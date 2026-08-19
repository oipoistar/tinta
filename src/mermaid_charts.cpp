// Native Mermaid quadrant charts and xy charts (xychart-beta).

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

namespace {

std::string unquote(std::string_view text) {
    text = trimView(text);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
    }
    return std::string(text);
}

bool parseNumber(std::string_view text, float& out) {
    try {
        out = std::stof(std::string(trimView(text)));
        return true;
    } catch (...) {
        return false;
    }
}

// [a, b, c] -> items (quotes optional)
bool parseBracketList(std::string_view text,
                      std::vector<std::string>& items) {
    text = trimView(text);
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') {
        return false;
    }
    text = text.substr(1, text.size() - 2);
    size_t position = 0;
    while (position <= text.size()) {
        size_t comma = text.find(',', position);
        if (comma == std::string_view::npos) comma = text.size();
        items.push_back(unquote(text.substr(position, comma - position)));
        if (comma == text.size()) break;
        position = comma + 1;
    }
    return true;
}

}  // namespace

// --------------------------------------------------------------------------
// Quadrant charts
// --------------------------------------------------------------------------

Built buildQuadrant(std::string_view source, const Measure& measure,
                    float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title;
    std::string xLow, xHigh, yLow, yHigh;
    std::string quadrants[4];
    struct QuadrantPoint {
        std::string name;
        float x = 0.0f, y = 0.0f;
    };
    std::vector<QuadrantPoint> points;

    auto parseAxis = [](std::string_view rest, std::string& low,
                        std::string& high) {
        size_t arrow = rest.find("-->");
        if (arrow == std::string_view::npos) {
            low = unquote(rest);
            high.clear();
        } else {
            low = unquote(trimView(rest.substr(0, arrow)));
            high = unquote(trimView(rest.substr(arrow + 3)));
        }
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "quadrantChart")) {
                result.error = "Expected quadrantChart header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "title", &rest)) {
            title = cleanLabel(rest);
            continue;
        }
        if (startsWithWord(line, "x-axis", &rest)) {
            parseAxis(rest, xLow, xHigh);
            continue;
        }
        if (startsWithWord(line, "y-axis", &rest)) {
            parseAxis(rest, yLow, yHigh);
            continue;
        }
        bool isQuadrant = false;
        for (int i = 0; i < 4; i++) {
            char keyword[12];
            snprintf(keyword, sizeof(keyword), "quadrant-%d", i + 1);
            if (startsWithWord(line, keyword, &rest)) {
                quadrants[i] = cleanLabel(rest);
                isQuadrant = true;
                break;
            }
        }
        if (isQuadrant) continue;
        // Point: Name: [x, y]
        size_t colon = line.find(':');
        if (colon != std::string_view::npos) {
            std::string_view coords = trimView(line.substr(colon + 1));
            std::vector<std::string> pair;
            if (parseBracketList(coords, pair) && pair.size() == 2) {
                QuadrantPoint point;
                point.name = cleanLabel(trimView(line.substr(0, colon)));
                if (parseNumber(pair[0], point.x) &&
                    parseNumber(pair[1], point.y)) {
                    points.push_back(std::move(point));
                    continue;
                }
            }
        }
        result.error = "Unsupported quadrant statement";
        return result;
    }

    TextStyle titleStyle;
    titleStyle.bold = true;
    titleStyle.scale = 1.1f;
    TextStyle quadrantStyle;
    quadrantStyle.bold = true;
    quadrantStyle.scale = 0.95f;
    TextStyle axisStyle;
    axisStyle.scale = 0.85f;
    TextStyle pointStyle;
    pointStyle.scale = 0.8f;

    float plotSize = 380.0f * scale;
    float leftGutter = 8.0f * scale;
    float axisBand = 22.0f * scale;

    float y = 4.0f * scale;
    float plotLeft = leftGutter + axisBand;
    if (!title.empty()) {
        Size titleSize = measure(title, titleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = plotLeft;
        text.y1 = y;
        text.x2 = plotLeft + plotSize;
        text.y2 = y + titleSize.h;
        result.prims.push_back(std::move(text));
        y += titleSize.h + 10.0f * scale;
    }
    float plotTop = y;

    // Quadrant fills: 1 = top right, 2 = top left, 3 = bottom left,
    // 4 = bottom right
    struct QuadrantRect {
        float x, ycoord;
    };
    QuadrantRect origins[4] = {
        {plotLeft + plotSize * 0.5f, plotTop},
        {plotLeft, plotTop},
        {plotLeft, plotTop + plotSize * 0.5f},
        {plotLeft + plotSize * 0.5f, plotTop + plotSize * 0.5f},
    };
    for (int i = 0; i < 4; i++) {
        Prim fill;
        fill.type = PrimType::Rect;
        fill.x1 = origins[i].x;
        fill.y1 = origins[i].ycoord;
        fill.x2 = origins[i].x + plotSize * 0.5f;
        fill.y2 = origins[i].ycoord + plotSize * 0.5f;
        fill.fill = Role::SeriesSoft;
        fill.seriesIndex = i;
        result.prims.push_back(fill);
        if (!quadrants[i].empty()) {
            Prim label;
            label.type = PrimType::Text;
            label.text = quadrants[i];
            label.style = quadrantStyle;
            label.fill = Role::Muted;
            label.x1 = origins[i].x + 6.0f * scale;
            label.y1 = origins[i].ycoord + 6.0f * scale;
            label.x2 = origins[i].x + plotSize * 0.5f - 6.0f * scale;
            label.y2 = origins[i].ycoord + plotSize * 0.5f - 6.0f * scale;
            result.prims.push_back(std::move(label));
        }
    }
    // Border + midlines
    Prim border;
    border.type = PrimType::Rect;
    border.x1 = plotLeft;
    border.y1 = plotTop;
    border.x2 = plotLeft + plotSize;
    border.y2 = plotTop + plotSize;
    border.stroke = Role::Muted;
    border.strokeWidth = 1.2f * scale;
    result.prims.push_back(border);
    for (int vertical = 0; vertical < 2; vertical++) {
        Prim mid;
        mid.type = PrimType::Line;
        if (vertical) {
            mid.x1 = plotLeft + plotSize * 0.5f;
            mid.y1 = plotTop;
            mid.x2 = plotLeft + plotSize * 0.5f;
            mid.y2 = plotTop + plotSize;
        } else {
            mid.x1 = plotLeft;
            mid.y1 = plotTop + plotSize * 0.5f;
            mid.x2 = plotLeft + plotSize;
            mid.y2 = plotTop + plotSize * 0.5f;
        }
        mid.stroke = Role::Muted;
        mid.strokeWidth = 0.8f * scale;
        result.prims.push_back(mid);
    }

    // Points
    for (const auto& point : points) {
        float px = plotLeft + point.x * plotSize;
        float py = plotTop + (1.0f - point.y) * plotSize;
        float radius = 5.0f * scale;
        Prim dot;
        dot.type = PrimType::Ellipse;
        dot.x1 = px - radius;
        dot.y1 = py - radius;
        dot.x2 = px + radius;
        dot.y2 = py + radius;
        dot.fill = Role::Accent;
        result.prims.push_back(dot);
        Size size = measure(point.name, pointStyle, 0.0f);
        Prim label;
        label.type = PrimType::Text;
        label.text = point.name;
        label.style = pointStyle;
        label.fill = Role::Text;
        label.x1 = px - size.w * 0.5f - 2.0f;
        label.y1 = py - radius - size.h - 2.0f * scale;
        label.x2 = px + size.w * 0.5f + 2.0f;
        label.y2 = py - radius - 2.0f * scale;
        result.prims.push_back(std::move(label));
    }

    // Axis labels
    float plotBottom = plotTop + plotSize;
    auto axisText = [&](const std::string& value, float x1, float x2,
                        float top, int alignH) {
        if (value.empty()) return;
        Size size = measure(value, axisStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = value;
        text.style = axisStyle;
        text.fill = Role::Muted;
        text.alignH = alignH;
        text.x1 = x1;
        text.y1 = top;
        text.x2 = x2;
        text.y2 = top + size.h;
        result.prims.push_back(std::move(text));
    };
    axisText(xLow, plotLeft, plotLeft + plotSize * 0.5f,
             plotBottom + 6.0f * scale, -1);
    axisText(xHigh, plotLeft + plotSize * 0.5f, plotLeft + plotSize,
             plotBottom + 6.0f * scale, 1);
    // Y labels sit sideways in mermaid; horizontal text goes beside the axis
    if (!yHigh.empty() || !yLow.empty()) {
        Size lowSize = measure(yLow, axisStyle, 0.0f);
        Size highSize = measure(yHigh, axisStyle, 0.0f);
        float axisWidth = std::max(lowSize.w, highSize.w);
        // shift the whole plot right by drawing labels at negative x and
        // normalizing afterwards
        if (!yHigh.empty()) {
            Prim text;
            text.type = PrimType::Text;
            text.text = yHigh;
            text.style = axisStyle;
            text.fill = Role::Muted;
            text.alignH = 1;
            text.x1 = plotLeft - axisWidth - 10.0f * scale;
            text.y1 = plotTop;
            text.x2 = plotLeft - 10.0f * scale;
            text.y2 = plotTop + highSize.h;
            result.prims.push_back(std::move(text));
        }
        if (!yLow.empty()) {
            Prim text;
            text.type = PrimType::Text;
            text.text = yLow;
            text.style = axisStyle;
            text.fill = Role::Muted;
            text.alignH = 1;
            text.x1 = plotLeft - axisWidth - 10.0f * scale;
            text.y1 = plotBottom - lowSize.h;
            text.x2 = plotLeft - 10.0f * scale;
            text.y2 = plotBottom;
            result.prims.push_back(std::move(text));
        }
    }

    result.width = plotLeft + plotSize + 8.0f * scale;
    result.height = plotBottom + 26.0f * scale;
    normalizeLeft(result);
    result.ok = true;
    return result;
}

// --------------------------------------------------------------------------
// XY charts (xychart-beta)
// --------------------------------------------------------------------------

Built buildXyChart(std::string_view source, const Measure& measure,
                   float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title, xTitle, yTitle;
    std::vector<std::string> categories;
    float yMin = 0.0f, yMax = 0.0f;
    bool yRangeSet = false;
    struct XySeries {
        bool isLine = false;
        std::vector<float> values;
    };
    std::vector<XySeries> series;

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            std::string_view rest;
            if (!startsWithWord(line, "xychart-beta", &rest) &&
                !startsWithWord(line, "xychart", &rest)) {
                result.error = "Expected xychart header";
                return result;
            }
            if (!trimView(rest).empty()) {
                result.error = "Horizontal xy charts are not supported";
                return result;
            }
            sawHeader = true;
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "title", &rest)) {
            title = unquote(rest);
            continue;
        }
        if (startsWithWord(line, "x-axis", &rest)) {
            std::string_view remainder = trimView(rest);
            if (!remainder.empty() && remainder.front() == '"') {
                size_t close = remainder.find('"', 1);
                if (close != std::string_view::npos) {
                    xTitle = std::string(remainder.substr(1, close - 1));
                    remainder = trimView(remainder.substr(close + 1));
                }
            }
            if (!remainder.empty()) {
                if (!parseBracketList(remainder, categories)) {
                    // numeric range: min --> max; render as two categories
                    size_t arrow = remainder.find("-->");
                    if (arrow == std::string_view::npos) {
                        result.error = "Bad x-axis";
                        return result;
                    }
                    categories.push_back(
                        unquote(trimView(remainder.substr(0, arrow))));
                    categories.push_back(
                        unquote(trimView(remainder.substr(arrow + 3))));
                }
            }
            continue;
        }
        if (startsWithWord(line, "y-axis", &rest)) {
            std::string_view remainder = trimView(rest);
            if (!remainder.empty() && remainder.front() == '"') {
                size_t close = remainder.find('"', 1);
                if (close != std::string_view::npos) {
                    yTitle = std::string(remainder.substr(1, close - 1));
                    remainder = trimView(remainder.substr(close + 1));
                }
            }
            size_t arrow = remainder.find("-->");
            if (arrow != std::string_view::npos) {
                if (parseNumber(remainder.substr(0, arrow), yMin) &&
                    parseNumber(remainder.substr(arrow + 3), yMax)) {
                    yRangeSet = true;
                }
            }
            continue;
        }
        bool isBar = startsWithWord(line, "bar", &rest);
        bool isLine = !isBar && startsWithWord(line, "line", &rest);
        if (isBar || isLine) {
            // optional series title before the list
            std::string_view remainder = trimView(rest);
            if (!remainder.empty() && remainder.front() == '"') {
                size_t close = remainder.find('"', 1);
                if (close != std::string_view::npos) {
                    remainder = trimView(remainder.substr(close + 1));
                }
            }
            std::vector<std::string> raw;
            if (!parseBracketList(remainder, raw)) {
                result.error = "Bad series data";
                return result;
            }
            XySeries one;
            one.isLine = isLine;
            for (const auto& value : raw) {
                float number = 0.0f;
                if (!parseNumber(value, number)) {
                    result.error = "Bad series value";
                    return result;
                }
                one.values.push_back(number);
            }
            series.push_back(std::move(one));
            continue;
        }
        result.error = "Unsupported xychart statement";
        return result;
    }

    if (series.empty()) {
        result.error = "No series";
        return result;
    }
    size_t categoryCount = 0;
    for (const auto& one : series) {
        categoryCount = std::max(categoryCount, one.values.size());
    }
    while (categories.size() < categoryCount) {
        categories.push_back(std::to_string(categories.size() + 1));
    }
    if (!yRangeSet) {
        yMin = series[0].values.empty() ? 0.0f : series[0].values[0];
        yMax = yMin;
        for (const auto& one : series) {
            for (float value : one.values) {
                yMin = std::min(yMin, value);
                yMax = std::max(yMax, value);
            }
        }
        if (yMin > 0.0f) yMin = 0.0f;
        if (yMax == yMin) yMax = yMin + 1.0f;
    }

    TextStyle titleStyle;
    titleStyle.bold = true;
    titleStyle.scale = 1.1f;
    TextStyle axisStyle;
    axisStyle.scale = 0.75f;
    TextStyle axisTitleStyle;
    axisTitleStyle.scale = 0.85f;

    // Nice tick step for ~5 ticks
    float range = yMax - yMin;
    float roughStep = range / 5.0f;
    float magnitude = std::pow(10.0f, std::floor(std::log10(roughStep)));
    float normalized = roughStep / magnitude;
    float step = magnitude * (normalized <= 1.0f   ? 1.0f
                              : normalized <= 2.0f ? 2.0f
                              : normalized <= 5.0f ? 5.0f
                                                   : 10.0f);
    float tickStart = std::ceil(yMin / step) * step;

    // Measure y tick labels for the gutter
    float yGutter = 0.0f;
    auto formatValue = [](float value) {
        char buffer[32];
        if (std::fabs(value - std::round(value)) < 0.001f &&
            std::fabs(value) < 1e7f) {
            snprintf(buffer, sizeof(buffer), "%d",
                     static_cast<int>(std::lround(value)));
        } else {
            snprintf(buffer, sizeof(buffer), "%.4g", value);
        }
        return std::string(buffer);
    };
    for (float tick = tickStart; tick <= yMax + step * 0.01f; tick += step) {
        yGutter = std::max(yGutter,
                           measure(formatValue(tick), axisStyle, 0.0f).w);
    }
    yGutter += 12.0f * scale;

    float plotWidth =
        std::max(340.0f, std::min(640.0f, categoryCount * 90.0f)) * scale;
    float plotHeight = 280.0f * scale;

    float y = 4.0f * scale;
    float plotLeft = yGutter;
    if (!title.empty()) {
        Size titleSize = measure(title, titleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = plotLeft;
        text.y1 = y;
        text.x2 = plotLeft + plotWidth;
        text.y2 = y + titleSize.h;
        result.prims.push_back(std::move(text));
        y += titleSize.h + 8.0f * scale;
    }
    if (!yTitle.empty()) {
        Size size = measure(yTitle, axisTitleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = yTitle;
        text.style = axisTitleStyle;
        text.fill = Role::Muted;
        text.alignH = -1;
        text.x1 = 2.0f;
        text.y1 = y;
        text.x2 = 2.0f + size.w + 4.0f;
        text.y2 = y + size.h;
        result.prims.push_back(std::move(text));
        y += size.h + 6.0f * scale;
    }
    float plotTop = y;
    float plotBottom = plotTop + plotHeight;

    auto valueY = [&](float value) {
        return plotBottom - (value - yMin) / (yMax - yMin) * plotHeight;
    };

    // Gridlines + tick labels
    for (float tick = tickStart; tick <= yMax + step * 0.01f; tick += step) {
        float gy = valueY(tick);
        Prim grid;
        grid.type = PrimType::Line;
        grid.x1 = plotLeft;
        grid.y1 = gy;
        grid.x2 = plotLeft + plotWidth;
        grid.y2 = gy;
        grid.stroke = Role::Muted;
        grid.strokeWidth = 0.5f * scale;
        result.prims.push_back(grid);
        std::string label = formatValue(tick);
        Size size = measure(label, axisStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = label;
        text.style = axisStyle;
        text.fill = Role::Muted;
        text.alignH = 1;
        text.x1 = plotLeft - size.w - 8.0f * scale;
        text.y1 = gy - size.h * 0.5f;
        text.x2 = plotLeft - 6.0f * scale;
        text.y2 = gy + size.h * 0.5f;
        result.prims.push_back(std::move(text));
    }
    // Axis lines
    for (int vertical = 0; vertical < 2; vertical++) {
        Prim axis;
        axis.type = PrimType::Line;
        axis.x1 = plotLeft;
        axis.y1 = vertical ? plotTop : plotBottom;
        axis.x2 = vertical ? plotLeft : plotLeft + plotWidth;
        axis.y2 = plotBottom;
        axis.stroke = Role::Muted;
        axis.strokeWidth = 1.2f * scale;
        result.prims.push_back(axis);
    }

    float categoryWidth = plotWidth / static_cast<float>(categoryCount);
    size_t barSeriesCount = 0;
    for (const auto& one : series) {
        if (!one.isLine) barSeriesCount++;
    }

    // Bars first, then lines on top
    size_t barIndex = 0;
    for (size_t s = 0; s < series.size(); s++) {
        if (series[s].isLine) continue;
        float groupWidth = categoryWidth * 0.62f;
        float barWidth = groupWidth / static_cast<float>(barSeriesCount);
        for (size_t i = 0; i < series[s].values.size(); i++) {
            float centerX = plotLeft + (i + 0.5f) * categoryWidth;
            float x1 = centerX - groupWidth * 0.5f + barIndex * barWidth;
            Prim bar;
            bar.type = PrimType::Rect;
            bar.x1 = x1 + 1.0f * scale;
            bar.y1 = valueY(series[s].values[i]);
            bar.x2 = x1 + barWidth - 1.0f * scale;
            bar.y2 = valueY(std::max(yMin, 0.0f));
            if (bar.y2 < bar.y1) std::swap(bar.y1, bar.y2);
            bar.fill = Role::Series;
            bar.seriesIndex = static_cast<int>(s);
            result.prims.push_back(bar);
        }
        barIndex++;
    }
    for (size_t s = 0; s < series.size(); s++) {
        if (!series[s].isLine) continue;
        for (size_t i = 0; i + 1 < series[s].values.size(); i++) {
            Prim segment;
            segment.type = PrimType::Line;
            segment.x1 = plotLeft + (i + 0.5f) * categoryWidth;
            segment.y1 = valueY(series[s].values[i]);
            segment.x2 = plotLeft + (i + 1.5f) * categoryWidth;
            segment.y2 = valueY(series[s].values[i + 1]);
            segment.stroke = Role::Series;
            segment.seriesIndex = static_cast<int>(s);
            segment.strokeWidth = 2.2f * scale;
            result.prims.push_back(segment);
        }
        for (size_t i = 0; i < series[s].values.size(); i++) {
            float radius = 3.5f * scale;
            float cx = plotLeft + (i + 0.5f) * categoryWidth;
            float cy = valueY(series[s].values[i]);
            Prim dot;
            dot.type = PrimType::Ellipse;
            dot.x1 = cx - radius;
            dot.y1 = cy - radius;
            dot.x2 = cx + radius;
            dot.y2 = cy + radius;
            dot.fill = Role::Series;
            dot.seriesIndex = static_cast<int>(s);
            result.prims.push_back(dot);
        }
    }

    // Category labels
    float labelBottom = plotBottom;
    for (size_t i = 0; i < categoryCount; i++) {
        Size size = measure(categories[i], axisStyle,
                            categoryWidth - 6.0f * scale);
        Prim text;
        text.type = PrimType::Text;
        text.text = categories[i];
        text.style = axisStyle;
        text.fill = Role::Muted;
        text.x1 = plotLeft + i * categoryWidth + 3.0f * scale;
        text.y1 = plotBottom + 5.0f * scale;
        text.x2 = plotLeft + (i + 1) * categoryWidth - 3.0f * scale;
        text.y2 = plotBottom + 5.0f * scale + size.h;
        labelBottom = std::max(labelBottom, text.y2);
        result.prims.push_back(std::move(text));
    }
    if (!xTitle.empty()) {
        Size size = measure(xTitle, axisTitleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = xTitle;
        text.style = axisTitleStyle;
        text.fill = Role::Muted;
        text.x1 = plotLeft;
        text.y1 = labelBottom + 4.0f * scale;
        text.x2 = plotLeft + plotWidth;
        text.y2 = labelBottom + 4.0f * scale + size.h;
        labelBottom = text.y2;
        result.prims.push_back(std::move(text));
    }

    result.width = plotLeft + plotWidth + 8.0f * scale;
    result.height = labelBottom + 8.0f * scale;
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
