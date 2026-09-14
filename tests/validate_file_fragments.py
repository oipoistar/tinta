"""Render the linked Markdown fixtures and verify their exported links and content.

The CTest file_fragment_navigation suite covers native navigation. This harness
checks Paper/Midnight PNG, HTML, DOCX and PDF exports in isolated settings.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import zipfile

repo = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, default=repo/'build/Release/tinta.exe')
parser.add_argument('--output', type=Path, default=repo/'out/discussion-225/exports')
args = parser.parse_args()
output = args.output.resolve()
fixtures = repo/'tests/fixtures/file-fragments'
hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in fixtures.glob('*.md')}
ctypes.windll.kernel32.SetErrorMode(3)
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
results = []
for theme, index in [('paper', 0), ('midnight', 5)]:
    folder = output/theme
    folder.mkdir(parents=True, exist_ok=True)
    exe = folder/'tinta.exe'
    shutil.copy2(args.binary, exe)
    (folder/'settings.ini').write_text(f'[Settings]\nthemeIndex={index}\nfollowSystemTheme=0\n'
        'hasAskedFileAssociation=1\ncheckUpdates=0\nlanguage=en\n', encoding='utf-8')
    for name in ['index', 'target notes']:
        dest = folder/name
        dest.mkdir(exist_ok=True)
        for option, filename in [('printpages','pages'),('exporthtml','document.html'),
                                 ('exportdocx','document.docx'),('exportpdf','document.pdf')]:
            run = subprocess.run([str(exe), str(fixtures/(name+'.md')), '--'+option, str(dest/filename)],
                startupinfo=startup, capture_output=True, timeout=45,
                env=dict(os.environ, LOCALAPPDATA=str(folder)))
            assert run.returncode == 0, (theme, name, option, run.returncode)
            assert (dest/filename).exists()
        html = (dest/'document.html').read_text(encoding='utf-8')
        for marker in ['<h1','<h2','<table','<blockquote','<pre','<strong','<ul','Surrounding content']:
            assert marker in html, (name, marker)
        assert 'fileref-ok:' not in html and 'fileref-missing:' not in html
        expected = ['target%20notes.md#destination-heading',
                    'target%20notes.md#%E4%B8%AD%E6%96%87%E6%A0%87%E9%A2%98',
                    'hash%23notes.md#destination-heading', 'literal%2523.md#destination-heading'] \
                   if name == 'index' else ['index.md#destination-heading']
        with zipfile.ZipFile(dest/'document.docx') as archive:
            rels = archive.read('word/_rels/document.xml.rels').decode('utf-8')
            xml = archive.read('word/document.xml').decode('utf-8')
            assert '<w:tbl>' in xml and 'Surrounding content' in xml
            for url in expected:
                assert url in html and url in rels, (theme, name, url)
        pages = sorted((dest/'pages').glob('*.png'))
        assert pages and all(p.read_bytes().startswith(b'\x89PNG\r\n\x1a\n') for p in pages)
        assert (dest/'document.pdf').read_bytes().startswith(b'%PDF-')
        assert not (folder/'Tinta/crash.dmp').exists()
        results.append(dict(theme=theme, document=name, png_pages=len(pages), links_preserved=True,
                            mixed_content_preserved=True, pdf_valid=True))
assert hashes == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in fixtures.glob('*.md')}
(output/'results.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
print(json.dumps(results, indent=2))
