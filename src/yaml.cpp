// The YAML reader (yaml.h): YAML 1.2's block and flow styles, as much of them
// as front matter and CSL YAML use, read into json::Json values.

#pragma optimize("s", on)

#include "yaml.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace yaml {
namespace {

using json::Json;

bool isBreak(char c) { return c == '\n' || c == '\r'; }
bool isBlank(char c) { return c == ' ' || c == '\t'; }
bool isFlowIndicator(char c) { return c == ',' || c == '[' || c == ']' || c == '{' || c == '}'; }

// A value written without quotes: YAML's core schema types null, true and
// false and whole numbers; a leading zero or sixteen digits keep a number a
// string, since an id or a code is no number
void typed(Json& out, std::string text) {
    out.type = Json::String;
    if (text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL") {
        out.type = Json::Null;
        text.clear();
    } else if (text == "true" || text == "True" || text == "TRUE") {
        out.type = Json::Bool;
        out.boolean = true;
    } else if (text == "false" || text == "False" || text == "FALSE") {
        out.type = Json::Bool;
    } else {
        size_t first = text[0] == '-' ? 1 : 0;
        bool whole = text.size() > first && text.size() - first <= 15 &&
                     (text[first] != '0' || text.size() == first + 1);
        for (size_t i = first; i < text.size() && whole; ++i) whole = text[i] >= '0' && text[i] <= '9';
        if (whole) {
            out.type = Json::Number;
            out.number = std::strtod(text.c_str(), nullptr);
        }
    }
    out.text = std::move(text);
}

struct Reader {
    std::string_view s;
    size_t at = 0, first = 0;  // first: past a byte order mark, where the first line starts
    int depth = 0;
    bool failed = false;
    Error error;

    struct Deeper {
        Reader& r;
        explicit Deeper(Reader& reader) : r(reader) { ++r.depth; }
        ~Deeper() { --r.depth; }
    };

    char peek(size_t i) const { return i < s.size() ? s[i] : '\0'; }
    bool ends(size_t i) const { return i >= s.size() || isBreak(s[i]); }
    // A blank or the line's end after an indicator such as `-` or `:`
    bool spaceAfter(size_t i) const { return ends(i) || isBlank(s[i]); }

    bool fail(size_t where, std::string message) {
        if (!failed) {
            failed = true;
            error.offset = std::min(where, s.size());
            error.line = 1 + (int)std::count(s.begin(), s.begin() + (ptrdiff_t)error.offset, '\n');
            error.message = std::move(message);
        }
        return false;
    }

    size_t lineStart(size_t i) const {
        while (i > first && !isBreak(s[i - 1])) --i;
        return i;
    }
    int column(size_t i) const { return (int)(i - lineStart(i)); }
    // The start of the line after the one `i` is on
    size_t nextLine(size_t i) const {
        while (i < s.size() && !isBreak(s[i])) ++i;
        if (i < s.size() && s[i] == '\r') ++i;
        if (i < s.size() && s[i] == '\n') ++i;
        return i;
    }
    // Only blanks and perhaps a comment from `i` to the line's end
    bool restEmpty(size_t i) const {
        while (i < s.size() && isBlank(s[i])) ++i;
        return ends(i) || s[i] == '#';
    }
    // `---` (or `...`) alone at the start of a line; `c` 0 for either
    bool marker(size_t i, char c = 0) const {
        if (i + 3 > s.size() || (i > first && !isBreak(s[i - 1]))) return false;
        if (c ? s[i] != c : s[i] != '-' && s[i] != '.') return false;
        return s[i + 1] == s[i] && s[i + 2] == s[i] && spaceAfter(i + 3);
    }

    // To the first character of the next line that holds more than blanks
    // and a comment. False at the text's end, and at an indentation with a
    // tab, which is reported
    bool nextContent() {
        size_t i = at;
        for (;;) {
            while (i < s.size() && isBlank(s[i])) ++i;
            if (i >= s.size()) {
                at = i;
                return false;
            }
            if (isBreak(s[i]) || s[i] == '#') {
                i = nextLine(i);
                continue;
            }
            at = i;
            for (size_t k = lineStart(i); k < i && isBlank(s[k]); ++k) {
                if (s[k] == '\t') return fail(k, "a tab in the indentation; YAML indents with spaces");
            }
            return true;
        }
    }

    // Nothing but blanks and a comment may follow a quoted or [ ] value
    bool lineDone() {
        size_t i = at;
        while (i < s.size() && isBlank(s[i])) ++i;
        return ends(i) || s[i] == '#' || fail(i, "unexpected text after a value");
    }

    bool emptyValue(Json& out, size_t where) {
        out = Json();
        out.begin = out.end = where;
        return true;
    }

    // What a value without quotes cannot start with
    bool plainStart(size_t i) {
        char c = s[i];
        if (c == '&') return fail(i, "anchors (&) are not supported; quote a value that starts with &");
        if (c == '*') return fail(i, "aliases (*) are not supported; quote a value that starts with *");
        if (c == '!') return fail(i, "tags (!) are not supported; quote a value that starts with !");
        if (c == '?' && spaceAfter(i + 1)) return fail(i, "complex keys (?) are not supported");
        if (c == '%' || c == '@' || c == '`') return fail(i, std::string("quote a value that starts with ") + c);
        if (c == ']' || c == '}' || c == ',') return fail(i, std::string("unexpected `") + c + "`");
        return true;
    }

    // Where the `:` is when the line from `i` is a key and its value
    size_t keyColon(size_t i) const {
        for (size_t k = i; !ends(k); ++k) {
            if (s[k] == '#' && k > i && isBlank(s[k - 1])) break;
            if (s[k] == ':' && spaceAfter(k + 1)) return k;
        }
        return std::string_view::npos;
    }

    // --- Block style ---

    // A node whose first character is at `at`. `indent` is the column of the
    // block it is in (-1 for the document): its lines must be beyond it. One
    // cut short by a mistake ends where the mistake is
    bool blockNode(int indent, Json& out) {
        Deeper guard(*this);
        if (depth > 64) return fail(at, "nested too deep");
        out.begin = out.end = at;
        bool ok = blockKind(indent, out);
        if (!ok) out.end = std::max(out.end, error.offset);
        return ok;
    }

    bool blockKind(int indent, Json& out) {
        char c = s[at];
        int col = column(at);
        if (c == '-' && spaceAfter(at + 1)) return blockSequence(col, out);
        if (c == '[' || c == '{') return flow(out) && lineDone();
        if (c == '|' || c == '>') return blockScalar(indent, out);
        if (c == '"' || c == '\'') {
            size_t start = at;
            if (!quoted(out)) return false;
            size_t i = at;
            while (i < s.size() && isBlank(s[i])) ++i;
            if (peek(i) == ':' && spaceAfter(i + 1)) {  // a quoted key
                at = start;
                out = Json();
                return blockMapping(col, out);
            }
            return lineDone();
        }
        if (!plainStart(at)) return false;
        if (keyColon(at) != std::string_view::npos) return blockMapping(col, out);
        return plain(indent, out);
    }

    // Keys at column `col`, each with its value
    bool blockMapping(int col, Json& out) {
        out.type = Json::Object;
        out.begin = out.end = at;
        for (;;) {
            size_t keyAt = at;
            std::string key;
            char c = s[at];
            if (c == '"' || c == '\'') {
                Json k;
                if (!quoted(k)) return false;
                if (lineStart(at) != lineStart(keyAt)) return fail(keyAt, "a key must fit on one line");
                while (at < s.size() && isBlank(s[at])) ++at;
                if (peek(at) != ':' || !spaceAfter(at + 1)) return fail(at, "expected `:` after a key");
                key = std::move(k.text);
            } else {
                if (c == '-' && spaceAfter(at + 1)) return fail(at, "a list item where a key was expected");
                if (c == '[' || c == '{') return fail(at, "a [ ] or { } as a key is not supported");
                if (!plainStart(at)) return false;
                size_t colon = keyColon(at);
                if (colon == std::string_view::npos) return fail(at, "expected a key and `:`");
                size_t e = colon;
                while (e > at && isBlank(s[e - 1])) --e;
                key.assign(s.substr(at, e - at));
                at = colon;
            }
            ++at;  // the `:`
            for (const auto& field : out.fields) {
                if (field.first == key) return fail(keyAt, "the key `" + key + "` appears twice");
            }
            Json value;
            bool ok = mappingValue(col, value);
            out.end = std::max(out.end, value.end);
            out.fields.emplace_back(std::move(key), std::move(value));
            if (!ok) return false;
            if (!nextContent() || marker(at)) return true;
            int k = column(at);
            if (k < col) return true;
            if (k > col) return fail(at, "this line is indented more than the key above it");
        }
    }

    // A key's value, from just after its `:`
    bool mappingValue(int col, Json& out) {
        while (at < s.size() && isBlank(s[at])) ++at;
        out.begin = out.end = at;
        if (ends(at) || s[at] == '#') {
            size_t empty = at;
            if (!nextContent() || marker(at)) return emptyValue(out, empty);
            int k = column(at);
            // Deeper lines, or a list at the key's own column
            if (k > col || (k == col && s[at] == '-' && spaceAfter(at + 1))) return blockNode(col, out);
            return emptyValue(out, empty);
        }
        char c = s[at];
        if (c == '-' && spaceAfter(at + 1)) return fail(at, "a list cannot start on the line of its key");
        if (c == '"' || c == '\'') return quoted(out) && lineDone();
        if (c == '[' || c == '{') return flow(out) && lineDone();
        if (c == '|' || c == '>') return blockScalar(col, out);
        if (!plainStart(at)) return false;
        return plain(col, out);
    }

    // Items at column `col`, each after a `- `
    bool blockSequence(int col, Json& out) {
        out.type = Json::Array;
        out.begin = out.end = at;
        for (;;) {
            ++at;  // the `-`
            while (at < s.size() && isBlank(s[at])) ++at;
            Json item;
            item.begin = item.end = at;
            bool ok = true;
            if (ends(at) || s[at] == '#') {
                size_t empty = at;
                if (!nextContent() || marker(at) || column(at) <= col) emptyValue(item, empty);
                else ok = blockNode(col, item);
            } else {
                ok = blockNode(col, item);
            }
            out.end = std::max(out.end, item.end);
            out.items.push_back(std::move(item));
            if (!ok) return false;
            if (!nextContent() || marker(at)) return true;
            int k = column(at);
            if (k < col) return true;
            if (k > col) return fail(at, "this line is indented more than the list item above it");
            if (s[at] != '-' || !spaceAfter(at + 1)) return true;  // the next key of the mapping this list is in
        }
    }

    // A value without quotes. It goes on over the lines after it that are
    // indented beyond `indent`; their breaks fold into spaces, and empty
    // lines between them into line breaks
    bool plain(int indent, Json& out) {
        out.begin = at;
        std::string text;
        size_t i = at;
        for (;;) {
            size_t e = i;
            while (!ends(e) && !(s[e] == '#' && e > i && isBlank(s[e - 1]))) ++e;
            size_t t = e;
            while (t > i && isBlank(s[t - 1])) --t;
            text.append(s.data() + i, t - i);
            out.end = at = t;
            if (!ends(e)) break;  // a comment ends the value
            size_t line = nextLine(e), c = line;
            int empties = 0;
            for (;;) {
                c = line;
                while (c < s.size() && isBlank(s[c])) ++c;
                if (c >= s.size() || !isBreak(s[c])) break;
                ++empties;
                line = nextLine(c);
            }
            if (c >= s.size()) break;
            int spaces = 0;
            while (s[line + (size_t)spaces] == ' ') ++spaces;
            if (spaces <= indent || s[c] == '#' || marker(line)) break;
            if (empties) text.append((size_t)empties, '\n');
            else text += ' ';
            i = c;
        }
        typed(out, std::move(text));
        return true;
    }

    // A | or > block: its lines are those indented beyond `indent`
    bool blockScalar(int indent, Json& out) {
        bool literal = s[at] == '|';
        size_t start = at++;
        out.type = Json::String;
        out.begin = start;
        int chomp = 0, given = 0;  // chomp -1 strip, 1 keep; given an indentation indicator
        for (int k = 0; k < 2; ++k) {
            char c = peek(at);
            if ((c == '-' || c == '+') && !chomp) chomp = c == '-' ? -1 : 1;
            else if (c >= '1' && c <= '9' && !given) given = c - '0';
            else break;
            ++at;
        }
        out.end = at;
        if (!restEmpty(at)) return fail(at, "unexpected text after `|` or `>`");
        at = nextLine(at);
        int content = -1;
        if (given) {
            content = std::max(indent, -1) + given;
        } else {
            // The first line with text sets the indentation
            for (size_t line = at; line < s.size();) {
                size_t c = line;
                while (c < s.size() && s[c] == ' ') ++c;
                if (c < s.size() && !isBreak(s[c])) {
                    content = (int)(c - line);
                    break;
                }
                line = nextLine(c);
            }
            if (content <= indent) content = -1;  // no line belongs to it
        }
        std::vector<std::string_view> lines;
        while (content >= 0 && at < s.size()) {
            size_t c = at;
            while (c < s.size() && s[c] == ' ') ++c;
            if (c >= s.size() || isBreak(s[c])) {
                lines.emplace_back();
                at = nextLine(c);
                continue;
            }
            if ((int)(c - at) < content || marker(at)) break;
            size_t from = at + (size_t)content, e = c;
            while (!ends(e)) ++e;
            lines.push_back(s.substr(from, e - from));
            out.end = e;
            at = nextLine(e);
        }
        size_t last = lines.size();
        while (last > 0 && lines[last - 1].empty()) --last;
        std::string text;
        if (literal) {
            for (size_t k = 0; k < last; ++k) {
                if (k) text += '\n';
                text.append(lines[k]);
            }
        } else {
            // Folded: a break between two lines of text is a space, but an
            // empty line is a break, and lines indented further keep theirs
            bool started = false, wasMore = false;
            size_t empties = 0;
            for (size_t k = 0; k < last; ++k) {
                if (lines[k].empty()) {
                    ++empties;
                    continue;
                }
                bool more = isBlank(lines[k][0]);
                if (!started) text.append(empties, '\n');
                else if (wasMore || more) text.append(empties + 1, '\n');
                else if (empties) text.append(empties, '\n');
                else text += ' ';
                text.append(lines[k]);
                started = true;
                wasMore = more;
                empties = 0;
            }
        }
        if (last > 0 && chomp >= 0) text += '\n';
        if (chomp > 0) text.append(lines.size() - last, '\n');
        out.text = std::move(text);
        return true;
    }

    // --- Quoted values ---

    bool hex(int digits, uint32_t& out) {
        out = 0;
        for (int k = 0; k < digits; ++k) {
            char c = peek(at);
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) return false;
            out = out << 4 | (uint32_t)v;
            ++at;
        }
        return true;
    }

    // A backslash escape in a "..." value, at its backslash
    bool escape(std::string& text) {
        size_t start = at++;
        if (at >= s.size()) return fail(start, "a quoted value is not closed");
        char e = s[at++];
        int digits = 0;
        switch (e) {
            case '0': text += '\0'; return true;
            case 'a': text += '\a'; return true;
            case 'b': text += '\b'; return true;
            case 't': case '\t': text += '\t'; return true;
            case 'n': text += '\n'; return true;
            case 'v': text += '\v'; return true;
            case 'f': text += '\f'; return true;
            case 'r': text += '\r'; return true;
            case 'e': text += '\x1b'; return true;
            case ' ': case '"': case '/': case '\\': text += e; return true;
            case 'N': json::appendUtf8(text, 0x85); return true;
            case '_': json::appendUtf8(text, 0xA0); return true;
            case 'L': json::appendUtf8(text, 0x2028); return true;
            case 'P': json::appendUtf8(text, 0x2029); return true;
            case 'x': digits = 2; break;
            case 'u': digits = 4; break;
            case 'U': digits = 8; break;
            case '\r':
            case '\n':
                // A line break escaped: the lines join with nothing between
                at = nextLine(at - 1);
                while (at < s.size() && isBlank(s[at])) ++at;
                return true;
            default:
                return fail(start, std::string("unknown escape `\\") + e + "`");
        }
        uint32_t code = 0;
        if (!hex(digits, code)) return fail(start, std::string("a bad `\\") + e + "` escape");
        if (digits == 4 && code >= 0xD800 && code <= 0xDBFF && peek(at) == '\\' && peek(at + 1) == 'u') {
            size_t back = at;
            at += 2;
            uint32_t low = 0;
            if (hex(4, low) && low >= 0xDC00 && low <= 0xDFFF) code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            else at = back;
        }
        if ((code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) code = 0xFFFD;
        json::appendUtf8(text, code);
        return true;
    }

    // A '...' or "..." value. Over lines, a break folds into a space and an
    // empty line into a break, as in a value without quotes
    bool quoted(Json& out) {
        char q = s[at];
        size_t start = at++;
        out.type = Json::String;
        out.begin = start;
        std::string text;
        size_t keep = 0;  // blanks before here came from escapes and stay at a fold
        for (;;) {
            if (at >= s.size()) return fail(start, "a quoted value is not closed");
            char c = s[at];
            if (c == q) {
                if (q == '\'' && peek(at + 1) == '\'') {
                    text += '\'';
                    at += 2;
                    continue;
                }
                ++at;
                break;
            }
            if (isBreak(c)) {
                while (text.size() > keep && isBlank(text.back())) text.pop_back();
                size_t breaks = 0;
                do {
                    at = nextLine(at);
                    ++breaks;
                    if (marker(at)) return fail(start, "a quoted value is not closed");
                    while (at < s.size() && isBlank(s[at])) ++at;
                } while (at < s.size() && isBreak(s[at]));
                if (breaks == 1) text += ' ';
                else text.append(breaks - 1, '\n');
                continue;
            }
            if (c == '\\' && q == '"') {
                if (!escape(text)) return false;
                keep = text.size();
                continue;
            }
            text += c;
            ++at;
        }
        out.text = std::move(text);
        out.end = at;
        return true;
    }

    // --- Flow style ---

    // Past blanks, line breaks and comments inside [ ] and { }; false at the
    // text's end or at a document marker
    bool flowSpace() {
        for (;;) {
            while (at < s.size() && (isBlank(s[at]) || isBreak(s[at]))) ++at;
            if (at >= s.size() || marker(at)) return false;
            if (s[at] != '#' || (at > 0 && !isBlank(s[at - 1]) && !isBreak(s[at - 1]))) return true;
            at = nextLine(at);
        }
    }

    // A [ ] or { } collection, over as many lines as it takes
    bool flow(Json& out) {
        Deeper guard(*this);
        if (depth > 64) return fail(at, "nested too deep");
        bool list = s[at] == '[';
        char close = list ? ']' : '}';
        std::string open = list ? "a `[`" : "a `{`";
        out.type = list ? Json::Array : Json::Object;
        out.begin = at++;
        for (;;) {
            if (!flowSpace()) return fail(out.begin, open + " is not closed");
            if (s[at] == close) break;
            if (list) {
                Json item;
                bool ok = flowNode(item);
                out.items.push_back(std::move(item));
                if (!ok) return false;
            } else {
                size_t keyAt = at;
                Json key;
                if (s[at] == '"' || s[at] == '\'') {
                    if (!quoted(key)) return false;
                } else if (s[at] == '[' || s[at] == '{') {
                    return fail(at, "a [ ] or { } as a key is not supported");
                } else if (!plainStart(at) || !flowPlain(key, true)) {
                    return false;
                }
                if (!flowSpace()) return fail(out.begin, open + " is not closed");
                Json value;
                value.begin = value.end = at;
                bool ok = true;
                if (s[at] == ':') {
                    ++at;
                    if (!flowSpace()) return fail(out.begin, open + " is not closed");
                    if (s[at] != ',' && s[at] != close) ok = flowNode(value);
                    else value.begin = value.end = at;
                }
                for (const auto& field : out.fields) {
                    if (field.first == key.text) return fail(keyAt, "the key `" + key.text + "` appears twice");
                }
                out.fields.emplace_back(std::move(key.text), std::move(value));
                if (!ok) return false;
            }
            if (!flowSpace()) return fail(out.begin, open + " is not closed");
            if (s[at] == ',') {
                ++at;
                continue;
            }
            if (s[at] != close) return fail(at, std::string("expected `,` or `") + close + "`");
            break;
        }
        out.end = ++at;
        return true;
    }

    bool flowNode(Json& out) {
        out.begin = out.end = at;
        char c = s[at];
        bool ok = c == '[' || c == '{'                ? flow(out)
                : c == '"' || c == '\''               ? quoted(out)
                : c == '|' || c == '>'                ? fail(at, "a `|` or `>` block cannot be inside [ ] or { }")
                : c == '-' && spaceAfter(at + 1)      ? fail(at, "a `- ` list cannot be inside [ ] or { }")
                                                      : plainStart(at) && flowPlain(out, false);
        if (!ok) out.end = std::max(out.end, error.offset);
        return ok;
    }

    // A value without quotes inside [ ] or { }: it stops at , [ ] { }, at a
    // `: ` and at a comment, and may go over lines
    bool flowPlain(Json& out, bool key) {
        out.begin = at;
        std::string text;
        while (at < s.size()) {
            char c = s[at];
            if (isFlowIndicator(c)) break;
            if (c == ':' && (spaceAfter(at + 1) || isFlowIndicator(peek(at + 1)))) break;
            if (c == '#' && at > out.begin && isBlank(s[at - 1])) break;
            if (isBreak(c)) {
                size_t i = at, breaks = 0;
                do {
                    i = nextLine(i);
                    ++breaks;
                    while (i < s.size() && isBlank(s[i])) ++i;
                } while (i < s.size() && isBreak(s[i]));
                if (i >= s.size() || isFlowIndicator(s[i]) || s[i] == '#' || s[i] == ':' || marker(lineStart(i))) break;
                while (!text.empty() && isBlank(text.back())) text.pop_back();
                if (breaks == 1) text += ' ';
                else text.append(breaks - 1, '\n');
                at = i;
                continue;
            }
            text += c;
            ++at;
        }
        while (!text.empty() && isBlank(text.back())) text.pop_back();
        out.end = at;
        if (text.empty()) return fail(at, "expected a value");
        if (!key) {
            typed(out, std::move(text));
        } else {
            out.type = Json::String;
            out.text = std::move(text);
        }
        return true;
    }
};

}  // namespace

bool parse(std::string_view text, json::Json& out, Error& error) {
    Reader r;
    r.s = text;
    out = Json();
    if (text.substr(0, 3) == "\xEF\xBB\xBF") r.at = r.first = 3;
    if (r.nextContent()) {
        if (r.s[r.at] == '%') {
            r.fail(r.at, "YAML directives (%) are not supported");
        } else {
            bool content = true;
            if (r.marker(r.at, '-')) {
                // The document may start on the marker's line: --- |
                r.at += 3;
                if (r.restEmpty(r.at)) {
                    content = r.nextContent();
                } else {
                    while (isBlank(r.s[r.at])) ++r.at;
                }
            }
            if (content && !r.failed && !r.marker(r.at)) r.blockNode(-1, out);
            bool ended = false;
            if (!r.failed && r.nextContent() && r.marker(r.at, '.')) {
                r.at = r.nextLine(r.at);
                ended = true;
            }
            if (!r.failed && r.nextContent()) {
                bool another = ended || r.marker(r.at, '-') || r.s[r.at] == '%';
                r.fail(r.at, another ? "a second YAML document; only the first is read"
                                     : "unexpected text; check how it is indented");
            }
        }
    }
    error = r.failed ? r.error : Error();
    return !r.failed;
}

bool frontMatter(std::string_view document, size_t& begin, size_t& end) {
    size_t at = document.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0;
    size_t eol = document.find('\n', at);
    if (eol == std::string_view::npos) return false;
    std::string_view opener = document.substr(at, eol - at);
    if (!opener.empty() && opener.back() == '\r') opener.remove_suffix(1);
    if (opener != "---") return false;
    for (size_t line = eol + 1; line <= document.size();) {
        size_t next = document.find('\n', line);
        std::string_view text = document.substr(line, (next == std::string_view::npos ? document.size() : next) - line);
        if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
        if (text == "---" || text == "...") {
            begin = eol + 1;
            end = line;
            return true;
        }
        if (next == std::string_view::npos) break;
        line = next + 1;
    }
    return false;
}

}
