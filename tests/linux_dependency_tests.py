"""Portable library provenance and real ELF replacement-closure regressions."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import bootstrap_linux_dependencies as bootstrap
from linux_dependency_graph import closure


class Dependencies(unittest.TestCase):
    def test_locked_repairs_are_exact_upstream_inputs(self):
        packages = bootstrap.load_lock()['packages']
        self.assertEqual(len(packages),12)
        for package in packages:
            self.assertTrue(any(p['file'].endswith('.dsc') for p in package['sources']))
            for source in package['sources']:
                self.assertTrue(source['url'].startswith('https://deb.debian.org/debian/pool/'))
            for repair in package.get('patches',[]):
                self.assertEqual(bootstrap.identity(ROOT/repair['file']),{k:repair[k] for k in ('size','sha256')})
                self.assertIn(repair['commit'],repair['url'])

    def test_unbound_or_changed_runtime_libraries_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);lib=root/'usr/lib/libsample.so.1';lib.parent.mkdir(parents=True);lib.write_bytes(b'library fixture')
            ident=bootstrap.identity(lib)
            row={'name':lib.name,'package':'sample',**ident}
            proof={'outputs':[row],'runtimeBindings':[{'name':lib.name,'path':'usr/lib/'+lib.name,'original':ident,'staged':ident}]}
            lock={'packages':[{'name':'sample','libraries':[lib.name]}]}
            with patch.object(bootstrap,'load_lock',return_value=lock):
                bootstrap.verify_bindings(proof,root)
                for mutate in (lambda v:v['runtimeBindings'][0]['original'].update(sha256='0'*64),
                               lambda v:v['runtimeBindings'][0].update(path='../libsample.so.1'),
                               lambda v:v.update(runtimeBindings=[]),lambda v:v.update(outputs=[])):
                    invalid=json.loads(json.dumps(proof));mutate(invalid)
                    with self.assertRaises(RuntimeError):bootstrap.verify_bindings(invalid,root)
                lib.write_bytes(b'changed library')
                with self.assertRaises(RuntimeError):bootstrap.verify_bindings(proof,root)
                # The explicit reindex/rebuild route remains available for
                # interface-compatible LGPL library replacements.
                bootstrap.verify_bindings(proof,root,locally_modified=True)

    def test_packaging_requires_notices_and_exact_relocated_libraries(self):
        import package_check
        body=b'library fixture';name='usr/lib/libsample.so.1'
        ident={'size':len(body),'sha256':hashlib.sha256(body).hexdigest()}
        proof={'outputs':[{'name':'libsample.so.1','package':'sample',**ident}],
               'runtimeBindings':[{'name':'libsample.so.1','path':name,'original':ident,'staged':ident}]}
        notice='licenses/maintained-sample-copyright'
        files={name:body,notice:b'Original copyright and licence'}
        with patch.object(bootstrap,'load_lock',return_value={'packages':[{'name':'sample','libraries':['libsample.so.1']}]}):
            self.assertEqual(package_check.maintained_libraries(proof,files,{name}),{notice})
            for invalid in ({name:body},{**files,name:b'changed library'}):
                with self.assertRaises(RuntimeError):package_check.maintained_libraries(proof,invalid,{name})
            with self.assertRaises(RuntimeError):package_check.maintained_libraries(proof,files,set())
            self.assertEqual(package_check.maintained_libraries(proof,{**files,name:b'changed library'},{name},True),{notice})

    @unittest.skipUnless(os.name=='posix' and shutil.which('cc') and shutil.which('readelf'),'Native ELF compiler tools required')
    def test_replacement_excludes_the_old_transitive_library(self):
        with tempfile.TemporaryDirectory(prefix='drifter-elf-closure-') as folder:
            root=Path(folder);old=root/'old';new=root/'new';staged=root/'staged'
            for p in (old,new,staged):p.mkdir()
            def compile_shared(directory,name,body,links=()):
                source=directory/(name+'.c');source.write_text(body)
                target=directory/('lib'+name+'.so')
                subprocess.run(['cc','-shared','-fPIC',str(source),'-o',str(target),'-Wl,-soname,'+target.name,
                                '-Wl,-rpath,$ORIGIN',*[str(p) for p in links]],check=True,capture_output=True)
                return target
            retired=compile_shared(old,'retired','int payload(void){return 13;}')
            active=compile_shared(new,'active','int payload(void){return 42;}')
            bridge=compile_shared(old,'bridge','extern int payload(void); int bridge(void){return payload();}',[retired])
            repaired=compile_shared(new,'bridge','extern int payload(void); int bridge(void){return payload();}',[active])
            entry=compile_shared(old,'entry','extern int bridge(void); int entry(void){return bridge();}',[bridge])
            def needed(source):
                text=subprocess.run(['readelf','-d',str(source)],capture_output=True,text=True,check=True).stdout
                return re.findall(r'\(NEEDED\).*Shared library: \[([^\]]+)\]',text)
            available={p.name:p for p in (retired,active,bridge)}
            resolved=[]
            def resolve(source,name):resolved.append(name);return available[name]
            result=closure([entry],{'libbridge.so':repaired},needed,resolve,lambda name:False)
            self.assertEqual(set(result),{'libbridge.so','libactive.so'})
            self.assertNotIn('libretired.so',resolved)
            for name,path in result.items():shutil.copyfile(path,staged/name)
            shutil.copyfile(entry,staged/entry.name)
            main=root/'main.c';main.write_text('extern int entry(void); int main(void){return entry()==42?0:1;}')
            subprocess.run(['cc',str(main),str(staged/'libentry.so'),'-Wl,-rpath,$ORIGIN','-Wl,-rpath-link,'+str(staged),'-o',str(staged/'probe')],check=True,capture_output=True)
            subprocess.run([str(staged/'probe')],check=True,capture_output=True)


if __name__=='__main__':unittest.main()
