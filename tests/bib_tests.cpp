// Scholar's references on their own (bib.h): BibTeX syntax and its mistakes,
// LaTeX in values, names, dates, the type and field maps, CSL-JSON, and the
// fixture libraries in tests/fixtures/scholar

#include "bib.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace bib;

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

void equal(int actual, int expected, const std::string& message) {
    equal(std::to_string(actual), std::to_string(expected), message);
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string fixture(const char* name) { return readFile(std::string(TINTA_SCHOLAR_FIXTURES) + "/" + name); }

// The line a piece of text starts on, for entries whose lines the tests
// should not hard-code
int lineOf(const std::string& text, const std::string& needle) {
    size_t at = text.find(needle);
    if (at == std::string::npos) return 0;
    int line = 1;
    for (size_t i = 0; i < at; ++i) line += text[i] == '\n';
    return line;
}

std::string field(const Item* item, const char* variable) {
    if (!item) return "(no item)";
    const std::string* value = item->field(variable);
    return value ? *value : "(none)";
}

// A name as "given|droppingParticle|nonDroppingParticle|family|suffix",
// or "=literal"
std::string show(const Name& n) {
    if (!n.literal.empty()) return "=" + n.literal;
    return n.given + "|" + n.droppingParticle + "|" + n.nonDroppingParticle + "|" + n.family + "|" + n.suffix;
}

std::string showNames(const Item* item, const char* variable) {
    if (!item) return "(no item)";
    const std::vector<Name>* names = item->namesOf(variable);
    if (!names) return "(none)";
    std::string out;
    for (const Name& n : *names) out += (out.empty() ? "" : "; ") + show(n);
    return out;
}

std::string showDate(const Date& d) {
    if (d.empty()) return "(none)";
    if (!d.literal.empty()) return "'" + d.literal + "'";
    auto part = [](const DateParts& p) {
        std::string out = std::to_string(p.year);
        if (p.month) out += "-" + std::to_string(p.month);
        if (p.day) out += "-" + std::to_string(p.day);
        return out;
    };
    std::string out = part(d.from);
    if (d.to.year) out += "/" + part(d.to);
    if (d.openEnd) out += "/..";
    if (d.season) out += " season " + std::to_string(d.season);
    if (d.circa) out += " circa";
    return out;
}

std::string showDate(const Item* item, const char* variable) {
    if (!item) return "(no item)";
    const Date* d = item->date(variable);
    return d ? showDate(*d) : "(none)";
}

std::string date(const char* text) {
    Date d;
    return parseDate(text, d) ? showDate(d) : "(no date)";
}

std::string rich(const char* tex) { return latexToRich(tex); }

std::string names(const char* field) {
    std::string out;
    for (const Name& n : parseBibtexNames(field)) out += (out.empty() ? "" : "; ") + show(n);
    return out;
}

Library bibtex(const std::string& text) {
    Library library;
    readBibtex(text, "test.bib", library);
    return library;
}

Library cslJson(const std::string& text) {
    Library library;
    readCslJson(text, "test.json", library);
    return library;
}

std::string diagnostics(const Library& library) {
    std::string out;
    for (const Diagnostic& d : library.diagnostics) {
        out += (out.empty() ? "" : "\n") + d.source + ":" + std::to_string(d.line) + ": " + d.message;
    }
    return out;
}

// --- BibTeX syntax -------------------------------------------------------

void bibtexSyntax() {
    // Braces and parentheses, case in types and fields, keys kept as written
    Library a = bibtex(
        "Text between entries is ignored, and so is me@example.com.\n"
        "@ARTICLE{Doe:2024,\n  TITLE = {Braced},\n  Year = 2024,\n}\n"
        "@book(roe2020, title = \"Quoted {\"}Word{\"}\", year = \"2020\")\n"
        "@misc{nofields}\n"
        "@misc{commas,, title = {Extra commas},,}\n");
    equal((int)a.items.size(), 4, "four entries in braces and parentheses");
    equal(diagnostics(a), "", "a clean file reads without problems");
    const Item* doe = a.find("Doe:2024");
    check(doe && doe->type == "article-journal", "@ARTICLE is an article, its key kept as written");
    equal(field(doe, "title"), "Braced", "field names are case-insensitive");
    equal(showDate(doe, "issued"), "2024", "a bare number is a value");
    check(!a.find("doe:2024"), "keys compare exactly");
    equal(field(a.find("roe2020"), "title"), "Quoted \"Word\"", "a brace hides a quote in a quoted value");
    equal(a.find("nofields") ? a.find("nofields")->type : "", "document", "an entry with no fields");
    equal(field(a.find("commas"), "title"), "Extra commas", "stray commas between fields are harmless");

    // @string, # and month macros
    Library b = bibtex(
        "@STRING{ Pub = {Ink} # \" Press\" }\n"
        "@string(city = \"Oxford\")\n"
        "@string{place = city # \", UK\"}\n"
        "@book{m1, publisher = pub, address = place, title = \"Vol. \" # 3 # { of } # {Ten},\n"
        "  year = 1999, month = dec}\n"
        "@string{pub = {Redefined}}\n"
        "@book{m2, publisher = pub, year = 2000, month = jan}\n");
    equal(diagnostics(b), "", "macros read without problems");
    equal(field(b.find("m1"), "publisher"), "Ink Press", "macros join with #, names in any case");
    equal(field(b.find("m1"), "publisher-place"), "Oxford, UK", "a macro made of a macro");
    equal(field(b.find("m1"), "title"), "Vol. 3 of Ten", "braces, quotes and numbers join with #");
    equal(showDate(b.find("m1"), "issued"), "1999-12", "dec is December");
    equal(field(b.find("m2"), "publisher"), "Redefined", "a macro defined again takes its new value");
    equal(showDate(b.find("m2"), "issued"), "2000-1", "jan is January");

    // Nested braces, comments, @preamble, a byte order mark and CRLF lines
    Library c = bibtex(
        "\xEF\xBB\xBF% A comment line with an @ in it: me@x.org @misc{no, title={x}}\r\n"
        "@preamble{ \"\\newcommand{\\noopsort}[1]{}\" # \"{}\" }\r\n"
        "@comment{ an old entry: @article{gone, title = {Gone}} }\r\n"
        "@comment this line is a comment too\r\n"
        "@Comment(jabref-meta: grouping;)\r\n"
        "@article{nested,\r\n  title = {A {B {C}} D},\r\n  note = {Line one\r\n    line two},\r\n}\r\n");
    equal(diagnostics(c), "", "comments, preambles and a byte order mark read quietly");
    equal((int)c.items.size(), 1, "entries in comments are not entries");
    equal(field(c.find("nested"), "title"), "A <span class=\"nocase\">B C</span> D", "nested braces");
    equal(field(c.find("nested"), "note"), "Line one line two", "line breaks in a value are spaces");
    equal(c.find("nested") ? c.find("nested")->line : 0, 6, "lines count over CRLF");

    // A field given twice keeps the first, as BibTeX does
    Library d = bibtex("@misc{twice,\n  title = {First},\n  title = {Second},\n}\n");
    equal(field(d.find("twice"), "title"), "First", "the first of two fields is kept");
    equal(diagnostics(d), "test.bib:3: @twice has a second title; the first is used", "a field given twice");
}

void bibtexErrors() {
    // Each mistake is reported with its line, and the entries around it read
    Library a = bibtex(
        "@article{a,\n"                      // 1
        "  title = {A}\n"                    // 2
        "  year = 2020\n"                    // 3
        "}\n"                                // 4
        "@article{b title = {B}}\n"          // 5
        "@article{c,\n"                      // 6
        "  title = {C,\n"                    // 7
        "  year = 2021,\n"                   // 8
        "}\n"                                // 9
        "@article{d,\n"                      // 10
        "  title = \"D,\n"                   // 11
        "}\n"                                // 12
        "@article{e,\n"                      // 13
        "  title = {E {inner}\n"             // 14
        "}\n"                                // 15
        "\n"                                 // 16
        "@book{f, title = {F}, publisher = nowhere, year = 2019}\n"  // 17
        "@foo\n"                             // 18
        "@article{g, title {G}}\n"           // 19
        "@article{, title = {H}}\n"          // 20
        "@article{i, title = {I}, = {x}}\n"  // 21
        "@article{j, title = }\n"            // 22
        "@article{k, title = {K}\n");        // 23
    equal(diagnostics(a),
          "test.bib:2: missing comma after the title of @a\n"
          "test.bib:5: missing comma after the key @b\n"
          "test.bib:7: unbalanced braces in the title of @c\n"
          "test.bib:11: unbalanced quotes in the title of @d\n"
          "test.bib:13: the entry @e is not closed\n"
          "test.bib:17: @string nowhere is not defined\n"
          "test.bib:18: expected { or ( after @foo\n"
          "test.bib:19: expected = after title in @g\n"
          "test.bib:20: an @article entry without a key\n"
          "test.bib:21: unexpected text in @i\n"
          "test.bib:22: expected a value for title of @j\n"
          "test.bib:23: the entry @k is not closed",
          "every mistake with its line");
    equal(field(a.find("a"), "title"), "A", "a field before a missing comma");
    equal(showDate(a.find("a"), "issued"), "2020", "the field after a missing comma");
    equal(field(a.find("b"), "title"), "B", "a key without its comma");
    equal(field(a.find("c"), "title"), "C", "a value that lost its brace ends at the next field");
    equal(showDate(a.find("c"), "issued"), "2021", "the field after an unbalanced one");
    equal(field(a.find("d"), "title"), "(none)", "an unbalanced quote runs to the next entry");
    // The last value's missing brace takes the entry's: BibTeX reads it so too
    equal(field(a.find("e"), "title"), "E <span class=\"nocase\">inner</span>", "an entry cut short by the next one");
    equal(field(a.find("f"), "publisher"), "(none)", "an undefined macro is empty");
    equal(field(a.find("f"), "title"), "F", "the rest of an entry with an undefined macro");
    check(a.find("g") != nullptr && a.find("i") != nullptr && a.find("j") != nullptr, "broken entries keep their key");
    equal(field(a.find("k"), "title"), "K", "an entry the file ends inside");

    // Unknown types read as documents
    Library b = bibtex("@blogpost{blog, title = {B}}\n");
    check(b.find("blog") && b.find("blog")->type == "document", "an unknown type is a document");
    equal(diagnostics(b), "test.bib:1: unknown entry type @blogpost for @blog; read as a document", "an unknown type");

    // An unclosed @comment swallows the rest, and says so
    Library c = bibtex("@comment{ never closed\n@article{x, title = {X}}\n");
    equal(diagnostics(c), "test.bib:1: @comment is not closed", "an unclosed comment");

    // A duplicate key: the first entry wins
    Library d = bibtex("@misc{same, title = {One}}\n@misc{same, title = {Two}}\n");
    equal(field(d.find("same"), "title"), "One", "the first of two entries with a key is kept");
    equal(diagnostics(d), "test.bib:2: duplicate key @same; the first is used", "a duplicate key");
}

// --- LaTeX ---------------------------------------------------------------

void latex() {
    // Accents in each form TeX writes them, precomposed
    equal(rich("{\\\"o}"), "\xC3\xB6", "{\\\"o}");
    equal(rich("\\\"{o}"), "\xC3\xB6", "\\\"{o}");
    equal(rich("\\\"o"), "\xC3\xB6", "\\\"o");
    equal(rich("\\\" o"), "\xC3\xB6", "\\\" o");
    equal(rich("{\\\"O}sterreich"), "\xC3\x96sterreich", "a special character keeps no case");
    equal(rich("\\'e \\`a \\^o \\~n \\=a \\.z"), "\xC3\xA9 \xC3\xA0 \xC3\xB4 \xC3\xB1 \xC4\x81 \xC5\xBC",
          "acute, grave, circumflex, tilde, macron, dot");
    equal(rich("\\u{g} \\v{s} \\v s \\H{o} \\c{c} \\c c \\k{a} \\r{a} \\d{s} \\b{t}"),
          "\xC4\x9F \xC5\xA1 \xC5\xA1 \xC5\x91 \xC3\xA7 \xC3\xA7 \xC4\x85 \xC3\xA5 \xE1\xB9\xA3 \xE1\xB9\xAF",
          "breve, caron, double acute, cedilla, ogonek, ring, dot and bar below");
    equal(rich("\\'{\\i} \\\"{\\i} \\v{\\j}"), "\xC3\xAD \xC3\xAF \xC7\xB0", "dotless i and j under accents");
    equal(rich("\\\"\\i x"), "\xC3\xAF" "x", "the space after \\i ends it, as in TeX");
    equal(rich("\\\"{q} \\t{oo}"), "q\xCC\x88 o\xCD\xA1o", "no precomposed form: a combining mark");
    equal(rich("\\~{} \\^{}"), "~ ^", "\\~{} and \\^{} are the signs");

    // Letters, and the space a control word ends with
    equal(rich("\\ss \\o \\O \\ae \\AE \\oe \\OE \\l \\L \\aa \\AA \\i \\j"),
          "\xC3\x9F\xC3\xB8\xC3\x98\xC3\xA6\xC3\x86\xC5\x93\xC5\x92\xC5\x82\xC5\x81\xC3\xA5\xC3\x85\xC4\xB1\xC8\xB7",
          "special letters; a control word ends with the spaces after it");
    equal(rich("Stra\\ss e"), "Stra\xC3\x9F" "e", "Stra\\ss e");
    equal(rich("Stra\\ss{}e and \\LaTeX{} and {\\TeX}"), "Stra\xC3\x9F" "e and LaTeX and TeX", "an empty group ends a word");
    equal(rich("{\\L}ukasz {\\AA}sa Garc{\\'\\i}a"), "\xC5\x81ukasz \xC3\x85sa Garc\xC3\xAD" "a", "letters in braces");

    // Escapes, dashes, ties and quotes
    equal(rich("\\& \\% \\$ \\# \\_ \\{ \\}"), "&amp; % $ # _ { }", "escaped specials");
    equal(rich("AT&T <b> 2 > 1"), "AT&amp;T &lt;b&gt; 2 &gt; 1", "& < > are text");
    equal(rich("pp. 1--2, 1---2, 1-2"), "pp. 1\xE2\x80\x93" "2, 1\xE2\x80\x94" "2, 1-2", "en and em dashes");
    equal(rich("D.~E. Knuth"), "D.\xC2\xA0" "E. Knuth", "~ is a no-break space");
    equal(rich("``Quoted''"), "\xE2\x80\x9CQuoted\xE2\x80\x9D", "TeX quotes");
    equal(rich("10\\,km and 50\\%"), "10\xE2\x80\x89km and 50%", "a thin space");
    equal(rich("  a \n\t b  "), "a b", "spaces collapse and trim");
    equal(rich("costs $5"), "costs $5", "a dollar with no partner");

    // Formatting
    equal(rich("\\textit{a} \\emph{b} \\textbf{c} \\textsc{d} x\\textsuperscript{2} H\\textsubscript{2}O"),
          "<i>a</i> <i>b</i> <b>c</b> <span style=\"font-variant:small-caps;\">d</span> "
          "x<sup>2</sup> H<sub>2</sub>O",
          "formatting commands");
    equal(rich("{\\em Nature} and {\\bf bold} words"), "<i>Nature</i> and <b>bold</b> words", "{\\em x} and {\\bf x}");
    equal(rich("\\emph{The {DNA} of \\textbf{ink}}"),
          "<i>The <span class=\"nocase\">DNA</span> of <b>ink</b></i>", "formatting nests");
    equal(rich("\\emph{} and \\textbf{ }x"), "and x", "empty formatting leaves nothing");
    equal(rich("\\enquote{Ink} \\mkbibquote{Pens}"), "\xE2\x80\x9CInk\xE2\x80\x9D \xE2\x80\x9CPens\xE2\x80\x9D", "quotes");

    // URLs and maths as written
    equal(rich("see \\url{http://x.org/a_b~c\\%20d}"), "see <span class=\"nocase\">http://x.org/a_b~c%20d</span>",
          "\\url{} is verbatim and keeps its case");
    equal(rich("$\\alpha < \\beta$-helix and $$x^2$$"),
          "<span class=\"nocase\">$\\alpha &lt; \\beta$</span>-helix and <span class=\"nocase\">$$x^2$$</span>",
          "maths as written");

    // Braces that keep case, and braces that only group
    equal(rich("{DNA} and {Monte Carlo}"),
          "<span class=\"nocase\">DNA</span> and <span class=\"nocase\">Monte Carlo</span>", "case-protecting braces");
    equal(rich("{{The Title}}"), "<span class=\"nocase\">The Title</span>", "a whole title in braces");
    equal(rich("{1984} and {--} and {}"), "1984 and \xE2\x80\x93 and", "braces with no letters keep nothing");
    equal(rich("A{} B"), "A B", "an empty group");
    equal(rich("\\foo{Bar} \\noopsort{1973}Book \\href{http://x}{Link}"), "Bar Book Link", "other commands");
    equal(rich("Unbalanced {open and } close}"), "Unbalanced <span class=\"nocase\">open and</span> close",
          "a stray closing brace");
    equal(rich("\xC3\x85ngstr\xC3\xB6m {\xE8\xAE\xA1}"), "\xC3\x85ngstr\xC3\xB6m <span class=\"nocase\">\xE8\xAE\xA1</span>",
          "UTF-8 passes through");
    equal(rich("\\\xC3\xA9t\xC3\xA9"), "\xC3\xA9t\xC3\xA9", "a backslash before a UTF-8 letter keeps the letter whole");
}

// --- Names ---------------------------------------------------------------

void bibtexNames() {
    // The cases of the BibTeX literature
    equal(names("Jean de La Fontaine"), "Jean|de||La Fontaine|", "First von Last");
    equal(names("de La Fontaine, Jean"), "Jean|de||La Fontaine|", "von Last, First");
    equal(names("Ford, Jr., Henry"), "Henry|||Ford|Jr.", "von Last, Jr, First");
    equal(names("{Barnes and Noble, Inc.}"), "=Barnes and Noble, Inc.", "a name in braces is literal");
    equal(names("Charles Louis Xavier Joseph de la Vall{\\'e}e Poussin"),
          "Charles Louis Xavier Joseph|de la||Vall\xC3\xA9" "e Poussin|", "a long name with particles");
    equal(names("Ludwig van Beethoven"), "Ludwig|van||Beethoven|", "van");
    equal(names("van Beethoven, Ludwig"), "Ludwig|van||Beethoven|", "van, comma form");
    equal(names("jean de la fontaine"), "|jean de la||fontaine|", "all lower case: the last word is the family");
    equal(names("Donald E. Knuth"), "Donald E.|||Knuth|", "First Last");
    equal(names("D.~E. Knuth"), "D. E.|||Knuth|", "a tie separates words");
    equal(names("{\\'E}mile Zola"), "\xC3\x89mile|||Zola|", "a special character's case is its letter's");
    equal(names("Aristotle"), "|||Aristotle|", "one word is a family name");
    equal(names("{Aristotle}"), "=Aristotle", "one braced word is literal");
    equal(names("Ludwig {van Beethoven}"), "Ludwig|||van Beethoven|", "braces keep a family name whole");
    equal(names("{World Health Organization}"), "=World Health Organization", "an organisation");
    equal(names("{AT\\&T Bell Laboratories}"), "=AT&T Bell Laboratories", "names are plain text, not rich");
    equal(names("Doe, John and others"), "John|||Doe|; =others", "and others is a last literal name");
    equal(names("A. Alpha and B. Beta AND C. Gamma\n and\tD. Delta"),
          "A.|||Alpha|; B.|||Beta|; C.|||Gamma|; D.|||Delta|", "and in any case and any spaces");
    equal(names("{Simon and Schuster} and Doe, J."), "=Simon and Schuster; J.|||Doe|", "and in braces joins");
    equal(names("Doe, J. and"), "J.|||Doe|", "a trailing and");
    equal(names("Brand and Andersen, Anders"), "|||Brand|; Anders|||Andersen|", "and inside a word is no separator");
    equal(names("family=Doe, given=Jane, prefix=van der, useprefix=true"), "Jane||van der|Doe|",
          "biblatex's extended name format");
    equal(names("family=Doe, given=Jane, prefix=de"), "Jane|de||Doe|", "an extended name without useprefix");
    equal(names("\xE5\xBC\xA0\xE4\xB8\x89 and \xE6\x9D\x8E, \xE6\x98\x8E"),
          "|||\xE5\xBC\xA0\xE4\xB8\x89|; \xE6\x98\x8E|||\xE6\x9D\x8E|", "Chinese names");
    equal(names(""), "", "no names");
    equal(names(", John"), "John||||", "an empty family name keeps the comma form");

    // Options of the entry: useprefix and juniorcomma
    Library a = bibtex(
        "@book{p, author = {Ludwig van Beethoven and King, Jr., Martin Luther},\n"
        "  options = {useprefix=true, juniorcomma}}\n");
    const std::vector<Name>* authors = a.find("p") ? a.find("p")->namesOf("author") : nullptr;
    check(authors && authors->size() == 2, "two authors with options");
    if (authors && authors->size() == 2) {
        equal(show((*authors)[0]), "Ludwig||van|Beethoven|", "useprefix makes von non-dropping");
        check((*authors)[1].commaSuffix, "juniorcomma keeps the comma before Jr.");
    }
}

// --- Dates ---------------------------------------------------------------

void dates() {
    equal(date("2024"), "2024", "a year");
    equal(date("2024-03"), "2024-3", "a month");
    equal(date("2024-03-01"), "2024-3-1", "a day");
    equal(date("2024-3-1"), "2024-3-1", "one-digit month and day");
    equal(date("2024-03-01T10:20:30Z"), "2024-3-1", "a time of day is dropped");
    equal(date("2024-03-01/2024-03-05"), "2024-3-1/2024-3-5", "a range of days");
    equal(date("2024-03/2024-05"), "2024-3/2024-5", "a range of months");
    equal(date("2024/"), "2024/..", "biblatex's open range");
    equal(date("2024/.."), "2024/..", "EDTF's open range");
    equal(date("../2024"), "2024", "an open start: the end is the date");
    equal(date("2024~"), "2024 circa", "~ is circa");
    equal(date("2024?"), "2024 circa", "? is uncertain, which CSL calls circa");
    equal(date("2024-03%"), "2024-3 circa", "% is both");
    equal(date("2024-21"), "2024 season 1", "spring");
    equal(date("2024-22"), "2024 season 2", "summer");
    equal(date("2024-23"), "2024 season 3", "autumn");
    equal(date("2024-24"), "2024 season 4", "winter");
    equal(date("-0043-03-15"), "-44-3-15", "EDTF's year -43 is 44 BCE");
    equal(date("0000"), "-1", "EDTF's year 0 is 1 BCE");
    equal(date("0850"), "850", "an early year");
    equal(date("March 2024"), "2024-3", "a month name");
    equal(date("1 March 2024"), "2024-3-1", "day month year");
    equal(date("Mar. 1, 2024"), "2024-3-1", "month day, year");
    equal(date("Spring 2024"), "2024 season 1", "a season name");
    equal(date("c. 1850"), "1850 circa", "circa in words");
    equal(date("2024-25"), "(no date)", "not a season");
    equal(date("2024-13"), "(no date)", "not a month");
    equal(date("2024-00"), "(no date)", "month zero");
    equal(date("2024-21-01"), "(no date)", "a season has no day");
    equal(date("in press"), "(no date)", "words are no date");
    equal(date("/"), "(no date)", "a range open at both ends");
    equal(date(""), "(no date)", "nothing");
    equal(date("12"), "(no date)", "too short for a year");

    // The date fields of BibTeX entries
    Library a = bibtex(
        "@misc{y1, year = {2001--2003}}\n"
        "@misc{y2, year = {in press}}\n"
        "@misc{y3, year = 2020, month = {Mar.}, day = 9}\n"
        "@misc{y4, year = 2020, month = {spring}}\n"
        "@misc{y5, year = 2020, month = 7}\n"
        "@misc{y6, date = {2020-05-01/2020-05-03}, year = 1999}\n"
        "@misc{y7, date = {sometime}}\n"
        "@misc{y8, year = 2020, month = aug # \"--\" # sep}\n"
        "@online{y9, urldate = {2026-10-01}, eventdate = {2020~}, origdate = {1850}}\n");
    equal(showDate(a.find("y1"), "issued"), "2001/2003", "a range of years");
    equal(showDate(a.find("y2"), "issued"), "'in press'", "a year in words is shown as written");
    equal(showDate(a.find("y3"), "issued"), "2020-3-9", "year, month and day");
    equal(showDate(a.find("y4"), "issued"), "2020 season 1", "a season in the month field");
    equal(showDate(a.find("y5"), "issued"), "2020-7", "a month number");
    equal(showDate(a.find("y6"), "issued"), "2020-5-1/2020-5-3", "date wins over year");
    equal(showDate(a.find("y7"), "issued"), "'sometime'", "a date field that is no date");
    equal(showDate(a.find("y8"), "issued"), "2020-8", "a range of month macros starts the month");
    equal(showDate(a.find("y9"), "accessed"), "2026-10-1", "urldate is accessed");
    equal(showDate(a.find("y9"), "event-date"), "2020 circa", "eventdate");
    equal(showDate(a.find("y9"), "original-date"), "1850", "origdate");
    equal(diagnostics(a), "test.bib:7: the date of @y7 is not a date: sometime", "a date field that is no date");
}

// --- Types and fields ----------------------------------------------------

void typeMap() {
    const char* const types[][2] = {
        {"article", "article-journal"}, {"book", "book"}, {"mvbook", "book"},
        {"inbook", "chapter"}, {"incollection", "chapter"}, {"bookinbook", "chapter"},
        {"inproceedings", "paper-conference"}, {"conference", "paper-conference"},
        {"proceedings", "book"}, {"phdthesis", "thesis"}, {"mastersthesis", "thesis"},
        {"thesis", "thesis"}, {"techreport", "report"}, {"report", "report"}, {"manual", "book"},
        {"online", "webpage"}, {"electronic", "webpage"}, {"www", "webpage"}, {"patent", "patent"},
        {"dataset", "dataset"}, {"software", "software"}, {"unpublished", "manuscript"},
        {"misc", "document"}, {"collection", "book"}, {"booklet", "pamphlet"},
        {"inreference", "entry-encyclopedia"}, {"periodical", "periodical"},
    };
    for (const auto& t : types) {
        Library library = bibtex(std::string("@") + t[0] + "{k, title = {T}}");
        check(library.diagnostics.empty(), std::string("@") + t[0] + " reads without problems");
        equal(library.find("k") ? library.find("k")->type : "(none)", t[1], std::string("@") + t[0]);
    }
    Library sub = bibtex(
        "@article{mag, entrysubtype = {magazine}}\n@article{news, entrysubtype = {newspaper}}\n"
        "@phdthesis{phd, title = {P}}\n@mastersthesis{ma, title = {M}}\n@thesis{t, type = {mathesis}}\n"
        "@techreport{tr, type = {Working Paper}, number = {12}}\n@set{s, entryset = {a,b}}\n");
    equal(sub.find("mag") ? sub.find("mag")->type : "", "article-magazine", "a magazine article");
    equal(sub.find("news") ? sub.find("news")->type : "", "article-newspaper", "a newspaper article");
    equal(field(sub.find("phd"), "genre"), "PhD thesis", "a PhD thesis has a genre");
    equal(field(sub.find("ma"), "genre"), "Master\xE2\x80\x99s thesis", "a master's thesis has a genre");
    equal(field(sub.find("t"), "genre"), "Master\xE2\x80\x99s thesis", "biblatex's type keys");
    equal(field(sub.find("tr"), "genre"), "Working Paper", "type is the genre");
    equal(field(sub.find("tr"), "number"), "12", "a report's number is its number");
    check(!sub.find("s"), "@set is no reference");
}

void fieldMap() {
    Library a = bibtex(
        "@book{why, title = {Why?}, subtitle = {An Answer}, titleaddon = {Revised}, shorttitle = {Why}}\n"
        "@misc{hp1, howpublished = {Blog post}}\n"
        "@misc{hp2, howpublished = {https://example.com/x?a=1&b=2}}\n"
        "@misc{ps, pubstate = {inpress}}\n"
        "@article{ids, doi = {doi:10.1000/XYZ}, url = {http://x.org/a\\_b\\%20}, pmid = {123}, eprint = {456},\n"
        "  eprinttype = {pubmed}}\n"
        "@misc{arx, eprint = {arXiv:2101.00001}, eprinttype = {arxiv}, eprintclass = {cs.DL}}\n"
        "@article{lang, langid = {ngerman}}\n@article{lang2, language = {Chinese}}\n"
        "@article{lang3, language = {zh-TW}}\n"
        "@book{bk, booktitle = {In a Book}, maintitle = {Ignored}}\n"
        "@inproceedings{ip, booktitle = {Proc.}, journal = {J.}}\n");
    equal(diagnostics(a), "", "the field map reads without problems");
    equal(field(a.find("why"), "title"), "Why? An Answer. Revised", "a subtitle after a question mark");
    equal(field(a.find("why"), "title-short"), "Why", "shorttitle");
    equal(field(a.find("hp1"), "medium"), "Blog post", "howpublished as a medium");
    equal(field(a.find("hp2"), "URL"), "https://example.com/x?a=1&amp;b=2", "howpublished as a URL, escaped");
    equal(field(a.find("ps"), "status"), "in press", "pubstate");
    equal(field(a.find("ids"), "DOI"), "10.1000/XYZ", "doi: is no part of a DOI");
    equal(field(a.find("ids"), "URL"), "http://x.org/a_b%20", "a URL without TeX escapes");
    equal(field(a.find("ids"), "PMID"), "123", "pmid");
    equal(field(a.find("arx"), "URL"), "https://arxiv.org/abs/2101.00001", "an arXiv eprint has a URL");
    equal(field(a.find("arx"), "number"), "arXiv:2101.00001 [cs.DL]", "and a number");
    check(a.find("arx") && a.find("arx")->type == "article", "a preprint is an article");
    equal(field(a.find("lang"), "language"), "de-DE", "babel names become language tags");
    equal(field(a.find("lang2"), "language"), "zh-CN", "Chinese");
    equal(field(a.find("lang3"), "language"), "zh-TW", "a tag stays");
    equal(field(a.find("bk"), "container-title"), "In a Book", "a booktitle elsewhere is still the container");
    equal(field(a.find("ip"), "container-title"), "Proc.", "a paper's booktitle comes before a journal");

    // The fixture library: every field Tinta maps
    std::string text = fixture("references.bib");
    Library lib;
    readBibtex(text, "references.bib", lib);
    equal(diagnostics(lib), "", "references.bib reads without problems");
    equal((int)lib.items.size(), 30, "thirty references");

    const Item* knuth = lib.find("knuth1984");
    check(knuth && knuth->type == "article-journal", "knuth1984 is a journal article");
    equal(knuth ? knuth->line : 0, lineOf(text, "@article{knuth1984"), "knuth1984's line");
    equal(knuth ? knuth->source : "", "references.bib", "knuth1984's source");
    equal(showNames(knuth, "author"), "Donald E.|||Knuth|", "Knuth");
    equal(field(knuth, "title"), "Literate Programming", "title");
    equal(field(knuth, "container-title"), "The Computer Journal", "journal, from a macro");
    equal(showDate(knuth, "issued"), "1984-5", "year and month macro");
    equal(field(knuth, "volume"), "27", "volume");
    equal(field(knuth, "issue"), "2", "an article's number is its issue");
    equal(field(knuth, "page"), "97\xE2\x80\x93" "111", "pages");
    equal(field(knuth, "DOI"), "10.1093/comjnl/27.2.97", "doi");
    equal(field(knuth, "publisher"), "Oxford University Press", "publisher");

    const Item* doe = lib.find("doe2024");
    equal(field(doe, "title"),
          "Ink Flow in <span class=\"nocase\">DNA</span>-Printed Nibs: A <span class=\"nocase\">Monte Carlo</span> Study",
          "title and subtitle, case kept");
    equal(field(doe, "container-title"), "Journal of Ink <span class=\"nocase\">Studies</span>",
          "journaltitle from a macro joined with #");
    equal(field(doe, "container-title-short"), "J. Ink Stud.", "shortjournal");
    equal(showDate(doe, "issued"), "2024-3-1", "date");
    equal(field(doe, "DOI"), "10.5555/ink.2024.0033", "a DOI without https://doi.org/");
    equal(field(doe, "ISSN"), "1234-5679", "issn");
    equal(field(doe, "language"), "en", "langid");
    equal(field(doe, "keyword"), "ink, nibs, simulation", "keywords");
    equal(field(doe, "abstract"),
          "We model ink in nibs &amp; show that <span class=\"nocase\">DNA</span> printing changes the flow by 5%.",
          "abstract");

    const Item* doeB = lib.find("doe2024b");
    check(doeB && doeB->type == "paper-conference", "@conference is a conference paper");
    equal(showNames(doeB, "author"), "Jane|||Doe|", "the same first author as doe2024");
    equal(showDate(doeB, "issued"), "2024", "and the same year, from the crossref");
    equal(field(doeB, "container-title"), "Proceedings of the Third Ink Conference", "the parent's title is the book");
    equal(showNames(doeB, "editor"), "Ilse|||Frank|; Anna|||Smith|", "the parent's editors");
    equal(field(doeB, "publisher"), "Association for Computing Machinery", "the parent's publisher");
    equal(field(doeB, "publisher-place"), "New York", "location");
    equal(field(doeB, "event-title"), "Third Ink Conference", "eventtitle");
    equal(field(doeB, "event-place"), "Lisbon, Portugal", "venue");
    equal(showDate(doeB, "event-date"), "2024-6-3/2024-6-5", "eventdate");
    equal(field(doeB, "page"), "7\xE2\x80\x93" "12", "its own pages");
    check(doeB && !doeB->field("title-short"), "a parent's shorttitle is not inherited");

    const Item* smith19 = lib.find("smith2019");
    check(smith19 && smith19->type == "chapter", "an incollection is a chapter");
    equal(field(smith19, "container-title"), "Pens and Their Makers: An Illustrated History", "title + subtitle to booktitle");
    equal(showNames(smith19, "editor"), "Ilse|||Frank|", "editor from the collection");
    equal(field(smith19, "publisher"), "Thames &amp; Hudson", "publisher from the collection");
    equal(showDate(smith19, "issued"), "2019", "date from the collection");
    equal(field(smith19, "chapter-number"), "3", "chapter");
    equal(showNames(smith19, "author"), "Anna|||Smith|", "Anna Smith");
    equal(showNames(lib.find("smith2020"), "author"), "John Q.|||Smith|", "and John Q. Smith share a surname");

    const Item* frank = lib.find("frank2019");
    check(frank && frank->type == "book", "a collection is a book");
    equal(showNames(frank, "editor"), "Ilse|||Frank|", "editor");
    equal(showNames(frank, "author"), "(none)", "an edited book has no author");
    equal(field(frank, "number-of-pages"), "320", "pagetotal");
    equal(field(frank, "publisher-place"), "London", "location");

    const Item* smith20 = lib.find("smith2020");
    equal(field(smith20, "title"), "Rivers of Ink: A History of Writing Fluids", "a book's subtitle");
    equal(field(smith20, "edition"), "2", "edition");
    equal(field(smith20, "collection-title"), "Studies in Material Culture", "series");
    equal(field(smith20, "collection-number"), "14", "a book's number is in its series");
    equal(field(smith20, "publisher-place"), "Oxford", "address");
    equal(field(smith20, "ISBN"), "978-0-19-000000-2", "isbn");

    const Item* brooks = lib.find("brooks1995");
    equal(showNames(brooks, "author"), "Frederick P.|||Brooks|Jr.", "Brooks, Jr., Frederick P.");
    equal(field(brooks, "title"), "The Mythical Man-Month: Essays on Software Engineering", "parentheses and quotes");
    equal(field(brooks, "publisher"), "Addison-Wesley", "a macro defined in parentheses");
    equal(field(brooks, "publisher-place"), "Reading, MA", "quoted parts joined with #");
    equal(field(brooks, "edition"), "Anniversary", "an edition in words");

    equal(showNames(lib.find("vanrossum2009"), "author"), "Guido||van|Rossum|; Fred L.|||Drake|",
          "van Rossum with useprefix");
    equal(field(lib.find("vanrossum2009"), "publisher"), "CreateSpace", "organization is a publisher");
    equal(showNames(lib.find("lafontaine1668"), "author"), "Jean|de||La Fontaine|", "Jean de La Fontaine");
    equal(field(lib.find("lafontaine1668"), "language"), "fr-FR", "french");
    equal(showNames(lib.find("delafuente2019"), "author"), "Mar\xC3\xAD" "a|de la||Fuente|", "de la Fuente");
    equal(field(lib.find("delafuente2019"), "title"),
          "Inks of the <span class=\"nocase\">Spanish</span> <span class=\"nocase\">Golden Age</span>", "two protected words");
    equal(showNames(lib.find("calvino1981"), "translator"), "William|||Weaver|", "translator");
    equal(showDate(lib.find("calvino1981"), "original-date"), "1979", "origdate");
    equal(showNames(lib.find("taylor2019"), "container-author"), "Chidi|||Okafor|", "bookauthor");
    equal(field(lib.find("taylor2019"), "page"), "ix\xE2\x80\x93xii", "roman pages");

    const Item* consortium = lib.find("consortium2022");
    const std::vector<Name>* many = consortium ? consortium->namesOf("author") : nullptr;
    equal(many ? (int)many->size() : 0, 22, "twenty-two authors");
    if (many && many->size() == 22) {
        equal(show((*many)[7]), "Hugo|||Hern\xC3\xA1ndez|", "an accent in a family name");
        equal(show((*many)[11]), "Luc\xC3\xAD" "a|||L\xC3\xB3pez|", "UTF-8 as written");
        equal(show((*many)[12]), "Klaus|||M\xC3\xBCller|", "M{\\\"u}ller");
        equal(show((*many)[14]), "Owen|||O'Brien|", "an apostrophe");
        equal(show((*many)[21]), "Vera|||Varga|", "the last of them");
    }
    equal(field(consortium, "title"), "The <span class=\"nocase\">Ink Atlas</span>: Viscosity of 1,200 Writing Inks",
          "a protected name in a title");

    equal(field(lib.find("hart2023"), "container-title"), "Ink Monthly", "a magazine");
    check(lib.find("hart2023") && lib.find("hart2023")->type == "article-magazine", "entrysubtype magazine");
    equal(field(lib.find("garcia2022"), "title"), "Ink Pigments in <i>Sepia officinalis</i>", "\\textit");
    equal(field(lib.find("garcia2022"), "note"), "Open access", "note");
    equal(showNames(lib.find("garcia2022"), "author"),
          "Ana|||Garc\xC3\xAD" "a|; Klaus|||M\xC3\xBCller|; Chidi|||Okafor|", "three authors");

    const Item* vaswani = lib.find("vaswani2017");
    const std::vector<Name>* eight = vaswani ? vaswani->namesOf("author") : nullptr;
    equal(eight ? (int)eight->size() : 0, 8, "eight authors");
    if (eight && eight->size() == 8) equal(show((*eight)[6]), "\xC5\x81ukasz|||Kaiser|", "{\\L}ukasz");
    equal(field(vaswani, "container-title"), "Advances in Neural Information Processing Systems", "booktitle");
    equal(field(vaswani, "event-title"),
          "31st Conference on Neural Information Processing Systems (<span class=\"nocase\">NIPS</span> 2017)", "eventtitle");
    equal(showDate(vaswani, "event-date"), "2017-12-4/2017-12-9", "a range of days");
    equal(field(vaswani, "URL"), "https://papers.nips.cc/paper/7181-attention-is-all-you-need", "url");

    const Item* okafor = lib.find("okafor2021");
    check(okafor && okafor->type == "thesis", "@thesis");
    equal(field(okafor, "genre"), "PhD thesis", "type = phdthesis");
    equal(field(okafor, "publisher"), "University of Edinburgh", "institution");
    equal(showDate(okafor, "issued"), "2021-7", "a month date");

    const Item* who = lib.find("who2023");
    check(who && who->type == "report", "@techreport");
    equal(showNames(who, "author"), "=World Health Organization", "a corporate author");
    equal(field(who, "publisher"), "World Health Organization", "institution");
    equal(field(who, "URL"), "https://www.who.int/publications/i/item/9789240083851", "url");

    check(lib.find("okafor2025") && lib.find("okafor2025")->type == "manuscript", "@unpublished");
    equal(field(lib.find("okafor2025"), "note"), "Manuscript in preparation", "an unpublished note");
    check(lib.find("tanaka2018") && lib.find("tanaka2018")->type == "patent", "@patent");
    equal(field(lib.find("tanaka2018"), "number"), "US 10,000,001 B2", "a patent number");

    const Item* pandoc = lib.find("macfarlane2024");
    check(pandoc && pandoc->type == "webpage", "@online");
    equal(showDate(pandoc, "accessed"), "2026-10-1", "urldate");
    equal(field(pandoc, "title"), "Pandoc User's Guide", "an apostrophe stays");

    const Item* unicode = lib.find("unicode2023");
    equal(field(unicode, "URL"), "https://www.unicode.org/versions/Unicode15.1.0/", "howpublished with \\url");
    equal(showDate(unicode, "issued"), "2023-9-12", "year, month macro and day");
    equal(showNames(unicode, "author"), "=The Unicode Consortium", "The Unicode Consortium");

    const Item* data = lib.find("lindqvist2023");
    check(data && data->type == "dataset", "@dataset");
    equal(showNames(data, "author"), "\xC3\x85sa|||Lindqvist|; Chidi|||Okafor|", "{\\AA}sa");
    equal(field(data, "title"), "Viscosity of Fountain Pen Inks at 10\xE2\x80\x93" "40\xE2\x80\x89\xC2\xB0" "C",
          "a thin space and a degree sign");
    equal(field(data, "version"), "2.1", "version");
    equal(field(data, "publisher"), "Zenodo", "publisher");

    const Item* r = lib.find("rcore2024");
    check(r && r->type == "software", "@software");
    equal(showNames(r, "author"), "=R Core Team", "a team");
    equal(field(r, "version"), "4.4.0", "a software version");
    equal(field(r, "publisher"), "R Foundation for Statistical Computing", "organization");

    const Item* bert = lib.find("devlin2018");
    check(bert && bert->type == "article", "an arXiv preprint");
    equal(field(bert, "number"), "arXiv:1810.04805 [cs.CL]", "archivePrefix and primaryClass");
    equal(field(bert, "URL"), "https://arxiv.org/abs/1810.04805", "the abstract page");
    equal(field(bert, "title"),
          "<span class=\"nocase\">BERT</span>: Pre-training of Deep Bidirectional Transformers for Language Understanding",
          "a protected acronym");
    equal(showNames(bert, "author"),
          "Jacob|||Devlin|; Ming-Wei|||Chang|; Kenton|||Lee|; Kristina|||Toutanova|", "First Last names");

    // In Chinese, for GB/T 7714
    const Item* zhang = lib.find("zhang2020");
    equal(showNames(zhang, "author"), "|||\xE5\xBC\xA0\xE4\xB8\x89|; |||\xE6\x9D\x8E\xE5\x9B\x9B|", "Chinese authors");
    equal(field(zhang, "language"), "zh", "language zh");
    equal(field(zhang, "page"), "812\xE2\x80\x93" "826", "pages");
    const Item* li = lib.find("li2021");
    equal(showNames(li, "author"), "\xE6\x98\x8E|||\xE6\x9D\x8E|", "a Chinese name with a comma");
    equal(field(li, "genre"), "\xE7\xA1\x95\xE5\xA3\xAB\xE5\xAD\xA6\xE4\xBD\x8D\xE8\xAE\xBA\xE6\x96\x87", "a thesis type");
    check(li && li->type == "thesis", "a master's thesis");
    check(lib.find("wang2019") && lib.find("wang2019")->type == "book", "a Chinese book");
    check(lib.find("chen2023") && lib.find("chen2023")->type == "webpage", "a Chinese web page");
    equal(showDate(lib.find("chen2023"), "accessed"), "2026-10-12", "its access date");
}

void brokenFixture() {
    Library lib;
    readBibtex(fixture("broken.bib"), "broken.bib", lib);
    equal(diagnostics(lib),
          "broken.bib:12: missing comma after the author of @nocomma2021\n"
          "broken.bib:20: unbalanced braces in the title of @unbalanced2019\n"
          "broken.bib:25: duplicate key @good2020; the first is used\n"
          "broken.bib:31: unknown entry type @blogpost for @unknown2023; read as a document\n"
          "broken.bib:40: @string undefinedpress is not defined\n"
          "broken.bib:44: the entry @unclosed2017 is not closed\n"
          "broken.bib:49: an @article entry without a key",
          "broken.bib: each mistake on its line");
    equal((int)lib.items.size(), 7, "seven entries read around the mistakes");
    equal(showNames(lib.find("good2020"), "author"), "Grace|||Good|", "the first good2020 is kept");
    equal(field(lib.find("nocomma2021"), "title"), "A Field Without Its Comma", "the field after a missing comma");
    equal(field(lib.find("unbalanced2019"), "title"), "An <span class=\"nocase\">Unbalanced</span> Title",
          "an unclosed title ends with its line");
    equal(field(lib.find("unbalanced2019"), "publisher"), "Brace Press", "and the fields after it read");
    check(lib.find("unknown2023") && lib.find("unknown2023")->type == "document", "an unknown type is a document");
    equal(field(lib.find("macro2018"), "title"), "A Macro Nobody Defined", "an entry with an undefined macro");
    equal(showDate(lib.find("unclosed2017"), "issued"), "2017", "an unclosed entry keeps its fields");
    equal(field(lib.find("last2016"), "title"), "The Entry After the Mistakes", "the entry after the mistakes");
}

// --- CSL-JSON ------------------------------------------------------------

void cslJsonFixture() {
    std::string text = fixture("references.json");
    Library lib;
    readCslJson(text, "references.json", lib);
    equal(diagnostics(lib), "", "references.json reads without problems");
    equal((int)lib.items.size(), 11, "eleven references");

    const Item* knuth = lib.find("knuth1984");
    check(knuth && knuth->type == "article-journal", "a CSL type");
    equal(knuth ? knuth->line : 0, lineOf(text, "\"knuth1984\"") - 1, "an item's line is its opening brace");
    equal(field(knuth, "issue"), "2", "a number as a JSON number");
    equal(field(knuth, "volume"), "27", "a number as a string");
    equal(showDate(knuth, "issued"), "1984-5", "date-parts");
    equal(showNames(knuth, "author"), "Donald E.|||Knuth|", "family and given");

    const Item* doe = lib.find("doe2024");
    equal(showDate(doe, "issued"), "2024-3-1", "date-parts as strings");
    equal(field(doe, "title"),
          "Ink Flow in <span class=\"nocase\">DNA</span>-Printed Nibs: A <span class=\"nocase\">Monte Carlo</span> Study",
          "rich text kept");
    equal(field(lib.find("garcia2022"), "title"), "Ink Pigments in <i>Sepia officinalis</i>", "italics kept");
    equal(field(lib.find("garcia2022"), "volume"), "8", "a JSON number");

    const Item* vaswani = lib.find("vaswani2017");
    const std::vector<Name>* eight = vaswani ? vaswani->namesOf("author") : nullptr;
    if (eight && eight->size() == 8) equal(show((*eight)[6]), "\xC5\x81ukasz|||Kaiser|", "a \\u escape in a name");
    else check(false, "vaswani2017 has eight authors");
    equal(showDate(vaswani, "event-date"), "2017-12-4/2017-12-9", "a range in date-parts");
    equal(field(vaswani, "event-title"), "31st Conference on Neural Information Processing Systems (NIPS 2017)",
          "event-title");

    equal(showNames(lib.find("vanrossum2009"), "author"), "Guido||van|Rossum|; Fred L.|||Drake|",
          "a particle read from the family name");
    equal(showNames(lib.find("lafontaine1668"), "author"), "Jean|de||La Fontaine|", "an explicit particle");
    equal(showNames(lib.find("who2023"), "author"), "=World Health Organization", "a literal name");
    equal(field(lib.find("macfarlane2024"), "title"), "Pandoc User\xE2\x80\x99s Guide", "a \\u escape in a title");
    equal(showDate(lib.find("macfarlane2024"), "accessed"), "2026-10-1", "a raw date");
    equal(showNames(lib.find("zhang2020"), "author"), "\xE4\xB8\x89|||\xE5\xBC\xA0|; \xE5\x9B\x9B|||\xE6\x9D\x8E|",
          "Chinese family and given names");
    equal(field(lib.find("zhang2020"), "language"), "zh-CN", "language");
    check(lib.find("42") != nullptr, "a numeric id is a key");
    equal(showDate(lib.find("42"), "issued"), "1979-10-12", "a full date");
    equal(showDate(lib.find("pliny77"), "issued"), "77 circa", "circa");
    equal(showNames(lib.find("pliny77"), "author"), "=Pliny the Elder", "a literal author");

    // The same works read from BibTeX match where both write them alike
    Library bib;
    readBibtex(fixture("references.bib"), "references.bib", bib);
    for (const char* key : {"knuth1984", "doe2024", "garcia2022", "vanrossum2009", "lafontaine1668"}) {
        const Item* fromJson = lib.find(key);
        const Item* fromBib = bib.find(key);
        check(fromJson && fromBib, std::string(key) + " in both");
        if (!fromJson || !fromBib) continue;
        equal(fromJson->type, fromBib->type, std::string(key) + ": the same type");
        equal(field(fromJson, "title"), field(fromBib, "title"), std::string(key) + ": the same title");
        equal(showNames(fromJson, "author"), showNames(fromBib, "author"), std::string(key) + ": the same authors");
        equal(showDate(fromJson, "issued"), showDate(fromBib, "issued"), std::string(key) + ": the same date");
    }

    // Both files in one library: the keys they share are duplicates
    readCslJson(text, "references.json", bib);
    bool reported = false;
    for (const Diagnostic& d : bib.diagnostics) {
        reported = reported || (d.source == "references.json" && d.message == "duplicate key @knuth1984; the first is used");
    }
    check(reported, "a key in two files is a duplicate");
    check(bib.find("42") != nullptr, "keys only one file has are added");
}

void cslJsonVariants() {
    Library one = cslJson("{\"id\": \"solo\", \"type\": \"book\", \"title\": \"Alone\"}");
    equal(field(one.find("solo"), "title"), "Alone", "one item, not in an array");

    Library a = cslJson(
        "[\n"
        "  {\"id\": 1.5, \"type\": \"book\", \"volume\": 12, \"page\": 7,\n"
        "   \"issued\": {\"raw\": \"March 2024\"}, \"accessed\": \"2026-01-02\"},\n"
        "  {\"id\": \"lit\", \"type\": \"book\", \"issued\": {\"literal\": \"Spring term\"}},\n"
        "  {\"id\": \"season\", \"type\": \"book\", \"issued\": {\"date-parts\": [[2020]], \"season\": \"3\"}},\n"
        "  {\"id\": \"season2\", \"type\": \"book\", \"issued\": {\"date-parts\": [[2020, 22]]}},\n"
        "  {\"id\": \"season3\", \"type\": \"book\", \"issued\": {\"date-parts\": [[2020]], \"season\": \"Winter\"}},\n"
        "  {\"id\": \"circa\", \"type\": \"book\", \"issued\": {\"date-parts\": [[1850]], \"circa\": \"1\"}},\n"
        "  {\"id\": \"open\", \"type\": \"book\", \"issued\": {\"date-parts\": [[2020], []]}},\n"
        "  {\"id\": \"edtf\", \"type\": \"book\", \"issued\": {\"raw\": \"2020-05-01/2020-06\"}},\n"
        "  {\"id\": \"old\", \"type\": \"book\", \"issued\": {\"year\": 1999, \"month\": \"4\"}},\n"
        "  {\"id\": \"rich\", \"type\": \"book\", \"title\": \"AT&T <b>bold</b> &amp; <script>x</script> &#38; &quot;q&quot; <i>open\",\n"
        "   \"container-title\": \"x</i> &lt;y&gt; <span style=\\\"font-variant: small-caps\\\">sc</span> <em>e</em>\"},\n"
        "  {\"id\": \"names\", \"type\": \"book\", \"author\": [\"Doe, Jane\", {\"family\": \"van Gogh\", \"given\": \"Vincent\",\n"
        "   \"parse-names\": false}, {\"family\": \"Beethoven\", \"given\": \"Ludwig van\"}, {\"literal\": \"\"}],\n"
        "   \"editor\": [{\"family\": \"Smith\", \"suffix\": \"Jr.\", \"comma-suffix\": true}], \"categories\": [\"x\"]},\n"
        "  {\"id\": \"alias\", \"type\": \"book\", \"shortTitle\": \"Short\", \"journalAbbreviation\": \"J.\",\n"
        "   \"event\": \"Old name\", \"event-title\": \"New name\"},\n"
        "  {\"id\": \"strange\", \"type\": \"blog\"},\n"
        "  {\"title\": \"No id\"},\n"
        "  {\"id\": \"notype\"},\n"
        "  \"not an item\"\n"
        "]");
    equal(diagnostics(a),
          "test.json:19: unknown type blog for @strange; read as a document\n"
          "test.json:20: a reference without an id\n"
          "test.json:21: @notype has no type; read as a document\n"
          "test.json:22: a reference that is not an object",
          "CSL-JSON problems with their lines");
    const Item* n = a.find("1.5");
    check(n != nullptr, "a number id that is not whole");
    equal(field(n, "volume"), "12", "a number variable as a number");
    equal(field(n, "page"), "7", "page as a number");
    equal(showDate(n, "issued"), "2024-3", "a raw date in words");
    equal(showDate(n, "accessed"), "2026-1-2", "a date as a plain string");
    equal(showDate(a.find("lit"), "issued"), "'Spring term'", "a literal date");
    equal(showDate(a.find("season"), "issued"), "2020 season 3", "a season as a string");
    equal(showDate(a.find("season2"), "issued"), "2020 season 2", "a season in date-parts");
    equal(showDate(a.find("season3"), "issued"), "2020 season 4", "a season by name");
    equal(showDate(a.find("circa"), "issued"), "1850 circa", "circa as a string");
    equal(showDate(a.find("open"), "issued"), "2020/..", "an open range");
    equal(showDate(a.find("edtf"), "issued"), "2020-5-1/2020-6", "an EDTF range in raw");
    equal(showDate(a.find("old"), "issued"), "1999-4", "year and month keys");
    equal(field(a.find("rich"), "title"),
          "AT&amp;T <b>bold</b> &amp; &lt;script&gt;x&lt;/script&gt; &amp; \"q\" <i>open</i>",
          "CSL markup kept, other markup escaped, open tags closed");
    equal(field(a.find("rich"), "container-title"),
          "x&lt;/i&gt; &lt;y&gt; <span style=\"font-variant:small-caps;\">sc</span> <i>e</i>",
          "a stray closing tag is text; small caps and em normalised");
    const Item* people = a.find("names");
    equal(showNames(people, "author"), "Jane|||Doe|; Vincent|||van Gogh|; Ludwig|van||Beethoven|",
          "a name as a string, parse-names false, a particle after the given name, an empty name dropped");
    const std::vector<Name>* editors = people ? people->namesOf("editor") : nullptr;
    check(editors && editors->size() == 1 && (*editors)[0].commaSuffix, "comma-suffix");
    check(people && !people->namesOf("categories"), "only name variables hold names");
    const Item* alias = a.find("alias");
    equal(field(alias, "title-short"), "Short", "shortTitle");
    equal(field(alias, "container-title-short"), "J.", "journalAbbreviation");
    equal(field(alias, "event-title"), "New name", "event-title wins over event");
    check(a.find("strange") && a.find("strange")->type == "document", "an unknown type is a document");

    // Mistakes in the JSON: what came before is kept
    Library b = cslJson("[\n  {\"id\": \"kept\", \"type\": \"book\"},\n  {\"id\": \"cut\" \"type\": \"book\"}\n]");
    equal(diagnostics(b), "test.json:3: not valid JSON: expected ',' or '}'", "a JSON mistake with its line");
    check(b.find("kept") && !b.find("cut"), "the items before a JSON mistake are read");
    Library c = cslJson("[{\"id\": \"x\", \"type\": \"book\"}]\n]");
    equal(diagnostics(c), "test.json:2: text after the references", "text after the array");
    Library d = cslJson("\"just a string\"");
    equal(diagnostics(d), "test.json:1: CSL-JSON is an array of references", "not an array");
    Library e = cslJson("\xEF\xBB\xBF  \n");
    equal(diagnostics(e), "", "an empty file with a byte order mark");
    Library f = cslJson("[{\"id\": \"huge\", \"type\": \"book\", \"issued\": 1e300, \"volume\": 1e300}]");
    equal(showDate(f.find("huge"), "issued"), "1", "an absurd year does not overflow");
    equal(field(f.find("huge"), "volume"), "1e+300", "an absurd number is written out");
}

// --- The library ---------------------------------------------------------

void library() {
    Library lib;
    check(readByExtension("@misc{a, title = {A}}", "refs/main.bib", lib), ".bib is BibTeX");
    check(readByExtension("@misc{b, title = {B}}", "C:\\refs\\Extra.BIBTEX", lib), ".BIBTEX in any case");
    check(readByExtension("[{\"id\": \"c\", \"type\": \"book\"}]", "refs/more.json", lib), ".json is CSL-JSON");
    check(!readByExtension("- id: d", "refs/refs.yaml", lib), "other extensions are not read");
    check(!readByExtension("@misc{e}", "refs.v2/noextension", lib), "a dot in a folder is no extension");
    equal((int)lib.items.size(), 3, "three files read");
    check(lib.find("a") && lib.find("b") && lib.find("c"), "found by key");
    equal(lib.find("b") ? lib.find("b")->source : "", "C:\\refs\\Extra.BIBTEX", "an item knows its file");
    check(!lib.find("A"), "keys are exact");

    Item empty;
    empty.fields["title"] = "";
    check(!empty.field("title") && !empty.field("note") && !empty.date("issued") && !empty.namesOf("author"),
          "empty variables read as absent");
    lib.add(empty);
    check(lib.find("") != nullptr, "an item without an id can still be added");
}

// --- Robustness ----------------------------------------------------------

void robustness() {
    // Nesting far deeper than any real title cannot exhaust the stack
    std::string deep = std::string(20000, '{') + "x" + std::string(20000, '}');
    equal(latexToRich(deep), "<span class=\"nocase\">x</span>", "twenty thousand braces");
    std::string accents;
    for (int i = 0; i < 5000; ++i) accents += "\\\"{";
    accents += "o" + std::string(5000, '}');
    check(!latexToRich(accents).empty(), "five thousand accents inside each other");
    Library lib = bibtex("@misc{deep, title = " + deep + ", author = " + deep + "}");
    equal(field(lib.find("deep"), "title"), "<span class=\"nocase\">x</span>", "a deep title in an entry");
    check(parseBibtexNames(deep).size() == 1, "a deep name");

    // Random text cannot crash a reader or make it loop
    const char alphabet[] = "@{}()\"#,=%\\~-$`'[];: \n\r\tabeoAB019./\xC3\xA9";
    uint32_t seed = 20261103;
    for (int round = 0; round < 4000; ++round) {
        static const char* const starts[] = {"", "@misc{k, title = ", "@article(k, author = {",
                                             "[{\"id\": \"k\", \"issued\": {\"raw\": \""};
        std::string text = starts[round % 4];
        seed = seed * 1103515245u + 12345u;
        size_t length = (seed >> 16) % 160;
        for (size_t i = 0; i < length; ++i) {
            seed = seed * 1103515245u + 12345u;
            text += alphabet[(seed >> 16) % (sizeof alphabet - 1)];
        }
        Library random;
        readBibtex(text, "random.bib", random);
        readCslJson(text, "random.json", random);
        latexToRich(text);
        parseBibtexNames(text);
        Date d;
        parseDate(text, d);
    }
    check(true, "random text reads");
}
}  // namespace

int main() {
    bibtexSyntax();
    bibtexErrors();
    latex();
    bibtexNames();
    dates();
    typeMap();
    fieldMap();
    brokenFixture();
    cslJsonFixture();
    cslJsonVariants();
    library();
    robustness();
    if (failures) {
        std::cerr << failures << " of " << checks << " bibliography checks failed\n";
        return 1;
    }
    std::cout << "bibliography: all " << checks << " checks passed\n";
    return 0;
}
