// #245: the edit-mode preview is docked like the side panels (#213), and
// the edit-mode hints show for the first few sessions, then fade on their
// own instead of piling up behind the bell
#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "i18n.h"
#include "input.h"
#include "render.h"
#include "settings.h"
#include "signals.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { if (failures < 40) std::cerr << "FAIL docked preview: " << message << '\n'; ++failures; }
}

void key(App& app, unsigned vk, bool ctrl = false) {
    BYTE old[256], pressed[256];
    GetKeyboardState(old);
    memcpy(pressed, old, sizeof old);
    pressed[VK_CONTROL] = ctrl ? 0x80 : 0;
    pressed[VK_SHIFT] = pressed[VK_MENU] = 0;
    SetKeyboardState(pressed);
    handleKeyDown(app, app.hwnd, vk);
    SetKeyboardState(old);
}

void hover(App& app, float x, float y) {
    handleMouseMove(app, app.hwnd, MAKELPARAM((int)x, (int)y));
}

void click(App& app, float x, float y) {
    const LPARAM point = MAKELPARAM((int)x, (int)y);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, point);
    handleMouseUp(app, app.hwnd, 0, point);
}

// One frame of the edit-mode chrome under test
void paint(App& app) {
    ensureLayoutComplete(app);
    app.renderTarget->BeginDraw();
    renderEditPaneChrome(app);
    renderTabStrip(app);
    renderSignalChips(app);
    renderEditorReadingButton(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "edit-mode chrome paints");
}

// Let a fade run on the real clock
void settle(App& app, int ms) {
    for (int elapsed = 0; elapsed < ms; elapsed += 50) {
        Sleep(50);
        paint(app);
    }
}

const App::SignalChip* findHint(const App& app, const char* trKey) {
    for (const auto& chip : app.signalChips) {
        if (chip.transient && chip.text == tr(app, trKey)) return &chip;
    }
    return nullptr;
}

void leaveEditMode(App& app) {
    app.editorDirty = false;
    if (app.editMode) exitEditMode(app);
    check(!app.editMode, "a clean buffer leaves edit mode");
}

// The preview is a flat pane from the seam to the window's edges, below a
// full-width tab strip, with its page where the reader's page starts
void geometry(App& app, const std::wstring& source) {
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {650, 1050}) {
            app.width = width;
            app.height = 900;
            updateTextFormats(app);
            restoreEditBuffer(app, source, false, 0, 0);
            app.editorShowPreview = true;
            app.layoutDirty = true;
            ensureLayoutComplete(app);
            check(editSplitPreview(app), "the split editor docks the preview");
            check(documentViewportX(app) + documentViewportWidth(app) == static_cast<float>(app.width),
                  "the preview runs to the window's right edge");
            const float divider = editSplitDividerX(app);
            check(editorPaneWidth(app) <= divider && divider < documentViewportX(app),
                  "one hairline separates source and preview");
            check(app.docText.find(L"Final heading") != std::wstring::npos, "the mixed fixture lays out completely");
            check(!app.scrollAnchors.empty(), "the docked preview lays out its blocks");
            const float splitTop = app.scrollAnchors.empty() ? 0.0f : app.scrollAnchors.front().renderedY;
            check(splitTop >= documentContentTop(app) - 0.5f, "the docked page starts below the tab strip");
            setEditorReadingPreview(app, true);
            ensureLayoutComplete(app);
            check(!app.scrollAnchors.empty() && std::abs(app.scrollAnchors.front().renderedY - splitTop) < 0.5f,
                  "the docked page starts where the reading view's page starts");
            setEditorReadingPreview(app, false);
            ensureLayoutComplete(app);

            // The strip spans the window: tabs reach past the source column
            // and the empty title bar beside the window buttons drags
            const auto savedTabs = app.tabs;
            const int savedActive = app.activeTab;
            app.tabs.resize(8);
            for (int i = 0; i < 8; ++i) app.tabs[i].title = L"Document " + std::to_wstring(i);
            app.activeTab = 0;
            paint(app);
            const auto drag = titleDragRect(app);
            check(drag.right >= captionIslandLeft(app) && drag.left > editorPaneWidth(app),
                  "the title bar drags beside the window buttons, past the source column");
            bool pastSource = false;
            for (const auto& hit : app.tabHits) {
                if (hit.index < 0) continue;
                const int middle = (int)((hit.rect.left + hit.rect.right) / 2);
                if (middle - 2 < editorPaneWidth(app)) continue;
                pastSource = true;
                check(tabDropInsertionIndex(app, {middle - 2, (LONG)dpi(app, 20)}) == hit.index &&
                      tabDropInsertionIndex(app, {middle + 2, (LONG)dpi(app, 20)}) == hit.index + 1,
                      "tab drops right of the source column land between tabs");
            }
            check(pastSource, "tabs use the strip beyond the source column");
            app.tabs = savedTabs;
            app.activeTab = savedActive;
        }
    }
    leaveEditMode(app);
}

// The first three edit sessions show the Esc hint and the Read button's
// intro; later sessions and tab switches stay quiet
void sessions(App& app) {
    applyTheme(app, 0);
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    app.editHintsShown = 0;
    for (int session = 1; session <= 4; ++session) {
        app.signalChips.clear();
        enterEditMode(app);
        check(app.editMode, "edit mode opens");
        const bool teach = session <= 3;
        check(app.editHintSession == teach, "the first three sessions are hint sessions");
        check((findHint(app, "toast.exit_edit_hint") != nullptr) == teach,
              "the Esc hint shows in the first sessions only");
        check(editorReadingButtonNeedsTicks(app) == teach,
              "the Read button's intro runs in the first sessions only");
        if (session == 1) {
            app.signalChips.clear();
            restoreEditBuffer(app, L"# Parked\n\nText\n", false, 0, 0);
            check(app.editHintsShown == 1 && findHint(app, "toast.exit_edit_hint") == nullptr,
                  "returning to a parked edit tab does not replay the hints");
        }
        leaveEditMode(app);
    }
    check(app.editHintsShown == 3, "the count stops after three sessions");
}

// A hint holds a moment, fades in place and leaves the tray and the bell
// alone; regular chips still park
void hintFade(App& app) {
    app.signalChips.clear();
    app.signalTray.clear();
    app.signalUnseen = 0;
    app.signalTrayOpen = false;
    signalHintKey(app, SIGI_INFO, "toast.exit_confirm");
    signalPushKey(app, SIG_SUCCESS, SIGI_CHECK, "toast.paste_table");
    check(app.signalChips.size() == 2 && app.signalChips[0].transient && !app.signalChips[1].transient,
          "hints and regular chips share the stack");
    check(app.signalChips[0].lifetime + 0.4f <= 2.0f, "a hint is gone within two seconds");
    D2D1_RECT_F steady{};
    bool gone = false, inPlace = true;
    for (int step = 0; step < 80; ++step) {
        app.signalLastTick -= 0.1;  // drive the drain clock in tenths
        paint(app);
        const App::SignalChip* hint = findHint(app, "toast.exit_confirm");
        if (!hint) { gone = true; break; }
        if (hint->tuckT < 0.0f) {
            steady = hint->rect;
        } else if (std::abs(hint->rect.right - steady.right) > 0.5f ||
                   std::abs(hint->rect.bottom - steady.bottom) > 0.5f) {
            inPlace = false;
        }
    }
    check(gone && inPlace, "the hint fades out in place on its own");
    check(app.signalTray.empty() && app.signalUnseen == 0, "a faded hint leaves nothing in the tray or on the bell");
    check(app.signalChips.size() == 1, "the regular chip outlives the hint");
    for (int step = 0; step < 80 && !app.signalChips.empty(); ++step) {
        app.signalLastTick -= 0.1;
        paint(app);
    }
    check(app.signalChips.empty() && app.signalTray.size() == 1 && app.signalUnseen == 1,
          "regular chips still park in the tray");

    // A click on a hint fades it instead of pinning it
    app.signalChips.clear();
    signalHintKey(app, SIGI_EYE, "toast.preview_hidden");
    paint(app);
    const D2D1_RECT_F r = app.signalChips.back().rect;
    check(signalMouseDown(app, app.hwnd, r.left + dpi(app, 40), (r.top + r.bottom) / 2),
          "a press on a hint is consumed");
    check(!app.signalChips.back().pinned && app.signalChips.back().tuckT >= 0.0f,
          "a click fades a hint instead of pinning it");
    // A press anywhere else fades hints at once
    app.signalChips.clear();
    signalHintKey(app, SIGI_INFO, "toast.wrap_on");
    signalTuckPassive(app);
    check(app.signalChips.back().tuckT >= 0.0f, "a press elsewhere fades a hint at once");
    for (int step = 0; step < 10 && !app.signalChips.empty(); ++step) {
        app.signalLastTick -= 0.1;
        paint(app);
    }
    check(app.signalChips.empty() && app.signalTray.size() == 1 && app.signalUnseen == 1,
          "dismissed hints never reach the tray");
    app.signalTray.clear();
    app.signalUnseen = 0;
}

// Leaving edit mode, or switching away from its tab, retires its hints
// at once: "press Esc again" never outlives the mode
void hintsLeaveWithTheMode(App& app, const std::wstring& source) {
    for (bool tabSwitch : {false, true}) {
        restoreEditBuffer(app, source, false, 0, 0);
        app.signalChips.clear();
        signalPushKey(app, SIG_SUCCESS, SIGI_CHECK, "toast.paste_table");
        key(app, VK_ESCAPE);
        check(findHint(app, "toast.exit_confirm") != nullptr, "the first Esc shows its hint");
        if (tabSwitch) {
            tabOpenStartPage(app, app.hwnd);
        } else {
            key(app, VK_ESCAPE);
        }
        check(!app.editMode, tabSwitch ? "a tab switch parks the edit buffer" : "the second Esc leaves edit mode");
        const App::SignalChip* hint = findHint(app, "toast.exit_confirm");
        check(hint && hint->tuckT >= 0.0f, "the hint starts fading as the mode ends");
        check(!app.signalChips.front().transient && app.signalChips.front().tuckT < 0.0f,
              "regular chips keep their own time");
        if (tabSwitch) {
            tabActivate(app, app.hwnd, 0);
            check(app.editMode, "the parked edit tab comes back");
            tabCloseIndex(app, app.hwnd, 1);
            leaveEditMode(app);
        }
    }
    app.signalChips.clear();
}

// Esc, Ctrl+E and Ctrl+W answer with hints rather than tray chips
void editorHints(App& app, const std::wstring& source) {
    restoreEditBuffer(app, source, false, 0, 0);
    app.editorShowPreview = true;
    app.signalChips.clear();
    key(app, VK_ESCAPE);
    check(app.editMode && findHint(app, "toast.exit_confirm"), "the first Esc answers with a fading hint");
    key(app, 'E', true);
    check(!app.editorShowPreview && findHint(app, "toast.preview_hidden"), "Ctrl+E hides the preview with a hint");
    key(app, 'E', true);
    check(app.editorShowPreview && findHint(app, "toast.preview_shown"), "Ctrl+E shows the preview with a hint");
    const bool wrap = app.editorWordWrap;
    key(app, 'W', true);
    check(app.editorWordWrap != wrap && findHint(app, wrap ? "toast.wrap_off" : "toast.wrap_on"),
          "Ctrl+W toggles wrap with a hint");
    key(app, 'W', true);
    check(app.editorWordWrap == wrap, "Ctrl+W restores wrap");
    check(std::all_of(app.signalChips.begin(), app.signalChips.end(),
                      [](const App::SignalChip& chip) { return chip.transient; }),
          "editor toggles never queue tray chips");
    leaveEditMode(app);
    app.signalChips.clear();
}

// Past the first sessions the Read button hides over the text and lets
// clicks through; the pointer on its corner brings it back
void readButton(App& app) {
    app.editHintsShown = 3;
    enterEditMode(app);
    paint(app);
    const auto r = editorReadingButtonRect(app);
    const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    check(!editorReadingButtonShown(app) && !editorReadingButtonNeedsTicks(app) && app.readButtonAlpha == 0.0f,
          "after the first sessions the Read button stays hidden");
    click(app, cx, cy);
    check(app.editMode && !app.editorReadingPreview, "a hidden Read button lets clicks through to the source");
    // An insert menu opened over the corner wins over the button
    openEditCtxMenu(app, app.hwnd, cx, cy - dpi(app, 120));
    hover(app, cx, cy);
    check(app.editCtxOpen && !app.readButtonHover && !editorReadingButtonShown(app),
          "an open insert menu keeps the Read button hidden");
    closeEditCtxMenu(app);
    hover(app, cx, cy);
    check(app.readButtonHover && editorReadingButtonShown(app) && editorReadingButtonNeedsTicks(app),
          "the pointer on its corner reveals the Read button");
    settle(app, 300);
    check(app.readButtonAlpha == 1.0f, "the Read button fades in");
    click(app, cx, cy);
    check(app.editorReadingPreview, "the revealed Read button opens the reading view");
    paint(app);
    const auto back = editorReadingButtonRect(app);
    const float bx = (back.left + back.right) / 2, by = (back.top + back.bottom) / 2;
    hover(app, bx, by);
    click(app, bx, by);
    check(app.editMode && !app.editorReadingPreview, "the same corner leads back to the source");
    hover(app, static_cast<float>(app.width) / 4, static_cast<float>(app.height) / 2);
    check(!app.readButtonHover, "moving away drops the hover");
    settle(app, 600);
    check(app.readButtonAlpha == 0.0f && !editorReadingButtonShown(app) && !editorReadingButtonNeedsTicks(app),
          "the Read button fades out and lets the timer stop");
    leaveEditMode(app);

    // A new user's session: the intro shows the button, then fades it
    app.editHintsShown = 0;
    enterEditMode(app);
    settle(app, 300);
    check(app.readButtonAlpha == 1.0f && editorReadingButtonShown(app), "a hint session fades the Read button in");
    settle(app, 2200);
    check(app.readButtonAlpha == 0.0f && !editorReadingButtonShown(app) && !editorReadingButtonNeedsTicks(app),
          "and fades it out after about two seconds");
    leaveEditMode(app);
    app.signalChips.clear();
}

// The count persists in settings.ini; malformed values start over
void persistence() {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path settingsPath = getSettingsPath();
    if (settingsPath.parent_path() != std::filesystem::path(exe).parent_path()) {
        check(false, "persistence checks need a portable settings.ini beside the test binary");
        return;
    }
    Settings stored = loadSettings();
    stored.editHintsShown = 2;
    saveSettings(stored);
    check(loadSettings().editHintsShown == 2, "the hint session count survives a restart");
    for (const char* bad : {"-4", "two", "", "3x", "99999999999999"}) {
        {
            std::ofstream file(settingsPath);
            file << "[Settings]\neditHintsShown=" << bad << "\n";
        }
        check(loadSettings().editHintsShown == 0, "a malformed hint count starts the hints over");
    }
    std::ofstream file(settingsPath);
    file << "[Settings]\nhasAskedFileAssociation=1\ncheckUpdates=0\n";
}
}

int runDockedPreviewTests() {
    OleInitialize(nullptr);
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Docked preview tests", WS_POPUP, 0, 0, 1050, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    const auto fixture = std::filesystem::path(TINTA_FIND_FIXTURE).parent_path() / L"docked-preview-245.md";
    std::ifstream input(fixture);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    const auto source = toWide(bytes);
    check(!source.empty(), "the #245 fixture loads");
    app.currentFile = toUtf8(fixture.wstring());
    tabsInit(app);
    geometry(app, source);
    sessions(app);
    hintFade(app);
    hintsLeaveWithTheMode(app, source);
    editorHints(app, source);
    readButton(app);
    persistence();
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    OleUninitialize();
    std::cout << "Docked preview and hints: " << failures << " failures\n";
    return failures ? 1 : 0;
}
