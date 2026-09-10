#include "syntax.h"
#include <cwctype>

namespace {
using Type = SyntaxTokenType;
using Mode = SyntaxState::Mode;
using View = std::wstring_view;
using Words = std::unordered_set<std::wstring>;

Words words(View text) {
    Words result;
    size_t i = 0;
    while (i < text.size()) {
        size_t end = text.find(L' ', i);
        if (end == View::npos) end = text.size();
        if (end > i) result.emplace(text.substr(i, end - i));
        i = end + 1;
    }
    return result;
}
std::wstring lower(View text) {
    std::wstring result(text);
    for (auto& c : result) c = towlower(c);
    return result;
}
bool identifier(wchar_t c) { return iswalnum(c) || c == L'_'; }
bool nameStart(wchar_t c) { return iswalpha(c) || c == L'_'; }

struct Rules { Words keywords, control, types; };
const Rules& rules(int language) {
    static const Rules sql {
        words(L"select from where as distinct all join inner outer left right full cross on using group by having order asc desc limit offset fetch first next rows only union intersect except insert into values update set delete merge returning output create alter drop truncate table view index database schema sequence procedure function trigger replace temporary temp primary foreign key references unique not null default check constraint with recursive materialized exists in between like ilike is and or xor true false unknown over partition range preceding following current row unbounded exclude window filter within lateral natural top percent ties identity auto_increment generated cascade restrict if begin end commit rollback transaction grant revoke explain analyze vacuum use show describe delimiter collate cast convert coalesce nullif"),
        words(L"case when then else end if loop while return declare do exception raise"),
        words(L"int integer smallint bigint tinyint serial bigserial decimal numeric number real float double precision money boolean bool bit char varchar nvarchar nchar text ntext clob blob binary varbinary bytea date datetime datetime2 timestamp timestamptz time interval uuid json jsonb xml")
    };
    static const Rules ps {
        words(L"function filter param begin process end dynamicparam class enum interface hidden static using module namespace assembly data workflow configuration parallel sequence in true false null mandatory validateset validaterange cmdletbinding"),
        words(L"if else elseif switch foreach for while do until break continue return throw try catch finally trap exit") ,
        words(L"string char int int32 int64 long double float decimal bool boolean byte array hashtable object datetime scriptblock psobject pscustomobject void")
    };
    static const Rules java {
        words(L"abstract assert boolean byte char class const double enum exports extends final float implements import instanceof int interface long module native new non-sealed open opens package permits private protected provides public record requires sealed short static strictfp super synchronized this to transient transitive uses var void volatile with true false null"),
        words(L"break case catch continue default do else finally for if return switch throw throws try while yield"),
        words(L"boolean byte char double float int long short void String Object Boolean Byte Character Double Float Integer Long Short Number Class System Math List ArrayList Map HashMap Set HashSet Optional Stream Exception RuntimeException")
    };
    static const Rules php {
        words(L"abstract and array as callable class clone const declare echo enum extends final fn function global implements include include_once instanceof insteadof interface isset list namespace new or print private protected public readonly require require_once static trait unset use var xor yield true false null self parent match __class__ __dir__ __file__ __function__ __line__ __method__ __namespace__ __trait__"),
        words(L"break case catch continue default die do else elseif empty enddeclare endfor endforeach endif endswitch endwhile exit finally for foreach goto if return switch throw try while"),
        words(L"array bool boolean callable double false float int integer iterable mixed never null object resource string true void")
    };
    switch (language) {
        case LanguageSql: return sql;
        case LanguagePowerShell: return ps;
        case LanguageJava: return java;
        default: return php;
    }
}

// Every token is a slice of the caller's line. A single advancing cursor makes
// source preservation independent of which language rule matched.
struct Lexer {
    View line;
    SyntaxState& state;
    int language;
    size_t i = 0;
    std::vector<SyntaxToken> tokens;

    bool at(View text, size_t pos) const { return line.substr(pos, text.size()) == text; }
    bool at(View text) const { return at(text, i); }
    size_t nonspace(size_t pos) const {
        while (pos < line.size() && iswspace(line[pos])) ++pos;
        return pos;
    }
    void emit(size_t end, Type type) {
        if (end > i) tokens.push_back({line.substr(i, end - i), type});
        i = end;
    }
    void clearMode() { state.mode = Mode::Normal; state.delimiter.clear(); }

    void continueComment(size_t scan) {
        while (scan < line.size()) {
            if (!state.commentOpen.empty() && at(state.commentOpen, scan)) {
                ++state.commentDepth;
                scan += state.commentOpen.size();
            } else if (at(state.delimiter, scan)) {
                scan += state.delimiter.size();
                if (--state.commentDepth == 0) {
                    clearMode();
                    emit(scan, Type::Comment);
                    return;
                }
            } else ++scan;
        }
        emit(line.size(), Type::Comment);
    }
    void comment(View open, View close, bool nested = false) {
        state.mode = Mode::Comment;
        state.delimiter = close;
        state.commentOpen = nested ? std::wstring(open) : L"";
        state.commentDepth = 1;
        continueComment(i + open.size());
    }
    void continueString(size_t scan) {
        while (scan < line.size()) {
            if (state.escape && line[scan] == state.escape) {
                scan += (scan + 1 < line.size() ? 2 : 1);
            } else if (at(state.delimiter, scan)) {
                scan += state.delimiter.size();
                if (state.doubledQuote && at(state.delimiter, scan)) {
                    scan += state.delimiter.size();
                    continue;
                }
                clearMode();
                emit(scan, Type::String);
                return;
            } else ++scan;
        }
        emit(line.size(), Type::String);
    }
    void quoted(size_t prefix, wchar_t escape, bool doubled = false) {
        state.mode = Mode::Quote;
        state.delimiter.assign(1, line[i + prefix]);
        state.escape = escape;
        state.doubledQuote = doubled;
        continueString(i + prefix + 1);
    }
    void delimited(View delimiter, wchar_t escape = 0) {
        state.mode = Mode::Delimited;
        state.delimiter = delimiter;
        state.escape = escape;
        state.doubledQuote = false;
        continueString(i + delimiter.size());
    }
    bool continuation() {
        switch (state.mode) {
            case Mode::Comment: continueComment(i); return true;
            case Mode::Quote:
            case Mode::Delimited: continueString(i); return true;
            case Mode::HereString:
            case Mode::Heredoc: {
                // PowerShell terminators start in column zero; PHP permits
                // indentation and punctuation following the identifier.
                size_t start = state.mode == Mode::Heredoc ? nonspace(0) : 0;
                size_t end = start + state.delimiter.size();
                bool closes = at(state.delimiter, start) &&
                    (state.mode == Mode::HereString || end == line.size() || !identifier(line[end]));
                if (closes) { clearMode(); emit(end, Type::String); }
                else emit(line.size(), Type::String);
                return true;
            }
            default: return false;
        }
    }
    size_t numberEnd(size_t start) const {
        size_t end = start;
        if (at(L"0x", end) || at(L"0X", end) || at(L"0b", end) || at(L"0B", end) || at(L"0o", end)) {
            end += 2;
            while (end < line.size() && (iswxdigit(line[end]) || line[end] == L'_')) ++end;
        } else {
            while (end < line.size() && (iswdigit(line[end]) || line[end] == L'_' || line[end] == L'.')) ++end;
            if (end < line.size() && (line[end] == L'e' || line[end] == L'E')) {
                size_t exponent = end + 1;
                if (exponent < line.size() && (line[exponent] == L'+' || line[exponent] == L'-')) ++exponent;
                if (exponent < line.size() && iswdigit(line[exponent])) {
                    end = exponent + 1;
                    while (end < line.size() && (iswdigit(line[end]) || line[end] == L'_')) ++end;
                }
            }
        }
        if (language == LanguageJava || language == LanguagePowerShell || language == LanguageCss)
            while (end < line.size() && (iswalpha(line[end]) || line[end] == L'%')) ++end;
        return end;
    }
    void program() {
        const bool sql = language == LanguageSql, ps = language == LanguagePowerShell;
        const bool java = language == LanguageJava, php = language == LanguagePhp;
        const auto& rule = rules(language);
        while (i < line.size()) {
            if (continuation()) continue;
            wchar_t c = line[i];
            if (iswspace(c)) { emit(nonspace(i), Type::Plain); continue; }
            if ((sql && at(L"--")) || ((java || php) && at(L"//")) ||
                ((ps || php) && c == L'#' && !(php && at(L"#[")))) {
                emit(line.size(), Type::Comment); continue;
            }
            if ((!ps && at(L"/*")) || (ps && at(L"<#"))) {
                comment(ps ? L"<#" : L"/*", ps ? L"#>" : L"*/", sql || ps); continue;
            }
            if (ps && (at(L"@\"") || at(L"@'")) && nonspace(i + 2) == line.size()) {
                state.mode = Mode::HereString;
                state.delimiter = std::wstring(1, line[i + 1]) + L"@";
                emit(line.size(), Type::String); continue;
            }
            if (php && at(L"<<<")) {
                size_t start = nonspace(i + 3), end = start;
                wchar_t quote = 0;
                if (end < line.size() && (line[end] == L'\'' || line[end] == L'"')) quote = line[end++];
                start = end;
                if (end < line.size() && nameStart(line[end])) {
                    while (end < line.size() && identifier(line[end])) ++end;
                    size_t tail = end;
                    if (quote && tail < line.size() && line[tail] == quote) ++tail;
                    else if (quote) tail = line.size() + 1;
                    if (tail <= line.size() && nonspace(tail) == line.size()) {
                        state.mode = Mode::Heredoc;
                        state.delimiter = line.substr(start, end - start);
                        emit(line.size(), Type::String); continue;
                    }
                }
            }
            if (java && at(L"\"\"\"")) { delimited(L"\"\"\"", L'\\'); continue; }
            if (sql && c == L'$') {
                size_t end = i + 1;
                if (end < line.size() && nameStart(line[end]))
                    while (end < line.size() && identifier(line[end])) ++end;
                if (end < line.size() && line[end] == L'$') {
                    delimited(line.substr(i, end + 1 - i)); continue;
                }
            }
            if (c == L'\'' || c == L'"' || ((sql || php) && c == L'`')) {
                quoted(0, ps ? (c == L'"' ? L'`' : 0) : sql ? 0 : L'\\', sql || (ps && c == L'\''));
                // Ordinary Java strings cannot consume the next source line.
                if (java) clearMode();
                continue;
            }
            if (sql && c == L'[') {
                state.mode = Mode::Quote; state.delimiter = L"]";
                state.escape = 0; state.doubledQuote = true;
                continueString(i + 1); continue;
            }
            if (ps && c == L'[') {
                size_t end = line.find(L']', i + 1);
                if (end != View::npos) { emit(end + 1, Type::TypeName); continue; }
            }
            if ((ps || php) && c == L'$') {
                size_t end = i + 1;
                if (end < line.size() && line[end] == L'{') {
                    size_t close = line.find(L'}', end + 1);
                    end = close == View::npos ? line.size() : close + 1;
                } else {
                    while (end < line.size() && (identifier(line[end]) || (ps && line[end] == L':'))) ++end;
                }
                auto name = lower(line.substr(i + 1, end - i - 1));
                emit(end, ps && (name == L"true" || name == L"false" || name == L"null") ? Type::Keyword : Type::TypeName);
                continue;
            }
            if (php && (at(L"<?") || at(L"?>"))) {
                size_t end = i + 2;
                if (at(L"<?", i)) {
                    if (lower(line.substr(end, 3)) == L"php") end += 3;
                    else if (end < line.size() && line[end] == L'=') ++end;
                }
                emit(end, Type::Keyword); continue;
            }
            if (iswdigit(c) || (c == L'.' && i + 1 < line.size() && iswdigit(line[i + 1]))) {
                emit(numberEnd(i), Type::Number); continue;
            }
            if ((ps && c == L'-' && i + 1 < line.size() && nameStart(line[i + 1])) ||
                (java && c == L'@')) {
                size_t end = i + 1;
                while (end < line.size() && (identifier(line[end]) || (java && line[end] == L'.'))) ++end;
                emit(end, java ? Type::TypeName : Type::Keyword); continue;
            }
            if (nameStart(c)) {
                size_t end = i + 1;
                while (end < line.size() && (identifier(line[end]) || (ps && line[end] == L'-') || (java && line[end] == L'$'))) ++end;
                auto text = line.substr(i, end - i);
                auto name = java ? std::wstring(text) : lower(text);
                size_t next = nonspace(end);
                Type type = Type::Plain;
                if (rule.control.count(name)) type = Type::ControlFlow;
                else if (rule.types.count(name)) type = Type::TypeName;
                else if (rule.keywords.count(name)) type = Type::Keyword;
                else if ((next < line.size() && line[next] == L'(') || (ps && text.find(L'-') != View::npos)) type = Type::Function;
                else if ((java || php) && iswupper(c)) type = Type::TypeName;
                emit(end, type); continue;
            }
            emit(i + 1, Type::Operator);
        }
    }

    void markup() {
        while (i < line.size()) {
            if (continuation()) continue;
            if (!state.rawTag.empty()) {
                // Embedded script/style bodies are plain text. Do not mistake
                // comparison operators inside them for markup tag openings.
                auto folded = lower(line.substr(i));
                size_t close = folded.find(L"</" + state.rawTag);
                while (close != View::npos) {
                    size_t end = close + state.rawTag.size() + 2;
                    if (end == folded.size() || iswspace(folded[end]) || folded[end] == L'>') break;
                    close = folded.find(L"</" + state.rawTag, close + 1);
                }
                if (close == View::npos) { emit(line.size(), Type::Plain); continue; }
                if (close) emit(i + close, Type::Plain);
                state.rawTag.clear();
            }
            if (at(L"<!--")) { comment(L"<!--", L"-->"); continue; }
            if (at(L"<![CDATA[")) {
                state.mode = Mode::Delimited; state.delimiter = L"]]>";
                state.escape = 0; state.doubledQuote = false;
                continueString(i + 9); continue;
            }
            wchar_t c = line[i];
            if (!state.inTag && c == L'<' && i + 1 < line.size() &&
                (nameStart(line[i + 1]) || line[i + 1] == L'/' || line[i + 1] == L'!' || line[i + 1] == L'?')) {
                state.inTag = state.tagName = true;
                state.closingTag = line[i + 1] == L'/';
                state.tag.clear();
                emit(i + (nameStart(line[i + 1]) ? 1 : 2), Type::Keyword); continue;
            }
            if (state.inTag) {
                if (at(L"/>") || at(L"?>") || c == L'>') {
                    bool selfClosing = c != L'>';
                    if (!selfClosing && !state.closingTag && language == LanguageHtml &&
                        (state.tag == L"script" || state.tag == L"style")) state.rawTag = state.tag;
                    state.inTag = false;
                    emit(i + (selfClosing ? 2 : 1), Type::Keyword); continue;
                }
                if (c == L'\'' || c == L'"') { quoted(0, 0); continue; }
                if (iswspace(c)) { emit(nonspace(i), Type::Plain); continue; }
                if (identifier(c) || c == L':' || c == L'-') {
                    size_t end = i + 1;
                    while (end < line.size() && (identifier(line[end]) || line[end] == L':' || line[end] == L'-' || line[end] == L'.')) ++end;
                    Type type = state.tagName ? Type::Keyword : Type::TypeName;
                    if (state.tagName) { state.tag = lower(line.substr(i, end - i)); state.tagName = false; }
                    emit(end, type); continue;
                }
                emit(i + 1, Type::Operator); continue;
            }
            if (c == L'&') {
                size_t end = i + 1;
                while (end < line.size() && (identifier(line[end]) || line[end] == L'#')) ++end;
                if (end > i + 1 && end < line.size() && line[end] == L';') { emit(end + 1, Type::Number); continue; }
            }
            emit(i + 1, Type::Plain);
        }
    }

    void css() {
        static const Words values = words(L"important inherit initial unset revert revert-layer none auto normal block inline inline-block flex grid contents hidden visible solid relative absolute fixed sticky transparent currentcolor true false and not only or from to");
        while (i < line.size()) {
            if (continuation()) continue;
            wchar_t c = line[i];
            if (iswspace(c)) { emit(nonspace(i), Type::Plain); continue; }
            if (at(L"/*")) { comment(L"/*", L"*/"); continue; }
            if (c == L'\'' || c == L'"') { quoted(0, L'\\'); continue; }
            if (c == L'{') { ++state.braceDepth; state.cssValue = false; }
            if (c == L'}') { if (state.braceDepth) --state.braceDepth; state.cssValue = false; }
            if (c == L';') state.cssValue = false;
            if (c == L':' && state.braceDepth) state.cssValue = true;
            if (c == L'#' && state.cssValue) {
                size_t end = i + 1;
                while (end < line.size() && iswxdigit(line[end])) ++end;
                if (end > i + 1) { emit(end, Type::Number); continue; }
            }
            if (iswdigit(c) || (c == L'.' && i + 1 < line.size() && iswdigit(line[i + 1]))) {
                emit(numberEnd(i), Type::Number); continue;
            }
            if (nameStart(c) || c == L'-' || c == L'@' || c == L'#' || c == L'.') {
                size_t end = i + 1;
                while (end < line.size() && (identifier(line[end]) || line[end] == L'-')) ++end;
                size_t next = nonspace(end);
                auto name = lower(line.substr(i, end - i));
                Type type = Type::Plain;
                if (c == L'@' || values.count(name)) type = Type::Keyword;
                else if (next < line.size() && line[next] == L'(') type = Type::Function;
                else if (!state.cssValue || at(L"--")) type = Type::TypeName;
                emit(end, type);
                if (name == L"url" && next < line.size() && line[next] == L'(') {
                    emit(next + 1, Type::Operator);
                    size_t content = nonspace(i);
                    if (content > i) emit(content, Type::Plain);
                    if (i < line.size() && line[i] != L'\'' && line[i] != L'"') {
                        state.mode = Mode::Delimited; state.delimiter = L")";
                        state.escape = L'\\'; state.doubledQuote = false;
                        continueString(i);
                    }
                }
                continue;
            }
            emit(i + 1, Type::Operator);
        }
    }

    void yaml() {
        size_t indent = nonspace(0);
        size_t parentIndent = indent;
        if (state.mode == Mode::YamlBlock) {
            bool content = indent == line.size() || (state.explicitIndent ? indent >= state.explicitIndent : indent > state.blockIndent);
            if (content) {
                // Infer indentation from the first nonempty content line. A
                // sibling key in a sequence can be indented beyond the dash
                // while still being outside this scalar.
                if (!state.explicitIndent && indent < line.size()) state.explicitIndent = indent;
                emit(line.size(), Type::String); return;
            }
            clearMode();
        }
        while (i < line.size()) {
            if (continuation()) continue;
            wchar_t c = line[i];
            if (iswspace(c)) { emit(nonspace(i), Type::Plain); continue; }
            if (c == L'#' && (i == 0 || iswspace(line[i - 1]))) { emit(line.size(), Type::Comment); continue; }
            if (c == L'\'' || c == L'"') {
                // Quoted mapping keys use the same type colour as plain keys.
                size_t firstToken = tokens.size();
                size_t keyIndent = i;
                quoted(0, c == L'"' ? L'\\' : 0, c == L'\'');
                size_t next = nonspace(i);
                if (state.mode == Mode::Normal && next < line.size() && line[next] == L':') {
                    parentIndent = keyIndent;
                    for (size_t t = firstToken; t < tokens.size(); ++t) tokens[t].tokenType = Type::TypeName;
                }
                continue;
            }
            if ((at(L"---") || at(L"...")) && i == 0 && (i + 3 == line.size() || iswspace(line[i + 3]))) {
                emit(i + 3, Type::Keyword); continue;
            }
            if (c == L'%' && i == 0) { emit(line.size(), Type::Keyword); continue; }
            if (c == L'|' || c == L'>') {
                size_t end = i + 1, extraIndent = 0;
                while (end < line.size() && (line[end] == L'+' || line[end] == L'-' || (line[end] >= L'1' && line[end] <= L'9'))) {
                    if (iswdigit(line[end])) extraIndent = line[end] - L'0';
                    ++end;
                }
                size_t tail = nonspace(end);
                if (tail == line.size() || line[tail] == L'#') {
                    state.mode = Mode::YamlBlock; state.blockIndent = parentIndent;
                    state.explicitIndent = extraIndent ? parentIndent + extraIndent : 0;
                    emit(end, Type::Keyword);
                    if (tail > i) emit(tail, Type::Plain);
                    emit(line.size(), Type::Comment); return;
                }
            }
            if (c == L'&' || c == L'*' || c == L'!') {
                size_t end = i + 1;
                while (end < line.size() && !iswspace(line[end]) && View(L",[]{}").find(line[end]) == View::npos) ++end;
                emit(end, Type::TypeName); continue;
            }
            if (View(L"{}[],:?").find(c) != View::npos || (c == L'-' && (i + 1 == line.size() || iswspace(line[i + 1])))) {
                emit(i + 1, Type::Operator); continue;
            }
            // A plain scalar is scanned as a unit so URLs, dates, hyphens and
            // fragments such as https://host/#part never become comments.
            size_t end = i + 1;
            while (end < line.size()) {
                if (View(L",[]{}").find(line[end]) != View::npos ||
                    (line[end] == L':' && (end + 1 == line.size() || iswspace(line[end + 1]) || View(L",[]{}").find(line[end + 1]) != View::npos)) ||
                    (line[end] == L'#' && iswspace(line[end - 1]))) break;
                ++end;
            }
            size_t textEnd = end;
            while (textEnd > i && iswspace(line[textEnd - 1])) --textEnd;
            auto name = lower(line.substr(i, textEnd - i));
            Type type = Type::Plain;
            if (end < line.size() && line[end] == L':') { type = Type::TypeName; parentIndent = i; }
            else if (name == L"true" || name == L"false" || name == L"null" || name == L"~") type = Type::Keyword;
            else {
                size_t start = i + ((c == L'+' || c == L'-') ? 1 : 0);
                if ((start < textEnd && iswdigit(line[start]) && numberEnd(start) == textEnd) ||
                    name == L".inf" || name == L"-.inf" || name == L"+.inf" || name == L".nan") type = Type::Number;
            }
            emit(textEnd, type);
            if (end > i) emit(end, Type::Plain);
        }
    }

    void markdown() {
        size_t start = nonspace(0);
        if (state.mode == Mode::MarkdownFence) {
            size_t end = start;
            while (end < line.size() && line[end] == state.delimiter[0]) ++end;
            bool closes = start <= 3 && end - start >= state.delimiter.size() && nonspace(end) == line.size();
            emit(line.size(), closes ? Type::Keyword : Type::String);
            if (closes) clearMode();
            return;
        }
        if (state.mode == Mode::Normal && start <= 3 && start < line.size()) {
            wchar_t c = line[start];
            size_t end = start;
            while (end < line.size() && line[end] == c) ++end;
            if ((c == L'`' || c == L'~') && end - start >= 3) {
                state.mode = Mode::MarkdownFence;
                state.delimiter = line.substr(start, end - start);
                emit(line.size(), Type::Keyword); return;
            }
            if ((c == L'#' && end - start <= 6 && (end == line.size() || iswspace(line[end]))) ||
                ((c == L'=' || c == L'-' || c == L'*' || c == L'_') && end - start >= 3 && nonspace(end) == line.size())) {
                emit(line.size(), Type::Keyword); return;
            }
        }
        while (i < line.size()) {
            if (continuation()) continue;
            wchar_t c = line[i];
            if (at(L"<!--")) { comment(L"<!--", L"-->"); continue; }
            if (c == L'\\' && i + 1 < line.size()) { emit(i + 2, Type::Plain); continue; }
            if (c == L'`') {
                size_t end = i + 1;
                while (end < line.size() && line[end] == c) ++end;
                View marker = line.substr(i, end - i);
                size_t close = line.find(marker, end);
                while (close != View::npos && ((close && line[close - 1] == c) ||
                       (close + marker.size() < line.size() && line[close + marker.size()] == c)))
                    close = line.find(marker, close + marker.size());
                if (close != View::npos) { emit(close + marker.size(), Type::String); continue; }
                emit(end, Type::Plain); continue;
            }
            if (at(L"](")) {
                size_t end = i + 2, depth = 1;
                while (end < line.size() && depth) {
                    if (line[end] == L'\\' && end + 1 < line.size()) { end += 2; continue; }
                    if (line[end] == L'(') ++depth;
                    if (line[end] == L')') --depth;
                    ++end;
                }
                emit(end, Type::String); continue;
            }
            if (c == L'*' || c == L'_') {
                size_t end = i + 1;
                while (end < line.size() && line[end] == c) ++end;
                View marker = line.substr(i, end - i);
                size_t close = line.find(marker, end);
                if (close != View::npos && end < line.size() && !iswspace(line[end])) {
                    emit(close + marker.size(), Type::TypeName); continue;
                }
            }
            if (i == start && iswdigit(c)) {
                size_t end = i + 1;
                while (end < line.size() && iswdigit(line[end])) ++end;
                if (end + 1 < line.size() && (line[end] == L'.' || line[end] == L')') && iswspace(line[end + 1])) {
                    emit(end + 1, Type::Keyword); continue;
                }
            }
            bool marker = c == L'[' || c == L']' || c == L'|' ||
                (i == start && (c == L'>' || c == L'-' || c == L'+' || c == L'*'));
            emit(i + 1, marker ? Type::Keyword : Type::Plain);
        }
    }
};
}

std::vector<SyntaxToken> tokenizeExtendedLine(const std::wstring& line, int language, SyntaxState& state) {
    Lexer lexer {line, state, language};
    switch (language) {
        case LanguageHtml:
        case LanguageXml: lexer.markup(); break;
        case LanguageCss: lexer.css(); break;
        case LanguageYaml: lexer.yaml(); break;
        case LanguageMarkdown: lexer.markdown(); break;
        default: lexer.program(); break;
    }
    return std::move(lexer.tokens);
}
