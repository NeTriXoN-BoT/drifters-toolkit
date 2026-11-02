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

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name('qt_webengine_security_lock.json')
VENDOR_INVENTORY = Path(__file__).with_name('qt_webengine_patched_files.json')
FINAL_INVENTORY = Path(__file__).with_name('qt_webengine_final_files.json')
PROOF = 'QTWEBENGINE-SECURITY-PROVENANCE.json'
RUNTIME_TARGETS = ('WebEngineCore', 'WebEngineWidgets', 'QtWebEngineProcess')
RUNTIME_INSTALL_SCRIPTS = ('src/core/api/cmake_install.cmake', 'src/process/cmake_install.cmake',
                           'src/webenginewidgets/cmake_install.cmake')
RUNTIME_RESOURCES = ('icudtl.dat', 'qtwebengine_resources.pak', 'qtwebengine_devtools_resources.pak',
                     'qtwebengine_resources_100p.pak', 'qtwebengine_resources_200p.pak', 'v8_context_snapshot.bin')


def runtime_scope():
    return {'targets': list(RUNTIME_TARGETS), 'installScripts': list(RUNTIME_INSTALL_SCRIPTS),
            'embeddedPrintingAndPdf': True}


def verify_output_scope(rows):
    names = set()
    for row in rows:
        name = row.get('file', '')
        allowed = (name in ('lib/libQt6WebEngineCore.so.6.11.3', 'lib/libQt6WebEngineWidgets.so.6.11.3',
                            'libexec/QtWebEngineProcess') or name in {'resources/' + value for value in RUNTIME_RESOURCES}
                   or re.fullmatch(r'translations/qtwebengine_locales/[A-Za-z0-9_-]+\.pak', name))
        if not allowed or name in names:
            raise RuntimeError('The updated runtime proof claims an unbuilt/unshipped module or duplicate output: ' + name)
        names.add(name)
    required = {'lib/libQt6WebEngineCore.so.6.11.3', 'lib/libQt6WebEngineWidgets.so.6.11.3',
                'libexec/QtWebEngineProcess', 'translations/qtwebengine_locales/en-US.pak',
                *('resources/' + value for value in RUNTIME_RESOURCES)}
    if not required <= names:
        raise RuntimeError('The required updated Core/Widgets/process/resources/locales proof is incomplete.')


def runtime_outputs(prefix):
    prefix = Path(prefix)
    paths = [prefix / 'lib/libQt6WebEngineCore.so.6.11.3', prefix / 'lib/libQt6WebEngineWidgets.so.6.11.3',
             prefix / 'libexec/QtWebEngineProcess', *(prefix / 'resources' / name for name in RUNTIME_RESOURCES)]
    paths.extend(sorted((prefix / 'translations/qtwebengine_locales').glob('*.pak')))
    rows = [{'file': path.relative_to(prefix).as_posix(), **file_record(path)} for path in paths]
    verify_output_scope(rows)
    return rows


def file_record(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file():
        raise RuntimeError('A WebEngine build input/output is not a plain file: ' + str(path))
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'sha256': digest, 'size': path.stat().st_size}


def load_lock(path=LOCK):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    if (data.get('schemaVersion') != 1 or data.get('recipeRevision') != 2
            or data.get('qtVersion') != '6.11.3' or len(data.get('patches', [])) != 24
            or len(data.get('compatibilityBackports', [])) != 1):
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
    for row in data['compatibilityBackports']:
        files=row.get('files',[])
        if (row.get('name')!='qt140-aura-dependency-compatibility.patch' or len(files)!=9 or len(set(files))!=9
                or set(row.get('before',{}))!=set(files) or set(row.get('after',{}))!=set(files)):
            raise RuntimeError('The separately identified Aura dependency scope is incomplete.')
        for name in files:
            relative=PurePosixPath(name)
            if relative.is_absolute() or '..' in relative.parts or '\\' in name or not name.startswith('chromium/ui/'):
                raise RuntimeError('An Aura dependency leaves its pinned source scope.')
            for phase in ('before','after'):
                value=row[phase][name]
                if (set(value)!={'sha256','size'} or not re.fullmatch('[a-f0-9]{64}',value.get('sha256',''))
                        or type(value.get('size')) is not int or not 0<value['size']<1024*1024):
                    raise RuntimeError('An Aura dependency pre/final source identity is invalid.')
        references=row.get('upstream',[])
        expected={'c57a349825ed93a8ac373519fb7da5319fb75335':'8051545','5c2010f1a7ea14ead6d4a9848cce9f783ddd9fc1':'8127178'}
        if len(references)!=2 or {value.get('commit') for value in references}!=set(expected):
            raise RuntimeError('The exact primary Aura dependency/security references are missing.')
        for value in [row,*references]:
            if (not re.fullmatch('[a-f0-9]{64}',value.get('sha256','')) or type(value.get('size')) is not int
                    or not 0<value['size']<1024*1024 or '/' in value.get('name','') or '\\' in value.get('name','')):
                raise RuntimeError('An Aura dependency patch input is invalid.')
        for value in references:
            expected_name=('Google-M144-LTS-' if value['commit'].startswith('c57') else 'Google-security-')+value['commit']+'.patch'
            if (value.get('name')!=expected_name or value.get('url')!='https://chromium-review.googlesource.com/changes/chromium%2Fsrc~'+expected[value['commit']]+'/revisions/'+value['commit']+'/patch'):
                raise RuntimeError('An Aura dependency reference is not its exact primary patch.')
    return data


def all_patch_inputs(data):
    return [*data['patches'],*(value for row in data['compatibilityBackports'] for value in (row,*row['upstream']))]


def source_inventory_identity():
    return {'vendor':file_record(VENDOR_INVENTORY),'final':file_record(FINAL_INVENTORY)}


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
    for row in data['compatibilityBackports']:
        for value in (row,*row['upstream']):
            patch=Path(root)/data['patchDirectory']/value['name']
            if file_record(patch)!={key:value[key] for key in ('sha256','size')}:
                raise RuntimeError('A pinned Aura dependency/reference patch changed: '+value['name'])
        body=(Path(root)/data['patchDirectory']/row['name']).read_text(encoding='utf-8')
        before=re.findall(r'^--- a/(.*)$',body,re.M);after=re.findall(r'^\+\+\+ b/(.*)$',body,re.M)
        if before!=after or len(before)!=9 or set(before)!=set(row['files']):
            raise RuntimeError('The Aura adapter diff differs from its declared source scope.')
    return data


def apply_compatibility(source,data=None,root=ROOT):
    """Apply only the reviewed adapter, after all exact preimages match."""
    data=verify_inputs(root) if data is None else data
    source=Path(source).resolve(strict=True)
    for row in data['compatibilityBackports']:
        for name,value in row['before'].items():
            path=source/'src/3rdparty'/name
            if not path.resolve(strict=True).is_relative_to(source) or file_record(path)!=value:
                raise RuntimeError('An Aura adapter source preimage differs: '+name)
        for name in row['excludedAbsentAshFiles']:
            if (source/'src/3rdparty/chromium'/name).exists():
                raise RuntimeError('The source unexpectedly contains excluded ChromeOS inputs.')
        patch=Path(root)/data['patchDirectory']/row['name']
        subprocess.run(['git','apply','--check',str(patch)],cwd=source/'src/3rdparty',check=True)
        subprocess.run(['git','apply',str(patch)],cwd=source/'src/3rdparty',check=True)
        for name,value in row['after'].items():
            if file_record(source/'src/3rdparty'/name)!=value:
                raise RuntimeError('An applied Aura adapter final source differs: '+name)
    return data['compatibilityBackports']


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
            or proof.get('storageHelper') != file_record(Path(__file__).with_name('linux_build_storage.py'))
            or proof.get('runtimeScope') != runtime_scope()
            or proof.get('compatibilityBackports') != data['compatibilityBackports']
            or proof.get('sourceInventories') != source_inventory_identity()
            or proof.get('patchedChromiumCommit') != data['patchedChromiumCommit']
            or proof.get('sourceArchive') != {'sha256': data['sourceArchive']['digest'], 'size': data['sourceArchive']['size']}
            or proof.get('patches') != [{key: row[key] for key in ('commit', 'sha256', 'size')} for row in data['patches']]):
        raise RuntimeError('The updated Qt WebEngine build provenance is missing or stale.')
    verify_output_scope(proof.get('outputs', []))
    for row in proof['outputs']:
        relative = PurePosixPath(row['file'])
        if relative.is_absolute() or '..' in relative.parts:
            raise RuntimeError('A Qt WebEngine build output path is redirected.')
        if file_record(prefix / relative) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('An updated Qt WebEngine build output differs: ' + row['file'])
    if 'continuation' in proof:
        from resume_qt_webengine_security import verify_continuation
        verify_continuation(proof['continuation'])
    return proof


def public_provenance(proof):
    """The public source identity excludes local compiler paths/command lines."""
    result = {key: proof[key] for key in ('recipeRevision', 'securityLock', 'recipe', 'storageHelper', 'runtimeScope', 'sourceArchive',
            'baseChromiumCommit', 'patchedChromiumCommit', 'declaredSecurityVersion', 'patches',
            'criticalAdvisories', 'compatibilityBackports', 'sourceInventories', 'outputs')}
    if 'continuation' in proof:
        result['continuation'] = proof['continuation']
    return result


def build(source_archive, sdk, work, prefix, jobs=2, storage_attestation=None, expected_host_drive=None):
    data = verify_inputs()
    source_archive, sdk = Path(source_archive).resolve(strict=True), Path(sdk).resolve(strict=True)
    work, prefix = Path(work).absolute(), Path(prefix).absolute()
    if type(jobs) is not int or not 1 <= jobs <= 3:
        raise RuntimeError('This bounded WebEngine recipe allows one to three build jobs.')
    for target in (work, prefix):
        if target.exists() or target.is_symlink():
            raise RuntimeError('Use a fresh WebEngine build/prefix; existing files were preserved.')
        for parent in target.parents:
            if parent.is_symlink():
                raise RuntimeError('A WebEngine build destination has a redirected parent.')
    from linux_build_storage import check_build_storage, project_build_lock
    storage = check_build_storage((work, prefix), storage_attestation, expected_host_drive)
    with project_build_lock(storage):
        return _build_after_storage(data, source_archive, sdk, work, prefix, jobs, storage, storage_attestation, expected_host_drive)


def _build_after_storage(data, source_archive, sdk, work, prefix, jobs, storage, storage_attestation, expected_host_drive):
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
    apply_compatibility(source,data)
    from resume_qt_webengine_security import load_resume_inputs,verify_source
    _,_,final_files=load_resume_inputs()
    verify_source(source,final_files)
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
    if not re.search(r'^FEATURE_webengine_printing_and_pdf:BOOL=ON$', (output / 'CMakeCache.txt').read_text(encoding='utf-8'), re.M):
        raise RuntimeError('The required Core embedded printing/PDF feature was not preserved.')
    from resume_qt_webengine_security import run_phase
    from linux_build_storage import RuntimeBudget
    budget = RuntimeBudget((work, prefix), storage_attestation, expected_host_drive, work / 'build-budget.json')
    run_phase(['cmake', '--build', str(output), '--target', *RUNTIME_TARGETS, '--parallel', '1'],
              work / 'build.log', env, work / 'build-process.json', 'runtime-build', budget, True)
    verify_source(source,final_files)
    for script in RUNTIME_INSTALL_SCRIPTS:
        run_phase(['cmake', '-DCMAKE_INSTALL_PREFIX=' + str(prefix), '-P', str(output / script)],
                  work / 'install.log', env, work / 'build-process.json', 'runtime-install', budget, True)
    if not original_core or not any(file_record(prefix / 'lib' / name) != record for name, record in original_core.items()):
        raise RuntimeError('The WebEngine source build did not replace the original core library.')
    outputs = runtime_outputs(prefix)
    proof = {'schemaVersion': 1, 'recipeRevision': data['recipeRevision'], 'qtVersion': data['qtVersion'],
             'securityLock': file_record(LOCK), 'recipe': file_record(__file__),
             'storageHelper': file_record(Path(__file__).with_name('linux_build_storage.py')),
             'compatibilityBackports': data['compatibilityBackports'], 'sourceInventories': source_inventory_identity(),
             'runtimeScope': runtime_scope(),
             'sourceArchive': file_record(source_archive), 'baseChromiumCommit': data['baseChromiumCommit'],
             'patchedChromiumCommit': data['patchedChromiumCommit'], 'declaredSecurityVersion': data['declaredSecurityVersion'],
             'patches': [{key: row[key] for key in ('commit', 'sha256', 'size')} for row in data['patches']],
             'criticalAdvisories': data['criticalAdvisories'], 'configure': configure, 'outputs': outputs}
    proof['buildStorage'] = storage
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
    parser.add_argument('--storage-attestation', type=Path, help='Required on WSL: fresh local operator host-volume attestation.')
    parser.add_argument('--expected-host-drive', help='Required on WSL: explicitly approved backing drive, such as D:.')
    args = parser.parse_args()
    if args.verify:
        verify_build(args.verify)
        print('Qt WebEngine security build proof verified.')
    elif sys.platform != 'linux' or not all((args.source_archive, args.sdk_prefix, args.work, args.prefix)):
        parser.error('Build requires Linux and --source-archive, --sdk-prefix, --work and --prefix.')
    else:
        build(args.source_archive, args.sdk_prefix, args.work, args.prefix, args.jobs, args.storage_attestation, args.expected_host_drive)
