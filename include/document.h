#ifndef TINTA_DOCUMENT_H
#define TINTA_DOCUMENT_H

#include "markdown.h"

#include <array>
#include <string>
#include <string_view>

inline constexpr std::array<std::wstring_view, 3> DOCUMENT_FILE_EXTENSIONS = {
    L".md",
    L".markdown",
    L".mmd",
};

bool isMermaidDocumentPath(std::string_view path);
bool isMermaidDocumentPath(std::wstring_view path);
bool isSupportedDocumentPath(std::string_view path);
bool isSupportedDocumentPath(std::wstring_view path);
bool isSupportedDropPath(std::wstring_view path);

qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::string_view path);
qmd::ParseResult parseDocument(qmd::MarkdownParser& parser,
                               const std::string& content,
                               std::wstring_view path);

#endif // TINTA_DOCUMENT_H
