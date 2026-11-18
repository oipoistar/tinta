#include "mermaid.h"
#include "mermaid_ext.h"
#include "c4_label_checks.h"
#include "mermaid_layout_checks.h"

#include <algorithm>
#include <cctype>
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

// #281: a double-quoted label may run over lines, as Mermaid allows, and its
// line breaks are the label's; quotes around edge text delimit it
const mermaid::Edge* findEdge(const mermaid::Diagram& diagram, const std::string& from,
                              const std::string& to) {
    for (const auto& edge : diagram.edges) {
        if (diagram.nodes[edge.from].id == from && diagram.nodes[edge.to].id == to) return &edge;
    }
    return nullptr;
}

void testMultilineQuotedLabels() {
    // The reporter's shape: front matter, a comment, tabs, CRLF, circles,
    // hyphenated IDs, a loop and edge labels running over two to four lines
    const std::string source =
        "---\r\ntitle: State Machine\r\n---\r\n%% stateDiagram-v2\r\ngraph TB\r\n\r\n"
        "\tSTART((START)) -- \"Nop\" --> MSH((MSH))\r\n"
        "\tMSH((MSH)) -- \"Push:ORDERS\r\nPush:LABORDERS\" --> PID-NTE((PID-NTE))\r\n"
        "\tPID-NTE((PID-NTE)) -- \"Nop\" --> PID-NTE((PID-NTE))\r\n"
        "\tPID-NTE((PID-NTE)) -- \"Pop\r\nPop\r\n    Pop\r\nPop\" --> MSH((MSH))\r\n"
        "\tMSH -->|\"Quoted\"| N[\"first line\r\n   second line\"]\r\n"
        "\tN -- \"an --> inside\" --> START\r\n";
    auto result = mermaid::parse(source);
    check(result.success, "labels running over lines parse");
    if (!result.success) {
        std::cerr << "  line " << result.errorLine << ": " << result.error << '\n';
        return;
    }
    const auto* nop = findEdge(result.diagram, "START", "MSH");
    check(nop && nop->label == "Nop", "quotes around an edge's text are not part of it");
    const auto* push = findEdge(result.diagram, "MSH", "PID-NTE");
    check(push && push->label == "Push:ORDERS\nPush:LABORDERS",
          "a quoted edge text keeps its line break, without the CR");
    const auto* loop = findEdge(result.diagram, "PID-NTE", "PID-NTE");
    check(loop && loop->label == "Nop", "a node's edge to itself parses");
    const auto* pops = findEdge(result.diagram, "PID-NTE", "MSH");
    check(pops && pops->label == "Pop\nPop\nPop\nPop",
          "four lines, the indentation of one not part of the text");
    const auto* quoted = findEdge(result.diagram, "MSH", "N");
    check(quoted && quoted->label == "Quoted", "quotes in a |text| label delimit it too");
    const auto* node = findNode(result.diagram, "N");
    check(node && node->label == "first line\nsecond line", "a quoted node label runs over lines");
    const auto* arrow = findEdge(result.diagram, "N", "START");
    check(arrow && arrow->label == "an --> inside", "an arrow inside quoted edge text stays text");
    check(result.diagram.nodes.size() == 4, "the diagram has its four nodes");

    // Errors after a label that ran over lines still name their own line
    auto broken = mermaid::parse("graph TD\nA -- \"one\ntwo\" --> B\nC ?? D\n");
    check(!broken.success && broken.errorLine == 4, "a later error names its own line");

    // A single quote is not a Mermaid string: an apostrophe joins nothing
    auto apostrophe = mermaid::parse("graph TD\nA[Don't stop] --> B\nB --> C\n");
    check(apostrophe.success && apostrophe.diagram.edges.size() == 2 &&
              findEdge(apostrophe.diagram, "B", "C"),
          "an apostrophe in a label leaves the next line alone");
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
        {Kind::C4, "c4",
         "C4Context\n    Person(a, \"A\")\n    System(b, \"B\")\n"
         "    Rel(a, b, \"Uses\")\n"},
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

// The ```mermaid fences of a Markdown file, in order
std::vector<std::string> mermaidFences(const std::string& markdown) {
    std::vector<std::string> fences;
    std::string current;
    bool inside = false;
    size_t position = 0;
    while (position < markdown.size()) {
        size_t newline = markdown.find('\n', position);
        if (newline == std::string::npos) newline = markdown.size();
        std::string line = markdown.substr(position, newline - position);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!inside && line.rfind("```mermaid", 0) == 0) {
            inside = true;
            current.clear();
        } else if (inside && line == "```") {
            inside = false;
            fences.push_back(current);
        } else if (inside) {
            current += line + "\n";
        }
        position = newline + 1;
    }
    return fences;
}

// What the app does with a fence: draws it, or shows its source
bool drawsDiagram(const std::string& source, std::string& error) {
    const auto kind = mermaidext::detectKind(source);
    if (kind == mermaidext::Kind::None) {
        error = "diagram type not recognised";
        return false;
    }
    if (kind == mermaidext::Kind::Flowchart) {
        auto parsed = mermaid::parse(source);
        error = parsed.error;
        return parsed.success;
    }
    auto built = mermaidext::build(kind, source, fakeMeasure, 1.0f);
    error = built.error;
    return built.ok && !built.prims.empty();
}

// #261: a diagram may open with YAML front matter (title, config). The
// fixture's first five fences put it on other diagram types; the rest are
// the 25 examples of Mermaid's ER documentation, in order.
void testFrontMatterFixture() {
    std::ifstream file(TINTA_MERMAID_FRONT_MATTER_FIXTURE, std::ios::binary);
    check(static_cast<bool>(file), "front matter fixture opens");
    const std::string markdown((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    const auto fences = mermaidFences(markdown);
    check(fences.size() == 30, "the fixture has 5 front matter and 25 ER docs fences");
    // Fences that draw: every one with front matter (1-6, 9, 30) and the
    // ER docs examples whose syntax the ER renderer knows. The others use
    // ER syntax it does not support yet (cardinalities in words, a quoted
    // entity on its own, subgraphs, style and classDef), and show their
    // source instead of a wrong diagram.
    const std::vector<size_t> draws = {1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 14, 15, 16, 18, 19, 30};
    for (size_t i = 0; i < fences.size(); ++i) {
        std::string error;
        const bool drawn = drawsDiagram(fences[i], error);
        const bool expected =
            std::find(draws.begin(), draws.end(), i + 1) != draws.end();
        if (drawn != expected) {
            std::cerr << "  fence " << i + 1 << (drawn ? " draws" : " shows source: " + error) << '\n';
        }
        check(drawn == expected, "a fence draws exactly when its syntax is supported");
    }
    // The front matter is skipped whole, whatever YAML it holds, and only
    // when it opens the source
    using mermaidext::skipFrontMatter;
    check(skipFrontMatter("---\ntitle: T\n---\nerDiagram\n") == "erDiagram\n",
          "a title block is skipped");
    check(skipFrontMatter("\xEF\xBB\xBF\n---\r\nconfig:\r\n  theme: dark\r\n---  \r\npie\r\n") ==
              "pie\r\n",
          "a byte order mark, a blank line, CRLF and trailing spaces are fine");
    check(skipFrontMatter("---\ntitle: never closed\nerDiagram\n") ==
              "---\ntitle: never closed\nerDiagram\n",
          "an unclosed block is not front matter");
    check(skipFrontMatter("graph TD\nA---B\n---\n") == "graph TD\nA---B\n---\n",
          "a --- line after the diagram starts is not front matter");
    check(skipFrontMatter("---\n---\n").empty(), "an empty block leaves nothing");
    // The flowchart parser blanks the block, so error lines still count
    // from the top of the fence
    auto bad = mermaid::parse("---\ntitle: T\n---\nflowchart LR\n  A -- B\n");
    check(!bad.success && bad.errorLine == 5,
          "a flowchart error after front matter reports its own line");
}

// The ids of a diagram's asymmetric nodes, in order, separated by spaces
std::string asymmetricIds(const mermaid::Diagram& diagram) {
    std::string ids;
    for (const auto& node : diagram.nodes) {
        if (node.shape != mermaid::NodeShape::Asymmetric) continue;
        if (!ids.empty()) ids += ' ';
        ids += node.id;
    }
    return ids;
}

// id>text] is Mermaid's asymmetric shape. It drew as one rectangle whose id
// and label were the whole "S>Screen]".
void testAsymmetricShape() {
    auto screen = mermaid::parse("flowchart LR\nR -.-> S>Screen]\n");
    check(screen.success, "an edge to an asymmetric node parses");
    if (screen.success) {
        const auto* node = findNode(screen.diagram, "S");
        check(node && node->label == "Screen" &&
                  node->shape == mermaid::NodeShape::Asymmetric,
              "S>Screen] is node S, labelled Screen, asymmetric");
        check(screen.diagram.nodes.size() == 2 && screen.diagram.edges.size() == 1 &&
                  screen.diagram.edges[0].dashed && findEdge(screen.diagram, "R", "S"),
              "the dotted edge runs from R to S");
    }

    // Quoted text as for the other shapes: '>', ']' and ';' inside, a label
    // over two lines; unquoted text keeps ';' and '|' as a bracketed one does
    auto labels = mermaid::parse(
        "flowchart LR\n"
        "A>\"a > b ] c; d\"] --> B>\"first line\n   second line\"]\n"
        "C>one; two | three] --> D; D --> E\n"
        "F>Classed]:::hot\n"
        "G --> H\n"
        "H>Defined later]\n"
        "subgraph Group\n"
        "    I>Inside]\n"
        "end\n");
    check(labels.success, "asymmetric labels parse");
    if (labels.success) {
        const auto& diagram = labels.diagram;
        const auto* a = findNode(diagram, "A");
        check(a && a->label == "a > b ] c; d", "a quoted label keeps '>', ']' and ';'");
        const auto* b = findNode(diagram, "B");
        check(b && b->label == "first line\nsecond line", "a quoted label runs over two lines");
        const auto* c = findNode(diagram, "C");
        check(c && c->label == "one; two | three", "an unquoted label keeps ';' and '|'");
        check(findEdge(diagram, "C", "D") && findEdge(diagram, "D", "E"),
              "a ';' after the label still separates statements");
        const auto* f = findNode(diagram, "F");
        check(f && f->className == "hot", "a class follows the label");
        const auto* h = findNode(diagram, "H");
        check(h && h->label == "Defined later", "a node used before takes the shape later");
        check(diagram.subgraphs.size() == 1 && diagram.subgraphs[0].nodes.size() == 1 &&
                  diagram.nodes[diagram.subgraphs[0].nodes[0]].id == "I",
              "an asymmetric node belongs to its subgraph");
        check(asymmetricIds(diagram) == "A B C F H I", "every '>' node is asymmetric");
    }

    // A '>' that belongs to an arrow or to text never opens the shape
    struct Case {
        const char* line;
        const char* asymmetric;  // ids of the asymmetric nodes
        const char* edgeLabel;
    };
    const Case cases[] = {
        {"A-->B", "", ""},
        {"A --> B", "", ""},
        {"A ==> B", "", ""},
        {"A -.-> B", "", ""},
        {"A==>B>Text]", "B", ""},
        {"A-.->B>Text]", "B", ""},
        {"A --> B>Text]", "B", ""},
        {"A>Text] --> B", "A", ""},
        {"A>Text]-->B", "A", ""},
        {"A -- text --> B", "", "text"},
        {"A -- a > b --> B", "", "a > b"},
        {"A -->|a > b| B", "", "a > b"},
        {"A>Text] -->|a > b| B>\"c > d\"]", "A B", "a > b"},
        {"A[\"a > b\"] --> B(\"c > d\")", "", ""},
    };
    for (const auto& item : cases) {
        const std::string name = std::string("'") + item.line + "'";
        auto parsed = mermaid::parse(std::string("flowchart LR\n") + item.line + "\n");
        check(parsed.success, (name + " parses").c_str());
        if (!parsed.success) continue;
        const auto& diagram = parsed.diagram;
        check(diagram.nodes.size() == 2 && findNode(diagram, "A") && findNode(diagram, "B"),
              (name + " has nodes A and B, and no others").c_str());
        check(asymmetricIds(diagram) == item.asymmetric,
              (name + " has asymmetric nodes '" + item.asymmetric + "', not '" +
               asymmetricIds(diagram) + "'").c_str());
        check(diagram.edges.size() == 1 && diagram.edges[0].directed &&
                  diagram.edges[0].label == item.edgeLabel,
              (name + " has one directed edge labelled '" + item.edgeLabel + "'").c_str());
    }
    auto thick = mermaid::parse("flowchart LR\nA==>B\n");
    check(thick.success && thick.diagram.edges.size() == 1 &&
              thick.diagram.edges[0].strokeScale == 2.0f,
          "'A==>B' stays a thick arrow");
    auto dotted = mermaid::parse("flowchart LR\nA-.->B\n");
    check(dotted.success && dotted.diagram.edges.size() == 1 && dotted.diagram.edges[0].dashed,
          "'A-.->B' stays a dotted arrow");

    // In edge text, a '>' after a letter opens no label either: the ';'
    // after the edge still separates
    auto edgeText = mermaid::parse("flowchart LR\nA -- a>b --> B; B --> C\nC -->|x>y| D; D --> E\n");
    check(edgeText.success && edgeText.diagram.edges.size() == 4 &&
              edgeText.diagram.edges[0].label == "a>b" &&
              edgeText.diagram.edges[2].label == "x>y" && asymmetricIds(edgeText.diagram).empty(),
          "a '>' in edge text stays text");

    // The '>' of "->", which Mermaid does not have, opens no node either:
    // the diagram shows its source
    auto arrowTypo = mermaid::parse("flowchart LR\nA->B\n");
    check(!arrowTypo.success && arrowTypo.error == "Unsupported edge syntax",
          "'A->B' shows its source as an unsupported edge");

    check(mermaid::leftNotch(mermaid::NodeShape::Asymmetric, 52.0f) == 26.0f &&
              mermaid::leftNotch(mermaid::NodeShape::Rectangle, 52.0f) == 0.0f &&
              mermaid::leftNotch(mermaid::NodeShape::Hexagon, 52.0f) == 0.0f,
          "only the asymmetric shape is notched, half its height in");
}

// A longer link is the same edge as its short form; Mermaid's extra length
// only stretches it over more ranks. Links Tinta does not draw (circle and
// cross ends, both-way and invisible links) show the source.
void testLongLinks() {
    struct Case {
        const char* line;
        bool directed;
        bool dashed;
        float strokeScale;
        const char* label;
    };
    const Case cases[] = {
        {"A-->B", true, false, 1.0f, ""},
        {"A ---> B", true, false, 1.0f, ""},
        {"A---->B", true, false, 1.0f, ""},
        {"A ==> B", true, false, 2.0f, ""},
        {"A ===> B", true, false, 2.0f, ""},
        {"A====>B", true, false, 2.0f, ""},
        {"A -.-> B", true, true, 1.0f, ""},
        {"A -..-> B", true, true, 1.0f, ""},
        {"A-...->B", true, true, 1.0f, ""},
        {"A --- B", false, false, 1.0f, ""},
        {"A ---- B", false, false, 1.0f, ""},
        {"A === B", false, false, 2.0f, ""},
        {"A====B", false, false, 2.0f, ""},
        {"A -.- B", false, true, 1.0f, ""},
        {"A -..- B", false, true, 1.0f, ""},
        {"A -- text --> B", true, false, 1.0f, "text"},
        {"A -- text ---> B", true, false, 1.0f, "text"},
        {"A-- a-b --->B", true, false, 1.0f, "a-b"},
        {"A -- text --- B", false, false, 1.0f, "text"},
        {"A == text ==> B", true, false, 2.0f, "text"},
        {"A == text ===> B", true, false, 2.0f, "text"},
        {"A == text === B", false, false, 2.0f, "text"},
        {"A -. text .-> B", true, true, 1.0f, "text"},
        {"A -. text ..-> B", true, true, 1.0f, "text"},
        {"A -. text .- B", false, true, 1.0f, "text"},
        {"A -. \"x .-> y\" ..-> B", true, true, 1.0f, "x .-> y"},
        {"A ---->|text| B", true, false, 1.0f, "text"},
        {"A ===>|a > b| B", true, false, 2.0f, "a > b"},
        {"A ---> B>Text]", true, false, 1.0f, ""},
        {"A>Text] ===> B", true, false, 2.0f, ""},
        {"A -..-> B>\"c > d\"]", true, true, 1.0f, ""},
    };
    for (const auto& item : cases) {
        const std::string name = std::string("'") + item.line + "'";
        auto parsed = mermaid::parse(std::string("flowchart TD\n") + item.line + "\n");
        check(parsed.success, (name + " parses").c_str());
        if (!parsed.success) {
            std::cerr << "  " << parsed.error << '\n';
            continue;
        }
        const auto& diagram = parsed.diagram;
        check(diagram.nodes.size() == 2 && findNode(diagram, "A") && findNode(diagram, "B"),
              (name + " has nodes A and B, and no others").c_str());
        const auto* edge = findEdge(diagram, "A", "B");
        check(diagram.edges.size() == 1 && edge && edge->directed == item.directed &&
                  edge->dashed == item.dashed && edge->strokeScale == item.strokeScale &&
                  edge->label == item.label,
              (name + " is the edge its short form draws").c_str());
    }

    // Mermaid's own example of a longer link
    auto docs = mermaid::parse(
        "flowchart TD\n"
        "    A[Start] --> B{Is it?}\n"
        "    B -->|Yes| C[OK]\n"
        "    C --> D[Rethink]\n"
        "    D --> B\n"
        "    B ---->|No| E[End]\n");
    const auto* no = docs.success ? findEdge(docs.diagram, "B", "E") : nullptr;
    check(docs.success && docs.diagram.nodes.size() == 5 && docs.diagram.edges.size() == 5 &&
              no && no->directed && no->label == "No",
          "Mermaid's 'B ---->|No| E[End]' is an edge labelled No");

    for (const char* line : {"A->B", "A -> B", "A --o B", "A --x B", "Ao--oB", "A o--o B",
                             "A x--x B", "A<-->B", "A <--> B", "A ~~~ B", "A~~~B", "A---oB",
                             "A ==x B", "A -.-o B", "A --o B --> C"}) {
        auto parsed = mermaid::parse(std::string("flowchart LR\n") + line + "\n");
        check(!parsed.success && parsed.error.find("edge") != std::string::npos,
              (std::string("'") + line + "' shows its source").c_str());
    }
}

// The asymmetric shape fixture: every fence parses, with its asymmetric nodes
void testAsymmetricFixture() {
    std::ifstream file(TINTA_MERMAID_ASYMMETRIC_FIXTURE, std::ios::binary);
    check(static_cast<bool>(file), "asymmetric shape fixture opens");
    const std::string markdown((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    const auto fences = mermaidFences(markdown);
    const std::vector<std::string> expected = {"Route Screen", "S A C D", "Parse Out", "Flag Back",
                                               "E", "Arrow Dotted DottedOpen ThickText"};
    check(fences.size() == expected.size(), "the fixture has six Mermaid fences");
    for (size_t i = 0; i < fences.size() && i < expected.size(); ++i) {
        auto parsed = mermaid::parse(fences[i]);
        if (!parsed.success) {
            std::cerr << "  fence " << i + 1 << ':' << parsed.errorLine << ": " << parsed.error << '\n';
        }
        check(parsed.success && asymmetricIds(parsed.diagram) == expected[i],
              ("fixture fence " + std::to_string(i + 1) + " has asymmetric nodes " +
               expected[i]).c_str());
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
    check(composite.ok, "a composite state draws instead of falling back");
    auto unclosed = mermaidext::build(
        mermaidext::Kind::State,
        "stateDiagram-v2\n    state Outer {\n        [*] --> A\n", fakeMeasure, 1.0f);
    check(!unclosed.ok, "an unclosed composite state falls back");
    auto stray = mermaidext::build(
        mermaidext::Kind::State, "stateDiagram-v2\n    A --> B\n    }\n",
        fakeMeasure, 1.0f);
    check(!stray.ok, "a } with no composite open falls back");
    auto across = mermaidext::build(
        mermaidext::Kind::State,
        "stateDiagram-v2\n    state P {\n        A --> B\n        --\n"
        "        C --> D\n    }\n    A --> C\n",
        fakeMeasure, 1.0f);
    check(!across.ok, "a transition between concurrent regions falls back");
    auto pie = mermaidext::build(mermaidext::Kind::Pie,
                                 "pie\n    \"A\" : -3\n", fakeMeasure, 1.0f);
    check(!pie.ok, "negative pie values fall back");
}

// --- C4 relation labels ---
// A relation's label sat on the middle of its line, so between two shapes
// 24 px apart it covered both of them and their text, and the labels of
// relations between the same shapes covered each other

// Builds a C4 source and checks every label: clear of the boxes, frames,
// captions, text, arrowheads and other labels, and on its own line
mermaidext::Built checkC4Labels(const std::string& name,
                                const std::string& source, float scale) {
    auto built = mermaidext::build(mermaidext::Kind::C4, source, fakeMeasure,
                                   scale);
    const std::string at = name + (scale == 1.0f ? " at 1x" : " at 1.5x");
    check(built.ok, (at + " builds").c_str());
    if (!built.ok) {
        std::cerr << "  (" << built.error << ")\n";
        return built;
    }
    const auto problems = c4checks::labelProblems(built, source, scale);
    for (const auto& problem : problems) {
        std::cerr << "  " << at << ": " << problem << '\n';
    }
    check(problems.empty(),
          (at + ": each label is clear of boxes, text, frames, arrowheads "
                "and the other labels, on its own line").c_str());
    return built;
}

// The lines drawn between two element boxes
std::vector<const mermaidext::Prim*> linesBetween(const c4checks::Parts& parts,
                                                  const c4checks::Box& a,
                                                  const c4checks::Box& b) {
    std::vector<const mermaidext::Prim*> lines;
    for (const auto* line : parts.lines) {
        mermaidext::Point p{line->x1, line->y1}, q{line->x2, line->y2};
        if ((c4checks::onEdge(p, a) && c4checks::onEdge(q, b)) ||
            (c4checks::onEdge(p, b) && c4checks::onEdge(q, a))) {
            lines.push_back(line);
        }
    }
    return lines;
}

const char* const kC4Reported = R"(C4Context
    title Tinta in context
    Person(writer, "Writer", "Reads and edits Markdown")
    System(tinta, "Tinta", "Native Markdown viewer and editor")
    System_Ext(plantuml, "PlantUML", "Optional diagram tool")
    System_Ext(github, "GitHub Releases", "Update check for portable builds")
    Rel(writer, tinta, "Opens files in")
    Rel(tinta, plantuml, "Renders diagrams with")
    Rel(tinta, github, "Checks for updates")
)";

void testC4ReportedLabels() {
    for (float scale : {1.0f, 1.5f}) {
        auto built = checkC4Labels("reported diagram", kC4Reported, scale);
        if (!built.ok) continue;
        auto parts = c4checks::partsOf(built);
        c4checks::Box writer, tinta, plantuml, github, opens, renders, updates;
        bool found = c4checks::elementBox(parts, "Writer", writer) &&
                     c4checks::elementBox(parts, "Tinta", tinta) &&
                     c4checks::elementBox(parts, "PlantUML", plantuml) &&
                     c4checks::elementBox(parts, "GitHub Releases", github) &&
                     c4checks::chipBox(parts, "Opens files in", opens) &&
                     c4checks::chipBox(parts, "Renders diagrams with", renders) &&
                     c4checks::chipBox(parts, "Checks for updates", updates);
        check(found, "the reported diagram draws its boxes and labels");
        if (!found) continue;
        // The gaps widen to hold the labels, which keep to the free space
        check(opens.x1 > writer.x2 && opens.x2 < tinta.x1,
              "\"Opens files in\" sits between Writer and Tinta");
        check(renders.x1 > tinta.x2 && renders.x2 < plantuml.x1,
              "\"Renders diagrams with\" sits between Tinta and PlantUML");
        check(updates.y1 > tinta.y2 && updates.y2 < github.y1,
              "\"Checks for updates\" sits between Tinta and GitHub Releases");
        check(!c4checks::overlaps(opens, tinta) &&
                  !c4checks::overlaps(renders, tinta),
              "no label covers the Tinta box or its description");
    }
}

void testC4LabelLayouts() {
    struct Case {
        const char* name;
        const char* source;
    };
    const Case kCases[] = {
        {"direction keywords", R"(C4Container
    Container(a, "Alpha", "Go")
    Container(b, "Beta", "Go")
    Container(c, "Gamma", "Go")
    Container(d, "Delta", "Go")
    Container(e, "Epsilon", "Go")
    Container(f, "Zeta", "Go")
    Rel_R(a, b, "Right to")
    Rel_Right(b, c, "Right again")
    Rel_L(c, b, "Left to")
    Rel_Left(b, a, "Left again")
    Rel_D(a, d, "Down to")
    Rel_Down(b, e, "Down again")
    Rel_U(f, c, "Up to")
    Rel_Up(e, b, "Up again")
    Rel_Back(d, e, "Back to")
    Rel_Neighbor(e, f, "Next to")
)"},
        {"BiRel", R"(C4Context
    System(shop, "Bookshop", "Sells books online")
    System_Ext(warehouse, "Warehouse", "Picks and ships orders")
    System_Ext(publisher, "Publisher feed", "New titles and prices")
    System_Ext(accounting, "Accounting", "Invoices and ledgers")
    BiRel(shop, warehouse, "Syncs stock with", "EDI")
    Rel(warehouse, shop, "Reports shipments to")
    Rel(publisher, warehouse, "Announces new titles to")
    BiRel(shop, accounting, "Exchanges invoices with")
    BiRel(accounting, warehouse, "Reconciles stock with")
)"},
        {"nested boundaries", R"(C4Container
    title Nested boundaries
    Person(customer, "Customer", "Orders books")
    Enterprise_Boundary(company, "Bookshop Ltd") {
        System_Boundary(shop, "Bookshop") {
            Container(web, "Web shop", "TypeScript", "Catalogue")
            Container(api, "Shop API", "Go", "Orders")
            Container(admin, "Back office", "Django")
            ContainerDb(db, "Orders", "PostgreSQL", "Orders and stock")
        }
        Boundary(ops, "Operations", "team") {
            Container(jobs, "Jobs", "Go", "Nightly work")
        }
    }
    System_Ext(pay, "Payment provider")
    Rel(customer, web, "Orders through", "HTTPS")
    Rel(web, api, "Calls", "JSON")
    Rel(api, db, "Stores orders in", "SQL")
    Rel(admin, api, "Manages stock with")
    Rel(jobs, db, "Archives old orders in")
    Rel(api, pay, "Charges cards with")
    Rel(pay, customer, "Sends receipts to")
)"},
        {"three rows", R"(C4Component
    Container(spa, "Single-page app", "TypeScript", "Runs in the browser")
    Component(router, "Router", "Go", "Maps URLs to handlers")
    Component(auth, "Auth", "Go", "Checks sessions")
    Component(catalogue, "Catalogue", "Go", "Lists and searches books")
    Component(basket, "Basket", "Go", "Holds chosen books")
    Component(orders, "Orders", "Go", "Turns baskets into orders")
    ComponentDb(store, "Store", "PostgreSQL", "Books and orders")
    ComponentQueue(events, "Events", "NATS", "Order events")
    Rel(spa, router, "Sends requests to", "JSON/HTTPS")
    Rel(router, auth, "Checks sessions with")
    Rel(router, catalogue, "Routes searches to")
    Rel(router, basket, "Routes basket edits to")
    Rel(basket, orders, "Checks out through")
    Rel(catalogue, store, "Reads books from", "SQL")
    Rel(orders, store, "Writes orders to", "SQL")
    Rel(orders, events, "Publishes to")
    Rel(auth, events, "Logs sign-ins to")
    Rel(spa, auth, "Refreshes tokens with")
)"},
        {"dense neighbours", R"(C4Container
    Person(user, "User", "Reads and writes")
    Container(spa, "Single-page app", "TypeScript")
    Container(api, "API", "Go")
    ContainerDb(db, "Documents", "PostgreSQL")
    ContainerQueue(bus, "Event bus", "Kafka")
    Container(worker, "Indexer", "Go")
    Rel(user, spa, "Edits in")
    Rel(spa, user, "Shows changes to")
    Rel(spa, api, "Saves through", "JSON/HTTPS")
    Rel(api, spa, "Pushes edits to", "WebSocket")
    BiRel(spa, api, "Keeps a session with")
    Rel(api, db, "Reads and writes", "SQL")
    Rel(db, api, "Notifies", "LISTEN")
    Rel(api, bus, "Publishes changes to")
    Rel(bus, worker, "Feeds")
    Rel(worker, bus, "Acknowledges")
    BiRel(worker, api, "Reports progress to")
)"},
        {"fan-out", R"(C4Context
    System(left, "Left", "Beside the person")
    Person(person, "Person", "Uses everything")
    System(right, "Right", "Beside the person")
    System(a, "A", "Below, left")
    System(b, "B", "Below, middle")
    System(c, "C", "Below, right")
    Rel(person, a, "Uses A for this")
    Rel(person, b, "Uses B for that")
    Rel(person, c, "Uses C for the rest")
    Rel(person, left, "Talks to")
    Rel(person, right, "Talks to as well")
    Rel(left, a, "Feeds")
)"},
        {"deployment", R"(C4Deployment
    Deployment_Node(laptop, "Laptop", "Windows 11") {
        Deployment_Node(folder, "Portable folder", "tinta.exe") {
            Container(viewer, "Viewer", "C++17", "Draws Markdown")
            Container(settings, "Settings", "INI file")
        }
        Node(java, "Java", "OpenJDK") {
            Container(plantuml, "PlantUML", "Java")
        }
    }
    Node_R(cloud, "GitHub", "Cloud") {
        Container(releases, "Releases", "JSON API")
    }
    Node_L(spare, "Spare node")
    Rel(viewer, settings, "Reads", "INI")
    Rel(viewer, plantuml, "Renders through", "stdin")
    Rel(viewer, releases, "Checks for updates", "HTTPS")
)"},
        {"self relation", R"(C4Container
    Container(api, "API", "Go", "Serves requests")
    Container(db, "Store", "SQL")
    Rel(api, api, "Retries failed calls")
    Rel(api, db, "Reads")
    Rel(db, db, "Vacuums")
)"},
    };
    for (const auto& testCase : kCases) {
        for (float scale : {1.0f, 1.5f}) {
            checkC4Labels(testCase.name, testCase.source, scale);
        }
    }

    // Relations between the same two shapes get lines of their own, side
    // by side, rather than one line under two labels
    auto dense = mermaidext::build(mermaidext::Kind::C4, kCases[4].source,
                                   fakeMeasure, 1.0f);
    auto parts = c4checks::partsOf(dense);
    c4checks::Box spa, api, user;
    if (c4checks::elementBox(parts, "Single-page app", spa) &&
        c4checks::elementBox(parts, "API", api) &&
        c4checks::elementBox(parts, "User", user)) {
        auto threeLanes = linesBetween(parts, spa, api);
        auto twoLanes = linesBetween(parts, user, spa);
        // BiRel draws its line twice, once per arrowhead
        check(threeLanes.size() == 4 && twoLanes.size() == 2,
              "each relation between two shapes has its own line");
        bool apart = true;
        for (const auto* lanes : {&threeLanes, &twoLanes}) {
            for (size_t i = 0; i < lanes->size(); i++) {
                for (size_t j = i + 1; j < lanes->size(); j++) {
                    const auto* p = (*lanes)[i];
                    const auto* q = (*lanes)[j];
                    bool reversed = p->x1 == q->x2 && p->y1 == q->y2 &&
                                    p->x2 == q->x1 && p->y2 == q->y1;
                    if (!reversed && std::abs(p->y1 - q->y1) < 10.0f &&
                        std::abs(p->y2 - q->y2) < 10.0f) {
                        apart = false;
                    }
                }
            }
        }
        check(apart, "lanes between the same two shapes run apart");
    } else {
        check(false, "the dense diagram draws its boxes");
    }

    // A relation passing behind a box keeps a lane beside the relation that
    // ends at that box, so neither label sits on the other's line
    {
        auto rows = mermaidext::build(mermaidext::Kind::C4, kCases[3].source,
                                      fakeMeasure, 1.0f);
        auto rowParts = c4checks::partsOf(rows);
        c4checks::Box browser, router, auth;
        bool found =
            c4checks::elementBox(rowParts, "Single-page app", browser) &&
            c4checks::elementBox(rowParts, "Router", router) &&
            c4checks::elementBox(rowParts, "Auth", auth);
        auto toRouter = linesBetween(rowParts, browser, router);
        auto toAuth = linesBetween(rowParts, browser, auth);
        check(found && toRouter.size() == 1 && toAuth.size() == 1 &&
                  std::abs(toRouter[0]->y1 - toAuth[0]->y1) >= 10.0f,
              "a line passing behind a box runs beside the line ending there");
    }

    // Without labels the layout keeps its 24 px and 26 px gaps
    const char* plain = R"(C4Context
    Person(a, "A")
    System(b, "B")
    System(c, "C")
    System(d, "D")
    Rel(a, b, "")
    Rel(a, d, "")
)";
    auto unlabelled =
        mermaidext::build(mermaidext::Kind::C4, plain, fakeMeasure, 1.0f);
    auto plainParts = c4checks::partsOf(unlabelled);
    c4checks::Box a, b, c, d;
    check(c4checks::elementBox(plainParts, "A", a) &&
              c4checks::elementBox(plainParts, "B", b) &&
              c4checks::elementBox(plainParts, "C", c) &&
              c4checks::elementBox(plainParts, "D", d) &&
              std::abs(b.x1 - a.x2 - 24.0f) < 0.01f &&
              std::abs(c.x1 - b.x2 - 24.0f) < 0.01f &&
              std::abs(d.y1 - std::max({a.y2, b.y2, c.y2}) - 26.0f) < 0.01f &&
              plainParts.chips.empty(),
          "a diagram without labels keeps its gaps");

    // The last resort: a loop on an element boxed in by its own boundary's
    // frame and a close neighbour takes its label off to the right of the
    // diagram, joined to the loop by a leader
    const char* boxedIn = R"(C4Context
    System_Boundary(box, "Box") {
        System(alone, "Alone", "Alone in its boundary")
    }
    System(next, "Next", "Right beside it")
    Rel(alone, alone, "Checks itself")
)";
    auto spilled = checkC4Labels("boxed-in loop", boxedIn, 1.0f);
    auto spilledParts = c4checks::partsOf(spilled);
    c4checks::Box chip, next;
    check(c4checks::chipBox(spilledParts, "Checks itself", chip) &&
              c4checks::elementBox(spilledParts, "Next", next) &&
              chip.x1 > next.x2 && chip.x2 <= spilled.width,
          "a label with no room left goes right of the diagram");
}

// The fixture's diagrams, among other Markdown, all keep their labels clear
void testC4LabelFixture() {
    std::ifstream file(TINTA_MERMAID_C4_FIXTURE, std::ios::binary);
    check(static_cast<bool>(file), "C4 label fixture opens");
    const std::string markdown((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    const auto fences = mermaidFences(markdown);
    check(fences.size() == 8, "the C4 label fixture has its eight diagrams");
    for (size_t i = 0; i < fences.size(); i++) {
        check(mermaidext::detectKind(fences[i]) == mermaidext::Kind::C4,
              "every fixture fence is a C4 diagram");
        for (float scale : {1.0f, 1.5f}) {
            checkC4Labels("fixture fence " + std::to_string(i + 1), fences[i],
                          scale);
        }
    }
}

// Crowded diagrams: eight rows of elements, each related to the next three,
// so lines cross every row and gap; and sixteen relations between the same
// two elements, more lanes than their boxes can hold apart
void testC4CrowdedLabels() {
    std::string rows = "C4Container\n";
    for (int i = 0; i < 24; i++) {
        rows += "Container(e" + std::to_string(i) + ", \"Element " +
                std::to_string(i) + "\", \"Go\", \"Does part " +
                std::to_string(i) + " of the work\")\n";
    }
    for (int i = 0; i < 24; i++) {
        for (int step = 1; step <= 3 && i + step < 24; step++) {
            rows += "Rel(e" + std::to_string(i) + ", e" +
                    std::to_string(i + step) + ", \"Calls " +
                    std::to_string(i) + " to " + std::to_string(i + step) +
                    "\", \"JSON\")\n";
        }
    }
    checkC4Labels("eight crowded rows", rows, 1.0f);

    std::string pair = "C4Container\n"
                       "Container(a, \"Client\", \"Go\")\n"
                       "Container(b, \"Server\", \"Go\")\n";
    for (int i = 0; i < 16; i++) {
        pair += "Rel(" + std::string(i % 2 ? "b, a" : "a, b") +
                ", \"Message " + std::to_string(i) + "\", \"HTTPS\")\n";
    }
    checkC4Labels("sixteen relations between two elements", pair, 1.0f);
}

// --- state, class and ER layouts: nothing drawn over anything else ---

using mermaidcheck::Box;
using mermaidcheck::hasText;
using mermaidcheck::overlap;
using mermaidcheck::textBox;

// Prints what collides in a diagram under `name` (see mermaid_layout_checks.h)
void checkLayout(const std::string& name, const mermaidext::Built& built, float scale,
                 bool state = true) {
    auto problems = mermaidcheck::layoutProblems(built, scale, state);
    for (size_t i = 0; i < problems.size() && i < 12; i++) {
        std::cerr << "  " << name << ": " << problems[i] << '\n';
    }
    check(problems.empty(), ("nothing overlaps in " + name).c_str());
}

std::string withDirection(const std::string& source, const char* direction) {
    size_t newline = source.find('\n');
    return source.substr(0, newline + 1) + "    direction " + direction + "\n" +
           source.substr(newline + 1);
}

// fakeMeasure at a zoom, as the app measures text at its scale
mermaidext::Measure scaledMeasure(float scale) {
    return [scale](const std::string& text, const mermaidext::TextStyle& style,
                   float wrap) {
        mermaidext::Size size = fakeMeasure(text, style, wrap);
        return mermaidext::Size{size.w * scale, size.h * scale};
    };
}

// Closer to the app's text: a 14 px font scaled by the style and zoom,
// uneven glyph widths, and wrapping at the given width
mermaidext::Measure proportionalMeasure(float scale) {
    return [scale](const std::string& text, const mermaidext::TextStyle& style,
                   float wrap) {
        float font = 14.0f * style.scale * scale * (style.bold ? 1.04f : 1.0f);
        float lineWidth = 0.0f;
        float widest = 0.0f;
        int lines = text.empty() ? 0 : 1;
        for (char c : text) {
            if (c == '\n') {
                lines++;
                lineWidth = 0.0f;
                continue;
            }
            float glyph = font * (c == ' '                                         ? 0.28f
                                  : std::isupper(static_cast<unsigned char>(c)) ? 0.63f
                                                                                : 0.513f);
            if (wrap > 0.0f && lineWidth + glyph > wrap) {
                lines++;
                lineWidth = 0.0f;
            }
            lineWidth += glyph;
            widest = std::max(widest, lineWidth);
        }
        return mermaidext::Size{widest, static_cast<float>(lines) * font * 1.3301f};
    };
}

const char* const kStateRepro = R"(stateDiagram-v2
    [*] --> Reading
    Reading --> Editing : press colon
    Editing --> Reading : Esc Esc
    Editing --> Saving : Ctrl+S
    Saving --> Editing
    Reading --> Searching : F
    Searching --> Reading : Esc
    Reading --> [*] : Q
    note right of Editing : Source on the left, preview on the right
)";

// A back edge and a note beside a state with neighbours: the label chip,
// the note and the edge all landed on "Searching"
void testStateOverlapRepro() {
    const char* const kDirections[] = {"TB", "LR", "BT", "RL"};
    for (float scale : {1.0f, 1.75f}) {
        for (const char* direction : kDirections) {
            std::string source = std::string(direction) == "TB"
                                     ? std::string(kStateRepro)
                                     : withDirection(kStateRepro, direction);
            auto built = mermaidext::build(mermaidext::Kind::State, source,
                                           scaledMeasure(scale), scale);
            std::string name = std::string("repro ") + direction + " at " +
                               std::to_string(scale).substr(0, 4);
            checkLayout(name, built, scale);
            Box searching = textBox(built, "Searching");
            check(!overlap(textBox(built, "Esc Esc"), searching),
                  "the Esc Esc label clears Searching");
            check(!overlap(textBox(built, "Source on the left, preview on the right"),
                           searching),
                  "the note clears Searching");
        }
    }
}

// Machines with self-loops, notes on both sides and stacked on one state,
// choices, forks and joins, long labels, and edges back over many ranks
void testStateMachines() {
    const char* const kMachines[] = {
        R"(stateDiagram-v2
    [*] --> Stopped
    Stopped --> Playing : play
    Playing --> Paused : pause
    Paused --> Playing : resume
    Playing --> Stopped : stop
    Paused --> Stopped : stop
    Playing --> Playing : next track
    Stopped --> [*] : eject
    note left of Paused : Position is kept
    note right of Playing : Volume keys work here
)",
        R"(stateDiagram-v2
    state check <<choice>>
    state split <<fork>>
    state merge <<join>>
    [*] --> Received
    Received --> check : validate
    check --> Rejected : invalid
    check --> split : valid
    split --> Packing
    split --> Billing
    Packing --> merge
    Billing --> merge
    merge --> Shipped
    Shipped --> Delivered : signed for
    Shipped --> Received : returned to sender
    Rejected --> [*]
    Delivered --> [*]
    Received --> Cancelled : customer cancels before packing
    Cancelled --> [*]
)",
        R"(stateDiagram-v2
    direction LR
    [*] --> Idle
    Idle --> Connecting : dial
    Connecting --> Connected : handshake ok
    Connecting --> Idle : timeout
    Connecting --> Connecting : retry
    Connected --> Idle : hang up
    Connected --> Error : lost carrier
    Error --> Connecting : redial
    Error --> Idle : give up
    Idle --> [*]
    note right of Connecting : Three tries at most
    note left of Connected : Keep-alive every 30 s
)",
        R"(stateDiagram-v2
    [*] --> A
    A --> B : one
    A --> C : two
    A --> D : three
    B --> E : four
    C --> E : five
    D --> E : six
    E --> A : back to the start
    E --> B : again
    D --> A : undo
    C --> C : stay
    C --> C : and stay
    B --> F : skip ahead
    E --> F : finish
    F --> [*]
    A --> F : shortcut over everything
    note right of E : first note
    note right of E : second note on the same state
    note left of A : notes on both sides
)",
        R"(stateDiagram-v2
    direction BT
    s1 : A state with
    s1 : two descriptions
    [*] --> s1
    s1 --> s2 : a long transition label that is wider than any state
    s2 --> s3
    s3 --> s1 : back
    s2 --> s2 : loop
    s3 --> [*]
    note left of s2 : left of s2
)",
    };
    const char* const kDirections[] = {"", "LR", "BT", "RL"};
    size_t index = 0;
    for (const char* machine : kMachines) {
        index++;
        for (const char* direction : kDirections) {
            std::string source = *direction ? withDirection(machine, direction)
                                            : std::string(machine);
            for (float scale : {1.0f, 1.75f}) {
                auto built = mermaidext::build(mermaidext::Kind::State, source,
                                               scaledMeasure(scale), scale);
                checkLayout("machine " + std::to_string(index) + " " +
                                (*direction ? direction : "as written") + " at " +
                                std::to_string(scale).substr(0, 4),
                            built, scale);
            }
            // Uneven text at uneven zooms, as printing and the HTML export
            // lay diagrams out; an exact lane spacing once looped forever
            for (float scale : {0.7f, 0.8333f, 1.15f, 1.37f}) {
                auto built = mermaidext::build(mermaidext::Kind::State, source,
                                               proportionalMeasure(scale), scale);
                checkLayout("machine " + std::to_string(index) + " " +
                                (*direction ? direction : "as written") +
                                " with real text at " + std::to_string(scale).substr(0, 4),
                            built, scale);
            }
        }
    }
}

// The box drawn for a composite state, found by the name in its header
Box containerNamed(const mermaidext::Built& built, const std::string& name, float scale) {
    using mermaidext::PrimType;
    using mermaidext::Role;
    for (const auto& prim : built.prims) {
        if (prim.type != PrimType::RoundRect || prim.fill != Role::None ||
            prim.stroke != Role::Stroke) {
            continue;
        }
        Box box{prim.x1, prim.y1, prim.x2, prim.y2};
        for (const auto& text : built.prims) {
            if (text.type == PrimType::Text && text.text == name &&
                mermaidcheck::within({text.x1, text.y1, text.x2, text.y2}, box) &&
                text.y1 < box.top + 10.0f * scale) {
                return box;
            }
        }
    }
    return {};
}

bool drawn(const Box& box) { return box.right > box.left; }

// Start dots and end rings inside `box`, and outside it
struct Ends {
    int startsIn = 0;
    int startsOut = 0;
    int endsIn = 0;
    int endsOut = 0;
};

Ends endsAround(const mermaidext::Built& built, const Box& box) {
    using mermaidext::PrimType;
    using mermaidext::Role;
    std::vector<Box> rings;
    std::vector<Box> dots;
    for (const auto& prim : built.prims) {
        if (prim.type != PrimType::Ellipse) continue;
        Box shape{prim.x1, prim.y1, prim.x2, prim.y2};
        if (prim.stroke == Role::Accent) rings.push_back(shape);
        else if (prim.fill == Role::Accent) dots.push_back(shape);
    }
    Ends ends;
    for (const Box& ring : rings) {
        (mermaidcheck::within(ring, box) ? ends.endsIn : ends.endsOut)++;
    }
    for (const Box& dot : dots) {
        bool inRing = false;
        for (const Box& ring : rings) inRing = inRing || mermaidcheck::within(dot, ring);
        if (!inRing) (mermaidcheck::within(dot, box) ? ends.startsIn : ends.startsOut)++;
    }
    return ends;
}

// Transitions that reach the border of `box` from outside it: they start
// on it, or stop an arrowhead short of it
int endsOnBorder(const mermaidext::Built& built, const Box& box, float scale) {
    float reach = 8.5f * scale;
    int count = 0;
    for (const auto& line : mermaidcheck::shapesOf(built, scale, true).lines) {
        for (int side = 0; side < 2; side++) {
            const mermaidext::Point& end = side ? line.b : line.a;
            const mermaidext::Point& other = side ? line.a : line.b;
            bool alongX = end.x > box.left - 0.5f && end.x < box.right + 0.5f;
            bool alongY = end.y > box.top - 0.5f && end.y < box.bottom + 0.5f;
            bool atBorder =
                (alongX && ((end.y <= box.top + 0.5f && end.y >= box.top - reach) ||
                            (end.y >= box.bottom - 0.5f && end.y <= box.bottom + reach))) ||
                (alongY && ((end.x <= box.left + 0.5f && end.x >= box.left - reach) ||
                            (end.x >= box.right - 0.5f && end.x <= box.right + reach)));
            if (atBorder && mermaidcheck::outside(other, box)) count++;
        }
    }
    return count;
}

// Dashed lines between concurrent regions inside `box`
int dividersIn(const mermaidext::Built& built, const Box& box, float scale) {
    int count = 0;
    for (const auto& prim : built.prims) {
        if (prim.type == mermaidext::PrimType::Line && prim.dashed &&
            prim.stroke == mermaidext::Role::Muted && prim.strokeWidth < 1.2f * scale &&
            mermaidcheck::within({std::min(prim.x1, prim.x2), std::min(prim.y1, prim.y2),
                                  std::max(prim.x1, prim.x2), std::max(prim.y1, prim.y2)},
                                 box)) {
            count++;
        }
    }
    return count;
}

const char* const kCompositeExample = R"(stateDiagram-v2
    [*] --> Reading
    Reading --> Editing : press colon
    Editing --> Reading : Esc Esc
    state Editing {
        [*] --> Typing
        Typing --> Selecting : Shift+arrows
        Selecting --> Typing
    }
    Reading --> [*] : Q
)";

// Composite states draw as a box with their name over their own diagram,
// with its own start and end; transitions to the composite end on its
// border, and nothing crosses it
void testCompositeStates() {
    const char* const kDirections[] = {"", "LR", "BT", "RL"};
    for (float scale : {1.0f, 1.75f}) {
        for (const char* direction : kDirections) {
            std::string source = *direction ? withDirection(kCompositeExample, direction)
                                            : std::string(kCompositeExample);
            auto built = mermaidext::build(mermaidext::Kind::State, source,
                                           scaledMeasure(scale), scale);
            std::string name = std::string("composite example ") +
                               (*direction ? direction : "TB") + " at " +
                               std::to_string(scale).substr(0, 4);
            checkLayout(name, built, scale);
            Box editing = containerNamed(built, "Editing", scale);
            check(drawn(editing), "Editing draws as a box around its states");
            check(mermaidcheck::within(textBox(built, "Typing"), editing) &&
                      mermaidcheck::within(textBox(built, "Selecting"), editing),
                  "Typing and Selecting are inside Editing");
            check(!overlap(textBox(built, "Reading"), editing), "Reading is outside Editing");
            Ends ends = endsAround(built, editing);
            check(ends.startsIn == 1 && ends.startsOut == 1 && ends.endsOut == 1 &&
                      ends.endsIn == 0,
                  "Editing's own start is inside it, apart from the diagram's start and end");
            check(endsOnBorder(built, editing, scale) >= 2,
                  "the transitions to and from Editing end on its border");
            check(hasText(built, "Shift+arrows") &&
                      mermaidcheck::within(textBox(built, "Shift+arrows"), editing),
                  "the inner label is inside Editing");
        }
    }

    // Nesting three deep, as in Mermaid's documentation
    const char* const kNested = R"(stateDiagram-v2
    [*] --> First
    state First {
        [*] --> Second
        state Second {
            [*] --> second
            second --> Third
            state Third {
                [*] --> third
                third --> [*]
            }
        }
    }
    First --> [*]
)";
    // Concurrent regions, as in Mermaid's documentation
    const char* const kConcurrent = R"(stateDiagram-v2
    [*] --> Active
    state Active {
        [*] --> NumLockOff
        NumLockOff --> NumLockOn : EvNumLockPressed
        NumLockOn --> NumLockOff : EvNumLockPressed
        --
        [*] --> CapsLockOff
        CapsLockOff --> CapsLockOn : EvCapsLockPressed
        CapsLockOn --> CapsLockOff : EvCapsLockPressed
        --
        [*] --> ScrollLockOff
        ScrollLockOff --> ScrollLockOn : EvScrollLockPressed
        ScrollLockOn --> ScrollLockOff : EvScrollLockPressed
    }
    Active --> [*] : power off
)";
    // A long name, its own direction, notes inside and on it, a loop on
    // it, and transitions between states outside and inside it
    const char* const kAcross = R"(stateDiagram-v2
    direction LR
    [*] --> Idle
    state "Text editing" as Editing {
        direction LR
        [*] --> Typing
        Typing --> Selecting : Shift+arrows
        Selecting --> Typing : any key
        Typing --> [*] : Esc
        note right of Typing : Caret visible
    }
    Idle --> Typing : open file
    Selecting --> Idle : close
    Editing --> Saving : Ctrl+S
    Saving --> Editing
    Editing --> Editing : undo
    note left of Editing : Edit mode
    Saving --> [*]
)";
    // The diagram itself split into regions
    const char* const kTopRegions = R"(stateDiagram-v2
    [*] --> Left
    Left --> [*]
    --
    [*] --> Right
    Right --> Right : tick
)";
    for (float scale : {1.0f, 1.75f}) {
        for (const char* direction : kDirections) {
            auto variant = [&](const char* source) {
                return *direction ? withDirection(source, direction) : std::string(source);
            };
            std::string suffix = std::string(" ") + (*direction ? direction : "as written") +
                                 " at " + std::to_string(scale).substr(0, 4);
            auto measure = scaledMeasure(scale);

            auto nested = mermaidext::build(mermaidext::Kind::State, variant(kNested),
                                            measure, scale);
            checkLayout("nested composites" + suffix, nested, scale);
            Box first = containerNamed(nested, "First", scale);
            Box second = containerNamed(nested, "Second", scale);
            Box third = containerNamed(nested, "Third", scale);
            check(drawn(first) && drawn(second) && drawn(third) &&
                      mermaidcheck::within(second, first) &&
                      mermaidcheck::within(third, second) &&
                      mermaidcheck::within(textBox(nested, "third"), third),
                  "nested composites sit inside each other");
            Ends thirdEnds = endsAround(nested, third);
            check(thirdEnds.startsIn == 1 && thirdEnds.endsIn == 1,
                  "the innermost composite has its own start and end");

            auto concurrent = mermaidext::build(mermaidext::Kind::State,
                                                variant(kConcurrent), measure, scale);
            checkLayout("concurrent regions" + suffix, concurrent, scale);
            Box active = containerNamed(concurrent, "Active", scale);
            check(drawn(active) && dividersIn(concurrent, active, scale) == 2,
                  "three concurrent regions are parted by two dashed lines");
            Ends activeEnds = endsAround(concurrent, active);
            check(activeEnds.startsIn == 3, "each concurrent region has its own start");
            // No state of one region lies across a divider
            for (const auto& prim : concurrent.prims) {
                if (prim.type != mermaidext::PrimType::Line || !prim.dashed ||
                    prim.strokeWidth > 1.2f * scale) {
                    continue;
                }
                mermaidcheck::Segment divider{{prim.x1, prim.y1}, {prim.x2, prim.y2}};
                for (const char* state : {"NumLockOff", "CapsLockOn", "ScrollLockOff"}) {
                    check(!mermaidcheck::pierces(divider, textBox(concurrent, state)),
                          "a divider passes between the regions' states");
                }
            }

            auto across = mermaidext::build(mermaidext::Kind::State, variant(kAcross),
                                            measure, scale);
            checkLayout("transitions across a composite" + suffix, across, scale);
            Box editing = containerNamed(across, "Text editing", scale);
            check(drawn(editing) && mermaidcheck::within(textBox(across, "Typing"), editing),
                  "a composite named with as shows its long name");
            check(!overlap(textBox(across, "Idle"), editing) &&
                      endsOnBorder(across, editing, scale) >= 5,
                  "transitions from outside to states inside end on the composite");
            check(mermaidcheck::within(textBox(across, "Caret visible"), editing) &&
                      !overlap(textBox(across, "Edit mode"), editing),
                  "a note on an inner state is inside, a note on the composite outside");

            auto top = mermaidext::build(mermaidext::Kind::State, variant(kTopRegions),
                                         measure, scale);
            checkLayout("concurrent diagram" + suffix, top, scale);
            check(dividersIn(top, {0.0f, 0.0f, top.width, top.height}, scale) == 1,
                  "a diagram split by -- draws a dashed line between its regions");

            // A transition into a state two composites deep ends on the
            // outer one, also when it names the state before the composite
            // that holds it; an empty composite is a box with its name
            const std::string nestedBody =
                std::string(kNested).substr(std::string(kNested).find('\n') + 1);
            const std::string deepSource =
                "stateDiagram-v2\n    [*] --> Idle\n    Idle --> third : jump\n" + nestedBody +
                "    state Empty {\n    }\n    Idle --> Empty\n";
            auto deep = mermaidext::build(mermaidext::Kind::State, variant(deepSource.c_str()),
                                          measure, scale);
            checkLayout("transition two composites deep" + suffix, deep, scale);
            Box outer = containerNamed(deep, "First", scale);
            check(drawn(outer) && !overlap(textBox(deep, "Idle"), outer) &&
                      endsOnBorder(deep, outer, scale) >= 1,
                  "a transition into a nested state ends on the outermost composite");
            check(mermaidcheck::within(textBox(deep, "third"),
                                       containerNamed(deep, "Third", scale)),
                  "a state named at the top first still belongs to its composite");
            check(drawn(containerNamed(deep, "Empty", scale)),
                  "an empty composite draws as a box with its name");
        }
    }
}

// Class and ER diagrams share the router: their edges and chips keep clear
// too, with markers and multiplicities at both ends
void testClassAndErLayouts() {
    const char* const kClass = R"(classDiagram
    Animal <|-- Duck
    Animal <|-- Fish
    Animal <|-- Zebra
    Animal : +int age
    Animal : +isMammal()
    Duck : +swim()
    Fish : +swim()
    Zebra : +run()
    Duck "1" --> "*" Pond : lives in
    Pond --> Duck : feeds
    Zebra --> Zebra : herds with
    Pond ..> Animal : waters
)";
    const char* const kEr = R"(erDiagram
    CUSTOMER ||--o{ ORDER : places
    CUSTOMER ||--o{ ADDRESS : has
    ORDER ||--|{ LINE-ITEM : contains
    LINE-ITEM }o--|| PRODUCT : describes
    PRODUCT ||--o{ CUSTOMER : "recommended to"
    CUSTOMER {
        string name PK
        string email
    }
)";
    for (float scale : {1.0f, 1.75f}) {
        auto classes = mermaidext::build(mermaidext::Kind::Class, kClass,
                                         scaledMeasure(scale), scale);
        checkLayout("class diagram", classes, scale, false);
        check(hasText(classes, "lives in") && hasText(classes, "*"),
              "class labels and multiplicities are drawn");
        auto entities = mermaidext::build(mermaidext::Kind::Er, kEr,
                                          scaledMeasure(scale), scale);
        checkLayout("ER diagram", entities, scale, false);
        check(hasText(entities, "recommended to"), "ER labels are drawn");
    }
}

} // namespace

// --- Subgraph directions (#326) ---
// A subgraph's "direction" was read and dropped, and every node of a diagram
// was ranked along the diagram's direction. In a left-to-right diagram the
// boxes of two groups, one above the other, also ran 8 px into each other:
// the nodes of a rank were 32 apart, and a box reaches 10 below its nodes
// and 30, with its title, above them.

struct DrawnFlowchart {
    mermaid::ParseResult parsed;
    mermaid::LayoutOptions options;
    mermaid::Layout layout;
    mermaid::Placement placed;
};

// Lays a flowchart out with the viewer's distances: nodes as wide as their
// labels, a label chip on every labelled edge, a title on every group
DrawnFlowchart drawFlowchart(const std::string& source) {
    DrawnFlowchart drawn;
    drawn.parsed = mermaid::parse(source);
    if (!drawn.parsed.success) return drawn;
    const auto& diagram = drawn.parsed.diagram;
    std::vector<mermaid::Size> sizes;
    for (const auto& node : diagram.nodes) {
        sizes.push_back({std::max(120.0f, 36.0f + 9.0f * static_cast<float>(node.label.size())),
                         node.shape == mermaid::NodeShape::Diamond ? 82.0f : 52.0f});
    }
    drawn.options = mermaid::drawingOptions(1.0f);
    for (const auto& edge : diagram.edges) {
        drawn.options.edgeLabels.push_back(
            edge.label.empty()
                ? mermaid::Size{}
                : mermaid::Size{std::min(180.0f, 13.0f + 9.0f * static_cast<float>(edge.label.size())),
                                28.0f});
    }
    for (const auto& subgraph : diagram.subgraphs) {
        drawn.options.groupTitles.push_back(16.0f + 9.0f * static_cast<float>(subgraph.label.size()));
    }
    drawn.layout = mermaid::layout(diagram, sizes, drawn.options);
    drawn.placed = mermaid::place(diagram, drawn.layout, drawn.options, 40.0f, 25.0f);
    return drawn;
}

size_t nodeIndex(const mermaid::Diagram& diagram, const std::string& id) {
    for (size_t i = 0; i < diagram.nodes.size(); i++) {
        if (diagram.nodes[i].id == id) return i;
    }
    return SIZE_MAX;
}

size_t groupIndex(const mermaid::Diagram& diagram, const std::string& id) {
    for (size_t i = 0; i < diagram.subgraphs.size(); i++) {
        if (diagram.subgraphs[i].id == id) return i;
    }
    return SIZE_MAX;
}

mermaidcheck::Box boxOf(const mermaid::Rect& rect) {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

std::string describe(const mermaid::Rect& rect) {
    return mermaidcheck::describe(boxOf(rect));
}

// How a row of nodes runs: each after the one before it along the
// direction, and all on one line across it (inLine; a rank that holds more
// than the node is centred as a whole, and its node is off that line)
bool runsAlong(const DrawnFlowchart& drawn, const std::vector<std::string>& ids,
               mermaid::Direction direction, bool inLine = true) {
    const auto& diagram = drawn.parsed.diagram;
    for (size_t i = 0; i < ids.size(); i++) {
        size_t node = nodeIndex(diagram, ids[i]);
        if (node == SIZE_MAX) return false;
        if (drawn.layout.directions[node] != direction) return false;
        if (i == 0) continue;
        const auto& before = drawn.placed.nodes[nodeIndex(diagram, ids[i - 1])];
        const auto& rect = drawn.placed.nodes[node];
        const float centreX = (rect.left + rect.right) * 0.5f;
        const float centreY = (rect.top + rect.bottom) * 0.5f;
        const float beforeX = inLine ? (before.left + before.right) * 0.5f : centreX;
        const float beforeY = inLine ? (before.top + before.bottom) * 0.5f : centreY;
        switch (direction) {
            case mermaid::Direction::TopToBottom:
                if (!(before.bottom < rect.top && std::abs(centreX - beforeX) < 0.5f)) return false;
                break;
            case mermaid::Direction::BottomToTop:
                if (!(rect.bottom < before.top && std::abs(centreX - beforeX) < 0.5f)) return false;
                break;
            case mermaid::Direction::LeftToRight:
                if (!(before.right < rect.left && std::abs(centreY - beforeY) < 0.5f)) return false;
                break;
            case mermaid::Direction::RightToLeft:
                if (!(rect.right < before.left && std::abs(centreY - beforeY) < 0.5f)) return false;
                break;
        }
    }
    return true;
}

// What must hold for the group boxes of any drawn flowchart: a box holds its
// nodes and the boxes inside it, the boxes of two groups that are not inside
// one another stay apart, and no box runs over a node that is not in it. A
// block also holds the edges and the labels of its nodes.
void checkGroupBoxes(const DrawnFlowchart& drawn, const std::string& name) {
    const auto& diagram = drawn.parsed.diagram;
    const auto& placed = drawn.placed;
    check(placed.nodes.size() == diagram.nodes.size() &&
          placed.groups.size() == diagram.subgraphs.size() &&
          placed.edges.size() == diagram.edges.size(),
          (name + ": every node, group and edge is placed").c_str());
    if (placed.nodes.size() != diagram.nodes.size() ||
        placed.groups.size() != diagram.subgraphs.size()) {
        return;
    }
    auto parentOf = [&](size_t group) { return diagram.subgraphs[group].parent; };
    auto inside = [&](size_t group, size_t outer) {
        for (size_t g = parentOf(group); g != SIZE_MAX; g = parentOf(g)) {
            if (g == outer) return true;
        }
        return false;
    };
    std::vector<size_t> groupOf(diagram.nodes.size(), SIZE_MAX);
    for (size_t s = 0; s < diagram.subgraphs.size(); s++) {
        for (size_t node : diagram.subgraphs[s].nodes) groupOf[node] = s;
    }
    auto holds = [&](size_t group, size_t node) {
        for (size_t g = groupOf[node]; g != SIZE_MAX; g = parentOf(g)) {
            if (g == group) return true;
        }
        return false;
    };

    for (size_t s = 0; s < diagram.subgraphs.size(); s++) {
        const auto& box = placed.groups[s];
        const std::string group = name + ": group " + diagram.subgraphs[s].id;
        if (mermaid::isEmpty(box)) continue;
        for (size_t node = 0; node < diagram.nodes.size(); node++) {
            const auto& rect = placed.nodes[node];
            if (holds(s, node)) {
                check(mermaidcheck::within(boxOf(rect), boxOf(box)),
                      (group + " holds its node " + diagram.nodes[node].id + ": " +
                       describe(rect) + " in " + describe(box)).c_str());
            } else {
                check(!mermaidcheck::overlap(boxOf(rect), boxOf(box)),
                      (group + " " + describe(box) + " keeps off node " +
                       diagram.nodes[node].id + " " + describe(rect)).c_str());
            }
        }
        for (size_t other = s + 1; other < diagram.subgraphs.size(); other++) {
            const auto& otherBox = placed.groups[other];
            if (mermaid::isEmpty(otherBox)) continue;
            if (inside(other, s)) {
                check(mermaidcheck::within(boxOf(otherBox), boxOf(box)),
                      (group + " holds the group " + diagram.subgraphs[other].id).c_str());
            } else {
                // Not only apart: with room for both borders between them
                mermaidcheck::Box grown = boxOf(box);
                grown.left -= 4.0f;
                grown.top -= 4.0f;
                grown.right += 4.0f;
                grown.bottom += 4.0f;
                check(!mermaidcheck::overlap(grown, boxOf(otherBox)),
                      (group + " " + describe(box) + " stays apart from group " +
                       diagram.subgraphs[other].id + " " + describe(otherBox)).c_str());
            }
        }
        if (!drawn.layout.blocks[s]) continue;
        for (size_t e = 0; e < diagram.edges.size(); e++) {
            if (!holds(s, diagram.edges[e].from)) continue;
            const auto& route = placed.edges[e];
            bool held = !route.points.empty();
            for (const auto& point : route.points) {
                held = held && point.x >= box.left && point.x <= box.right &&
                       point.y >= box.top && point.y <= box.bottom;
            }
            if (route.hasLabel) {
                held = held && mermaidcheck::within(boxOf(route.label), boxOf(box));
            }
            check(held, (group + " holds edge " + std::to_string(e + 1) + " and its label").c_str());
        }
    }
    // An edge of the diagram stays out of the blocks beside its nodes
    for (size_t e = 0; e < diagram.edges.size(); e++) {
        const auto& edge = diagram.edges[e];
        const auto& route = placed.edges[e];
        for (size_t s = 0; s < diagram.subgraphs.size(); s++) {
            if (!drawn.layout.blocks[s] || holds(s, edge.from) || holds(s, edge.to)) continue;
            if (drawn.layout.ranks[edge.from] + 1 != drawn.layout.ranks[edge.to]) continue;
            for (size_t i = 1; i < route.points.size(); i++) {
                mermaidcheck::Segment line{{route.points[i - 1].x, route.points[i - 1].y},
                                           {route.points[i].x, route.points[i].y}};
                check(!mermaidcheck::pierces(line, boxOf(placed.groups[s])),
                      (name + ": edge " + std::to_string(e + 1) + " keeps out of block " +
                       diagram.subgraphs[s].id).c_str());
            }
        }
    }
}

const char* const kReportedSubgraphDirections = R"(flowchart LR
    subgraph current [Current]
        direction TB
        A1[Start] --> A2[Process] --> A3[Finish]
    end
    subgraph expected [Expected]
        direction TB
        B1[Start] --> B2[Review] --> B3[Finish]
    end
)";

void testSubgraphDirectionParsing() {
    using mermaid::Direction;
    auto reported = mermaid::parse(kReportedSubgraphDirections);
    check(reported.success && reported.diagram.subgraphs.size() == 2,
          "the reported flowchart parses with its two subgraphs");
    if (reported.success && reported.diagram.subgraphs.size() == 2) {
        check(reported.diagram.direction == Direction::LeftToRight,
              "a subgraph's direction leaves the diagram's as it is");
        for (const auto& subgraph : reported.diagram.subgraphs) {
            check(subgraph.hasDirection && subgraph.direction == Direction::TopToBottom,
                  ("subgraph " + subgraph.id + " keeps direction TB").c_str());
        }
    }

    const struct {
        const char* value;
        Direction direction;
    } values[] = {{"TB", Direction::TopToBottom}, {"TD", Direction::TopToBottom},
                  {"BT", Direction::BottomToTop}, {"LR", Direction::LeftToRight},
                  {"RL", Direction::RightToLeft}, {"lr", Direction::LeftToRight}};
    for (const auto& value : values) {
        auto parsed = mermaid::parse(std::string("flowchart TB\n  subgraph S\n    direction ") +
                                     value.value + ";\n    a --> b\n  end\n");
        check(parsed.success && parsed.diagram.subgraphs.size() == 1 &&
              parsed.diagram.subgraphs[0].hasDirection &&
              parsed.diagram.subgraphs[0].direction == value.direction,
              (std::string("direction ") + value.value + " is kept").c_str());
    }

    auto nested = mermaid::parse(
        "flowchart LR\n  subgraph outer\n    direction TB\n    subgraph inner\n"
        "      direction RL\n      a --> b\n    end\n    subgraph plain\n      c --> d\n    end\n"
        "  end\n");
    check(nested.success && nested.diagram.subgraphs.size() == 3, "nested directions parse");
    if (nested.success && nested.diagram.subgraphs.size() == 3) {
        const auto& groups = nested.diagram.subgraphs;
        check(groups[0].hasDirection && groups[0].direction == Direction::TopToBottom,
              "the outer subgraph keeps its own direction");
        check(groups[1].hasDirection && groups[1].direction == Direction::RightToLeft,
              "the inner subgraph keeps its own direction");
        check(!groups[2].hasDirection, "a subgraph with no statement has no direction");
    }

    auto twice = mermaid::parse(
        "flowchart LR\n  subgraph S\n    direction TB\n    a --> b\n    direction RL\n  end\n");
    check(twice.success && twice.diagram.subgraphs[0].direction == Direction::RightToLeft,
          "the last direction of a subgraph counts");

    auto unsupported = mermaid::parse(
        "flowchart LR\n  subgraph S\n    direction XY\n    a --> b\n  end\n");
    check(!unsupported.success && unsupported.errorLine == 3 &&
          unsupported.error == "Unsupported flowchart direction",
          "an unsupported subgraph direction is reported with its line");
    auto header = mermaid::parse("flowchart XY\n  a --> b\n");
    check(!header.success && header.error == unsupported.error,
          "as an unsupported direction after flowchart is");

    auto outside = mermaid::parse("flowchart LR\n  direction TB\n  a --> b\n");
    check(outside.success && outside.diagram.direction == Direction::LeftToRight,
          "a direction outside any subgraph is passed over, as before");
    auto empty = mermaid::parse("flowchart LR\n  subgraph S\n    direction\n    a --> b\n  end\n");
    check(empty.success && !empty.diagram.subgraphs[0].hasDirection,
          "a direction with no value sets none");
}

void testSubgraphDirectionLayout() {
    using mermaid::Direction;

    // The reported flowchart: each group top to bottom, the two groups one
    // below the other as two unlinked nodes of a left-to-right diagram are
    auto reported = drawFlowchart(kReportedSubgraphDirections);
    check(reported.parsed.success, "the reported flowchart lays out");
    if (reported.parsed.success) {
        const auto& diagram = reported.parsed.diagram;
        check(reported.layout.blocks.size() == 2 && reported.layout.blocks[0] &&
              reported.layout.blocks[1],
              "both reported groups are laid out on their own");
        check(runsAlong(reported, {"A1", "A2", "A3"}, Direction::TopToBottom),
              "Current runs top to bottom: one x, growing y");
        check(runsAlong(reported, {"B1", "B2", "B3"}, Direction::TopToBottom),
              "Expected runs top to bottom: one x, growing y");
        const auto& current = reported.placed.groups[0];
        const auto& expected = reported.placed.groups[1];
        check(current.bottom < expected.top &&
              std::abs(current.left - expected.left) < 0.5f,
              "the diagram stays left to right: its two unlinked groups share a rank");
        check(reported.layout.scopes[nodeIndex(diagram, "A1")] == 1 &&
              reported.layout.scopes[nodeIndex(diagram, "B3")] == 2,
              "each node belongs to its group's layout");
        // An edge of a top-to-bottom group leaves below and arrives on top
        const auto& route = reported.placed.edges[0];
        const auto& from = reported.placed.nodes[nodeIndex(diagram, "A1")];
        const auto& to = reported.placed.nodes[nodeIndex(diagram, "A2")];
        check(route.points.size() == 4 && route.points.front().y == from.bottom &&
              route.points.back().y == to.top &&
              route.points.front().x == route.points.back().x,
              "an edge inside the group runs straight down");
        checkGroupBoxes(reported, "reported");
    }

    // The same with no direction: left to right as before, the boxes apart
    auto plain = drawFlowchart(
        "flowchart LR\n  subgraph current [Current]\n    A1 --> A2 --> A3\n  end\n"
        "  subgraph expected [Expected]\n    B1 --> B2 --> B3\n  end\n");
    check(plain.parsed.success, "the reported flowchart with no direction lays out");
    if (plain.parsed.success) {
        check(!plain.layout.blocks[0] && !plain.layout.blocks[1],
              "a group with no direction is no layout of its own");
        check(runsAlong(plain, {"A1", "A2", "A3"}, Direction::LeftToRight) &&
              runsAlong(plain, {"B1", "B2", "B3"}, Direction::LeftToRight),
              "with no direction the groups follow the diagram");
        check(plain.placed.groups[1].top - plain.placed.groups[0].bottom >=
                  plain.options.groupGap - 0.01f,
              "the box below starts under the box above");
        checkGroupBoxes(plain, "no direction");
    }

    // All four, side by side in a top-to-bottom diagram
    auto four = drawFlowchart(
        "flowchart TB\n"
        "  subgraph down\n    direction TB\n    d1 --> d2 --> d3\n  end\n"
        "  subgraph up\n    direction BT\n    u1 --> u2 --> u3\n  end\n"
        "  subgraph right\n    direction LR\n    r1 --> r2 --> r3\n  end\n"
        "  subgraph left\n    direction RL\n    l1 --> l2 --> l3\n  end\n");
    check(four.parsed.success, "four directions lay out");
    if (four.parsed.success) {
        check(runsAlong(four, {"d1", "d2", "d3"}, Direction::TopToBottom), "TB runs down");
        check(runsAlong(four, {"u1", "u2", "u3"}, Direction::BottomToTop), "BT runs up");
        check(runsAlong(four, {"r1", "r2", "r3"}, Direction::LeftToRight), "LR runs right");
        check(runsAlong(four, {"l1", "l2", "l3"}, Direction::RightToLeft), "RL runs left");
        const auto& groups = four.placed.groups;
        check(groups[0].right < groups[1].left && groups[1].right < groups[2].left &&
              groups[2].right < groups[3].left,
              "in a top-to-bottom diagram the unlinked groups sit side by side");
        checkGroupBoxes(four, "four directions");
    }

    // Nested: blocks inside a block, beside linked nodes of the diagram
    auto nested = drawFlowchart(
        "flowchart LR\n  Source --> Target\n"
        "  subgraph outer [Outer, top to bottom]\n    direction TB\n    o1 --> o2\n"
        "    subgraph inner1 [Inner, right to left]\n      direction RL\n      i1 --> i2 --> i3\n    end\n"
        "    subgraph inner2 [Inner, bottom to top]\n      direction BT\n      j1 --> j2\n    end\n"
        "  end\n");
    check(nested.parsed.success, "nested directions lay out");
    if (nested.parsed.success) {
        const auto& diagram = nested.parsed.diagram;
        check(nested.layout.blocks[0] && nested.layout.blocks[1] && nested.layout.blocks[2],
              "the outer and both inner groups are layouts of their own");
        check(runsAlong(nested, {"Source", "Target"}, Direction::LeftToRight, false),
              "the diagram's own nodes run left to right");
        check(runsAlong(nested, {"o1", "o2"}, Direction::TopToBottom, false),
              "the outer group's nodes run top to bottom");
        check(runsAlong(nested, {"i1", "i2", "i3"}, Direction::RightToLeft),
              "an inner group runs right to left");
        check(runsAlong(nested, {"j1", "j2"}, Direction::BottomToTop),
              "the other inner group runs bottom to top");
        check(nested.layout.groupScopes[1] == 1 && nested.layout.groupScopes[2] == 1 &&
              nested.layout.groupScopes[0] == 0,
              "the inner groups are items of the outer one, the outer of the diagram");
        // The title is wider than the two nodes: the box grows to it
        const auto& inner2 = nested.placed.groups[groupIndex(diagram, "inner2")];
        check(inner2.right - inner2.left >= nested.options.groupTitles[2] - 0.01f,
              "a block is as wide as its title");
        const auto& low = nested.placed.nodes[nodeIndex(diagram, "j1")];
        check(std::abs((low.left + low.right) - (inner2.left + inner2.right)) < 1.0f,
              "and its nodes sit in the middle of it");
        checkGroupBoxes(nested, "nested");
    }

    // Mermaid's limitation: a node linked to the outside, and the group
    // takes its parent's direction
    auto linked = drawFlowchart(
        "flowchart LR\n  subgraph kept\n    direction TB\n    top1 --> bottom1\n  end\n"
        "  subgraph linked\n    direction TB\n    top2 --> bottom2\n  end\n"
        "  Outside --> top2\n");
    check(linked.parsed.success, "a group linked to the outside lays out");
    if (linked.parsed.success) {
        check(linked.layout.blocks[0] && !linked.layout.blocks[1],
              "only the group with no edge to the outside is a layout of its own");
        check(runsAlong(linked, {"top1", "bottom1"}, Direction::TopToBottom),
              "the unlinked group keeps its direction");
        check(runsAlong(linked, {"Outside", "top2", "bottom2"}, Direction::LeftToRight, false),
              "the linked group runs with the diagram");
        checkGroupBoxes(linked, "linked");

        // The linked group is laid out as if it had no direction at all
        auto bare = drawFlowchart(
            "flowchart LR\n  subgraph kept\n    direction TB\n    top1 --> bottom1\n  end\n"
            "  subgraph linked\n    top2 --> bottom2\n  end\n"
            "  Outside --> top2\n");
        bool same = bare.parsed.success && bare.placed.nodes.size() == linked.placed.nodes.size();
        for (size_t i = 0; same && i < bare.placed.nodes.size(); i++) {
            same = bare.placed.nodes[i].left == linked.placed.nodes[i].left &&
                   bare.placed.nodes[i].top == linked.placed.nodes[i].top;
        }
        check(same, "an ignored direction changes nothing");
    }

    // The link may as well leave the group as enter it
    auto leaving = drawFlowchart(
        "flowchart LR\n  subgraph leaving\n    direction TB\n    a --> b\n  end\n  b --> Out\n");
    check(leaving.parsed.success && !leaving.layout.blocks[0] &&
          runsAlong(leaving, {"a", "b", "Out"}, Direction::LeftToRight),
          "a group with an edge out of it runs with the diagram");
    auto entered = drawFlowchart(
        "flowchart LR\n  subgraph entered\n    direction TB\n    a --> b\n  end\n  In --> a\n");
    check(entered.parsed.success && !entered.layout.blocks[0] &&
          runsAlong(entered, {"In", "a", "b"}, Direction::LeftToRight),
          "a group with an edge into it runs with the diagram");

    // A linked outer group, and inside it a group with no link of its own
    auto service = drawFlowchart(
        "flowchart TB\n  subgraph service\n    direction LR\n    api --> worker\n"
        "    subgraph retry\n      direction LR\n      try1 --> wait1 --> again\n    end\n  end\n"
        "  Client --> api\n  worker --> Store\n");
    check(service.parsed.success, "a block inside a linked group lays out");
    if (service.parsed.success) {
        check(!service.layout.blocks[0] && service.layout.blocks[1],
              "the linked outer group is none, the inner group is a layout of its own");
        check(runsAlong(service, {"Client", "api", "worker", "Store"}, Direction::TopToBottom, false),
              "the linked group runs with the diagram");
        check(runsAlong(service, {"try1", "wait1", "again"}, Direction::LeftToRight),
              "the inner group keeps its direction");
        checkGroupBoxes(service, "service");
    }

    // A group with no direction inside a block follows the block
    auto lane = drawFlowchart(
        "flowchart TB\n  subgraph lane\n    direction LR\n    p1 --> p2\n"
        "    subgraph sign\n      p3 --> p4\n    end\n    p2 --> p3\n  end\n  Start --> End\n");
    check(lane.parsed.success, "a plain group inside a block lays out");
    if (lane.parsed.success) {
        check(lane.layout.blocks[0] && !lane.layout.blocks[1],
              "the plain inner group is part of the block's layout");
        check(runsAlong(lane, {"p1", "p2", "p3", "p4"}, Direction::LeftToRight),
              "its nodes run along the block's direction");
        checkGroupBoxes(lane, "lane");
    }

    // Edges that turn back, return to their own node or carry a label stay
    // inside their block, and the block beside it stays clear of them
    auto loops = drawFlowchart(
        "flowchart LR\n"
        "  subgraph review\n    direction TB\n    w1 -->|send for review| w2{Approved?}\n"
        "    w2 -->|yes| w3\n    w2 -- no, a second round --> w1\n  end\n"
        "  subgraph build\n    direction TB\n    b1 --> b2\n    b2 -->|flaky| b2\n    b2 --> b3\n  end\n");
    check(loops.parsed.success, "loops inside blocks lay out");
    if (loops.parsed.success) {
        const auto& back = loops.placed.edges[2];
        const auto& node = loops.placed.nodes[nodeIndex(loops.parsed.diagram, "w2")];
        check(back.points.size() == 4 && back.points[1].x > node.right && back.hasLabel,
              "the edge back takes a lane beside the nodes");
        checkGroupBoxes(loops, "loops");
    }

    // A narrow node above a wide block: its edge to the next rank bends in
    // the gap between the ranks, not beside the block
    auto beside = drawFlowchart(
        "flowchart LR\n  In --> Out\n  subgraph wide\n    direction LR\n"
        "    s1 --> s2 --> s3 --> s4\n  end\n");
    check(beside.parsed.success, "a node above a wide block lays out");
    if (beside.parsed.success) {
        const auto& block = beside.placed.groups[0];
        const auto& route = beside.placed.edges[0];
        check(route.points.size() == 4 && route.points[1].x > block.right,
              "the edge bends past the block");
        checkGroupBoxes(beside, "beside");
    }
}

// Groups with no direction: the layout is the one from before, but for the
// room between the boxes of neighbours
void testNeighbouringGroupBoxes() {
    const char* const nestedRanks =
        "flowchart %s\n  subgraph north\n    subgraph northInner\n      a --> b\n    end\n  end\n"
        "  subgraph south\n    subgraph southInner\n      c --> d\n    end\n  end\n  b --> c\n";
    const char* const stacked =
        "flowchart %s\n  subgraph one\n    a1 --> a2\n  end\n"
        "  subgraph two\n    subgraph twoInner\n      b1 --> b2\n    end\n  end\n"
        "  subgraph three\n    c1 --> c2\n  end\n";
    for (const char* direction : {"TB", "BT", "LR", "RL"}) {
        for (const char* shape : {nestedRanks, stacked}) {
            std::string source = shape;
            source.replace(source.find("%s"), 2, direction);
            auto drawn = drawFlowchart(source);
            check(drawn.parsed.success, "a flowchart of plain groups lays out");
            if (!drawn.parsed.success) continue;
            checkGroupBoxes(drawn, std::string(shape == stacked ? "stacked " : "nested ") + direction);
        }
    }

    // With no group, and with the distances a caller gave before, nothing
    // moves: the nodes of a rank stay nodeGap apart and the ranks rankGap
    auto parsed = mermaid::parse("flowchart LR\n  subgraph g\n    b\n  end\n"
                                 "  subgraph h\n    c\n  end\n  a --> b\n  a --> c\n");
    check(parsed.success, "the spacing fixture parses");
    if (parsed.success) {
        std::vector<mermaid::Size> sizes(parsed.diagram.nodes.size(), {100.0f, 40.0f});
        auto layout = mermaid::layout(parsed.diagram, sizes, 20.0f, 60.0f);
        check(layout.nodes.size() == 3 && layout.nodes[0].left == 160.0f &&
              layout.nodes[1].top - layout.nodes[0].bottom == 20.0f,
              "without box sizes the nodes of two groups keep the node gap");
        mermaid::LayoutOptions options = mermaid::drawingOptions(1.0f);
        auto drawn = mermaid::layout(parsed.diagram, sizes, options);
        check(drawn.nodes.size() == 3 &&
              std::abs(drawn.nodes[1].top - drawn.nodes[0].bottom -
                       (options.groupPad * 2.0f + options.groupTitle + options.groupGap)) < 0.01f,
              "with them a pad, a title, a pad and the gap lie between the nodes");
    }
}

// Mermaid fences of a Markdown file, those inside a block quote too
std::vector<std::string> quotedMermaidFences(const std::string& markdown) {
    std::string plain;
    size_t position = 0;
    while (position < markdown.size()) {
        size_t newline = markdown.find('\n', position);
        if (newline == std::string::npos) newline = markdown.size();
        std::string line = markdown.substr(position, newline - position);
        if (line.rfind("> ", 0) == 0) {
            line.erase(0, 2);
        } else if (line == ">" || line == ">\r") {
            line.erase(0, 1);
        }
        plain += line + "\n";
        position = newline + 1;
    }
    return mermaidFences(plain);
}

// The fixture: every fence but the last draws, with the groups its text
// says are laid out on their own, and its boxes hold what they should
void testSubgraphDirectionFixture() {
    std::ifstream file(TINTA_MERMAID_SUBGRAPH_DIRECTION_FIXTURE, std::ios::binary);
    check(static_cast<bool>(file), "subgraph direction fixture opens");
    const std::string markdown((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    const auto fences = quotedMermaidFences(markdown);
    // Per fence, the ids of the subgraphs laid out on their own
    const std::vector<std::string> blocks = {
        "current expected", "", "down up right left", "steps", "outer inner1 inner2",
        "lane", "kept", "retry", "review build notes", "", ""};
    check(fences.size() == blocks.size() + 1, "the fixture has twelve Mermaid fences");
    for (size_t i = 0; i < fences.size() && i < blocks.size(); ++i) {
        const std::string name = "fixture fence " + std::to_string(i + 1);
        auto drawn = drawFlowchart(fences[i]);
        if (!drawn.parsed.success) {
            std::cerr << "  fence " << i + 1 << ':' << drawn.parsed.errorLine << ": "
                      << drawn.parsed.error << '\n';
        }
        check(drawn.parsed.success, (name + " parses").c_str());
        if (!drawn.parsed.success) continue;
        std::string own;
        for (size_t s = 0; s < drawn.parsed.diagram.subgraphs.size(); s++) {
            if (!drawn.layout.blocks[s]) continue;
            if (!own.empty()) own += ' ';
            own += drawn.parsed.diagram.subgraphs[s].id;
        }
        check(own == blocks[i],
              (name + " lays out on their own: " + blocks[i] + " (got: " + own + ")").c_str());
        checkGroupBoxes(drawn, name);
    }
    if (fences.size() == blocks.size() + 1) {
        auto unsupported = mermaid::parse(fences.back());
        check(!unsupported.success && unsupported.errorLine == 3 &&
              unsupported.error == "Unsupported flowchart direction",
              "the fixture's last fence reports its unsupported direction");
    }
}

int main(int argc, char** argv) {
    testStyledFlowchart();
    testGraphAliasesAndChaining();
    testLayoutDirections();
    testSubgraphs();
    testUnsupportedDiagram();
    testBomAndSemicolonStatements();
    testCyclicLayout();
    testBackslashNLineBreak();
    testMultilineQuotedLabels();
    testAttributeSyntaxRejected();
    testLayoutExposesRanks();
    testDetectKind();
    testExtBuilders();
    testExtFallbacks();
    testStateOverlapRepro();
    testStateMachines();
    testCompositeStates();
    testClassAndErLayouts();
    testFrontMatterFixture();
    testC4ReportedLabels();
    testC4LabelLayouts();
    testC4CrowdedLabels();
    testC4LabelFixture();
    testAsymmetricShape();
    testLongLinks();
    testAsymmetricFixture();
    testSubgraphDirectionParsing();
    testSubgraphDirectionLayout();
    testNeighbouringGroupBoxes();
    testSubgraphDirectionFixture();
    testFiles(argc, argv);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Mermaid tests passed\n";
    return 0;
}
