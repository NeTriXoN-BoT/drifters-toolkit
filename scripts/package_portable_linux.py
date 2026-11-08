"""Stage a verified maintained Qt SDK and seal a source-backed portable runtime.

Stage on Debian 12, compile the generated runtime manifest into both native
entrypoints, then seal the exact ordinary AppDir. Downloads are separate pinned
build steps; neither staging nor AppImage construction fetches code implicitly.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
from release_layout import PUBLIC_UI as UI, LINUX_NOTICES, read_plain, canonical_name, refuse_private_save_preview

ROOT = Path(__file__).resolve().parents[1]
# Libraries every desktop Linux provides; they are taken from the host. libexpat
# is on the AppImage project's base-system exclude list (Mesa, fontconfig and
# D-Bus all need it) and has had a stable ABI for decades. Debian 12's build of
# it calls arc4random_buf@GLIBC_2.36, so bundling it would raise the whole
# AppImage's glibc floor from 2.35 to 2.36 and exclude Ubuntu 22.04/Mint 21.
# libfontconfig is on the same list: it must match the host's own font
# configuration. Debian 12's 2.14 ignores the family rules of the 2.16+ configs
# on rolling distributions (SteamOS, Arch, Tumbleweed), which breaks the generic
# sans-serif/serif/monospace fallbacks the interface uses. Every font function
# the bundle calls exists in fontconfig 2.13 (Ubuntu 22.04), and the newest
# fontconfig needs no FreeType function the bundled FreeType lacks.
HOST_LIBS = {'ld-linux-x86-64.so.2','libc.so.6','libm.so.6','libpthread.so.0','librt.so.1',
             'libdl.so.2','libresolv.so.2','libutil.so.1','libstdc++.so.6','libgcc_s.so.1',
             'libEGL.so.1','libGL.so.1','libGLX.so.0','libGLdispatch.so.0','libOpenGL.so.0',
             'libGLESv2.so.2','libgbm.so.1','libdrm.so.2','libvulkan.so.1','libexpat.so.1','libfontconfig.so.1'}
# The oldest glibc the AppImage supports: Debian 12 builds it, but every bundled
# ELF object must stay at or below this symbol version (checked while staging).
MINIMUM_GLIBC = (2, 35)
PLUGIN_GROUPS = ('platforms','platformthemes','platforminputcontexts','xcbglintegrations','wayland-shell-integration',
                 'wayland-graphics-integration-client','wayland-decoration-client','tls','imageformats','iconengines')
# Qt Base/SVG image plugins plus the WebP decoder from the Image Formats module.
# The loader forces png/jpeg/webp after its own container inspection, so the
# module's other decoders (TIFF, TGA, WBMP, ICNS) and their bundled third-party
# codecs are deliberately not staged.
IMAGE_PLUGINS = {'libqgif.so','libqico.so','libqjpeg.so','libqsvg.so','libqwebp.so'}
RESOURCES = {'icudtl.dat','qtwebengine_resources.pak','qtwebengine_devtools_resources.pak','qtwebengine_resources_100p.pak',
             'qtwebengine_resources_200p.pak','v8_context_snapshot.bin'}
QT_CONF = '[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\nLibraryExecutables=libexec\nData=.\nTranslations=translations\n'
APP_RUN = '#!/bin/sh\nset -eu\nbase=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)\nif [ "${1-}" = "--cli" ]; then\n    shift\n    exec "$base/usr/bin/DriftersToolkitCLI" "$@"\nfi\nexec "$base/usr/bin/DriftersToolkit" "$@"\n'

def run(*args):
    env = {key:value for key,value in os.environ.items() if not key.startswith(('LD_','QT_','QTWEBENGINE_'))}
    return subprocess.run(args,check=True,text=True,capture_output=True,env=env).stdout.strip()

def digest(path):
    with Path(path).open('rb') as source:return hashlib.file_digest(source,'sha256').hexdigest()

def host_library(name):
    return name in HOST_LIBS or name.startswith(('libdrm_','libnss_'))

def copy(source,target,executable=False):
    source=Path(source)
    # Build-system SONAME symlinks are resolved deliberately; runtime output is
    # always an independent ordinary file, never an inherited filesystem alias.
    actual=source.resolve(strict=True)
    if not actual.is_file() or actual.stat().st_size>512*1024*1024:raise RuntimeError('Invalid runtime input: '+str(source))
    target=Path(target);target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():
        if digest(target)!=digest(actual):raise RuntimeError('Conflicting runtime basename: '+str(target))
        return
    with actual.open('rb') as src,target.open('xb') as dst:shutil.copyfileobj(src,dst,1024*1024)
    target.chmod(0o755 if executable else 0o644)

def rebuilt_runtime_targets(outputs):
    """Map the exact rebuilt outputs to the runtime names actually loaded."""
    from bootstrap_qt_webengine_security import verify_output_scope
    verify_output_scope(outputs)
    sonames={'lib/libQt6WebEngineCore.so.6.11.3':'usr/lib/libQt6WebEngineCore.so.6',
             'lib/libQt6WebEngineWidgets.so.6.11.3':'usr/lib/libQt6WebEngineWidgets.so.6'}
    return {sonames.get(row['file'],'usr/'+row['file']):
            {'source':row['file'],**{key:row[key] for key in ('sha256','size')}} for row in outputs}

def copy_runtime(source,target,name,sdk,rebuilt,copied,executable=False):
    """Bind SONAME aliases to repaired bytes before any RUNPATH rewriting."""
    expected=rebuilt.get(name)
    if expected is None:
        copy(source,target,executable)
        return
    from portable_materials import identity
    actual=Path(source).resolve(strict=True)
    original={key:expected[key] for key in ('sha256','size')}
    if not actual.is_relative_to(sdk) or identity(actual,512*1024*1024)!=original:
        raise RuntimeError('A loaded WebEngine component differs from the repaired runtime: '+name)
    copy(actual,target,executable)
    if identity(target,512*1024*1024)!=original:
        raise RuntimeError('A repaired WebEngine component changed while copying: '+name)
    copied[name]=original

def rebuilt_runtime_bindings(appdir,rebuilt,copied):
    """Record copied source identities and final, possibly relocated ELF bytes."""
    from portable_materials import identity
    if set(copied)!=set(rebuilt):
        raise RuntimeError('The staged runtime omits a required repaired WebEngine component.')
    rows=[]
    for name,expected in sorted(rebuilt.items()):
        original={key:expected[key] for key in ('sha256','size')}
        staged=identity(Path(appdir)/name,512*1024*1024)
        if copied[name]!=original or (not name.startswith(('usr/lib/','usr/libexec/')) and staged!=original):
            raise RuntimeError('A repaired WebEngine resource changed after copying: '+name)
        rows.append({'source':expected['source'],'path':name,'original':original,'staged':staged,'localReplacement':False})
    return rows

def baseline():
    if os.name!='posix' or platform.machine()!='x86_64':raise RuntimeError('Stage this bundle on native x86-64 Linux.')
    release=Path('/etc/os-release').read_text()
    if 'ID=debian\n' not in release or 'VERSION_ID="12"' not in release:raise RuntimeError('Use the pinned Debian 12 baseline, not a newer host with a higher glibc floor.')
    if platform.libc_ver()[1]!='2.36':raise RuntimeError('The expected build baseline is glibc 2.36.')
    for name in ('lddtree','patchelf','readelf','dpkg-query'):shutil.which(name) or (_ for _ in ()).throw(RuntimeError('Missing packaging tool: '+name))

def qt_inputs(prefix, provenance, archives):
    """Verify pinned official SDK inputs before following its relocatable paths."""
    prefix=Path(prefix).resolve(strict=True)
    data=json.loads(read_plain(provenance,2*1024*1024))
    if data.get('schemaVersion')!=1 or data.get('qtVersion')!='6.11.3':
        raise RuntimeError('Use the reviewed Qt 6.11.3 SDK provenance; the old Qt prototype is not a release input.')
    rows=data.get('archives')
    if not isinstance(rows,list) or not 1<=len(rows)<=128:raise RuntimeError('The pinned SDK archive inventory is missing.')
    verified=[];seen=set();archives=Path(archives).resolve(strict=True)
    for row in rows:
        name=row.get('name','');canonical_name(name)
        if '/' in name or name in seen or not re.fullmatch('[a-f0-9]{64}',row.get('sha256','')) or type(row.get('size')) is not int or not 0<row['size']<=2*1024*1024*1024:
            raise RuntimeError('Invalid Qt SDK archive identity.')
        url=row.get('url','')
        if not url.startswith('https://download.qt.io/') or any(c in url for c in ('\n','\r','\x00')):
            raise RuntimeError('Qt SDK provenance must identify its official HTTPS download.')
        path=archives/name
        if path.is_symlink() or not path.is_file() or path.stat().st_nlink!=1 or path.stat().st_size!=row['size'] or digest(path)!=row['sha256']:
            raise RuntimeError('The original Qt SDK archive is missing or changed: '+name)
        seen.add(name);verified.append({key:row[key] for key in ('name','url','sha256','size')})
    qmake=next((path for path in (prefix/'bin/qmake',prefix/'bin/qmake6') if path.is_file()),None)
    if qmake is None or run(str(qmake),'-query','QT_VERSION')!='6.11.3':raise RuntimeError('The staged SDK tool does not report Qt 6.11.3.')
    qt={key:Path(run(str(qmake),'-query',key)).resolve(strict=True) for key in ('QT_INSTALL_PLUGINS','QT_INSTALL_LIBEXECS','QT_INSTALL_DATA','QT_INSTALL_TRANSLATIONS')}
    if not all(path.is_relative_to(prefix) for path in qt.values()):raise RuntimeError('A Qt SDK tool resolved a path outside its verified prefix.')
    # The official 22 October SDK predates vendor critical backports. A version
    # string, original archive hashes or a claimed source patch is insufficient:
    # require the new core/helper/resource bytes from the completed pinned build.
    from qt_webengine_release_1_1 import verify_build, public_provenance
    security=public_provenance(verify_build(prefix))
    return prefix,qt,{'version':'6.11.3','origin':'official-qt-sdk-with-pinned-vendor-webengine-backports','archives':verified,'webengineSecurity':security}

def copy_qt_notices(source, destination):
    source=Path(source).resolve(strict=True);rows=[];total=0
    for path in sorted(source.rglob('*')):
        if path.is_symlink():raise RuntimeError('A Qt notice is redirected.')
        if path.is_dir():continue
        relative=path.relative_to(source).as_posix();canonical_name(relative)
        if any(re.match(r'readme(?:\.|$)',part,re.I) for part in Path(relative).parts):raise RuntimeError('README files belong only in the separate intact upstream source companion.')
        body=read_plain(path,16*1024*1024);total+=len(body)
        if total>128*1024*1024 or len(rows)>=8192:raise RuntimeError('The Qt notice collection exceeds its bounds.')
        target=Path(destination)/relative;copy(path,target)
        rows.append({'path':'licenses/qt/'+relative,'sha256':digest(target),'size':len(body)})
    if not rows:raise RuntimeError('The complete Qt/Chromium license notice collection is required.')
    return rows

def package_owner(source):
    for candidate in (str(Path(source)),str(Path(source).resolve())):
        result=subprocess.run(['dpkg-query','-S',candidate],text=True,capture_output=True)
        if result.returncode==0:
            for line in result.stdout.splitlines():
                if ': ' in line:return line.split(': ',1)[0]
    raise RuntimeError('Runtime input has no distribution package provenance: '+str(source))

def inventory(appdir,cli_paths=()):
    from portable_materials import identity
    cli_paths=set(cli_paths)
    rows=[]
    for folder in ('lib','plugins','libexec','resources','translations'):
        for path in sorted((appdir/'usr'/folder).rglob('*')):
            if path.is_symlink():raise RuntimeError('A staged runtime path is redirected: '+str(path))
            if path.is_file():rows.append({'path':path.relative_to(appdir).as_posix(),**identity(path,512*1024*1024),'executable':bool(path.stat().st_mode&0o111),'cli':path.relative_to(appdir).as_posix() in cli_paths})
    path=appdir/'usr/bin/qt.conf';rows.append({'path':'usr/bin/qt.conf',**identity(path,512*1024*1024),'executable':False,'cli':'usr/bin/qt.conf' in cli_paths})
    return {'schemaVersion':1,'architecture':'x86_64','qtVersion':'6.11.3','minimumGlibc':'.'.join(map(str,MINIMUM_GLIBC)),'minimumGlibcxx':'3.4.30','cliReady':bool(cli_paths),'files':rows}

def verify_inventory(appdir):
    expected=json.loads((appdir/'runtime-manifest.json').read_text())
    paths={row['path'] for row in expected['files'] if row.get('cli',False)}
    current=inventory(appdir,paths)
    if expected.get('locallyModified'):current['locallyModified']=True
    if current!=expected:raise RuntimeError('Staged runtime changed after manifest generation.')
    return expected

def reindex(appdir):
    """Reindex interface-compatible replacements, then require an app rebuild.

    Paths/CLI membership stay fixed; a changed dependency closure needs stage.
    An existing executable still rejects the changed bytes until rebuilt.
    """
    from generate_portable_manifest import render
    appdir=Path(appdir).resolve(strict=True);path=appdir/'runtime-manifest.json'
    old=json.loads(read_plain(path,2*1024*1024));render(old)
    current=inventory(appdir,{row['path'] for row in old['files'] if row.get('cli',False)})
    if {row['path'] for row in current['files']}!={row['path'] for row in old['files']}:
        raise RuntimeError('The replacement changed runtime paths. Stage the new dependency closure explicitly.')
    current['locallyModified']=True
    proof_path=appdir/'DEPENDENCY-SOURCES.json'
    if proof_path.exists():
        from portable_materials import identity
        from release_layout import write_artifact
        proof=json.loads(read_plain(proof_path,8*1024*1024))
        for binding in proof.get('qt',{}).get('rebuiltRuntimeFiles',[]):
            name=canonical_name(binding.get('path',''))
            if name not in {row['path'] for row in current['files']}:
                raise RuntimeError('A replacement binding leaves the staged runtime.')
            staged=identity(appdir/name,512*1024*1024)
            if staged!=binding.get('staged'):
                binding['staged']=staged;binding['localReplacement']=True
        write_artifact(proof_path,(json.dumps(proof,indent=2)+'\n').encode(),maximum=8*1024*1024).chmod(0o644)
    path.write_text(json.dumps(current,indent=2)+'\n')
    print('Reindexed local library replacements. Rebuild both native programs with this manifest before refresh; no signing key is needed.')
    return current


def codec_inputs(build, source):
    from save_codec_materials import target, verify_worker, provenance
    helper = Path(build)/target('linux')['file']
    if not helper.exists() and not helper.is_symlink():
        if source is not None: raise RuntimeError('SaveCodec source was supplied but its Linux worker is missing.')
        return None
    if source is None: raise RuntimeError('Supply the complete matching SaveCodec source closure for public staging.')
    body = verify_worker(helper, 'linux'); proof, notices = provenance(source, 'linux')
    return helper, body, proof, notices


def install_codec(appdir, inputs):
    from save_codec_materials import POINTER_NAME, pointer_bytes, verify_worker
    from release_layout import write_artifact
    if inputs is None: return None
    helper, body, proof, notices = inputs; target = Path(appdir)/'usr/bin'/helper.name
    if target.exists() or target.is_symlink(): verify_worker(target, 'linux')
    copy(helper, target, True)
    if read_plain(target, 8*1024*1024) != body: raise RuntimeError('The unchanged static SaveCodec worker was altered during staging.')
    write_artifact(Path(appdir)/'usr/bin'/POINTER_NAME, pointer_bytes(), maximum=16384).chmod(0o644)
    for name, content in notices.items():
        write_artifact(Path(appdir)/'licenses'/name, content, maximum=32*1024*1024).chmod(0o644)
    return proof


def stage(build,appdir,qt_prefix,qt_provenance,qt_archives,qt_notices,openssl_prefix,openssl_provenance,openssl_archive,save_codec_source=None,dependencies_prefix=None):
    refuse_private_save_preview(build,Path(appdir)/'usr/bin',appdir)
    baseline();build=build.resolve(strict=True);appdir=appdir.absolute()
    codec = codec_inputs(build, save_codec_source)
    from bootstrap_archive import verify_build, public_provenance
    cache=read_plain(build/'CMakeCache.txt',8*1024*1024).decode('utf-8')
    prefix=re.search(r'^DRIFTER_ARCHIVE_PREFIX:PATH=(.+)$',cache,re.M)
    if prefix is None:raise RuntimeError('The source-built static archive decoder prefix is missing from the application build.')
    verify_build(Path(prefix.group(1).strip()))
    sdk,qt,qt_proof=qt_inputs(qt_prefix,qt_provenance,qt_archives)
    rebuilt=rebuilt_runtime_targets(qt_proof['webengineSecurity']['outputs']);copied={}
    from bootstrap_openssl import runtime_inputs, RUNTIME_NAMES
    openssl_prefix=Path(openssl_prefix).absolute()
    ssl_proof=runtime_inputs(openssl_prefix,openssl_provenance,openssl_archive)
    if dependencies_prefix is None:raise RuntimeError('Stage with the verified maintained Linux dependency prefix.')
    from bootstrap_linux_dependencies import verify_build as dependency_build
    dependencies_prefix=Path(dependencies_prefix).absolute()
    maintained=dependency_build(dependencies_prefix)
    dependency_outputs={r['name']:r for r in maintained['outputs']}
    if appdir.exists():raise RuntimeError('Stage into a fresh path; an existing bundle is never overwritten.')
    # A previously portable build intentionally cannot run from the flat CMake
    # output directory. Staging is unsealed data; the second compiled-manifest
    # build and mandatory final verify-only gate must succeed before packaging.
    appdir.mkdir(mode=0o755,parents=True)
    bin_dir=appdir/'usr/bin'
    programs=('DriftersToolkit','DriftersToolkitCLI')
    for name in list(programs)+['ui/'+name for name in UI]+['catalog/catalog.json','config/nexus.json']:
        if (build/name).is_symlink():raise RuntimeError('Redirected release input: '+name)
        copy(build/name,bin_dir/name,name in programs)
    packages=set();originals={};elf_targets=[];sdk_members=[];ssl_members=[];dependency_members=[]
    def native(source,target):
        source=Path(source);copy_runtime(source,appdir/target,target,sdk,rebuilt,copied,True)
        if source.resolve().is_relative_to(sdk):sdk_members.append(target)
        elif source.resolve().is_relative_to(openssl_prefix):ssl_members.append(target)
        elif source.resolve().is_relative_to(dependencies_prefix):
            expected=dependency_outputs.get(source.name)
            if expected is None or digest(source)!=expected['sha256'] or source.stat().st_size!=expected['size']:
                raise RuntimeError('A maintained Linux library changed before staging: '+source.name)
            dependency_members.append(target)
        else:packages.add(package_owner(source))
        originals[str(appdir/target)]=source;elf_targets.append(appdir/target)
    native(qt['QT_INSTALL_LIBEXECS']/'QtWebEngineProcess','usr/libexec/QtWebEngineProcess')
    for group in PLUGIN_GROUPS:
        for source in sorted((qt['QT_INSTALL_PLUGINS']/group).glob('*.so')):
            # Qt/XDG portals provide the portable dialogs. The optional GTK
            # theme bridge pulls in an unused second widget/image stack.
            if group=='platformthemes' and source.name=='libqgtk3.so':continue
            if group=='platforms' and source.name not in {'libqxcb.so','libqwayland.so','libqwayland-egl.so','libqwayland-generic.so'}:continue
            if group=='imageformats' and source.name not in IMAGE_PLUGINS:continue
            native(source,'usr/plugins/'+group+'/'+source.name)
    if not (appdir/'usr/plugins/platforms/libqxcb.so').is_file() or not any((appdir/'usr/plugins/platforms'/name).is_file() for name in ('libqwayland.so','libqwayland-egl.so','libqwayland-generic.so')):raise RuntimeError('Both X11 and native Wayland platform plugins are required.')
    if not (appdir/'usr/plugins/imageformats/libqwebp.so').is_file():raise RuntimeError('The Qt WebP image plugin from the pinned Image Formats module is required for native author-picture decoding.')
    if not all((appdir/'usr/plugins/xcbglintegrations'/name).is_file() for name in ('libqxcb-egl-integration.so','libqxcb-glx-integration.so')):raise RuntimeError('X11 EGL/GLX integration plugins are required for WebEngine rendering.')
    for name in RESOURCES:
        source=qt['QT_INSTALL_DATA']/'resources'/name
        if source.exists():
            target='usr/resources/'+name
            copy_runtime(source,appdir/target,target,sdk,rebuilt,copied);sdk_members.append(target)
    if not (appdir/'usr/resources/qtwebengine_resources.pak').is_file():raise RuntimeError('Qt WebEngine resources are missing.')
    locales=qt['QT_INSTALL_TRANSLATIONS']/'qtwebengine_locales'
    for source in sorted(locales.glob('*.pak')):
        if not re.fullmatch(r'[A-Za-z0-9_-]+\.pak',source.name):raise RuntimeError('Unexpected locale name.')
        target='usr/translations/qtwebengine_locales/'+source.name
        copy_runtime(source,appdir/target,target,sdk,rebuilt,copied);sdk_members.append(target)
    if not (appdir/'usr/translations/qtwebengine_locales/en-US.pak').is_file():raise RuntimeError('Qt WebEngine English locale is missing.')
    # The TLS plugin resolves OpenSSL with dlopen, so DT_NEEDED alone does not
    # establish its full dependency closure. Use only the verified source build;
    # the older Debian 12 libssl3 package must never become an implicit fallback.
    ssl_inputs=[]
    for extra in RUNTIME_NAMES:
        source=openssl_prefix/'lib'/extra
        if not source.is_file():raise RuntimeError('The pinned OpenSSL TLS runtime is missing: '+extra)
        native(source,'usr/lib/'+extra);ssl_inputs.append(source)
    # Resolve direct edges after each replacement. Flattening the old lddtree
    # first would retain dependencies of libraries that are no longer shipped.
    replacements={p.name:p for p in (sdk/'lib').glob('*.so*') if p.is_file()}
    replacements.update({p.name:p for p in ssl_inputs})
    replacements.update({name:dependencies_prefix/'runtime'/name for name in dependency_outputs})
    dynamic={};resolved={}
    def needed(source):
        key=str(source)
        if key not in dynamic:dynamic[key]=re.findall(r'\(NEEDED\).*Shared library: \[([^\]]+)\]',run('readelf','-d',key))
        return dynamic[key]
    def resolve(source,name):
        key=str(source)
        if key not in resolved:
            paths=[Path(line) for line in run('lddtree','-l',key).splitlines()]
            resolved[key]={p.name:p for p in paths if p.is_absolute() and p.is_file()}
        if name not in resolved[key]:raise RuntimeError('An ELF dependency could not be resolved: '+name)
        return resolved[key][name]
    from linux_dependency_graph import closure
    tls_sources=[source for target,source in originals.items() if '/usr/plugins/tls/' in target]
    cli_sources=[build/'DriftersToolkitCLI']+ssl_inputs+tls_sources
    cli_paths={'usr/bin/qt.conf',*('usr/plugins/tls/'+p.name for p in tls_sources)}
    cli_paths.update('usr/lib/'+p.name for p in ssl_inputs)
    cli_paths.update('usr/lib/'+name for name in closure(cli_sources,replacements,needed,resolve,host_library))
    inputs=[build/name for name in programs]+list(originals.values())
    for extra in ('libsoftokn3.so','libfreebl3.so','libfreeblpriv3.so'):
        source=replacements[extra]
        native(source,'usr/lib/'+extra);inputs.append(source)
    for name,dependency in closure(inputs,replacements,needed,resolve,host_library).items():
        target=appdir/'usr/lib'/name
        if not target.exists():native(dependency,'usr/lib/'+name)
    # RUNPATH changes apply only to these ELF objects, not to Steam/the browser
    # launched later. No global LD_LIBRARY_PATH or Qt override is exported.
    elf_targets.extend(bin_dir/name for name in programs)
    for target in elf_targets:
        relative=os.path.relpath(appdir/'usr/lib',target.parent)
        run('patchelf','--set-rpath','$ORIGIN/'+relative,str(target))
        symbols=run('readelf','--version-info',str(target))
        versions=[tuple(map(int,x.split('.'))) for x in re.findall(r'\bGLIBC_([0-9.]+)',symbols)]
        if versions and max(versions)>MINIMUM_GLIBC:raise RuntimeError('A runtime input needs glibc newer than the supported floor '+'.'.join(map(str,MINIMUM_GLIBC))+': '+str(target))
        cxx_versions=[tuple(map(int,x.split('.'))) for x in re.findall(r'\bGLIBCXX_([0-9.]+)',symbols)]
        if cxx_versions and max(cxx_versions)>(3,4,30):raise RuntimeError('A runtime input needs libstdc++ newer than the GCC 12 baseline: '+str(target))
    for directory in (bin_dir,appdir/'usr/libexec'):(directory/'qt.conf').write_text(QT_CONF)
    notices=appdir/'licenses';notices.mkdir()
    dependencies=[]
    for package in sorted(packages):
        info=run('dpkg-query','-W','-f=${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n',package).split('\t')
        name=info[0].split(':',1)[0];source=Path('/usr/share/doc')/name/'copyright'
        if not source.is_file():raise RuntimeError('Missing runtime copyright notice: '+package)
        copy(source,notices/(name+'-copyright'))
        dependencies.append({'package':info[0],'version':info[1],'sourcePackage':info[2] or name,'sourceVersion':info[3] if len(info)>3 and info[3] else info[1]})
    for source in Path('/usr/share/common-licenses').iterdir():
        if source.is_file():copy(source,notices/'common'/source.name)
    for name in LINUX_NOTICES:copy(ROOT/'docs'/name,notices/name)
    for source in sorted((dependencies_prefix/'notices').iterdir()):copy(source,notices/('maintained-'+source.name))
    copy(ROOT/'vendor/miniz/LICENSE',notices/'miniz-LICENSE')
    copy(openssl_prefix/'OPENSSL-LICENSE.txt',notices/'OpenSSL-LICENSE.txt')
    copy(ROOT/'docs/BUILD_LIBRARY_REPLACEMENT.txt',notices/'LIBRARY_REPLACEMENT.txt')
    qt_proof['noticeFiles']=copy_qt_notices(qt_notices,notices/'qt')
    qt_proof['runtimeFiles']=sorted(set(sdk_members))
    qt_proof['rebuiltRuntimeFiles']=rebuilt_runtime_bindings(appdir,rebuilt,copied)
    ssl_proof['runtimeFiles']=sorted(set(ssl_members))
    from portable_materials import identity
    maintained['runtimeBindings']=[{'name':Path(name).name,'path':name,
        'original':{k:dependency_outputs[Path(name).name][k] for k in ('size','sha256')},
        'staged':identity(appdir/name)} for name in sorted(set(dependency_members))]
    codec_proof = install_codec(appdir, codec)
    dependency_proof = {'schemaVersion':2,'buildBaseline':'Debian 12 x86-64 / glibc 2.36','qt':qt_proof,'openssl':ssl_proof,'archiveDecoders':public_provenance(),'packages':dependencies,'replacementFile':'licenses/LIBRARY_REPLACEMENT.txt','publicationRequirement':'Distribute the matching actual source/build/replacement materials named in the companion manifest. This inventory is not a written source offer.'}
    dependency_proof['linuxDependencies']=maintained
    if codec_proof is not None: dependency_proof['saveCodec'] = codec_proof
    (appdir/'DEPENDENCY-SOURCES.json').write_text(json.dumps(dependency_proof,indent=2)+'\n')
    from bootstrap_linux_dependencies import verify_staged
    verify_staged(appdir)
    (appdir/'AppRun').write_text(APP_RUN);(appdir/'AppRun').chmod(0o755)
    (appdir/'DriftersToolkit.desktop').write_text('[Desktop Entry]\nType=Application\nName=Drifter’s Toolkit\nExec=AppRun\nIcon=DriftersToolkit\nTerminal=false\nCategories=Game;Utility;\n')
    copy(build/'ui/assets/icon.png',appdir/'DriftersToolkit.png')
    (appdir/'runtime-manifest.json').write_text(json.dumps(inventory(appdir,cli_paths),indent=2)+'\n')
    print('Staged portable runtime: '+str(appdir))
    print('Reconfigure CMake with -DDRIFTER_PORTABLE_MANIFEST='+str(appdir/'runtime-manifest.json')+', rebuild, then run seal.')

def refresh(build,appdir,save_codec_source=None):
    refuse_private_save_preview(build,Path(appdir)/'usr/bin',appdir)
    appdir=appdir.resolve(strict=True);build=build.resolve(strict=True)
    current_inventory=verify_inventory(appdir)
    from bootstrap_linux_dependencies import verify_staged
    verify_staged(appdir,locally_modified=current_inventory.get('locallyModified') is True)
    codec = codec_inputs(build, save_codec_source)
    from save_codec_materials import target as codec_target
    if codec is None and (appdir/'usr/bin'/codec_target('linux')['file']).exists(): raise RuntimeError('Restage explicitly when removing the SaveCodec feature; a previous worker is not silently retained.')
    programs=('DriftersToolkit','DriftersToolkitCLI')
    for name in programs:
        target=appdir/'usr/bin'/name
        if target.is_symlink() or not target.is_file():raise RuntimeError('The staged executable was redirected or is missing: '+name)
        target.unlink();copy(build/name,target,True);run('patchelf','--set-rpath','$ORIGIN/../lib',str(target))
    for name in ['ui/'+name for name in UI]+['catalog/catalog.json','config/nexus.json']:
        staged=appdir/'usr/bin'/name
        if staged.is_symlink() or not staged.is_file():raise RuntimeError('An owned staged resource changed type: '+name)
        staged.unlink();copy(build/name,staged)
    codec_proof = install_codec(appdir, codec)
    if codec_proof is not None:
        proof_path=appdir/'DEPENDENCY-SOURCES.json';proof=json.loads(read_plain(proof_path,8*1024*1024));proof['saveCodec']=codec_proof
        from release_layout import write_artifact
        write_artifact(proof_path,(json.dumps(proof,indent=2)+'\n').encode()).chmod(0o644)
    # The new compiled manifest must accept this exact bundle before archiving.
    for name in programs:
        target=appdir/'usr/bin'/name
        run(str(target),'--verify-only')
        for line in run('lddtree','-l',str(target)).splitlines():
            dependency=Path(line)
            if dependency==target or host_library(dependency.name):continue
            if not (appdir/'usr/lib'/dependency.name).is_file():raise RuntimeError('A new ELF dependency needs fresh staging: '+dependency.name)
    from package_check import check_directory
    check_directory(appdir)
    print('Refreshed and verified portable AppDir: '+str(appdir))

def appimage(appdir,destination,tools,runtime,runtime_provenance,source_companion,reviewer=False):
    from seal_portable_linux import seal_appimage
    return seal_appimage(appdir,destination,tools,runtime,runtime_provenance,source_companion,reviewer)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('mode',choices=('stage','refresh','appimage','reindex'));parser.add_argument('build',type=Path);parser.add_argument('appdir',type=Path);parser.add_argument('--destination',type=Path,default=None);parser.add_argument('--tools',type=Path)
    parser.add_argument('--qt-prefix',type=Path);parser.add_argument('--qt-provenance',type=Path)
    parser.add_argument('--qt-archives',type=Path);parser.add_argument('--qt-notices',type=Path)
    parser.add_argument('--openssl-prefix',type=Path);parser.add_argument('--openssl-provenance',type=Path);parser.add_argument('--openssl-archive',type=Path)
    parser.add_argument('--save-codec-source',type=Path)
    parser.add_argument('--dependencies-prefix',type=Path)
    parser.add_argument('--runtime',type=Path);parser.add_argument('--runtime-provenance',type=Path);parser.add_argument('--source-companion',type=Path);parser.add_argument('--reviewer',action='store_true')
    args=parser.parse_args()
    if args.destination is None:
        args.destination=ROOT/('build-artifacts/review-components' if args.reviewer else 'dist/03-Linux')
    if args.mode=='stage':
        if not all((args.qt_prefix,args.qt_provenance,args.qt_archives,args.qt_notices)):parser.error('stage requires --qt-prefix, --qt-provenance, --qt-archives and --qt-notices')
        if not all((args.openssl_prefix,args.openssl_provenance,args.openssl_archive,args.save_codec_source)):parser.error('stage requires --openssl-prefix, --openssl-provenance and --openssl-archive')
        if args.dependencies_prefix is None:parser.error('stage requires --dependencies-prefix')
        stage(args.build,args.appdir,args.qt_prefix,args.qt_provenance,args.qt_archives,args.qt_notices,args.openssl_prefix,args.openssl_provenance,args.openssl_archive,args.save_codec_source,args.dependencies_prefix)
    elif args.mode=='reindex':reindex(args.appdir)
    elif args.mode=='refresh':refresh(args.build,args.appdir,args.save_codec_source)
    else:
        if not all((args.tools,args.runtime,args.runtime_provenance,args.source_companion)):parser.error('appimage requires --tools, --runtime, --runtime-provenance and --source-companion descriptor')
        appimage(args.appdir,args.destination,args.tools.resolve(strict=True),args.runtime,args.runtime_provenance,args.source_companion,args.reviewer)
if __name__=='__main__':main()
