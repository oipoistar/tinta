# Selecting over bold and italic words in the reader (#313)

Validated on Windows with the Release build on 2026-11-02.

## What was reported

After the first fix for #313, the reporter showed the reader: in a
selected paragraph, every bold or italic word had a dark seam on either
side of it (`sit | amet`, `nisi | ut aliquip ex ea commodo | consequat`).

## Cause

- The reader lays a paragraph out run by run, one run for each span of one
  style, and a run ends at its last word: the space after `sit` and the one
  before `consequat` belong to no run. The selection was filled run by run
  from DirectWrite's hit tests, so those spaces stayed empty: a seam beside
  every bold, italic, link or code span, and at every soft line break, whose
  space is a run of its own that draws nothing.
- Runs that meet without a space (`**amet**,`, `($x$)`, `x^2^`) were filled
  as two rectangles meeting at a fractional x. The selection colour is
  translucent, so their antialiased edges left a faint seam, and the lines
  of a paragraph met the same way, one row darker between them.

## Change

- `selectionHighlightRects` walks the text rects in layout order and adds
  each run's selected span, the selected part of the spaces between two
  runs of a line (a code chip's padding when the selection runs on past the
  chip), and math boxes and code lines as before. Pieces that meet on a line
  join into one rectangle.
- Table cells are apart by a tab in the document text, so a selection over
  a row still paints only the text of each cell.
- On screen a band's top and bottom fall on whole pixels
  (`selectionBarOnScreen`), so the lines of a paragraph meet without a seam
  at any scroll.
- One pass over the text rects. The old code also kept a vector of runs on
  every paint; that is gone.

## Fixture

`fixtures/selection-over-emphasis-313.md`: the reporter's paragraph; a
paragraph with every kind of run (bold, italic, both, underscores, links, a
bold link, a wiki link, code before a word and before a comma,
strikethrough, a highlight, superscript and subscript, inline math) and a
soft line break at the end of each source line; Turkish bold and italic
words; a heading, a quote, nested bullets, a task and a long item with
emphasis; a table whose cells keep apart; a code block.

## Automated checks

`highlight_layout` (`tests/highlight_layout_tests.cpp`), in Paper and
Midnight, at scales of 100, 150 and 200 %, zooms of 80, 100 and 150 % and
widths of 420, 650 and 1050 px:

- A selection from the first to the last word of the 23 highlight cases
  and of 15 new sources (bold, italic, both, underscores, links, a wiki
  link, soft line breaks, code, math, strikethrough and a highlight,
  scripts, a heading, a quote, a list item, a table cell, a long paragraph
  that wraps) paints each line as one band, from its first selected
  character to its last, with no two pieces overlapping and one even colour
  along its middle row (software raster). On screen, at scrolls of 0, 0.37,
  0.5, 0.83 and 120.25, the bands sit on whole pixels and lines that meet
  share their edge.
- The same over seven passages of the fixture, and no overlap with the
  whole fixture selected.
- In `alpha sit **amet** omega`, the space before the bold word selected
  with it, a word with the space after it and the space after the bold word
  alone each fill just that much; the bold word alone keeps to itself. In
  `alpha sit  **amet** omega` a selection from the second space takes half
  the gap. A code chip selected alone keeps to its text, and with the comma
  after it takes its padding too.
- A selection over `| alpha one | two omega |` keeps the gap between the
  cells.

The first of these checks failed 6,654 times before the change: 3,246
lines in pieces, as many uneven, and 54 for each of the three spaces.
Neutering one part at a time:

- No pieces for the gaps: 3,078 lines in pieces and as many uneven, and the
  space checks.
- Pieces not joined: 3,246 lines in pieces, 3,236 uneven.
- Gaps of any text filled: the table check (54).
- A partly selected gap filled whole: the half gap (54).
- A chip's padding filled without the text after it: the chip (54).
- Bands off whole pixels: 19,105.
- The gaps between the words of one run filled too: passes. That skip only
  saves work; such pieces join into the run's own.

## In the built app

An isolated portable copy (`out/select-313b/`, ignored), driven with posted
messages and captured with PrintWindow (`out/select-313b/shots/`):

- The build before the change (eceb21e, `out/select-313b-before/`), Midnight,
  1280 x 900, Ctrl+A in the reader: the reporter's seams, dark and as wide
  as a space, beside `amet`, `ut aliquip ex ea commodo`, `fugiat nulla` and
  `sunt in culpa qui officia`.
- The new build, the same: each line of the reporter's
  paragraph is one band over `amet`, `ut aliquip ex ea commodo`,
  `fugiat nulla` and `sunt in culpa qui officia`. Down a column of pixels
  the band is one colour (26,54,107) from line to line; the build before
  the whole-pixel change had a darker row (26,52,103) where two lines met.
- Midnight at 650 px: the wrapped lines one band each; every pixel row
  through the paragraph is the band's colour or, between blocks, the page's.
- A drag from inside `dolor` to inside `aliquip` on the next line: one band
  over `sit amet,` and `nisi ut a`.
- Paper at 1280 px, again after a notch of the wheel, and at 650 px: every
  pixel row through the paragraph is the band's colour (177,193,231).
- The table: each cell's text a band of its own and the gaps between cells
  clear; within a cell `a link`, `code cell` and `x² and y` one band each.
  The code block and the last paragraph one band each.

## Size

`tinta.exe` grows by 1,536 bytes (3,960,832 to 3,962,368). A first version
that sorted the pieces of each line grew it by 5,632.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
57 of 57.

## Limitations

- The space where a line wraps stays unfilled, as before, and so do the
  gaps beside a ruby, whose base sits lower than the line around it.
- Search matches are still drawn word by word.
