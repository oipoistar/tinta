#include "markdown.h"
#include <md4c.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <stack>
#include <regex>
#include <unordered_map>
#include <algorithm>
#include <cctype>

namespace qmd {

// Parser context for MD4C callbacks
struct ParserContext {
    ElementPtr root;
    std::stack<Element*> elementStack;
    std::string currentText;
    const char* inputStart = nullptr;  // start of markdown source for offset tracking

    ParserContext() {
        root = std::make_shared<Element>(ElementType::Document);
        elementStack.push(root.get());
    }

    Element* current() {
        return elementStack.empty() ? nullptr : elementStack.top();
    }

    void pushElement(ElementPtr elem) {
        if (Element* parent = current()) {
            elem->parent = parent;
            parent->children.push_back(elem);
            elementStack.push(elem.get());
        }
    }

    void popElement() {
        if (elementStack.size() > 1) {
            elementStack.pop();
        }
    }

    void flushText() {
        if (!currentText.empty() && current()) {
            // For HTML blocks, accumulate raw HTML in the element's text field
            if (current()->type == ElementType::HtmlBlock) {
                current()->text += currentText;
            } else {
                auto textElem = std::make_shared<Element>(ElementType::Text);
                textElem->text = currentText;
                textElem->parent = current();
                current()->children.push_back(textElem);
            }
            currentText.clear();
        }
    }

    void addText(const char* text, MD_SIZE size) {
        currentText.append(text, size);
    }
};

// MD4C callbacks
static int enterBlockCallback(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);
    ctx->flushText();

    ElementPtr elem;
    switch (type) {
        case MD_BLOCK_DOC:
            // Root already exists
            return 0;

        case MD_BLOCK_P:
            elem = std::make_shared<Element>(ElementType::Paragraph);
            break;

        case MD_BLOCK_H: {
            auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            elem = std::make_shared<Element>(ElementType::Heading);
            elem->level = h->level;
            break;
        }

        case MD_BLOCK_CODE: {
            auto* code = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
            elem = std::make_shared<Element>(ElementType::CodeBlock);
            if (code->lang.text && code->lang.size > 0) {
                elem->language = std::string(code->lang.text, code->lang.size);
            }
            break;
        }

        case MD_BLOCK_QUOTE:
            elem = std::make_shared<Element>(ElementType::BlockQuote);
            break;

        case MD_BLOCK_UL:
            elem = std::make_shared<Element>(ElementType::List);
            elem->ordered = false;
            break;

        case MD_BLOCK_OL: {
            auto* ol = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
            elem = std::make_shared<Element>(ElementType::List);
            elem->ordered = true;
            elem->start = ol->start;
            break;
        }

        case MD_BLOCK_LI: {
            elem = std::make_shared<Element>(ElementType::ListItem);
            auto* li = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            if (li && li->is_task) {
                elem->isTask = true;
                elem->taskChecked = (li->task_mark == 'x' || li->task_mark == 'X');
                elem->taskMarkOffset = li->task_mark_offset;
            }
            break;
        }

        case MD_BLOCK_HR:
            elem = std::make_shared<Element>(ElementType::HorizontalRule);
            break;

        case MD_BLOCK_TABLE: {
            elem = std::make_shared<Element>(ElementType::Table);
            auto* table = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
            elem->col_count = (int)table->col_count;
            break;
        }

        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            // Skip these, handle rows directly
            return 0;

        case MD_BLOCK_TR:
            elem = std::make_shared<Element>(ElementType::TableRow);
            break;

        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            elem = std::make_shared<Element>(ElementType::TableCell);
            auto* td = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
            elem->align = (int)td->align;
            break;
        }

        case MD_BLOCK_HTML:
            elem = std::make_shared<Element>(ElementType::HtmlBlock);
            break;

        default:
            return 0;
    }

    if (elem) {
        ctx->pushElement(elem);
    }
    return 0;
}

static int leaveBlockCallback(MD_BLOCKTYPE type, void* /*detail*/, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);
    ctx->flushText();

    switch (type) {
        case MD_BLOCK_DOC:
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            return 0;
        case MD_BLOCK_HTML: {
            // Parse HTML content and convert to elements
            Element* htmlBlock = ctx->current();
            if (htmlBlock && htmlBlock->type == ElementType::HtmlBlock && !htmlBlock->text.empty()) {
                parseHtmlIntoElements(htmlBlock->text, htmlBlock);
                htmlBlock->text.clear(); // Clear raw HTML after parsing
            }
            ctx->popElement();
            break;
        }
        default:
            ctx->popElement();
            break;
    }
    return 0;
}

static int enterSpanCallback(MD_SPANTYPE type, void* detail, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);
    ctx->flushText();

    ElementPtr elem;
    switch (type) {
        case MD_SPAN_EM:
            elem = std::make_shared<Element>(ElementType::Emphasis);
            break;

        case MD_SPAN_STRONG:
            elem = std::make_shared<Element>(ElementType::Strong);
            break;

        case MD_SPAN_CODE:
            elem = std::make_shared<Element>(ElementType::Code);
            break;

        case MD_SPAN_LATEXMATH:
            elem = std::make_shared<Element>(ElementType::MathInline);
            break;

        case MD_SPAN_LATEXMATH_DISPLAY:
            elem = std::make_shared<Element>(ElementType::MathDisplay);
            break;

        case MD_SPAN_A: {
            auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
            elem = std::make_shared<Element>(ElementType::Link);
            if (a->href.text && a->href.size > 0) {
                elem->url = std::string(a->href.text, a->href.size);
            }
            if (a->title.text && a->title.size > 0) {
                elem->title = std::string(a->title.text, a->title.size);
            }
            break;
        }

        case MD_SPAN_IMG: {
            auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
            elem = std::make_shared<Element>(ElementType::Image);
            if (img->src.text && img->src.size > 0) {
                elem->url = std::string(img->src.text, img->src.size);
            }
            if (img->title.text && img->title.size > 0) {
                elem->title = std::string(img->title.text, img->title.size);
            }
            break;
        }

        default:
            return 0;
    }

    if (elem) {
        ctx->pushElement(elem);
    }
    return 0;
}

static int leaveSpanCallback(MD_SPANTYPE type, void* /*detail*/, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);
    ctx->flushText();

    switch (type) {
        case MD_SPAN_EM:
        case MD_SPAN_STRONG:
        case MD_SPAN_CODE:
        case MD_SPAN_A:
        case MD_SPAN_IMG:
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
            ctx->popElement();
            break;
        default:
            break;
    }
    return 0;
}

// Matches <br>, <br/>, <br /> etc., case-insensitively
static bool isBrTag(const MD_CHAR* text, MD_SIZE size) {
    if (size < 4 || text[0] != '<' || text[size - 1] != '>') return false;
    std::string norm;
    for (MD_SIZE i = 0; i < size; i++) {
        if (!isspace((unsigned char)text[i])) norm += (char)tolower((unsigned char)text[i]);
    }
    return norm == "<br>" || norm == "<br/>";
}

static int textCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);

    // Track source offset: set on the current element if not already set
    if (ctx->inputStart && ctx->current() && ctx->current()->sourceOffset == SIZE_MAX) {
        ctx->current()->sourceOffset = (size_t)(text - ctx->inputStart);
    }

    switch (type) {
        case MD_TEXT_HTML:  // Capture HTML content
            // Inline <br> becomes a hard line break (#45); raw HTML inside an
            // HtmlBlock keeps accumulating so parseHtmlIntoElements sees it all
            if (ctx->current() && ctx->current()->type != ElementType::HtmlBlock &&
                isBrTag(text, size)) {
                ctx->flushText();
                auto elem = std::make_shared<Element>(ElementType::HardBreak);
                elem->parent = ctx->current();
                ctx->current()->children.push_back(elem);
                break;
            }
            ctx->addText(text, size);
            break;
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
            ctx->addText(text, size);
            break;

        case MD_TEXT_LATEXMATH:
            // Raw TeX accumulates directly on the math span element
            if (ctx->current() &&
                (ctx->current()->type == ElementType::MathInline ||
                 ctx->current()->type == ElementType::MathDisplay)) {
                ctx->current()->text.append(text, size);
            } else {
                ctx->addText(text, size);
            }
            break;

        case MD_TEXT_SOFTBR:
            ctx->flushText();
            {
                auto elem = std::make_shared<Element>(ElementType::SoftBreak);
                elem->parent = ctx->current();
                if (ctx->current()) {
                    ctx->current()->children.push_back(elem);
                }
            }
            break;

        case MD_TEXT_BR:
            ctx->flushText();
            {
                auto elem = std::make_shared<Element>(ElementType::HardBreak);
                elem->parent = ctx->current();
                if (ctx->current()) {
                    ctx->current()->children.push_back(elem);
                }
            }
            break;

        case MD_TEXT_ENTITY:
            // Convert HTML entities
            if (size == 4 && strncmp(text, "&lt;", 4) == 0) {
                ctx->addText("<", 1);
            } else if (size == 4 && strncmp(text, "&gt;", 4) == 0) {
                ctx->addText(">", 1);
            } else if (size == 5 && strncmp(text, "&amp;", 5) == 0) {
                ctx->addText("&", 1);
            } else if (size == 6 && strncmp(text, "&quot;", 6) == 0) {
                ctx->addText("\"", 1);
            } else if (size == 6 && strncmp(text, "&nbsp;", 6) == 0) {
                ctx->addText(" ", 1);
            } else {
                // Pass through unknown entities
                ctx->addText(text, size);
            }
            break;

        default:
            ctx->addText(text, size);
            break;
    }
    return 0;
}

// MarkdownParser implementation
// --- Inline extension post-pass (==highlight==, ^sup^, ~sub~) ---
//
// md4c has no extension hooks for these Obsidian/Typora spans, so a pass
// over the finished tree splits Text nodes that contain them. Code spans,
// code blocks, and Mermaid sources are never touched.

namespace {

struct ExtensionMatch {
    size_t start;       // index of the opening delimiter
    size_t contentLen;  // bytes between the delimiters
    size_t delimLen;    // 2 for ==, 1 for ^ and ~
    ElementType type;
};

// Find the first extension span at or after `from`. Delimiters are ASCII,
// so byte scanning is UTF-8 safe.
static bool findExtensionSpan(const std::string& text, size_t from, ExtensionMatch& match) {
    for (size_t i = from; i < text.size(); i++) {
        char c = text[i];
        if (c == '=' && i + 1 < text.size() && text[i + 1] == '=') {
            size_t close = text.find("==", i + 2);
            if (close != std::string::npos && close > i + 2) {
                match = {i, close - (i + 2), 2, ElementType::Highlight};
                return true;
            }
        } else if (c == '~' && i + 1 < text.size() && text[i + 1] == '~') {
            size_t close = text.find("~~", i + 2);
            if (close != std::string::npos && close > i + 2) {
                match = {i, close - (i + 2), 2, ElementType::Strikethrough};
                return true;
            }
            // No closing ~~: fall through to try a single-tilde subscript
            size_t single = text.find('~', i + 2);
            if (single == std::string::npos) continue;
            i++;  // retry from the second tilde as a potential single opener
            continue;
        } else if (c == '^' || c == '~') {
            size_t close = text.find(c, i + 1);
            if (close == std::string::npos || close == i + 1) continue;
            // Typora rule: no whitespace inside sup/sub spans
            bool hasSpace = false;
            for (size_t j = i + 1; j < close; j++) {
                unsigned char uc = static_cast<unsigned char>(text[j]);
                if (uc == ' ' || uc == '\t' || uc == '\n') { hasSpace = true; break; }
            }
            if (hasSpace) continue;
            match = {i, close - (i + 1), 1,
                     c == '^' ? ElementType::Superscript : ElementType::Subscript};
            return true;
        }
    }
    return false;
}

static ElementPtr makeTextElement(std::string text, Element* parent) {
    auto node = std::make_shared<Element>(ElementType::Text);
    node->text = std::move(text);
    node->parent = parent;
    return node;
}

// GitHub alerts: a blockquote whose first line is exactly [!NOTE], [!TIP],
// [!IMPORTANT], [!WARNING] or [!CAUTION] renders as a styled callout.
// The marker must be alone on its line, matching github.com behavior.
static void detectGitHubAlerts(const ElementPtr& parent) {
    static const char* MARKERS[] = {"[!note]", "[!tip]", "[!important]", "[!warning]", "[!caution]"};

    for (auto& child : parent->children) {
        detectGitHubAlerts(child);
    }

    if (parent->type != ElementType::BlockQuote || parent->children.empty()) return;
    const ElementPtr& para = parent->children.front();
    if (para->type != ElementType::Paragraph || para->children.empty()) return;
    const ElementPtr& first = para->children.front();
    if (first->type != ElementType::Text) return;

    std::string marker = first->text;
    while (!marker.empty() && (marker.back() == ' ' || marker.back() == '\t')) marker.pop_back();
    for (auto& c : marker) c = (char)tolower((unsigned char)c);

    int kind = 0;
    for (int i = 0; i < 5; i++) {
        if (marker == MARKERS[i]) { kind = i + 1; break; }
    }
    if (kind == 0) return;

    // Marker must be the whole first line: alone in the paragraph, or
    // followed by a line break
    if (para->children.size() > 1 &&
        para->children[1]->type != ElementType::SoftBreak &&
        para->children[1]->type != ElementType::HardBreak) {
        return;
    }

    parent->alertKind = kind;
    size_t remove = std::min<size_t>(2, para->children.size());  // marker text + line break
    para->children.erase(para->children.begin(), para->children.begin() + remove);
    if (para->children.empty()) {
        parent->children.erase(parent->children.begin());
    }
}

static void splitInlineExtensions(const ElementPtr& parent) {
    if (parent->type == ElementType::Code ||
        parent->type == ElementType::CodeBlock ||
        parent->type == ElementType::MermaidDiagram) {
        return;
    }

    std::vector<ElementPtr> rebuilt;
    bool changed = false;
    for (auto& child : parent->children) {
        if (child->type != ElementType::Text) {
            splitInlineExtensions(child);
            rebuilt.push_back(child);
            continue;
        }

        const std::string& text = child->text;
        size_t cursor = 0;
        ExtensionMatch m;
        bool any = false;
        while (findExtensionSpan(text, cursor, m)) {
            any = true;
            if (m.start > cursor) {
                rebuilt.push_back(makeTextElement(text.substr(cursor, m.start - cursor), parent.get()));
            }
            auto span = std::make_shared<Element>(m.type);
            span->parent = parent.get();
            span->children.push_back(makeTextElement(
                text.substr(m.start + m.delimLen, m.contentLen), span.get()));
            rebuilt.push_back(std::move(span));
            cursor = m.start + m.delimLen + m.contentLen + m.delimLen;
        }
        if (!any) {
            rebuilt.push_back(child);
            continue;
        }
        changed = true;
        if (cursor < text.size()) {
            rebuilt.push_back(makeTextElement(text.substr(cursor), parent.get()));
        }
    }
    if (changed) parent->children = std::move(rebuilt);
}

// Obsidian [[wiki links]]: [[Target]] and [[Target|shown text]] become
// Link elements with a wiki: scheme URL; the click handler resolves the
// target against the current file's folder. Runs after the extension
// splitter, skipping code the same way.
static void splitWikiLinks(const ElementPtr& parent) {
    if (parent->type == ElementType::Code ||
        parent->type == ElementType::CodeBlock ||
        parent->type == ElementType::MermaidDiagram ||
        parent->type == ElementType::Link) {
        return;
    }

    std::vector<ElementPtr> rebuilt;
    bool changed = false;
    for (auto& child : parent->children) {
        if (child->type != ElementType::Text) {
            splitWikiLinks(child);
            rebuilt.push_back(child);
            continue;
        }

        const std::string& text = child->text;
        size_t cursor = 0;
        bool any = false;
        while (true) {
            size_t open = text.find("[[", cursor);
            if (open == std::string::npos) break;
            size_t close = text.find("]]", open + 2);
            if (close == std::string::npos) break;
            std::string inner = text.substr(open + 2, close - open - 2);
            if (inner.empty() || inner.find('\n') != std::string::npos) {
                cursor = open + 2;
                continue;
            }
            std::string target = inner, shown = inner;
            size_t pipe = inner.find('|');
            if (pipe != std::string::npos) {
                target = inner.substr(0, pipe);
                shown = inner.substr(pipe + 1);
            }
            if (target.empty() || shown.empty()) {
                cursor = open + 2;
                continue;
            }
            any = true;
            if (open > cursor) {
                rebuilt.push_back(makeTextElement(text.substr(cursor, open - cursor), parent.get()));
            }
            auto link = std::make_shared<Element>(ElementType::Link);
            link->parent = parent.get();
            link->url = "wiki:" + target;
            link->children.push_back(makeTextElement(shown, link.get()));
            rebuilt.push_back(std::move(link));
            cursor = close + 2;
        }
        if (!any) {
            rebuilt.push_back(child);
            continue;
        }
        changed = true;
        if (cursor < text.size()) {
            rebuilt.push_back(makeTextElement(text.substr(cursor), parent.get()));
        }
    }
    if (changed) parent->children = std::move(rebuilt);
}

// Plain-text references to local Markdown files ("docs/auth.md",
// "./setup.md") become fileref: Link elements; layout resolves the target
// against the current file's folder and badges it live or broken (#127).
// Agent-written notes drop these in constantly without wrapping them as
// links. Runs after the wiki pass, skipping code the same way.

static bool isFileRefChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ||
           c == '-' || c == '.' || c == '/' || c == '\\' || c == '~' ||
           c == ':';
}

// The extension ends at `end` (exclusive): the next character must not
// extend the file name (".mdx", "setup.md.bak" are different files; a
// sentence-ending ".md." is fine)
static bool fileRefBoundary(const std::string& text, size_t end) {
    if (end >= text.size()) return true;
    char next = text[end];
    if (std::isalnum(static_cast<unsigned char>(next)) || next == '_' ||
        next == '-') {
        return false;
    }
    if (next == '.' && end + 1 < text.size() &&
        std::isalnum(static_cast<unsigned char>(text[end + 1]))) {
        return false;
    }
    return true;
}

// Token shape check: a plausible local path, not a URL, not UNC (a dead
// server would stall the existence check), drive colon only as "C:"
static bool isFileRefToken(const std::string& token, size_t extLength) {
    if (token.size() <= extLength) return false;  // bare ".md"
    if (token.find("://") != std::string::npos) return false;
    if (token.rfind("//", 0) == 0 || token.rfind("\\\\", 0) == 0) {
        return false;
    }
    char beforeExt = token[token.size() - extLength - 1];
    if (beforeExt == '/' || beforeExt == '\\') return false;  // "docs/.md"
    for (size_t i = 0; i < token.size(); i++) {
        if (token[i] == ':' &&
            !(i == 1 && std::isalpha(static_cast<unsigned char>(token[0])))) {
            return false;
        }
    }
    return true;
}

// GitHub-style :shortcode: emoji. Names are the common GitHub set; the
// replacements are UTF-8 byte sequences (DirectWrite's emoji fallback
// renders them in color).
struct EmojiEntry {
    const char* name;
    const char* utf8;
};
static const EmojiEntry kEmoji[] = {
    {"+1", "\xF0\x9F\x91\x8D"}, {"-1", "\xF0\x9F\x91\x8E"},
    {"100", "\xF0\x9F\x92\xAF"}, {"airplane", "\xE2\x9C\x88\xEF\xB8\x8F"},
    {"alarm_clock", "\xE2\x8F\xB0"}, {"alien", "\xF0\x9F\x91\xBD"},
    {"anchor", "\xE2\x9A\x93"}, {"angry", "\xF0\x9F\x98\xA0"},
    {"arrow_down", "\xE2\xAC\x87\xEF\xB8\x8F"},
    {"arrow_left", "\xE2\xAC\x85\xEF\xB8\x8F"},
    {"arrow_right", "\xE2\x9E\xA1\xEF\xB8\x8F"},
    {"arrow_up", "\xE2\xAC\x86\xEF\xB8\x8F"},
    {"art", "\xF0\x9F\x8E\xA8"}, {"balloon", "\xF0\x9F\x8E\x88"},
    {"bar_chart", "\xF0\x9F\x93\x8A"}, {"battery", "\xF0\x9F\x94\x8B"},
    {"bear", "\xF0\x9F\x90\xBB"}, {"bee", "\xF0\x9F\x90\x9D"},
    {"beer", "\xF0\x9F\x8D\xBA"}, {"bell", "\xF0\x9F\x94\x94"},
    {"bike", "\xF0\x9F\x9A\xB2"}, {"birthday", "\xF0\x9F\x8E\x82"},
    {"blossom", "\xF0\x9F\x8C\xBC"}, {"blue_circle", "\xF0\x9F\x94\xB5"},
    {"bomb", "\xF0\x9F\x92\xA3"}, {"book", "\xF0\x9F\x93\x96"},
    {"bookmark", "\xF0\x9F\x94\x96"}, {"books", "\xF0\x9F\x93\x9A"},
    {"boom", "\xF0\x9F\x92\xA5"}, {"brain", "\xF0\x9F\xA7\xA0"},
    {"bug", "\xF0\x9F\x90\x9B"}, {"bulb", "\xF0\x9F\x92\xA1"},
    {"bus", "\xF0\x9F\x9A\x8C"}, {"butterfly", "\xF0\x9F\xA6\x8B"},
    {"cake", "\xF0\x9F\x8D\xB0"}, {"calendar", "\xF0\x9F\x93\x85"},
    {"camera", "\xF0\x9F\x93\xB7"}, {"candle", "\xF0\x9F\x95\xAF\xEF\xB8\x8F"},
    {"car", "\xF0\x9F\x9A\x97"}, {"cat", "\xF0\x9F\x90\xB1"},
    {"cd", "\xF0\x9F\x92\xBF"},
    {"chart_with_downwards_trend", "\xF0\x9F\x93\x89"},
    {"chart_with_upwards_trend", "\xF0\x9F\x93\x88"},
    {"checkered_flag", "\xF0\x9F\x8F\x81"},
    {"clap", "\xF0\x9F\x91\x8F"}, {"clapper", "\xF0\x9F\x8E\xAC"},
    {"clipboard", "\xF0\x9F\x93\x8B"},
    {"cloud", "\xE2\x98\x81\xEF\xB8\x8F"},
    {"clown_face", "\xF0\x9F\xA4\xA1"}, {"coffee", "\xE2\x98\x95"},
    {"comet", "\xE2\x98\x84\xEF\xB8\x8F"},
    {"computer", "\xF0\x9F\x92\xBB"},
    {"construction", "\xF0\x9F\x9A\xA7"},
    {"crescent_moon", "\xF0\x9F\x8C\x99"},
    {"crossed_fingers", "\xF0\x9F\xA4\x9E"}, {"crown", "\xF0\x9F\x91\x91"},
    {"cry", "\xF0\x9F\x98\xA2"}, {"dart", "\xF0\x9F\x8E\xAF"},
    {"date", "\xF0\x9F\x93\x85"}, {"deciduous_tree", "\xF0\x9F\x8C\xB3"},
    {"desktop_computer", "\xF0\x9F\x96\xA5\xEF\xB8\x8F"},
    {"dog", "\xF0\x9F\x90\xB6"}, {"dollar", "\xF0\x9F\x92\xB5"},
    {"electric_plug", "\xF0\x9F\x94\x8C"},
    {"email", "\xE2\x9C\x89\xEF\xB8\x8F"},
    {"envelope", "\xE2\x9C\x89\xEF\xB8\x8F"},
    {"evergreen_tree", "\xF0\x9F\x8C\xB2"},
    {"exclamation", "\xE2\x9D\x97"}, {"eyes", "\xF0\x9F\x91\x80"},
    {"factory", "\xF0\x9F\x8F\xAD"}, {"file_folder", "\xF0\x9F\x93\x81"},
    {"fire", "\xF0\x9F\x94\xA5"}, {"flashlight", "\xF0\x9F\x94\xA6"},
    {"floppy_disk", "\xF0\x9F\x92\xBE"},
    {"four_leaf_clover", "\xF0\x9F\x8D\x80"},
    {"fox_face", "\xF0\x9F\xA6\x8A"}, {"game_die", "\xF0\x9F\x8E\xB2"},
    {"gear", "\xE2\x9A\x99\xEF\xB8\x8F"}, {"gem", "\xF0\x9F\x92\x8E"},
    {"ghost", "\xF0\x9F\x91\xBB"}, {"gift", "\xF0\x9F\x8E\x81"},
    {"globe_with_meridians", "\xF0\x9F\x8C\x90"},
    {"green_circle", "\xF0\x9F\x9F\xA2"}, {"grin", "\xF0\x9F\x98\x81"},
    {"guitar", "\xF0\x9F\x8E\xB8"}, {"hammer", "\xF0\x9F\x94\xA8"},
    {"handshake", "\xF0\x9F\xA4\x9D"},
    {"heart", "\xE2\x9D\xA4\xEF\xB8\x8F"},
    {"heart_eyes", "\xF0\x9F\x98\x8D"},
    {"heavy_check_mark", "\xE2\x9C\x94\xEF\xB8\x8F"},
    {"heavy_minus_sign", "\xE2\x9E\x96"},
    {"heavy_plus_sign", "\xE2\x9E\x95"},
    {"hospital", "\xF0\x9F\x8F\xA5"}, {"hourglass", "\xE2\x8C\x9B"},
    {"hourglass_flowing_sand", "\xE2\x8F\xB3"},
    {"house", "\xF0\x9F\x8F\xA0"}, {"inbox_tray", "\xF0\x9F\x93\xA5"},
    {"information_source", "\xE2\x84\xB9\xEF\xB8\x8F"},
    {"iphone", "\xF0\x9F\x93\xB1"}, {"jack_o_lantern", "\xF0\x9F\x8E\x83"},
    {"joy", "\xF0\x9F\x98\x82"}, {"key", "\xF0\x9F\x94\x91"},
    {"keyboard", "\xE2\x8C\xA8\xEF\xB8\x8F"},
    {"label", "\xF0\x9F\x8F\xB7\xEF\xB8\x8F"},
    {"leaves", "\xF0\x9F\x8D\x83"}, {"link", "\xF0\x9F\x94\x97"},
    {"lock", "\xF0\x9F\x94\x92"}, {"loud_sound", "\xF0\x9F\x94\x8A"},
    {"loudspeaker", "\xF0\x9F\x93\xA2"}, {"mag", "\xF0\x9F\x94\x8D"},
    {"mega", "\xF0\x9F\x93\xA3"}, {"memo", "\xF0\x9F\x93\x9D"},
    {"microphone", "\xF0\x9F\x8E\xA4"}, {"microscope", "\xF0\x9F\x94\xAC"},
    {"moneybag", "\xF0\x9F\x92\xB0"}, {"movie_camera", "\xF0\x9F\x8E\xA5"},
    {"muscle", "\xF0\x9F\x92\xAA"}, {"musical_note", "\xF0\x9F\x8E\xB5"},
    {"mute", "\xF0\x9F\x94\x87"}, {"neutral_face", "\xF0\x9F\x98\x90"},
    {"new", "\xF0\x9F\x86\x95"}, {"newspaper", "\xF0\x9F\x93\xB0"},
    {"no_entry", "\xE2\x9B\x94"}, {"ok", "\xF0\x9F\x86\x97"},
    {"ok_hand", "\xF0\x9F\x91\x8C"},
    {"open_file_folder", "\xF0\x9F\x93\x82"},
    {"outbox_tray", "\xF0\x9F\x93\xA4"}, {"owl", "\xF0\x9F\xA6\x89"},
    {"package", "\xF0\x9F\x93\xA6"},
    {"page_facing_up", "\xF0\x9F\x93\x84"},
    {"panda_face", "\xF0\x9F\x90\xBC"}, {"paperclip", "\xF0\x9F\x93\x8E"},
    {"pencil2", "\xE2\x9C\x8F\xEF\xB8\x8F"},
    {"penguin", "\xF0\x9F\x90\xA7"}, {"pill", "\xF0\x9F\x92\x8A"},
    {"pizza", "\xF0\x9F\x8D\x95"}, {"point_left", "\xF0\x9F\x91\x88"},
    {"point_right", "\xF0\x9F\x91\x89"}, {"poop", "\xF0\x9F\x92\xA9"},
    {"popcorn", "\xF0\x9F\x8D\xBF"}, {"pray", "\xF0\x9F\x99\x8F"},
    {"pushpin", "\xF0\x9F\x93\x8C"}, {"puzzle_piece", "\xF0\x9F\xA7\xA9"},
    {"question", "\xE2\x9D\x93"}, {"rabbit", "\xF0\x9F\x90\xB0"},
    {"rainbow", "\xF0\x9F\x8C\x88"}, {"raised_hands", "\xF0\x9F\x99\x8C"},
    {"recycle", "\xE2\x99\xBB\xEF\xB8\x8F"},
    {"red_circle", "\xF0\x9F\x94\xB4"}, {"robot", "\xF0\x9F\xA4\x96"},
    {"rocket", "\xF0\x9F\x9A\x80"}, {"roll_eyes", "\xF0\x9F\x99\x84"},
    {"rose", "\xF0\x9F\x8C\xB9"},
    {"rotating_light", "\xF0\x9F\x9A\xA8"},
    {"round_pushpin", "\xF0\x9F\x93\x8D"}, {"runner", "\xF0\x9F\x8F\x83"},
    {"satellite", "\xF0\x9F\x9B\xB0\xEF\xB8\x8F"},
    {"school", "\xF0\x9F\x8F\xAB"}, {"scissors", "\xE2\x9C\x82\xEF\xB8\x8F"},
    {"scream", "\xF0\x9F\x98\xB1"}, {"seedling", "\xF0\x9F\x8C\xB1"},
    {"shield", "\xF0\x9F\x9B\xA1\xEF\xB8\x8F"}, {"ship", "\xF0\x9F\x9A\xA2"},
    {"shrug", "\xF0\x9F\xA4\xB7"}, {"skull", "\xF0\x9F\x92\x80"},
    {"sleeping", "\xF0\x9F\x98\xB4"}, {"smile", "\xF0\x9F\x98\x84"},
    {"smirk", "\xF0\x9F\x98\x8F"}, {"snail", "\xF0\x9F\x90\x8C"},
    {"snowflake", "\xE2\x9D\x84\xEF\xB8\x8F"}, {"sob", "\xF0\x9F\x98\xAD"},
    {"sos", "\xF0\x9F\x86\x98"}, {"sparkles", "\xE2\x9C\xA8"},
    {"speech_balloon", "\xF0\x9F\x92\xAC"}, {"star", "\xE2\xAD\x90"},
    {"star2", "\xF0\x9F\x8C\x9F"}, {"stopwatch", "\xE2\x8F\xB1\xEF\xB8\x8F"},
    {"sunglasses", "\xF0\x9F\x98\x8E"},
    {"sunny", "\xE2\x98\x80\xEF\xB8\x8F"},
    {"sweat_smile", "\xF0\x9F\x98\x85"}, {"tada", "\xF0\x9F\x8E\x89"},
    {"telephone", "\xE2\x98\x8E\xEF\xB8\x8F"},
    {"telescope", "\xF0\x9F\x94\xAD"}, {"thinking", "\xF0\x9F\xA4\x94"},
    {"thought_balloon", "\xF0\x9F\x92\xAD"},
    {"thumbsdown", "\xF0\x9F\x91\x8E"}, {"thumbsup", "\xF0\x9F\x91\x8D"},
    {"top", "\xF0\x9F\x94\x9D"}, {"trophy", "\xF0\x9F\x8F\x86"},
    {"tulip", "\xF0\x9F\x8C\xB7"}, {"turtle", "\xF0\x9F\x90\xA2"},
    {"umbrella", "\xE2\x98\x94"}, {"unicorn", "\xF0\x9F\xA6\x84"},
    {"video_camera", "\xF0\x9F\x93\xB9"},
    {"video_game", "\xF0\x9F\x8E\xAE"},
    {"warning", "\xE2\x9A\xA0\xEF\xB8\x8F"},
    {"wastebasket", "\xF0\x9F\x97\x91\xEF\xB8\x8F"},
    {"wave", "\xF0\x9F\x91\x8B"}, {"whale", "\xF0\x9F\x90\xB3"},
    {"white_check_mark", "\xE2\x9C\x85"},
    {"white_circle", "\xE2\x9A\xAA"}, {"wink", "\xF0\x9F\x98\x89"},
    {"world_map", "\xF0\x9F\x97\xBA\xEF\xB8\x8F"},
    {"wrench", "\xF0\x9F\x94\xA7"}, {"x", "\xE2\x9D\x8C"},
    {"yellow_circle", "\xF0\x9F\x9F\xA1"}, {"zap", "\xE2\x9A\xA1"},
    {"zzz", "\xF0\x9F\x92\xA4"},
};

static const char* emojiFor(const std::string& name) {
    for (const auto& entry : kEmoji) {
        if (name == entry.name) return entry.utf8;
    }
    return nullptr;
}

static bool isEmojiNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '+' || c == '-';
}

// Replaces :shortcode: with its emoji, in place, in every Text node
// outside code
static void splitEmojiShortcodes(const ElementPtr& parent) {
    if (parent->type == ElementType::Code ||
        parent->type == ElementType::CodeBlock ||
        parent->type == ElementType::MermaidDiagram) {
        return;
    }
    for (auto& child : parent->children) {
        if (child->type != ElementType::Text) {
            splitEmojiShortcodes(child);
            continue;
        }
        std::string& text = child->text;
        size_t pos = 0;
        while ((pos = text.find(':', pos)) != std::string::npos) {
            size_t end = pos + 1;
            while (end < text.size() && isEmojiNameChar(text[end]) &&
                   end - pos <= 40) {
                end++;
            }
            if (end >= text.size() || text[end] != ':' || end == pos + 1) {
                pos = end;
                continue;
            }
            const char* emoji =
                emojiFor(text.substr(pos + 1, end - pos - 1));
            if (emoji) {
                text.replace(pos, end - pos + 1, emoji);
                pos += strlen(emoji);
            } else {
                // The closing colon may open the next shortcode
                pos = end;
            }
        }
    }
}

static void splitFileRefs(const ElementPtr& parent) {
    if (parent->type == ElementType::Code ||
        parent->type == ElementType::CodeBlock ||
        parent->type == ElementType::MermaidDiagram ||
        parent->type == ElementType::Link) {
        return;
    }

    static const char* kExtensions[] = {".markdown", ".mmd", ".md"};

    std::vector<ElementPtr> rebuilt;
    bool changed = false;
    for (auto& child : parent->children) {
        if (child->type != ElementType::Text) {
            splitFileRefs(child);
            rebuilt.push_back(child);
            continue;
        }

        const std::string& text = child->text;
        size_t cursor = 0;
        bool any = false;
        size_t scan = 0;
        while (scan < text.size()) {
            size_t dot = text.find('.', scan);
            if (dot == std::string::npos) break;
            size_t extLength = 0;
            for (const char* extension : kExtensions) {
                size_t length = strlen(extension);
                if (dot + length <= text.size() &&
                    _strnicmp(text.c_str() + dot, extension, length) == 0 &&
                    fileRefBoundary(text, dot + length)) {
                    extLength = length;
                    break;
                }
            }
            if (extLength == 0) {
                scan = dot + 1;
                continue;
            }
            size_t end = dot + extLength;
            size_t start = dot;
            while (start > 0 && isFileRefChar(text[start - 1])) start--;
            // ':' and '-' cannot open a path
            while (start < dot &&
                   (text[start] == ':' || text[start] == '-')) {
                start++;
            }
            std::string token = text.substr(start, end - start);
            if (start < cursor || !isFileRefToken(token, extLength)) {
                scan = end;
                continue;
            }
            any = true;
            if (start > cursor) {
                rebuilt.push_back(makeTextElement(
                    text.substr(cursor, start - cursor), parent.get()));
            }
            auto link = std::make_shared<Element>(ElementType::Link);
            link->parent = parent.get();
            link->url = "fileref:" + token;
            link->children.push_back(makeTextElement(token, link.get()));
            rebuilt.push_back(std::move(link));
            cursor = end;
            scan = end;
        }
        if (!any) {
            rebuilt.push_back(child);
            continue;
        }
        changed = true;
        if (cursor < text.size()) {
            rebuilt.push_back(
                makeTextElement(text.substr(cursor), parent.get()));
        }
    }
    if (changed) parent->children = std::move(rebuilt);
}

} // namespace

MarkdownParser::MarkdownParser() = default;
MarkdownParser::~MarkdownParser() = default;

ParseResult MarkdownParser::parse(const std::string& markdown) {
    ParseResult result;

    auto startTime = std::chrono::high_resolution_clock::now();

    ParserContext ctx;
    ctx.inputStart = markdown.c_str();

    MD_PARSER parser = {
        0, // abi_version
        static_cast<unsigned>(
            MD_FLAG_TABLES |
            // Strikethrough is handled in the extension post-pass: md4c's
            // implementation also consumes single ~tilde~ pairs, which we
            // need for ~subscript~
            MD_FLAG_PERMISSIVEAUTOLINKS |
            MD_FLAG_PERMISSIVEURLAUTOLINKS |
            MD_FLAG_TASKLISTS |
            // Real-world notes (especially AI-chat exports) indent list
            // continuations far enough that CommonMark turns them into code
            // blocks — literal ** markers, no wrapping (#24). Fenced ```
            // blocks are unaffected and remain the way to write code.
            MD_FLAG_NOINDENTEDCODEBLOCKS |
            MD_FLAG_LATEXMATHSPANS
        ),
        enterBlockCallback,
        leaveBlockCallback,
        enterSpanCallback,
        leaveSpanCallback,
        textCallback,
        nullptr, // debug_log
        nullptr  // syntax
    };

    int ret = md_parse(markdown.c_str(), static_cast<MD_SIZE>(markdown.size()), &parser, &ctx);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.parseTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();

    if (ret != 0) {
        result.success = false;
        result.error = "Failed to parse markdown";
        return result;
    }

    ctx.flushText();
    splitInlineExtensions(ctx.root);
    splitWikiLinks(ctx.root);
    splitFileRefs(ctx.root);
    splitEmojiShortcodes(ctx.root);
    detectGitHubAlerts(ctx.root);
    result.root = ctx.root;
    result.success = true;
    return result;
}

ParseResult MarkdownParser::parseFile(const std::string& path) {
    ParseResult result;

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.success = false;
        result.error = "Failed to open file: " + path;
        return result;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    return parse(buffer.str());
}

// Utility functions
std::string elementTypeToString(ElementType type) {
    switch (type) {
        case ElementType::Document: return "Document";
        case ElementType::Paragraph: return "Paragraph";
        case ElementType::Heading: return "Heading";
        case ElementType::CodeBlock: return "CodeBlock";
        case ElementType::MermaidDiagram: return "MermaidDiagram";
        case ElementType::Highlight: return "Highlight";
        case ElementType::Strikethrough: return "Strikethrough";
        case ElementType::Superscript: return "Superscript";
        case ElementType::Subscript: return "Subscript";
        case ElementType::BlockQuote: return "BlockQuote";
        case ElementType::List: return "List";
        case ElementType::ListItem: return "ListItem";
        case ElementType::HorizontalRule: return "HorizontalRule";
        case ElementType::Table: return "Table";
        case ElementType::TableRow: return "TableRow";
        case ElementType::TableCell: return "TableCell";
        case ElementType::HtmlBlock: return "HtmlBlock";
        case ElementType::Text: return "Text";
        case ElementType::Code: return "Code";
        case ElementType::Emphasis: return "Emphasis";
        case ElementType::Strong: return "Strong";
        case ElementType::Link: return "Link";
        case ElementType::Image: return "Image";
        case ElementType::SoftBreak: return "SoftBreak";
        case ElementType::HardBreak: return "HardBreak";
        case ElementType::Ruby: return "Ruby";
        case ElementType::RubyText: return "RubyText";
        default: return "Unknown";
    }
}

void debugPrintElement(const ElementPtr& elem, int indent) {
    if (!elem) return;

    std::string pad(indent * 2, ' ');
    printf("%s%s", pad.c_str(), elementTypeToString(elem->type).c_str());

    if (!elem->text.empty()) {
        printf(": \"%s\"", elem->text.c_str());
    }
    if (elem->level > 0) {
        printf(" (level=%d)", elem->level);
    }
    if (!elem->url.empty()) {
        printf(" [url=%s]", elem->url.c_str());
    }
    printf("\n");

    for (const auto& child : elem->children) {
        debugPrintElement(child, indent + 1);
    }
}

// Simple HTML tag parser
struct HtmlTag {
    std::string name;
    bool isClosing = false;
    bool isSelfClosing = false;
    std::string href;
    std::string title;
    std::string id;
};

static std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static std::string extractAttribute(const std::string& tag, const std::string& attr) {
    // Cache compiled regexes - regex compilation is very expensive
    static std::unordered_map<std::string, std::regex> regexCache;
    auto it = regexCache.find(attr);
    if (it == regexCache.end()) {
        std::string pattern = attr + "\\s*=\\s*[\"']([^\"']*)[\"']";
        it = regexCache.emplace(attr, std::regex(pattern, std::regex::icase)).first;
    }
    std::smatch match;
    if (std::regex_search(tag, match, it->second) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

static HtmlTag parseTag(const std::string& tagStr) {
    HtmlTag tag;

    // Check if closing tag
    size_t start = 1;
    if (tagStr.length() > 1 && tagStr[1] == '/') {
        tag.isClosing = true;
        start = 2;
    }

    // Extract tag name
    size_t end = tagStr.find_first_of(" \t\n\r/>", start);
    if (end == std::string::npos) end = tagStr.length() - 1;
    tag.name = toLower(tagStr.substr(start, end - start));

    // Check for self-closing
    if (tagStr.find("/>") != std::string::npos) {
        tag.isSelfClosing = true;
    }

    // Extract common attributes
    tag.href = extractAttribute(tagStr, "href");
    tag.title = extractAttribute(tagStr, "title");
    tag.id = extractAttribute(tagStr, "id");

    return tag;
}

void parseHtmlIntoElements(const std::string& html, Element* parent) {
    if (!parent) return;

    std::stack<Element*> elementStack;
    elementStack.push(parent);

    size_t pos = 0;
    std::string textBuffer;

    auto flushText = [&]() {
        std::string trimmed = trim(textBuffer);
        if (!trimmed.empty() && !elementStack.empty()) {
            auto textElem = std::make_shared<Element>(ElementType::Text);
            textElem->text = trimmed;
            textElem->parent = elementStack.top();
            elementStack.top()->children.push_back(textElem);
        }
        textBuffer.clear();
    };

    while (pos < html.length()) {
        // Look for next tag
        size_t tagStart = html.find('<', pos);

        if (tagStart == std::string::npos) {
            // No more tags, add remaining text
            textBuffer += html.substr(pos);
            break;
        }

        // Add text before tag
        if (tagStart > pos) {
            textBuffer += html.substr(pos, tagStart - pos);
        }

        // Find end of tag
        size_t tagEnd = html.find('>', tagStart);
        if (tagEnd == std::string::npos) {
            // Malformed, add as text
            textBuffer += html.substr(tagStart);
            break;
        }

        std::string tagStr = html.substr(tagStart, tagEnd - tagStart + 1);
        HtmlTag tag = parseTag(tagStr);

        // Handle different tags
        if (tag.name == "ul" || tag.name == "ol") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::List);
                elem->ordered = (tag.name == "ol");
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "li") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::ListItem);
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "a") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::Link);
                elem->url = tag.href;
                elem->title = tag.title;
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "strong" || tag.name == "b") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::Strong);
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "em" || tag.name == "i") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::Emphasis);
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "code") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::Code);
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "p") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::Paragraph);
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "h1" || tag.name == "h2" || tag.name == "h3" ||
                 tag.name == "h4" || tag.name == "h5" || tag.name == "h6") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::Heading);
                elem->level = tag.name[1] - '0';
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "br") {
            flushText();
            auto elem = std::make_shared<Element>(ElementType::HardBreak);
            elem->parent = elementStack.top();
            elementStack.top()->children.push_back(elem);
        }
        else if (tag.name == "hr") {
            flushText();
            auto elem = std::make_shared<Element>(ElementType::HorizontalRule);
            elem->parent = elementStack.top();
            elementStack.top()->children.push_back(elem);
        }
        else if (tag.name == "pre") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::CodeBlock);
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "blockquote") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::BlockQuote);
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "ruby") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::Ruby);
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "rt") {
            if (!tag.isClosing) {
                flushText();
                auto elem = std::make_shared<Element>(ElementType::RubyText);
                elem->parent = elementStack.top();
                elementStack.top()->children.push_back(elem);
                elementStack.push(elem.get());
            } else if (elementStack.size() > 1) {
                flushText();
                elementStack.pop();
            }
        }
        else if (tag.name == "rp") {
            // Discard <rp> content (fallback parens for non-ruby renderers)
            if (!tag.isClosing) {
                flushText(); // Flush any preceding text before discarding
            } else {
                textBuffer.clear(); // Discard rp content (e.g. parentheses)
            }
        }
        // Ignore div, span, and other container tags but process their content
        else if (tag.name == "div" || tag.name == "span") {
            // Just continue processing content
        }
        // Skip comments
        else if (tagStr.substr(0, 4) == "<!--") {
            size_t commentEnd = html.find("-->", tagStart);
            if (commentEnd != std::string::npos) {
                tagEnd = commentEnd + 2;
            }
        }

        pos = tagEnd + 1;
    }

    flushText();
}

} // namespace qmd
