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
    std::string language;     // for code blocks
    int align = 0;            // for table cells (0=default, 1=left, 2=center, 3=right)
    int col_count = 0;        // for tables (number of columns)

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

// Utility functions
std::string elementTypeToString(ElementType type);
void debugPrintElement(const ElementPtr& elem, int indent = 0);

// Parse HTML content into markdown elements
void parseHtmlIntoElements(const std::string& html, Element* parent);

} // namespace qmd

#endif // TINTA_MARKDOWN_H
