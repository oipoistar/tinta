#include "d2d_init.h"
#include "export.h"
#include "render.h"
#include "selection.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool closeEnough(float a, float b) { return std::abs(a-b) < 0.05f; }
bool sameColor(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    return closeEnough(a.r, b.r) && closeEnough(a.g, b.g) && closeEnough(a.b, b.b) && closeEnough(a.a, b.a);
}
std::vector<D2D1_RECT_F> bars(const App& app) {
    std::vector<D2D1_RECT_F> result;
    for (const auto& item : app.layoutRects)
        if (closeEnough(item.rect.right-item.rect.left, 4) &&
            sameColor(item.color, app.theme.blockquoteBorder)) result.push_back(item.rect);
    return result;
}
void layout(App& app, const std::string& source) {
    auto doc = app.parser.parse(source);
    check(doc.success, "quote fixture parses");
    app.root = doc.root;
    layoutDocument(app);
}
float lastTextBottom(const App& app) {
    float bottom = 0;
    for (const auto& run : app.layoutTextRuns) bottom = std::max(bottom, run.bounds.bottom);
    return bottom;
}
void paragraphCases(App& app) {
    const float scale = app.contentScale * app.zoomFactor;
    for (const char* source : {
            "> One line.\n", "> One line.\r\n", "> One line.",
            "> First paragraph.\n>\n> Last paragraph.\n",
            "> First line.  \n> Last line.\n",
            "> A quote with `inline code`, **bold**, *italic*, a [link](https://example.com) and $a+b=c$.\n",
            "> A longer quote that wraps at narrow widths, retaining the full final line while keeping the paragraph gap outside the bar. More words make this wrap at both test widths when zoomed.\n"}) {
        layout(app, source);
        auto quoteBars = bars(app);
        check(quoteBars.size() == 1, "one paragraph quote bar");
        if (quoteBars.size() != 1) continue;
        const float contentBottom = lastTextBottom(app);
        check(closeEnough(quoteBars[0].bottom, contentBottom), "paragraph bar ends at the last line, before its margin");
        const std::wstring quotedText = app.docText;
        layout(app, std::string(source) + "\n\nFollowing paragraph.\n");
        float followingY = -1;
        for (const auto& run : app.layoutTextRuns)
            if (run.docLength && app.docText.substr(run.docStart, run.docLength) == L"Following paragraph.")
                followingY = run.bounds.top;
        check(closeEnough(followingY, contentBottom + 14*scale), "following paragraph keeps the existing 14px gap");
        check(app.docText.rfind(quotedText, 0) == 0, "quote searchable text is unchanged by a following block");
    }

    layout(app, "> First paragraph.\n>\n> Last paragraph.\n");
    check(app.layoutTextRuns.size() == 2, "two quoted paragraphs retain both lines");
    if (app.layoutTextRuns.size() == 2)
        check(closeEnough(app.layoutTextRuns[1].bounds.top-app.layoutTextRuns[0].bounds.bottom, 14*scale),
              "internal paragraph spacing is retained");

    layout(app, "> First line.  \n> Last line.\n");
    check(app.layoutTextRuns.size() == 2, "explicit hard break retains both lines");
    if (app.layoutTextRuns.size() == 2)
        check(closeEnough(app.layoutTextRuns[1].bounds.top, app.layoutTextRuns[0].bounds.bottom),
              "hard break remains exactly one line advance");
}
void blockCases(App& app) {
    layout(app, "> Outer paragraph.\n>\n> > Inner paragraph.\n");
    auto quoteBars = bars(app);
    check(quoteBars.size() == 2, "nested quote has two bars");
    for (const auto& bar : quoteBars)
        check(closeEnough(bar.bottom, lastTextBottom(app)), "nested bars both end at the final content line");

    for (const char* source : {"> ```text\n> line\n> ```\n", "> ```text\n> line\n>\n> ```\n", "> ```text\n> ```\n"}) {
        layout(app, source);
        quoteBars = bars(app);
        check(quoteBars.size() == 1 && app.codeBlocks.size() == 1, "quoted code block renders");
        if (quoteBars.size() == 1 && app.codeBlocks.size() == 1)
            check(closeEnough(quoteBars[0].bottom, app.codeBlocks[0].bounds.bottom),
                  "bar retains code background padding and intentional empty rows");
    }

    layout(app, "> | Table | Value |\n> | --- | --- |\n> | Last row | $x$ |\n");
    quoteBars = bars(app);
    check(quoteBars.size() == 1 && app.tableRects.size() == 1, "quoted table renders");
    if (quoteBars.size() == 1 && app.tableRects.size() == 1) {
        float bottom = app.tableRects[0].bounds.bottom;
        check(quoteBars[0].bottom >= bottom && quoteBars[0].bottom < bottom+2,
              "bar covers the last table border without including the paragraph margin");
    }

    layout(app, "> $$\n> \\begin{bmatrix}1 & 2 \\\\ 3 & 4\\end{bmatrix}\n> $$\n");
    quoteBars = bars(app);
    check(quoteBars.size() == 1 && !app.textRects.empty(), "quoted display math renders");
    if (quoteBars.size() == 1 && !app.textRects.empty())
        check(closeEnough(quoteBars[0].bottom, app.textRects.back().rect.bottom), "bar covers the full display math box");

    layout(app, "> - First item.\n> - Last item.\n");
    quoteBars = bars(app);
    check(quoteBars.size() == 1, "quoted list renders");
    if (quoteBars.size() == 1)
        check(closeEnough(quoteBars[0].bottom, lastTextBottom(app)), "bar ends at the final list line");

    layout(app, "> ## Heading\n");
    quoteBars = bars(app);
    check(quoteBars.size() == 1 && !app.layoutLines.empty(), "quoted heading rule renders");
    if (quoteBars.size() == 1 && !app.layoutLines.empty()) {
        const auto& rule = app.layoutLines.back();
        check(closeEnough(quoteBars[0].bottom, rule.p1.y+rule.stroke/2), "bar covers the heading underline");
    }

    for (const char* source : {"> [!NOTE]\n> Callout body.\n", "> [!TIP]\n"}) {
        layout(app, source);
        check(!app.layoutRects.empty(), "callout bar renders");
        if (!app.layoutRects.empty()) {
            // A built-in theme sets a callout on a tint of its colour: a
            // box, which holds its last line's margin, with the bar down
            // its whole side. Without a box the bar ends at the last line
            const float scale = app.contentScale * app.zoomFactor;
            const D2D1_RECT_F bar = app.layoutRects.back().rect;
            const App::LayoutRect* ground = nullptr;
            for (const auto& rect : app.layoutRects) {
                if (rect.rect.right - rect.rect.left > 100 * scale && rect.rect.top <= bar.top + 0.05f && !ground) ground = &rect;
            }
            const float text = lastTextBottom(app);
            if (ground) {
                check(closeEnough(bar.top, ground->rect.top) && closeEnough(bar.bottom, ground->rect.bottom) &&
                          bar.bottom >= text - 0.05f && bar.bottom <= text + 20 * scale,
                      "callout bar fits the body or standalone title");
            } else {
                check(closeEnough(bar.bottom, text), "callout bar fits the body or standalone title");
            }
        }
    }
    layout(app, ">\n\nFollowing paragraph.\n");
    quoteBars = bars(app);
    check(quoteBars.size() == 1 && closeEnough(quoteBars[0].top, quoteBars[0].bottom), "empty quote does not acquire height");
}

// A raw HTML quote around bare text (<blockquote>Text</blockquote>) shows
// the text as GitHub does, as if it sat in a <p>. Layout drew nothing for
// it, Word wrote an empty paragraph, and a copy of the page left it out.
struct Geometry {
    std::wstring text;
    std::vector<D2D1_RECT_F> runs, quoteBars, links;
    float height = 0;
};
Geometry geometry(const App& app) {
    Geometry result{app.docText, {}, bars(app), {}, app.contentHeight};
    for (const auto& run : app.layoutTextRuns) result.runs.push_back(run.bounds);
    for (const auto& link : app.linkRects) result.links.push_back(link.bounds);
    return result;
}
bool sameRects(const std::vector<D2D1_RECT_F>& a, const std::vector<D2D1_RECT_F>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!closeEnough(a[i].left, b[i].left) || !closeEnough(a[i].top, b[i].top) ||
            !closeEnough(a[i].right, b[i].right) || !closeEnough(a[i].bottom, b[i].bottom)) return false;
    return true;
}
const App::TextRect* textAt(const App& app, size_t offset) {
    for (const auto& rect : app.textRects)
        if (rect.docLength && rect.docStart <= offset && offset < rect.docStart + rect.docLength) return &rect;
    return nullptr;
}
const App::TextRect* textOf(const App& app, const wchar_t* value) {
    const size_t offset = app.docText.find(value);
    return offset == std::wstring::npos ? nullptr : textAt(app, offset);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};
}

std::string repeat(const std::string& piece, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) out += piece;
    return out;
}

void htmlBareTextCases(App& app) {
    // Each bare form against the same text in <p> paragraphs
    const std::string pairs[][2] = {
        {"<blockquote>Bare text</blockquote>\n\nFollowing paragraph.\n",
         "<blockquote><p>Bare text</p></blockquote>\n\nFollowing paragraph.\n"},
        {"<blockquote>\nBare text with <b>bold</b>, <a href=\"https://example.com/q\">a link</a> and H<sub>2</sub>O<br>\n"
         "a second line.\n</blockquote>\n",
         "<blockquote>\n<p>Bare text with <b>bold</b>, <a href=\"https://example.com/q\">a link</a> and H<sub>2</sub>O<br>\n"
         "a second line.</p>\n</blockquote>\n"},
        {"<blockquote>Bare before<p>Paragraph</p>Bare after</blockquote>\n",
         "<blockquote><p>Bare before</p><p>Paragraph</p><p>Bare after</p></blockquote>\n"},
        {"<blockquote>Outer<blockquote>Inner<blockquote>Innermost</blockquote></blockquote>After inner</blockquote>\n",
         "<blockquote><p>Outer</p><blockquote><p>Inner</p><blockquote><p>Innermost</p></blockquote></blockquote>"
         "<p>After inner</p></blockquote>\n"},
        {"> Markdown quote.\n>\n> <blockquote>Bare text in a Markdown quote</blockquote>\n",
         "> Markdown quote.\n>\n> <blockquote><p>Bare text in a Markdown quote</p></blockquote>\n"},
        {"- Item:\n\n  <blockquote>Bare text in a list item</blockquote>\n",
         "- Item:\n\n  <blockquote><p>Bare text in a list item</p></blockquote>\n"},
        {repeat("<blockquote>", 30) + "Thirty levels" + repeat("</blockquote>", 30) + "\n",
         repeat("<blockquote>", 30) + "<p>Thirty levels</p>" + repeat("</blockquote>", 30) + "\n"},
    };
    for (const auto& pair : pairs) {
        layout(app, pair[1]);
        const Geometry expected = geometry(app);
        layout(app, pair[0]);
        const Geometry actual = geometry(app);
        check(actual.text == expected.text, "bare quote text is on the page, as in a <p>");
        check(sameRects(actual.runs, expected.runs), "bare quote text lays out as a <p> paragraph");
        check(sameRects(actual.quoteBars, expected.quoteBars), "bare quote bars match the <p> quote's");
        check(sameRects(actual.links, expected.links), "links in bare quote text stay clickable");
        check(closeEnough(actual.height, expected.height), "bare quote takes the <p> quote's height");
    }

    layout(app, pairs[0][0]);
    const auto* text = textOf(app, L"Bare text");
    auto quoteBars = bars(app);
    check(text && quoteBars.size() == 1, "one-level bare quote draws its text and bar");
    if (text && quoteBars.size() == 1)
        check(closeEnough(quoteBars[0].top, text->rect.top) && closeEnough(quoteBars[0].bottom, text->rect.bottom),
              "bare quote bar runs beside its text, its paragraph gap outside");
    layout(app, pairs[std::size(pairs) - 1][0]);
    quoteBars = bars(app);
    check(app.docText.find(L"Thirty") != std::wstring::npos && quoteBars.size() == 30 &&
          std::all_of(quoteBars.begin(), quoteBars.end(), [](const auto& bar) { return bar.bottom > bar.top; }),
          "a bare quote thirty levels deep draws its text beside thirty bars");
}

void htmlFixture(App& app, const std::string& source) {
    layout(app, source);
    // Every quote's text is on the page once, in source order
    const wchar_t* order[] = {L"One level HTML quote paragraph", L"Bare text in an HTML quote",
        L"Bare text on a line of its own", L"Paragraph between the one-level quotes", L"Bare text with",
        L"quoted link", L"H2O,", L"a second line after a break", L"and a third line.",
        L"Bare text before a paragraph", L"A paragraph in the middle of the quote.", L"Bare text after the paragraph.",
        L"A paragraph after an anchor that takes no room.", L"Outer bare text", L"Inner bare text",
        L"Innermost paragraph", L"Inner text after the innermost quote", L"Outer text after the inner quote",
        L"Three levels of bare text", L"A nested Markdown quote.", L"Bare text between Markdown quotes",
        L"Bare text of an HTML quote inside a Markdown quote", L"A callout after the HTML quotes",
        L"Bare text in a quote inside a list item", L"A last list item", L"Code stays code, not a quote",
        L"Its second item", L"All surrounding content stays in place."};
    size_t from = 0;
    float previousY = -1;
    for (const wchar_t* token : order) {
        const size_t at = app.docText.find(token, from);
        const auto* rect = at == std::wstring::npos ? nullptr : textAt(app, at);
        if (!rect || rect->rect.top < previousY) std::wcerr << L"  missing or out of order: " << token << L'\n';
        check(rect && rect->rect.top >= previousY, "fixture text is on the page in source order");
        check(at == std::wstring::npos || app.docText.find(token, at + 1) == std::wstring::npos,
              "fixture text appears once");
        if (at != std::wstring::npos) from = at + std::wcslen(token);
        if (rect) previousY = rect->rect.top;
    }

    const auto quoteBars = bars(app);
    check(quoteBars.size() == 18, "every HTML and Markdown quote keeps its bar");
    for (const auto& bar : quoteBars)
        check(bar.bottom - bar.top > 10, "no quote bar is empty");
    check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1 && app.headings.size() == 6,
          "the table, the code block and every heading survive");

    // Inline tags and line breaks inside bare text
    const size_t bold = app.docText.find(L"bold", app.docText.find(L"Bare text with"));
    check(std::any_of(app.docBoldRanges.begin(), app.docBoldRanges.end(), [&](const auto& range) {
        return range.first <= bold && bold < range.second;
    }), "<b> in a bare quote stays bold");
    check(std::any_of(app.linkRects.begin(), app.linkRects.end(), [](const auto& link) {
        return link.url == "https://example.com/quote";
    }), "<a> in a bare quote stays a link");
    const auto* first = textOf(app, L"Bare text with");
    const auto* second = textOf(app, L"a second line after a break");
    const auto* third = textOf(app, L"and a third line.");
    check(first && second && third && closeEnough(second->rect.left, first->rect.left) &&
          closeEnough(third->rect.left, first->rect.left) && second->rect.top > first->rect.top &&
          third->rect.top > second->rect.top, "each <br> starts a line of the same quote");

    // The anchor before a quote's <p> takes no room
    const auto* anchored = textOf(app, L"A paragraph after an anchor");
    const auto anchor = std::find_if(app.htmlAnchors.begin(), app.htmlAnchors.end(), [](const auto& entry) {
        return entry.first == "quoted-anchor";
    });
    check(anchored && anchor != app.htmlAnchors.end() && closeEnough(anchor->second, anchored->rect.top) &&
          std::any_of(quoteBars.begin(), quoteBars.end(), [&](const auto& bar) {
              return closeEnough(bar.top, anchored->rect.top);
          }), "an anchor-only run in a quote pins its target without a blank line");

    // Nested quotes indent each level and return to it after an inner quote
    const auto* outer = textOf(app, L"Outer bare text");
    const auto* inner = textOf(app, L"Inner bare text");
    const auto* innermost = textOf(app, L"Innermost paragraph");
    const auto* innerAfter = textOf(app, L"Inner text after");
    const auto* outerAfter = textOf(app, L"Outer text after");
    check(outer && inner && innermost && innerAfter && outerAfter && inner->rect.left > outer->rect.left &&
          innermost->rect.left > inner->rect.left && closeEnough(innerAfter->rect.left, inner->rect.left) &&
          closeEnough(outerAfter->rect.left, outer->rect.left), "nested bare quotes keep their levels");

    // Exports and a copy of the whole page keep the bare text
    namespace fs = std::filesystem;
    const fs::path output = fs::current_path() / "blockquote-test-output";
    fs::create_directories(output);
    check(exportHtmlFile(app, (output / L"html-quotes.html").wstring()), "HTML export succeeds");
    check(exportDocxFile(app, (output / L"html-quotes.docx").wstring()), "Word export succeeds");
    const std::string html = read(output / L"html-quotes.html");
    const std::string docx = read(output / L"html-quotes.docx");  // stored entries: the XML reads as is
    for (const char* token : {"Bare text in an HTML quote", "Bare text on a line of its own", "Bare text before a paragraph",
                              "Bare text after the paragraph.", "Outer bare text", "Inner bare text",
                              "Inner text after the innermost quote", "Outer text after the inner quote",
                              "Three levels of bare text", "Bare text between Markdown quotes",
                              "Bare text of an HTML quote inside a Markdown quote"}) {
        check(html.find(std::string("<blockquote>") + token) != std::string::npos ||
              html.find(std::string("\n") + token) != std::string::npos, "HTML export keeps bare quote text");
        const size_t at = docx.find(std::string(">") + token);
        const size_t paragraph = at == std::string::npos ? std::string::npos : docx.rfind("<w:p>", at);
        if (at == std::string::npos) std::cerr << "  Word lost: " << token << '\n';
        check(paragraph != std::string::npos && docx.find("<w:pBdr>", paragraph) < at,
              "Word keeps bare quote text in a bordered quote paragraph");
    }
    // Word runs raw HTML in a list item into the item's own paragraph, a
    // <p> quote's text too: the text stays, without the quote's border
    check(html.find("<blockquote>Bare text in a quote inside a list item") != std::string::npos &&
          docx.find(">Bare text in a quote inside a list item<") != std::string::npos,
          "both exports keep the bare text of a quote in a list item");
    const size_t start = docx.find(">Bare text with ");
    const size_t open = start == std::string::npos ? std::string::npos : docx.rfind("<w:p>", start);
    const std::string paragraph = open == std::string::npos ? "" : docx.substr(open, docx.find("</w:p>", start) - open);
    size_t breaks = 0;
    for (size_t at = paragraph.find("<w:br/>"); at != std::string::npos; at = paragraph.find("<w:br/>", at + 1)) ++breaks;
    check(paragraph.find("<w:b/>") != std::string::npos && paragraph.find(">bold<") != std::string::npos &&
          paragraph.find("<w:hyperlink") != std::string::npos && paragraph.find(">quoted link<") != std::string::npos &&
          paragraph.find("w:val=\"subscript\"") != std::string::npos && breaks == 2 &&
          paragraph.find(">and a third line.<") != std::string::npos,
          "Word keeps the inline quote one paragraph, with its bold, link, subscript and breaks");
    const std::wstring copy = selectionHtmlForRange(app, 0, app.docText.size());
    check(copy.find(L"Bare text in an HTML quote") != std::wstring::npos &&
          copy.find(L"Bare text with <b>bold</b>") != std::wstring::npos &&
          copy.find(L"<a href=\"https://example.com/quote\">quoted link</a>") != std::wstring::npos,
          "a copy of the page keeps bare quote text, its bold and its link");
}
}

int main() {
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!initD2D(app)) return 2;
        app.height = 900;
        app.readingWidthPct = 100;
        const std::string htmlQuotes = read(TINTA_HTML_QUOTE_FIXTURE);
        check(!htmlQuotes.empty(), "HTML quote fixture exists");
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f}) {
                app.contentScale = scale;
                updateTextFormats(app);
                for (int width : {1050, 650}) {
                    app.width = width;
                    paragraphCases(app);
                    blockCases(app);
                    std::ifstream file(TINTA_QUOTE_FIXTURE, std::ios::binary);
                    check(file.good(), "mixed quote fixture exists");
                    layout(app, {(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()});
                    check(bars(app).size() == 9, "all plain and nested quote bars survive the mixed fixture");
                    check(app.tableRects.size() == 2 && app.codeBlocks.size() == 2, "mixed tables and fences survive");
                    check(app.docText.find(L"All surrounding content") != std::wstring::npos, "final mixed content renders");
                    htmlBareTextCases(app);
                    htmlFixture(app, htmlQuotes);
                }
            }
        }
    }
    CoUninitialize();
    std::cout << "Blockquote layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}
