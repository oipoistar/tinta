// Sheets: Tinta's infinite canvas of notes. A sheet is a .smd file beside
// the folder its notes live in. Its model is JSON Canvas 1.0
// (jsoncanvas.org), the format of Obsidian's canvases, so a .canvas file
// opens as a sheet and a sheet converts back without loss. A .smd wraps the
// canvas in Markdown: front matter, a title and an outline of the cards
// that reads anywhere else, with the canvas itself in a ```json canvas
// block at the end.
//
// Everything here is plain data and text: no window, no Direct2D, so the
// format and the layout algorithms are tested on their own.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sheet {

enum class NodeType { Text, File, Link, Group };
enum class Side { None, Top, Right, Bottom, Left };

// Raw JSON of fields a version does not know, kept for the round trip
using Extra = std::vector<std::pair<std::string, std::string>>;

struct Node {
    std::string id;
    NodeType type = NodeType::File;
    float x = 0, y = 0, width = 360, height = 240;
    std::string color;            // "" none, "1".."6" a preset, or "#rrggbb"
    std::string text;             // Text: Markdown
    std::string file;             // File: from the sheet's base folder, '/' separators
    std::string subpath;          // File: "#Heading" or "#^block"
    std::string url;              // Link
    std::string label;            // Group
    std::string background;       // Group: an image behind it
    std::string backgroundStyle;  // Group: cover, ratio or repeat
    // Tinta: a group that is a lane writes `laneKey: label` into the front
    // matter of the notes put in it, and holds at most `laneLimit` cards
    // without turning red (0: no limit)
    std::string laneKey;
    int laneLimit = 0;
    // Tinta: what follows this card along its arrows is folded away
    bool folded = false;
    Extra extra;
};

struct Edge {
    std::string id;
    std::string fromNode, toNode;
    Side fromSide = Side::None, toSide = Side::None;
    bool fromArrow = false, toArrow = true;
    std::string color;
    std::string label;
    Extra extra;
};

struct Sheet {
    std::vector<Node> nodes;  // back to front, as JSON Canvas orders them
    std::vector<Edge> edges;
    // Tinta: an arrow drawn from one note to another is written into the
    // first one's front matter as `related: "[[Other]]"`, and leaves with it
    bool writeLinks = false;
    Extra extra;
};

// --- JSON Canvas ---------------------------------------------------------

bool parseCanvas(std::string_view json, Sheet& out, std::string* error = nullptr);
// Obsidian's own layout: tab-indented, one node or edge per line
std::string writeCanvas(const Sheet& sheet);

// --- .smd ----------------------------------------------------------------

struct SmdInfo {
    std::string title;   // the first # heading, else empty
    std::string folder;  // front matter `folder:`, relative to the .smd
};
bool parseSmd(std::string_view source, Sheet& out, SmdInfo& info, std::string* error = nullptr);
// A complete .smd. The front matter of `previous` is kept, with `folder:`
// brought up to date; the outline and the canvas block are written anew.
// `titleOf` names a card in the outline (its note's title).
std::string writeSmd(const Sheet& sheet, const SmdInfo& info, std::string_view previous,
                     const std::function<std::string(const Node&)>& titleOf = nullptr);

// --- Helpers -------------------------------------------------------------

std::string newId();
const char* sideName(Side side);
Side sideFromName(std::string_view name);
int nodeIndex(const Sheet& sheet, std::string_view id);
int edgeIndex(const Sheet& sheet, std::string_view id);
// Whether the node's box lies inside the group's (its centre for a node
// bigger than the group, so a card dropped onto a lane counts)
bool insideGroup(const Node& group, const Node& node);
// The innermost group holding the node, or -1
int groupOf(const Sheet& sheet, int node);
// Removes a node with every edge touching it
void removeNode(Sheet& sheet, std::string_view id);

struct Box { float left = 0, top = 0, right = 0, bottom = 0; };
Box boxOf(const Node& node);
Box bounds(const Sheet& sheet, const std::vector<int>& nodes = {});  // all nodes when empty

// The sides an edge leaves and enters by: the stored ones, or the facing
// sides of the two boxes when none is stored
void edgeSides(const Node& from, const Node& to, Side storedFrom, Side storedTo,
               Side& fromSide, Side& toSide);

// --- Algorithms ----------------------------------------------------------

// Cards in reading order: along the arrows (each card after the cards
// pointing at it), and otherwise top to bottom, left to right. A cycle is
// cut at its top-left card. Groups are left out; `subset` limits the cards.
std::vector<int> readingOrder(const Sheet& sheet, const std::vector<int>& subset = {});

// The card the arrow keys move to: the nearest one whose centre lies in
// the direction (dx, dy) from this one's, favouring cards straight ahead.
// -1 when there is none. Cards flagged in `skip` are passed over.
int neighbourInDirection(const Sheet& sheet, int from, int dx, int dy, const std::vector<char>* skip = nullptr);

// Tidies cards into layers along their arrows, left to right or top to
// bottom; cards with no arrows go into a grid beside them. Each group is
// tidied inside and moves as one block. Only `subset` moves when given.
void tidy(Sheet& sheet, const std::vector<int>& subset, bool leftToRight);

// Places `count` new cards of the given size in a grid starting at (x, y)
std::vector<std::pair<float, float>> gridPlaces(int count, float x, float y, float width,
                                                float height, float gap = 40.0f);

// Cards folded away: those along the arrows after a folded card that
// nothing shown and unfolded points at. One flag per node.
std::vector<char> foldedAway(const Sheet& sheet);
// The cards after a folded one that are folded away, and how many
std::vector<int> foldedBehind(const Sheet& sheet, int folded, const std::vector<char>& hidden);
int foldedCount(const Sheet& sheet, int folded, const std::vector<char>& hidden);

// The cards a lane (any group) holds directly, top to bottom
std::vector<int> laneCards(const Sheet& sheet, int lane);
// Stacks a lane's cards into a column under its label, 20 apart, no wider
// than the lane; the lane grows to hold them
void stackLane(Sheet& sheet, int lane);

// Cards in rows, top to bottom, each row left to right: the order a chain
// of arrows joins them in
std::vector<int> rowOrder(const Sheet& sheet, std::vector<int> cards);

// A Markdown document cut into notes at its sections: ## sections when it
// has any (else its top headings), under groups for its # headings when
// there are two or more, a lone # heading being the title. Text before the
// first section is a part of its own; each part's headings are shifted so
// its own heading is #. Code fences are not cut.
struct ExplodedPart {
    std::string title;
    std::string text;
    int group = -1;  // into `groups`
};
struct Exploded {
    std::string title;
    std::vector<std::string> groups;
    std::vector<ExplodedPart> parts;
};
Exploded explodeDocument(std::string_view markdown, std::string_view fallbackTitle);
// Where the parts go from (x, y): each group a column of its cards, else
// rows of four, with room for the arrows that chain them
void layoutExploded(const Exploded& doc, float x, float y, std::vector<Box>& cards, std::vector<Box>& groups);

// Links a note makes to other notes: [[wiki]] names and relative Markdown
// links to .md files, decoded, without #fragments, in order and once each
std::vector<std::string> noteLinks(std::string_view markdown);

// The title a card shows for its note: front matter `title:`, else the
// first heading, else the first line of text, else the fallback
std::string noteTitle(std::string_view markdown, std::string_view fallback);
// What a new note is named after: front matter `title:`, else its first
// line of text, a heading or not; empty when it has none
std::string noteFirstLine(std::string_view markdown);

// Task boxes in a note: done and total
void noteTasks(std::string_view markdown, int& done, int& total);
int noteWords(std::string_view markdown);

// The note's text without its front matter
std::string_view noteBody(std::string_view markdown);

// Sets `key: value` in the note's front matter, adding the property or the
// whole block when missing; leaves anything it cannot edit safely alone
// and returns false then
bool setFrontmatterValue(std::string& markdown, std::string_view key, std::string_view value);
// The scalar value of a front matter property, or empty
std::string frontmatterValue(std::string_view markdown, std::string_view key);
// Adds `link` to the front matter list `key` (the list or the block is made
// when missing), once; removes it again. False when nothing could be done
// safely (another kind of value, a block it cannot edit) or, for removal,
// when the link was not there.
bool addFrontmatterLink(std::string& markdown, std::string_view key, std::string_view link);
bool removeFrontmatterLink(std::string& markdown, std::string_view key, std::string_view link);

// A file name for a card from its title: characters Windows refuses
// become spaces, runs collapse, at most `limit` characters, never empty
std::wstring safeFileStem(const std::wstring& title, size_t limit = 64);

// The heading levels of a note shifted by `by` (clamped to 1..6); fenced
// code is left alone
std::string shiftHeadings(std::string_view markdown, int by);

// --- Mermaid bridge ------------------------------------------------------

struct FlowImport {
    struct Card { std::string id, label; float width = 0, height = 0; float x = 0, y = 0; };
    struct Link { int from = 0, to = 0; std::string label; bool arrow = true; };
    struct Group { std::string label; std::vector<int> cards; };
    std::vector<Card> cards;
    std::vector<Link> links;
    std::vector<Group> groups;
    bool leftToRight = false;
};
// A Mermaid flowchart (bare or in a ```mermaid fence) as cards, laid out
// by Tinta's flowchart layout. False when the text is no flowchart.
bool importFlowchart(std::string_view text, FlowImport& out, float cardWidth, float cardHeight);
// The sheet's cards and arrows as a Mermaid flowchart
std::string exportFlowchart(const Sheet& sheet, const std::function<std::string(const Node&)>& titleOf,
                            const std::vector<int>& subset = {});

}  // namespace sheet
