// Scholar (docs/specs/scholar.md) as the parser and scholar.cpp leave a
// document: numbered figures, captioned tables, equations named and
// numbered, in the fixtures' mixed documents.
#include "document.h"
#include "scholar.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using qmd::ElementPtr;
using qmd::ElementType;

int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

qmd::ParseResult parse(const fs::path& file) {
    qmd::MarkdownParser parser;
    return parseDocument(parser, readFile(file), file.wstring());
}

qmd::ParseResult parseText(const std::string& text) {
    qmd::MarkdownParser parser;
    return parseDocument(parser, text, std::wstring(L"C:\\notes\\untitled.md"));
}

void collect(const ElementPtr& elem, ElementType type, std::vector<ElementPtr>& out) {
    if (elem->type == type) out.push_back(elem);
    for (const auto& child : elem->children) collect(child, type, out);
}
std::vector<ElementPtr> all(const ElementPtr& root, ElementType type) {
    std::vector<ElementPtr> out;
    collect(root, type, out);
    return out;
}

// The text an element reads as, its inline children flattened
std::string plain(const ElementPtr& elem) {
    if (elem->type == ElementType::Text) return elem->text;
    std::string out;
    for (const auto& child : elem->children) out += plain(child);
    return out;
}

ElementPtr captionOf(const ElementPtr& figure) {
    for (const auto& child : figure->children)
        if (child->type == ElementType::Caption) return child;
    return nullptr;
}

void figuresAndTables(const fs::path& fixtures) {
    auto result = parse(fixtures / "crossref.md");
    check(result.success, "crossref.md parses");
    auto figures = all(result.root, ElementType::Figure);
    check(figures.size() == 5, "five figures and captioned tables, got " + std::to_string(figures.size()));
    if (figures.size() != 5) return;
    check(figures[0]->language == "fig" && figures[0]->title == "fig:nib" && figures[0]->level == 1,
          "the nib is figure 1");
    auto caption = captionOf(figures[0]);
    check(caption && plain(caption) == "Figure 1: The nib, split along its slit, with the breather hole",
          "its caption is its alt text after its label: " + (caption ? plain(caption) : std::string()));
    check(caption && caption->children.size() > 1 && caption->children[0]->type == ElementType::Strong,
          "the label is bold");
    check(figures[0]->children[0]->type == ElementType::Paragraph &&
              figures[0]->children[0]->children.size() == 1 &&
              figures[0]->children[0]->children[0]->type == ElementType::Image,
          "the figure holds the image alone, its {#fig:...} gone");
    check(figures[1]->language == "tbl" && figures[1]->title == "tbl:density" && figures[1]->level == 1 &&
              figures[1]->children.size() == 2 && figures[1]->children[0]->type == ElementType::Caption &&
              figures[1]->children[1]->type == ElementType::Table,
          "a Table: caption above its table makes table 1, caption first");
    check(plain(captionOf(figures[1])) == "Table 1: Ink densities by brand, at 20 \xC2\xB0" "C",
          "its caption loses Table: and its id: " + plain(captionOf(figures[1])));
    check(figures[2]->title == "tbl:paper" && figures[2]->level == 2 &&
              figures[2]->children[0]->type == ElementType::Table &&
              figures[2]->children[1]->type == ElementType::Caption,
          "a : caption below its table makes table 2, caption last");
    check(figures[3]->title.empty() && figures[3]->level == 0 && plain(captionOf(figures[3])) == "Without a number",
          "a caption with no id keeps its words, unnumbered");
    check(figures[4]->title == "fig:bars" && figures[4]->level == 2, "the bars are figure 2");
    check(all(result.root, ElementType::Image).size() == 3, "the plain picture stays an image");

    auto equations = all(result.root, ElementType::MathDisplay);
    check(equations.size() == 3, "three display equations");
    if (equations.size() == 3) {
        check(equations[0]->title == "eq:tension" &&
                  equations[0]->text.find("\\tag{1}\\label{eq:tension}") != std::string::npos,
              "{#eq:tension} numbers the first equation 1 and names it");
        check(equations[1]->title == "eq:rise" && equations[1]->text.find("\\tag{2}") != std::string::npos,
              "the one in the quote is 2");
        check(equations[2]->title == "eq:poiseuille" && equations[2]->text.find("\\tag{P}") != std::string::npos &&
                  equations[2]->text.find("\\tag{3}") == std::string::npos &&
                  equations[2]->text.find("\\label{eq:poiseuille}") != std::string::npos,
              "one tagged by hand keeps its tag and is named");
    }
    for (const auto& text : all(result.root, ElementType::Text)) {
        check(text->text.find("{#") == std::string::npos, "no attribute is left as text: " + text->text);
    }
}

void wordsFromFrontMatter() {
    auto zh = parseText("---\nlang: zh-CN\n---\n\n![\xE7\xAC\x94\xE5\xB0\x96](nib.png){#fig:nib}\n");
    auto figures = all(zh.root, ElementType::Figure);
    check(figures.size() == 1 && plain(captionOf(figures[0])) == "\xE5\x9B\xBE 1\xEF\xBC\x9A \xE7\xAC\x94\xE5\xB0\x96",
          "lang: zh numbers in Chinese: " + (figures.empty() ? std::string() : plain(captionOf(figures[0]))));
    auto own = parseText("---\nfigureTitle: Fig.\ntitleDelim: \" -\"\n---\n\n![A nib](nib.png){#fig:nib}\n");
    figures = all(own.root, ElementType::Figure);
    check(figures.size() == 1 && plain(captionOf(figures[0])) == "Fig. 1 - A nib",
          "figureTitle and titleDelim name the label: " + (figures.empty() ? std::string() : plain(captionOf(figures[0]))));
    auto inline_ = parseText("Text ![a nib](nib.png){#fig:nib} in a sentence stays text.\n");
    check(all(inline_.root, ElementType::Figure).empty(), "an image inside a sentence is no figure");
    auto noTable = parseText("Table: a caption with no table after it.\n\nSome text.\n");
    check(all(noTable.root, ElementType::Figure).empty() && all(noTable.root, ElementType::Caption).empty(),
          "Table: without a table stays a paragraph");
}
}  // namespace

int main() {
    const fs::path fixtures = TINTA_SCHOLAR_FIXTURES;
    figuresAndTables(fixtures);
    wordsFromFrontMatter();
    std::cout << "Scholar: " << failures << " failures\n";
    return failures ? 1 : 0;
}
