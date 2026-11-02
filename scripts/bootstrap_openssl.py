"""Build the pinned OpenSSL 3 ABI runtime in a new private Linux prefix.

Developer tool only. Uses free public source and changes no system packages.
The source archive must already be downloaded from the official URL below.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tarfile

SOURCE = {'name': 'openssl-3.5.9.tar.gz',
          'url': 'https://www.openssl.org/source/openssl-3.5.9.tar.gz',
          'sha256': '603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a',
          'size': 53279637}
VERSION = '3.5.9'
BUILD_OPTIONS = ['linux-x86_64', 'shared', '--openssldir=/etc/ssl', '--libdir=lib', '-march=x86-64-v2']
RUNTIME_NAMES = ('libssl.so.3', 'libcrypto.so.3')


def plain_parents(path):
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.exists() and not parent.is_dir():
            raise RuntimeError('A build folder contains a redirected path.')


def identity(path, maximum=128 * 1024 * 1024):
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOCTTY', 0)
    with os.fdopen(os.open(path, flags), 'rb') as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or not 0 <= before.st_size <= maximum:
            raise RuntimeError('An OpenSSL input is not a bounded ordinary file.')
        digest = hashlib.sha256()
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
        after = os.fstat(source.fileno())
        if any(getattr(before, key) != getattr(after, key) for key in
               ('st_dev', 'st_ino', 'st_mode', 'st_nlink', 'st_size', 'st_mtime_ns', 'st_ctime_ns')):
            raise RuntimeError('An OpenSSL input changed during verification.')
        return {'sha256': digest.hexdigest(), 'size': before.st_size}


def verified_source(archive):
    if identity(archive) != {key: SOURCE[key] for key in ('sha256', 'size')}:
        raise RuntimeError('OpenSSL source differs from the pinned official release.')


def read_proof(path):
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOCTTY', 0)
    with os.fdopen(os.open(path, flags), 'rb') as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or before.st_size > 65536:
            raise RuntimeError('Invalid OpenSSL build provenance.')
        content = source.read(65537)
        after = os.fstat(source.fileno())
        if len(content) != before.st_size or any(getattr(before, key) != getattr(after, key) for key in
               ('st_dev', 'st_ino', 'st_mode', 'st_nlink', 'st_size', 'st_mtime_ns', 'st_ctime_ns')):
            raise RuntimeError('OpenSSL build provenance changed during verification.')
        return json.loads(content)


def runtime_inputs(prefix, provenance, archive):
    prefix = Path(prefix).absolute()
    plain_parents(prefix)
    verified_source(archive)
    proof = read_proof(provenance)
    if (proof.get('schemaVersion') != 1 or proof.get('version') != VERSION
            or proof.get('origin') != 'official-openssl-source' or proof.get('source') != SOURCE
            or proof.get('buildOptions') != BUILD_OPTIONS or proof.get('minimumCpu') != 'x86-64-v2'
            or proof.get('minimumGlibc') != '2.35' or set(proof.get('runtime', {})) != set(RUNTIME_NAMES)):
        raise RuntimeError('Unsupported OpenSSL build provenance.')
    for name in RUNTIME_NAMES:
        path = prefix / 'lib' / name
        if path.is_symlink() or identity(path) != proof['runtime'][name]:
            raise RuntimeError('An OpenSSL runtime differs from its verified build: ' + name)
    if identity(prefix / 'OPENSSL-LICENSE.txt') != proof.get('license'):
        raise RuntimeError('The OpenSSL license differs from its verified source build.')
    return proof


def unpack(archive, destination):
    # The digest is checked before opening the upstream tar. Still refuse links,
    # special entries, duplicate names and paths outside this fresh source tree.
    seen = set()
    total = 0
    with tarfile.open(archive, 'r:gz') as source:
        for member in source:
            name = member.name.rstrip('/')
            path = PurePosixPath(name)
            if (not name or '\\' in name or '\x00' in name or path.is_absolute()
                    or any(part in ('', '.', '..') for part in name.split('/'))
                    or path.parts[0] != 'openssl-' + VERSION or name in seen or len(seen) >= 12000
                    or not (member.isdir() or member.isfile())):
                raise RuntimeError('Unsupported OpenSSL source archive entry.')
            seen.add(name)
            target = destination.joinpath(*path.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            total += member.size
            if member.size < 0 or member.size > 64 * 1024 * 1024 or total > 1024 * 1024 * 1024:
                raise RuntimeError('OpenSSL source archive exceeds its extraction bounds.')
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.extractfile(member) as data, target.open('xb') as output:
                shutil.copyfileobj(data, output, 1024 * 1024)
            target.chmod(0o755 if member.mode & 0o111 else 0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if sys.platform != 'linux' or not 1 <= args.jobs <= 32:
        raise RuntimeError('Use Linux and a job count from 1 to 32.')
    archive, prefix, work = args.archive.absolute(), args.prefix.absolute(), args.work.absolute()
    plain_parents(prefix.parent)
    plain_parents(work.parent)
    if prefix.exists() or prefix.is_symlink() or work.exists() or work.is_symlink() or prefix == work:
        raise RuntimeError('Build and prefix folders must be new; existing files were preserved.')
    verified_source(archive)
    work.mkdir(mode=0o700)
    unpack(archive, work)
    build = work / 'build'
    build.mkdir()
    commands = [[str(work / ('openssl-' + VERSION) / 'Configure'), *BUILD_OPTIONS, '--prefix=' + str(prefix)],
                ['make', '-j' + str(args.jobs)], ['make', 'test', 'TESTS=test_evp test_x509 test_verify test_sslapi test_ssl_new test_ec test_ecdsa test_sha'],
                ['make', 'install_sw']]
    for index, command in enumerate(commands):
        with (work / ('build-step-' + str(index) + '.log')).open('x') as log:
            subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    runtime = {}
    for name in RUNTIME_NAMES:
        path = prefix / 'lib' / name
        runtime[name] = identity(path)
        symbols = subprocess.check_output(['readelf', '--version-info', str(path)], text=True)
        required = [tuple(map(int, value.split('.'))) for value in re.findall(r'GLIBC_(\d+\.\d+(?:\.\d+)?)', symbols)]
        if required and max(required) > (2, 35):
            raise RuntimeError('OpenSSL exceeds the supported glibc 2.35 floor.')
    license_path = prefix / 'OPENSSL-LICENSE.txt'
    shutil.copyfile(work / ('openssl-' + VERSION) / 'LICENSE.txt', license_path)
    proof = {'schemaVersion': 1, 'version': VERSION, 'origin': 'official-openssl-source', 'source': SOURCE,
             'buildOptions': BUILD_OPTIONS, 'minimumCpu': 'x86-64-v2', 'minimumGlibc': '2.35', 'runtime': runtime}
    proof['license'] = identity(license_path)
    (prefix / 'OPENSSL-PROVENANCE.json').write_text(json.dumps(proof, indent=2) + '\n')
    runtime_inputs(prefix, prefix / 'OPENSSL-PROVENANCE.json', archive)
    print('Verified OpenSSL', VERSION, 'runtime ready:', prefix)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, tarfile.TarError) as issue:
        print('OpenSSL build failed:', str(issue), file=sys.stderr)
        raise SystemExit(1)
