#ifndef TINTA_WORDCOUNT_H
#define TINTA_WORDCOUNT_H

#include "markdown.h"

#include <string>
#include <string_view>

// Word and character totals for the editor's count (#240)
struct TextCounts {
    size_t words = 0;
    size_t characters = 0;
};

// Plain text counted the way word processors do: every CJK ideograph and
// kana is a word of its own, other words are runs between spaces that hold
// a letter or digit. Characters are code points, spaces included, line
// breaks not.
TextCounts countPlainText(std::wstring_view text);

// The text a reader sees in a parsed Markdown tree: prose, link text and
// inline code. Markup, front matter, code blocks, diagrams, math, image alt
// text and footnote markers are left out.
void appendCountableText(const qmd::ElementPtr& element, std::wstring& out);

#endif  // TINTA_WORDCOUNT_H
