#include "d2d_init.h"
#include "document.h"
#include "render.h"
#include "utils.h"

#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>

namespace {
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool closeEnough(float a, float b) { return std::abs(a - b) < 0.05f; }

void checkDocument(App& app, const qmd::ParseResult& doc, int rows,
                   const std::wstring& source, const char* label) {
    check(doc.success, std::string(label) + ": document parses");
    app.root = doc.root;
    layoutDocument(app);
    check(app.codeBlocks.size() == 1, std::string(label) + ": one code block");
    if (app.codeBlocks.size() != 1) return;
    const auto& block = app.codeBlocks[0];
    float scale = app.contentScale * app.zoomFactor;
    float expectedHeight = (rows * 20.0f + 24.0f) * scale;
    check(closeEnough(block.bounds.bottom - block.bounds.top, expectedHeight),
          std::string(label) + ": actual rows plus top/bottom padding");
    check(block.codeText == source, std::string(label) + ": copy source retains all newlines");
    check(app.docText.find(source) != std::wstring::npos,
          std::string(label) + ": selectable/searchable text retains all newlines");
    for (const auto& run : app.layoutTextRuns) {
        check(run.pos.y >= block.bounds.top + 12.0f * scale - 0.05f &&
              run.pos.y + 20.0f * scale <= block.bounds.bottom - 12.0f * scale + 0.05f,
              std::string(label) + ": no text layout exists in a phantom bottom row");
    }
}

void testCases(App& app) {
    struct Case { const char* code; int rows; };
    for (const auto& item : {
            Case{"single line", 1}, Case{"single line\n", 1},
            Case{"single line\r\n", 1}, Case{"first\nsecond\n", 2},
            Case{"first\n\nlast\n", 3}, Case{"line\n\n", 2},
            Case{"line\n\n\n", 3}, Case{"line\r\n\r\n", 2},
            Case{"\n", 1}, Case{"\n\n", 2}, Case{"", 1}}) {
        for (const char* path : {"fixture.txt", "fixture.json"}) {
            checkDocument(app, parseDocument(app.parser, item.code, std::string_view(path)),
                          item.rows, toWide(item.code), "raw code");
        }
    }
    checkDocument(app, app.parser.parse("```text\nsingle line\n```\n"),
                  1, L"single line\n", "reporter LF fence");
    checkDocument(app, app.parser.parse("```text\r\nsingle line\r\n```\r\n"),
                  1, L"single line\n", "CRLF fence");
    checkDocument(app, app.parser.parse("```cpp\nint x = 1;\n```"),
                  1, L"int x = 1;\n", "closing fence at EOF");
    checkDocument(app, app.parser.parse("```text\nsingle line"),
                  1, L"single line\n", "unclosed fence at EOF");
    checkDocument(app, app.parser.parse("```text\nline\n\n```\n"),
                  2, L"line\n\n", "intentional trailing blank line");
    checkDocument(app, app.parser.parse("```text\n```\n"),
                  1, L"", "empty fence minimum height");
    checkDocument(app, app.parser.parse("```mermaid\nnot a diagram\n```\n"),
                  1, L"not a diagram\n", "invalid Mermaid falls back to code");
}

void testMixedFixture(App& app) {
    std::ifstream file(TINTA_CODE_FIXTURE, std::ios::binary);
    check(file.good(), "mixed fixture is available");
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto doc = app.parser.parse(source);
    check(doc.success, "mixed fixture parses");
    std::map<qmd::ElementType, int> counts;
    std::function<void(const qmd::ElementPtr&)> visit = [&](const qmd::ElementPtr& e) {
        ++counts[e->type];
        for (const auto& child : e->children) visit(child);
    };
    visit(doc.root);
    for (auto type : {qmd::ElementType::Heading, qmd::ElementType::Table,
                     qmd::ElementType::ListItem, qmd::ElementType::Strong,
                     qmd::ElementType::Emphasis, qmd::ElementType::Link,
                     qmd::ElementType::BlockQuote, qmd::ElementType::MathInline,
                     qmd::ElementType::MathDisplay})
        check(counts[type] > 0, "mixed content survives parsing");
    app.root = doc.root;
    layoutDocument(app);
    const int rows[] = {1, 1, 2, 4, 1, 1, 1, 1, 1};
    check(app.codeBlocks.size() == std::size(rows), "all nine mixed code blocks render");
    float scale = app.contentScale * app.zoomFactor;
    for (size_t i = 0; i < app.codeBlocks.size() && i < std::size(rows); ++i) {
        const auto& bounds = app.codeBlocks[i].bounds;
        check(closeEnough(bounds.bottom - bounds.top, (rows[i] * 20.0f + 24.0f) * scale),
              "mixed code block height reflects intentional rows");
        if (i) check(bounds.top > app.codeBlocks[i-1].bounds.bottom, "mixed code blocks never overlap");
    }
    check(app.docText.find(L"Final heading") != std::wstring::npos,
          "content following the final code block renders");
    check(app.contentWidth > app.width, "wide code still enables horizontal scrolling");
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
                    testCases(app);
                    testMixedFixture(app);
                }
            }
        }
    }
    CoUninitialize();
    std::cout << "Code block layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}
