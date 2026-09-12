"""Generate #220 fixtures and exercise native rendering, exports and HTTP loading.

Run from any directory with Python + Pillow + NumPy after building Release
tinta and input_tests. All generated assets/results stay in ignored out/.
"""
from pathlib import Path
import argparse
import base64
import ctypes
import functools
import hashlib
import http.server
import json
import os
import re
import shutil
import struct
import subprocess
import threading
import time
import zipfile

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=REPO / 'build/Release/tinta.exe')
    parser.add_argument('--input-tests', type=Path, default=REPO / 'build/Release/input_tests.exe')
    parser.add_argument('--output', type=Path, default=REPO / 'out/issue-220/fixed')
    args = parser.parse_args()
    output = args.output.resolve()
    assets = output / 'image-cache-assets'
    assets.mkdir(parents=True, exist_ok=True)
    Image.new('RGB', (256, 128), (45, 130, 190)).save(assets / 'small.png')
    Image.new('RGB', (256, 128), (45, 130, 190)).save(assets / 'small.jpg', quality=90)
    Image.new('RGB', (6000, 4000), (45, 130, 190)).save(assets / 'large-flat.jpg', quality=90)
    pixels = np.random.default_rng(220).integers(0, 256, (4000, 6000, 3), dtype=np.uint8)
    Image.fromarray(pixels).save(assets / 'large.jpg', quality=60)
    del pixels
    shutil.copy2(assets / 'large.jpg', assets / 'large-second.jpg')
    Image.new('RGBA', (128, 64), (20, 50, 100, 128)).save(assets / 'alpha.png')
    Image.new('RGB', (300, 150), (190, 130, 45)).save(assets / 'dpi.png', dpi=(300, 300))
    Image.new('RGB', (12000, 12), (45, 130, 190)).save(assets / 'wide.png')
    (assets / 'small.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="128" height="64">'
        '<rect width="128" height="64" fill="#2d82be"/></svg>', encoding='utf-8')
    (assets / 'broken.jpg').write_bytes(b'not an image')
    (assets / 'truncated.jpg').write_bytes((assets / 'large.jpg').read_bytes()[:200])
    (assets / 'oversized.bmp').write_bytes(
        struct.pack('<2sIHHI', b'BM', 54, 0, 0, 54) +
        struct.pack('<IiiHHIIiiII', 40, 20000, 20000, 1, 24, 0, 0, 0, 0, 0, 0))
    fixture = output / 'image-cache-large.md'
    shutil.copy2(REPO / 'tests/fixtures/image-cache-large.md', fixture)

    runner = output / 'runner'
    runner.mkdir(exist_ok=True)
    executable = runner / 'tinta.exe'
    shutil.copy2(args.binary.resolve(), executable)
    (runner / 'settings.ini').write_text(
        '[Settings]\nthemeIndex=0\nfollowSystemTheme=0\ncheckUpdates=0\n'
        'hasAskedFileAssociation=1\nopenInTabs=0\nlanguage=en\n', encoding='utf-8')
    ctypes.windll.kernel32.SetErrorMode(3)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    results = []

    def run(label, command, cwd=None):
        target = output / label
        target.mkdir(exist_ok=True)
        started = time.monotonic()
        process = subprocess.run([str(part) for part in command], cwd=cwd,
            env=dict(os.environ, LOCALAPPDATA=str(target)), startupinfo=startup,
            capture_output=True, timeout=60)
        row = dict(case=label, exit=process.returncode,
                   seconds=round(time.monotonic() - started, 2))
        results.append(row)
        print(json.dumps(row), flush=True)
        assert process.returncode == 0, process.stdout.decode(errors='replace') + process.stderr.decode(errors='replace')
        assert not (target / 'Tinta/crash.dmp').exists()
        return target

    cases = {
        'small-only': ['small.jpg'],
        'large-only': ['large.jpg'],
        'small-then-large': ['small.jpg', 'large.jpg'],
        'large-then-small': ['large.jpg', 'small.jpg'],
        'small-then-large-flat': ['small.jpg', 'large-flat.jpg'],
        'truncated-then-small': ['truncated.jpg', 'small.jpg'],
        'svg-then-large': ['small.svg', 'large.jpg'],
    }
    for name, images in cases.items():
        source = output / (name + '.md')
        source.write_text('# Image loading control\n\n**Bold**, *emphasis* and math $a+b=c$.\n\n'
            '> A quote before the image.\n\n' + '\n\n'.join(
                f'![{image}](image-cache-assets/{image})' for image in images) +
            '\n\n## Content after images\n\n| Column | Value |\n| --- | --- |\n'
            '| Test | `code` |\n\n- A list item.\n', encoding='utf-8')
        target = run(name, [executable, source, '--printpages', output / name / 'pages'])
        assert list((target / 'pages').glob('page-*.png'))

    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in [fixture, *assets.iterdir()]}
    for mode, destination in [('printpages', 'pages'), ('exporthtml', 'document.html'),
                              ('exportdocx', 'document.docx'), ('exportpdf', 'document.pdf')]:
        target = output / ('mixed-' + mode)
        run('mixed-' + mode, [executable, fixture, '--' + mode, target / destination])
        assert (target / destination).exists()
    html = (output / 'mixed-exporthtml/document.html').read_text(encoding='utf-8')
    for marker in ['<h1', '<h2', '<table', '<blockquote', '<pre', '<strong>', '<em>', '<svg',
                   'All surrounding content']:
        assert marker in html, marker
    expected = {(assets / name).read_bytes() for name in
                ['small.png', 'large.jpg', 'alpha.png', 'dpi.png', 'wide.png']}
    embedded = {base64.b64decode(value) for value in
                re.findall(r'data:image/[^;]+;base64,([^"\s]+)', html)}
    assert expected <= embedded, 'HTML changed/missed original images'
    with zipfile.ZipFile(output / 'mixed-exportdocx/document.docx') as docx:
        media = {docx.read(name) for name in docx.namelist() if name.startswith('word/media/')}
        assert expected <= media, 'DOCX changed/missed original images'
        assert b'All surrounding content' in docx.read('word/document.xml')
    assert (output / 'mixed-exportpdf/document.pdf').read_bytes().startswith(b'%PDF-')
    assert before == {name: hashlib.sha256(Path(name).read_bytes()).hexdigest() for name in before}

    class QuietHandler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *unused):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0),
        functools.partial(QuietHandler, directory=str(assets)))
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        remote = output / 'remote-native'
        remote.mkdir(exist_ok=True)
        # Copy beside isolated settings so the test cannot load personal config.
        shutil.copy2(args.input_tests.resolve(), remote / 'input_tests.exe')
        (remote / 'settings.ini').write_text('; Isolated remote image tests\n')
        run('remote-native', [remote / 'input_tests.exe', '--image-tests',
            f'http://127.0.0.1:{server.server_port}'], cwd=remote)
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    report = dict(binary_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                  large_jpeg_bytes=(assets / 'large.jpg').stat().st_size, results=results)
    (output / 'results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Native rendering, original-image exports and localhost downloads passed.', flush=True)


if __name__ == '__main__':
    main()
