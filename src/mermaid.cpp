#include "mermaid.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace mermaid {
namespace {

struct NodeSpec {
    std::string id;
    std::string label;
    NodeShape shape = NodeShape::Rectangle;
    std::string className;
    bool hasDefinition = false;
    size_t sourceOffset = 0;
};

struct ArrowSpec {
    bool directed = true;
    bool dashed = false;
    float strokeScale = 1.0f;
    std::string label;
};

struct Delimiter {
    std::string_view open;
    std::string_view close;
    NodeShape shape;
};

constexpr Delimiter kDelimiters[] = {
    { "((", "))", NodeShape::Circle },
    { "([", "])", NodeShape::Stadium },
    { "[(", ")]", NodeShape::RoundedRectangle },
    { "[[", "]]", NodeShape::Rectangle },
    { "{{", "}}", NodeShape::Hexagon },
    { "[/", "/]", NodeShape::Rectangle },
    { "[\\", "\\]", NodeShape::Rectangle },
    { "(", ")", NodeShape::RoundedRectangle },
    { "{", "}", NodeShape::Diamond },
    { "[", "]", NodeShape::Rectangle },
};

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

char lowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && isSpace(value.front())) value.remove_prefix(1);
    while (!value.empty() && isSpace(value.back())) value.remove_suffix(1);
    return value;
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); i++) {
        if (lowerAscii(left[i]) != lowerAscii(right[i])) return false;
    }
    return true;
}

bool startsWithIgnoreCase(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
        equalsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

bool startsWithAt(std::string_view value, size_t position, std::string_view prefix) {
    return position <= value.size() &&
        value.size() - position >= prefix.size() &&
        value.substr(position, prefix.size()) == prefix;
}

void skipSpaces(std::string_view value, size_t& position) {
    while (position < value.size() && isSpace(value[position])) position++;
}

std::string_view readWord(std::string_view value, size_t& position) {
    skipSpaces(value, position);
    size_t start = position;
    while (position < value.size() && !isSpace(value[position])) position++;
    return value.substr(start, position - start);
}

bool isArrowAt(std::string_view value, size_t position) {
    return startsWithAt(value, position, "-->") ||
        startsWithAt(value, position, "---") ||
        startsWithAt(value, position, "-.->") ||
        startsWithAt(value, position, "==>");
}

std::string decodeLabel(std::string_view encoded) {
    std::string decoded;
    decoded.reserve(encoded.size());

    for (size_t i = 0; i < encoded.size();) {
        if (encoded[i] == '\\' && i + 1 < encoded.size()) {
            char next = encoded[i + 1];
            if (next == '"' || next == '\'' || next == '\\') {
                decoded.push_back(next);
                i += 2;
                continue;
            }
            if (next == 'n') {
                // Mermaid renders a literal \n in labels as a line break
                decoded.push_back('\n');
                i += 2;
                continue;
            }
        }

        if (encoded[i] == '<') {
            size_t close = encoded.find('>', i + 1);
            if (close != std::string_view::npos) {
                std::string_view tag = trim(encoded.substr(i + 1, close - i - 1));
                if (!tag.empty() && tag.back() == '/') {
                    tag.remove_suffix(1);
                    tag = trim(tag);
                }
                if (equalsIgnoreCase(tag, "br")) {
                    decoded.push_back('\n');
                    i = close + 1;
                    continue;
                }
            }
        }

        struct Entity {
            std::string_view encoded;
            std::string_view decoded;
        };
        constexpr Entity entities[] = {
            { "&amp;", "&" },
            { "&lt;", "<" },
            { "&gt;", ">" },
            { "&quot;", "\"" },
            { "&#39;", "'" },
        };

        bool matchedEntity = false;
        for (const auto& entity : entities) {
            if (startsWithAt(encoded, i, entity.encoded)) {
                decoded.append(entity.decoded);
                i += entity.encoded.size();
                matchedEntity = true;
                break;
            }
        }
        if (matchedEntity) continue;

        decoded.push_back(encoded[i]);
        i++;
    }

    return decoded;
}

bool parseHexDigit(char c, uint32_t& value) {
    if (c >= '0' && c <= '9') {
        value = static_cast<uint32_t>(c - '0');
        return true;
    }
    c = lowerAscii(c);
    if (c >= 'a' && c <= 'f') {
        value = static_cast<uint32_t>(c - 'a' + 10);
        return true;
    }
    return false;
}

bool parseColor(std::string_view value, Color& color) {
    value = trim(value);
    if (equalsIgnoreCase(value, "transparent") || equalsIgnoreCase(value, "none")) {
        color = {0, 0.0f};
        return true;
    }

    struct NamedColor {
        std::string_view name;
        uint32_t rgb;
    };
    constexpr NamedColor namedColors[] = {
        { "black", 0x000000 },
        { "white", 0xFFFFFF },
        { "red", 0xFF0000 },
        { "green", 0x008000 },
        { "blue", 0x0000FF },
        { "gray", 0x808080 },
        { "grey", 0x808080 },
        { "yellow", 0xFFFF00 },
        { "orange", 0xFFA500 },
        { "purple", 0x800080 },
    };
    for (const auto& named : namedColors) {
        if (equalsIgnoreCase(value, named.name)) {
            color = {named.rgb, 1.0f};
            return true;
        }
    }

    if (value.empty() || value.front() != '#') return false;
    value.remove_prefix(1);

    if (value.size() == 3 || value.size() == 4) {
        uint32_t components[4] = {};
        for (size_t i = 0; i < value.size(); i++) {
            if (!parseHexDigit(value[i], components[i])) return false;
            components[i] = components[i] * 17;
        }
        color.rgb = (components[0] << 16) | (components[1] << 8) | components[2];
        color.alpha = value.size() == 4 ? components[3] / 255.0f : 1.0f;
        return true;
    }

    if (value.size() == 6 || value.size() == 8) {
        uint32_t components[8] = {};
        for (size_t i = 0; i < value.size(); i++) {
            if (!parseHexDigit(value[i], components[i])) return false;
        }
        auto byteAt = [&](size_t index) {
            return (components[index] << 4) | components[index + 1];
        };
        color.rgb = (byteAt(0) << 16) | (byteAt(2) << 8) | byteAt(4);
        color.alpha = value.size() == 8 ? byteAt(6) / 255.0f : 1.0f;
        return true;
    }

    return false;
}

bool parseStrokeWidth(std::string_view value, float& width) {
    std::string text(trim(value));
    if (text.empty()) return false;

    errno = 0;
    char* end = nullptr;
    float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || errno == ERANGE || !std::isfinite(parsed) || parsed < 0.0f) {
        return false;
    }
    while (*end != '\0' && isSpace(*end)) end++;
    if (*end != '\0' && std::string_view(end) != "px") return false;

    width = parsed;
    return true;
}

void mergeStyle(Style& target, const Style& source) {
    if (source.hasFill) {
        target.hasFill = true;
        target.fill = source.fill;
    }
    if (source.hasStroke) {
        target.hasStroke = true;
        target.stroke = source.stroke;
    }
    if (source.hasText) {
        target.hasText = true;
        target.text = source.text;
    }
    if (source.hasStrokeWidth) {
        target.hasStrokeWidth = true;
        target.strokeWidth = source.strokeWidth;
    }
}

bool parseStyleList(std::string_view value, Style& style, std::string& error) {
    size_t position = 0;
    while (position <= value.size()) {
        size_t comma = value.find(',', position);
        if (comma == std::string_view::npos) comma = value.size();
        std::string_view item = trim(value.substr(position, comma - position));
        if (!item.empty()) {
            size_t colon = item.find(':');
            if (colon == std::string_view::npos) {
                error = "Expected ':' in style declaration";
                return false;
            }

            std::string_view key = trim(item.substr(0, colon));
            std::string_view styleValue = trim(item.substr(colon + 1));
            Color color;

            if (equalsIgnoreCase(key, "fill")) {
                if (!parseColor(styleValue, color)) {
                    error = "Invalid fill color";
                    return false;
                }
                style.hasFill = true;
                style.fill = color;
            } else if (equalsIgnoreCase(key, "stroke")) {
                if (!parseColor(styleValue, color)) {
                    error = "Invalid stroke color";
                    return false;
                }
                style.hasStroke = true;
                style.stroke = color;
            } else if (equalsIgnoreCase(key, "color")) {
                if (!parseColor(styleValue, color)) {
                    error = "Invalid text color";
                    return false;
                }
                style.hasText = true;
                style.text = color;
            } else if (equalsIgnoreCase(key, "stroke-width")) {
                if (!parseStrokeWidth(styleValue, style.strokeWidth)) {
                    error = "Invalid stroke width";
                    return false;
                }
                style.hasStrokeWidth = true;
            }
        }

        if (comma == value.size()) break;
        position = comma + 1;
    }
    return true;
}

bool parseNodeSpec(std::string_view line, size_t& position, size_t sourceOffset,
                   NodeSpec& spec, std::string& error) {
    skipSpaces(line, position);
    size_t idStart = position;

    while (position < line.size()) {
        char c = line[position];
        if (isSpace(c) || c == '[' || c == '(' || c == '{' ||
            c == ':' || c == ';' || c == ',') {
            break;
        }
        if (isArrowAt(line, position)) break;
        position++;
    }

    if (position == idStart) {
        error = "Expected a node identifier";
        return false;
    }

    spec.id = std::string(line.substr(idStart, position - idStart));
    spec.label = spec.id;
    spec.sourceOffset = sourceOffset + idStart;
    skipSpaces(line, position);

    // Mermaid v11 attribute syntax (A@{ shape: ..., label: ... }) is not
    // supported — fail so the block falls back to a readable code block
    // instead of rendering the raw attributes as a diamond label
    if (!spec.id.empty() && spec.id.back() == '@' &&
        position < line.size() && line[position] == '{') {
        error = "Mermaid '@{ }' attribute syntax is not supported";
        return false;
    }

    const Delimiter* delimiter = nullptr;
    for (const auto& candidate : kDelimiters) {
        if (startsWithAt(line, position, candidate.open)) {
            delimiter = &candidate;
            break;
        }
    }

    if (delimiter) {
        spec.hasDefinition = true;
        spec.shape = delimiter->shape;
        position += delimiter->open.size();
        skipSpaces(line, position);

        std::string_view encodedLabel;
        if (position < line.size() && (line[position] == '"' || line[position] == '\'')) {
            char quote = line[position++];
            size_t labelStart = position;
            bool escaped = false;
            while (position < line.size()) {
                char c = line[position];
                if (c == quote && !escaped) break;
                if (c == '\\' && !escaped) {
                    escaped = true;
                } else {
                    escaped = false;
                }
                position++;
            }
            if (position >= line.size()) {
                error = "Unterminated quoted node label";
                return false;
            }
            encodedLabel = line.substr(labelStart, position - labelStart);
            position++;
            skipSpaces(line, position);
            if (!startsWithAt(line, position, delimiter->close)) {
                error = "Expected closing node delimiter";
                return false;
            }
            position += delimiter->close.size();
        } else {
            size_t close = line.find(delimiter->close, position);
            if (close == std::string_view::npos) {
                error = "Unterminated node label";
                return false;
            }
            encodedLabel = trim(line.substr(position, close - position));
            position = close + delimiter->close.size();
        }
        spec.label = decodeLabel(encodedLabel);
    }

    skipSpaces(line, position);
    if (startsWithAt(line, position, ":::")) {
        position += 3;
        size_t classStart = position;
        while (position < line.size() && !isSpace(line[position]) &&
               line[position] != ';' && !isArrowAt(line, position)) {
            position++;
        }
        if (position == classStart) {
            error = "Expected a class name after ':::'";
            return false;
        }
        spec.className = std::string(line.substr(classStart, position - classStart));
    }

    return true;
}

bool parseArrow(std::string_view line, size_t& position, ArrowSpec& arrow,
                std::string& error) {
    skipSpaces(line, position);

    if (startsWithAt(line, position, "-.->")) {
        arrow.directed = true;
        arrow.dashed = true;
        position += 4;
    } else if (startsWithAt(line, position, "==>")) {
        arrow.directed = true;
        arrow.strokeScale = 2.0f;
        position += 3;
    } else if (startsWithAt(line, position, "-->")) {
        arrow.directed = true;
        position += 3;
    } else if (startsWithAt(line, position, "---")) {
        arrow.directed = false;
        position += 3;
    } else if (startsWithAt(line, position, "--")) {
        size_t endArrow = line.find("-->", position + 2);
        if (endArrow == std::string_view::npos) {
            error = "Unsupported edge syntax";
            return false;
        }
        arrow.directed = true;
        arrow.label = std::string(trim(line.substr(position + 2, endArrow - position - 2)));
        position = endArrow + 3;
    } else {
        error = "Expected a Mermaid edge";
        return false;
    }

    skipSpaces(line, position);
    if (position < line.size() && line[position] == '|') {
        size_t labelEnd = line.find('|', position + 1);
        if (labelEnd == std::string_view::npos) {
            error = "Unterminated edge label";
            return false;
        }
        arrow.label = decodeLabel(line.substr(position + 1, labelEnd - position - 1));
        position = labelEnd + 1;
    }
    return true;
}

size_t ensureNode(Diagram& diagram, std::unordered_map<std::string, size_t>& nodeIds,
                  const NodeSpec& spec) {
    auto existing = nodeIds.find(spec.id);
    if (existing == nodeIds.end()) {
        size_t index = diagram.nodes.size();
        Node node;
        node.id = spec.id;
        node.label = spec.label;
        node.shape = spec.shape;
        node.className = spec.className;
        node.sourceOffset = spec.sourceOffset;
        diagram.nodes.push_back(std::move(node));
        nodeIds.emplace(spec.id, index);
        return index;
    }

    Node& node = diagram.nodes[existing->second];
    if (spec.hasDefinition) {
        node.label = spec.label;
        node.shape = spec.shape;
    }
    if (!spec.className.empty()) node.className = spec.className;
    node.sourceOffset = std::min(node.sourceOffset, spec.sourceOffset);
    return existing->second;
}

bool parseDirection(std::string_view value, Direction& direction) {
    if (equalsIgnoreCase(value, "TB") || equalsIgnoreCase(value, "TD")) {
        direction = Direction::TopToBottom;
    } else if (equalsIgnoreCase(value, "BT")) {
        direction = Direction::BottomToTop;
    } else if (equalsIgnoreCase(value, "LR")) {
        direction = Direction::LeftToRight;
    } else if (equalsIgnoreCase(value, "RL")) {
        direction = Direction::RightToLeft;
    } else {
        return false;
    }
    return true;
}

ParseResult fail(ParseResult result, size_t line, std::string message) {
    result.success = false;
    result.errorLine = line;
    result.error = std::move(message);
    return result;
}

} // namespace

ParseResult parse(std::string_view source) {
    std::string normalized(source);
    if (normalized.size() >= 3 &&
        static_cast<unsigned char>(normalized[0]) == 0xEF &&
        static_cast<unsigned char>(normalized[1]) == 0xBB &&
        static_cast<unsigned char>(normalized[2]) == 0xBF) {
        normalized[0] = ' ';
        normalized[1] = ' ';
        normalized[2] = ' ';
    }

    char quote = '\0';
    bool escaped = false;
    bool comment = false;
    bool pipeLabel = false;
    int squareDepth = 0;
    int roundDepth = 0;
    int curlyDepth = 0;
    for (size_t i = 0; i < normalized.size(); i++) {
        char& c = normalized[i];
        if (c == '\n') {
            comment = false;
            pipeLabel = false;
            continue;
        }
        if (comment) continue;
        if (quote != '\0') {
            if (c == quote && !escaped) quote = '\0';
            if (c == '\\' && !escaped) {
                escaped = true;
            } else {
                escaped = false;
            }
            continue;
        }
        if (pipeLabel) {
            if (c == '|') pipeLabel = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            escaped = false;
            continue;
        }
        if (c == '%' && i + 1 < normalized.size() &&
            normalized[i + 1] == '%' &&
            squareDepth == 0 && roundDepth == 0 && curlyDepth == 0) {
            comment = true;
            continue;
        }
        if (c == '|' && squareDepth == 0 && roundDepth == 0 && curlyDepth == 0) {
            pipeLabel = true;
        } else if (c == '[') squareDepth++;
        else if (c == ']' && squareDepth > 0) squareDepth--;
        else if (c == '(') roundDepth++;
        else if (c == ')' && roundDepth > 0) roundDepth--;
        else if (c == '{') curlyDepth++;
        else if (c == '}' && curlyDepth > 0) curlyDepth--;
        else if (c == ';' && squareDepth == 0 && roundDepth == 0 && curlyDepth == 0) {
            c = '\n';
        }
    }
    source = normalized;

    ParseResult result;
    std::unordered_map<std::string, size_t> nodeIds;
    bool foundHeader = false;

    size_t lineNumber = 0;
    size_t lineOffset = 0;
    while (lineOffset <= source.size()) {
        lineNumber++;
        size_t lineEnd = source.find('\n', lineOffset);
        if (lineEnd == std::string_view::npos) lineEnd = source.size();

        std::string_view line = source.substr(lineOffset, lineEnd - lineOffset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        line = trim(line);
        if (!line.empty() && line.back() == ';') {
            line.remove_suffix(1);
            line = trim(line);
        }

        if (!line.empty() && !startsWithAt(line, 0, "%%")) {
            size_t position = 0;
            std::string_view keyword = readWord(line, position);

            if (!foundHeader) {
                if (!equalsIgnoreCase(keyword, "flowchart") &&
                    !equalsIgnoreCase(keyword, "graph")) {
                    return fail(std::move(result), lineNumber,
                                "Only Mermaid flowchart and graph diagrams are supported");
                }

                std::string_view direction = readWord(line, position);
                if (!direction.empty() && !parseDirection(direction, result.diagram.direction)) {
                    return fail(std::move(result), lineNumber,
                                "Unsupported flowchart direction");
                }
                foundHeader = true;
            } else if (equalsIgnoreCase(keyword, "classDef")) {
                std::string_view className = readWord(line, position);
                if (className.empty()) {
                    return fail(std::move(result), lineNumber,
                                "Expected a class name after classDef");
                }
                Style style;
                std::string error;
                if (!parseStyleList(trim(line.substr(position)), style, error)) {
                    return fail(std::move(result), lineNumber, std::move(error));
                }
                result.diagram.classStyles[std::string(className)] = style;
            } else if (equalsIgnoreCase(keyword, "class")) {
                std::string_view ids = readWord(line, position);
                std::string_view className = readWord(line, position);
                if (ids.empty() || className.empty()) {
                    return fail(std::move(result), lineNumber,
                                "Expected node IDs and a class name");
                }

                size_t idPosition = 0;
                while (idPosition <= ids.size()) {
                    size_t comma = ids.find(',', idPosition);
                    if (comma == std::string_view::npos) comma = ids.size();
                    std::string_view id = trim(ids.substr(idPosition, comma - idPosition));
                    if (!id.empty()) {
                        NodeSpec spec;
                        spec.id = std::string(id);
                        spec.label = spec.id;
                        spec.className = std::string(className);
                        spec.sourceOffset = lineOffset;
                        ensureNode(result.diagram, nodeIds, spec);
                    }
                    if (comma == ids.size()) break;
                    idPosition = comma + 1;
                }
            } else if (equalsIgnoreCase(keyword, "style")) {
                std::string_view id = readWord(line, position);
                if (id.empty()) {
                    return fail(std::move(result), lineNumber,
                                "Expected a node ID after style");
                }
                Style style;
                std::string error;
                if (!parseStyleList(trim(line.substr(position)), style, error)) {
                    return fail(std::move(result), lineNumber, std::move(error));
                }
                NodeSpec spec;
                spec.id = std::string(id);
                spec.label = spec.id;
                spec.sourceOffset = lineOffset;
                size_t nodeIndex = ensureNode(result.diagram, nodeIds, spec);
                mergeStyle(result.diagram.nodes[nodeIndex].style, style);
            } else if (equalsIgnoreCase(keyword, "subgraph") ||
                       equalsIgnoreCase(keyword, "end") ||
                       equalsIgnoreCase(keyword, "click") ||
                       equalsIgnoreCase(keyword, "linkStyle")) {
                return fail(std::move(result), lineNumber,
                            "This Mermaid flowchart statement is not supported");
            } else {
                position = 0;
                NodeSpec currentSpec;
                std::string error;
                if (!parseNodeSpec(line, position, lineOffset, currentSpec, error)) {
                    return fail(std::move(result), lineNumber, std::move(error));
                }
                size_t currentNode = ensureNode(result.diagram, nodeIds, currentSpec);

                while (true) {
                    skipSpaces(line, position);
                    if (position >= line.size()) break;

                    ArrowSpec arrow;
                    if (!parseArrow(line, position, arrow, error)) {
                        return fail(std::move(result), lineNumber, std::move(error));
                    }

                    NodeSpec nextSpec;
                    if (!parseNodeSpec(line, position, lineOffset, nextSpec, error)) {
                        return fail(std::move(result), lineNumber, std::move(error));
                    }
                    size_t nextNode = ensureNode(result.diagram, nodeIds, nextSpec);
                    result.diagram.edges.push_back({
                        currentNode,
                        nextNode,
                        std::move(arrow.label),
                        arrow.directed,
                        arrow.dashed,
                        arrow.strokeScale,
                    });
                    currentNode = nextNode;
                }
            }
        }

        if (lineEnd == source.size()) break;
        lineOffset = lineEnd + 1;
    }

    if (!foundHeader) {
        return fail(std::move(result), 0,
                    "Only Mermaid flowchart and graph diagrams are supported");
    }
    if (result.diagram.nodes.empty()) {
        return fail(std::move(result), 0, "The Mermaid flowchart has no nodes");
    }

    result.success = true;
    return result;
}

Layout layout(const Diagram& diagram, const std::vector<Size>& nodeSizes,
              float nodeGap, float rankGap) {
    Layout result;
    const size_t nodeCount = diagram.nodes.size();
    if (nodeCount == 0 || nodeSizes.size() != nodeCount) return result;

    nodeGap = std::max(0.0f, nodeGap);
    rankGap = std::max(0.0f, rankGap);

    std::vector<std::vector<size_t>> outgoing(nodeCount);
    std::vector<std::vector<size_t>> incoming(nodeCount);
    std::vector<size_t> indegree(nodeCount, 0);
    for (const auto& edge : diagram.edges) {
        if (edge.from >= nodeCount || edge.to >= nodeCount) continue;
        outgoing[edge.from].push_back(edge.to);
        incoming[edge.to].push_back(edge.from);
        indegree[edge.to]++;
    }

    std::deque<size_t> ready;
    for (size_t i = 0; i < nodeCount; i++) {
        if (indegree[i] == 0) ready.push_back(i);
    }

    std::vector<size_t> rank(nodeCount, 0);
    std::vector<bool> processed(nodeCount, false);
    size_t maxRank = 0;
    while (!ready.empty()) {
        size_t node = ready.front();
        ready.pop_front();
        processed[node] = true;
        maxRank = std::max(maxRank, rank[node]);

        for (size_t target : outgoing[node]) {
            rank[target] = std::max(rank[target], rank[node] + 1);
            if (--indegree[target] == 0) ready.push_back(target);
        }
    }

    bool processedAny = std::any_of(
        processed.begin(), processed.end(), [](bool value) { return value; });
    size_t nextRank = processedAny ? maxRank + 1 : 0;
    for (size_t i = 0; i < nodeCount; i++) {
        if (!processed[i]) rank[i] = nextRank++;
    }
    for (size_t value : rank) maxRank = std::max(maxRank, value);

    std::vector<std::vector<size_t>> ranks(maxRank + 1);
    for (size_t i = 0; i < nodeCount; i++) ranks[rank[i]].push_back(i);
    result.ranks = rank;

    std::vector<float> order(nodeCount, 0.0f);
    for (size_t level = 0; level < ranks.size(); level++) {
        if (level > 0) {
            std::stable_sort(ranks[level].begin(), ranks[level].end(),
                [&](size_t left, size_t right) {
                    auto barycenter = [&](size_t node) {
                        if (incoming[node].empty()) {
                            return static_cast<float>(node);
                        }
                        float total = 0.0f;
                        size_t count = 0;
                        for (size_t parent : incoming[node]) {
                            if (rank[parent] < level) {
                                total += order[parent];
                                count++;
                            }
                        }
                        return count > 0 ? total / static_cast<float>(count)
                                         : static_cast<float>(node);
                    };
                    return barycenter(left) < barycenter(right);
                });
        }
        for (size_t i = 0; i < ranks[level].size(); i++) {
            order[ranks[level][i]] = static_cast<float>(i);
        }
    }

    result.nodes.resize(nodeCount);
    bool vertical = diagram.direction == Direction::TopToBottom ||
                    diagram.direction == Direction::BottomToTop;

    if (vertical) {
        std::vector<float> rankWidths(ranks.size(), 0.0f);
        std::vector<float> rankHeights(ranks.size(), 0.0f);
        for (size_t level = 0; level < ranks.size(); level++) {
            for (size_t node : ranks[level]) {
                rankWidths[level] += std::max(1.0f, nodeSizes[node].width);
                rankHeights[level] = std::max(
                    rankHeights[level], std::max(1.0f, nodeSizes[node].height));
            }
            if (ranks[level].size() > 1) {
                rankWidths[level] += nodeGap * static_cast<float>(ranks[level].size() - 1);
            }
            result.width = std::max(result.width, rankWidths[level]);
        }

        float y = 0.0f;
        for (size_t level = 0; level < ranks.size(); level++) {
            float x = (result.width - rankWidths[level]) * 0.5f;
            for (size_t node : ranks[level]) {
                float width = std::max(1.0f, nodeSizes[node].width);
                float height = std::max(1.0f, nodeSizes[node].height);
                float nodeY = y + (rankHeights[level] - height) * 0.5f;
                result.nodes[node] = {x, nodeY, x + width, nodeY + height};
                x += width + nodeGap;
            }
            y += rankHeights[level];
            if (level + 1 < ranks.size()) y += rankGap;
        }
        result.height = y;

        if (diagram.direction == Direction::BottomToTop) {
            for (auto& rect : result.nodes) {
                float oldTop = rect.top;
                rect.top = result.height - rect.bottom;
                rect.bottom = result.height - oldTop;
            }
        }
    } else {
        std::vector<float> rankWidths(ranks.size(), 0.0f);
        std::vector<float> rankHeights(ranks.size(), 0.0f);
        for (size_t level = 0; level < ranks.size(); level++) {
            for (size_t node : ranks[level]) {
                rankWidths[level] = std::max(
                    rankWidths[level], std::max(1.0f, nodeSizes[node].width));
                rankHeights[level] += std::max(1.0f, nodeSizes[node].height);
            }
            if (ranks[level].size() > 1) {
                rankHeights[level] += nodeGap * static_cast<float>(ranks[level].size() - 1);
            }
            result.height = std::max(result.height, rankHeights[level]);
        }

        float x = 0.0f;
        for (size_t level = 0; level < ranks.size(); level++) {
            float y = (result.height - rankHeights[level]) * 0.5f;
            for (size_t node : ranks[level]) {
                float width = std::max(1.0f, nodeSizes[node].width);
                float height = std::max(1.0f, nodeSizes[node].height);
                float nodeX = x + (rankWidths[level] - width) * 0.5f;
                result.nodes[node] = {nodeX, y, nodeX + width, y + height};
                y += height + nodeGap;
            }
            x += rankWidths[level];
            if (level + 1 < ranks.size()) x += rankGap;
        }
        result.width = x;

        if (diagram.direction == Direction::RightToLeft) {
            for (auto& rect : result.nodes) {
                float oldLeft = rect.left;
                rect.left = result.width - rect.right;
                rect.right = result.width - oldLeft;
            }
        }
    }

    return result;
}

} // namespace mermaid
