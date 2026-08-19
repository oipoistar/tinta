// Native Mermaid renderers for the graph-shaped UML families: state
// diagrams, class diagrams, and entity-relationship diagrams. All three
// parse into the flowchart engine's mermaid::Diagram to reuse its rank
// layout, then route edges and draw nodes through the primitive pipeline.

#include "mermaid.h"
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

constexpr float kNodeGap = 34.0f;
constexpr float kRankGap = 64.0f;
constexpr float kEdgeLabelPad = 5.0f;
constexpr float kMarkerDepth = 11.0f;

enum class Marker {
    None,
    FilledArrow,
    OpenArrow,
    HollowTriangle,
    HollowDiamond,
    FilledDiamond,
    CrowMany,     // crow's foot (+ bar/circle by flags below)
    One,          // double bar
    ZeroOne,      // circle + bar
    ZeroMany,     // circle + crow's foot
    OneMany,      // bar + crow's foot
};

struct EdgeDecor {
    Marker atFrom = Marker::None;
    Marker atTo = Marker::None;
    std::string labelFrom;  // multiplicity near the from end
    std::string labelTo;
    bool dashed = false;
    std::string label;
};

struct GraphNodes {
    mermaid::Diagram diagram;
    std::map<std::string, size_t, std::less<>> ids;

    size_t ensure(std::string_view id, std::string_view label) {
        auto found = ids.find(id);
        if (found != ids.end()) {
            if (!label.empty()) {
                diagram.nodes[found->second].label = cleanLabel(label);
            }
            return found->second;
        }
        mermaid::Node node;
        node.id = std::string(id);
        node.label = cleanLabel(label.empty() ? id : label);
        diagram.nodes.push_back(std::move(node));
        ids.emplace(std::string(id), diagram.nodes.size() - 1);
        return diagram.nodes.size() - 1;
    }
};

struct RoutedEdge {
    std::vector<Point> points;
};

// The rank layout is longest-path over a DAG; cyclic graphs (the norm for
// state machines: start/stop, pause/resume) would fall into its
// sequential-order fallback. Reverse DFS back-edges for RANKING ONLY —
// routing still uses the original edge directions.
mermaid::Diagram acyclicForRanking(const mermaid::Diagram& diagram) {
    size_t count = diagram.nodes.size();
    std::vector<std::vector<size_t>> outgoing(count);
    for (size_t i = 0; i < diagram.edges.size(); i++) {
        const auto& edge = diagram.edges[i];
        if (edge.from < count && edge.to < count && edge.from != edge.to) {
            outgoing[edge.from].push_back(i);
        }
    }
    std::vector<size_t> indegree(count, 0);
    for (const auto& edge : diagram.edges) {
        if (edge.to < count && edge.from != edge.to) indegree[edge.to]++;
    }

    enum : unsigned char { White, Gray, Black };
    std::vector<unsigned char> color(count, White);
    std::vector<bool> reversed(diagram.edges.size(), false);

    struct StackFrame {
        size_t node;
        size_t next = 0;
    };
    auto dfs = [&](size_t root) {
        std::vector<StackFrame> stack{{root}};
        color[root] = Gray;
        while (!stack.empty()) {
            StackFrame& frame = stack.back();
            if (frame.next < outgoing[frame.node].size()) {
                size_t edgeIndex = outgoing[frame.node][frame.next++];
                size_t target = diagram.edges[edgeIndex].to;
                if (color[target] == Gray) {
                    reversed[edgeIndex] = true;  // back edge
                } else if (color[target] == White) {
                    color[target] = Gray;
                    stack.push_back({target});
                }
            } else {
                color[frame.node] = Black;
                stack.pop_back();
            }
        }
    };
    for (size_t i = 0; i < count; i++) {
        if (indegree[i] == 0 && color[i] == White) dfs(i);
    }
    for (size_t i = 0; i < count; i++) {
        if (color[i] == White) dfs(i);
    }

    mermaid::Diagram ranking = diagram;
    for (size_t i = 0; i < ranking.edges.size(); i++) {
        if (reversed[i]) std::swap(ranking.edges[i].from, ranking.edges[i].to);
    }
    return ranking;
}

// Elbow-routes every edge the way the flowchart renderer does, against
// laid-out node rects. Returns per-edge polylines (diagram coordinates).
std::vector<RoutedEdge> routeEdges(const mermaid::Diagram& diagram,
                                   const mermaid::Layout& layout,
                                   float scale, float& maxRight,
                                   float& maxBottom) {
    std::vector<RoutedEdge> routed(diagram.edges.size());
    bool vertical =
        diagram.direction == mermaid::Direction::TopToBottom ||
        diagram.direction == mermaid::Direction::BottomToTop;
    bool topToBottom = diagram.direction == mermaid::Direction::TopToBottom;
    bool leftToRight = diagram.direction == mermaid::Direction::LeftToRight;
    size_t exteriorLane = 0;

    for (size_t i = 0; i < diagram.edges.size(); i++) {
        const auto& edge = diagram.edges[i];
        if (edge.from >= layout.nodes.size() ||
            edge.to >= layout.nodes.size()) {
            continue;
        }
        const auto& from = layout.nodes[edge.from];
        const auto& to = layout.nodes[edge.to];
        float fromCx = (from.left + from.right) * 0.5f;
        float fromCy = (from.top + from.bottom) * 0.5f;
        float toCx = (to.left + to.right) * 0.5f;
        float toCy = (to.top + to.bottom) * 0.5f;
        bool selfLoop = edge.from == edge.to;
        bool skipsRanks = false;
        if (edge.from < layout.ranks.size() && edge.to < layout.ranks.size()) {
            size_t fromRank = layout.ranks[edge.from];
            size_t toRank = layout.ranks[edge.to];
            skipsRanks = (fromRank < toRank ? toRank - fromRank
                                            : fromRank - toRank) > 1;
        }

        auto& points = routed[i].points;
        if (selfLoop) {
            float lane = (30.0f + exteriorLane++ * 14.0f) * scale;
            points = {
                {from.right, fromCy - 8.0f * scale},
                {from.right + lane, fromCy - 8.0f * scale},
                {from.right + lane, fromCy + 8.0f * scale},
                {from.right, fromCy + 8.0f * scale},
            };
        } else if (vertical) {
            bool forward =
                !skipsRanks &&
                (topToBottom ? toCy > fromCy : toCy < fromCy);
            if (forward) {
                float startY = topToBottom ? from.bottom : from.top;
                float endY = topToBottom ? to.top : to.bottom;
                float middle = (startY + endY) * 0.5f;
                points = {
                    {fromCx, startY},
                    {fromCx, middle},
                    {toCx, middle},
                    {toCx, endY},
                };
            } else {
                float lane = (30.0f + exteriorLane++ * 14.0f) * scale;
                float laneX = std::max(from.right, to.right) + lane;
                points = {
                    {from.right, fromCy},
                    {laneX, fromCy},
                    {laneX, toCy},
                    {to.right, toCy},
                };
            }
        } else {
            bool forward =
                !skipsRanks &&
                (leftToRight ? toCx > fromCx : toCx < fromCx);
            if (forward) {
                float startX = leftToRight ? from.right : from.left;
                float endX = leftToRight ? to.left : to.right;
                float middle = (startX + endX) * 0.5f;
                points = {
                    {startX, fromCy},
                    {middle, fromCy},
                    {middle, toCy},
                    {endX, toCy},
                };
            } else {
                float lane = (30.0f + exteriorLane++ * 14.0f) * scale;
                float laneY = std::max(from.bottom, to.bottom) + lane;
                points = {
                    {fromCx, from.bottom},
                    {fromCx, laneY},
                    {toCx, laneY},
                    {toCx, to.bottom},
                };
            }
        }
        for (const auto& point : points) {
            maxRight = std::max(maxRight, point.x);
            maxBottom = std::max(maxBottom, point.y);
        }
    }
    return routed;
}

// Removes duplicate consecutive points so direction vectors are non-zero
void dedupePoints(std::vector<Point>& points) {
    for (size_t i = points.size(); i-- > 1;) {
        if (std::abs(points[i].x - points[i - 1].x) < 0.01f &&
            std::abs(points[i].y - points[i - 1].y) < 0.01f) {
            points.erase(points.begin() + i);
        }
    }
}

// Emits an end marker at points[index] (0 = start, back = end); the marker
// apex touches the node border and the line is shortened underneath solid
// markers so it does not poke through
void emitMarker(std::vector<Prim>& prims, std::vector<Point>& points,
                bool atStart, Marker marker, float scale) {
    if (marker == Marker::None || points.size() < 2) return;
    Point& tip = atStart ? points.front() : points.back();
    const Point& neighbor = atStart ? points[1] : points[points.size() - 2];
    float dx = neighbor.x - tip.x;
    float dy = neighbor.y - tip.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.01f) return;
    dx /= length;  // direction AWAY from the node, along the line
    dy /= length;
    float px = -dy, py = dx;  // perpendicular
    float depth = kMarkerDepth * scale;

    auto polygon = [&](std::vector<Point> pts, Role fill, Role stroke) {
        Prim prim;
        prim.type = PrimType::Polygon;
        prim.pts = std::move(pts);
        prim.fill = fill;
        prim.stroke = stroke;
        prim.strokeWidth = 1.3f * scale;
        prims.push_back(std::move(prim));
    };
    auto lineAt = [&](Point a, Point b) {
        Prim prim;
        prim.type = PrimType::Line;
        prim.x1 = a.x;
        prim.y1 = a.y;
        prim.x2 = b.x;
        prim.y2 = b.y;
        prim.stroke = Role::Muted;
        prim.strokeWidth = 1.4f * scale;
        prims.push_back(prim);
    };
    auto shorten = [&](float amount) {
        tip.x += dx * amount;
        tip.y += dy * amount;
    };

    switch (marker) {
        case Marker::FilledArrow: {
            float wing = 4.5f * scale;
            polygon({{tip.x, tip.y},
                     {tip.x + dx * depth * 0.85f + px * wing,
                      tip.y + dy * depth * 0.85f + py * wing},
                     {tip.x + dx * depth * 0.85f - px * wing,
                      tip.y + dy * depth * 0.85f - py * wing}},
                    Role::Muted, Role::None);
            shorten(depth * 0.6f);
            break;
        }
        case Marker::OpenArrow: {
            float wing = 4.5f * scale;
            lineAt(tip, {tip.x + dx * depth * 0.85f + px * wing,
                         tip.y + dy * depth * 0.85f + py * wing});
            lineAt(tip, {tip.x + dx * depth * 0.85f - px * wing,
                         tip.y + dy * depth * 0.85f - py * wing});
            break;
        }
        case Marker::HollowTriangle: {
            float wing = 6.0f * scale;
            float triDepth = depth * 1.15f;
            polygon({{tip.x, tip.y},
                     {tip.x + dx * triDepth + px * wing,
                      tip.y + dy * triDepth + py * wing},
                     {tip.x + dx * triDepth - px * wing,
                      tip.y + dy * triDepth - py * wing}},
                    Role::Background, Role::Muted);
            shorten(depth * 1.15f);
            break;
        }
        case Marker::HollowDiamond:
        case Marker::FilledDiamond: {
            float wing = 5.0f * scale;
            float half = depth * 0.75f;
            polygon({{tip.x, tip.y},
                     {tip.x + dx * half + px * wing,
                      tip.y + dy * half + py * wing},
                     {tip.x + dx * half * 2.0f, tip.y + dy * half * 2.0f},
                     {tip.x + dx * half - px * wing,
                      tip.y + dy * half - py * wing}},
                    marker == Marker::FilledDiamond ? Role::Muted
                                                    : Role::Background,
                    Role::Muted);
            shorten(half * 2.0f);
            break;
        }
        case Marker::CrowMany:
        case Marker::ZeroMany:
        case Marker::OneMany: {
            float wing = 5.5f * scale;
            Point base = {tip.x + dx * depth, tip.y + dy * depth};
            lineAt(base, tip);
            lineAt(base, {tip.x + px * wing, tip.y + py * wing});
            lineAt(base, {tip.x - px * wing, tip.y - py * wing});
            if (marker == Marker::ZeroMany) {
                float r = 3.5f * scale;
                Prim circle;
                circle.type = PrimType::Ellipse;
                circle.x1 = base.x + dx * (r + 2.0f * scale) - r;
                circle.y1 = base.y + dy * (r + 2.0f * scale) - r;
                circle.x2 = circle.x1 + r * 2.0f;
                circle.y2 = circle.y1 + r * 2.0f;
                circle.fill = Role::Background;
                circle.stroke = Role::Muted;
                circle.strokeWidth = 1.3f * scale;
                prims.push_back(circle);
            } else if (marker == Marker::OneMany) {
                float barAt = depth + 4.0f * scale;
                float wing2 = 5.0f * scale;
                lineAt({tip.x + dx * barAt + px * wing2,
                        tip.y + dy * barAt + py * wing2},
                       {tip.x + dx * barAt - px * wing2,
                        tip.y + dy * barAt - py * wing2});
            }
            break;
        }
        case Marker::One: {
            float wing = 5.0f * scale;
            for (float at : {7.0f, 11.0f}) {
                float depth2 = at * scale;
                lineAt({tip.x + dx * depth2 + px * wing,
                        tip.y + dy * depth2 + py * wing},
                       {tip.x + dx * depth2 - px * wing,
                        tip.y + dy * depth2 - py * wing});
            }
            break;
        }
        case Marker::ZeroOne: {
            float wing = 5.0f * scale;
            float barAt = 7.0f * scale;
            lineAt({tip.x + dx * barAt + px * wing,
                    tip.y + dy * barAt + py * wing},
                   {tip.x + dx * barAt - px * wing,
                    tip.y + dy * barAt - py * wing});
            float r = 3.5f * scale;
            float circleAt = barAt + 3.0f * scale + r;
            Prim circle;
            circle.type = PrimType::Ellipse;
            circle.x1 = tip.x + dx * circleAt - r;
            circle.y1 = tip.y + dy * circleAt - r;
            circle.x2 = circle.x1 + r * 2.0f;
            circle.y2 = circle.y1 + r * 2.0f;
            circle.fill = Role::Background;
            circle.stroke = Role::Muted;
            circle.strokeWidth = 1.3f * scale;
            prims.push_back(circle);
            break;
        }
        case Marker::None:
            break;
    }
}

// Emits the routed edge polylines with markers and label chips
void emitEdges(std::vector<Prim>& prims, const mermaid::Diagram& diagram,
               std::vector<RoutedEdge>& routed,
               const std::vector<EdgeDecor>& decors, const Measure& measure,
               float scale) {
    TextStyle labelStyle;
    labelStyle.scale = 0.9f;
    for (size_t i = 0; i < diagram.edges.size(); i++) {
        auto& points = routed[i].points;
        if (points.size() < 2) continue;
        dedupePoints(points);
        if (points.size() < 2) continue;
        const EdgeDecor& decor =
            i < decors.size() ? decors[i] : EdgeDecor{};

        emitMarker(prims, points, true, decor.atFrom, scale);
        emitMarker(prims, points, false, decor.atTo, scale);

        for (size_t p = 1; p < points.size(); p++) {
            Prim line;
            line.type = PrimType::Line;
            line.x1 = points[p - 1].x;
            line.y1 = points[p - 1].y;
            line.x2 = points[p].x;
            line.y2 = points[p].y;
            line.stroke = Role::Muted;
            line.strokeWidth = 1.4f * scale;
            line.dashed = decor.dashed;
            prims.push_back(line);
        }

        const std::string& label =
            !decor.label.empty() ? decor.label : diagram.edges[i].label;
        if (!label.empty()) {
            Size size = measure(label, labelStyle, 0.0f);
            // Chip at the half-length point of the polyline
            float total = 0.0f;
            for (size_t p = 1; p < points.size(); p++) {
                total += std::abs(points[p].x - points[p - 1].x) +
                         std::abs(points[p].y - points[p - 1].y);
            }
            float remaining = total * 0.5f;
            float cx = (points.front().x + points.back().x) * 0.5f;
            float cy = (points.front().y + points.back().y) * 0.5f;
            for (size_t p = 1; p < points.size(); p++) {
                float segment =
                    std::abs(points[p].x - points[p - 1].x) +
                    std::abs(points[p].y - points[p - 1].y);
                if (segment >= remaining && segment > 0.0f) {
                    float t = remaining / segment;
                    cx = points[p - 1].x + (points[p].x - points[p - 1].x) * t;
                    cy = points[p - 1].y + (points[p].y - points[p - 1].y) * t;
                    break;
                }
                remaining -= segment;
            }
            Prim chip;
            chip.type = PrimType::RoundRect;
            chip.radius = 4.0f * scale;
            chip.x1 = cx - size.w * 0.5f - kEdgeLabelPad * scale;
            chip.y1 = cy - size.h * 0.5f - 2.0f * scale;
            chip.x2 = cx + size.w * 0.5f + kEdgeLabelPad * scale;
            chip.y2 = cy + size.h * 0.5f + 2.0f * scale;
            chip.fill = Role::Fill;
            chip.stroke = Role::Muted;
            chip.strokeWidth = 1.0f * scale;
            prims.push_back(chip);
            Prim text;
            text.type = PrimType::Text;
            text.text = label;
            text.style = labelStyle;
            text.fill = Role::Text;
            text.x1 = chip.x1;
            text.y1 = chip.y1;
            text.x2 = chip.x2;
            text.y2 = chip.y2;
            prims.push_back(std::move(text));
        }

        // Multiplicity labels near the ends
        auto endLabel = [&](const std::string& value, bool atStart) {
            if (value.empty()) return;
            Size size = measure(value, labelStyle, 0.0f);
            const Point& tip = atStart ? points.front() : points.back();
            const Point& neighbor =
                atStart ? points[1] : points[points.size() - 2];
            float dx = neighbor.x - tip.x;
            float dy = neighbor.y - tip.y;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length < 0.01f) return;
            dx /= length;
            dy /= length;
            float alongX = tip.x + dx * 16.0f * scale;
            float alongY = tip.y + dy * 16.0f * scale;
            float sideX = -dy, sideY = dx;
            Prim text;
            text.type = PrimType::Text;
            text.text = value;
            text.style = labelStyle;
            text.fill = Role::Muted;
            text.x1 = alongX + sideX * 8.0f * scale -
                      (sideX < -0.5f ? size.w : sideX > 0.5f ? 0.0f
                                                             : size.w * 0.5f);
            text.y1 = alongY + sideY * 8.0f * scale -
                      (sideY < -0.5f ? size.h : sideY > 0.5f ? 0.0f
                                                             : size.h * 0.5f);
            text.x2 = text.x1 + size.w;
            text.y2 = text.y1 + size.h;
            text.alignH = -1;
            prims.push_back(std::move(text));
        };
        endLabel(decor.labelFrom, true);
        endLabel(decor.labelTo, false);
    }
}

}  // namespace

// --------------------------------------------------------------------------
// State diagrams
// --------------------------------------------------------------------------

namespace {

enum class StateKind { Normal, Start, End, Choice, Fork };

struct StateNote {
    size_t node = 0;
    bool rightOf = true;
    std::string text;
};

}  // namespace

Built buildState(std::string_view source, const Measure& measure,
                 float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    GraphNodes graph;
    graph.diagram.direction = mermaid::Direction::TopToBottom;
    std::vector<StateKind> kinds;
    std::vector<std::vector<std::string>> descriptions;
    std::vector<EdgeDecor> decors;
    std::vector<StateNote> notes;
    size_t startNode = SIZE_MAX;
    size_t endNode = SIZE_MAX;

    auto ensureState = [&](std::string_view id,
                           std::string_view label) -> size_t {
        size_t index = graph.ensure(id, label);
        while (kinds.size() < graph.diagram.nodes.size()) {
            kinds.push_back(StateKind::Normal);
            descriptions.emplace_back();
        }
        return index;
    };
    auto ensureEndpoint = [&](bool isStart) -> size_t {
        size_t& node = isStart ? startNode : endNode;
        if (node == SIZE_MAX) {
            node = ensureState(isStart ? "__start" : "__end", " ");
            kinds[node] = isStart ? StateKind::Start : StateKind::End;
            graph.diagram.nodes[node].label.clear();
        }
        return node;
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "stateDiagram-v2") &&
                !startsWithWord(line, "stateDiagram")) {
                result.error = "Expected stateDiagram header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "direction", &rest)) {
            if (rest == "LR") {
                graph.diagram.direction = mermaid::Direction::LeftToRight;
            } else if (rest == "RL") {
                graph.diagram.direction = mermaid::Direction::RightToLeft;
            } else if (rest == "BT") {
                graph.diagram.direction = mermaid::Direction::BottomToTop;
            } else {
                graph.diagram.direction = mermaid::Direction::TopToBottom;
            }
            continue;
        }
        if (line.find('{') != std::string_view::npos ||
            line == "--" || line == "}") {
            result.error = "Composite states are not supported";
            return result;
        }
        if (startsWithWord(line, "note", &rest)) {
            StateNote note;
            std::string_view spec = rest;
            if (startsWithWord(spec, "right", &spec)) {
                note.rightOf = true;
            } else if (startsWithWord(spec, "left", &spec)) {
                note.rightOf = false;
            } else {
                result.error = "Unsupported note position";
                return result;
            }
            startsWithWord(spec, "of", &spec);
            size_t colon = spec.find(':');
            if (colon == std::string_view::npos) {
                result.error = "Multi-line notes are not supported";
                return result;
            }
            note.node = ensureState(trimView(spec.substr(0, colon)), {});
            note.text = cleanLabel(trimView(spec.substr(colon + 1)));
            notes.push_back(std::move(note));
            continue;
        }
        if (startsWithWord(line, "state", &rest)) {
            // state "description" as id  |  state id <<choice|fork|join>>
            if (!rest.empty() && rest.front() == '"') {
                size_t closeQuote = rest.find('"', 1);
                if (closeQuote == std::string_view::npos) {
                    result.error = "Unterminated state description";
                    return result;
                }
                std::string_view description = rest.substr(1, closeQuote - 1);
                std::string_view after = trimView(rest.substr(closeQuote + 1));
                std::string_view id;
                if (startsWithWord(after, "as", &id)) {
                    ensureState(trimView(id), description);
                } else {
                    ensureState(description, description);
                }
            } else {
                size_t marker = rest.find("<<");
                if (marker != std::string_view::npos) {
                    std::string_view id = trimView(rest.substr(0, marker));
                    std::string_view kind = rest.substr(marker + 2);
                    size_t close = kind.find(">>");
                    if (close != std::string_view::npos) {
                        kind = kind.substr(0, close);
                    }
                    size_t node = ensureState(id, {});
                    if (kind == "choice") {
                        kinds[node] = StateKind::Choice;
                    } else if (kind == "fork" || kind == "join") {
                        kinds[node] = StateKind::Fork;
                        graph.diagram.nodes[node].label.clear();
                    }
                } else {
                    ensureState(trimView(rest), {});
                }
            }
            continue;
        }

        // Transition: A --> B [: label], with [*] as start/end
        size_t arrow = line.find("-->");
        if (arrow != std::string_view::npos) {
            std::string_view fromId = trimView(line.substr(0, arrow));
            std::string_view afterArrow = line.substr(arrow + 3);
            std::string_view toId = afterArrow;
            std::string label;
            size_t colon = afterArrow.find(':');
            if (colon != std::string_view::npos) {
                toId = trimView(afterArrow.substr(0, colon));
                label = cleanLabel(trimView(afterArrow.substr(colon + 1)));
            } else {
                toId = trimView(afterArrow);
            }
            if (fromId.empty() || toId.empty()) {
                result.error = "Transition needs two states";
                return result;
            }
            size_t from = fromId == "[*]" ? ensureEndpoint(true)
                                          : ensureState(fromId, {});
            size_t to = toId == "[*]" ? ensureEndpoint(false)
                                      : ensureState(toId, {});
            mermaid::Edge edge;
            edge.from = from;
            edge.to = to;
            edge.label = label;
            graph.diagram.edges.push_back(std::move(edge));
            EdgeDecor decor;
            decor.atTo = Marker::FilledArrow;
            decors.push_back(std::move(decor));
            continue;
        }

        // Description: id : text
        size_t colon = line.find(':');
        if (colon != std::string_view::npos && colon > 0) {
            size_t node = ensureState(trimView(line.substr(0, colon)), {});
            descriptions[node].push_back(
                cleanLabel(trimView(line.substr(colon + 1))));
            continue;
        }

        result.error = "Unsupported state statement";
        return result;
    }

    if (graph.diagram.nodes.empty()) {
        result.error = "No states";
        return result;
    }

    // --- measure nodes ---
    TextStyle nameStyle;
    nameStyle.bold = true;
    TextStyle descStyle;
    descStyle.scale = 0.9f;
    bool vertical =
        graph.diagram.direction == mermaid::Direction::TopToBottom ||
        graph.diagram.direction == mermaid::Direction::BottomToTop;

    std::vector<mermaid::Size> sizes(graph.diagram.nodes.size());
    std::vector<Size> nameSizes(graph.diagram.nodes.size());
    for (size_t i = 0; i < graph.diagram.nodes.size(); i++) {
        switch (kinds[i]) {
            case StateKind::Start:
            case StateKind::End:
                sizes[i] = {16.0f * scale, 16.0f * scale};
                break;
            case StateKind::Choice:
                sizes[i] = {36.0f * scale, 36.0f * scale};
                break;
            case StateKind::Fork:
                sizes[i] = vertical
                               ? mermaid::Size{72.0f * scale, 9.0f * scale}
                               : mermaid::Size{9.0f * scale, 72.0f * scale};
                break;
            case StateKind::Normal: {
                Size nameSize =
                    measure(graph.diagram.nodes[i].label, nameStyle, 0.0f);
                nameSizes[i] = nameSize;
                float width = nameSize.w;
                float height = nameSize.h;
                if (!descriptions[i].empty()) {
                    for (const auto& description : descriptions[i]) {
                        Size size = measure(description, descStyle, 0.0f);
                        width = std::max(width, size.w);
                        height += size.h;
                    }
                    height += 6.0f * scale;  // separator breathing room
                }
                sizes[i] = {std::max(56.0f * scale, width + 24.0f * scale),
                            height + 16.0f * scale};
                break;
            }
        }
    }

    mermaid::Layout layout = mermaid::layout(
        acyclicForRanking(graph.diagram), sizes, kNodeGap * scale,
        kRankGap * scale);
    if (layout.nodes.size() != graph.diagram.nodes.size()) {
        result.error = "Layout failed";
        return result;
    }

    float maxRight = layout.width;
    float maxBottom = layout.height;
    auto routed =
        routeEdges(graph.diagram, layout, scale, maxRight, maxBottom);
    emitEdges(result.prims, graph.diagram, routed, decors, measure, scale);

    // --- nodes ---
    for (size_t i = 0; i < graph.diagram.nodes.size(); i++) {
        const auto& rect = layout.nodes[i];
        switch (kinds[i]) {
            case StateKind::Start: {
                Prim dot;
                dot.type = PrimType::Ellipse;
                dot.x1 = rect.left;
                dot.y1 = rect.top;
                dot.x2 = rect.right;
                dot.y2 = rect.bottom;
                dot.fill = Role::Accent;
                result.prims.push_back(dot);
                break;
            }
            case StateKind::End: {
                Prim ring;
                ring.type = PrimType::Ellipse;
                ring.x1 = rect.left;
                ring.y1 = rect.top;
                ring.x2 = rect.right;
                ring.y2 = rect.bottom;
                ring.stroke = Role::Accent;
                ring.strokeWidth = 1.6f * scale;
                result.prims.push_back(ring);
                Prim dot;
                dot.type = PrimType::Ellipse;
                float inset = 4.0f * scale;
                dot.x1 = rect.left + inset;
                dot.y1 = rect.top + inset;
                dot.x2 = rect.right - inset;
                dot.y2 = rect.bottom - inset;
                dot.fill = Role::Accent;
                result.prims.push_back(dot);
                break;
            }
            case StateKind::Choice: {
                Prim diamond;
                diamond.type = PrimType::Polygon;
                float cx = (rect.left + rect.right) * 0.5f;
                float cy = (rect.top + rect.bottom) * 0.5f;
                diamond.pts = {{cx, rect.top},
                               {rect.right, cy},
                               {cx, rect.bottom},
                               {rect.left, cy}};
                diamond.fill = Role::Fill;
                diamond.stroke = Role::Stroke;
                diamond.strokeWidth = 1.5f * scale;
                result.prims.push_back(std::move(diamond));
                break;
            }
            case StateKind::Fork: {
                Prim bar;
                bar.type = PrimType::RoundRect;
                bar.radius = 3.0f * scale;
                bar.x1 = rect.left;
                bar.y1 = rect.top;
                bar.x2 = rect.right;
                bar.y2 = rect.bottom;
                bar.fill = Role::Accent;
                result.prims.push_back(bar);
                break;
            }
            case StateKind::Normal: {
                Prim box;
                box.type = PrimType::RoundRect;
                box.radius = 7.0f * scale;
                box.x1 = rect.left;
                box.y1 = rect.top;
                box.x2 = rect.right;
                box.y2 = rect.bottom;
                box.fill = Role::Fill;
                box.stroke = Role::Stroke;
                box.strokeWidth = 1.5f * scale;
                result.prims.push_back(box);
                if (descriptions[i].empty()) {
                    Prim name;
                    name.type = PrimType::Text;
                    name.text = graph.diagram.nodes[i].label;
                    name.style = nameStyle;
                    name.fill = Role::Text;
                    name.x1 = rect.left;
                    name.y1 = rect.top;
                    name.x2 = rect.right;
                    name.y2 = rect.bottom;
                    result.prims.push_back(std::move(name));
                } else {
                    float nameHeight = nameSizes[i].h + 8.0f * scale;
                    Prim name;
                    name.type = PrimType::Text;
                    name.text = graph.diagram.nodes[i].label;
                    name.style = nameStyle;
                    name.fill = Role::Text;
                    name.x1 = rect.left;
                    name.y1 = rect.top;
                    name.x2 = rect.right;
                    name.y2 = rect.top + nameHeight;
                    result.prims.push_back(std::move(name));
                    Prim separator;
                    separator.type = PrimType::Line;
                    separator.x1 = rect.left;
                    separator.y1 = rect.top + nameHeight;
                    separator.x2 = rect.right;
                    separator.y2 = rect.top + nameHeight;
                    separator.stroke = Role::Stroke;
                    separator.strokeWidth = 1.0f * scale;
                    result.prims.push_back(separator);
                    std::string joined;
                    for (const auto& description : descriptions[i]) {
                        if (!joined.empty()) joined += '\n';
                        joined += description;
                    }
                    Prim body;
                    body.type = PrimType::Text;
                    body.text = joined;
                    body.style = descStyle;
                    body.fill = Role::Text;
                    body.alignH = -1;
                    body.x1 = rect.left + 10.0f * scale;
                    body.y1 = rect.top + nameHeight + 3.0f * scale;
                    body.x2 = rect.right - 10.0f * scale;
                    body.y2 = rect.bottom - 3.0f * scale;
                    result.prims.push_back(std::move(body));
                }
                break;
            }
        }
    }

    // Inline notes beside their nodes
    TextStyle noteStyle;
    noteStyle.scale = 0.9f;
    for (const auto& note : notes) {
        const auto& rect = layout.nodes[note.node];
        Size size = measure(note.text, noteStyle, 220.0f * scale);
        float width = size.w + 16.0f * scale;
        float height = size.h + 10.0f * scale;
        float top = (rect.top + rect.bottom) * 0.5f - height * 0.5f;
        float left = note.rightOf ? rect.right + 16.0f * scale
                                  : rect.left - 16.0f * scale - width;
        Prim box;
        box.type = PrimType::Rect;
        box.x1 = left;
        box.y1 = top;
        box.x2 = left + width;
        box.y2 = top + height;
        box.fill = Role::AccentSoft;
        box.stroke = Role::Stroke;
        box.strokeWidth = 1.0f * scale;
        result.prims.push_back(box);
        Prim text;
        text.type = PrimType::Text;
        text.text = note.text;
        text.style = noteStyle;
        text.fill = Role::Text;
        text.x1 = left + 8.0f * scale;
        text.y1 = top + 5.0f * scale;
        text.x2 = left + width - 8.0f * scale;
        text.y2 = top + height - 5.0f * scale;
        result.prims.push_back(std::move(text));
        Prim connectorLine;
        connectorLine.type = PrimType::Line;
        connectorLine.x1 = note.rightOf ? rect.right : rect.left;
        connectorLine.y1 = (rect.top + rect.bottom) * 0.5f;
        connectorLine.x2 = note.rightOf ? left : left + width;
        connectorLine.y2 = top + height * 0.5f;
        connectorLine.stroke = Role::Muted;
        connectorLine.strokeWidth = 1.0f * scale;
        connectorLine.dashed = true;
        result.prims.push_back(connectorLine);
        maxRight = std::max(maxRight, left + width);
        maxBottom = std::max(maxBottom, top + height);
    }

    result.width = maxRight + 8.0f * scale;
    result.height = maxBottom + 8.0f * scale;
    normalizeLeft(result);
    result.ok = true;
    return result;
}

// --------------------------------------------------------------------------
// Class diagrams
// --------------------------------------------------------------------------

namespace {

struct ClassMembers {
    std::string stereotype;
    std::vector<std::string> attributes;
    std::vector<std::string> methods;
};

// ~T~ generics display as <T>
std::string displayGenerics(std::string value) {
    bool open = true;
    for (char& c : value) {
        if (c == '~') {
            c = open ? '<' : '>';
            open = !open;
        }
    }
    return value;
}

void addClassMember(ClassMembers& members, std::string_view raw) {
    std::string_view trimmed = trimView(raw);
    if (trimmed.empty()) return;
    if (trimmed.size() >= 4 && trimmed.substr(0, 2) == "<<") {
        size_t close = trimmed.find(">>");
        if (close != std::string_view::npos) {
            members.stereotype =
                "<<" + std::string(trimView(trimmed.substr(2, close - 2))) +
                ">>";
            return;
        }
    }
    std::string display = displayGenerics(cleanLabel(trimmed));
    if (display.find('(') != std::string::npos) {
        members.methods.push_back(std::move(display));
    } else {
        members.attributes.push_back(std::move(display));
    }
}

struct RelationToken {
    Marker marker = Marker::None;
    size_t length = 0;
};

RelationToken leftRelationMarker(std::string_view text) {
    if (text.size() >= 2 && text.substr(text.size() - 2) == "<|") {
        return {Marker::HollowTriangle, 2};
    }
    if (!text.empty()) {
        char last = text.back();
        if (last == '*') return {Marker::FilledDiamond, 1};
        if (last == 'o') return {Marker::HollowDiamond, 1};
        if (last == '<') return {Marker::OpenArrow, 1};
    }
    return {};
}

RelationToken rightRelationMarker(std::string_view text) {
    if (text.size() >= 2 && text.substr(0, 2) == "|>") {
        return {Marker::HollowTriangle, 2};
    }
    if (!text.empty()) {
        char first = text.front();
        if (first == '*') return {Marker::FilledDiamond, 1};
        if (first == 'o') return {Marker::HollowDiamond, 1};
        if (first == '>') return {Marker::OpenArrow, 1};
    }
    return {};
}

// Strips one optional trailing quoted string ("1", "many") off the end
std::string takeTrailingQuoted(std::string_view& text) {
    text = trimView(text);
    if (text.empty() || text.back() != '"') return {};
    size_t open = text.rfind('"', text.size() - 2);
    if (open == std::string_view::npos) return {};
    std::string value(text.substr(open + 1, text.size() - open - 2));
    text = trimView(text.substr(0, open));
    return value;
}

std::string takeLeadingQuoted(std::string_view& text) {
    text = trimView(text);
    if (text.empty() || text.front() != '"') return {};
    size_t close = text.find('"', 1);
    if (close == std::string_view::npos) return {};
    std::string value(text.substr(1, close - 1));
    text = trimView(text.substr(close + 1));
    return value;
}

}  // namespace

Built buildClass(std::string_view source, const Measure& measure,
                 float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    GraphNodes graph;
    graph.diagram.direction = mermaid::Direction::TopToBottom;
    std::map<size_t, ClassMembers> members;
    std::vector<EdgeDecor> decors;
    size_t openBlock = SIZE_MAX;  // class whose { } block is open

    auto stripCss = [](std::string_view id) {
        size_t css = id.find(":::");
        return css == std::string_view::npos ? id : trimView(id.substr(0, css));
    };
    auto ensureClass = [&](std::string_view id) {
        std::string display = displayGenerics(std::string(stripCss(id)));
        return graph.ensure(display, display);
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "classDiagram") &&
                !startsWithWord(line, "classDiagram-v2")) {
                result.error = "Expected classDiagram header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        if (openBlock != SIZE_MAX) {
            if (line == "}") {
                openBlock = SIZE_MAX;
            } else {
                addClassMember(members[openBlock], line);
            }
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "direction", &rest)) {
            if (rest == "LR") {
                graph.diagram.direction = mermaid::Direction::LeftToRight;
            } else if (rest == "RL") {
                graph.diagram.direction = mermaid::Direction::RightToLeft;
            } else if (rest == "BT") {
                graph.diagram.direction = mermaid::Direction::BottomToTop;
            } else {
                graph.diagram.direction = mermaid::Direction::TopToBottom;
            }
            continue;
        }
        if (startsWithWord(line, "click", &rest) ||
            startsWithWord(line, "callback", &rest) ||
            startsWithWord(line, "link", &rest) ||
            startsWithWord(line, "style", &rest) ||
            startsWithWord(line, "classDef", &rest) ||
            startsWithWord(line, "cssClass", &rest)) {
            continue;  // interactivity/styling metadata, nothing to draw
        }
        if (startsWithWord(line, "note", &rest)) {
            result.error = "Class notes are not supported";
            return result;
        }
        if (startsWithWord(line, "namespace", &rest)) {
            result.error = "Namespaces are not supported";
            return result;
        }
        if (line.substr(0, 2) == "<<") {
            size_t close = line.find(">>");
            if (close != std::string_view::npos) {
                std::string_view name = trimView(line.substr(close + 2));
                if (!name.empty()) {
                    size_t node = ensureClass(name);
                    members[node].stereotype =
                        "<<" +
                        std::string(trimView(line.substr(2, close - 2))) +
                        ">>";
                    continue;
                }
            }
            result.error = "Unsupported annotation";
            return result;
        }
        if (startsWithWord(line, "class", &rest)) {
            std::string_view spec = rest;
            bool opensBlock = false;
            if (!spec.empty() && spec.back() == '{') {
                opensBlock = true;
                spec = trimView(spec.substr(0, spec.size() - 1));
            }
            size_t node = ensureClass(spec);
            if (opensBlock) openBlock = node;
            continue;
        }

        // Relation: A [<|*o<] -- / .. [|>*o>] B, quoted multiplicities,
        // optional : label
        {
            size_t linePos = std::string_view::npos;
            bool dashed = false;
            for (size_t i = 0; i + 1 < line.size(); i++) {
                if (line[i] == '-' && line[i + 1] == '-') {
                    linePos = i;
                    dashed = false;
                    break;
                }
                if (line[i] == '.' && line[i + 1] == '.') {
                    linePos = i;
                    dashed = true;
                    break;
                }
            }
            if (linePos != std::string_view::npos) {
                std::string_view leftPart = line.substr(0, linePos);
                std::string_view rightPart = line.substr(linePos + 2);
                RelationToken leftMarker = leftRelationMarker(leftPart);
                leftPart = leftPart.substr(0, leftPart.size() -
                                                  leftMarker.length);
                RelationToken rightMarker = rightRelationMarker(rightPart);
                rightPart = rightPart.substr(rightMarker.length);

                std::string label;
                size_t colon = rightPart.find(':');
                if (colon != std::string_view::npos) {
                    label = cleanLabel(trimView(rightPart.substr(colon + 1)));
                    rightPart = rightPart.substr(0, colon);
                }
                std::string leftMult = takeTrailingQuoted(leftPart);
                std::string rightMult = takeLeadingQuoted(rightPart);
                std::string_view leftId = trimView(leftPart);
                std::string_view rightId = trimView(rightPart);
                if (leftId.empty() || rightId.empty()) {
                    result.error = "Relation needs two classes";
                    return result;
                }
                mermaid::Edge edge;
                edge.from = ensureClass(leftId);
                edge.to = ensureClass(rightId);
                edge.label = label;
                graph.diagram.edges.push_back(std::move(edge));
                EdgeDecor decor;
                decor.atFrom = leftMarker.marker;
                decor.atTo = rightMarker.marker;
                decor.dashed = dashed;
                decor.labelFrom = leftMult;
                decor.labelTo = rightMult;
                decors.push_back(std::move(decor));
                continue;
            }
        }

        // Member via colon: Name : +member
        size_t colon = line.find(':');
        if (colon != std::string_view::npos && colon > 0) {
            size_t node = ensureClass(trimView(line.substr(0, colon)));
            addClassMember(members[node], line.substr(colon + 1));
            continue;
        }

        result.error = "Unsupported class statement";
        return result;
    }

    if (graph.diagram.nodes.empty()) {
        result.error = "No classes";
        return result;
    }

    // --- measure ---
    TextStyle nameStyle;
    nameStyle.bold = true;
    TextStyle stereoStyle;
    stereoStyle.italic = true;
    stereoStyle.scale = 0.85f;
    TextStyle memberStyle;
    memberStyle.scale = 0.9f;
    float padX = 12.0f;
    float memberLineGap = 3.0f;

    struct MeasuredClass {
        Size name;
        Size stereotype;
        float attrHeight = 0.0f;
        float methodHeight = 0.0f;
    };
    std::vector<MeasuredClass> measured(graph.diagram.nodes.size());
    std::vector<mermaid::Size> sizes(graph.diagram.nodes.size());
    for (size_t i = 0; i < graph.diagram.nodes.size(); i++) {
        const ClassMembers* classMembers = nullptr;
        auto found = members.find(i);
        if (found != members.end()) classMembers = &found->second;

        MeasuredClass& node = measured[i];
        node.name = measure(graph.diagram.nodes[i].label, nameStyle, 0.0f);
        float width = node.name.w;
        float height = node.name.h + 12.0f * scale;
        if (classMembers && !classMembers->stereotype.empty()) {
            node.stereotype =
                measure(classMembers->stereotype, stereoStyle, 0.0f);
            width = std::max(width, node.stereotype.w);
            height += node.stereotype.h;
        }
        if (classMembers) {
            for (const auto& attribute : classMembers->attributes) {
                Size size = measure(attribute, memberStyle, 0.0f);
                width = std::max(width, size.w);
                node.attrHeight += size.h + memberLineGap * scale;
            }
            for (const auto& method : classMembers->methods) {
                Size size = measure(method, memberStyle, 0.0f);
                width = std::max(width, size.w);
                node.methodHeight += size.h + memberLineGap * scale;
            }
            if (!classMembers->attributes.empty() ||
                !classMembers->methods.empty()) {
                // both compartments render (possibly thin) once any exist
                height += node.attrHeight + node.methodHeight +
                          16.0f * scale;
            }
        }
        sizes[i] = {std::max(74.0f * scale, width + padX * 2.0f * scale),
                    height};
    }

    mermaid::Layout layout = mermaid::layout(
        acyclicForRanking(graph.diagram), sizes, kNodeGap * scale,
        (kRankGap + 12.0f) * scale);
    if (layout.nodes.size() != graph.diagram.nodes.size()) {
        result.error = "Layout failed";
        return result;
    }

    float maxRight = layout.width;
    float maxBottom = layout.height;
    auto routed =
        routeEdges(graph.diagram, layout, scale, maxRight, maxBottom);
    emitEdges(result.prims, graph.diagram, routed, decors, measure, scale);

    for (size_t i = 0; i < graph.diagram.nodes.size(); i++) {
        const auto& rect = layout.nodes[i];
        const ClassMembers* classMembers = nullptr;
        auto found = members.find(i);
        if (found != members.end()) classMembers = &found->second;
        const MeasuredClass& node = measured[i];

        Prim box;
        box.type = PrimType::RoundRect;
        box.radius = 4.0f * scale;
        box.x1 = rect.left;
        box.y1 = rect.top;
        box.x2 = rect.right;
        box.y2 = rect.bottom;
        box.fill = Role::Fill;
        box.stroke = Role::Stroke;
        box.strokeWidth = 1.5f * scale;
        result.prims.push_back(box);

        float y = rect.top + 6.0f * scale;
        if (classMembers && !classMembers->stereotype.empty()) {
            Prim stereo;
            stereo.type = PrimType::Text;
            stereo.text = classMembers->stereotype;
            stereo.style = stereoStyle;
            stereo.fill = Role::Muted;
            stereo.x1 = rect.left;
            stereo.y1 = y;
            stereo.x2 = rect.right;
            stereo.y2 = y + node.stereotype.h;
            result.prims.push_back(std::move(stereo));
            y += node.stereotype.h;
        }
        Prim name;
        name.type = PrimType::Text;
        name.text = graph.diagram.nodes[i].label;
        name.style = nameStyle;
        name.fill = Role::Text;
        name.x1 = rect.left;
        name.y1 = y;
        name.x2 = rect.right;
        name.y2 = y + node.name.h;
        result.prims.push_back(std::move(name));
        y += node.name.h + 6.0f * scale;

        bool hasMembers =
            classMembers && (!classMembers->attributes.empty() ||
                             !classMembers->methods.empty());
        if (hasMembers) {
            auto compartment = [&](const std::vector<std::string>& entries,
                                   float bodyHeight) {
                Prim separator;
                separator.type = PrimType::Line;
                separator.x1 = rect.left;
                separator.y1 = y;
                separator.x2 = rect.right;
                separator.y2 = y;
                separator.stroke = Role::Stroke;
                separator.strokeWidth = 1.0f * scale;
                result.prims.push_back(separator);
                y += 4.0f * scale;
                if (!entries.empty()) {
                    std::string joined;
                    for (const auto& entry : entries) {
                        if (!joined.empty()) joined += '\n';
                        joined += entry;
                    }
                    Prim body;
                    body.type = PrimType::Text;
                    body.text = joined;
                    body.style = memberStyle;
                    body.fill = Role::Text;
                    body.alignH = -1;
                    body.alignV = -1;
                    body.x1 = rect.left + padX * scale;
                    body.y1 = y;
                    body.x2 = rect.right - padX * scale;
                    body.y2 = y + bodyHeight;
                    result.prims.push_back(std::move(body));
                }
                y += bodyHeight + 4.0f * scale;
            };
            compartment(classMembers->attributes, node.attrHeight);
            compartment(classMembers->methods, node.methodHeight);
        }
    }

    result.width = maxRight + 8.0f * scale;
    result.height = maxBottom + 8.0f * scale;
    normalizeLeft(result);
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
