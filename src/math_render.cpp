#include "math_render.h"
#include "math_parser.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Box model
// ---------------------------------------------------------------------------

namespace {

struct MathRun {
    IDWriteTextLayout* layout = nullptr;  // owned by the MathBox
    float x = 0, y = 0;                   // top-left, box-relative
    // Captured at creation so exporters can reproduce the run without
    // Direct2D (SVG needs the source text and metrics back)
    std::wstring text;
    float size = 0.0f;
    bool italic = false;
    bool bold = false;
    float baseline = 0.0f;                // run-internal baseline offset
};

struct MathRule {   // fraction bars, overlines, radical bars
    float x = 0, y = 0, w = 0, h = 0;
};

struct MathLine {   // arrowheads
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float thickness = 1.2f;
};

} // namespace

struct MathBox {
    std::vector<MathRun> runs;
    std::vector<MathRule> rules;
    std::vector<MathLine> lines;
    float width = 0, height = 0, baseline = 0;
    ~MathBox() {
        for (auto& r : runs) {
            if (r.layout) r.layout->Release();
        }
    }
};

namespace {

// ---------------------------------------------------------------------------
// Parse tree
// ---------------------------------------------------------------------------

using namespace tinta_math;

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

struct Metrics {
    float width = 0, ascent = 0, descent = 0;
    float height() const { return ascent + descent; }
};

struct LayoutCtx {
    App& app;
    MathBox& box;
    // TeX text style vs display style: inline fractions use near-script
    // sizes so they fit the surrounding line
    bool display = false;
};

IDWriteTextLayout* makeRunLayout(App& app, const std::wstring& text,
                                 float size, bool italic, bool bold,
                                 float& outBaseline, Metrics& m) {
    IDWriteTextFormat* fmt = nullptr;
    app.dwriteFactory->CreateTextFormat(
        app.theme.fontFamily, nullptr,
        bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &fmt);
    if (!fmt) return nullptr;

    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                        fmt, 100000.0f, 1000.0f, &layout);
    fmt->Release();
    if (!layout) return nullptr;

    if (app.fontFallback) {
        IDWriteTextLayout2* l2 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout2),
                                             reinterpret_cast<void**>(&l2)))) {
            l2->SetFontFallback(app.fontFallback);
            l2->Release();
        }
    }

    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    DWRITE_LINE_METRICS lm{};
    UINT32 lineCount = 1;
    layout->GetLineMetrics(&lm, 1, &lineCount);
    outBaseline = lm.baseline;
    m.width = tm.widthIncludingTrailingWhitespace;
    m.ascent = lm.baseline;
    m.descent = std::max(0.0f, lm.height - lm.baseline);
    return layout;
}

// Forward declaration
Metrics layoutNode(LayoutCtx& ctx, const MNodePtr& node, float size,
                   float x, float baselineY);

Metrics measureNode(LayoutCtx& ctx, const MNodePtr& node, float size);

// Lay a run out and record it (position given by baseline)
Metrics emitRun(LayoutCtx& ctx, const std::wstring& text, float size,
                bool italic, bool bold, float x, float baselineY,
                bool record) {
    Metrics m;
    float runBaseline = 0;
    IDWriteTextLayout* layout =
        makeRunLayout(ctx.app, text, size, italic, bold, runBaseline, m);
    if (!layout) return m;
    if (record) {
        ctx.box.runs.push_back({layout, x, baselineY - runBaseline, text,
                                size, italic, bold, runBaseline});
    } else {
        layout->Release();
    }
    return m;
}

bool isMathVariableChar(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

// The workhorse: recursively lay out `node` with the baseline at
// `baselineY`, starting at `x`. Returns the node's metrics. When
// record=false (measure pass), nothing is emitted.
Metrics layoutNodeImpl(LayoutCtx& ctx, const MNodePtr& node, float size,
                       float x, float baselineY, bool record) {
    Metrics m;
    if (!node) return m;
    float em = size;

    switch (node->kind) {
        case MNode::Space: {
            m.width = node->space * em;
            m.ascent = 0;
            m.descent = 0;
            return m;
        }
        case MNode::Sym: {
            bool italic = !node->roman && node->text.size() >= 1 &&
                          isMathVariableChar(node->text[0]) &&
                          !(node->text.size() > 1 && node->roman);
            Metrics rm = emitRun(ctx, node->text, size, italic, node->bold,
                                 x, baselineY, record);
            return rm;
        }
        case MNode::Row: {
            float cx = x;
            float prevSpace = 0.0f;
            bool first = true;
            for (const auto& kid : node->kids) {
                if (!kid) continue;
                float kidSpace = 0.0f;
                if (kid->kind == MNode::Sym) {
                    kidSpace = symbolSpacing(kid->text, kid->roman) * em;
                }
                if (!first) cx += std::max(prevSpace, kidSpace);
                Metrics km = layoutNodeImpl(ctx, kid, size, cx, baselineY, record);
                cx += km.width;
                m.ascent = std::max(m.ascent, km.ascent);
                m.descent = std::max(m.descent, km.descent);
                prevSpace = kidSpace;
                first = false;
            }
            m.width = cx - x;
            return m;
        }
        case MNode::Grid: {
            float cellSize = node->compact ? size * 0.7f : size;
            size_t columns = node->alignment.size();
            for (const auto& row : node->cells) columns = std::max(columns, row.size());
            if (!columns || node->cells.empty()) return m;
            std::vector<float> widths(columns, 0), ascents, descents;
            std::vector<std::vector<Metrics>> cells;
            float totalHeight = 0;
            float rowGap = cellSize * 0.35f;
            for (const auto& row : node->cells) {
                float ascent = cellSize * 0.8f, descent = cellSize * 0.2f;
                std::vector<Metrics> measured;
                for (size_t col = 0; col < row.size(); ++col) {
                    Metrics cm = measureNode(ctx, row[col], cellSize);
                    widths[col] = std::max(widths[col], cm.width);
                    ascent = std::max(ascent, cm.ascent);
                    descent = std::max(descent, cm.descent);
                    measured.push_back(cm);
                }
                ascents.push_back(ascent); descents.push_back(descent);
                totalHeight += ascent + descent;
                cells.push_back(std::move(measured));
            }
            totalHeight += rowGap * static_cast<float>(cells.size() - 1);
            m.ascent = totalHeight / 2 + em * 0.26f;
            m.descent = totalHeight - m.ascent;
            float top = baselineY - m.ascent;
            for (size_t row = 0; row < cells.size(); ++row) {
                float cx = x;
                for (size_t col = 0; col < columns; ++col) {
                    wchar_t align = node->aligned ? (col % 2 ? L'l' : L'r') : L'c';
                    if (col < node->alignment.size()) align = node->alignment[col];
                    if (col < cells[row].size() && record) {
                        float extra = widths[col] - cells[row][col].width;
                        float offset = align == L'r' ? extra : align == L'c' ? extra / 2 : 0;
                        layoutNodeImpl(ctx, node->cells[row][col], cellSize,
                                       cx + offset, top + ascents[row], true);
                    }
                    cx += widths[col];
                    if (col + 1 < columns)
                        cx += cellSize * (node->aligned && col % 2 == 0 ? 0.26f : 1.0f);
                }
                m.width = std::max(m.width, cx - x);
                top += ascents[row] + descents[row] + rowGap;
            }
            return m;
        }
        case MNode::Script: {
            const MNodePtr& base = node->kids[0];
            const MNodePtr& sub = node->kids[1];
            const MNodePtr& sup = node->kids[2];
            float scriptSize = std::max(8.0f, size * 0.68f);

            Metrics bm = layoutNodeImpl(ctx, base, size, x, baselineY, record);
            float sx = x + bm.width + em * 0.03f;
            float supShift = std::max(bm.ascent * 0.55f, em * 0.38f);
            float subShift = std::max(bm.descent + em * 0.05f, em * 0.16f);

            Metrics supM, subM;
            if (sup) {
                supM = layoutNodeImpl(ctx, sup, scriptSize, sx,
                                      baselineY - supShift, record);
            }
            if (sub) {
                subM = layoutNodeImpl(ctx, sub, scriptSize, sx,
                                      baselineY + subShift, record);
            }
            m.width = bm.width + em * 0.03f + std::max(supM.width, subM.width);
            m.ascent = std::max(bm.ascent, sup ? supShift + supM.ascent : 0.0f);
            m.descent = std::max(bm.descent, sub ? subShift + subM.descent : 0.0f);
            return m;
        }
        case MNode::Frac: {
            float inner = std::max(8.0f, size * (ctx.display ? 0.92f : 0.72f));
            Metrics num = measureNode(ctx, node->kids[0], inner);
            Metrics den = measureNode(ctx, node->kids[1], inner);
            float pad = em * 0.12f;
            float ruleW = std::max(num.width, den.width) + pad * 2;
            float ruleH = std::max(1.0f, em * 0.055f);
            float axis = em * 0.26f;   // fraction line sits on the math axis
            float gap = em * (ctx.display ? 0.14f : 0.1f);

            float ruleY = baselineY - axis - ruleH / 2;
            if (record) {
                // numerator baseline so its descent clears the rule
                layoutNodeImpl(ctx, node->kids[0], inner,
                               x + pad + (ruleW - 2 * pad - num.width) / 2,
                               ruleY - gap - num.descent, true);
                layoutNodeImpl(ctx, node->kids[1], inner,
                               x + pad + (ruleW - 2 * pad - den.width) / 2,
                               ruleY + ruleH + gap + den.ascent, true);
                ctx.box.rules.push_back({x, ruleY, ruleW, ruleH});
            }
            m.width = ruleW;
            m.ascent = axis + ruleH / 2 + gap + num.height();
            m.descent = -axis + ruleH / 2 + gap + den.height();
            m.descent = std::max(m.descent, 0.0f);
            return m;
        }
        case MNode::Delim: {
            Metrics cm = measureNode(ctx, node->kids[0], size);
            // Tall common delimiters use native strokes at a constant width.
            // Increasing the font size also widens glyphs, and used to cap
            // brackets at 3.5 em, leaving large matrices unenclosed.
            if (cm.height() > em * 1.4f &&
                (!node->open || std::wstring(L"()[]{}|\u2016").find(node->open) != std::wstring::npos) &&
                (!node->close || std::wstring(L"()[]{}|\u2016").find(node->close) != std::wstring::npos)) {
                float top = baselineY - cm.ascent - em * 0.08f;
                float height = cm.height() + em * 0.16f;
                float width = em * 0.38f, pad = em * 0.12f;
                float stroke = std::max(1.0f, em * 0.055f);
                auto line = [&](float x1, float y1, float x2, float y2) {
                    if (record) ctx.box.lines.push_back({x1, y1, x2, y2, stroke});
                };
                auto draw = [&](wchar_t d, float dx) {
                    if (!d) return;
                    float bottom = top + height;
                    bool right = d == L')' || d == L']' || d == L'}';
                    if (d == L'[' || d == L']') {
                        float edge = dx + (right ? width : 0);
                        line(edge, top, edge, bottom);
                        line(dx, top, dx + width, top);
                        line(dx, bottom, dx + width, bottom);
                    } else if (d == L'|' || d == L'\u2016') {
                        line(dx + width * 0.4f, top, dx + width * 0.4f, bottom);
                        if (d == L'\u2016')
                            line(dx + width * 0.8f, top, dx + width * 0.8f, bottom);
                    } else {
                        auto point = [&](float t) {
                            float u;
                            if (d == L'{' || d == L'}') {
                                // Shoulder, waist, shoulder of a curly brace.
                                float v = std::abs(t - 0.5f) * 2;
                                u = 0.5f + 0.5f * (2 * v - 1) * (2 * v - 1) * (2 * v - 1);
                            } else u = 1 - std::sin(t * 3.14159265f);
                            return D2D1::Point2F(dx + width * (right ? 1 - u : u), top + t * height);
                        };
                        auto prev = point(0);
                        for (int step = 1; step <= 32; ++step) {
                            auto next = point(static_cast<float>(step) / 32);
                            line(prev.x, prev.y, next.x, next.y);
                            prev = next;
                        }
                    }
                };
                float leftWidth = node->open ? width + pad : 0;
                float rightWidth = node->close ? width + pad : 0;
                draw(node->open, x);
                layoutNodeImpl(ctx, node->kids[0], size, x + leftWidth, baselineY, record);
                draw(node->close, x + leftWidth + cm.width + pad);
                m.width = leftWidth + cm.width + rightWidth;
                m.ascent = cm.ascent + em * 0.08f + stroke / 2;
                m.descent = cm.descent + em * 0.08f + stroke / 2;
                return m;
            }
            // Delimiters stretch to the content: scale the glyph size so a
            // paren grows with a fraction inside it
            float contentH = std::max(cm.height(), em);
            float glyphSize = size;
            if (contentH > em * 1.15f) {
                glyphSize = contentH;
            }
            float cx = x;
            auto emitDelim = [&](wchar_t d) -> float {
                if (!d) return 0.0f;
                std::wstring dt(1, d);
                Metrics dm;
                float bl = 0;
                IDWriteTextLayout* layout = makeRunLayout(
                    ctx.app, dt, glyphSize, false, false, bl, dm);
                if (!layout) return 0.0f;
                if (record) {
                    // center the glyph on the content's vertical center
                    float centerY = baselineY - (cm.ascent - cm.descent) / 2;
                    float top = centerY - (dm.ascent + dm.descent) / 2;
                    ctx.box.runs.push_back({layout, cx, top, dt, glyphSize,
                                            false, false, bl});
                } else {
                    layout->Release();
                }
                return dm.width;
            };
            float ow = emitDelim(node->open);
            cx += ow;
            Metrics inner = layoutNodeImpl(ctx, node->kids[0], size, cx,
                                           baselineY, record);
            cx += inner.width;
            float cw = emitDelim(node->close);
            cx += cw;
            m.width = cx - x;
            float glyphHalf = (glyphSize * 1.1f) / 2;
            float centerOff = (cm.ascent - cm.descent) / 2;
            m.ascent = std::max(cm.ascent, centerOff + glyphHalf);
            m.descent = std::max(cm.descent, glyphHalf - centerOff);
            return m;
        }
        case MNode::Deco: {
            if (node->decoKind == 3) {  // sqrt
                Metrics cm = measureNode(ctx, node->kids[0], size);
                float radSize = size * std::min(
                    3.0f, std::max(1.0f, cm.height() / em));
                Metrics rm;
                float bl = 0;
                float gap = em * 0.1f;
                float ruleH = std::max(1.0f, em * 0.05f);
                IDWriteTextLayout* rad = makeRunLayout(
                    ctx.app, L"\u221A", radSize, false, false, bl, rm);
                float cx = x;
                if (rad) {
                    if (record) {
                        float radTop = baselineY - cm.ascent - gap - ruleH -
                                       (rm.height() - (cm.height() + gap + ruleH)) * 0.5f;
                        // anchor: radical bottom near content bottom
                        radTop = baselineY + cm.descent - rm.height();
                        ctx.box.runs.push_back({rad, cx, radTop,
                                                std::wstring(L"\u221A"),
                                                radSize, false, false, bl});
                    } else {
                        rad->Release();
                    }
                    cx += rm.width * 0.95f;
                }
                Metrics inner = layoutNodeImpl(ctx, node->kids[0], size, cx,
                                               baselineY, record);
                if (record) {
                    ctx.box.rules.push_back(
                        {cx - em * 0.05f, baselineY - cm.ascent - gap - ruleH,
                         inner.width + em * 0.15f, ruleH});
                }
                m.width = (cx - x) + inner.width + em * 0.15f;
                m.ascent = cm.ascent + gap + ruleH + em * 0.05f;
                m.descent = cm.descent;
                return m;
            }
            // overline / overrightarrow / hat
            Metrics cm = measureNode(ctx, node->kids[0], size);
            float gap = em * 0.12f;
            float ruleH = std::max(1.0f, em * 0.05f);
            Metrics inner = layoutNodeImpl(ctx, node->kids[0], size, x,
                                           baselineY, record);
            float topY = baselineY - cm.ascent - gap - ruleH;
            if (record) {
                if (node->decoKind == 1) {
                    ctx.box.rules.push_back({x, topY, cm.width, ruleH});
                } else if (node->decoKind == 2) {
                    float aw = em * 0.22f;   // arrowhead
                    float ay = topY + ruleH / 2;
                    ctx.box.rules.push_back({x, topY, cm.width, ruleH});
                    ctx.box.lines.push_back(
                        {x + cm.width - aw, ay - aw * 0.6f, x + cm.width, ay});
                    ctx.box.lines.push_back(
                        {x + cm.width - aw, ay + aw * 0.6f, x + cm.width, ay});
                } else if (node->decoKind == 4) {
                    float cxm = x + cm.width / 2;
                    float hw = std::min(cm.width / 2, em * 0.28f);
                    ctx.box.lines.push_back(
                        {cxm - hw, topY + ruleH + em * 0.08f, cxm, topY});
                    ctx.box.lines.push_back(
                        {cxm, topY, cxm + hw, topY + ruleH + em * 0.08f});
                }
            }
            m.width = std::max(inner.width, cm.width);
            m.ascent = cm.ascent + gap + ruleH + em * 0.08f;
            m.descent = cm.descent;
            return m;
        }
    }
    return m;
}

Metrics layoutNode(LayoutCtx& ctx, const MNodePtr& node, float size,
                   float x, float baselineY) {
    return layoutNodeImpl(ctx, node, size, x, baselineY, true);
}

Metrics measureNode(LayoutCtx& ctx, const MNodePtr& node, float size) {
    return layoutNodeImpl(ctx, node, size, 0, 0, false);
}

// ---------------------------------------------------------------------------
// Cache + public API
// ---------------------------------------------------------------------------

std::unordered_map<std::wstring, MathBoxPtr> g_mathCache;

} // namespace

MathBoxPtr mathParse(App& app, const std::wstring& latex, float fontSize,
                     bool display) {
    std::wstring key = latex + L"|" + std::to_wstring((int)(fontSize * 4)) +
                       (display ? L"|d" : L"|i");
    auto it = g_mathCache.find(key);
    if (it != g_mathCache.end()) return it->second;

    MNodePtr root = parseLatex(latex);
    if (!root) {
        g_mathCache[key] = nullptr;   // remember the failure too
        return nullptr;
    }

    auto box = std::make_shared<MathBox>();
    LayoutCtx ctx{app, *box, display};
    Metrics probe = measureNode(ctx, root, fontSize);
    float pad = display ? fontSize * 0.15f : 0.0f;
    float baseline = probe.ascent + pad;
    Metrics final = layoutNode(ctx, root, fontSize, pad, baseline);
    box->width = final.width + pad * 2;
    box->height = final.ascent + final.descent + pad * 2;
    box->baseline = baseline;

    g_mathCache[key] = box;
    return box;
}

float mathBoxWidth(const MathBoxPtr& box) { return box ? box->width : 0; }
float mathBoxHeight(const MathBoxPtr& box) { return box ? box->height : 0; }
float mathBoxBaseline(const MathBoxPtr& box) { return box ? box->baseline : 0; }

void mathBoxDrawTo(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                   const MathBoxPtr& box, float x, float y,
                   const D2D1_COLOR_F& color) {
    if (!box || !target || !brush) return;
    brush->SetColor(color);
    for (const auto& run : box->runs) {
        target->DrawTextLayout(
            D2D1::Point2F(x + run.x, y + run.y), run.layout, brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    for (const auto& rule : box->rules) {
        target->FillRectangle(
            D2D1::RectF(x + rule.x, y + rule.y, x + rule.x + rule.w,
                        y + rule.y + rule.h),
            brush);
    }
    for (const auto& line : box->lines) {
        target->DrawLine(
            D2D1::Point2F(x + line.x1, y + line.y1),
            D2D1::Point2F(x + line.x2, y + line.y2), brush,
            line.thickness);
    }
}

void mathBoxDraw(App& app, const MathBoxPtr& box, float x, float y,
                 const D2D1_COLOR_F& color) {
    mathBoxDrawTo(app.renderTarget, app.brush, box, x, y, color);
}

void mathBoxRetain(App& app, const MathBoxPtr& box, float x, float y,
                   const D2D1_COLOR_F& color) {
    if (!box) return;
    for (const auto& run : box->runs) {
        if (!run.layout) continue;
        run.layout->AddRef();
        App::LayoutTextRun r;
        r.layout = run.layout;
        r.pos = D2D1::Point2F(x + run.x, y + run.y);
        // Whole-box bounds keep viewport culling conservative and correct
        r.bounds = D2D1::RectF(x, y, x + box->width, y + box->height);
        r.color = color;
        r.docStart = 0;
        r.docLength = 0;
        r.selectable = false;
        app.layoutTextRuns.push_back(r);
    }
    for (const auto& rule : box->rules) {
        app.layoutRects.push_back(
            {D2D1::RectF(x + rule.x, y + rule.y, x + rule.x + rule.w,
                         y + rule.y + rule.h),
             color});
    }
    for (const auto& line : box->lines) {
        app.layoutLines.push_back({D2D1::Point2F(x + line.x1, y + line.y1),
                                   D2D1::Point2F(x + line.x2, y + line.y2),
                                   color, line.thickness});
    }
}

void mathClearCache() {
    g_mathCache.clear();
}

// --- SVG export (#export_as) ---

namespace {

std::string svgUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), out.data(),
                        len, nullptr, nullptr);
    return out;
}

std::string svgEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string svgNum(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

}  // namespace

std::string mathBoxSvg(const MathBoxPtr& box, const std::string& colorCss,
                       const std::string& fontFamilyCss) {
    if (!box) return {};
    std::string s;
    s += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" +
         svgNum(box->width) + "\" height=\"" + svgNum(box->height) +
         "\" viewBox=\"0 0 " + svgNum(box->width) + " " +
         svgNum(box->height) + "\" role=\"math\">";
    for (const auto& r : box->rules) {
        s += "<rect x=\"" + svgNum(r.x) + "\" y=\"" + svgNum(r.y) +
             "\" width=\"" + svgNum(r.w) + "\" height=\"" + svgNum(r.h) +
             "\" fill=\"" + colorCss + "\"/>";
    }
    for (const auto& l : box->lines) {
        s += "<line x1=\"" + svgNum(l.x1) + "\" y1=\"" + svgNum(l.y1) +
             "\" x2=\"" + svgNum(l.x2) + "\" y2=\"" + svgNum(l.y2) +
             "\" stroke=\"" + colorCss + "\" stroke-width=\"" + svgNum(l.thickness) + "\"/>";
    }
    for (const auto& r : box->runs) {
        s += "<text x=\"" + svgNum(r.x) + "\" y=\"" +
             svgNum(r.y + r.baseline) + "\" font-size=\"" + svgNum(r.size) +
             "\" font-family=\"" + fontFamilyCss + "\" fill=\"" + colorCss +
             "\"";
        if (r.italic) s += " font-style=\"italic\"";
        if (r.bold) s += " font-weight=\"bold\"";
        s += ">" + svgEscape(svgUtf8(r.text)) + "</text>";
    }
    s += "</svg>";
    return s;
}
