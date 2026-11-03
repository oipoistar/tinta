#pragma once
// Pandoc's citation syntax: [@key], [see @key, p. 4; @other], [-@key] and
// @key in running text, with pandoc-crossref's @fig:id and friends
// (docs/specs/scholar.md). Plain text only.
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace cite {

struct Item {
    std::string id;
    std::string prefix;    // "see" in [see @key], trimmed, Markdown as written
    std::string locator;   // "4" in [@key, p. 4]; "33-35" in [@key, pp. 33-35]
    std::string label;     // the locator's CSL type: page, chapter, section...
    std::string suffix;    // ", emphasis added": what follows the locator
    bool suppressAuthor = false;  // [-@key]
};

struct Citation {
    std::vector<Item> items;
    bool authorInText = false;  // @key, or @key [p. 4], outside brackets
    size_t begin = 0, end = 0;  // the bytes it spans in the searched text
};

// Every citation in a run of inline text, in order. Bracketed citations
// always count. A bare @key counts only when `cites` says so: pandoc cites
// every one, but Tinta asks the library, so an @mention without a
// reference stays text. An @ inside a word (an e-mail address) never cites.
std::vector<Citation> find(std::string_view text,
                           const std::function<bool(std::string_view key)>& cites);

// Splits what follows a key into locator, label and suffix as pandoc does:
// ", p. 4" -> page 4; ", chap. 2, n. 3" -> chapter 2 and ", n. 3"; ", 33"
// -> page 33; ", {xiv}" -> page xiv
void splitLocator(std::string_view afterKey, Item& item);

// A pandoc-crossref reference rather than a citation: fig:, tbl:, eq:, sec:
// and lst: prefixes
bool isCrossReference(std::string_view key);
}
