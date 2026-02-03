#include "syntax.h"

const std::unordered_set<std::wstring> CPP_KEYWORDS = {
    L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"break", L"continue",
    L"return", L"goto", L"default", L"void", L"int", L"char", L"float", L"double", L"bool",
    L"long", L"short", L"unsigned", L"signed", L"const", L"static", L"extern", L"volatile",
    L"class", L"struct", L"union", L"enum", L"typedef", L"template", L"typename", L"namespace",
    L"public", L"private", L"protected", L"virtual", L"override", L"final", L"explicit",
    L"inline", L"constexpr", L"consteval", L"constinit", L"auto", L"register", L"mutable",
    L"new", L"delete", L"this", L"nullptr", L"true", L"false", L"throw", L"try", L"catch",
    L"using", L"operator", L"sizeof", L"alignof", L"decltype", L"noexcept", L"static_assert",
    L"friend", L"concept", L"requires", L"co_await", L"co_return", L"co_yield",
    L"#include", L"#define", L"#ifdef", L"#ifndef", L"#endif", L"#if", L"#else", L"#pragma"
};

const std::unordered_set<std::wstring> CPP_TYPES = {
    L"size_t", L"int8_t", L"int16_t", L"int32_t", L"int64_t",
    L"uint8_t", L"uint16_t", L"uint32_t", L"uint64_t",
    L"string", L"wstring", L"vector", L"map", L"set", L"unordered_map", L"unordered_set",
    L"shared_ptr", L"unique_ptr", L"weak_ptr", L"optional", L"variant", L"any",
    L"HRESULT", L"HWND", L"HINSTANCE", L"LPARAM", L"WPARAM", L"LRESULT", L"BOOL",
    L"DWORD", L"WORD", L"BYTE", L"UINT", L"INT", L"LONG", L"ULONG", L"FLOAT",
    L"IDWriteFactory", L"ID2D1Factory", L"ID2D1RenderTarget", L"IDWriteTextFormat"
};

const std::unordered_set<std::wstring> PYTHON_KEYWORDS = {
    L"if", L"elif", L"else", L"for", L"while", L"break", L"continue", L"pass", L"return",
    L"def", L"class", L"import", L"from", L"as", L"try", L"except", L"finally", L"raise",
    L"with", L"yield", L"lambda", L"global", L"nonlocal", L"assert", L"del", L"in", L"is",
    L"not", L"and", L"or", L"True", L"False", L"None", L"async", L"await", L"match", L"case"
};

const std::unordered_set<std::wstring> JS_KEYWORDS = {
    L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"break", L"continue",
    L"return", L"function", L"var", L"let", L"const", L"class", L"extends", L"new", L"this",
    L"super", L"try", L"catch", L"finally", L"throw", L"async", L"await", L"yield",
    L"import", L"export", L"default", L"from", L"as", L"of", L"in", L"typeof", L"instanceof",
    L"true", L"false", L"null", L"undefined", L"NaN", L"Infinity", L"void", L"delete",
    L"debugger", L"with", L"static", L"get", L"set", L"=>"
};

const std::unordered_set<std::wstring> RUST_KEYWORDS = {
    L"if", L"else", L"match", L"for", L"while", L"loop", L"break", L"continue", L"return",
    L"fn", L"let", L"mut", L"const", L"static", L"struct", L"enum", L"trait", L"impl",
    L"pub", L"mod", L"use", L"crate", L"super", L"self", L"Self", L"where", L"as", L"in",
    L"type", L"unsafe", L"async", L"await", L"move", L"ref", L"dyn", L"box", L"extern",
    L"true", L"false", L"Some", L"None", L"Ok", L"Err"
};

const std::unordered_set<std::wstring> GO_KEYWORDS = {
    L"if", L"else", L"for", L"range", L"switch", L"case", L"break", L"continue", L"return",
    L"func", L"var", L"const", L"type", L"struct", L"interface", L"map", L"chan",
    L"package", L"import", L"go", L"defer", L"select", L"default", L"fallthrough", L"goto",
    L"true", L"false", L"nil", L"iota", L"make", L"new", L"append", L"len", L"cap", L"copy"
};

int detectLanguage(const std::wstring& lang) {
    std::wstring lower = lang;
    for (auto& c : lower) c = towlower(c);
    if (lower == L"cpp" || lower == L"c++" || lower == L"c" || lower == L"h" || lower == L"hpp" || lower == L"cxx")
        return 1;  // C/C++
    if (lower == L"python" || lower == L"py")
        return 2;  // Python
    if (lower == L"javascript" || lower == L"js" || lower == L"jsx" || lower == L"ts" || lower == L"typescript" || lower == L"tsx")
        return 3;  // JavaScript/TypeScript
    if (lower == L"rust" || lower == L"rs")
        return 4;  // Rust
    if (lower == L"go" || lower == L"golang")
        return 5;  // Go
    return 0;  // Unknown
}

const std::unordered_set<std::wstring>* getKeywordsForLanguage(int lang) {
    switch (lang) {
        case 1: return &CPP_KEYWORDS;
        case 2: return &PYTHON_KEYWORDS;
        case 3: return &JS_KEYWORDS;
        case 4: return &RUST_KEYWORDS;
        case 5: return &GO_KEYWORDS;
        default: return nullptr;
    }
}

std::vector<SyntaxToken> tokenizeLine(const std::wstring& line, int language, bool& inBlockComment) {
    std::vector<SyntaxToken> tokens;
    const std::unordered_set<std::wstring>* keywords = getKeywordsForLanguage(language);

    size_t i = 0;
    while (i < line.length()) {
        // Handle block comment continuation
        if (inBlockComment) {
            size_t endComment = line.find(L"*/", i);
            if (endComment != std::wstring::npos) {
                tokens.push_back({std::wstring_view(line.data() + i, endComment + 2 - i),
                                  SyntaxTokenType::Comment});
                i = endComment + 2;
                inBlockComment = false;
            } else {
                tokens.push_back({std::wstring_view(line.data() + i, line.length() - i),
                                  SyntaxTokenType::Comment});
                return tokens;
            }
            continue;
        }

        wchar_t c = line[i];

        // Skip whitespace
        if (iswspace(c)) {
            size_t start = i;
            while (i < line.length() && iswspace(line[i])) i++;
            tokens.push_back({std::wstring_view(line.data() + start, i - start),
                              SyntaxTokenType::Plain});
            continue;
        }

        // Line comments
        if (i + 1 < line.length()) {
            if ((line[i] == L'/' && line[i+1] == L'/') ||  // C/C++/JS/Rust/Go
                (language == 2 && line[i] == L'#')) {       // Python
                tokens.push_back({std::wstring_view(line.data() + i, line.length() - i),
                                  SyntaxTokenType::Comment});
                return tokens;
            }
            // Block comment start
            if (line[i] == L'/' && line[i+1] == L'*') {
                size_t endComment = line.find(L"*/", i + 2);
                if (endComment != std::wstring::npos) {
                    tokens.push_back({std::wstring_view(line.data() + i, endComment + 2 - i),
                                      SyntaxTokenType::Comment});
                    i = endComment + 2;
                } else {
                    tokens.push_back({std::wstring_view(line.data() + i, line.length() - i),
                                      SyntaxTokenType::Comment});
                    inBlockComment = true;
                    return tokens;
                }
                continue;
            }
        }
        // Python single # comment
        if (language == 2 && c == L'#') {
            tokens.push_back({std::wstring_view(line.data() + i, line.length() - i),
                              SyntaxTokenType::Comment});
            return tokens;
        }

        // Strings
        if (c == L'"' || c == L'\'') {
            size_t start = i;
            wchar_t quote = c;
            i++;
            while (i < line.length()) {
                if (line[i] == L'\\' && i + 1 < line.length()) {
                    i += 2;  // Skip escaped char
                } else if (line[i] == quote) {
                    i++;
                    break;
                } else {
                    i++;
                }
            }
            tokens.push_back({std::wstring_view(line.data() + start, i - start),
                              SyntaxTokenType::String});
            continue;
        }

        // Numbers (including hex)
        if (iswdigit(c) || (c == L'.' && i + 1 < line.length() && iswdigit(line[i+1]))) {
            size_t start = i;
            if (c == L'0' && i + 1 < line.length() && (line[i+1] == L'x' || line[i+1] == L'X')) {
                i += 2;
                while (i < line.length() && iswxdigit(line[i])) i++;
            } else {
                while (i < line.length() && (iswdigit(line[i]) || line[i] == L'.' ||
                       line[i] == L'e' || line[i] == L'E' || line[i] == L'f' || line[i] == L'L')) i++;
            }
            tokens.push_back({std::wstring_view(line.data() + start, i - start),
                              SyntaxTokenType::Number});
            continue;
        }

        // Identifiers and keywords
        if (iswalpha(c) || c == L'_') {
            size_t start = i;
            while (i < line.length() && (iswalnum(line[i]) || line[i] == L'_')) i++;
            std::wstring word = line.substr(start, i - start);
            std::wstring_view view(line.data() + start, i - start);

            // Check if it's a function call (followed by parenthesis)
            size_t next = i;
            while (next < line.length() && iswspace(line[next])) next++;
            bool isFunction = (next < line.length() && line[next] == L'(');

            // Check if keyword
            if (keywords && keywords->count(word)) {
                tokens.push_back({view, SyntaxTokenType::Keyword});
            } else if (language == 1 && CPP_TYPES.count(word)) {
                tokens.push_back({view, SyntaxTokenType::TypeName});
            } else if (isFunction) {
                tokens.push_back({view, SyntaxTokenType::Function});
            } else {
                tokens.push_back({view, SyntaxTokenType::Plain});
            }
            continue;
        }

        // Preprocessor directives (C/C++)
        if (language == 1 && c == L'#') {
            size_t start = i;
            i++;
            while (i < line.length() && (iswalnum(line[i]) || line[i] == L'_')) i++;
            std::wstring directive = line.substr(start, i - start);
            std::wstring_view view(line.data() + start, i - start);
            if (keywords && keywords->count(directive)) {
                tokens.push_back({view, SyntaxTokenType::Keyword});
            } else {
                tokens.push_back({view, SyntaxTokenType::Plain});
            }
            continue;
        }

        // Operators and punctuation
        tokens.push_back({std::wstring_view(line.data() + i, 1), SyntaxTokenType::Operator});
        i++;
    }

    return tokens;
}

D2D1_COLOR_F getTokenColor(const D2DTheme& theme, SyntaxTokenType ttype) {
    switch (ttype) {
        case SyntaxTokenType::Keyword:  return theme.syntaxKeyword;
        case SyntaxTokenType::String:   return theme.syntaxString;
        case SyntaxTokenType::Comment:  return theme.syntaxComment;
        case SyntaxTokenType::Number:   return theme.syntaxNumber;
        case SyntaxTokenType::Function: return theme.syntaxFunction;
        case SyntaxTokenType::TypeName:     return theme.syntaxType;
        case SyntaxTokenType::Operator: return theme.code;
        default:                  return theme.code;
    }
}
