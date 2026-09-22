#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "overlays.h"
#include "render.h"
#include "tabs.h"
#include "utils.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <windowsx.h>

namespace {
int failures = 0, moveRequests = 0;
POINT requestedPoint{};
void check(bool value, const char* message) {
    if (!value) { if (failures < 30) std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
LRESULT CALLBACK testProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCLBUTTONDOWN && wParam == HTCAPTION) {
        ++moveRequests;
        requestedPoint = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        check(app && !app->appMenuPressed && GetCapture() != hwnd, "capture released before native window move");
        return 0; // Observe the real native move request without entering its modal loop.
    }
    if (app && ((msg == WM_CAPTURECHANGED && (HWND)lParam != hwnd) || msg == WM_CANCELMODE))
        cancelAppMenuPress(*app, hwnd);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void gestures(App& app) {
    const int x = (int)dpi(app, 20), y = (int)dpi(app, 20);
    const LPARAM point = MAKELPARAM(x, y);
    const UINT windowDpi = GetDpiForWindow(app.hwnd);
    const int dx = std::max(1, GetSystemMetricsForDpi(SM_CXDRAG, windowDpi));
    const int dy = std::max(1, GetSystemMetricsForDpi(SM_CYDRAG, windowDpi));
    auto press = [&] { handleMouseDown(app, app.hwnd, MK_LBUTTON, point); };
    auto release = [&](LPARAM at) { handleMouseUp(app, app.hwnd, 0, at); };
    closeContextMenu(app);
    press();
    check(app.appMenuPressed && GetCapture() == app.hwnd && !app.showContextMenu, "icon press arms a gesture without opening menu");
    int moves = moveRequests;
    handleMouseMove(app, app.hwnd, MAKELPARAM(x + dx - 1, y + dy - 1));
    check(app.appMenuPressed && moveRequests == moves, "small pointer movement remains a click");
    release(point);
    check(app.showContextMenu && app.applicationMenu && !app.appMenuPressed && GetCapture() != app.hwnd,
          "icon release opens menu and ends capture");
    press();
    release(point);
    check(!app.showContextMenu, "a second click closes the icon menu");
    for (POINT delta : {POINT{dx, 0}, POINT{-dx, 0}, POINT{0, dy}, POINT{0, -dy}}) {
        press();
        moves = moveRequests;
        const LPARAM moved = MAKELPARAM(x + delta.x, y + delta.y);
        handleMouseMove(app, app.hwnd, moved);
        POINT expected{x, y};
        ClientToScreen(app.hwnd, &expected);
        check(moveRequests == moves + 1 && requestedPoint.x == expected.x && requestedPoint.y == expected.y,
              "threshold crossing hands off to native dragging at the original screen grab point");
        check(!app.appMenuPressed && !app.showContextMenu && !app.mouseDown && !app.selecting,
              "drag never opens menu or starts selection");
        release(moved);
        check(!app.swallowNextMouseUp && !app.showContextMenu, "drag release is consumed");
    }
    openContextMenu(app, 4, chromeTopHeight(app), true);
    press();
    handleMouseMove(app, app.hwnd, MAKELPARAM(x + dx, y));
    release(point);
    check(!app.showContextMenu, "dragging an icon with its menu open dismisses the menu");
    for (int cancel = 0; cancel < 3; ++cancel) {
        press();
        if (cancel == 0) handleKeyDown(app, app.hwnd, VK_ESCAPE);
        else if (cancel == 1) ReleaseCapture();
        else SendMessageW(app.hwnd, WM_CANCELMODE, 0, 0);
        check(!app.appMenuPressed && GetCapture() != app.hwnd, "Escape, capture loss and cancel mode cancel the press");
        release(point);
        check(!app.showContextMenu, "cancelled gesture cannot reopen the menu on release");
    }
    press();
    release(MAKELPARAM((int)appMenuButtonRect(app).right + 1, y));
    check(!app.showContextMenu && !app.appMenuPressed, "release outside the icon without a move event cancels click");
    press();
    release(point);
    handleKeyDown(app, app.hwnd, VK_ESCAPE);
    handleKeyDown(app, app.hwnd, VK_F10);
    check(app.showContextMenu && app.contextMenuKeyboard, "F10 still opens the keyboard menu");
    handleKeyDown(app, app.hwnd, VK_ESCAPE);
    app.confirmExitPending = true;
    moves = moveRequests;
    press();
    handleMouseMove(app, app.hwnd, MAKELPARAM(x + dx, y));
    release(point);
    check(!app.appMenuPressed && !app.showContextMenu && moves == moveRequests, "modal confirmation blocks icon gesture");
    app.confirmExitPending = false;
}

void stripLayout(App& app) {
    app.renderTarget->BeginDraw();
    renderTabStrip(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "native title strip renders");
    const auto drag = titleDragRect(app);
    if (app.editMode && app.editorReadingPreview) {
        check(drag.right >= captionIslandLeft(app),
              "the reading view keeps the reader's drag area beside the window buttons (#242)");
    }
    if (app.width >= dpi(app, 500)) {
        check(std::abs(drag.right - drag.left - dpi(app, 32)) < 0.1f, "32 logical pixels remain available for dragging");
    } else {
        check(drag.right - drag.left >= dpi(app, 12) && drag.right - drag.left <= dpi(app, 32),
              "extremely narrow windows retain a usable gap without sacrificing tab context targets");
    }
    bool active = false, overflow = false, plus = false;
    int visible = 0;
    for (const auto& hit : app.tabHits) {
        check(hit.rect.right <= drag.left + 0.1f || hit.rect.left >= drag.right - 0.1f,
              "interactive controls never cover the reserved drag area");
        if (hit.index == -4) continue; // pin belongs to the caption island
        check(hit.rect.left >= appMenuButtonRect(app).right && hit.rect.right <= drag.left + 0.1f,
              "tabs and controls stay between icon and drag area");
        if (hit.index == -3) overflow = true;
        if (hit.index == -2) plus = true;
        if (hit.index < 0) continue;
        ++visible;
        active |= hit.index == app.activeTab;
        if (tabStripVisible(app)) {
            const int mid = (int)((hit.rect.left + hit.rect.right) / 2);
            check(tabDropInsertionIndex(app, {mid - 2, (LONG)dpi(app, 20)}) == hit.index &&
                  tabDropInsertionIndex(app, {mid + 2, (LONG)dpi(app, 20)}) == hit.index + 1,
                  "drops use absolute tab indices when leading tabs are hidden");
        }
    }
    check(active && plus, "active tab and new-tab control stay reachable");
    check(visible == (int)app.tabs.size() || overflow, "hidden tabs remain reachable through the switcher");
}
}

int runWindowDragTests() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = testProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TintaWindowDragTest";
    RegisterClassW(&wc);
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Window drag regression", WS_POPUP,
            -1100, 50, 1050, 900, nullptr, nullptr, wc.hInstance, nullptr);
        if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
        SetWindowLongPtrW(app.hwnd, GWLP_USERDATA, (LONG_PTR)&app);
        const auto fixture = std::filesystem::path(TINTA_TAB_DROP_FIXTURE).parent_path() / "window-drag-238.md";
        std::ifstream input(fixture);
        std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        check(!source.empty(), "mixed fixture loads");
        app.root = app.parser.parse(source).root;
        app.currentFile = toUtf8(fixture.wstring());
        app.editorText = toWide(source);
        app.editorCursorPos = 6;
        app.editorDirty = true;
        app.editRailAnim = 1;
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f, 2.0f}) {
                app.contentScale = scale;
                updateTextFormats(app);
                for (int width : {325, 500, 650, 1050}) {
                    app.width = (int)(width * scale);
                    app.height = (int)(700 * scale);
                    // Reader, split editor, and the full-width reading view
                    // of unsaved edits, which keeps the reader's title bar (#242)
                    for (int mode : {0, 1, 2}) {
                        app.editMode = mode > 0;
                        app.editorReadingPreview = mode == 2;
                        gestures(app);
                        check(app.editorText == toWide(source) && app.editorCursorPos == 6 && app.editorDirty,
                              "window gestures preserve unsaved source and cursor");
                        for (int count : {1, 3, 12}) {
                            app.tabs.clear();
                            app.tabs.resize(count);
                            for (int i = 0; i < count; ++i) app.tabs[i].title = L"Document " + std::to_wstring(i);
                            for (int active : {0, count / 2, count - 1}) {
                                app.activeTab = active;
                                stripLayout(app);
                            }
                        }
                        app.editMode = false;
                        app.editorReadingPreview = false;
                        layoutDocument(app);
                        check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1 &&
                              app.docText.find(L"Final heading") != std::wstring::npos,
                              "mixed Markdown remains intact after title-bar gestures");
                    }
                }
            }
        }
        DestroyWindow(app.hwnd);
        app.hwnd = nullptr;
    }
    CoUninitialize();
    std::cout << "Window drag: " << failures << " failures\n";
    return failures ? 1 : 0;
}
