// Native Mermaid gantt chart renderer.
//
// Sections color their task bars from the categorical palette; tasks parse
// the mermaid CSV form (tags crit/done/active/milestone, optional id,
// start date / "after id" / carry-on, end date or duration). A date axis
// with day/week/month gridlines sits under the bars. `excludes` and
// `axisFormat` are accepted but ignored.

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

constexpr float kRowHeight = 26.0f;
constexpr float kRowGap = 6.0f;
constexpr float kChartWidth = 640.0f;
constexpr float kLabelGutter = 10.0f;

// days since 1970-01-01 (proleptic civil calendar)
long long civilDays(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + doe - 719468LL;
}

void civilFromDays(long long z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = static_cast<unsigned>(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long year = yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = static_cast<int>(year + (m <= 2));
}

struct DateFormat {
    int yearIndex = 0, monthIndex = 1, dayIndex = 2;  // token order
};

// Parses "2014-01-06" style strings against the token order
bool parseDate(std::string_view text, const DateFormat& format,
               double& outDays) {
    int numbers[3] = {0, 0, 0};
    int count = 0;
    size_t i = 0;
    while (i < text.size() && count < 3) {
        if (std::isdigit(static_cast<unsigned char>(text[i]))) {
            int value = 0;
            while (i < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[i]))) {
                value = value * 10 + (text[i] - '0');
                i++;
            }
            numbers[count++] = value;
        } else {
            i++;
        }
    }
    if (count != 3) return false;
    int year = numbers[format.yearIndex];
    int month = numbers[format.monthIndex];
    int day = numbers[format.dayIndex];
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;
    outDays = static_cast<double>(civilDays(year, month, day));
    return true;
}

bool parseDuration(std::string_view text, double& outDays) {
    if (text.size() < 2) return false;
    char unit = text.back();
    double scale = 0.0;
    if (unit == 'd') scale = 1.0;
    else if (unit == 'w') scale = 7.0;
    else if (unit == 'h') scale = 1.0 / 24.0;
    else if (unit == 'm') scale = 30.0;  // months, coarse
    else return false;
    double value = 0.0;
    try {
        value = std::stod(std::string(text.substr(0, text.size() - 1)));
    } catch (...) {
        return false;
    }
    outDays = value * scale;
    return true;
}

struct GanttTask {
    std::string name;
    std::string id;
    size_t section = 0;
    double start = 0.0;
    double finish = 0.0;
    bool done = false;
    bool active = false;
    bool crit = false;
    bool milestone = false;
};

}  // namespace

Built buildGantt(std::string_view source, const Measure& measure,
                 float scale) {
    Built result;
    auto lines = diagramLines(source);
    if (lines.empty()) {
        result.error = "Empty diagram";
        return result;
    }

    std::string title;
    std::vector<std::string> sections;
    std::vector<GanttTask> tasks;
    std::map<std::string, size_t, std::less<>> taskIds;
    DateFormat dateFormat;
    double cursor = 0.0;  // running end for tasks without a start
    bool haveCursor = false;

    bool sawHeader = false;
    for (std::string_view line : lines) {
        if (!sawHeader) {
            if (!startsWithWord(line, "gantt")) {
                result.error = "Expected gantt header";
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
        if (startsWithWord(line, "dateFormat", &rest)) {
            // token order of YYYY / MM / DD in the format string
            std::string format(rest);
            size_t yearAt = format.find('Y');
            size_t monthAt = format.find('M');
            size_t dayAt = format.find('D');
            if (yearAt == std::string::npos ||
                monthAt == std::string::npos || dayAt == std::string::npos) {
                result.error = "Unsupported dateFormat";
                return result;
            }
            size_t order[3] = {yearAt, monthAt, dayAt};
            int position[3];
            for (int i = 0; i < 3; i++) {
                position[i] = 0;
                for (int j = 0; j < 3; j++) {
                    if (order[j] < order[i]) position[i]++;
                }
            }
            dateFormat.yearIndex = position[0];
            dateFormat.monthIndex = position[1];
            dateFormat.dayIndex = position[2];
            continue;
        }
        if (startsWithWord(line, "axisFormat", &rest) ||
            startsWithWord(line, "excludes", &rest) ||
            startsWithWord(line, "tickInterval", &rest) ||
            startsWithWord(line, "todayMarker", &rest) ||
            startsWithWord(line, "weekday", &rest)) {
            continue;
        }
        if (startsWithWord(line, "section", &rest)) {
            sections.push_back(cleanLabel(rest));
            continue;
        }

        // Task: name : csv parts
        size_t colon = line.rfind(':');
        if (colon == std::string_view::npos || colon == 0) {
            result.error = "Unsupported gantt statement";
            return result;
        }
        GanttTask task;
        task.name = cleanLabel(trimView(line.substr(0, colon)));
        task.section = sections.empty() ? 0 : sections.size() - 1;
        std::vector<std::string_view> parts;
        {
            std::string_view remainder = line.substr(colon + 1);
            size_t position = 0;
            while (position <= remainder.size()) {
                size_t comma = remainder.find(',', position);
                if (comma == std::string_view::npos) comma = remainder.size();
                std::string_view part =
                    trimView(remainder.substr(position, comma - position));
                if (!part.empty()) parts.push_back(part);
                if (comma == remainder.size()) break;
                position = comma + 1;
            }
        }
        size_t index = 0;
        while (index < parts.size()) {
            if (parts[index] == "done") task.done = true;
            else if (parts[index] == "active") task.active = true;
            else if (parts[index] == "crit") task.crit = true;
            else if (parts[index] == "milestone") task.milestone = true;
            else break;
            index++;
        }
        // Optional id: not a date, not a duration, not "after x"
        double parsed = 0.0;
        if (index < parts.size() &&
            !parseDate(parts[index], dateFormat, parsed) &&
            !parseDuration(parts[index], parsed) &&
            parts[index].substr(0, 6) != "after " &&
            parts.size() - index >= 2) {
            task.id = std::string(parts[index]);
            index++;
        }
        // Start
        bool haveStart = false;
        if (index < parts.size()) {
            if (parseDate(parts[index], dateFormat, task.start)) {
                haveStart = true;
                index++;
            } else if (parts[index].substr(0, 6) == "after ") {
                std::string_view ids = trimView(parts[index].substr(6));
                double latest = haveCursor ? cursor : 0.0;
                size_t position = 0;
                while (position <= ids.size()) {
                    size_t space = ids.find(' ', position);
                    if (space == std::string_view::npos) space = ids.size();
                    std::string_view one =
                        trimView(ids.substr(position, space - position));
                    auto found = taskIds.find(one);
                    if (found != taskIds.end()) {
                        latest = std::max(latest, tasks[found->second].finish);
                    }
                    if (space == ids.size()) break;
                    position = space + 1;
                }
                task.start = latest;
                haveStart = true;
                index++;
            }
        }
        if (!haveStart) {
            if (!haveCursor) {
                result.error = "First task needs a start date";
                return result;
            }
            task.start = cursor;
        }
        // End: date or duration (milestones may omit it)
        if (index < parts.size()) {
            double endValue = 0.0;
            if (parseDate(parts[index], dateFormat, endValue)) {
                task.finish = endValue;
            } else if (parseDuration(parts[index], endValue)) {
                task.finish = task.start + endValue;
            } else {
                result.error = "Bad task end";
                return result;
            }
        } else {
            task.finish = task.start;
        }
        if (task.milestone) task.finish = task.start;
        if (task.finish < task.start) std::swap(task.finish, task.start);
        cursor = task.finish;
        haveCursor = true;
        if (!task.id.empty()) taskIds[task.id] = tasks.size();
        tasks.push_back(std::move(task));
    }

    if (tasks.empty()) {
        result.error = "No tasks";
        return result;
    }
    if (sections.empty()) sections.push_back("");

    double minDay = tasks[0].start, maxDay = tasks[0].finish;
    for (const auto& task : tasks) {
        minDay = std::min(minDay, task.start);
        maxDay = std::max(maxDay, task.finish);
    }
    double span = std::max(1.0, maxDay - minDay);
    minDay -= span * 0.02;
    maxDay += span * 0.04;
    span = maxDay - minDay;

    // --- measure ---
    TextStyle titleStyle;
    titleStyle.bold = true;
    titleStyle.scale = 1.1f;
    TextStyle sectionStyle;
    sectionStyle.bold = true;
    sectionStyle.scale = 0.9f;
    TextStyle taskStyle;
    taskStyle.scale = 0.85f;
    TextStyle axisStyle;
    axisStyle.scale = 0.75f;

    float gutter = 0.0f;
    for (const auto& section : sections) {
        gutter = std::max(gutter, measure(section, sectionStyle, 0.0f).w);
    }
    gutter += kLabelGutter * 2.0f * scale;

    float chartWidth = kChartWidth * scale;
    float dayWidth = chartWidth / static_cast<float>(span);
    auto dayX = [&](double day) {
        return gutter + static_cast<float>(day - minDay) * dayWidth;
    };

    float y = 4.0f * scale;
    if (!title.empty()) {
        Size titleSize = measure(title, titleStyle, 0.0f);
        Prim text;
        text.type = PrimType::Text;
        text.text = title;
        text.style = titleStyle;
        text.fill = Role::Text;
        text.x1 = gutter;
        text.y1 = y;
        text.x2 = gutter + chartWidth;
        text.y2 = y + titleSize.h;
        result.prims.push_back(std::move(text));
        y += titleSize.h + 10.0f * scale;
    }
    float chartTop = y;

    // Rows in declaration order, grouped visually by section color
    float rowStride = (kRowHeight + kRowGap) * scale;
    float chartBottom = chartTop + tasks.size() * rowStride;

    // Gridlines: daily under 21 days, weekly under 130, else monthly
    {
        long long firstDay = static_cast<long long>(std::floor(minDay));
        long long lastDay = static_cast<long long>(std::ceil(maxDay));
        int step = span <= 21 ? 1 : span <= 130 ? 7 : 0;  // 0 = monthly
        for (long long day = firstDay; day <= lastDay; day++) {
            int year;
            unsigned month, dayOfMonth;
            civilFromDays(day, year, month, dayOfMonth);
            bool tick = false;
            if (step == 0) {
                tick = dayOfMonth == 1;
            } else if (step == 7) {
                tick = (day % 7) == 4;  // Mondays (1970-01-05 was Monday)
            } else {
                tick = true;
            }
            if (!tick) continue;
            float x = dayX(static_cast<double>(day));
            if (x < gutter - 1.0f || x > gutter + chartWidth + 1.0f) continue;
            Prim grid;
            grid.type = PrimType::Line;
            grid.x1 = x;
            grid.y1 = chartTop;
            grid.x2 = x;
            grid.y2 = chartBottom + 4.0f * scale;
            grid.stroke = Role::Muted;
            grid.strokeWidth = 0.5f * scale;
            result.prims.push_back(grid);
            char buffer[16];
            if (step == 0) {
                snprintf(buffer, sizeof(buffer), "%04d-%02u", year, month);
            } else {
                snprintf(buffer, sizeof(buffer), "%02u-%02u", month,
                         dayOfMonth);
            }
            Size size = measure(buffer, axisStyle, 0.0f);
            Prim label;
            label.type = PrimType::Text;
            label.text = buffer;
            label.style = axisStyle;
            label.fill = Role::Muted;
            label.x1 = x - size.w * 0.5f - 2.0f;
            label.y1 = chartBottom + 6.0f * scale;
            label.x2 = x + size.w * 0.5f + 2.0f;
            label.y2 = chartBottom + 6.0f * scale + size.h;
            result.prims.push_back(std::move(label));
        }
    }

    // Section labels beside their first row + soft row banding per section
    size_t lastSection = SIZE_MAX;
    for (size_t i = 0; i < tasks.size(); i++) {
        const auto& task = tasks[i];
        float rowTop = chartTop + i * rowStride;
        if (task.section != lastSection) {
            lastSection = task.section;
            size_t rows = 0;
            for (size_t j = i; j < tasks.size(); j++) {
                if (tasks[j].section != task.section) break;
                rows++;
            }
            Prim band;
            band.type = PrimType::Rect;
            band.x1 = 0;
            band.y1 = rowTop - kRowGap * 0.5f * scale;
            band.x2 = gutter + chartWidth;
            band.y2 = rowTop + rows * rowStride - kRowGap * 0.5f * scale;
            band.fill = Role::SeriesSoft;
            band.seriesIndex = static_cast<int>(task.section);
            // Halve the tint so bars stay dominant
            result.prims.push_back(band);
            if (!sections[task.section].empty()) {
                Size size =
                    measure(sections[task.section], sectionStyle, 0.0f);
                Prim label;
                label.type = PrimType::Text;
                label.text = sections[task.section];
                label.style = sectionStyle;
                label.fill = Role::Text;
                label.alignH = -1;
                label.x1 = kLabelGutter * scale;
                label.y1 = rowTop;
                label.x2 = gutter - kLabelGutter * scale;
                label.y2 = rowTop + size.h + 4.0f * scale;
                result.prims.push_back(std::move(label));
            }
        }
    }

    // Bars
    for (size_t i = 0; i < tasks.size(); i++) {
        const auto& task = tasks[i];
        float rowTop = chartTop + i * rowStride;
        float barTop = rowTop + 2.0f * scale;
        float barBottom = rowTop + kRowHeight * scale - 2.0f * scale;
        float x1 = dayX(task.start);
        float x2 = std::max(dayX(task.finish), x1 + 4.0f * scale);

        if (task.milestone) {
            float cx = x1;
            float cy = (barTop + barBottom) * 0.5f;
            float radius = (barBottom - barTop) * 0.5f;
            Prim diamond;
            diamond.type = PrimType::Polygon;
            diamond.pts = {{cx, cy - radius},
                           {cx + radius, cy},
                           {cx, cy + radius},
                           {cx - radius, cy}};
            diamond.fill = Role::Accent;
            result.prims.push_back(std::move(diamond));
            Size size = measure(task.name, taskStyle, 0.0f);
            Prim label;
            label.type = PrimType::Text;
            label.text = task.name;
            label.style = taskStyle;
            label.fill = Role::Text;
            label.alignH = -1;
            label.x1 = cx + radius + 6.0f * scale;
            label.y1 = barTop;
            label.x2 = label.x1 + size.w + 4.0f;
            label.y2 = barBottom;
            result.prims.push_back(std::move(label));
            continue;
        }

        Prim bar;
        bar.type = PrimType::RoundRect;
        bar.radius = 4.0f * scale;
        bar.x1 = x1;
        bar.y1 = barTop;
        bar.x2 = x2;
        bar.y2 = barBottom;
        if (task.done) {
            bar.fill = Role::Fill;
            bar.stroke = Role::Muted;
        } else if (task.active) {
            bar.fill = Role::AccentSoft;
            bar.stroke = Role::Accent;
        } else {
            bar.fill = Role::Series;
            bar.seriesIndex = static_cast<int>(task.section);
            bar.stroke = Role::None;
        }
        bar.strokeWidth = 1.2f * scale;
        if (task.crit) {
            bar.stroke = Role::Custom;
            bar.customR = 0.83f;
            bar.customG = 0.28f;
            bar.customB = 0.24f;
            bar.strokeWidth = 2.0f * scale;
        }
        result.prims.push_back(bar);

        Size size = measure(task.name, taskStyle, 0.0f);
        Prim label;
        label.type = PrimType::Text;
        label.text = task.name;
        label.style = taskStyle;
        if (size.w + 8.0f * scale < x2 - x1) {
            label.fill = task.done || task.active ? Role::Text
                                                  : Role::Background;
            label.x1 = x1;
            label.y1 = barTop;
            label.x2 = x2;
            label.y2 = barBottom;
        } else {
            label.fill = Role::Text;
            label.alignH = -1;
            label.x1 = x2 + 6.0f * scale;
            label.y1 = barTop;
            label.x2 = label.x1 + size.w + 4.0f;
            label.y2 = barBottom;
        }
        result.prims.push_back(std::move(label));
    }

    result.width = gutter + chartWidth + 90.0f * scale;
    result.height = chartBottom + 24.0f * scale;
    result.ok = true;
    return result;
}

}  // namespace detail
}  // namespace mermaidext
