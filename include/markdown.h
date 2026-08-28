#ifndef TINTA_MARKDOWN_H
#define TINTA_MARKDOWN_H

#include "types.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace qmd {

// Markdown element types
enum class ElementType {
    Document,
    // Block elements
    Paragraph,
    Heading,
    CodeBlock,
    MermaidDiagram,
    BlockQuote,
    List,
    ListItem,
    HorizontalRule,
    Table,
    TableRow,
    TableCell,
    HtmlBlock,      // Raw HTML block
    // Inline elements
    Text,
    Code,
    Emphasis,
    Strong,
    Link,
    Image,
    SoftBreak,
    HardBreak,
    Ruby,
    RubyText,
    // Synthetic first block built from YAML frontmatter (title + tags)
    Properties,
    // Obsidian/Typora inline extensions (parsed in a post-pass)
    Highlight,     // ==text==
    Superscript,   // ^text^
    Subscript,     // ~text~
    Strikethrough, // ~~text~~ (handled in the post-pass, not md4c)
    // LaTeX math spans (#80): text carries the raw TeX source
    MathInline,    // $...$
    MathDisplay,   // $$...$$ (rendered as a centered block)
};

// Forward declaration
struct Element;
using ElementPtr = std::shared_ptr<Element>;

// Base element structure
struct Element {
    ElementType type;
    std::string text;
    std::string url;          // for links/images
    std::string title;        // for links/images
    int level = 0;            // for headings (1-6)
    bool ordered = false;     // for lists
    int start = 1;            // for ordered lists
    bool isTask = false;      // list item is a - [ ] / - [x] task
    bool taskChecked = false;
    size_t taskMarkOffset = SIZE_MAX;  // byte offset of the mark char in the source
    std::string language;     // for code blocks
    int align = 0;            // for table cells (0=default, 1=left, 2=center, 3=right)
    int col_count = 0;        // for tables (number of columns)
    int alertKind = 0;        // for blockquotes: GitHub alert (0=none, 1=note, 2=tip,
                              // 3=important, 4=warning, 5=caution)

    size_t sourceOffset = SIZE_MAX; // byte offset in original markdown source

    std::vector<ElementPtr> children;
    Element* parent = nullptr;

    Element(ElementType t) : type(t) {}
};

// Parse result
struct ParseResult {
    ElementPtr root;
    bool success = false;
    std::string error;
    size_t parseTimeUs = 0; // microseconds
};

// Markdown parser using MD4C
class MarkdownParser {
public:
    MarkdownParser();
    ~MarkdownParser();

    ParseResult parse(const std::string& markdown);
    ParseResult parseFile(const std::string& path);

    // Options
    void setTabWidth(int width) { m_tabWidth = width; }
    void setPermissiveAutoLinks(bool enabled) { m_permissiveAutoLinks = enabled; }
    void setPermissiveUrls(bool enabled) { m_permissiveUrls = enabled; }
    void setTables(bool enabled) { m_tables = enabled; }
    void setStrikethrough(bool enabled) { m_strikethrough = enabled; }
    void setTaskLists(bool enabled) { m_taskLists = enabled; }

private:
    int m_tabWidth = 4;
    bool m_permissiveAutoLinks = true;
    bool m_permissiveUrls = true;
    bool m_tables = true;
    bool m_strikethrough = true;
    bool m_taskLists = true;
};

// File-reference extension gate (#127/#141/#162): true when the path
// ends in one of the extensions the plain-path scanner recognizes
bool fileRefKnownExtension(const std::string& path);
// True for Tinta's own document types (.md/.mmd/.markdown): these open
// in a tab, while other known text files open with their registered
// application (#162)
bool fileRefIsMarkdown(const std::string& path);

// Utility functions
std::string elementTypeToString(ElementType type);
void debugPrintElement(const ElementPtr& elem, int indent = 0);

// Parse HTML content into markdown elements
void parseHtmlIntoElements(const std::string& html, Element* parent);

} // namespace qmd

#endif // TINTA_MARKDOWN_H
