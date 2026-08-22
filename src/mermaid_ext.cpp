// Shared entry points and helpers for the extended Mermaid renderers.

#include "mermaid_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>

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

void emitCylinder(std::vector<Prim>& out, const Prim& paint, float x1,
                  float y1, float x2, float y2, float lidHeight) {
    constexpr float kPi = 3.14159265f;
    float rx = (x2 - x1) * 0.5f;
    float ry = lidHeight * 0.5f;
    float cx = (x1 + x2) * 0.5f;
    float topCy = y1 + ry;
    float botCy = y2 - ry;
    if (botCy < topCy) botCy = topCy;

    // Silhouette: over the top arc, down the right side, under the bottom
    // arc, back up the left side
    Prim body = paint;
    body.type = PrimType::Polygon;
    body.pts.clear();
    constexpr int kSteps = 10;
    for (int i = 0; i <= kSteps; i++) {
        float t = kPi + kPi * i / kSteps;  // left -> top -> right
        body.pts.push_back(
            {cx + rx * std::cos(t), topCy + ry * std::sin(t)});
    }
    for (int i = 0; i <= kSteps; i++) {
        float t = kPi * i / kSteps;  // right -> bottom -> left
        body.pts.push_back(
            {cx + rx * std::cos(t), botCy + ry * std::sin(t)});
    }
    out.push_back(std::move(body));

    // Lid seam: the lower half of the top ellipse, as short segments
    if (paint.stroke != Role::None) {
        for (int i = 0; i < kSteps; i++) {
            float t0 = kPi * i / kSteps;
            float t1 = kPi * (i + 1) / kSteps;
            Prim seam = paint;
            seam.type = PrimType::Line;
            seam.fill = Role::None;
            seam.pts.clear();
            seam.x1 = cx + rx * std::cos(t0);
            seam.y1 = topCy + ry * std::sin(t0);
            seam.x2 = cx + rx * std::cos(t1);
            seam.y2 = topCy + ry * std::sin(t1);
            out.push_back(std::move(seam));
        }
    }
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
            if (keyword == "packet" || keyword == "packet-beta") {
                return Kind::Packet;
            }
            if (keyword == "kanban") return Kind::Kanban;
            if (keyword == "requirementdiagram") return Kind::Requirement;
            if (keyword.rfind("c4", 0) == 0 &&
                (keyword == "c4context" || keyword == "c4container" ||
                 keyword == "c4component" || keyword == "c4dynamic" ||
                 keyword == "c4deployment")) {
                return Kind::C4;
            }
            if (keyword == "architecture" ||
                keyword == "architecture-beta") {
                return Kind::Architecture;
            }
            if (keyword == "block" || keyword == "block-beta") {
                return Kind::Block;
            }
            if (keyword == "radar" || keyword == "radar-beta") {
                return Kind::Radar;
            }
            if (keyword == "sankey" || keyword == "sankey-beta") {
                return Kind::Sankey;
            }
            if (keyword == "treemap" || keyword == "treemap-beta") {
                return Kind::Treemap;
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
        case Kind::Packet: return buildPacket(source, measure, scale);
        case Kind::Kanban: return buildKanban(source, measure, scale);
        case Kind::Treemap: return buildTreemap(source, measure, scale);
        case Kind::Radar: return buildRadar(source, measure, scale);
        case Kind::Sankey: return buildSankey(source, measure, scale);
        case Kind::Block: return buildBlock(source, measure, scale);
        case Kind::Requirement:
            return buildRequirement(source, measure, scale);
        case Kind::C4: return buildC4(source, measure, scale);
        case Kind::Architecture:
            return buildArchitecture(source, measure, scale);
        default: break;
    }
    Built failed;
    failed.error = "Unsupported diagram kind";
    return failed;
}

}  // namespace mermaidext
