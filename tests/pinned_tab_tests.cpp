// Pinned tabs and the remembered window pin (#312): a tab pinned from its
// menu leads the row, narrow and always in view; the sweeps spare it, drags
// and drops keep the two groups apart, and the session brings it back
// pinned. The title bar's pin is kept in settings.ini when pressed. Driven
// through the strip's and the menu's handlers, in a folder of its own.
#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string pathOf(const App& app, int i) {
    return i == app.activeTab ? app.currentFile : app.tabs[i].path;
}
int indexOf(const App& app, const fs::path& file) {
    for (int i = 0; i < (int)app.tabs.size(); i++)
        if (_stricmp(pathOf(app, i).c_str(), toUtf8(file.wstring()).c_str()) == 0) return i;
    return -1;
}
int pinnedCount(const App& app) {
    int n = 0;
    for (const auto& tab : app.tabs) n += tab.pinned;
    return n;
}

void paintStrip(App& app) {
    app.renderTarget->BeginDraw();
    app.renderTarget->Clear(app.theme.background);
    renderTabStrip(app);
    renderTabMenu(app);
    app.renderTarget->EndDraw();
}

const App::TabHit* hitOf(const App& app, int index) {
    for (const auto& hit : app.tabHits)
        if (hit.index == index) return &hit;
    return nullptr;
}

// Right-click tab i, then click the menu's row for `item` (0 Close, 1 Close
// all but this, 2 left, 3 right, 4 Pin or Unpin)
void menuPick(App& app, int i, int item) {
    paintStrip(app);
    const App::TabHit* hit = hitOf(app, i);
    check(hit != nullptr, "the tab is drawn in the strip");
    if (!hit) return;
    openTabMenu(app, i, hit->rect.left + 10, hit->rect.bottom);
    paintStrip(app);
    const float x = app.tabMenuX + 40;
    for (float y = app.tabMenuY; y < app.tabMenuY + 400; y += 2) {
        if (tabMenuItemAt(app, x, y) == item) {
            tabMenuMouseDown(app, app.hwnd, (int)x, (int)y);
            return;
        }
    }
    check(false, "the menu has the row");
}
}  // namespace

int runPinnedTabTests() {
    const fs::path dir = fs::current_path() / "notes";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::copy(TINTA_PINNED_TABS_FIXTURE_DIR, dir, fs::copy_options::recursive, ec);
    check(!ec, "the fixture copies");
    std::vector<fs::path> files;
    for (const char* name : {"Design review.md", "Agenda.md", "Budget 2026.md", "Changelog notes.md"}) files.push_back(dir / name);
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::wstring name = entry.path().filename().wstring();
        if (name[0] > 0x7F) files.push_back(entry.path());  // the Turkish and the Chinese notes
    }
    check(files.size() == 6, "six notes, two named past ASCII");

    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Pinned tab tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr,
                               nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);

    check(openDocumentInViewer(app, files[0].wstring()), "the first note opens");
    tabsInit(app);
    for (size_t i = 1; i < files.size(); i++) tabOpenPath(app, app.hwnd, toUtf8(files[i].wstring()), false);
    check(app.tabs.size() == 6 && app.activeTab == 0 && tabStripVisible(app), "six tabs, the first active");
    paintStrip(app);
    const App::TabHit* wide = hitOf(app, 1);
    const float wideWidth = wide ? wide->rect.right - wide->rect.left : 0.0f;

    // Pin from the menu: the tab leads the row, narrow, with no close button
    menuPick(app, indexOf(app, files[2]), 4);
    check(tabsPinnedCount(app) == 1 && indexOf(app, files[2]) == 0 && app.tabs[0].pinned,
          "#312: Pin tab moves the tab to the row's start, pinned");
    check(pathOf(app, app.activeTab) == toUtf8(files[0].wstring()), "#312: the active tab stays the same document");
    menuPick(app, indexOf(app, files[4]), 4);
    check(tabsPinnedCount(app) == 2 && indexOf(app, files[4]) == 1, "#312: a second one goes after the first");
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            updateTextFormats(app);
            paintStrip(app);
            const App::TabHit* first = hitOf(app, 0);
            const App::TabHit* second = hitOf(app, 1);
            const App::TabHit* other = hitOf(app, 2);
            const std::string where = " (theme " + std::to_string(theme) + ", " + std::to_string(width) + " px)";
            check(first && second && other, "#312: the pinned tabs and the next are drawn" + where);
            if (!first || !second || !other) continue;
            const float pinnedWidth = first->rect.right - first->rect.left;
            check(pinnedWidth < 50.0f && pinnedWidth < wideWidth / 2 &&
                      std::abs((second->rect.right - second->rect.left) - pinnedWidth) < 0.5f,
                  "#312: pinned tabs are narrow, all one width" + where);
            check(first->rect.right <= second->rect.left && second->rect.right <= other->rect.left,
                  "#312: in order, the others after them" + where);
            check(!first->hasClose && !second->hasClose, "#312: a pinned tab has no close button" + where);
        }
    }
    app.width = 1050;
    applyTheme(app, 0);
    updateTextFormats(app);

    // Resting on a pinned tab brings up its card, which names it
    paintStrip(app);
    if (const App::TabHit* first = hitOf(app, 0)) {
        handleMouseMove(app, app.hwnd, MAKELPARAM((int)(first->rect.left + 10), (int)(first->rect.top + 8)));
        check(app.hoveredTab == 0 && (app.tabHoverCardPending == 0 || app.tabHoverCardTab == 0) &&
                  tabHoverCardText(app, 0).name.find(L"Budget 2026") != std::wstring::npos,
              "#312: resting on a pinned tab brings up the card with its name");
        tabHoverCardDismiss(app, app.hwnd);
    }

    // A pinned tab's menu unpins it; the left sweep has nothing left of the
    // second pinned tab to close
    menuPick(app, 1, 2);
    check(app.tabs.size() == 6, "#312: Close all to the left spares the pinned tab left of it");

    // A press on a pinned tab activates it; dragged right it stays among the
    // pinned, and another dragged left stays after them
    paintStrip(app);
    const App::TabHit* pin = hitOf(app, 0);
    if (pin) {
        const int y = (int)((pin->rect.top + pin->rect.bottom) / 2);
        tabStripMouseDown(app, app.hwnd, (int)pin->rect.left + 5, y, false);
        check(app.activeTab == 0, "#312: a click on a pinned tab activates it");
        tabDragMove(app, app.hwnd, (int)pin->rect.left + 5 + 400, y);
        check(app.tabs[1].pinned && indexOf(app, files[2]) == 1 && tabsPinnedCount(app) == 2,
              "#312: a pinned tab dragged right stays among the pinned");
        tabDragEnd(app, app.hwnd, (int)pin->rect.left + 405, y);
    }
    paintStrip(app);
    const App::TabHit* last = hitOf(app, 5);
    if (last) {
        const int y = (int)((last->rect.top + last->rect.bottom) / 2);
        tabStripMouseDown(app, app.hwnd, (int)last->rect.left + 20, y, false);
        tabDragMove(app, app.hwnd, 50, y);
        check(app.activeTab == 2 && !app.tabs[2].pinned && tabsPinnedCount(app) == 2,
              "#312: a tab dragged over the pinned ones stops after them");
        tabDragEnd(app, app.hwnd, 50, y);
    }
    paintStrip(app);
    POINT overPinned{(LONG)(hitOf(app, 0) ? hitOf(app, 0)->rect.left + 5 : 50), 20};
    check(tabDropInsertionIndex(app, overPinned) >= 2, "#312: a tab dropped from another window lands after the pinned");
    const fs::path extra = dir / "Dropped.md";
    std::ofstream(extra) << "# Dropped\n\nA note dropped before the pinned tabs.\n";
    tabOpenPath(app, app.hwnd, toUtf8(extra.wstring()), false, 0);
    check(indexOf(app, extra) == 2 && tabsPinnedCount(app) == 2, "#312: and a tab opened before them goes after them");

    // Close all but this, from an unpinned tab: the pinned tabs stay
    const int keep = indexOf(app, files[0]);
    menuPick(app, keep, 1);
    check(app.tabs.size() == 3 && tabsPinnedCount(app) == 2 && indexOf(app, files[0]) == 2,
          "#312: Close all but this closes the others and spares the pinned");

    // Unpin: the tab goes just after the pinned ones
    menuPick(app, 0, 4);
    check(tabsPinnedCount(app) == 1 && !app.tabs[1].pinned, "#312: Unpin tab puts it right after the pinned");

    // The session: saved paths and pinned ones come back in their order
    Settings saved;
    saved.sessionTabs = {toUtf8(files[0].wstring()), toUtf8(files[1].wstring()), toUtf8(files[3].wstring()),
                         toUtf8(files[5].wstring())};
    saved.sessionPinned = {toUtf8(files[5].wstring()), toUtf8(files[1].wstring())};
    saved.sessionActive = 0;
    saved.alwaysOnTop = false;
    saveSettings(saved);
    const Settings loaded = loadSettings();
    check(loaded.sessionPinned == saved.sessionPinned && loaded.sessionTabs == saved.sessionTabs,
          "#312: settings.ini keeps which session tabs were pinned");
    openDocumentInViewer(app, files[0].wstring());
    tabsSeedSession(app, loaded.sessionTabs);
    tabsPinPaths(app, loaded.sessionPinned);
    check(tabsPinnedCount(app) == 2 && indexOf(app, files[1]) == 0 && indexOf(app, files[5]) == 1 &&
              indexOf(app, files[0]) == 2 && indexOf(app, files[3]) == 3,
          "#312: a restored session's pinned tabs lead the row in their order");
    check(pathOf(app, app.activeTab) == toUtf8(files[0].wstring()), "#312: and the active tab is still the one opened");

    // The title bar's pin is remembered when pressed
    paintStrip(app);
    const App::TabHit* pinButton = hitOf(app, -4);
    check(pinButton != nullptr, "the title bar's pin is drawn");
    if (pinButton) {
        const int x = (int)((pinButton->rect.left + pinButton->rect.right) / 2);
        const int y = (int)((pinButton->rect.top + pinButton->rect.bottom) / 2);
        tabStripMouseDown(app, app.hwnd, x, y, false);
        check(app.alwaysOnTop && loadSettings().alwaysOnTop, "#312: pressing the pin keeps the window on top, and says so in settings.ini");
        check(loadSettings().sessionPinned == saved.sessionPinned, "#312: without touching the rest of the file");
        tabStripMouseDown(app, app.hwnd, x, y, false);
        check(!app.alwaysOnTop && !loadSettings().alwaysOnTop, "#312: pressing it again lets go, and that is kept too");
    }

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Pinned tabs: " << failures << " failures\n";
    return failures ? 1 : 0;
}
