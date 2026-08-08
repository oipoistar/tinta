#include "document.h"

#include <functional>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

} // namespace

int main() {
    check(isSupportedDocumentPath("notes.md"), ".md is supported");
    check(isSupportedDocumentPath("notes.MARKDOWN"), ".markdown is case-insensitive");
    check(isSupportedDocumentPath(L"diagram.MMD"), ".mmd is case-insensitive");
    check(isMermaidDocumentPath("diagram.mmd"), ".mmd is detected as Mermaid");
    check(!isMermaidDocumentPath("notes.md"), ".md is not detected as Mermaid");
    check(!isSupportedDocumentPath("diagram.mmdd"), "similar extensions are rejected");
    check(!isSupportedDocumentPath("notes.txt"), ".txt is not shown as a document");
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

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All document tests passed\n";
    return 0;
}
