// Native Mermaid block diagrams (block-beta).
//
// Grid placement: `columns N` per container, items may span cells (`:N`),
// `space` skips cells, `block:id ... end` nests a container, and edges
// connect any two blocks by id. Shapes reuse the flowchart bracket forms;
// `id<["Label"]>(dir)` renders a fat arrow block. style/class lines are
// accepted and ignored.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

namespace {

constexpr float kBlockCellMargin = 6.0f;
constexpr float kBlockPad = 10.0f;
constexpr float kBlockMinCell = 76.0f;
constexpr float kBlockMinHeight = 40.0f;
constexpr float kBlockWrap = 150.0f;

enum class BlockShape {
    Rect,
    Rounded,
    Stadium,
    Cylinder,
    Circle,
    Diamond,
    Hexagon,
    ArrowRight,
    ArrowLeft,
    ArrowUp,
    ArrowDown,
    ArrowX,   // left+right
    ArrowY,   // up+down
};

struct BlockItem {
    std::string id;
    std::string label;
    BlockShape shape = BlockShape::Rect;
    int span = 1;
    bool space = false;
    bool container = false;
    // Container payload
    int columns = -1;
    std::vector<BlockItem> children;
    // Filled by layout
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

struct BlockEdge {
    std::string from;
    std::string to;
    std::string label;
    bool arrow = true;
};

// Splits a statement line into tokens at top-level whitespace; brackets,
// braces, parens, angle brackets, and quotes bind their contents
std::vector<std::string_view> blockTokens(std::string_view line) {
    std::vector<std::string_view> tokens;
    size_t start = std::string_view::npos;
    int depth = 0;
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inQuote) {
            if (c == '"') inQuote = false;
            continue;
        }
        if (c == '"') {
            inQuote = true;
            if (start == std::string_view::npos) start = i;
            continue;
        }
        if (c == '[' || c == '(' || c == '{' || c == '<') depth++;
        if (c == ']' || c == ')' || c == '}' || c == '>') {
            if (depth > 0) depth--;
        }
        if (std::isspace(static_cast<unsigned char>(c)) && depth == 0) {
            if (start != std::string_view::npos) {
                tokens.push_back(line.substr(start, i - start));
                start = std::string_view::npos;
            }
        } else if (start == std::string_view::npos) {
            start = i;
        }
    }
    if (start != std::string_view::npos) {
        tokens.push_back(line.substr(start));
    }
    return tokens;
}

std::string unquote(std::string_view value) {
    value = trimView(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return cleanLabel(value);
}

// id + shape bracket + optional :span -> item; false on malformed tokens
bool parseBlockItem(std::string_view token, BlockItem& item) {
    // Trailing :span (outside any closing bracket)
    size_t colon = token.rfind(':');
    if (colon != std::string_view::npos) {
        std::string_view tail = token.substr(colon + 1);
        bool numeric = !tail.empty();
        for (char c : tail) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                numeric = false;
            }
        }
        char before = colon > 0 ? token[colon - 1] : ' ';
        bool afterBracket = colon == 0 ||
                            !(before == ']' || before == ')' ||
                              before == '}');
        (void)afterBracket;
        if (numeric) {
            item.span = std::stoi(std::string(tail));
            if (item.span < 1) return false;
            token = token.substr(0, colon);
        }
    }

    if (token.empty()) return false;
    if (token == "space") {
        item.space = true;
        return true;
    }

    // Fat arrow: id<["Label"]>(direction)
    size_t angle = token.find("<[");
    if (angle != std::string_view::npos && token.back() == ')') {
        size_t close = token.find("]>(", angle);
        if (close == std::string_view::npos) return false;
        item.id = std::string(token.substr(0, angle));
        item.label = unquote(token.substr(angle + 2, close - angle - 2));
        std::string direction(
            token.substr(close + 3, token.size() - close - 4));
        std::transform(direction.begin(), direction.end(),
                       direction.begin(), [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (direction == "right") item.shape = BlockShape::ArrowRight;
        else if (direction == "left") item.shape = BlockShape::ArrowLeft;
        else if (direction == "up") item.shape = BlockShape::ArrowUp;
        else if (direction == "down") item.shape = BlockShape::ArrowDown;
        else if (direction == "x") item.shape = BlockShape::ArrowX;
        else if (direction == "y") item.shape = BlockShape::ArrowY;
        else return false;
        return true;
    }

    struct Delimiter {
        const char* open;
        const char* close;
        BlockShape shape;
    };
    // Longest-first so (["x"]) wins over ("x")
    static const Delimiter kDelimiters[] = {
        {"([", "])", BlockShape::Stadium},
        {"[(", ")]", BlockShape::Cylinder},
        {"((", "))", BlockShape::Circle},
        {"{{", "}}", BlockShape::Hexagon},
        {"[", "]", BlockShape::Rect},
        {"(", ")", BlockShape::Rounded},
        {"{", "}", BlockShape::Diamond},
    };
    for (const auto& delimiter : kDelimiters) {
        size_t openLength = strlen(delimiter.open);
        size_t closeLength = strlen(delimiter.close);
        size_t open = token.find(delimiter.open);
        if (open == std::string_view::npos) continue;
        if (token.size() < open + openLength + closeLength) continue;
        if (token.compare(token.size() - closeLength, closeLength,
                          delimiter.close) != 0) {
            continue;
        }
        item.id = std::string(token.substr(0, open));
        item.label = unquote(token.substr(
            open + openLength,
            token.size() - closeLength - open - openLength));
        item.shape = delimiter.shape;
        if (item.id.empty()) item.id = item.label;
        return true;
    }

    // Bare id
    for (char c : token) {
        if (std::isspace(static_cast<unsigned char>(c))) return false;
    }
    item.id = std::string(token);
    item.label = item.id;
    return true;
}

bool isEdgeToken(std::string_view token) {
    if (token.size() < 2) return false;
    for (char c : token) {
        if (c != '-' && c != '>' && c != '.') return false;
    }
    return true;
}

struct BlockMeasureCtx {
    const Measure* measure;
    float scale;
};

// Natural cell width / row heights for a container (recursive)
struct ContainerMetrics {
    float cellWidth = 0.0f;
    std::vector<float> rowHeights;
    int columns = 1;
    float width() const {
        return columns * cellWidth;
    }
    float height() const {
        float total = 0.0f;
        for (float h : rowHeights) total += h;
        return total;
    }
};

ContainerMetrics measureContainer(BlockMeasureCtx& ctx, BlockItem& container);

// Natural size of one item (width includes the span divisor)
void measureItem(BlockMeasureCtx& ctx, BlockItem& item, float& cellWidth,
                 float& height) {
    float scale = ctx.scale;
    if (item.space) {
        // A space consumes a real cell; spacer-only rows keep some height
        cellWidth = kBlockMinCell * 0.5f * scale;
        height = kBlockMinHeight * 0.6f * scale;
        return;
    }
    if (item.container) {
        ContainerMetrics metrics = measureContainer(ctx, item);
        float natural = metrics.width() +
                        (kBlockPad * 2.0f + kBlockCellMargin) * scale;
        cellWidth = natural / item.span;
        height = metrics.height() + (kBlockPad * 2.0f + 18.0f) * scale;
        return;
    }
    TextStyle style;
    style.scale = 0.95f;
    Size size = (*ctx.measure)(item.label, style, kBlockWrap * scale);
    float pad = 26.0f * scale;
    if (item.shape == BlockShape::Diamond) pad = 44.0f * scale;
    if (item.shape == BlockShape::Hexagon) pad = 40.0f * scale;
    if (item.shape == BlockShape::Circle) pad = 34.0f * scale;
    if (item.shape >= BlockShape::ArrowRight) pad = 44.0f * scale;
    cellWidth = std::max(kBlockMinCell * scale, size.w + pad) / item.span;
    height = std::max(kBlockMinHeight * scale, size.h + 18.0f * scale);
    if (item.shape == BlockShape::Diamond) height *= 1.35f;
    if (item.shape == BlockShape::Circle) {
        height = std::max(height, size.w + pad);
    }
}

ContainerMetrics measureContainer(BlockMeasureCtx& ctx,
                                  BlockItem& container) {
    ContainerMetrics metrics;
    int totalCells = 0;
    for (auto& child : container.children) totalCells += child.span;
    metrics.columns = container.columns > 0
                          ? container.columns
                          : std::max(1, totalCells);

    int cell = 0;
    float rowHeight = 0.0f;
    for (auto& child : container.children) {
        float childCell = 0.0f, childHeight = 0.0f;
        measureItem(ctx, child, childCell, childHeight);
        metrics.cellWidth = std::max(metrics.cellWidth, childCell);
        if (cell + child.span > metrics.columns && cell > 0) {
            metrics.rowHeights.push_back(rowHeight);
            rowHeight = 0.0f;
            cell = 0;
        }
        rowHeight = std::max(rowHeight, childHeight);
        cell += child.span;
        if (cell >= metrics.columns) {
            metrics.rowHeights.push_back(rowHeight);
            rowHeight = 0.0f;
            cell = 0;
        }
    }
    if (cell > 0) metrics.rowHeights.push_back(rowHeight);
    if (metrics.rowHeights.empty()) metrics.rowHeights.push_back(0.0f);
    metrics.cellWidth = std::max(metrics.cellWidth,
                                 kBlockMinCell * ctx.scale * 0.5f);
    return metrics;
}

struct BlockEmitCtx {
    BlockMeasureCtx* measure;
    std::vector<Prim>* shapes;
    std::vector<Prim>* texts;
    std::map<std::string, BlockItem*>* index;
};

void emitLeaf(BlockEmitCtx& ctx, BlockItem& item);

// Lays out a container's children into the given content rect
void emitContainer(BlockEmitCtx& ctx, BlockItem& container, float x1,
                   float y1, float x2, float y2) {
    BlockMeasureCtx& mctx = *ctx.measure;
    float scale = mctx.scale;
    ContainerMetrics metrics = measureContainer(mctx, container);
    float cellWidth = (x2 - x1) / metrics.columns;

    int cell = 0;
    size_t row = 0;
    float rowTop = y1;
    for (auto& child : container.children) {
        if (cell + child.span > metrics.columns && cell > 0) {
            rowTop += metrics.rowHeights[row];
            row++;
            cell = 0;
        }
        float rowHeight =
            row < metrics.rowHeights.size() ? metrics.rowHeights[row] : 0.0f;
        float cx1 = x1 + cell * cellWidth + kBlockCellMargin * scale * 0.5f;
        float cx2 = x1 + (cell + child.span) * cellWidth -
                    kBlockCellMargin * scale * 0.5f;
        float cy1 = rowTop + kBlockCellMargin * scale * 0.5f;
        float cy2 = rowTop + rowHeight - kBlockCellMargin * scale * 0.5f;
        cell += child.span;
        bool wrapped = cell >= metrics.columns;

        if (!child.space) {
            child.x1 = cx1;
            child.y1 = cy1;
            child.x2 = cx2;
            child.y2 = cy2;
            if (!child.id.empty()) (*ctx.index)[child.id] = &child;
            if (child.container) {
                Prim panel;
                panel.type = PrimType::RoundRect;
                panel.radius = 8.0f * scale;
                panel.x1 = cx1;
                panel.y1 = cy1;
                panel.x2 = cx2;
                panel.y2 = cy2;
                panel.fill = Role::AccentSoft;
                panel.stroke = Role::Stroke;
                panel.strokeWidth = 1.2f * scale;
                ctx.shapes->push_back(panel);
                float pad = kBlockPad * scale;
                emitContainer(ctx, child, cx1 + pad, cy1 + pad, cx2 - pad,
                              cy2 - pad);
            } else {
                emitLeaf(ctx, child);
            }
        }
        if (wrapped) {
            rowTop += rowHeight;
            row++;
            cell = 0;
        }
    }
}

void emitLeaf(BlockEmitCtx& ctx, BlockItem& item) {
    float scale = ctx.measure->scale;
    float x1 = item.x1, y1 = item.y1, x2 = item.x2, y2 = item.y2;
    float w = x2 - x1;
    float h = y2 - y1;

    Prim shape;
    shape.fill = Role::Fill;
    shape.stroke = Role::Stroke;
    shape.strokeWidth = 1.5f * scale;
    bool fatArrow = item.shape >= BlockShape::ArrowRight;

    switch (item.shape) {
        case BlockShape::Rounded:
            shape.type = PrimType::RoundRect;
            shape.radius = 8.0f * scale;
            shape.x1 = x1; shape.y1 = y1; shape.x2 = x2; shape.y2 = y2;
            break;
        case BlockShape::Stadium:
            shape.type = PrimType::RoundRect;
            shape.radius = h * 0.5f;
            shape.x1 = x1; shape.y1 = y1; shape.x2 = x2; shape.y2 = y2;
            break;
        case BlockShape::Circle: {
            shape.type = PrimType::Ellipse;
            float d = std::min(w, h);
            float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;
            shape.x1 = cx - d * 0.5f; shape.y1 = cy - d * 0.5f;
            shape.x2 = cx + d * 0.5f; shape.y2 = cy + d * 0.5f;
            break;
        }
        case BlockShape::Cylinder: {
            // Body plus an ellipse lid reads as a cylinder
            shape.type = PrimType::RoundRect;
            shape.radius = 9.0f * scale;
            shape.x1 = x1; shape.y1 = y1; shape.x2 = x2; shape.y2 = y2;
            ctx.shapes->push_back(shape);
            Prim lid;
            lid.type = PrimType::Ellipse;
            lid.x1 = x1;
            lid.y1 = y1;
            lid.x2 = x2;
            lid.y2 = y1 + 12.0f * scale;
            lid.stroke = Role::Stroke;
            lid.strokeWidth = 1.2f * scale;
            ctx.shapes->push_back(std::move(lid));
            shape.type = PrimType::Text;  // sentinel: already pushed
            break;
        }
        case BlockShape::Diamond:
            shape.type = PrimType::Polygon;
            shape.pts = {{(x1 + x2) * 0.5f, y1},
                         {x2, (y1 + y2) * 0.5f},
                         {(x1 + x2) * 0.5f, y2},
                         {x1, (y1 + y2) * 0.5f}};
            break;
        case BlockShape::Hexagon: {
            shape.type = PrimType::Polygon;
            float inset = w * 0.16f;
            shape.pts = {{x1 + inset, y1}, {x2 - inset, y1},
                         {x2, (y1 + y2) * 0.5f}, {x2 - inset, y2},
                         {x1 + inset, y2}, {x1, (y1 + y2) * 0.5f}};
            break;
        }
        default:
            if (fatArrow) {
                shape.type = PrimType::Polygon;
                shape.fill = Role::AccentSoft;
                shape.stroke = Role::Stroke;
                float head = std::min(24.0f * scale, w * 0.3f);
                float waist = h * 0.28f;
                float cy = (y1 + y2) * 0.5f;
                switch (item.shape) {
                    case BlockShape::ArrowRight:
                        shape.pts = {{x1, cy - waist}, {x2 - head, cy - waist},
                                     {x2 - head, y1}, {x2, cy},
                                     {x2 - head, y2}, {x2 - head, cy + waist},
                                     {x1, cy + waist}};
                        break;
                    case BlockShape::ArrowLeft:
                        shape.pts = {{x2, cy - waist}, {x1 + head, cy - waist},
                                     {x1 + head, y1}, {x1, cy},
                                     {x1 + head, y2}, {x1 + head, cy + waist},
                                     {x2, cy + waist}};
                        break;
                    case BlockShape::ArrowX:
                        shape.pts = {{x1 + head, cy - waist},
                                     {x2 - head, cy - waist},
                                     {x2 - head, y1}, {x2, cy},
                                     {x2 - head, y2}, {x2 - head, cy + waist},
                                     {x1 + head, cy + waist},
                                     {x1 + head, y2}, {x1, cy},
                                     {x1 + head, y1}};
                        break;
                    case BlockShape::ArrowUp: {
                        float headV = std::min(20.0f * scale, h * 0.35f);
                        float waistW = w * 0.28f;
                        float cx = (x1 + x2) * 0.5f;
                        shape.pts = {{cx - waistW, y2},
                                     {cx - waistW, y1 + headV},
                                     {x1, y1 + headV}, {cx, y1},
                                     {x2, y1 + headV},
                                     {cx + waistW, y1 + headV},
                                     {cx + waistW, y2}};
                        break;
                    }
                    case BlockShape::ArrowDown: {
                        float headV = std::min(20.0f * scale, h * 0.35f);
                        float waistW = w * 0.28f;
                        float cx = (x1 + x2) * 0.5f;
                        shape.pts = {{cx - waistW, y1},
                                     {cx - waistW, y2 - headV},
                                     {x1, y2 - headV}, {cx, y2},
                                     {x2, y2 - headV},
                                     {cx + waistW, y2 - headV},
                                     {cx + waistW, y1}};
                        break;
                    }
                    default: {  // ArrowY
                        float headV = std::min(20.0f * scale, h * 0.3f);
                        float waistW = w * 0.28f;
                        float cx = (x1 + x2) * 0.5f;
                        shape.pts = {{cx - waistW, y1 + headV},
                                     {x1, y1 + headV}, {cx, y1},
                                     {x2, y1 + headV},
                                     {cx + waistW, y1 + headV},
                                     {cx + waistW, y2 - headV},
                                     {x2, y2 - headV}, {cx, y2},
                                     {x1, y2 - headV},
                                     {cx - waistW, y2 - headV}};
                        break;
                    }
                }
            } else {
                shape.type = PrimType::Rect;
                shape.x1 = x1; shape.y1 = y1; shape.x2 = x2; shape.y2 = y2;
            }
            break;
    }
    if (shape.type != PrimType::Text) {
        ctx.shapes->push_back(std::move(shape));
    }

    if (!item.label.empty()) {
        TextStyle style;
        style.scale = 0.95f;
        Prim text;
        text.type = PrimType::Text;
        text.text = item.label;
        text.style = style;
        text.fill = Role::Text;
        float insetX = 4.0f * scale;
        if (item.shape == BlockShape::Diamond ||
            item.shape == BlockShape::Hexagon) {
            insetX = w * 0.16f;
        }
        text.x1 = x1 + insetX;
        text.y1 = y1;
        text.x2 = x2 - insetX;
        text.y2 = y2;
        ctx.texts->push_back(std::move(text));
    }
}

// Clips the segment from `outside` toward the rect center at the border
Point clipToRect(float cx, float cy, float ox, float oy, float x1, float y1,
                 float x2, float y2) {
    float dx = ox - cx;
    float dy = oy - cy;
    float t = 1.0f;
    if (dx > 0.0001f) t = std::min(t, (x2 - cx) / dx);
    if (dx < -0.0001f) t = std::min(t, (x1 - cx) / dx);
    if (dy > 0.0001f) t = std::min(t, (y2 - cy) / dy);
    if (dy < -0.0001f) t = std::min(t, (y1 - cy) / dy);
    return {cx + dx * t, cy + dy * t};
}

}  // namespace

Built buildBlock(std::string_view source, const Measure& measure,
                 float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    BlockItem root;
    root.container = true;
    std::vector<BlockItem*> stack = {&root};
    std::vector<BlockEdge> edges;

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "block-beta") &&
                !startsWithWord(line, "block")) {
                result.error = "Expected block header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "columns", &rest)) {
            try {
                stack.back()->columns = std::stoi(std::string(rest));
            } catch (...) {
                result.error = "Bad columns";
                return result;
            }
            continue;
        }
        if (startsWithWord(line, "style") || startsWithWord(line, "class") ||
            startsWithWord(line, "classDef")) {
            continue;  // styling hints, nothing structural
        }
        if (line == "end") {
            if (stack.size() <= 1) {
                result.error = "Unmatched end";
                return result;
            }
            stack.pop_back();
            continue;
        }

        auto tokens = blockTokens(line);
        // Edge statement: id (-- "label")? --> id
        bool edgeLine = false;
        for (const auto& token : tokens) {
            if (isEdgeToken(token)) edgeLine = true;
        }
        if (edgeLine) {
            BlockEdge edge;
            if (tokens.size() == 3 && isEdgeToken(tokens[1])) {
                edge.from = std::string(tokens[0]);
                edge.to = std::string(tokens[2]);
                edge.arrow = tokens[1].find('>') != std::string_view::npos;
            } else if (tokens.size() == 5 && isEdgeToken(tokens[1]) &&
                       isEdgeToken(tokens[3])) {
                edge.from = std::string(tokens[0]);
                edge.label = unquote(tokens[2]);
                edge.to = std::string(tokens[4]);
                edge.arrow = tokens[3].find('>') != std::string_view::npos;
            } else {
                result.error = "Unsupported block edge";
                return result;
            }
            edges.push_back(std::move(edge));
            continue;
        }

        for (const auto& token : tokens) {
            if (token.substr(0, 6) == "block:") {
                BlockItem container;
                container.container = true;
                std::string_view spec = token.substr(6);
                size_t colon = spec.find(':');
                if (colon != std::string_view::npos) {
                    try {
                        container.span = std::stoi(
                            std::string(spec.substr(colon + 1)));
                    } catch (...) {
                        result.error = "Bad block span";
                        return result;
                    }
                    spec = spec.substr(0, colon);
                }
                container.id = std::string(spec);
                stack.back()->children.push_back(std::move(container));
                stack.push_back(&stack.back()->children.back());
                continue;
            }
            if (token == "end") {
                if (stack.size() <= 1) {
                    result.error = "Unmatched end";
                    return result;
                }
                stack.pop_back();
                continue;
            }
            BlockItem item;
            if (!parseBlockItem(token, item)) {
                result.error = "Unsupported block statement";
                return result;
            }
            stack.back()->children.push_back(std::move(item));
        }
    }
    if (root.children.empty()) {
        result.error = "Empty block diagram";
        return result;
    }

    BlockMeasureCtx mctx{&measure, scale};
    ContainerMetrics metrics = measureContainer(mctx, root);

    std::vector<Prim> shapes;
    std::vector<Prim> texts;
    std::map<std::string, BlockItem*> index;
    BlockEmitCtx ectx{&mctx, &shapes, &texts, &index};
    float top = 4.0f * scale;
    emitContainer(ectx, root, 0.0f, top, metrics.width(),
                  top + metrics.height());

    result.prims = std::move(shapes);

    // Edges over the blocks, clipped to their borders
    for (const auto& edge : edges) {
        auto fromIt = index.find(edge.from);
        auto toIt = index.find(edge.to);
        if (fromIt == index.end() || toIt == index.end()) {
            result.error = "Unknown block in edge";
            return result;
        }
        const BlockItem& from = *fromIt->second;
        const BlockItem& to = *toIt->second;
        float fx = (from.x1 + from.x2) * 0.5f;
        float fy = (from.y1 + from.y2) * 0.5f;
        float tx = (to.x1 + to.x2) * 0.5f;
        float ty = (to.y1 + to.y2) * 0.5f;
        Point start = clipToRect(fx, fy, tx, ty, from.x1, from.y1, from.x2,
                                 from.y2);
        Point end = clipToRect(tx, ty, fx, fy, to.x1, to.y1, to.x2, to.y2);

        Prim line;
        line.type = PrimType::Line;
        line.x1 = start.x;
        line.y1 = start.y;
        line.x2 = end.x;
        line.y2 = end.y;
        line.stroke = Role::Muted;
        line.strokeWidth = 1.4f * scale;
        line.arrow = edge.arrow;
        result.prims.push_back(std::move(line));

        if (!edge.label.empty()) {
            TextStyle style;
            style.scale = 0.82f;
            Size size = measure(edge.label, style, 0.0f);
            float cx = (start.x + end.x) * 0.5f;
            float cy = (start.y + end.y) * 0.5f;
            Prim chip;
            chip.type = PrimType::RoundRect;
            chip.radius = 4.0f * scale;
            chip.x1 = cx - size.w * 0.5f - 5.0f * scale;
            chip.y1 = cy - size.h * 0.5f - 2.0f * scale;
            chip.x2 = cx + size.w * 0.5f + 5.0f * scale;
            chip.y2 = cy + size.h * 0.5f + 2.0f * scale;
            chip.fill = Role::Background;
            chip.stroke = Role::Muted;
            chip.strokeWidth = 1.0f * scale;
            result.prims.push_back(chip);

            Prim label;
            label.type = PrimType::Text;
            label.text = edge.label;
            label.style = style;
            label.fill = Role::Text;
            label.x1 = chip.x1;
            label.y1 = chip.y1;
            label.x2 = chip.x2;
            label.y2 = chip.y2;
            texts.push_back(std::move(label));
        }
    }

    for (auto& text : texts) result.prims.push_back(std::move(text));

    result.width = metrics.width();
    result.height = top + metrics.height() + 4.0f * scale;
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
