# A block of thousands of lines in Live

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code and by measuring; nobody reported it. Live
lays the block the caret is in out as Markdown, and a table, a list or a
fenced code block is one block however long. Every key in such a block,
and every move of the caret to another line of it, laid the whole block
out again, at a cost that grew with the square of its lines. Measured
with a document that is one table, the caret in its middle:

| Rows | A letter typed | A frame |
|-----:|---------------:|--------:|
| 1,000 | 194 ms | 14.5 ms |
| 5,000 | 4,393 ms | 54.6 ms |

A list of 20,000 items took 11.7 s a key, a fence of 5,000 lines 5.2 s.
Documents of ordinary blocks were not affected: a paragraph is a block of
a line or two.

## Cause

Three things, all in `live::layoutOf` and `live::draw` (`live_draw.cpp`):

- The block was one DirectWrite layout, made again from nothing whenever
  its text or the caret's line changed.
- A range set on a DirectWrite layout (a face, a size, a colour, an inline
  object for a hidden mark) costs time with the ranges the layout already
  holds. A table row sets a dozen. Four thousand rows dressed in one
  layout took 2,536 ms in a benchmark of DirectWrite alone; the same rows
  in layouts of 32 rows took 88 ms.
- Every frame drew the whole layout, the lines out of view with the rest,
  and measured the selection over all of it.

## Change

- A stretch's layout is a `live::Laid` (`live_text.h`): its lines in parts
  of about 32, each a DirectWrite layout of its own, stacked down the
  page. A part's text keeps the break that ends its last line, so every
  line is laid out with the letters it had in one layout and is exactly
  as tall.
- What a line leaves open for the lines after it (an open fence and its
  language and tokenizer state, an open `$$`, front matter, the indents of
  the list items around) is kept at each part's start and end.
- `layoutOf` keeps the parts a change leaves alone: those before the
  first changed letter and those after the last one, which move with the
  text. The lines between are split into parts anew. Then it goes down
  the parts and lays one out again only when its text is new, when what
  the part before left open changed, or when the line shown as typed came
  to it or left it. A fence opened in the middle of a block restyles the
  parts below it and stops where the lines read as before.
- A part's place is the sum of the heights of the lines above, kept as a
  double. A line does not move by a fraction of a pixel when the lines
  above it are split into parts another way.
- A fenced block's background that runs through several parts is drawn as
  one band, from its first line to its last.
- `draw` takes what the window shows and draws the parts in view (the
  page tells it, from the scroll position; sheet cards draw all, as
  theirs are short).
- The key a layout is cached under holds everything a part keeps from
  the look: every heading size, the list indent and the colours drawn
  into backgrounds and marks. It held three numbers of the colours, which
  a single layout made anew at every key could afford.
- `caretPoint`, `positionAt`, `taskBoxAt` and the callers in
  `live_editor.cpp`, `sheet_input.cpp` and `sheet_render.cpp` take the
  `Laid`.

Nothing changes in what is typed, saved or shown, with one exception that
takes measuring to see: far down a block of a thousand lines a line stands
up to 0.4 px from where one layout put it, because one layout adds its
lines' heights up as floats and drifts by that much. The test prints the
largest difference it met.

## Fixture

`fixtures/live-large-block.md`: front matter, a heading, a paragraph and a
quote with emphasis, a link, code and a mark; a table of 400 rows whose
cells hold bold, emphasis, code, marked and struck words, links, a formula
and Croatian, Japanese and Ukrainian town names, every ninth row long
enough to wrap at a narrow width; a list of 300 items with tasks done and
open and items one and two levels in; 60 numbered steps; a `cpp` fence of
200 lines whose block comments run over two lines; a rule; a `python`
fence in `~~~`; a `$$` formula; and a last paragraph.

## Automated checks

**`live_large_block`** (`live_parts_tests.cpp`, in `input_tests`) works on
a copy of the fixture, as a portable copy with its own settings.

- **Parts of any size show what one layout shows.** The fixture's 1,084
  lines are laid out as one layout, as Live did, and in parts of 1, 2, 3,
  7 and 32 lines, at 900 and 330 px, with no line shown as typed, with a
  table row and with the first line. For each: the place of every third
  position of the text; the position and the task box found from points
  on each sampled line (at the position, left of the line, past its end)
  and above and under the text; the lines under each code background;
  and every span with a background. The text is as tall in parts of every
  size to 0.02 px.
- **What is drawn.** With a selection from the table into the first fence,
  the pixels `draw` leaves when told what is in view are the pixels it
  leaves drawing every part, around seven places from the top to the end.
  The 25 lines around each place, laid out on their own, draw the same
  pixels from parts of three lines as from one layout.
- **A layout carried through edits is what a new one is.** 400 edits to a
  text of some 290 lines taken from the fixture, at places a fixed sequence
  picks: at the text's end first, then letters and marks typed and
  deleted, lines broken, joined, pasted and removed, fences opened and
  their marks deleted, front matter opened and closed, and the caret
  moved alone. After each, a layout carried in parts of 4 lines and one in
  parts of 32 are compared as above with one layout made from nothing,
  and after every fourth the lines around the edit are drawn and compared,
  with a selection of any length. The carried layout laid out 7,809 parts
  where making it anew each time laid out 31,877. (Five other sequences
  were tried by hand and pass; one is in the test.)
- **A key lays out its part**, counted through `keystroke.h` and a frame
  after each key, in a document made for the test: a table of 3,000 rows,
  a list of 3,000 items and a fence of 3,000 lines. Coming to each block
  lays out its lines once. A frame with nothing changed lays out nothing.
  A letter lays out 1 part (31 or 32 lines); Down 1 or 2 parts; Enter 1;
  Backspace 1. The test allows 2, 4, 6 and 6.
- It saves twelve frames under `build/live-parts-test-Release/frames/`:
  the fixture in Live with the caret and a selection in the table, the
  list and the fence, in Paper and Midnight, at 1050 and 560 px. Looked
  at: the table in Paper at 1050 px and in Midnight at 560 px, where its
  long rows wrap; the list in Paper at 560 px, with its boxes, bullets
  and a struck item; and the fence in Midnight at 560 px, where the code
  background runs through the parts as one band and the two-line comments
  keep their colour across them.

A pixel of the "same pixels from parts as from one layout" check counts
when it is one of three that differ in a column. One layout's lines stand
a few ten-thousandths of a pixel from the parts' (0.0002 px at line 20,
measured), which once shaded one row of a struck line differently.

Each part of the change taken out in turn, the test fails:

| Taken out | Checks failing |
|:----------|---------------:|
| Keeping parts through a change (every key lays out all, as before) | 13 |
| Laying a part out again when the part before it left something else open | 4 |
| A kept part must still start a line | 4 |
| Drawing the part that reaches up out of the view | 6 |
| A background joined across parts | 51 |
| Laying a part out again when the typed line comes to it or leaves it | 7 |
| The text's last part is kept only while it ends the text | 6 |
| Leaving a mark's no-length piece out of a longer selection | 1 |
| Heights summed exactly | 6 |

The checks carried through edits stop at the fourth wrong layout, so their
counts are floors.

`live_editor` and `sheet_canvas` use the same functions for the page and
for sheet cards and pass unchanged, but for reading a part's layout where
they read the layout.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
61 of 61.

## In the built app

An isolated portable copy (`out/audit/app-keys/`, ignored) with the test's
document of three blocks of 3,000 lines, opened in Live, a click into a
row of the table, then thirty letters posted to the window as the keyboard
posts them. Messages were then sent to the window until it answered at
once: a sent message is answered between two keys, so the longest wait is
about one key's work.

| Build | Busy with thirty letters | Longest wait for an answer |
|:------|-------------------------:|---------------------------:|
| v4.0.2 | 24.70 s | 1.304 s |
| This one | 0.08 s | 0.003 s |

After Ctrl+S the file on disk had the thirty letters in the row clicked,
with both builds. Six Downs, more letters and Enter, also posted, landed
in the rows below.

The window's pixels could not be looked at in this session: copies of the
screen came back black, as the remote desktop was not being shown, and
`PrintWindow` gave a blank image. What Live draws was looked at in the
test's frames instead, which go through the same `renderLivePage`.

## Speed and size

The Live editor in a profiling build of the same objects, a window of
1400 x 900, software rendering, the caret in the middle of the document's
one block. Before is the commit before this one.

| Document | A letter typed, before | after | A frame, before | after |
|:---------|-----------------------:|------:|----------------:|------:|
| Table, 1,000 rows | 194 ms | 1.3 ms | 14.5 ms | 3.3 ms |
| Table, 5,000 rows | 4,393 ms | 2.6 ms | 54.6 ms | 3.3 ms |
| List, 5,000 items | 744 ms | 1.9 ms | 39.1 ms | 2.9 ms |
| List, 20,000 items | 11,658 ms | 4.8 ms | 199 ms | 3.8 ms |
| Fence, 5,000 lines | 5,170 ms | 2.8 ms | 44.6 ms | 4.2 ms |
| Fence, 20,000 lines | not measured | 6.5 ms | not measured | 7.7 ms |

A letter and the frame after it take 5 to 11 ms in all of them; Enter,
Backspace and Down the same. In a document of ordinary blocks (1 MB of
mixed Markdown) a letter takes 3.1 ms, where it took 4.4.

Coming to a block lays its lines out once: 29 ms for the 1,000 rows,
134 ms for the 5,000, 500 ms for the 20,000 items and 610 ms for the
20,000 lines of code. For the two tables it took 208 ms and 4.4 s. Select
all lays the whole document out, once: 208 ms with the 5,000 rows, 730 ms
with the 20,000 items; the frames after it take 4 to 6 ms.

`tinta.exe` is 4,017,152 bytes, 11,264 more than before.

## Limitations

- Coming to a very long block, and selecting all of a very long document,
  still cost a layout of every line, once: half a second for 20,000
  lines. The lines are measured where they could wait until they are
  scrolled to.
- One line of any length is one layout. A paragraph of 100,000 letters on
  a single line is as slow in Live as it was.
- When the caret leaves a block that was changed, the page is parsed and
  laid out again, as before. With a table of 5,000 rows that takes about
  450 ms, most of it the reading view's layout of the table's cells,
  which this change does not touch.
- A layout is kept through a change at its start or its end. When both
  move at once, as when a selection grows over blocks above and below in
  one step, the parts are made again.
- Not checked in the window: scrolling a long block by wheel and by the
  scrollbar while it is the live stretch. The test draws it at seven
  scroll positions and compares the parts in view with every part drawn.
- No real GPU device loss was forced; a new render target makes every
  part again, as it made the one layout again.
