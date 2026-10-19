#!/usr/bin/env python3
"""Format project JSON with two-space indents and short arrays on one line."""
import argparse
import json
from pathlib import Path
import sys

LINE_WIDTH = 100
ROOT = Path(__file__).resolve().parents[1]


class Number(str):
    """Keep the original spelling and precision of JSON numbers."""


def unique_fields(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key: ' + key)
        result[key] = value
    return result


def invalid_constant(value):
    raise ValueError('Invalid JSON number: ' + value)


def format_json(source, line_width=LINE_WIDTH):
    value = json.loads(source, object_pairs_hook=unique_fields,
                       parse_int=Number, parse_float=Number,
                       parse_constant=invalid_constant)

    def compact(item):
        if isinstance(item, Number):
            return str(item)
        if isinstance(item, list):
            return '[' + ', '.join(compact(child) for child in item) + ']'
        if isinstance(item, dict):
            return '{' + ', '.join(compact(key) + ': ' + compact(child)
                                   for key, child in item.items()) + '}'
        return json.dumps(item, ensure_ascii=False, allow_nan=False)

    def lines(item, depth=0, prefix='', suffix=''):
        start = '  ' * depth + prefix
        if not isinstance(item, (list, dict)) or not item:
            return [start + compact(item) + suffix]
        if isinstance(item, list):
            single = start + compact(item) + suffix
            if len(single) <= line_width:
                return [single]
            result = [start + '[']
            for index, child in enumerate(item):
                result.extend(lines(child, depth + 1,
                                    suffix=',' if index + 1 < len(item) else ''))
            return result + ['  ' * depth + ']' + suffix]
        result = [start + '{']
        for index, (key, child) in enumerate(item.items()):
            result.extend(lines(child, depth + 1, compact(key) + ': ',
                                ',' if index + 1 < len(item) else ''))
        return result + ['  ' * depth + '}' + suffix]

    return '\n'.join(lines(value)) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths', nargs='*', type=Path,
                        help='JSON files or folders; default: assets, E2E scenes, .vscode')
    parser.add_argument('--check', action='store_true', help='Check without writing files')
    parser.add_argument('--line-width', type=int, default=LINE_WIDTH,
                        help='Maximum width for inline arrays, including indent and comma (default: 100)')
    args = parser.parse_args()
    if args.line_width < 1:
        parser.error('--line-width must be positive')
    paths = args.paths or [ROOT / 'assets', ROOT / 'tests/e2e/scenes', ROOT / '.vscode']
    files = sorted({file for path in paths
                    for file in (path.rglob('*.json') if path.is_dir() else [path])})
    changes = []
    for path in files:
        try:
            original = path.read_bytes()
            formatted = format_json(original.decode('utf-8'), args.line_width).encode('utf-8')
        except (OSError, ValueError, RecursionError) as error:
            print(f'{path}: {error}', file=sys.stderr)
            return 2
        if formatted != original:
            changes.append((path, formatted))
    for path, formatted in changes:
        print(('Needs formatting: ' if args.check else 'Formatted: ') + str(path))
        if not args.check:
            path.write_bytes(formatted)
    print(f'{len(files)} JSON files checked; {len(changes)} ' +
          ('need formatting.' if args.check else 'formatted.'))
    return 1 if args.check and changes else 0


if __name__ == '__main__':
    sys.exit(main())
