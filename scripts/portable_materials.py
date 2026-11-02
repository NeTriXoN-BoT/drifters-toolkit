"""Verify and assemble actual corresponding-source materials without extracting them.

Upstream archives remain intact, including their original source documentation.
This companion is separate from the runtime/reviewer ZIP; it is not a promise
to provide missing source later. All download/build scratch belongs in OS temp.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
from release_layout import canonical_name, read_plain

MAX_FILE=4*1024*1024*1024
BUILD_HELPERS=('package_portable_linux.py','portable_materials.py','generate_portable_manifest.py','release_layout.py','bootstrap_portable_tools.py','build_fetch.py','bootstrap_qt_sdk.py','qt_sdk_lock.json','bootstrap_qt_sources.py','qt_sources_lock.json','bootstrap_openssl.py','seal_portable_linux.py','package_check.py','release_signing.py','release_key.json','bootstrap_archive.py','archive_sources_lock.json','archive_decoder_limits.h')
BUILD_HELPERS+=('bootstrap_qt_webengine_security.py','qt_webengine_security_lock.json')
BUILD_HELPERS+=('resume_qt_webengine_security.py','qt_webengine_resume_lock.json','qt_webengine_patched_files.json','qt_webengine_final_files.json')
BUILD_HELPERS+=('qt_webengine_followup.py','qt_webengine_followup_lock.json')
BUILD_HELPERS+=('qt_webengine_incremental.py','qt_webengine_incremental_lock.json')
BUILD_HELPERS+=('qt_webengine_residual.py','qt_webengine_residual_lock.json')
BUILD_HELPERS+=('qt_webengine_vendor_followup.py','qt_webengine_vendor_followup_lock.json')
BUILD_HELPERS+=('linux_build_storage.py','save_codec_materials.py','save_codec_lock.json')

def identity(path, maximum=MAX_FILE):
    path=Path(path).absolute()
    for parent in (path,*path.parents):
        info=parent.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
            raise RuntimeError('A source-material path is redirected: '+path.name)
    before=path.stat()
    if not stat.S_ISREG(before.st_mode) or before.st_nlink!=1 or not 0<=before.st_size<=maximum:
        raise RuntimeError('A source-material file is not an independent bounded file: '+path.name)
    # Python/Windows path.stat and fstat may expose different legacy ctime
    # meanings. Birth time is the stable creation identity there; POSIX ctime
    # remains the metadata-change observation.
    key=lambda value:(value.st_dev,value.st_ino,value.st_size,value.st_mtime_ns,
                      getattr(value,'st_birthtime_ns',0) if os.name=='nt' else value.st_ctime_ns,value.st_nlink)
    value=hashlib.sha256();count=0
    descriptor=os.open(path,os.O_RDONLY|getattr(os,'O_BINARY',0)|getattr(os,'O_NOFOLLOW',0))
    with os.fdopen(descriptor,'rb') as source:
        if key(os.fstat(source.fileno()))!=key(before):raise RuntimeError('Source material changed before reading.')
        while chunk:=source.read(1024*1024):
            count+=len(chunk)
            if count>maximum:raise RuntimeError('Source material exceeds its limit.')
            value.update(chunk)
        if key(os.fstat(source.fileno()))!=key(before):raise RuntimeError('Source material changed while reading.')
    if count!=before.st_size or key(path.stat())!=key(before):raise RuntimeError('Source material was replaced while reading.')
    return {'sha256':value.hexdigest(),'size':count}

def row(root, name, component, version='', url=''):
    canonical_name(name)
    return {'file':name,'component':component,'version':version,'url':url,**identity(Path(root)/name)}

def verify_rows(root, rows):
    if not isinstance(rows,list) or not 1<=len(rows)<=8192:raise RuntimeError('Source materials have no bounded file inventory.')
    names=set();components=set();total=0
    for entry in rows:
        if not isinstance(entry,dict):raise RuntimeError('Invalid source-material record.')
        name=entry.get('file','');canonical_name(name)
        if name in names or not isinstance(entry.get('component'),str) or not entry['component'] or len(entry['component'])>256:
            raise RuntimeError('Duplicate or unidentified source material.')
        if type(entry.get('size')) is not int or not 0<=entry['size']<=MAX_FILE or not re.fullmatch('[a-f0-9]{64}',entry.get('sha256','')):
            raise RuntimeError('Invalid source-material identity.')
        if identity(Path(root)/name)!={key:entry[key] for key in ('sha256','size')}:
            raise RuntimeError('Source material is missing or changed: '+name)
        names.add(name);components.add(entry['component']);total+=entry['size']
        if total>16*1024*1024*1024:raise RuntimeError('The source-material collection exceeds its bound.')
    return components

def verify_lock(path, required=()):
    path=Path(path).absolute();data=json.loads(read_plain(path,8*1024*1024))
    if data.get('schemaVersion')!=1:raise RuntimeError('Unsupported source-material manifest.')
    components=verify_rows(path.parent,data.get('files'))
    if not set(required)<=components:raise RuntimeError('Required corresponding source/build material is missing: '+', '.join(sorted(set(required)-components)))
    return data

def verify_runtime(path, runtime):
    path=Path(path).absolute();data=json.loads(read_plain(path,8*1024*1024))
    if data.get('schemaVersion')!=1:raise RuntimeError('Unsupported AppImage runtime provenance.')
    expected=data.get('runtime',{})
    if expected.get('file')!=Path(runtime).name or identity(runtime)!={key:expected.get(key) for key in ('sha256','size')}:
        raise RuntimeError('The rebuilt AppImage runtime does not match its provenance.')
    records=[]
    for kind in ('sources','licenses'):
        entries=data.get(kind)
        if not isinstance(entries,list) or not entries:raise RuntimeError('The AppImage runtime is missing actual '+kind+'.')
        records.extend(entries)
    recipe=data.get('recipe')
    if not isinstance(recipe,dict):raise RuntimeError('The AppImage runtime build/relink recipe is missing.')
    records.append({**recipe,'component':recipe.get('component','appimage-runtime-build')})
    verify_rows(path.parent,records)
    return data

def verify_companion(path, descriptor, provenance, runtime_provenance=None):
    """Verify actual TAR bytes and the complete source/build closure, without extraction."""
    path=Path(path).absolute()
    if not isinstance(descriptor,dict) or descriptor.get('file')!=path.name:
        raise RuntimeError('The actual source companion is missing or changed.')
    for parent in (path,*path.parents):
        info=parent.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
            raise RuntimeError('The source companion path is redirected.')
    def stat_key(info):
        return (info.st_dev,info.st_ino,info.st_size,info.st_mtime_ns,info.st_nlink,
                getattr(info,'st_birthtime_ns',0) if os.name=='nt' else info.st_ctime_ns)
    named=path.stat();flags=os.O_RDONLY|getattr(os,'O_BINARY',0)|getattr(os,'O_NOFOLLOW',0)|getattr(os,'O_NONBLOCK',0)|getattr(os,'O_NOCTTY',0)
    class BoundedInfo(tarfile.TarInfo):
        @classmethod
        def fromtarfile(cls,archive):
            count=getattr(archive,'_toolkit_headers',0)+1;archive._toolkit_headers=count
            if count>32768:raise RuntimeError('The source companion has too many TAR headers.')
            return super().fromtarfile(archive)
        def _proc_pax(self,archive):
            if not 0<=self.size<=65536:raise RuntimeError('A source companion TAR metadata record is too large.')
            return super()._proc_pax(archive)
        def _proc_gnulong(self,archive):
            if not 0<=self.size<=65536:raise RuntimeError('A source companion TAR filename is too large.')
            return super()._proc_gnulong(archive)
        def _proc_sparse(self,archive):
            raise RuntimeError('Sparse source companion members are not supported.')
    with os.fdopen(os.open(path,flags),'rb') as source:
        before=os.fstat(source.fileno())
        if stat_key(before)!=stat_key(named) or not stat.S_ISREG(before.st_mode) or before.st_nlink!=1 or not 0<=before.st_size<=16*1024*1024*1024:
            raise RuntimeError('The source companion changed before reading.')
        digest=hashlib.sha256();count=0
        while chunk:=source.read(1024*1024):
            count+=len(chunk)
            if count>16*1024*1024*1024:raise RuntimeError('The source companion exceeds its safe byte limit.')
            digest.update(chunk)
        if {'sha256':digest.hexdigest(),'size':count}!={key:descriptor.get(key) for key in ('sha256','size')}:
            raise RuntimeError('The actual source companion is missing or changed.')
        source.seek(0)
        with tarfile.open(fileobj=source,mode='r:',tarinfo=BoundedInfo) as archive:
            observed,manifest=_companion_members(archive,source)
        after=os.fstat(source.fileno())
        if stat_key(after)!=stat_key(before) or after.st_ctime_ns!=before.st_ctime_ns or stat_key(path.lstat())!=stat_key(before):
            raise RuntimeError('The source companion changed while being verified.')
    return _companion_closure(path,descriptor,provenance,runtime_provenance,observed,manifest)

def _companion_members(archive,source):
    observed={};manifest=None;total=0;folded=set()
    for member in archive:
        name=member.name;canonical_name(name)
        if not member.isfile() or member.sparse is not None or name.casefold() in folded or len(observed)>=8193 or not 0<=member.size<=MAX_FILE:
            raise RuntimeError('The source companion contains an unsafe, duplicate or excessive member.')
        folded.add(name.casefold());total+=member.size
        if total>16*1024*1024*1024:raise RuntimeError('The source companion exceeds its total size bound.')
        sha256=hashlib.sha256();sha512=hashlib.sha512();count=0;content=bytearray()
        with archive.extractfile(member) as stream:
            while chunk:=stream.read(1024*1024):
                count+=len(chunk);sha256.update(chunk);sha512.update(chunk)
                if count>member.size:raise RuntimeError('A source companion member grew beyond its declared size.')
                if name in ('SOURCE-MATERIALS.json','save-codec/SOURCE-MANIFEST.json'):
                    if count>8*1024*1024:raise RuntimeError('The source companion inventory exceeds its bound.')
                    content.extend(chunk)
        if count!=member.size:raise RuntimeError('A source companion member is incomplete.')
        if name=='SOURCE-MATERIALS.json':
            def unique(pairs):
                result={}
                for key,value in pairs:
                    if key in result:raise RuntimeError('The source companion inventory has duplicate fields.')
                    result[key]=value
                return result
            manifest=json.loads(content,object_pairs_hook=unique)
        else:
            observed[name]={'size':count,'sha256':sha256.hexdigest(),'sha512':sha512.hexdigest()}
            if name=='save-codec/SOURCE-MANIFEST.json':observed[name]['sourceManifest']=bytes(content)
    source.seek(archive.offset)
    while chunk:=source.read(1024*1024):
        if any(chunk):raise RuntimeError('Unexpected trailing content follows the source companion.')
    return observed,manifest

def _companion_closure(path,descriptor,provenance,runtime_provenance,observed,manifest):
    if not isinstance(manifest,dict) or manifest.get('schemaVersion')!=1 or manifest.get('kind')!='actual-corresponding-source' or not isinstance(manifest.get('files'),list):
        raise RuntimeError('The source companion inventory is missing or invalid.')
    rows=manifest['files'];names=set();components=set();by_component={}
    for entry in rows:
        if not isinstance(entry,dict):raise RuntimeError('Invalid source companion record.')
        name=entry.get('file','');canonical_name(name);actual=observed.get(name)
        if name in names or actual is None or any(entry.get(key)!=actual[key] for key in ('size','sha256')) or not isinstance(entry.get('component'),str) or not entry['component']:
            raise RuntimeError('The source companion files differ from their inventory.')
        names.add(name);components.add(entry['component']);by_component.setdefault(entry['component'],[]).append(entry)
    if names!=set(observed) or not isinstance(manifest.get('components'),list) or len(manifest['components'])!=len(components) or set(manifest['components'])!=components or set(descriptor.get('components',[]))!=components:
        raise RuntimeError('The source companion component/file inventory is incomplete.')
    if not {'application','library-replacement','portable-build-tools'}<=components:
        raise RuntimeError('The application source and build/replacement instructions are required.')
    from bootstrap_qt_sources import source_lock
    qt=source_lock()
    if provenance.get('qt',{}).get('version')!=qt['qtVersion']:
        raise RuntimeError('The Qt source companion does not match the packaged runtime version.')
    for pinned in qt['archives']:
        candidates=by_component.get(pinned['component'],[])
        if len(candidates)!=1:raise RuntimeError('A complete pinned Qt/ICU source archive is missing: '+pinned['component'])
        entry=candidates[0];actual=observed[entry['file']]
        if Path(entry['file']).name!=pinned['name'] or entry.get('version')!=pinned['version'] or entry.get('url')!=pinned['url'] or actual['size']!=pinned['size'] or actual[pinned['algorithm']]!=pinned['digest']:
            raise RuntimeError('Qt/ICU source bytes differ from the pinned complete archive: '+pinned['component'])
    from bootstrap_qt_webengine_security import load_lock as webengine_lock, runtime_scope, all_patch_inputs, source_inventory_identity
    webengine=webengine_lock();security=provenance.get('qt',{}).get('webengineSecurity',{})
    if (security.get('recipeRevision')!=webengine['recipeRevision']
            or security.get('patchedChromiumCommit')!=webengine['patchedChromiumCommit']
            or security.get('securityLock')!=identity(Path(__file__).with_name('qt_webengine_security_lock.json'))
            or security.get('recipe')!=identity(Path(__file__).with_name('bootstrap_qt_webengine_security.py'))
            or security.get('storageHelper')!=identity(Path(__file__).with_name('linux_build_storage.py'))
            or security.get('runtimeScope')!=runtime_scope()
            or security.get('compatibilityBackports')!=webengine['compatibilityBackports']
            or security.get('sourceInventories')!=source_inventory_identity()
            or security.get('patches')!=[{key:entry[key] for key in ('commit','sha256','size')} for entry in webengine['patches']]):
        raise RuntimeError('The source companion requires the current completed WebEngine security build identity.')
    for patch in all_patch_inputs(webengine):
        relative=webengine['patchDirectory']+'/'+patch['name']
        if relative not in observed or any(observed[relative][key]!=patch[key] for key in ('sha256','size')):
            raise RuntimeError('An exact upstream WebEngine security patch is missing or changed: '+patch['name'])
    from resume_qt_webengine_security import load_resume_inputs, verify_continuation
    resume,_,_=load_resume_inputs();relative=resume['configuredRecipeFile']
    if relative not in observed or any(observed[relative][key]!=resume['configuredRecipe'][key] for key in ('sha256','size')):
        raise RuntimeError('The exact original WebEngine configure recipe source is missing or changed.')
    if 'continuation' in security:verify_continuation(security['continuation'])
    from qt_webengine_followup import verify_public as verify_followup
    followup=verify_followup(security.get('upstreamFollowup'))
    from qt_webengine_incremental import verify_public as verify_incremental
    incremental=verify_incremental(security.get('incrementalDelta'))
    from qt_webengine_residual import verify_public as verify_residual
    residual=verify_residual(security.get('residualDelta'))
    from qt_webengine_vendor_followup import verify_public as verify_vendor_followup
    vendor_followup=verify_vendor_followup(security.get('vendorFollowup'))
    if (incremental['priorOutputs']!=followup['outputs']
            or residual['priorOutputs']!=incremental['outputs']
            or vendor_followup['priorOutputs']!=residual['outputs']
            or vendor_followup['outputs']!=security.get('outputs')):
        raise RuntimeError('The source companion incremental/runtime history disagrees.')
    from qt_webengine_followup import verify_inputs as followup_inputs, all_patch_inputs as followup_patches
    followup_data=followup_inputs()
    for patch in followup_patches(followup_data):
        relative=followup_data['patchDirectory']+'/'+patch['name']
        if relative not in observed or any(observed[relative][key]!=patch[key] for key in ('sha256','size')):
            raise RuntimeError('An exact upstream WebEngine follow-up input is missing or changed: '+patch['name'])
    from qt_webengine_incremental import verify_inputs as incremental_inputs, all_patch_inputs as incremental_patches
    incremental_data=incremental_inputs()
    for patch in incremental_patches(incremental_data):
        relative=incremental_data['patchDirectory']+'/'+patch['name']
        if relative not in observed or any(observed[relative][key]!=patch[key] for key in ('sha256','size')):
            raise RuntimeError('An exact incremental WebEngine source input is missing or changed: '+patch['name'])
    from qt_webengine_residual import verify_inputs as residual_inputs, all_patch_inputs as residual_patches
    residual_data=residual_inputs()
    for patch in residual_patches(residual_data):
        relative=residual_data['patchDirectory']+'/'+patch['name']
        if relative not in observed or any(observed[relative][key]!=patch[key] for key in ('sha256','size')):
            raise RuntimeError('An exact residual WebEngine source input is missing or changed: '+patch['name'])
    from qt_webengine_vendor_followup import verify_inputs as vendor_followup_inputs, all_patch_inputs as vendor_followup_patches
    vendor_followup_data=vendor_followup_inputs()
    for patch in vendor_followup_patches(vendor_followup_data):
        relative=vendor_followup_data['patchDirectory']+'/'+patch['name']
        if relative not in observed or any(observed[relative][key]!=patch[key] for key in ('sha256','size')):
            raise RuntimeError('An exact vendor-followup WebEngine source input is missing or changed: '+patch['name'])
    from bootstrap_openssl import SOURCE as ssl_source, VERSION as ssl_version
    ssl=provenance.get('openssl',{})
    candidates=by_component.get('openssl',[])
    if ssl.get('version')!=ssl_version or ssl.get('source')!=ssl_source or len(candidates)!=1:
        raise RuntimeError('The exact OpenSSL corresponding source is missing.')
    entry=candidates[0]
    if Path(entry['file']).name!=ssl_source['name'] or entry.get('version')!=ssl_version or entry.get('url')!=ssl_source['url'] or any(observed[entry['file']][key]!=ssl_source[key] for key in ('size','sha256')):
        raise RuntimeError('OpenSSL source bytes do not match the packaged runtime.')
    from bootstrap_archive import source_lock, public_provenance
    if provenance.get('archiveDecoders')!=public_provenance():raise RuntimeError('The static archive decoder source/build identity is missing.')
    for source in source_lock():
        entries=by_component.get('archive-decoder:'+source['dependency'],[])
        if len(entries)!=1:raise RuntimeError('A complete archive decoder source is missing: '+source['dependency'])
        entry=entries[0]
        if (Path(entry['file']).name!=source['name'] or entry.get('version')!=source['version'] or entry.get('url')!=source['url']
                or any(observed[entry['file']][key]!=source[key] for key in ('size','sha256'))):
            raise RuntimeError('Archive decoder source bytes do not match the runtime: '+source['dependency'])
    if 'saveCodec' in provenance:
        from save_codec_materials import verify_source_records
        if 'save-codec' not in components:raise RuntimeError('The SaveCodec corresponding-source component is missing.')
        verify_source_records(observed)
    packages=provenance.get('packages')
    if not isinstance(packages,list) or not packages:raise RuntimeError('The exact distribution source closure is missing.')
    for package in packages:
        component='debian:'+package['sourcePackage'];entries=by_component.get(component,[])
        if not entries or any(entry.get('version')!=package['sourceVersion'] for entry in entries) or not any(entry['file'].endswith('.dsc') for entry in entries) or not any('.tar.' in entry['file'] for entry in entries):
            raise RuntimeError('Exact distribution source/control material is missing: '+component)
    scripts=Path(__file__).resolve().parent
    for name in BUILD_HELPERS:
        relative='scripts/'+name
        expected=identity(scripts/name)
        if relative not in observed or any(observed[relative][key]!=expected[key] for key in ('sha256','size')):
            raise RuntimeError('A current source rebuild helper is missing or changed: '+name)
    if runtime_provenance is not None:
        runtime_rows=[*runtime_provenance.get('sources',[]),runtime_provenance.get('recipe',{})]
        for expected in runtime_rows:
            component=expected.get('component','appimage-runtime-build')
            if not any(Path(entry['file']).name==Path(expected.get('file','')).name and all(observed[entry['file']][key]==expected.get(key) for key in ('sha256','size')) and all(key not in expected or entry.get(key)==expected[key] for key in ('version','url')) for entry in by_component.get(component,[])):
                raise RuntimeError('The AppImage runtime source/build closure is missing: '+component)
    return manifest

def collect_debian(provenance,destination):
    """APT verifies repository metadata; exact source=version prevents substitution."""
    data=json.loads(read_plain(provenance,8*1024*1024));destination=Path(destination).absolute()
    if destination.exists():raise RuntimeError('Use a fresh source-material output directory.')
    destination.mkdir(parents=True);records=[]
    pairs=sorted({(entry['sourcePackage'],entry['sourceVersion']) for entry in data['packages']})
    for name,version in pairs:
        if not re.fullmatch('[a-z0-9][a-z0-9+.-]*',name) or not re.fullmatch('[A-Za-z0-9.+:~_-]+',version):raise RuntimeError('Invalid exact Debian source identity.')
        with tempfile.TemporaryDirectory(prefix='drifter-debian-source-') as temporary:
            # deb-src indexes must have been configured and authenticated by
            # the build environment. Never enable unauthenticated repositories.
            subprocess.run(['apt-get','--download-only','--only-source','source',name+'='+version],cwd=temporary,check=True)
            inputs=sorted(Path(temporary).iterdir())
            if not any(path.suffix=='.dsc' for path in inputs):raise RuntimeError('APT did not supply the source control record: '+name)
            for source in inputs:
                if not source.is_file() or source.is_symlink():raise RuntimeError('Unexpected APT source output.')
                relative=name+'/'+source.name;target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True)
                with source.open('rb') as src,target.open('xb') as dst:shutil.copyfileobj(src,dst,1024*1024)
                target.chmod(0o644)
                records.append(row(destination,relative,'debian:'+name,version,'APT authenticated source='+name+'='+version))
    lock={'schemaVersion':1,'kind':'debian-corresponding-source','files':records}
    (destination/'source-materials.json').write_text(json.dumps(lock,indent=2)+'\n')
    verify_lock(destination/'source-materials.json')
    return destination/'source-materials.json'

def pack(locks, application, instructions, destination, save_codec_source=None):
    """Store already-compressed complete upstream archives unchanged in a tar."""
    output=Path(destination).absolute()
    sidecars=(Path(str(output)+'.json'),Path(str(output)+'.sha256'))
    if any(path.exists() or path.is_symlink() for path in (output,*sidecars)):
        raise RuntimeError('An existing source companion or sidecar is preserved.')
    inputs=[];records=[];components=set()
    from bootstrap_archive import verify_sources
    archive_root=Path(__file__).resolve().parents[1]/'vendor/archive-sources'
    for source in verify_sources(archive_root):
        name='archive-decoders/'+source['name'];component='archive-decoder:'+source['dependency']
        inputs.append((archive_root/source['name'],name))
        records.append(row(archive_root,source['name'],component,source['version'],source['url']))
        records[-1]['file']=name;components.add(component)
    for number,lock in enumerate(locks):
        data=verify_lock(lock)
        for entry in data['files']:
            name='materials-'+str(number)+'/'+entry['file'];canonical_name(name)
            inputs.append((Path(lock).absolute().parent/entry['file'],name))
            records.append({**entry,'file':name});components.add(entry['component'])
    for source,name,component in ((application,'application/Toolkit-Source.zip','application'),(instructions,'BUILD.txt','library-replacement')):
        inputs.append((Path(source).absolute(),name));records.append({'file':name,'component':component,**identity(source)})
        components.add(component)
    scripts=Path(__file__).resolve().parent
    for name in BUILD_HELPERS:
        source=scripts/name;relative='scripts/'+name;inputs.append((source,relative))
        records.append({'file':relative,'component':'portable-build-tools',**identity(source)});components.add('portable-build-tools')
    from bootstrap_qt_webengine_security import verify_inputs as verify_webengine_inputs, all_patch_inputs
    webengine=verify_webengine_inputs();source_root=scripts.parent
    for patch in all_patch_inputs(webengine):
        relative=webengine['patchDirectory']+'/'+patch['name'];source=source_root/relative
        inputs.append((source,relative));records.append({'file':relative,'component':'qtwebengine-security-patches',**identity(source)})
        components.add('qtwebengine-security-patches')
    from qt_webengine_followup import verify_inputs as followup_inputs, all_patch_inputs as followup_patches
    followup=followup_inputs()
    for patch in followup_patches(followup):
        relative=followup['patchDirectory']+'/'+patch['name'];source=source_root/relative
        inputs.append((source,relative));records.append({'file':relative,'component':'qtwebengine-upstream-followup',**identity(source)})
        components.add('qtwebengine-upstream-followup')
    from qt_webengine_incremental import verify_inputs as incremental_inputs, all_patch_inputs as incremental_patches
    incremental=incremental_inputs()
    for patch in incremental_patches(incremental):
        relative=incremental['patchDirectory']+'/'+patch['name'];source=source_root/relative
        inputs.append((source,relative));records.append({'file':relative,'component':'qtwebengine-incremental-security',**identity(source)})
        components.add('qtwebengine-incremental-security')
    from qt_webengine_residual import verify_inputs as residual_inputs, all_patch_inputs as residual_patches
    residual=residual_inputs()
    for patch in residual_patches(residual):
        relative=residual['patchDirectory']+'/'+patch['name'];source=source_root/relative
        inputs.append((source,relative));records.append({'file':relative,'component':'qtwebengine-residual-security',**identity(source)})
        components.add('qtwebengine-residual-security')
    from qt_webengine_vendor_followup import verify_inputs as vendor_followup_inputs, all_patch_inputs as vendor_followup_patches
    vendor_followup=vendor_followup_inputs()
    for patch in vendor_followup_patches(vendor_followup):
        relative=vendor_followup['patchDirectory']+'/'+patch['name'];source=source_root/relative
        inputs.append((source,relative));records.append({'file':relative,'component':'qtwebengine-vendor-followup-security',**identity(source)})
        components.add('qtwebengine-vendor-followup-security')
    from resume_qt_webengine_security import load_resume_inputs
    resume,_,_=load_resume_inputs();relative=resume['configuredRecipeFile'];source=source_root/relative
    inputs.append((source,relative));records.append({'file':relative,'component':'qtwebengine-build-continuation',**identity(source)})
    components.add('qtwebengine-build-continuation')
    if save_codec_source is not None:
        from save_codec_materials import verify_source, SOURCE_MANIFEST
        codec_root=Path(save_codec_source).absolute();codec=verify_source(codec_root)
        codec_names=[SOURCE_MANIFEST,*(entry['path'] for entry in codec['files'])]
        for name in codec_names:
            source=codec_root/name;relative='save-codec/'+name
            inputs.append((source,relative));records.append({'file':relative,'component':'save-codec',**identity(source)})
        components.add('save-codec')
    manifest={'schemaVersion':1,'kind':'actual-corresponding-source','components':sorted(components),'files':records}
    body=(json.dumps(manifest,indent=2)+'\n').encode()
    output.parent.mkdir(parents=True,exist_ok=True)
    for parent in (output.parent,*output.parent.parents):
        if parent.is_symlink():raise RuntimeError('The source companion output is redirected.')
    with tempfile.TemporaryDirectory(prefix='drifter-source-pack-') as temporary:
        staged=Path(temporary)/'sources.tar'
        with tarfile.open(staged,'w') as archive:
            for source,name in inputs:
                expected=next(record for record in records if record['file']==name)
                if identity(source)!={key:expected[key] for key in ('sha256','size')}:raise RuntimeError('Source material changed before archiving.')
                info=tarfile.TarInfo(name);info.size=expected['size'];info.mode=0o644;info.mtime=1767225600
                with source.open('rb') as stream:archive.addfile(info,stream)
            import io
            info=tarfile.TarInfo('SOURCE-MATERIALS.json');info.size=len(body);info.mode=0o644;info.mtime=1767225600;archive.addfile(info,io.BytesIO(body))
        # Publication is exclusive and copy-safe across filesystems. Only the
        # newly created incomplete output is removed on a failed publication.
        created=False
        try:
            with staged.open('rb') as source,output.open('xb') as target:
                created=True;shutil.copyfileobj(source,target,1024*1024);target.flush();os.fsync(target.fileno())
            if identity(output)!=identity(staged):raise RuntimeError('The source companion copy failed verification.')
        except BaseException:
            if created:output.unlink(missing_ok=True)
            raise
    descriptor={'file':output.name,**identity(output),'components':sorted(components)}
    for target,text in zip(sidecars,(json.dumps(descriptor,indent=2)+'\n',descriptor['sha256']+'  '+output.name+'\n')):
        with target.open('xb') as stream:
            stream.write(text.encode('utf-8'));stream.flush();os.fsync(stream.fileno())
    return descriptor

def main():
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest='mode',required=True)
    debian=commands.add_parser('debian');debian.add_argument('provenance',type=Path);debian.add_argument('destination',type=Path)
    sources=commands.add_parser('pack');sources.add_argument('--lock',type=Path,action='append',required=True);sources.add_argument('--application',type=Path,required=True);sources.add_argument('--instructions',type=Path,required=True);sources.add_argument('--output',type=Path,required=True);sources.add_argument('--save-codec-source',type=Path)
    args=parser.parse_args()
    if args.mode=='debian':print(collect_debian(args.provenance,args.destination))
    else:print(json.dumps(pack(args.lock,args.application,args.instructions,args.output,args.save_codec_source),indent=2))

if __name__=='__main__':main()
