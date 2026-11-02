"""Run the real headless entrypoint with disposable files and no real account."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import struct
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--appdir', action='store_true', help='Exercise the CLI subset from a complete portable AppDir.')
    parser.add_argument('--running-game-refusal', action='store_true',
                        help='Accept only a verified process-guard refusal instead of the local-import success check when a game is active or cannot be observed.')
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    name = 'DriftersToolkitCLI.exe' if os.name == 'nt' else 'DriftersToolkitCLI'
    checks = 0
    import_refused = False
    with tempfile.TemporaryDirectory(prefix='drifter-cli-native-') as temporary:
        root = Path(temporary)
        app = root/'app'
        if args.appdir:
            if os.name != 'posix':
                raise RuntimeError('The portable AppDir test is Linux-only.')
            shutil.copytree(build, app, symlinks=True)
            binary_dir = app/'usr/bin'
            source_dir = build/'usr/bin'
            # Only our disposable copy is changed. Remove every GUI-only
            # runtime member, GUI assets and the GUI executable so the CLI
            # cannot accidentally satisfy this test through those components.
            manifest = json.loads((app/'runtime-manifest.json').read_text())
            assert manifest.get('cliReady') is True
            for row in manifest['files']:
                if row.get('cli'):
                    continue
                target = app/row['path']
                assert target.resolve().is_relative_to(app.resolve())
                target.unlink()
            shutil.rmtree(binary_dir/'ui')
            (binary_dir/'DriftersToolkit').unlink()
            for folder in ('catalog','config'):
                (binary_dir/folder).rename(root/('held-'+folder))
        else:
            app.mkdir()
            binary_dir = app
            source_dir = build
            shutil.copy2(build/name, binary_dir/name)
        executable = binary_dir/name
        initial_bin_files = sorted(path.name for path in binary_dir.iterdir())
        environment = os.environ.copy()
        for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'QT_QPA_PLATFORM', 'QT_PLUGIN_PATH',
                    'QT_QPA_PLATFORM_PLUGIN_PATH', 'QTWEBENGINE_CHROMIUM_FLAGS',
                    'QTWEBENGINE_DISABLE_SANDBOX', 'QTWEBENGINE_REMOTE_DEBUGGING'):
            environment.pop(key, None)
        home = root/'home'
        runtime = root/'runtime'
        home.mkdir(mode=0o700)
        runtime.mkdir(mode=0o700)
        environment.update(HOME=str(home), XDG_DATA_HOME=str(root/'data'),
                           XDG_CONFIG_HOME=str(root/'config'), XDG_STATE_HOME=str(root/'state'),
                           XDG_CACHE_HOME=str(root/'cache'), XDG_RUNTIME_DIR=str(runtime))

        def run(arguments, expected=0, structured=True, launcher=False):
            nonlocal checks
            command = [str(app/'AppRun'),'--cli'] if launcher else [str(executable)]
            result = subprocess.run([*command, *arguments], env=environment, cwd=root,
                                    capture_output=True, text=True, encoding='utf-8',
                                    timeout=30, **({'creationflags': subprocess.CREATE_NO_WINDOW}
                                                  if os.name == 'nt' else {}))
            allowed = expected if isinstance(expected, tuple) else (expected,)
            if result.returncode not in allowed:
                raise AssertionError(f'CLI returned {result.returncode}, expected {allowed}: '
                                     +result.stdout[-3000:]+result.stderr[-2000:])
            checks += 1
            return json.loads(result.stdout) if structured else result

        # No catalog, UI, configuration, WebView loader or display is present.
        assert 'Usage:' in run(['--help'], structured=False).stdout
        assert run(['--json', '--version'])['data']['version']
        assert run(['--json', 'enable', 'no-recoil'], 3)['exitCode'] == 3
        assert run(['--sandbox', '--json', 'profiles', 'launch', 'Example', '--yes'], 6)['exitCode'] == 6
        assert run(['--json', '--apikey', 'private-test-value'], 2)['exitCode'] == 2
        assert sorted(path.name for path in binary_dir.iterdir()) == initial_bin_files
        assert not list(home.iterdir()), 'Pure help and refused commands must not initialize user state.'

        for folder in ('catalog', 'config'):
            shutil.copytree(source_dir/folder, binary_dir/folder)
        run(['--json', '--verify-only'])
        catalog_document = json.loads((binary_dir/'catalog/catalog.json').read_text(encoding='utf-8'))
        assert catalog_document.get('schemaVersion') == 1 and isinstance(catalog_document.get('mods'), list)
        catalog_ids = [row['id'] for row in catalog_document['mods']]
        assert catalog_ids and all(isinstance(value, str) and value for value in catalog_ids)
        assert len(catalog_ids) == len(set(catalog_ids)), 'The verified catalogue must have unique mod IDs.'
        if args.appdir:
            assert run(['--json','--verify-only'], launcher=True)['data']['verified'] is True
        listing = run(['--sandbox', '--json', 'mods', 'list'])
        assert listing.get('schemaVersion') == 1 and listing.get('ok') is True
        listed_ids = [row['id'] for row in listing['data']['mods']]
        assert len(listed_ids) == len(catalog_ids) and set(listed_ids) == set(catalog_ids), \
            'The CLI listing must match every ID in its verified bundled catalogue.'
        assert run(['--sandbox', '--json', 'settings', 'show'])['data']['settings']['autoScale'] is True
        # A real synthetic legacy PAK exercises native local review followed by
        # confirmation in one headless invocation, without an authored release.
        def fstring(value):
            data=value.encode('utf-8')+b'\0'
            return struct.pack('<i',len(data))+data
        index=(fstring('../../../')+struct.pack('<i',1)
               +fstring('BendGame/Content/CliLocalSmoke.uasset')
               +struct.pack('<QQQI',0,8,8,0)+bytes(20)+struct.pack('<BI',0,0))
        local_file=root/'ThirdPartyLocal_P.pak'
        local_file.write_bytes(bytes(8)+index+struct.pack('<IIQQ',0x5a6f12e1,3,8,len(index))
                               +hashlib.sha1(index).digest())
        original_local = local_file.read_bytes()
        # Fresh native observation is evidence only. It never overrides the
        # process guard in the actual production entrypoint under test.
        status = run(['--sandbox', '--json', 'status'])
        assert status.get('schemaVersion') == 1 and status['ok'] and status['data']['mods']['total'] == len(catalog_ids)
        assert status['data']['processStatus'] in ('stopped', 'running', 'unknown')
        active = Path(status['data']['activePath'])
        assert active.is_relative_to(Path(tempfile.gettempdir()).resolve())
        assert not active.is_relative_to(app)
        local_import=run(['--sandbox','--json','import',str(local_file),'--yes'],
                         (0,4) if args.running_game_refusal else 0)
        assert local_file.read_bytes() == original_local, 'Import/refusal must preserve the selected source.'
        if local_import['ok']:
            local_mods=[row for row in local_import['data']['mods'] if row['id'].startswith('local-')]
            assert len(local_mods)==1 and local_mods[0]['identityStatus']=='local-import'
            assert local_mods[0]['enabled'] is False
            assert all('reviewToken' not in review for review in local_import.get('review',[]))
        else:
            assert args.running_game_refusal and local_import['exitCode'] == 4
            assert local_import['command'] == 'import'
            assert local_import['error'] in (
                'Close Days Gone before changing packages, settings or profiles.',
                'The game process could not be checked. No change was started; run diagnostics before trying again.',
                'Close Days Gone before changing mods, settings, or profiles.',
                'Close Days Gone before inspecting and importing downloaded packages.',
            ), 'An unrelated import error must not count as a process-guard test.'
            assert not local_import.get('review'), 'A preflight process refusal must not reach package review.'
            import_refused = True
        run(['--sandbox', '--json', 'directories', 'scan'])
        diagnostics = run(['--sandbox', '--json', 'diagnostics', 'run'], (0, 5))
        assert diagnostics['data']['checks'] and diagnostics['data']['summary']
        exported = run(['--sandbox', '--json', 'diagnostics', 'export', 'support-report.json', '--yes'])
        report = root/'support-report.json'
        assert exported['data']['exported'] is True and report.is_file()
        original_report = report.read_bytes()
        assert json.loads(original_report)['summary']
        run(['--sandbox', '--json', 'diagnostics', 'export', 'support-report.json', '--yes'], 4)
        assert report.read_bytes() == original_report, 'An existing report must never be overwritten.'
        run(['--sandbox', '--json', 'profiles', 'list'])
        run(['--sandbox', '--json', 'crashlogs', 'list'])

        catalog = binary_dir/'catalog/catalog.json'
        original = catalog.read_bytes()
        catalog.write_bytes(original+b'\n')
        run(['--json', '--verify-only'], 4)
        refused = run(['--sandbox', '--json', 'status'], 4)
        assert refused['ok'] is False
        catalog.write_bytes(original)
        configuration = binary_dir/'config/nexus.json'
        configuration.unlink()
        run(['--json', '--verify-only'], 4)
        assert 'Usage:' in run(['--help'], structured=False).stdout
        assert sorted(path.name for path in binary_dir.iterdir()) == sorted(initial_bin_files+['catalog','config'])
        assert not (root/'data').exists(), 'Sandbox commands must not create persistent XDG state.'
    if import_refused:
        print(f'Native headless CLI passed {checks-1} other checks plus one explicit live-game/unknown-process refusal with source preservation. Successful local import was not exercised in this run.')
    else:
        print(f'Native headless CLI passed {checks} checks without a display or GUI resources, including successful local import.')


if __name__ == '__main__':
    main()
