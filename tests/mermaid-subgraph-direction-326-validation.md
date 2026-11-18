# Subgraph directions in flowcharts (#326)

Validated on Windows with the Release build on 2026-11-18.

## What was reported

A `flowchart LR` with two subgraphs, each with `direction TB` and three
steps linked only to each other. Tinta laid the steps of each group out
left to right: the parser read the `direction` statement and dropped it,
and the layout ranked every node of the diagram along the diagram's
direction.

A second fault showed in the same picture: the box of the group below ran
8 px into the box of the group above.

## What changed

- **The parser keeps a subgraph's direction.** `direction TB`, `TD`, `BT`,
  `LR` and `RL` inside a `subgraph` are kept with that subgraph; the last
  one counts. Any other value is the error `Unsupported flowchart
  direction` with its line, the same error an unsupported direction after
  `flowchart` gives, and the fence shows its source. A `direction` outside
  any subgraph, and one with no value, are passed over as before.
- **A group with no edge across its border is laid out on its own.** Such
  a group with a `direction` is a *block*: its nodes are ranked and placed
  along its direction, and the block then takes part in the layout around
  it as one item of the size of its box. Blocks nest: an inner block is an
  item of the outer one. The diagram keeps its direction for everything
  that is not inside a block, and for where the blocks go.
- **A group linked to the outside takes its parent's direction.** This is
  Mermaid's documented limitation
  ([Direction in subgraphs](https://mermaid.js.org/syntax/flowchart.html#direction-in-subgraphs)):
  when a node of a subgraph is linked to a node outside it, the subgraph's
  direction is ignored and the subgraph inherits the direction of the
  parent graph. Tinta does the same, whether the edge enters the group or
  leaves it, and lays such a group out exactly as if it had no
  `direction`. A group inside it that has no outside link of its own is
  still a block.
- **A group with no `direction` is laid out as before**, with its parent.
- **Neighbouring boxes stay apart.** See below.
- A block is as wide as its title at least, so a title longer than the
  nodes under it does not wrap onto them. An edge of the parent that
  starts beside a block bends in the gap between the ranks, not half way
  to its target, where it would run through the block. A block's box
  holds the lanes of its edges that turn back and the labels of its edges.
  The viewer kept 30 px free above every diagram with groups, for the
  title of a group at its top; a block's box lies inside the layout, so a
  diagram with a block keeps what its boxes do reach above it, often none.

### Where the blocks go, and what Mermaid does

The reported sample was drawn with Mermaid 11.4.1 for comparison. Mermaid
places the two groups **one above the other**, each top to bottom: the
diagram is left to right, the two groups have no edge between them, so
they share the first rank, and the nodes of a rank of a left-to-right
diagram stack. Tinta does the same and for the same reason, as it always
has for two unlinked nodes. (Mermaid draws `Expected` above `Current`;
Tinta keeps the order of the source.) In a top-to-bottom diagram unlinked
groups sit side by side, in both.

Mermaid also gives a subgraph that has *no* `direction` and no outside
link the direction across its parent's (top to bottom inside `LR`), unless
`flowchart.inheritDir` is set. Tinta does not follow that: a group with no
`direction` keeps its parent's, as before this change, so flowcharts
written for Tinta do not change.

### Why the boxes overlapped

A group's box is drawn around its nodes: 10 px beside and below them, and
30 px above (10 and a 20 px title strip). The nodes of one rank were set
32 px apart whatever groups they were in. In a left-to-right diagram the
nodes of a rank stack, so between a node of the group above and a node of
the group below there were 32 px for a box's bottom pad, the next box's
title and its top pad: 40 px. The boxes overlapped by 8. In a
top-to-bottom diagram the nodes of a rank sit side by side and need 20 of
the 32, which is why the overlap was only seen sideways. Nested groups
added a pad, or a pad and a title, per level, also from rank to rank: two
groups, each inside another, one ending where the next starts, need 80 px
of a 78 px rank gap.

The layout now knows the pads. Where a box border runs between two
neighbours of a rank, or between two ranks, the gap is at least the pads
on both sides plus 12 px. A gap that was already wide enough is left
alone: side by side in a top-to-bottom diagram, 10 + 10 + 12 is the 32
there was.

## Fixture

`fixtures/mermaid-subgraph-direction-326.md`: twelve fences among
headings, a table, an ordered list with a nested list, a block quote with
a fence inside it, emphasis, inline code, a link and a plain code block.

| Fence | Shows |
| --- | --- |
| 1 | the reported sample |
| 2 | the same groups with no `direction` |
| 3 | `TB`, `BT`, `LR` and `RL` groups in one top-to-bottom diagram |
| 4 | `TD`, beside linked nodes of the diagram, with a title wider than the nodes |
| 5 | a block with two blocks inside it, beside linked nodes |
| 6 | a group with no `direction` inside a block (in a block quote) |
| 7 | a group linked from outside beside one that is not |
| 8 | a block inside a group that is linked to the outside |
| 9 | edge labels, an edge back, an edge to its own node, shapes and `classDef` in blocks |
| 10 | nested groups with no `direction` on neighbouring ranks |
| 11 | three stacked groups with no `direction`, one nested |
| 12 | `direction XY`: shows its source |

## Checked in the built app

Isolated portable copies (`tinta.exe` beside its own `settings.ini`), the
build before the change (7e8dc0f) and the build after it.

- **Print pages** (`--printpages`), every page of the fixture, before
  (seven pages) and after (nine). Before: the steps of every group follow the diagram, and the
  boxes of fences 1, 2, 10 and 11 overlap. After: every group runs along
  its direction as the table above says, and no two boxes touch.
- **The window**, captured with `PrintWindow` (pixels checked not to be
  blank), each fence in a document of its own with text before and after
  it: fences 1, 3, 5 and 9 at 1100 px and at 520 px, the others at one of
  the two, in Midnight (dark) and in Paper (light). The diagrams are the
  ones of the print pages. At 520 px a diagram wider than the window keeps
  its size and the page scrolls sideways, as any wide diagram does; the
  text around it stays in the window. These captures are of the build
  one edit before the last (the room above a diagram with a block). With
  the last build the captures came back blank, the desktop session not
  being shown by then; that edit was checked on the print pages, which
  the same code lays out.
- **HTML export** (`--exporthtml`), opened in a browser at full width and
  at 480 px: the eleven diagrams match the window's, and at 480 px each
  wide diagram scrolls in its own box while the page does not.
- **Word and PDF export** (`--exportdocx`, `--exportpdf`): both files are
  written, the Word file with its eleven diagram pictures and the PDF with
  its pages. One picture of the Word file was opened (fence 5, the nested
  blocks) and is the window's diagram; the PDF's pages were not opened.

`tinta.exe` grows by 24,576 bytes, from 5,266,944 to 5,291,520. The edge
routing and the group boxes, which the viewer and the HTML export each
had a copy of, are now one in `mermaid.cpp`. Exporting the two documents
of existing flowcharts (37 diagrams) takes the same time as before within
the noise of starting the process (1.2 to 1.5 s for both builds, five
runs each, twice).

## Existing flowcharts

Printed and exported before and after, and compared byte for byte:

| Documents | Flowcharts | Pages | Result |
| --- | --- | --- | --- |
| the ten fixtures with Mermaid or PlantUML fences | all in them | 48 | pages and HTML identical |
| every flowchart in the repository's Markdown, in one document | 21 | 10 | pages and HTML identical |
| written for this check: four shapes in `TB`, `BT`, `LR` and `RL` (labels, edges back and to self, skipped ranks, groups side by side, a group inside a group) | 16 | 8 | pages and HTML identical |
| written for this check: stacked and nested groups in the four directions | 12 | 4 | differ: the boxes that overlapped are apart |

Two runs of the build before the change gave identical pages, so a
difference is the change's.

## Automated checks

`mermaid_parser` (`tests/mermaid_tests.cpp`):

- `testSubgraphDirectionParsing`: the reported source keeps `TB` on both
  subgraphs and `LR` on the diagram; each value, `TD` and lower case; a
  direction per nested subgraph; the last one counts; `direction XY` fails
  on its line with the header's message; a `direction` outside a subgraph
  and one with no value change nothing.
- `testSubgraphDirectionLayout`: the reported flowchart (each group one x
  and growing y, the groups in one rank, an edge straight down); the same
  with no direction; the four directions; nested blocks; a group entered
  from outside, one left, and that an ignored direction moves nothing; a
  block inside a linked group; a plain group inside a block; edges back,
  to self and with labels inside their block; an edge bending past a wide
  block.
- `testNeighbouringGroupBoxes`: stacked and nested groups with no
  direction in the four directions, and the gaps in numbers with and
  without box sizes.
- `testSubgraphDirectionFixture`: every fence of the fixture, which of its
  groups are blocks, and its boxes.

For every one of these flowcharts `checkGroupBoxes` holds: a box holds its
nodes and the boxes inside it; two boxes of which neither is inside the
other are at least 4 px apart; no box runs over a node that is not in it;
a block holds its edges and their labels; an edge of the diagram to the
next rank does not run through a block.

Thirteen parts of the change were taken out one at a time (the direction
kept, the error, blocks at all, the outside edge in each direction, a
block's own direction, the room along a rank and from rank to rank, the
title width, the box around lanes and labels, the bend, moving a block's
nodes, the right-to-left flip). Each makes the test fail, with 1 to 182
failing checks.

`sheet_model`: a pasted flowchart's cards follow a subgraph's direction.

The full `ctest` run without `notification_setting`, `copy_file_path` and
`code_block_layout` (they overwrite the clipboard, and were not run): 83
of 83 pass.

## Limits

- An edge written to a subgraph's id (`A --> TOP`, as in Mermaid's own
  example) still makes a node of that name; Tinta's flowcharts have no
  edges to groups. Not part of this change.
- A group with no `direction` is not a block, so its box is the box
  around its nodes wherever the ranks put them. The ranks of a layout are
  centred one by one, and two such groups whose nodes spread unevenly over
  the ranks can still meet; the new room is between neighbours of a rank
  and between neighbouring ranks.
- In a diagram with no block, a box two or more groups deep at the top
  reaches into the text above it by 30 px a level (fence 10 on its print
  page). It did before; the room kept above such a diagram is that of one
  level, and was left so that existing diagrams stay where they are.
- An edge back or over a rank of the *parent* takes a lane beside its
  nodes as before and can cross a block there, as it could cross a node.
- A sheet draws its own boxes around pasted groups, larger than the
  viewer's, and gets no extra room for them: pasted groups sit as close
  as before.
