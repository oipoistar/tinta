# Middle-click autoscroll (#331)

Validated on Windows with the Release build on 2026-11-21.

## What was asked for

The autoscroll of browsers: a middle click leaves a mark, the page scrolls
towards the pointer, faster the further the pointer is from the mark, and a
click ends it. The request named the reading view first, the editor "if it
is cheap", a timer rather than mouse moves, a speed that is the same at
every screen scale, a mark that causes no relayout, and an optional setting.

## What changed

- **A middle click** on the page or on the editor's text leaves a round
  mark with an arrow up and one down. The view scrolls towards the pointer
  and stands still while the pointer rests on the mark. On a page wider
  than its window the mark has four arrows and the page goes sideways too.
- **It ends** with any click (which does nothing else, nor does its
  release), any key (Esc does nothing else; any other key then does what
  it does), the wheel, a finger on the screen, or the window losing the
  keyboard. Another tab or document coming up under it ends it too.
- **Held and dragged** instead, the view scrolls while the button is down
  and stops where it is let go. A press let go without leaving the mark is
  the click.
- **Where.** The reading view, the page beside the source editor (the
  editor follows it, as it follows the page's scrollbar), Live's page, and
  the source editor's text. Not on the tab strip, where a middle click
  closes a tab; not on a sheet, which pans with the button held; not on a
  side panel, a scrollbar, the find bar, the start page or under a dialog;
  and not on a page that fits its window, where there is nowhere to go.
- **The speed** is 1.5 d + 0.05 d² pixels a second for a pointer d pixels
  past the mark, both in the pixels of a 100% screen, and at most 15,000:
  20 a second at 10 pixels, 650 at 100, 4,950 at 300. The mark's radius of
  12 is where the pointer can rest.
- **A timer** steps it, by the time that has really passed since the last
  step, so the speed does not depend on how often the timer fires; a stall
  is not made up for by a leap. The view is put on whole pixels, the
  fractions kept until they make one, and on its exact end at the ends.
- **`middleClickScroll=0`** in `settings.ini` turns it off. There is no
  switch for it in Settings.

It is built on what a finger's pan already uses (`touch.cpp`): the same
answer to "what scrolls here", the same ranges and the same scrolling, so
the editor, the page beside it and Live came with the reading view.

## Fixture

`fixtures/middle-click-scroll-331.md`: instructions to follow by hand, then
a page of several screens with a quote, nested and task lists, links,
tables, a note, a footnote, inline math, a line each of Turkish, Chinese
and Japanese, and a code block with a line much longer than the page, for
the sideways case with code wrapping off.

## Automated checks

**`middle_click_scroll`** (`autoscroll_tests.cpp`, 876 checks). The
steps are called at times of the test's own clock, as the touch tests step
a fling; the app's timer calls the same step.

In the reading view, in a light and a dark theme, at 1050 and 650 wide, at
100% and 150%:

- The speed: none on the mark, the table above within 1%, always faster
  further out, as fast up as down, the top speed, and all of it in
  proportion to the screen scale.
- It does not start on the tab strip, under an open dialog, with the
  setting off, or on the Contents list, which has more rows than it shows
  in a low window and scrolls for a finger. A press starts it: the mark where the press fell, the window
  holding the mouse, the pointer hovering over nothing.
- Nothing moves while the pointer is on the mark. Let go there, it stays
  on. A second at 100 below the mark is 650 down, in 8 ms steps and in
  40 ms steps alike; put somewhere else meanwhile, the page goes on from
  there; a step after a five-second pause moves a tenth of a second's
  worth; two seconds at 10 below is 40 down, a pixel at a time;
  every step leaves the page on a whole pixel.
- Sideways the pointer pulls nothing on a page that fits its width.
- Up to the top and down to the end: it stops there exactly, rests without
  a tremor for a second of steps, stays on, and turns back on the next
  step. Pulled gently against the top for two seconds, give or take a few
  steps, it moves again 48 ms after the pointer goes below the mark.

- Each of the four mouse buttons ends it, is taken together with its
  release, selects nothing and opens no menu; the next click is the
  window's again, and only the ending click's own release is taken.
- The wheel, a finger, an Alt key and the window losing the keyboard end
  it and go on to the window; its own mouse capture does not end it,
  another window's does.
- Ctrl and Shift by themselves leave it on. Esc ends it and does nothing
  else. PgDn ends it and pages. Ctrl+F ends it and opens the find bar.
- Held and dragged it scrolls the same and ends where the button is let
  go, also when the pointer is back on the mark by then.
- Another tab coming up ends it without a move.
- The mark is drawn where the press fell, with the ink of an arrow above
  and below the centre and none beside it, a centre that stands out from
  the page, and nothing drawn away from it; it is gone after the end. On a wide page it has an arrow
  left and right as well, and the page goes right, back to the left edge,
  and both ways at once.

In the editor: the source's text scrolls the same, nothing is typed or
selected, an arrow key ends it; the page beside the editor scrolls and the
editor follows; the editor closing under it ends it; Live's page scrolls
and Esc ends it without leaving the editor. A page that fits its window
starts nothing. `middleClickScroll=0` is written, read back, and on again.

Each part taken out in turn fails the test:

| Taken out                                         | Failing checks |
|---------------------------------------------------|---------------:|
| The pointer rests on the mark                     |            179 |
| The speed near the mark                           |            107 |
| The speed far from the mark                       |            115 |
| The speed has a top                               |              8 |
| The speed follows the screen scale                |             60 |
| Above the mark it goes up                         |             73 |
| middleClickScroll=0 turns it off                  |             16 |
| A page that fits starts nothing                   |              1 |
| A page that fits its width does not go sideways   |             17 |
| A wide page goes sideways                         |             32 |
| No hover under a steering pointer                 |              8 |
| The window holds the mouse                        |              8 |
| The mouse is let go at the end                    |             33 |
| Leaving the mark with the button down is a drag   |             24 |
| A click leaves it on                              |            245 |
| Another tab or view ends it                       |             49 |
| A pause is not made up for                        |             16 |
| Whole pixels                                      |             16 |
| The end is reached                                |              4 |
| The ending click release is taken                 |             40 |
| Only that release                                 |              8 |
| A click ends it                                   |             82 |
| The wheel ends it                                 |             48 |
| Losing the keyboard ends it                       |             16 |
| Its own capture does not end it                   |              8 |
| The pointer steers by its moves                   |             64 |
| The mark has four arrows on a wide page           |              8 |
| The mark goes at the end                          |             24 |
| A key ends it                                     |            178 |
| Esc only ends it                                  |              8 |
| Ctrl and Shift alone are no key                   |             16 |
| The setting is read                               |              1 |
| The setting is written                            |              2 |
| Only the page and the editor text                 |            144 |
| Kept within the ends                              |              8 |
| It goes on from where something else put the view |              8 |
| The mark has arrows                               |             16 |

Three were not caught at first: a side panel's list would have scrolled
too, a pull against an end was stored up, and the mark could have lost its
arrows. Each has a check of its own now, as has going on from where
something else put the view; the other rows' counts are from before those
were added.

The whole suite passes, 88 of 88, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which overwrite the clipboard.
`notification_setting` writes `settings.ini`, which has a new line: it was
run by itself with the clipboard kept and put back, and passes.

## In the built app

`tinta.exe` in a portable folder of its own (`out/audit/realapp_batch.py`,
not in the repository), with the fixture, in a window 900 by 640. The
window is moved so that its middle is 250 pixels above where the pointer
happens to be, a middle press and release are posted to its middle, and
1.2 seconds later Esc and then a close. The app's timer reads the pointer
itself. Where the page was is read from the place the app saves as it
closes.

| Build | Page when closed | After Esc |
|-------|-----------------:|-----------|
| master before this | 0 | the window had closed: Esc quits the reading view |
| this build | 3,722 | the window was still open, and answered |

The pointer was not moved, by the script or otherwise, during either run.

## Speed and size

- A step by itself takes about 21 microseconds (20,000 steps timed in the
  test). The frame each one asks for is the frame a turn of the wheel asks
  for; it was not timed separately.
- The mark is drawn over the finished page: no layout is made for it.
- `tinta.exe` is 5,350,400 bytes with all of this batch (#331, #332, #334,
  #335), 21,504 more than master's 5,328,896.

## Limitations

- Screen copies come back black here, so the window was not looked at.
  The mark was looked at as the picture the test draws of it
  (`frames/` in the test's folder), which has the mark and not the page
  under it.
- The speed's curve is chosen from the numbers above and from what
  browsers do; it was not tuned with a hand on a mouse.
- The pointer becomes Windows' up-and-down (or four-way) sizing arrow
  while it steers. Browsers have arrows of their own for this; Windows
  offers none.
- A side panel's list, Help and Settings are not scrolled this way.
- Live's page stops where a finger's pan stops it, at the page's own
  height, which can be a little short of where the wheel goes while a
  block is open for editing.
- Only `settings.ini` turns it off.
- A middle click on a link starts it like anywhere else on the page; it
  opens nothing, as before.
