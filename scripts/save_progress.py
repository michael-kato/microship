#!/usr/bin/env python3
"""Save one display screenshot per commit as NUMBER_description.png in screenshots/.

Run it once after committing a visible change. Tests and comparisons do not save here.
"""

import argparse
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib

PNG_SAMPLE_BITS = 8
MAX_DESCRIPTION_CHARACTERS = 64

ROOT = Path(__file__).resolve().parents[1] / 'screenshots'

def ppm_png(data):
    # Preserve nanovore's display-mapped RGB8 without resizing or changing pixels.
    header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255(?:\r\n|[\t\n\r ])', data)
    if not header:
        raise ValueError('Expected a nanovore RGB8 .ppm display capture')
    width, height = map(int, header.groups())
    pixels = data[header.end():]
    if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
        raise ValueError('Capture dimensions or RGB payload are invalid')

    stride = width * 3
    rows = b''.join(b'\0' + pixels[start:start+stride]
                    for start in range(0, len(pixels), stride))
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, PNG_SAMPLE_BITS, 2, 0, 0, 0))
            + chunk(b'sRGB', b'\0') + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

COMMIT_KEY = b'nanovore-commit'

def chunk(kind, payload):
    return (struct.pack('>I', len(payload)) + kind + payload
            + struct.pack('>I', zlib.crc32(kind + payload)))

def png_commit(data):
    # Walk the PNG chunks for the commit stamp written below.
    at = 8
    while at + 8 <= len(data):
        length, kind = struct.unpack('>I4s', data[at:at+8])
        payload = data[at+8:at+8+length]
        if kind == b'tEXt' and payload.startswith(COMMIT_KEY + b'\0'):
            return payload[len(COMMIT_KEY)+1:].decode()
        at += 12 + length
    return None

def head_commit():
    return subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=Path(__file__).resolve().parent,
                          capture_output=True, text=True, check=True).stdout.strip()

def archive_capture(capture, description=None):
    capture = Path(capture)
    data = capture.read_bytes()
    if capture.suffix.lower() == '.ppm':
        data = ppm_png(data)
    elif capture.suffix.lower() != '.png' or not data.startswith(b'\x89PNG\r\n\x1a\n'):
        raise ValueError('Use a display .ppm or .png screenshot')
    if description is None:
        description = re.sub(r'^\d+[-_ ]*', '', capture.stem) or 'render'
    description = re.sub(r'[^a-z0-9]+', '_', description.lower()).strip('_')[:MAX_DESCRIPTION_CHARACTERS].rstrip('_')
    if not description:
        raise ValueError('Description must contain letters or numbers')
    ROOT.mkdir(parents=True, exist_ok=True)
    numbered = sorted((int(m[1]), path) for path in ROOT.glob('*.png')
                      if (m := re.fullmatch(r'(\d+)(?:[-_][a-z0-9_-]+)?\.png', path.name)))
    commit = head_commit()
    # One screenshot per commit: the newest one carries the commit it shows.
    if numbered and png_commit(numbered[-1][1].read_bytes()) == commit:
        print(f'Screenshot for commit {commit} already exists: {numbered[-1][1]}', flush=True)
        return numbered[-1][1]
    ihdr_end = 8 + 12 + struct.unpack('>I', data[8:12])[0]
    data = data[:ihdr_end] + chunk(b'tEXt', COMMIT_KEY + b'\0' + commit.encode()) + data[ihdr_end:]
    path = ROOT / f'{(numbered[-1][0] if numbered else 0) + 1:06d}_{description}.png'
    with path.open('xb') as output:
        output.write(data)
    print(f'Screenshot saved for commit {commit}: {path}', flush=True)
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
