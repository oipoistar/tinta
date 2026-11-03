// #314: equations in a document as the reader lays them out. A $$ equation
// stands on its own line wherever it sits in a paragraph, two in a row
// included; a \tag's number stands at the line's right, clear of the
// equation; \eqref reads the number a \label's \tag gives it; and a display
// equation that falls back to its source shows its $$.
#include "d2d_init.h"
#include "export.h"
#include "math_render.h"
#include "render.h"
#include "utils.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

namespace {
int failures = 0;
std::string scenario;
void check(bool ok, const std::string& message) {
    if (!ok) {
        if (failures < 40) std::cerr << "FAIL math markdown: " << scenario << ": " << message << '\n';
        ++failures;
    }
}

// The text rect a piece of source the page lays out as one atom, by its text
const App::TextRect* rectOf(const App& app, const std::wstring& text) {
    const size_t at = app.docText.find(text);
    if (at == std::wstring::npos) return nullptr;
    for (const auto& r : app.textRects) {
        if (r.docStart <= at && at < r.docStart + r.docLength) return &r;
    }
    return nullptr;
}

// A math box's runs carry its whole box as their bounds: the ones standing
// at the column's right edge within an equation's height are its number
bool numberBeside(const App& app, const D2D1_RECT_F& equation, D2D1_RECT_F& number) {
    const float right = app.layoutIndent + app.layoutMaxWidth;
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength || run.selectable || std::fabs(run.bounds.right - right) > 0.5f) continue;
        if (run.bounds.bottom < equation.top - 1 || run.bounds.top > equation.bottom + run.bounds.bottom - run.bounds.top) continue;
        number = run.bounds;
        return true;
    }
    return false;
}

void checkLayout(App& app, const std::string& source) {
    auto doc = app.parser.parse(source);
    check(doc.success, "the fixture parses");
    if (!doc.success) return;
    app.root = doc.root;
    layoutDocument(app);
    const float em = app.textFormat ? app.textFormat->GetFontSize() : 16.0f;
    const float left = app.layoutIndent, right = app.layoutIndent + app.layoutMaxWidth;

    // The tagged jump condition: rendered, its number at the right, clear
    const App::TextRect* jump = rectOf(app, L"\\tag{1.8}");
    D2D1_RECT_F number{};
    check(jump != nullptr, "the tagged equation lays out as one equation");
    if (jump) {
        check(numberBeside(app, jump->rect, number), "its number stands at the column's right");
        check(number.left >= jump->rect.right + em - 0.5f || number.top >= jump->rect.bottom - 0.5f,
              "its number keeps an em clear of it, or goes under it");
    }

    // Two in a row: two equations, one under the other, the first centred
    const App::TextRect* scalesA = rectOf(app, L"U_c = \\sqrt");
    const App::TextRect* scalesB = rectOf(app, L"\\rho_{e,c}");
    check(scalesA && scalesB, "two equations in one paragraph lay out as two");
    if (scalesA && scalesB) {
        check(scalesB->rect.top >= scalesA->rect.bottom - 0.5f, "the second stands under the first");
        const float before = scalesA->rect.left - left, after = right - scalesA->rect.right;
        check(scalesA->rect.right - scalesA->rect.left > app.layoutMaxWidth || std::fabs(before - after) < 1.0f,
              "the first is centred on the column");
        check(numberBeside(app, scalesB->rect, number), "the second's number stands at the right");
    }

    // Words, an equation, words: three lines, the equation between
    const App::TextRect* wordsBefore = rectOf(app, L"A paragraph with words");
    const App::TextRect* circle = rectOf(app, L"x^2 + y^2 = r^2");
    const App::TextRect* wordsAfter = rectOf(app, L"and words after");
    check(wordsBefore && circle && wordsAfter, "words around an equation lay out");
    if (wordsBefore && circle && wordsAfter) {
        check(wordsBefore->rect.bottom <= circle->rect.top + 0.5f && wordsAfter->rect.top >= circle->rect.bottom - 0.5f,
              "the equation stands on its own line between the words");
        check(numberBeside(app, circle->rect, number), "tag* sets its words at the right");
    }

    // References read their equations' numbers
    check(app.mathLabels[L"eq:jump"] == L"1.8" && app.mathLabels[L"eq:scales"] == L"2.1" &&
              app.mathLabels.find(L"eq:missing") == app.mathLabels.end(),
          "labels take their equations' numbers");
    check(mathResolveRefs(L"\\eqref{eq:jump}", app.mathLabels) == L"\\text{(1.8)}", "eqref reads the number");

    // Falling back, a display equation keeps its $$; a code block keeps its text
    check(app.docText.find(L"$$\\unknowncommand{x}$$") != std::wstring::npos, "a display equation's source shows its $$");
    check(app.docText.find(L"$$x \\tag{9}$$ as text") != std::wstring::npos, "a code block's dollars stay text");

    // Nothing of an equation reaches past a number beside it
    for (const auto& r : app.textRects) {
        const std::wstring piece = app.docText.substr(r.docStart, r.docLength);
        if (piece.find(L"\\tag") == std::wstring::npos || piece.find(L"\\unknown") != std::wstring::npos) continue;
        if (piece.find(L"$") != std::wstring::npos) continue;  // inline: the number follows it
        if (numberBeside(app, r.rect, number))
            check(r.rect.right <= number.left - em + 0.5f || r.rect.bottom <= number.top + 0.5f,
                  "an equation never runs into its number");
    }
}
}  // namespace

int runMathMarkdownTests(const char* fixture) {
    std::ifstream file(fixture, std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    check(!source.empty(), "the fixture reads");
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!initD2D(app)) return 2;
        app.readingWidthPct = 100;
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f}) {
                app.contentScale = scale;
                updateTextFormats(app);
                for (int width : {1050, 650, 420}) {
                    app.width = static_cast<int>(width * scale);
                    app.height = 900;
                    scenario = "theme" + std::to_string(theme) + "-scale" + std::to_string((int)(scale * 100)) +
                               "-width" + std::to_string(width);
                    checkLayout(app, source);
                }
            }
        }
        // The exports: HTML sets each number at the right of its line, Word
        // at a right tab past the centred equation; references resolved
        applyTheme(app, 0);
        app.contentScale = 1.0f;
        updateTextFormats(app);
        app.width = 1050;
        scenario = "exports";
        checkLayout(app, source);
        auto readAll = [](const wchar_t* path) {
            std::ifstream in(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        check(exportHtmlFile(app, L"math-tags.html"), "the HTML export succeeds");
        const std::string html = readAll(L"math-tags.html");
        size_t numbers = 0;
        for (size_t at = html.find("class=\"math-tag\""); at != std::string::npos; at = html.find("class=\"math-tag\"", at + 1))
            ++numbers;
        check(numbers == 3, "HTML sets the three display numbers at the right: " + std::to_string(numbers));
        check(html.find(">(1.8)</text>") != std::string::npos && html.find(">(2.1)</text>") != std::string::npos,
              "HTML draws the numbers and the references that read them");
        check(exportDocxFile(app, L"math-tags.docx"), "the Word export succeeds");
        const std::string docx = readAll(L"math-tags.docx");
        check(docx.find("<w:tab w:val=\"right\" w:pos=\"9638\"/>") != std::string::npos &&
                  docx.find("<w:tab/><w:t>(1.8)</w:t>") != std::string::npos &&
                  docx.find("<w:tab/><w:t>(2.1)</w:t>") != std::string::npos &&
                  docx.find("<w:tab/><w:t>circle</w:t>") != std::string::npos,
              "Word sets each number at a right tab");
        check(docx.find("$$\\unknowncommand{x}$$") != std::string::npos,
              "Word keeps a list item's display equation, as its source when it falls back");
        app.clearLayoutCache();
        mathClearCache();
    }
    std::cout << "Math in Markdown: " << failures << " failures\n";
    return failures ? 1 : 0;
}
