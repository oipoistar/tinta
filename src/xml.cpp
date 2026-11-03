// A small non-validating XML reader (xml.h), enough for CSL styles and
// locales: one pass over the text, no DTD, no namespaces.

#pragma optimize("s", on)

#include "xml.h"

#include <cstdint>

namespace xml {
namespace {

void appendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) {
        out += (char)code;
    } else if (code < 0x800) {
        out += (char)(0xC0 | (code >> 6));
        out += (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += (char)(0xE0 | (code >> 12));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    } else {
        out += (char)(0xF0 | (code >> 18));
        out += (char)(0x80 | ((code >> 12) & 0x3F));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    }
}

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool nameStart(char c) {
    unsigned char u = (unsigned char)c;
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u == ':' || u >= 0x80;
}

bool nameChar(char c) {
    return nameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

// The local part of a qualified name; xml: and xmlns names stay whole
std::string localName(std::string_view qualified) {
    if (qualified.substr(0, 4) == "xml:" || qualified.substr(0, 5) == "xmlns") {
        return std::string(qualified);
    }
    size_t colon = qualified.rfind(':');
    return std::string(colon == std::string_view::npos ? qualified : qualified.substr(colon + 1));
}

struct Reader {
    std::string_view src;
    size_t at = 0;
    std::string error;
    size_t errorAt = 0;
    // Lines are counted lazily, forward from the last place asked about
    size_t countedTo = 0;
    int counted = 1;

    int lineOf(size_t pos) {
        if (pos < countedTo) {
            countedTo = 0;
            counted = 1;
        }
        if (pos > src.size()) pos = src.size();
        for (; countedTo < pos; ++countedTo) {
            if (src[countedTo] == '\n') ++counted;
        }
        return counted;
    }

    bool fail(std::string what, size_t pos) {
        if (error.empty()) {
            error = std::move(what);
            errorAt = pos;
        }
        return false;
    }

    bool startsWith(std::string_view word) const { return src.substr(at, word.size()) == word; }

    void skipSpace() {
        while (at < src.size() && space(src[at])) ++at;
    }

    // Decodes the entity whose '&' is at src[at], appending its character
    bool entity(std::string& out) {
        size_t start = at;
        size_t semi = src.find(';', at);
        if (semi == std::string_view::npos || semi - at > 12) {
            return fail("an '&' that starts no entity (write &amp;)", start);
        }
        std::string_view ref = src.substr(at + 1, semi - at - 1);
        at = semi + 1;
        if (ref == "lt") { out += '<'; return true; }
        if (ref == "gt") { out += '>'; return true; }
        if (ref == "amp") { out += '&'; return true; }
        if (ref == "quot") { out += '"'; return true; }
        if (ref == "apos") { out += '\''; return true; }
        if (ref.size() >= 2 && ref[0] == '#') {
            uint32_t code = 0;
            bool hex = ref[1] == 'x';
            size_t i = hex ? 2 : 1;
            if (i >= ref.size()) return fail("an empty character reference", start);
            for (; i < ref.size(); ++i) {
                char c = ref[i];
                uint32_t digit;
                if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
                else if (hex && c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a' + 10);
                else if (hex && c >= 'A' && c <= 'F') digit = (uint32_t)(c - 'A' + 10);
                else return fail("a bad character reference &" + std::string(ref) + ";", start);
                code = code * (hex ? 16 : 10) + digit;
                if (code > 0x10FFFF) return fail("a character reference past Unicode", start);
            }
            if (code == 0 || (code >= 0xD800 && code <= 0xDFFF)) {
                return fail("a character reference to no character", start);
            }
            appendUtf8(out, code);
            return true;
        }
        return fail("an unknown entity &" + std::string(ref) + ";", start);
    }

    // Text up to the next '<', entities decoded
    bool text(std::string& out) {
        while (at < src.size() && src[at] != '<') {
            if (src[at] == '&') {
                if (!entity(out)) return false;
            } else {
                size_t end = at;
                while (end < src.size() && src[end] != '<' && src[end] != '&') ++end;
                out.append(src.data() + at, end - at);
                at = end;
            }
        }
        return true;
    }

    bool name(std::string_view& out) {
        size_t start = at;
        if (at >= src.size() || !nameStart(src[at])) return fail("expected a name", at);
        while (at < src.size() && nameChar(src[at])) ++at;
        out = src.substr(start, at - start);
        return true;
    }

    // Skips past `close`, failing with `what` when it never comes
    bool skipPast(std::string_view close, const char* what) {
        size_t end = src.find(close, at);
        if (end == std::string_view::npos) return fail(what, at);
        at = end + close.size();
        return true;
    }

    bool doctype() {
        // <!DOCTYPE name ... [internal subset]>; the subset is skipped whole
        size_t start = at;
        at += 9;
        int bracket = 0;
        char quote = 0;
        while (at < src.size()) {
            char c = src[at++];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '[') {
                ++bracket;
            } else if (c == ']') {
                --bracket;
            } else if (c == '>' && bracket <= 0) {
                return true;
            }
        }
        return fail("an unclosed <!DOCTYPE", start);
    }

    // Comments, processing instructions and white space between elements
    bool misc(bool allowDoctype) {
        for (;;) {
            skipSpace();
            if (startsWith("<!--")) {
                if (!skipPast("-->", "an unclosed comment")) return false;
            } else if (startsWith("<?")) {
                if (!skipPast("?>", "an unclosed processing instruction")) return false;
            } else if (allowDoctype && startsWith("<!DOCTYPE")) {
                if (!doctype()) return false;
            } else {
                return true;
            }
        }
    }

    // A start tag from its '<': the element's name and attributes. `empty`
    // says it closed itself
    bool startTag(Node& node, std::string_view& qualified, bool& empty) {
        size_t start = at;
        ++at;
        if (!name(qualified)) return false;
        node.name = localName(qualified);
        node.line = lineOf(start);
        for (;;) {
            bool spaced = at < src.size() && space(src[at]);
            skipSpace();
            if (at >= src.size()) return fail("an unclosed <" + std::string(qualified) + ">", start);
            if (src[at] == '>') {
                ++at;
                empty = false;
                return true;
            }
            if (src[at] == '/') {
                if (at + 1 < src.size() && src[at + 1] == '>') {
                    at += 2;
                    empty = true;
                    return true;
                }
                return fail("a stray '/' in <" + std::string(qualified) + ">", at);
            }
            if (!spaced) return fail("attributes need white space between them", at);
            std::string_view key;
            size_t keyAt = at;
            if (!name(key)) return false;
            skipSpace();
            if (at >= src.size() || src[at] != '=') {
                return fail("attribute " + std::string(key) + " has no value", keyAt);
            }
            ++at;
            skipSpace();
            if (at >= src.size() || (src[at] != '"' && src[at] != '\'')) {
                return fail("attribute " + std::string(key) + " needs a quoted value", at);
            }
            char quote = src[at++];
            std::string value;
            while (at < src.size() && src[at] != quote) {
                char c = src[at];
                if (c == '<') return fail("a '<' inside attribute " + std::string(key), at);
                if (c == '&') {
                    if (!entity(value)) return false;
                    continue;
                }
                value += space(c) ? ' ' : c;
                ++at;
            }
            if (at >= src.size()) {
                return fail("attribute " + std::string(key) + " is never closed", keyAt);
            }
            ++at;
            std::string local = localName(key);
            for (const auto& attribute : node.attributes) {
                if (attribute.first == local) {
                    return fail("attribute " + std::string(key) + " given twice", keyAt);
                }
            }
            node.attributes.emplace_back(std::move(local), std::move(value));
        }
    }

    bool document(Node& root) {
        if (src.substr(0, 3) == "\xEF\xBB\xBF") at = 3;
        if (!misc(true)) return false;
        if (at >= src.size() || src[at] != '<') return fail("no root element", at);
        std::string_view rootName;
        bool empty = false;
        if (!startTag(root, rootName, empty)) return false;
        // Ancestors stay put while their deepest descendant grows, so the
        // pointers in the stack stay valid
        std::vector<Node*> stack;
        std::vector<std::string_view> names;
        std::vector<size_t> starts;
        if (!empty) {
            stack.push_back(&root);
            names.push_back(rootName);
            starts.push_back(0);
        }
        while (!stack.empty()) {
            Node& parent = *stack.back();
            if (at >= src.size()) {
                return fail("<" + std::string(names.back()) + "> is never closed", starts.back());
            }
            if (src[at] != '<') {
                size_t start = at;
                std::string chars;
                if (!text(chars)) return false;
                if (!parent.children.empty() && parent.children.back().isText()) {
                    parent.children.back().text += chars;
                } else {
                    Node node;
                    node.line = lineOf(start);
                    node.text = std::move(chars);
                    parent.children.push_back(std::move(node));
                }
                continue;
            }
            if (startsWith("</")) {
                size_t start = at;
                at += 2;
                std::string_view closing;
                if (!name(closing)) return false;
                skipSpace();
                if (at >= src.size() || src[at] != '>') return fail("an unclosed end tag", start);
                ++at;
                if (closing != names.back()) {
                    return fail("</" + std::string(closing) + "> closes <" + std::string(names.back()) +
                                    ">",
                                start);
                }
                stack.pop_back();
                names.pop_back();
                starts.pop_back();
                continue;
            }
            if (startsWith("<!--")) {
                if (!skipPast("-->", "an unclosed comment")) return false;
                continue;
            }
            if (startsWith("<![CDATA[")) {
                size_t start = at;
                at += 9;
                size_t end = src.find("]]>", at);
                if (end == std::string_view::npos) return fail("an unclosed CDATA section", start);
                std::string_view chars = src.substr(at, end - at);
                at = end + 3;
                if (!parent.children.empty() && parent.children.back().isText()) {
                    parent.children.back().text.append(chars.data(), chars.size());
                } else {
                    Node node;
                    node.line = lineOf(start);
                    node.text = std::string(chars);
                    parent.children.push_back(std::move(node));
                }
                continue;
            }
            if (startsWith("<?")) {
                if (!skipPast("?>", "an unclosed processing instruction")) return false;
                continue;
            }
            if (startsWith("<!")) return fail("a declaration inside an element", at);
            size_t start = at;
            // Readers walk the tree recursively; no style nests anywhere near this
            if (stack.size() >= 256) return fail("elements nested too deep", start);
            parent.children.emplace_back();
            Node& child = parent.children.back();
            std::string_view childName;
            if (!startTag(child, childName, empty)) return false;
            if (!empty) {
                stack.push_back(&child);
                names.push_back(childName);
                starts.push_back(start);
            }
        }
        if (!misc(false)) return false;
        if (at < src.size()) return fail("content after the root element", at);
        return true;
    }
};

void collectText(const Node& node, std::string& out) {
    if (node.isText()) {
        out += node.text;
        return;
    }
    for (const Node& child : node.children) collectText(child, out);
}
}

const std::string* Node::attribute(std::string_view key) const {
    for (const auto& attribute : attributes) {
        if (attribute.first == key) return &attribute.second;
    }
    return nullptr;
}

std::string_view Node::get(std::string_view key, std::string_view fallback) const {
    const std::string* value = attribute(key);
    return value ? std::string_view(*value) : fallback;
}

std::string Node::innerText() const {
    std::string out;
    collectText(*this, out);
    return out;
}

bool parse(std::string_view text, Node& root, std::string& error, int& errorLine) {
    Reader reader;
    reader.src = text;
    root = Node();
    if (reader.document(root)) {
        error.clear();
        errorLine = 0;
        return true;
    }
    error = reader.error;
    errorLine = reader.lineOf(reader.errorAt);
    root = Node();
    return false;
}
}
