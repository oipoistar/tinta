// Live text, drawn (live_text.h): the text laid out with its Markdown
// showing only on the caret's line, the slash menu and [[ names under the
// caret, and the format bar over a selection.

// DirectWrite does the work here: compiled for size
#pragma optimize("s", on)

#include "live_text.h"
#include "d2d_init.h"
#include "i18n.h"
#include "syntax.h"
#include "ui_paint.h"
#include "utils.h"

#include <dwrite_3.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>

using Microsoft::WRL::ComPtr;

namespace live {

namespace {

// Drawn in place of Markdown's own characters on the lines away from the
// caret: nothing at all, a bullet, a task box (ticked or not), a quote's
// bar or a divider's rule. The text stays as typed; only its look changes.
class MarkObject final : public IDWriteInlineObject {
public:
    enum Kind { Hidden, Bullet, Box, Checked, Quote, Rule, Space };
    MarkObject(Kind kind, float size, float width, ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, D2D1_COLOR_F text,
               D2D1_COLOR_F accent)
        : kind_(kind), size_(size), width_(width), target_(target), brush_(brush), text_(text), accent_(accent) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteInlineObject)) {
            *out = static_cast<IDWriteInlineObject*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG left = (ULONG)InterlockedDecrement(&refs_);
        if (!left) delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE Draw(void*, IDWriteTextRenderer*, FLOAT x, FLOAT y, BOOL, BOOL, IUnknown*) override {
        if (!target_ || !brush_ || kind_ == Hidden) return S_OK;
        float u = size_;
        float mid = y + u * 0.8f - u * 0.3f;  // halfway up a lowercase letter
        auto color = [&](D2D1_COLOR_F c, float a) {
            c.a = a;
            brush_->SetColor(c);
        };
        if (kind_ == Bullet) {
            color(text_, 0.7f);
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + u * 0.2f, mid), u * 0.12f, u * 0.12f), brush_);
        } else if (kind_ == Box || kind_ == Checked) {
            D2D1_ROUNDED_RECT box =
                D2D1::RoundedRect(D2D1::RectF(x + u * 0.08f, mid - u * 0.36f, x + u * 0.8f, mid + u * 0.36f), u * 0.14f, u * 0.14f);
            if (kind_ == Checked) {
                color(accent_, 1.0f);
                target_->FillRoundedRectangle(box, brush_);
                color(D2D1::ColorF(1, 1, 1), 1.0f);
                target_->DrawLine(D2D1::Point2F(x + u * 0.24f, mid), D2D1::Point2F(x + u * 0.4f, mid + u * 0.17f), brush_, u * 0.1f);
                target_->DrawLine(D2D1::Point2F(x + u * 0.4f, mid + u * 0.17f), D2D1::Point2F(x + u * 0.66f, mid - u * 0.2f),
                                  brush_, u * 0.1f);
            } else {
                color(text_, 0.6f);
                target_->DrawRoundedRectangle(box, brush_, u * 0.07f);
            }
        } else if (kind_ == Quote) {
            color(accent_, 0.6f);
            target_->FillRectangle(D2D1::RectF(x + u * 0.08f, y - u * 0.2f, x + u * 0.2f, y + u * 1.25f), brush_);
        } else if (kind_ == Rule) {
            color(text_, 0.3f);
            target_->FillRectangle(D2D1::RectF(x, mid, x + width_, mid + std::max(1.0f, u * 0.07f)), brush_);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override {
        // No taller than the line's own letters, so lines keep their height
        static const float widths[] = {0.0f, 0.55f, 1.05f, 1.05f, 0.45f, 0.0f, 0.0f};
        metrics->width = width_ > 0 || kind_ == Rule || kind_ == Space ? width_ : widths[kind_] * size_;
        metrics->height = size_;
        metrics->baseline = size_ * 0.8f;
        metrics->supportsSideways = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS* overhangs) override {
        *overhangs = {};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBreakConditions(DWRITE_BREAK_CONDITION* before, DWRITE_BREAK_CONDITION* after) override {
        *before = *after = DWRITE_BREAK_CONDITION_NEUTRAL;
        return S_OK;
    }

private:
    volatile LONG refs_ = 1;
    Kind kind_;
    float size_, width_;
    ID2D1RenderTarget* target_;
    ID2D1SolidColorBrush* brush_;
    D2D1_COLOR_F text_, accent_;
};

// Formats the engine lays text out with, kept for the factory's life
IDWriteTextFormat* baseFormat(App& app, const std::wstring& face, float size, bool wrap) {
    static std::map<std::wstring, IDWriteTextFormat*> cache;
    static IDWriteFactory* owner = nullptr;
    if (owner != app.dwriteFactory) {
        for (auto& entry : cache) {
            if (entry.second) entry.second->Release();
        }
        cache.clear();
        owner = app.dwriteFactory;
    }
    std::wstring key = face + L"|" + std::to_wstring((int)std::lround(size * 8)) + (wrap ? L"w" : L"n");
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    IDWriteTextFormat* fmt = nullptr;
    app.dwriteFactory->CreateTextFormat(face.empty() ? L"Segoe UI" : face.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, std::max(1.0f, size), L"en-us",
                                        &fmt);
    if (fmt) {
        useUiFontFallback(app, fmt);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if (!wrap) fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    cache[key] = fmt;
    return fmt;
}

// A line's own height in a format, over its size
float naturalHeight(App& app, IDWriteTextFormat* fmt) {
    static std::map<IDWriteTextFormat*, float> cache;
    auto it = cache.find(fmt);
    if (it != cache.end()) return it->second;
    float ratio = 1.33f;
    IDWriteTextLayout* probe = nullptr;
    app.dwriteFactory->CreateTextLayout(L"Ag", 2, fmt, 1000.0f, 1000.0f, &probe);
    if (probe) {
        DWRITE_LINE_METRICS line{};
        UINT32 count = 0;
        if (SUCCEEDED(probe->GetLineMetrics(&line, 1, &count)) && count && fmt->GetFontSize() > 0) {
            ratio = line.height / fmt->GetFontSize();
        }
        probe->Release();
    }
    cache[fmt] = ratio;
    return ratio;
}

std::wstring lookKey(const Look& look) {
    wchar_t buf[96];
    swprintf_s(buf, L"|%.2f|%.2f|%.2f|%.2f|%d|%.3f%.3f%.3f", look.size, look.codeSize, look.headingSize[0], look.lineHeight,
               look.markdown ? 1 : 0, look.text.r, look.accent.g, look.code.b);
    return look.face + L"|" + look.headingFace + L"|" + look.codeFace + buf;
}

bool blankLine(std::wstring_view s) { return s.find_first_not_of(L" \t") == std::wstring_view::npos; }

// ---, *** or ___ alone on a line
bool dividerLine(std::wstring_view s) {
    wchar_t c = 0;
    int n = 0;
    for (wchar_t ch : s) {
        if (ch == L' ') continue;
        if (ch != L'-' && ch != L'*' && ch != L'_') return false;
        if (c && ch != c) return false;
        c = ch;
        ++n;
    }
    return n >= 3;
}

}  // namespace

Look pageLook(const App& app) {
    Look look;
    const D2DTheme& th = app.theme;
    float scale = app.contentScale * app.zoomFactor;
    look.face = th.fontFamily && *th.fontFamily ? th.fontFamily : L"Segoe UI";
    look.headingFace = th.headingFontFamily && *th.headingFontFamily ? th.headingFontFamily : look.face;
    look.codeFace = th.codeFontFamily && *th.codeFontFamily ? th.codeFontFamily : L"Consolas";
    // The reading view's sizes (d2d_init.cpp) and line height (render.cpp)
    look.size = 16.0f * scale;
    look.codeSize = 14.0f * scale;
    const float headings[] = {32, 26, 22, 18, 16, 14};
    for (int i = 0; i < 6; ++i) {
        look.headingSize[i] = headings[i] * scale;
        look.heading[i] = themeHeadingColor(th, i + 1);
    }
    look.lineHeight = 1.7f;
    look.codeInset = 12.0f * scale;
    look.listIndent = 24.0f * scale;
    look.text = th.text;
    look.accent = th.accent;
    look.link = th.link;
    look.code = th.code;
    look.codeBack = th.codeBackground;
    look.inlineCodeBack = themeInlineCodeBackground(th);
    look.quote = th.blockquoteBorder;
    look.mark = themeHighlightBackground(th);
    return look;
}

size_t caretLineOf(const LiveText& e, size_t from, size_t to) {
    if (e.caret < from || e.caret > to) return (size_t)-1;
    return (size_t)std::count(e.text.begin() + (std::ptrdiff_t)from, e.text.begin() + (std::ptrdiff_t)e.caret, L'\n');
}

IDWriteTextLayout* layoutOf(App& app, const LiveText& e, size_t from, size_t to, float width, const Look& look,
                            LayoutCache& cache, size_t caretLine) {
    from = std::min(from, e.text.size());
    to = std::clamp(to, from, e.text.size());
    const std::wstring key = lookKey(look);
    if (cache.layout && cache.from == from && cache.text.size() == to - from &&
        e.text.compare(from, to - from, cache.text) == 0 && std::fabs(cache.width - width) < 0.01f && cache.key == key &&
        cache.caretLine == caretLine && cache.generation == app.renderTargetGeneration) {
        return cache.layout.Get();
    }
    cache.layout.Reset();
    cache.backs.clear();
    cache.text.assign(e.text, from, to - from);
    cache.from = from;
    cache.width = width;
    cache.key = key;
    cache.caretLine = caretLine;
    const std::wstring& t = cache.text;
    IDWriteTextFormat* fmt = baseFormat(app, look.face, look.size, look.markdown);
    if (!fmt) return nullptr;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(t.c_str(), (UINT32)t.size(), fmt, std::max(1.0f, width), 1.0e6f, &layout);
    if (!layout) return nullptr;
    cache.layout.Attach(layout);
    if (cache.generation != app.renderTargetGeneration || !cache.markBrush) {
        cache.markBrush.Reset();
        cache.linkBrush.Reset();
        cache.glyphBrush.Reset();
        cache.codeBrush.Reset();
        cache.tokenBrushes.clear();
        for (auto& b : cache.headingBrush) b.Reset();
        app.renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &cache.markBrush);
        app.renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &cache.linkBrush);
        app.renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &cache.glyphBrush);
        app.renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &cache.codeBrush);
        for (auto& b : cache.headingBrush) app.renderTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &b);
    }
    cache.generation = app.renderTargetGeneration;
    // Lines as tall as the page's: a share of each line's own height
    cache.factor = 1.0f;
    if (look.lineHeight > 0) {
        ComPtr<IDWriteTextLayout3> spaced;
        if (SUCCEEDED(layout->QueryInterface(IID_PPV_ARGS(&spaced)))) {
            cache.factor = look.lineHeight / naturalHeight(app, fmt);
            DWRITE_LINE_SPACING spacing{};
            spacing.method = DWRITE_LINE_SPACING_METHOD_PROPORTIONAL;
            spacing.height = cache.factor;
            spacing.baseline = cache.factor;
            spacing.leadingBefore = 0.0f;
            spacing.fontLineGapUsage = DWRITE_FONT_LINE_GAP_USAGE_DEFAULT;
            spaced->SetLineSpacing(&spacing);
        }
    }
    if (!look.markdown || !cache.markBrush) return layout;
    D2D1_COLOR_F muted = look.text;
    muted.a = 0.45f;
    cache.markBrush->SetColor(muted);
    cache.linkBrush->SetColor(look.link);
    cache.codeBrush->SetColor(look.code);
    for (int i = 0; i < 6; ++i) {
        if (cache.headingBrush[i]) cache.headingBrush[i]->SetColor(look.heading[i]);
    }

    // On the caret's line the marks are muted; on the others they go, or
    // turn into bullets, task boxes, bars and rules
    bool here = false;
    float lineSize = look.size;
    auto range = [](size_t at, size_t length) { return DWRITE_TEXT_RANGE{(UINT32)at, (UINT32)length}; };
    auto mark = [&](size_t at, size_t length, MarkObject::Kind kind, float objectWidth = 0) {
        if (!length) return;
        if (here) {
            layout->SetDrawingEffect(cache.markBrush.Get(), range(at, length));
            return;
        }
        MarkObject* object = new MarkObject(kind, lineSize, objectWidth, app.renderTarget, cache.glyphBrush.Get(), look.text,
                                            look.accent);
        layout->SetInlineObject(object, range(at, length));
        object->Release();
    };
    auto code = [&](size_t at, size_t length) {
        layout->SetFontFamilyName(look.codeFace.c_str(), range(at, length));
        layout->SetFontSize(look.codeSize * lineSize / look.size, range(at, length));
        layout->SetDrawingEffect(cache.codeBrush.Get(), range(at, length));
    };
    // A brush for a token's colour, made once per colour
    auto tokenBrush = [&](D2D1_COLOR_F c) -> ID2D1SolidColorBrush* {
        unsigned key = ((unsigned)(c.r * 255) << 24) | ((unsigned)(c.g * 255) << 16) | ((unsigned)(c.b * 255) << 8) |
                       (unsigned)(c.a * 255);
        for (auto& entry : cache.tokenBrushes) {
            if (entry.first == key) return entry.second.Get();
        }
        ComPtr<ID2D1SolidColorBrush> brush;
        app.renderTarget->CreateSolidColorBrush(c, &brush);
        cache.tokenBrushes.push_back({key, brush});
        return brush.Get();
    };
    int language = 0;
    SyntaxState syntax;
    size_t start = 0, lineIndex = 0;
    bool inFence = false, inMath = false, inFront = from == 0 && t.compare(0, 3, L"---") == 0;
    size_t fenceStart = 0, frontEnd = 0;
    while (start <= t.size()) {
        size_t end = t.find(L'\n', start);
        if (end == std::wstring::npos) end = t.size();
        here = lineIndex == caretLine;
        lineSize = look.size;
        std::wstring_view lineText(t.data() + start, end - start);
        size_t lead = lineText.find_first_not_of(L' ');
        std::wstring_view rest = lead == std::wstring_view::npos ? std::wstring_view() : lineText.substr(lead);
        bool fence = rest.size() >= 3 && (rest.compare(0, 3, L"```") == 0 || rest.compare(0, 3, L"~~~") == 0);
        bool mathFence = rest.size() >= 2 && rest.compare(0, 2, L"$$") == 0 && !inFence;
        if (inFront) {
            // Front matter: the properties as typed, in the code face
            code(start, end - start);
            if (lineIndex > 0 && rest == L"---") {
                inFront = false;
                frontEnd = end;
                cache.backs.push_back({0, frontEnd, look.codeBack, true});
            }
        } else if (fence || inFence) {
            code(start, end - start);
            if (fence) {
                layout->SetDrawingEffect(cache.markBrush.Get(), range(start, end - start));
                if (!inFence) {
                    // Its language, after the fence's marks
                    fenceStart = start;
                    size_t marks = rest.find_first_not_of(rest[0]);
                    std::wstring_view info = marks == std::wstring_view::npos ? std::wstring_view() : rest.substr(marks);
                    size_t word = info.find_first_of(L" {");
                    language = detectLanguage(std::wstring(info.substr(0, word)));
                    syntax = SyntaxState();
                } else {
                    cache.backs.push_back({fenceStart, end - fenceStart, look.codeBack, true});
                }
                inFence = !inFence;
            } else if (language && end > start) {
                // Its tokens in the page's colours
                std::wstring line(lineText);
                for (const SyntaxToken& token : tokenizeLine(line, language, syntax)) {
                    if (token.text.empty()) continue;
                    size_t at = (size_t)(token.text.data() - line.data());
                    layout->SetDrawingEffect(tokenBrush(getTokenColor(app.theme, token.tokenType)), range(start + at, token.text.size()));
                }
            }
        } else if (mathFence || inMath) {
            code(start, end - start);
            if (mathFence) {
                layout->SetDrawingEffect(cache.markBrush.Get(), range(start, 2 + lead));
                // $$x$$ on one line closes where it opens
                if (!(rest.size() > 4 && rest.substr(rest.size() - 2) == L"$$")) inMath = !inMath;
            }
        } else if (lead != std::wstring_view::npos && rest[0] == L'|') {
            // A table row: its pipes line up in the code face, muted with the
            // row under the header
            code(start, end - start);
            bool delimiter = rest.find_first_not_of(L" |:-") == std::wstring_view::npos;
            layout->SetDrawingEffect(delimiter ? cache.markBrush.Get() : nullptr, range(start, end - start));
            for (size_t i = 0; i < lineText.size() && !delimiter; ++i) {
                if (lineText[i] == L'|' && (i == 0 || lineText[i - 1] != L'\\')) {
                    layout->SetDrawingEffect(cache.markBrush.Get(), range(start + i, 1));
                }
            }
        } else if (!here && dividerLine(lineText) && !lineText.empty()) {
            mark(start, end - start, MarkObject::Rule, std::max(0.0f, width - 2.0f));
        } else {
            // Up to three spaces may come before a heading's marks
            size_t h0 = lead != std::wstring_view::npos && lead <= 3 ? lead : 0;
            size_t hashes = 0;
            while (h0 + hashes < lineText.size() && lineText[h0 + hashes] == L'#') ++hashes;
            if (hashes >= 1 && hashes <= 6 && h0 + hashes < lineText.size() && lineText[h0 + hashes] == L' ') {
                DWRITE_TEXT_RANGE whole = range(start, end - start);
                lineSize = look.headingSize[hashes - 1];
                layout->SetFontFamilyName(look.headingFace.c_str(), whole);
                layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, whole);
                layout->SetFontSize(lineSize, whole);
                layout->SetDrawingEffect(cache.headingBrush[hashes - 1].Get(), whole);
                mark(start, h0 + hashes + 1, MarkObject::Hidden);
            } else if (lead != std::wstring_view::npos) {
                // List, task and quote marks
                size_t at = start + lead;
                if (rest[0] == L'>') {
                    size_t marks = 1;
                    while (marks < rest.size() && (rest[marks] == L'>' || rest[marks] == L' ')) ++marks;
                    mark(at, marks, MarkObject::Quote);
                } else if (rest.size() >= 2 && (rest[0] == L'-' || rest[0] == L'*' || rest[0] == L'+') && rest[1] == L' ') {
                    bool task = rest.size() >= 5 && rest[2] == L'[' && rest[4] == L']' && (rest.size() == 5 || rest[5] == L' ');
                    // On the page a list's items stand a step in per level,
                    // their text a step past the mark
                    float step = look.listIndent;
                    if (step > 0 && lead) mark(start, lead, MarkObject::Space, step * (float)(lead / 2));
                    if (task) {
                        bool done = rest[3] == L'x' || rest[3] == L'X';
                        size_t length = rest.size() >= 6 ? 6 : 5;
                        if (step > 0) {
                            mark(at, length, done ? MarkObject::Checked : MarkObject::Box, step);
                        } else {
                            mark(at, 2, MarkObject::Hidden);
                            mark(at + 2, 3, done ? MarkObject::Checked : MarkObject::Box);
                        }
                        if (done && !here && end > at + 6) {
                            DWRITE_TEXT_RANGE text = range(at + 6, end - at - 6);
                            layout->SetDrawingEffect(cache.markBrush.Get(), text);
                            layout->SetStrikethrough(TRUE, text);
                        }
                    } else if (step > 0) {
                        mark(at, 2, MarkObject::Bullet, step);
                    } else {
                        mark(at, 1, MarkObject::Bullet);
                    }
                } else if (look.listIndent > 0 && iswdigit(rest[0])) {
                    // A number keeps its digits; the space after it reaches the item's text
                    size_t digits = 0;
                    while (digits < rest.size() && iswdigit(rest[digits])) ++digits;
                    if (digits < 10 && digits + 1 < rest.size() && (rest[digits] == L'.' || rest[digits] == L')') &&
                        rest[digits + 1] == L' ') {
                        if (lead) mark(start, lead, MarkObject::Space, look.listIndent * (float)(lead / 3));
                        if (!here) {
                            IDWriteTextLayout* number = nullptr;
                            app.dwriteFactory->CreateTextLayout(rest.data(), (UINT32)(digits + 1), fmt, 1000.0f, 1000.0f, &number);
                            float used = number ? paint::metrics(number).widthIncludingTrailingWhitespace : 0;
                            if (number) number->Release();
                            mark(at + digits + 1, 1, MarkObject::Space, std::max(look.size * 0.25f, look.listIndent - used));
                        }
                    }
                }
            }
            // Inline spans, past the line's mark: a task's box is no link
            LineMark lineMarks = lineMark(t, start);
            for (size_t i = lineMarks.kind == LineMark::None ? 0 : lineMarks.indent + lineMarks.length; i < lineText.size(); ++i) {
                wchar_t c = lineText[i];
                size_t at = start + i;
                if (c == L'\\' && i + 1 < lineText.size()) {
                    ++i;
                } else if (c == L'`') {
                    size_t ticks = 1;
                    while (i + ticks < lineText.size() && lineText[i + ticks] == L'`') ++ticks;
                    size_t close = lineText.find(std::wstring(ticks, L'`'), i + ticks);
                    if (close == std::wstring_view::npos) break;
                    code(at, close - i + ticks);
                    cache.backs.push_back({at + ticks, close - i - ticks, look.inlineCodeBack, false});
                    mark(at, ticks, MarkObject::Hidden);
                    mark(start + close, ticks, MarkObject::Hidden);
                    i = close + ticks - 1;
                } else if (c == L'$' && i + 1 < lineText.size() && lineText[i + 1] != L' ' && lineText[i + 1] != L'$') {
                    size_t close = lineText.find(L'$', i + 1);
                    if (close == std::wstring_view::npos || lineText[close - 1] == L' ') continue;
                    code(at, close - i + 1);
                    layout->SetDrawingEffect(cache.markBrush.Get(), range(at, 1));
                    layout->SetDrawingEffect(cache.markBrush.Get(), range(start + close, 1));
                    i = close;
                } else if (c == L'[' && i + 1 < lineText.size() && lineText[i + 1] == L'[') {
                    // [[a note]] or [[a note|shown as]]: the name in the link colour
                    size_t close = lineText.find(L"]]", i + 2);
                    if (close == std::wstring_view::npos) continue;
                    size_t bar = lineText.find(L'|', i + 2);
                    size_t shown = bar != std::wstring_view::npos && bar < close ? bar + 1 : i + 2;
                    layout->SetDrawingEffect(cache.linkBrush.Get(), range(start + shown, close - shown));
                    mark(at, shown - i, MarkObject::Hidden);
                    mark(start + close, 2, MarkObject::Hidden);
                    i = close + 1;
                } else if (c == L'[' && i + 1 < lineText.size() && lineText[i + 1] == L'^') {
                    // A footnote's mark
                    size_t close = lineText.find(L']', i + 2);
                    if (close == std::wstring_view::npos) continue;
                    layout->SetDrawingEffect(cache.linkBrush.Get(), range(at, close - i + 1));
                    i = close;
                } else if (c == L'[' || (c == L'!' && i + 1 < lineText.size() && lineText[i + 1] == L'[')) {
                    // [text](address) and ![alt](address): the text in the link colour
                    size_t open = c == L'!' ? i + 1 : i;
                    size_t middle = lineText.find(L"](", open + 1);
                    if (middle == std::wstring_view::npos) continue;
                    size_t close = lineText.find(L')', middle + 2);
                    if (close == std::wstring_view::npos) continue;
                    layout->SetDrawingEffect(c == L'!' ? cache.markBrush.Get() : cache.linkBrush.Get(),
                                             range(start + open + 1, middle - open - 1));
                    if (c == L'!') layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range(start + open + 1, middle - open - 1));
                    mark(at, open - i + 1, MarkObject::Hidden);
                    mark(start + middle, close - middle + 1, MarkObject::Hidden);
                    i = close;
                } else if ((c == L'*' || c == L'_') && i + 1 < lineText.size() && lineText[i + 1] == c) {
                    std::wstring pair(2, c);
                    size_t close = lineText.find(pair, i + 2);
                    if (close == std::wstring_view::npos || close == i + 2) continue;
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range(at, close - i + 2));
                    mark(at, 2, MarkObject::Hidden);
                    mark(start + close, 2, MarkObject::Hidden);
                    i = close + 1;
                } else if ((c == L'*' || c == L'_') && i + 1 < lineText.size() && lineText[i + 1] != L' ' &&
                           (i == 0 || !iswalnum(lineText[i - 1]))) {
                    size_t close = lineText.find(c, i + 1);
                    if (close == std::wstring_view::npos || close == i + 1 || lineText[close - 1] == L' ') continue;
                    layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range(at, close - i + 1));
                    mark(at, 1, MarkObject::Hidden);
                    mark(start + close, 1, MarkObject::Hidden);
                    i = close;
                } else if ((c == L'~' || c == L'=') && i + 1 < lineText.size() && lineText[i + 1] == c) {
                    std::wstring pair(2, c);
                    size_t close = lineText.find(pair, i + 2);
                    if (close == std::wstring_view::npos || close == i + 2) continue;
                    if (c == L'~') layout->SetStrikethrough(TRUE, range(at + 2, close - i - 2));
                    else cache.backs.push_back({at + 2, close - i - 2, look.mark, false});
                    mark(at, 2, MarkObject::Hidden);
                    mark(start + close, 2, MarkObject::Hidden);
                    i = close + 1;
                } else if ((c == L'h' || c == L'w') && (i == 0 || lineText[i - 1] == L' ' || lineText[i - 1] == L'(') &&
                           (lineText.compare(i, 7, L"http://") == 0 || lineText.compare(i, 8, L"https://") == 0 ||
                            lineText.compare(i, 4, L"www.") == 0)) {
                    size_t stop = i;
                    while (stop < lineText.size() && lineText[stop] != L' ' && lineText[stop] != L')') ++stop;
                    layout->SetDrawingEffect(cache.linkBrush.Get(), range(at, stop - i));
                    i = stop - 1;
                }
            }
        }
        ++lineIndex;
        if (end == t.size()) break;
        start = end + 1;
    }
    // A fence still open at the end runs to it
    if (inFence) cache.backs.push_back({fenceStart, t.size() - fenceStart, look.codeBack, true});
    return layout;
}

bool caretPoint(IDWriteTextLayout* layout, size_t from, size_t at, float& x, float& y, float& height) {
    if (!layout || at < from) return false;
    DWRITE_HIT_TEST_METRICS m{};
    if (FAILED(layout->HitTestTextPosition((UINT32)(at - from), FALSE, &x, &y, &m))) return false;
    height = m.height;
    return true;
}

size_t positionAt(IDWriteTextLayout* layout, size_t from, size_t to, float x, float y) {
    if (!layout) return from;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestPoint(x, y, &trailing, &inside, &m);
    size_t at = from + m.textPosition + (trailing ? m.length : 0);
    return std::min(at, to);
}

size_t taskBoxAt(IDWriteTextLayout* layout, const LiveText& e, size_t from, size_t to, float x, float y) {
    if (!layout) return (size_t)-1;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    layout->HitTestPoint(x, y, &trailing, &inside, &m);
    if (!inside) return (size_t)-1;
    size_t at = from + m.textPosition;
    if (at > to) return (size_t)-1;
    size_t ls = lineStart(e.text, at);
    LineMark mark = lineMark(e.text, ls);
    if (mark.kind != LineMark::Task) return (size_t)-1;
    size_t box = ls + mark.indent + 2;
    // On the caret's line the box is typed text, not a box
    if (lineStart(e.text, e.caret) == ls || at < ls + mark.indent || at >= box + 3) return (size_t)-1;
    return ls;
}

void draw(App& app, const LiveText& e, IDWriteTextLayout* layout, const LayoutCache& cache, size_t from, const Look& look,
          bool caretOn, float pixel) {
    if (!layout) return;
    ID2D1RenderTarget* rt = app.renderTarget;
    const float width = layout->GetMaxWidth();
    const size_t length = cache.text.size();
    auto rects = [&](size_t at, size_t count, std::vector<DWRITE_HIT_TEST_METRICS>& out) {
        out.clear();
        UINT32 n = 0;
        layout->HitTestTextRange((UINT32)at, (UINT32)count, 0, 0, nullptr, 0, &n);
        out.resize(n);
        if (n && FAILED(layout->HitTestTextRange((UINT32)at, (UINT32)count, 0, 0, out.data(), n, &n))) out.clear();
        out.resize(n);
    };
    std::vector<DWRITE_HIT_TEST_METRICS> hits;
    // Backgrounds: fenced blocks across the box, spans behind their text
    for (const auto& back : cache.backs) {
        if (back.at >= length && length) continue;
        if (back.block) {
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            DWRITE_HIT_TEST_METRICS first{}, last{};
            layout->HitTestTextPosition((UINT32)back.at, FALSE, &x0, &y0, &first);
            size_t lastAt = back.length ? back.at + back.length - 1 : back.at;
            layout->HitTestTextPosition((UINT32)std::min(lastAt, length), FALSE, &x1, &y1, &last);
            app.brush->SetColor(back.color);
            rt->FillRectangle(D2D1::RectF(-look.codeInset, first.top, width + look.codeInset, last.top + last.height), app.brush);
        } else if (back.length) {
            rects(back.at, back.length, hits);
            for (const auto& h : hits) {
                paint::fillRound(app, D2D1::RectF(h.left - 2 * pixel, h.top + h.height * 0.12f, h.left + h.width + 2 * pixel,
                                                  h.top + h.height * 0.92f),
                                 3 * pixel, back.color);
            }
        }
    }
    // The selection
    if (e.hasSelection()) {
        size_t a = std::max(e.selStart(), from), b = std::min(e.selEnd(), from + length);
        if (b > a) {
            rects(a - from, b - a, hits);
            app.brush->SetColor(paint::alpha(look.accent, 0.28f));
            for (const auto& h : hits) {
                rt->FillRectangle(D2D1::RectF(h.left, h.top, h.left + std::max(h.width, 4 * pixel), h.top + h.height), app.brush);
            }
        }
    }
    app.brush->SetColor(look.text);
    rt->DrawTextLayout(D2D1::Point2F(0, 0), layout, app.brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    if (e.caret < from || e.caret > from + length) return;
    if (look.markdown) {
        // The caret's line starts #word: a mark where the heading's space belongs
        size_t ls = lineStart(e.text, e.caret);
        size_t hashes = 0;
        while (ls + hashes < e.text.size() && e.text[ls + hashes] == L'#') ++hashes;
        if (ls >= from && hashes >= 1 && hashes <= 6 && ls + hashes < e.text.size() && e.text[ls + hashes] != L' ' &&
            e.text[ls + hashes] != L'\n' && e.text[ls + hashes] != L'#') {
            float gx = 0, gy = 0;
            DWRITE_HIT_TEST_METRICS gm{};
            layout->HitTestTextPosition((UINT32)(ls + hashes - from), FALSE, &gx, &gy, &gm);
            float base = gm.top + gm.height - gm.height * (1.0f - 1.0f / cache.factor) * 0.1f;
            app.brush->SetColor(paint::alpha(look.accent, 0.9f));
            rt->FillRectangle(D2D1::RectF(gx - 3.5f * pixel, base - 1.5f * pixel, gx + 3.5f * pixel, base), app.brush);
            rt->FillRectangle(D2D1::RectF(gx - 0.75f * pixel, base - 5.0f * pixel, gx + 0.75f * pixel, base), app.brush);
        }
    }
    if (caretOn) {
        float cx = 0, cy = 0;
        DWRITE_HIT_TEST_METRICS hm{};
        layout->HitTestTextPosition((UINT32)(e.caret - from), FALSE, &cx, &cy, &hm);
        // As tall as the line's letters, not its spacing
        float h = hm.height / cache.factor;
        float top = hm.top + (hm.height - h) * 0.72f;
        app.brush->SetColor(look.accent);
        rt->FillRectangle(D2D1::RectF(cx - 0.75f * pixel, top, cx + 1.25f * pixel, top + h), app.brush);
    }
}

// --- The popup and the format bar ---------------------------------------------

// A row's badge in the slash menu: a few letters, or a little drawing
void drawBadge(App& app, int insert, const D2D1_RECT_F& r) {
    paint::Surface s = paint::surface(app);
    paint::fillRound(app, r, dpi(app, 5.0f), paint::alpha(s.text, 0.07f));
    float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f, u = dpi(app, 1.0f);
    D2D1_COLOR_F ink = paint::alpha(s.text, 0.75f);
    auto L = [&](float x0, float y0, float x1, float y1) {
        paint::line(app, D2D1::Point2F(cx + x0 * u, cy + y0 * u), D2D1::Point2F(cx + x1 * u, cy + y1 * u), ink, 1.4f * u,
                    paint::roundStyle(app));
    };
    auto R = [&](float x0, float y0, float x1, float y1, float rad) {
        paint::strokeRound(app, D2D1::RectF(cx + x0 * u, cy + y0 * u, cx + x1 * u, cy + y1 * u), rad * u, ink, 1.4f * u);
    };
    switch (insert) {
        case INS_TASKS:
            R(-5, -5, 5, 5, 2);
            L(-2.5f, 0, -0.5f, 2.2f);
            L(-0.5f, 2.2f, 3, -2);
            return;
        case INS_TABLE:
            R(-6, -5, 6, 5, 1.5f);
            L(-6, -1.5f, 6, -1.5f);
            L(-6, 1.8f, 6, 1.8f);
            L(0, -5, 0, 5);
            return;
        case INS_IMAGE:
            R(-6, -5, 6, 5, 1.5f);
            L(-4.5f, 3, -1, -1);
            L(-1, -1, 1.5f, 1.5f);
            L(1.5f, 1.5f, 3, 0);
            L(3, 0, 4.5f, 3);
            return;
        case INS_LINK:
            R(-6.5f, -2.5f, 0.5f, 2.5f, 2.5f);
            R(-0.5f, -2.5f, 6.5f, 2.5f, 2.5f);
            return;
        case INS_GANTT:
            L(-5, -3.5f, 1, -3.5f);
            L(-2, 0, 4, 0);
            L(1, 3.5f, 5.5f, 3.5f);
            return;
        case kBadgeAlignLeft:
        case kBadgeAlignCenter:
        case kBadgeAlignRight: {
            // Lines of a paragraph set as the column is
            const float widths[] = {11, 7, 9};
            for (int i = 0; i < 3; ++i) {
                float w = widths[i], x0 = insert == kBadgeAlignLeft ? -5.5f : insert == kBadgeAlignRight ? 5.5f - w : -w * 0.5f;
                L(x0, -4.0f + 4.0f * (float)i, x0 + w, -4.0f + 4.0f * (float)i);
            }
            return;
        }
        default:
            break;
    }
    const wchar_t* glyph = L"";
    bool tiny = false;
    switch (insert) {
        case INS_H1: glyph = L"H1"; break;
        case INS_H2: glyph = L"H2"; break;
        case INS_H3: glyph = L"H3"; break;
        case INS_QUOTE: glyph = L"\x201C"; break;
        case INS_DIVIDER: glyph = L"\x2014"; break;
        case INS_BULLETS: glyph = L"\x2022"; break;
        case INS_NUMBERS: glyph = L"1."; break;
        case INS_NOTE: glyph = L"i"; break;
        case INS_TIP: glyph = L"\x2605"; break;
        case INS_IMPORTANT: glyph = L"!"; break;
        case INS_WARNING: glyph = L"\x25B3"; break;
        case INS_CAUTION: glyph = L"\x00D7"; break;
        case INS_CODE: glyph = L"{ }"; tiny = true; break;
        case INS_INLINE_MATH: glyph = L"x\x00B2"; break;
        case INS_MATH: glyph = L"\x2211"; break;
        case INS_FLOWCHART: glyph = L"\x2192"; break;
        case INS_SEQUENCE: glyph = L"\x21C4"; break;
        case INS_CLASS: glyph = L"\x25AD"; break;
        case INS_STATE: glyph = L"\x25D0"; break;
        case INS_PIE: glyph = L"\x25D4"; break;
        case INS_MINDMAP: glyph = L"\x25CB"; break;
        case INS_PLANTUML: glyph = L"UML"; tiny = true; break;
        case INS_FOOTNOTE: glyph = L"\x00B9"; break;
        case INS_PROPERTIES: glyph = L"\x2261"; break;
        case kBadgeText: glyph = L"T"; break;
        default: break;
    }
    IDWriteTextLayout* l = paint::makeLayout(app, glyph, paint::textFormat(app, paint::uiFace(), dpi(app, tiny ? 9.5f : 12.0f),
                                                                         DWRITE_FONT_WEIGHT_SEMI_BOLD, false),
                                             r.right - r.left, r.bottom - r.top);
    if (!l) return;
    DWRITE_TEXT_METRICS m = paint::metrics(l);
    paint::drawText(app, l, cx - m.width * 0.5f, cy - m.height * 0.5f, ink);
    l->Release();
}

bool formatOn(const LiveText& e, int format) {
    const std::wstring& t = e.text;
    size_t a = e.selStart(), b = e.selEnd();
    if (a == b && format < FMT_H1) {
        // No selection: the word at the caret, as the format keys take it
        while (a > 0 && isWordChar(t[a - 1])) --a;
        while (b < t.size() && isWordChar(t[b])) ++b;
        if (a == b) return false;
    }
    auto wrapped = [&](const std::wstring& m) {
        size_t n = m.size();
        if (a >= n && t.compare(a - n, n, m) == 0 && t.compare(b, n, m) == 0) {
            if (m == L"*") return !(a >= 2 && t[a - 2] == L'*') || (a >= 3 && t[a - 3] == L'*');
            return true;
        }
        return b - a >= 2 * n && t.compare(a, n, m) == 0 && t.compare(b - n, n, m) == 0;
    };
    switch (format) {
        case FMT_BOLD: return wrapped(L"**");
        case FMT_ITALIC: return wrapped(L"*") || wrapped(L"_");
        case FMT_STRIKE: return wrapped(L"~~");
        case FMT_CODE: return wrapped(L"`");
        case FMT_MARK: return wrapped(L"==");
        case FMT_H1:
        case FMT_H2:
        case FMT_H3: {
            LineMark m = lineMark(t, lineStart(t, a));
            return m.kind == LineMark::Heading && m.number == format - FMT_H1 + 1;
        }
        default: return false;
    }
}

void applyFormat(LiveText& e, int format) {
    switch (format) {
        case FMT_BOLD: toggleWrap(e, L"**"); break;
        case FMT_ITALIC: toggleWrap(e, L"*"); break;
        case FMT_STRIKE: toggleWrap(e, L"~~"); break;
        case FMT_CODE: toggleWrap(e, L"`"); break;
        case FMT_MARK: toggleWrap(e, L"=="); break;
        case FMT_LINK: makeLink(e); break;
        case FMT_H1:
        case FMT_H2:
        case FMT_H3: setHeading(e, format - FMT_H1 + 1); break;
        default: break;
    }
}

void drawPopup(App& app, LiveText& e, D2D1_POINT_2F caretTop, float caretBottom, const D2D1_RECT_F& bounds) {
    e.popupHits.clear();
    if (e.popup == LiveText::Popup::None || e.choices.empty()) return;
    paint::Surface s = paint::surface(app);
    const bool slash = e.popup == LiveText::Popup::Slash;
    const float rowH = dpi(app, slash ? 32.0f : 26.0f), w = dpi(app, slash ? 300.0f : 280.0f), pad = dpi(app, 4.0f);
    const int count = (int)e.choices.size();
    const int shown = std::min(count, 8);
    if (e.choice < e.popupTop) e.popupTop = e.choice;
    if (e.choice >= e.popupTop + shown) e.popupTop = e.choice - shown + 1;
    e.popupTop = std::clamp(e.popupTop, 0, std::max(0, count - shown));
    float h = rowH * (float)shown + pad * 2;
    float x = std::clamp(caretTop.x - dpi(app, 10.0f), bounds.left + dpi(app, 6.0f),
                         std::max(bounds.left + dpi(app, 6.0f), bounds.right - w - dpi(app, 6.0f)));
    float y = caretBottom + dpi(app, 6.0f);
    if (y + h > bounds.bottom - dpi(app, 6.0f)) y = std::max(bounds.top + dpi(app, 6.0f), caretTop.y - h - dpi(app, 6.0f));
    D2D1_RECT_F box = D2D1::RectF(x, y, x + w, y + h);
    paint::shadow(app, box, dpi(app, 8.0f), s.shadow, 1.4f);
    paint::fillRound(app, box, dpi(app, 8.0f), s.fill);
    paint::strokeRound(app, box, dpi(app, 8.0f), s.border, dpi(app, 1.0f));
    IDWriteTextFormat* nameFormat = paint::textFormat(app, paint::uiFace(), dpi(app, 13.0f), DWRITE_FONT_WEIGHT_NORMAL, false);
    IDWriteTextFormat* detailFormat = paint::textFormat(app, slash ? L"Consolas" : paint::uiFace(), dpi(app, 11.0f),
                                                        DWRITE_FONT_WEIGHT_NORMAL, false);
    for (int k = e.popupTop; k < e.popupTop + shown; ++k) {
        const LiveText::Choice& item = e.choices[k];
        float top = box.top + pad + rowH * (float)(k - e.popupTop);
        D2D1_RECT_F row = D2D1::RectF(box.left + pad, top, box.right - pad, top + rowH);
        if (k == e.choice) paint::fillRound(app, row, dpi(app, 5.0f), paint::alpha(s.accent, 0.18f));
        else if (k == e.hoverHit) paint::fillRound(app, row, dpi(app, 5.0f), paint::alpha(s.text, 0.06f));
        float textLeft = row.left + dpi(app, 8.0f);
        if (slash) {
            float b = dpi(app, 22.0f);
            D2D1_RECT_F mark = D2D1::RectF(row.left + dpi(app, 5.0f), (row.top + row.bottom - b) * 0.5f, row.left + dpi(app, 5.0f) + b,
                                           (row.top + row.bottom + b) * 0.5f);
            drawBadge(app, item.insert, mark);
            textLeft = mark.right + dpi(app, 9.0f);
        }
        IDWriteTextLayout* dl = paint::makeLayout(app, item.detail, detailFormat, 400, rowH);
        DWRITE_TEXT_METRICS dm = paint::metrics(dl);
        IDWriteTextLayout* nl =
            paint::makeLayout(app, item.label, nameFormat, std::max(1.0f, row.right - textLeft - dm.width - dpi(app, 18.0f)), rowH);
        DWRITE_TEXT_METRICS nm = paint::metrics(nl);
        paint::drawText(app, nl, textLeft, (row.top + row.bottom - nm.height) * 0.5f, s.text);
        paint::drawText(app, dl, row.right - dm.width - dpi(app, 8.0f), (row.top + row.bottom - dm.height) * 0.5f,
                        paint::alpha(s.text, 0.45f));
        if (nl) nl->Release();
        if (dl) dl->Release();
        e.popupHits.push_back({row, k});
    }
    // More rows than shown: where the window is among them
    if (count > shown) {
        float trackTop = box.top + pad * 2, trackH = h - pad * 4;
        float thumbH = std::max(dpi(app, 16.0f), trackH * (float)shown / (float)count);
        float thumbTop = trackTop + (trackH - thumbH) * (float)e.popupTop / (float)(count - shown);
        paint::fillRound(app, D2D1::RectF(box.right - dpi(app, 4.5f), thumbTop, box.right - dpi(app, 2.0f), thumbTop + thumbH),
                         dpi(app, 1.25f), paint::alpha(s.text, 0.25f));
    }
}

void drawFormatButton(App& app, int f, const D2D1_RECT_F& r, bool on, bool hover, bool enabled) {
    paint::Surface s = paint::surface(app);
    on = on && enabled;
    if (on) paint::fillRound(app, r, dpi(app, 5.0f), paint::alpha(s.accent, 0.22f));
    else if (hover && enabled) paint::fillRound(app, r, dpi(app, 5.0f), paint::alpha(s.text, 0.08f));
    D2D1_COLOR_F ink = on ? s.accent : paint::alpha(s.text, enabled ? 0.85f : 0.3f);
    float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f, u = dpi(app, 1.0f);
    if (f == FMT_LINK) {
        paint::strokeRound(app, D2D1::RectF(cx - 7.5f * u, cy - 3 * u, cx + 0.5f * u, cy + 3 * u), 3 * u, ink, 1.5f * u);
        paint::strokeRound(app, D2D1::RectF(cx - 0.5f * u, cy - 3 * u, cx + 7.5f * u, cy + 3 * u), 3 * u, ink, 1.5f * u);
        return;
    }
    const wchar_t* glyph = L"";
    const wchar_t* face = paint::uiFace();
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    switch (f) {
        case FMT_BOLD: glyph = L"B"; weight = DWRITE_FONT_WEIGHT_BOLD; break;
        case FMT_ITALIC: glyph = L"I"; face = L"Georgia"; weight = DWRITE_FONT_WEIGHT_NORMAL; break;
        case FMT_STRIKE: glyph = L"S"; weight = DWRITE_FONT_WEIGHT_NORMAL; break;
        case FMT_CODE: glyph = L"</>"; face = L"Consolas"; weight = DWRITE_FONT_WEIGHT_NORMAL; break;
        case FMT_MARK: glyph = L"A"; weight = DWRITE_FONT_WEIGHT_NORMAL; break;
        case FMT_H1: glyph = L"H1"; break;
        case FMT_H2: glyph = L"H2"; break;
        default: glyph = L"H3"; break;
    }
    float px = dpi(app, f == FMT_CODE ? 11.5f : f >= FMT_H1 ? 12.5f : 14.0f);
    float side = r.bottom - r.top;
    IDWriteTextLayout* l = paint::makeLayout(app, glyph, paint::textFormat(app, face, px, weight, false), side * 2, side);
    if (!l) return;
    if (f == FMT_ITALIC) l->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, {0, 1});
    DWRITE_TEXT_METRICS m = paint::metrics(l);
    if (f == FMT_MARK) {
        paint::fillRound(app, D2D1::RectF(cx - 7 * u, cy + 3 * u, cx + 7 * u, cy + 7 * u), 1.5f * u, themeHighlightBackground(app.theme));
    }
    paint::drawText(app, l, cx - m.width * 0.5f, cy - m.height * 0.5f - (f == FMT_MARK ? 1.5f * u : 0), ink);
    if (f == FMT_STRIKE) paint::line(app, D2D1::Point2F(cx - 6 * u, cy), D2D1::Point2F(cx + 6 * u, cy), ink, 1.3f * u);
    l->Release();
}

void drawFormatBar(App& app, LiveText& e, D2D1_POINT_2F top, float bottom, const D2D1_RECT_F& bounds) {
    e.barHits.clear();
    if (!e.hasSelection() || e.selectingWithMouse || e.popup != LiveText::Popup::None || !e.multiline || e.menu.open) return;
    if (e.text.find(L'\n', e.selStart()) < e.selEnd()) return;  // one line's words, not a passage
    paint::Surface s = paint::surface(app);
    const float b = dpi(app, 30.0f), pad = dpi(app, 4.0f), gap = dpi(app, 7.0f);
    const int count = FMT_COUNT;
    float w = b * count + pad * 2 + gap;
    float h = b + pad * 2;
    float x = std::clamp(top.x - w * 0.3f, bounds.left + dpi(app, 6.0f),
                         std::max(bounds.left + dpi(app, 6.0f), bounds.right - w - dpi(app, 6.0f)));
    float y = top.y - h - dpi(app, 8.0f);
    if (y < bounds.top + dpi(app, 6.0f)) y = bottom + dpi(app, 8.0f);
    D2D1_RECT_F box = D2D1::RectF(x, y, x + w, y + h);
    paint::shadow(app, box, dpi(app, 8.0f), s.shadow, 1.3f);
    paint::fillRound(app, box, dpi(app, 8.0f), s.fill);
    paint::strokeRound(app, box, dpi(app, 8.0f), s.border, dpi(app, 1.0f));
    float bx = box.left + pad;
    for (int f = 0; f < count; ++f) {
        if (f == FMT_H1) {
            float sx = bx + gap * 0.5f;
            paint::line(app, D2D1::Point2F(sx, box.top + pad * 2), D2D1::Point2F(sx, box.bottom - pad * 2), s.border, dpi(app, 1.0f));
            bx += gap;
        }
        D2D1_RECT_F r = D2D1::RectF(bx, box.top + pad, bx + b, box.top + pad + b);
        drawFormatButton(app, f, r, formatOn(e, f), e.hoverHit == 1000 + f);
        e.barHits.push_back({r, 1000 + f});
        bx += b;
    }
}

int hitAt(const LiveText& e, float x, float y) {
    for (const auto& hit : e.popupHits) {
        if (x >= hit.first.left && x < hit.first.right && y >= hit.first.top && y < hit.first.bottom) return hit.second;
    }
    for (const auto& hit : e.barHits) {
        if (x >= hit.first.left && x < hit.first.right && y >= hit.first.top && y < hit.first.bottom) return hit.second;
    }
    return -1;
}

}  // namespace live
