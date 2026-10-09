#include "search_panel.h"
#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "input.h"
#include "overlays.h"
#include "print.h"
#include "render.h"
#include "search.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

// Search results panel (#246): Ctrl+Shift+F lists every match beside the
// page under its heading, with other open tabs and this folder's files
// below; rows jump to matches or open files in their own tabs.

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}
void key(App& app, unsigned vk, bool control = false, bool shift = false) {
    BYTE original[256], pressed[256];
    GetKeyboardState(original);
    memcpy(pressed, original, sizeof pressed);
    pressed[VK_CONTROL] = control ? 0x80 : 0;
    pressed[VK_SHIFT] = shift ? 0x80 : 0;
    SetKeyboardState(pressed);
    handleKeyDown(app, app.hwnd, vk);
    SetKeyboardState(original);
}
void type(App& app, const std::wstring& text) {
    for (wchar_t ch : text) handleCharInput(app, app.hwnd, ch);
}
void click(App& app, float x, float y) {
    LPARAM pt = MAKELPARAM((int)x, (int)y);
    handleMouseMove(app, app.hwnd, pt);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, pt);
    handleMouseUp(app, app.hwnd, 0, pt);
}
void paint(App& app) {
    app.searchAnimation = 1.0f;
    app.renderTarget->BeginDraw();
    renderSearchOverlay(app);
    renderSearchPanel(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "search bar and results panel paint");
}
// The other files arrive from a worker thread, as in the app: start a scan
// as the debounce timer would, then take results until the latest lands
// (an earlier scan's results are dropped on arrival)
bool scanOtherFiles(App& app) {
    startFolderSearchScan(app);
    for (int i = 0; i < 500 && app.folderSearchPending; i++) {
        MSG msg;
        while (PeekMessageW(&msg, app.hwnd, WM_APP_FOLDER_SEARCH, WM_APP_FOLDER_SEARCH, PM_REMOVE))
            completeFolderSearch(app, (void*)msg.lParam);
        if (app.folderSearchPending) Sleep(10);
    }
    return !app.folderSearchPending;
}
// Where a row sits on screen, scrolled into the list if needed
bool rowPoint(App& app, SearchPanelRowKind kind, int index, int file, float& x, float& y) {
    auto rows = searchPanelRows(app);
    D2D1_RECT_F list = searchPanelListRect(app);
    for (const auto& row : rows) {
        if (row.kind != kind || row.index != index || row.file != file) continue;
        float height = list.bottom - list.top;
        if (row.top < app.searchPanelScroll || row.top + row.height > app.searchPanelScroll + height) {
            app.searchPanelScroll = std::max(0.0f, row.top - height * 0.5f);
            paint(app);  // the panel clamps its scroll as it draws
        }
        x = (list.left + list.right) * 0.5f;
        y = list.top + row.top - app.searchPanelScroll + row.height * 0.5f;
        return true;
    }
    return false;
}
const SearchPanelRow* findRow(const std::vector<SearchPanelRow>& rows, SearchPanelRowKind kind,
                              int index, int file = -1) {
    for (const auto& row : rows)
        if (row.kind == kind && row.index == index && row.file == file) return &row;
    return nullptr;
}
int fileIndex(const App& app, const std::wstring& name) {
    for (size_t i = 0; i < app.folderResults.size(); i++)
        if (app.folderResults[i].fileName == name) return (int)i;
    return -1;
}
bool endsWith(const std::string& text, const std::string& tail) {
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}
}

int runSearchPanelTests() {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = fs::path(exe).parent_path();
    const auto settings = read(dir / "settings.ini");
    if (!fs::equivalent(dir, fs::current_path()) ||
        (settings.rfind("; Isolated search results panel test configuration", 0) != 0)) {
        std::cerr << "Run through CTest's isolated search results panel wrapper\n";
        return 2;
    }
    check(fs::equivalent(tintaConfigDir(), dir), "all persistence stays in the test directory");

    // The mixed fixture folder, copied so reloads and new tabs stay here
    const fs::path docs = dir / "docs";
    std::error_code ec;
    fs::remove_all(docs, ec);
    fs::copy(fs::path(TINTA_SEARCH_PANEL_FIXTURE_DIR), docs, fs::copy_options::recursive, ec);
    check(!ec && fs::exists(docs / "current.md") && fs::exists(docs / "elsewhere" / "notes.md"),
          "fixture folder copies");
    const std::string current = toUtf8((docs / "current.md").wstring());
    const std::string notes = toUtf8((docs / "elsewhere" / "notes.md").wstring());

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TintaSearchPanelTest";
    RegisterClassW(&wc);
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Search panel native tests", WS_POPUP,
        -1200, 100, 1050, 900, nullptr, nullptr, wc.hInstance, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        std::cerr << "Cannot initialize native search panel test window\n";
        return 1;
    }
    app.width = 1050;
    app.height = 900;
    applyTheme(app, 0);
    updateTextFormats(app);
    check(openDocumentInViewer(app, toWide(current)), "mixed document opens");
    tabsSeedSession(app, {current, notes});
    check(app.tabs.size() == 2 && app.activeTab == 0, "the document and a tab from another folder");
    ensureLayoutComplete(app);

    // Ctrl+Shift+F opens the bar and the panel; typing searches
    key(app, 'F', true, true);
    check(app.showSearch && app.searchActive && app.showSearchPanel,
          "Ctrl+Shift+F opens the search bar and the results panel");
    paint(app);
    auto rows = searchPanelRows(app);
    check(rows.size() == 1 && rows[0].kind == SPR_NOTE, "an empty query asks for one");
    type(app, L"needle");
    check(app.searchMatches.size() == 50, "fifty matches in the rendered document");
    check(app.folderSearchPending, "other files are on their way");

    // The document's matches under the headings they fall in
    rows = searchPanelRows(app);
    check(!rows.empty() && rows[0].kind == SPR_DOCUMENT, "the document leads the list");
    std::vector<std::wstring> sections;
    int lastMatch = -1, matchRows = 0;
    bool ordered = true, placed = true;
    for (size_t i = 0; i < rows.size(); i++) {
        if (rows[i].kind == SPR_SECTION && rows[i].index >= 0)
            sections.push_back(app.headings[rows[i].index].text);
        if (rows[i].kind != SPR_MATCH) continue;
        matchRows++;
        ordered = ordered && rows[i].index > lastMatch;
        lastMatch = rows[i].index;
        // Its section is the last heading above it
        int section = -1;
        for (size_t j = i; j-- > 0;)
            if (rows[j].kind == SPR_SECTION) { section = rows[j].index; break; }
        const auto& m = app.searchMatches[rows[i].index];
        if (section >= 0) {
            placed = placed && app.headings[section].y <= m.highlightRect.top + 0.5f;
            if (section + 1 < (int)app.headings.size())
                placed = placed && app.headings[section + 1].y > m.highlightRect.top + 0.5f;
        }
    }
    check(matchRows == 50 && ordered, "every match gets one row, in document order");
    check(placed, "each match sits under the heading it falls in");
    const std::vector<std::wstring> expected = {L"Needle hunting (#246)", L"In a table",
        L"Quotes, lists and code", L"Mixed scripts", L"A long needle list", L"Closing needle"};
    check(sections == expected, "sections follow the headings that hold matches");

    // The other files: tabs first, then the folder by name, without the
    // document itself or files with no match
    check(scanOtherFiles(app), "the scan of other files completes");
    const int tab = fileIndex(app, L"notes.md"), alpha = fileIndex(app, L"alpha.md"),
              bravo = fileIndex(app, L"bravo.md"), sibling = fileIndex(app, L"sibling.md");
    check(app.folderResults.size() == 4 && tab == 0 && alpha == 1 && bravo == 2 && sibling == 3,
          "notes.md as a tab, then alpha, bravo and sibling");
    check(fileIndex(app, L"quiet.md") < 0 && fileIndex(app, L"current.md") < 0,
          "the document and quiet.md are not listed");
    if (app.folderResults.size() == 4 && tab == 0 && bravo == 2 && sibling == 3) {
        check(app.folderResults[0].openTab && !app.folderResults[1].openTab, "tabs and folder files are told apart");
        const auto& b = app.folderResults[bravo];
        check(b.totalMatches == 5 && b.matches.size() == 3 && b.matches[0].line == 3 &&
                  b.matches[1].line == 5 && b.matches[2].line == 7,
              "bravo.md lists its first three matches by line");
        const auto& s = app.folderResults[sibling];
        check(s.totalMatches == 3 && s.matches.size() == 3 && s.matches[1].line == 5,
              "sibling.md lists its matches by line");
        rows = searchPanelRows(app);
        check(findRow(rows, SPR_CAPTION, 0) && findRow(rows, SPR_CAPTION, 1),
              "captions name the open tabs and this folder");
        check(findRow(rows, SPR_MORE, -1, bravo) && !findRow(rows, SPR_MORE, -1, sibling),
              "+N more only where matches were left out");
    }

    // Layout: the panel docks in the Contents slot and the bar keeps clear
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float scale : {1.0f, 1.5f}) {
            app.contentScale = scale;
            updateTextFormats(app);
            for (int width : {650, 1050}) {
                app.width = (int)(width * scale);
                app.height = (int)(900 * scale);
                app.tocAnimation = 1.0f;
                paint(app);
                float panelW = tocPanelWidth(app);
                check(panelW > 0.0f, "the results panel takes the Contents slot");
                check(std::fabs(documentViewportWidth(app) - ((float)app.width - panelW)) < 0.5f,
                      "the page narrows by the panel's width");
                D2D1_RECT_F bar = searchBarRect(app);
                check(bar.left >= documentViewportX(app) - 0.5f &&
                          bar.right <= documentViewportX(app) + documentViewportWidth(app) + 0.5f,
                      "the search bar stays over the page, clear of the panel");
                D2D1_RECT_F button = searchResultsButtonRect(app);
                check(button.left > bar.left && button.right < bar.right,
                      "the results button sits inside the bar");
                D2D1_RECT_F list = searchPanelListRect(app);
                check(list.left >= (float)app.width - panelW - 0.5f && list.right <= (float)app.width + 0.5f &&
                          list.bottom > list.top,
                      "the list lies inside the panel");
            }
        }
    }
    app.contentScale = 1.0f;
    app.width = 1050;
    app.height = 900;
    applyTheme(app, 0);
    updateTextFormats(app);
    ensureLayoutComplete(app);
    paint(app);

    // Enter walks the matches and the list follows the current one
    app.searchPanelScroll = 0.0f;
    focusSearchInput(app, false);
    for (int i = 0; i < 40; i++) key(app, VK_RETURN);
    check(app.searchCurrentIndex == 40, "Enter steps to the forty-first match");
    paint(app);
    rows = searchPanelRows(app);
    D2D1_RECT_F list = searchPanelListRect(app);
    const SearchPanelRow* row = findRow(rows, SPR_MATCH, 40);
    check(row && app.searchPanelScroll > 0.0f && row->top >= app.searchPanelScroll &&
              row->top + row->height <= app.searchPanelScroll + (list.bottom - list.top),
          "the current match's row scrolls into view");

    // A row jumps to its match; a heading to its first match
    float x = 0, y = 0;
    check(rowPoint(app, SPR_MATCH, 7, -1, x, y), "the eighth match has a row");
    app.scrollY = app.targetScrollY = 0;
    click(app, x, y);
    check(app.searchCurrentIndex == 7, "a match row selects its match");
    const auto& seventh = app.searchMatches[7];
    check(seventh.highlightRect.top - app.scrollY >= chromeTopHeight(app) &&
              seventh.highlightRect.bottom - app.scrollY <= (float)app.height,
          "the page scrolls the selected match into view");
    int tableSection = -1;
    for (size_t i = 0; i < app.headings.size(); i++)
        if (app.headings[i].text == L"In a table") tableSection = (int)i;
    check(rowPoint(app, SPR_SECTION, tableSection, -1, x, y), "the table section has a row");
    click(app, x, y);
    int firstTableMatch = -1;
    rows = searchPanelRows(app);
    for (size_t i = 0; i < rows.size(); i++)
        if (rows[i].kind == SPR_SECTION && rows[i].index == tableSection && i + 1 < rows.size())
            firstTableMatch = rows[i + 1].index;
    check(firstTableMatch > 0 && app.searchCurrentIndex == firstTableMatch,
          "a heading row selects its first match");

    // The wheel scrolls the list, within its length
    app.searchPanelScroll = 0.0f;
    D2D1_RECT_F panelList = searchPanelListRect(app);
    handleMouseMove(app, app.hwnd, MAKELPARAM((int)((panelList.left + panelList.right) / 2), (int)(panelList.top + 40)));
    for (int i = 0; i < 60; i++) handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), 0);
    rows = searchPanelRows(app);
    float maxScroll = rows.back().top + rows.back().height - (panelList.bottom - panelList.top);
    check(app.searchPanelScroll > 0.0f && std::fabs(app.searchPanelScroll - maxScroll) < 0.5f,
          "the wheel scrolls the list to its end and no further");
    check(app.scrollY == app.targetScrollY, "the page stays put while the list scrolls");

    // Scope rows switch other tabs and this folder, and remember it
    const float scopeX = (panelList.left + panelList.right) / 2;
    const float scopeTabsY = chromeTopHeight(app) + dpi(app, 52.0f);
    const float scopeFolderY = chromeTopHeight(app) + dpi(app, 76.0f);
    click(app, scopeX, scopeTabsY);
    check(!app.tabSearchEnabled && !loadSettings().tabSearchEnabled, "Open tabs switches off and is saved");
    check(scanOtherFiles(app) && fileIndex(app, L"notes.md") < 0 && fileIndex(app, L"alpha.md") >= 0,
          "without open tabs only the folder is listed");
    click(app, scopeX, scopeFolderY);
    check(!app.folderSearchEnabled && !loadSettings().folderSearchEnabled, "This folder switches off and is saved");
    startFolderSearchScan(app);
    check(!app.folderSearchPending && app.folderResults.empty(), "with both off nothing else is searched");
    click(app, scopeX, scopeTabsY);
    click(app, scopeX, scopeFolderY);
    check(app.tabSearchEnabled && app.folderSearchEnabled && loadSettings().tabSearchEnabled &&
              loadSettings().folderSearchEnabled,
          "both scopes switch back on");
    check(scanOtherFiles(app) && app.folderResults.size() == 4, "and the other files return");

    // A file's match opens it in a tab of its own, at that match
    const int siblingFile = fileIndex(app, L"sibling.md");
    check(rowPoint(app, SPR_FILE_MATCH, 1, siblingFile, x, y), "sibling.md's line 5 has a row");
    click(app, x, y);
    check(endsWith(app.currentFile, "sibling.md") && app.tabs.size() == 3 && app.activeTab == 2,
          "the sibling opens in a new tab beside the document");
    check(app.showSearch && app.showSearchPanel && app.searchQuery == L"needle",
          "the search and its panel carry over");
    check(app.searchMatches.size() == 3 && app.searchCurrentIndex == 1,
          "the new tab lands on the match from line 5");
    check(scanOtherFiles(app) && fileIndex(app, L"current.md") >= 0 &&
              app.folderResults[fileIndex(app, L"current.md")].openTab,
          "the document now counts among the other tabs");
    check(std::count_if(app.folderResults.begin(), app.folderResults.end(),
                        [](const App::FolderFileResult& f) { return f.fileName == L"current.md"; }) == 1,
          "a file open in a tab is not listed again with the folder");

    // An open tab's row switches to that tab rather than opening another
    const int notesFile = fileIndex(app, L"notes.md");
    check(rowPoint(app, SPR_FILE, -1, notesFile, x, y), "notes.md has a row");
    click(app, x, y);
    check(endsWith(app.currentFile, "notes.md") && app.tabs.size() == 3 && app.activeTab == 1,
          "an open tab is switched to, not opened twice");
    check(app.searchMatches.size() == 1 && app.searchCurrentIndex == 0, "and lands on its match");

    // A reload of the open file searches the new text
    tabActivate(app, app.hwnd, 0);
    check(endsWith(app.currentFile, "current.md") && app.searchMatches.size() == 50,
          "switching tabs searches the document again");
    {
        std::ofstream out(docs / "current.md", std::ios::binary | std::ios::app);
        out << "\r\nOne more needle after a reload.\r\n";
    }
    FILETIME later{};
    HANDLE h = CreateFileW((docs / "current.md").c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        ULARGE_INTEGER t;
        t.LowPart = app.lastFileWriteTime.dwLowDateTime;
        t.HighPart = app.lastFileWriteTime.dwHighDateTime;
        t.QuadPart += 10000000ULL * 5;
        later.dwLowDateTime = t.LowPart;
        later.dwHighDateTime = t.HighPart;
        SetFileTime(h, nullptr, nullptr, &later);
        CloseHandle(h);
    }
    handleFileWatchTimer(app, app.hwnd);
    check(app.searchMatches.size() == 51, "a reloaded document is searched again");

    // The results button closes and reopens the panel; the bar re-centers
    // over the page in between
    D2D1_RECT_F button = searchResultsButtonRect(app);
    click(app, (button.left + button.right) / 2, (button.top + button.bottom) / 2);
    check(app.showSearch && !app.showSearchPanel, "the bar's button closes the panel");
    button = searchResultsButtonRect(app);
    click(app, (button.left + button.right) / 2, (button.top + button.bottom) / 2);
    check(app.showSearchPanel, "and opens it again");

    // Contents gives the slot over and gets it back with Esc
    key(app, VK_ESCAPE);
    check(!app.showSearch && !app.showSearchPanel && !app.showToc, "Esc closes the bar and the panel");
    app.showToc = true;
    app.tocAnimation = 1.0f;
    key(app, 'F', true, true);
    check(app.showSearchPanel && !app.showToc && app.tocAnimation == 1.0f,
          "the panel takes Contents' place without a slide");
    key(app, VK_ESCAPE);
    check(app.showToc && !app.showSearchPanel, "Contents returns when the panel closes");

    // Print layout leaves the panel's width out of the page
    app.showToc = false;
    key(app, 'F', true, true);
    check(app.showSearchPanel, "panel open before printing");
    openPrintPreview(app, app.hwnd);
    check(app.showPrintPreview && !app.showSearchPanel, "the page is laid out without the panel");
    closePrintPreview(app, app.hwnd);
    check(!app.showPrintPreview && app.showSearchPanel, "the panel returns after print preview");

    // The editor has no side panels: entering it closes the panel, and
    // Ctrl+Shift+F there does nothing
    check(app.showSearchPanel, "panel open before editing");
    enterEditMode(app);
    check(app.editMode && !app.showSearchPanel, "entering the editor closes the panel");
    if (app.showSearch) closeSearchInput(app);
    key(app, 'F', true, true);
    check(!app.showSearch && !app.showSearchPanel, "Ctrl+Shift+F leaves the editor alone");

    DestroyWindow(app.hwnd);
    if (failures) std::cerr << failures << " search results panel check(s) failed\n";
    else std::cout << "Search results panel tests passed\n";
    return failures ? 1 : 0;
}
