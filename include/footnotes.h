#pragma once
#include "markdown.h"
#include <unordered_map>

namespace qmd {
struct FootnoteSource {
    std::string label, body;
    std::vector<size_t> offsets;
    size_t sourceOffset = 0;
    int number = 0, references = 0;
};
struct FootnoteData {
    std::string source;
    std::vector<FootnoteSource> definitions;
    std::unordered_map<std::string, size_t> byLabel;
    std::vector<size_t> order;
};
FootnoteData extractFootnotes(const std::string& source);
void appendFootnoteText(Element* parent, const std::string& text,
                        const std::vector<size_t>& literalBrackets, FootnoteData& notes);
void appendFootnotes(const ElementPtr& root, FootnoteData& notes,
                     const std::function<ParseResult(const std::string&)>& parseBody);
}
