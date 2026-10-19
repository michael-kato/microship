#!/usr/bin/env python3
"""Open a before/after PNG viewer. Uses Python's standard library and a browser.

    python3 scripts/compare_images.py
    python3 scripts/compare_images.py before.png after.png --no-open

Drag the slider for a split view, or select Blend to change opacity.
The generated HTML file contains both images and works without a server.
"""
import argparse
import base64
import html
from pathlib import Path
import struct
import webbrowser

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BEFORE = ROOT / 'screenshots/000720-render-perf-near-baseline-6-film.png'
DEFAULT_AFTER = ROOT / 'screenshots/000787-render-perf-near-current-1-film.png'

PAGE = '''<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Nanova image comparison</title>
<style>
* { box-sizing: border-box; }
body { margin: 0; padding: 20px; background: #151719; color: #eee;
       font: 16px system-ui, sans-serif; }
main { max-width: 1920px; margin: auto; }
h1 { font-size: 22px; margin: 0 0 12px; }
.controls { display: flex; align-items: center; gap: 12px; flex-wrap: wrap;
            margin-bottom: 12px; }
input { flex: 1; min-width: 160px; accent-color: #73dfce; }
select { padding: 5px; font: inherit; }
output { min-width: 220px; }
.stage { position: relative; overflow: hidden; background: #000; }
.stage img { display: block; width: 100%; height: auto; }
#after { position: absolute; inset: 0; }
#line { position: absolute; top: 0; bottom: 0; width: 2px;
        background: white; box-shadow: 0 0 3px #000; pointer-events: none; }
.files { display: flex; justify-content: space-between; gap: 16px;
         margin-top: 10px; font-size: 13px; color: #c7cbce; }
.files span { overflow-wrap: anywhere; }
p { color: #c7cbce; font-size: 14px; }
</style>
<main>
<h1>Before / after</h1>
<div class="controls">
<label for="mode">View</label>
<select id="mode"><option value="split">Split</option><option value="blend">Blend</option></select>
<label for="slider">After</label>
<input id="slider" type="range" min="0" max="100" step="0.1" value="50">
<output id="value" for="slider"></output>
</div>
<div class="stage">
<img id="before" src="__BEFORE_DATA__" alt="Before" width="__WIDTH__" height="__HEIGHT__">
<img id="after" src="__AFTER_DATA__" alt="After" width="__WIDTH__" height="__HEIGHT__">
<div id="line"></div>
</div>
<div class="files"><span>Before: __BEFORE_NAME__</span><span>After: __AFTER_NAME__</span></div>
<p>0% shows Before. 100% shows After. Use the slider or its arrow keys.
Both images use the same scale. Open this file without an internet connection.</p>
</main>
<script>
const slider = document.getElementById('slider');
const mode = document.getElementById('mode');
const after = document.getElementById('after');
const line = document.getElementById('line');
const value = document.getElementById('value');
function update() {
    const amount = Number(slider.value);
    const split = mode.value === 'split';
    after.style.clipPath = split ? `inset(0 ${100 - amount}% 0 0)` : 'none';
    after.style.opacity = split ? '1' : String(amount / 100);
    line.style.left = `${amount}%`;
    line.hidden = !split || amount === 0 || amount === 100;
    value.textContent = split ? `${amount}% After / ${100 - amount}% Before`
                              : `${amount}% After opacity`;
}
slider.addEventListener('input', update);
mode.addEventListener('change', update);
update();
</script>
</html>
'''


def read_png(path):
    data = path.read_bytes()
    if (len(data) < 33 or data[:8] != b'\x89PNG\r\n\x1a\n'
            or data[8:16] != b'\x00\x00\x00\rIHDR'):
        raise ValueError(f'Not a PNG image: {path}')
    size = struct.unpack('>II', data[16:24])
    if not all(size):
        raise ValueError(f'Empty PNG image: {path}')
    return data, size


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('before', nargs='?', type=Path, default=DEFAULT_BEFORE)
    parser.add_argument('after', nargs='?', type=Path, default=DEFAULT_AFTER)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/image-comparison.html')
    parser.add_argument('--no-open', action='store_true', help='Write the viewer without opening it.')
    args = parser.parse_args()
    try:
        before, size = read_png(args.before)
        after, after_size = read_png(args.after)
        if size != after_size:
            raise ValueError(f'Image dimensions must match: {size} and {after_size}')
        output = args.output.resolve()
        if output in (args.before.resolve(), args.after.resolve()):
            raise ValueError('The output must not replace either input image.')
        page = PAGE.replace('__WIDTH__', str(size[0])).replace('__HEIGHT__', str(size[1]))
        for key, data in [('BEFORE', before), ('AFTER', after)]:
            page = page.replace(f'__{key}_DATA__', 'data:image/png;base64,'
                                + base64.b64encode(data).decode('ascii'))
        page = page.replace('__BEFORE_NAME__', html.escape(args.before.name))
        page = page.replace('__AFTER_NAME__', html.escape(args.after.name))
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(page, encoding='utf-8')
    except (OSError, ValueError) as error:
        parser.exit(1, f'Error: {error}\n')
    print(f'Viewer: {output}')
    if not args.no_open:
        webbrowser.open(output.as_uri())


if __name__ == '__main__':
    main()
