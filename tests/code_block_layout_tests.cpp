#include "c4_label_checks.h"
#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "export.h"
#include "input.h"
#include "mermaid_ext.h"
#include "mermaid_layout_checks.h"
#include "render.h"
#include "utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <tuple>

namespace {
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool closeEnough(float a, float b) { return std::abs(a - b) < 0.05f; }

void checkDocument(App& app, const qmd::ParseResult& doc, int rows,
                   const std::wstring& source, const std::wstring& copy,
                   const char* label) {
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
    check(block.codeText == source, std::string(label) + ": block source retains all newlines");
    // #269: the copy button leaves out only the last line's break
    check(codeCopyText(block.codeText) == copy,
          std::string(label) + ": copied text ends at the last line, blank lines kept");
    check(app.docText.find(source) != std::wstring::npos,
          std::string(label) + ": selectable/searchable text retains all newlines");
    for (const auto& run : app.layoutTextRuns) {
        check(run.pos.y >= block.bounds.top + 12.0f * scale - 0.05f &&
              run.pos.y + 20.0f * scale <= block.bounds.bottom - 12.0f * scale + 0.05f,
              std::string(label) + ": no text layout exists in a phantom bottom row");
    }
}

void testCases(App& app) {
    struct Case { const char* code; int rows; const wchar_t* copy; };
    for (const auto& item : {
            Case{"single line", 1, L"single line"},
            Case{"single line\n", 1, L"single line"},
            Case{"single line\r\n", 1, L"single line"},
            Case{"first\nsecond\n", 2, L"first\nsecond"},
            Case{"first\n\nlast\n", 3, L"first\n\nlast"},
            Case{"line\n\n", 2, L"line\n"},
            Case{"line\n\n\n", 3, L"line\n\n"},
            Case{"line\r\n\r\n", 2, L"line\r\n"},
            Case{"\n", 1, L""}, Case{"\n\n", 2, L"\n"}, Case{"", 1, L""}}) {
        for (const char* path : {"fixture.txt", "fixture.json"}) {
            checkDocument(app, parseDocument(app.parser, item.code, std::string_view(path)),
                          item.rows, toWide(item.code), item.copy, "raw code");
        }
    }
    checkDocument(app, app.parser.parse("```text\nsingle line\n```\n"),
                  1, L"single line\n", L"single line", "reporter LF fence");
    checkDocument(app, app.parser.parse("```text\r\nsingle line\r\n```\r\n"),
                  1, L"single line\n", L"single line", "CRLF fence");
    checkDocument(app, app.parser.parse("```cpp\nint x = 1;\n```"),
                  1, L"int x = 1;\n", L"int x = 1;", "closing fence at EOF");
    checkDocument(app, app.parser.parse("```text\nsingle line"),
                  1, L"single line\n", L"single line", "unclosed fence at EOF");
    checkDocument(app, app.parser.parse("```text\nline\n\n```\n"),
                  2, L"line\n\n", L"line\n", "intentional trailing blank line");
    checkDocument(app, app.parser.parse("```text\n```\n"),
                  1, L"", L"", "empty fence minimum height");
    checkDocument(app, app.parser.parse("```mermaid\nnot a diagram\n```\n"),
                  1, L"not a diagram\n", L"not a diagram", "invalid Mermaid falls back to code");
}

// Line breaks spelled out, for failure messages
std::string escaped(const std::wstring& text) {
    std::string out;
    for (wchar_t c : text) {
        if (c == L'\n') out += "\\n";
        else if (c == L'\r') out += "\\r";
        else if (c < 128) out += static_cast<char>(c);
        else out += '?';
    }
    return out;
}

// #261: in the app's own layout, Mermaid fences that open with YAML front
// matter are drawn as diagrams, not shown as source
void testMermaidFrontMatter(App& app) {
    std::ifstream file(TINTA_MERMAID_FIXTURE, std::ios::binary);
    check(file.good(), "Mermaid front matter fixture is available");
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    app.root = app.parser.parse(source).root;
    layoutDocument(app);
    ensureLayoutComplete(app);
    check(app.codeBlocks.size() == 30, "the fixture lays out its 30 Mermaid fences");
    // As in mermaid_tests: every front matter fence draws, and so do the
    // ER docs examples whose syntax is supported
    const std::vector<size_t> draws = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 14, 15, 16, 18, 19, 30};
    for (size_t i = 0; i < app.codeBlocks.size(); ++i) {
        const bool expected = std::find(draws.begin(), draws.end(), i + 1) != draws.end();
        check(app.codeBlocks[i].isDiagram == expected,
              "fence " + std::to_string(i + 1) + (expected ? " is drawn as a diagram"
                                                            : " falls back to its source"));
    }
    check(app.docText.find(L"still lays out below it") != std::wstring::npos,
          "text after the last diagram lays out");
}

// #281: a flowchart whose quoted edge labels run over lines draws, and each
// label keeps its own lines: a word longer than the label's cap widens the
// label instead of breaking, and no last letter wraps on its own
void testMermaidMultilineLabels(App& app) {
    std::ifstream file(TINTA_MERMAID_LABELS_FIXTURE, std::ios::binary);
    check(file.good(), "Mermaid multi-line label fixture is available");
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    app.root = app.parser.parse(source).root;
    layoutDocument(app);
    ensureLayoutComplete(app);
    check(app.codeBlocks.size() == 2 && app.codeBlocks[0].isDiagram && app.codeBlocks[1].isDiagram,
          "both fences draw as diagrams, not as their source");
    const std::vector<std::wstring> labels = {
        L"Nop", L"Push:PERSONS", L"Push:ORDERS\nPush:LABORDERS", L"Push:LABORDERPANELS",
        L"Push:LABORDERPANELRESULTS", L"Pop\nPop\nPop\nPop", L"Pop\nPop\nPush:LABORDERPANELS",
        L"first line\nsecond line", L"Quoted", L"an --> inside", L"plain text"};
    int found = 0;
    for (const auto& run : app.layoutTextRuns) {
        if (!run.layout || run.docStart + run.docLength > app.docText.size()) continue;
        const std::wstring text = app.docText.substr(run.docStart, run.docLength);
        if (std::find(labels.begin(), labels.end(), text) == labels.end()) continue;
        UINT32 lines = 0;
        run.layout->GetLineMetrics(nullptr, 0, &lines);
        const UINT32 expected = 1 + (UINT32)std::count(text.begin(), text.end(), L'\n');
        std::string narrow = toUtf8(text);
        std::replace(narrow.begin(), narrow.end(), '\n', '|');
        check(lines == expected, "label \"" + narrow + "\" keeps " + std::to_string(expected) +
                                     " line(s), laid out in " + std::to_string(lines));
        ++found;
    }
    check(found >= 25, "the diagrams' labels are laid out (" + std::to_string(found) + ")");
}

// State diagrams with notes, loops, back edges and composite states among
// other Markdown: every fence draws, and with the app's own text no label,
// note or edge lands on a state, a note or another label
void testMermaidStateDiagrams(App& app) {
    std::ifstream file(TINTA_MERMAID_STATES_FIXTURE, std::ios::binary);
    check(file.good(), "Mermaid state diagram fixture is available");
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    app.root = app.parser.parse(source).root;
    layoutDocument(app);
    ensureLayoutComplete(app);
    int diagrams = 0;
    const float scale = app.contentScale * app.zoomFactor;
    for (const auto& block : app.codeBlocks) {
        if (!block.isDiagram) continue;
        ++diagrams;
        const std::string code = toUtf8(block.codeText);
        const auto built = buildDiagramPrims(app, code, scale);
        const auto problems = mermaidcheck::layoutProblems(
            built, scale, mermaidext::detectKind(code) == mermaidext::Kind::State);
        for (size_t i = 0; i < problems.size() && i < 8; ++i) {
            std::cerr << "  diagram " << diagrams << ": " << problems[i] << '\n';
        }
        check(problems.empty(), "nothing overlaps in diagram " + std::to_string(diagrams) +
                                    " with the app's text");
    }
    check(diagrams == 12, "the fixture's twelve diagrams draw, not " + std::to_string(diagrams));
    check(app.docText.find(L"Overlapping labels") != std::wstring::npos,
          "text after the diagrams lays out");
}

// Mermaid's asymmetric outline around a node: (0,0) (w,0) (w,h) (0,h), then
// the notch's tip at (h/2,h/2)
std::array<D2D1_POINT_2F, 5> asymmetricOutline(float left, float top, float right, float bottom) {
    const float h = bottom - top;
    return {{{left, top}, {right, top}, {right, bottom}, {left, bottom},
             {left + h * 0.5f, top + h * 0.5f}}};
}

float outlineDistance(D2D1_POINT_2F p, const std::array<D2D1_POINT_2F, 5>& outline) {
    float nearest = std::numeric_limits<float>::max();
    for (size_t i = 0; i < outline.size(); ++i) {
        const auto a = outline[i], b = outline[(i + 1) % outline.size()];
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy),
                                   0.0f, 1.0f);
        nearest = std::min(nearest, std::hypot(a.x + t * dx - p.x, a.y + t * dy - p.y));
    }
    return nearest;
}

bool atPoint(float x, float y, D2D1_POINT_2F point) {
    return std::hypot(x - point.x, y - point.y) < 0.5f;
}

// Mermaid's asymmetric shape, id>text]: in the app's layout each node is the
// notched outline (one geometry, which the screen and print both draw), its
// label keeps to the rectangle beside the notch, and every edge ends on the
// outline, at the notch's tip when it meets the left side. The HTML and Word
// exports draw the same polygon.
void testMermaidAsymmetricShape(App& app) {
    std::ifstream file(TINTA_MERMAID_ASYMMETRIC_FIXTURE, std::ios::binary);
    check(file.good(), "Mermaid asymmetric shape fixture is available");
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    app.root = app.parser.parse(source).root;
    layoutDocument(app);
    ensureLayoutComplete(app);
    std::vector<D2D1_RECT_F> diagrams;
    for (const auto& block : app.codeBlocks) {
        if (block.isDiagram) diagrams.push_back(block.bounds);
    }
    check(diagrams.size() == 6, "the fixture's six Mermaid fences draw as diagrams");
    check(app.docText.find(L"Text after the last diagram") != std::wstring::npos,
          "text after the last diagram lays out");

    auto contains = [](const D2D1_RECT_F& outer, const D2D1_RECT_F& inner) {
        return outer.left <= inner.left + 0.01f && outer.top <= inner.top + 0.01f &&
               outer.right >= inner.right - 0.01f && outer.bottom >= inner.bottom - 0.01f;
    };
    auto area = [](const D2D1_RECT_F& r) { return (r.right - r.left) * (r.bottom - r.top); };
    const std::vector<std::wstring> labels = {
        L"Route request", L"Show screen", L"Screen", L"Quoted > label", L"Two lines\nof label",
        L"One; two | three", L"Parse", L"Output", L"Flag", L"Back again", L"End", L"Arrow",
        L"Dotted", L"Dotted open", L"Thick text"};
    size_t found = 0, tips = 0;
    for (const auto& run : app.layoutTextRuns) {
        if (!run.layout || run.docStart + run.docLength > app.docText.size()) continue;
        const std::wstring text = app.docText.substr(run.docStart, run.docLength);
        if (std::find(labels.begin(), labels.end(), text) == labels.end() ||
            std::none_of(diagrams.begin(), diagrams.end(),
                         [&](const D2D1_RECT_F& d) { return contains(d, run.bounds); })) {
            continue;
        }
        ++found;
        std::string name = toUtf8(text);
        std::replace(name.begin(), name.end(), '\n', '|');
        name = "\"" + name + "\"";

        // The node is the smallest shape around its label; a group box is larger
        const App::LayoutShape* node = nullptr;
        for (const auto& shape : app.layoutShapes) {
            if (contains(shape.rect, run.bounds) && (!node || area(shape.rect) < area(node->rect))) {
                node = &shape;
            }
        }
        check(node && node->type == App::LayoutShapeType::Path && node->geometry,
              name + " is drawn as its notched outline");
        if (!node) continue;
        const D2D1_RECT_F r = node->rect;
        const float w = r.right - r.left, h = r.bottom - r.top, notch = h * 0.5f;

        // In the shape's own space: the notch is cut out of the left side,
        // the rest is filled
        if (node->geometry) {
            D2D1_RECT_F bounds{};
            node->geometry->GetBounds(nullptr, &bounds);
            check(std::abs(bounds.left) < 0.05f && std::abs(bounds.top) < 0.05f &&
                      std::abs(bounds.right - w) < 0.05f && std::abs(bounds.bottom - h) < 0.05f,
                  name + "'s outline spans its node");
            auto filled = [&](float x, float y) {
                BOOL inside = FALSE;
                node->geometry->FillContainsPoint(D2D1::Point2F(x, y), nullptr, &inside);
                return inside == TRUE;
            };
            check(!filled(2.0f, notch) && !filled(notch - 2.0f, notch) &&
                      filled(notch + 2.0f, notch) && filled(w - 2.0f, notch) &&
                      filled(notch * 0.5f, 2.0f) && filled(notch * 0.5f, h - 2.0f),
                  name + " has its notch on the left, its tip half the height in");
        }

        // The label keeps to the rectangle beside the notch, centred in it,
        // in its own lines
        DWRITE_TEXT_METRICS metrics{};
        run.layout->GetMetrics(&metrics);
        const UINT32 lines = 1 + (UINT32)std::count(text.begin(), text.end(), L'\n');
        check(metrics.lineCount == lines,
              name + " keeps " + std::to_string(lines) + " line(s), laid out in " +
                  std::to_string(metrics.lineCount));
        const float textLeft = run.pos.x + metrics.left;
        check(textLeft >= r.left + notch && textLeft + metrics.width <= r.right,
              name + " stays out of the notch");
        check(std::abs((run.bounds.left + run.bounds.right) - (r.left + notch + r.right)) < 1.0f,
              name + " is centred beside the notch");

        // Every edge end at the node is on the outline
        const auto outline = asymmetricOutline(r.left, r.top, r.right, r.bottom);
        for (const auto& connector : app.layoutConnectors) {
            if (connector.points.empty()) continue;
            for (const auto& end : {connector.points.front(), connector.points.back()}) {
                if (end.x < r.left - 1.0f || end.x > r.right + 1.0f ||
                    end.y < r.top - 1.0f || end.y > r.bottom + 1.0f) {
                    continue;
                }
                check(outlineDistance(end, outline) < 0.5f,
                      "an edge of " + name + " ends on its outline, not " +
                          std::to_string(outlineDistance(end, outline)) + " px off");
                if (atPoint(end.x, end.y, outline[4])) ++tips;
            }
        }
    }
    check(found == labels.size(),
          "the fifteen asymmetric labels are laid out (" + std::to_string(found) + ")");
    // Into S, C, D, Parse and Output from the left, out of Flag leftwards
    check(tips == 6, "six edges meet a notch's tip (" + std::to_string(tips) + ")");

    // The exports' polygon, label and edges
    std::set<std::tuple<size_t, float, float>> polygons;  // diagram, left, top
    size_t exportTips = 0;
    for (size_t b = 0; b < app.codeBlocks.size(); ++b) {
        if (!app.codeBlocks[b].isDiagram) continue;
        const auto built = buildFlowchartPrims(app, toUtf8(app.codeBlocks[b].codeText), 1.0f);
        check(built.ok, "fixture diagram " + std::to_string(b) + " builds for export");
        for (const auto& prim : built.prims) {
            if (prim.type != mermaidext::PrimType::Polygon || prim.pts.size() != 5) continue;
            const auto& p = prim.pts;
            const auto outline = asymmetricOutline(p[0].x, p[0].y, p[2].x, p[2].y);
            bool exact = true;
            for (size_t i = 0; i < outline.size(); ++i) exact = exact && atPoint(p[i].x, p[i].y, outline[i]);
            check(exact, "the export's polygon runs (0,0) (w,0) (w,h) (0,h) (h/2,h/2)");
            // A styled node's fill and stroke are two prims of one node
            if (!polygons.insert({b, p[0].x, p[0].y}).second) continue;
            for (const auto& other : built.prims) {
                if (other.type == mermaidext::PrimType::Text && other.x1 >= p[0].x &&
                    other.x2 <= p[2].x && other.y1 >= p[0].y && other.y2 <= p[2].y) {
                    check(other.x1 >= outline[4].x &&
                              std::abs((other.x1 + other.x2) - (outline[4].x + p[2].x)) < 1.0f,
                          "the export centres the label beside the notch");
                }
                // An edge's axis-aligned segment, not an arrowhead's wing
                if (other.type == mermaidext::PrimType::Line && std::abs(other.y1 - other.y2) < 0.01f &&
                    (atPoint(other.x1, other.y1, outline[4]) || atPoint(other.x2, other.y2, outline[4]))) {
                    ++exportTips;
                }
            }
        }
    }
    check(polygons.size() == labels.size(),
          "the export draws the fifteen asymmetric nodes (" + std::to_string(polygons.size()) + ")");
    check(exportTips == 6, "six exported edges meet a notch's tip (" + std::to_string(exportTips) + ")");

    // The last diagram's longer links draw as their short forms: three of
    // its nine edges dotted, three thick, three without a head
    if (diagrams.size() == 6) {
        const float scale = app.contentScale * app.zoomFactor;
        size_t edges = 0, dotted = 0, thick = 0, open = 0;
        for (const auto& connector : app.layoutConnectors) {
            if (connector.points.empty()) continue;
            const auto start = connector.points.front();
            if (start.x < diagrams[5].left || start.x > diagrams[5].right ||
                start.y < diagrams[5].top || start.y > diagrams[5].bottom) {
                continue;
            }
            ++edges;
            dotted += connector.dashed;
            thick += connector.stroke > 2.0f * scale;
            open += !connector.directed;
        }
        check(edges == 9 && dotted == 3 && thick == 3 && open == 3,
              "longer links draw as their short forms (" + std::to_string(edges) + " edges, " +
                  std::to_string(dotted) + " dotted, " + std::to_string(thick) + " thick, " +
                  std::to_string(open) + " open)");
    }
}

// C4 relation labels, measured with the app's fonts: every diagram of the
// mixed fixture draws, and each label stays clear of the boxes, frames,
// text, arrowheads and other labels, on its own relation's line
void testMermaidC4Labels(App& app) {
    std::ifstream file(TINTA_MERMAID_C4_FIXTURE, std::ios::binary);
    check(file.good(), "C4 label fixture is available");
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    app.root = app.parser.parse(source).root;
    layoutDocument(app);
    ensureLayoutComplete(app);
    const float scale = app.contentScale * app.zoomFactor;
    int diagrams = 0;
    for (const auto& block : app.codeBlocks) {
        if (!block.isDiagram) continue;
        ++diagrams;
        const std::string fence = toUtf8(block.codeText);
        const auto built = buildDiagramPrims(app, fence, scale);
        for (const auto& problem : c4checks::labelProblems(built, fence, scale))
            check(false, "C4 diagram " + std::to_string(diagrams) + ": " + problem);
    }
    check(diagrams == 8, "the fixture's eight C4 diagrams draw");
    check(app.docText.find(L"mermaid fences above: 8") != std::wstring::npos,
          "text after the C4 diagrams lays out");
}

std::wstring clipboardText(HWND window) {
    std::wstring result;
    // Clipboard-history tools may briefly hold a newly copied value
    bool opened = false;
    for (int i = 0; i < 100 && !(opened = OpenClipboard(window)); ++i) Sleep(5);
    if (!opened) { check(false, "test clipboard opens for inspection"); return {}; }
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (auto text = static_cast<const wchar_t*>(GlobalLock(data))) {
            result = text;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return result;
}

// #269: clicking each block's Copy button in the mixed fixture puts the
// block's lines on the clipboard without a break after the last one
void testCopyButton() {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Tinta code copy test", WS_POPUP,
        100, 100, 1050, 900, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        check(false, "code copy test initializes");
        if (app.hwnd) DestroyWindow(app.hwnd);
        return;
    }
    std::ifstream file(TINTA_COPY_FIXTURE, std::ios::binary);
    check(file.good(), "copy fixture is available");
    app.currentFile = TINTA_COPY_FIXTURE;  // a document, not the start page
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const wchar_t* expected[] = {
        L"single line",
        L"int main() {\n    return 0;\n}",
        L"line\n",
        L"print(\"tilde\")",
        L"echo \"in a list\"",
        L"{\"quoted\": true}",
        L"```js\nlet x = 1;   \n```",
    };
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float scale : {1.0f, 1.5f}) {
            app.contentScale = scale;
            updateTextFormats(app);
            for (int width : {1050, 650}) {
                app.width = width;
                app.height = 900;
                app.root = app.parser.parse(source).root;
                layoutDocument(app);
                ensureLayoutComplete(app);
                check(app.codeBlocks.size() == std::size(expected),
                      "the fixture has seven code blocks, not " +
                          std::to_string(app.codeBlocks.size()));
                for (size_t i = 0; i < app.codeBlocks.size() && i < std::size(expected); ++i) {
                    // Bring the block under the pointer, then press its button
                    const auto bounds = app.codeBlocks[i].bounds;
                    app.scrollX = 0;
                    app.scrollY = std::max(0.0f, bounds.top - 200.0f);
                    const float pad = 8.0f * app.contentScale * app.zoomFactor;
                    const float right = std::min(bounds.right, app.scrollX + documentViewportWidth(app));
                    const float x = right - pad - dpi(app, 36.0f) - app.scrollX + documentViewportX(app);
                    const float y = bounds.top + pad + dpi(app, 13.0f) - app.scrollY;
                    const LPARAM point = MAKELPARAM((int)x, (int)y);
                    // Programs that follow the clipboard (rdpclip over Remote
                    // Desktop, WSLg's msrdc, clipboard history) open it to
                    // read each change in the quarter second after it. A
                    // click then finds it held and copies nothing, leaving
                    // the sentinel, so the click is repeated after a pause.
                    // The sentinel is written once, since each write starts
                    // the readers over.
                    for (int i2 = 0; i2 < 100 && !copyToClipboard(app.hwnd, L"sentinel"); ++i2) Sleep(5);
                    std::wstring copied = L"sentinel";
                    int clicks = 0;
                    for (; clicks < 20 && copied == L"sentinel"; ++clicks) {
                        if (clicks) Sleep(50);
                        handleMouseMove(app, app.hwnd, point);
                        const bool over = app.hoveredCodeBlock == (int)i;
                        check(over, "the pointer is over the block's copy button");
                        if (!over) break;
                        handleMouseDown(app, app.hwnd, MK_LBUTTON, point);
                        handleMouseUp(app, app.hwnd, 0, point);
                        copied = clipboardText(app.hwnd);
                    }
                    check(copied == expected[i],
                          "Copy puts the block's lines on the clipboard, no break after the last: block " +
                              std::to_string(i) + " gave \"" + escaped(copied) + "\" (theme " +
                              std::to_string(theme) + ", scale " + std::to_string(scale) +
                              ", width " + std::to_string(width) + ", clicks " +
                              std::to_string(clicks) + ")");
                }
            }
        }
    }
    DestroyWindow(app.hwnd);
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
    // A row a line: "Wrap long lines in code blocks" off (#324)
    app.wrapCode = false;
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

    // On, as it is in a new setup: the wide line goes over rows, the page
    // is no wider than the window, and the blocks whose lines fit are as
    // they were
    std::vector<float> heights;
    for (const auto& block : app.codeBlocks) heights.push_back(block.bounds.bottom - block.bounds.top);
    app.wrapCode = true;
    layoutDocument(app);
    check(app.codeBlocks.size() == heights.size(), "wrapped, all nine mixed code blocks render");
    check(app.contentWidth <= app.width + 0.5f, "wrapped, wide code leaves the page as wide as the window");
    int taller = 0;
    for (size_t i = 0; i < app.codeBlocks.size() && i < heights.size(); ++i) {
        const auto& bounds = app.codeBlocks[i].bounds;
        const float rowsAdded = (bounds.bottom - bounds.top - heights[i]) / (20.0f * scale);
        check(rowsAdded > -0.01f && std::abs(rowsAdded - std::round(rowsAdded)) < 0.01f,
              "wrapped, a block is taller by whole rows or not at all");
        if (rowsAdded > 0.5f) ++taller;
        if (i) check(bounds.top > app.codeBlocks[i-1].bounds.bottom, "wrapped, mixed code blocks never overlap");
        for (const auto& item : app.layoutRects) {
            if (std::abs(item.rect.top - bounds.top) < 0.5f && std::abs(item.rect.left - bounds.left) < 0.5f) {
                check(item.rect.right <= bounds.right + 0.5f, "wrapped, no block's background grows past the column");
            }
        }
    }
    check(taller >= 1, "wrapped, the block with the wide line gains rows");
    check(app.docText.find(L"Final heading") != std::wstring::npos,
          "wrapped, content following the final code block renders");
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
                    testMermaidFrontMatter(app);
                    testMermaidMultilineLabels(app);
                    testMermaidC4Labels(app);
                    testMermaidAsymmetricShape(app);
                    testMermaidStateDiagrams(app);
                }
            }
        }
    }
    testCopyButton();
    CoUninitialize();
    std::cout << "Code block layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}
