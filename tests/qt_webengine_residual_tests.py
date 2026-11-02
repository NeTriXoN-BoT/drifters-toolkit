"""Offline tests for the additive residual WebEngine provenance/build boundary."""
from pathlib import Path
from contextlib import ExitStack, nullcontext
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
import qt_webengine_residual as residual
import qt_webengine_incremental as previous
import bootstrap_qt_webengine_security as base


class Residual(unittest.TestCase):
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
        return {**residual.identity(residual.verify_inputs()),
                'outputs': [{'file': name, 'sha256': 'a' * 64, 'size': 1} for name in names],
                'priorOutputs': [{'file': name, 'sha256': 'b' * 64, 'size': 1} for name in names],
                'baselineProof': {'sha256': 'c' * 64, 'size': 36560}}

    def test_exact_predecessor_checkpoint_and_originals_remain_bound(self):
        data = residual.verify_inputs()
        self.assertEqual(data['baselineSourceCheckpoint'], previous.source_files(previous.verify_inputs(), 'after'))
        self.assertEqual(len(data['baselineSourceCheckpoint']), 328)
        self.assertEqual(data['vendorReferenceHead'], residual.VENDOR_HEAD)
        self.assertEqual(len(data['upstream']), 21)
        self.assertEqual(len(data['files']), residual.SOURCE_COUNT)
        self.assertGreater(len(residual.source_files(data, 'after')), 328)
        self.assertEqual(residual.record(previous.LOCK), data['baselineIncrementalLock'])
        large = [row for row in residual.all_patch_inputs(data) if row['size'] > 2 * 1024 * 1024]
        self.assertGreaterEqual(len(large), 2)  # Reviewed Brotli original and derived adapter.

    def test_unsafe_or_changed_lock_refused(self):
        original = residual.load_lock()
        changes = (lambda d: d['files'].pop(), lambda d: d['upstream'].pop(),
                   lambda d: d['upstream'].reverse(), lambda d: d.update(residualRevision=0),
                   lambda d: d['files'][0].update(path='../outside'),
                   lambda d: d['files'][0].update(path='chromium//ambiguous.cc'),
                   lambda d: d['files'][0]['after'].update(size=True),
                   lambda d: d['files'][0]['after'].update(sha256='0'),
                   lambda d: d['upstream'][0].update(url='https://example.invalid/'),
                   lambda d: d['adapter'].update(name='../outside.patch'),
                   lambda d: d['regression'].update(sha256='0' * 64, size=0),
                   lambda d: d['baselineIncrementalLock'].update(sha256='0' * 64),
                   lambda d: d['baselineRuntimeProof'].update(size=True),
                   lambda d: d.update(baselineRuntimeProofMeaning='mandatory universal binary hash'),
                   lambda d: d['baselineSourceCheckpoint'].pop(next(iter(d['baselineSourceCheckpoint']))))
        for change in changes:
            data = copy.deepcopy(original)
            change(data)
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / 'lock.json'
                path.write_text(json.dumps(data), encoding='utf-8')
                with self.assertRaises(RuntimeError):
                    residual.load_lock(path)

    def test_changed_production_regression_or_primary_export_refused(self):
        data = residual.load_lock()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directory = root / data['patchDirectory']
            directory.mkdir(parents=True)
            for row in residual.all_patch_inputs(data):
                shutil.copyfile(ROOT / data['patchDirectory'] / row['name'], directory / row['name'])
            residual.verify_inputs(root)
            for row in (data['upstream'][0], data['upstream'][-1], data['adapter'], data['regression']):
                path = directory / row['name']
                body = path.read_bytes()
                path.write_bytes(body + b'changed')
                with self.assertRaises(RuntimeError):
                    residual.verify_inputs(root)
                path.write_bytes(body)

    def test_before_inventory_extends_without_dropping_unchanged_predecessor(self):
        data = residual.load_lock()
        parent = previous.source_files(previous.verify_inputs(), 'after')
        before = residual.source_files(data, 'before')
        after = residual.source_files(data, 'after')
        changed = {row['path'] for row in data['files']}
        for name, digest in parent.items():
            self.assertEqual(before[name], digest)
            if name not in changed:
                self.assertEqual(after[name], digest)
        row = next(row for row in data['files'] if row['before'] is not None and row['path'] in parent)
        altered = copy.deepcopy(data)
        next(item for item in altered['files'] if item['path'] == row['path'])['before']['sha256'] = '0' * 64
        with self.assertRaisesRegex(RuntimeError, 'immutable predecessor'):
            residual.source_files(altered, 'before')

    def test_missing_source_or_unexpected_new_file_refused_before_any_apply(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(residual.subprocess, 'run') as run:
            with self.assertRaises((RuntimeError, OSError)):
                residual.verify_source(Path(tmp), residual.load_lock(), 'before')
            run.assert_not_called()
            root = Path(tmp)
            path = root / 'src/3rdparty/chromium/new.cc'
            path.parent.mkdir(parents=True)
            path.write_text('unrelated file', encoding='utf-8')
            data = {'files': [{'path': 'chromium/new.cc', 'before': None,
                               'after': {'sha256': 'a' * 64, 'size': 1}}]}
            with patch.object(residual, 'source_files', return_value={}), patch.object(residual.resume, 'verify_source'):
                with self.assertRaisesRegex(RuntimeError, 'already exists'):
                    residual.verify_source(root, data, 'before')
            self.assertEqual(path.read_text(), 'unrelated file')

    def test_old_runtime_relabelled_proof_or_incomplete_output_refused(self):
        original = self.receipt()
        residual.verify_public(original)
        changes = (lambda d: d.pop('regression'), lambda d: d['upstream'].pop(),
                   lambda d: d['lock'].update(sha256='0' * 64),
                   lambda d: d['baselineIncrementalIdentity'].update(schemaVersion=0),
                   lambda d: d.update(effectiveSourceFiles=1), lambda d: d.update(outputs=[]),
                   lambda d: d.update(priorOutputs=[]),
                   lambda d: d.update(outputs=copy.deepcopy(d['priorOutputs'])),
                   lambda d: d.pop('baselineProof'), lambda d: d['outputs'][0].update(size=True),
                   lambda d: d['outputs'][0].update(file='../outside'))
        for change in changes:
            data = copy.deepcopy(original)
            change(data)
            with self.assertRaises(RuntimeError):
                residual.verify_public(data)
        with self.assertRaises(RuntimeError):
            residual.verify_public(None)

    def test_latest_runtime_uses_historical_predecessor_without_demanding_old_live_bytes(self):
        proof = self.receipt()
        with tempfile.TemporaryDirectory() as tmp:
            prefix = Path(tmp)
            (prefix / previous.PROOF).write_text('{"immutableOldEngine":true}', encoding='utf-8')
            predecessor_record = residual.record(prefix / previous.PROOF)
            proof['baselineProof'] = predecessor_record
            for row in proof['outputs']:
                path = prefix / row['file']
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'new authenticated runtime')
                row.update(residual.record(path))
            old = {'incremental': {'outputs': proof['priorOutputs']}}
            (prefix / residual.PROOF).write_text(json.dumps(proof), encoding='utf-8')
            with patch.object(residual, 'historical_proofs', return_value=old), \
                 patch.object(previous, 'verify_build', side_effect=AssertionError('old live outputs no longer present')):
                result = residual.verify_build(prefix)
                self.assertEqual(result['residual']['outputs'], proof['outputs'])
                self.assertEqual(residual.record(prefix / previous.PROOF), predecessor_record)
                (prefix / proof['outputs'][0]['file']).write_bytes(b'old or altered engine')
                with self.assertRaisesRegex(RuntimeError, 'output differs'):
                    residual.verify_build(prefix)
                (prefix / previous.PROOF).write_text('{"changed":true}', encoding='utf-8')
                with self.assertRaisesRegex(RuntimeError, 'preceding runtime provenance'):
                    residual.verify_build(prefix)

    def test_historical_incremental_proof_must_link_exact_followup_and_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = Path(tmp)
            followup = prefix / previous.previous.PROOF
            followup.write_text('{"followup":true}', encoding='utf-8')
            proof = {'baselineProof': residual.record(followup), 'priorOutputs': [{'old': True}]}
            path = prefix / previous.PROOF
            path.write_text(json.dumps(proof), encoding='utf-8')
            with patch.object(previous, 'historical_proofs', return_value={'followup': {'outputs': [{'old': True}]}}), \
                 patch.object(previous, 'verify_public', side_effect=lambda value: value):
                residual.historical_proofs(prefix)
                for key, value in (('baselineProof', {'sha256': '0' * 64, 'size': 1}), ('priorOutputs', [])):
                    altered = {**proof, key: value}
                    path.write_text(json.dumps(altered), encoding='utf-8')
                    with self.assertRaisesRegex(RuntimeError, 'incremental runtime provenance'):
                        residual.historical_proofs(prefix)

    def test_storage_stop_precedes_source_configuration_and_writes(self):
        with tempfile.TemporaryDirectory() as tmp, \
             patch.object(residual, 'check_build_storage', side_effect=RuntimeError('host reserve')), \
             patch.object(residual.subprocess, 'run') as run, patch.object(residual, 'publish') as publish:
            with self.assertRaisesRegex(RuntimeError, 'host reserve'):
                residual.build(Path(tmp), Path(tmp))
            run.assert_not_called()
            publish.assert_not_called()

    def test_wrong_retained_transaction_and_mixed_source_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            work, prefix = self.cache(Path(tmp))
            (prefix / previous.PROOF).write_bytes(b'old immutable proof')
            old = {'incremental': {'outputs': [{'old': True}]}}
            state = work / 'residual-security-start.json'
            state.write_text('{}', encoding='utf-8')
            with patch.object(residual, 'verify_source') as source, patch.object(previous, 'verify_build') as live:
                with self.assertRaisesRegex(RuntimeError, 'transaction differs'):
                    residual.source_phase(work, prefix, {}, {}, old)
                source.assert_not_called()
                live.assert_not_called()
            intent = {'identity': {}, 'baselineProof': residual.record(prefix / previous.PROOF), 'beforeOutputs': old['incremental']['outputs']}
            state.write_text(json.dumps(intent), encoding='utf-8')
            with patch.object(residual, 'verify_source', side_effect=RuntimeError('mixed source')), \
                 patch.object(previous, 'verify_build') as live:
                with self.assertRaisesRegex(RuntimeError, 'mixed source'):
                    residual.source_phase(work, prefix, {}, {}, old)
                live.assert_not_called()

    def test_exact_after_source_resume_does_not_reapply_or_require_prior_live_runtime(self):
        with tempfile.TemporaryDirectory() as tmp:
            work, prefix = self.cache(Path(tmp))
            (prefix / previous.PROOF).write_bytes(b'old immutable proof')
            old = {'incremental': {'outputs': [{'old': True}]}}
            intent = {'identity': {}, 'baselineProof': residual.record(prefix / previous.PROOF), 'beforeOutputs': old['incremental']['outputs']}
            (work / 'residual-security-start.json').write_text(json.dumps(intent), encoding='utf-8')
            with patch.object(residual, 'verify_source') as source, patch.object(previous, 'verify_build') as live:
                self.assertEqual(residual.source_phase(work, prefix, {}, {}, old), ('after', intent))
                self.assertEqual(source.call_args.args[-1], 'after')
                live.assert_not_called()

    def test_exclusive_proof_publication_preserves_predecessor_and_existing_leaf(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            prior = root / previous.PROOF
            prior.write_bytes(b'immutable previous proof')
            target = root / residual.PROOF
            residual.publish(target, {'finished': True})
            with self.assertRaises(FileExistsError):
                residual.publish(target, {'replacement': True})
            self.assertEqual(json.loads(target.read_text()), {'finished': True})
            self.assertEqual(prior.read_bytes(), b'immutable previous proof')

    def test_source_changed_during_final_install_cannot_publish_runtime_proof(self):
        with tempfile.TemporaryDirectory() as tmp, ExitStack() as mocks:
            work, prefix = self.cache(Path(tmp))
            (prefix / previous.PROOF).write_bytes(b'immutable predecessor')
            sentinel = work / 'source/source-fixture.cc'
            sentinel.write_bytes(b'reviewed source')
            data, expected = {}, {'frozen': True}
            old = {'incremental': {'outputs': [{'prior': True}]}}
            for target, value in ((residual, 'verify_cache_paths'), (residual.resume, 'verify_configuration'),
                                  (residual.resume, 'verify_tools'), (residual, 'verify_install_paths')):
                mocks.enter_context(patch.object(target, value))
            mocks.enter_context(patch.object(residual, 'verify_inputs', return_value=data))
            mocks.enter_context(patch.object(residual, 'identity', return_value=expected))
            mocks.enter_context(patch.object(residual, 'check_build_storage', return_value={}))
            mocks.enter_context(patch.object(residual.resume, 'check_memory', return_value={}))
            mocks.enter_context(patch.object(residual.resume, 'load_resume_inputs', return_value=({},)))
            mocks.enter_context(patch.object(residual, 'historical_proofs', return_value=old))
            mocks.enter_context(patch.object(residual, 'source_phase', return_value=('after', {})))
            mocks.enter_context(patch.object(residual, 'project_build_lock', return_value=nullcontext()))
            mocks.enter_context(patch.object(residual, 'RuntimeBudget'))
            outputs = [{'file': 'lib/libQt6WebEngineCore.so.6.11.3', 'sha256': 'a' * 64, 'size': 1}]
            mocks.enter_context(patch.object(residual.base, 'runtime_outputs', return_value=outputs))
            mocks.enter_context(patch.object(residual.resume, 'elf_floor', return_value={}))
            mocks.enter_context(patch.object(residual, 'verify_public'))
            mocks.enter_context(patch.object(residual, 'verify_build', return_value={}))
            publish = mocks.enter_context(patch.object(residual, 'publish'))
            install_count = 0
            def run_phase(command, output, env, status, phase, budget, memory):
                nonlocal install_count
                if phase == 'residual-install':
                    install_count += 1
                    if install_count == len(base.RUNTIME_INSTALL_SCRIPTS):
                        sentinel.write_bytes(b'changed while installing')
            def verify_source(source, selected, phase):
                if sentinel.read_bytes() != b'reviewed source':
                    raise RuntimeError('retained source changed during install')
            mocks.enter_context(patch.object(residual.resume, 'run_phase', side_effect=run_phase))
            mocks.enter_context(patch.object(residual, 'verify_source', side_effect=verify_source))
            with self.assertRaisesRegex(RuntimeError, 'changed during install'):
                residual.build(work, prefix)
            self.assertEqual(install_count, len(base.RUNTIME_INSTALL_SCRIPTS))
            publish.assert_not_called()
            self.assertFalse((prefix / residual.PROOF).exists())

    def test_alias_logs_and_canonical_outputs_refused_but_soname_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            work, prefix = self.cache(root)
            other = root / 'unrelated.bin'
            other.write_bytes(b'keep unchanged')
            paths = [work / 'residual-security-build.log', work / 'residual-security-start.json',
                     prefix / 'lib/libQt6WebEngineCore.so.6.11.3', prefix / 'libexec/QtWebEngineProcess',
                     prefix / 'resources/qtwebengine_resources.pak', prefix / 'translations/qtwebengine_locales/de.pak']
            for path in paths:
                os.link(other, path)
                with self.assertRaisesRegex(RuntimeError, 'independent regular file'):
                    residual.verify_cache_paths(work, prefix)
                path.unlink()
                try:
                    path.symlink_to(other)
                except OSError:
                    continue
                with self.assertRaisesRegex(RuntimeError, 'redirected'):
                    residual.verify_cache_paths(work, prefix)
                path.unlink()
            core = prefix / 'lib/libQt6WebEngineCore.so.6.11.3'
            core.write_bytes(b'new engine')
            try:
                (prefix / 'lib/libQt6WebEngineCore.so.6').symlink_to(core.name)
            except OSError:
                pass
            residual.verify_cache_paths(work, prefix)
            self.assertEqual(other.read_bytes(), b'keep unchanged')

    def install_scripts(self, work, body):
        for name in base.RUNTIME_INSTALL_SCRIPTS:
            path = work / 'build' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(body, encoding='utf-8')

    def test_install_paths_cover_headers_cmake_metadata_and_legitimate_library_aliases(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            work, prefix = self.cache(root)
            self.install_scripts(work, '''file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/QtWebEngineCore" TYPE DIRECTORY FILES "/build/staging/")
file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/Qt6WebEngineCore" TYPE FILE FILES "/build/Qt6WebEngineCoreConfig.cmake")
file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/build/libQt6WebEngineCore.so.6.11.3" "/build/libQt6WebEngineCore.so.6" "/build/libQt6WebEngineCore.so")
''')
            residual.verify_install_paths(work, prefix)
            unrelated = root / 'unrelated.txt'
            unrelated.write_bytes(b'unrelated bytes remain intact')
            target = prefix / 'lib/cmake/Qt6WebEngineCore/Qt6WebEngineCoreConfig.cmake'
            target.parent.mkdir(parents=True)
            os.link(unrelated, target)
            with self.assertRaisesRegex(RuntimeError, 'independent regular file'):
                residual.verify_install_paths(work, prefix)
            target.unlink()
            headers = prefix / 'include/QtWebEngineCore'
            headers.parent.mkdir(parents=True)
            other = root / 'unrelated-directory'
            other.mkdir()
            try:
                headers.symlink_to(other, target_is_directory=True)
            except OSError:
                pass
            else:
                with self.assertRaisesRegex(RuntimeError, 'redirected'):
                    residual.verify_install_paths(work, prefix)
                headers.unlink()
            headers.mkdir()
            nested = headers / 'nested-header.h'
            os.link(unrelated, nested)
            with self.assertRaisesRegex(RuntimeError, 'independent regular file'):
                residual.verify_install_paths(work, prefix)
            nested.unlink()
            core = prefix / 'lib/libQt6WebEngineCore.so.6.11.3'
            core.write_bytes(b'independent canonical library')
            alias6 = prefix / 'lib/libQt6WebEngineCore.so.6'
            try:
                alias6.symlink_to(core.name)
                (prefix / 'lib/libQt6WebEngineCore.so').symlink_to(alias6.name)
            except OSError:
                pass
            else:
                residual.verify_install_paths(work, prefix)
                alias6.unlink()
                alias6.symlink_to(unrelated)
                with self.assertRaisesRegex(RuntimeError, 'SONAME.*redirected'):
                    residual.verify_install_paths(work, prefix)
            self.assertEqual(unrelated.read_bytes(), b'unrelated bytes remain intact')

    def test_install_script_unknown_or_external_destination_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            work, prefix = self.cache(Path(tmp))
            variants = ('file(INSTALL DESTINATION "/outside" TYPE FILE FILES "/build/a.h")',
                        'file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/../outside" TYPE FILE FILES "/build/a.h")',
                        'file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "${unknown}/a.h")',
                        'file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "/build/a.h" OPTIONAL)',
                        'file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "/build/a.h")\ninclude("child/cmake_install.cmake")')
            for body in variants:
                self.install_scripts(work, body)
                with self.assertRaises(RuntimeError):
                    residual.verify_install_paths(work, prefix)

    def test_install_environment_cannot_redirect_prefix_or_replace_regular_files_with_links(self):
        with patch.dict(os.environ, {'DESTDIR': '/unrelated', 'CMAKE_INSTALL_MODE': 'SYMLINK',
                                     'QTWEBENGINE_CHROMIUM_FLAGS': '--no-sandbox', 'LD_PRELOAD': 'unrelated.so'}):
            env = residual.build_environment(Path('/retained/work'))
            for name in ('DESTDIR', 'CMAKE_INSTALL_MODE', 'QTWEBENGINE_CHROMIUM_FLAGS', 'LD_PRELOAD'):
                self.assertNotIn(name, env)
            self.assertEqual(env['NINJAFLAGS'], '-j 2')
            self.assertEqual(env['CMAKE_BUILD_PARALLEL_LEVEL'], '1')
            self.assertTrue(env['PATH'].startswith(str(Path('/retained/work/node-v22.23.4-linux-x64/bin')) + os.pathsep))

    def test_raw_root_alias_refused_before_storage_or_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            work, prefix = self.cache(root)
            alias = root / 'alias'
            try:
                alias.symlink_to(work, target_is_directory=True)
            except OSError:
                self.skipTest('Directory symlink creation unavailable.')
            with patch.object(residual, 'check_build_storage') as storage, patch.object(residual.subprocess, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'redirected'):
                    residual.build(alias, prefix)
                storage.assert_not_called()
                run.assert_not_called()


if __name__ == '__main__':
    unittest.main(verbosity=2)
