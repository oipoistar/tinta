#pragma once
// Scholar (docs/specs/scholar.md): what a parsed document's figures, tables,
// equations, citations and references become, worked out once per parse
// from its front matter.
#include "markdown.h"

#include <string>
#include <vector>

namespace scholar {

// The words a document numbers with: pandoc-crossref's front matter keys
// (figureTitle, tableTitle, titleDelim, figPrefix, tblPrefix, eqnPrefix),
// else its lang:'s defaults, else English
struct Words {
    std::string figure = "Figure", table = "Table", delim = ":";
    std::string fig[2] = {"fig.", "figs."};
    std::string tbl[2] = {"tbl.", "tbls."};
    std::string eq[2] = {"eq.", "eqns."};
};
Words wordsFor(const qmd::ElementPtr& root);

// Numbers figures, tables and equations in document order and labels their
// captions. folder is where the document's relative paths start (empty for
// an untitled note). Returns the problems found, one line each
std::vector<std::string> process(const qmd::ElementPtr& root, const std::wstring& folder);

}
