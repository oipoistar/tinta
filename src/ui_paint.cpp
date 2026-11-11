// Shared drawing helpers (ui_paint.h).

#pragma optimize("s", on)

#include "ui_paint.h"
#include "ui_scale.h"
#include "d2d_init.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace paint {

D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

D2D1_COLOR_F alpha(D2D1_COLOR_F c, float a) {
    c.a *= a;
    return c;
}

IDWriteTextFormat* textFormat(App& app, const wchar_t* family, float px, DWRITE_FONT_WEIGHT weight, bool wrap) {
    static std::map<std::wstring, IDWriteTextFormat*> cache;
    static IDWriteFactory* owner = nullptr;
    // Kept for the factory's life (a few hundred sizes at most), so a
    // format's address names it for the cards' cached layouts
    if (owner != app.dwriteFactory) {
        for (auto& entry : cache) {
            if (entry.second) entry.second->Release();
        }
        cache.clear();
        owner = app.dwriteFactory;
    }
    // The interface language moved to other CJK faces, or away from them:
    // the formats made before follow (nullptr is the system's fallback)
    static int fallbackOrder = -2;
    if (fallbackOrder != app.uiFontFallbackOrder) {
        fallbackOrder = app.uiFontFallbackOrder;
        for (auto& entry : cache) {
            IDWriteTextFormat1* format1 = nullptr;
            if (entry.second && SUCCEEDED(entry.second->QueryInterface(__uuidof(IDWriteTextFormat1),
                                                                      reinterpret_cast<void**>(&format1)))) {
                format1->SetFontFallback(app.uiFontFallback);
                format1->Release();
            }
        }
    }
    px = std::max(1.0f, std::round(px * 4.0f) / 4.0f);
    std::wstring key = std::wstring(family) + L"|" + std::to_wstring((int)(px * 4)) + L"|" + std::to_wstring((int)weight) +
                       (wrap ? L"w" : L"n");
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    IDWriteTextFormat* fmt = nullptr;
    app.dwriteFactory->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px,
                                        L"en-us", &fmt);
    if (fmt) {
        useUiFontFallback(app, fmt);
        if (!wrap) fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        IDWriteInlineObject* sign = nullptr;
        if (SUCCEEDED(app.dwriteFactory->CreateEllipsisTrimmingSign(fmt, &sign)) && sign) {
            DWRITE_TRIMMING trim{wrap ? DWRITE_TRIMMING_GRANULARITY_WORD : DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            fmt->SetTrimming(&trim, sign);
            sign->Release();
        }
    }
    cache[key] = fmt;
    return fmt;
}

const wchar_t* uiFace() { return L"Segoe UI"; }

IDWriteTextLayout* makeLayout(App& app, const std::wstring& text, IDWriteTextFormat* fmt, float maxW, float maxH) {
    if (!fmt) return nullptr;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, std::max(1.0f, maxW), std::max(1.0f, maxH),
                                        &layout);
    return layout;
}

DWRITE_TEXT_METRICS metrics(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS m{};
    if (layout) layout->GetMetrics(&m);
    return m;
}

void drawText(App& app, IDWriteTextLayout* layout, float x, float y, D2D1_COLOR_F color) {
    if (!layout) return;
    app.brush->SetColor(color);
    app.renderTarget->DrawTextLayout(D2D1::Point2F(x, y), layout, app.brush,
                                     D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT | D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void fillRound(App& app, const D2D1_RECT_F& r, float radius, D2D1_COLOR_F color) {
    app.brush->SetColor(color);
    app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), app.brush);
}

void strokeRound(App& app, const D2D1_RECT_F& r, float radius, D2D1_COLOR_F color, float width, ID2D1StrokeStyle* style) {
    app.brush->SetColor(color);
    app.renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), app.brush, width, style);
}

void line(App& app, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F color, float width, ID2D1StrokeStyle* style) {
    app.brush->SetColor(color);
    app.renderTarget->DrawLine(a, b, app.brush, width, style);
}

ID2D1StrokeStyle* dashedStyle(App& app) {
    static ID2D1StrokeStyle* style = nullptr;
    static ID2D1Factory* owner = nullptr;
    if (!style || owner != app.d2dFactory) {
        D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10.0f,
            D2D1_DASH_STYLE_DASH, 0.0f);
        app.d2dFactory->CreateStrokeStyle(props, nullptr, 0, &style);
        owner = app.d2dFactory;
    }
    return style;
}

ID2D1StrokeStyle* roundStyle(App& app) {
    static ID2D1StrokeStyle* style = nullptr;
    static ID2D1Factory* owner = nullptr;
    if (!style || owner != app.d2dFactory) {
        D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        app.d2dFactory->CreateStrokeStyle(props, nullptr, 0, &style);
        owner = app.d2dFactory;
    }
    return style;
}

void shadow(App& app, const D2D1_RECT_F& r, float radius, D2D1_COLOR_F color, float strength) {
    for (int i = 2; i >= 1; --i) {
        float grow = dpi(app, 1.5f) * i;
        D2D1_RECT_F s = D2D1::RectF(r.left - grow * 0.5f, r.top + grow * 0.6f, r.right + grow * 0.5f, r.bottom + grow * 1.4f);
        fillRound(app, s, radius + grow, alpha(color, strength * (i == 1 ? 0.55f : 0.30f)));
    }
}

Surface pageSurface(const App& app) {
    const D2DTheme& th = app.theme;
    const D2D1_COLOR_F white = D2D1::ColorF(1, 1, 1, 1);
    Surface s;
    s.fill = th.isDark ? mix(th.background, th.text, 0.09f) : mix(th.background, white, 0.9f);
    s.fill.a = 1;
    s.border = alpha(th.text, th.isDark ? 0.16f : 0.15f);
    s.text = th.text;
    s.muted = alpha(th.text, 0.56f);
    s.accent = th.accent;
    s.shadow = D2D1::ColorF(0, 0, 0, th.isDark ? 0.40f : 0.12f);
    return s;
}

Surface surface(const App& app) {
    // A CSS theme's --ui-* colours where it sets them (docs/css-themes.md)
    const D2DTheme& th = app.theme;
    Surface s = pageSurface(app);
    s.fill = themeUiPanel(th, s.fill);
    s.text = themeUiText(th);
    s.border = themeUiBorder(th, alpha(s.text, th.isDark ? 0.16f : 0.15f));
    s.muted = alpha(s.text, 0.56f);
    s.accent = themeUiAccent(th);
    s.shadow = D2D1::ColorF(0, 0, 0, themeUiDark(th) ? 0.40f : 0.12f);
    return s;
}

void label(App& app, const wchar_t* text, uiscale::Type role, const D2D1_RECT_F& box, D2D1_COLOR_F color,
           DWRITE_TEXT_ALIGNMENT align) {
    IDWriteTextFormat* fmt = uiscale::font(app, role);
    if (!fmt || !text || box.right <= box.left) return;
    fmt->SetTextAlignment(align);
    app.brush->SetColor(color);
    app.renderTarget->DrawText(text, (UINT32)wcslen(text), fmt, box, app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
}

float labelWidth(App& app, const wchar_t* text, uiscale::Type role) {
    if (!text || !text[0]) return 0.0f;
    IDWriteTextLayout* layout = makeLayout(app, text, uiscale::font(app, role), 100000.0f, 100.0f);
    if (!layout) return 0.0f;
    const float w = metrics(layout).widthIncludingTrailingWhitespace;
    layout->Release();
    return w;
}

float lineTop(const App& app, float top, float height, uiscale::Type role) {
    return top + (height - dpi(app, uiscale::lineHeight(role))) * 0.5f;
}

void sectionLabel(App& app, const wchar_t* text, float x, float y, float maxW, D2D1_COLOR_F color) {
    std::wstring upper = text ? text : L"";
    if (upper.empty() || maxW <= 0.0f) return;
    CharUpperBuffW(upper.data(), (DWORD)upper.size());
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(upper.c_str(), (UINT32)upper.size(), uiscale::font(app, uiscale::Type::Micro),
                                        maxW, dpi(app, 16.0f), &layout);
    if (!layout) return;
    IDWriteTextLayout1* spaced = nullptr;
    if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(&spaced)))) {
        spaced->SetCharacterSpacing(0.0f, dpi(app, 0.6f), 0.0f, {0, (UINT32)upper.size()});
        spaced->Release();
    }
    app.brush->SetColor(color);
    app.renderTarget->DrawTextLayout(D2D1::Point2F(x, y + dpi(app, 1.0f)), layout, app.brush,
                                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    layout->Release();
}

D2D1_COLOR_F ink(const D2DTheme& theme, float share, float alpha) {
    D2D1_COLOR_F c = themeUiText(theme);
    c.a = share * alpha;
    return c;
}

D2D1_RECT_F toggle(App& app, const D2D1_RECT_F& slot, bool on, const D2DTheme& theme, float alpha) {
    const float w = dpi(app, 40.0f), h = dpi(app, 20.0f);
    const float x = slot.right - w, y = (slot.top + slot.bottom - h) * 0.5f;
    D2D1_COLOR_F track = on ? themeUiAccent(theme) : themeUiText(theme);
    track.a = (on ? 0.9f : 0.25f) * alpha;
    app.brush->SetColor(track);
    app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), h / 2, h / 2),
                                           app.brush);
    const float knobR = h / 2 - dpi(app, uiscale::sp2);
    const float knobX = on ? x + w - h / 2 : x + h / 2;
    app.brush->SetColor(D2D1::ColorF(1, 1, 1, alpha));
    app.renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, y + h / 2), knobR, knobR), app.brush);
    return D2D1::RectF(x, y, x + w, y + h);
}

void closeCross(App& app, const D2D1_RECT_F& box, D2D1_COLOR_F color) {
    const float cx = (box.left + box.right) * 0.5f, cy = (box.top + box.bottom) * 0.5f;
    const float arm = dpi(app, uiscale::sp4);
    app.brush->SetColor(color);
    app.renderTarget->DrawLine(D2D1::Point2F(cx - arm, cy - arm), D2D1::Point2F(cx + arm, cy + arm), app.brush,
                               dpi(app, 1.5f));
    app.renderTarget->DrawLine(D2D1::Point2F(cx + arm, cy - arm), D2D1::Point2F(cx - arm, cy + arm), app.brush,
                               dpi(app, 1.5f));
}

}  // namespace paint

// The interface's type roles (ui_scale.h)
namespace uiscale {

namespace {
struct Role {
    float size, line;
    bool semibold;
};
constexpr Role kRoles[] = {
    {11.0f, 14.0f, true},   // Micro
    {12.0f, 16.0f, false},  // Small
    {13.0f, 18.0f, false},  // Body
    {13.0f, 18.0f, true},   // Strong
    {16.0f, 22.0f, true},   // Subtitle
    {20.0f, 28.0f, true},   // Title
};
}  // namespace

float fontSize(Type role) { return kRoles[(int)role].size; }
float lineHeight(Type role) { return kRoles[(int)role].line; }
bool semibold(Type role) { return kRoles[(int)role].semibold; }

IDWriteTextFormat* font(App& app, Type role, bool wrap) {
    return paint::textFormat(app, paint::uiFace(), dpi(app, fontSize(role)),
                             semibold(role) ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL, wrap);
}

}  // namespace uiscale
