// The CSL processor (csl.h) and the XML reader (xml.h): the five bundled
// styles against hand-checked output, and the pieces of CSL one at a time.
// Run with --dump <file> to write every bundled-style rendering out.

#include "csl.h"
#include "csl_internal.h"
#include "xml.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

int checks = 0, failures = 0;
std::ofstream dump;

// UTF-8 the expected strings need, as escapes: the sources stay ASCII
const std::string ND = "\xE2\x80\x93";        // en dash
const std::string LQ = "\xE2\x80\x9C";        // left double quotation mark
const std::string RQ = "\xE2\x80\x9D";
const std::string LSQ = "\xE2\x80\x98";       // left single quotation mark
const std::string RSQ = "\xE2\x80\x99";
const std::string AND_ZH = "\xE5\x92\x8C";    // and
const std::string IN_ZH = "\xE6\x94\xB6\xE5\x85\xA5";
const std::string YEAR_ZH = "\xE5\xB9\xB4";
const std::string MONTH_ZH = "\xE6\x9C\x88";
const std::string DAY_ZH = "\xE6\x97\xA5";
const std::string ZHANG = "\xE5\xBC\xA0";
const std::string SAN = "\xE4\xB8\x89";

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void expect(const std::string& actual, const std::string& expected, const std::string& what) {
    ++checks;
    if (actual != expected) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n  expected: " << expected << "\n  actual:   " << actual << '\n';
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string cslPath(const std::string& name) { return std::string(TINTA_CSL_DIR) + "/" + name; }

std::shared_ptr<const csl::Style> loadStyle(const std::string& name) {
    std::string error;
    auto style = csl::readStyle(readFile(cslPath(name + ".csl")), error);
    check(style != nullptr, name + " parses: " + error);
    return style;
}

// The bundled locales, en-US and zh-CN only, read once as the app keeps them
std::string bundledLocale(std::string_view tag) {
    static const std::string english = readFile(cslPath("locales-en-US.xml"));
    static const std::string chinese = readFile(cslPath("locales-zh-CN.xml"));
    if (tag == "en-US") return english;
    if (tag == "zh-CN") return chinese;
    return {};
}

bib::Name person(const char* family, const char* given) {
    bib::Name n;
    n.family = family;
    n.given = given;
    return n;
}

bib::Name organisation(const char* literal) {
    bib::Name n;
    n.literal = literal;
    return n;
}

bib::Date day(int year, int month = 0, int dayOfMonth = 0) {
    bib::Date d;
    d.from.year = year;
    d.from.month = month;
    d.from.day = dayOfMonth;
    return d;
}

bib::Date range(bib::DateParts from, bib::DateParts to) {
    bib::Date d;
    d.from = from;
    d.to = to;
    return d;
}

bib::Library library() {
    bib::Library lib;
    auto add = [&](bib::Item item) { lib.add(std::move(item)); };
    {
        bib::Item it;
        it.id = "doe2024";
        it.type = "article-journal";
        it.names["author"] = {person("Doe", "John"), person("Smith", "Ann")};
        it.fields["title"] = "Ink flow in fountain pens";
        it.fields["container-title"] = "Journal of Writing Instruments";
        it.fields["container-title-short"] = "J. Writ. Instrum.";
        it.fields["volume"] = "12";
        it.fields["issue"] = "3";
        it.fields["page"] = "45-67";
        it.fields["DOI"] = "10.1234/jwi.2024.0012";
        it.dates["issued"] = day(2024);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "three2023";
        it.type = "article-journal";
        it.names["author"] = {person("Garcia", "Maria"), person("Chen", "Wei"), person("Brown", "Peter")};
        it.fields["title"] = "Paper and its pores";
        it.fields["container-title"] = "Paper Science";
        it.fields["volume"] = "5";
        it.fields["issue"] = "1";
        it.fields["page"] = "1-10";
        it.fields["DOI"] = "10.5555/ps.5.1";
        it.dates["issued"] = day(2023, 6);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "many2022";
        it.type = "article-journal";
        static const char* const families[] = {"Adams", "Baker", "Clark", "Davis", "Evans", "Fisher", "Green", "Hall",
                                               "Irwin", "Jones", "King", "Lewis", "Moore", "Nash", "Owen", "Price",
                                               "Quinn", "Reed", "Scott", "Turner", "Vance", "Young"};
        static const char* const givens[] = {"Amy", "Ben", "Cara", "Dan", "Eve", "Finn", "Gail", "Hugo", "Iris", "Jack", "Kate",
                                             "Liam", "Mia", "Ned", "Olga", "Paul", "Quin", "Rosa", "Sam", "Tina", "Uma", "Vic"};
        for (int i = 0; i < 22; ++i) it.names["author"].push_back(person(families[i], givens[i]));
        it.fields["title"] = "A survey of pens";
        it.fields["container-title"] = "Big Science";
        it.fields["volume"] = "7";
        it.fields["page"] = "100-120";
        it.dates["issued"] = day(2022);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "book2019";
        it.type = "book";
        it.names["author"] = {person("Frank", "Ruth")};
        it.fields["title"] = "The history of ink";
        it.fields["publisher"] = "Ink Press";
        it.fields["publisher-place"] = "London";
        it.fields["edition"] = "2";
        it.dates["issued"] = day(2019);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "edited2018";
        it.type = "book";
        it.names["editor"] = {person("Editor", "Alice"), person("Compiler", "Bob")};
        it.fields["title"] = "Writing tools";
        it.fields["publisher"] = "Academic Press";
        it.fields["publisher-place"] = "New York";
        it.dates["issued"] = day(2018);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "chapter2017";
        it.type = "chapter";
        it.names["author"] = {person("Writer", "Carl")};
        it.names["editor"] = {person("Editor", "Alice")};
        it.fields["title"] = "Quills and nibs";
        it.fields["container-title"] = "A history of writing";
        it.fields["page"] = "23-45";
        it.fields["publisher"] = "Academic Press";
        it.fields["publisher-place"] = "New York";
        it.dates["issued"] = day(2017);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "conf2021";
        it.type = "paper-conference";
        it.names["author"] = {person("Lee", "Dana"), person("Park", "Evan")};
        it.fields["title"] = "Fast drying inks";
        it.fields["container-title"] = "Proceedings of the Ink Conference";
        it.fields["publisher"] = "Ink Society";
        it.fields["publisher-place"] = "Boston";
        it.fields["page"] = "12-19";
        it.dates["issued"] = day(2021, 5);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "thesis2020";
        it.type = "thesis";
        it.names["author"] = {person("Grey", "Fiona")};
        it.fields["title"] = "The chemistry of iron gall ink";
        it.fields["genre"] = "PhD thesis";
        it.fields["publisher"] = "University of Oxford";
        it.dates["issued"] = day(2020);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "report2016";
        it.type = "report";
        it.names["author"] = {person("Hale", "Gina")};
        it.fields["title"] = "Ink safety in schools";
        it.fields["genre"] = "Technical report";
        it.fields["number"] = "TR-42";
        it.fields["publisher"] = "Ink Agency";
        it.fields["publisher-place"] = "Geneva";
        it.dates["issued"] = day(2016);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "web2025";
        it.type = "webpage";
        it.names["author"] = {person("Ward", "Henry")};
        it.fields["title"] = "How pens work";
        it.fields["container-title"] = "Pen Blog";
        it.fields["URL"] = "https://example.com/pens";
        it.dates["issued"] = day(2025, 1, 10);
        it.dates["accessed"] = day(2025, 2, 3);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "data2024";
        it.type = "dataset";
        it.names["author"] = {person("Stone", "Ivy")};
        it.fields["title"] = "Ink viscosity measurements";
        it.fields["publisher"] = "Zenodo";
        it.fields["DOI"] = "10.5281/zenodo.123";
        it.dates["issued"] = day(2024);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "zhang2020";
        it.type = "article-journal";
        it.names["author"] = {person("\xE5\xBC\xA0", "\xE4\xB8\x89"), person("\xE6\x9D\x8E", "\xE5\x9B\x9B")};
        it.fields["title"] = "\xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8";
        it.fields["container-title"] = "\xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5";
        it.fields["volume"] = "12";
        it.fields["issue"] = "3";
        it.fields["page"] = "45-67";
        it.fields["language"] = "zh";
        it.dates["issued"] = day(2020);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "doeA2020";
        it.type = "article-journal";
        it.names["author"] = {person("Doe", "John")};
        it.fields["title"] = "Alpha inks";
        it.fields["container-title"] = "Ink Letters";
        it.fields["volume"] = "3";
        it.fields["page"] = "1-5";
        it.dates["issued"] = day(2020);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "doeB2020";
        it.type = "article-journal";
        it.names["author"] = {person("Doe", "John")};
        it.fields["title"] = "Beta inks";
        it.fields["container-title"] = "Ink Letters";
        it.fields["volume"] = "3";
        it.fields["page"] = "6-9";
        it.dates["issued"] = day(2020);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "smithJ2021";
        it.type = "article-journal";
        it.names["author"] = {person("Smith", "John")};
        it.fields["title"] = "Blue ink";
        it.fields["container-title"] = "Ink Letters";
        it.fields["volume"] = "4";
        it.fields["page"] = "10-12";
        it.dates["issued"] = day(2021);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "smithM2021";
        it.type = "article-journal";
        it.names["author"] = {person("Smith", "Mary")};
        it.fields["title"] = "Red ink";
        it.fields["container-title"] = "Ink Letters";
        it.fields["volume"] = "4";
        it.fields["page"] = "13-15";
        it.dates["issued"] = day(2021);
        add(std::move(it));
    }
    {
        bib::Item it;
        it.id = "who2019";
        it.type = "report";
        it.names["author"] = {organisation("World Health Organization")};
        it.fields["title"] = "Ink in schools";
        it.fields["publisher"] = "World Health Organization";
        it.fields["publisher-place"] = "Geneva";
        it.dates["issued"] = day(2019);
        add(std::move(it));
    }
    return lib;
}

cite::Item ref(const char* id, const char* locator = "", const char* label = "", bool suppressAuthor = false) {
    cite::Item item;
    item.id = id;
    item.locator = locator;
    item.label = *locator && !*label ? "page" : label;
    item.suppressAuthor = suppressAuthor;
    return item;
}

cite::Citation citation(std::vector<cite::Item> items, bool inText = false) {
    cite::Citation c;
    c.items = std::move(items);
    c.authorInText = inText;
    return c;
}

// The document every bundled style formats, in this order
enum Shape {
    Single, Locator, Multiple, SuppressAuthor, InText, InTextLocator, Many, Missing, Consecutive, SameYear,
    SameSurname, Edited, Chapter, Conference, Thesis, Report, Web, Dataset, Chinese, Organisation, ShapeCount
};

std::vector<cite::Citation> document() {
    std::vector<cite::Citation> d(ShapeCount);
    d[Single] = citation({ref("doe2024")});
    d[Locator] = citation({ref("doe2024", "4")});
    d[Multiple] = citation({ref("three2023"), ref("book2019")});
    d[SuppressAuthor] = citation({ref("doe2024", "", "", true)});
    d[InText] = citation({ref("doe2024")}, true);
    d[InTextLocator] = citation({ref("doe2024", "4")}, true);
    d[Many] = citation({ref("many2022")});
    d[Missing] = citation({ref("nobody2000")});
    d[Consecutive] = citation({ref("doe2024"), ref("three2023"), ref("book2019")});
    d[SameYear] = citation({ref("doeA2020"), ref("doeB2020")});
    d[SameSurname] = citation({ref("smithJ2021"), ref("smithM2021")});
    d[Edited] = citation({ref("edited2018")});
    d[Chapter] = citation({ref("chapter2017")});
    d[Conference] = citation({ref("conf2021")});
    d[Thesis] = citation({ref("thesis2020")});
    d[Report] = citation({ref("report2016")});
    d[Web] = citation({ref("web2025")});
    d[Dataset] = citation({ref("data2024")});
    d[Chinese] = citation({ref("zhang2020")});
    d[Organisation] = citation({ref("who2019")});
    return d;
}

std::string entryText(const csl::Entry& e, bool html) {
    std::string field = html ? csl::html(e.field) : csl::plain(e.field);
    std::string text = html ? csl::html(e.text) : csl::plain(e.text);
    return field.empty() ? text : field + " " + text;
}

const csl::Entry* entryFor(const csl::Result& r, const std::string& id) {
    for (const csl::Entry& e : r.bibliography.entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void dumpResult(const std::string& name, const csl::Result& r) {
    if (!dump.is_open()) return;
    dump << "=== " << name << " ===\n";
    for (size_t i = 0; i < r.citations.size(); ++i) {
        dump << "cite " << i << ": " << csl::plain(r.citations[i]) << "    | " << csl::html(r.citations[i]) << '\n';
    }
    for (const csl::Entry& e : r.bibliography.entries) {
        dump << "bib " << e.id << ": " << entryText(e, false) << '\n';
        dump << "    html: " << entryText(e, true) << '\n';
    }
    for (const std::string& w : r.warnings) dump << "warning: " << w << '\n';
}

// --- The bundled styles ------------------------------------------------------

// A citation of the document (shape) or a bibliography entry (shape -1, id),
// as plain text and as HTML
struct Expected {
    int shape;
    const char* id;
    const char* plain;
    const char* html;
};

struct StyleExpectations {
    const char* style;
    std::vector<Expected> expected;
};

// Checked by hand against each style's CSL and its guide (APA 7, CMOS 18
// author-date, IEEE 2023, Nature, GB/T 7714-2015)
const std::vector<StyleExpectations>& expectations() {
    static const std::vector<StyleExpectations> all = {
{"apa", {
        // citations
        {Single, nullptr, "(Doe & Smith, 2024)", "(Doe &amp; Smith, 2024)"},
        {Locator, nullptr, "(Doe & Smith, 2024, p. 4)", "(Doe &amp; Smith, 2024, p. 4)"},
        {Multiple, nullptr, "(Frank, 2019; Garcia et al., 2023)", "(Frank, 2019; Garcia et al., 2023)"},
        {SuppressAuthor, nullptr, "(2024)", "(2024)"},
        {InText, nullptr, "Doe & Smith (2024)", "Doe &amp; Smith (2024)"},
        {InTextLocator, nullptr, "Doe & Smith (2024, p. 4)", "Doe &amp; Smith (2024, p. 4)"},
        {Many, nullptr, "(Adams et al., 2022)", "(Adams et al., 2022)"},
        {Missing, nullptr, "@nobody2000?", "@nobody2000?"},
        {Consecutive, nullptr, "(Doe & Smith, 2024; Frank, 2019; Garcia et al., 2023)", "(Doe &amp; Smith, 2024; Frank, 2019; Garcia et al., 2023)"},
        {SameYear, nullptr, "(Doe, 2020a, 2020b)", "(Doe, 2020a, 2020b)"},
        {SameSurname, nullptr, "(J. Smith, 2021; M. Smith, 2021)", "(J. Smith, 2021; M. Smith, 2021)"},
        {Edited, nullptr, "(Editor & Compiler, 2018)", "(Editor &amp; Compiler, 2018)"},
        {Chapter, nullptr, "(Writer, 2017)", "(Writer, 2017)"},
        {Conference, nullptr, "(Lee & Park, 2021)", "(Lee &amp; Park, 2021)"},
        {Thesis, nullptr, "(Grey, 2020)", "(Grey, 2020)"},
        {Report, nullptr, "(Hale, 2016)", "(Hale, 2016)"},
        {Web, nullptr, "(Ward, 2025)", "(Ward, 2025)"},
        {Dataset, nullptr, "(Stone, 2024)", "(Stone, 2024)"},
        {Chinese, nullptr, "(\xE5\xBC\xA0 & \xE6\x9D\x8E, 2020)", "(\xE5\xBC\xA0 &amp; \xE6\x9D\x8E, 2020)"},
        {Organisation, nullptr, "(World Health Organization, 2019)", "(World Health Organization, 2019)"},
        {-1, "many2022", "Adams, A., Baker, B., Clark, C., Davis, D., Evans, E., Fisher, F., Green, G., Hall, H., Irwin, I., Jones, J., King, K., Lewis, L., Moore, M., Nash, N., Owen, O., Price, P., Quinn, Q., Reed, R., Scott, S., \xE2\x80\xA6 Young, V. (2022). A survey of pens. Big Science, 7, 100\xE2\x80\x93" "120.", "Adams, A., Baker, B., Clark, C., Davis, D., Evans, E., Fisher, F., Green, G., Hall, H., Irwin, I., Jones, J., King, K., Lewis, L., Moore, M., Nash, N., Owen, O., Price, P., Quinn, Q., Reed, R., Scott, S., \xE2\x80\xA6 Young, V. (2022). A survey of pens. <i>Big Science</i>, <i>7</i>, 100\xE2\x80\x93" "120."},
        {-1, "doeA2020", "Doe, J. (2020a). Alpha inks. Ink Letters, 3, 1\xE2\x80\x93" "5.", "Doe, J. (2020a). Alpha inks. <i>Ink Letters</i>, <i>3</i>, 1\xE2\x80\x93" "5."},
        {-1, "doeB2020", "Doe, J. (2020b). Beta inks. Ink Letters, 3, 6\xE2\x80\x93" "9.", "Doe, J. (2020b). Beta inks. <i>Ink Letters</i>, <i>3</i>, 6\xE2\x80\x93" "9."},
        {-1, "doe2024", "Doe, J., & Smith, A. (2024). Ink flow in fountain pens. Journal of Writing Instruments, 12(3), 45\xE2\x80\x93" "67. https://doi.org/10.1234/jwi.2024.0012", "Doe, J., &amp; Smith, A. (2024). Ink flow in fountain pens. <i>Journal of Writing Instruments</i>, <i>12</i>(3), 45\xE2\x80\x93" "67. <a href=\"https://doi.org/10.1234/jwi.2024.0012\">https://doi.org/10.1234/jwi.2024.0012</a>"},
        {-1, "edited2018", "Editor, A., & Compiler, B. (Eds.). (2018). Writing tools. Academic Press.", "Editor, A., &amp; Compiler, B. (Eds.). (2018). <i>Writing tools</i>. Academic Press."},
        {-1, "book2019", "Frank, R. (2019). The history of ink (2nd ed.). Ink Press.", "Frank, R. (2019). <i>The history of ink</i> (2nd ed.). Ink Press."},
        {-1, "three2023", "Garcia, M., Chen, W., & Brown, P. (2023). Paper and its pores. Paper Science, 5(1), 1\xE2\x80\x93" "10. https://doi.org/10.5555/ps.5.1", "Garcia, M., Chen, W., &amp; Brown, P. (2023). Paper and its pores. <i>Paper Science</i>, <i>5</i>(1), 1\xE2\x80\x93" "10. <a href=\"https://doi.org/10.5555/ps.5.1\">https://doi.org/10.5555/ps.5.1</a>"},
        {-1, "thesis2020", "Grey, F. (2020). The chemistry of iron gall ink [PhD thesis]. University of Oxford.", "Grey, F. (2020). <i>The chemistry of iron gall ink</i> [PhD thesis]. University of Oxford."},
        {-1, "report2016", "Hale, G. (2016). Ink safety in schools (Technical Report TR-42). Ink Agency.", "Hale, G. (2016). <i>Ink safety in schools</i> (Technical Report TR-42). Ink Agency."},
        {-1, "conf2021", "Lee, D., & Park, E. (2021). Fast drying inks. Proceedings of the Ink Conference, 12\xE2\x80\x93" "19.", "Lee, D., &amp; Park, E. (2021). Fast drying inks. <i>Proceedings of the Ink Conference</i>, 12\xE2\x80\x93" "19."},
        {-1, "smithJ2021", "Smith, J. (2021). Blue ink. Ink Letters, 4, 10\xE2\x80\x93" "12.", "Smith, J. (2021). Blue ink. <i>Ink Letters</i>, <i>4</i>, 10\xE2\x80\x93" "12."},
        {-1, "smithM2021", "Smith, M. (2021). Red ink. Ink Letters, 4, 13\xE2\x80\x93" "15.", "Smith, M. (2021). Red ink. <i>Ink Letters</i>, <i>4</i>, 13\xE2\x80\x93" "15."},
        {-1, "data2024", "Stone, I. (2024). Ink viscosity measurements [Data set]. Zenodo. https://doi.org/10.5281/zenodo.123", "Stone, I. (2024). <i>Ink viscosity measurements</i> [Data set]. Zenodo. <a href=\"https://doi.org/10.5281/zenodo.123\">https://doi.org/10.5281/zenodo.123</a>"},
        {-1, "web2025", "Ward, H. (2025, January 10). How pens work. Pen Blog. https://example.com/pens", "Ward, H. (2025, January 10). <i>How pens work</i>. Pen Blog. <a href=\"https://example.com/pens\">https://example.com/pens</a>"},
        {-1, "who2019", "World Health Organization. (2019). Ink in schools. World Health Organization.", "World Health Organization. (2019). <i>Ink in schools</i>. World Health Organization."},
        {-1, "chapter2017", "Writer, C. (2017). Quills and nibs. In A. Editor (Ed.), A history of writing (pp. 23\xE2\x80\x93" "45). Academic Press.", "Writer, C. (2017). Quills and nibs. In A. Editor (Ed.), <i>A history of writing</i> (pp. 23\xE2\x80\x93" "45). Academic Press."},
        {-1, "zhang2020", "\xE5\xBC\xA0\xE4\xB8\x89, & \xE6\x9D\x8E\xE5\x9B\x9B. (2020). \xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8. \xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5, 12(3), 45\xE2\x80\x93" "67.", "\xE5\xBC\xA0\xE4\xB8\x89, &amp; \xE6\x9D\x8E\xE5\x9B\x9B. (2020). \xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8. <i>\xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5</i>, <i>12</i>(3), 45\xE2\x80\x93" "67."},
    }},
    {"chicago-author-date", {
        // citations
        {Single, nullptr, "(Doe and Smith 2024)", "(Doe and Smith 2024)"},
        {Locator, nullptr, "(Doe and Smith 2024, 4)", "(Doe and Smith 2024, 4)"},
        {Multiple, nullptr, "(Garcia et al. 2023; Frank 2019)", "(Garcia et al. 2023; Frank 2019)"},
        {SuppressAuthor, nullptr, "(2024)", "(2024)"},
        {InText, nullptr, "Doe and Smith (2024)", "Doe and Smith (2024)"},
        {InTextLocator, nullptr, "Doe and Smith (2024, 4)", "Doe and Smith (2024, 4)"},
        {Many, nullptr, "(Adams et al. 2022)", "(Adams et al. 2022)"},
        {Missing, nullptr, "@nobody2000?", "@nobody2000?"},
        {Consecutive, nullptr, "(Doe and Smith 2024; Garcia et al. 2023; Frank 2019)", "(Doe and Smith 2024; Garcia et al. 2023; Frank 2019)"},
        {SameYear, nullptr, "(Doe 2020a, 2020b)", "(Doe 2020a, 2020b)"},
        {SameSurname, nullptr, "(J. Smith 2021; M. Smith 2021)", "(J. Smith 2021; M. Smith 2021)"},
        {Edited, nullptr, "(Editor and Compiler 2018)", "(Editor and Compiler 2018)"},
        {Chapter, nullptr, "(Writer 2017)", "(Writer 2017)"},
        {Conference, nullptr, "(Lee and Park 2021)", "(Lee and Park 2021)"},
        {Thesis, nullptr, "(Grey 2020)", "(Grey 2020)"},
        {Report, nullptr, "(Hale 2016)", "(Hale 2016)"},
        {Web, nullptr, "(Ward 2025)", "(Ward 2025)"},
        {Dataset, nullptr, "(Stone 2024)", "(Stone 2024)"},
        {Chinese, nullptr, "(\xE5\xBC\xA0 and \xE6\x9D\x8E 2020)", "(\xE5\xBC\xA0 and \xE6\x9D\x8E 2020)"},
        {Organisation, nullptr, "(World Health Organization 2019)", "(World Health Organization 2019)"},
        {-1, "many2022", "Adams, Amy, Ben Baker, Cara Clark, et al. 2022. \xE2\x80\x9C" "A Survey of Pens.\xE2\x80\x9D Big Science 7: 100\xE2\x80\x93" "120.", "Adams, Amy, Ben Baker, Cara Clark, et al. 2022. \xE2\x80\x9C" "A Survey of Pens.\xE2\x80\x9D <i>Big Science</i> 7: 100\xE2\x80\x93" "120."},
        {-1, "doeA2020", "Doe, John. 2020a. \xE2\x80\x9C" "Alpha Inks.\xE2\x80\x9D Ink Letters 3: 1\xE2\x80\x93" "5.", "Doe, John. 2020a. \xE2\x80\x9C" "Alpha Inks.\xE2\x80\x9D <i>Ink Letters</i> 3: 1\xE2\x80\x93" "5."},
        {-1, "doeB2020", "Doe, John. 2020b. \xE2\x80\x9C" "Beta Inks.\xE2\x80\x9D Ink Letters 3: 6\xE2\x80\x93" "9.", "Doe, John. 2020b. \xE2\x80\x9C" "Beta Inks.\xE2\x80\x9D <i>Ink Letters</i> 3: 6\xE2\x80\x93" "9."},
        {-1, "doe2024", "Doe, John, and Ann Smith. 2024. \xE2\x80\x9CInk Flow in Fountain Pens.\xE2\x80\x9D Journal of Writing Instruments 12 (3): 45\xE2\x80\x93" "67. https://doi.org/10.1234/jwi.2024.0012.", "Doe, John, and Ann Smith. 2024. \xE2\x80\x9CInk Flow in Fountain Pens.\xE2\x80\x9D <i>Journal of Writing Instruments</i> 12 (3): 45\xE2\x80\x93" "67. <a href=\"https://doi.org/10.1234/jwi.2024.0012\">https://doi.org/10.1234/jwi.2024.0012</a>."},
        {-1, "edited2018", "Editor, Alice, and Bob Compiler, eds. 2018. Writing Tools. Academic Press.", "Editor, Alice, and Bob Compiler, eds. 2018. <i>Writing Tools</i>. Academic Press."},
        {-1, "book2019", "Frank, Ruth. 2019. The History of Ink. 2nd ed. Ink Press.", "Frank, Ruth. 2019. <i>The History of Ink</i>. 2nd ed. Ink Press."},
        {-1, "three2023", "Garcia, Maria, Wei Chen, and Peter Brown. 2023. \xE2\x80\x9CPaper and Its Pores.\xE2\x80\x9D Paper Science 5 (1): 1\xE2\x80\x93" "10. https://doi.org/10.5555/ps.5.1.", "Garcia, Maria, Wei Chen, and Peter Brown. 2023. \xE2\x80\x9CPaper and Its Pores.\xE2\x80\x9D <i>Paper Science</i> 5 (1): 1\xE2\x80\x93" "10. <a href=\"https://doi.org/10.5555/ps.5.1\">https://doi.org/10.5555/ps.5.1</a>."},
        {-1, "thesis2020", "Grey, Fiona. 2020. \xE2\x80\x9CThe Chemistry of Iron Gall Ink.\xE2\x80\x9D PhD thesis, University of Oxford.", "Grey, Fiona. 2020. \xE2\x80\x9CThe Chemistry of Iron Gall Ink.\xE2\x80\x9D PhD thesis, University of Oxford."},
        {-1, "report2016", "Hale, Gina. 2016. Ink Safety in Schools. Technical Report TR-42. Ink Agency.", "Hale, Gina. 2016. <i>Ink Safety in Schools</i>. Technical Report TR-42. Ink Agency."},
        {-1, "conf2021", "Lee, Dana, and Evan Park. 2021. \xE2\x80\x9C" "Fast Drying Inks.\xE2\x80\x9D Proceedings of the Ink Conference (Boston), May, 12\xE2\x80\x93" "19.", "Lee, Dana, and Evan Park. 2021. \xE2\x80\x9C" "Fast Drying Inks.\xE2\x80\x9D <i>Proceedings of the Ink Conference</i> (Boston), May, 12\xE2\x80\x93" "19."},
        {-1, "smithJ2021", "Smith, John. 2021. \xE2\x80\x9C" "Blue Ink.\xE2\x80\x9D Ink Letters 4: 10\xE2\x80\x93" "12.", "Smith, John. 2021. \xE2\x80\x9C" "Blue Ink.\xE2\x80\x9D <i>Ink Letters</i> 4: 10\xE2\x80\x93" "12."},
        {-1, "smithM2021", "Smith, Mary. 2021. \xE2\x80\x9CRed Ink.\xE2\x80\x9D Ink Letters 4: 13\xE2\x80\x93" "15.", "Smith, Mary. 2021. \xE2\x80\x9CRed Ink.\xE2\x80\x9D <i>Ink Letters</i> 4: 13\xE2\x80\x93" "15."},
        {-1, "data2024", "Stone, Ivy. 2024. \xE2\x80\x9CInk Viscosity Measurements.\xE2\x80\x9D Zenodo. https://doi.org/10.5281/zenodo.123.", "Stone, Ivy. 2024. \xE2\x80\x9CInk Viscosity Measurements.\xE2\x80\x9D Zenodo. <a href=\"https://doi.org/10.5281/zenodo.123\">https://doi.org/10.5281/zenodo.123</a>."},
        {-1, "web2025", "Ward, Henry. 2025. \xE2\x80\x9CHow Pens Work.\xE2\x80\x9D Pen Blog, January 10. https://example.com/pens.", "Ward, Henry. 2025. \xE2\x80\x9CHow Pens Work.\xE2\x80\x9D Pen Blog, January 10. <a href=\"https://example.com/pens\">https://example.com/pens</a>."},
        {-1, "who2019", "World Health Organization. 2019. Ink in Schools. World Health Organization.", "World Health Organization. 2019. <i>Ink in Schools</i>. World Health Organization."},
        {-1, "chapter2017", "Writer, Carl. 2017. \xE2\x80\x9CQuills and Nibs.\xE2\x80\x9D In A History of Writing, edited by Alice Editor. Academic Press.", "Writer, Carl. 2017. \xE2\x80\x9CQuills and Nibs.\xE2\x80\x9D In <i>A History of Writing</i>, edited by Alice Editor. Academic Press."},
        {-1, "zhang2020", "\xE5\xBC\xA0\xE4\xB8\x89, and \xE6\x9D\x8E\xE5\x9B\x9B. 2020. \xE2\x80\x9C\xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8.\xE2\x80\x9D \xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5 12 (3): 45\xE2\x80\x93" "67.", "\xE5\xBC\xA0\xE4\xB8\x89, and \xE6\x9D\x8E\xE5\x9B\x9B. 2020. \xE2\x80\x9C\xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8.\xE2\x80\x9D <i>\xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5</i> 12 (3): 45\xE2\x80\x93" "67."},
    }},
    {"ieee", {
        // citations
        {Single, nullptr, "[1]", "[1]"},
        {Locator, nullptr, "[1, p. 4]", "[1, p. 4]"},
        {Multiple, nullptr, "[2], [3]", "[2], [3]"},
        {SuppressAuthor, nullptr, "[1]", "[1]"},
        {InText, nullptr, "Doe and Smith [1]", "Doe and Smith [1]"},
        {InTextLocator, nullptr, "Doe and Smith [1, p. 4]", "Doe and Smith [1, p. 4]"},
        {Many, nullptr, "[4]", "[4]"},
        {Missing, nullptr, "@nobody2000?", "@nobody2000?"},
        {Consecutive, nullptr, "[1], [2], [3]", "[1], [2], [3]"},
        {SameYear, nullptr, "[5], [6]", "[5], [6]"},
        {SameSurname, nullptr, "[7], [8]", "[7], [8]"},
        {Edited, nullptr, "[9]", "[9]"},
        {Chapter, nullptr, "[10]", "[10]"},
        {Conference, nullptr, "[11]", "[11]"},
        {Thesis, nullptr, "[12]", "[12]"},
        {Report, nullptr, "[13]", "[13]"},
        {Web, nullptr, "[14]", "[14]"},
        {Dataset, nullptr, "[15]", "[15]"},
        {Chinese, nullptr, "[16]", "[16]"},
        {Organisation, nullptr, "[17]", "[17]"},
        {-1, "doe2024", "[1] J. Doe and A. Smith, \xE2\x80\x9CInk flow in fountain pens,\xE2\x80\x9D J. Writ. Instrum., vol. 12, no. 3, pp. 45\xE2\x80\x93" "67, 2024, doi: 10.1234/jwi.2024.0012.", "[1] J. Doe and A. Smith, \xE2\x80\x9CInk flow in fountain pens,\xE2\x80\x9D <i>J. Writ. Instrum.</i>, vol. 12, no. 3, pp. 45\xE2\x80\x93" "67, 2024, doi: <a href=\"https://doi.org/10.1234/jwi.2024.0012\">10.1234/jwi.2024.0012</a>."},
        {-1, "three2023", "[2] M. Garcia, W. Chen, and P. Brown, \xE2\x80\x9CPaper and its pores,\xE2\x80\x9D Paper Science, vol. 5, no. 1, pp. 1\xE2\x80\x93" "10, Jun. 2023, doi: 10.5555/ps.5.1.", "[2] M. Garcia, W. Chen, and P. Brown, \xE2\x80\x9CPaper and its pores,\xE2\x80\x9D <i>Paper Science</i>, vol. 5, no. 1, pp. 1\xE2\x80\x93" "10, Jun. 2023, doi: <a href=\"https://doi.org/10.5555/ps.5.1\">10.5555/ps.5.1</a>."},
        {-1, "book2019", "[3] R. Frank, The history of ink, 2nd ed. London: Ink Press, 2019.", "[3] R. Frank, <i>The history of ink</i>, 2nd ed. London: Ink Press, 2019."},
        {-1, "many2022", "[4] A. Adams et al., \xE2\x80\x9C" "A survey of pens,\xE2\x80\x9D Big Science, vol. 7, pp. 100\xE2\x80\x93" "120, 2022.", "[4] A. Adams <i>et al.</i>, \xE2\x80\x9C" "A survey of pens,\xE2\x80\x9D <i>Big Science</i>, vol. 7, pp. 100\xE2\x80\x93" "120, 2022."},
        {-1, "doeA2020", "[5] J. Doe, \xE2\x80\x9C" "Alpha inks,\xE2\x80\x9D Ink Letters, vol. 3, pp. 1\xE2\x80\x93" "5, 2020.", "[5] J. Doe, \xE2\x80\x9C" "Alpha inks,\xE2\x80\x9D <i>Ink Letters</i>, vol. 3, pp. 1\xE2\x80\x93" "5, 2020."},
        {-1, "doeB2020", "[6] J. Doe, \xE2\x80\x9C" "Beta inks,\xE2\x80\x9D Ink Letters, vol. 3, pp. 6\xE2\x80\x93" "9, 2020.", "[6] J. Doe, \xE2\x80\x9C" "Beta inks,\xE2\x80\x9D <i>Ink Letters</i>, vol. 3, pp. 6\xE2\x80\x93" "9, 2020."},
        {-1, "smithJ2021", "[7] J. Smith, \xE2\x80\x9C" "Blue ink,\xE2\x80\x9D Ink Letters, vol. 4, pp. 10\xE2\x80\x93" "12, 2021.", "[7] J. Smith, \xE2\x80\x9C" "Blue ink,\xE2\x80\x9D <i>Ink Letters</i>, vol. 4, pp. 10\xE2\x80\x93" "12, 2021."},
        {-1, "smithM2021", "[8] M. Smith, \xE2\x80\x9CRed ink,\xE2\x80\x9D Ink Letters, vol. 4, pp. 13\xE2\x80\x93" "15, 2021.", "[8] M. Smith, \xE2\x80\x9CRed ink,\xE2\x80\x9D <i>Ink Letters</i>, vol. 4, pp. 13\xE2\x80\x93" "15, 2021."},
        {-1, "edited2018", "[9] A. Editor and B. Compiler, Eds., Writing tools. New York: Academic Press, 2018.", "[9] A. Editor and B. Compiler, Eds., <i>Writing tools</i>. New York: Academic Press, 2018."},
        {-1, "chapter2017", "[10] C. Writer, \xE2\x80\x9CQuills and nibs,\xE2\x80\x9D in A history of writing, A. Editor, Ed., New York: Academic Press, 2017, pp. 23\xE2\x80\x93" "45.", "[10] C. Writer, \xE2\x80\x9CQuills and nibs,\xE2\x80\x9D in <i>A history of writing</i>, A. Editor, Ed., New York: Academic Press, 2017, pp. 23\xE2\x80\x93" "45."},
        {-1, "conf2021", "[11] D. Lee and E. Park, \xE2\x80\x9C" "Fast drying inks,\xE2\x80\x9D in Proceedings of the Ink Conference, Boston: Ink Society, May 2021, pp. 12\xE2\x80\x93" "19.", "[11] D. Lee and E. Park, \xE2\x80\x9C" "Fast drying inks,\xE2\x80\x9D in <i>Proceedings of the Ink Conference</i>, Boston: Ink Society, May 2021, pp. 12\xE2\x80\x93" "19."},
        {-1, "thesis2020", "[12] F. Grey, \xE2\x80\x9CThe chemistry of iron gall ink,\xE2\x80\x9D PhD thesis, University of Oxford, 2020.", "[12] F. Grey, \xE2\x80\x9CThe chemistry of iron gall ink,\xE2\x80\x9D PhD thesis, University of Oxford, 2020."},
        {-1, "report2016", "[13] G. Hale, \xE2\x80\x9CInk safety in schools,\xE2\x80\x9D Ink Agency, Geneva, Technical report TR-42, 2016.", "[13] G. Hale, \xE2\x80\x9CInk safety in schools,\xE2\x80\x9D Ink Agency, Geneva, Technical report TR-42, 2016."},
        {-1, "web2025", "[14] H. Ward, \xE2\x80\x9CHow pens work,\xE2\x80\x9D Pen Blog. Accessed: Feb. 03, 2025. [Online]. Available: https://example.com/pens", "[14] H. Ward, \xE2\x80\x9CHow pens work,\xE2\x80\x9D Pen Blog. Accessed: Feb. 03, 2025. [Online]. Available: <a href=\"https://example.com/pens\">https://example.com/pens</a>"},
        {-1, "data2024", "[15] I. Stone, \xE2\x80\x9CInk viscosity measurements.\xE2\x80\x9D Zenodo, 2024. doi: 10.5281/zenodo.123.", "[15] I. Stone, \xE2\x80\x9CInk viscosity measurements.\xE2\x80\x9D Zenodo, 2024. doi: <a href=\"https://doi.org/10.5281/zenodo.123\">10.5281/zenodo.123</a>."},
        {-1, "zhang2020", "[16] \xE5\xBC\xA0\xE4\xB8\x89 and \xE6\x9D\x8E\xE5\x9B\x9B, \xE2\x80\x9C\xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8,\xE2\x80\x9D \xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5, vol. 12, no. 3, pp. 45\xE2\x80\x93" "67, 2020.", "[16] \xE5\xBC\xA0\xE4\xB8\x89 and \xE6\x9D\x8E\xE5\x9B\x9B, \xE2\x80\x9C\xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8,\xE2\x80\x9D <i>\xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5</i>, vol. 12, no. 3, pp. 45\xE2\x80\x93" "67, 2020."},
        {-1, "who2019", "[17] World Health Organization, \xE2\x80\x9CInk in schools,\xE2\x80\x9D World Health Organization, Geneva, 2019.", "[17] World Health Organization, \xE2\x80\x9CInk in schools,\xE2\x80\x9D World Health Organization, Geneva, 2019."},
    }},
    {"nature", {
        // citations
        {Single, nullptr, "1", "<sup>1</sup>"},
        {Locator, nullptr, "1", "<sup>1</sup>"},
        {Multiple, nullptr, "2,3", "<sup>2,3</sup>"},
        {SuppressAuthor, nullptr, "1", "<sup>1</sup>"},
        {InText, nullptr, "Doe and Smith 1", "Doe and Smith <sup>1</sup>"},
        {InTextLocator, nullptr, "Doe and Smith 1", "Doe and Smith <sup>1</sup>"},
        {Many, nullptr, "4", "<sup>4</sup>"},
        {Missing, nullptr, "@nobody2000?", "@nobody2000?"},
        {Consecutive, nullptr, "1\xE2\x80\x93" "3", "<sup>1\xE2\x80\x93" "3</sup>"},
        {SameYear, nullptr, "5,6", "<sup>5,6</sup>"},
        {SameSurname, nullptr, "7,8", "<sup>7,8</sup>"},
        {Edited, nullptr, "9", "<sup>9</sup>"},
        {Chapter, nullptr, "10", "<sup>10</sup>"},
        {Conference, nullptr, "11", "<sup>11</sup>"},
        {Thesis, nullptr, "12", "<sup>12</sup>"},
        {Report, nullptr, "13", "<sup>13</sup>"},
        {Web, nullptr, "14", "<sup>14</sup>"},
        {Dataset, nullptr, "15", "<sup>15</sup>"},
        {Chinese, nullptr, "16", "<sup>16</sup>"},
        {Organisation, nullptr, "17", "<sup>17</sup>"},
        {-1, "doe2024", "1. Doe, J. & Smith, A. Ink flow in fountain pens. J. Writ. Instrum. 12, 45\xE2\x80\x93" "67 (2024).", "1. Doe, J. &amp; Smith, A. Ink flow in fountain pens. <i>J. Writ. Instrum.</i> <b>12</b>, 45\xE2\x80\x93" "67 (2024)."},
        {-1, "three2023", "2. Garcia, M., Chen, W. & Brown, P. Paper and its pores. Paper Science 5, 1\xE2\x80\x93" "10 (2023).", "2. Garcia, M., Chen, W. &amp; Brown, P. Paper and its pores. <i>Paper Science</i> <b>5</b>, 1\xE2\x80\x93" "10 (2023)."},
        {-1, "book2019", "3. Frank, R. The History of Ink. (Ink Press, London, 2019).", "3. Frank, R. <i>The History of Ink</i>. (Ink Press, London, 2019)."},
        {-1, "many2022", "4. Adams, A. et al. A survey of pens. Big Science 7, 100\xE2\x80\x93" "120 (2022).", "4. Adams, A. <i>et al.</i> A survey of pens. <i>Big Science</i> <b>7</b>, 100\xE2\x80\x93" "120 (2022)."},
        {-1, "doeA2020", "5. Doe, J. Alpha inks. Ink Letters 3, 1\xE2\x80\x93" "5 (2020).", "5. Doe, J. Alpha inks. <i>Ink Letters</i> <b>3</b>, 1\xE2\x80\x93" "5 (2020)."},
        {-1, "doeB2020", "6. Doe, J. Beta inks. Ink Letters 3, 6\xE2\x80\x93" "9 (2020).", "6. Doe, J. Beta inks. <i>Ink Letters</i> <b>3</b>, 6\xE2\x80\x93" "9 (2020)."},
        {-1, "smithJ2021", "7. Smith, J. Blue ink. Ink Letters 4, 10\xE2\x80\x93" "12 (2021).", "7. Smith, J. Blue ink. <i>Ink Letters</i> <b>4</b>, 10\xE2\x80\x93" "12 (2021)."},
        {-1, "smithM2021", "8. Smith, M. Red ink. Ink Letters 4, 13\xE2\x80\x93" "15 (2021).", "8. Smith, M. Red ink. <i>Ink Letters</i> <b>4</b>, 13\xE2\x80\x93" "15 (2021)."},
        {-1, "edited2018", "9. Writing Tools. (Academic Press, New York, 2018).", "9. <i>Writing Tools</i>. (Academic Press, New York, 2018)."},
        {-1, "chapter2017", "10. Writer, C. Quills and nibs. in A history of writing (ed. Editor, A.) 23\xE2\x80\x93" "45 (Academic Press, New York, 2017).", "10. Writer, C. Quills and nibs. in <i>A history of writing</i> (ed. Editor, A.) 23\xE2\x80\x93" "45 (Academic Press, New York, 2017)."},
        {-1, "conf2021", "11. Lee, D. & Park, E. Fast drying inks. in Proceedings of the Ink Conference 12\xE2\x80\x93" "19 (Ink Society, Boston, 2021).", "11. Lee, D. &amp; Park, E. Fast drying inks. in <i>Proceedings of the Ink Conference</i> 12\xE2\x80\x93" "19 (Ink Society, Boston, 2021)."},
        {-1, "thesis2020", "12. Grey, F. The chemistry of iron gall ink. (University of Oxford, 2020).", "12. Grey, F. The chemistry of iron gall ink. (University of Oxford, 2020)."},
        {-1, "report2016", "13. Hale, G. Ink Safety in Schools. (2016).", "13. Hale, G. <i>Ink Safety in Schools</i>. (2016)."},
        {-1, "web2025", "14. Ward, H. How pens work. Pen Blog https://example.com/pens (2025).", "14. Ward, H. How pens work. <i>Pen Blog</i> <a href=\"https://example.com/pens\">https://example.com/pens</a> (2025)."},
        {-1, "data2024", "15. Stone, I. Ink viscosity measurements. Zenodo https://doi.org/10.5281/zenodo.123 (2024).", "15. Stone, I. Ink viscosity measurements. Zenodo <a href=\"https://doi.org/10.5281/zenodo.123\">https://doi.org/10.5281/zenodo.123</a> (2024)."},
        {-1, "zhang2020", "16. \xE5\xBC\xA0\xE4\xB8\x89 & \xE6\x9D\x8E\xE5\x9B\x9B. \xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8. \xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5 12, 45\xE2\x80\x93" "67 (2020).", "16. \xE5\xBC\xA0\xE4\xB8\x89 &amp; \xE6\x9D\x8E\xE5\x9B\x9B. \xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8. <i>\xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5</i> <b>12</b>, 45\xE2\x80\x93" "67 (2020)."},
        {-1, "who2019", "17. World Health Organization. Ink in Schools. (2019).", "17. World Health Organization. <i>Ink in Schools</i>. (2019)."},
    }},
    {"china-national-standard-gb-t-7714-2015-numeric", {
        // citations
        {Single, nullptr, "[1]", "<sup>[1]</sup>"},
        {Locator, nullptr, "[1]", "<sup>[1]</sup>"},
        {Multiple, nullptr, "[2,3]", "<sup>[2,3]</sup>"},
        {SuppressAuthor, nullptr, "[1]", "<sup>[1]</sup>"},
        {InText, nullptr, "Doe \xE5\x92\x8C Smith [1]", "Doe \xE5\x92\x8C Smith <sup>[1]</sup>"},
        {InTextLocator, nullptr, "Doe \xE5\x92\x8C Smith [1]", "Doe \xE5\x92\x8C Smith <sup>[1]</sup>"},
        {Many, nullptr, "[4]", "<sup>[4]</sup>"},
        {Missing, nullptr, "@nobody2000?", "@nobody2000?"},
        {Consecutive, nullptr, "[1\xE2\x80\x93" "3]", "<sup>[1\xE2\x80\x93" "3]</sup>"},
        {SameYear, nullptr, "[5,6]", "<sup>[5,6]</sup>"},
        {SameSurname, nullptr, "[7,8]", "<sup>[7,8]</sup>"},
        {Edited, nullptr, "[9]", "<sup>[9]</sup>"},
        {Chapter, nullptr, "[10]", "<sup>[10]</sup>"},
        {Conference, nullptr, "[11]", "<sup>[11]</sup>"},
        {Thesis, nullptr, "[12]", "<sup>[12]</sup>"},
        {Report, nullptr, "[13]", "<sup>[13]</sup>"},
        {Web, nullptr, "[14]", "<sup>[14]</sup>"},
        {Dataset, nullptr, "[15]", "<sup>[15]</sup>"},
        {Chinese, nullptr, "[16]", "<sup>[16]</sup>"},
        {Organisation, nullptr, "[17]", "<sup>[17]</sup>"},
        {-1, "doe2024", "[1] DOE J, SMITH A. Ink flow in fountain pens[J/OL]. Journal of Writing Instruments, 2024, 12(3): 45-67. DOI:10.1234/jwi.2024.0012.", "[1] DOE J, SMITH A. Ink flow in fountain pens[J/OL]. Journal of Writing Instruments, 2024, 12(3): 45-67. DOI:<a href=\"https://doi.org/10.1234/jwi.2024.0012\">10.1234/jwi.2024.0012</a>."},
        {-1, "three2023", "[2] GARCIA M, CHEN W, BROWN P. Paper and its pores[J/OL]. Paper Science, 2023, 5(1): 1-10. DOI:10.5555/ps.5.1.", "[2] GARCIA M, CHEN W, BROWN P. Paper and its pores[J/OL]. Paper Science, 2023, 5(1): 1-10. DOI:<a href=\"https://doi.org/10.5555/ps.5.1\">10.5555/ps.5.1</a>."},
        {-1, "book2019", "[3] FRANK R. The history of ink[M]. 2 \xE7\x89\x88. London: Ink Press, 2019.", "[3] FRANK R. The history of ink[M]. 2 \xE7\x89\x88. London: Ink Press, 2019."},
        {-1, "many2022", "[4] ADAMS A, BAKER B, CLARK C, \xE7\xAD\x89. A survey of pens[J]. Big Science, 2022, 7: 100-120.", "[4] ADAMS A, BAKER B, CLARK C, \xE7\xAD\x89. A survey of pens[J]. Big Science, 2022, 7: 100-120."},
        {-1, "doeA2020", "[5] DOE J. Alpha inks[J]. Ink Letters, 2020, 3: 1-5.", "[5] DOE J. Alpha inks[J]. Ink Letters, 2020, 3: 1-5."},
        {-1, "doeB2020", "[6] DOE J. Beta inks[J]. Ink Letters, 2020, 3: 6-9.", "[6] DOE J. Beta inks[J]. Ink Letters, 2020, 3: 6-9."},
        {-1, "smithJ2021", "[7] SMITH J. Blue ink[J]. Ink Letters, 2021, 4: 10-12.", "[7] SMITH J. Blue ink[J]. Ink Letters, 2021, 4: 10-12."},
        {-1, "smithM2021", "[8] SMITH M. Red ink[J]. Ink Letters, 2021, 4: 13-15.", "[8] SMITH M. Red ink[J]. Ink Letters, 2021, 4: 13-15."},
        {-1, "edited2018", "[9] EDITOR A, COMPILER B. Writing tools[M]. New York: Academic Press, 2018.", "[9] EDITOR A, COMPILER B. Writing tools[M]. New York: Academic Press, 2018."},
        {-1, "chapter2017", "[10] WRITER C. Quills and nibs[M]//EDITOR A. A history of writing. New York: Academic Press, 2017: 23-45.", "[10] WRITER C. Quills and nibs[M]//EDITOR A. A history of writing. New York: Academic Press, 2017: 23-45."},
        {-1, "conf2021", "[11] LEE D, PARK E. Fast drying inks[C]//Proceedings of the Ink Conference. Boston: Ink Society, 2021: 12-19.", "[11] LEE D, PARK E. Fast drying inks[C]//Proceedings of the Ink Conference. Boston: Ink Society, 2021: 12-19."},
        {-1, "thesis2020", "[12] GREY F. The chemistry of iron gall ink[D]. University of Oxford, 2020.", "[12] GREY F. The chemistry of iron gall ink[D]. University of Oxford, 2020."},
        {-1, "report2016", "[13] HALE G. Ink safety in schools: TR-42[R]. Geneva: Ink Agency, 2016.", "[13] HALE G. Ink safety in schools: TR-42[R]. Geneva: Ink Agency, 2016."},
        {-1, "web2025", "[14] WARD H. How pens work[EB/OL]. (2025-01-10)[2025-02-03]. https://example.com/pens.", "[14] WARD H. How pens work[EB/OL]. (2025-01-10)[2025-02-03]. <a href=\"https://example.com/pens\">https://example.com/pens</a>."},
        {-1, "data2024", "[15] STONE I. Ink viscosity measurements[DS/OL]. Zenodo, 2024. DOI:10.5281/zenodo.123.", "[15] STONE I. Ink viscosity measurements[DS/OL]. Zenodo, 2024. DOI:<a href=\"https://doi.org/10.5281/zenodo.123\">10.5281/zenodo.123</a>."},
        {-1, "zhang2020", "[16] \xE5\xBC\xA0\xE4\xB8\x89, \xE6\x9D\x8E\xE5\x9B\x9B. \xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8[J]. \xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5, 2020, 12(3): 45-67.", "[16] \xE5\xBC\xA0\xE4\xB8\x89, \xE6\x9D\x8E\xE5\x9B\x9B. \xE5\xA2\xA8\xE6\xB0\xB4\xE7\x9A\x84\xE6\xB5\x81\xE5\x8A\xA8[J]. \xE4\xB9\xA6\xE5\x86\x99\xE5\xB7\xA5\xE5\x85\xB7\xE5\xAD\xA6\xE6\x8A\xA5, 2020, 12(3): 45-67."},
        {-1, "who2019", "[17] World Health Organization. Ink in schools[R]. Geneva: World Health Organization, 2019.", "[17] World Health Organization. Ink in schools[R]. Geneva: World Health Organization, 2019."},
    }},
    };
    return all;
}

void bundledStyles() {
    for (const StyleExpectations& se : expectations()) {
        auto style = loadStyle(se.style);
        if (!style) continue;
        bib::Library lib = library();
        csl::Result r = csl::process(*style, bundledLocale, lib, document(), csl::Options());
        dumpResult(se.style, r);
        check(r.citations.size() == ShapeCount, std::string(se.style) + ": one result per citation");
        check(r.bibliography.entries.size() == 17, std::string(se.style) + ": an entry per cited item");
        size_t entry = 0;
        for (const Expected& e : se.expected) {
            std::string what = std::string(se.style) + " " + (e.id ? std::string(e.id) : "citation " + std::to_string(e.shape));
            if (e.shape >= 0) {
                if (e.shape >= (int)r.citations.size()) continue;
                expect(csl::plain(r.citations[e.shape]), e.plain, what);
                expect(csl::html(r.citations[e.shape]), e.html, what + " (html)");
            } else {
                // Entries come in the bibliography's order
                if (entry >= r.bibliography.entries.size()) continue;
                const csl::Entry& got = r.bibliography.entries[entry++];
                expect(got.id, e.id, what + " in its place");
                expect(entryText(got, false), e.plain, what);
                expect(entryText(got, true), e.html, what + " (html)");
            }
        }
        bool warned = false;
        for (const std::string& w : r.warnings) warned = warned || w == "no reference for @nobody2000";
        check(warned, std::string(se.style) + ": a missing key warns");
        check(r.citations[Missing].size() == 1 && r.citations[Missing][0].flags == csl::Missing,
              std::string(se.style) + ": a missing key is one plain run, flagged");
    }

    // Layout of the bibliographies
    auto apa = loadStyle("apa");
    auto ieee = loadStyle("ieee");
    auto nature = loadStyle("nature");
    auto gbt = loadStyle("china-national-standard-gb-t-7714-2015-numeric");
    auto chicago = loadStyle("chicago-author-date");
    if (!apa || !ieee || !nature || !gbt || !chicago) return;
    bib::Library lib = library();
    csl::Result r = csl::process(*apa, bundledLocale, lib, document(), csl::Options());
    check(r.bibliography.hangingIndent && !r.bibliography.alignField && r.bibliography.lineSpacing == 2 &&
              r.bibliography.entrySpacing == 0,
          "apa: hanging indent, double spaced");
    r = csl::process(*ieee, bundledLocale, lib, document(), csl::Options());
    check(r.bibliography.alignField && !r.bibliography.hangingIndent, "ieee: numbers in a column");
    if (const csl::Entry* e = entryFor(r, "doe2024")) {
        expect(csl::plain(e->field), "[1]", "ieee: the number is the entry's field");
        expect(csl::plain(e->text).substr(0, 19), "J. Doe and A. Smith", "ieee: the text follows the field");
    }
    r = csl::process(*nature, bundledLocale, lib, document(), csl::Options());
    check(r.bibliography.alignField && r.bibliography.lineSpacing == 2, "nature: numbers in a column");

    // Infos
    check(!csl::info(*apa).numeric && !csl::info(*apa).note && csl::info(*apa).defaultLocale.empty(), "apa: author-date");
    expect(csl::info(*apa).title, "APA Style 7th edition", "apa: title");
    expect(csl::info(*apa).id, "http://www.zotero.org/styles/apa", "apa: id");
    check(csl::info(*ieee).numeric && csl::info(*nature).numeric && csl::info(*gbt).numeric, "numeric styles");
    expect(csl::info(*nature).defaultLocale, "en-GB", "nature: default locale");
    expect(csl::info(*gbt).defaultLocale, "zh-CN", "gb/t: default locale");

    // More citation shapes
    auto cites = [&](const csl::Style& style, std::vector<cite::Citation> doc, const std::string& lang = {}) {
        csl::Options options;
        options.lang = lang;
        csl::Result result = csl::process(style, bundledLocale, lib, doc, options);
        std::vector<std::string> out;
        for (const csl::Rich& c : result.citations) out.push_back(csl::plain(c));
        return out;
    };
    cite::Item seeDoe = ref("doe2024", "4");
    seeDoe.prefix = "see";
    seeDoe.suffix = ", emphasis added";
    auto out = cites(*apa, {citation({seeDoe}), citation({ref("doe2024", "1", "chapter")}),
                            citation({ref("doe2024", "33-35")}), citation({ref("three2023")}, true),
                            citation({ref("doe2024"), ref("nobody2000")})});
    expect(out[0], "(see Doe & Smith, 2024, p. 4, emphasis added)", "apa: prefix, locator and suffix");
    expect(out[1], "(Doe & Smith, 2024, Chapter 1)", "apa: a chapter locator");
    expect(out[2], "(Doe & Smith, 2024, pp. 33" + ND + "35)", "apa: a page range locator");
    expect(out[3], "Garcia et al. (2023)", "apa: in-text et al.");
    expect(out[4], "(Doe & Smith, 2024; @nobody2000?)", "apa: a missing key among others");
    out = cites(*chicago, {citation({ref("doe2024", "1", "chapter")}), citation({ref("doe2024", "33-35")}),
                           citation({ref("doeA2020"), ref("smithJ2021"), ref("doeB2020")})});
    expect(out[0], "(Doe and Smith 2024, chap. 1)", "chicago: a chapter locator");
    expect(out[1], "(Doe and Smith 2024, 33" + ND + "35)", "chicago: a page range");
    expect(out[2], "(Doe 2020a, 2020b; Smith 2021)", "chicago: cites of one author grouped and collapsed");
    out = cites(*ieee, {citation({ref("doe2024", "1", "chapter")}), citation({ref("book2019")}, true),
                        citation({ref("doe2024", "33-35")})});
    expect(out[0], "[1, Ch. 1]", "ieee: a chapter locator");
    expect(out[1], "Frank [2]", "ieee: in-text author of a numeric style");
    expect(out[2], "[1, pp. 33" + ND + "35]", "ieee: a page range");
    out = cites(*apa, {citation({ref("doeA2020"), ref("smithJ2021"), ref("doeB2020")})});
    expect(out[0], "(Doe, 2020a, 2020b; Smith, 2021)", "apa: sorted, grouped, collapsed; one Smith cited");
    out = cites(*gbt, {citation({ref("doe2024")}, true), citation({ref("book2019"), ref("doe2024"), ref("three2023")})});
    expect(out[0], "Doe " + AND_ZH + " Smith [1]", "gb/t: in-text author in Chinese");
    expect(out[1], "[1" + ND + "3]", "gb/t: numbers sorted and collapsed");
    out = cites(*nature, {citation({ref("doe2024"), ref("three2023")}), citation({ref("book2019")}),
                          citation({ref("doe2024"), ref("book2019"), ref("three2023"), ref("many2022")})});
    expect(out[2], "1" + ND + "4", "nature: four in a row collapse");

    // No date
    {
        bib::Library undated;
        bib::Item it;
        it.id = "undated";
        it.type = "article-journal";
        it.names["author"] = {person("Doe", "John")};
        it.fields["title"] = "Undated";
        it.fields["container-title"] = "Ink Letters";
        it.fields["volume"] = "1";
        it.fields["page"] = "1-2";
        undated.add(it);
        csl::Result a = csl::process(*apa, bundledLocale, undated, {citation({ref("undated")})}, csl::Options());
        expect(a.citations.empty() ? "" : csl::plain(a.citations[0]), "(Doe, n.d.)", "apa: no date");
        expect(a.bibliography.entries.empty() ? "" : csl::html(a.bibliography.entries[0].text),
               "Doe, J. (n.d.). Undated. <i>Ink Letters</i>, <i>1</i>, 1" + ND + "2.", "apa: no date, entry");
        csl::Result c = csl::process(*chicago, bundledLocale, undated, {citation({ref("undated")})}, csl::Options());
        expect(c.citations.empty() ? "" : csl::plain(c.citations[0]), "(Doe, n.d.)", "chicago: no date");
        expect(c.bibliography.entries.empty() ? "" : csl::html(c.bibliography.entries[0].text),
               "Doe, John. n.d. " + LQ + "Undated." + RQ + " <i>Ink Letters</i> 1: 1" + ND + "2.", "chicago: no date, entry");
    }

    // nocite
    csl::Options listed;
    listed.nocite = {"book2019", "@three2023", "unknown1999"};
    r = csl::process(*apa, bundledLocale, lib, {citation({ref("doe2024")})}, listed);
    check(r.bibliography.entries.size() == 3, "nocite: listed keys join the bibliography");
    bool unknown = false;
    for (const std::string& w : r.warnings) unknown = unknown || w == "no reference for @unknown1999";
    check(unknown, "nocite: an unknown key warns");
    listed.nocite = {"*"};
    r = csl::process(*ieee, bundledLocale, lib, {citation({ref("who2019")})}, listed);
    check(r.bibliography.entries.size() == 17, "nocite *: every item");
    if (!r.bibliography.entries.empty()) {
        expect(r.bibliography.entries[0].id, "who2019", "nocite *: cited items number first");
    }
}

// --- Styles written for one test -----------------------------------------------

std::string styleXml(const std::string& attributes, const std::string& body) {
    return "<style xmlns=\"http://purl.org/net/xbiblio/csl\" class=\"in-text\" version=\"1.0\" " + attributes + ">" +
           body + "</style>";
}

std::shared_ptr<const csl::Style> inlineStyle(const std::string& attributes, const std::string& body) {
    std::string error;
    auto style = csl::readStyle(styleXml(attributes, body), error);
    check(style != nullptr, "inline style parses: " + error);
    return style;
}

// The first citation of [@x] (or of `cites`) with a citation layout, for one item
csl::Result runOne(const std::string& attributes, const std::string& body, bib::Item item,
                   const std::string& lang = "en-US", std::vector<cite::Citation> cites = {}) {
    auto style = inlineStyle(attributes, body);
    if (!style) return {};
    bib::Library lib;
    if (item.id.empty()) item.id = "x";
    lib.add(item);
    if (cites.empty()) cites = {citation({ref(item.id.c_str())})};
    csl::Options options;
    options.lang = lang;
    return csl::process(*style, bundledLocale, lib, cites, options);
}

std::string layoutOut(const std::string& layout, const bib::Item& item, const std::string& attributes = "",
                      const std::string& lang = "en-US", bool asHtml = false) {
    csl::Result r = runOne(attributes, "<citation><layout>" + layout + "</layout></citation>", item, lang);
    if (r.citations.empty()) return "<no result>";
    return asHtml ? csl::html(r.citations[0]) : csl::plain(r.citations[0]);
}

bib::Item withAuthors(std::vector<bib::Name> names, const char* variable = "author") {
    bib::Item it;
    it.type = "book";
    it.names[variable] = std::move(names);
    return it;
}

std::string names(const std::string& name, std::vector<bib::Name> authors, const std::string& attributes = "") {
    return layoutOut("<names variable=\"author\">" + name + "</names>", withAuthors(std::move(authors)), attributes);
}

// --- XML ---------------------------------------------------------------------

void xmlTests() {
    xml::Node root;
    std::string error;
    int line = 0;
    std::string text = "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                       "<!DOCTYPE style [<!ENTITY x \"y\">]>\n"
                       "<!-- a comment -->\n"
                       "<csl:style xmlns:csl=\"http://purl.org/net/xbiblio/csl\" xml:lang=\"en\" a='1' b = \"x&amp;y&lt;&gt;&quot;&apos;\">"
                       "<csl:info>t&#65;&#x4E2D;<![CDATA[<raw & stuff>]]><!-- c --><?pi data?>u</csl:info>\n"
                       "  <empty/>\n"
                       "  <group delimiter=\", \"\tprefix=\"(\n)\"/>\n"
                       "</csl:style>\n"
                       "<!-- after -->\n";
    check(xml::parse(text, root, error, line), "xml: a document parses: " + error);
    expect(root.name, "style", "xml: local names");
    expect(std::string(root.get("xml:lang")), "en", "xml: xml:lang kept whole");
    check(root.has("xmlns:csl"), "xml: namespace declarations kept as written");
    expect(std::string(root.get("a")), "1", "xml: single-quoted attribute");
    expect(std::string(root.get("b")), "x&y<>\"'", "xml: entities in attributes");
    expect(std::string(root.get("missing", "fallback")), "fallback", "xml: absent attribute");
    check(root.attributes.size() == 4 && root.attributes[0].first == "xmlns:csl" && root.attributes[3].first == "b",
          "xml: attributes in order");
    check(root.line == 4, "xml: the root's line");
    size_t elements = 0;
    for (const xml::Node& child : root.children) elements += !child.isText();
    check(elements == 3, "xml: three child elements");
    if (!root.children.empty()) {
        const xml::Node& info = root.children[0];
        expect(info.name, "info", "xml: a child's local name");
        expect(info.innerText(), "tA\xE4\xB8\xAD<raw & stuff>u", "xml: text, references, CDATA, comments and PIs");
        check(info.children.size() == 1 && info.children[0].isText(), "xml: adjacent text merges into one node");
    }
    for (const xml::Node& child : root.children) {
        if (child.name == "group") {
            expect(std::string(child.get("prefix")), "( )", "xml: white space in attributes normalised");
            check(child.line == 6, "xml: a child's line");
        }
    }
    struct Bad {
        const char* text;
        const char* error;
        int line;
    } bad[] = {
        {"<a><b></a>", "</a> closes <b>", 1},
        {"<a>\n\n<b>\n</c></a>", "</c> closes <b>", 4},
        {"<a>\n<b>", "<b> is never closed", 2},
        {"<a>&nbsp;</a>", "an unknown entity &nbsp;", 1},
        {"<a>&#0;</a>", "a character reference to no character", 1},
        {"<a x=1/>", "attribute x needs a quoted value", 1},
        {"<a x='1' x='2'/>", "attribute x given twice", 1},
        {"<a/>\n<b/>", "content after the root element", 2},
        {"", "no root element", 1},
        {"<a><!-- open", "an unclosed comment", 1},
        {"<a><![CDATA[ open", "an unclosed CDATA section", 1},
        {"<a b=\"<\"/>", "a '<' inside attribute b", 1},
    };
    std::string deep;
    for (int i = 0; i < 300; ++i) deep += "<g>";
    check(!xml::parse(deep, root, error, line) && error == "elements nested too deep", "xml: nesting is bounded");
    for (const Bad& b : bad) {
        xml::Node node;
        bool ok = xml::parse(b.text, node, error, line);
        check(!ok, std::string("xml: malformed input fails: ") + b.text);
        expect(error, b.error, std::string("xml: the error for ") + b.text);
        check(line == b.line, std::string("xml: the error's line for ") + b.text + " is " + std::to_string(line));
    }
}

// --- Reading styles and locales ------------------------------------------------

void readingTests() {
    std::string error;
    check(!csl::readStyle("<style", error) && error.find("line 1:") == 0, "a broken style says where");
    check(!csl::readStyle("<locale xmlns=\"http://purl.org/net/xbiblio/csl\"/>", error) &&
              error.find("not a CSL style") == 0,
          "a locale is not a style");
    check(!csl::readStyle(styleXml("", "<citation><layout><text macro=\"nowhere\"/></layout></citation>"), error) &&
              error.find("no macro named \"nowhere\"") != std::string::npos,
          "an unknown macro fails the style: " + error);
    check(!csl::readStyle(styleXml("", "<bibliography><layout/></bibliography>"), error) &&
              error.find("no <citation>") != std::string::npos,
          "a style needs a citation: " + error);
    check(!csl::readLocale(readFile(cslPath("apa.csl")), error) && error.find("not a CSL locale") == 0,
          "a style is not a locale");
    check(csl::readLocale(bundledLocale("zh-CN"), error) != nullptr, "the zh-CN locale reads");
    // Packed as tools/csl_pack leaves it: no comments, no info, no indentation
    std::string packed = "<?xml version=\"1.0\" encoding=\"utf-8\"?><style xmlns=\"http://purl.org/net/xbiblio/csl\" "
                         "class=\"note\" version=\"1.0\"><citation><layout><text variable=\"title\"/></layout>"
                         "</citation></style>";
    auto note = csl::readStyle(packed, error);
    check(note && csl::info(*note).note && !csl::info(*note).numeric, "a note style reads");
    if (note) {
        bib::Library lib;
        bib::Item it;
        it.id = "n";
        it.fields["title"] = "In a note";
        lib.add(it);
        csl::Result r = csl::process(*note, bundledLocale, lib, {citation({ref("n")})}, csl::Options());
        expect(r.citations.empty() ? "" : csl::plain(r.citations[0]), "In a note", "a note style renders in the text");
    }
    // A macro that calls itself stops instead of overflowing
    auto loop = inlineStyle("", "<macro name=\"m\"><text macro=\"m\"/><text value=\"x\"/></macro>"
                                "<citation><layout><text macro=\"m\"/></layout></citation>");
    if (loop) {
        bib::Library lib;
        bib::Item it;
        it.id = "n";
        lib.add(it);
        csl::Result r = csl::process(*loop, bundledLocale, lib, {citation({ref("n")})}, csl::Options());
        check(r.citations.size() == 1, "a recursive macro renders without overflowing");
    }
}

// --- Locales -----------------------------------------------------------------

void localeTests() {
    std::string body = "<locale xml:lang=\"en\"><terms><term name=\"and\">plus</term></terms></locale>"
                       "<locale><terms><term name=\"et-al\">etc.</term></terms></locale>"
                       "<citation><layout><group delimiter=\"|\"><text term=\"and\"/><text term=\"et-al\"/>"
                       "<text term=\"in\"/></group></layout></citation>";
    auto terms = [&](const std::string& lang) {
        csl::Result r = runOne("", body, bib::Item(), lang);
        return r.citations.empty() ? std::string() : csl::plain(r.citations[0]);
    };
    expect(terms("en-US"), "plus|etc.|in", "locale: the style's language block, then its plain block, then the file");
    expect(terms("en-GB"), "plus|etc.|in", "locale: en-GB falls back to en-US");
    expect(terms("zh-CN"), AND_ZH + "|etc.|" + IN_ZH, "locale: zh-CN terms");
    expect(terms("zh-TW"), AND_ZH + "|etc.|" + IN_ZH, "locale: zh-TW falls back to zh-CN");
    expect(terms("zh"), AND_ZH + "|etc.|" + IN_ZH, "locale: zh is zh-CN");
    expect(terms("de-AT"), "and|etc.|in", "locale: no German file falls back to en-US");
    // No locale file at all
    auto style = inlineStyle("", "<citation><layout><text term=\"in\"/><text value=\"!\"/></layout></citation>");
    if (style) {
        bib::Library lib;
        bib::Item it;
        it.id = "x";
        lib.add(it);
        csl::Result r = csl::process(*style, [](std::string_view) { return std::string(); }, lib,
                                     {citation({ref("x")})}, csl::Options());
        bool warned = false;
        for (const std::string& w : r.warnings) warned = warned || w == "no locale for en-US";
        check(warned, "locale: a missing locale warns");
        expect(r.citations.empty() ? "" : csl::plain(r.citations[0]), "!", "locale: missing terms print nothing");
    }
    // Ordinals come as a set: zh-CN's empty ordinal, never en-US's "nd"
    bib::Item edition;
    edition.fields["edition"] = "2";
    std::string ordinal = "<number variable=\"edition\" form=\"ordinal\"/>";
    expect(layoutOut(ordinal, edition, "", "zh-CN"), "2", "locale: zh-CN ordinals");
    expect(layoutOut(ordinal, edition, "", "en-US"), "2nd", "locale: en-US ordinals");
    // Dates and quotation marks follow the locale
    bib::Item dated;
    dated.dates["issued"] = day(2020, 5, 3);
    dated.fields["title"] = "Ink";
    expect(layoutOut("<date variable=\"issued\" form=\"text\"/>", dated, "", "zh-CN"),
           "2020" + YEAR_ZH + "5" + MONTH_ZH + "3" + DAY_ZH, "locale: a zh-CN date");
    std::string quoted = "<group delimiter=\", \"><text variable=\"title\" quotes=\"true\"/><text value=\"x\"/></group>";
    expect(layoutOut(quoted, dated, "", "en-US"), LQ + "Ink," + RQ + " x", "locale: en-US moves the comma inside");
    expect(layoutOut(quoted, dated, "", "zh-CN"), "\xE3\x80\x8AInk\xE3\x80\x8B, x", "locale: zh-CN quotes, comma outside");
    // A style's own date format for its language
    std::string own = "<locale xml:lang=\"en\"><date form=\"text\"><date-part name=\"day\" suffix=\" \"/>"
                      "<date-part name=\"month\" form=\"short\" suffix=\" \"/><date-part name=\"year\"/></date></locale>"
                      "<citation><layout><date variable=\"issued\" form=\"text\"/></layout></citation>";
    csl::Result r = runOne("", own, dated, "en-US");
    expect(r.citations.empty() ? "" : csl::plain(r.citations[0]), "3 May 2020", "locale: the style's date format");
}

// --- Names -------------------------------------------------------------------

void nameTests() {
    std::vector<bib::Name> one = {person("Doe", "John")};
    std::vector<bib::Name> two = {person("Doe", "John"), person("Smith", "Ann")};
    std::vector<bib::Name> three = {person("Doe", "John"), person("Smith", "Ann"), person("Brown", "Bob")};
    std::vector<bib::Name> six = {person("Doe", "John"),  person("Smith", "Ann"), person("Brown", "Bob"),
                                  person("Green", "Cara"), person("White", "Dan"), person("Black", "Eve")};
    expect(names("<name/>", one), "John Doe", "names: long form");
    expect(names("<name initialize-with=\". \"/>", one), "J. Doe", "names: initials");
    expect(names("<name name-as-sort-order=\"all\" initialize-with=\". \"/>", one), "Doe, J.", "names: sort order");
    expect(names("<name and=\"text\"/>", two), "John Doe and Ann Smith", "names: two with and");
    expect(names("<name and=\"text\"/>", three), "John Doe, Ann Smith, and Bob Brown", "names: three with and");
    expect(names("<name and=\"symbol\" delimiter-precedes-last=\"never\"/>", three), "John Doe, Ann Smith & Bob Brown",
           "names: & never after a comma");
    expect(names("<name and=\"text\" delimiter-precedes-last=\"always\"/>", two), "John Doe, and Ann Smith",
           "names: a comma before and, always");
    expect(names("<name and=\"text\" name-as-sort-order=\"first\" delimiter-precedes-last=\"after-inverted-name\"/>", two),
           "Doe, John, and Ann Smith", "names: a comma after an inverted name");
    expect(names("<name and=\"text\" name-as-sort-order=\"first\" delimiter-precedes-last=\"after-inverted-name\"/>", three),
           "Doe, John, Ann Smith and Bob Brown", "names: no comma after a name in order");
    expect(names("<name et-al-min=\"3\" et-al-use-first=\"1\"/>", three), "John Doe et al.", "names: et al. after one");
    expect(names("<name et-al-min=\"3\" et-al-use-first=\"2\"/>", three), "John Doe, Ann Smith, et al.",
           "names: et al. after two");
    expect(names("<name et-al-min=\"3\" et-al-use-first=\"2\" delimiter-precedes-et-al=\"never\"/>", three),
           "John Doe, Ann Smith et al.", "names: no comma before et al.");
    expect(names("<name et-al-min=\"5\" et-al-use-first=\"3\" et-al-use-last=\"true\"/>", six),
           "John Doe, Ann Smith, Bob Brown, \xE2\x80\xA6 Eve Black", "names: et-al-use-last");
    expect(names("<name et-al-min=\"3\" et-al-use-first=\"1\"/><et-al term=\"and others\"/>", three),
           "John Doe and others", "names: the and others term");
    expect(names("<name/>", {person("Doe", "John"), organisation("others")}), "John Doe et al.",
           "names: BibTeX's and others forces et al.");
    expect(names("<name form=\"count\" et-al-min=\"3\" et-al-use-first=\"1\"/>", three), "1", "names: count after et al.");
    expect(names("<name form=\"count\"/>", three), "3", "names: count");
    expect(names("<name form=\"short\" and=\"text\"/>", three), "Doe, Smith, and Brown", "names: short form");
    // Name options inherit: <style>, <citation>, <names>, <name>
    expect(layoutOut("<names variable=\"author\" et-al-min=\"3\" et-al-use-first=\"1\"><name/></names>",
                     withAuthors(three)),
           "John Doe et al.", "names: options on <names>");
    expect(layoutOut("<names variable=\"author\" et-al-min=\"3\" et-al-use-first=\"1\"><name et-al-use-first=\"2\"/></names>",
                     withAuthors(three)),
           "John Doe, Ann Smith, et al.", "names: <name> over <names>");
    csl::Result inherited = runOne("initialize-with=\". \" name-as-sort-order=\"all\"",
                                   "<citation et-al-min=\"3\" et-al-use-first=\"1\" name-form=\"short\"><layout>"
                                   "<names variable=\"author\"><name/></names></layout></citation>",
                                   withAuthors(three));
    expect(inherited.citations.empty() ? "" : csl::plain(inherited.citations[0]), "Doe et al.",
           "names: <citation> and <style> options");
    expect(names("<name initialize-with=\". \"/>", {person("Sartre", "Jean-Paul")}), "J.-P. Sartre",
           "names: hyphenated initials");
    expect(names("<name initialize-with=\". \"/>", {person("Sartre", "Jean-Paul")}, "initialize-with-hyphen=\"false\""),
           "J. P. Sartre", "names: initials without the hyphen");
    expect(names("<name initialize=\"false\" initialize-with=\". \"/>", {person("Tolkien", "J.R.R.")}), "J. R. R. Tolkien",
           "names: initials reshaped, not made");
    expect(names("<name initialize=\"false\" initialize-with=\". \"/>", {person("Tolkien", "John R.")}),
           "John R. Tolkien", "names: given names kept whole");
    expect(names("<name initialize-with=\"\"/>", {person("Tolkien", "John Ronald")}), "JR Tolkien",
           "names: initials run together");
    bib::Name beethoven = person("Beethoven", "Ludwig");
    beethoven.nonDroppingParticle = "van";
    expect(names("<name/>", {beethoven}), "Ludwig van Beethoven", "names: a particle in order");
    expect(names("<name name-as-sort-order=\"all\"/>", {beethoven}), "Beethoven, Ludwig van",
           "names: the particle demoted (display-and-sort)");
    expect(names("<name name-as-sort-order=\"all\"/>", {beethoven}, "demote-non-dropping-particle=\"never\""),
           "van Beethoven, Ludwig", "names: the particle kept (never)");
    expect(names("<name form=\"short\"/>", {beethoven}), "van Beethoven", "names: short with a particle");
    // BibTeX's von part is a dropping particle unless useprefix=true
    bib::Name vonBeethoven = person("Beethoven", "Ludwig");
    vonBeethoven.droppingParticle = "van";
    expect(names("<name/>", {vonBeethoven}), "Ludwig van Beethoven", "names: a von part in order");
    expect(names("<name form=\"short\"/>", {vonBeethoven}), "Beethoven", "names: a von part dropped when short");
    expect(names("<name name-as-sort-order=\"all\" initialize-with=\". \"/>", {vonBeethoven}), "Beethoven, L. van",
           "names: a von part after the initials");
    // One name only
    expect(names("<name name-as-sort-order=\"all\" initialize-with=\". \"/>", {person("", "Plato")}), "Plato",
           "names: a single name inverted");
    expect(names("<name and=\"text\" form=\"short\"/>", {person("", "Plato"), person("Doe", "John")}), "Plato and Doe",
           "names: a single name, short");
    // "and others" is never printed
    expect(names("<name name-as-sort-order=\"all\" initialize-with=\". \" and=\"symbol\" delimiter-precedes-last=\"always\"/>",
                 {person("Doe", "John"), person("Smith", "Ann"), organisation("others")}),
           "Doe, J., Smith, A., et al.", "names: and others after two names");
    expect(names("<name et-al-min=\"5\" et-al-use-first=\"1\" et-al-use-last=\"true\"/>",
                 {person("Doe", "John"), person("Smith", "Ann"), person("Brown", "Bob"), person("Green", "Cara"),
                  person("White", "Dan"), organisation("others")}),
           "John Doe et al.", "names: and others never becomes the last name");
    bib::Name fontaine = person("La Fontaine", "Jean");
    fontaine.droppingParticle = "de";
    expect(names("<name/>", {fontaine}), "Jean de La Fontaine", "names: a dropping particle");
    expect(names("<name name-as-sort-order=\"all\"/>", {fontaine}), "La Fontaine, Jean de",
           "names: a dropping particle inverted");
    bib::Name king = person("King", "Martin Luther");
    king.suffix = "Jr.";
    expect(names("<name/>", {king}), "Martin Luther King Jr.", "names: a suffix");
    expect(names("<name name-as-sort-order=\"all\"/>", {king}), "King, Martin Luther, Jr.", "names: a suffix inverted");
    king.commaSuffix = true;
    expect(names("<name/>", {king}), "Martin Luther King, Jr.", "names: a comma suffix");
    bib::Name alembert = person("Alembert", "Jean");
    alembert.nonDroppingParticle = "d'";
    expect(names("<name/>", {alembert}), "Jean d'Alembert", "names: an apostrophe particle joins");
    expect(names("<name initialize-with=\". \"/>", {organisation("World Health Organization")}),
           "World Health Organization", "names: an organisation as written");
    expect(names("<name/>", {person("\xE5\xBC\xA0", "\xE4\xB8\x89")}), ZHANG + SAN, "names: a Chinese name, unspaced");
    expect(names("<name form=\"short\"/>", {person("\xE5\xBC\xA0", "\xE4\xB8\x89")}), ZHANG, "names: a Chinese short name");
    expect(names("<name name-as-sort-order=\"all\" sort-separator=\" \" initialize-with=\" \">"
                 "<name-part name=\"family\" text-case=\"uppercase\"/></name>",
                 {person("Doe", "John Michael")}),
           "DOE J M", "names: GB/T's form");
    expect(names("<name><name-part name=\"family\" font-variant=\"small-caps\" prefix=\"[\" suffix=\"]\"/></name>", one),
           "John [Doe]", "names: name-part affixes");
    // Labels, editors and translators
    bib::Item eds = withAuthors({person("Editor", "Alice"), person("Compiler", "Bob")}, "editor");
    expect(layoutOut("<names variable=\"editor\"><name/><label form=\"short\" prefix=\" (\" suffix=\")\"/></names>", eds),
           "Alice Editor, Bob Compiler (eds.)", "names: a plural label after");
    expect(layoutOut("<names variable=\"editor\"><label form=\"verb\" suffix=\" \"/><name and=\"text\"/></names>", eds),
           "edited by Alice Editor and Bob Compiler", "names: a verb label before");
    bib::Item both = withAuthors({person("Doe", "John")}, "editor");
    both.names["translator"] = {person("Doe", "John")};
    std::string editorTranslator = "<names variable=\"editor translator\"><name/><label form=\"short\" prefix=\", \"/></names>";
    expect(layoutOut(editorTranslator, both), "John Doe, ed. & trans.", "names: one editor who translated");
    both.names["translator"] = {person("Smith", "Ann")};
    expect(layoutOut(editorTranslator, both), "John Doe, ed., Ann Smith, trans.", "names: editor and translator apart");
    // Substitutes: a shorthand <names> borrows the name and label, and what
    // stood in is not printed again
    bib::Item edited = withAuthors({person("Editor", "Alice")}, "editor");
    edited.fields["title"] = "Lone title";
    std::string substituting = "<group delimiter=\" | \"><names variable=\"author\"><name name-as-sort-order=\"all\" "
                               "initialize-with=\". \"/><label form=\"short\" prefix=\" (\" suffix=\")\"/><substitute>"
                               "<names variable=\"editor\"/><text variable=\"title\" font-style=\"italic\"/></substitute>"
                               "</names><names variable=\"editor\"><name/></names><text variable=\"title\"/></group>";
    expect(layoutOut(substituting, edited), "Editor, A. (ed.) | Lone title", "names: the editor stands in, once");
    bib::Item untitled;
    untitled.fields["title"] = "Lone title";
    expect(layoutOut(substituting, untitled, "", "en-US", true), "<i>Lone title</i>",
           "names: the title stands in, once");
}

// --- Pages, numbers and case ----------------------------------------------------

void pageTests() {
    struct Case {
        uint8_t format;
        const char* in;
        std::string out;
    } cases[] = {
        {csl::PagesExpanded, "321-8", "321" + ND + "328"},
        {csl::PagesExpanded, "42-45", "42" + ND + "45"},
        {csl::PagesExpanded, "1-10", "1" + ND + "10"},
        {csl::PagesMinimal, "321-328", "321" + ND + "8"},
        {csl::PagesMinimal, "42-45", "42" + ND + "5"},
        {csl::PagesMinimal, "2787-2816", "2787" + ND + "816"},
        {csl::PagesMinimalTwo, "321-328", "321" + ND + "28"},
        {csl::PagesMinimalTwo, "42-45", "42" + ND + "45"},
        {csl::PagesMinimalTwo, "2787-2816", "2787" + ND + "816"},
        {csl::PagesChicago15, "71-72", "71" + ND + "72"},
        {csl::PagesChicago15, "96-117", "96" + ND + "117"},
        {csl::PagesChicago15, "100-104", "100" + ND + "104"},
        {csl::PagesChicago15, "1100-1113", "1100" + ND + "1113"},
        {csl::PagesChicago15, "101-108", "101" + ND + "8"},
        {csl::PagesChicago15, "808-833", "808" + ND + "33"},
        {csl::PagesChicago15, "1103-1104", "1103" + ND + "4"},
        {csl::PagesChicago15, "321-328", "321" + ND + "28"},
        {csl::PagesChicago15, "498-532", "498" + ND + "532"},
        {csl::PagesChicago15, "1087-1089", "1087" + ND + "89"},
        {csl::PagesChicago15, "1496-1500", "1496" + ND + "1500"},
        {csl::PagesChicago15, "1496-1504", "1496" + ND + "1504"},
        {csl::PagesChicago15, "2787-2816", "2787" + ND + "2816"},
        {csl::PagesChicago15, "11564-11615", "11564" + ND + "615"},
        {csl::PagesChicago15, "12991-13001", "12991" + ND + "3001"},
        {csl::PagesChicago16, "1496-1500", "1496" + ND + "500"},
        {csl::PagesChicago16, "1496-1504", "1496" + ND + "504"},
        {csl::PagesChicago16, "2787-2816", "2787" + ND + "816"},
        {csl::PagesChicago16, "321-328", "321" + ND + "28"},
        {csl::PagesAsWritten, "45-67", "45" + ND + "67"},
        {csl::PagesAsWritten, "45--67", "45" + ND + "67"},
        {csl::PagesAsWritten, "e1234", "e1234"},
        {csl::PagesAsWritten, "45-67, 70-72", "45" + ND + "67, 70" + ND + "72"},
        {csl::PagesExpanded, "S12-S15", "S12" + ND + "S15"},
        {csl::PagesExpanded, "xiv-xvi", "xiv" + ND + "xvi"},
        // BibTeX's -- arrives as an en dash; an em dash reads as one too
        {csl::PagesExpanded, "321\xE2\x80\x93" "8", "321" + ND + "328"},
        {csl::PagesMinimal, "321\xE2\x80\x94" "328", "321" + ND + "8"},
        {csl::PagesAsWritten, "45 \xE2\x80\x93 67", "45" + ND + "67"},
    };
    for (const Case& c : cases) {
        expect(csl::formatPages(c.in, c.format, ND), c.out, std::string("pages: ") + c.in);
    }
    expect(csl::formatPages("45-67", csl::PagesExpanded, "-"), "45-67", "pages: GB/T's hyphen");
    // Through a style: page and a page locator
    bib::Item paged;
    paged.fields["page"] = "321-328";
    expect(layoutOut("<text variable=\"page\"/>", paged, "page-range-format=\"minimal\""), "321" + ND + "8",
           "pages: the style's format");
    csl::Result r = runOne("page-range-format=\"minimal\"",
                           "<citation><layout><group delimiter=\" \"><label variable=\"locator\" form=\"short\"/>"
                           "<text variable=\"locator\"/></group></layout></citation>",
                           paged, "en-US", {citation({ref("x", "321-328")}), citation({ref("x", "4")}),
                                            citation({ref("x", "2", "chapter")})});
    if (r.citations.size() == 3) {
        expect(csl::plain(r.citations[0]), "pp. 321" + ND + "8", "pages: a locator range, plural label");
        expect(csl::plain(r.citations[1]), "p. 4", "pages: a single page");
        expect(csl::plain(r.citations[2]), "chap. 2", "pages: a chapter locator keeps its digits");
    }
    expect(layoutOut("<group delimiter=\" \"><label variable=\"page\" form=\"short\"/><text variable=\"page\"/></group>",
                     paged),
           "pp. 321" + ND + "328", "pages: a label for a range");
    bib::Item first;
    first.fields["page"] = "45-67";
    expect(layoutOut("<text variable=\"page-first\"/>", first), "45", "pages: page-first");
    first.fields["page"] = "45\xE2\x80\x93" "67";
    expect(layoutOut("<text variable=\"page-first\"/>", first), "45", "pages: page-first before an en dash");
    expect(layoutOut("<group delimiter=\" \"><label variable=\"page\" form=\"short\"/><text variable=\"page\"/></group>",
                     first),
           "pp. 45" + ND + "67", "pages: an en-dash range is plural");
    check(csl::isNumeric("2\xE2\x80\x93" "4") && csl::isNumeric("2\xE2\x80\x94" "4"), "pages: dashed ranges are numeric");
    // A thousands comma is no list: one patent, "No."
    bib::Item patent;
    patent.fields["number"] = "10,000,001";
    std::string numbered = "<group delimiter=\" \"><label variable=\"number\" form=\"short\" text-case=\"capitalize-first\"/>"
                           "<text variable=\"number\"/></group>";
    expect(layoutOut(numbered, patent), "No. 10,000,001", "a number with thousands commas is one");
    patent.fields["number"] = "2, 4";
    expect(layoutOut(numbered, patent), "Nos. 2, 4", "a list of numbers is several");
}

void numberTests() {
    auto number = [](const char* value, const char* form) {
        bib::Item it;
        it.fields["edition"] = value;
        return layoutOut(std::string("<number variable=\"edition\" form=\"") + form + "\"/>", it);
    };
    struct Ord {
        const char* in;
        const char* out;
    } ordinals[] = {{"1", "1st"},   {"2", "2nd"},   {"3", "3rd"},   {"4", "4th"},     {"11", "11th"},
                    {"12", "12th"}, {"13", "13th"}, {"21", "21st"}, {"22", "22nd"},   {"23", "23rd"},
                    {"101", "101st"}, {"111", "111th"}, {"112", "112th"}, {"1000", "1000th"}};
    for (const Ord& o : ordinals) expect(number(o.in, "ordinal"), o.out, std::string("ordinal ") + o.in);
    expect(number("2", "long-ordinal"), "second", "long ordinal");
    expect(number("10", "long-ordinal"), "tenth", "long ordinal 10");
    expect(number("11", "long-ordinal"), "11th", "long ordinal past ten");
    expect(number("14", "roman"), "xiv", "roman");
    expect(number("Second", "ordinal"), "Second", "a word stays");
    expect(number("2nd", "ordinal"), "2nd", "a number with a suffix stays");
    expect(number("2-4", "numeric"), "2" + ND + "4", "a number range takes an en dash");
    expect(number("2, 4", "ordinal"), "2nd, 4th", "a list in ordinals");
    // is-numeric
    struct Num {
        const char* value;
        bool numeric;
    } numerics[] = {{"12", true},  {"2nd", true},   {"L2d", true},  {"D2", true},     {"2b", true},
                    {"2-4", true}, {"2, 3", true},  {"2 & 4", true}, {"second", false}, {"2nd edition", false},
                    {"", false},   {"1.2", false}, {"xiv", false}, {"2-", false},    {"TR-42", false}};
    for (const Num& n : numerics) {
        check(csl::isNumeric(n.value) == n.numeric, std::string("is-numeric ") + n.value);
    }
    bib::Item it;
    it.fields["volume"] = "12";
    std::string test = "<choose><if is-numeric=\"volume\"><text value=\"yes\"/></if><else><text value=\"no\"/></else></choose>";
    expect(layoutOut(test, it), "yes", "is-numeric in a condition");
    it.fields["volume"] = "Supplement";
    expect(layoutOut(test, it), "no", "is-numeric false in a condition");
    // Gendered ordinals, from a style's locale
    std::string french = "<locale xml:lang=\"fr\"><terms><term name=\"edition\" gender=\"feminine\">\xC3\xA9" "dition</term>"
                         "<term name=\"issue\" gender=\"masculine\">num\xC3\xA9ro</term><term name=\"ordinal\">e</term>"
                         "<term name=\"ordinal-01\" gender-form=\"feminine\" match=\"whole-number\">re</term>"
                         "<term name=\"ordinal-01\" gender-form=\"masculine\" match=\"whole-number\">er</term></terms></locale>"
                         "<citation><layout><group delimiter=\" \"><number variable=\"edition\" form=\"ordinal\"/>"
                         "<number variable=\"issue\" form=\"ordinal\"/></group></layout></citation>";
    bib::Item gendered;
    gendered.fields["edition"] = "1";
    gendered.fields["issue"] = "1";
    csl::Result r = runOne("", french, gendered, "fr-FR");
    expect(r.citations.empty() ? "" : csl::plain(r.citations[0]), "1re 1er", "ordinals agree in gender");
    gendered.fields["edition"] = "21";
    gendered.fields["issue"] = "2";
    r = runOne("", french, gendered, "fr-FR");
    expect(r.citations.empty() ? "" : csl::plain(r.citations[0]), "21e 2e", "whole-number ordinals match only 1");
}

void caseTests() {
    auto titled = [](const char* title, const char* textCase, const char* language = "") {
        bib::Item it;
        it.fields["title"] = title;
        if (*language) it.fields["language"] = language;
        return layoutOut(std::string("<text variable=\"title\" text-case=\"") + textCase + "\"/>", it);
    };
    expect(titled("the art of war", "title"), "The Art of War", "title case");
    expect(titled("THE ART OF WAR", "title"), "The Art of War", "title case from capitals");
    expect(titled("a tale of two cities: the end of an era", "title"), "A Tale of Two Cities: The End of an Era",
           "title case after a colon");
    expect(titled("iPhone and the web", "title"), "iPhone and the Web", "title case keeps mixed case");
    expect(titled("the <span class=\"nocase\">pH</span> of ink", "title"), "The pH of Ink", "title case skips nocase");
    expect(titled("what it is for", "title"), "What It Is For", "title case capitalises the last word");
    expect(titled("self-report in ink", "title"), "Self-Report in Ink", "title case in hyphenated words");
    expect(titled("The Lord Of The Rings", "title"), "The Lord of the Rings", "title case lowers stop words");
    expect(titled("der weg zum ziel", "title", "de"), "der weg zum ziel", "title case only for English");
    expect(titled("the art of war", "title", "en-GB"), "The Art of War", "title case for en-GB items");
    expect(titled("edited by", "capitalize-first"), "Edited by", "capitalize-first");
    expect(titled("eBay rules", "capitalize-first"), "eBay rules", "capitalize-first leaves a mixed word");
    expect(titled("on the road", "capitalize-all"), "On The Road", "capitalize-all");
    expect(titled("Ink AND Paper", "lowercase"), "ink and paper", "lowercase");
    expect(titled("\xC3\xA9lan of ink", "uppercase"), "\xC3\x89LAN OF INK", "uppercase beyond ASCII");
    expect(titled("THE ART OF WAR", "sentence"), "The art of war", "sentence case from capitals");
    expect(titled("the Art of War", "sentence"), "The Art of War", "sentence case leaves mixed words");
    bib::Item stripped;
    expect(layoutOut("<text term=\"page\" form=\"short\" strip-periods=\"true\"/>", stripped), "p", "strip-periods");
}

// --- Dates -------------------------------------------------------------------

void dateTests() {
    auto dated = [](bib::Date d, const std::string& layout = "<date variable=\"issued\" form=\"text\"/>",
                    const std::string& attributes = "") {
        bib::Item it;
        it.dates["issued"] = d;
        return layoutOut(layout, it, attributes);
    };
    expect(dated(day(2020, 5, 3)), "May 3, 2020", "date: text form");
    expect(dated(range({2020, 5, 3}, {2020, 5, 5})), "May 3" + ND + "5, 2020", "date: a range of days");
    expect(dated(range({2020, 5, 3}, {2020, 6, 5})), "May 3" + ND + "June 5, 2020", "date: a range of months");
    expect(dated(range({2020, 5, 3}, {2021, 6, 5})), "May 3, 2020" + ND + "June 5, 2021", "date: a range of years");
    expect(dated(range({2020, 0, 0}, {2021, 0, 0})), "2020" + ND + "2021", "date: years");
    expect(dated(range({2020, 5, 0}, {2020, 6, 0}), "<date variable=\"issued\" form=\"text\" date-parts=\"year\"/>"),
           "2020", "date: a range finer than the parts shown");
    bib::Date spring = day(2020);
    spring.season = 1;
    expect(dated(spring), "Spring 2020", "date: a season");
    expect(dated(day(-50)), "50 BC", "date: BC");
    expect(dated(day(79)), "79 AD", "date: AD for short years");
    bib::Date easter;
    easter.literal = "Easter 1916";
    expect(dated(easter), "Easter 1916", "date: a literal");
    expect(dated(day(2020, 5, 3), "<date variable=\"issued\" form=\"numeric\"/>"), "05/03/2020", "date: numeric form");
    expect(dated(range({2020, 0, 0}, {2021, 0, 0}), "<date variable=\"issued\"><date-part name=\"year\" range-delimiter=\"/\"/></date>"),
           "2020/2021", "date: a range delimiter");
    expect(dated(day(2020), "<date variable=\"issued\"><date-part name=\"year\" form=\"short\"/></date>"), "20",
           "date: a short year");
    std::string monthYear = "<date variable=\"issued\"><date-part name=\"month\" form=\"short\" suffix=\" \"/>"
                            "<date-part name=\"year\"/></date>";
    expect(dated(day(2020, 9), monthYear), "Sept. 2020", "date: a short month");
    expect(dated(day(2020, 9), "<date variable=\"issued\"><date-part name=\"month\" form=\"short\" strip-periods=\"true\"/></date>"),
           "Sept", "date: periods stripped");
    expect(dated(day(2020, 3), "<date variable=\"issued\"><date-part name=\"month\" form=\"numeric-leading-zeros\"/></date>"),
           "03", "date: leading zeros");
    expect(dated(day(2020, 3, 2), "<date variable=\"issued\" delimiter=\"-\"><date-part name=\"year\"/>"
                                  "<date-part name=\"month\" form=\"numeric-leading-zeros\"/>"
                                  "<date-part name=\"day\" form=\"numeric-leading-zeros\"/></date>"),
           "2020-03-02", "date: the style's own parts and delimiter");
    std::string ordinalDay = "<date variable=\"issued\"><date-part name=\"day\" form=\"ordinal\"/></date>";
    expect(dated(day(2020, 3, 2), ordinalDay), "2nd", "date: an ordinal day");
    std::string limited = "<locale><style-options limit-day-ordinals-to-day-1=\"true\"/></locale><citation><layout>" +
                          ordinalDay + "</layout></citation>";
    bib::Item it;
    it.dates["issued"] = day(2020, 3, 2);
    csl::Result r = runOne("", limited, it);
    expect(r.citations.empty() ? "" : csl::plain(r.citations[0]), "2", "date: ordinals only for the first");
    it.dates["issued"] = day(2020, 3, 1);
    r = runOne("", limited, it);
    expect(r.citations.empty() ? "" : csl::plain(r.citations[0]), "1st", "date: the first keeps its ordinal");
    bib::Date about = day(1850);
    about.circa = true;
    expect(dated(about, "<choose><if is-uncertain-date=\"issued\"><text value=\"ca.\"/></if><else><text value=\"-\"/></else></choose>"),
           "ca.", "date: is-uncertain-date");
}

// --- Rich text, quotes, affixes and groups -------------------------------------------

void richTextTests() {
    bib::Item it;
    it.fields["title"] = "<i>Homo sapiens</i> in ink";
    expect(layoutOut("<text variable=\"title\" font-style=\"italic\"/>", it, "", "en-US", true),
           "Homo sapiens<i> in ink</i>", "rich text: italics flip inside italics");
    it.fields["title"] = "H<sub>2</sub>O, x<sup>2</sup>, <b>bold</b> &amp; <span style=\"font-variant:small-caps;\">caps</span>";
    expect(layoutOut("<text variable=\"title\"/>", it, "", "en-US", true),
           "H<sub>2</sub>O, x<sup>2</sup>, <b>bold</b> &amp; <span style=\"font-variant:small-caps\">caps</span>",
           "rich text: markup to runs");
    expect(layoutOut("<text variable=\"title\"/>", it), "H2O, x2, bold & caps", "rich text: plain");
    it.fields["title"] = "Ink";
    expect(layoutOut("<text variable=\"title\" quotes=\"true\"/>", it), LQ + "Ink" + RQ, "quotes");
    expect(layoutOut("<text variable=\"title\" font-style=\"italic\" prefix=\"(\" suffix=\")\"/>", it, "", "en-US", true),
           "(<i>Ink</i>)", "affixes stay outside the formatting");
    csl::Result nested = runOne("", "<macro name=\"q\"><text variable=\"title\" quotes=\"true\"/></macro>"
                                    "<citation><layout><text macro=\"q\" quotes=\"true\"/></layout></citation>", it);
    expect(nested.citations.empty() ? "" : csl::plain(nested.citations[0]), LQ + LSQ + "Ink" + RSQ + RQ,
           "quotes inside quotes turn single");
    it.fields["title"] = "The " + LQ + "best" + RQ + " ink";
    expect(layoutOut("<text variable=\"title\" quotes=\"true\"/>", it), LQ + "The " + LSQ + "best" + RSQ + " ink" + RQ,
           "quotation marks in a quoted title turn single");
    it.fields["title"] = "Why?";
    expect(layoutOut("<group delimiter=\", \"><text variable=\"title\" quotes=\"true\"/><text value=\"x\"/></group>", it),
           LQ + "Why?," + RQ + " x", "a comma moves inside the quotes, after a question mark");
    expect(layoutOut("<text variable=\"title\" suffix=\".\"/>", it), "Why?", "no period after a question mark");
    it.fields["title"] = "Ink in <i>Sepia</i>";
    expect(layoutOut("<group delimiter=\", \"><text variable=\"title\" quotes=\"true\"/><text value=\"x\"/></group>", it,
                     "", "en-US", true),
           LQ + "Ink in <i>Sepia</i>," + RQ + " x", "a comma moved inside the quotes is not italic");
    it.fields["title"] = "U.S.";
    expect(layoutOut("<text variable=\"title\" suffix=\".\"/>", it), "U.S.", "no doubled period");
    expect(layoutOut("<group delimiter=\". \"><text variable=\"title\"/><text value=\"Next\"/></group>", it), "U.S. Next",
           "a delimiter's period merges");
    expect(layoutOut("<group delimiter=\", \"><text variable=\"title\"/><text value=\"x\"/></group>", it), "U.S., x",
           "a period keeps a following comma");
    // Groups print only when a variable they ask for has a value
    std::string group = "<group delimiter=\" \"><text term=\"in\"/><text variable=\"container-title\"/></group>";
    bib::Item none;
    expect(layoutOut(group + "<text value=\"!\"/>", none), "!", "a group without its variables is silent");
    bib::Item box;
    box.fields["container-title"] = "Box";
    expect(layoutOut(group, box), "in Box", "a group with a variable prints its terms");
    expect(layoutOut("<group delimiter=\" \"><text term=\"in\"/><text value=\"x\"/></group>", none), "in x",
           "a group of terms alone prints");
    expect(layoutOut("<group delimiter=\" \"><text term=\"in\"/><group><text variable=\"volume\"/></group></group>"
                     "<text value=\"!\"/>",
                     none),
           "!", "a nested empty group silences its parent");
    // A choose's branch is part of the group around it
    expect(layoutOut("<group delimiter=\"; \"><text value=\"a\"/><choose><if type=\"book\"><text value=\"b\"/>"
                     "<text value=\"c\"/></if></choose></group>",
                     withAuthors({})),
           "a; b; c", "a branch's elements take the group's delimiter");
    // Links. Fields are rich text with entities, URLs too: a&amp;b is a&b
    bib::Item web;
    web.fields["URL"] = "https://example.com/a?b&amp;c";
    web.fields["DOI"] = "10.1/x";
    expect(layoutOut("<text variable=\"URL\"/>", web, "", "en-US", true),
           "<a href=\"https://example.com/a?b&amp;c\">https://example.com/a?b&amp;c</a>", "a URL links");
    expect(layoutOut("<text variable=\"URL\"/>", web), "https://example.com/a?b&c", "a URL prints unescaped");
    csl::Result linked = runOne("", "<citation><layout><text variable=\"URL\"/></layout></citation>", web);
    expect(linked.citations.empty() || linked.citations[0].empty() ? "" : linked.citations[0][0].link,
           "https://example.com/a?b&c", "a URL's link is unescaped");
    web.fields["DOI"] = "10.1000/a&amp;b";
    linked = runOne("", "<citation><layout><text variable=\"DOI\"/></layout></citation>", web);
    expect(linked.citations.empty() || linked.citations[0].empty() ? "" : linked.citations[0][0].link,
           "https://doi.org/10.1000/a&b", "a DOI's link is unescaped");
    web.fields["DOI"] = "10.1/x";
    expect(layoutOut("<text variable=\"DOI\" prefix=\"https://doi.org/\"/>", web, "", "en-US", true),
           "<a href=\"https://doi.org/10.1/x\">https://doi.org/10.1/x</a>", "a DOI links, prefix and all");
    expect(layoutOut("<text variable=\"DOI\" prefix=\"doi: \"/>", web, "", "en-US", true),
           "doi: <a href=\"https://doi.org/10.1/x\">10.1/x</a>", "a DOI links without its label");
    // Short forms fall back to the long
    bib::Item titled;
    titled.fields["title"] = "A long title";
    expect(layoutOut("<text variable=\"title\" form=\"short\"/>", titled), "A long title", "a short form falls back");
    titled.fields["title-short"] = "Short";
    expect(layoutOut("<text variable=\"title\" form=\"short\"/>", titled), "Short", "a short form");
}

// --- Citations -----------------------------------------------------------------

void citationTests() {
    // Positions, in document order
    std::string positions = "<citation><layout delimiter=\"; \"><choose>"
                            "<if position=\"ibid-with-locator\"><text value=\"ibid-loc\"/></if>"
                            "<else-if position=\"ibid\"><text value=\"ibid\"/></else-if>"
                            "<else-if position=\"near-note\"><text value=\"near\"/></else-if>"
                            "<else-if position=\"subsequent\"><text value=\"sub\"/></else-if>"
                            "<else><text value=\"first\"/></else></choose></layout></citation>";
    auto style = inlineStyle("", positions);
    if (style) {
        bib::Library lib;
        for (const char* id : {"a", "b", "c"}) {
            bib::Item it;
            it.id = id;
            lib.add(it);
        }
        std::vector<cite::Citation> doc = {
            citation({ref("a")}),       citation({ref("a")}),       citation({ref("a", "4")}),
            citation({ref("a", "4")}),  citation({ref("a")}),       citation({ref("b")}),
            citation({ref("a")}),       citation({ref("a"), ref("b")}), citation({ref("b")}),
        };
        for (int i = 0; i < 6; ++i) doc.push_back(citation({ref("c")}));
        doc.push_back(citation({ref("a")}));
        csl::Result r = csl::process(*style, bundledLocale, lib, doc, csl::Options());
        const char* want[] = {"first", "ibid", "ibid-loc", "ibid", "near", "first", "near", "ibid; near", "near",
                              "first", "ibid", "ibid", "ibid", "ibid", "ibid", "sub"};
        for (size_t i = 0; i < r.citations.size() && i < 16; ++i) {
            expect(csl::plain(r.citations[i]), want[i], "position of citation " + std::to_string(i));
        }
    }
    // Names added to tell cites apart (APA)
    auto apa = loadStyle("apa");
    if (apa) {
        bib::Library lib;
        for (const char* id : {"x", "y"}) {
            bib::Item it;
            it.id = id;
            it.type = "article-journal";
            it.names["author"] = {person("Doe", "John"), person(*id == 'x' ? "Smith" : "Jones", "Ann"),
                                  person("Brown", "Bob")};
            it.fields["title"] = "Ink";
            it.dates["issued"] = day(2020);
            lib.add(it);
        }
        csl::Result r = csl::process(*apa, bundledLocale, lib, {citation({ref("x")}), citation({ref("y")})},
                                     csl::Options());
        if (r.citations.size() == 2) {
            expect(csl::plain(r.citations[0]), "(Doe, Smith, et al., 2020)", "disambiguation adds names");
            expect(csl::plain(r.citations[1]), "(Doe, Jones, et al., 2020)", "disambiguation adds names, the other");
        }
        if (r.bibliography.entries.size() == 2) {
            expect(csl::plain(r.bibliography.entries[0].text).substr(0, 32), "Doe, J., Jones, A., & Brown, B. ",
                   "the bibliography keeps its names");
        }
    }
    bib::Library lib = library();
    // disambiguate="true" for what nothing else told apart
    auto byTitle = inlineStyle("", "<citation><layout delimiter=\"; \"><names variable=\"author\"><name form=\"short\"/>"
                                   "</names><choose><if disambiguate=\"true\"><text variable=\"title\" prefix=\", \"/></if>"
                                   "</choose></layout></citation>");
    if (byTitle) {
        csl::Result r = csl::process(*byTitle, bundledLocale, lib,
                                     {citation({ref("doeA2020"), ref("doeB2020"), ref("smithJ2021")})}, csl::Options());
        expect(r.citations.empty() ? "" : csl::plain(r.citations[0]), "Doe, Alpha inks; Doe, Beta inks; Smith",
               "disambiguate=\"true\"");
    }
    // Year suffixes the style never prints itself follow the first year;
    // collapsing them
    std::string suffixed = "<citation disambiguate-add-year-suffix=\"true\" collapse=\"%s\" year-suffix-delimiter=\",\">"
                           "<layout delimiter=\"; \"><group delimiter=\" \"><names variable=\"author\"><name form=\"short\"/>"
                           "</names><date variable=\"issued\"><date-part name=\"year\"/></date></group></layout></citation>"
                           "<bibliography><layout><names variable=\"author\"><name/></names>"
                           "<date variable=\"issued\" prefix=\" \"><date-part name=\"year\"/></date></layout></bibliography>";
    bib::Library three = library();
    {
        bib::Item c;
        c.id = "doeC2020";
        c.type = "article-journal";
        c.names["author"] = {person("Doe", "John")};
        c.fields["title"] = "Gamma inks";
        c.dates["issued"] = day(2020);
        three.add(c);
    }
    for (const char* collapse : {"year-suffix", "year-suffix-ranged", "year"}) {
        char body[2048];
        std::snprintf(body, sizeof body, suffixed.c_str(), collapse);
        auto s = inlineStyle("", body);
        if (!s) continue;
        csl::Result r = csl::process(*s, bundledLocale, three,
                                     {citation({ref("doeA2020"), ref("doeB2020"), ref("doeC2020")}),
                                      citation({ref("doeA2020"), ref("doeB2020"), ref("smithJ2021")})},
                                     csl::Options());
        if (r.citations.size() != 2) continue;
        std::string name = collapse;
        if (name == "year-suffix") {
            expect(csl::plain(r.citations[0]), "Doe 2020a,b,c", "year-suffix collapse");
            expect(csl::plain(r.citations[1]), "Doe 2020a,b; Smith 2021", "year-suffix collapse, then another");
            expect(r.bibliography.entries.size() >= 1 ? csl::plain(r.bibliography.entries[0].text) : "", "John Doe 2020a",
                   "an implicit year suffix in the bibliography");
        } else if (name == "year-suffix-ranged") {
            expect(csl::plain(r.citations[0]), "Doe 2020a" + ND + "c", "year-suffix-ranged collapse");
        } else {
            expect(csl::plain(r.citations[0]), "Doe 2020a, 2020b, 2020c", "year collapse");
        }
    }
    // Subsequent author substitute
    std::string substitute = "<citation><layout><text variable=\"title\"/></layout></citation>"
                             "<bibliography subsequent-author-substitute=\"---\" subsequent-author-substitute-rule=\"%s\">"
                             "<sort><key variable=\"title\"/></sort><layout><group delimiter=\". \"><names variable=\"author\">"
                             "<name/></names><text variable=\"title\"/></group></layout></bibliography>";
    bib::Library authors;
    auto addAuthored = [&](const char* id, const char* title, std::vector<bib::Name> who) {
        bib::Item it;
        it.id = id;
        it.fields["title"] = title;
        it.names["author"] = std::move(who);
        authors.add(it);
    };
    addAuthored("p", "Alpha", {person("Doe", "John"), person("Smith", "Ann")});
    addAuthored("q", "Beta", {person("Doe", "John"), person("Smith", "Ann")});
    addAuthored("r", "Gamma", {person("Doe", "John"), person("Brown", "Bob")});
    struct Rule {
        const char* rule;
        const char* second;
        const char* third;
    } rules[] = {{"complete-all", "---. Beta", "John Doe, Bob Brown. Gamma"},
                 {"complete-each", "---, ---. Beta", "John Doe, Bob Brown. Gamma"},
                 {"partial-each", "---, ---. Beta", "---, Bob Brown. Gamma"},
                 {"partial-first", "---, Ann Smith. Beta", "---, Bob Brown. Gamma"}};
    for (const Rule& rule : rules) {
        char body[2048];
        std::snprintf(body, sizeof body, substitute.c_str(), rule.rule);
        auto s = inlineStyle("", body);
        if (!s) continue;
        csl::Options all;
        all.nocite = {"*"};
        csl::Result r = csl::process(*s, bundledLocale, authors, {}, all);
        if (r.bibliography.entries.size() != 3) {
            check(false, std::string("subsequent author substitute: three entries for ") + rule.rule);
            continue;
        }
        expect(csl::plain(r.bibliography.entries[0].text), "John Doe, Ann Smith. Alpha", std::string(rule.rule) + ": first");
        expect(csl::plain(r.bibliography.entries[1].text), rule.second, std::string(rule.rule) + ": second");
        expect(csl::plain(r.bibliography.entries[2].text), rule.third, std::string(rule.rule) + ": third");
    }
    // Only missing keys: no layout around them
    auto ieee = loadStyle("ieee");
    if (ieee) {
        csl::Result r = csl::process(*ieee, bundledLocale, lib, {citation({ref("gone1"), ref("gone2")})}, csl::Options());
        expect(r.citations.empty() ? "" : csl::plain(r.citations[0]), "@gone1?, @gone2?", "missing keys only");
        check(r.bibliography.entries.empty(), "missing keys make no entries");
        check(r.warnings.size() == 2, "a warning per missing key");
    }
}

// --- Speed ---------------------------------------------------------------------

// 300 references and 200 citations, as a long paper has them; the best of
// a few runs, in milliseconds
double timing(const csl::Style& style, int runs) {
    bib::Library lib;
    static const char* const families[] = {"Adams", "Baker", "Clark", "Doe", "Evans", "Fisher", "Garcia", "Hall",
                                           "Ito", "Jones", "Kim", "Lopez", "Miller", "Nguyen", "Okafor", "Patel"};
    static const char* const givens[] = {"Ann", "Ben", "Chris", "Dee", "Eli", "Fay", "Gus", "Hana"};
    unsigned seed = 12345;
    auto next = [&]() {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0x7FFF;
    };
    for (int i = 0; i < 300; ++i) {
        bib::Item it;
        it.id = "ref" + std::to_string(i);
        it.type = i % 5 == 0 ? "book" : i % 7 == 0 ? "chapter" : "article-journal";
        int authors = 1 + next() % 5;
        for (int a = 0; a < authors; ++a) it.names["author"].push_back(person(families[next() % 16], givens[next() % 8]));
        if (it.type == "chapter") it.names["editor"] = {person(families[next() % 16], givens[next() % 8])};
        it.fields["title"] = "A study of ink number " + std::to_string(i);
        it.fields["container-title"] = it.type == "chapter" ? "Collected essays" : "Journal of Inks";
        it.fields["volume"] = std::to_string(1 + i % 40);
        it.fields["issue"] = std::to_string(1 + i % 4);
        it.fields["page"] = std::to_string(10 + i) + "-" + std::to_string(20 + i);
        it.fields["publisher"] = "Ink Press";
        it.fields["DOI"] = "10.1000/ink." + std::to_string(i);
        it.dates["issued"] = day(1990 + (int)(next() % 35));
        lib.add(std::move(it));
    }
    std::vector<cite::Citation> citations;
    for (int c = 0; c < 200; ++c) {
        std::vector<cite::Item> items;
        int count = 1 + next() % 3;
        for (int k = 0; k < count; ++k) {
            std::string id = "ref" + std::to_string(next() % 300);
            items.push_back(ref(id.c_str(), k == 0 && c % 4 == 0 ? "12" : ""));
        }
        citations.push_back(citation(std::move(items), c % 10 == 0));
    }
    csl::Options options;
    options.nocite = {"*"};
    double best = 1e9;
    for (int r = 0; r < runs; ++r) {
        auto start = std::chrono::steady_clock::now();
        csl::Result result = csl::process(style, bundledLocale, lib, citations, options);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        best = std::min(best, ms);
        check(result.citations.size() == 200 && result.bibliography.entries.size() == 300, "the timed run formats");
    }
    return best;
}

void speedTests() {
    std::cout << "200 citations of 300 references:";
    for (const char* name : {"apa", "chicago-author-date", "ieee", "nature",
                             "china-national-standard-gb-t-7714-2015-numeric"}) {
        auto style = loadStyle(name);
        if (!style) continue;
        double ms = timing(*style, 7);
        std::printf(" %s %.1f ms;", name, ms);
        // Generous, to catch a slide without failing a busy machine
        check(ms < 40, std::string(name) + ": formats a long paper in time");
    }
    std::cout << '\n';
}
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--dump") dump.open(argv[2], std::ios::binary);
    xmlTests();
    readingTests();
    bundledStyles();
    localeTests();
    nameTests();
    pageTests();
    numberTests();
    caseTests();
    dateTests();
    richTextTests();
    citationTests();
    speedTests();
    std::cout << "csl_tests: " << checks - failures << "/" << checks << " checks passed\n";
    return failures ? 1 : 0;
}
