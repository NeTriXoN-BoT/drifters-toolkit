"""Build a bounded, deterministic offline changelog from public source records."""
from pathlib import Path
import argparse
import json
import re

from release_layout import (MAX_RELEASE_NOTES_BYTES, app_version, read_plain,
                            release_notes, release_notes_bytes, write_artifact)

ROOT = Path(__file__).resolve().parents[1]
HISTORY_NAME = 'config/changelog-history.json'
OUTPUT_NAME = 'ui/changelog.js'
MAX_HISTORY_BYTES = 512 * 1024
MAX_RELEASES = 100
MAX_ITEMS = 100
MAX_ITEM_BYTES = 2000
# JSON escaping can expand each source character into six ASCII bytes.
MAX_SCRIPT_BYTES = 6 * (MAX_HISTORY_BYTES + MAX_RELEASE_NOTES_BYTES) + 1024
SCRIPT_PREFIX = 'window.CHANGELOG_HISTORY = '
SCRIPT_SUFFIX = ';\nwindow.WHATS_NEW = window.CHANGELOG_HISTORY[0] || null;\n'
VERSION = re.compile(r'(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})')


def version_key(version):
    if not isinstance(version, str) or not VERSION.fullmatch(version):
        raise RuntimeError('A changelog version must be a canonical three-part version.')
    return tuple(int(part) for part in version.split('.'))


def _unique_fields(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise RuntimeError('The changelog contains a duplicate JSON field.')
        result[key] = value
    return result


def _invalid_constant(value):
    raise RuntimeError('The changelog contains a non-JSON numeric constant.')


def _plain_item(item):
    if not isinstance(item, str):
        raise RuntimeError('Every changelog item must be plain text.')
    try:
        size = len(item.encode('utf-8'))
    except UnicodeError as error:
        raise RuntimeError('A changelog item is not valid UTF-8 text.') from error
    if (not 1 <= size <= MAX_ITEM_BYTES or item != item.strip()
            or any(ord(char) < 32 or 127 <= ord(char) <= 159 or char in '<>'
                   or 0x200b <= ord(char) <= 0x200f or 0x202a <= ord(char) <= 0x202e
                   or 0x2060 <= ord(char) <= 0x206f or ord(char) == 0xfeff for char in item)):
        raise RuntimeError('A changelog item contains markup, controls or oversized text.')
    return item


def history_records(content, current_version):
    """Read archived plain-text records; ordinary historical URLs stay as text."""
    if not isinstance(content, bytes) or len(content) > MAX_HISTORY_BYTES:
        raise RuntimeError('The changelog history exceeds its safe byte limit.')
    try:
        value = json.loads(content.decode('utf-8'), object_pairs_hook=_unique_fields,
                           parse_constant=_invalid_constant)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise RuntimeError('The changelog history is not valid bounded JSON.') from error
    if (not isinstance(value, dict) or set(value) != {'schemaVersion', 'releases'}
            or type(value['schemaVersion']) is not int or value['schemaVersion'] != 1
            or not isinstance(value['releases'], list) or len(value['releases']) > MAX_RELEASES):
        raise RuntimeError('The changelog history has the wrong fields or release count.')
    current = version_key(current_version)
    records = []
    seen = set()
    for record in value['releases']:
        if (not isinstance(record, dict) or set(record) != {'version', 'items'}
                or not isinstance(record['items'], list) or not 1 <= len(record['items']) <= MAX_ITEMS):
            raise RuntimeError('A changelog release has the wrong fields or item count.')
        key = version_key(record['version'])
        if key in seen:
            raise RuntimeError('The changelog contains a duplicate release version.')
        if key > current:
            raise RuntimeError('The changelog contains a version newer than the current release.')
        seen.add(key)
        records.append({'version': record['version'], 'items': [_plain_item(item) for item in record['items']]})
    return records


def script_bytes(records):
    """Use JSON as data in an external script, including safe HTML embedding."""
    body = json.dumps(records, ensure_ascii=False, indent=2)
    for character, escaped in (('<', '\\u003c'), ('>', '\\u003e'), ('&', '\\u0026'),
                               ('\u2028', '\\u2028'), ('\u2029', '\\u2029')):
        body = body.replace(character, escaped)
    return (SCRIPT_PREFIX + body + SCRIPT_SUFFIX).encode('utf-8')


def render(history_content, current_content, current_version):
    records = history_records(history_content, current_version)
    # The updater's signed release-note schema stays the sole current-record validator.
    current = release_notes(current_content, current_version)
    by_version = {record['version']: record for record in records}
    by_version[current_version] = {'version': current_version, 'items': current['items']}
    if len(by_version) > MAX_RELEASES:
        raise RuntimeError('The combined changelog exceeds the safe release count.')
    return script_bytes(sorted(by_version.values(), key=lambda record: version_key(record['version']), reverse=True))


def generate(root=ROOT, verify=False):
    root = Path(root).absolute()
    # Production always goes through the same explicit source record as the packager.
    # Alternate roots support disposable fixtures and clean source-tree verification.
    current = (release_notes_bytes() if root == ROOT else
               read_plain(root / 'config/release-notes.json', MAX_RELEASE_NOTES_BYTES))
    content = render(read_plain(root / HISTORY_NAME, MAX_HISTORY_BYTES), current, app_version())
    target = root / OUTPUT_NAME
    if verify:
        if read_plain(target, MAX_SCRIPT_BYTES) != content:
            raise RuntimeError('Regenerate the offline changelog: ' + OUTPUT_NAME)
    elif not target.exists() or read_plain(target, MAX_SCRIPT_BYTES) != content:
        write_artifact(target, content, maximum=MAX_SCRIPT_BYTES)
    return {OUTPUT_NAME: content}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    generate(verify=args.verify)
    print('Verified offline changelog.' if args.verify else 'Built offline changelog.')
