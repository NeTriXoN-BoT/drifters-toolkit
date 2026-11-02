"""Offline history validation and generation in disposable OS-temp fixtures."""
from pathlib import Path
import json
import os
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import build_changelog as changelog
from release_layout import app_version

UNSET = object()


class ChangelogTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='drifter-changelog-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'config').mkdir()
        self.version = app_version()
        self.current = {'schemaVersion': 1, 'version': self.version, 'items': ['The current release.']}
        self.history = {'schemaVersion': 1, 'releases': [
            {'version': '0.0.2', 'items': ['A historical release.']},
            {'version': self.version, 'items': ['An archived copy of the current release.']},
            {'version': '0.0.10', 'items': ['A later historical release.']},
        ]}
        self.write_inputs()

    def write_inputs(self):
        (self.root / changelog.HISTORY_NAME).write_bytes(self.encode(self.history))
        (self.root / 'config/release-notes.json').write_bytes(self.encode(self.current))

    @staticmethod
    def encode(value):
        return json.dumps(value, ensure_ascii=False).encode('utf-8')

    @staticmethod
    def records(content):
        text = content.decode('utf-8')
        assert text.startswith(changelog.SCRIPT_PREFIX) and text.endswith(changelog.SCRIPT_SUFFIX)
        return json.loads(text[len(changelog.SCRIPT_PREFIX):-len(changelog.SCRIPT_SUFFIX)])

    def render(self, history=UNSET, current=UNSET, version=UNSET):
        return changelog.render(self.encode(self.history if history is UNSET else history),
                                self.encode(self.current if current is UNSET else current),
                                self.version if version is UNSET else version)

    def test_numeric_order_current_override_and_exact_deterministic_output(self):
        content = self.render()
        records = self.records(content)
        self.assertEqual([record['version'] for record in records], [self.version, '0.0.10', '0.0.2'])
        self.assertEqual(records[0], {'version': self.version, 'items': self.current['items']})
        self.history['releases'].reverse()
        self.assertEqual(self.render(), content)
        self.assertNotIn(b'schemaVersion', content)

    def test_current_release_is_added_and_archived_urls_remain_plain_text(self):
        item = 'Earlier instructions: https://example.invalid/history?a=1&b=2'
        history = {'schemaVersion': 1, 'releases': [{'version': '0.0.1', 'items': [item]}]}
        content = self.render(history)
        self.assertEqual(self.records(content)[1]['items'], [item])
        self.assertIn(b'\\u0026', content)
        self.assertNotIn(b'&', content)
        self.assertEqual(self.records(self.render({'schemaVersion': 1, 'releases': []})),
                         [{'version': self.version, 'items': self.current['items']}])

    def test_external_script_embedding_escapes_active_delimiters(self):
        records = [{'version': '0.0.1', 'items': ['</script><script>bad()</script>&\u2028\u2029']}]
        content = changelog.script_bytes(records)
        for character in ('<', '>', '&', '\u2028', '\u2029'):
            self.assertNotIn(character.encode('utf-8'), content)
        self.assertEqual(self.records(content), records)

    def test_wrong_schema_fields_types_and_item_counts_are_rejected(self):
        invalid = [None, [], True, {}, {**self.history, 'schemaVersion': True},
                   {**self.history, 'schemaVersion': 1.0}, {**self.history, 'schemaVersion': 2},
                   {**self.history, 'releases': {}}, {**self.history, 'private': 'excluded'},
                   {**self.history, 'releases': [None]},
                   {**self.history, 'releases': [{'version': '0.0.1', 'items': ['Change.'], 'private': 'excluded'}]}]
        for items in (None, 'text', [], [True], [1], [{}], ['Change.'] * 101):
            invalid.append({'schemaVersion': 1, 'releases': [{'version': '0.0.1', 'items': items}]})
        for history in invalid:
            with self.subTest(history=history), self.assertRaises(RuntimeError):
                self.render(history)

    def test_duplicate_keys_invalid_utf8_and_nonjson_constants_are_rejected(self):
        invalid = [b'{', b'\xff', b'{"schemaVersion":1,"schemaVersion":1,"releases":[]}',
                   b'{"schemaVersion":1,"releases":[{"version":"0.0.1","version":"0.0.2","items":["x"]}]}',
                   b'{"schemaVersion":1,"releases":NaN}',
                   b'{"schemaVersion":1,"releases":Infinity}',
                   b'{"schemaVersion":1,"releases":[{"version":"0.0.1","items":["\\ud800"]}]}',
                   b'[' * 2000 + b'0' + b']' * 2000]
        for content in invalid:
            with self.subTest(content=content[:90]), self.assertRaises(RuntimeError):
                changelog.render(content, self.encode(self.current), self.version)

    def test_duplicate_noncanonical_and_future_release_versions_are_rejected(self):
        release = {'version': '0.0.1', 'items': ['A change.']}
        with self.assertRaisesRegex(RuntimeError, 'duplicate release'):
            self.render({'schemaVersion': 1, 'releases': [release, release]})
        for version in (None, True, 1, '1.0', 'v1.0.0', '01.0.0', '1.00.0', '1.0.0-beta',
                        '1000000.0.0', '1.0.0\n', '１.0.0', '999999.999999.999999'):
            with self.subTest(version=version), self.assertRaises(RuntimeError):
                self.render({'schemaVersion': 1, 'releases': [{'version': version, 'items': ['Change.']}]})

    def test_oversized_input_utf8_items_and_combined_release_count_are_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'byte limit'):
            changelog.render(b' ' * (changelog.MAX_HISTORY_BYTES + 1), self.encode(self.current), self.version)
        for item in ('x' * 2001, 'é' * 1001):
            with self.subTest(item=item[:12]), self.assertRaises(RuntimeError):
                self.render({'schemaVersion': 1, 'releases': [{'version': '0.0.1', 'items': [item]}]})
        releases = [{'version': f'0.0.{number}', 'items': ['Change.']} for number in range(101)]
        with self.assertRaisesRegex(RuntimeError, 'release count'):
            self.render({'schemaVersion': 1, 'releases': releases})
        with self.assertRaisesRegex(RuntimeError, 'combined changelog'):
            self.render({'schemaVersion': 1, 'releases': releases[:100]})

    def test_plain_text_rejects_markup_injection_controls_and_blank_items(self):
        for item in ('', ' leading', 'trailing ', ' ', '\tchange', 'Two\nlines', '<img src=x onerror=bad()>',
                     '</script><script>bad()</script>', 'nul\0text', 'delete\x7ftext', 'hidden\x85text',
                     'bidi\u202etext', 'hidden\u200btext', 'hidden\u2066text', 'hidden\ufefftext'):
            with self.subTest(item=item), self.assertRaises(RuntimeError):
                self.render({'schemaVersion': 1, 'releases': [{'version': '0.0.1', 'items': [item]}]})

    def test_current_record_keeps_strict_signed_updater_validation(self):
        for current in ({**self.current, 'items': ['x'] * 21}, {**self.current, 'items': ['x' * 401]},
                        {**self.current, 'items': ['https://example.invalid']},
                        {**self.current, 'version': '0.0.0'}, {**self.current, 'history': []}):
            with self.subTest(current=current), self.assertRaises(RuntimeError):
                self.render(current=current)
        duplicate = self.encode(self.current).replace(b'"schemaVersion": 1', b'"schemaVersion": 1,"schemaVersion": 1')
        with self.assertRaises(RuntimeError):
            changelog.render(self.encode(self.history), duplicate, self.version)

    def test_generate_and_verify_use_only_fixture_paths_and_preserve_unchanged_file(self):
        output = self.root / changelog.OUTPUT_NAME
        expected = self.render()
        self.assertEqual(changelog.generate(self.root), {changelog.OUTPUT_NAME: expected})
        self.assertEqual(output.read_bytes(), expected)
        before = output.stat().st_mtime_ns
        changelog.generate(self.root)
        self.assertEqual(output.stat().st_mtime_ns, before)
        changelog.generate(self.root, verify=True)
        self.assertEqual(sorted(path.relative_to(self.root).as_posix() for path in self.root.rglob('*') if path.is_file()),
                         ['config/changelog-history.json', 'config/release-notes.json', 'ui/changelog.js'])

    def test_verify_rejects_missing_or_stale_output_without_writing(self):
        output = self.root / changelog.OUTPUT_NAME
        with self.assertRaises(FileNotFoundError):
            changelog.generate(self.root, verify=True)
        self.assertFalse(output.exists())
        changelog.generate(self.root)
        output.write_bytes(b'window.CHANGELOG_HISTORY = [];\n')
        with self.assertRaisesRegex(RuntimeError, 'Regenerate'):
            changelog.generate(self.root, verify=True)
        self.assertEqual(output.read_bytes(), b'window.CHANGELOG_HISTORY = [];\n')
        changelog.generate(self.root)
        self.current['items'] = ['An edited current release.']
        self.write_inputs()
        with self.assertRaisesRegex(RuntimeError, 'Regenerate'):
            changelog.generate(self.root, verify=True)

    def test_redirected_or_hardlinked_inputs_and_outputs_are_rejected(self):
        outside = self.root / 'sentinel'
        outside.write_bytes(b'preserve me')
        history_path = self.root / changelog.HISTORY_NAME
        history_path.unlink()
        os.link(outside, history_path)
        with self.assertRaises(RuntimeError):
            changelog.generate(self.root)
        self.assertEqual(outside.read_bytes(), b'preserve me')
        history_path.unlink()
        self.write_inputs()
        output = self.root / changelog.OUTPUT_NAME
        output.parent.mkdir()
        os.link(outside, output)
        with self.assertRaises(RuntimeError):
            changelog.generate(self.root)
        self.assertEqual(outside.read_bytes(), b'preserve me')
        output.unlink()
        try:
            output.symlink_to(outside)
        except OSError:
            return  # Hardlink preservation was still checked on hosts without symlink privilege.
        with self.assertRaises(RuntimeError):
            changelog.generate(self.root)
        self.assertEqual(outside.read_bytes(), b'preserve me')


if __name__ == '__main__':
    unittest.main()
