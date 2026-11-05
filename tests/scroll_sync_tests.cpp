// #267: the edit-mode preview follows the editor through source-offset
// scroll anchors. A code block whose first line is indented took its source
// offset from md4c's own string of spaces, a value far past the document's
// end; every anchor after it was dropped, so the preview stopped following
// there and only moved again once the editor scrolled back up.
#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "input.h"
#include "render.h"
#include "settings.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { if (failures < 40) std::cerr << "FAIL scroll sync: " << message << '\n'; ++failures; }
}

void paintEditor(App& app) {
    app.renderTarget->BeginDraw();
    renderEditor(app, editorPaneWidth(app));
    check(SUCCEEDED(app.renderTarget->EndDraw()), "the source pane paints");
}

// Every element's offset lies inside the source, and the top-level blocks
// that have one come in source order
void offsetsInsideSource(App& app, const std::string& source) {
    auto doc = app.parser.parse(source);
    check(doc.success, "the document parses");
    if (!doc.root) return;
    size_t outside = 0;
    std::function<void(const qmd::ElementPtr&)> visit = [&](const qmd::ElementPtr& e) {
        if (e->sourceOffset != SIZE_MAX && e->sourceOffset > source.size()) ++outside;
        for (const auto& child : e->children) visit(child);
    };
    visit(doc.root);
    check(outside == 0, std::to_string(outside) + " elements have a source offset outside the document");
    size_t previous = 0;
    bool ordered = true;
    for (const auto& block : doc.root->children) {
        if (block->sourceOffset == SIZE_MAX) continue;
        ordered = ordered && block->sourceOffset >= previous;
        previous = block->sourceOffset;
    }
    check(ordered, "top-level blocks keep their source order");
}

// Source line of a heading, by its Markdown text
long headingLine(const App& app, const App::HeadingInfo& heading) {
    const std::wstring marker = std::wstring(heading.level, L'#') + L" " + heading.text;
    for (size_t line = 0; line < app.editorLineStarts.size(); ++line) {
        if (app.editorText.compare(app.editorLineStarts[line], marker.size(), marker) == 0) {
            const size_t end = app.editorLineStarts[line] + marker.size();
            if (end == app.editorText.size() || app.editorText[end] == L'\n' || app.editorText[end] == L'\r')
                return (long)line;
        }
    }
    return -1;
}

void followsEditor(App& app, const std::string& source, const char* path, int width, int height, bool wrap) {
    const std::string where = " (" + std::to_string(width) + "x" + std::to_string(height) +
                              (wrap ? ", wrap)" : ")");
    app.width = width; app.height = height;
    app.currentFile = path;
    enterEditMode(app);
    app.editorShowPreview = true;
    app.editorWordWrap = wrap;
    editorReparse(app);
    rebuildLineStarts(app);
    ensureLayoutComplete(app);
    app.editorScrollY = 0; paintEditor(app);

    // The anchors climb with the source and reach its end
    bool increasing = true, inside = true;
    for (size_t i = 0; i < app.scrollAnchors.size(); ++i) {
        inside = inside && app.scrollAnchors[i].sourceOffset <= source.size();
        if (i) increasing = increasing && app.scrollAnchors[i].sourceOffset > app.scrollAnchors[i - 1].sourceOffset;
    }
    check(!app.scrollAnchors.empty() && increasing && inside, "anchors climb inside the source" + where);
    check(!app.scrollAnchors.empty() && app.scrollAnchors.back().sourceOffset >= source.size() * 9 / 10,
          "the anchors reach the last tenth of the document" + where);

    // With the editor scrolled so each heading is its top line, the
    // preview shows that heading at its top
    const float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    const float editorMax = std::max(0.0f, app.editorContentHeight - (float)app.height);
    const float previewMax = std::max(0.0f, app.contentHeight - (float)app.height);
    float worst = 0.0f;
    std::wstring worstHeading;
    int checked = 0;
    for (const auto& heading : app.headings) {
        const long line = headingLine(app, heading);
        if (line <= 0) continue;
        const size_t row = wrap ? app.editorRowStarts[(size_t)line] : (size_t)line;
        const float scroll = row * lineHeight + 0.5f;
        if (scroll > editorMax - 1.0f) continue;  // the editor cannot scroll there
        app.editorScrollY = scroll;
        syncPreviewToEditor(app);
        const float expected = std::clamp(heading.y - documentContentTop(app), 0.0f, previewMax);
        const float error = std::abs(app.scrollY - expected);
        if (error > worst) { worst = error; worstHeading = heading.text; }
        ++checked;
    }
    check(checked >= 10, "enough headings sit where the editor can scroll" + where);
    if (worst > 80.0f) std::wcerr << L"  worst heading: " << worstHeading << L", off by " << worst << L" px\n";
    check(worst <= 80.0f, "the preview lands on the heading at the editor's top" + where);

    // As the reporter scrolls: the wheel over the source, top to bottom and
    // on past the editor's end (#85 lets the preview's tail scroll then),
    // with a frame after each notch. The preview never moves back, and
    // reaches its end.
    app.editorScrollY = 0; app.scrollY = app.targetScrollY = 0;
    syncPreviewToEditor(app);
    app.mouseX = (int)(editorPaneWidth(app) / 2); app.mouseY = app.height / 2;
    float previous = app.scrollY;
    bool forward = true;
    for (int notch = 0; notch < 4000 && app.scrollY < previewMax - 0.5f; ++notch) {
        handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (short)-WHEEL_DELTA), 0);
        paintEditor(app);
        syncPreviewToEditor(app);
        forward = forward && app.scrollY >= previous - 0.5f;
        previous = app.scrollY;
    }
    check(forward, "scrolling the source down never moves the preview up" + where);
    check(std::abs(app.scrollY - previewMax) <= 1.0f, "the wheel takes the preview to its end" + where);
    exitEditMode(app);
}
}

// The other direction, reported on #267 too: E opens the editor where the
// reader is. That maps the reading position to a source line through the
// same anchors, and past the bad one it opened the editor at the last line.
void editOpensWhereReading(App& app, const char* path, int width, int height) {
    const std::string where = " (" + std::to_string(width) + "x" + std::to_string(height) + ")";
    app.width = width; app.height = height;
    app.currentFile = path;
    app.root = nullptr;
    // Lay the document out for reading, as opening it does
    enterEditMode(app); exitEditMode(app);
    ensureLayoutComplete(app);
    const auto headings = app.headings;
    float worst = 0.0f;
    std::wstring worstHeading;
    int checked = 0;
    for (const auto& heading : headings) {
        if (heading.y <= documentContentTop(app) + 1.0f) continue;  // the top opens at the top
        app.scrollY = app.targetScrollY = heading.y + 1.0f;
        enterEditMode(app);
        const long line = headingLine(app, heading);
        size_t cursorLine = 0;
        while (cursorLine + 1 < app.editorLineStarts.size() &&
               app.editorLineStarts[cursorLine + 1] <= app.editorCursorPos) {
            ++cursorLine;
        }
        if (line >= 0) {
            const float off = std::abs((float)cursorLine - (float)line);
            if (off > worst) { worst = off; worstHeading = heading.text; }
            ++checked;
        }
        exitEditMode(app);
        ensureLayoutComplete(app);  // the reading layout and its anchors again
    }
    check(checked >= 10, "enough headings to open the editor at" + where);
    if (worst > 1.0f) std::wcerr << L"  E at heading " << worstHeading << L" opened " << worst << L" lines away\n";
    check(worst <= 1.0f, "E opens the editor at the heading being read" + where);
}

// A diagram fitted to the page is laid out again at a smaller scale. The
// first, larger pass left its nodes' anchors behind, below the diagram as
// drawn, and the editor's scroll sync followed those instead.
void fittedDiagramAnchors(App& app) {
    // Six branches stacked down the page, each a chain of single-word
    // labels too long to wrap: wider than the page however its labels fold
    std::string source = "# A wide diagram\n\n```mermaid\ngraph LR\n";
    for (int b = 1; b <= 6; ++b) {
        std::string from = "A";
        for (int k = 1; k <= 7; ++k) {
            const std::string id = "N" + std::to_string(b) + std::to_string(k);
            source += "  " + from + " --> " + id + "[Unbreakablewaypoint" + std::to_string(b) + std::to_string(k) + "]\n";
            from = id;
        }
    }
    source += "```\n\nA paragraph after the diagram.\n";
    auto parsed = parseDocument(app.parser, source, std::string());
    check(parsed.success, "the diagram document parses");
    app.root = parsed.root;
    app.width = 650; app.height = 900;
    app.fitBlocks.clear();
    layoutDocument(app);
    check(app.codeBlocks.size() == 1 && app.codeBlocks[0].fitCandidate,
          "unfitted, the diagram is wider than the page");
    app.fitBlocks = {0x80000000u};  // the page's first diagram, fitted
    layoutDocument(app);
    check(app.codeBlocks.size() == 1 && app.codeBlocks[0].isDiagram && app.codeBlocks[0].fitActive,
          "the wide diagram is fitted to the page");
    // A node's anchor is the top of its box: each must be the top of a box
    // the fitted diagram draws, not of one from the pass before
    const size_t nodesFrom = source.find("graph LR") + 8, nodesTo = source.find("```\n\nA paragraph");
    int nodes = 0;
    bool drawn = true;
    for (const auto& anchor : app.scrollAnchors) {
        if (anchor.sourceOffset <= nodesFrom || anchor.sourceOffset >= nodesTo) continue;
        ++nodes;
        drawn = drawn && std::any_of(app.layoutShapes.begin(), app.layoutShapes.end(), [&](const App::LayoutShape& shape) {
            return std::abs(shape.rect.top - anchor.renderedY) < 0.5f;
        });
    }
    check(nodes >= 6 * 7, "every node of the fitted diagram has a scroll anchor");
    check(drawn, "a fitted diagram's node anchors sit on the nodes as drawn");
    app.fitBlocks.clear();
}

int runScrollSyncTests(const char* path) {
    namespace fs = std::filesystem;
    std::ifstream file(path, std::ios::binary);
    std::string original((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    check(!original.empty(), "the scroll sync document opens");
    std::string lf;
    for (size_t i = 0; i < original.size(); ++i)
        if (!(original[i] == '\r' && i + 1 < original.size() && original[i + 1] == '\n')) lf += original[i];
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Scroll sync tests", WS_POPUP, 0, 0, 1979, 1251,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1979; app.height = 1251;
    updateTextFormats(app);
    Settings settings; settings.keyProfile = "windows"; applyKeymap(app, settings);
    fittedDiagramAnchors(app);
    // The document with LF and with CRLF line endings, whichever the
    // checkout produced: the viewer reads files in text mode, the editor
    // raw, and a CRLF file opened the editor too far up (#267). Each copy
    // keeps its folder (images in ../assets).
    fs::path root = fs::path(path).parent_path();
    if (root.filename() == "docs") root = root.parent_path();
    const fs::path relative = fs::relative(fs::path(path), root);
    for (bool crlf : {false, true}) {
        const fs::path copy = fs::current_path() / (crlf ? "variant-crlf" : "variant-lf");
        std::error_code ec;
        fs::remove_all(copy, ec);
        fs::copy(root, copy, fs::copy_options::recursive, ec);
        check(!ec, "the document's folder copies");
        std::string source;
        for (char c : lf) { if (crlf && c == '\n') source += '\r'; source += c; }
        { std::ofstream out(copy / relative, std::ios::binary); out << source; }
        const std::string variant = (copy / relative).string();
        const std::string label = crlf ? " CRLF" : " LF";
        const int before = failures;
        offsetsInsideSource(app, source);
        // The reporter's window, then a smaller one; word wrap off and on
        for (auto size : {std::pair<int, int>{1979, 1251}, std::pair<int, int>{1050, 900}})
            for (bool wrap : {false, true})
                followsEditor(app, source, variant.c_str(), size.first, size.second, wrap);
        for (auto size : {std::pair<int, int>{1979, 1251}, std::pair<int, int>{1050, 900}})
            editOpensWhereReading(app, variant.c_str(), size.first, size.second);
        if (failures != before) std::cerr << "  (in the" << label << " copy)\n";
    }
    DestroyWindow(app.hwnd); app.hwnd = nullptr; state.reset(); CoUninitialize();
    std::cout << "Scroll sync: " << failures << " failures\n";
    return failures ? 1 : 0;
}
