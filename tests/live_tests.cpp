// Live (live_editor.h, docs/live-editor.md): a document typed in where it
// reads, through the real key, character and pointer handlers, drawn into
// frame previews in two themes at two widths. Live shares its engine with
// sheet cards (live_text.h), whose own checks are in sheet_tests.cpp.
#include "clipboard_snapshot.h"
#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "frame_preview.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "live_text.h"
#include "overlays.h"
#include "render.h"
#include "settings.h"
#include "sidepanels.h"
#include "tabs.h"
#include "touch.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

namespace {
namespace fs = std::filesystem;

// The test window hands a lost capture to Live as the app's window does
App* g_liveApp = nullptr;
WNDPROC g_staticProc = nullptr;
LRESULT CALLBACK captureProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CAPTURECHANGED && g_liveApp && (HWND)lParam != hwnd) liveCaptureLost(*g_liveApp);
    return CallWindowProcW(g_staticProc, hwnd, msg, wParam, lParam);
}

int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        if (failures < 80) std::cerr << "FAIL live: " << message << '\n';
        ++failures;
    }
}

std::string readBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string narrow(const std::wstring& s) { return toUtf8(s); }

// What the window draws in Live: the page, the rail, the tab strip
void drawLive(App& app) {
    app.renderTarget->Clear(app.theme.background);
    D2D1_MATRIX_3X2_F saved;
    app.renderTarget->GetTransform(&saved);
    app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(documentViewportX(app), 0) * saved);
    renderLivePage(app);
    app.renderTarget->SetTransform(saved);
    // The rail covers the strip's left corner, as in the window
    renderTabStrip(app);
    app.editRailAnim = 1.0f;
    app.editRailHover = 0;  // no tooltip left from a click on the rail
    renderEditRail(app);
    // The menus over everything, as the window draws them last
    renderEditCtxMenu(app);
    renderLiveMenu(app);
}

// A frame as the window paints it before the next press: the page laid
// out as render() lays it out, the popups and buttons where the pointer
// finds them
void paint(App& app) {
    if (app.layoutDirty) ensureLayoutComplete(app);
    app.renderTarget->BeginDraw();
    drawLive(app);
    app.renderTarget->EndDraw();
}

void frame(App& app, const fs::path& path) {
    if (app.layoutDirty) ensureLayoutComplete(app);
    check(saveFramePreview(app, path, [&] { drawLive(app); }), "frame saved: " + path.filename().string());
}

LPARAM at(D2D1_POINT_2F p) { return MAKELPARAM((int)std::lround(p.x), (int)std::lround(p.y)); }

void click(App& app, D2D1_POINT_2F p, KeyMods mods = {}) {
    paint(app);
    withKeysHeld(mods, [&] {
        handleMouseMove(app, app.hwnd, at(p));
        handleMouseDown(app, app.hwnd, MK_LBUTTON, at(p));
        handleMouseUp(app, app.hwnd, 0, at(p));
    });
}

// A press soon after another on the same spot is a double-click
void pauseClicks() { Sleep(GetDoubleClickTime() + 30); }

void typeText(App& app, const std::wstring& text) {
    for (wchar_t c : text) {
        if (c == L'\n') {
            keystroke(app, VK_RETURN);
            continue;
        }
        SHORT scan = VkKeyScanW(c);
        unsigned vk = LOBYTE(scan);
        KeyMods mods;
        mods.shift = (HIBYTE(scan) & 1) != 0;
        keystroke(app, vk, mods, c);
    }
}

void placeCaret(App& app, size_t pos) {
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = pos;
    app.editorHasSelection = false;
    paint(app);
}

size_t find(const App& app, const std::wstring& what, size_t from = 0) { return app.editorText.find(what, from); }

// The caret's place on the screen, a little into its line
D2D1_POINT_2F caretOnScreen(App& app) {
    D2D1_POINT_2F top{};
    float height = 0;
    liveCaretClientPoint(app, top, height);
    return D2D1::Point2F(top.x, top.y + height * 0.5f);
}

// Where the page draws a word it lays out, on the screen as drawn now
bool renderedWord(App& app, const std::wstring& word, D2D1_POINT_2F& point) {
    size_t pos = app.docText.find(word);
    if (pos == std::wstring::npos) return false;
    for (const auto& run : app.layoutTextRuns) {
        if (!run.layout || pos < run.docStart || pos >= run.docStart + run.docLength) continue;
        float x = 0, y = 0;
        DWRITE_HIT_TEST_METRICS m{};
        run.layout->HitTestTextPosition((UINT32)(pos - run.docStart), FALSE, &x, &y, &m);
        float top = 0, bottom = 0, shift = 0;
        livePageGeometry(app, top, bottom, shift);
        float py = run.pos.y + y + m.height * 0.5f;
        if (py >= bottom - shift) py += shift;
        point = D2D1::Point2F(documentViewportX(app) + run.pos.x + x + m.width * 0.5f - app.scrollX, py - app.scrollY);
        return true;
    }
    return false;
}

void opening(App& app, const fs::path& out) {
    check(editorLiveOn(app), "Live is on");
    check(std::fabs(documentViewportX(app) - dpi(app, 48.0f)) < 0.5f, "the page starts beside the rail");
    check(editorPaneWidth(app) == 0.0f, "no source pane in Live");
    paint(app);
    check(!app.blockSpans.empty(), "the page records its blocks");
    float top = 0, bottom = 0, shift = 0;
    check(livePageGeometry(app, top, bottom, shift), "the caret's block is placed");
    frame(app, out / "live-start.png");
}

void typing(App& app, const fs::path& out) {
    // The caret on a heading: its line shows as typed
    size_t heading = find(app, L"## Lists");
    placeCaret(app, heading + 8);
    frame(app, out / "live-heading.png");
    // Typing goes into the source, one undo step
    typeText(app, L" and more");
    check(find(app, L"## Lists and more") != std::wstring::npos, "typing goes into the text");
    // Undo takes typing back a word at a time, as in the source
    keystroke(app, 'Z', {true, false, false});
    check(find(app, L"## Lists and\n") != std::wstring::npos, "Ctrl+Z takes the last word back");
    keystroke(app, 'Z', {true, false, false});
    check(find(app, L"## Lists\n") != std::wstring::npos && find(app, L"and more") == std::wstring::npos,
          "Ctrl+Z again takes the rest");
    // Enter continues a list, an empty item ends it
    size_t item = find(app, L"- Third item");
    placeCaret(app, item + 12);
    keystroke(app, VK_RETURN);
    check(app.editorText.compare(item + 12, 3, L"\n- ") == 0, "Enter continues the list");
    typeText(app, L"Fourth");
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    check(find(app, L"- Fourth\n\n") != std::wstring::npos && find(app, L"- Fourth\n- ") == std::wstring::npos,
          "Enter on an empty item ends the list");
    frame(app, out / "live-list.png");
    // A numbered list counts on
    size_t two = find(app, L"2. Two");
    placeCaret(app, two + 6);
    keystroke(app, VK_RETURN);
    typeText(app, L"Two and a half");
    check(find(app, L"3. Two and a half\n4. Three") != std::wstring::npos, "a numbered item counts on and the rest follow");
    // Tab nests an item under the one above
    size_t second = find(app, L"- Second item");
    placeCaret(app, second + 4);
    keystroke(app, VK_TAB);
    check(find(app, L"  - Second item") != std::wstring::npos, "Tab nests an item");
    keystroke(app, VK_TAB, {false, true, false});
    check(find(app, L"\n- Second item") != std::wstring::npos, "Shift+Tab takes it back out");
    // Backspace takes a mark off first
    size_t task = find(app, L"- [ ] Ship it");
    placeCaret(app, task + 6);
    keystroke(app, VK_BACK);
    check(find(app, L"\nShip it") != std::wstring::npos, "Backspace at an item's text takes its mark off");
    keystroke(app, 'Z', {true, false, false});
    check(find(app, L"- [ ] Ship it") != std::wstring::npos, "undo puts it back");
}

void formatting(App& app, const fs::path& out) {
    // A double-click selects a word; the format bar stands over it
    pauseClicks();
    D2D1_POINT_2F word{};
    check(renderedWord(app, L"tour.", word) || renderedWord(app, L"paragraph", word), "a rendered word is found");
    size_t para = find(app, L"A paragraph with");
    placeCaret(app, para + 2);
    D2D1_POINT_2F p = caretOnScreen(app);
    paint(app);
    click(app, D2D1::Point2F(p.x + 6, p.y));
    click(app, D2D1::Point2F(p.x + 6, p.y));
    paint(app);
    check(app.editorHasSelection && app.editorText.substr(std::min(app.editorSelStart, app.editorSelEnd), 9) == L"paragraph",
          "a double-click selects the word");
    frame(app, out / "live-format-bar.png");
    // Ctrl+B makes it bold, and again plain
    keystroke(app, 'B', {true, false, false});
    check(find(app, L"A **paragraph** with") != std::wstring::npos, "Ctrl+B makes the word bold");
    keystroke(app, 'B', {true, false, false});
    check(find(app, L"A paragraph with") != std::wstring::npos, "Ctrl+B again takes it off");
    // A * typed over a selection wraps it
    typeText(app, L"*");
    check(find(app, L"A *paragraph* with") != std::wstring::npos, "* wraps the selection");
    keystroke(app, 'Z', {true, false, false});
    // Ctrl+K makes a link of the selection
    size_t start = find(app, L"paragraph with");
    app.editorSelStart = start;
    app.editorSelEnd = app.editorCursorPos = start + 9;
    app.editorHasSelection = true;
    keystroke(app, 'K', {true, false, false});
    check(find(app, L"A [paragraph]() with") != std::wstring::npos, "Ctrl+K links the selection");
    keystroke(app, 'Z', {true, false, false});
    check(find(app, L"A paragraph with") != std::wstring::npos, "undo takes the link back");
}

void slashMenu(App& app, const fs::path& out) {
    // / at a line's start opens the menu; typing narrows it
    size_t last = find(app, L"The last paragraph, after a divider.");
    placeCaret(app, last + 36);
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    typeText(app, L"/");
    paint(app);
    frame(app, out / "live-slash.png");
    typeText(app, L"tab");
    paint(app);
    frame(app, out / "live-slash-table.png");
    keystroke(app, VK_RETURN);
    check(find(app, L"| Column | Column |\n| --- | --- |") != std::wstring::npos, "/tab and Enter make a table");
    check(app.editorHasSelection && app.editorText.substr(std::min(app.editorSelStart, app.editorSelEnd), 6) == L"Column",
          "the table's first cell is selected to type over");
    typeText(app, L"Name");
    keystroke(app, VK_TAB);
    typeText(app, L"Kind");
    check(find(app, L"| Name | Kind |") != std::wstring::npos, "Tab goes from cell to cell");
    frame(app, out / "live-table.png");
    // The callouts and the diagrams come from the menu too
    keystroke(app, VK_DOWN);
    keystroke(app, VK_DOWN);
    keystroke(app, VK_END);
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    typeText(app, L"/flow");
    keystroke(app, VK_RETURN);
    check(find(app, L"```mermaid\nflowchart TD") != std::wstring::npos, "/flow makes a flowchart");
    keystroke(app, VK_DOWN);
    keystroke(app, VK_END);
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    typeText(app, L"/note");
    keystroke(app, VK_RETURN);
    typeText(app, L"Remember this.");
    check(find(app, L"> [!NOTE]\n> Remember this.") != std::wstring::npos, "/note makes a callout to type in");
    // A heading or a list made from the menu takes the typing after its mark
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    typeText(app, L"/h2");
    keystroke(app, VK_RETURN);
    typeText(app, L"Made here");
    check(find(app, L"\n## Made here") != std::wstring::npos, "/h2 makes the line a heading to type in");
    keystroke(app, VK_RETURN);
    typeText(app, L"/task");
    keystroke(app, VK_RETURN);
    typeText(app, L"Check it");
    check(find(app, L"\n- [ ] Check it") != std::wstring::npos, "/task makes a task to type in");
    // Esc closes the menu and keeps the /
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    typeText(app, L"/qu");
    keystroke(app, VK_ESCAPE);
    check(find(app, L"/qu") != std::wstring::npos, "Esc closes the menu, the typing stays");
    frame(app, out / "live-inserted.png");
}

void names(App& app, const fs::path& out) {
    size_t last = find(app, L"The last paragraph, after a divider.");
    placeCaret(app, last + 36);
    typeText(app, L" See [[Pl");
    paint(app);
    frame(app, out / "live-names.png");
    keystroke(app, VK_RETURN);
    check(find(app, L"See [[Plans]]") != std::wstring::npos, "[[ lists the notes beside the document");
}

void pointer(App& app, const fs::path& out) {
    // A click on a rendered word puts the caret on it
    pauseClicks();
    placeCaret(app, 0);
    D2D1_POINT_2F word{};
    if (renderedWord(app, L"Second item", word)) {
        click(app, word);
        size_t second = find(app, L"Second item");
        check(app.editorCursorPos >= second && app.editorCursorPos <= second + 11,
              "a click on a rendered word puts the caret in it: " + std::to_string(app.editorCursorPos) + " vs " +
                  std::to_string(second));
    } else {
        check(false, "the list's item is drawn");
    }
    // A click on a task box ticks it without moving the caret
    pauseClicks();
    placeCaret(app, find(app, L"# Live tour") + 3);
    size_t task = find(app, L"- [ ] Write the tour");
    bool ticked = false;
    for (const auto& box : app.taskRects) {
        D2D1_POINT_2F centre = D2D1::Point2F((box.rect.left + box.rect.right) * 0.5f, (box.rect.top + box.rect.bottom) * 0.5f);
        float top = 0, bottom = 0, shift = 0;
        livePageGeometry(app, top, bottom, shift);
        if (centre.y >= bottom - shift) centre.y += shift;
        D2D1_POINT_2F screen = D2D1::Point2F(documentViewportX(app) + centre.x - app.scrollX, centre.y - app.scrollY);
        size_t caret = app.editorCursorPos;
        click(app, screen);
        ticked = app.editorText.compare(task, 6, L"- [x] ") == 0 && app.editorCursorPos == caret;
        break;
    }
    check(ticked, "a click on a task box ticks it and leaves the caret");
    // Up and Down walk from block to block
    placeCaret(app, find(app, L"# Live tour") + 3);
    keystroke(app, VK_DOWN);
    keystroke(app, VK_DOWN);
    size_t para = find(app, L"A paragraph with");
    check(app.editorCursorPos >= para && app.editorCursorPos < para + 200, "Down goes into the next block");
    keystroke(app, VK_UP);
    keystroke(app, VK_UP);
    check(app.editorCursorPos < para, "Up comes back");
    // From a block's last line Down goes on into the next one, over the
    // spacing between them, and Up comes back
    size_t ship = find(app, L"- [ ] Ship it");
    placeCaret(app, ship + 13);
    keystroke(app, VK_DOWN);
    size_t quotes = find(app, L"## Quotes and callouts");
    check(app.editorCursorPos >= quotes && app.editorCursorPos <= quotes + 22,
          "Down from a list's last item goes into the next block: " + std::to_string(app.editorCursorPos));
    keystroke(app, VK_UP);
    check(app.editorCursorPos >= ship && app.editorCursorPos <= ship + 13,
          "Up from a heading comes back to the list: " + std::to_string(app.editorCursorPos));
    frame(app, out / "live-pointer.png");
}

void menus(App& app) {
    // A right-click on the page puts the caret there and opens Live's menu
    pauseClicks();
    placeCaret(app, 0);
    D2D1_POINT_2F word{};
    check(renderedWord(app, L"Third item", word), "the list's third item is drawn");
    handleContextMenu(app, app.hwnd, at(word));
    size_t third = find(app, L"Third item");
    check(liveMenuOpen(app) && !app.editCtxOpen, "a right-click on the page opens Live's menu, not the source's");
    check(app.editorCursorPos >= third && app.editorCursorPos <= third + 10, "the caret goes where it was clicked");
    keystroke(app, VK_ESCAPE);
    check(!liveMenuOpen(app), "Esc closes it");
    // The find bar keeps its typing; the page does not take it
    const std::wstring before = app.editorText;
    keystroke(app, 'F', {true, false, false});
    check(app.showSearch && app.searchActive, "Ctrl+F opens the find bar in Live");
    typeText(app, L"tour");
    check(app.editorText == before, "typing in the find bar leaves the page alone");
    check(app.searchQuery.find(L"tour") != std::wstring::npos, "the find bar takes the typing");
    keystroke(app, VK_ESCAPE);
    // A caret moved elsewhere, by find or go to line, comes into view
    app.scrollY = app.targetScrollY = 0;
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = find(app, L"The last paragraph");
    app.editorHasSelection = false;
    paint(app);
    D2D1_POINT_2F caret = caretOnScreen(app);
    check(app.scrollY > 0 && caret.y > chromeTopHeight(app) && caret.y < app.height, "a caret moved elsewhere is brought into view");
}

// An open diagram keeps what it draws under its Markdown; the wheel
// scrolls the page; a file that is its own source stays in Source
void drawings(App& app, const fs::path& dir, const fs::path& out) {
    size_t flow = find(app, L"flowchart LR");
    placeCaret(app, flow + 4);
    float top = 0, bottom = 0, shift = 0;
    D2D1_POINT_2F top2{};
    float height = 0;
    livePageGeometry(app, top, bottom, shift);
    liveCaretClientPoint(app, top2, height);
    check(bottom - (top2.y + app.scrollY) > dpi(app, 120.0f), "an open diagram keeps its drawing under its Markdown");
    frame(app, out / "live-diagram.png");
    placeCaret(app, find(app, L"$$") + 3);
    frame(app, out / "live-math.png");
    placeCaret(app, find(app, L"return 0;") + 3);
    app.scrollY = app.targetScrollY = std::max(0.0f, app.scrollY - 160.0f);
    frame(app, out / "live-code.png");
    placeCaret(app, find(app, L"![Board mock]") + 3);
    frame(app, out / "live-picture.png");
    // The wheel scrolls the page
    app.scrollY = app.targetScrollY = 0;
    paint(app);
    handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), MAKELPARAM(500, 400));
    for (int i = 0; i < 30 && std::fabs(app.scrollY - app.targetScrollY) > 0.5f; ++i) app.scrollY = app.targetScrollY;
    check(app.targetScrollY > 0.0f, "the wheel scrolls the page");
    // A Mermaid file is its own source
    const fs::path mmd = dir / "Chart.mmd";
    std::ofstream(mmd, std::ios::binary) << "flowchart LR\n    A --> B\n";
    app.editorDirty = false;
    exitEditMode(app);
    check(openDocumentInViewer(app, mmd.wstring()), "the Mermaid file opens");
    enterEditMode(app);
    check(app.editMode && !editorLiveOn(app), "a Mermaid file is edited as its source");
    app.editorDirty = false;
    exitEditMode(app);
    check(openDocumentInViewer(app, (dir / "Live tour.md").wstring()), "the tour opens again");
    enterEditMode(app);
    check(editorLiveOn(app), "and Live is back for it");
}

void switching(App& app, const fs::path& out) {
    size_t caret = find(app, L"Second item") + 3;
    placeCaret(app, caret);
    keystroke(app, VK_OEM_2, {true, false, false});
    check(!editorLiveOn(app) && app.editMode, "Ctrl+/ shows the source");
    check(app.editorCursorPos == caret, "the caret stays on its character");
    check(loadSettings().editorLive == false, "the choice is saved");
    keystroke(app, VK_OEM_2, {true, false, false});
    check(editorLiveOn(app), "Ctrl+/ again shows Live");
    check(loadSettings().editorLive, "Live is saved as the choice");
    // The help overlay lists Live's keys with the editor's
    app.showHelp = true;
    app.helpAnimation = 1.0f;
    for (float scroll : {0.0f, 500.0f}) {
        app.helpScroll = scroll;
        check(saveFramePreview(app, out / ("live-help-" + std::to_string((int)scroll) + ".png"), [&] { drawLive(app); renderHelpOverlay(app); }),
              "the help overlay draws over Live");
    }
    app.showHelp = false;
    // The rail's pill switches too
    paint(app);
    D2D1_RECT_F source{};
    for (const auto& hit : app.editRailHits) {
        if (hit.second == 30) source = hit.first;
    }
    check(source.right > source.left, "the rail shows the pill");
    click(app, D2D1::Point2F((source.left + source.right) * 0.5f, (source.top + source.bottom) * 0.5f));
    check(!editorLiveOn(app), "the pill's Source side shows the source");
    app.editorLiveAnim = 0.0f;
    check(saveFramePreview(app, out / "live-source-rail.png", [&] { app.editRailAnim = 1.0f; renderEditRail(app); }),
          "the source's rail is drawn");
    setEditorLive(app, app.hwnd, true);
}

void openInEditor(App& app, const fs::path& path) {
    app.editorDirty = false;
    exitEditMode(app);
    check(openDocumentInViewer(app, path.wstring()), "opens: " + path.filename().string());
    enterEditMode(app);
}

// What a review of the first version found, each checked here
void reviewed(App& app, const fs::path& dir) {
    // A diagram file keeps the source's own chrome with Live remembered
    check(app.editorLive, "Live is the remembered mode");
    openInEditor(app, dir / "Chart.mmd");
    app.editorShowPreview = true;
    check(!editorLiveOn(app) && editSplitPreview(app), "a Mermaid file keeps its preview's chrome while Live is remembered");
    keystroke(app, VK_OEM_2, {true, false, false});
    check(!editorLiveOn(app) && app.editorLive, "Ctrl+/ leaves a Mermaid file in its source");

    const fs::path path = dir / "Fixes.md";
    std::ofstream(path, std::ios::binary)
        << "# Fixes\n\n| A | B |\n| --- | --- |\n\n- a\n\nText[^1].\n\n[^1]: Note.\n\nLast.\n\n```cpp\nint x;\n```\n";
    openInEditor(app, path);
    check(editorLiveOn(app), "the fixes open in Live");
    // A row made from the header goes past the delimiter row
    size_t header = find(app, L"| A | B |");
    placeCaret(app, header + 9);
    keystroke(app, VK_RETURN);
    check(find(app, L"| A | B |\n| --- | --- |\n|  |  |") != std::wstring::npos, "Enter on the header adds a row under the delimiter row");
    keystroke(app, 'Z', {true, false, false});
    placeCaret(app, header + 7);
    keystroke(app, VK_TAB);
    check(find(app, L"| A | B |\n| --- | --- |\n|  |  |") != std::wstring::npos, "Tab past the header's last cell adds a row under the delimiter row");
    keystroke(app, 'Z', {true, false, false});
    // Undo leaves the source's caret, not a stale selection
    size_t text = find(app, L"Text[^1]");
    app.editorSelStart = text;
    app.editorSelEnd = app.editorCursorPos = text + 4;
    app.editorHasSelection = true;
    keystroke(app, 'B', {true, false, false});
    check(find(app, L"**Text**") != std::wstring::npos, "Ctrl+B makes it bold");
    keystroke(app, 'Z', {true, false, false});
    check(find(app, L"**Text**") == std::wstring::npos && !app.editorHasSelection,
          "Ctrl+Z puts it back and leaves no stale selection");
    // A quote goes around a list item; it does not take its bullet
    placeCaret(app, find(app, L"- a") + 2);
    liveRailCommand(app, app.hwnd, 12);
    check(find(app, L"> - a") != std::wstring::npos, "the rail's quote goes around a list item");
    keystroke(app, 'Z', {true, false, false});
    // Footnotes gathered at the page's end stay the page's
    placeCaret(app, find(app, L"Last.") + 2);
    float top = 0, bottom = 0, shift = 0;
    livePageGeometry(app, top, bottom, shift);
    check(bottom - top < dpi(app, 120.0f), "a paragraph after a footnote opens alone, not with the footnotes");
    // Down at the very end stays there
    keystroke(app, VK_END, {true, false, false});
    size_t end = app.editorCursorPos;
    keystroke(app, VK_DOWN);
    check(app.editorCursorPos >= end, "Down at the end does not go back up: " + std::to_string(app.editorCursorPos));
    // In code, # and | are just characters
    placeCaret(app, find(app, L"int x;") + 6);
    keystroke(app, VK_RETURN);
    typeText(app, L"# ");
    keystroke(app, VK_BACK);
    check(find(app, L"int x;\n#\n```") != std::wstring::npos, "Backspace in code takes one character");
    keystroke(app, VK_BACK);
    typeText(app, L"|");
    keystroke(app, VK_HOME);
    keystroke(app, VK_TAB);
    check(find(app, L"int x;\n    |\n```") != std::wstring::npos, "Tab on a code line starting | indents it");
    // Alt+arrows leave the page alone while the find bar has the keys
    const std::wstring before = app.editorText;
    keystroke(app, 'F', {true, false, false});
    keystroke(app, VK_UP, {false, false, true});
    check(app.editorText == before, "Alt+Up with the find bar open moves no lines");
    keystroke(app, VK_ESCAPE);
    // A drag whose mouse goes elsewhere (Alt+Tab) ends with it
    pauseClicks();
    placeCaret(app, find(app, L"Last.") + 2);
    D2D1_POINT_2F p = caretOnScreen(app);
    handleMouseMove(app, app.hwnd, at(p));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(p));
    check(GetCapture() == app.hwnd, "a press on the page holds the mouse");
    size_t caret = app.editorCursorPos;
    ReleaseCapture();
    handleMouseMove(app, app.hwnd, at(D2D1::Point2F(p.x, p.y - dpi(app, 120.0f))));
    check(app.editorCursorPos == caret && !app.editorHasSelection, "a lost mouse ends the drag");
    handleMouseUp(app, app.hwnd, 0, at(p));
    // The popup keeps its row while its rows stay the same
    LiveText e;
    e.text = L"/";
    e.caret = e.anchor = 1;
    live::Host none;
    live::updatePopup(app, e, none);
    check(e.popup == LiveText::Popup::Slash && e.choices.size() > 3, "a / opens the menu");
    e.choice = 2;
    live::updatePopup(app, e, none);
    check(e.choice == 2, "the menu keeps its row while its rows stay the same");
    app.editorDirty = false;
}

// --- The menu (docs/live-editor.md, "The menu") -----------------------------------

const LiveText::MenuState& menuOf(const App& app) { return liveText(app).menu; }

std::wstring words(App& app, const char* key) { return tr(app, key); }

// A row's middle on the screen, found by its words in the menu's panel `panel`
bool rowPoint(App& app, size_t panel, const std::wstring& label, D2D1_POINT_2F& p) {
    paint(app);
    const auto& M = menuOf(app);
    if (!M.open || panel >= M.panels.size()) return false;
    const auto& P = M.panels[panel];
    for (size_t i = 0; i < P.rows.size() && i < P.rowBoxes.size(); ++i) {
        if (P.rows[i].label != label) continue;
        const D2D1_RECT_F& r = P.rowBoxes[i];
        p = D2D1::Point2F(documentViewportX(app) + (r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f);
        return true;
    }
    return false;
}

// Whether a panel of the menu has a row with these words, and whether it can be picked
bool hasRow(const App& app, size_t panel, const std::wstring& label, bool* enabled = nullptr) {
    const auto& M = menuOf(app);
    if (panel >= M.panels.size()) return false;
    for (const auto& row : M.panels[panel].rows) {
        if (row.label != label) continue;
        if (enabled) *enabled = row.enabled;
        return true;
    }
    return false;
}

void hover(App& app, D2D1_POINT_2F p) {
    handleMouseMove(app, app.hwnd, at(p));
    paint(app);
}

// A right-click as the window gets one: the pointer there, then the menu's request
void rightClick(App& app, D2D1_POINT_2F p) {
    paint(app);
    handleMouseMove(app, app.hwnd, at(p));
    handleContextMenu(app, app.hwnd, at(p));
    paint(app);
}

// The pointer onto a row by its words, then a click on it
void pick(App& app, size_t panel, const std::wstring& label) {
    D2D1_POINT_2F p{};
    if (!rowPoint(app, panel, label, p)) {
        check(false, "the menu has a row " + narrow(label));
        return;
    }
    hover(app, p);
    click(app, p);
}

// A right-click on words as the page draws them
bool menuOnWord(App& app, const std::wstring& word) {
    D2D1_POINT_2F p{};
    if (!renderedWord(app, word, p)) {
        check(false, "the page draws " + narrow(word));
        return false;
    }
    rightClick(app, p);
    return menuOf(app).open;
}

// The text around something, for a failure's message
std::string textNear(const App& app, const std::wstring& what) {
    size_t at = app.editorText.find(what);
    if (at == std::wstring::npos) at = 0;
    return narrow(app.editorText.substr(at, 160));
}

// The engine's edits behind the menu, on texts of their own
void menuEngine(App& app) {
    // Links: their words, addresses and names, without a title
    const std::wstring t = L"See [the page](https://a.b/c \"T\"), ![pic](x.png), [[Note|shown]] and https://a.b/d.";
    auto part = [&](size_t from, size_t to) { return t.substr(from, to - from); };
    live::LinkSpan l = live::linkAt(t, t.find(L"page"));
    check(l.kind == live::LinkSpan::Inline && part(l.addressFrom, l.addressTo) == L"https://a.b/c" &&
              part(l.textFrom, l.textTo) == L"the page",
          "a link's words and address, without its title");
    l = live::linkAt(t, t.find(L"pic"));
    check(l.kind == live::LinkSpan::Image && part(l.addressFrom, l.addressTo) == L"x.png", "a picture's address");
    l = live::linkAt(t, t.find(L"shown"));
    check(l.kind == live::LinkSpan::Wiki && part(l.addressFrom, l.addressTo) == L"Note" && part(l.textFrom, l.textTo) == L"shown",
          "a note's name and the words it shows");
    l = live::linkAt(t, t.find(L"a.b/d"));
    check(l.kind == live::LinkSpan::Address && part(l.addressFrom, l.addressTo) == L"https://a.b/d",
          "an address written out, without the full stop after it");
    check(live::linkAt(t, 1).kind == live::LinkSpan::None && live::linkAt(L"- [ ] task", 3).kind == live::LinkSpan::None,
          "plain words and a task's box are no link");
    check(live::linkAt(L"![a](b.png)", 11).kind == live::LinkSpan::Image, "just past a picture's ) is still on it");

    // Tables: a short row, its only row deleted, a row from the header
    LiveText e;
    e.text = L"| a | b |\n| --- | --- |\n| 1 |\n\nAfter";
    live::TableSpot s = live::tableAt(e.text, e.text.find(L"1"));
    check(s.in && s.index == 2 && s.columns == 2 && s.column == 0, "where a short row's cell is");
    live::editTable(e, e.text.find(L"1"), live::TABLE_COLUMN_RIGHT);
    check(e.text == L"| a |  | b |\n| --- | --- | --- |\n| 1 |  |\n\nAfter", "a column goes into a short row too: " + narrow(e.text));
    live::undo(e, false);
    check(e.text == L"| a | b |\n| --- | --- |\n| 1 |\n\nAfter", "and comes out with one undo");
    live::editTable(e, e.text.find(L"1"), live::TABLE_DELETE_ROW);
    check(e.text == L"| a | b |\n| --- | --- |\n\nAfter" && e.caret < 9 && e.caret == e.anchor,
          "the only row goes, and the caret goes up to the header: " + narrow(e.text));
    live::undo(e, false);
    live::editTable(e, 2, live::TABLE_ROW_BELOW);
    check(e.text == L"| a | b |\n| --- | --- |\n|  |  |\n| 1 |\n\nAfter", "a row from the header goes under the delimiter row: " + narrow(e.text));
    check(!live::editTable(e, 2, live::TABLE_ROW_ABOVE) && !live::editTable(e, 2, live::TABLE_DELETE_ROW),
          "nothing goes above the header, and it stays");
    live::editTable(e, 2, live::TABLE_ALIGN_RIGHT);
    check(e.text.find(L"| ---: | --- |") != std::wstring::npos && live::tableAt(e.text, 2).align == 3, "a column aligned right");
    live::editTable(e, 2, live::TABLE_ALIGN_RIGHT);
    check(e.text.find(L"| --- | --- |") != std::wstring::npos, "and the same again takes it off");

    // Turn into: numbers count at each indent, text leaves its quote, code gets fences
    e = LiveText{};
    e.text = L"one\n  two\nthree\n  four";
    e.caret = e.text.size();
    live::turnInto(e, live::BLOCK_NUMBERS);
    check(e.text == L"1. one\n  1. two\n2. three\n  1. four", "numbers count at each indent: " + narrow(e.text));
    e = LiveText{};
    e.text = L"> quoted\n> ## title";
    e.caret = e.text.size();
    live::turnInto(e, live::BLOCK_TEXT);
    check(e.text == L"quoted\ntitle", "text takes the quote and the heading off: " + narrow(e.text));
    live::turnInto(e, live::BLOCK_CODE);
    check(e.text == L"```\nquoted\ntitle\n```", "code goes between fences: " + narrow(e.text));
    const std::wstring fenced = e.text;
    e.caret = e.anchor = 6;
    live::turnInto(e, live::BLOCK_H1);
    check(e.text == fenced, "code's lines stay code");
    e = LiveText{};
    e.text = L"- [x] done\n- item\nplain";
    e.caret = e.text.size();
    live::turnInto(e, live::BLOCK_TASKS);
    check(e.text == L"- [x] done\n- [ ] item\n- [ ] plain", "tasks keep their ticks, the rest get boxes: " + narrow(e.text));
    check(live::blockAt(e.text, 0) == live::BLOCK_TASKS && live::blockAt(L"> a", 0) == live::BLOCK_QUOTE &&
              live::blockAt(L"#### a", 0) == live::BLOCK_OTHER,
          "what a line is");

    // The commands, with a host that follows links
    e = LiveText{};
    e.text = L"A [link](https://a.b) and [[Note]].";
    std::string followed;
    live::Host host;
    host.openLink = [&](const std::string& url) { followed = url; };
    const D2D1_RECT_F bounds = D2D1::RectF(0, 0, 800, 600);
    live::openMenu(app, e, 10, 10, 10, e.text.find(L"link"), bounds, host, false);
    check(e.menu.open && e.menu.canOpen, "a host that follows links gets Open");
    live::menuCommand(app, app.hwnd, e, live::CMD_OPEN_LINK, host);
    check(followed == "https://a.b" && !e.menu.open, "Open link hands the host the address, and the menu goes");
    live::openMenu(app, e, 10, 10, 10, e.text.find(L"Note"), bounds, host, false);
    live::menuCommand(app, app.hwnd, e, live::CMD_OPEN_LINK, host);
    check(followed == "wiki:Note", "Open note hands it the note's name");
    e.anchor = 0;
    e.caret = 1;
    live::menuCommand(app, app.hwnd, e, live::CMD_INSERT + live::INS_LINK, host);
    check(e.text.compare(0, 6, L"[A]() ") == 0, "Insert's Link over words makes them its words: " + narrow(e.text));
    e = LiveText{};
    e.text = L"| a |\n| --- |\n| 1 |\nafter";
    e.caret = e.anchor = 2;
    live::menuCommand(app, app.hwnd, e, live::CMD_INSERT + live::INS_DIVIDER, live::Host{});
    check(e.text.find(L"| 1 |\n\n---") != std::wstring::npos, "a divider asked for in a table goes under it: " + narrow(e.text));
    // Without a host's picker, Open or paste, the menu leaves them out
    e = LiveText{};
    e.text = L"[x](y)";
    live::openMenu(app, e, 10, 10, 10, 2, bounds, live::Host{}, false);
    bool open = false;
    for (const auto& row : e.menu.panels[0].rows) open = open || row.id == live::CMD_OPEN_LINK;
    check(!open, "a host that cannot follow links gets no Open");
    // The formats take words on one line, as the format bar does
    e = LiveText{};
    e.text = L"one\ntwo";
    e.caret = e.text.size();
    live::openMenu(app, e, 10, 10, 10, 1, bounds, live::Host{}, false);
    check(e.menu.panels[0].rows[0].kind == LiveText::MenuRow::Formats && !e.menu.panels[0].rows[0].enabled,
          "a selection over lines leaves the formats faint");
    live::closeMenu(e);
}

// The menu on the page, through the window's own pointer and key handlers
void contextMenu(App& app, const fs::path& dir, const fs::path& out) {
    openInEditor(app, dir / "Menu tour.md");
    check(editorLiveOn(app), "the menu tour opens in Live");
    const std::wstring original = app.editorText;
    const auto& M = menuOf(app);
    const auto npos = std::wstring::npos;
    auto undo = [&] { keystroke(app, 'Z', {true, false, false}); };
    // Each right-click starts from the title, every other block drawn by the page
    auto rest = [&] {
        if (liveMenuOpen(app)) liveCloseMenu(app);
        placeCaret(app, 2);
    };
    D2D1_POINT_2F p{};

    // A paragraph: the formats, the clipboard, Turn into, Insert and Select all
    rest();
    check(menuOnWord(app, L"format"), "a right-click on a paragraph opens the menu");
    size_t word = find(app, L"format");
    check(app.editorCursorPos >= word && app.editorCursorPos <= word + 6, "the caret goes to the word clicked");
    check(!app.editCtxOpen, "the source's insert menu stays shut in Live");
    check(!M.panels.empty() && M.panels[0].rows[0].kind == LiveText::MenuRow::Formats, "the formats come first");
    bool cut = true;
    check(hasRow(app, 0, words(app, "ectx.cut"), &cut) && !cut && hasRow(app, 0, words(app, "ectx.paste")), "Cut waits for a selection; Paste is there");
    check(hasRow(app, 0, words(app, "live.menu.turn_into")) && hasRow(app, 0, words(app, "live.menu.insert")) &&
              hasRow(app, 0, words(app, "live.menu.select_all")),
          "Turn into, Insert and Select all are there");
    check(!hasRow(app, 0, words(app, "live.menu.row_below")) && !hasRow(app, 0, words(app, "live.menu.open_link")),
          "a paragraph's menu has no table or link rows");
    frame(app, out / "menu-paragraph.png");
    check(rowPoint(app, 0, words(app, "live.menu.select_all"), p), "Select all is drawn");
    hover(app, p);
    check(M.panels[0].focus >= 0 && M.panels[0].rows[(size_t)M.panels[0].focus].label == words(app, "live.menu.select_all"),
          "the row under the pointer lights");
    // B makes the word under the pointer bold, and the menu goes
    const D2D1_RECT_F bold = M.panels[0].buttonBoxes[live::FMT_BOLD];
    const D2D1_POINT_2F b = D2D1::Point2F(documentViewportX(app) + (bold.left + bold.right) * 0.5f, (bold.top + bold.bottom) * 0.5f);
    hover(app, b);
    check(M.panels[0].button == live::FMT_BOLD, "the pointer lights B, with its name beside it");
    frame(app, out / "menu-bold-tip.png");
    click(app, b);
    check(find(app, L"**format**") != npos && !M.open, "the formats' B makes the word bold and closes the menu");
    undo();
    check(app.editorText == original, "undo takes it back");

    // Turn into opens beside its row with the line's kind ticked
    rest();
    menuOnWord(app, L"format");
    check(rowPoint(app, 0, words(app, "live.menu.turn_into"), p), "Turn into is drawn");
    hover(app, p);
    check(M.panels.size() == 2 && M.panels[1].sub == live::SUB_TURN, "Turn into opens beside its row");
    check(M.panels.size() == 2 && M.panels[1].rows[0].checked, "the paragraph is ticked as text");
    frame(app, out / "menu-turn-into.png");
    // On the way to it the pointer crosses Insert's row, and it stays open
    D2D1_POINT_2F insert{};
    rowPoint(app, 0, words(app, "live.menu.insert"), insert);
    const D2D1_RECT_F sub = M.panels.size() == 2 ? M.panels[1].box : D2D1_RECT_F{};
    hover(app, D2D1::Point2F(documentViewportX(app) + sub.left - dpi(app, 4.0f), insert.y));
    check(M.panels.size() == 2 && M.panels[1].sub == live::SUB_TURN, "crossing Insert's row on the way keeps Turn into open");
    hover(app, insert);
    check(M.panels.size() == 2 && M.panels[1].sub == live::SUB_INSERT, "resting on Insert opens its own");
    hover(app, p);
    pick(app, 1, words(app, "live.insert.h2"));
    check(find(app, L"## A paragraph to format") != npos && !M.open, "Turn into Heading 2 makes the paragraph a heading");
    undo();

    // Insert, Table, the grid: a table of four columns and two rows after the line
    rest();
    menuOnWord(app, L"The end");
    rowPoint(app, 0, words(app, "live.menu.insert"), p);
    hover(app, p);
    check(rowPoint(app, 1, words(app, "live.insert.table"), p), "Insert has Table");
    hover(app, p);
    check(M.panels.size() == 3 && M.panels[2].sub == live::SUB_TABLE, "Table opens its grid");
    const D2D1_RECT_F grid = M.panels.size() == 3 ? M.panels[2].rowBoxes[0] : D2D1_RECT_F{};
    const float step = dpi(app, 21.0f);
    D2D1_POINT_2F cell = D2D1::Point2F(documentViewportX(app) + grid.left + dpi(app, 8.0f) + step * 3 + dpi(app, 9.0f),
                                      grid.top + dpi(app, 8.0f) + step + dpi(app, 9.0f));
    hover(app, cell);
    check(M.cols == 4 && M.rows == 2, "the grid takes its size from the pointer: " + std::to_string(M.cols) + " x " + std::to_string(M.rows));
    frame(app, out / "menu-table-grid.png");
    click(app, cell);
    check(find(app, L"The end.\n\n| Column | Column | Column | Column |\n| --- | --- | --- | --- |\n|  |  |  |  |\n|  |  |  |  |") != npos,
          "the grid makes a table of its size after the line: " + textNear(app, L"The end."));
    undo();

    // A link: open it, copy its address, edit it, take it off
    rest();
    check(menuOnWord(app, L"link to Tinta"), "a right-click on a link opens the menu");
    check(hasRow(app, 0, words(app, "live.menu.open_link")) && hasRow(app, 0, words(app, "live.menu.copy_link")) &&
              hasRow(app, 0, words(app, "live.menu.edit_link")) && hasRow(app, 0, words(app, "live.menu.remove_link")),
          "a link's menu opens, copies, edits and removes it");
    frame(app, out / "menu-link.png");
    pick(app, 0, words(app, "live.menu.copy_link"));
    check(readClipboardText(app.hwnd) == L"https://example.com/tinta", "Copy link address copies the address without its title");
    rest();
    menuOnWord(app, L"link to Tinta");
    pick(app, 0, words(app, "live.menu.edit_link"));
    size_t address = find(app, L"https://example.com/tinta");
    check(app.editorHasSelection && std::min(app.editorSelStart, app.editorSelEnd) == address &&
              std::max(app.editorSelStart, app.editorSelEnd) == address + 25,
          "Edit link selects the address to type over");
    rest();
    menuOnWord(app, L"link to Tinta");
    pick(app, 0, words(app, "live.menu.remove_link"));
    check(find(app, L"with a link to Tinta, a [[Plans]]") != npos, "Remove link leaves its words: " + textNear(app, L"with a"));
    undo();
    // A note's link opens the note and comes off, with no address to copy
    rest();
    menuOnWord(app, L"Plans");
    check(hasRow(app, 0, words(app, "live.menu.open_note")) && hasRow(app, 0, words(app, "live.menu.remove_link")) &&
              !hasRow(app, 0, words(app, "live.menu.copy_link")),
          "a note's link opens the note");

    // A task ticks; a list item moves in and out
    rest();
    menuOnWord(app, L"A task to tick");
    bool outdent = true;
    check(hasRow(app, 0, words(app, "live.menu.outdent"), &outdent) && !outdent, "a top-level item has nowhere to go out to");
    pick(app, 0, words(app, "live.menu.done"));
    check(find(app, L"- [x] A task to tick") != npos, "Mark as done ticks the task");
    undo();
    rest();
    menuOnWord(app, L"Nested item");
    pick(app, 0, words(app, "live.menu.outdent"));
    check(find(app, L"\n- Nested item") != npos, "Outdent takes a nested item out a level");
    undo();

    // A table's rows and columns
    rest();
    check(menuOnWord(app, L"Midnight"), "a right-click in a table opens the menu");
    check(hasRow(app, 0, words(app, "live.menu.row_below")) && hasRow(app, 0, words(app, "live.menu.delete_column")) &&
              hasRow(app, 0, words(app, "live.menu.align")) && !hasRow(app, 0, words(app, "live.menu.turn_into")),
          "a table's menu has its rows and columns, and no Turn into");
    frame(app, out / "menu-table.png");
    pick(app, 0, words(app, "live.menu.row_below"));
    check(find(app, L"| Midnight | theme | 14 |\n|  |  |  |") != npos, "Insert row below adds an empty row: " + textNear(app, L"| Paper"));
    undo();
    rest();
    menuOnWord(app, L"Midnight");
    pick(app, 0, words(app, "live.menu.column_right"));
    check(find(app, L"| Name |  | Kind | Size |\n| --- | --- | :---: | ---: |\n| Paper |  | theme | 12 |\n| Midnight |  | theme | 14 |") != npos,
          "Insert column right adds a cell to every row: " + textNear(app, L"| Name"));
    undo();
    rest();
    menuOnWord(app, L"Midnight");
    pick(app, 0, words(app, "live.menu.delete_column"));
    check(find(app, L"| Kind | Size |\n| :---: | ---: |\n| theme | 12 |\n| theme | 14 |") != npos,
          "Delete column takes the cell from every row: " + textNear(app, L"| Kind"));
    undo();
    rest();
    menuOnWord(app, L"Midnight");
    pick(app, 0, words(app, "live.menu.delete_row"));
    check(find(app, L"Midnight") == npos && find(app, L"| Paper | theme | 12 |\n\n## Code") != npos, "Delete row takes the row");
    undo();
    // The header stays: nothing goes above it and it cannot be deleted; Align sets its column
    rest();
    menuOnWord(app, L"Name");
    bool above = true, remove = true;
    check(hasRow(app, 0, words(app, "live.menu.row_above"), &above) && !above &&
              hasRow(app, 0, words(app, "live.menu.delete_row"), &remove) && !remove,
          "the header has no row above it and stays");
    rowPoint(app, 0, words(app, "live.menu.align"), p);
    hover(app, p);
    frame(app, out / "menu-align.png");
    pick(app, 1, words(app, "live.menu.align_center"));
    check(find(app, L"| :---: | :---: | ---: |") != npos, "Align, Center centres the column: " + textNear(app, L"| Name"));
    undo();

    // Code, open at the caret: copy it; no formats, Turn into or Insert
    rest();
    placeCaret(app, find(app, L"answer") + 2);
    rightClick(app, caretOnScreen(app));
    check(M.open, "a right-click in code opens the menu");
    check(!M.panels.empty() && M.panels[0].rows[0].kind != LiveText::MenuRow::Formats &&
              !hasRow(app, 0, words(app, "live.menu.turn_into")) && !hasRow(app, 0, words(app, "live.menu.insert")) &&
              hasRow(app, 0, words(app, "live.menu.copy_code")),
          "code's menu copies the code and makes no Markdown");
    pick(app, 0, words(app, "live.menu.copy_code"));
    check(readClipboardText(app.hwnd) == L"const answer = 42;", "Copy code copies the fence's lines");

    // The keys: Shift+F10 opens it under the caret; arrows, Right and Enter make a diagram
    rest();
    placeCaret(app, find(app, L"The end.") + 8);
    keystroke(app, VK_F10, {false, true, false});
    check(M.open && M.keys && !M.panels.empty() && M.panels[0].focus >= 0, "Shift+F10 opens the menu at the caret with a row lit");
    keystroke(app, VK_END);
    keystroke(app, VK_UP);
    auto lit = [&](size_t panel) {
        if (panel >= M.panels.size() || M.panels[panel].focus < 0) return std::wstring();
        return M.panels[panel].rows[(size_t)M.panels[panel].focus].label;
    };
    check(lit(0) == words(app, "live.menu.insert"), "Up from the last row lights Insert: " + narrow(lit(0)));
    keystroke(app, VK_RIGHT);
    check(M.panels.size() == 2 && M.panels[1].focus == 0, "Right opens Insert with its first row lit");
    keystroke(app, VK_UP);
    keystroke(app, VK_UP);
    check(lit(1) == words(app, "ectx.diagram"), "Up goes round to Diagram: " + narrow(lit(1)));
    keystroke(app, VK_RIGHT);
    keystroke(app, VK_DOWN);
    check(lit(2) == words(app, "live.insert.sequence"), "Right opens the diagrams, Down lights the second");
    frame(app, out / "menu-keys.png");
    keystroke(app, VK_RETURN);
    check(find(app, L"sequenceDiagram") != npos && !M.open, "Enter makes a sequence diagram");
    undo();
    keystroke(app, VK_F10, {false, true, false});
    keystroke(app, VK_UP);
    check(!M.panels.empty() && M.panels[0].focus == 0 && M.panels[0].button == live::FMT_BOLD, "Up from the clipboard reaches the formats, B first");
    keystroke(app, VK_RIGHT);
    check(!M.panels.empty() && M.panels[0].button == live::FMT_ITALIC, "Right goes along the formats");
    frame(app, out / "menu-keys-formats.png");
    keystroke(app, VK_ESCAPE);
    check(!M.open, "Esc closes it");
    handleContextMenu(app, app.hwnd, MAKELPARAM(-1, -1));
    check(M.open && M.keys, "the menu key opens it at the caret");
    keystroke(app, 'S');
    check(lit(0) == words(app, "live.menu.select_all"), "a letter lights the row it starts");
    keystroke(app, VK_ESCAPE);
    // The menu key's own press opens it, and a second press puts it away
    keystroke(app, VK_APPS);
    check(M.open && M.keys, "the menu key's press opens it at the caret");
    keystroke(app, VK_APPS);
    check(!M.open, "the menu key again puts it away");

    // A click off the menu puts it away and leaves the caret
    rest();
    menuOnWord(app, L"format");
    size_t caret = app.editorCursorPos;
    click(app, D2D1::Point2F((float)app.width - 30.0f, chromeTopHeight(app) + 20.0f));
    check(!M.open && app.editorCursorPos == caret, "a click off the menu puts it away and leaves the caret");

    // The rail's table and diagram buttons open the grid and the diagrams beside it
    rest();
    placeCaret(app, find(app, L"The end.") + 8);
    D2D1_RECT_F tableButton{}, diagramButton{}, exportButton{};
    for (const auto& hit : app.editRailHits) {
        if (hit.second == 20) tableButton = hit.first;
        if (hit.second == 21) diagramButton = hit.first;
        if (hit.second == 23) exportButton = hit.first;
    }
    auto centre = [](const D2D1_RECT_F& r) { return D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f); };
    click(app, centre(tableButton));
    check(M.open && M.panels.size() == 1 && M.panels[0].sub == live::SUB_TABLE, "the rail's table button opens the grid beside it");
    paint(app);
    const D2D1_RECT_F railGrid = M.panels.empty() ? D2D1_RECT_F{} : M.panels[0].rowBoxes[0];
    cell = D2D1::Point2F(documentViewportX(app) + railGrid.left + dpi(app, 8.0f) + step + dpi(app, 9.0f),
                         railGrid.top + dpi(app, 8.0f) + step + dpi(app, 9.0f));
    hover(app, cell);
    frame(app, out / "menu-rail-grid.png");
    click(app, cell);
    check(find(app, L"The end.\n\n| Column | Column |\n| --- | --- |\n|  |  |\n|  |  |") != npos,
          "a cell of the rail's grid makes a table at the caret: " + textNear(app, L"The end."));
    undo();
    click(app, centre(diagramButton));
    check(M.open && M.panels.size() == 1 && M.panels[0].sub == live::SUB_DIAGRAM, "the rail's diagram button opens the diagrams");
    pick(app, 0, words(app, "live.insert.pie"));
    check(find(app, L"pie title") != npos, "a diagram picked from the rail is made at the caret");
    undo();
    // The export flyout, the source's, hangs over the page and still takes the
    // pointer (its button shows only where pandoc is installed)
    if (exportButton.right > exportButton.left) click(app, centre(exportButton));
    else editRailInvoke(app, app.hwnd, 23);
    check(app.editCtxOpen, "the rail's export button opens its flyout in Live");
    paint(app);
    if (!app.editCtxSubHits.empty()) {
        handleMouseMove(app, app.hwnd, at(centre(app.editCtxSubHits[0].first)));
        check(app.editCtxSubHover == app.editCtxSubHits[0].second, "its rows light under the pointer over the page");
    } else {
        check(false, "the export flyout draws its rows");
    }
    keystroke(app, VK_ESCAPE);
    check(!app.editCtxOpen, "Esc closes the flyout");

    // Two themes at two widths with Turn into open, inside the window
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            app.layoutDirty = true;
            rest();
            menuOnWord(app, L"format");
            rowPoint(app, 0, words(app, "live.menu.turn_into"), p);
            hover(app, p);
            check(M.panels.size() == 2, "Turn into opens at " + std::to_string(width) + " px");
            for (const auto& panel : M.panels) {
                check(panel.box.left >= 0 && documentViewportX(app) + panel.box.right <= (float)width && panel.box.bottom <= (float)app.height,
                      "the menu stays in the window at " + std::to_string(width) + " px");
            }
            frame(app, out / ("menu-theme" + std::to_string(theme) + "-" + std::to_string(width) + ".png"));
        }
    }
    applyTheme(app, 0);
    app.width = 1050;
    app.layoutDirty = true;
    rest();
    check(app.editorText == original, "with every pick undone the tour is as it was");
    app.editorDirty = false;
}

// A new note opens in Live on its empty page: the prompt to begin, with
// the Open and New sheet buttons of a new tab
void emptyNote(App& app, const fs::path& out) {
    tabOpenQuickNote(app, app.hwnd);
    check(app.editMode && app.editorText.empty() && editorLiveOn(app), "a new note opens in Live");
    paint(app);
    check(saveFramePreview(app, out / "live-empty.png", [&] { drawLive(app); renderQuickNoteEmptyState(app); }),
          "the empty note is drawn");
    check(app.quickNoteButtonRect.right > app.quickNoteButtonRect.left, "its page keeps the Open button");
    typeText(app, L"# Fresh\nA new note.");
    check(app.editorText == L"# Fresh\nA new note.", "typing on the empty page makes the note: " + narrow(app.editorText));
    frame(app, out / "live-new-note.png");
    app.editorDirty = false;
}

// A long document: switching to Live, a key typed in its middle, a frame,
// and leaving the block typed in, which lays the page out again
void performance(App& app, const fs::path& dir) {
    std::string big;
    for (int i = 0; i < 1500; ++i) {
        big += "## Section " + std::to_string(i) + "\n\nA paragraph with **bold**, *emphasis* and a [link](https://example.com/" +
               std::to_string(i) + ") that runs on for a while so it wraps at the window's width.\n\n- one\n- two\n\n";
        if (i % 10 == 0) big += "```cpp\nint x = " + std::to_string(i) + ";\n```\n\n";
    }
    const fs::path path = dir / "Big.md";
    std::ofstream(path, std::ios::binary) << big;
    app.editorDirty = false;
    exitEditMode(app);
    check(!app.editMode, "out of the editor");
    check(openDocumentInViewer(app, path.wstring()), "the long document opens");
    enterEditMode(app);
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    auto t0 = clock::now();
    setEditorLive(app, app.hwnd, true);
    paint(app);
    double toLive = ms(clock::now() - t0);
    placeCaret(app, find(app, L"## Section 750") + 14);
    t0 = clock::now();
    typeText(app, L" and more words");
    double perKey = ms(clock::now() - t0) / 15.0;
    t0 = clock::now();
    for (int i = 0; i < 10; ++i) paint(app);
    double perFrame = ms(clock::now() - t0) / 10.0;
    t0 = clock::now();
    keystroke(app, VK_DOWN);
    keystroke(app, VK_DOWN);
    paint(app);
    double leaving = ms(clock::now() - t0);
    check(find(app, L"## Section 750 and more words") != std::wstring::npos, "typing lands in the long document");
    std::cout << "Live, " << big.size() / 1024 << " KB: to Live " << (int)toLive << " ms, a key " << perKey
              << " ms, a frame " << perFrame << " ms, leaving the block " << (int)leaving << " ms\n";
    check(perKey < 40.0, "a key in a long document takes under 40 ms: " + std::to_string(perKey));
    check(perFrame < 40.0, "a frame of a long document takes under 40 ms: " + std::to_string(perFrame));
    app.editorDirty = false;
}

// A short document of lists, opened in Live
void openLists(App& app, const fs::path& dir) {
    const fs::path path = dir / "Lists.md";
    std::ofstream(path, std::ios::binary)
        << "# Lists\n\nMilk\nEggs\nBread\n\n1. First\n2. Second\n3. Third\n\n- Apples\n\n> Quoted\n\nThe end.\n";
    openInEditor(app, path);
    check(editorLiveOn(app), "the lists open in Live");
}

// The rail's numbered list in Live, on every selected line (#304, #306)
void railNumbers(App& app, const fs::path& dir, const fs::path& out) {
    const auto npos = std::wstring::npos;
    openLists(app, dir);
    const size_t milk = find(app, L"Milk\nEggs\nBread");
    app.editorSelStart = milk;
    app.editorSelEnd = app.editorCursorPos = milk + 15;
    app.editorHasSelection = true;
    paint(app);
    D2D1_RECT_F numbers{};
    for (const auto& hit : app.editRailHits) {
        if (hit.second == 13) numbers = hit.first;
    }
    check(numbers.right > numbers.left, "Live's rail shows the numbered list button");
    if (numbers.right > numbers.left) {
        click(app, D2D1::Point2F((numbers.left + numbers.right) * 0.5f, (numbers.top + numbers.bottom) * 0.5f));
        check(app.editRailHover == 13, "the pointer on it shows its name");
        // Straight onto the page, past the rail's edge in one move
        handleMouseMove(app, app.hwnd, at(D2D1::Point2F(documentViewportX(app) + 200.0f, 300.0f)));
        check(app.editRailHover == 0, "and on Live's page leaves no name behind");
    }
    check(find(app, L"\n1. Milk\n2. Eggs\n3. Bread\n") != npos, "it numbers every selected line in Live: " + textNear(app, L"Milk"));
    frame(app, out / "live-rail-numbers.png");
    keystroke(app, 'Z', {true, false, false});
    check(find(app, L"\nMilk\nEggs\nBread\n") != npos, "one Ctrl+Z takes the numbers back");
    app.editorDirty = false;
}

// Lines pasted on a list item in Live are its items, as Enter makes them (#305)
void listPaste(App& app, const fs::path& dir, const fs::path& out) {
    const auto npos = std::wstring::npos;
    const KeyMods ctrl{true, false, false};
    openLists(app, dir);
    // Enter on the numbered list's last item, then a paste: the lines count on,
    // and the numbers below follow
    bool copied = false;
    for (int attempt = 0; attempt < 20 && !(copied = copyToClipboard(app.hwnd, L"Milk\nEggs\n")); ++attempt) Sleep(5);
    check(copied, "the test's lines reach the clipboard");
    placeCaret(app, find(app, L"2. Second") + 9);
    keystroke(app, VK_RETURN);
    keystroke(app, 'V', ctrl);
    check(find(app, L"2. Second\n3. Milk\n4. Eggs\n5. Third") != npos,
          "lines pasted on a numbered item count on, and so do the numbers below: " + textNear(app, L"Second"));
    keystroke(app, 'Z', ctrl);
    check(find(app, L"2. Second\n3. \n4. Third") != npos, "one Ctrl+Z takes the paste back, numbers and all");
    keystroke(app, 'Z', ctrl);
    // In a quote each line is quoted; Ctrl+Shift+V pastes the text as it is
    placeCaret(app, find(app, L"> Quoted") + 8);
    keystroke(app, VK_RETURN);
    keystroke(app, 'V', ctrl);
    check(find(app, L"> Quoted\n> Milk\n> Eggs\n\nThe end.") != npos, "lines pasted in a quote are quoted: " + textNear(app, L"Quoted"));
    placeCaret(app, find(app, L"- Apples") + 8);
    keystroke(app, VK_RETURN);
    keystroke(app, 'V', {true, true, false});
    check(find(app, L"- Apples\n- Milk\nEggs\n") != npos, "Ctrl+Shift+V pastes them as they are: " + textNear(app, L"Apples"));
    frame(app, out / "live-list-paste.png");
    app.editorDirty = false;
}

// A finger held on the page (#286) takes the word under it, as a double-click
// does, and grows the selection by words as it slides, keeping that word
// whichever way it goes; Live's menu opens where it lifts. Live's page is the
// document viewport, but the split preview's own hold (#301) stays out of it
void heldFinger(App& app, const fs::path& doc) {
    openInEditor(app, doc);
    app.scrollY = app.targetScrollY = 0;
    // With the caret in the paragraph Live shows it as typed, as it does
    // while a finger selects in it: the words are found as drawn then
    const size_t word = find(app, L"paragraph");
    auto spot = [&](size_t pos) {
        placeCaret(app, pos);
        D2D1_POINT_2F q = caretOnScreen(app);
        return D2D1::Point2F(q.x + dpi(app, 2.0f), q.y);
    };
    const D2D1_POINT_2F w = spot(word + 3), after = spot(find(app, L"with") + 1), before = spot(find(app, L"A paragraph"));
    placeCaret(app, word + 3);
    auto pointOf = [](D2D1_POINT_2F q) { return POINT{(LONG)std::lround(q.x), (LONG)std::lround(q.y)}; };
    const POINT p = pointOf(w);
    PanelList list;
    check(touchTargetAt(app, p, list) == TouchTarget::Document, "a finger on Live's page drives the page");
    const LiveText& L = liveText(app);
    auto selected = [&] {
        size_t a = std::min(L.anchor, L.caret), b = std::max(L.anchor, L.caret);
        return L.text.substr(a, b - a);
    };
    pauseClicks();
    double ms = touchClockMs();
    check(touchDown(app, app.hwnd, 90, p, ms), "the held finger is taken");
    touchHold(app, app.hwnd);
    check(!app.selecting && !app.hasSelection, "a finger held in Live leaves the preview's selection alone");
    check(selected() == L"paragraph", "a finger held in Live takes its word: " + narrow(selected()));
    touchMove(app, app.hwnd, 90, pointOf(after), ms += 100);
    check(selected() == L"paragraph with", "sliding on takes whole words: " + narrow(selected()));
    touchMove(app, app.hwnd, 90, pointOf(before), ms += 100);
    check(selected() == L"A paragraph", "sliding back keeps the word held first: " + narrow(selected()));
    touchUp(app, app.hwnd, 90, pointOf(before), ms += 100, false);
    check(liveMenuOpen(app) && !app.showContextMenu && selected() == L"A paragraph",
          "lifting the finger opens Live's menu over the words");
    // While the menu is up a finger works it as the mouse does
    check(touchTargetAt(app, p, list) == TouchTarget::None && !touchDown(app, app.hwnd, 91, p, ms += 500),
          "a finger on Live's menu goes to the mouse, not to the page under it");
    liveCloseMenu(app);
    // A hold cut short keeps its word without the menu, and no drag lingers
    pauseClicks();
    touchDown(app, app.hwnd, 92, p, ms += 1000);
    touchHold(app, app.hwnd);
    touchCancel(app, app.hwnd);
    handleMouseMove(app, app.hwnd, at(after));
    check(selected() == L"paragraph" && !liveMenuOpen(app),
          "a hold cut short keeps its word, and the pointer drags nothing after it");
    // A mouse's double-click drawn back keeps its word too
    pauseClicks();
    handleMouseMove(app, app.hwnd, at(w));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(w));
    handleMouseUp(app, app.hwnd, 0, at(w));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(w));
    handleMouseMove(app, app.hwnd, at(before));
    handleMouseUp(app, app.hwnd, 0, at(before));
    check(selected() == L"A paragraph", "a double-click drawn back keeps its word: " + narrow(selected()));
}

// A frame as the window paints it (main_d2d.cpp), once the layout's chunks
// have landed: the scrollbar's extent, the source's scroll sync, the scroll
// held to the page, then Live's page, or the source, which measures the
// source's height as it draws
void windowFrame(App& app) {
    if (app.layoutDirty || !app.layoutComplete) ensureLayoutComplete(app);
    const bool live = editorLiveOn(app);
    app.verticalScrollbarVisible = app.contentHeight > app.height;
    app.scrollbarContentHeight = live ? std::max(app.contentHeight, livePageHeight(app)) : app.contentHeight;
    syncPreviewToEditor(app);
    const float page = live ? livePageHeight(app) : app.contentHeight;
    app.scrollY = std::clamp(app.scrollY, 0.0f, std::max(0.0f, page - (float)app.height));
    if (live) {
        paint(app);
        return;
    }
    app.renderTarget->BeginDraw();
    renderEditor(app, editorPaneWidth(app));
    app.renderTarget->EndDraw();
}

HCURSOR cursorAfterMove(App& app, D2D1_POINT_2F p) {
    SetCursor(nullptr);
    handleMouseMove(app, app.hwnd, at(p));
    return GetCursor();
}

void wheelDown(App& app) {
    handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), MAKELPARAM(500, 400));
}

// Live's page scrolls as the reader's does (#307): its scrollbar drags and
// takes presses on the track, leaving the caret where it is, and after a
// visit to the source the wheel and the arrow keys still move the page,
// which stays where they leave it, frame after frame
void scrolling(App& app, const fs::path& dir, const fs::path& out) {
    const HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    const HCURSOR beam = LoadCursor(nullptr, IDC_IBEAM);
    const KeyMods ctrl{true, false, false};
    openInEditor(app, dir / "Long read.md");
    check(editorLiveOn(app), "the long read opens in Live");
    pauseClicks();
    placeCaret(app, 0);
    app.scrollY = app.targetScrollY = 0;
    windowFrame(app);
    check(app.verticalScrollbarVisible && livePageHeight(app) > app.height * 2.5f,
          "the long read is several screens long in Live: " + std::to_string((int)livePageHeight(app)));
    const size_t caret = app.editorCursorPos;
    auto onScreen = [&](const std::string& what) {
        D2D1_POINT_2F p = caretOnScreen(app);
        check(p.y > chromeTopHeight(app) && p.y < app.height,
              what + ": the caret at " + std::to_string((int)p.y) + ", the page at " + std::to_string((int)app.scrollY));
    };

    // The thumb: an arrow over it, lit, and a press takes it, not the caret
    const float barX = documentViewportX(app) + documentViewportWidth(app) - dpi(app, 7.0f);
    ScrollbarGeometry bar = documentScrollbarV(app);
    const D2D1_POINT_2F thumb = D2D1::Point2F(barX, bar.thumbStart + bar.thumbLength * 0.5f);
    check(cursorAfterMove(app, thumb) == arrow && app.scrollbarHovered, "an arrow over Live's scrollbar, and its thumb lights up");
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(thumb));
    check(app.scrollbarDragging && GetCapture() == app.hwnd, "a press on Live's scrollbar takes its thumb");
    const D2D1_POINT_2F dragTo = D2D1::Point2F(barX, thumb.y + 200.0f);
    check(cursorAfterMove(app, dragTo) == arrow, "an arrow while the thumb is dragged");
    check(std::fabs(documentScrollbarV(app).thumbStart - (bar.thumbStart + 200.0f)) < 1.0f, "the thumb moves with the pointer");
    windowFrame(app);
    const float dragged = app.scrollY;
    check(dragged > 0.0f, "dragging the thumb scrolls Live's page");
    handleMouseUp(app, app.hwnd, 0, at(dragTo));
    windowFrame(app);
    check(!app.scrollbarDragging && GetCapture() != app.hwnd, "the release lets the thumb go");
    check(app.editorCursorPos == caret && !app.editorHasSelection, "the drag leaves the caret and selects nothing");
    check(std::fabs(app.scrollY - dragged) < 0.5f, "the page stays where the thumb left it");
    // A press on the track below the thumb takes the thumb there
    bar = documentScrollbarV(app);
    const float trackEnd = bar.trackStart + bar.trackLength;
    const D2D1_POINT_2F track = D2D1::Point2F(barX, (bar.thumbStart + bar.thumbLength + trackEnd) * 0.5f);
    handleMouseMove(app, app.hwnd, at(track));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(track));
    handleMouseUp(app, app.hwnd, 0, at(track));
    windowFrame(app);
    check(app.scrollY > dragged + 1.0f && app.editorCursorPos == caret, "a press on the track moves the page, not the caret");
    // Beside the scrollbar the page has its beam again, and the thumb dims
    check(cursorAfterMove(app, D2D1::Point2F(documentViewportX(app) + documentViewportWidth(app) * 0.5f, 400.0f)) == beam &&
              !app.scrollbarHovered,
          "beside the scrollbar the page's beam is back, and the thumb dims");
    frame(app, out / "live-scrolled.png");

    // A visit to the source measures the source's height
    placeCaret(app, 0);
    app.scrollY = app.targetScrollY = 0;
    keystroke(app, VK_OEM_2, ctrl);
    check(!editorLiveOn(app), "Ctrl+/ shows the source");
    windowFrame(app);
    check(app.editorContentHeight > app.height, "the long read's source is taller than the window");
    keystroke(app, VK_OEM_2, ctrl);
    check(editorLiveOn(app), "Ctrl+/ brings Live back");
    windowFrame(app);
    // The wheel scrolls, and the next frames leave the page there
    for (int i = 0; i < 3; ++i) wheelDown(app);
    windowFrame(app);
    windowFrame(app);
    check(app.scrollY > dpi(app, 100.0f), "after the source, the wheel scrolls Live's page: " + std::to_string(app.scrollY));
    // Down to the end: the page follows the caret and stays with it
    placeCaret(app, 0);
    app.scrollY = app.targetScrollY = 0;
    windowFrame(app);
    const size_t lastLine = find(app, L"The last line");
    for (int i = 0; i < 400 && app.editorCursorPos < lastLine; ++i) {
        keystroke(app, VK_DOWN);
        windowFrame(app);
    }
    windowFrame(app);
    windowFrame(app);
    check(app.editorCursorPos >= lastLine, "Down walks to the long read's last line: " + std::to_string(app.editorCursorPos));
    check(app.scrollY > app.height, "after the source, Down takes the page to the end");
    onScreen("the page stays with the caret, frame after frame");
    for (int i = 0; i < 3; ++i) {
        keystroke(app, VK_UP);
        windowFrame(app);
    }
    windowFrame(app);
    onScreen("Up from the end keeps the caret on the screen");
    // With the diagram open the page is taller: the thumb takes it to its
    // new end, and the wheel there stays at it
    placeCaret(app, find(app, L"Top --> Middle"));
    windowFrame(app);
    float top = 0, bottom = 0, shift = 0;
    livePageGeometry(app, top, bottom, shift);
    check(shift > 0.0f && livePageHeight(app) > app.contentHeight, "the open diagram makes the page taller");
    bar = documentScrollbarV(app);
    const D2D1_POINT_2F grip = D2D1::Point2F(barX, bar.thumbStart + bar.thumbLength * 0.5f);
    const D2D1_POINT_2F below = D2D1::Point2F(barX, (float)app.height + 300.0f);
    handleMouseMove(app, app.hwnd, at(grip));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(grip));
    handleMouseMove(app, app.hwnd, at(below));
    handleMouseUp(app, app.hwnd, 0, at(below));
    windowFrame(app);
    const float end = std::max(0.0f, livePageHeight(app) - app.height);
    check(std::fabs(app.scrollY - end) < 1.0f, "the thumb takes the page to its end with the diagram open");
    wheelDown(app);
    windowFrame(app);
    check(std::fabs(app.scrollY - end) < 1.0f,
          "the wheel at that end stays there: " + std::to_string(app.scrollY) + " vs " + std::to_string(end));
    frame(app, out / "live-scrolled-end.png");
    app.editorDirty = false;
}

// Live notices every document parsed past it, another file or a reload,
// whatever address the new parse is given. It knew the parse it had laid
// out by its address alone, and the next document's could be given the
// freed one's: the page then kept the old document's text, and drew
// everything from the caret to the first difference as being typed in.
// The menu's, the footnotes' and a key's timing checks above failed now
// and then for it. Each switch is a fresh chance at the old address: with
// the address alone about one in thirty went unnoticed, so 250 switches
// all but always find one.
void documentsSwitched(App& app, const fs::path& dir) {
    const fs::path docs[] = {dir / "Switch A.md", dir / "Switch B.md"};
    std::ofstream(docs[0], std::ios::binary) << "# Alpha\n\nOne.\n\nTwo.\n\nThree.\n\nFour.\n";
    std::ofstream(docs[1], std::ios::binary) << "# Beta\n\nFive.\n\nSix.\n\nSeven.\n\nEight.\n";
    int missed = 0;
    float tallest = 0;
    for (int i = 0; i < 250 && !missed; ++i) {
        openInEditor(app, docs[i % 2]);
        placeCaret(app, 2);
        float top = 0, bottom = 0, shift = 0;
        livePageGeometry(app, top, bottom, shift);
        tallest = std::max(tallest, bottom - top);
        if (bottom - top > dpi(app, 90.0f)) missed = i + 1;
    }
    check(!missed, "Live notices each document opened past it, and opens the title alone: switch " + std::to_string(missed) +
                       ", " + std::to_string((int)tallest) + " DIP open");
    app.editorDirty = false;
}

bool closeTo(D2D1_COLOR_F a, D2D1_COLOR_F b, float within = 0.002f) {
    return std::fabs(a.r - b.r) <= within && std::fabs(a.g - b.g) <= within && std::fabs(a.b - b.b) <= within;
}

// The pixels `draw` leaves, drawn into a bitmap the window's size
std::vector<D2D1_COLOR_F> pixelsAfter(App& app, const std::function<void()>& draw) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Bitmap1> target, readable;
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    const D2D1_SIZE_U size = D2D1::SizeU((UINT)app.width, (UINT)app.height);
    if (FAILED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc))) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target)) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
                                &readable))) {
        return {};
    }
    ComPtr<ID2D1Image> previous;
    dc->GetTarget(&previous);
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    draw();
    dc->EndDraw();
    dc->SetTarget(previous.Get());
    D2D1_MAPPED_RECT mapped{};
    std::vector<D2D1_COLOR_F> pixels;
    if (FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr)) || FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
        return pixels;
    for (UINT y = 0; y < size.height; ++y) {
        const BYTE* row = mapped.bits + y * mapped.pitch;
        for (UINT x = 0; x < size.width; ++x)
            pixels.push_back(D2D1::ColorF(row[x * 4 + 2] / 255.0f, row[x * 4 + 1] / 255.0f, row[x * 4] / 255.0f));
    }
    readable->Unmap();
    return pixels;
}

// #310: the theme's bold and italic colours, its selection and its rule, in
// the text Live and the sheets type in. A line's spans do not nest there,
// so ***words*** read as bold until the caret leaves them for the page
void themeColours(App& app) {
    using Microsoft::WRL::ComPtr;
    applyTheme(app, 0);
    D2DTheme& t = app.theme;
    t.boldText = hexColor(0xAA2200);
    t.italicText = hexColor(0x007755);
    t.boldItalicText = hexColor(0x7722AA);
    t.selection = hexColor(0xCC0000);
    t.rule = hexColor(0x00AA44);
    LiveText e;
    e.text = L"Words **Bold_live**, __Under_live__, *Italic_live*, _Lower_live_, [Link_live](x), `Code_live`, ==Marked_live== "
             L"and plain_live\n\n---\n\nAfter the rule\n";
    e.anchor = e.text.find(L"plain_live");
    e.caret = e.anchor + 10;
    const live::Look look = live::pageLook(app);
    live::LayoutCache cache;
    IDWriteTextLayout* layout = live::layoutOf(app, e, 0, e.text.size(), 900.0f, look, cache, (size_t)-1);
    check(layout != nullptr, "#310: Live lays the text out");
    if (!layout) return;
    auto colorOf = [&](const wchar_t* word) {
        ComPtr<IUnknown> effect;
        layout->GetDrawingEffect((UINT32)e.text.find(word), &effect, nullptr);
        ComPtr<ID2D1SolidColorBrush> brush;
        if (effect) effect.As(&brush);
        return brush ? brush->GetColor() : look.text;
    };
    check(closeTo(colorOf(L"Bold_live"), *t.boldText) && closeTo(colorOf(L"Under_live"), *t.boldText), "#310: Live draws bold in bold=");
    check(closeTo(colorOf(L"Italic_live"), *t.italicText) && closeTo(colorOf(L"Lower_live"), *t.italicText),
          "#310: Live draws italic in italic=");
    check(closeTo(colorOf(L"Link_live"), look.link) && closeTo(colorOf(L"Code_live"), look.code) &&
              closeTo(colorOf(L"Marked_live"), look.text) && closeTo(colorOf(L"plain_live"), look.text),
          "#310: Live's links, code, marks and plain words keep their colours");

    const std::vector<D2D1_COLOR_F> pixels = pixelsAfter(app, [&] {
        app.renderTarget->Clear(t.background);
        live::draw(app, e, layout, cache, 0, look, false, 1.0f);
    });
    // The selection: the top of its line, above the letters
    DWRITE_HIT_TEST_METRICS hit{};
    UINT32 hits = 0;
    layout->HitTestTextRange((UINT32)e.anchor, 10, 0, 0, &hit, 1, &hits);
    const size_t sx = (size_t)(hit.left + hit.width / 2), sy = (size_t)(hit.top + 2);
    D2D1_COLOR_F blend = t.background;
    for (float* c : {&blend.r, &blend.g, &blend.b}) *c *= 0.72f;
    blend.r += 0.8f * 0.28f;
    check(pixels.size() == (size_t)app.width * app.height && closeTo(pixels[sy * app.width + sx], blend, 0.02f),
          "#310: Live's selection is the theme's selection=, as translucent as before");
    // The rule: green in its line, not the text's faint grey
    float x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS rule{};
    layout->HitTestTextPosition((UINT32)e.text.find(L"---"), FALSE, &x, &y, &rule);
    float green = 0;
    for (size_t row = (size_t)rule.top; row < (size_t)(rule.top + rule.height) && !pixels.empty(); ++row) {
        const D2D1_COLOR_F& p = pixels[row * app.width + (size_t)(rule.left + 40)];
        green = std::max(green, p.g - p.r);
    }
    check(green > 0.2f, "#310: Live draws a rule in hr=");
    applyTheme(app, 0);
}

}  // namespace

int runLiveTests(const char* fixtureDir) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Live tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    g_liveApp = &app;
    g_staticProc = (WNDPROC)SetWindowLongPtrW(app.hwnd, GWLP_WNDPROC, (LONG_PTR)captureProc);
    // The user's clipboard comes back as it was
    auto clipboard = std::make_unique<ClipboardSnapshot>(app.hwnd);
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    const fs::path dir = fs::current_path() / "live";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::copy(fixtureDir, dir, fs::copy_options::recursive, ec);
    check(!ec, "the fixture copies");
    const fs::path out = fs::current_path() / "frames";
    fs::create_directories(out, ec);
    const fs::path doc = dir / "Live tour.md";

    tabsInit(app);
    applyTheme(app, 0);
    check(openDocumentInViewer(app, doc.wstring()), "the fixture opens");
    enterEditMode(app);
    check(app.editMode, "edit mode");
    setEditorLive(app, app.hwnd, true);
    opening(app, out);
    typing(app, out);
    formatting(app, out);
    slashMenu(app, out);
    names(app, out);
    pointer(app, out);
    menus(app);
    switching(app, out);
    drawings(app, dir, out);
    reviewed(app, dir);
    railNumbers(app, dir, out);
    listPaste(app, dir, out);
    menuEngine(app);
    contextMenu(app, dir, out);
    heldFinger(app, doc);
    scrolling(app, dir, out);
    documentsSwitched(app, dir);
    themeColours(app);

    // Two themes at two widths, on the tour
    openInEditor(app, doc);
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            app.layoutDirty = true;
            placeCaret(app, find(app, L"## Code") + 4);
            frame(app, out / ("live-theme" + std::to_string(theme) + "-" + std::to_string(width) + ".png"));
        }
    }
    app.width = 1050;
    app.layoutDirty = true;
    emptyNote(app, out);
    performance(app, dir);

    app.editorDirty = false;
    clipboard.reset();
    g_liveApp = nullptr;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Live: " << failures << " failures\n";
    return failures ? 1 : 0;
}
