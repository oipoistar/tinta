// Native Mermaid grid-style diagrams: packet, kanban, and treemap.
//
// packet:  bit-field rows (32 bits per row), contiguous ranges required.
// kanban:  indentation defines columns and cards, with @{...} metadata.
// treemap: indentation defines the hierarchy; slice-and-dice layout.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

namespace {

// Raw line with its indentation preserved (diagramLines trims, these
// families need the depth)
struct IndentedLine {
    int indent = 0;
    std::string_view text;
};

std::vector<IndentedLine> indentedLines(std::string_view source) {
    std::vector<IndentedLine> lines;
    size_t position = 0;
    while (position <= source.size()) {
        size_t newline = source.find('\n', position);
        if (newline == std::string_view::npos) newline = source.size();
        std::string_view raw = source.substr(position, newline - position);
        bool lastLine = newline == source.size();
        position = newline + 1;

        int indent = 0;
        size_t start = 0;
        while (start < raw.size() &&
               (raw[start] == ' ' || raw[start] == '\t')) {
            indent += raw[start] == '\t' ? 4 : 1;
            start++;
        }
        std::string_view line = trimView(raw.substr(start));
        if (!line.empty() && line.substr(0, 2) != "%%") {
            lines.push_back({indent, line});
        }
        if (lastLine) break;
    }
    return lines;
}

// Reads a leading "..." token; falls back to the whole view when unquoted
std::string readLabel(std::string_view value) {
    value = trimView(value);
    if (value.size() >= 2 && value.front() == '"') {
        size_t close = value.find('"', 1);
        if (close != std::string_view::npos) {
            return cleanLabel(value.substr(1, close - 1));
        }
    }
    return cleanLabel(value);
}

}  // namespace

// --------------------------------------------------------------------------
// Packet diagrams
// --------------------------------------------------------------------------

namespace {

constexpr int kPacketBitsPerRow = 32;
constexpr float kPacketCellWidth = 24.0f;
constexpr float kPacketRowHeight = 44.0f;

struct PacketField {
    int start = 0;
    int end = 0;
    std::string label;
};

}  // namespace

Built buildPacket(std::string_view source, const Measure& measure,
                  float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title;
    std::vector<PacketField> fields;
    int nextBit = 0;

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "packet-beta") &&
                !startsWithWord(line, "packet")) {
                result.error = "Expected packet header";
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
        // "start-end: "Label"" | "bit: "Label"" | "+count: "Label""
        size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            result.error = "Expected bits: label";
            return result;
        }
        std::string_view range = trimView(line.substr(0, colon));
        PacketField field;
        try {
            if (!range.empty() && range.front() == '+') {
                int count = std::stoi(std::string(range.substr(1)));
                if (count <= 0) throw std::exception();
                field.start = nextBit;
                field.end = nextBit + count - 1;
            } else {
                size_t dash = range.find('-');
                if (dash == std::string_view::npos) {
                    field.start = std::stoi(std::string(range));
                    field.end = field.start;
                } else {
                    field.start =
                        std::stoi(std::string(range.substr(0, dash)));
                    field.end =
                        std::stoi(std::string(range.substr(dash + 1)));
                }
            }
        } catch (...) {
            result.error = "Bad packet bit range";
            return result;
        }
        if (field.start != nextBit || field.end < field.start) {
            result.error = "Packet fields must be contiguous";
            return result;
        }
        field.label = readLabel(line.substr(colon + 1));
        nextBit = field.end + 1;
        fields.push_back(std::move(field));
    }
    if (fields.empty()) {
        result.error = "No packet fields";
        return result;
    }

    TextStyle titleStyle;
    titleStyle.bold = true;
    titleStyle.scale = 1.1f;
    TextStyle labelStyle;
    labelStyle.scale = 0.85f;
    TextStyle bitStyle;
    bitStyle.scale = 0.68f;

    float cellWidth = kPacketCellWidth * scale;
    float rowHeight = kPacketRowHeight * scale;
    float diagramWidth = kPacketBitsPerRow * cellWidth;
    float y = 4.0f * scale;

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

    float gridTop = y;
    for (const auto& field : fields) {
        // Fields wrap across 32-bit rows; each row slice draws separately
        int bit = field.start;
        while (bit <= field.end) {
            int row = bit / kPacketBitsPerRow;
            int rowEndBit =
                std::min(field.end, (row + 1) * kPacketBitsPerRow - 1);
            int col0 = bit % kPacketBitsPerRow;
            int col1 = rowEndBit % kPacketBitsPerRow;

            Prim box;
            box.type = PrimType::Rect;
            box.x1 = col0 * cellWidth;
            box.y1 = gridTop + row * rowHeight;
            box.x2 = (col1 + 1) * cellWidth;
            box.y2 = box.y1 + rowHeight;
            box.fill = Role::Fill;
            box.stroke = Role::Stroke;
            box.strokeWidth = 1.2f * scale;
            result.prims.push_back(box);

            float inset = 3.0f * scale;
            float bitRowHeight = 12.0f * scale;
            if (rowEndBit > bit) {
                Prim startBit;
                startBit.type = PrimType::Text;
                startBit.text = std::to_string(bit);
                startBit.style = bitStyle;
                startBit.fill = Role::Muted;
                startBit.alignH = -1;
                startBit.alignV = -1;
                startBit.x1 = box.x1 + inset;
                startBit.y1 = box.y1 + inset;
                startBit.x2 = box.x2 - inset;
                startBit.y2 = box.y1 + inset + bitRowHeight;
                result.prims.push_back(std::move(startBit));
                Prim endBit;
                endBit.type = PrimType::Text;
                endBit.text = std::to_string(rowEndBit);
                endBit.style = bitStyle;
                endBit.fill = Role::Muted;
                endBit.alignH = 1;
                endBit.alignV = -1;
                endBit.x1 = box.x1 + inset;
                endBit.y1 = box.y1 + inset;
                endBit.x2 = box.x2 - inset;
                endBit.y2 = box.y1 + inset + bitRowHeight;
                result.prims.push_back(std::move(endBit));
            } else {
                Prim oneBit;
                oneBit.type = PrimType::Text;
                oneBit.text = std::to_string(bit);
                oneBit.style = bitStyle;
                oneBit.fill = Role::Muted;
                oneBit.alignV = -1;
                oneBit.x1 = box.x1 + inset;
                oneBit.y1 = box.y1 + inset;
                oneBit.x2 = box.x2 - inset;
                oneBit.y2 = box.y1 + inset + bitRowHeight;
                result.prims.push_back(std::move(oneBit));
            }

            Prim label;
            label.type = PrimType::Text;
            label.text = field.label;
            label.style = labelStyle;
            label.fill = Role::Text;
            label.x1 = box.x1 + inset;
            label.y1 = box.y1 + bitRowHeight;
            label.x2 = box.x2 - inset;
            label.y2 = box.y2 - 2.0f * scale;
            result.prims.push_back(std::move(label));

            bit = rowEndBit + 1;
        }
    }

    int rowCount = (nextBit + kPacketBitsPerRow - 1) / kPacketBitsPerRow;
    result.width = diagramWidth;
    result.height = gridTop + rowCount * rowHeight + 4.0f * scale;
    result.ok = true;
    return result;
}

// --------------------------------------------------------------------------
// Kanban boards
// --------------------------------------------------------------------------

namespace {

constexpr float kKanbanColumnWidth = 210.0f;
constexpr float kKanbanColumnGap = 14.0f;
constexpr float kKanbanPanelPad = 10.0f;
constexpr float kKanbanCardGap = 8.0f;
constexpr float kKanbanCardPad = 8.0f;

struct KanbanCard {
    std::string text;
    std::string ticket;
    std::string assigned;
    int priority = 0;  // 0 none, 1 very high, 2 high, 3 low, 4 very low
};

struct KanbanColumn {
    std::string title;
    std::vector<KanbanCard> cards;
};

// Strips a trailing @{ key: 'value', ... } block and captures the fields
void parseKanbanMeta(std::string& text, KanbanCard& card) {
    size_t at = text.rfind("@{");
    if (at == std::string::npos || text.back() != '}') return;
    std::string meta = text.substr(at + 2, text.size() - at - 3);
    text = std::string(trimView(std::string_view(text).substr(0, at)));

    size_t position = 0;
    while (position < meta.size()) {
        size_t colon = meta.find(':', position);
        if (colon == std::string::npos) break;
        std::string key(
            trimView(std::string_view(meta).substr(position, colon - position)));
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        size_t valueStart = colon + 1;
        while (valueStart < meta.size() &&
               std::isspace(static_cast<unsigned char>(meta[valueStart]))) {
            valueStart++;
        }
        std::string value;
        size_t next;
        if (valueStart < meta.size() &&
            (meta[valueStart] == '\'' || meta[valueStart] == '"')) {
            char quote = meta[valueStart];
            size_t close = meta.find(quote, valueStart + 1);
            if (close == std::string::npos) break;
            value = meta.substr(valueStart + 1, close - valueStart - 1);
            next = meta.find(',', close);
        } else {
            next = meta.find(',', valueStart);
            size_t end = next == std::string::npos ? meta.size() : next;
            value = std::string(
                trimView(std::string_view(meta).substr(valueStart,
                                                       end - valueStart)));
        }
        if (key == "ticket") {
            card.ticket = value;
        } else if (key == "assigned") {
            card.assigned = value;
        } else if (key == "priority") {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (value == "very high") card.priority = 1;
            else if (value == "high") card.priority = 2;
            else if (value == "low") card.priority = 3;
            else if (value == "very low") card.priority = 4;
        }
        if (next == std::string::npos) break;
        position = next + 1;
    }
}

// id[Label] | [Label] | bare text -> the display label
std::string stripKanbanBrackets(std::string_view raw) {
    std::string text(raw);
    size_t open = text.find('[');
    if (open != std::string::npos && text.back() == ']') {
        text = text.substr(open + 1, text.size() - open - 2);
    }
    return cleanLabel(text);
}

void kanbanPriorityColor(int priority, Prim& prim) {
    switch (priority) {
        case 1: prim.customR = 0.86f; prim.customG = 0.22f;
                prim.customB = 0.27f; break;
        case 2: prim.customR = 0.92f; prim.customG = 0.55f;
                prim.customB = 0.15f; break;
        case 3: prim.customR = 0.25f; prim.customG = 0.55f;
                prim.customB = 0.85f; break;
        default: prim.customR = 0.55f; prim.customG = 0.55f;
                 prim.customB = 0.58f; break;
    }
}

}  // namespace

Built buildKanban(std::string_view source, const Measure& measure,
                  float scale) {
    Built result;
    auto lines = indentedLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::vector<KanbanColumn> columns;
    int columnIndent = -1;
    bool sawHeader = false;
    for (const auto& entry : lines) {
        if (!sawHeader) {
            if (!startsWithWord(entry.text, "kanban")) {
                result.error = "Expected kanban header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        if (columnIndent < 0) columnIndent = entry.indent;
        if (entry.indent <= columnIndent) {
            KanbanColumn column;
            column.title = stripKanbanBrackets(entry.text);
            columns.push_back(std::move(column));
        } else {
            if (columns.empty()) {
                result.error = "Kanban card before any column";
                return result;
            }
            KanbanCard card;
            std::string text(entry.text);
            parseKanbanMeta(text, card);
            card.text = stripKanbanBrackets(text);
            columns.back().cards.push_back(std::move(card));
        }
    }
    if (columns.empty()) {
        result.error = "No kanban columns";
        return result;
    }

    TextStyle headerStyle;
    headerStyle.bold = true;
    TextStyle cardStyle;
    cardStyle.scale = 0.9f;
    TextStyle metaStyle;
    metaStyle.scale = 0.78f;

    float columnWidth = kKanbanColumnWidth * scale;
    float columnGap = kKanbanColumnGap * scale;
    float panelPad = kKanbanPanelPad * scale;
    float cardGap = kKanbanCardGap * scale;
    float cardPad = kKanbanCardPad * scale;
    float cardWidth = columnWidth - panelPad * 2.0f;
    float textWidth = cardWidth - cardPad * 2.0f;
    float headerHeight = 26.0f * scale;

    // Measure card heights and the tallest column
    struct CardBox {
        float height = 0.0f;
        float textHeight = 0.0f;
        bool metaLine = false;
    };
    std::vector<std::vector<CardBox>> cardBoxes(columns.size());
    float tallest = 0.0f;
    for (size_t i = 0; i < columns.size(); i++) {
        float total = headerHeight + panelPad;
        for (const auto& card : columns[i].cards) {
            CardBox box;
            Size textSize = measure(card.text, cardStyle, textWidth);
            box.textHeight = textSize.h;
            box.metaLine = !card.ticket.empty() || !card.assigned.empty();
            box.height = textSize.h + cardPad * 2.0f;
            if (box.metaLine) {
                Size metaSize = measure("Xg", metaStyle, 0.0f);
                box.height += metaSize.h + 2.0f * scale;
            }
            total += box.height + cardGap;
            cardBoxes[i].push_back(box);
        }
        if (!columns[i].cards.empty()) total -= cardGap;
        total += panelPad;
        tallest = std::max(tallest, total);
    }
    tallest = std::max(tallest, headerHeight + panelPad * 2.0f);

    float y = 4.0f * scale;
    for (size_t i = 0; i < columns.size(); i++) {
        float x = i * (columnWidth + columnGap);

        Prim panel;
        panel.type = PrimType::RoundRect;
        panel.radius = 8.0f * scale;
        panel.x1 = x;
        panel.y1 = y;
        panel.x2 = x + columnWidth;
        panel.y2 = y + tallest;
        panel.fill = Role::Fill;
        result.prims.push_back(panel);

        Prim header;
        header.type = PrimType::Text;
        header.text = columns[i].title;
        header.style = headerStyle;
        header.fill = Role::Text;
        header.x1 = x + panelPad;
        header.y1 = y + 4.0f * scale;
        header.x2 = x + columnWidth - panelPad;
        header.y2 = y + headerHeight;
        result.prims.push_back(std::move(header));

        float cardY = y + headerHeight + panelPad;
        for (size_t j = 0; j < columns[i].cards.size(); j++) {
            const auto& card = columns[i].cards[j];
            const auto& box = cardBoxes[i][j];
            float cardX = x + panelPad;

            Prim cardPrim;
            cardPrim.type = PrimType::RoundRect;
            cardPrim.radius = 6.0f * scale;
            cardPrim.x1 = cardX;
            cardPrim.y1 = cardY;
            cardPrim.x2 = cardX + cardWidth;
            cardPrim.y2 = cardY + box.height;
            cardPrim.fill = Role::Background;
            cardPrim.stroke = Role::Muted;
            cardPrim.strokeWidth = 1.0f * scale;
            result.prims.push_back(cardPrim);

            if (card.priority != 0) {
                Prim strip;
                strip.type = PrimType::Line;
                strip.x1 = cardX + 1.5f * scale;
                strip.y1 = cardY + 4.0f * scale;
                strip.x2 = strip.x1;
                strip.y2 = cardY + box.height - 4.0f * scale;
                strip.stroke = Role::Custom;
                kanbanPriorityColor(card.priority, strip);
                strip.strokeWidth = 3.0f * scale;
                result.prims.push_back(std::move(strip));
            }

            Prim text;
            text.type = PrimType::Text;
            text.text = card.text;
            text.style = cardStyle;
            text.fill = Role::Text;
            text.alignH = -1;
            text.alignV = -1;
            text.x1 = cardX + cardPad;
            text.y1 = cardY + cardPad;
            text.x2 = cardX + cardWidth - cardPad;
            text.y2 = cardY + cardPad + box.textHeight;
            result.prims.push_back(std::move(text));

            if (box.metaLine) {
                float metaTop = cardY + cardPad + box.textHeight + 2.0f * scale;
                if (!card.ticket.empty()) {
                    Prim ticket;
                    ticket.type = PrimType::Text;
                    ticket.text = card.ticket;
                    ticket.style = metaStyle;
                    ticket.fill = Role::Muted;
                    ticket.alignH = -1;
                    ticket.x1 = cardX + cardPad;
                    ticket.y1 = metaTop;
                    ticket.x2 = cardX + cardWidth - cardPad;
                    ticket.y2 = cardY + box.height - 2.0f * scale;
                    result.prims.push_back(std::move(ticket));
                }
                if (!card.assigned.empty()) {
                    Prim assigned;
                    assigned.type = PrimType::Text;
                    assigned.text = card.assigned;
                    assigned.style = metaStyle;
                    assigned.fill = Role::Muted;
                    assigned.alignH = 1;
                    assigned.x1 = cardX + cardPad;
                    assigned.y1 = metaTop;
                    assigned.x2 = cardX + cardWidth - cardPad;
                    assigned.y2 = cardY + box.height - 2.0f * scale;
                    result.prims.push_back(std::move(assigned));
                }
            }

            cardY += box.height + cardGap;
        }
    }

    result.width =
        columns.size() * (columnWidth + columnGap) - columnGap;
    result.height = y + tallest + 4.0f * scale;
    result.ok = true;
    return result;
}

// --------------------------------------------------------------------------
// Treemaps
// --------------------------------------------------------------------------

namespace {

struct TreeNode {
    std::string label;
    float value = 0.0f;
    bool leaf = false;
    int indent = 0;
    size_t parent = SIZE_MAX;
    std::vector<size_t> children;
    int branch = 0;  // top-level index for coloring
};

constexpr float kTreemapWidth = 640.0f;
constexpr float kTreemapHeight = 420.0f;
constexpr float kTreemapHeader = 22.0f;

struct TreemapEmit {
    std::vector<TreeNode>* nodes;
    Built* result;
    const Measure* measure;
    float scale;
};

// Slice-and-dice: split horizontally at even depth, vertically at odd
void treemapLayout(TreemapEmit& ctx, size_t index, float x1, float y1,
                   float x2, float y2, int depth) {
    auto& nodes = *ctx.nodes;
    const auto& node = nodes[index];
    float scale = ctx.scale;

    TextStyle leafStyle;
    leafStyle.scale = 0.85f;
    TextStyle valueStyle;
    valueStyle.scale = 0.75f;
    TextStyle headerStyle;
    headerStyle.bold = true;
    headerStyle.scale = 0.9f;

    if (node.leaf) {
        Prim box;
        box.type = PrimType::Rect;
        box.x1 = x1;
        box.y1 = y1;
        box.x2 = x2;
        box.y2 = y2;
        box.fill = Role::SeriesSoft;
        box.stroke = Role::Series;
        box.seriesIndex = node.branch;
        box.strokeWidth = 1.2f * scale;
        ctx.result->prims.push_back(box);

        float w = x2 - x1;
        float h = y2 - y1;
        if (w > 46.0f * scale && h > 30.0f * scale) {
            char buffer[32];
            float rounded = std::round(node.value * 100.0f) / 100.0f;
            if (rounded == std::floor(rounded)) {
                snprintf(buffer, sizeof(buffer), "%d",
                         static_cast<int>(rounded));
            } else {
                snprintf(buffer, sizeof(buffer), "%.2f", rounded);
            }
            bool showValue = h > 52.0f * scale;
            float labelBottom = showValue ? (y1 + y2) * 0.5f + 2.0f * scale
                                          : y2 - 2.0f * scale;
            Prim label;
            label.type = PrimType::Text;
            label.text = node.label;
            label.style = leafStyle;
            label.fill = Role::Text;
            label.x1 = x1 + 3.0f * scale;
            label.y1 = y1 + 2.0f * scale;
            label.x2 = x2 - 3.0f * scale;
            label.y2 = labelBottom;
            label.alignV = showValue ? 1 : 0;
            ctx.result->prims.push_back(std::move(label));
            if (showValue) {
                Prim value;
                value.type = PrimType::Text;
                value.text = buffer;
                value.style = valueStyle;
                value.fill = Role::Muted;
                value.alignV = -1;
                value.x1 = x1 + 3.0f * scale;
                value.y1 = labelBottom;
                value.x2 = x2 - 3.0f * scale;
                value.y2 = y2 - 2.0f * scale;
                ctx.result->prims.push_back(std::move(value));
            }
        }
        return;
    }

    // Branch: header strip, then children in the remaining area
    float header = kTreemapHeader * scale;
    Prim frame;
    frame.type = PrimType::Rect;
    frame.x1 = x1;
    frame.y1 = y1;
    frame.x2 = x2;
    frame.y2 = y2;
    frame.stroke = Role::Series;
    frame.seriesIndex = node.branch;
    frame.strokeWidth = 1.4f * scale;
    ctx.result->prims.push_back(frame);

    Prim strip;
    strip.type = PrimType::Rect;
    strip.x1 = x1;
    strip.y1 = y1;
    strip.x2 = x2;
    strip.y2 = std::min(y1 + header, y2);
    strip.fill = Role::SeriesSoft;
    strip.seriesIndex = node.branch;
    ctx.result->prims.push_back(strip);

    Prim title;
    title.type = PrimType::Text;
    title.text = node.label;
    title.style = headerStyle;
    title.fill = Role::Text;
    title.x1 = x1 + 5.0f * scale;
    title.y1 = y1;
    title.x2 = x2 - 5.0f * scale;
    title.y2 = std::min(y1 + header, y2);
    title.alignH = -1;
    ctx.result->prims.push_back(std::move(title));

    float pad = 3.0f * scale;
    float innerX1 = x1 + pad;
    float innerY1 = y1 + header + pad;
    float innerX2 = x2 - pad;
    float innerY2 = y2 - pad;
    if (innerX2 <= innerX1 || innerY2 <= innerY1) return;

    float total = node.value;
    if (total <= 0.0f) return;
    bool horizontal = depth % 2 == 0;
    float cursor = horizontal ? innerX1 : innerY1;
    float span = horizontal ? innerX2 - innerX1 : innerY2 - innerY1;
    for (size_t child : node.children) {
        float fraction = nodes[child].value / total;
        float extent = span * fraction;
        if (horizontal) {
            treemapLayout(ctx, child, cursor, innerY1, cursor + extent,
                          innerY2, depth + 1);
        } else {
            treemapLayout(ctx, child, innerX1, cursor, innerX2,
                          cursor + extent, depth + 1);
        }
        cursor += extent;
    }
}

}  // namespace

Built buildTreemap(std::string_view source, const Measure& measure,
                   float scale) {
    Built result;
    auto lines = indentedLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title;
    std::vector<TreeNode> nodes;
    std::vector<size_t> stack;
    bool sawHeader = false;
    for (const auto& entry : lines) {
        std::string_view line = entry.text;
        if (!sawHeader) {
            if (!startsWithWord(line, "treemap-beta") &&
                !startsWithWord(line, "treemap")) {
                result.error = "Expected treemap header";
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
        if (startsWithWord(line, "classDef")) continue;

        // ":::class" styling suffix is dropped
        size_t classMark = line.rfind(":::");
        if (classMark != std::string_view::npos) {
            line = trimView(line.substr(0, classMark));
        }

        TreeNode node;
        node.indent = entry.indent;
        if (!line.empty() && line.front() == '"') {
            size_t close = line.find('"', 1);
            if (close == std::string_view::npos) {
                result.error = "Unterminated treemap label";
                return result;
            }
            node.label = cleanLabel(line.substr(1, close - 1));
            std::string_view remainder = trimView(line.substr(close + 1));
            if (!remainder.empty()) {
                if (remainder.front() != ':') {
                    result.error = "Expected : after treemap label";
                    return result;
                }
                try {
                    node.value =
                        std::stof(std::string(trimView(remainder.substr(1))));
                } catch (...) {
                    result.error = "Bad treemap value";
                    return result;
                }
                node.leaf = true;
            }
        } else {
            // Unquoted: value only when the tail parses as a number
            size_t colon = line.rfind(':');
            if (colon != std::string_view::npos) {
                try {
                    node.value = std::stof(
                        std::string(trimView(line.substr(colon + 1))));
                    node.leaf = true;
                    line = trimView(line.substr(0, colon));
                } catch (...) {
                }
            }
            node.label = cleanLabel(line);
        }
        if (node.leaf && node.value < 0.0f) {
            result.error = "Negative treemap value";
            return result;
        }

        while (!stack.empty() && nodes[stack.back()].indent >= node.indent) {
            stack.pop_back();
        }
        if (!stack.empty()) node.parent = stack.back();
        nodes.push_back(std::move(node));
        size_t index = nodes.size() - 1;
        if (nodes[index].parent != SIZE_MAX) {
            nodes[nodes[index].parent].children.push_back(index);
        }
        stack.push_back(index);
    }
    if (nodes.empty()) {
        result.error = "Empty treemap";
        return result;
    }

    // Roots, branch colors, and value roll-up (children sum into parents)
    std::vector<size_t> roots;
    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes[i].parent == SIZE_MAX) {
            nodes[i].branch = static_cast<int>(roots.size());
            roots.push_back(i);
        } else {
            nodes[i].branch = nodes[nodes[i].parent].branch;
        }
    }
    for (size_t i = nodes.size(); i-- > 0;) {
        if (!nodes[i].children.empty()) {
            float sum = 0.0f;
            for (size_t child : nodes[i].children) {
                sum += nodes[child].value;
            }
            nodes[i].value = sum;
        } else if (!nodes[i].leaf) {
            nodes[i].value = 0.0f;
        }
    }
    float total = 0.0f;
    for (size_t root : roots) total += nodes[root].value;
    if (total <= 0.0f) {
        result.error = "Treemap has no values";
        return result;
    }

    float y = 4.0f * scale;
    if (!title.empty()) {
        TextStyle titleStyle;
        titleStyle.bold = true;
        titleStyle.scale = 1.1f;
        Size titleSize = measure(title, titleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = 0;
        text.y1 = y;
        text.x2 = kTreemapWidth * scale;
        text.y2 = y + titleSize.h;
        result.prims.push_back(std::move(text));
        y += titleSize.h + 10.0f * scale;
    }

    TreemapEmit ctx{&nodes, &result, &measure, scale};
    // Top-level entries slice the canvas horizontally by value share
    float x = 0.0f;
    float width = kTreemapWidth * scale;
    float height = kTreemapHeight * scale;
    for (size_t root : roots) {
        float extent = width * (nodes[root].value / total);
        treemapLayout(ctx, root, x, y, x + extent, y + height, 1);
        x += extent;
    }

    result.width = width;
    result.height = y + height + 4.0f * scale;
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
