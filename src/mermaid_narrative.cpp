// Native Mermaid mindmaps, timelines, and user journeys.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

// --------------------------------------------------------------------------
// Mindmaps
// --------------------------------------------------------------------------

namespace {

enum class MindShape { Plain, Rounded, Square, Circle, Hexagon, Cloud, Bang };

struct MindNode {
    std::string text;
    MindShape shape = MindShape::Plain;
    int indent = 0;
    size_t parent = SIZE_MAX;
    std::vector<size_t> children;
    int branch = -1;  // top-level child index for coloring
    Size textSize;
    float width = 0.0f, height = 0.0f;  // node box
    float subtreeHeight = 0.0f;
    float x = 0.0f, y = 0.0f;           // box top-left after layout
};

// id((text)), id(text), id[text], id{{text}}, id)text(, id))text(( --
// the optional leading id is dropped, the delimited label is displayed
MindShape stripMindShape(std::string& text) {
    struct Delimiter {
        const char* open;
        const char* close;
        MindShape shape;
    };
    static const Delimiter kDelimiters[] = {
        {"((", "))", MindShape::Circle},
        {"))", "((", MindShape::Bang},
        {"{{", "}}", MindShape::Hexagon},
        {")", "(", MindShape::Cloud},
        {"(", ")", MindShape::Rounded},
        {"[", "]", MindShape::Square},
    };
    for (const auto& delimiter : kDelimiters) {
        size_t openLength = strlen(delimiter.open);
        size_t closeLength = strlen(delimiter.close);
        size_t open = text.find(delimiter.open);
        if (open == std::string::npos) continue;
        if (text.size() < closeLength ||
            text.compare(text.size() - closeLength, closeLength,
                         delimiter.close) != 0) {
            continue;
        }
        size_t innerStart = open + openLength;
        if (text.size() - closeLength <= innerStart) continue;
        text = text.substr(innerStart,
                           text.size() - closeLength - innerStart);
        return delimiter.shape;
    }
    return MindShape::Plain;
}

}  // namespace

Built buildMindmap(std::string_view source, const Measure& measure,
                   float scale) {
    Built result;

    // Indentation matters here: parse raw lines, not diagramLines
    std::vector<MindNode> nodes;
    std::vector<size_t> stack;  // node indices by ancestry
    bool sawHeader = false;
    size_t position = 0;
    while (position <= source.size()) {
        size_t newline = source.find('\n', position);
        if (newline == std::string_view::npos) newline = source.size();
        std::string_view raw = source.substr(position, newline - position);
        position = newline + 1;
        bool lastLine = newline == source.size();

        int indent = 0;
        size_t start = 0;
        while (start < raw.size() &&
               (raw[start] == ' ' || raw[start] == '\t')) {
            indent += raw[start] == '\t' ? 4 : 1;
            start++;
        }
        std::string_view line = trimView(raw.substr(start));
        if (line.empty() || line.substr(0, 2) == "%%") {
            if (lastLine) break;
            continue;
        }
        if (!sawHeader) {
            if (!startsWithWord(line, "mindmap")) {
                result.error = "Expected mindmap header";
                return result;
            }
            sawHeader = true;
            if (lastLine) break;
            continue;
        }
        if (line.substr(0, 6) == "::icon") {
            if (lastLine) break;
            continue;  // icon decoration, nothing to draw natively
        }
        MindNode node;
        node.text = cleanLabel(line);
        node.shape = stripMindShape(node.text);
        node.indent = indent;
        // Parent: nearest shallower indent on the stack
        while (!stack.empty() && nodes[stack.back()].indent >= indent) {
            stack.pop_back();
        }
        if (stack.empty()) {
            if (!nodes.empty()) {
                result.error = "Multiple mindmap roots";
                return result;
            }
        } else {
            node.parent = stack.back();
        }
        nodes.push_back(std::move(node));
        size_t index = nodes.size() - 1;
        if (nodes[index].parent != SIZE_MAX) {
            nodes[nodes[index].parent].children.push_back(index);
        }
        stack.push_back(index);
        if (lastLine) break;
    }

    if (nodes.empty()) {
        result.error = "Empty mindmap";
        return result;
    }

    // Branch colors: each child of the root starts a colored branch
    for (size_t i = 0; i < nodes[0].children.size(); i++) {
        nodes[nodes[0].children[i]].branch = static_cast<int>(i);
    }
    for (size_t i = 1; i < nodes.size(); i++) {
        if (nodes[i].branch < 0 && nodes[i].parent != SIZE_MAX) {
            nodes[i].branch = nodes[nodes[i].parent].branch;
        }
    }

    // --- measure ---
    TextStyle rootStyle;
    rootStyle.bold = true;
    TextStyle nodeStyle;
    nodeStyle.scale = 0.9f;
    for (size_t i = 0; i < nodes.size(); i++) {
        const TextStyle& style = i == 0 ? rootStyle : nodeStyle;
        nodes[i].textSize = measure(nodes[i].text, style, 190.0f * scale);
        float padX = nodes[i].shape == MindShape::Circle ? 16.0f : 12.0f;
        float padY = nodes[i].shape == MindShape::Circle ? 12.0f : 6.0f;
        nodes[i].width = nodes[i].textSize.w + padX * 2.0f * scale;
        nodes[i].height = nodes[i].textSize.h + padY * 2.0f * scale;
    }

    // Right-growing tree: subtree heights bottom-up (children listed after
    // parents, so a reverse pass suffices)
    float rowGap = 10.0f * scale;
    float levelGap = 46.0f * scale;
    for (size_t i = nodes.size(); i-- > 0;) {
        if (nodes[i].children.empty()) {
            nodes[i].subtreeHeight = nodes[i].height;
        } else {
            float total = 0.0f;
            for (size_t child : nodes[i].children) {
                total += nodes[child].subtreeHeight;
            }
            total += rowGap * (nodes[i].children.size() - 1);
            nodes[i].subtreeHeight = std::max(nodes[i].height, total);
        }
    }
    // Positions top-down
    struct PlaceItem {
        size_t node;
        float x, top;
    };
    std::vector<PlaceItem> queue{{0, 4.0f * scale, 4.0f * scale}};
    float maxRight = 0.0f, maxBottom = 0.0f;
    while (!queue.empty()) {
        PlaceItem item = queue.back();
        queue.pop_back();
        MindNode& node = nodes[item.node];
        node.x = item.x;
        node.y = item.top + (node.subtreeHeight - node.height) * 0.5f;
        maxRight = std::max(maxRight, node.x + node.width);
        maxBottom = std::max(maxBottom, item.top + node.subtreeHeight);
        float childX = item.x + node.width + levelGap;
        float childTop = item.top;
        if (!node.children.empty()) {
            float childrenHeight = 0.0f;
            for (size_t child : node.children) {
                childrenHeight += nodes[child].subtreeHeight;
            }
            childrenHeight += rowGap * (node.children.size() - 1);
            childTop += (node.subtreeHeight - childrenHeight) * 0.5f;
        }
        for (size_t child : node.children) {
            queue.push_back({child, childX, childTop});
            childTop += nodes[child].subtreeHeight + rowGap;
        }
    }

    // Edges (elbow from parent right edge to child left edge)
    for (size_t i = 1; i < nodes.size(); i++) {
        const MindNode& node = nodes[i];
        const MindNode& parent = nodes[node.parent];
        float x1 = parent.x + parent.width;
        float y1 = parent.y + parent.height * 0.5f;
        float x2 = node.x;
        float y2 = node.y + node.height * 0.5f;
        float bend = x1 + (x2 - x1) * 0.5f;
        int branch = std::max(0, node.branch);
        auto lineSegment = [&](float ax, float ay, float bx, float by) {
            Prim segment;
            segment.type = PrimType::Line;
            segment.x1 = ax;
            segment.y1 = ay;
            segment.x2 = bx;
            segment.y2 = by;
            segment.stroke = Role::Series;
            segment.seriesIndex = branch;
            segment.strokeWidth = 2.0f * scale;
            result.prims.push_back(segment);
        };
        lineSegment(x1, y1, bend, y1);
        if (std::abs(y2 - y1) > 0.5f) lineSegment(bend, y1, bend, y2);
        lineSegment(bend, y2, x2, y2);
    }

    // Nodes
    for (size_t i = 0; i < nodes.size(); i++) {
        const MindNode& node = nodes[i];
        int branch = std::max(0, node.branch);
        Prim box;
        box.x1 = node.x;
        box.y1 = node.y;
        box.x2 = node.x + node.width;
        box.y2 = node.y + node.height;
        bool drawBox = true;
        switch (node.shape) {
            case MindShape::Circle:
            case MindShape::Bang:
            case MindShape::Cloud:
                box.type = PrimType::Ellipse;
                break;
            case MindShape::Square:
                box.type = PrimType::Rect;
                break;
            case MindShape::Plain:
                if (i != 0) {
                    drawBox = false;  // plain text over the branch line
                    break;
                }
                box.type = PrimType::RoundRect;
                box.radius = 10.0f * scale;
                break;
            case MindShape::Rounded:
            case MindShape::Hexagon:
                box.type = PrimType::RoundRect;
                box.radius =
                    node.shape == MindShape::Rounded
                        ? (box.y2 - box.y1) * 0.5f
                        : 8.0f * scale;
                break;
        }
        if (drawBox) {
            if (i == 0) {
                box.fill = Role::Accent;
                box.stroke = Role::None;
            } else {
                box.fill = Role::SeriesSoft;
                box.seriesIndex = branch;
                box.stroke = Role::Series;
                box.strokeWidth = 1.3f * scale;
            }
            result.prims.push_back(box);
        }
        Prim text;
        text.type = PrimType::Text;
        text.text = node.text;
        text.style = i == 0 ? rootStyle : nodeStyle;
        text.fill = i == 0 ? Role::Background : Role::Text;
        text.x1 = node.x;
        text.y1 = node.y;
        text.x2 = node.x + node.width;
        text.y2 = node.y + node.height;
        result.prims.push_back(std::move(text));
    }

    result.width = maxRight + 8.0f * scale;
    result.height = maxBottom + 8.0f * scale;
    result.ok = true;
    return result;
}

// --------------------------------------------------------------------------
// Timelines
// --------------------------------------------------------------------------

namespace {

struct TimelinePeriod {
    std::string label;
    std::vector<std::string> events;
    size_t section = 0;
};

}  // namespace

Built buildTimeline(std::string_view source, const Measure& measure,
                    float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title;
    std::vector<std::string> sections;
    std::vector<TimelinePeriod> periods;

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "timeline")) {
                result.error = "Expected timeline header";
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
        if (startsWithWord(line, "section", &rest)) {
            sections.push_back(cleanLabel(rest));
            continue;
        }
        // period : event : event ...  (or a continuation ": event")
        if (line.front() == ':') {
            if (periods.empty()) {
                result.error = "Event before any period";
                return result;
            }
            periods.back().events.push_back(
                cleanLabel(trimView(line.substr(1))));
            continue;
        }
        size_t colon = line.find(':');
        TimelinePeriod period;
        period.section = sections.empty() ? 0 : sections.size() - 1;
        if (colon == std::string_view::npos) {
            period.label = cleanLabel(line);
        } else {
            period.label = cleanLabel(trimView(line.substr(0, colon)));
            std::string_view remainder = line.substr(colon + 1);
            size_t start = 0;
            while (start <= remainder.size()) {
                size_t next = remainder.find(':', start);
                if (next == std::string_view::npos) next = remainder.size();
                std::string event =
                    cleanLabel(trimView(remainder.substr(start, next - start)));
                if (!event.empty()) period.events.push_back(std::move(event));
                if (next == remainder.size()) break;
                start = next + 1;
            }
        }
        periods.push_back(std::move(period));
    }

    if (periods.empty()) {
        result.error = "No periods";
        return result;
    }
    if (sections.empty()) sections.push_back("");

    TextStyle titleStyle;
    titleStyle.bold = true;
    titleStyle.scale = 1.1f;
    TextStyle sectionStyle;
    sectionStyle.bold = true;
    sectionStyle.scale = 0.9f;
    TextStyle periodStyle;
    periodStyle.bold = true;
    TextStyle eventStyle;
    eventStyle.scale = 0.85f;

    // Column widths
    float maxColumn = 150.0f * scale;
    std::vector<float> columnWidths(periods.size());
    std::vector<Size> periodSizes(periods.size());
    std::vector<std::vector<Size>> eventSizes(periods.size());
    for (size_t i = 0; i < periods.size(); i++) {
        periodSizes[i] = measure(periods[i].label, periodStyle, maxColumn);
        float width = periodSizes[i].w + 20.0f * scale;
        for (const auto& event : periods[i].events) {
            Size size = measure(event, eventStyle, maxColumn);
            eventSizes[i].push_back(size);
            width = std::max(width, size.w + 16.0f * scale);
        }
        columnWidths[i] = std::max(70.0f * scale, width);
    }

    float columnGap = 10.0f * scale;
    float y = 4.0f * scale;
    float totalWidth = 0.0f;
    for (float width : columnWidths) totalWidth += width + columnGap;
    totalWidth -= columnGap;

    if (!title.empty()) {
        Size size = measure(title, titleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = 0;
        text.y1 = y;
        text.x2 = totalWidth;
        text.y2 = y + size.h;
        result.prims.push_back(std::move(text));
        y += size.h + 12.0f * scale;
    }

    // Section bands (only when sections are named)
    bool hasSections = sections.size() > 1 || !sections[0].empty();
    if (hasSections) {
        float bandHeight = 24.0f * scale;
        float x = 0.0f;
        size_t start = 0;
        while (start < periods.size()) {
            size_t section = periods[start].section;
            size_t end = start;
            float width = 0.0f;
            while (end < periods.size() && periods[end].section == section) {
                width += columnWidths[end] + columnGap;
                end++;
            }
            width -= columnGap;
            Prim band;
            band.type = PrimType::RoundRect;
            band.radius = 5.0f * scale;
            band.x1 = x;
            band.y1 = y;
            band.x2 = x + width;
            band.y2 = y + bandHeight;
            band.fill = Role::SeriesSoft;
            band.seriesIndex = static_cast<int>(section);
            result.prims.push_back(band);
            if (!sections[section].empty()) {
                Prim text;
                text.type = PrimType::Text;
                text.text = sections[section];
                text.style = sectionStyle;
                text.fill = Role::Text;
                text.x1 = band.x1;
                text.y1 = band.y1;
                text.x2 = band.x2;
                text.y2 = band.y2;
                result.prims.push_back(std::move(text));
            }
            x += width + columnGap;
            start = end;
        }
        y += bandHeight + 10.0f * scale;
    }

    // Period row height
    float periodHeight = 0.0f;
    for (const auto& size : periodSizes) {
        periodHeight = std::max(periodHeight, size.h + 12.0f * scale);
    }
    float periodTop = y;
    float periodBottom = y + periodHeight;

    // Timeline spine behind the period boxes
    Prim spine;
    spine.type = PrimType::Line;
    spine.x1 = 0;
    spine.y1 = (periodTop + periodBottom) * 0.5f;
    spine.x2 = totalWidth;
    spine.y2 = spine.y1;
    spine.stroke = Role::Muted;
    spine.strokeWidth = 2.0f * scale;
    result.prims.push_back(spine);

    float x = 0.0f;
    float maxBottom = periodBottom;
    for (size_t i = 0; i < periods.size(); i++) {
        size_t section = periods[i].section;
        Prim box;
        box.type = PrimType::RoundRect;
        box.radius = 6.0f * scale;
        box.x1 = x;
        box.y1 = periodTop;
        box.x2 = x + columnWidths[i];
        box.y2 = periodBottom;
        box.fill = Role::Series;
        box.seriesIndex = static_cast<int>(section);
        result.prims.push_back(box);
        Prim label;
        label.type = PrimType::Text;
        label.text = periods[i].label;
        label.style = periodStyle;
        label.fill = Role::Background;
        label.x1 = box.x1;
        label.y1 = box.y1;
        label.x2 = box.x2;
        label.y2 = box.y2;
        result.prims.push_back(std::move(label));

        // Events stacked below, connected by a dotted drop line
        float eventY = periodBottom + 10.0f * scale;
        for (size_t e = 0; e < periods[i].events.size(); e++) {
            float boxHeight = eventSizes[i][e].h + 10.0f * scale;
            Prim drop;
            drop.type = PrimType::Line;
            drop.x1 = x + columnWidths[i] * 0.5f;
            drop.y1 = e == 0 ? periodBottom : eventY - 6.0f * scale;
            drop.x2 = drop.x1;
            drop.y2 = eventY;
            drop.stroke = Role::Muted;
            drop.strokeWidth = 1.0f * scale;
            drop.dashed = true;
            result.prims.push_back(drop);
            Prim eventBox;
            eventBox.type = PrimType::RoundRect;
            eventBox.radius = 5.0f * scale;
            eventBox.x1 = x + 2.0f * scale;
            eventBox.y1 = eventY;
            eventBox.x2 = x + columnWidths[i] - 2.0f * scale;
            eventBox.y2 = eventY + boxHeight;
            eventBox.fill = Role::SeriesSoft;
            eventBox.seriesIndex = static_cast<int>(section);
            eventBox.stroke = Role::Series;
            eventBox.strokeWidth = 1.0f * scale;
            result.prims.push_back(eventBox);
            Prim text;
            text.type = PrimType::Text;
            text.text = periods[i].events[e];
            text.style = eventStyle;
            text.fill = Role::Text;
            text.x1 = eventBox.x1 + 6.0f * scale;
            text.y1 = eventBox.y1;
            text.x2 = eventBox.x2 - 6.0f * scale;
            text.y2 = eventBox.y2;
            result.prims.push_back(std::move(text));
            eventY += boxHeight + 6.0f * scale;
        }
        maxBottom = std::max(maxBottom, eventY);
        x += columnWidths[i] + columnGap;
    }

    result.width = totalWidth;
    result.height = maxBottom + 8.0f * scale;
    result.ok = true;
    return result;
}

// --------------------------------------------------------------------------
// User journeys
// --------------------------------------------------------------------------

namespace {

struct JourneyTask {
    std::string name;
    float score = 3.0f;
    std::vector<std::string> actors;
    size_t section = 0;
};

}  // namespace

Built buildJourney(std::string_view source, const Measure& measure,
                   float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title;
    std::vector<std::string> sections;
    std::vector<JourneyTask> tasks;

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "journey")) {
                result.error = "Expected journey header";
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
        if (startsWithWord(line, "section", &rest)) {
            sections.push_back(cleanLabel(rest));
            continue;
        }
        // Task name: score: actor, actor
        size_t first = line.find(':');
        if (first == std::string_view::npos) {
            result.error = "Unsupported journey statement";
            return result;
        }
        JourneyTask task;
        task.name = cleanLabel(trimView(line.substr(0, first)));
        task.section = sections.empty() ? 0 : sections.size() - 1;
        std::string_view remainder = line.substr(first + 1);
        size_t second = remainder.find(':');
        std::string_view scoreText =
            second == std::string_view::npos ? remainder
                                             : remainder.substr(0, second);
        try {
            task.score = std::stof(std::string(trimView(scoreText)));
        } catch (...) {
            result.error = "Bad journey score";
            return result;
        }
        if (second != std::string_view::npos) {
            std::string_view actors = remainder.substr(second + 1);
            size_t position = 0;
            while (position <= actors.size()) {
                size_t comma = actors.find(',', position);
                if (comma == std::string_view::npos) comma = actors.size();
                std::string actor(
                    trimView(actors.substr(position, comma - position)));
                if (!actor.empty()) task.actors.push_back(std::move(actor));
                if (comma == actors.size()) break;
                position = comma + 1;
            }
        }
        tasks.push_back(std::move(task));
    }

    if (tasks.empty()) {
        result.error = "No tasks";
        return result;
    }
    if (sections.empty()) sections.push_back("");

    TextStyle titleStyle;
    titleStyle.bold = true;
    titleStyle.scale = 1.1f;
    TextStyle sectionStyle;
    sectionStyle.bold = true;
    sectionStyle.scale = 0.9f;
    TextStyle taskStyle;
    taskStyle.scale = 0.85f;
    TextStyle actorStyle;
    actorStyle.scale = 0.75f;
    TextStyle scoreStyle;
    scoreStyle.scale = 0.8f;
    scoreStyle.bold = true;

    float columnWidth = 96.0f * scale;
    float columnGap = 8.0f * scale;
    float totalWidth =
        tasks.size() * (columnWidth + columnGap) - columnGap;

    float y = 4.0f * scale;
    if (!title.empty()) {
        Size size = measure(title, titleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = 0;
        text.y1 = y;
        text.x2 = totalWidth;
        text.y2 = y + size.h;
        result.prims.push_back(std::move(text));
        y += size.h + 12.0f * scale;
    }

    bool hasSections = sections.size() > 1 || !sections[0].empty();
    if (hasSections) {
        float bandHeight = 24.0f * scale;
        size_t start = 0;
        float x = 0.0f;
        while (start < tasks.size()) {
            size_t section = tasks[start].section;
            size_t end = start;
            float width = 0.0f;
            while (end < tasks.size() && tasks[end].section == section) {
                width += columnWidth + columnGap;
                end++;
            }
            width -= columnGap;
            Prim band;
            band.type = PrimType::RoundRect;
            band.radius = 5.0f * scale;
            band.x1 = x;
            band.y1 = y;
            band.x2 = x + width;
            band.y2 = y + bandHeight;
            band.fill = Role::SeriesSoft;
            band.seriesIndex = static_cast<int>(section);
            result.prims.push_back(band);
            if (!sections[section].empty()) {
                Prim text;
                text.type = PrimType::Text;
                text.text = sections[section];
                text.style = sectionStyle;
                text.fill = Role::Text;
                text.x1 = band.x1;
                text.y1 = band.y1;
                text.x2 = band.x2;
                text.y2 = band.y2;
                result.prims.push_back(std::move(text));
            }
            x += width + columnGap;
            start = end;
        }
        y += bandHeight + 10.0f * scale;
    }

    // Task labels (wrapped) above the score band
    float labelHeight = 0.0f;
    std::vector<Size> labelSizes(tasks.size());
    for (size_t i = 0; i < tasks.size(); i++) {
        labelSizes[i] =
            measure(tasks[i].name, taskStyle, columnWidth - 8.0f * scale);
        labelHeight = std::max(labelHeight, labelSizes[i].h);
    }
    float labelTop = y;
    y += labelHeight + 10.0f * scale;

    // Score lane: higher score = higher dot, 1..7
    float laneHeight = 110.0f * scale;
    float laneTop = y;
    float laneBottom = y + laneHeight;
    auto scoreY = [&](float score) {
        float clamped = std::max(1.0f, std::min(7.0f, score));
        return laneBottom -
               (clamped - 1.0f) / 6.0f * (laneHeight - 18.0f * scale) -
               9.0f * scale;
    };
    auto scoreColor = [](float score, float& r, float& g, float& b) {
        // 1 = red, 4 = amber, 7 = green
        float t = (std::max(1.0f, std::min(7.0f, score)) - 1.0f) / 6.0f;
        if (t < 0.5f) {
            float k = t * 2.0f;
            r = 0.83f + (0.93f - 0.83f) * k;
            g = 0.28f + (0.69f - 0.28f) * k;
            b = 0.24f + (0.17f - 0.24f) * k;
        } else {
            float k = (t - 0.5f) * 2.0f;
            r = 0.93f + (0.30f - 0.93f) * k;
            g = 0.69f + (0.69f - 0.69f) * k;
            b = 0.17f + (0.35f - 0.17f) * k;
        }
    };

    // Connecting line through the dots
    for (size_t i = 0; i + 1 < tasks.size(); i++) {
        Prim segment;
        segment.type = PrimType::Line;
        segment.x1 = (i + 0.5f) * (columnWidth + columnGap);
        segment.y1 = scoreY(tasks[i].score);
        segment.x2 = (i + 1.5f) * (columnWidth + columnGap);
        segment.y2 = scoreY(tasks[i + 1].score);
        segment.stroke = Role::Muted;
        segment.strokeWidth = 1.4f * scale;
        result.prims.push_back(segment);
    }

    float maxBottom = laneBottom;
    for (size_t i = 0; i < tasks.size(); i++) {
        float x = i * (columnWidth + columnGap);
        float centerX = x + columnWidth * 0.5f;
        Prim label;
        label.type = PrimType::Text;
        label.text = tasks[i].name;
        label.style = taskStyle;
        label.fill = Role::Text;
        label.x1 = x + 4.0f * scale;
        label.y1 = labelTop;
        label.x2 = x + columnWidth - 4.0f * scale;
        label.y2 = labelTop + labelHeight;
        result.prims.push_back(std::move(label));

        float dotY = scoreY(tasks[i].score);
        float radius = 11.0f * scale;
        Prim dot;
        dot.type = PrimType::Ellipse;
        dot.x1 = centerX - radius;
        dot.y1 = dotY - radius;
        dot.x2 = centerX + radius;
        dot.y2 = dotY + radius;
        dot.fill = Role::Custom;
        scoreColor(tasks[i].score, dot.customR, dot.customG, dot.customB);
        result.prims.push_back(dot);
        char buffer[8];
        snprintf(buffer, sizeof(buffer), "%d",
                 static_cast<int>(std::lround(tasks[i].score)));
        Prim score;
        score.type = PrimType::Text;
        score.text = buffer;
        score.style = scoreStyle;
        score.fill = Role::Background;
        score.x1 = centerX - radius;
        score.y1 = dotY - radius;
        score.x2 = centerX + radius;
        score.y2 = dotY + radius;
        result.prims.push_back(std::move(score));

        if (!tasks[i].actors.empty()) {
            std::string joined;
            for (const auto& actor : tasks[i].actors) {
                if (!joined.empty()) joined += ", ";
                joined += actor;
            }
            Size size =
                measure(joined, actorStyle, columnWidth - 4.0f * scale);
            Prim actors;
            actors.type = PrimType::Text;
            actors.text = joined;
            actors.style = actorStyle;
            actors.fill = Role::Muted;
            actors.x1 = x + 2.0f * scale;
            actors.y1 = laneBottom + 4.0f * scale;
            actors.x2 = x + columnWidth - 2.0f * scale;
            actors.y2 = laneBottom + 4.0f * scale + size.h;
            maxBottom = std::max(maxBottom, actors.y2);
            result.prims.push_back(std::move(actors));
        }
    }

    result.width = totalWidth;
    result.height = maxBottom + 8.0f * scale;
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
