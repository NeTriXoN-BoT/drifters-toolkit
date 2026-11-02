"""Offline enforcement of exact vendor backports, byte proofs and source closure."""
from pathlib import Path
import copy
import datetime
import hashlib
import json
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import bootstrap_qt_webengine_security as security
import review_sources
import resume_qt_webengine_security as continuation
import linux_build_storage as storage


class SecurityRecipe(unittest.TestCase):
    def test_complete_pinned_official_series_and_public_source_allowlist(self):
        data=security.verify_inputs()
        self.assertEqual(len(data['patches']),24)
        self.assertEqual(len(data['criticalAdvisories']),6)
        self.assertEqual(data['recipeRevision'],2)
        for row in security.all_patch_inputs(data):
            self.assertTrue(review_sources.allowed(data['patchDirectory']+'/'+row['name']))
        self.assertTrue(review_sources.allowed('scripts/bootstrap_qt_webengine_security.py'))
        self.assertTrue(review_sources.allowed('scripts/qt_webengine_security_lock.json'))
        self.assertFalse(review_sources.allowed(data['patchDirectory']+'/unreviewed.patch'))
        self.assertFalse(review_sources.allowed('build-artifacts/private-proof.json'))

    def test_changed_or_incomplete_chain_refused(self):
        original=security.load_lock()
        for change in (lambda data:data['patches'].pop(),
                       lambda data:data.update(recipeRevision=1),
                       lambda data:data.pop('compatibilityBackports'),
                       lambda data:data['patches'][1].update(parent='0'*40),
                       lambda data:data['patches'][0].update(url='https://example.invalid/patch'),
                       lambda data:data['patches'][0].update(name='../patch'),
                       lambda data:data.update(patchedChromiumCommit='0'*40)):
            data=copy.deepcopy(original);change(data)
            with tempfile.TemporaryDirectory() as temporary:
                path=Path(temporary)/'lock.json';path.write_text(json.dumps(data),encoding='utf-8')
                with self.assertRaises(RuntimeError):security.load_lock(path)

    def fixture_proof(self,prefix):
        data=security.load_lock();library=prefix/'lib/libQt6WebEngineCore.so.6.11.3'
        library.parent.mkdir();library.write_bytes(b'compiled updated core fixture')
        for name in ('lib/libQt6WebEngineWidgets.so.6.11.3','libexec/QtWebEngineProcess',
                     'translations/qtwebengine_locales/en-US.pak',*('resources/'+value for value in security.RUNTIME_RESOURCES)):
            target=prefix/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b'updated runtime fixture')
        proof={'schemaVersion':1,'recipeRevision':data['recipeRevision'],
               'securityLock':security.file_record(security.LOCK),'recipe':security.file_record(security.__file__),
               'storageHelper':security.file_record(ROOT/'scripts/linux_build_storage.py'),
               'runtimeScope':security.runtime_scope(),
               'compatibilityBackports':data['compatibilityBackports'],'sourceInventories':security.source_inventory_identity(),
               'patchedChromiumCommit':data['patchedChromiumCommit'],
               'sourceArchive':{'sha256':data['sourceArchive']['digest'],'size':data['sourceArchive']['size']},
               'patches':[{key:row[key] for key in ('commit','sha256','size')} for row in data['patches']],
               'outputs':security.runtime_outputs(prefix)}
        (prefix/security.PROOF).write_text(json.dumps(proof),encoding='utf-8')
        return proof,library

    def test_old_sdk_missing_core_changed_bytes_or_stale_proof_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            prefix=Path(temporary)
            with self.assertRaises(FileNotFoundError):security.verify_build(prefix)
            proof,library=self.fixture_proof(prefix)
            security.verify_build(prefix)
            original=library.read_bytes();library.write_bytes(b'old or modified SDK')
            with self.assertRaises(RuntimeError):security.verify_build(prefix)
            library.write_bytes(original)
            for change in (lambda data:data.update(recipeRevision=0),
                           lambda data:data.update(recipeRevision=1),
                           lambda data:data.pop('compatibilityBackports'),
                           lambda data:data['compatibilityBackports'][0]['before'].clear(),
                           lambda data:data['sourceInventories'].pop('final'),
                           lambda data:data.update(outputs=[]),
                           lambda data:data['patches'].pop(),
                           lambda data:data['runtimeScope']['targets'].append('Pdf'),
                           lambda data:data['outputs'].append({'file':'lib/libQt6WebEngineQuick.so.6.11.3','sha256':'0'*64,'size':1}),
                           lambda data:data['sourceArchive'].update(sha256='0'*64),
                           lambda data:data['outputs'][0].update(file='../outside')):
                data=copy.deepcopy(proof);change(data)
                (prefix/security.PROOF).write_text(json.dumps(data),encoding='utf-8')
                with self.assertRaises(RuntimeError):security.verify_build(prefix)

    def test_resume_preserves_old_configure_identity_and_rejects_forged_receipts(self):
        data,original,files=continuation.load_resume_inputs()
        self.assertEqual(len(files),81)
        self.assertEqual(security.file_record(continuation.INVENTORY)['sha256'],'f9ea6337cee48dea499622bd5933ea7376723ec51759eaa96d049503bc49e0db')
        self.assertNotEqual(data['configuredRecipe'],security.file_record(security.__file__))
        record=continuation.continuation_record(data)
        self.assertEqual(record['configuredRecipe'],data['configuredRecipe'])
        self.assertEqual(record['finalRecipe'],security.file_record(security.__file__))
        override=continuation.continuation_record(data,3)
        self.assertEqual(override['nestedJobs'],3)
        self.assertEqual(override['configuredNestedJobs'],2)
        self.assertEqual(override['coreBuildOverride'],{'target':'QtWebEngineCore','jobs':3,'phase':'normal-ninja'})
        changed=copy.deepcopy(override);changed['nestedJobs']=2
        with self.assertRaises(RuntimeError):continuation.verify_continuation(changed)
        changed=copy.deepcopy(override);changed['coreBuildOverride']['jobs']=4
        with self.assertRaises(RuntimeError):continuation.verify_continuation(changed)
        for mutate in (lambda value:value.update(nestedJobs=16),
                       lambda value:value['configuredRecipe'].update(sha256='0'*64),
                       lambda value:value['helper'].update(sha256='0'*64)):
            changed=copy.deepcopy(record);mutate(changed)
            with self.assertRaises(RuntimeError):continuation.verify_continuation(changed)
        for name in ('scripts/resume_qt_webengine_security.py','scripts/qt_webengine_resume_lock.json',
                     'scripts/qt_webengine_patched_files.json','scripts/qt_webengine_final_files.json',data['configuredRecipeFile']):
            self.assertTrue(review_sources.allowed(name))

    def test_resume_source_hash_check_refuses_changed_or_redirected_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            source=Path(temporary);target=source/'src/3rdparty/chromium/fix.cc';target.parent.mkdir(parents=True)
            target.write_bytes(b'expected vendor fix')
            files={'chromium/fix.cc':hashlib.sha256(target.read_bytes()).hexdigest()}
            continuation.verify_source(source,files)
            target.write_bytes(b'stale or changed vendor fix')
            with self.assertRaises(RuntimeError):continuation.verify_source(source,files)
            target.unlink()
            with self.assertRaises(FileNotFoundError):continuation.verify_source(source,files)

    def test_changed_adapter_or_primary_reference_bytes_are_refused(self):
        import shutil
        data=security.load_lock()
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);directory=root/data['patchDirectory'];directory.mkdir(parents=True)
            for row in security.all_patch_inputs(data):shutil.copyfile(ROOT/data['patchDirectory']/row['name'],directory/row['name'])
            security.verify_inputs(root)
            for row in (data['compatibilityBackports'][0],*data['compatibilityBackports'][0]['upstream']):
                path=directory/row['name'];original=path.read_bytes();path.write_bytes(original+b'changed')
                with self.assertRaises(RuntimeError):security.verify_inputs(root)
                path.write_bytes(original)

    def test_adapter_refuses_wrong_preimage_before_any_git_mutation(self):
        data=security.load_lock();row=data['compatibilityBackports'][0]
        with tempfile.TemporaryDirectory() as temporary:
            source=Path(temporary);first=source/'src/3rdparty'/row['files'][0];first.parent.mkdir(parents=True);first.write_bytes(b'wrong baseline')
            with patch.object(security.subprocess,'run') as apply:
                with self.assertRaises(RuntimeError):security.apply_compatibility(source,data)
                apply.assert_not_called()

    def test_vendor_before_and_final_source_hashes_cannot_be_conflated(self):
        data=security.load_lock()
        for phase,name in (('before','chromium/ui/aura/window_event_dispatcher.cc'),('after','chromium/ui/aura/window.h')):
            changed=copy.deepcopy(data);changed['compatibilityBackports'][0][phase][name]['sha256']='0'*64
            with patch.object(security,'verify_inputs',return_value=changed):
                with self.assertRaises(RuntimeError):continuation.load_resume_inputs()

    def test_resume_storage_requires_fresh_dedicated_host_attestation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);marker=root/'storage.json';now=datetime.datetime(2026,10,26,12,tzinfo=datetime.timezone.utc)
            data={'schemaVersion':1,'distro':'DriftersToolkitBuild','windowsBackingPath':'D:/DriftersToolkit-Build/Linux/ext4.vhdx',
                  'approvedHostDrive':'D:','registryBackingPath':'D:/DriftersToolkit-Build/Linux/ext4.vhdx','registryBasePath':'D:/DriftersToolkit-Build/Linux',
                  'projectRootPath':'D:/DriftersToolkit-Build','maxProjectBytes':80*1024**3,
                  'baselineLinuxUsedBytes':16*1024**3,'baselineVhdBytes':18*1024**3,
                  'hostAvailableBytes':128*1024**3,'checkedAtUtc':now.isoformat(),'rootFilesystemSource':'/dev/sdx'}
            usage={'projectBytes':18*1024**3,'vhdFileBytes':18*1024**3,'otherProjectBytes':0,'entries':1}
            with patch.dict('os.environ',{'WSL_DISTRO_NAME':'DriftersToolkitBuild'}),patch.object(storage,'is_wsl',return_value=True),patch.object(storage.subprocess,'check_output',return_value='/dev/sdx\n'),patch.object(storage.shutil,'disk_usage',return_value=SimpleNamespace(free=128*1024**3,used=16*1024**3)),patch.object(storage,'project_usage',return_value=usage):
                marker.write_text(json.dumps(data),encoding='utf-8');continuation.verify_storage(marker,root,root,'D:',now)
                # Public source users explicitly approving another adequate drive
                # retain the same checks; this workspace still requires D:.
                other={**data,'approvedHostDrive':'E:','windowsBackingPath':'E:/IndependentBuild/Linux/ext4.vhdx','registryBackingPath':'E:/IndependentBuild/Linux/ext4.vhdx','registryBasePath':'E:/IndependentBuild/Linux','projectRootPath':'E:/IndependentBuild'}
                marker.write_text(json.dumps(other),encoding='utf-8');continuation.verify_storage(marker,root,root,'E:',now)
                with self.assertRaises(RuntimeError):continuation.verify_storage(marker,root,root,'D:',now)
                for mutate in (lambda value:value.update(windowsBackingPath='C:/Docker'),
                               lambda value:value.update(registryBackingPath='C:/DifferentRegisteredDistro'),
                               lambda value:value.update(registryBasePath='D:/OtherRegisteredDistro'),
                               lambda value:value.update(approvedHostDrive='d:'),
                               lambda value:value.pop('maxProjectBytes'),
                               lambda value:value.update(hostAvailableBytes=1024),
                               lambda value:value.update(distro='other-project'),
                               lambda value:value.update(checkedAtUtc='2026-10-25T12:00:00Z')):
                    changed=copy.deepcopy(data);mutate(changed);marker.write_text(json.dumps(changed),encoding='utf-8')
                    with self.assertRaises(RuntimeError):continuation.verify_storage(marker,root,root,'D:',now)
                marker.write_text(json.dumps(data),encoding='utf-8')
                for drive in (None,'D','d:','D:/'):
                    with self.assertRaises(RuntimeError):continuation.verify_storage(marker,root,root,drive,now)

    def test_storage_rejects_redirected_or_linked_attestation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);marker=root/'storage.json';marker.write_text('{}',encoding='utf-8')
            import os
            alias=root/'duplicate.json';os.link(marker,alias)
            with self.assertRaises(RuntimeError):storage.read_attestation(marker)
            alias.unlink()
            original=Path.is_symlink
            with patch.object(Path,'is_symlink',autospec=True,side_effect=lambda value: value==marker or original(value)):
                with self.assertRaises(RuntimeError):storage.read_attestation(marker)
            redirected=root/'redirected'
            with patch.object(Path,'is_symlink',autospec=True,side_effect=lambda value: value==redirected or original(value)):
                with self.assertRaises(RuntimeError):storage.check_build_storage((redirected/'new-build',))

    def test_native_linux_storage_needs_no_windows_attestation(self):
        with tempfile.TemporaryDirectory() as temporary:
            target=Path(temporary)/'new-build'
            with patch.object(storage,'is_wsl',return_value=False),patch.object(storage.shutil,'disk_usage',return_value=SimpleNamespace(free=128*1024**3,used=16*1024**3)),patch.object(storage.subprocess,'check_output') as findmnt:
                self.assertEqual(storage.check_build_storage((target,))['mode'],'native-linux')
                findmnt.assert_not_called();self.assertFalse(target.exists())
            with patch.object(storage,'is_wsl',return_value=False),patch.object(storage.shutil,'disk_usage',return_value=SimpleNamespace(free=1024)):
                with self.assertRaises(RuntimeError):storage.check_build_storage((target,))

    def test_custom_wsl_kernel_still_requires_host_check_and_parent_paths_are_refused(self):
        with patch.object(storage.platform,'release',return_value='custom-linux-kernel'),patch.dict('os.environ',{'WSL_DISTRO_NAME':'DriftersToolkitBuild'},clear=True):
            self.assertTrue(storage.is_wsl())
        with patch.object(storage.platform,'release',return_value='ordinary-linux-kernel'),patch.dict('os.environ',{},clear=True):
            self.assertFalse(storage.is_wsl())
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'missing'/'..'/'..'/'outside'
            with self.assertRaises(RuntimeError):storage.plain_directory_target(path)
            self.assertFalse((Path(temporary)/'missing').exists())

    def test_fresh_wsl_recipe_checks_host_storage_before_any_growth(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);sdk=root/'sdk';sdk.mkdir();archive=root/'source.tar.xz';archive.write_bytes(b'not-extracted')
            work=root/'new-work';prefix=root/'new-sdk'
            with patch.object(storage,'is_wsl',return_value=True),patch.object(security,'unpack') as unpack,patch.object(security.shutil,'copytree') as clone:
                with self.assertRaises(RuntimeError):security.build(archive,sdk,work,prefix)
                unpack.assert_not_called();clone.assert_not_called()
                self.assertFalse(work.exists());self.assertFalse(prefix.exists())

    def test_resume_configuration_refuses_stale_paths_or_unbounded_nested_ninja(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);work=root/'work';sdk=root/'sdk';output=work/'build'
            (work/'source').mkdir(parents=True);(sdk/'lib/cmake/Qt6').mkdir(parents=True);(output/'src/core/Release/x86_64').mkdir(parents=True)
            flags='-march=x86-64-v2 -mtune=generic -fstack-protector-strong -ffile-prefix-map='+str(work)+'=/usr/src/drifters-qt-webengine'
            values={'CMAKE_HOME_DIRECTORY':str(work/'source'),'CMAKE_INSTALL_PREFIX':str(sdk),'CMAKE_BUILD_TYPE':'Release',
                    'CMAKE_C_COMPILER':'/usr/bin/clang-19','CMAKE_CXX_COMPILER':'/usr/bin/clang++-19',
                    'CMAKE_C_FLAGS':flags,'CMAKE_CXX_FLAGS':flags,'CMAKE_EXE_LINKER_FLAGS':'-fuse-ld=lld-19',
                    'CMAKE_SHARED_LINKER_FLAGS':'-fuse-ld=lld-19','QT_BUILD_EXAMPLES':'OFF','QT_BUILD_TESTS':'OFF',
                    'QT_FEATURE_webengine_jumbo_build':'ON','QT_FEATURE_webengine_system_libwebp':'OFF','Qt6_DIR':str(sdk/'lib/cmake/Qt6')}
            values['FEATURE_webengine_printing_and_pdf']='ON'
            cache=output/'CMakeCache.txt'
            def write_cache():cache.write_text('\n'.join(key+':STRING='+value for key,value in values.items()),encoding='utf-8')
            write_cache()
            gn={'is_debug':'false','is_official_build':'true','is_unsafe_developer_build':'false','is_clang':'true','enable_rust':'false',
                'use_jumbo_build':'true','jumbo_file_merge_limit':'8','use_sysroot':'false','use_system_libwebp':'false',
                'use_system_icu':'false','use_custom_libcxx':'false','proprietary_codecs':'false','symbol_level':'0',
                'custom_toolchain':json.dumps(str(output/'src/core/target_toolchain')+':target'),
                'host_toolchain':json.dumps(str(output/'src/core/host_toolchain')+':host')}
            (output/'src/core/Release/x86_64/args.gn').write_text('\n'.join(key+'='+value for key,value in gn.items())+'\nfatal_linker_warnings=false\nfatal_linker_warnings=false\n',encoding='utf-8')
            ninja=output/'build.ninja';ninja.write_text('  COMMAND = /usr/bin/ninja -j 2 -C /retained/QtWebEngineCore QtWebEngineCore\n',encoding='utf-8')
            continuation.verify_configuration(work,sdk)
            ninja.write_text('  COMMAND = /usr/bin/ninja -C /retained/QtWebEngineCore QtWebEngineCore\n',encoding='utf-8')
            with self.assertRaises(RuntimeError):continuation.verify_configuration(work,sdk)
            ninja.write_text('  COMMAND = /usr/bin/ninja -j 2 -C /retained/QtWebEngineCore QtWebEngineCore\n',encoding='utf-8')
            values['CMAKE_INSTALL_PREFIX']='/stale/prefix';write_cache()
            with self.assertRaises(RuntimeError):continuation.verify_configuration(work,sdk)

    def test_resume_state_write_failure_terminates_owned_process_group(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            from unittest.mock import Mock
            child=Mock(pid=12345);child.wait.return_value=0
            with patch.object(continuation.subprocess,'Popen',return_value=child),patch.object(continuation.os,'killpg',create=True) as terminate,patch.object(Path,'write_text',side_effect=OSError('full build volume')):
                with self.assertRaises(OSError):continuation.run_phase(['not-executed'],root/'log',{},root/'state','fixture')
                terminate.assert_called_once_with(12345,continuation.signal.SIGTERM)
                child.wait.assert_called_once_with(timeout=20)

    def test_descendant_cleanup_keeps_pid_start_and_owner_identity(self):
        fields=['S','1','123','123']+['0']*16;fields[19]='original-start'
        rows={123:{'pid':123,'start':'original-start','uid':1001}}
        with patch.object(Path,'read_text',return_value='123 (clang) '+' '.join(fields)),patch.object(Path,'stat',return_value=SimpleNamespace(st_uid=1001)),patch.object(continuation.os,'kill',create=True) as kill:
            continuation.signal_owned(rows,continuation.signal.SIGTERM)
            kill.assert_called_once_with(123,continuation.signal.SIGTERM)
            kill.reset_mock();rows[123]['start']='reused-pid'
            continuation.signal_owned(rows,continuation.signal.SIGTERM);kill.assert_not_called()

    def test_scope_collector_does_not_claim_unbuilt_sdk_quick_module(self):
        with tempfile.TemporaryDirectory() as temporary:
            prefix=Path(temporary);self.fixture_proof(prefix)
            (prefix/'lib/libQt6WebEngineQuick.so.6.11.3').write_bytes(b'unbuilt original SDK wrapper')
            rows=security.runtime_outputs(prefix)
            self.assertNotIn('lib/libQt6WebEngineQuick.so.6.11.3',{row['file'] for row in rows})

    def test_resume_proof_uses_unique_temporary_and_preserves_existing_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);stale=root/(security.PROOF+'.continuation-tmp');stale.write_bytes(b'preserved interrupted attempt')
            target=continuation.publish_proof(root,{'fixture':'completed'})
            self.assertEqual(json.loads(target.read_text(encoding='utf-8')),{'fixture':'completed'})
            self.assertEqual(stale.read_bytes(),b'preserved interrupted attempt')
            before=target.read_bytes()
            with self.assertRaises(FileExistsError):continuation.publish_proof(root,{'replacement':'refused'})
            self.assertEqual(target.read_bytes(),before)
            self.assertFalse(list(root.glob('.qtwebengine-security-*')))

    def test_live_budget_catches_stale_vhd_length_and_actual_host_reserve(self):
        with tempfile.TemporaryDirectory() as temporary:
            target=Path(temporary)
            policy={'approvedHostDrive':'D:','projectRootPath':'D:/Build','windowsBackingPath':'D:/Build/Linux/ext4.vhdx',
                    'maxProjectBytes':80*1024**3,'baselineVhdBytes':20*1024**3,'baselineLinuxUsedBytes':18*1024**3}
            stale={'projectBytes':20*1024**3,'vhdFileBytes':20*1024**3,'otherProjectBytes':0,'entries':1}
            with patch.object(storage.shutil,'disk_usage',side_effect=[SimpleNamespace(free=128*1024**3,used=79*1024**3),SimpleNamespace(free=128*1024**3,used=0)]),patch.object(storage,'project_usage',return_value=stale):
                with self.assertRaises(storage.BudgetStop) as stopped:storage.budget_snapshot((target,),policy)
                self.assertGreater(stopped.exception.snapshot['estimatedVhdBytes'],80*1024**3)
            with patch.object(storage.shutil,'disk_usage',side_effect=[SimpleNamespace(free=128*1024**3,used=18*1024**3),SimpleNamespace(free=63*1024**3,used=0)]),patch.object(storage,'project_usage',return_value=stale):
                with self.assertRaises(storage.BudgetStop) as stopped:storage.budget_snapshot((target,),policy)
                self.assertIn('host-drive reserve',str(stopped.exception))
            with patch.object(storage.shutil,'disk_usage',return_value=SimpleNamespace(free=128*1024**3,used=18*1024**3)),patch.object(storage,'project_usage',return_value={**stale,'otherProjectBytes':62*1024**3,'projectBytes':82*1024**3}):
                with self.assertRaises(storage.BudgetStop):storage.budget_snapshot((target,),policy)

    def test_project_metadata_inventory_counts_only_approved_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);project=root/'project';project.mkdir();vhd=project/'ext4.vhdx';vhd.write_bytes(b'vhd fixture')
            (project/'transfer.tar').write_bytes(b'leftover transfer');(root/'unrelated-game.bin').write_bytes(b'unrelated game stays outside inventory')
            usage=storage.project_usage(project,vhd)
            self.assertEqual(usage['entries'],2);self.assertEqual(usage['vhdFileBytes'],11)
            self.assertGreaterEqual(usage['projectBytes'],28)

    def test_missing_live_metadata_stops_instead_of_using_stale_attestation(self):
        monitor=object.__new__(storage.RuntimeBudget);monitor.targets=();monitor.attestation={};monitor.peak_vhd=0;monitor.receipt_path=None;monitor.history=[]
        with patch.object(storage,'budget_snapshot',side_effect=OSError('VHD metadata unavailable')):
            with self.assertRaises(storage.BudgetStop):monitor.check()
        self.assertEqual(monitor.history[-1]['status'],'budget-check-unavailable')

    def test_budget_stop_terminates_owned_group_and_records_reason(self):
        from unittest.mock import Mock
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);child=Mock(pid=12345);child.wait.return_value=0
            monitor=Mock();monitor.check.side_effect=[{},storage.BudgetStop('project budget exceeded',{'status':'budget-stopped'})]
            with patch.object(continuation.subprocess,'Popen',return_value=child),patch.object(continuation.os,'killpg',create=True) as terminate,patch.object(continuation.time,'monotonic',side_effect=[0,6]):
                with self.assertRaises(storage.BudgetStop):continuation.run_phase(['not-executed'],root/'log',{},root/'state','fixture',monitor)
                terminate.assert_called_once_with(12345,continuation.signal.SIGTERM)
                self.assertEqual(json.loads((root/'state').read_text(encoding='utf-8'))['phase'],'budget-stopped')

    def test_memory_reserve_prevents_spawn_and_missing_live_value_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            with patch.object(Path,'read_text',return_value='MemAvailable: 3145728 kB\n'),patch.object(continuation.subprocess,'Popen') as spawn:
                with self.assertRaises(continuation.MemoryStop):continuation.run_phase(['not-executed'],root/'log',{},root/'state','fixture',memory_guard=True)
                spawn.assert_not_called()
            with patch.object(Path,'read_text',return_value='MemTotal: 16377756 kB\n'):
                with self.assertRaises(RuntimeError):continuation.check_memory()

    def test_memory_stop_terminates_owned_tree_and_records_reason(self):
        from unittest.mock import Mock
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);child=Mock(pid=12345);child.wait.return_value=0
            stopped=continuation.MemoryStop({'availableMemoryBytes':3*1024**3,'minimumAvailableMemoryBytes':4*1024**3})
            rows={12345:{'pid':12345,'start':'fixture','uid':1001}}
            with patch.object(continuation.subprocess,'Popen',return_value=child),patch.object(continuation,'owned_descendants',return_value=rows),patch.object(continuation,'signal_owned') as tree,patch.object(continuation.os,'killpg',create=True) as group,patch.object(continuation,'check_memory',side_effect=[{},stopped]),patch.object(continuation.time,'monotonic',side_effect=[0,6]):
                with self.assertRaises(continuation.MemoryStop):continuation.run_phase(['not-executed'],root/'log',{},root/'state','fixture',memory_guard=True)
                group.assert_called_once_with(12345,continuation.signal.SIGTERM)
                self.assertEqual(tree.call_args_list[0].args,(rows,continuation.signal.SIGTERM))
                self.assertEqual(json.loads((root/'state').read_text(encoding='utf-8'))['phase'],'memory-stopped')

    def test_fresh_recipe_allows_explicit_three_jobs_but_rejects_four(self):
        from contextlib import nullcontext
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);sdk=root/'sdk';sdk.mkdir();archive=root/'source.tar.xz';archive.write_bytes(b'fixture')
            with patch.object(storage,'check_build_storage',return_value={}),patch.object(storage,'project_build_lock',return_value=nullcontext()),patch.object(security,'_build_after_storage',return_value='bounded-three'):
                self.assertEqual(security.build(archive,sdk,root/'work',root/'prefix',3),'bounded-three')
                with self.assertRaises(RuntimeError):security.build(archive,sdk,root/'work',root/'prefix',4)

    def test_one_project_owner_rejects_second_compiler_group(self):
        from unittest.mock import Mock
        fake=SimpleNamespace(LOCK_EX=1,LOCK_NB=2,LOCK_UN=4,flock=Mock(side_effect=BlockingIOError('already locked')))
        with tempfile.TemporaryDirectory() as temporary,patch.dict(sys.modules,{'fcntl':fake}):
            with self.assertRaises(RuntimeError):
                with storage.project_build_lock({'projectScope':'approved-project'},temporary):self.fail('Second group must not enter.')


if __name__=='__main__':unittest.main()
