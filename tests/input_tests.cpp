#include "input.h"
#include "d2d_init.h"
#include "overlays.h"
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

void settingsDismissal() {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Settings input tests", WS_POPUP,
        100, 100, 1050, 900, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        check(false, "settings native renderer initializes");
        if (app.hwnd) DestroyWindow(app.hwnd);
        return;
    }
    app.currentFile = TINTA_SHORTCUT_FIXTURE;
    app.tabs.resize(1);
    app.tabs[0].path = app.currentFile;
    app.editorText = L"# Unsaved text\n\n$x^2$\n";
    app.editorDirty = true;
    app.editorCursorPos = 4;
    app.editRailAnim = 1;
    auto open = [&](int section = 0, float animation = 1.0f) {
        app.showSettings = true;
        app.settingsSection = section;
        app.settingsAnimation = animation;
        app.renderTarget->BeginDraw();
        renderSettingsOverlay(app);
        check(SUCCEEDED(app.renderTarget->EndDraw()), "settings panel renders");
    };
    auto click = [&](float x, float y) {
        LPARAM point = MAKELPARAM((int)x, (int)y);
        handleMouseDown(app, app.hwnd, MK_LBUTTON, point);
        handleMouseUp(app, app.hwnd, 0, point);
        check(app.editorText == L"# Unsaved text\n\n$x^2$\n" && app.editorCursorPos == 4 && app.editorDirty,
              "settings clicks preserve editor text, cursor and unsaved changes");
        check(!app.selecting && !app.editorSelecting && !app.createRefPending,
              "settings dismissal cannot start document selection or activate a link");
    };
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float scale : {1.0f, 1.5f, 2.0f}) {
            app.contentScale = scale;
            updateTextFormats(app);
            for (int width : {650, 1050}) {
                app.width = width;
                app.height = width == 650 ? 600 : 900;
                for (bool edit : {false, true}) {
                    app.editMode = edit;
                    for (float animation : {0.0f, 1.0f}) {
                        open(0, animation);
                        auto panel = settingsPanelRect(app);
                        click(panel.left+8, (panel.top+panel.bottom)/2);
                        check(app.showSettings, "blank panel space does not dismiss settings");
                        const auto close = settingsCloseButtonRect(app);
                        check(close.left > panel.left && close.right < panel.right &&
                              close.top > panel.top && close.bottom < panel.bottom,
                              "close button stays inside the animated/scaled panel");
                        bool closeHit = false;
                        for (const auto& hit : app.settingsHits)
                            if (hit.second == SET_CLOSE) closeHit = true;
                        check(closeHit, "renderer registers a close-button hit target");
                        app.settingsLangOpen = app.settingsKeysOpen = true;
                        click((close.left+close.right)/2, (close.top+close.bottom)/2);
                        check(!app.showSettings && app.settingsAnimation == 0 &&
                              !app.settingsLangOpen && !app.settingsKeysOpen && app.settingsHits.empty(),
                              "close button clears transient dialog state");
                        for (const auto point : {D2D1::Point2F(panel.left-5, (panel.top+panel.bottom)/2),
                                D2D1::Point2F(panel.right+5, (panel.top+panel.bottom)/2),
                                D2D1::Point2F(app.width/2.0f, panel.top-5),
                                D2D1::Point2F(app.width/2.0f, panel.bottom+5)}) {
                            open(0, animation);
                            app.hoveredLink = "fileref-missing:must-not-be-created.md";
                            click(point.x, point.y);
                            check(!app.showSettings, "all four backdrop edges dismiss settings");
                        }
                    }
                    open();
                    // The backdrop also covers the icon and + tab control.
                    app.tabHits = {{D2D1::RectF(dpi(app, 80), 0, dpi(app, 110), dpi(app, 40)), -2}};
                    click(dpi(app, 90), dpi(app, 20));
                    check(!app.showSettings && app.tabs.size() == 1,
                          "backdrop click on + does not create a tab");
                    open();
                    click(dpi(app, 20), dpi(app, 20));
                    check(!app.showSettings && !app.showContextMenu,
                          "backdrop click on the icon does not open another menu");
                }
            }
        }
    }
    app.width = 1050;
    app.height = 900;
    app.contentScale = 1;
    updateTextFormats(app);
    open();
    const auto panel = settingsPanelRect(app);
    // A dropdown row outside the panel is still part of the dialog.
    app.settingsLangOpen = true;
    app.settingsHits.push_back({D2D1::RectF(panel.left, panel.bottom+4, panel.left+100, panel.bottom+30), SET_LANG_PICK_BASE});
    click(panel.left+30, panel.bottom+15);
    check(app.showSettings && !app.settingsLangOpen && app.languageSetting == -1,
          "an outlying language choice is selected before backdrop dismissal");
    app.settingsLangOpen = true;
    click(panel.left+8, panel.top+8);
    check(app.showSettings && !app.settingsLangOpen, "inside blank space collapses only the dropdown");
    open(1);
    const auto track = app.settingsSliderTrack[0];
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM((int)(track.left+10), (int)track.top));
    check(app.settingsDragSlider != 0, "reading-width slider starts dragging");
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM(5, 500));
    check(app.showSettings && !app.settingsDragSlider && GetCapture() != app.hwnd,
          "releasing a slider outside keeps settings open and releases capture");
    const int readingWidth = app.readingWidthPct;
    app.settingsLangOpen = app.settingsKeysOpen = true;
    app.settingsDragSlider = SET_SLIDER_READING;
    SetCapture(app.hwnd);
    stroke(app, VK_ESCAPE);
    check(!app.showSettings && !app.settingsLangOpen && !app.settingsKeysOpen &&
          !app.settingsDragSlider && GetCapture() != app.hwnd && app.readingWidthPct == readingWidth,
          "Escape shares close cleanup without reverting settings");
    openContextMenu(app, 4, chromeTopHeight(app), true);
    const auto& entries = contextMenuEntries(app);
    for (int row = 0; row < (int)entries.size(); ++row) {
        if (entries[row].action != CTX_SETTINGS) continue;
        click(app.contextMenuX+20, app.contextMenuY+contextMenuItemTop(app, row)+contextMenuItemHeight(app)/2);
        check(app.showSettings && !app.showContextMenu && !app.swallowNextMouseUp,
              "the icon menu's opening click cannot immediately dismiss settings");
        break;
    }
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
}
}

int runTabDropTests();
int runSuperscriptTests();
int runSidePanelTests();
int runImageTests(const char* remoteBase);
int runTabDropLaunchProbe();

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--image-tests")
        return runImageTests(argc > 2 ? argv[2] : nullptr);
    if (argc == 2 && std::string(argv[1]) == "--sidepanel-tests") return runSidePanelTests();
    if (argc == 2 && std::string(argv[1]) == "--superscript-tests") return runSuperscriptTests();
    if (argc == 2 && std::string(argv[1]) == "--tab-drop-tests") return runTabDropTests();
    if (argc == 7 && std::string(argv[1]) == "--cascade") return runTabDropLaunchProbe();
    settingsDismissal();
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
