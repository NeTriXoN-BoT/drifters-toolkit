"""Offline admission/recovery tests; never mutate or compile the retained SDK."""
from pathlib import Path
import copy
from contextlib import ExitStack, nullcontext, redirect_stdout
import io
import json
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'scripts'))
import qt_webengine_vendor_followup as current
import qt_webengine_residual as previous
import bootstrap_qt_webengine_security as base


class VendorFollowup(unittest.TestCase):
    def receipt(self):
        names = ['lib/libQt6WebEngineCore.so.6.11.3', 'lib/libQt6WebEngineWidgets.so.6.11.3',
                 'libexec/QtWebEngineProcess', 'translations/qtwebengine_locales/en-US.pak',
                 *('resources/' + name for name in base.RUNTIME_RESOURCES)]
        return {**current.identity(current.verify_inputs()),
                'outputs': [{'file': name, 'sha256': 'a' * 64, 'size': 1} for name in names],
                'priorOutputs': [{'file': name, 'sha256': 'b' * 64, 'size': 1} for name in names],
                'baselineProof': {'sha256': 'c' * 64, 'size': 41651}}

    def test_complete_original_union_and_immutable_predecessor(self):
        data = current.verify_inputs()
        self.assertEqual(len(data['upstream']), 13)
        self.assertEqual(len(data['files']), 66)
        self.assertEqual(len(data['baselineSourceCheckpoint']), 503)
        self.assertEqual(len(current.source_files(data, 'after')), 565)
        self.assertEqual(sum(row['before'] != row['after'] for row in data['files']), 65)
        unchanged = [row['path'] for row in data['files'] if row['before'] == row['after']]
        self.assertEqual(unchanged, ['chromium/third_party/blink/renderer/core/loader/preload_helper.cc'])
        self.assertEqual(data['baselineResidualLock'], current.record(previous.LOCK))
        self.assertEqual(data['baselineSourceCheckpoint'], previous.source_files(previous.verify_inputs(), 'after'))

    def test_changed_unsafe_or_incomplete_lock_refused(self):
        original = current.load_lock()
        changes = (lambda d: d['files'].pop(), lambda d: d['upstream'].reverse(),
                   lambda d: d['upstream'].pop(), lambda d: d.update(vendorFollowupRevision=0),
                   lambda d: d['files'][0].update(path='../outside'),
                   lambda d: d['files'][0].update(path='chromium//ambiguous.cc'),
                   lambda d: d['files'][0]['after'].update(size=True),
                   lambda d: d['files'][0]['after'].update(sha256='0'),
                   lambda d: d['adapter'].update(name='../outside.patch'),
                   lambda d: d['upstream'][0].update(url='https://example.invalid/'),
                   lambda d: d['baselineResidualLock'].update(sha256='0' * 64),
                   lambda d: d.update(baselineRuntimeProofMeaning='universal binary requirement'),
                   lambda d: d['baselineSourceCheckpoint'].pop(next(iter(d['baselineSourceCheckpoint']))))
        for change in changes:
            data = copy.deepcopy(original); change(data)
            with tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / 'lock.json'; path.write_text(json.dumps(data))
                with self.assertRaises(RuntimeError):
                    current.load_lock(path)

    def test_every_original_and_aggregate_is_pinned(self):
        data = current.load_lock()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); directory = root / data['patchDirectory']; directory.mkdir(parents=True)
            for row in current.all_patch_inputs(data):
                shutil.copyfile(ROOT / data['patchDirectory'] / row['name'], directory / row['name'])
            current.verify_inputs(root)
            for row in current.all_patch_inputs(data):
                file = directory / row['name']; body = file.read_bytes(); file.write_bytes(body + b'changed')
                with self.assertRaises(RuntimeError): current.verify_inputs(root)
                file.write_bytes(body)

    def test_patch_scope_rejects_rename_binary_alias_modes_and_traversal(self):
        valid = 'diff --git a/x.cc b/x.cc\n--- a/x.cc\n+++ b/x.cc\n'
        self.assertEqual(current.patch_paths(valid), {'chromium/x.cc'})
        for body in (valid.replace('b/x.cc', 'b/y.cc', 1), valid + 'GIT binary patch\n',
                     valid + 'new file mode 120000\n', valid + 'old mode 100644\n', valid * 2,
                     valid.replace('x.cc', '../outside'), valid.replace('x.cc', 'x\\y.cc')):
            with self.assertRaises(RuntimeError): current.patch_paths(body)

    def test_unchanged_predecessor_inventory_is_never_dropped(self):
        data = current.load_lock(); parent = previous.source_files(previous.verify_inputs(), 'after')
        before = current.source_files(data, 'before'); after = current.source_files(data, 'after')
        changed = {row['path'] for row in data['files']}
        for name, digest in parent.items():
            self.assertEqual(before[name], digest)
            if name not in changed: self.assertEqual(after[name], digest)
        overlap = next(row for row in data['files'] if row['path'] in parent)
        overlap['before']['sha256'] = '0' * 64
        with self.assertRaisesRegex(RuntimeError, 'immutable predecessor'): current.source_files(data, 'before')

    def test_new_source_preexisting_file_and_missing_inputs_refused(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(current.subprocess, 'run') as run:
            root = Path(temporary)
            with self.assertRaises((RuntimeError, OSError)): current.verify_source(root, current.load_lock(), 'before')
            file = root / 'src/3rdparty/chromium/new.cc'; file.parent.mkdir(parents=True); file.write_text('preserve')
            data = {'files': [{'path': 'chromium/new.cc', 'before': None,
                               'after': {'sha256': 'a' * 64, 'size': 1}}]}
            with patch.object(current, 'source_files', return_value={}), patch.object(current.resume, 'verify_source'):
                with self.assertRaisesRegex(RuntimeError, 'unexpectedly exists'): current.verify_source(root, data, 'before')
            self.assertEqual(file.read_text(), 'preserve'); run.assert_not_called()

    def test_public_proof_requires_changed_core_and_complete_chain_identity(self):
        original = self.receipt(); current.verify_public(original)
        for change in (lambda p: p.pop('adapter'), lambda p: p.pop('baselineProof'),
                       lambda p: p['baselineResidualIdentity'].update(schemaVersion=0),
                       lambda p: p.update(outputs=copy.deepcopy(p['priorOutputs'])),
                       lambda p: p['outputs'][0].update(file='../outside'),
                       lambda p: p['outputs'][0].update(size=True), lambda p: p.update(priorOutputs=[])):
            proof = copy.deepcopy(original); change(proof)
            with self.assertRaises(RuntimeError): current.verify_public(proof)

    def test_completed_latest_runtime_does_not_require_old_live_bytes(self):
        proof = self.receipt()
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary); old = prefix / previous.PROOF; old.write_text('immutable predecessor proof')
            proof['baselineProof'] = current.record(old)
            for row in proof['outputs']:
                file = prefix / row['file']; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(b'new runtime')
                row.update(current.record(file))
            (prefix / current.PROOF).write_text(json.dumps(proof))
            history = {'residual': {'outputs': proof['priorOutputs']}}
            with patch.object(current, 'historical_proofs', return_value=history), \
                 patch.object(previous, 'verify_build', side_effect=AssertionError('old live bytes were replaced')):
                self.assertEqual(current.verify_build(prefix)['vendorFollowup'], proof)
                old.write_text('changed predecessor proof')
                with self.assertRaises(RuntimeError): current.verify_build(prefix)

    def phase_fixture(self, temporary):
        work, prefix = Path(temporary) / 'work', Path(temporary) / 'sdk'
        work.mkdir(); prefix.mkdir(); (prefix / previous.PROOF).write_text('independently authenticated predecessor')
        old = {'residual': {'outputs': self.receipt()['priorOutputs']}}
        return work, prefix, old, current.load_lock(), {'fixed': 'identity'}

    def test_generic_predecessor_capture_is_not_universal_builder_pin(self):
        with tempfile.TemporaryDirectory() as temporary:
            work, prefix, old, data, expected = self.phase_fixture(temporary)
            self.assertNotEqual(current.record(prefix / previous.PROOF), data['baselineRuntimeProof'])
            with patch.object(current, 'verify_source'), patch.object(previous, 'verify_build', return_value=old):
                phase, intent = current.source_phase(work, prefix, data, expected, old)
                self.assertEqual(phase, 'before'); self.assertFalse(intent['observedBaselineRequired'])
                self.assertEqual(intent['baselineProof'], current.record(prefix / previous.PROOF))
                with self.assertRaisesRegex(RuntimeError, 'observed retained predecessor'):
                    current.source_phase(work, prefix, data, expected, old, True)

    def test_strict_observed_guard_requires_all62_verified_predecessor_outputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            work, prefix, old, data, expected = self.phase_fixture(temporary)
            data['baselineRuntimeProof'] = current.record(prefix / previous.PROOF)
            with patch.object(current, 'verify_source'), patch.object(previous, 'verify_build', return_value=old):
                with self.assertRaisesRegex(RuntimeError, 'exact retained predecessor'):
                    current.source_phase(work, prefix, data, expected, old, True)
                old['residual']['outputs'] = [{}] * 62
                phase, intent = current.source_phase(work, prefix, data, expected, old, True)
                self.assertEqual(phase, 'before'); self.assertTrue(intent['observedBaselineRequired'])

    def test_interrupted_apply_or_changed_intent_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            work, prefix, old, data, expected = self.phase_fixture(temporary)
            intent = {'identity': expected, 'baselineProof': current.record(prefix / previous.PROOF),
                      'beforeOutputs': old['residual']['outputs'], 'observedBaselineRequired': False}
            state = work / 'vendor-followup-security-start.json'; state.write_text(json.dumps(intent))
            with patch.object(current, 'verify_source', side_effect=RuntimeError('partial source')), \
                 patch.object(previous, 'verify_build') as prior:
                with self.assertRaisesRegex(RuntimeError, 'partial source'):
                    current.source_phase(work, prefix, data, expected, old)
                prior.assert_not_called()
            intent['identity'] = {'changed': True}; state.write_text(json.dumps(intent))
            with self.assertRaisesRegex(RuntimeError, 'transaction differs'):
                current.source_phase(work, prefix, data, expected, old)

    def test_interrupted_build_or_install_resumes_only_exact_after_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            work, prefix, old, data, expected = self.phase_fixture(temporary)
            intent = {'identity': expected, 'baselineProof': current.record(prefix / previous.PROOF),
                      'beforeOutputs': old['residual']['outputs'], 'observedBaselineRequired': False}
            (work / 'vendor-followup-security-start.json').write_text(json.dumps(intent))
            with patch.object(current, 'verify_source') as verify, \
                 patch.object(previous, 'verify_build', side_effect=AssertionError('partially installed prefix')):
                self.assertEqual(current.source_phase(work, prefix, data, expected, old)[0], 'after')
                self.assertEqual(verify.call_args.args[-1], 'after')

    def test_cache_guard_composes_immutable_parent_and_own_plain_file_checks(self):
        with tempfile.TemporaryDirectory() as temporary:
            work, prefix = Path(temporary) / 'work', Path(temporary) / 'sdk'; work.mkdir(); prefix.mkdir()
            outside = Path(temporary) / 'untouched'; outside.write_text('preserve')
            linked = work / 'vendor-followup-security-start.json'; linked.hardlink_to(outside)
            with patch.object(previous, 'verify_cache_paths') as guard:
                with self.assertRaises(RuntimeError): current.verify_cache_paths(work, prefix)
                guard.assert_called_once_with(work, prefix)
            self.assertEqual(outside.read_text(), 'preserve')

    def test_strict_private_build_rejects_another_cache_before_any_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            work, prefix, _, _, _ = self.phase_fixture(temporary)
            with patch.object(current.subprocess, 'run') as run, patch.object(current, 'check_build_storage') as storage:
                with self.assertRaisesRegex(RuntimeError, 'explicitly reviewed retained cache'):
                    current.build(work, prefix, check_only=True, require_observed_baseline=True)
                run.assert_not_called(); storage.assert_not_called()

    def test_check_only_and_changed_lock_never_apply_or_start_a_compiler(self):
        for changed in (False, True):
            with tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
                work, prefix, old, data, expected = self.phase_fixture(temporary)
                stack.enter_context(patch.object(current, 'verify_inputs', return_value=data))
                stack.enter_context(patch.object(current, 'identity', side_effect=[expected, {'changed': True}] if changed else None,
                                                 return_value=expected))
                for owner, name in ((current, 'verify_cache_paths'), (current, 'verify_install_paths'),
                                    (current.resume, 'verify_configuration'), (current.resume, 'verify_tools'),
                                    (current.resume, 'check_memory')):
                    stack.enter_context(patch.object(owner, name, return_value={}))
                stack.enter_context(patch.object(current, 'check_build_storage', return_value={'projectScope': 'fixture'}))
                stack.enter_context(patch.object(current, 'historical_proofs', return_value=old))
                stack.enter_context(patch.object(current, 'source_phase', return_value=('before', {'intent': True})))
                lock = stack.enter_context(patch.object(current, 'project_build_lock', return_value=nullcontext()))
                budget = stack.enter_context(patch.object(current, 'RuntimeBudget'))
                run = stack.enter_context(patch.object(current.subprocess, 'run'))
                before = (prefix / previous.PROOF).read_bytes()
                if changed:
                    with self.assertRaisesRegex(RuntimeError, 'before acquiring the build lock'):
                        current.build(work, prefix)
                    lock.assert_called_once()
                else:
                    with redirect_stdout(io.StringIO()): receipt = current.build(work, prefix, check_only=True)
                    self.assertEqual(receipt['sourcePhase'], 'before'); lock.assert_not_called()
                run.assert_not_called(); budget.assert_not_called()
                self.assertEqual((prefix / previous.PROOF).read_bytes(), before)

    def test_completed_proof_cannot_bypass_private_observed_baseline_guard(self):
        with tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
            work, prefix, old, data, expected = self.phase_fixture(temporary)
            (prefix / current.PROOF).write_text('a purported completed proof')
            for owner, name in ((current, 'verify_cache_paths'), (current, 'verify_install_paths'),
                                (current.resume, 'verify_configuration'), (current.resume, 'verify_tools'),
                                (current.resume, 'check_memory')):
                stack.enter_context(patch.object(owner, name, return_value={}))
            stack.enter_context(patch.object(current, 'EXPECTED_WORK', work))
            stack.enter_context(patch.object(current, 'EXPECTED_PREFIX', prefix))
            stack.enter_context(patch.object(current, 'check_build_storage', return_value={}))
            stack.enter_context(patch.object(current, 'verify_inputs', return_value=data))
            stack.enter_context(patch.object(current, 'identity', return_value=expected))
            stack.enter_context(patch.object(current, 'historical_proofs', return_value=old))
            completed = stack.enter_context(patch.object(current, 'verify_build'))
            run = stack.enter_context(patch.object(current.subprocess, 'run'))
            with self.assertRaisesRegex(RuntimeError, 'observed retained predecessor proof'):
                current.build(work, prefix, check_only=True, require_observed_baseline=True)
            completed.assert_not_called(); run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
