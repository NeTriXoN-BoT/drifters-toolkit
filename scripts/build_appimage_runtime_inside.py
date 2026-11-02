"""Offline, ordinary-user build of the separately supplied AppImage runtime sources."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tarfile
import tempfile


def sha(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def run(args, cwd=None, env=None):
    print('+ ' + ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def extract(source, destination, expected_root):
    """Source archives are pinned; still refuse escapes, devices and excess expansion."""
    destination = Path(destination)
    destination.mkdir(exist_ok=False)
    with tarfile.open(source) as archive:
        names = set()
        total = 0
        members = archive.getmembers()
        if len(members) > 20000:
            raise RuntimeError('Too many runtime source archive members.')
        for member in members:
            path = PurePosixPath(member.name)
            if (not member.name or path.is_absolute() or '\\' in member.name
                    or '..' in path.parts or path.parts[0] != expected_root
                    or member.name in names or member.size < 0
                    or member.size > 64 * 1024 * 1024):
                raise RuntimeError('Unsafe runtime source archive member.')
            names.add(member.name)
            if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                raise RuntimeError('Unsupported runtime source archive member type.')
            total += member.size
            if total > 512 * 1024 * 1024:
                raise RuntimeError('Runtime source expansion exceeds the limit.')
        archive.extractall(destination, members=members, filter='data')
    return destination / expected_root


def patch_fuse(path):
    """Retain AppImage's explicit helper route without searching a mutable PATH."""
    path = Path(path)
    text = path.read_text()
    original = '''\tconst char *full_path = FUSERMOUNT_DIR "/" FUSERMOUNT_PROG;
\tpid_t pid;'''
    replacement = '''\t/* Drifters Toolkit packaging, 20 October 2026:
\t * Rebased AppImage helper selection for libfuse 3.18.3.
\t * Never fall back to searching PATH for this executable. */
\tconst char *full_path = getenv("FUSERMOUNT_PROG");
\tif (full_path == NULL || full_path[0] != '/')
\t\treturn -EINVAL;
\tpid_t pid;'''
    fallback = '''\tif (status != 0) {
\t\t/* if that fails, try a system install */
\t\tstatus = posix_spawnp(&pid, FUSERMOUNT_PROG, action, NULL,
\t\t\t\t      (char * const *) argv, environ);
\t}
'''
    if text.count(original) != 1 or text.count(fallback) != 1:
        raise RuntimeError('The reviewed libfuse helper patch no longer matches.')
    path.write_text(text.replace(original, replacement).replace(fallback, ''))


def patch_runtime(path):
    path = Path(path)
    text = path.read_text()
    original = '''        ssize_t length = readlink(appimage_path, fullpath, sizeof(fullpath));
        if (length < 0) {'''
    replacement = '''        /* Drifters Toolkit packaging, 20 October 2026:
         * Reserve a terminator and refuse a possibly truncated link. */
        ssize_t length = readlink(appimage_path, fullpath, sizeof(fullpath) - 1);
        if (length < 0 || (size_t) length >= sizeof(fullpath) - 1) {'''
    original_format = '''        sprintf(portable_dir, "%s.%s", fullpath, name);'''
    replacement_format = '''        int written = snprintf(portable_dir, sizeof(portable_dir), "%s.%s", fullpath, name);
        if (written < 0 || (size_t) written >= sizeof(portable_dir)) {
            fprintf(stderr, "Portable directory path is too long\\n");
            exit(EXIT_FAILURE);
        }'''
    if text.count(original) != 2 or text.count(original_format) != 1:
        raise RuntimeError('The reviewed AppImage path-bounds patch no longer matches.')
    path.write_text(text.replace(original, replacement).replace(original_format, replacement_format))


def build(materials, output, lock):
    if os.geteuid() == 0:
        raise RuntimeError('Build the runtime as an ordinary container user.')
    materials, output = Path(materials), Path(output)
    lock = json.loads(Path(lock).read_text())
    for row in lock['sources']:
        path = materials / 'sources' / row['file']
        if path.is_symlink() or path.stat().st_size != row['size'] or sha(path) != row['sha256']:
            raise RuntimeError('A runtime source differs from the lock.')
    expected_packages = {'musl': '1.2.5-r11', 'musl-dev': '1.2.5-r11',
                         'gcc': '14.2.0-r4', 'libgcc': '14.2.0-r4'}
    installed = subprocess.check_output(['apk', 'info', '-v'], text=True).splitlines()
    if any(f'{name}-{version}' not in installed for name, version in expected_packages.items()):
        raise RuntimeError('The static C/toolchain libraries differ from their source recipes.')
    with tempfile.TemporaryDirectory(prefix='drifter-appimage-runtime-') as scratch:
        work = Path(scratch)
        prefix = work / 'prefix'
        prefix.mkdir()
        maps = ' -ffile-prefix-map=' + str(work) + '=/build/runtime -fmacro-prefix-map=' + str(work) + '=/build/runtime'
        env = dict(os.environ, CC='gcc', CXX='g++', LC_ALL='C',
                   CFLAGS='-Os -ffunction-sections -fdata-sections -fPIC' + maps,
                   CXXFLAGS='-Os -ffunction-sections -fdata-sections -fPIC' + maps,
                   SOURCE_DATE_EPOCH='1762646400')
        env['PKG_CONFIG_PATH'] = str(prefix / 'lib/pkgconfig')
        env['CMAKE_PREFIX_PATH'] = str(prefix)
        dirs = {}
        for row in lock['sources']:
            if row.get('build'):
                dirs[row['component']] = extract(materials / 'sources' / row['file'],
                                                work / row['component'], row['root'])
        zlib = dirs['zlib']
        run(['./configure', '--static', '--prefix=' + str(prefix)], zlib, env)
        run(['make', '-j2'], zlib, env)
        run(['make', 'install'], zlib, env)
        run(['make', '-j2', '-C', dirs['zstd'] / 'lib', 'libzstd.a'], env=env)
        run(['make', '-C', dirs['zstd'] / 'lib', 'install-static', 'install-includes',
             'install-pc', 'PREFIX=' + str(prefix)], env=env)
        mimalloc = work / 'mimalloc-build'
        run(['cmake', '-S', dirs['mimalloc'], '-B', mimalloc, '-G', 'Ninja',
             '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(prefix),
             '-DMI_BUILD_SHARED=OFF', '-DMI_BUILD_STATIC=ON', '-DMI_BUILD_OBJECT=OFF',
             '-DMI_BUILD_TESTS=OFF', '-DMI_SECURE=ON', '-DMI_OVERRIDE=ON',
             '-DMI_INSTALL_TOPLEVEL=ON'], env=env)
        run(['cmake', '--build', mimalloc, '--parallel', '2'], env=env)
        run(['cmake', '--install', mimalloc], env=env)
        fuse = dirs['fuse']
        before = (fuse / 'lib/mount.c').read_bytes()
        patch_fuse(fuse / 'lib/mount.c')
        import difflib
        diff = ''.join(difflib.unified_diff(before.decode().splitlines(True),
                                           (fuse / 'lib/mount.c').read_text().splitlines(True),
                                           fromfile='a/lib/mount.c', tofile='b/lib/mount.c'))
        (output / 'libfuse-helper.patch').write_text(diff)
        fuse_build = work / 'fuse-build'
        run(['meson', 'setup', fuse_build, fuse, '--prefix=' + str(prefix), '--libdir=lib',
             '--default-library=static', '--buildtype=minsize', '-Dutils=false',
             '-Dexamples=false', '-Dtests=false', '-Duseroot=false',
             '-Ddisable-mtab=true', '-Denable-io-uring=false', '-Denable-custom-io=false'], env=env)
        run(['ninja', '-C', fuse_build, '-j2'], env=env)
        run(['ninja', '-C', fuse_build, 'install'], env=env)
        squash = dirs['squashfuse']
        run(['./configure', '--prefix=' + str(prefix), '--disable-shared', '--enable-static',
             '--without-lzma', '--without-lzo', '--without-lz4', '--with-zlib=' + str(prefix),
             '--with-zstd=' + str(prefix), '--with-fuse3=' + str(prefix),
             'LDFLAGS=-static -L' + str(prefix / 'lib'),
             'CPPFLAGS=-I' + str(prefix / 'include')], squash, env)
        run(['make', '-j2'], squash, env)
        run(['make', 'install'], squash, env)
        headers = prefix / 'include/squashfuse'
        headers.mkdir(exist_ok=True)
        for header in squash.glob('*.h'):
            shutil.copyfile(header, headers / header.name)
        runtime = dirs['runtime'] / 'src/runtime'
        before_runtime = (runtime / 'runtime.c').read_bytes()
        patch_runtime(runtime / 'runtime.c')
        runtime_diff = ''.join(difflib.unified_diff(before_runtime.decode().splitlines(True),
                                                   (runtime / 'runtime.c').read_text().splitlines(True),
                                                   fromfile='a/src/runtime/runtime.c', tofile='b/src/runtime/runtime.c'))
        (output / 'runtime-hardening.patch').write_text(runtime_diff)
        (runtime / 'version').write_text('https://github.com/AppImage/type2-runtime/commit/'
                                       + lock['runtimeCommit'] + ' (Drifters Toolkit dependency rebuild)\n')
        flags = ['-std=gnu99', '-Os', '-D_FILE_OFFSET_BITS=64',
                 '-DGIT_COMMIT="' + lock['runtimeCommit'] + '-drifter-dependencies"',
                 '-T', 'data_sections.ld', '-ffunction-sections', '-fdata-sections',
                 '-Wl,--gc-sections', '-static-pie', '-Wall', '-Werror',
                 '-ffile-prefix-map=' + str(work) + '=/build/runtime',
                 '-fmacro-prefix-map=' + str(work) + '=/build/runtime',
                 '-Wl,-Map=' + str(output / 'runtime-link-map.txt'),
                 '-Wl,-z,relro,-z,now']
        libraries = ['-lsquashfuse_ll', '-lsquashfuse', '-lzstd', '-lz', '-lfuse3', '-lmimalloc-secure']
        run(['gcc', '-I' + str(headers), '-I' + str(prefix / 'include/fuse3'),
             '-I' + str(prefix / 'include'), '-L' + str(prefix / 'lib'),
             *flags, 'runtime.c', '-Wl,--start-group', *libraries, '-Wl,--end-group',
             '-pthread', '-o', output / 'runtime-unstripped'], runtime, env)
        run(['objcopy', '--only-keep-debug', output / 'runtime-unstripped', output / 'runtime-x86_64.debug'])
        run(['strip', '--strip-debug', '--strip-unneeded', '-o', output / 'runtime-x86_64', output / 'runtime-unstripped'])
        with (output / 'runtime-x86_64').open('r+b') as target:
            target.seek(8)
            target.write(b'AI\x02')
        (output / 'runtime-x86_64').chmod(0o755)
        program = subprocess.check_output(['readelf', '-l', output / 'runtime-x86_64'], text=True)
        dynamic = subprocess.check_output(['readelf', '-d', output / 'runtime-x86_64'], text=True)
        if 'INTERP' in program or '(NEEDED)' in dynamic:
            raise RuntimeError('The rebuilt AppImage runtime unexpectedly requires shared libraries.')
        link_map = (output / 'runtime-link-map.txt').read_text()
        linked = sorted({Path(name).name for name in re.findall(r'([^\s(]+\.a)(?:\(|\s)', link_map)})
        allowed = {'libsquashfuse_ll.a', 'libsquashfuse.a', 'libzstd.a', 'libz.a',
                   'libfuse3.a', 'libmimalloc-secure.a', 'libc.a', 'libgcc.a',
                   'libgcc_eh.a', 'libssp_nonshared.a', 'libatomic.a',
                   'libpthread.a', 'librt.a', 'libm.a', 'libdl.a'}
        if not linked or set(linked) - allowed:
            raise RuntimeError('A linked static runtime library has no selected source provenance.')
        version = subprocess.check_output([output / 'runtime-x86_64', '--appimage-version'], text=True, stderr=subprocess.STDOUT, timeout=10)
        # Real runtime invocations in the private scratch directory exercise the
        # suffix/truncated-link boundaries that prompted the source patch.
        short = work / 'short-runtime'
        shutil.copyfile(output / 'runtime-x86_64', short)
        short.chmod(0o755)
        subprocess.run([short, '--appimage-portable-home'], check=True, capture_output=True, timeout=10)
        if not Path(str(short) + '.home').is_dir():
            raise RuntimeError('The normal runtime portable-directory fixture failed.')
        for size in (4091, 4095):
            directory = work / ('long-' + str(size))
            directory.mkdir()
            while size - len(str(directory)) - 1 > 200:
                directory /= 'd' * min(200, size - len(str(directory)) - 2)
                directory.mkdir()
            name = directory / ('r' * (size - len(str(directory)) - 1))
            shutil.copyfile(output / 'runtime-x86_64', name)
            name.chmod(0o755)
            attempt = subprocess.run([name, '--appimage-portable-home'], capture_output=True, text=True, timeout=10)
            if attempt.returncode == 0 or not any(message in attempt.stderr for message in
                                                  ['path is too long', 'Error getting realpath']):
                raise RuntimeError('A long runtime path was not refused by its explicit bounds check.')
        (output / 'runtime-unstripped').unlink()
        record = {'schemaVersion': 1, 'installedBuildPackages': sorted(installed),
                  'compiler': subprocess.check_output(['gcc', '--version'], text=True).splitlines()[0],
                  'runtimeVersion': version.strip(), 'staticElfVerified': True,
                  'linkedStaticLibraries': linked, 'nativePathBoundsChecks': 3,
                  'rootBuild': False, 'networkDuringBuild': False}
        (output / 'runtime-build-record.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--materials', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lock', type=Path, required=True)
    args = parser.parse_args()
    build(args.materials, args.output, args.lock)
