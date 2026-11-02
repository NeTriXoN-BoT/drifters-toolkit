"""Apply reviewed upstream repairs to an existing, verified Qt build cache.

No downloads, extraction, SDK copies, system installation or sandbox changes.
The original vendor/Aura provenance remains immutable; this adds a separately
verified source and runtime proof and is required by current packaging.
"""
from pathlib import Path, PurePosixPath
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
import bootstrap_qt_webengine_security as base
import resume_qt_webengine_security as resume
from linux_build_storage import check_build_storage, project_build_lock, RuntimeBudget

ROOT = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name('qt_webengine_followup_lock.json')
PROOF = 'QTWEBENGINE-SECURITY-FOLLOWUP-PROVENANCE.json'
COMMITS = {'8c4d5b7dfef93032b452806c069d3ce8416b6ac7',
           '9340de2d6a5ee57d8a39d1863890123b34d8e325',
           '6438ea41e25396520b79d2713994852140713d78',
           '0ef44edd0ec4498a1599e6987422bca461fdd6ab'}


def record(path):
    return base.file_record(path)


def load_lock(path=LOCK):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    if (data.get('schemaVersion') != 1 or data.get('followupRevision') != 1
            or data.get('qtVersion') != '6.11.3'
            or data.get('patchDirectory') != 'vendor/qtwebengine-security/2026-10-28'
            or data.get('baselineSecurityLock') != record(base.LOCK)
            or data.get('baselineFinalInventory') != record(base.FINAL_INVENTORY)
            or len(data.get('upstream', [])) != 4
            or {row.get('commit') for row in data['upstream']} != COMMITS
            or len(data.get('files', [])) != 20):
        raise RuntimeError('The separate WebEngine follow-up identity is invalid.')
    names = set()
    for row in data['files']:
        name = row.get('path', ''); relative = PurePosixPath(name)
        if (relative.is_absolute() or '..' in relative.parts or '\\' in name
                or not name.startswith('chromium/') or name in names):
            raise RuntimeError('A follow-up source path is unsafe or duplicated.')
        names.add(name)
        for phase in ('before', 'after'):
            value = row.get(phase, {})
            if (set(value) != {'sha256', 'size'} or not re.fullmatch('[a-f0-9]{64}', value.get('sha256', ''))
                    or type(value.get('size')) is not int or not 0 < value['size'] < 1024 * 1024):
                raise RuntimeError('A follow-up preimage or final identity is invalid.')
    for row in all_patch_inputs(data):
        if (not re.fullmatch(r'[A-Za-z0-9-]+\.patch', row.get('name', ''))
                or not re.fullmatch('[a-f0-9]{64}', row.get('sha256', ''))
                or type(row.get('size')) is not int or not 0 < row['size'] < 1024 * 1024):
            raise RuntimeError('A follow-up patch identity is invalid.')
    if data['adapter']['name'] != 'qt140-four-critical-compatibility.patch':
        raise RuntimeError('The reviewed follow-up adapter is missing.')
    for row in data['upstream']:
        host = 'skia.googlesource.com/skia' if row['commit'].startswith('0ef') else 'chromium.googlesource.com/chromium/src'
        if (row['name'] != row['commit'] + '.patch'
                or row.get('url') != 'https://' + host + '/+/' + row['commit'] + '^!/?format=TEXT'):
            raise RuntimeError('A follow-up primary reference differs.')
    return data


def all_patch_inputs(data):
    return [*data['upstream'], data['adapter']]


def verify_inputs(root=ROOT, lock=LOCK):
    data = load_lock(lock)
    for row in all_patch_inputs(data):
        if record(Path(root) / data['patchDirectory'] / row['name']) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('A reviewed follow-up patch changed: ' + row['name'])
    body = (Path(root) / data['patchDirectory'] / data['adapter']['name']).read_text(encoding='utf-8')
    before = re.findall(r'^--- a/(.*)$', body, re.M)
    after = re.findall(r'^\+\+\+ b/(.*)$', body, re.M)
    if before != after or len(before) != 20 or {'chromium/' + name for name in before} != {r['path'] for r in data['files']}:
        raise RuntimeError('The follow-up adapter leaves its exact twenty-file scope.')
    return data


def source_files(data, phase):
    _, _, original = resume.load_resume_inputs()
    result = dict(original)
    for row in data['files']:
        if row['path'] in original and original[row['path']] != row['before']['sha256']:
            raise RuntimeError('The follow-up preimage disagrees with the frozen vendor/Aura inventory.')
        result[row['path']] = row[phase]['sha256']
    return result


def verify_source(source, data, phase):
    resume.verify_source(source, source_files(data, phase))
    for row in data['files']:
        if record(Path(source) / 'src/3rdparty' / row['path']) != row[phase]:
            raise RuntimeError('A follow-up source size differs: ' + row['path'])


def validate_baseline(proof):
    """Validate frozen provenance metadata without claiming old runtime bytes.

    First-time builds additionally call the original full runtime verifier.
    After replacement its immutable output list remains historical evidence.
    """
    data = base.verify_inputs()
    expected = {'schemaVersion': 1, 'recipeRevision': data['recipeRevision'],
                'securityLock': record(base.LOCK), 'recipe': record(base.__file__),
                'storageHelper': record(Path(__file__).with_name('linux_build_storage.py')),
                'runtimeScope': base.runtime_scope(), 'compatibilityBackports': data['compatibilityBackports'],
                'sourceInventories': base.source_inventory_identity(),
                'patchedChromiumCommit': data['patchedChromiumCommit'],
                'sourceArchive': {'sha256': data['sourceArchive']['digest'], 'size': data['sourceArchive']['size']},
                'patches': [{key: row[key] for key in ('commit', 'sha256', 'size')} for row in data['patches']]}
    if any(proof.get(key) != value for key, value in expected.items()):
        raise RuntimeError('The immutable vendor/Aura baseline provenance is stale.')
    base.verify_output_scope(proof.get('outputs', []))
    if 'continuation' in proof:
        resume.verify_continuation(proof['continuation'])
    return proof


def identity(data):
    return {'schemaVersion': 1, 'followupRevision': data['followupRevision'],
            'lock': record(LOCK), 'recipe': record(__file__),
            'baselineSourceInventories': base.source_inventory_identity(),
            'upstream': [{key: row[key] for key in ('commit', 'sha256', 'size')} for row in data['upstream']],
            'adapter': {key: data['adapter'][key] for key in ('name', 'sha256', 'size')},
            'effectiveSourceFiles': len(source_files(data, 'after'))}


def verify_public(proof):
    data = verify_inputs()
    if not isinstance(proof, dict) or any(proof.get(k) != v for k, v in identity(data).items()):
        raise RuntimeError('The required upstream WebEngine repair proof is missing or stale.')
    base.verify_output_scope(proof.get('outputs', []))
    return proof


def verify_build(prefix):
    prefix = Path(prefix).resolve(strict=True)
    old = validate_baseline(json.loads((prefix / base.PROOF).read_text(encoding='utf-8')))
    proof = verify_public(json.loads((prefix / PROOF).read_text(encoding='utf-8')))
    if proof.get('baselineProof') != record(prefix / base.PROOF):
        raise RuntimeError('The immutable baseline provenance changed.')
    if proof['outputs'] == old['outputs']:
        raise RuntimeError('The follow-up proof did not replace the original engine bytes.')
    for row in proof['outputs']:
        if record(prefix / row['file']) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('A follow-up runtime output differs: ' + row['file'])
    return {'baseline': old, 'followup': proof}


def public_provenance(proof):
    result = base.public_provenance(proof['baseline'])
    result['outputs'] = proof['followup']['outputs']
    result['upstreamFollowup'] = {key: proof['followup'][key]
                                 for key in (*identity(verify_inputs()), 'baselineProof', 'outputs', 'abiFloor')}
    return result


def publish(path, value):
    """Exclusive publication; never replace existing historical records."""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix='.followup-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(json.dumps(value, indent=2) + '\n'); stream.flush(); os.fsync(stream.fileno())
        os.link(temporary, path)
        if sys.platform == 'linux':
            descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def build(work, prefix, attestation=None, drive=None, check_only=False):
    data = verify_inputs(); expected = identity(data)
    work, prefix = Path(work).resolve(strict=True), Path(prefix).resolve(strict=True)
    storage = check_build_storage((work, prefix), attestation, drive)
    if not (work / 'build').is_dir() or not (prefix / 'lib').is_dir():
        raise RuntimeError('The existing Qt build and SDK are required; no copy will be created.')
    resume.verify_configuration(work, prefix)
    resume.verify_tools(work, resume.load_resume_inputs()[0]); resume.check_memory()
    old = validate_baseline(json.loads((prefix / base.PROOF).read_text(encoding='utf-8')))
    state_path = work / 'upstream-followup-start.json'
    if (prefix / PROOF).exists():
        return verify_build(prefix)
    if state_path.exists():
        state = json.loads(state_path.read_text(encoding='utf-8'))
        if state != {'identity': expected, 'baselineProof': record(prefix / base.PROOF), 'beforeOutputs': old['outputs']}:
            raise RuntimeError('The existing follow-up transaction differs from this reviewed build.')
        try:
            verify_source(work / 'source', data, 'after')
            phase = 'after'
        except (RuntimeError, OSError):
            # A crash after the durable intent but before git apply may leave
            # the complete preimage. Accept only that exact intact alternative.
            verify_source(work / 'source', data, 'before')
            base.verify_build(prefix)
            phase = 'before'
    else:
        base.verify_build(prefix)
        verify_source(work / 'source', data, 'before')
        phase = 'before'
    receipt = {'identity': expected, 'sourcePhase': phase, 'storage': storage,
               'memory': resume.check_memory(), 'runtimeTargets': base.runtime_scope()}
    if check_only:
        print(json.dumps(receipt, indent=2)); return receipt
    with project_build_lock(storage):
        budget = RuntimeBudget((work, prefix), attestation, drive, work / 'upstream-followup-budget.json')
        budget.check()
        if phase == 'before':
            patch = ROOT / data['patchDirectory'] / data['adapter']['name']
            source = work / 'source/src/3rdparty/chromium'
            subprocess.run(['git', 'apply', '--check', str(patch)], cwd=source, check=True)
            verify_source(work / 'source', data, 'before')
            # The baseline SDK proof remains untouched. This durable transaction
            # permits only the exact reviewed postimage on interrupted resume.
            if not state_path.exists():
                publish(state_path, {'identity': expected, 'baselineProof': record(prefix / base.PROOF), 'beforeOutputs': old['outputs']})
            subprocess.run(['git', 'apply', str(patch)], cwd=source, check=True)
            verify_source(work / 'source', data, 'after')
        env = {key: value for key, value in os.environ.items() if not key.startswith(('LD_', 'QT_', 'QTWEBENGINE_'))}
        env['PATH'] = str(work / 'node-v22.23.4-linux-x64/bin') + os.pathsep + env['PATH']
        env['NODEJS_EXECUTABLE'] = str(work / 'node-v22.23.4-linux-x64/bin/node')
        env['NINJAFLAGS'] = '-j 2'; env['CMAKE_BUILD_PARALLEL_LEVEL'] = '1'
        log = work / 'upstream-followup-build.log'; status = work / 'upstream-followup-process.json'
        resume.run_phase(['ninja', '-j', '2', '-C', str(work / 'build/src/core/Release/x86_64'), 'QtWebEngineCore'], log, env, status, 'upstream-core', budget, True)
        resume.run_phase(['cmake', '--build', str(work / 'build'), '--target', *base.RUNTIME_TARGETS, '--parallel', '1'], log, env, status, 'upstream-runtime', budget, True)
        verify_source(work / 'source', data, 'after')
        for script in base.RUNTIME_INSTALL_SCRIPTS:
            resume.run_phase(['cmake', '-DCMAKE_INSTALL_PREFIX=' + str(prefix), '-P', str(work / 'build' / script)], work / 'upstream-followup-install.log', env, status, 'upstream-install', budget, True)
        outputs = base.runtime_outputs(prefix)
        before_core = next(row for row in old['outputs'] if row['file'] == 'lib/libQt6WebEngineCore.so.6.11.3')
        if next(row for row in outputs if row['file'] == before_core['file']) == before_core:
            raise RuntimeError('The reviewed repairs did not replace the original engine core.')
        abi = resume.elf_floor([prefix / row['file'] for row in outputs])
        if identity(verify_inputs()) != expected:
            raise RuntimeError('The build helper or reviewed inputs changed while compiling.')
        proof = {**expected, 'baselineProof': record(prefix / base.PROOF), 'outputs': outputs, 'abiFloor': abi}
        publish(prefix / PROOF, proof)
        result = verify_build(prefix)
        status.write_text(json.dumps({'phase': 'complete', 'proof': record(prefix / PROOF)}, indent=2) + '\n', encoding='utf-8')
        print('Verified the separately rebuilt upstream WebEngine repairs.', flush=True)
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
        verify_build(args.prefix); print('Upstream follow-up runtime proof verified.')
    elif not args.work or sys.platform != 'linux':
        parser.error('Build requires Linux and the existing --work cache.')
    else:
        build(args.work, args.prefix, args.storage_attestation, args.expected_host_drive, args.check_only)
