#include "d2d_init.h"
#include "drafts.h"
#include "editor.h"
#include "editrail.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "live_text.h"
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"
#include "clipboard_snapshot.h"
#include "frame_preview.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

// Editor conveniences (#251), the rail's list marks and lines pasted on a
// list (#304, #305, #306), the word count (#240) and draft recovery after a
// close without asking (#252), driven through the real handlers
namespace {
int failures = 0, checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

struct Modifiers { bool ctrl = false, shift = false, alt = false; };

// The handlers read modifiers with GetKeyState, so set the thread's state
void withKeys(Modifiers mods, const std::function<void()>& action) {
    BYTE original[256], pressed[256];
    GetKeyboardState(original);
    memcpy(pressed, original, sizeof pressed);
    pressed[VK_CONTROL] = mods.ctrl ? 0x80 : 0;
    pressed[VK_SHIFT] = mods.shift ? 0x80 : 0;
    pressed[VK_MENU] = mods.alt ? 0x80 : 0;
    SetKeyboardState(pressed);
    action();
    SetKeyboardState(original);
}
void key(App& app, unsigned vk, Modifiers mods = {}) {
    withKeys(mods, [&] { handleKeyDown(app, app.hwnd, vk); });
}
// Alt+arrows arrive as WM_SYSKEYDOWN, which the window procedure hands to
// editorAltArrowKey
bool altArrow(App& app, unsigned vk) {
    bool handled = false;
    withKeys({false, false, true}, [&] { handled = editorAltArrowKey(app, app.hwnd, vk); });
    return handled;
}
void type(App& app, const std::wstring& text) {
    typeKeys(app, text);
}
void setText(App& app, const std::wstring& text, size_t caret) {
    editorClearExtraCarets(app);
    app.editorText = text;
    rebuildLineStarts(app);
    app.undoStack.clear();
    app.redoStack.clear();
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = caret;
    app.editorHasSelection = false;
    app.editorDesiredCol = -1;
}
void select(App& app, size_t anchor, size_t caret) {
    app.editorSelStart = anchor;
    app.editorSelEnd = app.editorCursorPos = caret;
    app.editorHasSelection = anchor != caret;
}
void addCaret(App& app, size_t anchor, size_t pos) {
    App::EditorCaret caret;
    caret.anchor = anchor;
    caret.pos = pos;
    caret.hasSelection = anchor != pos;
    app.editorExtraCarets.push_back(caret);
}
std::vector<size_t> carets(const App& app) {
    std::vector<size_t> positions{app.editorCursorPos};
    for (const auto& caret : app.editorExtraCarets) positions.push_back(caret.pos);
    std::sort(positions.begin(), positions.end());
    return positions;
}

void moveLines(App& app) {
    setText(app, L"one\ntwo\nthree", 5);
    check(altArrow(app, VK_UP) && app.editorText == L"two\none\nthree" && app.editorCursorPos == 1,
          "Alt+Up swaps the caret line with the line above and keeps the column");
    check(altArrow(app, VK_UP) && app.editorText == L"two\none\nthree" && app.undoStack.size() == 1,
          "Alt+Up on the first line changes nothing");
    key(app, 'Z', {true});
    check(app.editorText == L"one\ntwo\nthree", "one Ctrl+Z puts the moved line back");
    setText(app, L"one\ntwo\nthree", 9);
    altArrow(app, VK_DOWN);
    check(app.editorText == L"one\ntwo\nthree" && app.undoStack.empty(),
          "Alt+Down on the last line changes nothing");
    setText(app, L"a\nb", 3);
    altArrow(app, VK_UP);
    check(app.editorText == L"b\na" && app.editorCursorPos == 1,
          "the last line moves up although it has no newline");
    setText(app, L"one\ntwo\nthree", 0);
    select(app, 0, 5);
    altArrow(app, VK_DOWN);
    check(app.editorText == L"three\none\ntwo" && app.editorHasSelection &&
          app.editorSelStart == 6 && app.editorSelEnd == 11,
          "Alt+Down moves every selected line, the selection riding along");
    setText(app, L"one\ntwo\nthree", 0);
    select(app, 0, 4);
    altArrow(app, VK_DOWN);
    check(app.editorText == L"two\none\nthree",
          "a selection ending at a line start leaves that line in place");
    setText(app, L"1\n2\n3\n4\n5", 0);
    addCaret(app, 4, 4);
    altArrow(app, VK_DOWN);
    check(app.editorText == L"2\n1\n4\n3\n5" && carets(app) == std::vector<size_t>({2, 6}),
          "every caret's line moves down");
    key(app, 'Z', {true});
    check(app.editorText == L"1\n2\n3\n4\n5", "one Ctrl+Z restores every moved line");
    setText(app, L"1\n2\n3\n4", 2);
    addCaret(app, 4, 4);
    altArrow(app, VK_UP);
    check(app.editorText == L"2\n3\n1\n4", "neighbouring caret lines move as one block");
    app.editorReadingPreview = true;
    check(!altArrow(app, VK_UP), "Alt+Up belongs to the source editor only");
    app.editorReadingPreview = false;
}

void wrapSelection(App& app) {
    const std::pair<wchar_t, wchar_t> pairs[] = {
        {L'"', L'"'}, {L'\'', L'\''}, {L'`', L'`'}, {L'*', L'*'}, {L'~', L'~'}, {L'^', L'^'},
        {L'=', L'='}, {L':', L':'}, {L'(', L')'}, {L'[', L']'}, {L'{', L'}'}};
    for (const auto& [open, close] : pairs) {
        setText(app, L"a word here", 0);
        select(app, 2, 6);
        type(app, std::wstring(1, open));
        const std::wstring expected = L"a " + std::wstring(1, open) + L"word" + close + L" here";
        check(app.editorText == expected && app.editorHasSelection &&
              app.editorSelStart == 3 && app.editorSelEnd == 7,
              "a pair character typed over a selection wraps it and keeps it selected");
        key(app, 'Z', {true});
        check(app.editorText == L"a word here", "one Ctrl+Z unwraps");
    }
    setText(app, L"a word here", 0);
    select(app, 2, 6);
    type(app, L"**");
    check(app.editorText == L"a **word** here" && app.editorSelStart == 4 && app.editorSelEnd == 8,
          "typing * twice makes the selection bold");
    setText(app, L"a word here", 0);
    select(app, 6, 2);
    type(app, L"(");
    check(app.editorText == L"a (word) here" && app.editorSelStart == 7 &&
          app.editorSelEnd == 3 && app.editorCursorPos == 3,
          "a backward selection stays backward");
    setText(app, L"line one\nline two", 0);
    select(app, 0, 17);
    type(app, L"`");
    check(app.editorText == L"`line one\nline two`", "a selection across lines wraps as a whole");
    setText(app, L"a word here", 0);
    select(app, 2, 6);
    type(app, L"x");
    check(app.editorText == L"a x here", "other characters still replace the selection");
    app.editorAssists = false;
    setText(app, L"a word here", 0);
    select(app, 2, 6);
    type(app, L"(");
    check(app.editorText == L"a ( here", "with the assists off ( replaces the selection");
    app.editorAssists = true;
}

void columnCarets(App& app) {
    // The report's case: line up a table with carets in one column
    setText(app, L"| a | b |\n| cc | d |\n|eee| f |\nend", 3);
    key(app, VK_DOWN, {true, false, true});
    key(app, VK_DOWN, {true, false, true});
    check(carets(app) == std::vector<size_t>({3, 13, 24}) && app.editorCursorPos == 24,
          "each Ctrl+Alt+Down adds a caret below in the same column, as the primary");
    type(app, L"_");
    check(app.editorText == L"| a_ | b |\n| c_c | d |\n|ee_e| f |\nend" &&
          carets(app) == std::vector<size_t>({4, 15, 27}),
          "typing inserts at every caret");
    keystroke(app, VK_BACK);
    check(app.editorText == L"| a | b |\n| cc | d |\n|eee| f |\nend" &&
          carets(app) == std::vector<size_t>({3, 13, 24}),
          "Backspace deletes at every caret");
    key(app, VK_DELETE);
    check(app.editorText == L"| a| b |\n| c | d |\n|ee| f |\nend", "Delete removes at every caret");
    type(app, L"xy");
    key(app, 'Z', {true});
    check(app.editorText == L"| ax| b |\n| cx | d |\n|eex| f |\nend" &&
          app.editorExtraCarets.empty(),
          "Ctrl+Z takes back the last keystroke at every caret, leaving one caret");
    key(app, 'Z', {true});
    key(app, 'Z', {true});
    check(app.editorText == L"| a | b |\n| cc | d |\n|eee| f |\nend",
          "each Ctrl+Z is one keystroke at all carets");
    key(app, VK_UP, {true, false, true});
    check(app.editorExtraCarets.empty() == false, "Ctrl+Alt+Up adds a caret above");
    key(app, VK_ESCAPE);
    check(app.editorExtraCarets.empty() && app.editMode && !app.escPressedOnce,
          "Esc returns to one caret and stays in the editor");
    setText(app, L"| a | b |\n| cc | d |\n|eee| f |\nend", 3);
    key(app, VK_DOWN, {true, false, true});
    key(app, VK_DOWN, {true, false, true});
    key(app, VK_DOWN);
    check(carets(app) == std::vector<size_t>({13, 24, 34}),
          "added carets keep their column when they move down together");
}

void caretMotion(App& app) {
    setText(app, L"ab\ncd\nef", 1);
    key(app, VK_DOWN, {true, false, true});
    key(app, VK_DOWN, {true, false, true});
    key(app, VK_END);
    check(carets(app) == std::vector<size_t>({2, 5, 8}), "End moves every caret to its line end");
    key(app, VK_HOME);
    check(carets(app) == std::vector<size_t>({0, 3, 6}), "Home moves every caret to its line start");
    key(app, VK_UP);
    check(carets(app) == std::vector<size_t>({0, 3}), "carets that meet merge into one");
    key(app, VK_RIGHT, {false, true});
    check(app.editorHasSelection && app.editorExtraCarets.size() == 1 &&
          app.editorExtraCarets[0].hasSelection,
          "Shift+arrows extend every caret's selection");
    type(app, L"Z");
    check(app.editorText == L"Zb\nZd\nef", "typing replaces every selection");
    key(app, VK_RIGHT, {true});
    check(carets(app) == std::vector<size_t>({2, 5}), "Ctrl+Right moves every caret by word");
}

void clickCarets(App& app) {
    setText(app, L"alpha\nbeta\ngamma", 0);
    const float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    D2D1_POINT_2F point{};
    app.editorCursorPos = 8;
    editorCaretPoint(app, point);
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = 0;
    const int x = (int)(point.x + 1.0f), y = (int)(point.y + lineHeight * 0.5f);
    app.altClickGuard = false;
    withKeys({false, false, true}, [&] {
        handleEditorMouseDown(app, app.hwnd, x, y);
        handleEditorMouseUp(app, app.hwnd, x, y);
    });
    check(carets(app) == std::vector<size_t>({0, 8}) && app.editorCursorPos == 8 && app.altClickGuard,
          "Alt+click adds a caret, which becomes the primary and guards the Alt release");
    withKeys({false, false, true}, [&] {
        handleEditorMouseDown(app, app.hwnd, x, y);
        handleEditorMouseUp(app, app.hwnd, x, y);
    });
    check(carets(app) == std::vector<size_t>({0}), "Alt+click on a caret removes it");
    withKeys({false, false, true}, [&] { handleEditorMouseDown(app, app.hwnd, x, y); });
    app.lastClickTime = {};
    withKeys({}, [&] {
        handleEditorMouseDown(app, app.hwnd, x, y);
        handleEditorMouseUp(app, app.hwnd, x, y);
    });
    check(carets(app) == std::vector<size_t>({8}), "a plain click starts over from one caret");
}

void nextOccurrence(App& app) {
    setText(app, L"cat dog cat cat", 1);
    key(app, 'D', {true});
    check(app.editorHasSelection && app.editorSelStart == 0 && app.editorSelEnd == 3 &&
          app.editorExtraCarets.empty(), "the first Ctrl+D selects the word at the caret");
    key(app, 'D', {true});
    key(app, 'D', {true});
    check(app.editorExtraCarets.size() == 2 && app.editorCursorPos == 15,
          "each further Ctrl+D selects the next occurrence");
    key(app, 'D', {true});
    check(app.editorExtraCarets.size() == 2, "Ctrl+D stops once every occurrence is selected");
    type(app, L"cow");
    check(app.editorText == L"cow dog cow cow", "typing replaces every selected occurrence");
    key(app, 'Z', {true});
    key(app, 'Z', {true});
    key(app, 'Z', {true});
    check(app.editorText == L"cat dog cat cat", "three Ctrl+Z take back the three keystrokes");
}

void caretClipboardText(App& app) {
    setText(app, L"a1 b2 c3", 2);
    select(app, 1, 2);
    addCaret(app, 4, 5);
    addCaret(app, 7, 8);
    check(editorCaretSelectionsText(app) == L"1\n2\n3", "copy joins the selections with line breaks");
    editorPasteAtCarets(app, app.hwnd, L"x\ny\nz\n");
    check(app.editorText == L"ax by cz", "a line per caret is spread over the carets");
    editorPasteAtCarets(app, app.hwnd, L"!");
    check(app.editorText == L"ax! by! cz!", "other text is pasted whole at every caret");
    key(app, 'Z', {true});
    check(app.editorText == L"ax by cz", "a paste at every caret is one undo step");
}

// The rail's list and quote buttons act on every line a selection or a caret
// takes in (#306), and its numbered list counts on across them (#304)
void railLineMarks(App& app) {
    const std::wstring plain = L"line one\nline two\nline three";
    const std::wstring bullets = L"- line one\n- line two\n- line three";
    setText(app, plain, 0);
    select(app, 0, plain.size());
    editRailInvoke(app, app.hwnd, 10);
    check(app.editorText == bullets && app.editorHasSelection && app.editorSelStart == 2 &&
          app.editorSelEnd == bullets.size(),
          "the bullet button marks every selected line, the selection kept on their text");
    editRailInvoke(app, app.hwnd, 10);
    check(app.editorText == plain, "pressed again it takes every mark off");
    key(app, 'Z', {true});
    check(app.editorText == bullets, "one Ctrl+Z puts the marks back");
    key(app, 'Z', {true});
    check(app.editorText == plain, "and one more takes them off: each press is one step");
    setText(app, plain, 0);
    select(app, plain.size(), 0);
    editRailInvoke(app, app.hwnd, 13);
    check(app.editorText == L"1. line one\n2. line two\n3. line three",
          "the numbered list button numbers the selected lines, whichever way they were selected");
    editRailInvoke(app, app.hwnd, 13);
    check(app.editorText == plain, "pressed again it leaves plain text");
    for (int id : {11, 12}) {
        setText(app, plain, 0);
        select(app, 0, plain.size());
        editRailInvoke(app, app.hwnd, id);
        check(app.editorText == (id == 11 ? L"- [ ] line one\n- [ ] line two\n- [ ] line three"
                                          : L"> line one\n> line two\n> line three"),
              "the task and quote buttons mark every selected line too");
    }
    setText(app, L"a\nb\nc", 0);
    select(app, 0, 4);
    editRailInvoke(app, app.hwnd, 10);
    check(app.editorText == L"- a\n- b\nc", "a selection ending at a line's start leaves that line alone");
    setText(app, L"* one\ntwo\n\nthree", 0);
    select(app, 0, 16);
    editRailInvoke(app, app.hwnd, 10);
    check(app.editorText == L"* one\n* two\n\n* three",
          "the other lines take the list's own bullet, and a blank line inside stays blank");
    setText(app, L"a\n- b", 0);
    select(app, 0, 5);
    editRailInvoke(app, app.hwnd, 12);
    check(app.editorText == L"> a\n> - b", "a quote goes around a list item");
    editRailInvoke(app, app.hwnd, 12);
    check(app.editorText == L"a\n- b", "and comes off again");

    // One caret: its line, blank or not; a list mark gives way to another
    setText(app, L"text\n\nmore", 5);
    editRailInvoke(app, app.hwnd, 10);
    check(app.editorText == L"text\n- \nmore" && app.editorCursorPos == 7, "a caret on a blank line starts a list there");
    setText(app, L"- [ ] task", 10);
    editRailInvoke(app, app.hwnd, 10);
    check(app.editorText == L"- task", "the bullet button makes a task a bullet, not [ ] task");
    setText(app, L"- item", 6);
    editRailInvoke(app, app.hwnd, 11);
    check(app.editorText == L"- [ ] item", "the task button makes a bullet a task, not - [ ] - item");
    setText(app, L"- a\n  - b\n  - c\n- d", 0);
    select(app, 0, 19);
    editRailInvoke(app, app.hwnd, 13);
    check(app.editorText == L"1. a\n  1. b\n  2. c\n2. d", "numbers count on at each level of a nested list");

    // Several carets: each line once, the numbers counting on, one undo step
    setText(app, L"a\nb\nc", 0);
    key(app, VK_DOWN, {true, false, true});
    key(app, VK_DOWN, {true, false, true});
    editRailInvoke(app, app.hwnd, 13);
    check(app.editorText == L"1. a\n2. b\n3. c" && carets(app) == std::vector<size_t>({3, 8, 13}),
          "numbers count on across several carets, each caret after its number");
    key(app, 'Z', {true});
    check(app.editorText == L"a\nb\nc", "one Ctrl+Z takes back the numbers at every caret");
    setText(app, L"a\nb\nc", 0);
    key(app, VK_DOWN, {true, false, true});
    key(app, VK_DOWN, {true, false, true});
    editRailInvoke(app, app.hwnd, 10);
    check(app.editorText == L"- a\n- b\n- c", "bullets go on at every caret's line, as before (#251)");
    editRailInvoke(app, app.hwnd, 10);
    check(app.editorText == L"a\nb\nc", "and come off again");
    setText(app, L"ab\nc", 0);
    addCaret(app, 1, 1);
    editRailInvoke(app, app.hwnd, 10);
    check(app.editorText == L"- ab\nc", "two carets on a line mark it once");
}

// A list button on every line of a long document is one replace, quick to
// make and to take back
void railLongDocument(App& app) {
    std::wstring text;
    for (int i = 0; i < 20000; ++i) text += L"line " + std::to_wstring(i) + L"\n";
    setText(app, text, 0);
    select(app, 0, text.size());
    const auto start = std::chrono::steady_clock::now();
    editRailInvoke(app, app.hwnd, 13);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Numbering 20,000 lines: " << (int)ms << " ms\n";
    check(app.editorText.compare(0, 13, L"1. line 0\n2. ") == 0 && app.undoStack.size() == 1,
          "numbers on 20,000 selected lines are one replace");
    check(ms < 250.0, "and quick to make");
    key(app, 'Z', {true});
    check(app.editorText == text, "one Ctrl+Z takes them all off");
}

// The rail's numbered list button: between the bullets and the tasks, named,
// clicked; in a short window the buttons sit closer, the last above the foot
void railNumberButton(App& app) {
    auto paintRail = [&] {
        app.editRailAnim = 1.0f;
        app.renderTarget->BeginDraw();
        renderEditRail(app);
        app.renderTarget->EndDraw();
    };
    auto hit = [&](int id) {
        for (const auto& h : app.editRailHits) {
            if (h.second == id) return h.first;
        }
        return D2D1_RECT_F{};
    };
    paintRail();
    const D2D1_RECT_F bullets = hit(10), numbers = hit(13), tasks = hit(11);
    check(numbers.bottom > numbers.top && numbers.top >= bullets.bottom && numbers.bottom <= tasks.top,
          "the numbered list button sits between the bullets and the tasks");
    check(std::fabs(numbers.bottom - numbers.top - dpi(app, 32.0f)) < 0.5f, "a tall window keeps the buttons' size");
    setText(app, L"one\ntwo", 0);
    select(app, 0, 7);
    if (numbers.bottom > numbers.top) {
        const int x = (int)((numbers.left + numbers.right) * 0.5f), y = (int)((numbers.top + numbers.bottom) * 0.5f);
        handleMouseMove(app, app.hwnd, MAKELPARAM(x, y));
        check(app.editRailHover == 13 && std::wstring(tr(app, "rail.numbers")) == L"Numbered list",
              "it is named Numbered list");
        handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM(x, y));
        handleMouseUp(app, app.hwnd, 0, MAKELPARAM(x, y));
    }
    check(app.editorText == L"1. one\n2. two", "a click on it numbers the selected lines");
    app.editRailHover = 0;
    // With and without Pandoc's export button at the rail's foot
    const int height = app.height;
    const std::wstring pandoc = app.pandocExe;
    const bool pandocChecked = app.pandocChecked;
    for (bool foot : {false, true}) {
        app.pandocChecked = true;
        app.pandocExe = foot ? L"pandoc.exe" : L"";
        for (int h : {900, 600, 540}) {
            app.height = h;
            paintRail();
            const D2D1_RECT_F last = hit(22), export_ = hit(23), first = hit(1);
            check((export_.bottom > export_.top) == foot, "the foot shows Pandoc's export button only when it is found");
            const float limit = foot ? export_.top - dpi(app, 8.0f) : (float)h;
            check(last.bottom > last.top && last.bottom <= limit + 0.5f,
                  "the rail's last button stays above its foot and the window's bottom");
            check(first.bottom - first.top >= dpi(app, 24.0f) - 0.5f, "a short window's buttons stay a finger wide");
            const std::string name = "rail-" + std::to_string(h) + (foot ? "-pandoc" : "") + ".png";
            check(saveFramePreview(app, std::filesystem::current_path() / name, [&] { renderEditRail(app); }),
                  "the rail's frame is saved");
        }
    }
    app.pandocExe = pandoc;
    app.pandocChecked = pandocChecked;
    app.height = height;
}

// The mixed fixture of list marks in the editor, as it is on disk
std::wstring openListFixture(App& app) {
    app.editorDirty = false;
    exitEditMode(app);
    app.currentFile = TINTA_LIST_MARKS_FIXTURE;
    enterEditMode(app);
    check(app.editMode && app.editorText.find(L"Milk\nEggs\nBread") != std::wstring::npos,
          "the list marks fixture opens in the editor");
    return app.editorText;
}

// The buttons on the lines the mixed fixture names, the rest of it untouched
void railFixture(App& app) {
    const std::wstring original = openListFixture(app);
    const size_t milk = original.find(L"Milk\nEggs\nBread"), wake = original.find(L"Wake up\nMake coffee\nRead the news");
    if (milk == std::wstring::npos || wake == std::wstring::npos) return;
    select(app, milk, milk + 15);
    editRailInvoke(app, app.hwnd, 10);
    std::wstring expected = original;
    expected.replace(milk, 15, L"- Milk\n- Eggs\n- Bread");
    check(app.editorText == expected, "in the fixture the bullets go on those three lines and nowhere else");
    key(app, 'Z', {true});
    select(app, wake, wake + 33);
    editRailInvoke(app, app.hwnd, 13);
    expected = original;
    expected.replace(wake, 33, L"1. Wake up\n2. Make coffee\n3. Read the news");
    check(app.editorText == expected, "and the numbers on those three");
    key(app, 'Z', {true});
    check(app.editorText == original, "Ctrl+Z leaves the fixture as it was");
    app.editorDirty = false;
}

// Lines pasted on a list item are its items, as Enter makes them, and lines
// pasted in a quote its lines (#305)
void pasteOnLists(App& app) {
    const std::wstring three = L"line one\nline two\nline three";
    auto pasted = [](const std::wstring& text, size_t at, const std::wstring& paste) {
        std::wstring out = text;
        out.insert(at, live::pasteMarks(text, at, at, paste));
        return out;
    };
    check(pasted(L"- ", 2, three) == L"- line one\n- line two\n- line three", "lines pasted on a bullet are items");
    check(pasted(L"1. ", 3, three) == L"1. line one\n2. line two\n3. line three", "on a number they count on");
    check(pasted(L"- [x] ", 6, three) == L"- [x] line one\n- [ ] line two\n- [ ] line three",
          "on a task they get empty boxes");
    check(pasted(L"> ", 2, three) == L"> line one\n> line two\n> line three", "in a quote they are quoted");
    check(pasted(L"> - ", 4, three) == L"> - line one\n> - line two\n> - line three", "a list in a quote goes on in it");
    check(pasted(L"  * ", 4, three) == L"  * line one\n  * line two\n  * line three",
          "a nested item's indent and bullet go on");
    check(pasted(L"- ", 2, three + L"\n") == L"- line one\n- line two\n- line three",
          "a line break at the paste's end leaves no empty item");
    check(pasted(L"- ab", 3, L"x\ny") == L"- ax\n- yb", "pasted inside an item, its rest is the last item");
    check(pasted(L"- ab", 3, L"x\n") == L"- ax\n- b", "also after a paste ending with a line break");
    check(pasted(L"- ", 2, L"one\n\ntwo") == L"- one\n\n- two", "a blank line stays blank between items");
    check(pasted(L"> ", 2, L"one\n\ntwo") == L"> one\n>\n> two", "and keeps a quote going");
    check(pasted(L"- ", 2, L"one\n- two\n  - three\n    more") == L"- one\n- two\n  - three\n    more",
          "lines with a mark or an indent of their own keep it");
    check(pasted(L"- ", 2, L"```\ncode\n```") == L"- ```\ncode\n```", "a fence goes in as it is");
    check(pasted(L"- ", 2, L"| a |\n| - |") == L"- | a |\n| - |", "so does a table");
    check(pasted(L"- x", 0, three) == three + L"- x", "before the mark the paste is plain");
    check(pasted(L"plain", 5, three) == L"plain" + three, "on a paragraph it is plain");
    check(pasted(L"```\n- ", 6, three) == L"```\n- " + three, "in code it is plain");

    // Through the editor's own paste, the user's clipboard put back after
    ClipboardSnapshot saved(app.hwnd);
    check(saved.captured, "the clipboard is kept to be put back");
    if (!saved.captured) return;
    auto clip = [&](const std::wstring& text) {
        bool copied = false;
        for (int attempt = 0; attempt < 20 && !(copied = copyToClipboard(app.hwnd, text)); ++attempt) Sleep(5);
        check(copied, "the test's lines reach the clipboard");
    };
    clip(three);
    setText(app, L"- ", 2);
    key(app, 'V', {true});
    check(app.editorText == L"- line one\n- line two\n- line three" && app.editorCursorPos == app.editorText.size(),
          "Ctrl+V on a bullet pastes three items, the caret after the last");
    key(app, 'Z', {true});
    check(app.editorText == L"- ", "one Ctrl+Z takes the paste back");
    key(app, 'V', {true, true});
    check(app.editorText == L"- " + three, "Ctrl+Shift+V pastes the text as it is");
    app.editorAssists = false;
    setText(app, L"- ", 2);
    key(app, 'V', {true});
    check(app.editorText == L"- " + three, "with the assists off the paste is plain");
    app.editorAssists = true;
    setText(app, L"1. ", 3);
    editorClipboardPaste(app, app.hwnd);
    check(app.editorText == L"1. line one\n2. line two\n3. line three", "the menu's Paste numbers on as well");

    // The pastes the mixed fixture describes
    openListFixture(app);
    clip(L"Milk\nEggs\nBread\n");
    const struct { const wchar_t* item; const wchar_t* expect; } cases[] = {
        {L"- Pears", L"- Pears\n- Milk\n- Eggs\n- Bread\n\n1. First"},
        {L"2. Second", L"2. Second\n3. Milk\n4. Eggs\n5. Bread\n\n- [ ]"},
        {L"- [x] Send them", L"- [x] Send them\n- [ ] Milk\n- [ ] Eggs\n- [ ] Bread\n\n> A quoted"},
    };
    for (const auto& c : cases) {
        const size_t at = app.editorText.find(c.item) + wcslen(c.item);
        select(app, at, at);
        keystroke(app, VK_RETURN);
        key(app, 'V', {true});
        check(app.editorText.find(c.expect) != std::wstring::npos,
              "Enter at a list's end and a paste make each line an item of that list");
    }
    clip(L"\nSecond line\nThird line");
    const size_t quoted = app.editorText.find(L"> A quoted line") + 15;
    select(app, quoted, quoted);
    key(app, 'V', {true});
    check(app.editorText.find(L"> A quoted line\n> Second line\n> Third line\n") != std::wstring::npos,
          "lines pasted at a quote's end are quoted");
    const size_t code = app.editorText.find(L"- not a list here") + 17;
    select(app, code, code);
    key(app, 'V', {true});
    check(app.editorText.find(L"- not a list here\nSecond line\nThird line\n```") != std::wstring::npos,
          "in the fixture's code the lines stay as they are");
    app.editorDirty = false;
}

void wordCount(App& app) {
    app.showWordCount = true;
    setText(app, L"# Title\n\nHello **bold** world", 0);
    editorReparse(app, true);
    check(app.editorDocCounts.words == 4 && app.editorDocCounts.characters == 21,
          "the editor counts the words and characters a reader sees");
    select(app, 9, 14);
    app.renderTarget->BeginDraw();
    renderEditorWordCount(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "the count chip paints");
    const D2D1_RECT_F pill = editorReadingButtonRect(app);
    const D2D1_RECT_F chip = app.editorWordCountRect;
    check(chip.right > chip.left && chip.left > pill.right && chip.top == pill.top,
          "the chip sits beside the Read button");
    check(app.editorSelCounts.words == 1 && app.editorSelCounts.characters == 5,
          "a selection shows its own count");
    const std::string saved = app.currentFile;
    app.currentFile = "notes.txt";
    setText(app, L"**not markup** here", 0);
    editorReparse(app, true);
    check(app.editorDocCounts.words == 3 && app.editorDocCounts.characters == 19,
          "plain-text documents count as they stand");
    app.currentFile = "diagram.puml";
    setText(app, L"@startuml\nAlice -> Bob: hi\n@enduml", 0);
    editorReparse(app, true);
    check(app.editorDocCounts.words > 0 && app.editorDocCounts.characters > 0,
          "a PlantUML source counts as it stands");
    app.currentFile = saved;
    app.showWordCount = false;
    app.renderTarget->BeginDraw();
    renderEditorWordCount(app);
    app.renderTarget->EndDraw();
    check(app.editorWordCountRect.right <= app.editorWordCountRect.left,
          "the switch hides the chip");
    app.showWordCount = true;
}

// #252: what the window keeps on a close without asking, and the next
// launch handing it back
void draftRecovery() {
    namespace fs = std::filesystem;
    const std::string a = fs::absolute("draft-a.md").u8string();
    const std::string b = fs::absolute("draft-b.md").u8string();
    std::ofstream(a, std::ios::binary) << "saved A\n";
    std::ofstream(b, std::ios::binary) << "saved B\n";

    auto first = std::make_unique<App>();
    first->hwnd = CreateWindowExW(0, L"STATIC", L"Draft tests", WS_POPUP, 0, 0, 1050, 900,
                                  nullptr, nullptr, nullptr, nullptr);
    check(first->hwnd && initD2D(*first) && createRenderTarget(*first), "draft test editor starts");
    first->width = 1050;
    first->height = 900;
    updateTextFormats(*first);
    first->currentFile = a;
    tabsInit(*first);
    tabOpenPath(*first, first->hwnd, b, false);
    enterRecoveredDraft(*first, first->hwnd, "draft of A", a);
    check(first->tabs.size() == 2 && first->currentFile == a && first->editMode &&
          first->editorDirty && first->editorText == L"draft of A",
          "a draft takes over the clean tab of its own file");
    enterRecoveredDraft(*first, first->hwnd, "draft of B", b);
    check(first->tabs.size() == 2 && first->currentFile == b && first->editorText == L"draft of B",
          "an inactive clean tab of the file is reused too");
    enterRecoveredDraft(*first, first->hwnd, "second draft of B", b);
    check(first->tabs.size() == 3 && first->editorText == L"second draft of B",
          "a tab with unsaved edits keeps them; the draft opens beside it");

    // Closing without asking writes every unsaved buffer as a draft
    draftsSweep(*first);
    std::vector<std::wstring> drafts;
    const std::wstring prefix = L"draft-" + std::to_wstring(GetCurrentProcessId()) + L"-";
    for (const auto& entry : fs::directory_iterator(fs::path(tintaConfigDir()) / L"drafts")) {
        const std::wstring name = entry.path().filename().wstring();
        if (name.rfind(prefix, 0) == 0) drafts.push_back(entry.path().wstring());
    }
    check(drafts.size() == 3, "every unsaved tab leaves a draft behind");

    // The next launch: the session restored A clean, the drafts come back
    auto next = std::make_unique<App>();
    next->hwnd = CreateWindowExW(0, L"STATIC", L"Draft tests 2", WS_POPUP, 0, 0, 1050, 900,
                                 nullptr, nullptr, nullptr, nullptr);
    check(next->hwnd && initD2D(*next) && createRenderTarget(*next), "next launch starts");
    next->width = 1050;
    next->height = 900;
    updateTextFormats(*next);
    next->currentFile = a;
    tabsInit(*next);
    next->recoveredDrafts = drafts;
    draftsRecoverAll(*next, next->hwnd);
    size_t dirtyTabs = 0;
    bool sawA = false;
    for (size_t i = 0; i < next->tabs.size(); ++i) {
        const bool active = (int)i == next->activeTab;
        const bool dirty = active ? next->editorDirty : next->tabs[i].editorDirty;
        const std::wstring text = active ? next->editorText : next->tabs[i].editorText;
        dirtyTabs += dirty ? 1 : 0;
        sawA = sawA || text == L"draft of A";
    }
    check(next->tabs.size() == 3 && dirtyTabs == 3 && sawA,
          "the next launch restores every draft as an unsaved tab, A in its own tab");
    bool leftover = false;
    for (const auto& path : drafts) leftover = leftover || fs::exists(path);
    check(!leftover, "restored drafts are removed from disk");

    for (App* app : {first.get(), next.get()}) {
        draftsDeleteAll(*app);
        DestroyWindow(app->hwnd);
        app->hwnd = nullptr;
    }
}
}  // namespace

int runEditorFeatureTests() {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Editor feature tests", WS_POPUP, 0, 0, 1050, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    app.currentFile = TINTA_MULTICURSOR_FIXTURE;
    enterEditMode(app);
    check(app.editMode && app.editorText.find(L"| Grace | Admiral |") != std::wstring::npos,
          "the mixed #251 fixture opens in the editor");
    for (bool wrap : {false, true}) {
        app.editorWordWrap = wrap;
        moveLines(app);
        wrapSelection(app);
        columnCarets(app);
        caretMotion(app);
        clickCarets(app);
        nextOccurrence(app);
        caretClipboardText(app);
        railLineMarks(app);
    }
    railNumberButton(app);
    railLongDocument(app);
    railFixture(app);
    pasteOnLists(app);
    wordCount(app);
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    draftRecovery();
    CoUninitialize();
    std::cout << "Editor features: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
