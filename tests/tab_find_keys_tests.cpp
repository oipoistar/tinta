// #334: Ctrl+PgDn and Ctrl+PgUp go to the next and the previous tab, as
// Ctrl+Tab and Ctrl+Shift+Tab do.
// #335: F3 and Shift+F3 go to the next and the previous match, from the
// find bar or from the text beside it, and with the bar closed F3 opens it
// on the word searched last.
//
// Keys go through keystroke(), as the message loop delivers them.
#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "overlays.h"
#include "render.h"
#include "search.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) {
        if (failures < 60) std::cerr << "FAIL tab and find keys: " << message << '\n';
        ++failures;
    }
}

const KeyMods kCtrl{true, false, false};
const KeyMods kShift{false, true, false};
const KeyMods kCtrlShift{true, true, false};

std::string activePath(const App& app) { return app.currentFile; }

bool endsWith(const std::string& text, const std::string& tail) {
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

int matchCount(const App& app) {
    return (int)(app.editMode ? app.editorSearchMatches.size() : app.searchMatches.size());
}
int matchIndex(const App& app) { return app.editMode ? app.editorSearchCurrentIndex : app.searchCurrentIndex; }

void find(App& app, const std::wstring& word) {
    keystroke(app, 'F', kCtrl);
    typeKeys(app, word);
}

// The bar open on the word, on its match `index`
void findAt(App& app, const std::wstring& word, int index) {
    if (app.showSearch) closeSearchInput(app);
    find(app, word);
    for (int i = 0; i < index; i++) keystroke(app, VK_F3);
}

void tabKeys(App& app, const std::vector<fs::path>& files) {
    check(openDocumentInViewer(app, files[0].wstring()), "the first file opens");
    ensureLayoutComplete(app);
    // Nothing searched yet: F3 opens the bar, empty, as Ctrl+F does
    check(keystroke(app, VK_F3) && app.showSearch && app.searchActive && app.searchQuery.empty(),
          "#335: with nothing searched yet F3 opens an empty bar");
    keystroke(app, VK_ESCAPE);
    check(!app.showSearch, "Esc closes it");
    check(keystroke(app, VK_F3, kShift) && app.showSearch && app.searchQuery.empty(), "#335: and so does Shift+F3");
    keystroke(app, VK_ESCAPE);
    tabsInit(app);
    for (size_t i = 1; i < files.size(); i++) tabOpenPath(app, app.hwnd, toUtf8(files[i].wstring()), false);
    check(app.tabs.size() == 3 && app.activeTab == 0, "three tabs, the first in front");

    // Round the row both ways, each press consumed: it types nothing
    for (int expected : {1, 2, 0, 1}) {
        check(keystroke(app, VK_NEXT, kCtrl) && app.activeTab == expected,
              "#334: Ctrl+PgDn goes to tab " + std::to_string(expected));
    }
    check(endsWith(activePath(app), "Second tab.md"), "#334: and the tab's document is the one shown");
    for (int expected : {0, 2, 1, 0}) {
        check(keystroke(app, VK_PRIOR, kCtrl) && app.activeTab == expected,
              "#334: Ctrl+PgUp goes to tab " + std::to_string(expected));
    }
    // The same places Ctrl+Tab and Ctrl+Shift+Tab go to
    keystroke(app, VK_TAB, kCtrl);
    const int byTab = app.activeTab;
    keystroke(app, VK_TAB, kCtrlShift);
    keystroke(app, VK_NEXT, kCtrl);
    check(app.activeTab == byTab && byTab == 1, "#334: Ctrl+PgDn goes where Ctrl+Tab goes");
    keystroke(app, VK_PRIOR, kCtrl);
    check(app.activeTab == 0, "#334: Ctrl+PgUp goes where Ctrl+Shift+Tab goes");

    // Without Ctrl the keys page, and with Shift as well they are not these
    ensureLayoutComplete(app);
    app.scrollY = app.targetScrollY = 0;
    keystroke(app, VK_NEXT);
    const float paged = app.targetScrollY;
    check(app.activeTab == 0 && paged > 0, "#334: PgDn by itself pages down and stays on the tab");
    keystroke(app, VK_PRIOR);
    check(app.activeTab == 0 && app.targetScrollY == 0, "#334: PgUp by itself pages up");
    keystroke(app, VK_NEXT, kCtrlShift);
    check(app.activeTab == 0, "#334: Ctrl+Shift+PgDn changes no tab");
    keystroke(app, VK_PRIOR, kCtrlShift);
    check(app.activeTab == 0, "#334: nor does Ctrl+Shift+PgUp");

    // While editing: the text stays in its tab with its unsaved changes
    enterEditMode(app);
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, kCtrl);
    check(app.editMode && !editorLiveOn(app), "the source editor opens");
    typeKeys(app, L"edited ");
    check(app.editorDirty && app.editorText.rfind(L"edited ", 0) == 0, "a word is typed into the first tab");
    check(keystroke(app, VK_NEXT, kCtrl) && app.activeTab == 1, "#334: Ctrl+PgDn changes tabs from the source editor");
    check(app.editorText.find(L"edited ") == std::wstring::npos, "#334: the next tab has its own text");
    check(keystroke(app, VK_PRIOR, kCtrl) && app.activeTab == 0 && app.editMode && app.editorDirty &&
              app.editorText.rfind(L"edited ", 0) == 0,
          "#334: and Ctrl+PgUp comes back to the text as it was left, unsaved");
    keystroke(app, VK_HOME, kCtrl);
    for (int i = 0; i < 7; i++) keystroke(app, VK_DELETE);
    check(app.editorText.rfind(L"# Find next", 0) == 0, "the word is taken out again");
    keystroke(app, VK_OEM_2, kCtrl);
    check(editorLiveOn(app), "Live opens");
    check(keystroke(app, VK_NEXT, kCtrl) && app.activeTab == 1, "#334: Ctrl+PgDn changes tabs from Live");
    check(keystroke(app, VK_PRIOR, kCtrl) && app.activeTab == 0, "#334: and Ctrl+PgUp back");
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, kCtrl);
    app.editorDirty = false;
    exitEditMode(app);
    check(!app.editMode, "the reading view is back");

    // With the find bar open, as Ctrl+Tab
    find(app, L"lantern");
    check(app.showSearch && keystroke(app, VK_NEXT, kCtrl) && app.activeTab == 1,
          "#334: Ctrl+PgDn changes tabs with the find bar open");
    keystroke(app, VK_PRIOR, kCtrl);
    if (app.showSearch) closeSearchInput(app);
    check(app.activeTab == 0, "and back");

    // One tab: the keys are left as they were. Neither the reading view
    // nor the source editor had a use for them
    tabCloseIndex(app, app.hwnd, 2);
    tabCloseIndex(app, app.hwnd, 1);
    check(app.tabs.size() == 1, "one tab is left");
    ensureLayoutComplete(app);
    app.scrollY = app.targetScrollY = 0;
    const std::string shown = activePath(app);
    check(!keystroke(app, VK_NEXT, kCtrl) && !keystroke(app, VK_PRIOR, kCtrl) && app.activeTab == 0 &&
              activePath(app) == shown && app.targetScrollY == 0,
          "#334: with one tab the reading view leaves Ctrl+PgDn and Ctrl+PgUp alone, as before");
    keystroke(app, VK_NEXT);
    check(app.targetScrollY == paged, "and PgDn pages");
    keystroke(app, VK_PRIOR);
    enterEditMode(app);
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, kCtrl);
    keystroke(app, VK_HOME, kCtrl);
    keystroke(app, VK_NEXT);
    const size_t byPage = app.editorCursorPos;
    keystroke(app, VK_HOME, kCtrl);
    keystroke(app, VK_NEXT, kCtrl);
    check(byPage > 0 && app.editorCursorPos == 0 && app.activeTab == 0 && activePath(app) == shown,
          "#334: with one tab the source editor leaves Ctrl+PgDn alone, as before, and PgDn pages");
    exitEditMode(app);
}

void findKeys(App& app, const std::vector<fs::path>& files) {
    check(endsWith(activePath(app), "Find next.md") && !app.editMode && !app.showSearch, "the page with the word is shown");
    ensureLayoutComplete(app);
    app.scrollY = app.targetScrollY = 0;

    find(app, L"lantern");
    const int count = matchCount(app);
    check(count == 10 && matchIndex(app) == 0, "the word is found ten times: " + std::to_string(count));
    for (int expected : {1, 2, 3}) {
        check(keystroke(app, VK_F3) && matchIndex(app) == expected, "#335: F3 goes to match " + std::to_string(expected));
    }
    check(app.searchQuery == L"lantern" && app.showSearch && app.searchActive, "#335: F3 types nothing into the bar");
    check(keystroke(app, VK_F3, kShift) && matchIndex(app) == 2, "#335: Shift+F3 goes back one");
    keystroke(app, VK_RETURN);
    check(matchIndex(app) == 3, "Enter goes on from where F3 left it");
    keystroke(app, VK_RETURN, kShift);
    keystroke(app, VK_F3, kShift);
    keystroke(app, VK_F3, kShift);
    check(matchIndex(app) == 0, "back at the first");
    check(keystroke(app, VK_F3, kShift) && matchIndex(app) == count - 1, "#335: Shift+F3 at the first goes round to the last");
    check(app.targetScrollY > 0, "#335: and the page goes to it");
    check(keystroke(app, VK_F3) && matchIndex(app) == 0, "#335: F3 at the last goes round to the first");
    keystroke(app, VK_F3, kCtrl);
    check(matchIndex(app) == 0, "#335: Ctrl+F3 is not the key");

    // Closed on the fourth match: F3 opens the bar again on the fifth,
    // the word selected so that typing starts another search
    findAt(app, L"lantern", 3);
    keystroke(app, VK_ESCAPE);
    check(!app.showSearch && app.searchQuery.empty() && app.searchMatches.empty(), "Esc closes the bar and clears it");
    check(keystroke(app, VK_F3) && app.showSearch && app.searchActive && app.searchQuery == L"lantern" &&
              matchCount(app) == count,
          "#335: F3 with the bar closed opens it on the word searched last");
    check(matchIndex(app) == 4, "#335: at the match after the one it was closed on: " + std::to_string(matchIndex(app)));
    check(app.searchFields[0].anchor == 0 && app.searchFields[0].caret == 7, "#335: with the word selected");
    typeKeys(app, L"tab");
    check(app.searchQuery == L"tab", "#335: so typing starts another search");
    keystroke(app, VK_ESCAPE);

    findAt(app, L"lantern", 3);
    keystroke(app, VK_ESCAPE);
    check(keystroke(app, VK_F3, kShift) && app.searchQuery == L"lantern" && matchIndex(app) == 2,
          "#335: Shift+F3 with the bar closed opens it on the match before");
    keystroke(app, VK_ESCAPE);
    findAt(app, L"lantern", count - 1);
    keystroke(app, VK_ESCAPE);
    keystroke(app, VK_F3);
    check(matchIndex(app) == 0, "#335: closed on the last, F3 opens on the first");
    keystroke(app, VK_ESCAPE);
    keystroke(app, VK_F3, kShift);
    check(matchIndex(app) == count - 1, "#335: closed on the first, Shift+F3 opens on the last");
    keystroke(app, VK_ESCAPE);

    // A word with no match is remembered too, and finds nothing again
    find(app, L"zeppelin");
    check(matchCount(app) == 0, "a word that is not on the page");
    keystroke(app, VK_F3);
    check(matchCount(app) == 0 && app.showSearch, "#335: F3 on no match does nothing");
    keystroke(app, VK_ESCAPE);
    keystroke(app, VK_F3);
    check(app.searchQuery == L"zeppelin" && matchCount(app) == 0, "#335: and it is the word F3 opens on");
    keystroke(app, VK_ESCAPE);

    // In the editor: the same from the bar, and from the text beside it,
    // where F3 types nothing
    findAt(app, L"lantern", 3);
    keystroke(app, VK_ESCAPE);
    enterEditMode(app);
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, kCtrl);
    check(app.editMode && !editorLiveOn(app), "the source editor opens");
    const std::wstring source = app.editorText;
    check(keystroke(app, VK_F3) && app.showSearch && app.searchQuery == L"lantern" && matchCount(app) == count &&
              matchIndex(app) == 0,
          "#335: in another view than it was closed in, F3 opens on the first match");
    keystroke(app, VK_F3);
    keystroke(app, VK_F3);
    check(matchIndex(app) == 2 && app.searchCurrentIndex == 2, "#335: F3 steps through the source's matches");
    focusSourceEditor(app);
    check(app.showSearch && !app.searchActive && sourceEditorHasFocus(app), "the keyboard is in the text, the bar still open");
    check(keystroke(app, VK_F3) && matchIndex(app) == 3 && keystroke(app, VK_F3, kShift) && matchIndex(app) == 2,
          "#335: F3 and Shift+F3 step from the text too");
    check(app.editorText == source && !app.searchActive, "#335: and type nothing, the keyboard still in the text");
    keystroke(app, VK_F3);
    closeSearchInput(app);
    check(keystroke(app, VK_F3) && app.searchQuery == L"lantern" && matchIndex(app) == 4,
          "#335: closed on the fourth in the source, F3 opens on the fifth");
    closeSearchInput(app);
    keystroke(app, 'H', kCtrl);
    typeKeys(app, L"lantern");
    check(app.searchReplaceMode && keystroke(app, VK_F3) && matchIndex(app) == 1 && app.editorText == source,
          "#335: F3 steps with the replace row open, and replaces nothing");
    closeSearchInput(app);

    keystroke(app, VK_OEM_2, kCtrl);
    check(editorLiveOn(app), "Live opens");
    find(app, L"lantern");
    const int live = matchCount(app);
    check(live == count && keystroke(app, VK_F3) && matchIndex(app) == 1 && keystroke(app, VK_F3, kShift) &&
              matchIndex(app) == 0,
          "#335: F3 and Shift+F3 step in Live: " + std::to_string(live));
    closeSearchInput(app);
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, kCtrl);
    check(app.editorText == source, "the text is as it was");
    exitEditMode(app);
    check(!app.editMode && keystroke(app, VK_F3, kShift) && app.searchQuery == L"lantern" && matchIndex(app) == count - 1,
          "#335: back in the reading view, Shift+F3 opens on the last match");
    keystroke(app, VK_ESCAPE);

    // In another document the word is the same and the place is not
    findAt(app, L"lantern", 3);
    keystroke(app, VK_ESCAPE);
    tabOpenPath(app, app.hwnd, toUtf8(files[1].wstring()), true);
    ensureLayoutComplete(app);
    check(endsWith(activePath(app), "Second tab.md"), "the second file opens in a tab");
    check(keystroke(app, VK_F3) && app.searchQuery == L"lantern" && matchCount(app) == 1 && matchIndex(app) == 0,
          "#335: in another document F3 opens on that document's first match");
    keystroke(app, VK_ESCAPE);
    // A word both documents have many times: the place in one is no place
    // in the other
    tabActivate(app, app.hwnd, 0);
    ensureLayoutComplete(app);
    findAt(app, L"the", 6);
    check(endsWith(activePath(app), "Find next.md") && matchIndex(app) == 6 && matchCount(app) > 12, "a common word, on its seventh match");
    keystroke(app, VK_ESCAPE);
    tabActivate(app, app.hwnd, 1);
    ensureLayoutComplete(app);
    check(endsWith(activePath(app), "Second tab.md") && keystroke(app, VK_F3) && app.searchQuery == L"the" &&
              matchCount(app) > 6 && matchIndex(app) == 0,
          "#335: the place it was closed on in one document does not count in another: " + std::to_string(matchIndex(app)));
    keystroke(app, VK_ESCAPE);

    // A command bound to F3 keeps the key
    Settings custom;
    custom.keyProfile = "custom";
    custom.keyOverrides = {{"theme", "F3"}};
    applyKeymap(app, custom);
    keystroke(app, VK_F3);
    check(!app.showSearch && app.showThemeChooser, "#335: a command bound to F3 keeps it");
    keystroke(app, VK_ESCAPE);
    app.showThemeChooser = false;
    Settings windows;
    windows.keyProfile = "windows";
    applyKeymap(app, windows);
    keystroke(app, VK_F3);
    check(app.showSearch, "and F3 is the find key again");
    closeSearchInput(app);

    // A dialog that owns the keyboard keeps F3 out
    app.showSettings = true;
    keystroke(app, VK_F3);
    check(!app.showSearch, "#335: F3 opens no bar under the Settings dialog");
    app.showSettings = false;
}

}  // namespace

int runTabFindKeysTests(const char* fixtureDir) {
    const fs::path dir = fs::current_path() / "notes";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::copy(fixtureDir, dir, fs::copy_options::recursive, ec);
    check(!ec, "the fixture folder copies");
    const std::vector<fs::path> files = {dir / "Find next.md", dir / "Second tab.md", dir / "Third tab.md"};

    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Tab and find key tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr,
                               nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    applyTheme(app, 0);

    tabKeys(app, files);
    findKeys(app, files);

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Tab and find keys: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
