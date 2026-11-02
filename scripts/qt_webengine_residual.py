"""Append the reviewed residual security repairs to the retained Qt cache.

The preceding three source/runtime proofs remain immutable. No downloads,
second SDK, extraction, system installation or sandbox changes are performed.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
import bootstrap_qt_webengine_security as base
import resume_qt_webengine_security as resume
import qt_webengine_incremental as previous
from linux_build_storage import check_build_storage, project_build_lock, RuntimeBudget

ROOT = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name('qt_webengine_residual_lock.json')
PROOF = 'QTWEBENGINE-SECURITY-RESIDUAL-PROVENANCE.json'
VENDOR_HEAD = '43bcb1ece119814c1be448629ba0bd751b51ec7d'
MAX_PATCH_BYTES = 8 * 1024 * 1024
SERIES_DIGEST = 'a88c83413232f73135386c3a140e93ab2b9ddce2e931ee001e8ba7dad38c3931'
SOURCE_COUNT = 176
OBSERVED_PROOF_MEANING = ('Observed predecessor on this builder, not a universal reproducible binary requirement. '
                          'A new build must capture its exact validated predecessor proof and outputs before mutation.')
record = base.file_record
publish = previous.publish
plain_target = previous.plain_target


def valid_record(value, maximum=MAX_PATCH_BYTES):
    return (isinstance(value, dict) and set(value) == {'sha256', 'size'}
            and isinstance(value.get('sha256'), str)
            and re.fullmatch('[a-f0-9]{64}', value['sha256'])
            and type(value.get('size')) is int and 0 < value['size'] <= maximum)


def source_name(name):
    if not isinstance(name, str):
        raise RuntimeError('A residual source name is not text.')
    path = PurePosixPath(name)
    if (path.is_absolute() or '..' in path.parts or str(path) != name
            or not name.startswith('chromium/') or re.search(r'[\\\s:]', name)):
        raise RuntimeError('A residual source path is unsafe or ambiguous.')
    return name


def all_patch_inputs(data):
    return [*data['upstream'], data['adapter'], data['regression']]


def load_lock(path=LOCK):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    upstream = data.get('upstream', [])
    if (data.get('schemaVersion') != 1 or data.get('residualRevision') != 1
            or data.get('qtVersion') != '6.11.3'
            or data.get('baseChromiumVersion') != '140.0.7339.264'
            or data.get('vendorReferenceHead') != VENDOR_HEAD
            or data.get('baselineIncrementalLock') != record(previous.LOCK)
            or not valid_record(data.get('baselineRuntimeProof'))
            or data.get('baselineRuntimeProofMeaning') != OBSERVED_PROOF_MEANING
            or data.get('baselineSourceCheckpoint') != previous.source_files(previous.verify_inputs(), 'after')
            or data.get('patchDirectory') != 'vendor/qtwebengine-security/2026-10-30-residual'
            or len(upstream) != 21
            or hashlib.sha256('\n'.join(row.get('commit', '') for row in upstream).encode()).hexdigest() != SERIES_DIGEST
            or len(data.get('files', [])) != SOURCE_COUNT
            or data.get('adapter', {}).get('name') != 'qt140-residual-compatibility.patch'
            or data.get('regression', {}).get('name') != 'qt140-residual-regressions.patch'):
        raise RuntimeError('The residual WebEngine security lock is invalid.')
    names = set()
    for row in data['files']:
        name = source_name(row.get('path'))
        if (set(row) != {'path', 'before', 'after'} or name in names
                or not valid_record(row.get('after'))
                or row['before'] is not None and not valid_record(row['before'])):
            raise RuntimeError('A residual source identity is invalid or duplicated.')
        names.add(name)
    patch_names = set()
    for row in all_patch_inputs(data):
        if (not isinstance(row.get('name'), str)
                or not re.fullmatch(r'[A-Za-z0-9-]+\.patch', row['name'])
                or row['name'] in patch_names
                or not valid_record({key: row.get(key) for key in ('sha256', 'size')})):
            raise RuntimeError('A residual patch identity is unsafe or invalid.')
        patch_names.add(row['name'])
    if sum(row['size'] for row in all_patch_inputs(data)) > 16 * 1024 * 1024:
        raise RuntimeError('The residual export series exceeds its bounded input budget.')
    for row in upstream:
        commit = row.get('commit', '')
        allowed = ('https://github.com/qt/qtwebengine-chromium/commit/' + commit + '.patch',
                   'https://github.com/chromium/chromium/commit/' + commit + '.patch',
                   'https://github.com/v8/v8/commit/' + commit + '.patch',
                   'https://github.com/google/skia/commit/' + commit + '.patch',
                   'https://chromium.googlesource.com/chromium/src/+/' + commit + '^!/?format=TEXT',
                   'https://chromium.googlesource.com/v8/v8/+/' + commit + '^!/?format=TEXT',
                   'https://skia.googlesource.com/skia/+/' + commit + '^!/?format=TEXT')
        if not re.fullmatch('[a-f0-9]{40}', commit) or row.get('url') not in allowed:
            raise RuntimeError('A residual primary reference differs.')
    source_files(data, 'before')
    return data


def verify_inputs(root=ROOT, lock=LOCK):
    data = load_lock(lock)
    for row in all_patch_inputs(data):
        if record(Path(root) / data['patchDirectory'] / row['name']) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('A pinned residual export changed: ' + row['name'])
    paths = []
    for field in ('adapter', 'regression'):
        body = (Path(root) / data['patchDirectory'] / data[field]['name']).read_text(encoding='utf-8')
        diffs = re.findall(r'^diff --git a/([^ ]+) b/([^\r\n]+)$', body, re.M)
        if not diffs or any(a != b for a, b in diffs):
            raise RuntimeError('A residual adapter contains a rename or ambiguous source.')
        paths.extend('chromium/' + a for a, _ in diffs)
    if len(paths) != len(set(paths)) or set(paths) != {row['path'] for row in data['files']}:
        raise RuntimeError('The residual adapters leave their exact reviewed source scope.')
    return data


def source_files(data, phase):
    if phase not in ('before', 'after'):
        raise ValueError('Unsupported source phase.')
    original = previous.source_files(previous.verify_inputs(), 'after')
    result = dict(original)
    for row in data['files']:
        old = row['before']
        if row['path'] in original and (old is None or original[row['path']] != old['sha256']):
            raise RuntimeError('A residual preimage disagrees with the immutable predecessor inventory.')
        value = row[phase]
        if value is None:
            result.pop(row['path'], None)
        else:
            result[row['path']] = value['sha256']
    return result


def verify_source(source, data, phase):
    source = plain_target(source, directory=True, required=True)
    files = source_files(data, phase)
    for name in files:
        plain_target(source / 'src/3rdparty' / name, required=True)
    resume.verify_source(source, files)
    for row in data['files']:
        path = source / 'src/3rdparty' / row['path']
        plain_target(path)
        value = row[phase]
        if value is None:
            if path.exists():
                raise RuntimeError('A new residual source already exists: ' + row['path'])
        elif record(path) != value:
            raise RuntimeError('A residual source size or digest differs: ' + row['path'])


def identity(data):
    return {'schemaVersion': 1, 'residualRevision': data['residualRevision'],
            'lock': record(LOCK), 'recipe': record(__file__),
            'baselineIncrementalIdentity': previous.identity(previous.verify_inputs()),
            'upstream': [{key: row[key] for key in ('commit', 'sha256', 'size')} for row in data['upstream']],
            'adapter': {key: data['adapter'][key] for key in ('name', 'sha256', 'size')},
            'regression': {key: data['regression'][key] for key in ('name', 'sha256', 'size')},
            'effectiveSourceFiles': len(source_files(data, 'after'))}


def verify_public(proof):
    if not isinstance(proof, dict) or any(proof.get(k) != v for k, v in identity(verify_inputs()).items()):
        raise RuntimeError('The residual WebEngine security proof is missing or stale.')
    for field in ('outputs', 'priorOutputs'):
        rows = proof.get(field)
        if (not isinstance(rows, list) or not 1 <= len(rows) <= 1024
                or any(not isinstance(row, dict) or set(row) != {'file', 'sha256', 'size'}
                       or not valid_record({key: row.get(key) for key in ('sha256', 'size')}, 512 * 1024 * 1024)
                       for row in rows)):
            raise RuntimeError('A residual runtime output identity is invalid.')
        base.verify_output_scope(rows)
    core = 'lib/libQt6WebEngineCore.so.6.11.3'
    if next(row for row in proof['outputs'] if row['file'] == core) == next(row for row in proof['priorOutputs'] if row['file'] == core):
        raise RuntimeError('The residual repairs did not replace the preceding engine core.')
    if not valid_record(proof.get('baselineProof')):
        raise RuntimeError('The immutable incremental proof identity is missing.')
    return proof


def historical_proofs(prefix):
    old = previous.historical_proofs(prefix)
    plain_target(prefix / previous.PROOF, required=True)
    incremental = previous.verify_public(json.loads((prefix / previous.PROOF).read_text(encoding='utf-8')))
    if (incremental['baselineProof'] != record(prefix / previous.previous.PROOF)
            or incremental['priorOutputs'] != old['followup']['outputs']):
        raise RuntimeError('The immutable incremental runtime provenance changed.')
    return {**old, 'incremental': incremental}


def verify_build(prefix):
    prefix = plain_target(prefix, directory=True, required=True)
    plain_target(prefix / PROOF, required=True)
    old = historical_proofs(prefix)
    proof = verify_public(json.loads((prefix / PROOF).read_text(encoding='utf-8')))
    if (proof['baselineProof'] != record(prefix / previous.PROOF)
            or proof['priorOutputs'] != old['incremental']['outputs']):
        raise RuntimeError('The immutable preceding runtime provenance changed.')
    for row in proof['outputs']:
        plain_target(prefix / row['file'], required=True)
        if record(prefix / row['file']) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('A residual runtime output differs: ' + row['file'])
    return {**old, 'residual': proof}


def public_provenance(proof):
    result = previous.public_provenance(proof)
    result['outputs'] = proof['residual']['outputs']
    result['residualDelta'] = {key: proof['residual'][key]
                              for key in (*identity(verify_inputs()), 'baselineProof', 'priorOutputs', 'outputs', 'abiFloor')}
    return result


def verify_cache_paths(work, prefix):
    previous.verify_cache_paths(work, prefix)
    for path in (work / 'residual-security-start.json', work / 'residual-security-budget.json',
                 work / 'residual-security-build.log', work / 'residual-security-process.json',
                 work / 'residual-security-install.log', prefix / PROOF):
        plain_target(path)


def verify_install_paths(work, prefix):
    """Check every generated install destination, including development files.

    The three retained CMake scripts also install headers and module metadata.
    Protect those writes, without rejecting the four legitimate SONAME aliases
    or inspecting unrelated directories elsewhere on the host.
    """
    visited = 0
    def tree(path):
        nonlocal visited
        plain_target(path, directory=True)
        if not path.exists():
            return
        pending = [path]
        while pending:
            directory = pending.pop()
            for child in directory.iterdir():
                visited += 1
                if visited > 10000:
                    raise RuntimeError('The residual install destination inventory exceeds its bound.')
                if child.is_symlink():
                    raise RuntimeError('A residual install destination is redirected.')
                if child.is_dir():
                    plain_target(child, directory=True, required=True)
                    pending.append(child)
                else:
                    plain_target(child, required=True)

    aliases = {}
    for module in ('Core', 'Widgets'):
        family = 'libQt6WebEngine' + module + '.so'
        aliases['lib/' + family] = {family + '.6', family + '.6.11.3'}
        aliases['lib/' + family + '.6'] = {family + '.6.11.3'}
    def soname(leaf):
        seen = set()
        while leaf.is_symlink():
            key = leaf.relative_to(prefix).as_posix()
            link = os.readlink(leaf)
            if key in seen or key not in aliases or link not in aliases[key]:
                raise RuntimeError('A residual SONAME install destination is redirected.')
            seen.add(key)
            leaf = leaf.parent / link
        plain_target(leaf)
    for script in base.RUNTIME_INSTALL_SCRIPTS:
        path = plain_target(work / 'build' / script, required=True)
        if path.stat().st_size > 256 * 1024:
            raise RuntimeError('A residual install script exceeds its reviewed bound.')
        body = path.read_text(encoding='utf-8')
        entries = re.findall(r'\bfile\s*\(\s*INSTALL\s+DESTINATION\s+"([^"]+)"\s+TYPE\s+([A-Z_]+)\s+FILES\s*(.*?)\)', body, re.S)
        if not entries or len(entries) != len(re.findall(r'\bfile\s*\(\s*INSTALL\b', body)):
            raise RuntimeError('An unsupported residual install statement leaves the reviewed path guard.')
        if re.search(r'\binclude\s*\([^\n]*cmake_install', body, re.I):
            raise RuntimeError('A residual install script includes an unreviewed child installer.')
        for destination, kind, files in entries:
            marker = '${CMAKE_INSTALL_PREFIX}/'
            name = destination.removeprefix(marker)
            relative = PurePosixPath(name)
            if (not destination.startswith(marker) or relative.is_absolute() or '..' in relative.parts
                    or re.search(r'[\\\s:$;]', name) or kind not in ('FILE', 'DIRECTORY', 'SHARED_LIBRARY', 'EXECUTABLE')):
                raise RuntimeError('An unsafe residual install destination leaves the SDK prefix.')
            target = prefix / str(relative)
            plain_target(target, directory=True)
            sources = re.findall(r'"([^"]+)"', files)
            if re.sub(r'"[^"]+"|\s+', '', files):
                raise RuntimeError('A residual install source uses unsupported generated arguments.')
            if kind == 'DIRECTORY':
                tree(target)
                continue
            for source in sources:
                source_path = PurePosixPath(source)
                if not source_path.is_absolute() or '..' in source_path.parts or '$' in source or '\\' in source:
                    raise RuntimeError('A residual install source path is ambiguous.')
                leaf = target / source_path.name
                key = leaf.relative_to(prefix).as_posix()
                if leaf.is_symlink() and key in aliases:
                    soname(leaf)
                    continue
                plain_target(leaf)


def source_phase(work, prefix, data, expected, old):
    state = work / 'residual-security-start.json'
    intent = {'identity': expected, 'baselineProof': record(prefix / previous.PROOF),
              'beforeOutputs': old['incremental']['outputs']}
    if state.exists():
        if json.loads(state.read_text(encoding='utf-8')) != intent:
            raise RuntimeError('The retained residual build transaction differs.')
        try:
            verify_source(work / 'source', data, 'after')
            return 'after', intent
        except (RuntimeError, OSError):
            pass
    verify_source(work / 'source', data, 'before')
    previous.verify_build(prefix)
    return 'before', intent


def build_environment(work):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('LD_', 'QT_', 'QTWEBENGINE_'))
           and key not in ('DESTDIR', 'CMAKE_INSTALL_MODE')}
    env['PATH'] = str(work / 'node-v22.23.4-linux-x64/bin') + os.pathsep + env['PATH']
    env['NODEJS_EXECUTABLE'] = str(work / 'node-v22.23.4-linux-x64/bin/node')
    env['NINJAFLAGS'] = '-j 2'
    env['CMAKE_BUILD_PARALLEL_LEVEL'] = '1'
    return env


def build(work, prefix, attestation=None, drive=None, check_only=False):
    data = verify_inputs()
    expected = identity(data)
    work, prefix = (plain_target(path, directory=True, required=True) for path in (work, prefix))
    storage = check_build_storage((work, prefix), attestation, drive)
    verify_cache_paths(work, prefix)
    resume.verify_configuration(work, prefix)
    verify_install_paths(work, prefix)
    resume.verify_tools(work, resume.load_resume_inputs()[0])
    memory = resume.check_memory()
    old = historical_proofs(prefix)
    if (prefix / PROOF).exists():
        return verify_build(prefix)
    phase, intent = source_phase(work, prefix, data, expected, old)
    receipt = {'identity': expected, 'sourcePhase': phase, 'storage': storage,
               'memory': memory, 'runtimeTargets': base.runtime_scope()}
    if check_only:
        print(json.dumps(receipt, indent=2))
        return receipt
    with project_build_lock(storage):
        verify_cache_paths(work, prefix)
        if identity(verify_inputs()) != expected or historical_proofs(prefix) != old:
            raise RuntimeError('Residual inputs changed before the build lock was acquired.')
        resume.verify_configuration(work, prefix)
        verify_install_paths(work, prefix)
        phase, intent = source_phase(work, prefix, data, expected, old)
        budget = RuntimeBudget((work, prefix), attestation, drive, work / 'residual-security-budget.json')
        budget.check()
        if phase == 'before':
            source = work / 'source/src/3rdparty/chromium'
            patches = [ROOT / data['patchDirectory'] / data[field]['name'] for field in ('adapter', 'regression')]
            subprocess.run(['git', 'apply', '--check', *(str(path) for path in patches)], cwd=source, check=True)
            verify_source(work / 'source', data, 'before')
            if not (work / 'residual-security-start.json').exists():
                publish(work / 'residual-security-start.json', intent)
            subprocess.run(['git', 'apply', *(str(path) for path in patches)], cwd=source, check=True)
            verify_source(work / 'source', data, 'after')
        env = build_environment(work)
        log, status = work / 'residual-security-build.log', work / 'residual-security-process.json'
        def verify_frozen_inputs():
            verify_source(work / 'source', data, 'after')
            resume.verify_configuration(work, prefix)
            verify_install_paths(work, prefix)
            if identity(verify_inputs()) != expected or historical_proofs(prefix) != old:
                raise RuntimeError('Residual recipe, source or historical inputs changed while compiling.')
        def run_phase(command, output, name):
            verify_cache_paths(work, prefix)
            verify_frozen_inputs()
            resume.run_phase(command, output, env, status, name, budget, True)
        run_phase(['ninja', '-j', '2', '-C', str(work / 'build/src/core/Release/x86_64'), 'QtWebEngineCore'], log, 'residual-core')
        run_phase(['cmake', '--build', str(work / 'build'), '--target', *base.RUNTIME_TARGETS, '--parallel', '1'], log, 'residual-runtime')
        verify_source(work / 'source', data, 'after')
        for script in base.RUNTIME_INSTALL_SCRIPTS:
            run_phase(['cmake', '-DCMAKE_INSTALL_PREFIX=' + str(prefix), '-P', str(work / 'build' / script)], work / 'residual-security-install.log', 'residual-install')
        outputs = base.runtime_outputs(prefix)
        abi = resume.elf_floor([prefix / row['file'] for row in outputs])
        verify_frozen_inputs()
        proof = {**expected, 'baselineProof': record(prefix / previous.PROOF),
                 'priorOutputs': old['incremental']['outputs'], 'outputs': outputs, 'abiFloor': abi}
        verify_public(proof)
        verify_cache_paths(work, prefix)
        publish(prefix / PROOF, proof)
        result = verify_build(prefix)
        status.write_text(json.dumps({'phase': 'complete', 'proof': record(prefix / PROOF)}, indent=2) + '\n', encoding='utf-8')
        print('Verified the residual WebEngine security runtime.', flush=True)
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--prefix', required=True, type=Path)
    parser.add_argument('--storage-attestation', type=Path)
    parser.add_argument('--expected-host-drive')
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    if args.verify_only:
        verify_build(args.prefix)
        print('Residual WebEngine runtime proof verified.')
    elif not args.work or sys.platform != 'linux':
        parser.error('Build requires Linux and the retained --work cache.')
    else:
        build(args.work, args.prefix, args.storage_attestation, args.expected_host_drive, args.check_only)
