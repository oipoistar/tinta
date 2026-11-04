#ifndef TINTA_PAPER_H
#define TINTA_PAPER_H

// Paper export (docs/specs/scholar.md, phase 3): a paper's front matter in
// pandoc's variables, so the file builds the same with pandoc's LaTeX. Print,
// PDF and Word set a title block from it; print takes its class options,
// paper and type size. The screen keeps the properties strip.

#include "markdown.h"

#include <string>
#include <vector>

struct App;

namespace paper {

struct Front {
    std::string title, subtitle, shortTitle, date, abstract, thanks, lang;
    std::vector<std::string> authors, institutes, keywords;
    bool twoColumn = false;    // classoption: twocolumn
    std::string paperSize;     // papersize: a4, letter, legal or a3; else empty
    float fontSizePt = 0;      // fontsize: 10pt, 11pt, 12pt...; 0 when unset
    float marginMm = 0;        // geometry: margin=2cm; 0 when unset or unread

    // A title with authors or an abstract makes a title block
    bool titleBlock() const { return !title.empty() && (!authors.empty() || !abstract.empty()); }
};

// Reads the keys above from the YAML between the front matter's fences:
// plain, quoted and block scalars (| and >), flow and block lists, and
// pandoc's author objects (their name)
Front read(const std::string& yaml);
// The front matter of a parsed document, from its Properties element
Front frontOf(const qmd::ElementPtr& root);

// "Ana Okafor and Lars Lindqvist", "A, B and C", in the document's language
std::string joinAuthors(const std::vector<std::string>& authors, const std::string& lang);
// A running head's right side: the first author's surname, et al. for more
std::string headAuthors(const std::vector<std::string>& authors, const std::string& lang);
// Inline Markdown as plain text: "On *ink*" reads "On ink"
std::string plainText(const std::string& markdown);
// Index into PRINT_PAPERS of the paper named, or -1
int paperIndex(const Front& front);

// The words a paper's apparatus takes in the document's language, UTF-8:
// "Abstract", "Keywords", the comma between authors and the word before
// the last, et al., the colon after Keywords and the mark between them
struct Words {
    const char *lang, *abstract, *keywords, *comma, *andWord, *etAl, *colon, *between;
};
const Words& wordsFor(const std::string& lang);

}  // namespace paper

// Lays a paper's front matter out as its title block (title, subtitle,
// authors, institutes, date, abstract, keywords, thanks) while print asks
// for one (app.paperPrint.titleBlock), across app.paperPrint.width so it
// spans both columns. False leaves the front matter to the strip.
bool layoutTitleBlock(App& app, const qmd::ElementPtr& elem, float& y, float indent, float maxWidth);

#endif  // TINTA_PAPER_H
