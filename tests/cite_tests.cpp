// Pandoc's citation syntax on its own (cite.h): the examples of pandoc's
// manual, keys, e-mail addresses, locators and cross-references, and the
// citations of the fixture document against the fixture library

#include "bib.h"
#include "cite.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace cite;

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

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// Every bare key cites, as in pandoc
bool every(std::string_view) { return true; }

// Items as "id|prefix|locator|label|suffix", "|-" when the author is
// suppressed, joined by "; "; an author-in-text citation starts with @
std::string show(const Citation& c) {
    std::string out = c.authorInText ? "@" : "";
    for (size_t i = 0; i < c.items.size(); ++i) {
        const Item& it = c.items[i];
        if (i) out += "; ";
        out += it.id + "|" + it.prefix + "|" + it.locator + "|" + it.label + "|" + it.suffix;
        if (it.suppressAuthor) out += "|-";
    }
    return out;
}

// The citations in a text, one a line, each with the text it spans in {}
std::string found(std::string_view text, const std::function<bool(std::string_view)>& cites = every) {
    std::string out;
    for (const Citation& c : find(text, cites)) {
        if (!out.empty()) out += "\n";
        out += show(c) + " {" + std::string(text.substr(c.begin, c.end - c.begin)) + "}";
    }
    return out;
}

std::string locator(std::string_view afterKey) {
    Item item;
    item.locator = "stale";
    splitLocator(afterKey, item);
    return item.locator + "|" + item.label + "|" + item.suffix;
}

// --- The grammar ---------------------------------------------------------

void pandocManual() {
    equal(found("Blah blah [see @doe99, pp. 33-35; also @smith04, chap. 1]."),
          "doe99|see|33-35|page|; smith04|also|1|chapter| {[see @doe99, pp. 33-35; also @smith04, chap. 1]}",
          "prefixes, locators and two citations");
    equal(found("Blah blah [@doe99, pp. 33-35, 38-39 and *passim*]."),
          "doe99||33-35, 38-39|page| and *passim* {[@doe99, pp. 33-35, 38-39 and *passim*]}",
          "two page ranges and a suffix");
    equal(found("Blah blah [@smith04; @doe99]."), "smith04||||; doe99|||| {[@smith04; @doe99]}", "two citations");
    equal(found("Smith says blah [-@smith04]."), "smith04|||||- {[-@smith04]}", "the author suppressed");
    equal(found("@smith04 says blah."), "@smith04|||| {@smith04}", "author in text");
    equal(found("@smith04 [p. 33] says blah."), "@smith04||33|page| {@smith04 [p. 33]}", "author in text with a locator");
    equal(found("[@smith{ii, A, D-Z}, with a suffix]"), "smith||ii, A, D-Z|page|, with a suffix {[@smith{ii, A, D-Z}, with a suffix]}",
          "a locator in braces right after the key");
    equal(found("[@smith, {pp. iv, vi-xi, (xv)-(xvii)} with suffix here]"),
          "smith||iv, vi-xi, (xv)-(xvii)|page| with suffix here {[@smith, {pp. iv, vi-xi, (xv)-(xvii)} with suffix here]}",
          "a labelled locator in braces");
    equal(found("[@smith{}, 99 years later]"), "smith||||, 99 years later {[@smith{}, 99 years later]}",
          "empty braces keep a number out of the locator");
    equal(found("[@{https://example.com/bib?name=foobar&date=2000}, p. 33]"),
          "https://example.com/bib?name=foobar&date=2000||33|page| {[@{https://example.com/bib?name=foobar&date=2000}, p. 33]}",
          "a key in braces");
}

void keys() {
    equal(found("[@Foo_bar.baz.]"), "Foo_bar.baz||||. {[@Foo_bar.baz.]}", "a final period is not the key's");
    equal(found("As @doe99. Then"), "@doe99|||| {@doe99}", "a period after a key");
    equal(found("@doe99, then @doe99: and @doe99? and (@doe99) and @doe99's"),
          "@doe99|||| {@doe99}\n@doe99|||| {@doe99}\n@doe99|||| {@doe99}\n@doe99|||| {@doe99}\n@doe99|||| {@doe99}",
          "punctuation after keys");
    equal(found("@doe:2020/x-y+z#1$2%3&4?5<6>7~8."), "@doe:2020/x-y+z#1$2%3&4?5<6>7~8|||| {@doe:2020/x-y+z#1$2%3&4?5<6>7~8}",
          "internal punctuation");
    equal(found("@doe- and @doe-- and @a//b"), "@doe|||| {@doe}\n@doe|||| {@doe}\n@a//b|||| {@a//b}",
          "punctuation only inside a key");
    equal(found("See @https://example.com/x."), "@https://example.com/x|||| {@https://example.com/x}", "a URL as a key");
    equal(found("@{https://example.com/bib?name=foobar&date=2000} says"),
          "@https://example.com/bib?name=foobar&date=2000|||| {@{https://example.com/bib?name=foobar&date=2000}}",
          "a key in braces in the text");
    equal(found("[@{weird key}] and [@{a;b}; @c]"), "weird key|||| {[@{weird key}]}\na;b||||; c|||| {[@{a;b}; @c]}",
          "anything up to the brace");
    equal(found("@{} and @{unclosed"), "", "an empty or unclosed key in braces");
    equal(found("@_under and @2020report and @*"),
          "@_under|||| {@_under}\n@2020report|||| {@2020report}\n@*|||| {@*}", "_, a digit and * start keys");
    equal(found("@-x and @.x and @ x"), "", "punctuation does not start a key");
    equal(found("@\xC3\xBC" "ber2020 says"), "@\xC3\xBC" "ber2020|||| {@\xC3\xBC" "ber2020}", "letters of any script");
    equal(found("@doe99\xEF\xBC\x8C" "and"), "@doe99|||| {@doe99}", "a full-width comma ends a key");
}

void textStaysText() {
    equal(found("Write to ana@example.com today, or (ana.doe@example.com)."), "", "e-mail addresses");
    equal(found("x@doe99 2@doe99 \xC3\xA9@doe99"), "", "an @ after a letter or digit");
    equal(found("\xE5\xA6\x82@zhang2020"), "", "after a Chinese character too, as in pandoc");
    equal(found("\\@doe99 and `@doe99` and ``x @doe99 x``"), "", "escaped and in code");
    equal(found("\\[@doe99]"), "@doe99|||| {@doe99}", "an escaped bracket is text, the key after it is not");
    equal(found("`unclosed @doe99"), "@doe99|||| {@doe99}", "a backtick nothing closes is text");
    equal(found("[see the docs] and [mail ana@example.com]"), "", "brackets without keys are text");
    equal(found("[^1] and [^note]"), "", "footnote references");
    equal(found("@unknown says", [](std::string_view key) { return key == "doe99"; }), "",
          "a bare key the library does not know");
    equal(found("@nobody and @doe99", [](std::string_view key) { return key == "doe99"; }), "@doe99|||| {@doe99}",
          "only the known one cites");
    equal(found("text [@unknown] text", [](std::string_view) { return false; }), "unknown|||| {[@unknown]}",
          "brackets always cite");
    equal(found("@doe99 and [@roe]", nullptr), "@doe99|||| {@doe99}\nroe|||| {[@roe]}", "no callback cites every key");
}

void brackets() {
    equal(found("![alt](img.png) and ![see @doe99](img.png)"), "@doe99|||| {@doe99}",
          "an image is no citation; a key in its text is");
    equal(found("[@doe99](https://example.com)"), "doe99|||| {[@doe99]}", "pandoc cites before it links");
    equal(found("[see @doe99; and more]"), "@doe99|||| {@doe99}",
          "a part without a key: the bracket is text, the key in it cites");
    equal(found("[see\n@doe99, p. 4]"), "doe99|see|4|page| {[see\n@doe99, p. 4]}", "a line break in a citation");
    equal(found("[see @doe99,\r\np. 4]"), "doe99|see|4|page| {[see @doe99,\r\np. 4]}", "CRLF");
    equal(found("[see @doe99,\n\np. 4]"), "@doe99|||| {@doe99}", "a blank line ends the bracket");
    equal(found("[*see* @doe99, p. 4]"), "doe99|*see*|4|page| {[*see* @doe99, p. 4]}", "Markdown in a prefix");
    equal(found("[@doe99, p. 4, [note] here]"), "doe99||4|page|, [note] here {[@doe99, p. 4, [note] here]}",
          "brackets in a suffix");
    equal(found("[@doe99, `a; b`]"), "doe99||||, `a; b` {[@doe99, `a; b`]}", "a semicolon in code");
    equal(found("[@doe99 p. 33]"), "doe99||33|page| {[@doe99 p. 33]}", "a locator without a comma");
    equal(found("[ @doe99 ]"), "doe99|||| {[ @doe99 ]}", "spaces in brackets");
    equal(found("[-@doe99, p. 4; @smith04]"), "doe99||4|page||-; smith04|||| {[-@doe99, p. 4; @smith04]}",
          "a suppressed author and a locator");
    equal(found("[@doe99][@smith04]"), "doe99|||| {[@doe99]}\nsmith04|||| {[@smith04]}", "two in a row");
    equal(found("[cf. ana@example.com and @doe99]"), "doe99|cf. ana@example.com and||| {[cf. ana@example.com and @doe99]}",
          "an e-mail address in a prefix");
    equal(found("[pre-@doe99]"), "doe99|pre-||| {[pre-@doe99]}", "a hyphen after a word is no suppression");
    equal(found("[see [@x] @doe99]"), "doe99|see [@x]||| {[see [@x] @doe99]}", "a bracket in a prefix is text");
    equal(found("[@doe99"), "@doe99|||| {@doe99}", "an unclosed bracket");
}

void authorInText() {
    equal(found("@doe99 [p. 4; see @smith04, chap. 2] says"),
          "@doe99||4|page|; smith04|see|2|chapter| {@doe99 [p. 4; see @smith04, chap. 2]}", "a locator, then more");
    equal(found("@doe99 [@smith04] says"), "@doe99||||; smith04|||| {@doe99 [@smith04]}", "a citation after the key");
    equal(found("@doe99 [see also] says"), "@doe99||||, see also {@doe99 [see also]}", "a suffix gets its comma");
    equal(found("@doe99 [link](https://x.org) and @doe99 [ref][r] and @doe99 [^1]"),
          "@doe99|||| {@doe99}\n@doe99|||| {@doe99}\n@doe99|||| {@doe99}", "links and footnotes are not locators");
    equal(found("@doe99\n[p. 4] says"), "@doe99||4|page| {@doe99\n[p. 4]}", "a line break before the bracket");
    equal(found("@doe99\n\n[p. 4]"), "@doe99|||| {@doe99}", "a blank line before it");
    equal(found("-@doe99 says"), "doe99|||||- {-@doe99}", "-@key in the text suppresses the author");
    equal(found("well-@doe99 said"), "@doe99|||| {@doe99}", "a hyphen after a word does not");
    equal(found("@doe99 [p. 4; no key here]"), "@doe99|||| {@doe99}", "a locator bracket with a part without a key");
}

void offsets() {
    std::string text = "\xE5\x8F\x82\xE8\xA7\x81[@zhang2020] and @doe99.";
    std::vector<Citation> cs = find(text, every);
    check(cs.size() == 2, "two citations after Chinese text");
    if (cs.size() == 2) {
        check(cs[0].begin == 6 && cs[0].end == 18, "the bracket's bytes");
        check(cs[1].begin == 23 && cs[1].end == 29, "the bare key's bytes");
        check(!cs[0].authorInText && cs[1].authorInText, "which is in the text");
    }
}

// --- Locators ------------------------------------------------------------

void locators() {
    const char* const cases[][2] = {
        {", p. 4", "4|page|"},
        {", pp. 33-35", "33-35|page|"},
        {", 33", "33|page|"},
        {", 33-35", "33-35|page|"},
        {", 33\xE2\x80\x93" "35", "33\xE2\x80\x93" "35|page|"},
        {", chap. 2, n. 3", "2|chapter|, n. 3"},
        {", {xiv}", "xiv|page|"},
        {", {xiv-xvi}", "xiv-xvi|page|"},
        {", {pp. iv, vi--xi}", "iv, vi--xi|page|"},
        {", {chap. 2}", "2|chapter|"},
        {", p. xiv", "xiv|page|"},
        {", xiv", "||, xiv"},
        {", p. 33, emphasis added", "33|page|, emphasis added"},
        {", emphasis added", "||, emphasis added"},
        {" and passim", "|| and passim"},
        {"and passim", "||, and passim"},
        {"", "||"},
        {", sec. 3.2.1", "3.2.1|section|"},
        {", p. 33.", "33|page|."},
        {", vol. 2, p. 3", "2|volume|, p. 3"},
        {", page", "||, page"},
        {", pageant 3", "||, pageant 3"},
        {", 34(1)(i)", "34(1)(i)|page|"},
        {", [17]-[18]", "[17]-[18]|page|"},
        {", 3:16", "3:16|page|"},
        {", ch. 3", "||, ch. 3"},
        {", pp. 33, 35, and 40", "33, 35|page|, and 40"},
        {", P. 4", "4|page|"},
        {", Chap. 2", "2|chapter|"},
        {", p.33", "33|page|"},
        {", \xC2\xA7" "3", "3|section|"},
        {", \xC2\xA7 3", "3|section|"},
        {", \xC2\xA7\xC2\xA7 3-4", "3-4|section|"},
        {", \xC2\xB6 2", "2|paragraph|"},
        {" p. 4", "4|page|"},
        {",\n  p. 4  ", "4|page|"},
        {", p. 4 ", "4|page|"},
    };
    for (const auto& c : cases) equal(locator(c[0]), c[1], std::string("locator \"") + c[0] + "\"");

    // Each term pandoc's English locale gives, short and long, single and plural
    const char* const terms[][2] = {
        {"book", "book"}, {"books", "book"}, {"bk.", "book"}, {"bks.", "book"},
        {"chapter", "chapter"}, {"chapters", "chapter"}, {"chap.", "chapter"}, {"chaps.", "chapter"},
        {"column", "column"}, {"columns", "column"}, {"col.", "column"}, {"cols.", "column"},
        {"figure", "figure"}, {"figures", "figure"}, {"fig.", "figure"}, {"figs.", "figure"},
        {"folio", "folio"}, {"folios", "folio"}, {"fol.", "folio"}, {"fols.", "folio"},
        {"number", "issue"}, {"numbers", "issue"}, {"no.", "issue"}, {"nos.", "issue"},
        {"line", "line"}, {"lines", "line"}, {"l.", "line"}, {"ll.", "line"},
        {"note", "note"}, {"notes", "note"}, {"n.", "note"}, {"nn.", "note"},
        {"opus", "opus"}, {"opera", "opus"}, {"op.", "opus"}, {"opp.", "opus"},
        {"page", "page"}, {"pages", "page"}, {"p.", "page"}, {"pp.", "page"},
        {"paragraph", "paragraph"}, {"paragraphs", "paragraph"}, {"para.", "paragraph"},
        {"paras.", "paragraph"}, {"\xC2\xB6", "paragraph"}, {"\xC2\xB6\xC2\xB6", "paragraph"},
        {"part", "part"}, {"parts", "part"}, {"pt.", "part"}, {"pts.", "part"},
        {"section", "section"}, {"sections", "section"}, {"sec.", "section"}, {"secs.", "section"},
        {"\xC2\xA7", "section"}, {"\xC2\xA7\xC2\xA7", "section"},
        {"sub verbo", "sub-verbo"}, {"sub verbis", "sub-verbo"}, {"s.v.", "sub-verbo"}, {"s.vv.", "sub-verbo"},
        {"verse", "verse"}, {"verses", "verse"}, {"v.", "verse"}, {"vv.", "verse"},
        {"volume", "volume"}, {"volumes", "volume"}, {"vol.", "volume"}, {"vols.", "volume"},
    };
    for (const auto& t : terms) {
        equal(locator(std::string(", ") + t[0] + " 12"), std::string("12|") + t[1] + "|", std::string("the term ") + t[0]);
    }
}

void crossReferences() {
    for (const char* key : {"fig:nib", "tbl:density", "eq:energy", "sec:intro", "lst:code", "Fig:nib", "Tbl:x",
                            "Eq:x", "Sec:x", "Lst:x"}) {
        check(isCrossReference(key), std::string(key) + " is a cross-reference");
    }
    for (const char* key : {"FIG:nib", "fig:", "fig", "figure:x", "doe2024", "", "tbl", "eqn:x"}) {
        check(!isCrossReference(key), std::string("\"") + key + "\" is no cross-reference");
    }
    equal(found("As @fig:nib shows, and [@fig:a; @Tbl:b] confirm, @fig:nib.", [](std::string_view key) {
              return isCrossReference(key);
          }),
          "@fig:nib|||| {@fig:nib}\nfig:a||||; Tbl:b|||| {[@fig:a; @Tbl:b]}\n@fig:nib|||| {@fig:nib}",
          "cross-references take the citation syntax");
}

// --- The fixture document ------------------------------------------------

void fixtureDocument() {
    std::string dir = TINTA_SCHOLAR_FIXTURES;
    bib::Library library;
    bib::readBibtex(readFile(dir + "/references.bib"), "references.bib", library);
    check(library.items.size() == 30, "the fixture library reads");
    auto known = [&library](std::string_view key) { return library.find(key) != nullptr; };

    // Its paragraphs as scholar.cpp sees them: no front matter, no code blocks
    std::string text;
    for (char c : readFile(dir + "/citation-syntax.md")) {
        if (c != '\r') text += c;
    }
    check(text.compare(0, 4, "---\n") == 0, "the document has front matter");
    size_t body = text.find("\n---\n", 4);
    text = body == std::string::npos ? text : text.substr(body + 5);
    std::vector<std::string> paragraphs(1);
    bool fence = false;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        if (line.compare(0, 3, "```") == 0) {
            fence = !fence;
            continue;
        }
        if (fence) continue;
        if (line.empty()) {
            if (!paragraphs.back().empty()) paragraphs.emplace_back();
        } else {
            paragraphs.back() += (paragraphs.back().empty() ? "" : "\n") + line;
        }
    }
    std::string all;
    for (const std::string& p : paragraphs) {
        std::string f = found(p, known);
        if (!f.empty()) all += (all.empty() ? "" : "\n") + f;
    }
    equal(all,
          "knuth1984|||| {[@knuth1984]}\n"
          "brooks1995|see|33-35|page|; smith2020|also|1|chapter| {[see @brooks1995, pp. 33-35; also @smith2020,\nchap. 1]}\n"
          "doe2024||||; doe2024b|||| {[@doe2024; @doe2024b]}\n"
          "lafontaine1668||xiv-xvi|page| {[@lafontaine1668, {xiv-xvi}]}\n"
          "garcia2022||3-5, 7|page| and *passim* {[@garcia2022, pp. 3-5, 7 and *passim*]}\n"
          "doe2024b|||||- {[-@doe2024b]}\n"
          "@vaswani2017|||| {@vaswani2017}\n"
          "@devlin2018||4|page| {@devlin2018 [p. 4]}\n"
          "@okafor2021||2|chapter|, n. 3 {@okafor2021 [chap. 2, n. 3]}\n"
          "@frank2019|||| {@frank2019}\n"
          "frank2019||ix|page| {[@frank2019, p. ix]}\n"
          "@zhang2020|||| {@zhang2020}\n"
          "wang2019||||, \xE7\xAC\xAC 3 \xE7\xAB\xA0 {[@wang2019, \xE7\xAC\xAC 3 \xE7\xAB\xA0]}\n"
          "li2021|||| {[@li2021]}\n"
          "who2023||||; rcore2024||2|section| {[@who2023; @rcore2024, sec. 2]}\n"
          "lindqvist2023|||| {[@lindqvist2023]}\n"
          "@rcore2024|||| {@rcore2024}\n"
          "macfarlane2024|||| {[@macfarlane2024]}\n"
          "fig:nib||||; Tbl:density|||| {[@fig:nib; @Tbl:density]}",
          "the fixture's citations, in order; its addresses, mentions and code stay text");
}

// --- Robustness ----------------------------------------------------------

void robustness() {
    std::string deep = std::string(2000, '[') + "@doe" + std::string(2000, ']');
    equal(found(deep).substr(0, 9), "doe|||| {", "brackets two thousand deep");
    std::string ats(5000, '@');
    equal(found(ats + "doe"), "@doe|||| {@doe}", "five thousand @ signs: the last starts a key");

    // Random text cannot crash the parser or make it loop
    const char alphabet[] = "@[]{}();,-\\`*^!: \n\r\tpvsecx019.\xC2\xA7\xC3\xA9";
    uint32_t seed = 20261103;
    std::string wrong;
    for (int round = 0; round < 6000; ++round) {
        static const char* const starts[] = {"", "[see @doe", "@doe [", ", "};
        std::string text = starts[round % 4];
        seed = seed * 1103515245u + 12345u;
        size_t length = (seed >> 16) % 120;
        for (size_t i = 0; i < length; ++i) {
            seed = seed * 1103515245u + 12345u;
            text += alphabet[(seed >> 16) % (sizeof alphabet - 1)];
        }
        size_t last = 0;
        for (const Citation& c : find(text, every)) {
            bool inside = c.begin >= last && c.begin < c.end && c.end <= text.size() && !c.items.empty();
            if (!inside && wrong.empty()) wrong = text;
            last = c.end;
        }
        Item item;
        splitLocator(text, item);
        isCrossReference(text);
    }
    equal(wrong, "", "random text: citations in order, inside the text, never empty");
}
}  // namespace

int main() {
    pandocManual();
    keys();
    textStaysText();
    brackets();
    authorInText();
    offsets();
    locators();
    crossReferences();
    fixtureDocument();
    robustness();
    if (failures) {
        std::cerr << failures << " of " << checks << " citation checks failed\n";
        return 1;
    }
    std::cout << "citation syntax: all " << checks << " checks passed\n";
    return 0;
}
