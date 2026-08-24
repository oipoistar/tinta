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

// Pulls title and tags out of the (blanked) frontmatter region for the
// properties strip. Minimal YAML: `key: value`, inline `[a, b]` lists,
// and indented `- item` block lists under tags/aliases.
static void extractFrontmatterProps(const std::string& content, size_t regionEnd,
                                    std::string& title, std::vector<std::string>& tags) {
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r");
        size_t b = s.find_last_not_of(" \t\r");
        return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    };
    auto unquote = [&](std::string s) {
        s = trim(std::move(s));
        if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') ||
                              (s.front() == '"' && s.back() == '"'))) {
            s = s.substr(1, s.size() - 2);
        }
        return s;
    };
    auto addList = [&](const std::string& value, std::vector<std::string>& out) {
        std::string v = trim(value);
        if (v.size() >= 2 && v.front() == '[' && v.back() == ']') {
            v = v.substr(1, v.size() - 2);
        }
        size_t pos = 0;
        while (pos <= v.size()) {
            size_t comma = v.find(',', pos);
            std::string item = unquote(v.substr(pos, comma == std::string::npos
                                                        ? std::string::npos : comma - pos));
            if (!item.empty()) out.push_back(item);
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    };

    bool inTagList = false;
    size_t pos = 0;
    while (pos < regionEnd) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string::npos || eol > regionEnd) eol = regionEnd;
        std::string line = content.substr(pos, eol - pos);
        pos = eol + 1;

        std::string trimmed = trim(line);
        if (inTagList) {
            if (trimmed.rfind("- ", 0) == 0) {
                std::string item = unquote(trimmed.substr(2));
                if (!item.empty()) tags.push_back(item);
                continue;
            }
            inTagList = false;
        }
        size_t colon = trimmed.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(trimmed.substr(0, colon));
        std::string value = trim(trimmed.substr(colon + 1));
        if (key == "title") {
            title = unquote(value);
        } else if (key == "tags") {
            if (value.empty()) inTagList = true;
            else addList(value, tags);
        }
    }
}

// Builds the synthetic Properties block and inserts it as the first child
static void insertProperties(qmd::ParseResult& result,
                             const std::string& content) {
    if (!result.success || !result.root) return;
    // The frontmatter region ends at the first closing delimiter line;
    // blankFrontmatter validated it exists, so a cheap re-scan suffices
    size_t firstEol = content.find('\n');
    size_t regionEnd = content.find("\n---", firstEol);
    size_t regionEndDots = content.find("\n...", firstEol);
    if (regionEndDots != std::string::npos &&
        (regionEnd == std::string::npos || regionEndDots < regionEnd)) {
        regionEnd = regionEndDots;
    }
    if (regionEnd == std::string::npos) return;

    std::string title;
    std::vector<std::string> tags;
    extractFrontmatterProps(content, regionEnd, title, tags);
    if (title.empty() && tags.empty()) return;

    auto props = std::make_shared<qmd::Element>(qmd::ElementType::Properties);
    props->parent = result.root.get();
    props->text = title;
    for (const auto& tag : tags) {
        auto chip = std::make_shared<qmd::Element>(qmd::ElementType::Text);
        chip->text = tag;
        chip->parent = props.get();
        props->children.push_back(std::move(chip));
    }
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
