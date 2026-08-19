// Native Mermaid gitGraph renderer.
//
// Left-to-right commit graph: lanes per branch (colored from the
// categorical palette), commit dots in sequence order, branch/merge
// connector elbows, id labels under the dots, tag chips above them, and
// branch name chips at the left edge. Supported statements: commit
// (id/tag/type), branch, checkout/switch, merge, cherry-pick.

#include "mermaid_ext.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace mermaidext {
namespace detail {

namespace {

constexpr float kCommitGap = 46.0f;
constexpr float kLaneGap = 44.0f;
constexpr float kDotRadius = 7.0f;

struct GitCommit {
    size_t seq = 0;
    size_t lane = 0;
    std::string id;      // displayed under the dot
    std::string tag;     // chip above the dot
    bool merge = false;
    bool highlight = false;
    size_t parent = SIZE_MAX;       // previous commit on the same lane chain
    size_t mergeParent = SIZE_MAX;  // second parent for merges
    bool dashedParent = false;      // cherry-pick source link
};

struct GitBranch {
    std::string name;
    size_t lane = 0;
    size_t tip = SIZE_MAX;
};

// key: "value" pairs after a statement keyword
std::map<std::string, std::string> parseKeyValues(std::string_view rest) {
    std::map<std::string, std::string> values;
    size_t position = 0;
    while (position < rest.size()) {
        while (position < rest.size() &&
               (rest[position] == ' ' || rest[position] == '\t')) {
            position++;
        }
        size_t colon = rest.find(':', position);
        if (colon == std::string_view::npos) break;
        std::string key(trimView(rest.substr(position, colon - position)));
        // A leading branch name ("merge featureA tag: ...") glues onto the
        // first key; the key proper is the last word
        size_t lastSpace = key.rfind(' ');
        if (lastSpace != std::string::npos) key = key.substr(lastSpace + 1);
        position = colon + 1;
        while (position < rest.size() && rest[position] == ' ') position++;
        std::string value;
        if (position < rest.size() && rest[position] == '"') {
            size_t close = rest.find('"', position + 1);
            if (close == std::string_view::npos) break;
            value = std::string(rest.substr(position + 1,
                                            close - position - 1));
            position = close + 1;
        } else {
            size_t end = rest.find(' ', position);
            if (end == std::string_view::npos) end = rest.size();
            value = std::string(rest.substr(position, end - position));
            position = end;
        }
        values[key] = std::move(value);
    }
    return values;
}

}  // namespace

Built buildGit(std::string_view source, const Measure& measure, float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::vector<GitCommit> commits;
    std::vector<GitBranch> branches;
    std::map<std::string, size_t, std::less<>> branchIds;
    std::map<std::string, size_t, std::less<>> commitIds;
    size_t current = 0;
    int anonymous = 0;

    branches.push_back({"main", 0, SIZE_MAX});
    branchIds.emplace("main", 0);

    auto addCommit = [&](std::string id, std::string tag, bool merge,
                         bool highlight, size_t mergeParent,
                         size_t dashedFrom) -> size_t {
        GitCommit commit;
        commit.seq = commits.size();
        commit.lane = branches[current].lane;
        commit.merge = merge;
        commit.highlight = highlight;
        if (id.empty()) {
            // mermaid shows generated hashes; short ordinals read better
            id = std::to_string(++anonymous);
        }
        commit.id = std::move(id);
        commit.tag = std::move(tag);
        commit.parent = branches[current].tip;
        commit.mergeParent = mergeParent;
        if (dashedFrom != SIZE_MAX) {
            commit.mergeParent = dashedFrom;
            commit.dashedParent = true;
        }
        commits.push_back(std::move(commit));
        branches[current].tip = commits.size() - 1;
        commitIds[commits.back().id] = commits.size() - 1;
        return commits.size() - 1;
    };

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            std::string_view rest;
            if (!startsWithWord(line, "gitGraph", &rest)) {
                result.error = "Expected gitGraph header";
                return result;
            }
            rest = trimView(rest);
            if (!rest.empty() && rest != "LR:" && rest != "LR") {
                result.error = "Only left-to-right git graphs are supported";
                return result;
            }
            sawHeader = true;
            continue;
        }
        std::string_view rest;
        if (startsWithWord(line, "commit", &rest)) {
            auto values = parseKeyValues(rest);
            bool highlight = values.count("type") &&
                             values["type"] == "HIGHLIGHT";
            addCommit(values.count("id") ? values["id"] : std::string(),
                      values.count("tag") ? values["tag"] : std::string(),
                      false, highlight, SIZE_MAX, SIZE_MAX);
            continue;
        }
        if (startsWithWord(line, "branch", &rest)) {
            // strip optional order: N
            size_t order = rest.find("order:");
            std::string_view name =
                trimView(order == std::string_view::npos
                             ? rest
                             : rest.substr(0, order));
            if (name.empty()) {
                result.error = "branch needs a name";
                return result;
            }
            if (branchIds.find(name) == branchIds.end()) {
                GitBranch branch;
                branch.name = std::string(name);
                branch.lane = branches.size();
                branch.tip = branches[current].tip;  // branch point
                branches.push_back(branch);
                branchIds.emplace(std::string(name), branches.size() - 1);
            }
            current = branchIds.find(name)->second;
            continue;
        }
        if (startsWithWord(line, "checkout", &rest) ||
            startsWithWord(line, "switch", &rest)) {
            auto found = branchIds.find(trimView(rest));
            if (found == branchIds.end()) {
                result.error = "checkout of unknown branch";
                return result;
            }
            current = found->second;
            continue;
        }
        if (startsWithWord(line, "merge", &rest)) {
            std::string_view name = trimView(rest);
            auto values = parseKeyValues(rest);
            size_t space = name.find(' ');
            if (space != std::string_view::npos) {
                name = trimView(name.substr(0, space));
            }
            auto found = branchIds.find(name);
            if (found == branchIds.end()) {
                result.error = "merge of unknown branch";
                return result;
            }
            size_t otherTip = branches[found->second].tip;
            addCommit(values.count("id") ? values["id"] : std::string(),
                      values.count("tag") ? values["tag"] : std::string(),
                      true, false, otherTip, SIZE_MAX);
            continue;
        }
        if (startsWithWord(line, "cherry-pick", &rest)) {
            auto values = parseKeyValues(rest);
            size_t from = SIZE_MAX;
            if (values.count("id")) {
                auto found = commitIds.find(values["id"]);
                if (found != commitIds.end()) from = found->second;
            }
            std::string display = values.count("id")
                                      ? values["id"] + "'"
                                      : std::string();
            addCommit(std::move(display),
                      values.count("tag") ? values["tag"] : std::string(),
                      false, false, SIZE_MAX, from);
            continue;
        }
        result.error = "Unsupported gitGraph statement";
        return result;
    }

    if (commits.empty()) {
        result.error = "No commits";
        return result;
    }

    // --- measure and place ---
    TextStyle idStyle;
    idStyle.scale = 0.8f;
    idStyle.mono = true;
    TextStyle tagStyle;
    tagStyle.scale = 0.8f;
    TextStyle branchStyle;
    branchStyle.scale = 0.85f;
    branchStyle.bold = true;

    float branchLabelWidth = 0.0f;
    std::vector<Size> branchLabelSizes(branches.size());
    for (size_t i = 0; i < branches.size(); i++) {
        branchLabelSizes[i] = measure(branches[i].name, branchStyle, 0.0f);
        branchLabelWidth =
            std::max(branchLabelWidth,
                     branchLabelSizes[i].w + 16.0f * scale);
    }

    float left = branchLabelWidth + 16.0f * scale;
    float top = 26.0f * scale;  // room for tag chips
    auto commitX = [&](size_t seq) {
        return left + (seq + 0.5f) * kCommitGap * scale;
    };
    auto laneY = [&](size_t lane) {
        return top + (lane + 0.5f) * kLaneGap * scale;
    };

    // Connectors first (under the dots)
    for (const auto& commit : commits) {
        float x = commitX(commit.seq);
        float y = laneY(commit.lane);
        auto connect = [&](size_t parentIndex, bool dashed) {
            if (parentIndex == SIZE_MAX) return;
            const auto& parent = commits[parentIndex];
            float px = commitX(parent.seq);
            float py = laneY(parent.lane);
            int lane = static_cast<int>(
                parent.lane > commit.lane ? parent.lane : commit.lane);
            auto lineSegment = [&](float ax, float ay, float bx, float by) {
                Prim segment;
                segment.type = PrimType::Line;
                segment.x1 = ax;
                segment.y1 = ay;
                segment.x2 = bx;
                segment.y2 = by;
                segment.stroke = Role::Series;
                segment.seriesIndex = lane;
                segment.strokeWidth = 2.2f * scale;
                segment.dashed = dashed;
                result.prims.push_back(segment);
            };
            if (parent.lane == commit.lane) {
                lineSegment(px, py, x, y);
            } else {
                // Elbow just before the child commit
                float bendX = x - kCommitGap * 0.5f * scale;
                lineSegment(px, py, bendX, py);
                lineSegment(bendX, py, x, y);
            }
        };
        connect(commit.parent, false);
        connect(commit.mergeParent, commit.dashedParent);
    }

    // Dots + labels
    float maxBottom = 0.0f;
    for (const auto& commit : commits) {
        float x = commitX(commit.seq);
        float y = laneY(commit.lane);
        float radius = kDotRadius * scale;
        Prim dot;
        dot.type = PrimType::Ellipse;
        dot.x1 = x - radius;
        dot.y1 = y - radius;
        dot.x2 = x + radius;
        dot.y2 = y + radius;
        dot.fill = Role::Series;
        dot.seriesIndex = static_cast<int>(commit.lane);
        if (commit.highlight) {
            dot.stroke = Role::Text;
            dot.strokeWidth = 2.0f * scale;
        }
        result.prims.push_back(dot);
        if (commit.merge) {
            Prim inner;
            inner.type = PrimType::Ellipse;
            float innerRadius = radius * 0.45f;
            inner.x1 = x - innerRadius;
            inner.y1 = y - innerRadius;
            inner.x2 = x + innerRadius;
            inner.y2 = y + innerRadius;
            inner.fill = Role::Background;
            result.prims.push_back(inner);
        }

        if (!commit.id.empty()) {
            Size size = measure(commit.id, idStyle, 0.0f);
            Prim id;
            id.type = PrimType::Text;
            id.text = commit.id;
            id.style = idStyle;
            id.fill = Role::Muted;
            id.x1 = x - size.w * 0.5f - 2.0f;
            id.y1 = y + radius + 3.0f * scale;
            id.x2 = x + size.w * 0.5f + 2.0f;
            id.y2 = id.y1 + size.h;
            maxBottom = std::max(maxBottom, id.y2);
            result.prims.push_back(std::move(id));
        }
        if (!commit.tag.empty()) {
            Size size = measure(commit.tag, tagStyle, 0.0f);
            Prim chip;
            chip.type = PrimType::RoundRect;
            chip.radius = 4.0f * scale;
            chip.x1 = x - size.w * 0.5f - 6.0f * scale;
            chip.y1 = y - radius - size.h - 9.0f * scale;
            chip.x2 = x + size.w * 0.5f + 6.0f * scale;
            chip.y2 = y - radius - 3.0f * scale;
            chip.fill = Role::AccentSoft;
            chip.stroke = Role::Stroke;
            chip.strokeWidth = 1.0f * scale;
            result.prims.push_back(chip);
            Prim tag;
            tag.type = PrimType::Text;
            tag.text = commit.tag;
            tag.style = tagStyle;
            tag.fill = Role::Text;
            tag.x1 = chip.x1;
            tag.y1 = chip.y1;
            tag.x2 = chip.x2;
            tag.y2 = chip.y2;
            result.prims.push_back(std::move(tag));
        }
        maxBottom = std::max(maxBottom, y + radius);
    }

    // Branch name chips on the left, only for branches that have commits
    for (size_t i = 0; i < branches.size(); i++) {
        bool hasCommits = false;
        for (const auto& commit : commits) {
            if (commit.lane == branches[i].lane) {
                hasCommits = true;
                break;
            }
        }
        if (!hasCommits) continue;
        float y = laneY(branches[i].lane);
        const Size& size = branchLabelSizes[i];
        Prim chip;
        chip.type = PrimType::RoundRect;
        chip.radius = 4.0f * scale;
        chip.x1 = 2.0f;
        chip.y1 = y - size.h * 0.5f - 3.0f * scale;
        chip.x2 = 2.0f + size.w + 12.0f * scale;
        chip.y2 = y + size.h * 0.5f + 3.0f * scale;
        chip.fill = Role::SeriesSoft;
        chip.seriesIndex = static_cast<int>(branches[i].lane);
        chip.stroke = Role::Series;
        chip.strokeWidth = 1.0f * scale;
        result.prims.push_back(chip);
        Prim name;
        name.type = PrimType::Text;
        name.text = branches[i].name;
        name.style = branchStyle;
        name.fill = Role::Text;
        name.x1 = chip.x1;
        name.y1 = chip.y1;
        name.x2 = chip.x2;
        name.y2 = chip.y2;
        result.prims.push_back(std::move(name));
        maxBottom = std::max(maxBottom, chip.y2);
    }

    result.width = commitX(commits.size() - 1) + kCommitGap * 0.5f * scale +
                   8.0f * scale;
    result.height = maxBottom + 8.0f * scale;
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
