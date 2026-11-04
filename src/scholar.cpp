// Scholar (scholar.h): figures, tables and equations numbered as
// pandoc-crossref numbers them, and citations formatted by the CSL
// processor from the document's bibliography, as pandoc's citeproc does;
// and the keys and ids the editors offer as a citation is typed.

#pragma optimize("s", on)

#include "scholar.h"
#include "bib.h"
#include "cite.h"
#include "csl.h"
#include "csl_bundle.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace scholar {

using qmd::Element;
using qmd::ElementPtr;
using qmd::ElementType;

namespace {

const fm::Property* property(const std::vector<fm::Property>& front, const char* key) {
    for (const auto& p : front) {
        if (p.key == key) return &p;
    }
    return nullptr;
}

std::string scalar(const std::vector<fm::Property>& front, const char* key) {
    const fm::Property* p = property(front, key);
    return p && p->kind != fm::Kind::List && p->kind != fm::Kind::Structured ? p->text : std::string();
}

// A prefix key: one word, or a list of the singular and the plural
void prefixInto(const std::vector<fm::Property>& front, const char* key, std::string (&into)[2]) {
    const fm::Property* p = property(front, key);
    if (!p) return;
    if (p->kind == fm::Kind::List && !p->items.empty()) {
        into[0] = p->items[0];
        into[1] = p->items.size() > 1 ? p->items[1] : p->items[0];
    } else if (!p->text.empty()) {
        into[0] = into[1] = p->text;
    }
}

const std::vector<fm::Property>& frontOf(const ElementPtr& root) {
    static const std::vector<fm::Property> none;
    if (!root || root->children.empty() || root->children[0]->type != ElementType::Properties) return none;
    return root->children[0]->properties;
}

ElementPtr text(const std::string& value, Element* parent) {
    auto t = std::make_shared<Element>(ElementType::Text);
    t->text = value;
    t->parent = parent;
    return t;
}

std::string plain(const ElementPtr& elem) {
    if (elem->type == ElementType::Text) return elem->text;
    std::string out;
    for (const auto& child : elem->children) out += plain(child);
    return out;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)std::max(len, 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)std::max(len, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

// --- Numbering ---------------------------------------------------------------

// "Figure 1:" in bold before the caption's own words
void label(const ElementPtr& caption, const std::string& words) {
    auto strong = std::make_shared<Element>(ElementType::Strong);
    strong->parent = caption.get();
    strong->children.push_back(text(words, strong.get()));
    std::vector<ElementPtr> kids;
    kids.push_back(strong);
    // A space after "Figure 1:", none after a full-width colon
    const bool wide = !words.empty() && (unsigned char)words.back() >= 0x80;
    if (!caption->children.empty() && !wide) kids.push_back(text(" ", caption.get()));
    for (auto& child : caption->children) kids.push_back(child);
    caption->children = std::move(kids);
}

// What each id numbers: 'f' figure, 't' table, 'e' equation, 's' section,
// and its number; or, for a section in a document that numbers none, its
// title, which a reference reads instead (named)
struct Target {
    char kind = 0;
    std::string number;
    bool named = false;
};

struct Numbering {
    const Words& words;
    int figures = 0, tables = 0, equations = 0;
    std::map<std::string, Target> ids;
};

// An equation's \tag and \label names, as #314 reads them
void equationLabels(const std::string& tex, std::map<std::string, Target>& ids) {
    std::string tag;
    if (size_t at = tex.find("\\tag{"); at != std::string::npos) {
        size_t close = tex.find('}', at + 5);
        if (close != std::string::npos) tag = tex.substr(at + 5, close - at - 5);
    }
    if (tag.empty()) return;
    for (size_t at = tex.find("\\label{"); at != std::string::npos; at = tex.find("\\label{", at + 7)) {
        size_t close = tex.find('}', at + 7);
        if (close == std::string::npos) break;
        ids.emplace(tex.substr(at + 7, close - at - 7), Target{'e', tag});
    }
}

void number(const ElementPtr& elem, Numbering& n) {
    if (elem->type == ElementType::Figure) {
        ElementPtr caption;
        for (const auto& child : elem->children)
            if (child->type == ElementType::Caption) caption = child;
        const bool table = elem->language == "tbl";
        if (!elem->title.empty() && caption) {
            elem->level = table ? ++n.tables : ++n.figures;
            label(caption, (table ? n.words.table : n.words.figure) + " " + std::to_string(elem->level) +
                               n.words.delim);
            n.ids.emplace(elem->title, Target{table ? 't' : 'f', std::to_string(elem->level)});
        }
    } else if (elem->type == ElementType::MathDisplay && !elem->title.empty()) {
        // {#eq:id}: numbered as \tag and named as \label (#314) unless the
        // TeX numbers itself
        if (elem->text.find("\\tag") == std::string::npos) {
            elem->text += "\\tag{" + std::to_string(++n.equations) + "}";
        }
        elem->text += "\\label{" + elem->title + "}";
    }
    if (elem->type == ElementType::MathDisplay || elem->type == ElementType::MathInline) {
        equationLabels(elem->text, n.ids);
    }
    for (const auto& child : elem->children) number(child, n);
}

bool truthy(const std::string& value) {
    const std::string v = lower(value);
    return v == "true" || v == "yes" || v == "on" || v == "1";
}

// pandoc's numbersections (and pandoc-crossref's numberSections): the
// document's own sections, its top-level headings, numbered 1, 1.1, 1.1.1
// from the shallowest level they use. A first-level heading alone at the
// top is the title, and {-} or .unnumbered keep a heading out. The number
// leads the heading as text marked section-number, which a heading's slug
// and the word count skip. Ids find their section either way
void numberSections(const ElementPtr& root, bool numbered, std::map<std::string, Target>& ids) {
    std::vector<ElementPtr> headings;
    int ones = 0;
    for (const auto& block : root->children) {
        if (block->type != ElementType::Heading) continue;
        headings.push_back(block);
        ones += block->level == 1;
    }
    if (headings.empty()) return;
    const bool titled = headings.front()->level == 1 && ones == 1;
    int base = 7;
    for (size_t i = titled ? 1 : 0; i < headings.size(); ++i) {
        if (headings[i]->language != "unnumbered") base = std::min(base, headings[i]->level);
    }
    int counters[6] = {};
    for (size_t i = 0; i < headings.size(); ++i) {
        const ElementPtr& h = headings[i];
        std::string number;
        if (!(titled && i == 0) && h->language != "unnumbered" && h->level >= base) {
            const int depth = std::min(h->level - base, 5);
            ++counters[depth];
            for (int d = depth + 1; d < 6; ++d) counters[d] = 0;
            for (int d = 0; d <= depth; ++d) number += (d ? "." : "") + std::to_string(counters[d]);
        }
        if (numbered && !number.empty()) {
            auto mark = text(number, h.get());
            mark->language = "section-number";
            h->children.insert(h->children.begin(), {mark, text(" ", h.get())});
        }
        if (!h->title.empty()) {
            if (numbered && !number.empty()) ids.emplace(h->title, Target{'s', number});
            else ids.emplace(h->title, Target{'s', plain(h), true});
        }
    }
}

// --- Cross-references ----------------------------------------------------------

// "1, 2" or, for three in a row and more, "1-3" with an en dash
std::string joinNumbers(const std::vector<std::string>& numbers) {
    std::string out;
    for (size_t i = 0; i < numbers.size();) {
        size_t j = i;
        auto isInt = [](const std::string& s) {
            return !s.empty() && s.size() < 9 &&
                   std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
        };
        while (j + 1 < numbers.size() && isInt(numbers[j]) && isInt(numbers[j + 1]) &&
               std::stoi(numbers[j + 1]) == std::stoi(numbers[j]) + 1) {
            ++j;
        }
        if (!out.empty()) out += ", ";
        if (j >= i + 2) {
            out += numbers[i] + "\xE2\x80\x93" + numbers[j];
            i = j + 1;
        } else {
            out += numbers[i];
            ++i;
        }
    }
    return out;
}

std::string capitalised(std::string word) {
    if (!word.empty()) word[0] = (char)std::toupper((unsigned char)word[0]);
    return word;
}

// @fig:id, [@fig:a; @fig:b], [-@fig:id], @Fig:id: "fig. 1", "figs. 1, 2",
// "1", "Fig. 1". An id that numbers nothing reads @fig:id?
std::string renderReference(const cite::Citation& citation, const Words& words,
                            const std::map<std::string, Target>& ids, std::string& first,
                            std::vector<std::string>& problems) {
    std::string out;
    size_t i = 0;
    while (i < citation.items.size()) {
        const auto& item = citation.items[i];
        std::string id = item.id;
        const bool capital = !id.empty() && std::isupper((unsigned char)id[0]);
        if (size_t colon = id.find(':'); colon != std::string::npos) id = lower(id.substr(0, colon)) + id.substr(colon);
        auto found = ids.find(id);
        if (found == ids.end()) {
            problems.push_back("@" + item.id + " names no figure, table, equation or section");
            if (!out.empty()) out += ", ";
            out += "@" + item.id + "?";
            ++i;
            continue;
        }
        if (first.empty()) first = id;
        // The run of references to the same kind shares one prefix
        const char kind = found->second.kind;
        std::vector<std::string> numbers{found->second.number};
        const bool bare = item.suppressAuthor;
        size_t j = i + 1;
        for (; j < citation.items.size(); ++j) {
            std::string next = citation.items[j].id;
            if (size_t colon = next.find(':'); colon != std::string::npos) next = lower(next.substr(0, colon)) + next.substr(colon);
            auto more = ids.find(next);
            if (more == ids.end() || more->second.kind != kind) break;
            numbers.push_back(more->second.number);
        }
        const std::string (&prefix)[2] =
            kind == 'f' ? words.fig : kind == 't' ? words.tbl : kind == 's' ? words.sec : words.eq;
        std::string group = joinNumbers(numbers);
        if (kind == 'e' && bare) group = "(" + group + ")";
        if (found->second.named) {
            // A section of a document that numbers none reads as its title
            if (!out.empty()) out += ", ";
            out += found->second.number;
            ++i;
            continue;
        }
        if (!bare) {
            std::string word = prefix[numbers.size() > 1 ? 1 : 0];
            group = (capital ? capitalised(word) : word) + "\xC2\xA0" + group;
        }
        if (!out.empty()) out += ", ";
        out += group;
        if (!item.suffix.empty()) out += item.suffix;
        i = j;
    }
    return out;
}

// --- Citations -----------------------------------------------------------------

// What a CSL run reads as, in Tinta's inline elements; true when a run
// is a key the library lacks
bool appendRich(const csl::Rich& rich, Element* parent, std::vector<ElementPtr>& out, bool links) {
    bool missing = false;
    for (const auto& run : rich) {
        if (run.text.empty()) continue;
        missing |= (run.flags & csl::Missing) != 0;
        ElementPtr node = text(run.text, nullptr);
        auto wrap = [&](ElementType type) {
            auto outer = std::make_shared<Element>(type);
            node->parent = outer.get();
            outer->children.push_back(node);
            node = outer;
        };
        if (run.flags & csl::Superscript) wrap(ElementType::Superscript);
        if (run.flags & csl::Subscript) wrap(ElementType::Subscript);
        if (run.flags & csl::Italic) wrap(ElementType::Emphasis);
        if (run.flags & csl::Bold) wrap(ElementType::Strong);
        if (run.flags & csl::Underline) wrap(ElementType::Underline);
        if (links && !run.link.empty()) {
            wrap(ElementType::Link);
            node->url = run.link;
        }
        node->parent = parent;
        out.push_back(node);
    }
    return missing;
}

void collect(const ElementPtr& elem, ElementType type, std::vector<ElementPtr>& out) {
    if (elem->type == type) out.push_back(elem);
    for (const auto& child : elem->children) collect(child, type, out);
}

// Citations as a reader meets them: the text in order, and each footnote's
// at its first mark rather than where the notes are listed
void inReadingOrder(const ElementPtr& root, std::vector<ElementPtr>& out) {
    std::map<int, ElementPtr> notes;
    for (const auto& block : root->children) {
        if (block->type != ElementType::Footnotes) continue;
        for (const auto& note : block->children)
            if (note->type == ElementType::FootnoteDefinition) notes[note->level] = note;
    }
    std::set<int> visited;
    std::function<void(const ElementPtr&)> walk = [&](const ElementPtr& e) {
        if (e->type == ElementType::Footnotes) return;
        if (e->type == ElementType::Citation) {
            out.push_back(e);
            return;
        }
        if (e->type == ElementType::FootnoteReference && visited.insert(e->level).second) {
            auto note = notes.find(e->level);
            if (note != notes.end()) {
                for (const auto& child : note->second->children) walk(child);
            }
        }
        for (const auto& child : e->children) walk(child);
    };
    walk(root);
}

bool insideNote(const Element* e) {
    for (; e; e = e->parent)
        if (e->type == ElementType::FootnoteDefinition) return true;
    return false;
}

// The text just before and just after an element among its siblings, if
// those are text
ElementPtr textBefore(const Element* e) {
    if (!e->parent) return nullptr;
    const auto& kids = e->parent->children;
    for (size_t i = 1; i < kids.size(); ++i)
        if (kids[i].get() == e) return kids[i - 1]->type == ElementType::Text ? kids[i - 1] : nullptr;
    return nullptr;
}
ElementPtr textAfter(const Element* e) {
    if (!e->parent) return nullptr;
    const auto& kids = e->parent->children;
    for (size_t i = 0; i + 1 < kids.size(); ++i)
        if (kids[i].get() == e) return kids[i + 1]->type == ElementType::Text ? kids[i + 1] : nullptr;
    return nullptr;
}

// A note's mark sits against the word before it and after the punctuation
// that follows (pandoc's notes-after-punctuation): "first [@key]." reads
// "first.1"
void placeMark(const ElementPtr& mark) {
    ElementPtr before = textBefore(mark.get());
    if (before) {
        while (!before->text.empty() && before->text.back() == ' ') before->text.pop_back();
    }
    ElementPtr after = textAfter(mark.get());
    size_t take = 0;
    while (after && take < after->text.size() && std::strchr(".,;:!?", after->text[take])) ++take;
    if (!take || !before) return;
    before->text += after->text.substr(0, take);
    after->text.erase(0, take);
}

// A citation that ends with a period does not take the sentence's own:
// "(2020.)." reads "(2020.)" as pandoc reads it
void collapsePeriod(const ElementPtr& citation) {
    std::string tail;
    std::function<void(const ElementPtr&)> last = [&](const ElementPtr& e) {
        if (e->type == ElementType::Text && !e->text.empty()) tail = e->text;
        for (const auto& c : e->children) last(c);
    };
    last(citation);
    if (tail.empty() || tail.back() != '.') return;
    ElementPtr after = textAfter(citation.get());
    if (after && !after->text.empty() && after->text[0] == '.') after->text.erase(0, 1);
}

// The paragraph under a note that leads back to its marks, as footnotes.cpp
// writes it
ElementPtr backlinks(Element* note, int number, int marks) {
    auto back = std::make_shared<Element>(ElementType::Paragraph);
    back->parent = note;
    back->language = "footnote-backlinks";
    if (marks > 1) back->children.push_back(text("Back to references: ", back.get()));
    for (int r = 1; r <= marks; ++r) {
        if (r > 1) back->children.push_back(text(", ", back.get()));
        auto link = std::make_shared<Element>(ElementType::FootnoteBacklink);
        link->parent = back.get();
        link->url = "#tinta-fnref-" + std::to_string(number) + "-" + std::to_string(r);
        link->children.push_back(text(marks == 1 ? "Back to reference" : std::to_string(r), link.get()));
        back->children.push_back(link);
    }
    return back;
}

// A note style's citations in the text become footnotes: one footnote each,
// numbered with the document's own footnotes in the order their marks come.
// The notes the document wrote keep their text and backlinks, renumbered
void intoNotes(const ElementPtr& root, const std::vector<ElementPtr>& cited, const std::vector<csl::Rich>& texts,
               bool links) {
    std::map<const Element*, size_t> citationAt;
    for (size_t i = 0; i < cited.size() && i < texts.size(); ++i)
        if (!insideNote(cited[i].get())) citationAt[cited[i].get()] = i;
    if (citationAt.empty()) return;
    ElementPtr section;
    std::map<int, ElementPtr> written;  // the document's notes by their old number
    for (const auto& block : root->children) {
        if (block->type != ElementType::Footnotes) continue;
        section = block;
        for (const auto& note : block->children)
            if (note->type == ElementType::FootnoteDefinition) written[note->level] = note;
    }
    // Number every mark in the text in order: a written note keeps one
    // number for all its marks, each citation gets its own
    std::map<int, int> renumbered;   // old note number -> new
    std::map<int, int> marks;        // new note number -> marks so far
    std::vector<std::pair<int, ElementPtr>> notes;  // new number -> definition
    int next = 0;
    std::function<void(const ElementPtr&)> walk = [&](const ElementPtr& e) {
        if (e->type == ElementType::Footnotes) return;
        if (e->type == ElementType::FootnoteReference) {
            auto known = renumbered.find(e->level);
            const int n = known != renumbered.end() ? known->second : (renumbered[e->level] = ++next);
            const int mark = ++marks[n];
            if (known == renumbered.end()) {
                auto note = written.find(e->level);
                if (note != written.end()) notes.push_back({n, note->second});
            }
            e->level = n;
            e->url = "#tinta-fn-" + std::to_string(n);
            e->title = "tinta-fnref-" + std::to_string(n) + "-" + std::to_string(mark);
            e->children = {text(std::to_string(n), e.get())};
            return;
        }
        auto citation = citationAt.find(e.get());
        if (citation != citationAt.end()) {
            const int n = ++next;
            marks[n] = 1;
            auto note = std::make_shared<Element>(ElementType::FootnoteDefinition);
            note->level = n;
            note->url = "tinta-fn-" + std::to_string(n);
            auto paragraph = std::make_shared<Element>(ElementType::Paragraph);
            paragraph->parent = note.get();
            appendRich(texts[citation->second], paragraph.get(), paragraph->children, links);
            note->children.push_back(paragraph);
            notes.push_back({n, note});
            e->type = ElementType::FootnoteReference;
            e->level = n;
            e->url = "#tinta-fn-" + std::to_string(n);
            e->title = "tinta-fnref-" + std::to_string(n) + "-1";
            e->children = {text(std::to_string(n), e.get())};
            placeMark(e);
            return;
        }
        for (const auto& child : e->children) walk(child);
    };
    walk(root);
    // Written notes no mark reaches keep their place after the rest
    for (const auto& note : written)
        if (!renumbered.count(note.first)) notes.push_back({renumbered[note.first] = ++next, note.second});
    if (!section) {
        section = std::make_shared<Element>(ElementType::Footnotes);
        section->parent = root.get();
        root->children.push_back(section);
    }
    std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    section->children.clear();
    for (auto& [n, note] : notes) {
        note->level = n;
        note->url = "tinta-fn-" + std::to_string(n);
        note->parent = section.get();
        // A written note's own backlinks go; fresh ones lead to its marks now
        auto& kids = note->children;
        kids.erase(std::remove_if(kids.begin(), kids.end(),
                                  [](const ElementPtr& c) {
                                      return c->type == ElementType::Paragraph && c->language == "footnote-backlinks";
                                  }),
                   kids.end());
        kids.push_back(backlinks(note.get(), n, std::max(1, marks[n])));
        section->children.push_back(note);
    }
}

// The last heading among the top-level blocks
ElementPtr lastHeading(const ElementPtr& root) {
    for (auto it = root->children.rbegin(); it != root->children.rend(); ++it) {
        if ((*it)->type == ElementType::Heading) return *it;
    }
    return nullptr;
}

bool namesReferences(const std::string& heading) {
    static const char* names[] = {
        "references", "reference", "bibliography", "works cited", "literature", "sources",
        "literatur", "literaturverzeichnis", "bibliographie", "r\xC3\xA9" "f\xC3\xA9rences",
        "bibliograf\xC3\xAD" "a", "bibliografia", "\xD0\xBB\xD0\xB8\xD1\x82\xD0\xB5\xD1\x80\xD0\xB0\xD1\x82\xD1\x83\xD1\x80\xD0\xB0",
        "\xE5\x8F\x82\xE8\x80\x83\xE6\x96\x87\xE7\x8C\xAE", "\xE5\xBC\x95\xE7\x94\xA8\xE6\x96\x87\xE7\x8C\xAE",
        "\xE5\x8F\x83\xE8\x80\x83\xE6\x96\x87\xE7\x8D\xBB", "\xEC\xB0\xB8\xEA\xB3\xA0\xEB\xAC\xB8\xED\x97\x8C",
    };
    std::string h = lower(heading);
    while (!h.empty() && std::isspace((unsigned char)h.back())) h.pop_back();
    size_t start = h.find_first_not_of(" \t");
    h = start == std::string::npos ? std::string() : h.substr(start);
    for (const char* name : names) {
        if (h == name) return true;
    }
    return false;
}

// --- Files -----------------------------------------------------------------------

bool readFile(const std::wstring& path, std::string& out, FILETIME& time) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    time = data.ftLastWriteTime;
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    if (out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
    return true;
}

std::wstring resolve(const std::string& name, const std::wstring& folder) {
    std::wstring path = wide(name);
    for (auto& c : path) {
        if (c == L'/') c = L'\\';
    }
    const bool absolute = path.size() > 2 && (path[1] == L':' || (path[0] == L'\\' && path[1] == L'\\'));
    if (absolute || folder.empty()) return path;
    return folder + L"\\" + path;
}

std::string shown(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return utf8(slash == std::wstring::npos ? path : path.substr(slash + 1));
}

// Libraries and styles stay parsed while their files are unchanged
struct Cached {
    FILETIME time{};
    std::shared_ptr<const bib::Library> library;
    std::shared_ptr<const csl::Style> style;
    std::string error;
};
std::mutex g_cacheLock;
std::map<std::wstring, Cached> g_cache;

std::shared_ptr<const bib::Library> loadLibrary(const std::wstring& path, std::vector<std::string>& problems) {
    std::string bytes;
    FILETIME time{};
    if (!readFile(path, bytes, time)) {
        problems.push_back(shown(path) + ": the bibliography cannot be read");
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_cacheLock);
    Cached& cached = g_cache[L"bib|" + path];
    if (!cached.library || CompareFileTime(&cached.time, &time) != 0) {
        auto library = std::make_shared<bib::Library>();
        if (!bib::readByExtension(bytes, utf8(path), *library)) {
            cached.error = shown(path) + ": not a .bib, .json or .yaml bibliography";
        } else {
            cached.error.clear();
        }
        cached.library = std::move(library);
        cached.time = time;
    }
    if (!cached.error.empty()) problems.push_back(cached.error);
    for (const auto& d : cached.library->diagnostics) {
        std::string where = d.source.empty() ? shown(path) : shown(wide(d.source));
        if (d.line > 0) where += ", line " + std::to_string(d.line);
        problems.push_back(where + ": " + d.message);
    }
    return cached.library;
}

// The bundled styles by the names people use for them
std::string bundledStyleName(std::string name) {
    name = lower(name);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".csl") == 0) name.resize(name.size() - 4);
    if (name == "apa" || name == "apa7") return "apa";
    if (name == "chicago" || name == "chicago-author-date") return "chicago-author-date";
    if (name == "ieee") return "ieee";
    if (name == "nature") return "nature";
    if (name == "gb-t-7714" || name == "gbt7714" || name == "gb7714" || name == "gb/t 7714" ||
        name == "china-national-standard-gb-t-7714-2015-numeric") {
        return "china-national-standard-gb-t-7714-2015-numeric";
    }
    return {};
}

std::shared_ptr<const csl::Style> loadStyle(const std::string& wanted, const std::wstring& folder,
                                            std::vector<std::wstring>& files,
                                            std::vector<std::string>& problems) {
    std::string bundled = bundledStyleName(wanted.empty() ? "chicago-author-date" : wanted);
    std::wstring key;
    std::string xml;
    FILETIME time{};
    if (!bundled.empty() && wanted.find_first_of("/\\") == std::string::npos) {
        key = L"style|" + wide(bundled);
    } else {
        std::wstring path = resolve(wanted, folder);
        if (!readFile(path, xml, time)) {
            problems.push_back(wanted + ": no such style; citations use Chicago author-date");
            bundled = "chicago-author-date";
            key = L"style|" + wide(bundled);
        } else {
            key = L"style|" + path;
            files.push_back(path);
        }
    }
    std::lock_guard<std::mutex> lock(g_cacheLock);
    Cached& cached = g_cache[key];
    if (!cached.style || CompareFileTime(&cached.time, &time) != 0) {
        if (xml.empty()) xml = bundledCsl(bundled);
        std::string error;
        cached.style = xml.empty() ? nullptr : csl::readStyle(xml, error);
        cached.time = time;
        cached.error = cached.style ? std::string() : (wanted.empty() ? bundled : wanted) + ": " +
                                                          (error.empty() ? "the style cannot be read" : error);
    }
    if (!cached.error.empty()) problems.push_back(cached.error);
    return cached.style;
}

// @a, @b or @* in nocite:
std::vector<std::string> nociteKeys(const fm::Property* p) {
    std::vector<std::string> keys;
    if (!p) return keys;
    std::string all = p->text;
    for (const auto& item : p->items) all += " " + item;
    for (size_t at = all.find('@'); at != std::string::npos; at = all.find('@', at + 1)) {
        size_t end = at + 1;
        while (end < all.size() && !std::isspace((unsigned char)all[end]) && all[end] != ',' && all[end] != ';' &&
               all[end] != ']' && all[end] != '\'' && all[end] != '"') {
            ++end;
        }
        if (end > at + 1) keys.push_back(all.substr(at + 1, end - at - 1));
    }
    return keys;
}

}  // namespace

Words wordsFor(const std::vector<fm::Property>& front) {
    Words words;
    const std::string lang = scalar(front, "lang");
    if (lang.compare(0, 2, "zh") == 0) {
        // Figure, table, equation in Chinese, and a full-width colon
        words.figure = words.fig[0] = words.fig[1] = "\xE5\x9B\xBE";
        words.table = words.tbl[0] = words.tbl[1] = "\xE8\xA1\xA8";
        words.eq[0] = words.eq[1] = "\xE5\xBC\x8F";
        words.sec[0] = words.sec[1] = "\xE8\x8A\x82";
        words.delim = "\xEF\xBC\x9A";
    }
    if (std::string v = scalar(front, "figureTitle"); !v.empty()) words.figure = v;
    if (std::string v = scalar(front, "tableTitle"); !v.empty()) words.table = v;
    if (const fm::Property* p = property(front, "titleDelim")) words.delim = p->text;
    prefixInto(front, "figPrefix", words.fig);
    prefixInto(front, "tblPrefix", words.tbl);
    prefixInto(front, "eqnPrefix", words.eq);
    prefixInto(front, "secPrefix", words.sec);
    return words;
}

Session begin(const std::vector<fm::Property>& front, const std::wstring& folder, std::string_view document) {
    Session session;
    // The front matter's own references: first, so they win over a file's
    // entries with the same id, as in pandoc
    auto merged = std::make_shared<bib::Library>();
    const bool own = !document.empty() && bib::readFrontMatterReferences(document, "front matter", *merged);
    for (const auto& d : merged->diagnostics) {
        session.problems.push_back("references" + (d.line > 0 ? ", line " + std::to_string(d.line) : std::string()) +
                                   ": " + d.message);
    }
    merged->diagnostics.clear();
    const fm::Property* bibliography = property(front, "bibliography");
    std::vector<std::string> names;
    if (bibliography) {
        names = bibliography->items;
        if (names.empty() && !bibliography->text.empty()) names.push_back(bibliography->text);
    }
    if (names.empty() && !own) return session;
    session.cites = true;
    for (const auto& name : names) {
        if (folder.empty() && !(name.size() > 2 && (name[1] == ':' || name[0] == '\\'))) {
            session.problems.push_back(name + ": save the note first, so the bibliography has a folder to be found in");
            continue;
        }
        const std::wstring path = resolve(name, folder);
        session.files.push_back(path);
        if (auto library = loadLibrary(path, session.problems)) {
            for (const auto& item : library->items) merged->add(item);
        }
    }
    session.library = merged;
    std::string wanted = scalar(front, "csl");
    if (wanted.empty()) wanted = scalar(front, "citation-style");
    session.style = loadStyle(wanted, folder, session.files, session.problems);
    session.lang = scalar(front, "lang");
    session.sectionTitle = scalar(front, "reference-section-title");
    session.nocite = nociteKeys(property(front, "nocite"));
    const std::string link = lower(scalar(front, "link-citations"));
    session.linkCitations = !(link == "false" || link == "no" || link == "0");
    return session;
}

std::vector<std::wstring> followedFiles(const ElementPtr& root, const std::wstring& folder) {
    std::vector<std::wstring> files;
    const auto& front = frontOf(root);
    if (const fm::Property* bibliography = property(front, "bibliography")) {
        std::vector<std::string> names = bibliography->items;
        if (names.empty() && !bibliography->text.empty()) names.push_back(bibliography->text);
        for (const auto& name : names) files.push_back(resolve(name, folder));
    }
    std::string style = scalar(front, "csl");
    if (style.empty()) style = scalar(front, "citation-style");
    if (!style.empty() && (bundledStyleName(style).empty() || style.find_first_of("/\\") != std::string::npos)) {
        files.push_back(resolve(style, folder));
    }
    return files;
}

qmd::CitationFinder finder(const Session& session) {
    auto library = session.library;
    const bool cites = session.cites;
    return [library, cites](std::string_view text) {
        std::vector<qmd::CitationSpan> spans;
        auto known = [&](std::string_view key) {
            return cite::isCrossReference(key) || (library && library->find(key));
        };
        for (const auto& found : cite::find(text, known)) {
            const bool cross = !found.items.empty() && cite::isCrossReference(found.items[0].id);
            if (!cross && !cites) continue;  // no bibliography: only references to figures and the like
            spans.push_back({found.begin, found.end, cross});
        }
        return spans;
    };
}

std::vector<std::string> finish(const ElementPtr& root, const Session& session) {
    std::vector<std::string> problems = session.problems;
    if (!root) return problems;
    const Words words = wordsFor(frontOf(root));
    Numbering numbering{words};
    number(root, numbering);
    const auto& front = frontOf(root);
    numberSections(root, truthy(scalar(front, "numbersections")) || truthy(scalar(front, "numberSections")),
                   numbering.ids);

    // @fig:id and friends
    std::vector<ElementPtr> references;
    collect(root, ElementType::CrossReference, references);
    for (size_t i = 0; i < references.size(); ++i) {
        const ElementPtr& ref = references[i];
        auto found = cite::find(ref->text, [](std::string_view) { return true; });
        if (found.empty()) continue;
        std::string first;
        const std::string reads = renderReference(found[0], words, numbering.ids, first, problems);
        ref->children = {text(reads, ref.get())};
        ref->url = first.empty() ? std::string() : "#" + first;
        ref->title = "xref-" + std::to_string(i + 1);
        ref->level = reads.find('?') != std::string::npos;  // names something missing
    }

    // Citations, formatted all at once: numbers, disambiguation and ibid
    // depend on the order they come in, which is a reader's: a footnote's
    // citations at its mark
    if (!session.cites || !session.style || !session.library) return problems;
    std::vector<ElementPtr> elements;
    inReadingOrder(root, elements);
    std::vector<cite::Citation> citations;
    std::vector<ElementPtr> cited;
    for (const auto& elem : elements) {
        auto found = cite::find(elem->text, [](std::string_view) { return true; });
        if (found.empty() || found[0].items.empty()) continue;
        citations.push_back(found[0]);
        cited.push_back(elem);
    }
    if (citations.empty() && session.nocite.empty()) return problems;
    csl::Options options;
    options.lang = session.lang;
    options.nocite = session.nocite;
    const csl::LocaleSource locales = [](std::string_view lang) {
        return bundledCsl("locales-" + std::string(lang));
    };
    csl::Result result = csl::process(*session.style, locales, *session.library, citations, options);
    for (const auto& warning : result.warnings) problems.push_back(warning);
    if (csl::info(*session.style).note) {
        // A note style: each citation in the text becomes a footnote of its
        // own, numbered with the document's footnotes; one inside a footnote
        // stays where it is
        intoNotes(root, cited, result.citations, session.linkCitations);
    }
    for (size_t i = 0; i < cited.size() && i < result.citations.size(); ++i) {
        if (cited[i]->type != ElementType::Citation) continue;  // now a note mark
        const ElementPtr& elem = cited[i];
        std::vector<ElementPtr> kids;
        elem->level = appendRich(result.citations[i], elem.get(), kids, false);  // cites a missing key
        elem->children = std::move(kids);
        std::string keys;
        for (const auto& item : citations[i].items) keys += (keys.empty() ? "" : " ") + item.id;
        elem->language = keys;  // data-cites in the HTML export
        elem->url = session.linkCitations ? "#ref-" + citations[i].items[0].id : std::string();
        elem->title = "cite-" + std::to_string(i + 1);
        collapsePeriod(elem);
    }
    if (result.bibliography.entries.empty()) return problems;

    // The references go after the last block, before the footnotes, under
    // reference-section-title unless the last heading already names them
    auto list = std::make_shared<Element>(ElementType::Bibliography);
    list->parent = root.get();
    list->language = result.bibliography.alignField ? "field" : result.bibliography.hangingIndent ? "hanging" : "";
    for (const auto& entry : result.bibliography.entries) {
        auto item = std::make_shared<Element>(ElementType::BibEntry);
        item->parent = list.get();
        item->title = "ref-" + entry.id;
        item->text = csl::plain(entry.field);
        appendRich(entry.text, item.get(), item->children, true);
        list->children.push_back(item);
    }
    auto at = root->children.end();
    if (!root->children.empty() && root->children.back()->type == ElementType::Footnotes) --at;
    const ElementPtr last = lastHeading(root);
    if (!session.sectionTitle.empty() && !(last && namesReferences(plain(last)))) {
        auto heading = std::make_shared<Element>(ElementType::Heading);
        heading->parent = root.get();
        heading->level = last ? last->level : 2;
        heading->children.push_back(text(session.sectionTitle, heading.get()));
        at = root->children.insert(at, heading) + 1;
    }
    root->children.insert(at, list);
    return problems;
}

// --- Completion ------------------------------------------------------------------

namespace {

// Lower case without accents, so typing finds a name as it is written
// with them; the letters that do not come apart are spelled out
std::wstring folded(const std::wstring& s) {
    std::wstring parts = s;
    if (int n = s.empty() ? 0 : FoldStringW(MAP_COMPOSITE, s.c_str(), (int)s.size(), nullptr, 0); n > 0) {
        parts.assign((size_t)n, L'\0');
        FoldStringW(MAP_COMPOSITE, s.c_str(), (int)s.size(), parts.data(), n);
    }
    std::wstring out;
    out.reserve(parts.size());
    for (wchar_t c : parts) {
        if (c >= 0x300 && c <= 0x36F) continue;  // an accent, apart from its letter
        switch (c) {
            case 0xD8: case 0xF8: out += L'o'; break;
            case 0xC6: case 0xE6: out += L"ae"; break;
            case 0x152: case 0x153: out += L"oe"; break;
            case 0xDF: out += L"ss"; break;
            case 0x141: case 0x142: out += L'l'; break;
            case 0x110: case 0x111: out += L'd'; break;
            case 0x131: out += L'i'; break;
            default: out += (wchar_t)towlower(c); break;
        }
    }
    return out;
}

bool startsWith(const std::wstring& s, const std::wstring& start) { return s.compare(0, start.size(), start) == 0; }

// CSL rich text read as words: its markup gone, its entities and runs of
// spaces made plain
std::wstring words(const std::string& rich) {
    std::string out;
    for (size_t i = 0; i < rich.size(); ++i) {
        const char c = rich[i];
        if (c == '<' && rich.find('>', i) != std::string::npos) {
            i = rich.find('>', i);
        } else if (c == '&' && rich.compare(i, 5, "&amp;") == 0) {
            out += '&';
            i += 4;
        } else if (c == '&' && (rich.compare(i, 4, "&lt;") == 0 || rich.compare(i, 4, "&gt;") == 0)) {
            out += rich[i + 1] == 'l' ? '<' : '>';
            i += 3;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += c;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return wide(out);
}

// Cut at a word near `limit` characters, with an ellipsis
std::wstring shortened(std::wstring s, size_t limit) {
    if (s.size() <= limit) return s;
    size_t cut = s.rfind(L' ', limit);
    s.resize(cut == std::wstring::npos || cut < limit / 2 ? limit : cut);
    while (!s.empty() && wcschr(L" ,:;.", s.back())) s.pop_back();
    return s + (wchar_t)0x2026;
}

// A work of the library as the list shows it, and what typing finds it by
struct Work {
    std::wstring key, detail;
    std::wstring keyFolded, title, year;
    std::vector<std::wstring> names;  // folded, with and without their particles
};

// "Doe 2024, Ink Flow in DNA-Printed Nibs": the first author's family name
// (both of two, et al. for more), the year, the title shortened
Work workOf(const bib::Item& item) {
    Work w;
    w.key = wide(item.id);
    w.keyFolded = folded(w.key);
    const std::vector<bib::Name>* names = item.namesOf("author");
    if (!names || names->empty()) names = item.namesOf("editor");
    std::vector<std::wstring> shown;
    bool others = false;
    for (size_t i = 0; names && i < names->size(); ++i) {
        const bib::Name& n = (*names)[i];
        if (n.literal == "others") {
            others = true;
            continue;
        }
        const std::wstring family = wide(!n.literal.empty() ? n.literal : n.family.empty() ? n.given : n.family);
        std::string particles = n.droppingParticle;
        if (!n.nonDroppingParticle.empty()) particles += (particles.empty() ? "" : " ") + n.nonDroppingParticle;
        const std::wstring whole = particles.empty() ? family : wide(particles) + L" " + family;
        shown.push_back(whole);
        w.names.push_back(folded(whole));
        if (whole != family) w.names.push_back(folded(family));
    }
    std::wstring detail;
    if (!shown.empty()) {
        detail = shown[0];
        if (shown.size() > 2 || others) detail += L" et al.";
        else if (shown.size() == 2) detail += L" & " + shown[1];
    }
    if (const bib::Date* date = item.date("issued")) {
        w.year = date->from.year ? std::to_wstring(date->from.year) : wide(date->literal);
    }
    if (!w.year.empty()) detail += (detail.empty() ? L"" : L" ") + w.year;
    if (const std::string* field = item.field("title")) {
        const std::wstring title = words(*field);
        w.title = folded(title);
        if (!title.empty()) detail += (detail.empty() ? L"" : L", ") + shortened(title, 40);
    }
    w.detail = std::move(detail);
    return w;
}

// How well a work answers what was typed: 0 by its key, 1 an author, 2 its
// year, 3 inside its key, 4 a word of its title; -1 not at all
int score(const Work& w, const std::wstring& typed) {
    if (typed.empty() || startsWith(w.keyFolded, typed)) return 0;
    for (const auto& name : w.names) {
        if (startsWith(name, typed)) return 1;
    }
    // A name and a year run together, as keys are made: knuth1984
    const size_t digits = typed.find_first_of(L"0123456789");
    if (digits != std::wstring::npos && digits > 0 && !w.year.empty() &&
        typed.find_first_not_of(L"0123456789", digits) == std::wstring::npos &&
        startsWith(w.year, typed.substr(digits))) {
        for (const auto& name : w.names) {
            if (startsWith(name, typed.substr(0, digits))) return 1;
        }
    }
    if (startsWith(w.year, typed)) return 2;
    if (w.keyFolded.find(typed) != std::wstring::npos) return 3;
    for (size_t at = w.title.find(typed); at != std::wstring::npos; at = w.title.find(typed, at + 1)) {
        if (at == 0 || !iswalnum(w.title[at - 1])) return 4;
    }
    return -1;
}

// The properties at a document's start, as UTF-8 for fm::parse
std::string frontMatter(const std::wstring& doc) {
    if (doc.compare(0, 3, L"---") != 0) return {};
    for (size_t nl = doc.find(L'\n'); nl != std::wstring::npos; nl = doc.find(L'\n', nl + 1)) {
        if (doc.compare(nl + 1, 3, L"---") == 0 || doc.compare(nl + 1, 3, L"...") == 0) {
            const size_t end = doc.find(L'\n', nl + 1);
            return utf8(doc.substr(0, end == std::wstring::npos ? doc.size() : end + 1));
        }
    }
    return {};
}

// The works of the bibliography a document names: kept while its
// properties and folder stay, and read again after two seconds, for a
// library its reference manager saves as the writer types
const std::vector<Work>& libraryWorks(const std::wstring& doc, const std::wstring& folder) {
    static std::string front;
    static std::wstring where;
    static unsigned long long made = 0;
    static std::vector<Work> works;
    std::string now = frontMatter(doc);
    const unsigned long long ms = GetTickCount64();
    if (made && now == front && folder == where && ms - made < 2000) return works;
    front = std::move(now);
    where = folder;
    made = ms;
    works.clear();
    if (front.empty()) return works;
    const Session session = begin(fm::parse(front).properties, folder, front);
    if (!session.library) return works;
    works.reserve(session.library->items.size());
    for (const auto& item : session.library->items) works.push_back(workOf(item));
    return works;
}

// What a line holding an id says of it: an image's words, a diagram's
// caption, a table's caption, a heading or an equation's TeX
std::wstring describe(const std::wstring& doc, size_t at) {
    const size_t ls = doc.rfind(L'\n', at), le = doc.find(L'\n', at);
    std::wstring line = doc.substr(ls == std::wstring::npos ? 0 : ls + 1,
                                    (le == std::wstring::npos ? doc.size() : le) - (ls == std::wstring::npos ? 0 : ls + 1));
    const size_t image = line.find(L"![");
    const size_t shown = image == std::wstring::npos ? image : line.find(L"](", image);
    const size_t caption = line.find(L"caption=\"");
    if (shown != std::wstring::npos) {
        line = line.substr(image + 2, shown - image - 2);
    } else if (caption != std::wstring::npos && line.find(L'"', caption + 9) != std::wstring::npos) {
        line = line.substr(caption + 9, line.find(L'"', caption + 9) - caption - 9);
    } else {
        // The line without its attributes, its \label, its marks and spaces
        for (const wchar_t* open : {L"{#", L"\\label{"}) {
            for (size_t a = line.find(open); a != std::wstring::npos; a = line.find(open, a)) {
                const size_t b = line.find(L'}', a);
                line.erase(a, b == std::wstring::npos ? std::wstring::npos : b - a + 1);
            }
        }
        size_t from = line.find_first_not_of(L"# \t$");
        if (from != std::wstring::npos && line.compare(from, 6, L"Table:") == 0) from += 6;
        else if (from != std::wstring::npos && line[from] == L':') from += 1;
        from = from == std::wstring::npos ? line.size() : line.find_first_not_of(L" \t", from);
        const size_t to = line.find_last_not_of(L" \t$");
        line = from == std::wstring::npos || to == std::wstring::npos || to < from ? std::wstring() : line.substr(from, to - from + 1);
    }
    line.erase(std::remove_if(line.begin(), line.end(), [](wchar_t c) { return c == L'*' || c == L'`'; }), line.end());
    return shortened(line, 48);
}

// The document's own ids of a kind (fig, tbl, eq, sec), in order: {#fig:id}
// in attributes and \label{eq:id} in equations; those starting with what
// was typed first, then those holding it
std::vector<Suggestion> documentIds(const std::wstring& doc, const std::wstring& kind, const std::wstring& typed, bool capital) {
    const std::wstring prefix = kind + L":";
    const std::wstring shown = (capital ? std::wstring(1, (wchar_t)towupper(kind[0])) + kind.substr(1) : kind) + L":";
    std::vector<Suggestion> rows;
    for (int holding = 0; holding < 2; ++holding) {
        for (size_t at = doc.find(prefix); at != std::wstring::npos; at = doc.find(prefix, at + 1)) {
            bool id = at >= 7 && doc.compare(at - 7, 7, L"\\label{") == 0;
            if (!id && at >= 2 && doc[at - 1] == L'#') {
                // In an attribute block: { before it on the line with no } between
                for (size_t i = at - 1; i > 0 && doc[i - 1] != L'\n' && doc[i - 1] != L'}' && !id; --i) id = doc[i - 1] == L'{';
            }
            size_t end = at + prefix.size();
            while (id && end < doc.size() && (iswalnum(doc[end]) || wcschr(L"_-.:", doc[end]))) ++end;
            while (end > at + prefix.size() && wcschr(L".:-", doc[end - 1])) --end;
            if (end == at + prefix.size()) continue;
            const std::wstring name = doc.substr(at + prefix.size(), end - at - prefix.size()), key = folded(name);
            if (holding ? startsWith(key, typed) || key.find(typed) == std::wstring::npos : !startsWith(key, typed)) continue;
            const std::wstring label = shown + name;
            if (std::any_of(rows.begin(), rows.end(), [&](const Suggestion& row) { return row.key == label; })) continue;
            rows.push_back({label, describe(doc, at)});
        }
    }
    return rows;
}

}  // namespace

std::vector<Suggestion> suggest(const std::wstring& doc, size_t from, size_t caret, const std::wstring& folder) {
    if (from > caret || caret > doc.size()) return {};
    const std::wstring typed = doc.substr(from, caret - from);
    // @fig:, @tbl:, @eq: and @sec: refer to the document's own figures and the like
    const size_t colon = typed.find(L':');
    if (colon != std::wstring::npos) {
        const std::wstring kind = folded(typed.substr(0, colon));
        if (kind == L"fig" || kind == L"tbl" || kind == L"eq" || kind == L"sec") {
            return documentIds(doc, kind, folded(typed.substr(colon + 1)), iswupper(typed[0]) != 0);
        }
    }
    // Otherwise the library's works, the best first, then by their keys
    const std::wstring wanted = folded(typed);
    std::vector<std::pair<int, const Work*>> ranked;
    for (const Work& w : libraryWorks(doc, folder)) {
        const int s = score(w, wanted);
        if (s >= 0) ranked.push_back({s, &w});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second->keyFolded != b.second->keyFolded ? a.second->keyFolded < b.second->keyFolded
                                                          : a.second->key < b.second->key;
    });
    std::vector<Suggestion> rows;
    for (size_t i = 0; i < ranked.size() && rows.size() < 200; ++i) rows.push_back({ranked[i].second->key, ranked[i].second->detail});
    return rows;
}

}
