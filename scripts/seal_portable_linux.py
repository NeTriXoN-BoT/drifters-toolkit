"""Seal a validated AppDir with its exact source-backed runtime and component envelope."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from portable_materials import identity, verify_runtime, verify_companion
from release_layout import app_version, combined_licenses, read_plain, release_notes_bytes, refuse_private_save_preview, RELEASE_NOTES_NAME, zip_bytes, write_artifact

MAIN_LICENSES=('GNU-GPL-3.0.txt','GNU-LGPL-2.1.txt','GNU-LGPL-3.0.txt','LIBRARY_REPLACEMENT.txt','NotoEmoji-Images-LICENSE.txt',
               'THIRD_PARTY.md','miniz-LICENSE','nlohmann-json-LICENSE.MIT')

def output_paths(destination,stem,name):
    destination=Path(destination).absolute()
    for parent in (destination,*destination.parents):
        if parent.is_symlink() or parent.exists() and getattr(parent.lstat(),'st_file_attributes',0)&0x400:
            raise RuntimeError('The AppImage output folder is redirected.')
    output=destination/name;archive=destination/(stem+'.zip')
    paths=(output,archive,Path(str(output)+'.sha256'),Path(str(archive)+'.sha256'),destination/'APPIMAGE_MANIFEST.json')
    for target in paths:
        if target.exists() or target.is_symlink():raise RuntimeError('An existing artifact is preserved: '+str(target))
    return output,archive
def launcher(name):
    # Pick one launch strategy before running the application. Never retry a
    # failed application invocation, since that could replay a mutation.
    return ('#!/bin/sh\nset -eu\nbase=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)\n'
        'image="$base/'+name+'"\n'
        'if [ ! -x "$image" ]; then printf "%s\\n" "Make the AppImage executable first: chmod +x \\"$image\\"" >&2; exit 1; fi\n'
        # A readable refusal instead of the dynamic loader's "GLIBC_2.35 not found".
        'libc=$(getconf GNU_LIBC_VERSION 2>/dev/null || true)\n'
        'case "$libc" in "glibc 2."*)\n'
        '    minor=${libc#glibc 2.}; minor=${minor%%[!0-9]*}\n'
        '    if [ -n "$minor" ] && [ "$minor" -lt 35 ]; then\n'
        '        printf "%s\\n" "Drifter\'s Toolkit needs glibc 2.35 or newer, but this system has $libc. Ubuntu 22.04, Linux Mint 21, Debian 12, Fedora 36 and newer releases work." >&2; exit 1\n'
        '    fi;;\nesac\n'
        'if [ ! -c /dev/fuse ] || [ ! -r /dev/fuse ] || [ ! -w /dev/fuse ] || { [ ! -x /usr/bin/fusermount3 ] && [ ! -x /bin/fusermount3 ]; }; then\n'
        '    exec "$image" --appimage-extract-and-run "$@"\nfi\nexec "$image" "$@"\n').encode()

def payload_digest(appdir):
    from package_check import Inspection
    inspection=Inspection(public=True,appdir_prefix='')
    rows=[]
    for source in sorted(Path(appdir).rglob('*')):
        if source.is_symlink():raise RuntimeError('The copied AppDir contains a redirected file.')
        if source.is_dir():continue
        name=source.relative_to(appdir).as_posix()
        record=identity(source,inspection.member_limit(name));inspection.consume(record['size'],name)
        rows.append({'path':name,**record,
                     'mode':source.stat().st_mode&0o777})
        if len(rows)>16384:raise RuntimeError('The copied AppDir has too many files.')
    return hashlib.sha256(json.dumps(rows,sort_keys=True,separators=(',',':')).encode()).hexdigest()


def verify_seal_payload(appdir,reviewer=False):
    """Check the exact AppDir passed to appimagetool, not its earlier input."""
    from package_portable_linux import verify_inventory, run, digest
    from package_check import check_directory
    appdir=Path(appdir)
    refuse_private_save_preview(appdir,appdir/'usr/bin')
    before=payload_digest(appdir)
    inventory=verify_inventory(appdir)
    if inventory.get('locallyModified'):
        raise RuntimeError('A locally modified runtime cannot be sealed as an official build. Restage the verified vendor runtime first.')
    check_directory(appdir)
    gui=appdir/'usr/bin/DriftersToolkit';cli=appdir/'usr/bin/DriftersToolkitCLI'
    run(str(gui),'--verify-only');run(str(cli),'--verify-only')
    clean_env={key:value for key,value in os.environ.items() if not key.startswith(('LD_','QT_','QTWEBENGINE_'))}
    def code(*arguments):return subprocess.run([str(gui),*arguments],env=clean_env,capture_output=True,timeout=60).returncode
    if (code('--reviewer-verify-only')==0)!=reviewer:raise RuntimeError('The native reviewer capability does not match the requested package mode.')
    if code('--sandbox','--smoke-test')==0:raise RuntimeError('Private interface test hooks cannot be shipped.')
    for program in (gui,cli):
        result=subprocess.run([str(program),'--save-editor-development-verify-only'],env=clean_env,capture_output=True,timeout=60)
        if result.returncode==0:raise RuntimeError('Private save-editor development builds cannot be shipped.')
    after=payload_digest(appdir)
    if before!=after:raise RuntimeError('The copied AppDir changed during verification.')
    programs={'gui':identity(gui),'cli':identity(cli)}
    from save_codec_materials import target as codec_target, verify_worker
    worker=appdir/'usr/bin'/codec_target('linux')['file']
    if worker.exists():verify_worker(worker,'linux');programs['codec']=identity(worker)
    return {'runtimeManifestSha256':digest(appdir/'runtime-manifest.json'),'nativePrograms':programs,'payloadSha256':after}


def verified_appdir_copy(appdir,staged,reviewer=False):
    shutil.copytree(appdir,staged)
    return verify_seal_payload(staged,reviewer)


def seal_appimage(appdir,destination,tools,runtime,runtime_provenance,source_companion,reviewer=False):
    refuse_private_save_preview(appdir,Path(appdir)/'usr/bin')
    # Refuse an invalid/mismatched source record before creating build artifacts.
    notes=release_notes_bytes()
    from bootstrap_portable_tools import TOOLS
    from package_portable_linux import verify_inventory, run, digest, MINIMUM_GLIBC
    MINIMUM='.'.join(map(str,MINIMUM_GLIBC))
    from package_check import check_directory
    appdir=Path(appdir).resolve(strict=True);destination=Path(destination).absolute();destination.mkdir(parents=True,exist_ok=True)
    tools=Path(tools).resolve(strict=True);runtime=Path(runtime).resolve(strict=True)
    runtime_proof=verify_runtime(runtime_provenance,runtime)
    source_path=Path(source_companion).absolute();source=json.loads(read_plain(source_path,8*1024*1024))
    if Path(source.get('file','')).name!=source.get('file') or identity(source_path.parent/source['file'])!={key:source.get(key) for key in ('sha256','size')}:
        raise RuntimeError('The actual corresponding-source companion is missing or changed.')
    provenance=json.loads(read_plain(appdir/'DEPENDENCY-SOURCES.json',8*1024*1024))
    verify_companion(source_path.parent/source['file'],source,provenance,runtime_proof)
    verify_seal_payload(appdir,reviewer)
    clean_env={key:value for key,value in os.environ.items() if not key.startswith(('LD_','QT_','QTWEBENGINE_'))}
    for name,(_,expected,size) in TOOLS.items():
        if identity(tools/name)!={'sha256':expected,'size':size}:raise RuntimeError('A fixed packaging tool is missing or changed: '+name)
    stem='Drifters-Toolkit-'+('NexusReview-' if reviewer else '')+'Linux'
    name=stem+'-x86_64.AppImage';output,archive_path=output_paths(destination,stem,name)
    clean_env['ARCH']='x86_64'
    with tempfile.TemporaryDirectory(prefix='drifter-appimage-pack-') as temporary:
        staged=Path(temporary)/'AppDir';payload=verified_appdir_copy(appdir,staged,reviewer)
        staged_provenance=json.loads(read_plain(staged/'DEPENDENCY-SOURCES.json',8*1024*1024))
        verify_companion(source_path.parent/source['file'],source,staged_provenance,runtime_proof)
        notices={}
        for path in sorted((staged/'licenses').rglob('*')):
            if path.is_file():notices[path.relative_to(staged/'licenses').as_posix()]=read_plain(path,32*1024*1024)
        subprocess.run([str(tools/'appimagetool-x86_64.AppImage'),'--appimage-extract-and-run','--runtime-file',str(runtime),'--mksquashfs-opt','-processors','--mksquashfs-opt','2',str(staged),str(output)],check=True,env=clean_env)
        icon=staged/'.DirIcon'
        if not icon.is_symlink() or os.readlink(icon)!='DriftersToolkit.png':raise RuntimeError('Unexpected AppImage icon alias.')
        icon.unlink()
        if verify_seal_payload(staged,reviewer)!=payload:raise RuntimeError('The sealed AppDir payload changed during AppImage construction.')
    manifest={'schemaVersion':1,'format':'appimage-x86_64','mode':'nexus-reviewer' if reviewer else 'public','applicationVersion':app_version(),'qtVersion':'6.11.3','minimumGlibc':MINIMUM,'minimumGlibcxx':'3.4.30','minimumCpu':'x86-64-v2','runtime':{'file':name,**identity(output)},'runtimeManifestSha256':payload['runtimeManifestSha256'],'nativePrograms':payload['nativePrograms'],'appImageRuntime':{**identity(runtime),'provenanceSha256':digest(runtime_provenance)},'sourceCompanion':source,'capabilities':{'reviewerConnect':reviewer,'interfaceTesting':False}}
    requirements={'schemaVersion':1,'format':'appimage-x86_64','architecture':'x86_64','minimumGlibc':MINIMUM,'minimumGlibcxx':'3.4.30','minimumCpu':'x86-64-v2','qtVersion':'6.11.3','displayProtocols':['x11','wayland'],'sandboxRequired':True,'requirements':['A compatible desktop session, GPU/display drivers and Mesa or vendor OpenGL/EGL libraries, plus the system libexpat and fontconfig that every desktop provides.','Linux unprivileged user namespaces and seccomp-bpf must be available to the Qt WebEngine sandbox. Host AppArmor or administrator policy can prevent this; Launch-DriftersToolkit.sh --check-sandbox explains the cause and, on Ubuntu 24.04 and newer, prints a one-time AppArmor rule for this AppImage only.','The launch helper uses OS-temp extraction when FUSE is unavailable. It never disables the sandbox.','Secret Service is needed to retain a Nexus login. No live Nexus account or game acceptance is implied by package verification.'],'cli':'Launch-DriftersToolkit.sh --cli --help'}
    contents={name:read_plain(output,1024*1024*1024),'Launch-DriftersToolkit.sh':launcher(name),'LINUX_REQUIREMENTS.json':(json.dumps(requirements,indent=2)+'\n').encode(),'APPIMAGE_MANIFEST.json':(json.dumps(manifest,indent=2)+'\n').encode()}
    if 'codec' in payload['nativePrograms']:
        from save_codec_materials import POINTER_NAME, verify_pointer
        pointer=read_plain(appdir/'usr/bin'/POINTER_NAME,16384);verify_pointer(pointer)
        contents[POINTER_NAME]=pointer
    # Every licence text and copyright notice (the toolkit's, Qt's, each bundled library's and the AppImage
    # runtime's) in one LICENSES.txt. The same files also stay inside the AppImage's licenses folder.
    # No archive inside the ZIP: Nexus quarantines nested archives.
    for entry in runtime_proof['licenses']:
        notices['appimage-runtime/'+entry['file']]=read_plain(Path(runtime_provenance).parent/entry['file'],32*1024*1024)
    contents['LICENSES.txt']=combined_licenses(notices,[
        "Drifter's Toolkit for Linux bundles Qt and other open-source libraries. This file contains every",
        'licence text and copyright notice that applies to them, and to the toolkit itself. Texts that are',
        'identical for several components are included once, with the list of components they belong to.',
        'The section LIBRARY_REPLACEMENT.txt explains how to use modified versions of the LGPL libraries.',
        'The same files are also inside the AppImage, in its licenses folder.'],MAIN_LICENSES)
    # Outside the AppImage so the running, older loader can authenticate and show
    # these notes before extracting or starting any new program.
    contents[RELEASE_NOTES_NAME]=notes
    root='DriftersToolkit-Linux';body=zip_bytes(root,contents,{name,'Launch-DriftersToolkit.sh'})
    with archive_path.open('xb') as target:target.write(body)
    for target in (output,archive_path):write_artifact(destination/(target.name+'.sha256'),(digest(target)+'  '+target.name+'\n').encode(),replace=False)
    write_artifact(destination/'APPIMAGE_MANIFEST.json',(json.dumps(manifest,indent=2)+'\n').encode(),replace=False)
    print('Created '+str(archive_path));return archive_path

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('appdir','destination','tools','runtime','runtime_provenance','source_companion'):parser.add_argument(name,type=Path)
    parser.add_argument('--reviewer',action='store_true');args=parser.parse_args()
    seal_appimage(args.appdir,args.destination,args.tools,args.runtime,args.runtime_provenance,args.source_companion,args.reviewer)
