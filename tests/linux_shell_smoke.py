"""Exercise the actual Linux executable in disposable state; never mount a game."""
from pathlib import Path
import argparse
from contextlib import contextmanager
import json
import hashlib
import time
import os
import select
import signal
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def run(executable, arguments, environment, expected=0, timeout=45):
    with subprocess.Popen([str(executable), *arguments], env=environment,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                          start_new_session=True) as process:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            stdout, stderr = process.communicate(timeout=5)
            raise RuntimeError('Native executable exceeded its test deadline.\n'+stdout[-3000:]+stderr[-4000:])
        result = subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
    if result.returncode != expected:
        raise RuntimeError(f'{Path(executable).name} returned {result.returncode}, expected {expected}\n'
                           + result.stdout[-3000:] + result.stderr[-4000:])
    return result


def teardown_probe(executable, environment, report_path=None):
    """Exercise a real active Shell callback during application quit/destruction."""
    result = run(executable, ['--sandbox', '--smoke-test', '--smoke-view', 'teardown'], environment, timeout=35)
    rows = [line[len('Linux Qt teardown: '):] for line in result.stdout.splitlines() if line.startswith('Linux Qt teardown: ')]
    if len(rows) != 1:
        raise RuntimeError('The real Shell teardown did not report exactly one completed probe.\n'+result.stdout[-4000:])
    actual = json.loads(rows[0])
    expected = ('passed','shellDestroyed','workerStarted','nativeDrainEntered','watcherAvailable',
                'actualWatchCallbackCompleted','backendWorkerStopped')
    if any(actual.get(key) is not True for key in expected) or actual.get('failureCode') != 0:
        raise RuntimeError('The real Shell teardown callback did not survive native drain: '+json.dumps(actual))
    program = executable.parent/'usr/bin/DriftersToolkit' if executable.name == 'AppRun' else executable
    report = {'status':'passed-native-linux-shell-watcher-teardown','version':actual['version'],
              'sandboxEnabled':True,'realGameOrAccountUsed':False,'actual':actual,
              'executableSha256':hashlib.sha256(program.read_bytes()).hexdigest(),
              'sourceFiles':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                             for name in ('src/linux_main.cpp','tests/linux_shell_smoke.py')}}
    if report_path is not None:
        report_path.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


@contextmanager
def managed_display(environment, args, root):
    process = None
    log = root/'compositor.log'
    output = None
    platform = 'wayland' if args.wayland else 'x11'
    try:
        if args.wayland:
            output = log.open('wb')
            socket_name = 'drifter-test-wayland'
            environment.pop('DISPLAY', None)
            environment.update(WAYLAND_DISPLAY=socket_name, XDG_SESSION_TYPE='wayland', QT_QPA_PLATFORM='wayland')
            if args.software_rendering:
                # Weston headless has no input seat. Qt 6.4's Wayland text-input
                # adapter dereferences that absent seat when an input gains focus.
                # Keep this override inside the isolated headless test only.
                environment['QT_IM_MODULE'] = 'compose'
            compositor_env = environment.copy()
            compositor_env['LIBGL_ALWAYS_SOFTWARE'] = '1'
            process = subprocess.Popen(['weston','--backend=headless-backend.so','--use-gl',
                                        '--socket='+socket_name,'--width='+str(getattr(args,'screen_size',(3840,2160))[0]),'--height='+str(getattr(args,'screen_size',(3840,2160))[1]),
                                        '--idle-time=0','--no-config'], env=compositor_env,
                                       stdout=output, stderr=output, start_new_session=True)
            socket = Path(environment['XDG_RUNTIME_DIR'])/socket_name
            deadline = time.monotonic()+10
            while not socket.exists():
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('The isolated Wayland compositor could not start.\n'+log.read_text(errors='replace')[-5000:])
                time.sleep(.05)
        elif args.xvfb:
            environment.pop('WAYLAND_DISPLAY', None)
            environment.update(XDG_SESSION_TYPE='x11', QT_QPA_PLATFORM='xcb')
            read_fd, write_fd = os.pipe()
            try:
                process = subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0',
                                            str(getattr(args,'screen_size',(3840,2160))[0])+'x'+str(getattr(args,'screen_size',(3840,2160))[1])+'x24','-nolisten','tcp'], pass_fds=(write_fd,),
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                           start_new_session=True)
                os.close(write_fd)
                write_fd = -1
                if not select.select([read_fd], [], [], 10)[0]:
                    raise RuntimeError('Xvfb did not become ready within ten seconds.')
                number = os.read(read_fd, 64).strip().decode('ascii')
                if not number.isdigit():
                    raise RuntimeError('Xvfb returned an invalid display identifier.')
                environment['DISPLAY'] = ':'+number
            finally:
                os.close(read_fd)
                if write_fd >= 0:
                    os.close(write_fd)
        elif environment.get('WAYLAND_DISPLAY') and environment.get('QT_QPA_PLATFORM') == 'wayland':
            platform = 'wayland'
        elif not environment.get('DISPLAY'):
            raise RuntimeError('Use --xvfb, --wayland or an isolated existing display. Chromium sandbox stays enabled.')
        yield platform
    finally:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
        if output is not None:
            output.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--gui', action='store_true', help='Run the real Qt renderer under an existing display or Xvfb.')
    displays = parser.add_mutually_exclusive_group()
    displays.add_argument('--xvfb', action='store_true', help='Start an isolated X11 display.')
    displays.add_argument('--wayland', action='store_true', help='Start an isolated native Wayland compositor.')
    parser.add_argument('--appdir', action='store_true', help='Copy a complete sealed portable AppDir for this test.')
    parser.add_argument('--software-rendering', action='store_true', help='Use Qt software rendering inside the isolated headless display.')
    parser.add_argument('--management-only', action='store_true', help='Repeat enlarged-text mod/diagnostics screens and renderer recovery after a management-only change.')
    parser.add_argument('--screenshots', type=Path)
    args = parser.parse_args()
    if args.management_only and not args.gui:parser.error('--management-only requires --gui.')
    build = args.build.resolve()
    if os.name != 'posix':
        raise RuntimeError('This native test must run on Linux.')
    with tempfile.TemporaryDirectory(prefix='drifter-linux-shell-') as temporary:
        root = Path(temporary)
        app = root/'app'
        executable = app/'AppRun' if args.appdir else app/'DriftersToolkit'
        if args.appdir:
            shutil.copytree(build, app, symlinks=True)
            app = app/'usr/bin'
        else:
            app.mkdir()
            shutil.copy2(build/'DriftersToolkit', app/'DriftersToolkit')
            for folder in ('ui','catalog','config'):
                shutil.copytree(build/folder, app/folder)
        home, data, runtime = root/'home', root/'data', root/'runtime'
        for path in (home, data, runtime):
            path.mkdir(mode=0o700)
        environment = os.environ.copy()
        environment.update(HOME=str(home), XDG_DATA_HOME=str(data),
                           XDG_CONFIG_HOME=str(root/'config'), XDG_STATE_HOME=str(root/'state'),
                           XDG_CACHE_HOME=str(root/'cache'), XDG_RUNTIME_DIR=str(runtime))
        if args.software_rendering:
            environment.update(LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe', QT_QUICK_BACKEND='software')
        run(executable, ['--verify-only'], environment)
        assert not list(data.iterdir()), 'Integrity-only verification must not create account or game state.'
        script = app/'ui/app.js'
        original = script.read_bytes()
        script.write_bytes(original+b'\n// integrity test\n')
        run(executable, ['--verify-only'], environment, expected=1)
        script.write_bytes(original)
        catalog = app/'catalog/catalog.json'
        missing = catalog.with_suffix('.missing')
        catalog.rename(missing)
        run(executable, ['--verify-only'], environment, expected=1)
        missing.rename(catalog)
        script.unlink()
        fixture = root/'same-script.js'
        fixture.write_bytes(original)
        script.symlink_to(fixture)
        run(executable, ['--verify-only'], environment, expected=1)
        script.unlink()
        os.link(fixture, script)
        run(executable, ['--verify-only'], environment, expected=1)
        script.unlink()
        script.write_bytes(original)
        run(executable, ['--verify-only'], environment)
        qt_config = app/'qt.conf'
        qt_original = qt_config.read_bytes() if qt_config.exists() else None
        qt_config.write_bytes((qt_original or b'')+b'\n[Paths]\nPlugins=/untrusted-test\n')
        run(executable, ['--verify-only'], environment, expected=1)
        if qt_original is None:
            qt_config.unlink()
        else:
            qt_config.write_bytes(qt_original)
        run(executable, ['--verify-only'], environment)
        print('Native Linux integrity passed: clean, modified, missing, links and unexpected/altered Qt configuration.')
        if args.gui:
            screenshot_dir = args.screenshots.resolve() if args.screenshots else root/'screenshots'
            screenshot_dir.mkdir(parents=True, exist_ok=True)
            with managed_display(environment, args, root) as platform:
                scenarios = [('1280x800',100,'overview','dark',None),('1440x955',100,'overview','dark',None),
                             ('3840x2160',100,'overview','dark',None),('1280x800',150,'mods','dark',None),
                             ('1440x955',150,'help','dark',None),('3840x2160',150,'settings','dark',None),
                             ('1440x955',150,'diagnostics','dark',None),('1280x800',150,'mods','light',None),
                             ('1440x955',150,'help','light',None),('1440x955',150,'settings','light',None),
                             ('1440x955',150,'diagnostics','light',None),('3840x2160',100,'overview','light',None),
                             ('1440x955',150,'settings','auto','dark'),('1440x955',150,'settings','auto','light')]
                if args.management_only:
                    scenarios=[('1280x800',150,'mods','dark',None),('1280x800',150,'mods','light',None),
                               ('1440x955',150,'diagnostics','dark',None),('1440x955',150,'diagnostics','light',None)]
                scenario_reports=[]
                for size,font,view,color,palette in scenarios:
                    suffix = '-palette-'+palette if palette else ''
                    image = screenshot_dir/(f'linux-{platform}-{size}-{view}-font{font}-{color}{suffix}.png')
                    arguments = ['--sandbox','--smoke-test','--smoke-size',size,'--smoke-font',str(font),
                                 '--smoke-view',view,'--smoke-color',color,'--smoke-output',str(image)]
                    if palette:
                        arguments += ['--smoke-system-palette',palette]
                    result = run(executable, arguments, environment)
                    if 'Linux Qt smoke passed' not in result.stdout or not image.is_file() or image.stat().st_size < 10000:
                        raise RuntimeError('The native Qt render test did not produce its expected result at '+size)
                    rows = [line.split('Linux Qt DOM: ',1)[1] for line in result.stdout.splitlines() if line.startswith('Linux Qt DOM: ')]
                    dom = json.loads(rows[-1]) if rows else {}
                    expected_color = palette if palette else color
                    if expected_color != 'auto' and dom.get('colorMode') != expected_color:
                        raise RuntimeError('The requested color mode did not render: '+result.stdout[-3000:])
                    if palette and dom.get('systemPalette') != 'linux':
                        raise RuntimeError('The native palette was not applied: '+result.stdout[-3000:])
                    raster_rows=[line.split('Linux Qt raster: ',1)[1] for line in result.stdout.splitlines() if line.startswith('Linux Qt raster: ')]
                    raster=json.loads(raster_rows[-1]) if raster_rows else {}
                    if not raster.get('modeMatches'):
                        raise RuntimeError('The native screenshot does not match the measured color mode.')
                    scenario_reports.append({'window':size,'view':view,'textPreference':font,'color':color,'nativePalette':palette,'dom':dom,'raster':raster})
                    (screenshot_dir/f'{platform}-view-results.json').write_text(json.dumps({'executableSha256':hashlib.sha256((app/'DriftersToolkit').read_bytes()).hexdigest(),'sandboxEnabled':True,'scenarioSet':'management'if args.management_only else'full','scenarios':scenario_reports},indent=2)+'\n',encoding='utf-8')
                    print(f'Native Linux {platform} rendering passed: {size}, {view}, text {font}%, {color}{suffix}, sandbox enabled.')
                recovery = run(executable, ['--sandbox','--smoke-test','--smoke-renderer-recovery'], environment)
                expected = ['Linux Qt renderer recovery:','"terminations":3','"automaticReloads":2',
                            '"nativeFallback":true','"workerSurvived":true','"uniqueRequests":true','"mutations":0']
                if not all(marker in recovery.stdout for marker in expected):
                    raise RuntimeError('Native Qt renderer recovery did not produce the expected bounded result.\n'+recovery.stdout[-4000:])
                print(f'Native Linux {platform} renderer recovery passed: three terminations, two reloads, native fallback, worker retained, no mutation replay.')
                teardown_probe(executable, environment, screenshot_dir/f'{platform}-teardown-results.json')
                print(f'Native Linux {platform} teardown passed: active stored watch callback survived application quit; worker stopped before watcher removal.')
            print('Rendered screenshots: '+str(screenshot_dir))

if __name__ == '__main__':
    main()
