// Paper export (paper.h, docs/specs/scholar.md phase 3): pandoc's front
// matter variables for a paper, read from the YAML the properties leave
// opaque, and the title block print sets from them.

#pragma optimize("s", on)

#include "paper.h"
#include "app.h"
#include "print.h"
#include "utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace paper {
namespace {

using qmd::ElementPtr;
using qmd::ElementType;

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

size_t indentOf(const std::string& line) {
    size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return n;
}

bool blank(const std::string& line) { return line.find_first_not_of(" \t\r") == std::string::npos; }

// A comment starts at a # after whitespace, outside quotes
std::string uncomment(const std::string& text) {
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == '\\' && quote == '"') ++i;
            else if (c == quote && quote == '\'' && i + 1 < text.size() && text[i + 1] == '\'') ++i;
            else if (c == quote) quote = 0;
        } else if ((c == '"' || c == '\'') &&
                   (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\t' || text[i - 1] == '[' || text[i - 1] == ',')) {
            quote = c;
        } else if (c == '#' && (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\t')) {
            return text.substr(0, i);
        }
    }
    return text;
}

void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// One scalar: quotes and their escapes undone, a plain one as written
std::string scalar(const std::string& raw) {
    const std::string v = trim(raw);
    if (v.size() < 2 || (v.front() != '"' && v.front() != '\'')) return v;
    const char quote = v.front();
    std::string out;
    for (size_t i = 1; i < v.size(); ++i) {
        const char c = v[i];
        if (c == quote) {
            if (quote == '\'' && i + 1 < v.size() && v[i + 1] == '\'') {
                out += '\'';
                ++i;
                continue;
            }
            break;
        }
        if (quote == '"' && c == '\\' && i + 1 < v.size()) {
            const char e = v[++i];
            if (e == 'n') out += '\n';
            else if (e == 't') out += '\t';
            else if (e == 'u' && i + 4 < v.size()) {
                appendUtf8(out, (unsigned)std::strtoul(v.substr(i + 1, 4).c_str(), nullptr, 16));
                i += 4;
            } else if (e == '"' || e == '\\' || e == '/') out += e;
            else {
                out += '\\';  // TeX in a title keeps its backslash
                out += e;
            }
            continue;
        }
        out += c;
    }
    return out;
}

// "name: Ana" in a list item: a mapping's key and what follows it
bool mapping(const std::string& text, std::string& key, std::string& rest) {
    size_t i = 0;
    while (i < text.size() && (std::isalnum((unsigned char)text[i]) || text[i] == '_' || text[i] == '-')) ++i;
    if (i == 0 || i >= text.size() || text[i] != ':' || (i + 1 < text.size() && text[i + 1] != ' ')) return false;
    key = text.substr(0, i);
    rest = text.substr(i + 1);
    return true;
}

// A key at the left edge, and what follows its colon
bool keyLine(const std::string& line, std::string& key, std::string& rest) {
    if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '#' || line[0] == '-') return false;
    size_t colon = std::string::npos;
    if (line[0] == '"' || line[0] == '\'') {
        const size_t close = line.find(line[0], 1);
        if (close == std::string::npos || close + 1 >= line.size() || line[close + 1] != ':') return false;
        key = line.substr(1, close - 1);
        colon = close + 1;
    } else {
        colon = line.find(':');
        if (colon == std::string::npos) return false;
        key = trim(line.substr(0, colon));
    }
    if (colon + 1 < line.size() && line[colon + 1] != ' ' && line[colon + 1] != '\t') return false;
    rest = line.substr(colon + 1);
    return !key.empty();
}

// A top-level key, what follows its colon and the lines under it
struct Entry {
    std::string key, rest;
    std::vector<std::string> body;
};

std::vector<Entry> entries(const std::string& yaml) {
    std::vector<Entry> out;
    for (size_t at = 0; at < yaml.size();) {
        const size_t nl = yaml.find('\n', at);
        std::string line = yaml.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string key, rest;
        if (keyLine(line, key, rest)) out.push_back({key, rest, {}});
        else if (!out.empty()) out.back().body.push_back(line);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    return out;
}

// A block scalar under its header: | keeps the line breaks, > folds lines
// into paragraphs; - drops the final break, + keeps every one (YAML 1.2)
std::string blockScalar(const std::string& header, const std::vector<std::string>& body) {
    const bool literal = header[0] == '|';
    char chomp = 0;
    size_t indent = 0;
    for (size_t i = 1; i < header.size() && header[i] != ' ' && header[i] != '#'; ++i) {
        if (header[i] == '-' || header[i] == '+') chomp = header[i];
        else if (header[i] >= '1' && header[i] <= '9') indent = (size_t)(header[i] - '0');
    }
    if (!indent) {
        for (const auto& line : body) {
            if (!blank(line)) {
                indent = indentOf(line);
                break;
            }
        }
    }
    if (!indent) return {};
    std::vector<std::string> lines;
    for (const auto& line : body) {
        if (blank(line)) lines.emplace_back();
        else if (indentOf(line) < indent) break;
        else lines.push_back(line.substr(indent));
    }
    size_t trailing = 0;
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
        ++trailing;
    }
    std::string out;
    size_t empties = 0;
    bool started = false, wasIndented = false;
    for (const auto& line : lines) {
        if (line.empty()) {
            ++empties;
            continue;
        }
        const bool indented = line[0] == ' ' || line[0] == '\t';
        if (started) {
            if (literal) out += std::string(empties + 1, '\n');
            else if (empties) out += std::string(empties, '\n');
            else out += indented || wasIndented ? '\n' : ' ';
        }
        out += line;
        started = true;
        empties = 0;
        wasIndented = indented;
    }
    if (chomp == '+') out += std::string(trailing + 1, '\n');
    else if (chomp != '-' && !out.empty()) out += '\n';
    return out;
}

// [a, "b, c", {name: D}]: its items, perhaps written over several lines
void flowItems(const std::string& flow, std::vector<std::string>& items) {
    auto push = [&](const std::string& raw) {
        std::string item = trim(raw), key, rest;
        if (!item.empty() && item[0] == '{') {
            // pandoc's author object: its name
            const size_t at = item.find("name:");
            if (at == std::string::npos) return;
            item = item.substr(at + 5);
            const size_t end = item.find_first_of(",}");
            item = item.substr(0, end);
        }
        item = scalar(item);
        if (!item.empty()) items.push_back(item);
    };
    int depth = 0;
    char quote = 0;
    std::string current;
    for (size_t i = 1; i < flow.size(); ++i) {
        const char c = flow[i];
        if (quote) {
            current += c;
            if (c == '\\' && quote == '"' && i + 1 < flow.size()) current += flow[++i];
            else if (c == quote) quote = 0;
            continue;
        }
        // Quotes open an item, never inside one (O'Brien)
        if ((c == '"' || c == '\'') && trim(current).empty()) quote = c;
        if (c == '[' || c == '{') ++depth;
        if ((c == ']' || c == '}') && depth-- == 0) break;
        if (c == ',' && depth == 0) {
            push(current);
            current.clear();
            continue;
        }
        current += c;
    }
    push(current);
}

// A scalar value: inline, a block scalar, or lines folded into one
std::string scalarOf(const Entry& e) {
    const std::string value = trim(uncomment(e.rest));
    if (!value.empty() && (value[0] == '|' || value[0] == '>')) return blockScalar(value, e.body);
    std::string joined = value;
    bool listed = false;
    for (const auto& line : e.body) {
        if (blank(line)) {
            if (!joined.empty()) joined += '\n';
            continue;
        }
        const std::string text = trim(uncomment(line));
        if (text.empty()) continue;
        // A list under an empty key: its items, comma separated
        if (text == "-" || text.rfind("- ", 0) == 0) {
            if (!value.empty()) break;
            joined += (joined.empty() ? "" : ", ") + scalar(text.substr(1));
            listed = true;
            continue;
        }
        if (indentOf(line) == 0 || listed) break;
        joined += (joined.empty() || joined.back() == '\n' ? "" : " ") + text;
    }
    return trim(listed ? joined : scalar(trim(joined)));
}

// A list value: [a, b], a block list of "- item" lines (an item that is a
// mapping gives its name:), or a single scalar
std::vector<std::string> listOf(const Entry& e) {
    std::vector<std::string> items;
    const std::string value = trim(uncomment(e.rest));
    if (!value.empty() && value[0] == '[') {
        std::string flow = trim(e.rest);
        for (const auto& line : e.body) {
            if (flow.find(']') != std::string::npos) break;
            flow += ' ' + trim(line);
        }
        flowItems(flow, items);
        return items;
    }
    if (!value.empty()) {
        const std::string one = scalarOf(e);
        if (!one.empty()) items.push_back(one);
        return items;
    }
    size_t itemIndent = std::string::npos;
    bool inMapping = false;
    for (const auto& line : e.body) {
        if (blank(line)) continue;
        const size_t indent = indentOf(line);
        const std::string text = trim(uncomment(line));
        if (text.empty()) continue;
        std::string key, rest;
        if (text == "-" || text.rfind("- ", 0) == 0) {
            if (itemIndent == std::string::npos) itemIndent = indent;
            if (indent != itemIndent) continue;  // a list inside an item
            std::string item = trim(text.substr(1));
            inMapping = mapping(item, key, rest);
            items.push_back(inMapping ? (key == "name" ? scalar(rest) : std::string()) : scalar(item));
        } else if (itemIndent != std::string::npos && indent > itemIndent && !items.empty()) {
            if (mapping(text, key, rest)) {
                if (key == "name" && items.back().empty()) items.back() = scalar(rest);
                inMapping = true;
            } else if (!inMapping) {
                items.back() += ' ' + text;  // a plain item over two lines
            }
        }
    }
    items.erase(std::remove_if(items.begin(), items.end(), [](const std::string& s) { return trim(s).empty(); }),
                items.end());
    return items;
}

// "2cm", "1in", "20mm", "72pt" in millimetres; 0 when unread
float lengthMm(const std::string& text) {
    const std::string t = lower(trim(text));
    char* end = nullptr;
    const float value = std::strtof(t.c_str(), &end);
    if (!end || end == t.c_str() || value <= 0) return 0;
    const std::string unit = trim(end);
    if (unit == "mm") return value;
    if (unit == "cm") return value * 10.0f;
    if (unit == "in") return value * 25.4f;
    if (unit == "pt") return value * 25.4f / 72.27f;
    if (unit == "bp") return value * 25.4f / 72.0f;
    return 0;
}

// Comma-separated options inside list items: [twocolumn, a4paper] and "twocolumn,10pt"
std::vector<std::string> options(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    for (const auto& item : items) {
        size_t at = 0;
        while (at <= item.size()) {
            const size_t comma = item.find(',', at);
            const std::string option = lower(trim(item.substr(at, comma == std::string::npos ? std::string::npos : comma - at)));
            if (!option.empty()) out.push_back(option);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
    }
    return out;
}

// In French a no-break space stands before the colon; Chinese writes
// keywords with full-width marks (GB/T 7713), Japanese too
const Words kWords[] = {
    {"en", "Abstract", "Keywords", ", ", " and ", " et al.", ": ", ", "},
    {"de", "Zusammenfassung", "Schl\xC3\xBCsselw\xC3\xB6rter", ", ", " und ", " et al.", ": ", ", "},
    {"fr", "R\xC3\xA9sum\xC3\xA9", "Mots-cl\xC3\xA9s", ", ", " et ", " et al.", "\xC2\xA0: ", ", "},
    {"it", "Sommario", "Parole chiave", ", ", " e ", " et al.", ": ", ", "},
    {"es", "Resumen", "Palabras clave", ", ", " y ", " et al.", ": ", ", "},
    {"pt", "Resumo", "Palavras-chave", ", ", " e ", " et al.", ": ", ", "},
    {"ru", "\xD0\x90\xD0\xBD\xD0\xBD\xD0\xBE\xD1\x82\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8F",
     "\xD0\x9A\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB5\xD0\xB2\xD1\x8B\xD0\xB5 \xD1\x81\xD0\xBB\xD0\xBE\xD0\xB2\xD0\xB0", ", ",
     " \xD0\xB8 ", " \xD0\xB8 \xD0\xB4\xD1\x80.", ": ", ", "},
    {"zh", "\xE6\x91\x98\xE8\xA6\x81", "\xE5\x85\xB3\xE9\x94\xAE\xE8\xAF\x8D", "\xE3\x80\x81", "\xE3\x80\x81",
     "\xE7\xAD\x89", "\xEF\xBC\x9A", "\xEF\xBC\x9B"},
    {"ja", "\xE6\xA6\x82\xE8\xA6\x81", "\xE3\x82\xAD\xE3\x83\xBC\xE3\x83\xAF\xE3\x83\xBC\xE3\x83\x89", "\xE3\x80\x81",
     "\xE3\x80\x81", "\xE3\x81\xBB\xE3\x81\x8B", "\xEF\xBC\x9A", "\xE3\x80\x81"},
    {"ko", "\xEC\x9A\x94\xEC\x95\xBD", "\xEC\xA3\xBC\xEC\xA0\x9C\xEC\x96\xB4", ", ", ", ", " \xEC\x99\xB8", ": ", ", "},
};

}  // namespace

const Words& wordsFor(const std::string& lang) {
    const std::string two = lower(lang.substr(0, 2));
    for (const auto& w : kWords) {
        if (two == w.lang) return w;
    }
    return kWords[0];
}

Front read(const std::string& yaml) {
    Front f;
    for (const Entry& e : entries(yaml)) {
        const std::string key = lower(e.key);
        if (key == "title") f.title = scalarOf(e);
        else if (key == "subtitle") f.subtitle = scalarOf(e);
        else if (key == "shorttitle" || key == "short-title" || key == "running-head") f.shortTitle = scalarOf(e);
        else if (key == "date") f.date = scalarOf(e);
        else if (key == "abstract") f.abstract = trim(scalarOf(e));
        else if (key == "thanks") f.thanks = scalarOf(e);
        else if (key == "lang") f.lang = scalarOf(e);
        else if (key == "author" || key == "authors") f.authors = listOf(e);
        else if (key == "institute") f.institutes = listOf(e);
        else if (key == "keywords") f.keywords = listOf(e);
        else if (key == "classoption") {
            for (const auto& option : options(listOf(e))) {
                if (option == "twocolumn") f.twoColumn = true;
                else if (option == "onecolumn") f.twoColumn = false;
            }
        } else if (key == "papersize") {
            std::string size = lower(scalarOf(e));
            if (size.size() > 5 && size.compare(size.size() - 5, 5, "paper") == 0) size.resize(size.size() - 5);
            if (size == "a4" || size == "letter" || size == "legal" || size == "a3") f.paperSize = size;
        } else if (key == "fontsize") {
            const std::string size = lower(scalarOf(e));
            const float pt = std::strtof(size.c_str(), nullptr);
            if (pt >= 8.0f && pt <= 14.0f) f.fontSizePt = pt;
        } else if (key == "geometry") {
            for (const auto& option : options(listOf(e))) {
                if (option.rfind("margin=", 0) != 0) continue;
                const float mm = lengthMm(option.substr(7));
                if (mm > 0) f.marginMm = std::clamp(mm, 5.0f, 50.0f);
            }
        }
    }
    return f;
}

Front frontOf(const ElementPtr& root) {
    if (!root || root->children.empty() || root->children[0]->type != ElementType::Properties) return {};
    return read(root->children[0]->text);
}

std::string joinAuthors(const std::vector<std::string>& authors, const std::string& lang) {
    const Words& w = wordsFor(lang);
    std::string out;
    for (size_t i = 0; i < authors.size(); ++i) {
        if (i > 0) out += i + 1 == authors.size() ? w.andWord : w.comma;
        out += authors[i];
    }
    return out;
}

std::string headAuthors(const std::vector<std::string>& authors, const std::string& lang) {
    if (authors.empty()) return {};
    std::string name = trim(plainText(authors[0]));
    // "Okafor, Ana" names the surname first; "Ana Okafor" last; a name
    // without spaces ("Zhang San" in characters) is all surname
    const size_t comma = name.find(',');
    if (comma != std::string::npos) {
        name = trim(name.substr(0, comma));
    } else {
        const size_t space = name.find_last_of(' ');
        if (space != std::string::npos) name = name.substr(space + 1);
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ';')) name.pop_back();
    return authors.size() > 1 ? name + wordsFor(lang).etAl : name;
}

int paperIndex(const Front& front) {
    if (front.paperSize.empty()) return -1;
    for (int i = 0; i < PRINT_PAPER_COUNT; ++i) {
        std::string name;
        for (const wchar_t* c = PRINT_PAPERS[i].name; *c; ++c) name += (char)std::tolower((int)*c);
        if (name == front.paperSize) return i;
    }
    return -1;
}

}  // namespace paper

// --- The title block ---------------------------------------------------------

namespace {

using qmd::ElementPtr;
using qmd::ElementType;

// Inline Markdown as one run of text and the stretches it styles
enum Style : unsigned { Bold = 1, Italic = 2, Mono = 4, Strike = 8, Under = 16 };
struct Styled {
    std::wstring text;
    struct Range {
        UINT32 start, length;
        unsigned style;
    };
    std::vector<Range> ranges;
    void add(const std::wstring& s, unsigned style) {
        if (s.empty()) return;
        if (style) ranges.push_back({(UINT32)text.size(), (UINT32)s.size(), style});
        text += s;
    }
    void add(const Styled& other, unsigned style) {
        const UINT32 base = (UINT32)text.size();
        if (style) ranges.push_back({base, (UINT32)other.text.size(), style});
        for (const auto& r : other.ranges) ranges.push_back({base + r.start, r.length, r.style});
        text += other.text;
    }
};

void flatten(const ElementPtr& e, unsigned style, Styled& out) {
    switch (e->type) {
        case ElementType::Text: out.add(toWide(e->text), style); return;
        case ElementType::Code: out.add(toWide(e->text), style | Mono); break;
        case ElementType::SoftBreak: out.add(L" ", style); return;
        case ElementType::HardBreak: out.add(L"\n", style); return;
        case ElementType::Emphasis: style |= Italic; break;
        case ElementType::Strong: style |= Bold; break;
        case ElementType::Strikethrough: style |= Strike; break;
        case ElementType::Underline: style |= Under; break;
        // TeX reads as written: the title block sets no maths
        case ElementType::MathInline:
        case ElementType::MathDisplay: out.add(L"$" + toWide(e->text) + L"$", style); return;
        case ElementType::FootnoteReference:
        case ElementType::FootnoteBacklink:
        case ElementType::RubyText: return;
        default: break;
    }
    for (const auto& child : e->children) flatten(child, style, out);
}

void trimStyled(Styled& s) {
    size_t end = s.text.find_last_not_of(L" \t\r\n");
    s.text.resize(end == std::wstring::npos ? 0 : end + 1);
    for (auto& r : s.ranges) {
        if (r.start >= s.text.size()) r.length = 0;
        else r.length = std::min<UINT32>(r.length, (UINT32)s.text.size() - r.start);
    }
}

// Markdown's blocks as styled paragraphs; a list's items each a paragraph
std::vector<Styled> paragraphsOf(const std::string& markdown) {
    std::vector<Styled> out;
    qmd::MarkdownParser parser;
    const auto parsed = parser.parse(markdown);
    if (!parsed.success || !parsed.root) return out;
    for (const auto& block : parsed.root->children) {
        if (block->type == ElementType::List) {
            int n = block->start;
            for (const auto& item : block->children) {
                Styled s;
                s.add(block->ordered ? std::to_wstring(n++) + L". " : std::wstring(L"\x2022 "), 0);
                flatten(item, 0, s);
                trimStyled(s);
                out.push_back(std::move(s));
            }
            continue;
        }
        Styled s;
        flatten(block, 0, s);
        trimStyled(s);
        if (!s.text.empty()) out.push_back(std::move(s));
    }
    return out;
}

// A one-line field (a title, a name): its Markdown when it is a paragraph
// of text, else as written ("2026. A year" is no list)
Styled inlineOf(const std::string& markdown) {
    qmd::MarkdownParser parser;
    const auto parsed = parser.parse(markdown);
    Styled s;
    if (parsed.success && parsed.root && parsed.root->children.size() == 1 &&
        parsed.root->children[0]->type == ElementType::Paragraph) {
        flatten(parsed.root->children[0], 0, s);
        trimStyled(s);
    } else {
        s.add(toWide(markdown), 0);
    }
    return s;
}

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)std::max(n, 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

// One piece of the block: its paragraphs and how they are set
struct Piece {
    std::vector<Styled> paragraphs;
    const wchar_t* family = nullptr;
    float size = 16.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    bool centre = true;
    float inset = 0;     // on each side
    float leading = 1.4f;
    float gap = 0;       // between paragraphs
    D2D1_COLOR_F color{};
};

// A line of a paragraph in the piece's format, its styles applied
IDWriteTextLayout* lineLayout(App& app, IDWriteTextFormat* format, const Piece& piece, const Styled& s, UINT32 start,
                              UINT32 length, float width) {
    while (length > 0 && (s.text[start + length - 1] == L'\n' || s.text[start + length - 1] == L'\r')) --length;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(s.text.c_str() + start, length, format, width, 10000.0f, &layout);
    if (!layout) return nullptr;
    IDWriteTextLayout2* layout2 = nullptr;
    if (app.fontFallback && SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout2), (void**)&layout2))) {
        layout2->SetFontFallback(app.fontFallback);
        layout2->Release();
    }
    for (const auto& r : s.ranges) {
        const UINT32 from = std::max(r.start, start), to = std::min(r.start + r.length, start + length);
        if (from >= to) continue;
        const DWRITE_TEXT_RANGE range{from - start, to - from};
        if (r.style & Bold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        if (r.style & Italic) layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
        if (r.style & Strike) layout->SetStrikethrough(TRUE, range);
        if (r.style & Under) layout->SetUnderline(TRUE, range);
        if (r.style & Mono) {
            layout->SetFontFamilyName(app.theme.codeFontFamily, range);
            layout->SetFontSize(piece.size * 0.88f, range);
        }
    }
    return layout;
}

// Sets a piece a line at a time, a text run a line, so a page can break
// between any two; returns the y under it
float place(App& app, const Piece& piece, float x, float y, float width) {
    if (piece.paragraphs.empty()) return y;
    IDWriteTextFormat* format = nullptr;
    app.dwriteFactory->CreateTextFormat(piece.family, nullptr, piece.weight, piece.style, DWRITE_FONT_STRETCH_NORMAL,
                                        piece.size, L"en-us", &format);
    if (!format) return y;
    const float lineH = piece.size * piece.leading;
    format->SetTextAlignment(piece.centre ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lineH, lineH * 0.78f);
    const float left = x + piece.inset, w = std::max(1.0f, width - 2 * piece.inset);
    for (size_t p = 0; p < piece.paragraphs.size(); ++p) {
        const Styled& s = piece.paragraphs[p];
        if (p > 0) y += piece.gap;
        IDWriteTextLayout* whole = lineLayout(app, format, piece, s, 0, (UINT32)s.text.size(), w);
        if (!whole) continue;
        UINT32 count = 0;
        whole->GetLineMetrics(nullptr, 0, &count);
        std::vector<DWRITE_LINE_METRICS> lines(count);
        if (count) whole->GetLineMetrics(lines.data(), count, &count);
        whole->Release();
        const size_t docBase = app.docText.size();
        UINT32 start = 0;
        for (const auto& line : lines) {
            if (IDWriteTextLayout* one = lineLayout(app, format, piece, s, start, line.length, w)) {
                App::LayoutTextRun run;
                run.layout = one;
                run.pos = D2D1::Point2F(left, y);
                run.bounds = D2D1::RectF(left, y, left + w, y + lineH);
                run.color = piece.color;
                run.docStart = docBase + start;
                run.docLength = line.length;
                app.layoutTextRuns.push_back(run);
            }
            start += line.length;
            y += lineH;
        }
        app.docText += s.text + L"\n\n";
    }
    format->Release();
    return y;
}

}  // namespace

std::string paper::plainText(const std::string& markdown) { return utf8(inlineOf(markdown).text); }

bool layoutTitleBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    if (!app.paperPrint.titleBlock || !elem || elem->type != ElementType::Properties) return false;
    const paper::Front front = paper::read(elem->text);
    if (!front.titleBlock()) return false;

    const D2DTheme& theme = app.theme;
    const float em = app.textFormat ? app.textFormat->GetFontSize() : 16.0f;
    const float width = std::max(maxWidth, app.paperPrint.width);
    const float x = indent;
    D2D1_COLOR_F muted = theme.text;
    muted.a *= 0.72f;
    D2D1_COLOR_F rule = theme.text;
    rule.a *= 0.18f;
    const wchar_t* body = theme.fontFamily;
    const wchar_t* heading = themeHeadingFont(theme);

    auto piece = [&](const wchar_t* family, float size, D2D1_COLOR_F color) {
        Piece p;
        p.family = family;
        p.size = size;
        p.color = color;
        return p;
    };

    // The title, its mark leading to the thanks
    Piece title = piece(heading, em * 1.6f, theme.heading);
    title.weight = DWRITE_FONT_WEIGHT_BOLD;
    title.leading = 1.25f;
    title.paragraphs.push_back(inlineOf(front.title));
    if (!front.thanks.empty()) title.paragraphs.back().add(L"*", 0);
    y = place(app, title, x, y, width) + em * 0.3f;

    if (!front.subtitle.empty()) {
        Piece subtitle = piece(heading, em * 1.2f, theme.heading);
        subtitle.leading = 1.3f;
        subtitle.paragraphs.push_back(inlineOf(front.subtitle));
        y = place(app, subtitle, x, y, width) + em * 0.25f;
    }
    y += em * 0.5f;
    if (!front.authors.empty()) {
        Piece authors = piece(body, em * 1.1f, theme.text);
        authors.paragraphs.push_back(inlineOf(paper::joinAuthors(front.authors, front.lang)));
        y = place(app, authors, x, y, width) + em * 0.15f;
    }
    if (!front.institutes.empty()) {
        Piece institutes = piece(body, em * 0.9f, muted);
        institutes.style = DWRITE_FONT_STYLE_ITALIC;
        for (const auto& institute : front.institutes) institutes.paragraphs.push_back(inlineOf(institute));
        y = place(app, institutes, x, y, width) + em * 0.15f;
    }
    if (!front.date.empty()) {
        Piece date = piece(body, em, theme.text);
        date.paragraphs.push_back(inlineOf(front.date));
        y = place(app, date, x, y + em * 0.3f, width);
    }
    y += em * 1.1f;

    // The abstract, set in from both sides as LaTeX's is
    const paper::Words& words = paper::wordsFor(front.lang);
    const float inset = std::min(width * 0.06f, em * 3.0f);
    if (!front.abstract.empty()) {
        Piece label = piece(body, em * 0.95f, theme.heading);
        label.weight = DWRITE_FONT_WEIGHT_BOLD;
        Styled word;
        word.add(toWide(words.abstract), 0);
        label.paragraphs.push_back(word);
        y = place(app, label, x, y, width) + em * 0.3f;
        Piece text = piece(body, em * 0.92f, theme.text);
        text.centre = false;
        text.inset = inset;
        text.leading = 1.5f;
        text.gap = em * 0.45f;
        text.paragraphs = paragraphsOf(front.abstract);
        y = place(app, text, x, y, width) + em * 0.5f;
    }
    if (!front.keywords.empty()) {
        Piece keywords = piece(body, em * 0.92f, theme.text);
        keywords.centre = false;
        keywords.inset = inset;
        keywords.leading = 1.5f;
        Styled line;
        line.add(toWide(words.keywords) + toWide(words.colon), Italic | Bold);
        for (size_t i = 0; i < front.keywords.size(); ++i) {
            if (i) line.add(toWide(words.between), 0);
            line.add(inlineOf(front.keywords[i]), 0);
        }
        keywords.paragraphs.push_back(line);
        y = place(app, keywords, x, y, width) + em * 0.5f;
    }
    // The thanks as a footnote under its rule
    if (!front.thanks.empty()) {
        y += em * 0.2f;
        app.layoutLines.push_back({D2D1::Point2F(x + inset, y), D2D1::Point2F(x + inset + em * 6.0f, y), rule, 1.0f});
        y += em * 0.35f;
        Piece thanks = piece(body, em * 0.8f, muted);
        thanks.centre = false;
        thanks.inset = inset;
        Styled note;
        note.add(L"* ", 0);
        note.add(inlineOf(front.thanks), 0);
        thanks.paragraphs.push_back(note);
        y = place(app, thanks, x, y, width) + em * 0.3f;
    }
    // A hairline across the page under the block, then the text
    y += em * 0.6f;
    app.layoutLines.push_back({D2D1::Point2F(x, y), D2D1::Point2F(x + width, y), rule, 1.0f});
    y += em * 1.2f;
    app.paperPrint.titleBottom = y;
    return true;
}
