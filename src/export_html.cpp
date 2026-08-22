// HTML export (#export_as): walks the parsed element tree and emits one
// standalone .html with the active theme's palette inlined. Mermaid
// diagrams re-run their native parsers and come out as inline SVG; math
// re-uses the TeX layout engine through mathBoxSvg. No third-party code,
// no external assets: local images are embedded as data URIs.

#include "export.h"

#include "editor.h"
#include "i18n.h"
#include "math_render.h"
#include "mermaid.h"
#include "mermaid_ext.h"
#include "render.h"
#include "utils.h"

#include <commdlg.h>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace qmd;

namespace {

constexpr float kDiagramFontSize = 14.0f;

// --- small emit helpers ---

std::string htmlEscape(const std::string& text) {
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

std::string num(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

std::string colorCss(D2D1_COLOR_F c) {
    char buf[48];
    if (c.a >= 0.999f) {
        snprintf(buf, sizeof(buf), "#%02x%02x%02x", (int)(c.r * 255 + 0.5f),
                 (int)(c.g * 255 + 0.5f), (int)(c.b * 255 + 0.5f));
    } else {
        snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.2f)",
                 (int)(c.r * 255 + 0.5f), (int)(c.g * 255 + 0.5f),
                 (int)(c.b * 255 + 0.5f), c.a);
    }
    return buf;
}

std::string base64(const std::string& data) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 2 < data.size()) {
        unsigned v = ((unsigned char)data[i] << 16) |
                     ((unsigned char)data[i + 1] << 8) |
                     (unsigned char)data[i + 2];
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += alphabet[v & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        unsigned v = (unsigned char)data[i] << 16;
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        unsigned v = ((unsigned char)data[i] << 16) |
                     ((unsigned char)data[i + 1] << 8);
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += alphabet[(v >> 6) & 63];
        out += "=";
    }
    return out;
}

const char* mimeForPath(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return nullptr;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "webp") return "image/webp";
    if (ext == "bmp") return "image/bmp";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    return nullptr;
}

bool readBinary(const std::wstring& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return !out.empty() && out.size() < 20u * 1024 * 1024;
}

// --- diagram SVG ---

struct DiagramCtx {
    App& app;
    std::string bodyFontCss;
    std::string monoFontCss;
};

std::string prpaint(const App& app, const mermaidext::Prim& p) {
    std::string s;
    if (p.fill != mermaidext::Role::None) {
        s += " fill=\"" + colorCss(resolveDiagramRole(app, p, p.fill)) + "\"";
    } else {
        s += " fill=\"none\"";
    }
    if (p.stroke != mermaidext::Role::None) {
        float width = p.strokeWidth > 0 ? p.strokeWidth : 1.2f;
        s += " stroke=\"" + colorCss(resolveDiagramRole(app, p, p.stroke)) +
             "\" stroke-width=\"" + num(width) + "\"";
        if (p.dashed) {
            // D2D's stock DASH pattern: 2-on 2-off in stroke-width units
            s += " stroke-dasharray=\"" + num(width * 2.0f) + " " +
                 num(width * 2.0f) + "\"";
        }
    }
    return s;
}

// The viewer's diagram text base: 14px body / 13px mono at scale 1
float diagramFontSize(const mermaidext::TextStyle& style, float scale) {
    return (style.mono ? 13.0f : kDiagramFontSize) *
           (style.scale > 0 ? style.scale : 1.0f) * scale;
}

// Wrapped text broken into physical lines with their DirectWrite baselines,
// so SVG <text> rows land where the viewer's text layout puts them
struct WrappedLines {
    std::vector<std::string> lines;
    std::vector<float> baselines;  // from block top
    float width = 0.0f;
    float height = 0.0f;
};

WrappedLines wrapLines(App& app, const std::wstring& text, float wrapWidth,
                       const mermaidext::TextStyle& style, float scale) {
    WrappedLines out;
    if (text.empty() || !app.dwriteFactory) return out;
    constexpr float kHuge = 100000.0f;
    IDWriteTextFormat* format = nullptr;
    app.dwriteFactory->CreateTextFormat(
        style.mono ? app.theme.codeFontFamily : app.theme.fontFamily, nullptr,
        style.bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, diagramFontSize(style, scale), L"en-us",
        &format);
    if (!format) return out;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(
        text.c_str(), (UINT32)text.size(), format,
        wrapWidth > 0.0f ? wrapWidth : kHuge, kHuge, &layout);
    format->Release();
    if (!layout) return out;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    out.width = metrics.widthIncludingTrailingWhitespace;
    out.height = metrics.height;
    UINT32 lineCount = 0;
    layout->GetLineMetrics(nullptr, 0, &lineCount);
    std::vector<DWRITE_LINE_METRICS> lm(lineCount);
    if (lineCount) layout->GetLineMetrics(lm.data(), lineCount, &lineCount);
    layout->Release();
    float top = 0.0f;
    size_t pos = 0;
    for (UINT32 i = 0; i < lineCount && pos <= text.size(); i++) {
        size_t visible = lm[i].length - lm[i].newlineLength;
        std::wstring line = text.substr(pos, visible);
        pos += lm[i].length;
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t'))
            line.pop_back();
        out.lines.push_back(toUtf8(line));
        out.baselines.push_back(top + lm[i].baseline);
        top += lm[i].height;
    }
    return out;
}

void emitDiagramText(DiagramCtx& ctx, const mermaidext::Prim& p,
                     std::string& s) {
    float fontSize = diagramFontSize(p.style, 1.0f);
    // Half a pixel of slack: boxes sized to exactly fit measured text must
    // not re-wrap on the float boundary
    WrappedLines wrapped = wrapLines(ctx.app, toWide(p.text),
                                     p.x2 - p.x1 + 0.5f, p.style, 1.0f);
    float top;
    if (p.alignV < 0) top = p.y1;
    else if (p.alignV > 0) top = p.y2 - wrapped.height;
    else top = (p.y1 + p.y2) * 0.5f - wrapped.height * 0.5f;

    float x;
    const char* anchor;
    if (p.alignH < 0) { x = p.x1; anchor = "start"; }
    else if (p.alignH > 0) { x = p.x2; anchor = "end"; }
    else { x = (p.x1 + p.x2) * 0.5f; anchor = "middle"; }

    D2D1_COLOR_F color = resolveDiagramRole(
        ctx.app, p,
        p.fill != mermaidext::Role::None ? p.fill : mermaidext::Role::Text);
    const std::string& family =
        p.style.mono ? ctx.monoFontCss : ctx.bodyFontCss;

    for (size_t i = 0; i < wrapped.lines.size(); i++) {
        if (wrapped.lines[i].empty()) continue;
        s += "<text x=\"" + num(x) + "\" y=\"" +
             num(top + wrapped.baselines[i]) + "\" text-anchor=\"" + anchor +
             "\" font-size=\"" + num(fontSize) + "\" font-family=\"" +
             family + "\" fill=\"" + colorCss(color) + "\"";
        if (p.style.bold) s += " font-weight=\"600\"";
        if (p.style.italic) s += " font-style=\"italic\"";
        s += ">" + htmlEscape(wrapped.lines[i]) + "</text>";
    }
}

void emitArrowHead(const App& app, const mermaidext::Prim& p, std::string& s) {
    float dx = p.x2 - p.x1, dy = p.y2 - p.y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) return;
    float ux = dx / len, uy = dy / len;
    float size = 8.0f, half = 3.6f;
    float bx = p.x2 - ux * size, by = p.y2 - uy * size;
    float px = -uy, py = ux;
    D2D1_COLOR_F color = resolveDiagramRole(
        app, p,
        p.stroke != mermaidext::Role::None ? p.stroke
                                           : mermaidext::Role::Text);
    if (p.openArrow) {
        s += "<path d=\"M " + num(bx + px * half) + " " + num(by + py * half) +
             " L " + num(p.x2) + " " + num(p.y2) + " L " +
             num(bx - px * half) + " " + num(by - py * half) +
             "\" fill=\"none\" stroke=\"" + colorCss(color) +
             "\" stroke-width=\"1.2\"/>";
    } else {
        s += "<polygon points=\"" + num(p.x2) + "," + num(p.y2) + " " +
             num(bx + px * half) + "," + num(by + py * half) + " " +
             num(bx - px * half) + "," + num(by - py * half) + "\" fill=\"" +
             colorCss(color) + "\"/>";
    }
}

// --- flowchart primitives (mirrors layoutMermaidDiagram in render.cpp) ---
// The flowchart engine draws through the app's layout structures in the
// viewer; for export it is mirrored once into mermaidext prims that both
// the SVG emitter and the DOCX rasterizer consume.

constexpr float kFlowTextScale = 16.0f / 14.0f;  // body font over diagram base

mermaidext::TextStyle flowTextStyle() {
    mermaidext::TextStyle style;
    style.scale = kFlowTextScale;
    return style;
}

D2D1_COLOR_F flowColor(const mermaid::Color& color) {
    return D2D1::ColorF(((color.rgb >> 16) & 0xFF) / 255.0f,
                        ((color.rgb >> 8) & 0xFF) / 255.0f,
                        (color.rgb & 0xFF) / 255.0f, color.alpha);
}

struct FlowStyle {
    D2D1_COLOR_F fill{};
    D2D1_COLOR_F stroke{};
    D2D1_COLOR_F text{};
    float strokeWidth = 1.5f;
    bool customFill = false;
    bool customStroke = false;
    bool customText = false;
};

FlowStyle flowResolveStyle(const App& app, const mermaid::Diagram& diagram,
                           const mermaid::Node& node, float scale) {
    FlowStyle resolved;
    resolved.fill = app.theme.codeBackground;
    resolved.stroke = app.theme.accent;
    resolved.text = app.theme.text;
    resolved.strokeWidth = 1.5f * scale;
    auto apply = [&](const mermaid::Style& style) {
        if (style.hasFill) {
            resolved.fill = flowColor(style.fill);
            resolved.customFill = true;
        }
        if (style.hasStroke) {
            resolved.stroke = flowColor(style.stroke);
            resolved.customStroke = true;
        }
        if (style.hasText) {
            resolved.text = flowColor(style.text);
            resolved.customText = true;
        }
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

void primCustom(mermaidext::Prim& prim, const D2D1_COLOR_F& color) {
    prim.customR = color.r;
    prim.customG = color.g;
    prim.customB = color.b;
    prim.customA = color.a;
}

// One node outline as a prim (geometry only; paint set by the caller)
mermaidext::Prim flowShapePrim(const mermaid::Node& node,
                               const mermaid::Rect& rect, float scale) {
    using mermaidext::Prim;
    using mermaidext::PrimType;
    Prim shape;
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    switch (node.shape) {
        case mermaid::NodeShape::Diamond:
            shape.type = PrimType::Polygon;
            shape.pts = {{rect.left + w * 0.5f, rect.top},
                         {rect.right, rect.top + h * 0.5f},
                         {rect.left + w * 0.5f, rect.bottom},
                         {rect.left, rect.top + h * 0.5f}};
            break;
        case mermaid::NodeShape::Hexagon: {
            float inset = w * 0.18f;
            shape.type = PrimType::Polygon;
            shape.pts = {{rect.left + inset, rect.top},
                         {rect.right - inset, rect.top},
                         {rect.right, rect.top + h * 0.5f},
                         {rect.right - inset, rect.bottom},
                         {rect.left + inset, rect.bottom},
                         {rect.left, rect.top + h * 0.5f}};
            break;
        }
        case mermaid::NodeShape::Circle:
            shape.type = PrimType::Ellipse;
            shape.x1 = rect.left;
            shape.y1 = rect.top;
            shape.x2 = rect.right;
            shape.y2 = rect.bottom;
            break;
        case mermaid::NodeShape::Stadium:
        case mermaid::NodeShape::RoundedRectangle:
            shape.type = PrimType::RoundRect;
            shape.radius = node.shape == mermaid::NodeShape::Stadium
                               ? h * 0.5f
                               : 8.0f * scale;
            shape.x1 = rect.left;
            shape.y1 = rect.top;
            shape.x2 = rect.right;
            shape.y2 = rect.bottom;
            break;
        case mermaid::NodeShape::Rectangle:
        default:
            shape.type = PrimType::Rect;
            shape.x1 = rect.left;
            shape.y1 = rect.top;
            shape.x2 = rect.right;
            shape.y2 = rect.bottom;
            break;
    }
    return shape;
}

}  // namespace

mermaidext::Built buildFlowchartPrims(App& app, const std::string& source,
                                      float scale) {
    using mermaidext::Prim;
    using mermaidext::PrimType;
    using mermaidext::Role;
    mermaidext::Built result;
    auto parsed = mermaid::parse(source);
    if (!parsed.success || parsed.diagram.nodes.empty()) return result;
    const auto& diagram = parsed.diagram;

    const float maxLabelWidth = 280.0f * scale;
    const float paddingX = 18.0f * scale;
    const float paddingY = 12.0f * scale;
    const float minWidth = 120.0f * scale;
    const float minHeight = 52.0f * scale;
    const float labelPadX = 6.0f * scale;
    const float labelPadY = 4.0f * scale;
    const float arrowSize = 8.0f * scale;
    mermaidext::TextStyle textStyle = flowTextStyle();

    std::vector<std::wstring> labels;
    std::vector<mermaid::Size> nodeSizes;
    std::vector<FlowStyle> styles;
    labels.reserve(diagram.nodes.size());
    nodeSizes.reserve(diagram.nodes.size());
    styles.reserve(diagram.nodes.size());
    for (const auto& node : diagram.nodes) {
        std::wstring label = toWide(node.label.empty() ? node.id : node.label);
        WrappedLines measured =
            wrapLines(app, label, maxLabelWidth, textStyle, scale);
        float width = std::max(minWidth, measured.width + paddingX * 2.0f);
        float height = std::max(minHeight, measured.height + paddingY * 2.0f);
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
        labels.push_back(std::move(label));
        nodeSizes.push_back({width, height});
        styles.push_back(flowResolveStyle(app, diagram, node, scale));
    }

    bool vertical = diagram.direction == mermaid::Direction::TopToBottom ||
                    diagram.direction == mermaid::Direction::BottomToTop;
    struct FlowEdgeLabel {
        std::wstring text;
        float width = 0.0f;
        float height = 0.0f;
    };
    float rankGap = 78.0f * scale;
    std::vector<FlowEdgeLabel> edgeLabels(diagram.edges.size());
    for (size_t i = 0; i < diagram.edges.size(); i++) {
        if (diagram.edges[i].label.empty()) continue;
        auto& edgeLabel = edgeLabels[i];
        edgeLabel.text = toWide(diagram.edges[i].label);
        WrappedLines single =
            wrapLines(app, edgeLabel.text, 0.0f, textStyle, scale);
        edgeLabel.width = std::min(
            180.0f * scale,
            std::max(60.0f * scale, single.width + labelPadX * 2.0f));
        WrappedLines wrapped = wrapLines(
            app, edgeLabel.text, edgeLabel.width - labelPadX * 2.0f,
            textStyle, scale);
        edgeLabel.height =
            std::max(28.0f * scale, wrapped.height + labelPadY * 2.0f);
        float labelExtent = vertical ? edgeLabel.height : edgeLabel.width;
        rankGap = std::max(rankGap, labelExtent + 20.0f * scale);
    }

    mermaid::Layout graphLayout =
        mermaid::layout(diagram, nodeSizes, 32.0f * scale, rankGap);
    if (graphLayout.nodes.size() != diagram.nodes.size()) return result;
    const auto& nodeRects = graphLayout.nodes;

    // Subgraph group boxes, finalized leaves-first exactly like the viewer
    constexpr float kNoBox = 3.0e38f;
    std::vector<mermaid::Rect> subBoxes(
        diagram.subgraphs.size(), {kNoBox, kNoBox, -kNoBox, -kNoBox});
    {
        const float boxPad = 10.0f * scale;
        const float titleHeight = 20.0f * scale;
        auto grow = [](mermaid::Rect& box, const mermaid::Rect& rect) {
            box.left = std::min(box.left, rect.left);
            box.top = std::min(box.top, rect.top);
            box.right = std::max(box.right, rect.right);
            box.bottom = std::max(box.bottom, rect.bottom);
        };
        for (size_t sub = diagram.subgraphs.size(); sub-- > 0;) {
            for (size_t node : diagram.subgraphs[sub].nodes) {
                if (node < nodeRects.size()) {
                    grow(subBoxes[sub], nodeRects[node]);
                }
            }
            for (size_t c = sub + 1; c < diagram.subgraphs.size(); c++) {
                if (diagram.subgraphs[c].parent == sub &&
                    subBoxes[c].left != kNoBox) {
                    grow(subBoxes[sub], subBoxes[c]);
                }
            }
            if (subBoxes[sub].left == kNoBox) continue;
            subBoxes[sub].left -= boxPad;
            subBoxes[sub].top -= boxPad + titleHeight;
            subBoxes[sub].right += boxPad;
            subBoxes[sub].bottom += boxPad;
        }
    }

    D2D1_COLOR_F connectorColor = app.theme.text;
    connectorColor.a = app.theme.isDark ? 0.7f : 0.6f;
    D2D1_COLOR_F chipStroke = connectorColor;
    chipStroke.a *= 0.6f;
    D2D1_COLOR_F groupFill = app.theme.codeBackground;
    groupFill.a *= 0.55f;
    D2D1_COLOR_F groupBorder = app.theme.text;
    groupBorder.a = 0.28f;
    D2D1_COLOR_F groupTitle = app.theme.text;
    groupTitle.a = 0.78f;

    struct FlowConn {
        std::vector<D2D1_POINT_2F> points;
        float stroke = 1.4f;
        bool dashed = false;
        bool directed = true;
    };
    struct FlowChip {
        mermaid::Rect rect;
        std::wstring text;
    };
    std::vector<FlowConn> conns;
    std::vector<FlowChip> chips;
    conns.reserve(diagram.edges.size());

    float boundsL = 0.0f, boundsT = 0.0f;
    float boundsR = graphLayout.width, boundsB = graphLayout.height;
    for (const auto& box : subBoxes) {
        if (box.left == kNoBox) continue;
        boundsL = std::min(boundsL, box.left);
        boundsT = std::min(boundsT, box.top);
        boundsR = std::max(boundsR, box.right);
        boundsB = std::max(boundsB, box.bottom);
    }
    size_t exteriorLane = 0;
    std::vector<mermaid::Rect> placedLabelRects;
    placedLabelRects.reserve(diagram.edges.size());
    for (size_t edgeIndex = 0; edgeIndex < diagram.edges.size(); edgeIndex++) {
        const auto& edge = diagram.edges[edgeIndex];
        if (edge.from >= nodeRects.size() || edge.to >= nodeRects.size())
            continue;
        const auto& from = nodeRects[edge.from];
        const auto& to = nodeRects[edge.to];
        FlowConn conn;
        conn.stroke = 1.4f * scale * edge.strokeScale;
        conn.directed = edge.directed;
        conn.dashed = edge.dashed;

        float fromCenterX = (from.left + from.right) * 0.5f;
        float fromCenterY = (from.top + from.bottom) * 0.5f;
        float toCenterX = (to.left + to.right) * 0.5f;
        float toCenterY = (to.top + to.bottom) * 0.5f;
        bool selfLoop = edge.from == edge.to;
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
                (topToBottom ? toCenterY > fromCenterY
                             : toCenterY < fromCenterY);
            if (selfLoop) {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(from.right, fromCenterY);
                D2D1_POINT_2F end = D2D1::Point2F(fromCenterX, from.bottom);
                float laneX = from.right + lane;
                float laneY = from.bottom + lane;
                conn.points = {
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
                conn.points = {
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
                conn.points = {
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
                (leftToRight ? toCenterX > fromCenterX
                             : toCenterX < fromCenterX);
            if (selfLoop) {
                float lane = (36.0f + exteriorLane++ * 14.0f) * scale;
                D2D1_POINT_2F start = D2D1::Point2F(fromCenterX, from.bottom);
                D2D1_POINT_2F end = D2D1::Point2F(from.right, fromCenterY);
                float laneX = from.right + lane;
                float laneY = from.bottom + lane;
                conn.points = {
                    start,
                    D2D1::Point2F(start.x, laneY),
                    D2D1::Point2F(laneX, laneY),
                    D2D1::Point2F(laneX, end.y),
                    end,
                };
            } else if (forward) {
                D2D1_POINT_2F start = D2D1::Point2F(
                    leftToRight ? from.right : from.left, fromCenterY);
                D2D1_POINT_2F end = D2D1::Point2F(
                    leftToRight ? to.left : to.right, toCenterY);
                float middleX = (start.x + end.x) * 0.5f;
                conn.points = {
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
                conn.points = {
                    start,
                    D2D1::Point2F(start.x, laneY),
                    D2D1::Point2F(end.x, laneY),
                    end,
                };
            }
        }

        for (const auto& point : conn.points) {
            boundsL = std::min(boundsL, point.x - arrowSize);
            boundsT = std::min(boundsT, point.y - arrowSize);
            boundsR = std::max(boundsR, point.x + arrowSize);
            boundsB = std::max(boundsB, point.y + arrowSize);
        }

        if (!edge.label.empty()) {
            const auto& edgeLabel = edgeLabels[edgeIndex];
            size_t middle = conn.points.size() / 2;
            const auto& middleStart = conn.points[middle - 1];
            const auto& middleEnd = conn.points[middle];
            float centerX = (middleStart.x + middleEnd.x) * 0.5f;
            float centerY = (middleStart.y + middleEnd.y) * 0.5f;
            auto chipAt = [&](float cx, float cy) {
                return mermaid::Rect{
                    cx - edgeLabel.width * 0.5f, cy - edgeLabel.height * 0.5f,
                    cx + edgeLabel.width * 0.5f, cy + edgeLabel.height * 0.5f};
            };
            mermaid::Rect labelRect = chipAt(centerX, centerY);
            bool segVertical = std::abs(middleEnd.x - middleStart.x) <
                               std::abs(middleEnd.y - middleStart.y);
            float stepX =
                segVertical ? 0.0f : edgeLabel.width + 8.0f * scale;
            float stepY =
                segVertical ? edgeLabel.height + 8.0f * scale : 0.0f;
            auto overlapsPlaced = [&](const mermaid::Rect& rect) {
                for (const auto& placed : placedLabelRects) {
                    if (rect.left < placed.right && rect.right > placed.left &&
                        rect.top < placed.bottom && rect.bottom > placed.top) {
                        return true;
                    }
                }
                return false;
            };
            for (int attempt = 1;
                 attempt <= 8 && overlapsPlaced(labelRect); attempt++) {
                float direction = (attempt % 2 == 1) ? 1.0f : -1.0f;
                float magnitude = (float)((attempt + 1) / 2);
                labelRect = chipAt(centerX + stepX * direction * magnitude,
                                   centerY + stepY * direction * magnitude);
            }
            placedLabelRects.push_back(labelRect);
            boundsL = std::min(boundsL, labelRect.left);
            boundsT = std::min(boundsT, labelRect.top);
            boundsR = std::max(boundsR, labelRect.right);
            boundsB = std::max(boundsB, labelRect.bottom);
            chips.push_back({labelRect, edgeLabel.text});
        }
        conns.push_back(std::move(conn));
    }

    // Emission order = viewer draw order: group boxes, edges, chips and
    // node shapes, text last
    std::vector<Prim> texts;
    for (size_t sub = 0; sub < subBoxes.size(); sub++) {
        const auto& box = subBoxes[sub];
        if (box.left == kNoBox) continue;
        Prim fill;
        fill.type = PrimType::Rect;
        fill.x1 = box.left;
        fill.y1 = box.top;
        fill.x2 = box.right;
        fill.y2 = box.bottom;
        fill.fill = Role::Custom;
        primCustom(fill, groupFill);
        result.prims.push_back(std::move(fill));
        Prim border;
        border.type = PrimType::Rect;
        border.x1 = box.left;
        border.y1 = box.top;
        border.x2 = box.right;
        border.y2 = box.bottom;
        border.stroke = Role::Custom;
        border.strokeWidth = 1.2f * scale;
        primCustom(border, groupBorder);
        result.prims.push_back(std::move(border));

        Prim title;
        title.type = PrimType::Text;
        title.text = diagram.subgraphs[sub].label;
        title.style = textStyle;
        title.fill = Role::Custom;
        primCustom(title, groupTitle);
        title.x1 = box.left + 8.0f * scale;
        title.y1 = box.top + 2.0f * scale;
        title.x2 = box.right - 8.0f * scale;
        title.y2 = box.top + 22.0f * scale;
        texts.push_back(std::move(title));
    }

    for (const auto& conn : conns) {
        for (size_t i = 1; i < conn.points.size(); i++) {
            Prim segment;
            segment.type = PrimType::Line;
            segment.x1 = conn.points[i - 1].x;
            segment.y1 = conn.points[i - 1].y;
            segment.x2 = conn.points[i].x;
            segment.y2 = conn.points[i].y;
            segment.stroke = Role::Custom;
            primCustom(segment, connectorColor);
            segment.strokeWidth = conn.stroke;
            segment.dashed = conn.dashed;
            result.prims.push_back(std::move(segment));
        }
        if (conn.directed && conn.points.size() >= 2) {
            const auto& tip = conn.points.back();
            const auto& previous = conn.points[conn.points.size() - 2];
            float dx = tip.x - previous.x;
            float dy = tip.y - previous.y;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.001f) {
                dx /= length;
                dy /= length;
                float wing = arrowSize * 0.5f;
                const float wings[2][2] = {{dy, -dx}, {-dy, dx}};
                for (const auto& side : wings) {
                    Prim stroke;
                    stroke.type = PrimType::Line;
                    stroke.x1 = tip.x - dx * arrowSize + side[0] * wing;
                    stroke.y1 = tip.y - dy * arrowSize + side[1] * wing;
                    stroke.x2 = tip.x;
                    stroke.y2 = tip.y;
                    stroke.stroke = Role::Custom;
                    primCustom(stroke, connectorColor);
                    stroke.strokeWidth = conn.stroke;
                    result.prims.push_back(std::move(stroke));
                }
            }
        }
    }

    for (const auto& chip : chips) {
        Prim box;
        box.type = PrimType::RoundRect;
        box.radius = 4.0f * scale;
        box.x1 = chip.rect.left;
        box.y1 = chip.rect.top;
        box.x2 = chip.rect.right;
        box.y2 = chip.rect.bottom;
        box.fill = Role::Fill;
        box.stroke = Role::Custom;
        primCustom(box, chipStroke);
        box.strokeWidth = 1.2f * scale;
        result.prims.push_back(std::move(box));

        Prim label;
        label.type = PrimType::Text;
        label.text = toUtf8(chip.text);
        label.style = textStyle;
        label.fill = Role::Text;
        label.x1 = chip.rect.left + labelPadX;
        label.y1 = chip.rect.top + labelPadY;
        label.x2 = chip.rect.right - labelPadX;
        label.y2 = chip.rect.bottom - labelPadY;
        texts.push_back(std::move(label));
    }

    for (size_t i = 0; i < diagram.nodes.size(); i++) {
        const auto& node = diagram.nodes[i];
        const auto& rect = nodeRects[i];
        const auto& style = styles[i];

        // Fill and stroke may carry different custom colors; a prim holds
        // one custom slot, so styled nodes split into two prims
        if (style.customFill || style.customStroke) {
            Prim fill = flowShapePrim(node, rect, scale);
            if (style.customFill) {
                fill.fill = Role::Custom;
                primCustom(fill, style.fill);
            } else {
                fill.fill = Role::Fill;
            }
            result.prims.push_back(std::move(fill));
            Prim stroke = flowShapePrim(node, rect, scale);
            if (style.customStroke) {
                stroke.stroke = Role::Custom;
                primCustom(stroke, style.stroke);
            } else {
                stroke.stroke = Role::Stroke;
            }
            stroke.strokeWidth = style.strokeWidth;
            result.prims.push_back(std::move(stroke));
        } else {
            Prim shape = flowShapePrim(node, rect, scale);
            shape.fill = Role::Fill;
            shape.stroke = Role::Stroke;
            shape.strokeWidth = style.strokeWidth;
            result.prims.push_back(std::move(shape));
        }

        float insetX = paddingX;
        float insetY = paddingY;
        if (node.shape == mermaid::NodeShape::Diamond) {
            insetX = (rect.right - rect.left) * 0.18f;
            insetY = (rect.bottom - rect.top) * 0.18f;
        } else if (node.shape == mermaid::NodeShape::Hexagon) {
            insetX = (rect.right - rect.left) * 0.18f;
        }
        Prim label;
        label.type = PrimType::Text;
        label.text = toUtf8(labels[i]);
        label.style = textStyle;
        if (style.customText) {
            label.fill = Role::Custom;
            primCustom(label, style.text);
        } else {
            label.fill = Role::Text;
        }
        label.x1 = rect.left + insetX;
        label.y1 = rect.top + insetY;
        label.x2 = rect.right - insetX;
        label.y2 = rect.bottom - insetY;
        texts.push_back(std::move(label));
    }
    for (auto& text : texts) result.prims.push_back(std::move(text));

    // Shift everything into positive coordinates
    float shiftX = -boundsL;
    float shiftY = -boundsT;
    for (auto& prim : result.prims) {
        prim.x1 += shiftX;
        prim.x2 += shiftX;
        prim.y1 += shiftY;
        prim.y2 += shiftY;
        for (auto& point : prim.pts) {
            point.x += shiftX;
            point.y += shiftY;
        }
    }
    result.width = boundsR - boundsL;
    result.height = boundsB - boundsT;
    result.ok = true;
    return result;
}

// Any native diagram source -> prims at the given scale (flowcharts via
// their own mirror, every other family via mermaidext). ok=false means the
// caller should fall back to the source block.
mermaidext::Built buildDiagramPrims(App& app, const std::string& sourceUtf8,
                                    float scale) {
    mermaidext::Kind kind = mermaidext::detectKind(sourceUtf8);
    if (kind == mermaidext::Kind::None) return {};
    if (kind == mermaidext::Kind::Flowchart)
        return buildFlowchartPrims(app, sourceUtf8, scale);

    auto measure = [&](const std::string& text,
                       const mermaidext::TextStyle& style,
                       float wrapWidth) -> mermaidext::Size {
        if (text.empty() || !app.dwriteFactory) return {};
        std::wstring wide = toWide(text);
        IDWriteTextFormat* format = nullptr;
        const wchar_t* family =
            style.mono ? app.theme.codeFontFamily : app.theme.fontFamily;
        app.dwriteFactory->CreateTextFormat(
            family, nullptr,
            style.bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                       : DWRITE_FONT_WEIGHT_NORMAL,
            style.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, diagramFontSize(style, scale),
            L"en-us", &format);
        if (!format) return {};
        constexpr float kHuge = 100000.0f;
        IDWriteTextLayout* layout = nullptr;
        app.dwriteFactory->CreateTextLayout(
            wide.c_str(), (UINT32)wide.size(), format,
            wrapWidth > 0.0f ? wrapWidth : kHuge, kHuge, &layout);
        format->Release();
        if (!layout) return {};
        if (wrapWidth <= 0.0f) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        layout->Release();
        return {metrics.widthIncludingTrailingWhitespace, metrics.height};
    };

    return mermaidext::build(kind, sourceUtf8, measure, scale);
}

namespace {

// Native diagram -> standalone SVG; empty string = caller shows the source
std::string diagramSvg(App& app, const std::string& sourceUtf8,
                       const std::string& bodyFontCss,
                       const std::string& monoFontCss) {
    mermaidext::Built built = buildDiagramPrims(app, sourceUtf8, 1.0f);
    if (!built.ok || built.prims.empty()) return {};

    DiagramCtx ctx{app, bodyFontCss, monoFontCss};
    std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" +
                    num(built.width) + "\" height=\"" + num(built.height) +
                    "\" viewBox=\"0 0 " + num(built.width) + " " +
                    num(built.height) + "\">";
    for (const auto& p : built.prims) {
        switch (p.type) {
            case mermaidext::PrimType::Rect:
                s += "<rect x=\"" + num(p.x1) + "\" y=\"" + num(p.y1) +
                     "\" width=\"" + num(p.x2 - p.x1) + "\" height=\"" +
                     num(p.y2 - p.y1) + "\"" + prpaint(app, p) + "/>";
                break;
            case mermaidext::PrimType::RoundRect:
                s += "<rect x=\"" + num(p.x1) + "\" y=\"" + num(p.y1) +
                     "\" width=\"" + num(p.x2 - p.x1) + "\" height=\"" +
                     num(p.y2 - p.y1) + "\" rx=\"" + num(p.radius) + "\"" +
                     prpaint(app, p) + "/>";
                break;
            case mermaidext::PrimType::Ellipse:
                s += "<ellipse cx=\"" + num((p.x1 + p.x2) * 0.5f) +
                     "\" cy=\"" + num((p.y1 + p.y2) * 0.5f) + "\" rx=\"" +
                     num((p.x2 - p.x1) * 0.5f) + "\" ry=\"" +
                     num((p.y2 - p.y1) * 0.5f) + "\"" + prpaint(app, p) +
                     "/>";
                break;
            case mermaidext::PrimType::Line:
                s += "<line x1=\"" + num(p.x1) + "\" y1=\"" + num(p.y1) +
                     "\" x2=\"" + num(p.x2) + "\" y2=\"" + num(p.y2) + "\"" +
                     prpaint(app, p) + "/>";
                if (p.arrow || p.openArrow) emitArrowHead(app, p, s);
                break;
            case mermaidext::PrimType::Polygon: {
                s += "<polygon points=\"";
                for (size_t i = 0; i < p.pts.size(); i++) {
                    if (i) s += " ";
                    s += num(p.pts[i].x) + "," + num(p.pts[i].y);
                }
                s += "\"" + prpaint(app, p) + "/>";
                break;
            }
            case mermaidext::PrimType::Slice: {
                float r = p.radius;
                float sx = p.x1 + r * std::cos(p.a0);
                float sy = p.y1 + r * std::sin(p.a0);
                float ex = p.x1 + r * std::cos(p.a1);
                float ey = p.y1 + r * std::sin(p.a1);
                int large = (p.a1 - p.a0) > 3.14159265f ? 1 : 0;
                s += "<path d=\"M " + num(p.x1) + " " + num(p.y1) + " L " +
                     num(sx) + " " + num(sy) + " A " + num(r) + " " + num(r) +
                     " 0 " + std::to_string(large) + " 1 " + num(ex) + " " +
                     num(ey) + " Z\"" + prpaint(app, p) + "/>";
                break;
            }
            case mermaidext::PrimType::Text:
                emitDiagramText(ctx, p, s);
                break;
        }
    }
    s += "</svg>";
    return s;
}

// --- element tree walker ---

struct ExportCtx {
    App& app;
    std::string out;
    std::wstring baseDir;      // for resolving relative image paths
    std::string bodyFontCss;
    std::string monoFontCss;
    std::string textColorCss;
};

void walk(ExportCtx& ctx, const ElementPtr& elem);

void walkChildren(ExportCtx& ctx, const ElementPtr& elem) {
    for (const auto& child : elem->children) walk(ctx, child);
}

void emitImage(ExportCtx& ctx, const ElementPtr& elem) {
    std::string src = elem->url;
    bool remote = src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0;
    if (!remote) {
        std::wstring widePath = toWide(src);
        // Resolve relative to the document folder
        if (widePath.size() < 2 || widePath[1] != L':') {
            widePath = ctx.baseDir + L"\\" + widePath;
        }
        std::string data;
        const char* mime = mimeForPath(src);
        if (mime && readBinary(widePath, data)) {
            src = std::string("data:") + mime + ";base64," + base64(data);
        }
    }
    std::string alt;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::Text) alt += child->text;
    }
    ctx.out += "<img src=\"" + htmlEscape(src) + "\" alt=\"" +
               htmlEscape(alt) + "\"/>";
}

void emitMath(ExportCtx& ctx, const ElementPtr& elem, bool display) {
    std::wstring tex = toWide(elem->text);
    MathBoxPtr box = mathParse(ctx.app, tex, display ? 18.0f : 16.0f, display);
    if (box) {
        std::string svg =
            mathBoxSvg(box, ctx.textColorCss, ctx.bodyFontCss);
        if (display) {
            ctx.out += "<div class=\"math-display\">" + svg + "</div>";
        } else {
            ctx.out += "<span class=\"math-inline\">" + svg + "</span>";
        }
        return;
    }
    // Unsupported TeX falls back to the raw source, like the viewer
    if (display) {
        ctx.out += "<pre><code>$$" + htmlEscape(elem->text) + "$$</code></pre>";
    } else {
        ctx.out += "<code>$" + htmlEscape(elem->text) + "$</code>";
    }
}

void walk(ExportCtx& ctx, const ElementPtr& elem) {
    if (!elem) return;
    switch (elem->type) {
        case ElementType::Document:
            walkChildren(ctx, elem);
            break;
        case ElementType::Properties:
            break;  // frontmatter stays out of the export, like print
        case ElementType::Paragraph:
            ctx.out += "<p>";
            walkChildren(ctx, elem);
            ctx.out += "</p>\n";
            break;
        case ElementType::Heading: {
            int level = std::max(1, std::min(6, elem->level));
            ctx.out += "<h" + std::to_string(level) + ">";
            walkChildren(ctx, elem);
            ctx.out += "</h" + std::to_string(level) + ">\n";
            break;
        }
        case ElementType::CodeBlock: {
            // Mermaid fences stay CodeBlocks in the parse tree; the viewer
            // (and this exporter) decide at render time, like layoutCodeBlock
            if (elem->language == "mermaid") {
                std::string source;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        source += child->text;
                    }
                }
                std::string svg = diagramSvg(ctx.app, source, ctx.bodyFontCss,
                                             ctx.monoFontCss);
                if (!svg.empty()) {
                    ctx.out += "<div class=\"diagram\">" + svg + "</div>\n";
                    break;
                }
            }
            ctx.out += "<pre><code";
            if (!elem->language.empty()) {
                ctx.out +=
                    " class=\"language-" + htmlEscape(elem->language) + "\"";
            }
            ctx.out += ">";
            for (const auto& child : elem->children) {
                if (child->type == ElementType::Text) {
                    ctx.out += htmlEscape(child->text);
                }
            }
            ctx.out += "</code></pre>\n";
            break;
        }
        case ElementType::MermaidDiagram: {
            std::string svg = diagramSvg(ctx.app, elem->text, ctx.bodyFontCss,
                                         ctx.monoFontCss);
            if (!svg.empty()) {
                ctx.out += "<div class=\"diagram\">" + svg + "</div>\n";
            } else {
                ctx.out += "<pre><code class=\"language-mermaid\">" +
                           htmlEscape(elem->text) + "</code></pre>\n";
            }
            break;
        }
        case ElementType::BlockQuote: {
            // GitHub alert callouts: class for the palette plus the same
            // icon-and-label title line the viewer draws
            static const struct {
                const char* cls;
                const char* title;  // icon (text presentation) + label
            } kAlert[] = {
                {"note", "\xE2\x93\x98\xEF\xB8\x8E  Note"},
                {"tip", "\xF0\x9F\x92\xA1\xEF\xB8\x8E  Tip"},
                {"important", "\xE2\x9D\x97\xEF\xB8\x8E  Important"},
                {"warning", "\xE2\x9A\xA0\xEF\xB8\x8E  Warning"},
                {"caution", "\xE2\x9B\x94\xEF\xB8\x8E  Caution"},
            };
            int kind =
                elem->alertKind >= 1 && elem->alertKind <= 5 ? elem->alertKind
                                                             : 0;
            if (kind) {
                const auto& a = kAlert[kind - 1];
                ctx.out += std::string("<blockquote class=\"alert-") + a.cls +
                           "\"><p class=\"alert-title\">" + a.title + "</p>";
            } else {
                ctx.out += "<blockquote>";
            }
            walkChildren(ctx, elem);
            ctx.out += "</blockquote>\n";
            break;
        }
        case ElementType::List:
            if (elem->ordered) {
                ctx.out += "<ol";
                if (elem->start != 1) {
                    ctx.out += " start=\"" + std::to_string(elem->start) + "\"";
                }
                ctx.out += ">";
            } else {
                ctx.out += "<ul>";
            }
            walkChildren(ctx, elem);
            ctx.out += elem->ordered ? "</ol>\n" : "</ul>\n";
            break;
        case ElementType::ListItem:
            if (elem->isTask) {
                ctx.out += "<li class=\"task\"><input type=\"checkbox\" "
                           "disabled";
                if (elem->taskChecked) ctx.out += " checked";
                ctx.out += "/> ";
            } else {
                ctx.out += "<li>";
            }
            walkChildren(ctx, elem);
            ctx.out += "</li>";
            break;
        case ElementType::HorizontalRule:
            ctx.out += "<hr/>\n";
            break;
        case ElementType::Table:
            ctx.out += "<table>\n";
            for (size_t i = 0; i < elem->children.size(); i++) {
                const auto& row = elem->children[i];
                if (row->type != ElementType::TableRow) continue;
                ctx.out += "<tr>";
                const char* cellTag = i == 0 ? "th" : "td";
                for (const auto& cell : row->children) {
                    if (cell->type != ElementType::TableCell) continue;
                    ctx.out += std::string("<") + cellTag;
                    if (cell->align == 2) ctx.out += " align=\"center\"";
                    else if (cell->align == 3) ctx.out += " align=\"right\"";
                    ctx.out += ">";
                    walkChildren(ctx, cell);
                    ctx.out += std::string("</") + cellTag + ">";
                }
                ctx.out += "</tr>\n";
            }
            ctx.out += "</table>\n";
            break;
        case ElementType::TableRow:
        case ElementType::TableCell:
            walkChildren(ctx, elem);  // reached only outside a Table
            break;
        case ElementType::HtmlBlock:
            walkChildren(ctx, elem);
            break;
        case ElementType::Text:
            ctx.out += htmlEscape(elem->text);
            break;
        case ElementType::Code:
            ctx.out += "<code>";
            ctx.out += htmlEscape(elem->text);
            walkChildren(ctx, elem);
            ctx.out += "</code>";
            break;
        case ElementType::Emphasis:
            ctx.out += "<em>";
            walkChildren(ctx, elem);
            ctx.out += "</em>";
            break;
        case ElementType::Strong:
            ctx.out += "<strong>";
            walkChildren(ctx, elem);
            ctx.out += "</strong>";
            break;
        case ElementType::Highlight:
            ctx.out += "<mark>";
            walkChildren(ctx, elem);
            ctx.out += "</mark>";
            break;
        case ElementType::Strikethrough:
            ctx.out += "<del>";
            walkChildren(ctx, elem);
            ctx.out += "</del>";
            break;
        case ElementType::Superscript:
            ctx.out += "<sup>";
            walkChildren(ctx, elem);
            ctx.out += "</sup>";
            break;
        case ElementType::Subscript:
            ctx.out += "<sub>";
            walkChildren(ctx, elem);
            ctx.out += "</sub>";
            break;
        case ElementType::Link: {
            bool wiki = elem->url.rfind("wiki:", 0) == 0;
            if (wiki) {
                // Single-file export: wiki targets stay as styled text
                ctx.out += "<span class=\"wikilink\">";
                walkChildren(ctx, elem);
                ctx.out += "</span>";
            } else {
                ctx.out += "<a href=\"" + htmlEscape(elem->url) + "\"";
                if (!elem->title.empty()) {
                    ctx.out += " title=\"" + htmlEscape(elem->title) + "\"";
                }
                ctx.out += ">";
                walkChildren(ctx, elem);
                ctx.out += "</a>";
            }
            break;
        }
        case ElementType::Image:
            emitImage(ctx, elem);
            break;
        case ElementType::SoftBreak:
            ctx.out += "\n";
            break;
        case ElementType::HardBreak:
            ctx.out += "<br/>\n";
            break;
        case ElementType::Ruby:
            ctx.out += "<ruby>";
            for (const auto& child : elem->children) {
                if (child->type == ElementType::RubyText) {
                    ctx.out += "<rt>" + htmlEscape(child->text);
                    for (const auto& g : child->children) walk(ctx, g);
                    ctx.out += "</rt>";
                } else {
                    walk(ctx, child);
                }
            }
            ctx.out += "</ruby>";
            break;
        case ElementType::RubyText:
            break;  // handled inside Ruby
        case ElementType::MathInline:
            emitMath(ctx, elem, false);
            break;
        case ElementType::MathDisplay:
            emitMath(ctx, elem, true);
            break;
    }
}

std::string themeCss(const App& app, const std::string& bodyFont,
                     const std::string& monoFont) {
    const D2DTheme& t = app.theme;
    D2D1_COLOR_F border = t.text;
    border.a = 0.18f;
    D2D1_COLOR_F mutedText = t.text;
    mutedText.a = 0.65f;
    std::string css;
    css += "body{background:" + colorCss(t.background) + ";color:" +
           colorCss(t.text) + ";font-family:" + bodyFont +
           ";line-height:1.6;max-width:820px;margin:0 auto;"
           "padding:32px 24px;}";
    css += "h1,h2,h3,h4,h5,h6{color:" + colorCss(t.heading) +
           ";line-height:1.25;}";
    css += "h1{border-bottom:2px solid " + colorCss(t.accent) +
           ";padding-bottom:8px;}";
    css += "a{color:" + colorCss(t.link) + ";}";
    css += ".wikilink{color:" + colorCss(t.link) +
           ";border-bottom:1px dashed " + colorCss(t.link) + ";}";
    css += "code{font-family:" + monoFont + ";background:" +
           colorCss(t.codeBackground) + ";color:" + colorCss(t.code) +
           ";padding:2px 6px;border-radius:4px;font-size:0.9em;}";
    css += "pre{background:" + colorCss(t.codeBackground) +
           ";padding:14px 16px;border-radius:8px;overflow-x:auto;}";
    css += "pre code{background:none;padding:0;}";
    css += "blockquote{border-left:4px solid " +
           colorCss(t.blockquoteBorder) +
           ";margin:1em 0;padding:2px 18px;color:" + colorCss(mutedText) +
           ";}";
    // github.com's alert accents, light or dark side picked by the theme
    // (the same table the viewer uses)
    {
        static const struct {
            const char* cls;
            uint32_t light;
            uint32_t dark;
        } kAlertCss[] = {
            {"note", 0x0969DA, 0x4493F8},
            {"tip", 0x1A7F37, 0x3FB950},
            {"important", 0x8250DF, 0xAB7DF8},
            {"warning", 0x9A6700, 0xD29922},
            {"caution", 0xCF222E, 0xF85149},
        };
        css += ".alert-title{font-weight:600;margin:0 0 4px 0;}";
        for (const auto& a : kAlertCss) {
            std::string hex = colorCss(hexColor(t.isDark ? a.dark : a.light));
            css += std::string("blockquote.alert-") + a.cls +
                   "{border-color:" + hex + ";}";
            css += std::string("blockquote.alert-") + a.cls +
                   " .alert-title{color:" + hex + ";}";
        }
    }
    css += "table{border-collapse:collapse;margin:1em 0;}";
    css += "th,td{border:1px solid " + colorCss(border) +
           ";padding:6px 13px;}";
    css += "th{background:" + colorCss(t.codeBackground) + ";}";
    css += "hr{border:none;border-top:1px solid " + colorCss(border) +
           ";margin:24px 0;}";
    css += "img{max-width:100%;}";
    css += "li.task{list-style:none;margin-left:-20px;}";
    css += ".diagram{text-align:center;margin:1em 0;overflow-x:auto;}";
    css += ".math-display{text-align:center;margin:1em 0;}";
    css += ".math-inline svg{vertical-align:middle;}";
    // ==highlight== uses the viewer's marker-pen yellow, not the accent
    css += "mark{background:" +
           colorCss(t.isDark ? D2D1::ColorF(0.98f, 0.80f, 0.25f, 0.28f)
                             : D2D1::ColorF(1.00f, 0.88f, 0.20f, 0.45f)) +
           ";color:inherit;padding:0 2px;}";
    return css;
}

std::string cssFontList(const std::wstring& family, bool mono) {
    std::string name = toUtf8(family);
    std::string list = "'" + name + "'";
    list += mono ? ",Consolas,monospace" : ",'Segoe UI',sans-serif";
    return list;
}

}  // namespace

bool exportHtmlFile(App& app, const std::wstring& path) {
    if (!app.root) return false;

    std::string bodyFont = cssFontList(app.theme.fontFamily, false);
    std::string monoFont = cssFontList(app.theme.codeFontFamily, true);

    ExportCtx ctx{app, {}, {}, bodyFont, monoFont, colorCss(app.theme.text)};
    if (!app.currentFile.empty()) {
        std::wstring wide = toWide(app.currentFile);
        size_t slash = wide.find_last_of(L"/\\");
        if (slash != std::wstring::npos) ctx.baseDir = wide.substr(0, slash);
    }

    std::string title = "Tinta export";
    if (!app.currentFile.empty()) {
        size_t slash = app.currentFile.find_last_of("/\\");
        title = slash == std::string::npos ? app.currentFile
                                           : app.currentFile.substr(slash + 1);
    }

    ctx.out += "<!doctype html>\n<html>\n<head>\n<meta charset=\"utf-8\"/>\n";
    ctx.out += "<meta name=\"viewport\" content=\"width=device-width, "
               "initial-scale=1\"/>\n";
    ctx.out += "<title>" + htmlEscape(title) + "</title>\n";
    ctx.out += "<style>" + themeCss(app, bodyFont, monoFont) + "</style>\n";
    ctx.out += "</head>\n<body>\n";
    walk(ctx, app.root);
    ctx.out += "</body>\n</html>\n";

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(ctx.out.data(), ctx.out.size());
    return out.good();
}

void exportDocumentAs(App& app, HWND hwnd) {
    if (!app.root) return;

    wchar_t path[MAX_PATH] = L"";
    // Default name: the document's stem
    if (!app.currentFile.empty()) {
        std::wstring wide = toWide(app.currentFile);
        size_t slash = wide.find_last_of(L"/\\");
        std::wstring stem =
            slash == std::wstring::npos ? wide : wide.substr(slash + 1);
        size_t dot = stem.find_last_of(L'.');
        if (dot != std::wstring::npos) stem = stem.substr(0, dot);
        wcsncpy_s(path, stem.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        L"Web page (*.html)\0*.html\0Word document (*.docx)\0*.docx\0"
        L"PDF document (*.pdf)\0*.pdf\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;

    // The chosen filter decides the format; a typed extension wins
    std::wstring chosen = path;
    auto endsWith = [&](const wchar_t* suffix) {
        size_t n = wcslen(suffix);
        return chosen.size() >= n &&
               _wcsicmp(chosen.c_str() + chosen.size() - n, suffix) == 0;
    };
    enum class Format { Html, Docx, Pdf };
    Format format = ofn.nFilterIndex == 2   ? Format::Docx
                    : ofn.nFilterIndex == 3 ? Format::Pdf
                                            : Format::Html;
    if (endsWith(L".docx")) format = Format::Docx;
    else if (endsWith(L".pdf")) format = Format::Pdf;
    else if (endsWith(L".html") || endsWith(L".htm")) format = Format::Html;
    else {
        chosen += format == Format::Docx  ? L".docx"
                  : format == Format::Pdf ? L".pdf"
                                          : L".html";
    }

    bool ok = format == Format::Docx  ? exportDocxFile(app, chosen)
              : format == Format::Pdf ? exportPdfFile(app, chosen)
                                      : exportHtmlFile(app, chosen);

    app.copiedNotificationKey = ok ? "toast.exported" : "toast.save_failed";
    app.showCopiedNotification = true;
    app.copiedNotificationStart = std::chrono::steady_clock::now();
    startNotificationTimer(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}
