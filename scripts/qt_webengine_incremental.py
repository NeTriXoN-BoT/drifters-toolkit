"""Rebuild the retained Qt cache with the pinned 30 October security delta.

No downloads, extraction, second SDK, system installation or sandbox changes.
Earlier vendor/Aura and four-repair provenance files remain immutable.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import sys

sys.dont_write_bytecode = True
import bootstrap_qt_webengine_security as base
import resume_qt_webengine_security as resume
import qt_webengine_followup as previous
from linux_build_storage import check_build_storage, project_build_lock, RuntimeBudget

ROOT = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name('qt_webengine_incremental_lock.json')
PROOF = 'QTWEBENGINE-SECURITY-INCREMENTAL-PROVENANCE.json'
SERIES_DIGEST = 'f4448666bdf1e2f724a1c941e4095239fb91de061dc8681e6e0cb3a4c2435f75'
CRITICAL = '6442ebe52d911b4b0f4df0ac5ffe307e79b705b1'
record = base.file_record
publish = previous.publish


def plain_target(path, directory=False, required=False):
    """Reject aliases before resolving or opening retained cache/write paths."""
    path = Path(path)
    if '..' in path.parts:
        raise RuntimeError('An incremental cache path contains parent traversal.')
    path = path.absolute()
    for item in (path, *path.parents):
        try:
            info = item.lstat()
        except FileNotFoundError:
            if item == path and required:
                raise RuntimeError('A required incremental cache path is missing.')
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise RuntimeError('An incremental cache path is redirected.')
        if item != path or directory:
            if not stat.S_ISDIR(info.st_mode):
                raise RuntimeError('An incremental cache ancestor is not a directory.')
        elif not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise RuntimeError('An incremental cache file is not an independent regular file.')
    return path


def verify_cache_paths(work, prefix):
    for path in (work, prefix, work / 'build', work / 'source', prefix / 'lib',
                 prefix / 'libexec', prefix / 'resources', prefix / 'translations/qtwebengine_locales',
                 work / 'build/src/core/Release/x86_64'):
        plain_target(path, directory=True, required=True)
    for path in (work / 'incremental-security-start.json', work / 'incremental-security-budget.json',
                 work / 'incremental-security-build.log', work / 'incremental-security-process.json',
                 work / 'incremental-security-install.log', prefix / PROOF,
                 prefix / previous.PROOF, prefix / base.PROOF):
        plain_target(path)
    # A partially completed install may legitimately omit some outputs. Existing
    # canonical destinations must still be independent files before CMake writes
    # them. SONAME aliases (.so.6/.so) are not install destinations in this list.
    outputs = [prefix / 'lib/libQt6WebEngineCore.so.6.11.3',
               prefix / 'lib/libQt6WebEngineWidgets.so.6.11.3',
               prefix / 'libexec/QtWebEngineProcess',
               *(prefix / 'resources' / name for name in base.RUNTIME_RESOURCES),
               prefix / 'translations/qtwebengine_locales/en-US.pak',
               *(prefix / 'translations/qtwebengine_locales').glob('*.pak')]
    for path in outputs:
        plain_target(path)


def valid_record(value):
    return (isinstance(value, dict) and set(value) == {'sha256', 'size'}
            and re.fullmatch('[a-f0-9]{64}', value.get('sha256', ''))
            and type(value.get('size')) is int and 0 < value['size'] <= 2 * 1024 * 1024)


def load_lock(path=LOCK):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    upstream = data.get('upstream', [])
    series = '\n'.join(row.get('commit', '') for row in upstream)
    if (data.get('schemaVersion') != 1 or data.get('incrementalRevision') != 1
            or data.get('qtVersion') != '6.11.3'
            or data.get('baseChromiumVersion') != '140.0.7339.264'
            or data.get('vendorReferenceHead') != 'a927ba3741827670206ab9ddb28c935032826235'
            or data.get('baselineFollowupLock') != record(previous.LOCK)
            or data.get('patchDirectory') != 'vendor/qtwebengine-security/2026-10-30'
            or len(upstream) != 53 or hashlib.sha256(series.encode()).hexdigest() != SERIES_DIGEST
            or len(data.get('files', [])) != 230 or len(data.get('excludedVendorCommits', [])) != 22):
        raise RuntimeError('The incremental WebEngine security lock is invalid.')
    names = set()
    for row in data['files']:
        name = row.get('path', '')
        relative = PurePosixPath(name)
        if (relative.is_absolute() or '..' in relative.parts or '\\' in name
                or not name.startswith('chromium/') or name in names
                or any(not valid_record(row.get(phase)) for phase in ('before', 'after')
                       if row.get(phase) is not None)
                or row.get('after') is None or set(row) != {'path', 'before', 'after'}):
            raise RuntimeError('An incremental source identity is unsafe or invalid.')
        names.add(name)
    if sum(row['before'] is None for row in data['files']) != 18:
        raise RuntimeError('The incremental newly added source scope differs.')
    for row in all_patch_inputs(data):
        if (not re.fullmatch(r'[A-Za-z0-9-]+\.patch', row.get('name', ''))
                or not valid_record({key: row.get(key) for key in ('sha256', 'size')})):
            raise RuntimeError('An incremental upstream export identity is invalid.')
    for index, row in enumerate(upstream):
        commit = row['commit']
        expected = ('https://code.qt.io/cgit/qt/qtwebengine-chromium.git/patch/?h=140-based&id=' + commit
                    if index < 52 else 'https://github.com/chromium/chromium/commit/' + CRITICAL + '.patch')
        if row.get('url') != expected or row.get('kind') != ('qt-vendor' if index < 52 else 'chromium'):
            raise RuntimeError('An incremental primary reference differs.')
    if data['adapter']['name'] != 'qt140-incremental-compatibility.patch':
        raise RuntimeError('The incremental Qt140 compatibility adapter is missing.')
    return data


def all_patch_inputs(data):
    return [*data['upstream'], data['adapter']]


def verify_inputs(root=ROOT, lock=LOCK):
    data = load_lock(lock)
    for row in all_patch_inputs(data):
        if record(Path(root) / data['patchDirectory'] / row['name']) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('A pinned incremental patch changed: ' + row['name'])
    body = (Path(root) / data['patchDirectory'] / data['adapter']['name']).read_text(encoding='utf-8')
    paths = re.findall(r'^diff --git a/([^ ]+) b/([^\n]+)$', body, re.M)
    if (len(paths) != 230 or any(a != b for a, b in paths)
            or {'chromium/' + a for a, _ in paths} != {row['path'] for row in data['files']}):
        raise RuntimeError('The incremental adapter leaves its exact reviewed source scope.')
    source_files(data, 'before')
    return data


def source_files(data, phase):
    if phase not in ('before', 'after'):
        raise ValueError('Unsupported source phase.')
    original = previous.source_files(previous.verify_inputs(), 'after')
    result = dict(original)
    for row in data['files']:
        old = row['before']
        if row['path'] in original and (old is None or original[row['path']] != old['sha256']):
            raise RuntimeError('The incremental preimage disagrees with the frozen previous inventory.')
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
        if any(parent.is_symlink() for parent in (path, *path.parents)):
            raise RuntimeError('An incremental source path is redirected.')
        value = row[phase]
        if value is None:
            if path.exists():
                raise RuntimeError('A new incremental source already exists: ' + row['path'])
        elif record(path) != value:
            raise RuntimeError('An incremental source size or digest differs: ' + row['path'])


def identity(data):
    return {'schemaVersion': 1, 'incrementalRevision': data['incrementalRevision'],
            'lock': record(LOCK), 'recipe': record(__file__),
            'baselineFollowupIdentity': previous.identity(previous.verify_inputs()),
            'upstream': [{key: row[key] for key in ('commit', 'sha256', 'size')} for row in data['upstream']],
            'adapter': {key: data['adapter'][key] for key in ('name', 'sha256', 'size')},
            'effectiveSourceFiles': len(source_files(data, 'after'))}


def verify_public(proof):
    if not isinstance(proof, dict) or any(proof.get(k) != v for k, v in identity(verify_inputs()).items()):
        raise RuntimeError('The incremental WebEngine security proof is missing or stale.')
    for field in ('outputs', 'priorOutputs'):
        rows = proof.get(field)
        if (not isinstance(rows, list) or not 1 <= len(rows) <= 1024
                or any(not isinstance(row, dict) or set(row) != {'file', 'sha256', 'size'}
                       or type(row.get('size')) is not int or not 0 < row['size'] <= 512 * 1024 * 1024
                       or not re.fullmatch('[a-f0-9]{64}', row.get('sha256', '')) for row in rows)):
            raise RuntimeError('An incremental runtime output identity is invalid.')
        base.verify_output_scope(rows)
    core = 'lib/libQt6WebEngineCore.so.6.11.3'
    before = next(row for row in proof['priorOutputs'] if row['file'] == core)
    if next(row for row in proof['outputs'] if row['file'] == core) == before:
        raise RuntimeError('The incremental repair did not replace the preceding engine core.')
    if not valid_record(proof.get('baselineProof')):
        raise RuntimeError('The immutable predecessor proof identity is missing.')
    return proof


def historical_proofs(prefix):
    for name in (base.PROOF, previous.PROOF):
        plain_target(prefix / name, required=True)
    original = previous.validate_baseline(json.loads((prefix / base.PROOF).read_text(encoding='utf-8')))
    followup = previous.verify_public(json.loads((prefix / previous.PROOF).read_text(encoding='utf-8')))
    if followup.get('baselineProof') != record(prefix / base.PROOF):
        raise RuntimeError('The immutable four-repair proof lost its original baseline.')
    return {'baseline': original, 'followup': followup}


def verify_build(prefix):
    prefix = plain_target(prefix, directory=True, required=True)
    plain_target(prefix / PROOF, required=True)
    old = historical_proofs(prefix)
    proof = verify_public(json.loads((prefix / PROOF).read_text(encoding='utf-8')))
    if (proof['baselineProof'] != record(prefix / previous.PROOF)
            or proof['priorOutputs'] != old['followup']['outputs']):
        raise RuntimeError('The immutable preceding runtime provenance changed.')
    for row in proof['outputs']:
        plain_target(prefix / row['file'], required=True)
        if record(prefix / row['file']) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('An incremental runtime output differs: ' + row['file'])
    return {**old, 'incremental': proof}


def public_provenance(proof):
    result = previous.public_provenance(proof)
    result['outputs'] = proof['incremental']['outputs']
    result['incrementalDelta'] = {key: proof['incremental'][key]
                                 for key in (*identity(verify_inputs()), 'baselineProof', 'priorOutputs', 'outputs', 'abiFloor')}
    return result


def build(work, prefix, attestation=None, drive=None, check_only=False):
    data = verify_inputs()
    expected = identity(data)
    work, prefix = (plain_target(path, directory=True, required=True) for path in (work, prefix))
    storage = check_build_storage((work, prefix), attestation, drive)
    verify_cache_paths(work, prefix)
    resume.verify_configuration(work, prefix)
    resume.verify_tools(work, resume.load_resume_inputs()[0])
    resume.check_memory()
    old = historical_proofs(prefix)
    if (prefix / PROOF).exists():
        return verify_build(prefix)
    state_path = work / 'incremental-security-start.json'
    intent = {'identity': expected, 'baselineProof': record(prefix / previous.PROOF),
              'beforeOutputs': old['followup']['outputs']}
    if state_path.exists():
        if json.loads(state_path.read_text(encoding='utf-8')) != intent:
            raise RuntimeError('The retained incremental build transaction differs.')
        try:
            verify_source(work / 'source', data, 'after')
            phase = 'after'
        except (RuntimeError, OSError):
            verify_source(work / 'source', data, 'before')
            previous.verify_build(prefix)
            phase = 'before'
    else:
        previous.verify_build(prefix)
        verify_source(work / 'source', data, 'before')
        phase = 'before'
    receipt = {'identity': expected, 'sourcePhase': phase, 'storage': storage,
               'memory': resume.check_memory(), 'runtimeTargets': base.runtime_scope()}
    if check_only:
        print(json.dumps(receipt, indent=2))
        return receipt
    with project_build_lock(storage):
        verify_cache_paths(work, prefix)
        budget = RuntimeBudget((work, prefix), attestation, drive, work / 'incremental-security-budget.json')
        budget.check()
        if phase == 'before':
            patch = ROOT / data['patchDirectory'] / data['adapter']['name']
            source = work / 'source/src/3rdparty/chromium'
            subprocess.run(['git', 'apply', '--check', str(patch)], cwd=source, check=True)
            verify_source(work / 'source', data, 'before')
            if not state_path.exists():
                publish(state_path, intent)
            subprocess.run(['git', 'apply', str(patch)], cwd=source, check=True)
            verify_source(work / 'source', data, 'after')
        env = {key: value for key, value in os.environ.items() if not key.startswith(('LD_', 'QT_', 'QTWEBENGINE_'))}
        env['PATH'] = str(work / 'node-v22.23.4-linux-x64/bin') + os.pathsep + env['PATH']
        env['NODEJS_EXECUTABLE'] = str(work / 'node-v22.23.4-linux-x64/bin/node')
        env['NINJAFLAGS'] = '-j 2'
        env['CMAKE_BUILD_PARALLEL_LEVEL'] = '1'
        log, status = work / 'incremental-security-build.log', work / 'incremental-security-process.json'
        def run_phase(command, output, phase):
            verify_cache_paths(work, prefix)
            resume.run_phase(command, output, env, status, phase, budget, True)
        run_phase(['ninja', '-j', '2', '-C', str(work / 'build/src/core/Release/x86_64'), 'QtWebEngineCore'], log, 'incremental-core')
        run_phase(['cmake', '--build', str(work / 'build'), '--target', *base.RUNTIME_TARGETS, '--parallel', '1'], log, 'incremental-runtime')
        verify_source(work / 'source', data, 'after')
        for script in base.RUNTIME_INSTALL_SCRIPTS:
            run_phase(['cmake', '-DCMAKE_INSTALL_PREFIX=' + str(prefix), '-P', str(work / 'build' / script)], work / 'incremental-security-install.log', 'incremental-install')
        outputs = base.runtime_outputs(prefix)
        abi = resume.elf_floor([prefix / row['file'] for row in outputs])
        if identity(verify_inputs()) != expected:
            raise RuntimeError('Incremental recipe inputs changed while compiling.')
        proof = {**expected, 'baselineProof': record(prefix / previous.PROOF),
                 'priorOutputs': old['followup']['outputs'], 'outputs': outputs, 'abiFloor': abi}
        verify_public(proof)
        verify_cache_paths(work, prefix)
        publish(prefix / PROOF, proof)
        result = verify_build(prefix)
        status.write_text(json.dumps({'phase': 'complete', 'proof': record(prefix / PROOF)}, indent=2) + '\n', encoding='utf-8')
        print('Verified the incremental WebEngine security runtime.', flush=True)
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
        print('Incremental WebEngine runtime proof verified.')
    elif not args.work or sys.platform != 'linux':
        parser.error('Build requires Linux and the retained --work cache.')
    else:
        build(args.work, args.prefix, args.storage_attestation, args.expected_host_drive, args.check_only)
