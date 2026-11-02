"""Offline SaveCodec package/source boundaries in disposable OS-temp fixtures."""
from pathlib import Path
import hashlib
import json
import os
import runpy
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import save_codec_materials as codec
import release_layout as layout
import package_check as checker
import package as windows
import package_portable_linux as portable
import portable_materials as materials
import review_sources


class CodecPackaging(unittest.TestCase):
    def test_linux_stage_cli_forwards_matching_codec_source(self):
        names = ('build', 'AppDir', 'qt-prefix', 'qt-provenance', 'qt-archives',
                 'qt-notices', 'openssl-prefix', 'openssl-provenance', 'openssl-archive')
        paths = [self.root / name for name in names]
        argv = ['package_portable_linux.py', 'stage', str(paths[0]), str(paths[1])]
        for name, path in zip(names[2:], paths[2:]):
            argv += ['--' + name, str(path)]
        argv += ['--save-codec-source', str(self.source), '--destination', str(self.root / 'output')]
        with patch.object(sys, 'argv', argv), patch.object(portable, 'stage') as stage:
            portable.main()
        stage.assert_called_once_with(*paths, self.source)

    def setUp(self):
        self.owner = tempfile.TemporaryDirectory(prefix='drifter-codec-package-test-')
        self.root = Path(self.owner.name)
        self.source = self.root / 'source'; self.source.mkdir()
        pe = bytearray(128); pe[:2] = b'MZ'; pe[60:64] = (80).to_bytes(4, 'little'); pe[80:86] = b'PE\0\0\x64\x86'
        elf = bytearray(64); elf[:7] = b'\x7fELF\x02\x01\x01'; elf[18:20] = b'\x3e\0'; elf[32:40] = (64).to_bytes(8, 'little'); elf[54:56] = (56).to_bytes(2, 'little')
        self.bodies = {'windows': bytes(pe), 'linux': bytes(elf)}
        self.policy = codec.lock()
        for platform, body in self.bodies.items():
            self.policy['targets'][platform].update(sha256=hashlib.sha256(body).hexdigest(), size=len(body))
        self.files = {'.cargo/config.toml': b'[source.crates-io]\nreplace-with="vendored"\n', 'build.py': b'print("offline rebuild")\n',
            'BUILDING.txt': b'Run python build.py offline.\n', 'Cargo.toml': b'[package]\nname="worker"\n', 'Cargo.lock': b'version = 4\n',
            'COPYING': b'GNU GENERAL PUBLIC LICENSE\nGPL-3.0-or-later\n', 'NOTICE.txt': b'Worker copyright and MIT notices.\n', 'src/main.rs': b'fn main() {}\n',
            'vendor/crate/LICENSE': b'MIT licence text\n', 'vendor/crate/README.md': b'Unmodified upstream build documentation.\n'}
        for name, body in self.files.items():
            target = self.source / name; target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(body)
        self.reseal()
        native = self.root / 'project/src'; native.mkdir(parents=True)
        self.header = native / 'save_codec_pin.hpp'
        self.header.write_text('#pragma once\n#ifdef _WIN32\n' + self.branch('windows') + '\n#else\n' + self.branch('linux') + '\n#endif\n')
        self.lock_patch = patch.object(codec, 'lock', return_value=self.policy); self.lock_patch.start()
        self.root_patch = patch.object(codec, 'ROOT', self.root / 'project'); self.root_patch.start()
        manifest = codec.source_manifest(self.manifest)
        rows = codec.notices(self.source, manifest)
        records = [{'path': 'licenses/' + name, 'sha256': hashlib.sha256(body).hexdigest(), 'size': len(body)} for name, body in sorted(rows.items())]
        self.policy['noticeInventorySha256'] = hashlib.sha256(json.dumps(records, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

    def branch(self, platform):
        row = self.policy['targets'][platform]
        return 'inline constexpr const char* SaveCodecFileName = ' + json.dumps(row['file']) + ';\ninline constexpr const char* SaveCodecSha256 = ' + json.dumps(row['sha256']) + ';'

    def reseal(self):
        self.manifest = (json.dumps({'schemaVersion': 1, 'component': self.policy['component'], 'version': self.policy['version'], 'licence': self.policy['licence'],
            'targets': {row['triple']: {'sha256': row['sha256'], 'bytes': row['size']} for row in self.policy['targets'].values()},
            'files': [{'path': name, 'bytes': len(body), 'sha256': hashlib.sha256(body).hexdigest()} for name, body in sorted(self.files.items())]}, indent=2) + '\n').encode()
        self.policy['sourceManifestSha256'] = hashlib.sha256(self.manifest).hexdigest()
        (self.source / codec.SOURCE_MANIFEST).write_bytes(self.manifest)

    def tearDown(self):
        self.root_patch.stop(); self.lock_patch.stop(); self.owner.cleanup()

    def worker(self, platform):
        path = self.root / self.policy['targets'][platform]['file']; path.write_bytes(self.bodies[platform]); return path

    def test_worker_pin_platform_name_and_native_pin_are_one_boundary(self):
        for platform in ('windows', 'linux'):
            with self.subTest(platform=platform):
                worker = self.worker(platform)
                self.assertEqual(codec.verify_worker(worker, platform), self.bodies[platform])
                worker.write_bytes(self.bodies[platform] + b'changed')
                with self.assertRaises(RuntimeError): codec.verify_worker(worker, platform)
        wrong = self.root / 'renamed.exe'; wrong.write_bytes(self.bodies['windows'])
        with self.assertRaises(RuntimeError): codec.verify_worker(wrong, 'windows')
        self.header.write_text(self.header.read_text().replace(self.policy['targets']['windows']['sha256'], '0' * 64))
        with self.assertRaises(RuntimeError): codec.verify_worker(self.worker('windows'), 'windows')

    def test_complete_source_manifest_cannot_hide_missing_modified_or_extra_notes(self):
        self.assertEqual(len(codec.verify_source(self.source)['files']), len(self.files))
        self.assertEqual(set(codec.collect_source(self.source)), set(self.files) | {codec.SOURCE_MANIFEST})
        extra = self.source / 'README_FOR_CLAUDE.md'; extra.write_bytes(b'private handoff')
        with self.assertRaises(RuntimeError): codec.verify_source(self.source)
        extra.unlink()
        selected = self.source / 'src/main.rs'; original = selected.read_bytes(); selected.write_bytes(b'changed source')
        with self.assertRaises(RuntimeError): codec.verify_source(self.source)
        selected.write_bytes(original); (self.source / 'COPYING').unlink()
        with self.assertRaises(RuntimeError): codec.verify_source(self.source)

    def test_source_aliases_and_standalone_archive_inventory_are_rejected(self):
        source = self.source / 'src/main.rs'; alias = self.root / 'source-alias'; os.link(source, alias)
        with self.assertRaises(RuntimeError): codec.verify_source(self.source)
        alias.unlink(); contents = codec.collect_source(self.source)
        self.assertEqual(codec.verify_source_members(contents)['component'], 'DriftersToolkitSaveCodec')
        altered = dict(contents); altered['src/main.rs'] += b'bad'
        with self.assertRaises(RuntimeError): codec.verify_source_members(altered)
        altered = dict(contents); altered['account.dpapi'] = b'secret'
        with self.assertRaises(RuntimeError): codec.verify_source_members(altered)

    def test_pointer_is_exact_version_matched_and_contains_no_local_source_path(self):
        body = codec.pointer_bytes(); value = codec.verify_pointer(body)
        self.assertEqual(value['sourceArchive']['file'], self.policy['sourceArchive'])
        self.assertNotIn(str(self.source).encode(), body)
        value['version'] = '999.0.0'
        with self.assertRaises(RuntimeError): codec.verify_pointer(json.dumps(value).encode())
        with self.assertRaises(RuntimeError): codec.verify_pointer(body.replace(b'"schemaVersion": 1', b'"schemaVersion": true'))
        with self.assertRaises(RuntimeError): codec.installed_files({codec.POINTER_NAME: body}, 'windows')
        with self.assertRaises(RuntimeError): codec.installed_files({'DriftersToolkitSaveCodec.exe': self.bodies['windows']}, 'windows')

    def test_notices_are_complete_and_unchanged_linux_provenance(self):
        proof, notices = codec.provenance(self.source, 'linux')
        files = {'licenses/' + name: body for name, body in notices.items()}
        self.assertEqual(codec.verify_provenance(proof, files, 'linux'), set(files))
        self.assertIn('licenses/save-codec/vendor/crate/LICENSE', files)
        self.assertNotIn('licenses/save-codec/vendor/crate/README.md', files)
        proof['noticeFiles'] = [row for row in proof['noticeFiles'] if row['path'] != 'licenses/save-codec/vendor/crate/LICENSE']
        with self.assertRaises(RuntimeError): codec.verify_provenance(proof, files, 'linux')

    def test_linux_static_worker_is_copied_without_patchelf_or_sdk_dependency(self):
        build = self.root / 'build'; build.mkdir(); worker = build / self.policy['targets']['linux']['file']; worker.write_bytes(self.bodies['linux'])
        with self.assertRaises(RuntimeError): portable.codec_inputs(build, None)
        data = portable.codec_inputs(build, self.source); appdir = self.root / 'AppDir'
        with patch.object(portable, 'run', side_effect=AssertionError('A static codec must never be patchelf-adjusted')), patch.object(portable, 'copy', wraps=portable.copy) as copier:
            proof = portable.install_codec(appdir, data)
        target = appdir / 'usr/bin' / worker.name
        self.assertEqual(target.read_bytes(), self.bodies['linux']); copier.assert_called_once_with(worker, target, True)
        if os.name == 'posix': self.assertTrue(target.stat().st_mode & 0o111)
        self.assertEqual(proof['binary']['sha256'], hashlib.sha256(target.read_bytes()).hexdigest())
        self.assertEqual(codec.verify_pointer((appdir / 'usr/bin' / codec.POINTER_NAME).read_bytes()), codec.source_pointer())

    def test_flattened_linux_corresponding_source_requires_every_pinned_file(self):
        contents = codec.collect_source(self.source)
        observed = {'save-codec/' + name: {'sha256': hashlib.sha256(body).hexdigest(), 'size': len(body)} for name, body in contents.items()}
        observed['save-codec/' + codec.SOURCE_MANIFEST]['sourceManifest'] = self.manifest
        self.assertEqual(codec.verify_source_records(observed)['version'], self.policy['version'])
        del observed['save-codec/vendor/crate/LICENSE']
        with self.assertRaises(RuntimeError): codec.verify_source_records(observed)

    def test_windows_layout_requires_worker_pointer_and_gpl_source_notice_together(self):
        contents = {name: b'fixture' for name in layout.WINDOWS_RUNTIME}
        contents['config/nexus.json'] = b'{"applicationSlug":""}'
        contents['DriftersToolkitSaveCodec.exe'] = self.bodies['windows']; contents[codec.POINTER_NAME] = codec.pointer_bytes()
        contents['LICENSES.txt'] = ('DriftersToolkitSaveCodec\nGNU GENERAL PUBLIC LICENSE\n' + self.policy['sourceArchive']).encode()
        checker.check_archive(Path('fixture.zip'), content=layout.zip_bytes('DriftersToolkit', contents), require_sidecar=False)
        del contents[codec.POINTER_NAME]
        with self.assertRaises(RuntimeError): checker.check_archive(Path('fixture.zip'), content=layout.zip_bytes('DriftersToolkit', contents), require_sidecar=False)

    def test_source_archive_is_explicit_and_never_accepted_as_runtime(self):
        contents = codec.collect_source(self.source)
        body = layout.zip_bytes(codec.SOURCE_ROOT_NAME, contents)
        checker.check_archive(Path('source.zip'), source=True, content=body, require_sidecar=False)
        with self.assertRaises(RuntimeError): checker.check_archive(Path('runtime.zip'), content=body, require_sidecar=False)

    def test_required_vendored_windows_ffi_is_not_misclassified_as_user_credentials(self):
        name = 'vendor/windows-sys-0.61.2/src/Windows/Win32/Security/Credentials/mod.rs'
        self.files[name] = b'// Unmodified vendored Win32 FFI declarations.\n'
        path = self.source / name; path.parent.mkdir(parents=True); path.write_bytes(self.files[name]); self.reseal()
        contents = codec.collect_source(self.source)
        checker.check_archive(Path('source.zip'), source=True, content=layout.zip_bytes(codec.SOURCE_ROOT_NAME, contents), require_sidecar=False)
        public_name = codec.SOURCE_ROOT_NAME + '/' + name
        for public in (False, True):
            with self.assertRaises(RuntimeError): checker.Inspection(public=public).name(public_name)
        for bad in ('credentials/account.json', 'vendor/other/Credentials/mod.rs', name + '.dpapi'):
            with self.assertRaises(RuntimeError): checker.Inspection(public=False,pinned_codec_source=True).name(codec.SOURCE_ROOT_NAME + '/' + bad)
        contents[name] = b'changed FFI or private data'
        with self.assertRaises(RuntimeError): checker.check_archive(Path('source.zip'), source=True, content=layout.zip_bytes(codec.SOURCE_ROOT_NAME, contents), require_sidecar=False)

    def test_asset_generator_pins_exact_worker_and_separate_helper_constant(self):
        helper = self.worker('windows'); output = self.root / 'generated.hpp'
        original_read = layout.read_plain
        def fixture_loader(path, *args, **kwargs):
            if Path(path) == ROOT / 'vendor/WebView2Loader.dll':
                return b'synthetic Windows loader fixture'
            return original_read(path, *args, **kwargs)
        with patch('sys.argv', ['generate_assets_manifest.py', '--platform', 'windows', '--save-codec-helper', str(helper), '--output', str(output)]), \
                patch('build_seasonal_emoji.generate'), patch('build_changelog.generate'), \
                patch.object(layout, 'read_plain', side_effect=fixture_loader):
            runpy.run_path(str(ROOT / 'scripts/generate_assets_manifest.py'), run_name='__main__')
        content = output.read_text()
        self.assertIn('DriftersToolkitSaveCodec.exe', content)
        self.assertIn('SaveCodecHelperSha256 = "' + self.policy['targets']['windows']['sha256'] + '"', content)

    def test_collection_rechecks_captured_source_bytes_after_initial_inventory(self):
        original = codec.read_plain; reads = 0
        def raced(path, *args, **kwargs):
            nonlocal reads
            body = original(path, *args, **kwargs)
            if Path(path) == self.source / 'src/main.rs':
                reads += 1
                if reads == 2: return b'changed between verification and source collection'
            return body
        with patch.object(codec, 'read_plain', side_effect=raced), self.assertRaises(RuntimeError): codec.collect_source(self.source)

    def test_public_packaging_requires_actual_source_closure_before_any_archive(self):
        build = self.root / 'build'; build.mkdir()
        for name in layout.WINDOWS_RUNTIME:
            path = build / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'fixture')
        (build / 'config/nexus.json').write_bytes(b'{"applicationSlug":""}')
        (build / 'DriftersToolkitSaveCodec.exe').write_bytes(self.bodies['windows'])
        destination = self.root / 'packages'
        with patch.object(windows, 'ROOT', self.root), self.assertRaisesRegex(RuntimeError, 'complete matching SaveCodec source'):
            windows.package(build, destination)
        self.assertFalse(destination.exists())

    def test_dynamic_elf_interpreter_is_not_accepted_as_static_worker(self):
        body = bytearray(self.bodies['linux']); body[56:58] = (1).to_bytes(2, 'little'); body.extend((3).to_bytes(4, 'little') + bytes(52))
        body = bytes(body); self.policy['targets']['linux'].update(sha256=hashlib.sha256(body).hexdigest(), size=len(body))
        self.header.write_text('#ifdef _WIN32\n' + self.branch('windows') + '\n#else\n' + self.branch('linux') + '\n#endif\n')
        with self.assertRaisesRegex(RuntimeError, 'ELF interpreter'): codec.verify_binary(body, 'linux')

    def test_app_source_collector_keeps_separate_helper_sources_and_private_notes_out(self):
        for name in ('src/save_preview_decoder/native.cpp', 'src/save_codec_decoder/native.cpp',
                     'src/save_preview_decoder/src/main.rs', 'README_FOR_CLAUDE.md', 'docs/SAVE_PREVIEW_SOURCE_PLAN.md'):
            self.assertFalse(review_sources.allowed(name), name)
        self.assertTrue(review_sources.allowed('src/save_preview_process.cpp'))
        self.assertTrue(review_sources.allowed('src/save_format.cpp'))
        self.assertTrue(review_sources.allowed('scripts/save_codec_materials.py'))
        self.assertTrue(review_sources.allowed('scripts/save_codec_lock.json'))

    def test_private_review_envelope_requires_complete_separate_codec_source(self):
        # The exception is only for the established private submission envelope.
        # Public runtime packages retain the blanket nested-archive refusal.
        sys.path.insert(0, str(ROOT / 'tests'))
        from packaging_security_tests import PackagingSecurity
        linux, executables = PackagingSecurity().appimage(True)
        runtime = {name: b'fixture' for name in layout.WINDOWS_RUNTIME}
        runtime['config/nexus.json'] = b'{"applicationSlug":""}'
        runtime.update({'DriftersToolkitSaveCodec.exe': self.bodies['windows'], codec.POINTER_NAME: codec.pointer_bytes(),
            'LICENSES.txt': ('DriftersToolkitSaveCodec\nGNU GENERAL PUBLIC LICENSE\n' + self.policy['sourceArchive']).encode()})
        content = {'Drifters-Toolkit-NexusReview-Windows.zip': layout.zip_bytes('DriftersToolkit', runtime),
            'Drifters-Toolkit-NexusReview-Linux.zip': layout.zip_bytes('DriftersToolkit-Linux', linux, executables),
            'Drifters-Toolkit-Review-Source.zip': layout.zip_bytes('DriftersToolkit-review-source', {'CMakeLists.txt': b'fixture build'}),
            self.policy['sourceArchive']: layout.zip_bytes(codec.SOURCE_ROOT_NAME, codec.collect_source(self.source))}
        with patch.object(review_sources, 'FIXED', {'CMakeLists.txt'}):
            checker.check_archive(Path('review.zip'), content=layout.zip_bytes('DriftersToolkit-NexusReview', content), require_sidecar=False)
            del content[self.policy['sourceArchive']]
            with self.assertRaisesRegex(RuntimeError, 'matching SaveCodec source ZIP'):
                checker.check_archive(Path('review.zip'), content=layout.zip_bytes('DriftersToolkit-NexusReview', content), require_sidecar=False)


if __name__ == '__main__': unittest.main(verbosity=2)
