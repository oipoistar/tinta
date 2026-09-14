#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "overlays.h"
#include "render.h"
#include "search.h"
#include "settings.h"
#include "tableedit.h"
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
        enterEditMode(app); app.editorShowPreview = true; app.editorScrollY = app.scrollY = 0;
        editorReparse(app); ensureLayoutComplete(app);
    };
    auto openCell = [&]() {
        ensureLayoutComplete(app);
        for (const auto& cell : app.tableCellRects) if (cell.row == 1 && cell.col == 0) {
            auto rect = cell.rect;
            click(app, documentViewportX(app)+(rect.left+rect.right)/2-app.scrollX,
                  (rect.top+rect.bottom)/2-app.scrollY);
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
    fieldEditing(app); focusTransfers(app);
    DestroyWindow(app.hwnd); app.hwnd=nullptr; state.reset(); CoUninitialize();
    OleUninitialize();
    std::cout << "Search input: " << failures << " failures\n";
    return failures ? 1 : 0;
}
