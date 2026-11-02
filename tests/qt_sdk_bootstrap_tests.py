"""Offline validation of the pinned native SDK bootstrap inputs and boundaries."""
from pathlib import Path
import copy
import importlib.util
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('bootstrap_qt_sdk', ROOT/'scripts/bootstrap_qt_sdk.py')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class SdkInputs(unittest.TestCase):
    def test_committed_lock(self):
        data = sdk.load_lock(ROOT/'scripts/qt_sdk_lock.json')
        self.assertEqual(data['qtVersion'], '6.11.3')
        self.assertEqual(len(data['archives']), 11)
        self.assertEqual([row['extractSubdir'] for row in data['archives'] if 'icu-linux-' in row['name']], ['lib'])
        self.assertTrue(any('qtwebengine-' in row['name'] for row in data['archives']))
        # The WebP image plugin (libqwebp.so) comes from the Image Formats module.
        self.assertTrue(any('qtimageformats-' in row['name'] for row in data['archives']))

    def test_member_names(self):
        self.assertEqual(str(sdk.relative_member('lib/libQt6Core.so.6')), 'lib/libQt6Core.so.6')
        for name in ('../escape', '/tmp/escape', 'lib/../escape', 'lib\\escape', 'C:/escape', 'lib//escape', './escape', 'lib/./escape', ''):
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                sdk.relative_member(name)

    def test_transport(self):
        sdk.https_url('https://download.qt.io/online/archive.7z', original=True)
        sdk.https_url('https://mirror.example/qt/archive.7z')
        for url in ('http://download.qt.io/a', 'https://user:secret@download.qt.io/a', 'https://download.qt.io:444/a', 'https://download.qt.io/a#x'):
            with self.subTest(url=url), self.assertRaises(RuntimeError):
                sdk.https_url(url)
        with self.assertRaises(RuntimeError):
            sdk.https_url('https://mirror.example/a', original=True)

    def test_invalid_locks(self):
        original = sdk.load_lock(ROOT/'scripts/qt_sdk_lock.json')
        cases = []
        value = copy.deepcopy(original);value['archives'].append(value['archives'][0]);cases.append(value)
        value = copy.deepcopy(original);value['archives'][0]['sha256'] = 'bad';cases.append(value)
        value = copy.deepcopy(original);value['archives'][0]['size'] = True;cases.append(value)
        value = copy.deepcopy(original);value['archives'][0]['extractSubdir'] = '../outside';cases.append(value)
        value = copy.deepcopy(original);value['archives'][0]['url'] += '?unsigned=1';cases.append(value)
        value = copy.deepcopy(original);value['archives'][0]['name'] = '../outside.7z';cases.append(value)
        with tempfile.TemporaryDirectory(prefix='qt-lock-tests-') as temporary:
            path = Path(temporary)/'lock.json'
            for number, value in enumerate(cases):
                path.write_text(json.dumps(value), encoding='utf-8')
                with self.subTest(number=number), self.assertRaises(RuntimeError):
                    sdk.load_lock(path)

    def test_offline_cache_preserves_mismatch(self):
        with tempfile.TemporaryDirectory(prefix='qt-cache-tests-') as temporary:
            cache = Path(temporary)
            path = cache/'archive.7z';path.write_bytes(b'wrong')
            row = {'name': path.name, 'size': 5, 'sha256': '0'*64}
            with self.assertRaises(RuntimeError):
                sdk.fetch(row, cache, True)
            self.assertEqual(path.read_bytes(), b'wrong')
            with self.assertRaises(RuntimeError):
                sdk.fetch({'name':'missing.7z'}, cache, True)


if __name__ == '__main__':
    unittest.main()
