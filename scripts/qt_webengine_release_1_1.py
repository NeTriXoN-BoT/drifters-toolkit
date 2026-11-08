"""Add the reviewed 8 November Qt vendor delta to the retained security cache.

No downloads, extraction, new SDK, sandbox changes or system installation.
The vendor-followup source/lock/proof and all older provenance remain immutable.
"""
from pathlib import Path
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
import qt_webengine_vendor_followup as previous
from linux_build_storage import check_build_storage, project_build_lock, RuntimeBudget

ROOT = Path(__file__).resolve().parents[1]
LOCK = Path(__file__).with_name('qt_webengine_release_1_1_lock.json')
PROOF = 'QTWEBENGINE-SECURITY-RELEASE-1-1-PROVENANCE.json'
VENDOR_BASE = '7f36c49d30de45ee5518bcb2cf7ec52d4f970d40'
VENDOR_HEAD = '97302d5d8a7571782f3d1d96c1d9aa4bd009c827'
SOURCE_COUNT = 157
SERIES_DIGEST = '66a476d2d3aa36599f11a47b6b5591435572ce0c7ec4a67f4258588f32f61610'
EXPECTED_WORK = Path('/tmp/drifter-qt-security-20261026/build-v3')
EXPECTED_PREFIX = Path('/tmp/drifter-qt-security-20261026/sdk-patched-v3')
MAX_PATCH_BYTES = 8 * 1024 * 1024
OBSERVED_PROOF_MEANING = previous.OBSERVED_PROOF_MEANING
record, publish, plain_target = previous.record, previous.publish, previous.plain_target
valid_record, source_name = previous.valid_record, previous.source_name
verify_install_paths, build_environment = previous.verify_install_paths, previous.build_environment


def all_patch_inputs(data):
    return [*data['upstream'], data['adapter']]


def load_lock(path=LOCK):
    data = json.loads(Path(path).read_text(encoding='utf-8'))
    rows = data.get('upstream', [])
    if (data.get('schemaVersion') != 1 or data.get('release11Revision') != 1
            or data.get('qtVersion') != '6.11.3' or data.get('baseChromiumVersion') != '140.0.7339.264'
            or data.get('vendorBaseHead') != VENDOR_BASE or data.get('vendorReferenceHead') != VENDOR_HEAD
            or data.get('baselineVendorFollowupLock') != record(previous.LOCK)
            or not valid_record(data.get('baselineRuntimeProof'))
            or data.get('baselineRuntimeProofMeaning') != OBSERVED_PROOF_MEANING
            or data.get('baselineSourceCheckpoint') != previous.source_files(previous.verify_inputs(), 'after')
            or data.get('patchDirectory') != 'vendor/qtwebengine-security/2026-11-08-release-1.1'
            or not isinstance(rows, list) or len(rows) != 57
            or hashlib.sha256('\n'.join(row.get('commit', '') for row in rows).encode()).hexdigest() != SERIES_DIGEST
            or len(data.get('files', [])) != SOURCE_COUNT
            or data.get('adapter', {}).get('name') != 'qt140-release-1-1.patch'):
        raise RuntimeError('The release-1-1 security lock is invalid.')
    names = set()
    for row in data['files']:
        name = source_name(row.get('path'))
        if (set(row) != {'path', 'before', 'after'} or name in names
                or row['before'] is None and row['after'] is None
                or any(value is not None and not valid_record(value) for value in (row['before'], row['after']))):
            raise RuntimeError('A release-1-1 source identity is invalid or duplicated.')
        names.add(name)
    patches = set()
    for row in all_patch_inputs(data):
        if (not isinstance(row, dict) or not isinstance(row.get('name'), str)
                or not re.fullmatch(r'[A-Za-z0-9-]+\.patch', row['name']) or row['name'] in patches
                or not valid_record({key: row.get(key) for key in ('sha256', 'size')})):
            raise RuntimeError('A release-1-1 patch identity is unsafe or invalid.')
        patches.add(row['name'])
    if sum(row['size'] for row in all_patch_inputs(data)) > 16 * 1024 * 1024:
        raise RuntimeError('The release-1-1 exports exceed their input budget.')
    for row in rows:
        commit = row.get('commit', '')
        if (not re.fullmatch('[a-f0-9]{40}', commit)
                or row.get('url') != 'https://github.com/qt/qtwebengine-chromium/commit/' + commit + '.patch'):
            raise RuntimeError('A release-1-1 primary reference differs.')
    source_files(data, 'before')
    return data


def patch_paths(body, original=False):
    diffs = re.findall(r'^diff --git a/([^ ]+) b/([^\r\n]+)$', body, re.M)
    if (not diffs or any(a != b for a, b in diffs)
            or re.search(r'^(?:rename |copy |GIT binary patch|Binary files|old mode|new mode)', body, re.M)
            or any(mode != '100644' for mode in re.findall(r'^(?:new|deleted) file mode (\S+)$', body, re.M))):
        raise RuntimeError('A release-1-1 patch has an unsupported path, mode or binary operation.')
    paths = [source_name(a if original else 'chromium/' + a) for a, _ in diffs]
    if len(paths) != len(set(paths)):
        raise RuntimeError('A release-1-1 patch repeats a source path.')
    return set(paths)


def verify_inputs(root=ROOT, lock=LOCK):
    data = load_lock(lock)
    original_paths = set()
    for row in all_patch_inputs(data):
        path = plain_target(Path(root) / data['patchDirectory'] / row['name'], required=True)
        if record(path) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('A pinned release-1-1 export changed: ' + row['name'])
        paths = patch_paths(path.read_text(encoding='utf-8'), row is not data['adapter'])
        if row is data['adapter']:
            changed = {row['path'] for row in data['files'] if row['before'] != row['after']}
            if paths != changed:
                raise RuntimeError('The release-1-1 adapter leaves its exact changed source scope.')
        else:
            original_paths.update(paths)
    if original_paths != {row['path'] for row in data['files']}:
        raise RuntimeError('The release-1-1 lock omits an original export source path.')
    return data


def source_files(data, phase):
    if phase not in ('before', 'after'):
        raise ValueError('Unsupported source phase.')
    parent = previous.source_files(previous.verify_inputs(), 'after')
    result = dict(parent)
    for row in data['files']:
        old = row['before']
        if row['path'] in parent and (old is None or parent[row['path']] != old['sha256']):
            raise RuntimeError('A release-1-1 preimage disagrees with its immutable predecessor.')
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
        path = plain_target(source / 'src/3rdparty' / row['path'])
        value = row[phase]
        if value is None:
            if path.exists():
                raise RuntimeError('An absent release-1-1 source unexpectedly exists: ' + row['path'])
        elif record(path) != value:
            raise RuntimeError('A release-1-1 source size or digest differs: ' + row['path'])


def identity(data):
    return {'schemaVersion': 1, 'release11Revision': data['release11Revision'],
            'lock': record(LOCK), 'recipe': record(__file__),
            'baselineVendorFollowupIdentity': previous.identity(previous.verify_inputs()),
            'upstream': [{key: row[key] for key in ('commit', 'sha256', 'size')} for row in data['upstream']],
            'adapter': {key: data['adapter'][key] for key in ('name', 'sha256', 'size')},
            'effectiveSourceFiles': len(source_files(data, 'after'))}


def verify_public(proof):
    if not isinstance(proof, dict) or any(proof.get(k) != v for k, v in identity(verify_inputs()).items()):
        raise RuntimeError('The release-1-1 runtime proof is missing or stale.')
    for field in ('outputs', 'priorOutputs'):
        rows = proof.get(field)
        if (not isinstance(rows, list) or not 1 <= len(rows) <= 1024
                or any(not isinstance(row, dict) or set(row) != {'file', 'sha256', 'size'}
                       or not valid_record({key: row.get(key) for key in ('sha256', 'size')}, 512 * 1024 * 1024)
                       for row in rows)):
            raise RuntimeError('A release-1-1 runtime output identity is invalid.')
        base.verify_output_scope(rows)
    core = 'lib/libQt6WebEngineCore.so.6.11.3'
    if next(row for row in proof['outputs'] if row['file'] == core) == next(row for row in proof['priorOutputs'] if row['file'] == core):
        raise RuntimeError('The release-1-1 repairs did not replace the preceding engine core.')
    if not valid_record(proof.get('baselineProof')):
        raise RuntimeError('The immutable vendorFollowup proof identity is missing.')
    return proof


def historical_proofs(prefix):
    old = previous.historical_proofs(prefix)
    path = plain_target(prefix / previous.PROOF, required=True)
    vendorFollowup = previous.verify_public(json.loads(path.read_text(encoding='utf-8')))
    if (vendorFollowup['baselineProof'] != record(prefix / previous.previous.PROOF)
            or vendorFollowup['priorOutputs'] != old['residual']['outputs']):
        raise RuntimeError('The immutable vendorFollowup runtime provenance changed.')
    return {**old, 'vendorFollowup': vendorFollowup}


def verify_build(prefix):
    prefix = plain_target(prefix, directory=True, required=True)
    old = historical_proofs(prefix)
    path = plain_target(prefix / PROOF, required=True)
    proof = verify_public(json.loads(path.read_text(encoding='utf-8')))
    if proof['baselineProof'] != record(prefix / previous.PROOF) or proof['priorOutputs'] != old['vendorFollowup']['outputs']:
        raise RuntimeError('The release-1-1 predecessor proof or outputs changed.')
    for row in proof['outputs']:
        path = plain_target(prefix / row['file'], required=True)
        if record(path) != {key: row[key] for key in ('sha256', 'size')}:
            raise RuntimeError('A release-1-1 runtime output differs: ' + row['file'])
    return {**old, 'release11': proof}


def public_provenance(proof):
    result = previous.public_provenance(proof)
    result['outputs'] = proof['release11']['outputs']
    result['release11'] = {key: proof['release11'][key]
        for key in (*identity(verify_inputs()), 'baselineProof', 'priorOutputs', 'outputs', 'abiFloor')}
    return result


def verify_cache_paths(work, prefix):
    previous.verify_cache_paths(work, prefix)
    for name in ('start.json', 'budget.json', 'build.log', 'process.json', 'install.log'):
        plain_target(work / ('release-1-1-security-' + name))
    plain_target(prefix / PROOF)


def source_phase(work, prefix, data, expected, old, require_observed_baseline=False):
    state = work / 'release-1-1-security-start.json'
    intent = {'identity': expected, 'baselineProof': record(prefix / previous.PROOF),
              'beforeOutputs': old['vendorFollowup']['outputs'], 'observedBaselineRequired': require_observed_baseline}
    if require_observed_baseline and intent['baselineProof'] != data['baselineRuntimeProof']:
        raise RuntimeError('The observed retained predecessor differs before release-1-1 mutation.')
    if state.exists():
        if json.loads(state.read_text(encoding='utf-8')) != intent:
            raise RuntimeError('The retained release-1-1 transaction differs.')
        try:
            verify_source(work / 'source', data, 'after')
            return 'after', intent
        except (RuntimeError, OSError):
            pass
    verify_source(work / 'source', data, 'before')
    verified = previous.verify_build(prefix)
    if verified != old or require_observed_baseline and len(verified['vendorFollowup']['outputs']) != 62:
        raise RuntimeError('The exact retained predecessor runtime must verify before mutation.')
    return 'before', intent


def build(work, prefix, attestation=None, drive=None, check_only=False, require_observed_baseline=False):
    data = verify_inputs()
    expected = identity(data)
    work, prefix = (plain_target(path, directory=True, required=True) for path in (work, prefix))
    if require_observed_baseline and (work != EXPECTED_WORK or prefix != EXPECTED_PREFIX):
        raise RuntimeError('Vendor-followup reuses only the explicitly reviewed retained cache and SDK.')
    storage = check_build_storage((work, prefix), attestation, drive)
    verify_cache_paths(work, prefix)
    resume.verify_configuration(work, prefix)
    verify_install_paths(work, prefix)
    resume.verify_tools(work, resume.load_resume_inputs()[0])
    memory = resume.check_memory()
    old = historical_proofs(prefix)
    if require_observed_baseline and (record(prefix / previous.PROOF) != data['baselineRuntimeProof']
                                     or len(old['vendorFollowup']['outputs']) != 62):
        raise RuntimeError('The observed retained predecessor proof and all62 outputs must match.')
    if (prefix / PROOF).exists():
        return verify_build(prefix)
    phase, intent = source_phase(work, prefix, data, expected, old, require_observed_baseline)
    receipt = {'identity': expected, 'sourcePhase': phase, 'storage': storage,
               'memory': memory, 'runtimeTargets': base.runtime_scope()}
    if check_only:
        print(json.dumps(receipt, indent=2))
        return receipt
    with project_build_lock(storage):
        verify_cache_paths(work, prefix)
        if identity(verify_inputs()) != expected or historical_proofs(prefix) != old:
            raise RuntimeError('Vendor-followup inputs changed before acquiring the build lock.')
        resume.verify_configuration(work, prefix)
        verify_install_paths(work, prefix)
        phase, intent = source_phase(work, prefix, data, expected, old, require_observed_baseline)
        budget = RuntimeBudget((work, prefix), attestation, drive, work / 'release-1-1-security-budget.json')
        budget.check()
        if phase == 'before':
            source = work / 'source/src/3rdparty/chromium'
            adapter = ROOT / data['patchDirectory'] / data['adapter']['name']
            subprocess.run(['git', 'apply', '--check', str(adapter)], cwd=source, check=True)
            verify_source(work / 'source', data, 'before')
            if not (work / 'release-1-1-security-start.json').exists():
                publish(work / 'release-1-1-security-start.json', intent)
            subprocess.run(['git', 'apply', str(adapter)], cwd=source, check=True)
            verify_source(work / 'source', data, 'after')
        env = build_environment(work)
        log, status = work / 'release-1-1-security-build.log', work / 'release-1-1-security-process.json'
        def verify_frozen_inputs():
            verify_source(work / 'source', data, 'after')
            resume.verify_configuration(work, prefix)
            verify_install_paths(work, prefix)
            if identity(verify_inputs()) != expected or historical_proofs(prefix) != old:
                raise RuntimeError('Vendor-followup recipe, source or historical inputs changed during compilation.')
        def run_phase(command, output, name):
            verify_cache_paths(work, prefix)
            verify_frozen_inputs()
            resume.run_phase(command, output, env, status, name, budget, True)
        run_phase(['ninja', '-j', '2', '-C', str(work / 'build/src/core/Release/x86_64'), 'QtWebEngineCore'], log, 'release-1-1-core')
        run_phase(['cmake', '--build', str(work / 'build'), '--target', *base.RUNTIME_TARGETS, '--parallel', '1'], log, 'release-1-1-runtime')
        for script in base.RUNTIME_INSTALL_SCRIPTS:
            run_phase(['cmake', '-DCMAKE_INSTALL_PREFIX=' + str(prefix), '-P', str(work / 'build' / script)],
                      work / 'release-1-1-security-install.log', 'release-1-1-install')
        outputs = base.runtime_outputs(prefix)
        abi = resume.elf_floor([prefix / row['file'] for row in outputs])
        verify_frozen_inputs()
        proof = {**expected, 'baselineProof': record(prefix / previous.PROOF),
                 'priorOutputs': old['vendorFollowup']['outputs'], 'outputs': outputs, 'abiFloor': abi}
        verify_public(proof)
        verify_cache_paths(work, prefix)
        publish(prefix / PROOF, proof)
        result = verify_build(prefix)
        status.write_text(json.dumps({'phase': 'complete', 'proof': record(prefix / PROOF)}, indent=2) + '\n', encoding='utf-8')
        print('Verified the release-1-1 WebEngine security runtime.', flush=True)
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--prefix', required=True, type=Path)
    parser.add_argument('--storage-attestation', type=Path)
    parser.add_argument('--expected-host-drive')
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--require-observed-baseline', action='store_true',
                        help='Private retained-builder guard: exact approved paths and observed predecessor outputs.')
    args = parser.parse_args()
    if args.verify_only:
        verify_build(args.prefix)
        print('Vendor-followup WebEngine runtime proof verified.')
    elif not args.work or sys.platform != 'linux':
        parser.error('Build requires Linux and the retained --work cache.')
    else:
        build(args.work, args.prefix, args.storage_attestation, args.expected_host_drive,
              args.check_only, args.require_observed_baseline)
