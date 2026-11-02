"""Archive build input and extraction tests; no downloads, builds or host changes."""
from pathlib import Path
import hashlib
import io
import json
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import bootstrap_archive as archive


class ArchiveBootstrap(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='drifter-archive-bootstrap-tests-')
        self.root = Path(self.temporary.name)

    def tearDown(self): self.temporary.cleanup()

    def source(self, rows):
        path = self.root / 'source.tar.gz'
        with tarfile.open(path, 'w:gz') as output:
            for name, kind, body in rows:
                info = tarfile.TarInfo(name); info.type = kind; info.mode = 0o644
                if kind == tarfile.REGTYPE:
                    info.size = len(body); output.addfile(info, io.BytesIO(body))
                else:
                    info.linkname = body.decode(); output.addfile(info)
        body = path.read_bytes()
        return path, {'size': len(body), 'sha256': hashlib.sha256(body).hexdigest()}

    def test_source_pins_are_fixed_and_cached_originals_match(self):
        rows = archive.verify_sources(ROOT / 'vendor/archive-sources')
        self.assertEqual([row['dependency'] for row in rows], ['libarchive', 'zlib', 'bzip2', 'xz', 'zstd', 'lz4'])
        self.assertEqual(rows[0]['version'], '3.8.10')
        self.assertTrue(all(row['url'].startswith('https://') and len(row['sha256']) == 64 for row in rows))

    def test_pinned_bytes_are_checked_before_tar_parsing(self):
        source, expected = self.source([('component/source.c', tarfile.REGTYPE, b'ordinary source')])
        out = self.root / 'out'; out.mkdir()
        actual = archive.unpack(source, out, expected)
        self.assertEqual((actual / 'source.c').read_bytes(), b'ordinary source')
        source.write_bytes(source.read_bytes() + b'changed')
        with self.assertRaises(RuntimeError): archive.unpack(source, self.root / 'other', expected)

    def test_unsafe_source_paths_and_links_are_rejected(self):
        for rows in ([('../escape', tarfile.REGTYPE, b'x')],
                     [('/absolute', tarfile.REGTYPE, b'x')],
                     [('component/link', tarfile.SYMTYPE, b'outside')],
                     [('component/link', tarfile.LNKTYPE, b'outside')],
                     [('component/file', tarfile.REGTYPE, b'x'), ('component/FILE', tarfile.REGTYPE, b'y')]):
            with self.subTest(rows=rows):
                source, expected = self.source(rows)
                out = self.root / ('out-' + str(len(list(self.root.iterdir())))); out.mkdir()
                with self.assertRaises(RuntimeError): archive.unpack(source, out, expected)
                self.assertFalse((self.root / 'escape').exists())

    def test_changed_existing_cache_is_preserved(self):
        name = archive.source_lock()[0]['name']; path = self.root / name
        path.write_bytes(b'changed cached input')
        with self.assertRaises(RuntimeError): archive.download(self.root)
        self.assertEqual(path.read_bytes(), b'changed cached input')

    def test_stale_or_missing_build_proof_is_rejected(self):
        prefix = self.root / 'prefix'; prefix.mkdir()
        (prefix / 'archive-build.json').write_text(json.dumps({'schemaVersion': 1, 'recipeRevision': -1}))
        with self.assertRaises(RuntimeError): archive.verify_build(prefix)
        proof = archive.public_provenance()
        self.assertEqual(proof['linkage'], 'static')
        self.assertEqual(proof['recipeRevision'], archive.RECIPE_REVISION)
        self.assertEqual(proof['limitHeaderSha256'], archive.digest(ROOT / 'scripts/archive_decoder_limits.h'))


if __name__ == '__main__': unittest.main(verbosity=2)
