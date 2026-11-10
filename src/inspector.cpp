// The CSS inspector (inspector.h): what styles the element under the
// pointer, drawn as a card over the page.

#pragma optimize("s", on)

#include "inspector.h"

#include "editor.h"
#include "i18n.h"
#include "page_style.h"
#include "render.h"
#include "signals.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <cfloat>
#include <optional>

struct InspectorCard {
    struct Row {
        std::wstring text, where;  // "color: #b35900"; "blockquote > p   Ink.css:14"
        std::string file;
        int line = 0;
        css::Applied::State state = css::Applied::Winning;
        bool heading = false;  // the line that heads what is inherited
        std::optional<D2D1_COLOR_F> swatch;
        D2D1_RECT_F hit{};
    };
    bool open = false;
    float x = 0, y = 0;  // where it was asked for, in the document view's coordinates
    std::wstring path;
    std::vector<Row> rows;
    D2D1_RECT_F bounds{};
};

namespace {

InspectorCard& card(App& app) {
    if (!app.inspector) app.inspector = std::make_shared<InspectorCard>();
    return *app.inspector;
}

bool inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; }

std::wstring baseName(const std::string& file) {
    const size_t slash = file.find_last_of("/\\");
    return toWide(slash == std::string::npos ? file : file.substr(slash + 1));
}

// A colour property's value as a swatch: the colour it was written as, or
// for the one that wins, the colour it computed to (a var() or currentColor)
std::optional<D2D1_COLOR_F> swatchOf(const css::Applied& a, const css::ComputedStyle* computed, bool dark) {
    const std::string& p = a.property;
    const bool colour = p.find("color") != std::string::npos || p == "background" || p.rfind("--", 0) == 0;
    if (!colour) return std::nullopt;
    if (auto c = css::parseColor(a.value, dark)) return D2D1::ColorF(c->r, c->g, c->b, c->a);
    if (a.state != css::Applied::Winning || a.ancestor != 0 || !computed) return std::nullopt;
    const css::Color* c = nullptr;
    if (p == "color") c = &computed->color;
    else if (p == "background-color") c = &computed->backgroundColor;
    else if (p == "border-top-color") c = &computed->border[0].color;
    else if (p == "border-right-color") c = &computed->border[1].color;
    else if (p == "border-bottom-color") c = &computed->border[2].color;
    else if (p == "border-left-color") c = &computed->border[3].color;
    if (!c) return std::nullopt;
    return D2D1::ColorF(c->r, c->g, c->b, c->a);
}

}  // namespace

bool inspectorShown(const App& app) { return app.inspector && app.inspector->open; }

std::wstring inspectorText(const App& app) {
    if (!inspectorShown(app)) return {};
    std::wstring text = app.inspector->path + L"\n";
    for (const auto& row : app.inspector->rows) {
        if (row.heading) text += row.text + L"\n";
        else text += (row.state == css::Applied::Overridden ? L"~" : L"") + row.text + L"   " + row.where + L"\n";
    }
    return text;
}

void inspectorClose(App& app) {
    if (app.inspector) app.inspector->open = false;
}

bool inspectorOpen(App& app, float x, float y) {
    inspectorClose(app);
    if (!app.root) return false;
    // A layout that records what it draws where, then looked up
    page::record(app, true);
    layoutDocument(app);
    page::record(app, false);
    if (!page::active(app)) {
        signalPush(app, SIG_INFO, SIGI_INFO, tr(app, "toast.inspector_css"), L"", L"");
        return false;
    }
    const float dx = x - documentViewportX(app) + app.scrollX, dy = y + app.scrollY;
    const auto& boxes = page::recorded(app);
    const page::Inspected* hit = nullptr;
    for (auto it = boxes.rbegin(); it != boxes.rend() && !hit; ++it) {
        if (!it->block && inside(it->bounds, dx, dy)) hit = &*it;
    }
    float best = FLT_MAX;
    for (const auto& box : boxes) {
        if (hit && !hit->block) break;
        if (!box.block || !inside(box.bounds, dx, dy)) continue;
        const float area = (box.bounds.right - box.bounds.left) * (box.bounds.bottom - box.bounds.top);
        if (area < best) {
            best = area;
            hit = &box;
        }
    }
    if (!hit) return false;

    InspectorCard& c = card(app);
    c.rows.clear();
    c.path = toWide(css::selectorPath(hit->path));
    const css::ComputedStyle* computed = page::computedStyle(app, hit->context);
    bool inherited = false;
    for (const auto& a : page::explain(app, hit->context)) {
        if (a.state == css::Applied::Inherited && !inherited) {
            InspectorCard::Row heading;
            heading.heading = true;
            heading.text = tr(app, "inspector.inherited");
            c.rows.push_back(heading);
            inherited = true;
        }
        InspectorCard::Row row;
        row.text = toWide(a.property + ": " + a.value + (a.important ? " !important" : ""));
        if (a.origin == css::Origin::Base) {
            row.where = toWide(a.selector) + L"   Tinta";
        } else {
            // A built-in theme's sheet is no file to open: its name stands for it
            const bool builtin = a.file.empty() && app.currentThemeIndex >= 0 && app.currentThemeIndex < THEME_COUNT;
            row.where = toWide(a.selector) + (builtin          ? L"   " + std::wstring(THEMES[app.currentThemeIndex].name)
                                              : a.file.empty() ? std::wstring()
                                                               : L"   " + baseName(a.file) + L":" + std::to_wstring(a.line));
            row.file = a.file;
            row.line = a.line;
        }
        row.state = a.state;
        row.swatch = swatchOf(a, computed, app.theme.isDark);
        c.rows.push_back(std::move(row));
    }
    if (c.rows.empty()) {
        InspectorCard::Row none;
        none.heading = true;
        none.text = tr(app, "inspector.none");
        c.rows.push_back(none);
    }
    c.x = x - documentViewportX(app);
    c.y = y;
    c.open = true;
    return true;
}

bool inspectorMouseDown(App& app, HWND hwnd, float x, float y) {
    if (!inspectorShown(app)) return false;
    InspectorCard& c = card(app);
    const float vx = x - documentViewportX(app);
    for (const auto& row : c.rows) {
        if (row.file.empty() || !inside(row.hit, vx, y)) continue;
        // The rule's own line in its sheet, in the editor
        const std::string file = row.file;
        const int line = row.line;
        inspectorClose(app);
        tabOpenPath(app, hwnd, file);
        if (!app.editMode) enterEditMode(app);
        if (line > 0) gotoLineJump(app, (size_t)line);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (!inside(c.bounds, vx, y)) inspectorClose(app);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool inspectorKeyDown(App& app, HWND hwnd, WPARAM key) {
    if (!inspectorShown(app) || key != VK_ESCAPE) return false;
    inspectorClose(app);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void renderInspector(App& app) {
    if (!inspectorShown(app) || !app.folderBrowserFormat || !app.statsFormat) return;
    InspectorCard& c = card(app);
    const float s = app.contentScale;
    const float viewW = documentViewportWidth(app);
    const float w = std::min(560 * s, viewW - 24 * s);
    const float pad = 12 * s, line = 18 * s, gap = 6 * s;
    auto layoutOf = [&](const std::wstring& text, IDWriteTextFormat* format, float width) {
        IDWriteTextLayout* layout = nullptr;
        app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), format, std::max(10.0f, width), 1000, &layout);
        if (layout) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            IDWriteInlineObject* ellipsis = nullptr;
            app.dwriteFactory->CreateEllipsisTrimmingSign(format, &ellipsis);
            layout->SetTrimming(&trim, ellipsis);
            if (ellipsis) ellipsis->Release();
        }
        return layout;
    };
    // As many rules as the window has room for, then how many more
    const float room = (float)app.height - chromeTopHeight(app) - 40 * s;
    const float rowH = line * 2 + gap;
    const int fit = std::max(1, (int)((room - pad * 3 - line * 3) / rowH));
    const int shown = std::min((int)c.rows.size(), fit);
    const float h = pad * 2 + line * 1.5f + shown * rowH + (shown < (int)c.rows.size() ? line : 0) + line;
    const float x = std::clamp(c.x + 8 * s, 12 * s, std::max(12 * s, viewW - w - 12 * s));
    const float y = std::clamp(c.y + 8 * s, chromeTopHeight(app) + 8 * s, std::max(chromeTopHeight(app) + 8 * s, (float)app.height - h - 12 * s));
    c.bounds = D2D1::RectF(x, y, x + w, y + h);

    D2D1_COLOR_F panel = app.theme.codeBackground;
    panel.a = 1;
    app.brush->SetColor(panel);
    app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(c.bounds, 8 * s, 8 * s), app.brush);
    D2D1_COLOR_F edge = app.theme.text;  // the card's own edge, not a theme's quote bar
    edge.a = 0.18f;
    app.brush->SetColor(edge);
    app.renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(c.bounds, 8 * s, 8 * s), app.brush, 1.0f);

    float cy = y + pad;
    if (IDWriteTextLayout* title = layoutOf(c.path, app.folderBrowserFormat, w - pad * 2)) {
        DWRITE_TEXT_RANGE all{0, (UINT32)c.path.size()};
        title->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, all);
        app.brush->SetColor(app.theme.accent);
        app.renderTarget->DrawTextLayout(D2D1::Point2F(x + pad, cy), title, app.brush);
        title->Release();
    }
    cy += line * 1.5f;
    D2D1_COLOR_F dim = app.theme.text;
    dim.a = 0.6f;
    for (int i = 0; i < shown; ++i) {
        auto& row = c.rows[(size_t)i];
        row.hit = D2D1::RectF(x, cy, x + w, cy + rowH);
        float tx = x + pad;
        if (row.heading) {
            if (IDWriteTextLayout* t = layoutOf(row.text, app.folderBrowserFormat, w - pad * 2)) {
                app.brush->SetColor(dim);
                app.renderTarget->DrawTextLayout(D2D1::Point2F(tx, cy + line * 0.5f), t, app.brush);
                t->Release();
            }
            row.hit = {};
            cy += rowH;
            continue;
        }
        const bool beaten = row.state == css::Applied::Overridden;
        if (row.swatch) {
            const D2D1_RECT_F chip = D2D1::RectF(tx, cy + 3 * s, tx + 12 * s, cy + 15 * s);
            app.brush->SetColor(*row.swatch);
            app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(chip, 2 * s, 2 * s), app.brush);
            app.brush->SetColor(edge);
            app.renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(chip, 2 * s, 2 * s), app.brush, 1.0f);
            tx += 18 * s;
        }
        D2D1_COLOR_F ink = app.theme.text;
        if (beaten) ink.a = 0.45f;
        if (IDWriteTextLayout* t = layoutOf(row.text, app.statsFormat, x + w - pad - tx)) {
            if (beaten) t->SetStrikethrough(TRUE, {0, (UINT32)row.text.size()});
            app.brush->SetColor(ink);
            app.renderTarget->DrawTextLayout(D2D1::Point2F(tx, cy), t, app.brush);
            t->Release();
        }
        if (IDWriteTextLayout* t = layoutOf(row.where, app.folderBrowserFormat, w - pad * 2)) {
            D2D1_COLOR_F where = row.file.empty() ? dim : app.theme.link;
            if (beaten) where.a *= 0.6f;
            app.brush->SetColor(where);
            app.renderTarget->DrawTextLayout(D2D1::Point2F(x + pad, cy + line), t, app.brush);
            t->Release();
        }
        cy += rowH;
    }
    for (size_t i = (size_t)shown; i < c.rows.size(); ++i) c.rows[i].hit = {};
    if (shown < (int)c.rows.size()) {
        wchar_t more[64];
        swprintf_s(more, _countof(more), tr(app, "inspector.more"), (int)c.rows.size() - shown);
        if (IDWriteTextLayout* t = layoutOf(more, app.folderBrowserFormat, w - pad * 2)) {
            app.brush->SetColor(dim);
            app.renderTarget->DrawTextLayout(D2D1::Point2F(x + pad, cy), t, app.brush);
            t->Release();
        }
        cy += line;
    }
    if (IDWriteTextLayout* t = layoutOf(tr(app, "inspector.hint"), app.folderBrowserFormat, w - pad * 2)) {
        app.brush->SetColor(dim);
        app.renderTarget->DrawTextLayout(D2D1::Point2F(x + pad, cy + 2 * s), t, app.brush);
        t->Release();
    }
}
