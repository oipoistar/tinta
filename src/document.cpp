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

qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::string_view path) {
    if (isMermaidDocumentPath(path)) return createMermaidDocument(content);
    return parser.parse(content);
}

qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::wstring_view path) {
    if (isMermaidDocumentPath(path)) return createMermaidDocument(content);
    return parser.parse(content);
}
