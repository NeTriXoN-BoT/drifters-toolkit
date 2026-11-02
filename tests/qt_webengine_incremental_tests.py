"""Offline enforcement of the incremental engine source/provenance boundary."""
from pathlib import Path
import copy
import json
import os
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'scripts'))
import qt_webengine_incremental as incremental
import qt_webengine_followup as previous
import bootstrap_qt_webengine_security as base


class Incremental(unittest.TestCase):
    def cache(self, root):
        work, prefix = root / 'work', root / 'sdk'
        for path in (work / 'source', work / 'build/src/core/Release/x86_64', prefix / 'lib',
                     prefix / 'libexec', prefix / 'resources', prefix / 'translations/qtwebengine_locales'):
            path.mkdir(parents=True, exist_ok=True)
        return work, prefix

    def receipt(self):
        names = ['lib/libQt6WebEngineCore.so.6.11.3', 'lib/libQt6WebEngineWidgets.so.6.11.3',
                 'libexec/QtWebEngineProcess', 'translations/qtwebengine_locales/en-US.pak',
                 *('resources/' + name for name in base.RUNTIME_RESOURCES)]
        return {**incremental.identity(incremental.verify_inputs()),
                'outputs': [{'file': name, 'sha256': 'a' * 64, 'size': 1} for name in names],
                'priorOutputs': [{'file': name, 'sha256': 'b' * 64, 'size': 1} for name in names],
                'baselineProof': {'sha256': 'c' * 64, 'size': 15699}}

    def test_exact_ordered_scope_and_immutable_previous_layers(self):
        data = incremental.verify_inputs()
        self.assertEqual(len(data['upstream']), 53)
        self.assertEqual(len(data['files']), 230)
        self.assertEqual(len(incremental.source_files(data, 'after')), 328)
        self.assertEqual(sum(row['before'] is None for row in data['files']), 18)
        self.assertEqual(data['upstream'][-1]['commit'], incremental.CRITICAL)
        self.assertEqual(base.file_record(previous.LOCK)['sha256'],
                         '7093d3c7e09906770c952629c519ebd64862123434728bb129a75d5919edb016')
        self.assertEqual(base.file_record(base.FINAL_INVENTORY)['sha256'],
                         'b30d014b8342ca17e5c18620fd2bb8e0dd67fc56c15065bc5a246edc9ec79f1b')
        decisions = [row['decision'] for row in data['excludedVendorCommits']]
        self.assertEqual({name: decisions.count(name) for name in set(decisions)},
                         {'overlap': 4, 'platform-excluded': 2, 'optional-pki': 16})

    def test_unsafe_or_changed_source_lock_refused(self):
        original = incremental.load_lock()
        changes = (lambda d: d['files'].pop(), lambda d: d['upstream'].pop(),
                   lambda d: d['upstream'].reverse(), lambda d: d.update(incrementalRevision=0),
                   lambda d: d['files'][0].update(path='../outside'),
                   lambda d: d['files'][0]['after'].update(size=True),
                   lambda d: d['files'][0]['after'].update(sha256='0'),
                   lambda d: d['upstream'][0].update(url='https://example.invalid/'),
                   lambda d: d['adapter'].update(name='../outside.patch'),
                   lambda d: d['baselineFollowupLock'].update(sha256='0' * 64))
        for change in changes:
            data = copy.deepcopy(original)
            change(data)
            with tempfile.TemporaryDirectory() as tmp:
                p = Path(tmp) / 'lock.json'
                p.write_text(json.dumps(data), encoding='utf-8')
                with self.assertRaises(RuntimeError):
                    incremental.load_lock(p)

    def test_changed_adapter_or_primary_export_refused(self):
        data = incremental.load_lock()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directory = root / data['patchDirectory']
            directory.mkdir(parents=True)
            for row in incremental.all_patch_inputs(data):
                shutil.copyfile(ROOT / data['patchDirectory'] / row['name'], directory / row['name'])
            incremental.verify_inputs(root)
            for row in [data['upstream'][0], data['upstream'][-1], data['adapter']]:
                p = directory / row['name']
                original = p.read_bytes()
                p.write_bytes(original + b'changed')
                with self.assertRaises(RuntimeError):
                    incremental.verify_inputs(root)
                p.write_bytes(original)

    def test_missing_source_preimage_refused_before_any_git(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(incremental.subprocess, 'run') as run:
            with self.assertRaises((RuntimeError, OSError)):
                incremental.verify_source(Path(tmp), incremental.load_lock(), 'before')
            run.assert_not_called()

    def test_unexpected_new_source_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / 'src/3rdparty/chromium/new.cc'
            path.parent.mkdir(parents=True)
            path.write_text('unrelated existing input', encoding='utf-8')
            data = {'files': [{'path': 'chromium/new.cc', 'before': None, 'after': {'size': 1, 'sha256': 'a' * 64}}]}
            with patch.object(incremental, 'source_files', return_value={}), \
                 patch.object(incremental.resume, 'verify_source'):
                with self.assertRaisesRegex(RuntimeError, 'already exists'):
                    incremental.verify_source(root, data, 'before')
            self.assertEqual(path.read_text(), 'unrelated existing input')

    def test_old_runtime_or_incomplete_new_receipt_refused(self):
        original = self.receipt()
        incremental.verify_public(original)
        changes = (lambda d: d.pop('adapter'), lambda d: d['upstream'].pop(),
                   lambda d: d['lock'].update(sha256='0' * 64),
                   lambda d: d.update(effectiveSourceFiles=101),
                   lambda d: d.update(outputs=[]), lambda d: d.update(priorOutputs=[]),
                   lambda d: d.update(outputs=copy.deepcopy(d['priorOutputs'])),
                   lambda d: d.pop('baselineProof'), lambda d: d['outputs'][0].update(size=True),
                   lambda d: d['outputs'][0].update(file='../outside'))
        for change in changes:
            data = copy.deepcopy(original)
            change(data)
            with self.assertRaises(RuntimeError):
                incremental.verify_public(data)
        with self.assertRaises(RuntimeError):
            incremental.verify_public(None)

    def test_storage_stop_precedes_source_build_and_proof_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            with patch.object(incremental, 'check_build_storage', side_effect=RuntimeError('host reserve')), \
                 patch.object(incremental.subprocess, 'run') as run, patch.object(incremental, 'publish') as publish:
                with self.assertRaisesRegex(RuntimeError, 'host reserve'):
                    incremental.build(Path(tmp), Path(tmp))
                run.assert_not_called()
                publish.assert_not_called()

    def test_new_proof_publication_preserves_existing_record(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / incremental.PROOF
            incremental.publish(target, {'historical': True})
            with self.assertRaises(FileExistsError):
                incremental.publish(target, {'replacement': True})
            self.assertEqual(json.loads(target.read_text()), {'historical': True})
            self.assertEqual(len(list(Path(tmp).iterdir())), 1)

    def test_raw_root_symlink_is_rejected_before_storage_resolution(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            work, prefix = self.cache(root)
            alias = root / 'work-alias'
            try:
                alias.symlink_to(work, target_is_directory=True)
            except OSError:
                self.skipTest('Directory symlink creation is unavailable to this test user.')
            with patch.object(incremental, 'check_build_storage') as storage, \
                 patch.object(incremental.subprocess, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'redirected'):
                    incremental.build(alias, prefix)
                storage.assert_not_called()
                run.assert_not_called()

    def test_hardlinked_log_is_rejected_before_configuration_or_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            work, prefix = self.cache(root)
            unrelated = root / 'unrelated.txt'
            unrelated.write_text('keep this file unchanged', encoding='utf-8')
            os.link(unrelated, work / 'incremental-security-build.log')
            with patch.object(incremental, 'check_build_storage', return_value={}), \
                 patch.object(incremental.resume, 'verify_configuration') as configuration, \
                 patch.object(incremental.subprocess, 'run') as run, patch.object(incremental, 'publish') as publish:
                with self.assertRaisesRegex(RuntimeError, 'independent regular file'):
                    incremental.build(work, prefix)
                configuration.assert_not_called()
                run.assert_not_called()
                publish.assert_not_called()
            self.assertEqual(unrelated.read_text(), 'keep this file unchanged')

    def test_nested_cache_and_source_aliases_do_not_pass_root_checks(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            work, prefix = self.cache(root)
            real = root / 'different-directory'
            real.mkdir()
            (prefix / 'lib').rmdir()
            try:
                (prefix / 'lib').symlink_to(real, target_is_directory=True)
            except OSError:
                self.skipTest('Directory symlink creation is unavailable to this test user.')
            with patch.object(incremental, 'check_build_storage', return_value={}), \
                 patch.object(incremental.resume, 'verify_configuration') as configuration:
                with self.assertRaisesRegex(RuntimeError, 'redirected'):
                    incremental.build(work, prefix)
                configuration.assert_not_called()
            alias = root / 'source-alias'
            alias.symlink_to(work / 'source', target_is_directory=True)
            with patch.object(incremental.resume, 'verify_source') as source:
                with self.assertRaisesRegex(RuntimeError, 'redirected'):
                    incremental.verify_source(alias, incremental.load_lock(), 'before')
                source.assert_not_called()

    def test_partial_install_rejects_canonical_output_aliases_without_rejecting_sonames(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            work, prefix = self.cache(root)
            unrelated = root / 'unrelated-output.bin'
            unrelated.write_bytes(b'unrelated file remains unchanged')
            canonical = ['lib/libQt6WebEngineCore.so.6.11.3',
                         'lib/libQt6WebEngineWidgets.so.6.11.3', 'libexec/QtWebEngineProcess',
                         'resources/' + base.RUNTIME_RESOURCES[0],
                         'translations/qtwebengine_locales/de.pak']
            # Phase-after resume can have missing outputs; those remain allowed.
            incremental.verify_cache_paths(work, prefix)
            for name in canonical:
                path = prefix / name
                os.link(unrelated, path)
                with self.assertRaisesRegex(RuntimeError, 'independent regular file'):
                    incremental.verify_cache_paths(work, prefix)
                path.unlink()
                try:
                    path.symlink_to(unrelated)
                except OSError:
                    continue
                with self.assertRaisesRegex(RuntimeError, 'redirected'):
                    incremental.verify_cache_paths(work, prefix)
                path.unlink()
            core = prefix / canonical[0]
            core.write_bytes(b'canonical engine output')
            try:
                (prefix / 'lib/libQt6WebEngineCore.so.6').symlink_to(core.name)
            except OSError:
                pass
            incremental.verify_cache_paths(work, prefix)
            self.assertEqual(unrelated.read_bytes(), b'unrelated file remains unchanged')


if __name__ == '__main__':
    unittest.main(verbosity=2)
