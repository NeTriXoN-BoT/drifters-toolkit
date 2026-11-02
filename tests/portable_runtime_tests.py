"""Native Linux focused checks for compiled runtime identity and packaging policy."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
def module(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/(name+'.py'));value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value);return value

def main():
    if sys.platform!='linux':raise RuntimeError('Run these checks natively on Linux.')
    generator=module('generate_portable_manifest');checker=module('package_check');checks=0
    def check(value,message):
        nonlocal checks;checks+=1
        if not value:raise AssertionError(message)
    def rejects(call,message):
        try:call()
        except Exception:check(True,message)
        else:check(False,message)
    with tempfile.TemporaryDirectory(prefix='drifter-portable-tests-') as temporary:
        root=Path(temporary);appdir=root/'bundle';files={'usr/bin/qt.conf':b'[Paths]\nPrefix=..\n','usr/libexec/qt.conf':b'[Paths]\nPrefix=..\n','usr/libexec/QtWebEngineProcess':b'fixture helper\n','usr/lib/fakeQt.so':b'fixture library\n'}
        cli_paths={'usr/bin/qt.conf','usr/lib/libQt6Core.so.6','usr/lib/libQt6Network.so.6','usr/lib/libQt6WebSockets.so.6','usr/lib/libcrypto.so.3','usr/lib/libssl.so.3','usr/plugins/tls/libqopensslbackend.so'}
        for name in cli_paths-{'usr/bin/qt.conf'}:files[name]=b'fixture headless dependency\n'
        rows=[]
        for name,data in files.items():
            target=appdir/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data);executable=name.endswith('QtWebEngineProcess');target.chmod(0o755 if executable else 0o644)
            rows.append({'path':name,'sha256':hashlib.sha256(data).hexdigest(),'size':len(data),'executable':executable,'cli':name in cli_paths})
        manifest=root/'manifest.json';manifest.write_text(json.dumps({'schemaVersion':1,'cliReady':True,'files':rows}));header=root/'portable_runtime_manifest.hpp';generator.generate(manifest,header);check(header.is_file(),'generator must write the compiled header')
        probe=root/'probe.cpp';probe.write_text(r'''
#include "portable_runtime.hpp"
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/provider.h>
#include <openssl/ssl.h>
#include <array>
#include <cstring>
int main(int n,char** v){try{
    drifter::verifyPortableRuntime(v[1],n>2);
    drifter::requireSafeCryptoEnvironment(); // repeated startup checks remain safe
    unsigned char hash[32]{};unsigned length=0;
    const unsigned char expected[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    if(EVP_Digest("abc",3,hash,&length,EVP_sha256(),nullptr)!=1||length!=32||std::memcmp(hash,expected,32))return 8;
    EVP_PKEY_CTX* context=EVP_PKEY_CTX_new_from_name(nullptr,"EC",nullptr);
    EVP_PKEY* key=nullptr;
    if(!context||EVP_PKEY_keygen_init(context)<=0||EVP_PKEY_CTX_set_group_name(context,"prime256v1")<=0||EVP_PKEY_generate(context,&key)<=0)return 9;
    EVP_PKEY_CTX_free(context);
    EVP_MD_CTX* digest=EVP_MD_CTX_new();std::array<unsigned char,128> signature{};size_t size=signature.size();
    if(!digest||EVP_DigestSignInit(digest,nullptr,EVP_sha256(),nullptr,key)<=0||EVP_DigestSign(digest,signature.data(),&size,reinterpret_cast<const unsigned char*>("fixture"),7)<=0)return 10;
    EVP_MD_CTX_free(digest);digest=EVP_MD_CTX_new();
    if(!digest||EVP_DigestVerifyInit(digest,nullptr,EVP_sha256(),nullptr,key)<=0||EVP_DigestVerify(digest,signature.data(),size,reinterpret_cast<const unsigned char*>("fixture"),7)!=1)return 11;
    EVP_MD_CTX_free(digest);EVP_PKEY_free(key);
    SSL_CTX* tls=SSL_CTX_new(TLS_client_method());if(!tls)return 12;SSL_CTX_free(tls);
    // The built-in default provider works; a separately installed module must
    // remain outside the loader's fixed provider lookup path.
    OSSL_PROVIDER* external=OSSL_PROVIDER_load(nullptr,"legacy");
    if(external){OSSL_PROVIDER_unload(external);return 13;}ERR_clear_error();
    return 0;
}catch(...){return 7;}}
''')
        common=['g++','-std=c++17','-O1','-pthread','-I'+str(ROOT/'src'),'-I'+str(root),str(probe),str(ROOT/'src/portable_runtime.cpp'),str(ROOT/'src/platform_posix.cpp'),'-lssl','-lcrypto']
        portable=root/'portable';ordinary=root/'ordinary';subprocess.run(common+['-DDRIFTER_PORTABLE_BUILD','-o',str(portable)],check=True);subprocess.run(common+['-o',str(ordinary)],check=True)
        environment=os.environ.copy()
        for name in ('OPENSSL_CONF','OPENSSL_MODULES','OPENSSL_ENGINES'):environment.pop(name,None)
        def accepts(executable,path,cli=False,env=None):return subprocess.run([str(executable),str(path)]+(['cli'] if cli else []),check=False,env=environment if env is None else env).returncode==0
        check(accepts(portable,appdir/'usr/bin'),'exact compiled fixture must pass')
        check(accepts(portable,appdir/'usr/bin',True),'reviewed headless runtime subset must pass')
        system=root/'environment-system';system.mkdir()
        check(accepts(ordinary,system),'absent OpenSSL overrides must permit ordinary runtime verification')
        hostile=root/'untrusted-openssl.cnf';hostile.write_text('openssl_conf = untrusted\n[untrusted]\nproviders = provider_section\n[provider_section]\nlegacy = legacy_section\n[legacy_section]\nmodule = /untrusted/legacy.so\nactivate = 1\n')
        for name in ('OPENSSL_CONF','OPENSSL_MODULES','OPENSSL_ENGINES'):
            override={**environment,name:'/untrusted/runtime-override'}
            check(not accepts(portable,appdir/'usr/bin',env=override),name+' must be refused before GUI runtime hashing')
            check(not accepts(portable,appdir/'usr/bin',True,override),name+' must be refused before CLI runtime hashing')
            check(not accepts(ordinary,system,env=override),name+' must also be refused by ordinary runtime verification')
            empty={**environment,name:''}
            check(accepts(portable,appdir/'usr/bin',env=empty),'empty '+name+' must permit GUI runtime verification')
            check(accepts(portable,appdir/'usr/bin',True,empty),'empty '+name+' must permit CLI runtime verification')
            check(accepts(ordinary,system,env=empty),'empty '+name+' must permit ordinary runtime verification')
        check(not accepts(ordinary,system,env={**environment,'OPENSSL_CONF':str(hostile)}),'custom config must be refused before a provider can be selected')
        unexpected=appdir/'usr/lib/unlisted.so';unexpected.write_text('unexpected');check(not accepts(portable,appdir/'usr/bin'),'unlisted runtime code must fail');unexpected.unlink()
        qt6=appdir/'usr/bin/qt6.conf';qt6.write_text('[Paths]\nPrefix=/outside\n');check(not accepts(portable,appdir/'usr/bin'),'higher-priority versioned qt6.conf must fail');qt6.unlink()
        config=appdir/'usr/bin/qt.conf';original=config.read_bytes();config.write_bytes(original+b'changed');check(not accepts(portable,appdir/'usr/bin'),'modified configuration must fail');config.write_bytes(original)
        helper=appdir/'usr/libexec/QtWebEngineProcess';saved=helper.read_bytes();helper.unlink();check(not accepts(portable,appdir/'usr/bin'),'missing helper must fail');check(accepts(portable,appdir/'usr/bin',True),'CLI must not require the unused GUI engine helper');helper.write_bytes(saved);helper.chmod(0o755)
        helper.chmod(0o644);check(not accepts(portable,appdir/'usr/bin'),'nonexecutable helper must fail');helper.chmod(0o755)
        external=root/'outside';external.write_bytes(original);config.unlink();config.symlink_to(external);check(not accepts(portable,appdir/'usr/bin'),'symlink configuration must fail');config.unlink();config.write_bytes(original)
        alias=root/'hardlink';os.link(helper,alias);check(not accepts(portable,appdir/'usr/bin'),'hardlinked runtime must fail');alias.unlink()
        library=appdir/'usr/lib/fakeQt.so';before=library.stat();originalLibrary=library.read_bytes();library.write_bytes(b'X'+originalLibrary[1:]);os.utime(library,ns=(before.st_atime_ns,before.st_mtime_ns));check(not accepts(portable,appdir/'usr/bin'),'same-size/timestamp tampering must fail');library.write_bytes(originalLibrary)
        library.write_bytes(b'X'+originalLibrary[1:]);check(accepts(portable,appdir/'usr/bin',True),'CLI must not hash unused GUI-only libraries');library.write_bytes(originalLibrary)
        ssl=appdir/'usr/lib/libssl.so.3';originalSsl=ssl.read_bytes();ssl.write_bytes(b'X'+originalSsl[1:]);check(not accepts(portable,appdir/'usr/bin',True),'CLI must reject tampered dynamically loaded OpenSSL');ssl.write_bytes(originalSsl)
        config.write_bytes(original+b'changed');check(not accepts(portable,appdir/'usr/bin',True),'CLI must verify shared Qt configuration');config.write_bytes(original)
        unexpected.write_text('unexpected');check(not accepts(portable,appdir/'usr/bin',True),'CLI must still refuse injected unlisted runtime code');unexpected.unlink()
        redirected=root/'redirected';redirected.symlink_to(appdir,target_is_directory=True);check(not accepts(portable,redirected/'usr/bin'),'redirected bundle ancestor must fail')
        baseline=root/'system/bin';baseline.mkdir(parents=True);check(accepts(ordinary,baseline),'ordinary system build must permit no qt.conf');(baseline/'qt.conf').write_text('untrusted');check(not accepts(ordinary,baseline),'ordinary system build must refuse adjacent qt.conf');check(accepts(portable,appdir/'usr/bin'),'restored exact bytes must pass')
        (baseline/'qt.conf').unlink();(baseline/'qt6.conf').write_text('untrusted');check(not accepts(ordinary,baseline),'ordinary system build must refuse versioned qt6.conf')
        for change in ['traversal','absolute','duplicate','bad-hash','negative-size','huge-size','wrong-bool','missing-required','wrong-scope','wrong-cli-bool','incomplete-cli','gui-in-cli','unready-cli']:
            bad=json.loads(manifest.read_text())
            if change=='traversal':bad['files'][0]['path']='usr/bin/../qt.conf'
            elif change=='absolute':bad['files'][0]['path']='/usr/bin/qt.conf'
            elif change=='duplicate':bad['files'].append(bad['files'][0])
            elif change=='bad-hash':bad['files'][0]['sha256']='0'*63
            elif change=='negative-size':bad['files'][0]['size']=-1
            elif change=='huge-size':bad['files'][0]['size']=513*1024*1024
            elif change=='wrong-bool':bad['files'][0]['executable']='true'
            elif change=='missing-required':bad['files'][0]['path']='usr/lib/other.so'
            elif change=='wrong-scope':bad['files'][0]['path']='usr/bin/README.md'
            elif change=='wrong-cli-bool':bad['files'][0]['cli']='true'
            elif change=='incomplete-cli':next(row for row in bad['files'] if row['path']=='usr/lib/libssl.so.3')['cli']=False
            elif change=='gui-in-cli':next(row for row in bad['files'] if row['path']=='usr/libexec/QtWebEngineProcess')['cli']=True
            elif change=='unready-cli':bad['cliReady']=False
            invalid=root/'invalid.json';invalid.write_text(json.dumps(bad));rejects(lambda:generator.generate(invalid,root/'invalid.hpp'),change+' inventory must fail')
        legacy=json.loads(manifest.read_text());legacy.pop('cliReady')
        for row in legacy['files']:row.pop('cli')
        legacyManifest=root/'legacy.json';legacyManifest.write_text(json.dumps(legacy));generator.generate(legacyManifest,header)
        legacyProbe=root/'legacy';subprocess.run(common+['-DDRIFTER_PORTABLE_BUILD','-o',str(legacyProbe)],check=True)
        check(accepts(legacyProbe,appdir/'usr/bin'),'legacy GUI-only portable inventory must remain valid')
        check(not accepts(legacyProbe,appdir/'usr/bin',True),'CLI must refuse a bundle with no reviewed CLI membership')
        inspect=checker.Inspection(True)
        for name in ['usr/resources/qtwebengine_resources.pak','usr/resources/qtwebengine_devtools_resources.pak','AppDir/usr/resources/qtwebengine_resources_100p.pak','AppDir/usr/resources/qtwebengine_resources_200p.pak','usr/translations/qtwebengine_locales/en-US.pak']:check(inspect.name(name)==name,'exact Qt resource exception must pass')
        for name in ['usr/resources/mod.pak','usr/resources/BendGame-WindowsNoEditor.pak','usr/translations/qtwebengine_locales/../mod.pak','other/qtwebengine_resources.pak']:rejects(lambda:inspect.name(name),'game or misplaced PAK must fail')
    print(str(checks)+' native portable runtime/packaging checks passed.')
if __name__=='__main__':main()
