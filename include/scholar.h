#pragma once
// Scholar (docs/specs/scholar.md): what a parsed document's figures, tables,
// equations, citations and references become, worked out once per parse
// from its front matter.
#include "markdown.h"

#include <memory>
#include <string>
#include <vector>

namespace bib { struct Library; }
namespace csl { struct Style; }

namespace scholar {

// The words a document numbers with: pandoc-crossref's front matter keys
// (figureTitle, tableTitle, titleDelim, figPrefix, tblPrefix, eqnPrefix,
// secPrefix), else its lang:'s defaults, else English
struct Words {
    std::string figure = "Figure", table = "Table", delim = ":";
    std::string fig[2] = {"fig.", "figs."};
    std::string tbl[2] = {"tbl.", "tbls."};
    std::string eq[2] = {"eq.", "eqns."};
    std::string sec[2] = {"sec.", "secs."};
};
Words wordsFor(const std::vector<fm::Property>& front);

// What a document cites with, read from its front matter before the parse,
// since a bare @key cites only a key its library holds
struct Session {
    bool cites = false;  // it names a bibliography
    std::shared_ptr<const bib::Library> library;
    std::shared_ptr<const csl::Style> style;
    std::string lang, sectionTitle;
    std::vector<std::string> nocite;
    bool linkCitations = true;
    std::vector<std::wstring> files;  // its bibliographies and style file, to follow
    std::vector<std::string> problems;
};
// folder is where the document's relative paths start (empty for an
// untitled note)
Session begin(const std::vector<fm::Property>& front, const std::wstring& folder);
// What in a run of the document's text cites, for the parser
qmd::CitationFinder finder(const Session& session);
// Numbers figures, tables and equations, resolves @fig:id and friends,
// formats the citations and appends the references. Returns the problems
// found, a line each
std::vector<std::string> finish(const qmd::ElementPtr& root, const Session& session);

// The files a parsed document cites from, its bibliographies and a style
// file, followed as the document is: a library saved by Zotero reformats
// its citations
std::vector<std::wstring> followedFiles(const qmd::ElementPtr& root, const std::wstring& folder);

// A row of the citation list Live and Source open as a key is typed
// (docs/citations.md): the key or id, and what it names
struct Suggestion {
    std::wstring key, detail;  // detail: "Doe et al. 2024, Ink Flow in..." or a caption
};
// What completes the key typed in text[from, caret), after its @. After
// fig:, tbl:, eq: or sec:, the document's own ids of that kind, in order;
// otherwise the keys of the bibliography its properties name, matched by
// key, author, year and title words, best first. `folder` is the
// document's, where its bibliography is found
std::vector<Suggestion> suggest(const std::wstring& text, size_t from, size_t caret, const std::wstring& folder);

}
