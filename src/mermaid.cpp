#include "mermaid.h"
#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

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
    { ">", "]", NodeShape::Asymmetric },
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

// A Mermaid link. A longer one (--->, ===>, -..->, ----) is the same edge
// as its short form: Mermaid stretches it over more ranks, which Tinta's
// layout does not, since an edge across ranks would go round them.
struct Link {
    size_t length = 0;         // 0: no whole link here
    bool unsupported = false;  // circle or cross ends, ~~~: not drawn
    bool directed = false;
    bool dashed = false;
    float strokeScale = 1.0f;
};

// The link at position, or the end of dotted edge text (.->, ..-) when
// dottedEnd is set
Link linkAt(std::string_view value, size_t position, bool dottedEnd = false) {
    auto at = [&](size_t i) { return i < value.size() ? value[i] : '\0'; };
    Link link;
    size_t end = position;
    const char first = at(position);
    if (dottedEnd || (first == '-' && at(position + 1) == '.')) {
        // Dotted: -.- and -.->, with any number of dots
        end = dottedEnd ? position : position + 1;
        if (at(end) != '.') return link;
        while (at(end) == '.') end++;
        if (at(end) != '-') return link;  // "-." opens dotted edge text
        end++;
        link.dashed = true;
    } else if (first == '-' || first == '=') {
        // Solid and thick: two or more before a '>', three or more open
        while (at(end) == first) end++;
        if (end - position < 2) return link;
        link.strokeScale = first == '=' ? 2.0f : 1.0f;
        if (at(end) != '>' && end - position < 3) {
            // "--" and "==" open edge text, but --o and --x are ends
            link.unsupported = at(end) == 'o' || at(end) == 'x';
            return link;
        }
    } else {
        while (at(end) == '~') end++;
        link.unsupported = end - position >= 3;  // ~~~, an invisible link
        return link;
    }
    if (at(end) == '>') {
        link.directed = true;
        end++;
    } else if (at(end) == 'o' || at(end) == 'x') {
        link.unsupported = true;
        return link;
    }
    link.length = end - position;
    return link;
}

// "-- text -->", "== text ==>" and "-. text .->" open with two characters
bool opensLinkText(std::string_view value, size_t position) {
    return startsWithAt(value, position, "--") || startsWithAt(value, position, "==") ||
           startsWithAt(value, position, "-.");
}

bool isArrowAt(std::string_view value, size_t position) {
    const Link link = linkAt(value, position);
    return link.length > 0 || link.unsupported || opensLinkText(value, position);
}

// The ']' closing the unquoted label of an asymmetric node (id>text])
// whose '>' is at position, or npos. A '>' after anything but a node ID
// opens no label, nor does one that meets an arrow or the line's end first;
// a quoted label is kept whole by its quotes.
size_t asymmetricLabelEnd(std::string_view text, size_t position) {
    const auto before = static_cast<unsigned char>(position > 0 ? text[position - 1] : ' ');
    if (!std::isalnum(before) && before != '_' && before < 0x80) {
        return std::string_view::npos;
    }
    size_t i = position + 1;
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) i++;
    if (i < text.size() && (text[i] == '"' || text[i] == '\'')) {
        return std::string_view::npos;
    }
    for (; i < text.size() && text[i] != '\n'; i++) {
        if (text[i] == ']') return i;
        if (isArrowAt(text, i)) break;
    }
    return std::string_view::npos;
}

// A line break inside a double-quoted string, kept in the statement as one
// character of the same width (#281)
constexpr char kQuotedLineBreak = '\x1F';

std::string decodeLabel(std::string_view encoded) {
    std::string decoded;
    decoded.reserve(encoded.size());

    for (size_t i = 0; i < encoded.size();) {
        if (encoded[i] == kQuotedLineBreak) {
            // The label's own line break; the source's indentation and
            // line endings around it are not part of the text
            while (!decoded.empty() && isSpace(decoded.back())) decoded.pop_back();
            decoded.push_back('\n');
            i++;
            while (i < encoded.size() && isSpace(encoded[i])) i++;
            continue;
        }

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
                // Continuation of an unknown comma-valued property
                // (stroke-dasharray: 9,5): skip it like the property itself
                if (comma == value.size()) break;
                position = comma + 1;
                continue;
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
        if (isSpace(c) || c == '[' || c == '(' || c == '{' || c == '>' ||
            c == '<' || c == ':' || c == ';' || c == ',') {
            break;
        }
        if (isArrowAt(line, position)) break;
        position++;
    }

    // A '>' straight after '-' or '=' heads an arrow Mermaid does not have
    // (->, =>), not an asymmetric node
    if (position > 0 && position < line.size() && line[position] == '>' &&
        (line[position - 1] == '-' || line[position - 1] == '=')) {
        error = "Unsupported edge syntax";
        return false;
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

// An edge's text. Quotes around it delimit a Mermaid string, as they do in
// a node's label, and are not part of the text (#281).
std::string decodeEdgeLabel(std::string_view text) {
    text = trim(text);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
    }
    return decodeLabel(text);
}

// Past the double-quoted string starting at position, or position itself
// when none starts there
size_t skipQuotedString(std::string_view line, size_t position) {
    if (position >= line.size() || line[position] != '"') return position;
    for (size_t i = position + 1; i < line.size(); i++) {
        if (line[i] == '\\') {
            i++;
        } else if (line[i] == '"') {
            return i + 1;
        }
    }
    return position;
}

bool parseArrow(std::string_view line, size_t& position, ArrowSpec& arrow,
                std::string& error,
                std::unordered_set<std::string>* edgeIds = nullptr) {
    skipSpaces(line, position);

    // Mermaid 11 edge IDs ("A e1@--> B") name the edge for styling and
    // animation; the name itself has nothing to draw
    {
        size_t probe = position;
        while (probe < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[probe])) ||
                line[probe] == '_')) {
            probe++;
        }
        if (probe > position && probe + 1 < line.size() &&
            line[probe] == '@' &&
            (line[probe + 1] == '-' || line[probe + 1] == '=' ||
             line[probe + 1] == '.')) {
            if (edgeIds) {
                edgeIds->insert(
                    std::string(line.substr(position, probe - position)));
            }
            position = probe + 1;
        }
    }

    Link link = linkAt(line, position);
    if (link.unsupported) {
        error = "Unsupported edge syntax";
        return false;
    }
    if (link.length > 0) {
        position += link.length;
    } else if (opensLinkText(line, position)) {
        // "-- text -->", "== text ==>", "-. text .->": the text runs to the
        // first link of its own kind; a quoted text may hold anything, an
        // arrow included
        const char kind = line[position + 1];
        size_t textStart = position + 2;
        skipSpaces(line, textStart);
        size_t textEnd = skipQuotedString(line, textStart);
        for (; textEnd < line.size(); textEnd++) {
            link = linkAt(line, textEnd, kind == '.');
            if (link.length > 0 && (kind == '.' || (!link.dashed &&
                                                    (link.strokeScale > 1.0f) == (kind == '=')))) {
                break;
            }
        }
        if (textEnd >= line.size()) {
            error = "Unsupported edge syntax";
            return false;
        }
        arrow.label = decodeEdgeLabel(line.substr(position + 2, textEnd - position - 2));
        position = textEnd + link.length;
    } else {
        error = "Expected a Mermaid edge";
        return false;
    }
    arrow.directed = link.directed;
    arrow.dashed = link.dashed;
    arrow.strokeScale = link.strokeScale;

    skipSpaces(line, position);
    if (position < line.size() && line[position] == '|') {
        size_t labelEnd = line.find('|', position + 1);
        if (labelEnd == std::string_view::npos) {
            error = "Unterminated edge label";
            return false;
        }
        arrow.label = decodeEdgeLabel(line.substr(position + 1, labelEnd - position - 1));
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
    // YAML front matter (#261) is blanked, its line breaks kept, so error
    // lines still count from the top of the fence
    const size_t frontMatter = static_cast<size_t>(
        mermaidext::skipFrontMatter(source).data() - source.data());
    for (size_t i = 0; i < frontMatter; i++) {
        if (normalized[i] != '\n') normalized[i] = ' ';
    }
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
            // A double-quoted string runs on to its closing quote, as in
            // Mermaid, and its line breaks are the label's (#281): the
            // statement continues on the next line
            if (quote == '"') {
                c = kQuotedLineBreak;
                continue;
            }
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
        // An asymmetric node's unquoted label keeps its ';' and '|', as a
        // bracketed one does
        if (c == '>' && squareDepth == 0 && roundDepth == 0 && curlyDepth == 0) {
            size_t close = asymmetricLabelEnd(normalized, i);
            if (close != std::string_view::npos) {
                i = close;
                continue;
            }
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
    std::unordered_set<std::string> edgeIds;
    std::vector<size_t> subgraphStack;
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
                    if (!id.empty() &&
                        edgeIds.find(std::string(id)) == edgeIds.end()) {
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
            } else if (equalsIgnoreCase(keyword, "subgraph")) {
                std::string_view rest = trim(line.substr(position));
                if (rest.empty()) {
                    return fail(std::move(result), lineNumber,
                                "Expected a subgraph name");
                }
                Subgraph subgraph;
                size_t bracket = rest.find('[');
                if (bracket != std::string_view::npos && rest.back() == ']') {
                    subgraph.id = std::string(trim(rest.substr(0, bracket)));
                    std::string_view label = trim(rest.substr(
                        bracket + 1, rest.size() - bracket - 2));
                    if (label.size() >= 2 &&
                        (label.front() == '"' || label.front() == '\'') &&
                        label.back() == label.front()) {
                        label = label.substr(1, label.size() - 2);
                    }
                    subgraph.label = decodeLabel(label);
                } else {
                    subgraph.id = std::string(rest);
                    subgraph.label = decodeLabel(rest);
                }
                subgraph.parent = subgraphStack.empty() ? SIZE_MAX
                                                        : subgraphStack.back();
                result.diagram.subgraphs.push_back(std::move(subgraph));
                subgraphStack.push_back(result.diagram.subgraphs.size() - 1);
            } else if (equalsIgnoreCase(keyword, "end")) {
                if (subgraphStack.empty()) {
                    return fail(std::move(result), lineNumber,
                                "Unmatched subgraph end");
                }
                subgraphStack.pop_back();
            } else if (equalsIgnoreCase(keyword, "direction")) {
                // A subgraph's own direction (#326). Outside a subgraph the
                // header gives the direction and the statement is passed over
                std::string_view value = readWord(line, position);
                if (!subgraphStack.empty() && !value.empty()) {
                    auto& owner =
                        result.diagram.subgraphs[subgraphStack.back()];
                    if (!parseDirection(value, owner.direction)) {
                        return fail(std::move(result), lineNumber,
                                    "Unsupported flowchart direction");
                    }
                    owner.hasDirection = true;
                }
            } else if (equalsIgnoreCase(keyword, "click") ||
                       equalsIgnoreCase(keyword, "linkStyle")) {
                // Click handlers and edge styling have nothing to draw
                // natively; the diagram itself still renders
            } else if (std::string configName; [&] {
                           // Mermaid 11 config: "e1@{ animate: true }" for
                           // edges, "A@{ img: ..., label: ... }" for nodes
                           size_t at = line.find("@{");
                           if (at == std::string_view::npos ||
                               line.back() != '}') {
                               return false;
                           }
                           std::string_view name = trim(line.substr(0, at));
                           if (name.empty()) return false;
                           for (char c : name) {
                               if (!std::isalnum(
                                       static_cast<unsigned char>(c)) &&
                                   c != '_') {
                                   return false;
                               }
                           }
                           configName = std::string(name);
                           return true;
                       }()) {
                // Edge configs are presentation only; node configs keep the
                // node, with its label when one is given (images and fancy
                // shapes degrade to a plain box)
                if (edgeIds.find(configName) == edgeIds.end()) {
                    std::string label = configName;
                    size_t labelKey = line.find("label");
                    if (labelKey != std::string_view::npos) {
                        size_t quoteStart = line.find('"', labelKey);
                        size_t quoteEnd = quoteStart == std::string_view::npos
                            ? std::string_view::npos
                            : line.find('"', quoteStart + 1);
                        if (quoteEnd != std::string_view::npos) {
                            label = decodeLabel(
                                line.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
                        }
                    }
                    size_t nodesBefore = result.diagram.nodes.size();
                    NodeSpec spec;
                    spec.id = configName;
                    spec.label = label;
                    spec.hasDefinition = label != configName;
                    spec.sourceOffset = lineOffset;
                    ensureNode(result.diagram, nodeIds, spec);
                    if (!subgraphStack.empty()) {
                        auto& owner =
                            result.diagram.subgraphs[subgraphStack.back()];
                        for (size_t i = nodesBefore;
                             i < result.diagram.nodes.size(); i++) {
                            owner.nodes.push_back(i);
                        }
                    }
                }
            } else {
                position = 0;
                size_t nodesBefore = result.diagram.nodes.size();
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
                    if (!parseArrow(line, position, arrow, error, &edgeIds)) {
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

                // Nodes first defined inside an open subgraph belong to it
                if (!subgraphStack.empty()) {
                    auto& owner =
                        result.diagram.subgraphs[subgraphStack.back()];
                    for (size_t i = nodesBefore;
                         i < result.diagram.nodes.size(); i++) {
                        owner.nodes.push_back(i);
                    }
                }
            }
        }

        if (lineEnd == source.size()) break;
        // Lines a quoted string ran over still count for the error lines
        lineNumber += static_cast<size_t>(std::count(
            source.begin() + lineOffset, source.begin() + lineEnd, kQuotedLineBreak));
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

namespace {

constexpr int kNoGroup = -1;
constexpr float kNoBox = std::numeric_limits<float>::max();

bool isVertical(Direction direction) {
    return direction == Direction::TopToBottom ||
           direction == Direction::BottomToTop;
}

Rect emptyRect() {
    return {kNoBox, kNoBox, -kNoBox, -kNoBox};
}

void grow(Rect& box, const Rect& rect) {
    box.left = std::min(box.left, rect.left);
    box.top = std::min(box.top, rect.top);
    box.right = std::max(box.right, rect.right);
    box.bottom = std::max(box.bottom, rect.bottom);
}

void shift(Rect& rect, float dx, float dy) {
    rect.left += dx;
    rect.top += dy;
    rect.right += dx;
    rect.bottom += dy;
}

// A gap widened to what group boxes need. Pads that add up to the gap but
// for rounding leave it, and with it the layout, as it was
float atLeast(float gap, float needed) {
    return needed > gap + 0.01f ? needed : gap;
}

// The box of a subgraph that is not a block: around its own nodes and the
// boxes of the subgraphs in it, which come later and are boxed already
Rect groupBox(const Diagram& diagram, size_t subgraph,
              const std::vector<Rect>& nodes, const std::vector<Rect>& groups,
              const LayoutOptions& options) {
    Rect box = emptyRect();
    for (size_t node : diagram.subgraphs[subgraph].nodes) {
        if (node < nodes.size()) grow(box, nodes[node]);
    }
    for (size_t child = subgraph + 1; child < diagram.subgraphs.size(); child++) {
        if (diagram.subgraphs[child].parent == subgraph &&
            child < groups.size() && !isEmpty(groups[child])) {
            grow(box, groups[child]);
        }
    }
    if (isEmpty(box)) return box;
    box.left -= options.groupPad;
    box.top -= options.groupPad + options.groupTitle;
    box.right += options.groupPad;
    box.bottom += options.groupPad;
    return box;
}

// What the edges of one layout share: the lanes taken beside the nodes and
// the labels set down so far
struct EdgeLanes {
    size_t taken = 0;
    std::vector<Rect> labels;
};

// What a rank of a layout covers along the layout's direction
struct RankBand {
    float low = kNoBox;
    float high = -kNoBox;
    bool block = false;  // a block is among its items
};

// Where an edge to the next rank bends: half way between its two nodes. A
// block is far longer than a node of its rank, and half way from that node
// can lie beside the block, the edge then running through it; the bend
// moves into the gap between the two ranks
float clearBend(float bend, const std::vector<RankBand>* bands, size_t fromRank,
                size_t toRank, float margin) {
    if (!bands || fromRank >= bands->size() || toRank >= bands->size()) {
        return bend;
    }
    const RankBand& from = (*bands)[fromRank];
    const RankBand& to = (*bands)[toRank];
    if (!from.block && !to.block) return bend;
    float low = from.high <= to.low ? from.high : to.high;
    float high = from.high <= to.low ? to.low : from.low;
    if (high - low <= margin * 2.0f) return (low + high) * 0.5f;
    return std::min(std::max(bend, low + margin), high - margin);
}

// One edge between two nodes of a layout that runs along `direction`
EdgeRoute routeEdge(const Diagram& diagram, size_t edgeIndex, Direction direction,
                    const std::vector<Rect>& nodes,
                    const std::vector<size_t>& ranks,
                    const LayoutOptions& options, EdgeLanes& lanes,
                    const std::vector<RankBand>* bands = nullptr) {
    const Edge& edge = diagram.edges[edgeIndex];
    const Rect& from = nodes[edge.from];
    const Rect& to = nodes[edge.to];
    EdgeRoute route;

    // A node's left side at mid-height, where a notch has its tip
    auto leftSide = [&](size_t node) {
        const Rect& rect = nodes[node];
        return rect.left + leftNotch(diagram.nodes[node].shape,
                                     rect.bottom - rect.top);
    };
    auto nextLane = [&] {
        return (options.laneStart + lanes.taken++ * options.laneStep) *
               options.laneScale;
    };

    float fromCenterX = (from.left + from.right) * 0.5f;
    float fromCenterY = (from.top + from.bottom) * 0.5f;
    float toCenterX = (to.left + to.right) * 0.5f;
    float toCenterY = (to.top + to.bottom) * 0.5f;
    bool selfLoop = edge.from == edge.to;

    // Edges that skip over intermediate ranks would cut straight through
    // the nodes between them (and drop their label onto whatever edge
    // happens to sit at the midpoint) — route those through an exterior
    // lane like back-edges instead
    bool skipsRanks = false;
    if (edge.from < ranks.size() && edge.to < ranks.size()) {
        size_t fromRank = ranks[edge.from];
        size_t toRank = ranks[edge.to];
        skipsRanks = (fromRank < toRank ? toRank - fromRank
                                        : fromRank - toRank) > 1;
    }

    if (isVertical(direction)) {
        bool topToBottom = direction == Direction::TopToBottom;
        bool forward = !selfLoop && !skipsRanks &&
            (topToBottom ? toCenterY > fromCenterY : toCenterY < fromCenterY);
        if (selfLoop) {
            float lane = nextLane();
            Point start{from.right, fromCenterY};
            Point end{fromCenterX, from.bottom};
            float laneX = from.right + lane;
            float laneY = from.bottom + lane;
            route.points = {
                start,
                {laneX, start.y},
                {laneX, laneY},
                {end.x, laneY},
                end,
            };
        } else if (forward) {
            Point start{fromCenterX, topToBottom ? from.bottom : from.top};
            Point end{toCenterX, topToBottom ? to.top : to.bottom};
            float middleY = clearBend((start.y + end.y) * 0.5f, bands,
                                      ranks[edge.from], ranks[edge.to],
                                      options.groupGap);
            route.points = {
                start,
                {start.x, middleY},
                {end.x, middleY},
                end,
            };
        } else {
            float lane = nextLane();
            Point start{from.right, fromCenterY};
            Point end{to.right, toCenterY};
            float laneX = std::max(from.right, to.right) + lane;
            route.points = {
                start,
                {laneX, start.y},
                {laneX, end.y},
                end,
            };
        }
    } else {
        bool leftToRight = direction == Direction::LeftToRight;
        bool forward = !selfLoop && !skipsRanks &&
            (leftToRight ? toCenterX > fromCenterX : toCenterX < fromCenterX);
        if (selfLoop) {
            float lane = nextLane();
            Point start{fromCenterX, from.bottom};
            Point end{from.right, fromCenterY};
            float laneX = from.right + lane;
            float laneY = from.bottom + lane;
            route.points = {
                start,
                {start.x, laneY},
                {laneX, laneY},
                {laneX, end.y},
                end,
            };
        } else if (forward) {
            Point start{leftToRight ? from.right : leftSide(edge.from), fromCenterY};
            Point end{leftToRight ? leftSide(edge.to) : to.right, toCenterY};
            float middleX = clearBend((start.x + end.x) * 0.5f, bands,
                                      ranks[edge.from], ranks[edge.to],
                                      options.groupGap);
            route.points = {
                start,
                {middleX, start.y},
                {middleX, end.y},
                end,
            };
        } else {
            float lane = nextLane();
            Point start{fromCenterX, from.bottom};
            Point end{toCenterX, to.bottom};
            float laneY = std::max(from.bottom, to.bottom) + lane;
            route.points = {
                start,
                {start.x, laneY},
                {end.x, laneY},
                end,
            };
        }
    }

    if (edgeIndex >= options.edgeLabels.size()) return route;
    const Size& label = options.edgeLabels[edgeIndex];
    if (label.width <= 0.0f && label.height <= 0.0f) return route;

    size_t middle = route.points.size() / 2;
    const Point& middleStart = route.points[middle - 1];
    const Point& middleEnd = route.points[middle];
    float centerX = (middleStart.x + middleEnd.x) * 0.5f;
    float centerY = (middleStart.y + middleEnd.y) * 0.5f;
    auto chipAt = [&](float cx, float cy) {
        return Rect{cx - label.width * 0.5f, cy - label.height * 0.5f,
                    cx + label.width * 0.5f, cy + label.height * 0.5f};
    };
    Rect labelRect = chipAt(centerX, centerY);

    // Parallel exterior lanes sit only a few pixels apart, so their
    // midpoint chips stack on top of each other — slide an overlapping
    // chip along its own segment until it finds space
    bool segVertical = std::abs(middleEnd.x - middleStart.x) <
                       std::abs(middleEnd.y - middleStart.y);
    float stepX = segVertical ? 0.0f : label.width + options.labelStep;
    float stepY = segVertical ? label.height + options.labelStep : 0.0f;
    auto overlapsPlaced = [&](const Rect& rect) {
        for (const auto& placed : lanes.labels) {
            if (rect.left < placed.right && rect.right > placed.left &&
                rect.top < placed.bottom && rect.bottom > placed.top) {
                return true;
            }
        }
        return false;
    };
    for (int attempt = 1; attempt <= 8 && overlapsPlaced(labelRect); attempt++) {
        float side = (attempt % 2 == 1) ? 1.0f : -1.0f;
        float magnitude = static_cast<float>((attempt + 1) / 2);
        labelRect = chipAt(centerX + stepX * side * magnitude,
                           centerY + stepY * side * magnitude);
    }
    lanes.labels.push_back(labelRect);
    route.hasLabel = true;
    route.label = labelRect;
    return route;
}

// A flowchart is one layout, or several: a subgraph that has a direction of
// its own and no edge across its border is a block (#326). A block is laid
// out first, on its own and along its direction, and then takes part in the
// layout around it as one item of the size of its group box. A subgraph
// with an edge to the outside is none: Mermaid has it take its parent's
// direction, and its nodes are ranked with the nodes around it.
class FlowLayout {
public:
    FlowLayout(const Diagram& diagram, const std::vector<Size>& nodeSizes,
               const LayoutOptions& options, Layout& result)
        : diagram(diagram), nodeSizes(nodeSizes), options(options),
          result(result) {}

    void run() {
        const size_t nodeCount = diagram.nodes.size();
        const size_t groupCount = diagram.subgraphs.size();
        findBlocks();
        result.nodes.assign(nodeCount, Rect{});
        result.ranks.assign(nodeCount, 0);
        result.directions.assign(nodeCount, diagram.direction);
        result.scopes.assign(nodeCount, 0);
        for (size_t node = 0; node < nodeCount; node++) {
            result.scopes[node] = static_cast<size_t>(nodeScope[node] + 1);
        }
        result.blocks = block;
        result.groups.assign(groupCount, emptyRect());
        result.groupRanks.assign(groupCount, 0);
        result.groupScopes.assign(groupCount, 0);
        for (size_t s = 0; s < groupCount; s++) {
            result.groupScopes[s] = static_cast<size_t>(groupScope[s] + 1);
        }
        blockBounds.assign(groupCount, Rect{});
        // Inner blocks come later and are laid out first
        for (size_t s = groupCount; s-- > 0;) {
            if (block[s]) layoutScope(static_cast<int>(s));
        }
        layoutScope(kNoGroup);
    }

private:
    // A node, or a block, of one layout
    struct Item {
        size_t key = 0;        // where it sorts: a node's index, a block's first node
        size_t index = 0;      // the node, or the block's subgraph
        bool block = false;
        int group = kNoGroup;  // innermost subgraph around it in this layout
        float width = 0.0f;
        float height = 0.0f;
    };

    const Diagram& diagram;
    const std::vector<Size>& nodeSizes;
    const LayoutOptions& options;
    Layout& result;

    std::vector<int> groupOf;        // innermost subgraph of a node
    std::vector<int> parentOf;       // the subgraph around a subgraph
    std::vector<bool> block;
    std::vector<int> nodeScope;      // the block a node is laid out in
    std::vector<int> groupScope;     // the block around a subgraph
    std::vector<size_t> firstNode;   // lowest node in a subgraph, nested ones included
    std::vector<Rect> blockBounds;   // all a block draws, in its own space

    bool holds(int group, size_t node) const {
        for (int g = groupOf[node]; g != kNoGroup; g = parentOf[g]) {
            if (g == group) return true;
        }
        return false;
    }

    bool isInside(size_t subgraph, int group) const {
        for (int g = parentOf[subgraph]; g != kNoGroup; g = parentOf[g]) {
            if (g == group) return true;
        }
        return false;
    }

    void findBlocks() {
        const size_t nodeCount = diagram.nodes.size();
        const size_t groupCount = diagram.subgraphs.size();
        groupOf.assign(nodeCount, kNoGroup);
        parentOf.assign(groupCount, kNoGroup);
        for (size_t s = 0; s < groupCount; s++) {
            // A subgraph is declared inside its parent, so after it
            if (diagram.subgraphs[s].parent < s) {
                parentOf[s] = static_cast<int>(diagram.subgraphs[s].parent);
            }
            for (size_t node : diagram.subgraphs[s].nodes) {
                if (node < nodeCount) groupOf[node] = static_cast<int>(s);
            }
        }

        std::vector<bool> populated(groupCount, false);
        std::vector<bool> crossed(groupCount, false);
        firstNode.assign(groupCount, SIZE_MAX);
        for (size_t node = 0; node < nodeCount; node++) {
            for (int g = groupOf[node]; g != kNoGroup; g = parentOf[g]) {
                populated[g] = true;
                firstNode[g] = std::min(firstNode[g], node);
            }
        }
        for (const auto& edge : diagram.edges) {
            if (edge.from >= nodeCount || edge.to >= nodeCount) continue;
            for (int g = groupOf[edge.from]; g != kNoGroup; g = parentOf[g]) {
                if (!holds(g, edge.to)) crossed[g] = true;
            }
            for (int g = groupOf[edge.to]; g != kNoGroup; g = parentOf[g]) {
                if (!holds(g, edge.from)) crossed[g] = true;
            }
        }

        block.assign(groupCount, false);
        for (size_t s = 0; s < groupCount; s++) {
            block[s] = diagram.subgraphs[s].hasDirection && populated[s] &&
                       !crossed[s];
        }
        auto nearestBlock = [&](int group) {
            while (group != kNoGroup && !block[group]) group = parentOf[group];
            return group;
        };
        nodeScope.assign(nodeCount, kNoGroup);
        for (size_t node = 0; node < nodeCount; node++) {
            nodeScope[node] = nearestBlock(groupOf[node]);
        }
        groupScope.assign(groupCount, kNoGroup);
        for (size_t s = 0; s < groupCount; s++) {
            groupScope[s] = nearestBlock(parentOf[s]);
        }
    }

    // Lays out the diagram (kNoGroup) or one block. The blocks inside it
    // are laid out already
    void layoutScope(int scope) {
        const size_t nodeCount = diagram.nodes.size();
        const size_t groupCount = diagram.subgraphs.size();
        const Direction direction = scope == kNoGroup
            ? diagram.direction
            : diagram.subgraphs[scope].direction;
        const bool vertical = isVertical(direction);
        const float nodeGap = options.nodeGap;

        std::vector<Item> items;
        for (size_t node = 0; node < nodeCount; node++) {
            if (nodeScope[node] != scope) continue;
            Item item;
            item.key = node;
            item.index = node;
            item.group = groupOf[node] == scope ? kNoGroup : groupOf[node];
            item.width = std::max(1.0f, nodeSizes[node].width);
            item.height = std::max(1.0f, nodeSizes[node].height);
            items.push_back(item);
        }
        bool hasBlocks = false;
        for (size_t s = 0; s < groupCount; s++) {
            if (!block[s] || groupScope[s] != scope) continue;
            Item item;
            item.key = firstNode[s];
            item.index = s;
            item.block = true;
            item.group = parentOf[s] == scope ? kNoGroup : parentOf[s];
            const Rect& bounds = blockBounds[s];
            item.width = bounds.right - bounds.left + options.groupPad * 2.0f;
            if (s < options.groupTitles.size()) {
                item.width = std::max(item.width, options.groupTitles[s]);
            }
            item.height = bounds.bottom - bounds.top + options.groupPad * 2.0f +
                          options.groupTitle;
            items.push_back(item);
            hasBlocks = true;
        }
        if (hasBlocks) {
            std::sort(items.begin(), items.end(),
                      [](const Item& left, const Item& right) {
                          return left.key < right.key;
                      });
        }
        const size_t itemCount = items.size();
        if (itemCount == 0) return;

        std::vector<size_t> itemOf(nodeCount, SIZE_MAX);
        for (size_t i = 0; i < itemCount; i++) {
            if (!items[i].block) itemOf[items[i].index] = i;
        }

        // The edges of this layout. An edge never leaves it: one that did
        // would cross a block's border, and the block would be none
        std::vector<size_t> scopeEdges;
        std::vector<std::vector<size_t>> outgoing(itemCount);
        std::vector<std::vector<size_t>> incoming(itemCount);
        std::vector<size_t> indegree(itemCount, 0);
        for (size_t e = 0; e < diagram.edges.size(); e++) {
            const auto& edge = diagram.edges[e];
            if (edge.from >= nodeCount || edge.to >= nodeCount) continue;
            if (nodeScope[edge.from] != scope || nodeScope[edge.to] != scope) {
                continue;
            }
            size_t from = itemOf[edge.from];
            size_t to = itemOf[edge.to];
            outgoing[from].push_back(to);
            incoming[to].push_back(from);
            indegree[to]++;
            scopeEdges.push_back(e);
        }

        std::deque<size_t> ready;
        for (size_t i = 0; i < itemCount; i++) {
            if (indegree[i] == 0) ready.push_back(i);
        }

        std::vector<size_t> rank(itemCount, 0);
        std::vector<bool> processed(itemCount, false);
        size_t maxRank = 0;
        while (!ready.empty()) {
            size_t item = ready.front();
            ready.pop_front();
            processed[item] = true;
            maxRank = std::max(maxRank, rank[item]);

            for (size_t target : outgoing[item]) {
                rank[target] = std::max(rank[target], rank[item] + 1);
                if (--indegree[target] == 0) ready.push_back(target);
            }
        }

        bool processedAny = std::any_of(
            processed.begin(), processed.end(), [](bool value) { return value; });
        size_t nextRank = processedAny ? maxRank + 1 : 0;
        for (size_t i = 0; i < itemCount; i++) {
            if (!processed[i]) rank[i] = nextRank++;
        }
        for (size_t value : rank) maxRank = std::max(maxRank, value);

        std::vector<std::vector<size_t>> ranks(maxRank + 1);
        for (size_t i = 0; i < itemCount; i++) ranks[rank[i]].push_back(i);

        // Subgraph members must stay adjacent within a rank so their group
        // box encloses them without swallowing strangers
        std::vector<float> order(itemCount, 0.0f);
        for (size_t level = 0; level < ranks.size(); level++) {
            if (level > 0) {
                std::stable_sort(ranks[level].begin(), ranks[level].end(),
                    [&](size_t left, size_t right) {
                        auto barycenter = [&](size_t item) {
                            if (incoming[item].empty()) {
                                return static_cast<float>(items[item].key);
                            }
                            float total = 0.0f;
                            size_t count = 0;
                            for (size_t parent : incoming[item]) {
                                if (rank[parent] < level) {
                                    total += order[parent];
                                    count++;
                                }
                            }
                            return count > 0 ? total / static_cast<float>(count)
                                             : static_cast<float>(items[item].key);
                        };
                        return barycenter(left) < barycenter(right);
                    });
            }
            if (groupCount > 0 && ranks[level].size() > 1) {
                std::vector<float> position(itemCount, 0.0f);
                for (size_t i = 0; i < ranks[level].size(); i++) {
                    position[ranks[level][i]] = static_cast<float>(i);
                }
                std::vector<float> groupTotal(groupCount, 0.0f);
                std::vector<int> groupSize(groupCount, 0);
                for (size_t item : ranks[level]) {
                    int group = items[item].group;
                    if (group >= 0) {
                        groupTotal[group] += position[item];
                        groupSize[group]++;
                    }
                }
                auto sortKey = [&](size_t item) {
                    int group = items[item].group;
                    if (group < 0 || groupSize[group] == 0) {
                        return position[item];
                    }
                    return groupTotal[group] / groupSize[group];
                };
                std::stable_sort(ranks[level].begin(), ranks[level].end(),
                    [&](size_t left, size_t right) {
                        return sortKey(left) < sortKey(right);
                    });
            }
            for (size_t i = 0; i < ranks[level].size(); i++) {
                order[ranks[level][i]] = static_cast<float>(i);
            }
        }

        // A rank gap is at least as long as the labels that sit in it
        float rankGap = options.rankGap;
        for (size_t e : scopeEdges) {
            if (e >= options.edgeLabels.size()) break;
            const Size& label = options.edgeLabels[e];
            if (label.width <= 0.0f && label.height <= 0.0f) continue;
            float extent = vertical ? label.height : label.width;
            rankGap = std::max(rankGap, extent + options.labelRankGap);
        }

        // Group boxes reach past their members by a pad, and by a title
        // strip above them. Two items a box border runs between are set
        // that much further apart, so the boxes of neighbours stay clear of
        // each other: nodeGap alone is less than a pad, a title and a pad.
        // (Ranks are still centred one by one, so two groups whose members
        // spread unevenly over the ranks can meet.)
        std::vector<std::vector<int>> chains(itemCount);
        std::vector<size_t> firstRank(groupCount, SIZE_MAX);
        std::vector<size_t> lastRank(groupCount, 0);
        for (size_t i = 0; i < itemCount; i++) {
            for (int g = items[i].group; g != kNoGroup && g != scope;
                 g = parentOf[g]) {
                chains[i].push_back(g);
                firstRank[g] = std::min(firstRank[g], rank[i]);
                lastRank[g] = std::max(lastRank[g], rank[i]);
            }
        }
        auto bordersBetween = [&](size_t item, size_t other) {
            int borders = 0;
            for (int g : chains[item]) {
                if (std::find(chains[other].begin(), chains[other].end(), g) ==
                    chains[other].end()) {
                    borders++;
                }
            }
            return borders;
        };
        // Along a rank: the later item's boxes show their left side, or in
        // a layout that runs sideways their top with its title
        const float alongAfter = options.groupPad;
        const float alongBefore =
            options.groupPad + (vertical ? 0.0f : options.groupTitle);
        // From rank to rank: where ranks run downwards a box that ends shows
        // its bottom and one that starts its top; upwards it is the reverse
        float acrossEnd = options.groupPad;
        float acrossStart = options.groupPad;
        if (direction == Direction::TopToBottom) {
            acrossStart += options.groupTitle;
        } else if (direction == Direction::BottomToTop) {
            acrossEnd += options.groupTitle;
        }

        std::vector<std::vector<float>> gapAfter(ranks.size());
        std::vector<float> rankGapAfter(ranks.size(), rankGap);
        for (size_t level = 0; level < ranks.size(); level++) {
            const auto& row = ranks[level];
            gapAfter[level].assign(row.size(), nodeGap);
            for (size_t i = 0; i + 1 < row.size(); i++) {
                int ends = bordersBetween(row[i], row[i + 1]);
                int starts = bordersBetween(row[i + 1], row[i]);
                if (ends + starts == 0) continue;
                gapAfter[level][i] = atLeast(
                    nodeGap, static_cast<float>(ends) * alongAfter +
                                 static_cast<float>(starts) * alongBefore +
                                 options.groupGap);
            }
            if (level + 1 == ranks.size()) continue;
            int ends = 0;
            int starts = 0;
            for (size_t item : row) {
                int count = 0;
                for (int g : chains[item]) {
                    if (lastRank[g] == level) count++;
                }
                ends = std::max(ends, count);
            }
            for (size_t item : ranks[level + 1]) {
                int count = 0;
                for (int g : chains[item]) {
                    if (firstRank[g] == level + 1) count++;
                }
                starts = std::max(starts, count);
            }
            if (ends + starts == 0) continue;
            rankGapAfter[level] = atLeast(
                rankGap, static_cast<float>(ends) * acrossEnd +
                             static_cast<float>(starts) * acrossStart +
                             options.groupGap);
        }

        // Ranks follow each other along the direction; the items of a rank
        // sit side by side across it, centred on the widest rank
        auto along = [&](const Item& item) {
            return vertical ? item.width : item.height;
        };
        auto across = [&](const Item& item) {
            return vertical ? item.height : item.width;
        };
        std::vector<float> rankLength(ranks.size(), 0.0f);
        std::vector<float> rankDepth(ranks.size(), 0.0f);
        float longest = 0.0f;
        for (size_t level = 0; level < ranks.size(); level++) {
            float widened = 0.0f;
            for (size_t i = 0; i < ranks[level].size(); i++) {
                const Item& item = items[ranks[level][i]];
                rankLength[level] += along(item);
                rankDepth[level] = std::max(rankDepth[level], across(item));
                widened += gapAfter[level][i] - nodeGap;
            }
            if (ranks[level].size() > 1) {
                rankLength[level] +=
                    nodeGap * static_cast<float>(ranks[level].size() - 1);
            }
            if (widened > 0.0f) rankLength[level] += widened;
            longest = std::max(longest, rankLength[level]);
        }

        std::vector<Rect> rects(itemCount);
        float depth = 0.0f;
        for (size_t level = 0; level < ranks.size(); level++) {
            float offset = (longest - rankLength[level]) * 0.5f;
            for (size_t i = 0; i < ranks[level].size(); i++) {
                size_t index = ranks[level][i];
                const Item& item = items[index];
                float centred = depth + (rankDepth[level] - across(item)) * 0.5f;
                if (vertical) {
                    rects[index] = {offset, centred, offset + item.width,
                                    centred + item.height};
                } else {
                    rects[index] = {centred, offset, centred + item.width,
                                    offset + item.height};
                }
                offset += along(item) + gapAfter[level][i];
            }
            depth += rankDepth[level];
            if (level + 1 < ranks.size()) depth += rankGapAfter[level];
        }
        const float width = vertical ? longest : depth;
        const float height = vertical ? depth : longest;

        if (direction == Direction::BottomToTop) {
            for (auto& rect : rects) {
                float oldTop = rect.top;
                rect.top = height - rect.bottom;
                rect.bottom = height - oldTop;
            }
        } else if (direction == Direction::RightToLeft) {
            for (auto& rect : rects) {
                float oldLeft = rect.left;
                rect.left = width - rect.right;
                rect.right = width - oldLeft;
            }
        }

        for (size_t i = 0; i < itemCount; i++) {
            const Item& item = items[i];
            if (!item.block) {
                result.nodes[item.index] = rects[i];
                result.ranks[item.index] = rank[i];
                result.directions[item.index] = direction;
                continue;
            }
            // The block's box is the item; what it holds moves inside it,
            // to the middle of a box that its title made wider
            const Rect& bounds = blockBounds[item.index];
            float spare = item.width -
                          (bounds.right - bounds.left + options.groupPad * 2.0f);
            float dx = rects[i].left + spare * 0.5f + options.groupPad -
                       bounds.left;
            float dy = rects[i].top + options.groupPad + options.groupTitle -
                       bounds.top;
            const int moved = static_cast<int>(item.index);
            for (size_t node = 0; node < nodeCount; node++) {
                if (holds(moved, node)) shift(result.nodes[node], dx, dy);
            }
            for (size_t s = 0; s < groupCount; s++) {
                if (isInside(s, moved) && !isEmpty(result.groups[s])) {
                    shift(result.groups[s], dx, dy);
                }
            }
            result.groups[item.index] = rects[i];
            result.groupRanks[item.index] = rank[i];
        }

        Rect bounds{0.0f, 0.0f, width, height};
        for (size_t s = groupCount; s-- > 0;) {
            if (block[s] || groupScope[s] != scope) continue;
            result.groups[s] =
                groupBox(diagram, s, result.nodes, result.groups, options);
            if (!isEmpty(result.groups[s])) grow(bounds, result.groups[s]);
        }

        if (scope == kNoGroup) {
            result.width = width;
            result.height = height;
            return;
        }
        // A block's box holds its edges too: the lanes of those that turn
        // back or skip a rank, and the labels
        EdgeLanes lanes;
        for (size_t e : scopeEdges) {
            EdgeRoute route = routeEdge(diagram, e, direction, result.nodes,
                                        result.ranks, options, lanes);
            for (const Point& point : route.points) {
                grow(bounds, {point.x - options.arrow, point.y - options.arrow,
                              point.x + options.arrow, point.y + options.arrow});
            }
            if (route.hasLabel) grow(bounds, route.label);
        }
        blockBounds[scope] = bounds;
    }
};

} // namespace

LayoutOptions drawingOptions(float scale) {
    LayoutOptions options;
    options.nodeGap = 32.0f * scale;
    options.rankGap = 78.0f * scale;
    options.groupPad = 10.0f * scale;
    options.groupTitle = 20.0f * scale;
    options.groupGap = 12.0f * scale;
    options.labelRankGap = 20.0f * scale;
    options.labelStep = 8.0f * scale;
    options.laneStart = 36.0f;
    options.laneStep = 14.0f;
    options.laneScale = scale;
    options.arrow = 8.0f * scale;
    return options;
}

Layout layout(const Diagram& diagram, const std::vector<Size>& nodeSizes,
              const LayoutOptions& given) {
    Layout result;
    const size_t nodeCount = diagram.nodes.size();
    if (nodeCount == 0 || nodeSizes.size() != nodeCount) return result;

    LayoutOptions options = given;
    options.nodeGap = std::max(0.0f, options.nodeGap);
    options.rankGap = std::max(0.0f, options.rankGap);

    FlowLayout(diagram, nodeSizes, options, result).run();
    return result;
}

Layout layout(const Diagram& diagram, const std::vector<Size>& nodeSizes,
              float nodeGap, float rankGap) {
    LayoutOptions options;
    options.nodeGap = nodeGap;
    options.rankGap = rankGap;
    return layout(diagram, nodeSizes, options);
}

Placement place(const Diagram& diagram, const Layout& layout,
                const LayoutOptions& options, float originX, float originY) {
    Placement placed;
    const size_t nodeCount = diagram.nodes.size();
    const size_t groupCount = diagram.subgraphs.size();
    if (layout.nodes.size() != nodeCount || layout.ranks.size() != nodeCount ||
        layout.scopes.size() != nodeCount ||
        layout.directions.size() != nodeCount ||
        layout.blocks.size() != groupCount ||
        layout.groups.size() != groupCount ||
        layout.groupScopes.size() != groupCount ||
        layout.groupRanks.size() != groupCount) {
        return placed;
    }

    placed.nodes.reserve(nodeCount);
    for (const auto& rect : layout.nodes) {
        placed.nodes.push_back({originX + rect.left, originY + rect.top,
                                originX + rect.right, originY + rect.bottom});
    }

    // Children were declared after their parents, so a reverse pass
    // finalizes leaves first and parents grow around them
    placed.groups.assign(groupCount, emptyRect());
    for (size_t s = groupCount; s-- > 0;) {
        if (!layout.blocks[s]) {
            placed.groups[s] =
                groupBox(diagram, s, placed.nodes, placed.groups, options);
        } else if (!isEmpty(layout.groups[s])) {
            const Rect& box = layout.groups[s];
            placed.groups[s] = {originX + box.left, originY + box.top,
                                originX + box.right, originY + box.bottom};
        }
    }

    // What each rank of each layout covers, for the edges that pass a block
    std::vector<std::vector<RankBand>> bands(groupCount + 1);
    auto cover = [&](size_t scope, size_t rank, const Rect& rect, bool block) {
        if (scope >= bands.size()) return;
        Direction direction = scope == 0 ? diagram.direction
                                         : diagram.subgraphs[scope - 1].direction;
        if (bands[scope].size() <= rank) bands[scope].resize(rank + 1);
        RankBand& band = bands[scope][rank];
        band.low = std::min(band.low, isVertical(direction) ? rect.top : rect.left);
        band.high = std::max(band.high,
                             isVertical(direction) ? rect.bottom : rect.right);
        if (block) band.block = true;
    };
    for (size_t node = 0; node < nodeCount; node++) {
        if (node < layout.ranks.size()) {
            cover(layout.scopes[node], layout.ranks[node], placed.nodes[node], false);
        }
    }
    for (size_t s = 0; s < groupCount; s++) {
        if (layout.blocks[s] && !isEmpty(placed.groups[s])) {
            cover(layout.groupScopes[s], layout.groupRanks[s], placed.groups[s], true);
        }
    }

    placed.edges.resize(diagram.edges.size());
    std::vector<EdgeLanes> lanes(groupCount + 1);
    for (size_t e = 0; e < diagram.edges.size(); e++) {
        const auto& edge = diagram.edges[e];
        if (edge.from >= nodeCount || edge.to >= nodeCount) continue;
        size_t scope = layout.scopes[edge.from];
        if (scope >= lanes.size()) continue;
        placed.edges[e] = routeEdge(diagram, e, layout.directions[edge.from],
                                    placed.nodes, layout.ranks, options,
                                    lanes[scope], &bands[scope]);
    }
    return placed;
}

} // namespace mermaid
