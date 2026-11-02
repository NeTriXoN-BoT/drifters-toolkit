"""Offline Linux interface staging checks; no desktop, worker or save is opened."""
from pathlib import Path
import hashlib
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from linux_interface_audit import stage_public_save_codec


class InterfaceStage(unittest.TestCase):
    def test_absent_worker_does_not_copy_old_private_helper(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build, app = root / 'build', root / 'app'
            build.mkdir(); app.mkdir()
            (build / 'DriftersToolkitSavePreview').write_bytes(b'obsolete private program')
            self.assertIsNone(stage_public_save_codec(build, app))
            self.assertEqual(list(app.iterdir()), [])

    def test_wrong_public_worker_refused_without_staging(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build, app = root / 'build', root / 'app'
            build.mkdir(); app.mkdir()
            (build / 'DriftersToolkitSaveCodec').write_bytes(b'not the pinned worker')
            with self.assertRaises(RuntimeError):
                stage_public_save_codec(build, app)
            self.assertEqual(list(app.iterdir()), [])

    def test_verified_snapshot_is_executable_and_independent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build, app = root / 'build', root / 'app'
            build.mkdir(); app.mkdir()
            helper = build / 'DriftersToolkitSaveCodec'
            helper.write_bytes(b'original verified input')
            frozen = b'checked immutable worker snapshot'
            def checked(path, platform):
                self.assertEqual(path, helper)
                self.assertEqual(platform, 'linux')
                helper.write_bytes(b'changed after verification')
                return frozen
            with patch('save_codec_materials.verify_worker', side_effect=checked) as verifier:
                result = stage_public_save_codec(build, app)
                verifier.assert_called_once_with(helper, 'linux')
            staged = app / helper.name
            self.assertEqual(staged.read_bytes(), frozen)
            self.assertEqual(result, hashlib.sha256(frozen).hexdigest())
            if os.name == 'posix':
                self.assertEqual(staged.stat().st_mode & 0o777, 0o755)
            self.assertFalse((app / 'DriftersToolkitSavePreview').exists())
            with self.assertRaises(FileExistsError):
                with patch('save_codec_materials.verify_worker', return_value=frozen):
                    stage_public_save_codec(build, app)
            self.assertEqual(staged.read_bytes(), frozen)


if __name__ == '__main__':
    unittest.main(verbosity=2)
