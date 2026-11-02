# Pinned tabs, and a window pin that stays (#312)

Validated on Windows with the Release build on 2026-11-02.

## What was asked

1. The pin at the right of the title bar, which keeps the window on top,
   was off again every time Tinta started. It should stay as left.
2. Tabs should pin, from the tab's right-click menu, and a pinned tab
   should shrink as browsers shrink theirs.

## The change

- The title bar's pin is written to `settings.ini` (`alwaysOnTop=`) when
  pressed, as Open files in tabs is, and a window opens on top when it was
  left so. Other windows keep their own state until their pin is pressed.
- The tab menu has Pin tab, Unpin tab on a pinned one, between the close
  commands and Copy file path, in all eight languages.
- A pinned tab moves to the end of the pinned ones at the start of the row
  and is drawn 40 px wide with its name's first letter or digit (`Ö` for
  `Önemli notlar.md`, `中` for `中文笔记.md`), its unsaved or missing dot
  in the corner and no close button; its hover card names it. Unpinned, it
  goes right after the pinned ones.
- The pinned tabs are always in view; the others scroll and compress after
  them as before.
- Close all but this, Close all to the left and Close all to the right
  leave pinned tabs; their rows grey out when only pinned tabs would go.
  Close, Ctrl+W and a middle click still close a pinned tab.
- A pinned tab dragged moves among the pinned, another among the others; a
  tab dropped from another window or opened before the pinned ones lands
  after them.
- The session keeps which tabs were pinned (`pinnedTab=` in `[Session]`),
  and they lead the row again on the next launch (`tabsPinPaths`).
- The README says so.

## Fixture

`fixtures/pinned-tabs/`: six notes, two of them named past ASCII (Turkish
and Chinese), with headings, lists, a table, tasks, code, maths and links
between them.

## Automated checks

`pinned_tabs` (`tests/pinned_tab_tests.cpp`, a folder of its own): six tabs
from the fixture; Pin tab from the menu moves a tab to the row's start, the
active document unchanged, and a second one goes after it; in Paper and
Midnight at 1050 and 650 px the pinned tabs are drawn narrow, one width, in
order before the others, without a close button; resting on a pinned tab
brings up its card with the file's name; Close all to the left of
the second pinned tab closes nothing; a click activates a pinned tab, which
dragged right stays among the pinned, and another dragged over them stops
after them; a drop over them and an open before them land after them;
Close all but this spares the pinned; Unpin puts the tab right after them;
`settings.ini` keeps the pinned session tabs and a restored session puts
them first in order, the active tab kept; the title bar's pin, pressed,
writes `alwaysOnTop=1` without touching the rest, and pressed again `0`.

Neutering one part at a time: no narrow pinned group (8 failures), a pinned
tab sent to the row's end (17), sweeps that close pinned tabs (1), drags
that ignore the groups (6), opens among the pinned (1), no `pinnedTab=` in
the session (3), the pin not remembered (1).

## In the built app

An isolated copy (`out/pin-312/`, Paper, 1280 x 900) whose `settings.ini`
held a session of the six notes with two pinned and the window pin on,
driven with posted messages and captured with PrintWindow
(`out/pin-312/shots/`):

- Launched without a file: `A` and `Ö` lead the row, the other four after
  them as before, the pin lit and the window on top (WS_EX_TOPMOST).
- Right-click on `Budget 2026.md`: Pin tab; pressed, `B` joins them third.
  On `Ö` the menu reads Unpin tab, Close all to the left greyed.
- Close all but this on `Design review.md`: the two other unpinned tabs
  closed, `A`, `Ö` and `B` stayed. `B` clicked: raised as the active tab.
- At 650 px wide the three and `Design review.md` fit beside the +.
- Closed and started again: `A`, `Ö`, `B` pinned and first, `B` active; the
  saved `[Session]` lists them as `pinnedTab=`.
- The pin pressed off: `alwaysOnTop=0` and, started again, not on top;
  pressed on: `alwaysOnTop=1`.
- `中文笔记.md` added to the saved session as pinned: a fourth narrow tab,
  `中`, after `B`. (The hover card waits on the real pointer, which posted
  messages do not move, so it was checked in `pinned_tabs` instead.)

## Size

`tinta.exe` grows by 8,192 bytes (3,952,640 to 3,960,832).

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
57 of 57, `pinned_tabs` among them.

## Limitations

- Pinned tabs come back with the session, which is restored on plain
  launches with Open files in tabs on, by the window that owns it.
- A tab pulled out to a new window, or dropped on another one, arrives
  unpinned there.
- A window opens on top when the last pin pressed, in any window, was on.
