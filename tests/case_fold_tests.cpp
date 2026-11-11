// Text is found in any case: the capitals beyond A to Z match their small
// letters in Find and Replace, in Contents, in the results panel, in links
// between notes and in the menus that complete what is typed. Each check
// goes through the keys a reader would press.
#include "ui_scale.h"
#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "folding.h"
#include "frame_preview.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "live_text.h"
#include "overlays.h"
#include "render.h"
#include "search.h"
#include "search_panel.h"
#include "settings.h"
#include "sheet_ui.h"
#include "sidepanels.h"
#include "tabs.h"
#include "utils.h"
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace sheet;
using namespace sheetui;

int failures = 0;
std::string scenario;

void check(bool ok, const std::string& message) {
    if (ok) return;
    if (++failures <= 60) std::cout << "FAIL case fold: " << scenario << ": " << message << "\n";
}

// Wide text for a message: the letters outside ASCII by their code
std::string shown(const std::wstring& text) {
    std::string out;
    char code[16];
    for (wchar_t c : text) {
        if (c >= 32 && c < 127) {
            out += (char)c;
            continue;
        }
        snprintf(code, sizeof(code), "<%04X>", (unsigned)c);
        out += code;
    }
    return out;
}

// The words of find-any-case.md's table: each is written there small,
// Capitalised and in CAPITALS, and the table says how often in all
struct Word {
    const wchar_t* little;
    const wchar_t* capitals;
    size_t matches;
    const char* language;
};
const Word kWords[] = {
    {L"\u00FCber", L"\u00DCBER", 7, "German"},
    {L"\u010Daj", L"\u010CAJ", 6, "Croatian"},
    {L"\u00E9cole", L"\u00C9COLE", 6, "French"},
    {L"\u03B5\u03BB\u03BB\u03AC\u03B4\u03B1", L"\u0395\u039B\u039B\u0386\u0394\u0391", 4, "Greek"},
    {L"\u043F\u0440\u0438\u0432\u0435\u0442", L"\u041F\u0420\u0418\u0412\u0415\u0422", 6, "Russian"},
    {L"tinta", L"TINTA", 3, "English"},
};

// --- The table --------------------------------------------------------------

void foldTable() {
    scenario = "table";
    const wchar_t* fold = caseFoldTable();
    bool ascii = true;
    for (unsigned c = 0; c < 128; ++c) {
        const wchar_t want = c >= 'A' && c <= 'Z' ? (wchar_t)(c + 32) : (wchar_t)c;
        if (fold[c] != want) ascii = false;
    }
    check(ascii, "A to Z fold to a to z, and the rest of ASCII stays");
    bool halves = true;
    for (unsigned c = 0xD800; c <= 0xDFFF; ++c) {
        if (fold[c] != (wchar_t)c) halves = false;
    }
    check(halves, "half a surrogate pair has no case");

    // Pairs every version of Windows lowers the same way
    struct Pair {
        const wchar_t* letters;  // the capital, then the small letter
        const char* name;
    };
    const Pair pairs[] = {
        {L"\u00C4\u00E4", "A umlaut"}, {L"\u00D6\u00F6", "O umlaut"}, {L"\u00DC\u00FC", "U umlaut"}, {L"\u00C9\u00E9", "E acute"},
        {L"\u00D1\u00F1", "N tilde"}, {L"\u010C\u010D", "C caron"}, {L"\u0160\u0161", "S caron"}, {L"\u017D\u017E", "Z caron"},
        {L"\u0110\u0111", "D stroke"}, {L"\u0391\u03B1", "alpha"}, {L"\u03A9\u03C9", "omega"}, {L"\u0386\u03AC", "alpha tonos"},
        {L"\u0410\u0430", "Cyrillic a"}, {L"\u042F\u044F", "Cyrillic ya"}, {L"\u0401\u0451", "Cyrillic yo"},
    };
    for (const Pair& pair : pairs) {
        const wchar_t capital = pair.letters[0], little = pair.letters[1];
        check(capital != little && fold[capital] == little, std::string(pair.name) + ": the capital folds to the small letter");
        check(fold[little] == little, std::string(pair.name) + ": the small letter stays");
    }
    // Letters with no small form of one unit, a sign and an ideograph
    for (wchar_t same : std::wstring(L"\u00DF\u00D7\u4E2D")) {
        check(fold[same] == same, "a unit without a small form stays: " + shown(std::wstring(1, same)));
    }

    // A unit in gives a unit out, so a place in folded text is the same
    // place in the text: a NUL and an emoji's two halves pass through
    std::wstring mixed = L"Ab\u00DC";
    mixed += (wchar_t)0;
    mixed += (wchar_t)0xD83D;
    mixed += (wchar_t)0xDE00;
    mixed += L"\u0416!";
    std::wstring want = L"ab\u00FC";
    want += (wchar_t)0;
    want += (wchar_t)0xD83D;
    want += (wchar_t)0xDE00;
    want += L"\u0436!";
    check(toLower(mixed) == want, "text folds unit for unit: " + shown(toLower(mixed)));
    check(toLower(std::wstring()).empty(), "nothing folds to nothing");
}

// --- The reading view -------------------------------------------------------

bool shows(const App& app, const wchar_t* text) {
    return app.docText.find(text) != std::wstring::npos;
}

int heading(const App& app, const wchar_t* text) {
    for (size_t i = 0; i < app.headings.size(); i++) {
        if (app.headings[i].text == text) return (int)i;
    }
    return -1;
}

// Esc with nothing open closes the window: it goes only to an open bar
void closeFind(App& app) {
    check(app.showSearch, "Find is open");
    if (app.showSearch) keystroke(app, VK_ESCAPE);
    check(!app.showSearch && !app.showSearchPanel, "Esc closes Find");
}

// The page as a frame draws it: the marks of the matches in view, then the
// bar and the panels over them
void readerFrame(App& app, const fs::path& path) {
    if (app.layoutDirty) ensureLayoutComplete(app);
    app.searchAnimation = 1.0f;
    if (app.showToc || app.showSearchPanel) app.tocAnimation = 1.0f;
    const bool saved = saveFramePreview(app, path, [&] {
        D2D1_MATRIX_3X2_F before;
        app.renderTarget->GetTransform(&before);
        app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(documentViewportX(app), 0) * before);
        std::vector<SearchHighlight> marks;
        searchHighlightRects(app, app.scrollY - 50.0f, app.scrollY + app.height + 50.0f, marks);
        for (const SearchHighlight& mark : marks) {
            const bool current = app.searchCurrentIndex >= 0 && mark.matchIndex == (size_t)app.searchCurrentIndex;
            app.brush->SetColor(current ? D2D1::ColorF(1.0f, 0.6f, 0.0f, 0.5f) : D2D1::ColorF(1.0f, 0.9f, 0.0f, 0.3f));
            app.renderTarget->FillRectangle(D2D1::RectF(mark.rect.left - app.scrollX, mark.rect.top - app.scrollY,
                                                        mark.rect.right - app.scrollX, mark.rect.bottom - app.scrollY),
                                            app.brush);
        }
        app.renderTarget->SetTransform(before);
        if (app.showToc) renderToc(app);
        if (app.showSearchPanel) renderSearchPanel(app);
        if (app.showSearch) renderSearchOverlay(app);
    });
    check(saved, "frame saved: " + path.filename().string());
}

void reader(App& app, const fs::path& out) {
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            app.layoutDirty = true;
            ensureLayoutComplete(app);
            const std::string where = "theme" + std::to_string(theme) + "-width" + std::to_string(width);
            for (const Word& word : kWords) {
                for (const wchar_t* typed : {word.little, word.capitals}) {
                    scenario = "reader " + where + " " + word.language + (typed == word.little ? " small" : " capitals");
                    keystroke(app, 'F', {true, false, false});
                    check(app.showSearch && app.searchActive, "Ctrl+F opens Find");
                    typeKeys(app, typed);
                    check(app.searchQuery == typed, "Find holds what was typed: " + shown(app.searchQuery));
                    check(app.searchMatches.size() == word.matches,
                          "matches: " + std::to_string(app.searchMatches.size()) + ", the table says " + std::to_string(word.matches));
                    // Each is the word in some case, and is marked on the page
                    const std::wstring folded = toLower(typed);
                    bool words = true, marked = true;
                    for (const auto& match : app.searchMatches) {
                        if (toLower(app.docText.substr(match.startPos, match.length)) != folded) words = false;
                        const D2D1_RECT_F& mark = match.highlightRect;
                        if (mark.right <= mark.left || mark.bottom <= mark.top) marked = false;
                    }
                    check(words, "every match is the word");
                    check(marked, "every match has its mark on the page");
                    if (&word == &kWords[0] && typed == word.little) readerFrame(app, out / ("reader-" + where + ".png"));
                    closeFind(app);
                }
            }
        }
    }
    applyTheme(app, 0);
    app.width = 1050;
    app.layoutDirty = true;
    ensureLayoutComplete(app);
}

// A folded section opens for the word it hides (#316), in either case
void folded(App& app, const fs::path& out) {
    scenario = "folded";
    foldAllSections(app, true);
    layoutDocument(app);
    check(!shows(app, L"\u017DIRAFA") && !shows(app, L"Die Suche"), "folded, the sections' text is off the page");
    keystroke(app, 'F', {true, false, false});
    typeKeys(app, L"\u017Eirafa");
    check(app.searchMatches.size() == 1, "the hidden word is found: " + std::to_string(app.searchMatches.size()));
    check(shows(app, L"\u017DIRAFA"), "the section holding it opens");
    check(!shows(app, L"Die Suche") && !shows(app, L"\u03B3\u03C1\u03AC\u03C6\u03B5\u03C4\u03B1\u03B9"), "the sections without it stay folded");
    if (app.searchMatches.size() == 1) {
        scrollToCurrentMatch(app);
        app.scrollY = app.targetScrollY;
    }
    readerFrame(app, out / "reader-folded.png");
    closeFind(app);
    foldAllSections(app, false);
    layoutDocument(app);
    app.scrollY = app.targetScrollY = 0;
}

// Contents narrows to the headings holding what is typed
void contents(App& app, const fs::path& out) {
    struct Filter {
        const wchar_t* typed;
        const wchar_t* heading;
    };
    // Small letters typed for a heading's capital, and capitals for its small letters
    const Filter filters[] = {
        {L"\u00FCber", L"\u00DCber uns"},
        {L"\u0395\u039B\u039B\u0386\u0394\u0391", L"\u0395\u03BB\u03BB\u03AC\u03B4\u03B1"},
        {L"\u043F\u0440\u0438\u0432\u0435\u0442", L"\u041F\u0440\u0438\u0432\u0435\u0442"},
        {L"\u00E9cole", L"\u00C9cole"},
    };
    for (const Filter& filter : filters) {
        scenario = "contents " + shown(filter.typed);
        check(!app.showToc && !app.showSearch, "nothing is open");
        if (app.showToc || app.showSearch) return;
        keystroke(app, VK_TAB);
        check(app.showToc, "Tab opens Contents");
        if (!app.showToc) return;
        typeKeys(app, filter.typed);
        check(app.tocFilter == filter.typed, "the filter holds what was typed: " + shown(app.tocFilter));
        check(tocListContentHeight(app) == dpi(app, uiscale::rowList), "one heading is left");
        if (&filter == &filters[0]) readerFrame(app, out / "contents-filter.png");
        const int want = heading(app, filter.heading);
        check(want >= 0, "the heading is in the document");
        keystroke(app, VK_RETURN);
        check(!app.showToc, "Enter goes to the heading and closes Contents");
        check(app.tocSpyOverride == want, "the heading gone to is the one holding the word");
        // Left open by a miss: the first Esc empties the filter, the next closes
        for (int press = 0; press < 2 && app.showToc; press++) keystroke(app, VK_ESCAPE);
    }
    app.scrollY = app.targetScrollY = 0;
}

// --- The folder's other notes -----------------------------------------------

// The scan runs on its own thread and posts what it found
bool scanOtherFiles(App& app) {
    startFolderSearchScan(app);
    for (int i = 0; i < 500 && app.folderSearchPending; i++) {
        MSG msg;
        while (PeekMessageW(&msg, app.hwnd, WM_APP_FOLDER_SEARCH, WM_APP_FOLDER_SEARCH, PM_REMOVE))
            completeFolderSearch(app, (void*)msg.lParam);
        if (app.folderSearchPending) Sleep(10);
    }
    return !app.folderSearchPending;
}

int fileIndex(const App& app, const std::wstring& name) {
    for (size_t i = 0; i < app.folderResults.size(); i++) {
        if (app.folderResults[i].fileName == name) return (int)i;
    }
    return -1;
}

void otherFiles(App& app, const fs::path& out) {
    scenario = "other files";
    keystroke(app, 'F', {true, true, false});
    check(app.showSearch && app.searchActive && app.showSearchPanel, "Ctrl+Shift+F opens the results panel");
    if (!app.showSearchPanel) return;
    typeKeys(app, L"\u00FCber");
    check(app.searchMatches.size() == kWords[0].matches, "the document's own matches are listed");
    check(app.folderSearchPending, "the folder is looked through once the typing rests");
    check(scanOtherFiles(app), "the look through the folder ends");
    // Both notes write the word with a capital only
    const int tea = fileIndex(app, L"\u00DCber Tee.md"), greetings = fileIndex(app, L"Gr\u00FC\u00DFe.md");
    check(tea >= 0 && app.folderResults[tea].totalMatches == 3,
          "the note on tea holds it three times: " + std::to_string(tea < 0 ? -1 : app.folderResults[tea].totalMatches));
    check(greetings >= 0 && app.folderResults[greetings].totalMatches == 1,
          "the greetings hold it once: " + std::to_string(greetings < 0 ? -1 : app.folderResults[greetings].totalMatches));
    bool snippets = !app.folderResults.empty();
    for (const auto& file : app.folderResults) {
        for (const auto& match : file.matches) {
            if (match.matchStart + match.matchLen > match.snippet.size() ||
                toLower(match.snippet.substr(match.matchStart, match.matchLen)) != L"\u00FCber")
                snippets = false;
        }
    }
    check(snippets, "each line shown marks the word where it stands");
    int fileRows = 0;
    for (const SearchPanelRow& row : searchPanelRows(app)) {
        if (row.kind == SPR_FILE) fileRows++;
    }
    check(fileRows == 2, "the panel lists both notes: " + std::to_string(fileRows));
    readerFrame(app, out / "results-panel.png");
    closeFind(app);
}

// A note is linked by its name in either case. Windows finds a file's own
// name that way; the name without its extension is matched by Tinta
void wikiLink(App& app, const fs::path& doc) {
    scenario = "wiki link";
    std::string url;
    for (const auto& link : app.linkRects) {
        if (link.url.rfind("wiki:", 0) == 0) url = link.url;
    }
    check(url == "wiki:" + toUtf8(L"\u0161uma"), "the link to the note is on the page: " + shown(toWide(url)));
    if (url.empty()) return;
    followDocumentLink(app, app.hwnd, url);
    ensureLayoutComplete(app);
    const std::wstring opened = fs::path(toWide(app.currentFile)).filename().wstring();
    check(opened == L"\u0160uma.txt", "the link opens the note named with a capital: " + shown(opened));
    check(shows(app, L"plain-text file"), "and its text is on the page");
    check(openDocumentInViewer(app, doc.wstring()), "the document opens again");
    ensureLayoutComplete(app);
}

// --- The source editor ------------------------------------------------------

void editorFrame(App& app, const fs::path& path) {
    app.searchAnimation = 1.0f;
    app.cursorBlinkOn = true;
    check(saveFramePreview(app, path, [&] { renderSearchOverlay(app); }), "frame saved: " + path.filename().string());
}

void sourceEditor(App& app, const fs::path& out) {
    scenario = "source";
    enterEditMode(app);
    check(app.editMode && !editorLiveOn(app), "the source editor opens");
    if (!app.editMode) return;
    editorReparse(app);
    ensureLayoutComplete(app);
    for (const Word& word : kWords) {
        for (const wchar_t* typed : {word.little, word.capitals}) {
            scenario = std::string("source ") + word.language + (typed == word.little ? " small" : " capitals");
            keystroke(app, 'F', {true, false, false});
            check(app.showSearch && app.searchActive, "Ctrl+F opens Find");
            typeKeys(app, typed);
            check(app.searchQuery == typed, "Find holds what was typed: " + shown(app.searchQuery));
            check(app.editorSearchMatches.size() == word.matches,
                  "matches: " + std::to_string(app.editorSearchMatches.size()) + ", the table says " + std::to_string(word.matches));
            const std::wstring folded = toLower(typed);
            bool words = true;
            size_t after = 0;
            for (const auto& match : app.editorSearchMatches) {
                if (match.startPos < after || match.length != folded.size() ||
                    toLower(app.editorText.substr(match.startPos, match.length)) != folded)
                    words = false;
                after = match.startPos + match.length;
            }
            check(words, "the matches are the word, each after the one before");
            if (&word == &kWords[3] && typed == word.capitals) editorFrame(app, out / "source-find.png");
            closeFind(app);
        }
    }

    // Replace all writes one spelling over every case of the word
    scenario = "source replace";
    keystroke(app, 'H', {true, false, false});
    check(app.showSearch && app.searchActive && app.searchReplaceMode, "Ctrl+H opens Replace");
    typeKeys(app, L"\u00F6l");
    check(app.editorSearchMatches.size() == 3, "the oil is there in three cases: " + std::to_string(app.editorSearchMatches.size()));
    keystroke(app, VK_TAB);
    typeKeys(app, L"Oel");
    check(app.replaceFieldActive && app.replaceText == L"Oel", "Tab moves on to the words to put in");
    keystroke(app, VK_RETURN, {true, false, false});
    check(app.editorText.find(L"- Oel, Oel und Oel ebenso") != std::wstring::npos, "Ctrl+Enter replaces each of them");
    check(app.editorText.find(L"\u00D6l") == std::wstring::npos && app.editorText.find(L"\u00D6L") == std::wstring::npos, "none is left");
    closeFind(app);

    // The file keeps its text: the test changes nothing on disk
    app.editorDirty = false;
    exitEditMode(app);
    check(!app.editMode, "the editor closes");
}


// --- The Live editor --------------------------------------------------------

void drawLive(App& app) {
    app.renderTarget->Clear(app.theme.background);
    D2D1_MATRIX_3X2_F saved;
    app.renderTarget->GetTransform(&saved);
    app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(documentViewportX(app), 0) * saved);
    renderLivePage(app);
    app.renderTarget->SetTransform(saved);
    renderTabStrip(app);
    app.editRailAnim = 1.0f;
    app.editRailHover = 0;
    renderEditRail(app);
    renderEditCtxMenu(app);
    renderLiveMenu(app);
}

void paintLive(App& app) {
    if (app.layoutDirty) ensureLayoutComplete(app);
    app.renderTarget->BeginDraw();
    drawLive(app);
    app.renderTarget->EndDraw();
}

void liveFrame(App& app, const fs::path& path) {
    if (app.layoutDirty) ensureLayoutComplete(app);
    check(saveFramePreview(app, path, [&] { drawLive(app); }), "frame saved: " + path.filename().string());
}

void liveEditor(App& app, const fs::path& out) {
    scenario = "live";
    enterEditMode(app);
    if (app.editMode) setEditorLive(app, app.hwnd, true);
    check(app.editMode && editorLiveOn(app), "the Live editor opens");
    if (!app.editMode || !editorLiveOn(app)) return;
    paintLive(app);

    // The menu is searched by the names it shows, here the German ones
    const int english = app.currentLanguageIndex;
    const int german = languageIndexById("de");
    check(german >= 0, "German is among the languages");
    const std::wstring last = L"nowhere else in this file.";
    const size_t line = app.editorText.find(last);
    check(line != std::wstring::npos, "the document's last line is in the editor");
    if (german >= 0 && line != std::wstring::npos) {
        app.currentLanguageIndex = german;
        app.editorCursorPos = app.editorSelStart = app.editorSelEnd = line + last.size();
        app.editorHasSelection = false;
        paintLive(app);
        keystroke(app, VK_RETURN);
        keystroke(app, VK_RETURN);
        typeKeys(app, L"/\u00FCb");
        paintLive(app);
        const LiveText& slash = liveText(app);
        check(slash.popup == LiveText::Popup::Slash, "a small letter keeps the menu open on the names its capital begins");
        check(!slash.choices.empty() && slash.choices[0].label == L"\u00DCberschrift 1" && slash.choices[0].insert == live::INS_H1,
              "the first heading leads: " + (slash.choices.empty() ? std::string("nothing") : shown(slash.choices[0].label)));
        liveFrame(app, out / "live-slash.png");
        keystroke(app, VK_RETURN);
        typeKeys(app, L"Neu");
        check(app.editorText.find(L"\n# Neu") != std::wstring::npos, "Enter writes the heading");

        // A note is offered by a small letter for its capital, ahead of the
        // names that only hold the letter
        keystroke(app, VK_RETURN);
        typeKeys(app, L"Siehe [[\u00FC");
        paintLive(app);
        const LiveText& names = liveText(app);
        check(names.popup == LiveText::Popup::Names, "the notes beside the document are offered");
        check(names.choices.size() == 2 && names.choices[0].label == L"\u00DCber Tee" && names.choices[1].label == L"Gr\u00FC\u00DFe",
              "the note named with the capital comes first, of " + std::to_string(names.choices.size()));
        liveFrame(app, out / "live-names.png");
        keystroke(app, VK_RETURN);
        check(app.editorText.find(L"Siehe [[\u00DCber Tee]]") != std::wstring::npos, "Enter writes its name");
    }

    // The file keeps its text: the test changes nothing on disk
    app.currentLanguageIndex = english;
    app.editorDirty = false;
    setEditorLive(app, app.hwnd, false);
    exitEditMode(app);
    check(!app.editMode, "the editor closes");
}

// --- Sheets -----------------------------------------------------------------

void paintSheet(App& app) {
    sheetPrepareFrame(app);
    app.renderTarget->BeginDraw();
    renderSheet(app);
    app.renderTarget->EndDraw();
}

void sheetFrame(App& app, const fs::path& path) {
    sheetPrepareFrame(app);
    // Let the cards' content finish drawing at this zoom
    for (int pass = 0; pass < 20; ++pass) {
        SheetDoc* doc = activeSheet(app);
        if (!doc) break;
        doc->zoomChangedMs = 0;
        renderCardBitmaps(app, *doc, 400);
    }
    check(saveFramePreview(app, path, [&] { renderSheet(app); renderTabStrip(app); }), "frame saved: " + path.filename().string());
}

// The camera arrives where it was sent
void settle(App& app) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) return;
    doc->flight.active = false;
    if (doc->flight.durationMs > 0) {
        doc->camX = doc->flight.toX;
        doc->camY = doc->flight.toY;
        doc->zoom = doc->flight.toZoom;
    }
    doc->glide.active = false;
}

void sheets(App& app, const fs::path& dir, const fs::path& out) {
    scenario = "sheet";
    tabOpenPath(app, app.hwnd, toUtf8((dir / "Teeplan.smd").wstring()));
    check(sheetActive(app), "the sheet opens");
    SheetDoc* opened = activeSheet(app);
    if (!opened) return;
    SheetDoc& doc = *opened;
    pollFolder(app, doc, true);
    paintSheet(app);
    settle(app);

    // Find lights each card and group that holds the word, in any case
    keystroke(app, 'F');
    check(doc.finding && doc.editing == SheetDoc::Edit::Find, "F opens Find");
    typeKeys(app, L"\u00FCber");
    settle(app);
    const std::set<std::string> found(doc.found.begin(), doc.found.end());
    std::string lit;
    for (const std::string& id : found) lit += " " + id;
    check(found == std::set<std::string>{"tee", "note", "more", "overview"}, "Find lights the three cards and their group:" + lit);
    sheetFrame(app, out / "sheet-find.png");
    if (doc.finding) keystroke(app, VK_ESCAPE);
    check(!doc.finding, "Esc closes Find");

    // A card that names a note in small letters is joined to that note's card
    const auto ghosts = ghostLinks(app, doc);
    check(ghosts.size() == 1 && ghosts[0].first == "more" && ghosts[0].second == "tee",
          "the card that names the note is joined to it: " + std::to_string(ghosts.size()));

    // In a card, [[ offers the notes as Live does
    scenario = "sheet names";
    doc.selected = {"note"};
    startEdit(app, doc, SheetDoc::Edit::Card, "note");
    check(doc.editing == SheetDoc::Edit::Card, "a card opens for typing");
    if (doc.editing != SheetDoc::Edit::Card) return;
    doc.edit.caret = doc.edit.anchor = doc.edit.text.size();
    typeKeys(app, L" [[\u00FC");
    check(doc.edit.popup == LiveText::Popup::Names, "the notes are offered");
    check(doc.edit.choices.size() == 2 && doc.edit.choices[0].label == L"\u00DCber Tee" && doc.edit.choices[1].label == L"Gr\u00FC\u00DFe",
          "the note named with the capital comes first, of " + std::to_string(doc.edit.choices.size()));
    sheetFrame(app, out / "sheet-names.png");
    keystroke(app, VK_RETURN);
    check(doc.edit.text.find(L"[[\u00DCber Tee]]") != std::wstring::npos, "Enter writes its name");
    keystroke(app, VK_ESCAPE);
    check(doc.editing == SheetDoc::Edit::None, "Esc leaves the card");
    if (doc.editing != SheetDoc::Edit::None) return;

    // Open note, from the menu of a link written in small letters, goes to
    // the note's card on this sheet. It used to open the file in a tab of its
    // own, under the name as the link spelt it
    scenario = "sheet link";
    doc.selected = {"more"};
    startEdit(app, doc, SheetDoc::Edit::Card, "more");
    const size_t link = doc.editing == SheetDoc::Edit::Card ? doc.edit.text.find(L"[[") : std::wstring::npos;
    check(link != std::wstring::npos, "the card with the link opens for typing");
    if (link == std::wstring::npos) return;
    doc.edit.caret = doc.edit.anchor = link + 4;
    paintSheet(app);
    handleContextMenu(app, app.hwnd, MAKELPARAM(-1, -1));
    check(doc.edit.menu.open && doc.edit.menu.keys && !doc.edit.menu.panels.empty(), "the menu key opens the text's menu");
    if (!doc.edit.menu.open || doc.edit.menu.panels.empty()) return;
    int row = -1;
    for (size_t i = 0; i < doc.edit.menu.panels[0].rows.size(); i++) {
        if (doc.edit.menu.panels[0].rows[i].id == live::CMD_OPEN_LINK) row = (int)i;
    }
    check(row >= 0, "the menu offers to open the note");
    if (row < 0) return;
    for (int i = 0; i < 40 && doc.edit.menu.panels[0].focus != row; i++) keystroke(app, VK_DOWN);
    check(doc.edit.menu.panels[0].focus == row, "the arrow keys reach that row");
    sheetFrame(app, out / "sheet-open-note.png");
    if (doc.edit.menu.panels[0].focus != row) return;
    keystroke(app, VK_RETURN);
    check(sheetActive(app) && activeSheet(app) == &doc, "the sheet stays in front");
    if (!sheetActive(app) || activeSheet(app) != &doc) return;
    check(doc.selected == std::vector<std::string>{"tee"}, "and the note's card is the one selected");
}

void everything(App& app, const char* fixtureDir) {
    scenario = "setup";
    std::error_code ec;
    // With its settings beside it the test keeps to its own folder, and
    // leaves the user's recent files and reading positions alone
    const bool own = fs::equivalent(fs::path(tintaConfigDir()), fs::current_path(), ec);
    check(own, "the settings are the ones beside the test");
    if (!own) return;
    // German, French, Italian and Russian are built in, and load as the app loads them
    loadLanguageOverrides();

    const fs::path dir = fs::current_path() / "notes";
    const fs::path out = fs::current_path() / "frames";
    fs::remove_all(dir, ec);
    fs::remove_all(out, ec);
    ec.clear();
    fs::copy(fixtureDir, dir, fs::copy_options::recursive, ec);
    check(!ec, "the fixture is copied");
    fs::create_directories(out, ec);
    const fs::path doc = dir / "find-any-case.md";

    tabsInit(app);
    applyTheme(app, 0);
    check(openDocumentInViewer(app, doc.wstring()), "the fixture opens");
    if (app.currentFile.empty()) return;
    ensureLayoutComplete(app);

    reader(app, out);
    folded(app, out);
    contents(app, out);
    otherFiles(app, out);
    wikiLink(app, doc);
    sourceEditor(app, out);
    liveEditor(app, out);
    sheets(app, dir, out);
}

}  // namespace

int runCaseFoldTests(const char* fixtureDir) {
    foldTable();

    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Case fold tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    everything(app, fixtureDir);

    app.editorDirty = false;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Case fold: " << failures << " failures\n";
    return failures ? 1 : 0;
}
