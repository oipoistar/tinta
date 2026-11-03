// Scholar (scholar.h): figures, tables and equations numbered and captioned
// as pandoc-crossref numbers them.

#pragma optimize("s", on)

#include "scholar.h"

#include <cstring>
#include <map>

namespace scholar {

using qmd::Element;
using qmd::ElementPtr;
using qmd::ElementType;

namespace {

const fm::Property* property(const ElementPtr& root, const char* key) {
    if (!root || root->children.empty() || root->children[0]->type != ElementType::Properties) return nullptr;
    for (const auto& p : root->children[0]->properties) {
        if (p.key == key) return &p;
    }
    return nullptr;
}

std::string scalar(const ElementPtr& root, const char* key) {
    const fm::Property* p = property(root, key);
    return p && p->kind != fm::Kind::List && p->kind != fm::Kind::Structured ? p->text : std::string();
}

// A prefix key: one word, or a list of the singular and the plural
void prefixInto(const ElementPtr& root, const char* key, std::string (&into)[2]) {
    const fm::Property* p = property(root, key);
    if (!p) return;
    if (p->kind == fm::Kind::List && !p->items.empty()) {
        into[0] = p->items[0];
        into[1] = p->items.size() > 1 ? p->items[1] : p->items[0];
    } else if (!p->text.empty()) {
        into[0] = into[1] = p->text;
    }
}

ElementPtr text(const std::string& value, Element* parent) {
    auto t = std::make_shared<Element>(ElementType::Text);
    t->text = value;
    t->parent = parent;
    return t;
}

// "Figure 1:" in bold before the caption's own words
void label(const ElementPtr& caption, const std::string& words) {
    auto strong = std::make_shared<Element>(ElementType::Strong);
    strong->parent = caption.get();
    strong->children.push_back(text(words, strong.get()));
    std::vector<ElementPtr> kids;
    kids.push_back(strong);
    if (!caption->children.empty()) kids.push_back(text(" ", caption.get()));
    for (auto& child : caption->children) kids.push_back(child);
    caption->children = std::move(kids);
}

struct Numbering {
    const Words& words;
    int figures = 0, tables = 0, equations = 0;
};

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
        }
    } else if (elem->type == ElementType::MathDisplay && !elem->title.empty()) {
        // {#eq:id}: numbered as \tag and named as \label (#314) unless the
        // TeX numbers itself
        if (elem->text.find("\\tag") == std::string::npos) {
            elem->text += "\\tag{" + std::to_string(++n.equations) + "}";
        }
        elem->text += "\\label{" + elem->title + "}";
    }
    for (const auto& child : elem->children) number(child, n);
}

}  // namespace

Words wordsFor(const ElementPtr& root) {
    Words words;
    const std::string lang = scalar(root, "lang");
    if (lang.compare(0, 2, "zh") == 0) {
        // Figure, table, equation in Chinese, and a full-width colon
        words.figure = words.fig[0] = words.fig[1] = "\xE5\x9B\xBE";
        words.table = words.tbl[0] = words.tbl[1] = "\xE8\xA1\xA8";
        words.eq[0] = words.eq[1] = "\xE5\xBC\x8F";
        words.delim = "\xEF\xBC\x9A";
    }
    if (std::string v = scalar(root, "figureTitle"); !v.empty()) words.figure = v;
    if (std::string v = scalar(root, "tableTitle"); !v.empty()) words.table = v;
    if (const fm::Property* p = property(root, "titleDelim")) words.delim = p->text;
    prefixInto(root, "figPrefix", words.fig);
    prefixInto(root, "tblPrefix", words.tbl);
    prefixInto(root, "eqnPrefix", words.eq);
    return words;
}

std::vector<std::string> process(const ElementPtr& root, const std::wstring& folder) {
    (void)folder;
    std::vector<std::string> problems;
    if (!root) return problems;
    const Words words = wordsFor(root);
    Numbering n{words};
    number(root, n);
    return problems;
}

}
