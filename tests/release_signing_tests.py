"""Bounded release inspection and publication fixtures, with throwaway keys only."""
from pathlib import Path
import hashlib
import io
import json
import os
import stat
import struct
import sys
import tempfile
import unittest
import warnings
import zipfile
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import release_signing as signing
import release_layout as layout
import seal_portable_linux as seal


class DosDefaultZipInfo(zipfile.ZipInfo):
    """Reproduce the Windows constructor default on either test host."""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.create_system = 0


class SigningSecurity(unittest.TestCase):
    def setUp(self):
        self.owner = tempfile.TemporaryDirectory(prefix='drifter-signing-fixture-')
        self.root = Path(self.owner.name)

    def tearDown(self):
        self.owner.cleanup()

    def archive(self, rows=None, mode=None, compression=zipfile.ZIP_DEFLATED):
        target = self.root / 'fixture.zip'
        with warnings.catch_warnings(), zipfile.ZipFile(target, 'w', compression=compression) as archive:
            warnings.simplefilter('ignore', UserWarning)
            for name, body in rows or [('DriftersToolkit/program.exe', b'program')]:
                info = zipfile.ZipInfo(name)
                info.compress_type = compression
                info.external_attr = (mode if mode is not None else 0o100644) << 16
                archive.writestr(info, body)
        return target

    def unsigned(self):
        target = self.root / 'release.zip'
        target.write_bytes(layout.zip_bytes('DriftersToolkit', {'program.exe': b'program'}, {'program.exe'}))
        return target

    def test_valid_release_read_preserves_exact_file_bytes_and_modes(self):
        target = self.unsigned()
        platform, root, files, modes = signing.read_release(target)
        self.assertEqual((platform, root), ('windows', 'DriftersToolkit'))
        self.assertEqual(files['program.exe'], b'program')
        self.assertEqual(modes, {'program.exe'})
        self.assertIsNone(signing.verify_contents(platform, files, require_signature=False))

    def test_linux_zip_declares_unix_permissions_with_windows_writer_defaults(self):
        image = 'Drifters-Toolkit-Linux-x86_64.AppImage'
        launcher = 'Launch-DriftersToolkit.sh'
        contents = {image: b'AppImage fixture', launcher: b'#!/bin/sh\nexit 0\n',
                    'LICENSES.txt': b'Licence fixture', 'LINUX_REQUIREMENTS.json': b'{}\n'}
        with patch.object(zipfile, 'ZipInfo', DosDefaultZipInfo):
            payload = layout.zip_bytes('DriftersToolkit-Linux', contents, {image, launcher})
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:
            for info in archive.infolist():
                name = info.filename.split('/', 1)[1]
                self.assertEqual(info.create_system, 3, info.filename)
                self.assertEqual(info.external_attr >> 16,
                                 0o100755 if name in (image, launcher) else 0o100644, info.filename)
                if name in contents:
                    self.assertEqual(archive.read(info), contents[name])

    def test_windows_and_source_zip_files_keep_explicit_data_permissions(self):
        contents = {'program.exe': b'PE fixture', 'scripts/build.sh': b'#!/bin/sh\n', 'data.json': b'{}\n'}
        for root in ('DriftersToolkit', 'DriftersToolkit-review-source'):
            with self.subTest(root=root), patch.object(zipfile, 'ZipInfo', DosDefaultZipInfo):
                payload = layout.zip_bytes(root, contents)
            with zipfile.ZipFile(io.BytesIO(payload)) as archive:
                for info in archive.infolist():
                    self.assertEqual(info.create_system, 3, info.filename)
                    self.assertEqual(info.external_attr >> 16, 0o100644, info.filename)

    def test_linux_signing_keeps_unix_permissions_with_windows_writer_defaults(self):
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        key = ec.generate_private_key(ec.SECP256R1())
        private = self.root / 'linux-fixture-test-key.pem'
        private.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                             serialization.NoEncryption()))
        numbers = key.public_key().public_numbers()
        x, y = numbers.x.to_bytes(32, 'big'), numbers.y.to_bytes(32, 'big')
        public = self.root / 'linux-fixture-public.json'
        public.write_text(json.dumps({'curve': 'P-256', 'x': x.hex(), 'y': y.hex(),
                                      'keyId': signing.key_id(x, y)}))
        image, launcher = 'Drifters-Toolkit-Linux-x86_64.AppImage', 'Launch-DriftersToolkit.sh'
        contents = {image: b'AppImage fixture', launcher: b'#!/bin/sh\nexit 0\n', 'LICENSES.txt': b'Licence fixture'}
        target = self.root / 'linux-release.zip'
        target.write_bytes(layout.zip_bytes('DriftersToolkit-Linux', contents, {image, launcher}))
        unsigned_sums = layout.zip_manifest(contents)
        with patch.object(zipfile, 'ZipInfo', DosDefaultZipInfo), \
                patch.object(signing, 'PRIVATE_KEY', private), patch.object(signing, 'PUBLIC_KEY', public):
            signing.sign(target, 'linux', '1.2.0')
            platform, root, files, modes = signing.read_release(target)
            self.assertEqual((platform, root), ('linux', 'DriftersToolkit-Linux'))
            self.assertEqual(signing.verify_contents(platform, files), '1.2.0')
        self.assertEqual(modes, {image, launcher})
        self.assertEqual(files['SHA256SUMS.json'], unsigned_sums)
        self.assertEqual({name: body for name, body in files.items()
                          if name not in ('SHA256SUMS.json', signing.SIGNATURE_NAME)}, contents)
        with zipfile.ZipFile(target) as archive:
            for info in archive.infolist():
                name = info.filename.split('/', 1)[1]
                self.assertEqual(info.create_system, 3, info.filename)
                self.assertEqual(info.external_attr >> 16,
                                 0o100755 if name in modes else 0o100644, info.filename)
        self.assertEqual(Path(str(target) + '.sha256').read_text(),
                         hashlib.sha256(target.read_bytes()).hexdigest() + '  ' + target.name + '\n')

    def test_compressed_member_and_total_limits_before_decompression(self):
        target = self.archive([('DriftersToolkit/a', b'a' * 50), ('DriftersToolkit/b', b'b' * 50)])
        with patch.object(signing, 'MAX_FILE', 49), patch.object(zipfile.ZipFile, 'read', side_effect=AssertionError('not reached')):
            with self.assertRaises(RuntimeError): signing.read_release(target)
        with patch.object(signing, 'MAX_TOTAL', 99), patch.object(zipfile.ZipFile, 'read', side_effect=AssertionError('not reached')):
            with self.assertRaises(RuntimeError): signing.read_release(target)
        with patch.object(signing, 'MAX_ZIP', 1):
            with self.assertRaises(RuntimeError): signing.read_release(target)
        target = self.archive([('DriftersToolkit/RELEASE-SIGNATURE.json', b'x' * 5)])
        with patch.object(signing, 'MAX_SIGNATURE', 4), patch.object(zipfile.ZipFile, 'read', side_effect=AssertionError('not reached')):
            with self.assertRaises(RuntimeError): signing.read_release(target)

    def test_directory_count_is_checked_before_zipfile_allocation(self):
        target = self.archive([('DriftersToolkit/a', b'a'), ('DriftersToolkit/b', b'b')])
        with patch.object(signing, 'MAX_ENTRIES', 1), patch.object(signing.zipfile, 'ZipFile', side_effect=AssertionError('not reached')):
            with self.assertRaises(RuntimeError): signing.read_release(target)
        data = bytearray(target.read_bytes())
        end = data.rfind(b'PK\x05\x06')
        struct.pack_into('<HH', data, end + 8, 1, 1)
        target.write_bytes(data)
        with patch.object(signing.zipfile, 'ZipFile', side_effect=AssertionError('not reached')):
            with self.assertRaises(RuntimeError): signing.read_release(target)

    def test_duplicate_case_collision_and_multiple_platform_roots(self):
        for rows in ([('DriftersToolkit/a', b'a'), ('DriftersToolkit/a', b'b')],
                     [('DriftersToolkit/a', b'a'), ('DriftersToolkit/A', b'b')],
                     [('DriftersToolkit/a', b'a'), ('DriftersToolkit-Linux/a', b'b')]):
            with self.subTest(rows=rows), self.assertRaises(RuntimeError): signing.read_release(self.archive(rows))

    def test_unsafe_paths_links_and_privileged_members(self):
        for name in ('../outside', 'ui/../outside', 'CON.txt', 'a:b', 'a*', '.update-work/file',
                     'ui/app.js.previous', '/absolute', 'a/' * 9 + 'b'):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                signing.read_release(self.archive([('DriftersToolkit/' + name, b'x')]))
        for mode in (stat.S_IFLNK | 0o777, stat.S_IFIFO | 0o644, stat.S_IFREG | 0o4755, stat.S_IFREG | 0o666):
            with self.subTest(mode=mode), self.assertRaises(RuntimeError): signing.read_release(self.archive(mode=mode))

    def test_encryption_unknown_compression_and_embedded_nul_names(self):
        target = self.archive()
        data = bytearray(target.read_bytes())
        central = data.find(b'PK\x01\x02')
        struct.pack_into('<H', data, central + 8, 1)
        target.write_bytes(data)
        with self.assertRaises(RuntimeError): signing.read_release(target)
        target = self.archive(compression=zipfile.ZIP_BZIP2)
        with self.assertRaises(RuntimeError): signing.read_release(target)
        for raw_name in (b'DriftersToolkit/a\x00c', b'DriftersToolkit/a\\b'):
            target = self.archive([('DriftersToolkit/abc', b'x')])
            target.write_bytes(target.read_bytes().replace(b'DriftersToolkit/abc', raw_name))
            with self.assertRaises(RuntimeError): signing.read_release(target)

    def test_malformed_trailer_and_redirected_inputs_preserved(self):
        target = self.archive()
        target.write_bytes(target.read_bytes() + b'unexpected suffix')
        with self.assertRaises(RuntimeError): signing.read_release(target)
        target = self.archive()
        alias = self.root / 'hardlink.zip'
        os.link(target, alias)
        before = target.read_bytes()
        with self.assertRaises(RuntimeError): signing.read_release(target)
        self.assertEqual(alias.read_bytes(), before)

    def test_json_duplicates_types_nesting_and_versions_are_bounded(self):
        for document in (b'{"x":1,"x":1}', b'[]', b'{"x":NaN}', b'[' * 1500):
            with self.subTest(document=document[:20]), self.assertRaises(RuntimeError):
                signing.json_object(document, 4096, 'fixture')
        with self.assertRaises(RuntimeError): signing.json_object(b'{}', 1, 'fixture')
        for version in ('1.2', '1.2.3.4', '1234567.0.0', '1.2.3\n', 'a.b.c'):
            with self.subTest(version=version), self.assertRaises(RuntimeError): signing.message('windows', version, b'{}')
        for sums in (b'{"program.exe":"bad"}', b'{"program.exe":1}', b'{"program.exe":"x","program.exe":"x"}'):
            with self.assertRaises(RuntimeError): signing.verify_contents('windows', {'program.exe': b'program', 'SHA256SUMS.json': sums}, False)

    def test_unknown_sidecar_refused_before_any_private_key_read(self):
        target = self.unsigned()
        sidecar = Path(str(target) + '.sha256')
        sidecar.write_bytes(b'personal checksum data')
        before = target.read_bytes()
        secret = Mock()
        with patch.object(signing, 'PRIVATE_KEY', secret), self.assertRaises(RuntimeError): signing.sign(target, 'windows', '1.2.0')
        secret.read_bytes.assert_not_called()
        self.assertEqual(target.read_bytes(), before)
        self.assertEqual(sidecar.read_bytes(), b'personal checksum data')

    def test_throwaway_key_signing_and_reverification_only(self):
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        key = ec.generate_private_key(ec.SECP256R1())
        private = self.root / 'fixture-test-key.pem'
        private.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        public = self.root / 'fixture-public.json'
        numbers = key.public_key().public_numbers()
        x, y = numbers.x.to_bytes(32, 'big'), numbers.y.to_bytes(32, 'big')
        public.write_text(json.dumps({'curve': 'P-256', 'x': x.hex(), 'y': y.hex(), 'keyId': signing.key_id(x, y)}))
        target = self.unsigned()
        Path(str(target) + '.sha256').write_bytes((hashlib.sha256(target.read_bytes()).hexdigest() + '  ' + target.name + '\n').encode())
        with patch.object(signing, 'PRIVATE_KEY', private), patch.object(signing, 'PUBLIC_KEY', public):
            signing.sign(target, 'windows', '1.2.0')
            platform, _, files, _ = signing.read_release(target)
            self.assertEqual(signing.verify_contents(platform, files), '1.2.0')
            before = target.read_bytes()
            with self.assertRaises(RuntimeError): signing.sign(target, 'windows', '1.3.0')
            self.assertEqual(target.read_bytes(), before)
        self.assertEqual(Path(str(target) + '.sha256').read_text(), hashlib.sha256(target.read_bytes()).hexdigest() + '  ' + target.name + '\n')

    def test_sealer_preserves_every_existing_output_before_build(self):
        stem = 'Drifters-Toolkit-Linux'
        image = stem + '-x86_64.AppImage'
        names = (image, stem + '.zip', image + '.sha256', stem + '.zip.sha256', 'APPIMAGE_MANIFEST.json')
        for name in names:
            with self.subTest(name=name):
                target = self.root / name
                target.write_bytes(b'unrelated existing output')
                with self.assertRaises(RuntimeError): seal.output_paths(self.root, stem, image)
                self.assertEqual(target.read_bytes(), b'unrelated existing output')
                target.unlink()
        self.assertEqual(seal.output_paths(self.root, stem, image), (self.root / image, self.root / (stem + '.zip')))


if __name__ == '__main__':
    unittest.main(verbosity=2)
