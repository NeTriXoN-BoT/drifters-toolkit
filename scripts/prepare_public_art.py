"""Retain only PNG image/color chunks in owned public branding art.

Original generated files stay in the author's image library. This build step never
alters pixel data or third-party artwork, and does not claim human authorship.
"""
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
PNG = b'\x89PNG\r\n\x1a\n'
KEEP = {b'IHDR', b'PLTE', b'IDAT', b'IEND', b'tRNS', b'sRGB', b'gAMA', b'cHRM'}


def image_chunks(data):
    if not data.startswith(PNG) or len(data) > 32 * 1024 * 1024:
        raise ValueError('Expected a bounded PNG image.')
    result, removed, offset, ended = bytearray(PNG), [], len(PNG), False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError('Truncated PNG chunk.')
        size = struct.unpack_from('>I', data, offset)[0]
        end = offset + 12 + size
        if end > len(data):
            raise ValueError('Invalid PNG chunk length.')
        kind = data[offset+4:offset+8]
        chunk = data[offset+8:offset+8+size]
        crc = struct.unpack_from('>I', data, offset+8+size)[0]
        if zlib.crc32(kind+chunk) & 0xffffffff != crc:
            raise ValueError('Invalid PNG checksum.')
        if offset == len(PNG) and (kind != b'IHDR' or size != 13):
            raise ValueError('Missing PNG header.')
        if kind in KEEP:
            result.extend(data[offset:end])
        elif not kind[0] & 32:
            raise ValueError('Unsupported critical PNG chunk.')
        else:
            removed.append(kind.decode('ascii'))
        offset = end
        if kind == b'IEND':
            if size or offset != len(data):
                raise ValueError('Unexpected data after PNG image.')
            ended = True
            break
    if not ended:
        raise ValueError('Missing PNG end marker.')
    return bytes(result), removed


def prepare(path, source=None):
    path = path.resolve()
    if not path.is_relative_to(ROOT/'ui/assets'):
        raise ValueError('Public art output must stay in ui/assets.')
    original = (source or path).read_bytes()
    content, removed = image_chunks(original)
    path.parent.mkdir(parents=True, exist_ok=True)
    if source or content != original:
        path.write_bytes(content)
    print(f'{path.relative_to(ROOT).as_posix()}: image chunks retained; {len(removed)} metadata chunks omitted.')


if __name__ == '__main__':
    for name in ('hero.png', 'icon.png'):
        prepare(ROOT/'ui/assets'/name)
