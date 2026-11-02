"""Build the local seasonal sprite from pinned Noto SVG originals; no runtime fetches."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import xml.etree.ElementTree as ET

from release_layout import read_plain, write_artifact

ROOT = Path(__file__).resolve().parents[1]
SVG = 'http://www.w3.org/2000/svg'
XLINK = 'http://www.w3.org/1999/xlink'
ET.register_namespace('', SVG)
ET.register_namespace('xlink', XLINK)


def verified(entry, root):
    name = entry['target']
    path = root / name
    if not path.resolve().is_relative_to(root.resolve()):
        raise RuntimeError('An emoji source is outside the source tree.')
    content = read_plain(path, 1024 * 1024)
    if len(content) != entry['size'] or hashlib.sha256(content).hexdigest() != entry['sha256']:
        raise RuntimeError('An emoji source does not match its pin: ' + name)
    return content


def sprite_bytes(root=ROOT):
    lock = json.loads(read_plain(root / 'scripts/dependencies.lock.json').decode('utf-8'))
    entries = sorted((entry for entry in lock.values() if 'seasonalIndex' in entry),
                     key=lambda entry: entry['seasonalIndex'])
    if [entry['seasonalIndex'] for entry in entries] != list(range(18)):
        raise RuntimeError('The seasonal sprite needs exactly eighteen distinct cells.')
    atlas = ET.Element('{' + SVG + '}svg', {'viewBox': '0 0 768 384', 'width': '768', 'height': '384'})
    atlas.append(ET.Comment('Noto Emoji artwork, Copyright 2013 Google, Inc.; Apache-2.0. '
                            'Rearranged into a local sprite; see LICENSES.txt.'))
    allowed = {'svg', 'g', 'path', 'defs', 'linearGradient', 'radialGradient', 'stop',
               'circle', 'ellipse', 'rect', 'polygon', 'polyline', 'line', 'clipPath', 'use'}
    for entry in entries:
        content = verified(entry, root)
        if re.search(br'<!\s*(?:DOCTYPE|ENTITY)', content, re.I):
            raise RuntimeError('An emoji SVG contains a document type or entity.')
        image = ET.fromstring(content)
        if image.tag != '{' + SVG + '}svg' or image.get('viewBox') != '0 0 128 128':
            raise RuntimeError('An emoji SVG has the wrong canvas.')
        index = entry['seasonalIndex']
        prefix = 'emoji' + str(index) + '-'
        ids = {element.get('id') for element in image.iter() if element.get('id')}
        for element in image.iter():
            if element.tag not in {'{' + SVG + '}' + name for name in allowed}:
                raise RuntimeError('An emoji SVG contains an unexpected element.')
            for name, value in list(element.attrib.items()):
                if name.lower().startswith('on') or '@' in value:
                    raise RuntimeError('An emoji SVG contains active content.')
                if name in ('href', '{' + XLINK + '}href'):
                    if not value.startswith('#') or value[1:] not in ids:
                        raise RuntimeError('An emoji SVG contains an external reference.')
                    value = '#' + prefix + value[1:]
                if 'url(' in value.lower():
                    if re.sub(r'url\(#[A-Za-z0-9_.:-]+\)', '', value).lower().find('url(') >= 0:
                        raise RuntimeError('An emoji SVG contains an external image reference.')
                    value = re.sub(r'url\(#([A-Za-z0-9_.:-]+)\)',
                                   lambda match: 'url(#' + prefix + match[1] + ')', value)
                if name == 'id':
                    value = prefix + value
                element.set(name, value)
        image.attrib.clear()
        image.attrib.update({'x': str(index % 6 * 128), 'y': str(index // 6 * 128),
                             'width': '128', 'height': '128', 'viewBox': '0 0 128 128', 'overflow': 'hidden'})
        atlas.append(image)
    return ET.tostring(atlas, encoding='utf-8', xml_declaration=True) + b'\n'


def generate(root=ROOT, verify=False):
    lock = json.loads(read_plain(root / 'scripts/dependencies.lock.json').decode('utf-8'))
    notice = verified(lock['noto-emoji/svg/LICENSE'], root)
    license_text = verified(lock['noto-emoji/Apache-2.0.txt'], root)
    outputs = {'ui/assets/seasonal-emoji.svg': sprite_bytes(root),
               'docs/NotoEmoji-Images-LICENSE.txt': notice + b'\n' + license_text}
    for name, content in outputs.items():
        target = root / name
        if verify:
            if read_plain(target) != content:
                raise RuntimeError('Regenerate the seasonal artwork: ' + name)
        else:
            write_artifact(target, content)
    return outputs


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    generate(verify=args.verify)
    print('Verified seasonal emoji artwork.' if args.verify else 'Built seasonal emoji artwork.')
