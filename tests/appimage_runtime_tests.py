"""Offline policy tests for the native AppImage runtime build/source materials."""
from pathlib import Path
import copy
import hashlib
import io
import json
import os
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build_appimage_runtime as runtime
import build_appimage_runtime_inside as inside


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class Opener:
    def __init__(self, data, callback=None):
        self.data, self.callback = data, callback
        self.request = None

    def open(self, request, **_):
        self.request = request
        if self.callback:
            self.callback()
        return Response(self.data)


def source_row(data=b'valid'):
    return {'component': 'fixture', 'file': 'fixture.tar.gz',
            'url': 'https://github.com/AppImage/type2-runtime/archive/fixed.tar.gz',
            'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}


class RuntimePolicyTests(unittest.TestCase):
    def test_urls_refuse_credentials_ports_local_hosts_and_unsafe_redirects(self):
        runtime.require_url('https://github.com/AppImage/type2-runtime/archive/fixed.tar.gz')
        for value in ['http://github.com/file', 'https://github.com:444/file',
                      'https://github.com.evil.example/file', 'https://user@github.com/file',
                      'https://127.0.0.1/file', 'file:///etc/passwd',
                      'https://github.com/file#fragment', 'https://github.com/file\n']:
            with self.subTest(value=value), self.assertRaises(RuntimeError):
                runtime.require_url(value)
        with self.assertRaises(RuntimeError):
            runtime.Redirects().redirect_request(None, None, 302, '', {}, 'https://localhost/file')

    def test_material_names_cannot_escape_or_alias(self):
        self.assertEqual(runtime.relative('sources/lib/a.patch'), 'sources/lib/a.patch')
        for name in ['../a', '/a', 'a/../b', 'a//b', './a', 'a\\b', 'C:/x', 'a\n']:
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                runtime.relative(name)

    def test_download_has_no_auth_and_does_not_replace_existing_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'input'
            opener = Opener(b'valid')
            runtime.fetch(source_row(), target, opener)
            self.assertEqual(target.read_bytes(), b'valid')
            self.assertFalse(any(name.lower() in {'authorization', 'apikey', 'cookie'}
                                 for name in opener.request.headers))
            with self.assertRaises(RuntimeError):
                runtime.fetch(source_row(b'other'), target, Opener(b'other'))
            self.assertEqual(target.read_bytes(), b'valid')
            self.assertEqual(len(list(Path(temporary).iterdir())), 1)

    def test_failed_and_raced_downloads_clean_only_their_own_partial(self):
        for data in [b'wrong', b'longer-than-expected', b'']:
            with tempfile.TemporaryDirectory() as temporary:
                target = Path(temporary) / 'input'
                with self.assertRaises(RuntimeError):
                    runtime.fetch(source_row(), target, Opener(data))
                self.assertEqual(list(Path(temporary).iterdir()), [])
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'input'
            def raced():
                target.write_bytes(b'keep raced destination')
            with self.assertRaises(FileExistsError):
                runtime.fetch(source_row(), target, Opener(b'valid', raced))
            self.assertEqual(target.read_bytes(), b'keep raced destination')
            self.assertEqual(len(list(Path(temporary).iterdir())), 1)

    def test_hardlinked_cache_is_refused_without_changing_either_name(self):
        with tempfile.TemporaryDirectory() as temporary:
            original = Path(temporary) / 'original'
            original.write_bytes(b'valid')
            alias = Path(temporary) / 'alias'
            os.link(original, alias)
            with self.assertRaises(RuntimeError):
                runtime.fetch(source_row(), alias, Opener(b'valid'))
            self.assertEqual(original.read_bytes(), b'valid')
            self.assertEqual(alias.read_bytes(), b'valid')

    def test_source_deadline_removes_its_partial(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'input'
            with mock.patch.object(runtime.time, 'monotonic', side_effect=[0, 301]):
                with self.assertRaises(RuntimeError):
                    runtime.fetch(source_row(), target, Opener(b'valid'))
            self.assertEqual(list(Path(temporary).iterdir()), [])

    def test_docker_option_injection_is_refused_before_creating_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'output'
            with self.assertRaises(RuntimeError):
                runtime.build(output, image='--privileged')
            self.assertFalse(output.exists())
            with self.assertRaises(RuntimeError):
                runtime.build(Path(temporary) / 'comma,readonly=false')
            self.assertEqual(list(Path(temporary).iterdir()), [])

    def test_real_lock_includes_exact_patched_native_library_sources(self):
        lock = runtime.load_lock()
        self.assertEqual(len(lock['sources']), 65)
        components = {row['component'] for row in lock['sources']}
        self.assertTrue({'runtime', 'fuse', 'squashfuse', 'zstd', 'zlib', 'mimalloc',
                         'musl', 'gcc', 'alpine-musl', 'alpine-gcc'} <= components)
        names = {row['file'] for row in lock['sources']}
        self.assertIn('alpine-recipes/musl/CVE-2026-40200.patch', names)
        self.assertIn('alpine-recipes/musl/CVE-2025-26519.patch', names)
        self.assertIn('alpine-recipes/gcc/APKBUILD', names)
        self.assertIn('FROM ' + lock['baseImage'], (runtime.ROOT / 'Dockerfile.appimage-runtime').read_text())
        for mutate in ['duplicate', 'http', 'oversize']:
            altered = copy.deepcopy(lock)
            if mutate == 'duplicate':
                altered['sources'].append(altered['sources'][0])
            elif mutate == 'http':
                altered['sources'][0]['url'] = 'http://github.com/file'
            else:
                altered['sources'][0]['size'] = runtime.MAX_INPUT + 1
            with tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / 'lock.json'
                path.write_text(json.dumps(altered))
                with self.assertRaises(RuntimeError):
                    runtime.load_lock(path)

    def archive(self, destination, members):
        with tarfile.open(destination, 'w') as archive:
            for name, kind, value in members:
                info = tarfile.TarInfo(name)
                if kind == 'file':
                    info.size = len(value)
                    archive.addfile(info, io.BytesIO(value))
                else:
                    info.type = kind
                    info.linkname = value
                    archive.addfile(info)

    def test_source_extraction_refuses_escape_links_devices_and_duplicates(self):
        fixtures = [
            [('root/../escape', 'file', b'bad')],
            [('/absolute', 'file', b'bad')],
            [('other/file', 'file', b'bad')],
            [('root/a', 'file', b'a'), ('root/a', 'file', b'b')],
            [('root/device', tarfile.CHRTYPE, '')],
            [('root/link', tarfile.SYMTYPE, '../../escape')],
            [('root/link', tarfile.LNKTYPE, '../escape')],
        ]
        for fixture in fixtures:
            with tempfile.TemporaryDirectory() as temporary:
                temporary = Path(temporary)
                source = temporary / 'source.tar'
                self.archive(source, fixture)
                with self.assertRaises((RuntimeError, tarfile.FilterError)):
                    inside.extract(source, temporary / 'stage', 'root')
                self.assertFalse((temporary / 'escape').exists())
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            self.archive(temporary / 'source.tar', [('root/file', 'file', b'safe')])
            result = inside.extract(temporary / 'source.tar', temporary / 'stage', 'root')
            self.assertEqual((result / 'file').read_bytes(), b'safe')

    def test_source_patch_fails_closed_if_the_expected_upstream_changes(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'runtime.c'
            path.write_text('unrecognised source')
            for patch in [inside.patch_fuse, inside.patch_runtime]:
                with self.assertRaises(RuntimeError):
                    patch(path)
                self.assertEqual(path.read_text(), 'unrecognised source')


if __name__ == '__main__':
    unittest.main(verbosity=2)
