# A scrollbar along the source's foot (#314)

Validated on Windows with the Release build on 2026-11-03.

## What was reported

With wrapping off, a long line in the source runs past the pane, and the
only way along it was to follow the caret: "moving forward only by following
the cursor is also too inefficient". A horizontal scrollbar was asked for.

## Cause

The source pans sideways with the caret and Shift+wheel (#77), with no bar
to show or drag where it is, and the horizontal wheel of a tilting wheel or
a touchpad scrolled the preview even over the source. Shift+wheel had no end
on the right.

## Change

- While unwrapped lines run past the pane, a thumb runs along the editor's
  foot, under the text column and clear of the side bar's corner. It drags,
  a click on its track centres it there, and the pointer is an arrow over it
  and through a drag.
- Its reach is the widest line, counted in columns as the source's text is
  written (a CJK or full-width letter two, a tab four) whenever the text
  changes, or further when the caret took the pane further.
- The horizontal wheel over the source pans its lines; it and Shift+wheel
  stop at the widest line's end.
- A dragged scrollbar, the side one too, keeps the pointer when it passes
  over the rail.
- Wrapped, nothing runs past the pane and there is no bar.

## Fixture

`fixtures/editor-long-lines-314.md`: a heading and a short paragraph; the
reported long line with code spans; a line of CJK and full-width commas; a
table with a long row, bold, italic and code; a list item with a link and
inline math; a code block with a long line; a quote; a last short paragraph.

## Automated checks

`split_scrollbars` (`acrossBar`, `tests/split_scrollbar_tests.cpp`), in Paper
and Midnight at 1050 and 650 px: the bar shows with its thumb at the start,
clear of the corner; an arrow over it; a press on the thumb drags it, the
thumb following the pointer and the lines panning; dragged to the end the
widest line's end is in view, dragged back their starts, over the rail too;
a track click centres the thumb; the horizontal wheel pans by a notch's
60 px and stops at the widest line's end; wrapped, there is no bar.

Neutering one part at a time:

- No presses on the bar: 24 (the arrow, the drag, the end, the track click).
- No horizontal wheel over the source: 8.
- No end to the panning: 4.
- The rail first in a drag: 4 (dragged back over the rail).

## In the built app

An isolated portable copy (`out/issues-314-315/`, ignored), Paper, 1280 px,
the fixture in the source beside its preview: a thumb at the foot of the
editor under the line numbers' right; dragged, line 6 reads from its middle;
dragged to the end, "explicitly requested it." stands in view with the line
numbers where they were.

## Size

`tinta.exe` grows by 3,072 bytes (3,981,312 to 3,984,384).

## Limitations

- The reach counts columns as a monospaced face sets them; a proportional
  face or characters wider than two columns may reach a little short, and
  the caret still takes the pane further.
- Live and the reading view have no source pane and keep their own bars.
