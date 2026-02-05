#include "markdown.h"
#include <md4c.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <stack>
#include <regex>
#include <algorithm>
#include <cctype>

namespace qmd {

// Parser context for MD4C callbacks
struct ParserContext {
    ElementPtr root;
    std::stack<Element*> elementStack;
    std::string currentText;

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

        case MD_BLOCK_LI:
            elem = std::make_shared<Element>(ElementType::ListItem);
            break;

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
            ctx->popElement();
            break;
        default:
            break;
    }
    return 0;
}

static int textCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* ctx = static_cast<ParserContext*>(userdata);

    switch (type) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
        case MD_TEXT_HTML:  // Capture HTML content
            ctx->addText(text, size);
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
MarkdownParser::MarkdownParser() = default;
MarkdownParser::~MarkdownParser() = default;

ParseResult MarkdownParser::parse(const std::string& markdown) {
    ParseResult result;

    auto startTime = std::chrono::high_resolution_clock::now();

    ParserContext ctx;

    MD_PARSER parser = {
        0, // abi_version
        static_cast<unsigned>(
            MD_FLAG_TABLES |
            MD_FLAG_STRIKETHROUGH |
            MD_FLAG_PERMISSIVEAUTOLINKS |
            MD_FLAG_PERMISSIVEURLAUTOLINKS |
            MD_FLAG_TASKLISTS
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
    std::string pattern = attr + "\\s*=\\s*[\"']([^\"']*)[\"']";
    std::regex re(pattern, std::regex::icase);
    std::smatch match;
    if (std::regex_search(tag, match, re) && match.size() > 1) {
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
