// Lists, quotes and spans nested far deeper than any document needs. Every
// walk over the tree went one call deeper per level, so a thousand levels
// overflowed the stack (0xC0000005) in layout, the exports and freeing
// the tree. The parser stops nesting past a limit: each case keeps its
// text and lays out, exports, copies and frees.
#include "d2d_init.h"
#include "export.h"
#include "render.h"
#include "selection.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
int failures = 0;
std::string scenario;
void check(bool value, const std::string& message) {
    if (!value) { std::cerr << "FAIL: " << scenario << ": " << message << '\n'; ++failures; }
}
using qmd::Element;
using qmd::ElementPtr;
using qmd::ElementType;

// The deepest element below root, walked without recursion, so a tree
// that is still too deep fails the check rather than the test
size_t depthOf(const ElementPtr& root) {
    size_t deepest = 0;
    std::vector<std::pair<const Element*, size_t>> open{{root.get(), 0}};
    while (!open.empty()) {
        auto [elem, depth] = open.back();
        open.pop_back();
        deepest = std::max(deepest, depth);
        for (const auto& child : elem->children) open.push_back({child.get(), depth + 1});
    }
    return deepest;
}

// The tree's text in document order, also without recursion
std::string textOf(const ElementPtr& root) {
    std::string text;
    std::vector<const Element*> open{root.get()};
    while (!open.empty()) {
        const Element* elem = open.back();
        open.pop_back();
        text += elem->text;
        for (auto it = elem->children.rbegin(); it != elem->children.rend(); ++it) open.push_back(it->get());
    }
    return text;
}

// The text element that reads exactly value, and how many of its
// ancestors have the given type
size_t ancestorsOf(const ElementPtr& root, const std::string& value, ElementType type) {
    std::vector<const Element*> open{root.get()};
    while (!open.empty()) {
        const Element* elem = open.back();
        open.pop_back();
        if (elem->type == ElementType::Text && elem->text == value) {
            size_t count = 0;
            for (const Element* up = elem->parent; up; up = up->parent) count += up->type == type;
            return count;
        }
        for (const auto& child : elem->children) open.push_back(child.get());
    }
    return 0;
}

std::string repeat(const std::string& piece, int count) {
    std::string out;
    out.reserve(piece.size() * count);
    for (int i = 0; i < count; i++) out += piece;
    return out;
}

// prefix 1 .. prefix count appear in order, each once
bool numberedInOrder(const std::string& text, const std::string& prefix, int count) {
    size_t at = 0;
    for (int i = 1; i <= count; i++) {
        const std::string want = prefix + std::to_string(i);
        for (;; at++) {
            at = text.find(want, at);
            if (at == std::string::npos) return false;
            const size_t end = at + want.size();
            if (end >= text.size() || !std::isdigit(static_cast<unsigned char>(text[end]))) break;
        }
        at += want.size();
    }
    return true;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};
}

// Lays out at both widths, exports, copies the whole page and frees the
// tree: the walks that overflowed
void exercise(App& app, const ElementPtr& root, const std::wstring& name) {
    namespace fs = std::filesystem;
    app.root = root;
    for (int width : {1050, 650}) {
        app.width = width;
        layoutDocument(app);
        ensureLayoutComplete(app);
        check(!app.docText.empty(), "lays out at width " + std::to_string(width));
    }
    const fs::path output = fs::current_path() / "nesting-test-output";
    fs::create_directories(output);
    check(exportHtmlFile(app, (output / (name + L".html")).wstring()), "HTML export succeeds");
    check(exportDocxFile(app, (output / (name + L".docx")).wstring()), "Word export succeeds");
    check(!selectionHtmlForRange(app, 0, app.docText.size()).empty(), "copying the page succeeds");
    app.clearLayoutCache();
    app.root.reset();
}

void deepCases(App& app) {
    struct Case {
        const char* name;
        std::string source;
        std::string numbered;  // items "<numbered>1" .. that must all survive
        int count;
    };
    std::string mdList, quotedList, htmlList;
    for (int i = 1; i <= 1000; i++) {
        mdList += std::string(2 * (i - 1), ' ') + "- level " + std::to_string(i) + "\n";
        quotedList += repeat("> ", i) + "- level " + std::to_string(i) + "\n";
    }
    for (int i = 1; i <= 5000; i++) htmlList += "<ul><li>level " + std::to_string(i);
    htmlList += repeat("</li></ul>", 5000);
    const std::vector<Case> cases = {
        {"Markdown quotes", repeat("> ", 5000) + "deep\n\nAfter.\n", "", 0},
        {"Markdown list", mdList + "\nAfter.\n", "level ", 1000},
        {"Markdown list in quotes", quotedList + "\nAfter.\n", "level ", 1000},
        {"Markdown emphasis", "Start " + repeat("*a ", 5000) + "deep" + repeat(" b*", 5000) + " end.\n", "", 0},
        {"Markdown strong", "Start " + repeat("**a ", 5000) + "deep" + repeat(" b**", 5000) + " end.\n", "", 0},
        {"Markdown links around images",
         "Start " + repeat("[![", 5000) + "deep" + repeat("](i.png)](#a)", 5000) + " end.\n", "", 0},
        {"inline HTML spans", "Start " + repeat("<sub>", 5000) + "deep" + repeat("</sub>", 5000) + " end.\n", "", 0},
        // Each level's ~~ wraps the span inside it, one more level each
        {"Markdown strong in strikethrough",
         "Start " + repeat("~~**a ", 5000) + "deep" + repeat(" b**~~", 5000) + " end.\n", "", 0},
        {"inline HTML spans in highlights",
         "Start " + repeat("==<sub>a ", 5000) + "deep" + repeat(" b</sub>==", 5000) + " end.\n", "", 0},
        {"inline HTML marks", "Start " + repeat("<mark>", 20000) + "deep" + repeat("</mark>", 20000) + " end.\n", "", 0},
        {"HTML list", htmlList + "\n\nAfter.\n", "level ", 5000},
        {"HTML list left open", repeat("<ul><li>", 5000) + "deep\n\nAfter.\n", "", 0},
        {"HTML quotes", repeat("<blockquote>", 5000) + "deep" + repeat("</blockquote>", 5000) + "\n\nAfter.\n", "", 0},
        {"HTML spans in a block", "<div>" + repeat("<b>", 20000) + "deep" + repeat("</b>", 20000) + "</div>\n\nAfter.\n",
         "", 0},
    };
    for (const auto& item : cases) {
        scenario = item.name;
        const auto started = std::chrono::steady_clock::now();
        auto parsed = app.parser.parse("# Nesting\n\nBefore.\n\n" + item.source);
        check(parsed.success && parsed.root, "parses");
        if (!parsed.root) continue;
        const size_t depth = depthOf(parsed.root);
        check(depth <= 150, "stops nesting (depth " + std::to_string(depth) + ")");
        const std::string text = textOf(parsed.root);
        if (item.count) {
            check(numberedInOrder(text, item.numbered, item.count), "keeps every item in order");
        } else {
            check(text.find("deep") != std::string::npos, "keeps the innermost text");
        }
        check(text.find("Before.") != std::string::npos, "keeps the text before");
        exercise(app, parsed.root, L"case");
        parsed.root.reset();
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::cout << "  " << item.name << ": depth " << depth << ", " << seconds << " s\n";
    }
}

// The mixed fixture: ordinary nesting keeps every level, the deep parts
// nest up to the limit and keep all their text
void fixture(App& app) {
    scenario = "fixture";
    const std::string source = read(TINTA_DEEP_FIXTURE);
    check(!source.empty(), "deep nesting fixture is readable");
    auto parsed = app.parser.parse(source);
    check(parsed.success && parsed.root, "parses");
    if (!parsed.root) return;
    const ElementPtr root = parsed.root;
    check(depthOf(root) <= 150, "stops nesting");
    check(ancestorsOf(root, "Level 4", ElementType::List) == 4 &&
          ancestorsOf(root, "Third level", ElementType::BlockQuote) == 3,
          "ordinary lists and quotes keep every level");
    check(ancestorsOf(root, "level 50", ElementType::List) == 50 &&
          ancestorsOf(root, "level 60", ElementType::List) == 50,
          "a Markdown list nests fifty levels and goes on in the deepest");
    check(ancestorsOf(root, "Deep quote", ElementType::BlockQuote) == 100,
          "a Markdown quote nests a hundred levels");
    check(ancestorsOf(root, "html 50", ElementType::List) == 50 &&
          ancestorsOf(root, "html 60", ElementType::List) == 50,
          "an HTML list nests fifty levels and goes on in the deepest");
    const size_t htmlQuotes = ancestorsOf(root, "Deep HTML quote", ElementType::BlockQuote);
    check(htmlQuotes >= 95 && htmlQuotes <= 100, "an HTML quote nests about a hundred levels");
    const std::string text = textOf(root);
    check(numberedInOrder(text, "level ", 60) && numberedInOrder(text, "html ", 60),
          "every deep item keeps its text, in order");
    bool task = false;
    std::vector<const Element*> open{root.get()};
    while (!open.empty()) {
        const Element* elem = open.back();
        open.pop_back();
        task = task || (elem->type == ElementType::ListItem && elem->isTask);
        for (const auto& child : elem->children) open.push_back(child.get());
    }
    check(task, "the ordinary task item stays a task");
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        updateTextFormats(app);
        for (int width : {1050, 650}) {
            scenario = "fixture theme " + std::to_string(theme) + " width " + std::to_string(width);
            app.root = root;
            app.width = width;
            layoutDocument(app);
            ensureLayoutComplete(app);
            check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1, "table and code block lay out");
            check(app.docText.find(L"Deep quote") != std::wstring::npos &&
                  app.docText.find(L"Deep HTML quote") != std::wstring::npos &&
                  app.docText.find(L"html 60") != std::wstring::npos &&
                  app.docText.find(L"level 60") != std::wstring::npos &&
                  app.docText.find(L"Surrounding content survives.") != std::wstring::npos,
                  "deep and surrounding text is on the page");
        }
    }
    scenario = "fixture exports";
    exercise(app, root, L"deep-nesting");
    const std::string html = read(std::filesystem::current_path() / "nesting-test-output" / "deep-nesting.html");
    check(html.find("html 60") != std::string::npos && html.find("level 60") != std::string::npos &&
          html.find("Deep quote") != std::string::npos &&
          html.find("Deep HTML quote") != std::string::npos, "HTML export keeps the deep text");
}
}  // namespace

int main() {
    {
        auto state = std::make_unique<App>();
        auto& app = *state;
        if (!initD2D(app)) return 2;
        app.height = 900;
        applyTheme(app, 0);
        updateTextFormats(app);
        fixture(app);
        applyTheme(app, 0);
        updateTextFormats(app);
        deepCases(app);
    }
    CoUninitialize();
    std::cout << "Deep nesting: " << failures << " failures\n";
    return failures ? 1 : 0;
}
