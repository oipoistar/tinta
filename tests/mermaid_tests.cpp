#include "mermaid.h"

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

} // namespace

int main(int argc, char** argv) {
    testStyledFlowchart();
    testGraphAliasesAndChaining();
    testLayoutDirections();
    testUnsupportedDiagram();
    testBomAndSemicolonStatements();
    testCyclicLayout();
    testAttributeSyntaxRejected();
    testLayoutExposesRanks();
    testFiles(argc, argv);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All Mermaid tests passed\n";
    return 0;
}
