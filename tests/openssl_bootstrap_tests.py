"""Offline fixtures for the pinned OpenSSL build and staging boundary."""
from pathlib import Path
import hashlib
import importlib.util
import io
import json
import multiprocessing
import os
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('bootstrap_openssl', ROOT / 'scripts/bootstrap_openssl.py')
openssl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(openssl)


class OpenSSLBootstrapTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='drifter-openssl-tests-')
        self.root = Path(self.temporary.name)
        self.archive = self.root / 'source.tar.gz'
        self.archive.write_bytes(b'fixture source')
        self.source = {**openssl.SOURCE, 'sha256': hashlib.sha256(self.archive.read_bytes()).hexdigest(),
                       'size': self.archive.stat().st_size}
        self.prefix = self.root / 'prefix'
        (self.prefix / 'lib').mkdir(parents=True)
        for name in openssl.RUNTIME_NAMES:
            (self.prefix / 'lib' / name).write_bytes(b'fixture ' + name.encode())
        (self.prefix / 'OPENSSL-LICENSE.txt').write_bytes(b'Apache License 2.0 fixture')
        self.proof = {'schemaVersion': 1, 'version': openssl.VERSION, 'origin': 'official-openssl-source',
                      'source': self.source, 'buildOptions': openssl.BUILD_OPTIONS,
                      'minimumCpu': 'x86-64-v2', 'minimumGlibc': '2.35',
                      'runtime': {name: openssl.identity(self.prefix / 'lib' / name) for name in openssl.RUNTIME_NAMES},
                      'license': openssl.identity(self.prefix / 'OPENSSL-LICENSE.txt')}
        self.proof_path = self.prefix / 'OPENSSL-PROVENANCE.json'
        self.save_proof()

    def tearDown(self):
        self.temporary.cleanup()

    def save_proof(self):
        self.proof_path.write_text(json.dumps(self.proof))

    def validate(self):
        with patch.object(openssl, 'SOURCE', self.source):
            return openssl.runtime_inputs(self.prefix, self.proof_path, self.archive)

    def test_exact_source_and_runtime_are_accepted(self):
        self.assertEqual(self.validate(), self.proof)

    def test_changed_source_is_refused(self):
        self.archive.write_bytes(b'changed source')
        with self.assertRaises(RuntimeError): self.validate()

    def test_changed_library_is_refused(self):
        (self.prefix / 'lib/libcrypto.so.3').write_bytes(b'changed crypto')
        with self.assertRaises(RuntimeError): self.validate()

    def test_missing_library_is_refused(self):
        (self.prefix / 'lib/libssl.so.3').unlink()
        with self.assertRaises(OSError): self.validate()

    def test_changed_license_is_refused(self):
        (self.prefix / 'OPENSSL-LICENSE.txt').write_bytes(b'changed license')
        with self.assertRaises(RuntimeError): self.validate()

    def test_old_unpinned_builds_and_changed_options_are_refused(self):
        for key, value in [('version', '3.0.22'), ('minimumCpu', 'native'), ('minimumGlibc', '2.36'),
                           ('source', {**self.source, 'sha256': '0' * 64}), ('buildOptions', ['no-shared'])]:
            with self.subTest(key=key):
                original = self.proof[key]
                self.proof[key] = value
                self.save_proof()
                with self.assertRaises(RuntimeError): self.validate()
                self.proof[key] = original

    def test_redirected_runtime_is_refused(self):
        source = self.prefix / 'lib/libcrypto.so.3'
        backup = self.prefix / 'other-crypto'
        source.rename(backup)
        try: source.symlink_to(backup)
        except OSError: self.skipTest('Symbolic links unavailable for this account.')
        with self.assertRaises(RuntimeError): self.validate()

    def test_hardlinked_runtime_is_refused(self):
        try: os.link(self.prefix / 'lib/libcrypto.so.3', self.root / 'other-crypto')
        except OSError: self.skipTest('Hard links unavailable for this filesystem.')
        with self.assertRaises(RuntimeError): self.validate()

    def test_untrusted_oversized_proof_is_refused(self):
        self.proof_path.write_bytes(b' ' * 65537)
        with self.assertRaises(RuntimeError): self.validate()

    @unittest.skipUnless(sys.platform == 'linux', 'Linux special-file fixture')
    def test_special_proof_and_source_are_rejected_without_blocking(self):
        pipe = self.root / 'fifo'
        os.mkfifo(pipe, 0o600)
        for operation in (openssl.identity, openssl.read_proof):
            with self.subTest(operation=operation.__name__):
                def child():
                    try: operation(pipe)
                    except RuntimeError: return
                    raise SystemExit(1)
                process = multiprocessing.get_context('fork').Process(target=child)
                process.start()
                process.join(1)
                if process.is_alive():
                    process.terminate()
                    process.join()
                    self.fail('Special file blocked a build-input reader.')
                self.assertEqual(process.exitcode, 0)
        self.assertTrue(pipe.exists())

    def tar(self, rows):
        target = self.root / 'fixture.tar.gz'
        with tarfile.open(target, 'w:gz') as output:
            for name, kind in rows:
                member = tarfile.TarInfo(name)
                member.type = kind
                member.size = 0
                member.linkname = '../../outside'
                output.addfile(member, io.BytesIO())
        return target

    def test_extraction_rejects_escape_links_special_and_duplicate_entries(self):
        for rows in [[('../outside', tarfile.REGTYPE)], [('/outside', tarfile.REGTYPE)],
                     [('openssl-3.5.9/link', tarfile.SYMTYPE)], [('openssl-3.5.9/link', tarfile.LNKTYPE)],
                     [('openssl-3.5.9/pipe', tarfile.FIFOTYPE)],
                     [('openssl-3.5.9/file', tarfile.REGTYPE), ('openssl-3.5.9/file', tarfile.REGTYPE)]]:
            with self.subTest(rows=rows):
                destination = self.root / ('extract-' + str(len(list(self.root.iterdir()))))
                destination.mkdir()
                with self.assertRaises(RuntimeError): openssl.unpack(self.tar(rows), destination)
                self.assertFalse((self.root / 'outside').exists())

    def test_source_pin_is_a_fixed_official_release(self):
        self.assertEqual(openssl.SOURCE['name'], 'openssl-3.5.9.tar.gz')
        self.assertEqual(openssl.SOURCE['url'], 'https://www.openssl.org/source/openssl-3.5.9.tar.gz')
        self.assertEqual(openssl.SOURCE['size'], 53279637)
        self.assertEqual(len(openssl.SOURCE['sha256']), 64)


if __name__ == '__main__': unittest.main()
