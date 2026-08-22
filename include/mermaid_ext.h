#ifndef TINTA_MERMAID_EXT_H
#define TINTA_MERMAID_EXT_H

// Native renderers for the Mermaid diagram families beyond flowcharts.
// Each family parses its source into a flat list of drawing primitives in
// diagram-local pixel coordinates (already scaled); the app translates the
// primitives into its layout structures and theme colors. The library is
// pure C++ (no Direct2D/DirectWrite) so parsers stay unit-testable: text
// measurement is injected through a callback.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace mermaidext {

enum class Kind {
    None,       // not a diagram this library handles
    Flowchart,  // handled by the original mermaid:: flowchart engine
    Sequence,
    Class,
    State,
    Er,
    Pie,
    Git,
    Gantt,
    Mindmap,
    Timeline,
    Journey,
    Quadrant,
    XyChart,
    Packet,
    Kanban,
    Treemap,
    Radar,
    Sankey,
    Block,
    Requirement,
    C4,
    Architecture,
};

// Inspects the first meaningful line's keyword.
Kind detectKind(std::string_view source);

struct Size {
    float w = 0.0f;
    float h = 0.0f;
};

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct TextStyle {
    float scale = 1.0f;  // multiplier on the app's body size
    bool bold = false;
    bool italic = false;
    bool mono = false;
};

// (utf8 text, style, wrap width or 0 = no wrap) -> measured size.
// Text may contain '\n' which always breaks lines.
using Measure =
    std::function<Size(const std::string&, const TextStyle&, float)>;

enum class PrimType {
    Rect,       // x1,y1,x2,y2 bounds
    RoundRect,  // + radius
    Ellipse,    // bounds
    Line,       // x1,y1 -> x2,y2 (+ dashed, arrow flags)
    Polygon,    // pts (closed, filled and/or stroked)
    Slice,      // pie slice: center (x1,y1), radius, angles a0->a1 radians
    Text,       // bounds + text + style + align
};

// Color roles resolved against the app theme at translation time.
enum class Role {
    Text,        // theme text
    Muted,       // theme text at reduced alpha
    Stroke,      // theme accent (node outline color)
    Fill,        // theme code background (node fill)
    Accent,      // theme accent
    AccentSoft,  // theme accent at low alpha (note/band fills)
    Background,  // theme background
    Series,      // categorical palette color [seriesIndex]
    SeriesSoft,  // same, low alpha
    Custom,      // the prim's own customR/G/B (journey scores, crit tasks)
    None,        // do not draw this half (no fill / no stroke)
};

struct Prim {
    PrimType type = PrimType::Rect;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float radius = 0.0f;
    float a0 = 0.0f, a1 = 0.0f;
    std::vector<Point> pts;
    std::string text;
    TextStyle style;
    Role fill = Role::None;
    Role stroke = Role::None;
    int seriesIndex = 0;
    float customR = 0.0f, customG = 0.0f, customB = 0.0f, customA = 1.0f;
    float strokeWidth = 0.0f;
    bool dashed = false;
    bool arrow = false;      // Line: filled arrowhead at (x2,y2)
    bool openArrow = false;  // Line: two-stroke arrowhead at (x2,y2)
    int alignH = 0;          // Text: -1 left, 0 center, 1 right
    int alignV = 0;          // Text: -1 top, 0 center, 1 bottom
};

struct Built {
    bool ok = false;
    float width = 0.0f;
    float height = 0.0f;
    std::vector<Prim> prims;
    std::string error;
};

// Parses and lays out `source` for the given kind. `scale` multiplies every
// base coordinate (the app passes contentScale * zoomFactor). Returns
// ok=false when the source uses constructs the native renderer does not
// support; the caller falls back to showing the source.
Built build(Kind kind, std::string_view source, const Measure& measure,
            float scale);

// --- shared parsing helpers (used by the per-family implementations and
// exercised directly by tests) ---
namespace detail {

std::string_view trimView(std::string_view value);
// Splits into trimmed, non-empty, non-%%-comment lines.
std::vector<std::string_view> diagramLines(std::string_view source);
// Replaces <br/> variants with '\n' and decodes a few common entities.
std::string cleanLabel(std::string_view raw);
bool startsWithWord(std::string_view line, std::string_view word,
                    std::string_view* rest = nullptr);
// Shifts every primitive right so nothing sits at negative x, growing the
// diagram width to match (notes and markers can overhang the layout box).
void normalizeLeft(Built& built);

// Cylinder silhouette (top ellipse, straight sides, bottom arc) plus the
// lid seam, sampled into a polygon and short lines. `paint` supplies the
// fill/stroke roles and widths; lidHeight is the full top-ellipse height.
void emitCylinder(std::vector<Prim>& out, const Prim& paint, float x1,
                  float y1, float x2, float y2, float lidHeight);

Built buildSequence(std::string_view source, const Measure& measure,
                    float scale);
Built buildPie(std::string_view source, const Measure& measure, float scale);
Built buildState(std::string_view source, const Measure& measure,
                 float scale);
Built buildClass(std::string_view source, const Measure& measure,
                 float scale);
Built buildEr(std::string_view source, const Measure& measure, float scale);
Built buildGit(std::string_view source, const Measure& measure, float scale);
Built buildGantt(std::string_view source, const Measure& measure,
                 float scale);
Built buildMindmap(std::string_view source, const Measure& measure,
                   float scale);
Built buildTimeline(std::string_view source, const Measure& measure,
                    float scale);
Built buildJourney(std::string_view source, const Measure& measure,
                   float scale);
Built buildQuadrant(std::string_view source, const Measure& measure,
                    float scale);
Built buildXyChart(std::string_view source, const Measure& measure,
                   float scale);
Built buildPacket(std::string_view source, const Measure& measure,
                  float scale);
Built buildKanban(std::string_view source, const Measure& measure,
                  float scale);
Built buildTreemap(std::string_view source, const Measure& measure,
                   float scale);
Built buildRadar(std::string_view source, const Measure& measure,
                 float scale);
Built buildSankey(std::string_view source, const Measure& measure,
                  float scale);
Built buildBlock(std::string_view source, const Measure& measure,
                 float scale);
Built buildRequirement(std::string_view source, const Measure& measure,
                       float scale);
Built buildC4(std::string_view source, const Measure& measure, float scale);
Built buildArchitecture(std::string_view source, const Measure& measure,
                        float scale);

}  // namespace detail

}  // namespace mermaidext

#endif  // TINTA_MERMAID_EXT_H
