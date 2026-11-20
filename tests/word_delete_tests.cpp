// #330: Ctrl+Backspace and Ctrl+Delete take a word in the source editor.
//
// The source knew neither. Ctrl+Delete did nothing. Ctrl+Backspace was no
// key to it, so the message loop made the key's character and the editor
// typed it: 0x7F, a DEL that shows as nothing, into the file. The count of
// characters went up with every press and the word stayed.
//
// Keys go through keystroke(), which hands a key's character over when the
// key-down leaves it unconsumed, as the message loop does: Ctrl+Backspace's
// DEL arrives here as it does in the app.
#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "render.h"
#include "search.h"
#include "settings.h"
#include "utils.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) {
        if (failures < 60) std::cerr << "FAIL word delete: " << message << '\n';
        ++failures;
    }
}

const wchar_t kDel = 0x7F;
const wchar_t kMark = L'^';  // a caret in a case's text; the fixture has none

// For messages: ASCII as it is, a line break as \n, the rest as \uXXXX
std::string narrow(const std::wstring& text) {
    std::string out;
    for (wchar_t c : text) {
        if (c == L'\n') out += "\\n";
        else if (c >= 0x20 && c < 0x7F) out += (char)c;
        else {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\u%04X", (unsigned)c);
            out += buffer;
        }
    }
    return out;
}

struct Marked {
    std::wstring text;
    std::vector<size_t> carets;
};

Marked marked(const std::wstring& spec) {
    Marked m;
    for (wchar_t c : spec) {
        if (c == kMark) m.carets.push_back(m.text.size());
        else m.text += c;
    }
    return m;
}

// The text in the editor with a caret at each mark: the last is the primary
void place(App& app, const std::wstring& text, const std::vector<size_t>& carets) {
    app.editorText = text;
    rebuildLineStarts(app);
    app.editorExtraCarets.clear();
    for (size_t i = 0; i + 1 < carets.size(); ++i) {
        App::EditorCaret caret;
        caret.pos = caret.anchor = carets[i];
        app.editorExtraCarets.push_back(caret);
    }
    app.editorCursorPos = carets.empty() ? 0 : carets.back();
    app.editorSelStart = app.editorSelEnd = app.editorCursorPos;
    app.editorHasSelection = false;
    app.editorDesiredCol = -1;
    app.undoStack.clear();
    app.redoStack.clear();
}

// The editor's text with a mark at every caret
std::wstring shown(const App& app) {
    std::vector<size_t> at{app.editorCursorPos};
    for (const auto& caret : app.editorExtraCarets) at.push_back(caret.pos);
    std::sort(at.begin(), at.end());
    std::wstring text = app.editorText;
    for (size_t i = at.size(); i-- > 0;) text.insert(std::min(at[i], text.size()), 1, kMark);
    return text;
}

enum class Key { Back, Delete };
const char* nameOf(Key key) { return key == Key::Back ? "Ctrl+Backspace" : "Ctrl+Delete"; }
void press(App& app, Key key, bool shift = false) {
    keystroke(app, key == Key::Back ? VK_BACK : VK_DELETE, {true, shift});
}

void paint(App& app) {
    app.renderTarget->BeginDraw();
    renderEditor(app, editorPaneWidth(app));
    check(SUCCEEDED(app.renderTarget->EndDraw()), "the editor paints");
}

// --- Cases in a text of their own --------------------------------------------

struct Case {
    Key key;
    const wchar_t* before;
    const wchar_t* after;
    const char* what;
};

void run(App& app, const Case& c, bool shift = false) {
    const Marked from = marked(c.before);
    place(app, from.text, from.carets);
    press(app, c.key, shift);
    const std::string name = std::string(nameOf(c.key)) + (shift ? " with Shift" : "") + ", " + c.what;
    check(shown(app) == c.after, name + ": " + narrow(c.before) + " gives " + narrow(shown(app)) + ", not " + narrow(c.after));
    check(app.editorText.find(kDel) == std::wstring::npos || from.text.find(kDel) != std::wstring::npos,
          name + ": no DEL is typed");
    check(!app.editorHasSelection, name + ": nothing is left selected");
    const bool changed = app.editorText != from.text;
    check(app.undoStack.empty() == !changed, name + ": a change is on the undo list, and nothing else is");
    if (!changed) return;
    // One step back whatever the number of carets, and forward again
    const std::wstring result = app.editorText;
    keystroke(app, 'Z', {true, false});
    check(app.editorText == from.text, name + ": one undo brings the text back");
    check(from.carets.size() > 1 || app.editorCursorPos == from.carets[0], name + ": and the caret");
    keystroke(app, 'Y', {true, false});
    check(app.editorText == result, name + ": redo takes the word again");
}

void cases(App& app) {
    const Case list[] = {
        {Key::Back, L"alpha beta^", L"alpha ^", "a word"},
        {Key::Back, L"alpha beta   ^", L"alpha ^", "the spaces behind a word go with it"},
        {Key::Back, L"alpha be^ta", L"alpha ^ta", "the caret inside a word: its part before"},
        {Key::Back, L"alpha, beta^", L"alpha, ^", "a comma stays with the word before it"},
        {Key::Back, L"alpha, ^beta", L"^beta", "punctuation and the word before it"},
        {Key::Back, L"a.b(c^", L"a.b(^", "a letter of a call"},
        {Key::Back, L"a.b(^", L"a.^", "a bracket and its name"},
        {Key::Back, L"snake_case_name^ x", L"^ x", "an underscore joins"},
        {Key::Back, L"^alpha", L"^alpha", "nothing at the start of the text"},
        {Key::Back, L"alpha\n^beta", L"alpha^beta", "at a line's start the line break alone"},
        {Key::Back, L"alpha\n\n^beta", L"alpha\n^beta", "one empty line at a time"},
        {Key::Back, L"alpha\n    ^beta", L"alpha\n^beta", "indentation, and no further than the line"},
        {Key::Back, L"alpha\n\t\t^beta", L"alpha\n^beta", "tabs as indentation"},
        {Key::Back, L"alpha\n- [ ] ^task", L"alpha\n^task", "a task's mark, and no further than the line"},
        {Key::Back, L"   ^", L"^", "nothing but spaces"},
        {Key::Back, L"caf\u00E9 \u00F6\u011Frenci^", L"caf\u00E9 ^", "a word in Turkish letters"},
        {Key::Back, L"e\u0301te\u0301^ x", L"^ x", "a letter typed with its accent after it"},
        {Key::Back, L"abc \u4F60\u597D\u4E16\u754C^", L"abc ^", "a run of Chinese, and not the word before it"},
        {Key::Back, L"\u4F60\u597D world^", L"\u4F60\u597D ^", "a word behind Chinese"},
        {Key::Back, L"\u4F60\u597Dworld^", L"\u4F60\u597D^", "a word that touches Chinese"},
        {Key::Back, L"say \U0001F44D\U0001F44D^", L"say ^", "emoji, whole"},
        {Key::Back, L"word\x7F\x7F^", L"^", "the DELs an older build typed go with their word"},
        {Key::Delete, L"^alpha beta", L"^ beta", "a word"},
        {Key::Delete, L"alpha^ beta gamma", L"alpha^ gamma", "the space before the next word goes with it"},
        {Key::Delete, L"al^pha beta", L"al^ beta", "the caret inside a word: its part after"},
        {Key::Delete, L"a^.b(c)", L"a^(c)", "punctuation and the name after it"},
        {Key::Delete, L"alpha^\nbeta", L"alpha^beta", "at a line's end the line break alone"},
        {Key::Delete, L"alpha^   \nbeta", L"alpha^\nbeta", "spaces at a line's end, and no further than the line"},
        {Key::Delete, L"alpha^", L"alpha^", "nothing at the end of the text"},
        {Key::Delete, L"abc^\u4F60\u597D def", L"abc^ def", "a run of Chinese, and not the word after it"},
        {Key::Delete, L"^\u4F60\u597Dworld x", L"^world x", "Chinese that touches a word"},
        {Key::Delete, L"say ^\U0001F44D\U0001F44D up", L"say ^ up", "emoji, whole"},
        // Several carets: each its own word, no caret past its neighbour
        {Key::Back, L"foo bar^\nbaz qux^", L"foo ^\nbaz ^", "two carets, a word each"},
        {Key::Back, L"aa bb^ cc^", L"aa ^ ^", "two carets on a line"},
        {Key::Back, L"foo^bar^", L"^", "two carets in one word take it between them"},
        {Key::Back, L"one^\n^two", L"^two", "a caret at a line's start beside one at the line's end before it"},
        {Key::Delete, L"^foo bar\n^baz qux", L"^ bar\n^ qux", "two carets, a word each"},
        {Key::Delete, L"^foo^bar", L"^", "two carets in one word take it between them"},
        {Key::Delete, L"^a b ^c d ^e f", L"^ b ^ d ^ f", "three carets"},
        {Key::Delete, L"^.^.. x y", L"^ y", "a caret takes no more than what is before the next caret"},
    };
    for (const Case& c : list) run(app, c);
    // Ctrl+Shift is no chord of the editor's but the few it names
    // (shortcutModifiersAllowed): nothing goes, and no DEL is typed
    run(app, {Key::Back, L"alpha beta^", L"alpha beta^", "nothing"}, true);
    run(app, {Key::Delete, L"^alpha beta", L"^alpha beta", "nothing"}, true);

    // A selection goes as Backspace and Delete take it, and no word with it
    for (Key key : {Key::Back, Key::Delete}) {
        place(app, L"alpha beta gamma", {10});
        app.editorHasSelection = true;
        app.editorSelStart = 6;
        app.editorSelEnd = 10;
        press(app, key);
        check(shown(app) == L"alpha ^ gamma" && !app.editorHasSelection,
              std::string(nameOf(key)) + " on a selection takes the selection: " + narrow(shown(app)));
        check(app.undoStack.size() == 1, std::string(nameOf(key)) + " on a selection is one step to undo");
    }

    // The keys without Ctrl are what they were
    place(app, L"alpha beta", {10});
    keystroke(app, VK_BACK);
    check(shown(app) == L"alpha bet^", "Backspace alone takes a letter: " + narrow(shown(app)));
    place(app, L"alpha beta", {0});
    keystroke(app, VK_DELETE);
    check(shown(app) == L"^lpha beta", "Delete alone takes a letter: " + narrow(shown(app)));

    // Word after word, down to nothing; each press its own step
    place(app, L"one two three four", {18});
    for (const wchar_t* left : {L"one two three ^", L"one two ^", L"one ^", L"^", L"^"}) {
        press(app, Key::Back);
        check(shown(app) == left, std::string("word after word: ") + narrow(shown(app)));
    }
    check(app.undoStack.size() == 4, "four words, four steps to undo, and the fifth press none");
    for (int i = 0; i < 4; ++i) keystroke(app, 'Z', {true, false});
    check(app.editorText == L"one two three four", "undone, the line is back");
}

// --- The fixture -------------------------------------------------------------

struct OnPage {
    Key key;
    const wchar_t* before;
    const wchar_t* after;
};

const OnPage kOnPage[] = {
    {Key::Back, L"are separated^ by spaces", L"are ^ by spaces"},
    {Key::Back, L"by spaces, ^by commas", L"by ^by commas"},
    {Key::Back, L"A snake_case_name^ is", L"A ^ is"},
    {Key::Back, L"a hyphenated-pair^ is two", L"a hyphenated-^ is two"},
    {Key::Back, L"a full st^op.", L"a full ^op."},
    {Key::Back, L"line ends here.\n^> Its second", L"line ends here.^> Its second"},
    {Key::Back, L"of the list\n    ^- nested item", L"of the list\n^- nested item"},
    {Key::Back, L"- [ ] ^an open task", L"^an open task"},
    {Key::Back, L"(https://example.com/path/to/page^) inside", L"(https://example.com/path/to/^) inside"},
    {Key::Back, L"| Ctrl+Backspace^ | the word before", L"| Ctrl+^ | the word before"},
    {Key::Back, L"first_value + second_value^;", L"first_value + ^;"},
    {Key::Back, L"\n    return total_count^;", L"\n    return ^;"},
    {Key::Back, L"\n    ^return total_count;", L"\n^return total_count;"},
    {Key::Back, L"\u00E7al\u0131\u015Fma \u015F\u00F6yle^ g\u00F6r\u00FCn\u00FCr", L"\u00E7al\u0131\u015Fma ^ g\u00F6r\u00FCn\u00FCr"},
    {Key::Back, L"\u0131\u011Fd\u0131r, \u0130stanbul^,", L"\u0131\u011Fd\u0131r, ^,"},
    {Key::Back, L"\u4E00\u6B21\u5220\u6389\u4E00\u6BB5\u3002^Mixed", L"^Mixed"},
    {Key::Back, L"Mixed \u4E2D\u6587^ and English", L"Mixed ^ and English"},
    {Key::Back, L"thumbs \U0001F44D\U0001F44D^ up", L"thumbs ^ up"},
    {Key::Back, L"a flag \U0001F1ED\U0001F1F7^ too", L"a flag ^ too"},
    {Key::Back, L"no line break after it^", L"no line break after ^"},
    {Key::Delete, L"are ^separated by spaces", L"are ^ by spaces"},
    {Key::Delete, L"are separated^ by spaces,", L"are separated^ spaces,"},
    {Key::Delete, L"line ends here.^\n> Its second", L"line ends here.^> Its second"},
    {Key::Delete, L"by commas^, and by", L"by commas^ by"},
    {Key::Delete, L"a full stop^. A snake", L"a full stop^ snake"},
    {Key::Delete, L") inside^\n- [ ] an open", L") inside^- [ ] an open"},
    {Key::Delete, L"Mixed ^\u4E2D\u6587 and", L"Mixed ^ and"},
    {Key::Delete, L"thumbs ^\U0001F44D\U0001F44D up", L"thumbs ^ up"},
    {Key::Delete, L"second_value;^  // trailing words", L"second_value;^ words"},
    {Key::Delete, L"no line break after it^", L"no line break after it^"},
};

void onPage(App& app, const std::wstring& original, const OnPage& c, const std::string& where) {
    const Marked from = marked(c.before), to = marked(c.after);
    const std::string name = std::string(nameOf(c.key)) + " at " + narrow(c.before) + where;
    // The fixture's Chinese line begins with the run the case names the end of
    const size_t at = original.find(from.text);
    check(at != std::wstring::npos && original.find(from.text, at + 1) == std::wstring::npos,
          name + ": the fixture has the place once");
    if (at == std::wstring::npos) return;
    place(app, original, {at + from.carets[0]});
    press(app, c.key);
    std::wstring expected = original;
    size_t cut = at, length = from.text.size();
    std::wstring put = to.text;
    if (std::wstring(c.after) == L"^Mixed") {
        // The whole run of Chinese before the caret, back to the line's start
        cut = original.rfind(L'\n', at) + 1;
        length = at + from.text.size() - cut;
    }
    expected.replace(cut, length, put);
    const size_t caret = cut + to.carets[0];
    check(app.editorText == expected, name + ": the text is " + narrow(shown(app).substr(caret > 30 ? caret - 30 : 0, 70)));
    check(app.editorCursorPos == caret, name + ": the caret is where the word began");
    check(app.editorText.find(kDel) == std::wstring::npos, name + ": no DEL is typed");
    const bool changed = expected != original;
    check(app.editorText.size() == original.size() - (length - put.size()),
          name + ": the text is shorter by what went, and never longer");
    check(app.undoStack.size() == (changed ? 1u : 0u), name + ": one step to undo");
    paint(app);
    D2D1_POINT_2F point{};
    check(editorCaretPoint(app, point) && point.y >= chromeTopHeight(app) - 1.0f && point.y < app.height,
          name + ": the caret is in the pane afterwards");
    if (!changed) return;
    keystroke(app, 'Z', {true, false});
    check(app.editorText == original && app.editorCursorPos == at + from.carets[0], name + ": undo puts it back");
}

std::string bytesOf(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void fixture(App& app, const char* path) {
    app.currentFile = path;
    app.editorLive = false;
    enterEditMode(app);
    check(app.editMode && !editorLiveOn(app), "the fixture opens in the source");
    const std::wstring original = app.editorText;
    check(original.find(kDel) == std::wstring::npos && original.find(kMark) == std::wstring::npos,
          "the fixture has no DEL and none of the test's marks");
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 420}) {
            for (bool wrap : {false, true}) {
                app.width = width;
                app.editorShowPreview = width > 600;
                app.editorWordWrap = wrap;
                const std::string where = " (" + std::to_string(width) + (wrap ? ", wrapped" : "") +
                                          ", theme " + std::to_string(theme) + ")";
                for (const OnPage& c : kOnPage) onPage(app, original, c, where);
            }
        }
    }
    app.width = 1050;
    app.editorShowPreview = true;

    // The count the status bar shows goes down, where it went up
    place(app, original, {original.size()});
    app.editorDirty = false;
    for (int i = 0; i < 6; ++i) press(app, Key::Back);
    check(app.editorText.size() < original.size() && app.editorText.find(kDel) == std::wstring::npos,
          "six presses at the end leave the text shorter and without a DEL");
    check(app.editorDirty, "and the text counts as changed");

    // The find bar open over the source: its own word goes, the source stays
    place(app, original, {original.size()});
    keystroke(app, 'F', {true, false});
    check(app.showSearch && app.searchActive, "Ctrl+F opens the find bar");
    typeKeys(app, L"one two");
    press(app, Key::Back);
    check(app.searchQuery == L"one ", "Ctrl+Backspace in the find bar takes its word: " + narrow(app.searchQuery));
    check(app.editorText == original, "and leaves the source alone");
    keystroke(app, VK_ESCAPE);
    check(!app.showSearch, "Esc closes the find bar");

    // The reading view laid over the editor edits nothing
    place(app, original, {original.size()});
    setEditorReadingPreview(app, true);
    press(app, Key::Back);
    press(app, Key::Delete);
    check(app.editorText == original, "the reading view inside the editor takes no word");
    setEditorReadingPreview(app, false);

    // Saved, the file holds the text and no DEL
    place(app, original, {original.find(L"separated") + 9});
    press(app, Key::Back);
    saveEditorFile(app, app.hwnd);
    const std::string saved = bytesOf(path);
    check(!saved.empty() && saved.find('\x7F') == std::string::npos, "the saved file has no DEL in it");
    check(saved.find("These words are  by spaces") != std::string::npos, "and has lost the word");

    // Live had the keys already and keeps them
    setEditorLive(app, app.hwnd, true);
    check(editorLiveOn(app), "the page switches to Live");
    keystroke(app, VK_END, {true, false});
    typeKeys(app, L" tail words");
    const std::wstring typed = app.editorText;
    check(typed.size() > 11 && typed.compare(typed.size() - 11, 11, L" tail words") == 0, "Live types at the end");
    press(app, Key::Back);
    check(app.editorText == typed.substr(0, typed.size() - 5), "Live: Ctrl+Backspace takes the word: " +
                                                                  narrow(app.editorText.substr(app.editorText.size() > 20 ? app.editorText.size() - 20 : 0)));
    check(app.editorText.find(kDel) == std::wstring::npos, "Live: no DEL is typed");
    setEditorLive(app, app.hwnd, false);
    app.editorDirty = false;
    exitEditMode(app);
    check(!app.editMode, "the editor closes");
}

// --- A note on the page ------------------------------------------------------

void note(App& app) {
    auto open = [&](const std::wstring& text, size_t caret) {
        app.annotEditorOpen = true;
        app.annotEditorText = text;
        app.annotEditorCaret = caret;
    };
    open(L"one two three", 13);
    press(app, Key::Back);
    check(app.annotEditorText == L"one two " && app.annotEditorCaret == 8,
          "a note: Ctrl+Backspace takes the word and types no DEL: " + narrow(app.annotEditorText));
    open(L"one two three", 3);
    press(app, Key::Delete);
    check(app.annotEditorText == L"one three" && app.annotEditorCaret == 3,
          "a note: Ctrl+Delete takes the next word: " + narrow(app.annotEditorText));
    open(L"one two", 0);
    press(app, Key::Back);
    check(app.annotEditorText == L"one two" && app.annotEditorCaret == 0, "a note: nothing at its start");
    open(L"one two", 7);
    press(app, Key::Delete);
    check(app.annotEditorText == L"one two" && app.annotEditorCaret == 7, "a note: nothing at its end");
    open(L"one two", 7);
    keystroke(app, VK_BACK);
    check(app.annotEditorText == L"one tw" && app.annotEditorCaret == 6, "a note: Backspace alone takes a letter");
    open(L"one two", 0);
    keystroke(app, VK_DELETE);
    check(app.annotEditorText == L"ne two" && app.annotEditorCaret == 0, "a note: Delete alone takes a letter");
    app.annotEditorOpen = false;
}
}  // namespace

int runWordDeleteTests(const char* path) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Word delete tests", WS_POPUP, 0, 0, 1050, 900, nullptr, nullptr,
                               nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    applyTheme(app, 0);

    // Before the editor opens: a note takes the keys only in the reading view
    note(app);
    app.currentFile = path;
    app.editorLive = false;
    enterEditMode(app);
    check(app.editMode && !editorLiveOn(app), "the source opens");
    cases(app);
    app.editorDirty = false;
    exitEditMode(app);
    fixture(app, path);

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Word delete: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
