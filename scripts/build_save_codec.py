"""Build the separate save-container filter from a complete offline source tree.

This script never downloads dependencies, reads game saves or changes a toolkit
installation. Supply the Rust 1.90.0 toolchain and vendored source directory.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--cargo', type=Path, required=True)
    parser.add_argument('--rustc', type=Path, required=True)
    parser.add_argument('--target', choices=['x86_64-pc-windows-gnu', 'x86_64-unknown-linux-musl'], required=True)
    parser.add_argument('--target-dir', type=Path, required=True)
    parser.add_argument('--cargo-home', type=Path, required=True)
    parser.add_argument('--linker', type=Path)
    parser.add_argument('--dlltool', type=Path)
    parser.add_argument('--test', action='store_true')
    parser.add_argument('--proof', type=Path)
    args = parser.parse_args()
    source, cargo, rustc = (item.resolve(strict=True) for item in (args.source, args.cargo, args.rustc))
    for name in ['Cargo.toml', 'Cargo.lock', 'src/main.rs', '.cargo/config.toml']:
        if not (source / name).is_file() or (source / name).is_symlink():
            raise RuntimeError('The complete source tree is missing a required ordinary file.')
    if not (source / 'vendor').is_dir() or (source / 'vendor').is_symlink():
        raise RuntimeError('The offline vendored dependencies are required.')
    for tool in (cargo, rustc):
        if tool.is_symlink() or not tool.is_file():
            raise RuntimeError('The toolchain must use ordinary executable files.')
    home, target_dir = args.cargo_home.absolute(), args.target_dir.absolute()
    home.mkdir(parents=True, exist_ok=True)
    target_dir.mkdir(parents=True, exist_ok=True)
    if home.is_symlink() or target_dir.is_symlink():
        raise RuntimeError('Build/cache folders cannot be links.')
    env = {key: value for key, value in os.environ.items()
           if key in {'SystemRoot', 'SYSTEMROOT', 'WINDIR', 'TEMP', 'TMP', 'PATH', 'PATHEXT', 'HOME', 'LANG', 'LC_ALL'}}
    env.update({'CARGO_HOME': str(home), 'CARGO_NET_OFFLINE': 'true', 'RUSTC': str(rustc), 'CARGO_INCREMENTAL': '0'})
    version = subprocess.check_output([str(rustc), '--version'], env=env, text=True).strip()
    if not version.startswith('rustc 1.90.0 '):
        raise RuntimeError('This source build is pinned to Rust 1.90.0.')
    sysroot = Path(subprocess.check_output([str(rustc), '--print', 'sysroot'], env=env, text=True).strip())
    flags = ['-C', 'target-feature=+crt-static']
    if args.dlltool:
        flags += ['-C', 'dlltool=' + str(args.dlltool.resolve(strict=True))]
    for original, label in [(source, '/save-codec'), (source / 'vendor', '/vendor'), (home, '/cargo-home'), (sysroot, '/rust-toolchain')]:
        for path in {str(original), original.as_posix()}:
            flags += ['--remap-path-prefix', path + '=' + label]
    env['CARGO_ENCODED_RUSTFLAGS'] = '\x1f'.join(flags)
    if args.linker:
        env['CARGO_TARGET_' + args.target.upper().replace('-', '_') + '_LINKER'] = str(args.linker.resolve(strict=True))
    # Cargo is told explicitly not to resolve or fetch anything, even with an
    # empty Cargo home. The lock and vendor checksums remain authoritative.
    command = [str(cargo), 'test' if args.test else 'build', '--release', '--frozen', '--offline',
               '--target', args.target, '--target-dir', str(target_dir), '--jobs', '2']
    subprocess.run(command, cwd=source, env=env, check=True)
    if args.test:
        return
    binary = target_dir / args.target / 'release' / ('DriftersToolkitSaveCodec.exe' if os.name == 'nt' else 'DriftersToolkitSaveCodec')
    data = binary.read_bytes()
    # A public binary must not retain any operator, checkout or cache path.
    for path in {str(source), source.as_posix(), str(home), home.as_posix(), str(sysroot), sysroot.as_posix()}:
        if path.encode() in data or path.encode('utf-16le') in data:
            raise RuntimeError('A private build path survived compiler path remapping.')
    proof = {'rustc': version, 'target': args.target, 'offline': True, 'frozen': True,
             'binary': str(binary), 'sha256': sha(binary), 'bytes': len(data),
             'source': {name: sha(source / name) for name in ['Cargo.toml', 'Cargo.lock', 'src/main.rs']}}
    if args.proof:
        args.proof.parent.mkdir(parents=True, exist_ok=True)
        args.proof.write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(proof, indent=2))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as issue:
        print(str(issue), file=sys.stderr)
        sys.exit(1)
