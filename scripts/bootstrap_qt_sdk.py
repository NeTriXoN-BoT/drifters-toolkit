"""Fetch and extract the pinned native Linux Qt SDK into a new explicit folder.

Developer build tool only. It installs no system packages and does not run Qt or
Python bindings. Requires Python 3.9+ and a maintained 7z executable on Linux.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request


LOCK = Path(__file__).with_name('qt_sdk_lock.json')
MAX_ARCHIVE = 512 * 1024 * 1024


def https_url(value, original=False):
    try:
        url = urllib.parse.urlsplit(value)
        valid = (url.scheme == 'https' and url.hostname and url.port in (None, 443)
                 and not url.username and not url.password and not url.fragment
                 and all(32 < ord(c) < 127 for c in value))
        if original:
            valid = valid and url.hostname == 'download.qt.io' and not url.query
    except ValueError:
        valid = False
    if not valid:
        raise RuntimeError('Unexpected Qt SDK download URL.')
    return value


class HttpsRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        # Qt selects public mirrors. Redirects carry no authentication and every
        # final byte is checked against the committed archive digest and size.
        https_url(newurl)
        return super().redirect_request(request, fp, code, message, headers, newurl)


def plain_parents(path):
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.exists() and not parent.is_dir():
            raise RuntimeError('The SDK destination contains a redirected path.')


def digest_file(path, expected_size):
    with os.fdopen(os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
                          | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOCTTY', 0)), 'rb') as source:
        identity = os.fstat(source.fileno())
        if not stat.S_ISREG(identity.st_mode) or identity.st_nlink != 1 or identity.st_size != expected_size:
            raise RuntimeError('The SDK archive is not a plain file of the expected size.')
        digest = hashlib.sha256()
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
        if any(getattr(os.fstat(source.fileno()), field) != getattr(identity, field)
               for field in ('st_dev', 'st_ino', 'st_mode', 'st_nlink', 'st_size', 'st_mtime_ns', 'st_ctime_ns')):
            raise RuntimeError('The SDK archive changed while it was being checked.')
        return digest.hexdigest()


def fetch(row, cache, offline):
    target = cache / row['name']
    if target.exists() or target.is_symlink():
        if target.is_symlink() or digest_file(target, row['size']) != row['sha256']:
            raise RuntimeError('An existing SDK archive differs and was preserved: ' + target.name)
        return target
    if offline:
        raise RuntimeError('The offline SDK archive is missing: ' + target.name)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.qt-download-', dir=cache, delete=False) as output:
            temporary = Path(output.name)
            digest = hashlib.sha256()
            count = 0
            request = urllib.request.Request(row['url'], headers={'User-Agent': 'DriftersToolkit-Build/0.1'})
            with urllib.request.build_opener(HttpsRedirects()).open(request, timeout=120) as response:
                while chunk := response.read(1024 * 1024):
                    count += len(chunk)
                    if count > row['size']:
                        raise RuntimeError('The Qt SDK response exceeds its pinned size.')
                    digest.update(chunk)
                    output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        if count != row['size'] or digest.hexdigest() != row['sha256']:
            raise RuntimeError('The Qt SDK archive checksum or size did not match.')
        os.link(temporary, target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return target


def relative_member(name):
    path = PurePosixPath(name)
    if (not name or '\\' in name or '\x00' in name or path.is_absolute()
            or any(part in ('', '.', '..') for part in name.split('/'))
            or ':' in path.parts[0]):
        raise RuntimeError('Unexpected Qt SDK archive member path.')
    return path


def archive_members(tool, archive):
    listed = subprocess.run([tool, 'l', '-slt', '-ba', str(archive)],
                            check=True, capture_output=True, text=True)
    members = {}
    for block in re.split(r'\n\s*\n', listed.stdout):
        fields = dict(line.split(' = ', 1) for line in block.splitlines() if ' = ' in line)
        if 'Path' not in fields:
            continue
        name = fields['Path']
        relative_member(name)
        if name in members or len(members) >= 100000:
            raise RuntimeError('The Qt SDK archive contains duplicate or excessive members.')
        if fields.get('Encrypted') == '+':
            raise RuntimeError('Encrypted Qt SDK archive members are unsupported.')
        members[name] = fields
    if not members:
        raise RuntimeError('The Qt SDK archive has no file list.')
    return members


def repair_internal_alias(root, name, target, members, tool=None, archive=None):
    # 7z 26 refuses a few upstream .so aliases that point through another alias.
    # Preserve that protection for all other paths; restore only an archive-listed
    # internal alias whose final target is already a real file inside this SDK.
    if name not in members or not re.search(r'(?:^| )l[rwx-]{9}$', members[name].get('Attributes', '')):
        raise RuntimeError('The ignored Qt SDK entry is not an archive-listed symbolic link.')
    recorded = subprocess.run([tool, 'x', '-so', '-spd', str(archive), name],
                              check=True, capture_output=True).stdout
    if len(recorded) > 4096 or recorded.decode('utf-8', errors='strict') != target:
        raise RuntimeError('The ignored Qt SDK link differs from its archive payload.')
    relative_member(name)
    link = root / name
    parent = link.parent
    if parent.resolve() != parent or not parent.is_dir():
        raise RuntimeError('The ignored SDK alias has a redirected parent.')
    resolved = (parent / target).resolve(strict=True)
    if not resolved.is_relative_to(root) or not resolved.is_file():
        raise RuntimeError('The ignored SDK alias points outside the SDK.')
    if link.exists() or link.is_symlink():
        item = link.lstat()
        if not stat.S_ISREG(item.st_mode) or item.st_size != 0 or item.st_nlink != 1:
            raise RuntimeError('An unexpected SDK alias destination was preserved.')
        link.unlink()
    link.symlink_to(target)


def extract(tool, archive, destination):
    members = archive_members(tool, archive)
    result = subprocess.run([tool, 'x', '-y', '-o' + str(destination), str(archive)],
                            capture_output=True, text=True)
    ignored = []
    for line in result.stderr.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r'ERROR: Dangerous link via another link was ignored : (.+) : (.+)', line)
        if not match:
            raise RuntimeError('Qt SDK extraction failed: ' + line[:200])
        ignored.append(match.groups())
    if result.returncode and not ignored:
        raise RuntimeError('Qt SDK extraction failed with code ' + str(result.returncode))
    for name, target in ignored:
        repair_internal_alias(destination, name, target, members, tool, archive)
    return len(ignored)


def load_lock(path):
    data = json.loads(path.read_text(encoding='utf-8'))
    if data.get('schemaVersion') != 1 or data.get('qtVersion') != '6.11.3':
        raise RuntimeError('Unsupported Qt SDK lock version.')
    rows = data.get('archives', [])
    if not 1 <= len(rows) <= 32:
        raise RuntimeError('Invalid Qt SDK archive count.')
    names = set()
    for row in rows:
        name = row.get('name', '')
        if (not re.fullmatch(r'[A-Za-z0-9_.-]+\.7z', name) or name in names
                or not re.fullmatch('[a-f0-9]{64}', row.get('sha256', ''))
                or type(row.get('size')) is not int or not 0 < row['size'] <= MAX_ARCHIVE
                or row.get('extractSubdir', '.') not in ('.', 'lib')):
            raise RuntimeError('Invalid Qt SDK archive lock entry.')
        https_url(row['url'], original=True)
        if urllib.parse.urlsplit(row['url']).path.rsplit('/', 1)[-1] != name:
            raise RuntimeError('The Qt SDK URL and archive name differ.')
        names.add(name)
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True, type=Path, help='New SDK folder; existing folders are never overwritten.')
    parser.add_argument('--archives', required=True, type=Path, help='Archive cache folder; hashes are checked on every use.')
    parser.add_argument('--lock', default=LOCK, type=Path)
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'linux':
        raise RuntimeError('This pinned native SDK recipe requires Linux.')
    tool = shutil.which('7z') or shutil.which('7zz')
    if not tool:
        raise RuntimeError('Install a maintained 7z build before extracting this SDK.')
    lock = load_lock(args.lock)
    prefix, cache = args.prefix.absolute(), args.archives.absolute()
    plain_parents(prefix.parent)
    plain_parents(cache)
    cache.mkdir(parents=True, exist_ok=True)
    if prefix.exists() or prefix.is_symlink():
        raise RuntimeError('The SDK destination already exists and was preserved.')
    archives = [fetch(row, cache, args.offline) for row in lock['archives']]
    prefix.mkdir(parents=True, mode=0o700)
    repairs = 0
    for row, archive in zip(lock['archives'], archives):
        destination = prefix / row.get('extractSubdir', '.')
        destination.mkdir(exist_ok=True)
        repairs += extract(tool, archive, destination)
        print('Verified and extracted', row['name'], flush=True)
    for current, directories, files in os.walk(prefix, followlinks=False):
        for name in directories + files:
            item = Path(current) / name
            if item.is_symlink() and not item.resolve(strict=True).is_relative_to(prefix):
                raise RuntimeError('An SDK symbolic link leaves the SDK prefix.')
    print('Native Qt', lock['qtVersion'], 'SDK ready:', prefix, '(' + str(repairs) + ' checked internal aliases)')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print('Qt SDK setup failed:', str(error), file=sys.stderr)
        raise SystemExit(1)
