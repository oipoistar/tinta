# Reopen closed tab

Validated on Windows with the Release build on 2026-11-03. The design is in
`docs/specs/reopen-closed-tab.md`.

## What was asked

From the todo list: Ctrl+Shift+T brings back the most recently closed tab
with its scroll position, its mode and any unsaved text; pressed again, it
walks further back; the tab menu lists recently closed tabs; every way a
tab closes counts.

## The change

- Every close the user makes is remembered, newest last, 25 per window, in
  memory only: Ctrl+W, Ctrl+F4, a middle click, the close button, the tab
  menu's Close, Close all but this and the left and right sweeps (one
  group, reopened together), and the last tab closing into the start page.
- A tab closed in the editor keeps its buffer, caret, scroll, word wrap and
  byte order mark; "Don't save" hands the text it throws away to the close
  it answers, so the tab comes back with it, unsaved. Untitled notes come
  back as untitled notes.
- Not remembered: the start page, an empty note, Tinta's own closes (an
  empty start page giving way to a file, a tab dragged to another window)
  and closing the window.
- Ctrl+Shift+T reopens the newest entry or sweep beside the file that was
  to its left (else at its old index), activates it and, on a lone start
  page, replaces it. A file open again is switched to; a file gone with no
  text to restore is reported ("File not found:") and the one before it
  reopens instead. Nothing happens while the unsaved-changes dialog is up.
- The tab menu ends with Reopen closed tab (Ctrl+Shift+T), greyed when
  nothing is closed, then up to five closed tabs by name, newest first, an
  unsaved one marked with a dot; a row reopens the tab it names. The menu
  measures its labels, so the longer translations fit beside the shortcut.
- Ctrl+T still opens the start page. The keyboard help (F1) and the README
  list Ctrl+Shift+T. "Reopen closed tab" is in all eight languages.

## Fixture

`fixtures/reopen-closed-tab/`: four notes, one named past ASCII (`Notizen
über Tinte.md`), with headings, lists, tasks, tables, quotes, code, maths,
a Mermaid diagram, footnotes and links between them, long enough to
scroll.

## Automated checks

`reopen_closed_tab` (`tests/reopen_closed_tab_tests.cpp`, a folder of its
own, keys through `tests/keystroke.h`): Ctrl+W on a reader tab scrolled to
half its length, then Ctrl+Shift+T: back at index 1, active, at the same
scroll, the entry used up, and a second Ctrl+Shift+T does nothing; a
middle click and the close button are remembered and undone; Close all but
this closes three as one group and one Ctrl+Shift+T restores the old
order; the menu shows no recent rows with nothing closed, its Reopen row
is then inert, and with two closed it lists exactly two, the older one's
row reopening only that tab and Reopen closed tab the newest, every tab
back in place; a clean editor tab closed with Ctrl+F4 comes back in the
editor with its text, caret (120) and scroll (64); typing into the German
note, Ctrl+F4 and Don't save close it with the file untouched, and it
comes back unsaved with the typed text under its own name; thrown away
again, it is kept again; an untitled note comes back untitled, unsaved,
with its text; a closed file opened again is switched to, not doubled; a
deleted file is skipped with a warning naming it and the tab before it
reopens; Ctrl+T opens the start page, and closing it, or an internal close,
leaves nothing to reopen; a pinned tab comes back pinned and first;
Ctrl+Shift+T works from Live; the last tab closes into the start page and
Ctrl+Shift+T replaces the start page with it; 30 closes keep 25, the
oldest gone first.

The neighbouring suites (`pinned_tabs`, `deleted_tab`, `reading_position`,
`input_shortcuts`) pass unchanged.

## In the built app

The test saves frames of the real drawing code beside itself
(`build/reopen-closed-tab-test-Release/*.png`), and they were inspected:

- The tab menu with two closed tabs, in Paper and Midnight at 1050 and
  650 px: Reopen closed tab with Ctrl+Shift+T at the right after a rule,
  then Reading list.md and Field notes.md set in under it.
- The same menu in German (Geschlossenen Tab öffnen), French, Russian
  and Chinese: the menu widens to its longest label and the shortcut stays
  clear of it; every one fits the window.
- The keyboard help: Ctrl+Shift+T, Reopen closed tab, under Ctrl+W /
  Ctrl+F4.
- The German note reopened after Don't save: in the editor, An unsaved
  thought. on its last line, the tab's unsaved dot lit.

An isolated copy (`out/reopen-tab/`, a seeded four-tab session) was driven
with posted messages, but in this remote session neither PrintWindow nor
a copy from the screen returns the window's Direct2D content (white and
black frames), so the window itself was not captured; the frames above
come from the same drawing code.

## Size

`tinta.exe` grows by 10,240 bytes (3,984,896 to 3,995,136).

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
59 of 59, `reopen_closed_tab` among them.

## Limitations

- The list is per window and in memory: it ends with the window. A tab
  dragged into another window has moved, not closed, so it is not listed.
- Undo history, selections and extra carets do not come back.
- Live or Source, and the docked preview, follow the editor preference
  rather than the closed tab, as they do for every tab.
