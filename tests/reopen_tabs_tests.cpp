#include "d2d_init.h"
#include "editor.h"
#include "file_utils.h"
#include "i18n.h"
#include "input.h"
#include "overlays.h"
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "ui_frame.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Tabs from last time (#323). Tinta started with the tabs it was closed
// with, and a file opened from Explorer joined them: to look at one file
// tomorrow, every tab had to be closed by hand today. Settings > General >
// Reopen tabs from last time turns that off; pinned tabs still come back.
//
// A window is closed and started again here the way the app does it: the
// close writes the session with sessionKeep, the start reads it with
// sessionToReopen and seeds the tab row.
namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
namespace fs = std::filesystem;
using uiframe::Frame;

fs::path g_fixtures, g_work, g_frames;

std::string utf8(const fs::path& path) { return toUtf8(path.wstring()); }
std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}
void writeAll(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}
fs::path fresh(const std::string& name, const char* fixture) {
    const fs::path to = g_work / name;
    std::error_code ignored;
    fs::copy_file(g_fixtures / fixture, to, fs::copy_options::overwrite_existing, ignored);
    return to;
}
bool same(const std::string& a, const fs::path& b) { return _stricmp(a.c_str(), utf8(b).c_str()) == 0; }
bool same(const std::vector<std::string>& paths, const std::vector<fs::path>& files) {
    if (paths.size() != files.size()) return false;
    for (size_t i = 0; i < paths.size(); i++)
        if (!same(paths[i], files[i])) return false;
    return true;
}
std::string pathOf(const App& app, int i) { return i == app.activeTab ? app.currentFile : app.tabs[i].path; }
// The window's tabs are these files, in this order
bool holds(const App& app, const std::vector<fs::path>& files) {
    if (app.tabs.size() != files.size()) return false;
    for (size_t i = 0; i < files.size(); i++)
        if (!same(pathOf(app, (int)i), files[i])) return false;
    return true;
}
bool shows(const App& app, const fs::path& file) { return same(app.currentFile, file); }

// --- A window, started and closed as the app does it ---------------------------

struct Window {
    std::unique_ptr<App> state = std::make_unique<App>();
    App& app = *state;
    bool sessionCameBack = false;
    bool ok = false;
    Window() {
        app.hwnd = CreateWindowExW(0, L"STATIC", L"Reopen tabs tests", WS_POPUP, 0, 0, 1300, 900, nullptr, nullptr,
                                   nullptr, nullptr);
        if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return;
        app.width = 1300;
        app.height = 900;
        updateTextFormats(app);
        Settings keys;
        keys.keyProfile = "windows";
        applyKeymap(app, keys);
        applyTheme(app, 0);
        ok = true;
    }
    ~Window() {
        if (app.hwnd) DestroyWindow(app.hwnd);
        app.hwnd = nullptr;
    }
};

// The start: settings.ini read, the session's tabs that come back, the
// file the launch was given (or the session's own), the tab row
std::unique_ptr<Window> start(const fs::path* file) {
    auto window = std::make_unique<Window>();
    App& app = window->app;
    if (!window->ok) return window;
    const Settings saved = loadSettings();
    app.openInTabs = saved.openInTabs;
    app.reopenTabs = saved.reopenTabs;
    SessionTabs session;
    if (saved.openInTabs && !saved.sessionTabs.empty()) session = sessionToReopen(saved);
    const bool restore = !session.paths.empty();
    if (file) {
        openDocumentInViewer(app, file->wstring());
    } else if (restore) {
        openDocumentInViewer(app, toWide(session.paths[session.active]));
    }
    if (restore && !app.currentFile.empty()) {
        tabsSeedSession(app, session.paths);
        tabsPinPaths(app, saved.sessionPinned);
        window->sessionCameBack = true;
    } else {
        tabsInit(app);
    }
    return window;
}
// The close: the file as it stands, the session put into it, written
void close(std::unique_ptr<Window>& window) {
    Settings settings = loadSettings();
    sessionKeep(settings, window->app);
    saveSettings(settings);
    window.reset();
}
int indexOf(const App& app, const fs::path& file) {
    for (int i = 0; i < (int)app.tabs.size(); i++)
        if (same(pathOf(app, i), file)) return i;
    return -1;
}
void pin(App& app, const fs::path& file) {
    const int at = indexOf(app, file);
    check(at >= 0, "the tab to pin is open");
    if (at >= 0) tabSetPinned(app, app.hwnd, at, true);
}
// settings.ini's [Session], as written: empty when there is none
std::string sessionPart() {
    const std::string text = readAll(getSettingsPath());
    const size_t from = text.find("[Session]");
    if (from == std::string::npos) return {};
    const size_t to = text.find("\n[", from);
    return text.substr(from, to == std::string::npos ? std::string::npos : to - from);
}
void setReopen(bool on) {
    Settings settings = loadSettings();
    settings.reopenTabs = on;
    saveSettings(settings);
}
// The tab row as drawn: how many tabs it has
int tabsDrawn(App& app) {
    app.renderTarget->BeginDraw();
    app.renderTarget->Clear(app.theme.background);
    renderTabStrip(app);
    app.renderTarget->EndDraw();
    int count = 0;
    for (const auto& hit : app.tabHits)
        if (hit.index >= 0) ++count;
    return count;
}
bool has(const ElementPtr& root, ElementType type) {
    if (!root) return false;
    if (root->type == type) return true;
    for (const auto& child : root->children)
        if (has(child, type)) return true;
    return false;
}
// The page of a tab that came back: laid out, with its parts
void pageIsWhole(App& app, const std::string& where, bool code = true) {
    ensureLayoutComplete(app);
    check(app.root && has(app.root, ElementType::Heading) && has(app.root, ElementType::Table) &&
              has(app.root, ElementType::List) && has(app.root, ElementType::BlockQuote) &&
              has(app.root, ElementType::Link) && (!code || has(app.root, ElementType::CodeBlock)),
          where + ": the page has its headings, table, list, quote and links");
    check(app.contentHeight > 200.0f && !app.layoutTextRuns.empty(), where + ": and is laid out");
}

fs::path g_doc, g_notes, g_log, g_other;

// --- The setting's place in settings.ini ----------------------------------------

void theLine() {
    check(Settings().reopenTabs && std::make_unique<App>()->reopenTabs, "a new setup brings the tabs back");
    check(loadSettings().reopenTabs, "a settings.ini without the line brings them back, as before");
    const fs::path ini = getSettingsPath();

    Settings settings = loadSettings();
    settings.reopenTabs = false;
    saveSettings(settings);
    std::string written = readAll(ini);
    size_t at = written.find("reopenTabs=0");
    check(at != std::string::npos && (at == 0 || written[at - 1] == '\n') &&
              std::string("\r\n").find(written[at + 12]) != std::string::npos,
          "settings.ini has the line reopenTabs=0");
    check(!loadSettings().reopenTabs, "and it is read back");
    settings.reopenTabs = true;
    saveSettings(settings);
    check(readAll(ini).find("reopenTabs=1") != std::string::npos && loadSettings().reopenTabs,
          "reopenTabs=1 is written and read back");

    auto byHand = [&](const std::string& value) {
        std::string text = readAll(ini);
        const size_t line = text.find("reopenTabs=");
        if (line == std::string::npos) return false;
        text.replace(line, text.find('\n', line) - line, "reopenTabs=" + value);
        writeAll(ini, text);
        return loadSettings().reopenTabs;
    };
    check(!byHand("0"), "reopenTabs=0 by hand turns it off");
    check(byHand("1"), "reopenTabs=1 by hand turns it on");
    check(byHand("yes") && byHand(""), "a value that is neither keeps the tabs coming back");

    // Toggled, it is kept alone: the session in the file and the other
    // settings stay as they are
    settings = loadSettings();
    settings.zenWidthPct = 77;
    settings.reopenTabs = true;
    settings.sessionTabs = {utf8(g_doc), utf8(g_notes)};
    settings.sessionPinned = {utf8(g_notes)};
    settings.sessionActive = 1;
    saveSettings(settings);
    auto other = std::make_unique<App>();
    other->reopenTabs = false;
    other->zenWidthPct = 55;  // the window's own, not to be written with it
    persistReopenTabs(*other);
    const Settings kept = loadSettings();
    check(!kept.reopenTabs && kept.zenWidthPct == 77, "the toggle is kept alone, the moment it is made");
    check(same(kept.sessionTabs, {g_doc, g_notes}) && same(kept.sessionPinned, {g_notes}) && kept.sessionActive == 1,
          "the session in the file is not touched by it");
    other->reopenTabs = true;
    persistReopenTabs(*other);
    check(loadSettings().reopenTabs, "and so is the way back");
}

// --- The tabs a launch brings back ----------------------------------------------

void whatComesBack() {
    const fs::path gone = g_work / "gone.md";
    Settings s;
    s.sessionTabs = {utf8(g_doc), utf8(gone), utf8(g_notes), utf8(g_log)};
    s.sessionActive = 2;

    SessionTabs back = sessionToReopen(s);
    check(same(back.paths, {g_doc, g_notes, g_log}), "on: every tab whose file is still there, in order");
    check(back.active == 1, "on: the tab in front is still in front, past the file that is gone");
    s.sessionActive = 1;
    check(sessionToReopen(s).active == 0, "on: with the front tab's file gone the first tab is in front");
    s.sessionActive = 3;
    check(sessionToReopen(s).active == 2, "on: the last tab in front");

    s.reopenTabs = false;
    back = sessionToReopen(s);
    check(back.paths.empty() && back.active == 0, "off, nothing pinned: no tab comes back");

    s.sessionPinned = {utf8(g_notes)};
    s.sessionActive = 2;
    back = sessionToReopen(s);
    check(same(back.paths, {g_notes}) && back.active == 0, "off: the pinned tab comes back, alone and in front");
    s.sessionActive = 3;
    back = sessionToReopen(s);
    check(same(back.paths, {g_notes}) && back.active == 0, "off: with a forgotten tab in front, the pinned one is");

    s.sessionPinned = {utf8(g_log), utf8(g_doc)};
    s.sessionActive = 3;
    back = sessionToReopen(s);
    check(same(back.paths, {g_doc, g_log}) && back.active == 1, "off: two pinned tabs in the row's order, the front one in front");

    std::string shouted = utf8(g_notes);
    for (char& c : shouted) c = (char)toupper((unsigned char)c);
    s.sessionPinned = {shouted};
    check(same(sessionToReopen(s).paths, {g_notes}), "off: a pinned path is its tab whatever its capitals");

    s.sessionPinned = {utf8(gone)};
    check(sessionToReopen(s).paths.empty(), "off: a pinned tab whose file is gone does not come back");

    s.reopenTabs = true;
    s.sessionPinned = {utf8(g_notes)};
    check(same(sessionToReopen(s).paths, {g_doc, g_notes, g_log}), "on again: all of them, pinned or not");
    s.sessionTabs.clear();
    check(sessionToReopen(s).paths.empty(), "no session: nothing to bring back");
}

// --- The tabs a closing window leaves -------------------------------------------

void whatIsLeft() {
    Window window;
    App& app = window.app;
    if (!window.ok) { check(false, "a window for the test"); return; }
    tabsInit(app);  // the start page, a tab with no path
    tabOpenPath(app, app.hwnd, utf8(g_doc), true);
    tabOpenPath(app, app.hwnd, utf8(g_notes), true);
    tabOpenPath(app, app.hwnd, utf8(g_log), true);
    tabOpenStartPage(app, app.hwnd);  // and another after them
    check(app.tabs.size() == 5 && pathOf(app, 0).empty() && pathOf(app, 4).empty(),
          "three files are open between two tabs with no path");
    tabActivate(app, app.hwnd, indexOf(app, g_notes));

    Settings s;
    sessionKeep(s, app);
    check(same(s.sessionTabs, {g_doc, g_notes, g_log}) && s.sessionPinned.empty() && s.sessionActive == 1,
          "on: the three files are left, the one in front marked; the pathless tab is not");

    s.reopenTabs = false;
    sessionKeep(s, app);
    check(s.sessionTabs.empty() && s.sessionPinned.empty() && s.sessionActive == 0, "off, nothing pinned: nothing is left");

    pin(app, g_log);  // goes to the start of the row
    check(app.tabs[0].pinned && same(pathOf(app, 0), g_log), "the log is pinned, first in the row");
    sessionKeep(s, app);
    check(same(s.sessionTabs, {g_log}) && same(s.sessionPinned, {g_log}) && s.sessionActive == 0,
          "off: only the pinned tab is left, and left as pinned");
    tabActivate(app, app.hwnd, indexOf(app, g_log));
    sessionKeep(s, app);
    check(same(s.sessionTabs, {g_log}) && s.sessionActive == 0, "off: the pinned tab in front");
    pin(app, g_notes);
    tabActivate(app, app.hwnd, indexOf(app, g_notes));
    sessionKeep(s, app);
    check(same(s.sessionTabs, {g_log, g_notes}) && same(s.sessionPinned, {g_log, g_notes}) && s.sessionActive == 1,
          "off: two pinned tabs, the second in front");

    s.reopenTabs = true;
    sessionKeep(s, app);
    check(same(s.sessionTabs, {g_log, g_notes, g_doc}) && same(s.sessionPinned, {g_log, g_notes}) &&
              s.sessionActive == 1,
          "on: all three, the pinned ones named");

    // What was left before is replaced, not added to
    s.sessionTabs = {"C:\\old\\one.md", "C:\\old\\two.md"};
    s.sessionPinned = {"C:\\old\\one.md"};
    s.sessionActive = 1;
    s.reopenTabs = false;
    tabSetPinned(app, app.hwnd, 0, false);
    tabSetPinned(app, app.hwnd, 0, false);
    check(tabsPinnedCount(app) == 0, "both tabs are unpinned again");
    sessionKeep(s, app);
    check(s.sessionTabs.empty() && s.sessionPinned.empty() && s.sessionActive == 0,
          "off: an older session in the file is cleared, not kept");
}

// --- Closed and started again ---------------------------------------------------

void closedAndStarted() {
    auto threeTabs = [&](bool pinNotes) {
        auto window = start(&g_doc);
        App& app = window->app;
        tabOpenPath(app, app.hwnd, utf8(g_notes), true);
        tabOpenPath(app, app.hwnd, utf8(g_log), true);
        if (pinNotes) pin(app, g_notes);
        return window;
    };
    auto clearSession = [&] {
        Settings settings = loadSettings();
        settings.sessionTabs.clear();
        settings.sessionPinned.clear();
        settings.sessionActive = 0;
        saveSettings(settings);
    };
    const std::string logName = "reopen-log.md", notesName = "reopen-notes.md", docName = "reopen-doc.md";

    // On, as it always was
    clearSession();
    setReopen(true);
    auto window = threeTabs(false);
    check(holds(window->app, {g_doc, g_notes, g_log}) && shows(window->app, g_log), "three files are open, the log in front");
    close(window);
    check(sessionPart().find("tab=" + utf8(g_doc)) != std::string::npos &&
              sessionPart().find("tab=" + utf8(g_notes)) != std::string::npos &&
              sessionPart().find("tab=" + utf8(g_log)) != std::string::npos,
          "on: settings.ini's [Session] names the three tabs left open");

    window = start(nullptr);
    check(window->sessionCameBack && holds(window->app, {g_doc, g_notes, g_log}) && shows(window->app, g_log),
          "on: a launch with no file has the three tabs again, the log in front");
    check(tabsDrawn(window->app) == 3, "on: and the row draws three tabs");
    pageIsWhole(window->app, "on, the log", false);
    tabActivate(window->app, window->app.hwnd, 0);
    check(shows(window->app, g_doc), "on: a tab that came back opens when gone to");
    pageIsWhole(window->app, "on, the fixture");
    close(window);

    window = start(&g_other);
    check(holds(window->app, {g_doc, g_notes, g_log, g_other}) && shows(window->app, g_other),
          "on: a launch with another file has the three tabs and the file, in front");
    check(tabsDrawn(window->app) == 4, "on: four tabs in the row");
    close(window);

    // Off, nothing pinned: the report's own case
    clearSession();
    window = threeTabs(false);
    setReopen(false);  // toggled in the window while it is open
    close(window);
    std::string text = readAll(getSettingsPath());
    check(sessionPart().empty() && text.find("tab=") == std::string::npos && text.find("pinnedTab=") == std::string::npos,
          "off: the closing window writes no session, and no tab's path");
    check(text.find("[Recent]") != std::string::npos && text.find(logName) != std::string::npos,
          "off: the start page's recent files still name what was open");

    window = start(&g_other);
    check(!window->sessionCameBack && holds(window->app, {g_other}) && shows(window->app, g_other),
          "off: a file opened next comes up alone");
    check(!window->app.reopenTabs, "off: the window knows the setting as the file has it");
    pageIsWhole(window->app, "off, the file alone", false);
    close(window);
    window = start(nullptr);
    check(!window->sessionCameBack && window->app.tabs.size() == 1 && window->app.currentFile.empty(),
          "off: a launch with no file has one tab, with no file in it");
    close(window);

    // Off, with a tab pinned
    setReopen(true);
    clearSession();
    window = threeTabs(true);
    check(holds(window->app, {g_notes, g_doc, g_log}) && window->app.tabs[0].pinned, "the notes are pinned, first in the row");
    setReopen(false);
    close(window);
    text = sessionPart();
    check(text.find("tab=" + utf8(g_notes)) != std::string::npos &&
              text.find("pinnedTab=" + utf8(g_notes)) != std::string::npos &&
              text.find(logName) == std::string::npos && text.find(docName) == std::string::npos,
          "off: [Session] names the pinned tab, as pinned, and neither of the others");

    window = start(&g_other);
    check(window->sessionCameBack && holds(window->app, {g_notes, g_other}) && shows(window->app, g_other),
          "off: the pinned tab comes back beside the file opened, the file in front");
    check(window->app.tabs[0].pinned && !window->app.tabs[1].pinned && tabsPinnedCount(window->app) == 1,
          "off: and it is still pinned; the file is not");
    check(tabsDrawn(window->app) == 2, "off: two tabs in the row");
    const Frame row = uiframe::draw(window->app, [&] { renderTabStrip(window->app); });
    check(uiframe::save(window->app, row, (g_frames / "row-off-pinned-and-file.png").wstring()), "a picture of that row");
    tabActivate(window->app, window->app.hwnd, 0);
    check(shows(window->app, g_notes), "off: the pinned tab opens when gone to");
    pageIsWhole(window->app, "off, the pinned notes");
    close(window);

    window = start(nullptr);
    check(window->sessionCameBack && holds(window->app, {g_notes}) && window->app.tabs[0].pinned &&
              shows(window->app, g_notes),
          "off: a launch with no file has the pinned tab alone; the file opened last time is forgotten");
    close(window);

    // The file the launch is given is the pinned one
    window = start(&g_notes);
    check(holds(window->app, {g_notes}) && window->app.tabs[0].pinned, "off: opening the pinned file itself gives one tab, pinned");
    // Unpinned and closed: nothing is left
    tabSetPinned(window->app, window->app.hwnd, 0, false);
    close(window);
    check(sessionPart().empty(), "off: unpinned, the tab is not left either");

    // Two windows: one started while the tabs still came back, the setting
    // turned off in another since. Its close follows the file
    setReopen(true);
    clearSession();
    window = threeTabs(false);
    check(window->app.reopenTabs, "a window started with the setting on");
    {
        auto second = std::make_unique<App>();
        second->reopenTabs = false;
        persistReopenTabs(*second);
    }
    close(window);
    check(sessionPart().empty(), "turned off in another window: the older window's close leaves no tabs");

    // Turned on again: everything as it was
    setReopen(true);
    window = threeTabs(true);
    close(window);
    window = start(&g_other);
    check(holds(window->app, {g_notes, g_doc, g_log, g_other}) && window->app.tabs[0].pinned &&
              shows(window->app, g_other),
          "on again: all the tabs come back, the pinned one first, and the file joins them");
    close(window);

    // One window, one document ("Open files in tabs" off): no tab comes
    // back either way, as before
    Settings settings = loadSettings();
    settings.openInTabs = false;
    saveSettings(settings);
    window = start(&g_other);
    check(!window->sessionCameBack && holds(window->app, {g_other}), "Open files in tabs off: the file alone, as before");
    window.reset();  // not this test's close: the session stays in the file
    settings = loadSettings();
    settings.openInTabs = true;
    saveSettings(settings);
    clearSession();
}

// --- Settings > General ---------------------------------------------------------

const D2D1_RECT_F* hitOf(const App& app, int action) {
    for (const auto& hit : app.settingsHits)
        if (hit.second == action) return &hit.first;
    return nullptr;
}
void click(App& app, const D2D1_RECT_F& r) {
    handleMouseDown(app, app.hwnd, MK_LBUTTON, uiframe::centre(r));
    handleMouseUp(app, app.hwnd, 0, uiframe::centre(r));
}
Frame settingsFrame(App& app, int w, int h) {
    app.width = w;
    app.height = h;
    app.showSettings = true;
    app.settingsAnimation = 1.0f;
    app.settingsSection = 0;
    app.settingsScroll = 0.0f;
    app.settingsLangOpen = app.settingsKeysOpen = false;
    app.mouseX = app.mouseY = -100;
    return uiframe::draw(app, [&] { renderSettingsOverlay(app); });
}
bool speak(App& app, const char* id) {
    const int index = languageIndexById(id);
    if (index < 0) return false;
    app.languageSetting = app.currentLanguageIndex = index;
    updateOverlayFormats(app);
    return true;
}
// The row of the page a control belongs to
const App::SettingsRowBox* rowOf(const App& app, const D2D1_RECT_F& control) {
    for (const auto& row : app.settingsRows)
        if (uiframe::inside(control, row.row)) return &row;
    return nullptr;
}

void settingsRow() {
    using uiframe::inside;
    Window window;
    App& app = window.app;
    if (!window.ok) { check(false, "a window for the test"); return; }
    tabsInit(app);
    setReopen(true);
    app.reopenTabs = true;

    loadLanguageOverrides();  // German, French, Italian and Russian
    const std::wstring english = tr(languageIndexById("en"), "settings.reopen_tabs");
    const std::wstring englishHint = tr(languageIndexById("en"), "settings.reopen_tabs.hint");
    for (const char* lang : {"en", "zh", "ja", "ko", "de", "fr", "it", "ru"}) {
        check(speak(app, lang), std::string(lang) + ": the interface speaks it");
        for (const char* key : {"settings.reopen_tabs", "settings.reopen_tabs.hint"})
            check(languageTranslatesKey(app.currentLanguageIndex, key), std::string(lang) + " has " + key);
        const bool isEnglish = std::string(lang) == "en";
        check((std::wstring(tr(app, "settings.reopen_tabs")) == english) == isEnglish &&
                  (std::wstring(tr(app, "settings.reopen_tabs.hint")) == englishHint) == isEnglish,
              std::string(lang) + ": the row and its hint are in its own words");
        for (int w : {1300, 760, 420}) {
            const std::string where = std::string(lang) + " at " + std::to_string(w);
            settingsFrame(app, w, 900);
            const D2D1_RECT_F panel = settingsPanelRect(app);
            const D2D1_RECT_F* toggle = hitOf(app, SET_TOGGLE_REOPENTABS);
            const D2D1_RECT_F* tabs = hitOf(app, SET_TOGGLE_OPENTABS);
            const D2D1_RECT_F* deleted = hitOf(app, SET_TOGGLE_KEEPDELETED);
            check(toggle && tabs && deleted, where + ": the row's toggle is on the page");
            if (!toggle || !tabs || !deleted) continue;
            const App::SettingsRowBox* row = rowOf(app, *toggle);
            const App::SettingsRowBox* above = rowOf(app, *tabs);
            const App::SettingsRowBox* below = rowOf(app, *deleted);
            check(row && above && below, where + ": each toggle is in a row");
            if (!row || !above || !below) continue;
            check(row->group == 1 && above->group == 1 && below->group == 1, where + ": in the files and tabs group");
            check(uiframe::about(above->row.bottom, row->row.top) && uiframe::about(row->row.bottom, below->row.top),
                  where + ": right under Open files in tabs, over the row that followed it");
            check(inside(row->row, panel) && inside(*toggle, row->row) && uiframe::overlap(*toggle, row->control),
                  where + ": inside the dialog, the toggle in the row's control");
            check(uiframe::about(uiframe::width(*toggle), uiframe::width(*tabs)) &&
                      uiframe::about(toggle->left, tabs->left),
                  where + ": the toggle is the size of its neighbour's, under it");
            check(uiframe::about(uiframe::height(row->row), uiframe::height(above->row)),
                  where + ": the row is as tall as its neighbour");
            check(!row->stacked && row->text.right <= row->control.left + 0.5f, where + ": the label ends before the toggle");
            check(w < 760 || !row->titleTrimmed, where + ": the label is whole");
            check(w < 760 || !row->hintTrimmed, where + ": the hint is whole");
        }
    }
    speak(app, "en");

    // A click turns it off, keeps that at once, and draws the toggle off
    Frame last = settingsFrame(app, 1300, 900);
    for (bool want : {false, true, false}) {
        const std::string where = want ? "a click to turn it on" : "a click to turn it off";
        const D2D1_RECT_F* target = hitOf(app, SET_TOGGLE_REOPENTABS);
        check(target != nullptr, where + ": the toggle is there");
        if (!target) continue;
        const D2D1_RECT_F at = *target;
        const bool tabsBefore = app.openInTabs, deletedBefore = app.keepDeletedTabs;
        click(app, at);
        check(app.reopenTabs == want, where + ": does");
        check(loadSettings().reopenTabs == want, where + ": and settings.ini has it at once");
        check(app.showSettings && app.openInTabs == tabsBefore && app.keepDeletedTabs == deletedBefore,
              where + ": the dialog stays open and its neighbours as they were");
        const Frame now = settingsFrame(app, 1300, 900);
        check(now.differs(last, at), where + ": the toggle is drawn the other way");
        last = now;
    }
    // Off now. The window's own tabs stay as they are until it closes
    app.showSettings = false;
    tabOpenPath(app, app.hwnd, utf8(g_doc), true);
    tabOpenPath(app, app.hwnd, utf8(g_notes), true);
    settingsFrame(app, 1300, 900);
    const size_t open = app.tabs.size();
    if (const D2D1_RECT_F* target = hitOf(app, SET_TOGGLE_REOPENTABS)) click(app, *target);
    check(app.reopenTabs && app.tabs.size() == open && shows(app, g_notes), "toggling leaves the open tabs alone");

    // Pictures: the page in a wide and a narrow dialog, on and off
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (const char* lang : {"en", "zh", "ja", "ko", "de", "fr", "it", "ru"}) {
            speak(app, lang);
            for (int w : {1300, 420}) {
                if (theme && std::string(lang) != "en") continue;  // dark once
                app.reopenTabs = !(theme && w == 1300);  // the dark, wide one with it off
                const Frame frame = settingsFrame(app, w, 900);
                const std::string file = std::string("settings-general-") + lang + "-" + std::to_string(w) +
                                         (theme ? "-dark" : "-light");
                check(uiframe::save(app, frame, (g_frames / (file + ".png")).wstring()), "a picture of " + file);
            }
        }
    }
    app.showSettings = false;
    setReopen(true);
}
}  // namespace

int runReopenTabsTests(const char* fixtureRoot) {
    HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 2;
    g_fixtures = fixtureRoot;
    g_work = fs::current_path() / "reopen-tabs";
    g_frames = fs::current_path() / "frames";
    std::error_code ignored;
    fs::remove_all(g_work, ignored);
    fs::create_directories(g_work);
    fs::create_directories(g_frames);
    g_doc = fresh("reopen-doc.md", "reopen-tabs-323.md");
    g_notes = fresh("reopen-notes.md", "reopen-tabs-323-notes.md");
    g_log = fresh("reopen-log.md", "reopen-tabs-323-log.md");
    g_other = fresh("asked-for.md", "reopen-tabs-323-log.md");

    theLine();
    whatComesBack();
    whatIsLeft();
    closedAndStarted();
    settingsRow();

    CoUninitialize();
    OleUninitialize();
    std::cout << "Reopen tabs: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
