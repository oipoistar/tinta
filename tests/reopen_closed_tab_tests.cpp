// Reopen closed tab (docs/specs/reopen-closed-tab.md): Ctrl+Shift+T brings
// back the tab closed last, where it stood and scrolled as it was, in the
// reader or the editor, with the text "Don't save" threw away; a sweep's
// tabs come back together, and the tab menu names the last few. Driven
// through the keys, the strip and the menu, in a folder of its own.
#include "d2d_init.h"
#include "editor.h"
#include "frame_preview.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "overlays.h"
#include "render.h"
#include "signals.h"
#include "startpage.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
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
int copiesOf(const App& app, const fs::path& file) {
    int n = 0;
    for (int i = 0; i < (int)app.tabs.size(); i++)
        n += _stricmp(pathOf(app, i).c_str(), toUtf8(file.wstring()).c_str()) == 0;
    return n;
}
std::vector<std::string> order(const App& app) {
    std::vector<std::string> paths;
    for (int i = 0; i < (int)app.tabs.size(); i++) paths.push_back(pathOf(app, i));
    return paths;
}
std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
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

// What the window's paint does once the layout reaches a saved position
void settle(App& app) {
    ensureLayoutComplete(app);
    if (app.pendingScrollRestore >= 0.0f) {
        app.scrollY = app.targetScrollY = app.pendingScrollRestore;
        app.pendingScrollRestore = -1.0f;
    }
}

// The menu's rows: 0 Close, 1 Close all but this, 7 Reopen closed tab,
// and from 8 the recently closed tabs, newest first
constexpr int kReopenRow = 7;
constexpr int kFirstRecentRow = 8;

bool menuHasRow(App& app, int i, int row) {
    paintStrip(app);
    const App::TabHit* hit = hitOf(app, i);
    if (!hit) return false;
    openTabMenu(app, i, hit->rect.left + 10, hit->rect.bottom);
    paintStrip(app);
    bool found = false;
    for (float y = app.tabMenuY; y < app.tabMenuY + 700 && !found; y += 2)
        found = tabMenuItemAt(app, app.tabMenuX + 40, y) == row;
    closeTabMenu(app);
    return found;
}

// Right-click tab i, then click the menu's row
void menuPick(App& app, int i, int row) {
    paintStrip(app);
    const App::TabHit* hit = hitOf(app, i);
    check(hit != nullptr, "the tab is drawn in the strip");
    if (!hit) return;
    openTabMenu(app, i, hit->rect.left + 10, hit->rect.bottom);
    paintStrip(app);
    const float x = app.tabMenuX + 40;
    for (float y = app.tabMenuY; y < app.tabMenuY + 700; y += 2) {
        if (tabMenuItemAt(app, x, y) == row) {
            tabMenuMouseDown(app, app.hwnd, (int)x, (int)y);
            return;
        }
    }
    check(false, "the menu has row " + std::to_string(row));
    closeTabMenu(app);
}

// Tab i's menu, open over the strip and the page, saved beside the test
// for inspection; false when it would not fit the window
bool menuPreview(App& app, int i, const std::string& name) {
    paintStrip(app);
    const App::TabHit* hit = hitOf(app, i);
    if (!hit) return false;
    ensureLayoutComplete(app);
    openTabMenu(app, i, hit->rect.left + 10, hit->rect.bottom);
    app.tabMenuAnimation = 1.0f;
    check(saveFramePreview(app, fs::current_path() / (name + ".png"), [&] { renderTabMenu(app); }),
          "a preview is saved: " + name);
    const bool fits = app.tabMenuX >= 0 && app.tabMenuX + app.tabMenuW <= app.width + 0.5f;
    closeTabMenu(app);
    return fits;
}

void reopenKey(App& app) { keystroke(app, 'T', {true, true, false}); }
void closeKey(App& app) { keystroke(app, VK_F4, {true, false, false}); }
}  // namespace

int runReopenClosedTabTests() {
    const fs::path dir = fs::current_path() / "notes";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::copy(TINTA_REOPEN_TAB_FIXTURE_DIR, dir, fs::copy_options::recursive, ec);
    check(!ec, "the fixture copies");
    std::vector<fs::path> files;
    for (const char* name : {"Field notes.md", "Ink recipes.md", "Reading list.md"}) files.push_back(dir / name);
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::wstring name = entry.path().filename().wstring();
        if (std::any_of(name.begin(), name.end(), [](wchar_t c) { return c > 0x7F; })) files.push_back(entry.path());
    }
    check(files.size() == 4, "four notes, one named past ASCII");
    if (files.size() != 4) return 1;
    const std::string original3 = readFile(files[3]);

    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Reopen closed tab tests", WS_POPUP, 0, 0, 1050, 800, nullptr,
                               nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);

    check(openDocumentInViewer(app, files[0].wstring()), "the first note opens");
    tabsInit(app);
    for (size_t i = 1; i < files.size(); i++) tabOpenPath(app, app.hwnd, toUtf8(files[i].wstring()), false);
    check(app.tabs.size() == 4 && app.activeTab == 0, "four tabs, the first active");
    check(app.closedTabs.empty(), "nothing closed yet");

    // Ctrl+W on a scrolled reader tab, then Ctrl+Shift+T: back in its place,
    // active, at the same scroll
    tabActivate(app, app.hwnd, 1);
    settle(app);
    const float scrolled = std::round(std::max(0.0f, app.contentHeight - (float)app.height) * 0.5f);
    check(scrolled > 100.0f, "the recipes are long enough to scroll");
    app.scrollY = app.targetScrollY = scrolled;
    keystroke(app, 'W', {true, false, false});
    check(app.tabs.size() == 3 && indexOf(app, files[1]) < 0 && app.closedTabs.size() == 1,
          "Ctrl+W closes the tab and remembers it");
    reopenKey(app);
    settle(app);
    check(app.tabs.size() == 4 && indexOf(app, files[1]) == 1 && app.activeTab == 1,
          "Ctrl+Shift+T reopens it where it stood, active");
    check(std::fabs(app.scrollY - scrolled) < 1.0f, "and scrolled as it was");
    check(app.closedTabs.empty(), "the entry is used up");
    reopenKey(app);
    check(app.tabs.size() == 4, "with nothing closed, Ctrl+Shift+T does nothing");

    // Middle-click and the close button are remembered too
    paintStrip(app);
    if (const App::TabHit* hit = hitOf(app, 2)) {
        tabStripMouseDown(app, app.hwnd, (int)hit->rect.left + 20, (int)((hit->rect.top + hit->rect.bottom) / 2), true);
        check(indexOf(app, files[2]) < 0 && app.closedTabs.size() == 1, "a middle-click close is remembered");
        reopenKey(app);
        check(indexOf(app, files[2]) == 2 && app.activeTab == 2, "and comes back at its index");
    }
    paintStrip(app);
    if (const App::TabHit* hit = hitOf(app, app.activeTab); hit && hit->hasClose) {
        const int at = app.activeTab;
        tabStripMouseDown(app, app.hwnd, (int)((hit->closeRect.left + hit->closeRect.right) / 2),
                          (int)((hit->closeRect.top + hit->closeRect.bottom) / 2), false);
        check(app.tabs.size() == 3 && app.closedTabs.size() == 1, "the close button's close is remembered");
        reopenKey(app);
        check(app.tabs.size() == 4 && app.activeTab == at && indexOf(app, files[2]) == at,
              "and the tab comes back from it");
    } else {
        check(false, "the active tab has a close button");
    }

    // Close all but this: one Ctrl+Shift+T brings the whole sweep back in
    // its old order
    const std::vector<std::string> before = order(app);
    menuPick(app, 2, 1);
    check(app.tabs.size() == 1 && app.closedTabs.size() == 3, "Close all but this closes the other three");
    check(app.closedTabs.size() == 3 && app.closedTabs[0].group == app.closedTabs[2].group,
          "as one sweep");
    reopenKey(app);
    check(order(app) == before, "one Ctrl+Shift+T reopens the sweep in its old order");
    check(app.closedTabs.empty(), "and the whole sweep is used up");

    // The tab menu: Reopen closed tab, then the closed tabs by name
    check(!menuHasRow(app, 0, kFirstRecentRow), "no recent rows while nothing is closed");
    menuPick(app, 0, kReopenRow);
    check(app.tabs.size() == 4, "Reopen closed tab is inert with nothing closed");
    menuPick(app, 0, 0);  // Close: the field notes
    tabActivate(app, app.hwnd, indexOf(app, files[2]));
    keystroke(app, 'W', {true, false, false});  // the reading list
    check(app.closedTabs.size() == 2, "two closed");
    check(menuHasRow(app, 0, kFirstRecentRow + 1) && !menuHasRow(app, 0, kFirstRecentRow + 2),
          "the menu lists both, and no more");
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            updateTextFormats(app);
            const std::string name = "menu-" + std::to_string(theme) + "-" + std::to_string(width);
            check(menuPreview(app, 0, name), "the menu fits the window: " + name);
        }
    }
    const int english = app.currentLanguageIndex;
    loadLanguageOverrides();  // de, fr, it and ru ship this way
    for (const char* language : {"de", "fr", "ru", "zh"}) {
        app.currentLanguageIndex = languageIndexById(language);
        check(menuPreview(app, 0, std::string("menu-") + language), std::string("the menu fits in ") + language);
    }
    app.currentLanguageIndex = english;
    app.width = 1050;
    applyTheme(app, 0);
    updateTextFormats(app);
    ensureLayoutComplete(app);
    app.showHelp = true;
    app.helpAnimation = 1.0f;
    check(saveFramePreview(app, fs::current_path() / "help.png", [&] { renderHelpOverlay(app); }),
          "the keyboard help is saved");
    app.showHelp = false;
    menuPick(app, 0, kFirstRecentRow + 1);  // the older one: the field notes
    check(indexOf(app, files[0]) == 0 && indexOf(app, files[2]) < 0 && app.closedTabs.size() == 1,
          "a recent row reopens the tab it names, and only it");
    menuPick(app, 0, kReopenRow);
    check(indexOf(app, files[2]) >= 0 && app.closedTabs.empty(), "Reopen closed tab reopens the newest");
    check(order(app) == before, "and every tab is where it was");

    // An editor tab comes back in the editor, with its caret and scroll
    tabActivate(app, app.hwnd, indexOf(app, files[0]));
    enterEditMode(app);
    check(app.editMode && !app.editorDirty, "the field notes open in the editor");
    const std::wstring fieldText = app.editorText;
    app.editorCursorPos = 120;
    app.editorScrollY = 64.0f;
    closeKey(app);
    check(indexOf(app, files[0]) < 0 && !app.confirmExitPending, "Ctrl+F4 closes a clean editor tab");
    reopenKey(app);
    check(indexOf(app, files[0]) == 0 && app.activeTab == 0 && app.editMode, "it comes back in the editor");
    check(app.editorText == fieldText && !app.editorDirty, "with its text, unchanged");
    check(app.editorCursorPos == 120 && std::fabs(app.editorScrollY - 64.0f) < 0.5f, "its caret and its scroll");
    exitEditMode(app);

    // "Don't save" can be taken back: the text comes back unsaved
    tabActivate(app, app.hwnd, indexOf(app, files[3]));
    enterEditMode(app);
    app.editorCursorPos = app.editorText.size();
    typeKeys(app, L"\nAn unsaved thought.");
    check(app.editorDirty, "typing makes the German note unsaved");
    closeKey(app);
    check(app.confirmExitPending && indexOf(app, files[3]) >= 0, "closing it asks first");
    confirmExitAction(app, app.hwnd, 2);
    check(indexOf(app, files[3]) < 0 && app.closedTabs.size() == 1, "Don't save closes it");
    check(readFile(files[3]) == original3, "and the file is untouched");
    reopenKey(app);
    check(indexOf(app, files[3]) == 3 && app.editMode && app.editorDirty, "it comes back unsaved, in the editor");
    check(app.editorText.find(L"An unsaved thought.") != std::wstring::npos, "with the text that was thrown away");
    check(pathOf(app, app.activeTab) == toUtf8(files[3].wstring()), "under its own name, umlaut and all");
    check(saveFramePreview(app, fs::current_path() / "unsaved-back.png"), "the reopened editor is saved");
    closeKey(app);
    confirmExitAction(app, app.hwnd, 2);
    check(app.closedTabs.size() == 1 && app.closedTabs.back().editorDirty, "thrown away again, it is kept again");
    app.closedTabs.clear();
    tabOpenPath(app, app.hwnd, toUtf8(files[3].wstring()), false);

    // An untitled note comes back as one
    tabOpenQuickNote(app, app.hwnd);
    check(app.editMode && app.currentFile.empty(), "a quick note opens");
    typeKeys(app, L"A note never saved.");
    closeKey(app);
    check(app.confirmExitPending, "closing the note asks first");
    confirmExitAction(app, app.hwnd, 2);
    check(app.tabs.size() == 4 && app.closedTabs.size() == 1, "Don't save closes the note");
    reopenKey(app);
    check(app.tabs.size() == 5 && app.currentFile.empty() && app.editMode && app.editorDirty &&
              app.editorText == L"A note never saved.",
          "Ctrl+Shift+T brings the note back, unsaved");
    closeKey(app);
    confirmExitAction(app, app.hwnd, 2);
    app.closedTabs.clear();

    // A file that is open again is switched to; a file that is gone is
    // reported and the one before it reopens instead
    tabActivate(app, app.hwnd, indexOf(app, files[1]));
    keystroke(app, 'W', {true, false, false});
    tabOpenPath(app, app.hwnd, toUtf8(files[1].wstring()), false);
    tabActivate(app, app.hwnd, 0);
    reopenKey(app);
    check(copiesOf(app, files[1]) == 1 && pathOf(app, app.activeTab) == toUtf8(files[1].wstring()) &&
              app.closedTabs.empty(),
          "a closed file opened again is switched to, not doubled");
    tabActivate(app, app.hwnd, indexOf(app, files[2]));
    keystroke(app, 'W', {true, false, false});
    tabActivate(app, app.hwnd, indexOf(app, files[1]));
    keystroke(app, 'W', {true, false, false});
    const std::string recipes = readFile(files[1]);
    fs::remove(files[1], ec);
    const size_t chips = app.signalChips.size();
    reopenKey(app);
    check(indexOf(app, files[1]) < 0 && indexOf(app, files[2]) >= 0 && app.closedTabs.empty(),
          "a deleted file is skipped and the tab before it reopens");
    check(app.signalChips.size() > chips && app.signalChips.back().severity == SIG_WARN &&
              app.signalChips.back().emph.find(L"Ink recipes") != std::wstring::npos,
          "with a notice naming the missing file");
    std::ofstream(files[1], std::ios::binary) << recipes;
    tabOpenPath(app, app.hwnd, toUtf8(files[1].wstring()), false, 1);

    // The start page and Tinta's own closes leave nothing to reopen; Ctrl+T
    // still opens the start page
    keystroke(app, 'T', {true, false, false});
    check(startPageActive(app) && app.tabs.size() == 5, "Ctrl+T opens the start page in a new tab");
    keystroke(app, 'W', {true, false, false});
    check(app.tabs.size() == 4 && app.closedTabs.empty(), "closing the start page leaves nothing to reopen");
    tabCloseIndex(app, app.hwnd, indexOf(app, files[2]), TabClose::Internal);
    check(app.tabs.size() == 3 && app.closedTabs.empty(), "an internal close is not remembered");
    tabOpenPath(app, app.hwnd, toUtf8(files[2].wstring()), false, 2);

    // A pinned tab comes back pinned
    tabSetPinned(app, app.hwnd, indexOf(app, files[3]), true);
    menuPick(app, 0, 0);
    check(indexOf(app, files[3]) < 0, "the pinned tab closes from its menu");
    reopenKey(app);
    check(indexOf(app, files[3]) == 0 && app.tabs[0].pinned, "and comes back pinned, first in the row");
    tabSetPinned(app, app.hwnd, 0, false);

    // Ctrl+Shift+T works from Live, too
    tabActivate(app, app.hwnd, indexOf(app, files[0]));
    tabCloseIndex(app, app.hwnd, indexOf(app, files[2]));
    app.editorLive = true;
    enterEditMode(app);
    check(editorLiveOn(app), "Live is on");
    reopenKey(app);
    check(indexOf(app, files[2]) >= 0 && app.tabs.size() == 4, "Ctrl+Shift+T reopens from Live");
    tabActivate(app, app.hwnd, indexOf(app, files[0]));
    if (app.editMode) exitEditMode(app);
    app.editorLive = false;

    // The last tab closes into the start page, and comes back from it
    while (app.tabs.size() > 1) tabCloseIndex(app, app.hwnd, (int)app.tabs.size() - 1, TabClose::Internal);
    const std::string last = pathOf(app, 0);
    keystroke(app, 'W', {true, false, false});
    check(app.tabs.size() == 1 && startPageActive(app) && app.closedTabs.size() == 1,
          "closing the last tab lands on the start page, remembered");
    reopenKey(app);
    check(app.tabs.size() == 1 && app.currentFile == last && !startPageActive(app),
          "Ctrl+Shift+T replaces the start page with it");

    // Twenty-five at most
    app.closedTabs.clear();
    const fs::path many = dir / "many";
    fs::create_directories(many, ec);
    for (int i = 0; i < 30; i++) {
        const fs::path note = many / ("note " + std::to_string(i) + ".md");
        std::ofstream(note) << "# Note " << i << "\n\nOne of thirty.\n";
        tabOpenPath(app, app.hwnd, toUtf8(note.wstring()), false);
        tabCloseIndex(app, app.hwnd, indexOf(app, note));
    }
    check(app.closedTabs.size() == 25, "twenty-five closed tabs are kept");
    check(!app.closedTabs.empty() && app.closedTabs.front().path == toUtf8((many / "note 5.md").wstring()),
          "the oldest go first");

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Reopen closed tab: " << failures << " failures\n";
    return failures ? 1 : 0;
}
