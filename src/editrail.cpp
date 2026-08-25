// Left tool rail (design t8/t11): a 48dp column that slides in with edit
// mode carrying the formatting controls grouped text / blocks / insert.
// Line numbers live in the editor's own slim gutter beside it, and the
// thread seam (also here) ties source blocks to their render.

#include "editrail.h"
#include "editor.h"
#include "i18n.h"
#include "startpage.h"

#include <algorithm>
#include <string>

// --- Small helpers (same one-shot layout pattern as the start page) ----

static D2D1_COLOR_F railMix(D2D1_COLOR_F base, D2D1_COLOR_F to, float t) {
    D2D1_COLOR_F c;
    c.r = base.r + (to.r - base.r) * t;
    c.g = base.g + (to.g - base.g) * t;
    c.b = base.b + (to.b - base.b) * t;
    c.a = 1.0f;
    return c;
}

static D2D1_COLOR_F railA(D2D1_COLOR_F c, float a) {
    c.a = a;
    return c;
}

static IDWriteTextLayout* railLayout(App& app, const std::wstring& text,
                                     float sizeDip,
                                     DWRITE_FONT_WEIGHT weight,
                                     const wchar_t* family = L"Segoe UI",
                                     DWRITE_FONT_STYLE style =
                                         DWRITE_FONT_STYLE_NORMAL) {
    IDWriteTextFormat* fmt = nullptr;
    app.dwriteFactory->CreateTextFormat(
        family, nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL,
        dpi(app, sizeDip), L"en-us", &fmt);
    if (!fmt) return nullptr;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                        fmt, 4096.0f, 200.0f, &layout);
    fmt->Release();
    return layout;
}

static void railDraw(App& app, IDWriteTextLayout* layout, float x, float y,
                     D2D1_COLOR_F color) {
    if (!layout) return;
    app.brush->SetColor(color);
    app.renderTarget->DrawTextLayout(D2D1::Point2F(x, y), layout,
                                     app.brush);
}

// Centers a glyph layout in a rect, then releases it
static void railGlyph(App& app, const std::wstring& text, float sizeDip,
                      DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                      const wchar_t* family, D2D1_RECT_F r,
                      D2D1_COLOR_F color, bool strike = false) {
    IDWriteTextLayout* l =
        railLayout(app, text, sizeDip, weight, family, style);
    if (!l) return;
    if (strike) {
        DWRITE_TEXT_RANGE all{0, (UINT32)text.size()};
        l->SetStrikethrough(TRUE, all);
    }
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    railDraw(app, l, r.left + (r.right - r.left - m.width) * 0.5f,
             r.top + (r.bottom - r.top - m.height) * 0.5f, color);
    l->Release();
}

struct RailBtn {
    int id;
    const wchar_t* glyph;
    float size;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STYLE style;
    const wchar_t* family;
    bool strike;
    const char* tipKey;      // i18n label
    const wchar_t* tipKeys;  // shortcut shown in the flyout ("" = none)
};

static const RailBtn kTextBtns[] = {
    {1, L"B", 13.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
     L"Segoe UI", false, "rail.bold", L"Ctrl+B"},
    {2, L"I", 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC,
     L"Georgia", false, "rail.italic", L"Ctrl+I"},
    {3, L"S", 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
     L"Segoe UI", true, "rail.strike", L""},
    {4, L"</>", 10.5f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
     L"Consolas", false, "rail.code", L""},
    {5, L"\U0001F517", 12.0f, DWRITE_FONT_WEIGHT_NORMAL,
     DWRITE_FONT_STYLE_NORMAL, L"Segoe UI", false, "rail.link", L"Ctrl+K"},
};
static const RailBtn kBlockBtns[] = {
    {10, L"\u2022\u2261", 12.0f, DWRITE_FONT_WEIGHT_NORMAL,
     DWRITE_FONT_STYLE_NORMAL, L"Segoe UI", false, "rail.bullets", L""},
    {11, L"\u2611", 13.0f, DWRITE_FONT_WEIGHT_NORMAL,
     DWRITE_FONT_STYLE_NORMAL, L"Segoe UI", false, "rail.tasks", L""},
    {12, L"\u275D", 13.0f, DWRITE_FONT_WEIGHT_NORMAL,
     DWRITE_FONT_STYLE_NORMAL, L"Segoe UI", false, "rail.quote", L""},
};
static const RailBtn kInsertBtns[] = {
    {20, L"\u229E", 13.0f, DWRITE_FONT_WEIGHT_NORMAL,
     DWRITE_FONT_STYLE_NORMAL, L"Segoe UI", false, "rail.table", L""},
    {21, L"\u25C8", 13.0f, DWRITE_FONT_WEIGHT_NORMAL,
     DWRITE_FONT_STYLE_NORMAL, L"Segoe UI", false, "rail.diagram", L""},
    {22, L"\U0001F5BC", 12.0f, DWRITE_FONT_WEIGHT_NORMAL,
     DWRITE_FONT_STYLE_NORMAL, L"Segoe UI", false, "rail.image", L""},
};

static const RailBtn* railBtnById(int id) {
    for (const RailBtn& b : kTextBtns) {
        if (b.id == id) return &b;
    }
    for (const RailBtn& b : kBlockBtns) {
        if (b.id == id) return &b;
    }
    for (const RailBtn& b : kInsertBtns) {
        if (b.id == id) return &b;
    }
    return nullptr;
}

// --- Rendering ----------------------------------------------------------

void renderEditRail(App& app) {
    app.editRailHits.clear();
    if (!app.editMode || !app.renderTarget || !app.brush ||
        !app.dwriteFactory) {
        return;
    }

    // Slide-in: advance toward 1 and keep painting until it lands
    if (app.editRailAnim < 1.0f) {
        app.editRailAnim = std::min(1.0f, app.editRailAnim + 0.25f);
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }

    const D2DTheme& th = app.theme;
    float railW = dpi(app, 48.0f);
    float slide = railW * (1.0f - app.editRailAnim);
    float H = (float)app.height;

    D2D1_COLOR_F railBg =
        th.isDark ? railMix(th.background, D2D1::ColorF(0, 0, 0, 1), 0.45f)
                  : railMix(th.background, D2D1::ColorF(0, 0, 0, 1), 0.06f);
    D2D1_COLOR_F text = th.text;
    D2D1_COLOR_F accent = th.accent;
    float accentLum =
        0.299f * accent.r + 0.587f * accent.g + 0.114f * accent.b;
    D2D1_COLOR_F onAccent = accentLum > 0.45f
                                ? D2D1::ColorF(0.05f, 0.08f, 0.15f, 1.0f)
                                : D2D1::ColorF(1.0f, 0.97f, 0.95f, 1.0f);

    app.renderTarget->PushAxisAlignedClip(
        D2D1::RectF(0, 0, railW, H), D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_MATRIX_3X2_F prev;
    app.renderTarget->GetTransform(&prev);
    app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(-slide, 0) *
                                   prev);

    app.brush->SetColor(railBg);
    app.renderTarget->FillRectangle(D2D1::RectF(0, 0, railW, H), app.brush);
    app.brush->SetColor(railA(text, 0.07f));
    app.renderTarget->FillRectangle(
        D2D1::RectF(railW - 1.0f, 0, railW, H), app.brush);

    auto addHit = [&](D2D1_RECT_F r, int id) {
        r.left -= slide;
        r.right -= slide;
        app.editRailHits.push_back({r, id});
    };
    auto sep = [&](float y) {
        app.brush->SetColor(railA(text, 0.09f));
        app.renderTarget->FillRectangle(
            D2D1::RectF((railW - dpi(app, 20.0f)) * 0.5f, y,
                        (railW + dpi(app, 20.0f)) * 0.5f, y + 1.0f),
            app.brush);
    };

    // Logo
    float y = dpi(app, 11.0f);
    startPageEnsureIcon(app);
    if (app.startPageIconBitmap) {
        float iconSize = dpi(app, 20.0f);
        app.renderTarget->DrawBitmap(
            app.startPageIconBitmap,
            D2D1::RectF((railW - iconSize) * 0.5f, y,
                        (railW + iconSize) * 0.5f, y + iconSize));
    }
    y += dpi(app, 20.0f) + dpi(app, 12.0f);
    sep(y);
    y += dpi(app, 9.0f);

    // Tool groups: text style, blocks, insert
    float btn = dpi(app, 32.0f);
    float bx = (railW - btn) * 0.5f;
    auto group = [&](const RailBtn* btns, int count) {
        for (int i = 0; i < count; i++) {
            const RailBtn& b = btns[i];
            D2D1_RECT_F r = D2D1::RectF(bx, y, bx + btn, y + btn);
            bool hov = app.editRailHover == b.id;
            if (hov) {
                app.brush->SetColor(railA(text, 0.07f));
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(r, dpi(app, 7.0f), dpi(app, 7.0f)),
                    app.brush);
            }
            railGlyph(app, b.glyph, b.size, b.weight, b.style, b.family, r,
                      hov ? railA(text, 0.95f) : railA(text, 0.8f),
                      b.strike);
            addHit(r, b.id);
            y += btn + dpi(app, 2.0f);
        }
    };
    group(kTextBtns, 5);
    y += dpi(app, 7.0f);
    sep(y);
    y += dpi(app, 8.0f);
    group(kBlockBtns, 3);
    y += dpi(app, 7.0f);
    sep(y);
    y += dpi(app, 8.0f);
    group(kInsertBtns, 3);
    (void)onAccent;
    (void)accent;

    app.renderTarget->SetTransform(prev);
    app.renderTarget->PopAxisAlignedClip();

    // Flyout label beside the hovered tool (design: "Bold  Ctrl+B")
    const RailBtn* hovBtn = railBtnById(app.editRailHover);
    if (hovBtn && app.folderBrowserFormat) {
        D2D1_RECT_F src{};
        for (const auto& hit : app.editRailHits) {
            if (hit.second == hovBtn->id) {
                src = hit.first;
                break;
            }
        }
        std::wstring label = tr(app, hovBtn->tipKey);
        IDWriteTextLayout* ll =
            railLayout(app, label, 12.0f, DWRITE_FONT_WEIGHT_NORMAL);
        IDWriteTextLayout* kl =
            hovBtn->tipKeys[0]
                ? railLayout(app, hovBtn->tipKeys, 10.5f,
                             DWRITE_FONT_WEIGHT_NORMAL)
                : nullptr;
        if (ll) {
            DWRITE_TEXT_METRICS lm{}, km{};
            ll->GetMetrics(&lm);
            if (kl) kl->GetMetrics(&km);
            float pad = dpi(app, 12.0f);
            float gap = kl ? dpi(app, 8.0f) : 0.0f;
            float w = pad + lm.width + gap + km.width + pad;
            float h = dpi(app, 30.0f);
            float fx = railW + dpi(app, 10.0f);
            float fy = (src.top + src.bottom - h) * 0.5f;
            D2D1_RECT_F fr = D2D1::RectF(fx, fy, fx + w, fy + h);
            D2D1_COLOR_F fbg = th.isDark
                                   ? railMix(th.background,
                                             D2D1::ColorF(0, 0, 0, 1), 0.3f)
                                   : railMix(th.background,
                                             D2D1::ColorF(1, 1, 1, 1), 0.5f);
            fbg.a = 0.96f;
            app.brush->SetColor(fbg);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(fr, dpi(app, 7.0f), dpi(app, 7.0f)),
                app.brush);
            app.brush->SetColor(railA(text, 0.14f));
            app.renderTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(fr, dpi(app, 7.0f), dpi(app, 7.0f)),
                app.brush, 1.0f);
            railDraw(app, ll, fx + pad, fy + (h - lm.height) * 0.5f, text);
            if (kl) {
                railDraw(app, kl, fx + pad + lm.width + gap,
                         fy + (h - km.height) * 0.5f, railA(text, 0.45f));
                kl->Release();
            }
            ll->Release();
        }
    }
}

// --- Input --------------------------------------------------------------

int editRailHitAt(const App& app, float x, float y) {
    for (const auto& hit : app.editRailHits) {
        if (x >= hit.first.left && x <= hit.first.right &&
            y >= hit.first.top && y <= hit.first.bottom) {
            return hit.second;
        }
    }
    return 0;
}

bool editRailMouseDown(App& app, HWND hwnd, int x, int y) {
    if (!app.editMode || (float)x >= editRailWidth(app)) return false;
    int hit = editRailHitAt(app, (float)x, (float)y);
    if (hit != 0) {
        editRailInvoke(app, hwnd, hit);
    }
    return true;  // the rail swallows its column's clicks either way
}

bool editRailMouseMove(App& app, HWND hwnd, int x, int y) {
    if (!app.editMode) return false;
    int hit = (float)x < editRailWidth(app)
                  ? editRailHitAt(app, (float)x, (float)y)
                  : 0;
    if (hit != app.editRailHover) {
        app.editRailHover = hit;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return (float)x < editRailWidth(app);
}

// --- Raw editor insert menu (design t9) ----------------------------------
//
// Clipboard verbs on top, then an INSERT section whose right column shows
// the markdown each item drops at the caret. Table and Diagram open
// flyout submenus: a size grid and the mermaid templates.

struct ECtxItem {
    int id;           // 0 = header, -1 = separator-after marker on prev
    const wchar_t* icon;
    const char* label;    // i18n key ("" for header text below)
    const wchar_t* hint;  // right column (mono)
    bool needsSel;
    bool sub;
    bool sepAfter;
};

static const ECtxItem kECtx[] = {
    {100, L"", "ectx.cut", L"Ctrl+X", true, false, false},
    {101, L"", "ectx.copy", L"Ctrl+C", true, false, false},
    {102, L"", "ectx.paste", L"Ctrl+V", false, false, true},
    {0, L"", "ectx.insert", L"", false, false, false},
    {110, L"\u229E", "ectx.table", L"\u25B8", false, true, false},
    {111, L"\u25C8", "ectx.diagram", L"\u25B8", false, true, false},
    {112, L"\U0001F5BC", "ectx.image", L"![](\u2026)", false, false, false},
    {113, L"</>", "ectx.codeblock", L"```", false, false, false},
    {114, L"\U0001F517", "ectx.link", L"[](\u2026)", false, false, false},
    {115, L"\u2015", "ectx.hr", L"---", false, false, false},
};

struct EDiagItem {
    const wchar_t* icon;
    const char* label;
    const wchar_t* hint;
};
static const EDiagItem kEDiag[] = {
    {L"\u2192", "ectx.d.flow", L"flowchart"},
    {L"\u21C4", "ectx.d.seq", L"sequenceDiagram"},
    {L"\u25AD", "ectx.d.class", L"classDiagram"},
    {L"\u25D0", "ectx.d.state", L"stateDiagram"},
    {L"\u25A4", "ectx.d.gantt", L"gantt"},
    {L"\u25D4", "ectx.d.pie", L"pie"},
    {L"\u270E", "ectx.d.empty", L"```mermaid"},
};

void openEditCtxMenu(App& app, HWND hwnd, float x, float y) {
    editorMoveCaretToPoint(app, (int)x, (int)y);
    app.editCtxOpen = true;
    app.editCtxX = x;
    app.editCtxY = y;
    app.editCtxHover = 0;
    app.editCtxSub = 0;
    app.editCtxSubHover = 0;
    app.editCtxGridC = 3;
    app.editCtxGridR = 4;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void closeEditCtxMenu(App& app) {
    if (!app.editCtxOpen) return;
    app.editCtxOpen = false;
    app.editCtxSub = 0;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void renderEditCtxMenu(App& app) {
    app.editCtxHits.clear();
    app.editCtxSubHits.clear();
    if (!app.editCtxOpen || !app.renderTarget || !app.dwriteFactory) return;

    const D2DTheme& th = app.theme;
    D2D1_COLOR_F text = th.text;
    D2D1_COLOR_F accent = th.accent;
    D2D1_COLOR_F menuBg =
        th.isDark ? railMix(th.background, D2D1::ColorF(0, 0, 0, 1), 0.25f)
                  : railMix(th.background, D2D1::ColorF(1, 1, 1, 1), 0.55f);
    menuBg.a = 0.97f;

    float w = dpi(app, 232.0f);
    float rowH = dpi(app, 34.0f);
    float hdH = dpi(app, 24.0f);
    float sepH = dpi(app, 9.0f);
    float padV = dpi(app, 4.0f);

    float totalH = padV * 2.0f;
    for (const ECtxItem& it : kECtx) {
        totalH += it.id == 0 ? hdH : rowH;
        if (it.sepAfter) totalH += sepH;
    }
    float mx = std::min(app.editCtxX, (float)app.width - w - dpi(app, 8.0f));
    float my = std::min(app.editCtxY,
                        (float)app.height - totalH - dpi(app, 8.0f));
    my = std::max(my, chromeTopHeight(app) + dpi(app, 4.0f));

    D2D1_RECT_F panel = D2D1::RectF(mx, my, mx + w, my + totalH);
    // Soft shadow, then the acrylic-ish panel
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.30f));
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(panel.left + dpi(app, 2.0f),
                                      panel.top + dpi(app, 4.0f),
                                      panel.right + dpi(app, 2.0f),
                                      panel.bottom + dpi(app, 4.0f)),
                          dpi(app, 8.0f), dpi(app, 8.0f)),
        app.brush);
    app.brush->SetColor(menuBg);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(panel, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush);
    app.brush->SetColor(railA(text, 0.12f));
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(panel, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush,
        1.0f);

    float cy = my + padV;
    float subAnchorY = my;
    for (const ECtxItem& it : kECtx) {
        if (it.id == 0) {
            IDWriteTextLayout* hl = railLayout(app, tr(app, it.label), 10.0f,
                                               DWRITE_FONT_WEIGHT_SEMI_BOLD);
            if (hl) {
                DWRITE_TEXT_METRICS m{};
                hl->GetMetrics(&m);
                railDraw(app, hl, mx + dpi(app, 14.0f),
                         cy + hdH - m.height - dpi(app, 2.0f),
                         railA(text, 0.45f));
                hl->Release();
            }
            cy += hdH;
            continue;
        }
        bool disabled = it.needsSel && !app.editorHasSelection;
        bool hov = app.editCtxHover == it.id && !disabled;
        bool subOpen = (it.id == 110 && app.editCtxSub == 1) ||
                       (it.id == 111 && app.editCtxSub == 2);
        D2D1_RECT_F r = D2D1::RectF(mx + dpi(app, 4.0f), cy,
                                    mx + w - dpi(app, 4.0f), cy + rowH);
        if (hov || subOpen) {
            app.brush->SetColor(subOpen ? railA(accent, 0.16f)
                                        : railA(text, 0.08f));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(r, dpi(app, 5.0f), dpi(app, 5.0f)),
                app.brush);
        }
        float alpha = disabled ? 0.38f : 1.0f;
        if (it.icon[0]) {
            railGlyph(app, it.icon, it.id == 113 ? 9.5f : 12.0f,
                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                      it.id == 113 ? L"Consolas" : L"Segoe UI",
                      D2D1::RectF(r.left + dpi(app, 6.0f), r.top,
                                  r.left + dpi(app, 28.0f), r.bottom),
                      railA(accent, alpha));
        }
        IDWriteTextLayout* ll = railLayout(app, tr(app, it.label), 13.0f,
                                           DWRITE_FONT_WEIGHT_NORMAL);
        if (ll) {
            DWRITE_TEXT_METRICS m{};
            ll->GetMetrics(&m);
            railDraw(app, ll, r.left + dpi(app, 34.0f),
                     cy + (rowH - m.height) * 0.5f,
                     subOpen ? railA(accent, 0.95f)
                             : railA(text, 0.92f * alpha));
            ll->Release();
        }
        if (it.hint[0]) {
            IDWriteTextLayout* hl = railLayout(app, it.hint, 11.0f,
                                               DWRITE_FONT_WEIGHT_NORMAL,
                                               L"Consolas");
            if (hl) {
                DWRITE_TEXT_METRICS m{};
                hl->GetMetrics(&m);
                railDraw(app, hl, r.right - dpi(app, 10.0f) - m.width,
                         cy + (rowH - m.height) * 0.5f,
                         railA(text, 0.45f * alpha));
                hl->Release();
            }
        }
        if (!disabled) app.editCtxHits.push_back({r, it.id});
        if (it.id == 110 && app.editCtxSub == 1) subAnchorY = cy;
        if (it.id == 111 && app.editCtxSub == 2) subAnchorY = cy;
        cy += rowH;
        if (it.sepAfter) {
            app.brush->SetColor(railA(text, 0.10f));
            app.renderTarget->FillRectangle(
                D2D1::RectF(mx + dpi(app, 12.0f), cy + sepH * 0.5f,
                            mx + w - dpi(app, 12.0f),
                            cy + sepH * 0.5f + 1.0f),
                app.brush);
            cy += sepH;
        }
    }

    // Submenus, anchored beside their parent row
    if (app.editCtxSub == 2) {
        float sw = dpi(app, 240.0f);
        float srowH = dpi(app, 34.0f);
        float sh = dpi(app, 30.0f) + 7 * (srowH + dpi(app, 2.0f)) +
                   dpi(app, 8.0f);
        float sx = std::min(mx + w + dpi(app, 4.0f),
                            (float)app.width - sw - dpi(app, 8.0f));
        float sy = std::min(subAnchorY,
                            (float)app.height - sh - dpi(app, 8.0f));
        D2D1_RECT_F sp = D2D1::RectF(sx, sy, sx + sw, sy + sh);
        app.brush->SetColor(menuBg);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush);
        app.brush->SetColor(railA(text, 0.12f));
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush, 1.0f);
        IDWriteTextLayout* hl = railLayout(app, tr(app, "ectx.templates"),
                                           10.5f,
                                           DWRITE_FONT_WEIGHT_SEMI_BOLD);
        if (hl) {
            railDraw(app, hl, sx + dpi(app, 14.0f), sy + dpi(app, 8.0f),
                     railA(text, 0.5f));
            hl->Release();
        }
        float ry = sy + dpi(app, 30.0f);
        for (int i = 0; i < 7; i++) {
            D2D1_RECT_F r = D2D1::RectF(sx + dpi(app, 4.0f), ry,
                                        sx + sw - dpi(app, 4.0f),
                                        ry + srowH);
            bool hov = app.editCtxSubHover == 130 + i;
            if (hov) {
                app.brush->SetColor(railA(text, 0.08f));
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(r, dpi(app, 5.0f), dpi(app, 5.0f)),
                    app.brush);
            }
            railGlyph(app, kEDiag[i].icon, 12.0f,
                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                      L"Segoe UI",
                      D2D1::RectF(r.left + dpi(app, 6.0f), r.top,
                                  r.left + dpi(app, 28.0f), r.bottom),
                      accent);
            IDWriteTextLayout* ll = railLayout(app, tr(app, kEDiag[i].label),
                                               13.0f,
                                               DWRITE_FONT_WEIGHT_NORMAL);
            if (ll) {
                DWRITE_TEXT_METRICS m{};
                ll->GetMetrics(&m);
                railDraw(app, ll, r.left + dpi(app, 34.0f),
                         ry + (srowH - m.height) * 0.5f,
                         railA(text, 0.92f));
                ll->Release();
            }
            IDWriteTextLayout* hint = railLayout(app, kEDiag[i].hint, 10.5f,
                                                 DWRITE_FONT_WEIGHT_NORMAL,
                                                 L"Consolas");
            if (hint) {
                DWRITE_TEXT_METRICS m{};
                hint->GetMetrics(&m);
                railDraw(app, hint, r.right - dpi(app, 10.0f) - m.width,
                         ry + (srowH - m.height) * 0.5f, railA(text, 0.4f));
                hint->Release();
            }
            app.editCtxSubHits.push_back({r, 130 + i});
            ry += srowH + dpi(app, 2.0f);
        }
    } else if (app.editCtxSub == 1) {
        float cell = dpi(app, 20.0f);
        float gap = dpi(app, 3.0f);
        float pad = dpi(app, 12.0f);
        float sw = pad * 2.0f + 8.0f * cell + 7.0f * gap;
        float sh = pad * 2.0f + 6.0f * cell + 5.0f * gap + dpi(app, 24.0f);
        float sx = std::min(mx + w + dpi(app, 4.0f),
                            (float)app.width - sw - dpi(app, 8.0f));
        float sy = std::min(subAnchorY,
                            (float)app.height - sh - dpi(app, 8.0f));
        D2D1_RECT_F sp = D2D1::RectF(sx, sy, sx + sw, sy + sh);
        app.brush->SetColor(menuBg);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush);
        app.brush->SetColor(railA(text, 0.12f));
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(sp, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush, 1.0f);
        for (int r = 0; r < 6; r++) {
            for (int c = 0; c < 8; c++) {
                bool in = r < app.editCtxGridR && c < app.editCtxGridC;
                D2D1_RECT_F cr = D2D1::RectF(
                    sx + pad + c * (cell + gap), sy + pad + r * (cell + gap),
                    sx + pad + c * (cell + gap) + cell,
                    sy + pad + r * (cell + gap) + cell);
                app.brush->SetColor(in ? railA(accent, 0.35f)
                                       : railA(text, 0.05f));
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(cr, 2.0f, 2.0f), app.brush);
                app.brush->SetColor(in ? railA(accent, 0.6f)
                                       : railA(text, 0.12f));
                app.renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(cr, 2.0f, 2.0f), app.brush, 1.0f);
            }
        }
        wchar_t sizeLabel[48];
        swprintf_s(sizeLabel, _countof(sizeLabel), L"%d \u00D7 %d %ls",
                   app.editCtxGridC, app.editCtxGridR,
                   tr(app, "ectx.table_word"));
        IDWriteTextLayout* sl = railLayout(app, sizeLabel, 12.0f,
                                           DWRITE_FONT_WEIGHT_NORMAL);
        if (sl) {
            DWRITE_TEXT_METRICS m{};
            sl->GetMetrics(&m);
            railDraw(app, sl, sx + (sw - m.width) * 0.5f,
                     sy + sh - dpi(app, 22.0f), railA(text, 0.9f));
            sl->Release();
        }
        // The whole grid area is one hit; cell resolution happens on move
        app.editCtxSubHits.push_back(
            {D2D1::RectF(sx, sy, sx + sw, sy + sh), 140});
    }
}

bool editCtxMouseDown(App& app, HWND hwnd, int x, int y) {
    if (!app.editCtxOpen) return false;
    float fx = (float)x, fy = (float)y;
    for (const auto& hit : app.editCtxSubHits) {
        if (fx < hit.first.left || fx > hit.first.right ||
            fy < hit.first.top || fy > hit.first.bottom) {
            continue;
        }
        if (hit.second == 140) {
            editorInsertTableGrid(app, hwnd, app.editCtxGridC,
                                  app.editCtxGridR);
        } else if (hit.second >= 130) {
            editorInsertDiagramTemplate(app, hwnd, hit.second - 130);
        }
        closeEditCtxMenu(app);
        return true;
    }
    for (const auto& hit : app.editCtxHits) {
        if (fx < hit.first.left || fx > hit.first.right ||
            fy < hit.first.top || fy > hit.first.bottom) {
            continue;
        }
        switch (hit.second) {
            case 100: editorClipboardCut(app, hwnd); break;
            case 101: editorClipboardCopy(app, hwnd); break;
            case 102: editorClipboardPaste(app, hwnd); break;
            case 110: app.editCtxSub = 1; InvalidateRect(hwnd, nullptr, FALSE); return true;
            case 111: app.editCtxSub = 2; InvalidateRect(hwnd, nullptr, FALSE); return true;
            case 112: editRailInvoke(app, hwnd, 22); break;
            case 113:
                editorInsertSnippetPublic(app, hwnd, L"```\n\n```\n", 4);
                break;
            case 114: editRailInvoke(app, hwnd, 5); break;
            case 115:
                editorInsertSnippetPublic(app, hwnd, L"---\n", 4);
                break;
        }
        closeEditCtxMenu(app);
        return true;
    }
    closeEditCtxMenu(app);
    return true;  // the closing click is consumed
}

bool editCtxMouseMove(App& app, int x, int y) {
    if (!app.editCtxOpen) return false;
    float fx = (float)x, fy = (float)y;
    int hover = 0, subHover = 0;
    for (const auto& hit : app.editCtxHits) {
        if (fx >= hit.first.left && fx <= hit.first.right &&
            fy >= hit.first.top && fy <= hit.first.bottom) {
            hover = hit.second;
            break;
        }
    }
    for (const auto& hit : app.editCtxSubHits) {
        if (fx >= hit.first.left && fx <= hit.first.right &&
            fy >= hit.first.top && fy <= hit.first.bottom) {
            subHover = hit.second;
            if (hit.second == 140) {
                // Resolve the hovered grid size
                float cell = dpi(app, 20.0f);
                float gap = dpi(app, 3.0f);
                float pad = dpi(app, 12.0f);
                int c = (int)((fx - hit.first.left - pad) / (cell + gap)) + 1;
                int r = (int)((fy - hit.first.top - pad) / (cell + gap)) + 1;
                app.editCtxGridC = std::max(1, std::min(c, 8));
                app.editCtxGridR = std::max(1, std::min(r, 6));
            }
            break;
        }
    }
    // Hovering a parent row opens its submenu; hovering another row
    // closes it
    if (hover == 110) app.editCtxSub = 1;
    else if (hover == 111) app.editCtxSub = 2;
    else if (hover != 0 && subHover == 0) app.editCtxSub = 0;
    if (hover != app.editCtxHover || subHover != app.editCtxSubHover) {
        app.editCtxHover = hover;
        app.editCtxSubHover = subHover;
        InvalidateRect(app.hwnd, nullptr, FALSE);
    } else if (subHover == 140) {
        InvalidateRect(app.hwnd, nullptr, FALSE);  // live grid highlight
    }
    return true;
}

// --- Floating sheet (design 10a) -----------------------------------------
//
// The render is a sheet of paper lying on the editor's desk: both panes
// share the desk surface, the sheet floats inset from the chrome with a
// soft shadow thrown below it, and the caret's block keeps its wash on
// the page. The desk gap left of the sheet is the split drag handle.

// Index of the scroll anchor (top-level block) containing the caret,
// or -1 when the tables aren't ready
static int seamCaretAnchor(const App& app) {
    if (app.scrollAnchors.empty() || app.editorLineByteOffsets.empty() ||
        app.editorLineStarts.empty()) {
        return -1;
    }
    // Caret position -> line (wchar indices), then the byte offset of the
    // line's END: anchors point at a block's first text node, which sits
    // past the "## " marker, so keying on the line start would resolve a
    // heading's caret to the previous block
    size_t lo = 0, hi = app.editorLineStarts.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (app.editorLineStarts[mid] <= app.editorCursorPos) lo = mid;
        else hi = mid;
    }
    size_t caretLine = std::min(lo, app.editorLineByteOffsets.size() - 1);
    size_t caretByte =
        caretLine + 1 < app.editorLineByteOffsets.size()
            ? app.editorLineByteOffsets[caretLine + 1] - 1
            : SIZE_MAX;
    // Greatest anchor at or before the caret's line start
    size_t alo = 0, ahi = app.scrollAnchors.size();
    while (alo + 1 < ahi) {
        size_t mid = (alo + ahi) / 2;
        if (app.scrollAnchors[mid].sourceOffset <= caretByte) alo = mid;
        else ahi = mid;
    }
    return (int)alo;
}

// Desk fill, sheet shadow, sheet surface and edge — drawn before the
// document content clips into the sheet
void renderEditSheetChrome(App& app) {
    if (!editorPreviewVisible(app) || !app.renderTarget || !app.brush) {
        return;
    }
    float W = (float)app.width;
    float H = (float)app.height;
    D2D1_RECT_F sheet = editSheetRect(app);
    float radius = dpi(app, 10.0f);

    // Desk right of the source column (the editor fills its own side)
    app.brush->SetColor(editDeskColor(app));
    app.renderTarget->FillRectangle(
        D2D1::RectF(editorPaneWidth(app), 0, W, H), app.brush);

    // Shadow: stacked translucent fills growing outward and sliding down,
    // so the sheet throws its shade below itself
    float grow = dpi(app, 2.2f);
    float drop = dpi(app, 1.1f);
    float ringAlpha = app.theme.isDark ? 0.07f : 0.035f;
    for (int i = 8; i >= 1; i--) {
        D2D1_RECT_F halo = D2D1::RectF(
            sheet.left - grow * i, sheet.top - grow * i * 0.3f + drop * i,
            sheet.right + grow * i, sheet.bottom + grow * i + drop * i);
        app.brush->SetColor(D2D1::ColorF(0, 0, 0, ringAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(halo, radius + grow * i, radius + grow * i),
            app.brush);
    }

    // The sheet itself, closed by a hairline edge
    app.brush->SetColor(editSheetColor(app));
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(sheet, radius, radius), app.brush);
    app.brush->SetColor(railA(app.theme.text, 0.08f));
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(sheet, radius, radius), app.brush, 1.0f);
}

// Soft accent wash behind the caret block's rendered output; drawn inside
// the preview's transform, before the document content
void renderPreviewCaretBlock(App& app, float previewWidth) {
    if (!editorPreviewVisible(app) || !app.renderTarget || !app.brush) {
        return;
    }
    int k = seamCaretAnchor(app);
    if (k < 0) return;
    float blockTop = app.scrollAnchors[k].renderedY - app.scrollY -
                     dpi(app, 4.0f);
    float nextY = k + 1 < (int)app.scrollAnchors.size()
                      ? app.scrollAnchors[k + 1].renderedY
                      : app.contentHeight;
    float blockBottom = nextY - app.scrollY - dpi(app, 8.0f);
    if (blockBottom <= blockTop + dpi(app, 4.0f)) return;
    if (blockBottom < editSheetRect(app).top || blockTop > app.height) return;
    D2D1_COLOR_F c = app.theme.accent;
    c.a = 0.08f;
    app.brush->SetColor(c);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(dpi(app, 10.0f), blockTop,
                                      previewWidth - dpi(app, 10.0f),
                                      blockBottom),
                          dpi(app, 5.0f), dpi(app, 5.0f)),
        app.brush);
}
