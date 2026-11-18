#ifndef TINTA_MERMAID_H
#define TINTA_MERMAID_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mermaid {

enum class Direction {
    TopToBottom,
    BottomToTop,
    LeftToRight,
    RightToLeft,
};

enum class NodeShape {
    Rectangle,
    RoundedRectangle,
    Diamond,
    Stadium,
    Circle,
    Hexagon,
    Asymmetric,  // id>text]: a rectangle notched in on its left side
};

struct Color {
    uint32_t rgb = 0;
    float alpha = 1.0f;
};

struct Style {
    bool hasFill = false;
    bool hasStroke = false;
    bool hasText = false;
    bool hasStrokeWidth = false;
    Color fill;
    Color stroke;
    Color text;
    float strokeWidth = 1.0f;
};

struct Node {
    std::string id;
    std::string label;
    NodeShape shape = NodeShape::Rectangle;
    std::string className;
    Style style;
    size_t sourceOffset = 0;
};

struct Edge {
    size_t from = 0;
    size_t to = 0;
    std::string label;
    bool directed = true;
    bool dashed = false;
    float strokeScale = 1.0f;
};

struct Subgraph {
    std::string id;
    std::string label;
    size_t parent = SIZE_MAX;    // enclosing subgraph, SIZE_MAX = top level
    std::vector<size_t> nodes;   // nodes first defined inside this subgraph
    bool hasDirection = false;   // a "direction" statement of its own (#326)
    Direction direction = Direction::TopToBottom;
};

struct Diagram {
    Direction direction = Direction::TopToBottom;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Subgraph> subgraphs;
    std::unordered_map<std::string, Style> classStyles;
};

struct ParseResult {
    Diagram diagram;
    bool success = false;
    size_t errorLine = 0;
    std::string error;
};

struct Size {
    float width = 0.0f;
    float height = 0.0f;
};

struct Rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

inline bool isEmpty(const Rect& rect) { return rect.right < rect.left; }

// The distances a flowchart is laid out and drawn with
struct LayoutOptions {
    float nodeGap = 0.0f;       // between two nodes of a rank
    float rankGap = 0.0f;       // between two ranks
    // Group boxes: the room around a group's members, the title strip above
    // them, and the least distance between the boxes of two groups
    float groupPad = 0.0f;
    float groupTitle = 0.0f;
    float groupGap = 0.0f;
    // The width each subgraph's title takes (none: no least width). A block
    // is made as wide, where its nodes alone would leave the title to wrap
    std::vector<float> groupTitles;
    // Edge labels, one size per edge (none or zero: no label). A rank gap is
    // at least as long as a label that sits in it, plus labelRankGap
    std::vector<Size> edgeLabels;
    float labelRankGap = 0.0f;
    float labelStep = 0.0f;     // between two labels pushed apart
    // An edge that cannot run straight to the next rank takes a lane beside
    // the nodes: lane n runs (laneStart + n * laneStep) * laneScale away
    float laneStart = 0.0f;
    float laneStep = 0.0f;
    float laneScale = 1.0f;
    float arrow = 0.0f;         // room an arrowhead needs around a line
};

// What the viewer and the exports use, at a scale
LayoutOptions drawingOptions(float scale);

struct Layout {
    std::vector<Rect> nodes;
    // Layer index per node, counted inside the layout the node is part of
    std::vector<size_t> ranks;
    // A subgraph with a direction of its own and no edge across its border
    // is laid out on its own and set into its parent as one block (#326).
    // blocks: such subgraphs. scopes: per node, the layout it is part of,
    // 0 for the diagram's and s + 1 for subgraph s. directions: per node,
    // the direction of that layout
    std::vector<bool> blocks;
    std::vector<size_t> scopes;
    std::vector<Direction> directions;
    // Per subgraph: the layout it lies in, numbered as in scopes, and for a
    // block its rank there
    std::vector<size_t> groupScopes;
    std::vector<size_t> groupRanks;
    std::vector<Rect> groups;   // box per subgraph; isEmpty() with no nodes
    float width = 0.0f;         // the nodes' extent; group boxes reach past it
    float height = 0.0f;
};

struct EdgeRoute {
    std::vector<Point> points;
    bool hasLabel = false;
    Rect label;
};

// A layout moved to where it is drawn
struct Placement {
    std::vector<Rect> nodes;
    std::vector<Rect> groups;       // isEmpty() for a subgraph with no nodes
    std::vector<EdgeRoute> edges;   // empty points for an edge left out
};

ParseResult parse(std::string_view source);
Layout layout(const Diagram& diagram, const std::vector<Size>& nodeSizes,
              const LayoutOptions& options);
Layout layout(const Diagram& diagram, const std::vector<Size>& nodeSizes,
              float nodeGap, float rankGap);
Placement place(const Diagram& diagram, const Layout& layout,
                const LayoutOptions& options, float originX, float originY);

// How far a node's left side is notched in at mid-height. Mermaid draws the
// asymmetric shape through (0,0) (w,0) (w,h) (0,h) and (h/2,h/2); its label
// and edges keep to the rectangle beside the notch.
inline float leftNotch(NodeShape shape, float height) {
    return shape == NodeShape::Asymmetric ? height * 0.5f : 0.0f;
}

} // namespace mermaid

#endif // TINTA_MERMAID_H
