"""Actual Linux CLI preferences work without a game context, including while busy.

The only DaysGone-named process is a child created and stopped by this fixture.
All state is confined to a temporary HOME; no real game path is used.
"""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--reviewer', action='store_true', help='Expect the isolated reviewer preferences namespace.')
    args = parser.parse_args()
    if sys.platform != 'linux':
        raise RuntimeError('This fixture requires native Linux /proc process observation.')
    binary = args.binary.resolve(strict=True)
    outcomes = []
    with tempfile.TemporaryDirectory(prefix='drifter-cli-preferences-') as temporary:
        test_home = Path(temporary)
        env = os.environ.copy()
        env.update(HOME=str(test_home), XDG_DATA_HOME=str(test_home/'data'),
                   XDG_CONFIG_HOME=str(test_home/'config'), XDG_CACHE_HOME=str(test_home/'cache'))
        env.pop('DISPLAY', None)
        env.pop('WAYLAND_DISPLAY', None)
        child = subprocess.Popen([sys.executable, '-c',
            'import ctypes,time;assert ctypes.CDLL(None).prctl(15,b"DaysGone.exe",0,0,0)==0;time.sleep(60)'])
        try:
            comm = Path('/proc')/str(child.pid)/'comm'
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise RuntimeError('The owned synthetic game process stopped too early.')
                if comm.read_text().strip() == 'DaysGone.exe':
                    break
                time.sleep(.02)
            else:
                raise RuntimeError('The owned synthetic game process could not be observed.')

            def execute(words, success=True):
                command = [str(binary), '--game', str(test_home/'not-a-game'), '--json', *words]
                result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=20)
                data = json.loads(result.stdout)
                if success:
                    assert result.returncode == 0 and data['ok'], result.stdout + result.stderr
                else:
                    assert result.returncode == 4 and data['ok'] is False, result.stdout + result.stderr
                outcomes.append({'command': words, 'exitCode': result.returncode, 'ok': data['ok']})
                return data

            execute(['--yes', 'settings', 'set', 'colorMode', 'light'])
            execute(['--yes', 'settings', 'set', 'fontScale', '125'])
            execute(['--yes', 'settings', 'set', 'theme', 'newyear'])
            data = execute(['settings', 'show'])['data']['settings']
            assert (data['colorMode'], data['fontScale'], data['theme']) == ('light', 125, 'newyear')
            execute(['status'], success=False)
            files = [p.relative_to(test_home).as_posix() for p in test_home.rglob('*') if p.is_file()]
            expected = 'data/drifters-toolkit/' + ('reviewer/' if args.reviewer else '') + 'preferences.json'
            assert files == [expected], files
            assert child.poll() is None and comm.read_text().strip() == 'DaysGone.exe'
        finally:
            if child.poll() is None:
                child.terminate()
            child.wait(timeout=5)
    evidence = {'checks': outcomes, 'preferencesPersisted': True,
                'invalidGameRefused': True, 'ownedGameProcessObserved': True,
                'onlyPreferencesFileCreated': True, 'realGamePathsUsed': False}
    if args.evidence:
        args.evidence.write_text(json.dumps(evidence, indent=2)+'\n', encoding='utf-8')
    print('Linux CLI preferences passed: invalid game, owned running-process fixture, persistence and package refusal.')


if __name__ == '__main__':
    main()
