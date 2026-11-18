"""Prepare an isolated, first-frame-only profiling copy; never edit the checkout.

Build its source directory with BUILD_TESTING=OFF, then use benchmark_startup.py.
The resulting executable exits after the first visible frame and is not an app
distribution. Timings end at EndDraw, not desktop composition or screen scanout.
"""
from pathlib import Path
import argparse
import io
import shutil
import subprocess
import zipfile

repo = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--work-dir', type=Path, default=repo / 'build/startup-profile')
p.add_argument('--revision', help='Git revision to copy; omit to copy the current working tree')
args = p.parse_args()
base = args.work_dir.resolve()
src = base / 'source'
src.mkdir(parents=True, exist_ok=False)
if args.revision:
    data = subprocess.check_output(['git', 'archive', '--format=zip', args.revision], cwd=repo)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for member in archive.infolist():
            target = (src / member.filename).resolve()
            if not target.is_relative_to(src):
                raise ValueError('Archive path escapes profiling directory')
        archive.extractall(src)
else:
    paths = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=repo)
    for name in set(paths.decode('utf-8').split('\0')):
        if not name or not (repo / name).is_file():
            continue
        target = src / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / name, target)

def edit(file, old, new, count=1):
    path = src / file
    text = path.read_text(encoding='utf-8')
    assert text.count(old) >= count, (file, old)
    path.write_text(text.replace(old, new, count), encoding='utf-8')

(src / 'include/startup_trace.h').write_text(r'''
#pragma once
#include "app.h"
#include "../tests/frame_preview.h"
#include <cstdio>
namespace startup_trace {
struct Event { const char* name; long long start, duration; int visible, runs, complete; };
inline std::vector<Event> events;
inline Clock::time_point origin;
inline long long loader = 0;
inline long long now() { return loader + usElapsed(origin); }
inline void begin() {
    FILETIME c,e,k,u,n;
    GetProcessTimes(GetCurrentProcess(), &c,&e,&k,&u);
    GetSystemTimePreciseAsFileTime(&n);
    origin = Clock::now();
    ULARGE_INTEGER a,b;
    a.LowPart=c.dwLowDateTime; a.HighPart=c.dwHighDateTime;
    b.LowPart=n.dwLowDateTime; b.HighPart=n.dwHighDateTime;
    loader=(b.QuadPart-a.QuadPart)/10;
    events.reserve(256);
    events.push_back({"loader",0,loader,0,0,0});
}
struct Scope {
    const char* name; App* app; long long start;
    Scope(const char* n, App* a=nullptr):name(n),app(a),start(now()) {}
    ~Scope() { events.push_back({name,start,now()-start,
        app && app->hwnd ? (int)IsWindowVisible(app->hwnd) : 0,
        app ? (int)app->layoutTextRuns.size() : 0, app ? (int)app->layoutComplete : 0}); }
};
inline void mark(const char* name, App* app=nullptr) { Scope s(name,app); }
inline void finish(App& app) {
    // All hashing and disk output happen after the timed first paint.
    uint64_t hash=14695981039346656037ull;
    auto add=[&](uint64_t v){hash=(hash^v)*1099511628211ull;};
    for(wchar_t c:app.docText)add(c);
    for(const auto& r:app.textRects) {
        add(r.docStart);add(r.docLength);
        for(float f:{r.rect.left,r.rect.top,r.rect.right,r.rect.bottom}) add((uint64_t)(int64_t)std::llround(f*100));
    }
    wchar_t path[32768]{}; GetEnvironmentVariableW(L"TINTA_STARTUP_LOG",path,32768);
    FILE* file=nullptr; _wfopen_s(&file,path,L"w");
    if(file) {
        fprintf(file,"event,start_us,duration_us,visible,text_runs,complete\n");
        for(const auto& e:events)fprintf(file,"%s,%lld,%lld,%d,%d,%d\n",e.name,e.start,e.duration,e.visible,e.runs,e.complete);
        fprintf(file,"hash,%llu,0,0,0,0\n",(unsigned long long)hash);
        fclose(file);
    }
    wchar_t frame[32768]{};
    const DWORD frameLength = GetEnvironmentVariableW(L"TINTA_STARTUP_FRAME",frame,32768);
    const bool captured = !frameLength || (frameLength < 32768 && saveFramePreview(app, frame));
    ExitProcess(file && captured ? 0 : 2);
}
}
''', encoding='utf-8')
for file in ['src/main_d2d.cpp','src/d2d_init.cpp','src/render.cpp']:
    path=src/file
    path.write_text('#include "startup_trace.h"\n'+path.read_text(encoding='utf-8'),encoding='utf-8')
edit('src/main_d2d.cpp','    SetUnhandledExceptionFilter(writeCrashDump);','    startup_trace::begin();\n    SetUnhandledExceptionFilter(writeCrashDump);')
edit('src/main_d2d.cpp','    if (!app.renderTarget) return;','    if (!app.renderTarget) return;\n    startup_trace::Scope traceFrame("render", &app);')
edit('src/main_d2d.cpp','    app.renderTarget->EndDraw();\n}', '    { startup_trace::Scope s("end_draw", &app); app.renderTarget->EndDraw(); }\n}')
for name,statement in [('themes','loadCustomThemes();'),('language','loadLanguageOverrides();'),('formats','updateTextFormats(app);'),('typography','createTypography(app);'),('recent','persistRecentFile(app.currentFile);'),('association','askAndRegisterFileAssociation(savedSettings);')]:
    edit('src/main_d2d.cpp','    '+statement,'    { startup_trace::Scope s("'+name+'", &app); '+statement+' }')
edit('src/main_d2d.cpp','    Settings savedSettings = loadSettings();','    Settings savedSettings;\n    { startup_trace::Scope s("settings"); savedSettings=loadSettings(); }')
edit('src/main_d2d.cpp','    // Create window with saved position/size','    startup_trace::mark("before_window");\n    // Create window with saved position/size')
edit('src/main_d2d.cpp','    app.metrics.windowInitUs = usElapsed(t0);','    app.metrics.windowInitUs = usElapsed(t0);\n    startup_trace::mark("after_window", &app);')
edit('src/main_d2d.cpp','    applyWindowChrome(app);','    { startup_trace::Scope s("chrome", &app); applyWindowChrome(app); }')
edit('src/main_d2d.cpp','    // Load document\n    t0 = Clock::now();','    // Load document\n    t0 = Clock::now();\n    startup_trace::mark("before_load", &app);')
edit('src/main_d2d.cpp','    app.metrics.fileLoadUs = usElapsed(t0);','    app.metrics.fileLoadUs = usElapsed(t0);\n    startup_trace::mark("after_load", &app);')
edit('src/main_d2d.cpp','    // Show window (respect saved maximized state)','    startup_trace::mark("before_show", &app);\n    // Show window (respect saved maximized state)')
edit('src/main_d2d.cpp','    UpdateWindow(app.hwnd);','    startup_trace::mark("after_show", &app);\n    UpdateWindow(app.hwnd);')
edit('src/main_d2d.cpp','    app.metrics.showWindowUs = usElapsed(t0);','    app.metrics.showWindowUs = usElapsed(t0);\n    startup_trace::mark("after_update", &app);\n    startup_trace::finish(app);')
for name,sig in [('d2d_init','bool initD2D(App& app) {'),('target','bool createRenderTarget(App& app) {')]:
    edit('src/d2d_init.cpp',sig,sig+'\n    startup_trace::Scope trace("'+name+'", &app);')
for name,sig in [('layout_begin','bool layoutBegin(App& app) {'),('first_layout','void layoutDocumentViewportFirst(App& app) {'),('full_layout','void layoutDocument(App& app) {')]:
    edit('src/render.cpp',sig,sig+'\n    startup_trace::Scope trace("'+name+'", &app);')
print(src)
