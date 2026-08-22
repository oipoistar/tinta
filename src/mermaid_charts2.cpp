// Native Mermaid radar charts and sankey flow diagrams.
//
// radar:  axes on spokes, curves as filled polygons, optional legend.
// sankey: CSV source,target,value rows; nodes ranked by longest path,
//         links drawn as smooth ribbons sampled into polygons.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

// --------------------------------------------------------------------------
// Radar charts
// --------------------------------------------------------------------------

namespace {

constexpr float kRadarRadius = 110.0f;
constexpr float kRadarPi = 3.14159265f;

struct RadarAxis {
    std::string label;
};

struct RadarCurve {
    std::string label;
    std::vector<float> values;
};

// id["Label"] | id -> display label; returns the consumed length
std::string radarEntryLabel(std::string_view entry) {
    entry = trimView(entry);
    size_t bracket = entry.find('[');
    if (bracket != std::string_view::npos && entry.back() == ']') {
        std::string_view inner =
            entry.substr(bracket + 1, entry.size() - bracket - 2);
        inner = trimView(inner);
        if (inner.size() >= 2 && inner.front() == '"' &&
            inner.back() == '"') {
            inner = inner.substr(1, inner.size() - 2);
        }
        return cleanLabel(inner);
    }
    return cleanLabel(entry);
}

}  // namespace

Built buildRadar(std::string_view source, const Measure& measure,
                 float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title;
    std::vector<RadarAxis> axes;
    std::vector<RadarCurve> curves;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    bool maxSet = false;
    bool showLegend = false;
    bool legendSet = false;
    int ticks = 4;
    bool polygonGrid = false;

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "radar-beta") &&
                !startsWithWord(line, "radar")) {
                result.error = "Expected radar header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "title", &rest)) {
            title = cleanLabel(rest);
        } else if (startsWithWord(line, "axis", &rest)) {
            // Comma-separated entries, brackets may hold quoted commas
            size_t position = 0;
            int depth = 0;
            size_t start = 0;
            std::string remainder(rest);
            remainder += ',';
            for (; position < remainder.size(); position++) {
                char c = remainder[position];
                if (c == '[') depth++;
                else if (c == ']') depth--;
                else if (c == ',' && depth == 0) {
                    std::string_view entry =
                        trimView(std::string_view(remainder)
                                     .substr(start, position - start));
                    if (!entry.empty()) {
                        axes.push_back({radarEntryLabel(entry)});
                    }
                    start = position + 1;
                }
            }
        } else if (startsWithWord(line, "curve", &rest)) {
            size_t brace = rest.find('{');
            if (brace == std::string_view::npos || rest.back() != '}') {
                result.error = "Expected curve values";
                return result;
            }
            RadarCurve curve;
            curve.label = radarEntryLabel(rest.substr(0, brace));
            std::string values(
                rest.substr(brace + 1, rest.size() - brace - 2));
            values += ',';
            size_t start = 0;
            for (size_t i = 0; i < values.size(); i++) {
                if (values[i] != ',') continue;
                std::string_view piece = trimView(
                    std::string_view(values).substr(start, i - start));
                start = i + 1;
                if (piece.empty()) continue;
                // "name: value" pairs reduce to the value
                size_t colon = piece.rfind(':');
                if (colon != std::string_view::npos) {
                    piece = trimView(piece.substr(colon + 1));
                }
                try {
                    curve.values.push_back(std::stof(std::string(piece)));
                } catch (...) {
                    result.error = "Bad curve value";
                    return result;
                }
            }
            curves.push_back(std::move(curve));
        } else if (startsWithWord(line, "max", &rest)) {
            try {
                maxValue = std::stof(std::string(rest));
                maxSet = true;
            } catch (...) {
                result.error = "Bad max";
                return result;
            }
        } else if (startsWithWord(line, "min", &rest)) {
            try {
                minValue = std::stof(std::string(rest));
            } catch (...) {
                result.error = "Bad min";
                return result;
            }
        } else if (startsWithWord(line, "ticks", &rest)) {
            try {
                ticks = std::max(1, std::stoi(std::string(rest)));
            } catch (...) {
                result.error = "Bad ticks";
                return result;
            }
        } else if (startsWithWord(line, "graticule", &rest)) {
            polygonGrid = trimView(rest) == "polygon";
        } else if (startsWithWord(line, "showLegend", &rest)) {
            showLegend = trimView(rest) != "false";
            legendSet = true;
        } else {
            result.error = "Unknown radar statement";
            return result;
        }
    }

    if (axes.size() < 3) {
        result.error = "Radar needs at least three axes";
        return result;
    }
    if (curves.empty()) {
        result.error = "Radar has no curves";
        return result;
    }
    for (const auto& curve : curves) {
        if (curve.values.size() != axes.size()) {
            result.error = "Curve values do not match the axes";
            return result;
        }
        for (float value : curve.values) {
            maxValue = maxSet ? maxValue : std::max(maxValue, value);
        }
    }
    if (maxValue <= minValue) maxValue = minValue + 1.0f;
    if (!legendSet) showLegend = curves.size() > 1;

    TextStyle titleStyle;
    titleStyle.bold = true;
    titleStyle.scale = 1.1f;
    TextStyle axisStyle;
    axisStyle.scale = 0.9f;
    TextStyle legendStyle;

    float radius = kRadarRadius * scale;
    float labelPad = 14.0f * scale;

    // Axis label metrics decide the outer margins
    std::vector<Size> axisSizes;
    float overhangX = 0.0f;
    float overhangY = 0.0f;
    for (size_t i = 0; i < axes.size(); i++) {
        Size size = measure(axes[i].label, axisStyle, 0.0f);
        axisSizes.push_back(size);
        overhangX = std::max(overhangX, size.w);
        overhangY = std::max(overhangY, size.h);
    }

    float y = 4.0f * scale;
    float marginX = radius + labelPad + overhangX + 6.0f * scale;
    float centerX = marginX;

    float diagramWidth = marginX * 2.0f;
    if (!title.empty()) {
        Size titleSize = measure(title, titleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = 0;
        text.y1 = y;
        text.x2 = diagramWidth;
        text.y2 = y + titleSize.h;
        result.prims.push_back(std::move(text));
        y += titleSize.h + 10.0f * scale;
    }
    float centerY = y + radius + labelPad + overhangY;

    auto axisAngle = [&](size_t index) {
        return -kRadarPi * 0.5f +
               index * (2.0f * kRadarPi / axes.size());
    };

    // Graticule rings
    for (int ring = 1; ring <= ticks; ring++) {
        float fraction = static_cast<float>(ring) / ticks;
        if (polygonGrid) {
            Prim grid;
            grid.type = PrimType::Polygon;
            for (size_t i = 0; i < axes.size(); i++) {
                float angle = axisAngle(i);
                grid.pts.push_back(
                    {centerX + radius * fraction * std::cos(angle),
                     centerY + radius * fraction * std::sin(angle)});
            }
            grid.stroke = Role::Muted;
            grid.strokeWidth = 1.0f * scale;
            result.prims.push_back(std::move(grid));
        } else {
            Prim grid;
            grid.type = PrimType::Ellipse;
            grid.x1 = centerX - radius * fraction;
            grid.y1 = centerY - radius * fraction;
            grid.x2 = centerX + radius * fraction;
            grid.y2 = centerY + radius * fraction;
            grid.stroke = Role::Muted;
            grid.strokeWidth = 1.0f * scale;
            result.prims.push_back(std::move(grid));
        }
    }

    // Spokes and labels
    for (size_t i = 0; i < axes.size(); i++) {
        float angle = axisAngle(i);
        float dx = std::cos(angle);
        float dy = std::sin(angle);
        Prim spoke;
        spoke.type = PrimType::Line;
        spoke.x1 = centerX;
        spoke.y1 = centerY;
        spoke.x2 = centerX + radius * dx;
        spoke.y2 = centerY + radius * dy;
        spoke.stroke = Role::Muted;
        spoke.strokeWidth = 1.0f * scale;
        result.prims.push_back(std::move(spoke));

        const Size& size = axisSizes[i];
        float labelX = centerX + (radius + labelPad) * dx +
                       dx * size.w * 0.5f;
        float labelY = centerY + (radius + labelPad) * dy +
                       dy * size.h * 0.5f;
        Prim label;
        label.type = PrimType::Text;
        label.text = axes[i].label;
        label.style = axisStyle;
        label.fill = Role::Text;
        label.x1 = labelX - size.w * 0.5f - 2.0f * scale;
        label.y1 = labelY - size.h * 0.5f;
        label.x2 = labelX + size.w * 0.5f + 2.0f * scale;
        label.y2 = labelY + size.h * 0.5f;
        result.prims.push_back(std::move(label));
    }

    // Curves
    for (size_t c = 0; c < curves.size(); c++) {
        Prim shape;
        shape.type = PrimType::Polygon;
        for (size_t i = 0; i < axes.size(); i++) {
            float fraction = (curves[c].values[i] - minValue) /
                             (maxValue - minValue);
            fraction = std::max(0.0f, std::min(1.0f, fraction));
            float angle = axisAngle(i);
            shape.pts.push_back(
                {centerX + radius * fraction * std::cos(angle),
                 centerY + radius * fraction * std::sin(angle)});
        }
        shape.fill = Role::SeriesSoft;
        shape.stroke = Role::Series;
        shape.seriesIndex = static_cast<int>(c);
        shape.strokeWidth = 2.0f * scale;
        result.prims.push_back(std::move(shape));
    }

    float bottom = centerY + radius + labelPad + overhangY;

    // Legend on the right, like pie
    if (showLegend) {
        float swatch = 12.0f * scale;
        float legendWidth = 0.0f;
        std::vector<Size> legendSizes;
        for (const auto& curve : curves) {
            Size size = measure(curve.label, legendStyle, 0.0f);
            legendSizes.push_back(size);
            legendWidth =
                std::max(legendWidth, size.w + swatch + 10.0f * scale);
        }
        float rowHeight = swatch;
        for (const auto& size : legendSizes) {
            rowHeight = std::max(rowHeight, size.h);
        }
        float rowGap = 8.0f * scale;
        float legendHeight = curves.size() * rowHeight +
                             (curves.size() - 1) * rowGap;
        float legendX = diagramWidth + 8.0f * scale;
        float legendY = centerY - legendHeight * 0.5f;
        for (size_t c = 0; c < curves.size(); c++) {
            float rowTop = legendY + c * (rowHeight + rowGap);
            Prim swatchPrim;
            swatchPrim.type = PrimType::RoundRect;
            swatchPrim.radius = 3.0f * scale;
            swatchPrim.x1 = legendX;
            swatchPrim.y1 = rowTop + (rowHeight - swatch) * 0.5f;
            swatchPrim.x2 = legendX + swatch;
            swatchPrim.y2 = swatchPrim.y1 + swatch;
            swatchPrim.fill = Role::Series;
            swatchPrim.seriesIndex = static_cast<int>(c);
            result.prims.push_back(swatchPrim);

            Prim label;
            label.type = PrimType::Text;
            label.text = curves[c].label;
            label.style = legendStyle;
            label.fill = Role::Text;
            label.alignH = -1;
            label.x1 = legendX + swatch + 10.0f * scale;
            label.y1 = rowTop;
            label.x2 = legendX + legendWidth + 8.0f * scale;
            label.y2 = rowTop + rowHeight;
            result.prims.push_back(std::move(label));
        }
        diagramWidth = legendX + legendWidth + 8.0f * scale;
        bottom = std::max(bottom, legendY + legendHeight);
    }

    result.width = diagramWidth;
    result.height = bottom + 4.0f * scale;
    result.ok = true;
    normalizeLeft(result);
    return result;
}

// --------------------------------------------------------------------------
// Sankey diagrams
// --------------------------------------------------------------------------

namespace {

constexpr float kSankeyBarWidth = 14.0f;
constexpr float kSankeyColumnGap = 170.0f;
constexpr float kSankeyUsableHeight = 340.0f;
constexpr float kSankeyNodeGap = 12.0f;
constexpr int kSankeyRibbonSteps = 16;

struct SankeyNode {
    std::string name;
    float in = 0.0f;
    float out = 0.0f;
    int column = -1;
    float y = 0.0f;       // stacked top after layout
    float height = 0.0f;
    float inOffset = 0.0f;   // next free ribbon slot on the left
    float outOffset = 0.0f;  // next free ribbon slot on the right
    float throughput() const { return std::max(in, out); }
};

struct SankeyLink {
    size_t from = 0;
    size_t to = 0;
    float value = 0.0f;
};

// One CSV field, honoring "quoted, fields" with "" escapes
std::string_view sankeyField(std::string_view line, size_t& position,
                             std::string& storage, bool& ok) {
    ok = true;
    while (position < line.size() &&
           std::isspace(static_cast<unsigned char>(line[position]))) {
        position++;
    }
    if (position < line.size() && line[position] == '"') {
        storage.clear();
        position++;
        while (position < line.size()) {
            if (line[position] == '"') {
                if (position + 1 < line.size() &&
                    line[position + 1] == '"') {
                    storage += '"';
                    position += 2;
                    continue;
                }
                position++;
                break;
            }
            storage += line[position++];
        }
        while (position < line.size() && line[position] != ',') position++;
        if (position < line.size()) position++;
        return storage;
    }
    size_t comma = line.find(',', position);
    size_t end = comma == std::string_view::npos ? line.size() : comma;
    std::string_view field = trimView(line.substr(position, end - position));
    position = comma == std::string_view::npos ? line.size() : comma + 1;
    if (field.empty()) ok = false;
    return field;
}

}  // namespace

Built buildSankey(std::string_view source, const Measure& measure,
                  float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::vector<SankeyNode> nodes;
    std::vector<SankeyLink> links;
    auto nodeIndex = [&](std::string_view name) {
        for (size_t i = 0; i < nodes.size(); i++) {
            if (nodes[i].name == name) return i;
        }
        SankeyNode node;
        node.name = std::string(name);
        nodes.push_back(std::move(node));
        return nodes.size() - 1;
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "sankey-beta") &&
                !startsWithWord(line, "sankey")) {
                result.error = "Expected sankey header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        size_t position = 0;
        std::string sourceStorage, targetStorage;
        bool ok = true;
        std::string_view from =
            sankeyField(line, position, sourceStorage, ok);
        if (!ok) {
            result.error = "Bad sankey row";
            return result;
        }
        std::string_view to = sankeyField(line, position, targetStorage, ok);
        if (!ok) {
            result.error = "Bad sankey row";
            return result;
        }
        std::string valueStorage;
        std::string_view valueField =
            sankeyField(line, position, valueStorage, ok);
        SankeyLink link;
        try {
            link.value = std::stof(std::string(valueField));
        } catch (...) {
            result.error = "Bad sankey value";
            return result;
        }
        if (link.value <= 0.0f) {
            result.error = "Sankey values must be positive";
            return result;
        }
        link.from = nodeIndex(from);
        link.to = nodeIndex(to);
        nodes[link.from].out += link.value;
        nodes[link.to].in += link.value;
        links.push_back(link);
    }
    if (links.empty()) {
        result.error = "No sankey links";
        return result;
    }

    // Rank by longest path from the sources; a cycle never settles
    for (size_t pass = 0; pass <= nodes.size(); pass++) {
        bool changed = false;
        for (const auto& link : links) {
            int required =
                nodes[link.from].column < 0 ? 1
                                            : nodes[link.from].column + 1;
            if (nodes[link.from].column < 0) {
                nodes[link.from].column = 0;
                changed = true;
            }
            if (nodes[link.to].column < required) {
                nodes[link.to].column = required;
                changed = true;
            }
        }
        if (!changed) break;
        if (pass == nodes.size()) {
            result.error = "Sankey links form a cycle";
            return result;
        }
    }
    int columnCount = 0;
    for (const auto& node : nodes) {
        columnCount = std::max(columnCount, node.column + 1);
    }

    // Vertical packing: fit the busiest column into the usable height
    std::vector<float> columnSum(columnCount, 0.0f);
    std::vector<int> columnNodes(columnCount, 0);
    for (const auto& node : nodes) {
        columnSum[node.column] += node.throughput();
        columnNodes[node.column]++;
    }
    float pixelPerUnit = 1.0f;
    for (int c = 0; c < columnCount; c++) {
        float gaps = (columnNodes[c] - 1) * kSankeyNodeGap * scale;
        float available = kSankeyUsableHeight * scale - gaps;
        if (columnSum[c] > 0.0f) {
            pixelPerUnit =
                c == 0 ? available / columnSum[c]
                       : std::min(pixelPerUnit, available / columnSum[c]);
        }
    }

    std::vector<float> columnCursor(columnCount, 4.0f * scale);
    float barWidth = kSankeyBarWidth * scale;
    float columnStep = (kSankeyColumnGap + kSankeyBarWidth) * scale;
    for (auto& node : nodes) {
        node.height = std::max(2.0f * scale,
                               node.throughput() * pixelPerUnit);
        node.y = columnCursor[node.column];
        columnCursor[node.column] +=
            node.height + kSankeyNodeGap * scale;
    }

    TextStyle labelStyle;
    labelStyle.scale = 0.85f;

    // Ribbons first so the node bars sit on top of them
    for (const auto& link : links) {
        auto& from = nodes[link.from];
        auto& to = nodes[link.to];
        float thickness = link.value * pixelPerUnit;
        float x0 = from.column * columnStep + barWidth;
        float x1 = to.column * columnStep;
        float y0 = from.y + from.outOffset;
        float y1 = to.y + to.inOffset;
        from.outOffset += thickness;
        to.inOffset += thickness;

        Prim ribbon;
        ribbon.type = PrimType::Polygon;
        ribbon.fill = Role::SeriesSoft;
        ribbon.seriesIndex = static_cast<int>(link.from);
        for (int step = 0; step <= kSankeyRibbonSteps; step++) {
            float t = static_cast<float>(step) / kSankeyRibbonSteps;
            float ease = t * t * (3.0f - 2.0f * t);
            ribbon.pts.push_back(
                {x0 + (x1 - x0) * t, y0 + (y1 - y0) * ease});
        }
        for (int step = kSankeyRibbonSteps; step >= 0; step--) {
            float t = static_cast<float>(step) / kSankeyRibbonSteps;
            float ease = t * t * (3.0f - 2.0f * t);
            ribbon.pts.push_back({x0 + (x1 - x0) * t,
                                  y0 + (y1 - y0) * ease + thickness});
        }
        result.prims.push_back(std::move(ribbon));
    }

    float maxRight = 0.0f;
    float maxBottom = 0.0f;
    for (size_t i = 0; i < nodes.size(); i++) {
        const auto& node = nodes[i];
        float x = node.column * columnStep;
        Prim bar;
        bar.type = PrimType::Rect;
        bar.x1 = x;
        bar.y1 = node.y;
        bar.x2 = x + barWidth;
        bar.y2 = node.y + node.height;
        bar.fill = Role::Series;
        bar.seriesIndex = static_cast<int>(i);
        result.prims.push_back(bar);

        Size size = measure(node.name, labelStyle, 0.0f);
        bool lastColumn = node.column == columnCount - 1;
        Prim label;
        label.type = PrimType::Text;
        label.text = node.name;
        label.style = labelStyle;
        label.fill = Role::Text;
        if (lastColumn) {
            label.alignH = 1;
            label.x1 = x - size.w - 8.0f * scale;
            label.x2 = x - 4.0f * scale;
        } else {
            label.alignH = -1;
            label.x1 = x + barWidth + 4.0f * scale;
            label.x2 = label.x1 + size.w + 4.0f * scale;
        }
        float centerY = node.y + node.height * 0.5f;
        label.y1 = centerY - size.h * 0.5f;
        label.y2 = centerY + size.h * 0.5f;
        result.prims.push_back(std::move(label));

        maxRight = std::max(maxRight, x + barWidth);
        maxBottom = std::max(maxBottom, node.y + node.height);
    }

    result.width = maxRight + 4.0f * scale;
    result.height = maxBottom + 8.0f * scale;
    result.ok = true;
    normalizeLeft(result);
    return result;
}

}  // namespace detail
}  // namespace mermaidext
