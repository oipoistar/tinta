#include "d2d_init.h"
#include "render.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
std::string scenario;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << scenario << ": " << message << '\n'; ++failures; }
}
bool closeEnough(float a, float b) { return std::abs(a - b) < 0.1f; }
using qmd::Element;
using qmd::ElementPtr;
using qmd::ElementType;

ElementPtr node(ElementType type, std::vector<ElementPtr> children = {}, const char* text = "") {
    auto result = std::make_shared<Element>(type);
    result->children = std::move(children);
    result->text = text;
    for (const auto& child : result->children) child->parent = result.get();
    return result;
}
ElementPtr text(const char* value) { return node(ElementType::Text, {}, value); }
ElementPtr paragraph(const char* value) { return node(ElementType::Paragraph, {text(value)}); }

const App::LayoutTextRun* runFor(const App& app, const wchar_t* value) {
    const size_t offset = app.docText.find(value);
    if (offset != std::wstring::npos) {
        for (const auto& run : app.layoutTextRuns)
            if (run.docLength && run.docStart <= offset && offset < run.docStart + run.docLength) return &run;
    }
    return nullptr;
}
void checkOrder(const App& app, std::initializer_list<const wchar_t*> values) {
    size_t previous = 0;
    float previousY = -1;
    for (const auto* value : values) {
        const size_t offset = app.docText.find(value);
        check(offset != std::wstring::npos && offset >= previous, "searchable text follows source order");
        check(offset == std::wstring::npos || app.docText.find(value, offset + 1) == std::wstring::npos,
              "content occurs exactly once");
        if (offset != std::wstring::npos) previous = offset + std::wcslen(value);
        const auto* run = runFor(app, value);
        if (!run || run->bounds.top < previousY) {
            std::wcerr << L"  ordering token: " << value << L", y=" << (run ? run->bounds.top : -1)
                       << L", previous y=" << previousY << L'\n';
        }
        check(run && run->bounds.top >= previousY, "visible text follows source order");
        if (run) previousY = run->bounds.top;
    }
}
void checkTable(const App& app) {
    check(app.tableRects.size() == 1, "table reaches native table layout");
    if (app.tableRects.size() != 1) return;
    // Detailed grid and wrapping checks live in table_layout_tests.cpp.
    const auto& table = app.tableRects[0];
    check(table.tsv == L"Header left\tHeader right\nCell alpha\tCell beta\nCell gamma\tCell delta",
          "table copy retains every row and column");
    const auto* left = runFor(app, L"Cell alpha");
    const auto* right = runFor(app, L"Cell beta");
    const auto* lower = runFor(app, L"Cell gamma");
    check(left && right && lower && left->pos.x < right->pos.x && closeEnough(left->pos.y, right->pos.y) && lower->pos.y > left->pos.y,
          "cells occupy distinct native rows and columns");
}
void checkFixtureTree(const ElementPtr& root) {
    check(root && root->children.size() > 2, "fixture has surrounding blocks");
    if (!root || root->children.size() <= 2) return;
    const auto& list = root->children[2];
    check(list->type == ElementType::List && list->ordered && list->start == 7 && list->children.size() == 2,
          "MD4C recognizes ordered start and two items");
    if (list->type != ElementType::List || list->children.empty()) return;
    const auto& children = list->children[0]->children;
    const std::vector<ElementType> expected = {ElementType::Paragraph, ElementType::Table, ElementType::Paragraph,
        ElementType::Heading, ElementType::Paragraph, ElementType::HorizontalRule, ElementType::Paragraph,
        ElementType::BlockQuote, ElementType::CodeBlock, ElementType::List, ElementType::Paragraph};
    check(children.size() == expected.size(), "MD4C keeps all mixed list blocks");
    for (size_t i = 0; i < children.size() && i < expected.size(); ++i)
        check(children[i]->type == expected[i], "MD4C preserves block types in source order");
}
void checkFixture(const App& app) {
    checkOrder(app, {L"Before table paragraph", L"Header left", L"Header right", L"Cell alpha", L"Cell beta",
        L"Cell gamma", L"Cell delta", L"After table paragraph", L"Item heading", L"After heading paragraph",
        L"After rule paragraph", L"Quoted paragraph", L"fenced sample", L"Nested first", L"Nested deeper",
        L"Nested last", L"Final item paragraph", L"Next numbered item", L"Ordinary lists", L"Ordinary item",
        L"Parent item", L"Child item", L"Last bullet", L"Pending task", L"Completed task",
        L"Unordered HTML alpha", L"Unordered HTML beta", L"After the omitted HTML end tags.",
        L"Ordered HTML alpha", L"Ordered HTML beta", L"After the ordered HTML list.",
        L"Outer alpha", L"Inner alpha", L"Inner beta", L"Outer beta", L"After the nested HTML lists.",
        L"HTML heading first", L"HTML heading body.", L"HTML following item",
        L"Ordered heading first", L"Paragraph-first ordered control.", L"Ordered level one heading",
        L"Ordered level six heading", L"Following the ordered headings.", L"Unordered heading first",
        L"Paragraph-first unordered control.", L"Unordered level one heading", L"Unordered level six heading",
        L"Following the unordered headings.", L"Surrounding quote",
        L"surrounding code", L"Final surrounding paragraph", L"HTML wrappers and empty anchors",
        L"Wrapped HTML heading", L"Unanchored HTML heading", L"Anchored HTML heading",
        L"After the HTML wrapper and anchor examples.", L"Heading-first tasks",
        L"Unchecked task heading", L"Finished task heading", L"Small task heading",
        L"Following task paragraph control.", L"After the heading-first tasks."});
    checkTable(app);
    const float scale = app.contentScale * app.zoomFactor;
    const auto* before = runFor(app, L"Before table paragraph");
    const auto* after = runFor(app, L"After table paragraph");
    if (before && after && app.tableRects.size() == 1) {
        check(before->bounds.bottom <= app.tableRects[0].bounds.top && after->bounds.top >= app.tableRects[0].bounds.bottom,
              "table remains between the surrounding paragraphs");
        check(closeEnough(before->pos.x, app.layoutIndent + 24 * scale) && closeEnough(after->pos.x, before->pos.x) &&
              closeEnough(app.tableRects[0].bounds.left, before->pos.x), "paragraphs and table share list indentation");
        check(std::any_of(app.layoutTextRuns.begin(), app.layoutTextRuns.end(), [&](const auto& run) {
            return !run.docLength && closeEnough(run.pos.x, app.layoutIndent) && closeEnough(run.pos.y, before->pos.y);
        }), "first paragraph starts beside its list marker");
    }
    const auto* nested = runFor(app, L"Nested first");
    const auto* deeper = runFor(app, L"Nested deeper");
    check(nested && deeper && closeEnough(nested->pos.x, app.layoutIndent + 48 * scale) &&
          closeEnough(deeper->pos.x, app.layoutIndent + 72 * scale), "nested lists retain each indentation level");
    check(app.taskRects.size() == 6 && !app.taskRects[0].checked && app.taskRects[1].checked,
          "task checkboxes retain checked state");
    const wchar_t* tasks[] = {L"Pending task", L"Completed task"};
    for (size_t i = 0; i < app.taskRects.size() && i < std::size(tasks); ++i) {
        const auto* task = runFor(app, tasks[i]);
        const auto& hit = app.taskRects[i].rect;
        check(task && closeEnough(task->pos.x, app.layoutIndent + 24 * scale) &&
              closeEnough(hit.top, task->pos.y) && hit.left <= app.layoutIndent &&
              hit.right > app.layoutIndent && hit.right < task->pos.x && hit.bottom > hit.top,
              "task hit regions align beside their item text");
    }
    const auto* quote = runFor(app, L"Quoted paragraph");
    check(quote && quote->pos.x > app.layoutIndent + 24 * scale &&
          std::any_of(app.layoutRects.begin(), app.layoutRects.end(), [&](const auto& rect) {
              return closeEnough(rect.rect.left, app.layoutIndent + 24 * scale) &&
                     closeEnough(rect.rect.right - rect.rect.left, 4) &&
                     rect.rect.top <= quote->bounds.top && rect.rect.bottom >= quote->bounds.bottom;
          }), "list-contained quote retains its inset and bar");
    check(app.codeBlocks.size() == 2 && app.headings.size() == 19, "mixed headings and fences keep block layout");
    const auto* ruleBefore = runFor(app, L"After heading paragraph");
    const auto* ruleAfter = runFor(app, L"After rule paragraph");
    check(ruleBefore && ruleAfter && std::any_of(app.layoutLines.begin(), app.layoutLines.end(), [&](const auto& line) {
        return closeEnough(line.p1.x, app.layoutIndent + 24 * scale) && closeEnough(line.p1.y, line.p2.y) &&
               line.p1.y > ruleBefore->bounds.bottom && line.p1.y < ruleAfter->bounds.top;
    }), "horizontal rule remains between its surrounding paragraphs");
    const size_t ordinary = app.docText.find(L"Ordinary item");
    const size_t parent = app.docText.find(L"Parent item");
    int ordinaryLines = 0;
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && run.docStart >= ordinary && run.docStart < parent) {
            ++ordinaryLines;
            check(closeEnough(run.pos.x, app.layoutIndent + 24 * scale), "wrapped lines keep hanging indentation");
        }
    }
    if (app.width == 650) check(ordinaryLines > 1, "narrow fixture exercises list wrapping");
}

void syntheticCases(App& app) {
    // The loose fixture uses paragraphs; direct inline children exercise
    // buffering across a block and the final flush without relying on MD4C.
    auto inlineItem = node(ElementType::ListItem, {
        text("Inline before"), node(ElementType::Strong, {text(" bold")}),
        node(ElementType::Table, {node(ElementType::TableRow, {node(ElementType::TableCell, {text("Middle cell")})})}),
        text("Inline after")});
    auto blockFirstItem = node(ElementType::ListItem, {
        node(ElementType::Table, {node(ElementType::TableRow, {node(ElementType::TableCell, {text("First cell")})})}),
        paragraph("Block first after")});
    app.root = node(ElementType::Document, {node(ElementType::List, {inlineItem, blockFirstItem})});
    layoutDocument(app);
    checkOrder(app, {L"Inline before", L"bold", L"Middle cell", L"Inline after", L"First cell", L"Block first after"});
    const auto* before = runFor(app, L"Inline before");
    const auto* bold = runFor(app, L"bold");
    check(before && bold && closeEnough(before->pos.y, bold->pos.y) && bold->pos.x >= before->bounds.right,
          "consecutive styled inline children share a line");
    check(app.tableRects.size() == 2, "inline/block alternation and table-first item retain native tables");
    if (app.tableRects.size() != 2) return;
    const auto* after = runFor(app, L"Inline after");
    check(before && after && before->bounds.bottom <= app.tableRects[0].bounds.top &&
          after->bounds.top >= app.tableRects[0].bounds.bottom, "direct inline content brackets the table visually");
    const auto* blockFirstAfter = runFor(app, L"Block first after");
    check(blockFirstAfter && blockFirstAfter->bounds.top >= app.tableRects[1].bounds.bottom,
          "table-first item keeps its paragraph below the table");
}

float baseline(const App::LayoutTextRun& run) {
    DWRITE_LINE_METRICS metrics{};
    UINT32 count = 0;
    check(SUCCEEDED(run.layout->GetLineMetrics(&metrics, 1, &count)) && count == 1,
          "first-line baseline is measurable");
    return run.pos.y + metrics.baseline;
}

// The first item's marker: a number set as text, or a bullet drawn halfway
// up the letters of the baseline it stands on (#315)
struct Marker {
    bool found = false;
    float baseline = 0, top = 0;
};
Marker firstMarker(const App& app) {
    for (const auto& run : app.layoutTextRuns)
        if (!run.docLength) return {true, baseline(run), run.pos.y};
    if (app.layoutShapes.empty() || !app.textFormat) return {};
    const D2D1_RECT_F& bullet = app.layoutShapes.front().rect;
    return {true, (bullet.top + bullet.bottom) * 0.5f + app.textFormat->GetFontSize() * 0.3f, bullet.top};
}

void firstBlockCases(App& app) {
    auto html = app.parser.parse("<ul><li>a<li>b</ul>\n<p>after</p>\n");
    check(html.success, "omitted-end-tag HTML parses");
    app.root = html.root;
    layoutDocument(app);
    const size_t a = app.docText.find(L"a"), b = app.docText.find(L"b"), after = app.docText.find(L"after");
    check(a < b && b < after && after != std::wstring::npos, "omitted-end-tag HTML keeps both items searchable in order");
    check(runFor(app, L"a") && runFor(app, L"b") && runFor(app, L"after"),
          "omitted-end-tag HTML keeps both items visible");

    const float scale = app.contentScale * app.zoomFactor;
    const std::string title = "Heading first with enough words to wrap across lines at narrow widths while retaining native heading formatting";
    for (const std::string prefix : {"1. ", "- "}) {
        for (int level : {1, 3, 6}) {
            // A leading script must not pull the marker above the heading's main baseline.
            const std::string heading = std::string(level, '#') + (level == 6 ? " ^up^ " : " ") + title + "\n\n";
            auto doc = app.parser.parse(prefix + heading + "Following paragraph.\n");
            check(doc.success && doc.root && doc.root->children.size() == 2 && doc.root->children[0]->type == ElementType::List &&
                  doc.root->children[1]->type == ElementType::Paragraph,
                  "heading-first Markdown parses as a list");
            if (!doc.success || !doc.root || doc.root->children.empty() ||
                doc.root->children[0]->type != ElementType::List || doc.root->children[0]->children.empty()) continue;
            const auto& item = doc.root->children[0]->children[0];
            check(item->children.size() == 1 && item->children[0]->type == ElementType::Heading &&
                  item->children[0]->level == level,
                  "heading-first item retains its native heading node");
            app.root = doc.root;
            layoutDocument(app);
            const auto* text = runFor(app, L"Heading first");
            const auto* following = runFor(app, L"Following paragraph.");
            const Marker marker = firstMarker(app);
            check(app.headings.size() == 1 && text && following && marker.found,
                  "heading-first item renders a native heading, marker and following paragraph");
            if (!text || !following || !marker.found) continue;
            const float delta = baseline(*text) - marker.baseline;
            check(closeEnough(delta, 0), "heading-first marker shares the heading's first-line baseline");
            float size = 0;
            text->layout->GetFontSize(0, &size);
            check(closeEnough(size, app.headingFormats[level - 1]->GetFontSize()), "heading-first text retains its heading font size");
            const float headingY = text->bounds.top, followingY = following->bounds.top;
            const int width = app.width;
            app.width -= static_cast<int>(24 * scale);
            app.root = app.parser.parse(heading + "Following paragraph.\n").root;
            layoutDocument(app);
            const auto* standalone = runFor(app, L"Heading first");
            const auto* standaloneAfter = runFor(app, L"Following paragraph.");
            check(standalone && standaloneAfter && closeEnough(standalone->bounds.top, headingY) &&
                  closeEnough(standaloneAfter->bounds.top + 8 * scale, followingY),
                  "heading-first list preserves native heading margins, wrapping and trailing list gap");
            app.width = width;
        }
    }
}

void htmlHeadingCases(App& app) {
    const float scale = app.contentScale * app.zoomFactor;
    for (const std::string prefix : {"- ", "1. ", "<ul>", "<ol>"}) {
        const bool raw = prefix[0] == '<';
        for (int level : {1, 3, 6}) {
            const std::string tag = "h" + std::to_string(level);
            const std::string heading = "<" + tag + ">" + (level == 6 ? "<sup>up</sup> " : "") +
                "HTML title with enough words to wrap across lines at narrow widths while retaining native heading formatting</" + tag + ">";
            std::vector<float> plain;
            for (bool anchored : {false, true}) {
                if (anchored && !raw) continue;
                const std::string anchors = anchored ? "<a id=\"first\"></a> <a id=\"second\"></a>" : "";
                const std::string tail = anchored ? "<a id=\"tail\"></a>" : "";
                const std::string source = raw ? prefix + "<li>" + anchors + heading + tail + "</li></" +
                    prefix.substr(1, 2) + ">\n<p>Following HTML paragraph.</p>\n" :
                    prefix + heading + "\n\nFollowing HTML paragraph.\n";
                auto parsed = app.parser.parse(source);
                check(parsed.success, "HTML heading list parses with default flags");
                app.root = parsed.root;
                layoutDocument(app);
                const auto* title = runFor(app, L"HTML title");
                const auto* following = runFor(app, L"Following HTML paragraph.");
                check(title && following && app.headings.size() == 1 && !app.layoutTextRuns.empty(),
                      "HTML list retains a native heading, marker and following paragraph");
                if (!title || !following || app.headings.empty() || app.layoutTextRuns.empty()) continue;
                const Marker marker = firstMarker(app);
                check(marker.found && closeEnough(marker.baseline, baseline(*title)),
                      "HTML first heading shares its main text baseline with the marker");
                float size = 0;
                title->layout->GetFontSize(0, &size);
                check(closeEnough(size, app.headingFormats[level - 1]->GetFontSize()),
                      "HTML first heading keeps native font size");
                const std::vector<float> geometry = {title->pos.x, title->pos.y, following->pos.y,
                    app.contentHeight, marker.top};
                if (!anchored) plain = geometry;
                else {
                    for (size_t i = 0; i < plain.size(); ++i)
                        check(closeEnough(plain[i], geometry[i]), "empty list anchors preserve heading, marker and following layout");
                    for (const char* id : {"first", "second", "tail"}) {
                        check(std::count_if(app.htmlAnchors.begin(), app.htmlAnchors.end(), [&](const auto& a) {
                            return a.first == id;
                        }) == 1, "each list anchor is registered exactly once");
                    }
                    for (const auto& anchor : app.htmlAnchors) {
                        const float expected = anchor.first == "tail" ? following->pos.y - 8 * scale :
                            app.headings[0].y - (level == 1 ? 16 : 20) * scale;
                        check(closeEnough(anchor.second, expected), "empty list anchors pin to their flow position");
                    }
                }
            }
        }
    }
}

void anchorBufferCases(App& app) {
    app.root = app.parser.parse("- Control\n").root;
    layoutDocument(app);
    const float markerY = app.layoutTextRuns.front().pos.y, bulletTop = firstMarker(app).top;
    for (int kind = 0; kind < 7; ++kind) {
        auto heading = node(ElementType::Heading, {text("Later heading")});
        heading->level = 3;
        auto anchor = node(ElementType::Anchor);
        anchor->title = "buffer";
        std::vector<ElementPtr> children;
        switch (kind) {
            case 0: children = {anchor, text("Visible before"), heading}; break;
            case 1: children = {node(ElementType::Anchor, {text("Visible anchor")}), heading}; break;
            case 2: children = {anchor, node(ElementType::HardBreak), heading}; break;
            case 3: children = {node(ElementType::HorizontalRule), heading}; break;
            case 4: children = {node(ElementType::BlockQuote, {heading})}; break;
            case 5: children = {node(ElementType::List, {node(ElementType::ListItem, {heading})})}; break;
            case 6: children = {anchor}; break;
        }
        app.root = node(ElementType::Document, {node(ElementType::List, {node(ElementType::ListItem, children)})});
        layoutDocument(app);
        check(firstMarker(app).found && closeEnough(firstMarker(app).top, bulletTop),
              "later, nested and absent headings do not move the outer marker");
        if (kind < 2) {
            const auto* visible = runFor(app, kind == 0 ? L"Visible before" : L"Visible anchor");
            check(visible && closeEnough(visible->pos.y, markerY), "visible anchor buffers retain their text line");
        }
        if (kind == 2) {
            check(!app.headings.empty() && app.headings[0].y > markerY + 20 * app.contentScale * app.zoomFactor,
                  "an explicit hard break still allocates a line");
        }
        if (kind == 6) {
            const float height = app.contentHeight;
            check(app.htmlAnchors.size() == 1, "anchor-only item registers its target");
            app.root = node(ElementType::Document, {node(ElementType::List, {node(ElementType::ListItem)})});
            layoutDocument(app);
            check(closeEnough(height, app.contentHeight), "anchor-only item keeps the ordinary empty-item minimum spacing");
        }
    }
    // Nested wrappers and anchor-only paragraphs are transparent to a first heading.
    auto heading = node(ElementType::Heading, {text("Wrapped heading")});
    heading->level = 3;
    auto anchor = node(ElementType::Anchor);
    anchor->title = "wrapped";
    app.root = node(ElementType::Document, {node(ElementType::List, {node(ElementType::ListItem, {
        node(ElementType::Paragraph, {anchor, text(" \t"), node(ElementType::SoftBreak)}),
        node(ElementType::HtmlBlock, {node(ElementType::HtmlBlock, {heading})})})})});
    layoutDocument(app);
    const auto* title = runFor(app, L"Wrapped heading");
    check(title && closeEnough(firstMarker(app).baseline, baseline(*title)),
          "first heading resolves through nested HTML wrappers and an anchor-only paragraph");
    check(app.htmlAnchors.size() == 1 && closeEnough(app.htmlAnchors[0].second, markerY),
          "anchor-only paragraph pins once without advancing the heading");
}

void taskHeadingCases(App& app) {
    const float scale = app.contentScale * app.zoomFactor;
    for (bool html : {false, true}) for (bool checked : {false, true}) for (int level : {1, 3, 6}) {
        const std::string title = "Task heading with enough words to wrap at narrow widths while keeping its checkbox beside the first line";
        const std::string tag = "h" + std::to_string(level);
        const std::string heading = html ? "<" + tag + ">" + title + "</" + tag + ">" :
            std::string(level, '#') + (level == 6 ? " ^up^ " : " ") + title;
        // The task marker needs its own line for MD4C to recognize a heading block.
        // HTML also needs a blank line before the nested Markdown list.
        auto parsed = app.parser.parse(std::string(checked ? "- [x]\n  " : "- [ ]\n  ") + heading +
            (html ? "\n\n" : "\n") + "  - [x] Nested task control\n- [ ] Following task control\n");
        check(parsed.success && parsed.root && !parsed.root->children.empty(), "heading-first task parses");
        if (!parsed.success || !parsed.root || parsed.root->children.empty()) continue;
        const auto& list = parsed.root->children[0];
        check(list->type == ElementType::List && list->children.size() == 2, "task heading keeps its nested and following items");
        if (list->type != ElementType::List || list->children.empty()) continue;
        const auto& item = list->children[0];
        check(item->isTask && item->taskChecked == checked && !item->children.empty() &&
              item->children[0]->type == (html ? ElementType::HtmlBlock : ElementType::Heading),
              "task state and first heading block survive parsing");
        app.root = parsed.root;
        layoutDocument(app);
        const auto* text = runFor(app, L"Task heading");
        const auto* nested = runFor(app, L"Nested task control");
        const auto* following = runFor(app, L"Following task control");
        check(text && nested && following && app.taskRects.size() == 3 && app.headings.size() == 1,
              "heading-first task retains its text, checkboxes and native heading");
        if (!text || !nested || !following || app.taskRects.size() != 3) continue;
        const auto& task = app.taskRects[0];
        const auto& control = app.taskRects[2];
        check(task.checked == checked && task.markOffset == item->taskMarkOffset,
              "heading task retains its checked state and source mark");
        check(closeEnough(baseline(*text) - task.rect.top, baseline(*following) - control.rect.top),
              "heading task checkbox shares the ordinary task's baseline alignment");
        check(closeEnough(app.taskRects[1].rect.top, nested->pos.y) &&
              closeEnough(control.rect.top, following->pos.y), "moving a heading checkbox leaves nested and following tasks in place");
        check(closeEnough(task.rect.left, control.rect.left) && closeEnough(task.rect.right, control.rect.right) &&
              closeEnough(task.rect.bottom - task.rect.top, control.rect.bottom - control.rect.top),
              "heading task keeps ordinary checkbox indentation and hit size");
        const auto box = D2D1::RectF(task.rect.left + 4 * scale, task.rect.top + 4 * scale,
                                   task.rect.right - 4 * scale, task.rect.bottom - 4 * scale);
        if (checked) check(std::any_of(app.layoutRects.begin(), app.layoutRects.end(), [&](const auto& r) {
            return closeEnough(r.rect.left, box.left) && closeEnough(r.rect.right, box.right) &&
                   closeEnough(r.rect.top, box.top) && closeEnough(r.rect.bottom, box.bottom);
        }), "checked fill moves with its hit region");
        int lines = 0;
        for (const auto& line : app.layoutLines) {
            if (line.p1.x >= box.left - 0.1f && line.p2.x >= box.left - 0.1f &&
                line.p1.x <= box.right + 0.1f && line.p2.x <= box.right + 0.1f &&
                line.p1.y >= box.top - 0.1f && line.p2.y >= box.top - 0.1f &&
                line.p1.y <= box.bottom + 0.1f && line.p2.y <= box.bottom + 0.1f) ++lines;
        }
        check(lines == (checked ? 2 : 4), "check mark or outline moves with its hit region");
        float size = 0;
        text->layout->GetFontSize(0, &size);
        check(closeEnough(size, app.headingFormats[level - 1]->GetFontSize()), "task heading retains native font size");
    }
}

void ordinaryCases(App& app) {
    const float scale = app.contentScale * app.zoomFactor;
    for (bool loose : {false, true}) {
        auto parsed = app.parser.parse(loose ? "7. First\n\n8. Second\n" : "7. First\n8. Second\n");
        check(parsed.success, "ordinary list parses");
        app.root = parsed.root;
        layoutDocument(app);
        const auto* first = runFor(app, L"First");
        const auto* second = runFor(app, L"Second");
        check(first && second, "both ordinary items render");
        if (!first || !second) continue;
        check(closeEnough(second->pos.y - first->pos.y, loose ? std::max(28 * scale, first->bounds.bottom - first->bounds.top + 14 * scale) : 28 * scale),
              "tight and loose lists retain item spacing");
        int markers = 0;
        for (const auto& run : app.layoutTextRuns) {
            if (run.docLength) continue;
            check(closeEnough(run.pos.x, app.layoutIndent) &&
                  closeEnough(run.pos.y, markers == 0 ? first->pos.y : second->pos.y) &&
                  closeEnough(baseline(run), baseline(markers == 0 ? *first : *second)),
                  "ordinary markers align beside their item text");
            ++markers;
        }
        check(markers == 2, "two numbered markers render");
    }
}

}

int main() {
    {
        auto state = std::make_unique<App>();
        auto& app = *state;
        if (!initD2D(app)) return 2;
        app.height = 900;
        app.readingWidthPct = 100;
        std::ifstream file(TINTA_LIST_FIXTURE, std::ios::binary);
        check(file.good(), "mixed fixture opens");
        const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto parsed = app.parser.parse(source);
        check(parsed.success, "mixed fixture parses");
        checkFixtureTree(parsed.root);
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f}) {
                app.contentScale = scale;
                updateTextFormats(app);
                for (int width : {1050, 650}) {
                    app.width = width;
                    scenario = "theme" + std::to_string(theme) + "-scale" + std::to_string(static_cast<int>(scale * 100)) + "-width" + std::to_string(width);
                    app.root = parsed.root;
                    layoutDocument(app);
                    checkFixture(app);
                    syntheticCases(app);
                    ordinaryCases(app);
                    firstBlockCases(app);
                    htmlHeadingCases(app);
                    anchorBufferCases(app);
                    taskHeadingCases(app);
                }
            }
        }
    }
    CoUninitialize();
    std::cout << "List block order: " << failures << " failures\n";
    return failures ? 1 : 0;
}
