"""Build a pinned AppImage runtime and retain its exact source/licence materials.

This is packaging tooling, never a loader runtime dependency. Sources are fetched
over approved HTTPS routes, checked before use, then compiled as an ordinary
container user with the network disabled. Existing output folders are preserved.
"""
from pathlib import Path, PurePosixPath
import argparse
import gzip
import hashlib
import io
import json
import os
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parent
HOSTS = {'github.com', 'codeload.github.com', 'raw.githubusercontent.com',
         'release-assets.githubusercontent.com', 'objects.githubusercontent.com',
         'musl.libc.org', 'gcc.gnu.org'}
MAX_INPUT = 128 * 1024 * 1024
RECIPE_FILES = ('build_appimage_runtime.py', 'build_appimage_runtime_inside.py',
                'Dockerfile.appimage-runtime', 'appimage_runtime.lock.json')


def require_url(url):
    try:
        target = urllib.parse.urlsplit(url)
        valid = (target.scheme == 'https' and target.hostname in HOSTS
                 and target.port in (None, 443) and not target.fragment
                 and '@' not in target.netloc and target.path.startswith('/')
                 and all(33 <= ord(char) <= 126 for char in url))
    except (ValueError, TypeError):
        valid = False
    if not valid:
        raise RuntimeError('Unexpected runtime-source URL.')
    return url


def relative(name):
    if not isinstance(name, str) or not name or len(name) > 240:
        raise RuntimeError('Invalid runtime-material path.')
    path = PurePosixPath(name)
    if (path.is_absolute() or '\\' in name or str(path) != name
            or any(part in {'.', '..'} or ':' in part for part in path.parts)
            or any(ord(char) < 32 or ord(char) == 127 for char in name)):
        raise RuntimeError('Unsafe runtime-material path.')
    return name


def plain(path, maximum=MAX_INPUT):
    path = Path(path).absolute()
    for parent in [path, *path.parents]:
        if parent.is_symlink() or (parent.exists() and getattr(parent.lstat(), 'st_file_attributes', 0) & 0x400):
            raise RuntimeError('A runtime-material path is redirected.')
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or not 0 <= info.st_size <= maximum:
        raise RuntimeError('A runtime material is not a bounded ordinary file.')
    return path


def digest(path):
    with plain(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def checked(path, row):
    path = plain(path)
    if path.stat().st_size != row['size'] or digest(path) != row['sha256']:
        raise RuntimeError('Runtime material differs and was preserved: ' + path.name)
    return path


class Redirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        require_url(newurl)
        return super().redirect_request(request, fp, code, message, headers, newurl)


def fetch(row, target, opener=None):
    require_url(row['url'])
    target = Path(target).absolute()
    for parent in target.parents:
        if parent.is_symlink() or (parent.exists() and getattr(parent.lstat(), 'st_file_attributes', 0) & 0x400):
            raise RuntimeError('The runtime-source cache is redirected.')
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() or target.is_symlink():
        return checked(target, row)
    partial = None
    deadline = time.monotonic() + 300
    try:
        with tempfile.NamedTemporaryFile(prefix='source-', dir=target.parent, delete=False) as output:
            partial = Path(output.name)
            count, sha = 0, hashlib.sha256()
            request = urllib.request.Request(row['url'], headers={'User-Agent': 'DriftersToolkit-Build/0.1'})
            with (opener or urllib.request.build_opener(Redirects())).open(request, timeout=90) as response:
                while chunk := response.read(1024 * 1024):
                    if time.monotonic() > deadline:
                        raise RuntimeError('Runtime source transfer timed out.')
                    count += len(chunk)
                    if count > row['size']:
                        raise RuntimeError('Runtime source response exceeds its pinned size.')
                    sha.update(chunk)
                    output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        if count != row['size'] or sha.hexdigest() != row['sha256']:
            raise RuntimeError('Runtime source checksum/size mismatch.')
        os.link(partial, target)
        partial.unlink()
        partial = None
    finally:
        if partial is not None:
            partial.unlink(missing_ok=True)
    return target


def row_for(path, folder, component=None, url=None):
    path = plain(path)
    result = {'file': relative(path.relative_to(folder).as_posix()),
              'sha256': digest(path), 'size': path.stat().st_size}
    if component is not None:
        result['component'] = component
    if url is not None:
        result['url'] = url
    return result


def write_new(path, body):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as output:
        output.write(body)
        output.flush()
        os.fsync(output.fileno())


def copy_new(source, target):
    source = plain(source)
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    with source.open('rb') as src, target.open('xb') as dst:
        shutil.copyfileobj(src, dst, 1024 * 1024)
        dst.flush()
        os.fsync(dst.fileno())
    if digest(source) != digest(target):
        raise RuntimeError('Runtime material changed while being copied.')


def load_lock(path=ROOT / 'appimage_runtime.lock.json'):
    value = json.loads(plain(path, 256 * 1024).read_text())
    if (value.get('schemaVersion') != 1 or not re.fullmatch('[a-f0-9]{40}', value.get('runtimeCommit', ''))
            or not re.fullmatch('alpine@sha256:[a-f0-9]{64}', value.get('baseImage', ''))
            or type(value.get('sources')) is not list or not 8 <= len(value['sources']) <= 100):
        raise RuntimeError('Invalid runtime-source lock.')
    names = set()
    for row in value['sources']:
        relative(row['file'])
        require_url(row['url'])
        if (row['file'].casefold() in names or not re.fullmatch('[a-f0-9]{64}', row['sha256'])
                or type(row['size']) is not int or not 0 < row['size'] <= MAX_INPUT
                or not re.fullmatch('[a-z][a-z0-9-]{0,40}', row['component'])):
            raise RuntimeError('Invalid runtime-source entry.')
        names.add(row['file'].casefold())
        if row.get('build'):
            relative(row['root'])
            if '/' in row['root']:
                raise RuntimeError('Invalid runtime source root.')
    return value


def recipe_archive(recipe, target):
    with target.open('xb') as raw, gzip.GzipFile(filename='', fileobj=raw, mode='wb', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as archive:
            for name in RECIPE_FILES:
                body = plain(recipe / name, 256 * 1024).read_bytes()
                info = tarfile.TarInfo(name)
                info.size, info.mode, info.mtime = len(body), 0o644, 0
                archive.addfile(info, io.BytesIO(body))


def licences(folder, lock):
    rows = []
    for source in lock['sources']:
        wanted = set(source.get('licences', []))
        if not wanted:
            continue
        with tarfile.open(folder / 'sources' / source['file']) as archive:
            for member in archive:
                if member.name not in wanted:
                    continue
                if not member.isfile() or not 0 < member.size < 256 * 1024:
                    raise RuntimeError('Invalid upstream licence member.')
                body = archive.extractfile(member).read()
                target = folder / 'licenses' / (source['component'] + '-' + Path(member.name).name + '.txt')
                write_new(target, body)
                rows.append(row_for(target, folder, source['component']))
                wanted.remove(member.name)
        if wanted:
            raise RuntimeError('A required upstream licence was not found.')
    return rows


BUILD_TEXT = '''AppImage runtime source and rebuild materials

This separate companion contains the exact unmodified upstream source archives,
Alpine musl/GCC package recipes and patches, required notices, and our complete
runtime build recipe. It is not a loader runtime ZIP or a development handoff.
Upstream README files inside complete third-party sources are intentionally kept.

Extract runtime-build-recipe.tar.gz to an empty folder. With Python 3.12+ and
Docker available, run:
  python build_appimage_runtime.py NEW_OUTPUT --cache THIS_MATERIALS/sources
The first run installs the pinned build-only container toolchain. Compilation
then runs as UID 1001 with the network disabled. Existing outputs are preserved.
All runtime dependency archives are included; no personal credential is needed.

The recipe rebases AppImage's explicit FUSERMOUNT_PROG helper selection onto
libfuse 3.18.3, uses maintained pinned compression/allocation sources, disables
libfuse custom I/O and io_uring, and does not install a setuid mount helper.
Normal mounting uses the operating system's separately maintained fusermount3.
FUSE-less extraction is tested separately with the completed AppImage.

Library modification/relinking is permitted by the applicable included licences.
Use the included runtime source and recipe to rebuild against modified libfuse;
no secret signing key is needed. Keep modified source notices and identify your
changes. The outer app's own rebuilt runtime manifest is a separate build step.

Pinned inputs and checksums identify selected bytes; they do not prove the
absence of vulnerabilities or compatibility with every host. The build-only
Alpine image does not determine the loader payload's glibc/Qt compatibility.
'''


def build(output, cache=None, image='drifters-appimage-runtime:20261020', prepare_only=False):
    lock = load_lock()
    if not re.fullmatch('[a-z0-9][a-z0-9._:/@-]{0,180}', image):
        raise RuntimeError('Invalid runtime build image name.')
    output = Path(output).absolute()
    if ',' in str(output):
        raise RuntimeError('A comma in the output path cannot be bound safely to Docker.')
    for parent in [output, *output.parents]:
        if parent.is_symlink() or (parent.exists() and getattr(parent.lstat(), 'st_file_attributes', 0) & 0x400):
            raise RuntimeError('The runtime output path is redirected.')
    output.mkdir(exist_ok=False)
    cache = Path(cache).absolute() if cache else output / 'sources'
    rows = []
    for source in lock['sources']:
        downloaded = fetch(source, cache / source['file'])
        target = output / 'sources' / source['file']
        if downloaded.absolute() != target.absolute():
            copy_new(downloaded, target)
        checked(target, source)
        rows.append(row_for(target, output, source['component'], source['url']))
    license_rows = licences(output, lock)
    recipe_archive(ROOT, output / 'runtime-build-recipe.tar.gz')
    write_new(output / 'BUILD.txt', BUILD_TEXT.encode())
    if prepare_only:
        return output
    with tempfile.TemporaryDirectory(prefix='drifter-runtime-build-') as temporary:
        temporary = Path(temporary)
        recipe = temporary / 'recipe'
        recipe.mkdir()
        results = temporary / 'results'
        results.mkdir()
        for name in RECIPE_FILES:
            copy_new(ROOT / name, recipe / name)
        subprocess.run(['docker', 'build', '--platform', 'linux/amd64', '-t', image,
                        '-f', str(recipe / 'Dockerfile.appimage-runtime'), str(recipe)], check=True)
        subprocess.run(['docker', 'run', '--rm', '--platform', 'linux/amd64', '--network', 'none',
                        '--user', '1001:1001', '--cpus', '2', '--memory', '3g',
                        '--mount', f'type=bind,source={output},target=/materials,readonly',
                        '--mount', f'type=bind,source={recipe},target=/recipe,readonly',
                        '--mount', f'type=bind,source={results},target=/results', image,
                        'python3', '/recipe/build_appimage_runtime_inside.py',
                        '--materials', '/materials', '--output', '/results',
                        '--lock', '/recipe/appimage_runtime.lock.json'], check=True)
        for name in ['runtime-x86_64', 'runtime-build-record.json', 'runtime-link-map.txt',
                     'libfuse-helper.patch', 'runtime-hardening.patch']:
            copy_new(results / name, output / name)
    # Recheck the complete immutable input snapshot after the build, before its
    # provenance can authorize packaging any output.
    for row in rows + license_rows:
        checked(output / row['file'], row)
    provenance = {'schemaVersion': 1, 'upstreamRuntimeCommit': lock['runtimeCommit'],
                  'baseImage': lock['baseImage'], 'runtime': row_for(output / 'runtime-x86_64', output),
                  'sources': rows, 'licenses': license_rows,
                  'recipe': row_for(output / 'runtime-build-recipe.tar.gz', output),
                  'records': [row_for(output / name, output) for name in
                              ['BUILD.txt', 'runtime-build-record.json', 'runtime-link-map.txt',
                               'libfuse-helper.patch', 'runtime-hardening.patch']]}
    write_new(output / 'runtime-provenance.json', (json.dumps(provenance, indent=2) + '\n').encode())
    print('Built runtime with complete selected source materials: ' + str(output))
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--image', default='drifters-appimage-runtime:20261020')
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    build(args.output, args.cache, args.image, args.prepare_only)
