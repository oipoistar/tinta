// PlantUML app bridge: reads and writes App state around the app-free
// plantuml:: core. No Direct2D calls - this translation unit only touches
// Settings/App fields and the settings file, mirroring pandoc.cpp's
// pandocResolve / pandocAvailable / pandocSetUserPath structure without
// the installer probes pandoc needs (PlantUML has none).

#include "plantuml_app.h"

#include "plantuml.h"
#include "plantuml_queue.h"
#include "settings.h"

#include <string>

namespace {

std::string toUtf8Str(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len,
                        nullptr, nullptr);
    return out;
}

}  // namespace

void plantumlResolve(App& app) {
    if (app.plantumlChecked) return;
    app.plantumlChecked = true;

    // 1. The saved path (exe or jar) wins, then 2. plantuml.exe on PATH;
    // 3. none. A saved path that no longer exists stays unavailable - it is
    // an explicit choice, not a hint.
    app.plantumlTool = plantuml::resolveToolWithPathSearch(app.plantumlUserPath);
}

bool plantumlAvailable(App& app) {
    plantumlResolve(app);
    return app.plantumlTool.available;
}

void plantumlSetUserPath(App& app, const std::wstring& path) {
    app.plantumlUserPath = path;
    app.plantumlChecked = false;
    plantumlResolve(app);
    Settings settings = loadSettings();
    settings.plantumlPath = toUtf8Str(path);
    saveSettings(settings);
}

void plantumlEnsureQueue(App& app) {
    if (app.plantumlQueue) return;
    app.plantumlQueue = std::make_unique<plantuml::PlantumlRenderQueue>();

    // The completion callback fires FROM THE WORKER THREAD: it may only
    // marshal (PostMessageW) and must never touch App state or the cache.
    // The HWND is captured once; it exists before any layout runs.
    HWND hwnd = app.hwnd;
    app.plantumlQueue->setCompletion([hwnd](uint64_t key, bool) {
        if (hwnd) PostMessageW(hwnd, WM_APP_PLANTUML_READY, (WPARAM)key, 0);
    });

    // One work root per process; the queue names each render's directory
    // with keyHex(key) underneath it (LRU eviction and shutdown delete them).
    if (app.plantumlWorkRoot.empty()) {
        wchar_t temp[MAX_PATH] = {};
        if (GetTempPathW(MAX_PATH, temp) == 0) {
            temp[0] = L'.';
            temp[1] = L'\0';  // no temp path: fall back to the cwd
        }
        app.plantumlWorkRoot = std::wstring(temp) + L"tinta-plantuml-" +
                               std::to_wstring(GetCurrentProcessId());
    }
}