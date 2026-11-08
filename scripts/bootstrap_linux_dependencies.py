"""Build the pinned portable Linux libraries on the Debian 12 ABI baseline."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from release_layout import read_plain, canonical_name

ROOT = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name('linux_dependencies_lock.json')
PROOF = 'LINUX-DEPENDENCIES-PROVENANCE.json'
REVISION = 2


def identity(path):
    path = Path(path)
    info = path.lstat()
    if not path.is_file() or path.is_symlink() or info.st_nlink != 1 or info.st_size > 256 * 1024 * 1024:
        raise RuntimeError('A dependency input is not an independent bounded file: ' + path.name)
    with path.open('rb') as stream:
        return {'size': info.st_size, 'sha256': hashlib.file_digest(stream, 'sha256').hexdigest()}


def load_lock():
    value = json.loads(read_plain(LOCK, 1024 * 1024))
    if value.get('schemaVersion') != 1 or value.get('recipeRevision') != REVISION:
        raise RuntimeError('Unsupported Linux dependency lock.')
    packages = value.get('packages', [])
    if len(packages) != 12 or len({p['name'] for p in packages}) != len(packages):
        raise RuntimeError('The Linux library collection is incomplete.')
    names = set()
    for package in packages:
        for entry in package['sources'] + package.get('patches', []):
            canonical_name(entry['file'])
            if entry.get('directory'):canonical_name(entry['directory'])
            for target in entry.get('targetFiles',[]):canonical_name(target['file'])
            if entry['file'] in names or not re.fullmatch('[a-f0-9]{64}', entry['sha256']):
                raise RuntimeError('Duplicate or invalid dependency source identity.')
            names.add(entry['file'])
        for name in package['libraries']:
            if '/' in name or not re.fullmatch(r'[A-Za-z0-9_.+-]+', name):
                raise RuntimeError('Invalid dependency SONAME.')
    return value


def verify_sources(cache):
    lock = load_lock()
    for package in lock['packages']:
        for entry in package['sources']:
            if identity(Path(cache) / entry['file']) != {k: entry[k] for k in ('size', 'sha256')}:
                raise RuntimeError('Missing or changed dependency source: ' + entry['file'])
        for entry in package.get('patches', []):
            if identity(ROOT / entry['file']) != {k: entry[k] for k in ('size', 'sha256')}:
                raise RuntimeError('Missing or changed dependency repair: ' + entry['file'])
    return lock


def fetch(cache):
    from build_fetch import fetch_pinned
    for package in load_lock()['packages']:
        for entry in package['sources']:
            fetch_pinned(entry['url'], Path(cache) / entry['file'], entry['sha256'], entry['size'])
    verify_sources(cache)


def verify_build(prefix):
    prefix = Path(prefix).absolute()
    proof = json.loads(read_plain(prefix / PROOF, 1024 * 1024))
    if (proof.get('schemaVersion') != 1 or proof.get('recipeRevision') != REVISION or
            proof.get('sourceLock') != identity(LOCK) or proof.get('recipe') != identity(Path(__file__))):
        raise RuntimeError('The Linux dependency build does not match the current recipe and source lock.')
    expected = {name for p in load_lock()['packages'] for name in p['libraries']}
    rows = proof.get('outputs', [])
    if {r['name'] for r in rows} != expected or len(rows) != len(expected):
        raise RuntimeError('The Linux dependency build output inventory is incomplete.')
    for row in rows:
        if identity(prefix / 'runtime' / row['name']) != {k: row[k] for k in ('size', 'sha256')}:
            raise RuntimeError('A rebuilt dependency changed: ' + row['name'])
    return proof


def verify_public(proof, observed, by_component):
    lock = load_lock()
    if (proof.get('schemaVersion') != 1 or proof.get('sourceLock') != identity(LOCK) or
            proof.get('recipe') != identity(Path(__file__))):
        raise RuntimeError('The current Linux dependency source/build identity is missing.')
    verify_bindings(proof)
    for package in lock['packages']:
        entries = by_component.get('linux-dependency:' + package['name'], [])
        for source in package['sources']:
            matches = [e for e in entries if e.get('originalFile') == source['file']]
            if len(matches) != 1 or any(observed[matches[0]['file']][k] != source[k] for k in ('size', 'sha256')):
                raise RuntimeError('A rebuilt dependency source is missing or changed: ' + source['file'])
        for patch in package.get('patches', []):
            name = patch['file']
            if name not in observed or any(observed[name][k] != patch[k] for k in ('size', 'sha256')):
                raise RuntimeError('A dependency repair is missing from corresponding source: ' + name)


def verify_bindings(proof, appdir=None, locally_modified=False):
    expected = {name:p['name'] for p in load_lock()['packages'] for name in p['libraries']}
    rows = proof.get('outputs', [])
    outputs = {r.get('name'):r for r in rows}
    if set(outputs) != set(expected) or len(rows) != len(expected):
        raise RuntimeError('Incomplete maintained-library build outputs.')
    def valid(value):
        return (isinstance(value,dict) and type(value.get('size')) is int and
                0 < value['size'] <= 256*1024*1024 and
                isinstance(value.get('sha256'),str) and re.fullmatch('[a-f0-9]{64}',value['sha256']))
    if any(not valid(r) or r.get('package') != expected[name] for name,r in outputs.items()):
        raise RuntimeError('Invalid maintained-library output identity.')
    bindings = proof.get('runtimeBindings', [])
    names = set()
    for binding in bindings:
        name = binding.get('name')
        if name not in expected or name in names or binding.get('path') != 'usr/lib/' + name:
            raise RuntimeError('Invalid or duplicate maintained-library runtime binding.')
        names.add(name)
        if (binding.get('original') != {k:outputs[name][k] for k in ('size','sha256')} or
                not valid(binding.get('staged'))):
            raise RuntimeError('A runtime library is not bound to its maintained build output.')
        if appdir is not None:
            actual=identity(Path(appdir)/binding['path'])
            if not locally_modified and actual != binding['staged']:
                raise RuntimeError('A maintained runtime library changed after staging: ' + name)
    core = {'libz.so.1','libsqlite3.so','libglib-2.0.so.0','libdbus-1.so.3','libnss3.so','libsoftokn3.so'}
    if not names or not (core & set(expected)) <= names:
        raise RuntimeError('A required maintained runtime library was omitted.')


def verify_staged(appdir,locally_modified=False):
    appdir = Path(appdir)
    data = json.loads(read_plain(appdir/'DEPENDENCY-SOURCES.json',8*1024*1024))
    verify_bindings(data.get('linuxDependencies',{}),appdir,locally_modified)
    removed = {'libsecret-1.so.0','libgtk-3.so.0','libgdk-3.so.0','libgdk_pixbuf-2.0.so.0',
               'libgio-2.0.so.0','libsystemd.so.0','libmount.so.1','libblkid.so.1','libgcrypt.so.20'}
    if any((appdir/'usr/lib'/name).exists() for name in removed):
        raise RuntimeError('An unused desktop/account dependency was pulled back into the portable runtime.')


def build(cache, prefix, scratch, tools, jobs=2):
    from package_portable_linux import baseline, MINIMUM_GLIBC
    baseline()
    lock = verify_sources(cache)
    prefix, scratch, tools = (Path(p).absolute() for p in (prefix, scratch, tools))
    for path in (prefix, scratch):
        if path == Path('/') or path.is_symlink() or any(p.is_symlink() for p in path.parents):
            raise RuntimeError('Use an independent plain dependency build directory.')
        path.mkdir(parents=True, exist_ok=True)
    if not 1 <= jobs <= 2:
        raise RuntimeError('Build these libraries with one or two workers.')
    (prefix / 'lib').mkdir(exist_ok=True)
    (prefix / 'runtime').mkdir(exist_ok=True)
    (prefix / 'notices').mkdir(exist_ok=True)
    # Keep compiler/header and loader probes on the native build filesystem.
    # The durable prefix may be on a slower mounted filesystem under WSL.
    build_prefix=scratch/'library-prefix'
    build_prefix.mkdir(exist_ok=True)
    def mirror():
        # Recreate only this mirror's build-system aliases before updating it.
        # copytree does not replace an existing symlink with symlinks=True.
        for path in build_prefix.rglob('*'):
            if path.is_symlink():path.unlink()
        for name in ('lib','include','bin'):
            if (prefix/name).is_dir():shutil.copytree(prefix/name,build_prefix/name,symlinks=True,dirs_exist_ok=True)
        for pc in (build_prefix/'lib/pkgconfig').glob('*.pc'):
            pc.write_text(re.sub(r'^prefix=.*$',lambda _:'prefix='+str(build_prefix),pc.read_text(),flags=re.M))
    mirror()
    environment = {k: v for k, v in os.environ.items() if not k.startswith(('LD_', 'QT_', 'QTWEBENGINE_'))}
    environment.update(PATH=str(tools / 'bin') + os.pathsep + environment['PATH'],
                       PKG_CONFIG_PATH=str(build_prefix / 'lib/pkgconfig'), LD_LIBRARY_PATH=str(build_prefix / 'lib'),
                       CFLAGS='-O2 -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2 -ffile-prefix-map=' + str(scratch) + '=/usr/src/drifters-toolkit-dependencies',
                       CXXFLAGS='-O2 -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2 -ffile-prefix-map=' + str(scratch) + '=/usr/src/drifters-toolkit-dependencies',
                       LDFLAGS='-Wl,-z,relro,-z,now,-z,noexecstack -L' + str(build_prefix / 'lib'),
                       ac_cv_func_arc4random='no', ac_cv_func_arc4random_buf='no')
    outputs = []
    for package in lock['packages']:
        name = package['name']
        inputs = hashlib.sha256(json.dumps({'recipeRevision':REVISION,'package':package}, sort_keys=True).encode()).hexdigest()
        stamp = prefix / (name + '-build.json')
        prior = json.loads(stamp.read_text()) if stamp.exists() else {}
        valid = (prior.get('inputs') == inputs and all(
            (prefix / 'runtime' / r['name']).is_file() and
            identity(prefix / 'runtime' / r['name']) == {k: r[k] for k in ('size', 'sha256')}
            for r in prior.get('outputs', [])) and
            {r['name'] for r in prior.get('outputs', [])} == set(package['libraries']))
        if valid:
            outputs.extend(prior['outputs'])
            print('Verified retained library build: ' + name, flush=True)
            continue
        directory = scratch / name
        if directory.exists():
            raise RuntimeError('Preserve or explicitly remove an incomplete build before retrying: ' + str(directory))
        directory.mkdir()
        source, obj, installed = directory / 'source', directory / 'build', directory / 'installed'
        obj.mkdir()
        descriptor = next(r for r in package['sources'] if r['file'].endswith('.dsc'))
        with (prefix / (name + '-build.log')).open('w') as log:
            def run(args, cwd=obj):
                command=[str(a) for a in args];command_env=environment.copy()
                if command[0]=='make':
                    command_env['MAKEFLAGS']='--no-print-directory'
                    for item in command[1:]:
                        if re.match(r'^[A-Z_][A-Z0-9_]*=',item):
                            key,value=item.split('=',1);command_env[key]=value
                subprocess.run(command, cwd=cwd, env=command_env,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            print('Building maintained library: ' + name + ' ' + package['version'], flush=True)
            run(['dpkg-source', '-x', str(Path(cache).absolute() / descriptor['file']), source], directory)
            for repair in package.get('patches', []):
                patch_root=source / repair.get('directory','')
                run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i', ROOT / repair['file']], patch_root)
                for target in repair.get('targetFiles',[]):
                    if identity(patch_root/target['file'])!={k:target[k] for k in ('size','sha256')}:
                        raise RuntimeError('A repaired source differs from its pinned upstream target: '+target['file'])
            fmt = dict(prefix='/usr', build_prefix=str(build_prefix), stage=str(installed), source=str(source), build=str(obj), jobs=str(jobs),
                       python=str(tools / 'bin/python'), tools=str(tools), cflags=environment['CFLAGS'], ldflags=environment['LDFLAGS'])
            for command in package['commands']:
                run([arg.format(**fmt) for arg in command['args']], Path(command.get('cwd', '{build}').format(**fmt)))
            if installed.exists():
                for folder in (installed/'usr').iterdir():
                    if folder.is_dir() and folder.name in ('lib','include','bin'):
                        shutil.copytree(folder,prefix/folder.name,symlinks=True,dirs_exist_ok=True)
                for pc in (prefix/'lib/pkgconfig').glob('*.pc'):
                    text=pc.read_text()
                    pc.write_text(re.sub(r'^prefix=/usr$',lambda _: 'prefix='+str(prefix),text,flags=re.M))
                mirror()
            rows = []
            for library in package['libraries']:
                candidates = [p for p in (prefix / 'lib' / library, source / 'dist/lib' / library) if p.is_file()]
                if len(candidates) != 1:
                    raise RuntimeError('A maintained library is missing or ambiguous: ' + library)
                actual = candidates[0].resolve(strict=True)
                target = prefix / 'runtime' / library
                if target.exists():
                    raise RuntimeError('A prior runtime output was preserved: ' + library)
                shutil.copyfile(actual, target)
                target.chmod(0o644)
                symbols = subprocess.run(['readelf', '--version-info', str(target)], capture_output=True, text=True, check=True).stdout
                versions = [tuple(map(int, s.split('.'))) for s in re.findall(r'\bGLIBC_([0-9.]+)', symbols)]
                if versions and max(versions) > MINIMUM_GLIBC:
                    raise RuntimeError('A rebuilt library raises the supported glibc floor: ' + library)
                rows.append({'name': library, 'package': name, **identity(target)})
            shutil.copyfile(source / 'debian/copyright', prefix / 'notices' / (name + '-copyright'))
            stamp.write_text(json.dumps({'inputs': inputs, 'outputs': rows}, indent=2) + '\n')
            outputs.extend(rows)
        # Only the exact per-component scratch directory created above is removed.
        if directory.parent != scratch or directory.is_symlink():
            raise RuntimeError('The build scratch path changed type.')
        shutil.rmtree(directory)
        print('Built and verified: ' + name, flush=True)
    proof = {'schemaVersion': 1, 'recipeRevision': REVISION, 'sourceLock': identity(LOCK),
             'recipe': identity(Path(__file__)), 'outputs': sorted(outputs, key=lambda r: r['name'])}
    (prefix / PROOF).write_text(json.dumps(proof, indent=2) + '\n')
    verify_build(prefix)
    print('All maintained Linux dependency outputs verified.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('fetch', 'verify-sources', 'build', 'verify-build'))
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--prefix', type=Path)
    parser.add_argument('--scratch', type=Path)
    parser.add_argument('--tools', type=Path)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    if args.mode == 'fetch': fetch(args.cache)
    elif args.mode == 'verify-sources': verify_sources(args.cache)
    elif args.mode == 'verify-build': verify_build(args.prefix)
    else: build(args.cache, args.prefix, args.scratch, args.tools, args.jobs)


if __name__ == '__main__': main()
