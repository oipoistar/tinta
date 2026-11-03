// Tinta's CSS (css.h): tokens and component values as CSS Syntax 3 reads
// them, and the table of properties.

// Themes favour small code over fast: a sheet is read once per load
#pragma optimize("s", on)

#include "css_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace css {
namespace {

bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool isHex(unsigned char c) { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool isNameStart(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}
bool isName(unsigned char c) { return isNameStart(c) || isDigit(c) || c == '-'; }
bool isNewline(unsigned char c) { return c == '\n' || c == '\r' || c == '\f'; }
bool isSpace(unsigned char c) { return c == ' ' || c == '\t' || isNewline(c); }
int hexValue(unsigned char c) {
    return isDigit(c) ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
}

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

struct Tokenizer {
    std::string_view s;
    size_t at = 0;

    bool eof(size_t k = 0) const { return at + k >= s.size(); }
    unsigned char peek(size_t k = 0) const { return eof(k) ? 0 : (unsigned char)s[at + k]; }
    bool validEscape(size_t k) const { return peek(k) == '\\' && !(at + k + 1 < s.size() && isNewline(peek(k + 1))); }
    bool startsIdent(size_t k) const {
        unsigned char c = peek(k);
        if (c == '-') return isNameStart(peek(k + 1)) || peek(k + 1) == '-' || validEscape(k + 1);
        if (isNameStart(c)) return true;
        return c == '\\' && validEscape(k);
    }
    bool startsNumber(size_t k) const {
        unsigned char c = peek(k);
        if (c == '+' || c == '-') {
            c = peek(k + 1);
            return isDigit(c) || (c == '.' && isDigit(peek(k + 2)));
        }
        if (c == '.') return isDigit(peek(k + 1));
        return isDigit(c);
    }
    void skipNewline() { at += peek() == '\r' && peek(1) == '\n' ? 2 : 1; }
    // After a backslash: a hex escape or the character itself
    void escape(std::string& out) {
        if (eof()) {
            appendUtf8(out, 0xFFFD);
            return;
        }
        if (isHex(peek())) {
            uint32_t code = 0;
            for (int n = 0; n < 6 && isHex(peek()); ++n, ++at) code = code * 16 + hexValue(peek());
            if (isSpace(peek())) skipNewline();
            if (code == 0 || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) code = 0xFFFD;
            appendUtf8(out, code);
            return;
        }
        out += s[at++];
        while (!eof() && (peek() & 0xC0) == 0x80) out += s[at++];
    }
    std::string name() {
        std::string out;
        while (!eof()) {
            if (isName(peek())) {
                size_t start = at;
                while (!eof() && isName(peek())) ++at;
                out.append(s.data() + start, at - start);
            } else if (validEscape(0)) {
                ++at;
                escape(out);
            } else {
                break;
            }
        }
        return out;
    }
    double number(bool& integer) {
        integer = true;
        double sign = 1, mantissa = 0;
        int scale = 0;
        if (peek() == '+' || peek() == '-') sign = s[at++] == '-' ? -1 : 1;
        while (isDigit(peek())) mantissa = mantissa * 10 + (s[at++] - '0');
        if (peek() == '.' && isDigit(peek(1))) {
            integer = false;
            ++at;
            while (isDigit(peek())) {
                mantissa = mantissa * 10 + (s[at++] - '0');
                --scale;
            }
        }
        unsigned char e = peek();
        if ((e == 'e' || e == 'E') &&
            (isDigit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && isDigit(peek(2))))) {
            integer = false;
            ++at;
            int esign = 1, exponent = 0;
            if (peek() == '+' || peek() == '-') esign = s[at++] == '-' ? -1 : 1;
            while (isDigit(peek())) exponent = std::min(exponent * 10 + (s[at++] - '0'), 10000);
            scale += esign * exponent;
        }
        return sign * (scale ? mantissa * std::pow(10.0, scale) : mantissa);
    }
    void string(Token& t, char quote) {
        t.type = Tok::String;
        while (!eof()) {
            size_t start = at;
            while (!eof() && s[at] != quote && s[at] != '\\' && !isNewline(peek())) ++at;
            t.text.append(s.data() + start, at - start);
            if (eof()) return;
            char c = s[at];
            if (c == quote) {
                ++at;
                return;
            }
            if (isNewline((unsigned char)c)) {
                t.type = Tok::BadString;
                return;
            }
            if (c == '\\') {
                if (eof(1)) {
                    ++at;
                } else if (isNewline(peek(1))) {
                    ++at;
                    skipNewline();
                } else {
                    ++at;
                    escape(t.text);
                }
                continue;
            }
            t.text += c;
            ++at;
        }
    }
    void badUrl(Token& t) {
        t.type = Tok::BadUrl;
        while (!eof()) {
            if (peek() == ')') {
                ++at;
                return;
            }
            at += validEscape(0) ? 2 : 1;
        }
    }
    void url(Token& t) {
        t.type = Tok::Url;
        while (isSpace(peek())) ++at;
        while (!eof()) {
            unsigned char c = peek();
            if (c == ')') {
                ++at;
                return;
            }
            if (isSpace(c)) {
                while (isSpace(peek())) ++at;
                if (eof()) return;
                if (peek() == ')') {
                    ++at;
                    return;
                }
                return badUrl(t);
            }
            if (c == '"' || c == '\'' || c == '(' || c < 0x09 || c == 0x0B || (c >= 0x0E && c < 0x20) || c == 0x7F) {
                return badUrl(t);
            }
            if (c == '\\') {
                if (!validEscape(0)) return badUrl(t);
                ++at;
                escape(t.text);
                continue;
            }
            t.text += s[at++];
        }
    }
    void numeric(Token& t) {
        t.number = number(t.integer);
        if (startsIdent(0)) {
            t.type = Tok::Dimension;
            t.text = name();
        } else if (peek() == '%') {
            ++at;
            t.type = Tok::Percentage;
        } else {
            t.type = Tok::Number;
        }
    }
    void identLike(Token& t) {
        t.text = name();
        if (peek() != '(') {
            t.type = Tok::Ident;
            return;
        }
        ++at;
        t.type = Tok::Function;
        if (!equalsLower(t.text, "url")) return;
        size_t keep = at;
        while (isSpace(peek())) ++at;
        if (peek() == '"' || peek() == '\'') {
            at = keep;
            return;
        }
        t.text.clear();
        url(t);
    }

    bool next(Token& t) {
        for (;;) {
            if (eof()) return false;
            if (peek() == '/' && peek(1) == '*') {
                size_t close = s.find("*/", at + 2);
                at = close == std::string_view::npos ? s.size() : close + 2;
                continue;
            }
            break;
        }
        t.begin = (uint32_t)at;
        unsigned char c = peek();
        if (isSpace(c)) {
            while (isSpace(peek())) ++at;
            t.type = Tok::Space;
        } else if (c == '"' || c == '\'') {
            ++at;
            string(t, (char)c);
        } else if (c == '#') {
            if (isName(peek(1)) || validEscape(1)) {
                ++at;
                t.type = Tok::Hash;
                t.idHash = startsIdent(0);
                t.text = name();
            } else {
                ++at;
                t.type = Tok::Delim;
                t.delim = '#';
            }
        } else if (isDigit(c) || ((c == '+' || c == '.') && startsNumber(0))) {
            numeric(t);
        } else if (c == '-') {
            if (startsNumber(0)) {
                numeric(t);
            } else if (peek(1) == '-' && peek(2) == '>') {
                at += 3;
                t.type = Tok::CDC;
            } else if (startsIdent(0)) {
                identLike(t);
            } else {
                ++at;
                t.type = Tok::Delim;
                t.delim = '-';
            }
        } else if (c == '<' && s.substr(at, 4) == "<!--") {
            at += 4;
            t.type = Tok::CDO;
        } else if (c == '@' && startsIdent(1)) {
            ++at;
            t.type = Tok::AtKeyword;
            t.text = name();
        } else if (startsIdent(0)) {
            identLike(t);
        } else {
            ++at;
            switch (c) {
                case '(': t.type = Tok::OpenParen; break;
                case ')': t.type = Tok::CloseParen; break;
                case '[': t.type = Tok::OpenSquare; break;
                case ']': t.type = Tok::CloseSquare; break;
                case '{': t.type = Tok::OpenCurly; break;
                case '}': t.type = Tok::CloseCurly; break;
                case ',': t.type = Tok::Comma; break;
                case ':': t.type = Tok::Colon; break;
                case ';': t.type = Tok::Semicolon; break;
                default:
                    t.type = Tok::Delim;
                    if (c < 0x80) {
                        t.delim = (char)c;
                    } else {
                        while (!eof() && (peek() & 0xC0) == 0x80) ++at;
                    }
            }
        }
        t.end = (uint32_t)at;
        return true;
    }
};

Tok closer(Tok open) {
    switch (open) {
        case Tok::Function:
        case Tok::OpenParen: return Tok::CloseParen;
        case Tok::OpenSquare: return Tok::CloseSquare;
        case Tok::OpenCurly: return Tok::CloseCurly;
        default: return Tok::Space;
    }
}

size_t skipNodeIn(const std::vector<Token>& tokens, size_t at, size_t end);

// How many component values lie between `at` and a `stop` token or the end
size_t countNodes(const std::vector<Token>& tokens, size_t at, size_t end, const Tok* stop) {
    size_t count = 0;
    while (at < end && !(stop && tokens[at].type == *stop)) {
        at = skipNodeIn(tokens, at, end);
        ++count;
    }
    return count;
}

Node readNodeAt(std::vector<Token>& tokens, size_t& at, size_t end, int depth) {
    Node n;
    n.token = std::move(tokens[at++]);
    Tok close = closer(n.token.type);
    if (close == Tok::Space || depth > 64) return n;
    n.children.reserve(countNodes(tokens, at, end, &close));
    while (at < end) {
        if (tokens[at].type == close) {
            n.token.end = tokens[at++].end;
            return n;
        }
        n.children.push_back(readNodeAt(tokens, at, end, depth + 1));
    }
    n.token.end = tokens[at - 1].end;
    return n;
}

bool wordChar(char c) { return isName((unsigned char)c) || c == '%' || c == '.' || c == '#' || c == '@'; }

void appendNumber(std::string& out, double v) {
    char buffer[32];
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        std::snprintf(buffer, sizeof buffer, "%.0f", v);
    } else {
        std::snprintf(buffer, sizeof buffer, "%.6g", v);
        for (char* p = buffer; *p; ++p) {
            if (*p == ',') *p = '.';  // a locale's decimal comma
        }
    }
    out += buffer;
}

void appendEscaped(std::string& out, const std::string& name) {
    for (size_t i = 0; i < name.size(); ++i) {
        unsigned char c = (unsigned char)name[i];
        bool plain = isName(c) && !(i == 0 && isDigit(c)) && !(i == 1 && name[0] == '-' && isDigit(c));
        if (!plain) out += '\\';
        out += (char)c;
    }
}

void serializeNode(const Node& n, std::string& out) {
    const Token& t = n.token;
    std::string piece;
    switch (t.type) {
        case Tok::Ident: appendEscaped(piece, t.text); break;
        case Tok::Function: appendEscaped(piece, t.text); piece += '('; break;
        case Tok::AtKeyword: piece = "@"; appendEscaped(piece, t.text); break;
        case Tok::Hash: piece = "#"; appendEscaped(piece, t.text); break;
        case Tok::String:
        case Tok::BadString:
        case Tok::Url:
        case Tok::BadUrl:
            piece = t.type == Tok::Url || t.type == Tok::BadUrl ? "url(\"" : "\"";
            for (char c : t.text) {
                if (c == '"' || c == '\\') piece += '\\';
                if (c == '\n') {
                    piece += "\\a ";
                    continue;
                }
                piece += c;
            }
            piece += t.type == Tok::Url || t.type == Tok::BadUrl ? "\")" : "\"";
            break;
        case Tok::Delim: piece = t.delim ? std::string(1, t.delim) : std::string("\xEF\xBF\xBD"); break;
        case Tok::Number: appendNumber(piece, t.number); break;
        case Tok::Percentage: appendNumber(piece, t.number); piece += '%'; break;
        case Tok::Dimension:
            appendNumber(piece, t.number);
            if (t.text.size() > 1 && (t.text[0] == 'e' || t.text[0] == 'E') &&
                (isDigit((unsigned char)t.text[1]) || t.text[1] == '-' || t.text[1] == '+')) {
                piece += '\\';  // 1e3 would read as an exponent
            }
            appendEscaped(piece, t.text);
            break;
        case Tok::Space: piece = " "; break;
        case Tok::CDO: piece = "<!--"; break;
        case Tok::CDC: piece = "-->"; break;
        case Tok::Colon: piece = ":"; break;
        case Tok::Semicolon: piece = ";"; break;
        case Tok::Comma: piece = ","; break;
        case Tok::OpenSquare: piece = "["; break;
        case Tok::OpenParen: piece = "("; break;
        case Tok::OpenCurly: piece = "{"; break;
        default: piece = t.type == Tok::CloseSquare ? "]" : t.type == Tok::CloseParen ? ")" : "}";
    }
    if (!out.empty() && !piece.empty() && wordChar(out.back()) && wordChar(piece[0])) out += "/**/";
    out += piece;
    Tok close = closer(t.type);
    if (close == Tok::Space) return;
    for (const Node& child : n.children) serializeNode(child, out);
    out += close == Tok::CloseParen ? ')' : close == Tok::CloseSquare ? ']' : '}';
}

}  // namespace

std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> tokens;
    tokens.reserve(text.size() / 3 + 4);
    Tokenizer tokenizer{text};
    for (;;) {
        tokens.emplace_back();
        if (!tokenizer.next(tokens.back())) break;
    }
    tokens.pop_back();
    return tokens;
}

bool Node::ident(const char* lower) const { return token.type == Tok::Ident && equalsLower(token.text, lower); }
bool Node::function(const char* lower) const {
    return token.type == Tok::Function && equalsLower(token.text, lower);
}

Node readNode(std::vector<Token>& tokens, size_t& at, size_t end) {
    return readNodeAt(tokens, at, end, 0);
}

void readNodes(std::vector<Token>& tokens, size_t& at, size_t end, std::vector<Node>& out) {
    out.reserve(out.size() + countNodes(tokens, at, end, nullptr));
    while (at < end) out.push_back(readNodeAt(tokens, at, end, 0));
}

size_t skipNode(const std::vector<Token>& tokens, size_t at, size_t end) { return skipNodeIn(tokens, at, end); }

namespace {

size_t skipNodeIn(const std::vector<Token>& tokens, size_t at, size_t end) {
    Tok open[64];
    int depth = 0;
    open[0] = closer(tokens[at].type);
    if (open[0] == Tok::Space) return at + 1;
    for (++at, ++depth; at < end && depth > 0; ++at) {
        Tok t = tokens[at].type;
        if (t == open[depth - 1]) {
            --depth;
        } else {
            Tok inner = closer(t);
            if (inner != Tok::Space && depth < 64) open[depth++] = inner;
        }
    }
    return at;
}

}  // namespace

std::vector<Node> parseNodes(std::string_view text) {
    std::vector<Token> tokens = tokenize(text);
    std::vector<Node> out;
    size_t at = 0;
    readNodes(tokens, at, tokens.size(), out);
    return out;
}

void serializeNodes(const std::vector<Node>& nodes, std::string& out) {
    for (const Node& n : nodes) serializeNode(n, out);
}

bool hasVar(const std::vector<Node>& nodes) {
    for (const Node& n : nodes) {
        if (n.function("var") || hasVar(n.children)) return true;
    }
    return false;
}

void trimSpace(std::vector<Node>& nodes) {
    while (!nodes.empty() && nodes.back().is(Tok::Space)) nodes.pop_back();
    size_t lead = 0;
    while (lead < nodes.size() && nodes[lead].is(Tok::Space)) ++lead;
    if (lead) nodes.erase(nodes.begin(), nodes.begin() + (ptrdiff_t)lead);
}

std::vector<std::vector<Node>> splitCommas(const std::vector<Node>& nodes) {
    std::vector<std::vector<Node>> out(1);
    for (const Node& n : nodes) {
        if (n.is(Tok::Comma)) {
            out.emplace_back();
        } else {
            out.back().push_back(n);
        }
    }
    for (auto& part : out) trimSpace(part);
    return out;
}

std::vector<const Node*> parts(const std::vector<Node>& nodes) {
    std::vector<const Node*> out;
    out.reserve(nodes.size());
    for (const Node& n : nodes) {
        if (!n.is(Tok::Space)) out.push_back(&n);
    }
    return out;
}

std::vector<std::vector<const Node*>> splitParts(const std::vector<Node>& nodes) {
    std::vector<std::vector<const Node*>> out(1);
    for (const Node& n : nodes) {
        if (n.is(Tok::Comma)) out.emplace_back();
        else if (!n.is(Tok::Space)) out.back().push_back(&n);
    }
    return out;
}

bool equalsLower(std::string_view text, const char* lower) {
    size_t n = std::strlen(lower);
    if (text.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != lower[i]) return false;
    }
    return true;
}

bool startsWithLower(std::string_view text, const char* lower) {
    size_t n = std::strlen(lower);
    return text.size() >= n && equalsLower(text.substr(0, n), lower);
}

std::string toLower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return out;
}

Lines::Lines(std::string_view text) {
    starts.push_back(0);
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n' || c == '\f' || (c == '\r' && (i + 1 >= text.size() || text[i + 1] != '\n'))) {
            starts.push_back((uint32_t)i + 1);
        }
    }
}

void Lines::at(std::string_view text, uint32_t offset, int& line, int& column) const {
    size_t index = (size_t)(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin());
    line = (int)index;
    column = 1;
    for (uint32_t i = starts[index - 1]; i < offset && i < text.size(); ++i) {
        if (((unsigned char)text[i] & 0xC0) != 0x80) ++column;
    }
}

// --- Properties ----------------------------------------------------------------------

namespace {

const char* const propertyNames[] = {
    "color", "background-color", "background-image",
    "font-family", "font-size", "font-weight", "font-style", "font-stretch", "font-feature-settings",
    "font-variation-settings", "font-variant-caps", "font-variant-numeric", "font-variant-ligatures",
    "line-height", "letter-spacing", "text-align", "text-indent", "text-transform",
    "text-decoration-line", "text-decoration-color", "text-decoration-style", "text-decoration-thickness",
    "text-underline-offset",
    "border-top-width", "border-top-style", "border-top-color",
    "border-right-width", "border-right-style", "border-right-color",
    "border-bottom-width", "border-bottom-style", "border-bottom-color",
    "border-left-width", "border-left-style", "border-left-color",
    "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
    "border-bottom-left-radius",
    "margin-top", "margin-right", "margin-bottom", "margin-left",
    "padding-top", "padding-right", "padding-bottom", "padding-left",
    "max-width", "list-style-type", "content", "counter-reset", "counter-increment",
    "opacity", "box-shadow", "accent-color", "caret-color", "color-scheme",
    "font", "background", "border", "border-top", "border-right", "border-bottom", "border-left",
    "border-width", "border-style", "border-color", "border-radius", "margin", "padding",
    "text-decoration", "font-variant", "list-style",
};
static_assert(sizeof(propertyNames) / sizeof(propertyNames[0]) == PropCustom, "a name for every property");

// Properties CSS has and Tinta leaves alone: 'b' blocks, 'a' animation, 'd' not drawn
struct Ignored {
    const char* name;
    char why;
};
const Ignored ignoredProperties[] = {
    {"display", 'b'}, {"position", 'b'}, {"float", 'b'}, {"clear", 'b'}, {"width", 'b'},
    {"height", 'b'}, {"min-width", 'b'}, {"min-height", 'b'}, {"max-height", 'b'}, {"top", 'b'},
    {"right", 'b'}, {"bottom", 'b'}, {"left", 'b'}, {"inset", 'b'}, {"z-index", 'b'},
    {"overflow", 'b'}, {"overflow-x", 'b'}, {"overflow-y", 'b'}, {"flex", 'b'},
    {"flex-direction", 'b'}, {"flex-wrap", 'b'}, {"flex-flow", 'b'}, {"flex-grow", 'b'},
    {"flex-shrink", 'b'}, {"flex-basis", 'b'}, {"justify-content", 'b'}, {"align-items", 'b'},
    {"align-content", 'b'}, {"align-self", 'b'}, {"justify-items", 'b'}, {"justify-self", 'b'},
    {"place-items", 'b'}, {"place-content", 'b'}, {"place-self", 'b'}, {"order", 'b'},
    {"grid", 'b'}, {"grid-template", 'b'}, {"grid-template-columns", 'b'},
    {"grid-template-rows", 'b'}, {"grid-template-areas", 'b'}, {"grid-area", 'b'},
    {"grid-column", 'b'}, {"grid-row", 'b'}, {"grid-auto-flow", 'b'}, {"grid-auto-columns", 'b'},
    {"grid-auto-rows", 'b'}, {"gap", 'b'}, {"row-gap", 'b'}, {"column-gap", 'b'},
    {"grid-gap", 'b'}, {"columns", 'b'}, {"column-count", 'b'}, {"column-width", 'b'},
    {"column-rule", 'b'}, {"column-span", 'b'}, {"box-sizing", 'b'}, {"vertical-align", 'b'},
    {"visibility", 'b'}, {"aspect-ratio", 'b'}, {"object-fit", 'b'}, {"object-position", 'b'},
    {"contain", 'b'}, {"table-layout", 'b'}, {"border-collapse", 'b'}, {"border-spacing", 'b'},
    {"caption-side", 'b'}, {"empty-cells", 'b'}, {"inline-size", 'b'}, {"block-size", 'b'},
    {"min-inline-size", 'b'}, {"max-inline-size", 'b'}, {"min-block-size", 'b'},
    {"max-block-size", 'b'}, {"margin-inline", 'b'}, {"margin-block", 'b'},
    {"margin-inline-start", 'b'}, {"margin-inline-end", 'b'}, {"margin-block-start", 'b'},
    {"margin-block-end", 'b'}, {"padding-inline", 'b'}, {"padding-block", 'b'},
    {"padding-inline-start", 'b'}, {"padding-inline-end", 'b'}, {"padding-block-start", 'b'},
    {"padding-block-end", 'b'}, {"break-before", 'b'}, {"break-after", 'b'},
    {"break-inside", 'b'}, {"page-break-before", 'b'}, {"page-break-after", 'b'},
    {"page-break-inside", 'b'}, {"orphans", 'b'}, {"widows", 'b'}, {"content-visibility", 'b'},
    {"writing-mode", 'b'}, {"direction", 'b'}, {"unicode-bidi", 'b'},
    {"animation", 'a'}, {"animation-name", 'a'}, {"animation-duration", 'a'},
    {"animation-delay", 'a'}, {"animation-iteration-count", 'a'}, {"animation-timing-function", 'a'},
    {"animation-fill-mode", 'a'}, {"animation-direction", 'a'}, {"animation-play-state", 'a'},
    {"transition", 'a'}, {"transition-property", 'a'}, {"transition-duration", 'a'},
    {"transition-delay", 'a'}, {"transition-timing-function", 'a'}, {"transform", 'a'},
    {"transform-origin", 'a'}, {"translate", 'a'}, {"rotate", 'a'}, {"scale", 'a'},
    {"perspective", 'a'}, {"will-change", 'a'}, {"scroll-behavior", 'a'},
    {"cursor", 'd'}, {"pointer-events", 'd'}, {"user-select", 'd'}, {"resize", 'd'},
    {"touch-action", 'd'}, {"outline", 'd'}, {"outline-color", 'd'}, {"outline-style", 'd'},
    {"outline-width", 'd'}, {"outline-offset", 'd'}, {"text-shadow", 'd'}, {"word-spacing", 'd'},
    {"white-space", 'd'}, {"word-break", 'd'}, {"overflow-wrap", 'd'}, {"word-wrap", 'd'},
    {"hyphens", 'd'}, {"tab-size", 'd'}, {"text-overflow", 'd'}, {"text-wrap", 'd'},
    {"line-break", 'd'}, {"quotes", 'd'}, {"font-kerning", 'd'}, {"font-size-adjust", 'd'},
    {"font-synthesis", 'd'}, {"font-optical-sizing", 'd'}, {"font-variant-east-asian", 'd'},
    {"font-variant-position", 'd'}, {"font-variant-alternates", 'd'}, {"font-language-override", 'd'},
    {"font-palette", 'd'}, {"text-rendering", 'd'}, {"image-rendering", 'd'},
    {"background-repeat", 'd'}, {"background-position", 'd'}, {"background-size", 'd'},
    {"background-attachment", 'd'}, {"background-clip", 'd'}, {"background-origin", 'd'},
    {"background-blend-mode", 'd'}, {"list-style-position", 'd'}, {"list-style-image", 'd'},
    {"counter-set", 'd'}, {"filter", 'd'}, {"backdrop-filter", 'd'}, {"mix-blend-mode", 'd'},
    {"isolation", 'd'}, {"clip-path", 'd'}, {"mask", 'd'}, {"appearance", 'd'},
    {"text-justify", 'd'}, {"text-align-last", 'd'}, {"text-emphasis", 'd'},
    {"text-combine-upright", 'd'}, {"text-orientation", 'd'}, {"ruby-position", 'd'},
    {"text-decoration-skip-ink", 'd'}, {"text-underline-position", 'd'}, {"scrollbar-width", 'd'},
    {"scrollbar-color", 'd'}, {"scrollbar-gutter", 'd'}, {"overscroll-behavior", 'd'},
    {"border-image", 'd'}, {"text-size-adjust", 'd'}, {"print-color-adjust", 'd'},
    {"forced-color-adjust", 'd'},
};

size_t editDistance(std::string_view a, std::string_view b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diagonal = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

}  // namespace

const char* propertyName(int id) {
    return id >= 0 && id < PropCustom ? propertyNames[id] : "custom property";
}

int propertyId(std::string_view lowerName) {
    static const std::vector<std::pair<std::string_view, int>> sorted = [] {
        std::vector<std::pair<std::string_view, int>> list;
        for (int i = 0; i < PropCustom; ++i) list.emplace_back(propertyNames[i], i);
        std::sort(list.begin(), list.end());
        return list;
    }();
    auto it = std::lower_bound(sorted.begin(), sorted.end(), std::make_pair(lowerName, -1));
    return it != sorted.end() && it->first == lowerName ? it->second : -1;
}

bool inherited(Prop prop) {
    switch (prop) {
        case Prop::Color: case Prop::FontFamily: case Prop::FontSize: case Prop::FontWeight:
        case Prop::FontStyle: case Prop::FontStretch: case Prop::FontFeatureSettings:
        case Prop::FontVariationSettings: case Prop::FontVariantCaps: case Prop::FontVariantNumeric:
        case Prop::FontVariantLigatures: case Prop::LineHeight: case Prop::LetterSpacing:
        case Prop::TextAlign: case Prop::TextIndent: case Prop::TextTransform:
        case Prop::TextUnderlineOffset: case Prop::ListStyleType: case Prop::AccentColor:
        case Prop::CaretColor: case Prop::ColorScheme:
            return true;
        default:
            return false;
    }
}

const std::vector<Prop>& longhands(int shorthand) {
    using P = Prop;
    static const std::vector<Prop> table[] = {
        {P::FontStyle, P::FontVariantCaps, P::FontVariantNumeric, P::FontVariantLigatures, P::FontWeight,
         P::FontStretch, P::FontSize, P::LineHeight, P::FontFamily},
        {P::BackgroundColor, P::BackgroundImage},
        {P::BorderTopWidth, P::BorderTopStyle, P::BorderTopColor, P::BorderRightWidth, P::BorderRightStyle,
         P::BorderRightColor, P::BorderBottomWidth, P::BorderBottomStyle, P::BorderBottomColor,
         P::BorderLeftWidth, P::BorderLeftStyle, P::BorderLeftColor},
        {P::BorderTopWidth, P::BorderTopStyle, P::BorderTopColor},
        {P::BorderRightWidth, P::BorderRightStyle, P::BorderRightColor},
        {P::BorderBottomWidth, P::BorderBottomStyle, P::BorderBottomColor},
        {P::BorderLeftWidth, P::BorderLeftStyle, P::BorderLeftColor},
        {P::BorderTopWidth, P::BorderRightWidth, P::BorderBottomWidth, P::BorderLeftWidth},
        {P::BorderTopStyle, P::BorderRightStyle, P::BorderBottomStyle, P::BorderLeftStyle},
        {P::BorderTopColor, P::BorderRightColor, P::BorderBottomColor, P::BorderLeftColor},
        {P::BorderTopLeftRadius, P::BorderTopRightRadius, P::BorderBottomRightRadius, P::BorderBottomLeftRadius},
        {P::MarginTop, P::MarginRight, P::MarginBottom, P::MarginLeft},
        {P::PaddingTop, P::PaddingRight, P::PaddingBottom, P::PaddingLeft},
        {P::TextDecorationLine, P::TextDecorationStyle, P::TextDecorationColor, P::TextDecorationThickness},
        {P::FontVariantCaps, P::FontVariantNumeric, P::FontVariantLigatures},
        {P::ListStyleType},
    };
    static const std::vector<Prop> none;
    int i = shorthand - ShFont;
    return i >= 0 && i < (int)(sizeof(table) / sizeof(table[0])) ? table[i] : none;
}

const char* ignoredReason(std::string_view lowerName) {
    // Families of properties by their prefix, for the members not listed
    static const Ignored families[] = {
        {"flex-", 'b'}, {"grid-", 'b'}, {"align-", 'b'}, {"justify-", 'b'}, {"place-", 'b'},
        {"inset-", 'b'}, {"overflow-", 'b'}, {"column-", 'b'}, {"break-", 'b'}, {"page-", 'b'},
        {"scroll-", 'b'}, {"overscroll-", 'b'}, {"container", 'b'}, {"animation-", 'a'},
        {"transition-", 'a'}, {"transform-", 'a'}, {"offset-", 'a'}, {"view-transition", 'a'},
        {"mask-", 'd'}, {"outline-", 'd'}, {"scrollbar-", 'd'}, {"text-emphasis-", 'd'},
        {"font-synthesis-", 'd'},
    };
    char why = 0;
    for (const Ignored& ignored : ignoredProperties) {
        if (lowerName == ignored.name) why = ignored.why;
    }
    for (const Ignored& family : families) {
        if (!why && startsWithLower(lowerName, family.name)) why = family.why;
    }
    return why == 'b' ? "has no effect: Tinta lays out blocks itself"
         : why == 'a' ? "has no effect: Tinta does not animate"
         : why == 'd' ? "is not drawn by Tinta" : nullptr;
}

std::string suggestProperty(std::string_view lowerName) {
    std::string best;
    size_t bestDistance = 3;
    auto consider = [&](const char* name) {
        size_t d = editDistance(lowerName, name);
        if (d < bestDistance) {
            bestDistance = d;
            best = name;
        }
    };
    for (const char* name : propertyNames) consider(name);
    for (const Ignored& ignored : ignoredProperties) consider(ignored.name);
    return lowerName.size() > 2 ? best : std::string();
}

bool cssWideKeyword(const Node& n) {
    return n.ident("inherit") || n.ident("initial") || n.ident("unset") || n.ident("revert") || n.ident("revert-layer");
}

bool cssWideKeyword(const std::vector<Node>& value) { return value.size() == 1 && cssWideKeyword(value[0]); }

// --- Small public pieces -------------------------------------------------------------------

float Length::resolve(float containing) const {
    return kind == Px ? value : kind == Percent ? containing * value / 100 : 0;
}

bool operator==(const Color& x, const Color& y) {
    return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
}

std::string formatColor(const Color& color) {
    auto byte = [](float v) { return (int)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255); };
    char buffer[16];
    if (byte(color.a) == 255) {
        std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", byte(color.r), byte(color.g), byte(color.b));
    } else {
        std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x%02x", byte(color.r), byte(color.g),
                      byte(color.b), byte(color.a));
    }
    return buffer;
}

}
