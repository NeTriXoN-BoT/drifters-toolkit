"""Reject incomplete, modified or active artwork before it enters a release."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from build_seasonal_emoji import generate


class SeasonalArtworkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='drifter-seasonal-artwork-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.lock = json.loads((ROOT / 'scripts/dependencies.lock.json').read_text(encoding='utf-8'))
        self.lock = {name: entry for name, entry in self.lock.items() if name.startswith('noto-emoji/')}
        for entry in self.lock.values():
            target = self.root / entry['target']
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((ROOT / entry['target']).read_bytes())
        (self.root / 'scripts').mkdir()
        self.write_lock()

    def write_lock(self):
        (self.root / 'scripts/dependencies.lock.json').write_text(json.dumps(self.lock), encoding='utf-8')

    def first_image(self):
        return next(entry for entry in self.lock.values() if entry.get('seasonalIndex') == 0)

    def replace_image_and_pin(self, content):
        entry = self.first_image()
        (self.root / entry['target']).write_bytes(content)
        entry.update(size=len(content), sha256=hashlib.sha256(content).hexdigest())
        self.write_lock()

    def test_originals_reproduce_exact_shipped_image_and_notices(self):
        for name, content in generate(self.root).items():
            self.assertEqual(content, (ROOT / name).read_bytes())
        generate(self.root, verify=True)

    def test_corrupt_source_and_corrupt_output_are_rejected(self):
        entry = self.first_image()
        original = (self.root / entry['target']).read_bytes()
        (self.root / entry['target']).write_bytes(original + b' ')
        with self.assertRaisesRegex(RuntimeError, 'does not match its pin'):
            generate(self.root)
        (self.root / entry['target']).write_bytes(original)
        generate(self.root)
        (self.root / 'ui/assets/seasonal-emoji.svg').write_bytes(b'<svg/>')
        with self.assertRaisesRegex(RuntimeError, 'Regenerate'):
            generate(self.root, verify=True)

    def test_future_artwork_cannot_introduce_active_or_remote_content(self):
        for element in ('<script>run()</script>', '<image href="https://example.invalid/a.png"/>',
                        '<use href="https://example.invalid/a.svg#x"/>',
                        '<path style="fill:url(https://example.invalid/x)"/>',
                        '<path onload="run()"/>'):
            with self.subTest(element=element):
                self.replace_image_and_pin(('<svg xmlns="http://www.w3.org/2000/svg" '
                                            'viewBox="0 0 128 128">' + element + '</svg>').encode())
                with self.assertRaises(RuntimeError):
                    generate(self.root)

    def test_missing_or_duplicate_cells_are_rejected(self):
        self.first_image()['seasonalIndex'] = 1
        self.write_lock()
        with self.assertRaisesRegex(RuntimeError, 'eighteen distinct'):
            generate(self.root)


if __name__ == '__main__':
    unittest.main()
