"""Named release inputs and bounded ordinary-file reads shared by packagers."""
from pathlib import Path, PurePosixPath
import os
import stat
import hashlib
import io
import json
import re
import tempfile
import zipfile

PUBLIC_UI = (
    'index.html', 'app.js', 'style.css', 'help.js', 'changelog.js',
    'assets/hero.png', 'assets/icon.png', 'assets/app.ico',
    'assets/hero-forest.png', 'assets/hero-pass.png', 'assets/hero-outpost.png',
    'assets/hero-fogwood.png', 'assets/hero-tunnel.png', 'assets/hero-lookout.png',
    'assets/hero-sawmill.png', 'assets/hero-lake.png', 'assets/hero-quarantine.png',
    'assets/hero-camp.png', 'assets/hero-burn.png', 'assets/hero-freakers.png', 'assets/hero-breaker.png',
    'assets/seasonal-emoji.svg', 'assets/mod-art-the-clown.png',
)
COMMON_NOTICES = ('Drifters-Toolkit-LICENSE.txt', 'THIRD_PARTY.md', 'nlohmann-json-LICENSE.MIT', 'NotoEmoji-Images-LICENSE.txt', 'Archive-Decoder-LICENSES.txt')
LINUX_NOTICES = (*COMMON_NOTICES, 'GNU-LGPL-3.0.txt', 'GNU-GPL-3.0.txt', 'GNU-LGPL-2.1.txt')
WINDOWS_NOTICES = (*COMMON_NOTICES, 'WebView2-LICENSE.txt', 'WebView2-NOTICE.txt',
                   'LLVM-RUNTIME-LICENSE.txt', 'MinGW-w64-RUNTIME-LICENSE.txt')
# Every licence text ships in one LICENSES.txt next to the programs (no licence folder).
WINDOWS_RUNTIME = (
    'DriftersToolkit.exe', 'DriftersToolkitCLI.exe', 'WebView2Loader.dll',
    *('ui/'+name for name in PUBLIC_UI), 'catalog/catalog.json', 'config/nexus.json',
    'LICENSES.txt',
)
SAVE_CODEC_POINTER = 'SAVE-CODEC-SOURCE.json'
SAVE_CODEC_PROGRAMS = {'windows': 'DriftersToolkitSaveCodec.exe', 'linux': 'DriftersToolkitSaveCodec'}
SAVE_CODEC_WINDOWS_FILES = (SAVE_CODEC_PROGRAMS['windows'], SAVE_CODEC_POINTER)
RELEASE_NOTES_NAME = 'RELEASE-NOTES.json'
MAX_RELEASE_NOTES_BYTES = 16 * 1024
PRIVATE_SAVE_PREVIEW_PROGRAMS = ('DriftersToolkitSavePreview', 'DriftersToolkitSavePreview.exe')


def refuse_private_save_preview(*folders):
    """The private pinned decoder cannot enter a public/reviewer runtime yet.

    It is intentionally absent from runtime/source allowlists until the separate
    distribution and licensing decision is complete. Omitting a required helper
    silently would produce a runtime that fails its own integrity check.
    """
    for folder in folders:
        for name in PRIVATE_SAVE_PREVIEW_PROGRAMS:
            candidate = Path(folder) / name
            if candidate.exists() or candidate.is_symlink():
                raise RuntimeError('A private save-preview helper is present. Use a build without that helper; '
                                   'public packaging remains gated pending its distribution review.')


def release_notes(content, version):
    """Validate the same small plain-text record the native updater accepts.

    An unsigned record is packaging input only. The updater displays it only after
    checking its bytes against the file list authenticated by RELEASE-SIGNATURE.
    """
    if not isinstance(content, bytes) or len(content) > MAX_RELEASE_NOTES_BYTES:
        raise RuntimeError('Release notes exceed the safe byte limit.')
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result: raise RuntimeError('Release notes contain a duplicate field.')
            result[key] = value
        return result
    try:
        value = json.loads(content.decode('utf-8'), object_pairs_hook=unique)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise RuntimeError('Release notes are not valid bounded JSON.') from error
    if (not isinstance(value, dict) or set(value) != {'schemaVersion', 'version', 'items'}
            or type(value['schemaVersion']) is not int or value['schemaVersion'] != 1
            or not isinstance(value['version'], str) or value['version'] != version
            or not re.fullmatch(r'\d{1,6}\.\d{1,6}\.\d{1,6}', version)
            or not isinstance(value['items'], list) or not 1 <= len(value['items']) <= 20):
        raise RuntimeError('Release notes have the wrong fields or version.')
    seen = set()
    ascii_lower = str.maketrans('ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'abcdefghijklmnopqrstuvwxyz')
    for item in value['items']:
        if not isinstance(item, str): raise RuntimeError('A release note must be plain text.')
        try: size = len(item.encode('utf-8'))
        except UnicodeError as error: raise RuntimeError('A release note contains invalid text.') from error
        folded = item.translate(ascii_lower)
        if (not 1 <= size <= 400 or item.startswith(' ') or item.endswith(' ')
                or any(part in folded for part in ('://', 'www.', 'http:', 'https:', 'mailto:'))
                or any(ord(char) < 32 or 127 <= ord(char) <= 159 or char in '<>'
                       or 0x200b <= ord(char) <= 0x200f or 0x2028 <= ord(char) <= 0x202e
                       or 0x2060 <= ord(char) <= 0x206f or ord(char) == 0xfeff for char in item)
                or folded in seen):
            raise RuntimeError('Release notes contain markup, links, controls, repeated or oversized text.')
        seen.add(folded)
    return value


def release_notes_bytes():
    """Use the explicit versioned source record; never scrape UI text or a user's state."""
    source = Path(__file__).resolve().parents[1] / 'config' / 'release-notes.json'
    value = release_notes(read_plain(source, MAX_RELEASE_NOTES_BYTES), app_version())
    return (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')


def combined_licenses(notices, intro, first=()):
    """One text file with every licence text and notice. Identical texts appear once, under the list of every
    file they came from; the names in `first` lead."""
    groups = {}
    for name, content in notices.items():
        groups.setdefault(hashlib.sha256(content).hexdigest(), (content, []))[1].append(name)
    first = [name for name in first if name in notices]
    order = sorted(groups.values(), key=lambda group: (
        min((first.index(n) for n in group[1] if n in first), default=len(first)), sorted(group[1])[0]))
    rule = '=' * 78
    parts = ['LICENCES AND COPYRIGHT NOTICES', rule, *intro, '']
    for content, names in order:
        parts += [rule, *sorted(names), rule,
                  content.decode('utf-8', 'replace').replace('\r\n', '\n').rstrip('\n'), '']
    return ('\n'.join(parts) + '\n').encode('utf-8')
# Linux ships as ONE universal AppImage (package_portable_linux.py); there are
# deliberately no distribution-specific folder, .deb or .rpm layouts.
REVIEW_OUTER_FILES={
    'Drifters-Toolkit-NexusReview-Windows.zip','Drifters-Toolkit-NexusReview-Linux.zip',
    'Drifters-Toolkit-Review-Source.zip',
}


def app_version():
    """The application version from src/version.hpp (the single source of truth)."""
    import re
    text=(Path(__file__).resolve().parents[1]/'src'/'version.hpp').read_text(encoding='utf-8')
    match=re.search(r'#define DRIFTER_VERSION "(\d+\.\d+\.\d+)"',text)
    if not match:raise RuntimeError('src/version.hpp does not define DRIFTER_VERSION.')
    return match.group(1)


def zip_manifest(contents):
    """SHA256SUMS.json bytes for these package contents."""
    if 'SHA256SUMS.json' in contents:raise RuntimeError('The checksum manifest is generated by the packager.')
    checks={}
    for name,content in contents.items():
        canonical_name(name)
        if not isinstance(content,bytes):raise RuntimeError('Package data must be bytes.')
        checks[name]=hashlib.sha256(content).hexdigest()
    return (json.dumps(checks,indent=2)+'\n').encode()


def zip_bytes(root,contents,executables=(),extra=None):
    """Deterministic ZIP with SHA256SUMS.json. `extra` files (the release signature) are
    added after the manifest and are deliberately not listed in it."""
    canonical_name(root)
    rows={**contents,'SHA256SUMS.json':zip_manifest(contents)}
    for name,content in (extra or {}).items():
        canonical_name(name)
        if name in rows or not isinstance(content,bytes):raise RuntimeError('Invalid extra package file.')
        rows[name]=content
    output=io.BytesIO()
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,content in rows.items():
            info=zipfile.ZipInfo(root+'/'+name,date_time=(2026,1,1,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            info.create_system=3  # external_attr below always contains UNIX file modes, including on Windows.
            info.external_attr=(0o100755 if name in executables else 0o100644)<<16
            archive.writestr(info,content)
    return output.getvalue()


def write_artifact(path,content,*,replace=True,maximum=256*1024*1024):
    path=Path(path).absolute();path.parent.mkdir(parents=True,exist_ok=True)
    for parent in (path.parent,*path.parent.parents):
        if parent.is_symlink() or getattr(parent.lstat(),'st_file_attributes',0)&0x400:raise RuntimeError('A package output folder is redirected.')
    if path.exists() or path.is_symlink():
        if not replace:raise RuntimeError('An existing release artifact is preserved: '+path.name)
        read_plain(path,maximum)
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(prefix='drifter-package-',dir=path.parent,delete=False) as output:
            temporary=Path(output.name);output.write(content);output.flush();os.fsync(output.fileno())
        if replace:
            os.replace(temporary,path);temporary=None
        else:
            # Publish without overwriting a path that appeared after preflight.
            os.link(temporary,path)
            temporary.unlink();temporary=None
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)
    return path


def write_zip(path,root,contents,executables=()):
    path=Path(path)
    for target in (path,Path(str(path)+'.sha256')):
        if target.exists() or target.is_symlink():raise RuntimeError('An existing release artifact is preserved: '+target.name)
    content=zip_bytes(root,contents,executables)
    path=write_artifact(path,content,replace=False)
    write_artifact(str(path)+'.sha256',(hashlib.sha256(content).hexdigest()+'  '+path.name+'\n').encode(),replace=False)
    return path


def canonical_name(name):
    if not isinstance(name, str) or not name or len(name) > 1024:
        raise RuntimeError('Invalid release path.')
    if (any(ord(char) < 32 or ord(char) == 127 for char in name)
            or '\\' in name or ':' in name or name.startswith('/')
            or PurePosixPath(name).as_posix() != name
            or any(part in ('.', '..') or part.endswith((' ', '.')) for part in name.split('/'))):
        raise RuntimeError('Noncanonical or unsafe release path: '+repr(name))
    # Windows treats these names as devices even when an extension is present.
    reserved = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)), *(f'LPT{i}' for i in range(1, 10))}
    if any(part.split('.')[0].upper() in reserved for part in name.split('/')):
        raise RuntimeError('A release path uses a reserved device name.')
    return name


def read_plain(path, maximum=256*1024*1024):
    """Reject redirected/hardlinked inputs and detect replacement during reading."""
    path = Path(path).absolute()
    for parent in (path, *path.parents):
        info = parent.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise RuntimeError('A release input path is redirected: '+path.name)
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or not 0 <= before.st_size <= maximum:
        raise RuntimeError('A release input is not a bounded independent file: '+path.name)
    descriptor = os.open(path, os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0)
                         | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOCTTY', 0))
    with os.fdopen(descriptor, 'rb') as source:
        opened = os.fstat(source.fileno())
        identity = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_nlink)
        if identity(opened) != identity(before):
            raise RuntimeError('A release input changed before reading: '+path.name)
        content = source.read(maximum+1)
        after = os.fstat(source.fileno())
    final = path.lstat()
    if (len(content) != before.st_size or identity(before) != identity(after)
            or identity(before) != identity(final) or not stat.S_ISREG(final.st_mode)):
        raise RuntimeError('A release input changed during reading: '+path.name)
    return content
