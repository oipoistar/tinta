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
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

namespace {
namespace fs = std::filesystem;

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
    // A right-click on the page puts the caret there and opens the insert menu
    pauseClicks();
    placeCaret(app, 0);
    D2D1_POINT_2F word{};
    check(renderedWord(app, L"Third item", word), "the list's third item is drawn");
    handleContextMenu(app, app.hwnd, at(word));
    size_t third = find(app, L"Third item");
    check(app.editCtxOpen, "a right-click on the page opens the insert menu");
    check(app.editorCursorPos >= third && app.editorCursorPos <= third + 10, "the caret goes where it was clicked");
    keystroke(app, VK_ESCAPE);
    check(!app.editCtxOpen, "Esc closes it");
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

}  // namespace

int runLiveTests(const char* fixtureDir) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Live tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
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

    // Two themes at two widths
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
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Live: " << failures << " failures\n";
    return failures ? 1 : 0;
}
