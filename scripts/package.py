"""Package the Windows preview from named release inputs, never a user-state folder."""
from pathlib import Path
import argparse
import json
import subprocess
import tempfile
from release_layout import PUBLIC_UI, WINDOWS_NOTICES as PUBLIC_NOTICES, RELEASE_NOTES_NAME, app_version, canonical_name, combined_licenses, read_plain, release_notes_bytes, refuse_private_save_preview, write_zip, SAVE_CODEC_WINDOWS_FILES, SAVE_CODEC_POINTER

ROOT = Path(__file__).resolve().parents[1]

def verify_windows_runtime(output, reviewer=False, run=None):
    """Probe integrity and compiled capabilities without opening a window."""
    output=Path(output)
    refuse_private_save_preview(output)
    from save_codec_materials import verify_worker, verify_pointer
    helper = output/SAVE_CODEC_WINDOWS_FILES[0]
    if helper.exists() or helper.is_symlink(): verify_worker(helper, 'windows')
    pointer = output/SAVE_CODEC_POINTER
    if pointer.exists() or pointer.is_symlink():
        if not helper.is_file(): raise RuntimeError('A SaveCodec source pointer has no matching worker.')
        verify_pointer(read_plain(pointer, 16384))
    run=subprocess.run if run is None else run
    def probe(program, arguments, expected):
        result=run([str(output/program),*arguments],capture_output=True,timeout=20)
        if result.returncode!=expected:
            raise RuntimeError('The Windows build has the wrong integrity or release capabilities: '+program)
    for program in ('DriftersToolkit.exe','DriftersToolkitCLI.exe'):
        read_plain(output/program)
        probe(program,['--verify-only'],0)
        probe(program,['--save-editor-development-verify-only','--verify-only'],1)
    probe('DriftersToolkit.exe',['--reviewer-verify-only'],0 if reviewer else 1)
    # --verify-only keeps even an accidentally supplied private build out of
    # WebView initialization. Ordinary/reviewer builds reject the private flag.
    with tempfile.TemporaryDirectory(prefix='DrifterInterfaceTest-') as temporary:
        report=Path(temporary)/'interface.json'
        probe('DriftersToolkit.exe',['--interface-test-only',str(report),'--verify-only'],1)
        if report.exists():raise RuntimeError('A private interface probe must not be present in a release build.')


def verify_windows_payload(contents, reviewer=False, run=None):
    """Probe the exact immutable bytes that will be written into the ZIP."""
    with tempfile.TemporaryDirectory(prefix='drifter-windows-payload-') as temporary:
        snapshot=Path(temporary)
        for name,body in contents.items():
            canonical_name(name)
            target=snapshot/name;target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('xb') as output:output.write(body)
        verify_windows_runtime(snapshot,reviewer,run)
        refuse_private_save_preview(snapshot)
        for name,body in contents.items():
            if read_plain(snapshot/name)!=body:
                raise RuntimeError('The verified Windows payload changed before packaging: '+name)


def package(output,destination=None,reviewer=False,save_codec_source=None):
    output = output.resolve()
    temporary_root = Path(tempfile.gettempdir()).resolve()
    if output in (ROOT, temporary_root) or not (output.is_relative_to(ROOT) or output.is_relative_to(temporary_root)):
        raise ValueError('Use a dedicated build directory inside the project or OS temporary folder.')
    refuse_private_save_preview(output)
    files = [(output/name, name) for name in ('DriftersToolkit.exe', 'DriftersToolkitCLI.exe', 'WebView2Loader.dll')]
    files += [(output/'ui'/name, 'ui/'+name) for name in PUBLIC_UI]
    files += [(output/name, name) for name in ('catalog/catalog.json', 'config/nexus.json')]
    contents = {}
    for source, name in files:
        contents[name] = read_plain(source)
    helper = output/SAVE_CODEC_WINDOWS_FILES[0]
    codec_notices = {}
    if helper.exists() or helper.is_symlink():
        if save_codec_source is None: raise RuntimeError('Supply the complete matching SaveCodec source closure before public packaging.')
        from save_codec_materials import verify_worker, notices, pointer_bytes
        contents[helper.name] = verify_worker(helper, 'windows')
        contents[SAVE_CODEC_POINTER] = pointer_bytes()
        codec_notices = notices(save_codec_source)
        files.extend(((None, helper.name), (None, SAVE_CODEC_POINTER)))
    elif save_codec_source is not None: raise RuntimeError('A SaveCodec source closure was supplied but the worker is missing.')
    # Every licence text in one file next to the programs.
    notices = {name: read_plain(ROOT/'docs'/name) for name in PUBLIC_NOTICES}
    notices['miniz-LICENSE'] = read_plain(ROOT/'vendor/miniz/LICENSE')
    notices.update(codec_notices)
    contents['LICENSES.txt'] = combined_licenses(notices, [
        "Drifter's Toolkit for Windows uses the components listed in THIRD_PARTY.md. This file contains every",
        'licence text and notice that applies to them. Each text is listed under the file name it belongs to.'],
        ('THIRD_PARTY.md',))
    files.append((None, 'LICENSES.txt'))
    contents[RELEASE_NOTES_NAME] = release_notes_bytes()
    files.append((None, RELEASE_NOTES_NAME))
    config = json.loads(contents['config/nexus.json'])
    if set(config) != {'applicationSlug'} or not isinstance(config['applicationSlug'], str):
        raise RuntimeError('The public Nexus configuration may contain only the application reference.')
    if reviewer and config['applicationSlug']:raise RuntimeError('The pre-registration reviewer package must retain an empty application reference.')
    verify_windows_payload(contents,reviewer)
    dist = Path(destination) if destination is not None else ROOT/'build-artifacts/packages'/app_version()/('windows-review' if reviewer else 'windows')
    dist.mkdir(parents=True,exist_ok=True)
    target = dist/('Drifters-Toolkit-NexusReview-Windows.zip' if reviewer else 'Drifters-Toolkit-Windows.zip')
    write_zip(target,'DriftersToolkit',contents)
    print(f'Packaged {len(files)} public files: {target}')
    return target

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', nargs='?', type=Path, default=ROOT/'out')
    parser.add_argument('--destination',type=Path)
    parser.add_argument('--reviewer',action='store_true')
    parser.add_argument('--save-codec-source',type=Path,help='Complete pinned source closure for the separate GPL worker.')
    args = parser.parse_args()
    package(args.output if args.output.is_absolute() else ROOT/args.output,args.destination,args.reviewer,args.save_codec_source)
