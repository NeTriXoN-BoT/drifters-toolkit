"""Offline binding of rebuilt WebEngine bytes to loaded SONAMEs and bundles."""
from pathlib import Path
import copy
import hashlib
import json
import os
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import bootstrap_qt_webengine_security as base
import package_portable_linux as portable
import package_check as checker
import seal_portable_linux as seal


def identity(body):return {'sha256':hashlib.sha256(body).hexdigest(),'size':len(body)}


class RuntimeBindings(unittest.TestCase):
    def setUp(self):
        self.owner=tempfile.TemporaryDirectory(prefix='drifter-runtime-binding-')
        self.root=Path(self.owner.name);self.sdk=self.root/'sdk';self.app=self.root/'app'
        names=['lib/libQt6WebEngineCore.so.6.11.3','lib/libQt6WebEngineWidgets.so.6.11.3',
               'libexec/QtWebEngineProcess','translations/qtwebengine_locales/en-US.pak',
               *('resources/'+name for name in base.RUNTIME_RESOURCES)]
        self.outputs=[]
        for name in names:
            body=('repaired '+name).encode();self.write(self.sdk/name,body)
            self.outputs.append({'file':name,**identity(body)})
        self.targets=portable.rebuilt_runtime_targets(self.outputs);self.copied={}
    def tearDown(self):self.owner.cleanup()
    def write(self,path,body):
        path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(body);return path
    def stage(self):
        for name,row in self.targets.items():
            portable.copy_runtime(self.sdk/row['source'],self.app/name,name,self.sdk,self.targets,self.copied)
    def bundle(self):
        rows=portable.rebuilt_runtime_bindings(self.app,self.targets,self.copied)
        files={name:(self.app/name).read_bytes() for name in self.targets}
        expected={name:{'path':name,**identity(body)} for name,body in files.items()}
        qt={'webengineSecurity':{'outputs':self.outputs},'runtimeFiles':list(files),'rebuiltRuntimeFiles':rows}
        return qt,files,expected

    def test_old_soname_does_not_inherit_valid_canonical_proof(self):
        name='usr/lib/libQt6WebEngineCore.so.6'
        alias=self.write(self.sdk/'lib/libQt6WebEngineCore.so.6',b'old unpatched engine')
        before=(self.sdk/self.targets[name]['source']).read_bytes()
        with self.assertRaisesRegex(RuntimeError,'differs from the repaired'):
            portable.copy_runtime(alias,self.app/name,name,self.sdk,self.targets,self.copied,True)
        self.assertFalse((self.app/name).exists());self.assertEqual(self.copied,{})
        self.assertEqual((self.sdk/self.targets[name]['source']).read_bytes(),before)

    def test_soname_alias_is_allowed_only_with_repaired_in_prefix_bytes(self):
        name='usr/lib/libQt6WebEngineCore.so.6';canonical=self.sdk/self.targets[name]['source']
        alias=self.sdk/'lib/libQt6WebEngineCore.so.6'
        try:alias.symlink_to(canonical.name)
        except OSError as error:self.skipTest('Host does not permit symbolic-link fixtures: '+str(error))
        portable.copy_runtime(alias,self.app/name,name,self.sdk,self.targets,self.copied,True)
        self.assertFalse((self.app/name).is_symlink());self.assertEqual((self.app/name).read_bytes(),canonical.read_bytes())
        alias.unlink();outside=self.write(self.root/'outside.so',canonical.read_bytes());alias.symlink_to(outside)
        with self.assertRaisesRegex(RuntimeError,'differs from the repaired'):
            portable.copy_runtime(alias,self.app/'another',name,self.sdk,self.targets,{},True)

    def test_copied_bytes_are_verified_before_relocation(self):
        name='usr/lib/libQt6WebEngineWidgets.so.6'
        def bad_copy(source,target,executable):self.write(Path(target),b'changed while copying')
        with patch.object(portable,'copy',side_effect=bad_copy),self.assertRaisesRegex(RuntimeError,'changed while copying'):
            portable.copy_runtime(self.sdk/self.targets[name]['source'],self.app/name,name,self.sdk,self.targets,self.copied,True)
        self.assertEqual(self.copied,{})

    def test_complete_bindings_allow_elf_relocation_but_not_changed_resources(self):
        self.stage();name='usr/lib/libQt6WebEngineCore.so.6'
        self.write(self.app/name,b'repaired engine with staged RUNPATH')
        qt,files,expected=self.bundle();checker.verify_rebuilt_runtime_bindings(qt,files,expected)
        core=next(row for row in qt['rebuiltRuntimeFiles'] if row['path']==name)
        self.assertNotEqual(core['original'],core['staged'])
        self.write(self.app/'usr/resources/icudtl.dat',b'changed resources')
        with self.assertRaisesRegex(RuntimeError,'resource changed'):
            portable.rebuilt_runtime_bindings(self.app,self.targets,self.copied)

    def test_receipt_requires_exact_source_final_manifest_and_complete_unique_scope(self):
        self.stage();qt,files,expected=self.bundle()
        changes=(lambda d:d.pop('rebuiltRuntimeFiles'),lambda d:d['rebuiltRuntimeFiles'].pop(),
                 lambda d:d['rebuiltRuntimeFiles'].append(copy.deepcopy(d['rebuiltRuntimeFiles'][0])),
                 lambda d:d['rebuiltRuntimeFiles'][0].update(path='../outside'),
                 lambda d:d['rebuiltRuntimeFiles'][0].update(source='lib/old.so'),
                 lambda d:d['rebuiltRuntimeFiles'][0]['original'].update(sha256='0'*64),
                 lambda d:d['rebuiltRuntimeFiles'][0]['staged'].update(sha256='0'*64),
                 lambda d:d['rebuiltRuntimeFiles'][0].update(localReplacement=True))
        for change in changes:
            altered=copy.deepcopy(qt);change(altered)
            with self.assertRaises(RuntimeError):checker.verify_rebuilt_runtime_bindings(altered,files,expected)
        with self.assertRaisesRegex(RuntimeError,'omits a required'):
            portable.rebuilt_runtime_bindings(self.app,self.targets,{})

    def test_explicit_reindex_marks_local_replacement_and_preserves_source_history(self):
        self.stage();qt,files,expected=self.bundle()
        self.write(self.app/'usr/bin/qt.conf',b'[Paths]\nPrefix=..\n')
        self.write(self.app/'usr/libexec/qt.conf',b'[Paths]\nPrefix=..\n')
        manifest=portable.inventory(self.app)
        self.write(self.app/'runtime-manifest.json',json.dumps(manifest).encode())
        proof={'qt':qt};self.write(self.app/'DEPENDENCY-SOURCES.json',json.dumps(proof).encode())
        name='usr/lib/libQt6WebEngineCore.so.6';self.write(self.app/name,b'user modified library')
        with self.assertRaises(RuntimeError):portable.verify_inventory(self.app)
        current=portable.reindex(self.app);portable.verify_inventory(self.app)
        replacement=json.loads((self.app/'DEPENDENCY-SOURCES.json').read_text())['qt']
        binding=next(row for row in replacement['rebuiltRuntimeFiles'] if row['path']==name)
        self.assertTrue(binding['localReplacement']);self.assertTrue(current['locallyModified'])
        self.assertEqual(replacement['webengineSecurity'],qt['webengineSecurity'])
        files={key:(self.app/key).read_bytes() for key in self.targets}
        expected={row['path']:row for row in current['files']}
        checker.verify_rebuilt_runtime_bindings(replacement,files,expected,True)
        with self.assertRaisesRegex(RuntimeError,'not explicitly reindexed'):
            checker.verify_rebuilt_runtime_bindings(replacement,files,expected,False)

    def test_official_seal_refuses_local_replacements_before_native_programs(self):
        with patch.object(seal,'payload_digest',return_value='fixture'), \
             patch.object(seal,'refuse_private_save_preview'), \
             patch.object(portable,'verify_inventory',return_value={'locallyModified':True}), \
             patch.object(checker,'check_directory') as check,patch.object(portable,'run') as run:
            with self.assertRaisesRegex(RuntimeError,'cannot be sealed as an official'):
                seal.verify_seal_payload(self.app)
            check.assert_not_called();run.assert_not_called()


if __name__=='__main__':unittest.main(verbosity=2)
