"""Offline source-companion, pinned SDK and library-replacement policy fixtures."""
from pathlib import Path
import hashlib
import json
import os
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import portable_materials as materials
import package_portable_linux as portable
import bootstrap_qt_sources as qt_sources
import bootstrap_openssl as openssl

class Materials(unittest.TestCase):
    def setUp(self):
        self.owner=tempfile.TemporaryDirectory(prefix='drifter-source-policy-');self.root=Path(self.owner.name)
    def tearDown(self):self.owner.cleanup()
    def write(self,name,body=b'fixture'):
        path=self.root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(body);return path
    def lock(self,folder='input'):
        base=self.root/folder;source=self.write(folder+'/upstream.tar.xz',b'intact upstream source archive')
        data={'schemaVersion':1,'files':[materials.row(base,source.name,'qtbase','6.11.3','https://download.qt.io/fixture')]}
        path=base/'source-materials.json';path.write_text(json.dumps(data));return path,data
    def test_intact_source_companion_and_exclusive_publication(self):
        lock,data=self.lock();app=self.write('app.zip',b'checked application source');instructions=self.write('BUILD.txt',b'rebuild instructions')
        output=self.root/'corresponding-source.tar';descriptor=materials.pack([lock],app,instructions,output)
        self.assertEqual(descriptor['sha256'],hashlib.sha256(output.read_bytes()).hexdigest())
        with tarfile.open(output) as archive:
            self.assertEqual(archive.extractfile('materials-0/upstream.tar.xz').read(),b'intact upstream source archive')
            self.assertEqual(archive.extractfile('application/Toolkit-Source.zip').read(),app.read_bytes())
            self.assertIn('scripts/generate_portable_manifest.py',archive.getnames())
            self.assertIn('scripts/package_portable_linux.py',archive.getnames())
            manifest=json.load(archive.extractfile('SOURCE-MATERIALS.json'))
            self.assertIn('library-replacement',manifest['components'])
        before=output.read_bytes()
        with self.assertRaises(RuntimeError):materials.pack([lock],app,instructions,output)
        self.assertEqual(output.read_bytes(),before)
    def test_source_hash_alias_bounds_and_required_component(self):
        lock,data=self.lock();self.assertEqual(materials.verify_lock(lock,('qtbase',))['schemaVersion'],1)
        with self.assertRaises(RuntimeError):materials.verify_lock(lock,('qtwebengine',))
        target=lock.parent/'upstream.tar.xz';original=target.read_bytes();target.write_bytes(original+b'changed')
        with self.assertRaises(RuntimeError):materials.verify_lock(lock)
        target.write_bytes(original)
        with self.assertRaises(RuntimeError):materials.identity(target,1)
        alias=self.root/'hardlink';os.link(target,alias)
        with self.assertRaises(RuntimeError):materials.identity(target)
        alias.unlink()
        duplicate={**data,'files':data['files']*2};lock.write_text(json.dumps(duplicate))
        with self.assertRaises(RuntimeError):materials.verify_lock(lock)
        data['files'][0]['file']='../outside';lock.write_text(json.dumps(data))
        with self.assertRaises(RuntimeError):materials.verify_lock(lock)
    def test_runtime_requires_actual_sources_licenses_and_recipe(self):
        runtime=self.write('runtime-x86_64',b'ELF fixture');self.write('sources/fuse.tar.gz',b'actual fuse source')
        self.write('licenses/LGPL.txt',b'license fixture');recipe=self.write('recipe.tar.gz',b'complete recipe fixture')
        data={'schemaVersion':1,'runtime':{'file':runtime.name,**materials.identity(runtime)},
              'sources':[materials.row(self.root,'sources/fuse.tar.gz','fuse')],
              'licenses':[materials.row(self.root,'licenses/LGPL.txt','fuse')],
              'recipe':{'file':recipe.name,**materials.identity(recipe)}}
        proof=self.root/'runtime-provenance.json';proof.write_text(json.dumps(data));materials.verify_runtime(proof,runtime)
        for key in ('sources','licenses','recipe'):
            invalid=dict(data);invalid.pop(key);proof.write_text(json.dumps(invalid))
            with self.assertRaises(RuntimeError):materials.verify_runtime(proof,runtime)
        proof.write_text(json.dumps(data));runtime.write_bytes(b'changed runtime')
        with self.assertRaises(RuntimeError):materials.verify_runtime(proof,runtime)
    def test_maintained_sdk_pin_and_prefix(self):
        prefix=self.root/'sdk';(prefix/'bin').mkdir(parents=True);(prefix/'bin/qmake').write_bytes(b'fixture qmake')
        for folder in ('plugins','libexec','data','translations'):(prefix/folder).mkdir()
        archive=self.write('archives/module.7z',b'verified SDK archive');data={'schemaVersion':1,'qtVersion':'6.11.3','archives':[{'name':archive.name,'url':'https://download.qt.io/fixture.7z',**materials.identity(archive)}]}
        proof=self.root/'sdk.json';proof.write_text(json.dumps(data))
        values={'QT_VERSION':'6.11.3','QT_INSTALL_PLUGINS':str(prefix/'plugins'),'QT_INSTALL_LIBEXECS':str(prefix/'libexec'),'QT_INSTALL_DATA':str(prefix/'data'),'QT_INSTALL_TRANSLATIONS':str(prefix/'translations')}
        with patch.object(portable,'run',side_effect=lambda *args:values[args[-1]]):
            # Correct upstream archive pins alone cannot waive the updated-engine
            # gate; this SDK fixture intentionally has no completed build proof.
            with self.assertRaises((RuntimeError,FileNotFoundError)):portable.qt_inputs(prefix,proof,archive.parent)
            with patch('qt_webengine_vendor_followup.verify_build',return_value={}),patch('qt_webengine_vendor_followup.public_provenance',return_value={'fixture':True}):
                sdk,paths,record=portable.qt_inputs(prefix,proof,archive.parent);self.assertEqual(record['version'],'6.11.3')
                self.assertEqual(record['webengineSecurity'],{'fixture':True})
            archive.write_bytes(b'modified')
            with self.assertRaises(RuntimeError):portable.qt_inputs(prefix,proof,archive.parent)
        data['qtVersion']='6.4.2';proof.write_text(json.dumps(data))
        with self.assertRaises(RuntimeError):portable.qt_inputs(prefix,proof,archive.parent)
    def test_notices_preserved_without_runtime_readmes(self):
        source=self.root/'notices';source.mkdir();(source/'LICENSE.txt').write_text('upstream licence text')
        target=self.root/'AppDir/licenses/qt';records=portable.copy_qt_notices(source,target)
        self.assertEqual(records[0]['path'],'licenses/qt/LICENSE.txt')
        self.assertEqual((target/'LICENSE.txt').read_text(),'upstream licence text')
        (source/'README.md').write_text('upstream source documentation belongs in companion')
        with self.assertRaises(RuntimeError):portable.copy_qt_notices(source,self.root/'another/licenses/qt')
    def test_replacement_reindex_changes_identity_not_paths_or_cli_roles(self):
        app=self.root/'AppDir'
        for name in ('usr/bin/qt.conf','usr/libexec/qt.conf','usr/libexec/QtWebEngineProcess','usr/lib/fakeQt.so'):
            self.write('AppDir/'+name,b'original fixture runtime')
        before=portable.inventory(app);(app/'runtime-manifest.json').write_text(json.dumps(before))
        library=app/'usr/lib/fakeQt.so';library.write_bytes(b'modified fixture runtime')
        with self.assertRaises(RuntimeError):portable.verify_inventory(app)
        current=portable.reindex(app);self.assertTrue(current['locallyModified'])
        self.assertEqual({row['path'] for row in before['files']},{row['path'] for row in current['files']})
        self.assertNotEqual(before['files'],current['files']);portable.verify_inventory(app)
        original=(app/'runtime-manifest.json').read_bytes();self.write('AppDir/usr/lib/injected.so')
        with self.assertRaises(RuntimeError):portable.reindex(app)
        self.assertEqual((app/'runtime-manifest.json').read_bytes(),original)

    def complete_companion(self, missing=()):
        base=self.root/'complete';records=[];pins=[]
        for component in (*qt_sources.QT_MODULES,'icu'):
            name=component+'.tar.xz';body=('complete source fixture '+component).encode();self.write('complete/'+name,body)
            algorithm='sha512' if component=='icu' else 'sha256';version='73.2' if component=='icu' else '6.11.3'
            url='https://download.qt.io/'+name
            pins.append({'component':component,'name':name,'version':version,'url':url,'size':len(body),'algorithm':algorithm,'digest':hashlib.new(algorithm,body).hexdigest()})
            if component not in missing:records.append(materials.row(base,name,component,version,url))
        body=b'complete pinned OpenSSL source fixture';name='openssl-3.5.9.tar.gz';self.write('complete/'+name,body)
        ssl={'name':name,'url':'https://www.openssl.org/source/'+name,'sha256':hashlib.sha256(body).hexdigest(),'size':len(body)}
        if 'openssl' not in missing:records.append(materials.row(base,name,'openssl','3.5.9',ssl['url']))
        for name in ('sample_1.dsc','sample_1.tar.xz'):
            self.write('complete/'+name,b'complete exact Debian fixture '+name.encode())
            records.append(materials.row(base,name,'debian:sample','1','APT authenticated source=sample=1'))
        lock=base/'source-materials.json';lock.write_text(json.dumps({'schemaVersion':1,'files':records}))
        application=self.write('app.zip',b'complete app fixture');instructions=self.write('BUILD.txt',b'build and replace libraries fixture')
        output=self.root/'complete-source.tar';descriptor=materials.pack([lock],application,instructions,output)
        from bootstrap_archive import public_provenance
        from bootstrap_qt_webengine_security import load_lock as webengine_lock, runtime_scope, source_inventory_identity
        webengine=webengine_lock();security={'recipeRevision':webengine['recipeRevision'],'patchedChromiumCommit':webengine['patchedChromiumCommit'],
            'securityLock':materials.identity(ROOT/'scripts/qt_webengine_security_lock.json'),
            'recipe':materials.identity(ROOT/'scripts/bootstrap_qt_webengine_security.py'),
            'storageHelper':materials.identity(ROOT/'scripts/linux_build_storage.py'),
            'runtimeScope':runtime_scope(),
            'compatibilityBackports':webengine['compatibilityBackports'],'sourceInventories':source_inventory_identity(),
            'patches':[{key:row[key] for key in ('commit','sha256','size')} for row in webengine['patches']]}
        import qt_webengine_followup as followup
        outputs=[{'file':name,'sha256':'a'*64,'size':1} for name in ('lib/libQt6WebEngineCore.so.6.11.3','lib/libQt6WebEngineWidgets.so.6.11.3','libexec/QtWebEngineProcess','translations/qtwebengine_locales/en-US.pak',*('resources/'+name for name in __import__('bootstrap_qt_webengine_security').RUNTIME_RESOURCES))]
        security['outputs']=outputs
        prior_outputs=[{**row,'sha256':'b'*64} for row in outputs]
        security['upstreamFollowup']={**followup.identity(followup.verify_inputs()),'outputs':prior_outputs}
        import qt_webengine_incremental as incremental
        incremental_outputs=[{**row,'sha256':'d'*64} for row in outputs]
        security['incrementalDelta']={**incremental.identity(incremental.verify_inputs()),'outputs':incremental_outputs,
                                     'priorOutputs':prior_outputs,'baselineProof':{'sha256':'c'*64,'size':15699}}
        import qt_webengine_residual as residual
        residual_outputs=[{**row,'sha256':'f'*64} for row in outputs]
        security['residualDelta']={**residual.identity(residual.verify_inputs()),'outputs':residual_outputs,
                                  'priorOutputs':incremental_outputs,'baselineProof':{'sha256':'e'*64,'size':36560}}
        import qt_webengine_vendor_followup as vendor_followup
        security['vendorFollowup']={**vendor_followup.identity(vendor_followup.verify_inputs()),'outputs':outputs,
                                   'priorOutputs':residual_outputs,'baselineProof':{'sha256':'1'*64,'size':44626}}
        provenance={'qt':{'version':'6.11.3','webengineSecurity':security},'openssl':{'version':'3.5.9','source':ssl},'archiveDecoders':public_provenance(),'packages':[{'sourcePackage':'sample','sourceVersion':'1'}]}
        return output,descriptor,provenance,{'schemaVersion':1,'qtVersion':'6.11.3','archives':pins},ssl

    def test_complete_source_closure_is_verified_from_actual_tar_members(self):
        output,descriptor,provenance,pinned,ssl=self.complete_companion()
        with patch.object(qt_sources,'source_lock',return_value=pinned),patch.object(openssl,'SOURCE',ssl):
            manifest=materials.verify_companion(output,descriptor,provenance)
            self.assertIn('qtwebengine',manifest['components']);self.assertIn('openssl',manifest['components'])
            self.assertIn('scripts/qt_sources_lock.json',{entry['file'] for entry in manifest['files']})
            self.assertIn('scripts/bootstrap_openssl.py',{entry['file'] for entry in manifest['files']})
            self.assertIn('scripts/release_signing.py',{entry['file'] for entry in manifest['files']})
            self.assertIn('scripts/release_key.json',{entry['file'] for entry in manifest['files']})
            self.assertIn('archive-decoder:libarchive',manifest['components'])
            self.assertIn('scripts/archive_decoder_limits.h',{entry['file'] for entry in manifest['files']})
            self.assertIn('scripts/qt_webengine_security_lock.json',{entry['file'] for entry in manifest['files']})
            self.assertIn('qtwebengine-security-patches',manifest['components'])
            from bootstrap_qt_webengine_security import load_lock,all_patch_inputs
            current=load_lock()
            for row in all_patch_inputs(current):self.assertIn(current['patchDirectory']+'/'+row['name'],{entry['file'] for entry in manifest['files']})
            self.assertIn('scripts/qt_webengine_final_files.json',{entry['file'] for entry in manifest['files']})
            self.assertIn('scripts/qt_webengine_vendor_followup.py',{entry['file'] for entry in manifest['files']})
            self.assertIn('scripts/qt_webengine_vendor_followup_lock.json',{entry['file'] for entry in manifest['files']})
            for mutate in (lambda value:value.update(recipeRevision=1),lambda value:value.pop('compatibilityBackports'),lambda value:value['sourceInventories'].pop('final'),lambda value:value.pop('upstreamFollowup'),lambda value:value['upstreamFollowup']['upstream'].pop(),lambda value:value['upstreamFollowup']['lock'].update(sha256='0'*64)):
                stale=json.loads(json.dumps(provenance));mutate(stale['qt']['webengineSecurity'])
                with self.assertRaises(RuntimeError):materials.verify_companion(output,descriptor,stale)
            for mutate in (lambda value:value.pop('vendorFollowup'),
                           lambda value:value['vendorFollowup']['upstream'].pop(),
                           lambda value:value['vendorFollowup']['lock'].update(sha256='0'*64),
                           lambda value:value['vendorFollowup']['priorOutputs'][0].update(sha256='0'*64),
                           lambda value:value['vendorFollowup']['outputs'][0].update(sha256='0'*64)):
                stale=json.loads(json.dumps(provenance));mutate(stale['qt']['webengineSecurity'])
                with self.assertRaises(RuntimeError):materials.verify_companion(output,descriptor,stale)
            stale=json.loads(json.dumps(provenance));stale['qt'].pop('webengineSecurity')
            with self.assertRaises(RuntimeError):materials.verify_companion(output,descriptor,stale)
            stale=json.loads(json.dumps(provenance));stale['qt']['webengineSecurity']['runtimeScope']['targets'].append('Pdf')
            with self.assertRaises(RuntimeError):materials.verify_companion(output,descriptor,stale)
            stale=json.loads(json.dumps(provenance));stale['qt']['version']='6.11.2'
            with self.assertRaises(RuntimeError):materials.verify_companion(output,descriptor,stale)
            stale=json.loads(json.dumps(provenance));stale['packages'][0]['sourceVersion']='2'
            with self.assertRaises(RuntimeError):materials.verify_companion(output,descriptor,stale)
            forged={**descriptor,'components':descriptor['components']+['invented-source']}
            with self.assertRaises(RuntimeError):materials.verify_companion(output,forged,provenance)
            wrong_runtime={'sources':[{'file':'missing.tar','component':'fuse','sha256':'a'*64,'size':1}],'recipe':{}}
            with self.assertRaises(RuntimeError):materials.verify_companion(output,descriptor,provenance,wrong_runtime)
            # Even a recomputed outer descriptor cannot hide a new unlisted TAR member.
            extra=self.root/'extra-source.tar'
            with tarfile.open(output) as source,tarfile.open(extra,'w') as target:
                for member in source:target.addfile(member,source.extractfile(member))
                import io
                member=tarfile.TarInfo('unlisted.txt');member.size=1;target.addfile(member,io.BytesIO(b'x'))
            extra_descriptor={**descriptor,'file':extra.name,**materials.identity(extra)}
            with self.assertRaises(RuntimeError):materials.verify_companion(extra,extra_descriptor,provenance)
            # Removing both a required reference and its inventory row still fails.
            omitted=current['patchDirectory']+'/'+current['compatibilityBackports'][0]['upstream'][0]['name']
            missing=self.root/'missing-primary-reference.tar'
            with tarfile.open(output) as source,tarfile.open(missing,'w') as target:
                for member in source:
                    if member.name==omitted:continue
                    if member.name=='SOURCE-MATERIALS.json':
                        body=json.loads(source.extractfile(member).read());body['files']=[entry for entry in body['files'] if entry['file']!=omitted]
                        encoded=(json.dumps(body,indent=2)+'\n').encode();member.size=len(encoded);target.addfile(member,io.BytesIO(encoded))
                    else:target.addfile(member,source.extractfile(member))
            missing_descriptor={**descriptor,'file':missing.name,**materials.identity(missing)}
            with self.assertRaises(RuntimeError):materials.verify_companion(missing,missing_descriptor,provenance)
            # The new adapted repair cannot disappear together with its row.
            import qt_webengine_followup as followup
            followup_data=followup.load_lock();omitted=followup_data['patchDirectory']+'/'+followup_data['adapter']['name']
            missing=self.root/'missing-followup-adapter.tar'
            with tarfile.open(output) as source,tarfile.open(missing,'w') as target:
                for member in source:
                    if member.name==omitted:continue
                    if member.name=='SOURCE-MATERIALS.json':
                        body=json.loads(source.extractfile(member).read());body['files']=[entry for entry in body['files'] if entry['file']!=omitted]
                        encoded=(json.dumps(body,indent=2)+'\n').encode();member.size=len(encoded);target.addfile(member,io.BytesIO(encoded))
                    else:target.addfile(member,source.extractfile(member))
            missing_descriptor={**descriptor,'file':missing.name,**materials.identity(missing)}
            with self.assertRaises(RuntimeError):materials.verify_companion(missing,missing_descriptor,provenance)

    def test_claimed_source_components_cannot_replace_missing_archives(self):
        output,descriptor,provenance,pinned,ssl=self.complete_companion(missing=('qtwebengine','openssl'))
        with patch.object(qt_sources,'source_lock',return_value=pinned),patch.object(openssl,'SOURCE',ssl):
            with self.assertRaises(RuntimeError):materials.verify_companion(output,descriptor,provenance)

    def test_incremental_inputs_cannot_disappear_with_their_manifest_rows(self):
        import io
        import qt_webengine_incremental as incremental
        import qt_webengine_residual as residual
        import qt_webengine_vendor_followup as vendor_followup
        output,descriptor,provenance,pinned,ssl=self.complete_companion()
        with patch.object(qt_sources,'source_lock',return_value=pinned),patch.object(openssl,'SOURCE',ssl):
            manifest=materials.verify_companion(output,descriptor,provenance)
            for label,module in (('incremental',incremental),('residual',residual),('vendor-followup',vendor_followup)):
                self.assertIn('qtwebengine-'+label+'-security',manifest['components'])
                data=module.verify_inputs()
                for index,row in enumerate((data['upstream'][-1],data['adapter'])):
                    omitted=data['patchDirectory']+'/'+row['name']
                    missing=self.root/('missing-'+label+'-'+str(index)+'.tar')
                    with tarfile.open(output) as source,tarfile.open(missing,'w') as target:
                        for member in source:
                            if member.name==omitted:continue
                            if member.name=='SOURCE-MATERIALS.json':
                                body=json.loads(source.extractfile(member).read())
                                body['files']=[entry for entry in body['files'] if entry['file']!=omitted]
                                encoded=(json.dumps(body,indent=2)+'\n').encode();member.size=len(encoded)
                                target.addfile(member,io.BytesIO(encoded))
                            else:target.addfile(member,source.extractfile(member))
                    missing_descriptor={**descriptor,'file':missing.name,**materials.identity(missing)}
                    with self.assertRaisesRegex(RuntimeError,label):
                        materials.verify_companion(missing,missing_descriptor,provenance)

    def test_current_qt_sources_lock_matches_sdk_and_rejects_changed_policy(self):
        pinned=qt_sources.source_lock()
        self.assertEqual(pinned['qtVersion'],'6.11.3');self.assertEqual(len(pinned['archives']),10)
        self.assertTrue(all('online/' in entry['url'] for entry in pinned['archives'] if entry['component']!='icu'))
        lock=self.root/'qt_sources_lock.json'
        for mutate in (lambda data:data.update(qtVersion='6.11.2'),
                       lambda data:data['archives'].pop(),
                       lambda data:data['archives'].append(dict(data['archives'][0])),
                       lambda data:data['archives'][0].update(url='https://evil.invalid/source.tar.xz'),
                       lambda data:data['archives'][0].update(digest='invalid'),
                       lambda data:data['archives'][0].update(size=True)):
            changed=json.loads(json.dumps(pinned));mutate(changed);lock.write_text(json.dumps(changed))
            with self.assertRaises(RuntimeError):qt_sources.source_lock(lock)

    def test_source_companion_rejects_tar_links_metadata_and_trailing_bytes(self):
        import io
        for kind in ('symlink','metadata','trailing'):
            path=self.root/(kind+'.tar')
            with tarfile.open(path,'w',format=tarfile.PAX_FORMAT) as archive:
                member=tarfile.TarInfo('safe.txt')
                if kind=='symlink':member.type=tarfile.SYMTYPE;member.linkname='../outside';archive.addfile(member)
                else:
                    member.size=1
                    if kind=='metadata':member.pax_headers={'comment':'x'*70000}
                    archive.addfile(member,io.BytesIO(b'x'))
            if kind=='trailing':
                with path.open('ab') as output:output.write(b'unlisted trailing content')
            descriptor={'file':path.name,**materials.identity(path),'components':[]}
            with self.assertRaises(RuntimeError):materials.verify_companion(path,descriptor,{})

    def test_existing_source_sidecars_are_never_overwritten(self):
        lock,_=self.lock();app=self.write('app.zip');instructions=self.write('BUILD.txt')
        for suffix in ('.json','.sha256'):
            output=self.root/('preserve-'+suffix[1:]+'.tar');sidecar=Path(str(output)+suffix)
            sidecar.write_bytes(b'preserve existing sidecar')
            with self.assertRaises(RuntimeError):materials.pack([lock],app,instructions,output)
            self.assertEqual(sidecar.read_bytes(),b'preserve existing sidecar');self.assertFalse(output.exists())
        if os.name!='nt':
            output=self.root/'linked.tar';unrelated=self.write('unrelated.txt',b'never overwrite unrelated bytes')
            Path(str(output)+'.json').symlink_to(unrelated)
            with self.assertRaises(RuntimeError):materials.pack([lock],app,instructions,output)
            self.assertEqual(unrelated.read_bytes(),b'never overwrite unrelated bytes');self.assertFalse(output.exists())

    def test_companion_keeps_one_open_identity_through_tar_validation(self):
        output,descriptor,provenance,pinned,ssl=self.complete_companion()
        original=materials._companion_members
        def change_after_read(archive,stream):
            result=original(archive,stream)
            # Replace the path with different bytes after parsing. A new outer
            # descriptor cannot authorize a swap during this verification.
            changed=output.with_suffix('.replacement');changed.write_bytes(b'changed after parsing')
            try:os.replace(changed,output)
            except PermissionError:
                # Windows can deny a rename of an open file; its observed
                # last-write time still detects a writer on the same object.
                stamp=output.stat();os.utime(output,ns=(stamp.st_atime_ns,stamp.st_mtime_ns+1000000000))
            return result
        with patch.object(qt_sources,'source_lock',return_value=pinned),patch.object(openssl,'SOURCE',ssl),patch.object(materials,'_companion_members',side_effect=change_after_read):
            with self.assertRaises(RuntimeError):materials.verify_companion(output,descriptor,provenance)

if __name__=='__main__':unittest.main(verbosity=2)
