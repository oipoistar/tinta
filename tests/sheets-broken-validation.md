# Sheets: a cut file, twin groups, a pasted self-link, a restless timer

Validated on Windows with the Release build on 2026-11-10.

## What was found

Four things, found in a review of the code; nobody reported them.

1. **A sheet file cut short was saved over.** A `.smd` that ends inside
   its canvas block (a sync conflict, a partial copy, a closing fence
   deleted in an editor) opened as an empty, writable sheet. The first
   change saved the empty sheet over what was left of the cards.
2. **Two groups with the same box hung Tidy and Compile.** Each group is
   inside the other, so the walk up from a card to its outermost group
   went round the two for ever. Grouping the same cards twice makes such
   a pair.
3. **A pasted flowchart could take another arrow's head, or end the
   program.** A link from a card to itself (`A --- A`), or a second link
   between two cards, adds no arrow. Its "no arrowhead" was then set on
   whatever arrow was last: another link's, the sheet's own, or, on a
   sheet with no arrows, memory before the list.
4. **A sheet drew itself every 16 ms while it stood still.** The timer
   asked for the next frame at once for as long as one note card had
   never been drawn. A card out of view never is, and none is when the
   sheet is zoomed out to titles: so on any sheet larger than its window.

## Change

1. `parseSmd` (`sheet_model.cpp`) fails when the file ends inside its
   canvas block, and the sheet opens read-only, as one whose canvas does
   not parse already did.
2. `groupOf` takes one of two groups that hold each other as the outer:
   the larger, or of two as large the one made first. The two walks up,
   in `tidy` and in `compileMarkdown`, also stop after as many steps as
   the sheet has cards.
3. `addEdge` (`sheet_input.cpp`) says whether it added an arrow, and the
   paste sets the head only on one it added.
4. `renderCardBitmaps` notes whether it ran out of time with cards in
   view still to draw (`cardsPending`), and the timer goes fast only
   then.

## Fixture

`fixtures/sheets-broken/`: `Cut short.smd`, `Twin groups.canvas` and
`How to try.md`, which says what to do with each and has the flowchart to
paste, in a list with a code block, a table and a quote. The tests build
the same cases themselves.

## Automated checks

**`sheet_canvas`** (`sheet_tests.cpp`):

- a sheet text that ends inside its canvas block does not parse; the file
  opens read-only; after a key that adds a note and leaving the sheet,
  the file is as it was, byte for byte. An open code block of another
  kind under a closed canvas still reads;
- `A --- A` pasted on the fixture sheet adds no arrow and every arrow
  keeps its head; `P --> Q`, `R --> S`, `P --- Q` adds two arrows, both
  with heads;
- on the sheet of 400 notes, close up: over 100 notes are out of view
  and not drawn, and the timer sets itself to more than 16 ms; zoomed out
  to titles, the same.

**`sheet_model`** (`sheet_model_tests.cpp`): of two groups with one box
the first is the outer and has none over it; a card in them is in a
group; Tidy, with arrows and without, comes back; with a third group a
unit off, a walk up ends within four steps.

Each change taken out in turn: the cut file, 3 checks fail; the groups'
order, 2 (with the walks' bound also out, Tidy does not come back); the
arrow's head, 2; the timer, 2.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
64 of 64. `sheet_canvas` itself puts text on the clipboard and puts back
what was there, as it did before.

## Size

`tinta.exe` is 4,030,464 bytes, as before these changes.

## Limitations

- Not driven in the built window: the screen could not be copied in this
  session. The tests go through the key and mouse handlers.
- A cut file opens read-only and stays so. Tinta does not try to read
  the cards that are whole before the cut.
- The names of a file card, its heading and a link card go into a
  sheet's outline as written; one with a line break and a fence in it
  could still make a file whose canvas is not found on the next load. It
  then opens as described under 1 only when the block is left open;
  otherwise as an empty sheet. Not changed here.
- How much the fan runs was not measured; the timer's period is what the
  test reads.
