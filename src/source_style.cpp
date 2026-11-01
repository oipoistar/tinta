// Markdown in the source editor, in colour (source_style.h, #268).
//
// What a line is can depend on the lines above it (a fence opened further
// up, the properties at the top), so the lines are worked out from the top,
// once per change to the text and only as far down as the editor draws. A
// line's inline Markdown is read as Live reads it (live::inlineSpans), so
// the source and the page agree on what is code, a link or bold.

// Its work is a few short scans per drawn line: compiled for size
#pragma optimize("s", on)

#include "source_style.h"
#include "live_text.h"
#include "syntax.h"

#include <d2d1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>

using Microsoft::WRL::ComPtr;

namespace {

const size_t npos = std::wstring_view::npos;

// What a line is
enum Kind : unsigned char {
    K_TEXT,         // Markdown
    K_FENCE,        // a code fence's opening or closing line
    K_CODE,         // a line inside a fence, or the properties'
    K_FRONT_MARK,   // the properties' --- lines
    K_MATH_MARK,    // a $$ line
    K_MATH,         // a line inside $$
    K_COMMENT,      // a line of an HTML comment over lines
    K_RULE,         // a setext heading's underline
};

struct Line {
    Kind kind = K_TEXT;
    unsigned char heading = 0;  // a setext heading's level, on its text
    bool head = false;          // a table's header: the row over its delimiter row
    int fence = -1;             // the fence a code line is in
};

// A fence's code: its language and the lexer's state at each line's start,
// worked out as far down as drawn
struct Fence {
    size_t first = 0;           // its first line of code
    int language = 0;
    std::vector<SyntaxState> states;
};

struct State {
    // The colours, for one render target, theme and font
    unsigned long long key = 0, painted = 0;
    bool keepsWidth = false;
    ComPtr<ID2D1SolidColorBrush> mark, accent, link, code, inlineCode, comment, heading[6];
    ComPtr<ID2D1SolidColorBrush> bold, italic;  // the theme's own, if it has them (#310)
    std::vector<std::pair<unsigned, ComPtr<ID2D1SolidColorBrush>>> tokens;
    // What the lines are, for one revision of the text, from the top down
    unsigned long long revision = ~0ull;
    std::vector<Line> lines;
    std::vector<Fence> fences;
    size_t walked = 0;
    size_t frontEnd = 0;        // the properties' closing line, or 0 for none
    bool inFence = false, inMath = false, inComment = false;
    wchar_t fenceChar = 0;
    size_t fenceRun = 0;
    std::vector<live::InlineSpan> spans;
};

State& state(const App& app) {
    static std::map<const App*, State> states;
    return states[&app];
}

std::wstring_view lineAt(const App& app, size_t i) {
    const std::vector<size_t>& starts = app.editorLineStarts;
    size_t from = starts[i];
    size_t to = i + 1 < starts.size() ? starts[i + 1] - 1 : app.editorText.size();
    return std::wstring_view(app.editorText.data() + from, std::max(from, to) - from);
}

std::wstring_view trimmed(std::wstring_view s) {
    size_t a = s.find_first_not_of(L" \t");
    if (a == npos) return {};
    size_t b = s.find_last_not_of(L" \t");
    return s.substr(a, b - a + 1);
}

size_t leading(std::wstring_view s) {
    size_t a = s.find_first_not_of(L' ');
    return a == npos ? s.size() : a;
}

// ``` or ~~~ after any indent: how many, and which
size_t fenceRun(std::wstring_view line, wchar_t& ch) {
    size_t i = leading(line);
    if (i >= line.size() || (line[i] != L'`' && line[i] != L'~')) return 0;
    ch = line[i];
    size_t n = 0;
    while (i + n < line.size() && line[i + n] == ch) ++n;
    // A backtick fence's words hold no backtick
    if (n < 3 || (ch == L'`' && line.find(L'`', i + n) != npos)) return 0;
    return n;
}

// ---, *** or ___ alone on a line, with spaces between if any
bool divider(std::wstring_view s) {
    wchar_t mark = 0;
    int count = 0;
    for (wchar_t c : s) {
        if (c == L' ' || c == L'\t') continue;
        if ((c != L'-' && c != L'*' && c != L'_') || (mark && c != mark)) return false;
        mark = c;
        ++count;
    }
    return count >= 3;
}

bool tableRow(std::wstring_view s) {
    size_t i = leading(s);
    return i < s.size() && s[i] == L'|';
}

bool delimiterRow(std::wstring_view s) {
    return tableRow(s) && s.find(L'-') != npos && s.find_first_not_of(L" |:-\t") == npos;
}

// A setext heading's underline: = for the first level, - for the second
int setextLevel(std::wstring_view s) {
    if (leading(s) > 3) return 0;
    std::wstring_view t = trimmed(s);
    if (t.empty() || t.find_first_not_of(t[0]) != npos) return 0;
    if (t[0] == L'=') return 1;
    return t[0] == L'-' && t.size() >= 2 ? 2 : 0;
}

// A line of a paragraph: a setext underline below it makes it a heading
bool paragraphLine(const App& app, size_t i) {
    std::wstring_view s = lineAt(app, i);
    if (trimmed(s).empty() || leading(s) > 3 || tableRow(s) || divider(s)) return false;
    wchar_t c = s[leading(s)];
    if (c == L'<' || c == L'>') return false;
    return live::lineMark(app.editorText, app.editorLineStarts[i]).kind == live::LineMark::None;
}

// The lines worked out from the top down to `upTo`, again after any change
void walk(const App& app, State& S, size_t upTo) {
    const size_t count = app.editorLineStarts.size();
    if (!count) return;
    if (S.revision != app.editorTextRevision || S.lines.size() != count) {
        S.revision = app.editorTextRevision;
        S.lines.assign(count, Line{});
        S.fences.clear();
        S.walked = 0;
        S.inFence = S.inMath = S.inComment = false;
        // The properties: --- on the first line, closed by --- or ... further down
        S.frontEnd = 0;
        if (count > 2 && trimmed(lineAt(app, 0)) == L"---") {
            for (size_t i = 1; i < count; ++i) {
                std::wstring_view t = trimmed(lineAt(app, i));
                if (t == L"---" || t == L"...") {
                    S.frontEnd = i;
                    break;
                }
            }
        }
        if (S.frontEnd) {
            Fence yaml;
            yaml.first = 1;
            yaml.language = LanguageYaml;
            S.fences.push_back(yaml);
        }
    }
    upTo = std::min(upTo, count - 1);
    for (size_t i = S.walked; i <= upTo; ++i) {
        Line& L = S.lines[i];
        std::wstring_view s = lineAt(app, i);
        if (S.frontEnd && i <= S.frontEnd) {
            L.kind = i == 0 || i == S.frontEnd ? K_FRONT_MARK : K_CODE;
            L.fence = 0;
            continue;
        }
        if (S.inFence) {
            // It closes on the same character, at least as many, and nothing after
            L.fence = (int)S.fences.size() - 1;
            wchar_t ch = 0;
            size_t run = fenceRun(s, ch);
            std::wstring_view t = trimmed(s);
            if (run >= S.fenceRun && ch == S.fenceChar && t.find_first_not_of(ch) == npos) {
                L.kind = K_FENCE;
                S.inFence = false;
            } else {
                L.kind = K_CODE;
            }
            continue;
        }
        std::wstring_view t = trimmed(s);
        if (S.inMath) {
            bool closes = t.size() >= 2 && (t.compare(0, 2, L"$$") == 0 || t.substr(t.size() - 2) == L"$$");
            L.kind = closes ? K_MATH_MARK : K_MATH;
            if (closes) S.inMath = false;
            continue;
        }
        if (S.inComment) {
            L.kind = K_COMMENT;
            if (s.find(L"-->") != npos) S.inComment = false;
            continue;
        }
        wchar_t ch = 0;
        if (size_t run = fenceRun(s, ch)) {
            L.kind = K_FENCE;
            S.inFence = true;
            S.fenceChar = ch;
            S.fenceRun = run;
            // Its language: the first word after the marks
            std::wstring_view info = trimmed(s.substr(leading(s) + run));
            Fence f;
            f.first = i + 1;
            f.language = detectLanguage(std::wstring(info.substr(0, std::min(info.find_first_of(L" {"), info.size()))));
            S.fences.push_back(f);
            L.fence = (int)S.fences.size() - 1;
            continue;
        }
        if (t.size() >= 2 && t.compare(0, 2, L"$$") == 0) {
            // $$x$$ on one line closes where it opens
            L.kind = K_MATH_MARK;
            if (!(t.size() > 4 && t.substr(t.size() - 2) == L"$$")) S.inMath = true;
            continue;
        }
        if (t.size() >= 4 && t.compare(0, 4, L"<!--") == 0 && s.find(L"-->") == npos) {
            L.kind = K_COMMENT;
            S.inComment = true;
            continue;
        }
        // An underline makes the paragraph's lines above it a heading
        int level = i > 0 && S.lines[i - 1].kind == K_TEXT && !S.lines[i - 1].heading ? setextLevel(s) : 0;
        if (level && paragraphLine(app, i - 1)) {
            L.kind = K_RULE;
            for (size_t k = i; k-- > 0 && S.lines[k].kind == K_TEXT && paragraphLine(app, k);) S.lines[k].heading = (unsigned char)level;
            continue;
        }
        L.kind = K_TEXT;
        L.head = tableRow(s) && i + 1 < count && delimiterRow(lineAt(app, i + 1));
    }
    S.walked = std::max(S.walked, upTo + 1);
}

// The lexer's state at the start of a line of a fence's code
SyntaxState stateAt(const App& app, Fence& f, size_t line) {
    if (f.states.empty()) f.states.emplace_back();
    while (f.first + f.states.size() <= line) {
        SyntaxState next = f.states.back();
        std::wstring text(lineAt(app, f.first + f.states.size() - 1));
        tokenizeLine(text, f.language, next);
        f.states.push_back(next);
    }
    return f.states[line - f.first];
}

unsigned long long mix(unsigned long long hash, const void* data, size_t size) {
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

// What the colours are made from: the target, the theme, the font, the setting
unsigned long long paletteKey(const App& app) {
    const D2DTheme& th = app.theme;
    unsigned long long h = 1469598103934665603ull;
    h = mix(h, &app.renderTargetGeneration, sizeof app.renderTargetGeneration);
    const void* target = app.renderTarget;
    h = mix(h, &target, sizeof target);
    D2D1_COLOR_F colours[] = {th.text, th.accent, th.link, th.code, themeInlineCodeColor(th), th.syntaxComment,
                              themeHeadingColor(th, 1), themeHeadingColor(th, 2), themeHeadingColor(th, 3),
                              themeHeadingColor(th, 4), themeHeadingColor(th, 5), themeHeadingColor(th, 6),
                              th.syntaxKeyword, th.syntaxString, th.syntaxNumber, th.syntaxFunction, th.syntaxType,
                              th.syntaxControlFlow};
    h = mix(h, colours, sizeof colours);
    for (const auto& c : {th.boldText, th.italicText}) {
        D2D1_COLOR_F colour = c.value_or(D2D1::ColorF(0, 0, 0, 0));
        h = mix(h, &colour, sizeof colour);
    }
    if (th.codeFontFamily) h = mix(h, th.codeFontFamily, wcslen(th.codeFontFamily) * sizeof(wchar_t));
    float size = app.editorTextFormat ? app.editorTextFormat->GetFontSize() : 0.0f;
    h = mix(h, &size, sizeof size);
    bool on = app.editorHighlight;
    h = mix(h, &on, sizeof on);
    return h;
}

// Whether bold and italic keep the editor font's widths, as a monospace
// font's do: then they change nothing a caret, a click or a wrap measures
bool keepsWidth(App& app) {
    static const wchar_t sample[] = L"Wim0 _.,W";
    const UINT32 n = (UINT32)(sizeof sample / sizeof sample[0] - 1);
    IDWriteTextLayout* l = nullptr;
    if (!app.dwriteFactory || !app.editorTextFormat ||
        FAILED(app.dwriteFactory->CreateTextLayout(sample, n, app.editorTextFormat, 10000.0f, 100.0f, &l)) || !l) {
        return false;
    }
    auto width = [&] {
        DWRITE_TEXT_METRICS m{};
        l->GetMetrics(&m);
        return m.widthIncludingTrailingWhitespace;
    };
    float plain = width();
    l->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, {0, n});
    float bold = width();
    l->SetFontWeight(DWRITE_FONT_WEIGHT_NORMAL, {0, n});
    l->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, {0, n});
    float italic = width();
    l->Release();
    return std::fabs(bold - plain) < 0.01f && std::fabs(italic - plain) < 0.01f;
}

bool palette(App& app, State& S) {
    if (!app.renderTarget) return false;
    unsigned long long key = paletteKey(app);
    if (key == S.key && S.mark) return true;
    S.key = key;
    const D2DTheme& th = app.theme;
    auto make = [&](ComPtr<ID2D1SolidColorBrush>& brush, D2D1_COLOR_F c) {
        brush.Reset();
        app.renderTarget->CreateSolidColorBrush(c, &brush);
    };
    D2D1_COLOR_F muted = th.text;
    muted.a = 0.45f;
    make(S.mark, muted);
    make(S.accent, th.accent);
    make(S.link, th.link);
    make(S.code, th.code);
    make(S.inlineCode, themeInlineCodeColor(th));
    make(S.comment, th.syntaxComment);
    for (int i = 0; i < 6; ++i) make(S.heading[i], themeHeadingColor(th, i + 1));
    S.bold.Reset();
    S.italic.Reset();
    if (th.boldText) make(S.bold, *th.boldText);
    if (th.italicText) make(S.italic, *th.italicText);
    S.tokens.clear();
    S.keepsWidth = keepsWidth(app);
    return S.mark != nullptr;
}

// A brush for a code token's colour, made once per colour
ID2D1SolidColorBrush* tokenBrush(App& app, State& S, D2D1_COLOR_F c) {
    unsigned key = ((unsigned)(c.r * 255) << 24) | ((unsigned)(c.g * 255) << 16) | ((unsigned)(c.b * 255) << 8) |
                   (unsigned)(c.a * 255);
    for (auto& entry : S.tokens) {
        if (entry.first == key) return entry.second.Get();
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    app.renderTarget->CreateSolidColorBrush(c, &brush);
    S.tokens.push_back({key, brush});
    return brush.Get();
}

// A line of Markdown
void styleText(App& app, State& S, IDWriteTextLayout* layout, std::wstring_view s, size_t lineStart, const Line& L) {
    const size_t n = s.size();
    auto paint = [&](ID2D1Brush* brush, size_t at, size_t length) {
        if (length && at < n) layout->SetDrawingEffect(brush, {(UINT32)at, (UINT32)std::min(length, n - at)});
    };
    auto bold = [&](size_t at, size_t length) {
        if (S.keepsWidth && length && at < n) layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, {(UINT32)at, (UINT32)std::min(length, n - at)});
    };
    auto italic = [&](size_t at, size_t length) {
        if (S.keepsWidth && length && at < n) layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, {(UINT32)at, (UINT32)std::min(length, n - at)});
    };
    if (divider(s)) {
        paint(S.mark.Get(), 0, n);
        return;
    }
    // A table: its pipes faint, its delimiter row all faint, its header bold
    if (tableRow(s)) {
        if (delimiterRow(s)) {
            paint(S.mark.Get(), 0, n);
            return;
        }
        if (L.head) bold(0, n);
    }
    // The marks that start a block: quotes, then a list's, a task's or a heading's
    size_t q = live::quoteLength(app.editorText, lineStart);
    if (q) {
        for (size_t i = 0; i < q; ++i) {
            if (s[i] == L'>') paint(S.accent.Get(), i, 1);
        }
    }
    live::LineMark m = live::lineMark(app.editorText, lineStart + q);
    size_t content = q + m.indent + m.length;
    const size_t markAt = q + m.indent;
    switch (m.kind) {
        case live::LineMark::Heading:
            paint(S.mark.Get(), markAt, (size_t)m.number);
            paint(S.heading[std::clamp(m.number, 1, 6) - 1].Get(), content, n - content);
            bold(markAt, n - markAt);
            break;
        case live::LineMark::Bullet:
        case live::LineMark::Number:
            paint(S.accent.Get(), markAt, m.length);
            break;
        case live::LineMark::Task:
            paint(S.accent.Get(), markAt, m.length);
            // A done task's words go faint, as on the page
            if (m.done) paint(S.mark.Get(), content, n - content);
            break;
        default:
            break;
    }
    if (L.heading) {
        paint(S.heading[L.heading - 1].Get(), content, n - content);
        bold(content, n - content);
    }
    // A callout's kind
    if (q && s.compare(content, 2, L"[!") == 0) {
        size_t close = s.find(L']', content);
        if (close != npos) {
            paint(S.accent.Get(), content, close - content + 1);
            bold(content, close - content + 1);
        }
    }
    // A footnote's or a link's definition: its label; a link's address faint,
    // a footnote's words read on
    if (m.kind == live::LineMark::None && !q && leading(s) <= 3 && leading(s) < n && s[leading(s)] == L'[') {
        size_t open = leading(s);
        size_t close = s.find(L"]:", open);
        if (close != npos && s.find(L'[', open + 1) > close) {
            paint(S.link.Get(), open, close - open + 2);
            if (s[open + 1] != L'^') {
                paint(S.mark.Get(), close + 2, n - close - 2);
                return;
            }
            content = close + 2;
        }
    }
    // HTML: tags faint, comments in the comment colour, an address in <> a link
    for (size_t i = content; i < n; ++i) {
        if (s[i] != L'<') continue;
        if (s.compare(i, 4, L"<!--") == 0) {
            size_t end = s.find(L"-->", i + 4);
            size_t stop = end == npos ? n : end + 3;
            paint(S.comment.Get(), i, stop - i);
            i = stop - 1;
            continue;
        }
        size_t j = i + 1 + (i + 1 < n && s[i + 1] == L'/');
        size_t close = s.find(L'>', i + 1);
        if (j >= n || !iswalpha(s[j]) || close == npos) continue;
        std::wstring_view inside = s.substr(i + 1, close - i - 1);
        bool address = inside.find(L"://") != npos || (inside.find(L'@') != npos && inside.find(L' ') == npos);
        paint(address ? S.link.Get() : S.mark.Get(), i, close - i + 1);
        i = close;
    }
    // A table's pipes, past code spans
    if (tableRow(s)) {
        bool code = false;
        for (size_t i = 0; i < n; ++i) {
            if (s[i] == L'`') code = !code;
            if (s[i] == L'|' && !code && (i == 0 || s[i - 1] != L'\\')) paint(S.mark.Get(), i, 1);
        }
    }
    // Inline Markdown, as the page reads it
    S.spans.clear();
    live::inlineSpans(s, content, S.spans);
    for (const live::InlineSpan& span : S.spans) {
        switch (span.kind) {
            case live::INL_CODE:
            case live::INL_MATH:
                paint(S.inlineCode.Get(), span.at, span.length);
                break;
            case live::INL_WIKI:
            case live::INL_FOOTNOTE:
            case live::INL_LINK:
            case live::INL_ADDRESS:
                paint(S.link.Get(), span.at, span.length);
                break;
            case live::INL_IMAGE:
                paint(S.link.Get(), span.at, span.length);
                italic(span.at, span.length);
                break;
            case live::INL_STRONG:
                bold(span.at, span.length);
                if (S.bold) paint(S.bold.Get(), span.at, span.length);
                break;
            case live::INL_EM:
                italic(span.at, span.length);
                if (S.italic) paint(S.italic.Get(), span.at, span.length);
                break;
            case live::INL_STRIKE:
                if (span.length) layout->SetStrikethrough(TRUE, {(UINT32)span.at, (UINT32)span.length});
                break;
            case live::INL_HIGHLIGHT:
                break;
            default:
                // Every mark: backticks, dollars, brackets, an address in (),
                // asterisks, tildes, a backslash
                paint(S.mark.Get(), span.at, span.length);
                break;
        }
    }
}

}  // namespace

void sourceHighlightFrame(App& app) {
    State& S = state(app);
    unsigned long long key = paletteKey(app);
    if (key != S.painted) {
        // Lines laid out before held other colours, or none
        S.painted = key;
        app.clearEditorLineLayoutCache();
    }
}

void sourceHighlightLine(App& app, IDWriteTextLayout* layout, size_t lineStart, size_t lineLength) {
    if (!app.editorHighlight || !layout || !lineLength || app.editorLineStarts.empty()) return;
    State& S = state(app);
    if (!palette(app, S)) return;
    const std::vector<size_t>& starts = app.editorLineStarts;
    size_t line = (size_t)(std::upper_bound(starts.begin(), starts.end(), lineStart) - starts.begin());
    if (!line || starts[line - 1] != lineStart) return;
    --line;
    // A setext underline and a table's delimiter row are read from the line
    // below; a paragraph's lines wait for the line under the paragraph
    walk(app, S, line + 1);
    if (S.lines[line].kind == K_TEXT && paragraphLine(app, line)) {
        for (size_t k = line + 1; k < S.lines.size() && S.lines[k].kind == K_TEXT && paragraphLine(app, k); ++k) walk(app, S, k + 1);
    }
    const Line& L = S.lines[line];
    std::wstring_view s = lineAt(app, line);
    s = s.substr(0, std::min(s.size(), lineLength));
    const UINT32 n = (UINT32)s.size();
    switch (L.kind) {
        case K_FRONT_MARK:
        case K_MATH_MARK:
        case K_RULE:
            layout->SetDrawingEffect(S.mark.Get(), {0, n});
            return;
        case K_MATH:
            layout->SetDrawingEffect(S.inlineCode.Get(), {0, n});
            return;
        case K_COMMENT:
            layout->SetDrawingEffect(S.comment.Get(), {0, n});
            return;
        case K_FENCE: {
            // Its marks faint, its language in the accent
            layout->SetDrawingEffect(S.mark.Get(), {0, n});
            wchar_t ch = 0;
            size_t run = fenceRun(s, ch);
            size_t word = run ? s.find_first_not_of(L" \t", leading(s) + run) : npos;
            if (word != npos) {
                size_t end = std::min(s.find_first_of(L" {", word), s.size());
                layout->SetDrawingEffect(S.accent.Get(), {(UINT32)word, (UINT32)(end - word)});
            }
            return;
        }
        case K_CODE: {
            // In its language's colours; code with none in the code colour
            if (L.fence < 0 || L.fence >= (int)S.fences.size()) return;
            Fence& f = S.fences[(size_t)L.fence];
            if (!f.language) {
                layout->SetDrawingEffect(S.code.Get(), {0, n});
                return;
            }
            SyntaxState syntax = stateAt(app, f, line);
            std::wstring text(s);
            for (const SyntaxToken& token : tokenizeLine(text, f.language, syntax)) {
                if (token.text.empty()) continue;
                size_t at = (size_t)(token.text.data() - text.data());
                layout->SetDrawingEffect(tokenBrush(app, S, getTokenColor(app.theme, token.tokenType)),
                                         {(UINT32)at, (UINT32)token.text.size()});
            }
            return;
        }
        default:
            styleText(app, S, layout, s, lineStart, L);
            return;
    }
}
