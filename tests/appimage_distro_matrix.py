"""Run the ONE universal Linux AppImage, unchanged, on several distributions.

Run on a Docker host (Windows or Linux) with the public Linux ZIP. Every
distribution gets a throwaway container holding only what a desktop already has
(an X server, Mesa, fonts, D-Bus); nothing is built per distribution. As an
ordinary user the container checks the launcher's glibc gate, the headless CLI,
the GUI's own runtime verification, the sandbox check and a real start of the
browser view under Xvfb with Chromium's sandbox enabled, then saves a
screenshot. Containers are always removed; images only with --remove-images.

The containers share Docker's kernel, so a host's AppArmor policy, GPU drivers
and desktop integration are NOT reproduced. Docker's seccomp filter is relaxed
(as for the earlier render matrix) only so Chromium can build its own sandbox;
the test user has no capabilities and no-new-privileges is set. This is
userland evidence (glibc, libstdc++, host libraries, fonts, X11), not a
replacement for real desktops.
"""
from pathlib import Path
import argparse
import json
import subprocess
import sys
import tempfile
import zipfile

# name: (image, package family, expected outcome)
DISTROS = {
    'ubuntu-22.04': ('ubuntu:22.04', 'apt', 'run'),
    'ubuntu-24.04': ('ubuntu:24.04', 'apt', 'run'),
    'debian-12': ('debian:12', 'apt', 'run'),
    'debian-13': ('debian:13', 'apt', 'run'),
    'fedora-42': ('fedora:42', 'dnf', 'run'),
    'arch': ('archlinux:latest', 'pacman', 'run'),
    'opensuse-tumbleweed': ('opensuse/tumbleweed:latest', 'zypper', 'run'),
    'rocky-9': ('rockylinux:9', 'dnf', 'too-old'),
}
# Desktop basics only: display server, OpenGL/EGL/GBM (glvnd + Mesa), libexpat,
# fonts, D-Bus, plus screenshot/window tools for the test itself.
INSTALL = {
    'apt': 'export DEBIAN_FRONTEND=noninteractive; o="-o Acquire::Retries=5 -o Acquire::http::Timeout=20"; '
           'apt-get $o update -qq && apt-get $o install -y -qq --no-install-recommends '
           'xvfb xauth dbus dbus-x11 libgl1 libegl1 libopengl0 libgbm1 libexpat1 libgl1-mesa-dri fonts-dejavu-core '
           'fontconfig imagemagick x11-utils procps ca-certificates >/dev/null',
    'dnf': 'dnf -y -q --setopt=retries=5 --setopt=timeout=20 install xorg-x11-server-Xvfb dbus-daemon dbus-tools mesa-libGL mesa-libEGL mesa-libgbm '
           'mesa-dri-drivers libglvnd-opengl expat dejavu-sans-fonts fontconfig ImageMagick xwininfo procps-ng '
           'shadow-utils >/dev/null',
    'pacman': 'pacman -Syu --noconfirm --needed -q xorg-server-xvfb mesa libglvnd expat ttf-dejavu fontconfig '
              'imagemagick xorg-xwininfo dbus procps-ng >/dev/null',
    'zypper': 'zypper -n -q install --no-recommends xorg-x11-server-Xvfb Mesa-libGL1 Mesa-libEGL1 libgbm1 '
              'Mesa-dri libglvnd libexpat1 dejavu-fonts fontconfig ImageMagick xwininfo dbus-1-daemon procps shadow '
              '>/dev/null',
}
USER = 'useradd -m -u 4242 tester 2>/dev/null || adduser -D -u 4242 tester'


def install_command(family):
    # Mirrors occasionally stall; retry the whole install a few times.
    return 'for attempt in 1 2 3; do (' + INSTALL[family] + ') && exit 0; sleep 10; done; exit 1'
RUN_TEST = r'''
set -u
cd "$HOME" && rm -rf toolkit && cp -r /dist toolkit && cd toolkit
chmod +x Launch-DriftersToolkit.sh Drifters-Toolkit-Linux-x86_64.AppImage
echo "RESULT glibc=$(getconf GNU_LIBC_VERSION 2>/dev/null | tr ' ' '-')"
./Launch-DriftersToolkit.sh --cli --help >/tmp/cli.txt 2>&1; echo "RESULT cli=$?"
./Launch-DriftersToolkit.sh --verify-only >/tmp/verify.txt 2>&1; echo "RESULT verify=$?"
./Launch-DriftersToolkit.sh --check-sandbox >/tmp/check.txt 2>&1; echo "RESULT check_sandbox=$?"
Xvfb :99 -screen 0 1440x960x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
sleep 2
export DISPLAY=:99
setsid dbus-run-session -- ./Launch-DriftersToolkit.sh --sandbox >/tmp/gui.out 2>/tmp/gui.err &
gui=$!
sleep 45
if kill -0 "$gui" 2>/dev/null; then echo "RESULT gui=alive"; else wait "$gui"; echo "RESULT gui=exited-$?"; fi
echo "RESULT windows=$(xwininfo -root -tree 2>/dev/null | grep -c 'Drifter')"
import -window root "/out/$NAME.png" 2>/dev/null; echo "RESULT screenshot=$?"
kill -TERM -- "-$gui" 2>/dev/null; sleep 2; kill -KILL -- "-$gui" 2>/dev/null
echo "== cli"; head -3 /tmp/cli.txt
echo "== verify"; cat /tmp/verify.txt
echo "== check-sandbox"; head -3 /tmp/check.txt
echo "== gui stderr (tail)"; tail -15 /tmp/gui.err
'''
TOO_OLD_TEST = r'''
cd "$HOME" && rm -rf toolkit && cp -r /dist toolkit && cd toolkit
chmod +x Launch-DriftersToolkit.sh Drifters-Toolkit-Linux-x86_64.AppImage
echo "RESULT glibc=$(getconf GNU_LIBC_VERSION 2>/dev/null | tr ' ' '-')"
./Launch-DriftersToolkit.sh >/tmp/out.txt 2>&1; echo "RESULT launcher=$?"
grep -q 'needs glibc 2.35 or newer' /tmp/out.txt && echo "RESULT friendly=yes" || echo "RESULT friendly=no"
cat /tmp/out.txt
'''


def docker(*arguments, check=True, timeout=1800):
    return subprocess.run(['docker', *arguments], check=check, text=True, capture_output=True, timeout=timeout,
                          encoding='utf-8', errors='replace')


def extract(archive, destination):
    with zipfile.ZipFile(archive) as bundle:
        for info in bundle.infolist():
            if not info.filename.startswith('DriftersToolkit-Linux/') or '..' in Path(info.filename).parts:
                raise RuntimeError('Unexpected entry in the Linux ZIP: ' + info.filename)
        bundle.extractall(destination)
    return Path(destination) / 'DriftersToolkit-Linux'


def check_distro(name, image, family, expect, bundle, out):
    container = 'drifters-matrix-' + name
    docker('rm', '-f', container, check=False)
    docker('run', '-d', '--name', container, '--security-opt', 'seccomp=unconfined', '--security-opt',
           'no-new-privileges', '--shm-size=512m', '-v', f'{bundle}:/dist:ro', '-v', f'{out}:/out', image,
           'sleep', 'infinity', timeout=900)
    try:
        if expect == 'run':
            docker('exec', container, 'sh', '-c', install_command(family), timeout=1500)
        else:
            docker('exec', container, 'sh', '-c', 'command -v useradd >/dev/null || dnf -y -q install shadow-utils')
        docker('exec', container, 'sh', '-c', USER)
        script = RUN_TEST if expect == 'run' else TOO_OLD_TEST
        result = docker('exec', '-u', '4242', '-e', 'HOME=/home/tester', '-e', 'NAME=' + name, container, 'bash',
                        '-c', script, check=False, timeout=600)
        values = dict(line[7:].split('=', 1) for line in result.stdout.splitlines()
                      if line.startswith('RESULT ') and '=' in line)
        digest = docker('image', 'inspect', '--format', '{{index .RepoDigests 0}}', image, check=False).stdout.strip()
        if expect == 'run':
            passed = (values.get('cli') == '0' and values.get('verify') == '0' and values.get('check_sandbox') == '0'
                      and values.get('gui') == 'alive' and values.get('windows', '0') != '0')
        else:
            passed = values.get('launcher') == '1' and values.get('friendly') == 'yes'
        return {'distribution': name, 'image': digest or image, 'expected': expect, 'passed': passed,
                'results': values, 'log': result.stdout[-6000:] + result.stderr[-2000:]}
    finally:
        docker('rm', '-f', container, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('zip', type=Path, help='The public Drifters-Toolkit-Linux.zip')
    parser.add_argument('output', type=Path, help='Folder for screenshots and the JSON report')
    parser.add_argument('--only', action='append', choices=sorted(DISTROS))
    parser.add_argument('--remove-images', action='store_true', help='Remove the distribution images afterwards')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = []
    with tempfile.TemporaryDirectory(prefix='drifter-matrix-') as temporary:
        bundle = extract(args.zip, temporary)
        for name in args.only or DISTROS:
            image, family, expect = DISTROS[name]
            print(f'== {name} ({image})', flush=True)
            try:
                row = check_distro(name, image, family, expect, bundle, args.output.resolve())
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as issue:
                detail = issue.stderr if isinstance(issue.stderr, str) else ''
                row = {'distribution': name, 'image': image, 'expected': expect, 'passed': False,
                       'error': (str(issue) + ' ' + detail)[-3000:]}
                docker('rm', '-f', 'drifters-matrix-' + name, check=False)
            report.append(row)
            print(('PASS ' if row['passed'] else 'FAIL ') + json.dumps(row.get('results', row.get('error'))), flush=True)
            if args.remove_images:
                docker('image', 'rm', image, check=False)
    (args.output / 'matrix-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    failed = [row['distribution'] for row in report if not row['passed']]
    print('All distributions passed.' if not failed else 'Failed: ' + ', '.join(failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
