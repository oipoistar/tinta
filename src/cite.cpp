// Pandoc's citation syntax (cite.h): citations found in text, and the
// locators that follow their keys, read as pandoc and citeproc read them.

#pragma optimize("s", on)

#include "cite.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

namespace cite {
namespace {

const size_t npos = std::string_view::npos;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool oneOf(char c, const char* set) { return c && std::strchr(set, c); }
char lowerChar(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

// The code point at `at` and its length in bytes
uint32_t codeAt(std::string_view s, size_t at, size_t& length) {
    unsigned char c = (unsigned char)s[at];
    length = 1;
    if (c < 0xC0) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    uint32_t code = c & (0x3F >> extra);
    for (int i = 0; i < extra && at + length < s.size() && ((unsigned char)s[at + length] & 0xC0) == 0x80; ++i) {
        code = (code << 6) | ((unsigned char)s[at + length] & 0x3F);
        ++length;
    }
    return code;
}

// The code point that ends just before `at`
uint32_t codeBefore(std::string_view s, size_t at) {
    size_t start = at - 1;
    while (start > 0 && at - start < 4 && ((unsigned char)s[start] & 0xC0) == 0x80) --start;
    size_t length = 0;
    return codeAt(s, start, length);
}

// Letters and digits of any script, as pandoc's isAlphaNum sees them, near
// enough: whatever is not a space, punctuation or a symbol
bool isAlnum(uint32_t c) {
    if (c < 0x80) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c < 0xC0) return c == 0xAA || c == 0xB2 || c == 0xB3 || c == 0xB5 || c == 0xB9 || c == 0xBA ||
                         (c >= 0xBC && c <= 0xBE);
    if (c == 0xD7 || c == 0xF7 || c == 0xFEFF) return false;
    if ((c >= 0x55A && c <= 0x55F) || (c >= 0x66A && c <= 0x66D)) return false;
    switch (c) {  // punctuation among the letters of their scripts
        case 0x37E: case 0x387: case 0x589: case 0x5BE: case 0x5C0: case 0x5C3: case 0x5F3:
        case 0x5F4: case 0x60C: case 0x61B: case 0x61F: case 0x6D4: case 0x964: case 0x965:
        case 0xE4F: case 0xE5A: case 0xE5B:
            return false;
        default:
            break;
    }
    if (c >= 0x2000 && c <= 0x2BFF) return false;  // punctuation, signs, arrows, maths
    if (c >= 0x2E00 && c <= 0x2E7F) return false;
    if (c >= 0x3000 && c <= 0x303F) return c >= 0x3005 && c <= 0x3007;  // CJK punctuation
    if (c >= 0xE000 && c <= 0xF8FF) return false;
    if ((c >= 0xFE10 && c <= 0xFE1F) || (c >= 0xFE30 && c <= 0xFE6F)) return false;
    if (c >= 0xFF00 && c <= 0xFFEF) {
        return (c >= 0xFF10 && c <= 0xFF19) || (c >= 0xFF21 && c <= 0xFF3A) ||
               (c >= 0xFF41 && c <= 0xFF5A) || (c >= 0xFF66 && c <= 0xFFDC);
    }
    if (c >= 0xFFF0 && c <= 0xFFFF) return false;
    if (c >= 0x1F000 && c <= 0x1FAFF) return false;  // emoji and other pictures
    return true;
}

bool isKeyChar(uint32_t c) { return isAlnum(c) || c == '_'; }

// Past an escaped character or a code span at `i`
size_t skip(std::string_view s, size_t i) {
    if (s[i] == '\\') return std::min(i + 2, s.size());
    size_t run = 0;
    while (i + run < s.size() && s[i + run] == '`') ++run;
    for (size_t j = i + run; j < s.size();) {
        if (s[j] != '`') {
            ++j;
            continue;
        }
        size_t k = j;
        while (k < s.size() && s[k] == '`') ++k;
        if (k - j == run) return k;
        j = k;
    }
    return i + run;  // backticks nothing closes are text
}

// Whether a blank line follows the line break at `i`
bool blankLineAfter(std::string_view s, size_t i) {
    for (size_t j = i + 1; j < s.size(); ++j) {
        if (s[j] == '\n') return true;
        if (s[j] != ' ' && s[j] != '\t' && s[j] != '\r') return false;
    }
    return false;
}

// The bracket that closes the one at `open`, past escapes, code and nested
// brackets; npos when a blank line or the end comes first
size_t closingBracket(std::string_view s, size_t open) {
    int depth = 0;
    size_t i = open;
    while (i < s.size()) {
        char c = s[i];
        if (c == '\\' || c == '`') {
            i = skip(s, i);
            continue;
        }
        if (c == '[') ++depth;
        else if (c == ']' && --depth == 0) return i;
        else if (c == '\n' && blankLineAfter(s, i)) return npos;
        ++i;
    }
    return npos;
}

// s[from, to) cut at the semicolons outside brackets, braces and code
std::vector<std::pair<size_t, size_t>> pieces(std::string_view s, size_t from, size_t to) {
    std::vector<std::pair<size_t, size_t>> out;
    int brackets = 0, braces = 0;
    size_t start = from, i = from;
    while (i < to) {
        char c = s[i];
        if (c == '\\' || c == '`') {
            i = std::min(skip(s, i), to);
            continue;
        }
        if (c == '[') ++brackets;
        else if (c == ']') --brackets;
        else if (c == '{') ++braces;
        else if (c == '}' && braces > 0) --braces;
        else if (c == ';' && brackets == 0 && braces == 0) {
            out.emplace_back(start, i);
            start = i + 1;
        }
        ++i;
    }
    out.emplace_back(start, to);
    return out;
}

// The end of a key from `at`: a letter, digit or _ first, then those and
// the punctuation :.#$%&-+?<>~/ inside it, never at its end
size_t keyEnd(std::string_view s, size_t at, size_t limit) {
    if (at >= limit) return at;
    size_t length = 0;
    uint32_t first = codeAt(s, at, length);
    if (!isKeyChar(first) && first != '*') return at;  // @* is every key, for nocite
    size_t i = at + length;
    while (i < limit) {
        uint32_t c = codeAt(s, i, length);
        if (isKeyChar(c)) {
            i += length;
            continue;
        }
        if (c < 0x80 && oneOf((char)c, ":.#$%&-+?<>~/") && i + 1 < limit) {
            size_t nextLength = 0;
            uint32_t next = codeAt(s, i + 1, nextLength);
            // https:// keeps its colon and slashes
            if (isKeyChar(next) || ((c == ':' || c == '/') && next == '/')) {
                ++i;
                continue;
            }
        }
        break;
    }
    return i;
}

// The key after the @ at `at`: @doe2024 or @{any key}
bool parseKey(std::string_view s, size_t at, size_t limit, std::string& key, size_t& after) {
    size_t i = at + 1;
    if (i < limit && s[i] == '{') {
        int depth = 0;
        for (size_t j = i; j < limit; ++j) {
            char c = s[j];
            if (c == '\n') return false;
            if (c == '{') {
                ++depth;
            } else if (c == '}' && --depth == 0) {
                std::string_view inner = trim(s.substr(i + 1, j - i - 1));
                if (inner.empty()) return false;
                key = std::string(inner);
                after = j + 1;
                return true;
            }
        }
        return false;
    }
    size_t end = keyEnd(s, i, limit);
    if (end == i) return false;
    key = std::string(s.substr(i, end - i));
    after = end;
    return true;
}

// Whether a citation may start with the @ (or -@) at `at`: not after a
// letter or digit, as in an e-mail address
bool startsCitation(std::string_view s, size_t at) {
    return at == 0 || !isAlnum(codeBefore(s, at));
}

// The first key in s[from, to): where its citation starts (its @, or the -
// before it), its key and where the key ends. npos when there is none
size_t firstKey(std::string_view s, size_t from, size_t to, std::string& key, size_t& after) {
    int brackets = 0;
    size_t i = from;
    while (i < to) {
        char c = s[i];
        if (c == '\\' || c == '`') {
            i = skip(s, i);
            continue;
        }
        if (c == '[') {
            ++brackets;
        } else if (c == ']') {
            --brackets;
        } else if (c == '@' && brackets == 0 && startsCitation(s, i) && parseKey(s, i, to, key, after)) {
            if (i > from && s[i - 1] == '-' && startsCitation(s, i - 1)) return i - 1;
            return i;
        }
        ++i;
    }
    return npos;
}

// One citation in brackets: prefix, key, then the locator and suffix
bool citationItem(std::string_view s, size_t from, size_t to, Item& item) {
    std::string key;
    size_t after = 0;
    size_t start = firstKey(s, from, to, key, after);
    if (start == npos) return false;
    item.id = key;
    item.suppressAuthor = s[start] == '-';
    item.prefix = std::string(trim(s.substr(from, start - from)));
    splitLocator(s.substr(after, to - after), item);
    return true;
}

// [see @doe, p. 4; also @roe] with its opening bracket at `open`
bool bracketed(std::string_view s, size_t open, Citation& citation) {
    if (open > 0 && s[open - 1] == '!') return false;              // an image
    if (open + 1 < s.size() && s[open + 1] == '^') return false;  // a footnote
    size_t close = closingBracket(s, open);
    if (close == npos) return false;
    citation = Citation{};
    for (const auto& piece : pieces(s, open + 1, close)) {
        Item item;
        if (!citationItem(s, piece.first, piece.second, item)) return false;
        citation.items.push_back(std::move(item));
    }
    citation.begin = open;
    citation.end = close + 1;
    return true;
}

// @doe, -@doe, and either followed by [p. 4] or by [more; citations]
bool bare(std::string_view s, size_t at, const std::function<bool(std::string_view)>& cites,
          Citation& citation) {
    bool suppress = s[at] == '-';
    size_t mark = suppress ? at + 1 : at;
    if (!startsCitation(s, at)) return false;
    std::string key;
    size_t after = 0;
    if (!parseKey(s, mark, s.size(), key, after)) return false;
    if (cites && !cites(key)) return false;
    Item first;
    first.id = key;
    first.suppressAuthor = suppress;
    citation = Citation{};
    citation.authorInText = !suppress;
    citation.begin = at;
    citation.end = after;
    // Spaces and at most one line break, then a bracket
    size_t j = after;
    while (j < s.size() && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r')) ++j;
    if (j < s.size() && s[j] == '\n' && !blankLineAfter(s, j)) {
        ++j;
        while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
    }
    if (j < s.size() && s[j] == '[') {
        Citation more;
        if (bracketed(s, j, more)) {
            citation.items.push_back(std::move(first));
            for (Item& item : more.items) citation.items.push_back(std::move(item));
            citation.end = more.end;
            return true;
        }
        // [p. 33] holds the locator; a link's [text](url) or [text][ref] does not
        size_t close = j + 1 < s.size() && s[j + 1] == '^' ? npos : closingBracket(s, j);
        if (close != npos && !(close + 1 < s.size() && (s[close + 1] == '[' || s[close + 1] == '('))) {
            std::vector<std::pair<size_t, size_t>> parts = pieces(s, j + 1, close);
            std::vector<Item> items(1, first);
            splitLocator(s.substr(parts[0].first, parts[0].second - parts[0].first), items[0]);
            bool ok = true;
            for (size_t p = 1; p < parts.size() && ok; ++p) {
                Item item;
                ok = citationItem(s, parts[p].first, parts[p].second, item);
                items.push_back(std::move(item));
            }
            if (ok) {
                citation.items = std::move(items);
                citation.end = close + 1;
                return true;
            }
        }
    }
    citation.items.push_back(std::move(first));
    return true;
}

// Locator terms in English, as the en-US locale has them, and the CSL
// locator each names. "number" is pandoc's older word for an issue
const char* const terms[][2] = {
    {"act", "act"}, {"acts", "act"},
    {"appendix", "appendix"}, {"appendices", "appendix"}, {"app.", "appendix"}, {"apps.", "appendix"},
    {"article", "article-locator"}, {"articles", "article-locator"}, {"art.", "article-locator"},
    {"arts.", "article-locator"},
    {"book", "book"}, {"books", "book"}, {"bk.", "book"}, {"bks.", "book"},
    {"canon", "canon"}, {"canons", "canon"}, {"can.", "canon"}, {"cann.", "canon"},
    {"chapter", "chapter"}, {"chapters", "chapter"}, {"chap.", "chapter"}, {"chaps.", "chapter"},
    {"c.", "chapter"}, {"cc.", "chapter"},
    {"column", "column"}, {"columns", "column"}, {"col.", "column"}, {"cols.", "column"},
    {"location", "elocation"}, {"locations", "elocation"}, {"loc.", "elocation"}, {"locs.", "elocation"},
    {"equation", "equation"}, {"equations", "equation"}, {"eq.", "equation"}, {"eqq.", "equation"},
    {"figure", "figure"}, {"figures", "figure"}, {"fig.", "figure"}, {"figs.", "figure"},
    {"folio", "folio"}, {"folios", "folio"}, {"fol.", "folio"}, {"fols.", "folio"},
    {"issue", "issue"}, {"issues", "issue"}, {"number", "issue"}, {"numbers", "issue"},
    {"no.", "issue"}, {"nos.", "issue"},
    {"line", "line"}, {"lines", "line"}, {"l.", "line"}, {"ll.", "line"},
    {"note", "note"}, {"notes", "note"}, {"n.", "note"}, {"nn.", "note"},
    {"opus", "opus"}, {"opera", "opus"}, {"op.", "opus"}, {"opp.", "opus"},
    {"page", "page"}, {"pages", "page"}, {"p.", "page"}, {"pp.", "page"},
    {"paragraph", "paragraph"}, {"paragraphs", "paragraph"}, {"para.", "paragraph"},
    {"paras.", "paragraph"}, {"\xC2\xB6", "paragraph"}, {"\xC2\xB6\xC2\xB6", "paragraph"},
    {"part", "part"}, {"parts", "part"}, {"pt.", "part"}, {"pts.", "part"},
    {"rule", "rule"}, {"rules", "rule"}, {"r.", "rule"}, {"rr.", "rule"},
    {"scene", "scene"}, {"scenes", "scene"}, {"sc.", "scene"}, {"scs.", "scene"},
    {"section", "section"}, {"sections", "section"}, {"sec.", "section"}, {"secs.", "section"},
    {"\xC2\xA7", "section"}, {"\xC2\xA7\xC2\xA7", "section"},
    {"sub verbo", "sub-verbo"}, {"sub verbis", "sub-verbo"}, {"s.v.", "sub-verbo"},
    {"s.vv.", "sub-verbo"},
    {"supplement", "supplement"}, {"supplements", "supplement"}, {"supp.", "supplement"},
    {"supps.", "supplement"},
    {"table", "table"}, {"tables", "table"}, {"tbl.", "table"}, {"tbls.", "table"},
    {"title", "title-locator"}, {"titles", "title-locator"}, {"tit.", "title-locator"},
    {"titt.", "title-locator"},
    {"verse", "verse"}, {"verses", "verse"}, {"v.", "verse"}, {"vv.", "verse"},
    {"volume", "volume"}, {"volumes", "volume"}, {"vol.", "volume"}, {"vols.", "volume"},
};

// The term at `at`, when one is there and ends where a word does
size_t termAt(std::string_view s, size_t at, const char* term) {
    size_t n = std::strlen(term);
    if (s.size() - at < n) return 0;
    for (size_t i = 0; i < n; ++i) {
        if (lowerChar(s[at + i]) != term[i]) return 0;
    }
    size_t length = 0;
    if (oneOf(term[n - 1], "abcdefghijklmnopqrstuvwxyz") && at + n < s.size() &&
        isAlnum(codeAt(s, at + n, length))) {
        return 0;  // "page" is not the start of "pageant"
    }
    return n;
}

bool isRoman(std::string_view word) {
    if (word.empty()) return false;
    std::string w;
    for (char c : word) w += lowerChar(c);
    size_t i = 0;
    auto run = [&](char c, size_t most) {
        size_t n = 0;
        while (i < w.size() && w[i] == c && n < most) {
            ++i;
            ++n;
        }
    };
    // m{0,4} (cm|cd|d?c{0,3}) (xc|xl|l?x{0,3}) (ix|iv|v?i{0,3})
    run('m', 4);
    const char* const digits[3][3] = {{"cm", "cd", "d"}, {"xc", "xl", "l"}, {"ix", "iv", "v"}};
    const char ones[3] = {'c', 'x', 'i'};
    for (int place = 0; place < 3; ++place) {
        if (w.compare(i, 2, digits[place][0]) == 0 || w.compare(i, 2, digits[place][1]) == 0) {
            i += 2;
            continue;
        }
        if (i < w.size() && w[i] == digits[place][2][0]) ++i;
        run(ones[place], 3);
    }
    return i == w.size();
}

// A character a locator unit holds: anything but spaces and punctuation,
// though - and an en dash make ranges and : a volume:page
bool unitChar(std::string_view s, size_t at, size_t& length) {
    uint32_t c = codeAt(s, at, length);
    return isAlnum(c) || c == 0x2013 || (c < 0x80 && oneOf((char)c, "-:+<=>|~$^`"));
}

// One unit: 33, 33-35, xiv, 3:16
bool unit(std::string_view s, size_t& at, bool& digitLike, bool& hasDigit) {
    size_t k = at, length = 0;
    while (k < s.size() && unitChar(s, k, length)) k += length;
    if (k == at) return false;
    std::string_view u = s.substr(at, k - at);
    hasDigit = std::any_of(u.begin(), u.end(), [](char c) { return c >= '0' && c <= '9'; });
    size_t dash = u.find_first_of("-");
    size_t enDash = u.find("\xE2\x80\x93");
    digitLike = hasDigit || isRoman(u.substr(0, std::min(dash, enDash)));
    at = k;
    return true;
}

// Units joined by periods: 1.2.3. A period that ends it is not its own
bool pageSequence(std::string_view s, size_t& at, bool& digitLike, bool& hasDigit) {
    size_t k = at;
    if (!unit(s, k, digitLike, hasDigit)) return false;
    while (k < s.size() && s[k] == '.') {
        size_t m = k + 1;
        bool d = false, h = false;
        if (!unit(s, m, d, h)) break;
        digitLike = digitLike || d;
        hasDigit = hasDigit || h;
        k = m;
    }
    at = k;
    return true;
}

// One locator word: page sequences and bracketed ones, as in 34(1)(i) and
// [17]-[18]
bool locatorWord(std::string_view s, size_t& at, bool& digitLike, bool& hasDigit) {
    size_t start = at;
    digitLike = hasDigit = false;
    for (;;) {
        bool d = false, h = false;
        if (at < s.size() && oneOf(s[at], "([{")) {
            char close = s[at] == '(' ? ')' : s[at] == '[' ? ']' : '}';
            size_t k = at + 1;
            pageSequence(s, k, d, h);
            if (k >= s.size() || s[k] != close) break;
            at = k + 1;
        } else if (!pageSequence(s, at, d, h)) {
            break;
        }
        digitLike = digitLike || d;
        hasDigit = hasDigit || h;
    }
    return at > start;
}

// Locator words from `at`, each after a comma or a space; with `digits`
// each must have a digit, else a digit or a roman numeral. Sets `end`
bool locatorWords(std::string_view s, size_t at, bool digits, size_t& end) {
    bool d = false, h = false;
    size_t k = at;
    if (!locatorWord(s, k, d, h) || !(digits ? h : d)) return false;
    end = k;
    for (;;) {
        size_t m = end;
        if (m < s.size() && s[m] == ',') ++m;
        while (m < s.size() && isSpace(s[m])) ++m;
        if (m == end) break;
        size_t n = m;
        if (!locatorWord(s, n, d, h) || !(digits ? h : d)) break;
        end = n;
    }
    return true;
}

// The longest term at `at`, and the CSL locator it names
size_t labelAt(std::string_view s, size_t at, const char*& label, bool needsWord, size_t& wordsEnd) {
    size_t best = 0;
    for (const auto& term : terms) {
        size_t n = termAt(s, at, term[0]);
        if (n <= best) continue;
        if (needsWord) {
            size_t k = at + n;
            while (k < s.size() && isSpace(s[k])) ++k;
            size_t end = 0;
            if (!locatorWords(s, k, false, end)) continue;
            wordsEnd = end;
        }
        best = n;
        label = term[1];
    }
    return best;
}

// Trailing space trimmed, leading space kept as one: " and passim"
std::string tidySuffix(std::string_view s) {
    size_t lead = 0;
    while (lead < s.size() && isSpace(s[lead])) ++lead;
    std::string_view body = trim(s);
    if (body.empty()) return {};
    return (lead ? " " : "") + std::string(body);
}

}  // namespace

std::vector<Citation> find(std::string_view text, const std::function<bool(std::string_view key)>& cites) {
    std::vector<Citation> found;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c == '\\' || c == '`') {
            i = skip(text, i);
            continue;
        }
        Citation citation;
        if (c == '[' && bracketed(text, i, citation)) {
            i = citation.end;
            found.push_back(std::move(citation));
            continue;
        }
        if ((c == '@' || (c == '-' && i + 1 < text.size() && text[i + 1] == '@')) && bare(text, i, cites, citation)) {
            i = citation.end;
            found.push_back(std::move(citation));
            continue;
        }
        ++i;
    }
    return found;
}

void splitLocator(std::string_view afterKey, Item& item) {
    item.locator.clear();
    item.label.clear();
    item.suffix.clear();
    std::string_view s = afterKey;
    size_t at = 0;
    if (at < s.size() && s[at] == ',') ++at;
    while (at < s.size() && isSpace(s[at])) ++at;
    // {xiv-xvi} or {pp. iv, vi}: a locator as written, a label inside it or page
    if (at < s.size() && s[at] == '{') {
        int depth = 0;
        for (size_t i = at; i < s.size(); ++i) {
            if (s[i] == '{' || s[i] == '[') {
                ++depth;
            } else if ((s[i] == '}' || s[i] == ']') && --depth == 0) {
                if (s[i] != '}') break;
                std::string_view inner = trim(s.substr(at + 1, i - at - 1));
                const char* label = "page";
                size_t wordsEnd = 0;
                size_t n = labelAt(inner, 0, label, false, wordsEnd);
                if (!n) label = "page";
                item.locator = std::string(trim(inner.substr(n)));
                item.label = item.locator.empty() ? "" : label;  // [@smith{}, 99 years later]
                item.suffix = tidySuffix(s.substr(i + 1));
                return;
            }
        }
    }
    // p. 33-35, chap. 2, 33, xiv with a label
    const char* label = nullptr;
    size_t end = 0;
    size_t n = labelAt(s, at, label, true, end);
    if (!n && locatorWords(s, at, true, end)) label = "page";  // a bare number is a page
    if (label) {
        size_t from = at + n;
        item.label = label;
        item.locator = std::string(trim(s.substr(from, end - from)));
        item.suffix = tidySuffix(s.substr(end));
        return;
    }
    // No locator: the suffix as written, with a comma before a word that
    // would otherwise run on from the key
    item.suffix = tidySuffix(s);
    if (!item.suffix.empty()) {
        size_t length = 0;
        uint32_t first = codeAt(item.suffix, 0, length);
        if (first != ' ' && isAlnum(first)) item.suffix = ", " + item.suffix;
    }
}

bool isCrossReference(std::string_view key) {
    for (std::string_view prefix : {"fig:", "tbl:", "eq:", "sec:", "lst:"}) {
        if (key.size() > prefix.size() && lowerChar(key[0]) == prefix[0] &&
            key.substr(1, prefix.size() - 1) == prefix.substr(1)) {
            return true;
        }
    }
    return false;
}

}
