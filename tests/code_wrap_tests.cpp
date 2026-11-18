#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "export.h"
#include "file_utils.h"
#include "i18n.h"
#include "input.h"
#include "overlays.h"
#include "print.h"
#include "render.h"
#include "search.h"
#include "selection.h"
#include "settings.h"
#include "tabs.h"
#include "ui_frame.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Long lines in code blocks (#324). A code block grew as wide as its longest
// line, so a prompt kept in a fence ran far past the window and the page had
// to be scrolled sideways to read it. A long line now wraps at the block's
// edge; Settings > Appearance > Wrap long lines in code blocks turns that
// off and brings the old layout back.
namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
namespace fs = std::filesystem;
using uiframe::Frame;

fs::path g_fixtures, g_work, g_frames;
std::string g_fixture;

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}
void writeAll(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

struct Window {
    std::unique_ptr<App> state = std::make_unique<App>();
    App& app = *state;
    bool ok = false;
    Window() {
        app.hwnd = CreateWindowExW(0, L"STATIC", L"Code wrap tests", WS_POPUP, 0, 0, 1300, 900, nullptr, nullptr,
                                   nullptr, nullptr);
        if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return;
        app.width = 1300;
        app.height = 900;
        updateTextFormats(app);
        Settings keys;
        keys.keyProfile = "windows";
        applyKeymap(app, keys);
        applyTheme(app, 0);
        ok = true;
    }
    ~Window() {
        if (app.hwnd) DestroyWindow(app.hwnd);
        app.hwnd = nullptr;
    }
};

// --- The page, laid out ---------------------------------------------------------

bool lay(App& app, const std::string& source, int width, bool wrap, const char* file = nullptr) {
    app.width = width;
    app.height = 900;
    app.wrapCode = wrap;
    const auto doc = file ? parseDocument(app.parser, source, std::string_view(file)) : app.parser.parse(source);
    if (!doc.success) return false;
    app.root = doc.root;
    app.layoutDirty = true;
    layoutDocument(app);
    ensureLayoutComplete(app);
    return true;
}

// A row of a code block: where it is and the stretch of the document it shows
struct Row {
    D2D1_RECT_F rect{};
    size_t start = 0, length = 0, run = (size_t)-1;
};
// A code block as laid out: its card, its rows down the page, its text's runs
struct Block {
    D2D1_RECT_F bounds{};
    std::wstring code;
    size_t docAt = 0;
    std::vector<Row> rows;
    std::vector<const App::LayoutTextRun*> runs;
    const App::LayoutRect* card = nullptr;  // the background that starts where the block does
    int lines = 0, written = 0;             // lines of the code, and those with something on them
    float lineHeight = 0.0f, padLeft = 0.0f, padTop = 0.0f;
    float inner() const { return uiframe::width(bounds) - 2.0f * padLeft; }
};

std::vector<Block> blocksOf(const App& app) {
    std::vector<Block> blocks;
    size_t from = 0;
    for (const auto& info : app.codeBlocks) {
        if (info.isDiagram) continue;
        Block block;
        block.bounds = info.bounds;
        block.code = info.codeText;
        const size_t at = app.docText.find(block.code, from);
        block.docAt = at == std::wstring::npos ? 0 : at;
        from = block.docAt + block.code.size();
        for (const auto& tr : app.textRects) {
            if (tr.docStart < block.docAt || tr.docStart + tr.docLength > block.docAt + block.code.size()) continue;
            if (tr.rect.top < block.bounds.top - 0.5f || tr.rect.bottom > block.bounds.bottom + 0.5f) continue;
            block.rows.push_back({tr.rect, tr.docStart, tr.docLength, tr.runIndex});
        }
        std::stable_sort(block.rows.begin(), block.rows.end(),
                         [](const Row& a, const Row& b) { return a.rect.top < b.rect.top - 0.25f; });
        for (const auto& run : app.layoutTextRuns) {
            if (run.bounds.top >= block.bounds.top - 0.5f && run.bounds.bottom <= block.bounds.bottom + 0.5f &&
                run.bounds.left >= block.bounds.left - 0.5f) {
                block.runs.push_back(&run);
            }
        }
        for (const auto& rect : app.layoutRects) {
            if (uiframe::about(rect.rect.left, block.bounds.left) && uiframe::about(rect.rect.top, block.bounds.top) &&
                uiframe::about(rect.rect.bottom, block.bounds.bottom)) {
                block.card = &rect;
            }
        }
        size_t lineAt = 0;
        while (lineAt < block.code.size()) {
            size_t end = block.code.find(L'\n', lineAt);
            if (end == std::wstring::npos) end = block.code.size();
            ++block.lines;
            if (end > lineAt && !(end == lineAt + 1 && block.code[lineAt] == L'\r')) ++block.written;
            lineAt = end + 1;
        }
        if (!block.rows.empty()) {
            block.lineHeight = uiframe::height(block.rows[0].rect);
            block.padLeft = block.rows[0].rect.left - block.bounds.left;
            block.padTop = block.rows[0].rect.top - block.bounds.top;
        }
        blocks.push_back(std::move(block));
    }
    return blocks;
}

// The rows that show one line of the code
std::vector<Row> rowsOfLine(const App& app, const Block& block, const std::wstring& startsWith) {
    std::vector<Row> rows;
    const size_t at = block.code.find(startsWith);
    if (at == std::wstring::npos) return rows;
    size_t end = block.code.find(L'\n', at);
    if (end == std::wstring::npos) end = block.code.size();
    for (const Row& row : block.rows)
        if (row.start >= block.docAt + at && row.start + row.length <= block.docAt + end) rows.push_back(row);
    (void)app;
    return rows;
}

// What holds for every block whose long lines wrap: nothing of it past its
// card, the card as wide as the column, its rows one under the other and
// showing the code from its first letter to its last with nothing left out
void wrapsWhole(const App& app, const std::vector<Block>& blocks, const std::string& where) {
    const float page = documentViewportWidth(app);
    check(app.contentWidth <= page + 0.5f, where + ": the page is no wider than the window");
    for (size_t i = 0; i < blocks.size(); i++) {
        const Block& block = blocks[i];
        const std::string name = where + ", block " + std::to_string(i + 1);
        check(block.bounds.right <= page + 0.5f, name + ": ends inside the window");
        check(block.card && uiframe::about(block.card->rect.right, block.bounds.right),
              name + ": its background is as wide as the column, no wider");
        bool inside = !block.runs.empty();
        float past = 0.0f;
        for (const auto* run : block.runs) {
            inside = inside && run->bounds.right <= block.bounds.right - block.padLeft + 0.5f &&
                     run->bounds.left >= block.bounds.left + block.padLeft - 0.5f;
            past = std::max(past, run->bounds.right - (block.bounds.right - block.padLeft));
        }
        check(inside, name + ": every stretch of its text is inside its padding (" + std::to_string(past) + " px past)");
        bool rowsIn = !block.rows.empty(), stacked = true;
        for (size_t r = 0; r < block.rows.size(); r++) {
            const Row& row = block.rows[r];
            rowsIn = rowsIn && row.rect.right <= block.bounds.right - block.padLeft + 0.5f &&
                     row.rect.bottom <= block.bounds.bottom + 0.5f;
            if (r) {
                const float step = (row.rect.top - block.rows[r - 1].rect.top) / block.lineHeight;
                stacked = stacked && step > 0.99f && std::fabs(step - std::round(step)) < 0.02f;
            }
        }
        check(rowsIn, name + ": every row is inside it");
        check(stacked, name + ": each row is a whole number of rows under the one before");
        // The rows show the code from end to end: only line breaks lie between them
        size_t expected = block.docAt;
        bool whole = true;
        for (const Row& row : block.rows) {
            while (expected < row.start && (app.docText[expected] == L'\n' || app.docText[expected] == L'\r')) ++expected;
            whole = whole && row.start == expected;
            expected = row.start + row.length;
        }
        for (; expected < block.docAt + block.code.size(); ++expected)
            whole = whole && (app.docText[expected] == L'\n' || app.docText[expected] == L'\r');
        check(whole, name + ": its rows show the code from end to end, nothing twice and nothing left out");
        check(app.docText.find(block.code) != std::wstring::npos,
              name + ": the document's text has the code as written, no break put into a line");
        if (!block.rows.empty()) {
            const float below = block.bounds.bottom - block.rows.back().rect.bottom;
            const bool endsBlank = !block.code.empty() && block.code.size() >= 2 &&
                                   block.code[block.code.size() - 2] == L'\n';
            check(endsBlank || std::fabs(below - block.padTop) < 0.75f,
                  name + ": as much room under its last row as over its first");
        }
    }
}

int rowsBeyondLines(const Block& block) { return (int)block.rows.size() - block.written; }

// --- The fixture ----------------------------------------------------------------

void theFixture(App& app) {
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1300, 760, 420}) {
            const std::string where = std::string(theme ? "dark" : "light") + " at " + std::to_string(width);
            check(lay(app, g_fixture, width, true), where + ": the fixture is laid out");
            const std::vector<Block> on = blocksOf(app);
            check(on.size() == 6, where + ": its six code blocks are there");
            if (on.size() != 6) continue;
            wrapsWhole(app, on, where);
            const float heightOn = app.contentHeight;

            // 1: the prompt, plain. 2: Python, highlighted. 3: short. 4: in
            // a list. 5: in a quote. 6: indentation and a word with no break
            check(rowsBeyondLines(on[0]) >= 2, where + ": the prompt's long lines go over several rows");
            check(rowsBeyondLines(on[1]) >= 2, where + ": the highlighted block's long lines do too");
            check(rowsBeyondLines(on[2]) == 0 && on[2].rows.size() == 1, where + ": the short block is one row, as before");
            check(rowsBeyondLines(on[3]) >= 1, where + ": the block in the list wraps");
            check(rowsBeyondLines(on[4]) >= 1, where + ": the block in the quote wraps");
            check(rowsBeyondLines(on[5]) >= 2, where + ": the indented line and the long word wrap");
            check(on[3].bounds.left > on[0].bounds.left + 8.0f && on[3].bounds.right <= on[0].bounds.right + 0.5f,
                  where + ": the block in the list is narrower and wraps at its own edge");
            check(on[4].bounds.left > on[0].bounds.left + 4.0f, where + ": the block in the quote is set in");

            // Full rows: a row that is not a line's last runs most of the way
            // across (Chinese text may break between any two characters)
            const std::vector<Row> prompt = rowsOfLine(app, on[0], on[0].code.substr(0, 6));
            check(prompt.size() >= 2, where + ": the prompt's first line has more than one row");
            bool full = true;
            for (size_t r = 0; r + 1 < prompt.size(); r++)
                full = full && uiframe::width(prompt[r].rect) > on[0].inner() * 0.8f;
            check(full, where + ": a row of it runs most of the way across before the next begins");

            // At a space where the row has one
            const std::vector<Row> words = rowsOfLine(app, on[4], L"one two three");
            check(words.size() >= 2, where + ": the line of words has more than one row");
            bool atSpaces = true;
            for (size_t r = 1; r < words.size(); r++)
                atSpaces = atSpaces && (app.docText[words[r].start - 1] == L' ' || app.docText[words[r].start - 1] == L'-') &&
                           app.docText[words[r].start] != L' ';
            check(atSpaces, where + ": its rows begin after a space or a hyphen, never inside a word");

            // Inside a word that is longer than the block is wide
            const std::vector<Row> word = rowsOfLine(app, on[5], L"AWordWithNoPlace");
            check(word.size() >= 2, where + ": the word with no place to break goes over rows");
            bool cut = !word.empty();
            for (size_t r = 0; r + 1 < word.size(); r++) {
                const float letter = uiframe::width(word[r].rect) / (float)word[r].length;
                cut = cut && uiframe::width(word[r].rect) > on[5].inner() - 2.0f * letter;
            }
            check(cut, where + ": each of its rows is filled to within a letter or two");

            // Indentation stays on the line's first row
            const std::vector<Row> indented = rowsOfLine(app, on[5], L"        eight spaces");
            check(indented.size() >= 2, where + ": the indented line wraps");
            if (!indented.empty() && indented[0].run < app.layoutTextRuns.size()) {
                const auto& run = app.layoutTextRuns[indented[0].run];
                DWRITE_HIT_TEST_METRICS m{};
                float one = 0, eight = 0, y = 0;
                run.layout->HitTestTextPosition(1, FALSE, &one, &y, &m);
                run.layout->HitTestTextPosition(8, FALSE, &eight, &y, &m);
                check(one > 1.0f && std::fabs(eight - 8.0f * one) < 0.5f && uiframe::about(run.pos.x, indented[0].rect.left),
                      where + ": its eight spaces lead the first row");
            } else {
                check(false, where + ": the plain block's row has its own layout");
            }
            if (indented.size() >= 2) {
                check(uiframe::about(indented[1].rect.left, indented[0].rect.left),
                      where + ": its second row starts at the block's left");
            }

            // Colours: the same ones, in rows that sit side by side
            std::vector<D2D1_COLOR_F> colours;
            auto colourSet = [](const Block& block) {
                std::vector<D2D1_COLOR_F> set;
                for (const auto* run : block.runs) {
                    bool seen = false;
                    for (const auto& c : set) seen = seen || (c.r == run->color.r && c.g == run->color.g && c.b == run->color.b);
                    if (!seen) set.push_back(run->color);
                }
                return set;
            };
            const size_t coloursOn = colourSet(on[1]).size();
            // Row by row: the first stretch at the left padding, each next one
            // where the one before ends, the last where the row's text ends
            bool abut = true, rowStarts = true, rowEnds = true;
            int several = 0;
            for (const Row& row : on[1].rows) {
                std::vector<const App::LayoutTextRun*> across;
                for (const auto* run : on[1].runs)
                    if (std::fabs(run->bounds.top - row.rect.top) < 0.5f) across.push_back(run);
                if (across.empty()) continue;
                std::stable_sort(across.begin(), across.end(), [](const App::LayoutTextRun* a, const App::LayoutTextRun* b) {
                    return a->bounds.left < b->bounds.left;
                });
                rowStarts = rowStarts && uiframe::about(across.front()->bounds.left, on[1].bounds.left + on[1].padLeft);
                rowEnds = rowEnds && std::fabs(across.back()->bounds.right - row.rect.right) < 0.75f;
                for (size_t a = 1; a < across.size(); a++)
                    abut = abut && std::fabs(across[a]->bounds.left - across[a - 1]->bounds.right) < 0.75f;
                if (across.size() >= 3) ++several;
            }
            check(several >= 3, where + ": rows of the highlighted block have several coloured stretches");
            check(abut, where + ": the coloured stretches of a row follow one another, none over another and no gap");
            check(rowStarts, where + ": every row of the highlighted block starts at its left padding");
            check(rowEnds, where + ": and its last stretch ends where the row's text does");

            // Selecting a wrapped line: one band a row, and the line copied as one line
            if (prompt.size() >= 2) {
                const size_t lineStart = prompt.front().start, lineEnd = prompt.back().start + prompt.back().length;
                std::vector<D2D1_RECT_F> bands;
                selectionHighlightRects(app, lineStart, lineEnd, bands);
                bool bandsIn = bands.size() == prompt.size();
                for (const auto& band : bands) bandsIn = bandsIn && uiframe::inside(band, on[0].bounds);
                check(bandsIn, where + ": a selected line is marked with one band a row, inside the block (" +
                                   std::to_string(bands.size()) + " for " + std::to_string(prompt.size()) + ")");
                const std::wstring copied = selectionTextForRange(app, lineStart, lineEnd);
                check(copied.find(L'\n') == std::wstring::npos && copied == app.docText.substr(lineStart, lineEnd - lineStart) &&
                          copied.size() > 40,
                      where + ": and is copied as the one line it is");
                // A point on its second row is a place in that row's text
                const Row& second = prompt[1];
                const size_t at = selectionOffsetAtPoint(app, (second.rect.left + second.rect.right) / 2,
                                                         (second.rect.top + second.rect.bottom) / 2);
                check(at > second.start && at < second.start + second.length,
                      where + ": a click on its second row lands in that row's text");
            }

            // Find marks a word on a wrapped row where the row is
            for (const wchar_t* word : {L"seventeen", L"\u8bc1\u636e\u7b49\u7ea7"}) {
                app.searchQuery = word;
                performSearch(app);
                mapSearchMatchesToLayout(app);
                const bool quote = word[0] == L's';
                const Block& in = quote ? on[4] : on[0];
                check(app.searchMatches.size() == 1, where + ": the word is found once");
                if (app.searchMatches.size() != 1) continue;
                const D2D1_RECT_F mark = app.searchMatches[0].highlightRect;
                bool onRow = false;
                for (const Row& row : in.rows)
                    onRow = onRow || (app.searchMatches[0].startPos >= row.start &&
                                      app.searchMatches[0].startPos < row.start + row.length &&
                                      mark.top >= row.rect.top - 0.5f && mark.bottom <= row.rect.bottom + 0.5f);
                check(uiframe::inside(mark, in.bounds) && uiframe::width(mark) > 4.0f && onRow,
                      where + ": and marked on the row it sits on, inside the block");
            }
            // A match that runs over rows is marked on each of them
            if (word.size() >= 2) {
                const size_t from = word[0].start + word[0].length - 5;
                app.searchQuery = app.docText.substr(from, 10);
                performSearch(app);
                mapSearchMatchesToLayout(app);
                std::vector<SearchHighlight> marks;
                searchHighlightRects(app, on[5].bounds.top, on[5].bounds.bottom, marks);
                bool marksIn = marks.size() == 2;
                for (const auto& mark : marks) marksIn = marksIn && uiframe::inside(mark.rect, on[5].bounds);
                check(app.searchMatches.size() == 1 && marksIn && marks[0].rect.top < marks[1].rect.top - 1.0f,
                      where + ": letters found across the end of a row are marked on both rows (" +
                          std::to_string(marks.size()) + " marks)");
            }
            app.searchQuery.clear();
            app.searchMatches.clear();

            // The setting off: every line one row, the block as wide as its
            // longest line, the page scrolling sideways, as it always was
            check(lay(app, g_fixture, width, false), where + ": laid out with the setting off");
            const std::vector<Block> off = blocksOf(app);
            check(off.size() == 6, where + ", off: the six blocks");
            if (off.size() != 6) continue;
            bool oneRow = true, grown = true;
            for (size_t i = 0; i < off.size(); i++) {
                oneRow = oneRow && rowsBeyondLines(off[i]) == 0;
                if (i == 2) continue;
                grown = grown && off[i].card && off[i].card->rect.right > off[i].bounds.right + 20.0f;
            }
            check(oneRow, where + ", off: every line is one row");
            check(grown, where + ", off: the blocks with long lines grow past the column");
            check(off[2].card && uiframe::about(off[2].card->rect.right, off[2].bounds.right),
                  where + ", off: the short block is as wide as the column");
            check(app.contentWidth > documentViewportWidth(app) + 200.0f, where + ", off: the page scrolls sideways");
            check(app.contentHeight < heightOn - 3.0f * on[0].lineHeight, where + ", off: the page is shorter by the rows");
            check(colourSet(off[1]).size() == coloursOn && coloursOn >= 3,
                  where + ": the highlighted block has the same colours wrapped as not (" + std::to_string(coloursOn) + ")");
            check(uiframe::about(off[2].bounds.top, on[2].bounds.top - (on[0].bounds.bottom - off[0].bounds.bottom) -
                                                        (on[1].bounds.bottom - on[1].bounds.top -
                                                         (off[1].bounds.bottom - off[1].bounds.top))),
                  where + ": the blocks below move down by exactly the rows added above them");
            (void)colours;
        }
    }
    applyTheme(app, 0);
}

// --- Lines that are awkward to wrap ---------------------------------------------

void awkwardLines(App& app) {
    auto fence = [](const std::string& lang, const std::string& code) { return "Before.\n\n```" + lang + "\n" + code + "\n```\n\nAfter.\n"; };
    const std::string spaces(300, ' ');
    const std::string longComment = "the quick brown fox jumps over the lazy dog and keeps running across the field until the row ends ";

    struct Case { const char* name; std::string source; int atLeastRows; };
    const std::vector<Case> cases = {
        {"spaces in the middle and at the end", fence("", "word" + spaces + "end" + spaces), 2},
        {"a line of nothing but spaces", fence("", std::string(600, ' ') + "\nnext"), 2},
        {"tabs in the middle of a highlighted line",
         fence("cpp", "int a = 1;\t// " + longComment + "\tint b = 2;\t\t/* " + longComment + longComment + " */\tint c;"), 2},
        {"tabs in the middle of a plain line", fence("", "a\tb\t" + longComment + "\tc\t\t" + longComment + longComment), 2},
        {"a line that leads with tabs", fence("go", "\t\tfmt.Println(\"" + longComment + longComment + longComment + "\")"), 2},
        {"Windows line ends", "Before.\r\n\r\n```\r\n" + longComment + longComment + longComment + "\r\nshort\r\n```\r\n\r\nAfter.\r\n", 3},
        {"a string with no space in a highlighted line",
         fence("json", "{\"data\": \"" + std::string(700, 'A') + "\", \"next\": true}"), 3},
        {"emoji and Chinese among the letters",
         fence("", "note \xF0\x9F\x98\x80 \xE4\xB8\xAD\xE6\x96\x87\xE5\x92\x8C English \xF0\x9F\x8E\x89 " + longComment + longComment +
                       "\xE7\xBB\x93\xE6\x9D\x9F"), 2},
    };
    for (const Case& item : cases) {
        for (int width : {900, 420}) {
            const std::string where = std::string(item.name) + " at " + std::to_string(width);
            check(lay(app, item.source, width, true), where + ": laid out");
            const std::vector<Block> blocks = blocksOf(app);
            check(blocks.size() == 1, where + ": one block");
            if (blocks.size() != 1) continue;
            wrapsWhole(app, blocks, where);
            const float rows = (uiframe::height(blocks[0].bounds) - 2.0f * blocks[0].padTop) / blocks[0].lineHeight;
            check(rows > item.atLeastRows - 0.01f, where + ": the block is tall enough for its rows");
            check(app.docText.find(L"After.") != std::wstring::npos, where + ": the text after it is laid out");
        }
    }

    // A plain text file is one code block, and wraps as one
    const std::string text = longComment + longComment + longComment + "\nsecond line\n";
    check(lay(app, text, 760, true, "notes.txt"), "a text file is laid out");
    std::vector<Block> blocks = blocksOf(app);
    check(blocks.size() == 1 && rowsBeyondLines(blocks[0]) >= 1, "a text file's long line wraps");
    if (blocks.size() == 1) wrapsWhole(app, blocks, "a text file");

    // Short lines, blank lines and an empty block: the same block either way
    for (const char* code : {"short\n\nlines\n", "one", "", "\n\n", "a\n\n\n"}) {
        const std::string source = fence("", code);
        check(lay(app, source, 760, true), "short lines are laid out");
        const std::vector<Block> on = blocksOf(app);
        const float heightOn = app.contentHeight;
        check(lay(app, source, 760, false), "and with the setting off");
        const std::vector<Block> off = blocksOf(app);
        check(on.size() == 1 && off.size() == 1 && uiframe::about(on[0].bounds.bottom, off[0].bounds.bottom) &&
                  uiframe::about(on[0].bounds.right, off[0].bounds.right) && on[0].rows.size() == off[0].rows.size() &&
                  uiframe::about(heightOn, app.contentHeight),
              "lines that fit make the same block with the setting on as off");
    }

    // A block too narrow for a few letters is left to grow, as before:
    // one letter a row would be worse than a sideways scroll
    const std::string nested = "- a\n  - b\n    - c\n      - d\n        - e\n\n          ```\n          " + longComment + "\n          ```\n";
    bool found = false;
    for (int width = 400; width >= 120 && !found; width -= 20) {
        if (!lay(app, nested, width, true)) break;
        const std::vector<Block> narrow = blocksOf(app);
        if (narrow.size() != 1 || narrow[0].rows.empty()) continue;
        if (narrow[0].inner() >= narrow[0].lineHeight * 2.0f) continue;
        found = true;
        check(rowsBeyondLines(narrow[0]) == 0 && narrow[0].card && narrow[0].card->rect.right > narrow[0].bounds.right,
              "a block narrower than a few letters keeps its line whole and grows (at " + std::to_string(width) + ")");
    }
    check(found, "a width at which the nested block is narrower than a few letters was tried");

    // Zoomed in, the rows follow the bigger letters
    check(lay(app, g_fixture, 760, true), "the fixture again");
    const std::vector<Block> plain = blocksOf(app);
    const float savedZoom = app.zoomFactor;
    app.zoomFactor = 1.6f;
    updateTextFormats(app);
    check(lay(app, g_fixture, 760, true), "the fixture, zoomed in");
    const std::vector<Block> zoomed = blocksOf(app);
    if (plain.size() == 6 && zoomed.size() == 6) {
        wrapsWhole(app, zoomed, "zoomed in");
        check(zoomed[0].rows.size() > plain[0].rows.size(), "zoomed in, the prompt takes more rows");
    } else {
        check(false, "zoomed in: six blocks");
    }
    app.zoomFactor = savedZoom;
    updateTextFormats(app);
}

// --- Print, export, pictures ----------------------------------------------------

Frame pageFrame(App& app, int width, int height) {
    app.width = width;
    app.height = height;
    app.printShrinkBands.clear();  // the last print's, which the screen does not have
    return uiframe::draw(app, [&] {
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc;
        if (SUCCEEDED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc)))) {
            drawLayoutRange(app, dc.Get(), app.brush, 0.0f, (float)height, 0.0f, 0.0f);
        }
    });
}

void printAndExport(App& app) {
    // Printed, a block with a long line was shrunk as a whole to fit the
    // paper; wrapped, it prints at the size of the text around it
    check(lay(app, g_fixture, 1000, true), "the fixture for printing");
    const int pagesOn = printDebugPages(app, (g_frames / "print-on").wstring());
    check(pagesOn > 0, "the fixture prints");
    check(app.printShrinkBands.empty(), "printed with the setting on, nothing has to be shrunk to fit the paper");
    check(lay(app, g_fixture, 1000, false), "the fixture for printing, off");
    const int pagesOff = printDebugPages(app, (g_frames / "print-off").wstring());
    check(pagesOff > 0 && !app.printShrinkBands.empty(), "printed with it off, the wide blocks are shrunk as before");
    app.wrapCode = true;
    app.layoutDirty = true;
    ensureLayoutComplete(app);

    // HTML: the blocks wrap in the browser too, and scroll with it off
    const fs::path html = g_work / "wrap.html";
    check(exportHtmlFile(app, html.wstring()), "the fixture exports to HTML");
    std::string page = readAll(html);
    check(page.find("pre{white-space:pre-wrap;overflow-wrap:anywhere;}") != std::string::npos,
          "the HTML export's blocks wrap");
    check(page.find("overflow-x:auto") != std::string::npos, "and keep their own scrolling for a browser that will not");
    app.wrapCode = false;
    check(exportHtmlFile(app, html.wstring()), "the fixture exports to HTML with the setting off");
    page = readAll(html);
    check(page.find("pre-wrap") == std::string::npos && page.find("overflow-x:auto") != std::string::npos,
          "with it off the HTML export's blocks scroll, as before");
    app.wrapCode = true;

    // Pictures, for looking at
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1300, 760, 420}) {
            for (bool wrap : {true, false}) {
                if (!wrap && (theme || width != 760)) continue;
                if (!lay(app, g_fixture, width, wrap)) continue;
                const int height = (int)std::min(app.contentHeight + 40.0f, 6000.0f);
                const Frame frame = pageFrame(app, width, height);
                const std::string file = std::string("page-") + std::to_string(width) + (theme ? "-dark" : "-light") +
                                         (wrap ? "" : "-off");
                check(uiframe::save(app, frame, (g_frames / (file + ".png")).wstring()), "a picture of " + file);
                if (wrap && !frame.empty()) {
                    // The code's background runs to the block's right edge and stops there
                    const std::vector<Block> blocks = blocksOf(app);
                    if (!blocks.empty() && blocks[0].card) {
                        const float y = blocks[0].bounds.top + blocks[0].padTop * 0.5f;
                        const D2D1_COLOR_F in = frame.at(blocks[0].bounds.right - 6.0f, y);
                        const D2D1_COLOR_F card = blocks[0].card->color;
                        check(std::fabs(in.r - card.r) < 0.03f && std::fabs(in.g - card.g) < 0.03f &&
                                  std::fabs(in.b - card.b) < 0.03f,
                              file + ": the block's background is drawn up to its right edge");
                    }
                }
            }
        }
    }
    applyTheme(app, 0);
    app.height = 900;
}

// --- How long it takes -----------------------------------------------------------

void timing(App& app) {
    std::string code;
    for (int i = 0; i < 400; i++) {
        code += "    result_" + std::to_string(i) + " = compute(alpha, beta, gamma) + ";
        for (int j = 0; j < 12; j++) code += "another_term_" + std::to_string(j) + "(value) + ";
        code += "0\n";
    }
    const std::string source = "```python\n" + code + "```\n";
    auto time = [&](bool wrap) {
        double best = 1e9;
        for (int i = 0; i < 3; i++) {
            const auto t0 = std::chrono::steady_clock::now();
            lay(app, source, 1000, wrap);
            best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        return best;
    };
    const double off = time(false), on = time(true);
    const std::vector<Block> blocks = blocksOf(app);
    std::cout << "400 long highlighted lines: " << off << " ms unwrapped, " << on << " ms wrapped into "
              << (blocks.empty() ? 0 : blocks[0].rows.size()) << " rows\n";
    check(!blocks.empty() && blocks[0].rows.size() >= 800, "400 long lines wrap into at least twice as many rows");
    check(on < off * 10.0 + 500.0, "wrapping them does not take out of all proportion longer");
}

// --- The setting ----------------------------------------------------------------

void theLine() {
    check(Settings().wrapCode && std::make_unique<App>()->wrapCode, "a new setup wraps long lines");
    check(loadSettings().wrapCode, "a settings.ini without the line wraps them");
    const fs::path ini = getSettingsPath();

    Settings settings = loadSettings();
    settings.wrapCode = false;
    saveSettings(settings);
    std::string written = readAll(ini);
    const size_t at = written.find("wrapCode=0");
    check(at != std::string::npos && (at == 0 || written[at - 1] == '\n') &&
              std::string("\r\n").find(written[at + 10]) != std::string::npos,
          "settings.ini has the line wrapCode=0");
    check(!loadSettings().wrapCode, "and it is read back");
    check(loadSettings().headingRules && loadSettings().foldSections, "the settings beside it are as they were");
    settings.wrapCode = true;
    saveSettings(settings);
    check(readAll(ini).find("wrapCode=1") != std::string::npos && loadSettings().wrapCode,
          "wrapCode=1 is written and read back");

    auto byHand = [&](const std::string& value) {
        std::string text = readAll(ini);
        const size_t line = text.find("wrapCode=");
        if (line == std::string::npos) return false;
        text.replace(line, text.find('\n', line) - line, "wrapCode=" + value);
        writeAll(ini, text);
        return loadSettings().wrapCode;
    };
    check(!byHand("0"), "wrapCode=0 by hand turns it off");
    check(byHand("1"), "wrapCode=1 by hand turns it on");
    check(byHand("yes") && byHand(""), "a value that is neither wraps");
    settings = loadSettings();
    settings.wrapCode = true;
    saveSettings(settings);
}

const D2D1_RECT_F* hitOf(const App& app, int action) {
    for (const auto& hit : app.settingsHits)
        if (hit.second == action) return &hit.first;
    return nullptr;
}
void click(App& app, const D2D1_RECT_F& r) {
    handleMouseDown(app, app.hwnd, MK_LBUTTON, uiframe::centre(r));
    handleMouseUp(app, app.hwnd, 0, uiframe::centre(r));
}
Frame settingsFrame(App& app, int w, int h) {
    app.width = w;
    app.height = h;
    app.showSettings = true;
    app.settingsAnimation = 1.0f;
    app.settingsSection = 1;  // Appearance
    app.settingsScroll = 0.0f;
    app.settingsLangOpen = app.settingsKeysOpen = false;
    app.mouseX = app.mouseY = -100;
    return uiframe::draw(app, [&] { renderSettingsOverlay(app); });
}
bool speak(App& app, const char* id) {
    const int index = languageIndexById(id);
    if (index < 0) return false;
    app.languageSetting = app.currentLanguageIndex = index;
    updateOverlayFormats(app);
    return true;
}
const App::SettingsRowBox* rowOf(const App& app, const D2D1_RECT_F& control) {
    for (const auto& row : app.settingsRows)
        if (uiframe::inside(control, row.row)) return &row;
    return nullptr;
}

void settingsRow(App& app) {
    using uiframe::inside;
    app.wrapCode = true;
    loadLanguageOverrides();  // German, French, Italian and Russian
    const std::wstring english = tr(languageIndexById("en"), "settings.wrap_code");
    const std::wstring englishHint = tr(languageIndexById("en"), "settings.wrap_code.hint");
    for (const char* lang : {"en", "zh", "ja", "ko", "de", "fr", "it", "ru"}) {
        check(speak(app, lang), std::string(lang) + ": the interface speaks it");
        for (const char* key : {"settings.wrap_code", "settings.wrap_code.hint"})
            check(languageTranslatesKey(app.currentLanguageIndex, key), std::string(lang) + " has " + key);
        const bool isEnglish = std::string(lang) == "en";
        check((std::wstring(tr(app, "settings.wrap_code")) == english) == isEnglish &&
                  (std::wstring(tr(app, "settings.wrap_code.hint")) == englishHint) == isEnglish,
              std::string(lang) + ": the row and its hint are in its own words");
        for (int w : {1300, 760, 420}) {
            const std::string where = std::string(lang) + " at " + std::to_string(w);
            settingsFrame(app, w, 900);
            const D2D1_RECT_F panel = settingsPanelRect(app);
            const D2D1_RECT_F* toggle = hitOf(app, SET_TOGGLE_WRAPCODE);
            const D2D1_RECT_F* rules = hitOf(app, SET_TOGGLE_HEADRULES);
            const D2D1_RECT_F* folding = hitOf(app, SET_FOLD_ON);
            check(toggle && rules && folding, where + ": the row's toggle is on the page");
            if (!toggle || !rules || !folding) continue;
            const App::SettingsRowBox* row = rowOf(app, *toggle);
            const App::SettingsRowBox* above = rowOf(app, *rules);
            const App::SettingsRowBox* below = rowOf(app, *folding);
            check(row && above && below, where + ": each control is in a row");
            if (!row || !above || !below) continue;
            check(row->group == above->group && row->group == below->group, where + ": in the reading group");
            check(uiframe::about(above->row.bottom, row->row.top) && uiframe::about(row->row.bottom, below->row.top),
                  where + ": right under Heading separators, over Fold sections");
            check(inside(row->row, panel) && inside(*toggle, row->row) && uiframe::overlap(*toggle, row->control),
                  where + ": inside the dialog, the toggle in the row's control");
            check(uiframe::about(uiframe::width(*toggle), uiframe::width(*rules)) &&
                      uiframe::about(toggle->left, rules->left),
                  where + ": the toggle is the size of its neighbour's, under it");
            check(uiframe::about(uiframe::height(row->row), uiframe::height(above->row)),
                  where + ": the row is as tall as its neighbour");
            check(!row->stacked && row->text.right <= row->control.left + 0.5f, where + ": the label ends before the toggle");
            check(w < 760 || !row->titleTrimmed, where + ": the label is whole");
            check(w < 760 || !row->hintTrimmed, where + ": the hint is whole");
        }
    }
    speak(app, "en");

    // A click turns it off: the page behind is laid out again, its blocks
    // one row a line and wide; another click wraps them again
    app.showSettings = false;
    check(lay(app, g_fixture, 1300, true), "the fixture behind the dialog");
    const float heightWrapped = app.contentHeight;
    Frame last = settingsFrame(app, 1300, 900);
    for (bool want : {false, true, false, true}) {
        const std::string where = want ? "a click to turn it on" : "a click to turn it off";
        const D2D1_RECT_F* target = hitOf(app, SET_TOGGLE_WRAPCODE);
        check(target != nullptr, where + ": the toggle is there");
        if (!target) continue;
        const D2D1_RECT_F at = *target;
        const bool rulesBefore = app.headingRules, foldBefore = app.foldSections;
        app.layoutDirty = false;
        click(app, at);
        check(app.wrapCode == want, where + ": does");
        check(app.layoutDirty, where + ": and has the page laid out again");
        check(app.showSettings && app.headingRules == rulesBefore && app.foldSections == foldBefore,
              where + ": the dialog stays open and its neighbours as they were");
        ensureLayoutComplete(app);
        check(want ? uiframe::about(app.contentHeight, heightWrapped) : app.contentHeight < heightWrapped - 40.0f,
              where + (want ? ": the page is as tall as it was wrapped" : ": the page is shorter, its lines one row each"));
        check((app.contentWidth > documentViewportWidth(app) + 1.0f) == !want,
              where + (want ? ": and no wider than the window" : ": and wider than the window"));
        const Frame now = settingsFrame(app, 1300, 900);
        check(now.differs(last, at), where + ": the toggle is drawn the other way");
        last = now;
    }

    // Pictures: the page in a wide and a narrow dialog
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (const char* lang : {"en", "zh", "ja", "ko", "de", "fr", "it", "ru"}) {
            speak(app, lang);
            for (int w : {1300, 420}) {
                if (theme && std::string(lang) != "en") continue;  // dark once
                app.wrapCode = !(theme && w == 1300);  // the dark, wide one with it off
                const Frame frame = settingsFrame(app, w, 900);
                const std::string file = std::string("settings-appearance-") + lang + "-" + std::to_string(w) +
                                         (theme ? "-dark" : "-light");
                check(uiframe::save(app, frame, (g_frames / (file + ".png")).wstring()), "a picture of " + file);
            }
        }
    }
    speak(app, "en");
    applyTheme(app, 0);
    app.showSettings = false;
    app.wrapCode = true;
}
}  // namespace

int runCodeWrapTests(const char* fixtureRoot) {
    HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 2;
    g_fixtures = fixtureRoot;
    g_work = fs::current_path() / "code-wrap";
    g_frames = fs::current_path() / "frames";
    std::error_code ignored;
    fs::remove_all(g_work, ignored);
    fs::create_directories(g_work);
    fs::create_directories(g_frames);
    g_fixture = readAll(g_fixtures / "code-wrap-324.md");
    check(g_fixture.size() > 2000, "the fixture is there");

    theLine();
    {
        Window window;
        if (!window.ok) {
            check(false, "a window for the test");
        } else {
            tabsInit(window.app);
            theFixture(window.app);
            awkwardLines(window.app);
            printAndExport(window.app);
            timing(window.app);
            settingsRow(window.app);
        }
    }

    CoUninitialize();
    OleUninitialize();
    std::cout << "Code wrap: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
