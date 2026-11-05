# Selecting words above tables (#318)

Validated on Windows with the Release build on 2026-11-05.

## What was reported

- In the reading view, a double- or triple-click could select a word or
  line somewhere other than under the pointer, and a drag was affected
  too. Before a table holding a bold cell, clicking `a table` on the second
  line of the paragraph above it took words from the first line. Before a
  table whose cell wraps, a click anywhere in the paragraph took the
  table's header. Plain cells were fine.
- The reporter traced it to `rollbackTo` in `render.cpp`. It cuts each list
  back to its length at the snapshot, which assumes a layout only appends.
  Since #110 (4ad30d1), a text rectangle can join a line bucket laid out
  earlier, or insert a new bucket among them.

## Cause

- A table measures some cells before drawing them. It lays a styled cell
  out once at full width for the column widths, and a cell that may wrap
  once more for the row heights, then takes the layout back with
  `rollbackTo`. Both trials lay the cell out at y = 0.
- Their rectangles at y = 0 made new line buckets at the front of the list,
  or joined a bucket of the page's first lines. `rollbackTo` then cut the
  list back to its old length:
  - it dropped the real last line, the paragraph's second line, so a click
    there found the first;
  - it kept the cell's bucket at the top, naming rectangle numbers that the
    table's own text took next, so a click in the paragraph could land in
    the table's header.
- The review of the other snapshot users found one more leftover. A
  Mermaid diagram fitted to the page is laid out again at a smaller scale,
  and the scroll anchors of the larger pass stayed. The editor's scroll
  sync kept its nodes where that pass had them.
- Nothing else is affected:
  - List markers and quote bars read their snapshots' bounds without
    taking anything back.
  - `shiftLayoutItems` already rebuilt the extents of the buckets it moved.
  - Inline layout changes nothing that `rollbackTo` does not restore.

## Change

`render.cpp`:

- **Measured cells stay out of the lines.** A cell laid out only to be
  measured gives its text to no line bucket (`MeasuringOnly`,
  `App::layoutMeasuring`).
- **`rollbackTo` restores the buckets exactly.** It takes a trial's
  rectangles back out of every bucket they joined, drops the buckets they
  alone made, and sets the extents of the rest again from the rectangles
  they keep (`rollbackLineBuckets`). Only the buckets within reach of the
  trial's rectangles are visited; the buckets run down the page.
- **Scroll anchors are taken back too** (`LayoutSnapshot::scrollAnchors`).
- **Finding a line is faster.** A rectangle finds its line by binary
  search, instead of walking every line laid out before it.

## Fixture

`fixtures/table-selection-318.md` holds the reporter's three tables:

- plain cells;
- a bold cell;
- a cell that wraps at any width.

Each table follows a paragraph that wraps at a narrow width. A heading, a
list with emphasis and a link, a quote, a table of italic, link, code and
bold-italic cells, a code block and a closing paragraph come after them.

## Automated checks

**`wide_table_layout`** (`checkWordsAroundTables`) runs in Paper and
Midnight, at 1050, 650 and 420 px. On the laid-out fixture:

- every text rectangle sits in exactly one line bucket, the one of its line;
- the buckets run down the page;
- for every word on the page, a click in the middle of its rectangle
  (`selectionOffsetAtPoint`) lands in it, and the triple-click line
  (`selectionLineRange`) holds it.

On master it fails 176 checks.

**`edit_preview_scroll_sync`** (`fittedDiagramAnchors`) lays out a
left-to-right flowchart of six branches of seven nodes:

- unfitted, it is wider than a 650 px page;
- fitted, every node's anchor is the top of a node box as drawn.

Without the anchor rollback, the second check fails.

Neutering one part at a time, with the line search by halves kept:

- No measuring-only cells: `wide_table_layout` passes, through the exact
  rollback alone.
- No exact rollback: it passes, through measuring-only alone.
- Neither, as on master: it fails.

## In the built app

The build ran as an isolated portable copy, in its default theme at
700 x 900. It was driven with posted messages and captured with
PrintWindow (`out/select-318/`):

- A double-click on `second`, on the second line of the paragraph before
  the bold cell's table, selects `second`.
- A double-click on `sentence`, before the wrapping cell's table, selects
  `sentence`.
- A triple-click on `window is narrow.` selects that line.

## Layout time

The test laid out all 152 fixtures as one page at 1050 px and took the best
of five runs. The page is 307 KB, 36,663 text rectangles on 5,106 lines.

| Version | Time |
| --- | --- |
| master | 331 ms |
| exact rollback, master's line search | 344 to 354 ms |
| exact rollback, line search by halves | 287 ms |
| this change | 275 to 285 ms |

Line and rectangle counts are the same as on master.

## Size

`tinta.exe` grows by 1,024 bytes (3,984,896 to 3,985,920), with #317's
change in the same build.

## Limitations

- A trial still lays its cell out at y = 0. The cell's rectangles never
  reach the lines now, but other trials rely on the exact rollback.
