#pragma once
// References for citations: BibTeX, BibLaTeX and CSL-JSON read into the CSL
// data model that csl.h formats (docs/specs/scholar.md). Plain data and text
// only.
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bib {

// A person or an organisation, split as CSL splits names. Plain UTF-8 text,
// not rich text. BibTeX's "and others" reads as a last name whose literal is
// "others": et al.
struct Name {
    std::string family, given;
    std::string droppingParticle;     // "de" in "Jean de La Fontaine": BibTeX's von part
    std::string nonDroppingParticle;  // "van" in "van Gogh": von with useprefix=true
    std::string suffix;               // "Jr."
    std::string literal;              // an organisation: never split or shortened
    bool commaSuffix = false;         // "Smith, Jr., John" keeps its comma
    bool staticOrdering = false;      // family name first even in display order
};

// One point in time. Year 0 means absent; years before 1 CE are negative
struct DateParts {
    int year = 0, month = 0, day = 0;
};

// A date, a range (to.year set, or openEnd), or text that is no date
struct Date {
    DateParts from, to;
    int season = 0;        // 1 spring, 2 summer, 3 autumn, 4 winter
    bool circa = false;    // ~ or ? in EDTF, circa in CSL-JSON
    bool openEnd = false;  // a range still running: 2020/.. reads "2020-"
    std::string literal;   // printed as written when the parts are empty
    bool empty() const { return from.year == 0 && literal.empty(); }
};

// One reference, found by its citation key
struct Item {
    std::string id;
    std::string type = "document";  // a CSL type: article-journal, book, chapter...
    // Standard and number variables under their CSL names, as CSL rich text:
    // <i>, <b>, <sup>, <sub>, <span style="font-variant:small-caps;"> and
    // <span class="nocase"> (from BibTeX braces) are the only markup, and
    // &amp; &lt; &gt; the only entities
    std::map<std::string, std::string> fields;
    std::map<std::string, std::vector<Name>> names;  // author, editor, translator...
    std::map<std::string, Date> dates;               // issued, accessed...
    std::string source;  // the file it came from
    int line = 0;        // the line its entry starts on there

    const std::string* field(std::string_view variable) const;
    const std::vector<Name>* namesOf(std::string_view variable) const;
    const Date* date(std::string_view variable) const;
};

struct Diagnostic {
    std::string source;
    int line = 0;
    std::string message;
};

struct Library {
    std::vector<Item> items;
    std::vector<Diagnostic> diagnostics;

    // Keys compare exactly, as pandoc's do
    const Item* find(std::string_view id) const;
    // Appends the item; a key already present is reported and the first kept
    void add(Item item);

private:
    std::unordered_map<std::string, size_t> index;
};

// Read every entry that parses; problems go to library.diagnostics with the
// line they are on
void readBibtex(std::string_view text, std::string_view source, Library& library);
void readCslJson(std::string_view text, std::string_view source, Library& library);
// Chooses by extension: .bib and .bibtex read as BibTeX, .json as CSL-JSON.
// False for any other extension
bool readByExtension(std::string_view text, std::string_view path, Library& library);

// The pieces, for tests and for the citation completion list
std::vector<Name> parseBibtexNames(std::string_view field);
// LaTeX to CSL rich text: {\"o} and \"{o} become o-umlaut, \emph{x} <i>x</i>,
// {DNA} <span class="nocase">DNA</span>, -- an en dash, ~ a no-break space
std::string latexToRich(std::string_view tex);
// EDTF and the forms BibLaTeX dates take: 2024, 2024-03-01, 2024-03/2024-05,
// 2024~ (circa), 2024-21 (spring)
bool parseDate(std::string_view text, Date& date);
}
