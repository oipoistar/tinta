#include "mermaid.h"
#include "mermaid_ext.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

const mermaid::Node* findNode(const mermaid::Diagram& diagram, const std::string& id) {
    for (const auto& node : diagram.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

void testStyledFlowchart() {
    const char* source = R"(flowchart TB
classDef start fill:#fff7ed,stroke:#f97316,color:#7c2d12,stroke-width:2px;
Start["Begin<br/>enrollment"]:::start --> Choice{"Assigned?"}
Choice -->|Yes| Done["Complete"]
Choice -->|No| Retry(["Retry"])
)";

    auto result = mermaid::parse(source);
    check(result.success, "styled flowchart parses");
    if (!result.success) return;

    check(result.diagram.direction == mermaid::Direction::TopToBottom,
          "TB direction is detected");
    check(result.diagram.nodes.size() == 4, "all nodes are parsed");
    check(result.diagram.edges.size() == 3, "all edges are parsed");

    const auto* start = findNode(result.diagram, "Start");
    const auto* choice = findNode(result.diagram, "Choice");
    const auto* retry = findNode(result.diagram, "Retry");
    check(start && start->label == "Begin\nenrollment", "HTML breaks become line breaks");
    check(start && start->className == "start", "node class is captured");
    check(choice && choice->shape == mermaid::NodeShape::Diamond,
          "diamond node shape is captured");
    check(retry && retry->shape == mermaid::NodeShape::Stadium,
          "stadium node shape is captured");

    auto style = result.diagram.classStyles.find("start");
    check(style != result.diagram.classStyles.end(), "class style is captured");
    if (style != result.diagram.classStyles.end()) {
        check(style->second.hasFill && style->second.fill.rgb == 0xFFF7ED,
              "class fill color is parsed");
        check(style->second.hasStrokeWidth && style->second.strokeWidth == 2.0f,
              "class stroke width is parsed");
    }
    check(result.diagram.edges[1].label == "Yes", "edge label is captured");
}

void testGraphAliasesAndChaining() {
    const char* source = R"(graph LR
A --> B --> C
class A,B emphasized
style C fill:#abc,stroke:#123456
)";

    auto result = mermaid::parse(source);
    check(result.success, "graph alias parses");
    if (!result.success) return;

    check(result.diagram.direction == mermaid::Direction::LeftToRight,
          "LR direction is detected");
    check(result.diagram.nodes.size() == 3, "chained nodes are parsed once");
    check(result.diagram.edges.size() == 2, "chained edges are expanded");

    const auto* a = findNode(result.diagram, "A");
    const auto* b = findNode(result.diagram, "B");
    const auto* c = findNode(result.diagram, "C");
    check(a && a->className == "emphasized", "class command styles first node");
    check(b && b->className == "emphasized", "class command styles second node");
    check(c && c->style.hasFill && c->style.fill.rgb == 0xAABBCC,
          "short inline color expands correctly");
}

void testLayoutDirections() {
    auto topBottom = mermaid::parse("flowchart TB\nA --> B\nA --> C\n");
    check(topBottom.success, "layout fixture parses");
    if (!topBottom.success) return;

    std::vector<mermaid::Size> sizes(topBottom.diagram.nodes.size(), {100.0f, 50.0f});
    auto vertical = mermaid::layout(topBottom.diagram, sizes, 20.0f, 40.0f);
    check(vertical.nodes.size() == 3, "vertical layout includes every node");
    check(vertical.nodes[0].bottom < vertical.nodes[1].top,
          "TB target is below source");
    check(vertical.nodes[1].right <= vertical.nodes[2].left,
          "same-rank nodes do not overlap");

    auto leftRight = mermaid::parse("flowchart LR\nA --> B\n");
    check(leftRight.success, "LR layout fixture parses");
    if (!leftRight.success) return;
    sizes.assign(leftRight.diagram.nodes.size(), {100.0f, 50.0f});
    auto horizontal = mermaid::layout(leftRight.diagram, sizes, 20.0f, 40.0f);
    check(horizontal.nodes[0].right < horizontal.nodes[1].left,
          "LR target is right of source");
}

void testSubgraphs() {
    const char* source = R"(flowchart TB
subgraph one [Group One]
    A --> B
end
subgraph two
    direction LR
    C --> D
end
A e1@--> C
e1@{ animate: true }
click A callback
linkStyle 0 stroke:#f00
)";
    auto result = mermaid::parse(source);
    check(result.success, "subgraph flowchart parses");
    if (!result.success) return;
    check(result.diagram.subgraphs.size() == 2, "both subgraphs captured");
    if (result.diagram.subgraphs.size() == 2) {
        check(result.diagram.subgraphs[0].label == "Group One",
              "bracket label captured");
        check(result.diagram.subgraphs[0].nodes.size() == 2,
              "first subgraph owns its two nodes");
        check(result.diagram.subgraphs[1].id == "two",
              "bare subgraph name is the id");
    }
    check(result.diagram.edges.size() == 3,
          "edge-ID edge parses alongside subgraph edges");
}

void testUnsupportedDiagram() {
    auto result = mermaid::parse("sequenceDiagram\nAlice->>Bob: Hello\n");
    check(!result.success, "unsupported Mermaid diagram is rejected");
    check(result.errorLine == 1, "unsupported diagram reports its line");
}

void testBomAndSemicolonStatements() {
    const char* source =
        "\xEF\xBB\xBF"
        "flowchart LR; A[\"One; still one label\"] -->|Yes; still yes| B; "
        "%% comment; not a statement\n"
        "B --> C;";
    auto result = mermaid::parse(source);
    check(result.success, "UTF-8 BOM and semicolon-separated statements parse");
    if (!result.success) return;
    check(result.diagram.nodes.size() == 3, "semicolon statements include all nodes");
    check(result.diagram.edges.size() == 2, "semicolon statements include all edges");
    const auto* a = findNode(result.diagram, "A");
    check(a && a->label == "One; still one label",
          "semicolon inside a node label is preserved");
    check(result.diagram.edges[0].label == "Yes; still yes",
          "semicolon inside a pipe-delimited edge label is preserved");
}

void testCyclicLayout() {
    auto parsed = mermaid::parse(
        "flowchart TB\n"
        "A --> B\n"
        "B --> A\n"
        "A --> A\n");
    check(parsed.success, "cyclic flowchart parses");
    if (!parsed.success) return;

    std::vector<mermaid::Size> sizes(parsed.diagram.nodes.size(), {100.0f, 50.0f});
    auto graph = mermaid::layout(parsed.diagram, sizes, 20.0f, 40.0f);
    check(graph.nodes.size() == 2, "cyclic layout includes every node");
    check(graph.nodes[0].top == 0.0f, "cyclic layout does not leave an empty first rank");
    check(graph.nodes[0].bottom < graph.nodes[1].top,
          "cyclic nodes occupy distinct non-overlapping ranks");
}

void testFiles(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::ifstream file(argv[i], std::ios::binary);
        check(static_cast<bool>(file), "supplied Mermaid file opens");
        if (!file) continue;

        std::string source(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        auto result = mermaid::parse(source);
        if (!result.success) {
            std::cerr << argv[i] << ':' << result.errorLine << ": "
                      << result.error << '\n';
        }
        check(result.success, "supplied Mermaid file parses");
    }
}

void testBackslashNLineBreak() {
    const char* source = R"(graph TD
    BG[background thread\nengine.stop / PlotLan / DNS] --> TK[main]
)";
    auto result = mermaid::parse(source);
    check(result.success, "label with \\n parses");
    if (!result.success) return;
    const auto* bg = findNode(result.diagram, "BG");
    check(bg && bg->label == "background thread\nengine.stop / PlotLan / DNS",
          "literal \\n in a label becomes a line break");
}

void testAttributeSyntaxRejected() {
    const char* source = R"(flowchart TD
A@{ shape: rounded, label: "Fancy" } --> B[Plain]
)";
    auto result = mermaid::parse(source);
    check(!result.success, "v11 '@{ }' attribute syntax fails instead of mis-rendering");
}

void testLayoutExposesRanks() {
    const char* source = R"(graph LR
A --> B
B --> C
C --> D
A --> D
)";
    auto result = mermaid::parse(source);
    check(result.success, "rank test diagram parses");
    if (!result.success) return;

    std::vector<mermaid::Size> sizes(result.diagram.nodes.size(), {100.0f, 40.0f});
    auto layout = mermaid::layout(result.diagram, sizes, 20.0f, 60.0f);
    check(layout.ranks.size() == result.diagram.nodes.size(),
          "layout exposes one rank per node");
    if (layout.ranks.size() == 4) {
        check(layout.ranks[0] == 0 && layout.ranks[1] == 1 &&
              layout.ranks[2] == 2 && layout.ranks[3] == 3,
              "ranks follow the longest path (A=0, B=1, C=2, D=3)");
    }
}

// --- extended diagram families (mermaidext) ---

// Character-cell measurer: 8px per column, 16px per line
mermaidext::Size fakeMeasure(const std::string& text,
                             const mermaidext::TextStyle&, float) {
    size_t longest = 0, current = 0, lines = text.empty() ? 0 : 1;
    for (char c : text) {
        if (c == '\n') {
            lines++;
            current = 0;
        } else {
            current++;
            longest = std::max(longest, current);
        }
    }
    return {static_cast<float>(longest * 8),
            static_cast<float>(lines * 16)};
}

void testDetectKind() {
    using mermaidext::Kind;
    using mermaidext::detectKind;
    check(detectKind("graph TD\nA-->B") == Kind::Flowchart,
          "graph detects as flowchart");
    check(detectKind("%% comment\nflowchart LR\nA-->B") == Kind::Flowchart,
          "leading comments are skipped in detection");
    check(detectKind("sequenceDiagram\nA->>B: hi") == Kind::Sequence,
          "sequenceDiagram detected");
    check(detectKind("classDiagram\nA <|-- B") == Kind::Class,
          "classDiagram detected");
    check(detectKind("stateDiagram-v2\n[*] --> A") == Kind::State,
          "stateDiagram-v2 detected");
    check(detectKind("erDiagram\nA ||--o{ B : has") == Kind::Er,
          "erDiagram detected");
    check(detectKind("pie\n\"A\" : 1") == Kind::Pie, "pie detected");
    check(detectKind("gitGraph\ncommit") == Kind::Git, "gitGraph detected");
    check(detectKind("gantt\ntitle X") == Kind::Gantt, "gantt detected");
    check(detectKind("mindmap\n  root") == Kind::Mindmap,
          "mindmap detected");
    check(detectKind("timeline\n2020 : x") == Kind::Timeline,
          "timeline detected");
    check(detectKind("journey\ntitle X") == Kind::Journey,
          "journey detected");
    check(detectKind("quadrantChart\ntitle X") == Kind::Quadrant,
          "quadrantChart detected");
    check(detectKind("xychart-beta\nbar [1]") == Kind::XyChart,
          "xychart-beta detected");
    check(detectKind("packet-beta\n0-15: \"x\"") == Kind::Packet,
          "packet-beta detected");
    check(detectKind("kanban\n  Todo") == Kind::Kanban, "kanban detected");
    check(detectKind("treemap-beta\n\"A\"") == Kind::Treemap,
          "treemap-beta detected");
    check(detectKind("zenuml\n...") == Kind::None,
          "unsupported kinds report None");
}

void testExtBuilders() {
    using mermaidext::Kind;
    struct Case {
        Kind kind;
        const char* name;
        const char* source;
    };
    const Case kCases[] = {
        {Kind::Sequence, "sequence",
         "sequenceDiagram\n"
         "    participant A as Service<br/>api\n"
         "    A->>+B: request\n"
         "    B-->>-A: reply\n"
         "    Note over A,B: both\n"
         "    loop retry\n        A-)B: ping\n    end\n"},
        {Kind::Pie, "pie",
         "pie showData title Share\n    \"A\" : 60\n    \"B\" : 40\n"},
        {Kind::State, "state",
         "stateDiagram-v2\n    [*] --> S1\n    S1 --> S2 : go\n"
         "    S2 --> S1 : back\n    S2 --> [*]\n"},
        {Kind::Class, "class",
         "classDiagram\n    Animal <|-- Duck\n    Animal : +int age\n"
         "    class Duck{\n        +swim()\n    }\n"},
        {Kind::Er, "er",
         "erDiagram\n    CUSTOMER ||--o{ ORDER : places\n"
         "    CUSTOMER {\n        string name PK\n    }\n"},
        {Kind::Git, "git",
         "gitGraph\n   commit\n   branch dev\n   commit\n"
         "   checkout main\n   merge dev tag: \"v1\"\n"},
        {Kind::Gantt, "gantt",
         "gantt\n    dateFormat YYYY-MM-DD\n    section S\n"
         "        T1 :a1, 2026-01-01, 5d\n        T2 :after a1, 3d\n"},
        {Kind::Mindmap, "mindmap",
         "mindmap\n  root((center))\n    A\n      A1\n    B\n"},
        {Kind::Timeline, "timeline",
         "timeline\n    title T\n    2020 : one : two\n    2021 : three\n"},
        {Kind::Journey, "journey",
         "journey\n    title T\n    section S\n      Task: 5: Me\n"},
        {Kind::Quadrant, "quadrant",
         "quadrantChart\n    x-axis Low --> High\n    quadrant-1 Q\n"
         "    A: [0.3, 0.6]\n"},
        {Kind::XyChart, "xychart",
         "xychart-beta\n    x-axis [a, b]\n    bar [1, 2]\n    line [2, 1]\n"},
        {Kind::Packet, "packet",
         "packet-beta\ntitle UDP\n0-15: \"Source Port\"\n"
         "16-31: \"Destination Port\"\n32-63: \"Data\"\n"},
        {Kind::Kanban, "kanban",
         "kanban\n  Todo\n    [Write docs]\n    id2[Fix bug]@{ ticket: "
         "'MC-1', assigned: 'knsv', priority: 'High' }\n  [Done]\n"
         "    [Ship it]\n"},
        {Kind::Treemap, "treemap",
         "treemap-beta\ntitle T\n\"Section 1\"\n    \"Leaf 1.1\": 12\n"
         "    \"Leaf 1.2\": 8\n\"Section 2\"\n    \"Leaf 2.1\": 10\n"},
        {Kind::Radar, "radar",
         "radar-beta\ntitle Grades\naxis m[\"Math\"], s[\"Science\"], "
         "e[\"English\"]\ncurve a[\"Alice\"]{85, 90, 80}\n"
         "curve b{70, 75, 85}\nmax 100\nmin 0\n"},
        {Kind::Sankey, "sankey",
         "sankey-beta\nA,B,10\nA,C,5\nB,D,8\nC,D,5\n\"E, e\",D,2\n"},
    };
    for (const auto& testCase : kCases) {
        auto built = mermaidext::build(testCase.kind, testCase.source,
                                       fakeMeasure, 1.0f);
        std::string label = std::string(testCase.name) + " builds";
        check(built.ok, label.c_str());
        if (!built.ok) {
            std::cerr << "  (" << built.error << ")\n";
            continue;
        }
        label = std::string(testCase.name) + " has extent";
        check(built.width > 0.0f && built.height > 0.0f, label.c_str());
        label = std::string(testCase.name) + " emits primitives";
        check(!built.prims.empty(), label.c_str());
    }
}

void testExtFallbacks() {
    auto sequence = mermaidext::build(
        mermaidext::Kind::Sequence,
        "sequenceDiagram\n    createParticipant A\n", fakeMeasure, 1.0f);
    check(!sequence.ok, "unknown sequence statement falls back");
    auto composite = mermaidext::build(
        mermaidext::Kind::State,
        "stateDiagram-v2\n    state Outer {\n        [*] --> A\n    }\n",
        fakeMeasure, 1.0f);
    check(!composite.ok, "composite states fall back");
    auto pie = mermaidext::build(mermaidext::Kind::Pie,
                                 "pie\n    \"A\" : -3\n", fakeMeasure, 1.0f);
    check(!pie.ok, "negative pie values fall back");
}

} // namespace

int main(int argc, char** argv) {
    testStyledFlowchart();
    testGraphAliasesAndChaining();
    testLayoutDirections();
    testSubgraphs();
    testUnsupportedDiagram();
    testBomAndSemicolonStatements();
    testCyclicLayout();
    testBackslashNLineBreak();
    testAttributeSyntaxRejected();
    testLayoutExposesRanks();
    testDetectKind();
    testExtBuilders();
    testExtFallbacks();
    testFiles(argc, argv);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Mermaid tests passed\n";
    return 0;
}
