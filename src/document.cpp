#include "document.h"

#include <chrono>
#include <memory>

namespace {

template <typename Character>
Character lowerAscii(Character c) {
    const Character upperA = static_cast<Character>('A');
    const Character upperZ = static_cast<Character>('Z');
    if (c >= upperA && c <= upperZ) {
        return static_cast<Character>(c - upperA + static_cast<Character>('a'));
    }
    return c;
}

template <typename Character>
bool hasExtension(std::basic_string_view<Character> path,
                  std::basic_string_view<Character> expected) {
    size_t dot = path.find_last_of(static_cast<Character>('.'));
    if (dot == std::basic_string_view<Character>::npos) return false;

    std::basic_string_view<Character> extension = path.substr(dot);
    if (extension.size() != expected.size()) return false;
    for (size_t i = 0; i < extension.size(); i++) {
        if (lowerAscii(extension[i]) != lowerAscii(expected[i])) return false;
    }
    return true;
}

template <typename Character>
bool isMermaidPath(std::basic_string_view<Character> path) {
    const Character extension[] = {
        static_cast<Character>('.'),
        static_cast<Character>('m'),
        static_cast<Character>('m'),
        static_cast<Character>('d'),
    };
    return hasExtension(path, std::basic_string_view<Character>(extension, 4));
}

template <typename Character>
bool isSupportedPath(std::basic_string_view<Character> path) {
    const Character md[] = {
        static_cast<Character>('.'),
        static_cast<Character>('m'),
        static_cast<Character>('d'),
    };
    const Character markdown[] = {
        static_cast<Character>('.'),
        static_cast<Character>('m'),
        static_cast<Character>('a'),
        static_cast<Character>('r'),
        static_cast<Character>('k'),
        static_cast<Character>('d'),
        static_cast<Character>('o'),
        static_cast<Character>('w'),
        static_cast<Character>('n'),
    };
    return hasExtension(path, std::basic_string_view<Character>(md, 3)) ||
        hasExtension(path, std::basic_string_view<Character>(markdown, 9)) ||
        isMermaidPath(path);
}

qmd::ParseResult createMermaidDocument(const std::string& content) {
    auto start = std::chrono::high_resolution_clock::now();

    qmd::ParseResult result;
    result.root = std::make_shared<qmd::Element>(qmd::ElementType::Document);
    auto diagram = std::make_shared<qmd::Element>(qmd::ElementType::MermaidDiagram);
    diagram->text = content;
    diagram->sourceOffset = 0;
    diagram->parent = result.root.get();
    result.root->children.push_back(std::move(diagram));
    result.success = true;
    result.parseTimeUs = static_cast<size_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count());
    return result;
}

} // namespace

bool isMermaidDocumentPath(std::string_view path) {
    return isMermaidPath(path);
}

bool isMermaidDocumentPath(std::wstring_view path) {
    return isMermaidPath(path);
}

bool isSupportedDocumentPath(std::string_view path) {
    return isSupportedPath(path);
}

bool isSupportedDocumentPath(std::wstring_view path) {
    return isSupportedPath(path);
}

bool isSupportedDropPath(std::wstring_view path) {
    return isSupportedDocumentPath(path) || hasExtension(path, std::wstring_view(L".txt"));
}

// YAML frontmatter at the start of a file would otherwise render as a giant
// setext heading — the closing --- promotes the block above it (#61). The
// block is blanked in a copy rather than removed so byte offsets stay
// aligned with the raw source for edit-preview sync and scroll anchors.
// Only a block opened by --- on the very first line and closed by --- or
// ... counts; an unclosed fence renders as ordinary markdown.
static bool blankFrontmatter(const std::string& content, std::string& out) {
    size_t start = 0;
    if (content.compare(0, 3, "\xEF\xBB\xBF") == 0) start = 3;

    auto isDelimiter = [&](size_t lineStart, size_t lineEnd, char c) {
        size_t len = lineEnd - lineStart;
        if (len > 0 && content[lineStart + len - 1] == '\r') len--;
        return len == 3 && content[lineStart] == c &&
               content[lineStart + 1] == c && content[lineStart + 2] == c;
    };

    size_t firstEol = content.find('\n', start);
    if (firstEol == std::string::npos) return false;
    if (!isDelimiter(start, firstEol, '-')) return false;

    size_t pos = firstEol + 1;
    while (pos <= content.size()) {
        size_t eol = content.find('\n', pos);
        size_t lineEnd = (eol == std::string::npos) ? content.size() : eol;
        if (isDelimiter(pos, lineEnd, '-') || isDelimiter(pos, lineEnd, '.')) {
            out = content;
            for (size_t i = start; i < lineEnd; i++) {
                if (out[i] != '\n' && out[i] != '\r') out[i] = ' ';
            }
            return true;
        }
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return false;
}

qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::string_view path) {
    if (isMermaidDocumentPath(path)) return createMermaidDocument(content);
    std::string cleaned;
    if (blankFrontmatter(content, cleaned)) return parser.parse(cleaned);
    return parser.parse(content);
}

qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::wstring_view path) {
    if (isMermaidDocumentPath(path)) return createMermaidDocument(content);
    std::string cleaned;
    if (blankFrontmatter(content, cleaned)) return parser.parse(cleaned);
    return parser.parse(content);
}
