"""Render the production seasonal CSS/SVG in sandboxed native Qt on X11 and Wayland.

Build the optional linux_seasonal_render_tests CMake target first. Run this as an
ordinary Linux user with Xvfb, Weston, D-Bus and unprivileged user namespaces.
All transient files and isolated display sockets use the OS temporary directory.
No browser sandbox override, host-policy change, network or account is required.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import shutil
import signal
import subprocess
import tempfile
import time


def protected_user():
    if os.name != 'posix' or os.getuid() == 0:
        raise RuntimeError('Run this native Qt regression as an ordinary Linux user.')
    if int(next(line.split()[1] for line in Path('/proc/self/status').read_text().splitlines()
                if line.startswith('CapEff:')), 16):
        raise RuntimeError('The test user must have no effective capabilities.')
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(38, 1, 0, 0, 0):
        raise OSError(ctypes.get_errno(), 'Cannot set no-new-privileges for this test process.')
    probe = subprocess.run(['unshare', '-Ur', 'true'], capture_output=True, text=True, timeout=10)
    if probe.returncode:
        raise RuntimeError('Chromium needs a working user namespace; no sandbox bypass is used. ' + probe.stderr.strip())


def stop(process):
    if process is None or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=5)


def run_backend(args, backend, output, temporary):
    for command in ('dbus-run-session', 'Xvfb' if backend == 'x11' else 'weston'):
        if not shutil.which(command):
            raise RuntimeError('Missing native rendering prerequisite: ' + command)
    runtime = temporary / (backend + '-runtime')
    runtime.mkdir(mode=0o700)
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('QT_', 'QTWEBENGINE_', 'LD_')) and key not in ('DISPLAY', 'WAYLAND_DISPLAY')}
    env['XDG_RUNTIME_DIR'] = str(runtime)
    if args.runtime_root:
        root = args.runtime_root.resolve()
        env.update(LD_LIBRARY_PATH=str(root / 'lib'), QT_PLUGIN_PATH=str(root / 'plugins'),
                   QTWEBENGINEPROCESS_PATH=str(root / 'libexec/QtWebEngineProcess'),
                   QTWEBENGINE_RESOURCES_PATH=str(root / 'resources'),
                   QTWEBENGINE_LOCALES_PATH=str(root / 'translations/qtwebengine_locales'))
    server = None
    server_log = (output / 'compositor.log').open('wb')
    try:
        if backend == 'x11':
            display_file = temporary / 'x11-display-number'
            with display_file.open('wb') as display:
                server = subprocess.Popen(['Xvfb', '-displayfd', str(display.fileno()), '-screen', '0', '1280x900x24',
                                           '-nolisten', 'tcp'], env=env, stdout=server_log, stderr=subprocess.STDOUT,
                                          pass_fds=(display.fileno(),), start_new_session=True)
                for _ in range(100):
                    if display_file.read_text().strip() or server.poll() is not None:
                        break
                    time.sleep(0.05)
            number = display_file.read_text().strip()
            if not number.isdecimal() or server.poll() is not None:
                raise RuntimeError('The private Xvfb display did not start.')
            env.update(DISPLAY=':' + number, QT_QPA_PLATFORM='xcb')
        else:
            socket = 'seasonal-regression'
            server = subprocess.Popen(['weston', '--backend=headless-backend.so', '--use-pixman', '--idle-time=0',
                                       '--width=1280', '--height=900', '--socket=' + socket], env=env,
                                      stdout=server_log, stderr=subprocess.STDOUT, start_new_session=True)
            for _ in range(100):
                if (runtime / socket).exists() or server.poll() is not None:
                    break
                time.sleep(0.05)
            if not (runtime / socket).exists() or server.poll() is not None:
                raise RuntimeError('The private Weston Wayland compositor did not start.')
            env.update(WAYLAND_DISPLAY=socket, QT_QPA_PLATFORM='wayland')
        command = ['dbus-run-session', '--', str(args.binary.resolve()), '--ui-root', str(args.ui_root.resolve()),
                   '--output', str(output)]
        child = subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True, start_new_session=True)
        try:
            native_log, _ = child.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            stop(child)
            native_log, _ = child.communicate(timeout=5)
            (output / 'native.log').write_text(native_log)
            raise RuntimeError('The bounded native rendering subprocess timed out.') from None
        finally:
            stop(child)
        (output / 'native.log').write_text(native_log)
        report_path = output / 'report.json'
        report = json.loads(report_path.read_text()) if report_path.is_file() else {}
        passed = child.returncode == 0 and report.get('passed') is True
        return {'backend': backend, 'passed': passed, 'exitCode': child.returncode, 'command': command,
                'checks': report.get('checks'), 'qtVersion': report.get('qtVersion'),
                'platform': report.get('platform'), 'failures': report.get('failures', [])}
    finally:
        stop(server)
        server_log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--ui-root', required=True, type=Path)
    parser.add_argument('--runtime-root', type=Path, help='Optional reviewed AppDir/usr runtime for this test only.')
    parser.add_argument('--output', type=Path, help='Evidence directory. Defaults to a private OS-temp directory.')
    parser.add_argument('--backend', choices=('both', 'x11', 'wayland'), default='both')
    args = parser.parse_args()
    protected_user()
    if not args.binary.is_file():
        raise RuntimeError('Build the optional native seasonal rendering test executable first.')
    output = args.output.resolve() if args.output else Path(tempfile.mkdtemp(prefix='drifter-seasonal-render-results-'))
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    with tempfile.TemporaryDirectory(prefix='drifter-seasonal-render-') as temporary:
        for backend in ('x11', 'wayland') if args.backend == 'both' else (args.backend,):
            target = output / backend
            target.mkdir()
            rows.append(run_backend(args, backend, target, Path(temporary)))
    assets = {}
    for name in ('style.css', 'assets/seasonal-emoji.svg'):
        assets[name] = hashlib.sha256((args.ui_root / name).read_bytes()).hexdigest()
    report = {'passed': all(row['passed'] for row in rows), 'cases': rows, 'productionAssets': assets,
              'browserSandboxDisabled': False, 'ordinaryUser': True, 'noNewPrivileges': True,
              'limits': 'Headless X11/Wayland software rendering does not replace real GPU, desktop or game acceptance.'}
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    print('Native evidence:', output)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
