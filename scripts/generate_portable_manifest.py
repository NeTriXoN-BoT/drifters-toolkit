"""Compile a bounded staged-runtime inventory into the portable Linux executable."""
from pathlib import Path, PurePosixPath
import argparse
import json
import re
from release_layout import canonical_name, read_plain, write_artifact

def render(data):
    if (not isinstance(data, dict) or type(data.get('schemaVersion')) is not int
            or data.get('schemaVersion') != 1 or not isinstance(data.get('files'), list)
            or not 4 <= len(data['files']) <= 4096):
        raise RuntimeError('Invalid portable runtime inventory.')
    rows, seen, total = [], set(), 0
    required = {'usr/bin/qt.conf','usr/libexec/qt.conf','usr/libexec/QtWebEngineProcess'}
    cli_ready = data.get('cliReady', False)
    if type(cli_ready) is not bool: raise RuntimeError('Invalid command-line runtime membership.')
    cli_seen = set()
    cli_required = {'usr/bin/qt.conf','usr/lib/libQt6Core.so.6','usr/lib/libQt6Network.so.6',
                    'usr/lib/libcrypto.so.3','usr/lib/libssl.so.3',
                    'usr/plugins/tls/libqopensslbackend.so'}
    for row in data['files']:
        if not isinstance(row, dict): raise RuntimeError('Invalid portable runtime entry.')
        name = row.get('path','')
        canonical_name(name)
        parts = PurePosixPath(name).parts
        if (not name or name.startswith('/') or '\\' in name or ':' in name or '..' in parts or '.' in parts
                or name in seen or not name.startswith(('usr/lib/','usr/plugins/','usr/libexec/','usr/resources/','usr/translations/')) and name != 'usr/bin/qt.conf'):
            raise RuntimeError('Unexpected portable runtime path: '+str(name))
        size, digest, executable = row.get('size'), row.get('sha256',''), row.get('executable')
        cli = row.get('cli',False)
        if type(size) is not int or not 0 <= size <= 512*1024*1024 or not re.fullmatch('[a-f0-9]{64}',digest) or type(executable) is not bool or type(cli) is not bool:
            raise RuntimeError('Invalid portable runtime file identity: '+name)
        if cli:
            if not cli_ready or not (name == 'usr/bin/qt.conf' or name.startswith(('usr/lib/','usr/plugins/tls/'))):
                raise RuntimeError('Unexpected command-line runtime path: '+name)
            cli_seen.add(name)
        total += size
        if total > 2*1024*1024*1024: raise RuntimeError('Portable runtime exceeds its bounded size.')
        seen.add(name)
        rows.append('{'+json.dumps(name)+','+json.dumps(digest)+','+str(size)+'ULL,'+str(executable).lower()+','+str(cli).lower()+'}')
    if not required <= seen: raise RuntimeError('Qt configuration/helper is missing from the inventory.')
    if cli_ready and not cli_required <= cli_seen: raise RuntimeError('The command-line dependency subset is incomplete.')
    text = '#pragma once\n#include <cstdint>\nnamespace drifter::portable_manifest {\nstruct Entry { const char* path; const char* sha256; std::uint64_t size; bool executable; bool cli; };\ninline constexpr bool cliReady = '+str(cli_ready).lower()+';\ninline constexpr Entry files[] = {\n'+',\n'.join(rows)+'\n};\n}\n'
    return text

def generate(source, destination):
    data = json.loads(read_plain(source, 2*1024*1024).decode('utf-8'))
    text = render(data)
    target = Path(destination); target.parent.mkdir(parents=True,exist_ok=True)
    if target.is_symlink(): raise RuntimeError('The generated header target is redirected.')
    if not target.exists() or read_plain(target,2*1024*1024).decode('utf-8') != text: write_artifact(target,text.encode('utf-8'))

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--input',required=True);parser.add_argument('--output',required=True)
    args=parser.parse_args();generate(args.input,args.output)
