#ifndef TINTA_SYNTAX_H
#define TINTA_SYNTAX_H

#include "app.h"
#include <vector>
#include <string>
#include <unordered_set>

// Token for syntax highlighting
struct SyntaxToken {
    std::wstring text;
    SyntaxTokenType tokenType;
};

// Language keyword sets
extern const std::unordered_set<std::wstring> CPP_KEYWORDS;
extern const std::unordered_set<std::wstring> CPP_TYPES;
extern const std::unordered_set<std::wstring> PYTHON_KEYWORDS;
extern const std::unordered_set<std::wstring> JS_KEYWORDS;
extern const std::unordered_set<std::wstring> RUST_KEYWORDS;
extern const std::unordered_set<std::wstring> GO_KEYWORDS;

int detectLanguage(const std::wstring& lang);
const std::unordered_set<std::wstring>* getKeywordsForLanguage(int lang);
std::vector<SyntaxToken> tokenizeLine(const std::wstring& line, int language, bool& inBlockComment);
D2D1_COLOR_F getTokenColor(const D2DTheme& theme, SyntaxTokenType ttype);

#endif // TINTA_SYNTAX_H
