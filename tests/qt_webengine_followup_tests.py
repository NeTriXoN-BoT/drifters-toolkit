"""Offline proof/source enforcement for the separate upstream repair batch."""
from pathlib import Path
import copy
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'scripts'))
import qt_webengine_followup as followup
import bootstrap_qt_webengine_security as base


class Followup(unittest.TestCase):
    def test_exact_scope_refs_and_frozen_baseline(self):
        data = followup.verify_inputs()
        self.assertEqual(len(data['files']), 20)
        self.assertEqual(len(data['upstream']), 4)
        self.assertEqual(len(data['files']) - sum('unittest' in row['path'] for row in data['files']), 15)
        before = followup.source_files(data, 'before')
        after = followup.source_files(data, 'after')
        self.assertEqual(set(before), set(after))
        self.assertGreater(len(before), 81)
        self.assertEqual(base.file_record(base.FINAL_INVENTORY)['sha256'], 'b30d014b8342ca17e5c18620fd2bb8e0dd67fc56c15065bc5a246edc9ec79f1b')

    def test_corrupt_or_unsafe_lock_refused(self):
        original = followup.load_lock()
        changes = (lambda d: d['files'].pop(), lambda d: d['upstream'].pop(),
                   lambda d: d.update(followupRevision=0),
                   lambda d: d['files'][0].update(path='../elsewhere'),
                   lambda d: d['files'][0]['before'].update(size=True),
                   lambda d: d['files'][0]['after'].update(sha256='0'),
                   lambda d: d['upstream'][0].update(url='https://example.invalid/'),
                   lambda d: d['adapter'].update(name='../outside.patch'),
                   lambda d: d['baselineFinalInventory'].update(sha256='0' * 64))
        for change in changes:
            data = copy.deepcopy(original); change(data)
            with tempfile.TemporaryDirectory() as tmp:
                p = Path(tmp) / 'lock.json'; p.write_text(json.dumps(data), encoding='utf-8')
                with self.assertRaises(RuntimeError): followup.load_lock(p)

    def test_changed_patch_or_reference_refused(self):
        import shutil
        data = followup.load_lock()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); directory = root / data['patchDirectory']; directory.mkdir(parents=True)
            for row in followup.all_patch_inputs(data): shutil.copyfile(ROOT / data['patchDirectory'] / row['name'], directory / row['name'])
            followup.verify_inputs(root)
            for row in followup.all_patch_inputs(data):
                path = directory / row['name']; b = path.read_bytes(); path.write_bytes(b + b'changed')
                with self.assertRaises(RuntimeError): followup.verify_inputs(root)
                path.write_bytes(b)

    def test_wrong_preimage_refused_without_git(self):
        data = followup.load_lock()
        with tempfile.TemporaryDirectory() as tmp, patch.object(followup.subprocess, 'run') as run:
            with self.assertRaises((RuntimeError, OSError)): followup.verify_source(Path(tmp), data, 'before')
            run.assert_not_called()

    def test_public_receipt_requires_current_full_output_scope(self):
        data = followup.load_lock(); value = followup.identity(data)
        names = ['lib/libQt6WebEngineCore.so.6.11.3', 'lib/libQt6WebEngineWidgets.so.6.11.3',
                 'libexec/QtWebEngineProcess', 'translations/qtwebengine_locales/en-US.pak',
                 *('resources/' + name for name in base.RUNTIME_RESOURCES)]
        value['outputs'] = [{'file': name, 'sha256': 'a' * 64, 'size': 1} for name in names]
        followup.verify_public(value)
        for mutate in (lambda d: d.pop('adapter'), lambda d: d['upstream'].pop(),
                       lambda d: d['lock'].update(sha256='0' * 64),
                       lambda d: d.update(effectiveSourceFiles=81),
                       lambda d: d.update(outputs=[]),
                       lambda d: d['outputs'][0].update(file='../elsewhere')):
            changed = copy.deepcopy(value); mutate(changed)
            with self.assertRaises(RuntimeError): followup.verify_public(changed)

    def test_publication_preserves_prior_proof(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'proof.json'; followup.publish(target, {'first': True})
            with self.assertRaises(FileExistsError): followup.publish(target, {'second': True})
            self.assertEqual(json.loads(target.read_text()), {'first': True})
            self.assertEqual(len(list(Path(tmp).iterdir())), 1)
            self.assertEqual(target.stat().st_nlink, 1)

    def test_no_space_reserve_stops_before_any_source_or_build_change(self):
        with tempfile.TemporaryDirectory() as tmp:
            with patch.object(followup, 'check_build_storage', side_effect=RuntimeError('host reserve')), \
                 patch.object(followup.subprocess, 'run') as run, patch.object(followup, 'publish') as publish:
                with self.assertRaisesRegex(RuntimeError, 'host reserve'):
                    followup.build(Path(tmp), Path(tmp))
                run.assert_not_called(); publish.assert_not_called()


if __name__ == '__main__':
    unittest.main()
