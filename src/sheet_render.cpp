// Sheets: drawing the canvas (sheet_ui.h). Everything is Direct2D on the
// window's target; a note card's content is the note itself, laid out by
// the document layout at the card's width into a bitmap that is kept until
// the note, the card's width, the theme or the zoom bucket changes.

// Sheets favour small code over fast: they are drawn by Direct2D and
// laid out by the document layout, which keep their own optimisation
#pragma optimize("s", on)

#include "sheet_ui.h"
#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "i18n.h"
#include "image_loader.h"
#include "print.h"
#include "render.h"
#include "ui_paint.h"
#include "utils.h"

#include <d2d1_1.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace sheet;
using namespace sheetui;
using Microsoft::WRL::ComPtr;
using namespace paint;

// --- Geometry ------------------------------------------------------------

namespace sheetui {

float scale(const App& app, const SheetDoc& doc) { return doc.zoom * app.contentScale; }

D2D1_RECT_F viewRect(const App& app) {
    return D2D1::RectF(documentViewportX(app), chromeTopHeight(app), (float)app.width, (float)app.height);
}

D2D1_POINT_2F toScreen(const App& app, const SheetDoc& doc, float x, float y) {
    D2D1_RECT_F v = viewRect(app);
    float s = scale(app, doc);
    return D2D1::Point2F((v.left + v.right) * 0.5f + (x - doc.camX) * s, (v.top + v.bottom) * 0.5f + (y - doc.camY) * s);
}

D2D1_POINT_2F toSheet(const App& app, const SheetDoc& doc, float x, float y) {
    D2D1_RECT_F v = viewRect(app);
    float s = scale(app, doc);
    return D2D1::Point2F(doc.camX + (x - (v.left + v.right) * 0.5f) / s, doc.camY + (y - (v.top + v.bottom) * 0.5f) / s);
}

D2D1_RECT_F screenRect(const App& app, const SheetDoc& doc, const Box& box) {
    D2D1_POINT_2F a = toScreen(app, doc, box.left, box.top);
    D2D1_POINT_2F b = toScreen(app, doc, box.right, box.bottom);
    return D2D1::RectF(a.x, a.y, b.x, b.y);
}

static float easeInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
}

Box drawBox(const SheetDoc& doc, const Node& node) {
    Box target = boxOf(node);
    if (!doc.glide.active) return target;
    auto it = doc.glide.from.find(node.id);
    if (it == doc.glide.from.end()) return target;
    float t = easeInOut((float)(nowMs() - doc.glide.startMs) / 300.0f);
    const Box& f = it->second;
    return {f.left + (target.left - f.left) * t, f.top + (target.top - f.top) * t,
            f.right + (target.right - f.right) * t, f.bottom + (target.bottom - f.bottom) * t};
}

float cardPadding() { return 18.0f; }

D2D1_POINT_2F sidePoint(const Box& b, Side side) {
    switch (side) {
        case Side::Top: return D2D1::Point2F((b.left + b.right) * 0.5f, b.top);
        case Side::Bottom: return D2D1::Point2F((b.left + b.right) * 0.5f, b.bottom);
        case Side::Left: return D2D1::Point2F(b.left, (b.top + b.bottom) * 0.5f);
        default: return D2D1::Point2F(b.right, (b.top + b.bottom) * 0.5f);
    }
}

static D2D1_POINT_2F sideNormal(Side side) {
    switch (side) {
        case Side::Top: return D2D1::Point2F(0, -1);
        case Side::Bottom: return D2D1::Point2F(0, 1);
        case Side::Left: return D2D1::Point2F(-1, 0);
        default: return D2D1::Point2F(1, 0);
    }
}

Curve edgeCurve(const Box& from, const Box& to, Side fromSide, Side toSide) {
    Curve c;
    c.p0 = sidePoint(from, fromSide);
    c.p3 = sidePoint(to, toSide);
    c.n0 = sideNormal(fromSide);
    c.n3 = sideNormal(toSide);
    float dx = c.p3.x - c.p0.x, dy = c.p3.y - c.p0.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    float reach = std::clamp(dist * 0.45f, 40.0f, 260.0f);
    c.p1 = D2D1::Point2F(c.p0.x + c.n0.x * reach, c.p0.y + c.n0.y * reach);
    c.p2 = D2D1::Point2F(c.p3.x + c.n3.x * reach, c.p3.y + c.n3.y * reach);
    return c;
}

Curve edgeCurveFor(const SheetDoc& doc, const Edge& edge) {
    int a = nodeIndex(doc.model, edge.fromNode), b = nodeIndex(doc.model, edge.toNode);
    if (a < 0 || b < 0) return {};
    const Node& from = doc.model.nodes[a];
    const Node& to = doc.model.nodes[b];
    Side fs, ts;
    edgeSides(from, to, edge.fromSide, edge.toSide, fs, ts);
    return edgeCurve(drawBox(doc, from), drawBox(doc, to), fs, ts);
}

D2D1_POINT_2F bezier(const Curve& c, float t) {
    float u = 1 - t;
    float a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    return D2D1::Point2F(a * c.p0.x + b * c.p1.x + d * c.p2.x + e * c.p3.x,
                         a * c.p0.y + b * c.p1.y + d * c.p2.y + e * c.p3.y);
}

float curveDistance(const Curve& c, float x, float y, float* at) {
    float best = 1e30f;
    D2D1_POINT_2F prev = c.p0;
    const int steps = 32;
    for (int i = 1; i <= steps; ++i) {
        float t = (float)i / steps;
        D2D1_POINT_2F p = bezier(c, t);
        float vx = p.x - prev.x, vy = p.y - prev.y;
        float len2 = vx * vx + vy * vy;
        float u = len2 > 0 ? std::clamp(((x - prev.x) * vx + (y - prev.y) * vy) / len2, 0.0f, 1.0f) : 0;
        float px = prev.x + vx * u - x, py = prev.y + vy * u - y;
        float d = std::sqrt(px * px + py * py);
        if (d < best) {
            best = d;
            if (at) *at = ((float)(i - 1) + u) / steps;
        }
        prev = p;
    }
    return best;
}

int nodeAt(const SheetDoc& doc, float x, float y, bool groupsToo) {
    // Selected cards draw on top, then later ones: test in that order
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = (int)doc.model.nodes.size() - 1; i >= 0; --i) {
            const Node& n = doc.model.nodes[i];
            if (n.type == NodeType::Group || foldedAwayAt(doc, i)) continue;
            if ((pass == 0) != isSelected(doc, n.id)) continue;
            Box b = boxOf(n);
            if (x >= b.left && x <= b.right && y >= b.top && y <= b.bottom) return i;
        }
    }
    if (!groupsToo) return -1;
    int best = -1;
    float bestArea = 0;
    for (int i = 0; i < (int)doc.model.nodes.size(); ++i) {
        const Node& n = doc.model.nodes[i];
        if (n.type != NodeType::Group) continue;
        Box b = boxOf(n);
        if (x < b.left || x > b.right || y < b.top || y > b.bottom) continue;
        float area = n.width * n.height;
        if (best < 0 || area < bestArea) {
            best = i;
            bestArea = area;
        }
    }
    return best;
}

}  // namespace sheetui

// --- Colours and text ------------------------------------------------------

namespace {

struct Palette {
    D2D1_COLOR_F canvas, dots, card, cardBorder, text, muted, accent, surface, surfaceBorder, shadow, heading;
};

Palette palette(const App& app) {
    const D2DTheme& th = app.theme;
    const D2D1_COLOR_F white = D2D1::ColorF(1, 1, 1, 1), black = D2D1::ColorF(0, 0, 0, 1);
    Palette p;
    p.text = th.text;
    p.heading = th.heading;
    p.accent = th.accent;
    p.canvas = th.isDark ? mix(th.background, black, 0.25f) : mix(th.background, th.text, 0.05f);
    p.canvas.a = 1;
    p.dots = alpha(th.text, th.isDark ? 0.20f : 0.24f);
    p.card = th.isDark ? mix(th.background, th.text, 0.04f) : mix(th.background, white, 0.75f);
    p.card.a = 1;
    p.cardBorder = alpha(th.text, th.isDark ? 0.16f : 0.17f);
    p.muted = alpha(th.text, 0.56f);
    paint::Surface s = paint::surface(app);
    p.surface = s.fill;
    p.surfaceBorder = s.border;
    p.shadow = s.shadow;
    return p;
}

D2D1_COLOR_F cardFill(const App& app, const Node& node, const Palette& p) {
    bool has = false;
    D2D1_COLOR_F c = presetColor(app, node.color, &has);
    if (!has) return p.card;
    D2D1_COLOR_F out = mix(p.card, c, app.theme.isDark ? 0.13f : 0.09f);
    out.a = 1;
    return out;
}

const wchar_t* docFace(const App& app) {
    return app.theme.fontFamily && *app.theme.fontFamily ? app.theme.fontFamily : L"Segoe UI";
}

// A layout kept on its card; the caller does not release it
IDWriteTextLayout* cachedLayout(App& app, SheetCard::TextCache& cache, const std::wstring& text, IDWriteTextFormat* fmt,
                                float maxW, float maxH) {
    maxW = std::max(1.0f, std::round(maxW));
    maxH = std::max(1.0f, std::round(maxH));
    if (cache.layout && cache.format == fmt && cache.width == maxW && cache.height == maxH && cache.text == text) {
        return cache.layout.Get();
    }
    cache.layout.Reset();
    cache.layout.Attach(makeLayout(app, text, fmt, maxW, maxH));
    cache.text = text;
    cache.format = fmt;
    cache.width = maxW;
    cache.height = maxH;
    return cache.layout.Get();
}

bool intersects(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

// A soft shadow under a card, in the palette's shadow colour
void shadow(App& app, const D2D1_RECT_F& r, float radius, const Palette& p, float strength) {
    paint::shadow(app, r, radius, p.shadow, strength);
}

}  // namespace

// --- Card content bitmaps ------------------------------------------------

namespace {

// Zoom buckets a note's content is drawn at: the nearest at or above the
// zoom, so cards are never drawn soft, never at more than they need
float bucketFor(float s) {
    static const float buckets[] = {0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f, 1.25f, 1.5f, 1.75f,
                                    2.0f,   2.5f, 3.0f,   3.5f,  4.0f,   5.0f, 6.0f};
    for (float b : buckets) {
        if (b >= s * 0.97f) return b;
    }
    return 6.0f;
}

unsigned long long contentKey(const App& app, const SheetCard& c, const D2D1_COLOR_F& fill, float width) {
    unsigned long long key = c.stamp * 1000003ull;
    key ^= (unsigned long long)app.currentThemeIndex * 7919ull;
    key ^= (unsigned long long)(fill.r * 255) << 8 ^ (unsigned long long)(fill.g * 255) << 16 ^
           (unsigned long long)(fill.b * 255) << 24;
    key ^= (unsigned long long)(width * 4) << 32;
    key ^= std::hash<std::wstring>{}(app.theme.fontFamily ? app.theme.fontFamily : L"");
    return key | 1;  // never 0: 0 means "draw again"
}

// The document layout borrowed to draw notes off screen: unit scale, the
// print layout, no panels, the screen's theme. Everything is put back when
// the session ends, and the page behind the sheet is laid out again if it
// is ever shown.
struct NoteLayoutSession {
    App& app;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1SolidColorBrush> brush;
    qmd::ElementPtr savedRoot;
    std::string savedFile;
    int savedW = 0, savedH = 0, savedPct = 100;
    float savedScale = 1, savedZoom = 1, savedApplied = 1, savedScrollX = 0, savedScrollY = 0;
    bool savedEdit = false, savedToc = false, savedBrowser = false, savedPanel = false;

    explicit NoteLayoutSession(App& a) : app(a) {
        ComPtr<ID2D1Device> device;
        if (app.deviceContext) app.deviceContext->GetDevice(&device);
        if (!device || FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)) || !dc) return;
        dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush);
        if (!brush) return;
        savedRoot = app.root;
        savedFile = app.currentFile;
        savedW = app.width;
        savedH = app.height;
        savedScale = app.contentScale;
        savedZoom = app.zoomFactor;
        savedApplied = app.appliedZoomFactor;
        savedEdit = app.editMode;
        savedToc = app.showToc;
        savedBrowser = app.showFolderBrowser;
        savedPanel = app.showSearchPanel;
        savedScrollX = app.scrollX;
        savedScrollY = app.scrollY;
        savedPct = app.readingWidthPct;
        app.editMode = false;
        app.showToc = app.showFolderBrowser = app.showSearchPanel = false;
        app.printLayout = true;
        app.contentScale = 1.0f;
        app.zoomFactor = 1.0f;
        app.readingWidthPct = 100;
        updateTextFormats(app);
    }

    ~NoteLayoutSession() {
        if (!ready()) return;
        app.root = savedRoot;
        app.currentFile = savedFile;
        app.width = savedW;
        app.height = savedH;
        app.contentScale = savedScale;
        app.zoomFactor = savedZoom;
        app.editMode = savedEdit;
        app.showToc = savedToc;
        app.showFolderBrowser = savedBrowser;
        app.showSearchPanel = savedPanel;
        app.readingWidthPct = savedPct;
        app.printLayout = false;
        app.scrollX = savedScrollX;
        app.scrollY = savedScrollY;
        updateTextFormats(app);
        app.appliedZoomFactor = savedApplied;
        app.layoutDirty = true;
    }

    bool ready() const { return dc && brush; }

    // Lays a note out at `width`: its content height, or 0 when it does not parse
    float layout(const std::string& source, const std::wstring& path, float width) {
        auto parsed = parseDocument(app.parser, source, path);
        if (!parsed.success || !parsed.root) return 0;
        app.root = parsed.root;
        app.currentFile = toUtf8(path);
        layoutForWidth(app, width);
        return std::max(1.0f, app.contentHeight);
    }

    // The laid-out note from `top`, `height` units of it, at `px` pixels a unit
    ComPtr<ID2D1Bitmap1> draw(float width, float top, float height, float px, D2D1_COLOR_F fill) {
        UINT w = (UINT)std::ceil(width * px), h = (UINT)std::ceil(std::max(1.0f, height) * px);
        if (w == 0 || h == 0) return nullptr;
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f * px, 96.0f * px);
        ComPtr<ID2D1Bitmap1> bitmap;
        if (FAILED(dc->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, &props, &bitmap)) || !bitmap) return nullptr;
        dc->SetTarget(bitmap.Get());
        dc->SetDpi(96.0f * px, 96.0f * px);
        dc->BeginDraw();
        dc->Clear(fill);
        drawLayoutRange(app, dc.Get(), brush.Get(), top, top + height, 0.0f, 0.0f);
        bool ok = SUCCEEDED(dc->EndDraw());
        dc->SetTarget(nullptr);
        return ok ? bitmap : nullptr;
    }
};

}  // namespace

float sheetui::sheetDetailThreshold() { return 0.33f; }

namespace sheetui {

void renderCardBitmaps(App& app, SheetDoc& doc, unsigned long long budgetMs) {
    if (!app.deviceContext || !app.renderTarget) return;
    float s = scale(app, doc);
    if (s < sheetDetailThreshold()) return;  // cards show their titles only
    unsigned long long now = nowMs();
    bool zoomSettled = now - doc.zoomChangedMs > 180 && !doc.flight.active;
    float bucket = bucketFor(s);
    Palette pal = palette(app);
    D2D1_RECT_F view = viewRect(app);

    struct Job { int index; float distance; };
    std::vector<Job> jobs;
    for (int i = 0; i < (int)doc.model.nodes.size(); ++i) {
        const Node& n = doc.model.nodes[i];
        if (n.type != NodeType::File && n.type != NodeType::Text) continue;
        if (doc.editing == SheetDoc::Edit::Card && doc.editId == n.id) continue;
        SheetCard& c = card(app, doc, n);
        if (c.kind != SheetCard::Kind::Note) continue;
        D2D1_RECT_F r = screenRect(app, doc, boxOf(n));
        if (!intersects(r, view)) continue;
        float inner = std::max(40.0f, n.width - cardPadding() * 2);
        unsigned long long key = contentKey(app, c, cardFill(app, n, pal), inner);
        float innerH = std::max(20.0f, n.height - cardPadding() * 2);
        bool stale = !c.bitmap || c.bmKey != key || c.bmGeneration != app.renderTargetGeneration;
        bool sliceOff = c.bitmap && (c.scroll < c.bmTop - 0.5f ||
                                     c.scroll + innerH > c.bmTop + c.bmHeight + 0.5f) &&
                        c.bmTop + c.bmHeight < c.contentHeight;
        bool blurry = c.bitmap && zoomSettled && std::fabs(c.bmScale - bucket) > 0.01f;
        if (!stale && !sliceOff && !blurry) continue;
        float cx = (r.left + r.right) * 0.5f - (view.left + view.right) * 0.5f;
        float cy = (r.top + r.bottom) * 0.5f - (view.top + view.bottom) * 0.5f;
        jobs.push_back({i, cx * cx + cy * cy + (stale ? 0.0f : 1e12f)});
    }
    if (jobs.empty()) return;
    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.distance < b.distance; });

    NoteLayoutSession session(app);
    if (!session.ready()) return;

    for (const Job& job : jobs) {
        if (nowMs() - now > budgetMs) break;
        const Node& n = doc.model.nodes[job.index];
        SheetCard& c = doc.cards[n.id];
        float inner = std::max(40.0f, n.width - cardPadding() * 2);
        float innerH = std::max(20.0f, n.height - cardPadding() * 2);
        D2D1_COLOR_F fill = cardFill(app, n, pal);
        std::wstring notePath = n.type == NodeType::File ? c.path : doc.file;
        float contentH = session.layout(c.source, notePath, inner);
        if (contentH <= 0) {
            c.layoutFailed = true;
            c.bmKey = contentKey(app, c, fill, inner);
            continue;
        }
        c.contentHeight = contentH;
        c.scroll = std::clamp(c.scroll, 0.0f, std::max(0.0f, contentH - innerH));
        // The slice around what the card shows, so a short scroll needs no redraw
        float sliceH = std::min(contentH, std::max(innerH * 2.5f, innerH + 400.0f));
        float sliceTop = std::clamp(c.scroll - innerH * 0.5f, 0.0f, std::max(0.0f, contentH - sliceH));
        // A bitmap stays within 4096 pixels a side
        float px = std::min(bucket, 4096.0f / std::max(inner, sliceH));
        ComPtr<ID2D1Bitmap1> bitmap = session.draw(inner, sliceTop, sliceH, px, fill);
        if (!bitmap) continue;
        c.bitmap = bitmap;
        c.bmScale = px;
        c.bmWidth = inner;
        c.bmTop = sliceTop;
        c.bmHeight = sliceH;
        c.bmGeneration = app.renderTargetGeneration;
        c.bmKey = contentKey(app, c, fill, inner);
        c.layoutFailed = false;
    }
}

void renderPeekBitmap(App& app, SheetDoc& doc) {
    if (doc.peekId.empty() || !app.deviceContext) return;
    int i = nodeIndex(doc.model, doc.peekId);
    if (i < 0) return;
    const Node& n = doc.model.nodes[i];
    SheetCard& c = card(app, doc, n);
    if (c.kind != SheetCard::Kind::Note) return;
    float inner = std::max(40.0f, n.width - cardPadding() * 2);
    D2D1_COLOR_F fill = cardFill(app, n, palette(app));
    unsigned long long key = contentKey(app, c, fill, inner) ^ (unsigned long long)(app.contentScale * 64);
    if (doc.peekBitmap && doc.peekBitmapId == n.id && doc.peekBitmapKey == key &&
        doc.peekBitmapGeneration == app.renderTargetGeneration) {
        return;
    }
    NoteLayoutSession session(app);
    if (!session.ready()) return;
    float contentH = session.layout(c.source, n.type == NodeType::File ? c.path : doc.file, inner);
    if (contentH <= 0) return;
    // The lens reads at the screen's scale, up to a long note's first pages
    float px = app.contentScale;
    float height = std::min(contentH, 4096.0f / px);
    px = std::min(px, 4096.0f / std::max(inner, height));
    ComPtr<ID2D1Bitmap1> bitmap = session.draw(inner, 0.0f, height, px, fill);
    if (!bitmap) return;
    doc.peekBitmap = bitmap;
    doc.peekBitmapId = n.id;
    doc.peekBitmapKey = key;
    doc.peekBitmapGeneration = app.renderTargetGeneration;
    doc.peekContentHeight = contentH;
    doc.peekBitmapHeight = height;
    doc.peekBitmapScale = px;
}

}  // namespace sheetui

// --- The frame ------------------------------------------------------------

void sheetPrepareFrame(App& app) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) return;
    if (!doc->placed) {
        if (doc->model.nodes.empty()) {
            flyTo(*doc, 0, 0, 1.0f, 0);
        } else {
            fitBox(app, *doc, bounds(doc->model), false);
        }
        doc->zoomChangedMs = nowMs();
    }
    if (doc->flight.active) {
        float t = std::clamp((float)(nowMs() - doc->flight.startMs) / std::max(1.0f, doc->flight.durationMs), 0.0f, 1.0f);
        float e = easeInOut(t);
        // Zoom eases in log space, so a deep zoom does not rush its end
        float z0 = std::log(doc->flight.fromZoom), z1 = std::log(doc->flight.toZoom);
        doc->zoom = std::exp(z0 + (z1 - z0) * e);
        doc->camX = doc->flight.fromX + (doc->flight.toX - doc->flight.fromX) * e;
        doc->camY = doc->flight.fromY + (doc->flight.toY - doc->flight.fromY) * e;
        doc->zoomChangedMs = nowMs();
        if (t >= 1.0f) doc->flight.active = false;
    }
    renderCardBitmaps(app, *doc, 14);
    renderPeekBitmap(app, *doc);
    ensureTimer(app);
}

namespace {

// --- Background ----------------------------------------------------------

void drawGrid(App& app, SheetDoc& doc, const Palette& p) {
    D2D1_RECT_F v = viewRect(app);
    float s = scale(app, doc);
    // Steps of 20 sheet units, doubled until the dots are at least 14 pixels apart
    float step = kGrid;
    while (step * s < dpi(app, 14.0f)) step *= 2.0f;
    float spacing = step * s;
    float fade = std::clamp((spacing - dpi(app, 14.0f)) / dpi(app, 10.0f), 0.35f, 1.0f);
    D2D1_POINT_2F origin = toSheet(app, doc, v.left, v.top);
    float startX = std::floor(origin.x / step) * step;
    float startY = std::floor(origin.y / step) * step;
    float radius = std::clamp(spacing * 0.055f, dpi(app, 0.7f), dpi(app, 1.6f));
    app.brush->SetColor(alpha(p.dots, fade));
    int count = 0;
    for (float y = startY; ; y += step) {
        D2D1_POINT_2F a = toScreen(app, doc, startX, y);
        if (a.y > v.bottom + spacing) break;
        for (float x = startX; ; x += step) {
            D2D1_POINT_2F q = toScreen(app, doc, x, y);
            if (q.x > v.right + spacing) break;
            app.renderTarget->FillRectangle(D2D1::RectF(q.x - radius, q.y - radius, q.x + radius, q.y + radius), app.brush);
            if (++count > 20000) return;
        }
    }
}

// --- Groups ----------------------------------------------------------------

void drawGroup(App& app, SheetDoc& doc, const Node& g, const Palette& p, float fade) {
    float s = scale(app, doc);
    D2D1_RECT_F r = screenRect(app, doc, drawBox(doc, g));
    if (!intersects(r, viewRect(app))) return;
    bool has = false;
    D2D1_COLOR_F color = presetColor(app, g.color, &has);
    float radius = std::max(dpi(app, 4.0f), 14.0f * s);
    D2D1_COLOR_F fill = has ? alpha(color, app.theme.isDark ? 0.10f : 0.08f) : alpha(p.text, app.theme.isDark ? 0.035f : 0.03f);
    fillRound(app, r, radius, alpha(fill, fade));
    bool selected = isSelected(doc, g.id);
    D2D1_COLOR_F border = has ? alpha(color, 0.55f) : alpha(p.text, 0.20f);
    if (selected) border = p.accent;
    strokeRound(app, r, radius, alpha(border, fade), selected ? dpi(app, 2.0f) : dpi(app, 1.25f),
                g.laneKey.empty() ? nullptr : nullptr);
    // The label, and for a lane the property it writes
    Box label = groupLabelBox(app, doc, g);
    D2D1_RECT_F lr = screenRect(app, doc, label);
    float px = std::clamp(17.0f * s, dpi(app, 9.0f), dpi(app, 44.0f));
    if (px < dpi(app, 9.5f) && s < 0.2f) px = dpi(app, 9.5f);
    std::wstring text = toWide(g.label);
    if (text.empty()) text = tr(app, "sheet.group_untitled");
    IDWriteTextLayout* l = makeLayout(app, text, textFormat(app, docFace(app), px, DWRITE_FONT_WEIGHT_SEMI_BOLD, false),
                                      std::max(1.0f, r.right - r.left - 24.0f * s), px * 2);
    DWRITE_TEXT_METRICS m = metrics(l);
    float tx = lr.left, ty = lr.top;
    drawText(app, l, tx, ty, alpha(has ? mix(color, p.heading, 0.35f) : p.heading, fade * (g.label.empty() ? 0.5f : 1.0f)));
    if (l) l->Release();
    if (!g.laneKey.empty() && s > 0.12f) {
        // Lane chip: "status = Doing" and how many notes sit in it
        int count = 0;
        for (int i = 0; i < (int)doc.model.nodes.size(); ++i) {
            if (doc.model.nodes[i].type != NodeType::Group && insideGroup(g, doc.model.nodes[i])) ++count;
        }
        // Over its limit, the lane says so in red
        bool over = g.laneLimit > 0 && count > g.laneLimit;
        std::wstring chip = toWide(g.laneKey) + L": " + text + L" \u00B7 " + std::to_wstring(count) +
                            (g.laneLimit > 0 ? L"/" + std::to_wstring(g.laneLimit) : std::wstring());
        if (over) strokeRound(app, r, radius, alpha(presetColor(app, "1"), 0.85f * fade), dpi(app, 2.0f));
        float cpx = std::clamp(11.5f * s, dpi(app, 8.0f), dpi(app, 22.0f));
        IDWriteTextLayout* cl = makeLayout(app, chip, textFormat(app, uiFace(), cpx, DWRITE_FONT_WEIGHT_NORMAL, false), 4000, cpx * 2);
        DWRITE_TEXT_METRICS cm = metrics(cl);
        float cx = tx + m.width + cpx * 0.9f, cy = ty + (m.height - cm.height) * 0.5f;
        D2D1_RECT_F pill = D2D1::RectF(cx - cpx * 0.55f, cy - cpx * 0.12f, cx + cm.width + cpx * 0.55f, cy + cm.height + cpx * 0.12f);
        if (pill.right < r.right) {
            fillRound(app, pill, (pill.bottom - pill.top) * 0.5f,
                      over ? alpha(presetColor(app, "1"), 0.32f * fade) : alpha(has ? color : p.accent, 0.16f * fade));
            drawText(app, cl, cx, cy, alpha(p.text, 0.75f * fade));
        }
        if (cl) cl->Release();
    }
}

// --- Connections ---------------------------------------------------------

void drawCurve(App& app, SheetDoc& doc, const Curve& c, D2D1_COLOR_F color, float width, bool arrowEnd,
               bool arrowStart, bool dashed) {
    // Curves are built once in sheet units and drawn through the view's
    // transform, so panning makes no geometry; one triangle is every arrowhead
    float s = scale(app, doc);
    float arrowLen = std::clamp(14.0f * s, dpi(app, 6.0f), dpi(app, 18.0f));
    float headSheet = std::round(arrowLen) / s;  // whole pixels: steady while panning
    // The line stops at the arrowhead's base
    D2D1_POINT_2F start = c.p0, end = c.p3;
    if (arrowEnd) end = D2D1::Point2F(c.p3.x + c.n3.x * headSheet * 0.85f, c.p3.y + c.n3.y * headSheet * 0.85f);
    if (arrowStart) start = D2D1::Point2F(c.p0.x + c.n0.x * headSheet * 0.85f, c.p0.y + c.n0.y * headSheet * 0.85f);
    static std::unordered_map<std::string, ComPtr<ID2D1PathGeometry>> paths;
    static ID2D1Factory* pathOwner = nullptr;
    if (pathOwner != app.d2dFactory || paths.size() > 6000) {
        paths.clear();
        pathOwner = app.d2dFactory;
    }
    char key[200];
    snprintf(key, sizeof key, "%.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f", start.x, start.y, c.p1.x, c.p1.y, c.p2.x, c.p2.y,
             end.x, end.y);
    ComPtr<ID2D1PathGeometry>& path = paths[key];
    if (!path) {
        app.d2dFactory->CreatePathGeometry(&path);
        ComPtr<ID2D1GeometrySink> sink;
        if (!path || FAILED(path->Open(&sink))) {
            path.Reset();
            return;
        }
        sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddBezier(D2D1::BezierSegment(c.p1, c.p2, end));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
    }
    static ComPtr<ID2D1PathGeometry> triangle;
    static ID2D1Factory* triangleOwner = nullptr;
    if (!triangle || triangleOwner != app.d2dFactory) {
        triangle.Reset();
        triangleOwner = app.d2dFactory;
        app.d2dFactory->CreatePathGeometry(&triangle);
        ComPtr<ID2D1GeometrySink> sink;
        if (triangle && SUCCEEDED(triangle->Open(&sink))) {
            // Tip at the origin, base along +x
            sink->BeginFigure(D2D1::Point2F(0, 0), D2D1_FIGURE_BEGIN_FILLED);
            D2D1_POINT_2F pts[2] = {D2D1::Point2F(1.0f, 0.42f), D2D1::Point2F(1.0f, -0.42f)};
            sink->AddLines(pts, 2);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
        }
    }
    D2D1_RECT_F v = viewRect(app);
    D2D1_MATRIX_3X2_F saved;
    app.renderTarget->GetTransform(&saved);
    D2D1_MATRIX_3X2_F view = D2D1::Matrix3x2F::Translation(-doc.camX, -doc.camY) * D2D1::Matrix3x2F::Scale(s, s) *
                             D2D1::Matrix3x2F::Translation((v.left + v.right) * 0.5f, (v.top + v.bottom) * 0.5f);
    app.brush->SetColor(color);
    app.renderTarget->SetTransform(view * saved);
    app.renderTarget->DrawGeometry(path.Get(), app.brush, width / s, dashed ? dashedStyle(app) : roundStyle(app));
    auto head = [&](D2D1_POINT_2F tip, D2D1_POINT_2F n) {
        if (!triangle) return;
        D2D1_POINT_2F screen = toScreen(app, doc, tip.x, tip.y);
        float angle = std::atan2(n.y, n.x) * 57.29578f;
        app.renderTarget->SetTransform(D2D1::Matrix3x2F::Scale(arrowLen, arrowLen) * D2D1::Matrix3x2F::Rotation(angle) *
                                       D2D1::Matrix3x2F::Translation(screen.x, screen.y) * saved);
        app.renderTarget->FillGeometry(triangle.Get(), app.brush);
    };
    if (arrowEnd) head(c.p3, c.n3);
    if (arrowStart) head(c.p0, c.n0);
    app.renderTarget->SetTransform(saved);
}

void drawEdgeLabel(App& app, SheetDoc& doc, const Curve& c, const std::string& label, const Palette& p, float fade,
                   bool selected) {
    float s = scale(app, doc) / app.contentScale;
    if (label.empty() || s < 0.3f) return;
    s = scale(app, doc);
    float px = std::clamp(13.0f * s, dpi(app, 8.0f), dpi(app, 26.0f));
    IDWriteTextLayout* l = makeLayout(app, toWide(label), textFormat(app, uiFace(), px, DWRITE_FONT_WEIGHT_NORMAL, false),
                                      std::max(40.0f, 260.0f * s), px * 2);
    DWRITE_TEXT_METRICS m = metrics(l);
    D2D1_POINT_2F mid = bezier(c, 0.5f);
    D2D1_POINT_2F q = toScreen(app, doc, mid.x, mid.y);
    D2D1_RECT_F pill = D2D1::RectF(q.x - m.width * 0.5f - px * 0.6f, q.y - m.height * 0.5f - px * 0.25f,
                                   q.x + m.width * 0.5f + px * 0.6f, q.y + m.height * 0.5f + px * 0.25f);
    fillRound(app, pill, (pill.bottom - pill.top) * 0.5f, alpha(p.canvas, 0.96f * fade));
    strokeRound(app, pill, (pill.bottom - pill.top) * 0.5f, alpha(selected ? p.accent : p.text, (selected ? 0.9f : 0.18f) * fade),
                dpi(app, 1.0f));
    drawText(app, l, q.x - m.width * 0.5f, q.y - m.height * 0.5f, alpha(p.text, 0.85f * fade));
    if (l) l->Release();
}

// --- Cards -----------------------------------------------------------------

void drawSkeleton(App& app, const D2D1_RECT_F& r, float s, const Palette& p, float fade) {
    float pad = cardPadding() * s;
    float y = r.top + pad;
    float h = std::max(dpi(app, 2.0f), 10.0f * s);
    float widths[] = {0.62f, 0.9f, 0.82f, 0.95f, 0.5f};
    for (int i = 0; i < 5 && y + h < r.bottom - pad; ++i) {
        D2D1_RECT_F bar = D2D1::RectF(r.left + pad, y, r.left + pad + (r.right - r.left - pad * 2) * widths[i], y + h);
        fillRound(app, bar, h * 0.5f, alpha(p.text, (i == 0 ? 0.14f : 0.07f) * fade));
        y += h * (i == 0 ? 2.6f : 2.0f);
    }
}

// Zoomed out a card is its title, big enough to read, with what is in it
void drawOverview(App& app, SheetDoc& doc, const Node& n, SheetCard& c, const D2D1_RECT_F& r,
                  const Palette& p, float fade) {
    float w = r.right - r.left, h = r.bottom - r.top;
    if (w < dpi(app, 14.0f) || h < dpi(app, 10.0f)) return;
    std::wstring title = toWide(cardTitle(app, doc, n));
    if (title.empty()) title = toWide(n.type == NodeType::Text ? noteTitle(n.text, "") : std::string());
    float pad = std::max(dpi(app, 4.0f), std::min(w, h) * 0.08f);
    float px = std::clamp(std::min(h * 0.20f, w * 0.13f), dpi(app, 8.5f), dpi(app, 30.0f));
    IDWriteTextLayout* l = cachedLayout(app, c.texts[0], title, textFormat(app, docFace(app), px, DWRITE_FONT_WEIGHT_SEMI_BOLD),
                                        w - pad * 2, std::max(px * 1.4f, h - pad * 2 - (h > px * 4 ? px * 1.4f : 0)));
    drawText(app, l, r.left + pad, r.top + pad, alpha(p.heading, fade));
    // Task progress and words along the bottom
    if (h > px * 3.2f && c.kind == SheetCard::Kind::Note) {
        float my = r.bottom - pad - px * 0.9f;
        float mx = r.left + pad;
        if (c.tasksTotal > 0) {
            float barW = std::min(w - pad * 2, px * 6.0f);
            D2D1_RECT_F bar = D2D1::RectF(mx, my + px * 0.3f, mx + barW, my + px * 0.55f);
            fillRound(app, bar, px * 0.12f, alpha(p.text, 0.12f * fade));
            D2D1_RECT_F done = bar;
            done.right = bar.left + barW * c.tasksDone / std::max(1, c.tasksTotal);
            fillRound(app, done, px * 0.12f, alpha(c.tasksDone == c.tasksTotal ? presetColor(app, "4") : p.accent, fade));
            mx += barW + px * 0.6f;
        }
        std::wstring meta = c.tasksTotal > 0 ? std::to_wstring(c.tasksDone) + L"/" + std::to_wstring(c.tasksTotal) : L"";
        if (c.words > 0) {
            meta += (meta.empty() ? L"" : L"  \u00B7  ") + trCount(app.currentLanguageIndex, "sheet.words", c.words);
        }
        IDWriteTextLayout* ml = cachedLayout(app, c.texts[1], meta,
                                             textFormat(app, uiFace(), px * 0.62f, DWRITE_FONT_WEIGHT_NORMAL, false),
                                             std::max(1.0f, r.right - pad - mx), px);
        drawText(app, ml, mx, my + px * 0.05f, alpha(p.text, 0.55f * fade));
    }
}

void drawImageCard(App& app, SheetDoc& doc, SheetCard& c, const D2D1_RECT_F& r, float radius, float fade) {
    static std::map<std::wstring, std::pair<ComPtr<ID2D1Bitmap>, unsigned>> images;
    auto it = images.find(c.path);
    if (it == images.end() || it->second.second != app.renderTargetGeneration) {
        if (images.size() > 64) images.clear();
        ComPtr<ID2D1Bitmap> bitmap;
        DecodedImage decoded;
        if (app.wicFactory && SUCCEEDED(decodeImageFile(app.wicFactory, c.path.c_str(), 2048, decoded)) &&
            decoded.width && decoded.height) {
            D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            app.renderTarget->CreateBitmap(D2D1::SizeU(decoded.width, decoded.height), decoded.pixels.data(),
                                           decoded.width * 4, props, &bitmap);
        }
        it = images.insert_or_assign(c.path, std::make_pair(bitmap, app.renderTargetGeneration)).first;
    }
    (void)doc;
    ID2D1Bitmap* bitmap = it->second.first.Get();
    if (!bitmap) return;
    D2D1_SIZE_F size = bitmap->GetSize();
    float w = r.right - r.left, h = r.bottom - r.top;
    float k = std::min(w / size.width, h / size.height);
    float dw = size.width * k, dh = size.height * k;
    D2D1_RECT_F dest = D2D1::RectF(r.left + (w - dw) * 0.5f, r.top + (h - dh) * 0.5f, r.left + (w + dw) * 0.5f,
                                   r.top + (h + dh) * 0.5f);
    ComPtr<ID2D1RoundedRectangleGeometry> clip;
    app.d2dFactory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(r, radius, radius), &clip);
    if (clip) {
        app.renderTarget->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
    }
    if (app.deviceContext) {
        app.deviceContext->DrawBitmap(bitmap, dest, fade, k < 0.8f ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
                                                                   : D2D1_INTERPOLATION_MODE_LINEAR);
    } else {
        app.renderTarget->DrawBitmap(bitmap, dest, fade);
    }
    if (clip) app.renderTarget->PopLayer();
}

void drawIconText(App& app, const D2D1_RECT_F& r, float s, const std::wstring& big, const std::wstring& detail,
                  const wchar_t* glyph, const Palette& p, float fade) {
    float pad = cardPadding() * s;
    float px = std::clamp(15.0f * s, dpi(app, 8.0f), dpi(app, 34.0f));
    float x = r.left + pad, y = r.top + pad;
    IDWriteTextLayout* g = makeLayout(app, glyph, textFormat(app, L"Segoe UI Symbol", px * 1.3f, DWRITE_FONT_WEIGHT_NORMAL, false), 400, px * 3);
    DWRITE_TEXT_METRICS gm = metrics(g);
    drawText(app, g, x, y - px * 0.1f, alpha(p.accent, fade));
    if (g) g->Release();
    float tx = x + gm.width + px * 0.5f;
    IDWriteTextLayout* b = makeLayout(app, big, textFormat(app, docFace(app), px, DWRITE_FONT_WEIGHT_SEMI_BOLD, false),
                                      std::max(1.0f, r.right - pad - tx), px * 2);
    drawText(app, b, tx, y, alpha(p.heading, fade));
    DWRITE_TEXT_METRICS bm = metrics(b);
    if (b) b->Release();
    if (!detail.empty() && r.bottom - r.top > px * 3.2f) {
        IDWriteTextLayout* sl = makeLayout(app, detail, textFormat(app, uiFace(), px * 0.78f, DWRITE_FONT_WEIGHT_NORMAL),
                                           std::max(1.0f, r.right - r.left - pad * 2),
                                           std::max(1.0f, r.bottom - y - bm.height - pad - px * 0.4f));
        drawText(app, sl, x, y + bm.height + px * 0.5f, alpha(p.text, 0.6f * fade));
        if (sl) sl->Release();
    }
}

std::wstring hostOf(const std::string& url) {
    std::string u = url;
    size_t scheme = u.find("://");
    if (scheme != std::string::npos) u = u.substr(scheme + 3);
    size_t slash = u.find('/');
    if (slash != std::string::npos) u = u.substr(0, slash);
    if (u.rfind("www.", 0) == 0) u = u.substr(4);
    return toWide(u);
}

void drawCard(App& app, SheetDoc& doc, const Node& n, const Palette& p, float fade, bool highlight) {
    float s = scale(app, doc);
    Box box = drawBox(doc, n);
    D2D1_RECT_F r = screenRect(app, doc, box);
    D2D1_RECT_F view = viewRect(app);
    // The name label above a file card reaches out of its box
    D2D1_RECT_F reach = D2D1::RectF(r.left, r.top - 30.0f * s, r.right, r.bottom);
    if (!intersects(reach, view)) return;
    SheetCard& c = card(app, doc, n);
    bool selected = isSelected(doc, n.id);
    bool editing = doc.editing == SheetDoc::Edit::Card && doc.editId == n.id;
    float radius = std::max(dpi(app, 3.0f), 10.0f * s);
    D2D1_COLOR_F fill = cardFill(app, n, p);
    bool has = false;
    D2D1_COLOR_F color = presetColor(app, n.color, &has);

    if (r.bottom - r.top > dpi(app, 40.0f)) shadow(app, r, radius, p, fade);
    fillRound(app, r, radius, alpha(fill, fade));

    float pad = cardPadding() * s;
    D2D1_RECT_F inner = D2D1::RectF(r.left + pad, r.top + pad, r.right - pad, r.bottom - pad);
    bool detail = s >= sheetDetailThreshold() && inner.right - inner.left > dpi(app, 40.0f);

    if (editing) {
        // The text box draws over it (drawEditBox)
    } else if (c.kind == SheetCard::Kind::Image) {
        drawImageCard(app, doc, c, r, radius, fade);
    } else if (c.kind == SheetCard::Kind::Note) {
        if (detail && c.bitmap && c.bmGeneration == app.renderTargetGeneration) {
            float innerH = std::max(1.0f, n.height - cardPadding() * 2);
            float top = std::clamp(c.scroll - c.bmTop, 0.0f, std::max(0.0f, c.bmHeight - 1.0f));
            float showH = std::min(innerH, c.bmHeight - top);
            D2D1_RECT_F src = D2D1::RectF(0, top, c.bmWidth, top + showH);
            D2D1_RECT_F dest = D2D1::RectF(inner.left, inner.top, inner.left + c.bmWidth * s, inner.top + showH * s);
            app.renderTarget->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if (app.deviceContext) {
                float ratio = s / std::max(0.01f, c.bmScale);
                app.deviceContext->DrawBitmap(c.bitmap.Get(), dest, fade,
                                              ratio < 0.75f ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
                                                            : D2D1_INTERPOLATION_MODE_LINEAR,
                                              src);
            } else {
                app.renderTarget->DrawBitmap(c.bitmap.Get(), dest, fade, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
            }
            app.renderTarget->PopAxisAlignedClip();
            // More below: the content fades into the card's foot
            if (c.contentHeight > c.scroll + innerH + 1.0f) {
                float band = std::min(28.0f * s, (inner.bottom - inner.top) * 0.4f);
                for (int i = 0; i < 8; ++i) {
                    float t0 = (float)i / 8, t1 = (float)(i + 1) / 8;
                    app.brush->SetColor(alpha(fill, fade * (0.15f + 0.85f * t1)));
                    app.renderTarget->FillRectangle(
                        D2D1::RectF(inner.left, inner.bottom - band + band * t0, inner.right, inner.bottom - band + band * t1 + 0.5f),
                        app.brush);
                }
                app.renderTarget->FillRectangle(D2D1::RectF(inner.left, inner.bottom, inner.right, r.bottom - radius * 0.5f), app.brush);
            }
            if (c.scroll > 0.5f || c.contentHeight > innerH + 1.0f) {
                // A slim scroll mark on the right edge
                float trackTop = r.top + radius, trackBottom = r.bottom - radius;
                float frac = innerH / std::max(innerH, c.contentHeight);
                float thumbH = std::max(dpi(app, 12.0f), (trackBottom - trackTop) * frac);
                float pos = c.contentHeight > innerH ? c.scroll / (c.contentHeight - innerH) : 0;
                float ty = trackTop + (trackBottom - trackTop - thumbH) * pos;
                if ((selected || doc.hoverNode == n.id) && !doc.presenting) {
                    fillRound(app, D2D1::RectF(r.right - dpi(app, 5.0f), ty, r.right - dpi(app, 2.5f), ty + thumbH),
                              dpi(app, 1.5f), alpha(p.text, 0.28f * fade));
                }
            }
        } else if (detail) {
            drawSkeleton(app, r, s, p, fade);
        } else {
            drawOverview(app, doc, n, c, r, p, fade);
        }
    } else if (n.type == NodeType::Link) {
        if (!detail) drawOverview(app, doc, n, c, r, p, fade);
        else drawIconText(app, r, s, hostOf(n.url), toWide(n.url), L"\u2197", p, fade);
    } else if (c.kind == SheetCard::Kind::Missing) {
        drawIconText(app, r, s, toWide(c.title.empty() ? n.file : c.title), tr(app, "sheet.missing"), L"\u26A0", p, fade);
    } else if (c.kind == SheetCard::Kind::Blocked) {
        drawIconText(app, r, s, toWide(c.title), tr(app, "image.network_blocked"), L"\U0001F512", p, fade);
    } else {
        std::wstring name = c.path.substr(c.path.find_last_of(L"\\/") + 1);
        drawIconText(app, r, s, name, tr(app, "sheet.open_file_hint"), L"\U0001F4C4", p, fade);
    }

    // Border: colour, selection, find and spotlight marks
    D2D1_COLOR_F border = has ? alpha(color, 0.85f) : p.cardBorder;
    float bw = has ? dpi(app, 1.5f) : dpi(app, 1.0f);
    if (c.kind == SheetCard::Kind::Missing) {
        strokeRound(app, r, radius, alpha(p.text, 0.45f * fade), dpi(app, 1.25f), dashedStyle(app));
    } else {
        strokeRound(app, r, radius, alpha(border, fade), bw);
    }
    if (has && s > 0.2f) {
        // A band of the card's colour along its top
        D2D1_RECT_F band = D2D1::RectF(r.left + radius * 0.6f, r.top, r.right - radius * 0.6f, r.top + std::max(dpi(app, 2.0f), 4.0f * s));
        fillRound(app, band, dpi(app, 1.0f), alpha(color, fade));
    }
    if (highlight) {
        D2D1_RECT_F ring = D2D1::RectF(r.left - dpi(app, 4.0f), r.top - dpi(app, 4.0f), r.right + dpi(app, 4.0f), r.bottom + dpi(app, 4.0f));
        strokeRound(app, ring, radius + dpi(app, 4.0f), alpha(p.accent, 0.55f), dpi(app, 3.0f));
    }
    if ((selected || editing) && !doc.presenting) {
        D2D1_RECT_F ring = D2D1::RectF(r.left - dpi(app, 1.5f), r.top - dpi(app, 1.5f), r.right + dpi(app, 1.5f), r.bottom + dpi(app, 1.5f));
        strokeRound(app, ring, radius + dpi(app, 1.5f), p.accent, dpi(app, 2.0f));
    }

    // The note's name above the card (Obsidian's card label), and its tasks
    if (n.type == NodeType::File && s >= 0.30f && c.kind != SheetCard::Kind::Image) {
        std::wstring name = c.path.substr(c.path.find_last_of(L"\\/") + 1);
        size_t dot = name.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0 && c.kind == SheetCard::Kind::Note) name = name.substr(0, dot);
        if (c.tasksTotal > 0) name += L"   \u2611 " + std::to_wstring(c.tasksDone) + L"/" + std::to_wstring(c.tasksTotal);
        float px = std::clamp(12.0f * s, dpi(app, 9.0f), dpi(app, 15.0f));
        IDWriteTextLayout* l = cachedLayout(app, c.texts[2], name,
                                            textFormat(app, uiFace(), px, DWRITE_FONT_WEIGHT_NORMAL, false),
                                            std::max(1.0f, r.right - r.left), px * 2);
        DWRITE_TEXT_METRICS m = metrics(l);
        drawText(app, l, r.left + dpi(app, 2.0f), r.top - m.height - dpi(app, 3.0f),
                 alpha(p.text, (selected || doc.hoverNode == n.id ? 0.75f : 0.48f) * fade));
    }

    // Connection dots on the sides of the card under the pointer
    if ((doc.hoverNode == n.id || selected) && s > 0.25f && !editing && doc.gesture != SheetDoc::Gesture::Move &&
        doc.gesture != SheetDoc::Gesture::Resize && !doc.presenting) {
        for (Side side : {Side::Top, Side::Right, Side::Bottom, Side::Left}) {
            D2D1_POINT_2F q = sidePoint(box, side);
            D2D1_POINT_2F sq = toScreen(app, doc, q.x, q.y);
            float rr = dpi(app, 5.0f);
            app.brush->SetColor(p.surface);
            app.renderTarget->FillEllipse(D2D1::Ellipse(sq, rr, rr), app.brush);
            app.brush->SetColor(alpha(p.accent, 0.9f));
            app.renderTarget->DrawEllipse(D2D1::Ellipse(sq, rr, rr), app.brush, dpi(app, 1.5f));
        }
    }
    // Resize corner of a single selected card
    if (selected && doc.selected.size() == 1 && !editing && s > 0.15f && !doc.presenting) {
        float k = dpi(app, 7.0f);
        D2D1_RECT_F knob = D2D1::RectF(r.right - k * 0.5f, r.bottom - k * 0.5f, r.right + k * 0.5f, r.bottom + k * 0.5f);
        fillRound(app, knob, dpi(app, 2.0f), p.surface);
        strokeRound(app, knob, dpi(app, 2.0f), p.accent, dpi(app, 1.5f));
    }
}

// --- The text box while editing ------------------------------------------

void drawEditBox(App& app, SheetDoc& doc, const Palette& p) {
    if (doc.editing == SheetDoc::Edit::None || doc.editing == SheetDoc::Edit::Find) return;
    D2D1_RECT_F rect{};
    float textScale = 1.0f;
    if (!editRect(app, doc, rect, textScale)) return;
    float width = (rect.right - rect.left) / textScale;
    IDWriteTextLayout* layout = editLayout(app, doc, width);
    if (!layout) return;
    bool card = doc.editing == SheetDoc::Edit::Card;
    if (!card) {
        // A field of its own: a pill over the label or address it edits
        D2D1_RECT_F box = D2D1::RectF(rect.left - dpi(app, 8.0f), rect.top - dpi(app, 5.0f), rect.right + dpi(app, 8.0f),
                                      rect.bottom + dpi(app, 5.0f));
        shadow(app, box, dpi(app, 7.0f), p, 1.0f);
        fillRound(app, box, dpi(app, 7.0f), p.surface);
        strokeRound(app, box, dpi(app, 7.0f), p.accent, dpi(app, 1.5f));
    }
    app.renderTarget->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    D2D1_MATRIX_3X2_F saved;
    app.renderTarget->GetTransform(&saved);
    app.renderTarget->SetTransform(D2D1::Matrix3x2F::Scale(textScale, textScale) *
                                   D2D1::Matrix3x2F::Translation(rect.left, rect.top - doc.edit.scrollY * textScale) * saved);
    live::draw(app, doc.edit, layout, doc.editCache, 0, editLook(app, doc), app.cursorBlinkOn, dpi(app, 1.0f) / textScale);
    app.renderTarget->SetTransform(saved);
    app.renderTarget->PopAxisAlignedClip();
    if (!card && doc.edit.text.empty()) {
        const char* key = doc.editing == SheetDoc::Edit::Url       ? "sheet.url_placeholder"
                          : doc.editing == SheetDoc::Edit::LaneKey ? "sheet.lane_placeholder"
                          : doc.editing == SheetDoc::Edit::LaneLimit ? "sheet.limit_placeholder"
                                                                   : "sheet.label_placeholder";
        IDWriteTextLayout* l = makeLayout(app, tr(app, key), textFormat(app, uiFace(), editFontSize(doc) * textScale,
                                                                        DWRITE_FONT_WEIGHT_NORMAL, false),
                                          rect.right - rect.left, rect.bottom - rect.top);
        drawText(app, l, rect.left + dpi(app, 2.0f), rect.top, alpha(p.text, 0.4f));
        if (l) l->Release();
    }
}

// --- Chrome: tool bar, zoom, minimap, tray, find, presentation, menu ------

void icon(App& app, int which, const D2D1_RECT_F& r, D2D1_COLOR_F c) {
    float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    float u = dpi(app, 1.0f);
    float w = 1.5f * u;
    auto L = [&](float x0, float y0, float x1, float y1) {
        line(app, D2D1::Point2F(cx + x0 * u, cy + y0 * u), D2D1::Point2F(cx + x1 * u, cy + y1 * u), c, w, roundStyle(app));
    };
    auto R = [&](float x0, float y0, float x1, float y1, float rad, bool dashed = false) {
        strokeRound(app, D2D1::RectF(cx + x0 * u, cy + y0 * u, cx + x1 * u, cy + y1 * u), rad * u, c, w,
                    dashed ? dashedStyle(app) : nullptr);
    };
    switch (which) {
        case ACT_NEW_NOTE:  // a card with a plus
            R(-8, -7, 6, 7, 2);
            L(9, -9, 9, -3);
            L(6, -6, 12, -6);
            L(-4, -2, 2, -2);
            L(-4, 2, 0, 2);
            break;
        case ACT_ADD_FILES:  // a page with a folded corner
            L(-6, -8, 2, -8);
            L(2, -8, 7, -3);
            L(7, -3, 7, 8);
            L(7, 8, -6, 8);
            L(-6, 8, -6, -8);
            L(2, -8, 2, -3);
            L(2, -3, 7, -3);
            break;
        case ACT_ADD_LINK:  // two chain links
            R(-10, -4, 0, 4, 4);
            R(0, -4, 10, 4, 4);
            break;
        case ACT_ADD_GROUP:  // a dashed frame with a tab
            R(-9, -6, 9, 8, 2, true);
            L(-9, -9, -2, -9);
            break;
        case ACT_TIDY:  // boxes in layers with arrows
            R(-10, -7, -4, -1, 1);
            R(-10, 2, -4, 8, 1);
            R(4, -3, 10, 3, 1);
            L(-4, -4, 4, 0);
            L(-4, 5, 4, 0);
            break;
        case ACT_PRESENT: {  // a play triangle
            ComPtr<ID2D1PathGeometry> tri;
            app.d2dFactory->CreatePathGeometry(&tri);
            ComPtr<ID2D1GeometrySink> sink;
            if (tri && SUCCEEDED(tri->Open(&sink))) {
                sink->BeginFigure(D2D1::Point2F(cx - 5 * u, cy - 7 * u), D2D1_FIGURE_BEGIN_FILLED);
                D2D1_POINT_2F pts[2] = {D2D1::Point2F(cx + 7 * u, cy), D2D1::Point2F(cx - 5 * u, cy + 7 * u)};
                sink->AddLines(pts, 2);
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                app.brush->SetColor(c);
                app.renderTarget->FillGeometry(tri.Get(), app.brush);
            }
            break;
        }
        case ACT_COMPILE:  // cards into a page
            R(-10, -8, -3, -2, 1);
            R(-10, 2, -3, 8, 1);
            L(-2, -5, 2, 0);
            L(-2, 5, 2, 0);
            R(3, -8, 11, 8, 1);
            L(5, -3, 9, -3);
            L(5, 1, 9, 1);
            break;
        case ACT_GHOSTS:  // a dashed link between two dots
            app.brush->SetColor(c);
            app.renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 8 * u, cy + 5 * u), 2.5f * u, 2.5f * u), app.brush);
            app.renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 8 * u, cy - 5 * u), 2.5f * u, 2.5f * u), app.brush);
            line(app, D2D1::Point2F(cx - 6 * u, cy + 3.5f * u), D2D1::Point2F(cx + 6 * u, cy - 3.5f * u), c, w, dashedStyle(app));
            break;
        case ACT_MINIMAP:  // a small map with its viewport
            R(-10, -7, 10, 7, 2);
            R(-2, -3, 6, 3, 1);
            break;
        case ACT_ZOOM_OUT:
            L(-5, 0, 5, 0);
            break;
        case ACT_ZOOM_IN:
            L(-5, 0, 5, 0);
            L(0, -5, 0, 5);
            break;
        case ACT_ZOOM_FIT:  // corners
            L(-8, -8, -3, -8);
            L(-8, -8, -8, -3);
            L(8, -8, 3, -8);
            L(8, -8, 8, -3);
            L(-8, 8, -3, 8);
            L(-8, 8, -8, 3);
            L(8, 8, 3, 8);
            L(8, 8, 8, 3);
            break;
        case ACT_PRESENT_PREV:
            L(3, -6, -3, 0);
            L(-3, 0, 3, 6);
            break;
        case ACT_PRESENT_NEXT:
            L(-3, -6, 3, 0);
            L(3, 0, -3, 6);
            break;
        case ACT_GROUP_SELECTION:  // as Add group
            R(-9, -6, 9, 8, 2, true);
            L(-9, -9, -2, -9);
            break;
        case ACT_CHAIN:  // three cards joined in a row
            R(-11, -3, -6, 3, 1);
            R(-2.5f, -3, 2.5f, 3, 1);
            R(6, -3, 11, 3, 1);
            L(-6, 0, -2.5f, 0);
            L(2.5f, 0, 6, 0);
            break;
        case ACT_ALIGN_MENU:  // bars against a line
            L(-8, -8, -8, 8);
            R(-5, -6, 7, -2, 1);
            R(-5, 2, 3, 6, 1);
            break;
        case ACT_FOLD:  // chevrons closing
            L(-6, -8, 0, -3);
            L(0, -3, 6, -8);
            L(-6, 8, 0, 3);
            L(0, 3, 6, 8);
            break;
        case ACT_UNFOLD_ALL:  // chevrons opening
            L(-6, -3, 0, -8);
            L(0, -8, 6, -3);
            L(-6, 3, 0, 8);
            L(0, 8, 6, 3);
            break;
        case ACT_REMOVE:  // a bin
            L(-7, -5, 7, -5);
            L(-2.5f, -5, -2.5f, -8);
            L(-2.5f, -8, 2.5f, -8);
            L(2.5f, -8, 2.5f, -5);
            L(-5, -5, -4, 8);
            L(-4, 8, 4, 8);
            L(4, 8, 5, -5);
            L(-1.5f, -2, -1.5f, 5);
            L(1.5f, -2, 1.5f, 5);
            break;
        case ACT_PRESENT_EXIT:
        case ACT_FIND_CLOSE:
            L(-5, -5, 5, 5);
            L(5, -5, -5, 5);
            break;
        default:
            break;
    }
}

void addHit(SheetDoc& doc, const D2D1_RECT_F& r, int id) { doc.uiHits.push_back({r, id}); }

void drawTip(App& app, SheetDoc& doc, const Palette& p, float x, float y) {
    if (doc.tip.empty()) return;
    IDWriteTextLayout* l = makeLayout(app, doc.tip, textFormat(app, uiFace(), dpi(app, 12.0f), DWRITE_FONT_WEIGHT_NORMAL, false), 1000, 100);
    DWRITE_TEXT_METRICS m = metrics(l);
    D2D1_RECT_F box = D2D1::RectF(x - m.width * 0.5f - dpi(app, 8.0f), y - m.height - dpi(app, 10.0f),
                                  x + m.width * 0.5f + dpi(app, 8.0f), y - dpi(app, 4.0f));
    float shift = 0;
    if (box.left < dpi(app, 4.0f)) shift = dpi(app, 4.0f) - box.left;
    if (box.right > app.width - dpi(app, 4.0f)) shift = app.width - dpi(app, 4.0f) - box.right;
    box.left += shift;
    box.right += shift;
    fillRound(app, box, dpi(app, 5.0f), alpha(app.theme.isDark ? D2D1::ColorF(0.95f, 0.95f, 0.97f) : D2D1::ColorF(0.13f, 0.14f, 0.17f), 0.96f));
    drawText(app, l, box.left + dpi(app, 8.0f), box.top + dpi(app, 3.0f),
             app.theme.isDark ? D2D1::ColorF(0.1f, 0.1f, 0.12f) : D2D1::ColorF(0.96f, 0.96f, 0.97f));
    if (l) l->Release();
    (void)p;
}

void drawToolbar(App& app, SheetDoc& doc, const Palette& p, float rightLimit) {
    struct Btn { int id; bool on; };
    std::vector<Btn> btns = {{ACT_NEW_NOTE, false}, {ACT_ADD_FILES, false}, {ACT_ADD_LINK, false}, {ACT_ADD_GROUP, false},
                             {0, false},           {ACT_TIDY, false},      {ACT_PRESENT, false},  {ACT_COMPILE, false},
                             {0, false},           {ACT_GHOSTS, doc.showGhosts}, {ACT_MINIMAP, doc.showMinimap}};
    float size = dpi(app, 36.0f), gap = dpi(app, 2.0f), sep = dpi(app, 12.0f);
    float total = 0;
    for (const Btn& b : btns) total += b.id ? size + gap : sep;
    total -= gap;
    D2D1_RECT_F v = viewRect(app);
    float padX = dpi(app, 6.0f);
    float cx = (v.left + v.right) * 0.5f;
    // Centred, unless that runs into the zoom control on the right
    if (cx + total * 0.5f + padX > rightLimit - dpi(app, 12.0f)) {
        cx = std::max(v.left + total * 0.5f + padX + dpi(app, 12.0f),
                      rightLimit - dpi(app, 12.0f) - total * 0.5f - padX);
    }
    float y = v.bottom - dpi(app, 16.0f) - size - dpi(app, 8.0f);
    D2D1_RECT_F bar = D2D1::RectF(cx - total * 0.5f - padX, y - dpi(app, 4.0f), cx + total * 0.5f + padX, y + size + dpi(app, 4.0f));
    shadow(app, bar, dpi(app, 12.0f), p, 1.2f);
    fillRound(app, bar, dpi(app, 12.0f), p.surface);
    strokeRound(app, bar, dpi(app, 12.0f), p.surfaceBorder, dpi(app, 1.0f));
    addHit(doc, bar, -1);
    float x = cx - total * 0.5f;
    float tipX = 0;
    for (const Btn& b : btns) {
        if (!b.id) {
            line(app, D2D1::Point2F(x + sep * 0.5f, y + dpi(app, 8.0f)), D2D1::Point2F(x + sep * 0.5f, y + size - dpi(app, 8.0f)),
                 alpha(p.text, 0.15f), dpi(app, 1.0f));
            x += sep;
            continue;
        }
        D2D1_RECT_F r = D2D1::RectF(x, y, x + size, y + size);
        bool hover = doc.hoverUi == b.id;
        if (b.on) fillRound(app, r, dpi(app, 8.0f), alpha(p.accent, 0.16f));
        if (hover) {
            fillRound(app, r, dpi(app, 8.0f), alpha(p.text, 0.08f));
            tipX = (r.left + r.right) * 0.5f;
        }
        icon(app, b.id, r, b.on ? p.accent : alpha(p.text, hover ? 0.95f : 0.75f));
        addHit(doc, r, b.id);
        x += size + gap;
    }
    if (tipX > 0) drawTip(app, doc, p, tipX, bar.top);
}

float drawZoom(App& app, SheetDoc& doc, const Palette& p, float right, float bottom) {
    float h = dpi(app, 32.0f), w = dpi(app, 32.0f);
    wchar_t pct[16];
    swprintf_s(pct, L"%d%%", (int)std::lround(doc.zoom * 100));
    IDWriteTextLayout* l = makeLayout(app, pct, textFormat(app, uiFace(), dpi(app, 12.0f), DWRITE_FONT_WEIGHT_NORMAL, false), 200, h);
    DWRITE_TEXT_METRICS m = metrics(l);
    float pctW = std::max(dpi(app, 46.0f), m.width + dpi(app, 14.0f));
    float total = w * 3 + pctW;
    D2D1_RECT_F bar = D2D1::RectF(right - total - dpi(app, 8.0f), bottom - h - dpi(app, 4.0f), right, bottom + dpi(app, 4.0f) - dpi(app, 4.0f));
    bar.bottom = bar.top + h + dpi(app, 8.0f);
    shadow(app, bar, dpi(app, 10.0f), p, 1.0f);
    fillRound(app, bar, dpi(app, 10.0f), p.surface);
    strokeRound(app, bar, dpi(app, 10.0f), p.surfaceBorder, dpi(app, 1.0f));
    addHit(doc, bar, -1);
    float x = bar.left + dpi(app, 4.0f), y = bar.top + dpi(app, 4.0f);
    auto button = [&](int id, float width) {
        D2D1_RECT_F r = D2D1::RectF(x, y, x + width, y + h);
        if (doc.hoverUi == id) fillRound(app, r, dpi(app, 7.0f), alpha(p.text, 0.08f));
        addHit(doc, r, id);
        x += width;
        return r;
    };
    icon(app, ACT_ZOOM_OUT, button(ACT_ZOOM_OUT, w), alpha(p.text, 0.8f));
    D2D1_RECT_F pr = button(ACT_ZOOM_RESET, pctW);
    drawText(app, l, (pr.left + pr.right - m.width) * 0.5f, (pr.top + pr.bottom - m.height) * 0.5f, alpha(p.text, 0.85f));
    icon(app, ACT_ZOOM_IN, button(ACT_ZOOM_IN, w), alpha(p.text, 0.8f));
    icon(app, ACT_ZOOM_FIT, button(ACT_ZOOM_FIT, w), alpha(p.text, 0.8f));
    if (l) l->Release();
    return bar.left;
}

void drawMinimap(App& app, SheetDoc& doc, const Palette& p) {
    doc.minimapRect = {};
    if (!doc.showMinimap || doc.model.nodes.empty()) return;
    D2D1_RECT_F v = viewRect(app);
    float w = dpi(app, 208.0f), h = dpi(app, 136.0f);
    if (v.right - v.left < dpi(app, 760.0f) || v.bottom - v.top < dpi(app, 480.0f)) return;
    D2D1_RECT_F box = D2D1::RectF(v.right - w - dpi(app, 16.0f), v.bottom - h - dpi(app, 64.0f), v.right - dpi(app, 16.0f),
                                  v.bottom - dpi(app, 64.0f));
    shadow(app, box, dpi(app, 10.0f), p, 1.0f);
    fillRound(app, box, dpi(app, 10.0f), alpha(p.surface, 0.97f));
    strokeRound(app, box, dpi(app, 10.0f), p.surfaceBorder, dpi(app, 1.0f));
    doc.minimapRect = box;
    // What the minimap shows: every node and the viewport
    Box all = bounds(doc.model);
    D2D1_POINT_2F tl = toSheet(app, doc, v.left, v.top), br = toSheet(app, doc, v.right, v.bottom);
    all.left = std::min(all.left, tl.x);
    all.top = std::min(all.top, tl.y);
    all.right = std::max(all.right, br.x);
    all.bottom = std::max(all.bottom, br.y);
    float inset = dpi(app, 10.0f);
    float k = std::min((w - inset * 2) / std::max(1.0f, all.right - all.left), (h - inset * 2) / std::max(1.0f, all.bottom - all.top));
    float ox = box.left + (w - (all.right - all.left) * k) * 0.5f - all.left * k;
    float oy = box.top + (h - (all.bottom - all.top) * k) * 0.5f - all.top * k;
    doc.minimapScale = k;
    doc.minimapOriginX = ox;
    doc.minimapOriginY = oy;
    app.renderTarget->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (const Node& n : doc.model.nodes) {
        D2D1_RECT_F r = D2D1::RectF(ox + n.x * k, oy + n.y * k, ox + (n.x + n.width) * k, oy + (n.y + n.height) * k);
        bool has = false;
        D2D1_COLOR_F c = presetColor(app, n.color, &has);
        if (n.type == NodeType::Group) {
            strokeRound(app, r, dpi(app, 2.0f), alpha(has ? c : p.text, 0.35f), dpi(app, 1.0f));
        } else {
            fillRound(app, r, dpi(app, 1.5f), isSelected(doc, n.id) ? p.accent : alpha(has ? c : p.text, has ? 0.7f : 0.30f));
        }
    }
    D2D1_RECT_F vr = D2D1::RectF(ox + tl.x * k, oy + tl.y * k, ox + br.x * k, oy + br.y * k);
    fillRound(app, vr, dpi(app, 2.0f), alpha(p.accent, 0.10f));
    strokeRound(app, vr, dpi(app, 2.0f), alpha(p.accent, 0.85f), dpi(app, 1.25f));
    app.renderTarget->PopAxisAlignedClip();
}

void drawTray(App& app, SheetDoc& doc, const Palette& p) {
    doc.trayRect = {};
    doc.trayRows.clear();
    if (doc.unplaced.empty() || doc.presenting) return;
    D2D1_RECT_F v = viewRect(app);
    std::wstring label = trCount(app.currentLanguageIndex, "sheet.tray", (long long)doc.unplaced.size());
    IDWriteTextLayout* l = makeLayout(app, label, textFormat(app, uiFace(), dpi(app, 12.5f), DWRITE_FONT_WEIGHT_SEMI_BOLD, false), 600, 60);
    DWRITE_TEXT_METRICS m = metrics(l);
    D2D1_RECT_F chip = D2D1::RectF(v.left + dpi(app, 16.0f), v.top + dpi(app, 14.0f),
                                   v.left + dpi(app, 16.0f) + m.width + dpi(app, 38.0f), v.top + dpi(app, 14.0f) + dpi(app, 32.0f));
    shadow(app, chip, dpi(app, 16.0f), p, 1.0f);
    fillRound(app, chip, dpi(app, 16.0f), doc.trayOpen || doc.hoverUi == ACT_TRAY ? mix(p.surface, p.accent, 0.12f) : p.surface);
    strokeRound(app, chip, dpi(app, 16.0f), alpha(p.accent, 0.5f), dpi(app, 1.0f));
    // An inbox tray glyph
    float gx = chip.left + dpi(app, 16.0f), gy = (chip.top + chip.bottom) * 0.5f;
    float u = dpi(app, 1.0f);
    line(app, D2D1::Point2F(gx - 6 * u, gy), D2D1::Point2F(gx - 6 * u, gy + 5 * u), p.accent, 1.5f * u);
    line(app, D2D1::Point2F(gx - 6 * u, gy + 5 * u), D2D1::Point2F(gx + 6 * u, gy + 5 * u), p.accent, 1.5f * u);
    line(app, D2D1::Point2F(gx + 6 * u, gy + 5 * u), D2D1::Point2F(gx + 6 * u, gy), p.accent, 1.5f * u);
    line(app, D2D1::Point2F(gx, gy - 6 * u), D2D1::Point2F(gx, gy + 2 * u), p.accent, 1.5f * u);
    line(app, D2D1::Point2F(gx - 3 * u, gy - 1 * u), D2D1::Point2F(gx, gy + 2 * u), p.accent, 1.5f * u);
    line(app, D2D1::Point2F(gx + 3 * u, gy - 1 * u), D2D1::Point2F(gx, gy + 2 * u), p.accent, 1.5f * u);
    drawText(app, l, chip.left + dpi(app, 28.0f), (chip.top + chip.bottom - m.height) * 0.5f, p.text);
    if (l) l->Release();
    addHit(doc, chip, ACT_TRAY);
    if (!doc.trayOpen) return;
    // The list: drag a note onto the sheet, or place them all
    float rowH = dpi(app, 30.0f);
    int shown = std::min<int>((int)doc.unplaced.size(), 10);
    float listW = dpi(app, 300.0f);
    D2D1_RECT_F list = D2D1::RectF(chip.left, chip.bottom + dpi(app, 6.0f), chip.left + listW,
                                   chip.bottom + dpi(app, 6.0f) + rowH * shown + dpi(app, 52.0f));
    shadow(app, list, dpi(app, 10.0f), p, 1.2f);
    fillRound(app, list, dpi(app, 10.0f), p.surface);
    strokeRound(app, list, dpi(app, 10.0f), p.surfaceBorder, dpi(app, 1.0f));
    doc.trayRect = list;
    addHit(doc, list, -1);
    float y = list.top + dpi(app, 6.0f);
    for (int i = 0; i < shown; ++i) {
        const std::wstring& path = doc.unplaced[i];
        std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
        D2D1_RECT_F row = D2D1::RectF(list.left + dpi(app, 6.0f), y, list.right - dpi(app, 6.0f), y + rowH);
        if (doc.hoverUi == ACT_TRAY_ROW + i) fillRound(app, row, dpi(app, 6.0f), alpha(p.text, 0.07f));
        IDWriteTextLayout* rl = makeLayout(app, name, textFormat(app, uiFace(), dpi(app, 12.5f), DWRITE_FONT_WEIGHT_NORMAL, false),
                                           row.right - row.left - dpi(app, 30.0f), rowH);
        DWRITE_TEXT_METRICS rm = metrics(rl);
        strokeRound(app, D2D1::RectF(row.left + dpi(app, 8.0f), y + dpi(app, 8.0f), row.left + dpi(app, 18.0f), y + rowH - dpi(app, 8.0f)),
                    dpi(app, 2.0f), alpha(p.text, 0.5f), dpi(app, 1.0f));
        drawText(app, rl, row.left + dpi(app, 26.0f), y + (rowH - rm.height) * 0.5f, p.text);
        if (rl) rl->Release();
        addHit(doc, row, ACT_TRAY_ROW + i);
        doc.trayRows.push_back({row, path});
        y += rowH;
    }
    D2D1_RECT_F place = D2D1::RectF(list.left + dpi(app, 10.0f), list.bottom - dpi(app, 42.0f), list.right - dpi(app, 10.0f),
                                    list.bottom - dpi(app, 10.0f));
    fillRound(app, place, dpi(app, 7.0f), doc.hoverUi == ACT_PLACE_ALL ? mix(p.accent, D2D1::ColorF(1, 1, 1), 0.1f) : p.accent);
    IDWriteTextLayout* pl = makeLayout(app, tr(app, "sheet.place_all"), textFormat(app, uiFace(), dpi(app, 12.5f), DWRITE_FONT_WEIGHT_SEMI_BOLD, false),
                                       place.right - place.left, place.bottom - place.top);
    DWRITE_TEXT_METRICS pm = metrics(pl);
    float lum = 0.299f * p.accent.r + 0.587f * p.accent.g + 0.114f * p.accent.b;
    drawText(app, pl, (place.left + place.right - pm.width) * 0.5f, (place.top + place.bottom - pm.height) * 0.5f,
             lum > 0.5f ? D2D1::ColorF(0.05f, 0.06f, 0.1f) : D2D1::ColorF(1, 1, 1));
    if (pl) pl->Release();
    addHit(doc, place, ACT_PLACE_ALL);
}

void drawFind(App& app, SheetDoc& doc, const Palette& p) {
    doc.findRect = {};
    if (!doc.finding) return;
    D2D1_RECT_F v = viewRect(app);
    float w = std::min(dpi(app, 420.0f), v.right - v.left - dpi(app, 40.0f)), h = dpi(app, 38.0f);
    float cx = (v.left + v.right) * 0.5f;
    D2D1_RECT_F box = D2D1::RectF(cx - w * 0.5f, v.top + dpi(app, 14.0f), cx + w * 0.5f, v.top + dpi(app, 14.0f) + h);
    shadow(app, box, dpi(app, 10.0f), p, 1.2f);
    fillRound(app, box, dpi(app, 10.0f), p.surface);
    strokeRound(app, box, dpi(app, 10.0f), p.accent, dpi(app, 1.5f));
    doc.findRect = box;
    addHit(doc, box, -1);
    // Count and close on the right
    std::wstring count;
    if (!doc.edit.text.empty()) {
        if (doc.found.empty()) count = tr(app, "sheet.find_none");
        else count = std::to_wstring(doc.foundIndex + 1) + L" / " + std::to_wstring(doc.found.size());
    }
    IDWriteTextLayout* cl = makeLayout(app, count, textFormat(app, uiFace(), dpi(app, 12.0f), DWRITE_FONT_WEIGHT_NORMAL, false), 200, h);
    DWRITE_TEXT_METRICS cm = metrics(cl);
    D2D1_RECT_F close = D2D1::RectF(box.right - h, box.top, box.right, box.bottom);
    icon(app, ACT_FIND_CLOSE, close, alpha(p.text, doc.hoverUi == ACT_FIND_CLOSE ? 0.95f : 0.6f));
    addHit(doc, close, ACT_FIND_CLOSE);
    drawText(app, cl, close.left - cm.width - dpi(app, 4.0f), (box.top + box.bottom - cm.height) * 0.5f, alpha(p.text, 0.55f));
    if (cl) cl->Release();
    // The field: its text, caret and placeholder
    D2D1_RECT_F field = D2D1::RectF(box.left + dpi(app, 14.0f), box.top, close.left - cm.width - dpi(app, 12.0f), box.bottom);
    IDWriteTextFormat* fmt = textFormat(app, uiFace(), dpi(app, 14.0f), DWRITE_FONT_WEIGHT_NORMAL, false);
    std::wstring shown = doc.edit.text.empty() ? std::wstring(tr(app, "sheet.find_placeholder")) : doc.edit.text;
    IDWriteTextLayout* fl = makeLayout(app, shown, fmt, 4000, h);
    DWRITE_TEXT_METRICS fm = metrics(fl);
    float ty = (box.top + box.bottom - fm.height) * 0.5f;
    app.renderTarget->PushAxisAlignedClip(field, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (doc.edit.hasSelection() && !doc.edit.text.empty() && fl) {
        float x0 = 0, x1 = 0, yy = 0;
        DWRITE_HIT_TEST_METRICS hm{};
        fl->HitTestTextPosition((UINT32)doc.edit.selStart(), FALSE, &x0, &yy, &hm);
        fl->HitTestTextPosition((UINT32)doc.edit.selEnd(), FALSE, &x1, &yy, &hm);
        app.brush->SetColor(alpha(p.accent, 0.3f));
        app.renderTarget->FillRectangle(D2D1::RectF(field.left + x0, ty, field.left + x1, ty + fm.height), app.brush);
    }
    drawText(app, fl, field.left, ty, doc.edit.text.empty() ? alpha(p.text, 0.4f) : p.text);
    if (app.cursorBlinkOn && fl) {
        float cx2 = 0, cy2 = 0;
        DWRITE_HIT_TEST_METRICS hm{};
        fl->HitTestTextPosition(doc.edit.text.empty() ? 0 : (UINT32)doc.edit.caret, FALSE, &cx2, &cy2, &hm);
        app.brush->SetColor(p.accent);
        app.renderTarget->FillRectangle(D2D1::RectF(field.left + cx2, ty, field.left + cx2 + dpi(app, 1.5f), ty + fm.height), app.brush);
    }
    app.renderTarget->PopAxisAlignedClip();
    if (fl) fl->Release();
}

void drawPresentation(App& app, SheetDoc& doc, const Palette& p) {
    D2D1_RECT_F v = viewRect(app);
    float h = dpi(app, 36.0f);
    std::wstring count = std::to_wstring(doc.slide + 1) + L" / " + std::to_wstring(doc.slides.size());
    IDWriteTextLayout* l = makeLayout(app, count, textFormat(app, uiFace(), dpi(app, 13.0f), DWRITE_FONT_WEIGHT_SEMI_BOLD, false), 200, h);
    DWRITE_TEXT_METRICS m = metrics(l);
    float w = h * 3 + m.width + dpi(app, 24.0f);
    float cx = (v.left + v.right) * 0.5f;
    D2D1_RECT_F bar = D2D1::RectF(cx - w * 0.5f, v.bottom - h - dpi(app, 24.0f), cx + w * 0.5f, v.bottom - dpi(app, 24.0f));
    fillRound(app, bar, h * 0.5f, alpha(p.surface, 0.94f));
    strokeRound(app, bar, h * 0.5f, p.surfaceBorder, dpi(app, 1.0f));
    addHit(doc, bar, -1);
    D2D1_RECT_F prev = D2D1::RectF(bar.left + dpi(app, 4.0f), bar.top, bar.left + dpi(app, 4.0f) + h, bar.bottom);
    D2D1_RECT_F next = D2D1::RectF(prev.right + m.width + dpi(app, 16.0f), bar.top, prev.right + m.width + dpi(app, 16.0f) + h, bar.bottom);
    D2D1_RECT_F exit = D2D1::RectF(next.right, bar.top, next.right + h, bar.bottom);
    for (auto [r, id] : {std::make_pair(prev, (int)ACT_PRESENT_PREV), std::make_pair(next, (int)ACT_PRESENT_NEXT),
                         std::make_pair(exit, (int)ACT_PRESENT_EXIT)}) {
        if (doc.hoverUi == id) fillRound(app, r, h * 0.5f, alpha(p.text, 0.08f));
        icon(app, id, r, alpha(p.text, 0.85f));
        addHit(doc, r, id);
    }
    drawText(app, l, prev.right + dpi(app, 8.0f), (bar.top + bar.bottom - m.height) * 0.5f, p.text);
    if (l) l->Release();
}

void drawMenu(App& app, SheetDoc& doc, const Palette& p) {
    doc.menuHits.clear();
    if (!doc.menuOpen) return;
    float rowH = dpi(app, 30.0f), sepH = dpi(app, 9.0f), swatchH = dpi(app, 36.0f), w = dpi(app, 248.0f);
    float h = dpi(app, 8.0f);
    for (const auto& item : doc.menu) h += item.separator ? sepH : item.swatches ? swatchH : rowH;
    float x = std::min(doc.menuX, (float)app.width - w - dpi(app, 8.0f));
    float y = std::min(doc.menuY, (float)app.height - h - dpi(app, 8.0f));
    x = std::max(x, dpi(app, 4.0f));
    y = std::max(y, chromeTopHeight(app) + dpi(app, 4.0f));
    D2D1_RECT_F box = D2D1::RectF(x, y, x + w, y + h);
    shadow(app, box, dpi(app, 9.0f), p, 1.6f);
    fillRound(app, box, dpi(app, 9.0f), p.surface);
    strokeRound(app, box, dpi(app, 9.0f), p.surfaceBorder, dpi(app, 1.0f));
    float ty = y + dpi(app, 4.0f);
    IDWriteTextFormat* fmt = textFormat(app, uiFace(), dpi(app, 13.0f), DWRITE_FONT_WEIGHT_NORMAL, false);
    IDWriteTextFormat* keyFmt = textFormat(app, uiFace(), dpi(app, 11.5f), DWRITE_FONT_WEIGHT_NORMAL, false);
    for (int i = 0; i < (int)doc.menu.size(); ++i) {
        const auto& item = doc.menu[i];
        if (item.separator) {
            line(app, D2D1::Point2F(x + dpi(app, 10.0f), ty + sepH * 0.5f), D2D1::Point2F(x + w - dpi(app, 10.0f), ty + sepH * 0.5f),
                 alpha(p.text, 0.12f), dpi(app, 1.0f));
            ty += sepH;
            continue;
        }
        if (item.swatches) {
            float sx = x + dpi(app, 14.0f);
            for (int k = 0; k <= 6; ++k) {
                float rr = dpi(app, 9.0f);
                D2D1_POINT_2F c = D2D1::Point2F(sx + rr, ty + swatchH * 0.5f);
                D2D1_RECT_F hit = D2D1::RectF(c.x - rr - 2, c.y - rr - 2, c.x + rr + 2, c.y + rr + 2);
                bool hov = doc.menuHover == 1000 + k;
                if (k == 0) {
                    app.brush->SetColor(alpha(p.text, hov ? 0.8f : 0.45f));
                    app.renderTarget->DrawEllipse(D2D1::Ellipse(c, rr - 1, rr - 1), app.brush, dpi(app, 1.25f));
                    line(app, D2D1::Point2F(c.x - rr * 0.6f, c.y + rr * 0.6f), D2D1::Point2F(c.x + rr * 0.6f, c.y - rr * 0.6f),
                         alpha(p.text, hov ? 0.8f : 0.45f), dpi(app, 1.25f));
                } else {
                    std::string preset(1, (char)('0' + k));
                    app.brush->SetColor(presetColor(app, preset));
                    app.renderTarget->FillEllipse(D2D1::Ellipse(c, hov ? rr : rr - 1.5f, hov ? rr : rr - 1.5f), app.brush);
                }
                doc.menuHits.push_back({hit, 1000 + k});
                sx += rr * 2 + dpi(app, 7.0f);
            }
            ty += swatchH;
            continue;
        }
        D2D1_RECT_F row = D2D1::RectF(x + dpi(app, 4.0f), ty, x + w - dpi(app, 4.0f), ty + rowH);
        if (doc.menuHover == i && item.enabled) fillRound(app, row, dpi(app, 6.0f), alpha(p.accent, 0.16f));
        IDWriteTextLayout* l = makeLayout(app, item.label, fmt, w - dpi(app, 90.0f), rowH);
        DWRITE_TEXT_METRICS m = metrics(l);
        drawText(app, l, row.left + dpi(app, 10.0f), ty + (rowH - m.height) * 0.5f, alpha(p.text, item.enabled ? 1.0f : 0.38f));
        if (l) l->Release();
        if (!item.keys.empty()) {
            IDWriteTextLayout* k = makeLayout(app, item.keys, keyFmt, 200, rowH);
            DWRITE_TEXT_METRICS km = metrics(k);
            drawText(app, k, row.right - km.width - dpi(app, 10.0f), ty + (rowH - km.height) * 0.5f, alpha(p.text, 0.45f));
            if (k) k->Release();
        }
        doc.menuHits.push_back({row, i});
        ty += rowH;
    }
}

// Text that reads on the accent: dark on a light accent, else white
D2D1_COLOR_F onAccent(const Palette& p) {
    float lum = 0.299f * p.accent.r + 0.587f * p.accent.g + 0.114f * p.accent.b;
    return lum > 0.55f ? D2D1::ColorF(0.07f, 0.08f, 0.1f) : D2D1::ColorF(1, 1, 1);
}

// --- Folds, the selection bar, jump labels, [[ names ---------------------

// A folded card's +N on its right, joined to it; a click unfolds it
void drawFoldBadges(App& app, SheetDoc& doc, const Palette& p) {
    float s = scale(app, doc);
    D2D1_RECT_F v = viewRect(app);
    for (int i = 0; i < (int)doc.model.nodes.size(); ++i) {
        const Node& n = doc.model.nodes[i];
        if (!n.folded || n.type == NodeType::Group || foldedAwayAt(doc, i)) continue;
        int count = foldedCount(doc.model, i, doc.hidden);
        if (count <= 0) continue;
        D2D1_RECT_F r = screenRect(app, doc, drawBox(doc, n));
        float px = std::clamp(12.0f * s, dpi(app, 9.0f), dpi(app, 13.0f));
        IDWriteTextLayout* l = makeLayout(app, L"+" + std::to_wstring(count),
                                          textFormat(app, uiFace(), px, DWRITE_FONT_WEIGHT_SEMI_BOLD, false), 400, px * 2);
        DWRITE_TEXT_METRICS m = metrics(l);
        float h = m.height + dpi(app, 6.0f), w = std::max(h, m.width + dpi(app, 14.0f));
        float mid = (r.top + r.bottom) * 0.5f;
        D2D1_RECT_F pill = D2D1::RectF(r.right + dpi(app, 12.0f), mid - h * 0.5f, r.right + dpi(app, 12.0f) + w, mid + h * 0.5f);
        if (intersects(pill, v)) {
            line(app, D2D1::Point2F(r.right, mid), D2D1::Point2F(pill.left, mid), alpha(p.accent, 0.7f), dpi(app, 1.5f));
            fillRound(app, pill, h * 0.5f, p.accent);
            drawText(app, l, pill.left + (w - m.width) * 0.5f, pill.top + (h - m.height) * 0.5f, onAccent(p));
            doc.foldHits.push_back({pill, n.id});
        }
        if (l) l->Release();
    }
}

// Over the selected cards: their colour, Group, Join in order and Align for
// several, Fold for a card with arrows out, and Remove
void drawSelectionBar(App& app, SheetDoc& doc, const Palette& p) {
    doc.selectionBar = {};
    if (doc.selected.empty() || doc.presenting || doc.menuOpen || doc.jumping || doc.editing != SheetDoc::Edit::None ||
        (doc.gesture != SheetDoc::Gesture::None && doc.gesture != SheetDoc::Gesture::Pending)) {
        return;
    }
    std::vector<int> picked = selectedIndexes(doc);
    if (picked.empty()) return;
    bool many = picked.size() > 1;
    const Node& first = doc.model.nodes[picked[0]];
    bool leaves = std::any_of(doc.model.edges.begin(), doc.model.edges.end(),
                              [&](const Edge& e) { return e.fromNode == first.id; });
    std::string color = first.color;
    for (int i : picked) {
        if (doc.model.nodes[i].color != color) color = "*";
    }
    std::vector<int> ids;
    for (int c = 1; c <= 6; ++c) ids.push_back(ACT_COLOR_BASE + c);
    ids.push_back(ACT_COLOR_BASE);
    ids.push_back(0);
    ids.push_back(ACT_GROUP_SELECTION);
    if (many) {
        ids.push_back(ACT_CHAIN);
        ids.push_back(ACT_ALIGN_MENU);
    } else if (first.type != NodeType::Group && (first.folded || leaves)) {
        ids.push_back(ACT_FOLD);
    }
    ids.push_back(ACT_REMOVE);
    float swatch = dpi(app, 22.0f), size = dpi(app, 30.0f), sep = dpi(app, 11.0f), pad = dpi(app, 5.0f);
    float total = 0;
    for (int id : ids) total += id == 0 ? sep : id >= ACT_COLOR_BASE && id <= ACT_COLOR_BASE + 6 ? swatch : size;
    float h = size + pad * 2, w = total + pad * 2;
    D2D1_RECT_F v = viewRect(app);
    D2D1_RECT_F sel = screenRect(app, doc, bounds(doc.model, picked));
    if (!intersects(sel, v)) return;  // nothing selected in view, nothing to stand over
    // Above the cards, else below them, else inside the top of the view
    float top = sel.top - dpi(app, 34.0f) - h;
    if (top < v.top + dpi(app, 8.0f)) top = sel.bottom + dpi(app, 14.0f);
    if (top + h > v.bottom - dpi(app, 70.0f)) top = v.top + dpi(app, 8.0f);
    float left = std::clamp((sel.left + sel.right) * 0.5f - w * 0.5f, v.left + dpi(app, 8.0f),
                            std::max(v.left + dpi(app, 8.0f), v.right - dpi(app, 8.0f) - w));
    D2D1_RECT_F bar = D2D1::RectF(left, top, left + w, top + h);
    shadow(app, bar, dpi(app, 10.0f), p, 1.2f);
    fillRound(app, bar, dpi(app, 10.0f), p.surface);
    strokeRound(app, bar, dpi(app, 10.0f), p.surfaceBorder, dpi(app, 1.0f));
    addHit(doc, bar, -1);
    doc.selectionBar = bar;
    float x = bar.left + pad, y = bar.top + pad;
    float tipX = 0;
    for (int id : ids) {
        if (id == 0) {
            line(app, D2D1::Point2F(x + sep * 0.5f, y + dpi(app, 6.0f)), D2D1::Point2F(x + sep * 0.5f, y + size - dpi(app, 6.0f)),
                 alpha(p.text, 0.15f), dpi(app, 1.0f));
            x += sep;
            continue;
        }
        bool isSwatch = id >= ACT_COLOR_BASE && id <= ACT_COLOR_BASE + 6;
        float slot = isSwatch ? swatch : size;
        D2D1_RECT_F r = D2D1::RectF(x, y, x + slot, y + size);
        bool hover = doc.hoverUi == id;
        if (hover) tipX = (r.left + r.right) * 0.5f;
        if (isSwatch) {
            int preset = id - ACT_COLOR_BASE;
            D2D1_POINT_2F c = D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f);
            float rad = dpi(app, hover ? 7.5f : 6.5f);
            if (preset > 0) {
                app.brush->SetColor(presetColor(app, std::string(1, (char)('0' + preset))));
                app.renderTarget->FillEllipse(D2D1::Ellipse(c, rad, rad), app.brush);
            } else {
                // No colour: a ring struck through
                app.brush->SetColor(alpha(p.text, 0.55f));
                app.renderTarget->DrawEllipse(D2D1::Ellipse(c, rad, rad), app.brush, dpi(app, 1.2f));
                line(app, D2D1::Point2F(c.x - rad * 0.7f, c.y + rad * 0.7f), D2D1::Point2F(c.x + rad * 0.7f, c.y - rad * 0.7f),
                     alpha(p.text, 0.55f), dpi(app, 1.2f));
            }
            bool current = color != "*" && ((preset == 0 && color.empty()) || color == std::string(1, (char)('0' + preset)));
            if (current) {
                app.brush->SetColor(p.text);
                app.renderTarget->DrawEllipse(D2D1::Ellipse(c, rad + dpi(app, 2.5f), rad + dpi(app, 2.5f)), app.brush,
                                              dpi(app, 1.25f));
            }
        } else {
            if (hover) fillRound(app, r, dpi(app, 7.0f), alpha(p.text, 0.08f));
            int shape = id == ACT_FOLD && first.folded ? ACT_UNFOLD_ALL : id;
            icon(app, shape, r, alpha(p.text, hover ? 0.95f : 0.75f));
        }
        addHit(doc, r, id);
        x += slot;
    }
    if (tipX > 0) drawTip(app, doc, p, tipX, bar.top);
}

// J: a label on each card in view; what was typed leaves the labels it ruled out
void drawJumpLabels(App& app, SheetDoc& doc, const Palette& p) {
    if (!doc.jumping) return;
    D2D1_RECT_F v = viewRect(app);
    app.brush->SetColor(alpha(p.canvas, 0.35f));
    app.renderTarget->FillRectangle(v, app.brush);
    float px = dpi(app, 13.0f);
    for (const auto& label : doc.jumpLabels) {
        if (label.first.compare(0, doc.jumpTyped.size(), doc.jumpTyped) != 0) continue;
        int i = nodeIndex(doc.model, label.second);
        if (i < 0) continue;
        D2D1_RECT_F r = screenRect(app, doc, drawBox(doc, doc.model.nodes[i]));
        std::wstring rest = label.first.substr(doc.jumpTyped.size());
        for (auto& ch : rest) ch = (wchar_t)towupper(ch);
        IDWriteTextLayout* l = makeLayout(app, rest, textFormat(app, uiFace(), px, DWRITE_FONT_WEIGHT_BOLD, false), 200, px * 2);
        DWRITE_TEXT_METRICS m = metrics(l);
        float h = m.height + dpi(app, 6.0f), w = std::max(h, m.width + dpi(app, 12.0f));
        float x = std::max(v.left + dpi(app, 4.0f), r.left + dpi(app, 6.0f));
        float y = std::max(v.top + dpi(app, 4.0f), r.top + dpi(app, 6.0f));
        D2D1_RECT_F pill = D2D1::RectF(x, y, x + w, y + h);
        shadow(app, pill, dpi(app, 5.0f), p, 1.0f);
        fillRound(app, pill, dpi(app, 5.0f), p.accent);
        drawText(app, l, x + (w - m.width) * 0.5f, y + (h - m.height) * 0.5f, onAccent(p));
        if (l) l->Release();
    }
}

// Over a card being typed in: the slash menu or the [[ names under the
// caret, or the format bar over a selection
void drawEditPopups(App& app, SheetDoc& doc) {
    doc.edit.popupHits.clear();
    doc.edit.barHits.clear();
    if (doc.editing != SheetDoc::Edit::Card) return;
    D2D1_RECT_F rect{};
    float sc = 1;
    if (!editRect(app, doc, rect, sc)) return;
    IDWriteTextLayout* layout = editLayout(app, doc, (rect.right - rect.left) / sc);
    if (!layout) return;
    auto place = [&](size_t at, float& x, float& top, float& bottom) {
        float cx = 0, cy = 0, h = 0;
        if (!live::caretPoint(layout, 0, std::min(at, doc.edit.text.size()), cx, cy, h)) return false;
        x = rect.left + cx * sc;
        top = rect.top + (cy - doc.edit.scrollY) * sc;
        bottom = top + h * sc;
        return true;
    };
    D2D1_RECT_F v = viewRect(app);
    float x = 0, top = 0, bottom = 0;
    if (doc.edit.popup != LiveText::Popup::None) {
        if (place(doc.edit.popupFrom, x, top, bottom)) live::drawPopup(app, doc.edit, D2D1::Point2F(x, top), bottom, v);
    } else if (doc.edit.hasSelection() && place(doc.edit.selStart(), x, top, bottom)) {
        float x2 = 0, top2 = 0, bottom2 = 0;
        place(doc.edit.selEnd(), x2, top2, bottom2);
        live::drawFormatBar(app, doc.edit, D2D1::Point2F(x, top), bottom2, v);
    }
}

// --- The hint line -------------------------------------------------------

// What the hint line says now: the next thing that can be done, until it
// has been done a few times; jumping and the [[ list always say theirs
std::wstring contextHint(App& app, SheetDoc& doc) {
    if (doc.presenting) return {};
    if (doc.jumping) return tr(app, doc.jumpJoin ? "sheet.hint.join" : "sheet.hint.jump");
    if (doc.editing == SheetDoc::Edit::Card && doc.edit.popup == LiveText::Popup::Names) return tr(app, "sheet.hint.complete");
    auto say = [&](int which, const char* key) { return hintRetired(which) ? std::wstring() : std::wstring(tr(app, key)); };
    switch (doc.gesture) {
        case SheetDoc::Gesture::Connect: return say(HINT_CONNECT, "sheet.hint.connect");
        case SheetDoc::Gesture::Marquee: return doc.gestureMoved ? say(HINT_MARQUEE, "sheet.hint.marquee") : std::wstring();
        case SheetDoc::Gesture::None:
        case SheetDoc::Gesture::Pending: break;
        default: return {};
    }
    if (doc.editing == SheetDoc::Edit::Card) {
        // A line that starts #word is not a heading yet
        const std::wstring& t = doc.edit.text;
        size_t caret = std::min(doc.edit.caret, t.size());
        size_t start = caret == 0 ? 0 : t.rfind(L'\n', caret - 1);
        start = start == std::wstring::npos || caret == 0 ? 0 : start + 1;
        size_t hashes = 0;
        while (start + hashes < t.size() && t[start + hashes] == L'#') ++hashes;
        if (hashes >= 1 && hashes <= 6 && start + hashes < t.size() && t[start + hashes] != L' ' && t[start + hashes] != L'\n' &&
            hintsShown()) {
            return tr(app, "sheet.hint.heading");
        }
        int i = nodeIndex(doc.model, doc.editId);
        if (i >= 0 && doc.model.nodes[i].type == NodeType::File) {
            std::wstring path = absolutePath(doc, doc.model.nodes[i].file);
            size_t slash = path.find_last_of(L"\\/");
            std::wstring name = path.substr(slash == std::wstring::npos ? 0 : slash + 1);
            size_t dot = name.find_last_of(L'.');
            if (placeholderName(app, dot == std::wstring::npos ? name : name.substr(0, dot)))
                return say(HINT_NAMING, "sheet.hint.naming");
        }
        return say(HINT_LINK, "sheet.hint.link");
    }
    if (doc.editing != SheetDoc::Edit::None) return {};
    if (doc.selected.size() >= 2) return say(HINT_MULTI, "sheet.hint.multi");
    if (doc.selected.size() == 1) {
        int i = nodeIndex(doc.model, doc.selected[0]);
        if (i < 0) return {};
        const Node& n = doc.model.nodes[i];
        if (n.type == NodeType::Group) return n.laneKey.empty() ? std::wstring() : say(HINT_LANE, "sheet.hint.lane");
        return say(HINT_SINGLE, "sheet.hint.single");
    }
    if (!doc.model.nodes.empty() && scale(app, doc) < sheetDetailThreshold()) return say(HINT_FAR, "sheet.hint.far");
    return {};
}

// A quiet line above the tool bar
void drawHintLine(App& app, SheetDoc& doc, const Palette& p) {
    doc.hintText = contextHint(app, doc);
    if (doc.hintText.empty()) return;
    D2D1_RECT_F v = viewRect(app);
    float maxW = std::max(dpi(app, 120.0f), std::min(dpi(app, 720.0f), v.right - v.left - dpi(app, 40.0f)));
    IDWriteTextLayout* l = makeLayout(app, doc.hintText, textFormat(app, uiFace(), dpi(app, 12.0f), DWRITE_FONT_WEIGHT_NORMAL),
                                      maxW, dpi(app, 100.0f));
    if (!l) return;
    l->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    DWRITE_TEXT_METRICS m = metrics(l);
    // The tool bar's top, as drawToolbar places it
    float toolbarTop = v.bottom - dpi(app, 16.0f) - dpi(app, 36.0f) - dpi(app, 8.0f) - dpi(app, 4.0f);
    float cx = (v.left + v.right) * 0.5f;
    D2D1_RECT_F pill = D2D1::RectF(cx - m.width * 0.5f - dpi(app, 12.0f), toolbarTop - dpi(app, 10.0f) - m.height - dpi(app, 8.0f),
                                   cx + m.width * 0.5f + dpi(app, 12.0f), toolbarTop - dpi(app, 10.0f));
    fillRound(app, pill, dpi(app, 9.0f), alpha(p.surface, 0.92f));
    drawText(app, l, cx - maxW * 0.5f, pill.top + dpi(app, 4.0f), alpha(p.text, 0.72f));
    l->Release();
}

// --- Peek ----------------------------------------------------------------

// A card seen from far, at the screen's own scale beside it
void drawPeek(App& app, SheetDoc& doc, const Palette& p) {
    doc.peekRect = {};
    if (doc.peekId.empty() || !doc.peekBitmap || doc.peekBitmapId != doc.peekId ||
        doc.peekBitmapGeneration != app.renderTargetGeneration || scale(app, doc) >= sheetDetailThreshold()) {
        return;
    }
    int i = nodeIndex(doc.model, doc.peekId);
    if (i < 0 || foldedAwayAt(doc, i)) return;
    const Node& n = doc.model.nodes[i];
    D2D1_RECT_F r = screenRect(app, doc, drawBox(doc, n));
    D2D1_RECT_F v = viewRect(app);
    float s = app.contentScale;
    float pad = cardPadding() * s;
    float innerW = std::max(40.0f, n.width - cardPadding() * 2);
    float shownH = std::min(doc.peekBitmapHeight, doc.peekContentHeight);
    float w = innerW * s + pad * 2;
    float h = std::min((v.bottom - v.top) * 0.7f, shownH * s + pad * 2);
    float x = r.right + dpi(app, 14.0f);
    if (x + w > v.right - dpi(app, 8.0f)) x = r.left - dpi(app, 14.0f) - w;
    x = std::clamp(x, v.left + dpi(app, 8.0f), std::max(v.left + dpi(app, 8.0f), v.right - dpi(app, 8.0f) - w));
    float y = std::clamp(r.top, v.top + dpi(app, 8.0f), std::max(v.top + dpi(app, 8.0f), v.bottom - dpi(app, 8.0f) - h));
    D2D1_RECT_F lens = D2D1::RectF(x, y, x + w, y + h);
    float radius = dpi(app, 12.0f);
    shadow(app, lens, radius, p, 1.8f);
    fillRound(app, lens, radius, cardFill(app, n, p));
    strokeRound(app, lens, radius, alpha(p.accent, 0.55f), dpi(app, 1.5f));
    float visible = (h - pad * 2) / s;
    doc.peekScroll = std::clamp(doc.peekScroll, 0.0f, std::max(0.0f, shownH - visible));
    D2D1_RECT_F inner = D2D1::RectF(lens.left + pad, lens.top + pad, lens.right - pad, lens.bottom - pad);
    D2D1_RECT_F src = D2D1::RectF(0, doc.peekScroll, innerW, doc.peekScroll + std::min(visible, shownH));
    D2D1_RECT_F dest = D2D1::RectF(inner.left, inner.top, inner.left + innerW * s, inner.top + (src.bottom - src.top) * s);
    app.renderTarget->PushAxisAlignedClip(inner, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    app.renderTarget->DrawBitmap(doc.peekBitmap.Get(), dest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
    app.renderTarget->PopAxisAlignedClip();
    if (shownH > visible + 1.0f) {
        // How far into the note the lens reads
        float trackTop = lens.top + radius, trackBottom = lens.bottom - radius;
        float thumbH = std::max(dpi(app, 14.0f), (trackBottom - trackTop) * visible / shownH);
        float ty = trackTop + (trackBottom - trackTop - thumbH) * (doc.peekScroll / std::max(1.0f, shownH - visible));
        fillRound(app, D2D1::RectF(lens.right - dpi(app, 6.0f), ty, lens.right - dpi(app, 3.0f), ty + thumbH), dpi(app, 1.5f),
                  alpha(p.text, 0.3f));
    }
    doc.peekRect = lens;
}

void drawEmptyHint(App& app, SheetDoc& doc, const Palette& p) {
    if (!doc.model.nodes.empty() || doc.editing != SheetDoc::Edit::None) return;
    D2D1_RECT_F v = viewRect(app);
    IDWriteTextLayout* t = makeLayout(app, tr(app, "sheet.empty_title"), textFormat(app, docFace(app), dpi(app, 22.0f), DWRITE_FONT_WEIGHT_SEMI_BOLD, false), 1000, 100);
    IDWriteTextLayout* d = makeLayout(app, tr(app, "sheet.empty_body"), textFormat(app, uiFace(), dpi(app, 13.5f), DWRITE_FONT_WEIGHT_NORMAL), dpi(app, 460.0f), 300);
    DWRITE_TEXT_METRICS tm = metrics(t), dm = metrics(d);
    if (d) d->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    float cx = (v.left + v.right) * 0.5f, cy = (v.top + v.bottom) * 0.5f - dpi(app, 40.0f);
    drawText(app, t, cx - tm.width * 0.5f, cy - tm.height, alpha(p.heading, 0.85f));
    drawText(app, d, cx - dpi(app, 230.0f), cy + dpi(app, 10.0f), alpha(p.text, 0.55f));
    (void)dm;
    if (t) t->Release();
    if (d) d->Release();
}

}  // namespace

namespace sheetui {

Box groupLabelBox(App& app, const SheetDoc& doc, const Node& g) {
    (void)app;
    (void)doc;
    float h = 34.0f;
    return {g.x + 16.0f, g.y + 12.0f, g.x + std::max(60.0f, std::min(g.width - 16.0f, 16.0f + 16.0f * std::max<size_t>(4, g.label.size()) * 0.62f)),
            g.y + 12.0f + h};
}

}  // namespace sheetui

void renderSheet(App& app) {
    SheetDoc* docPtr = activeSheet(app);
    if (!docPtr || !app.renderTarget || !app.brush) return;
    SheetDoc& doc = *docPtr;
    Palette p = palette(app);
    doc.uiHits.clear();
    doc.foldHits.clear();
    if (doc.hidden.size() != doc.model.nodes.size()) updateFolds(doc);
    bool anyHidden = std::find(doc.hidden.begin(), doc.hidden.end(), 1) != doc.hidden.end();
    auto away = [&](const std::string& id) { return anyHidden && foldedAwayAt(doc, nodeIndex(doc.model, id)); };
    D2D1_RECT_F v = viewRect(app);
    app.brush->SetColor(p.canvas);
    app.renderTarget->FillRectangle(v, app.brush);
    app.renderTarget->PushAxisAlignedClip(v, D2D1_ANTIALIAS_MODE_ALIASED);
    drawGrid(app, doc, p);

    // Who is dimmed: everything outside the spotlight, the slide or the finds
    std::set<std::string> lit;
    bool dimming = false;
    if (doc.presenting && doc.slide >= 0 && doc.slide < (int)doc.slides.size()) {
        dimming = true;
        const std::string& id = doc.slides[doc.slide];
        lit.insert(id);
        int gi = nodeIndex(doc.model, id);
        if (gi >= 0 && doc.model.nodes[gi].type == NodeType::Group) {
            for (int i = 0; i < (int)doc.model.nodes.size(); ++i) {
                if (insideGroup(doc.model.nodes[gi], doc.model.nodes[i])) lit.insert(doc.model.nodes[i].id);
            }
        }
    } else if (doc.spotlight) {
        dimming = true;
        lit.insert(doc.spotlightId);
        for (const Edge& e : doc.model.edges) {
            if (e.fromNode == doc.spotlightId) lit.insert(e.toNode);
            if (e.toNode == doc.spotlightId) lit.insert(e.fromNode);
        }
        for (const auto& g : ghostLinks(app, doc)) {
            if (g.first == doc.spotlightId) lit.insert(g.second);
            if (g.second == doc.spotlightId) lit.insert(g.first);
        }
    } else if (doc.finding && !doc.edit.text.empty()) {
        dimming = true;
        lit.insert(doc.found.begin(), doc.found.end());
    }
    auto fadeOf = [&](const std::string& id) { return !dimming || lit.count(id) ? 1.0f : 0.22f; };

    // Groups underneath everything
    for (const Node& n : doc.model.nodes) {
        if (n.type == NodeType::Group) drawGroup(app, doc, n, p, fadeOf(n.id) * (dimming && !lit.count(n.id) ? 0.6f : 1.0f));
    }
    // Links written in the notes, as dashed ghosts
    if (doc.showGhosts && !doc.presenting) {
        std::vector<std::pair<std::string, std::string>> ghosts = ghostLinks(app, doc);
        for (const auto& g : ghosts) {
            if (!ghostShown(doc, ghosts.size(), g)) continue;
            int a = nodeIndex(doc.model, g.first), b = nodeIndex(doc.model, g.second);
            if (a < 0 || b < 0 || foldedAwayAt(doc, a) || foldedAwayAt(doc, b)) continue;
            Side fs, ts;
            edgeSides(doc.model.nodes[a], doc.model.nodes[b], Side::None, Side::None, fs, ts);
            Curve c = edgeCurve(drawBox(doc, doc.model.nodes[a]), drawBox(doc, doc.model.nodes[b]), fs, ts);
            bool hover = doc.hoverGhostFrom == g.first && doc.hoverGhostTo == g.second;
            float f = std::min(fadeOf(g.first), fadeOf(g.second));
            drawCurve(app, doc, c, alpha(hover ? p.accent : p.text, (hover ? 0.85f : 0.30f) * f),
                      hover ? dpi(app, 2.0f) : dpi(app, 1.4f), true, false, true);
            if (hover) {
                D2D1_POINT_2F mid = bezier(c, 0.5f);
                D2D1_POINT_2F q = toScreen(app, doc, mid.x, mid.y);
                doc.tip = tr(app, "sheet.ghost_tip");
                drawTip(app, doc, p, q.x, q.y - dpi(app, 6.0f));
                doc.tip.clear();
            }
        }
    }
    // Arrows
    for (const Edge& e : doc.model.edges) {
        if (away(e.fromNode) || away(e.toNode)) continue;
        Curve c = edgeCurveFor(doc, e);
        if (c.p0.x == 0 && c.p3.x == 0 && c.p0.y == 0 && c.p3.y == 0) continue;
        bool has = false;
        D2D1_COLOR_F col = presetColor(app, e.color, &has);
        bool selected = doc.selectedEdge == e.id;
        bool hover = doc.hoverEdge == e.id;
        float f = std::min(fadeOf(e.fromNode), fadeOf(e.toNode));
        D2D1_COLOR_F stroke = selected ? p.accent : has ? col : alpha(p.text, 0.55f);
        if (hover && !selected) stroke = mix(stroke, p.accent, 0.5f);
        float width = std::clamp(2.0f * scale(app, doc), dpi(app, 1.25f), dpi(app, 3.5f)) + (selected ? dpi(app, 0.75f) : 0);
        drawCurve(app, doc, c, alpha(stroke, f), width, e.toArrow, e.fromArrow, false);
        if (!(doc.editing == SheetDoc::Edit::EdgeLabel && doc.editId == e.id))
            drawEdgeLabel(app, doc, c, e.label, p, f, selected);
    }
    // Cards, the selected ones last so a drag stays on top
    std::set<std::string> foundSet(doc.found.begin(), doc.found.end());
    std::string current = doc.foundIndex >= 0 && doc.foundIndex < (int)doc.found.size() ? doc.found[doc.foundIndex] : std::string();
    for (int pass = 0; pass < 2; ++pass) {
        for (const Node& n : doc.model.nodes) {
            if (n.type == NodeType::Group || (anyHidden && foldedAwayAt(doc, (int)(&n - doc.model.nodes.data())))) continue;
            if ((pass == 1) != isSelected(doc, n.id)) continue;
            drawCard(app, doc, n, p, fadeOf(n.id), doc.finding && n.id == current);
        }
    }
    drawFoldBadges(app, doc, p);
    // The press in progress
    if (doc.gesture == SheetDoc::Gesture::Marquee) {
        D2D1_POINT_2F a = toScreen(app, doc, doc.pressSX, doc.pressSY), b = toScreen(app, doc, doc.lastSX, doc.lastSY);
        D2D1_RECT_F r = D2D1::RectF(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
        app.brush->SetColor(alpha(p.accent, 0.08f));
        app.renderTarget->FillRectangle(r, app.brush);
        app.brush->SetColor(alpha(p.accent, 0.7f));
        app.renderTarget->DrawRectangle(r, app.brush, dpi(app, 1.0f));
    }
    if (doc.gesture == SheetDoc::Gesture::Connect) {
        int from = nodeIndex(doc.model, doc.pressNode);
        if (from >= 0) {
            Box fb = boxOf(doc.model.nodes[from]);
            Curve c;
            int to = nodeIndex(doc.model, doc.connectTarget);
            if (to >= 0) {
                Side fs, ts;
                edgeSides(doc.model.nodes[from], doc.model.nodes[to], Side::None, Side::None, fs, ts);
                c = edgeCurve(fb, boxOf(doc.model.nodes[to]), fs, ts);
                D2D1_RECT_F tr2 = screenRect(app, doc, boxOf(doc.model.nodes[to]));
                strokeRound(app, D2D1::RectF(tr2.left - 3, tr2.top - 3, tr2.right + 3, tr2.bottom + 3), dpi(app, 10.0f), alpha(p.accent, 0.8f), dpi(app, 2.0f));
            } else {
                Box point{doc.lastSX - 1, doc.lastSY - 1, doc.lastSX + 1, doc.lastSY + 1};
                Node fake;
                fake.x = doc.lastSX;
                fake.y = doc.lastSY;
                fake.width = fake.height = 1;
                Side fs, ts;
                edgeSides(doc.model.nodes[from], fake, Side::None, Side::None, fs, ts);
                c = edgeCurve(fb, point, fs, ts);
            }
            drawCurve(app, doc, c, alpha(p.accent, 0.9f), dpi(app, 2.0f), true, false, to < 0);
        }
    }
    for (float gx : doc.guidesX) {
        D2D1_POINT_2F q = toScreen(app, doc, gx, 0);
        line(app, D2D1::Point2F(q.x, v.top), D2D1::Point2F(q.x, v.bottom), alpha(p.accent, 0.55f), dpi(app, 1.0f), dashedStyle(app));
    }
    for (float gy : doc.guidesY) {
        D2D1_POINT_2F q = toScreen(app, doc, 0, gy);
        line(app, D2D1::Point2F(v.left, q.y), D2D1::Point2F(v.right, q.y), alpha(p.accent, 0.55f), dpi(app, 1.0f), dashedStyle(app));
    }
    drawSelectionBar(app, doc, p);
    drawJumpLabels(app, doc, p);
    drawEditBox(app, doc, p);
    drawEditPopups(app, doc);
    drawEmptyHint(app, doc, p);

    // Chrome
    if (doc.presenting) {
        drawPresentation(app, doc, p);
    } else {
        drawMinimap(app, doc, p);
        float zoomRight = v.right - dpi(app, 16.0f);
        float zoomLeft = drawZoom(app, doc, p, zoomRight, v.bottom - dpi(app, 16.0f));
        drawTray(app, doc, p);
        drawFind(app, doc, p);
        drawToolbar(app, doc, p, zoomLeft);
        drawHintLine(app, doc, p);
    }
    drawPeek(app, doc, p);
    if (doc.readOnly) {
        std::wstring text = tr(app, "sheet.read_only");
        IDWriteTextLayout* l = makeLayout(app, text, textFormat(app, uiFace(), dpi(app, 12.5f), DWRITE_FONT_WEIGHT_NORMAL), v.right - v.left - dpi(app, 80.0f), 200);
        DWRITE_TEXT_METRICS m = metrics(l);
        float cx = (v.left + v.right) * 0.5f;
        D2D1_RECT_F banner = D2D1::RectF(cx - m.width * 0.5f - dpi(app, 14.0f), v.top + dpi(app, 60.0f), cx + m.width * 0.5f + dpi(app, 14.0f),
                                         v.top + dpi(app, 60.0f) + m.height + dpi(app, 14.0f));
        fillRound(app, banner, dpi(app, 8.0f), alpha(presetColor(app, "2"), 0.18f));
        strokeRound(app, banner, dpi(app, 8.0f), alpha(presetColor(app, "2"), 0.6f), dpi(app, 1.0f));
        drawText(app, l, banner.left + dpi(app, 14.0f), banner.top + dpi(app, 7.0f), p.text);
        if (l) l->Release();
    }
    drawMenu(app, doc, p);
    app.renderTarget->PopAxisAlignedClip();
}
