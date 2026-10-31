#ifndef TINTA_DOCUMENT_H
#define TINTA_DOCUMENT_H

#include "markdown.h"

#include <array>
#include <string>
#include <string_view>

inline constexpr std::array<std::wstring_view, 6> DOCUMENT_FILE_EXTENSIONS = {
    L".md",
    L".markdown",
    L".mmd",
    L".puml",
    L".plantuml",
    L".smd",  // a sheet (sheet.h)
};

bool isMermaidDocumentPath(std::string_view path);
bool isMermaidDocumentPath(std::wstring_view path);
bool isPlantUmlDocumentPath(std::string_view path);
bool isPlantUmlDocumentPath(std::wstring_view path);
// Plain-text sidecars (.txt/.json/.yaml/...) render as one highlighted
// code block instead of being parsed as markdown
bool isPlainTextDocumentPath(std::string_view path);
bool isPlainTextDocumentPath(std::wstring_view path);
bool isSupportedDocumentPath(std::string_view path);
bool isSupportedDocumentPath(std::wstring_view path);
bool isSupportedDropPath(std::wstring_view path);

// The UTF-8 byte order mark Windows PowerShell 5.1, Visual Studio and some
// editors write ahead of the text. It is not text: md4c never sees it in a
// Markdown document, and the editor keeps it out of its buffer and writes
// it back on save.
inline constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";

inline bool startsWithUtf8Bom(std::string_view text) {
    return text.substr(0, UTF8_BOM.size()) == UTF8_BOM;
}

qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::string_view path);
qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::wstring_view path);

#endif // TINTA_DOCUMENT_H
