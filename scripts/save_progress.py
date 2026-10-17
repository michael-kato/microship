#!/usr/bin/env python3
"""Save a display screenshot as NUMBER-description.png in screenshots/."""

import argparse
from pathlib import Path
import re
import struct
import sys
import zlib

PNG_SAMPLE_BITS = 8
MAX_DESCRIPTION_CHARACTERS = 64

ROOT = Path(__file__).resolve().parents[1] / 'screenshots'

def ppm_png(data):
    # Preserve nanova's display-mapped RGB8 without resizing or changing pixels.
    header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255(?:\r\n|[\t\n\r ])', data)
    if not header:
        raise ValueError('Expected a nanova RGB8 .ppm display capture')
    width, height = map(int, header.groups())
    pixels = data[header.end():]
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise ValueError('Capture dimensions or RGB payload are invalid')

    def chunk(kind, payload):
        return (struct.pack('>I', len(payload)) + kind + payload
                + struct.pack('>I', zlib.crc32(kind + payload)))

    stride = width * 3
    rows = b''.join(b'\0' + pixels[start:start+stride]
                    for start in range(0, len(pixels), stride))
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, PNG_SAMPLE_BITS, 2, 0, 0, 0))
            + chunk(b'sRGB', b'\0') + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

def archive_capture(capture, description=None):
    capture = Path(capture)
    data = capture.read_bytes()
    if capture.suffix.lower() == '.ppm':
        data = ppm_png(data)
    elif capture.suffix.lower() != '.png' or not data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError('Use a display .ppm or .png screenshot')
    if description is None:
        description = re.sub(r'^\d+[-_ ]*', '', capture.stem) or 'render'
    description = re.sub(r'[^a-z0-9]+', '-', description.lower()).strip('-')[:MAX_DESCRIPTION_CHARACTERS].rstrip('-')
    if not description:
        raise ValueError('Description must contain letters or numbers')
    ROOT.mkdir(parents=True, exist_ok=True)
    numbers = (re.fullmatch(r'(\d+)(?:-[a-z0-9-]+)?\.png', path.name)
               for path in ROOT.glob('*.png'))
    number = max((int(match[1]) for match in numbers if match), default=0) + 1
    while True:
        # Claim the numeric name first so concurrent writers with different
        # descriptions cannot create two files with the same sequence number.
        claim = ROOT / f'{number:06d}.png'
        try:
            with claim.open('xb') as output:
                occupied = any(ROOT.glob(f'{number:06d}-*.png'))
                if not occupied:
                    output.write(data)
        except FileExistsError:
            number += 1
            continue
        if occupied:
            claim.unlink()
            number += 1
            continue
        path = ROOT / f'{number:06d}-{description}.png'
        claim.rename(path)
        break
    print(f'Screenshot saved: {path}', flush=True)
    return path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path, help='Existing display .ppm or .png; this script does not run the GPU')
    parser.add_argument('--description', help='Short description; defaults to the capture filename')
    args = parser.parse_args()
    archive_capture(args.capture, args.description)

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as error:
        print(f'Could not save screenshot: {error}', file=sys.stderr)
        sys.exit(1)
