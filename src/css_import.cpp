// Typora and Obsidian themes as Tinta sheets (css.h, docs/css-themes.md):
// their selectors rewritten onto the elements of Tinta's page, the variables
// the apps theme with set on those elements, and what styles the apps'
// windows dropped, each with a note.

#pragma optimize("s", on)

#include "css_internal.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace css {
namespace {

// --- What the apps' names become --------------------------------------------------

// A class, id, tag or attribute value of an app's page, and what it becomes:
// "" nothing, it is left out; "@" the page itself, Tinta's body; "~" a
// wrapper Tinta's page has not, left out; "?l", "?d" or "?p" the rule holds
// in light, dark or print only; "!" and a letter (see `why`) the rule is
// dropped; else the compound it becomes. A name ending in * stands for every
// name it starts
struct Map {
    const char* name;
    const char* to;
};

const char* why(char reason) {
    switch (reason) {
        case 'w': return "styles the app's window, which Tinta draws itself";
        case 'e': return "styles the app's editor";
        case 's': return "styles Typora's source mode";
        case 't': return "styles what Typora shows while editing";
        case 'c': return "styles a callout Tinta has not: its callouts are note, tip, important, warning and caution";
        case 'b': return "styles scrollbars, which are the window's";
        default: return "styles something Tinta's page has not";
    }
}

const Map typoraTags[] = {
    {"content", "@"}, {"footer", "!w"}, {"header", "!w"}, {"nav", "!w"}, {"button", "!w"}, {"select", "!w"},
    {"textarea", "!w"}, {"dialog", "!w"}, {"iframe", "!n"},
};

const Map typoraIds[] = {{"write", "@"}, {"typora-source", "!s"}};  // any other id is the window's

const Map typoraClasses[] = {
    {"typora-export", "?p"}, {"typora-export-content", "@"}, {"typora-sourceview-on", "!s"}, {"typora-*", "!w"},
    {"md-fences", "pre"}, {"mock-cm", ""}, {"md-fences-with-lineno", ""}, {"md-diagram*", "div.diagram"},
    {"cm-s-inner", "pre"}, {"CodeMirror", "pre"}, {"CodeMirror-lines", "code"}, {"CodeMirror-code", "code"},
    {"CodeMirror-line", "code"}, {"CodeMirror-scroll", "code"}, {"CodeMirror-sizer", "code"}, {"CodeMirror*", "!e"},
    {"cm-s-typora-default", "!s"}, {"cm-keyword", ".token.keyword"}, {"cm-string", ".token.string"},
    {"cm-string-2", ".token.string"}, {"cm-comment", ".token.comment"}, {"cm-number", ".token.number"},
    {"cm-def", ".token.function"}, {"cm-variable", ".token.variable"}, {"cm-variable-2", ".token.variable"},
    {"cm-variable-3", ".token.class-name"}, {"cm-type", ".token.class-name"}, {"cm-qualifier", ".token.class-name"},
    {"cm-property", ".token.property"}, {"cm-operator", ".token.operator"}, {"cm-atom", ".token.boolean"},
    {"cm-builtin", ".token.builtin"}, {"cm-tag", ".token.tag"}, {"cm-attribute", ".token.attr-name"},
    {"cm-bracket", ".token.punctuation"}, {"cm-meta", ".token.important"}, {"cm-link", ".token.url"}, {"cm-*", "!e"},
    {"md-task-list-item", "li.task"}, {"task-list-item", "li.task"}, {"task-list", ""}, {"contains-task-list", ""},
    {"task-list-done", ":checked"}, {"task-list-not-done", ":not(:checked)"},
    {"md-alert", "blockquote[class*=\"alert-\"]"}, {"md-alert-note", "blockquote.alert-note"},
    {"md-alert-tip", "blockquote.alert-tip"}, {"md-alert-important", "blockquote.alert-important"},
    {"md-alert-warning", "blockquote.alert-warning"}, {"md-alert-caution", "blockquote.alert-caution"},
    {"md-alert-text-note", "blockquote.alert-note > p.alert-title"},
    {"md-alert-text-tip", "blockquote.alert-tip > p.alert-title"},
    {"md-alert-text-important", "blockquote.alert-important > p.alert-title"},
    {"md-alert-text-warning", "blockquote.alert-warning > p.alert-title"},
    {"md-alert-text-caution", "blockquote.alert-caution > p.alert-title"},
    {"md-alert-text*", "p.alert-title"}, {"md-meta-block", "div.frontmatter"}, {"md-math-block", "div.math-display"},
    {"mathjax-block", "div.math-display"}, {"md-inline-math", "span.math-inline"}, {"md-image", "img"},
    {"md-footnote", "sup"}, {"footnotes-area", "section.footnotes"}, {"md-def-footnote", "div.footnote"},
    {"md-heading", ":is(h1, h2, h3, h4, h5, h6)"}, {"md-p", "p"}, {"md-hr", "hr"}, {"md-link", "a"}, {"md-url", "a"},
    {"md-blockquote", "blockquote"}, {"ul-list", "ul"}, {"ol-list", "ol"}, {"md-list-item", "li"},
    {"table-figure", "table"}, {"md-toc*", "!n"}, {"md-tag", "!n"}, {"md-emoji", "!n"}, {"md-*", "!t"},
    {"sidebar*", "!w"}, {"outline*", "!w"}, {"file-*", "!w"}, {"megamenu*", "!w"}, {"menu*", "!w"},
    {"dropdown*", "!w"}, {"context-menu*", "!w"}, {"modal*", "!w"}, {"btn*", "!w"}, {"footer*", "!w"}, {"ty-*", "!w"},
    {"searchpanel*", "!w"}, {"info-panel*", "!w"}, {"pin-outline", "!w"}, {"auto-suggest*", "!w"}, {"mac-*", "!w"},
    {"os-*", "!w"}, {"unibody-window", "!w"}, {"native-window", "!w"}, {"html-for-mac", "!w"},
    {"on-focus-mode", "!w"}, {"popover*", "!w"}, {"tooltip*", "!w"}, {"window-*", "!w"}, {"toc-*", "!w"},
    {"active-tab-*", "!w"}, {"dialog*", "!w"},
};

const Map typoraTypes[] = {  // [mdtype=...]
    {"heading", ":is(h1, h2, h3, h4, h5, h6)"}, {"paragraph", "p"}, {"table", "table"}, {"list_item", "li"},
    {"blockquote", "blockquote"}, {"fences", "pre"}, {"hr", "hr"}, {"math_block", "div.math-display"},
    {"meta_block", "div.frontmatter"}, {"def_footnote", "div.footnote"}, {"toc", "!n"}, {"*", "!t"},
};

const Map typoraInlines[] = {  // [md-inline=...]
    {"strong", "strong"}, {"em", "em"}, {"code", "code"}, {"underline", "ins"}, {"del", "del"},
    {"highlight", "mark"}, {"superscript", "sup"}, {"subscript", "sub"}, {"inline_math", "span.math-inline"},
    {"footnote", "sup"}, {"url", "a"}, {"autolink", "a"}, {"link", "a"}, {"reflink", "a"}, {"image", "img"},
    {"refimg", "img"}, {"*", "!t"},
};

const Map obsidianClasses[] = {
    {"theme-light", "?l"}, {"theme-dark", "?d"}, {"markdown-preview-view", "@"}, {"markdown-rendered", "@"},
    {"markdown-reading-view", "@"}, {"markdown-preview-section", "@"}, {"markdown-preview-sizer", "@"},
    {"view-content", "@"}, {"is-readable-line-width", ""}, {"allow-fold-headings", ""}, {"allow-fold-lists", ""},
    {"show-indentation-guide", ""}, {"show-properties", ""}, {"node-insert-event", ""}, {"contains-task-list", ""},
    {"markdown-source-view", "!e"}, {"cm-*", "!e"}, {"HyperMD-*", "!e"}, {"mod-cm6", "!e"}, {"is-live-preview", "!e"},
    {"callout", "blockquote[class*=\"alert-\"]"}, {"callout-title", "p.alert-title"},
    {"callout-title-inner", "p.alert-title"}, {"callout-content", "~"}, {"callout-icon", "!n"}, {"callout-fold", "!n"},
    {"task-list-item", "li.task"}, {"task-list-item-checkbox", "input[type=\"checkbox\"]"}, {"is-checked", ":checked"},
    {"internal-link", "span.wikilink"}, {"external-link", "a"}, {"tag", "!n"}, {"list-bullet", "!n"},
    {"list-collapse-indicator", "!n"}, {"collapse-indicator", "!n"}, {"heading-collapse-indicator", "!n"},
    {"copy-code-button", "!n"}, {"edit-block-button", "!n"}, {"inline-title", "!n"}, {"markdown-embed*", "!n"},
    {"internal-embed", "!n"}, {"file-embed*", "!n"}, {"image-embed", "img"}, {"frontmatter", "div.frontmatter"},
    {"frontmatter-container", "div.frontmatter"}, {"metadata-container", "div.frontmatter"},
    {"metadata-property", ".property"}, {"metadata-*", "!n"}, {"footnotes", "section.footnotes"},
    {"footnote-ref", "sup"}, {"footnote-backref", "!n"}, {"math-block", "div.math-display"},
    {"math-inline", "span.math-inline"}, {"math", ""}, {"mermaid", "div.diagram"},
    {"workspace*", "!w"}, {"mod-*", "!w"}, {"side-dock*", "!w"}, {"nav-*", "!w"}, {"tree-item*", "!w"},
    {"titlebar*", "!w"}, {"status-bar*", "!w"}, {"view-header*", "!w"}, {"view-action*", "!w"}, {"modal*", "!w"},
    {"menu*", "!w"}, {"prompt*", "!w"}, {"suggestion*", "!w"}, {"tooltip*", "!w"}, {"notice*", "!w"},
    {"vertical-tab-*", "!w"}, {"setting-*", "!w"}, {"graph-*", "!w"}, {"search-*", "!w"}, {"document-search*", "!w"},
    {"ribbon*", "!w"}, {"clickable-icon", "!w"}, {"app-container", "!w"}, {"horizontal-main-container", "!w"},
    {"is-*", "!w"}, {"mobile-*", "!w"}, {"community-*", "!w"}, {"sidebar*", "!w"}, {"canvas*", "!w"},
    {"bookmark*", "!w"}, {"backlink*", "!w"}, {"outline*", "!w"}, {"tab-*", "!w"}, {"popover*", "!w"},
    {"hover-popover", "!w"}, {"dropdown*", "!w"}, {"checkbox-container", "!w"}, {"slider*", "!w"}, {"pdf-*", "!w"},
    {"empty-state*", "!w"},
};

template <size_t N>
const char* lookup(const Map (&table)[N], std::string_view name) {
    for (const Map& m : table) {
        if (name == m.name) return m.to;
    }
    for (const Map& m : table) {
        std::string_view prefix = m.name;
        if (!prefix.empty() && prefix.back() == '*' && name.substr(0, prefix.size() - 1) == prefix.substr(0, prefix.size() - 1)) {
            return m.to;
        }
    }
    return nullptr;
}

// The variables an app themes with, and the declaration each becomes on
// Tinta's elements. "a|b": the first the theme sets. "@callout": a callout's
// bar and title, from an RGB triplet or a colour
struct Variable {
    const char* names;
    const char* selector;
    const char* property;
};

const Variable typoraVariables[] = {
    {"--bg-color", "body", "background-color"},
    {"--text-color", "body", "color"},
    {"--monospace", "code, pre", "font-family"},
    {"--select-text-bg-color", "::selection", "background-color"},
    {"--select-text-font-color", "::selection", "color"},
    {"--primary-color", ":root", "accent-color"},
    {"--side-bar-bg-color", ":root", "--ui-panel"},
};

const Variable obsidianVariables[] = {
    {"--background-primary", "body", "background-color"},
    {"--text-normal", "body", "color"},
    {"--font-text-theme|--font-text", "body", "font-family"},
    {"--font-text-size", "body", "font-size"},
    {"--line-height-normal", "body", "line-height"},
    {"--interactive-accent|--color-accent", ":root", "accent-color"},
    {"--caret-color", ":root", "caret-color"},
    {"--background-secondary", ":root", "--ui-panel"},
    {"--background-modifier-border", ":root", "--ui-border"},
    {"--link-color|--text-accent", "a", "color"},
    {"--link-decoration", "a", "text-decoration-line"},
    {"--bold-color", "strong", "color"},
    {"--bold-weight", "strong", "font-weight"},
    {"--italic-color", "em", "color"},
    {"--code-background", "pre, :not(pre) > code", "background-color"},
    {"--code-normal", "pre, code", "color"},
    {"--code-size", "pre, :not(pre) > code", "font-size"},
    {"--font-monospace-theme|--font-monospace", "code, pre", "font-family"},
    {"--code-comment", ".token.comment", "color"},
    {"--code-function", ".token.function", "color"},
    {"--code-keyword", ".token.keyword", "color"},
    {"--code-operator", ".token.operator", "color"},
    {"--code-property", ".token.property", "color"},
    {"--code-punctuation", ".token.punctuation", "color"},
    {"--code-string", ".token.string", "color"},
    {"--code-tag", ".token.tag", "color"},
    {"--code-value", ".token.number, .token.boolean", "color"},
    {"--code-important", ".token.important, .token.regex", "color"},
    {"--blockquote-border-color", "blockquote", "border-left-color"},
    {"--blockquote-border-thickness", "blockquote", "border-left-width"},
    {"--blockquote-background-color", "blockquote", "background-color"},
    {"--blockquote-color", "blockquote", "color"},
    {"--blockquote-font-style", "blockquote", "font-style"},
    {"--text-highlight-bg", "mark", "background-color"},
    {"--text-selection", "::selection", "background-color"},
    {"--hr-color", "hr", "border-top-color"},
    {"--hr-thickness", "hr", "border-top-width"},
    {"--table-border-color", "table, th, td", "border-color"},
    {"--table-header-background", "th", "background-color"},
    {"--table-header-color", "th", "color"},
    {"--table-text-color", "td", "color"},
    {"--list-marker-color", "li::marker", "color"},
    {"--checkbox-color", "input[type=\"checkbox\"]", "accent-color"},
    {"--checklist-done-color", "li.task:checked", "color"},
    {"--checklist-done-decoration", "li.task:checked", "text-decoration-line"},
    {"--callout-default", "blockquote.alert-note", "@callout"},
    {"--callout-tip", "blockquote.alert-tip", "@callout"},
    {"--callout-important", "blockquote.alert-important", "@callout"},
    {"--callout-warning", "blockquote.alert-warning, blockquote.alert-caution", "@callout"},
};

// --h1-color to --h6-variant, each on its heading
const char* const headingParts[][2] = {{"color", "color"},    {"size", "font-size"},          {"weight", "font-weight"},
                                       {"font", "font-family"}, {"style", "font-style"}, {"line-height", "line-height"},
                                       {"variant", "font-variant"}};

// Obsidian's sizes and weights, which themes read without setting
const Map obsidianDefaults[] = {
    {"--font-smallest", "0.8em"}, {"--font-smaller", "0.875em"}, {"--font-small", "0.933em"}, {"--font-thin", "100"},
    {"--font-extralight", "200"}, {"--font-light", "300"}, {"--font-normal", "400"}, {"--font-medium", "500"},
    {"--font-semibold", "600"}, {"--font-bold", "700"}, {"--font-extrabold", "800"}, {"--font-black", "900"},
    {"--line-height-normal", "1.5"}, {"--line-height-tight", "1.3"},
};

// --- Text --------------------------------------------------------------------------

bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

// Pieces of text appended in one go: smaller code than a chain of +
void put(std::string& out, std::initializer_list<std::string_view> parts) {
    for (std::string_view p : parts) out.append(p);
}
std::string cat(std::initializer_list<std::string_view> parts) {
    std::string out;
    put(out, parts);
    return out;
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && isBlank(s.front())) s.remove_prefix(1);
    while (!s.empty() && isBlank(s.back())) s.remove_suffix(1);
    return s;
}

// Text on one line, each run of white space one space
std::string collapsed(std::string_view s) {
    std::string out;
    for (char c : trimmed(s)) {
        if (!isBlank(c)) out += c;
        else if (out.back() != ' ') out += ' ';
    }
    return out;
}

// Source text for a note: on one line, and not too long
std::string oneLine(std::string_view s) {
    std::string out = collapsed(s);
    return out.size() > 80 ? out.substr(0, 77) + "..." : out;
}

bool remote(std::string_view url) {
    return startsWithLower(url, "http:") || startsWithLower(url, "https:") || url.substr(0, 2) == "//";
}

// "236, 117, 0": Obsidian's old callout colours, which rgb() takes
bool triplet(std::string_view value) {
    int numbers = 0, commas = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c >= '0' && c <= '9') {
            ++numbers;
            while (i + 1 < value.size() && ((value[i + 1] >= '0' && value[i + 1] <= '9') || value[i + 1] == '.')) ++i;
        } else if (c == ',') {
            ++commas;
        } else if (!isBlank(c)) {
            return false;
        }
    }
    return numbers == 3 && commas == 2;
}

// A name for a selector, its characters CSS would misread escaped; a digit
// first as a hex escape
std::string escapedName(const std::string& name) {
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        unsigned char c = (unsigned char)name[i];
        bool digit = c >= '0' && c <= '9';
        if (digit && i == 0) {
            out += "\\3";
            out += (char)c;
            out += ' ';
            continue;
        }
        if (!(c >= 0x80 || c == '-' || c == '_' || digit || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) out += '\\';
        out += (char)c;
    }
    return out;
}

// --- Selectors --------------------------------------------------------------------

struct Attr {
    std::string name, op, value, text;  // name lowercase; text as written
};

struct Comp {
    std::string combinator;  // joining the compound before: " ", " > ", " + ", " ~ "; none for the first
    std::string tag, id;
    std::vector<std::string> classes, pseudos;  // pseudo-classes and -elements as written
    std::vector<Attr> attrs;
    std::vector<Comp> above;  // compounds a mapping puts before this one: blockquote.alert-tip > p.alert-title
    std::string join;         // and the combinator after them
    char role = 0;            // '@' the page, '~' a wrapper to leave out
    bool empty() const { return tag.empty() && id.empty() && classes.empty() && pseudos.empty() && attrs.empty(); }
};

// A selector's compounds; false for anything a theme's selector should not hold
bool readSelector(const std::vector<Node>& nodes, std::string_view text, std::vector<Comp>& out) {
    out.assign(1, Comp());
    std::string pending;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& n = nodes[i];
        if (n.is(Tok::Space) || n.delim('>') || n.delim('+') || n.delim('~')) {
            if (pending.empty() || pending == " ") pending = n.is(Tok::Space) ? " " : std::string(" ") + n.token.delim + " ";
            continue;
        }
        if (!pending.empty()) {
            if (out.back().empty()) return false;
            out.emplace_back();
            out.back().combinator = pending;
            pending.clear();
        }
        Comp& c = out.back();
        if (n.is(Tok::Ident) && c.empty()) {
            c.tag = toLower(n.token.text);
        } else if (n.delim('*') && c.empty()) {
            c.tag = "*";
        } else if (n.is(Tok::Hash)) {
            c.id = n.token.text;
        } else if (n.delim('.') && i + 1 < nodes.size() && nodes[i + 1].is(Tok::Ident)) {
            c.classes.push_back(nodes[++i].token.text);
        } else if (n.is(Tok::OpenSquare)) {
            std::vector<const Node*> p = parts(n.children);
            if (p.empty() || !p[0]->is(Tok::Ident)) return false;
            Attr a;
            a.name = toLower(p[0]->token.text);
            size_t k = 1;
            while (k < p.size() && p[k]->is(Tok::Delim)) a.op += p[k++]->token.delim;
            if (k < p.size()) a.value = p[k]->token.text;
            a.text = std::string(text.substr(n.token.begin, n.token.end - n.token.begin));
            c.attrs.push_back(std::move(a));
        } else if (n.is(Tok::Colon)) {
            size_t j = i + 1;
            if (j < nodes.size() && nodes[j].is(Tok::Colon)) ++j;
            if (j >= nodes.size() || !(nodes[j].is(Tok::Ident) || nodes[j].is(Tok::Function))) return false;
            c.pseudos.push_back(std::string(text.substr(n.token.begin, nodes[j].token.end - n.token.begin)));
            i = j;
        } else {
            return false;
        }
    }
    return !out.back().empty();
}

bool readSelector(std::string_view text, std::vector<Comp>& out) {
    std::vector<Node> nodes = parseNodes(text);
    trimSpace(nodes);
    return readSelector(nodes, text, out);
}

std::string writeSelector(const std::vector<Comp>& comps) {
    std::string out;
    for (const Comp& c : comps) {
        out += c.combinator;
        out += c.empty() ? "*" : c.tag;
        if (!c.id.empty()) out += "#" + escapedName(c.id);
        for (const std::string& k : c.classes) out += "." + escapedName(k);
        for (const Attr& a : c.attrs) out += a.text;
        for (const std::string& p : c.pseudos) out += p;
    }
    return out;
}

uint32_t specificityOf(const std::vector<Comp>& comps) {
    uint32_t ids = 0, classes = 0, types = 0;
    for (const Comp& c : comps) {
        ids += !c.id.empty();
        classes += (uint32_t)(c.classes.size() + c.attrs.size());
        for (const std::string& p : c.pseudos) (p.compare(0, 2, "::") == 0 ? types : classes) += 1;
        types += !c.tag.empty() && c.tag != "*";
    }
    return std::min(ids, 1023u) << 20 | std::min(classes, 1023u) << 10 | std::min(types, 1023u);
}

void merge(Comp& c, const Comp& with) {
    if (!with.tag.empty()) c.tag = with.tag;
    for (const std::string& k : with.classes) {
        if (std::find(c.classes.begin(), c.classes.end(), k) == c.classes.end()) c.classes.push_back(k);
    }
    for (const Attr& a : with.attrs) c.attrs.push_back(a);
    for (const std::string& p : with.pseudos) {
        if (std::find(c.pseudos.begin(), c.pseudos.end(), p) == c.pseudos.end()) c.pseudos.push_back(p);
    }
}

// --- The converter ------------------------------------------------------------------

// A rule of the result, or an at-rule's block of them
struct Item {
    char condition = 0;  // 'l' light, 'd' dark, 'p' print only
    std::string text;    // the rule, or the at-rule's prelude for a block
    bool block = false;
    std::vector<Item> items;
};

// A custom property set on the page: where, and for which side
struct Definition {
    char side = 0;  // 'l', 'd', or 0 for both
    int level = 1;  // 0 :root, 1 the body, 2 the reading view inside it
    uint32_t specificity = 0;
    int order = 0;
    std::string value;
};

struct Mapped {
    char drop = 0, condition = 0;
    int level = 1;
};

struct Source {
    std::string_view text;
    std::string file;
    std::vector<Token> tokens;
    Lines lines;
};

struct Decl {
    std::string name, text, value;
};

struct Converter {
    Flavor flavor;
    std::vector<Diagnostic>& notes;
    const ImportSource& imports;
    std::vector<std::string> files;  // the sheets being read, against import cycles
    std::string keptImports;
    // Obsidian's variables set on the page, by name in the order first set
    std::vector<std::string> names;
    std::map<std::string, std::vector<Definition>> vars;
    // Typora's: the names set on the page, and its background
    std::set<std::string> pageVars;
    std::string background;
    std::set<std::string> defined;  // every custom property any rule sets
    std::vector<std::tuple<std::string, std::string, int, int>> reads;  // var() without a fallback: name, file, line, column
    bool light = false, dark = false, darkQuery = false, layered = false;
    int order = 0;

    Converter(Flavor f, std::vector<Diagnostic>& n, const ImportSource& i) : flavor(f), notes(n), imports(i) {}

    void note(const Source& src, size_t offset, std::string message, Diagnostic::Level level = Diagnostic::Note) {
        Diagnostic d;
        d.level = level;
        d.file = src.file;
        src.lines.at(src.text, (uint32_t)offset, d.line, d.column);
        d.message = std::move(message);
        notes.push_back(std::move(d));
    }

    static size_t closing(const std::vector<Token>& tokens, size_t open, size_t end) {
        std::vector<Tok> want{Tok::CloseCurly};
        for (size_t at = open + 1; at < end; ++at) {
            Tok t = tokens[at].type;
            if (t == want.back()) {
                want.pop_back();
                if (want.empty()) return at;
            } else if (t == Tok::Function || t == Tok::OpenParen) {
                want.push_back(Tok::CloseParen);
            } else if (t == Tok::OpenSquare) {
                want.push_back(Tok::CloseSquare);
            } else if (t == Tok::OpenCurly) {
                want.push_back(Tok::CloseCurly);
            }
        }
        return end;
    }

    // The text of tokens [from, to)
    static std::string_view written(const Source& src, size_t from, size_t to) {
        if (from >= to) return {};
        return src.text.substr(src.tokens[from].begin, src.tokens[to - 1].end - src.tokens[from].begin);
    }

    // --- Selectors onto Tinta's page ---

    // What one Map entry says, done to a compound; false drops the rule
    bool apply(Comp& c, const char* to, Mapped& m) {
        switch (to[0]) {
            case '\0': return true;
            case '@':
                c.role = '@';
                m.level = 2;
                return true;
            case '~': c.role = '~'; return true;
            case '?':
                if (m.condition && m.condition != to[1]) {
                    m.drop = 'n';
                    return false;
                }
                m.condition = to[1];
                return true;
            case '!': m.drop = to[1]; return false;
        }
        std::vector<Comp> with;
        if (!readSelector(to, with)) return true;
        merge(c, with.back());
        if (with.size() > 1) {
            c.above.assign(with.begin(), with.end() - 1);
            c.join = with.back().combinator;
        }
        return true;
    }

    // The compounds mappings put above others, in their places
    static void splice(std::vector<Comp>& comps) {
        for (size_t i = 0; i < comps.size(); ++i) {
            if (comps[i].above.empty()) continue;
            std::vector<Comp> above = std::move(comps[i].above);
            comps[i].above.clear();
            above.front().combinator = comps[i].combinator;
            comps[i].combinator = comps[i].join;
            comps.insert(comps.begin() + (ptrdiff_t)i, above.begin(), above.end());
            i += above.size();
        }
    }

    // Whether a :not() holds a class of the app's window, and so always
    // holds on Tinta's page
    bool windowClassIn(const std::string& pseudo) {
        std::vector<Node> nodes = parseNodes(pseudo);
        for (const Node& n : nodes) {
            if (!n.is(Tok::Function)) continue;
            for (size_t i = 0; i + 1 < n.children.size(); ++i) {
                if (!n.children[i].delim('.') || !n.children[i + 1].is(Tok::Ident)) continue;
                const std::string& name = n.children[i + 1].token.text;
                const char* to = flavor == Flavor::Typora ? lookup(typoraClasses, name) : lookup(obsidianClasses, name);
                if (to && to[0] == '!' && to[1] == 'w') return true;
            }
        }
        return false;
    }

    bool mapCompound(Comp& c, Mapped& m) {
        bool typora = flavor == Flavor::Typora;
        if (typora && !c.tag.empty()) {
            if (const char* to = lookup(typoraTags, c.tag)) {
                c.tag.clear();
                if (!apply(c, to, m)) return false;
            }
        }
        if (typora && !c.id.empty()) {
            const char* to = lookup(typoraIds, c.id);
            c.id.clear();
            if (!apply(c, to ? to : "!w", m)) return false;
        }
        std::vector<Attr> attrs = std::move(c.attrs);
        c.attrs.clear();
        for (const Attr& a : attrs) {
            const char* to = nullptr;
            if (typora && a.name == "mdtype") {
                to = lookup(typoraTypes, a.value);
            } else if (typora && a.name == "md-inline") {
                to = lookup(typoraInlines, a.value);
            } else if (typora && a.name == "lang" && c.tag == "pre") {
                std::string lang = toLower(a.value);
                to = a.op.empty() ? "" : lang == "mermaid" || lang == "sequence" || lang == "flow" ? "div.diagram" : "!n";
            } else if (!typora && a.name == "data-callout") {
                // One of GitHub's five, the alerts Tinta draws
                std::string kind = toLower(a.value);
                if (a.op != "=" || !(kind == "note" || kind == "tip" || kind == "important" || kind == "warning" || kind == "caution")) {
                    m.drop = 'c';
                    return false;
                }
                c.classes.erase(std::remove(c.classes.begin(), c.classes.end(), "callout"), c.classes.end());
                c.tag = "blockquote";
                c.classes.push_back("alert-" + kind);
                continue;
            } else if (!typora && a.name.compare(0, 13, "data-callout-") == 0) {
                to = "!n";
            } else if (!typora && a.name == "data-task") {
                to = a.value == "x" || a.value == "X" ? ":checked" : a.value == " " ? ":not(:checked)" : "!n";
            } else if (!typora && a.name == "data-type") {
                continue;
            } else {
                c.attrs.push_back(a);
                continue;
            }
            if (!apply(c, to ? to : "!n", m)) return false;
        }
        std::vector<std::string> classes = std::move(c.classes);
        c.classes.clear();
        for (const std::string& name : classes) {
            const char* to = typora ? lookup(typoraClasses, name) : lookup(obsidianClasses, name);
            if (!to) {
                if (std::find(c.classes.begin(), c.classes.end(), name) == c.classes.end()) c.classes.push_back(name);
            } else if (!apply(c, to, m)) {
                return false;
            }
        }
        for (size_t k = 0; k < c.pseudos.size();) {
            std::string lower = toLower(c.pseudos[k]);
            if (startsWithLower(lower, "::-webkit-scrollbar") || startsWithLower(lower, "::-webkit-resizer")) {
                m.drop = 'b';
                return false;
            }
            if (lower == "::-moz-selection") {
                c.pseudos[k] = "::selection";
            } else if (startsWithLower(lower, "::-webkit-") || startsWithLower(lower, "::-moz-")) {
                m.drop = 'n';
                return false;
            } else if (startsWithLower(lower, ":not(") && windowClassIn(c.pseudos[k])) {
                c.pseudos.erase(c.pseudos.begin() + (ptrdiff_t)k);  // always true on Tinta's page
                continue;
            }
            ++k;
        }
        if (c.empty() && !c.role) c.role = '@';  // only a class for the window's state was there: the page
        return true;
    }

    // The page and wrappers out of a selector; false when nothing is left
    bool simplify(std::vector<Comp>& comps, Mapped& m) {
        Comp& subject = comps.back();
        if (subject.role == '~') {
            m.drop = 'n';
            return false;
        }
        if (subject.role == '@') {
            subject.role = 0;
            subject.tag = "body";
        }
        for (size_t k = comps.size() - 1; k-- > 0;) {
            Comp& c = comps[k];
            if (!c.role) continue;
            if (c.role == '@' && comps[k + 1].combinator == " > ") {  // #write > p: a paragraph on the page itself
                c.role = 0;
                c.tag = "body";
                continue;
            }
            comps[k + 1].combinator = k == 0 ? std::string() : c.combinator;
            comps.erase(comps.begin() + (ptrdiff_t)k);
        }
        for (size_t k = 1; k < comps.size();) {
            Comp& a = comps[k - 1];
            Comp& b = comps[k];
            if (a.tag == b.tag && (a.tag == "body" || a.tag == "pre") && (b.combinator == " " || b.combinator == " > ")) {
                merge(b, a);
                b.combinator = a.combinator;
                comps.erase(comps.begin() + (ptrdiff_t)(k - 1));
                continue;
            }
            ++k;
        }
        for (const Comp& c : comps) {
            if ((c.tag == "body" || c.tag == "html") && !c.classes.empty()) {  // a class the app sets on its window
                m.drop = 'w';
                return false;
            }
        }
        const Comp& last = comps.back();
        if (comps.size() == 1 && (last.tag == "html" || (last.tag.empty() && last.pseudos.size() == 1 && last.pseudos[0] == ":root"))) {
            m.level = 0;
        }
        // Typora's code blocks hold no code element: its code is inline code
        if (flavor == Flavor::Typora && comps.size() == 1 && last.tag == "code") {
            Comp parent;
            parent.pseudos.push_back(":not(pre)");
            comps[0].combinator = " > ";
            comps.insert(comps.begin(), parent);
        }
        return true;
    }

    // --- Declarations ---

    std::vector<Decl> declarations(const Source& src, size_t open, size_t close) {
        std::vector<Decl> out;
        const std::vector<Token>& t = src.tokens;
        for (size_t i = open + 1; i < close;) {
            if (t[i].type == Tok::Space || t[i].type == Tok::Semicolon) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < close && t[j].type != Tok::Semicolon) j = skipNode(t, j, close);
            size_t colon = i + 1;
            while (colon < j && t[colon].type == Tok::Space) ++colon;
            if (t[i].type == Tok::Ident && colon < j && t[colon].type == Tok::Colon) {
                Decl d;
                d.name = t[i].text;
                d.text = std::string(trimmed(written(src, i, j)));
                std::string_view value = trimmed(src.text.substr(t[colon].end, t[j - 1].end - t[colon].end));
                size_t bang = value.rfind('!');
                if (bang != std::string_view::npos && equalsLower(trimmed(value.substr(bang + 1)), "important")) {
                    value = trimmed(value.substr(0, bang));
                }
                d.value = std::string(value);
                out.push_back(std::move(d));
            }
            i = j;
        }
        return out;
    }

    // The var()s without a fallback that tokens [from, to) read
    void readsIn(const Source& src, size_t from, size_t to) {
        const std::vector<Token>& t = src.tokens;
        for (size_t k = from; k < to; ++k) {
            if (t[k].type != Tok::Function || !equalsLower(t[k].text, "var")) continue;
            size_t n = k + 1;
            while (n < to && t[n].type == Tok::Space) ++n;
            if (n >= to || t[n].type != Tok::Ident || t[n].text.compare(0, 2, "--") != 0) continue;
            size_t c = n + 1;
            while (c < to && t[c].type == Tok::Space) ++c;
            if (c < to && t[c].type == Tok::Comma) continue;
            int line, column;
            src.lines.at(src.text, t[n].begin, line, column);
            reads.emplace_back(t[n].text, src.file, line, column);
        }
    }

    void define(const std::string& name, char side, int level, uint32_t specificity, const std::string& value) {
        auto& list = vars[name];
        if (list.empty()) names.push_back(name);
        list.push_back({side, level, specificity, order++, value});
    }

    // --- Rules ---

    void list(Source& src, size_t begin, size_t end, std::vector<Item>& out, bool top, int depth) {
        for (size_t i = begin; i < end;) {
            Tok t = src.tokens[i].type;
            if (t == Tok::Space || t == Tok::CDO || t == Tok::CDC || t == Tok::Semicolon) ++i;
            else if (t == Tok::AtKeyword) i = atRule(src, i, end, out, depth);
            else i = rule(src, i, end, out, top);
        }
    }

    size_t rule(Source& src, size_t start, size_t end, std::vector<Item>& out, bool top) {
        size_t open = start;
        while (open < end && src.tokens[open].type != Tok::OpenCurly) open = skipNode(src.tokens, open, end);
        if (open >= end) {
            note(src, src.tokens[start].begin, cat({"`", oneLine(written(src, start, end)), "` has no `{` block; it is left out"}));
            return end;
        }
        size_t close = closing(src.tokens, open, end);
        size_t after = close < end ? close + 1 : end;
        size_t insideEnd = close < end ? src.tokens[close].begin : src.text.size();
        std::string inside(src.text.substr(src.tokens[open].end, insideEnd - src.tokens[open].end));
        std::string_view prelude = written(src, start, open);
        uint32_t preludeAt = src.tokens[start].begin;

        struct Kept {
            std::string text;
            Mapped mapped;
            uint32_t specificity = 0;
            bool callout = false;
        };
        std::vector<Kept> kept;
        std::vector<Node> nodes = parseNodes(prelude);
        for (const std::vector<Node>& part : splitCommas(nodes)) {
            if (part.empty()) continue;
            uint32_t from = part.front().token.begin;
            std::string_view selector = prelude.substr(from, part.back().token.end - from);
            std::vector<Comp> comps;
            Kept k;
            if (flavor == Flavor::Detect || !readSelector(part, prelude, comps)) {
                k.text = collapsed(selector);
                kept.push_back(std::move(k));
                continue;
            }
            k.specificity = specificityOf(comps);
            bool ok = true;
            for (Comp& c : comps) {
                if (!(ok = mapCompound(c, k.mapped))) break;
            }
            if (ok) {
                splice(comps);
                ok = simplify(comps, k.mapped);
            }
            if (!ok) {
                note(src, preludeAt + from, cat({"`", oneLine(selector), "` is dropped: it ", why(k.mapped.drop)}));
                continue;
            }
            light |= k.mapped.condition == 'l';
            dark |= k.mapped.condition == 'd';
            const Comp& subject = comps.back();
            k.callout = subject.tag == "blockquote" &&
                        (std::any_of(subject.classes.begin(), subject.classes.end(),
                                     [](const std::string& name) { return name.compare(0, 6, "alert-") == 0; }) ||
                         !subject.attrs.empty());
            k.text = writeSelector(comps);
            kept.push_back(std::move(k));
        }
        if (kept.empty()) return after;
        readsIn(src, open + 1, std::min(close, end));
        std::vector<Decl> decls = declarations(src, open, std::min(close, end));
        for (const Decl& d : decls) {
            if (d.name.compare(0, 2, "--") == 0) defined.insert(d.name);
        }

        // On the page itself: :root, html and body
        bool page = top && flavor != Flavor::Detect && std::all_of(kept.begin(), kept.end(), [](const Kept& k) {
            return k.text == ":root" || k.text == "html" || k.text == "body";
        });
        if (page && flavor == Flavor::Typora) {
            for (const Decl& d : decls) {
                if (d.name.compare(0, 2, "--") == 0) pageVars.insert(d.name);
                if ((d.name == "--bg-color" || equalsLower(d.name, "background-color") || equalsLower(d.name, "background")) &&
                    parseColor(d.value)) {
                    background = d.value;
                }
            }
        } else if (page && flavor == Flavor::Obsidian) {
            // Its custom properties join the variables, merged by side
            std::string rest;
            for (const Decl& d : decls) {
                if (d.name.compare(0, 2, "--") != 0) {
                    put(rest, {"\n  ", d.text, ";"});
                    continue;
                }
                for (const Kept& k : kept) define(d.name, k.mapped.condition, k.mapped.level, k.specificity, d.value);
            }
            if (rest.empty()) return after;
            inside = rest + "\n";
        }

        // One rule for each condition its selectors hold under
        for (char condition : {'\0', 'l', 'd', 'p'}) {
            std::vector<std::string_view> selectors, callouts;
            for (const Kept& k : kept) {
                if (k.mapped.condition != condition ||
                    std::find(selectors.begin(), selectors.end(), std::string_view(k.text)) != selectors.end()) {
                    continue;
                }
                selectors.push_back(k.text);
                if (k.callout) callouts.push_back(k.text);
            }
            if (selectors.empty()) continue;
            Item item;
            item.condition = condition;
            std::string list = joined(selectors, "");
            put(item.text, {list, " {", inside, "}\n"});
            // Obsidian's --callout-color draws a callout's bar and title
            for (const Decl& d : decls) {
                if (d.name != "--callout-color" || callouts.empty() || flavor != Flavor::Obsidian) continue;
                std::string_view colour = triplet(d.value) ? "rgb(var(--callout-color))" : "var(--callout-color)";
                put(item.text, {list, " { border-left-color: ", colour, "; }\n", joined(callouts, " > p.alert-title"),
                                " { color: ", colour, "; }\n"});
            }
            out.push_back(std::move(item));
        }
        return after;
    }

    // Selectors as a list, each with `after` added
    static std::string joined(const std::vector<std::string_view>& selectors, std::string_view after) {
        std::string out;
        for (std::string_view s : selectors) put(out, {out.empty() ? "" : ", ", s, after});
        return out;
    }

    // Every url() in tokens [from, to), as written
    static std::vector<std::string> urls(const Source& src, size_t from, size_t to) {
        std::vector<std::string> out;
        for (size_t k = from; k < to; ++k) {
            const Token& t = src.tokens[k];
            if (t.type == Tok::Url) {
                out.push_back(t.text);
            } else if (t.type == Tok::Function && equalsLower(t.text, "url")) {
                size_t n = k + 1;
                while (n < to && src.tokens[n].type == Tok::Space) ++n;
                if (n < to && src.tokens[n].type == Tok::String) out.push_back(src.tokens[n].text);
            }
        }
        return out;
    }

    size_t atRule(Source& src, size_t start, size_t end, std::vector<Item>& out, int depth) {
        const Token& at = src.tokens[start];
        std::string name = toLower(at.text);
        size_t j = start + 1;
        while (j < end && src.tokens[j].type != Tok::Semicolon && src.tokens[j].type != Tok::OpenCurly) j = skipNode(src.tokens, j, end);
        bool hasBlock = j < end && src.tokens[j].type == Tok::OpenCurly;
        size_t close = hasBlock ? closing(src.tokens, j, end) : j;
        size_t after = hasBlock ? (close < end ? close + 1 : end) : std::min(end, j + 1);
        std::string prelude(trimmed(written(src, start + 1, j)));
        if (name == "charset") return after;
        if (name == "import") {
            importRule(src, start, prelude, out, depth);
            return after;
        }
        if (name == "include-when-export") {
            note(src, at.begin, "`@include-when-export` is dropped: it brings web fonts into Typora's exports, and Tinta fetches nothing");
            return after;
        }
        if (name == "font-face" && hasBlock) {
            std::vector<std::string> sources = urls(src, j, close);
            bool web = !sources.empty() && std::all_of(sources.begin(), sources.end(), [](const std::string& u) { return remote(u); });
            if (web) {
                note(src, at.begin, "`@font-face` is dropped: its font is on the web, and Tinta fetches nothing; install the font");
                return after;
            }
            Item item;
            put(item.text, {written(src, start, after), close >= end ? "}\n" : "\n"});
            out.push_back(std::move(item));
            if (!sources.empty()) {
                note(src, at.begin, cat({"`@font-face` reads `", sources[0], "` beside the theme: copy its fonts beside the imported one"}));
            }
            return after;
        }
        if (hasBlock && (name == "media" || name == "supports" || name == "layer")) {
            if (name == "media" && prelude.find("prefers-color-scheme") != std::string::npos &&
                prelude.find("dark") != std::string::npos) {
                darkQuery = true;
            }
            std::vector<Item> inner;
            if (depth < 16) list(src, j + 1, std::min(close, end), inner, false, depth + 1);
            if (name == "layer") {
                if (!layered) note(src, at.begin, "`@layer` is unwrapped: Tinta applies its rules in order");
                layered = true;
                for (Item& item : inner) out.push_back(std::move(item));
                return after;
            }
            Item block;
            block.block = true;
            block.text = cat({"@", at.text, " ", prelude});
            block.items = std::move(inner);
            out.push_back(std::move(block));
            return after;
        }
        if (name == "layer") return after;  // a list of layer names
        note(src, at.begin, cat({"`@", name, "` is dropped: Tinta does not ",
                                 name.find("keyframes") != std::string::npos ? "animate" : "read it"}));
        return after;
    }

    void importRule(Source& src, size_t start, const std::string& prelude, std::vector<Item>& out, int depth) {
        uint32_t where = src.tokens[start].begin;
        std::vector<Node> nodes = parseNodes(prelude);
        trimSpace(nodes);
        std::string url;
        if (!nodes.empty()) {
            const Node& n = nodes[0];
            std::vector<const Node*> inner = parts(n.children);
            if (n.is(Tok::String) || n.is(Tok::Url)) url = n.token.text;
            else if (n.function("url") && inner.size() == 1 && inner[0]->is(Tok::String)) url = inner[0]->token.text;
        }
        std::string media = nodes.size() > 1 ? std::string(trimmed(std::string_view(prelude).substr(nodes[0].token.end))) : std::string();
        if (url.empty()) {
            note(src, where, cat({"`@import ", oneLine(prelude), "` names no sheet; it is dropped"}));
        } else if (remote(url)) {
            note(src, where, cat({"`@import` of `", oneLine(url), "` is dropped: it is on the web, and Tinta fetches nothing"}));
        } else if (!imports) {
            put(keptImports, {"@import ", prelude, ";\n"});
            note(src, where, cat({"`", url, "` is imported as it is, not rewritten"}));
        } else {
            std::string text, file;
            bool read = imports(url, src.file, text, file);
            if (!read || std::find(files.begin(), files.end(), file) != files.end() || depth >= 8) {
                note(src, where, cat({read ? "`" : "cannot read `", url, read ? "` imports a sheet already read" : "` to rewrite it",
                                      "; it is left out"}),
                     Diagnostic::Warning);
            } else {
                std::vector<Item> items = sheet(text, file, media.empty(), depth + 1);
                if (media.empty()) {
                    for (Item& item : items) out.push_back(std::move(item));
                } else {
                    Item block;
                    block.block = true;
                    block.text = "@media " + media;
                    block.items = std::move(items);
                    out.push_back(std::move(block));
                }
            }
        }
    }

    std::vector<Item> sheet(std::string_view text, const std::string& file, bool top, int depth) {
        if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
        Source src{text, file, tokenize(text), Lines(text)};
        files.push_back(file);
        std::vector<Item> items;
        list(src, 0, src.tokens.size(), items, top, depth);
        files.pop_back();
        return items;
    }

    // --- The result ---

    const Definition* effective(const std::string& name, char side) const {
        auto found = vars.find(name);
        if (found == vars.end()) return nullptr;
        const Definition* best = nullptr;
        for (const Definition& d : found->second) {
            if (d.side && d.side != side) continue;
            if (!best || std::tie(d.level, d.specificity, d.order) > std::tie(best->level, best->specificity, best->order)) best = &d;
        }
        return best;
    }

    // The custom properties a value reads with no fallback
    static std::vector<std::string> readsOf(const std::string& value) {
        std::vector<std::string> out;
        std::vector<Token> t = tokenize(value);
        for (size_t k = 0; k + 1 < t.size(); ++k) {
            if (t[k].type != Tok::Function || !equalsLower(t[k].text, "var")) continue;
            size_t n = k + 1;
            while (n < t.size() && t[n].type == Tok::Space) ++n;
            if (n >= t.size() || t[n].text.compare(0, 2, "--") != 0) continue;
            size_t c = n + 1;
            while (c < t.size() && t[c].type == Tok::Space) ++c;
            if (c >= t.size() || t[c].type != Tok::Comma) out.push_back(t[n].text);
        }
        return out;
    }

    // Whether a variable has a value on a side, each var() it reads too
    bool resolves(const std::string& name, char side, int depth = 0) const {
        const Definition* d = effective(name, side);
        if (!d) return lookup(obsidianDefaults, name) != nullptr;
        if (depth > 16) return false;
        for (const std::string& read : readsOf(d->value)) {
            if (!resolves(read, side, depth + 1)) return false;
        }
        return true;
    }

    bool colourLike(const std::string& value, char side, int depth = 0) const {
        std::string_view v = trimmed(value);
        if (parseColor(v)) return true;
        static const char* const functions[] = {"rgb(", "rgba(", "hsl(", "hsla(", "hwb(", "lab(", "lch(",
                                                "oklab(", "oklch(", "color-mix(", "light-dark(", "color("};
        for (const char* f : functions) {
            if (startsWithLower(v, f)) return true;
        }
        if (depth < 16 && startsWithLower(v, "var(")) {
            std::vector<std::string> read = readsOf(std::string(v));
            const Definition* d = read.empty() ? nullptr : effective(read[0], side);
            return d && colourLike(d->value, side, depth + 1);
        }
        return false;
    }

    // Light rules and dark rules, after the rest, or with the rest when the
    // theme has one side only
    // A block of rules under a media query, or none when it is empty
    static void wrap(std::string& out, std::string_view query, std::string_view rules) {
        if (!rules.empty()) put(out, {"@media ", query, " {\n", rules, "}\n"});
    }

    std::string write(const std::vector<Item>& items) const {
        std::string groups[4];
        for (const Item& item : items) {
            char c = item.condition;
            if ((c == 'l' && !dark) || (c == 'd' && !light)) c = 0;
            std::string& group = groups[c == 'l' ? 1 : c == 'd' ? 2 : c == 'p' ? 3 : 0];
            if (!item.block) {
                group += item.text;
                continue;
            }
            std::string inner = write(item.items);
            if (!inner.empty()) put(group, {item.text, " {\n", inner, "}\n"});
        }
        wrap(groups[0], "(prefers-color-scheme: light)", groups[1]);
        wrap(groups[0], "(prefers-color-scheme: dark)", groups[2]);
        wrap(groups[0], "print", groups[3]);
        return groups[0];
    }

    void line(std::string& out, std::string_view name, std::string_view value, std::string_view second = {}) const {
        if (second.empty()) put(out, {"  ", name, ": ", value, ";\n"});
        else put(out, {"  ", name, ": light-dark(", value, ", ", second, ");\n"});
    }

    // What the theme reads and never sets: Obsidian's sizes are given, the
    // rest noted once each
    void unset(std::string& root) {
        std::set<std::string> told;
        for (const auto& read : reads) {
            const std::string& name = std::get<0>(read);
            if (defined.count(name) || vars.count(name) || !told.insert(name).second) continue;
            if (const char* value = flavor == Flavor::Obsidian ? lookup(obsidianDefaults, name) : nullptr) {
                line(root, name, value);
                continue;
            }
            Diagnostic d;
            d.level = Diagnostic::Note;
            d.file = std::get<1>(read);
            d.line = std::get<2>(read);
            d.column = std::get<3>(read);
            d.message = cat({"`", name, "` is read but the theme never sets it; the app does, so what reads it is unset in Tinta"});
            notes.push_back(std::move(d));
        }
    }

    std::string finish(const std::vector<Item>& rules, const std::string& lead) {
        bool obsidian = flavor == Flavor::Obsidian, both = light && dark;
        std::string out = lead;
        put(out, {"/* Imported into Tinta", obsidian ? " from an Obsidian theme" : flavor == Flavor::Typora ? " from a Typora theme" : "",
                  " */\n", keptImports});

        // The scheme, and Obsidian's variables merged across its sides
        std::string root, sides[2];  // sides: light only, dark only
        const char* scheme = both ? "light dark" : dark ? "dark" : light ? "light" : nullptr;
        if (flavor == Flavor::Typora) {
            std::optional<Color> page = parseColor(background);
            scheme = darkQuery ? "light dark"
                   : page && page->a > 0 ? (0.2126f * page->r + 0.7152f * page->g + 0.0722f * page->b < 0.5f ? "dark" : "light")
                                         : nullptr;
        }
        if (scheme) line(root, "color-scheme", scheme);
        for (size_t k = 0; obsidian && k < names.size(); ++k) {
            const std::string& name = names[k];
            if (!both) {
                if (const Definition* d = effective(name, dark ? 'd' : light ? 'l' : 0)) line(root, name, d->value);
                continue;
            }
            // Colours on both sides as light-dark(); anything else by side
            const Definition* l = effective(name, 'l');
            const Definition* d = effective(name, 'd');
            if (l && d && l->value == d->value) {
                line(root, name, l->value);
            } else if (l && d && colourLike(l->value, 'l') && colourLike(d->value, 'd')) {
                line(root, name, l->value, d->value);
            } else {
                if (l) line(d ? root : sides[0], name, l->value);
                if (d) line(sides[1], name, d->value);
            }
        }
        unset(root);
        if (!root.empty()) put(out, {":root {\n", root, "}\n"});
        wrap(out, "(prefers-color-scheme: light)", sides[0].empty() ? "" : cat({":root {\n", sides[0], "}\n"}));
        wrap(out, "(prefers-color-scheme: dark)", sides[1].empty() ? "" : cat({":root {\n", sides[1], "}\n"}));

        // The variables each app themes with, as declarations on Tinta's
        // elements: those set on both sides, or on one side only
        std::vector<std::pair<std::string, std::string>> mapped[3];
        auto add = [&](int group, const std::string& selector, std::initializer_list<std::string_view> declaration) {
            for (auto& entry : mapped[group]) {
                if (entry.first == selector) return put(entry.second, declaration);
            }
            mapped[group].emplace_back(selector, cat(declaration));
        };
        auto map = [&](std::string_view names, const std::string& selector, std::string_view property) {
            std::string name;
            for (size_t from = 0; name.empty() && from <= names.size();) {
                size_t bar = std::min(names.find('|', from), names.size());
                std::string candidate(names.substr(from, bar - from));
                if (obsidian ? vars.count(candidate) != 0 : pageVars.count(candidate) != 0) name = candidate;
                from = bar + 1;
            }
            if (name.empty()) return;
            int group = 0;
            if (obsidian) {
                bool onLight = !both || resolves(name, 'l'), onDark = !both || resolves(name, 'd');
                if (!both && !resolves(name, dark ? 'd' : light ? 'l' : 0)) return;
                if (!onLight && !onDark) return;
                group = !onDark ? 1 : !onLight ? 2 : 0;
            }
            if (property != "@callout") return add(group, selector, {"  ", property, ": var(", name, ");\n"});
            const Definition* d = effective(name, 'd');
            if (!d) d = effective(name, 'l');
            bool rgb = d && triplet(d->value);
            std::string colour = cat({rgb ? "rgb(var(" : "var(", name, rgb ? "))" : ")"});
            add(group, selector, {"  border-left-color: ", colour, ";\n"});
            std::string titles;
            for (size_t from = 0; from < selector.size();) {
                size_t comma = std::min(selector.find(", ", from), selector.size());
                put(titles, {titles.empty() ? "" : ", ", std::string_view(selector).substr(from, comma - from), " > p.alert-title"});
                from = comma + 2;
            }
            add(group, titles, {"  color: ", colour, ";\n"});
        };
        if (obsidian) {
            for (const Variable& v : obsidianVariables) map(v.names, v.selector, v.property);
            for (char level = '1'; level <= '6'; ++level) {
                for (const auto& part : headingParts) {
                    char h[] = {'h', level, 0};
                    map(cat({"--", h, "-", part[0]}), h, part[1]);
                }
            }
        } else if (flavor == Flavor::Typora) {
            for (const Variable& v : typoraVariables) map(v.names, v.selector, v.property);
        }
        std::string blocks[3];
        for (int group = 0; group < 3; ++group) {
            for (const auto& entry : mapped[group]) put(blocks[group], {entry.first, " {\n", entry.second, "}\n"});
        }
        if (!blocks[0].empty() || !blocks[1].empty() || !blocks[2].empty()) {
            put(out, {"\n/* The theme's variables, on Tinta's elements */\n", blocks[0]});
            wrap(out, "(prefers-color-scheme: light)", blocks[1]);
            wrap(out, "(prefers-color-scheme: dark)", blocks[2]);
        }
        put(out, {"\n/* The theme's own rules */\n", write(rules)});
        return out;
    }
};

}  // namespace

Flavor detectFlavor(std::string_view css) {
    static const char* const typora[] = {"#write", "typora", "--bg-color", "--side-bar-bg-color", "md-fences",
                                         "cm-s-inner", "@include-when-export", "--monospace"};
    static const char* const obsidian[] = {".theme-dark", ".theme-light", "--background-primary", "--text-normal",
                                           "markdown-preview-view", "markdown-rendered", ".workspace",
                                           "--interactive-accent", "--text-accent"};
    int t = 0, o = 0;
    for (const char* s : typora) t += css.find(s) != std::string_view::npos;
    for (const char* s : obsidian) o += css.find(s) != std::string_view::npos;
    return t > o ? Flavor::Typora : o > t ? Flavor::Obsidian : Flavor::Detect;
}

std::string importTheme(std::string_view css, Flavor flavor, std::vector<Diagnostic>& notes, std::string_view file,
                        const ImportSource& imports) {
    if (flavor == Flavor::Detect) flavor = detectFlavor(css);
    Converter converter(flavor, notes, imports);
    if (flavor == Flavor::Detect) {
        Diagnostic d;
        d.level = Diagnostic::Note;
        d.file = std::string(file);
        d.line = d.column = 1;
        d.message = "neither a Typora nor an Obsidian theme, as far as its names show; it is read as CSS";
        notes.push_back(std::move(d));
    }
    // The comments before its first rule: its name, author and licence
    std::string_view text = css.substr(0, 3) == "\xEF\xBB\xBF" ? css.substr(3) : css;
    std::string lead;
    for (size_t i = 0; i < text.size();) {
        if (isBlank(text[i])) {
            ++i;
        } else if (text.compare(i, 2, "/*") == 0) {
            size_t end = text.find("*/", i + 2);
            end = end == std::string_view::npos ? text.size() : end + 2;
            lead += std::string(text.substr(i, end - i)) + "\n";
            if (end >= text.size() && text.compare(text.size() - 2, 2, "*/") != 0) lead += "*/\n";
            i = end;
        } else {
            break;
        }
    }
    std::vector<Item> rules = converter.sheet(text, std::string(file), true, 0);
    return converter.finish(rules, lead);
}

}
