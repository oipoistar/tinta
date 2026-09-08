#include "d2d_init.h"
#include "render.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>

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
        if (!app.layoutRects.empty())
            check(closeEnough(app.layoutRects.back().rect.bottom, lastTextBottom(app)),
                  "callout bar fits the body or standalone title");
    }
    layout(app, ">\n\nFollowing paragraph.\n");
    quoteBars = bars(app);
    check(quoteBars.size() == 1 && closeEnough(quoteBars[0].top, quoteBars[0].bottom), "empty quote does not acquire height");
}
}

int main() {
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!initD2D(app)) return 2;
        app.height = 900;
        app.readingWidthPct = 100;
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
                }
            }
        }
    }
    CoUninitialize();
    std::cout << "Blockquote layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}
