#include "tabs.h"
#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "settings.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <shellapi.h>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}
LRESULT CALLBACK dropTestProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_COPYDATA && app && lParam)
        return tabReceiveCopyData(*app, hwnd, *reinterpret_cast<COPYDATASTRUCT*>(lParam));
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
std::vector<App::TabHit> renderTabs(App& app) {
    app.renderTarget->BeginDraw();
    renderTabStrip(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "native tab strip renders");
    std::vector<App::TabHit> tabs;
    for (const auto& hit : app.tabHits) if (hit.index >= 0) tabs.push_back(hit);
    return tabs;
}
}

// The real detach path relaunches its own executable. In this isolated
// native test, that child records the launch arguments instead of opening
// a user-facing editor or entering the test suite recursively.
int runTabDropLaunchProbe() {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = fs::path(exe).parent_path();
    if (!fs::equivalent(dir, fs::current_path()) ||
        read(dir / "launch-probe-guard.txt") != "Tinta tab drop launch probe") return 2;
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 2;
    int result = 2;
    if (count == 7 && std::wstring(args[1]) == L"--cascade" &&
        std::wstring(args[2]) == L"--tabbed" && std::wstring(args[3]) == L"--pos") {
        std::ofstream out(dir / "launch-result.txt", std::ios::binary);
        out << toUtf8(args[4]) << '\n' << toUtf8(args[5]) << '\n' << toUtf8(args[6]);
        result = out ? 0 : 1;
    }
    LocalFree(args);
    return result;
}

int runTabDropTests() {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = fs::path(exe).parent_path();
    const auto settings = read(dir / "settings.ini");
    if (!fs::equivalent(dir, fs::current_path()) ||
        (settings != "; Isolated tab drop test configuration\n" &&
         settings != "; Isolated tab drop test configuration\r\n")) {
        std::cerr << "Run through CTest's isolated tab-drop wrapper\n";
        return 2;
    }
    check(fs::equivalent(tintaConfigDir(), dir), "all persistence stays in the test directory");
    const std::string fixture = read(TINTA_TAB_DROP_FIXTURE);
    check(!fixture.empty(), "mixed Markdown fixture loads");
    const wchar_t* names[] = {L"A.md", L"B.md", L"C.md", L"Incoming \u03b4.md", L"First.md", L"Last.md", L"Normal.md", L"Content.md"};
    std::vector<std::string> paths;
    for (const auto name : names) {
        const auto path = dir / name;
        std::ofstream(path, std::ios::binary) << fixture;
        paths.push_back(toUtf8(path.wstring()));
    }
    WNDCLASSW wc{};
    wc.lpfnWndProc = dropTestProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TintaTabDropTest";
    RegisterClassW(&wc);
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Tab drop native tests", WS_POPUP,
        -1200, 100, 1050, 900, nullptr, nullptr, wc.hInstance, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        std::cerr << "Cannot initialize native tab-drop test window\n";
        return 1;
    }
    SetWindowLongPtrW(app.hwnd, GWLP_USERDATA, (LONG_PTR)&app);
    app.currentFile = paths[1];
    tabsSeedSession(app, {paths[0], paths[1], paths[2]});
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float scale : {1.0f, 1.5f, 2.0f}) {
            app.contentScale = scale;
            updateTextFormats(app);
            for (int width : {650, 1050}) {
                app.width = (int)(width * scale);
                app.height = (int)(900 * scale);
                for (bool edit : {false, true}) {
                    app.editMode = edit;
                    app.editRailAnim = 1;
                    const auto hits = renderTabs(app);
                    check(hits.size() == 3, "all three existing tabs render");
                    for (const auto& hit : hits) {
                        const int middle = (int)((hit.rect.left + hit.rect.right) / 2);
                        if (middle + 2 >= captionIslandLeft(app) ||
                            (editorPreviewVisible(app) && middle + 2 >= editorPaneWidth(app))) continue;
                        check(tabDropInsertionIndex(app, {middle-2, (LONG)dpi(app, 20)}) == hit.index,
                              "left half inserts before the destination tab");
                        check(tabDropInsertionIndex(app, {middle+2, (LONG)dpi(app, 20)}) == hit.index+1,
                              "right half inserts after the destination tab");
                    }
                    check(tabDropInsertionIndex(app, {100, (LONG)chromeTopHeight(app)+20}) == 3,
                          "content-area drops keep appending");
                }
            }
        }
    }
    app.editMode = false;
    app.width = 1050;
    app.height = 900;
    app.contentScale = 1;
    updateTextFormats(app);
    check(openDocumentInViewer(app, toWide(paths[1])), "active B document opens");
    const std::wstring dirty = L"# Unsaved B\n\n| Keep | Value |\n| --- | --- |\n| Math | $x^2$ |\n";
    restoreEditBuffer(app, dirty, true, 17.0f, 12);
    auto sendAt = [&](const std::string& path, POINT client) {
        POINT screen = client;
        ClientToScreen(app.hwnd, &screen);
        return tabSendDrop(app.hwnd, path, screen);
    };
    auto hits = renderTabs(app);
    POINT gap{(LONG)((hits[0].rect.right+hits[1].rect.left)/2), 20};
    check(sendAt(paths[3], gap), "positioned UTF-8 transfer is acknowledged via WM_COPYDATA");
    check(app.tabs.size() == 4 && app.tabs[0].path == paths[0] && app.tabs[1].path == paths[3] &&
          app.tabs[2].path == paths[1] && app.tabs[3].path == paths[2], "drop inserts A, Incoming, B, C");
    check(app.activeTab == 1 && app.currentFile == paths[3], "incoming tab becomes active");
    check(app.tabs[2].editorDirty && app.tabs[2].editorText == dirty && app.tabs[2].editorCursor == 12,
          "insertion before active B parks its dirty buffer in B, not the incoming tab");
    tabActivate(app, app.hwnd, 2);
    check(app.editMode && app.editorDirty && app.editorText == dirty && app.editorCursorPos == 12,
          "returning to B restores its unsaved text and cursor");
    hits = renderTabs(app);
    check(sendAt(paths[4], {(LONG)hits[0].rect.left+2, 20}), "drop before first tab succeeds");
    check(app.tabs.front().path == paths[4] && app.activeTab == 0, "new tab can be first");
    hits = renderTabs(app);
    check(sendAt(paths[5], {(LONG)hits.back().rect.right+2, 20}), "drop after last tab succeeds");
    check(app.tabs.back().path == paths[5] && app.activeTab == 5, "new tab can be last");
    check(sendAt(paths[7], {900, 400}), "content drop succeeds");
    check(app.tabs.back().path == paths[7] && app.activeTab == 6, "content drop still appends");
    // Legacy launch messages append without interpreting the current cursor.
    COPYDATASTRUCT legacy{1, (DWORD)paths[6].size()+1, paths[6].data()};
    check(SendMessageW(app.hwnd, WM_COPYDATA, 0, (LPARAM)&legacy) != FALSE, "legacy path-only message accepted");
    check(app.tabs.back().path == paths[6] && app.activeTab == 7, "ordinary launch still appends");
    const auto count = app.tabs.size();
    check(sendAt(paths[1], {42, 20}), "already-open path accepted");
    check(app.tabs.size() == count && app.activeTab == 3 && app.editorDirty && app.editorText == dirty,
          "duplicate drop activates the existing dirty tab without duplicating or moving it");
    char malformed[] = "bad";
    COPYDATASTRUCT invalid{2, 3, malformed};
    check(!tabReceiveCopyData(app, app.hwnd, invalid), "truncated drop payload rejected");
    invalid.dwData = 1;
    check(!tabReceiveCopyData(app, app.hwnd, invalid), "unterminated path rejected");
    invalid = {99, sizeof(malformed), malformed};
    check(!tabReceiveCopyData(app, app.hwnd, invalid), "unknown message type rejected");
    check(!tabSendDrop(nullptr, paths[0], {0, 0}), "unavailable recipient cannot acknowledge a transfer");
    check(app.tabs.size() == count && app.editorText == dirty, "rejected transfers leave destination unchanged");

    // Exercise the actual drag lifecycle, including its ghost, without
    // injecting mouse input into any desktop window.
    app.editMode = false;
    app.editorDirty = false;
    auto armDrag = [&]() {
        app.currentFile = paths[3];
        tabsSeedSession(app, {paths[0], paths[3], paths[2]});
        const auto row = renderTabs(app);
        int x = (int)((row[1].rect.left+row[1].rect.right)/2);
        app.tabDragIndex = 1;
        app.tabDragging = false;
        app.tabDragStartX = x;
        app.tabDragStartY = (int)dpi(app, 20);
        return x;
    };
    for (float scale : {1.0f, 1.5f, 2.0f}) {
        app.contentScale = scale;
        app.width = (int)(1050*scale);
        updateTextFormats(app);
        const int x = armDrag();
        const int edge = (int)chromeTopHeight(app);
        tabDragMove(app, app.hwnd, x, edge+(int)dpi(app, 47));
        check(!app.tabDragDetached, "staying near the tab bar does not detach");
        tabDragMove(app, app.hwnd, x, edge+(int)dpi(app, 50));
        check(app.tabDragDetached && app.tabGhostWnd, "about 50 pixels below the bar shows the drag ghost");
        tabDragMove(app, app.hwnd, x, edge+(int)dpi(app, 10));
        check(!app.tabDragDetached && !app.tabGhostWnd, "returning near the bar cancels detachment");
        tabDragMove(app, app.hwnd, x, -(int)dpi(app, 50));
        check(app.tabDragDetached, "about 50 pixels above the bar also detaches");
        tabDragCancel(app, app.hwnd);
        check(app.tabDragIndex == -1 && !app.tabGhostWnd && app.tabs.size() == 3,
              "cancelling the drag keeps every tab and removes its ghost");
    }
    app.contentScale = 1;
    app.width = 1050;
    updateTextFormats(app);
    const int detachX = armDrag();
    // Put only our test window outside the virtual desktop. Making it
    // visible there lets WindowFromPoint exercise the original snap-back
    // bug while leaving the user's workspace and pointer alone.
    SetWindowPos(app.hwnd, HWND_BOTTOM, GetSystemMetrics(SM_XVIRTUALSCREEN)-1600,
        GetSystemMetrics(SM_YVIRTUALSCREEN)-1200, 1050, 900, SWP_NOACTIVATE|SWP_SHOWWINDOW);
    const int detachY = (int)chromeTopHeight(app)+50;
    POINT dropPoint{detachX, detachY};
    ClientToScreen(app.hwnd, &dropPoint);
    check(WindowFromPoint(dropPoint) == app.hwnd, "release point is over the source document, not outside its window");
    fs::remove(dir / "launch-result.txt");
    tabDragMove(app, app.hwnd, detachX, detachY);
    tabDragEnd(app, app.hwnd, detachX, detachY);
    const std::string expectedLaunch = std::to_string(dropPoint.x-120) + "\n" +
        std::to_string(dropPoint.y-20) + "\n" + paths[3];
    const auto deadline = GetTickCount64()+5000;
    while (read(dir / "launch-result.txt") != expectedLaunch && GetTickCount64() < deadline) Sleep(10);
    check(read(dir / "launch-result.txt") == expectedLaunch,
          "releasing over the source content launches a positioned window with the Unicode path intact");
    check(app.tabs.size() == 2 && app.tabs[0].path == paths[0] && app.tabs[1].path == paths[2],
          "successful tear-off removes only the transferred tab");
    ShowWindow(app.hwnd, SW_HIDE);
    app.tabs.resize(1);
    app.activeTab = 0;
    app.forceTabStrip = false;
    check(tabDropInsertionIndex(app, {45, 20}) == 1, "tabless destination keeps append behavior");
    app.forceTabStrip = true;
    hits = renderTabs(app);
    check(tabDropInsertionIndex(app, {(LONG)hits[0].rect.left+2, 20}) == 0,
          "single-tab satellite can accept insertion before its tab");
    app.zenMode = true;
    check(tabDropInsertionIndex(app, {45, 20}) == 1, "hidden zen strip has no insertion positions");
    for (const auto& path : paths) check(read(fs::path(toWide(path))) == fixture, "document transfers do not modify Markdown on disk");
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    std::cout << "Tab drops: " << failures << " failures\n";
    return failures ? 1 : 0;
}
