"""Compare the #223 mixed Markdown exports with the published 3.7.1 baseline.

The native input checks run through CTest search_input_focus; this script checks
surrounding document rendering and export fidelity using isolated settings.
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
parser.add_argument('--baseline', type=Path, default=repo.parent/'tinta-3.7.1.exe')
parser.add_argument('--output', type=Path, default=repo/'out/issue-223/exports')
args = parser.parse_args()
output = args.output.resolve()
fixture = repo/'tests/fixtures/find-table-focus.md'
before = hashlib.sha256(fixture.read_bytes()).hexdigest()
ctypes.windll.kernel32.SetErrorMode(3)
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
results = []
for variant, binary in [('baseline', args.baseline), ('fixed', args.binary)]:
    for theme, index in [('paper', 0), ('midnight', 5)]:
        folder = output/variant/theme
        folder.mkdir(parents=True, exist_ok=True)
        exe = folder/'tinta.exe'
        shutil.copy2(binary, exe)
        (folder/'settings.ini').write_text(f'[Settings]\nthemeIndex={index}\nfollowSystemTheme=0\n'
            'hasAskedFileAssociation=1\ncheckUpdates=0\nlanguage=en\n', encoding='utf-8')
        for option, filename in [('printpages','pages'),('exporthtml','document.html'),
                                 ('exportdocx','document.docx'),('exportpdf','document.pdf')]:
            result = subprocess.run([str(exe), str(fixture), '--'+option, str(folder/filename)],
                startupinfo=startup, capture_output=True, timeout=45,
                env=dict(os.environ, LOCALAPPDATA=str(folder)))
            assert result.returncode == 0, (variant, theme, option, result.returncode)
            assert (folder/filename).exists()
        html = (folder/'document.html').read_text(encoding='utf-8')
        for marker in ['<h1','<h2','<table','<blockquote','<pre','<strong','<ul','Surrounding content']:
            assert marker in html, marker
        assert (folder/'document.pdf').read_bytes().startswith(b'%PDF-')
        assert not (folder/'Tinta/crash.dmp').exists()
for theme in ['paper','midnight']:
    old, new = output/'baseline'/theme, output/'fixed'/theme
    names = ['document.html'] + [str(p.relative_to(old)) for p in sorted((old/'pages').glob('*.png'))]
    assert len(names) > 1
    for name in names:
        assert (old/name).read_bytes() == (new/name).read_bytes(), (theme, name)
    with zipfile.ZipFile(old/'document.docx') as a, zipfile.ZipFile(new/'document.docx') as b:
        assert set(a.namelist()) == set(b.namelist())
        assert all(a.read(name) == b.read(name) for name in a.namelist())
    results.append(dict(theme=theme, identical=names+['document.docx members'], pdf_valid=True))
assert hashlib.sha256(fixture.read_bytes()).hexdigest() == before
(output/'results.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
print(json.dumps(results, indent=2))
