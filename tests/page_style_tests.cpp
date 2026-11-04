// Per-element styles (docs/specs/theming.md, phase 2): a CSS theme's rules
// for one element at a time, drawn by the real layout. The reading column,
// margins, a first-line indent and centred text; a heading's border; a
// quote's bar, indent and colours; list markers; inline code chips; a code
// block's box; a table's grid, padding and stripes; a rule; and the same
// page under a built-in theme, which draws as it always has.
#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "frame_preview.h"
#include "render.h"
#include "utils.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool closeTo(float a, float b, float tolerance = 0.6f) { return std::fabs(a - b) <= tolerance; }
bool sameColor(D2D1_COLOR_F a, uint32_t rgb) {
    const D2D1_COLOR_F b = hexColor(rgb);
    return std::fabs(a.r - b.r) < 0.01f && std::fabs(a.g - b.g) < 0.01f && std::fabs(a.b - b.b) < 0.01f;
}

const char* const kSheet =
    "body { color: #202020; max-width: 600px; }\n"
    "p { margin: 0 0 20px; }\n"
    "p + p { text-indent: 32px; }\n"
    "h1 { text-align: center; }\n"
    "h2 { border-bottom: 3px solid #ff0000; padding-bottom: 5px; }\n"
    "blockquote { border-left: 6px solid #00aa00; padding-left: 30px; color: #555555; }\n"
    "blockquote strong { color: #aa00aa; }\n"
    "a { text-decoration: none; }\n"
    "ul { list-style-type: square; padding-left: 40px; }\n"
    "ol { list-style-type: upper-roman; }\n"
    "li::marker { color: #0000ff; }\n"
    "code { padding: 0 6px; border-radius: 4px; background: #eeeeee; }\n"
    "pre { padding: 20px; border-radius: 8px; border: 1px solid #cccccc; }\n"
    "th, td { padding: 2px 20px; border: 2px solid #123456; }\n"
    "tbody tr:nth-child(even) td { background-color: #fafafa; }\n"
    "hr { border-top: 5px solid #999999; }\n";

const char* const kPage =
    "# Centred title\n\n"
    "First paragraph with a [link](https://example.com) and `code`.\n\n"
    "Second paragraph, its first line indented.\n\n"
    "## A ruled heading\n\n"
    "> A quote with **strong** words.\n\n"
    "- one\n- two\n\n"
    "1. alpha\n2. beta\n\n"
    "| A | B |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\n\n"
    "```js\nlet x = 1;\n```\n\n"
    "---\n\n"
    "The end.\n";

// The text run that draws `text`, or null
const App::LayoutTextRun* runOf(const App& app, const std::wstring& text) {
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && run.docStart + run.docLength <= app.docText.size() &&
            app.docText.compare(run.docStart, run.docLength, text) == 0) {
            return &run;
        }
    }
    return nullptr;
}
const App::LayoutTextRun* runStarting(const App& app, const std::wstring& text) {
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength >= text.size() && run.docStart + text.size() <= app.docText.size() &&
            app.docText.compare(run.docStart, text.size(), text) == 0) {
            return &run;
        }
    }
    return nullptr;
}
bool hasLine(const App& app, uint32_t rgb, float stroke) {
    for (const auto& line : app.layoutLines) {
        if (sameColor(line.color, rgb) && closeTo(line.stroke, stroke, 0.05f)) return true;
    }
    return false;
}

void styled(App& app, const fs::path& dir) {
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    { std::ofstream(themes / "Boxes.css", std::ios::binary) << kSheet; }
    loadCustomThemes();
    int boxes = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"Boxes") boxes = i;
    }
    check(boxes >= 0, "the sheet is listed");
    if (boxes < 0) return;
    applyTheme(app, boxes);
    app.root = app.parser.parse(kPage).root;
    app.scrollY = app.targetScrollY = 0;
    layoutDocument(app);
    const float scale = app.contentScale * app.zoomFactor;
    const float left = app.layoutIndent;

    check(closeTo(app.layoutMaxWidth, 600 * scale), "max-width on body sets the reading column");

    const auto* title = runOf(app, L"Centred title");
    check(title && closeTo((title->bounds.left + title->bounds.right) / 2, left + app.layoutMaxWidth / 2, 1.0f),
          "text-align: center centres the heading");

    const auto* first = runStarting(app, L"First paragraph");
    const auto* second = runStarting(app, L"Second paragraph");
    check(first && closeTo(first->bounds.left, left), "the first paragraph starts at the column's edge");
    check(second && closeTo(second->bounds.left, left + 32 * scale), "p + p indents the second paragraph's first line");
    check(first && second && closeTo(second->bounds.top - first->bounds.bottom, 20 * scale, 1.0f),
          "p's margin-bottom spaces the paragraphs");

    const auto* link = runOf(app, L"link");
    bool underlined = false;
    for (const auto& line : app.layoutLines) {
        if (link && line.p1.y > link->bounds.top && line.p1.y <= link->bounds.bottom + 1 &&
            line.p1.x <= link->bounds.left + 1 && line.p2.x >= link->bounds.right - 1)
            underlined = true;
    }
    check(link && !underlined, "text-decoration: none takes a link's rule away");

    bool chip = false;
    for (const auto& rect : app.layoutRects) chip = chip || (sameColor(rect.color, 0xEEEEEE) && closeTo(rect.radius, 4 * scale));
    check(chip, "inline code gets the sheet's rounded chip");

    check(hasLine(app, 0xFF0000, 3 * scale), "the heading's border-bottom is drawn as its rule");

    bool bar = false;
    for (const auto& rect : app.layoutRects) {
        bar = bar || (sameColor(rect.color, 0x00AA00) && closeTo(rect.rect.right - rect.rect.left, 6 * scale));
    }
    check(bar, "the quote's border-left is its bar");
    const auto* quote = runStarting(app, L"A quote");
    check(quote && closeTo(quote->bounds.left, left + 36 * scale) && sameColor(quote->color, 0x555555),
          "the quote's text sits past its border and padding, in its colour");
    const auto* strong = runOf(app, L"strong");
    check(strong && sameColor(strong->color, 0xAA00AA), "blockquote strong takes its own colour");

    const auto* one = runOf(app, L"one");
    check(one && closeTo(one->bounds.left, left + 40 * scale), "padding-left on ul sets the list's indent");
    int squares = 0;
    for (const auto& shape : app.layoutShapes) {
        if (shape.type == App::LayoutShapeType::Rectangle && sameColor(shape.fill, 0x0000FF)) squares++;
    }
    check(squares == 2, "list-style-type: square, in ::marker's colour");
    // Markers carry no document text: the two blue ones are I. and II.
    std::vector<float> romans;
    for (const auto& run : app.layoutTextRuns) {
        DWRITE_TEXT_METRICS m{};
        if (!run.docLength && sameColor(run.color, 0x0000FF) && run.layout && SUCCEEDED(run.layout->GetMetrics(&m)))
            romans.push_back(m.width);
    }
    check(romans.size() == 2 && romans[1] > romans[0], "upper-roman numbers the ordered list, in ::marker's colour");

    bool card = false;
    for (const auto& rect : app.layoutRects) {
        card = card || (closeTo(rect.radius, 8 * scale) && closeTo(rect.strokeWidth, 1 * scale) && sameColor(rect.stroke, 0xCCCCCC));
    }
    check(card, "the code block gets the sheet's corners and edge");
    bool padded = false;
    for (const auto& rect : app.textRects) {
        if (rect.docStart + 3 <= app.docText.size() && app.docText.compare(rect.docStart, 3, L"let") == 0)
            padded = closeTo(rect.rect.left, left + 20 * scale);
    }
    check(padded, "the code block's padding");

    check(hasLine(app, 0x123456, 2 * scale), "the table's grid in the cells' border");
    int stripes = 0;
    for (const auto& rect : app.layoutRects) stripes += sameColor(rect.color, 0xFAFAFA) ? 1 : 0;
    check(stripes == 1, "tbody tr:nth-child(even) stripes the second body row only");
    const auto* cell = runOf(app, L"1");
    check(cell && closeTo(cell->bounds.left, left + 20 * scale), "the cells' padding");

    check(hasLine(app, 0x999999, 5 * scale), "hr's border-top is the rule");

    check(saveFramePreview(app, dir / "page-styles.png"), "saved page-styles.png");
    fs::remove(themes / "Boxes.css");
    loadCustomThemes();
}

// The same page under Paper draws as before: Tinta's bullets, rules and chips
void unstyled(App& app) {
    applyTheme(app, 0);
    app.root = app.parser.parse(kPage).root;
    layoutDocument(app);
    int discs = 0;
    for (const auto& shape : app.layoutShapes) discs += shape.type == App::LayoutShapeType::Ellipse ? 1 : 0;
    check(discs == 2, "a built-in theme keeps its round bullets");
    const auto* second = runStarting(app, L"Second paragraph");
    check(second && closeTo(second->bounds.left, app.layoutIndent), "and starts every paragraph at the edge");
    for (const auto& rect : app.layoutRects) check(rect.radius == 0 && rect.strokeWidth == 0, "and square boxes");
}
}  // namespace

int testPageStyles(App&, const fs::path& dir) {
    failures = 0;
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Page styles", WS_POPUP, 0, 0, 1000, 1400, nullptr, nullptr, nullptr,
                               nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        check(false, "a window to draw the page in");
        return failures;
    }
    app.width = 1000;
    app.height = 1400;
    createTypography(app);
    styled(app, dir);
    unstyled(app);
    DestroyWindow(app.hwnd);
    std::cout << "Page styles: " << failures << " failures\n";
    return failures;
}
