// Scholar (docs/specs/scholar.md) as the parser and scholar.cpp leave a
// document: numbered figures, captioned tables, equations named and
// numbered, in the fixtures' mixed documents.
#include "document.h"
#include "scholar.h"

#include <algorithm>
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
    check(figures.size() == 1 && plain(captionOf(figures[0])) == "\xE5\x9B\xBE 1\xEF\xBC\x9A\xE7\xAC\x94\xE5\xB0\x96",
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
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

ElementPtr bibliographyOf(const ElementPtr& root) {
    auto lists = all(root, ElementType::Bibliography);
    return lists.empty() ? nullptr : lists.front();
}

bool listsKey(const ElementPtr& bibliography, const std::string& key) {
    if (!bibliography) return false;
    for (const auto& entry : bibliography->children)
        if (entry->title == "ref-" + key) return true;
    return false;
}

void citationsInAPaper(const fs::path& fixtures) {
    auto result = parse(fixtures / "paper.md");
    check(result.success, "paper.md parses");
    for (const auto& problem : result.problems) check(false, "paper.md has no problems: " + problem);
    auto citations = all(result.root, ElementType::Citation);
    check(citations.size() == 17, "the paper's 17 citations are found, got " + std::to_string(citations.size()));
    if (citations.empty()) return;
    const std::string first = plain(citations[0]);
    check(contains(first, "Knuth") && contains(first, "1984") && contains(first, "Brooks") && first[0] == '(',
          "APA cites by author and year in parentheses: " + first);
    check(citations[0]->url == "#ref-knuth1984" && citations[0]->language == "knuth1984 brooks1995" &&
              citations[0]->title == "cite-1",
          "a citation goes to its first reference, knows its keys and has its own anchor");
    const std::string inText = plain(citations[1]);
    check(contains(inText, "Okafor") && contains(inText, "2021") && inText[0] != '(',
          "@okafor2021 reads as the author in the sentence: " + inText);
    bool inTable = false, inFootnote = false, inQuote = false;
    for (const auto& c : citations) {
        for (const qmd::Element* up = c->parent; up; up = up->parent) {
            inTable |= up->type == ElementType::TableCell;
            inFootnote |= up->type == ElementType::FootnoteDefinition;
            inQuote |= up->type == ElementType::BlockQuote;
        }
    }
    check(inTable && inFootnote && inQuote, "citations in a table, a footnote and a quote are found");
    for (const auto& text : all(result.root, ElementType::Text)) {
        check(!contains(text->text, "[@") && !contains(text->text, "@knuth"), "no citation is left as text: " + text->text);
    }
    bool mail = false, mention = false;
    for (const auto& elem : all(result.root, ElementType::Link)) mail |= contains(elem->url, "okafor@example.org");
    for (const auto& text : all(result.root, ElementType::Text)) mention |= contains(text->text, "@inkfan");
    check(mail && mention, "an e-mail address and an @mention without a reference stay what they were");

    auto refs = all(result.root, ElementType::CrossReference);
    std::vector<std::string> reads;
    for (const auto& r : refs) reads.push_back(plain(r));
    const std::string nbsp = "\xC2\xA0";
    auto has = [&](const std::string& want) { return std::find(reads.begin(), reads.end(), want) != reads.end(); };
    check(has("eq." + nbsp + "2") && has("fig." + nbsp + "1") && has("tbl." + nbsp + "1") &&
              has("eqns." + nbsp + "1, 2") && has("Fig." + nbsp + "1"),
          "@eq:rise, @fig:nib, @tbl:inks, [@eq:flow; @eq:rise] and @Fig:nib read as pandoc-crossref reads them");
    for (const auto& r : refs) check(r->url.size() > 1 && r->url[0] == '#', "a reference goes to its target: " + plain(r));

    ElementPtr list = bibliographyOf(result.root);
    check(list && list->language == "hanging" && list->children.size() == 21,
          "the references: one entry per work cited, hanging (APA)");
    for (const char* key : {"knuth1984", "consortium2022", "zhang2020"}) {
        check(listsKey(list, key) == (std::string(key) != "zhang2020"), std::string("the references list ") + key + " only if cited");
    }
    const auto& top = result.root->children;
    size_t at = 0;
    while (at < top.size() && top[at] != list) ++at;
    check(at >= 1 && at < top.size() && top[at - 1]->type == ElementType::Heading && plain(top[at - 1]) == "References",
          "reference-section-title heads them");
    check(at + 1 < top.size() && top[at + 1]->type == ElementType::Footnotes, "and they come before the footnotes");
}

void citationsInChinese(const fs::path& fixtures) {
    auto result = parse(fixtures / "paper-zh.md");
    for (const auto& problem : result.problems) check(false, "paper-zh.md has no problems: " + problem);
    ElementPtr list = bibliographyOf(result.root);
    check(list && list->language == "field" && !list->children.empty() && list->children[0]->text == "[1]",
          "GB/T 7714 numbers its references in a column");
    auto citations = all(result.root, ElementType::Citation);
    check(!citations.empty() && contains(plain(citations[0]), "1"), "and cites by number: " +
          (citations.empty() ? std::string() : plain(citations[0])));
    auto refs = all(result.root, ElementType::CrossReference);
    bool chinese = false;
    for (const auto& r : refs) chinese |= plain(r) == "\xE5\xBC\x8F\xC2\xA0" "1" || plain(r) == "\xE5\x9B\xBE\xC2\xA0" "1";
    check(chinese, "lang: zh words the references in Chinese");
}

void citationsWithout(const fs::path& fixtures) {
    // No bibliography: @ is text, but figures are still referred to
    auto plainDoc = parseText("Write to ana@example.org or [@knuth1984] @knuth1984.\n\n"
                              "![A nib](nib.png){#fig:nib}\n\nSee @fig:nib.\n");
    check(all(plainDoc.root, ElementType::Citation).empty(), "without a bibliography nothing cites");
    auto refs = all(plainDoc.root, ElementType::CrossReference);
    check(refs.size() == 1 && plain(refs[0]) == "fig.\xC2\xA0" "1", "but @fig:id still refers to its figure");
    check(all(plainDoc.root, ElementType::Bibliography).empty(), "and no references are listed");

    // Problems are reported with where they are, and nothing stops reading
    const std::string folder = fixtures.string();
    qmd::MarkdownParser parser;
    auto broken = parseDocument(parser, "---\nbibliography: broken.bib\ncsl: no-such-style\n---\n\n"
                                        "A missing key [@nobody] and a known one [@good2020].\n\n@fig:missing\n",
                                (fixtures / "draft.md").wstring());
    bool line = false, style = false, missing = false, figure = false;
    for (const auto& p : broken.problems) {
        line |= contains(p, "broken.bib, line ");
        style |= contains(p, "no-such-style");
        missing |= contains(p, "nobody");
        figure |= contains(p, "fig:missing");
    }
    check(line && style && missing && figure,
          "a broken library, an unknown style, a missing key and a missing figure are each reported");
    auto cites = all(broken.root, ElementType::Citation);
    check(cites.size() == 2, "both citations still format");
    check(cites.size() == 2 && cites[0]->level == 1 && cites[1]->level == 0,
          "the one with a missing key is marked for the warning colour, the other not");
    auto missingRef = all(broken.root, ElementType::CrossReference);
    check(missingRef.size() == 1 && missingRef[0]->level == 1, "and so is a reference to a missing figure");
    check(bibliographyOf(broken.root) != nullptr, "with the references of the keys that exist");
}
void sections(const fs::path& fixtures) {
    auto result = parse(fixtures / "sections.md");
    for (const auto& problem : result.problems) check(false, "sections.md has no problems: " + problem);
    std::vector<std::string> heads;
    for (const auto& block : result.root->children)
        if (block->type == ElementType::Heading) heads.push_back(plain(block));
    const std::vector<std::string> want = {"Numbered sections", "1 Method", "1.1 Apparatus", "2 Results",
                                           "Acknowledgements", "Appendix", "3 Sets {a, b}"};
    check(heads == want, "sections are numbered from the second level, the title, {-} and .unnumbered left out");
    for (size_t i = 0; i < heads.size() && i < want.size(); ++i)
        if (heads[i] != want[i]) check(false, "  heading " + std::to_string(i) + " reads: " + heads[i]);
    bool marked = false;
    for (const auto& block : result.root->children) {
        if (block->type == ElementType::Heading && block->title == "sec:method") {
            marked = !block->children.empty() && block->children[0]->language == "section-number";
        }
    }
    check(marked, "the number is marked, and the heading keeps its {#sec:method} id");
    std::vector<std::string> reads;
    for (const auto& r : all(result.root, ElementType::CrossReference)) reads.push_back(plain(r));
    const std::string nbsp = "\xC2\xA0";
    const std::vector<std::string> refs = {"sec." + nbsp + "1", "sec." + nbsp + "2", "Appendix",
                                           "secs." + nbsp + "1, 2", "Sec." + nbsp + "1.1"};
    check(reads == refs, "@sec: reads the section's number, or the title of one left unnumbered");

    // Without numbersections, a reference reads as the section's title
    auto loose = parseText("## Method {#sec:method}\n\nText.\n\n## Results\n\nSee @sec:method.\n");
    auto looseRefs = all(loose.root, ElementType::CrossReference);
    check(looseRefs.size() == 1 && plain(looseRefs[0]) == "Method", "unnumbered, @sec:method reads Method");
    for (const auto& block : loose.root->children)
        if (block->type == ElementType::Heading) check(plain(block).find("1") == std::string::npos, "and nothing is numbered");
}
}  // namespace

int main() {
    const fs::path fixtures = TINTA_SCHOLAR_FIXTURES;
    figuresAndTables(fixtures);
    wordsFromFrontMatter();
    citationsInAPaper(fixtures);
    citationsInChinese(fixtures);
    citationsWithout(fixtures);
    sections(fixtures);
    std::cout << "Scholar: " << failures << " failures\n";
    return failures ? 1 : 0;
}
