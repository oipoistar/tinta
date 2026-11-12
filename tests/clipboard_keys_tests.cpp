#include "clipboard_keys.h"
#include "clipboard_snapshot.h"
#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "live_text.h"
#include "overlays.h"
#include "render.h"
#include "search.h"
#include "settings.h"
#include "sheet_ui.h"
#include "tableedit.h"
#include "tabs.h"
#include "utils.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

// The clipboard's older keys (#320): Ctrl+Insert copies, Shift+Insert
// pastes, Shift+Delete cuts. Tinta took Ctrl+C, X and V everywhere and the
// older keys almost nowhere: Live pasted and copied with them, nothing else
// did, and Shift+Delete took a selection away without keeping it.
//
// Every place that copies, cuts or pastes is put through the same steps
// twice, with the letters and with the older keys, and has to end the same.
// What the clipboard held before is put back when the test ends, and no
// message of the test says what was on it.
namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
namespace fs = std::filesystem;

const wchar_t* kSentinel = L"tinta-test-sentinel";

struct Keys {
    const char* name;
    bool older;
};
const Keys kLetters = {"the letters", false}, kOlder = {"the older keys", true};

void copyKey(App& app, const Keys& keys) {
    if (keys.older) keystroke(app, VK_INSERT, {true, false});
    else keystroke(app, 'C', {true, false});
}
void cutKey(App& app, const Keys& keys) {
    if (keys.older) keystroke(app, VK_DELETE, {false, true});
    else keystroke(app, 'X', {true, false});
}
void pasteKey(App& app, const Keys& keys) {
    if (keys.older) keystroke(app, VK_INSERT, {false, true});
    else keystroke(app, 'V', {true, false});
}

void put(App& app, const std::wstring& text) {
    check(copyToClipboard(app.hwnd, text), "the test puts its own text on the clipboard");
}
std::wstring held(App& app) { return readClipboardText(app.hwnd); }
// Whether the clipboard holds what is expected, without saying what it holds
bool holds(App& app, const std::wstring& text) { return held(app) == text; }

// --- The keys themselves -----------------------------------------------------

void letters() {
    check(clipboardKeyLetter(VK_INSERT, true, false) == 'C', "Ctrl+Insert is C");
    check(clipboardKeyLetter(VK_INSERT, false, true) == 'V', "Shift+Insert is V");
    check(clipboardKeyLetter(VK_DELETE, false, true) == 'X', "Shift+Delete is X");
    check(clipboardKeyLetter(VK_INSERT, false, false) == 0 && clipboardKeyLetter(VK_INSERT, true, true) == 0,
          "Insert alone and Ctrl+Shift+Insert are no clipboard keys");
    check(clipboardKeyLetter(VK_DELETE, false, false) == 0 && clipboardKeyLetter(VK_DELETE, true, false) == 0 &&
              clipboardKeyLetter(VK_DELETE, true, true) == 0,
          "Delete, Ctrl+Delete and Ctrl+Shift+Delete are no clipboard keys");
    check(clipboardKeyLetter('C', true, false) == 0 && clipboardKeyLetter(VK_BACK, false, true) == 0,
          "other keys are left alone");
    WPARAM key = VK_INSERT;
    bool ctrl = false, shift = true;
    check(takeClipboardKey(key, ctrl, shift) && key == 'V' && ctrl && !shift, "a handler reads Shift+Insert as Ctrl+V");
    key = VK_DELETE;
    ctrl = false;
    shift = false;
    check(!takeClipboardKey(key, ctrl, shift) && key == VK_DELETE && !ctrl && !shift, "and Delete as Delete");
}

// --- The source editor and Live ----------------------------------------------

const wchar_t* kText = L"alpha beta gamma\nsecond line\n";

void setText(App& app, const std::wstring& text) {
    // Through the keys, so that the source and Live take it alike
    keystroke(app, 'A', {true, false});
    keystroke(app, VK_BACK);
    put(app, text);
    keystroke(app, 'V', {true, true});  // as it is
    check(app.editorText == text, "the editor holds the test's text");
}
// "beta" by the keys: Ctrl+Home, six to the right, four more with Shift
void selectBeta(App& app) {
    keystroke(app, VK_HOME, {true, false});
    for (int i = 0; i < 6; i++) keystroke(app, VK_RIGHT);
    for (int i = 0; i < 4; i++) keystroke(app, VK_RIGHT, {false, true});
}

struct Round {
    bool copied = false, cut = false;
    std::wstring afterCut, afterPaste, table, untouched;
    bool keptWithoutSelection = false;
};
Round editorRound(App& app, const Keys& keys, const std::string& where) {
    const std::string name = where + ", " + keys.name;
    Round r;
    setText(app, kText);
    selectBeta(app);
    put(app, kSentinel);
    copyKey(app, keys);
    r.copied = holds(app, L"beta");
    check(r.copied, name + ": copy puts the selection on the clipboard");
    check(app.editorText == kText, name + ": and leaves the text");
    put(app, kSentinel);
    cutKey(app, keys);
    r.cut = holds(app, L"beta");
    r.afterCut = app.editorText;
    check(r.cut, name + ": cut puts the selection on the clipboard");
    check(r.afterCut == L"alpha  gamma\nsecond line\n", name + ": and takes it out of the text");
    pasteKey(app, keys);
    r.afterPaste = app.editorText;
    check(r.afterPaste == kText, name + ": paste puts it back where it was");

    // Nothing selected: cut keeps the clipboard and the text
    put(app, kSentinel);
    keystroke(app, VK_HOME, {true, false});
    cutKey(app, keys);
    r.keptWithoutSelection = holds(app, kSentinel) && app.editorText == kText;
    check(r.keptWithoutSelection, name + ": with nothing selected cut changes neither the text nor the clipboard");

    // A spreadsheet's range arrives as a table
    setText(app, L"x\n\n");
    keystroke(app, VK_END, {true, false});
    put(app, L"a\tb\nc\td\n");
    pasteKey(app, keys);
    r.table = app.editorText;
    check(r.table.find(L'|') != std::wstring::npos && r.table.find(L'\t') == std::wstring::npos,
          name + ": a pasted spreadsheet range becomes a table");
    return r;
}

void editors(App& app) {
    enterEditMode(app);
    app.editorShowPreview = false;
    for (int live = 0; live < 2; live++) {
        if (editorLiveOn(app) != (live == 1)) keystroke(app, VK_OEM_2, {true, false, false});
        const std::string where = live ? "Live" : "source";
        check(app.editMode && editorLiveOn(app) == (live == 1), where + ": the editor is open");
        const Round a = editorRound(app, kLetters, where);
        const Round b = editorRound(app, kOlder, where);
        check(a.afterCut == b.afterCut && a.afterPaste == b.afterPaste && a.table == b.table,
              where + ": the older keys leave the text as the letters do");

        // What the other Insert and Delete keys did, they do
        setText(app, kText);
        put(app, kSentinel);
        keystroke(app, VK_HOME, {true, false});
        keystroke(app, VK_DELETE);
        check(app.editorText == std::wstring(kText).substr(1) && holds(app, kSentinel),
              where + ": Delete takes the letter after the caret and leaves the clipboard");
        keystroke(app, VK_INSERT);
        keystroke(app, VK_INSERT, {true, true});
        check(app.editorText == std::wstring(kText).substr(1) && holds(app, kSentinel),
              where + ": Insert alone and Ctrl+Shift+Insert do nothing");
        // Ctrl+Shift+V pastes a range as it is, with its tabs
        setText(app, L"x\n\n");
        keystroke(app, VK_END, {true, false});
        put(app, L"a\tb\nc\td\n");
        keystroke(app, 'V', {true, true});
        check(app.editorText.find(L'\t') != std::wstring::npos, where + ": Ctrl+Shift+V still pastes as it is");
    }
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, {true, false, false});
}

// --- A text of the shared engine ---------------------------------------------
//
// Live's page, a sheet's cards and labels and its find field type through
// one engine (live_text.h). Its own key handler is asked here, as a card's
// text asks it

void engineText(App& app) {
    for (const Keys* keys : {&kLetters, &kOlder}) {
        const std::string name = std::string("a card's text, ") + keys->name;
        LiveText e;
        e.multiline = false;
        e.text = L"alpha beta";
        e.anchor = 0;
        e.caret = 5;
        auto press = [&](WPARAM vk, bool ctrl, bool shift) { return live::key(app, app.hwnd, e, vk, ctrl, shift, live::Host{}); };
        auto copy = [&] { return keys->older ? press(VK_INSERT, true, false) : press('C', true, false); };
        auto cut = [&] { return keys->older ? press(VK_DELETE, false, true) : press('X', true, false); };
        auto paste = [&] { return keys->older ? press(VK_INSERT, false, true) : press('V', true, false); };
        put(app, kSentinel);
        check(copy() != live::Result::Unhandled && holds(app, L"alpha") && e.text == L"alpha beta", name + ": copy");
        put(app, kSentinel);
        check(cut() != live::Result::Unhandled && holds(app, L"alpha") && e.text == L" beta", name + ": cut");
        paste();
        paste();
        check(e.text == L"alphaalpha beta", name + ": paste, twice");
        e.caret = e.anchor = 0;
        put(app, kSentinel);
        cut();
        check(e.text == L"alphaalpha beta" && holds(app, kSentinel), name + ": cut with nothing selected changes nothing");
        press(VK_DELETE, false, false);
        check(e.text == L"lphaalpha beta", name + ": Delete takes the letter after the caret");
    }
}

// --- The find bar's fields ---------------------------------------------------

void findFields(App& app) {
    check(app.editMode && !editorLiveOn(app), "find: the source editor is open");
    setText(app, kText);
    for (int field = 0; field < 2; field++) {
        const std::string where = field ? "the replace field" : "the find field";
        std::wstring ends[2];
        for (const Keys* keys : {&kLetters, &kOlder}) {
            const std::string name = where + ", " + keys->name;
            closeSearchInput(app);
            keystroke(app, 'H', {true, false});
            if (field) keystroke(app, VK_TAB);
            std::wstring& text = field ? app.replaceText : app.searchQuery;
            keystroke(app, 'A', {true, false});
            keystroke(app, VK_BACK);
            typeKeys(app, L"needle");
            check(text == L"needle" && app.searchActive, name + ": a word is typed into it");
            keystroke(app, 'A', {true, false});
            put(app, kSentinel);
            copyKey(app, *keys);
            check(holds(app, L"needle") && text == L"needle", name + ": copy");
            put(app, kSentinel);
            cutKey(app, *keys);
            check(holds(app, L"needle") && text.empty(), name + ": cut");
            pasteKey(app, *keys);
            pasteKey(app, *keys);
            check(text == L"needleneedle", name + ": paste, twice");
            keystroke(app, VK_HOME);
            put(app, kSentinel);
            cutKey(app, *keys);
            check(text == L"needleneedle" && holds(app, kSentinel), name + ": cut with nothing selected changes nothing");
            keystroke(app, VK_DELETE);
            check(text == L"eedleneedle", name + ": Delete takes the letter after the caret");
            ends[keys->older] = text;
            check(app.editorText == kText, name + ": the source is left alone");
        }
        check(ends[0] == ends[1], where + ": the older keys end as the letters do");
    }
    closeSearchInput(app);
}

// --- A table cell --------------------------------------------------------------

void tableCell(App& app, const std::wstring& fixtureText) {
    // The fixture's own text again, with its table, beside its preview
    setText(app, fixtureText);
    app.editorShowPreview = true;
    app.editorScrollY = app.scrollY = 0;
    editorReparse(app);
    ensureLayoutComplete(app);
    auto openCell = [&]() {
        ensureLayoutComplete(app);
        for (const auto& cell : app.tableCellRects) {
            if (cell.row != 1 || cell.col != 0) continue;
            const float x = documentViewportX(app) + (cell.rect.left + cell.rect.right) / 2 - app.scrollX;
            const float y = (cell.rect.top + cell.rect.bottom) / 2 - app.scrollY;
            const LPARAM at = MAKELPARAM((int)x, (int)y);
            app.lastClickTime = {};
            app.clickCount = 0;
            handleMouseDown(app, app.hwnd, MK_LBUTTON, at);
            handleMouseUp(app, app.hwnd, 0, at);
            return app.tableEditActive;
        }
        return false;
    };
    std::wstring ends[2];
    for (const Keys* keys : {&kLetters, &kOlder}) {
        const std::string name = std::string("a table cell, ") + keys->name;
        tableEditCancel(app);
        check(openCell(), name + ": the cell opens");
        if (!app.tableEditActive) return;
        keystroke(app, 'A', {true, false});
        const std::wstring cell = app.tableEditText;
        check(cell == L"Copy", name + ": it holds the fixture's word");
        put(app, kSentinel);
        copyKey(app, *keys);
        check(holds(app, cell) && app.tableEditText == cell, name + ": copy");
        put(app, kSentinel);
        cutKey(app, *keys);
        check(holds(app, cell) && app.tableEditText.empty(), name + ": cut");
        pasteKey(app, *keys);
        pasteKey(app, *keys);
        check(app.tableEditText == cell + cell, name + ": paste, twice");
        keystroke(app, 'A', {true, false});
        keystroke(app, VK_LEFT);  // the caret before the first letter, nothing selected
        put(app, kSentinel);
        cutKey(app, *keys);
        check(app.tableEditText == cell + cell && holds(app, kSentinel),
              name + ": cut with nothing selected changes nothing");
        keystroke(app, VK_DELETE);
        check(app.tableEditText == (cell + cell).substr(1), name + ": Delete takes the letter after the caret (" + toUtf8(app.tableEditText) + ", caret " +
                  std::to_string(app.tableEditCaret) + ")");
        ends[keys->older] = app.tableEditText;
        tableEditCancel(app);
    }
    check(ends[0] == ends[1], "a table cell: the older keys end as the letters do");

    // The preview's own selection, copied from the editor
    auto select = [&] {
        app.selAnchor = 0;
        app.selFocus = 12;
        app.hasSelection = true;
    };
    select();
    put(app, kSentinel);
    keystroke(app, 'C', {true, false});
    const std::wstring letters = held(app);
    select();  // a copy lets the selection go
    put(app, kSentinel);
    keystroke(app, VK_INSERT, {true, false});
    check(!letters.empty() && letters != kSentinel && holds(app, letters),
          "the preview's selection: Ctrl+Insert copies what Ctrl+C copies (" + std::to_string(letters.size()) +
              " letters, then " + std::to_string(held(app).size()) + ")");
    app.hasSelection = false;
}

// --- The reading view and its fields -----------------------------------------

void readingView(App& app) {
    check(!app.editMode, "reading: the reading view is open");
    ensureLayoutComplete(app);
    keystroke(app, 'A', {true, false});
    check(app.hasSelection, "reading: Ctrl+A selects the page");
    put(app, kSentinel);
    keystroke(app, 'C', {true, false});
    const std::wstring letters = held(app);
    keystroke(app, 'A', {true, false});  // a copy lets the selection go
    put(app, kSentinel);
    keystroke(app, VK_INSERT, {true, false});
    check(letters.size() > 100 && letters != kSentinel && holds(app, letters),
          "reading: Ctrl+Insert copies what Ctrl+C copies (" + std::to_string(letters.size()) + " letters, then " +
              std::to_string(held(app).size()) + ")");
    put(app, kSentinel);
    keystroke(app, VK_INSERT, {true, true});
    keystroke(app, VK_INSERT);
    check(holds(app, kSentinel), "reading: Ctrl+Shift+Insert and Insert alone copy nothing");
    app.hasSelection = false;

    // A note on the page
    for (const Keys* keys : {&kLetters, &kOlder}) {
        app.annotEditorOpen = true;
        app.annotEditorText = L"note: ";
        app.annotEditorCaret = app.annotEditorText.size();
        put(app, L"pasted");
        pasteKey(app, *keys);
        check(app.annotEditorText == L"note: pasted" && app.annotEditorCaret == 12,
              std::string("a note, ") + keys->name + ": paste");
        app.annotEditorOpen = false;
    }

    // The folder browser's path field
    for (const Keys* keys : {&kLetters, &kOlder}) {
        app.showFolderBrowser = true;
        app.folderBrowserEditingPath = true;
        app.folderBrowserInputSelectAll = false;
        app.folderBrowserInput = L"C:\\";
        put(app, L"docs");
        pasteKey(app, *keys);
        check(app.folderBrowserInput == L"C:\\docs", std::string("the folder field, ") + keys->name + ": paste");
        app.folderBrowserEditingPath = false;
        app.showFolderBrowser = false;
    }
}

// --- A sheet's cards ---------------------------------------------------------

void sheetCards(App& app, const fs::path& fixtures) {
    // The test works on a copy; the fixture stays as it is
    const fs::path dir = fs::current_path() / "sheets";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::copy(fixtures / "sheets", dir, fs::copy_options::recursive, ec);
    check(!ec, "sheet: the fixture copies");
    tabsInit(app);
    tabOpenPath(app, app.hwnd, toUtf8((dir / "Launch plan.smd").wstring()));
    check(sheetActive(app), "sheet: a .smd opens as a sheet");
    SheetDoc* doc = activeSheet(app);
    if (!doc) return;
    size_t counts[2] = {0, 0};
    for (const Keys* keys : {&kLetters, &kOlder}) {
        const std::string name = std::string("sheet, ") + keys->name;
        const size_t before = doc->model.nodes.size();
        doc->selected.clear();
        put(app, keys->older ? L"flowchart TD\n  M[Older] --> N[Keys]\n" : L"flowchart TD\n  A[Plan] --> B[Do]\n");
        pasteKey(app, *keys);
        check(doc->model.nodes.size() == before + 2, name + ": a pasted flowchart arrives as two cards");
        if (doc->model.nodes.size() != before + 2) continue;
        doc->selected = {doc->model.nodes[before].id, doc->model.nodes[before + 1].id};
        put(app, kSentinel);
        copyKey(app, *keys);
        check(!holds(app, kSentinel) && doc->model.nodes.size() == before + 2, name + ": copy takes the two cards");
        put(app, kSentinel);
        cutKey(app, *keys);
        check(!holds(app, kSentinel) && doc->model.nodes.size() == before, name + ": cut takes them off the sheet too");
        pasteKey(app, *keys);
        check(doc->model.nodes.size() == before + 2, name + ": paste puts the two back");
        counts[keys->older] = doc->model.nodes.size() - before;
    }
    check(counts[0] == counts[1] && counts[0] == 2, "sheet: the older keys end as the letters do");
}
}  // namespace

int runClipboardKeysTests(const char* fixtureRoot) {
    HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 2;
    const fs::path fixtures = fixtureRoot;
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Clipboard key tests", WS_POPUP, 0, 0, 1300, 900, nullptr, nullptr,
                               nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1300;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    applyTheme(app, 0);

    letters();
    {
        // Everything the clipboard held, in every format, is put back when
        // this goes out of scope
        ClipboardSnapshot clipboard(app.hwnd);
        check(clipboard.captured, "the clipboard can be kept for the time of the test");
        if (clipboard.captured) {
            const fs::path page = fixtures / "clipboard-keys-320.md";
            check(openDocumentInViewer(app, page.wstring()), "the fixture opens");
            ensureLayoutComplete(app);
            readingView(app);
            enterEditMode(app);
            const std::wstring fixtureText = app.editorText;
            check(fixtureText.find(L"| Copy  |") != std::wstring::npos, "the fixture's table is in the source");
            editors(app);
            engineText(app);
            findFields(app);
            tableCell(app, fixtureText);
            app.editorDirty = false;
            exitEditMode(app);
            check(!app.editMode, "the editor closes");
            sheetCards(app, fixtures);
        }
    }

    app.editorDirty = false;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    OleUninitialize();
    std::cout << "Clipboard keys: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
