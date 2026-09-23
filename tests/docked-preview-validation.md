# Docked preview and quieter hints (#245)

Validated on Windows with the Release build on 2026-09-23. Reported by
[@whiskyrye](https://github.com/whiskyrye) in
[#245](https://github.com/oipoistar/tinta/issues/245).

## Problem

In edit mode the preview was a floating sheet: inset from the window edges,
with rounded corners and a shadow, rising past the tab strip and carrying the
window buttons. The report reads it as a separate window lying over the editor.
Two on-screen prompts sat over the text as well. The Read · Ctrl+Shift+E button
permanently covered the bottom left of the source. The "Press Esc to exit edit
mode" chip appeared on every edit-mode entry and parked in the bell tray, so the
bell's unread badge kept growing.

## Change

- The preview is docked like the Contents and file browser panels (#213). It is
  a flat pane on the reader's page color, from the seam to the window's right
  and bottom edges, below a tab strip that spans the whole window again. One
  hairline marks the seam and takes the accent while the pointer is on it or
  drags it. The page starts where the reader's page starts, and the preview
  scrolls exactly as far as the reader does.
- The title bar is the reader's in every mode: tabs, the drag area, tab drops
  and the window buttons use the full strip. The special hit-test band over the
  sheet and the caption island are gone.
- New hint chips hold for 1.5 s, then fade in place over 0.4 s. They never park
  in the tray or add to the bell's badge. A click, or a press anywhere else,
  fades them at once, and leaving edit mode or switching away from its tab
  retires them.
- "Press Esc to exit edit mode" shows only in the first three edit sessions.
  The count is saved as `editHintsShown` in settings.ini at exit, only ever
  increasing, and a malformed value starts the hints over. Returning to a
  parked edit tab continues its session and replays nothing.
- "Press ESC again", the Ctrl+E preview chip and the Ctrl+W wrap chip answer a
  key press, so they still appear every time, as fading hints.
- The Read button fades in for about two seconds in those first sessions, on
  entry and when switching to or from the full-width reading view. Otherwise it
  stays hidden and clicks go through to the text. It fades in while the pointer
  rests on its corner, and an insert menu opened over that corner keeps it
  hidden.
- Help (F1) lists Ctrl+Shift+E for the reading view and ESC ESC for leaving edit
  mode again. 3.7.3 had replaced the ESC ESC row, and 3.7.4 restored the key
  without the row. README describes the docked preview and lists Ctrl+Shift+E.

## Fixtures and automated checks

`fixtures/docked-preview-245.md` mirrors the report: an nginx server block with
long lines, a table with CJK cells and inline code, a task list, a quote, inline
math and a final heading.

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

- The full Release build succeeded with no compiler warnings, and all 29 CTest
  suites passed.
- The new `docked_preview_and_hints` suite runs the input tests binary from a
  scratch folder with its own portable settings.ini. In Paper and Midnight at
  650 and 1050 pixels it checks that the split preview runs to the right edge,
  that the hairline sits between source and preview, and that the first block
  starts where the reading view's does. With eight tabs, tabs reach past the
  source column, drops there land between tabs, and the drag area sits beside
  the window buttons.
- It then covers the hints. Four sessions show the Esc hint and the Read
  button's intro only in the first three, and a parked tab replays nothing. A
  hint fades in place and leaves the tray and the bell alone, while a regular
  chip still parks. Clicks and outside presses fade hints, double Esc and tab
  switches retire them, and Esc, Ctrl+E and Ctrl+W answer with hints only. The
  Read button is hidden and click-through after the first sessions, stays hidden
  under an open insert menu, and fades in on hover. It opens the reading view,
  leads back from the same corner, and fades out on its own after an intro.
  Finally, settings.ini keeps the count, and malformed values start over.
- `window_drag_and_title_space` asserts the drag area beside the window buttons
  in all three modes: reader, split editor and reading view.
  `tab_drop_position` no longer skips tabs right of the source column.
- With six deliberate regressions applied together, the new suite reported 25
  failures and `tab_drop_position` 12, each naming its regression. The six were
  the old right inset, hints parking in the tray, a hint session every time, a
  hidden Read button taking clicks, hints outliving edit mode, and refusing tab
  drops right of the source column.

## Desktop check

The built application ran in portable mode from a scratch folder with fresh
settings, in Paper and Midnight at 650 and 1050 pixels in a 760 pixel tall
window. Input was posted window messages, plus the real pointer for hover.

- Session one: 0.45 s after `:` the Esc hint and the Read button were up; at
  2.75 s both were gone and no bell appeared.
- A hit-test scan of the title bar in the split editor returned caption (drag)
  everywhere between the tab's controls and the pin, beside the source and the
  preview alike. In 3.7.4 the band over the sheet was client area by design.
- One Esc showed "Press ESC again to exit edit mode"; 2.3 s later it was gone,
  again without a bell.
- Session two: clicking the Read button during its intro opened the reading
  view, which showed Edit · Esc for its own intro.
- Session four: no hint, no Read button and no bell. A click on the hidden
  button's spot placed the caret on the source line beneath it.
- Real pointer, Paper at 1050: resting on the corner revealed the Read button,
  and resting on the seam lit the hairline (211,158,138 against 227,222,213 at
  rest). In Midnight the resting hairline is 34,46,60 between the source at
  20,34,48 and the page at 13,27,42.
- After closing, settings.ini held `editHintsShown=3` in all four runs.
- The first live run showed "Press ESC again" lingering for about a second
  after a double-Esc exit. Hints now fade as edit mode ends; the rerun
  confirmed it.

## Exports

The four fixtures `docked-preview-245.md`, `reading-view-242.md`,
`table-input-236.md` and `markdown-regression-control.md` were exported in
Paper and Midnight by the official 3.7.4 binary and by this build. HTML bytes
and all DOCX archive members were identical, and every PDF was valid. In the
first run, three Paper print pages differed in 11 anti-aliased glyph pixels in
total. An immediate rerun of the same two binaries had zero changed pixels, as
did each binary against itself. Outputs are under the ignored `out/issue-245/`
directory.

## Limits

- The docked pane starts below the tab strip, like the reader and the side
  panels. The sheet rose past the strip to 10 px below the window's top, so at
  100% scale the preview's first line now sits 32 px lower. The pane gains the
  sheet's 14 px bottom and 16 px right margins, and the tab strip gets the full
  width back.
- The title-bar hit test and the exit-time settings save live in the window
  procedure, which the test binary does not link. Both were verified live.
- Posted messages cannot hold Ctrl and Shift, so Ctrl+E, Ctrl+W and
  Ctrl+Shift+E were covered by the native tests. Posted hovers are cancelled by
  the real pointer's position, so hover was checked once with the real pointer.
- Once the first sessions are over, the Read button is found by pointing at its
  corner or through Ctrl+Shift+E, which Help and the README list.
- Word wrap stays off by default. Long lines scroll sideways in the source, and
  Ctrl+W wraps them.

## Draft reply (not posted)

### #245

Thanks for the report and the screenshot. In the next release the preview no
longer floats over the editor. It is docked beside the source like the Contents
and file browser panels, running to the window's edges below the tab bar. Ctrl+E
still hides it when you want the whole width for editing, and you can drag the
divider to resize both sides. The hints are quieter too. "Press Esc to exit edit
mode" and the Read · Ctrl+Shift+E button appear only in your first few edit
sessions and fade out after about two seconds, and hints no longer collect
behind the bell. After that the Read button stays hidden until you point at its
corner, so it no longer covers your text. If long lines get cut off at the
divider, Ctrl+W turns on word wrap in the editor.
