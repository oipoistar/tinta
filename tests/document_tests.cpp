#include "document.h"

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
