"""Pinned separate GPL worker and its complete, version-matched source closure.

No downloads or source copying occur while checking. Source ZIP creation is an
explicit final-release operation; runtime archives never contain nested sources.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
from release_layout import canonical_name, read_plain, write_zip

ROOT = Path(__file__).resolve().parents[1]
POINTER_NAME = 'SAVE-CODEC-SOURCE.json'
SOURCE_MANIFEST = 'SOURCE-MANIFEST.json'
SOURCE_ROOT_NAME = 'DriftersToolkit-SaveCodec-source'
MAX_SOURCE_BYTES = 128 * 1024 * 1024


def lock():
    value = json.loads(read_plain(Path(__file__).with_name('save_codec_lock.json'), 16384))
    if (value.get('schemaVersion') != 1 or value.get('component') != 'DriftersToolkitSaveCodec'
            or value.get('licence') != 'GPL-3.0-or-later' or set(value.get('targets', {})) != {'windows', 'linux'}
            or not re.fullmatch(r'[a-f0-9]{64}', value.get('sourceManifestSha256', ''))):
        raise RuntimeError('The SaveCodec release lock is invalid.')
    canonical_name(value['sourceArchive'])
    return value


def target(platform):
    value = lock()
    if platform not in value['targets']: raise RuntimeError('Choose a supported SaveCodec platform.')
    row = value['targets'][platform]
    # Keep the native process boundary and packaging boundary pinned together.
    source = read_plain(ROOT / 'src/save_codec_pin.hpp', 16384).decode('utf-8')
    branch = source.split('#else', 1)[0] if platform == 'windows' else source.split('#else', 1)[1]
    if ('SaveCodecFileName = ' + json.dumps(row['file']) not in branch
            or 'SaveCodecSha256 = ' + json.dumps(row['sha256']) not in branch):
        raise RuntimeError('The native SaveCodec identity and packaging lock differ.')
    return row


def verify_binary(content, platform):
    row = target(platform)
    if not isinstance(content, bytes) or len(content) != row['size'] or hashlib.sha256(content).hexdigest() != row['sha256']:
        raise RuntimeError('The SaveCodec worker differs from its reviewed platform pin.')
    if platform == 'windows':
        offset = int.from_bytes(content[60:64], 'little') if len(content) >= 64 else len(content)
        if content[:2] != b'MZ' or content[offset:offset + 6] != b'PE\0\0\x64\x86':
            raise RuntimeError('SaveCodec is not the reviewed x86-64 Windows program.')
    else:
        if content[:7] != b'\x7fELF\x02\x01\x01' or content[18:20] != b'\x3e\0':
            raise RuntimeError('SaveCodec is not the reviewed x86-64 Linux program.')
        # This separate worker uses static musl. Do not patch its RPATH or add
        # distro libraries; doing either would invalidate the exact binary pin.
        offset = int.from_bytes(content[32:40], 'little')
        size = int.from_bytes(content[54:56], 'little'); count = int.from_bytes(content[56:58], 'little')
        if size < 56 or count > 1024 or offset + size * count > len(content): raise RuntimeError('Invalid SaveCodec ELF program table.')
        if any(int.from_bytes(content[offset + n * size:offset + n * size + 4], 'little') == 3 for n in range(count)):
            raise RuntimeError('The SaveCodec worker unexpectedly depends on a system ELF interpreter.')
    return row


def verify_worker(path, platform):
    row = target(platform); path = Path(path)
    if path.name != row['file']: raise RuntimeError('The SaveCodec worker has an unexpected basename.')
    body = read_plain(path, 8 * 1024 * 1024)
    verify_binary(body, platform)
    return body


def source_pointer():
    value = lock()
    return {'schemaVersion': 1, 'component': value['component'], 'version': value['version'], 'licence': value['licence'],
            'sourceArchive': {'file': value['sourceArchive'], 'manifestSha256': value['sourceManifestSha256']},
            'binaries': {name: {key: row[key] for key in ('file', 'sha256', 'size')} for name, row in value['targets'].items()}}


def pointer_bytes():
    return (json.dumps(source_pointer(), indent=2) + '\n').encode('utf-8')


def verify_pointer(content):
    if len(content) > 16384: raise RuntimeError('The SaveCodec source pointer is too large.')
    def unique(pairs):
        value = {}
        for key, entry in pairs:
            if key in value: raise RuntimeError('Duplicate SaveCodec source-pointer field.')
            value[key] = entry
        return value
    value = json.loads(content, object_pairs_hook=unique)
    if value != source_pointer() or content != pointer_bytes(): raise RuntimeError('The SaveCodec source pointer is missing or version-mismatched.')
    return value


def source_manifest(content):
    value = lock()
    if len(content) > 2 * 1024 * 1024 or hashlib.sha256(content).hexdigest() != value['sourceManifestSha256']:
        raise RuntimeError('The complete SaveCodec source manifest differs from its reviewed pin.')
    manifest = json.loads(content)
    if any(manifest.get(key) != value[key] for key in ('schemaVersion', 'component', 'version', 'licence')):
        raise RuntimeError('The SaveCodec source component identity is wrong.')
    for row in value['targets'].values():
        if manifest.get('targets', {}).get(row['triple']) != {'sha256': row['sha256'], 'bytes': row['size']}:
            raise RuntimeError('The SaveCodec sources do not match both released workers.')
    rows = manifest.get('files')
    if not isinstance(rows, list) or not 1 <= len(rows) <= 8192: raise RuntimeError('The SaveCodec source inventory is unbounded.')
    names = set(); total = 0
    for row in rows:
        if not isinstance(row, dict) or set(row) != {'path', 'bytes', 'sha256'}: raise RuntimeError('Invalid SaveCodec source record.')
        name = canonical_name(row['path'])
        if (name.casefold() in names or name == SOURCE_MANIFEST or type(row['bytes']) is not int or not 0 <= row['bytes'] <= 32 * 1024 * 1024
                or not re.fullmatch('[a-f0-9]{64}', row['sha256'])): raise RuntimeError('Invalid or duplicate SaveCodec source identity.')
        names.add(name.casefold()); total += row['bytes']
    if total > MAX_SOURCE_BYTES: raise RuntimeError('The SaveCodec sources exceed their size limit.')
    required = {'COPYING', 'NOTICE.txt', 'BUILDING.txt', 'build.py', 'Cargo.toml', 'Cargo.lock', '.cargo/config.toml', 'src/main.rs'}
    if not required <= {row['path'] for row in rows}: raise RuntimeError('Complete SaveCodec sources, notices and offline build instructions are required.')
    return manifest


def verify_source(root):
    root = Path(root).absolute()
    manifest = source_manifest(read_plain(root / SOURCE_MANIFEST, 2 * 1024 * 1024))
    expected = {row['path']: row for row in manifest['files']}
    observed = set()
    for path in root.rglob('*'):
        details = path.lstat()
        if path.is_symlink() or getattr(details, 'st_file_attributes', 0) & 0x400: raise RuntimeError('A SaveCodec source path is redirected.')
        if path.is_dir(): continue
        name = path.relative_to(root).as_posix(); observed.add(name)
        if name == SOURCE_MANIFEST: continue
        row = expected.get(name)
        if row is None: raise RuntimeError('Unexpected file in the SaveCodec source closure: ' + name)
        body = read_plain(path, 32 * 1024 * 1024)
        if len(body) != row['bytes'] or hashlib.sha256(body).hexdigest() != row['sha256']: raise RuntimeError('A SaveCodec source file is missing or changed: ' + name)
    if observed != set(expected) | {SOURCE_MANIFEST}: raise RuntimeError('The SaveCodec source closure is incomplete.')
    return manifest


def collect_source(root):
    root = Path(root); manifest = verify_source(root)
    contents = {name: read_plain(root / name, 32 * 1024 * 1024) for name in [SOURCE_MANIFEST, *(row['path'] for row in manifest['files'])]}
    verify_source_members(contents)
    return contents


def notices(root, manifest=None):
    root = Path(root); manifest = verify_source(root) if manifest is None else manifest
    names = [row['path'] for row in manifest['files'] if row['path'] in {'COPYING', 'NOTICE.txt'} or row['path'].startswith('runtime-notices/')
             or Path(row['path']).name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE', 'COPYRIGHT'))]
    result = {'save-codec/' + name: read_plain(root / name, 32 * 1024 * 1024) for name in names}
    expected = {row['path']: row for row in manifest['files']}
    for name, body in result.items():
        row = expected[name[len('save-codec/'):]]
        if len(body) != row['bytes'] or hashlib.sha256(body).hexdigest() != row['sha256']: raise RuntimeError('A SaveCodec notice changed after source verification.')
    value = lock()
    result['save-codec/SOURCE.txt'] = (value['component'] + ' ' + value['version'] + ' is a separate GPL-3.0-or-later component.\n'
        'Complete matching source and rebuild instructions: ' + value['sourceArchive'] + '\n'
        'SAVE-CODEC-SOURCE.json identifies the exact workers and source manifest.\n').encode('utf-8')
    return result



def installed_files(files, platform):
    worker = target(platform)['file']
    enabled = worker in files or POINTER_NAME in files
    if enabled:
        if worker not in files or POINTER_NAME not in files: raise RuntimeError('The SaveCodec worker and matching source pointer must ship together.')
        verify_binary(files[worker], platform); verify_pointer(files[POINTER_NAME])
    return enabled


def provenance(source, platform):
    manifest = verify_source(source); rows = notices(source, manifest)
    value = lock(); worker = target(platform)
    return {'component': value['component'], 'version': value['version'], 'licence': value['licence'],
            'binary': {key: worker[key] for key in ('file', 'sha256', 'size')},
            'source': {'archive': value['sourceArchive'], 'manifestSha256': value['sourceManifestSha256']},
            'noticeFiles': [{'path': 'licenses/' + name, 'sha256': hashlib.sha256(body).hexdigest(), 'size': len(body)} for name, body in sorted(rows.items())]}, rows


def verify_provenance(value, files, platform):
    pinned = lock(); worker = target(platform)
    if not isinstance(value, dict) or set(value) != {'component', 'version', 'licence', 'binary', 'source', 'noticeFiles'} or any(value.get(key) != pinned[key] for key in ('component', 'version', 'licence')):
        raise RuntimeError('The SaveCodec dependency provenance is missing or invalid.')
    if value['binary'] != {key: worker[key] for key in ('file', 'sha256', 'size')} or value['source'] != {'archive': pinned['sourceArchive'], 'manifestSha256': pinned['sourceManifestSha256']}:
        raise RuntimeError('The SaveCodec dependency provenance is not version-matched.')
    rows = value['noticeFiles']; names = set()
    if not isinstance(rows, list) or not 3 <= len(rows) <= 1024: raise RuntimeError('The SaveCodec notices are missing or unbounded.')
    for row in rows:
        if not isinstance(row, dict) or set(row) != {'path', 'sha256', 'size'}: raise RuntimeError('Invalid SaveCodec notice identity.')
        name = canonical_name(row['path'])
        if not name.startswith('licenses/save-codec/') or name in names or type(row['size']) is not int or not 0 <= row['size'] <= 32 * 1024 * 1024 or not re.fullmatch('[a-f0-9]{64}', row['sha256']): raise RuntimeError('Invalid SaveCodec notice path or bounds.')
        if name not in files or len(files[name]) != row['size'] or hashlib.sha256(files[name]).hexdigest() != row['sha256']: raise RuntimeError('A SaveCodec notice is missing or changed: ' + name)
        names.add(name)
    if hashlib.sha256(json.dumps(rows, sort_keys=True, separators=(',', ':')).encode()).hexdigest() != pinned.get('noticeInventorySha256'): raise RuntimeError('The complete SaveCodec notice inventory differs from the reviewed source closure.')
    if not {'licenses/save-codec/COPYING', 'licenses/save-codec/NOTICE.txt', 'licenses/save-codec/SOURCE.txt'} <= names: raise RuntimeError('The SaveCodec GPL and matching-source notices are incomplete.')
    return names


def verify_source_members(files):
    """Check a standalone source archive or flattened companion bytes."""
    if SOURCE_MANIFEST not in files: raise RuntimeError('The SaveCodec source manifest is missing.')
    manifest = source_manifest(files[SOURCE_MANIFEST]); expected = {row['path']: row for row in manifest['files']}
    if set(files) != set(expected) | {SOURCE_MANIFEST}: raise RuntimeError('The SaveCodec source package has missing or extra files.')
    for name, row in expected.items():
        if len(files[name]) != row['bytes'] or hashlib.sha256(files[name]).hexdigest() != row['sha256']:
            raise RuntimeError('A packaged SaveCodec source file changed: ' + name)
    return manifest



def verify_source_records(observed, prefix='save-codec/'):
    """Match flattened corresponding-source TAR identities without private paths."""
    manifest_name=prefix+SOURCE_MANIFEST
    if manifest_name not in observed or not isinstance(observed[manifest_name].get('sourceManifest'),bytes):
        raise RuntimeError('The complete SaveCodec corresponding source manifest is missing.')
    manifest=source_manifest(observed[manifest_name]['sourceManifest'])
    expected={prefix+entry['path']:entry for entry in manifest['files']}
    actual={name for name in observed if name.startswith(prefix)}
    if actual!=set(expected)|{manifest_name}: raise RuntimeError('The SaveCodec corresponding source has missing or extra files.')
    for name,row in expected.items():
        if observed[name]['sha256']!=row['sha256'] or observed[name]['size']!=row['bytes']:
            raise RuntimeError('A corresponding SaveCodec source differs from its pin: '+name)
    return manifest


def package_source(root, destination):
    content = collect_source(root); target = Path(destination) / lock()['sourceArchive']
    return write_zip(target, SOURCE_ROOT_NAME, content, {name for name in content if name.endswith('.sh')})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path); parser.add_argument('--destination', type=Path)
    args = parser.parse_args()
    if args.destination: print(package_source(args.source, args.destination))
    else: print(json.dumps({'verifiedSourceFiles': len(verify_source(args.source)['files']) + 1, 'pointer': source_pointer()}, indent=2))
