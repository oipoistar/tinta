# Nested lists in the reader and in Live (#315)

Validated on Windows with the Release build on 2026-11-03.

## What was reported

In Live, with a nested bullet list:

- every level drew the same bullet;
- one Tab moved an item three or four spaces in, and taking it back out
  took a Shift+Tab per level;
- the bullet stood far from its item's text;
- the faint wash behind the text being typed covered the whole list, not
  the line being typed.

## Cause

- The reader set every bullet as the text "•" at the list's indent, a full
  step (24 px) before the item's text; Live drew the same small disc for
  every level.
- Tab put an item under the nearest item above it. After a nested list that
  item is the deepest one: `- Also` under `      - A fourth level` went in
  eight spaces, and Shift+Tab, which goes back to the parent's level, took
  a press per level to bring it back.
- Live washed the whole stretch it draws, the blocks the caret is in: a
  list is one block.

## Change

- Bullets are drawn (`listBullet`, render.cpp), by how deep the list sits
  in other lists: a disc, a circle, a square, then round again. The page,
  print and Live's stretch draw the same shapes in the same place, two
  fifths of an em before the item's text and halfway up its letters. A
  bullet beside a heading still moves to the heading's baseline.
- Live works out each line's depth from the indents of the items above it.
- Tab takes an item under the item before it at its own level, to that
  item's text; with unevenly indented items, under the shallowest since
  the parent. The first item at its level stays. Shift+Tab is unchanged,
  so it now undoes a Tab.
- Live's wash covers the rows of the line showing its Markdown: the
  caret's, or the line a selection started from.

## Fixture

`fixtures/live/Nested lists.md`: a paragraph with bold and italic words; a
bullet list four levels deep with code and a link; a numbered list with
bullets under it; tasks with a bullet under them; a list in a quote; a
table and a last paragraph.

## Automated checks

`live_editor` (`tests/live_tests.cpp`, `nestedLists`):

- The page draws the fixture's eleven bullets as disc, circle, square,
  disc, then discs, circles and squares by depth in the numbered list, under
  the tasks and in the quote. The first bullet's right edge is 0.3 to 0.5 em
  before its text and its middle within the text's line.
- Tab on `- Tab on this item` gives `  - Tab on this item`; Shift+Tab gives
  `- Tab on this item` back. The first item at its level does not move in;
  out a level and in again it is back. `2. Second` goes to `   2. Second`
  and back.
- With the caret on the list's last item, a column of pixels left of the
  bullets is washed for one line's height, not the list's.
- Live's engine at three times the size draws disc, circle, square, disc for
  four levels, each 0.3 to 0.55 em before its text.

Neutering one part at a time, each fails its own checks:

- Tab under the nearest item above: four (both Tab cases and their
  Shift+Tab).
- The whole stretch washed: the wash check, 175 rows of the list's 172.
- One bullet on the page: `ddddddddddd`.
- One bullet in Live: `dddd`.
- The bullet a step from its text: the page's gap check, and Live's shapes
  are no longer found where they belong.

`list_block_order` and `print_and_word_export` found the marker beside a
heading as a text run; they now take a drawn bullet too, and check the same
baselines and page breaks.

## In the built app

An isolated portable copy (`out/issues-314-315/`, ignored), driven with
posted messages and captured with PrintWindow:

- The reader, Paper, 1280 px: disc, circle, square, disc down the reported
  list; under a numbered item a circle, then a square; numbers stay where
  they were. The bullet now stands about 6 px before the text, not 19 px.
- Live, Paper: the caret on the third-level item; its line shows its
  Markdown on a wash of its own, the other lines draw the page's bullets.
- Live: Tab on `- Also, when I indent once` took it one level in, and one
  Shift+Tab brought it back.
- Live at 650 px: the wash covers the three rows of the wrapped line.
- Midnight: the fixture in the reader, in Live and in the print preview.

## Size

`tinta.exe` grows by 1,536 bytes (3,962,368 to 3,963,904).

The full suite without the three tests that use the clipboard passed 55 of
57; the two marker tests, updated, pass.

## Limitations

- A wrapped line in Live's stretch starts at the left edge, not under its
  item's text, as before: DirectWrite lays the stretch out as one text and
  has no hanging indent.
- Word and HTML exports keep their own bullets: Word cycles disc, circle,
  square already, and a browser draws disc, circle, square.
