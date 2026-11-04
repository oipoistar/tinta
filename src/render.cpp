#include "frontmatter_ui.h"
#include "render.h"
#include "file_utils.h"
#include "inline_style.h"
#include "font_faces.h"
#include "page_style.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"
#include "mermaid.h"
#include "mermaid_ext.h"
#include "math_render.h"
#include "image_loader.h"
#include "i18n.h"
#include "link_target.h"
#include "network_path.h"
#include "paper.h"
#include "plantuml_app.h"
#include "plantuml_queue.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <exception>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <urlmon.h>
#include <thread>
#include <d2d1_3.h>
#include <shlwapi.h>
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shlwapi.lib")

// Fit-to-width override lookup for a table/diagram ordinal key
static bool fitBlockEnabled(const App& app, unsigned key) {
    return std::find(app.fitBlocks.begin(), app.fitBlocks.end(), key) !=
           app.fitBlocks.end();
}

App::LayoutShape listBullet(float textX, float baseline, float em, int depth, D2D1_COLOR_F color) {
    App::LayoutShape shape;
    const int kind = std::max(0, depth) % 3;
    // Two fifths of an em clear of the text, as a browser sets its marker
    const float cx = textX - em * 0.56f, cy = baseline - em * 0.3f;
    const float half = em * (kind == 2 ? 0.14f : 0.16f);
    shape.type = kind == 2 ? App::LayoutShapeType::Rectangle : App::LayoutShapeType::Ellipse;
    shape.rect = D2D1::RectF(cx - half, cy - half, cx + half, cy + half);
    shape.fill = color;
    if (kind == 1) {
        // A ring as wide as the disc: its stroke inside the disc's edge
        shape.strokeWidth = std::max(1.0f, em * 0.07f);
        const float inset = shape.strokeWidth * 0.5f;
        shape.rect = D2D1::RectF(cx - half + inset, cy - half + inset, cx + half - inset, cy + half - inset);
        shape.stroke = color;
        shape.fill.a = 0.0f;
    }
    return shape;
}

namespace {
constexpr float kHugeWidth = 100000.0f;
constexpr float kLineBucketTolerance = 5.0f;
// Horizontal padding drawn around an inline `code` pill
constexpr float kCodeSpanPadding = 4.0f;

struct LayoutInfo {
    IDWriteTextLayout* layout = nullptr;
    float width = 0.0f;
    float height = 0.0f;
};

static LayoutInfo createLayout(App& app, std::wstring_view text, IDWriteTextFormat* format,
                               float lineHeight, IDWriteTypography* typography,
                               float letterSpacing = 0.0f) {
    LayoutInfo info;
    if (!format || text.empty()) return info;

    app.dwriteFactory->CreateTextLayout(text.data(), (UINT32)text.length(),
        format, kHugeWidth, lineHeight, &info.layout);
    if (info.layout) {
        if (typography) {
            info.layout->SetTypography(typography, {0, (UINT32)text.length()});
        }
        // A CSS theme's letter-spacing (page_style.h): after each letter
        if (letterSpacing != 0.0f) {
            IDWriteTextLayout1* spaced = nullptr;
            if (SUCCEEDED(info.layout->QueryInterface(__uuidof(IDWriteTextLayout1),
                    reinterpret_cast<void**>(&spaced)))) {
                spaced->SetCharacterSpacing(0.0f, letterSpacing, 0.0f, {0, (UINT32)text.length()});
                spaced->Release();
            }
        }
        // Apply font fallback for emoji support
        if (app.fontFallback) {
            IDWriteTextLayout2* layout2 = nullptr;
            if (SUCCEEDED(info.layout->QueryInterface(__uuidof(IDWriteTextLayout2),
                    reinterpret_cast<void**>(&layout2)))) {
                layout2->SetFontFallback(app.fontFallback);
                layout2->Release();
            }
        }
        DWRITE_TEXT_METRICS metrics{};
        info.layout->GetMetrics(&metrics);
        info.width = metrics.widthIncludingTrailingWhitespace;
        info.height = metrics.height;
    }
    return info;
}

static void addTextRect(App& app, const D2D1_RECT_F& rect, size_t docStart, size_t docLength,
                        size_t runIndex = (size_t)-1) {
    size_t idx = app.textRects.size();
    app.textRects.push_back({rect, docStart, docLength, runIndex});

    // Table cells are laid out column by column. A wrapped cell can append a
    // later line before the next column appends its first line, so buckets
    // cannot be grouped by looking only at the last one.
    for (auto& bucket : app.lineBuckets) {
        if (std::abs(rect.top - bucket.top) <= kLineBucketTolerance) {
            bucket.bottom = std::max(bucket.bottom, rect.bottom);
            bucket.minX = std::min(bucket.minX, rect.left);
            bucket.maxX = std::max(bucket.maxX, rect.right);
            bucket.textRectIndices.push_back(idx);
            return;
        }
    }

    if (app.lineBuckets.empty() || rect.top > app.lineBuckets.back().top) {
        App::LineBucket bucket;
        bucket.top = rect.top;
        bucket.bottom = rect.bottom;
        bucket.minX = rect.left;
        bucket.maxX = rect.right;
        bucket.textRectIndices.push_back(idx);
        app.lineBuckets.push_back(std::move(bucket));
        return;
    }

    App::LineBucket bucket;
    bucket.top = rect.top;
    bucket.bottom = rect.bottom;
    bucket.minX = rect.left;
    bucket.maxX = rect.right;
    bucket.textRectIndices.push_back(idx);
    auto insertAt = std::lower_bound(
        app.lineBuckets.begin(), app.lineBuckets.end(), rect.top,
        [](const App::LineBucket& existing, float top) {
            return existing.top < top;
        });
    app.lineBuckets.insert(insertAt, std::move(bucket));
}

static void addTextRun(App& app, LayoutInfo&& info, const D2D1_POINT_2F& pos,
                       const D2D1_RECT_F& bounds, D2D1_COLOR_F color,
                       size_t docStart, size_t docLength, bool selectable) {
    if (!info.layout) return;

    App::LayoutTextRun run;
    run.layout = info.layout;
    run.pos = pos;
    run.bounds = bounds;
    run.color = color;
    run.docStart = docStart;
    run.docLength = docLength;
    run.selectable = selectable;
    app.layoutTextRuns.push_back(run);

    if (selectable) {
        addTextRect(app, bounds, docStart, docLength, app.layoutTextRuns.size() - 1);
    }
}

// Records docText[start, end) as bold, joining a range it continues
static void markRange(std::vector<std::pair<size_t, size_t>>& ranges, size_t start, size_t end) {
    if (start >= end) return;
    if (!ranges.empty() && ranges.back().second >= start) {
        ranges.back().second = std::max(ranges.back().second, end);
    } else {
        ranges.push_back({start, end});
    }
}

// The styles a copy from the viewer carries (#285): bold, italic, and
// links that lead somewhere outside the document. Anchors, wiki links and
// file references would lead nowhere from a mail, so they paste as text.
static void markStyles(App& app, const InlineStyle& style, bool isLink, const std::string& url,
                       size_t start, size_t end) {
    if (style.bold) markRange(app.docBoldRanges, start, end);
    if (style.italic) markRange(app.docItalicRanges, start, end);
    if (!isLink || start >= end ||
        !(url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 || url.rfind("mailto:", 0) == 0))
        return;
    auto& links = app.docLinkRanges;
    if (!links.empty() && links.back().url == url && links.back().end >= start) {
        links.back().end = std::max(links.back().end, end);
    } else {
        links.push_back({start, end, url});
    }
}

struct LayoutSnapshot {
    size_t textRuns, rects, lines, shapes, connectors, bitmaps;
    size_t links, textRects, lineBuckets, docTextLen, tasks, anchors;
};

static LayoutSnapshot takeSnapshot(App& app) {
    return {
        app.layoutTextRuns.size(),
        app.layoutRects.size(),
        app.layoutLines.size(),
        app.layoutShapes.size(),
        app.layoutConnectors.size(),
        app.layoutBitmaps.size(),
        app.linkRects.size(),
        app.textRects.size(),
        app.lineBuckets.size(),
        app.docText.size(),
        app.taskRects.size(),
        app.htmlAnchors.size()
    };
}

static void rollbackTo(App& app, const LayoutSnapshot& s) {
    for (size_t i = s.textRuns; i < app.layoutTextRuns.size(); i++) {
        if (app.layoutTextRuns[i].layout) {
            app.layoutTextRuns[i].layout->Release();
        }
    }
    for (size_t i = s.shapes; i < app.layoutShapes.size(); i++) {
        if (app.layoutShapes[i].geometry) {
            app.layoutShapes[i].geometry->Release();
        }
    }
    app.layoutTextRuns.resize(s.textRuns);
    app.layoutRects.resize(s.rects);
    app.layoutLines.resize(s.lines);
    app.layoutShapes.resize(s.shapes);
    app.layoutConnectors.resize(s.connectors);
    app.layoutBitmaps.resize(s.bitmaps);
    app.linkRects.resize(s.links);
    app.textRects.resize(s.textRects);
    app.lineBuckets.resize(s.lineBuckets);
    app.docText.resize(s.docTextLen);
    for (auto* ranges : {&app.docBoldRanges, &app.docItalicRanges}) {
        while (!ranges->empty() && ranges->back().first >= s.docTextLen) ranges->pop_back();
        if (!ranges->empty()) ranges->back().second = std::min(ranges->back().second, s.docTextLen);
    }
    auto& links = app.docLinkRanges;
    while (!links.empty() && links.back().start >= s.docTextLen) links.pop_back();
    if (!links.empty()) links.back().end = std::min(links.back().end, s.docTextLen);
    auto& items = app.docListItems;
    while (!items.empty() && items.back().start >= s.docTextLen) items.pop_back();
    for (auto& item : items) item.end = std::min(item.end, s.docTextLen);
    app.taskRects.resize(s.tasks);
    app.htmlAnchors.resize(s.anchors);  // a trial layout's anchors sit at trial positions
}

static void shiftLayoutItems(App& app, const LayoutSnapshot& from, float dx) {
    if (dx == 0.0f) return;
    for (size_t i = from.textRuns; i < app.layoutTextRuns.size(); i++) {
        auto& r = app.layoutTextRuns[i];
        r.pos.x += dx;
        r.bounds.left += dx;
        r.bounds.right += dx;
    }
    for (size_t i = from.rects; i < app.layoutRects.size(); i++) {
        auto& r = app.layoutRects[i];
        r.rect.left += dx;
        r.rect.right += dx;
    }
    for (size_t i = from.lines; i < app.layoutLines.size(); i++) {
        auto& r = app.layoutLines[i];
        r.p1.x += dx;
        r.p2.x += dx;
    }
    for (size_t i = from.shapes; i < app.layoutShapes.size(); i++) {
        auto& shape = app.layoutShapes[i];
        shape.rect.left += dx;
        shape.rect.right += dx;
    }
    for (size_t i = from.connectors; i < app.layoutConnectors.size(); i++) {
        auto& connector = app.layoutConnectors[i];
        connector.bounds.left += dx;
        connector.bounds.right += dx;
        for (auto& point : connector.points) point.x += dx;
    }
    for (size_t i = from.bitmaps; i < app.layoutBitmaps.size(); i++) {
        auto& bitmap = app.layoutBitmaps[i].destRect;
        bitmap.left += dx;
        bitmap.right += dx;
    }
    for (size_t i = from.links; i < app.linkRects.size(); i++) {
        auto& r = app.linkRects[i];
        r.bounds.left += dx;
        r.bounds.right += dx;
    }
    for (size_t i = from.textRects; i < app.textRects.size(); i++) {
        auto& r = app.textRects[i];
        r.rect.left += dx;
        r.rect.right += dx;
    }
    for (size_t i = from.tasks; i < app.taskRects.size(); i++) {
        app.taskRects[i].rect.left += dx;
        app.taskRects[i].rect.right += dx;
    }

    // A cell can share a line bucket with content laid out before it. Rebuild
    // the affected bucket extents from their rectangles after the shift.
    for (auto& bucket : app.lineBuckets) {
        bool affected = false;
        for (size_t idx : bucket.textRectIndices) {
            if (idx >= from.textRects) {
                affected = true;
                break;
            }
        }
        if (!affected) continue;

        bucket.minX = std::numeric_limits<float>::max();
        bucket.maxX = std::numeric_limits<float>::lowest();
        for (size_t idx : bucket.textRectIndices) {
            const auto& rect = app.textRects[idx].rect;
            bucket.minX = std::min(bucket.minX, rect.left);
            bucket.maxX = std::max(bucket.maxX, rect.right);
        }
    }
}

static float getSpaceWidth(App& app, IDWriteTextFormat* format) {
    if (format == app.textFormat) return app.spaceWidthText;
    if (format == app.boldFormat) return app.spaceWidthBold;
    if (format == app.italicFormat) return app.spaceWidthItalic;
    if (format == app.codeFormat) return app.spaceWidthCode;
    return measureText(app, L" ", format);
}

// The bucket extents of the text rects laid out since `from`, after a move
static void refreshBuckets(App& app, const LayoutSnapshot& from) {
    for (auto& bucket : app.lineBuckets) {
        bool affected = false;
        for (size_t idx : bucket.textRectIndices) affected = affected || idx >= from.textRects;
        if (!affected) continue;
        bucket.minX = std::numeric_limits<float>::max();
        bucket.maxX = std::numeric_limits<float>::lowest();
        for (size_t idx : bucket.textRectIndices) {
            bucket.minX = std::min(bucket.minX, app.textRects[idx].rect.left);
            bucket.maxX = std::max(bucket.maxX, app.textRects[idx].rect.right);
        }
    }
}

// A CSS theme's text-align (page_style.h) for text laid out since `from`
// between startX and startX + width: each line moved to the centre or the
// right, or, justified, its spaces widened to fill the line (but the last
// line's, and a line a hard break ends)
static void alignLines(App& app, const LayoutSnapshot& from, float startX, float width, const page::Style& css) {
    if (!css.has(css::Prop::TextAlign)) return;
    const css::TextAlign align = css->textAlign;
    if (align != css::TextAlign::Center && align != css::TextAlign::Right && align != css::TextAlign::Justify) return;

    // Lines by the tops of their words
    std::vector<float> lines;
    for (size_t i = from.textRects; i < app.textRects.size(); ++i) lines.push_back(app.textRects[i].rect.top);
    for (size_t i = from.bitmaps; i < app.layoutBitmaps.size(); ++i) lines.push_back(app.layoutBitmaps[i].destRect.top);
    if (lines.empty()) return;
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end(), [](float a, float b) { return b - a < 0.5f; }), lines.end());
    auto lineOf = [&](float top, float bottom) {
        size_t k = (size_t)(std::upper_bound(lines.begin(), lines.end(), (top + bottom) * 0.5f) - lines.begin());
        return k ? k - 1 : 0;
    };
    std::vector<float> minX(lines.size(), std::numeric_limits<float>::max());
    std::vector<float> maxX(lines.size(), std::numeric_limits<float>::lowest());
    auto extend = [&](const D2D1_RECT_F& r) {
        const size_t k = lineOf(r.top, r.bottom);
        minX[k] = std::min(minX[k], r.left);
        maxX[k] = std::max(maxX[k], r.right);
    };
    for (size_t i = from.textRects; i < app.textRects.size(); ++i) extend(app.textRects[i].rect);
    for (size_t i = from.bitmaps; i < app.layoutBitmaps.size(); ++i) extend(app.layoutBitmaps[i].destRect);
    for (size_t i = from.rects; i < app.layoutRects.size(); ++i) extend(app.layoutRects[i].rect);

    // Where each line's x goes: a shift, and for a justified line the
    // widened spaces at or left of x
    std::vector<float> shift(lines.size(), 0.0f), per(lines.size(), 0.0f);
    std::vector<std::vector<float>> points(lines.size());
    if (align == css::TextAlign::Justify) {
        for (size_t k = 0; k + 1 < lines.size(); ++k) {
            if (maxX[k] <= minX[k]) continue;
            const float extra = startX + width - maxX[k];
            if (extra <= 0.5f) continue;
            std::vector<size_t> runs;
            for (size_t i = from.textRuns; i < app.layoutTextRuns.size(); ++i) {
                const auto& r = app.layoutTextRuns[i];
                if (lineOf(r.bounds.top, r.bounds.bottom) == k && r.layout) runs.push_back(i);
            }
            std::sort(runs.begin(), runs.end(), [&](size_t a, size_t b) {
                return app.layoutTextRuns[a].pos.x < app.layoutTextRuns[b].pos.x;
            });
            if (runs.empty()) continue;
            const auto& last = app.layoutTextRuns[runs.back()];
            const size_t lineEnd = last.docStart + last.docLength;
            if (lineEnd < app.docText.size() && app.docText[lineEnd] == L'\n') continue;
            struct Gap { float x; size_t run; UINT32 index; };
            std::vector<Gap> gaps;
            for (size_t n = 0; n < runs.size(); ++n) {
                const auto& r = app.layoutTextRuns[runs[n]];
                for (size_t c = 0; c + 1 < r.docLength && r.docStart + c < app.docText.size(); ++c) {
                    if (app.docText[r.docStart + c] != L' ') continue;
                    FLOAT hx = 0, hy = 0;
                    DWRITE_HIT_TEST_METRICS m{};
                    if (SUCCEEDED(r.layout->HitTestTextPosition((UINT32)c, TRUE, &hx, &hy, &m)))
                        gaps.push_back({r.pos.x + hx, runs[n], (UINT32)c});
                }
                if (n + 1 < runs.size()) {
                    const auto& next = app.layoutTextRuns[runs[n + 1]];
                    for (size_t at = r.docStart + r.docLength; at < next.docStart && at < app.docText.size(); ++at) {
                        if (app.docText[at] == L' ') {
                            gaps.push_back({next.pos.x, SIZE_MAX, 0});
                            break;
                        }
                    }
                }
            }
            if (gaps.empty()) continue;
            per[k] = extra / (float)gaps.size();
            for (const Gap& g : gaps) {
                points[k].push_back(g.x);
                if (g.run == SIZE_MAX) continue;
                IDWriteTextLayout1* spaced = nullptr;
                if (SUCCEEDED(app.layoutTextRuns[g.run].layout->QueryInterface(__uuidof(IDWriteTextLayout1),
                        reinterpret_cast<void**>(&spaced)))) {
                    spaced->SetCharacterSpacing(0.0f, per[k], 0.0f, {g.index, 1});
                    spaced->Release();
                }
            }
            std::sort(points[k].begin(), points[k].end());
        }
    } else {
        for (size_t k = 0; k < lines.size(); ++k) {
            if (maxX[k] <= minX[k]) continue;
            const float room = align == css::TextAlign::Center
                ? (startX + width * 0.5f) - (minX[k] + maxX[k]) * 0.5f
                : startX + width - maxX[k];
            shift[k] = std::max(0.0f, room);
        }
    }
    auto moved = [&](size_t k, float x) {
        if (points[k].empty()) return x + shift[k];
        const size_t n = (size_t)(std::upper_bound(points[k].begin(), points[k].end(), x + 0.01f) - points[k].begin());
        return x + per[k] * (float)n;
    };
    auto moveRect = [&](D2D1_RECT_F& r) {
        const size_t k = lineOf(r.top, r.bottom);
        const float left = moved(k, r.left);
        r.right = moved(k, r.right);
        r.left = left;
    };
    for (size_t i = from.textRuns; i < app.layoutTextRuns.size(); ++i) {
        auto& r = app.layoutTextRuns[i];
        const size_t k = lineOf(r.bounds.top, r.bounds.bottom);
        const float dx = moved(k, r.pos.x) - r.pos.x;
        r.pos.x += dx;
        r.bounds.right = moved(k, r.bounds.right);
        r.bounds.left += dx;
    }
    for (size_t i = from.rects; i < app.layoutRects.size(); ++i) moveRect(app.layoutRects[i].rect);
    for (size_t i = from.lines; i < app.layoutLines.size(); ++i) {
        auto& l = app.layoutLines[i];
        const size_t k = lineOf(std::min(l.p1.y, l.p2.y), std::max(l.p1.y, l.p2.y));
        l.p1.x = moved(k, l.p1.x);
        l.p2.x = moved(k, l.p2.x);
    }
    for (size_t i = from.shapes; i < app.layoutShapes.size(); ++i) moveRect(app.layoutShapes[i].rect);
    for (size_t i = from.bitmaps; i < app.layoutBitmaps.size(); ++i) moveRect(app.layoutBitmaps[i].destRect);
    for (size_t i = from.links; i < app.linkRects.size(); ++i) moveRect(app.linkRects[i].bounds);
    for (size_t i = from.textRects; i < app.textRects.size(); ++i) moveRect(app.textRects[i].rect);
    refreshBuckets(app, from);
}

// A block's box as a CSS theme draws it (page_style.h): background,
// padding and borders around its content. It opens before the content,
// narrowing the room the content has, and closes after it. Nothing is
// drawn or moved when the theme sets none of these
struct BlockBox {
    App& app;
    bool open = false;
    float left = 0, right = 0, top = 0;
    float edge[4] = {}, pad[4] = {};
    D2D1_COLOR_F edgeColor[4] = {};
    bool edgeColored[4] = {};
    size_t background = SIZE_MAX;

    BlockBox(App& app, const page::Style& css, float& y, float& indent, float& maxWidth) : app(app) {
        using css::Prop;
        if (!css) return;
        bool any = css.has(Prop::BackgroundColor) && css->backgroundColor.a > 0.0f;
        for (int side = 0; side < 4; ++side) {
            if (css.has((Prop)((int)Prop::PaddingTop + side))) {
                pad[side] = page::px(app, css->padding[side]);
                any = any || pad[side] != 0.0f;
            }
            const Prop width = (Prop)((int)Prop::BorderTopWidth + side * 3);
            if (css.has(width) || css.has((Prop)((int)width + 1))) {
                const css::Border& b = css->border[side];
                edge[side] = b.style == css::LineStyle::None ? 0.0f : page::px(app, b.width);
                edgeColored[side] = css.has((Prop)((int)width + 2));
                edgeColor[side] = page::color(b.color);
                any = any || edge[side] > 0.0f;
            }
        }
        if (!any) return;
        open = true;
        left = indent;
        right = indent + maxWidth;
        top = y;
        if (css.has(Prop::BackgroundColor) && css->backgroundColor.a > 0.0f) {
            background = app.layoutRects.size();
            App::LayoutRect r;
            r.rect = D2D1::RectF(left, top, right, top);
            r.color = page::color(css->backgroundColor);
            if (css.has(Prop::BorderTopLeftRadius)) r.radius = page::px(app, css->radius[0]);
            app.layoutRects.push_back(r);
        }
        y += edge[0] + pad[0];
        indent += edge[3] + pad[3];
        maxWidth = std::max(1.0f, maxWidth - edge[3] - pad[3] - edge[1] - pad[1]);
        if (edge[0] + pad[0] > 0.0f) page::boundary(app);
    }

    // `ink` colours a border the theme gave no colour of its own, as CSS's
    // currentColor does
    void close(float& y, D2D1_COLOR_F ink) {
        if (!open) return;
        y += pad[2] + edge[2];
        if (pad[2] + edge[2] > 0.0f) page::boundary(app);
        if (background < app.layoutRects.size()) app.layoutRects[background].rect.bottom = y;
        auto color = [&](int side) { return edgeColored[side] ? edgeColor[side] : ink; };
        if (edge[0] > 0) app.layoutLines.push_back({{left, top + edge[0] / 2}, {right, top + edge[0] / 2}, color(0), edge[0]});
        if (edge[2] > 0) app.layoutLines.push_back({{left, y - edge[2] / 2}, {right, y - edge[2] / 2}, color(2), edge[2]});
        if (edge[3] > 0) app.layoutLines.push_back({{left + edge[3] / 2, top}, {left + edge[3] / 2, y}, color(3), edge[3]});
        if (edge[1] > 0) app.layoutLines.push_back({{right - edge[1] / 2, top}, {right - edge[1] / 2, y}, color(1), edge[1]});
    }
};

// The marker text a list-style-type gives the nth item ("3", "c", "iii")
static std::wstring counterText(int n, css::ListStyle style) {
    switch (style) {
        case css::ListStyle::DecimalLeadingZero:
            return (n >= 0 && n < 10 ? L"0" : L"") + std::to_wstring(n);
        case css::ListStyle::LowerAlpha:
        case css::ListStyle::UpperAlpha:
        case css::ListStyle::LowerGreek: {
            if (n < 1) return std::to_wstring(n);
            const bool greek = style == css::ListStyle::LowerGreek;
            const int base = greek ? 24 : 26;
            const wchar_t first = greek ? 0x03B1 : style == css::ListStyle::LowerAlpha ? L'a' : L'A';
            std::wstring out;
            for (int k = n; k > 0; k = (k - 1) / base) {
                wchar_t c = (wchar_t)(first + (k - 1) % base);
                if (greek && c >= 0x03C2) c++;  // no final sigma
                out.insert(out.begin(), c);
            }
            return out;
        }
        case css::ListStyle::LowerRoman:
        case css::ListStyle::UpperRoman: {
            if (n < 1 || n > 3999) return std::to_wstring(n);
            static const struct { int value; const wchar_t* text; } numerals[] = {
                {1000, L"m"}, {900, L"cm"}, {500, L"d"}, {400, L"cd"}, {100, L"c"}, {90, L"xc"},
                {50, L"l"}, {40, L"xl"}, {10, L"x"}, {9, L"ix"}, {5, L"v"}, {4, L"iv"}, {1, L"i"}};
            std::wstring out;
            for (const auto& numeral : numerals) {
                for (; n >= numeral.value; n -= numeral.value) out += numeral.text;
            }
            if (style == css::ListStyle::UpperRoman) CharUpperBuffW(out.data(), (DWORD)out.size());
            return out;
        }
        case css::ListStyle::CjkDecimal: {
            static const wchar_t digits[] = {0x3007, 0x4E00, 0x4E8C, 0x4E09, 0x56DB, 0x4E94, 0x516D, 0x4E03, 0x516B, 0x4E5D};
            std::wstring out;
            for (wchar_t c : std::to_wstring(n)) out += c >= L'0' && c <= L'9' ? digits[c - L'0'] : c;
            return out;
        }
        default:
            return std::to_wstring(n);
    }
}

static void layoutElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);
static void layoutImage(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);

// --- UAX#14 line-break analysis ---
//
// DirectWrite's text analyzer implements the Unicode line breaking
// algorithm, including CJK rules (a break opportunity between almost every
// pair of ideographs, but never before closing punctuation like 。，」or
// after opening brackets). Chinese prose has no spaces, so anything less
// treats an entire sentence as one unbreakable word.

namespace {

class LineBreakAnalysis final : public IDWriteTextAnalysisSource,
                                public IDWriteTextAnalysisSink {
public:
    LineBreakAnalysis(const wchar_t* text, UINT32 length)
        : text_(text), length_(length), breakpoints_(length) {}

    std::vector<DWRITE_LINE_BREAKPOINT> breakpoints_;

    // Stack-allocated and used synchronously: refcounting is inert
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (riid == __uuidof(IDWriteTextAnalysisSource)) {
            *object = static_cast<IDWriteTextAnalysisSource*>(this);
            return S_OK;
        }
        if (riid == __uuidof(IDWriteTextAnalysisSink)) {
            *object = static_cast<IDWriteTextAnalysisSink*>(this);
            return S_OK;
        }
        if (riid == __uuidof(IUnknown)) {
            *object = static_cast<IUnknown*>(static_cast<IDWriteTextAnalysisSource*>(this));
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    // IDWriteTextAnalysisSource
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** text,
                                                UINT32* textLength) override {
        if (position >= length_) { *text = nullptr; *textLength = 0; return S_OK; }
        *text = text_ + position;
        *textLength = length_ - position;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** text,
                                                    UINT32* textLength) override {
        if (position == 0 || position > length_) { *text = nullptr; *textLength = 0; return S_OK; }
        *text = text_;
        *textLength = position;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* textLength,
                                            const WCHAR** localeName) override {
        *localeName = L"en-us";
        *textLength = length_ - position;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 position, UINT32* textLength,
                                                    IDWriteNumberSubstitution** substitution) override {
        *substitution = nullptr;
        *textLength = length_ - position;
        return S_OK;
    }

    // IDWriteTextAnalysisSink
    HRESULT STDMETHODCALLTYPE SetLineBreakpoints(UINT32 position, UINT32 length,
                                                 const DWRITE_LINE_BREAKPOINT* lineBreakpoints) override {
        for (UINT32 i = 0; i < length && position + i < breakpoints_.size(); i++) {
            breakpoints_[position + i] = lineBreakpoints[i];
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetScriptAnalysis(UINT32, UINT32, const DWRITE_SCRIPT_ANALYSIS*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBidiLevel(UINT32, UINT32, UINT8, UINT8) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetNumberSubstitution(UINT32, UINT32, IDWriteNumberSubstitution*) override { return S_OK; }

private:
    const wchar_t* text_;
    UINT32 length_;
};

// canBreak[i] == true when a line may break before text[i]; canBreak[len]
// is always true. Falls back to space-only breaking if analysis fails.
void analyzeBreakOpportunities(App& app, const std::wstring& text, std::vector<bool>& canBreak) {
    canBreak.assign(text.size() + 1, false);
    if (text.empty()) return;
    canBreak[text.size()] = true;

    // Fast path: plain ASCII prose with ordinary token lengths breaks at
    // spaces exactly like the full algorithm — skip the analyzer round trip
    bool simple = true;
    size_t tokenLen = 0;
    for (wchar_t c : text) {
        if (c >= 0x80) { simple = false; break; }
        if (c == L' ') tokenLen = 0;
        else if (++tokenLen > 60) { simple = false; break; }  // long URLs etc. want real breaks
    }
    if (simple) {
        for (size_t i = 1; i < text.size(); i++) {
            if (text[i - 1] == L' ') canBreak[i] = true;
        }
        return;
    }

    bool analyzed = false;
    if (app.textAnalyzer) {
        LineBreakAnalysis analysis(text.c_str(), (UINT32)text.size());
        if (SUCCEEDED(app.textAnalyzer->AnalyzeLineBreakpoints(
                &analysis, 0, (UINT32)text.size(), &analysis))) {
            for (size_t i = 1; i < text.size(); i++) {
                UINT8 after = analysis.breakpoints_[i - 1].breakConditionAfter;
                canBreak[i] = (after == DWRITE_BREAK_CONDITION_CAN_BREAK ||
                               after == DWRITE_BREAK_CONDITION_MUST_BREAK);
            }
            analyzed = true;
        }
    }
    if (!analyzed) {
        for (size_t i = 1; i < text.size(); i++) {
            if (text[i - 1] == L' ') canBreak[i] = true;
        }
    }
}

} // namespace

// First-line baseline of a layout, measured from its top; negative
// when the layout has no line to measure
static float layoutBaseline(IDWriteTextLayout* layout) {
    if (!layout) return -1.0f;
    UINT32 count = 0;
    layout->GetLineMetrics(nullptr, 0, &count);
    if (!count) return -1.0f;
    std::vector<DWRITE_LINE_METRICS> lines(count);
    if (FAILED(layout->GetLineMetrics(lines.data(), count, &count))) return -1.0f;
    return lines.front().baseline;
}

// Where Latin text in this format sits below the top of its layout: the
// same "Ag" probe the inline math aligns to. Measured once per format;
// updateTextFormats clears the cache when it rebuilds the formats.
static float formatBaseline(App& app, IDWriteTextFormat* format) {
    for (const auto& known : app.formatBaselines)
        if (known.first == format) return known.second;
    float baseline = -1.0f;
    IDWriteTextLayout* probe = nullptr;
    if (SUCCEEDED(app.dwriteFactory->CreateTextLayout(L"Ag", 2, format, 1000, 1000, &probe))) {
        baseline = layoutBaseline(probe);
        probe->Release();
    }
    if (baseline < 0.0f) baseline = format->GetFontSize() * 0.8f;
    app.formatBaselines.emplace_back(format, baseline);
    return baseline;
}

// A run's own baseline, or its format's when the layout has none
static float runBaseline(App& app, IDWriteTextLayout* layout, IDWriteTextFormat* format) {
    float baseline = layoutBaseline(layout);
    return baseline >= 0.0f ? baseline : formatBaseline(app, format);
}

// The ==highlight== an inline element sits in, if any
static const Element* highlightSpanOf(const Element* elem) {
    for (; elem; elem = elem->parent)
        if (elem->type == ElementType::Highlight) return elem;
    return nullptr;
}

// firstLineX, when set, starts the first line left of startX, where the
// lines after it wrap: a hanging indent (a bibliography's entries)
static void layoutInlineContent(App& app, const std::vector<ElementPtr>& elements,
                                float startX, float& y, float maxWidth,
                                IDWriteTextFormat* baseFormat, D2D1_COLOR_F baseColor,
                                const std::string& baseLinkUrl = {}, float customLineHeight = 0.0f,
                                float firstLineX = -1.0f, const page::Style* blockCss = nullptr) {
    float x = firstLineX >= 0.0f ? firstLineX : startX;
    float lineHeight = customLineHeight > 0 ? customLineHeight : baseFormat->GetFontSize() * themeLineHeight(app.theme);
    const float normalLineHeight = lineHeight;
    float mathTextOffset = 0;
    float maxX = startX + maxWidth;
    float spaceWidth = getSpaceWidth(app, baseFormat);

    // Every run sits on the baseline its format's own font gives Latin
    // text, and code-styled spans on the baseline of the text around them
    // (#259, #262). DirectWrite hangs each run from the line top on the
    // ascent of the fonts it uses, so a code font or a fallback font with
    // another ascent would otherwise shift the run up or down.
    auto baselineOf = [&](IDWriteTextFormat* format) { return formatBaseline(app, format); };

    // <ins> and <u> text (#263): a rule just under the baseline of the text
    // the underline began in, and never above the run's own, so a subscript
    // inside stays clear of it. The spaces between two underlined runs on
    // one line (<ins>a **b** c</ins>) carry the rule across.
    float underlineEndX = -1.0f;
    float underlineLineY = 0.0f;
    auto addUnderline = [&](const InlineStyle& style, float lineStartX, float lineEndX,
                            float lineY, float runBaselineY, D2D1_COLOR_F color) {
        if (lineEndX <= lineStartX) return;
        if (underlineEndX >= 0.0f && underlineLineY == lineY && underlineEndX < lineStartX) {
            lineStartX = underlineEndX;
        }
        IDWriteTextFormat* origin = style.underlineFormat ? style.underlineFormat : baseFormat;
        float originBaseline = lineY + mathTextOffset + style.underlineYOffset + baselineOf(origin);
        float baseline = std::max(originBaseline, runBaselineY);
        float underlineY = baseline + std::max(1.0f, origin->GetFontSize() * 0.12f);
        app.layoutLines.push_back({D2D1::Point2F(lineStartX, underlineY),
                                   D2D1::Point2F(lineEndX, underlineY), color, 1.0f});
        underlineEndX = lineEndX;
        underlineLineY = lineY;
    };

    std::string inlineAnchor;
    bool linkUnderInsertion = false;  // a link inside <ins> shares its rule
    int8_t linkRule = -1;             // a CSS theme's text-decoration on the link
    std::optional<D2D1_COLOR_F> linkRuleColor;
    auto addLinkSegment = [&](float lineStartX, float lineEndX, float lineY,
                              const std::string& linkUrl, D2D1_COLOR_F color) {
        if (lineEndX <= lineStartX) return;
        if (!inlineAnchor.empty()) app.footnoteAnchors.emplace(inlineAnchor, lineY);
        bool refLive = linkUrl.rfind("fileref-ok:", 0) == 0;
        bool refMissing = linkUrl.rfind("fileref-missing:", 0) == 0;
        float underlineY = lineY + mathTextOffset + normalLineHeight - 2;
        if (refLive || refMissing) {
            // .md references underline dashed; a missing target keeps a
            // faint ghost of the same treatment, and its click offers to
            // create the file (#127)
            D2D1_COLOR_F dashColor = color;
            dashColor.a *= 0.55f;
            float dash = normalLineHeight * 0.16f;
            float gap = normalLineHeight * 0.12f;
            for (float sx = lineStartX; sx < lineEndX; sx += dash + gap) {
                float ex = std::min(sx + dash, lineEndX);
                app.layoutLines.push_back({D2D1::Point2F(sx, underlineY),
                                           D2D1::Point2F(ex, underlineY),
                                           dashColor, 1.0f});
            }
        } else if ((inlineAnchor.empty() || linkRule == 1) && !linkUnderInsertion && linkRule != 0) {
            app.layoutLines.push_back({D2D1::Point2F(lineStartX, underlineY),
                                       D2D1::Point2F(lineEndX, underlineY),
                                       linkRuleColor.value_or(color), 1.0f});
        }
        App::LinkRect lr;
        lr.bounds = D2D1::RectF(lineStartX, lineY, lineEndX, lineY + lineHeight);
        lr.url = linkUrl;
        lr.anchor = inlineAnchor;
        app.linkRects.push_back(lr);
    };

    InlineStyle rootStyle;
    rootStyle.format = baseFormat;
    rootStyle.color = baseColor;
    rootStyle.linkUrl = baseLinkUrl;
    rootStyle.isLink = !baseLinkUrl.empty();
    rootStyle.css = blockCss ? *blockCss : page::current(app);
    std::vector<StyledRun> runs;
    flattenInline(app, elements, rootStyle, lineHeight, runs);

    // Reserve a common baseline and enough vertical room before retaining
    // any runs. Uniform spacing within this paragraph keeps wrapping and
    // selection consistent even when a tall inline matrix occurs mid-line.
    std::vector<MathBoxPtr> mathBoxes;
    std::vector<float> mathBaselines;
    float mathBelow = 0;
    for (size_t i = 0; i < runs.size(); ++i) {
        const auto& run = runs[i];
        if (run.elem->type != ElementType::MathInline && run.elem->type != ElementType::MathDisplay) continue;
        if (mathBoxes.empty()) {
            mathBoxes.resize(runs.size());
            mathBaselines.resize(runs.size(), 0);
        }
        IDWriteTextFormat* format = run.style.format ? run.style.format : baseFormat;
        float size = format->GetFontSize();
        mathBoxes[i] = mathParse(app, mathResolveRefs(toWide(run.elem->text), app.mathLabels), size, false);
        if (!mathBoxes[i]) continue;
        float baseline = size * 0.8f;
        IDWriteTextLayout* probe = nullptr;
        if (SUCCEEDED(app.dwriteFactory->CreateTextLayout(L"Ag", 2, format, 1000, 1000, &probe))) {
            DWRITE_LINE_METRICS metrics{};
            UINT32 count = 1;
            if (SUCCEEDED(probe->GetLineMetrics(&metrics, 1, &count))) baseline = metrics.baseline;
            probe->Release();
        }
        mathBaselines[i] = baseline;
        mathExpandLine(mathBoxes[i], baseline + run.style.drawYOffset, normalLineHeight, mathTextOffset, mathBelow);
    }
    lineHeight += mathTextOffset + mathBelow;

    // A highlight is drawn in pieces, one per text segment and math box,
    // each padded 2 px on both sides. Its colour is translucent, so pieces
    // that meet on a line (==x^2^==, ==a $x$ b==) join rather than overlap
    // into a darker strip. Pieces of one highlight also bridge the space
    // between their runs; separate highlights only stop overlapping. Pieces
    // of equal height merge into one band. Beside a taller math box the text
    // band ends at the box, which keeps its full height.
    size_t bgPiece = SIZE_MAX;  // the latest piece, in layoutRects
    float bgPieceY = 0.0f;
    const Element* bgPieceSpan = nullptr;
    bool bgSpanBroken = false;  // a code chip, ruby or image came since
    auto addHighlight = [&](D2D1_RECT_F rect, D2D1_COLOR_F color, const Element* span) {
        if (bgPiece < app.layoutRects.size() && bgPieceY == y) {
            D2D1_RECT_F& prev = app.layoutRects[bgPiece].rect;
            if ((span && span == bgPieceSpan && !bgSpanBroken) || rect.left < prev.right) {
                bgPieceSpan = span;
                bgSpanBroken = false;
                if (rect.top == prev.top && rect.bottom == prev.bottom) {
                    prev.right = std::max(prev.right, rect.right);
                    return;
                }
                // A whole-pixel joint leaves no antialiased seam on screen
                bool taller = rect.top <= prev.top && rect.bottom >= prev.bottom;
                float joint = std::round(taller ? rect.left : prev.right);
                prev.right = std::max(prev.left, joint);
                rect.left = joint;
                if (rect.right <= rect.left) return;
            }
        }
        bgPiece = app.layoutRects.size();
        bgPieceY = y;
        bgPieceSpan = span;
        bgSpanBroken = false;
        app.layoutRects.push_back({rect, color});
    };

    for (size_t runIndex = 0; runIndex < runs.size(); runIndex++) {
        const auto& run = runs[runIndex];
        const ElementPtr& elem = run.elem;
        IDWriteTextFormat* format = run.style.format;
        IDWriteTextFormat* textFormat = format ? format : baseFormat;
        D2D1_COLOR_F color = run.style.color;
        const std::string& linkUrl = run.style.linkUrl;
        inlineAnchor = run.style.anchorId;
        linkRule = run.style.linkRule;
        linkRuleColor = run.style.ruleColor;
        bool isLink = run.style.isLink;
        bool hasBg = run.style.hasBg;
        bool hasStrike = run.style.hasStrike;
        // A link inside <ins> is underlined once, by the insertion's rule;
        // a .md reference keeps its dashed one alone
        bool fileRef = isLink && (linkUrl.rfind("fileref-ok:", 0) == 0 ||
                                  linkUrl.rfind("fileref-missing:", 0) == 0);
        bool hasUnderline = run.style.hasUnderline && !fileRef;
        linkUnderInsertion = hasUnderline;
        if (!hasUnderline) underlineEndX = -1.0f;
        D2D1_COLOR_F bgColor = run.style.bgColor;
        const Element* bgSpan = hasBg ? highlightSpanOf(elem.get()) : nullptr;
        // These draw no highlight, so the band does not bridge across them
        if (elem->type == ElementType::Code || elem->type == ElementType::Ruby ||
            elem->type == ElementType::Image) bgSpanBroken = true;
        float drawYOffset = run.style.drawYOffset + mathTextOffset;
        bool wrappedCode = false;

        std::wstring text;

        switch (elem->type) {
            case ElementType::Text:
                text = run.style.css ? page::transform(toWide(elem->text), run.style.css) : toWide(elem->text);
                break;

            case ElementType::Code: {
                format = run.style.codeFormat ? run.style.codeFormat
                                              : inlineCodeFormat(app, run.style.bold, run.style.italic);
                if (run.style.codeColor) color = *run.style.codeColor;
                else if (!isLink) color = themeInlineCodeColor(app.theme);
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text += toWide(child->text);
                    }
                }

                size_t codeDocStart = app.docText.size();
                if (run.style.css) text = page::transform(std::move(text), run.style.css);
                LayoutInfo info = createLayout(app, text, format, lineHeight,
                                               page::typography(app, run.style.css, app.codeTypography),
                                               page::letterSpacing(app, run.style.css));
                float textWidth = info.width;
                // A CSS theme's padding puts the chip after the space before
                // it, the text inside; Tinta's own overhangs that space by 2
                const bool padded = run.style.codePadLeft >= 0.0f || run.style.codePadRight >= 0.0f;
                const float padLeft = padded ? (run.style.codePadLeft >= 0.0f ? run.style.codePadLeft : 2.0f) : 0.0f;
                const float advance = padded
                    ? padLeft + textWidth + (run.style.codePadRight >= 0.0f ? run.style.codePadRight : kCodeSpanPadding)
                    : textWidth + kCodeSpanPadding;

                // Keep fitting code spans atomic. Oversized ones share the
                // Unicode/cluster-aware wrapping path below, including during
                // table row measurement, so their chips cannot cover the next
                // column (#239).
                if (advance > maxWidth) {
                    if (info.layout) info.layout->Release();
                    wrappedCode = true;
                    drawYOffset += baselineOf(textFormat) - baselineOf(format);
                    break;
                }

                if (x + advance > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }

                // The span keeps its padding without a background (#278)
                const D2D1_COLOR_F codeBox = run.style.codeBox.value_or(themeInlineCodeBackground(app.theme));
                if (codeBox.a > 0.0f || run.style.codeEdgeWidth > 0.0f) {
                    App::LayoutRect chip;
                    chip.rect = padded ? D2D1::RectF(x, y + mathTextOffset, x + advance, y + mathTextOffset + normalLineHeight)
                                       : D2D1::RectF(x - 2, y + mathTextOffset, x + textWidth + kCodeSpanPadding,
                                                     y + mathTextOffset + normalLineHeight);
                    chip.color = codeBox;
                    chip.radius = run.style.codeRadius;
                    chip.strokeWidth = run.style.codeEdgeWidth;
                    chip.stroke = run.style.codeEdge.value_or(color);
                    app.layoutRects.push_back(chip);
                }
                const float textX = x + padLeft;

                // The run's own baseline counts any fallback font it uses
                float codeTop = drawYOffset + baselineOf(textFormat) -
                                runBaseline(app, info.layout, format);
                D2D1_POINT_2F pos = D2D1::Point2F(textX, y + codeTop);
                D2D1_RECT_F bounds = D2D1::RectF(textX, y, textX + textWidth, y + lineHeight);
                addTextRun(app, std::move(info), pos, bounds, color,
                           codeDocStart, text.length(), true);

                app.docText += text;
                markStyles(app, run.style, isLink, linkUrl, codeDocStart, app.docText.size());
                if (isLink) addLinkSegment(textX, textX + textWidth, y, linkUrl, color);
                if (hasUnderline) {
                    addUnderline(run.style, textX, textX + textWidth, y, y + drawYOffset + baselineOf(textFormat),
                                 color);
                }
                // ~~`code`~~ and <del>`code`</del>: struck like the text around it
                if (hasStrike) {
                    float strikeY = y + mathTextOffset + normalLineHeight * 0.55f;
                    app.layoutLines.push_back({D2D1::Point2F(textX, strikeY),
                                               D2D1::Point2F(textX + textWidth, strikeY), color, 1.0f});
                }
                // Advance by the pill's own padding only: the surrounding text
                // already carries the source's spacing, so a full space width
                // here would double it and shove a following comma off the span
                x += advance;
                continue;
            }

            case ElementType::MathInline:
            case ElementType::MathDisplay: {
                // Native inline math (#80): an atomic measured box in the
                // text flow, baseline-aligned with the surrounding line
                std::wstring latex = toWide(elem->text);
                MathBoxPtr box = mathBoxes[runIndex];
                // $$ shows as $$: a display equation's source read $x$ (#314)
                const std::wstring dollars = elem->type == ElementType::MathDisplay ? L"$$" : L"$";
                if (!box) {
                    // Unsupported TeX: show the raw source in code style
                    format = inlineCodeFormat(app, run.style.bold, run.style.italic);
                    if (!isLink) color = themeInlineCodeColor(app.theme);
                    drawYOffset += baselineOf(textFormat) - baselineOf(format);
                    text = dollars + latex + dollars;
                    break;
                }
                float w = mathBoxWidth(box);
                if (x + w > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }
                // Align the box baseline with the surrounding text baseline
                float textBaseline = mathBaselines[runIndex];
                float boxTop = y + drawYOffset + textBaseline - mathBoxBaseline(box);
                if (hasBg) {
                    addHighlight(D2D1::RectF(x - 2,
                        std::min(boxTop, y + mathTextOffset + 1), x + w + 2,
                        std::max(boxTop + mathBoxHeight(box), y + mathTextOffset + normalLineHeight - 1)),
                        bgColor, bgSpan);
                }
                if (hasStrike) {
                    float strikeY = y + mathTextOffset + normalLineHeight * 0.55f;
                    app.layoutLines.push_back({D2D1::Point2F(x, strikeY),
                                               D2D1::Point2F(x + w, strikeY), color, 1.0f});
                }
                if (hasUnderline) {
                    // On the text baseline, as under the words around it
                    addUnderline(run.style, x, x + w, y, y + drawYOffset + textBaseline, color);
                }
                mathBoxRetain(app, box, x, boxTop, color);
                if (isLink) addLinkSegment(x, x + w, y, linkUrl, color);

                size_t docStart = app.docText.size();
                std::wstring source = dollars + latex + dollars;
                app.docText += source;
                addTextRect(app,
                            D2D1::RectF(x, y, x + w, y + lineHeight),
                            docStart, source.size());
                x += w;
                continue;
            }

            case ElementType::SoftBreak:
                text = L" ";
                break;

            case ElementType::HardBreak:
                app.docText += L"\n";
                x = startX;
                y += lineHeight;
                continue;

            case ElementType::Anchor:
                // An <a id> link target draws nothing; links land on its line
                app.htmlAnchors.push_back({elem->title, y});
                continue;

            case ElementType::Image: {
                // Break out of inline flow, render image as block
                if (x > startX) {
                    y += lineHeight;  // end current line
                    x = startX;
                }
                layoutImage(app, elem, y, startX, maxWidth);
                continue;
            }

            case ElementType::Ruby: {
                // Collect base text and ruby annotation text
                std::wstring baseText, rubyText;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::RubyText) {
                        for (const auto& rtChild : child->children) {
                            if (rtChild->type == ElementType::Text) {
                                rubyText += toWide(rtChild->text);
                            }
                        }
                    } else if (child->type == ElementType::Text) {
                        baseText += toWide(child->text);
                    }
                }
                if (baseText.empty()) continue;

                float rubyFontSize = baseFormat->GetFontSize() * 0.5f;
                float rubyLineHeight = rubyFontSize * 1.4f;

                // Measure base text
                size_t rubyDocStart = app.docText.size();
                LayoutInfo baseInfo = createLayout(app, baseText, baseFormat, lineHeight, app.bodyTypography);
                float baseWidth = baseInfo.width;

                // Measure ruby text
                LayoutInfo rubyInfo = {nullptr, 0.0f};
                float rubyWidth = 0.0f;
                if (!rubyText.empty()) {
                    rubyInfo = createLayout(app, rubyText, baseFormat, rubyLineHeight, app.bodyTypography);
                    if (rubyInfo.layout) {
                        // Set smaller font size on the ruby layout
                        DWRITE_TEXT_RANGE range = {0, (UINT32)rubyText.length()};
                        rubyInfo.layout->SetFontSize(rubyFontSize, range);
                        DWRITE_TEXT_METRICS metrics{};
                        rubyInfo.layout->GetMetrics(&metrics);
                        rubyWidth = metrics.widthIncludingTrailingWhitespace;
                    }
                }

                float totalWidth = std::max(baseWidth, rubyWidth);

                // Word-wrap: treat ruby as atomic
                if (x + totalWidth > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }

                // We need extra space above for the ruby text
                float rubyAboveOffset = rubyLineHeight;
                // If we're at the start of a line, push y down to make room for annotation
                // For simplicity, always reserve space above
                float baseY = y + mathTextOffset + rubyAboveOffset;

                // Center the narrower one under the wider one
                float basePosX = x + (totalWidth - baseWidth) / 2.0f;
                float rubyPosX = x + (totalWidth - rubyWidth) / 2.0f;

                // Draw ruby annotation (above base text, not selectable)
                if (rubyInfo.layout) {
                    D2D1_COLOR_F rubyColor = baseColor;
                    rubyColor.a *= 0.7f;
                    D2D1_POINT_2F rubyPos = D2D1::Point2F(rubyPosX, y + mathTextOffset);
                    D2D1_RECT_F rubyBounds = D2D1::RectF(rubyPosX, y + mathTextOffset, rubyPosX + rubyWidth, y + mathTextOffset + rubyLineHeight);
                    addTextRun(app, std::move(rubyInfo), rubyPos, rubyBounds, rubyColor, 0, 0, false);
                }

                if (hasUnderline) {
                    addUnderline(run.style, basePosX, basePosX + baseWidth, y,
                                 baseY + std::max(0.0f, layoutBaseline(baseInfo.layout)), color);
                }

                // Draw base text (selectable)
                D2D1_POINT_2F basePos = D2D1::Point2F(basePosX, baseY);
                D2D1_RECT_F baseBounds = D2D1::RectF(basePosX, baseY, basePosX + baseWidth, baseY + lineHeight);
                addTextRun(app, std::move(baseInfo), basePos, baseBounds, baseColor,
                           rubyDocStart, baseText.length(), true);

                app.docText += baseText;
                x += totalWidth + spaceWidth;

                // Adjust y to account for the extra ruby height on the next line wrap
                // The total height is rubyAboveOffset + lineHeight but we only advance by lineHeight
                // at the end of the line, so we need to make sure the ruby doesn't overlap
                continue;
            }

            default:
                continue;  // flattenInline only ever hands us leaves
        }

        if (text.empty()) continue;

        const float runWidth = std::max(0.0f, maxWidth - (wrappedCode ? kCodeSpanPadding : 0.0f));
        const float runMaxX = startX + runWidth;
        IDWriteTypography* typography =
            page::typography(app, run.style.css, wrappedCode ? app.codeTypography : app.bodyTypography);
        const float spacing = page::letterSpacing(app, run.style.css);
        size_t textDocStart = app.docText.size();
        float linkLineStartX = x;
        float linkLineY = y;

        // Measure the whole element once: cluster metrics give every break
        // unit's width without creating one IDWriteTextLayout per unit.
        // The measurement layout is kept alive: when the element ends up as
        // a single segment (no wrap — the common case), it IS the segment's
        // layout and no second CreateTextLayout happens.
        std::vector<float> cumW(text.length() + 1, -1.0f);
        std::vector<bool> isBoundary(text.length() + 1, false);
        cumW[0] = 0.0f;
        isBoundary[0] = true;
        isBoundary[text.length()] = true;
        LayoutInfo measureInfo = createLayout(app, text, format, lineHeight, typography, spacing);
        {
            if (measureInfo.layout) {
                UINT32 clusterCount = 0;
                measureInfo.layout->GetClusterMetrics(nullptr, 0, &clusterCount);
                if (clusterCount > 0) {
                    std::vector<DWRITE_CLUSTER_METRICS> clusters(clusterCount);
                    if (SUCCEEDED(measureInfo.layout->GetClusterMetrics(
                            clusters.data(), clusterCount, &clusterCount))) {
                        size_t cpos = 0;
                        float w = 0.0f;
                        for (UINT32 ci = 0; ci < clusterCount && cpos < text.length(); ci++) {
                            w += clusters[ci].width;
                            cpos += clusters[ci].length;
                            if (cpos <= text.length()) {
                                cumW[cpos] = w;
                                isBoundary[cpos] = true;
                            }
                        }
                    }
                }
            }
            // Fill positions that fall inside clusters (break opportunities
            // never land there)
            float last = 0.0f;
            for (auto& v : cumW) { if (v < 0.0f) v = last; else last = v; }
        }
        auto widthOf = [&cumW](size_t a, size_t b) { return cumW[b] - cumW[a]; };

        // Words on the same line merge into one drawn layout; per-word rects
        // are still recorded so hit-testing/selection/search stay word-level.
        struct WordRef { size_t start, len; };
        std::vector<WordRef> segWords;
        size_t segStart = 0;
        float segX = 0.0f;
        bool segOpen = false;
        float lastWordEndX = linkLineStartX;

        auto flushSegment = [&](size_t segEnd) {
            if (!segOpen) return;
            segOpen = false;
            if (segEnd <= segStart) { segWords.clear(); return; }
            std::wstring_view segText(text.data() + segStart, segEnd - segStart);

            // Single-segment element: the measurement layout already shaped
            // exactly this text (a space-only tail draws nothing) — hand it
            // over instead of shaping a second time
            bool coversAll = segStart == 0 && measureInfo.layout != nullptr;
            for (size_t i = segEnd; coversAll && i < text.length(); i++) {
                if (text[i] != L' ') coversAll = false;
            }
            LayoutInfo info;
            if (coversAll) {
                info = measureInfo;
                measureInfo.layout = nullptr;
            } else {
                info = createLayout(app, segText, format, lineHeight, typography, spacing);
            }
            float segWidth = widthOf(segStart, segEnd);
            if (wrappedCode) {
                const D2D1_COLOR_F codeBox = run.style.codeBox.value_or(themeInlineCodeBackground(app.theme));
                if (codeBox.a > 0.0f) {
                    app.layoutRects.push_back({
                        D2D1::RectF(segX - 2, y + mathTextOffset, segX + segWidth + kCodeSpanPadding,
                                    y + mathTextOffset + normalLineHeight),
                        codeBox});
                }
            } else if (hasBg) {
                addHighlight(D2D1::RectF(segX - 2, y + mathTextOffset + 1, segX + segWidth + 2,
                                         y + mathTextOffset + normalLineHeight - 1),
                             bgColor, bgSpan);
            }
            if (hasStrike) {
                float strikeY = y + mathTextOffset + normalLineHeight * 0.55f;
                app.layoutLines.push_back({D2D1::Point2F(segX, strikeY),
                                           D2D1::Point2F(segX + segWidth, strikeY),
                                           color, 1.0f});
            }
            if (hasUnderline) {
                addUnderline(run.style, segX, segX + segWidth, y, y + drawYOffset + baselineOf(format),
                             color);
            }
            // A run that draws fallback glyphs (Chinese beside a Latin
            // font, emoji) gets a taller line ascent from DirectWrite; put
            // it back on its format's baseline like every other run (#259)
            float segTop = drawYOffset + baselineOf(format) -
                           runBaseline(app, info.layout, format);
            D2D1_POINT_2F segPos = D2D1::Point2F(segX, y + segTop);
            D2D1_RECT_F segBounds = D2D1::RectF(segX, y, segX + segWidth, y + lineHeight);
            size_t runsBefore = app.layoutTextRuns.size();
            addTextRun(app, std::move(info), segPos, segBounds, color,
                       textDocStart + segStart, segEnd - segStart, false);
            // Word rects hit-test against the covering segment layout
            size_t segRun = app.layoutTextRuns.size() > runsBefore
                ? app.layoutTextRuns.size() - 1 : (size_t)-1;
            for (const auto& w : segWords) {
                float wx = segX + widthOf(segStart, w.start);
                D2D1_RECT_F wb = D2D1::RectF(wx, y, wx + widthOf(w.start, w.start + w.len),
                                             y + lineHeight);
                addTextRect(app, wb, textDocStart + w.start, w.len, segRun);
            }
            segWords.clear();
        };

        // Break opportunities from the Unicode line breaking algorithm:
        // spaces for Latin text, between-ideograph positions (with proper
        // punctuation rules) for CJK, after / and - inside URLs, etc.
        std::vector<bool> canBreak;
        analyzeBreakOpportunities(app, text, canBreak);

        size_t pos = 0;
        size_t lastWordEnd = 0;

        auto emitWord = [&](size_t wordStart, size_t wordEnd) {
            float wordWidth = widthOf(wordStart, wordEnd);
            float wordX = segOpen ? segX + widthOf(segStart, wordStart) : x;

            if (wordX + wordWidth > runMaxX && wordX > startX) {
                // Unit wraps: flush the current line's segment first
                flushSegment(lastWordEnd);
                if (isLink && lastWordEndX > linkLineStartX) {
                    addLinkSegment(linkLineStartX, lastWordEndX, linkLineY, linkUrl, color);
                }
                x = startX;
                y += lineHeight;
                linkLineStartX = x;
                linkLineY = y;
                wordX = x;
            }

            if (!segOpen) {
                segOpen = true;
                segStart = wordStart;
                segX = wordX;
            }
            segWords.push_back({wordStart, wordEnd - wordStart});
            lastWordEnd = wordEnd;
            lastWordEndX = segX + widthOf(segStart, wordEnd);
            x = lastWordEndX;
        };

        while (pos < text.length()) {
            // The next unit runs to the following break opportunity
            size_t bp = pos + 1;
            while (bp < text.length() && (!canBreak[bp] || !isBoundary[bp])) bp++;

            // Trailing spaces belong to the unit but don't participate in
            // the wrap decision and never render at a line start
            size_t vis = bp;
            while (vis > pos && text[vis - 1] == L' ') vis--;

            if (vis > pos) {
                if (widthOf(pos, vis) > runWidth) {
                    // Emergency break: a single unbreakable unit wider than a
                    // whole line splits at cluster boundaries so nothing is
                    // ever clipped or overlaps a neighbor
                    size_t pieceStart = pos;
                    while (widthOf(pieceStart, vis) > runWidth) {
                        size_t lo = pieceStart + 1, hi = vis - 1;
                        while (lo < hi) {
                            size_t mid = (lo + hi + 1) / 2;
                            if (widthOf(pieceStart, mid) <= runWidth) lo = mid;
                            else hi = mid - 1;
                        }
                        size_t pieceEnd = lo;
                        while (pieceEnd > pieceStart && !isBoundary[pieceEnd]) pieceEnd--;
                        if (pieceEnd == pieceStart) {
                            // Even when one cluster is wider than the line,
                            // never split a surrogate pair or combining mark.
                            pieceEnd = pieceStart + 1;
                            while (pieceEnd < vis && !isBoundary[pieceEnd]) pieceEnd++;
                        }
                        emitWord(pieceStart, pieceEnd);
                        pieceStart = pieceEnd;
                        if (pieceStart >= vis) break;
                    }
                    if (pieceStart < vis) emitWord(pieceStart, vis);
                } else {
                    emitWord(pos, vis);
                }
            }

            if (bp > vis) {
                // Advance x across the trailing spaces (shaped width when a
                // segment is open)
                x = segOpen ? segX + widthOf(segStart, bp) : x + widthOf(vis, bp);
            }
            pos = bp;
        }
        flushSegment(lastWordEnd);
        if (measureInfo.layout) measureInfo.layout->Release();

        app.docText += text;
        markStyles(app, run.style, isLink, linkUrl, textDocStart, app.docText.size());

        if (isLink && lastWordEndX > linkLineStartX) {
            addLinkSegment(linkLineStartX, lastWordEndX, linkLineY, linkUrl, color);
        }
        if (wrappedCode) x += kCodeSpanPadding;
    }

    y += lineHeight;
}

static void layoutCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);

// Display math ($$...$$, #80): a centered block rendered natively; the raw
// TeX joins docText so search and selection see the source
static void layoutMathBlock(App& app, const ElementPtr& elem, float& y,
                            float indent, float maxWidth) {
    std::wstring latex = toWide(elem->text);
    float baseSize = app.textFormat ? app.textFormat->GetFontSize() : 16.0f;
    MathBoxPtr box = mathParse(app, mathResolveRefs(latex, app.mathLabels), baseSize * 1.06f, true);
    if (!box) {
        // Unsupported TeX: raw source in code style (the mermaid pattern)
        auto fallback = std::make_shared<Element>(ElementType::CodeBlock);
        auto text = std::make_shared<Element>(ElementType::Text);
        text->text = elem->text;
        text->parent = fallback.get();
        fallback->children.push_back(std::move(text));
        fallback->sourceOffset = elem->sourceOffset;
        layoutCodeBlock(app, fallback, y, indent, maxWidth);
        return;
    }
    float scale = app.contentScale * app.zoomFactor;
    y += 6 * scale;
    // Its \label names, {#eq:id} among them, are places @eq:id and
    // #eq:id links scroll to (Scholar)
    for (size_t at = elem->text.find("\\label{"); at != std::string::npos;
         at = elem->text.find("\\label{", at + 7)) {
        const size_t close = elem->text.find('}', at + 7);
        if (close == std::string::npos) break;
        app.footnoteAnchors[elem->text.substr(at + 7, close - at - 7)] = y;
    }
    float w = mathBoxWidth(box);
    float x = indent + std::max(0.0f, (maxWidth - w) / 2);
    // Its \tag's number at the line's right (#314). The equation moves off
    // the middle to keep an em clear of it, and with no room for both on
    // one line the number goes under it, at the right.
    float below = 0;
    if (MathBoxPtr tag = mathBoxTag(box)) {
        const float tagX = indent + std::max(0.0f, maxWidth - mathBoxWidth(tag));
        if (x + w + baseSize > tagX) x = std::max(indent, tagX - baseSize - w);
        const bool under = x + w + baseSize > tagX;
        const float tagY = under ? y + mathBoxHeight(box) : y + mathBoxBaseline(box) - mathBoxBaseline(tag);
        mathBoxRetain(app, tag, tagX, tagY, app.theme.text);
        if (under) below = mathBoxHeight(tag);
    }
    mathBoxRetain(app, box, x, y, app.theme.text);

    size_t docStart = app.docText.size();
    app.docText += latex;
    addTextRect(app, D2D1::RectF(x, y, x + w, y + mathBoxHeight(box)),
                docStart, latex.size());
    app.docText += L"\n\n";

    y += mathBoxHeight(box) + below + 14 * scale;
}

// Nothing to draw between equations: blanks and line breaks
static bool blankInline(const ElementPtr& child) {
    if (!child || child->type == ElementType::SoftBreak) return true;
    if (child->type != ElementType::Text) return false;
    for (char c : child->text) {
        if (!isspace((unsigned char)c)) return false;
    }
    return true;
}

static bool isAnchorOnlyContent(const std::vector<ElementPtr>& children) {
    bool anchor = false;
    for (const auto& child : children) {
        if (!child) continue;
        if (child->type == ElementType::Anchor && child->children.empty()) {
            anchor = true;
        } else if (child->type == ElementType::Text) {
            for (char c : child->text) {
                if (!isspace((unsigned char)c)) return false;
            }
        } else if (child->type != ElementType::SoftBreak) {
            return false;
        }
    }
    return anchor;
}

// Inline content of nothing but empty anchors (<a id="x"></a> on its own
// line above a heading) only marks link targets. Like a browser's empty
// paragraph it takes no room; the targets pin to where it would start (#255).
static bool pinAnchorsOnly(App& app, const std::vector<ElementPtr>& children, float y) {
    if (!isAnchorOnlyContent(children)) return false;
    for (const auto& child : children) {
        if (child && child->type == ElementType::Anchor) {
            app.htmlAnchors.push_back({child->title, y});
        }
    }
    return true;
}

static void layoutInlineRun(App& app, std::vector<ElementPtr>& run, float& y, float indent, float maxWidth);

// What an element reads as, its inline children's text run together
static std::string plainText(const ElementPtr& elem) {
    if (elem->type == ElementType::Text) return elem->text;
    if (elem->type == ElementType::SoftBreak) return " ";
    std::string out;
    for (const auto& child : elem->children) out += plainText(child);
    return out;
}


static void layoutParagraph(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    // A $$...$$ equation stands on its own line, centered, wherever it sits
    // in the paragraph: two in a row ran on as one line of text (#314). The
    // words before, between and after them make lines of their own.
    if (std::any_of(elem->children.begin(), elem->children.end(),
                    [](const ElementPtr& c) { return c && c->type == ElementType::MathDisplay; })) {
        std::vector<ElementPtr> words;
        auto flush = [&] {
            while (!words.empty() && blankInline(words.front())) words.erase(words.begin());
            while (!words.empty() && blankInline(words.back())) words.pop_back();
            layoutInlineRun(app, words, y, indent, maxWidth);
        };
        for (const auto& child : elem->children) {
            if (child && child->type == ElementType::MathDisplay) {
                flush();
                layoutMathBlock(app, child, y, indent, maxWidth);
            } else {
                words.push_back(child);
            }
        }
        flush();
        return;
    }
    if (pinAnchorsOnly(app, elem->children, y)) return;
    // A CSS theme's paragraph (page_style.h): its margins, box, face,
    // colour, line height, first-line indent and alignment
    const page::Style& css = page::current(app);
    auto format = elem->language == "footnote-backlinks" && app.supSubFormat ? app.supSubFormat
                                                                             : page::format(app, css, app.textFormat);
    const D2D1_COLOR_F color = page::textColor(css, app.theme.text);
    y += page::before(app, 0);
    BlockBox box(app, css, y, indent, maxWidth);
    const LayoutSnapshot start = takeSnapshot(app);
    const float textIndent = css.has(css::Prop::TextIndent) ? page::px(app, css->textIndent) : 0.0f;
    layoutInlineContent(app, elem->children, indent, y, maxWidth, format, color, {},
                        page::lineHeight(app, css, format, 0.0f), textIndent != 0.0f ? indent + textIndent : -1.0f);
    alignLines(app, start, indent, maxWidth, css);
    box.close(y, color);
    app.docText += L"\n\n";
    y += page::after(app, 14);
}

// Inline content outside any paragraph, the bare text of a raw HTML block
// or <blockquote>, lays out as a paragraph would. Empties the run.
static void layoutInlineRun(App& app, std::vector<ElementPtr>& run, float& y, float indent, float maxWidth) {
    if (run.empty()) return;
    if (!pinAnchorsOnly(app, run, y)) {
        // The block the text sits in styles it
        const page::Style& css = page::current(app);
        IDWriteTextFormat* format = page::format(app, css, app.textFormat);
        const LayoutSnapshot start = takeSnapshot(app);
        layoutInlineContent(app, run, indent, y, maxWidth, format, page::textColor(css, app.theme.text), {},
                            page::lineHeight(app, css, format, 0.0f));
        alignLines(app, start, indent, maxWidth, css);
        app.docText += L"\n\n";
        y += page::after(app, 14);
    }
    run.clear();
}

static void layoutHeading(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    int levelIndex = std::min(elem->level - 1, 5);
    // A CSS theme's heading (page_style.h): margins, box, face, colour,
    // alignment, and a border under it in place of Tinta's rule
    const page::Style& css = page::current(app);
    IDWriteTextFormat* format = page::format(
        app, css, app.headingFormats[levelIndex] ? app.headingFormats[levelIndex] : app.textFormat);
    const D2D1_COLOR_F color = page::textColor(css, themeHeadingColor(app.theme, elem->level));
    const bool themedRule = css.has(css::Prop::BorderBottomWidth) || css.has(css::Prop::BorderBottomStyle);

    y += page::before(app, elem->level == 1 ? 16.0f : 20.0f);
    BlockBox box(app, css, y, indent, maxWidth);

    // All Markdown heading levels participate in Contents and anchor navigation (#210).
    // A section number (Scholar) shows in Contents but stays out of the slug,
    // and an id given as {#id} is the heading's own
    if (elem->level >= 1 && elem->level <= 6) {
        std::wstring headingText, slugText;
        std::function<void(const ElementPtr&)> extract = [&](const ElementPtr& e) {
            if (!e) return;
            if (e->type == ElementType::Text) {
                headingText += toWide(e->text);
                if (e->language != "section-number") slugText += toWide(e->text);
            } else {
                for (const auto& c : e->children) extract(c);
            }
        };
        for (const auto& child : elem->children) extract(child);

        std::string id = elem->title;
        if (id.empty()) {
            slugText.erase(0, slugText.find_first_not_of(L' '));  // the space after a number
            std::string baseId = slugifyHeading(slugText);
            int& n = app.headingSlugCounts[baseId];
            id = (n == 0) ? baseId : (baseId + "-" + std::to_string(n));
            n++;
        }
        app.headings.push_back({headingText, elem->level, y, id});
    }

    const LayoutSnapshot start = takeSnapshot(app);
    layoutInlineContent(app, elem->children, indent, y, maxWidth, format, color, {},
                        page::lineHeight(app, css, format, 0.0f));
    alignLines(app, start, indent, maxWidth, css);
    box.close(y, color);

    if (elem->level <= 2 && app.headingRules && !themedRule) {
        y += 6 * scale;
        D2D1_COLOR_F lineColor = themeHeadingColor(app.theme, elem->level);
        lineColor.a = 0.3f;
        float lineWidth = (elem->level == 1) ? 2.0f * scale : 1.0f * scale;
        app.layoutLines.push_back({D2D1::Point2F(indent, y),
                                   D2D1::Point2F(indent + maxWidth, y),
                                   lineColor, lineWidth});
        y += lineWidth;
    }

    app.docText += L"\n\n";
    y += page::after(app, 12);
}

static LayoutInfo createWrappedLayout(App& app, std::wstring_view text,
                                      IDWriteTextFormat* format,
                                      float width, float height) {
    LayoutInfo info;
    if (!format || text.empty() || width <= 0.0f || height <= 0.0f) return info;

    app.dwriteFactory->CreateTextLayout(
        text.data(), static_cast<UINT32>(text.length()),
        format, width, height, &info.layout);
    if (!info.layout) return info;

    info.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    info.layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    info.layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (app.fontFallback) {
        IDWriteTextLayout2* layout2 = nullptr;
        if (SUCCEEDED(info.layout->QueryInterface(
                __uuidof(IDWriteTextLayout2),
                reinterpret_cast<void**>(&layout2)))) {
            layout2->SetFontFallback(app.fontFallback);
            layout2->Release();
        }
    }

    DWRITE_TEXT_METRICS metrics{};
    info.layout->GetMetrics(&metrics);
    info.width = metrics.widthIncludingTrailingWhitespace;
    info.height = metrics.height;
    return info;
}

static D2D1_COLOR_F mermaidColor(const mermaid::Color& color) {
    return D2D1::ColorF(
        ((color.rgb >> 16) & 0xFF) / 255.0f,
        ((color.rgb >> 8) & 0xFF) / 255.0f,
        (color.rgb & 0xFF) / 255.0f,
        color.alpha);
}

struct ResolvedMermaidStyle {
    D2D1_COLOR_F fill{};
    D2D1_COLOR_F stroke{};
    D2D1_COLOR_F text{};
    float strokeWidth = 1.0f;
};

static ResolvedMermaidStyle resolveMermaidStyle(
        const App& app, const mermaid::Diagram& diagram,
        const mermaid::Node& node, float scale) {
    ResolvedMermaidStyle resolved;
    resolved.fill = app.theme.codeBackground;
    resolved.stroke = app.theme.accent;
    resolved.text = app.theme.text;
    resolved.strokeWidth = 1.5f * scale;

    auto apply = [&](const mermaid::Style& style) {
        if (style.hasFill) resolved.fill = mermaidColor(style.fill);
        if (style.hasStroke) resolved.stroke = mermaidColor(style.stroke);
        if (style.hasText) resolved.text = mermaidColor(style.text);
        if (style.hasStrokeWidth) {
            resolved.strokeWidth = style.strokeWidth * scale;
        }
    };

    auto defaultStyle = diagram.classStyles.find("default");
    if (defaultStyle != diagram.classStyles.end()) apply(defaultStyle->second);
    if (!node.className.empty()) {
        auto classStyle = diagram.classStyles.find(node.className);
        if (classStyle != diagram.classStyles.end()) apply(classStyle->second);
    }
    apply(node.style);
    return resolved;
}

static App::LayoutShapeType mermaidShapeType(mermaid::NodeShape shape) {
    switch (shape) {
        case mermaid::NodeShape::RoundedRectangle:
            return App::LayoutShapeType::RoundedRectangle;
        case mermaid::NodeShape::Diamond:
            return App::LayoutShapeType::Diamond;
        case mermaid::NodeShape::Stadium:
            return App::LayoutShapeType::Stadium;
        case mermaid::NodeShape::Circle:
            return App::LayoutShapeType::Ellipse;
        case mermaid::NodeShape::Hexagon:
            return App::LayoutShapeType::Hexagon;
        case mermaid::NodeShape::Asymmetric:  // a Path once its outline is built
        case mermaid::NodeShape::Rectangle:
        default:
            return App::LayoutShapeType::Rectangle;
    }
}

static ID2D1PathGeometry* buildPolygonGeometry(
        App& app, const std::vector<mermaidext::Point>& points,
        float originX, float originY);

static bool layoutMermaidDiagram(App& app, const std::string& source,
                                 size_t sourceOffset, float& y,
                                 float indent, float maxWidth,
                                 D2D1_RECT_F* renderedBounds = nullptr,
                                 float scaleMul = 1.0f) {
    auto parsed = mermaid::parse(source);
    if (!parsed.success || parsed.diagram.nodes.empty()) return false;

    const auto& diagram = parsed.diagram;
    float scale = app.contentScale * app.zoomFactor * scaleMul;
    float maxLabelWidth = 280.0f * scale;
    float measureHeight = 10000.0f * scale;
    float paddingX = 18.0f * scale;
    float paddingY = 12.0f * scale;
    float minimumWidth = 120.0f * scale;
    float minimumHeight = 52.0f * scale;

    std::vector<std::wstring> labels;
    std::vector<mermaid::Size> nodeSizes;
    std::vector<ResolvedMermaidStyle> styles;
    labels.reserve(diagram.nodes.size());
    nodeSizes.reserve(diagram.nodes.size());
    styles.reserve(diagram.nodes.size());

    for (const auto& node : diagram.nodes) {
        std::wstring label = toWide(node.label.empty() ? node.id : node.label);
        LayoutInfo measured = createWrappedLayout(
            app, label, app.textFormat, maxLabelWidth, measureHeight);
        float width = std::max(minimumWidth, measured.width + paddingX * 2.0f);
        float height = std::max(minimumHeight, measured.height + paddingY * 2.0f);
        if (measured.layout) measured.layout->Release();

        if (node.shape == mermaid::NodeShape::Diamond) {
            width = std::max(width * 1.28f, 150.0f * scale);
            height = std::max(height * 1.45f, 82.0f * scale);
        } else if (node.shape == mermaid::NodeShape::Hexagon) {
            width += 40.0f * scale;
        } else if (node.shape == mermaid::NodeShape::Circle) {
            float diameter = std::max(width, height);
            width = diameter;
            height = diameter;
        }
        width += mermaid::leftNotch(node.shape, height);

        labels.push_back(std::move(label));
        nodeSizes.push_back({width, height});
        styles.push_back(resolveMermaidStyle(app, diagram, node, scale));
    }

    struct MeasuredMermaidEdgeLabel {
        std::wstring text;
        float width = 0.0f;
        float height = 0.0f;
    };
    bool vertical = diagram.direction == mermaid::Direction::TopToBottom ||
                    diagram.direction == mermaid::Direction::BottomToTop;
    float labelPaddingX = 6.0f * scale;
    float labelPaddingY = 4.0f * scale;
    float rankGap = 78.0f * scale;
    std::vector<MeasuredMermaidEdgeLabel> edgeLabels(diagram.edges.size());
    for (size_t i = 0; i < diagram.edges.size(); i++) {
        if (diagram.edges[i].label.empty()) continue;

        auto& edgeLabel = edgeLabels[i];
        edgeLabel.text = toWide(diagram.edges[i].label);
        // A pixel of slack: laid out again at exactly its measured width, a
        // label could wrap its last letter onto a line of its own (#281)
        edgeLabel.width = std::min(
            180.0f * scale,
            std::max(60.0f * scale,
                     std::ceil(measureText(app, edgeLabel.text, app.textFormat)) + 1.0f +
                         labelPaddingX * 2.0f));
        LayoutInfo measured = createWrappedLayout(
            app, edgeLabel.text, app.textFormat,
            edgeLabel.width - labelPaddingX * 2.0f, measureHeight);
        // A word longer than the cap widens the label, up to twice the
        // cap, rather than break in the middle
        FLOAT wordWidth = 0.0f;
        if (measured.layout && SUCCEEDED(measured.layout->DetermineMinWidth(&wordWidth))) {
            const float needed = std::min(
                360.0f * scale, std::ceil(wordWidth) + 1.0f + labelPaddingX * 2.0f);
            if (needed > edgeLabel.width) {
                edgeLabel.width = needed;
                measured.layout->Release();
                measured = createWrappedLayout(
                    app, edgeLabel.text, app.textFormat,
                    edgeLabel.width - labelPaddingX * 2.0f, measureHeight);
            }
        }
        edgeLabel.height = std::max(
            28.0f * scale, measured.height + labelPaddingY * 2.0f);
        if (measured.layout) measured.layout->Release();

        float labelExtent = vertical ? edgeLabel.height : edgeLabel.width;
        rankGap = std::max(rankGap, labelExtent + 20.0f * scale);
    }

    mermaid::Layout graphLayout = mermaid::layout(
        diagram, nodeSizes, 32.0f * scale, rankGap);
    if (graphLayout.nodes.size() != diagram.nodes.size()) return false;

    float baseX = indent;
    if (graphLayout.width < maxWidth) {
        baseX += (maxWidth - graphLayout.width) * 0.5f;
    }
    float baseY = y + 10.0f * scale;
    if (!diagram.subgraphs.empty()) {
        // Group boxes extend above their topmost node by pad + title
        // height; reserve that room so they stay inside the block
        baseY += 30.0f * scale;
    }
    float diagramLeft = baseX;
    float diagramTop = baseY;
    float diagramRight = baseX + graphLayout.width;
    float diagramBottom = baseY + graphLayout.height;

    std::vector<D2D1_RECT_F> nodeRects;
    nodeRects.reserve(graphLayout.nodes.size());
    for (const auto& rect : graphLayout.nodes) {
        nodeRects.push_back(D2D1::RectF(
            baseX + rect.left,
            baseY + rect.top,
            baseX + rect.right,
            baseY + rect.bottom));
    }

    struct MermaidTextItem {
        std::wstring text;
        D2D1_RECT_F rect{};
        D2D1_COLOR_F color{};
    };
    std::vector<MermaidTextItem> textItems;
    textItems.reserve(diagram.nodes.size() + diagram.edges.size());

    D2D1_COLOR_F connectorColor = app.theme.text;
    connectorColor.a = app.theme.isDark ? 0.7f : 0.6f;

    // Subgraph group boxes: fill goes through layoutRects (drawn before the
    // connectors), the border rides with the node shapes, the title with the
    // node labels. Children were declared after their parents, so a reverse
    // pass finalizes leaves first and parents grow around them.
    if (!diagram.subgraphs.empty()) {
        float boxPad = 10.0f * scale;
        float titleHeight = 20.0f * scale;
        constexpr float kEmpty = std::numeric_limits<float>::max();
        std::vector<D2D1_RECT_F> boxes(
            diagram.subgraphs.size(),
            D2D1::RectF(kEmpty, kEmpty, -kEmpty, -kEmpty));
        auto grow = [](D2D1_RECT_F& box, const D2D1_RECT_F& rect) {
            box.left = std::min(box.left, rect.left);
            box.top = std::min(box.top, rect.top);
            box.right = std::max(box.right, rect.right);
            box.bottom = std::max(box.bottom, rect.bottom);
        };
        for (size_t s = diagram.subgraphs.size(); s-- > 0;) {
            for (size_t node : diagram.subgraphs[s].nodes) {
                if (node < nodeRects.size()) grow(boxes[s], nodeRects[node]);
            }
            for (size_t c = s + 1; c < diagram.subgraphs.size(); c++) {
                if (diagram.subgraphs[c].parent == s &&
                    boxes[c].left != kEmpty) {
                    grow(boxes[s], boxes[c]);
                }
            }
            if (boxes[s].left == kEmpty) continue;
            boxes[s].left -= boxPad;
            boxes[s].top -= boxPad + titleHeight;
            boxes[s].right += boxPad;
            boxes[s].bottom += boxPad;
        }
        D2D1_COLOR_F groupFill = app.theme.codeBackground;
        groupFill.a *= 0.55f;
        D2D1_COLOR_F groupBorder = app.theme.text;
        groupBorder.a = 0.28f;
        D2D1_COLOR_F groupTitle = app.theme.text;
        groupTitle.a = 0.78f;
        for (size_t s = 0; s < diagram.subgraphs.size(); s++) {
            if (boxes[s].left == kEmpty) continue;
            app.layoutRects.push_back({boxes[s], groupFill});
            app.layoutShapes.push_back({
                App::LayoutShapeType::Rectangle,
                boxes[s],
                D2D1::ColorF(0, 0, 0, 0),
                groupBorder,
                1.2f * scale,
                0.0f,
            });
            D2D1_RECT_F titleRect = D2D1::RectF(
                boxes[s].left + 8.0f * scale, boxes[s].top + 2.0f * scale,
                boxes[s].right - 8.0f * scale,
                boxes[s].top + titleHeight + 2.0f * scale);
            textItems.push_back({toWide(diagram.subgraphs[s].label),
                                 titleRect, groupTitle});
            diagramLeft = std::min(diagramLeft, boxes[s].left);
            diagramTop = std::min(diagramTop, boxes[s].top);
            diagramRight = std::max(diagramRight, boxes[s].right);
            diagramBottom = std::max(diagramBottom, boxes[s].bottom);
        }
    }

    app.layoutConnectors.reserve(
        app.layoutConnectors.size() + diagram.edges.size());
    // A node's left side at mid-height, where a notch has its tip
    auto leftSide = [&](size_t node) {
        const auto& rect = nodeRects[node];
        return rect.left + mermaid::leftNotch(diagram.nodes[node].shape,
                                              rect.bottom - rect.top);
    };
    size_t exteriorLane = 0;
    std::vector<D2D1_RECT_F> placedLabelRects;
    placedLabelRects.reserve(diagram.edges.size());
    for (size_t edgeIndex = 0; edgeIndex < diagram.edges.size(); edgeIndex++) {
        const auto& edge = diagram.edges[edgeIndex];
        if (edge.from >= nodeRects.size() || edge.to >= nodeRects.size()) continue;

        const auto& from = nodeRects[edge.from];
        const auto& to = nodeRects[edge.to];
        App::LayoutConnector connector;
        connector.color = connectorColor;
        connector.stroke = 1.4f * scale * edge.strokeScale;
        connector.arrowSize = 8.0f * scale;
        connector.directed = edge.directed;
        connector.dashed = edge.dashed;

        float fromCenterX = (from.left + from.right) * 0.5f;
        float fromCenterY = (from.top + from.bottom) * 0.5f;
        float toCenterX = (to.left + to.right) * 0.5f;
        float toCenterY = (to.top + to.bottom) * 0.5f;
        bool selfLoop = edge.from == edge.to;

        // Edges that skip over intermediate ranks would cut straight through
        // the nodes between them (and drop their label onto whatever edge
        // happens to sit at the midpoint) — route those through an exterior
        // lane like back-edges instead
        bool skipsRanks = false;
        if (edge.from < graphLayout.ranks.size() &&
            edge.to < graphLayout.ranks.size()) {
            size_t fromRank = graphLayout.ranks[edge.from];
            size_t toRank = graphLayout.ranks[edge.to];
            skipsRanks = (fromRank < toRank ? toRank - fromRank
                                            : fromRank - toRank) > 1;
        }

        if (vertical) {
            bool topToBottom =
                diagram.direction == mermaid::Direction::TopToBottom;
            bool forward = !selfLoop && !skipsRanks &&
                (topToBottom ? toCenterY > fromCenterY : toCenterY < fromCenterY);
            if (selfLoop) {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(from.right, fromCenterY);
                D2D1_POINT_2F end = D2D1::Point2F(fromCenterX, from.bottom);
                float laneX = from.right + lane;
                float laneY = from.bottom + lane;
                connector.points = {
                    start,
                    D2D1::Point2F(laneX, start.y),
                    D2D1::Point2F(laneX, laneY),
                    D2D1::Point2F(end.x, laneY),
                    end,
                };
            } else if (forward) {
                D2D1_POINT_2F start = D2D1::Point2F(
                    fromCenterX, topToBottom ? from.bottom : from.top);
                D2D1_POINT_2F end = D2D1::Point2F(
                    toCenterX, topToBottom ? to.top : to.bottom);
                float middleY = (start.y + end.y) * 0.5f;
                connector.points = {
                    start,
                    D2D1::Point2F(start.x, middleY),
                    D2D1::Point2F(end.x, middleY),
                    end,
                };
            } else {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(from.right, fromCenterY);
                D2D1_POINT_2F end = D2D1::Point2F(to.right, toCenterY);
                float laneX = std::max(from.right, to.right) + lane;
                connector.points = {
                    start,
                    D2D1::Point2F(laneX, start.y),
                    D2D1::Point2F(laneX, end.y),
                    end,
                };
            }
        } else {
            bool leftToRight =
                diagram.direction == mermaid::Direction::LeftToRight;
            bool forward = !selfLoop && !skipsRanks &&
                (leftToRight ? toCenterX > fromCenterX : toCenterX < fromCenterX);
            if (selfLoop) {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(fromCenterX, from.bottom);
                D2D1_POINT_2F end = D2D1::Point2F(from.right, fromCenterY);
                float laneX = from.right + lane;
                float laneY = from.bottom + lane;
                connector.points = {
                    start,
                    D2D1::Point2F(start.x, laneY),
                    D2D1::Point2F(laneX, laneY),
                    D2D1::Point2F(laneX, end.y),
                    end,
                };
            } else if (forward) {
                D2D1_POINT_2F start = D2D1::Point2F(
                    leftToRight ? from.right : leftSide(edge.from), fromCenterY);
                D2D1_POINT_2F end = D2D1::Point2F(
                    leftToRight ? leftSide(edge.to) : to.right, toCenterY);
                float middleX = (start.x + end.x) * 0.5f;
                connector.points = {
                    start,
                    D2D1::Point2F(middleX, start.y),
                    D2D1::Point2F(middleX, end.y),
                    end,
                };
            } else {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(fromCenterX, from.bottom);
                D2D1_POINT_2F end = D2D1::Point2F(toCenterX, to.bottom);
                float laneY = std::max(from.bottom, to.bottom) + lane;
                connector.points = {
                    start,
                    D2D1::Point2F(start.x, laneY),
                    D2D1::Point2F(end.x, laneY),
                    end,
                };
            }
        }

        connector.bounds = D2D1::RectF(
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest());
        for (const auto& point : connector.points) {
            connector.bounds.left = std::min(connector.bounds.left, point.x);
            connector.bounds.top = std::min(connector.bounds.top, point.y);
            connector.bounds.right = std::max(connector.bounds.right, point.x);
            connector.bounds.bottom = std::max(connector.bounds.bottom, point.y);
        }
        connector.bounds.left -= connector.arrowSize;
        connector.bounds.top -= connector.arrowSize;
        connector.bounds.right += connector.arrowSize;
        connector.bounds.bottom += connector.arrowSize;
        diagramLeft = std::min(diagramLeft, connector.bounds.left);
        diagramTop = std::min(diagramTop, connector.bounds.top);
        diagramRight = std::max(diagramRight, connector.bounds.right);
        diagramBottom = std::max(diagramBottom, connector.bounds.bottom);
        app.layoutConnectors.push_back(std::move(connector));

        if (!edge.label.empty()) {
            const auto& edgeLabel = edgeLabels[edgeIndex];
            const auto& points = app.layoutConnectors.back().points;
            size_t middle = points.size() / 2;
            const auto& middleStart = points[middle - 1];
            const auto& middleEnd = points[middle];
            float centerX = (middleStart.x + middleEnd.x) * 0.5f;
            float centerY = (middleStart.y + middleEnd.y) * 0.5f;
            auto chipAt = [&](float cx, float cy) {
                return D2D1::RectF(
                    cx - edgeLabel.width * 0.5f,
                    cy - edgeLabel.height * 0.5f,
                    cx + edgeLabel.width * 0.5f,
                    cy + edgeLabel.height * 0.5f);
            };
            D2D1_RECT_F labelRect = chipAt(centerX, centerY);

            // Parallel exterior lanes sit only a few pixels apart, so their
            // midpoint chips stack on top of each other — slide an
            // overlapping chip along its own segment until it finds space
            bool segVertical = std::abs(middleEnd.x - middleStart.x) <
                               std::abs(middleEnd.y - middleStart.y);
            float stepX = segVertical ? 0.0f : edgeLabel.width + 8.0f * scale;
            float stepY = segVertical ? edgeLabel.height + 8.0f * scale : 0.0f;
            auto overlapsPlaced = [&](const D2D1_RECT_F& rect) {
                for (const auto& placed : placedLabelRects) {
                    if (rect.left < placed.right && rect.right > placed.left &&
                        rect.top < placed.bottom && rect.bottom > placed.top) {
                        return true;
                    }
                }
                return false;
            };
            for (int attempt = 1; attempt <= 8 && overlapsPlaced(labelRect); attempt++) {
                float direction = (attempt % 2 == 1) ? 1.0f : -1.0f;
                float magnitude = (float)((attempt + 1) / 2);
                labelRect = chipAt(centerX + stepX * direction * magnitude,
                                   centerY + stepY * direction * magnitude);
            }
            placedLabelRects.push_back(labelRect);
            // Draw the label as a visible chip on the edge — an invisible
            // background-colored pill erases the line under it, which makes
            // edges look disconnected and labels look like floating text
            D2D1_COLOR_F chipStroke = connectorColor;
            chipStroke.a *= 0.6f;
            app.layoutShapes.push_back({
                App::LayoutShapeType::RoundedRectangle,
                labelRect,
                app.theme.codeBackground,
                chipStroke,
                1.2f * scale,
                4.0f * scale,
            });
            diagramLeft = std::min(diagramLeft, labelRect.left);
            diagramTop = std::min(diagramTop, labelRect.top);
            diagramRight = std::max(diagramRight, labelRect.right);
            diagramBottom = std::max(diagramBottom, labelRect.bottom);
            D2D1_RECT_F textRect = D2D1::RectF(
                labelRect.left + labelPaddingX,
                labelRect.top + labelPaddingY,
                labelRect.right - labelPaddingX,
                labelRect.bottom - labelPaddingY);
            textItems.push_back({edgeLabel.text, textRect, app.theme.text});
        }
    }

    app.layoutShapes.reserve(app.layoutShapes.size() + diagram.nodes.size());
    for (size_t i = 0; i < diagram.nodes.size(); i++) {
        const auto& node = diagram.nodes[i];
        const auto& rect = nodeRects[i];
        const auto& style = styles[i];
        const float notch = mermaid::leftNotch(node.shape, rect.bottom - rect.top);

        App::LayoutShape shape = {
            mermaidShapeType(node.shape),
            rect,
            style.fill,
            style.stroke,
            style.strokeWidth,
            8.0f * scale,
        };
        if (notch > 0.0f) {
            // Built once in the shape's own space, so the screen and print
            // draw the same outline
            float w = rect.right - rect.left;
            float h = rect.bottom - rect.top;
            shape.geometry = buildPolygonGeometry(
                app, {{0.0f, 0.0f}, {w, 0.0f}, {w, h}, {0.0f, h}, {notch, h * 0.5f}},
                0.0f, 0.0f);
            if (shape.geometry) shape.type = App::LayoutShapeType::Path;
        }
        app.layoutShapes.push_back(shape);

        float insetX = paddingX;
        float insetY = paddingY;
        if (node.shape == mermaid::NodeShape::Diamond) {
            insetX = (rect.right - rect.left) * 0.18f;
            insetY = (rect.bottom - rect.top) * 0.18f;
        } else if (node.shape == mermaid::NodeShape::Hexagon) {
            insetX = (rect.right - rect.left) * 0.18f;
        }

        D2D1_RECT_F textRect = D2D1::RectF(
            rect.left + notch + insetX,
            rect.top + insetY,
            rect.right - insetX,
            rect.bottom - insetY);
        textItems.push_back({labels[i], textRect, style.text});

        if (sourceOffset != SIZE_MAX) {
            size_t anchorOffset = sourceOffset + node.sourceOffset;
            if (app.scrollAnchors.empty() ||
                app.scrollAnchors.back().sourceOffset < anchorOffset) {
                app.scrollAnchors.push_back({anchorOffset, rect.top});
            }
        }
    }

    std::stable_sort(
        textItems.begin(), textItems.end(),
        [](const MermaidTextItem& left, const MermaidTextItem& right) {
            if (std::abs(left.rect.top - right.rect.top) > kLineBucketTolerance) {
                return left.rect.top < right.rect.top;
            }
            return left.rect.left < right.rect.left;
        });
    for (const auto& item : textItems) {
        // A pixel of slack, centred: a label boxed at its measured width and
        // laid out again at exactly that width could wrap its last word
        LayoutInfo textLayout = createWrappedLayout(
            app, item.text, app.textFormat,
            item.rect.right - item.rect.left + 1.0f,
            item.rect.bottom - item.rect.top);
        size_t docStart = app.docText.size();
        app.docText += item.text;
        addTextRun(
            app, std::move(textLayout),
            D2D1::Point2F(item.rect.left - 0.5f, item.rect.top),
            item.rect, item.color, docStart, item.text.size(), true);
        app.docText += L"\n";
    }
    app.docText += L"\n";

    app.contentWidth = std::max(
        app.contentWidth,
        diagramRight + 40.0f * scale);
    if (app.focusMermaidOnNextLayout && !nodeRects.empty()) {
        std::vector<bool> hasIncoming(diagram.nodes.size(), false);
        for (const auto& edge : diagram.edges) {
            if (edge.to < hasIncoming.size()) hasIncoming[edge.to] = true;
        }
        size_t rootIndex = 0;
        for (size_t i = 0; i < hasIncoming.size(); i++) {
            if (!hasIncoming[i]) {
                rootIndex = i;
                break;
            }
        }

        float rootCenter = (nodeRects[rootIndex].left + nodeRects[rootIndex].right) * 0.5f;
        float viewportWidth = documentViewportWidth(app);
        float maxScroll = std::max(0.0f, app.contentWidth - viewportWidth);
        float focusedScroll = rootCenter - viewportWidth * 0.5f;
        app.scrollX = std::max(0.0f, std::min(focusedScroll, maxScroll));
        app.targetScrollX = app.scrollX;
        app.focusMermaidOnNextLayout = false;
    }
    if (renderedBounds) {
        *renderedBounds =
            D2D1::RectF(diagramLeft, diagramTop, diagramRight, diagramBottom);
    }
    y = diagramBottom + 24.0f * scale;
    return true;
}

// --- extended Mermaid diagram families (sequence, pie, state, ...) ---

static void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float maxC = std::max(r, std::max(g, b));
    float minC = std::min(r, std::min(g, b));
    l = (maxC + minC) * 0.5f;
    if (maxC == minC) {
        h = 0.0f;
        s = 0.0f;
        return;
    }
    float d = maxC - minC;
    s = l > 0.5f ? d / (2.0f - maxC - minC) : d / (maxC + minC);
    if (maxC == r) {
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    } else if (maxC == g) {
        h = (b - r) / d + 2.0f;
    } else {
        h = (r - g) / d + 4.0f;
    }
    h /= 6.0f;
}

static float hueChannel(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

static void hslToRgb(float h, float s, float l, float& r, float& g, float& b) {
    if (s == 0.0f) {
        r = g = b = l;
        return;
    }
    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    r = hueChannel(p, q, h + 1.0f / 3.0f);
    g = hueChannel(p, q, h);
    b = hueChannel(p, q, h - 1.0f / 3.0f);
}

// Categorical palette derived from the theme accent: golden-angle hue steps
// keep neighboring series distinct in any theme
static D2D1_COLOR_F diagramSeriesColor(const App& app, int index) {
    float h, s, l;
    rgbToHsl(app.theme.accent.r, app.theme.accent.g, app.theme.accent.b,
             h, s, l);
    h = std::fmod(h + index * 0.38197f, 1.0f);
    s = app.theme.isDark ? 0.42f : 0.48f;
    l = app.theme.isDark ? 0.60f : 0.52f;
    float r, g, b;
    hslToRgb(h, s, l, r, g, b);
    return D2D1::ColorF(r, g, b, 1.0f);
}

D2D1_COLOR_F resolveDiagramRoleImpl(const App& app,
                                const mermaidext::Prim& prim,
                                mermaidext::Role role) {
    int seriesIndex = prim.seriesIndex;
    D2D1_COLOR_F color = app.theme.text;
    switch (role) {
        case mermaidext::Role::Text:
            color = app.theme.text;
            break;
        case mermaidext::Role::Muted:
            color = app.theme.text;
            color.a = app.theme.isDark ? 0.72f : 0.62f;
            break;
        case mermaidext::Role::Stroke:
            color = app.theme.accent;
            break;
        case mermaidext::Role::Fill:
            color = app.theme.codeBackground;
            break;
        case mermaidext::Role::Accent:
            color = app.theme.accent;
            break;
        case mermaidext::Role::AccentSoft:
            color = app.theme.accent;
            color.a = app.theme.isDark ? 0.20f : 0.13f;
            break;
        case mermaidext::Role::Background:
            color = app.theme.background;
            break;
        case mermaidext::Role::Series:
            color = diagramSeriesColor(app, seriesIndex);
            break;
        case mermaidext::Role::SeriesSoft:
            color = diagramSeriesColor(app, seriesIndex);
            color.a = 0.30f;
            break;
        case mermaidext::Role::Custom:
            color = D2D1::ColorF(prim.customR, prim.customG, prim.customB,
                                 prim.customA);
            break;
        case mermaidext::Role::None:
            color.a = 0.0f;
            break;
    }
    return color;
}

// Per-layout-pass cache of text formats for the diagram text styles
struct DiagramFormats {
    App& app;
    std::map<int, IDWriteTextFormat*> formats;

    explicit DiagramFormats(App& application) : app(application) {}
    DiagramFormats(const DiagramFormats&) = delete;
    DiagramFormats& operator=(const DiagramFormats&) = delete;
    ~DiagramFormats() {
        for (auto& entry : formats) {
            if (entry.second) entry.second->Release();
        }
    }

    IDWriteTextFormat* get(const mermaidext::TextStyle& style, float scale) {
        int key = (style.bold ? 1 : 0) | (style.italic ? 2 : 0) |
                  (style.mono ? 4 : 0) |
                  (static_cast<int>(style.scale * 100.0f + 0.5f) << 3);
        auto found = formats.find(key);
        if (found != formats.end()) return found->second;

        const wchar_t* family =
            style.mono ? app.theme.codeFontFamily : app.theme.fontFamily;
        float size = (style.mono ? 13.0f : 14.0f) * scale * style.scale;
        IDWriteTextFormat* format = nullptr;
        app.dwriteFactory->CreateTextFormat(
            fontfaces::name(family), fontfaces::collection(family),
            style.bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                       : DWRITE_FONT_WEIGHT_NORMAL,
            style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &format);
        formats[key] = format;
        return format;
    }
};

static IDWriteTextLayout* createDiagramTextLayout(
        App& app, DiagramFormats& formats, const std::wstring& text,
        const mermaidext::TextStyle& style, float scale, float width,
        float height, int alignH, int alignV) {
    IDWriteTextFormat* format = formats.get(style, scale);
    if (!format || text.empty()) return nullptr;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(
        text.data(), static_cast<UINT32>(text.size()), format,
        std::max(width, 1.0f), std::max(height, 1.0f), &layout);
    if (!layout) return nullptr;
    layout->SetTextAlignment(alignH < 0 ? DWRITE_TEXT_ALIGNMENT_LEADING
                             : alignH > 0 ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                          : DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(alignV < 0
                                      ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                  : alignV > 0 ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                               : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (app.fontFallback) {
        IDWriteTextLayout2* layout2 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(
                __uuidof(IDWriteTextLayout2),
                reinterpret_cast<void**>(&layout2)))) {
            layout2->SetFontFallback(app.fontFallback);
            layout2->Release();
        }
    }
    return layout;
}

static ID2D1PathGeometry* buildPolygonGeometry(
        App& app, const std::vector<mermaidext::Point>& points,
        float originX, float originY) {
    if (points.size() < 3) return nullptr;
    ID2D1PathGeometry* geometry = nullptr;
    if (FAILED(app.d2dFactory->CreatePathGeometry(&geometry)) || !geometry) {
        return nullptr;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || !sink) {
        geometry->Release();
        return nullptr;
    }
    sink->BeginFigure(
        D2D1::Point2F(points[0].x - originX, points[0].y - originY),
        D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < points.size(); i++) {
        sink->AddLine(
            D2D1::Point2F(points[i].x - originX, points[i].y - originY));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    sink->Release();
    return geometry;
}

static ID2D1PathGeometry* buildSliceGeometry(App& app, float radius,
                                             float a0, float a1) {
    // Local space: circle center at (radius, radius)
    ID2D1PathGeometry* geometry = nullptr;
    if (FAILED(app.d2dFactory->CreatePathGeometry(&geometry)) || !geometry) {
        return nullptr;
    }
    ID2D1GeometrySink* sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || !sink) {
        geometry->Release();
        return nullptr;
    }
    float cx = radius, cy = radius;
    D2D1_POINT_2F start =
        D2D1::Point2F(cx + radius * std::cos(a0), cy + radius * std::sin(a0));
    D2D1_POINT_2F end =
        D2D1::Point2F(cx + radius * std::cos(a1), cy + radius * std::sin(a1));
    float sweep = a1 - a0;
    if (sweep >= 6.28318f - 0.0001f) {
        // Full circle: two half arcs
        sink->BeginFigure(start, D2D1_FIGURE_BEGIN_FILLED);
        D2D1_POINT_2F opposite = D2D1::Point2F(
            cx + radius * std::cos(a0 + 3.14159f),
            cy + radius * std::sin(a0 + 3.14159f));
        D2D1_ARC_SEGMENT half1 = {
            opposite, {radius, radius}, 0.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL};
        D2D1_ARC_SEGMENT half2 = {
            start, {radius, radius}, 0.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL};
        sink->AddArc(half1);
        sink->AddArc(half2);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    } else {
        sink->BeginFigure(D2D1::Point2F(cx, cy), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(start);
        D2D1_ARC_SEGMENT arc = {
            end, {radius, radius}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
            sweep > 3.14159f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL};
        sink->AddArc(arc);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    sink->Close();
    sink->Release();
    return geometry;
}

static bool layoutMermaidExtDiagram(App& app, mermaidext::Kind kind,
                                    const std::string& source,
                                    size_t sourceOffset, float& y,
                                    float indent, float maxWidth,
                                    D2D1_RECT_F* renderedBounds,
                                    float scaleMul = 1.0f) {
    float scale = app.contentScale * app.zoomFactor * scaleMul;
    DiagramFormats formats(app);

    auto measureFn = [&](const std::string& text,
                         const mermaidext::TextStyle& style,
                         float wrapWidth) -> mermaidext::Size {
        if (text.empty()) return {};
        std::wstring wide = toWide(text);
        IDWriteTextLayout* layout = createDiagramTextLayout(
            app, formats, wide, style, scale,
            wrapWidth > 0.0f ? wrapWidth : kHugeWidth, kHugeWidth, -1, -1);
        if (!layout) return {};
        if (wrapWidth <= 0.0f) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        layout->Release();
        return {metrics.widthIncludingTrailingWhitespace, metrics.height};
    };

    mermaidext::Built built =
        mermaidext::build(kind, source, measureFn, scale);
    if (!built.ok || built.prims.empty()) return false;

    float baseX = indent;
    if (built.width < maxWidth) {
        baseX += (maxWidth - built.width) * 0.5f;
    }
    float baseY = y + 10.0f * scale;

    if (sourceOffset != SIZE_MAX) {
        if (app.scrollAnchors.empty() ||
            app.scrollAnchors.back().sourceOffset < sourceOffset) {
            app.scrollAnchors.push_back({sourceOffset, baseY});
        }
    }

    // Text prims are appended to docText in reading order so selection
    // sweeps the diagram top-to-bottom
    std::vector<const mermaidext::Prim*> textPrims;

    for (const auto& prim : built.prims) {
        float x1 = baseX + prim.x1, y1 = baseY + prim.y1;
        float x2 = baseX + prim.x2, y2 = baseY + prim.y2;
        D2D1_COLOR_F fill = resolveDiagramRoleImpl(app, prim, prim.fill);
        D2D1_COLOR_F stroke = resolveDiagramRoleImpl(app, prim, prim.stroke);
        switch (prim.type) {
            case mermaidext::PrimType::Rect:
            case mermaidext::PrimType::RoundRect:
            case mermaidext::PrimType::Ellipse: {
                App::LayoutShape shape;
                shape.type =
                    prim.type == mermaidext::PrimType::Rect
                        ? App::LayoutShapeType::Rectangle
                    : prim.type == mermaidext::PrimType::RoundRect
                        ? App::LayoutShapeType::RoundedRectangle
                        : App::LayoutShapeType::Ellipse;
                shape.rect = D2D1::RectF(x1, y1, x2, y2);
                shape.fill = fill;
                shape.stroke = stroke;
                shape.strokeWidth = prim.strokeWidth;
                shape.radius = prim.radius;
                app.layoutShapes.push_back(shape);
                break;
            }
            case mermaidext::PrimType::Line: {
                App::LayoutConnector connector;
                connector.color = stroke;
                connector.stroke = prim.strokeWidth;
                connector.dashed = prim.dashed;
                connector.arrowSize = 8.0f * scale;
                connector.directed = prim.openArrow;
                float endX = x2, endY = y2;
                if (prim.arrow) {
                    // Filled triangle head: shorten the line, add a polygon
                    float dx = x2 - x1, dy = y2 - y1;
                    float length = std::sqrt(dx * dx + dy * dy);
                    if (length > 0.01f) {
                        dx /= length;
                        dy /= length;
                        float head = 9.0f * scale;
                        float wing = 4.5f * scale;
                        endX = x2 - dx * head * 0.7f;
                        endY = y2 - dy * head * 0.7f;
                        std::vector<mermaidext::Point> tri = {
                            {x2, y2},
                            {x2 - dx * head + dy * wing,
                             y2 - dy * head - dx * wing},
                            {x2 - dx * head - dy * wing,
                             y2 - dy * head + dx * wing},
                        };
                        float minX = std::min({tri[0].x, tri[1].x, tri[2].x});
                        float minY = std::min({tri[0].y, tri[1].y, tri[2].y});
                        float maxX = std::max({tri[0].x, tri[1].x, tri[2].x});
                        float maxY = std::max({tri[0].y, tri[1].y, tri[2].y});
                        App::LayoutShape triangle;
                        triangle.type = App::LayoutShapeType::Path;
                        triangle.rect = D2D1::RectF(minX, minY, maxX, maxY);
                        triangle.fill = stroke;
                        triangle.stroke.a = 0.0f;
                        triangle.geometry =
                            buildPolygonGeometry(app, tri, minX, minY);
                        app.layoutShapes.push_back(triangle);
                    }
                }
                connector.points = {D2D1::Point2F(x1, y1),
                                    D2D1::Point2F(endX, endY)};
                connector.bounds = D2D1::RectF(
                    std::min(x1, x2) - 10.0f * scale,
                    std::min(y1, y2) - 10.0f * scale,
                    std::max(x1, x2) + 10.0f * scale,
                    std::max(y1, y2) + 10.0f * scale);
                app.layoutConnectors.push_back(std::move(connector));
                break;
            }
            case mermaidext::PrimType::Polygon: {
                if (prim.pts.size() < 3) break;
                std::vector<mermaidext::Point> shifted = prim.pts;
                float minX = shifted[0].x, minY = shifted[0].y;
                float maxX = minX, maxY = minY;
                for (auto& point : shifted) {
                    point.x += baseX;
                    point.y += baseY;
                }
                minX = maxX = shifted[0].x;
                minY = maxY = shifted[0].y;
                for (const auto& point : shifted) {
                    minX = std::min(minX, point.x);
                    minY = std::min(minY, point.y);
                    maxX = std::max(maxX, point.x);
                    maxY = std::max(maxY, point.y);
                }
                App::LayoutShape shape;
                shape.type = App::LayoutShapeType::Path;
                shape.rect = D2D1::RectF(minX, minY, maxX, maxY);
                shape.fill = fill;
                shape.stroke = stroke;
                shape.strokeWidth = prim.strokeWidth;
                shape.geometry = buildPolygonGeometry(app, shifted, minX, minY);
                app.layoutShapes.push_back(shape);
                break;
            }
            case mermaidext::PrimType::Slice: {
                App::LayoutShape shape;
                shape.type = App::LayoutShapeType::Path;
                shape.rect = D2D1::RectF(
                    x1 - prim.radius, y1 - prim.radius,
                    x1 + prim.radius, y1 + prim.radius);
                shape.fill = fill;
                shape.stroke = stroke;
                shape.strokeWidth = prim.strokeWidth;
                shape.geometry =
                    buildSliceGeometry(app, prim.radius, prim.a0, prim.a1);
                app.layoutShapes.push_back(shape);
                break;
            }
            case mermaidext::PrimType::Text:
                textPrims.push_back(&prim);
                break;
        }
    }

    std::stable_sort(textPrims.begin(), textPrims.end(),
                     [](const mermaidext::Prim* a, const mermaidext::Prim* b) {
                         if (std::abs(a->y1 - b->y1) > kLineBucketTolerance) {
                             return a->y1 < b->y1;
                         }
                         return a->x1 < b->x1;
                     });
    for (const mermaidext::Prim* prim : textPrims) {
        std::wstring wide = toWide(prim->text);
        if (wide.empty()) continue;
        D2D1_RECT_F rect = D2D1::RectF(baseX + prim->x1, baseY + prim->y1,
                                       baseX + prim->x2, baseY + prim->y2);
        // Slight width slack: boxes are sized from measured text, and
        // re-wrapping at the exact measured width can push the last word
        // (or a CJK character) onto a phantom second line
        IDWriteTextLayout* layout = createDiagramTextLayout(
            app, formats, wide, prim->style, scale,
            rect.right - rect.left + 2.5f, rect.bottom - rect.top + 2.0f,
            prim->alignH, prim->alignV);
        if (!layout) continue;
        LayoutInfo info;
        info.layout = layout;
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        info.width = metrics.widthIncludingTrailingWhitespace;
        info.height = metrics.height;
        size_t docStart = app.docText.size();
        app.docText += wide;
        addTextRun(app, std::move(info),
                   D2D1::Point2F(rect.left, rect.top), rect,
                   resolveDiagramRoleImpl(app, *prim, prim->fill),
                   docStart, wide.size(), true);
        app.docText += L"\n";
    }
    app.docText += L"\n";

    float diagramRight = baseX + built.width;
    float diagramBottom = baseY + built.height;
    app.contentWidth = std::max(app.contentWidth,
                                diagramRight + 40.0f * scale);
    if (renderedBounds) {
        *renderedBounds =
            D2D1::RectF(baseX, baseY, diagramRight, diagramBottom);
    }
    y = diagramBottom + 24.0f * scale;
    return true;
}

// A document's image is checked against the document's folder before it
// opens (network_path.h); Tinta's own files, such as a rendered PlantUML
// diagram, pass fromDocument = false
static App::ImageEntry& getOrLoadImage(App& app, const std::string& src,
                                       bool fromDocument = true);

// UTF-8 form of a wide string (the PlantUML preamble and the image cache
// key are narrow; render.cpp has no toUtf8 dependency)
static std::string wideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(),
                                        (int)s.size(), nullptr, 0, nullptr,
                                        nullptr);
    std::string out(len > 0 ? (size_t)len : 0, '\0');
    if (len > 0) {
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                            out.data(), len, nullptr, nullptr);
    }
    return out;
}

// "RRGGBB" (no '#') for skinparam defaultFontColor
static std::string colorHexNoHash(const D2D1_COLOR_F& c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02X%02X%02X",
             (int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f),
             (int)(c.b * 255.0f + 0.5f));
    return buf;
}

static void layoutCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    std::string code;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::Text) {
            code += child->text;
        }
    }

    std::string languageName = elem->language;
    std::transform(
        languageName.begin(), languageName.end(), languageName.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (languageName == "mermaid") {
        D2D1_RECT_F renderedBounds{};
        mermaidext::Kind kind = mermaidext::detectKind(code);
        unsigned fitKey = 0x80000000u | app.layoutDiagramSeq++;
        bool fitActive = fitBlockEnabled(app, fitKey);
        bool fitCandidate = false;
        bool rendered = false;
        LayoutSnapshot preSnap = takeSnapshot(app);
        float preY = y;
        float scaleMul = 1.0f;
        // Label re-wrapping makes width nonlinear in scale, so fitting
        // shrinks iteratively from the measured bounds of each pass
        for (int pass = 0; pass < 4; pass++) {
            if (kind == mermaidext::Kind::Flowchart) {
                rendered = layoutMermaidDiagram(
                    app, code, elem->sourceOffset, y, indent, maxWidth,
                    &renderedBounds, scaleMul);
            } else if (kind != mermaidext::Kind::None) {
                rendered = layoutMermaidExtDiagram(
                    app, kind, code, elem->sourceOffset, y, indent, maxWidth,
                    &renderedBounds, scaleMul);
            }
            if (!rendered) break;
            float renderedW = renderedBounds.right - indent;
            if (renderedW > maxWidth + 1.0f) {
                if (pass == 0) fitCandidate = true;
                if (fitActive && scaleMul > 0.15f) {
                    scaleMul = std::max(
                        0.15f, scaleMul * (maxWidth / renderedW) * 0.98f);
                    rollbackTo(app, preSnap);
                    y = preY;
                    continue;
                }
            }
            break;
        }
        if (rendered) {
            App::CodeBlockInfo info;
            info.bounds = renderedBounds;
            info.codeText = toWide(code);
            info.isDiagram = true;
            info.fitKey = fitKey;
            info.fitCandidate = fitCandidate || fitActive;
            info.fitActive = fitActive;
            app.codeBlocks.push_back(std::move(info));
            return;
        }
        rollbackTo(app, preSnap);
        y = preY;
    }

    // PlantUML fence: render through the configured tool. The interactive
    // path schedules off the UI thread; the print/PDF layout (which has no
    // message pump) is the single synchronous exception, bounded below.
    if (plantuml::isFenceLanguage(languageName)) {
        plantumlResolve(app);
        if (app.plantumlTool.available) {
            const std::wstring toolPath = app.plantumlTool.isJar
                                              ? app.plantumlTool.jar
                                              : app.plantumlTool.program;
            // Preamble from the live theme: family/size follow the mermaid
            // diagram conventions, the text color comes from the shared
            // role resolver so both renderers agree on the active theme.
            // Dark palettes additionally theme shape fills and strokes from
            // the same resolver (Fill = code surface, Stroke = accent);
            // light palettes keep their byte-identical five-line preamble.
            // The print layout swaps app.theme for the light Paper palette
            // before reaching here, so printed diagrams stay on that light
            // path by design.
            mermaidext::Prim colorPrim{};
            const std::string preambleText = plantuml::preamble(
                wideToUtf8(app.theme.fontFamily), 14.0f,
                colorHexNoHash(resolveDiagramRoleImpl(
                    app, colorPrim, mermaidext::Role::Text)),
                app.theme.isDark,
                colorHexNoHash(resolveDiagramRoleImpl(
                    app, colorPrim, mermaidext::Role::Fill)),
                colorHexNoHash(resolveDiagramRoleImpl(
                    app, colorPrim, mermaidext::Role::Stroke)));
            std::string sourceWithPreamble = code;
            // No @startuml anchor: nothing to render, fall through to the
            // existing source-code rendering unchanged.
            if (plantuml::injectPreamble(sourceWithPreamble,
                                        preambleText)) {
                const uint64_t key = plantuml::cacheKey(
                    code, preambleText, toolPath,
                    plantuml::toolStampFor(toolPath), 0 /*png*/);
                // The fit ordinal is taken whether or not the image has
                // arrived yet: a diagram still rendering must not shift
                // the fit state of the diagrams after it
                const unsigned fitKey = 0x80000000u | app.layoutDiagramSeq++;
                // Adopt finished renders on this (owner) thread before
                // deciding: a relayout may run before the posted
                // WM_APP_PLANTUML_READY is dispatched, and drainFinished()
                // is the ONLY cache mutation point - without it an
                // identical key misses and gets spawned again.
                if (app.plantumlQueue) app.plantumlQueue->drainFinished();
                auto hit = app.plantumlQueue
                               ? app.plantumlQueue->lookup(key)
                               : nullptr;

                // Lays the rendered PNG out as a diagram block: natural
                // size is the PNG's pixel size (PlantUML renders 1x, not
                // the mermaid 2x convention); scaling is exact math, so a
                // fit computes one scale instead of the 4-pass loop.
                auto layOutPng = [&](const std::wstring& pngPath) -> bool {
                    auto& image = getOrLoadImage(app, wideToUtf8(pngPath), false);
                    if (image.failed || !image.bitmap) return false;
                    const float scale = app.contentScale * app.zoomFactor;
                    const bool fitActive = fitBlockEnabled(app, fitKey);
                    const float renderedW = (float)image.width * scale;
                    const float renderedH = (float)image.height * scale;
                    const bool fitCandidate = renderedW > maxWidth;
                    float fitScale = 1.0f;
                    if (fitActive && fitCandidate && renderedW > 0.0f) {
                        fitScale = maxWidth / renderedW;
                    }
                    const D2D1_RECT_F bounds = D2D1::RectF(
                        indent, y, indent + renderedW * fitScale,
                        y + renderedH * fitScale);
                    app.layoutBitmaps.push_back({image.bitmap, bounds});
                    app.contentWidth = std::max(
                        app.contentWidth, bounds.right + 40.0f * scale);
                    y = bounds.bottom + 24.0f * scale;
                    App::CodeBlockInfo info;
                    info.bounds = bounds;
                    info.codeText = toWide(code);
                    info.isDiagram = true;
                    info.fitKey = fitKey;
                    info.fitCandidate = fitCandidate || fitActive;
                    info.fitActive = fitActive;
                    info.isPlantuml = true;
                    info.plantumlKey = key;
                    info.plantumlFormat = 0;
                    app.codeBlocks.push_back(std::move(info));
                    return true;
                };

                if (hit && hit->ok) {
                    if (layOutPng(hit->filePath)) return;
                } else if (!hit && app.plantumlPrintLayout &&
                           app.plantumlPrintBudgetMsLeft > 0) {
                    // Deliberate SYNC exception: the print/PDF layout runs
                    // under the light Paper palette (print.cpp swaps
                    // app.theme in), so this branch always renders the
                    // light, byte-stable preamble built above.
                    // without a message pump, so the async queue can never
                    // deliver there. Bounded per diagram by the shared
                    // budget; the interactive path never renders inline.
                    //
                    // A private print-sync subtree keeps these files away
                    // from the queue's own <workRoot>\<key> directories,
                    // where the worker may be mid-render for the same key
                    // on another thread (renderSync truncates its output).
                    // A pumpless flow (--printpages, --exportpdf) can reach
                    // this branch before the interactive path ever ran,
                    // but plantumlWorkRoot is computed by
                    // plantumlEnsureQueue(). Without it the root would be
                    // empty and the work dir would degenerate to a drive-
                    // relative \print-sync\<key>. Idempotent.
                    plantumlEnsureQueue(app);
                    const std::wstring workDir = app.plantumlWorkRoot +
                        L"\\print-sync\\" + plantuml::keyHex(key);
                    // The post-print relayout at screen width reuses this
                    // file instead of spawning the tool again; the tool
                    // stamp is part of the key, so an upgrade invalidates.
                    const std::wstring cachedPng = workDir + L"\\input.png";
                    if (GetFileAttributesW(cachedPng.c_str()) !=
                            INVALID_FILE_ATTRIBUTES &&
                        layOutPng(cachedPng)) {
                        return;
                    }
                    std::wstring outFile;
                    std::wstring error;
                    const DWORD budgetMs = (DWORD)std::min(
                        15000, app.plantumlPrintBudgetMsLeft);
                    const auto t0 = Clock::now();
                    const bool rendered = plantuml::renderSync(
                        app.plantumlTool, sourceWithPreamble, 0, workDir,
                        outFile, budgetMs, error, nullptr);
                    app.plantumlPrintBudgetMsLeft -=
                        (int)(usElapsed(t0) / 1000);
                    if (rendered && layOutPng(outFile)) return;
                } else if (!app.plantumlPrintLayout) {
                    // Interactive path: schedule off the UI thread.
                    // request() no-ops on a cache hit or a still-pending
                    // key, so a relayout pass never enqueues twice.
                    plantumlEnsureQueue(app);
                    if (app.plantumlQueue) {
                        app.plantumlQueue->request(elem->sourceOffset, key,
                                                   sourceWithPreamble, 0,
                                                   app.plantumlTool,
                                                   app.plantumlWorkRoot);
                    }
                }
            }
        }
    }

    std::wstring langHint = toWide(elem->language);
    int language = detectLanguage(langHint);

    float scale = app.contentScale * app.zoomFactor;
    // A CSS theme's block (page_style.h): padding, line height, colours,
    // corners and an edge
    using css::Prop;
    const page::Style& css = page::current(app);
    float lineHeight = page::lineHeight(app, css, app.codeFormat, 20.0f * scale);
    float pad[4] = {12.0f * scale, 12.0f * scale, 12.0f * scale, 12.0f * scale};
    for (int side = 0; side < 4; ++side) {
        if (css.has((Prop)((int)Prop::PaddingTop + side))) pad[side] = page::px(app, css->padding[side]);
    }
    const D2D1_COLOR_F codeText = page::textColor(css, app.theme.code);
    y += page::before(app, 0);

    // A terminal newline ends the last row; it does not start another.
    // Count earlier blank rows normally and keep the empty-block minimum.
    int lineCount = (code.empty() || code.back() != '\n') ? 1 : 0;
    for (char c : code) if (c == '\n') lineCount++;

    app.docText += L"\n";

    float blockHeight = lineCount * lineHeight + pad[0] + pad[2];
    size_t bgRectIndex = app.layoutRects.size();
    // Peeking a plain-text file: the panel already frames the content,
    // so the whole-file wrapper block skips its own card
    bool frameless = app.peekFrameless;
    if (!frameless) {
        App::LayoutRect card;
        card.rect = D2D1::RectF(indent, y, indent + maxWidth, y + blockHeight);
        card.color = css.has(Prop::BackgroundColor) ? page::color(css->backgroundColor) : app.theme.codeBackground;
        if (css.has(Prop::BorderTopLeftRadius)) card.radius = page::px(app, css->radius[0]);
        if (css.has(Prop::BorderTopWidth) || css.has(Prop::BorderTopStyle)) {
            const css::Border& edge = css->border[0];
            card.strokeWidth = edge.style == css::LineStyle::None ? 0.0f : page::px(app, edge.width);
            card.stroke = css.has(Prop::BorderTopColor) ? page::color(edge.color) : app.theme.blockquoteBorder;
        }
        app.layoutRects.push_back(card);
    }

    std::wstring wcode = toWide(code);

    // Track code block for copy button
    app.codeBlocks.push_back({
        D2D1::RectF(indent, y, indent + maxWidth, y + blockHeight),
        wcode
    });
    float textY = y + pad[0];
    SyntaxState syntaxState;
    size_t codeDocStart = app.docText.size();
    size_t lineStart = 0;
    float maxLineWidth = 0.0f;

    while (lineStart < wcode.length()) {
        size_t lineEnd = wcode.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos) lineEnd = wcode.length();

        std::wstring wline = wcode.substr(lineStart, lineEnd - lineStart);
        if (!wline.empty() && wline.back() == L'\r') wline.pop_back();

        size_t lineDocStart = codeDocStart + lineStart;
        float lineWidth = 0.0f;
        size_t lineRun = (size_t)-1;

        if (language > 0) {
            std::vector<SyntaxToken> tokens = tokenizeLine(wline, language, syntaxState);
            float tokenX = indent + pad[3];

            // Merge consecutive same-color tokens into one layout — a line
            // typically collapses to a handful of color runs instead of one
            // IDWriteTextLayout per token.
            size_t ti = 0;
            while (ti < tokens.size()) {
                if (tokens[ti].text.empty()) { ti++; continue; }

                D2D1_COLOR_F runColor = getTokenColor(app.theme, tokens[ti].tokenType);
                std::wstring_view runText = tokens[ti].text;
                size_t tj = ti + 1;
                while (tj < tokens.size()) {
                    const auto& next = tokens[tj];
                    if (next.text.empty()) { tj++; continue; }
                    D2D1_COLOR_F c = getTokenColor(app.theme, next.tokenType);
                    bool sameColor = c.r == runColor.r && c.g == runColor.g &&
                                     c.b == runColor.b && c.a == runColor.a;
                    // Tokens are views into wline; only merge physically
                    // adjacent ones so the combined view stays valid
                    bool adjacent = next.text.data() == runText.data() + runText.size();
                    if (!sameColor || !adjacent) break;
                    runText = std::wstring_view(runText.data(), runText.size() + next.text.size());
                    tj++;
                }

                LayoutInfo info = createLayout(app, runText, app.codeFormat, lineHeight, app.codeTypography);
                float runWidth = info.width;

                D2D1_POINT_2F pos = D2D1::Point2F(tokenX, textY);
                D2D1_RECT_F bounds = D2D1::RectF(tokenX, textY, tokenX + runWidth, textY + lineHeight);
                addTextRun(app, std::move(info), pos, bounds, runColor,
                           lineDocStart, 0, false);

                tokenX += runWidth;
                lineWidth += runWidth;
                ti = tj;
            }
        } else {
            LayoutInfo info = createLayout(app, wline, app.codeFormat, lineHeight, app.codeTypography);
            lineWidth = info.width;
            D2D1_POINT_2F pos = D2D1::Point2F(indent + pad[3], textY);
            D2D1_RECT_F bounds = D2D1::RectF(indent + pad[3], textY,
                                             indent + pad[3] + lineWidth, textY + lineHeight);
            size_t runsBefore = app.layoutTextRuns.size();
            addTextRun(app, std::move(info), pos, bounds, codeText,
                       lineDocStart, wline.length(), false);
            if (app.layoutTextRuns.size() > runsBefore) {
                lineRun = app.layoutTextRuns.size() - 1;
            }
        }

        if (!wline.empty()) {
            D2D1_RECT_F lineBounds = D2D1::RectF(indent + pad[3], textY,
                indent + pad[3] + lineWidth, textY + lineHeight);
            // Syntax-highlighted lines have no single covering layout;
            // monospace interpolation stays accurate there
            addTextRect(app, lineBounds, lineDocStart, wline.length(), lineRun);
        }

        maxLineWidth = std::max(maxLineWidth, lineWidth);
        textY += lineHeight;
        if (lineEnd == wcode.length()) break;
        lineStart = lineEnd + 1;
    }

    // Long code lines used to be clipped at the block edge with no way to
    // reach them — extend the block background and the document width so
    // wide code participates in horizontal scrolling like diagrams do
    float widestExtent = maxLineWidth + pad[1] + pad[3];
    if (widestExtent > maxWidth) {
        if (!frameless) {
            app.layoutRects[bgRectIndex].rect.right = indent + widestExtent;
        }
        app.contentWidth = std::max(app.contentWidth, indent + widestExtent);
    }

    app.docText += wcode;
    app.docText += L"\n\n";
    y += blockHeight;
    y += page::after(app, 14);
}

static bool isInlineElement(ElementType type) {
    switch (type) {
        case ElementType::Text:
        case ElementType::Code:
        case ElementType::Emphasis:
        case ElementType::Strong:
        case ElementType::Link:
        case ElementType::Image:
        case ElementType::SoftBreak:
        case ElementType::HardBreak:
        case ElementType::Ruby:
        case ElementType::RubyText:
        case ElementType::Anchor:
        case ElementType::FootnoteReference:
        case ElementType::FootnoteBacklink:
        case ElementType::Highlight:
        case ElementType::Superscript:
        case ElementType::Subscript:
        case ElementType::Strikethrough:
        case ElementType::Underline:
        case ElementType::MathInline:
        case ElementType::Citation:
        case ElementType::CrossReference:
            return true;
        case ElementType::MathDisplay:
            // Tight lists expose $$...$$ directly; route it through inline layout.
            return true;
        default:
            return false;
    }
}

static void layoutBlockquote(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    using css::Prop;
    float scale = app.contentScale * app.zoomFactor;
    float quoteIndent = 20.0f * scale;
    // A CSS theme's quote (page_style.h): its left border is the bar, the
    // border and padding its indent; a background or vertical padding
    // makes it a box the bar runs the height of
    const page::Style& css = page::current(app);
    y += page::before(app, 0);
    float barWidth = 4.0f;
    const bool themedBar = css.has(Prop::BorderLeftWidth) || css.has(Prop::BorderLeftStyle);
    if (themedBar) barWidth = css->border[3].style == css::LineStyle::None ? 0.0f : page::px(app, css->border[3].width);
    if (css.has(Prop::PaddingLeft)) quoteIndent = barWidth + page::px(app, css->padding[3]);
    else if (themedBar) quoteIndent = std::max(quoteIndent, barWidth + 16.0f * scale);
    const float padRight = css.has(Prop::PaddingRight) ? page::px(app, css->padding[1]) : 0.0f;
    const bool filled = css.has(Prop::BackgroundColor) && css->backgroundColor.a > 0.0f;
    const bool boxed = filled || css.has(Prop::PaddingTop) || css.has(Prop::PaddingBottom);
    size_t fill = SIZE_MAX;
    if (filled) {
        fill = app.layoutRects.size();
        App::LayoutRect r;
        r.rect = D2D1::RectF(indent, y, indent + maxWidth, y);
        r.color = page::color(css->backgroundColor);
        if (css.has(Prop::BorderTopLeftRadius)) r.radius = page::px(app, css->radius[0]);
        app.layoutRects.push_back(r);
    }
    float startY = y;
    if (css.has(Prop::PaddingTop)) y += page::px(app, css->padding[0]);
    if (boxed) page::boundary(app);
    maxWidth -= padRight;
    const LayoutSnapshot contentStart = takeSnapshot(app);

    // GitHub alert callouts: accent-colored bar plus a bold title line.
    // Colors are github.com's light/dark alert accents, picked by theme.
    static const struct {
        const wchar_t* title;
        uint32_t light;
        uint32_t dark;
    } ALERT_STYLES[] = {
        {L"\u24D8  Note",            0x0969DA, 0x4493F8},  // circled info
        {L"\U0001F4A1\uFE0E  Tip",   0x1A7F37, 0x3FB950},  // bulb, text presentation
        {L"\u2757\uFE0E  Important", 0x8250DF, 0xAB7DF8},  // exclamation
        {L"\u26A0\uFE0E  Warning",   0x9A6700, 0xD29922},  // warning triangle
        {L"\u26D4\uFE0E  Caution",   0xCF222E, 0xF85149},  // no-entry
    };

    D2D1_COLOR_F barColor = app.theme.blockquoteBorder;
    if (elem->alertKind >= 1 && elem->alertKind <= 5) {
        const auto& style = ALERT_STYLES[elem->alertKind - 1];
        barColor = hexColor(app.theme.isDark ? style.dark : style.light);
        if (css.has(Prop::BorderLeftColor)) barColor = page::color(css->border[3].color);
        css::Element titleElement;
        titleElement.tag = "p";
        titleElement.classes = {"alert-title"};
        titleElement.index = titleElement.typeIndex = 1;
        titleElement.count = titleElement.typeCount = 1;
        page::Block titleBlock(app, titleElement);
        const D2D1_COLOR_F titleColor = page::textColor(titleBlock.style(), barColor);

        std::wstring title = style.title;
        LayoutInfo info = createLayout(app, title, app.textFormat, 24.0f, app.bodyTypography);
        if (info.layout) {
            DWRITE_TEXT_RANGE range = {0, (UINT32)title.length()};
            info.layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
        }
        D2D1_POINT_2F pos = D2D1::Point2F(indent + quoteIndent, y);
        D2D1_RECT_F bounds = D2D1::RectF(indent + quoteIndent, y,
                                         indent + maxWidth, y + 24 * scale);
        addTextRun(app, std::move(info), pos, bounds, titleColor, 0, 0, false);
        y += 30 * scale;
    } else if (css.has(Prop::BorderLeftColor)) {
        barColor = page::color(css->border[3].color);
    }

    // A raw HTML quote can hold its text bare, with no <p> around it
    // (<blockquote>Text <b>bold</b></blockquote>), and layoutElement drew
    // nothing for it. Each run of inline children is a paragraph, in
    // source order with the blocks around it.
    std::vector<ElementPtr> inlineRun;
    for (const auto& child : elem->children) {
        if (isInlineElement(child->type)) {
            inlineRun.push_back(child);
            continue;
        }
        layoutInlineRun(app, inlineRun, y, indent + quoteIndent, maxWidth - quoteIndent);
        layoutElement(app, child, y, indent + quoteIndent, maxWidth - quoteIndent);
    }
    layoutInlineRun(app, inlineRun, y, indent + quoteIndent, maxWidth - quoteIndent);

    // Child layout advances y past its trailing margin. Keep that spacing
    // outside the bar, without moving the next block. Use retained content
    // bounds so nested quotes, code padding, tables and math stay covered.
    float contentBottom = startY;
    for (size_t i = contentStart.textRuns; i < app.layoutTextRuns.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutTextRuns[i].bounds.bottom);
    for (size_t i = contentStart.textRects; i < app.textRects.size(); ++i)
        contentBottom = std::max(contentBottom, app.textRects[i].rect.bottom);
    for (size_t i = contentStart.rects; i < app.layoutRects.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutRects[i].rect.bottom);
    for (size_t i = contentStart.lines; i < app.layoutLines.size(); ++i) {
        const auto& line = app.layoutLines[i];
        contentBottom = std::max(contentBottom, std::max(line.p1.y, line.p2.y) + line.stroke / 2);
    }
    for (size_t i = contentStart.shapes; i < app.layoutShapes.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutShapes[i].rect.bottom);
    for (size_t i = contentStart.connectors; i < app.layoutConnectors.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutConnectors[i].bounds.bottom);
    for (size_t i = contentStart.bitmaps; i < app.layoutBitmaps.size(); ++i)
        contentBottom = std::max(contentBottom, app.layoutBitmaps[i].destRect.bottom);

    if (boxed) {
        // A box holds its children's margins, as CSS's padding does
        if (css.has(Prop::PaddingBottom)) y += page::px(app, css->padding[2]);
        if (fill < app.layoutRects.size()) app.layoutRects[fill].rect.bottom = y;
        if (barWidth > 0.0f) app.layoutRects.push_back({D2D1::RectF(indent, startY, indent + barWidth, y), barColor});
        page::boundary(app);
        y += page::afterContainer(app, 14);
        return;
    }
    if (barWidth > 0.0f) {
        app.layoutRects.push_back({D2D1::RectF(indent, startY, indent + barWidth, std::min(y, contentBottom)), barColor});
    }
    y += page::afterContainer(app, 0);
}

// HTML wrappers and anchor-only content do not determine a list marker's baseline.
static const Element* firstListContent(const std::vector<ElementPtr>& children) {
    for (size_t i = 0; i < children.size();) {
        const auto& child = children[i];
        if (isInlineElement(child->type)) {
            std::vector<ElementPtr> inlineElements;
            do {
                inlineElements.push_back(children[i++]);
            } while (i < children.size() && isInlineElement(children[i]->type));
            if (!isAnchorOnlyContent(inlineElements)) return inlineElements.front().get();
        } else {
            ++i;
            if (child->type == ElementType::Paragraph && isAnchorOnlyContent(child->children)) continue;
            if (child->type != ElementType::HtmlBlock) return child.get();
            if (const auto* first = firstListContent(child->children)) return first;
        }
    }
    return nullptr;
}

static void layoutList(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    using css::Prop;
    float scale = app.contentScale * app.zoomFactor;
    // A CSS theme's list (page_style.h): padding-left is the indent its
    // markers hang in, list-style-type and ::marker the markers
    const page::Style& listCss = page::current(app);
    float listIndent = listCss.has(Prop::PaddingLeft) ? page::px(app, listCss->padding[3]) : 24.0f * scale;
    y += page::before(app, 0);
    int itemNum = elem->start;
    // How deep this list sits in other lists, for the copy's nesting
    static int listDepth = 0;
    struct DepthScope {
        DepthScope() { listDepth++; }
        ~DepthScope() { listDepth--; }
    } depthScope;

    for (const auto& child : elem->children) {
        if (child->type != ElementType::ListItem) continue;
        page::Block item(app, child.get());
        const page::Style& css = item.style();
        y += page::before(app, 0);
        IDWriteTextFormat* itemFormat = page::format(app, css, app.textFormat);
        const D2D1_COLOR_F itemColor = page::textColor(css, app.theme.text);
        // The theme's marker: ::marker's content, or the list-style-type
        // (a bullet's shape, a counter, a string or none)
        const css::ComputedStyle* markerCss = css ? page::pseudo(app, css, css::Pseudo::Marker) : nullptr;
        D2D1_COLOR_F markerColor = itemColor;
        if (markerCss && markerCss->has(Prop::Color)) markerColor = page::color(markerCss->color);
        int themedBullet = -1;
        bool themedText = false, themedNone = false;
        std::wstring themedMarker;
        if (markerCss && !markerCss->content.empty()) {
            themedText = true;
            for (const auto& part : markerCss->content) {
                if (part.kind == css::ContentPart::Text) themedMarker += toWide(part.text);
                else if (part.kind == css::ContentPart::Counter || part.kind == css::ContentPart::Counters)
                    themedMarker += counterText(itemNum, part.style);
            }
        } else if (css.has(Prop::ListStyleType)) {
            switch (css->listStyle) {
                case css::ListStyle::Disc: themedBullet = 0; break;
                case css::ListStyle::Circle: themedBullet = 1; break;
                case css::ListStyle::Square: themedBullet = 2; break;
                case css::ListStyle::None: themedNone = true; break;
                case css::ListStyle::String:
                    themedText = true;
                    themedMarker = toWide(css->listString);
                    break;
                default:
                    themedText = true;
                    themedMarker = counterText(itemNum, css->listStyle) + L".";
                    break;
            }
        }

        const LayoutSnapshot markerStart = takeSnapshot(app);
        size_t markerIndex = SIZE_MAX, bulletIndex = SIZE_MAX;
        float bulletBaseline = 0;
        size_t listItem = app.docListItems.size();
        app.docListItems.push_back({app.docText.size(), app.docText.size(), listDepth - 1,
                                    elem->ordered, itemNum,
                                    child->isTask ? (child->taskChecked ? 2 : 1) : 0,
                                    elem.get()});

        if (child->isTask) {
            // Task item: a clickable checkbox replaces the bullet
            float box = 15.0f * scale;
            float boxX = indent + 1.0f * scale;
            float boxY = y + 4.0f * scale;
            D2D1_RECT_F boxRect = D2D1::RectF(boxX, boxY, boxX + box, boxY + box);
            if (child->taskChecked) {
                app.layoutRects.push_back({boxRect, app.theme.accent});
                D2D1_COLOR_F mark = app.theme.isDark
                    ? D2D1::ColorF(0.08f, 0.08f, 0.10f) : D2D1::ColorF(1, 1, 1);
                app.layoutLines.push_back({
                    D2D1::Point2F(boxX + box * 0.24f, boxY + box * 0.52f),
                    D2D1::Point2F(boxX + box * 0.44f, boxY + box * 0.74f),
                    mark, 2.0f * scale});
                app.layoutLines.push_back({
                    D2D1::Point2F(boxX + box * 0.44f, boxY + box * 0.74f),
                    D2D1::Point2F(boxX + box * 0.78f, boxY + box * 0.28f),
                    mark, 2.0f * scale});
            } else {
                D2D1_COLOR_F edge = app.theme.blockquoteBorder;
                float w = 1.2f * scale;
                app.layoutLines.push_back({{boxX, boxY}, {boxX + box, boxY}, edge, w});
                app.layoutLines.push_back({{boxX, boxY + box}, {boxX + box, boxY + box}, edge, w});
                app.layoutLines.push_back({{boxX, boxY}, {boxX, boxY + box}, edge, w});
                app.layoutLines.push_back({{boxX + box, boxY}, {boxX + box, boxY + box}, edge, w});
            }
            if (child->taskMarkOffset != SIZE_MAX) {
                app.taskRects.push_back({
                    D2D1::RectF(boxX - 4 * scale, boxY - 4 * scale,
                                boxX + box + 4 * scale, boxY + box + 4 * scale),
                    child->taskMarkOffset, child->taskChecked});
            }
        } else if (themedNone) {
            itemNum++;
        } else if (themedText) {
            itemNum++;
            LayoutInfo info = createLayout(app, themedMarker, itemFormat, 24.0f, app.bodyTypography);
            D2D1_POINT_2F pos = D2D1::Point2F(indent, y);
            // A marker wider than the indent ends where the text begins
            if (info.width > listIndent) pos.x = indent + listIndent - info.width - 4.0f * scale;
            D2D1_RECT_F bounds = D2D1::RectF(pos.x, y, indent + listIndent, y + 24);
            if (info.layout) markerIndex = app.layoutTextRuns.size();
            addTextRun(app, std::move(info), pos, bounds, markerColor, 0, 0, false);
        } else if (themedBullet >= 0 && itemFormat) {
            bulletBaseline = y + formatBaseline(app, itemFormat);
            bulletIndex = app.layoutShapes.size();
            app.layoutShapes.push_back(listBullet(indent + listIndent, bulletBaseline,
                itemFormat->GetFontSize(), themedBullet, markerColor));
        } else if (elem->ordered) {
            std::wstring marker = std::to_wstring(itemNum++) + L".";

            LayoutInfo info = createLayout(app, marker, app.textFormat, 24.0f, app.bodyTypography);
            D2D1_POINT_2F pos = D2D1::Point2F(indent, y);
            D2D1_RECT_F bounds = D2D1::RectF(indent, y, indent + listIndent, y + 24);
            if (info.layout) markerIndex = app.layoutTextRuns.size();
            addTextRun(app, std::move(info), pos, bounds, markerColor, 0, 0, false);
        } else if (app.textFormat) {
            bulletBaseline = y + formatBaseline(app, itemFormat);
            bulletIndex = app.layoutShapes.size();
            app.layoutShapes.push_back(listBullet(indent + listIndent, bulletBaseline,
                itemFormat->GetFontSize(), listDepth - 1, markerColor));
        }

        const LayoutSnapshot contentStart = takeSnapshot(app);
        const size_t headingStart = app.headings.size();
        float itemStartY = y;

        // MD4C omits paragraph wrappers in tight lists.
        // Hence list items can mix direct inline children with block children.
        std::vector<ElementPtr> inlineElements;
        for (const auto& itemChild : child->children) {
            if (isInlineElement(itemChild->type)) {
                inlineElements.push_back(itemChild);
            } else {
                if (!inlineElements.empty()) {
                    if (!pinAnchorsOnly(app, inlineElements, y)) {
                        layoutInlineContent(app, inlineElements, indent + listIndent, y,
                            maxWidth - listIndent, itemFormat, itemColor, {},
                            page::lineHeight(app, css, itemFormat, 0.0f));
                        // The item's text ends its line before a nested list, as
                        // on screen (a copy read "Water it.morning")
                        app.docText += L"\n";
                    }
                    inlineElements.clear();
                }
                layoutElement(app, itemChild, y, indent + listIndent, maxWidth - listIndent);
            }
        }
        if (!inlineElements.empty() && !pinAnchorsOnly(app, inlineElements, y)) {
            const LayoutSnapshot textStart = takeSnapshot(app);
            layoutInlineContent(app, inlineElements, indent + listIndent, y,
                maxWidth - listIndent, itemFormat, itemColor, {},
                page::lineHeight(app, css, itemFormat, 0.0f));
            alignLines(app, textStart, indent + listIndent, maxWidth - listIndent, css);
        }

        const auto* first = firstListContent(child->children);
        if (headingStart < app.headings.size() && first && first->type == ElementType::Heading) {
            // Move only the marker: keep heading margins and align to the main
            // text baseline, even when the heading starts with a raised script.
            auto format = app.headingFormats[first->level - 1];
            float headingBaseline = app.headings[headingStart].y + formatBaseline(app, format ? format : app.textFormat);
            if (child->isTask) {
                float shift = headingBaseline - itemStartY - formatBaseline(app, app.textFormat);
                // Move only this checkbox, including its hit region; nested content stays put.
                for (size_t i = markerStart.rects; i < contentStart.rects; ++i) {
                    app.layoutRects[i].rect.top += shift;
                    app.layoutRects[i].rect.bottom += shift;
                }
                for (size_t i = markerStart.lines; i < contentStart.lines; ++i) {
                    app.layoutLines[i].p1.y += shift;
                    app.layoutLines[i].p2.y += shift;
                }
                for (size_t i = markerStart.tasks; i < contentStart.tasks; ++i) {
                    app.taskRects[i].rect.top += shift;
                    app.taskRects[i].rect.bottom += shift;
                }
            } else if (markerIndex < app.layoutTextRuns.size()) {
                auto& marker = app.layoutTextRuns[markerIndex];
                float shift = headingBaseline - marker.pos.y - layoutBaseline(marker.layout);
                marker.pos.y += shift;
                marker.bounds.top += shift;
                marker.bounds.bottom += shift;
            } else if (bulletIndex < app.layoutShapes.size()) {
                auto& bullet = app.layoutShapes[bulletIndex].rect;
                float shift = headingBaseline - bulletBaseline;
                bullet.top += shift;
                bullet.bottom += shift;
            }
        }

        if (listItem < app.docListItems.size()) app.docListItems[listItem].end = app.docText.size();
        app.docText += L"\n\n";

        if (y < itemStartY + 28 * scale) {
            y = itemStartY + 28 * scale;
        }
        y += page::afterContainer(app, 0);
    }
    y += page::afterContainer(app, 8);
}

// Result of a background image download+decode, handed to the UI thread
// via WM_APP_IMAGE_READY. Pixels are premultiplied BGRA.
struct AsyncImageResult {
    std::string src;
    DecodedImage image;
    bool ok = false;
};

static bool createDecodedBitmap(App& app, const DecodedImage& image,
                                App::ImageEntry& entry) {
    if (!app.renderTarget || image.pixels.empty()) return false;
    const auto props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        image.dpiX, image.dpiY);
    if (FAILED(app.renderTarget->CreateBitmap(D2D1::SizeU(image.width, image.height),
            image.pixels.data(), image.width * 4, props, entry.bitmap.GetAddressOf())))
        return false;
    const auto size = entry.bitmap->GetSize();
    entry.width = std::max(1, static_cast<int>(size.width));
    entry.height = std::max(1, static_cast<int>(size.height));
    entry.failed = false;
    return true;
}

// Only pixel bytes cross the thread boundary. The UI allocates the result
// before launching so even download/decode allocation failures can complete
// the pending cache entry without an exception terminating the process.
static void asyncImageWorker(HWND hwnd, UINT maxDimension,
                             std::unique_ptr<AsyncImageResult> result) {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        if (SUCCEEDED(apartment)) {
            wchar_t tempPath[MAX_PATH] = {};
            const std::wstring wideSrc = toWide(result->src);
            if (SUCCEEDED(URLDownloadToCacheFileW(nullptr, wideSrc.c_str(), tempPath,
                                                  MAX_PATH, 0, nullptr))) {
                Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
                if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())))) {
                    result->ok = SUCCEEDED(decodeImageFile(wic.Get(), tempPath,
                                                          maxDimension, result->image));
                }
            }
        }
    } catch (const std::exception&) {
        result->ok = false;
    }
    if (SUCCEEDED(apartment)) CoUninitialize();
    // Transfer ownership before posting: the UI can consume immediately.
    auto* delivered = result.release();
    if (!PostMessageW(hwnd, WM_APP_IMAGE_READY, 0, reinterpret_cast<LPARAM>(delivered)))
        delete delivered;
}

// SVG images rasterize at 2x through the Direct2D SVG renderer
// (ID2D1DeviceContext5, Windows 10 1703+). Shapes, paths, and gradients
// render; <text> elements are not supported by D2D's SVG subset.
static bool loadSvgBitmap(App& app, const std::wstring& path,
                          App::ImageEntry& entry) {
    if (!app.d2dFactory || !app.wicFactory || !app.renderTarget) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::string bytes((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;

    auto makeDc5 = [&](UINT w, UINT h, IWICBitmap** wicOut,
                       ID2D1DeviceContext5** dcOut) -> bool {
        *wicOut = nullptr;
        *dcOut = nullptr;
        if (FAILED(app.wicFactory->CreateBitmap(
                w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand,
                wicOut))) {
            return false;
        }
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        ID2D1RenderTarget* rt = nullptr;
        if (FAILED(app.d2dFactory->CreateWicBitmapRenderTarget(*wicOut, props,
                                                               &rt))) {
            (*wicOut)->Release();
            *wicOut = nullptr;
            return false;
        }
        HRESULT hr = rt->QueryInterface(IID_PPV_ARGS(dcOut));
        rt->Release();
        if (FAILED(hr) || !*dcOut) {
            // Pre-1703 Windows: no Direct2D SVG; the alt text shows instead
            (*wicOut)->Release();
            *wicOut = nullptr;
            return false;
        }
        return true;
    };
    auto makeStream = [&]() -> IStream* {
        return SHCreateMemStream((const BYTE*)bytes.data(),
                                 (UINT)bytes.size());
    };

    // Pass 1: parse only, to learn the intrinsic size
    float svgW = 0.0f, svgH = 0.0f;
    {
        IWICBitmap* tmp = nullptr;
        ID2D1DeviceContext5* dc = nullptr;
        if (!makeDc5(4, 4, &tmp, &dc)) return false;
        IStream* stream = makeStream();
        ID2D1SvgDocument* doc = nullptr;
        if (stream &&
            SUCCEEDED(dc->CreateSvgDocument(stream, D2D1::SizeF(512, 512),
                                            &doc)) &&
            doc) {
            ID2D1SvgElement* root = nullptr;
            doc->GetRoot(&root);
            if (root) {
                FLOAT w = 0, h = 0;
                if (SUCCEEDED(root->GetAttributeValue(L"width", &w)) &&
                    SUCCEEDED(root->GetAttributeValue(L"height", &h)) &&
                    w > 0 && h > 0) {
                    svgW = w;
                    svgH = h;
                } else {
                    D2D1_SVG_VIEWBOX vb{};
                    if (SUCCEEDED(root->GetAttributeValue(
                            L"viewBox", D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX,
                            &vb, sizeof(vb))) &&
                        vb.width > 0 && vb.height > 0) {
                        svgW = vb.width;
                        svgH = vb.height;
                    }
                }
                root->Release();
            }
            doc->Release();
        }
        if (stream) stream->Release();
        dc->Release();
        tmp->Release();
    }
    if (svgW <= 0.0f || svgH <= 0.0f) {
        svgW = 300.0f;  // the SVG spec's replaced-element default
        svgH = 150.0f;
    }
    svgW = std::min(svgW, 4096.0f);
    svgH = std::min(svgH, 4096.0f);

    // Pass 2: rasterize at 2x for crisp zooming
    UINT pxW = (UINT)std::ceil(svgW * 2.0f);
    UINT pxH = (UINT)std::ceil(svgH * 2.0f);
    IWICBitmap* wicBitmap = nullptr;
    ID2D1DeviceContext5* dc = nullptr;
    if (!makeDc5(pxW, pxH, &wicBitmap, &dc)) return false;
    bool drawn = false;
    {
        IStream* stream = makeStream();
        ID2D1SvgDocument* doc = nullptr;
        if (stream &&
            SUCCEEDED(dc->CreateSvgDocument(stream, D2D1::SizeF(svgW, svgH),
                                            &doc)) &&
            doc) {
            dc->BeginDraw();
            dc->Clear(D2D1::ColorF(0, 0, 0, 0));
            dc->SetTransform(D2D1::Matrix3x2F::Scale(2.0f, 2.0f));
            dc->DrawSvgDocument(doc);
            drawn = SUCCEEDED(dc->EndDraw());
            doc->Release();
        }
        if (stream) stream->Release();
    }
    dc->Release();

    if (drawn) {
        D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            192.0f, 192.0f);
        ID2D1Bitmap* bitmap = nullptr;
        if (SUCCEEDED(app.renderTarget->CreateBitmapFromWicBitmap(
                wicBitmap, &bp, &bitmap)) &&
            bitmap) {
            entry.bitmap.Attach(bitmap);
            entry.width = (int)svgW;
            entry.height = (int)svgH;
            entry.failed = false;
        }
    }
    wicBitmap->Release();
    return !entry.failed;
}

static App::ImageEntry& getOrLoadImage(App& app, const std::string& src, bool fromDocument) {
    // Remote images load on a worker thread — a synchronous download here
    // stalled layout for seconds per unreachable URL (#44)
    bool isUrl = (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0);

    std::wstring widePath;
    std::wstring folder;
    if (!isUrl) {
        // Resolve relative to the current file's directory. Both strings are
        // UTF-8 and must be widened explicitly: a narrow filesystem::path
        // decodes through the ANSI codepage and mangles non-ASCII paths (#87)
        // Decode URL escapes once before converting UTF-8 to a native path
        // (#293). Remote URLs keep their original encoding.
        std::wstring wsrc = toWide(qmd::decodeLinkComponent(src));
        if (!app.currentFile.empty()) {
            folder = std::filesystem::path(toWide(app.currentFile)).parent_path().wstring();
        } else {
            // An untitled note has no folder of its own: relative images
            // resolve against the launch folder, as they did while the
            // process still worked there (#253)
            folder = launchDirectory();
        }
        widePath = (std::filesystem::path(folder) / wsrc).wstring();
    }

    // A document opens images on this computer and on the server it came
    // from, never another computer's share: Windows would sign in to that
    // server with the user's credentials. Decided before the cache, so a
    // file another document was allowed to open does not show here either.
    if (fromDocument && !isUrl && !qmd::mayOpenFromDocument(widePath, folder)) {
        static App::ImageEntry blocked = [] {
            App::ImageEntry entry;
            entry.failed = true;
            entry.blocked = true;
            return entry;
        }();
        return blocked;
    }

    // A local image is cached as the file it resolves to: documents in two
    // folders that both show "shot.png" each get their own
    const std::string key = isUrl ? src : wideToUtf8(widePath);
    auto it = app.imageCache.find(key);
    if (it != app.imageCache.end()) {
        app.touchImageCacheEntry(it->second);
        return it->second;
    }

    App::ImageEntry entry;
    entry.failed = true;  // assume failure

    if (!app.wicFactory || !app.renderTarget) {
        app.storeImageCacheEntry(key, std::move(entry));
        return app.imageCache[key];
    }

    if (isUrl) {
        entry.failed = false;
        entry.pending = true;
        app.storeImageCacheEntry(src, std::move(entry));
        try {
            auto result = std::make_unique<AsyncImageResult>();
            result->src = src;
            std::thread(asyncImageWorker, app.hwnd,
                        app.renderTarget->GetMaximumBitmapSize(), std::move(result)).detach();
        } catch (const std::exception&) {
            auto& failed = app.imageCache.at(src);
            failed.pending = false;
            failed.failed = true;
        }
        return app.imageCache[src];
    }

    // SVG renders through Direct2D's SVG document support instead of WIC
    {
        std::wstring lower = widePath;
        for (auto& c : lower) c = (wchar_t)towlower(c);
        if (lower.size() > 4 &&
            lower.compare(lower.size() - 4, 4, L".svg") == 0) {
            loadSvgBitmap(app, widePath, entry);
            app.storeImageCacheEntry(key, std::move(entry));
            return app.imageCache[key];
        }
    }

    DecodedImage image;
    if (SUCCEEDED(decodeImageFile(app.wicFactory, widePath.c_str(),
                                 app.renderTarget->GetMaximumBitmapSize(), image)))
        createDecodedBitmap(app, image, entry);

    app.storeImageCacheEntry(key, std::move(entry));
    return app.imageCache[key];
}

static void layoutImage(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    auto& entry = getOrLoadImage(app, elem->url);

    if (entry.failed || !entry.bitmap) {
        // Render alt text as placeholder, in the interface's language
        std::wstring altText = std::wstring(L"[") + tr(app, "image.placeholder");
        std::wstring alt;
        std::function<void(const ElementPtr&)> extract = [&](const ElementPtr& e) {
            if (!e) return;
            if (e->type == ElementType::Text) alt += toWide(e->text);
            else for (const auto& c : e->children) extract(c);
        };
        for (const auto& c : elem->children) extract(c);
        if (!alt.empty()) {
            altText += L": " + alt;
        }
        // An image left on another computer's share says why it is missing
        if (entry.blocked) {
            altText += L" \u2014 ";
            altText += tr(app, "image.network_blocked");
        }
        altText += L"]";

        float lineHeight = app.italicFormat->GetFontSize() * themeLineHeight(app.theme);
        size_t docStart = app.docText.size();
        LayoutInfo info = createLayout(app, altText, app.italicFormat, lineHeight, app.bodyTypography);

        D2D1_COLOR_F color = app.theme.text;
        color.a = 0.6f;
        D2D1_POINT_2F pos = D2D1::Point2F(indent, y);
        D2D1_RECT_F bounds = D2D1::RectF(indent, y, indent + info.width, y + lineHeight);
        addTextRun(app, std::move(info), pos, bounds, color, docStart, altText.length(), false);
        app.docText += altText;
        y += lineHeight;
        return;
    }

    float scale = app.contentScale * app.zoomFactor;
    float imgW = (float)entry.width;
    float imgH = (float)entry.height;

    // Scale to fit within maxWidth, never upscale
    float displayScale = std::min(1.0f, maxWidth / imgW);
    float displayW = imgW * displayScale;
    float displayH = imgH * displayScale;

    // Cap max height
    float maxH = 600.0f * scale;
    if (displayH > maxH) {
        displayH = maxH;
        displayW = displayH * (imgW / imgH);
    }

    app.layoutBitmaps.push_back({entry.bitmap,
        D2D1::RectF(indent, y, indent + displayW, y + displayH)});

    y += displayH + 12 * scale;
}

static void layoutTable(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    using css::Prop;
    float scale = app.contentScale * app.zoomFactor;
    float cellPadding = 8.0f * scale;
    float fontSize = app.textFormat->GetFontSize();
    float lineHeight = fontSize * themeLineHeight(app.theme);
    float minColWidth = 40.0f * scale;

    // Collect rows
    std::vector<Element*> rows;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::TableRow) {
            rows.push_back(child.get());
        }
    }
    if (rows.empty()) return;

    // Determine column count
    int colCount = elem->col_count;
    if (colCount <= 0 && !rows.empty()) {
        colCount = (int)rows[0]->children.size();
    }
    if (colCount <= 0) return;

    // A CSS theme's table (page_style.h), named as the export writes it:
    // thead > tr > th for the header, tbody > tr > td for the rest
    const page::Style& tableCss = page::current(app);
    std::vector<page::Style> rowCss(rows.size());
    std::vector<std::vector<page::Style>> cellCss(rows.size(), std::vector<page::Style>((size_t)colCount));
    if (tableCss) {
        auto named = [](const char* tag, int index, int count) {
            css::Element e;
            e.tag = tag;
            e.index = e.typeIndex = index;
            e.count = e.typeCount = count;
            return e;
        };
        const int parts = rows.size() > 1 ? 2 : 1;
        const css::Element head = named("thead", 1, parts), body = named("tbody", 2, parts);
        const page::Style headCss = page::child(app, tableCss, head);
        const page::Style bodyCss = page::child(app, tableCss, body, &head, 1);
        std::vector<css::Element> trs;
        for (size_t r = 1; r < rows.size(); r++) trs.push_back(named("tr", (int)r, (int)rows.size() - 1));
        const css::Element headRow = named("tr", 1, 1);
        std::vector<css::Element> heads, cells;
        for (int c = 0; c < colCount; c++) {
            heads.push_back(named("th", c + 1, colCount));
            cells.push_back(named("td", c + 1, colCount));
        }
        for (size_t r = 0; r < rows.size(); r++) {
            rowCss[r] = r == 0 ? page::child(app, headCss, headRow)
                               : page::child(app, bodyCss, trs[r - 1], trs.data(), r - 1);
            const std::vector<css::Element>& kind = r == 0 ? heads : cells;
            for (int c = 0; c < colCount; c++)
                cellCss[r][(size_t)c] = page::child(app, rowCss[r], kind[(size_t)c], kind.data(), (size_t)c);
        }
    }
    // One padding for the grid, the body cells' (or the header's)
    float padX = cellPadding, padY = cellPadding;
    {
        const page::Style& cell = cellCss[rows.size() > 1 ? 1 : 0][0];
        if (cell.has(Prop::PaddingLeft) || cell.has(Prop::PaddingRight))
            padX = page::px(app, std::max(cell->padding[3], cell->padding[1]));
        if (cell.has(Prop::PaddingTop) || cell.has(Prop::PaddingBottom))
            padY = page::px(app, std::max(cell->padding[0], cell->padding[2]));
    }
    auto cellFormat = [&](size_t r, size_t c) {
        return page::format(app, cellCss[r][c], r == 0 ? app.boldFormat : app.textFormat);
    };
    auto cellColor = [&](size_t r, size_t c) {
        return page::textColor(cellCss[r][c], r == 0 ? app.theme.heading : app.theme.text);
    };
    y += page::before(app, 0);

    // Pass 1: Measure natural widths using plain text extraction (cheap, approximate)
    std::vector<float> colWidths(colCount, minColWidth);
    std::vector<float> rowHeights(rows.size(), lineHeight + padY * 2);
    std::vector<std::vector<int>> cellAligns(rows.size(), std::vector<int>(colCount, 0));
    // Per-cell natural width + plain-text flag: a simple cell that fits its
    // final column is exactly one line tall, so the height-measuring trial
    // layout in pass 1b can be skipped for it entirely
    std::vector<std::vector<float>> cellNatural(rows.size(), std::vector<float>(colCount, 0.0f));
    std::vector<std::vector<uint8_t>> cellSimple(rows.size(), std::vector<uint8_t>(colCount, 0));

    std::wstring tsv;  // per-cell plain text doubles as the copy payload
    for (size_t r = 0; r < rows.size(); r++) {
        const auto& row = rows[r];
        if (r > 0) tsv += L"\n";
        for (size_t c = 0; c < row->children.size() && c < (size_t)colCount; c++) {
            const auto& cell = row->children[c];
            cellAligns[r][c] = cell->align;
            // A column's own alignment stands; the theme's fills the rest
            if (!cell->align && cellCss[r][c].has(Prop::TextAlign)) {
                const css::TextAlign a = cellCss[r][c]->textAlign;
                cellAligns[r][c] = a == css::TextAlign::Center ? 2 : a == css::TextAlign::Right ? 3 : 0;
            }

            // Extract plain text for width estimation
            std::wstring text;
            std::function<void(const ElementPtr&)> extract = [&](const ElementPtr& e) {
                if (!e) return;
                if (e->type == ElementType::Text) text += toWide(e->text);
                else for (const auto& ch : e->children) extract(ch);
            };
            for (const auto& ch : cell->children) extract(ch);

            if (c > 0) tsv += L"\t";
            std::wstring tsvCell = text;
            for (wchar_t& ch : tsvCell) {
                if (ch == L'\t' || ch == L'\n' || ch == L'\r') ch = L' ';
            }
            tsv += tsvCell;

            // Measure natural width THE WAY THE RENDERER SHAPES IT — same
            // typography and the language-aware font fallback. A raw
            // layout here picked a different CJK font than the renderer,
            // whose slightly wider advances then wrapped a "fitting" cell
            // one line past its measured row height (#148)
            IDWriteTextFormat* fmt = cellFormat(r, c);
            bool plainCell = cell->children.size() == 1 &&
                             cell->children[0]->type == ElementType::Text;
            float textWidth = 0;
            if (plainCell && !text.empty() && fmt) {
                LayoutInfo mi = createLayout(app, text, fmt, lineHeight,
                                             app.bodyTypography);
                textWidth = mi.width;
                if (mi.layout) mi.layout->Release();
            } else if (!cell->children.empty() && fmt) {
                // Styled cell: plain-text extraction lies about width —
                // inline code renders in the mono face plus a padded chip,
                // so an under-measured column
                // pushed chips past the table border. Lay the cell out at
                // unlimited width with the real renderer and take its
                // right edge (chip rects included), then roll back.
                LayoutSnapshot snap = takeSnapshot(app);
                float cy = 0.0f;
                layoutInlineContent(app, cell->children, 0.0f, cy,
                                    kHugeWidth, fmt, app.theme.text, {},
                                    lineHeight, -1.0f, &cellCss[r][c]);
                float maxRight = 0.0f;
                for (size_t i = snap.textRuns; i < app.layoutTextRuns.size();
                     i++) {
                    maxRight = std::max(maxRight,
                                        app.layoutTextRuns[i].bounds.right);
                }
                for (size_t i = snap.rects; i < app.layoutRects.size(); i++) {
                    maxRight = std::max(maxRight,
                                        app.layoutRects[i].rect.right);
                }
                for (size_t i = snap.bitmaps; i < app.layoutBitmaps.size(); i++) {
                    maxRight = std::max(maxRight,
                                        app.layoutBitmaps[i].destRect.right);
                }
                rollbackTo(app, snap);
                textWidth = maxRight;
            }
            float needed = textWidth + padX * 2 + 6.0f * scale;
            if (needed > colWidths[c]) colWidths[c] = needed;
            cellNatural[r][c] = needed;
            cellSimple[r][c] = cell->children.size() == 1 &&
                               cell->children[0]->type == ElementType::Text;
        }
    }

    // Distribute widths like a browser's auto table layout: columns whose
    // natural width is under their fair share keep it untouched; only the
    // wide columns shrink, splitting the remaining space proportionally.
    // Pure proportional scaling starved narrow columns (a 3-character CJK
    // header ended up one character wide) while a single huge cell hogged
    // the row (#24).
    float totalWidth = 0;
    for (int c = 0; c < colCount; c++) totalWidth += colWidths[c];

    unsigned fitKey = 0x40000000u | app.layoutTableSeq++;
    bool fitCandidate = false;
    bool fitActive = fitBlockEnabled(app, fitKey);

    if (totalWidth > maxWidth && fitActive) {
        // Reserve room for padding and at least one full character before
        // distributing the remaining width. A dense table may still need
        // scrolling: negative/near-zero cell widths cannot wrap safely.
        const float floorTotal = minColWidth * colCount;
        const float available = std::max(0.0f, maxWidth - floorTotal);
        const float naturalExtra = totalWidth - floorTotal;
        for (int c = 0; c < colCount; c++) {
            colWidths[c] = minColWidth + (naturalExtra > 0.0f
                ? available * ((colWidths[c] - minColWidth) / naturalExtra) : 0.0f);
        }
        totalWidth = std::max(maxWidth, floorTotal);
        app.contentWidth = std::max(app.contentWidth, indent + totalWidth);
        fitCandidate = true;
    } else if (totalWidth > maxWidth) {
        // Floor: at least ~2.5 CJK glyphs per line so no column degenerates
        // into a vertical strip
        float minCol = std::max(minColWidth, fontSize * 2.5f + padX * 2);
        std::vector<bool> fixed(colCount, false);
        float available = maxWidth;
        int unfixedCount = colCount;
        bool changed = true;
        while (changed && unfixedCount > 0) {
            changed = false;
            float fair = available / unfixedCount;
            for (int c = 0; c < colCount; c++) {
                if (!fixed[c] && colWidths[c] <= fair) {
                    fixed[c] = true;
                    available -= colWidths[c];
                    unfixedCount--;
                    changed = true;
                }
            }
        }
        if (unfixedCount > 0) {
            float naturalSum = 0;
            for (int c = 0; c < colCount; c++) {
                if (!fixed[c]) naturalSum += colWidths[c];
            }
            for (int c = 0; c < colCount; c++) {
                if (!fixed[c]) {
                    float w = (naturalSum > 0.0f)
                        ? available * (colWidths[c] / naturalSum)
                        : available / unfixedCount;
                    colWidths[c] = std::max(minCol, w);
                }
            }
        }
        totalWidth = 0;
        for (int c = 0; c < colCount; c++) totalWidth += colWidths[c];
        // Minimum widths can push past maxWidth; the table then joins
        // horizontal scrolling instead of squeezing columns unreadably
        // (the fit toggle uses a smaller floor)
        if (totalWidth > maxWidth + 1.0f) fitCandidate = true;
        app.contentWidth = std::max(app.contentWidth, indent + totalWidth);
    }

    // Pass 1b: Measure row heights via layoutInlineContent with snapshot-rollback
    for (size_t r = 0; r < rows.size(); r++) {
        float maxRowH = lineHeight + padY * 2;
        const auto& row = rows[r];

        for (size_t c = 0; c < row->children.size() && c < (size_t)colCount; c++) {
            const auto& cell = row->children[c];
            if (cell->children.empty()) continue;

            // A plain-text cell whose natural width fits the column is one
            // line tall — no trial layout needed (the common case)
            if (cellSimple[r][c] && cellNatural[r][c] <= colWidths[c]) continue;

            float cellW = colWidths[c] - padX * 2;
            LayoutSnapshot snap = takeSnapshot(app);
            float cellY = 0.0f;
            layoutInlineContent(app, cell->children, 0.0f, cellY, cellW,
                                cellFormat(r, c), cellColor(r, c), {}, lineHeight, -1.0f, &cellCss[r][c]);
            rollbackTo(app, snap);

            float h = cellY + padY * 2;
            if (h > maxRowH) maxRowH = h;
        }
        rowHeights[r] = maxRowH;
    }

    // The Table element itself never receives a sourceOffset (the parser
    // stamps the deepest element at text time), so the table's source
    // anchor is its first text-bearing descendant - the same convention
    // the in-place cell editor's source parser assumes (#148)
    size_t tableSrc = SIZE_MAX;
    {
        std::function<void(const ElementPtr&)> firstSrc =
            [&](const ElementPtr& e) {
                if (!e || tableSrc != SIZE_MAX) return;
                if (e->sourceOffset != SIZE_MAX) {
                    tableSrc = e->sourceOffset;
                    return;
                }
                for (const auto& child : e->children) firstSrc(child);
            };
        for (const auto& child : elem->children) {
            firstSrc(child);
            if (tableSrc != SIZE_MAX) break;
        }
    }

    // Pass 2: Render cells via layoutInlineContent (produces real text runs, links, etc.)
    float tableStartY = y;
    D2D1_COLOR_F borderColor = app.theme.blockquoteBorder;
    float borderStroke = 1.0f * scale;
    // The theme's grid: the cells' borders across (top, bottom) and down
    // (left, right); its rows' backgrounds in place of Tinta's stripes
    D2D1_COLOR_F acrossColor = borderColor, downColor = borderColor;
    float across = borderStroke, down = borderStroke, headerRule = borderStroke * 2;
    bool themedRows = false;
    {
        const page::Style& cell = cellCss[rows.size() > 1 ? 1 : 0][0];
        auto side = [&](int s, float& width, D2D1_COLOR_F& color) {
            const Prop w = (Prop)((int)Prop::BorderTopWidth + s * 3);
            if (!cell.has(w) && !cell.has((Prop)((int)w + 1))) return false;
            const css::Border& b = cell->border[s];
            width = b.style == css::LineStyle::None ? 0.0f : page::px(app, b.width);
            if (cell.has((Prop)((int)w + 2))) color = page::color(b.color);
            return true;
        };
        float a = across, d = down;
        const bool themedAcross = side(0, a, acrossColor) | side(2, a, acrossColor);
        const bool themedDown = side(3, d, downColor) | side(1, d, downColor);
        if (themedAcross) across = headerRule = a;
        if (themedDown) down = d;
        const page::Style& head = cellCss[0][0];
        if (head.has(Prop::BorderBottomWidth) || head.has(Prop::BorderBottomStyle)) {
            headerRule = head->border[2].style == css::LineStyle::None ? 0.0f : page::px(app, head->border[2].width);
        }
        for (size_t r = 1; r < rows.size(); r++) {
            themedRows = themedRows || rowCss[r].has(Prop::BackgroundColor) || cellCss[r][0].has(Prop::BackgroundColor);
        }
    }

    for (size_t r = 0; r < rows.size(); r++) {
        float cellX = indent;
        bool isHeader = (r == 0);
        const auto& row = rows[r];

        // Header row background
        const page::Style& rowBack = cellCss[r][0].has(Prop::BackgroundColor) ? cellCss[r][0] : rowCss[r];
        if (rowBack.has(Prop::BackgroundColor)) {
            const D2D1_COLOR_F back = page::color(rowBack->backgroundColor);
            if (back.a > 0.0f) {
                app.layoutRects.push_back({D2D1::RectF(indent, y, indent + totalWidth, y + rowHeights[r]), back});
            }
        } else if (isHeader) {
            D2D1_COLOR_F headerBg = app.theme.codeBackground;
            headerBg.a = 0.5f;
            app.layoutRects.push_back({D2D1::RectF(indent, y, indent + totalWidth, y + rowHeights[r]), headerBg});
        } else if (r % 2 == 0 && !themedRows) {
            // Subtle alternating row background
            D2D1_COLOR_F altBg = app.theme.codeBackground;
            altBg.a = 0.15f;
            app.layoutRects.push_back({D2D1::RectF(indent, y, indent + totalWidth, y + rowHeights[r]), altBg});
        }

        for (size_t c = 0; c < row->children.size() && c < (size_t)colCount; c++) {
            const auto& cell = row->children[c];
            IDWriteTextFormat* fmt = cellFormat(r, c);
            D2D1_COLOR_F textColor = cellColor(r, c);

            // In-place editing (#148): the preview records every cell's
            // rect keyed by the table's source anchor plus row/col
            if (app.editMode && tableSrc != SIZE_MAX) {
                app.tableCellRects.push_back(
                    {tableSrc, (int)r, (int)c,
                     D2D1::RectF(cellX, y, cellX + colWidths[c],
                                 y + rowHeights[r])});
            }

            if (!cell->children.empty()) {
                float cellW = colWidths[c] - padX * 2;
                float textX = cellX + padX;
                float textY = y + padY;

                int align = cellAligns[r][c];
                LayoutSnapshot cellSnap = takeSnapshot(app);

                layoutInlineContent(app, cell->children, textX, textY, cellW,
                                    fmt, textColor, {}, lineHeight, -1.0f, &cellCss[r][c]);

                // Apply center/right alignment by shifting all new items
                if (align == 2 || align == 3) {
                    float maxRight = 0.0f;
                    for (size_t i = cellSnap.textRuns; i < app.layoutTextRuns.size(); i++) {
                        maxRight = std::max(maxRight, app.layoutTextRuns[i].bounds.right);
                    }
                    for (size_t i = cellSnap.bitmaps; i < app.layoutBitmaps.size(); i++) {
                        maxRight = std::max(maxRight, app.layoutBitmaps[i].destRect.right);
                    }
                    float contentW = maxRight - textX;
                    float dx = 0.0f;
                    if (align == 2) { // center
                        dx = (cellW - contentW) / 2.0f;
                    } else { // right
                        dx = cellW - contentW;
                    }
                    if (dx > 0.0f) {
                        shiftLayoutItems(app, cellSnap, dx);
                    }
                }
            }

            // A tab between cells keeps them apart in the selectable text:
            // double-click takes one cell's word instead of gluing on its
            // neighbours ("DeltaEchoFoxtrot"), and a copied row pastes into
            // a spreadsheet as columns (#246, spotted by @Lex987)
            if (c + 1 < row->children.size() && c + 1 < (size_t)colCount) {
                app.docText += L"\t";
            }
            cellX += colWidths[c];
        }
        app.docText += L"\n";
        y += rowHeights[r];
    }

    // Grid lines: horizontal
    for (size_t r = 0; r <= rows.size(); r++) {
        float lineY = tableStartY;
        for (size_t i = 0; i < r; i++) lineY += rowHeights[i];
        float stroke = (r == 1) ? headerRule : across; // thicker after header
        if (stroke <= 0.0f) continue;
        app.layoutLines.push_back({D2D1::Point2F(indent, lineY),
                                   D2D1::Point2F(indent + totalWidth, lineY),
                                   acrossColor, stroke});
    }

    // Grid lines: vertical
    {
        float tableEndY = tableStartY;
        for (size_t r = 0; r < rows.size(); r++) tableEndY += rowHeights[r];
        float vx = indent;
        for (int c = 0; c <= colCount; c++) {
            if (down > 0.0f) {
                app.layoutLines.push_back({D2D1::Point2F(vx, tableStartY),
                                           D2D1::Point2F(vx, tableEndY),
                                           downColor, down});
            }
            if (c < colCount) vx += colWidths[c];
        }

        // Copy-as-TSV hover button hit region
        App::TableInfo info;
        info.bounds =
            D2D1::RectF(indent, tableStartY, indent + totalWidth, tableEndY);
        info.tsv = std::move(tsv);
        info.fitKey = fitKey;
        info.fitCandidate = fitCandidate || fitActive;
        info.fitActive = fitActive;
        info.sourceOffset = tableSrc;
        app.tableRects.push_back(std::move(info));
    }

    app.docText += L"\n";
    y += page::after(app, 14);
}

static void layoutHorizontalRule(App& app, float& y, float indent, float maxWidth) {
    using css::Prop;
    float scale = app.contentScale * app.zoomFactor;
    // A CSS theme's rule (page_style.h): its top border, or a bar of its
    // background when it has no border (GitHub's)
    const page::Style& css = page::current(app);
    D2D1_COLOR_F color = themeRuleColor(app.theme);
    float width = scale;
    if (css.has(Prop::BorderTopWidth) || css.has(Prop::BorderTopStyle)) {
        const css::Border& b = css->border[0];
        width = b.style == css::LineStyle::None ? 0.0f : page::px(app, b.width);
        if (css.has(Prop::BorderTopColor)) color = page::color(b.color);
    }
    if (width <= 0.0f && css.has(Prop::BackgroundColor) && css->backgroundColor.a > 0.0f) {
        width = 4.0f * scale;
        color = page::color(css->backgroundColor);
    }
    y += page::before(app, 16);
    if (width > 0.0f) {
        app.layoutLines.push_back({D2D1::Point2F(indent, y),
                                   D2D1::Point2F(indent + maxWidth, y),
                                   color, width});
    }
    y += page::after(app, 16);
}

static void layoutProperties(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    y = layoutFrontmatterStrip(app, elem->properties, indent, y, maxWidth,
                              app.contentScale * app.zoomFactor);
}

static void layoutElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    if (!elem) return;

    // The element's style for the layout below (page_style.h), and its
    // side margins and max-width when a CSS theme sets them
    page::Block block(app, elem.get());
    if (const page::Style& css = block.style()) {
        using css::Prop;
        if (css.has(Prop::MarginLeft)) {
            const float m = page::px(app, css->margin[3]);
            indent += m;
            maxWidth -= m;
        }
        if (css.has(Prop::MarginRight)) maxWidth -= page::px(app, css->margin[1]);
        if (css.has(Prop::MaxWidth) && (css->maxWidth.kind == css::Length::Px || css->maxWidth.kind == css::Length::Percent)) {
            const float cap = css->maxWidth.kind == css::Length::Px ? page::px(app, css->maxWidth.value)
                                                                    : css->maxWidth.resolve(maxWidth);
            maxWidth = std::min(maxWidth, cap);
        }
        maxWidth = std::max(maxWidth, 1.0f);
    }

    switch (elem->type) {
        case ElementType::Properties:
            // A paper prints its title block in the strip's place (paper.h)
            if (!layoutTitleBlock(app, elem, y, indent, maxWidth)) layoutProperties(app, elem, y, indent, maxWidth);
            break;
        case ElementType::Paragraph:
            layoutParagraph(app, elem, y, indent, maxWidth);
            break;
        case ElementType::Footnotes: {
            {
                // section.footnotes > hr, as the export writes it
                css::Element rule;
                rule.tag = "hr";
                rule.empty = true;
                rule.index = rule.count = rule.typeIndex = rule.typeCount = 1;
                page::Block ruleBlock(app, rule);
                layoutHorizontalRule(app, y, indent, maxWidth);
            }
            for (const auto& child : elem->children) layoutElement(app, child, y, indent, maxWidth);
            break;
        }
        case ElementType::FootnoteDefinition: {
            app.footnoteAnchors[elem->url] = y;
            auto label = std::make_shared<Element>(ElementType::Text);
            label->text = std::to_string(elem->level) + ".";
            float labelY = y;
            float inset = 32 * app.contentScale * app.zoomFactor;
            const page::Style& css = page::current(app);
            layoutInlineContent(app, {label}, indent, labelY, inset, page::format(app, css, app.textFormat),
                                page::textColor(css, app.theme.text));
            for (const auto& child : elem->children) layoutElement(app, child, y, indent + inset, maxWidth - inset);
            break;
        }
        case ElementType::Heading:
            layoutHeading(app, elem, y, indent, maxWidth);
            break;
        case ElementType::Figure:
            // A numbered figure or captioned table (Scholar): its id is a
            // place a reference scrolls to, and its caption what the
            // reference's card shows
            if (!elem->title.empty()) {
                app.footnoteAnchors[elem->title] = y;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Caption) app.referenceCards[elem->title] = toWide(plainText(child));
                }
            }
            for (const auto& child : elem->children) {
                // The picture itself, with its caption close under it
                // rather than past a paragraph's empty last line
                if (child->type == ElementType::Paragraph && child->children.size() == 1 &&
                    child->children[0]->type == ElementType::Image) {
                    layoutImage(app, child->children[0], y, indent, maxWidth);
                    continue;
                }
                layoutElement(app, child, y, indent, maxWidth);
            }
            break;
        case ElementType::Caption: {
            D2D1_COLOR_F muted = app.theme.text;
            muted.a *= 0.8f;
            const page::Style& css = page::current(app);
            muted = page::textColor(css, muted);
            IDWriteTextFormat* format = page::format(app, css, app.textFormat);
            y += page::before(app, 0);
            BlockBox box(app, css, y, indent, maxWidth);
            const LayoutSnapshot start = takeSnapshot(app);
            layoutInlineContent(app, elem->children, indent, y, maxWidth, format, muted, {},
                                page::lineHeight(app, css, format, 0.0f));
            alignLines(app, start, indent, maxWidth, css);
            box.close(y, muted);
            app.docText += L"\n\n";
            y += page::after(app, 14);
            break;
        }
        case ElementType::Bibliography: {
            // Scholar's references: a number column (IEEE, Nature, GB/T) or
            // a hanging indent (APA, Chicago); each entry is the place its
            // citations go to
            const float scale = app.contentScale * app.zoomFactor;
            float column = 0;
            if (elem->language == "field") {
                for (const auto& entry : elem->children)
                    column = std::max(column, measureText(app, toWide(entry->text), app.textFormat));
                column += 10 * scale;
            }
            const float hang = elem->language == "hanging" ? 32 * scale : 0;
            for (const auto& entry : elem->children) {
                if (entry->type != ElementType::BibEntry) continue;
                page::Block entryBlock(app, entry.get());
                const page::Style& css = entryBlock.style();
                IDWriteTextFormat* entryFormat = page::format(app, css, app.textFormat);
                const D2D1_COLOR_F entryColor = page::textColor(css, app.theme.text);
                app.footnoteAnchors[entry->title] = y;
                app.referenceCards[entry->title] =
                    toWide((entry->text.empty() ? std::string() : entry->text + " ") + plainText(entry));
                if (column > 0) {
                    auto label = std::make_shared<Element>(ElementType::Text);
                    label->text = entry->text;
                    float labelY = y;
                    layoutInlineContent(app, {label}, indent, labelY, column, entryFormat, entryColor);
                    float entryY = y;
                    layoutInlineContent(app, entry->children, indent + column, entryY, maxWidth - column,
                                        entryFormat, entryColor);
                    y = std::max(labelY, entryY);
                } else {
                    layoutInlineContent(app, entry->children, indent + hang, y, maxWidth - hang, entryFormat,
                                        entryColor, {}, 0.0f, indent);
                }
                app.docText += L"\n\n";
                y += 8 * scale;
            }
            y += 6 * scale;
            break;
        }
        case ElementType::CodeBlock:
            layoutCodeBlock(app, elem, y, indent, maxWidth);
            break;
        case ElementType::MermaidDiagram:
            if (!layoutMermaidDiagram(
                    app, elem->text, elem->sourceOffset, y, indent, maxWidth)) {
                app.focusMermaidOnNextLayout = false;
                auto fallback = std::make_shared<Element>(ElementType::CodeBlock);
                auto text = std::make_shared<Element>(ElementType::Text);
                text->text = elem->text;
                text->parent = fallback.get();
                fallback->children.push_back(std::move(text));
                fallback->sourceOffset = elem->sourceOffset;
                layoutCodeBlock(app, fallback, y, indent, maxWidth);
            }
            break;
        case ElementType::BlockQuote:
            layoutBlockquote(app, elem, y, indent, maxWidth);
            break;
        case ElementType::List:
            layoutList(app, elem, y, indent, maxWidth);
            break;
        case ElementType::HorizontalRule:
            layoutHorizontalRule(app, y, indent, maxWidth);
            break;
        case ElementType::Table:
            layoutTable(app, elem, y, indent, maxWidth);
            break;
        case ElementType::HtmlBlock: {
            // HtmlBlock can contain both block elements (Paragraph, List, etc.)
            // and inline elements (Text, Ruby, Link, etc.). Collect consecutive
            // inline children and render them through layoutInlineContent.
            std::vector<ElementPtr> inlineBuffer;
            for (const auto& child : elem->children) {
                bool isBlock = (child->type == ElementType::Paragraph ||
                                child->type == ElementType::Heading ||
                                child->type == ElementType::CodeBlock ||
                                child->type == ElementType::BlockQuote ||
                                child->type == ElementType::List ||
                                child->type == ElementType::HorizontalRule ||
                                child->type == ElementType::HtmlBlock ||
                                child->type == ElementType::Table);
                if (isBlock) {
                    layoutInlineRun(app, inlineBuffer, y, indent, maxWidth);
                    layoutElement(app, child, y, indent, maxWidth);
                } else {
                    inlineBuffer.push_back(child);
                }
            }
            layoutInlineRun(app, inlineBuffer, y, indent, maxWidth);
            break;
        }
        default:
            for (const auto& child : elem->children) {
                layoutElement(app, child, y, indent, maxWidth);
            }
            break;
    }
}

// Find first valid sourceOffset in an element subtree
static size_t findFirstSourceOffset(const ElementPtr& elem) {
    if (!elem) return SIZE_MAX;
    if (elem->sourceOffset != SIZE_MAX) return elem->sourceOffset;
    for (const auto& child : elem->children) {
        size_t off = findFirstSourceOffset(child);
        if (off != SIZE_MAX) return off;
    }
    return SIZE_MAX;
}

// Count total elements in AST for vector pre-allocation
static size_t countElements(const ElementPtr& elem) {
    if (!elem) return 0;
    size_t count = 1;
    for (const auto& child : elem->children) {
        count += countElements(child);
    }
    return count;
}

} // namespace

namespace {

void collectMathLabels(const ElementPtr& elem, std::map<std::wstring, std::wstring>& labels) {
    if (!elem) return;
    if ((elem->type == ElementType::MathInline || elem->type == ElementType::MathDisplay) &&
        elem->text.find("\\label") != std::string::npos)
        mathCollectLabels(toWide(elem->text), labels);
    for (const auto& child : elem->children) collectMathLabels(child, labels);
}

// Reset layout state and prepare for laying out blocks. Returns false when
// there is nothing to lay out (no document).
bool layoutBegin(App& app) {
    app.clearLayoutCache();
    app.layoutTimeUs = 0;

    if (!app.root) {
        app.contentHeight = 0;
        app.contentWidth = static_cast<float>(app.width);
        app.layoutComplete = true;
        return false;
    }

    // Pre-allocate vectors based on estimated element count
    size_t elemCount = countElements(app.root);
    app.layoutTextRuns.reserve(elemCount * 2);
    app.layoutRects.reserve(elemCount);
    app.layoutLines.reserve(elemCount);
    app.layoutShapes.reserve(elemCount);
    app.layoutConnectors.reserve(elemCount);
    app.linkRects.reserve(elemCount / 4);
    app.textRects.reserve(elemCount * 2);
    app.lineBuckets.reserve(elemCount);
    app.docText.reserve(elemCount * 20);  // ~20 chars per element average

    float scale = app.contentScale * app.zoomFactor;

    float layoutWidth = documentViewportWidth(app);

    // On paper the page margins frame the text: no screen padding (#257)
    app.layoutIndent = app.printLayout ? 0.0f : 40.0f * scale;
    app.layoutMaxWidth = layoutWidth - app.layoutIndent * 2;
    // A CSS theme's sheet, asked for each element as layout goes
    // (page_style.h); its (max-width) queries see the page's width
    page::begin(app, app.layoutMaxWidth / std::max(scale, 0.01f));
    // Reading column (#82): a centered percentage of the window, with a
    // separate preference for fullscreen (zen). Edit-mode panes are exempt,
    // except the full-width reading view, which reads like the reader (#242).
    // Pages print across their margins whatever the screen's column (#257).
    // A CSS theme's max-width on body narrows it further
    {
        int pct = app.zenMode ? app.zenWidthPct : app.readingWidthPct;
        const bool column = !app.printLayout && (!app.editMode || app.editorReadingPreview || editorLiveOn(app));
        if (pct < 100 && column) {
            float width = app.layoutMaxWidth * (float)pct / 100.0f;
            app.layoutIndent += (app.layoutMaxWidth - width) / 2.0f;
            app.layoutMaxWidth = width;
        }
        const page::Style& body = page::body(app);
        if (column && body.has(css::Prop::MaxWidth) &&
            (body->maxWidth.kind == css::Length::Px || body->maxWidth.kind == css::Length::Percent)) {
            const float cap = body->maxWidth.kind == css::Length::Px ? body->maxWidth.value * scale
                                                                     : body->maxWidth.resolve(app.layoutMaxWidth);
            if (cap > 0.0f && cap < app.layoutMaxWidth) {
                app.layoutIndent += (app.layoutMaxWidth - cap) / 2.0f;
                app.layoutMaxWidth = cap;
            }
        }
    }
    // Content starts below the title-bar tab strip; the strip paints over
    // anything that scrolls up under it. The docked edit-mode preview
    // starts there too (#245). Paper has no strip: the first page
    // starts at its top margin (#257).
    app.layoutCursorY = app.printLayout ? 0.0f : documentContentTop(app);
    app.layoutNextBlock = 0;
    app.layoutComplete = false;
    app.contentWidth = layoutWidth;
    app.scrollAnchors.clear();
    app.layoutTableSeq = 0;
    app.layoutDiagramSeq = 0;
    // The equations' numbers by label, for an \eqref anywhere to read (#314)
    app.mathLabels.clear();
    collectMathLabels(app.root, app.mathLabels);
    return true;
}

// Lay out top-level blocks until targetY is passed (targetY < 0: no limit) or
// budgetUs is exhausted (budgetUs <= 0: no limit). Returns true when all
// blocks are done.
bool layoutStep(App& app, float targetY, int64_t budgetUs) {
    auto t0 = Clock::now();
    const auto& children = app.root->children;
    float y = app.layoutCursorY;

    while (app.layoutNextBlock < children.size()) {
        if (targetY >= 0.0f && y > targetY) break;
        if (budgetUs > 0 && usElapsed(t0) > budgetUs) break;

        const auto& child = children[app.layoutNextBlock];
        // Record scroll anchor from source offset
        size_t offset = findFirstSourceOffset(child);
        // Endnotes can move definitions ahead of their source order. Keep the
        // editor/preview mapping ordered; note jumps use their explicit anchors.
        if (offset != SIZE_MAX && child->type != ElementType::Footnotes &&
            (app.scrollAnchors.empty() || app.scrollAnchors.back().sourceOffset < offset)) {
            app.scrollAnchors.push_back({offset, y});
        }
        float top = y;
        layoutElement(app, child, y, app.layoutIndent, app.layoutMaxWidth);
        app.blockSpans.push_back({offset, top, y});
        app.layoutNextBlock++;
    }

    app.layoutCursorY = y;
    // Partial content height grows as layout fills in (keeps scrollbar sane)
    float scale = app.contentScale * app.zoomFactor;
    float tail = 40.0f * scale;
    // The reading view's Edit pill does not zoom with the page: zoomed
    // out, the last line must still scroll clear of it (#250)
    if (app.editMode && app.editorReadingPreview) tail = std::max(tail, readPillClearance(app));
    // Live types at the page's end: room to bring the last line up
    if (editorLiveOn(app)) tail = std::max(tail, (float)app.height * 0.35f);
    app.contentHeight = y + tail;
    return app.layoutNextBlock >= children.size();
}

void layoutFinish(App& app) {
    // Defer toLower to when search actually needs it (lazy rebuild)
    app.docTextLower.clear();
    mapSearchMatchesToLayout(app);
    app.layoutComplete = true;
}

} // namespace

void layoutDocument(App& app) {
    auto t0 = Clock::now();
    if (layoutBegin(app)) {
        layoutStep(app, -1.0f, -1);
        layoutFinish(app);
    }
    app.layoutDirty = false;
    app.layoutTimeUs += (size_t)usElapsed(t0);
}

void layoutDocumentViewportFirst(App& app) {
    auto t0 = Clock::now();
    if (layoutBegin(app)) {
        // Lay out through two viewports past the current scroll so the first
        // frame presents immediately; the rest continues in chunks.
        float targetY = app.scrollY + (float)app.height * 2.0f;
        if (layoutStep(app, targetY, -1)) {
            layoutFinish(app);
        }
    }
    app.layoutDirty = false;
    app.layoutTimeUs += (size_t)usElapsed(t0);
}

bool layoutDocumentContinue(App& app, int64_t budgetUs) {
    if (app.layoutComplete) return true;
    auto t0 = Clock::now();
    bool done = layoutStep(app, -1.0f, budgetUs);
    if (done) layoutFinish(app);
    app.layoutTimeUs += (size_t)usElapsed(t0);
    return done;
}

void discardAsyncImage(void* asyncResult) {
    delete static_cast<AsyncImageResult*>(asyncResult);
}

void completeAsyncImage(App& app, void* asyncResult) {
    std::unique_ptr<AsyncImageResult> result(static_cast<AsyncImageResult*>(asyncResult));
    if (!result) return;
    // A document/cache reset can make an in-flight download irrelevant.
    const auto pending = app.imageCache.find(result->src);
    if (pending == app.imageCache.end() || !pending->second.pending) return;

    App::ImageEntry entry;
    entry.failed = true;
    if (result->ok) createDecodedBitmap(app, result->image, entry);
    app.storeImageCacheEntry(result->src, std::move(entry));

    // Coalesce several completions into one reflow.
    if (app.hwnd) SetTimer(app.hwnd, TIMER_IMAGE_REFLOW, 60, nullptr);
}

void ensureLayoutComplete(App& app) {
    if (app.layoutDirty) {
        layoutDocument(app);
        return;
    }
    if (!app.layoutComplete) {
        auto t0 = Clock::now();
        layoutStep(app, -1.0f, -1);
        layoutFinish(app);
        app.layoutTimeUs += (size_t)usElapsed(t0);
    }
}


// Public face of the role resolver for the exporters (the implementation
// lives in this file's anonymous namespace beside the palette helpers)
D2D1_COLOR_F resolveDiagramRole(const App& app, const mermaidext::Prim& prim,
                                mermaidext::Role role) {
    return resolveDiagramRoleImpl(app, prim, role);
}

// Scholar's hover cards: above the page's own drawing, in its coordinates
void renderReferenceCard(App& app) {
    if (app.showSettings || app.showContextMenu || app.showThemeChooser || app.showThemeEditor ||
        app.referenceCards.empty()) {
        return;
    }
    const float dx = app.mouseX - documentViewportX(app) + app.scrollX, dy = app.mouseY + app.scrollY;
    for (const auto& link : app.linkRects) {
        const auto& b = link.bounds;
        if (dx < b.left || dx > b.right || dy < b.top || dy > b.bottom) continue;
        std::wstring card;
        if (link.anchor.rfind("cite-", 0) == 0) {
            auto keys = app.citationKeys.find(link.anchor);
            if (keys == app.citationKeys.end()) return;
            size_t at = 0;
            while (at < keys->second.size()) {
                size_t space = keys->second.find(' ', at);
                const std::string key = keys->second.substr(at, space == std::string::npos ? std::string::npos : space - at);
                at = space == std::string::npos ? keys->second.size() : space + 1;
                if (key.empty()) continue;
                auto entry = app.referenceCards.find("ref-" + key);
                if (!card.empty()) card += L"\n\n";
                card += entry != app.referenceCards.end() ? entry->second : L"@" + toWide(key) + L"?";
            }
        } else if (link.anchor.rfind("xref-", 0) == 0 && link.url.size() > 1) {
            auto entry = app.referenceCards.find(link.url.substr(1));
            if (entry != app.referenceCards.end()) card = entry->second;
        }
        if (card.empty() || !app.folderBrowserFormat) return;
        const float s = app.contentScale;
        const float w = std::min(440 * s, documentViewportWidth(app) - 24 * s);
        const float x = std::clamp(b.left - app.scrollX, 12 * s, std::max(12 * s, documentViewportWidth(app) - w - 12 * s));
        const float y = b.bottom - app.scrollY + 6 * s;
        IDWriteTextLayout* layout = nullptr;
        app.dwriteFactory->CreateTextLayout(card.c_str(), (UINT32)card.size(), app.folderBrowserFormat, w - 20 * s,
                                            10000, &layout);
        if (!layout) return;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        app.brush->SetColor(app.theme.codeBackground);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + metrics.height + 16 * s), 6 * s, 6 * s), app.brush);
        app.brush->SetColor(app.theme.text);
        app.renderTarget->DrawTextLayout(D2D1::Point2F(x + 10 * s, y + 8 * s), layout, app.brush);
        layout->Release();
        return;
    }
}
