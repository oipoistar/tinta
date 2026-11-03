// Scholar (scholar.h): figures, tables and equations numbered as
// pandoc-crossref numbers them, and citations formatted by the CSL
// processor from the document's bibliography, as pandoc's citeproc does.

#pragma optimize("s", on)

#include "scholar.h"
#include "bib.h"
#include "cite.h"
#include "csl.h"
#include "csl_bundle.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
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

// What each id numbers: 'f' figure, 't' table, 'e' equation, and its number
struct Target {
    char kind = 0;
    std::string number;
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
            problems.push_back("@" + item.id + " names no figure, table or equation");
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
        const std::string (&prefix)[2] = kind == 'f' ? words.fig : kind == 't' ? words.tbl : words.eq;
        std::string group = joinNumbers(numbers);
        if (kind == 'e' && bare) group = "(" + group + ")";
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
            cached.error = shown(path) + ": not a .bib or .json bibliography";
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
        words.delim = "\xEF\xBC\x9A";
    }
    if (std::string v = scalar(front, "figureTitle"); !v.empty()) words.figure = v;
    if (std::string v = scalar(front, "tableTitle"); !v.empty()) words.table = v;
    if (const fm::Property* p = property(front, "titleDelim")) words.delim = p->text;
    prefixInto(front, "figPrefix", words.fig);
    prefixInto(front, "tblPrefix", words.tbl);
    prefixInto(front, "eqnPrefix", words.eq);
    return words;
}

Session begin(const std::vector<fm::Property>& front, const std::wstring& folder) {
    Session session;
    const fm::Property* bibliography = property(front, "bibliography");
    if (!bibliography) return session;
    std::vector<std::string> names = bibliography->items;
    if (names.empty() && !bibliography->text.empty()) names.push_back(bibliography->text);
    if (names.empty()) return session;
    session.cites = true;
    auto merged = std::make_shared<bib::Library>();
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
    // depend on the order they come in
    if (!session.cites || !session.style || !session.library) return problems;
    std::vector<ElementPtr> elements;
    collect(root, ElementType::Citation, elements);
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
    for (size_t i = 0; i < cited.size() && i < result.citations.size(); ++i) {
        const ElementPtr& elem = cited[i];
        std::vector<ElementPtr> kids;
        elem->level = appendRich(result.citations[i], elem.get(), kids, false);  // cites a missing key
        elem->children = std::move(kids);
        std::string keys;
        for (const auto& item : citations[i].items) keys += (keys.empty() ? "" : " ") + item.id;
        elem->language = keys;  // data-cites in the HTML export
        elem->url = session.linkCitations ? "#ref-" + citations[i].items[0].id : std::string();
        elem->title = "cite-" + std::to_string(i + 1);
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

}
