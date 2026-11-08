"""Public source boundary regressions using disposable files only.

These tests exercise the production review collector with a reduced fixture
input set. They do not build, download, read user state or launch native tools.
"""
from contextlib import ExitStack
from pathlib import Path
import json
import hashlib
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import review_sources
import build_seasonal_emoji
import build_changelog


class PublicSourcePrivacy(unittest.TestCase):
    def fixture(self, root, names):
        for name, body in names.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(body)
        (root / 'vendor/miniz').mkdir(parents=True, exist_ok=True)

    def collect_fixture(self, root):
        # Dependency cryptographic verifiers are covered separately. Restrict
        # this fixture to the real traversal and private-source boundary logic.
        with ExitStack() as stack:
            stack.enter_context(patch.object(review_sources, 'FIXED', {'src/core.cpp'}))
            for name in ('verify_sources', 'verify_webengine_inputs',
                         'verify_followup_inputs', 'verify_incremental_inputs',
                         'verify_residual_inputs', 'verify_vendor_followup_inputs',
                         'verify_release_1_1_inputs'):
                stack.enter_context(patch.object(review_sources, name, return_value=None))
            stack.enter_context(patch.object(build_seasonal_emoji, 'generate', return_value=None))
            stack.enter_context(patch.object(build_changelog, 'generate', return_value=None))
            return review_sources.collect(root)

    def test_private_state_handoffs_and_decoder_are_not_collected(self):
        with tempfile.TemporaryDirectory(prefix='drifter-public-source-privacy-') as temp:
            root = Path(temp)
            marker = b'PRIVATE FIXTURE MUST NEVER SHIP'
            private = (
                'README_PRIVATE.md', 'docs/NEXT_SESSION.md',
                '.env', '.git/config', 'config/account.json',
                'private-artifacts/release-signing/private-key.pem',
                'logs/run.log', 'profiles/default.json',
                'src/README_PRIVATE.hpp', 'src/.hidden.hpp',
                'src/logs/trace.cpp', 'src/profiles/account.cpp',
                'src/save_preview_decoder/src/main.rs',
                'src/save_codec_decoder/src/main.cpp',
            )
            self.fixture(root, {'src/core.cpp': b'// public fixture\n',
                                **{name: marker for name in private}})
            collected = self.collect_fixture(root)
            self.assertEqual(collected, {'src/core.cpp': b'// public fixture\n'})
            self.assertFalse(any(marker in body for body in collected.values()))
            self.assertTrue(all((root / name).read_bytes() == marker for name in private))

    def test_allowed_source_link_does_not_expose_an_external_private_file(self):
        with tempfile.TemporaryDirectory(prefix='drifter-public-source-link-') as temp:
            base = Path(temp)
            root = base / 'source'
            self.fixture(root, {'src/core.cpp': b'// public fixture\n'})
            external = base / 'private.hpp'
            external.write_bytes(b'PRIVATE OUTSIDE SOURCE ROOT')
            try:
                (root / 'src/link.hpp').symlink_to(external)
            except OSError:
                self.skipTest('Creating symlinks requires OS permission on this host.')
            with self.assertRaises((RuntimeError, ValueError)):
                self.collect_fixture(root)
            self.assertEqual(external.read_bytes(), b'PRIVATE OUTSIDE SOURCE ROOT')

    def test_public_tree_excludes_private_packager_and_tracks_full_codec(self):
        self.assertFalse((ROOT / 'scripts/package_source.py').exists())
        codec = ROOT / 'components/save-codec'
        manifest = json.loads((codec / 'SOURCE-MANIFEST.json').read_text(encoding='utf-8'))
        self.assertEqual(manifest['licence'], 'GPL-3.0-or-later')
        for row in manifest['files']:
            source = codec / row['path']
            self.assertTrue(source.resolve().is_relative_to(codec.resolve()))
            self.assertEqual(source.stat().st_size, row['bytes'])
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), row['sha256'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
