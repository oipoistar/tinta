// Shared drawing helpers (ui_paint.h).

#pragma optimize("s", on)

#include "ui_paint.h"
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

Surface surface(const App& app) {
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

}  // namespace paint
