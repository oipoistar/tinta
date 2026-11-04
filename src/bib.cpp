// References for citations (bib.h): the item model, and the BibTeX, CSL-JSON
// and CSL YAML readers that fill it.

#pragma optimize("s", on)

#include "bib.h"
#include "json.h"
#include "yaml.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace bib {

const std::string* Item::field(std::string_view variable) const {
    auto it = fields.find(std::string(variable));
    return it == fields.end() || it->second.empty() ? nullptr : &it->second;
}

const std::vector<Name>* Item::namesOf(std::string_view variable) const {
    auto it = names.find(std::string(variable));
    return it == names.end() || it->second.empty() ? nullptr : &it->second;
}

const Date* Item::date(std::string_view variable) const {
    auto it = dates.find(std::string(variable));
    return it == dates.end() || it->second.empty() ? nullptr : &it->second;
}

const Item* Library::find(std::string_view id) const {
    auto it = index.find(std::string(id));
    return it == index.end() ? nullptr : &items[it->second];
}

void Library::add(Item item) {
    if (index.count(item.id)) {
        diagnostics.push_back({item.source, item.line, "duplicate key @" + item.id + "; the first is used"});
        return;
    }
    index.emplace(item.id, items.size());
    items.push_back(std::move(item));
}

namespace {

// --- Text ----------------------------------------------------------------

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool oneOf(char c, const char* set) { return c && std::strchr(set, c); }
char lowerChar(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lowerChar(c);
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

bool startsWithNoCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (lowerChar(s[i]) != lowerChar(prefix[i])) return false;
    }
    return true;
}

bool in(std::string_view value, std::initializer_list<const char*> list) {
    for (const char* item : list) {
        if (value == item) return true;
    }
    return false;
}

// The code point at `at`, moving past it; a stray byte reads as itself
uint32_t nextCode(std::string_view s, size_t& at) {
    unsigned char c = (unsigned char)s[at++];
    if (c < 0xC0) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    uint32_t code = c & (0x3F >> extra);
    for (int i = 0; i < extra && at < s.size() && ((unsigned char)s[at] & 0xC0) == 0x80; ++i) {
        code = (code << 6) | ((unsigned char)s[at++] & 0x3F);
    }
    return code;
}

// Lower-case letters of the Latin, Greek and Cyrillic alphabets. Scripts
// without case, like Chinese, have none
bool isLowerCode(uint32_t c) {
    if (c < 0x80) return c >= 'a' && c <= 'z';
    if (c >= 0xDF && c <= 0xFF) return c != 0xF7;
    if (c >= 0x100 && c <= 0x17F) {
        if (c == 0x130 || c == 0x178) return false;
        if (c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return true;
        bool odd = (c & 1) != 0;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return !odd;
        return odd;
    }
    if (c >= 0x1CD && c <= 0x1DC) return (c & 1) == 0;
    if ((c >= 0x1DE && c <= 0x1EF) || (c >= 0x1F8 && c <= 0x233)) return (c & 1) != 0;
    if (c >= 0x250 && c <= 0x2AF) return true;
    if (c >= 0x3AC && c <= 0x3CE) return true;
    if (c >= 0x430 && c <= 0x45F) return true;
    if (c >= 0x460 && c <= 0x4FF) return (c & 1) != 0;
    if (c >= 0x1E96 && c <= 0x1E9F) return c != 0x1E9E;
    if (c >= 0x1E00 && c <= 0x1EFF) return (c & 1) != 0;
    return c >= 0xFF41 && c <= 0xFF5A;
}

// Whether rich text has a letter of any script, past its tags and entities:
// not only digits, spaces, punctuation and signs
bool hasLetters(std::string_view text) {
    size_t at = 0;
    while (at < text.size()) {
        if (text[at] == '<' || text[at] == '&') {
            size_t end = text.find(text[at] == '<' ? '>' : ';', at);
            at = end == std::string_view::npos ? text.size() : end + 1;
            continue;
        }
        uint32_t c = nextCode(text, at);
        if (c < 0x80 ? isLetter((char)c)
                     : c >= 0xC0 && c != 0xD7 && c != 0xF7 && !(c >= 0x2000 && c <= 0x2BFF) &&
                           !(c >= 0x3000 && c <= 0x303F) && !(c >= 0xFF00 && c <= 0xFF0F)) {
            return true;
        }
    }
    return false;
}

void appendEscaped(std::string& out, std::string_view text) {
    for (char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
}

std::string escaped(std::string_view text) {
    std::string out;
    appendEscaped(out, text);
    return out;
}

// Line numbers for offsets into a text
class Lines {
public:
    explicit Lines(std::string_view text) {
        starts.push_back(0);
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') starts.push_back(i + 1);
        }
    }
    int line(size_t offset) const {
        return (int)(std::upper_bound(starts.begin(), starts.end(), offset) - starts.begin());
    }

private:
    std::vector<size_t> starts;
};

// A URL or DOI as written, without the escapes and braces TeX wants:
// \url{http://x.org/a\_b} reads http://x.org/a_b
std::string verbatim(std::string_view raw) {
    raw = trim(raw);
    if (startsWithNoCase(raw, "\\url{") && raw.back() == '}') raw = raw.substr(5, raw.size() - 6);
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size() && oneOf(raw[i + 1], "#$%&~_^\\{}")) {
            out += raw[++i];
        } else if (c != '{' && c != '}' && !isSpace(c)) {
            out += c;
        }
    }
    return out;
}

// --- LaTeX ---------------------------------------------------------------

// Letters with a precomposed form under each accent command, in pairs of
// letter and code point
const uint16_t acute[] = {
    'A', 0xC1, 'a', 0xE1, 'E', 0xC9, 'e', 0xE9, 'I', 0xCD, 'i', 0xED, 'O', 0xD3, 'o', 0xF3,
    'U', 0xDA, 'u', 0xFA, 'Y', 0xDD, 'y', 0xFD, 'C', 0x106, 'c', 0x107, 'L', 0x139, 'l', 0x13A,
    'N', 0x143, 'n', 0x144, 'R', 0x154, 'r', 0x155, 'S', 0x15A, 's', 0x15B, 'Z', 0x179, 'z', 0x17A,
    'G', 0x1F4, 'g', 0x1F5, 'K', 0x1E30, 'k', 0x1E31, 'M', 0x1E3E, 'm', 0x1E3F, 'P', 0x1E54,
    'p', 0x1E55, 'W', 0x1E82, 'w', 0x1E83};
const uint16_t grave[] = {
    'A', 0xC0, 'a', 0xE0, 'E', 0xC8, 'e', 0xE8, 'I', 0xCC, 'i', 0xEC, 'O', 0xD2, 'o', 0xF2,
    'U', 0xD9, 'u', 0xF9, 'N', 0x1F8, 'n', 0x1F9, 'W', 0x1E80, 'w', 0x1E81, 'Y', 0x1EF2, 'y', 0x1EF3};
const uint16_t circumflex[] = {
    'A', 0xC2, 'a', 0xE2, 'E', 0xCA, 'e', 0xEA, 'I', 0xCE, 'i', 0xEE, 'O', 0xD4, 'o', 0xF4,
    'U', 0xDB, 'u', 0xFB, 'C', 0x108, 'c', 0x109, 'G', 0x11C, 'g', 0x11D, 'H', 0x124, 'h', 0x125,
    'J', 0x134, 'j', 0x135, 'S', 0x15C, 's', 0x15D, 'W', 0x174, 'w', 0x175, 'Y', 0x176, 'y', 0x177,
    'Z', 0x1E90, 'z', 0x1E91};
const uint16_t diaeresis[] = {
    'A', 0xC4, 'a', 0xE4, 'E', 0xCB, 'e', 0xEB, 'I', 0xCF, 'i', 0xEF, 'O', 0xD6, 'o', 0xF6,
    'U', 0xDC, 'u', 0xFC, 'Y', 0x178, 'y', 0xFF, 'H', 0x1E26, 'h', 0x1E27, 'W', 0x1E84, 'w', 0x1E85,
    'X', 0x1E8C, 'x', 0x1E8D, 't', 0x1E97};
const uint16_t tilde[] = {
    'A', 0xC3, 'a', 0xE3, 'N', 0xD1, 'n', 0xF1, 'O', 0xD5, 'o', 0xF5, 'I', 0x128, 'i', 0x129,
    'U', 0x168, 'u', 0x169, 'E', 0x1EBC, 'e', 0x1EBD, 'Y', 0x1EF8, 'y', 0x1EF9, 'V', 0x1E7C, 'v', 0x1E7D};
const uint16_t macron[] = {
    'A', 0x100, 'a', 0x101, 'E', 0x112, 'e', 0x113, 'I', 0x12A, 'i', 0x12B, 'O', 0x14C, 'o', 0x14D,
    'U', 0x16A, 'u', 0x16B, 'Y', 0x232, 'y', 0x233, 'G', 0x1E20, 'g', 0x1E21};
const uint16_t dotAbove[] = {
    'C', 0x10A, 'c', 0x10B, 'E', 0x116, 'e', 0x117, 'G', 0x120, 'g', 0x121, 'I', 0x130, 'Z', 0x17B,
    'z', 0x17C, 'A', 0x226, 'a', 0x227, 'O', 0x22E, 'o', 0x22F, 'B', 0x1E02, 'b', 0x1E03, 'D', 0x1E0A,
    'd', 0x1E0B, 'F', 0x1E1E, 'f', 0x1E1F, 'H', 0x1E22, 'h', 0x1E23, 'M', 0x1E40, 'm', 0x1E41,
    'N', 0x1E44, 'n', 0x1E45, 'P', 0x1E56, 'p', 0x1E57, 'R', 0x1E58, 'r', 0x1E59, 'S', 0x1E60,
    's', 0x1E61, 'T', 0x1E6A, 't', 0x1E6B, 'W', 0x1E86, 'w', 0x1E87, 'X', 0x1E8A, 'x', 0x1E8B,
    'Y', 0x1E8E, 'y', 0x1E8F};
const uint16_t breve[] = {
    'A', 0x102, 'a', 0x103, 'E', 0x114, 'e', 0x115, 'G', 0x11E, 'g', 0x11F, 'I', 0x12C, 'i', 0x12D,
    'O', 0x14E, 'o', 0x14F, 'U', 0x16C, 'u', 0x16D};
const uint16_t caron[] = {
    'C', 0x10C, 'c', 0x10D, 'D', 0x10E, 'd', 0x10F, 'E', 0x11A, 'e', 0x11B, 'L', 0x13D, 'l', 0x13E,
    'N', 0x147, 'n', 0x148, 'R', 0x158, 'r', 0x159, 'S', 0x160, 's', 0x161, 'T', 0x164, 't', 0x165,
    'Z', 0x17D, 'z', 0x17E, 'A', 0x1CD, 'a', 0x1CE, 'I', 0x1CF, 'i', 0x1D0, 'O', 0x1D1, 'o', 0x1D2,
    'U', 0x1D3, 'u', 0x1D4, 'G', 0x1E6, 'g', 0x1E7, 'K', 0x1E8, 'k', 0x1E9, 'j', 0x1F0, 'H', 0x21E,
    'h', 0x21F};
const uint16_t doubleAcute[] = {'O', 0x150, 'o', 0x151, 'U', 0x170, 'u', 0x171};
const uint16_t cedilla[] = {
    'C', 0xC7, 'c', 0xE7, 'G', 0x122, 'g', 0x123, 'K', 0x136, 'k', 0x137, 'L', 0x13B, 'l', 0x13C,
    'N', 0x145, 'n', 0x146, 'R', 0x156, 'r', 0x157, 'S', 0x15E, 's', 0x15F, 'T', 0x162, 't', 0x163,
    'E', 0x228, 'e', 0x229, 'D', 0x1E10, 'd', 0x1E11, 'H', 0x1E28, 'h', 0x1E29};
const uint16_t ogonek[] = {
    'A', 0x104, 'a', 0x105, 'E', 0x118, 'e', 0x119, 'I', 0x12E, 'i', 0x12F, 'U', 0x172, 'u', 0x173,
    'O', 0x1EA, 'o', 0x1EB};
const uint16_t ring[] = {'A', 0xC5, 'a', 0xE5, 'U', 0x16E, 'u', 0x16F, 'w', 0x1E98, 'y', 0x1E99};
const uint16_t dotBelow[] = {
    'A', 0x1EA0, 'a', 0x1EA1, 'E', 0x1EB8, 'e', 0x1EB9, 'I', 0x1ECA, 'i', 0x1ECB, 'O', 0x1ECC,
    'o', 0x1ECD, 'U', 0x1EE4, 'u', 0x1EE5, 'Y', 0x1EF4, 'y', 0x1EF5, 'B', 0x1E04, 'b', 0x1E05,
    'D', 0x1E0C, 'd', 0x1E0D, 'H', 0x1E24, 'h', 0x1E25, 'K', 0x1E32, 'k', 0x1E33, 'L', 0x1E36,
    'l', 0x1E37, 'M', 0x1E42, 'm', 0x1E43, 'N', 0x1E46, 'n', 0x1E47, 'R', 0x1E5A, 'r', 0x1E5B,
    'S', 0x1E62, 's', 0x1E63, 'T', 0x1E6C, 't', 0x1E6D, 'V', 0x1E7E, 'v', 0x1E7F, 'W', 0x1E88,
    'w', 0x1E89, 'Z', 0x1E92, 'z', 0x1E93};
const uint16_t barBelow[] = {
    'B', 0x1E06, 'b', 0x1E07, 'D', 0x1E0E, 'd', 0x1E0F, 'K', 0x1E34, 'k', 0x1E35, 'L', 0x1E3A,
    'l', 0x1E3B, 'N', 0x1E48, 'n', 0x1E49, 'R', 0x1E5E, 'r', 0x1E5F, 'T', 0x1E6E, 't', 0x1E6F,
    'Z', 0x1E94, 'z', 0x1E95, 'h', 0x1E96};

struct Accent {
    char command;
    uint16_t mark;  // the combining mark, for letters with no precomposed form
    const uint16_t* pairs;
    size_t size;
};

const Accent accents[] = {
    {'\'', 0x301, acute, std::size(acute)},
    {'`', 0x300, grave, std::size(grave)},
    {'^', 0x302, circumflex, std::size(circumflex)},
    {'"', 0x308, diaeresis, std::size(diaeresis)},
    {'~', 0x303, tilde, std::size(tilde)},
    {'=', 0x304, macron, std::size(macron)},
    {'.', 0x307, dotAbove, std::size(dotAbove)},
    {'u', 0x306, breve, std::size(breve)},
    {'v', 0x30C, caron, std::size(caron)},
    {'H', 0x30B, doubleAcute, std::size(doubleAcute)},
    {'c', 0x327, cedilla, std::size(cedilla)},
    {'k', 0x328, ogonek, std::size(ogonek)},
    {'r', 0x30A, ring, std::size(ring)},
    {'d', 0x323, dotBelow, std::size(dotBelow)},
    {'b', 0x331, barBelow, std::size(barBelow)},
    {'t', 0x361, nullptr, 0},
};

// A letter under an accent: precomposed where Unicode has the pair, else
// followed by the combining mark
std::string accented(char command, uint32_t letter) {
    std::string out;
    for (const Accent& a : accents) {
        if (a.command != command) continue;
        for (size_t i = 0; i + 1 < a.size; i += 2) {
            if (a.pairs[i] == letter) {
                json::appendUtf8(out, a.pairs[i + 1]);
                return out;
            }
        }
        json::appendUtf8(out, letter);
        json::appendUtf8(out, a.mark);
        return out;
    }
    json::appendUtf8(out, letter);
    return out;
}

struct Named {
    const char* name;
    const char* text;
};

// Commands that stand for a letter or a sign
const Named named[] = {
    {"ss", "\xC3\x9F"}, {"SS", "SS"}, {"o", "\xC3\xB8"}, {"O", "\xC3\x98"}, {"ae", "\xC3\xA6"},
    {"AE", "\xC3\x86"}, {"oe", "\xC5\x93"}, {"OE", "\xC5\x92"}, {"l", "\xC5\x82"}, {"L", "\xC5\x81"},
    {"aa", "\xC3\xA5"}, {"AA", "\xC3\x85"}, {"i", "\xC4\xB1"}, {"j", "\xC8\xB7"}, {"dh", "\xC3\xB0"},
    {"DH", "\xC3\x90"}, {"th", "\xC3\xBE"}, {"TH", "\xC3\x9E"}, {"dj", "\xC4\x91"}, {"DJ", "\xC4\x90"},
    {"ng", "\xC5\x8B"}, {"NG", "\xC5\x8A"},
    {"textendash", "\xE2\x80\x93"}, {"textemdash", "\xE2\x80\x94"},
    {"textquoteleft", "\xE2\x80\x98"}, {"textquoteright", "\xE2\x80\x99"},
    {"textquotedblleft", "\xE2\x80\x9C"}, {"textquotedblright", "\xE2\x80\x9D"},
    {"guillemotleft", "\xC2\xAB"}, {"guillemotright", "\xC2\xBB"},
    {"guillemetleft", "\xC2\xAB"}, {"guillemetright", "\xC2\xBB"},
    {"S", "\xC2\xA7"}, {"P", "\xC2\xB6"}, {"textsection", "\xC2\xA7"}, {"textparagraph", "\xC2\xB6"},
    {"copyright", "\xC2\xA9"}, {"textcopyright", "\xC2\xA9"}, {"textregistered", "\xC2\xAE"},
    {"texttrademark", "\xE2\x84\xA2"}, {"dag", "\xE2\x80\xA0"}, {"ddag", "\xE2\x80\xA1"},
    {"textdagger", "\xE2\x80\xA0"}, {"textdaggerdbl", "\xE2\x80\xA1"},
    {"pounds", "\xC2\xA3"}, {"textsterling", "\xC2\xA3"}, {"euro", "\xE2\x82\xAC"},
    {"texteuro", "\xE2\x82\xAC"}, {"textdegree", "\xC2\xB0"}, {"ldots", "\xE2\x80\xA6"},
    {"dots", "\xE2\x80\xA6"}, {"textellipsis", "\xE2\x80\xA6"}, {"textbullet", "\xE2\x80\xA2"},
    {"textperiodcentered", "\xC2\xB7"}, {"textexclamdown", "\xC2\xA1"},
    {"textquestiondown", "\xC2\xBF"}, {"textbackslash", "\\"}, {"textasciitilde", "~"},
    {"textasciicircum", "^"}, {"textbar", "|"}, {"textless", "<"}, {"textgreater", ">"},
    {"textunderscore", "_"}, {"textbraceleft", "{"}, {"textbraceright", "}"},
    {"textquotesingle", "'"}, {"textquotedbl", "\""}, {"slash", "/"},
    {"LaTeX", "LaTeX"}, {"TeX", "TeX"}, {"BibTeX", "BibTeX"},
};

constexpr char smallCaps[] = "<span style=\"font-variant:small-caps;\">";
constexpr char nocaseSpan[] = "<span class=\"nocase\">";

struct Style {
    const char* name;
    const char* open;
    const char* close;
};

// Commands that format their argument
const Style styled[] = {
    {"textit", "<i>", "</i>"}, {"emph", "<i>", "</i>"}, {"textsl", "<i>", "</i>"},
    {"mkbibemph", "<i>", "</i>"}, {"textbf", "<b>", "</b>"}, {"mkbibbold", "<b>", "</b>"},
    {"textsc", smallCaps, "</span>"}, {"textsuperscript", "<sup>", "</sup>"},
    {"mkbibsuperscript", "<sup>", "</sup>"}, {"textsubscript", "<sub>", "</sub>"},
    {"mkbibsubscript", "<sub>", "</sub>"}, {"NoCaseChange", nocaseSpan, "</span>"},
};

// Declarations that format the rest of their group: {\em x}
const Style switched[] = {
    {"em", "<i>", "</i>"}, {"it", "<i>", "</i>"}, {"itshape", "<i>", "</i>"},
    {"sl", "<i>", "</i>"}, {"slshape", "<i>", "</i>"}, {"bf", "<b>", "</b>"},
    {"bfseries", "<b>", "</b>"}, {"sc", smallCaps, "</span>"}, {"scshape", smallCaps, "</span>"},
};

// Commands whose argument never shows, and those that show only the second
const char* const dropped[] = {"noopsort", "nocite", "cite", "citep", "citet", "index",
                               "label", "footnote", "hyphenation", "ref"};
const char* const secondShown[] = {"href", "foreignlanguage", "textcolor", "colorbox"};

// LaTeX to CSL rich text, or to plain text for names, dates and keys
class Tex {
public:
    // `nested`: how deep in other groups the text sits, so that a file of
    // a thousand braces cannot exhaust the stack
    Tex(std::string_view text, bool asRich, int nested = 0) : s(text), rich(asRich), level(nested) {}

    std::string run() {
        group(false);
        return std::move(out);
    }

private:
    struct Switch {
        const char* close;
        size_t mark, inner;
        bool spaced;
    };

    std::string_view s;
    bool rich;
    int level;           // groups open around the one being read
    size_t at = 0;
    std::string out;
    bool space = false;  // a space is due before the next text
    bool any = false;    // text has been written
    int nocase = 0;      // within a span that keeps its case

    void flush() {
        if (space) out += ' ';
        space = false;
    }

    void text(std::string_view t) {
        if (t.empty()) return;
        flush();
        if (rich) appendEscaped(out, t);
        else out += t;
        any = true;
    }

    void code(uint32_t c) {
        std::string t;
        json::appendUtf8(t, c);
        text(t);
    }

    void blank() {
        if (any) space = true;
    }

    // What `body` writes, between two tags; nothing at all when it writes
    // nothing
    template <class Body>
    void wrap(const char* open, const char* close, Body body) {
        size_t mark = out.size();
        bool spaced = space;
        if (rich) {
            flush();
            out += open;
        }
        size_t inner = out.size();
        body();
        if (out.size() == inner) {
            out.resize(mark);
            space = space || spaced;
        } else if (rich) {
            out += close;
        }
    }

    void end(const Switch& sw) {
        if (out.size() == sw.inner) {
            out.resize(sw.mark);
            space = space || sw.spaced;
        } else {
            out += sw.close;
        }
    }

    // The brace that closes the one at `open`, or the end of the text
    size_t matching(size_t open) const {
        int depth = 0;
        for (size_t i = open; i < s.size(); ++i) {
            if (s[i] == '\\') {
                ++i;
            } else if (s[i] == '{') {
                ++depth;
            } else if (s[i] == '}' && --depth == 0) {
                return i;
            }
        }
        return s.size();
    }

    void skipSpace() {
        while (at < s.size() && isSpace(s[at])) ++at;
    }

    // Decodes to the end of the text, or past the brace that closes the group
    void group(bool inGroup) {
        if (level >= 64) {
            // Nested deeper than any real title: the rest as plain text
            size_t end = inGroup ? matching(at - 1) : s.size();
            std::string flat;
            for (char c : s.substr(at, std::min(end, s.size()) - at)) {
                if (c != '{' && c != '}' && c != '\\') flat += isSpace(c) ? ' ' : c;
            }
            text(trim(flat));
            at = std::min(end + 1, s.size());
            return;
        }
        ++level;
        std::vector<Switch> switches;
        while (at < s.size()) {
            char c = s[at];
            if (c == '}') {
                ++at;
                if (inGroup) break;
            } else if (c == '{') {
                ++at;
                braced();
            } else if (c == '\\') {
                command(switches);
            } else if (c == '$') {
                math();
            } else if (c == '~') {
                ++at;
                code(0xA0);
            } else if (c == '-') {
                dashes();
            } else if ((c == '`' || c == '\'') && at + 1 < s.size() && s[at + 1] == c) {
                at += 2;
                code(c == '`' ? 0x201C : 0x201D);
            } else if (isSpace(c)) {
                ++at;
                blank();
            } else {
                size_t from = at++;
                while (at < s.size() && !oneOf(s[at], "{}\\$~-`' \t\n\r\f\v")) ++at;
                text(s.substr(from, at - from));
            }
        }
        for (size_t i = switches.size(); i-- > 0;) end(switches[i]);
        --level;
    }

    // A group in braces. One that starts with a command is a special
    // character ({\"o}, {\em x}); any other keeps its case ({DNA}), unless
    // it has no letters to keep ({1984})
    void braced() {
        if (!rich || nocase || (at < s.size() && s[at] == '\\')) {
            group(true);
            return;
        }
        size_t tag = out.size() + (space ? 1 : 0);
        ++nocase;
        wrap(nocaseSpan, "</span>", [this] { group(true); });
        --nocase;
        size_t length = std::strlen(nocaseSpan);
        if (out.size() <= tag + length || out.compare(tag, length, nocaseSpan) != 0) return;
        if (!hasLetters(std::string_view(out).substr(tag + length))) {
            out.erase(out.size() - 7);  // </span>
            out.erase(tag, length);
        }
    }

    // Text that keeps its case and is never decoded: URLs and maths
    void keep(std::string_view t) {
        if (!rich || nocase) {
            text(t);
            return;
        }
        ++nocase;
        wrap(nocaseSpan, "</span>", [this, t] { text(t); });
        --nocase;
    }

    void command(std::vector<Switch>& switches) {
        ++at;  // the backslash
        if (at >= s.size()) return;
        char c = s[at];
        if (!isLetter(c)) {
            size_t from = at++;
            if (oneOf(c, "'`^\"~=.")) accent(c);
            else if (oneOf(c, "&%$#_{}")) text(s.substr(from, 1));
            else if (c == ',') code(0x2009);
            else if (isSpace(c) || oneOf(c, "\\;:")) blank();
            else if ((unsigned char)c >= 0x80) {
                while (at < s.size() && ((unsigned char)s[at] & 0xC0) == 0x80) ++at;
                text(s.substr(from, at - from));  // a backslash before UTF-8: the letter
            }
            return;  // \- \/ \@ and the other hints leave nothing
        }
        size_t from = at;
        while (at < s.size() && isLetter(s[at])) ++at;
        std::string_view name = s.substr(from, at - from);
        skipSpace();  // spaces end a control word
        if (name.size() == 1 && oneOf(name[0], "uvHckrdbt")) {
            accent(name[0]);
            return;
        }
        for (const Named& n : named) {
            if (name == n.name) {
                text(n.text);
                return;
            }
        }
        for (const Style& st : styled) {
            if (name == st.name) {
                bool protects = st.open == nocaseSpan;
                if (protects && (!rich || nocase)) {
                    argument();
                    return;
                }
                if (protects) ++nocase;
                wrap(st.open, st.close, [this] { argument(); });
                if (protects) --nocase;
                return;
            }
        }
        for (const Style& st : switched) {
            if (name == st.name) {
                if (!rich) return;
                Switch sw{st.close, out.size(), 0, space};
                flush();
                out += st.open;
                sw.inner = out.size();
                switches.push_back(sw);
                return;
            }
        }
        if (name == "url" || name == "nolinkurl") {
            if (at >= s.size() || s[at] != '{') return;
            size_t close = matching(at);
            std::string link = verbatim(s.substr(at, std::min(close + 1, s.size()) - at));
            at = std::min(close + 1, s.size());
            keep(link);
            return;
        }
        if (name == "enquote" || name == "mkbibquote") {
            code(0x201C);
            argument();
            code(0x201D);
            return;
        }
        if (name == "quad" || name == "qquad") {
            blank();
            return;
        }
        for (const char* d : dropped) {
            if (name == d) {
                skipArgument();
                return;
            }
        }
        for (const char* d : secondShown) {
            if (name == d) {
                skipArgument();
                argument();
                return;
            }
        }
        // Any other command leaves the text of its arguments
        while (at < s.size() && s[at] == '{') {
            ++at;
            group(true);
        }
    }

    // A command's argument: a group in braces, a command or one character
    void argument() {
        skipSpace();
        if (at >= s.size()) return;
        if (s[at] == '{') {
            ++at;
            group(true);
        } else if (s[at] == '\\') {
            std::vector<Switch> switches;
            command(switches);
            for (size_t i = switches.size(); i-- > 0;) end(switches[i]);
        } else {
            size_t from = at;
            nextCode(s, at);
            text(s.substr(from, at - from));
        }
    }

    void skipArgument() {
        skipSpace();
        if (at >= s.size()) return;
        if (s[at] == '{') {
            at = std::min(matching(at) + 1, s.size());
        } else if (s[at] == '\\') {
            ++at;
            if (at < s.size() && isLetter(s[at])) {
                while (at < s.size() && isLetter(s[at])) ++at;
            } else if (at < s.size()) {
                ++at;
            }
        } else {
            nextCode(s, at);
        }
    }

    // \"o, \"{o}, \" o, \'{\i}, \c c: the letter that follows, accented
    void accent(char which) {
        skipSpace();
        if (at >= s.size()) return;
        std::string base;
        if (s[at] == '{') {
            size_t close = matching(at);
            base = Tex(s.substr(at + 1, close - at - 1), false, level + 1).run();
            at = std::min(close + 1, s.size());
        } else if (s[at] == '\\') {
            size_t from = at++;
            if (at < s.size() && isLetter(s[at])) {
                while (at < s.size() && isLetter(s[at])) ++at;
            } else if (at < s.size()) {
                ++at;
            }
            base = Tex(s.substr(from, at - from), false, level + 1).run();
            skipSpace();
        } else {
            size_t from = at;
            nextCode(s, at);
            base = std::string(s.substr(from, at - from));
        }
        if (base.empty()) {
            // \~{} and \^{} stand for the marks themselves
            if (oneOf(which, "~^'`\"")) text(std::string(1, which));
            return;
        }
        size_t p = 0;
        uint32_t letter = nextCode(base, p);
        if (letter == 0x131) letter = 'i';  // \i and \j lose their dots to the accent
        if (letter == 0x237) letter = 'j';
        text(accented(which, letter));
        text(std::string_view(base).substr(p));
    }

    // $x^2$ as written; a dollar with no partner is a dollar
    void math() {
        size_t fence = s.substr(at, 2) == "$$" ? 2 : 1;
        size_t stop = at + fence;
        while (stop < s.size() && s[stop] != '$') stop += s[stop] == '\\' ? 2 : 1;
        if (stop >= s.size()) {
            ++at;
            text("$");
            return;
        }
        stop = std::min(stop + fence, s.size());
        std::string formula;
        for (char c : s.substr(at, stop - at)) formula += isSpace(c) ? ' ' : c;
        at = stop;
        keep(formula);
    }

    // - a hyphen, -- an en dash, --- an em dash
    void dashes() {
        size_t n = 0;
        while (at < s.size() && s[at] == '-') {
            ++at;
            ++n;
        }
        for (; n >= 3; n -= 3) code(0x2014);
        if (n == 2) code(0x2013);
        else if (n == 1) text("-");
    }
};

std::string plain(std::string_view tex) { return Tex(tex, false).run(); }

// --- Names ---------------------------------------------------------------

// The closing brace for the one at `open` in a name, or npos
size_t closingBrace(std::string_view s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        if (s[i] == '\\') {
            ++i;
        } else if (s[i] == '{') {
            ++depth;
        } else if (s[i] == '}' && --depth == 0) {
            return i;
        }
    }
    return std::string_view::npos;
}

// Splits at `separator` where it is not in braces; with ' ', at spaces and
// ties (~). Parts are trimmed, and empty ones dropped unless `keepEmpty`
std::vector<std::string_view> split(std::string_view s, char separator, bool keepEmpty = false) {
    std::vector<std::string_view> parts;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        bool cut = i == s.size();
        if (!cut) {
            char c = s[i];
            if (c == '\\') {
                if (i + 1 < s.size()) ++i;
                continue;
            }
            if (c == '{') ++depth;
            else if (c == '}' && depth > 0) --depth;
            else if (depth == 0) cut = separator == ' ' ? isSpace(c) || c == '~' : c == separator;
        }
        if (!cut) continue;
        std::string_view part = trim(s.substr(start, std::min(i, s.size()) - start));
        if (!part.empty() || keepEmpty) parts.push_back(part);
        start = i + 1;
    }
    return parts;
}

// Whether a word of a name starts with a capital, as BibTeX decides: by its
// first letter, or the first letter of the special character it starts
// with. A word in braces, or in a script without case, counts as one
bool capitalised(std::string_view word) {
    if (word.size() >= 2 && word[0] == '{' && word[1] != '\\') return true;
    std::string text = plain(word);
    size_t at = 0;
    while (at < text.size()) {
        uint32_t c = nextCode(text, at);
        if (c < 0x80 && !isLetter((char)c)) continue;
        return !isLowerCode(c);
    }
    return true;
}

// Words from..to joined by spaces, decoded from TeX when `tex`
std::string joined(const std::vector<std::string_view>& words, size_t from, size_t to, bool tex = true) {
    std::string out;
    for (size_t i = from; i < to && i < words.size(); ++i) {
        std::string word = tex ? plain(words[i]) : std::string(words[i]);
        if (word.empty()) continue;
        if (!out.empty()) out += ' ';
        out += word;
    }
    return out;
}

// biblatex's extended form: family=Doe, given=John, prefix=van, useprefix=true
bool extendedName(std::string_view text, Name& name) {
    std::string prefix;
    bool usePrefix = false;
    std::vector<std::string_view> parts = split(text, ',');
    for (std::string_view part : parts) {
        size_t eq = part.find('=');
        if (eq == std::string_view::npos) return false;
        std::string key = lower(trim(part.substr(0, eq)));
        std::string_view value = trim(part.substr(eq + 1));
        if (key == "family") name.family = plain(value);
        else if (key == "given") name.given = plain(value);
        else if (key == "prefix") prefix = plain(value);
        else if (key == "suffix") name.suffix = plain(value);
        else if (key == "useprefix") usePrefix = lower(plain(value)) == "true";
        else if (!in(key, {"familyi", "giveni", "prefixi", "suffixi"})) return false;
    }
    if (parts.empty()) return false;
    (usePrefix ? name.nonDroppingParticle : name.droppingParticle) = prefix;
    return true;
}

// One name as BibTeX reads it: "First von Last", "von Last, First" or
// "von Last, Jr, First"; the von part is what pandoc reads it to be, the
// lower-case words before the first capitalised one
bool parseName(std::string_view text, Name& name) {
    text = trim(text);
    if (text.empty()) return false;
    if (lower(text) == "others") {
        name.literal = "others";
        return true;
    }
    if (text.front() == '{' && closingBrace(text, 0) == text.size() - 1) {
        name.literal = plain(text.substr(1, text.size() - 2));
        return !name.literal.empty();
    }
    if (text.find('=') != std::string_view::npos && extendedName(text, name)) return true;
    std::vector<std::string_view> parts = split(text, ',', true);
    std::vector<std::string_view> first, vonLast, junior;
    if (parts.size() == 1) {
        std::vector<std::string_view> words = split(parts[0], ' ');
        size_t caps = 0;
        while (caps < words.size() && capitalised(words[caps])) ++caps;
        if (caps == words.size() && caps > 0) --caps;
        first.assign(words.begin(), words.begin() + caps);
        vonLast.assign(words.begin() + caps, words.end());
    } else if (parts.size() > 1) {
        vonLast = split(parts[0], ' ');
        if (parts.size() == 2) {
            first = split(parts[1], ' ');
        } else {
            junior = split(parts[1], ' ');
            first = split(parts[2], ' ');
        }
    }
    size_t von = 0;
    while (von < vonLast.size() && !capitalised(vonLast[von])) ++von;
    if (von == vonLast.size() && von > 0) --von;
    name.given = joined(first, 0, first.size());
    name.droppingParticle = joined(vonLast, 0, von);
    name.family = joined(vonLast, von, vonLast.size());
    name.suffix = joined(junior, 0, junior.size());
    return !name.family.empty() || !name.given.empty();
}

// --- Dates ---------------------------------------------------------------

const char* const monthNames[] = {"january", "february", "march", "april", "may", "june",
                                  "july", "august", "september", "october", "november", "december"};

// March, Mar, Mar., mar: 3. Three letters at least
int monthNumber(std::string_view word) {
    while (!word.empty() && word.back() == '.') word.remove_suffix(1);
    if (word.size() < 3) return 0;
    for (int i = 0; i < 12; ++i) {
        if (startsWithNoCase(monthNames[i], word)) return i + 1;
    }
    return 0;
}

int seasonNumber(std::string_view word) {
    std::string w = lower(word);
    if (w == "spring") return 1;
    if (w == "summer") return 2;
    if (w == "autumn" || w == "fall") return 3;
    if (w == "winter") return 4;
    return 0;
}

// One EDTF date: [-]YYYY[-MM[-DD]], with a season (21-24) for the month,
// ~ ? % marks anywhere and a time of day that is ignored
bool datePoint(std::string_view text, DateParts& parts, Date& date) {
    std::string t;
    for (char c : trim(text)) {
        if (c == '~' || c == '?' || c == '%') date.circa = true;
        else t += c;
    }
    size_t time = t.find('T');
    if (time != std::string::npos) t.resize(time);
    size_t at = 0;
    bool negative = !t.empty() && t[0] == '-';
    if (negative) ++at;
    long year = 0;
    size_t from = at;
    while (at < t.size() && isDigit(t[at]) && at - from < 6) year = year * 10 + (t[at++] - '0');
    if (at - from < 3) return false;  // a year has three digits at least
    // EDTF counts a year 0 (1 BCE); CSL does not
    long astronomical = negative ? -year : year;
    int fields[2] = {0, 0};
    int count = 0;
    while (count < 2 && at < t.size()) {
        if (t[at] != '-') return false;
        size_t digits = ++at;
        while (at < t.size() && isDigit(t[at]) && at - digits < 2) fields[count] = fields[count] * 10 + (t[at++] - '0');
        if (at == digits) return false;
        ++count;
    }
    if (at != t.size()) return false;
    if (count && fields[0] >= 21 && fields[0] <= 24) {
        if (count == 2) return false;  // a season has no days
        date.season = fields[0] - 20;
    } else if ((count && (fields[0] < 1 || fields[0] > 12)) || (count == 2 && (fields[1] < 1 || fields[1] > 31))) {
        return false;
    } else {
        parts.month = fields[0];
        parts.day = fields[1];
    }
    parts.year = (int)(astronomical <= 0 ? astronomical - 1 : astronomical);
    return true;
}

// Dates as people write them: March 2024, 1 March 2024, March 1, 2024,
// Spring 2024, c. 1850
bool looseDate(std::string_view text, Date& date) {
    int year = 0, month = 0, day = 0, season = 0;
    bool circa = false;
    size_t at = 0;
    while (at < text.size()) {
        char c = text[at];
        if (isDigit(c)) {
            size_t from = at;
            int value = 0;
            while (at < text.size() && isDigit(text[at]) && at - from < 6) value = value * 10 + (text[at++] - '0');
            if (at - from >= 3) {
                if (year) return false;
                year = value;
            } else {
                if (day || value < 1 || value > 31) return false;
                day = value;
            }
        } else if (isLetter(c)) {
            size_t from = at;
            while (at < text.size() && isLetter(text[at])) ++at;
            std::string_view word = text.substr(from, at - from);
            if (int m = monthNumber(word)) {
                if (month) return false;
                month = m;
            } else if (int sn = seasonNumber(word)) {
                season = sn;
            } else if (in(lower(word), {"c", "ca", "circa"})) {
                circa = true;
            } else {
                return false;
            }
        } else if (isSpace(c) || c == ',' || c == '.') {
            ++at;
        } else {
            return false;
        }
    }
    if (!year || (day && !month)) return false;
    date.from = {year, month, day};
    date.season = season;
    date.circa = circa;
    return true;
}

// --- BibTeX --------------------------------------------------------------

// An entry as written: its fields' TeX, with macros expanded
struct Entry {
    std::string type, key;
    size_t offset = 0;
    std::vector<std::pair<std::string, std::string>> fields;

    const std::string* get(std::string_view name) const {
        for (const auto& f : fields) {
            if (f.first == name) return &f.second;
        }
        return nullptr;
    }
};

// BibTeX and BibLaTeX entry types, and the CSL types they become
const char* const typeMap[][2] = {
    {"article", "article-journal"}, {"book", "book"}, {"mvbook", "book"}, {"collection", "book"},
    {"mvcollection", "book"}, {"proceedings", "book"}, {"mvproceedings", "book"},
    {"reference", "book"}, {"mvreference", "book"}, {"manual", "book"}, {"booklet", "pamphlet"},
    {"periodical", "periodical"}, {"inbook", "chapter"}, {"incollection", "chapter"},
    {"bookinbook", "chapter"}, {"suppbook", "chapter"}, {"suppcollection", "chapter"},
    {"inreference", "entry-encyclopedia"}, {"inproceedings", "paper-conference"},
    {"conference", "paper-conference"}, {"phdthesis", "thesis"}, {"mastersthesis", "thesis"},
    {"thesis", "thesis"}, {"techreport", "report"}, {"report", "report"}, {"online", "webpage"},
    {"electronic", "webpage"}, {"www", "webpage"}, {"patent", "patent"}, {"dataset", "dataset"},
    {"software", "software"}, {"unpublished", "manuscript"}, {"misc", "document"},
    {"suppperiodical", "article-journal"}, {"review", "review"}, {"standard", "standard"},
    {"artwork", "graphic"}, {"image", "graphic"}, {"audio", "song"}, {"music", "song"},
    {"movie", "motion_picture"}, {"video", "motion_picture"}, {"letter", "personal_communication"},
    {"performance", "performance"}, {"legislation", "legislation"}, {"jurisdiction", "legal_case"},
};

// biblatex's localisation keys in the type field
const char* const genres[][2] = {
    {"phdthesis", "PhD thesis"}, {"mathesis", "Master\xE2\x80\x99s thesis"},
    {"mastersthesis", "Master\xE2\x80\x99s thesis"}, {"candthesis", "Candidate thesis"},
    {"techreport", "Technical report"}, {"resreport", "Research report"},
    {"software", "Computer software"}, {"datacd", "Data CD"}, {"audiocd", "Audio CD"},
};

const char* const states[][2] = {
    {"inpress", "in press"}, {"submitted", "submitted"}, {"forthcoming", "forthcoming"},
    {"inpreparation", "in preparation"}, {"prepublished", "pre-published"},
};

// babel's language names, as BCP 47 tags
const char* const languages[][2] = {
    {"english", "en"}, {"american", "en-US"}, {"usenglish", "en-US"}, {"british", "en-GB"},
    {"ukenglish", "en-GB"}, {"australian", "en-AU"}, {"canadian", "en-CA"}, {"german", "de-DE"},
    {"ngerman", "de-DE"}, {"austrian", "de-AT"}, {"naustrian", "de-AT"}, {"swissgerman", "de-CH"},
    {"nswissgerman", "de-CH"}, {"french", "fr-FR"}, {"spanish", "es-ES"}, {"italian", "it-IT"},
    {"portuguese", "pt-PT"}, {"brazilian", "pt-BR"}, {"dutch", "nl-NL"}, {"russian", "ru-RU"},
    {"ukrainian", "uk-UA"}, {"polish", "pl-PL"}, {"czech", "cs-CZ"}, {"slovak", "sk-SK"},
    {"slovene", "sl-SI"}, {"croatian", "hr-HR"}, {"serbian", "sr-RS"}, {"bulgarian", "bg-BG"},
    {"greek", "el-GR"}, {"turkish", "tr-TR"}, {"hungarian", "hu-HU"}, {"magyar", "hu-HU"},
    {"romanian", "ro-RO"}, {"swedish", "sv-SE"}, {"norwegian", "nb-NO"}, {"norsk", "nb-NO"},
    {"nynorsk", "nn-NO"}, {"danish", "da-DK"}, {"finnish", "fi-FI"}, {"icelandic", "is-IS"},
    {"catalan", "ca-AD"}, {"basque", "eu"}, {"galician", "gl-ES"}, {"latin", "la"},
    {"hebrew", "he-IL"}, {"arabic", "ar"}, {"persian", "fa-IR"}, {"farsi", "fa-IR"},
    {"chinese", "zh-CN"}, {"japanese", "ja-JP"}, {"korean", "ko-KR"}, {"vietnamese", "vi-VN"},
    {"thai", "th-TH"}, {"indonesian", "id-ID"}, {"estonian", "et-EE"}, {"latvian", "lv-LV"},
    {"lithuanian", "lt-LT"}, {"afrikaans", "af-ZA"}, {"welsh", "cy-GB"}, {"irish", "ga-IE"},
};

template <size_t N>
const char* lookup(const char* const (&table)[N][2], std::string_view key) {
    for (const auto& row : table) {
        if (key == row[0]) return row[1];
    }
    return nullptr;
}

// Fields common enough that a line starting with one ends any value still
// open before it: the value lost its closing brace or quote
const char* const commonFields[] = {
    "abstract", "address", "annote", "author", "booktitle", "chapter", "crossref", "date",
    "day", "doi", "edition", "editor", "eprint", "eventdate", "eventtitle", "file",
    "howpublished", "institution", "isbn", "issn", "issue", "journal", "journaltitle",
    "keywords", "langid", "language", "location", "month", "note", "number", "organization",
    "pages", "publisher", "school", "series", "shorttitle", "subtitle", "title", "translator",
    "type", "url", "urldate", "venue", "version", "volume", "year"};

// Types whose number is the issue, those whose number is in a series, and
// those that hold the parts crossref points from
bool periodical(std::string_view type) { return in(type, {"article", "periodical", "suppperiodical"}); }
bool bookLike(std::string_view type) {
    return in(type, {"book", "mvbook", "collection", "mvcollection", "proceedings", "mvproceedings",
                     "reference", "mvreference", "bookinbook", "inbook", "incollection",
                     "inproceedings", "conference", "inreference", "suppbook", "suppcollection"});
}
bool container(std::string_view type) {
    return in(type, {"book", "mvbook", "collection", "mvcollection", "proceedings", "mvproceedings",
                     "reference", "mvreference"});
}

// Whether a biblatex option ("useprefix=true, juniorcomma") is on
bool option(std::string_view options, std::string_view name) {
    for (std::string_view part : split(options, ',')) {
        size_t eq = part.find('=');
        std::string key = lower(trim(part.substr(0, eq)));
        std::string value = eq == std::string_view::npos ? "true" : lower(trim(part.substr(eq + 1)));
        if (key == name) return value == "true";
    }
    return false;
}

// The last character a reader sees in rich text, past closing tags
char lastVisible(const std::string& rich) {
    size_t end = rich.size();
    while (end > 0 && rich[end - 1] == '>') {
        size_t open = rich.rfind('<', end - 1);
        if (open == std::string::npos) break;
        end = open;
    }
    return end ? rich[end - 1] : 0;
}

// A title and its subtitle as pandoc joins them: "Title: Subtitle", but
// "Why? A subtitle" after a title that ends in punctuation
std::string joinTitle(std::string title, const std::string& more, char separator) {
    if (more.empty()) return title;
    if (title.empty()) return more;
    if (!oneOf(lastVisible(title), "!?.,:;")) title += separator;
    title += ' ';
    title += more;
    return title;
}

std::string doiOf(std::string_view raw) {
    std::string doi = verbatim(raw);
    for (const char* prefix : {"https://doi.org/", "http://doi.org/", "https://dx.doi.org/",
                               "http://dx.doi.org/", "doi.org/", "doi:"}) {
        if (startsWithNoCase(doi, prefix)) {
            doi.erase(0, std::strlen(prefix));
            break;
        }
    }
    return doi;
}

std::string languageTag(std::string_view text) {
    if (const char* tag = lookup(languages, lower(text))) return tag;
    return std::string(text);
}

// BibTeX fields that become one CSL variable each, in the order they are
// tried: the first with a value sets its variable
const char* const fieldMap[][2] = {
    {"shorttitle", "title-short"}, {"shortjournal", "container-title-short"},
    {"series", "collection-title"}, {"volume", "volume"}, {"volumes", "number-of-volumes"},
    {"edition", "edition"}, {"pages", "page"}, {"pagetotal", "number-of-pages"},
    {"chapter", "chapter-number"}, {"version", "version"}, {"issue", "issue"},
    {"publisher", "publisher"}, {"institution", "publisher"}, {"school", "publisher"},
    {"organization", "publisher"}, {"location", "publisher-place"}, {"address", "publisher-place"},
    {"eventtitle", "event-title"}, {"venue", "event-place"}, {"isbn", "ISBN"}, {"issn", "ISSN"},
    {"note", "note"}, {"abstract", "abstract"}, {"keywords", "keyword"}, {"annote", "annote"},
    {"annotation", "annote"},
};

// Fields that hold identifiers as written, not TeX
const char* const verbatimFields[][2] = {{"url", "URL"}, {"pmid", "PMID"}, {"pmcid", "PMCID"}};

std::string richField(const Entry& e, const char* field) {
    const std::string* v = e.get(field);
    return v ? latexToRich(*v) : std::string();
}

// Sets a variable that has no value yet
void setField(Item& it, const char* variable, std::string value) {
    if (!value.empty() && !it.fields.count(variable)) it.fields[variable] = std::move(value);
}

class BibReader {
public:
    BibReader(std::string_view text, std::string_view file, Library& into)
        : s(text), source(file), library(into), lines(text) {}

    void read() {
        size_t first = library.diagnostics.size();
        if (s.substr(0, 3) == "\xEF\xBB\xBF") at = 3;
        while (at < s.size()) {
            char c = s[at];
            if (c == '@' && (at == 0 || !(isLetter(s[at - 1]) || isDigit(s[at - 1])))) {
                entry();
            } else if (c == '%') {
                while (at < s.size() && s[at] != '\n') ++at;  // a comment line
            } else {
                ++at;  // text between entries
            }
        }
        inherit();
        for (const Entry& e : entries) library.add(item(e));
        // Syntax is checked before entries become items: the file's problems
        // in the order of their lines. They are few, and the sort is stable
        std::vector<Diagnostic>& found = library.diagnostics;
        for (size_t i = first + 1; i < found.size(); ++i) {
            for (size_t j = i; j > first && found[j - 1].line > found[j].line; --j) std::swap(found[j - 1], found[j]);
        }
    }

private:
    std::string_view s;
    std::string source;
    Library& library;
    Lines lines;
    size_t at = 0;
    size_t valueEnd = 0;  // where the last value read ended, before any space
    std::map<std::string, std::string> macros;
    std::vector<Entry> entries;

    // A problem at `offset`, its message in parts
    void report(size_t offset, std::initializer_list<std::string_view> parts) {
        std::string message;
        for (std::string_view part : parts) message += part;
        library.diagnostics.push_back({source, lines.line(offset), std::move(message)});
    }

    void skipSpace() {
        while (at < s.size() && isSpace(s[at])) ++at;
    }

    bool atLineStart(size_t pos) const {
        while (pos > 0 && (s[pos - 1] == ' ' || s[pos - 1] == '\t')) --pos;
        return pos == 0 || s[pos - 1] == '\n';
    }

    // Whether an entry starts at `pos`: @type{ or @type( first on its line.
    // A value or entry that runs into one was not closed
    bool entryAt(size_t pos) const {
        if (pos >= s.size() || s[pos] != '@' || !atLineStart(pos)) return false;
        size_t i = pos + 1;
        while (i < s.size() && isLetter(s[i])) ++i;
        if (i == pos + 1) return false;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        return i < s.size() && (s[i] == '{' || s[i] == '(');
    }

    bool entryOnNextLine(size_t newline) const {
        size_t i = newline + 1;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        return entryAt(i);
    }

    // Whether the line after `newline` starts a field: "  year = 2020". A
    // value still open there lost its closing brace or quote
    bool fieldOnNextLine(size_t newline) const {
        size_t i = newline + 1;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t from = i;
        while (i < s.size() && isLetter(s[i])) ++i;
        std::string word = lower(s.substr(from, i - from));
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i >= s.size() || s[i] != '=') return false;
        for (const char* field : commonFields) {
            if (word == field) return true;
        }
        return false;
    }

    // After a mistake, on to the next entry
    void recover() {
        while (at < s.size() && !entryAt(at)) ++at;
    }

    // A type, field or macro name
    std::string name() {
        size_t from = at;
        while (at < s.size() && (isLetter(s[at]) || isDigit(s[at]) || oneOf(s[at], "_-:.+/"))) ++at;
        return lower(s.substr(from, at - from));
    }

    void entry() {
        size_t start = at++;
        skipSpace();
        std::string type = name();
        if (type.empty()) return;  // a lone @ between entries
        size_t afterName = at;
        skipSpace();
        if (type == "comment" && (at >= s.size() || (s[at] != '{' && s[at] != '('))) {
            at = afterName;  // BibTeX's @comment runs to the end of its line
            while (at < s.size() && s[at] != '\n') ++at;
            return;
        }
        if (at >= s.size() || (s[at] != '{' && s[at] != '(')) {
            report(start, {"expected { or ( after @", type});
            return;
        }
        char close = s[at++] == '{' ? '}' : ')';
        if (type == "comment" || type == "preamble") {
            skipBlock(close, start, type);
            return;
        }
        if (type == "string") {
            macro(close, start);
            return;
        }
        Entry e;
        e.type = type;
        e.offset = start;
        skipSpace();
        size_t keyAt = at;
        while (at < s.size() && !isSpace(s[at]) && !oneOf(s[at], ",={}\"") && !(close == ')' && s[at] == ')')) ++at;
        e.key = std::string(s.substr(keyAt, at - keyAt));
        skipSpace();
        if (e.key.empty() || (at < s.size() && s[at] == '=')) {
            report(start, {"an @", type, " entry without a key"});
            recover();
            return;
        }
        fields(e, close, start);
        // @set and @xdata gather other entries' data; they are no references
        if (type != "set" && type != "xdata") entries.push_back(std::move(e));
    }

    // @comment and @preamble: skipped to their closing brace or parenthesis
    void skipBlock(char close, size_t start, const std::string& type) {
        char open = close == '}' ? '{' : '(';
        int depth = 1;
        while (at < s.size()) {
            char c = s[at++];
            if (c == '\\' && at < s.size()) ++at;
            else if (c == open) ++depth;
            else if (c == close && --depth == 0) return;
        }
        report(start, {"@", type, " is not closed"});
    }

    // @string{name = value}
    void macro(char close, size_t start) {
        for (;;) {
            skipSpace();
            if (at >= s.size() || entryAt(at)) {
                report(start, {"@string is not closed"});
                return;
            }
            if (s[at] == close) {
                ++at;
                return;
            }
            if (s[at] == ',') {
                ++at;
                continue;
            }
            size_t nameAt = at;
            std::string key = name();
            if (key.empty()) {
                report(at, {"expected a name in @string"});
                recover();
                return;
            }
            skipSpace();
            if (at >= s.size() || s[at] != '=') {
                report(nameAt, {"expected = after @string ", key});
                recover();
                return;
            }
            ++at;
            std::string text;
            if (value(text, "@string " + key) == Read::Failed) {
                recover();
                return;
            }
            macros[key] = std::move(text);
        }
    }

    void fields(Entry& e, char close, size_t start) {
        auto unclosed = [&] { report(start, {"the entry @", e.key, " is not closed"}); };
        if (at < s.size() && s[at] == ',') {
            ++at;
        } else if (at < s.size() && s[at] == close) {
            ++at;
            return;
        } else if (at >= s.size() || entryAt(at)) {
            unclosed();
            return;
        } else {
            report(at, {"missing comma after the key @", e.key});
        }
        for (;;) {
            skipSpace();
            if (at >= s.size() || entryAt(at)) {
                unclosed();
                return;
            }
            if (s[at] == close) {
                ++at;
                return;
            }
            if (s[at] == ',') {
                ++at;
                continue;
            }
            size_t nameAt = at;
            std::string field = name();
            if (field.empty()) {
                report(at, {"unexpected text in @", e.key});
                recover();
                return;
            }
            skipSpace();
            if (at >= s.size() || s[at] != '=') {
                report(nameAt, {"expected = after ", field, " in @", e.key});
                recover();
                return;
            }
            ++at;
            std::string text;
            Read read = value(text, field, e.key);
            if (read == Read::Failed) {
                recover();
                return;
            }
            if (e.get(field)) {
                report(nameAt, {"@", e.key, " has a second ", field, "; the first is used"});
            } else {
                e.fields.emplace_back(field, std::move(text));
            }
            if (read == Read::Cut) continue;  // the next field's line
            skipSpace();
            if (at < s.size() && s[at] == ',') {
                ++at;
            } else if (at < s.size() && s[at] == close) {
                ++at;
                return;
            } else if (at >= s.size() || entryAt(at)) {
                unclosed();
                return;
            } else {
                report(valueEnd, {"missing comma after the ", field, " of @", e.key});
            }
        }
    }

    enum class Read { Done, Cut, Failed };

    // A value: {braced} and "quoted" parts, numbers and macros joined by #.
    // A part that lost its closing brace or quote is cut where the next
    // field's line starts: its text to there counts, and reading goes on
    Read value(std::string& out, std::string_view field, std::string_view entry = {}) {
        std::string_view of = entry.empty() ? "" : " of @";  // "the title of @doe2024"
        for (;;) {
            skipSpace();
            size_t partAt = at;
            if (at >= s.size() || entryAt(at)) {
                report(partAt, {"expected a value for ", field, of, entry});
                return Read::Failed;
            }
            char c = s[at];
            if (c == '{' || c == '"') {
                size_t from = ++at;
                int depth = 0;
                bool closed = false, cut = false;
                while (at < s.size()) {
                    char d = s[at];
                    if (d == '\\') {
                        at = std::min(at + 2, s.size());
                        continue;
                    }
                    if (d == '\n' && entryOnNextLine(at)) break;
                    if (d == '\n' && fieldOnNextLine(at)) {
                        cut = true;
                        break;
                    }
                    if (d == '{') {
                        ++depth;
                    } else if (d == '}') {
                        if (depth == 0) {
                            closed = c == '{';
                            break;
                        }
                        --depth;
                    } else if (d == '"' && c == '"' && depth == 0) {
                        closed = true;
                        break;
                    }
                    ++at;
                }
                if (!closed) {
                    report(partAt, {c == '{' ? "unbalanced braces in the " : "unbalanced quotes in the ", field, of, entry});
                    if (!cut) return Read::Failed;
                    std::string_view kept = trim(s.substr(from, at - from));
                    if (!kept.empty() && kept.back() == ',') kept.remove_suffix(1);
                    out.append(trim(kept));
                    ++at;
                    return Read::Cut;
                }
                out.append(s.substr(from, at - from));
                ++at;
            } else if (isDigit(c)) {
                size_t from = at;
                while (at < s.size() && isDigit(s[at])) ++at;
                out.append(s.substr(from, at - from));
            } else if (isLetter(c)) {
                std::string key = name();
                auto it = macros.find(key);
                if (it != macros.end()) {
                    out += it->second;
                } else if (int month = monthNumber(key); month && key.size() == 3) {
                    // jan..dec, as BibTeX defines them
                    std::string full = monthNames[month - 1];
                    full[0] = (char)(full[0] - 'a' + 'A');
                    out += full;
                } else {
                    report(partAt, {"@string ", key, " is not defined"});
                }
            } else {
                report(partAt, {"expected a value for ", field, of, entry});
                return Read::Failed;
            }
            valueEnd = at;
            skipSpace();
            if (at < s.size() && s[at] == '#') {
                ++at;
                continue;
            }
            return Read::Done;
        }
    }

    // crossref: an entry takes the fields it lacks from its parent, whose
    // title becomes the booktitle as biblatex maps it
    void inherit() {
        std::unordered_map<std::string, size_t> byKey;
        for (size_t i = 0; i < entries.size(); ++i) byKey.emplace(lower(entries[i].key), i);
        for (Entry& child : entries) {
            const std::string* ref = child.get("crossref");
            if (!ref) continue;
            std::string key = plain(*ref);
            auto found = byKey.find(lower(key));
            if (found == byKey.end()) {
                report(child.offset, {"@", child.key, " has a crossref to @", key, ", which is not in the file"});
                continue;
            }
            const Entry& parent = entries[found->second];
            if (&parent == &child) continue;
            bool book = container(parent.type);
            bool journal = parent.type == "periodical";
            bool inBook = in(child.type, {"inbook", "bookinbook", "suppbook"});
            for (const auto& f : parent.fields) {
                std::string field = f.first;
                if (in(field, {"crossref", "xref", "ids", "entryset", "entrysubtype", "options", "label",
                               "shorthand", "sortkey", "sortname", "sorttitle", "sortyear"})) {
                    continue;
                }
                if (book || journal) {
                    if (field == "title" || field == "subtitle" || field == "titleaddon") {
                        field = (journal ? "journal" : "book") + field;
                    } else if (field == "shorttitle") {
                        continue;
                    } else if (field == "author" && inBook) {
                        field = "bookauthor";
                    }
                }
                if (!child.get(field)) child.fields.emplace_back(field, f.second);
            }
        }
    }

    std::string plainField(const Entry& e, const char* field) const {
        const std::string* v = e.get(field);
        return v ? plain(*v) : std::string();
    }

    // A date field's text for EDTF: braces dropped, but ~ is circa there,
    // no tie
    static std::string dateText(std::string_view raw) {
        std::string out;
        for (char c : raw) {
            if (c != '{' && c != '}') out += c;
        }
        return std::string(trim(out));
    }

    void dateField(const Entry& e, const char* field, const char* variable, Item& it) {
        const std::string* v = e.get(field);
        if (!v) return;
        Date date;
        if (!parseDate(dateText(*v), date)) {
            date = Date{};
            date.literal = plain(*v);
            report(e.offset, {"the ", field, " of @", e.key, " is not a date: ", date.literal});
        }
        if (!date.empty()) it.dates[variable] = date;
    }

    // issued: the date field, else year, month and day
    void issued(const Entry& e, Item& it) {
        if (e.get("date")) {
            dateField(e, "date", "issued", it);
            return;
        }
        const std::string* year = e.get("year");
        if (!year) return;
        // 2001--2003 and 2001-2003 are ranges
        std::string range = dateText(*year);
        size_t dash = range.find("--");
        if (dash != std::string::npos) range.replace(dash, 2, "/");
        if (range.size() == 9 && range[4] == '-') range[4] = '/';
        Date date;
        if (!parseDate(range, date)) {
            date = Date{};
            date.literal = plain(*year);
        } else if (!date.to.year && !date.from.month) {
            std::string month = lower(plainField(e, "month"));
            while (!month.empty() && month.back() == '.') month.pop_back();
            if (!month.empty() && isDigit(month[0])) {
                int m = std::atoi(month.c_str());
                if (m >= 1 && m <= 12) date.from.month = m;
                else if (m >= 21 && m <= 24) date.season = m - 20;
            } else if (!month.empty()) {
                size_t end = 0;
                while (end < month.size() && isLetter(month[end])) ++end;
                std::string word = month.substr(0, end);
                if (int m = monthNumber(word)) date.from.month = m;
                else if (int sn = seasonNumber(word)) date.season = sn;
            }
            int day = std::atoi(plainField(e, "day").c_str());
            if (date.from.month && day >= 1 && day <= 31) date.from.day = day;
        }
        if (!date.empty()) it.dates["issued"] = date;
    }

    Item item(const Entry& e) {
        Item it;
        it.id = e.key;
        it.source = source;
        it.line = lines.line(e.offset);
        const std::string& type = e.type;
        const char* csl = lookup(typeMap, type);
        if (!csl) {
            report(e.offset, {"unknown entry type @", type, " for @", e.key, "; read as a document"});
            csl = "document";
        }
        it.type = csl;
        std::string subtype = lower(plainField(e, "entrysubtype"));
        if (type == "article" && (subtype == "magazine" || subtype == "newspaper")) it.type = "article-" + subtype;

        auto rich = [&e](const char* field) { return richField(e, field); };
        auto set = [&it](const char* variable, std::string value) { setField(it, variable, std::move(value)); };

        set("title", joinTitle(joinTitle(rich("title"), rich("subtitle"), ':'), rich("titleaddon"), '.'));
        std::string book = joinTitle(joinTitle(rich("booktitle"), rich("booksubtitle"), ':'), rich("booktitleaddon"), '.');
        std::string journal = rich("journaltitle");
        if (journal.empty()) journal = rich("journal");
        journal = joinTitle(journal, rich("journalsubtitle"), ':');
        // A chapter or paper is in its booktitle; anything else in its journal
        bool inside = in(it.type, {"chapter", "paper-conference", "entry-encyclopedia"});
        set("container-title", inside ? book : journal);
        set("container-title", inside ? journal : book);
        std::string number = rich("number");
        if (periodical(type)) set("issue", number);
        else if (bookLike(type)) set("collection-number", number);
        else set("number", number);
        for (const auto& pair : fieldMap) set(pair[1], rich(pair[0]));

        for (const auto& pair : verbatimFields) {
            if (const std::string* v = e.get(pair[0])) set(pair[1], escaped(verbatim(*v)));
        }
        if (const std::string* v = e.get("doi")) set("DOI", escaped(doiOf(*v)));
        std::string language = plainField(e, "langid");
        if (language.empty()) language = plainField(e, "language");
        set("language", escaped(languageTag(language)));

        std::string genre = rich("type");
        if (const char* key = lookup(genres, lower(plainField(e, "type")))) genre = key;
        if (genre.empty() && type == "phdthesis") genre = "PhD thesis";
        if (genre.empty() && type == "mastersthesis") genre = "Master\xE2\x80\x99s thesis";
        set("genre", genre);
        std::string state = lower(plainField(e, "pubstate"));
        if (const char* key = lookup(states, state)) set("status", key);
        else set("status", rich("pubstate"));

        // howpublished holds a URL or how the work came out
        if (const std::string* v = e.get("howpublished")) {
            std::string_view t = trim(*v);
            if (startsWithNoCase(t, "\\url{") || startsWithNoCase(t, "http://") || startsWithNoCase(t, "https://")) {
                set("URL", escaped(verbatim(t)));
            } else {
                set("medium", latexToRich(*v));
            }
        }

        // eprint: an arXiv preprint and the other archives biblatex knows
        if (const std::string* v = e.get("eprint")) {
            std::string eprint = verbatim(*v);
            std::string kind = lower(plainField(e, "eprinttype"));
            if (kind.empty()) kind = lower(plainField(e, "archiveprefix"));
            if (kind == "arxiv") {
                if (startsWithNoCase(eprint, "arxiv:")) eprint.erase(0, 6);
                set("URL", escaped("https://arxiv.org/abs/" + eprint));
                if (it.type == "document") {
                    it.type = "article";
                    std::string label = "arXiv:" + eprint;
                    std::string area = plainField(e, "eprintclass");
                    if (area.empty()) area = plainField(e, "primaryclass");
                    if (!area.empty()) label += " [" + area + "]";
                    set("number", escaped(label));
                }
            } else if (kind == "doi") {
                set("DOI", escaped(doiOf(eprint)));
            } else if (kind == "pubmed") {
                set("PMID", escaped(eprint));
            } else if (kind == "hdl" || kind == "handle") {
                set("URL", escaped("https://hdl.handle.net/" + eprint));
            } else if (kind == "jstor") {
                set("URL", escaped("https://www.jstor.org/stable/" + eprint));
            }
        }

        issued(e, it);
        dateField(e, "urldate", "accessed", it);
        dateField(e, "eventdate", "event-date", it);
        dateField(e, "origdate", "original-date", it);

        std::string options = plainField(e, "options");
        bool usePrefix = option(options, "useprefix");
        bool juniorComma = option(options, "juniorcomma");
        const char* const nameFields[][2] = {{"author", "author"}, {"editor", "editor"},
                                             {"translator", "translator"}, {"bookauthor", "container-author"}};
        for (const auto& pair : nameFields) {
            const std::string* v = e.get(pair[0]);
            if (!v) continue;
            std::vector<Name> list = parseBibtexNames(*v);
            for (Name& n : list) {
                if (usePrefix && !n.droppingParticle.empty()) {
                    n.nonDroppingParticle = std::move(n.droppingParticle);
                    n.droppingParticle.clear();
                }
                if (juniorComma && !n.suffix.empty()) n.commaSuffix = true;
            }
            if (!list.empty()) it.names[pair[1]] = std::move(list);
        }
        return it;
    }
};

// --- CSL-JSON ------------------------------------------------------------

const char* const cslTypes[] = {
    "article", "article-journal", "article-magazine", "article-newspaper", "bill", "book",
    "broadcast", "chapter", "classic", "collection", "dataset", "document", "entry",
    "entry-dictionary", "entry-encyclopedia", "event", "figure", "graphic", "hearing", "interview",
    "legal_case", "legislation", "manuscript", "map", "motion_picture", "musical_score", "pamphlet",
    "paper-conference", "patent", "performance", "periodical", "personal_communication", "post",
    "post-weblog", "regulation", "report", "review", "review-book", "software", "song", "speech",
    "standard", "thesis", "treaty", "webpage",
};

const char* const dateVariables[] = {"accessed", "available-date", "event-date",
                                     "issued", "original-date", "submitted"};

const char* const nameVariables[] = {
    "author", "chair", "collection-editor", "compiler", "composer", "container-author",
    "contributor", "curator", "director", "editor", "editorial-director", "editor-translator",
    "executive-producer", "guest", "host", "illustrator", "interviewer", "narrator", "organizer",
    "original-author", "performer", "producer", "recipient", "reviewed-author", "script-writer",
    "series-creator", "translator",
};

template <size_t N>
bool listed(const char* const (&list)[N], std::string_view value) {
    for (const char* item : list) {
        if (value == item) return true;
    }
    return false;
}

using json::Json;

std::string numberText(double n) {
    char buf[40];
    if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 1e15) {
        std::snprintf(buf, sizeof buf, "%lld", (long long)n);
    } else {
        std::snprintf(buf, sizeof buf, "%g", n);
    }
    return buf;
}

// A string as it is, a number written out; anything else is empty. Values
// from YAML keep their text, so a number or true reads as it was written
std::string jsonText(const Json* v) {
    if (!v) return {};
    if (v->type == Json::String) return v->text;
    if (v->type == Json::Number) return v->text.empty() ? numberText(v->number) : v->text;
    if (v->type == Json::Bool) return v->text;
    return {};
}

bool jsonTrue(const Json* v) {
    if (!v) return false;
    if (v->type == Json::Bool) return v->boolean;
    if (v->type == Json::Number) return v->number != 0;
    if (v->type == Json::String) return !v->text.empty() && v->text != "0" && lower(v->text) != "false";
    return false;
}

// CSL-JSON text keeps the markup CSL allows: <i>, <b>, <sup>, <sub>, small
// caps and nocase spans. Any other < > & is text
std::string cslRich(std::string_view s) {
    struct Tag {
        const char* text;
        const char* open;
        const char* close;
    };
    static const Tag tags[] = {
        {"<i>", "<i>", "</i>"}, {"<em>", "<i>", "</i>"}, {"<b>", "<b>", "</b>"},
        {"<strong>", "<b>", "</b>"}, {"<sup>", "<sup>", "</sup>"}, {"<sub>", "<sub>", "</sub>"},
        {"<span class=\"nocase\">", nocaseSpan, "</span>"},
        {"<span style=\"font-variant:small-caps;\">", smallCaps, "</span>"},
        {"<span style=\"font-variant: small-caps;\">", smallCaps, "</span>"},
        {"<span style=\"font-variant:small-caps\">", smallCaps, "</span>"},
        {"<span style=\"font-variant: small-caps\">", smallCaps, "</span>"},
        {"</i>", nullptr, "</i>"}, {"</em>", nullptr, "</i>"}, {"</b>", nullptr, "</b>"},
        {"</strong>", nullptr, "</b>"}, {"</sup>", nullptr, "</sup>"}, {"</sub>", nullptr, "</sub>"},
        {"</span>", nullptr, "</span>"},
    };
    static const Named entities[] = {
        {"&amp;", "&amp;"}, {"&lt;", "&lt;"}, {"&gt;", "&gt;"}, {"&quot;", "\""}, {"&apos;", "'"},
        {"&#39;", "'"}, {"&nbsp;", "\xC2\xA0"},
    };
    std::string out;
    std::vector<const char*> open;  // the closing tags due
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        bool done = false;
        if (c == '<') {
            for (const Tag& t : tags) {
                size_t length = std::strlen(t.text);
                if (s.compare(i, length, t.text) != 0) continue;
                if (t.open) {
                    out += t.open;
                    open.push_back(t.close);
                } else if (!open.empty() && std::strcmp(open.back(), t.close) == 0) {
                    out += t.close;
                    open.pop_back();
                } else {
                    break;  // a closing tag with nothing to close is text
                }
                i += length;
                done = true;
                break;
            }
        } else if (c == '&') {
            for (const Named& n : entities) {
                size_t length = std::strlen(n.name);
                if (s.compare(i, length, n.name) != 0) continue;
                out += n.text;
                i += length;
                done = true;
                break;
            }
            if (!done && i + 2 < s.size() && s[i + 1] == '#') {
                size_t semi = s.find(';', i);
                if (semi != std::string_view::npos && semi - i <= 10) {
                    bool hex = s[i + 2] == 'x' || s[i + 2] == 'X';
                    std::string digits(s.substr(i + (hex ? 3 : 2), semi - i - (hex ? 3 : 2)));
                    char* stop = nullptr;
                    unsigned long code = std::strtoul(digits.c_str(), &stop, hex ? 16 : 10);
                    if (!digits.empty() && stop && !*stop && code > 0 && code < 0x110000) {
                        std::string t;
                        json::appendUtf8(t, (uint32_t)code);
                        appendEscaped(out, t);
                        i = semi + 1;
                        done = true;
                    }
                }
            }
        }
        if (!done) {
            appendEscaped(out, std::string_view(&s[i], 1));
            ++i;
        }
    }
    for (size_t k = open.size(); k-- > 0;) out += open[k];
    return out;
}

// CSL YAML fields are pandoc Markdown: *x* and _x_ italic, **x** and __x__
// bold, ^x^ and ~x~ super- and subscript, [x]{.smallcaps} and [x]{.nocase}.
// Written as the tags cslRich() keeps; escapes and code spans as text
bool wordByte(char c) { return isLetter(c) || isDigit(c) || (unsigned char)c >= 0x80; }

void markdownInto(std::string_view s, std::string& out, int depth) {
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size() && oneOf(s[i + 1], "\\`*_{}[]()#+-.!^~<>&\"'|$@:;,/=?%")) {
            appendEscaped(out, s.substr(i + 1, 1));
            i += 2;
            continue;
        }
        size_t n = 1;
        while (i + n < s.size() && s[i + n] == c) ++n;
        if (c == '`') {
            size_t close = s.find(std::string(n, '`'), i + n);
            if (close != std::string_view::npos) {
                appendEscaped(out, s.substr(i + n, close - i - n));
                i = close + n;
                continue;
            }
        }
        bool emphasis = c == '*' || c == '_';
        if ((emphasis ? n <= 3 : (c == '^' || c == '~') && n == 1) && depth < 8 && i + n < s.size() &&
            !isSpace(s[i + n]) && !(c == '_' && i > 0 && wordByte(s[i - 1]))) {
            // The closing run: as long, after no space, and for _ before no letter
            size_t close = std::string_view::npos;
            for (size_t k = i + n; k < s.size(); ++k) {
                if (s[k] == '\\') {
                    ++k;
                    continue;
                }
                if (!emphasis && isSpace(s[k])) break;  // ^ and ~ hold no spaces
                if (s[k] != c) continue;
                size_t m = 1;
                while (k + m < s.size() && s[k + m] == c) ++m;
                if (m == n && !isSpace(s[k - 1]) && !(c == '_' && k + m < s.size() && wordByte(s[k + m]))) {
                    close = k;
                    break;
                }
                k += m - 1;
            }
            if (close != std::string_view::npos) {
                static const char* const opening[] = {"<sup>", "<i>", "<b>", "<b><i>"};
                static const char* const closing[] = {"</sup>", "</i>", "</b>", "</i></b>"};
                bool sub = c == '~';
                out += sub ? "<sub>" : opening[emphasis ? n : 0];
                markdownInto(s.substr(i + n, close - i - n), out, depth + 1);
                out += sub ? "</sub>" : closing[emphasis ? n : 0];
                i = close + n;
                continue;
            }
        }
        if (c == '[' && depth < 8) {
            int level = 0;
            size_t k = i;
            for (; k < s.size(); ++k) {
                if (s[k] == '\\') ++k;
                else if (s[k] == '[') ++level;
                else if (s[k] == ']' && --level == 0) break;
            }
            std::string_view rest = k < s.size() ? s.substr(k + 1) : std::string_view();
            const char* span = rest.substr(0, 12) == "{.smallcaps}" ? smallCaps
                             : rest.substr(0, 9) == "{.nocase}"     ? nocaseSpan
                                                                     : nullptr;
            if (span) {
                out += span;
                markdownInto(s.substr(i + 1, k - i - 1), out, depth + 1);
                out += "</span>";
                i = k + 1 + (span == smallCaps ? 12 : 9);
                continue;
            }
        }
        out.append(s.substr(i, n));
        i += n;
    }
}

// Variables that hold an identifier, not text: no Markdown there
bool verbatimVariable(std::string_view variable) {
    return in(variable, {"DOI", "ISBN", "ISSN", "PMCID", "PMID", "URL", "call-number", "citation-key", "language"});
}

// A CSL-JSON name. One without particles of its own takes them from its
// words as citeproc-js does: "van Gogh" has the particle van, "Ludwig van"
// the particle van after the given name
bool jsonName(const Json& v, Name& name) {
    if (v.type == Json::String) return parseName(v.text, name);
    if (v.type != Json::Object) return false;
    name.family = jsonText(v.get("family"));
    name.given = jsonText(v.get("given"));
    name.droppingParticle = jsonText(v.get("dropping-particle"));
    name.nonDroppingParticle = jsonText(v.get("non-dropping-particle"));
    name.suffix = jsonText(v.get("suffix"));
    name.literal = jsonText(v.get("literal"));
    name.commaSuffix = jsonTrue(v.get("comma-suffix"));
    name.staticOrdering = jsonTrue(v.get("static-ordering"));
    const Json* parse = v.get("parse-names");
    if ((!parse || jsonTrue(parse)) && name.droppingParticle.empty() && name.nonDroppingParticle.empty()) {
        std::string family = name.family, given = name.given;
        std::vector<std::string_view> words = split(family, ' ');
        size_t particles = 0;
        while (particles + 1 < words.size() && !capitalised(words[particles])) ++particles;
        if (particles) {
            name.nonDroppingParticle = joined(words, 0, particles, false);
            name.family = joined(words, particles, words.size(), false);
        }
        words = split(given, ' ');
        size_t keep = words.size();
        while (keep > 1 && !capitalised(words[keep - 1])) --keep;
        if (keep < words.size()) {
            name.droppingParticle = joined(words, keep, words.size(), false);
            name.given = joined(words, 0, keep, false);
        }
    }
    return !(name.family.empty() && name.given.empty() && name.literal.empty());
}

int jsonInt(const Json& v) {
    long n = std::strtol(jsonText(&v).c_str(), nullptr, 10);
    return (int)std::max(-1000000L, std::min(n, 1000000L));
}

// A CSL-JSON date: date-parts, raw, literal, season and circa
bool jsonDate(const Json& v, Date& date) {
    date = Date{};
    if (v.type == Json::String) {
        if (!parseDate(v.text, date)) {
            date = Date{};
            date.literal = v.text;
        }
        return !date.empty();
    }
    if (v.type == Json::Number) {
        date.from.year = jsonInt(v);
        return !date.empty();
    }
    if (v.type == Json::Array) {
        // A list of years, months and days, as pandoc-citeproc wrote dates in
        // YAML: the second ends a range
        for (size_t i = 0; i < v.items.size() && i < 2; ++i) {
            const Json& p = v.items[i];
            DateParts d;
            if (const Json* y = p.get("year")) d.year = jsonInt(*y);
            if (const Json* m = p.get("month")) d.month = jsonInt(*m);
            if (const Json* day = p.get("day")) d.day = jsonInt(*day);
            if (d.month < 0 || d.month > 12) d.month = d.day = 0;
            if (i == 0) date.from = d;
            else if (d.year) date.to = d;
        }
        return !date.empty();
    }
    if (v.type != Json::Object) return false;
    const Json* parts = v.get("date-parts");
    if (parts && parts->type == Json::Array) {
        for (size_t i = 0; i < parts->items.size() && i < 2; ++i) {
            const Json& p = parts->items[i];
            if (p.type != Json::Array) continue;
            DateParts d;
            if (p.items.size() > 0) d.year = jsonInt(p.items[0]);
            if (p.items.size() > 1) d.month = jsonInt(p.items[1]);
            if (p.items.size() > 2) d.day = jsonInt(p.items[2]);
            // Seasons in the month: 21-24, and 13-16 as citeproc-js once wrote them
            int season = d.month >= 21 && d.month <= 24 ? d.month - 20 : d.month >= 13 && d.month <= 16 ? d.month - 12 : 0;
            if (season || d.month < 0 || d.month > 12) {
                if (i == 0) date.season = season;
                d.month = 0;
                d.day = 0;
            }
            if (i == 0) date.from = d;
            else if (d.year) date.to = d;
            else date.openEnd = date.from.year != 0;
        }
    }
    // Without parts: raw text, then the year, month and day of old files
    std::string raw = jsonText(v.get("raw"));
    if (!date.from.year && !raw.empty() && !parseDate(raw, date)) {
        date = Date{};
        date.literal = raw;
    }
    if (!date.from.year && date.literal.empty()) {
        if (const Json* y = v.get("year")) date.from.year = jsonInt(*y);
        if (const Json* m = v.get("month")) date.from.month = jsonInt(*m);
        if (const Json* d = v.get("day")) date.from.day = jsonInt(*d);
        if (!date.from.year) date.from = DateParts{};
    }
    // A literal is for dates that have no parts
    std::string literal = jsonText(v.get("literal"));
    if (!literal.empty() && !date.from.year) date.literal = literal;
    if (const Json* season = v.get("season")) {
        int n = jsonInt(*season);
        if (n >= 1 && n <= 4) date.season = n;
        else if (n >= 21 && n <= 24) date.season = n - 20;
        else if (int word = seasonNumber(jsonText(season))) date.season = word;
    }
    if (jsonTrue(v.get("circa"))) date.circa = true;
    return !date.empty();
}

// One reference. `base` is where the value's text starts in the text `lines`
// counts; `markdown` reads its text fields as CSL YAML's pandoc Markdown
void jsonItem(const Json& v, std::string_view source, const Lines& lines, Library& library, size_t base = 0,
              bool markdown = false) {
    Item it;
    it.source = std::string(source);
    it.line = lines.line(base + v.begin);
    it.id = jsonText(v.get("id"));
    if (it.id.empty()) it.id = jsonText(v.get("citation-key"));
    if (it.id.empty()) {
        library.diagnostics.push_back({it.source, it.line, "a reference without an id"});
        return;
    }
    std::string type = jsonText(v.get("type"));
    if (listed(cslTypes, type)) {
        it.type = type;
    } else {
        library.diagnostics.push_back({it.source, it.line,
                                       (type.empty() ? "@" + it.id + " has no type"
                                                     : "unknown type " + type + " for @" + it.id) +
                                           "; read as a document"});
    }
    for (const auto& [key, value] : v.fields) {
        if (key == "id" || key == "type") continue;
        // Older names some writers still use; the CSL name wins when both are there
        std::string variable = key;
        bool alias = true;
        if (key == "shortTitle") variable = "title-short";
        else if (key == "journalAbbreviation") variable = "container-title-short";
        else if (key == "event") variable = "event-title";
        else alias = false;
        if (listed(dateVariables, variable) ||
            (value.type == Json::Object && (value.get("date-parts") || value.get("raw") || value.get("literal")))) {
            Date date;
            if (jsonDate(value, date)) it.dates[variable] = date;
            continue;
        }
        if (value.type == Json::Array) {
            if (!listed(nameVariables, variable)) continue;
            std::vector<Name> list;
            for (const Json& n : value.items) {
                Name name;
                if (jsonName(n, name)) list.push_back(std::move(name));
            }
            if (!list.empty()) it.names[variable] = std::move(list);
            continue;
        }
        std::string text;
        if (value.type != Json::String) {
            text = escaped(jsonText(&value));
        } else if (markdown && !verbatimVariable(variable)) {
            std::string tags;
            markdownInto(value.text, tags, 0);
            text = cslRich(tags);
        } else {
            text = cslRich(value.text);
        }
        if (text.empty() || (alias && it.fields.count(variable))) continue;
        it.fields[variable] = std::move(text);
    }
    library.add(std::move(it));
}

}  // namespace

// --- The readers ---------------------------------------------------------

std::string latexToRich(std::string_view tex) { return Tex(tex, true).run(); }

std::vector<Name> parseBibtexNames(std::string_view field) {
    std::vector<Name> names;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= field.size(); ++i) {
        bool last = i == field.size();
        if (!last) {
            char c = field[i];
            if (c == '\\') {
                if (i + 1 < field.size()) ++i;
                continue;
            }
            if (c == '{') {
                ++depth;
                continue;
            }
            if (c == '}') {
                if (depth > 0) --depth;
                continue;
            }
            // " and " between names, outside braces and in any case; a
            // trailing " and" ends the list
            if (depth > 0 || !isSpace(c) || !startsWithNoCase(field.substr(i + 1), "and")) continue;
            if (i + 4 < field.size() && !isSpace(field[i + 4])) continue;
        }
        Name name;
        if (parseName(field.substr(start, i - start), name)) names.push_back(std::move(name));
        if (last) break;
        start = std::min(i + 5, field.size());
        i += 3;
    }
    return names;
}

bool parseDate(std::string_view text, Date& date) {
    date = Date{};
    std::string_view t = trim(text);
    if (t.empty()) return false;
    size_t slash = t.find('/');
    if (slash == std::string_view::npos) {
        if (datePoint(t, date.from, date)) return true;
        date = Date{};
        if (looseDate(t, date)) return true;
        date = Date{};
        return false;
    }
    std::string_view a = trim(t.substr(0, slash)), b = trim(t.substr(slash + 1));
    bool openStart = a.empty() || a == "..";
    bool openEnd = b.empty() || b == "..";
    if (!(openStart && openEnd)) {
        bool ok = openStart || datePoint(a, date.from, date);
        int season = date.season;
        ok = ok && (openEnd || datePoint(b, date.to, date));
        if (ok) {
            date.season = season;
            if (openStart) {
                // ../2024 runs up to 2024: the end is the date there is
                date.from = date.to;
                date.to = DateParts{};
            }
            date.openEnd = openEnd;
            return true;
        }
    }
    date = Date{};
    return false;
}

void readBibtex(std::string_view text, std::string_view source, Library& library) {
    BibReader(text, source, library).read();
}

void readCslJson(std::string_view text, std::string_view source, Library& library) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    if (trim(text).empty()) return;
    Lines lines(text);
    auto report = [&](size_t offset, std::string message) {
        library.diagnostics.push_back({std::string(source), lines.line(offset), std::move(message)});
    };
    json::JsonReader reader{text};
    Json root;
    bool ok = reader.value(root);
    if (!ok) {
        // The reader says where in bytes; a line serves people better
        std::string error = reader.error;
        size_t cut = error.rfind(" at offset ");
        if (cut != std::string::npos) error.resize(cut);
        report(reader.at, "not valid JSON: " + error);
    } else {
        reader.space();
        if (reader.at < text.size()) report(reader.at, "text after the references");
    }
    if (root.type == Json::Array) {
        // Items before a mistake still count
        for (const Json& v : root.items) {
            if (v.type == Json::Object) jsonItem(v, source, lines, library);
            else report(v.begin, "a reference that is not an object");
        }
    } else if (root.type == Json::Object && ok) {
        jsonItem(root, source, lines, library);
    } else if (ok) {
        report(root.begin, "CSL-JSON is an array of references");
    }
}

namespace {

// The references in YAML that sits at `offset` in `whole` (a .yaml file, or
// a document's front matter), its lines counted in `whole`
void yamlReferences(std::string_view whole, size_t offset, std::string_view text, std::string_view source,
                    Library& library) {
    Lines lines(whole);
    auto report = [&](size_t at, std::string message) {
        library.diagnostics.push_back({std::string(source), lines.line(offset + at), std::move(message)});
    };
    Json root;
    yaml::Error error;
    const size_t none = std::string_view::npos;
    size_t cut = none;  // a reference that reaches the mistake is left out
    if (!yaml::parse(text, root, error)) {
        report(error.offset, "not valid YAML: " + error.message);
        cut = error.offset;
    }
    const Json* list = &root;
    if (root.type == Json::Object && !root.get("id")) {
        list = root.get("references");
        if (!list) {
            if (cut == none) report(root.begin, "CSL YAML is a list of references, or a mapping with references:");
            return;
        }
    }
    if (list->type == Json::Array) {
        for (const Json& v : list->items) {
            if (cut != none && v.end >= cut) break;
            if (v.type == Json::Object) jsonItem(v, source, lines, library, offset, true);
            else report(v.begin, "a reference that is not a mapping");
        }
    } else if (list->type == Json::Object) {
        if (cut == none) jsonItem(*list, source, lines, library, offset, true);
    } else if (list->type != Json::Null) {
        report(list->begin, "references: holds a list of references");
    }
}

// A front matter line that starts the references: key
bool referencesKey(std::string_view line) {
    size_t n = line.substr(0, 10) == "references" ? 10
             : line.substr(0, 12) == "\"references\"" || line.substr(0, 12) == "'references'" ? 12 : 0;
    if (!n) return false;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return n < line.size() && line[n] == ':' && (n + 1 == line.size() || isSpace(line[n + 1]));
}

}  // namespace

void readCslYaml(std::string_view yaml, std::string_view source, Library& library) {
    yamlReferences(yaml, 0, yaml, source, library);
}

bool readFrontMatterReferences(std::string_view document, std::string_view source, Library& library) {
    size_t begin = 0, end = 0;
    if (!yaml::frontMatter(document, begin, end)) return false;
    // Only the references: key is read, so a mistake elsewhere in the front
    // matter costs no reference: its line, and the lines after it that are
    // indented, empty, comments or its list's items
    const size_t none = std::string_view::npos;
    size_t from = none, to = end;
    for (size_t at = begin; at < end;) {
        size_t next = document.find('\n', at);
        next = next == none || next >= end ? end : next + 1;
        std::string_view line = document.substr(at, next - at);
        if (from == none) {
            if (referencesKey(line)) from = at;
        } else if (!line.empty() && !isSpace(line[0]) && line[0] != '#' && line[0] != '-') {
            to = at;
            break;
        }
        at = next;
    }
    if (from == none) return false;
    yamlReferences(document, from, document.substr(from, to - from), source, library);
    return true;
}

bool readByExtension(std::string_view text, std::string_view path, Library& library) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return false;
    std::string extension = lower(path.substr(dot + 1));
    if (extension == "bib" || extension == "bibtex") {
        readBibtex(text, path, library);
        return true;
    }
    if (extension == "json") {
        readCslJson(text, path, library);
        return true;
    }
    if (extension == "yaml" || extension == "yml") {
        readCslYaml(text, path, library);
        return true;
    }
    return false;
}

}
