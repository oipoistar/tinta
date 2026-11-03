// Text for the CSL processor (csl_internal.h): UTF-8, letter case, CSL rich
// text, punctuation where pieces meet, numbers, sort keys, and the plain and
// HTML forms of the output (csl.h).

#pragma optimize("s", on)

#include "csl_internal.h"

#include <cstdio>
#include <cstring>

namespace csl {

uint32_t nextChar(std::string_view text, size_t& at) {
    unsigned char c = (unsigned char)text[at++];
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (extra == 0) return 0xFFFD;
    uint32_t code = c & (0x3F >> extra);
    for (int i = 0; i < extra; ++i) {
        if (at >= text.size() || ((unsigned char)text[at] & 0xC0) != 0x80) return 0xFFFD;
        code = (code << 6) | ((unsigned char)text[at++] & 0x3F);
    }
    return code;
}

void appendChar(std::string& out, uint32_t code) {
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

// Latin, Greek and Cyrillic: the scripts whose case CSL styles change
uint32_t upper(uint32_t c) {
    if (c < 0x80) return c >= 'a' && c <= 'z' ? c - 32 : c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c == 0x131) return 'I';
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return c & 1 ? c - 1 : c;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c & 1 ? c : c - 1;
    if (c >= 0x3B1 && c <= 0x3C9) return c == 0x3C2 ? 0x3A3 : c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}

uint32_t lower(uint32_t c) {
    if (c < 0x80) return c >= 'A' && c <= 'Z' ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c == 0x178) return 0xFF;
    if (c == 0x130) return 'i';
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) return c & 1 ? c : c + 1;
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return c & 1 ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

namespace {

bool isCjk(uint32_t c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x1100 && c <= 0x11FF) ||
           (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x2FFFF);
}

bool isUpperLetter(uint32_t c) { return lower(c) != c; }
bool isLowerLetter(uint32_t c) { return upper(c) != c; }
bool isLetter(uint32_t c) { return isUpperLetter(c) || isLowerLetter(c) || c == 0xDF || (c >= 0x80 && isCjk(c)); }
bool isSpace(uint32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == 0xA0 || c == 0x2009 || c == 0x202F; }

bool isPunct(char c) { return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?'; }

// Which mark stays where two meet, as pandoc's citeproc decides: 1 the
// first, 2 the second, 3 both
int keepWhich(char first, char second) {
    if (first == second) return 1;
    switch (first) {
        case ',': return 2;
        case ';': return second == ',' ? 3 : (second == '!' || second == '?') ? 2 : 1;
        case '.': return 3;
        case ':': return (second == '!' || second == '?') ? 2 : second == '.' ? 1 : 3;
        case '!':
        case '?': return (second == '.' || second == ':') ? 1 : 3;
    }
    return 3;
}

int lastFilled(const Pieces& pieces, int from) {
    for (int i = from; i >= 0; --i) {
        if (!pieces[i].text.empty()) return i;
    }
    return -1;
}

// Mends the meeting of `out` and the text that follows it
void mend(Pieces& out, std::string& next, bool punctuationInQuote) {
    if (next.empty()) return;
    int last = lastFilled(out, (int)out.size() - 1);
    if (last < 0) return;
    Piece& before = out[last];
    char a = before.text.back(), b = next.front();
    if ((before.mark & CloseQuote) && punctuationInQuote && (b == ',' || b == '.')) {
        int inside = lastFilled(out, last - 1);
        bool place = true;
        if (inside >= 0 && isPunct(out[inside].text.back())) {
            int keep = keepWhich(out[inside].text.back(), b);
            if (keep == 1) place = false;
            else if (keep == 2) out[inside].text.pop_back();
        }
        if (place && inside >= 0) {
            // The mark takes the quotation mark's format, not an italic title's
            if (out[inside].flags == before.flags && !out[inside].link) {
                out[inside].text += b;
            } else {
                Piece mark;
                mark.text = std::string(1, b);
                mark.flags = before.flags;
                out.insert(out.begin() + last, std::move(mark));
            }
        }
        next.erase(0, 1);
        return;
    }
    if (isPunct(a) && isPunct(b)) {
        int keep = keepWhich(a, b);
        if (keep == 1) next.erase(0, 1);
        else if (keep == 2) before.text.pop_back();
    } else if (a == ' ' && b == ' ') {
        next.erase(0, 1);
    }
}

const char* const stopWords[] = {"a",   "an",   "and",  "as",   "at",  "but", "by",   "down", "for",
                                 "from", "in",  "into", "nor",  "of",  "on",  "onto", "or",   "over",
                                 "so",  "the",  "till", "to",   "up",  "via", "with", "yet"};

bool stopWord(const std::string& word) {
    for (const char* stop : stopWords) {
        if (word == stop) return true;
    }
    return false;
}
}

bool hasCjk(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        if ((unsigned char)text[i] < 0x80) {
            ++i;
            continue;
        }
        if (isCjk(nextChar(text, i))) return true;
    }
    return false;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

std::string trim(std::string_view text) {
    size_t b = 0, e = text.size();
    while (b < e && (text[b] == ' ' || text[b] == '\t' || text[b] == '\n' || text[b] == '\r')) ++b;
    while (e > b && (text[e - 1] == ' ' || text[e - 1] == '\t' || text[e - 1] == '\n' || text[e - 1] == '\r')) --e;
    return std::string(text.substr(b, e - b));
}

void join(Pieces& out, Pieces& add, bool punctuationInQuote) {
    // Nothing to meet: the lists trade places, no piece moves
    if (out.empty()) {
        out.swap(add);
        add.clear();
        return;
    }
    size_t first = 0;
    while (first < add.size() && add[first].text.empty()) ++first;
    if (first == add.size()) {
        add.clear();
        return;
    }
    mend(out, add[first].text, punctuationInQuote);
    for (size_t i = first; i < add.size(); ++i) {
        if (!add[i].text.empty()) out.push_back(std::move(add[i]));
    }
    add.clear();
}

void prependText(Pieces& pieces, std::string_view text, uint16_t flags, const std::string* link) {
    if (text.empty()) return;
    Piece piece;
    piece.text = std::string(text);
    piece.flags = flags;
    piece.link = link;
    size_t first = 0;
    while (first < pieces.size() && pieces[first].text.empty()) ++first;
    if (first < pieces.size()) {
        std::string& next = pieces[first].text;
        char a = piece.text.back(), b = next.front();
        if (isPunct(a) && isPunct(b)) {
            int keep = keepWhich(a, b);
            if (keep == 1) next.erase(0, 1);
            else if (keep == 2) piece.text.pop_back();
        } else if (a == ' ' && b == ' ') {
            next.erase(0, 1);
        }
    }
    pieces.insert(pieces.begin(), std::move(piece));
}

void addText(Pieces& out, std::string_view text, uint16_t flags, bool punctuationInQuote) {
    if (text.empty()) return;
    std::string next(text);
    mend(out, next, punctuationInQuote);
    if (next.empty()) return;
    out.emplace_back();
    out.back().text = std::move(next);
    out.back().flags = flags;
}

void parseRich(std::string_view text, uint16_t flags, Pieces& out) {
    if (text.find('<') == std::string_view::npos && text.find('&') == std::string_view::npos) {
        if (text.empty()) return;
        out.emplace_back();
        out.back().text = std::string(text);
        out.back().flags = flags;
        return;
    }
    enum Tag : uint8_t { TagI, TagB, TagSup, TagSub, TagSpan };
    struct Open {
        Tag tag;
        uint16_t flags;
        uint8_t mark;
    };
    std::vector<Open> open;
    uint16_t current = flags;
    uint8_t mark = 0;
    std::string chars;
    auto flush = [&]() {
        if (chars.empty()) return;
        out.emplace_back();
        out.back().text = std::move(chars);
        out.back().flags = current;
        out.back().mark = mark;
        chars.clear();
    };
    auto close = [&](Tag tag) {
        for (size_t k = open.size(); k-- > 0;) {
            if (open[k].tag != tag) continue;
            flush();
            current = open[k].flags;
            mark = open[k].mark;
            open.resize(k);
            return;
        }
    };
    for (size_t i = 0; i < text.size();) {
        char c = text[i];
        if (c == '<') {
            size_t end = text.find('>', i);
            if (end != std::string_view::npos) {
                std::string_view tag = text.substr(i + 1, end - i - 1);
                bool known = true;
                if (tag == "i" || tag == "b" || tag == "sup" || tag == "sub" || startsWith(tag, "span ")) {
                    Tag kind = tag == "i" ? TagI : tag == "b" ? TagB : tag == "sup" ? TagSup : tag == "sub" ? TagSub : TagSpan;
                    if (kind == TagSpan && tag.find("small-caps") == std::string_view::npos &&
                        tag.find("nocase") == std::string_view::npos) {
                        known = false;
                    } else {
                        flush();
                        open.push_back({kind, current, mark});
                        if (kind == TagI) current ^= Italic;
                        else if (kind == TagB) current ^= Bold;
                        else if (kind == TagSup) current = (uint16_t)((current | Superscript) & ~Subscript);
                        else if (kind == TagSub) current = (uint16_t)((current | Subscript) & ~Superscript);
                        else if (tag.find("small-caps") != std::string_view::npos) current ^= SmallCaps;
                        else mark |= NoCase;
                    }
                } else if (tag == "/i") {
                    close(TagI);
                } else if (tag == "/b") {
                    close(TagB);
                } else if (tag == "/sup") {
                    close(TagSup);
                } else if (tag == "/sub") {
                    close(TagSub);
                } else if (tag == "/span") {
                    close(TagSpan);
                } else {
                    known = false;
                }
                if (known) {
                    i = end + 1;
                    continue;
                }
            }
        } else if (c == '&') {
            static const struct {
                const char* name;
                char value;
            } entities[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#38;", '&'}, {"&#39;", '\''}};
            bool found = false;
            for (const auto& entity : entities) {
                size_t length = std::strlen(entity.name);
                if (text.compare(i, length, entity.name) == 0) {
                    chars += entity.value;
                    i += length;
                    found = true;
                    break;
                }
            }
            if (found) continue;
        }
        chars += c;
        ++i;
    }
    flush();
}

std::string plainText(const Pieces& pieces) {
    std::string out;
    for (const Piece& piece : pieces) out += piece.text;
    return out;
}

std::string stripMarkup(std::string_view rich) {
    if (rich.find_first_of("<&") == std::string_view::npos) return std::string(rich);
    Pieces pieces;
    parseRich(rich, 0, pieces);
    return plainText(pieces);
}

void applyCase(Pieces& pieces, uint8_t textCase, bool english) {
    if (textCase == CaseNone || (textCase == TitleCase && !english)) return;
    struct Char {
        uint32_t code;
        uint32_t piece;
        bool locked;
    };
    // Kept between calls: case changes run on most entries
    thread_local std::vector<Char> chars;
    chars.clear();
    for (size_t p = 0; p < pieces.size(); ++p) {
        const std::string& text = pieces[p].text;
        bool locked = (pieces[p].mark & NoCase) != 0;
        for (size_t i = 0; i < text.size();) chars.push_back({nextChar(text, i), (uint32_t)p, locked});
    }
    auto setUpper = [&](size_t i) {
        if (!chars[i].locked) chars[i].code = upper(chars[i].code);
    };
    auto setLower = [&](size_t i) {
        if (!chars[i].locked) chars[i].code = lower(chars[i].code);
    };
    // Words: runs between spaces (title case also splits at hyphens and slashes)
    struct Word {
        size_t begin, end;
    };
    thread_local std::vector<Word> wordList;
    auto words = [&](bool title) -> const std::vector<Word>& {
        wordList.clear();
        size_t i = 0;
        while (i < chars.size()) {
            auto separator = [&](uint32_t c) {
                return isSpace(c) || (title && (c == '-' || c == '/' || c == 0x2013 || c == 0x2014));
            };
            while (i < chars.size() && separator(chars[i].code)) ++i;
            size_t begin = i;
            while (i < chars.size() && !separator(chars[i].code)) ++i;
            if (i > begin) wordList.push_back({begin, i});
        }
        return wordList;
    };
    auto firstLetter = [&](const Word& w) {
        for (size_t i = w.begin; i < w.end; ++i) {
            if (isLetter(chars[i].code)) return i;
        }
        return w.end;
    };
    auto lowercaseWord = [&](const Word& w) {
        for (size_t i = w.begin; i < w.end; ++i) {
            if (isUpperLetter(chars[i].code)) return false;
        }
        return true;
    };
    auto lockedWord = [&](const Word& w) {
        for (size_t i = w.begin; i < w.end; ++i) {
            if (chars[i].locked) return true;
        }
        return false;
    };
    bool anyLower = false, anyUpper = false;
    for (const Char& c : chars) {
        if (c.locked) continue;
        anyLower |= isLowerLetter(c.code);
        anyUpper |= isUpperLetter(c.code);
    }
    switch (textCase) {
        case Lowercase:
            for (size_t i = 0; i < chars.size(); ++i) setLower(i);
            break;
        case Uppercase:
            for (size_t i = 0; i < chars.size(); ++i) setUpper(i);
            break;
        case CapitalizeFirst:
        case Sentence: {
            const std::vector<Word>& list = words(false);
            if (list.empty()) break;
            if (textCase == Sentence && anyUpper && !anyLower) {
                bool first = true;
                for (size_t i = 0; i < chars.size(); ++i) {
                    if (!isLetter(chars[i].code)) continue;
                    if (!first) setLower(i);
                    first = false;
                }
                break;
            }
            size_t letter = firstLetter(list[0]);
            if (letter < list[0].end && lowercaseWord(list[0])) setUpper(letter);
            break;
        }
        case CapitalizeAll:
            for (const Word& w : words(false)) {
                size_t letter = firstLetter(w);
                if (letter < w.end && lowercaseWord(w)) setUpper(letter);
            }
            break;
        case TitleCase: {
            const std::vector<Word>& list = words(true);
            bool shouting = anyUpper && !anyLower;
            for (size_t k = 0; k < list.size(); ++k) {
                const Word& w = list[k];
                if (lockedWord(w)) continue;
                if (shouting) {
                    for (size_t i = w.begin; i < w.end; ++i) setLower(i);
                }
                std::string core;
                for (size_t i = w.begin; i < w.end; ++i) {
                    if (isLetter(chars[i].code)) appendChar(core, lower(chars[i].code));
                }
                bool afterColon = false;
                for (size_t i = w.begin; i-- > 0;) {
                    uint32_t c = chars[i].code;
                    if (isSpace(c)) continue;
                    afterColon = c == ':' || c == '?' || c == '!';
                    break;
                }
                bool edge = k == 0 || k + 1 == list.size() || afterColon;
                if (!edge && stopWord(core)) {
                    for (size_t i = w.begin; i < w.end; ++i) setLower(i);
                } else if (lowercaseWord(w)) {
                    size_t letter = firstLetter(w);
                    if (letter < w.end) setUpper(letter);
                }
            }
            break;
        }
    }
    for (Piece& piece : pieces) piece.text.clear();
    for (const Char& c : chars) appendChar(pieces[c.piece].text, c.code);
}

void stripPeriods(Pieces& pieces) {
    for (Piece& piece : pieces) {
        std::string kept;
        kept.reserve(piece.text.size());
        for (char c : piece.text) {
            if (c != '.') kept += c;
        }
        piece.text.swap(kept);
    }
}

Rich toRich(const Pieces& pieces) {
    // Pieces in one format make one run, its text sized once
    Rich rich;
    size_t i = 0, n = pieces.size();
    while (i < n) {
        const Piece& first = pieces[i];
        size_t end = i + 1, length = first.text.size();
        while (end < n && (pieces[end].text.empty() ||
                           (pieces[end].flags == first.flags && pieces[end].link == first.link))) {
            length += pieces[end].text.size();
            ++end;
        }
        if (length > 0) {
            Run run;
            run.text.reserve(length);
            for (size_t k = i; k < end; ++k) run.text += pieces[k].text;
            run.flags = first.flags;
            if (first.link) run.link = *first.link;
            if (!rich.empty() && rich.back().flags == run.flags && rich.back().link == run.link) {
                rich.back().text += run.text;
            } else {
                rich.push_back(std::move(run));
            }
        }
        i = end;
    }
    return rich;
}

namespace {

bool asciiLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool digit(char c) { return c >= '0' && c <= '9'; }

// The length of a range or list separator at text[i]: a dash, a comma or &
size_t separatorAt(std::string_view text, size_t i) {
    if (text[i] == ',' || text[i] == '&') return 1;
    return dashAt(text, i);
}
}

size_t dashAt(std::string_view text, size_t at) {
    if (at >= text.size()) return 0;
    if (text[at] == '-') return 1;
    if (text.compare(at, 3, "\xE2\x80\x93") == 0 || text.compare(at, 3, "\xE2\x80\x94") == 0) return 3;
    return 0;
}

bool isNumeric(std::string_view text) {
    std::string value = trim(text);
    size_t i = 0, n = value.size();
    if (n == 0) return false;
    for (;;) {
        while (i < n && asciiLetter(value[i])) ++i;
        size_t digits = i;
        while (i < n && digit(value[i])) ++i;
        if (i == digits) return false;
        while (i < n && asciiLetter(value[i])) ++i;
        while (i < n && value[i] == ' ') ++i;
        if (i == n) return true;
        size_t separator = separatorAt(value, i);
        if (!separator) return false;
        i += separator;
        while (i < n && value[i] == ' ') ++i;
        if (i == n) return false;
    }
}

bool hasSeveral(std::string_view text) {
    int parts = 0;
    bool inPart = false;
    for (size_t i = 0; i < text.size();) {
        size_t separator = separatorAt(text, i);
        // A thousands comma, as in 10,000,001, joins one number
        if (separator && text[i] == ',' && i > 0 && digit(text[i - 1]) && i + 3 < text.size() &&
            digit(text[i + 1]) && digit(text[i + 2]) && digit(text[i + 3]) &&
            (i + 4 == text.size() || !digit(text[i + 4]))) {
            separator = 0;
        }
        if (separator) {
            inPart = false;
            i += separator;
            continue;
        }
        if (text[i] != ' ' && !inPart) {
            inPart = true;
            ++parts;
        }
        ++i;
    }
    return parts >= 2;
}

std::string roman(int number) {
    if (number <= 0 || number >= 4000) return std::to_string(number);
    static const struct {
        int value;
        const char* letters;
    } table[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"}, {50, "l"},
                 {40, "xl"},  {10, "x"},   {9, "ix"},   {5, "v"},   {4, "iv"},   {1, "i"}};
    std::string out;
    for (const auto& entry : table) {
        while (number >= entry.value) {
            out += entry.letters;
            number -= entry.value;
        }
    }
    return out;
}

std::string collationKey(std::string_view text) {
    // Latin-1 and Latin Extended-A letters fold to their base letters
    static const char* const latin1 =
        "aaaaaa\x01" "ceeeeiiii" "dnooooo\x02" "ouuuuy\x03\x04" "aaaaaa\x01" "ceeeeiiii" "dnooooo\x02" "ouuuuy\x03y";
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        uint32_t c = lower(nextChar(text, i));
        if (c >= 0xC0 && c <= 0xFF) {
            char folded = latin1[c - 0xC0];
            if (folded == '\x01') out += "ae";
            else if (folded == '\x02') appendChar(out, c);  // the multiplication and division signs
            else if (folded == '\x03') out += "th";
            else if (folded == '\x04') out += "ss";
            else out += folded;
        } else if (c >= 0x100 && c <= 0x17F) {
            static const struct {
                uint32_t last;
                const char* base;
            } ranges[] = {{0x105, "a"}, {0x10D, "c"}, {0x111, "d"}, {0x11B, "e"}, {0x123, "g"}, {0x127, "h"},
                          {0x131, "i"}, {0x133, "ij"}, {0x135, "j"}, {0x138, "k"}, {0x142, "l"}, {0x14B, "n"},
                          {0x151, "o"}, {0x153, "oe"}, {0x159, "r"}, {0x161, "s"}, {0x167, "t"}, {0x173, "u"},
                          {0x175, "w"}, {0x178, "y"}, {0x17E, "z"}, {0x17F, "s"}};
            for (const auto& range : ranges) {
                if (c <= range.last) {
                    out += range.base;
                    break;
                }
            }
        } else {
            appendChar(out, c);
        }
    }
    return out;
}

std::string plain(const Rich& rich) {
    std::string out;
    for (const Run& run : rich) out += run.text;
    return out;
}

namespace {

void escapeHtml(std::string& out, std::string_view text, bool attribute) {
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"':
                if (attribute) {
                    out += "&quot;";
                    break;
                }
                out += c;
                break;
            default: out += c;
        }
    }
}
}

std::string html(const Rich& rich) {
    static const struct {
        uint16_t flag;
        const char* open;
        const char* close;
    } tags[] = {{Italic, "<i>", "</i>"},
                {Bold, "<b>", "</b>"},
                {SmallCaps, "<span style=\"font-variant:small-caps\">", "</span>"},
                {Superscript, "<sup>", "</sup>"},
                {Subscript, "<sub>", "</sub>"},
                {Underline, "<u>", "</u>"}};
    std::string out;
    for (const Run& run : rich) {
        if (!run.link.empty()) {
            out += "<a href=\"";
            escapeHtml(out, run.link, true);
            out += "\">";
        }
        for (const auto& tag : tags) {
            if (run.flags & tag.flag) out += tag.open;
        }
        escapeHtml(out, run.text, false);
        for (size_t k = sizeof tags / sizeof tags[0]; k-- > 0;) {
            if (run.flags & tags[k].flag) out += tags[k].close;
        }
        if (!run.link.empty()) out += "</a>";
    }
    return out;
}
}
