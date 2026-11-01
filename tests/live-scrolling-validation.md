# Live's page scrolls as the reader's (#307)

Validated on Windows with the Release build on 2026-11-01, on master after
4.0.0.

## What was wrong

- **The scrollbar would not drag.** Live takes the pointer on its page for
  the caret, the selection and the format bar, and it took the scrollbar's
  band at the right edge with it: a press there put the caret near the edge
  and dragged a selection, the thumb never lit up and the pointer was the
  text's beam.
- **After a visit to Source the wheel and the keys stopped.** The source's
  scroll sync, which keeps the split preview level with the source, ran
  every frame in Live too, since Live counts as a visible preview. Until
  Source has been drawn once its height is unknown and the sync left the
  page alone; after Ctrl+/ there and back it held the page at the source's
  scroll position. The wheel moved the page and the next frame put it back;
  Down revealed the caret and the next frame took the page back to where
  the source was, the top in the report.

## The fix

- Live's press and hover handlers give way to the document's scrollbars
  (`documentScrollbarPress`, `documentScrollbarHover` in `input.cpp`): the
  thumb lights up under the pointer with the arrow, a press takes it, a
  press on the track moves the thumb there, and the caret stays where it
  was. The reader, the side panels' press path and Live share one band
  (`documentScrollbarsAt`) and one drag start (`beginDocumentScrollbarDrag`).
  A finger on the band already went to the mouse (`touchTargetAt`), so this
  covers touch too.
- The sync and its reverse, `editorFollowPreview`, run only while the source
  shows with its preview beside it (`editSplitPreview`). Live's page keeps
  its own place; a switch between them already carries the caret across at
  its height in the window.
- The wheel in Live goes as far as the scrollbar does, `livePageHeight`,
  which grows with an open diagram's Markdown above its drawing; it stopped
  short at the reader's height and jumped back from the scrollbar's end.

## Fixture

`tests/fixtures/live/Long read.md`: a long read of headings, paragraphs with
emphasis, links and a note link, bullets, numbers, a nested list, tasks,
quotes, a callout, a table, a code block and a Mermaid diagram at the end.

## Automated checks

`live_editor` (`scrolling` in `tests/live_tests.cpp`) paints each frame as
the window does (`windowFrame`: the scrollbar's extent, the source's sync,
the scroll held to the page, then Live or Source), on the long read at
1050 x 800:

- An arrow over Live's scrollbar and the thumb lit; a press takes the thumb,
  the thumb follows the pointer, the page scrolls and stays, the caret does
  not move and nothing is selected; a press on the track moves the page;
  beside the bar the page's beam is back and the thumb dims.
- Ctrl+/ to Source, a frame (the source measures its height), Ctrl+/ back:
  three notches of the wheel scroll the page and two frames later it is
  still there; Down from the top reaches the last line with the page at the
  end and the caret on screen two frames later; Up keeps it there.
- With the diagram open the page is taller: the thumb takes it to the new
  end, and the wheel there stays at the end.

The tests around the code it touches pass as before: the split preview's
scroll sync and scrollbars, the side panels' scrollbars and edges, touch,
the docked preview, the reading position, wide tables and a dragged
selection.

Neutering, each part on its own: without Live's hand-over to the scrollbar
9 checks fail; with the sync running in Live 6 fail (the wheel leaves the
page at 0; after Down to the end the caret is at y 3085 with the page at
0, the report's jump back); with the wheel stopping at the reader's height 1
fails.

## In the built app

An isolated portable copy (`out/live-307/`, ignored, its own settings.ini
with Live remembered), driven with posted messages and captured with
PrintWindow (`out/live-307/shots/`), in Midnight:

- 1280 x 900: the thumb dragged 300 px scrolled the page to "More to read"
  with the caret still in the title and nothing selected.
- The pill to Source and back to Live (the caret kept its height), then five
  notches of the wheel: the page scrolled and was still there two seconds
  and a mouse move later.
- 110 presses of Down: the caret reached the last line, the page under it,
  and two seconds and a mouse move later the page was still at the end.
- 720 x 760: the thumb dragged back up, and a press low on the track went to
  the end, the caret staying on its line.

## Size

`tinta.exe` grows by 512 bytes (3,934,208 to 3,934,720).

## Limitations

- While the find bar is open the pointer goes to it before the page, as
  before, so the thumb does not light up under the pointer then; a press
  still drags it.
- The checks run at one size; the built app was checked at two.
