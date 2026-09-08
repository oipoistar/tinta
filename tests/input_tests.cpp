#include "input.h"
#include "settings.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

// Same contract as the main message loop: a consumed key must not have
// its layout-dependent WM_CHAR translated into the new editor/overlay.
void stroke(App& app, unsigned vk, unsigned character = 0) {
    bool consumed = handleKeyDown(app, app.hwnd, vk);
    if (!consumed && character) handleCharInput(app, app.hwnd, character);
}
}

int main() {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Tinta input tests", 0,
                              0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    check(app.hwnd != nullptr, "message-only test window created");
    if (!app.hwnd) return 1;
    app.currentFile = TINTA_SHORTCUT_FIXTURE;
    app.width = 1050;
    app.height = 900;
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    stroke(app, VK_F1);
    check(app.showHelp, "F1 opens Help with no character message");
    stroke(app, VK_F1);
    check(!app.showHelp, "F1 closes Help");
    stroke(app, 'P', 'p');
    check(!app.showHelp, "P cannot open Help");

    std::wstring original;
    for (unsigned ch : {0x65, 0x0443, 0x03B5}) {
        app.editMode = false;
        stroke(app, 'E', ch);
        check(app.editMode && !app.editorDirty, "E enters a clean editor for Latin/Cyrillic/Greek input");
        check(app.editorText.rfind(L"# Shortcut regression:", 0) == 0,
              "the mode-switch key cannot prefix the Markdown heading");
        original = app.editorText;
        stroke(app, 'X', 'x');
        check(app.editorText == L"x" + original, "immediate next character is preserved without a timer");
    }
    app.editMode = false;
    stroke(app, 'E', 0x0443);
    original = app.editorText;
    stroke(app, VK_F1);
    check(app.showHelp && app.editMode, "F1 opens Help while editing");
    stroke(app, 'E', 'e');
    check(app.editorText == original, "Help captures printable input over the editor");
    stroke(app, VK_F1);
    check(!app.showHelp && app.editMode && !app.editorDirty, "Help closes over a clean editor");
    stroke(app, 'P', 'p');
    check(app.editorText == L"p" + original, "normal P typing survives in editor");

    app.editMode = false;
    app.showSearch = true;
    app.searchActive = true;
    app.searchJustOpened = false;
    stroke(app, 'E', 0x0443);
    check(!app.editMode && app.searchQuery == L"\x0443", "search receives non-Latin E text");
    stroke(app, VK_ESCAPE);
    app.showToc = true;
    app.tocPinned = false;
    stroke(app, 'E', 0x0443);
    check(!app.editMode && app.tocFilter == L"\x0443", "unpinned Contents receives non-Latin filter text");
    app.tocPinned = true;
    stroke(app, 'E', 0x0443);
    check(app.editMode, "pinned Contents lets E reach the document");

    app.editMode = false;
    app.showToc = false;
    app.showSettings = true;
    stroke(app, 'E', 'e');
    stroke(app, VK_F1);
    check(!app.editMode && !app.showHelp, "Settings retains modal keyboard capture");
    app.showSettings = false;
    settings.keyProfile = "vim";
    applyKeymap(app, settings);
    stroke(app, VK_OEM_2, '?');
    check(app.showHelp, "vim punctuation opens Help");
    stroke(app, VK_OEM_2, '?');
    check(!app.showHelp, "vim punctuation closes Help");
    stroke(app, VK_OEM_2, '/');
    stroke(app, 'E', 'e');
    check(app.showSearch && app.searchQuery == L"e", "vim search keeps its first query character");
    stroke(app, VK_ESCAPE);
    stroke(app, VK_OEM_1, 0xFF1A);
    check(app.editMode && !app.editorDirty, "full-width colon still enters a clean editor");

    app.editMode = false;
    settings.keyProfile = "custom";
    settings.keyOverrides = {{"edit", "F2"}, {"help", "H"}};
    applyKeymap(app, settings);
    stroke(app, 'H', 'h');
    check(app.showHelp, "custom letter Help opens only once");
    stroke(app, 'H', 'h');
    check(!app.showHelp, "custom letter Help closes without reopening");
    stroke(app, VK_F2);
    check(app.editMode && !app.editorDirty, "custom function key enters Edit");
    app.confirmExitPending = true;
    settings.keyOverrides = {{"help", "F3"}};
    applyKeymap(app, settings);
    stroke(app, VK_F3);
    check(app.confirmExitPending && !app.showHelp, "Help cannot bypass unsaved-changes confirmation");

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    std::cout << "Input shortcuts: " << failures << " failures\n";
    return failures ? 1 : 0;
}
