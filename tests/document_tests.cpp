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
