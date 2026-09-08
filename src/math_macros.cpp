#include "math_macros.h"

#include <cwctype>
#include <unordered_map>
#include <vector>

namespace tinta_math {
namespace {
struct Macro {
    std::wstring body, defaultArgument;
    int arguments = 0;
    bool hasDefault = false;
};
using Definitions = std::unordered_map<std::wstring, Macro>;

bool letter(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool controlWordAtEnd(const std::wstring& text) {
    size_t i = text.size();
    while (i && letter(text[i - 1])) --i;
    if (i == text.size() || !i || text[i - 1] != L'\\') return false;
    size_t slashes = 0;
    while (i && text[--i] == L'\\') ++slashes;
    return slashes % 2 == 1;
}
void joinTokens(std::wstring& left, const std::wstring& right) {
    if (!right.empty() && letter(right[0]) && controlWordAtEnd(left)) left += L' ';
    left += right;
}

struct Expander {
    std::wstring text;
    size_t pos = 0, expansions = 0;
    bool failed = false;
    Definitions macros;
    std::vector<Definitions> scopes;

    void whitespace() {
        while (pos < text.size()) {
            if (iswspace(text[pos])) { ++pos; continue; }
            if (text[pos] != L'%') break;
            while (pos < text.size() && text[pos] != L'\n') ++pos;
        }
    }
    std::wstring command() {
        if (pos == text.size() || text[pos++] != L'\\') { failed = true; return {}; }
        size_t start = pos;
        while (pos < text.size() && letter(text[pos])) ++pos;
        if (start == pos && pos < text.size()) ++pos;
        return text.substr(start, pos - start);
    }
    std::wstring group(wchar_t open = L'{', wchar_t close = L'}') {
        whitespace();
        if (pos == text.size() || text[pos++] != open) { failed = true; return {}; }
        size_t start = pos;
        int depth = 1, braces = 0;
        while (pos < text.size()) {
            wchar_t c = text[pos++];
            if (c == L'\\') { if (pos < text.size()) ++pos; continue; }
            if (c == L'%') { while (pos < text.size() && text[pos] != L'\n') ++pos; continue; }
            if (open == L'[') {
                if (c == L'{') ++braces;
                if (c == L'}') --braces;
                if (braces) continue;
            }
            if (c == open) ++depth;
            if (c == close && --depth == 0) return text.substr(start, pos - start - 1);
        }
        failed = true; return {};
    }
    std::wstring argument() {
        whitespace();
        if (pos == text.size()) { failed = true; return {}; }
        if (text[pos] == L'{') return group();
        size_t start = pos;
        if (text[pos] == L'\\') command(); else ++pos;
        return text.substr(start, pos - start);
    }
    void replace(size_t start, const std::wstring& replacement) {
        std::wstring result = text.substr(0, start);
        joinTokens(result, replacement);
        size_t resume = start;
        joinTokens(result, text.substr(pos));
        if (result.size() > 32768) { failed = true; return; }
        text = std::move(result); pos = resume;
    }
    std::wstring name(bool braced) {
        whitespace();
        if (braced && pos < text.size() && text[pos] == L'{') {
            std::wstring value = group();
            if (value.size() < 2 || value[0] != L'\\') { failed = true; return {}; }
            for (size_t i = 1; i < value.size(); ++i) if (!letter(value[i])) failed = true;
            return value.substr(1);
        }
        auto value = command();
        if (value.empty() || !letter(value[0])) failed = true;
        return value;
    }
    void define(const std::wstring& kind, size_t start) {
        if (++expansions > 512) { failed = true; return; }
        bool starred = pos < text.size() && text[pos] == L'*';
        if (starred) ++pos;
        std::wstring key = name(kind != L"def");
        Macro macro;
        whitespace();
        if (kind == L"def") {
            while (pos < text.size() && text[pos] == L'#') {
                ++pos; ++macro.arguments;
                if (macro.arguments > 9 || pos == text.size() || text[pos++] != L'0' + macro.arguments) {
                    failed = true; return;
                }
                whitespace();
            }
        } else if (kind != L"DeclareMathOperator" && pos < text.size() && text[pos] == L'[') {
            auto count = group(L'[', L']');
            if (count.size() != 1 || count[0] < L'0' || count[0] > L'9') { failed = true; return; }
            macro.arguments = count[0] - L'0';
            whitespace();
            if (pos < text.size() && text[pos] == L'[') {
                macro.defaultArgument = group(L'[', L']'); macro.hasDefault = true;
                if (!macro.arguments) failed = true;
            }
        }
        macro.body = group();
        if (kind == L"DeclareMathOperator") macro.body = std::wstring(starred ? L"\\operatorname*{" : L"\\operatorname{") + macro.body + L"}";
        bool exists = macros.find(key) != macros.end() || isBuiltinMathCommand(key);
        if ((kind == L"newcommand" || kind == L"DeclareMathOperator") && exists) failed = true;
        if (kind == L"renewcommand" && !exists) failed = true;
        if (failed) return;
        if (kind != L"providecommand" || !exists) macros[key] = std::move(macro);
        replace(start, L"");
    }
    bool run() {
        while (pos < text.size() && !failed) {
            wchar_t c = text[pos];
            if (c == L'%') { whitespace(); continue; }
            if (c == L'{') {
                if (scopes.size() >= 96) return false;
                scopes.push_back(macros); ++pos; continue;
            }
            if (c == L'}') {
                if (scopes.empty()) return false;
                macros = std::move(scopes.back()); scopes.pop_back(); ++pos; continue;
            }
            if (c != L'\\') { ++pos; continue; }
            size_t start = pos;
            std::wstring cmd = command();
            if (cmd == L"newcommand" || cmd == L"renewcommand" || cmd == L"providecommand" ||
                cmd == L"def" || cmd == L"DeclareMathOperator") { define(cmd, start); continue; }
            auto found = macros.find(cmd);
            if (found == macros.end()) continue;
            if (++expansions > 512) return false;
            Macro macro = found->second;
            std::vector<std::wstring> args;
            whitespace();
            if (macro.hasDefault) {
                whitespace();
                args.push_back(pos < text.size() && text[pos] == L'[' ? group(L'[', L']') : macro.defaultArgument);
            }
            while (!failed && args.size() < static_cast<size_t>(macro.arguments)) args.push_back(argument());
            std::wstring replacement;
            size_t chunkStart = 0;
            for (size_t i = 0; i < macro.body.size() && !failed; ++i) {
                if (macro.body[i] == L'\\' && i + 1 < macro.body.size()) { ++i; continue; }
                if (macro.body[i] == L'#') {
                    joinTokens(replacement, macro.body.substr(chunkStart, i - chunkStart));
                    if (++i == macro.body.size()) { failed = true; break; }
                    wchar_t digit = macro.body[i];
                    if (digit == L'#') replacement += L'#';
                    else if (digit >= L'1' && digit <= L'0' + macro.arguments) joinTokens(replacement, args[digit - L'1']);
                    else failed = true;
                    chunkStart = i + 1;
                }
                if (replacement.size() > 32768) failed = true;
            }
            joinTokens(replacement, macro.body.substr(chunkStart));
            if (!failed) replace(start, replacement);
        }
        return !failed && scopes.empty();
    }
};
}

bool expandLatexMacros(const std::wstring& source, std::wstring& expanded) {
    if (source.size() > 32768) return false;
    if (source.find(L"\\newcommand") == std::wstring::npos && source.find(L"\\def") == std::wstring::npos &&
        source.find(L"\\renewcommand") == std::wstring::npos && source.find(L"\\providecommand") == std::wstring::npos &&
        source.find(L"\\DeclareMathOperator") == std::wstring::npos) {
        expanded = source; return true;
    }
    Expander expander;
    expander.text = source;
    if (!expander.run()) return false;
    expanded = std::move(expander.text);
    return true;
}
}
