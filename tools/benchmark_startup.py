"""Compare instrument_startup.py binaries using shuffled, fresh-process launches.

Requires profiling binaries: regular Tinta does not exit or produce trace CSVs.
Each supplied binary is copied beside isolated portable settings. No installed
configuration, file associations, or user document is modified.
"""
import argparse, csv, json, os, random, statistics, subprocess, time, shutil
from pathlib import Path

repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--work-dir', type=Path, default=repo/'build/startup-benchmark')
p.add_argument('--binary', action='append', required=True, help='label=path to a profiling executable; repeat for comparisons')
p.add_argument('--runs', type=int, default=5)
p.add_argument('--cases', nargs='+', choices=['start','small','readme','paragraphs','table','code','fixture'],
               default=['start','small','readme','paragraphs','table','code'])
p.add_argument('--width', type=int, default=1000)
p.add_argument('--theme', type=int, default=0)
p.add_argument('--frames', action='store_true', help='Save the retained reading layout after the measured frame')
args=p.parse_args()
if args.runs < 1 or args.width < 200:
    p.error('Use at least one run and a window width of at least 200')
base=args.work_dir.resolve(); base.mkdir(parents=True,exist_ok=True)
args.variants=[]
for binary in args.binary:
    label, path=binary.split('=',1)
    if not label or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in label):
        p.error('Use a simple label without path separators')
    if label in args.variants:
        p.error('Duplicate binary label')
    args.variants.append(label)
    folder=base/'bin'/label; folder.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(Path(path).resolve(),folder/'tinta.exe')
docs=base/'docs'; docs.mkdir(exist_ok=True)
(docs/'small.md').write_text('# Startup test\n\nHello **Tinta**, with *emphasis*, `code`, a [link](https://example.com), and a short list.\n\n- First\n- Second\n',encoding='utf-8')
import re
readme=(repo/'README.md').read_text(encoding='utf-8')
(docs/'readme.md').write_text(re.sub(r'<img\b[^>]*>', '', readme),encoding='utf-8')
(docs/'paragraphs.md').write_text('# Long document\n\n'+('A paragraph with **bold**, *emphasis* and `code`, with enough words to wrap onto another line.\n\n'*10000),encoding='utf-8')
(docs/'table.md').write_text('| Row | Value | Note |\n|:---:|---:|:---:|\n'+''.join(f'| {i} | {i*3} | A typical cell |\n' for i in range(4000)),encoding='utf-8')
(docs/'code.md').write_text('```cpp\n'+''.join(f'int value_{i} = {i}; // benchmark\n' for i in range(10000))+'```\n',encoding='utf-8')
shutil.copyfile(repo/'tests/fixtures/startup-code.md',docs/'fixture.md')
settings='''[Settings]
themeIndex=0
followSystemTheme=0
zoomFactor=1
windowX=100
windowY=100
windowWidth=1000
windowHeight=800
windowMaximized=0
hasAskedFileAssociation=1
checkUpdates=0
openInTabs=0
reopenTabs=0
readingWidthPct=100
tocPinned=0
browserPinned=0
language=en
'''
settings=settings.replace('windowWidth=1000',f'windowWidth={args.width}').replace('themeIndex=0',f'themeIndex={args.theme}')
jobs=[(variant,case,n) for variant in args.variants for case in args.cases for n in range(args.runs)]
random.Random(20261119).shuffle(jobs)
for variant,case,n in jobs:
    folder=base/'bin'/variant
    (folder/'settings.ini').write_text(settings,encoding='utf-8')
    out=base/'results'/variant; out.mkdir(parents=True,exist_ok=True)
    log=out/f'{case}-{n}.csv'
    env=os.environ.copy(); env['TINTA_STARTUP_LOG']=str(log)
    env.pop('TINTA_STARTUP_FRAME',None)
    if args.frames:
        env['TINTA_STARTUP_FRAME']=str(out/f'{case}-{n}.png')
    command=[str(folder/'tinta.exe')]
    if case!='start': command.append(str(docs/f'{case}.md'))
    result=subprocess.run(command,cwd=folder,env=env,timeout=45)
    if result.returncode: raise RuntimeError((command,result.returncode))
    time.sleep(.1)
summary={}
for variant in args.variants:
    bycase={}
    for case in args.cases:
        samples=[]
        for n in range(args.runs):
            events=list(csv.DictReader((base/'results'/variant/f'{case}-{n}.csv').open()))
            get=lambda name: next(e for e in events if e['event']==name)
            renders=[e for e in events if e['event']=='render' and e['visible']=='1']
            if not renders: raise RuntimeError('No visible first frame')
            first=renders[0]
            values={name:float(get(name)['duration_us'])/1000 for name in ['loader','themes','language','settings','formats','typography','chrome','d2d_init','target','end_draw']}
            values['first_frame']=(int(first['start_us'])+int(first['duration_us']))/1000
            values['render']=float(first['duration_us'])/1000
            for label,start,end in [('window','before_window','after_window'),('file','before_load','after_load'),('show','before_show','after_show')]:
                values[label]=(int(get(end)['start_us'])-int(get(start)['start_us']))/1000
            for label in ['layout_begin','first_layout','full_layout']:
                values[label]=sum(float(e['duration_us']) for e in events if e['event']==label)/1000
            values['hash']=get('hash')['start_us'];values['runs']=first['text_runs']
            samples.append(values)
        bycase[case]={k:round(statistics.median(s[k] for s in samples),3) for k in samples[0] if k not in ['hash','runs']}
        bycase[case]['hashes']=sorted(set(s['hash'] for s in samples))
        bycase[case]['text_runs']=sorted(set(s['runs'] for s in samples))
    summary[variant]=bycase
(base/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
reference=summary[args.variants[0]]
for variant in args.variants[1:]:
    for case in args.cases:
        for key in ['hashes','text_runs']:
            if summary[variant][case][key] != reference[case][key]:
                raise RuntimeError(f'{variant}/{case}: first-frame {key} differs from {args.variants[0]}')
