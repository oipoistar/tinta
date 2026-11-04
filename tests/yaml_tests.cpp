// The YAML reader on its own (yaml.h): block and flow styles, scalars and
// their folding, typed values, comments and markers, front matter, the
// mistakes it refuses with their lines, and hostile input.

#include "yaml.h"

#include <iostream>
#include <string>

using json::Json;

namespace {
int failures = 0;
int checks = 0;

void check(bool value, const std::string& message) {
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void equal(const std::string& actual, const std::string& expected, const std::string& message) {
    ++checks;
    if (actual != expected) {
        std::cerr << "FAIL: " << message << "\n  expected: " << expected << "\n  actual:   " << actual << '\n';
        ++failures;
    }
}

// A value written out to compare: ~ null, #12 a number, "x" a string
std::string show(const Json& v) {
    switch (v.type) {
        case Json::Null: return "~";
        case Json::Bool: return v.boolean ? "true" : "false";
        case Json::Number: return "#" + v.text;
        case Json::String: {
            std::string out = "\"";
            for (char c : v.text) {
                if (c == '\n') out += "\\n";
                else if (c == '\t') out += "\\t";
                else if (c == '"') out += "\\\"";
                else out += c;
            }
            return out + "\"";
        }
        case Json::Array: {
            std::string out = "[";
            for (size_t i = 0; i < v.items.size(); ++i) out += (i ? ", " : "") + show(v.items[i]);
            return out + "]";
        }
        case Json::Object: {
            std::string out = "{";
            for (size_t i = 0; i < v.fields.size(); ++i) {
                out += (i ? ", " : "") + v.fields[i].first + ": " + show(v.fields[i].second);
            }
            return out + "}";
        }
    }
    return "?";
}

std::string read(const std::string& text) {
    Json v;
    yaml::Error e;
    if (!yaml::parse(text, v, e)) return "line " + std::to_string(e.line) + ": " + e.message;
    return show(v);
}

void same(const std::string& text, const std::string& want, const std::string& what) { equal(read(text), want, what); }

// A mistake on `line` whose message has `part`
void refused(const std::string& text, int line, const std::string& part, const std::string& what) {
    Json v;
    yaml::Error e;
    bool ok = yaml::parse(text, v, e);
    check(!ok && e.line == line && e.message.find(part) != std::string::npos,
          what + ": " + (ok ? std::string("read without a mistake") : "line " + std::to_string(e.line) + ": " + e.message));
}

// --- Block style -----------------------------------------------------------

void blocks() {
    same("title: Rivers of ink\nlang: en-US\nnested:\n  a: 1\n  b:\n    c: deep\n",
         "{title: \"Rivers of ink\", lang: \"en-US\", nested: {a: #1, b: {c: \"deep\"}}}", "mappings in mappings");
    same("list:\n- a\n- b\nother:\n  - x\n  -\n  - - 1\n    - 2\nlast: y\n",
         "{list: [\"a\", \"b\"], other: [\"x\", ~, [#1, #2]], last: \"y\"}",
         "a list at its key's column, an empty item, a list in a list");
    same("- name: Ana\n  role: lead\n- name: Bo\n-   indented: far\n    more: yes\n",
         "[{name: \"Ana\", role: \"lead\"}, {name: \"Bo\"}, {indented: \"far\", more: \"yes\"}]",
         "mappings as list items, at the column their first key is at");
    same("a:\nb: 1\nc:\n", "{a: ~, b: #1, c: ~}", "keys without values");
    same("  indented: root\n  second: key\n", "{indented: \"root\", second: \"key\"}", "an indented document");
    same("\"quoted key\": 1\n'single': 2\nplain key with spaces : 3\n",
         "{quoted key: #1, single: #2, plain key with spaces: #3}", "quoted keys, and spaces before the colon");
    same("- \"quoted\": first\n  then: second\n", "[{quoted: \"first\", then: \"second\"}]", "a quoted key starts a mapping in a list");
    same("plain scalar\n", "\"plain scalar\"", "a document that is one value");
    same("", "~", "an empty document");
    same("# only a comment\n\n", "~", "a document of comments");
}

// --- Flow style -------------------------------------------------------------

void flows() {
    same("tags: [ink, \"paper\", 'nib', [1, 2], {k: v}]\n", "{tags: [\"ink\", \"paper\", \"nib\", [#1, #2], {k: \"v\"}]}",
         "a flow list of every kind");
    same("map: {a: 1, b: {c: [x]}, empty: , \"json\":4, alone}\n",
         "{map: {a: #1, b: {c: [\"x\"]}, empty: ~, json: #4, alone: ~}}", "a flow mapping; JSON's \"key\":value; a key alone");
    same("tags: [ink,\n  paper,   # a comment\n  \"quoted, with comma\", ]\nnext: 1\n",
         "{tags: [\"ink\", \"paper\", \"quoted, with comma\"], next: #1}", "over lines, with a comment and a trailing comma");
    same("words: [one\n  two, three]\n", "{words: [\"one two\", \"three\"]}", "a value over lines inside [ ]");
    same("a: [a:b, http://x.org/p#q]\n", "{a: [\"a:b\", \"http://x.org/p#q\"]}", "colons and # inside flow values");
    same("[]", "[]", "an empty list");
    same("e: {}\n", "{e: {}}", "an empty mapping");
    same("- [a, b]\n- {c: d}\n", "[[\"a\", \"b\"], {c: \"d\"}]", "flow values as list items");
}

// --- Scalars ---------------------------------------------------------------

void scalars() {
    same("abstract: This is\n  a long abstract\n\n  with a paragraph.\nnext: x # comment\n",
         "{abstract: \"This is a long abstract\\nwith a paragraph.\", next: \"x\"}", "a value over lines folds");
    same("url: https://example.com/a#b\ntime: 12:30\ncolon: A paper: with a colon\nhash: C# and F#\n",
         "{url: \"https://example.com/a#b\", time: \"12:30\", colon: \"A paper: with a colon\", hash: \"C# and F#\"}",
         "colons and # inside values");
    same("- a\n  - b\n- c\n", "[\"a - b\", \"c\"]", "a deeper line goes on with the value");
    same("- id: x\n  title: A long\n    title here\n  next: 1\n", "[{id: \"x\", title: \"A long title here\", next: #1}]",
         "a value over lines in a list item's mapping");
    same("a:b\n", "\"a:b\"", "a colon without a space after it is no key");
    same("path: C:\\refs\\main.bib\nneg: -x\nnum: -5\ndash: a - b\n",
         "{path: \"C:\\refs\\main.bib\", neg: \"-x\", num: #-5, dash: \"a - b\"}", "backslashes and dashes in values");
    same("single: 'It''s here'\nempty: ''\n", "{single: \"It's here\", empty: \"\"}", "single quotes");
    same("double: \"tab\\tline\\nquote\\\" slash\\\\ \\u00e9 \\x41 \\U0001F600 \\u2019 \\ud83d\\ude00\"\n",
         "{double: \"tab\\tline\\nquote\\\" slash\\ \xC3\xA9 A \xF0\x9F\x98\x80 \xE2\x80\x99 \xF0\x9F\x98\x80\"}",
         "double-quoted escapes, a surrogate pair among them");
    same("folded: 'one\n  two\n\n  three'\n", "{folded: \"one two\\nthree\"}", "a quoted value over lines folds");
    same("escaped: \"a\\\n  b\"\n", "{escaped: \"ab\"}", "an escaped line break joins with nothing");
    same("t: \"keep\\t\n  next\"\n", "{t: \"keep\\t next\"}", "an escaped tab before a fold stays");
    same("lit: |\n  line 1\n    indented\n  line 3\n\nfold: >\n  one\n  two\n\n  three\n    more\n  four\n",
         "{lit: \"line 1\\n  indented\\nline 3\\n\", fold: \"one two\\nthree\\n  more\\nfour\\n\"}",
         "| keeps lines; > folds them, but not deeper ones");
    same("strip: |-\n  x\n\nkeep: |+\n  y\n\nafter: z\nind: |2\n    two spaces kept\n",
         "{strip: \"x\", keep: \"y\\n\\n\", after: \"z\", ind: \"  two spaces kept\\n\"}",
         "chomping and an indentation indicator");
    same("a: >-\n  folded\n  stripped\nb: |\n\n  after an empty line\n", "{a: \"folded stripped\", b: \"\\nafter an empty line\\n\"}",
         "> with -, and an empty first line");
    same("empty: |\nnext: 1\n", "{empty: \"\", next: #1}", "an empty | block");
    same("- |\n  in a list\n- >\n  folded\n  item\n", "[\"in a list\\n\", \"folded item\\n\"]", "blocks as list items");
    same("c: |\n  # not a comment\n  text\n# a comment\nd: 1\n", "{c: \"# not a comment\\ntext\\n\", d: #1}",
         "# inside a block is text");
}

void types() {
    same("n: ~\nn2: null\ne:\nt: true\nf: False\ni: 42\nneg: -7\nzero: 0\nlead: 007\nfloat: 1.50\n"
         "big: 1234567890123456\nstr: \"42\"\nword: yes\n",
         "{n: ~, n2: ~, e: ~, t: true, f: false, i: #42, neg: #-7, zero: #0, lead: \"007\", float: \"1.50\", "
         "big: \"1234567890123456\", str: \"42\", word: \"yes\"}",
         "null, booleans and whole numbers are typed; the rest are strings");
    Json v;
    yaml::Error e;
    yaml::parse("volume: 171\nflag: true\n", v, e);
    const Json* volume = v.get("volume");
    const Json* flag = v.get("flag");
    check(volume && volume->type == Json::Number && volume->number == 171 && volume->text == "171",
          "a number keeps its text");
    check(flag && flag->type == Json::Bool && flag->boolean && flag->text == "true", "true keeps its text");
}

// --- Comments, markers and line ends -----------------------------------------

void documents() {
    same("# leading comment\n---\na: 1   # trailing\n# between\n\nb: 2\n...\n", "{a: #1, b: #2}",
         "comments, and the --- and ... markers");
    same("--- |\n  a block for a document\n", "\"a block for a document\\n\"", "a document that starts on its marker's line");
    same("---\n...\n", "~", "an empty document between markers");
    same("\xEF\xBB\xBF" "a: 1\r\nb:\r\n  - x\r\n  - y\r\ns: |\r\n  one\r\n  two\r\nq: 'over\r\n  lines'\r\n",
         "{a: #1, b: [\"x\", \"y\"], s: \"one\\ntwo\\n\", q: \"over lines\"}", "a byte order mark and CRLF line ends");
    same("\xEF\xBB\xBF---\nkey: value\n", "{key: \"value\"}", "a byte order mark before ---");
    Json v;
    yaml::Error e;
    yaml::parse("a: hello\nlist:\n  - x\n", v, e);
    const Json* a = v.get("a");
    const Json* list = v.get("list");
    check(a && a->begin == 3 && a->end == 8, "a value's offsets");
    check(list && list->begin == 17 && list->items.size() == 1 && list->items[0].begin == 19 && v.begin == 0,
          "a list's offsets and its item's");
}

// --- Mistakes ---------------------------------------------------------------

void mistakes() {
    refused("a: 1\nb: &x 2\n", 2, "anchors", "an anchor");
    refused("a: *x\n", 1, "aliases", "an alias");
    refused("title: *Sepia* ink\n", 1, "quote a value that starts with *", "Markdown emphasis at a value's start");
    refused("a: !!str 1\n", 1, "tags", "a tag");
    refused("a: 1\n---\nb: 2\n", 2, "a second YAML document", "a second document");
    refused("a: 1\n...\nb: 2\n", 3, "a second YAML document", "a document after ...");
    refused("%YAML 1.2\n---\na: 1\n", 1, "directives", "a directive");
    refused("a:\n\tb: 1\n", 2, "a tab in the indentation", "a tab indents");
    refused("a: 'x\nb: 1\n", 1, "not closed", "a quote not closed");
    refused("a: [1, 2\n", 1, "not closed", "a [ not closed");
    refused("a: {b: 1\n", 1, "not closed", "a { not closed");
    refused("a:\n    b: 1\n  c: 2\n", 3, "indented more", "a key indented between two levels");
    refused("a: 1\na: 2\n", 2, "appears twice", "a key twice");
    refused("m: {k: 1, k: 2}\n", 1, "appears twice", "a flow key twice");
    refused("? a\n: b\n", 1, "complex keys", "a complex key");
    refused("a: - b\n", 1, "cannot start on the line of its key", "a list on its key's line");
    refused("a: \"x\" y\n", 1, "unexpected text after a value", "text after a quoted value");
    refused("a: \"\\q\"\n", 1, "unknown escape", "an unknown escape");
    refused("a: |x\n", 1, "unexpected text after", "text after |");
    refused("- a\nb: 1\n", 2, "unexpected text", "a key after a list at the top");
    refused("a: [b, |\n", 1, "cannot be inside", "a block inside [ ]");
    refused("a: [x, , y]\n", 1, "unexpected `,`", "an empty flow item");

    // What came before a mistake is kept
    Json v;
    yaml::Error e;
    check(!yaml::parse("items:\n- a\n- b\n- *c\n- d\n", v, e), "the alias is refused");
    const Json* items = v.get("items");
    check(items && items->items.size() == 3 && items->items[0].text == "a" && items->items[1].text == "b",
          "the items before a mistake are read: " + show(v));
    check(items && items->items.size() == 3 && items->items[2].end >= e.offset && items->items[1].end < e.offset,
          "the item cut short ends at the mistake; the one before it, before it");
    check(e.offset == 17, "the mistake's offset: " + std::to_string(e.offset));
}

// --- Front matter --------------------------------------------------------------

void frontMatters() {
    size_t begin = 0, end = 0;
    std::string doc = "---\ntitle: x\n---\nbody";
    check(yaml::frontMatter(doc, begin, end) && doc.substr(begin, end - begin) == "title: x\n", "front matter");
    doc = "\xEF\xBB\xBF---\r\ntitle: x\r\n...\r\nbody";
    check(yaml::frontMatter(doc, begin, end) && doc.substr(begin, end - begin) == "title: x\r\n",
          "a byte order mark, CRLF and a ... end");
    check(yaml::frontMatter("---\n---\nbody", begin, end) && begin == end, "empty front matter");
    check(!yaml::frontMatter("---\ntitle: x\nbody", begin, end), "no closing line: no front matter");
    check(!yaml::frontMatter(" ---\na: 1\n---\n", begin, end), "the first line must be ---");
    check(!yaml::frontMatter("----\na: 1\n---\n", begin, end), "---- is no marker");
    check(!yaml::frontMatter("text\n---\na: 1\n---\n", begin, end), "only at the start");
    check(!yaml::frontMatter("---", begin, end), "a lone ---");
}

// --- Authors and references -----------------------------------------------------

void papers() {
    // A paper's authors as structured maps (scholar.md, paper export)
    same("author:\n"
         "  - name: Ana Lima\n"
         "    affiliation: [1, 2]\n"
         "    email: ana@example.com\n"
         "    orcid: 0000-0002-1825-0097\n"
         "    corresponding: true\n"
         "  - name: Bo Chen\n"
         "    affiliation: 2\n"
         "affiliations:\n"
         "  - id: 1\n"
         "    name: University of Ink\n"
         "  - {id: 2, name: \"Paper Institute\"}\n",
         "{author: [{name: \"Ana Lima\", affiliation: [#1, #2], email: \"ana@example.com\", "
         "orcid: \"0000-0002-1825-0097\", corresponding: true}, {name: \"Bo Chen\", affiliation: #2}], "
         "affiliations: [{id: #1, name: \"University of Ink\"}, {id: #2, name: \"Paper Institute\"}]}",
         "authors with affiliations, e-mail, ORCID and a flag");
    // pandoc's manual: a CSL item in front matter
    same("---\n"
         "references:\n"
         "- type: article-journal\n"
         "  id: WatsonCrick1953\n"
         "  author:\n"
         "  - family: Watson\n"
         "    given: J. D.\n"
         "  - family: Crick\n"
         "    given: F. H. C.\n"
         "  issued:\n"
         "    date-parts:\n"
         "    - - 1953\n"
         "      - 4\n"
         "      - 25\n"
         "  title: 'Molecular structure of nucleic acids: a structure for\n"
         "    deoxyribose nucleic acid'\n"
         "  title-short: Molecular structure of nucleic acids\n"
         "  container-title: Nature\n"
         "  volume: 171\n"
         "  issue: 4356\n"
         "  page: 737-738\n"
         "  DOI: 10.1038/171737a0\n"
         "  URL: https://www.nature.com/articles/171737a0\n"
         "  language: en-GB\n"
         "...\n",
         "{references: [{type: \"article-journal\", id: \"WatsonCrick1953\", author: [{family: \"Watson\", given: \"J. D.\"}, "
         "{family: \"Crick\", given: \"F. H. C.\"}], issued: {date-parts: [[#1953, #4, #25]]}, "
         "title: \"Molecular structure of nucleic acids: a structure for deoxyribose nucleic acid\", "
         "title-short: \"Molecular structure of nucleic acids\", container-title: \"Nature\", volume: #171, issue: #4356, "
         "page: \"737-738\", DOI: \"10.1038/171737a0\", URL: \"https://www.nature.com/articles/171737a0\", "
         "language: \"en-GB\"}]}",
         "pandoc's manual example");
}

// --- Hostile input ------------------------------------------------------------

void robustness() {
    refused(std::string(10000, '['), 1, "nested too deep", "ten thousand [");
    std::string dashes;
    for (int i = 0; i < 2000; ++i) dashes += "- ";
    refused(dashes + "x\n", 1, "nested too deep", "two thousand - on a line");
    std::string keys;
    for (int i = 0; i < 200; ++i) keys += std::string((size_t)i, ' ') + "k:\n";
    Json v;
    yaml::Error e;
    check(!yaml::parse(keys, v, e) && e.message.find("nested too deep") != std::string::npos, "two hundred levels of keys");
    const char alphabet[] = "-:[]{},#'\"|>&*!?%@`~ \n\r\tabc019.\\";
    uint32_t seed = 20261104;
    for (int round = 0; round < 6000; ++round) {
        static const char* const starts[] = {"", "a:\n  - ", "- [", "k: |\n  ", "x: \""};
        std::string text = starts[round % 5];
        seed = seed * 1103515245u + 12345u;
        size_t length = (seed >> 16) % 120;
        for (size_t i = 0; i < length; ++i) {
            seed = seed * 1103515245u + 12345u;
            text += alphabet[(seed >> 16) % (sizeof alphabet - 1)];
        }
        Json out;
        yaml::Error error;
        bool ok = yaml::parse(text, out, error);
        if (!ok && (error.line < 1 || error.offset > text.size())) {
            check(false, "a mistake inside the text: " + text);
            break;
        }
        size_t b, f;
        yaml::frontMatter(text, b, f);
    }
    check(true, "random text reads");
}

}  // namespace

int main() {
    blocks();
    flows();
    scalars();
    types();
    documents();
    mistakes();
    frontMatters();
    papers();
    robustness();
    if (failures) {
        std::cerr << failures << " of " << checks << " YAML checks failed\n";
        return 1;
    }
    std::cout << "yaml: all " << checks << " checks passed\n";
    return 0;
}
