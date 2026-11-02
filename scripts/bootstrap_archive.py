"""Build verified static archive decoders in an OS-temp developer prefix.

No end-user installers, subprocess extractors or shared runtime DLLs are used.
Original dependency archives are retained intact for source-review packages.
"""
from pathlib import Path, PurePosixPath
import argparse
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
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name('archive_sources_lock.json')
RECIPE_REVISION = 4
LICENSES = {'libarchive': ('COPYING',), 'zlib': ('LICENSE',), 'bzip2': ('LICENSE',),
            'xz': ('COPYING', 'COPYING.0BSD'), 'zstd': ('LICENSE', 'COPYING'), 'lz4': ('lib/LICENSE',)}


def libarchive_notices(source):
    """Keep the controlling per-file copyrights, including the BSD 3-clause files."""
    groups = {}
    for path in sorted((Path(source) / 'libarchive').iterdir()):
        if path.suffix not in ('.c', '.h'): continue
        content = path.read_text(encoding='utf-8')
        match = re.match(r'\s*(/\*.*?\*/)', content, re.S)
        if match is None: continue
        notice = match.group(1)
        if not any(word in notice.lower() for word in ('copyright', 'redistribution', 'public domain', 'permission')): continue
        groups.setdefault(notice, []).append('libarchive/' + path.name)
    parts = ['LIBARCHIVE PER-FILE COPYRIGHT AND LICENCE NOTICES', '']
    for notice, names in groups.items(): parts.extend(['=' * 78, *names, '=' * 78, notice, ''])
    return ('\n'.join(parts) + '\n').encode('utf-8')


def source_lock():
    value = json.loads(LOCK.read_text(encoding='utf-8'))
    if value.get('schemaVersion') != 1 or len(value.get('sources', [])) != 6:
        raise RuntimeError('The archive source lock is invalid.')
    return value['sources']


def digest(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or not 0 <= info.st_size <= 256 * 1024 * 1024 or getattr(info, 'st_file_attributes', 0) & 0x400:
        raise RuntimeError('An archive dependency input is redirected or not an independent ordinary file.')
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''): value.update(block)
    return value.hexdigest()


def verify_sources(directory):
    directory = Path(directory).absolute()
    for source in source_lock():
        path = directory / source['name']
        if not path.is_file() or path.stat().st_size != source['size'] or digest(path) != source['sha256']:
            raise RuntimeError('Missing or changed pinned archive source: ' + source['name'])
    return source_lock()


def download(directory):
    directory = Path(directory).absolute(); directory.mkdir(parents=True, exist_ok=True)
    for source in source_lock():
        target = directory / source['name']
        if target.exists():
            if target.stat().st_size != source['size'] or digest(target) != source['sha256']:
                raise RuntimeError('An existing dependency archive was preserved because its identity differs: ' + target.name)
            continue
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(prefix='drifter-archive-download-', delete=False) as output:
                temporary = Path(output.name); count = 0; sha = hashlib.sha256()
                with urllib.request.urlopen(source['url'], timeout=120) as response:
                    if not response.url.startswith('https://'): raise RuntimeError('Dependency downloads require HTTPS.')
                    while block := response.read(1024 * 1024):
                        count += len(block)
                        if count > source['size']: raise RuntimeError('A dependency download exceeds its pinned size.')
                        output.write(block); sha.update(block)
            if count != source['size'] or sha.hexdigest() != source['sha256']:
                raise RuntimeError('A dependency download does not match its pinned SHA-256.')
            # Exclusive publication. Never replace an already-present source.
            with target.open('xb') as output, temporary.open('rb') as stream: shutil.copyfileobj(stream, output)
        finally:
            if temporary is not None: temporary.unlink(missing_ok=True)
    return verify_sources(directory)


def unpack(path, destination, expected):
    total = 0; seen = set()
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOCTTY', 0)
    with os.fdopen(os.open(path, flags), 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size != expected['size']:
            raise RuntimeError('A pinned dependency source changed before extraction.')
        content = stream.read(expected['size'] + 1)
        if len(content) != expected['size'] or hashlib.sha256(content).hexdigest() != expected['sha256']:
            raise RuntimeError('A pinned dependency source changed before extraction.')
    # Parse only the immutable, already size/SHA-verified bytes, never a path
    # reopened after validation. The largest source archive is less than 10 MB.
    with tarfile.open(fileobj=io.BytesIO(content), mode='r:gz') as archive:
        members = archive.getmembers()
        if len(members) > 30000: raise RuntimeError('Too many archive dependency source members.')
        for entry in members:
            name = entry.name.rstrip('/')
            parts = PurePosixPath(name).parts
            if not name or name.startswith('/') or '\\' in name or ':' in name or '..' in parts or '.' in parts or name.casefold() in seen:
                raise RuntimeError('Unsafe archive dependency source path.')
            seen.add(name.casefold())
            # The official Zstandard test tree contains two symlinks. They are
            # not build inputs; leave them in the intact source archive only.
            if entry.issym() and name.endswith(('/tests/cli-tests/bin/unzstd', '/tests/cli-tests/bin/zstdcat')): continue
            if not (entry.isdir() or entry.isfile()) or not 0 <= entry.size <= 128 * 1024 * 1024:
                raise RuntimeError('Unsafe archive dependency source member.')
            total += entry.size
            if total > 512 * 1024 * 1024: raise RuntimeError('Archive dependency source expands beyond its limit.')
            target = destination.joinpath(*parts)
            if entry.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(entry) as stream, target.open('xb') as output:
                    shutil.copyfileobj(stream, output, 1024 * 1024)
                if target.stat().st_size != entry.size: raise RuntimeError('Incomplete dependency source member.')
    roots = list(destination.iterdir())
    if len(roots) != 1 or not roots[0].is_dir(): raise RuntimeError('Unexpected dependency source root.')
    return roots[0]


def build(prefix, archives, cmake='cmake', ninja=None, cc=None, cxx=None):
    prefix = Path(prefix).absolute()
    temp = Path(tempfile.gettempdir()).resolve()
    if not prefix.resolve().is_relative_to(temp): raise RuntimeError('Archive build prefixes belong in the operating system temp directory.')
    rows = verify_sources(archives)
    if prefix.exists():
        verify_build(prefix, rows)
        print(prefix)
        return prefix
    prefix.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='drifter-archive-build-') as temporary:
        work = Path(temporary)
        sources = {}
        for row in rows:
            directory = work / (row['dependency'] + '-source'); directory.mkdir()
            sources[row['dependency']] = unpack(Path(archives) / row['name'], directory, row)
        staged = work / 'prefix'
        # Bound single-stream XZ/LZMA decoder dictionaries. The complete
        # upstream sources stay intact in the retained archive; this explicit
        # reproducible build-only edit is part of the shipped build recipe.
        xz_reader = sources['libarchive'] / 'libarchive/archive_read_support_filter_xz.c'
        body = xz_reader.read_text()
        needle = '#define LZMA_MEMLIMIT\tUINT64_MAX'
        if body.count(needle) != 1: raise RuntimeError('The reviewed XZ decoder memory-limit patch no longer applies.')
        xz_reader.write_text(body.replace(needle, '#define LZMA_MEMLIMIT\t(128ULL * 1024 * 1024)'))
        seven = sources['libarchive'] / 'libarchive/archive_read_support_format_7zip.c'
        body = seven.read_text()
        patches = (
            ('if (ci->numFolders > (uint64_t)zip->header_bytes_remaining)',
             'if (ci->numFolders > 1024 || ci->numFolders > (uint64_t)zip->header_bytes_remaining)'),
            ('if (zip->numFiles > SIZE_MAX / sizeof(*zip->entries))',
             'if (zip->numFiles > 1024 || zip->numFiles > SIZE_MAX / sizeof(*zip->entries))'),
            ('\n\t\tif (attr) {', '\n\t\tif (f->coders[i].numInStreams > 16 || f->coders[i].numOutStreams > 16)\n\t\t\treturn (-1);\n\n\t\tif (attr) {'),
            ('if ((p = header_bytes(\n\t\t\t    a, f->coders[i].propertiesSize)) == NULL)',
             'if (f->coders[i].propertiesSize > 4096)\n\t\t\t\treturn (-1);\n\t\t\tif ((p = header_bytes(\n\t\t\t    a, f->coders[i].propertiesSize)) == NULL)'),
            ('numInStreamsTotal < numOutStreamsTotal-1)',
             'numInStreamsTotal < numOutStreamsTotal-1 || numInStreamsTotal > 16 || numOutStreamsTotal > 16)'),
        )
        for needle, replacement in patches:
            if body.count(needle) != 1: raise RuntimeError('A reviewed 7z metadata-limit patch no longer applies.')
            body = body.replace(needle, replacement)
        seven.write_text(body)
        for filename, needle, replacement in (
            ('archive_read_support_format_7zip.c', 'r = lzma_raw_decoder(&(zip->lzstream), filters);',
             'r = lzma_raw_decoder_memusage(filters) > 128ULL * 1024 * 1024 ? LZMA_MEMLIMIT_ERROR : lzma_raw_decoder(&(zip->lzstream), filters);'),
            ('archive_read_support_filter_xz.c', 'ret = lzma_raw_decoder(&(xz->stream), filters);',
             'ret = lzma_raw_decoder_memusage(filters) > 128ULL * 1024 * 1024 ? LZMA_MEMLIMIT_ERROR : lzma_raw_decoder(&(xz->stream), filters);')):
            path = sources['libarchive'] / 'libarchive' / filename
            body = path.read_text()
            if body.count(needle) != 1: raise RuntimeError('The reviewed raw decoder memory-limit patch no longer applies.')
            path.write_text(body.replace(needle, replacement))
        shutil.copyfile(Path(__file__).with_name('archive_decoder_limits.h'), sources['libarchive'] / 'libarchive/drifter_archive_limits.h')
        for path in (sources['libarchive'] / 'libarchive').glob('*.c'):
            body = path.read_text()
            needle = '#include "archive_platform.h"'
            if needle in body: path.write_text(body.replace(needle, needle + '\n#include "drifter_archive_limits.h"', 1))
        common = ['-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_INSTALL_PREFIX=' + str(staged), '-DCMAKE_INSTALL_LIBDIR=lib',
                  '-DBUILD_SHARED_LIBS=OFF', '-DBUILD_TESTING=OFF', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON']
        if cc: common += ['-DCMAKE_C_COMPILER=' + str(cc)]
        if cxx: common += ['-DCMAKE_CXX_COMPILER=' + str(cxx)]
        if ninja: common += ['-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + str(ninja)]
        if not cc or 'clang' in str(cc) or 'gcc' in str(cc):
            common += ['-DCMAKE_C_FLAGS=-ffile-prefix-map=' + str(work).replace('\\', '/') + '=.']
        def run(source, name, options=()):
            directory = work / (name + '-build')
            arguments = [value.replace('\\', '/') for value in [*common, *options]]
            subprocess.run([str(cmake), '-S', str(source), '-B', str(directory), *arguments], check=True)
            subprocess.run([str(cmake), '--build', str(directory), '--parallel', '4'], check=True)
            subprocess.run([str(cmake), '--install', str(directory)], check=True)
        run(sources['zlib'], 'zlib', ['-DZLIB_BUILD_SHARED=OFF', '-DZLIB_BUILD_TESTING=OFF'])
        bz = sources['bzip2']
        (bz / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(DrifterBzip2 C)
add_library(bz2 STATIC blocksort.c huffman.c crctable.c randtable.c compress.c decompress.c bzlib.c)
target_compile_definitions(bz2 PUBLIC _FILE_OFFSET_BITS=64)
install(TARGETS bz2 ARCHIVE DESTINATION lib)
install(FILES bzlib.h DESTINATION include)
''', encoding='utf-8')
        run(bz, 'bzip2')
        run(sources['xz'], 'xz', ['-DXZ_TOOL_XZ=OFF', '-DXZ_TOOL_XZDEC=OFF', '-DXZ_TOOL_LZMADEC=OFF',
                                '-DXZ_TOOL_LZMAINFO=OFF', '-DXZ_NLS=OFF', '-DXZ_DOC=OFF'])
        run(sources['zstd'] / 'build/cmake', 'zstd', ['-DZSTD_BUILD_SHARED=OFF', '-DZSTD_BUILD_STATIC=ON',
                                                  '-DZSTD_BUILD_PROGRAMS=OFF', '-DZSTD_BUILD_TESTS=OFF', '-DZSTD_MULTITHREAD_SUPPORT=OFF'])
        run(sources['lz4'] / 'build/cmake', 'lz4', ['-DLZ4_BUILD_CLI=OFF', '-DLZ4_BUILD_LEGACY_LZ4C=OFF'])
        zlib_library = staged / ('lib/libzs.a' if os.name == 'nt' else 'lib/libz.a')
        if not zlib_library.is_file(): raise RuntimeError('The pinned static zlib build is missing.')
        run(sources['libarchive'], 'libarchive', ['-DCMAKE_PREFIX_PATH=' + str(staged), '-DENABLE_TEST=OFF',
            '-DENABLE_TAR=OFF', '-DENABLE_CPIO=OFF', '-DENABLE_CAT=OFF', '-DENABLE_UNZIP=OFF', '-DENABLE_ACL=OFF',
            '-DENABLE_XATTR=OFF', '-DENABLE_OPENSSL=OFF', '-DENABLE_CNG=OFF', '-DENABLE_MD=OFF', '-DENABLE_NETTLE=OFF',
            '-DENABLE_MBEDTLS=OFF', '-DENABLE_LIBB2=OFF', '-DENABLE_LZO=OFF', '-DENABLE_LIBXML2=OFF', '-DENABLE_BSDXML=OFF',
            '-DENABLE_EXPAT=OFF', '-DENABLE_WIN32_XMLLITE=OFF', '-DENABLE_PCREPOSIX=OFF', '-DENABLE_PCRE2POSIX=OFF',
            '-DENABLE_LIBGCC=OFF', '-DENABLE_ICONV=OFF', '-DPOSIX_REGEX_LIB=NONE', '-DZLIB_USE_STATIC_LIBS=ON',
            '-DZLIB_LIBRARY=' + str(zlib_library), '-DLZ4_LIBRARY=' + str(staged / 'lib/liblz4.a'),
            '-DZSTD_LIBRARY=' + str(staged / 'lib/libzstd.a')])
        notices = staged / 'licenses'; notices.mkdir()
        for dependency, names in LICENSES.items():
            for name in names:
                shutil.copyfile(sources[dependency] / name, notices / (dependency + '-' + Path(name).name))
        (notices / 'libarchive-source-notices.txt').write_bytes(libarchive_notices(sources['libarchive']))
        outputs = {str(p.relative_to(staged).as_posix()): {'sha256': digest(p), 'size': p.stat().st_size}
                   for folder in ('include', 'lib', 'licenses') for p in sorted((staged / folder).rglob('*'))
                   if p.is_file() and not p.is_symlink()}
        (staged / 'archive-build.json').write_text(json.dumps({'schemaVersion': 1, 'recipeRevision': RECIPE_REVISION,
            'limitHeaderSha256': digest(Path(__file__).with_name('archive_decoder_limits.h')), 'sources': rows, 'files': outputs,
            'options': 'Static decoders; no archive programs, crypto, XML or ACL. Libarchive individual allocation and LZMA dictionary 128 MiB; early 7z files/folders 1024, coder streams 16 and properties 4096 bytes.'}, indent=2) + '\n')
        shutil.copytree(staged, prefix)
    verify_build(prefix, rows)
    print(prefix)
    return prefix


def verify_build(prefix, rows=None):
    prefix = Path(prefix)
    proof = json.loads((prefix / 'archive-build.json').read_text(encoding='utf-8'))
    if (proof.get('schemaVersion') != 1 or proof.get('recipeRevision') != RECIPE_REVISION
            or proof.get('limitHeaderSha256') != digest(Path(__file__).with_name('archive_decoder_limits.h'))
            or proof.get('sources') != (rows or source_lock())):
        raise RuntimeError('The archive build proof differs from the current source lock or hardening recipe. Build a fresh OS-temp prefix.')
    files = proof.get('files')
    if not isinstance(files, dict) or not 10 <= len(files) <= 2048: raise RuntimeError('The archive build inventory is invalid.')
    for name, expected in files.items():
        path = prefix / name
        if not path.resolve().is_relative_to(prefix.resolve()) or path.stat().st_size != expected['size'] or digest(path) != expected['sha256']:
            raise RuntimeError('A static archive build input is missing or changed: ' + name)
    return proof


def public_provenance():
    return {'schemaVersion': 1, 'linkage': 'static', 'recipeRevision': RECIPE_REVISION,
            'limitHeaderSha256': digest(Path(__file__).with_name('archive_decoder_limits.h')), 'sources': source_lock()}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archives', type=Path, default=ROOT / 'vendor/archive-sources')
    parser.add_argument('--prefix', type=Path)
    parser.add_argument('--download', action='store_true')
    parser.add_argument('--cmake', default=os.environ.get('DRIFTER_CMAKE', 'cmake'))
    parser.add_argument('--ninja', default=os.environ.get('DRIFTER_NINJA'))
    parser.add_argument('--cc'); parser.add_argument('--cxx'); parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    if args.download: download(args.archives)
    if args.prefix:
        if args.verify: verify_build(args.prefix)
        else: build(args.prefix, args.archives, args.cmake, args.ninja, args.cc, args.cxx)
    elif not args.download: parser.error('Supply --download or --prefix.')
