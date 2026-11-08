"""Offline adversarial archive/manifest fixtures; never build or launch the GUI."""
from pathlib import Path
import hashlib
import io
import json
import os
import stat
import struct
import sys
import tempfile
import unittest
import zipfile
from types import SimpleNamespace
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import package_check as checker
import release_layout as layout
import generate_portable_manifest as generator
import build_fetch
import review_sources
import package as windows_packager
import package_portable_linux as linux_packager
import seal_portable_linux as linux_sealer
import release_signing


class PackagingSecurity(unittest.TestCase):
    @unittest.skipIf(os.name == 'nt', 'Unix file permissions apply to the Linux package.')
    def test_save_codec_metadata_and_notices_are_readable_by_other_accounts(self):
        helper = self.root / 'DriftersToolkitSaveCodec'
        helper.write_bytes(b'synthetic worker')
        appdir = self.root / 'AppDir'
        previous = os.umask(0o077)
        try:
            linux_packager.install_codec(appdir, (helper, b'synthetic worker', {},
                {'save-codec/COPYING': b'synthetic licence'}))
        finally:
            os.umask(previous)
        for path in (appdir / 'usr/bin/SAVE-CODEC-SOURCE.json', appdir / 'licenses/save-codec/COPYING'):
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o644)
        self.assertEqual(stat.S_IMODE((appdir / 'usr/bin/DriftersToolkitSaveCodec').stat().st_mode), 0o755)

    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='drifter-packaging-security-')
        self.root=Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_windows_packaging_rejects_private_hooks_and_wrong_reviewer_mode(self):
        for name in ('DriftersToolkit.exe','DriftersToolkitCLI.exe'):(self.root/name).write_bytes(b'fixture')
        for reviewer in (False,True):
            def runner(private=False,wrong_mode=False,write_private=False,editor_program=None):
                def run(command,**kwargs):
                    self.assertEqual(kwargs,{'capture_output':True,'timeout':20})
                    self.assertTrue('--verify-only'in command or '--reviewer-verify-only'in command or '--save-editor-development-verify-only'in command)
                    code=0
                    if '--interface-test-only'in command:
                        if write_private:Path(command[command.index('--interface-test-only')+1]).write_bytes(b'private hook')
                        code=0 if private else 1
                    elif '--reviewer-verify-only'in command:code=0 if reviewer!=wrong_mode else 1
                    elif '--save-editor-development-verify-only'in command:code=0 if Path(command[0]).name==editor_program else 1
                    return SimpleNamespace(returncode=code)
                return run
            windows_packager.verify_windows_runtime(self.root,reviewer,runner())
            for run in (runner(private=True),runner(wrong_mode=True),runner(write_private=True),
                        runner(editor_program='DriftersToolkit.exe'),runner(editor_program='DriftersToolkitCLI.exe')):
                with self.subTest(reviewer=reviewer),self.assertRaises(RuntimeError):
                    windows_packager.verify_windows_runtime(self.root,reviewer,run)

    def test_private_save_preview_rejected_before_packaging_side_effects(self):
        build=self.root/'build';build.mkdir()
        appdir=self.root/'AppDir';(appdir/'usr/bin').mkdir(parents=True)
        program=appdir/'usr/bin/DriftersToolkit';program.write_bytes(b'preserve original')
        destination=self.root/'must-not-be-created'
        for name in layout.PRIVATE_SAVE_PREVIEW_PROGRAMS:
            helper=build/name;helper.write_bytes(b'private decoder')
            with self.subTest(helper=name):
                with patch.object(windows_packager.subprocess,'run',side_effect=AssertionError('Native probe ran')):
                    with self.assertRaisesRegex(RuntimeError,'private save-preview'):
                        windows_packager.verify_windows_runtime(build)
                with patch.object(linux_packager,'baseline',side_effect=AssertionError('Baseline probe ran')):
                    with self.assertRaisesRegex(RuntimeError,'private save-preview'):
                        linux_packager.stage(build,destination,None,None,None,None,None,None,None)
                with patch.object(linux_packager,'verify_inventory',side_effect=AssertionError('Inventory probe ran')):
                    with self.assertRaisesRegex(RuntimeError,'private save-preview'):
                        linux_packager.refresh(build,appdir)
                self.assertEqual(program.read_bytes(),b'preserve original')
                self.assertFalse(destination.exists())
            helper.unlink()
        helper=appdir/'usr/bin/DriftersToolkitSavePreview';helper.write_bytes(b'private decoder')
        with patch.object(linux_sealer,'release_notes_bytes',side_effect=AssertionError('Packaging inputs read')):
            with self.assertRaisesRegex(RuntimeError,'private save-preview'):
                linux_sealer.seal_appimage(appdir,destination,None,None,None,None)
        self.assertFalse(destination.exists())
        self.assertEqual(program.read_bytes(),b'preserve original')

    def test_linux_sealing_rejects_private_editor_in_either_program(self):
        appdir=self.root/'AppDir';programs=appdir/'usr/bin';programs.mkdir(parents=True)
        for name in ('DriftersToolkit','DriftersToolkitCLI'):(programs/name).write_bytes(b'release fixture')
        (appdir/'runtime-manifest.json').write_text('{}\n',encoding='utf-8')
        for private_program in (None,'DriftersToolkit','DriftersToolkitCLI'):
            calls=[]
            def runner(command,**kwargs):
                calls.append(command)
                self.assertEqual(kwargs['capture_output'],True)
                if '--save-editor-development-verify-only' in command:
                    code=0 if Path(command[0]).name==private_program else 1
                else:code=1 # Neither reviewer nor private GUI test capability.
                return SimpleNamespace(returncode=code)
            with self.subTest(private_program=private_program), \
                 patch.object(linux_packager,'verify_inventory',return_value={}), \
                 patch.object(checker,'check_directory'), \
                 patch.object(linux_packager,'run'), \
                 patch.object(linux_sealer.subprocess,'run',side_effect=runner):
                if private_program:
                    with self.assertRaisesRegex(RuntimeError,'Private save-editor'):
                        linux_sealer.verify_seal_payload(appdir)
                else:
                    proof=linux_sealer.verify_seal_payload(appdir)
                    self.assertEqual(set(proof['nativePrograms']),{'gui','cli'})
                    self.assertEqual([Path(call[0]).name for call in calls
                                      if '--save-editor-development-verify-only' in call],
                                     ['DriftersToolkit','DriftersToolkitCLI'])
            for name in ('DriftersToolkit','DriftersToolkitCLI'):
                self.assertEqual((programs/name).read_bytes(),b'release fixture')

    def test_windows_checks_exact_immutable_payload_and_rejects_snapshot_race(self):
        source=self.root/'original-app.js';source.write_bytes(b'verified UI')
        contents={'DriftersToolkit.exe':b'GUI','DriftersToolkitCLI.exe':b'CLI','ui/app.js':source.read_bytes()}
        snapshots=[]
        def runner(command,**kwargs):
            snapshot=Path(command[0]).parent;snapshots.append(snapshot)
            self.assertNotEqual(snapshot,self.root)
            self.assertEqual((snapshot/'ui/app.js').read_bytes(),b'verified UI')
            source.write_bytes(b'changed original after snapshot')
            return SimpleNamespace(returncode=1 if '--interface-test-only' in command or '--reviewer-verify-only' in command or '--save-editor-development-verify-only' in command else 0)
        windows_packager.verify_windows_payload(contents,run=runner)
        self.assertTrue(snapshots)
        self.assertEqual(contents['ui/app.js'],b'verified UI')
        def raced(command,**kwargs):
            (Path(command[0]).parent/'ui/app.js').write_bytes(b'raced snapshot')
            return SimpleNamespace(returncode=1 if '--interface-test-only' in command or '--reviewer-verify-only' in command or '--save-editor-development-verify-only' in command else 0)
        with self.assertRaisesRegex(RuntimeError,'payload changed'):
            windows_packager.verify_windows_payload(contents,run=raced)

    def test_linux_rechecks_copied_payload_after_original_verification(self):
        appdir=self.root/'original';appdir.mkdir();program=appdir/'native';program.write_bytes(b'verified runtime')
        checks=[]
        def verify(directory,reviewer=False):
            checks.append(Path(directory))
            if (Path(directory)/'native').read_bytes()!=b'verified runtime':
                raise RuntimeError('Copied payload failed native verification')
            return {'gui':'verified'}
        with patch.object(linux_sealer,'verify_seal_payload',side_effect=verify):
            linux_sealer.verify_seal_payload(appdir)
            program.write_bytes(b'changed after original validation')
            with self.assertRaisesRegex(RuntimeError,'Copied payload'):
                linux_sealer.verified_appdir_copy(appdir,self.root/'changed-copy')
            program.write_bytes(b'verified runtime')
            result=linux_sealer.verified_appdir_copy(appdir,self.root/'verified-copy',True)
            self.assertEqual(result,{'gui':'verified'})
        self.assertEqual(checks,[appdir,self.root/'changed-copy',self.root/'verified-copy'])

    def test_linux_payload_identity_covers_notices_and_interface_bytes(self):
        appdir=self.root/'AppDir';(appdir/'licenses').mkdir(parents=True);(appdir/'usr/bin/ui').mkdir(parents=True)
        notice=appdir/'licenses/required.txt';notice.write_bytes(b'original notice')
        interface=appdir/'usr/bin/ui/app.js';interface.write_bytes(b'verified UI')
        first=linux_sealer.payload_digest(appdir)
        self.assertEqual(linux_sealer.payload_digest(appdir),first)
        notice.write_bytes(b'changed notice')
        self.assertNotEqual(linux_sealer.payload_digest(appdir),first)
        notice.write_bytes(b'original notice');interface.write_bytes(b'changed UI')
        self.assertNotEqual(linux_sealer.payload_digest(appdir),first)

    def archive(self,change=None,name='fixture.zip',mode=None):
        contents={name:b'fixture\n' for name in layout.WINDOWS_RUNTIME}
        contents['config/nexus.json']=b'{"applicationSlug":""}'
        if change:change(contents)
        checks={name:hashlib.sha256(data).hexdigest() for name,data in contents.items()}
        contents['SHA256SUMS.json']=(json.dumps(checks)+'\n').encode()
        path=self.root/name
        with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as archive:
            for name,data in contents.items():
                info=zipfile.ZipInfo('DriftersToolkit/'+name)
                info.external_attr=(mode if name=='ui/app.js' and mode else stat.S_IFREG|0o644)<<16
                archive.writestr(info,data)
        return path

    def test_public_downloads_never_nest_archives(self):
        checker.refuse_nested_archives({'licenses/GNU-GPL-3.0.txt':b'GPL','Launch-DriftersToolkit.sh':b'#!/bin/sh'})
        for name,content in (('licenses.zip',b'PK\x03\x04data'),('notes/extra.7z',b'x'),('licenses/notice.txt',b'PK\x03\x04hidden zip')):
            with self.subTest(name=name),self.assertRaises(RuntimeError):checker.refuse_nested_archives({name:content})

    def test_named_windows_layout_and_hashes(self):
        checker.check_archive(self.archive())

    def test_release_notes_are_versioned_bounded_plain_text(self):
        content=layout.release_notes_bytes()
        value=layout.release_notes(content,layout.app_version())
        self.assertEqual(value['version'],layout.app_version())
        self.assertTrue(1<=len(value['items'])<=20)
        invalid=[b'{',b'[]',b'null',b' '*16385,
                 ('{"schemaVersion":1,"schemaVersion":1,"version":"'+layout.app_version()+'","items":["A change."]}').encode()]
        for field,replacement in (('schemaVersion',True),('schemaVersion',1.0),('schemaVersion',2),
                                  ('version','0.0.0'),('items',[]),('items',[True]),
                                  ('items',['Same.','same.']),('items',['change']*21)):
            invalid.append(json.dumps({**value,field:replacement}).encode())
        invalid.append(json.dumps({**value,'url':'extra field'}).encode())
        for text in ('','x'*401,' leading','trailing ','Two\nlines','<b>Markup</b>',
                     'Visit https://example.com','Visit WWW.example.com','Invisible \u202e text','Hidden \u0085 text'):
            invalid.append(json.dumps({**value,'items':[text]}).encode())
        for body in invalid:
            with self.subTest(body=body[:90]),self.assertRaises(RuntimeError):
                layout.release_notes(body,layout.app_version())

    def test_windows_release_notes_are_manifest_listed_and_optional_for_old_packages(self):
        checker.check_archive(self.archive()) # Older signed-updater layout stays compatible.
        path=self.archive(lambda rows:rows.update({layout.RELEASE_NOTES_NAME:layout.release_notes_bytes()}))
        checker.check_archive(path)
        with zipfile.ZipFile(path) as archive:
            checks=json.loads(archive.read('DriftersToolkit/SHA256SUMS.json'))
            self.assertEqual(checks[layout.RELEASE_NOTES_NAME],hashlib.sha256(layout.release_notes_bytes()).hexdigest())
        for body in (b'{}',json.dumps({'schemaVersion':1,'version':'0.0.0','items':['Wrong version.']}).encode()):
            with self.assertRaises(RuntimeError):
                checker.check_archive(self.archive(lambda rows:rows.update({layout.RELEASE_NOTES_NAME:body})))

    def test_linux_notes_are_outside_appimage_and_checksum_covered(self):
        rows,modes=self.appimage(False)
        rows[layout.RELEASE_NOTES_NAME]=layout.release_notes_bytes()
        path=self.root/'linux-notes.zip'
        path.write_bytes(layout.zip_bytes('DriftersToolkit-Linux',rows,modes))
        checker.check_archive(path)
        with zipfile.ZipFile(path) as archive:
            checks=json.loads(archive.read('DriftersToolkit-Linux/SHA256SUMS.json'))
            self.assertEqual(checks[layout.RELEASE_NOTES_NAME],hashlib.sha256(rows[layout.RELEASE_NOTES_NAME]).hexdigest())
        rows[layout.RELEASE_NOTES_NAME]=b'{}'
        path.write_bytes(layout.zip_bytes('DriftersToolkit-Linux',rows,modes))
        with self.assertRaises(RuntimeError):checker.check_archive(path)

    def test_signed_linux_executables_require_unix_origin_even_with_valid_hashes(self):
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        key=ec.generate_private_key(ec.SECP256R1())
        private=self.root/'fixture-test-key.pem'
        private.write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,
                                             serialization.NoEncryption()))
        numbers=key.public_key().public_numbers()
        x,y=numbers.x.to_bytes(32,'big'),numbers.y.to_bytes(32,'big')
        public=self.root/'fixture-public.json'
        public.write_text(json.dumps({'curve':'P-256','x':x.hex(),'y':y.hex(),'keyId':release_signing.key_id(x,y)}))
        rows,executables=self.appimage(False)
        path=self.root/'signed-linux.zip'
        path.write_bytes(layout.zip_bytes('DriftersToolkit-Linux',rows,executables))
        with patch.object(release_signing,'PRIVATE_KEY',private),patch.object(release_signing,'PUBLIC_KEY',public):
            release_signing.sign(path,'linux',layout.app_version())
            self.assertEqual(checker.check_archive(path,require_signature=True),
                             {'platform':'linux','distribution':'portable','reviewer':False})
            signed=path.read_bytes()
            for executable in executables:
                with self.subTest(executable=executable):
                    changed=bytearray(signed)
                    with zipfile.ZipFile(io.BytesIO(signed)) as archive:
                        position=archive.start_dir
                        for info in archive.infolist():
                            self.assertEqual(changed[position:position+4],b'PK\x01\x02')
                            name_size,extra_size,comment_size=struct.unpack_from('<3H',changed,position+28)
                            if info.filename=='DriftersToolkit-Linux/'+executable:
                                changed[position+5]=0 # Only the creator-OS byte; file bytes/mode/signature stay intact.
                            position+=46+name_size+extra_size+comment_size
                    path.write_bytes(changed)
                    platform,_,files,modes=release_signing.read_release(path)
                    self.assertEqual(release_signing.verify_contents(platform,files),layout.app_version())
                    self.assertIn(executable,modes)
                    with self.assertRaisesRegex(RuntimeError,'UNIX permission metadata'):
                        checker.check_archive(path,require_signature=True)

    def test_windows_package_still_accepts_dos_origin_data_metadata(self):
        path=self.archive()
        changed=bytearray(path.read_bytes())
        with zipfile.ZipFile(path) as archive:
            position=archive.start_dir
            for info in archive.infolist():
                self.assertEqual(info.external_attr>>16,0o100644)
                name_size,extra_size,comment_size=struct.unpack_from('<3H',changed,position+28)
                changed[position+5]=0
                position+=46+name_size+extra_size+comment_size
        path.write_bytes(changed)
        checker.check_archive(path)

    def test_release_signature_is_required_and_checked(self):
        path=self.archive()
        with self.assertRaises(RuntimeError):checker.check_archive(path,require_signature=True)
        with zipfile.ZipFile(path) as archive:contents={info.filename:archive.read(info) for info in archive.infolist()}
        record={'schemaVersion':1,'product':'drifters-toolkit','platform':'windows','version':layout.app_version(),
                'sums':'0'*64,'keyId':'0'*16,'signature':'0'*128}
        contents['DriftersToolkit/RELEASE-SIGNATURE.json']=json.dumps(record).encode()
        with zipfile.ZipFile(path,'w') as archive:
            for name,data in contents.items():archive.writestr(name,data)
        # The signature record is not part of the checksum list, but a record that covers another
        # list (or another key) is refused before any cryptography runs.
        with self.assertRaisesRegex(RuntimeError,'checksum manifest'):checker.check_archive(path)

    def test_hash_mismatch(self):
        path=self.archive()
        with zipfile.ZipFile(path) as archive:contents={info.filename:archive.read(info) for info in archive.infolist()}
        contents['DriftersToolkit/ui/app.js']=b'changed'
        with zipfile.ZipFile(path,'w') as archive:
            for name,data in contents.items():archive.writestr(name,data)
        with self.assertRaises(RuntimeError):checker.check_archive(path)

    def test_extra_file_even_with_matching_manifest(self):
        path=self.archive(lambda rows:rows.update({'notes.txt':b'unlisted'}))
        with self.assertRaises(RuntimeError):checker.check_archive(path)

    def test_missing_cli_even_with_matching_manifest(self):
        path=self.archive(lambda rows:rows.pop('DriftersToolkitCLI.exe'))
        with self.assertRaises(RuntimeError):checker.check_archive(path)

    def test_filename_cannot_disable_runtime_privacy(self):
        path=self.archive(lambda rows:rows.update({'ui/app.js':b'Codex'}),name='FakeSource.zip')
        with self.assertRaises(RuntimeError):checker.check_archive(path)

    def test_zip_symlink_and_privileged_modes(self):
        for mode in (stat.S_IFLNK|0o777,stat.S_IFREG|0o4755,stat.S_IFREG|0o666):
            with self.subTest(mode=mode):
                with self.assertRaises(RuntimeError):checker.check_archive(self.archive(mode=mode))

    def test_private_and_noncanonical_paths(self):
        for name in ('../escape','a//b','a/./b','a/../b','a\\b','C:/file','/absolute','a\x00b','a\nb','a/NUL.txt','a/end.','a/end ','profiles/profile.json','state/key.dpapi','crash.dmp'):
            with self.subTest(name=repr(name)):
                with self.assertRaises(RuntimeError):checker.Inspection(True).name(name)

    def test_duplicate_metadata_keys(self):
        with self.assertRaises(RuntimeError):checker.json_object(b'{"key":1,"key":2}','fixture')

    def test_sidecar_matches_exact_archive(self):
        path=self.archive();sidecar=Path(str(path)+'.sha256')
        sidecar.write_text(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+path.name+'\n')
        checker.check_archive(path,require_sidecar=True)
        sidecar.write_text('0'*64+'  '+path.name+'\n')
        with self.assertRaises(RuntimeError):checker.check_archive(path,require_sidecar=True)

    def test_nexus_configuration_is_exact(self):
        for body in (b'{"applicationSlug":"","apiKey":"fixture"}',b'{"applicationSlug":false}'):
            with self.assertRaises(RuntimeError):checker.check_archive(self.archive(lambda rows:rows.update({'config/nexus.json':body})))

    def test_plain_input_preserves_hardlinked_fixture(self):
        original=self.root/'original';original.write_bytes(b'unchanged')
        alias=self.root/'alias';os.link(original,alias)
        with self.assertRaises(RuntimeError):layout.read_plain(original)
        self.assertEqual(alias.read_bytes(),b'unchanged')

    def test_plain_input_size_limit(self):
        path=self.root/'large';path.write_bytes(b'12345')
        with self.assertRaises(RuntimeError):layout.read_plain(path,4)

    def test_source_built_core_exact_appdir_boundaries(self):
        name='usr/lib/libQt6WebEngineCore.so.6'
        for prefix in ('','DriftersToolkit.AppDir/'):
            for size in (checker.MAX_MEMBER+1,checker.MAX_QT_WEBENGINE_CORE):
                checker.Inspection(True,appdir_prefix=prefix).consume(size,prefix+name)
            with self.assertRaisesRegex(RuntimeError,'member exceeds'):
                checker.Inspection(True,appdir_prefix=prefix).consume(checker.MAX_QT_WEBENGINE_CORE+1,prefix+name)
            for wrong in (name+'.11.3',name+'.bak',name.upper(),'usr/lib/another.so.6','other/'+name,name+'/other'):
                with self.subTest(prefix=prefix,name=wrong),self.assertRaisesRegex(RuntimeError,'member exceeds'):
                    checker.Inspection(True,appdir_prefix=prefix).consume(checker.MAX_MEMBER+1,prefix+wrong)
        for public in (False,True):
            for label in (name,'DriftersToolkit.AppDir/'+name,'DriftersToolkit-review-source/'+name):
                with self.subTest(public=public,label=label),self.assertRaisesRegex(RuntimeError,'member exceeds'):
                    checker.Inspection(public).consume(checker.MAX_MEMBER+1,label)

    def test_source_built_core_keeps_shared_total_and_entry_budgets(self):
        name='usr/lib/libQt6WebEngineCore.so.6';size=checker.MAX_QT_WEBENGINE_CORE
        budget=[checker.MAX_TOTAL-size,0]
        checker.Inspection(True,budget=budget,appdir_prefix='').consume(size,name)
        self.assertEqual(budget[0],checker.MAX_TOTAL)
        with self.assertRaisesRegex(RuntimeError,'total inspection'):
            checker.Inspection(False,budget=budget).consume(1,'nested-source-member')
        with self.assertRaisesRegex(RuntimeError,'total inspection'):
            checker.Inspection(True,budget=[0,checker.MAX_ENTRIES],appdir_prefix='').consume(1,name)

    def test_source_built_core_directory_read_privacy_and_alias_gates(self):
        appdir=self.root/'AppDir';core=appdir/'usr/lib/libQt6WebEngineCore.so.6'
        core.parent.mkdir(parents=True);core.write_bytes(b'12345678')
        with patch.object(checker,'MAX_MEMBER',4),patch.object(checker,'MAX_QT_WEBENGINE_CORE',8),patch.object(checker,'MAX_TOTAL',128),patch.object(checker,'verify_portable'),patch.object(checker,'read_plain',wraps=layout.read_plain) as reader:
            checker.check_directory(appdir)
            self.assertEqual(reader.call_args.args,(core,8))
            core.write_bytes(b'123456789')
            with self.assertRaisesRegex(RuntimeError,'bounded independent'):checker.check_directory(appdir)
            core.write_bytes(b'12345678');wrong=core.with_name(core.name+'.bak');core.rename(wrong)
            with self.assertRaisesRegex(RuntimeError,'bounded independent'):checker.check_directory(appdir)
            wrong.rename(core)
            alias=self.root/'alias';os.link(core,alias)
            with self.assertRaisesRegex(RuntimeError,'independent ordinary'):checker.check_directory(appdir)
            alias.unlink()
            if os.name=='posix':
                core.unlink();core.symlink_to(self.root/'missing')
                with self.assertRaisesRegex(RuntimeError,'redirected'):checker.check_directory(appdir)
                core.unlink()
            core.write_bytes(b'codex')
            with self.assertRaisesRegex(RuntimeError,'private assistant'):checker.check_directory(appdir)

    def test_source_built_core_exception_never_enters_zip_contexts(self):
        path=self.root/'core.zip'
        for root in ('DriftersToolkit.AppDir','DriftersToolkit-review-source','DriftersToolkit-NexusReview'):
            with zipfile.ZipFile(path,'w') as archive:archive.writestr(root+'/usr/lib/libQt6WebEngineCore.so.6',b'12345678')
            for source in (False,True):
                with self.subTest(root=root,source=source),patch.object(checker,'MAX_MEMBER',4),patch.object(checker,'MAX_QT_WEBENGINE_CORE',8),patch.object(checker.zipfile.ZipFile,'read',side_effect=AssertionError('Oversized ZIP content was read')):
                    with self.assertRaisesRegex(RuntimeError,'member exceeds'):checker.check_archive(path,source=source)

    def test_sealer_snapshot_uses_same_exact_core_bound_and_total(self):
        appdir=self.root/'snapshot';core=appdir/'usr/lib/libQt6WebEngineCore.so.6'
        core.parent.mkdir(parents=True);core.write_bytes(b'12345678')
        with patch.object(checker,'MAX_MEMBER',4),patch.object(checker,'MAX_QT_WEBENGINE_CORE',8),patch.object(checker,'MAX_TOTAL',12):
            self.assertEqual(len(linux_sealer.payload_digest(appdir)),64)
            core.write_bytes(b'123456789')
            with self.assertRaisesRegex(RuntimeError,'bounded'):linux_sealer.payload_digest(appdir)
            core.write_bytes(b'12345678');wrong=core.with_name(core.name+'.bak');core.rename(wrong)
            with self.assertRaisesRegex(RuntimeError,'bounded'):linux_sealer.payload_digest(appdir)
            wrong.rename(core)
            (core.parent/'other').write_bytes(b'1234')
            linux_sealer.payload_digest(appdir)
            (core.parent/'extra').write_bytes(b'1')
            with self.assertRaisesRegex(RuntimeError,'total inspection'):linux_sealer.payload_digest(appdir)

    def test_source_built_core_recognized_tar_uses_same_bounded_read(self):
        import gzip,tarfile
        path=self.root/'appdir.tar.gz'
        def make(name):
            content=io.BytesIO()
            with tarfile.open(fileobj=content,mode='w') as archive:
                info=tarfile.TarInfo(name);body=b'a'*40002;info.size=len(body);info.mode=0o644
                archive.addfile(info,io.BytesIO(body))
            path.write_bytes(gzip.compress(content.getvalue()))
        name='DriftersToolkit.AppDir/usr/lib/libQt6WebEngineCore.so.6'
        with patch.object(checker,'MAX_MEMBER',40000),patch.object(checker,'MAX_QT_WEBENGINE_CORE',80000),patch.object(checker,'verify_portable'):
            make(name);checker.check_archive(path)
            with self.assertRaisesRegex(RuntimeError,'member exceeds'):checker.check_archive(path,source=True)
            for wrong in (name+'.11.3',name+'.bak','wrong/'+name):
                make(wrong)
                with self.assertRaisesRegex(RuntimeError,'member exceeds'):checker.check_archive(path)

    def test_manifest_types_and_noncanonical_paths(self):
        rows=[{'path':name,'size':1,'sha256':'a'*64,'executable':False} for name in ('usr/bin/qt.conf','usr/libexec/qt.conf','usr/libexec/QtWebEngineProcess','usr/lib/library.so')]
        valid={'schemaVersion':1,'files':rows};self.assertIn('portable_manifest',generator.render(valid))
        for path in ('usr/lib//x','usr/lib/./x','usr/lib/x\x00y','usr/lib/x\ny'):
            bad=json.loads(json.dumps(valid));bad['files'][-1]['path']=path
            with self.subTest(path=repr(path)):
                with self.assertRaises(RuntimeError):generator.render(bad)
        bad=json.loads(json.dumps(valid));bad['schemaVersion']=True
        with self.assertRaises(RuntimeError):generator.render(bad)

    def test_build_url_whitelist(self):
        for url in ('https://raw.githubusercontent.com/googlefonts/noto-emoji/pin/font.ttf','https://release-assets.githubusercontent.com/fixed?signature=example','https://globalcdn.nuget.org/package.nupkg'):
            self.assertEqual(build_fetch.require_https(url),url)
        for url in ('http://github.com/file','https://github.com:444/file','https://user:password@github.com/file','https://github.com.evil.invalid/file','https://github.com/file#fragment','file:///tmp/file'):
            with self.subTest(url=url):
                with self.assertRaises(RuntimeError):build_fetch.require_https(url)

    def fake_fetch(self,body,target,expected=None,size=None,callback=None):
        class Opener:
            def open(self,*args,**kwargs):
                if callback:callback()
                return io.BytesIO(body)
        return build_fetch.fetch_pinned('https://raw.githubusercontent.com/project/repository/pin/file',target,expected or hashlib.sha256(body).hexdigest(),size,Opener())

    def test_pinned_download_and_cached_verification(self):
        path=self.root/'dependency';self.fake_fetch(b'fixed bytes',path,size=11)
        self.assertEqual(path.read_bytes(),b'fixed bytes');self.assertEqual(path.stat().st_nlink,1)
        self.fake_fetch(b'fixed bytes',path,size=11,callback=lambda:self.fail('A matching cached input should not download again'))

    def test_bad_download_leaves_no_partial(self):
        for body,expected,size in ((b'bad','0'*64,3),(b'oversized','0'*64,2),(b'short','0'*64,9)):
            with self.subTest(body=body):
                path=self.root/'dependency'
                with self.assertRaises(RuntimeError):self.fake_fetch(body,path,expected,size)
                self.assertEqual(list(self.root.iterdir()),[])

    def test_existing_and_raced_destination_preserved(self):
        path=self.root/'existing';path.write_bytes(b'external')
        with self.assertRaises(RuntimeError):self.fake_fetch(b'download',path)
        self.assertEqual(path.read_bytes(),b'external');path.unlink()
        with self.assertRaises(FileExistsError):self.fake_fetch(b'download',path,callback=lambda:path.write_bytes(b'new external'))
        self.assertEqual(path.read_bytes(),b'new external');self.assertEqual([p.name for p in self.root.iterdir()],['existing'])

    def test_reviewer_source_excludes_private_handoff(self):
        for name in ('README_FOR_CLAUDE.md','README.md','catalog/README.md','src/README.hpp','docs/NEXUS_REVIEW_BUILD.md','tests/nexus_tests.cpp','scripts/package_reviewer.py','scripts/catalog_sources.json','scripts/Dockerfile.appimage-build','docs/AUDIT_PACKAGING.md','docs/NEXUS_REGISTRATION_EMAIL.txt','docs/artwork-prototypes/no-recoil.png','scripts/account.json','ui/assets/nexus-reference.png','src/secret.dpapi','out/DriftersToolkit.exe'):
            with self.subTest(name=name):self.assertFalse(review_sources.allowed(name))
        for name in ('src/core.cpp','config/release-notes.json','config/changelog-history.json','scripts/build_changelog.py','ui/changelog.js','scripts/build.ps1','scripts/package_portable_linux.py','scripts/bootstrap_qt_sdk.py','scripts/generate_assets_manifest.py','ui/assets/seasonal-emoji.svg','docs/NotoEmoji-Images-LICENSE.txt'):
            with self.subTest(name=name):self.assertTrue(review_sources.allowed(name))

    def test_minimal_source_contains_explicit_versioned_release_notes(self):
        # Inspect the source inventory in memory; do not create an actual review package.
        sources=review_sources.collect()
        self.assertIn('config/release-notes.json',review_sources.FIXED)
        self.assertIn('config/release-notes.json',sources)
        for name in ('config/changelog-history.json','scripts/build_changelog.py','ui/changelog.js'):
            self.assertIn(name,sources)
            self.assertEqual(sources[name],layout.read_plain(ROOT/name))
        self.assertIn('changelog.js',layout.PUBLIC_UI)
        self.assertIn('ui/changelog.js',layout.WINDOWS_RUNTIME)
        self.assertEqual(layout.release_notes(sources['config/release-notes.json'],layout.app_version())['version'],layout.app_version())
        for name in ('src/save_backups.cpp','src/game_build.cpp','src/renderer_snapshot.hpp','src/library_meta.hpp'):
            self.assertIn(name,sources)
        for name in ('docs/BUILD_LIBRARY_REPLACEMENT.txt','scripts/release_signing.py','scripts/release_key.json'):
            self.assertIn(name,sources)
            self.assertEqual(sources[name],layout.read_plain(ROOT/name))
        self.assertFalse(any('README' in name or name.startswith(('private-artifacts/','dist/','out/')) for name in sources))

    def test_zip_metadata_is_repeatable(self):
        files={'file.txt':b'unchanged'}
        first=layout.zip_bytes('fixture',files);second=layout.zip_bytes('fixture',files)
        self.assertEqual(first,second)
        with zipfile.ZipFile(io.BytesIO(first)) as archive:
            self.assertTrue(all(info.date_time==(2026,1,1,0,0,0) for info in archive.infolist()))

    def appimage(self,reviewer=True):
        """Minimal synthetic universal AppImage ZIP rows (Type2 ELF header only)."""
        name='Drifters-Toolkit-'+('NexusReview-'if reviewer else '')+'Linux-x86_64.AppImage'
        image=bytearray(64);image[0:7]=b'\x7fELF\x02\x01\x01';image[8:11]=b'AI\x02';image[18:20]=b'\x3e\x00';image=bytes(image)
        record=lambda body:{'sha256':hashlib.sha256(body).hexdigest(),'size':len(body)}
        manifest={'schemaVersion':1,'format':'appimage-x86_64','mode':'nexus-reviewer'if reviewer else 'public','applicationVersion':layout.app_version(),
                  'qtVersion':'6.11.3','minimumGlibc':'2.35','minimumGlibcxx':'3.4.30','minimumCpu':'x86-64-v2',
                  'runtime':{'file':name,**record(image)},'runtimeManifestSha256':'a'*64,
                  'appImageRuntime':{**record(b'runtime'),'provenanceSha256':'b'*64},
                  'sourceCompanion':{'file':'Drifters-Toolkit-Linux-Corresponding-Source.tar',**record(b'tar'),'components':['qtbase']},
                  'nativePrograms':{'gui':record(b'gui'),'cli':record(b'cli')},
                  'capabilities':{'reviewerConnect':reviewer,'interfaceTesting':False}}
        rows={name:image,'Launch-DriftersToolkit.sh':b'#!/bin/sh\nexec "$(dirname "$0")/'+name.encode()+b'" "$@"\n',
              'LINUX_REQUIREMENTS.json':b'{"format":"appimage-x86_64"}','APPIMAGE_MANIFEST.json':json.dumps(manifest).encode(),
              'licenses/THIRD_PARTY.md':b'fixture notices\n'}
        return rows,(name,'Launch-DriftersToolkit.sh')

    def test_only_the_universal_appimage_is_accepted_for_linux(self):
        rows,executables=self.appimage(False)
        path=self.root/'linux.zip';path.write_bytes(layout.zip_bytes('DriftersToolkit-Linux',rows,executables))
        self.assertEqual(checker.check_archive(path),{'platform':'linux','distribution':'portable','reviewer':False})
        for name in ('fixture.deb','fixture.rpm'):
            package=self.root/name;package.write_bytes(b'!<arch>\nfixture')
            with self.subTest(package=name),self.assertRaises(RuntimeError):checker.check_archive(package)
        # A system-linked folder (no AppImage) or a nested distribution package is refused.
        folder={'DriftersToolkit':b'\x7fELF','DriftersToolkitCLI':b'\x7fELF','LINUX_REQUIREMENTS.json':b'{}'}
        nested={**rows,'drifters-toolkit_0.1.0_amd64.deb':b'!<arch>\n'}
        for invalid,modes in ((folder,('DriftersToolkit','DriftersToolkitCLI')),(nested,executables)):
            path.write_bytes(layout.zip_bytes('DriftersToolkit-Linux',invalid,modes))
            with self.assertRaises(RuntimeError):checker.check_archive(path)
        path.write_bytes(layout.zip_bytes('DriftersToolkit-Linux',rows,()))
        with self.assertRaises(RuntimeError):checker.check_archive(path)

    def test_linux_system_qt_notices_are_pinned_and_required(self):
        lock=json.loads((ROOT/'scripts/dependencies.lock.json').read_text())
        for name in ('GNU-LGPL-3.0.txt','GNU-GPL-3.0.txt','GNU-LGPL-2.1.txt'):
            self.assertIn(name,layout.LINUX_NOTICES);self.assertNotIn(name,layout.WINDOWS_NOTICES)
            self.assertIn('docs/'+name,review_sources.FIXED)
            entry=lock['linux-runtime-notices/'+name];body=layout.read_plain(ROOT/entry['target'])
            self.assertEqual(len(body),entry['size']);self.assertEqual(hashlib.sha256(body).hexdigest(),entry['sha256'])

    def test_clean_source_and_nested_reviewer_bundle(self):
        sources={name:b'fixture\n' for name in review_sources.FIXED};sources['config/nexus.json']=b'{"applicationSlug":""}'
        review_source=layout.zip_bytes('DriftersToolkit-review-source',sources)
        path=self.root/'source.zip';path.write_bytes(review_source)
        checker.check_archive(path,source=True)
        with self.assertRaises(RuntimeError):checker.check_archive(path)
        with zipfile.ZipFile(self.archive()) as archive:windows={info.filename.split('/',1)[1]:archive.read(info) for info in archive.infolist() if not info.filename.endswith('SHA256SUMS.json')}
        inner={'Drifters-Toolkit-NexusReview-Windows.zip':layout.zip_bytes('DriftersToolkit',windows),
               'Drifters-Toolkit-NexusReview-Linux.zip':layout.zip_bytes('DriftersToolkit-Linux',*self.appimage(True)),
               'Drifters-Toolkit-Review-Source.zip':review_source}
        inner.update({name:b'fixture review material' for name in layout.REVIEW_OUTER_FILES if name not in inner})
        target=self.root/'review.zip';target.write_bytes(layout.zip_bytes('DriftersToolkit-NexusReview',inner));checker.check_archive(target)
        public_rows,public_modes=self.appimage(False)
        folder={'DriftersToolkit':b'\x7fELF','DriftersToolkitCLI':b'\x7fELF'}
        for invalid,modes in ((public_rows,public_modes),(folder,('DriftersToolkit','DriftersToolkitCLI'))):
            replaced={**inner,'Drifters-Toolkit-NexusReview-Linux.zip':layout.zip_bytes('DriftersToolkit-Linux',invalid,modes)}
            target.write_bytes(layout.zip_bytes('DriftersToolkit-NexusReview',replaced))
            with self.assertRaises(RuntimeError):checker.check_archive(target)
        inner['Drifters-Toolkit-Review-Source.zip']=layout.zip_bytes('DriftersToolkit-review-source',{**sources,'README_FOR_CLAUDE.md':b'private handoff'})
        target.write_bytes(layout.zip_bytes('DriftersToolkit-NexusReview',inner))
        with self.assertRaises(RuntimeError):checker.check_archive(target)

    def test_legacy_portable_inventory_cannot_skip_current_security_proof(self):
        runtime={name:b'fixture' for name in ('usr/bin/qt.conf','usr/libexec/qt.conf','usr/libexec/QtWebEngineProcess','usr/lib/library.so')}
        rows=[{'path':name,'size':len(body),'sha256':hashlib.sha256(body).hexdigest(),'executable':False} for name,body in runtime.items()]
        files={**runtime,'runtime-manifest.json':json.dumps({'schemaVersion':1,'files':rows}).encode(),
               'DEPENDENCY-SOURCES.json':json.dumps({'packages':[{'package':'fixture:amd64','version':'1','sourcePackage':'fixture','sourceVersion':'1'}]}).encode()}
        required={'AppRun','DriftersToolkit.desktop','DriftersToolkit.png','usr/bin/DriftersToolkit','usr/bin/DriftersToolkitCLI','usr/bin/catalog/catalog.json','usr/bin/config/nexus.json',
                  *('usr/bin/ui/'+name for name in layout.PUBLIC_UI),*('licenses/'+name for name in layout.COMMON_NOTICES),'licenses/miniz-LICENSE','licenses/fixture-copyright'}
        files.update({name:b'fixture' for name in required})
        for schema in (None,1,True):
            proof=json.loads(files['DEPENDENCY-SOURCES.json'])
            if schema is not None:proof['schemaVersion']=schema
            with self.assertRaises(RuntimeError):checker.verify_portable({**files,'DEPENDENCY-SOURCES.json':json.dumps(proof).encode()})

    def test_current_qt_and_openssl_runtime_provenance_is_required(self):
        import bootstrap_openssl as ssl
        import bootstrap_qt_webengine_security as webengine
        runtime={name:b'fixture runtime' for name in ('usr/bin/qt.conf','usr/libexec/qt.conf','usr/libexec/QtWebEngineProcess',
                 'usr/lib/libQt6Core.so.6','usr/lib/libQt6WebEngineCore.so.6','usr/lib/libQt6WebEngineWidgets.so.6','usr/lib/libssl.so.3','usr/lib/libcrypto.so.3')}
        runtime.update({name:b'fixture resources' for name in ('usr/translations/qtwebengine_locales/en-US.pak',*('usr/resources/'+value for value in webengine.RUNTIME_RESOURCES))})
        digest=lambda body:{'size':len(body),'sha256':hashlib.sha256(body).hexdigest()}
        rows=[{'path':name,**digest(body),'executable':False} for name,body in runtime.items()]
        notice=b'OpenSSL Apache source notice';qt_notice=b'Qt source notice'
        pinned=webengine.load_lock()
        security={'recipeRevision':pinned['recipeRevision'],'securityLock':webengine.file_record(webengine.LOCK),
                  'recipe':webengine.file_record(webengine.__file__),
                  'storageHelper':webengine.file_record(ROOT/'scripts/linux_build_storage.py'),
                  'runtimeScope':webengine.runtime_scope(),
                  'compatibilityBackports':pinned['compatibilityBackports'],'sourceInventories':webengine.source_inventory_identity(),
                  'sourceArchive':{'sha256':pinned['sourceArchive']['digest'],'size':pinned['sourceArchive']['size']},
                  'baseChromiumCommit':pinned['baseChromiumCommit'],'patchedChromiumCommit':pinned['patchedChromiumCommit'],
                  'declaredSecurityVersion':pinned['declaredSecurityVersion'],
                  'patches':[{key:entry[key] for key in ('commit','sha256','size')} for entry in pinned['patches']],
                  'criticalAdvisories':pinned['criticalAdvisories'],
                  'outputs':[{'file':name,**digest(b'updated original build output')} for name in ('lib/libQt6WebEngineCore.so.6.11.3','lib/libQt6WebEngineWidgets.so.6.11.3','libexec/QtWebEngineProcess','translations/qtwebengine_locales/en-US.pak',*('resources/'+value for value in webengine.RUNTIME_RESOURCES))]}
        import qt_webengine_followup as followup
        prior_outputs=[{**row,'sha256':'b'*64} for row in security['outputs']]
        security['upstreamFollowup']={**followup.identity(followup.verify_inputs()),'outputs':prior_outputs}
        import qt_webengine_incremental as incremental
        incremental_outputs=[{**row,'sha256':'d'*64} for row in security['outputs']]
        security['incrementalDelta']={**incremental.identity(incremental.verify_inputs()),'outputs':incremental_outputs,
                                     'priorOutputs':prior_outputs,'baselineProof':{'sha256':'c'*64,'size':15699}}
        import qt_webengine_residual as residual
        residual_outputs=[{**row,'sha256':'f'*64} for row in security['outputs']]
        security['residualDelta']={**residual.identity(residual.verify_inputs()),'outputs':residual_outputs,
                                  'priorOutputs':incremental_outputs,'baselineProof':{'sha256':'e'*64,'size':36560}}
        import qt_webengine_vendor_followup as vendor_followup
        security['vendorFollowup']={**vendor_followup.identity(vendor_followup.verify_inputs()),'outputs':security['outputs'],
                                   'priorOutputs':residual_outputs,'baselineProof':{'sha256':'1'*64,'size':44626}}
        vendor_outputs=[{**row,'sha256':'2'*64} for row in security['outputs']]
        security['vendorFollowup']['outputs']=vendor_outputs
        import qt_webengine_release_1_1 as release_1_1
        security['release11']={**release_1_1.identity(release_1_1.verify_inputs()),'outputs':security['outputs'],
                               'priorOutputs':vendor_outputs,'baselineProof':{'sha256':'3'*64,'size':44626}}
        # Resource bytes are copied unchanged; only ELF RUNPATH relocation may
        # produce a different final runtime identity.
        for output in security['outputs']:
            if not output['file'].startswith(('lib/','libexec/')):
                output.update(digest(runtime['usr/'+output['file']]))
        qt={'version':'6.11.3','origin':'official-qt-sdk-with-pinned-vendor-webengine-backports','webengineSecurity':security,
            'archives':[{key:entry[key] for key in ('name','url','sha256','size')} for entry in json.loads((ROOT/'scripts/qt_sdk_lock.json').read_text())['archives']],
            'noticeFiles':[{'path':'licenses/qt/LICENSE.txt',**digest(qt_notice)}],
            'runtimeFiles':['usr/lib/libQt6Core.so.6','usr/lib/libQt6WebEngineCore.so.6','usr/lib/libQt6WebEngineWidgets.so.6','usr/libexec/QtWebEngineProcess','usr/translations/qtwebengine_locales/en-US.pak',*('usr/resources/'+value for value in webengine.RUNTIME_RESOURCES)]}
        from package_portable_linux import rebuilt_runtime_targets
        qt['rebuiltRuntimeFiles']=[{'source':entry['source'],'path':name,
                                   'original':{key:entry[key] for key in ('sha256','size')},
                                   'staged':digest(runtime[name]),'localReplacement':False}
                                  for name,entry in sorted(rebuilt_runtime_targets(security['outputs']).items())]
        openssl={'schemaVersion':1,'version':ssl.VERSION,'origin':'official-openssl-source','source':ssl.SOURCE,
                 'buildOptions':ssl.BUILD_OPTIONS,'minimumCpu':'x86-64-v2','minimumGlibc':'2.35',
                 'runtime':{name:digest(b'fixture original library') for name in ssl.RUNTIME_NAMES},'license':digest(notice),
                 'runtimeFiles':['usr/lib/'+name for name in ssl.RUNTIME_NAMES]}
        from bootstrap_archive import public_provenance
        proof={'schemaVersion':2,'qt':qt,'openssl':openssl,'archiveDecoders':public_provenance(),'replacementFile':'licenses/LIBRARY_REPLACEMENT.txt',
               'packages':[{'package':'fixture:amd64','version':'1','sourcePackage':'fixture','sourceVersion':'1'}]}
        files={**runtime,'runtime-manifest.json':json.dumps({'schemaVersion':1,'files':rows}).encode(),
               'DEPENDENCY-SOURCES.json':json.dumps(proof).encode(),'licenses/OpenSSL-LICENSE.txt':notice,'licenses/qt/LICENSE.txt':qt_notice}
        required={'AppRun','DriftersToolkit.desktop','DriftersToolkit.png','usr/bin/DriftersToolkit','usr/bin/DriftersToolkitCLI','usr/bin/catalog/catalog.json','usr/bin/config/nexus.json',
                  *('usr/bin/ui/'+name for name in layout.PUBLIC_UI),*('licenses/'+name for name in layout.LINUX_NOTICES),
                  'licenses/miniz-LICENSE','licenses/fixture-copyright','licenses/LIBRARY_REPLACEMENT.txt'}
        files.update({name:b'fixture' for name in required});checker.verify_portable(files)
        with self.assertRaises(RuntimeError):checker.verify_portable({**files,'usr/lib/libQt6WebEngineCore.so.6':b'changed library'})
        missing=dict(files);missing.pop('licenses/NotoEmoji-Images-LICENSE.txt')
        with self.assertRaises(RuntimeError):checker.verify_portable(missing)
        for mutate in (lambda value:value.pop('openssl'),lambda value:value['openssl'].update(version='3.0.22'),
                       lambda value:value.pop('archiveDecoders'),lambda value:value['archiveDecoders'].update(recipeRevision=0),
                       lambda value:value['archiveDecoders']['sources'][0].update(sha256='0'*64),
                       lambda value:value['qt'].update(version='6.11.2'),
                       lambda value:value['qt'].pop('webengineSecurity'),
                       lambda value:value['qt']['webengineSecurity'].update(patchedChromiumCommit='0'*40),
                       lambda value:value['qt']['webengineSecurity']['patches'].pop(),
                       lambda value:value['qt']['webengineSecurity'].update(recipeRevision=1),
                       lambda value:value['qt']['webengineSecurity'].pop('compatibilityBackports'),
                       lambda value:value['qt']['webengineSecurity']['compatibilityBackports'][0]['upstream'].pop(),
                       lambda value:value['qt']['webengineSecurity']['sourceInventories'].pop('final'),
                       lambda value:value['qt']['webengineSecurity'].pop('upstreamFollowup'),
                       lambda value:value['qt']['webengineSecurity']['upstreamFollowup']['upstream'].pop(),
                       lambda value:value['qt']['webengineSecurity']['upstreamFollowup']['lock'].update(sha256='0'*64),
                       lambda value:value['qt']['webengineSecurity']['upstreamFollowup'].update(outputs=[]),
                       lambda value:value['qt']['webengineSecurity'].pop('incrementalDelta'),
                       lambda value:value['qt']['webengineSecurity']['incrementalDelta']['upstream'].pop(),
                       lambda value:value['qt']['webengineSecurity']['incrementalDelta']['lock'].update(sha256='0'*64),
                       lambda value:value['qt']['webengineSecurity']['incrementalDelta'].update(outputs=[]),
                       lambda value:value['qt']['webengineSecurity']['incrementalDelta'].update(priorOutputs=[]),
                       lambda value:value['qt']['webengineSecurity'].pop('residualDelta'),
                       lambda value:value['qt']['webengineSecurity']['residualDelta']['upstream'].pop(),
                       lambda value:value['qt']['webengineSecurity']['residualDelta']['lock'].update(sha256='0'*64),
                       lambda value:value['qt']['webengineSecurity']['residualDelta'].update(outputs=[]),
                       lambda value:value['qt']['webengineSecurity']['residualDelta'].update(priorOutputs=[]),
                       lambda value:value['qt']['webengineSecurity'].pop('release11'),
                       lambda value:value['qt']['webengineSecurity']['release11']['upstream'].pop(),
                       lambda value:value['qt']['webengineSecurity']['release11']['priorOutputs'][0].update(sha256='0'*64),
                       lambda value:value['qt']['webengineSecurity'].pop('vendorFollowup'),
                       lambda value:value['qt']['webengineSecurity']['vendorFollowup']['upstream'].pop(),
                       lambda value:value['qt']['webengineSecurity']['vendorFollowup']['lock'].update(sha256='0'*64),
                       lambda value:value['qt']['webengineSecurity']['vendorFollowup'].update(outputs=[]),
                       lambda value:value['qt']['webengineSecurity']['vendorFollowup'].update(priorOutputs=[]),
                       lambda value:value['qt']['webengineSecurity']['vendorFollowup']['priorOutputs'][0].update(sha256='0'*64),
                       lambda value:value['qt']['webengineSecurity']['vendorFollowup']['outputs'][0].update(sha256='0'*64),
                       lambda value:value['qt']['webengineSecurity'].update(outputs=[]),
                       lambda value:value['qt']['webengineSecurity']['runtimeScope']['targets'].append('Pdf'),
                       lambda value:value['qt']['webengineSecurity']['outputs'].append({'file':'lib/libQt6WebEngineQuick.so.6.11.3','sha256':'0'*64,'size':1}),
                       lambda value:value['qt'].pop('rebuiltRuntimeFiles'),
                       lambda value:value['qt']['rebuiltRuntimeFiles'].pop(),
                       lambda value:value['qt']['rebuiltRuntimeFiles'][0]['original'].update(sha256='0'*64),
                       lambda value:value['qt']['rebuiltRuntimeFiles'][0]['staged'].update(sha256='0'*64),
                       lambda value:value['qt']['rebuiltRuntimeFiles'][0].update(localReplacement=True),
                       lambda value:value['openssl']['source'].update(sha256='0'*64),
                       lambda value:value['openssl']['runtimeFiles'].pop(),
                       lambda value:value['packages'].append({'package':'libssl3','version':'3.0.22','sourcePackage':'openssl','sourceVersion':'3.0.22'})):
            bad=json.loads(json.dumps(proof));mutate(bad)
            with self.assertRaises(RuntimeError):checker.verify_portable({**files,'DEPENDENCY-SOURCES.json':json.dumps(bad).encode()})
        with self.assertRaises(RuntimeError):checker.verify_portable({**files,'licenses/OpenSSL-LICENSE.txt':b'changed notice'})


if __name__=='__main__':unittest.main(verbosity=2)
