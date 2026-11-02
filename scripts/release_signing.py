"""Sign and verify Drifter's Toolkit release ZIPs for the in-app updater.

A release ZIP already contains SHA256SUMS.json with the SHA-256 of every file.
RELEASE-SIGNATURE.json signs that manifest together with the platform and the
version, using an ECDSA P-256 key that only the author holds. The application
embeds the public key (src/release_key.hpp) and installs an update only when
the signature and every listed file hash match.

  python scripts/release_signing.py keygen            # once; never overwrites a key
  python scripts/release_signing.py sign <zip> --platform windows|linux
  python scripts/release_signing.py verify <zip>

The private key lives in private-artifacts/release-signing/ (excluded from Git
and from every package). Keep a backup: without it, later releases cannot be
installed by the in-app updater of existing copies.
"""
from pathlib import Path
import argparse
import hashlib
import io
import json
import re
import stat
import struct
import sys
import zipfile
from release_layout import canonical_name, read_plain

ROOT = Path(__file__).resolve().parents[1]
KEY_DIR = ROOT / 'private-artifacts' / 'release-signing'
PRIVATE_KEY = KEY_DIR / 'drifters-toolkit-release-key.pem'
PUBLIC_KEY = ROOT / 'scripts' / 'release_key.json'
PUBLIC_HEADER = ROOT / 'src' / 'release_key.hpp'
SIGNATURE_NAME = 'RELEASE-SIGNATURE.json'
ROOTS = {'windows': 'DriftersToolkit', 'linux': 'DriftersToolkit-Linux'}
MAX_ZIP = 1024 * 1024 * 1024
MAX_FILE = 768 * 1024 * 1024
MAX_TOTAL = 1536 * 1024 * 1024
MAX_ENTRIES = 20000
MAX_SUMS = 4 * 1024 * 1024
MAX_SIGNATURE = 64 * 1024
VERSION = re.compile(r'[0-9]{1,6}\.[0-9]{1,6}\.[0-9]{1,6}\Z')


def relative_name(name):
    canonical_name(name)
    if (len(name.encode('utf-8')) > 512 or len(name.split('/')) > 8
            or any(len(part.encode('utf-8')) > 255 or any(char in '*?"<>|' for char in part) for part in name.split('/'))
            or name.lower().endswith('.previous') or name.lower().startswith('.update')):
        raise RuntimeError('An unsafe or updater-reserved release path was included.')
    return name


def json_object(content, maximum, label):
    if not isinstance(content, bytes) or len(content) > maximum:
        raise RuntimeError(label + ' exceeds its safe size limit.')
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result: raise RuntimeError(label + ' contains a duplicate field.')
            result[key] = value
        return result
    try:
        value = json.loads(content, object_pairs_hook=unique,
                           parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Non-finite JSON')))
    except (ValueError, UnicodeError, RecursionError) as issue:
        raise RuntimeError(label + ' is not valid bounded JSON.') from issue
    if not isinstance(value, dict): raise RuntimeError(label + ' must be a JSON object.')
    return value


def app_version():
    text = (ROOT / 'src' / 'version.hpp').read_text(encoding='utf-8')
    match = re.search(r'#define DRIFTER_VERSION "(\d+\.\d+\.\d+)"', text)
    if not match: raise RuntimeError('src/version.hpp does not define DRIFTER_VERSION.')
    return match.group(1)


def message(platform, version, sums_bytes):
    """The exact bytes that are signed; src/self_update.cpp builds the same text."""
    if platform not in ROOTS or not isinstance(version, str) or not VERSION.fullmatch(version): raise RuntimeError('Invalid release identity.')
    return ('drifters-toolkit release v1\nplatform=' + platform + '\nversion=' + version + '\nsums=' +
            hashlib.sha256(sums_bytes).hexdigest() + '\n').encode()


def public_key():
    data = json.loads(PUBLIC_KEY.read_text(encoding='utf-8'))
    x, y = bytes.fromhex(data['x']), bytes.fromhex(data['y'])
    if data.get('curve') != 'P-256' or len(x) != 32 or len(y) != 32: raise RuntimeError('Invalid release public key.')
    return x, y, data['keyId']


def key_id(x, y):
    return hashlib.sha256(b'\x04' + x + y).hexdigest()[:16]


def keygen():
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    if PRIVATE_KEY.exists() or PUBLIC_KEY.exists() or PUBLIC_HEADER.exists():
        raise RuntimeError('A release key already exists. It is never overwritten.')
    KEY_DIR.mkdir(parents=True, exist_ok=True)
    key = ec.generate_private_key(ec.SECP256R1())
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption())
    with open(PRIVATE_KEY, 'xb') as output: output.write(pem)
    numbers = key.public_key().public_numbers()
    x, y = numbers.x.to_bytes(32, 'big'), numbers.y.to_bytes(32, 'big')
    identity = key_id(x, y)
    PUBLIC_KEY.write_text(json.dumps({'curve': 'P-256', 'keyId': identity, 'x': x.hex(), 'y': y.hex()}, indent=2) + '\n',
                          encoding='utf-8', newline='\n')
    as_c = lambda raw: ', '.join('0x%02x' % b for b in raw)
    PUBLIC_HEADER.write_text(
        '#pragma once\n#include <array>\n#include <cstdint>\n'
        '// Public half of the author\'s release-signing key (scripts/release_signing.py).\n'
        '// Only releases signed with the matching private key can be installed by the updater.\n'
        'namespace drifter::release_key {\n'
        'inline constexpr const char* KeyId = "' + identity + '";\n'
        'inline constexpr std::array<std::uint8_t, 32> X{' + as_c(x) + '};\n'
        'inline constexpr std::array<std::uint8_t, 32> Y{' + as_c(y) + '};\n'
        '} // namespace drifter::release_key\n', encoding='utf-8', newline='\n')
    (KEY_DIR / 'README.txt').write_text(
        'Drifter\'s Toolkit release-signing key. KEEP PRIVATE. Never share, upload or commit this folder.\n\n'
        'The in-app updater installs only ZIPs signed with this key. Back up drifters-toolkit-release-key.pem\n'
        '(for example on a USB stick or in a password manager). If it is lost, existing copies cannot update\n'
        'themselves to releases signed with a new key; people would download the next release manually once.\n\n'
        'Public key id: ' + identity + '\n', encoding='utf-8', newline='\n')
    print('Created the release key (id ' + identity + ').')
    print('Private key: ' + str(PRIVATE_KEY))
    print('Public key:  ' + str(PUBLIC_KEY) + ' and ' + str(PUBLIC_HEADER))


def read_release(path, *, content=None):
    path = Path(path)
    content = read_plain(path, MAX_ZIP) if content is None else content
    if not isinstance(content, bytes) or len(content) > MAX_ZIP: raise RuntimeError('The release ZIP is too large.')
    # Inspect the fixed-size trailer before ZipFile can allocate an unbounded
    # number of central-directory objects. Our limits never require ZIP64's
    # 65,535+ member/disk/offset sentinels; individual local ZIP64 headers remain
    # supported by ZipFile for otherwise ordinary small archives.
    end = content.rfind(b'PK\x05\x06', max(0, len(content) - 65557))
    if end < 0 or end + 22 > len(content): raise RuntimeError('The release ZIP has no valid directory trailer.')
    disk, directory_disk, on_disk, count, directory_bytes, directory_offset, comment = struct.unpack_from('<4H2IH', content, end + 4)
    if (disk or directory_disk or on_disk != count or not 1 <= count <= MAX_ENTRIES or end + 22 + comment != len(content)
            or directory_bytes > 32 * 1024 * 1024 or directory_offset + directory_bytes > end):
        raise RuntimeError('The release ZIP exceeds its directory limits or spans multiple disks.')
    position, observed = directory_offset, 0
    while position < directory_offset + directory_bytes:
        if position + 46 > directory_offset + directory_bytes or content[position:position+4] != b'PK\x01\x02':
            raise RuntimeError('The release ZIP central directory is malformed.')
        name_bytes, extra_bytes, comment_bytes = struct.unpack_from('<3H', content, position + 28)
        observed += 1
        if observed > MAX_ENTRIES or not 1 <= name_bytes <= 1024:
            raise RuntimeError('The release ZIP central directory exceeds its entry/name limits.')
        position += 46 + name_bytes + extra_bytes + comment_bytes
        if position > directory_offset + directory_bytes: raise RuntimeError('The release ZIP central directory is incomplete.')
    if observed != count: raise RuntimeError('The release ZIP directory count does not match.')
    with zipfile.ZipFile(io.BytesIO(content)) as archive:
        entries = archive.infolist()
        if len(entries) != count: raise RuntimeError('The release ZIP directory count does not match.')
        files, modes, seen = {}, set(), set()
        root = None
        total = 0
        approved = []
        for info in entries:
            name = info.orig_filename
            if name != info.filename: raise RuntimeError('The release ZIP contains an embedded NUL filename.')
            canonical_name(name[:-1] if info.is_dir() and name.endswith('/') else name)
            found, separator, relative = name.partition('/')
            if found not in ROOTS.values() or not separator or root is not None and root != found:
                raise RuntimeError('A release ZIP must have one known platform root folder.')
            root = found
            folded = name.casefold()
            if folded in seen: raise RuntimeError('The release ZIP contains duplicate or case-colliding entries.')
            seen.add(folded)
            mode = info.external_attr >> 16
            kind = stat.S_IFMT(mode)
            if (info.flag_bits & 1 or info.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)
                    or kind not in (0, stat.S_IFDIR if info.is_dir() else stat.S_IFREG) or mode & 0o7022):
                raise RuntimeError('The release ZIP contains encrypted, linked, special or privileged content.')
            if info.is_dir():
                if relative: relative_name(relative[:-1])
                continue
            relative_name(relative)
            if not 0 <= info.file_size <= MAX_FILE or total + info.file_size > MAX_TOTAL:
                raise RuntimeError('The release ZIP exceeds its per-file or total expansion limit.')
            metadata_limit = {'SHA256SUMS.json': MAX_SUMS, SIGNATURE_NAME: MAX_SIGNATURE, 'RELEASE-NOTES.json': 16 * 1024}.get(relative)
            if metadata_limit is not None and info.file_size > metadata_limit:
                raise RuntimeError('The release ZIP metadata exceeds its safe size limit.')
            total += info.file_size
            approved.append((info, relative, bool(mode & 0o111)))
        if not approved: raise RuntimeError('The release ZIP contains no files.')
        platform = next(key for key, value in ROOTS.items() if value == root)
        for info, relative, executable in approved:
            data = archive.read(info)
            if len(data) != info.file_size: raise RuntimeError('A release ZIP member is incomplete.')
            files[relative] = data
            if executable: modes.add(relative)
    return platform, root, files, modes


def verify_contents(platform, files, require_signature=True):
    sums_bytes = files.get('SHA256SUMS.json')
    if sums_bytes is None: raise RuntimeError('SHA256SUMS.json is missing.')
    if platform not in ROOTS or len(files) > MAX_ENTRIES: raise RuntimeError('Invalid release platform or file count.')
    sums = json_object(sums_bytes, MAX_SUMS, 'SHA256SUMS.json')
    listed = set(sums)
    present = set(files) - {'SHA256SUMS.json', SIGNATURE_NAME}
    if listed != present: raise RuntimeError('SHA256SUMS.json does not list exactly the packaged files.')
    for name, digest in sums.items():
        relative_name(name)
        if not isinstance(digest, str) or not re.fullmatch('[a-f0-9]{64}', digest): raise RuntimeError('A release checksum is malformed.')
        if hashlib.sha256(files[name]).hexdigest() != digest: raise RuntimeError('A packaged file does not match its checksum: ' + name)
    if SIGNATURE_NAME not in files:
        if require_signature: raise RuntimeError('The release is not signed.')
        return None
    signature = json_object(files[SIGNATURE_NAME], MAX_SIGNATURE, 'RELEASE-SIGNATURE.json')
    expected = {'schemaVersion', 'product', 'platform', 'version', 'sums', 'keyId', 'signature'}
    if set(signature) != expected or type(signature['schemaVersion']) is not int or signature['schemaVersion'] != 1 or signature['product'] != 'drifters-toolkit':
        raise RuntimeError('The release signature record is malformed.')
    if not isinstance(signature['version'], str) or not VERSION.fullmatch(signature['version']): raise RuntimeError('Invalid release version.')
    if not isinstance(signature['signature'], str) or not re.fullmatch('[a-f0-9]{128}', signature['signature']): raise RuntimeError('The release signature has the wrong format.')
    if signature['platform'] != platform: raise RuntimeError('The release was signed for another platform.')
    if 'RELEASE-NOTES.json' in files:
        from release_layout import release_notes
        release_notes(files['RELEASE-NOTES.json'], signature['version'])
    if signature['sums'] != hashlib.sha256(sums_bytes).hexdigest(): raise RuntimeError('The signature covers another checksum manifest.')
    x, y, identity = public_key()
    if signature['keyId'] != identity: raise RuntimeError('The release was signed with an unknown key.')
    raw = bytes.fromhex(signature['signature'])
    if len(raw) != 64: raise RuntimeError('The release signature has the wrong length.')
    # Only the cryptographic check needs the cryptography package; malformed records fail above without it.
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    key = ec.EllipticCurvePublicNumbers(int.from_bytes(x, 'big'), int.from_bytes(y, 'big'), ec.SECP256R1()).public_key()
    der = encode_dss_signature(int.from_bytes(raw[:32], 'big'), int.from_bytes(raw[32:], 'big'))
    key.verify(der, message(platform, signature['version'], sums_bytes), ec.ECDSA(hashes.SHA256()))
    return signature['version']


def sign(path, platform, version=None):
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
    import release_layout
    version = version or app_version()
    if not isinstance(version, str) or not VERSION.fullmatch(version): raise RuntimeError('Invalid release version.')
    path = Path(path)
    unsigned = read_plain(path, MAX_ZIP)
    unsigned_digest = hashlib.sha256(unsigned).hexdigest()
    sidecar = Path(str(path) + '.sha256')
    expected_sidecar = (unsigned_digest + '  ' + path.name + '\n').encode()
    if (sidecar.exists() or sidecar.is_symlink()) and read_plain(sidecar, 2048) != expected_sidecar:
        raise RuntimeError('An unrelated release checksum sidecar was preserved.')
    found, root, files, modes = read_release(path, content=unsigned)
    if found != platform: raise RuntimeError('This ZIP belongs to the ' + found + ' release.')
    if SIGNATURE_NAME in files: raise RuntimeError('This release is already signed.')
    verify_contents(platform, files, require_signature=False)
    if 'RELEASE-NOTES.json' in files:
        release_layout.release_notes(files['RELEASE-NOTES.json'], version)
    contents = {name: data for name, data in files.items() if name != 'SHA256SUMS.json'}
    # Rebuild the same deterministic ZIP; zip_bytes writes the identical SHA256SUMS.json.
    sums_bytes = release_layout.zip_manifest(contents)
    if sums_bytes != files['SHA256SUMS.json']: raise RuntimeError('The checksum manifest could not be reproduced.')
    key = serialization.load_pem_private_key(PRIVATE_KEY.read_bytes(), password=None)
    der = key.sign(message(platform, version, sums_bytes), ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der)
    x, y, identity = public_key()
    numbers = key.public_key().public_numbers()
    if (numbers.x.to_bytes(32, 'big'), numbers.y.to_bytes(32, 'big')) != (x, y):
        raise RuntimeError('The private key does not match scripts/release_key.json.')
    record = {'schemaVersion': 1, 'product': 'drifters-toolkit', 'platform': platform, 'version': version,
              'sums': hashlib.sha256(sums_bytes).hexdigest(), 'keyId': identity,
              'signature': (r.to_bytes(32, 'big') + s.to_bytes(32, 'big')).hex()}
    signed = release_layout.zip_bytes(root, contents, modes, extra={SIGNATURE_NAME: (json.dumps(record, indent=2) + '\n').encode()})
    # Refuse changed targets after the (explicit) signing operation too. Existing
    # unrelated sidecars must not make a valid unsigned input get replaced first.
    if hashlib.sha256(read_plain(path, MAX_ZIP)).hexdigest() != unsigned_digest:
        raise RuntimeError('The release ZIP changed before publication and was preserved.')
    if (sidecar.exists() or sidecar.is_symlink()) and read_plain(sidecar, 2048) != expected_sidecar:
        raise RuntimeError('The release checksum sidecar changed and was preserved.')
    release_layout.write_artifact(path, signed, maximum=MAX_ZIP)
    release_layout.write_artifact(sidecar, (hashlib.sha256(signed).hexdigest() + '  ' + path.name + '\n').encode())
    # Prove the written file verifies exactly as the application will check it.
    platform_check, _, check_files, _ = read_release(path)
    if verify_contents(platform_check, check_files) != version: raise RuntimeError('The signed release did not verify.')
    print('Signed ' + Path(path).name + ' as ' + platform + ' ' + version + ' (key ' + identity + ').')


def main():
    sys.path.insert(0, str(ROOT / 'scripts'))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=('keygen', 'sign', 'verify'))
    parser.add_argument('zip', nargs='?', type=Path)
    parser.add_argument('--platform', choices=sorted(ROOTS))
    parser.add_argument('--version')
    args = parser.parse_args()
    if args.mode == 'keygen':
        keygen()
    elif args.mode == 'sign':
        if not args.zip or not args.platform: parser.error('sign needs a ZIP and --platform')
        sign(args.zip, args.platform, args.version)
    else:
        if not args.zip: parser.error('verify needs a ZIP')
        platform, _, files, _ = read_release(args.zip)
        print('Verified ' + platform + ' release ' + str(verify_contents(platform, files)) + '.')


if __name__ == '__main__':
    main()
