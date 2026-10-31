// Sheets: the JSON Canvas model, the .smd format and the layout
// algorithms (sheet.h). Plain data and text only.

// Sheets favour small code over fast: they are drawn by Direct2D and
// laid out by the document layout, which keep their own optimisation
#pragma optimize("s", on)

#include "sheet.h"
#include "frontmatter.h"
#include "mermaid.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <map>
#include <random>
#include <set>
#include <unordered_map>

namespace sheet {
namespace {

// --- JSON ----------------------------------------------------------------

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;
    size_t begin = 0, end = 0;  // where the value sits in the source

    const Json* get(std::string_view key) const {
        for (const auto& field : fields) {
            if (field.first == key) return &field.second;
        }
        return nullptr;
    }
};

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

struct JsonReader {
    std::string_view src;
    size_t at = 0;
    int depth = 0;
    std::string error;

    bool fail(const char* what) {
        if (error.empty()) error = std::string(what) + " at offset " + std::to_string(at);
        return false;
    }
    void space() {
        while (at < src.size() &&
               (src[at] == ' ' || src[at] == '\t' || src[at] == '\n' || src[at] == '\r')) {
            ++at;
        }
    }
    bool literal(std::string_view word) {
        if (src.substr(at, word.size()) != word) return false;
        at += word.size();
        return true;
    }
    bool hex4(uint32_t& out) {
        if (at + 4 > src.size()) return fail("short \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = src[at++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (uint32_t)(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& out) {
        if (at >= src.size() || src[at] != '"') return fail("expected a string");
        ++at;
        while (at < src.size()) {
            char c = src[at++];
            if (c == '"') return true;
            if ((unsigned char)c < 0x20) return fail("control character in a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at >= src.size()) break;
            char e = src[at++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t code = 0;
                    if (!hex4(code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF && src.substr(at, 2) == "\\u") {
                        size_t keep = at;
                        at += 2;
                        uint32_t low = 0;
                        if (!hex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            at = keep;
                        }
                    }
                    if (code >= 0xD800 && code <= 0xDFFF) code = 0xFFFD;  // a lone half
                    appendUtf8(out, code);
                    break;
                }
                default:
                    return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Json& out) {
        space();
        if (at >= src.size()) return fail("unexpected end");
        if (++depth > 64) return fail("nested too deep");
        out.begin = at;
        bool ok = true;
        char c = src[at];
        if (c == '{') {
            out.type = Json::Object;
            ++at;
            space();
            if (at < src.size() && src[at] == '}') {
                ++at;
            } else {
                while (ok) {
                    space();
                    std::string key;
                    if (!string(key)) { ok = false; break; }
                    space();
                    if (at >= src.size() || src[at] != ':') { ok = fail("expected ':'"); break; }
                    ++at;
                    Json item;
                    if (!value(item)) { ok = false; break; }
                    out.fields.emplace_back(std::move(key), std::move(item));
                    space();
                    if (at < src.size() && src[at] == ',') { ++at; continue; }
                    if (at < src.size() && src[at] == '}') { ++at; break; }
                    ok = fail("expected ',' or '}'");
                }
            }
        } else if (c == '[') {
            out.type = Json::Array;
            ++at;
            space();
            if (at < src.size() && src[at] == ']') {
                ++at;
            } else {
                while (ok) {
                    Json item;
                    if (!value(item)) { ok = false; break; }
                    out.items.push_back(std::move(item));
                    space();
                    if (at < src.size() && src[at] == ',') { ++at; continue; }
                    if (at < src.size() && src[at] == ']') { ++at; break; }
                    ok = fail("expected ',' or ']'");
                }
            }
        } else if (c == '"') {
            out.type = Json::String;
            ok = string(out.text);
        } else if (literal("true")) {
            out.type = Json::Bool;
            out.boolean = true;
        } else if (literal("false")) {
            out.type = Json::Bool;
        } else if (literal("null")) {
            out.type = Json::Null;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = at;
            if (src[at] == '-') ++at;
            while (at < src.size() &&
                   ((src[at] >= '0' && src[at] <= '9') || src[at] == '.' || src[at] == 'e' ||
                    src[at] == 'E' || src[at] == '+' || src[at] == '-')) {
                ++at;
            }
            std::string digits(src.substr(start, at - start));
            char* endPtr = nullptr;
            out.type = Json::Number;
            out.number = std::strtod(digits.c_str(), &endPtr);
            if (!endPtr || *endPtr) ok = fail("bad number");
        } else {
            ok = fail("unexpected character");
        }
        --depth;
        out.end = at;
        return ok;
    }
};

void writeJsonString(std::string& out, std::string_view text) {
    out += '"';
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", (unsigned)(unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

std::string textOf(const Json* value) {
    return value && value->type == Json::String ? value->text : std::string();
}

float numberOf(const Json* value, float fallback) {
    return value && value->type == Json::Number && std::isfinite(value->number)
               ? (float)value->number
               : fallback;
}

void writeNumber(std::string& out, float value) {
    out += std::to_string((long long)std::lround(value));
}

bool knownNodeField(std::string_view key) {
    static const char* known[] = {"id", "type", "x", "y", "width", "height", "color", "text",
                                  "file", "subpath", "url", "label", "background",
                                  "backgroundStyle", "tintaLane", "tintaLimit", "tintaFolded"};
    for (const char* k : known) {
        if (key == k) return true;
    }
    return false;
}

bool knownEdgeField(std::string_view key) {
    static const char* known[] = {"id", "fromNode", "fromSide", "fromEnd", "toNode",
                                  "toSide", "toEnd", "color", "label"};
    for (const char* k : known) {
        if (key == k) return true;
    }
    return false;
}

std::string lowerAscii(std::string_view text) {
    std::string out(text);
    for (auto& c : out) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return out;
}

bool endsWithNoCase(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    return lowerAscii(text.substr(text.size() - suffix.size())) == lowerAscii(suffix);
}

// --- Markdown lines ------------------------------------------------------

struct Line {
    std::string_view text;  // without its line break
    size_t start = 0;
};

std::vector<Line> splitLines(std::string_view text) {
    std::vector<Line> lines;
    size_t at = 0;
    while (at <= text.size()) {
        size_t next = text.find('\n', at);
        size_t end = next == std::string_view::npos ? text.size() : next;
        std::string_view line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back({line, at});
        if (next == std::string_view::npos) break;
        at = next + 1;
    }
    return lines;
}

std::string_view trimView(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

// A fence opener or closer: its character and length, or 0
int fenceRun(std::string_view line, char& marker) {
    size_t indent = 0;
    while (indent < line.size() && indent < 4 && line[indent] == ' ') ++indent;
    if (indent > 3 || indent >= line.size()) return 0;
    char c = line[indent];
    if (c != '`' && c != '~') return 0;
    size_t run = 0;
    while (indent + run < line.size() && line[indent + run] == c) ++run;
    if (run < 3) return 0;
    marker = c;
    return (int)run;
}

std::string_view frontmatterBlockEnd(std::string_view text, size_t& bodyStart) {
    bodyStart = 0;
    size_t begin = text.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
    if (text.substr(begin, 3) != "---") return {};
    size_t eol = text.find('\n', begin);
    if (eol == std::string_view::npos) return {};
    if (trimView(text.substr(begin, eol - begin)) != "---") return {};
    size_t at = eol + 1;
    while (at <= text.size()) {
        size_t next = text.find('\n', at);
        size_t end = next == std::string_view::npos ? text.size() : next;
        std::string_view line = trimView(text.substr(at, end - at));
        if (line == "---" || line == "...") {
            bodyStart = next == std::string_view::npos ? text.size() : next + 1;
            return text.substr(begin, bodyStart - begin);
        }
        if (next == std::string_view::npos) break;
        at = next + 1;
    }
    return {};
}

// Inline Markdown reduced to its text, for titles
std::string plainInline(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '*' || c == '_' || c == '`' || c == '~' || c == '=') continue;
        if (c == '!' && i + 1 < text.size() && text[i + 1] == '[') continue;
        if (c == '[' && i + 1 < text.size() && text[i + 1] == '[') {
            // [[target|alias]] shows its alias, else its target
            size_t close = text.find("]]", i + 2);
            if (close != std::string_view::npos) {
                std::string_view inner = text.substr(i + 2, close - i - 2);
                size_t bar = inner.find('|');
                out += std::string(bar == std::string_view::npos ? inner : inner.substr(bar + 1));
                i = close + 1;
                continue;
            }
        }
        if (c == '[') {
            size_t close = text.find(']', i + 1);
            if (close != std::string_view::npos && close + 1 < text.size() && text[close + 1] == '(') {
                size_t paren = text.find(')', close + 2);
                if (paren != std::string_view::npos) {
                    out += plainInline(text.substr(i + 1, close - i - 1));
                    i = paren;
                    continue;
                }
            }
            continue;
        }
        if (c == ']') continue;
        out += c;
    }
    std::string_view trimmed = trimView(out);
    return std::string(trimmed);
}

// The title-worthy text of a line: an ATX heading's text, a list or task
// item's text, a quote's text
std::string lineText(std::string_view line, int* headingLevel = nullptr) {
    std::string_view t = trimView(line);
    if (headingLevel) *headingLevel = 0;
    size_t hashes = 0;
    while (hashes < t.size() && t[hashes] == '#') ++hashes;
    if (hashes >= 1 && hashes <= 6 && (hashes == t.size() || t[hashes] == ' ')) {
        if (headingLevel) *headingLevel = (int)hashes;
        t.remove_prefix(hashes);
        t = trimView(t);
        while (!t.empty() && t.back() == '#') t.remove_suffix(1);
        return plainInline(trimView(t));
    }
    while (!t.empty() && t.front() == '>') t = trimView(t.substr(1));
    if (t.size() >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') {
        t = trimView(t.substr(2));
    } else {
        size_t digits = 0;
        while (digits < t.size() && t[digits] >= '0' && t[digits] <= '9') ++digits;
        if (digits > 0 && digits + 1 < t.size() && (t[digits] == '.' || t[digits] == ')') &&
            t[digits + 1] == ' ') {
            t = trimView(t.substr(digits + 2));
        }
    }
    if (t.size() >= 4 && t[0] == '[' && t[2] == ']' && t[3] == ' ') t = trimView(t.substr(4));
    return plainInline(t);
}

std::string clipUtf8(const std::string& text, size_t maxChars) {
    size_t chars = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (((unsigned char)text[i] & 0xC0) != 0x80) {
            if (chars == maxChars) return text.substr(0, i) + "\xE2\x80\xA6";  // an ellipsis
            ++chars;
        }
    }
    return text;
}

std::string percentDecode(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit((unsigned char)text[i + 1]) &&
            std::isxdigit((unsigned char)text[i + 2])) {
            out += (char)std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16);
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool hasScheme(std::string_view url) {
    size_t colon = url.find(':');
    if (colon == std::string_view::npos || colon < 2) return false;  // C:\ is a path
    for (size_t i = 0; i < colon; ++i) {
        char c = url[i];
        if (!std::isalnum((unsigned char)c) && c != '+' && c != '-' && c != '.') return false;
    }
    return true;
}

std::string yamlScalar(std::string_view value) {
    bool quote = value.empty() || value.front() == ' ' || value.back() == ' ';
    static const std::string_view special = "-?:,[]{}#&*!|>'\"%@`";
    if (!value.empty() && special.find(value.front()) != std::string_view::npos) quote = true;
    if (value.find(": ") != std::string_view::npos || value.find(" #") != std::string_view::npos)
        quote = true;
    std::string lower = lowerAscii(value);
    static const char* reserved[] = {"true", "false", "yes", "no", "on", "off", "null", "~"};
    for (const char* r : reserved) {
        if (lower == r) quote = true;
    }
    bool numeric = value.find_first_of("0123456789") != std::string_view::npos;
    for (char c : value) {
        if (!std::isdigit((unsigned char)c) && c != '.' && c != '-' && c != '+') numeric = false;
    }
    if (numeric) quote = true;
    for (char c : value) {
        if (c == '\n' || c == '\r' || c == '\t') quote = true;
    }
    if (!quote) return std::string(value);
    std::string out = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\r') continue;
        if (c == '\t') { out += "\\t"; continue; }
        out += c;
    }
    return out + "\"";
}

struct Center { float x, y; };
Center centerOf(const Node& node) { return {node.x + node.width * 0.5f, node.y + node.height * 0.5f}; }

bool positionBefore(const Node& a, const Node& b) {
    // Rows first: cards whose tops lie within half a card of each other
    // read left to right
    float rowTolerance = std::min(a.height, b.height) * 0.5f;
    if (std::fabs(a.y - b.y) > rowTolerance) return a.y < b.y;
    if (a.x != b.x) return a.x < b.x;
    return a.id < b.id;
}

}  // namespace

// --- JSON Canvas ---------------------------------------------------------

bool parseCanvas(std::string_view json, Sheet& out, std::string* error) {
    out = Sheet{};
    std::string_view trimmed = json;
    if (trimmed.substr(0, 3) == "\xEF\xBB\xBF") trimmed.remove_prefix(3);
    size_t first = trimmed.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return true;  // an empty file is an empty sheet
    JsonReader reader{trimmed};
    Json root;
    if (!reader.value(root)) {
        if (error) *error = reader.error;
        return false;
    }
    reader.space();
    if (reader.at != trimmed.size()) {
        if (error) *error = "text after the canvas";
        return false;
    }
    if (root.type != Json::Object) {
        if (error) *error = "the canvas is not an object";
        return false;
    }
    std::set<std::string> ids;
    for (const auto& field : root.fields) {
        if (field.first == "nodes" && field.second.type == Json::Array) {
            for (const Json& item : field.second.items) {
                if (item.type != Json::Object) continue;
                Node node;
                node.id = textOf(item.get("id"));
                std::string type = textOf(item.get("type"));
                if (type == "text") node.type = NodeType::Text;
                else if (type == "file") node.type = NodeType::File;
                else if (type == "link") node.type = NodeType::Link;
                else if (type == "group") node.type = NodeType::Group;
                else continue;  // a node type this version cannot draw
                node.x = numberOf(item.get("x"), 0);
                node.y = numberOf(item.get("y"), 0);
                node.width = std::max(1.0f, numberOf(item.get("width"), 250));
                node.height = std::max(1.0f, numberOf(item.get("height"), 60));
                node.color = textOf(item.get("color"));
                node.text = textOf(item.get("text"));
                node.file = textOf(item.get("file"));
                node.subpath = textOf(item.get("subpath"));
                node.url = textOf(item.get("url"));
                node.label = textOf(item.get("label"));
                node.background = textOf(item.get("background"));
                node.backgroundStyle = textOf(item.get("backgroundStyle"));
                node.laneKey = textOf(item.get("tintaLane"));
                node.laneLimit = std::max(0, (int)numberOf(item.get("tintaLimit"), 0));
                const Json* folded = item.get("tintaFolded");
                node.folded = folded && folded->type == Json::Bool && folded->boolean;
                for (const auto& f : item.fields) {
                    if (!knownNodeField(f.first)) {
                        node.extra.emplace_back(
                            f.first, std::string(trimmed.substr(f.second.begin, f.second.end - f.second.begin)));
                    }
                }
                if (node.id.empty() || ids.count(node.id)) node.id = newId();
                ids.insert(node.id);
                out.nodes.push_back(std::move(node));
            }
        } else if (field.first == "edges" && field.second.type == Json::Array) {
            for (const Json& item : field.second.items) {
                if (item.type != Json::Object) continue;
                Edge edge;
                edge.id = textOf(item.get("id"));
                edge.fromNode = textOf(item.get("fromNode"));
                edge.toNode = textOf(item.get("toNode"));
                edge.fromSide = sideFromName(textOf(item.get("fromSide")));
                edge.toSide = sideFromName(textOf(item.get("toSide")));
                edge.fromArrow = textOf(item.get("fromEnd")) == "arrow";
                edge.toArrow = textOf(item.get("toEnd")) != "none";
                edge.color = textOf(item.get("color"));
                edge.label = textOf(item.get("label"));
                for (const auto& f : item.fields) {
                    if (!knownEdgeField(f.first)) {
                        edge.extra.emplace_back(
                            f.first, std::string(trimmed.substr(f.second.begin, f.second.end - f.second.begin)));
                    }
                }
                if (edge.id.empty() || ids.count(edge.id)) edge.id = newId();
                ids.insert(edge.id);
                out.edges.push_back(std::move(edge));
            }
        } else if (field.first == "tintaWriteLinks") {
            out.writeLinks = field.second.type == Json::Bool && field.second.boolean;
        } else {
            out.extra.emplace_back(field.first, std::string(trimmed.substr(
                                                    field.second.begin, field.second.end - field.second.begin)));
        }
    }
    // Edges to nodes that are not there would draw from nowhere
    out.edges.erase(std::remove_if(out.edges.begin(), out.edges.end(),
                                   [&](const Edge& e) {
                                       return nodeIndex(out, e.fromNode) < 0 || nodeIndex(out, e.toNode) < 0;
                                   }),
                    out.edges.end());
    return true;
}

std::string writeCanvas(const Sheet& sheet) {
    std::string out = "{\n\t\"nodes\":[";
    for (size_t i = 0; i < sheet.nodes.size(); ++i) {
        const Node& n = sheet.nodes[i];
        out += i ? ",\n\t\t{" : "\n\t\t{";
        out += "\"id\":";
        writeJsonString(out, n.id);
        out += ",\"type\":";
        switch (n.type) {
            case NodeType::Text:
                out += "\"text\",\"text\":";
                writeJsonString(out, n.text);
                break;
            case NodeType::File:
                out += "\"file\",\"file\":";
                writeJsonString(out, n.file);
                if (!n.subpath.empty()) {
                    out += ",\"subpath\":";
                    writeJsonString(out, n.subpath);
                }
                break;
            case NodeType::Link:
                out += "\"link\",\"url\":";
                writeJsonString(out, n.url);
                break;
            case NodeType::Group:
                out += "\"group\"";
                if (!n.label.empty()) {
                    out += ",\"label\":";
                    writeJsonString(out, n.label);
                }
                if (!n.background.empty()) {
                    out += ",\"background\":";
                    writeJsonString(out, n.background);
                }
                if (!n.backgroundStyle.empty()) {
                    out += ",\"backgroundStyle\":";
                    writeJsonString(out, n.backgroundStyle);
                }
                if (!n.laneKey.empty()) {
                    out += ",\"tintaLane\":";
                    writeJsonString(out, n.laneKey);
                }
                if (n.laneLimit > 0) {
                    out += ",\"tintaLimit\":";
                    out += std::to_string(n.laneLimit);
                }
                break;
        }
        if (n.folded) out += ",\"tintaFolded\":true";
        out += ",\"x\":";
        writeNumber(out, n.x);
        out += ",\"y\":";
        writeNumber(out, n.y);
        out += ",\"width\":";
        writeNumber(out, n.width);
        out += ",\"height\":";
        writeNumber(out, n.height);
        if (!n.color.empty()) {
            out += ",\"color\":";
            writeJsonString(out, n.color);
        }
        for (const auto& extra : n.extra) {
            out += ',';
            writeJsonString(out, extra.first);
            out += ':';
            out += extra.second;
        }
        out += '}';
    }
    out += sheet.nodes.empty() ? "],\n\t\"edges\":[" : "\n\t],\n\t\"edges\":[";
    for (size_t i = 0; i < sheet.edges.size(); ++i) {
        const Edge& e = sheet.edges[i];
        out += i ? ",\n\t\t{" : "\n\t\t{";
        out += "\"id\":";
        writeJsonString(out, e.id);
        out += ",\"fromNode\":";
        writeJsonString(out, e.fromNode);
        if (e.fromSide != Side::None) {
            out += ",\"fromSide\":";
            writeJsonString(out, sideName(e.fromSide));
        }
        if (e.fromArrow) out += ",\"fromEnd\":\"arrow\"";
        out += ",\"toNode\":";
        writeJsonString(out, e.toNode);
        if (e.toSide != Side::None) {
            out += ",\"toSide\":";
            writeJsonString(out, sideName(e.toSide));
        }
        if (!e.toArrow) out += ",\"toEnd\":\"none\"";
        if (!e.color.empty()) {
            out += ",\"color\":";
            writeJsonString(out, e.color);
        }
        if (!e.label.empty()) {
            out += ",\"label\":";
            writeJsonString(out, e.label);
        }
        for (const auto& extra : e.extra) {
            out += ',';
            writeJsonString(out, extra.first);
            out += ':';
            out += extra.second;
        }
        out += '}';
    }
    out += sheet.edges.empty() ? "]" : "\n\t]";
    if (sheet.writeLinks) out += ",\n\t\"tintaWriteLinks\":true";
    for (const auto& extra : sheet.extra) {
        out += ",\n\t";
        writeJsonString(out, extra.first);
        out += ':';
        out += extra.second;
    }
    out += "\n}\n";
    return out;
}

// --- .smd ----------------------------------------------------------------

namespace {

bool isCanvasInfo(std::string_view info) {
    std::string lower = lowerAscii(trimView(info));
    for (char& c : lower) {
        if (c == '-' || c == '_' || c == '{' || c == '}' || c == '.') c = ' ';
    }
    size_t at = 0;
    while (at < lower.size()) {
        while (at < lower.size() && lower[at] == ' ') ++at;
        size_t end = lower.find(' ', at);
        if (end == std::string::npos) end = lower.size();
        std::string word = lower.substr(at, end - at);
        if (word == "canvas" || word == "jsoncanvas") return true;
        at = end;
    }
    return false;
}

}  // namespace

bool parseSmd(std::string_view source, Sheet& out, SmdInfo& info, std::string* error) {
    out = Sheet{};
    info = SmdInfo{};
    size_t bodyStart = 0;
    std::string_view front = frontmatterBlockEnd(source, bodyStart);
    if (!front.empty()) {
        fm::Document doc = fm::parse(std::string(source.substr(0, bodyStart)));
        if (const fm::Property* folder = fm::find(doc, "folder")) info.folder = folder->text;
    }
    std::vector<Line> lines = splitLines(source.substr(bodyStart));
    bool inFence = false;
    char fenceChar = 0;
    int fenceLen = 0;
    bool canvasFence = false;
    size_t blockStart = 0;
    std::string_view lastBlock;
    bool found = false;
    for (const Line& line : lines) {
        char marker = 0;
        int run = fenceRun(line.text, marker);
        if (!inFence) {
            if (run) {
                inFence = true;
                fenceChar = marker;
                fenceLen = run;
                size_t infoAt = line.text.find_first_not_of(" ");
                std::string_view infoText = line.text.substr(infoAt + run);
                canvasFence = isCanvasInfo(infoText);
                blockStart = bodyStart + line.start + line.text.size();
                // Past the line break
                size_t lb = source.find('\n', blockStart);
                blockStart = lb == std::string_view::npos ? source.size() : lb + 1;
                continue;
            }
            if (info.title.empty()) {
                int level = 0;
                std::string text = lineText(line.text, &level);
                if (level == 1) info.title = text;
            }
        } else if (run >= fenceLen && marker == fenceChar &&
                   trimView(line.text).find_first_not_of(fenceChar) == std::string_view::npos) {
            if (canvasFence) {
                size_t blockEnd = bodyStart + line.start;
                lastBlock = source.substr(blockStart, blockEnd - blockStart);
                found = true;
            }
            inFence = false;
            canvasFence = false;
        }
    }
    if (!found) return true;  // a sheet with nothing on it yet
    return parseCanvas(lastBlock, out, error);
}

std::string writeSmd(const Sheet& sheet, const SmdInfo& info, std::string_view previous,
                     const std::function<std::string(const Node&)>& titleOf) {
    std::string eol = "\n";
    std::string out;
    // Front matter: the old block with its folder brought up to date
    size_t bodyStart = 0;
    std::string_view front = frontmatterBlockEnd(previous, bodyStart);
    std::string folderLine = "folder: " + yamlScalar(info.folder.empty() ? "." : info.folder);
    if (!front.empty()) {
        std::vector<Line> lines = splitLines(front);
        bool wrote = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string_view text = lines[i].text;
            if (i + 1 == lines.size() && text.empty()) break;  // the block's final line break
            bool closing = i > 0 && (trimView(text) == "---" || trimView(text) == "...");
            if (closing && !wrote) {
                out += folderLine + eol;
                wrote = true;
            }
            if (!closing && text.substr(0, 7) == "folder:") {
                if (!wrote) out += folderLine + eol;
                wrote = true;
                continue;
            }
            out += std::string(text) + eol;
        }
    } else {
        out += "---" + eol + folderLine + eol + "---" + eol;
    }
    out += eol;
    out += "# " + (info.title.empty() ? std::string("Sheet") : info.title) + eol + eol;

    // The outline: cards outside groups, then each group's, with their
    // arrows; a Markdown reader sees the sheet as a list of its notes
    auto title = [&](const Node& n) -> std::string {
        std::string t = titleOf ? titleOf(n) : std::string();
        if (!t.empty()) return t;
        if (n.type == NodeType::File) {
            std::string name = n.file;
            size_t slash = name.find_last_of('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
            size_t dot = name.find_last_of('.');
            if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
            return name;
        }
        if (n.type == NodeType::Text) return noteTitle(n.text, "\xE2\x80\xA6");
        if (n.type == NodeType::Link) return n.url;
        return n.label;
    };
    auto escapeLabel = [](std::string text) {
        std::string out;
        for (char c : text) {
            if (c == '[' || c == ']') out += '\\';
            if (c == '\n' || c == '\r') { out += ' '; continue; }
            out += c;
        }
        return out;
    };
    auto item = [&](const Node& n) {
        std::string line = "- ";
        if (n.type == NodeType::File) {
            line += "[" + escapeLabel(title(n)) + "](<" + n.file + n.subpath + ">)";
        } else if (n.type == NodeType::Link) {
            line += "<" + n.url + ">";
        } else {
            line += escapeLabel(clipUtf8(title(n), 80));
        }
        std::vector<std::string> arrows;
        for (const Edge& e : sheet.edges) {
            if (e.fromNode != n.id) continue;
            int to = nodeIndex(sheet, e.toNode);
            if (to < 0) continue;
            std::string arrow = (e.toArrow || !e.fromArrow) ? "\xE2\x86\x92 " : "\xE2\x86\x90 ";
            arrow += escapeLabel(clipUtf8(title(sheet.nodes[to]), 60));
            if (!e.label.empty()) arrow += " (" + escapeLabel(e.label) + ")";
            arrows.push_back(arrow);
        }
        for (size_t i = 0; i < arrows.size(); ++i) line += (i ? ", " : " ") + arrows[i];
        return line + eol;
    };
    std::vector<int> order = readingOrder(sheet);
    std::vector<int> groups;
    for (int i = 0; i < (int)sheet.nodes.size(); ++i) {
        if (sheet.nodes[i].type == NodeType::Group) groups.push_back(i);
    }
    std::sort(groups.begin(), groups.end(),
              [&](int a, int b) { return positionBefore(sheet.nodes[a], sheet.nodes[b]); });
    bool any = false;
    for (int index : order) {
        if (groupOf(sheet, index) >= 0) continue;
        out += item(sheet.nodes[index]);
        any = true;
    }
    for (int g : groups) {
        std::string block;
        for (int index : order) {
            if (groupOf(sheet, index) == g) block += item(sheet.nodes[index]);
        }
        if (any || !out.empty()) out += eol;
        out += "## " + escapeLabel(sheet.nodes[g].label.empty() ? std::string("\xE2\x80\x94") : sheet.nodes[g].label) + eol;
        if (!block.empty()) out += eol + block;
        any = true;
    }
    if (any) out += eol;
    out += "```json canvas" + eol;
    out += writeCanvas(sheet);
    out += "```" + eol;
    return out;
}

// --- Helpers -------------------------------------------------------------

std::string newId() {
    static std::mt19937_64 engine = [] {
        std::random_device device;
        uint64_t seed = ((uint64_t)device() << 32) ^ device() ^
                        (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
        return std::mt19937_64(seed);
    }();
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)engine());
    return buf;
}

const char* sideName(Side side) {
    switch (side) {
        case Side::Top: return "top";
        case Side::Right: return "right";
        case Side::Bottom: return "bottom";
        case Side::Left: return "left";
        default: return "";
    }
}

Side sideFromName(std::string_view name) {
    if (name == "top") return Side::Top;
    if (name == "right") return Side::Right;
    if (name == "bottom") return Side::Bottom;
    if (name == "left") return Side::Left;
    return Side::None;
}

int nodeIndex(const Sheet& sheet, std::string_view id) {
    for (int i = 0; i < (int)sheet.nodes.size(); ++i) {
        if (sheet.nodes[i].id == id) return i;
    }
    return -1;
}

int edgeIndex(const Sheet& sheet, std::string_view id) {
    for (int i = 0; i < (int)sheet.edges.size(); ++i) {
        if (sheet.edges[i].id == id) return i;
    }
    return -1;
}

Box boxOf(const Node& node) { return {node.x, node.y, node.x + node.width, node.y + node.height}; }

bool insideGroup(const Node& group, const Node& node) {
    if (&group == &node || group.type != NodeType::Group) return false;
    Box g = boxOf(group), n = boxOf(node);
    const float slack = 1.0f;
    if (n.left >= g.left - slack && n.top >= g.top - slack && n.right <= g.right + slack &&
        n.bottom <= g.bottom + slack) {
        return true;
    }
    if (node.type == NodeType::Group) return false;
    // A card bigger than the group counts when its centre is in
    if (node.width > group.width || node.height > group.height) {
        Center c = centerOf(node);
        return c.x >= g.left && c.x <= g.right && c.y >= g.top && c.y <= g.bottom;
    }
    return false;
}

int groupOf(const Sheet& sheet, int node) {
    if (node < 0 || node >= (int)sheet.nodes.size()) return -1;
    int best = -1;
    float bestArea = 0;
    for (int i = 0; i < (int)sheet.nodes.size(); ++i) {
        if (i == node || sheet.nodes[i].type != NodeType::Group) continue;
        if (!insideGroup(sheet.nodes[i], sheet.nodes[node])) continue;
        float area = sheet.nodes[i].width * sheet.nodes[i].height;
        if (best < 0 || area < bestArea) {
            best = i;
            bestArea = area;
        }
    }
    return best;
}

void removeNode(Sheet& sheet, std::string_view id) {
    std::string key(id);
    sheet.edges.erase(std::remove_if(sheet.edges.begin(), sheet.edges.end(),
                                     [&](const Edge& e) { return e.fromNode == key || e.toNode == key; }),
                      sheet.edges.end());
    sheet.nodes.erase(std::remove_if(sheet.nodes.begin(), sheet.nodes.end(),
                                     [&](const Node& n) { return n.id == key; }),
                      sheet.nodes.end());
}

Box bounds(const Sheet& sheet, const std::vector<int>& nodes) {
    Box box;
    bool first = true;
    auto take = [&](const Node& n) {
        Box b = boxOf(n);
        if (first) {
            box = b;
            first = false;
        } else {
            box.left = std::min(box.left, b.left);
            box.top = std::min(box.top, b.top);
            box.right = std::max(box.right, b.right);
            box.bottom = std::max(box.bottom, b.bottom);
        }
    };
    if (nodes.empty()) {
        for (const Node& n : sheet.nodes) take(n);
    } else {
        for (int i : nodes) {
            if (i >= 0 && i < (int)sheet.nodes.size()) take(sheet.nodes[i]);
        }
    }
    return box;
}

void edgeSides(const Node& from, const Node& to, Side storedFrom, Side storedTo, Side& fromSide,
               Side& toSide) {
    Center a = centerOf(from), b = centerOf(to);
    float dx = b.x - a.x, dy = b.y - a.y;
    // Scale by the boxes so wide cards side by side still join left-right
    float nx = dx / std::max(1.0f, (from.width + to.width) * 0.5f);
    float ny = dy / std::max(1.0f, (from.height + to.height) * 0.5f);
    Side autoFrom, autoTo;
    if (std::fabs(nx) >= std::fabs(ny)) {
        autoFrom = dx >= 0 ? Side::Right : Side::Left;
        autoTo = dx >= 0 ? Side::Left : Side::Right;
    } else {
        autoFrom = dy >= 0 ? Side::Bottom : Side::Top;
        autoTo = dy >= 0 ? Side::Top : Side::Bottom;
    }
    fromSide = storedFrom != Side::None ? storedFrom : autoFrom;
    toSide = storedTo != Side::None ? storedTo : autoTo;
}

// --- Algorithms ----------------------------------------------------------

std::vector<int> readingOrder(const Sheet& sheet, const std::vector<int>& subset) {
    std::vector<int> cards;
    if (subset.empty()) {
        for (int i = 0; i < (int)sheet.nodes.size(); ++i) {
            if (sheet.nodes[i].type != NodeType::Group) cards.push_back(i);
        }
    } else {
        for (int i : subset) {
            if (i >= 0 && i < (int)sheet.nodes.size() && sheet.nodes[i].type != NodeType::Group)
                cards.push_back(i);
        }
    }
    std::sort(cards.begin(), cards.end(),
              [&](int a, int b) { return positionBefore(sheet.nodes[a], sheet.nodes[b]); });
    cards.erase(std::unique(cards.begin(), cards.end()), cards.end());
    std::unordered_map<std::string, int> local;
    for (int i = 0; i < (int)cards.size(); ++i) local[sheet.nodes[cards[i]].id] = i;
    std::vector<std::vector<int>> out(cards.size());
    std::vector<int> indegree(cards.size(), 0);
    for (const Edge& e : sheet.edges) {
        auto f = local.find(e.fromNode), t = local.find(e.toNode);
        if (f == local.end() || t == local.end() || f->second == t->second) continue;
        int a = f->second, b = t->second;
        if (e.fromArrow && !e.toArrow) std::swap(a, b);
        out[a].push_back(b);
        indegree[b]++;
    }
    std::vector<int> order;
    std::vector<bool> done(cards.size(), false);
    // Cards are sorted by position, so the lowest ready index reads first
    std::set<int> ready;
    for (int i = 0; i < (int)cards.size(); ++i) {
        if (indegree[i] == 0) ready.insert(i);
    }
    while (order.size() < cards.size()) {
        int next;
        if (!ready.empty()) {
            next = *ready.begin();
            ready.erase(ready.begin());
        } else {
            next = -1;  // a cycle: cut it at its first card
            for (int i = 0; i < (int)cards.size(); ++i) {
                if (!done[i]) { next = i; break; }
            }
        }
        if (done[next]) continue;
        done[next] = true;
        order.push_back(cards[next]);
        for (int t : out[next]) {
            if (done[t]) continue;
            if (--indegree[t] <= 0) ready.insert(t);
        }
    }
    return order;
}

int neighbourInDirection(const Sheet& sheet, int from, int dx, int dy, const std::vector<char>* skip) {
    if (from < 0 || from >= (int)sheet.nodes.size()) return -1;
    Center a = centerOf(sheet.nodes[from]);
    float len = std::sqrt((float)(dx * dx + dy * dy));
    if (len == 0) return -1;
    float ux = dx / len, uy = dy / len;
    int best = -1;
    float bestScore = 0;
    for (int i = 0; i < (int)sheet.nodes.size(); ++i) {
        if (i == from || sheet.nodes[i].type == NodeType::Group) continue;
        if (skip && i < (int)skip->size() && (*skip)[i]) continue;
        Center b = centerOf(sheet.nodes[i]);
        float vx = b.x - a.x, vy = b.y - a.y;
        float along = vx * ux + vy * uy;
        if (along <= 1.0f) continue;
        float across = std::fabs(vx * uy - vy * ux);
        // Within a cone of about 60 degrees either side
        if (across > along * 1.8f) continue;
        float score = along + across * 2.0f;
        if (best < 0 || score < bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
}

std::vector<std::pair<float, float>> gridPlaces(int count, float x, float y, float width,
                                                float height, float gap) {
    std::vector<std::pair<float, float>> places;
    if (count <= 0) return places;
    int columns = std::max(1, (int)std::ceil(std::sqrt((float)count)));
    for (int i = 0; i < count; ++i) {
        places.emplace_back(x + (i % columns) * (width + gap), y + (i / columns) * (height + gap));
    }
    return places;
}

namespace {

struct Unit {
    std::vector<int> members;  // the group's node index first, then its contents
    int group = -1;
    float x = 0, y = 0, width = 0, height = 0;  // where the unit is now
};

// Lays out boxes (sizes) with arrows (pairs of box indexes): layered along
// the arrows, free boxes in a grid after them. Positions start at 0, 0.
std::vector<std::pair<float, float>> layoutBoxes(const std::vector<std::pair<float, float>>& sizes,
                                                 std::vector<std::pair<int, int>> arrows,
                                                 const std::vector<std::pair<float, float>>& now,
                                                 bool leftToRight) {
    const int count = (int)sizes.size();
    std::vector<std::pair<float, float>> place(count, {0.0f, 0.0f});
    if (count == 0) return place;
    std::vector<bool> linked(count, false);
    arrows.erase(std::remove_if(arrows.begin(), arrows.end(),
                                [&](const std::pair<int, int>& a) {
                                    return a.first == a.second || a.first < 0 || a.second < 0 ||
                                           a.first >= count || a.second >= count;
                                }),
                 arrows.end());
    for (auto& a : arrows) linked[a.first] = linked[a.second] = true;

    // Cycles become layers only once they are cut: drop the arrows that
    // point back against the reading order of the current positions
    std::vector<int> byPosition(count);
    for (int i = 0; i < count; ++i) byPosition[i] = i;
    std::sort(byPosition.begin(), byPosition.end(), [&](int a, int b) {
        if (std::fabs(now[a].second - now[b].second) > 1.0f) return now[a].second < now[b].second;
        return now[a].first < now[b].first;
    });
    std::vector<int> rankOf(count);
    for (int i = 0; i < count; ++i) rankOf[byPosition[i]] = i;
    {
        // Depth-first from each box in reading order: back edges close a cycle
        std::vector<std::vector<int>> out(count);
        for (auto& a : arrows) out[a.first].push_back(a.second);
        std::vector<int> state(count, 0);
        std::set<std::pair<int, int>> back;
        for (int start : byPosition) {
            if (state[start]) continue;
            std::vector<std::pair<int, size_t>> stack{{start, 0}};
            state[start] = 1;
            while (!stack.empty()) {
                auto& [node, next] = stack.back();
                if (next < out[node].size()) {
                    int t = out[node][next++];
                    if (state[t] == 1) back.insert({node, t});
                    else if (state[t] == 0) {
                        state[t] = 1;
                        stack.push_back({t, 0});
                    }
                } else {
                    state[node] = 2;
                    stack.pop_back();
                }
            }
        }
        arrows.erase(std::remove_if(arrows.begin(), arrows.end(),
                                    [&](const std::pair<int, int>& a) { return back.count(a) > 0; }),
                     arrows.end());
    }

    std::vector<int> layered, free;
    for (int i : byPosition) (linked[i] ? layered : free).push_back(i);
    float extentMain = 0, extentCross = 0;
    if (!layered.empty()) {
        mermaid::Diagram diagram;
        diagram.direction = leftToRight ? mermaid::Direction::LeftToRight : mermaid::Direction::TopToBottom;
        std::vector<int> localOf(count, -1);
        std::vector<mermaid::Size> boxSizes;
        for (int i : layered) {
            localOf[i] = (int)diagram.nodes.size();
            mermaid::Node node;
            node.id = "n" + std::to_string(i);
            diagram.nodes.push_back(node);
            boxSizes.push_back({sizes[i].first, sizes[i].second});
        }
        for (auto& a : arrows) {
            if (localOf[a.first] < 0 || localOf[a.second] < 0) continue;
            mermaid::Edge edge;
            edge.from = (size_t)localOf[a.first];
            edge.to = (size_t)localOf[a.second];
            diagram.edges.push_back(edge);
        }
        mermaid::Layout result = mermaid::layout(diagram, boxSizes, 60.0f, 110.0f);
        if (result.nodes.size() == layered.size()) {
            float minX = 1e30f, minY = 1e30f;
            for (const auto& r : result.nodes) {
                minX = std::min(minX, r.left);
                minY = std::min(minY, r.top);
            }
            for (size_t k = 0; k < layered.size(); ++k) {
                const auto& r = result.nodes[k];
                place[layered[k]] = {r.left - minX, r.top - minY};
                extentMain = std::max(extentMain, leftToRight ? r.right - minX : r.bottom - minY);
                extentCross = std::max(extentCross, leftToRight ? r.bottom - minY : r.right - minX);
            }
        } else {
            free.insert(free.begin(), layered.begin(), layered.end());
            layered.clear();
        }
    }
    if (!free.empty()) {
        // A grid of the free boxes beside the layered ones (below them when
        // laying out left to right, to their right otherwise)
        int columns = std::max(1, (int)std::ceil(std::sqrt((float)free.size())));
        float gap = 40.0f;
        float originX = layered.empty() ? 0 : (leftToRight ? 0 : extentCross + 120.0f);
        float originY = layered.empty() ? 0 : (leftToRight ? extentCross + 120.0f : 0);
        float rowY = originY, rowH = 0, x = originX;
        for (size_t k = 0; k < free.size(); ++k) {
            if (k > 0 && k % columns == 0) {
                rowY += rowH + gap;
                rowH = 0;
                x = originX;
            }
            int i = free[k];
            place[i] = {x, rowY};
            x += sizes[i].first + gap;
            rowH = std::max(rowH, sizes[i].second);
        }
    }
    return place;
}

}  // namespace

void tidy(Sheet& sheet, const std::vector<int>& subset, bool leftToRight) {
    const int count = (int)sheet.nodes.size();
    if (count == 0) return;
    std::vector<bool> moving(count, subset.empty());
    for (int i : subset) {
        if (i >= 0 && i < count) moving[i] = true;
    }
    // A moving group takes everything inside it along
    for (int i = 0; i < count; ++i) {
        if (!moving[i] || sheet.nodes[i].type != NodeType::Group) continue;
        for (int j = 0; j < count; ++j) {
            if (insideGroup(sheet.nodes[i], sheet.nodes[j])) moving[j] = true;
        }
    }
    // Top-level groups: groups inside no other group
    std::vector<int> topGroup(count, -1);
    for (int i = 0; i < count; ++i) {
        if (!moving[i]) continue;
        int g = groupOf(sheet, i);
        int top = -1;
        while (g >= 0) {
            top = g;
            g = groupOf(sheet, g);
        }
        topGroup[i] = top;
    }
    std::unordered_map<std::string, int> indexOf;
    for (int i = 0; i < count; ++i) indexOf[sheet.nodes[i].id] = i;

    auto arrowsAmong = [&](const std::vector<int>& members, const std::vector<int>& unitOf) {
        std::vector<std::pair<int, int>> arrows;
        for (const Edge& e : sheet.edges) {
            auto f = indexOf.find(e.fromNode), t = indexOf.find(e.toNode);
            if (f == indexOf.end() || t == indexOf.end()) continue;
            int a = unitOf[f->second], b = unitOf[t->second];
            if (a < 0 || b < 0 || a == b) continue;
            if (e.fromArrow && !e.toArrow) std::swap(a, b);
            arrows.emplace_back(a, b);
        }
        (void)members;
        return arrows;
    };
    auto moveBy = [&](int i, float dx, float dy) {
        sheet.nodes[i].x += dx;
        sheet.nodes[i].y += dy;
    };

    // Inside each moving top-level group: its own cards tidy first (nested
    // groups move with their contents as single blocks), then the group
    // fits around them
    const float padX = 40.0f, padTop = 60.0f, padBottom = 40.0f;
    std::vector<int> groups;
    for (int i = 0; i < count; ++i) {
        if (moving[i] && sheet.nodes[i].type == NodeType::Group && topGroup[i] < 0) groups.push_back(i);
    }
    for (int g : groups) {
        std::vector<int> inner;  // direct members of g
        for (int j = 0; j < count; ++j) {
            if (j != g && groupOf(sheet, j) == g) inner.push_back(j);
        }
        if (inner.empty()) continue;
        std::vector<int> unitOf(count, -1);
        std::vector<std::pair<float, float>> sizes, now;
        for (int k = 0; k < (int)inner.size(); ++k) {
            int j = inner[k];
            unitOf[j] = k;
            if (sheet.nodes[j].type == NodeType::Group) {
                for (int m = 0; m < count; ++m) {
                    if (insideGroup(sheet.nodes[j], sheet.nodes[m])) unitOf[m] = k;
                }
            }
            sizes.emplace_back(sheet.nodes[j].width, sheet.nodes[j].height);
            now.emplace_back(sheet.nodes[j].x, sheet.nodes[j].y);
        }
        auto places = layoutBoxes(sizes, arrowsAmong(inner, unitOf), now, leftToRight);
        // Contents of nested groups move with them
        std::vector<std::vector<int>> carried(inner.size());
        for (int k = 0; k < (int)inner.size(); ++k) {
            int j = inner[k];
            if (sheet.nodes[j].type != NodeType::Group) continue;
            for (int m = 0; m < count; ++m) {
                if (m != j && insideGroup(sheet.nodes[j], sheet.nodes[m])) carried[k].push_back(m);
            }
        }
        float originX = sheet.nodes[g].x + padX, originY = sheet.nodes[g].y + padTop;
        float right = originX, bottom = originY;
        for (int k = 0; k < (int)inner.size(); ++k) {
            int j = inner[k];
            float dx = originX + places[k].first - sheet.nodes[j].x;
            float dy = originY + places[k].second - sheet.nodes[j].y;
            moveBy(j, dx, dy);
            for (int m : carried[k]) moveBy(m, dx, dy);
            right = std::max(right, sheet.nodes[j].x + sheet.nodes[j].width);
            bottom = std::max(bottom, sheet.nodes[j].y + sheet.nodes[j].height);
        }
        sheet.nodes[g].width = std::max(right + padX - sheet.nodes[g].x, 200.0f);
        sheet.nodes[g].height = std::max(bottom + padBottom - sheet.nodes[g].y, 120.0f);
    }

    // The top level: groups as blocks, free cards as themselves
    std::vector<Unit> units;
    std::vector<int> unitOf(count, -1);
    for (int i = 0; i < count; ++i) {
        if (!moving[i] || topGroup[i] >= 0) continue;
        Unit unit;
        unit.members.push_back(i);
        unit.group = sheet.nodes[i].type == NodeType::Group ? i : -1;
        unit.x = sheet.nodes[i].x;
        unit.y = sheet.nodes[i].y;
        unit.width = sheet.nodes[i].width;
        unit.height = sheet.nodes[i].height;
        unitOf[i] = (int)units.size();
        units.push_back(unit);
    }
    for (int i = 0; i < count; ++i) {
        if (moving[i] && topGroup[i] >= 0 && unitOf[topGroup[i]] >= 0) {
            int u = unitOf[topGroup[i]];
            unitOf[i] = u;
            units[u].members.push_back(i);
        }
    }
    if (units.empty()) return;
    float originX = 1e30f, originY = 1e30f;
    std::vector<std::pair<float, float>> sizes, now;
    for (const Unit& u : units) {
        originX = std::min(originX, u.x);
        originY = std::min(originY, u.y);
        sizes.emplace_back(u.width, u.height);
        now.emplace_back(u.x, u.y);
    }
    std::vector<int> all;
    auto places = layoutBoxes(sizes, arrowsAmong(all, unitOf), now, leftToRight);
    for (size_t k = 0; k < units.size(); ++k) {
        float dx = originX + places[k].first - units[k].x;
        float dy = originY + places[k].second - units[k].y;
        for (int m : units[k].members) moveBy(m, dx, dy);
    }
}

std::vector<std::string> noteLinks(std::string_view markdown) {
    std::vector<std::string> links;
    std::set<std::string> seen;
    auto add = [&](std::string target) {
        size_t hash = target.find('#');
        if (hash != std::string::npos) target = target.substr(0, hash);
        std::string_view trimmed = trimView(target);
        if (trimmed.empty()) return;
        std::string key(trimmed);
        if (seen.insert(lowerAscii(key)).second) links.push_back(key);
    };
    bool inFence = false;
    char fenceChar = 0;
    int fenceLen = 0;
    for (const Line& line : splitLines(noteBody(markdown))) {
        char marker = 0;
        int run = fenceRun(line.text, marker);
        if (inFence) {
            if (run >= fenceLen && marker == fenceChar) inFence = false;
            continue;
        }
        if (run) {
            inFence = true;
            fenceChar = marker;
            fenceLen = run;
            continue;
        }
        std::string_view t = line.text;
        for (size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '`') {
                size_t close = t.find('`', i + 1);
                if (close == std::string_view::npos) break;
                i = close;
                continue;
            }
            if (t.substr(i, 2) == "[[") {
                size_t close = t.find("]]", i + 2);
                if (close == std::string_view::npos) break;
                std::string_view inner = t.substr(i + 2, close - i - 2);
                size_t bar = inner.find('|');
                if (bar != std::string_view::npos) inner = inner.substr(0, bar);
                add(std::string(inner));
                i = close + 1;
                continue;
            }
            if (t[i] == ']' && i + 1 < t.size() && t[i + 1] == '(') {
                size_t close = t.find(')', i + 2);
                if (close == std::string_view::npos) continue;
                std::string_view dest = trimView(t.substr(i + 2, close - i - 2));
                if (!dest.empty() && dest.front() == '<') {
                    size_t gt = dest.find('>');
                    dest = dest.substr(1, gt == std::string_view::npos ? std::string_view::npos : gt - 1);
                } else {
                    size_t space = dest.find(' ');
                    if (space != std::string_view::npos) dest = dest.substr(0, space);
                }
                if (dest.empty() || dest.front() == '#' || hasScheme(dest)) continue;
                std::string decoded = percentDecode(dest);
                size_t cut = decoded.find_first_of("#?");
                std::string path = cut == std::string::npos ? decoded : decoded.substr(0, cut);
                if (endsWithNoCase(path, ".md") || endsWithNoCase(path, ".markdown")) add(path);
                i = close;
            }
        }
    }
    return links;
}

std::string_view noteBody(std::string_view markdown) {
    size_t bodyStart = 0;
    if (!frontmatterBlockEnd(markdown, bodyStart).empty()) return markdown.substr(bodyStart);
    if (markdown.substr(0, 3) == "\xEF\xBB\xBF") return markdown.substr(3);
    return markdown;
}

std::string frontmatterValue(std::string_view markdown, std::string_view key) {
    size_t bodyStart = 0;
    if (frontmatterBlockEnd(markdown, bodyStart).empty()) return {};
    fm::Document doc = fm::parse(std::string(markdown.substr(0, bodyStart)));
    const fm::Property* p = fm::find(doc, std::string(key));
    if (!p || p->kind == fm::Kind::Structured) return {};
    if (p->kind == fm::Kind::List) {
        std::string joined;
        for (const auto& item : p->items) joined += (joined.empty() ? "" : ", ") + item;
        return joined;
    }
    return p->text;
}

std::string noteTitle(std::string_view markdown, std::string_view fallback) {
    std::string fromFront = frontmatterValue(markdown, "title");
    if (!fromFront.empty()) return fromFront;
    std::string firstText;
    bool inFence = false;
    char fenceChar = 0;
    int fenceLen = 0;
    for (const Line& line : splitLines(noteBody(markdown))) {
        char marker = 0;
        int run = fenceRun(line.text, marker);
        if (inFence) {
            if (run >= fenceLen && marker == fenceChar) inFence = false;
            continue;
        }
        if (run) {
            inFence = true;
            fenceChar = marker;
            fenceLen = run;
            continue;
        }
        int level = 0;
        std::string text = lineText(line.text, &level);
        if (level > 0 && !text.empty()) return clipUtf8(text, 120);
        if (firstText.empty() && !text.empty() && trimView(line.text).substr(0, 3) != "---") firstText = text;
    }
    if (!firstText.empty()) return clipUtf8(firstText, 120);
    return std::string(fallback);
}

std::string noteFirstLine(std::string_view markdown) {
    std::string fromFront = frontmatterValue(markdown, "title");
    if (!fromFront.empty()) return fromFront;
    bool inFence = false;
    char fenceChar = 0;
    int fenceLen = 0;
    for (const Line& line : splitLines(noteBody(markdown))) {
        char marker = 0;
        int run = fenceRun(line.text, marker);
        if (inFence) {
            if (run >= fenceLen && marker == fenceChar) inFence = false;
            continue;
        }
        if (run) {
            inFence = true;
            fenceChar = marker;
            fenceLen = run;
            continue;
        }
        if (trimView(line.text).substr(0, 3) == "---") continue;
        std::string text = lineText(line.text);
        if (!text.empty()) return clipUtf8(text, 120);
    }
    return {};
}

void noteTasks(std::string_view markdown, int& done, int& total) {
    done = total = 0;
    bool inFence = false;
    char fenceChar = 0;
    int fenceLen = 0;
    for (const Line& line : splitLines(noteBody(markdown))) {
        char marker = 0;
        int run = fenceRun(line.text, marker);
        if (inFence) {
            if (run >= fenceLen && marker == fenceChar) inFence = false;
            continue;
        }
        if (run) {
            inFence = true;
            fenceChar = marker;
            fenceLen = run;
            continue;
        }
        std::string_view t = trimView(line.text);
        while (!t.empty() && t.front() == '>') t = trimView(t.substr(1));
        size_t at = 0;
        if (t.size() >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') {
            at = 2;
        } else {
            size_t digits = 0;
            while (digits < t.size() && t[digits] >= '0' && t[digits] <= '9') ++digits;
            if (digits == 0 || digits + 1 >= t.size() || (t[digits] != '.' && t[digits] != ')') ||
                t[digits + 1] != ' ') {
                continue;
            }
            at = digits + 2;
        }
        if (t.size() >= at + 3 && t[at] == '[' && t[at + 2] == ']' &&
            (t.size() == at + 3 || t[at + 3] == ' ')) {
            char box = t[at + 1];
            if (box == ' ') ++total;
            else if (box == 'x' || box == 'X') { ++total; ++done; }
        }
    }
}

int noteWords(std::string_view markdown) {
    int words = 0;
    bool inWord = false;
    std::string_view body = noteBody(markdown);
    for (size_t i = 0; i < body.size(); ++i) {
        unsigned char c = (unsigned char)body[i];
        // CJK ideographs and kana count one each (lead bytes E3..E9)
        if (c >= 0xE3 && c <= 0xE9) {
            ++words;
            inWord = false;
            i += 2;
            continue;
        }
        bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        bool mark = c == '#' || c == '*' || c == '-' || c == '>' || c == '|' || c == '`' || c == '_';
        if (space || mark) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            ++words;
        }
    }
    return words;
}

bool setFrontmatterValue(std::string& markdown, std::string_view key, std::string_view value) {
    std::string scalar = yamlScalar(value);
    fm::Document doc = fm::parse(markdown);
    if (!doc.present) {
        size_t begin = markdown.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
        std::string eol = markdown.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        std::string block = "---" + eol + std::string(key) + ": " + scalar + eol + "---" + eol;
        if (begin < markdown.size() && markdown[begin] != '\n' && markdown[begin] != '\r') block += eol;
        markdown.insert(begin, block);
        return true;
    }
    if (!doc.safeToWrite) return false;
    if (const fm::Property* p = fm::find(doc, std::string(key))) {
        if (!p->writable) return false;
        markdown.replace(p->valueStart, p->valueEnd - p->valueStart, scalar);
        return true;
    }
    markdown.insert(doc.closing, std::string(key) + ": " + scalar + doc.eol);
    return true;
}

std::wstring safeFileStem(const std::wstring& title, size_t limit) {
    std::wstring out;
    bool space = false;
    for (wchar_t c : title) {
        bool bad = c < 0x20 || c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' ||
                   c == L'\\' || c == L'|' || c == L'?' || c == L'*' || c == L'#' || c == L'^' ||
                   c == L'[' || c == L']';
        if (bad || c == L' ' || c == L'\t') {
            space = !out.empty();
            continue;
        }
        if (space) out += L' ';
        space = false;
        out += c;
        if (out.size() >= limit) break;
    }
    while (!out.empty() && (out.back() == L'.' || out.back() == L' ')) out.pop_back();
    while (!out.empty() && out.front() == L'.') out.erase(out.begin());
    if (out.empty()) return L"Untitled";
    std::wstring upper;
    for (wchar_t c : out) upper += (wchar_t)towupper(c);
    static const wchar_t* reserved[] = {L"CON", L"PRN", L"AUX", L"NUL", L"COM1", L"COM2", L"COM3",
                                        L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1",
                                        L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8",
                                        L"LPT9"};
    for (const wchar_t* r : reserved) {
        if (upper == r) return out + L"_";
    }
    return out;
}

std::string shiftHeadings(std::string_view markdown, int by) {
    std::string out;
    bool inFence = false;
    char fenceChar = 0;
    int fenceLen = 0;
    std::vector<Line> lines = splitLines(markdown);
    for (size_t li = 0; li < lines.size(); ++li) {
        const Line& line = lines[li];
        std::string text(line.text);
        char marker = 0;
        int run = fenceRun(line.text, marker);
        if (inFence) {
            if (run >= fenceLen && marker == fenceChar) inFence = false;
        } else if (run) {
            inFence = true;
            fenceChar = marker;
            fenceLen = run;
        } else {
            size_t indent = text.find_first_not_of(' ');
            if (indent != std::string::npos && indent < 4) {
                size_t hashes = 0;
                while (indent + hashes < text.size() && text[indent + hashes] == '#') ++hashes;
                if (hashes >= 1 && hashes <= 6 &&
                    (indent + hashes == text.size() || text[indent + hashes] == ' ')) {
                    int level = std::clamp((int)hashes + by, 1, 6);
                    text = std::string(level, '#') + text.substr(indent + hashes);
                }
            }
        }
        out += text;
        if (li + 1 < lines.size()) out += '\n';
    }
    return out;
}

// --- Mermaid bridge ------------------------------------------------------

bool importFlowchart(std::string_view text, FlowImport& out, float cardWidth, float cardHeight) {
    out = FlowImport{};
    std::string_view source = text;
    // A ```mermaid fence inside the text wins
    std::vector<Line> lines = splitLines(text);
    for (size_t i = 0; i < lines.size(); ++i) {
        char marker = 0;
        int run = fenceRun(lines[i].text, marker);
        if (!run) continue;
        std::string info = lowerAscii(trimView(trimView(lines[i].text).substr(run)));
        if (info.rfind("mermaid", 0) != 0) continue;
        size_t start = (i + 1 < lines.size()) ? lines[i + 1].start : text.size();
        size_t end = text.size();
        for (size_t j = i + 1; j < lines.size(); ++j) {
            char m2 = 0;
            if (fenceRun(lines[j].text, m2) >= run && m2 == marker) {
                end = lines[j].start;
                break;
            }
        }
        source = text.substr(start, end - start);
        break;
    }
    std::string_view first;
    for (const Line& line : splitLines(source)) {
        std::string_view t = trimView(line.text);
        if (t.empty() || t.substr(0, 2) == "%%") continue;
        first = t;
        break;
    }
    std::string head = lowerAscii(first.substr(0, 9));
    if (head.rfind("flowchart", 0) != 0 && head.rfind("graph", 0) != 0) return false;
    mermaid::ParseResult parsed = mermaid::parse(source);
    if (!parsed.success || parsed.diagram.nodes.empty()) return false;
    const mermaid::Diagram& d = parsed.diagram;
    out.leftToRight = d.direction == mermaid::Direction::LeftToRight ||
                      d.direction == mermaid::Direction::RightToLeft;
    std::vector<mermaid::Size> sizes;
    for (const auto& node : d.nodes) {
        FlowImport::Card card;
        card.id = node.id;
        card.label = node.label.empty() ? node.id : node.label;
        // Mermaid's <br/> line breaks become real ones
        for (const char* br : {"<br/>", "<br>", "<br />"}) {
            size_t at;
            while ((at = card.label.find(br)) != std::string::npos)
                card.label.replace(at, std::strlen(br), "\n");
        }
        card.width = cardWidth;
        card.height = cardHeight;
        sizes.push_back({cardWidth, cardHeight});
        out.cards.push_back(card);
    }
    for (const auto& edge : d.edges) {
        if (edge.from >= d.nodes.size() || edge.to >= d.nodes.size()) continue;
        out.links.push_back({(int)edge.from, (int)edge.to, edge.label, edge.directed});
    }
    // Subgraphs become groups. The parser keeps the nodes first defined in
    // a subgraph; a node defined earlier and named inside one belongs to it
    // too, so the subgraph blocks are read again for the ids they mention.
    std::vector<std::set<int>> members(d.subgraphs.size());
    for (size_t s = 0; s < d.subgraphs.size(); ++s) {
        for (size_t n : d.subgraphs[s].nodes) {
            if (n < out.cards.size()) members[s].insert((int)n);
        }
    }
    {
        std::unordered_map<std::string, int> idIndex;
        for (size_t i = 0; i < d.nodes.size(); ++i) idIndex[d.nodes[i].id] = (int)i;
        std::vector<size_t> open;
        size_t nextSubgraph = 0;
        for (const Line& line : splitLines(source)) {
            std::string t(trimView(line.text));
            if (t.rfind("subgraph", 0) == 0 && (t.size() == 8 || t[8] == ' ')) {
                if (nextSubgraph < d.subgraphs.size()) open.push_back(nextSubgraph++);
                continue;
            }
            if (t == "end") {
                if (!open.empty()) open.pop_back();
                continue;
            }
            if (open.empty()) continue;
            // Labels and edge text are not ids
            std::string bare;
            int depth = 0;
            bool quoted = false, piped = false;
            for (char c : t) {
                if (c == '"') { quoted = !quoted; continue; }
                if (quoted) continue;
                if (c == '|') { piped = !piped; continue; }
                if (piped) continue;
                if (c == '[' || c == '(' || c == '{') { ++depth; continue; }
                if (c == ']' || c == ')' || c == '}') { depth = std::max(0, depth - 1); continue; }
                if (depth == 0) bare += c;
            }
            std::string token;
            auto take = [&] {
                auto it = idIndex.find(token);
                if (it != idIndex.end()) members[open.back()].insert(it->second);
                token.clear();
            };
            for (char c : bare) {
                if (std::isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80) token += c;
                else take();
            }
            take();
        }
    }
    for (size_t s = 0; s < d.subgraphs.size(); ++s) {
        FlowImport::Group group;
        group.label = d.subgraphs[s].label.empty() ? d.subgraphs[s].id : d.subgraphs[s].label;
        group.cards.assign(members[s].begin(), members[s].end());
        if (!group.cards.empty()) out.groups.push_back(group);
    }
    mermaid::Layout layout = mermaid::layout(d, sizes, 60.0f, 110.0f);
    if (layout.nodes.size() == out.cards.size()) {
        float minX = 1e30f, minY = 1e30f;
        for (const auto& r : layout.nodes) {
            minX = std::min(minX, r.left);
            minY = std::min(minY, r.top);
        }
        for (size_t i = 0; i < out.cards.size(); ++i) {
            out.cards[i].x = layout.nodes[i].left - minX;
            out.cards[i].y = layout.nodes[i].top - minY;
        }
    } else {
        auto places = gridPlaces((int)out.cards.size(), 0, 0, cardWidth, cardHeight);
        for (size_t i = 0; i < out.cards.size(); ++i) {
            out.cards[i].x = places[i].first;
            out.cards[i].y = places[i].second;
        }
    }
    return true;
}

std::string exportFlowchart(const Sheet& sheet, const std::function<std::string(const Node&)>& titleOf,
                            const std::vector<int>& subset) {
    std::vector<int> cards = readingOrder(sheet, subset);
    std::set<int> included(cards.begin(), cards.end());
    std::map<std::string, std::string> idOf;
    std::string out = "flowchart LR\n";
    auto label = [&](const Node& n) {
        std::string text = titleOf ? titleOf(n) : std::string();
        if (text.empty()) text = n.type == NodeType::Link ? n.url : n.label;
        if (text.empty()) text = noteTitle(n.text, "\xE2\x80\xA6");
        std::string escaped;
        for (char c : text) {
            if (c == '"') escaped += "#quot;";
            else if (c == '\n') escaped += "<br/>";
            else if (c != '\r') escaped += c;
        }
        return escaped;
    };
    int counter = 0;
    for (int i : cards) idOf[sheet.nodes[i].id] = "n" + std::to_string(++counter);
    std::set<int> grouped;
    int groupCounter = 0;
    for (int g = 0; g < (int)sheet.nodes.size(); ++g) {
        if (sheet.nodes[g].type != NodeType::Group) continue;
        std::vector<int> members;
        for (int i : cards) {
            if (groupOf(sheet, i) == g) members.push_back(i);
        }
        if (members.empty()) continue;
        std::string groupLabel = sheet.nodes[g].label.empty() ? std::string(" ") : sheet.nodes[g].label;
        for (auto& c : groupLabel) {
            if (c == '"') c = '\'';
        }
        out += "    subgraph g" + std::to_string(++groupCounter) + " [\"" + groupLabel + "\"]\n";
        for (int i : members) {
            out += "        " + idOf[sheet.nodes[i].id] + "[\"" + label(sheet.nodes[i]) + "\"]\n";
            grouped.insert(i);
        }
        out += "    end\n";
    }
    for (int i : cards) {
        if (grouped.count(i)) continue;
        out += "    " + idOf[sheet.nodes[i].id] + "[\"" + label(sheet.nodes[i]) + "\"]\n";
    }
    for (const Edge& e : sheet.edges) {
        auto f = idOf.find(e.fromNode), t = idOf.find(e.toNode);
        if (f == idOf.end() || t == idOf.end()) continue;
        std::string arrow = e.fromArrow && e.toArrow ? "<-->" : (e.fromArrow || e.toArrow) ? "-->" : "---";
        std::string from = f->second, to = t->second;
        if (e.fromArrow && !e.toArrow) std::swap(from, to);
        std::string text = e.label;
        for (auto& c : text) {
            if (c == '|' || c == '"') c = '\'';
            if (c == '\n' || c == '\r') c = ' ';
        }
        out += "    " + from + " " + arrow + (text.empty() ? "" : "|" + text + "|") + " " + to + "\n";
    }
    (void)included;
    return out;
}

// --- Folds, lanes, chains --------------------------------------------------

namespace {

// Each node's arrows out (and in), as node indexes
std::vector<std::vector<int>> arrowsOut(const Sheet& sheet, std::vector<std::vector<int>>* in = nullptr) {
    std::unordered_map<std::string, int> index;
    for (int i = 0; i < (int)sheet.nodes.size(); ++i) index[sheet.nodes[i].id] = i;
    std::vector<std::vector<int>> out(sheet.nodes.size());
    if (in) in->assign(sheet.nodes.size(), {});
    for (const Edge& e : sheet.edges) {
        auto f = index.find(e.fromNode), t = index.find(e.toNode);
        if (f == index.end() || t == index.end() || f->second == t->second) continue;
        out[f->second].push_back(t->second);
        if (in) (*in)[t->second].push_back(f->second);
    }
    return out;
}

// Marks the cards after `from` along the arrows, never going back through it
void collectAfter(const std::vector<std::vector<int>>& out, int from, std::vector<char>& seen) {
    std::vector<int> stack(out[from].begin(), out[from].end());
    while (!stack.empty()) {
        int i = stack.back();
        stack.pop_back();
        if (i == from || seen[i]) continue;
        seen[i] = 1;
        stack.insert(stack.end(), out[i].begin(), out[i].end());
    }
}

}  // namespace

std::vector<char> foldedAway(const Sheet& sheet) {
    std::vector<char> hidden(sheet.nodes.size(), 0);
    if (std::none_of(sheet.nodes.begin(), sheet.nodes.end(), [](const Node& n) { return n.folded; })) return hidden;
    std::vector<std::vector<int>> in;
    std::vector<std::vector<int>> out = arrowsOut(sheet, &in);
    for (int i = 0; i < (int)sheet.nodes.size(); ++i) {
        if (sheet.nodes[i].folded) collectAfter(out, i, hidden);
    }
    // A card that something shown and unfolded points at stays in sight
    for (bool changed = true; changed;) {
        changed = false;
        for (int i = 0; i < (int)hidden.size(); ++i) {
            if (!hidden[i]) continue;
            for (int from : in[i]) {
                if (!hidden[from] && !sheet.nodes[from].folded) {
                    hidden[i] = 0;
                    changed = true;
                    break;
                }
            }
        }
    }
    return hidden;
}

std::vector<int> foldedBehind(const Sheet& sheet, int folded, const std::vector<char>& hidden) {
    std::vector<int> out;
    if (folded < 0 || folded >= (int)sheet.nodes.size() || !sheet.nodes[folded].folded) return out;
    std::vector<char> after(sheet.nodes.size(), 0);
    collectAfter(arrowsOut(sheet), folded, after);
    for (size_t i = 0; i < after.size() && i < hidden.size(); ++i) {
        if (after[i] && hidden[i]) out.push_back((int)i);
    }
    return out;
}

int foldedCount(const Sheet& sheet, int folded, const std::vector<char>& hidden) {
    return (int)foldedBehind(sheet, folded, hidden).size();
}

std::vector<int> laneCards(const Sheet& sheet, int lane) {
    std::vector<int> cards;
    for (int i = 0; i < (int)sheet.nodes.size(); ++i) {
        if (sheet.nodes[i].type != NodeType::Group && groupOf(sheet, i) == lane) cards.push_back(i);
    }
    std::sort(cards.begin(), cards.end(), [&](int a, int b) {
        const Node &na = sheet.nodes[a], &nb = sheet.nodes[b];
        return na.y != nb.y ? na.y < nb.y : na.x < nb.x;
    });
    return cards;
}

void stackLane(Sheet& sheet, int lane) {
    if (lane < 0 || lane >= (int)sheet.nodes.size() || sheet.nodes[lane].type != NodeType::Group) return;
    std::vector<int> cards = laneCards(sheet, lane);
    Node& g = sheet.nodes[lane];
    // Under the label, 40 in from the sides, 20 apart; the lane grows to hold them
    float y = g.y + 80.0f;
    for (int i : cards) {
        Node& n = sheet.nodes[i];
        n.x = g.x + 40.0f;
        n.y = y;
        n.width = std::min(n.width, std::max(80.0f, g.width - 80.0f));
        y += n.height + 20.0f;
    }
    float needed = y - 20.0f + 40.0f - g.y;
    if (needed > g.height) g.height = std::ceil(needed / 20.0f) * 20.0f;
}

std::vector<int> rowOrder(const Sheet& sheet, std::vector<int> cards) {
    std::sort(cards.begin(), cards.end(), [&](int a, int b) {
        const Node &na = sheet.nodes[a], &nb = sheet.nodes[b];
        return na.y + na.height * 0.5f < nb.y + nb.height * 0.5f;
    });
    // A card whose middle lies within the first card of a row joins the row
    std::vector<std::vector<int>> rows;
    for (int i : cards) {
        const Node& n = sheet.nodes[i];
        float middle = n.y + n.height * 0.5f;
        if (!rows.empty()) {
            const Node& first = sheet.nodes[rows.back().front()];
            if (middle >= first.y && middle <= first.y + first.height) {
                rows.back().push_back(i);
                continue;
            }
        }
        rows.push_back({i});
    }
    std::vector<int> out;
    for (auto& row : rows) {
        std::sort(row.begin(), row.end(), [&](int a, int b) { return sheet.nodes[a].x < sheet.nodes[b].x; });
        out.insert(out.end(), row.begin(), row.end());
    }
    return out;
}

// --- Exploding a document --------------------------------------------------

Exploded explodeDocument(std::string_view markdown, std::string_view fallbackTitle) {
    Exploded out;
    std::string_view body = noteBody(markdown);
    struct Head {
        size_t start, after;
        int level;
        std::string text;
    };
    std::vector<Head> heads;
    bool inFence = false;
    char fenceChar = 0;
    int fenceLen = 0;
    std::vector<Line> lines = splitLines(body);
    for (size_t k = 0; k < lines.size(); ++k) {
        const Line& line = lines[k];
        char marker = 0;
        int run = fenceRun(line.text, marker);
        if (inFence) {
            if (run >= fenceLen && marker == fenceChar) inFence = false;
            continue;
        }
        if (run) {
            inFence = true;
            fenceChar = marker;
            fenceLen = run;
            continue;
        }
        int level = 0;
        std::string text = lineText(line.text, &level);
        if (level > 0 && !text.empty())
            heads.push_back({line.start, k + 1 < lines.size() ? lines[k + 1].start : body.size(), level, text});
    }
    int firstLevel = 0, minLevel = 7;
    bool second = false;
    for (const Head& h : heads) {
        firstLevel += h.level == 1;
        second = second || h.level == 2;
        minLevel = std::min(minLevel, h.level);
    }
    // ## sections are the cards when there are any, else the top headings;
    // two or more # headings over them are groups, one alone is the title
    int cardLevel = second ? 2 : minLevel;
    bool grouped = second && firstLevel >= 2;
    out.title = frontmatterValue(markdown, "title");
    if (out.title.empty() && firstLevel == 1 && !heads.empty() && heads[0].level == 1) out.title = heads[0].text;
    if (out.title.empty()) out.title = std::string(fallbackTitle);

    int group = -1;
    size_t from = 0;
    bool fromHeading = false;
    std::string partTitle = out.title;
    auto cut = [&](size_t end) {
        std::string_view piece = body.substr(from, end - from);
        if (piece.find_first_not_of(" \t\r\n") == std::string_view::npos) return;
        std::string text = shiftHeadings(piece, 1 - cardLevel);
        if (!fromHeading) {
            // Text before any card heading reads under its own title
            size_t first = text.find_first_not_of("\r\n");
            text = "# " + partTitle + "\n\n" + (first == std::string::npos ? std::string() : text.substr(first));
        }
        out.parts.push_back({partTitle, text, group});
    };
    for (const Head& h : heads) {
        if (h.level > cardLevel) continue;  // stays inside its card
        cut(h.start);
        if (h.level == cardLevel) {
            from = h.start;
            fromHeading = true;
        } else {
            if (grouped) {
                out.groups.push_back(h.text);
                group = (int)out.groups.size() - 1;
            }
            from = h.after;
            fromHeading = false;
        }
        partTitle = h.text;
    }
    from = std::min(from, body.size());
    cut(body.size());
    return out;
}

void layoutExploded(const Exploded& doc, float x, float y, std::vector<Box>& cards, std::vector<Box>& groups) {
    const float w = 360, h = 240;
    cards.assign(doc.parts.size(), Box{});
    groups.assign(doc.groups.size(), Box{});
    if (doc.groups.empty()) {
        // Rows of four, with room for the arrows between them
        for (size_t i = 0; i < doc.parts.size(); ++i) {
            float cx = x + (float)(i % 4) * (w + 80), cy = y + (float)(i / 4) * (h + 80);
            cards[i] = {cx, cy, cx + w, cy + h};
        }
        return;
    }
    // Each group a column of its cards; what comes before the first group
    // stands in a column of its own on the left
    std::vector<std::vector<size_t>> columns(doc.groups.size() + 1);
    for (size_t i = 0; i < doc.parts.size(); ++i) columns[(size_t)(doc.parts[i].group + 1)].push_back(i);
    float cx = x;
    for (size_t c = 0; c < columns.size(); ++c) {
        if (c == 0 && columns[c].empty()) continue;
        bool inGroup = c > 0;
        float left = cx + (inGroup ? 40.0f : 0.0f), top = y + (inGroup ? 80.0f : 0.0f);
        for (size_t k = 0; k < columns[c].size(); ++k) {
            float cy = top + (float)k * (h + 60);
            cards[columns[c][k]] = {left, cy, left + w, cy + h};
        }
        if (inGroup) {
            float rows = (float)std::max<size_t>(1, columns[c].size());
            groups[c - 1] = {cx, y, cx + w + 80, top + rows * (h + 60) - 60 + 40};
            cx += w + 80 + 120;
        } else {
            cx += w + 120;
        }
    }
}

// --- Links in the front matter ---------------------------------------------

namespace {

// The lines of a block list under a key line, as [start, end) of the text
// past the key line; `indent` takes what the last item line starts with
size_t blockListEnd(const std::string& markdown, size_t keyLineEnd, size_t closing, std::string* indent) {
    size_t end = keyLineEnd;
    size_t scan = keyLineEnd;
    while (scan < closing) {
        size_t next = markdown.find('\n', scan);
        size_t lineEnd = next == std::string::npos ? markdown.size() : next;
        std::string_view line(markdown.data() + scan, lineEnd - scan);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        std::string_view t = trimView(line);
        size_t after = next == std::string::npos ? markdown.size() : next + 1;
        if (!t.empty() && t[0] != '#') {
            if (line[0] != ' ' && line[0] != '\t' && t.rfind("- ", 0) != 0) break;
            if (indent && t.rfind("- ", 0) == 0) *indent = std::string(line.substr(0, line.find('-')));
            end = after;
        }
        scan = after;
    }
    return end;
}

// Whether a list item's raw text (after "- ") holds the link
bool itemIs(std::string_view raw, std::string_view link) {
    size_t hash = raw.find(" #");
    std::string_view t = trimView(hash == std::string_view::npos ? raw : raw.substr(0, hash));
    if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front()) t = t.substr(1, t.size() - 2);
    return t == link;
}

}  // namespace

bool addFrontmatterLink(std::string& markdown, std::string_view key, std::string_view link) {
    std::string item = yamlScalar(link);
    fm::Document doc = fm::parse(markdown);
    if (!doc.present) {
        size_t begin = markdown.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
        std::string eol = markdown.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        std::string block = "---" + eol + std::string(key) + ":" + eol + "  - " + item + eol + "---" + eol;
        if (begin < markdown.size() && markdown[begin] != '\n' && markdown[begin] != '\r') block += eol;
        markdown.insert(begin, block);
        return true;
    }
    if (!doc.safeToWrite) return false;
    const fm::Property* p = fm::find(doc, std::string(key));
    if (!p) {
        markdown.insert(doc.closing, std::string(key) + ":" + doc.eol + "  - " + item + doc.eol);
        return true;
    }
    size_t keyLineEnd = markdown.find('\n', p->valueEnd);
    keyLineEnd = keyLineEnd == std::string::npos ? markdown.size() : keyLineEnd + 1;
    if (p->kind == fm::Kind::List) {
        if (std::find(p->items.begin(), p->items.end(), std::string(link)) != p->items.end()) return true;
        if (p->valueStart < p->valueEnd) {
            // A flow list: [a, b]
            std::string flow = "[";
            for (const std::string& existing : p->items) flow += yamlScalar(existing) + ", ";
            markdown.replace(p->valueStart, p->valueEnd - p->valueStart, flow + item + "]");
            return true;
        }
        std::string indent = "  ";
        size_t end = blockListEnd(markdown, keyLineEnd, doc.closing, &indent);
        markdown.insert(end, indent + "- " + item + doc.eol);
        return true;
    }
    // `key:` with nothing yet becomes the list
    if (p->writable && !p->nullValue && p->valueStart == p->valueEnd) {
        markdown.insert(keyLineEnd, "  - " + item + doc.eol);
        return true;
    }
    return false;
}

bool removeFrontmatterLink(std::string& markdown, std::string_view key, std::string_view link) {
    fm::Document doc = fm::parse(markdown);
    if (!doc.present || !doc.safeToWrite) return false;
    const fm::Property* p = fm::find(doc, std::string(key));
    if (!p || p->kind != fm::Kind::List) return false;
    if (std::find(p->items.begin(), p->items.end(), std::string(link)) == p->items.end()) return false;
    size_t keyLine = p->valueStart == 0 ? 0 : markdown.rfind('\n', p->valueStart - 1);
    keyLine = keyLine == std::string::npos ? 0 : keyLine + 1;
    size_t keyLineEnd = markdown.find('\n', p->valueEnd);
    keyLineEnd = keyLineEnd == std::string::npos ? markdown.size() : keyLineEnd + 1;
    bool last = p->items.size() == 1;
    if (p->valueStart < p->valueEnd) {
        if (last) {
            markdown.erase(keyLine, keyLineEnd - keyLine);
            return true;
        }
        std::string flow = "[";
        for (const std::string& existing : p->items) {
            if (existing == link) continue;
            flow += (flow.size() > 1 ? ", " : "") + yamlScalar(existing);
        }
        markdown.replace(p->valueStart, p->valueEnd - p->valueStart, flow + "]");
        return true;
    }
    // The item's line in the block list, then the key line when it was the last
    size_t end = blockListEnd(markdown, keyLineEnd, doc.closing, nullptr);
    for (size_t scan = keyLineEnd; scan < end;) {
        size_t next = markdown.find('\n', scan);
        size_t lineEnd = next == std::string::npos ? markdown.size() : next;
        size_t after = next == std::string::npos ? markdown.size() : next + 1;
        std::string_view t = trimView(std::string_view(markdown.data() + scan, lineEnd - scan));
        if (t.rfind("- ", 0) == 0 && itemIs(t.substr(2), link)) {
            markdown.erase(scan, after - scan);
            if (last) markdown.erase(keyLine, keyLineEnd - keyLine);
            return true;
        }
        scan = after;
    }
    return false;
}

}  // namespace sheet
