#ifndef TINTA_SYNTAX_H
#define TINTA_SYNTAX_H

#include "app.h"
#include <vector>
#include <string>
#include <unordered_set>
#include <string_view>

// Token for syntax highlighting
struct SyntaxToken {
    std::wstring_view text;
    SyntaxTokenType tokenType;
};

// Keep the original language IDs stable; the extended lexers share block-local
// state, never state between unrelated fenced blocks.
enum SyntaxLanguage {
    LanguageSql = 8, LanguagePowerShell, LanguageJava, LanguagePhp,
    LanguageHtml, LanguageXml, LanguageCss, LanguageYaml, LanguageMarkdown
};

struct SyntaxState {
    enum class Mode { Normal, Comment, Quote, Delimited, HereString, Heredoc,
                      YamlBlock, MarkdownFence };
    Mode mode = Mode::Normal;
    bool inBlockComment = false; // Original language lexers.
    std::wstring delimiter;
    std::wstring commentOpen;
    int commentDepth = 0;
    wchar_t escape = 0;
    bool doubledQuote = false;
    bool inTag = false;
    bool tagName = false;
    bool closingTag = false;
    std::wstring tag;
    std::wstring rawTag;
    int braceDepth = 0;
    bool cssValue = false;
    size_t blockIndent = 0;
    size_t explicitIndent = 0;
};

// Language keyword sets
extern const std::unordered_set<std::wstring> CPP_KEYWORDS;
extern const std::unordered_set<std::wstring> CPP_TYPES;
extern const std::unordered_set<std::wstring> PYTHON_KEYWORDS;
extern const std::unordered_set<std::wstring> JS_KEYWORDS;
extern const std::unordered_set<std::wstring> RUST_KEYWORDS;
extern const std::unordered_set<std::wstring> GO_KEYWORDS;
extern const std::unordered_set<std::wstring> BASH_KEYWORDS;
extern const std::unordered_set<std::wstring> CSHARP_CONTROL_FLOW;
extern const std::unordered_set<std::wstring> CSHARP_KEYWORDS;
extern const std::unordered_set<std::wstring> CSHARP_TYPES;

int detectLanguage(const std::wstring& lang);
const std::unordered_set<std::wstring>* getKeywordsForLanguage(int lang);
std::vector<SyntaxToken> tokenizeLine(const std::wstring& line, int language, SyntaxState& state);
std::vector<SyntaxToken> tokenizeExtendedLine(const std::wstring& line, int language, SyntaxState& state);
D2D1_COLOR_F getTokenColor(const D2DTheme& theme, SyntaxTokenType ttype);

#endif // TINTA_SYNTAX_H
