"""Build the pinned official Qt WebEngine security backports in a fresh SDK copy.

Developer tool: preserves the original SDK, does not install host packages, and
never disables Chromium's sandbox. The original source and all vendor patches
remain separate reproducible inputs. Requires Linux, clang/lld 19, maintained
Git/tar, and the Qt WebEngine build prerequisites documented by Qt.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name('qt_webengine_security_lock.json')
PROOF = 'QTWEBENGINE-SECURITY-PROVENANCE.json'


def file_record(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise RuntimeError('A WebEngine build input/output is not a plain file: ' + str(path))
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'sha256': digest, 'size': path.stat().st_size}


def load_lock(path=LOCK):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    if (data.get('schemaVersion') != 1 or data.get('recipeRevision') != 1
            or data.get('qtVersion') != '6.11.3' or len(data.get('patches', [])) != 24):
        raise RuntimeError('Unsupported Qt WebEngine security recipe.')
    previous = data.get('baseChromiumCommit')
    for row in data['patches']:
        if (not re.fullmatch('[a-f0-9]{40}', row.get('commit', ''))
                or row.get('parent') != previous
                or not re.fullmatch(r'[0-9]{2}-[a-f0-9]{40}\.patch', row.get('name', ''))
                or not re.fullmatch('[a-f0-9]{64}', row.get('sha256', ''))
                or type(row.get('size')) is not int or not 0 < row['size'] < 1024 * 1024
                or row.get('url') != 'https://code.qt.io/cgit/qt/qtwebengine-chromium.git/patch/?h=140-based&id=' + row['commit']):
            raise RuntimeError('The pinned official WebEngine patch chain is invalid.')
        previous = row['commit']
    if previous != data.get('patchedChromiumCommit'):
        raise RuntimeError('The WebEngine patch chain does not reach its pinned target.')
    for advisory in data.get('criticalAdvisories', []):
        if not any(advisory in row['title'] for row in data['patches']):
            raise RuntimeError('A required critical vendor backport is missing.')
    return data


def verify_inputs(root=ROOT, lock=LOCK):
    data = load_lock(lock)
    for row in data['patches']:
        patch = Path(root) / data['patchDirectory'] / row['name']
        if file_record(patch) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('A pinned official WebEngine patch changed: ' + row['name'])
        body = patch.read_text(encoding='utf-8')
        if not body.startswith('From ' + row['commit'] + ' '):
            raise RuntimeError('A WebEngine patch commit identity differs.')
        for name in re.findall(r'^diff --git a/(.*?) b/', body, re.M):
            relative = PurePosixPath(name)
            if (relative.is_absolute() or '..' in relative.parts or '\\' in name
                    or relative.parts[0] not in ('chromium', 'gn', 'ninja')):
                raise RuntimeError('A WebEngine vendor patch leaves the vendor source tree.')
    return data


def unpack(archive, destination):
    """Validate all archive paths/links before using tar on hash-verified bytes."""
    with tarfile.open(archive, 'r:*') as source:
        members = source.getmembers()
        if len(members) > 1000000 or sum(member.size for member in members) > 16 * 1024 ** 3:
            raise RuntimeError('The upstream source exceeds its extraction bound.')
        for member in members:
            name = PurePosixPath(member.name)
            if name.is_absolute() or '..' in name.parts or '\\' in member.name:
                raise RuntimeError('An upstream archive path is redirected.')
            if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                raise RuntimeError('An upstream archive contains a special file.')
            if member.issym() or member.islnk():
                target = PurePosixPath(member.linkname)
                resolved = (destination / name.parent / target if member.issym()
                            else destination / target).resolve()
                if target.is_absolute() or not resolved.is_relative_to(destination.resolve()):
                    raise RuntimeError('An upstream archive link leaves its destination.')
    subprocess.run(['tar', '--extract', '--file', str(archive), '--directory', str(destination),
                    '--no-same-owner', '--no-same-permissions'], check=True)


def verify_build(prefix, root=ROOT, lock=LOCK):
    prefix = Path(prefix).resolve(strict=True)
    data = verify_inputs(root, lock)
    proof = json.loads((prefix / PROOF).read_text(encoding='utf-8'))
    if (proof.get('schemaVersion') != 1 or proof.get('recipeRevision') != data['recipeRevision']
            or proof.get('securityLock') != file_record(lock)
            or proof.get('recipe') != file_record(__file__)
            or proof.get('patchedChromiumCommit') != data['patchedChromiumCommit']
            or proof.get('sourceArchive') != {'sha256': data['sourceArchive']['digest'], 'size': data['sourceArchive']['size']}
            or proof.get('patches') != [{key: row[key] for key in ('commit', 'sha256', 'size')} for row in data['patches']]):
        raise RuntimeError('The updated Qt WebEngine build provenance is missing or stale.')
    if not proof.get('outputs') or not any(row['file'].startswith('lib/libQt6WebEngineCore.so.') for row in proof['outputs']):
        raise RuntimeError('The updated Qt WebEngine build has no core library proof.')
    for row in proof['outputs']:
        relative = PurePosixPath(row['file'])
        if relative.is_absolute() or '..' in relative.parts:
            raise RuntimeError('A Qt WebEngine build output path is redirected.')
        if file_record(prefix / relative) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('An updated Qt WebEngine build output differs: ' + row['file'])
    return proof


def public_provenance(proof):
    """The public source identity excludes local compiler paths/command lines."""
    return {key: proof[key] for key in ('recipeRevision', 'securityLock', 'recipe', 'sourceArchive',
            'baseChromiumCommit', 'patchedChromiumCommit', 'declaredSecurityVersion', 'patches',
            'criticalAdvisories', 'outputs')}


def build(source_archive, sdk, work, prefix, jobs=2):
    data = verify_inputs()
    source_archive, sdk = Path(source_archive).resolve(strict=True), Path(sdk).resolve(strict=True)
    work, prefix = Path(work).absolute(), Path(prefix).absolute()
    if not 1 <= jobs <= 2:
        raise RuntimeError('This bounded WebEngine recipe allows one or two build jobs.')
    for target in (work, prefix):
        if target.exists() or target.is_symlink():
            raise RuntimeError('Use a fresh WebEngine build/prefix; existing files were preserved.')
        for parent in target.parents:
            if parent.is_symlink():
                raise RuntimeError('A WebEngine build destination has a redirected parent.')
    source_pin = data['sourceArchive']
    if file_record(source_archive) != {'sha256': source_pin['digest'], 'size': source_pin['size']}:
        raise RuntimeError('The complete Qt WebEngine source differs from its official archive pin.')
    work.mkdir(parents=True)
    source = work / 'source';source.mkdir()
    unpack(source_archive, source)
    for row in data['patches']:
        patch = ROOT / data['patchDirectory'] / row['name']
        subprocess.run(['git', 'apply', '--check', str(patch)], cwd=source / 'src/3rdparty', check=True)
        subprocess.run(['git', 'apply', str(patch)], cwd=source / 'src/3rdparty', check=True)
    print('Applied complete pinned official 24-commit WebEngine series.', flush=True)
    shutil.copytree(sdk, prefix, symlinks=True)
    original_core = {path.name: file_record(path) for path in (prefix / 'lib').glob('libQt6WebEngineCore.so.*')
                     if path.is_file() and not path.is_symlink()}
    # The clone is a build prefix, so remove any old proof copied from a previous
    # overlay. Publication below happens only after successful install and hashes.
    (prefix / PROOF).unlink(missing_ok=True)
    node = data['nodeBuildInput'];node_archive = work / node['name']
    with urllib.request.urlopen(node['url'], timeout=90) as response, node_archive.open('xb') as output:
        shutil.copyfileobj(response, output, 1024 * 1024)
    if file_record(node_archive)['sha256'] != node['sha256']:
        raise RuntimeError('The build-only Node archive differs from the official checksum.')
    unpack(node_archive, work)
    env = dict(os.environ)
    env['PATH'] = str(work / node['name'].removesuffix('.tar.xz') / 'bin') + os.pathsep + env['PATH']
    # Qt launches a separate Ninja for Chromium; --parallel on the outer CMake
    # build alone does not constrain it. Pin the nested build before configure.
    env['NINJAFLAGS'] = '-j ' + str(jobs)
    env['CMAKE_BUILD_PARALLEL_LEVEL'] = str(jobs)
    output = work / 'build';output.mkdir()
    flags = '-march=x86-64-v2 -mtune=generic -fstack-protector-strong -ffile-prefix-map=' + str(work) + '=/usr/src/drifters-qt-webengine'
    configure = [str(prefix / 'bin/qt-configure-module'), str(source), '--', '-GNinja',
                 '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
                 '-DCMAKE_C_COMPILER=clang-19', '-DCMAKE_CXX_COMPILER=clang++-19',
                 '-DCMAKE_C_FLAGS=' + flags, '-DCMAKE_CXX_FLAGS=' + flags,
                 '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld-19', '-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld-19',
                 '-DQT_BUILD_EXAMPLES=OFF', '-DQT_BUILD_TESTS=OFF', '-DQT_FEATURE_webengine_jumbo_build=ON',
                 '-DQT_FEATURE_webengine_system_libwebp=OFF']
    with (work / 'configure.log').open('w') as log:
        subprocess.run(configure, cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (work / 'build.log').open('w') as log:
        subprocess.run(['cmake', '--build', str(output), '--parallel', str(jobs)], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    with (work / 'install.log').open('w') as log:
        subprocess.run(['cmake', '--install', str(output)], env=env,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    if not original_core or not any(file_record(prefix / 'lib' / name) != record for name, record in original_core.items()):
        raise RuntimeError('The WebEngine source build did not replace the original core library.')
    outputs = []
    for directory in ('lib', 'libexec', 'resources', 'translations/qtwebengine_locales', 'plugins', 'qml'):
        for path in sorted((prefix / directory).rglob('*')):
            if path.is_file() and not path.is_symlink() and ('WebEngine' in path.name or directory in ('resources', 'translations/qtwebengine_locales')):
                outputs.append({'file': path.relative_to(prefix).as_posix(), **file_record(path)})
    proof = {'schemaVersion': 1, 'recipeRevision': data['recipeRevision'], 'qtVersion': data['qtVersion'],
             'securityLock': file_record(LOCK), 'recipe': file_record(__file__),
             'sourceArchive': file_record(source_archive), 'baseChromiumCommit': data['baseChromiumCommit'],
             'patchedChromiumCommit': data['patchedChromiumCommit'], 'declaredSecurityVersion': data['declaredSecurityVersion'],
             'patches': [{key: row[key] for key in ('commit', 'sha256', 'size')} for row in data['patches']],
             'criticalAdvisories': data['criticalAdvisories'], 'configure': configure, 'outputs': outputs}
    (prefix / PROOF).write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    verify_build(prefix)
    print('Verified updated Qt WebEngine SDK:', prefix, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', type=Path, help='Verify an existing updated SDK without building.')
    parser.add_argument('--source-archive', type=Path)
    parser.add_argument('--sdk-prefix', type=Path)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--prefix', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.verify:
        verify_build(args.verify)
        print('Qt WebEngine security build proof verified.')
    elif sys.platform != 'linux' or not all((args.source_archive, args.sdk_prefix, args.work, args.prefix)):
        parser.error('Build requires Linux and --source-archive, --sdk-prefix, --work and --prefix.')
    else:
        build(args.source_archive, args.sdk_prefix, args.work, args.prefix, args.jobs)
