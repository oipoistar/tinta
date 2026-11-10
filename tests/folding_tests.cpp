// Folding sections in the reading view (#316): which headings get an arrow,
// what a fold hides and what it keeps, the ways back to hidden text
// (Contents, links, search), the editor and print showing everything, and
// the folds a document keeps for the session.
#include "d2d_init.h"
#include "document.h"
#include "folding.h"
#include "overlays.h"
#include "render.h"
#include "search.h"
#include "selection.h"
#include "settings.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0, checks = 0;
std::string scenario;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) {
        if (failures < 40) std::cerr << "FAIL: " << scenario << ": " << message << '\n';
        ++failures;
    }
}

// The index of the nth heading with this text, else -1
int heading(const App& app, const wchar_t* text, int nth = 0) {
    for (size_t i = 0; i < app.headings.size(); ++i) {
        if (app.headings[i].text == text && nth-- == 0) return (int)i;
    }
    return -1;
}
std::string idOf(const App& app, const wchar_t* text, int nth = 0) {
    const int i = heading(app, text, nth);
    return i >= 0 ? app.headings[i].id : std::string();
}
int arrowOf(const App& app, int headingIndex) {
    for (size_t i = 0; i < app.foldArrows.size(); ++i) {
        if ((int)app.foldArrows[i].heading == headingIndex) return (int)i;
    }
    return -1;
}
bool shows(const App& app, const wchar_t* text) { return app.docText.find(text) != std::wstring::npos; }
std::vector<std::string> ids(const App& app) {
    std::vector<std::string> out;
    for (const auto& h : app.headings) out.push_back(h.id);
    return out;
}

// What is laid out still selects where it is clicked (#318): each text
// rectangle in one line bucket, the one of its line
void selectionHolds(App& app) {
    std::vector<int> seen(app.textRects.size(), 0);
    bool onLine = true;
    for (const auto& bucket : app.lineBuckets) {
        for (size_t idx : bucket.textRectIndices) {
            if (idx >= seen.size()) {
                onLine = false;
                continue;
            }
            ++seen[idx];
            if (std::abs(app.textRects[idx].rect.top - bucket.top) > 5.0f) onLine = false;
        }
    }
    check(onLine && std::all_of(seen.begin(), seen.end(), [](int n) { return n == 1; }),
          "every rectangle sits in one line bucket, its line's");
    bool lands = true;
    for (const auto& r : app.textRects) {
        if (!r.docLength) continue;
        const size_t off = selectionOffsetAtPoint(app, (r.rect.left + r.rect.right) / 2, (r.rect.top + r.rect.bottom) / 2);
        lands = lands && off >= r.docStart && off <= r.docStart + r.docLength;
    }
    check(lands, "a click on any word laid out lands in it");
}
}  // namespace

int runFoldingTests(const char* fixture) {
    auto state = std::make_unique<App>();
    App& app = *state;
    // A window and its target, as the viewer has: the front matter's card
    // lays out with the interface's formats
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Folding tests", WS_POPUP, 0, 0, 1050, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    updateTextFormats(app);
    std::ifstream file(fixture);
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    check(!source.empty(), "the folding fixture opens");
    // As the viewer reads it: the front matter apart from the page
    auto doc = parseDocument(app.parser, source, std::string(fixture));
    check(doc.success, "the folding fixture parses");
    app.root = doc.root;
    app.currentFile = fixture;
    app.height = 900;
    app.foldSections = true;

    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            scenario = "theme" + std::to_string(theme) + "-width" + std::to_string(width);

            // Open: an arrow beside each heading with something under it
            foldAllSections(app, false);
            layoutDocument(app);
            const std::vector<std::string> openIds = ids(app);
            const float openHeight = app.contentHeight;
            check(app.headings.size() == 8, "the fixture's eight headings count");
            check(app.foldArrows.size() == 7, "seven headings have a section to fold");
            check(arrowOf(app, heading(app, L"Empty section")) < 0, "a heading with nothing under it has no arrow");
            check(idOf(app, L"Tables", 1) == idOf(app, L"Tables") + "-1", "the second Tables heading is numbered");
            check(shows(app, L"zanzibar") && shows(app, L"The first footnote."), "an open page shows everything");
            check(!anySectionFolded(app), "nothing is folded yet");
            bool placed = true;
            for (int i = 0; i < (int)app.foldArrows.size(); ++i) {
                const auto& fold = app.foldArrows[i];
                const float cx = (fold.arrow.left + fold.arrow.right) / 2, cy = (fold.arrow.top + fold.arrow.bottom) / 2;
                placed = placed && fold.arrow.left >= 0.0f && fold.arrow.right <= app.layoutIndent &&
                         cy >= fold.line.top && cy <= fold.line.bottom && foldArrowAt(app, cx, cy) == i &&
                         foldLineAt(app, app.layoutIndent + 10.0f, cy) == i;
            }
            check(placed, "each arrow sits in the margin beside its heading's first line, and is hit there");
            check(contextMenuItemEnabled(app, CTX_FOLD_ALL), "Fold all sections is offered on a page with folds");

            // Fold the first Tables section through its arrow
            const int tables = heading(app, L"Tables");
            foldArrowClick(app, arrowOf(app, tables));
            layoutDocument(app);
            check(headingFolded(app, idOf(app, L"Tables")) && app.foldArrows[arrowOf(app, tables)].folded,
                  "a click on the arrow folds the section");
            check(!shows(app, L"zanzibar") && !shows(app, L"def fold") && !shows(app, L"A heading under a heading"),
                  "the folded section's table, text, nested heading, quote and code are gone");
            check(shows(app, L"Empty section") && shows(app, L"tables-1") && shows(app, L"The first footnote."),
                  "the sections after it stay");
            check(ids(app) == openIds, "slugs stay as they were open");
            const int deep = heading(app, L"Deep inside");
            check(deep >= 0 && app.headings[deep].hiddenUnder == tables &&
                      app.headings[deep].y == app.headings[tables].y,
                  "the hidden heading counts, at the folded heading's place");
            check(arrowOf(app, deep) < 0, "a hidden heading has no arrow");
            check(app.contentHeight < openHeight, "the page is shorter");
            float anchorY = -1.0f;
            check(documentTargetY(app, "hidden-anchor", anchorY) && anchorY == app.headings[tables].y,
                  "an anchor in a folded section leads to its heading");
            selectionHolds(app);

            // A link to the hidden heading opens what hides it
            float deepY = -1.0f;
            check(documentTargetY(app, "deep-inside", deepY), "the hidden heading's link resolves");
            check(!headingFolded(app, idOf(app, L"Tables")) && heading(app, L"Deep inside") >= 0 &&
                      app.headings[heading(app, L"Deep inside")].hiddenUnder < 0 &&
                      deepY == app.headings[heading(app, L"Deep inside")].y && shows(app, L"zanzibar"),
                  "following it opens the section and lands on the heading");

            // Fold all: only the top-level headings show, and the footnotes
            foldAllSections(app, true);
            layoutDocument(app);
            int visible = 0;
            for (const auto& h : app.headings) visible += h.hiddenUnder < 0 ? 1 : 0;
            check(visible == 2 && app.foldArrows.size() == 2 && app.headings.size() == 8,
                  "folded everywhere, the two top-level headings show and all eight count");
            check(shows(app, L"The first footnote.") && shows(app, L"The second footnote."),
                  "the footnotes stay when every section folds");
            float backY = -1.0f;
            check(documentTargetY(app, "tinta-fnref-2-1", backY) &&
                      backY == app.headings[heading(app, L"A second part")].y,
                  "a footnote's way back leads to the folded heading over its reference");
            check(!shows(app, L"Text under the second top-level heading"), "the second part's text is folded");
            check(ids(app) == openIds, "slugs stay as they were open, folded everywhere");
            check(anySectionFolded(app), "Fold all reads as folded");
            selectionHolds(app);

            // Contents: going to a heading two folds deep opens just those two
            const int deepIndex = heading(app, L"Deep inside");
            const float deepTarget = headingTargetY(app, (size_t)deepIndex);
            check(app.headings[deepIndex].hiddenUnder < 0 && deepTarget == app.headings[deepIndex].y,
                  "Contents reaches a heading two folds deep");
            check(!headingFolded(app, idOf(app, L"Folding sections")) && !headingFolded(app, idOf(app, L"Tables")) &&
                      headingFolded(app, idOf(app, L"Lists and emphasis")) &&
                      headingFolded(app, idOf(app, L"A second part")),
                  "only the folds around it open");

            // Search: a word only a folded section holds opens the sections around it
            foldAllSections(app, true);
            layoutDocument(app);
            app.searchQuery = L"zanzibar";
            performSearch(app);
            check(app.searchMatches.size() == 1 &&
                      app.searchMatches[0].highlightRect.bottom > app.searchMatches[0].highlightRect.top,
                  "search finds and shows a word in a folded section");
            check(!headingFolded(app, idOf(app, L"Folding sections")) && !headingFolded(app, idOf(app, L"Tables")) &&
                      headingFolded(app, idOf(app, L"Lists and emphasis")) && headingFolded(app, idOf(app, L"A second part")),
                  "only the sections holding the word open");
            foldAllSections(app, true);
            layoutDocument(app);
            app.searchQuery = L"nowhere-in-this-page";
            performSearch(app);
            check(app.searchMatches.empty() && headingFolded(app, idOf(app, L"Folding sections")),
                  "a search that finds nothing opens nothing");
            app.searchQuery.clear();
            app.searchMatches.clear();

            // The editor's preview, print and the setting turned off show every section
            app.editMode = true;
            layoutDocument(app);
            check(app.foldArrows.empty() && shows(app, L"zanzibar"), "the editor's preview folds nothing");
            check(!contextMenuItemEnabled(app, CTX_FOLD_ALL), "Fold all sections is not offered in the editor");
            app.editMode = false;
            app.printLayout = true;
            layoutDocument(app);
            check(app.foldArrows.empty() && shows(app, L"zanzibar"), "print folds nothing");
            app.printLayout = false;
            app.foldSections = false;
            layoutDocument(app);
            check(app.foldArrows.empty() && shows(app, L"zanzibar") && !contextMenuItemEnabled(app, CTX_FOLD_ALL),
                  "with the setting off, nothing folds");
            app.foldSections = true;
            foldAllSections(app, false);
        }
    }

    // Each document keeps its folds for the session; a new one takes the setting's
    scenario = "documents";
    const std::string a = app.currentFile, b = a + ".other";
    foldAllSections(app, false);
    toggleFold(app, "tables");
    foldsLeave(app);
    check(!app.foldAll && app.foldToggled.empty(), "leaving a document clears the page's folds");
    app.currentFile = b;
    foldsEnter(app);
    check(!app.foldAll && app.foldToggled.empty(), "another document opens unfolded");
    foldsLeave(app);
    app.currentFile = a;
    foldsEnter(app);
    check(!app.foldAll && headingFolded(app, "tables") && !headingFolded(app, "lists-and-emphasis"),
          "back on the first document, its fold is there");
    app.foldOnOpen = true;
    app.currentFile = a + ".third";
    app.pendingScrollRestore = 500.0f;
    foldsEnter(app);
    check(app.foldAll && headingFolded(app, "lists-and-emphasis") && app.pendingScrollRestore < 0.0f,
          "with Folded set, a new document opens folded, at its top");
    app.foldOnOpen = false;

    // The settings keep both choices
    scenario = "settings";
    Settings settings = loadSettings();
    check(settings.foldSections && !settings.foldOnOpen, "folding is on and documents open unfolded by default");
    settings.foldSections = false;
    settings.foldOnOpen = true;
    saveSettings(settings);
    const Settings reread = loadSettings();
    check(!reread.foldSections && reread.foldOnOpen, "settings.ini keeps both choices");

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Folding sections: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
