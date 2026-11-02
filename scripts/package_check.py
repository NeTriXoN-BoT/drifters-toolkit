"""Inspect release ZIP, universal AppImage ZIP and tar contents for public-layout/privacy policy, without extraction.

Linux ships only as one universal AppImage. Distribution-specific packages (.deb, .rpm, system-linked
folders) are refused rather than inspected."""
from pathlib import Path, PurePosixPath
import argparse
import bz2
import getpass
import gzip
import hashlib
import io
import json
import lzma
import re
import stat
import tarfile
import zipfile
from release_layout import WINDOWS_RUNTIME, SAVE_CODEC_WINDOWS_FILES, SAVE_CODEC_POINTER, COMMON_NOTICES, LINUX_NOTICES, PUBLIC_UI, REVIEW_OUTER_FILES, RELEASE_NOTES_NAME, app_version, canonical_name, read_plain, release_notes

MAX_MEMBER = 256 * 1024 * 1024
MAX_QT_WEBENGINE_CORE = 512 * 1024 * 1024
MAX_TAR = 512 * 1024 * 1024
MAX_TOTAL = 1024 * 1024 * 1024
MAX_ENTRIES = 16384
def branding_pattern(encoding):
    boundary = b'[a-z0-9_]' if encoding == 'utf-8' else b'[a-z0-9_]\x00' if encoding == 'utf-16le' else b'\x00[a-z0-9_]'
    words = b'|'.join(re.escape(word.encode(encoding)) for word in ('openai','chatgpt','codex','claude'))
    return re.compile(b'(?<!'+boundary+b')(?:'+words+b')(?!'+boundary+b')',re.I)
BRANDING_BYTES = tuple(branding_pattern(encoding) for encoding in ('utf-8','utf-16le','utf-16be'))
BRANDING_LITERALS = tuple(word.encode(encoding) for encoding in ('utf-8','utf-16le','utf-16be') for word in ('openai','chatgpt','codex','claude'))
QT_PAK = re.compile(r'(?:^|/)usr/(?:resources/qtwebengine_(?:devtools_resources|resources(?:_100p|_200p)?)\.pak|translations/qtwebengine_locales/[A-Za-z0-9_-]+\.pak)$')

class Inspection:
    def __init__(self, public, budget=None, appdir_prefix=None, pinned_codec_source=False):
        self.public = public
        self.pinned_codec_source = pinned_codec_source and not public
        if appdir_prefix not in (None, '', 'DriftersToolkit.AppDir/'):
            raise RuntimeError('Unknown AppDir inspection context.')
        self.appdir_prefix = appdir_prefix
        self.bytes = self.entries = 0
        self.budget=budget if budget is not None else [0,0]
        user = getpass.getuser()
        prefixes = ['C:/Users/'+user+'/', 'C:\\Users\\'+user+'\\', '/home/'+user+'/', '/Users/'+user+'/']
        self.probes = [prefix.encode(encoding).lower() for prefix in prefixes
                       for encoding in ('utf-8', 'utf-16le', 'utf-16be')]

    def member_limit(self, label):
        # The source-built Core exceeds the general archive-member bound. Only
        # this exact file in an explicitly inspected AppDir gets the larger cap.
        if self.appdir_prefix is not None and label == self.appdir_prefix+'usr/lib/libQt6WebEngineCore.so.6':
            return MAX_QT_WEBENGINE_CORE
        return MAX_MEMBER

    def consume(self, size, label):
        if size < 0 or size > self.member_limit(label):
            raise RuntimeError('Archive member exceeds the inspection limit: '+label)
        self.bytes += size
        self.entries += 1
        self.budget[0]+=size;self.budget[1]+=1
        if self.budget[0] > MAX_TOTAL or self.budget[1] > MAX_ENTRIES:
            raise RuntimeError('Archive exceeds the total inspection limit: '+label)

    def name(self, name, directory=False):
        # tar uses an optional ./ prefix. Normalize only that prefix, never a ..
        # segment, and use POSIX archive paths consistently on Windows and Linux.
        while name.startswith('./'):
            name = name[2:]
        if directory and name.endswith('/'): name=name[:-1]
        canonical_name(name)
        parts = PurePosixPath(name).parts
        if (not name or name.startswith(('/', '\\')) or '..' in parts or '\\' in name
                or ':' in name or '\0' in name):
            raise RuntimeError('Unsafe archive entry: '+name)
        lower = [part.lower() for part in parts]
        # This is Rust's checked-in Win32 FFI source, not credential data. The
        # exact complete source manifest and every member hash are still required
        # before this source-only container can pass; no runtime exception exists.
        windows_ffi = self.pinned_codec_source and name == 'DriftersToolkit-SaveCodec-source/vendor/windows-sys-0.61.2/src/Windows/Win32/Security/Credentials/mod.rs'
        if not windows_ffi and any(part in {'sandbox-state', 'webview', 'downloads', 'profiles', 'logs', '__pycache__', 'credentials', '.git'} for part in lower):
            raise RuntimeError('A user-state directory was included: '+name)
        if name.lower().endswith(('.dpapi', '.pdb', '.log', '.part', '.dmp', '.sav', '.pfx', '.pem', '.key', '.sqlite', '.db')) or name.lower().endswith('.pak') and not QT_PAK.search(name):
            raise RuntimeError('Private/debug/game file included: '+name)
        if self.public and (lower[-1].startswith('readme') or 'docs' in lower):
            raise RuntimeError('Development notes were included in a runtime package: '+name)
        return name

    def content(self, name, content):
        folded = content.lower()
        if any(probe in folded for probe in self.probes):
            raise RuntimeError('A local account path was included in '+name)
        # Required upstream notices retain their original company/product names.
        # Only exact named markers in application content are checked, never 'AI'.
        if self.public and not QT_PAK.search(name) and 'licenses' not in [part.lower() for part in PurePosixPath(name).parts] and PurePosixPath(name).name!='LICENSES.txt' and any(word in folded for word in BRANDING_LITERALS):
            for pattern in BRANDING_BYTES:
                if pattern.search(content):
                    raise RuntimeError('A private assistant/branding reference was included in '+name)
        if name == 'config/nexus.json' or name.lower().endswith('/config/nexus.json'):
            config = json_object(content,name)
            if set(config) != {'applicationSlug'} or not isinstance(config['applicationSlug'], str) or not re.fullmatch('[A-Za-z0-9_-]{0,100}',config['applicationSlug']):
                raise RuntimeError('Nexus configuration has unexpected fields.')

def json_object(content, label):
    if len(content)>2*1024*1024: raise RuntimeError('JSON metadata is too large: '+label)
    def unique(pairs):
        value={}
        for key,item in pairs:
            if key in value: raise RuntimeError('Duplicate JSON metadata key: '+label)
            value[key]=item
        return value
    value=json.loads(content,object_pairs_hook=unique)
    if not isinstance(value,dict): raise RuntimeError('Expected a JSON object: '+label)
    return value

SIGNATURE='RELEASE-SIGNATURE.json'

def verify_signature(files, root):
    """A signed release must verify with the compiled-in public key and carry this version."""
    if root+'/'+SIGNATURE not in files:return None
    from release_signing import ROOTS,verify_contents
    platform=next((key for key,value in ROOTS.items() if value==root),None)
    if platform is None:raise RuntimeError('Only a Windows or Linux runtime ZIP can carry a release signature.')
    version=verify_contents(platform,{name[len(root)+1:]:data for name,data in files.items()})
    if version!=app_version():raise RuntimeError('The release signature names another version than src/version.hpp.')
    return version

def verify_checksums(files, root):
    label=root+'/SHA256SUMS.json'
    if label not in files: raise RuntimeError('The package checksum manifest is missing.')
    checks=json_object(files[label],label)
    expected={name[len(root)+1:]:data for name,data in files.items() if name not in (label,root+'/'+SIGNATURE)}
    if set(checks)!=set(expected): raise RuntimeError('The checksum manifest does not cover the exact package contents.')
    for name,digest in checks.items():
        canonical_name(name)
        if not isinstance(digest,str) or not re.fullmatch('[a-f0-9]{64}',digest) or hashlib.sha256(expected[name]).hexdigest()!=digest:
            raise RuntimeError('A package checksum does not match: '+name)

def verify_sidecar(path):
    sidecar=Path(str(path)+'.sha256')
    lines=read_plain(sidecar,2048).decode('utf-8').splitlines()
    expected=hashlib.sha256(read_plain(path,MAX_TOTAL)).hexdigest()+'  '+path.name
    if lines!=[expected]: raise RuntimeError('The archive SHA-256 sidecar does not match.')

def verify_rebuilt_runtime_bindings(qt,files,expected,locally_modified=False):
    """Join immutable pre-relocation provenance to the final runtime manifest."""
    from package_portable_linux import rebuilt_runtime_targets
    targets=rebuilt_runtime_targets(qt['webengineSecurity']['outputs'])
    rows=qt.get('rebuiltRuntimeFiles')
    if not isinstance(rows,list) or len(rows)!=len(targets):
        raise RuntimeError('The repaired WebEngine runtime bindings are missing or incomplete.')
    seen=set()
    for row in rows:
        if not isinstance(row,dict) or set(row)!={'source','path','original','staged','localReplacement'}:
            raise RuntimeError('A repaired WebEngine runtime binding is invalid.')
        name=row['path']
        if not isinstance(name,str) or name not in targets or name in seen or type(row['localReplacement'])is not bool:
            raise RuntimeError('A repaired WebEngine runtime binding is unsafe or duplicated.')
        pinned=targets[name];original={key:pinned[key] for key in ('sha256','size')}
        if any(not isinstance(row.get(field),dict) or set(row[field])!={'sha256','size'}
               or type(row[field].get('size'))is not int or not 0<row[field]['size']<=512*1024*1024
               or not isinstance(row[field].get('sha256'),str) or not re.fullmatch('[a-f0-9]{64}',row[field]['sha256'])
               for field in ('original','staged')):
            raise RuntimeError('A repaired WebEngine binding has an invalid byte identity.')
        if row['source']!=pinned['source'] or row['original']!=original or name not in qt['runtimeFiles'] or name not in expected:
            raise RuntimeError('A runtime binding does not identify the repaired source output: '+name)
        staged={key:expected[name][key] for key in ('sha256','size')}
        if row['staged']!=staged or name not in files or len(files[name])!=staged['size'] or hashlib.sha256(files[name]).hexdigest()!=staged['sha256']:
            raise RuntimeError('A repaired WebEngine runtime binding differs from staged bytes: '+name)
        if row['localReplacement']:
            if locally_modified is not True:raise RuntimeError('A local library replacement was not explicitly reindexed.')
        elif not name.startswith(('usr/lib/','usr/libexec/')) and staged!=original:
            raise RuntimeError('A repaired WebEngine resource changed during staging: '+name)
        seen.add(name)

def verify_portable(files):
    from generate_portable_manifest import render
    manifest=json_object(files['runtime-manifest.json'],'runtime-manifest.json')
    render(manifest)
    expected={row['path']:row for row in manifest['files']}
    for name,row in expected.items():
        if name not in files or len(files[name])!=row['size'] or hashlib.sha256(files[name]).hexdigest()!=row['sha256']:
            raise RuntimeError('A staged runtime file does not match its manifest: '+name)
    required={'AppRun','DriftersToolkit.desktop','DriftersToolkit.png','DEPENDENCY-SOURCES.json','runtime-manifest.json',
              'usr/bin/DriftersToolkit','usr/bin/DriftersToolkitCLI','usr/bin/catalog/catalog.json','usr/bin/config/nexus.json',
              *('usr/bin/ui/'+name for name in PUBLIC_UI),
              *('licenses/'+name for name in COMMON_NOTICES),'licenses/miniz-LICENSE'}
    provenance=json_object(files['DEPENDENCY-SOURCES.json'],'DEPENDENCY-SOURCES.json')
    if type(provenance.get('schemaVersion')) is not int or provenance['schemaVersion']!=2:
        raise RuntimeError('The current release requires complete maintained dependency and WebEngine security provenance.')
    packages=provenance.get('packages')
    if not isinstance(packages,list) or not 1<=len(packages)<=4096:raise RuntimeError('The runtime source inventory is missing.')
    for package in packages:
        if not isinstance(package,dict) or not all(isinstance(package.get(field),str) and 0<len(package[field])<=256 for field in ('package','version','sourcePackage','sourceVersion')):
            raise RuntimeError('The runtime source package identity is invalid.')
        name=package['package'].split(':',1)[0]
        if not re.fullmatch('[a-z0-9][a-z0-9+.-]*',name):raise RuntimeError('A runtime package name is invalid.')
        required.add('licenses/'+name+'-copyright')
    if provenance.get('schemaVersion')==2:
        from bootstrap_archive import public_provenance
        if provenance.get('archiveDecoders')!=public_provenance():raise RuntimeError('The pinned static archive decoder provenance is missing or changed.')
        if provenance.get('replacementFile')!='licenses/LIBRARY_REPLACEMENT.txt':raise RuntimeError('The library replacement instructions are missing.')
        required.update('licenses/'+name for name in LINUX_NOTICES)
        required.add(provenance['replacementFile'])
        qt=provenance.get('qt')
        if not isinstance(qt,dict) or set(qt)!={'version','origin','archives','noticeFiles','runtimeFiles','webengineSecurity','rebuiltRuntimeFiles'} or qt['version']!='6.11.3' or qt['origin']!='official-qt-sdk-with-pinned-vendor-webengine-backports':raise RuntimeError('The maintained Qt SDK and updated WebEngine identity is missing.')
        from bootstrap_qt_webengine_security import load_lock as webengine_lock, file_record as webengine_record
        webengine=webengine_lock();security=qt['webengineSecurity']
        from bootstrap_qt_webengine_security import runtime_scope, verify_output_scope, source_inventory_identity
        pinned={'recipeRevision':webengine['recipeRevision'],
                'securityLock':webengine_record(Path(__file__).with_name('qt_webengine_security_lock.json')),
                'recipe':webengine_record(Path(__file__).with_name('bootstrap_qt_webengine_security.py')),
                'storageHelper':webengine_record(Path(__file__).with_name('linux_build_storage.py')),
                'runtimeScope':runtime_scope(),
                'compatibilityBackports':webengine['compatibilityBackports'],'sourceInventories':source_inventory_identity(),
                'sourceArchive':{'sha256':webengine['sourceArchive']['digest'],'size':webengine['sourceArchive']['size']},
                'baseChromiumCommit':webengine['baseChromiumCommit'],'patchedChromiumCommit':webengine['patchedChromiumCommit'],
                'declaredSecurityVersion':webengine['declaredSecurityVersion'],
                'patches':[{key:entry[key] for key in ('commit','sha256','size')} for entry in webengine['patches']],
                'criticalAdvisories':webengine['criticalAdvisories']}
        if (not isinstance(security,dict) or set(security)-{'continuation'}!={*pinned,'outputs','upstreamFollowup','incrementalDelta','residualDelta','vendorFollowup'}
                or any(security[key]!=value for key,value in pinned.items())):
            raise RuntimeError('The exact completed vendor WebEngine security build identity is missing or changed.')
        if 'continuation' in security:
            from resume_qt_webengine_security import verify_continuation
            verify_continuation(security['continuation'])
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
            raise RuntimeError('The incremental WebEngine repairs and runtime history disagree.')
        outputs=security['outputs'];output_names=set()
        if not isinstance(outputs,list) or not 1<=len(outputs)<=1024:raise RuntimeError('The updated WebEngine output inventory is missing.')
        for row in outputs:
            if (not isinstance(row,dict) or set(row)!={'file','sha256','size'} or not isinstance(row.get('file'),str)
                    or type(row.get('size')) is not int or not 0<row['size']<=512*1024*1024
                    or not isinstance(row.get('sha256'),str) or not re.fullmatch('[a-f0-9]{64}',row['sha256'])):
                raise RuntimeError('An updated WebEngine output identity is invalid.')
            name=canonical_name(row['file'])
            if name in output_names:raise RuntimeError('An updated WebEngine output is duplicated.')
            output_names.add(name)
        if not any(name.startswith('lib/libQt6WebEngineCore.so.') for name in output_names) or 'libexec/QtWebEngineProcess' not in output_names:
            raise RuntimeError('The updated WebEngine core/helper output proof is incomplete.')
        verify_output_scope(outputs)
        lock=json_object(read_plain(Path(__file__).with_name('qt_sdk_lock.json')),'qt_sdk_lock.json')
        archives=[{key:row[key] for key in ('name','url','sha256','size')} for row in lock['archives']]
        if qt['archives']!=archives:raise RuntimeError('The Qt SDK archive identities differ from the reviewed inputs.')
        notices=qt['noticeFiles'];seen=set()
        if not isinstance(notices,list) or not 1<=len(notices)<=8192:raise RuntimeError('The Qt notice collection is missing or unbounded.')
        for row in notices:
            if not isinstance(row,dict) or set(row)!={'path','size','sha256'}:raise RuntimeError('An upstream notice identity is invalid.')
            name=canonical_name(row['path'])
            if not name.startswith('licenses/qt/') or name in seen or any(part.lower().startswith('readme') for part in name.split('/')):raise RuntimeError('An upstream notice has an unexpected path.')
            if type(row['size'])is not int or not 0<=row['size']<=16*1024*1024 or not isinstance(row['sha256'],str) or not re.fullmatch('[a-f0-9]{64}',row['sha256']):raise RuntimeError('An upstream notice has invalid content bounds.')
            if name not in files or len(files[name])!=row['size'] or hashlib.sha256(files[name]).hexdigest()!=row['sha256']:raise RuntimeError('An upstream notice is missing or changed: '+name)
            seen.add(name);required.add(name)
        runtime=qt['runtimeFiles']
        if not isinstance(runtime,list) or not 1<=len(runtime)<=1024 or len(runtime)!=len(set(runtime)) or any(name not in expected for name in runtime):raise RuntimeError('The official Qt runtime inventory is invalid.')
        if not {'usr/lib/libQt6Core.so.6','usr/lib/libQt6WebEngineCore.so.6','usr/lib/libQt6WebEngineWidgets.so.6','usr/libexec/QtWebEngineProcess'}<=set(runtime):raise RuntimeError('The Qt runtime inventory omits required native components.')
        from bootstrap_qt_webengine_security import RUNTIME_RESOURCES
        if not {'usr/translations/qtwebengine_locales/en-US.pak',*('usr/resources/'+name for name in RUNTIME_RESOURCES)}<=set(runtime):
            raise RuntimeError('The Qt runtime inventory omits required rebuilt resources/locales.')
        if any('/libQt6Pdf' in name or '/libQt6WebEngineQuick' in name for name in runtime):raise RuntimeError('An unbuilt standalone Pdf/Quick module cannot enter the required runtime closure.')
        verify_rebuilt_runtime_bindings(qt,files,expected,manifest.get('locallyModified') is True)
        from bootstrap_openssl import SOURCE as SSL_SOURCE, VERSION as SSL_VERSION, BUILD_OPTIONS as SSL_OPTIONS, RUNTIME_NAMES as SSL_NAMES
        ssl=provenance.get('openssl')
        ssl_fields={'schemaVersion','version','origin','source','buildOptions','minimumCpu','minimumGlibc','runtime','license','runtimeFiles'}
        if (not isinstance(ssl,dict) or set(ssl)!=ssl_fields or type(ssl['schemaVersion'])is not int or ssl['schemaVersion']!=1
                or ssl['version']!=SSL_VERSION or ssl['origin']!='official-openssl-source' or ssl['source']!=SSL_SOURCE
                or ssl['buildOptions']!=SSL_OPTIONS or ssl['minimumCpu']!='x86-64-v2' or ssl['minimumGlibc']!='2.35'
                or not isinstance(ssl['runtime'],dict) or set(ssl['runtime'])!=set(SSL_NAMES)):
            raise RuntimeError('The maintained source-built OpenSSL identity is missing or changed.')
        for record in (*ssl['runtime'].values(),ssl['license']):
            if (not isinstance(record,dict) or set(record)!={'size','sha256'} or type(record['size'])is not int
                    or not 0<record['size']<=512*1024*1024 or not isinstance(record['sha256'],str)
                    or not re.fullmatch('[a-f0-9]{64}',record['sha256'])):
                raise RuntimeError('The OpenSSL component identity is invalid.')
        ssl_files={'usr/lib/'+name for name in SSL_NAMES}
        if not isinstance(ssl['runtimeFiles'],list) or len(ssl['runtimeFiles'])!=len(ssl_files) or set(ssl['runtimeFiles'])!=ssl_files or not ssl_files<=set(expected):
            raise RuntimeError('The maintained OpenSSL runtime inventory is incomplete.')
        if any(package.get('sourcePackage')=='openssl' for package in packages):
            raise RuntimeError('The obsolete distribution OpenSSL cannot accompany the maintained runtime.')
        notice='licenses/OpenSSL-LICENSE.txt';required.add(notice)
        if notice not in files or len(files[notice])!=ssl['license']['size'] or hashlib.sha256(files[notice]).hexdigest()!=ssl['license']['sha256']:
            raise RuntimeError('The OpenSSL source licence is missing or changed.')
    from save_codec_materials import target as codec_target, installed_files, verify_provenance
    codec_worker='usr/bin/'+codec_target('linux')['file'];codec_pointer='usr/bin/'+SAVE_CODEC_POINTER
    codec_present=codec_worker in files or codec_pointer in files or 'saveCodec' in provenance
    if codec_present:
        installed_files({name[len('usr/bin/'):]:body for name,body in files.items() if name.startswith('usr/bin/')}, 'linux')
        required.update({codec_worker,codec_pointer})
        required.update(verify_provenance(provenance.get('saveCodec'),files,'linux'))
    common={name for name in files if re.fullmatch(r'licenses/common/(?:Apache-2\.0|Artistic|BSD|CC0-1\.0|GFDL(?:-1\.[123])?|GPL(?:-[123])?|LGPL(?:-(?:2|2\.1|3))?|MPL-(?:1\.1|2\.0))',name)}
    if set(files)!=(set(expected)|required|common):raise RuntimeError('The portable bundle has missing or unlisted files.')

def tar_bytes(name, content, label):
    source = io.BytesIO(content)
    if name.endswith('.tar'):
        reader = source
    elif name.endswith('.tar.gz'):
        reader = gzip.GzipFile(fileobj=source)
    elif name.endswith(('.tar.xz', '.tar.lzma')):
        reader = lzma.LZMAFile(source)
    elif name.endswith('.tar.bz2'):
        reader = bz2.BZ2File(source)
    elif name.endswith('.tar.zst'):
        try:
            from compression import zstd
        except ImportError as issue:
            raise RuntimeError('Use Python 3.14+ with Zstandard support to inspect '+label) from issue
        reader = zstd.ZstdFile(source)
    else:
        raise RuntimeError('Unsupported tar compression: '+label)
    with reader:
        expanded = reader.read(MAX_TAR+1)
    if len(expanded) > MAX_TAR:
        raise RuntimeError('Tar archive exceeds the decompression limit: '+label)
    return expanded

NESTED_ARCHIVE=re.compile(r'\.(?:zip|7z|rar|tar|tgz|gz|xz|bz2|zst|cab|iso)$',re.I)

def refuse_nested_archives(files):
    """Nexus quarantines uploads that contain another archive, so a public download never carries one."""
    for name,content in files.items():
        if NESTED_ARCHIVE.search(name) or content[:4] in (b'PK\x03\x04',b'7z\xbc\xaf',b'Rar!'):
            raise RuntimeError('A public download must not contain another archive (Nexus quarantines it): '+name)

def check_linux_appimage(files):
    manifest=json_object(files['APPIMAGE_MANIFEST.json'],'APPIMAGE_MANIFEST.json')
    fields={'schemaVersion','format','mode','applicationVersion','qtVersion','minimumGlibc','minimumGlibcxx','minimumCpu','runtime','runtimeManifestSha256','appImageRuntime','sourceCompanion','nativePrograms','capabilities'}
    if set(manifest)!=fields or type(manifest['schemaVersion'])is not int or manifest['schemaVersion']!=1 or manifest['format']!='appimage-x86_64' or manifest['mode']not in ('public','nexus-reviewer'):
        raise RuntimeError('The AppImage envelope identity is invalid.')
    reviewer=manifest['mode']=='nexus-reviewer'
    name='Drifters-Toolkit-'+('NexusReview-'if reviewer else '')+'Linux-x86_64.AppImage'
    required={name,'Launch-DriftersToolkit.sh','LINUX_REQUIREMENTS.json','APPIMAGE_MANIFEST.json'}
    if not required<=set(files) or any(key not in required and key not in ('LICENSES.txt',RELEASE_NOTES_NAME,SAVE_CODEC_POINTER) and not key.startswith('licenses/') for key in files):raise RuntimeError('The AppImage ZIP contains missing or unexpected files.')
    if 'LICENSES.txt' in files and b'LIBRARY_REPLACEMENT.txt' not in files['LICENSES.txt']:raise RuntimeError('LICENSES.txt is missing the library replacement instructions.')
    refuse_nested_archives(files)
    def digest_record(record,label,expected_fields={'sha256','size'}):
        if not isinstance(record,dict) or set(record)!=expected_fields or not isinstance(record.get('sha256'),str) or not re.fullmatch('[a-f0-9]{64}',record['sha256']) or type(record.get('size'))is not int or not 0<record['size']<=4*MAX_TOTAL:raise RuntimeError('Invalid AppImage component record: '+label)
    for key,value in {'applicationVersion':app_version(),'qtVersion':'6.11.3','minimumGlibc':'2.35','minimumGlibcxx':'3.4.30','minimumCpu':'x86-64-v2'}.items():
        if manifest[key]!=value:raise RuntimeError('The AppImage baseline differs from the tested runtime: '+key)
    runtime=manifest['runtime'];digest_record(runtime,'runtime',{'file','sha256','size'})
    if runtime['file']!=name or len(files[name])!=runtime['size'] or hashlib.sha256(files[name]).hexdigest()!=runtime['sha256']:raise RuntimeError('The AppImage bytes differ from their manifest.')
    if files[name][:7]!=b'\x7fELF\x02\x01\x01' or files[name][8:11]!=b'AI\x02' or files[name][18:20]!=b'\x3e\x00':raise RuntimeError('The package is not an x86-64 Type2 AppImage.')
    if not isinstance(manifest['runtimeManifestSha256'],str) or not re.fullmatch('[a-f0-9]{64}',manifest['runtimeManifestSha256']):raise RuntimeError('The sealed runtime manifest identity is missing.')
    native=manifest['nativePrograms']
    if not isinstance(native,dict) or set(native) not in ({'gui','cli'},{'gui','cli','codec'}):raise RuntimeError('The native program inventory is incomplete.')
    for key,record in native.items():digest_record(record,key)
    if 'codec' in native:
        from save_codec_materials import target as codec_target, verify_pointer
        expected_codec=codec_target('linux')
        if native['codec'] != {key:expected_codec[key] for key in ('sha256','size')} or SAVE_CODEC_POINTER not in files: raise RuntimeError('The AppImage SaveCodec identity/source pointer is incomplete.')
        verify_pointer(files[SAVE_CODEC_POINTER])
    elif SAVE_CODEC_POINTER in files: raise RuntimeError('The AppImage source pointer has no worker identity.')
    digest_record(manifest['appImageRuntime'],'wrapper',{'sha256','size','provenanceSha256'})
    if not isinstance(manifest['appImageRuntime']['provenanceSha256'],str) or not re.fullmatch('[a-f0-9]{64}',manifest['appImageRuntime']['provenanceSha256']):raise RuntimeError('The AppImage wrapper provenance is invalid.')
    companion=manifest['sourceCompanion'];digest_record(companion,'source companion',{'file','sha256','size','components'})
    if companion['file']!='Drifters-Toolkit-Linux-Corresponding-Source.tar' or not isinstance(companion['components'],list) or not companion['components']:raise RuntimeError('The corresponding-source companion is missing.')
    if 'codec' in native and 'save-codec' not in companion['components']: raise RuntimeError('The Linux corresponding-source companion omits the SaveCodec source component.')
    if manifest['capabilities']!={'reviewerConnect':reviewer,'interfaceTesting':False}:raise RuntimeError('The native reviewer/private capabilities are inconsistent.')
    if len(files['LINUX_REQUIREMENTS.json'])>16384:raise RuntimeError('Linux requirements are unbounded.')
    json_object(files['LINUX_REQUIREMENTS.json'],'LINUX_REQUIREMENTS.json')
    launcher=files['Launch-DriftersToolkit.sh']
    if not launcher.startswith(b'#!/bin/sh\n') or len(launcher)>16384 or name.encode()not in launcher or b'--no-sandbox'in launcher:raise RuntimeError('The AppImage launch helper is unexpected.')
    return {'platform':'linux','distribution':'portable','reviewer':reviewer}


def check_archive(path, source=False, require_sidecar=False, content=None, _budget=None, require_signature=False):
    path = Path(path)
    if require_sidecar:verify_sidecar(path)
    inspection = Inspection(public=not source,budget=_budget,pinned_codec_source=source)
    identity=None
    if path.suffix.lower() in ('.deb', '.rpm'):
        raise RuntimeError('Distribution-specific Linux packages are not part of this project; ship the universal AppImage ZIP: '+path.name)
    elif path.name.endswith(('.tar','.tar.gz','.tar.xz','.tar.bz2','.tar.zst')):
        if not source:
            inspection = Inspection(public=True,budget=inspection.budget,appdir_prefix='DriftersToolkit.AppDir/')
        if path.stat().st_size > MAX_MEMBER:
            raise RuntimeError('Portable archive exceeds the inspection limit: '+path.name)
        expanded = tar_bytes(path.name,path.read_bytes(),path.name)
        with tarfile.open(fileobj=io.BytesIO(expanded),mode='r:') as archive:
            seen=set();files={}
            for member in archive:
                name=inspection.name(member.name,member.isdir())
                if name in seen: raise RuntimeError('Duplicate portable payload entry: '+name)
                seen.add(name);inspection.consume(member.size,name)
                if member.isdir():continue
                if not member.isreg() or member.issparse():raise RuntimeError('Unexpected portable payload type: '+name)
                if member.mode&0o7022:raise RuntimeError('Unexpected writable or privileged portable entry: '+name)
                stream=archive.extractfile(member)
                if stream is None:raise RuntimeError('Unreadable portable payload: '+name)
                with stream:content=stream.read(inspection.member_limit(name)+1)
                if len(content)!=member.size:raise RuntimeError('Incomplete portable payload: '+name)
                inspection.content(name,content)
                files[name]=content
        prefix='DriftersToolkit.AppDir/'
        if any(not name.startswith(prefix) for name in files):raise RuntimeError('Unexpected portable archive root.')
        verify_portable({name[len(prefix):]:data for name,data in files.items()})
    else:
        with zipfile.ZipFile(io.BytesIO(content) if content is not None else path) as archive:
            seen = set();folded=set();files={};file_metadata={}
            for info in archive.infolist():
                name = inspection.name(info.filename,info.is_dir())
                if name in seen:
                    raise RuntimeError('Duplicate archive entries: '+path.name)
                if name.casefold() in folded:raise RuntimeError('Case-colliding archive entries: '+path.name)
                seen.add(name)
                folded.add(name.casefold())
                mode=info.external_attr>>16;kind=stat.S_IFMT(mode)
                if info.flag_bits&1 or kind not in (0,stat.S_IFDIR if info.is_dir() else stat.S_IFREG) or mode&0o7022:
                    raise RuntimeError('An encrypted, redirected or privileged ZIP member was included: '+name)
                inspection.consume(info.file_size, name)
                if info.is_dir():continue
                content = archive.read(info)
                if len(content)!=info.file_size:raise RuntimeError('Incomplete ZIP entry: '+name)
                files[name]=content
                file_metadata[name]=(mode,info.create_system)
                if name.lower().endswith(('.deb', '.rpm')):
                    raise RuntimeError('Distribution-specific Linux packages are not part of this project: '+name)
                inspection.content(name, content)
            roots={name.split('/',1)[0] for name in files}
            if len(roots)!=1:raise RuntimeError('The ZIP must contain one named package root.')
            root=next(iter(roots))
            verify_checksums(files,root)
            signed=verify_signature(files,root)
            if require_signature and not signed:raise RuntimeError('This release ZIP is not signed. Sign it with scripts/release_signing.py.')
            relative={name[len(root)+1:] for name in files}-{SIGNATURE}
            notes_name=root+'/'+RELEASE_NOTES_NAME
            if notes_name in files and root in ('DriftersToolkit','DriftersToolkit-Linux'):
                release_notes(files[notes_name],signed or app_version())
            if root=='DriftersToolkit' and not source:
                allowed=set(WINDOWS_RUNTIME)|{'SHA256SUMS.json'}
                codec=set(SAVE_CODEC_WINDOWS_FILES)
                if relative not in (allowed,allowed|{RELEASE_NOTES_NAME},allowed|codec,allowed|codec|{RELEASE_NOTES_NAME}):raise RuntimeError('The Windows package has missing or unexpected files.')
                from save_codec_materials import installed_files, lock as codec_lock
                if installed_files({name[len(root)+1:]:body for name,body in files.items()}, 'windows'):
                    licenses=files.get(root+'/LICENSES.txt',b'')
                    if b'DriftersToolkitSaveCodec' not in licenses or b'GNU GENERAL PUBLIC LICENSE' not in licenses or codec_lock()['sourceArchive'].encode() not in licenses:
                        raise RuntimeError('The separate GPL SaveCodec notices/source pointer are missing from LICENSES.txt.')
                refuse_nested_archives({name[len(root)+1:]:data for name,data in files.items()})
            elif root=='DriftersToolkit-Linux' and not source:
                if 'APPIMAGE_MANIFEST.json' not in relative:
                    raise RuntimeError('The Linux ZIP must contain the universal AppImage; distribution-specific packages are not supported.')
                identity=check_linux_appimage({name[len(root)+1:]:data for name,data in files.items()if not name.endswith(('/SHA256SUMS.json','/'+SIGNATURE))})
                for name,(mode,origin) in file_metadata.items():
                    executable=name.endswith(('.AppImage','/Launch-DriftersToolkit.sh'))
                    if executable and origin!=3:raise RuntimeError('The AppImage ZIP executable lacks UNIX permission metadata: '+name)
                    if bool(mode&0o111)!=executable:raise RuntimeError('The AppImage ZIP has incorrect executable permissions: '+name)
            elif root=='DriftersToolkit-SaveCodec-source' and source:
                from save_codec_materials import verify_source_members
                verify_source_members({name[len(root)+1:]:body for name,body in files.items() if name != root+'/SHA256SUMS.json'})
            elif root=='DriftersToolkit-source' and source:
                required={'CMakeLists.txt','config/nexus.json','catalog/catalog.json','ui/assets/seasonal-emoji.svg','docs/NotoEmoji-Images-LICENSE.txt'}
                if not required<=relative:raise RuntimeError('The source package is incomplete.')
            elif root=='DriftersToolkit-review-source' and source:
                from review_sources import allowed,FIXED
                if not FIXED<=relative or any(name!='SHA256SUMS.json' and not allowed(name) for name in relative):raise RuntimeError('The clean reviewer source list is incomplete or contains private handoff files.')
            elif root=='DriftersToolkit-NexusReview' and not source:
                from save_codec_materials import lock as codec_lock
                codec_source=codec_lock()['sourceArchive']
                expected=REVIEW_OUTER_FILES|{'SHA256SUMS.json'}
                if relative not in (expected,expected|{codec_source}):raise RuntimeError('The Nexus review bundle has missing or unexpected files.')
                needs_codec_source=False
                for name in sorted(relative):
                    if name.endswith('.zip'):
                        nested=check_archive(Path(name),source=name in ('Drifters-Toolkit-Review-Source.zip',codec_source),content=files[root+'/'+name],_budget=inspection.budget)
                        if name=='Drifters-Toolkit-NexusReview-Linux.zip'and nested!={'platform':'linux','distribution':'portable','reviewer':True}:
                            raise RuntimeError('The submission requires the separately built universal Linux reviewer AppImage.')
                        if name in ('Drifters-Toolkit-NexusReview-Windows.zip','Drifters-Toolkit-NexusReview-Linux.zip'):
                            with zipfile.ZipFile(io.BytesIO(files[root+'/'+name])) as nested_zip:
                                needs_codec_source|=any(entry.filename.endswith('/'+SAVE_CODEC_POINTER) for entry in nested_zip.infolist())
                if needs_codec_source and codec_source not in relative:
                    raise RuntimeError('The review bundle needs the complete matching SaveCodec source ZIP.')
            else:raise RuntimeError('Unknown package root or missing explicit --source option.')
    print('Preview archive layout and content-privacy checks passed: '+path.name)
    return identity

def check_directory(path):
    """Inspect an explicitly staged AppDir before an opaque SquashFS is built."""
    root=Path(path)
    if root.is_symlink() or not root.is_dir():raise RuntimeError('The staged bundle must be an ordinary directory.')
    inspection=Inspection(public=True,appdir_prefix='');files={}
    visited=0
    for source in root.rglob('*'):
        visited+=1
        if visited>MAX_ENTRIES:raise RuntimeError('The staged bundle has too many filesystem entries.')
        name=inspection.name(source.relative_to(root).as_posix())
        if source.is_symlink() or getattr(source.lstat(),'st_file_attributes',0)&0x400:raise RuntimeError('A staged bundle entry is redirected: '+name)
        if source.is_dir():continue
        if not source.is_file() or source.stat().st_nlink!=1:raise RuntimeError('A staged bundle entry is not an independent ordinary file: '+name)
        content=read_plain(source,inspection.member_limit(name));inspection.consume(len(content),name);inspection.content(name,content);files[name]=content
    verify_portable(files)
    print('Staged portable bundle layout and content-privacy checks passed: '+root.name)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archives', nargs='+', type=Path)
    parser.add_argument('--source',action='store_true',help='Explicitly inspect a source archive instead of a public runtime.')
    parser.add_argument('--allow-missing-sidecar',action='store_true',help='For isolated fixtures only; the internal checksum manifest remains mandatory.')
    parser.add_argument('--signed',action='store_true',help='Require a valid release signature (public releases).')
    args=parser.parse_args()
    for path in args.archives:
        check_archive(path,source=args.source,require_sidecar=not args.allow_missing_sidecar,require_signature=args.signed)
