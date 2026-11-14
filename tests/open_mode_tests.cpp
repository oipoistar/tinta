#include "d2d_init.h"
#include "editor.h"
#include "file_utils.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "overlays.h"
#include "render.h"
#include "search.h"
#include "search_panel.h"
#include "settings.h"
#include "sheet_ui.h"
#include "tabs.h"
#include "ui_frame.h"
#include "utils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

// The view a Markdown file opens in (#322). Tinta opened every file in the
// reading view; Settings > Editor now says whether a Markdown file opens
// there, or in the editor in Live or on its source.
//
// Each way a file is opened is taken with each choice, and what must stay
// as it was is checked beside it: a tab the reader went back to reading in,
// the files that are their own source, a find result, a tab reopened.
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
// A file's text as the editor holds it: one line feed a line
std::wstring editorTextOf(const fs::path& path) {
    std::string text = readAll(path), lf;
    for (size_t i = 0; i < text.size(); i++)
        if (!(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')) lf += text[i];
    return toWide(lf);
}
// A copy of a fixture under a name of its own, so that each part of the
// test opens a file no tab has yet
fs::path fresh(const std::string& name, const char* fixture = "open-mode-322.md") {
    const fs::path to = g_work / name;
    std::error_code ignored;
    fs::copy_file(g_fixtures / fixture, to, fs::copy_options::overwrite_existing, ignored);
    return to;
}

enum class View { Reading, Live, Source };
View viewOf(const App& app) {
    return !app.editMode ? View::Reading : editorLiveOn(app) ? View::Live : View::Source;
}
const char* name(View view) { return view == View::Reading ? "reading" : view == View::Live ? "Live" : "Source"; }
View viewFor(int mode) { return mode == OPEN_LIVE ? View::Live : mode == OPEN_SOURCE ? View::Source : View::Reading; }
bool shows(const App& app, const fs::path& path) { return _stricmp(app.currentFile.c_str(), utf8(path).c_str()) == 0; }
int tabOf(const App& app, const fs::path& path) {
    for (int i = 0; i < (int)app.tabs.size(); i++) {
        const std::string& open = i == app.activeTab ? app.currentFile : app.tabs[i].path;
        if (_stricmp(open.c_str(), utf8(path).c_str()) == 0) return i;
    }
    return -1;
}

void drawEditor(App& app) {
    app.renderTarget->Clear(app.theme.background);
    if (editorLiveOn(app)) {
        D2D1_MATRIX_3X2_F saved;
        app.renderTarget->GetTransform(&saved);
        app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(documentViewportX(app), 0) * saved);
        renderLivePage(app);
        app.renderTarget->SetTransform(saved);
    } else if (app.editMode) {
        renderEditor(app, editorPaneWidth(app));
    }
}
// A frame, as the window would draw one
void paint(App& app) {
    if (app.editMode && !editorPreviewVisible(app)) {
        // No page beside the source: nothing to lay out
    } else if (app.layoutDirty || !app.layoutComplete) {
        ensureLayoutComplete(app);
    }
    app.renderTarget->BeginDraw();
    drawEditor(app);
    app.renderTarget->EndDraw();
}
void picture(App& app, const std::string& file) {
    if (app.layoutDirty || !app.layoutComplete) ensureLayoutComplete(app);
    const Frame frame = uiframe::draw(app, [&] { drawEditor(app); });
    check(uiframe::save(app, frame, (g_frames / (file + ".png")).wstring()), "a picture of " + file + " is kept");
}
// A click of its own, not the second of a double click
void clickAt(App& app, const D2D1_RECT_F& r) {
    app.clickCount = 0;
    app.lastClickTime = {};
    const LPARAM at = uiframe::centre(r);
    handleMouseMove(app, app.hwnd, at);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at);
    handleMouseUp(app, app.hwnd, 0, at);
}
// The editor left without a question, back to the page
void toReading(App& app) {
    if (!app.editMode) return;
    app.editorDirty = false;
    exitEditMode(app);
}
// Whether Live has its stretch of the page laid out to be typed in
bool livePlaced(const App& app) {
    float top = 0, bottom = 0, shift = 0;
    return livePageGeometry(app, top, bottom, shift);
}
// The line of the source the caret is on, counted from 0
size_t caretLine(const App& app) {
    return (size_t)std::count(app.editorText.begin(),
                              app.editorText.begin() + std::min(app.editorCursorPos, app.editorText.size()), L'\n');
}

// --- The setting's name and its place in settings.ini -------------------------

void names(App& app) {
    check(std::string(openModeName(OPEN_READING)) == "reading" && std::string(openModeName(OPEN_LIVE)) == "live" &&
              std::string(openModeName(OPEN_SOURCE)) == "source",
          "the three views have their names");
    check(std::string(openModeName(7)) == "reading", "a number that is no view is the reading view");
    check(openModeByName("reading") == OPEN_READING && openModeByName("live") == OPEN_LIVE &&
              openModeByName("source") == OPEN_SOURCE,
          "each name is its view");
    check(openModeByName("Live") == OPEN_LIVE && openModeByName(" SOURCE ") == OPEN_SOURCE,
          "a name typed by hand may have capitals and spaces around it");
    check(openModeByName("") == OPEN_READING && openModeByName("typora") == OPEN_READING &&
              openModeByName("1") == OPEN_READING && openModeByName("livesource") == OPEN_READING,
          "a name that is none of the three is the reading view");

    Settings settings = loadSettings();
    check(settings.openMode == OPEN_READING, "a settings.ini without the line opens files in the reading view");
    check(Settings().openMode == OPEN_READING && std::make_unique<App>()->openMode == OPEN_READING,
          "which is where a new one starts");
    settings.openMode = OPEN_LIVE;
    saveSettings(settings);
    const fs::path ini = getSettingsPath();
    const std::string written = readAll(ini);
    const size_t lineAt = written.find("openMode=live");
    check(lineAt != std::string::npos && (lineAt == 0 || written[lineAt - 1] == '\n') &&
              std::string("\r\n").find(written[lineAt + 13]) != std::string::npos,
          "settings.ini has the line openMode=live");
    check(loadSettings().openMode == OPEN_LIVE, "and it is read back");
    auto byHand = [&](const std::string& value) {
        std::string text = readAll(ini);
        const size_t at = text.find("openMode=");
        if (at == std::string::npos) return -1;
        text.replace(at, text.find('\n', at) - at, "openMode=" + value);
        writeAll(ini, text);
        return loadSettings().openMode;
    };
    check(byHand("Source") == OPEN_SOURCE, "openMode=Source written by hand is the source");
    check(byHand("sideways") == OPEN_READING, "a value that is no view is the reading view");

    // Picked in Settings it is kept at once, and nothing else is touched
    settings = loadSettings();
    settings.zenWidthPct = 77;
    settings.openMode = OPEN_READING;
    saveSettings(settings);
    app.openMode = OPEN_SOURCE;
    app.zenWidthPct = 55;  // the window's own, not to be written with it
    persistOpenMode(app);
    const Settings kept = loadSettings();
    check(kept.openMode == OPEN_SOURCE && kept.zenWidthPct == 77, "the choice is kept alone, the moment it is made");
    app.openMode = OPEN_READING;
    persistOpenMode(app);
    check(loadSettings().openMode == OPEN_READING, "and so is the way back");
}

// --- A file opened in a tab ----------------------------------------------------

void opensInTabs(App& app) {
    for (int mode : {OPEN_READING, OPEN_LIVE, OPEN_SOURCE}) {
        const View want = viewFor(mode);
        const std::string where = std::string("opened with ") + openModeName(mode);
        app.openMode = mode;
        const fs::path file = fresh(std::string("tab-") + openModeName(mode) + ".md");
        const size_t tabs = app.tabs.size();
        tabOpenPath(app, app.hwnd, utf8(file), true);
        check(shows(app, file) && app.tabs.size() == tabs + 1, where + ": the file has its tab");
        check(viewOf(app) == want, where + ": it is in the " + name(want) + " view, not " + name(viewOf(app)));
        check(openModeApplies(app) == false, where + ": and is placed once");
        paint(app);
        if (want == View::Reading) {
            check(app.editorText.empty() && !app.editRailShown, where + ": no editor is behind the page");
            continue;
        }
        check(app.editorText == editorTextOf(file) && !app.editorDirty, where + ": the editor holds the file, unchanged");
        check(app.editorCursorPos == 0 && !app.editorHasSelection, where + ": the caret is at the start");
        if (want == View::Live) check(livePlaced(app), where + ": the page is there to be typed in");
        // A letter typed lands in the text, as after E
        typeKeys(app, L"z");
        check(app.editorText == L"z" + editorTextOf(file) && app.editorDirty, where + ": a letter typed is in the text");
        keystroke(app, 'Z', {true, false});
        check(app.editorText == editorTextOf(file), where + ": and Ctrl+Z takes it back");
        app.editorDirty = false;
        picture(app, std::string("opened-") + openModeName(mode));
        // Esc is the way back to the page it always was
        exitEditMode(app);
        check(viewOf(app) == View::Reading && shows(app, file), where + ": leaving the editor shows the page");
        check(readAll(file) == readAll(g_fixtures / "open-mode-322.md"), where + ": the file is as it was");
    }
}

// The rail's pill is the editor's own; the setting says where a file opens
void pillAndSetting(App& app) {
    app.openMode = OPEN_LIVE;
    tabOpenPath(app, app.hwnd, utf8(fresh("pill-a.md")), true);
    check(viewOf(app) == View::Live, "Live: a file opens in Live");
    setEditorLive(app, app.hwnd, false);
    check(viewOf(app) == View::Source && !loadSettings().editorLive, "the pill takes the editor to the source");
    tabOpenPath(app, app.hwnd, utf8(fresh("pill-b.md")), true);
    check(viewOf(app) == View::Live, "Live: the next file opens in Live, wherever the pill was left");
    check(app.editorLiveAnim == 1.0f, "with the pill's knob in its place, not on the way");
    check(loadSettings().editorLive, "and the pill is kept as it now stands");

    app.openMode = OPEN_SOURCE;
    tabOpenPath(app, app.hwnd, utf8(fresh("pill-c.md")), true);
    check(viewOf(app) == View::Source && app.editorLiveAnim == 0.0f, "Source: a file opens on its source after Live");
    setEditorLive(app, app.hwnd, true);
    check(viewOf(app) == View::Live, "the pill takes it to Live");
    tabOpenPath(app, app.hwnd, utf8(fresh("pill-d.md")), true);
    check(viewOf(app) == View::Source, "Source: the next file opens on its source again");
    toReading(app);
}

// A tab is placed when it is first shown, and then left as the reader has it
void tabsAsLeft(App& app) {
    app.openMode = OPEN_LIVE;
    const fs::path a = fresh("left-a.md"), b = fresh("left-b.md"), c = fresh("left-c.md");
    tabOpenPath(app, app.hwnd, utf8(a), true);
    check(viewOf(app) == View::Live, "a opens in Live");
    toReading(app);
    tabOpenPath(app, app.hwnd, utf8(b), true);
    check(viewOf(app) == View::Live && shows(app, b), "b opens in Live");
    tabActivate(app, app.hwnd, tabOf(app, a));
    check(shows(app, a) && viewOf(app) == View::Reading, "back on a, left in the reading view, it is still read");
    tabActivate(app, app.hwnd, tabOf(app, b));
    check(shows(app, b) && viewOf(app) == View::Live, "and b, left in Live, is still in Live");
    // The same path asked for again goes to its tab as it is
    tabActivate(app, app.hwnd, tabOf(app, a));
    const size_t tabs = app.tabs.size();
    tabOpenPath(app, app.hwnd, utf8(b), true);
    check(app.tabs.size() == tabs && shows(app, b) && viewOf(app) == View::Live, "a file open already keeps its view");
    tabOpenPath(app, app.hwnd, utf8(a), true);
    check(app.tabs.size() == tabs && viewOf(app) == View::Reading, "also the one that is being read");

    // Opened behind the tab in front, as the second of two files dropped
    tabOpenPath(app, app.hwnd, utf8(c), false);
    const int at = tabOf(app, c);
    check(at >= 0 && shows(app, a) && viewOf(app) == View::Reading, "a file opened behind leaves the tab in front alone");
    check(at >= 0 && app.tabs[at].openModePending, "and waits to be placed");
    tabActivate(app, app.hwnd, at);
    check(shows(app, c) && viewOf(app) == View::Live, "shown for the first time, it is in Live");
    check(!app.tabs[app.activeTab].openModePending, "and is placed");
    toReading(app);
    tabActivate(app, app.hwnd, tabOf(app, a));
    tabActivate(app, app.hwnd, tabOf(app, c));
    check(viewOf(app) == View::Reading, "then it too stays as it was left");

    // The setting changed while a tab waits: the tab opens as it says then
    const fs::path d = fresh("left-d.md");
    tabOpenPath(app, app.hwnd, utf8(d), false);
    app.openMode = OPEN_READING;
    tabActivate(app, app.hwnd, tabOf(app, d));
    check(viewOf(app) == View::Reading, "a waiting tab opens by the setting as it is when shown");
    app.openMode = OPEN_LIVE;
    tabActivate(app, app.hwnd, tabOf(app, a));
    tabActivate(app, app.hwnd, tabOf(app, d));
    check(viewOf(app) == View::Reading, "and is not placed a second time");
}

// --- Files that are not Markdown ----------------------------------------------

void otherKinds(App& app) {
    writeAll(g_work / "diagram.mmd", "flowchart LR\n  A[Open] --> B{Markdown?}\n  B -- yes --> C[Editor]\n");
    writeAll(g_work / "sequence.puml", "@startuml\nAlice -> Bob: open\n@enduml\n");
    writeAll(g_work / "plain.txt", "A text file is its own source.\n");
    writeAll(g_work / "data.json", "{\"openMode\": \"live\"}\n");
    std::error_code ignored;
    fs::copy(g_fixtures / "sheets", g_work / "sheets", fs::copy_options::recursive, ignored);
    for (int mode : {OPEN_LIVE, OPEN_SOURCE}) {
        app.openMode = mode;
        for (const char* file : {"diagram.mmd", "sequence.puml", "plain.txt", "data.json"}) {
            const std::string where = std::string(file) + " with " + openModeName(mode);
            // Closed after, so that the next round opens it anew
            tabOpenPath(app, app.hwnd, utf8(g_work / file), true);
            check(shows(app, g_work / file), where + ": opens");
            check(viewOf(app) == View::Reading && !openModeApplies(app), where + ": is read, as it always was");
            tabCloseIndex(app, app.hwnd, app.activeTab, TabClose::Internal);
        }
        const fs::path sheet = g_work / "sheets" / "Launch plan.smd";
        tabOpenPath(app, app.hwnd, utf8(sheet), true);
        check(shows(app, sheet) && sheetActive(app) && !app.editMode,
              std::string("a sheet with ") + openModeName(mode) + ": opens as a sheet");
        tabCloseIndex(app, app.hwnd, app.activeTab, TabClose::Internal);
    }
    // A Markdown file by its other ending
    app.openMode = OPEN_SOURCE;
    const fs::path long_ = fresh("other-ending.markdown");
    tabOpenPath(app, app.hwnd, utf8(long_), true);
    check(viewOf(app) == View::Source, ".markdown is a Markdown file");
    toReading(app);

    // A file gone before its tab was first shown: its last text is read,
    // not made an unsaved editor of
    app.openMode = OPEN_LIVE;
    tabsSetKeepDeleted(app, true);
    const fs::path gone = fresh("gone.md");
    tabOpenPath(app, app.hwnd, utf8(gone), false);
    fs::remove(gone, ignored);
    tabActivate(app, app.hwnd, tabOf(app, gone));
    check(shows(app, gone) && viewOf(app) == View::Reading && !app.unsavedNow(),
          "a file deleted before its tab was shown is read from what was kept of it");
    tabCloseIndex(app, app.hwnd, app.activeTab, TabClose::Internal);
    tabsSetKeepDeleted(app, false);
}

// --- Opened in place: the folder browser, its path field, a wiki link ---------

bool browserClick(App& app, const fs::path& folder, const std::wstring& file) {
    app.showFolderBrowser = true;
    app.folderBrowserAnimation = 1.0f;
    app.folderBrowserPath = folder.wstring();
    app.folderBrowserScroll = 0.0f;
    populateFolderItems(app);
    int index = -1;
    for (int i = 0; i < (int)app.folderItems.size(); i++)
        if (app.folderItems[i].name == file) index = i;
    if (index < 0) return false;
    const FolderBrowserMetrics g = folderBrowserMetrics(app);
    const float x = g.cardLeft + dpi(app, 48.0f);
    const float y = g.listStartY + folderItemContentY(g, index) + g.itemHeight * 0.5f;
    if (folderItemIndexAt(app, x, y) != index) return false;
    const LPARAM at = MAKELPARAM((int)x, (int)y);
    handleMouseMove(app, app.hwnd, at);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at);
    handleMouseUp(app, app.hwnd, 0, at);
    return true;
}

void inPlace(App& app) {
    const fs::path folder = g_work / "browse";
    fs::create_directories(folder);
    for (const char* file : {"one.md", "two.md", "three.md"}) {
        std::error_code ignored;
        fs::copy_file(g_fixtures / "open-mode-322-notes.md", folder / file, fs::copy_options::overwrite_existing, ignored);
    }
    app.openMode = OPEN_READING;
    tabOpenPath(app, app.hwnd, utf8(fresh("browse-from.md")), true);
    size_t tabs = app.tabs.size();
    check(browserClick(app, folder, L"one.md"), "the folder browser's row is clicked");
    check(shows(app, folder / "one.md") && app.tabs.size() == tabs && viewOf(app) == View::Reading &&
              app.showFolderBrowser,
          "Reading: a file picked in the browser is read in this tab, the browser beside it");
    app.openMode = OPEN_LIVE;
    check(browserClick(app, folder, L"two.md"), "the next row is clicked");
    check(shows(app, folder / "two.md") && app.tabs.size() == tabs && viewOf(app) == View::Live,
          "Live: a file picked in the browser opens in Live, in this tab");
    check(!app.showFolderBrowser, "and the editor closes the browser, as E does");
    check(app.editorText == editorTextOf(folder / "two.md"), "the editor holds the picked file");
    toReading(app);

    // The path typed into the browser's field
    app.openMode = OPEN_SOURCE;
    app.showFolderBrowser = true;
    app.folderBrowserAnimation = 1.0f;
    app.folderBrowserEditingPath = true;
    app.folderBrowserInput = (folder / "three.md").wstring();
    keystroke(app, VK_RETURN);
    check(shows(app, folder / "three.md") && viewOf(app) == View::Source && app.tabs.size() == tabs,
          "Source: a path typed into the browser's field opens on its source");
    check(!app.folderBrowserEditingPath, "and the field is closed");
    toReading(app);
    app.showFolderBrowser = false;

    // A wiki link on the page opens its note in this tab
    const fs::path page = fresh("wiki-from.md");
    fresh("open-mode-322-notes.md", "open-mode-322-notes.md");
    app.openMode = OPEN_READING;
    tabOpenPath(app, app.hwnd, utf8(page), true);
    ensureLayoutComplete(app);
    app.scrollY = app.targetScrollY = 0.0f;
    tabs = app.tabs.size();
    const App::LinkRect* wiki = nullptr;
    for (const auto& link : app.linkRects)
        if (link.url.rfind("wiki:", 0) == 0) wiki = &link;
    check(wiki != nullptr, "the page has its wiki link");
    if (!wiki) return;
    app.openMode = OPEN_SOURCE;
    clickAt(app, wiki->bounds);
    const fs::path notes = g_work / "open-mode-322-notes.md";
    check(shows(app, notes) && app.tabs.size() == tabs, "the wiki link opens its note in this tab");
    check(viewOf(app) == View::Source, "Source: on its source");

    // Back is a way to a place that was being read, not a file opened
    toReading(app);
    check(!app.navBack.empty(), "the jump can be gone back on");
    navigateBack(app, app.hwnd);
    check(shows(app, page) && viewOf(app) == View::Reading, "Back returns to the page that was read, in the reading view");
}

// --- A link to a heading of another file ---------------------------------------

void linkToHeading(App& app) {
    const fs::path page = fresh("link-from.md");
    fresh("open-mode-322-notes.md", "open-mode-322-notes.md");
    const fs::path notes = g_work / "open-mode-322-notes.md";
    for (int mode : {OPEN_LIVE, OPEN_SOURCE}) {
        const std::string where = std::string("a link to a heading with ") + openModeName(mode);
        const int open = tabOf(app, notes);
        if (open >= 0) {
            tabActivate(app, app.hwnd, open);
            toReading(app);
            tabCloseIndex(app, app.hwnd, app.activeTab, TabClose::Internal);
        }
        app.openMode = OPEN_READING;
        tabOpenPath(app, app.hwnd, utf8(page), true);
        toReading(app);
        ensureLayoutComplete(app);
        app.scrollY = app.targetScrollY = 0.0f;
        const App::LinkRect* link = nullptr;
        for (const auto& rect : app.linkRects)
            if (rect.url.find("second-part") != std::string::npos) link = &rect;
        check(link != nullptr, where + ": the page has the link");
        if (!link) return;
        app.openMode = mode;
        const size_t tabs = app.tabs.size();
        clickAt(app, link->bounds);
        check(shows(app, notes) && app.tabs.size() == tabs + 1, where + ": the note joins as a tab");
        check(viewOf(app) == viewFor(mode), where + ": in the " + name(viewFor(mode)) + " view");
        const std::wstring line = app.editorText.substr(std::min(app.editorCursorPos, app.editorText.size()), 14);
        check(line == L"## Second part", where + ": the caret is on the heading the link names");
        toReading(app);
    }
}

// --- A find result opens to show its match --------------------------------------

void findResult(App& app) {
    const fs::path page = fresh("find-from.md");
    const fs::path hit = fresh("find-hit.md", "open-mode-322-notes.md");
    app.openMode = OPEN_READING;
    tabOpenPath(app, app.hwnd, utf8(page), true);
    app.openMode = OPEN_LIVE;
    openSearchInput(app);
    app.searchQuery = L"lantern";
    performSearch(app);
    App::FolderFileResult result;
    result.fileName = L"find-hit.md";
    result.fullPath = hit.wstring();
    App::FolderMatch match;
    match.snippet = L"the word lantern is here once";
    match.line = 8;
    result.matches.push_back(match);
    result.totalMatches = 1;
    app.folderResults.assign(1, result);
    searchPanelOpenFile(app, app.hwnd, 0, 0);
    check(shows(app, hit), "the find result's file opens");
    check(viewOf(app) == View::Reading, "Live: a find result is read, with its match on the page");
    check(app.showSearch && !app.searchMatches.empty(), "the find bar is still open on it, with the match found");
    check(!app.openModeHeld, "the setting is held no longer than that");
    closeSearchInput(app);
    tabActivate(app, app.hwnd, tabOf(app, page));
    tabActivate(app, app.hwnd, tabOf(app, hit));
    check(viewOf(app) == View::Reading, "and its tab stays in the reading view");
    const fs::path next = fresh("find-next.md");
    tabOpenPath(app, app.hwnd, utf8(next), true);
    check(viewOf(app) == View::Live, "the next file opened is in Live again");
    toReading(app);
}

// --- From the start page: the file takes the empty tab's place -----------------

void fromStartPage(App& app) {
    // Ctrl+O and a recent file open the file in a tab and close the empty
    // one they were asked from, with the new tab already in front
    app.openMode = OPEN_LIVE;
    tabOpenStartPage(app, app.hwnd);
    check(app.currentFile.empty() && !app.editMode, "a new tab is the start page");
    const int shell = app.tabs[app.activeTab].id;
    const size_t tabs = app.tabs.size();
    const fs::path file = fresh("from-start.md");
    tabOpenPath(app, app.hwnd, utf8(file));
    for (int i = 0; i < (int)app.tabs.size(); i++) {
        if (app.tabs[i].id == shell) {
            tabCloseIndex(app, app.hwnd, i, TabClose::Internal);
            break;
        }
    }
    check(app.tabs.size() == tabs && shows(app, file), "the file has taken the start page's place");
    check(viewOf(app) == View::Live && app.editorText == editorTextOf(file) && !app.editorDirty,
          "Live: it is in the editor, whole and unchanged");
    paint(app);
    typeKeys(app, L"q");
    check(app.editorText == L"q" + editorTextOf(file), "and takes what is typed");
    keystroke(app, 'Z', {true, false});
    toReading(app);
}

// --- A closed tab reopened, and the tabs a session comes back with -------------

void reopenedAndSession(App& app) {
    app.openMode = OPEN_LIVE;
    const fs::path read = fresh("closed-read.md"), edited = fresh("closed-edited.md");
    tabOpenPath(app, app.hwnd, utf8(read), true);
    toReading(app);
    tabCloseIndex(app, app.hwnd, app.activeTab);
    check(tabOf(app, read) < 0, "a tab left in the reading view is closed");
    check(tabReopenClosed(app, app.hwnd) && shows(app, read), "and reopened");
    check(viewOf(app) == View::Reading, "it comes back as it was closed, in the reading view");
    tabOpenPath(app, app.hwnd, utf8(edited), true);
    check(viewOf(app) == View::Live, "a second file opens in Live");
    tabCloseIndex(app, app.hwnd, app.activeTab);
    check(tabOf(app, edited) < 0, "and is closed in the editor");
    check(tabReopenClosed(app, app.hwnd) && shows(app, edited) && viewOf(app) == View::Live,
          "it comes back in the editor, as it was closed");
    toReading(app);

    // A session's tabs: the one in the window is the launch's to place,
    // the others open when they are first shown
    const fs::path first = fresh("session-1.md"), second = fresh("session-2.md"), third = fresh("session-3.md");
    app.openMode = OPEN_READING;
    tabOpenPath(app, app.hwnd, utf8(second), true);
    app.openMode = OPEN_SOURCE;
    tabsSeedSession(app, {utf8(first), utf8(second), utf8(third)});
    check(app.tabs.size() == 3 && app.activeTab == 1, "the session's three tabs are back, the second in front");
    check(app.tabs[0].openModePending && !app.tabs[1].openModePending && app.tabs[2].openModePending,
          "the tabs behind wait to be placed; the one in front is the launch's");
    tabActivate(app, app.hwnd, 2);
    check(shows(app, third) && viewOf(app) == View::Source, "Source: a session's tab opens on its source when shown");
    tabActivate(app, app.hwnd, 0);
    check(shows(app, first) && viewOf(app) == View::Source, "and so does the other");
    tabActivate(app, app.hwnd, 2);
    check(viewOf(app) == View::Source, "left in the editor, a tab is found in the editor");
    toReading(app);
    tabActivate(app, app.hwnd, 0);
    toReading(app);
}

// --- The place the file was left at ---------------------------------------------

void placeLeftAt(App& app) {
    // A long page: the notes forty times over, each under a heading of its own
    std::string text;
    // With line feeds alone, whatever the checkout gave the fixture
    const std::string body = toUtf8(editorTextOf(g_fixtures / "open-mode-322-notes.md"));
    for (int i = 1; i <= 40; i++) text += "# Round " + std::to_string(i) + "\n\n" + body + "\n";
    const float place = 4200.0f;
    // Where E opens the editor from that place on the page
    app.openMode = OPEN_READING;
    const fs::path byHand = g_work / "place-e.md";
    writeAll(byHand, text);
    persistReadingPosition(utf8(byHand), place);
    auto before = std::chrono::steady_clock::now();
    tabOpenPath(app, app.hwnd, utf8(byHand), true);
    layoutDocumentViewportFirst(app);
    auto ms = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count(); };
    std::cout << "  the same file opened to be read, laid out for its first frame: " << ms() << " ms\n";
    ensureLayoutComplete(app);
    check(app.contentHeight > place + (float)app.height, "the long page is longer than the place asked for");
    app.scrollY = app.targetScrollY = place;
    app.editorLive = false;
    enterEditMode(app);
    const size_t line = caretLine(app);
    const float sourceScroll = app.editorScrollY;
    check(line > 100 && sourceScroll > 0.0f, "E opens the source far down, at the place being read");
    toReading(app);

    for (int mode : {OPEN_LIVE, OPEN_SOURCE}) {
        const std::string where = std::string("a file left far down, opened with ") + openModeName(mode);
        const fs::path file = g_work / (std::string("place-") + openModeName(mode) + ".md");
        writeAll(file, text);
        persistReadingPosition(utf8(file), place);
        app.openMode = mode;
        before = std::chrono::steady_clock::now();
        tabOpenPath(app, app.hwnd, utf8(file), true);
        std::cout << "  " << where << ": " << ms() << " ms\n";
        check(viewOf(app) == viewFor(mode), where + ": is in the " + name(viewFor(mode)) + " view");
        check(caretLine(app) == line, where + ": the caret is on the line E would put it on (" +
                                          std::to_string(caretLine(app)) + ", E " + std::to_string(line) + ")");
        check(app.pendingScrollRestore < 0.0f, where + ": the place is taken, not waited for");
        if (mode == OPEN_SOURCE) {
            check(std::fabs(app.editorScrollY - sourceScroll) < 1.0f, where + ": the source is scrolled as E scrolls it");
        } else {
            check(std::fabs(app.scrollY - place) < 1.0f, where + ": the page is at the place it was left at");
            paint(app);
            check(livePlaced(app) && std::fabs(app.scrollY - place) < (float)app.height,
                  where + ": and stays about there once drawn");
            picture(app, "opened-live-far-down");
        }
        check(!app.editorDirty && app.editorText == toWide(text), where + ": the file is unchanged");
        toReading(app);
    }

    // No place kept: the top
    app.openMode = OPEN_LIVE;
    const fs::path top = g_work / "place-none.md";
    writeAll(top, text);
    tabOpenPath(app, app.hwnd, utf8(top), true);
    check(viewOf(app) == View::Live && app.editorCursorPos == 0 && app.scrollY == 0.0f,
          "a file with no place kept opens at its top");
    toReading(app);
}

// --- Settings > Editor ------------------------------------------------------------

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
    app.settingsSection = 2;
    app.settingsScroll = 0.0f;
    app.settingsLangOpen = app.settingsKeysOpen = false;
    app.mouseX = app.mouseY = -100;
    return uiframe::draw(app, [&] { renderSettingsOverlay(app); });
}

// The interface in a language, as the dialog's own list picks it
bool speak(App& app, const char* id) {
    const int index = languageIndexById(id);
    if (index < 0) return false;
    app.languageSetting = app.currentLanguageIndex = index;
    updateOverlayFormats(app);
    return true;
}

void settingsRow(App& app) {
    using uiframe::inside;
    using uiframe::overlap;
    const int actions[3] = {SET_OPEN_READING, SET_OPEN_LIVE, SET_OPEN_SOURCE};
    const int modes[3] = {OPEN_READING, OPEN_LIVE, OPEN_SOURCE};
    toReading(app);
    app.openMode = OPEN_READING;
    persistOpenMode(app);

    loadLanguageOverrides();  // German, French, Italian and Russian
    const std::wstring english = tr(languageIndexById("en"), "settings.open_mode");
    for (const char* lang : {"en", "zh", "ja", "ko", "de", "fr", "it", "ru"}) {
        check(speak(app, lang), std::string(lang) + ": the interface speaks it");
        for (const char* key : {"settings.open_mode", "settings.open_mode.hint", "settings.open.reading",
                                "settings.open.live", "settings.open.source"}) {
            check(languageTranslatesKey(app.currentLanguageIndex, key), std::string(lang) + " has " + key);
        }
        check((std::wstring(tr(app, "settings.open_mode")) == english) == (std::string(lang) == "en"),
              std::string(lang) + ": the row is named in its own words");
        float wide[3] = {0, 0, 0};  // each segment's width in the wide dialog, where nothing is short of room
        for (int w : {1300, 760, 420}) {
            const std::string where = std::string(lang) + " at " + std::to_string(w);
            settingsFrame(app, w, 900);
            const D2D1_RECT_F panel = settingsPanelRect(app);
            const D2D1_RECT_F* seg[3] = {hitOf(app, actions[0]), hitOf(app, actions[1]), hitOf(app, actions[2])};
            check(seg[0] && seg[1] && seg[2], where + ": the row has its three segments");
            if (!seg[0] || !seg[1] || !seg[2] || app.settingsRows.empty()) continue;
            const App::SettingsRowBox& row = app.settingsRows.front();
            check(row.group == 0 && inside(*seg[0], row.row) && inside(*seg[2], row.row),
                  where + ": it is the first row of the page");
            check(inside(row.row, panel), where + ": inside the dialog");
            check(seg[0]->right <= seg[1]->left + 0.5f && seg[1]->right <= seg[2]->left + 0.5f,
                  where + ": Reading, Live, Source from left to right");
            check(!overlap(*seg[0], *seg[1]) && !overlap(*seg[1], *seg[2]), where + ": none over another");
            for (int s = 0; s < 3; s++) {
                check(inside(*seg[s], row.control), where + ": segment " + std::to_string(s) + " is in the row's control");
                if (w == 1300) wide[s] = uiframe::width(*seg[s]);
                check(uiframe::width(*seg[s]) >= dpi(app, 40.0f) && uiframe::about(uiframe::width(*seg[s]), wide[s]),
                      where + ": segment " + std::to_string(s) + " is as wide as its word, cut by nothing");
            }
            if (!row.stacked) {
                check(row.text.right <= row.control.left + 0.5f, where + ": the label ends before the control");
                check(w < 1300 || !row.titleTrimmed, where + ": the label is whole in a wide dialog");
            } else {
                check(row.control.top >= row.text.top + dpi(app, 16.0f), where + ": stacked, the control is under the label");
            }
        }
    }
    speak(app, "en");

    // A click on a segment picks the view, keeps it, and lifts the segment
    Frame last = settingsFrame(app, 1300, 900);
    for (int pick : {1, 2, 0}) {
        const std::string where = std::string("a click on ") + openModeName(modes[pick]);
        const D2D1_RECT_F* target = hitOf(app, actions[pick]);
        check(target != nullptr, where + ": the segment is there");
        if (!target) continue;
        const D2D1_RECT_F at = *target;
        click(app, at);
        check(app.openMode == modes[pick], where + ": picks it");
        check(loadSettings().openMode == modes[pick], where + ": and settings.ini has it at once");
        check(app.showSettings, where + ": the dialog stays open");
        const Frame now = settingsFrame(app, 1300, 900);
        check(now.differs(last, at), where + ": the segment is drawn picked");
        last = now;
    }

    // The choice is for the next file opened: nothing open changes
    tabOpenPath(app, app.hwnd, utf8(fresh("settings-open.md")), true);
    check(viewOf(app) == View::Reading, "Reading picked: a file opens in the reading view");
    settingsFrame(app, 1300, 900);
    if (const D2D1_RECT_F* live = hitOf(app, SET_OPEN_LIVE)) click(app, *live);
    check(viewOf(app) == View::Reading, "picking Live leaves the file being read as it is");
    app.showSettings = false;
    tabOpenPath(app, app.hwnd, utf8(fresh("settings-next.md")), true);
    check(viewOf(app) == View::Live, "and the next file opens in Live");
    toReading(app);

    // Pictures: the page in a wide and a narrow dialog, light and dark
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (const char* lang : {"en", "zh", "ja", "ko", "de", "fr", "it", "ru"}) {
            speak(app, lang);
            for (int w : {1300, 420}) {
                if (theme && std::string(lang) != "en") continue;  // dark once
                const Frame frame = settingsFrame(app, w, 900);
                const std::string file = std::string("settings-editor-") + lang + "-" + std::to_string(w) +
                                         (theme ? "-dark" : "-light");
                check(uiframe::save(app, frame, (g_frames / (file + ".png")).wstring()), "a picture of " + file);
            }
        }
    }
    applyTheme(app, 0);
    speak(app, "en");
    app.showSettings = false;
    app.width = 1300;
    app.height = 900;
    app.openMode = OPEN_READING;
    persistOpenMode(app);
}
}  // namespace

int runOpenModeTests(const char* fixtureRoot) {
    HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 2;
    g_fixtures = fixtureRoot;
    g_work = fs::current_path() / "open-mode";
    g_frames = fs::current_path() / "frames";
    std::error_code ignored;
    fs::remove_all(g_work, ignored);
    fs::create_directories(g_work);
    fs::create_directories(g_frames);

    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Open mode tests", WS_POPUP, 0, 0, 1300, 900, nullptr, nullptr, nullptr,
                               nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1300;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    applyTheme(app, 0);
    tabsInit(app);

    names(app);
    opensInTabs(app);
    pillAndSetting(app);
    tabsAsLeft(app);
    otherKinds(app);
    inPlace(app);
    linkToHeading(app);
    findResult(app);
    fromStartPage(app);
    reopenedAndSession(app);
    placeLeftAt(app);
    settingsRow(app);

    toReading(app);
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    OleUninitialize();
    std::cout << "Open mode: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
