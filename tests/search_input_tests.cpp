#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "overlays.h"
#include "render.h"
#include "search.h"
#include "settings.h"
#include "tableedit.h"
#include "tabs.h"
#include "utils.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void key(App& app, unsigned vk, unsigned ch = 0, bool control = false, bool shift = false) {
    BYTE original[256], pressed[256];
    GetKeyboardState(original);
    memcpy(pressed, original, sizeof pressed);
    pressed[VK_CONTROL] = control ? 0x80 : 0;
    pressed[VK_SHIFT] = shift ? 0x80 : 0;
    SetKeyboardState(pressed);
    if (!handleKeyDown(app, app.hwnd, vk) && ch) handleCharInput(app, app.hwnd, ch);
    SetKeyboardState(original);
}
void type(App& app, const std::wstring& text) {
    for (wchar_t ch : text) handleCharInput(app, app.hwnd, ch);
}
void click(App& app, float x, float y) {
    LPARAM pt = MAKELPARAM(static_cast<int>(x), static_cast<int>(y));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, pt);
    handleMouseUp(app, app.hwnd, 0, pt);
}
void paint(App& app) {
    app.searchAnimation = 1;
    app.cursorBlinkOn = true;
    app.renderTarget->BeginDraw();
    renderEditor(app, editorPaneWidth(app));
    renderSearchOverlay(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "editor and search paint");
}
void query(App& app, const std::wstring& text) {
    focusSearchInput(app, false);
    key(app, 'A', 1, true);
    key(app, VK_BACK, 8);
    type(app, text);
}
void fieldEditing(App& app) {
    const auto source = app.editorText;
    key(app, 'H', 8, true);
    check(app.searchActive && app.searchReplaceMode,
          "Ctrl+H focuses Find and consumes only the opening keystroke");
    type(app, L"abc"); key(app, VK_LEFT); type(app, L"X");
    check(app.searchQuery == L"abXc", "Left moves inside Find before insertion");
    key(app, VK_HOME); key(app, VK_DELETE);
    key(app, VK_END); key(app, VK_BACK, 8);
    check(app.searchQuery == L"bX", "Home/End and Delete/Backspace edit at the caret");
    key(app, VK_HOME); key(app, VK_RIGHT, 0, false, true); type(app, L"Z");
    check(app.searchQuery == L"ZX", "Shift selection is replaced by typing");
    key(app, VK_TAB, 9); type(app, L"abc"); key(app, VK_LEFT); type(app, L"X");
    check(app.replaceText == L"abXc", "Replace has independent caret editing");
    key(app, VK_TAB, 9, false, true);
    check(!app.replaceFieldActive, "Shift+Tab returns to Find");
    check(app.editorText == source, "field edits cannot leak into source");

    query(app, L"A\xd83d\xde00" L"e\x0301\x4e2d");
    key(app, VK_LEFT); key(app, VK_BACK, 8);
    check(app.searchQuery == L"A\xd83d\xde00\x4e2d", "Backspace deletes a combining cluster");
    key(app, VK_BACK, 8);
    check(app.searchQuery == L"A\x4e2d", "Backspace deletes an entire surrogate pair");
    key(app, VK_DELETE);
    check(app.searchQuery == L"A", "Delete handles a Chinese character");
    query(app, L"one two"); key(app, VK_HOME); key(app, VK_RIGHT, 0, true);
    check(app.searchFields[0].caret == 4, "Ctrl+Right moves to the next word");
    key(app, VK_BACK, 8, true);
    check(app.searchQuery == L"two", "Ctrl+Backspace removes the preceding word");

    // Preserve the user's clipboard, including non-text formats.
    Microsoft::WRL::ComPtr<IDataObject> clipboard;
    HRESULT saved = OleGetClipboard(clipboard.GetAddressOf());
    check(SUCCEEDED(saved), "clipboard can be preserved for editing tests");
    if (SUCCEEDED(saved)) {
        query(app, L"copy me"); key(app, 'A', 1, true); key(app, 'C', 3, true);
        check(clipboardLine(app.hwnd) == L"copy me", "copy puts the selected field text on the clipboard");
        key(app, VK_END); key(app, 'V', 22, true);
        check(app.searchQuery == L"copy mecopy me", "paste inserts at the search caret");
        key(app, 'A', 1, true); key(app, 'X', 24, true);
        check(app.searchQuery.empty(), "cut removes the selected search text");
        key(app, VK_TAB); key(app, 'A', 1, true); key(app, 'V', 22, true);
        check(app.replaceText == L"copy mecopy me", "paste replaces the replacement selection");
        HRESULT restored = E_FAIL;
        for (int attempt = 0; attempt < 10 && FAILED(restored = OleSetClipboard(clipboard.Get())); ++attempt) Sleep(5);
        if (FAILED(restored)) std::cerr << "clipboard restore HRESULT=" << std::hex << restored << std::dec << '\n';
        check(SUCCEEDED(restored), "clipboard restored");
    }

    for (int theme : {0, 5}) for (bool wrap : {false, true})
    for (float scale : {1.0f, 1.5f})
    for (bool preview : {false, true}) for (int width : {650, 1050}) {
        app.editorWordWrap = wrap; app.contentScale = scale; applyTheme(app, theme);
        app.editorShowPreview = preview; app.width = width;
        query(app, L""); paint(app);
        D2D1_POINT_2F caret{};
        check(searchInputCaretPoint(app, caret) && !sourceEditorHasFocus(app) &&
              caret.x == app.searchFields[0].textRect.left, "empty field owns a visible caret position");
        type(app, std::wstring(150, L'W')); paint(app);
        check(app.searchFields[0].scrollX > 0 && searchInputCaretPoint(app, caret) &&
              caret.x <= app.searchFields[0].textRect.right, "long query keeps its caret inside the field");
        key(app, VK_HOME); paint(app);
        check(app.searchFields[0].scrollX == 0, "Home scrolls a long field back to its start");
        check(app.searchFields[1].textRect.right-app.searchFields[1].textRect.left > 60,
              "narrow/full editor preserves replacement input width");
    }
    app.width = 1050; app.editorShowPreview = true; app.editorWordWrap = false;
    app.contentScale = 1; applyTheme(app, 0);
    query(app, L"abcd"); paint(app);
    auto rect = app.searchFields[0].textRect;
    click(app, rect.left, rect.top+2);
    type(app, L"X");
    check(app.searchQuery == L"Xabcd", "mouse places Find caret at the start");
    paint(app); rect = app.searchFields[0].textRect;
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM(static_cast<int>(rect.left), static_cast<int>(rect.top+2)));
    handleMouseMove(app, app.hwnd, MAKELPARAM(static_cast<int>(rect.right), static_cast<int>(rect.top+2)));
    handleMouseUp(app, app.hwnd, 0, 0);
    type(app, L"selected");
    check(app.searchQuery == L"selected" && app.searchSelectingField == -1 && GetCapture() != app.hwnd,
          "mouse drag selects text and releases capture");
    D2D1_POINT_2F imePoint{};
    check(searchInputCaretPoint(app, imePoint) && imePoint.y == app.searchFields[0].textRect.bottom,
          "IME candidate anchor follows the active Find field");
    key(app, VK_ESCAPE, 27);
    check(!app.showSearch && sourceEditorHasFocus(app), "Escape returns typing to source");
}
void focusTransfers(App& app) {
    auto reset = [&]() {
        closeSearchInput(app); tableEditCancel(app);
        app.lastClickTime = {}; app.clickCount = 0;
        app.editMode = false; // Explicitly reload the fixture, not resume a live preview.
        enterEditMode(app); app.editorShowPreview = true; app.editorScrollY = app.scrollY = 0;
        editorReparse(app); ensureLayoutComplete(app);
    };
    auto openCell = [&]() {
        ensureLayoutComplete(app);
        for (const auto& cell : app.tableCellRects) if (cell.row == 1 && cell.col == 0) {
            auto rect = cell.rect;
            click(app, documentViewportX(app)+(rect.left+rect.right)/2-app.scrollX,
                  (rect.top+rect.bottom)/2-app.scrollY);
            key(app,VK_END,0,true); // Mouse placement now honors the clicked position (#236).
            return app.tableEditActive;
        }
        return false;
    };
    for (bool tab : {false, true}) {
        reset(); check(openCell(), "rendered body cell opens");
        type(app, L"Q");
        if (tab) { key(app, VK_TAB, 9); type(app, L"R"); }
        click(app, 180, chromeTopHeight(app)+16);
        auto source = app.editorText; size_t pos = app.editorCursorPos;
        type(app, L"X"); source.insert(pos, L"X");
        check(app.editorText == source && !app.tableEditActive && sourceEditorHasFocus(app),
              "cell/source transfer commits before hit testing, with and without Tab");
        check(app.editorText.find(L"alphaQ") != std::wstring::npos &&
              (!tab || app.editorText.find(L"betaR") != std::wstring::npos), "pending cells survive source focus transfer");
        key(app, 'Z', 26, true);
        check(app.editorText.find(L"alphaQ") != std::wstring::npos &&
              app.editorText.size()+1 == source.size(), "Undo after transfer removes source typing independently");
    }
    reset(); check(openCell(), "cell opens before search shortcut"); type(app, L"Q");
    D2D1_POINT_2F imePoint{};
    check(tableEditCaretPoint(app, imePoint) && imePoint.x > documentViewportX(app),
          "table IME anchor stays in the preview cell");
    key(app, 'H', 8, true); type(app, L"alpha");
    check(app.searchQuery == L"alpha" && !app.tableEditActive && app.editorText.find(L"alphaQ") != std::wstring::npos,
          "Ctrl+H commits the cell and routes immediate typing to Find");
    paint(app);
    click(app, 180, chromeTopHeight(app)+185);
    auto source = app.editorText; size_t pos = app.editorCursorPos; type(app, L"X"); source.insert(pos,L"X");
    check(app.editorText == source && !app.searchActive && app.showSearch,
          "source click releases Find focus while leaving it visible");
    key(app, 'F', 6, true);
    check(app.searchActive && !app.searchReplaceMode, "Ctrl+F refocuses an already visible bar");
    key(app, VK_ESCAPE);
    reset(); check(openCell(), "cell opens for Shift+Tab and Escape");
    key(app, VK_TAB); key(app, VK_TAB, 0, false, true);
    check(app.tableEditActive && app.tableEditRow == 1 && app.tableEditCol == 0, "table Tab/Shift+Tab remain cell navigation");
    type(app, L"cancel"); key(app, VK_ESCAPE);
    check(app.editorText.find(L"alphacancel") == std::wstring::npos, "Escape discards an uncommitted cell edit");

    // Enter still replaces literal matches; regex remains out of scope.
    key(app, 'H', 8, true); query(app, L"beta"); key(app, VK_TAB); key(app, 'A', 1, true);
    type(app, L"replaced"); key(app, VK_RETURN, 13);
    check(app.editorText.find(L"replaced") != std::wstring::npos, "Enter replaces the current match");
    app.currentFile = std::filesystem::absolute("find-table-focus-save.md").string();
    key(app, 'S', 19, true);
    std::ifstream saved(app.currentFile, std::ios::binary);
    std::string savedText{std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()};
    check(!app.editorDirty && savedText == toUtf8(app.editorText) && app.searchActive,
          "Ctrl+S saves actual source while Find/Replace retains focus");
    closeSearchInput(app); app.editMode = false; app.layoutDirty = true;
    key(app, 'F', 6, true); type(app, L"abc"); key(app,VK_LEFT); type(app,L"X");
    check(app.searchQuery == L"abXc", "viewer Find shares caret editing");
    key(app,VK_ESCAPE);
}
void searchResultsPanel(App& app) {
    // The dock is a viewer-only feature (side panels never open in edit
    // mode). Load the mixed fixture so rows span headings, table, list,
    // quote, code, emphasis and links, and the list overflows the dock.
    closeSearchInput(app);
    app.currentFile = TINTA_SEARCHRESULT_FIXTURE;
    enterEditMode(app); editorReparse(app); ensureLayoutComplete(app);
    app.editMode = false;
    app.contentScale = 1;
    app.searchResultsAnimation = 1;
    app.searchResultsScroll = 0;
    app.showToc = false;

    // Claim path keys off the mouse's last position, so a synthetic press
    // must plant the cursor before down+up, exactly as mousemove would.
    auto press = [&](float x, float y) {
        app.mouseX = static_cast<int>(x);
        app.mouseY = static_cast<int>(y);
        handleMouseDown(app, app.hwnd, MK_LBUTTON,
                        MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
        handleMouseUp(app, app.hwnd, 0,
                      MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
    };
    // renderSearchResultsPanel slides the dock in one 0.15 step per paint
    // and publishes the close/hit geometry; run it to settled final state.
    auto settle = [&]() {
        while (app.searchResultsAnimation < 1.0f) {
            app.renderTarget->BeginDraw();
            renderSearchResultsPanel(app);
            if (FAILED(app.renderTarget->EndDraw())) break;
        }
        app.searchResultsAnimation = 1.0f;
    };

    // Ctrl+F alone never raises the results dock.
    key(app, 'F', 6, true);
    type(app, L"needle");
    check(app.searchActive && !app.showSearchResults && !app.searchMatches.empty(),
          "plain Ctrl+F search keeps the results panel closed");
    check(app.searchResultItems.size() == app.searchMatches.size(),
          "result rows mirror searchMatches 1:1");
    const float readerWidth = documentViewportWidth(app);

    // Ctrl+Shift+F opens the dock, drops the Contents panel, narrows the
    // reading column and rebuilds rows for the current query.
    key(app, 'F', 6, true, true);
    check(app.showSearchResults && !app.showToc,
          "Ctrl+Shift+F opens the results dock and clears the Contents dock");
    check(documentViewportWidth(app) == readerWidth - searchResultsPanelWidth(app),
          "results dock narrows the reading column by panel width");
    settle();

    const auto list = searchResultsListRect(app);
    const float rowH = dpi(app, 26.0f);
    check(list.bottom - list.top >= rowH * 20,
          "overflowing fixture gives the dock a long list");
    check(searchResultsMaxScroll(app) > 0.0f, "long list exposes wheel scrolling");

    // Wheel over the list scrolls it down and clamps on the way back up.
    app.searchResultsScroll = 10.0f;
    app.mouseX = (int)((list.left + list.right) * 0.5f);
    app.mouseY = (int)((list.top + list.bottom) * 0.5f);
    handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), 0);
    check(app.searchResultsScroll > 10.0f, "wheel scrolls the results list");
    app.searchResultsScroll = 0;
    handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)+WHEEL_DELTA), 0);
    check(app.searchResultsScroll == 0, "wheel clamps the list at its top");

    // Clicking a row jumps to that match and keeps the panel open. The
    // up-handler hit-tests the click's own coordinates (#114 pattern), so
    // the render-time hover must not be trusted for navigation.
    app.searchResultsScroll = 0;
    app.searchCurrentIndex = 0;
    const float rowY = list.top + rowH * 1.0f + rowH * 0.5f;
    const float rowX = (list.left + list.right) * 0.5f;
    check(searchPanelHitAt(app, rowX, rowY).type == SearchPanelHitType::Document &&
          searchPanelHitAt(app, rowX, rowY).index == 1,
          "row hit-testing agrees with the click coordinates");
    press(rowX, rowY);
    check(app.searchCurrentIndex == 1 && app.showSearchResults && app.searchActive,
          "clicking a result row navigates to it, keeps the dock and Find focus");

    // The header close cross dismisses only the dock; Find stays focused.
    const auto closeRect = app.searchResultsCloseRect;
    if (closeRect.right > closeRect.left && closeRect.bottom > closeRect.top) {
        press((closeRect.left + closeRect.right) * 0.5f,
              (closeRect.top + closeRect.bottom) * 0.5f);
        check(!app.showSearchResults && app.showSearch && app.searchActive,
              "close cross dismisses the dock while Find keeps focus");
    } else {
        check(false, "close cross geometry is published by the render pass");
    }

    // A click outside the dock dismisses it similarly; the bar remains up.
    key(app, 'F', 6, true, true);
    settle();
    press(40.0f, (list.top + list.bottom) * 0.5f);
    check(!app.showSearchResults && app.showSearch,
          "click outside the dock dismisses it and keeps the search bar");

    // A second Ctrl+Shift+F toggles the dock back off mid-search.
    key(app, 'F', 6, true, true);
    check(app.showSearchResults && app.searchActive,
          "Ctrl+Shift+F reopens the dock and Find keeps focus");
    key(app, 'F', 6, true, true);
    check(!app.showSearchResults && app.showSearch,
          "a second Ctrl+Shift+F closes the dock while the search bar stays");

    // Escape closes search and unloads the dock with its rows.
    key(app, 'F', 6, true, true);
    key(app, VK_ESCAPE, 27);
    check(!app.showSearch && !app.showSearchResults && app.searchResultItems.empty(),
          "Escape closes search and unloads the results dock");

    // While the dock is up, TAB cannot raise the Contents panel: the two
    // share the right dock (#156 exclusivity).
    key(app, 'F', 6, true); type(app, L"needle"); key(app, 'F', 6, true, true);
    key(app, VK_TAB, 9);
    check(!app.showToc, "Contents dock stays blocked while results are shown");
    key(app, VK_ESCAPE, 27);
}

// Extra-file search lives behind two checkboxes in the Ctrl+Shift+F panel
// (open documents / current folder). Both default off, so the dock lists the
// open document only and a click can never swap it for a sibling file
// (#foldersearch regression).
void folderSearchToggles(App& app) {
    closeSearchInput(app);
    app.currentFile = TINTA_FOLDERSEARCH_FIXTURE;
    enterEditMode(app); editorReparse(app); ensureLayoutComplete(app);
    app.editMode = false;
    app.contentScale = 1;
    app.searchResultsAnimation = 1;
    app.searchResultsScroll = 0;
    app.showToc = false;
    app.folderSearchEnabled = false;
    app.searchAllOpenFiles = false;
    app.tabs.clear();
    app.activeTab = 0;
    app.folderResults.clear();

    auto press = [&](float x, float y) {
        app.mouseX = static_cast<int>(x);
        app.mouseY = static_cast<int>(y);
        handleMouseDown(app, app.hwnd, MK_LBUTTON,
                        MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
        handleMouseUp(app, app.hwnd, 0,
                      MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
    };
    auto settle = [&]() {
        for (int i = 0; i < 20 && app.searchResultsAnimation < 1.0f; i++) {
            app.renderTarget->BeginDraw();
            renderSearchResultsPanel(app);
            if (FAILED(app.renderTarget->EndDraw())) break;
        }
        app.searchResultsAnimation = 1.0f;
        app.renderTarget->BeginDraw();
        renderSearchResultsPanel(app);
        app.renderTarget->EndDraw();
    };
    // The worker posts WM_APP_FOLDER_SEARCH; drain it here (no app pump).
    auto drainScan = [&]() {
        MSG msg;
        DWORD deadline = GetTickCount() + 3000;
        while (GetTickCount() < deadline) {
            if (PeekMessageW(&msg, app.hwnd, WM_APP_FOLDER_SEARCH,
                             WM_APP_FOLDER_SEARCH, PM_REMOVE)) {
                completeFolderSearch(app, (void*)msg.lParam);
                return true;
            }
            Sleep(5);
        }
        return false;
    };
    auto hasResult = [&](const wchar_t* name) {
        for (const auto& f : app.folderResults)
            if (f.fileName == name) return true;
        return false;
    };

    key(app, 'F', 6, true); type(app, L"needle");
    key(app, 'F', 6, true, true);
    settle();

    check(!app.folderSearchEnabled && !app.searchAllOpenFiles,
          "extra-file search defaults off");
    check(!app.searchResultItems.empty() && app.folderResults.empty(),
          "both toggles off keeps the dock to the open document");
    startFolderSearchScan(app);
    check(app.folderResults.empty(),
          "no scan runs while both extra-file sources are off");

    // Folder checkbox: scan the open document's folder for siblings.
    check(app.searchFolderToggleRect.right > app.searchFolderToggleRect.left,
          "render publishes the folder checkbox rect");
    press((app.searchFolderToggleRect.left + app.searchFolderToggleRect.right) * 0.5f,
          (app.searchFolderToggleRect.top + app.searchFolderToggleRect.bottom) * 0.5f);
    check(app.folderSearchEnabled, "clicking the checkbox enables the folder scan");
    check(app.showSearch && !app.editMode && !app.searchQuery.empty(),
          "scan preconditions hold after enabling the folder scan");
    int generationBefore = app.folderSearchGeneration;
    startFolderSearchScan(app);
    check(app.folderSearchGeneration == generationBefore + 1, "folder scan started");
    check(drainScan(), "folder scan completes");
    check(hasResult(L"sibling.md") && !hasResult(L"quiet.md") &&
          !hasResult(L"current.md"),
          "folder scan lists matching siblings, skips the open document");

    // Clicking a file row opens that file and drops the extra results.
    settle();
    float fileY = -1.0f;
    int fileIndex = -1;
    {
        const auto list = searchResultsListRect(app);
        for (const auto& r : app.searchPanelRows) {
            if (r.type == App::SearchPanelRow::Type::File) {
                fileY = list.top + r.top + r.height * 0.5f - app.searchResultsScroll;
                fileIndex = r.index;
                break;
            }
        }
    }
    check(fileY > 0.0f && fileIndex >= 0, "the sibling file row is laid out in the dock");
    if (fileY > 0.0f && fileIndex >= 0) {
        std::wstring expectedName = app.folderResults[fileIndex].fileName;
        press((searchResultsListRect(app).left + searchResultsListRect(app).right) * 0.5f,
              fileY);
        check(std::filesystem::path(app.currentFile).filename().wstring() == expectedName,
              "clicking a file row opens that sibling document");
        bool previousStillOpen = false;
        for (const auto& t : app.tabs)
            if (t.path == TINTA_FOLDERSEARCH_FIXTURE) previousStillOpen = true;
        check(previousStillOpen && app.tabs.size() >= 2,
              "opening a sibling keeps the previous document open in its tab");
        check(app.folderResults.empty(), "opening a sibling clears the extra results");
    }

    // Open-files checkbox: a second tab is an extra source even when the
    // folder scan is off.
    closeSearchInput(app);
    if (app.showSearchResults) closeSearchResultsPanel(app);
    app.folderSearchEnabled = false;
    app.searchAllOpenFiles = false;
    app.folderResults.clear();
    app.currentFile = TINTA_FOLDERSEARCH_FIXTURE;
    app.tabs.clear();
    App::DocTab current;
    current.id = 1; current.path = TINTA_FOLDERSEARCH_FIXTURE;
    current.title = L"current.md";
    app.tabs.push_back(current);
    App::DocTab other;
    other.id = 2;
    other.path = (std::filesystem::path(TINTA_FOLDERSEARCH_FIXTURE).parent_path()
                  / L"sibling.md").string();
    other.title = L"sibling.md";
    app.tabs.push_back(other);
    app.activeTab = 0;

    key(app, 'F', 6, true); type(app, L"needle");
    key(app, 'F', 6, true, true);
    settle();
    check(app.searchOpenFilesToggleRect.right > app.searchOpenFilesToggleRect.left,
          "render publishes the open-files checkbox rect");
    press((app.searchOpenFilesToggleRect.left + app.searchOpenFilesToggleRect.right) * 0.5f,
          (app.searchOpenFilesToggleRect.top + app.searchOpenFilesToggleRect.bottom) * 0.5f);
    check(app.searchAllOpenFiles, "clicking the checkbox enables the open-files scan");
    int openGenBefore = app.folderSearchGeneration;
    startFolderSearchScan(app);
    check(app.folderSearchGeneration == openGenBefore + 1, "open-files scan started");
    check(drainScan(), "open-files scan completes");
    check(hasResult(L"sibling.md"),
          "open-files scan reaches another open tab outside the folder");

    app.tabs.clear();
    key(app, VK_ESCAPE, 27);
}

// Regression: with several matching siblings the dock lists them in scan
// order; clicking any row - especially the last one - must open the exact file
// shown on that row, not a neighbour (reported wrong-file opens).
void folderSearchLastRow(App& app) {
    closeSearchInput(app);
    if (app.showSearchResults) closeSearchResultsPanel(app);
    app.currentFile = TINTA_FOLDERSEARCH_FIXTURE;
    enterEditMode(app); editorReparse(app); ensureLayoutComplete(app);
    app.editMode = false;
    app.contentScale = 1;
    app.showToc = false;
    app.searchResultsAnimation = 1;
    app.searchResultsScroll = 0;
    app.searchAllOpenFiles = false;
    app.folderSearchEnabled = true;
    app.tabs.clear();
    app.activeTab = 0;
    app.folderResults.clear();
    app.folderSearchGeneration = 0;

    auto press = [&](float x, float y) {
        app.mouseX = static_cast<int>(x);
        app.mouseY = static_cast<int>(y);
        handleMouseDown(app, app.hwnd, MK_LBUTTON,
                        MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
        handleMouseUp(app, app.hwnd, 0,
                      MAKELPARAM(static_cast<int>(x), static_cast<int>(y)));
    };
    auto settle = [&]() {
        app.searchResultsAnimation = 1.0f;
        app.renderTarget->BeginDraw();
        renderSearchResultsPanel(app);
        app.renderTarget->EndDraw();
    };
    auto drainScan = [&]() {
        MSG msg;
        DWORD deadline = GetTickCount() + 3000;
        while (GetTickCount() < deadline) {
            if (PeekMessageW(&msg, app.hwnd, WM_APP_FOLDER_SEARCH,
                             WM_APP_FOLDER_SEARCH, PM_REMOVE)) {
                completeFolderSearch(app, (void*)msg.lParam);
                return true;
            }
            Sleep(5);
        }
        return false;
    };

    key(app, 'F', 6, true); type(app, L"needle");
    key(app, 'F', 6, true, true);
    settle();
    startFolderSearchScan(app);
    check(drainScan(), "multi-file folder scan completes");
    check(app.folderResults.size() >= 3, "folder scan lists several matching siblings");
    settle();
    check(!app.searchPanelRows.empty(), "rows laid out for the multi-file folder");

    // Click the last file row, after scrolling so it is on screen.
    int lastFileRow = -1;
    for (int i = 0; i < (int)app.searchPanelRows.size(); i++)
        if (app.searchPanelRows[i].type == App::SearchPanelRow::Type::File)
            lastFileRow = i;
    check(lastFileRow >= 0, "the scan lists at least one file row");
    if (lastFileRow >= 0) {
        float listH = 0.0f, contentH = 0.0f;
        {
            const auto list = searchResultsListRect(app);
            listH = list.bottom - list.top;
        }
        for (const auto& r : app.searchPanelRows)
            contentH = std::max(contentH, r.top + r.height);
        app.searchResultsScroll = std::max(0.0f, contentH - listH);
        settle();
        int expectedIndex = app.searchPanelRows[lastFileRow].index;
        std::wstring expectedName = app.folderResults[expectedIndex].fileName;
        const auto list = searchResultsListRect(app);
        float y = list.top + app.searchPanelRows[lastFileRow].top +
                  app.searchPanelRows[lastFileRow].height * 0.5f -
                  app.searchResultsScroll;
        press((list.left + list.right) * 0.5f, y);
        check(std::filesystem::path(app.currentFile).filename().wstring() == expectedName,
              "clicking the last file row opens the file shown there");
    }

    closeSearchInput(app);
    if (app.showSearchResults) closeSearchResultsPanel(app);
    app.tabs.clear();
}

// Regression: the strip ellipsizes file names, so dwelling on a tab must
// reveal the whole name on a floating card below it. The dwell handler
// grants the tab; the render pass only draws when the title really
// overflows and the pointer rests on the label, not its close button
// (tab-tooltip fixture).
void tabTitleTooltip(App& app) {
    closeSearchInput(app);
    if (app.showSearchResults) closeSearchResultsPanel(app);
    app.tabs.clear();
    app.activeTab = 0;
    app.hoveredTab = -1;
    app.tabTooltipTab = -1;
    app.tabTooltipRect = {};
    app.searchResultsAnimation = 1;

    const std::string dir =
        std::filesystem::path(TINTA_TABTOOLTIP_FIXTURE).parent_path().string();
    const std::string shortName = "tab-tooltip.md";
    const std::string longName =
        "a-very-long-file-name-that-can-never-fit-inside-a-tab-card-without-the-tooltip.md";
    // The tab row starts from the actively loaded document, exactly as a
    // real window: currentFile is already open and the sibling appends.
    app.currentFile = TINTA_TABTOOLTIP_FIXTURE;
    tabsInit(app);
    tabOpenPath(app, app.hwnd, (std::filesystem::path(dir) / longName).string());
    check(app.tabs.size() == 2 && app.tabs[0].title == toWide(shortName) &&
          app.tabs[1].title == toWide(longName),
          "both fixture files open as tabs with their file names");
    check(app.activeTab == 1, "opening the second tab makes it active");

    // The dwell-endpoint grants the hovered tab after resting on it.
    app.hoveredTab = -1;
    app.tabTooltipTab = -1;
    handleTabTooltipTimer(app, app.hwnd);
    check(app.tabTooltipTab == -1, "dwell over no tab stays hidden");
    app.hoveredTab = 1;
    handleTabTooltipTimer(app, app.hwnd);
    check(app.tabTooltipTab == 1, "dwell over a tab grants its tooltip");

    // Render publishes the card only for a title the strip would truncate.
    app.mouseY = 20;
    app.mouseX = 300;  // left half of the long tab's label
    app.hoveredTab = 1;
    app.tabTooltipTab = 1;
    {
        app.renderTarget->BeginDraw();
        renderTabStrip(app);
        HRESULT hr = app.renderTarget->EndDraw();
        check(SUCCEEDED(hr), "tab strip draws with a tooltip card");
        check(app.tabTooltipRect.right > app.tabTooltipRect.left &&
              app.tabTooltipRect.bottom > app.tabTooltipRect.top &&
              app.tabTooltipRect.top >= chromeTopHeight(app),
              "dwelling a long title publishes the card below the strip");
    }
    // Pointer parked over the close button withdraws it even mid-dwell.
    app.mouseX = 405;
    {
        app.renderTarget->BeginDraw();
        renderTabStrip(app);
        app.renderTarget->EndDraw();
        check(app.tabTooltipRect.right == 0 && app.tabTooltipRect.bottom == 0,
              "card hides while the pointer rests on the close button");
    }
    // A short title that fits fully raises nothing.
    app.mouseX = 100;
    app.hoveredTab = 0;
    app.tabTooltipTab = 0;
    {
        app.renderTarget->BeginDraw();
        renderTabStrip(app);
        app.renderTarget->EndDraw();
        check(app.tabTooltipRect.right == 0 && app.tabTooltipRect.bottom == 0,
              "a fitting title raises no tooltip card");
    }
    // Without the dwell, or pointed elsewhere, the card stays away.
    app.hoveredTab = 1;
    app.tabTooltipTab = -1;
    app.mouseX = 300;
    {
        app.renderTarget->BeginDraw();
        renderTabStrip(app);
        app.renderTarget->EndDraw();
        check(app.tabTooltipRect.right == 0 && app.tabTooltipRect.bottom == 0,
              "no card before the dwell fires");
    }
    app.hoveredTab = -1;
    app.tabTooltipTab = 1;
    {
        app.renderTarget->BeginDraw();
        renderTabStrip(app);
        app.renderTarget->EndDraw();
        check(app.tabTooltipRect.right == 0 && app.tabTooltipRect.bottom == 0,
              "leaving the strip hides the card");
    }

    app.tabs.clear();
    app.hoveredTab = -1;
    app.tabTooltipTab = -1;
    app.tabTooltipRect = {};
}
}
int runSearchInputTests() {
    HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 2;
    auto state = std::make_unique<App>(); App& app = *state;
    app.hwnd = CreateWindowExW(0,L"STATIC",L"Find focus tests",WS_POPUP,0,0,1050,900,nullptr,nullptr,nullptr,nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width=1050; app.height=900; updateTextFormats(app);
    app.currentFile=TINTA_FIND_FIXTURE;
    Settings settings; settings.keyProfile="windows"; applyKeymap(app,settings);
    enterEditMode(app); editorReparse(app); ensureLayoutComplete(app);
    check(app.tableCellRects.size()==6 && app.codeBlocks.size()==1, "mixed fixture lays out its table and code");
    fieldEditing(app); focusTransfers(app); searchResultsPanel(app);
    folderSearchToggles(app);
    folderSearchLastRow(app);
    tabTitleTooltip(app);
    DestroyWindow(app.hwnd); app.hwnd=nullptr; state.reset(); CoUninitialize();
    OleUninitialize();
    std::cout << "Search input: " << failures << " failures\n";
    return failures ? 1 : 0;
}
