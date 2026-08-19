// Shared entry points and helpers for the extended Mermaid renderers.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>

namespace mermaidext {

namespace detail {

std::string_view trimView(std::string_view value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end &&
           std::isspace(static_cast<unsigned char>(value[begin]))) {
        begin++;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        end--;
    }
    return value.substr(begin, end - begin);
}

std::vector<std::string_view> diagramLines(std::string_view source) {
    std::vector<std::string_view> lines;
    size_t position = 0;
    while (position <= source.size()) {
        size_t newline = source.find('\n', position);
        if (newline == std::string_view::npos) newline = source.size();
        std::string_view line =
            trimView(source.substr(position, newline - position));
        if (!line.empty() && line.substr(0, 2) != "%%") {
            lines.push_back(line);
        }
        if (newline == source.size()) break;
        position = newline + 1;
    }
    return lines;
}

std::string cleanLabel(std::string_view raw) {
    std::string result;
    result.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] == '<') {
            // <br>, <br/>, <br />
            size_t close = raw.find('>', i);
            if (close != std::string_view::npos && close - i <= 6) {
                std::string tag(raw.substr(i + 1, close - i - 1));
                std::transform(tag.begin(), tag.end(), tag.begin(),
                               [](unsigned char c) {
                                   return static_cast<char>(std::tolower(c));
                               });
                tag.erase(std::remove_if(tag.begin(), tag.end(),
                                         [](unsigned char c) {
                                             return c == '/' || c == ' ';
                                         }),
                          tag.end());
                if (tag == "br") {
                    result += '\n';
                    i = close + 1;
                    continue;
                }
            }
        }
        if (raw[i] == '&') {
            struct Entity {
                std::string_view name;
                char replacement;
            };
            static const Entity kEntities[] = {
                {"&amp;", '&'}, {"&lt;", '<'},   {"&gt;", '>'},
                {"&quot;", '"'}, {"&#39;", '\''}, {"&nbsp;", ' '},
            };
            bool matched = false;
            for (const auto& entity : kEntities) {
                if (raw.substr(i, entity.name.size()) == entity.name) {
                    result += entity.replacement;
                    i += entity.name.size();
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }
        result += raw[i];
        i++;
    }
    // Trim each produced line
    std::string trimmed;
    size_t start = 0;
    while (start <= result.size()) {
        size_t end = result.find('\n', start);
        if (end == std::string::npos) end = result.size();
        std::string_view line =
            trimView(std::string_view(result).substr(start, end - start));
        if (!trimmed.empty()) trimmed += '\n';
        trimmed.append(line);
        if (end == result.size()) break;
        start = end + 1;
    }
    return trimmed;
}

bool startsWithWord(std::string_view line, std::string_view word,
                    std::string_view* rest) {
    if (line.size() < word.size()) return false;
    for (size_t i = 0; i < word.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(line[i])) !=
            std::tolower(static_cast<unsigned char>(word[i]))) {
            return false;
        }
    }
    if (line.size() > word.size()) {
        char next = line[word.size()];
        if (!std::isspace(static_cast<unsigned char>(next)) && next != ':') {
            return false;
        }
    }
    if (rest) {
        std::string_view remainder = line.substr(word.size());
        if (!remainder.empty() && remainder.front() == ':') {
            remainder.remove_prefix(1);
        }
        *rest = trimView(remainder);
    }
    return true;
}

void normalizeLeft(Built& built) {
    float minX = 0.0f;
    for (const auto& prim : built.prims) {
        if (prim.type == PrimType::Slice) {
            minX = std::min(minX, prim.x1 - prim.radius);
        } else if (prim.type == PrimType::Polygon) {
            for (const auto& point : prim.pts) minX = std::min(minX, point.x);
        } else {
            minX = std::min(minX, std::min(prim.x1, prim.x2));
        }
    }
    if (minX >= 0.0f) return;
    float shift = -minX;
    for (auto& prim : built.prims) {
        prim.x1 += shift;
        prim.x2 += shift;
        for (auto& point : prim.pts) point.x += shift;
    }
    built.width += shift;
}

}  // namespace detail

Kind detectKind(std::string_view source) {
    using detail::trimView;
    size_t position = 0;
    while (position <= source.size()) {
        size_t newline = source.find('\n', position);
        if (newline == std::string_view::npos) newline = source.size();
        std::string_view line =
            trimView(source.substr(position, newline - position));
        if (!line.empty() && line.substr(0, 2) != "%%" &&
            line.substr(0, 3) != "---") {  // skip frontmatter fences too
            size_t wordEnd = 0;
            while (wordEnd < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[wordEnd])) ||
                    line[wordEnd] == '-')) {
                wordEnd++;
            }
            std::string keyword(line.substr(0, wordEnd));
            std::transform(keyword.begin(), keyword.end(), keyword.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (keyword == "graph" || keyword == "flowchart") {
                return Kind::Flowchart;
            }
            if (keyword == "sequencediagram") return Kind::Sequence;
            if (keyword == "classdiagram" || keyword == "classdiagram-v2") {
                return Kind::Class;
            }
            if (keyword == "statediagram" || keyword == "statediagram-v2") {
                return Kind::State;
            }
            if (keyword == "erdiagram") return Kind::Er;
            if (keyword == "pie") return Kind::Pie;
            if (keyword == "gitgraph") return Kind::Git;
            if (keyword == "gantt") return Kind::Gantt;
            if (keyword == "mindmap") return Kind::Mindmap;
            if (keyword == "timeline") return Kind::Timeline;
            if (keyword == "journey") return Kind::Journey;
            if (keyword == "quadrantchart") return Kind::Quadrant;
            if (keyword == "xychart" || keyword == "xychart-beta") {
                return Kind::XyChart;
            }
            return Kind::None;
        }
        if (newline == source.size()) break;
        position = newline + 1;
    }
    return Kind::None;
}

Built build(Kind kind, std::string_view source, const Measure& measure,
            float scale) {
    using namespace detail;
    switch (kind) {
        case Kind::Sequence: return buildSequence(source, measure, scale);
        case Kind::Pie: return buildPie(source, measure, scale);
        case Kind::State: return buildState(source, measure, scale);
        case Kind::Class: return buildClass(source, measure, scale);
        case Kind::Er: return buildEr(source, measure, scale);
        case Kind::Git: return buildGit(source, measure, scale);
        case Kind::Gantt: return buildGantt(source, measure, scale);
        case Kind::Mindmap: return buildMindmap(source, measure, scale);
        case Kind::Timeline: return buildTimeline(source, measure, scale);
        case Kind::Journey: return buildJourney(source, measure, scale);
        case Kind::Quadrant: return buildQuadrant(source, measure, scale);
        case Kind::XyChart: return buildXyChart(source, measure, scale);
        default: break;
    }
    Built failed;
    failed.error = "Unsupported diagram kind";
    return failed;
}

// Temporary stubs for families whose builders have not landed yet; each is
// deleted when the real implementation arrives in its own source file.
namespace detail {

static Built notYet() {
    Built failed;
    failed.error = "Not implemented yet";
    return failed;
}

Built buildGit(std::string_view, const Measure&, float) { return notYet(); }
Built buildGantt(std::string_view, const Measure&, float) { return notYet(); }
Built buildMindmap(std::string_view, const Measure&, float) { return notYet(); }
Built buildTimeline(std::string_view, const Measure&, float) { return notYet(); }
Built buildJourney(std::string_view, const Measure&, float) { return notYet(); }
Built buildQuadrant(std::string_view, const Measure&, float) { return notYet(); }
Built buildXyChart(std::string_view, const Measure&, float) { return notYet(); }

}  // namespace detail

}  // namespace mermaidext
