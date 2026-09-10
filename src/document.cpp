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

// Lowercased ASCII extension incl. the dot; empty for none or non-ASCII
template <typename Character>
std::string extensionLower(std::basic_string_view<Character> path) {
    size_t dot = path.find_last_of(static_cast<Character>('.'));
    if (dot == std::basic_string_view<Character>::npos) return {};
    std::string out;
    for (size_t i = dot; i < path.size(); i++) {
        Character c = path[i];
        if (static_cast<unsigned long>(c) > 127) return {};
        out += static_cast<char>(lowerAscii(c));
    }
    return out;
}

template <typename Character>
bool isPlainTextPath(std::basic_string_view<Character> path) {
    std::string ext = extensionLower(path);
    return ext == ".txt" || ext == ".json" || ext == ".yaml" ||
           ext == ".yml" || ext == ".toml" || ext == ".ini" ||
           ext == ".csv" || ext == ".log";
}

// Fence language tag for a plain-text document's code block
template <typename Character>
const char* plainTextLanguage(std::basic_string_view<Character> path) {
    std::string ext = extensionLower(path);
    if (ext == ".json") return "json";
    if (ext == ".yaml" || ext == ".yml") return "yaml";
    if (ext == ".toml") return "toml";
    if (ext == ".ini") return "ini";
    if (ext == ".csv") return "csv";
    if (ext == ".log") return "log";
    return "";
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
        isMermaidPath(path) || isPlainTextPath(path);
}

// A plain-text file becomes one highlighted code block: peek, tabs, and
// export all reuse the normal code-block path
qmd::ParseResult createPlainTextDocument(const std::string& content,
                                         const char* language) {
    auto start = std::chrono::high_resolution_clock::now();

    qmd::ParseResult result;
    result.root = std::make_shared<qmd::Element>(qmd::ElementType::Document);
    auto block = std::make_shared<qmd::Element>(qmd::ElementType::CodeBlock);
    block->language = language;
    block->sourceOffset = 0;
    block->parent = result.root.get();
    auto text = std::make_shared<qmd::Element>(qmd::ElementType::Text);
    text->text = content;
    text->sourceOffset = 0;
    text->parent = block.get();
    block->children.push_back(std::move(text));
    result.root->children.push_back(std::move(block));
    result.success = true;
    result.parseTimeUs = static_cast<size_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count());
    return result;
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

bool isPlainTextDocumentPath(std::string_view path) {
    return isPlainTextPath(path);
}

bool isPlainTextDocumentPath(std::wstring_view path) {
    return isPlainTextPath(path);
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
    auto doc = fm::parse(content);
    if (!doc.present) return false;
    out = content;
    for (size_t i = doc.begin; i < doc.end; ++i)
        if (out[i] != '\n' && out[i] != '\r') out[i] = ' ';
    return true;
}

static void insertProperties(qmd::ParseResult& result, const std::string& content) {
    if (!result.success || !result.root) return;
    auto doc = fm::parse(content);
    if (!doc.present) return;
    auto props = std::make_shared<qmd::Element>(qmd::ElementType::Properties);
    props->parent = result.root.get();
    props->properties = std::move(doc.properties);
    result.root->children.insert(result.root->children.begin(), std::move(props));
}

qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::string_view path) {
    if (isMermaidDocumentPath(path)) return createMermaidDocument(content);
    if (isPlainTextDocumentPath(path)) {
        return createPlainTextDocument(content, plainTextLanguage(path));
    }
    std::string cleaned;
    if (blankFrontmatter(content, cleaned)) {
        auto result = parser.parse(cleaned);
        insertProperties(result, content);
        return result;
    }
    return parser.parse(content);
}

qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::wstring_view path) {
    if (isMermaidDocumentPath(path)) return createMermaidDocument(content);
    if (isPlainTextDocumentPath(path)) {
        return createPlainTextDocument(content, plainTextLanguage(path));
    }
    std::string cleaned;
    if (blankFrontmatter(content, cleaned)) {
        auto result = parser.parse(cleaned);
        insertProperties(result, content);
        return result;
    }
    return parser.parse(content);
}
