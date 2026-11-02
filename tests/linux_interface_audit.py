"""Render the native interface in isolated monitors; never mount a game/account."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import re
import shutil
import sys
import tempfile
from types import SimpleNamespace
from linux_shell_smoke import managed_display, run


def stage_public_save_codec(build, app):
    """Stage only the frozen public worker; old private helpers stay outside."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
    from save_codec_materials import verify_worker
    helper = Path(build) / 'DriftersToolkitSaveCodec'
    if not helper.exists() and not helper.is_symlink():
        return None
    body = verify_worker(helper, 'linux')
    destination = Path(app) / helper.name
    with destination.open('xb') as output:
        output.write(body)
    destination.chmod(0o755)
    return hashlib.sha256(body).hexdigest()


def contrast_ratio(first, second):
    def luminance(color):
        channels = [int(value)/255 for value in re.findall(r'\d+', color)[:3]]
        assert len(channels) == 3, color
        linear = [value/12.92 if value <= .04045 else ((value+.055)/1.055)**2.4 for value in channels]
        return sum(weight*value for weight, value in zip((.2126, .7152, .0722), linear))
    values = sorted((luminance(first), luminance(second)))
    return (values[1]+.05)/(values[0]+.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--wayland', action='store_true')
    parser.add_argument('--screenshots', type=Path, required=True)
    parser.add_argument('--quick', action='store_true')
    parser.add_argument('--case')
    parser.add_argument('--accessibility', action='store_true')
    args = parser.parse_args()
    build = args.build.resolve(strict=True)
    evidence = args.screenshots.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    platform = 'wayland' if args.wayland else 'x11'
    cases = [
        ('1080p-100', 1920, 1080, 1, '1440x955', 100, 'overview', 'dark', 'halloween', 100),
        ('1440p-100', 2560, 1440, 1, '1440x955', 100, 'settings', 'dark', 'spring', 135),
        ('1440p-125', 2560, 1440, 1.25, '1440x955', 100, 'settings', 'light', 'summer', 105),
        ('1440p-150', 2560, 1440, 1.5, '1440x955', 100, 'overview', 'dark', 'autumn', 100),
        ('4k-100-small', 3840, 2160, 1, '960x700', 100, 'mods', 'dark', 'winter', 160),
        ('4k-100', 3840, 2160, 1, '1440x955', 100, 'overview', 'dark', 'halloween', 200),
        ('4k-150', 3840, 2160, 1.5, '1440x955', 100, 'settings', 'light', 'spring', 135),
        ('4k-200', 3840, 2160, 2, '1440x955', 100, 'overview', 'dark', 'winter', 100),
        ('ultrawide', 5120, 1440, 1, '1440x955', 125, 'help', 'light', 'autumn', 135),
        ('4k-text150', 3840, 2160, 1, '1280x800', 150, 'mods', 'light', 'summer', 200),
    ]
    cases.append(('4k-fullwindow',3840,2160,1,'3840x2160',150,'settings','dark','winter',200))
    cases.append(('saves-1080p',1920,1080,1,'1440x955',100,'saves','dark','halloween',100))
    cases.append(('profiles-create',1920,1080,1,'1440x955',100,'profiles','dark','halloween',100))
    cases.append(('changelog',1920,1080,1,'1280x800',125,'changelog','dark','halloween',100))
    if args.quick:
        cases = [cases[0], cases[4], cases[9]]
    else:
        cases += [(f'{season}-{mode}', 2560, 1440, 1, '1440x955', 110,
                   'overview', mode, season, 135)
                  for season in ('spring', 'summer', 'autumn', 'winter', 'halloween', 'newyear')
                  for mode in ('dark', 'light')]
    if args.accessibility:
        cases = [(name, 2560, 1440, 1, '1280x800', 150, 'settings', mode, season, 135)
                 for name, mode, season in (
                     ('reduced-dark', 'dark', 'halloween'), ('reduced-light', 'light', 'spring'),
                     ('contrast-dark', 'dark', 'winter'), ('contrast-light', 'light', 'summer'),
                     ('original-dark', 'dark', 'default'), ('original-light', 'light', 'default'))]
    if args.case:
        cases=[entry for entry in cases if entry[0]==args.case]
        if not cases: raise ValueError('Unknown interface scenario')
    results = []
    with tempfile.TemporaryDirectory(prefix='drifter-interface-audit-') as temporary:
        root = Path(temporary)
        app = root/'app'
        app.mkdir()
        shutil.copy2(build/'DriftersToolkit', app/'DriftersToolkit')
        for folder in ('ui', 'catalog', 'config'):
            shutil.copytree(build/folder, app/folder)
        # Public GUI integrity pins this worker too. A stale private decoder in
        # a reused build cache is deliberately not copied into the test app.
        codec_hash = stage_public_save_codec(build, app)
        executable = app/'DriftersToolkit'
        for name, width, height, ratio, window, font, view, mode, season, expected in cases:
            state = root/name
            state.mkdir()
            environment = os.environ.copy()
            for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH',
                        'QTWEBENGINE_CHROMIUM_FLAGS', 'QTWEBENGINE_DISABLE_SANDBOX',
                        'QTWEBENGINE_REMOTE_DEBUGGING', 'QTWEBENGINEPROCESS_PATH', 'QT_SCREEN_SCALE_FACTORS'):
                environment.pop(key, None)
            for label in ('home', 'data', 'runtime', 'config', 'cache'):
                (state/label).mkdir(mode=0o700)
            environment.update(HOME=str(state/'home'), XDG_DATA_HOME=str(state/'data'),
                               XDG_CONFIG_HOME=str(state/'config'), XDG_CACHE_HOME=str(state/'cache'),
                               XDG_RUNTIME_DIR=str(state/'runtime'), QT_SCALE_FACTOR=str(ratio),
                               LIBGL_ALWAYS_SOFTWARE='1', GALLIUM_DRIVER='llvmpipe',
                               QT_QUICK_BACKEND='software')
            options = SimpleNamespace(wayland=args.wayland, xvfb=not args.wayland,
                                      software_rendering=True, screen_size=(width, height))
            image = evidence/f'{platform}-{name}.png'
            with managed_display(environment, options, state):
                completed = run(executable, ['--sandbox', '--smoke-test', '--smoke-size', window,
                                            '--smoke-font', str(font), '--smoke-view', view,
                                            '--smoke-color', mode, '--smoke-season', season,
                                            '--smoke-output', str(image)]+(['--smoke-reduced-motion'] if name.startswith('reduced-') else ['--smoke-high-contrast'] if name.startswith('contrast-') else []), environment)
            (evidence/f'{platform}-{name}.stderr.txt').write_text(completed.stderr, encoding='utf-8')
            lines = [line.split('Linux Qt DOM: ', 1)[1] for line in completed.stdout.splitlines()
                     if line.startswith('Linux Qt DOM: ')]
            assert lines, completed.stdout+completed.stderr
            actual = json.loads(lines[-1])
            # Ten main pages, including Workshop and Saves, plus Settings.
            assert actual['nav'] == 11, actual
            if view == 'profiles':
                workflow = actual['profileWorkflow']
                assert workflow['passed'] is True and not workflow['running'], actual
                assert workflow['checks'] and all(workflow['checks'].values()), actual
                assert workflow['files'] == {'ready': True, 'manifests': 2, 'owned': 2, 'empty': 2}, actual
            if view == 'changelog':
                workflow = actual['changelogWorkflow']
                assert workflow['passed'] is True and not workflow['running'], actual
                assert workflow['selections'] == 128 and all(workflow['checks'].values()), actual
            (evidence/f'{platform}-{name}.dom.json').write_text(json.dumps(actual,indent=2)+'\n',encoding='utf-8')
            scaling, decoration = actual['scaling'], actual['seasonal']
            assert scaling['automatic'] and scaling['source'] == 'native monitor', actual
            assert scaling['interfaceScale'] == expected, (name, expected, actual)
            assert math.isclose(scaling['monitor']['devicePixelRatio'], ratio, abs_tol=.02), actual
            assert math.isclose(actual['browserDpr'], ratio, abs_tol=.02), actual
            assert math.isclose(actual['rootFont'], 14*font/100, abs_tol=.02), actual
            assert math.isclose(actual['zoom'], expected/100, abs_tol=.001), actual
            assert math.isclose(scaling['textScale'], expected*font/100, abs_tol=.02), actual
            moving = not args.accessibility
            assert decoration['particles'] == (18 if moving else 0), actual
            if moving:
                assert decoration['artwork'] == 'local-svg-sprite', actual
                assert decoration['glyphs'] == 18, actual
            assert decoration['season'] == (None if season == 'default' else season), actual
            assert decoration['scenery'] == (0 if season == 'default' else 2), actual
            assert decoration['reduced'] == name.startswith('reduced-'), actual
            assert actual['highContrast'] == name.startswith('contrast-'), actual
            assert actual['effectPointerEvents'] == 'none', actual
            for navigation in actual['navState']:
                if not navigation['disabled']:
                    assert contrast_ratio(navigation['color'], actual['navBackground']) >= 4.5, (name, navigation, actual['navBackground'])
            assert not decoration['paused'] and not actual['overflow'] and actual['brandFits'], actual
            assert actual['nativeRendererRestarts'] == 0 and not actual['recoveryVisible'], actual
            assert image.stat().st_size > 10000, image
            results.append({'case': name, 'monitor': [width, height], 'systemScale': ratio,
                            'window': window, 'fontPreference': font, 'theme': season,
                            'mode': mode, 'actual': actual,'nativeTerminations':[line for line in completed.stderr.splitlines() if line.startswith('Interface renderer termination:')]})
            (evidence/f'{platform}-results.json').write_text(json.dumps({'platform': platform, 'sandboxEnabled': True, 'cases': results}, indent=2)+'\n', encoding='utf-8')
            print(f'Native {platform} interface passed: {name}, Auto {expected}%, text {font}%, {season}/{mode}.', flush=True)
    report = {'platform': platform, 'sandboxEnabled': True,
              'executableSha256': hashlib.sha256((build/'DriftersToolkit').read_bytes()).hexdigest(),
              'privateFixtureHelperSha256': None,
              'saveCodecSha256': codec_hash,
              'cases': results}
    (evidence/f'{platform}-results.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Native interface audit passed {len(results)} scenarios with the local color font loaded.')


if __name__ == '__main__':
    main()
