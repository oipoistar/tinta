// Native Mermaid pie chart renderer.
//
// Grammar: `pie` header with optional `showData` and `title <text>` (on the
// header line or their own lines), then one `"Label" : value` entry per
// line. Slices run clockwise from 12 o'clock in source order, colored from
// the theme-derived categorical palette; the legend sits to the right.

#include "mermaid_ext.h"

#include <cmath>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

namespace {

constexpr float kRadius = 105.0f;
constexpr float kLegendGap = 30.0f;
constexpr float kLegendSwatch = 12.0f;
constexpr float kLegendRowGap = 8.0f;
constexpr float kPi = 3.14159265f;

struct PieEntry {
    std::string label;
    float value = 0.0f;
};

}  // namespace

Built buildPie(std::string_view source, const Measure& measure, float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    bool showData = false;
    std::string title;
    std::vector<PieEntry> entries;

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            std::string_view rest;
            if (!startsWithWord(line, "pie", &rest)) {
                result.error = "Expected pie header";
                return result;
            }
            sawHeader = true;
            if (startsWithWord(rest, "showData", &rest)) showData = true;
            std::string_view titleRest;
            if (startsWithWord(rest, "title", &titleRest)) {
                title = cleanLabel(titleRest);
            }
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "showData", &rest)) {
            showData = true;
            continue;
        }
        if (startsWithWord(line, "title", &rest)) {
            title = cleanLabel(rest);
            continue;
        }
        // "Label" : value
        if (line.front() != '"') {
            result.error = "Expected a quoted slice label";
            return result;
        }
        size_t closeQuote = line.find('"', 1);
        if (closeQuote == std::string_view::npos) {
            result.error = "Unterminated slice label";
            return result;
        }
        PieEntry entry;
        entry.label = cleanLabel(line.substr(1, closeQuote - 1));
        std::string_view remainder = trimView(line.substr(closeQuote + 1));
        if (remainder.empty() || remainder.front() != ':') {
            result.error = "Expected : after slice label";
            return result;
        }
        remainder = trimView(remainder.substr(1));
        try {
            entry.value = std::stof(std::string(remainder));
        } catch (...) {
            result.error = "Bad slice value";
            return result;
        }
        if (entry.value < 0.0f) {
            result.error = "Negative slice value";
            return result;
        }
        entries.push_back(std::move(entry));
    }

    float total = 0.0f;
    for (const auto& entry : entries) total += entry.value;
    if (entries.empty() || total <= 0.0f) {
        result.error = "No slice data";
        return result;
    }

    TextStyle titleStyle;
    titleStyle.bold = true;
    titleStyle.scale = 1.1f;
    TextStyle legendStyle;
    TextStyle sliceStyle;
    sliceStyle.scale = 0.9f;
    sliceStyle.bold = true;

    float radius = kRadius * scale;
    float y = 4.0f * scale;

    // Legend measurement
    float legendWidth = 0.0f;
    std::vector<std::string> legendLabels;
    std::vector<Size> legendSizes;
    for (const auto& entry : entries) {
        std::string label = entry.label;
        if (showData) {
            char buffer[48];
            float rounded = std::round(entry.value * 100.0f) / 100.0f;
            if (rounded == std::floor(rounded)) {
                snprintf(buffer, sizeof(buffer), " (%d)",
                         static_cast<int>(rounded));
            } else {
                snprintf(buffer, sizeof(buffer), " (%.2f)", rounded);
            }
            label += buffer;
        }
        Size size = measure(label, legendStyle, 0.0f);
        legendWidth = std::max(legendWidth,
                               size.w + (kLegendSwatch + 10.0f) * scale);
        legendLabels.push_back(std::move(label));
        legendSizes.push_back(size);
    }

    float diagramWidth =
        radius * 2.0f + kLegendGap * scale + legendWidth + 8.0f * scale;

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
        y += titleSize.h + 12.0f * scale;
    }

    float centerX = radius + 4.0f * scale;
    float centerY = y + radius;

    float angle = -kPi * 0.5f;  // 12 o'clock, clockwise
    for (size_t i = 0; i < entries.size(); i++) {
        float sweep = entries[i].value / total * kPi * 2.0f;
        Prim slice;
        slice.type = PrimType::Slice;
        slice.x1 = centerX;
        slice.y1 = centerY;
        slice.radius = radius;
        slice.a0 = angle;
        slice.a1 = angle + sweep;
        slice.fill = Role::Series;
        slice.seriesIndex = static_cast<int>(i);
        slice.stroke = Role::Background;
        slice.strokeWidth = 2.0f * scale;
        result.prims.push_back(slice);

        // Percentage label inside sufficiently large slices
        float fraction = entries[i].value / total;
        if (fraction >= 0.04f) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "%d%%",
                     static_cast<int>(std::round(fraction * 100.0f)));
            float middle = angle + sweep * 0.5f;
            float labelRadius = radius * 0.62f;
            float labelX = centerX + labelRadius * std::cos(middle);
            float labelY = centerY + labelRadius * std::sin(middle);
            Prim text;
            text.type = PrimType::Text;
            text.text = buffer;
            text.style = sliceStyle;
            text.fill = Role::Background;
            text.x1 = labelX - 24.0f * scale;
            text.y1 = labelY - 10.0f * scale;
            text.x2 = labelX + 24.0f * scale;
            text.y2 = labelY + 10.0f * scale;
            result.prims.push_back(std::move(text));
        }
        angle += sweep;
    }

    // Legend, vertically centered against the pie
    float legendRowHeight = 0.0f;
    for (const auto& size : legendSizes) {
        legendRowHeight = std::max(legendRowHeight, size.h);
    }
    legendRowHeight = std::max(legendRowHeight, kLegendSwatch * scale);
    float legendHeight =
        entries.size() * legendRowHeight +
        (entries.size() > 0 ? (entries.size() - 1) * kLegendRowGap * scale
                            : 0.0f);
    float legendX = centerX + radius + kLegendGap * scale;
    float legendY = centerY - legendHeight * 0.5f;
    for (size_t i = 0; i < entries.size(); i++) {
        float rowTop = legendY + i * (legendRowHeight + kLegendRowGap * scale);
        Prim swatch;
        swatch.type = PrimType::RoundRect;
        swatch.radius = 3.0f * scale;
        swatch.x1 = legendX;
        swatch.y1 = rowTop + (legendRowHeight - kLegendSwatch * scale) * 0.5f;
        swatch.x2 = legendX + kLegendSwatch * scale;
        swatch.y2 = swatch.y1 + kLegendSwatch * scale;
        swatch.fill = Role::Series;
        swatch.seriesIndex = static_cast<int>(i);
        result.prims.push_back(swatch);

        Prim text;
        text.type = PrimType::Text;
        text.text = legendLabels[i];
        text.style = legendStyle;
        text.fill = Role::Text;
        text.alignH = -1;
        text.x1 = legendX + (kLegendSwatch + 10.0f) * scale;
        text.y1 = rowTop;
        text.x2 = legendX + legendWidth + 8.0f * scale;
        text.y2 = rowTop + legendRowHeight;
        result.prims.push_back(std::move(text));
    }

    result.width = diagramWidth;
    result.height = std::max(centerY + radius, legendY + legendHeight) +
                    8.0f * scale;
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
