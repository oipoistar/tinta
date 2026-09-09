#include "d2d_init.h"
#include "input.h"
#include "overlays.h"
#include "tabs.h"
#include "utils.h"

#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <thread>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

std::wstring clipboardText(HWND window) {
    std::wstring result;
    // Clipboard-history tools may briefly read a newly copied value.
    bool opened = false;
    for (int i = 0; i < 100 && !(opened = OpenClipboard(window)); ++i) Sleep(5);
    if (!opened) { check(false, "test clipboard opens for inspection"); return {}; }
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (auto text = static_cast<const wchar_t*>(GlobalLock(data))) {
            result = text;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return result;
}
void documentCopy(App& app) {
    openContextMenu(app, 20, 80);
    app.hoveredContextMenuItem = CTX_COPY_PATH;
    handleKeyDown(app, app.hwnd, VK_RETURN);
    check(!app.showContextMenu, "document Copy file path closes the menu");
}
void clickTabCopy(App& app) {
    // Copy file path follows the four close actions and their separator.
    int x = static_cast<int>(app.tabMenuX + 20);
    int y = static_cast<int>(app.tabMenuY + dpi(app, 6+4*30+9+15));
    check(tabMenuItemAt(app, x, y) == 4, "tab Copy file path remains next to Reveal");
    check(tabMenuMouseDown(app, app.hwnd, x, y), "tab path action consumes its click");
    check(!app.showTabMenu, "tab Copy file path closes the menu");
}
void tabCopy(App& app, int index) {
    openTabMenu(app, index, 20, 40);
    clickTabCopy(app);
}
void renderStrip(App& app) {
    app.renderTarget->BeginDraw();
    renderTabStrip(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "native title strip renders");
}
void rightClick(App& app, int x, int y) {
    closeTabMenu(app);
    POINT point{x, y};
    ClientToScreen(app.hwnd, &point);
    handleContextMenu(app, app.hwnd, MAKELPARAM(point.x, point.y));
}
void titleMenuCases(bool testClipboard) {
    // Render the actual caption into a hidden test window so these checks
    // catch missing hit regions, rather than opening a tab menu directly.
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Tinta title test", WS_POPUP,
        100, 100, 1050, 900, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        check(false, "native caption test initializes");
        if (app.hwnd) DestroyWindow(app.hwnd);
        return;
    }
    app.height = 900;
    app.editRailAnim = 1;
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float scale : {1.0f, 1.5f, 2.0f}) {
            app.contentScale = scale;
            updateTextFormats(app);
            for (int width : {650, 1050}) {
                app.width = width;
                for (bool edit : {false, true}) {
                    app.editMode = edit;
                    app.editorShowPreview = edit;
                    for (const std::string& path : {std::string(u8"C:\\Tinta tests\\\u017divot \u6f22\u5b57.md"),
                            "C:\\Tinta tests\\" + std::string(120, 'a') + ".md"}) {
                        app.currentFile = path;
                        app.tabs.resize(2);
                        app.tabs[1].path = "C:\\companion.md";
                        app.tabs[1].title = L"companion.md";
                        app.forceTabStrip = false;
                        renderStrip(app);
                        for (const auto& hit : app.tabHits) {
                            if (hit.index < 0) continue;
                            rightClick(app, (int)(hit.rect.left+5), (int)dpi(app, 20));
                            check(app.showTabMenu && app.tabMenuIndex == hit.index,
                                  "both tab headers open their own context menu");
                        }
                        app.tabs.resize(1);  // last companion closed
                        renderStrip(app);
                        int x = (int)dpi(app, edit ? 60 : 48);
                        int y = (int)dpi(app, 20);
                        check(!tabStripVisible(app) && tabContextMenuIndexAt(app, (float)x, (float)y) == 0,
                              "the lone title has a tab context target after closing a companion");
                        for (const auto& hit : app.tabHits)
                            check(!(x >= hit.rect.left && x <= hit.rect.right &&
                                    y >= hit.rect.top && y <= hit.rect.bottom),
                                  "the lone title retains non-client window dragging");
                        rightClick(app, x, y);
                        check(app.showTabMenu && app.tabMenuIndex == 0,
                              "right-click routing opens the lone document's tab menu");
                        if (testClipboard && app.showTabMenu) {
                            clickTabCopy(app);
                            check(clipboardText(app.hwnd) == toWide(path),
                                  "single-title menu copies the full path");
                        }
                        check(tabContextMenuIndexAt(app, dpi(app, 20), (float)y) == -1,
                              "the app icon does not open the tab menu");
                        for (const auto& hit : app.tabHits)
                            if (hit.index < 0)
                                check(tabContextMenuIndexAt(app, (hit.rect.left+hit.rect.right)/2,
                                      (hit.rect.top+hit.rect.bottom)/2) == -1,
                                      "plus, pin and caption controls remain separate");
                        check(tabContextMenuIndexAt(app, (float)width/2, dpi(app, 50)) == -1,
                              "document content has no tab context target");
                        app.zenMode = true;
                        check(tabContextMenuIndexAt(app, (float)x, (float)y) == -1,
                              "zen mode cannot use stale caption hit regions");
                        app.zenMode = false;
                        app.forceTabStrip = true;
                        renderStrip(app);
                        rightClick(app, (int)dpi(app, 60), y);
                        check(app.showTabMenu && app.tabMenuIndex == 0,
                              "a detached single-tab window retains its tab menu");
                    }
                }
            }
        }
    }
    app.currentFile.clear();
    app.editMode = false;
    app.forceTabStrip = false;
    renderStrip(app);
    check(tabContextMenuIndexAt(app, dpi(app, 48), dpi(app, 20)) == -1,
          "the start page has no document title menu");
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
}
}

int main(int argc, char** argv) {
    // Default CTest runs are read-only. The explicit clipboard mode also
    // exercises the real menus/Win32 clipboard and overwrites its contents.
    bool testClipboard = argc == 2 && std::string(argv[1]) == "--clipboard-test";
    titleMenuCases(testClipboard);
    auto state = std::make_unique<App>();
    App& app = *state;
    if (testClipboard) {
        app.hwnd = CreateWindowExW(0, L"STATIC", L"Tinta clipboard test", 0,
            0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
        if (!app.hwnd) return 2;
    }
    app.width = 1050;
    app.height = 900;
    app.tabs.resize(2);
    app.activeTab = 0;
    app.tabs[0].path = "C:\\old-name.md";  // live path is authoritative after Save As
    app.tabs[1].path = "C:\\Tinta tests\\inactive document.md";

    std::string longPath = "C:\\Tinta tests";
    for (int i = 0; i < 20; ++i) longPath += "\\long folder name";
    longPath += "\\document.md";
    std::wstring relativeExpected =
        (std::filesystem::current_path() / L"tests" / L"fixtures" / L"copy-file-path.md").wstring();
    for (const auto& item : {
            std::pair<std::string, std::wstring>{"tests\\..\\tests\\fixtures\\copy-file-path.md", relativeExpected},
            {"C:/Tinta tests/new name.md", L"C:\\Tinta tests\\new name.md"},
            {u8"C:\\Tinta tests\\\u017divot \u6f22\u5b57.md", L"C:\\Tinta tests\\\u017divot \u6f22\u5b57.md"},
            {"\\\\server\\share\\folder name\\document.md", L"\\\\server\\share\\folder name\\document.md"},
            {longPath, toWide(longPath)}, {"\\\\?\\"+longPath, L"\\\\?\\"+toWide(longPath)}}) {
        app.currentFile = item.first;
        check(absoluteFilePath(item.first) == item.second, "full paths resolve without quoting or truncation");
        if (!testClipboard) continue;
        documentCopy(app);
        check(clipboardText(app.hwnd) == item.second, "document action copies the full Unicode path");
        tabCopy(app, 0);
        check(clipboardText(app.hwnd) == item.second, "active-tab action uses the live path, not its saved snapshot");
    }
    if (!testClipboard) {
        std::cout << "Full file paths: " << failures << " failures (clipboard checks require --clipboard-test)\n";
        return failures ? 1 : 0;
    }
    app.editMode = true;
    app.editorText = L"# Unsaved edits\n\n$x^2$\n";
    app.editorDirty = true;
    tabCopy(app, 1);
    check(clipboardText(app.hwnd) == L"C:\\Tinta tests\\inactive document.md", "inactive-tab action copies the clicked tab");
    check(app.activeTab == 0 && app.editorDirty && app.editorText == L"# Unsaved edits\n\n$x^2$\n",
          "copying an inactive path preserves the active editor and unsaved changes");

    check(copyToClipboard(app.hwnd, L"sentinel"), "test clipboard writes Unicode text");
    check(!copyToClipboard(app.hwnd, L""), "empty clipboard requests do nothing");
    app.currentFile.clear();
    app.signalChips.clear();
    documentCopy(app);
    tabCopy(app, 0);
    check(clipboardText(app.hwnd) == L"sentinel" && app.signalChips.empty(),
          "untitled documents keep the clipboard and show no copy confirmation");
    app.tabs[1].path.clear();
    tabCopy(app, 1);
    check(clipboardText(app.hwnd) == L"sentinel", "untitled inactive tab cannot copy another document's path");
    app.editMode = false;
    app.startPageEmbeddedOpen = true;
    documentCopy(app);
    check(clipboardText(app.hwnd) == L"sentinel", "embedded help/start page has no file path");

    std::promise<bool> opened;
    std::promise<void> release;
    auto released = release.get_future();
    std::thread locker([&] {
        bool locked = OpenClipboard(nullptr) != 0;
        opened.set_value(locked);
        released.wait();
        if (locked) CloseClipboard();
    });
    bool locked = opened.get_future().get();
    check(locked, "another thread can hold the test clipboard");
    if (locked) {
        app.signalChips.clear();
        copyFilePath(app, app.hwnd, "C:\\document.md");
        check(app.signalChips.empty(), "a busy clipboard does not produce a false Copied confirmation");
    }
    release.set_value();
    locker.join();
    check(clipboardText(app.hwnd) == L"sentinel", "clipboard contention preserves its previous content");
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    std::cout << "Copy file path: " << failures << " failures\n";
    return failures ? 1 : 0;
}
