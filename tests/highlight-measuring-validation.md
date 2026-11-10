# Search marks and the selection in a long document

Validated on Windows with the Release build on 2026-11-10. The change is
commit 4e48ce8 of 2026-11-09; this record was written after it.

## What was found

Found in a review of the code and by measuring; nobody reported it. In
the reading view a frame took longer the more of the document was marked
by Find or selected, wherever the marks were. In a document of 2 MB a
frame took 2.5 ms plain, 128 ms with every `e` marked and 23 ms with all
of it selected. Scrolling such a document with Find open, or after
Ctrl+A, stuttered.

## Cause

`render` (`main_d2d.cpp`) measured every search match and every selected
run of the whole document for every frame, one DirectWrite hit test
apiece, and filled a rectangle for each mark wherever it lay.

## Change

- `searchHighlightRects(app, top, bottom, out)` (`search.cpp`) gives the
  marks of the matches on the lines between two heights of the document.
  Text rects outside are skipped before anything is measured, and the
  first match reaching into a rect is found by halving.
- `selectionHighlightRects` (`selection.cpp`) takes the same two heights
  for the selection's bars. The form without them still gives the whole
  selection.
- A frame asks both for the window and 50 px above and below it, the
  margin it already drew with.
- `selectionMeasureCount()` counts the ranges measured against a layout,
  so tests count what a highlight costs instead of timing a frame.

## Fixture

`fixtures/highlight-long-document.md`: 150 sections, each a heading, a
paragraph with bold, italic, code, a link and marked text that wraps at a
narrow width, a list with a nested item, a table, a `cpp` fence and a
quote with a superscript and a subscript. Its first lines say what to try.
The test builds the same sections itself. `fixtures/highlight-seams.md`
stays the fixture of the test's other checks.

## Automated checks

**`highlight_layout`** (`highlight_layout_tests.cpp`, in `input_tests`),
in `checkViewRanges`, at 420 and 1050 px, the window 900 px high, at the
document's top, a third down, its middle and its end:

- the long document is over twenty windows long and `e` is found more
  than 5,000 times; every match is marked somewhere in it;
- for the whole document selected, a selection that starts in the window
  and one that ends in it: the window's lines have bars; a frame measures
  no more of the selection than the text rects it draws; no bar comes
  from outside its lines; the bars are the ones the whole selection gives
  for those lines; and, selected whole, they cost under a tenth of all
  the bars;
- for the marks: the window's lines have them; a frame measures a match
  once for each mark it draws, under a tenth of the whole document's; no
  mark comes from outside; the marks are the ones the whole document
  gives for those lines, and each names the match it names there.

With both functions made to give the whole document again, as a frame
asked before, the test fails 81,783 checks: one for every bar and mark
out of view, besides those on the cost.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passed
60 of 60 with this change, and passes 61 of 61 now.

## In the built app

An isolated portable copy (`out/audit/app-find/`, ignored) with the 2 MB
document, 1008 x 760. Twenty wheel notches were posted to the window, and
messages sent to it until it answered at once; the longest wait for an
answer is about one frame's work.

| Build | Nothing marked | Ctrl+F and `e` |
|:------|---------------:|---------------:|
| v4.0.2 | 0.011 s | 0.112 s |
| With the change | under 0.001 s | 0.001 s |

The window's pixels could not be looked at in this session: copies of the
screen came back black, as the remote desktop was not being shown. That
Find was open with `e` in it is seen in v4.0.2's wait, not in a picture.

## Speed and size

A frame in a profiling build of the same objects, a window of 1400 x 900,
software rendering:

| Document | Marked or selected | Before | After |
|:---------|:-------------------|-------:|------:|
| Prose, 2 MB | every `e` | 128 ms | 5.7 ms |
| Prose, 2 MB | all selected | 23 ms | 3.8 ms |
| A fence of 50,000 lines | every `e` | 70 ms | 7 ms |
| A list of 20,000 items | every `e` | 64 ms | 2.6 ms |

A plain frame of the 2 MB document takes 2.5 ms. `tinta.exe` grew by
1,024 bytes, to 4,004,864.

## Limitations

- A frame still measures each mark and bar of the lines it draws. One
  line that holds thousands of matches costs thousands of measurements.
- The search itself reads the whole document for every letter typed into
  Find; only what a frame does with the matches changed.
- Source and Live draw their own marks and selections and are not
  touched by this.
- Not checked in the window: the marks' and the bars' pixels while
  scrolling, for the reason above. The test compares them, rectangle for
  rectangle, with those of the whole document at four scroll positions.
