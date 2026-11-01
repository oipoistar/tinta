#include "document.h"

#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

std::string readFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// The text with every line break written as `eol`
std::string withEol(const std::string& text, const std::string& eol) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        if (text[i] == '\n') out += eol;
        else out += text[i];
    }
    return out;
}

// The parsed tree as text, every source offset taken `shift` bytes back:
// a document behind a byte order mark dumps at 3 as it does at 0 without
std::string dumpTree(const qmd::ElementPtr& root, size_t shift) {
    std::string out;
    auto at = [&](size_t offset) {
        return offset == SIZE_MAX ? std::string("-")
                                  : std::to_string((long long)offset - (long long)shift);
    };
    std::function<void(const qmd::ElementPtr&, int)> walk =
        [&](const qmd::ElementPtr& e, int depth) {
        out += std::string(depth * 2, ' ') + qmd::elementTypeToString(e->type) +
               " @" + at(e->sourceOffset) + " level=" + std::to_string(e->level) +
               " list=" + std::to_string(e->ordered) + "/" + std::to_string(e->start) +
               " task=" + std::to_string(e->isTask) + std::to_string(e->taskChecked) +
               "@" + at(e->taskMarkOffset) + " lang=" + e->language +
               " align=" + std::to_string(e->align) + " cols=" + std::to_string(e->col_count) +
               " alert=" + std::to_string(e->alertKind) + " url=" + e->url +
               " title=" + e->title + " text=[" + e->text + "]\n";
        for (const auto& p : e->properties) {
            out += std::string(depth * 2 + 2, ' ') + p.key + "=" + p.text + " @" +
                   at(p.valueStart) + "-" + at(p.valueEnd) + "\n";
        }
        for (const auto& child : e->children) walk(child, depth + 1);
    };
    if (root) walk(root, 0);
    return out;
}

} // namespace

int main() {
    check(isSupportedDocumentPath("notes.md"), ".md is supported");
    check(isSupportedDocumentPath("notes.MARKDOWN"), ".markdown is case-insensitive");
    check(isSupportedDocumentPath(L"diagram.MMD"), ".mmd is case-insensitive");
    check(isMermaidDocumentPath("diagram.mmd"), ".mmd is detected as Mermaid");
    check(!isMermaidDocumentPath("notes.md"), ".md is not detected as Mermaid");
    check(!isSupportedDocumentPath("diagram.mmdd"), "similar extensions are rejected");
    check(isSupportedDocumentPath("notes.txt"), ".txt opens as a plain-text document");
    check(isSupportedDocumentPath("data.JSON"), ".json is case-insensitive");
    check(isPlainTextDocumentPath(L"config.yaml"), ".yaml is plain text");
    check(!isPlainTextDocumentPath("notes.md"), ".md is not plain text");
    check(isSupportedDropPath(L"notes.txt"), "existing .txt drag-and-drop remains supported");

    qmd::MarkdownParser parser;
    auto mermaid = parseDocument(
        parser, "flowchart LR\nA --> B\n", "diagram.mmd");
    check(mermaid.success, "Mermaid document is created");
    check(mermaid.root && mermaid.root->children.size() == 1,
          "Mermaid document has one diagram element");
    if (mermaid.root && mermaid.root->children.size() == 1) {
        check(mermaid.root->children[0]->type == qmd::ElementType::MermaidDiagram,
              ".mmd content becomes a Mermaid diagram element");
    }

    // Standalone PlantUML documents open as a plantuml code block so the
    // fence render paths (preview, HTML, DOCX) pick them up unchanged
    check(isPlantUmlDocumentPath("diagram.puml"), ".puml is detected as PlantUML");
    check(isPlantUmlDocumentPath(L"diagram.PUML"), ".PUML is case-insensitive");
    check(isPlantUmlDocumentPath(L"diagram.PlantUML"), ".PlantUML is case-insensitive");
    check(!isPlantUmlDocumentPath("notes.md"), ".md is not detected as PlantUML");
    check(!isPlainTextDocumentPath("diagram.puml"), ".puml is not plain text");
    check(isSupportedDocumentPath("diagram.puml"), ".puml is supported");
    check(isSupportedDocumentPath(L"diagram.plantuml"), ".plantuml is supported");
    check(!isSupportedDocumentPath("diagram.puml2"), "similar extensions are rejected");
    check(!isPlantUmlDocumentPath("diagram.puml2"), ".puml2 is not detected");
    check(isSupportedDropPath(L"diagram.puml"), ".puml drag-and-drop is supported");

    auto puml = parseDocument(parser, "@startuml\nA -> B\n@enduml\n", "diagram.puml");
    check(puml.success, "PlantUML document is created");
    check(puml.root && puml.root->children.size() == 1,
          "PlantUML document has one block");
    if (puml.root && puml.root->children.size() == 1) {
        const auto& block = puml.root->children[0];
        check(block->type == qmd::ElementType::CodeBlock,
              ".puml content becomes a code block");
        check(block->language == "plantuml",
              "code block carries the plantuml language tag");
        check(!block->children.empty() &&
                  block->children[0]->text == "@startuml\nA -> B\n@enduml\n",
              "PlantUML source is preserved verbatim");
    }
    auto plantuml = parseDocument(
        parser, "@startuml\nAlice -> Bob\n@enduml\n", L"diagram.plantuml");
    check(plantuml.success && plantuml.root &&
              plantuml.root->children.size() == 1 &&
              plantuml.root->children[0]->type == qmd::ElementType::CodeBlock &&
              plantuml.root->children[0]->language == "plantuml",
          ".plantuml parses through the wide-path overload");

    // Obsidian/Typora inline extensions
    auto ext = parseDocument(parser,
        "before ==mark 中文== mid x^2^ and H~2~O ~~gone~~ `==not this==`\n", "notes.md");
    check(ext.success, "extension test parses");
    if (ext.success && !ext.root->children.empty()) {
        const auto& para = ext.root->children[0];
        int highlights = 0, sups = 0, subs = 0, codeIntact = 0, strikes = 0;
        for (const auto& child : para->children) {
            if (child->type == qmd::ElementType::Highlight) {
                highlights++;
                check(!child->children.empty() &&
                      child->children[0]->text == "mark \xe4\xb8\xad\xe6\x96\x87",
                      "highlight content preserved incl. CJK");
            }
            if (child->type == qmd::ElementType::Superscript) {
                sups++;
                check(!child->children.empty() && child->children[0]->text == "2",
                      "superscript content preserved");
            }
            if (child->type == qmd::ElementType::Subscript) subs++;
            if (child->type == qmd::ElementType::Strikethrough) {
                strikes++;
                check(!child->children.empty() && child->children[0]->text == "gone",
                      "strikethrough content preserved");
            }
            if (child->type == qmd::ElementType::Code) {
                codeIntact++;
                check(!child->children.empty() &&
                      child->children[0]->text == "==not this==",
                      "code spans are not transformed");
            }
        }
        check(highlights == 1, "one ==highlight== parsed");
        check(sups == 1, "one ^sup^ parsed");
        check(subs == 1, "one ~sub~ parsed");
        check(strikes == 1, "one ~~strike~~ parsed");
        check(codeIntact == 1, "inline code untouched");
    }

    // A span the post-pass creates is split again inside, so scripts and
    // marks nest in each other: ~~struck x^2^~~ kept literal carets before
    {
        std::function<std::string(const qmd::ElementPtr&)> shape =
            [&](const qmd::ElementPtr& node) -> std::string {
            if (node->type == qmd::ElementType::Text) return "'" + node->text + "'";
            if (node->type == qmd::ElementType::MathInline) return "$" + node->text + "$";
            std::string out = qmd::elementTypeToString(node->type) + "(";
            for (size_t i = 0; i < node->children.size(); i++) {
                if (i) out += ",";
                out += shape(node->children[i]);
            }
            return out + ")";
        };
        auto paragraphShape = [&](const std::string& source) {
            auto doc = parseDocument(parser, source, "notes.md");
            if (!doc.success || !doc.root || doc.root->children.empty()) return std::string("<no block>");
            return shape(doc.root->children[0]);
        };
        struct NestCase { const char* source; const char* expected; const char* message; };
        const NestCase cases[] = {
            {"~~struck x^2^~~\n",
             "Paragraph(Strikethrough('struck x',Superscript('2')))",
             "superscript nests in strikethrough"},
            {"==mark x^2^ and H~2~O==\n",
             "Paragraph(Highlight('mark x',Superscript('2'),' and H',Subscript('2'),'O'))",
             "scripts nest in a highlight"},
            {"==mark ~~gone~~ kept==\n",
             "Paragraph(Highlight('mark ',Strikethrough('gone'),' kept'))",
             "strikethrough nests in a highlight"},
            {"~~old ==mark== text~~\n",
             "Paragraph(Strikethrough('old ',Highlight('mark'),' text'))",
             "highlight nests in strikethrough"},
            {"x^a~i~^ and H~2^+^~\n",
             "Paragraph('x',Superscript('a',Subscript('i')),' and H',Subscript('2',Superscript('+')))",
             "superscript and subscript nest in each other"},
            {"~~a ~b~ c~~\n",
             "Paragraph(Strikethrough('a ',Subscript('b'),' c'))",
             "subscript nests in strikethrough"},
            // The closing run is shared: the subscript takes its first
            // tilde and the strikethrough the last two
            {"~~a ~b~~~ after\n",
             "Paragraph(Strikethrough('a ',Subscript('b')),' after')",
             "subscript closing against strikethrough shares the tilde run"},
            {"~~a~~~ after\n",
             "Paragraph(Strikethrough('a'),'~ after')",
             "a spare tilde after strikethrough stays literal"},
            {"~~x~~~y~\n",
             "Paragraph(Strikethrough('x'),Subscript('y'))",
             "a subscript right after strikethrough keeps its own tildes"},
            // Math stays one opaque node at every level, and nested
            // delimiters never reach into it
            {"==see $a^b$ and x^2^==\n",
             "Paragraph(Highlight('see ',$a^b$,' and x',Superscript('2')))",
             "math inside a highlight stays opaque beside a nested script"},
            {"~~old ==$x==y$ mark== end~~\n",
             "Paragraph(Strikethrough('old ',Highlight($x==y$,' mark'),' end'))",
             "math delimiters stay literal in a nested highlight"},
            {"==mark x^2^== `~~x^2^~~`\n",
             "Paragraph(Highlight('mark x',Superscript('2')),' ',Code('~~x^2^~~'))",
             "inline code beside a nested span stays literal"},
            {"**bold ~~struck x^2^~~**\n",
             "Paragraph(Strong('bold ',Strikethrough('struck x',Superscript('2'))))",
             "nesting works inside md4c emphasis"},
            // An inline HTML span (#263) moves whole into the nested span,
            // and a range tilde (#277) stays text at every level
            {"~~H<sub>2</sub>O and x^2^~~\n",
             "Paragraph(Strikethrough('H',Subscript('2'),'O and x',Superscript('2')))",
             "inline HTML and a script nest in strikethrough"},
            {"==50~100 mm and H~2~O==\n",
             "Paragraph(Highlight('50~100 mm and H',Subscript('2'),'O'))",
             "a range tilde stays literal in a nested highlight"},
            // md4c's spans move whole into a ~~ or == span, as GitHub's
            // strikethrough wraps them: ~~**bold**~~ kept its tildes before
            {"~~**struck bold**~~ text\n",
             "Paragraph(Strikethrough(Strong('struck bold')),' text')",
             "strikethrough wraps bold"},
            {"a mark: ==**marked bold**==.\n",
             "Paragraph('a mark: ',Highlight(Strong('marked bold')),'.')",
             "a highlight wraps bold"},
            {"~~*struck*~~ and ==*marked*==\n",
             "Paragraph(Strikethrough(Emphasis('struck')),' and ',Highlight(Emphasis('marked')))",
             "strikethrough and highlight wrap italic"},
            {"~~a **b** c~~ and ==a _b_ c==\n",
             "Paragraph(Strikethrough('a ',Strong('b'),' c'),' and ',Highlight('a ',Emphasis('b'),' c'))",
             "they wrap emphasis among text"},
            {"**~~x~~** and *==y==*\n",
             "Paragraph(Strong(Strikethrough('x')),' and ',Emphasis(Highlight('y')))",
             "emphasis still wraps them"},
            {"~~==**x**==~~ and ==~~*y*~~==\n",
             "Paragraph(Strikethrough(Highlight(Strong('x'))),' and ',Highlight(Strikethrough(Emphasis('y'))))",
             "nested spans wrap emphasis"},
            {"~~**bold ==mark== in**~~\n",
             "Paragraph(Strikethrough(Strong('bold ',Highlight('mark'),' in')))",
             "a span inside the wrapped bold stays in it"},
            {"~~[link](#a)~~, ==`code`== and ~~![alt](i.png)~~\n",
             "Paragraph(Strikethrough(Link('link')),', ',Highlight(Code('code')),' and ',Strikethrough(Image('alt')))",
             "they wrap a link, code and an image"},
            {"==**bold** x^2^ and $y$==\n",
             "Paragraph(Highlight(Strong('bold'),' x',Superscript('2'),' and ',$y$))",
             "a highlight wraps bold beside a script and math"},
            // Delimiters that open before a blank or close after one stay
            // text, as md4c reads ~~, and == is two signs alone; so do
            // escaped ones and pairs split by a span's edge
            {"`~~**x**~~` ~~**open** and ==*open*\n",
             "Paragraph(Code('~~**x**~~'),' ~~',Strong('open'),' and ==',Emphasis('open'))",
             "code and unclosed delimiters stay text"},
            {"~~ **x** ~~ and == *y* ==\n",
             "Paragraph('~~ ',Strong('x'),' ~~ and == ',Emphasis('y'),' ==')",
             "a blank just inside keeps the delimiters text"},
            {"`a` == `b` and `c` == `d`\n",
             "Paragraph(Code('a'),' == ',Code('b'),' and ',Code('c'),' == ',Code('d'))",
             "comparisons between code spans stay text"},
            {"`a` === `b` and `c` === `d`\n",
             "Paragraph(Code('a'),' === ',Code('b'),' and ',Code('c'),' === ',Code('d'))",
             "three equals signs are no delimiter"},
            {"**a** == **b** and **c** == **d**\n",
             "Paragraph(Strong('a'),' == ',Strong('b'),' and ',Strong('c'),' == ',Strong('d'))",
             "comparisons between bold words stay text"},
            {"~~a **b~~ c** and **d ==e** f==\n",
             "Paragraph('~~a ',Strong('b~~ c'),' and ',Strong('d ==e'),' f==')",
             "delimiters on both sides of a span's edge stay text"},
            {"\\~\\~**x**\\~\\~ and \\=\\=*y*\\=\\=\n",
             "Paragraph('~~',Strong('x'),'~~ and ==',Emphasis('y'),'==')",
             "escaped delimiters around emphasis stay text"},
            {"\\~\\~x\\~\\~, H\\~2\\~O and x\\^2\\^\n",
             "Paragraph('~~x~~, H~2~O and x^2^')",
             "escaped signs stay text in plain text too"},
            {"\\\\~~x~~ and a\\\\~b~\n",
             "Paragraph('\\',Strikethrough('x'),' and a\\',Subscript('b'))",
             "a sign after an escaped backslash still pairs"},
            {"x ~~ y ~~ z and a == b == c\n",
             "Paragraph('x ~~ y ~~ z and a == b == c')",
             "blank-flanked delimiters in plain text stay text"},
            {"~~a ~~ b~~ and ==a\\=== b\n",
             "Paragraph(Strikethrough('a ~~ b'),' and ',Highlight('a='),' b')",
             "a delimiter that cannot close is passed over"},
        };
        for (const auto& c : cases) {
            std::string actual = paragraphShape(c.source);
            if (actual != c.expected) {
                std::cerr << "  got      " << actual << "\n  expected " << c.expected << '\n';
            }
            check(actual == c.expected, c.message);
        }
    }

    // The mixed fixture: each Struck_ and Marked_ word sits under its
    // strikethrough or highlight, wherever it is, and the last section
    // keeps every delimiter as text
    {
        using qmd::Element;
        using qmd::ElementType;
        auto doc = parseDocument(parser, readFile(TINTA_MARKED_EMPHASIS_FIXTURE), "notes.md");
        check(doc.success && doc.root, "the struck and marked emphasis fixture parses");
        if (doc.success && doc.root) {
            auto under = [](const Element* e, ElementType type) {
                for (; e; e = e->parent)
                    if (e->type == type) return true;
                return false;
            };
            auto wordChar = [](char c) { return isalnum(static_cast<unsigned char>(c)) || c == '_'; };
            const std::string keptPrefixes[] = {"Code_", "Fence_", "Spaced_", "Compared_",
                                                "Open_", "Crossing_", "Escaped_"};
            int struck = 0, marked = 0, kept = 0, wrong = 0;
            int lastSectionSpans = 0;
            bool literalAbove = false, lastSection = false;
            std::string lastSectionText;
            std::function<void(const qmd::ElementPtr&)> walk = [&](const qmd::ElementPtr& e) {
                if (e->type == ElementType::Strikethrough || e->type == ElementType::Highlight) {
                    if (lastSection) ++lastSectionSpans;
                }
                if (e->type == ElementType::Text) {
                    const bool code = under(e.get(), ElementType::Code) || under(e.get(), ElementType::CodeBlock);
                    if (!code && !lastSection && (e->text.find("~~") != std::string::npos ||
                                                  e->text.find("==") != std::string::npos)) {
                        std::cerr << "  literal delimiter in '" << e->text << "'\n";
                        literalAbove = true;
                    }
                    if (!code && lastSection) lastSectionText += e->text;
                    const std::string& t = e->text;
                    for (size_t i = 0; i < t.size(); ++i) {
                        if (i && wordChar(t[i - 1])) continue;
                        size_t j = i;
                        while (j < t.size() && wordChar(t[j])) ++j;
                        const std::string word = t.substr(i, j - i);
                        const bool isStrike = under(e.get(), ElementType::Strikethrough);
                        const bool isMark = under(e.get(), ElementType::Highlight);
                        bool right = true;
                        if (word.rfind("Struck_", 0) == 0) { ++struck; right = isStrike; }
                        else if (word.rfind("Marked_", 0) == 0) { ++marked; right = isMark; }
                        else {
                            for (const auto& prefix : keptPrefixes) {
                                if (word.rfind(prefix, 0) != 0) continue;
                                ++kept;
                                right = !isStrike && !isMark;
                            }
                        }
                        if (!right) {
                            std::cerr << "  misplaced " << word << '\n';
                            ++wrong;
                        }
                        if (j > i) i = j - 1;
                    }
                }
                for (const auto& child : e->children) walk(child);
            };
            size_t tables = 0, quotes = 0, lists = 0, fences = 0;
            for (const auto& block : doc.root->children) {
                if (block->type == ElementType::Heading && !block->children.empty() &&
                    block->children[0]->text == "Stays as typed") {
                    lastSection = true;
                }
                tables += block->type == ElementType::Table;
                quotes += block->type == ElementType::BlockQuote;
                lists += block->type == ElementType::List;
                fences += block->type == ElementType::CodeBlock;
                // The footnotes close the page and are back to marked words
                if (block->type == ElementType::Footnotes) lastSection = false;
                walk(block);
            }
            check(struck == 22 && marked == 19, "the fixture's struck and marked words are all found");
            check(kept == 16, "the fixture's literal words are all found");
            check(wrong == 0, "every struck, marked and literal word is where it belongs");
            check(!literalAbove, "no ~~ or == stays as text above the last section");
            check(lastSectionSpans == 1, "the last section strikes only its final paragraph's word");
            check(lastSectionText.find("~~") != std::string::npos && lastSectionText.find("==") != std::string::npos,
                  "the last section keeps its delimiters as text");
            check(tables == 1 && quotes == 1 && lists == 3 && fences == 1,
                  "the fixture's table, quote, lists and fence remain");
        }
    }

    // An escaped caret is no footnote reference, as on GitHub
    {
        auto doc = parseDocument(parser, "[^n] and [\\^n]\n\n[^n]: A note.\n", "notes.md");
        size_t references = 0;
        std::function<void(const qmd::ElementPtr&)> count = [&](const qmd::ElementPtr& e) {
            references += e->type == qmd::ElementType::FootnoteReference;
            for (const auto& child : e->children) count(child);
        };
        if (doc.success && doc.root) count(doc.root);
        check(references == 1, "an escaped caret makes no footnote reference");
    }

    // GitHub alerts
    auto alert = parseDocument(parser, "> [!NOTE]\n> Body text here.\n", "notes.md");
    check(alert.success, "alert blockquote parses");
    if (alert.success && !alert.root->children.empty()) {
        const auto& quote = alert.root->children[0];
        check(quote->type == qmd::ElementType::BlockQuote, "alert stays a blockquote");
        check(quote->alertKind == 1, "[!NOTE] detected as alert kind 1");
        check(!quote->children.empty() &&
              !quote->children[0]->children.empty() &&
              quote->children[0]->children[0]->text == "Body text here.",
              "alert marker stripped, body preserved");
    }
    auto caution = parseDocument(parser, "> [!caution]\n> Careful.\n", "notes.md");
    check(caution.success && !caution.root->children.empty() &&
          caution.root->children[0]->alertKind == 5,
          "alert markers match case-insensitively");
    auto notAlert = parseDocument(parser, "> [!NOTE] trailing words\n", "notes.md");
    check(notAlert.success && !notAlert.root->children.empty() &&
          notAlert.root->children[0]->alertKind == 0,
          "marker with trailing text on the same line stays a plain quote");

    // Inline <br> becomes a hard break (#45)
    auto br = parseDocument(parser, "line one<br>line two<BR/>line three<br />end\n", "notes.md");
    check(br.success, "inline br document parses");
    if (br.success && !br.root->children.empty()) {
        int hardBreaks = 0;
        int literalBr = 0;
        for (const auto& child : br.root->children[0]->children) {
            if (child->type == qmd::ElementType::HardBreak) hardBreaks++;
            if (child->type == qmd::ElementType::Text &&
                child->text.find("<br") != std::string::npos) literalBr++;
        }
        check(hardBreaks == 3, "all three <br> variants become hard breaks");
        check(literalBr == 0, "no literal <br> text remains");
    }

    // Exported documents from office/AI tools often wrap all visible content
    // in presentational <font> tags. They must not become literal table text.
    {
        auto styledTable = parseDocument(parser,
            "# 示例测试文档\n"
            "**<font style=\"color:#000000;\">文档版本</font>**：V1.0\n\n"
            "| **<font style=\"color:rgb(0, 0, 0);\">序号</font>** | "
            "**<font style=\"color:rgb(0, 0, 0);\">功能项</font>** | "
            "**<font style=\"color:rgb(0, 0, 0);\">测试状态</font>** | "
            "**<font style=\"color:rgb(0, 0, 0);\">备注</font>** |\n"
            "| --- | --- | --- | --- |\n"
            "| <font style=\"color:rgb(0, 0, 0);\">1</font> | "
            "<font style=\"color:#000000;\">检查输入框。</font> | "
            "<font style=\"color:#000000;\">正常</font> |  |\n"
            "| <font style=\"color:rgb(0, 0, 0);\">2</font> | "
            "<font style=\"color:#000000;\">提交示例数据。</font> | "
            "<font style=\"color:#000000;\">异常</font> | "
            "<font style=\"color:#000000;\">截图</font>![](https://example.test/error.png) |\n"
            "| <font style=\"color:rgb(0, 0, 0);\">3</font> | "
            "<font style=\"color:#000000;\">空状态占位。</font> | "
            "<font style=\"color:#000000;\">正常</font> | <br/> |\n",
            "sample.md");
        check(styledTable.success, "font-wrapped Chinese table parses");
        bool sawTable = false;
        bool sawLiteralFont = false;
        bool sawHeader = false;
        bool sawImage = false;
        bool sawHardBreak = false;
        std::function<void(const qmd::ElementPtr&)> walk =
            [&](const qmd::ElementPtr& node) {
            if (node->type == qmd::ElementType::Table) sawTable = true;
            if (node->type == qmd::ElementType::Image) sawImage = true;
            if (node->type == qmd::ElementType::HardBreak) sawHardBreak = true;
            if (node->type == qmd::ElementType::Text) {
                if (node->text.find("<font") != std::string::npos ||
                    node->text.find("</font>") != std::string::npos) {
                    sawLiteralFont = true;
                }
                if (node->text == "序号") sawHeader = true;
            }
            for (const auto& child : node->children) walk(child);
        };
        if (styledTable.root) walk(styledTable.root);
        check(sawTable, "font-wrapped content remains a Markdown table");
        check(sawHeader, "font-wrapped table header text is preserved");
        check(sawImage, "remote images inside table cells are preserved");
        check(sawHardBreak, "HTML line breaks inside table cells are preserved");
        check(!sawLiteralFont, "font tags are hidden from rendered text");
    }

    // YAML frontmatter is hidden instead of rendering as a setext heading (#61)
    auto fm = parseDocument(parser,
        "---\ntitle: 'Barometer'\naliases:\ntags: [Barometer]\n---\n\n"
        "[Link](https://example.com)\n", "notes.md");
    check(fm.success, "frontmatter document parses");
    if (fm.success) {
        bool sawHeading = false, sawTitleText = false;
        std::function<void(const qmd::ElementPtr&)> walk = [&](const qmd::ElementPtr& e) {
            if (e->type == qmd::ElementType::Heading) sawHeading = true;
            if (e->type == qmd::ElementType::Text &&
                e->text.find("title:") != std::string::npos) sawTitleText = true;
            for (const auto& ch : e->children) walk(ch);
        };
        walk(fm.root);
        check(!sawHeading, "frontmatter does not become a heading");
        check(!sawTitleText, "frontmatter keys are not rendered");
        bool sawLink = false;
        std::function<void(const qmd::ElementPtr&)> findLink = [&](const qmd::ElementPtr& e) {
            if (e->type == qmd::ElementType::Link) sawLink = true;
            for (const auto& ch : e->children) findLink(ch);
        };
        findLink(fm.root);
        check(sawLink, "content after frontmatter renders");
    }
    auto unclosed = parseDocument(parser, "---\ntitle: x\nno closing fence\n", "notes.md");
    check(unclosed.success && !unclosed.root->children.empty(),
          "unclosed fence renders as ordinary markdown");
    auto hrDoc = parseDocument(parser, "para\n\n---\n\nafter\n", "notes.md");
    bool hrSurvives = false;
    if (hrDoc.success) {
        for (const auto& ch : hrDoc.root->children) {
            if (ch->type == qmd::ElementType::HorizontalRule) hrSurvives = true;
        }
    }
    check(hrSurvives, "a mid-document --- is still a horizontal rule");

    auto markdown = parseDocument(parser, "# Heading\n", "notes.md");
    check(markdown.success, "Markdown document still parses");
    check(markdown.root && !markdown.root->children.empty() &&
          markdown.root->children[0]->type == qmd::ElementType::Heading,
          ".md content keeps Markdown parsing");

    // Plain-text .md references become fileref: links (#127)
    {
        auto refs = parseDocument(parser,
            "See docs/auth.md and ./setup.md plus C:\\notes\\a.md here.\n"
            "Not these: `code.md` or https://x.com/y.md or file.mdx or "
            "bare .md or v2.md.bak\n"
            "Sentence ends with index.md.\n",
            "notes.md");
        check(refs.success, "fileref test parses");
        std::vector<std::string> found;
        std::function<void(const qmd::ElementPtr&)> walk =
            [&](const qmd::ElementPtr& node) {
            if (node->type == qmd::ElementType::Link &&
                node->url.rfind("fileref:", 0) == 0) {
                found.push_back(node->url.substr(8));
            }
            if (node->type != qmd::ElementType::Code &&
                node->type != qmd::ElementType::CodeBlock) {
                for (const auto& child : node->children) walk(child);
            }
        };
        if (refs.root) walk(refs.root);
        check(found.size() == 4, "exactly the four real references detected");
        auto has = [&](const char* token) {
            for (const auto& item : found) {
                if (item == token) return true;
            }
            return false;
        };
        check(has("docs/auth.md"), "bare relative path detected");
        check(has("./setup.md"), "dot-relative path detected");
        check(has("C:\\notes\\a.md"), "absolute path detected");
        check(has("index.md"), "sentence-ending path detected sans period");
        check(!has("code.md"), "code spans are left alone");
        check(!has("y.md"), "URLs are left alone");
        check(!has("file.mdx"), "longer extensions are not references");
        check(!has(".md"), "a bare extension is not a reference");
        check(!has("v2.md"), "dotted archive names are not references");
    }

    // Plain-text documents become one highlighted code block
    {
        auto json = parseDocument(
            parser, "{\n  \"key\": true\n}\n", "data.json");
        check(json.success, "json document parses");
        check(json.root && json.root->children.size() == 1,
              "json document is a single block");
        if (json.root && json.root->children.size() == 1) {
            const auto& block = json.root->children[0];
            check(block->type == qmd::ElementType::CodeBlock,
                  "json content becomes a code block");
            check(block->language == "json",
                  "json block carries its language tag");
            check(!block->children.empty() &&
                      block->children[0]->text.find("\"key\"") !=
                          std::string::npos,
                  "json content is preserved verbatim");
        }
        auto txt = parseDocument(parser, "# not a heading\n", "notes.txt");
        check(txt.success && txt.root && !txt.root->children.empty() &&
                  txt.root->children[0]->type ==
                      qmd::ElementType::CodeBlock,
              ".txt content stays literal instead of parsing as markdown");
    }

    // Text-file references are detected like .md ones (#7)
    {
        auto refs = parseDocument(parser,
            "Check config.yaml and data/out.json plus error.log here.\n",
            "notes.md");
        check(refs.success, "text-file ref test parses");
        std::vector<std::string> found;
        std::function<void(const qmd::ElementPtr&)> walk =
            [&](const qmd::ElementPtr& node) {
            if (node->type == qmd::ElementType::Link &&
                node->url.rfind("fileref:", 0) == 0) {
                found.push_back(node->url.substr(8));
            }
            if (node->type != qmd::ElementType::Code &&
                node->type != qmd::ElementType::CodeBlock) {
                for (const auto& child : node->children) walk(child);
            }
        };
        if (refs.root) walk(refs.root);
        check(found.size() == 3, "all three text-file references detected");
    }

    // Emoji shortcodes become their emoji, code stays literal
    {
        auto emoji = parseDocument(parser,
            "Ship it :rocket: with :+1: but `:tada:` stays and "
            ":not_a_real_code: survives\n"
            "```\n:fire: in a fence\n```\n",
            "notes.md");
        check(emoji.success, "emoji test parses");
        std::string flat;
        std::string codeFlat;
        std::function<void(const qmd::ElementPtr&)> walk =
            [&](const qmd::ElementPtr& node) {
            if (node->type == qmd::ElementType::Code ||
                node->type == qmd::ElementType::CodeBlock) {
                codeFlat += node->text;
                for (const auto& child : node->children)
                    codeFlat += child->text;
                return;
            }
            flat += node->text;
            for (const auto& child : node->children) walk(child);
        };
        if (emoji.root) walk(emoji.root);
        check(flat.find("\xF0\x9F\x9A\x80") != std::string::npos,
              ":rocket: replaced with the emoji");
        check(flat.find("\xF0\x9F\x91\x8D") != std::string::npos,
              ":+1: replaced with the emoji");
        check(flat.find(":rocket:") == std::string::npos,
              "no literal :rocket: remains");
        check(flat.find(":not_a_real_code:") != std::string::npos,
              "unknown shortcodes stay literal");
        check(codeFlat.find(":tada:") != std::string::npos,
              "inline code shortcodes untouched");
        check(codeFlat.find(":fire:") != std::string::npos,
              "fenced code shortcodes untouched");
    }

    // A UTF-8 byte order mark (EF BB BF), as Windows PowerShell 5.1's
    // Set-Content -Encoding utf8 and Visual Studio write it, is not text:
    // the document parses as it does without one, its first line included,
    // and every source offset still counts the mark's three bytes
    {
        const std::string bom = "\xEF\xBB\xBF";
        const std::string fixture = readFile(TINTA_BOM_FIXTURE);
        const std::string frontMatter = readFile(TINTA_BOM_FRONTMATTER_FIXTURE);
        check(fixture.rfind(bom, 0) == 0 && frontMatter.rfind(bom, 0) == 0,
              "the byte order mark fixtures start with the mark");

        auto titled = parseDocument(parser, fixture, "bom.md");
        check(titled.success && !titled.root->children.empty() &&
                  titled.root->children[0]->type == qmd::ElementType::Heading &&
                  titled.root->children[0]->level == 1 &&
                  !titled.root->children[0]->children.empty() &&
                  titled.root->children[0]->children[0]->text == "Byte order mark" &&
                  titled.root->children[0]->sourceOffset == bom.size() + 2,
              "the first line after the mark is a heading, its text 5 bytes in");
        auto withProperties = parseDocument(parser, frontMatter, L"bom.md");
        check(withProperties.success && withProperties.root->children.size() > 1 &&
                  withProperties.root->children[0]->type == qmd::ElementType::Properties &&
                  withProperties.root->children[1]->type == qmd::ElementType::Heading,
              "front matter after the mark becomes properties, the heading right after");

        const std::pair<const char*, std::string> cases[] = {
            {"the mixed fixture", fixture.substr(bom.size())},
            {"the front matter fixture", frontMatter.substr(bom.size())},
            {"a fence on the first line", "```js\n   indented()\n```\n"},
            {"a list item and its continuation", "- item\n\n  continued\n"},
            {"a table on the first line", "| a | b |\n| --- | --- |\n| 1 | 2 |\n"},
            {"a footnote defined on the first line", "[^1]: The note.\n\nText with a note[^1].\n"},
            {"raw HTML on the first line", "<div>\n\n**HTML block**\n\n</div>\n"},
            {"a bare www link on the first line", "www.example.com\n"},
            {"a task on the first line", "- [ ] first\n- [x] second\n"},
            {"nothing after the mark", ""},
        };
        for (const auto& [name, text] : cases) {
            for (const char* eol : {"\n", "\r\n"}) {
                const std::string source = withEol(text, eol);
                auto plain = parseDocument(parser, source, "bom.md");
                auto marked = parseDocument(parser, bom + source, L"bom.md");
                const std::string message = std::string(name) +
                    (eol[1] ? " (CRLF)" : " (LF)") +
                    " parses as without the mark, every offset 3 bytes on";
                check(plain.success && marked.success &&
                          dumpTree(marked.root, bom.size()) == dumpTree(plain.root, 0),
                      message.c_str());
            }
        }

        // Only the first three bytes are the mark: the same character
        // further on is text, as it always was
        auto inner = parseDocument(parser, "a" + bom + "b\n", "bom.md");
        check(inner.success && inner.root->children.size() == 1 &&
                  !inner.root->children[0]->children.empty() &&
                  inner.root->children[0]->children[0]->text == "a" + bom + "b",
              "a zero width no-break space inside the text stays");
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All document tests passed\n";
    return 0;
}
