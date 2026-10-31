// Sheets: the JSON Canvas model, the .smd format and the layout
// algorithms, on their own (sheet.h)

#include "sheet.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

using namespace sheet;

namespace {
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

bool overlaps(const Node& a, const Node& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

Node card(const std::string& id, float x, float y, float w = 200, float h = 100) {
    Node n;
    n.id = id;
    n.type = NodeType::File;
    n.file = id + ".md";
    n.x = x;
    n.y = y;
    n.width = w;
    n.height = h;
    return n;
}

Edge arrow(const std::string& from, const std::string& to, const std::string& label = "") {
    Edge e;
    e.id = from + "-" + to;
    e.fromNode = from;
    e.toNode = to;
    e.label = label;
    return e;
}

void canvasFormat() {
    // The shape Obsidian writes, with a field this version does not know,
    // escapes, a surrogate pair and a node type from the future
    const std::string json =
        "{\n\t\"nodes\":[\n"
        "\t\t{\"id\":\"a1\",\"type\":\"text\",\"text\":\"# Hello\\n\\nline \\\"two\\\" \\u00e9 \\ud83d\\ude00\","
        "\"x\":-100,\"y\":-50,\"width\":250,\"height\":120,\"color\":\"3\",\"styleAttributes\":{\"border\":\"dashed\"}},\n"
        "\t\t{\"id\":\"b2\",\"type\":\"file\",\"file\":\"Notes/Plan.md\",\"subpath\":\"#Goals\",\"x\":300,\"y\":0,\"width\":400,\"height\":300},\n"
        "\t\t{\"id\":\"c3\",\"type\":\"link\",\"url\":\"https://obsidian.md\",\"x\":0,\"y\":400,\"width\":300,\"height\":200,\"color\":\"#ff8800\"},\n"
        "\t\t{\"id\":\"g4\",\"type\":\"group\",\"label\":\"Phase 1\",\"x\":-200,\"y\":-200,\"width\":1000,\"height\":800,\"tintaLane\":\"status\"},\n"
        "\t\t{\"id\":\"z5\",\"type\":\"hologram\",\"x\":0,\"y\":0,\"width\":10,\"height\":10}\n"
        "\t],\n\t\"edges\":[\n"
        "\t\t{\"id\":\"e1\",\"fromNode\":\"a1\",\"fromSide\":\"right\",\"toNode\":\"b2\",\"toSide\":\"left\",\"label\":\"leads to\"},\n"
        "\t\t{\"id\":\"e2\",\"fromNode\":\"b2\",\"toNode\":\"c3\",\"fromEnd\":\"arrow\",\"toEnd\":\"none\",\"color\":\"1\"},\n"
        "\t\t{\"id\":\"e3\",\"fromNode\":\"a1\",\"toNode\":\"missing\"}\n"
        "\t],\n\t\"meta\":{\"version\":\"1.0\"}\n}\n";
    Sheet s;
    std::string error;
    check(parseCanvas(json, s, &error), "an Obsidian canvas parses: " + error);
    check(s.nodes.size() == 4, "four known nodes; the unknown type is left out");
    check(s.edges.size() == 2, "the edge to a missing node is dropped");
    if (s.nodes.size() == 4) {
        check(s.nodes[0].type == NodeType::Text && s.nodes[0].text ==
                  "# Hello\n\nline \"two\" \xC3\xA9 \xF0\x9F\x98\x80",
              "text with escapes, an accent and a surrogate pair decodes");
        check(s.nodes[0].color == "3" && s.nodes[0].x == -100 && s.nodes[0].height == 120,
              "geometry and colour read");
        check(s.nodes[0].extra.size() == 1 && s.nodes[0].extra[0].first == "styleAttributes" &&
                  s.nodes[0].extra[0].second == "{\"border\":\"dashed\"}",
              "an unknown field is kept verbatim");
        check(s.nodes[1].file == "Notes/Plan.md" && s.nodes[1].subpath == "#Goals", "file and subpath");
        check(s.nodes[2].url == "https://obsidian.md" && s.nodes[2].color == "#ff8800", "link and hex colour");
        check(s.nodes[3].type == NodeType::Group && s.nodes[3].label == "Phase 1" &&
                  s.nodes[3].laneKey == "status",
              "group label and Tinta's lane");
    }
    if (s.edges.size() == 2) {
        check(s.edges[0].fromSide == Side::Right && s.edges[0].toSide == Side::Left &&
                  !s.edges[0].fromArrow && s.edges[0].toArrow && s.edges[0].label == "leads to",
              "edge sides, default ends and label");
        check(s.edges[1].fromArrow && !s.edges[1].toArrow && s.edges[1].color == "1",
              "edge ends and colour");
    }
    check(s.extra.size() == 1 && s.extra[0].first == "meta", "a top-level unknown field is kept");

    // Written back, it reads the same
    std::string written = writeCanvas(s);
    Sheet again;
    check(parseCanvas(written, again, &error), "the written canvas parses: " + error);
    check(writeCanvas(again) == written, "writing is stable");
    check(written.find("\t\t{\"id\":\"a1\"") != std::string::npos, "one node per tab-indented line");
    check(written.find("\"styleAttributes\":{\"border\":\"dashed\"}") != std::string::npos,
          "the unknown field is written back");
    check(written.find("\"fromEnd\":\"arrow\"") != std::string::npos &&
              written.find("\"toEnd\":\"none\"") != std::string::npos,
          "non-default ends are written");

    // Broken or odd input
    Sheet broken;
    check(!parseCanvas("{\"nodes\":[{\"id\":", broken, &error) && !error.empty(),
          "truncated JSON fails with a reason");
    check(parseCanvas("", broken) && broken.nodes.empty(), "an empty file is an empty sheet");
    check(!parseCanvas("[1,2]", broken), "an array is no canvas");
    Sheet dup;
    check(parseCanvas("{\"nodes\":[{\"id\":\"x\",\"type\":\"text\",\"text\":\"a\"},"
                      "{\"id\":\"x\",\"type\":\"text\",\"text\":\"b\"}]}",
                      dup),
          "duplicate ids parse");
    check(dup.nodes.size() == 2 && dup.nodes[0].id != dup.nodes[1].id, "a duplicate id is replaced");
    std::string deep(200, '[');
    check(!parseCanvas(deep, broken), "nesting has a limit");
}

void smdFormat() {
    Sheet s;
    s.nodes.push_back(card("kick", 0, 0));
    s.nodes.back().file = "Roadmap/Kick off.md";
    s.nodes.push_back(card("research", 300, 0));
    s.nodes.back().file = "Roadmap/Research.md";
    Node group;
    group.id = "phase";
    group.type = NodeType::Group;
    group.label = "Phase [1]";
    group.x = 260;
    group.y = -60;
    group.width = 300;
    group.height = 220;
    s.nodes.insert(s.nodes.begin(), group);
    Node link;
    link.id = "web";
    link.type = NodeType::Link;
    link.url = "https://jsoncanvas.org";
    link.x = 0;
    link.y = 300;
    s.nodes.push_back(link);
    s.edges.push_back(arrow("kick", "research", "then"));

    SmdInfo info;
    info.title = "Roadmap";
    info.folder = "Roadmap";
    std::string previous = "---\ntags: [plan]\nfolder: Old\nowner: Dario\n---\n\n# Old title\n";
    std::string smd = writeSmd(s, info, previous, [](const Node& n) {
        return n.id == "kick" ? std::string("Kick-off") : std::string();
    });
    check(smd.rfind("---\ntags: [plan]\nfolder: Roadmap\nowner: Dario\n---\n", 0) == 0,
          "the old front matter is kept with its folder brought up to date");
    check(smd.find("# Roadmap\n") != std::string::npos, "the title heading");
    check(smd.find("- [Kick-off](<Roadmap/Kick off.md>) \xE2\x86\x92 Research (then)") != std::string::npos,
          "a card reads as a link to its note with its arrows");
    check(smd.find("- <https://jsoncanvas.org>") != std::string::npos, "a link card reads as its address");
    check(smd.find("## Phase \\[1\\]") != std::string::npos, "a group is a section, its label escaped");
    check(smd.find("```json canvas\n{") != std::string::npos && smd.size() > 4 &&
              smd.substr(smd.size() - 4) == "```\n",
          "the canvas sits in a closing json canvas block");
    size_t groupAt = smd.find("## Phase");
    size_t researchAt = smd.find("- [Research]");
    size_t kickAt = smd.find("- [Kick-off]");
    check(kickAt < groupAt && groupAt < researchAt, "ungrouped cards first, then each group's");

    Sheet back;
    SmdInfo backInfo;
    std::string error;
    check(parseSmd(smd, back, backInfo, &error), "the .smd parses: " + error);
    check(backInfo.title == "Roadmap" && backInfo.folder == "Roadmap", "title and folder read back");
    check(writeCanvas(back) == writeCanvas(s), "the canvas survives the round trip");

    // A byte order mark, CRLF lines, other code blocks and a later canvas
    std::string crlf =
        "\xEF\xBB\xBF---\r\nfolder: \"My notes\"\r\n---\r\n\r\n# Sheet\r\n\r\n```json\r\n{\"not\":\"me\"}\r\n```\r\n\r\n"
        "````json canvas\r\n{\"nodes\":[{\"id\":\"n\",\"type\":\"text\",\"text\":\"```\",\"x\":1,\"y\":2,\"width\":3,\"height\":4}],\"edges\":[]}\r\n````\r\n";
    check(parseSmd(crlf, back, backInfo, &error), "CRLF with a byte order mark parses: " + error);
    check(backInfo.folder == "My notes" && back.nodes.size() == 1 && back.nodes[0].text == "```",
          "the canvas block is found past other code, inside a longer fence");
    check(parseSmd("# Empty\n", back, backInfo) && back.nodes.empty() && backInfo.title == "Empty",
          "a .smd without a canvas is an empty sheet");
    std::string fresh = writeSmd(Sheet{}, SmdInfo{"Fresh", ""}, "");
    check(fresh.rfind("---\nfolder: .\n---\n\n# Fresh\n\n```json canvas\n", 0) == 0,
          "a new .smd gets front matter: " + fresh);

    // The fixture sheet reads, and its cards point into its folder
    std::string fixture = readFile(std::string(TINTA_SHEET_FIXTURE_DIR) + "/Launch plan.smd");
    check(!fixture.empty(), "the sheet fixture is there");
    check(parseSmd(fixture, back, backInfo, &error), "the sheet fixture parses: " + error);
    check(backInfo.folder == "Launch plan", "the fixture's folder");
    int files = 0, groups = 0;
    for (const Node& n : back.nodes) {
        if (n.type == NodeType::File) {
            ++files;
            check(n.file.rfind("Launch plan/", 0) == 0, "fixture card in its folder: " + n.file);
        }
        if (n.type == NodeType::Group) ++groups;
    }
    check(files >= 6 && groups >= 2 && back.edges.size() >= 4, "the fixture has cards, groups and arrows");
}

void orderAndNavigation() {
    Sheet s;
    // c sits top-left but comes after b along the arrows
    s.nodes = {card("a", 0, 200), card("b", 300, 200), card("c", 0, 0), card("d", 600, 200)};
    s.edges = {arrow("a", "b"), arrow("b", "c"), arrow("c", "d")};
    std::vector<int> order = readingOrder(s);
    std::vector<std::string> ids;
    for (int i : order) ids.push_back(s.nodes[i].id);
    check(ids == std::vector<std::string>({"a", "b", "c", "d"}), "reading order follows the arrows");

    // A cycle is cut at its top-left card
    s.edges.push_back(arrow("d", "a"));
    order = readingOrder(s);
    ids.clear();
    for (int i : order) ids.push_back(s.nodes[i].id);
    check(ids.size() == 4 && ids[0] == "c" && ids[1] == "d" && ids[2] == "a" && ids[3] == "b",
          "a cycle starts at its top-left card");

    // Without arrows: rows top to bottom, left to right
    Sheet grid;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            grid.nodes.push_back(card(std::string(1, (char)('a' + r * 3 + c)), c * 300.0f + (r == 1 ? 20 : 0),
                                      r * 200.0f + (c == 2 ? 30 : 0)));
    order = readingOrder(grid);
    std::string sequence;
    for (int i : order) sequence += grid.nodes[i].id;
    check(sequence == "abcdefghi", "rows read left to right, top to bottom: " + sequence);

    // Arrow keys
    int centre = nodeIndex(grid, "e");
    check(neighbourInDirection(grid, centre, 1, 0) == nodeIndex(grid, "f"), "right of the centre");
    check(neighbourInDirection(grid, centre, -1, 0) == nodeIndex(grid, "d"), "left of the centre");
    check(neighbourInDirection(grid, centre, 0, -1) == nodeIndex(grid, "b"), "above the centre");
    check(neighbourInDirection(grid, centre, 0, 1) == nodeIndex(grid, "h"), "below the centre");
    check(neighbourInDirection(grid, nodeIndex(grid, "c"), 1, 0) == -1, "nothing right of the edge");
    Node lane;
    lane.id = "lane";
    lane.type = NodeType::Group;
    lane.x = -50;
    lane.y = -50;
    lane.width = 2000;
    lane.height = 2000;
    grid.nodes.insert(grid.nodes.begin(), lane);
    check(neighbourInDirection(grid, nodeIndex(grid, "e"), 1, 0) == nodeIndex(grid, "f"),
          "groups are not stops for the arrow keys");
}

void tidyLayout() {
    Sheet s;
    s.nodes = {card("a", 500, 500), card("b", 0, 0), card("c", 900, 30), card("free1", 40, 900),
               card("free2", 41, 905)};
    s.edges = {arrow("a", "b"), arrow("b", "c")};
    tidy(s, {}, true);
    const Node& a = s.nodes[nodeIndex(s, "a")];
    const Node& b = s.nodes[nodeIndex(s, "b")];
    const Node& c = s.nodes[nodeIndex(s, "c")];
    check(a.x + a.width <= b.x && b.x + b.width <= c.x, "left to right along the arrows");
    for (size_t i = 0; i < s.nodes.size(); ++i)
        for (size_t j = i + 1; j < s.nodes.size(); ++j)
            check(!overlaps(s.nodes[i], s.nodes[j]), "tidied cards do not overlap: " + s.nodes[i].id + " " + s.nodes[j].id);

    // Top to bottom, with a cycle
    s.edges.push_back(arrow("c", "a"));
    tidy(s, {}, false);
    check(s.nodes[nodeIndex(s, "a")].y < s.nodes[nodeIndex(s, "b")].y &&
              s.nodes[nodeIndex(s, "b")].y < s.nodes[nodeIndex(s, "c")].y,
          "a cycle still lays out top to bottom");

    // A group stays around its cards and moves as one block
    Sheet g;
    Node group;
    group.id = "g";
    group.type = NodeType::Group;
    group.label = "Group";
    group.x = 1000;
    group.y = 1000;
    group.width = 900;
    group.height = 700;
    g.nodes.push_back(group);
    g.nodes.push_back(card("in1", 1500, 1300));
    g.nodes.push_back(card("in2", 1100, 1100));
    g.nodes.push_back(card("out", 0, 0));
    g.edges = {arrow("in2", "in1"), arrow("out", "in2")};
    tidy(g, {}, true);
    const Node& gg = g.nodes[nodeIndex(g, "g")];
    for (const char* id : {"in1", "in2"}) {
        check(insideGroup(gg, g.nodes[nodeIndex(g, id)]), std::string("still inside its group: ") + id);
    }
    check(!insideGroup(gg, g.nodes[nodeIndex(g, "out")]), "the outside card stays out");
    check(g.nodes[nodeIndex(g, "in2")].x < g.nodes[nodeIndex(g, "in1")].x, "the group's own arrows lay out inside");
    check(g.nodes[nodeIndex(g, "out")].x + 200 <= gg.x, "the group follows the card pointing at it");

    // Only the selection moves
    Sheet part;
    part.nodes = {card("x", 0, 0), card("y", 1000, 1000), card("z", 5000, 5000)};
    part.edges = {arrow("x", "y")};
    tidy(part, {0, 1}, true);
    check(part.nodes[2].x == 5000 && part.nodes[2].y == 5000, "a card outside the selection stays put");
    check(part.nodes[0].x == 0 && part.nodes[0].y == 0, "the tidy starts where the selection was");

    auto places = gridPlaces(5, 10, 20, 100, 50, 10);
    check(places.size() == 5 && places[0] == std::make_pair(10.0f, 20.0f) && places[3].second == 80.0f,
          "a grid of places");
}

void notes() {
    const std::string note =
        "---\ntitle: \"Launch: the plan\"\nstatus: Doing\n---\n# Heading one\n\n"
        "See [[Research notes|research]] and [[Budget#Q3]] and ![[diagram.png]].\n"
        "A [relative link](Sub%20folder/Next%20step.md#part) and [web](https://example.com/a.md) "
        "and [anchor](#here) and <not a link>.\n"
        "`[[in code]]`\n\n```\n[[fenced]]\n```\n"
        "- [ ] open task\n- [x] done task\n  * [X] nested done\n1. [ ] numbered\n- [link](x.md) not a task\n";
    std::vector<std::string> links = noteLinks(note);
    check(links == std::vector<std::string>({"Research notes", "Budget", "diagram.png", "Sub folder/Next step.md", "x.md"}),
          "links: wiki names, embeds and relative .md paths, decoded, without fragments");
    check(noteTitle(note, "fallback") == "Launch: the plan", "the front matter title wins");
    check(noteTitle("intro text\n\n## **Bold** heading `x`\n", "f") == "Bold heading x",
          "the first heading, without its markup");
    check(noteTitle("- [ ] first task\nmore", "f") == "first task", "else the first line's text");
    check(noteTitle("\n\n", "Fallback") == "Fallback", "else the fallback");
    int done = 0, total = 0;
    noteTasks(note, done, total);
    check(done == 2 && total == 4, "task boxes counted: " + std::to_string(done) + "/" + std::to_string(total));
    check(noteWords("---\ntitle: x\n---\nOne two  three\n\n# Four\n") == 4, "words outside front matter");
    check(noteBody(note).rfind("# Heading one", 0) == 0, "the body starts after the front matter");
    check(frontmatterValue(note, "status") == "Doing", "a front matter value");

    std::string plain = "# Card\n\ntext\n";
    check(setFrontmatterValue(plain, "status", "To do") && plain == "---\nstatus: To do\n---\n\n# Card\n\ntext\n",
          "front matter is added: " + plain);
    check(setFrontmatterValue(plain, "status", "Done") && plain == "---\nstatus: Done\n---\n\n# Card\n\ntext\n",
          "a value is replaced");
    check(setFrontmatterValue(plain, "owner", "yes") && plain.find("owner: \"yes\"\n---") != std::string::npos,
          "a new key goes before the closing line, a YAML word quoted: " + plain);
    std::string crlf = "---\r\na: 1\r\n---\r\nbody";
    check(setFrontmatterValue(crlf, "lane", "Next: soon") && crlf == "---\r\na: 1\r\nlane: \"Next: soon\"\r\n---\r\nbody",
          "CRLF line ends are kept: " + crlf);
    std::string unsafe = "---\na: 1\na: 2\n---\n";
    std::string before = unsafe;
    check(!setFrontmatterValue(unsafe, "status", "x") && unsafe == before, "an ambiguous block is left alone");
    std::string bom = "\xEF\xBB\xBF# T\n";
    check(setFrontmatterValue(bom, "k", "v") && bom.rfind("\xEF\xBB\xBF---\nk: v\n---\n\n# T", 0) == 0,
          "front matter goes after a byte order mark");

    check(safeFileStem(L"  Plan: Q3 / Q4?  ") == L"Plan Q3 Q4", "unsafe characters become single spaces");
    check(safeFileStem(L"...") == L"Untitled" && safeFileStem(L"") == L"Untitled", "never empty");
    check(safeFileStem(L"con") == L"con_", "reserved device names are avoided");
    check(safeFileStem(std::wstring(200, L'a'), 64).size() == 64, "names are kept short");

    std::string shifted = shiftHeadings("# One\n## Two\n```\n# code\n```\n###### Six\n#tag\n", 1);
    check(shifted == "## One\n### Two\n```\n# code\n```\n###### Six\n#tag\n",
          "headings shift and clamp, code and tags stay: " + shifted);
}

void mermaidBridge() {
    FlowImport flow;
    check(importFlowchart("flowchart LR\n    A[Start] --> B{Choice}\n    B -->|yes| C[Done]\n    B --- D\n"
                          "    subgraph S [Later]\n        D\n    end\n",
                          flow, 240, 120),
          "a flowchart imports");
    check(flow.cards.size() == 4 && flow.links.size() == 3 && flow.leftToRight, "cards, links, direction");
    if (flow.cards.size() == 4) {
        check(flow.cards[0].label == "Start" && flow.cards[1].label == "Choice", "labels");
        check(flow.cards[0].x < flow.cards[1].x && flow.cards[1].x < flow.cards[2].x, "laid out left to right");
    }
    if (flow.links.size() == 3) {
        check(flow.links[1].label == "yes" && flow.links[1].arrow, "a labelled arrow");
        check(!flow.links[2].arrow, "an open link has no arrow");
    }
    check(flow.groups.size() == 1 && flow.groups[0].label == "Later" && flow.groups[0].cards.size() == 1,
          "a subgraph becomes a group");
    check(importFlowchart("Some notes\n\n```mermaid\ngraph TD\nX --> Y\n```\n", flow, 200, 100) &&
              flow.cards.size() == 2 && !flow.leftToRight && flow.cards[0].y < flow.cards[1].y,
          "a fenced flowchart inside text imports top to bottom");
    check(!importFlowchart("sequenceDiagram\nA->>B: hi\n", flow, 200, 100), "other diagrams do not");
    check(!importFlowchart("just text", flow, 200, 100), "plain text does not");

    Sheet s;
    s.nodes = {card("a", 0, 0), card("b", 300, 0), card("c", 600, 0)};
    Node group;
    group.id = "g";
    group.type = NodeType::Group;
    group.label = "Say \"hi\"";
    group.x = 280;
    group.y = -20;
    group.width = 260;
    group.height = 160;
    s.nodes.insert(s.nodes.begin(), group);
    s.edges = {arrow("a", "b", "goes | on"), arrow("b", "c")};
    s.edges[1].toArrow = false;
    std::string text = exportFlowchart(s, [](const Node& n) { return n.id == "a" ? std::string("Quote \"me\"") : n.id; });
    check(text.rfind("flowchart LR\n", 0) == 0, "a flowchart");
    check(text.find("n1[\"Quote #quot;me#quot;\"]") != std::string::npos, "quotes are escaped");
    check(text.find("subgraph g1 [\"Say 'hi'\"]") != std::string::npos, "groups are subgraphs");
    check(text.find("n1 -->|goes ' on| n2") != std::string::npos && text.find("n2 --- n3") != std::string::npos,
          "arrows and open links: " + text);
    FlowImport round;
    check(importFlowchart(text, round, 200, 100) && round.cards.size() == 3 && round.links.size() == 2 &&
              round.groups.size() == 1,
          "the export imports again");
}

void geometry() {
    Node a = card("a", 0, 0, 100, 100), b = card("b", 300, 10, 100, 100), c = card("c", 10, 400, 100, 100);
    Side f, t;
    edgeSides(a, b, Side::None, Side::None, f, t);
    check(f == Side::Right && t == Side::Left, "side by side joins right to left");
    edgeSides(a, c, Side::None, Side::None, f, t);
    check(f == Side::Bottom && t == Side::Top, "one above the other joins bottom to top");
    edgeSides(a, c, Side::Left, Side::None, f, t);
    check(f == Side::Left && t == Side::Top, "a stored side wins");
    Sheet s;
    s.nodes = {a, b, c};
    Box box = bounds(s);
    check(box.left == 0 && box.top == 0 && box.right == 400 && box.bottom == 500, "bounds of all nodes");
    removeNode(s, "a");
    check(s.nodes.size() == 2 && nodeIndex(s, "a") < 0, "a node is removed");
    std::set<std::string> ids;
    for (int i = 0; i < 1000; ++i) ids.insert(newId());
    check(ids.size() == 1000 && ids.begin()->size() == 16, "ids are unique 16-digit hex");
}
}  // namespace

int main() {
    canvasFormat();
    smdFormat();
    orderAndNavigation();
    tidyLayout();
    notes();
    mermaidBridge();
    geometry();
    if (failures) {
        std::cerr << failures << " sheet model check(s) failed\n";
        return 1;
    }
    std::cout << "sheet model: all checks passed\n";
    return 0;
}
