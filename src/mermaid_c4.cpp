// Native Mermaid requirement, C4, and architecture diagrams.
//
// requirement: typed boxes (<<Requirement>> / <<Element>>) linked by dashed
//              labeled relations, laid out in layers by relation depth.
// C4:          Person/System/Container/Component boxes in their standard
//              palette, nested boundaries, straight labeled relations.
// architecture: services with line-art icons placed on an integer grid by
//              the side constraints of their edges, grouped by dashed boxes.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

namespace {

// name(arg, "quoted, arg", ...) -> args; returns false without a paren list
bool splitArgs(std::string_view rest, std::vector<std::string>& args) {
    rest = trimView(rest);
    if (rest.empty() || rest.front() != '(') return false;
    size_t depth = 0;
    bool inQuote = false;
    std::string current;
    for (size_t i = 0; i < rest.size(); i++) {
        char c = rest[i];
        if (inQuote) {
            if (c == '"') inQuote = false;
            else current += c;
            continue;
        }
        if (c == '"') {
            inQuote = true;
            continue;
        }
        if (c == '(') {
            depth++;
            if (depth == 1) continue;
        }
        if (c == ')') {
            depth--;
            if (depth == 0) {
                args.push_back(std::string(trimView(current)));
                return true;
            }
        }
        if (c == ',' && depth == 1) {
            args.push_back(std::string(trimView(current)));
            current.clear();
            continue;
        }
        current += c;
    }
    return false;
}

Point clipRect(float cx, float cy, float ox, float oy, float x1, float y1,
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

// --------------------------------------------------------------------------
// Requirement diagrams
// --------------------------------------------------------------------------

namespace {

struct ReqNode {
    std::string name;
    std::string typeName;   // shown as <<typeName>>
    std::vector<std::string> bodyLines;
    int rank = 0;
    float x = 0, y = 0, width = 0, height = 0;
};

struct ReqEdge {
    size_t from = 0;
    size_t to = 0;
    std::string label;
};

const char* kReqTypes[] = {
    "requirement",         "functionalRequirement", "interfaceRequirement",
    "performanceRequirement", "physicalRequirement", "designConstraint",
    "element",
};

const char* kReqRelations[] = {
    "contains", "copies", "derives", "satisfies", "verifies", "refines",
    "traces",
};

}  // namespace

Built buildRequirement(std::string_view source, const Measure& measure,
                       float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::vector<ReqNode> nodes;
    std::vector<ReqEdge> edges;
    auto findNode = [&](std::string_view name) -> size_t {
        for (size_t i = 0; i < nodes.size(); i++) {
            if (nodes[i].name == name) return i;
        }
        return SIZE_MAX;
    };

    bool sawHeader = false;
    size_t openNode = SIZE_MAX;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "requirementDiagram")) {
                result.error = "Expected requirementDiagram header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        if (openNode != SIZE_MAX) {
            if (line == "}") {
                openNode = SIZE_MAX;
                continue;
            }
            size_t colon = line.find(':');
            if (colon == std::string_view::npos) {
                result.error = "Expected key: value";
                return result;
            }
            std::string key(trimView(line.substr(0, colon)));
            std::string value(trimView(line.substr(colon + 1)));
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            const char* shown = nullptr;
            if (key == "id") shown = "Id";
            else if (key == "text") shown = "Text";
            else if (key == "risk") shown = "Risk";
            else if (key == "verifymethod") shown = "Verification";
            else if (key == "type") shown = "Type";
            else if (key == "docref") shown = "Doc Ref";
            if (!shown) {
                result.error = "Unknown requirement field";
                return result;
            }
            nodes[openNode].bodyLines.push_back(std::string(shown) + ": " +
                                                value);
            continue;
        }

        // Type name { ... } opens a node
        bool matched = false;
        for (const char* type : kReqTypes) {
            std::string_view rest;
            if (!startsWithWord(line, type, &rest)) continue;
            matched = true;
            bool sameLineBrace = !rest.empty() && rest.back() == '{';
            if (sameLineBrace) rest = trimView(rest.substr(0, rest.size() - 1));
            ReqNode node;
            node.name = std::string(rest);
            node.typeName = type;
            if (node.name.empty()) {
                result.error = "Requirement without a name";
                return result;
            }
            nodes.push_back(std::move(node));
            openNode = nodes.size() - 1;
            break;
        }
        if (matched) continue;

        // source - relation -> destination
        size_t dash = line.find(" - ");
        size_t arrow = line.find("-> ");
        if (dash != std::string_view::npos && arrow != std::string_view::npos &&
            arrow > dash) {
            std::string_view from = trimView(line.substr(0, dash));
            std::string_view relation =
                trimView(line.substr(dash + 3, arrow - dash - 3));
            if (!relation.empty() && relation.back() == '-') {
                relation = trimView(relation.substr(0, relation.size() - 1));
            }
            std::string_view to = trimView(line.substr(arrow + 3));
            bool known = false;
            for (const char* name : kReqRelations) {
                if (relation == name) known = true;
            }
            size_t fromIndex = findNode(from);
            size_t toIndex = findNode(to);
            if (!known || fromIndex == SIZE_MAX || toIndex == SIZE_MAX) {
                result.error = "Unsupported requirement relation";
                return result;
            }
            ReqEdge edge;
            edge.from = fromIndex;
            edge.to = toIndex;
            edge.label = "<<" + std::string(relation) + ">>";
            edges.push_back(std::move(edge));
            continue;
        }
        result.error = "Unsupported requirement statement";
        return result;
    }
    if (nodes.empty()) {
        result.error = "Empty requirement diagram";
        return result;
    }

    // Layered by relation depth
    for (size_t pass = 0; pass <= nodes.size(); pass++) {
        bool changed = false;
        for (const auto& edge : edges) {
            if (nodes[edge.to].rank < nodes[edge.from].rank + 1) {
                nodes[edge.to].rank = nodes[edge.from].rank + 1;
                changed = true;
            }
        }
        if (!changed) break;
        if (pass == nodes.size()) {
            result.error = "Requirement relations form a cycle";
            return result;
        }
    }

    TextStyle stereotypeStyle;
    stereotypeStyle.italic = true;
    stereotypeStyle.scale = 0.8f;
    TextStyle nameStyle;
    nameStyle.bold = true;
    TextStyle bodyStyle;
    bodyStyle.scale = 0.85f;

    float padX = 12.0f * scale;
    float padY = 8.0f * scale;
    float bodyWrap = 220.0f * scale;
    for (auto& node : nodes) {
        std::string stereotype = "<<" + node.typeName + ">>";
        Size stereoSize = measure(stereotype, stereotypeStyle, 0.0f);
        Size nameSize = measure(node.name, nameStyle, 0.0f);
        float width = std::max(stereoSize.w, nameSize.w);
        float height = stereoSize.h + nameSize.h + 6.0f * scale;
        for (const auto& lineText : node.bodyLines) {
            Size size = measure(lineText, bodyStyle, bodyWrap);
            width = std::max(width, std::min(size.w, bodyWrap));
            height += size.h + 2.0f * scale;
        }
        node.width = std::max(170.0f * scale, width + padX * 2.0f);
        node.height = height + padY * 2.0f + 4.0f * scale;
    }

    int maxRank = 0;
    for (const auto& node : nodes) maxRank = std::max(maxRank, node.rank);
    float nodeGap = 30.0f * scale;
    float rankGap = 64.0f * scale;
    std::vector<float> rowWidths(maxRank + 1, 0.0f);
    std::vector<float> rowHeights(maxRank + 1, 0.0f);
    for (const auto& node : nodes) {
        rowWidths[node.rank] += node.width + nodeGap;
        rowHeights[node.rank] = std::max(rowHeights[node.rank], node.height);
    }
    float diagramWidth = 0.0f;
    for (float w : rowWidths) diagramWidth = std::max(diagramWidth, w - nodeGap);

    std::vector<float> rowCursor(maxRank + 1);
    std::vector<float> rowTop(maxRank + 1);
    float y = 4.0f * scale;
    for (int r = 0; r <= maxRank; r++) {
        rowCursor[r] = (diagramWidth - (rowWidths[r] - nodeGap)) * 0.5f;
        rowTop[r] = y;
        y += rowHeights[r] + rankGap;
    }
    for (auto& node : nodes) {
        node.x = rowCursor[node.rank];
        node.y = rowTop[node.rank];
        rowCursor[node.rank] += node.width + nodeGap;
    }

    // Edges first, boxes cover their ends
    std::vector<Prim> texts;
    for (const auto& edge : edges) {
        const auto& from = nodes[edge.from];
        const auto& to = nodes[edge.to];
        float fx = from.x + from.width * 0.5f;
        float fy = from.y + from.height * 0.5f;
        float tx = to.x + to.width * 0.5f;
        float ty = to.y + to.height * 0.5f;
        Point start = clipRect(fx, fy, tx, ty, from.x, from.y,
                               from.x + from.width, from.y + from.height);
        Point end = clipRect(tx, ty, fx, fy, to.x, to.y, to.x + to.width,
                             to.y + to.height);
        Prim line;
        line.type = PrimType::Line;
        line.x1 = start.x;
        line.y1 = start.y;
        line.x2 = end.x;
        line.y2 = end.y;
        line.stroke = Role::Muted;
        line.strokeWidth = 1.3f * scale;
        line.dashed = true;
        line.openArrow = true;
        result.prims.push_back(std::move(line));

        TextStyle labelStyle;
        labelStyle.scale = 0.78f;
        labelStyle.italic = true;
        Size size = measure(edge.label, labelStyle, 0.0f);
        float cx = (start.x + end.x) * 0.5f;
        float cy = (start.y + end.y) * 0.5f;
        Prim chip;
        chip.type = PrimType::Rect;
        chip.x1 = cx - size.w * 0.5f - 4.0f * scale;
        chip.y1 = cy - size.h * 0.5f - 1.0f * scale;
        chip.x2 = cx + size.w * 0.5f + 4.0f * scale;
        chip.y2 = cy + size.h * 0.5f + 1.0f * scale;
        chip.fill = Role::Background;
        result.prims.push_back(chip);
        Prim label;
        label.type = PrimType::Text;
        label.text = edge.label;
        label.style = labelStyle;
        label.fill = Role::Muted;
        label.x1 = chip.x1;
        label.y1 = chip.y1;
        label.x2 = chip.x2;
        label.y2 = chip.y2;
        texts.push_back(std::move(label));
    }

    for (const auto& node : nodes) {
        Prim box;
        box.type = PrimType::Rect;
        box.x1 = node.x;
        box.y1 = node.y;
        box.x2 = node.x + node.width;
        box.y2 = node.y + node.height;
        box.fill = Role::Fill;
        box.stroke = Role::Stroke;
        box.strokeWidth = 1.4f * scale;
        result.prims.push_back(box);

        float textY = node.y + padY;
        std::string stereotype = "<<" + node.typeName + ">>";
        Size stereoSize = measure(stereotype, stereotypeStyle, 0.0f);
        Prim stereo;
        stereo.type = PrimType::Text;
        stereo.text = stereotype;
        stereo.style = stereotypeStyle;
        stereo.fill = Role::Muted;
        stereo.x1 = node.x + padX;
        stereo.y1 = textY;
        stereo.x2 = node.x + node.width - padX;
        stereo.y2 = textY + stereoSize.h;
        texts.push_back(std::move(stereo));
        textY += stereoSize.h;

        Size nameSize = measure(node.name, nameStyle, 0.0f);
        Prim nameText;
        nameText.type = PrimType::Text;
        nameText.text = node.name;
        nameText.style = nameStyle;
        nameText.fill = Role::Text;
        nameText.x1 = node.x + padX;
        nameText.y1 = textY;
        nameText.x2 = node.x + node.width - padX;
        nameText.y2 = textY + nameSize.h;
        texts.push_back(std::move(nameText));
        textY += nameSize.h + 6.0f * scale;

        // Separator under the header
        Prim rule;
        rule.type = PrimType::Line;
        rule.x1 = node.x;
        rule.y1 = textY - 3.0f * scale;
        rule.x2 = node.x + node.width;
        rule.y2 = rule.y1;
        rule.stroke = Role::Stroke;
        rule.strokeWidth = 1.0f * scale;
        result.prims.push_back(std::move(rule));

        for (const auto& lineText : node.bodyLines) {
            Size size = measure(lineText, bodyStyle,
                                node.width - padX * 2.0f);
            Prim body;
            body.type = PrimType::Text;
            body.text = lineText;
            body.style = bodyStyle;
            body.fill = Role::Text;
            body.alignH = -1;
            body.alignV = -1;
            body.x1 = node.x + padX;
            body.y1 = textY;
            body.x2 = node.x + node.width - padX;
            body.y2 = textY + size.h;
            texts.push_back(std::move(body));
            textY += size.h + 2.0f * scale;
        }
    }
    for (auto& text : texts) result.prims.push_back(std::move(text));

    result.width = diagramWidth;
    result.height = rowTop[maxRank] + rowHeights[maxRank] + 8.0f * scale;
    result.ok = true;
    normalizeLeft(result);
    return result;
}

// --------------------------------------------------------------------------
// C4 diagrams
// --------------------------------------------------------------------------

namespace {

enum class C4Kind { Person, System, Container, Component };

struct C4Element {
    std::string alias;
    std::string label;
    std::string techn;
    std::string descr;
    C4Kind kind = C4Kind::System;
    bool external = false;
    bool db = false;
    bool queue = false;
    float x = 0, y = 0, width = 0, height = 0;
};

struct C4Boundary {
    std::string label;
    std::string type;
    std::vector<size_t> elements;    // indices into elements
    std::vector<size_t> children;    // indices into boundaries
    size_t parent = SIZE_MAX;
    float x = 0, y = 0, width = 0, height = 0;
};

struct C4Rel {
    std::string from;
    std::string to;
    std::string label;
    std::string techn;
    bool bidirectional = false;
};

struct C4Colors {
    float bgR, bgG, bgB;
    float fgR, fgG, fgB;
};

C4Colors c4Palette(const C4Element& element) {
    if (element.kind == C4Kind::Person) {
        return element.external
                   ? C4Colors{0.41f, 0.41f, 0.41f, 1.0f, 1.0f, 1.0f}
                   : C4Colors{0.031f, 0.259f, 0.482f, 1.0f, 1.0f, 1.0f};
    }
    if (element.kind == C4Kind::Container) {
        return element.external
                   ? C4Colors{0.70f, 0.70f, 0.70f, 0.10f, 0.10f, 0.10f}
                   : C4Colors{0.263f, 0.553f, 0.835f, 1.0f, 1.0f, 1.0f};
    }
    if (element.kind == C4Kind::Component) {
        return element.external
                   ? C4Colors{0.80f, 0.80f, 0.80f, 0.10f, 0.10f, 0.10f}
                   : C4Colors{0.522f, 0.733f, 0.941f, 0.08f, 0.08f, 0.08f};
    }
    return element.external
               ? C4Colors{0.60f, 0.60f, 0.60f, 1.0f, 1.0f, 1.0f}
               : C4Colors{0.067f, 0.408f, 0.741f, 1.0f, 1.0f, 1.0f};
}

constexpr float kC4ElementWidth = 200.0f;
constexpr int kC4PerRow = 3;

}  // namespace

Built buildC4(std::string_view source, const Measure& measure, float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title;
    std::vector<C4Element> elements;
    std::vector<C4Boundary> boundaries;
    boundaries.push_back({});  // implicit root
    std::vector<size_t> stack = {0};
    std::vector<C4Rel> rels;

    auto keyword = [](std::string_view line) {
        size_t end = 0;
        while (end < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[end])) ||
                line[end] == '_')) {
            end++;
        }
        return std::string(line.substr(0, end));
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            sawHeader = true;  // C4Context / C4Container / ... all accepted
            continue;
        }
        if (line == "}") {
            if (stack.size() <= 1) {
                result.error = "Unmatched }";
                return result;
            }
            stack.pop_back();
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "title", &rest)) {
            title = cleanLabel(rest);
            continue;
        }
        std::string word = keyword(line);
        std::string lower = word;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        std::string_view tail = line.substr(word.size());

        // Style and layout tweaks do not affect the native rendering
        if (lower.rfind("update", 0) == 0 || lower.rfind("show_", 0) == 0 ||
            lower == "show_legend" || lower.rfind("layout", 0) == 0) {
            continue;
        }

        bool opensScope = false;
        std::string_view scopeCheck = trimView(tail);
        if (!scopeCheck.empty() && scopeCheck.back() == '{') {
            opensScope = true;
        }

        std::vector<std::string> args;
        bool hasArgs = splitArgs(tail, args);

        // Boundaries and deployment nodes nest
        if (lower == "boundary" || lower == "enterprise_boundary" ||
            lower == "system_boundary" || lower == "container_boundary" ||
            lower == "deployment_node" || lower == "node" ||
            lower == "node_l" || lower == "node_r") {
            if (!hasArgs || args.size() < 2) {
                result.error = "Bad boundary";
                return result;
            }
            C4Boundary boundary;
            boundary.label = cleanLabel(args[1]);
            if (lower == "enterprise_boundary") boundary.type = "enterprise";
            else if (lower == "system_boundary") boundary.type = "system";
            else if (lower == "container_boundary") boundary.type = "container";
            else if (lower == "deployment_node" || lower == "node" ||
                     lower == "node_l" || lower == "node_r") {
                boundary.type = args.size() > 2 ? args[2] : "node";
            } else if (args.size() > 2) {
                boundary.type = args[2];
            }
            boundary.parent = stack.back();
            boundaries.push_back(std::move(boundary));
            boundaries[stack.back()].children.push_back(
                boundaries.size() - 1);
            stack.push_back(boundaries.size() - 1);
            if (!opensScope) stack.pop_back();  // empty boundary
            continue;
        }

        if (lower.rfind("rel", 0) == 0 || lower.rfind("birel", 0) == 0) {
            if (!hasArgs || args.size() < 3) {
                result.error = "Bad relation";
                return result;
            }
            C4Rel rel;
            rel.from = args[0];
            rel.to = args[1];
            rel.label = cleanLabel(args[2]);
            if (args.size() > 3) rel.techn = cleanLabel(args[3]);
            rel.bidirectional = lower.rfind("birel", 0) == 0;
            rels.push_back(std::move(rel));
            continue;
        }

        // Element constructs
        C4Element element;
        bool isElement = true;
        if (lower.rfind("person", 0) == 0) {
            element.kind = C4Kind::Person;
        } else if (lower.rfind("systemdb", 0) == 0) {
            element.kind = C4Kind::System;
            element.db = true;
        } else if (lower.rfind("systemqueue", 0) == 0) {
            element.kind = C4Kind::System;
            element.queue = true;
        } else if (lower.rfind("system", 0) == 0) {
            element.kind = C4Kind::System;
        } else if (lower.rfind("containerdb", 0) == 0) {
            element.kind = C4Kind::Container;
            element.db = true;
        } else if (lower.rfind("containerqueue", 0) == 0) {
            element.kind = C4Kind::Container;
            element.queue = true;
        } else if (lower.rfind("container", 0) == 0) {
            element.kind = C4Kind::Container;
        } else if (lower.rfind("componentdb", 0) == 0) {
            element.kind = C4Kind::Component;
            element.db = true;
        } else if (lower.rfind("componentqueue", 0) == 0) {
            element.kind = C4Kind::Component;
            element.queue = true;
        } else if (lower.rfind("component", 0) == 0) {
            element.kind = C4Kind::Component;
        } else {
            isElement = false;
        }
        if (!isElement) {
            result.error = "Unsupported C4 statement";
            return result;
        }
        if (!hasArgs || args.size() < 2) {
            result.error = "Bad C4 element";
            return result;
        }
        element.external =
            lower.find("_ext") != std::string::npos;
        element.alias = args[0];
        element.label = cleanLabel(args[1]);
        bool hasTechn = element.kind == C4Kind::Container ||
                        element.kind == C4Kind::Component;
        if (hasTechn) {
            if (args.size() > 2) element.techn = cleanLabel(args[2]);
            if (args.size() > 3) element.descr = cleanLabel(args[3]);
        } else if (args.size() > 2) {
            element.descr = cleanLabel(args[2]);
        }
        elements.push_back(std::move(element));
        boundaries[stack.back()].elements.push_back(elements.size() - 1);
    }
    if (elements.empty()) {
        result.error = "Empty C4 diagram";
        return result;
    }

    TextStyle labelStyle;
    labelStyle.bold = true;
    labelStyle.scale = 0.95f;
    TextStyle technStyle;
    technStyle.scale = 0.75f;
    technStyle.italic = true;
    TextStyle descrStyle;
    descrStyle.scale = 0.8f;

    // Element sizes
    float elementWidth = kC4ElementWidth * scale;
    float wrap = elementWidth - 24.0f * scale;
    for (auto& element : elements) {
        float height = 14.0f * scale;
        if (element.kind == C4Kind::Person) height += 22.0f * scale;
        if (element.db) height += 10.0f * scale;
        height += measure(element.label, labelStyle, wrap).h;
        if (!element.techn.empty()) {
            height +=
                measure("[" + element.techn + "]", technStyle, wrap).h +
                2.0f * scale;
        }
        if (!element.descr.empty()) {
            height += measure(element.descr, descrStyle, wrap).h +
                      4.0f * scale;
        }
        height += 12.0f * scale;
        element.width = elementWidth;
        element.height = std::max(height, 56.0f * scale);
    }

    // Recursive boundary layout: members flow in rows of kC4PerRow
    float memberGapX = 24.0f * scale;
    float memberGapY = 26.0f * scale;
    float boundaryPad = 18.0f * scale;
    float boundaryLabelSpace = 26.0f * scale;

    struct SizeF {
        float w = 0, h = 0;
    };
    std::function<SizeF(size_t)> measureBoundary =
        [&](size_t index) -> SizeF {
        auto& boundary = boundaries[index];
        // Row members: element boxes then child boundaries, in order
        std::vector<SizeF> members;
        for (size_t e : boundary.elements) {
            members.push_back({elements[e].width, elements[e].height});
        }
        for (size_t child : boundary.children) {
            members.push_back(measureBoundary(child));
        }
        float width = 0.0f, height = 0.0f;
        float rowWidth = 0.0f, rowHeight = 0.0f;
        int inRow = 0;
        for (const auto& member : members) {
            if (inRow == kC4PerRow) {
                width = std::max(width, rowWidth - memberGapX);
                height += rowHeight + memberGapY;
                rowWidth = 0.0f;
                rowHeight = 0.0f;
                inRow = 0;
            }
            rowWidth += member.w + memberGapX;
            rowHeight = std::max(rowHeight, member.h);
            inRow++;
        }
        width = std::max(width, rowWidth - memberGapX);
        height += rowHeight;
        SizeF size;
        if (index == 0) {
            size = {width, height};
        } else {
            size = {width + boundaryPad * 2.0f,
                    height + boundaryPad * 2.0f + boundaryLabelSpace};
        }
        boundary.width = size.w;
        boundary.height = size.h;
        return size;
    };

    std::function<void(size_t, float, float)> placeBoundary =
        [&](size_t index, float x, float y) {
        auto& boundary = boundaries[index];
        boundary.x = x;
        boundary.y = y;
        float innerX = index == 0 ? x : x + boundaryPad;
        float innerY = index == 0 ? y : y + boundaryPad;
        float innerWidth =
            index == 0 ? boundary.width : boundary.width - boundaryPad * 2.0f;

        struct Member {
            bool isElement;
            size_t index;
            float w, h;
        };
        std::vector<Member> members;
        for (size_t e : boundary.elements) {
            members.push_back(
                {true, e, elements[e].width, elements[e].height});
        }
        for (size_t child : boundary.children) {
            members.push_back({false, child, boundaries[child].width,
                               boundaries[child].height});
        }
        float rowTop = innerY;
        size_t start = 0;
        while (start < members.size()) {
            size_t end = std::min(start + kC4PerRow, members.size());
            float rowWidth = 0.0f, rowHeight = 0.0f;
            for (size_t i = start; i < end; i++) {
                rowWidth += members[i].w + memberGapX;
                rowHeight = std::max(rowHeight, members[i].h);
            }
            rowWidth -= memberGapX;
            float cursor = innerX + (innerWidth - rowWidth) * 0.5f;
            for (size_t i = start; i < end; i++) {
                float memberY = rowTop + (rowHeight - members[i].h) * 0.5f;
                if (members[i].isElement) {
                    elements[members[i].index].x = cursor;
                    elements[members[i].index].y = memberY;
                } else {
                    placeBoundary(members[i].index, cursor, memberY);
                }
                cursor += members[i].w + memberGapX;
            }
            rowTop += rowHeight + memberGapY;
            start = end;
        }
    };

    SizeF rootSize = measureBoundary(0);
    float top = 4.0f * scale;
    float diagramWidth = rootSize.w;
    if (!title.empty()) {
        TextStyle titleStyle;
        titleStyle.bold = true;
        titleStyle.scale = 1.1f;
        Size titleSize = measure(title, titleStyle, 0.0f);
        diagramWidth = std::max(diagramWidth, titleSize.w);
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = 0;
        text.y1 = top;
        text.x2 = diagramWidth;
        text.y2 = top + titleSize.h;
        result.prims.push_back(std::move(text));
        top += titleSize.h + 14.0f * scale;
    }
    placeBoundary(0, (diagramWidth - rootSize.w) * 0.5f, top);

    std::vector<Prim> texts;

    // Boundary frames under everything else
    for (size_t i = 1; i < boundaries.size(); i++) {
        const auto& boundary = boundaries[i];
        Prim frame;
        frame.type = PrimType::Rect;
        frame.x1 = boundary.x;
        frame.y1 = boundary.y;
        frame.x2 = boundary.x + boundary.width;
        frame.y2 = boundary.y + boundary.height;
        frame.stroke = Role::Muted;
        frame.strokeWidth = 1.2f * scale;
        frame.dashed = true;
        result.prims.push_back(std::move(frame));

        TextStyle boundaryStyle;
        boundaryStyle.bold = true;
        boundaryStyle.scale = 0.85f;
        std::string text = boundary.label;
        if (!boundary.type.empty()) text += " [" + boundary.type + "]";
        Size size = measure(text, boundaryStyle, boundary.width);
        Prim label;
        label.type = PrimType::Text;
        label.text = text;
        label.style = boundaryStyle;
        label.fill = Role::Muted;
        label.alignH = -1;
        label.x1 = boundary.x + 8.0f * scale;
        label.y1 = boundary.y + boundary.height - size.h - 4.0f * scale;
        label.x2 = boundary.x + boundary.width - 8.0f * scale;
        label.y2 = boundary.y + boundary.height - 4.0f * scale;
        texts.push_back(std::move(label));
    }

    // Relations under the element boxes
    auto findElement = [&](const std::string& alias) -> C4Element* {
        for (auto& element : elements) {
            if (element.alias == alias) return &element;
        }
        return nullptr;
    };
    for (const auto& rel : rels) {
        C4Element* from = findElement(rel.from);
        C4Element* to = findElement(rel.to);
        if (!from || !to) {
            result.error = "Unknown alias in relation";
            return result;
        }
        float fx = from->x + from->width * 0.5f;
        float fy = from->y + from->height * 0.5f;
        float tx = to->x + to->width * 0.5f;
        float ty = to->y + to->height * 0.5f;
        Point start = clipRect(fx, fy, tx, ty, from->x, from->y,
                               from->x + from->width, from->y + from->height);
        Point end = clipRect(tx, ty, fx, fy, to->x, to->y, to->x + to->width,
                             to->y + to->height);
        Prim line;
        line.type = PrimType::Line;
        line.x1 = start.x;
        line.y1 = start.y;
        line.x2 = end.x;
        line.y2 = end.y;
        line.stroke = Role::Muted;
        line.strokeWidth = 1.3f * scale;
        line.dashed = true;
        line.arrow = true;
        result.prims.push_back(line);
        if (rel.bidirectional) {
            Prim back = line;
            std::swap(back.x1, back.x2);
            std::swap(back.y1, back.y2);
            result.prims.push_back(std::move(back));
        }

        if (!rel.label.empty()) {
            TextStyle relStyle;
            relStyle.scale = 0.78f;
            Size size = measure(rel.label, relStyle, 150.0f * scale);
            float chipW = std::min(size.w, 150.0f * scale);
            std::string technText;
            Size technSize;
            TextStyle technStyle2;
            technStyle2.scale = 0.7f;
            technStyle2.italic = true;
            if (!rel.techn.empty()) {
                technText = "[" + rel.techn + "]";
                technSize = measure(technText, technStyle2, 150.0f * scale);
                chipW = std::max(chipW, technSize.w);
            }
            float cx = (start.x + end.x) * 0.5f;
            float cy = (start.y + end.y) * 0.5f;
            float chipH = size.h + technSize.h;
            Prim chip;
            chip.type = PrimType::Rect;
            chip.x1 = cx - chipW * 0.5f - 4.0f * scale;
            chip.y1 = cy - chipH * 0.5f - 2.0f * scale;
            chip.x2 = cx + chipW * 0.5f + 4.0f * scale;
            chip.y2 = cy + chipH * 0.5f + 2.0f * scale;
            chip.fill = Role::Background;
            result.prims.push_back(chip);
            Prim label;
            label.type = PrimType::Text;
            label.text = rel.label;
            label.style = relStyle;
            label.fill = Role::Text;
            label.x1 = cx - chipW * 0.5f;
            label.y1 = cy - chipH * 0.5f;
            label.x2 = cx + chipW * 0.5f;
            label.y2 = cy - chipH * 0.5f + size.h;
            texts.push_back(std::move(label));
            if (!technText.empty()) {
                Prim techn;
                techn.type = PrimType::Text;
                techn.text = technText;
                techn.style = technStyle2;
                techn.fill = Role::Muted;
                techn.x1 = label.x1;
                techn.y1 = label.y2;
                techn.x2 = label.x2;
                techn.y2 = label.y2 + technSize.h;
                texts.push_back(std::move(techn));
            }
        }
    }

    // Element boxes
    for (const auto& element : elements) {
        C4Colors colors = c4Palette(element);
        float bodyTop = element.y;
        if (element.kind == C4Kind::Person) {
            float headR = 16.0f * scale;
            Prim head;
            head.type = PrimType::Ellipse;
            head.x1 = element.x + element.width * 0.5f - headR;
            head.y1 = element.y;
            head.x2 = element.x + element.width * 0.5f + headR;
            head.y2 = element.y + headR * 2.0f;
            head.fill = Role::Custom;
            head.customR = colors.bgR;
            head.customG = colors.bgG;
            head.customB = colors.bgB;
            result.prims.push_back(std::move(head));
            bodyTop += 22.0f * scale;
        }
        Prim box;
        box.type = PrimType::RoundRect;
        box.radius =
            element.queue ? (element.y + element.height - bodyTop) * 0.5f
                          : 8.0f * scale;
        box.x1 = element.x;
        box.y1 = bodyTop;
        box.x2 = element.x + element.width;
        box.y2 = element.y + element.height;
        box.fill = Role::Custom;
        box.customR = colors.bgR;
        box.customG = colors.bgG;
        box.customB = colors.bgB;
        result.prims.push_back(box);
        if (element.db) {
            // A lid ellipse turns the box into a cylinder
            Prim lid;
            lid.type = PrimType::Ellipse;
            lid.x1 = element.x;
            lid.y1 = bodyTop;
            lid.x2 = element.x + element.width;
            lid.y2 = bodyTop + 14.0f * scale;
            lid.stroke = Role::Custom;
            lid.customR = colors.bgR * 0.75f + colors.fgR * 0.25f;
            lid.customG = colors.bgG * 0.75f + colors.fgG * 0.25f;
            lid.customB = colors.bgB * 0.75f + colors.fgB * 0.25f;
            lid.strokeWidth = 1.4f * scale;
            result.prims.push_back(std::move(lid));
        }

        float textTop = bodyTop + (element.db ? 16.0f : 8.0f) * scale;
        float textLeft = element.x + 12.0f * scale;
        float textRight = element.x + element.width - 12.0f * scale;
        Size labelSize = measure(element.label, labelStyle, wrap);
        Prim label;
        label.type = PrimType::Text;
        label.text = element.label;
        label.style = labelStyle;
        label.fill = Role::Custom;
        label.customR = colors.fgR;
        label.customG = colors.fgG;
        label.customB = colors.fgB;
        label.x1 = textLeft;
        label.y1 = textTop;
        label.x2 = textRight;
        label.y2 = textTop + labelSize.h;
        texts.push_back(std::move(label));
        textTop += labelSize.h;
        if (!element.techn.empty()) {
            std::string technText = "[" + element.techn + "]";
            Size technSize = measure(technText, technStyle, wrap);
            Prim techn;
            techn.type = PrimType::Text;
            techn.text = technText;
            techn.style = technStyle;
            techn.fill = Role::Custom;
            techn.customR = colors.bgR * 0.30f + colors.fgR * 0.70f;
            techn.customG = colors.bgG * 0.30f + colors.fgG * 0.70f;
            techn.customB = colors.bgB * 0.30f + colors.fgB * 0.70f;
            techn.x1 = textLeft;
            techn.y1 = textTop + 2.0f * scale;
            techn.x2 = textRight;
            techn.y2 = textTop + 2.0f * scale + technSize.h;
            texts.push_back(std::move(techn));
            textTop += technSize.h + 2.0f * scale;
        }
        if (!element.descr.empty()) {
            Size descrSize = measure(element.descr, descrStyle, wrap);
            Prim descr;
            descr.type = PrimType::Text;
            descr.text = element.descr;
            descr.style = descrStyle;
            descr.fill = Role::Custom;
            descr.customR = colors.bgR * 0.22f + colors.fgR * 0.78f;
            descr.customG = colors.bgG * 0.22f + colors.fgG * 0.78f;
            descr.customB = colors.bgB * 0.22f + colors.fgB * 0.78f;
            descr.x1 = textLeft;
            descr.y1 = textTop + 4.0f * scale;
            descr.x2 = textRight;
            descr.y2 = textTop + 4.0f * scale + descrSize.h;
            texts.push_back(std::move(descr));
        }
    }
    for (auto& text : texts) result.prims.push_back(std::move(text));

    float bottom = 0.0f;
    for (const auto& element : elements) {
        bottom = std::max(bottom, element.y + element.height);
    }
    for (const auto& boundary : boundaries) {
        bottom = std::max(bottom, boundary.y + boundary.height);
    }
    result.width = diagramWidth;
    result.height = bottom + 8.0f * scale;
    result.ok = true;
    normalizeLeft(result);
    return result;
}

// --------------------------------------------------------------------------
// Architecture diagrams
// --------------------------------------------------------------------------

namespace {

struct ArchNode {
    std::string id;
    std::string icon;
    std::string label;
    std::string group;
    bool junction = false;
    bool placed = false;
    int gridX = 0, gridY = 0;
    float x = 0, y = 0;  // icon box top-left after layout
};

struct ArchGroup {
    std::string id;
    std::string icon;
    std::string label;
    std::string parent;
};

struct ArchEdge {
    std::string from;
    std::string to;
    char fromSide = 'R';
    char toSide = 'L';
    bool arrowFrom = false;
    bool arrowTo = false;
};

constexpr float kArchIcon = 56.0f;
constexpr float kArchCell = 120.0f;

// id(icon)[Label] -> parts; icon and label optional
void parseArchEntity(std::string_view spec, std::string& id,
                     std::string& icon, std::string& label) {
    size_t paren = spec.find('(');
    size_t bracket = spec.find('[');
    size_t idEnd = std::min(paren == std::string_view::npos ? spec.size()
                                                            : paren,
                            bracket == std::string_view::npos ? spec.size()
                                                              : bracket);
    id = std::string(trimView(spec.substr(0, idEnd)));
    if (paren != std::string_view::npos) {
        size_t close = spec.find(')', paren);
        if (close != std::string_view::npos) {
            icon = std::string(trimView(
                spec.substr(paren + 1, close - paren - 1)));
        }
    }
    if (bracket != std::string_view::npos) {
        size_t close = spec.rfind(']');
        if (close != std::string_view::npos && close > bracket) {
            label = cleanLabel(
                spec.substr(bracket + 1, close - bracket - 1));
        }
    }
    if (label.empty()) label = id;
}

}  // namespace

Built buildArchitecture(std::string_view source, const Measure& measure,
                        float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::vector<ArchNode> nodes;
    std::vector<ArchGroup> groups;
    std::vector<ArchEdge> edges;
    auto findNode = [&](std::string_view id) -> ArchNode* {
        for (auto& node : nodes) {
            if (node.id == id) return &node;
        }
        return nullptr;
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "architecture-beta") &&
                !startsWithWord(line, "architecture")) {
                result.error = "Expected architecture header";
                return result;
            }
            sawHeader = true;
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "group", &rest)) {
            ArchGroup group;
            std::string_view spec = rest;
            size_t inWord = rest.rfind(" in ");
            if (inWord != std::string_view::npos) {
                group.parent = std::string(trimView(rest.substr(inWord + 4)));
                spec = trimView(rest.substr(0, inWord));
            }
            parseArchEntity(spec, group.id, group.icon, group.label);
            groups.push_back(std::move(group));
            continue;
        }
        if (startsWithWord(line, "service", &rest) ||
            startsWithWord(line, "junction", &rest)) {
            ArchNode node;
            node.junction = line[0] == 'j' || line[0] == 'J';
            std::string_view spec = rest;
            size_t inWord = rest.rfind(" in ");
            if (inWord != std::string_view::npos) {
                node.group = std::string(trimView(rest.substr(inWord + 4)));
                spec = trimView(rest.substr(0, inWord));
            }
            parseArchEntity(spec, node.id, node.icon, node.label);
            if (node.junction) node.label.clear();
            nodes.push_back(std::move(node));
            continue;
        }
        // Edge: id{group}?:S (<)?--(>)? T:id
        size_t firstColon = line.find(':');
        if (firstColon != std::string_view::npos) {
            std::string_view left = trimView(line.substr(0, firstColon));
            std::string_view remainder = line.substr(firstColon + 1);
            // strip {group} modifiers
            size_t brace = left.find('{');
            if (brace != std::string_view::npos) {
                left = left.substr(0, brace);
            }
            remainder = trimView(remainder);
            if (!remainder.empty()) {
                char fromSide =
                    static_cast<char>(std::toupper(remainder.front()));
                std::string_view afterSide = trimView(remainder.substr(1));
                bool arrowFrom = false, arrowTo = false;
                if (!afterSide.empty() && afterSide.front() == '<') {
                    arrowFrom = true;
                    afterSide = trimView(afterSide.substr(1));
                }
                size_t dashEnd = 0;
                while (dashEnd < afterSide.size() &&
                       afterSide[dashEnd] == '-') {
                    dashEnd++;
                }
                if (dashEnd >= 2) {
                    afterSide = trimView(afterSide.substr(dashEnd));
                    if (!afterSide.empty() && afterSide.front() == '>') {
                        arrowTo = true;
                        afterSide = trimView(afterSide.substr(1));
                    }
                    size_t toColon = afterSide.find(':');
                    if (toColon != std::string_view::npos &&
                        toColon == 1) {
                        char toSide = static_cast<char>(
                            std::toupper(afterSide.front()));
                        std::string_view right =
                            trimView(afterSide.substr(2));
                        size_t rightBrace = right.find('{');
                        if (rightBrace != std::string_view::npos) {
                            right = right.substr(0, rightBrace);
                        }
                        if (findNode(left) && findNode(right) &&
                            std::strchr("LRTB", fromSide) &&
                            std::strchr("LRTB", toSide)) {
                            ArchEdge edge;
                            edge.from = std::string(left);
                            edge.to = std::string(right);
                            edge.fromSide = fromSide;
                            edge.toSide = toSide;
                            edge.arrowFrom = arrowFrom;
                            edge.arrowTo = arrowTo;
                            edges.push_back(std::move(edge));
                            continue;
                        }
                    }
                }
            }
        }
        result.error = "Unsupported architecture statement";
        return result;
    }
    if (nodes.empty()) {
        result.error = "Empty architecture diagram";
        return result;
    }

    // Grid placement from the edge side constraints
    auto sideDelta = [](char side, int& dx, int& dy) {
        switch (side) {
            case 'L': dx = -1; dy = 0; break;
            case 'R': dx = 1; dy = 0; break;
            case 'T': dx = 0; dy = -1; break;
            default: dx = 0; dy = 1; break;
        }
    };
    auto cellTaken = [&](int gx, int gy) {
        for (const auto& node : nodes) {
            if (node.placed && node.gridX == gx && node.gridY == gy) {
                return true;
            }
        }
        return false;
    };

    nodes[0].placed = true;
    for (size_t pass = 0; pass < nodes.size() + edges.size() + 1; pass++) {
        bool changed = false;
        for (const auto& edge : edges) {
            ArchNode* from = findNode(edge.from);
            ArchNode* to = findNode(edge.to);
            if (from->placed == to->placed) continue;
            ArchNode* anchor = from->placed ? from : to;
            ArchNode* free = from->placed ? to : from;
            char side = from->placed ? edge.fromSide : edge.toSide;
            int dx, dy;
            sideDelta(side, dx, dy);
            int gx = anchor->gridX + dx;
            int gy = anchor->gridY + dy;
            while (cellTaken(gx, gy)) gy++;
            free->gridX = gx;
            free->gridY = gy;
            free->placed = true;
            changed = true;
        }
        if (!changed) break;
    }
    // Anything unconstrained flows into fresh rows below
    int fallbackY = 0;
    for (const auto& node : nodes) {
        if (node.placed) fallbackY = std::max(fallbackY, node.gridY + 1);
    }
    int fallbackX = 0;
    for (auto& node : nodes) {
        if (!node.placed) {
            while (cellTaken(fallbackX, fallbackY)) fallbackX++;
            node.gridX = fallbackX;
            node.gridY = fallbackY;
            node.placed = true;
        }
    }

    int minX = nodes[0].gridX, minY = nodes[0].gridY;
    for (const auto& node : nodes) {
        minX = std::min(minX, node.gridX);
        minY = std::min(minY, node.gridY);
    }
    float cell = kArchCell * scale;
    float icon = kArchIcon * scale;
    float top = 24.0f * scale;   // room for group labels
    float left = 16.0f * scale;
    for (auto& node : nodes) {
        node.x = left + (node.gridX - minX) * cell +
                 (cell - icon) * 0.5f;
        node.y = top + (node.gridY - minY) * cell + 6.0f * scale;
    }

    TextStyle labelStyle;
    labelStyle.scale = 0.85f;
    TextStyle groupStyle;
    groupStyle.bold = true;
    groupStyle.scale = 0.85f;

    std::vector<Prim> texts;

    // Group frames from member bounding boxes (nested groups included)
    struct GroupBox {
        float x1 = 1e9f, y1 = 1e9f, x2 = -1e9f, y2 = -1e9f;
        bool any = false;
    };
    std::map<std::string, GroupBox> groupBoxes;
    std::function<void(const std::string&, float, float, float, float)>
        extendGroup = [&](const std::string& id, float x1, float y1,
                          float x2, float y2) {
        auto& box = groupBoxes[id];
        box.x1 = std::min(box.x1, x1);
        box.y1 = std::min(box.y1, y1);
        box.x2 = std::max(box.x2, x2);
        box.y2 = std::max(box.y2, y2);
        box.any = true;
        for (const auto& group : groups) {
            if (group.id == id && !group.parent.empty()) {
                extendGroup(group.parent, x1, y1, x2, y2);
            }
        }
    };
    for (const auto& node : nodes) {
        if (node.group.empty()) continue;
        float labelSpace = node.junction ? 0.0f : 20.0f * scale;
        extendGroup(node.group, node.x - 8.0f * scale,
                    node.y - 8.0f * scale, node.x + icon + 8.0f * scale,
                    node.y + icon + labelSpace + 8.0f * scale);
    }
    for (const auto& group : groups) {
        auto found = groupBoxes.find(group.id);
        if (found == groupBoxes.end() || !found->second.any) continue;
        const auto& box = found->second;
        Prim frame;
        frame.type = PrimType::RoundRect;
        frame.radius = 8.0f * scale;
        frame.x1 = box.x1;
        frame.y1 = box.y1 - 18.0f * scale;
        frame.x2 = box.x2;
        frame.y2 = box.y2;
        frame.stroke = Role::Muted;
        frame.strokeWidth = 1.2f * scale;
        frame.dashed = true;
        result.prims.push_back(std::move(frame));

        Prim label;
        label.type = PrimType::Text;
        label.text = group.label;
        label.style = groupStyle;
        label.fill = Role::Muted;
        label.alignH = -1;
        label.x1 = box.x1 + 8.0f * scale;
        label.y1 = box.y1 - 16.0f * scale;
        label.x2 = box.x2 - 8.0f * scale;
        label.y2 = box.y1 + 2.0f * scale;
        texts.push_back(std::move(label));
    }

    // Edges between side anchor points
    auto anchor = [&](const ArchNode& node, char side, float& x, float& y) {
        float half = node.junction ? 4.0f * scale : icon * 0.5f;
        float cx = node.x + icon * 0.5f;
        float cy = node.y + icon * 0.5f;
        switch (side) {
            case 'L': x = cx - half; y = cy; break;
            case 'R': x = cx + half; y = cy; break;
            case 'T': x = cx; y = cy - half; break;
            default: x = cx; y = cy + half; break;
        }
    };
    for (const auto& edge : edges) {
        ArchNode* from = findNode(edge.from);
        ArchNode* to = findNode(edge.to);
        float x1, y1, x2, y2;
        anchor(*from, edge.fromSide, x1, y1);
        anchor(*to, edge.toSide, x2, y2);
        Prim line;
        line.type = PrimType::Line;
        line.x1 = x1;
        line.y1 = y1;
        line.x2 = x2;
        line.y2 = y2;
        line.stroke = Role::Muted;
        line.strokeWidth = 1.6f * scale;
        line.arrow = edge.arrowTo;
        result.prims.push_back(std::move(line));
        if (edge.arrowFrom) {
            Prim back;
            back.type = PrimType::Line;
            back.x1 = x2;
            back.y1 = y2;
            back.x2 = x1;
            back.y2 = y1;
            back.stroke = Role::Muted;
            back.strokeWidth = 1.6f * scale;
            back.arrow = true;
            result.prims.push_back(std::move(back));
        }
    }

    // Icons: simple line art in the theme accent
    for (const auto& node : nodes) {
        float x = node.x, y = node.y;
        if (node.junction) {
            Prim dot;
            dot.type = PrimType::Ellipse;
            dot.x1 = x + icon * 0.5f - 4.0f * scale;
            dot.y1 = y + icon * 0.5f - 4.0f * scale;
            dot.x2 = x + icon * 0.5f + 4.0f * scale;
            dot.y2 = y + icon * 0.5f + 4.0f * scale;
            dot.fill = Role::Muted;
            result.prims.push_back(std::move(dot));
            continue;
        }
        std::string kind = node.icon;
        std::transform(kind.begin(), kind.end(), kind.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        float stroke = 1.6f * scale;
        if (kind == "database" || kind == "disk") {
            float lidH = icon * (kind == "disk" ? 0.30f : 0.22f);
            float bodyTop = kind == "disk" ? y + icon * 0.18f : y;
            float bodyBottom = y + icon - (kind == "disk" ? icon * 0.18f : 0);
            Prim body;
            body.type = PrimType::RoundRect;
            body.radius = 10.0f * scale;
            body.x1 = x;
            body.y1 = bodyTop;
            body.x2 = x + icon;
            body.y2 = bodyBottom;
            body.fill = Role::Fill;
            body.stroke = Role::Stroke;
            body.strokeWidth = stroke;
            result.prims.push_back(std::move(body));
            Prim lid;
            lid.type = PrimType::Ellipse;
            lid.x1 = x;
            lid.y1 = bodyTop;
            lid.x2 = x + icon;
            lid.y2 = bodyTop + lidH;
            lid.stroke = Role::Stroke;
            lid.strokeWidth = stroke;
            result.prims.push_back(std::move(lid));
        } else if (kind == "cloud") {
            Prim shape;
            shape.type = PrimType::Polygon;
            shape.fill = Role::Fill;
            shape.stroke = Role::Stroke;
            shape.strokeWidth = stroke;
            // Three arcs over a flat base, sampled into a polygon
            struct Arc {
                float cx, cy, r, a0, a1;
            };
            const Arc kArcs[] = {
                {0.26f, 0.62f, 0.22f, 3.34f, 4.90f},
                {0.50f, 0.42f, 0.26f, 3.50f, 5.90f},
                {0.74f, 0.62f, 0.22f, 4.55f, 6.10f},
            };
            for (const auto& arc : kArcs) {
                for (int i = 0; i <= 8; i++) {
                    float t = arc.a0 + (arc.a1 - arc.a0) * i / 8.0f;
                    shape.pts.push_back(
                        {x + (arc.cx + arc.r * std::cos(t)) * icon,
                         y + (arc.cy + arc.r * std::sin(t)) * icon});
                }
            }
            shape.pts.push_back({x + 0.92f * icon, y + 0.80f * icon});
            shape.pts.push_back({x + 0.08f * icon, y + 0.80f * icon});
            result.prims.push_back(std::move(shape));
        } else if (kind == "internet") {
            Prim globe;
            globe.type = PrimType::Ellipse;
            globe.x1 = x;
            globe.y1 = y;
            globe.x2 = x + icon;
            globe.y2 = y + icon;
            globe.fill = Role::Fill;
            globe.stroke = Role::Stroke;
            globe.strokeWidth = stroke;
            result.prims.push_back(std::move(globe));
            Prim meridian;
            meridian.type = PrimType::Ellipse;
            meridian.x1 = x + icon * 0.30f;
            meridian.y1 = y;
            meridian.x2 = x + icon * 0.70f;
            meridian.y2 = y + icon;
            meridian.stroke = Role::Stroke;
            meridian.strokeWidth = stroke * 0.8f;
            result.prims.push_back(std::move(meridian));
            Prim equator;
            equator.type = PrimType::Line;
            equator.x1 = x;
            equator.y1 = y + icon * 0.5f;
            equator.x2 = x + icon;
            equator.y2 = y + icon * 0.5f;
            equator.stroke = Role::Stroke;
            equator.strokeWidth = stroke * 0.8f;
            result.prims.push_back(std::move(equator));
        } else {  // server and anything unknown
            Prim box;
            box.type = PrimType::RoundRect;
            box.radius = 8.0f * scale;
            box.x1 = x;
            box.y1 = y;
            box.x2 = x + icon;
            box.y2 = y + icon;
            box.fill = Role::Fill;
            box.stroke = Role::Stroke;
            box.strokeWidth = stroke;
            result.prims.push_back(std::move(box));
            if (kind == "server" || kind.empty()) {
                for (int i = 1; i <= 2; i++) {
                    Prim slot;
                    slot.type = PrimType::Line;
                    slot.x1 = x + icon * 0.16f;
                    slot.y1 = y + icon * i / 3.0f;
                    slot.x2 = x + icon * 0.84f;
                    slot.y2 = slot.y1;
                    slot.stroke = Role::Stroke;
                    slot.strokeWidth = stroke * 0.8f;
                    result.prims.push_back(std::move(slot));
                }
                Prim light;
                light.type = PrimType::Ellipse;
                light.x1 = x + icon * 0.70f;
                light.y1 = y + icon * 0.78f;
                light.x2 = x + icon * 0.78f;
                light.y2 = y + icon * 0.86f;
                light.fill = Role::Stroke;
                result.prims.push_back(std::move(light));
            }
        }

        if (!node.label.empty()) {
            Size size = measure(node.label, labelStyle, cell - 8.0f * scale);
            Prim label;
            label.type = PrimType::Text;
            label.text = node.label;
            label.style = labelStyle;
            label.fill = Role::Text;
            label.x1 = x + icon * 0.5f - cell * 0.5f + 4.0f * scale;
            label.y1 = y + icon + 2.0f * scale;
            label.x2 = x + icon * 0.5f + cell * 0.5f - 4.0f * scale;
            label.y2 = y + icon + 2.0f * scale + size.h;
            label.alignV = -1;
            texts.push_back(std::move(label));
        }
    }
    for (auto& text : texts) result.prims.push_back(std::move(text));

    float right = 0.0f, bottom = 0.0f;
    for (const auto& prim : result.prims) {
        if (prim.type == PrimType::Polygon) {
            for (const auto& point : prim.pts) {
                right = std::max(right, point.x);
                bottom = std::max(bottom, point.y);
            }
        } else {
            right = std::max(right, std::max(prim.x1, prim.x2));
            bottom = std::max(bottom, std::max(prim.y1, prim.y2));
        }
    }
    result.width = right + 8.0f * scale;
    result.height = bottom + 8.0f * scale;
    result.ok = true;
    normalizeLeft(result);
    return result;
}

}  // namespace detail
}  // namespace mermaidext
