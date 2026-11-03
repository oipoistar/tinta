---
title: Reopen closed tab
status: Implemented
created: 2026-11-03
---

# Reopen closed tab

Ctrl+Shift+T brings back the tab closed last, as it was: same place in the
strip, same scroll, reader or editor, and any text that was never saved.
Pressed again, it walks further back. The tab menu offers the same, with the
last few closed tabs by name.

## Why

Closing the wrong tab is the most common tab mistake, and every browser and
editor has taught the same remedy. Tinta's tabs carry more than a path: an
edit buffer, a caret, sometimes text that was discarded by a hurried "Don't
save". Losing that to one click is worse than losing a web page.

## Behaviour

### What is remembered

Every close the user makes is remembered, newest first, up to 25 per window:

- Ctrl+W and Ctrl+F4, middle-click, the tab's close button, the lone title's
  close button, and the tab menu's Close.
- Close all but this, Close to the left and Close to the right. The tabs one
  such command closes are a group: one Ctrl+Shift+T reopens the whole group
  in its old order.
- Closing the last tab (which turns it into the start page).
- Closing a tab with unsaved changes, whatever the answer: after Save the
  tab reopens from the saved file; after Don't save it reopens with the
  discarded text, still unsaved, so a hurried answer can be taken back.

Not remembered: the start page, closes Tinta makes itself (an empty start
page replaced by a file, a tab dragged into another window) and closing the
window. The list lives in memory only: nothing discarded is written to
disk, and it ends with the window.

### What comes back

- The tab's place in the strip (clamped to the strip as it is now), and its
  pin.
- Reader tabs: the scroll position and zoom (from the reading position Tinta
  already keeps per document).
- Editor tabs: the text, whether it is unsaved, the caret, the editor's scroll,
  word wrap and the byte order mark. Live or Source follows the editor
  preference, as for any tab.
- Untitled notes: their text, as an untitled note.
- Sheets: the sheet, with its view (kept by the sheet itself).

Undo history, selections and extra carets do not come back.

### Reopening

- Ctrl+Shift+T reopens the newest entry (or group) and activates it.
- A file that is open again already: Tinta switches to its tab and drops
  the entry.
- A file that no longer exists and has no text to restore: the entry is
  dropped with a notice ("notes.md no longer exists"), and the next one is
  reopened instead.
- On the start page as the only tab, the reopened tab replaces it, as
  opening a file from the start page does.
- Nothing happens while the unsaved-changes dialog is open.

### Where to find it

- Ctrl+Shift+T. Ctrl+T keeps opening the start page.
- The tab menu (right-click a tab): **Reopen closed tab** with its shortcut,
  then up to five recently closed tabs by name; clicking one reopens that
  one. The rows are greyed when the list is empty.
- The keyboard help (F1) and the README's shortcut table list it.

## Design

- `App::ClosedTab` (app.h) holds what a `DocTab` holds that matters for
  reopening: path, title, pinned, index, group, edit mode, editor text, dirty,
  caret, editor scroll, word wrap, BOM, last write time. `App::closedTabs`
  is a vector, newest last, capped at 25. `App::closedTabGroup` numbers
  groups.
- `tabCloseIndex(app, hwnd, index, TabClose how = TabClose::User)`.
  The closes Tinta makes itself pass `TabClose::Internal`. The record is
  made once the close is certain (after the unsaved-changes check) and
  before the edit buffer is cleared; `tabBecomeStartPage` records the same
  way.
- The unsaved-changes dialog's Don't save keeps the buffer it discards in
  `App::discardedEdit` (text, BOM, caret, scroll) for the close that follows;
  `tabCloseIndex` folds it into the record when the tab ids match, and any
  other path clears it.
- Bulk closes take one group number for the whole command.
- `tabReopenClosed(app, hwnd, entry)` reopens an entry (the newest when
  `entry` is negative) and the rest of its group. Reader tabs go through
  `tabOpenPath` with `insertBefore`, so the reading position restores as it
  does for any open. Editor tabs are built as parked `DocTab`s (edit mode,
  text, caret, scroll) and activated, which is how `restoreEditBuffer`
  brings back a parked tab; their `lastWrite` is the file's time at close,
  so an unchanged file raises no "changed on disk" prompt.
- `shortcutModifiersAllowed` (keymap.cpp) lets Ctrl+Shift+T through; the
  Ctrl+T branch of `handleKeyDown` tells Shift apart.
- The tab menu's static table gets `TabMenuReopen` at the end of the enum;
  the recent rows are drawn after it from `closedTabs` with their file names.

## Strings

`tab.menu.reopen` "Reopen closed tab", `tab.reopen.gone` "%s no longer
exists", `help.reopenTab` "Reopen closed tab", in all eight languages.

## Testing

- `reopen_closed_tab` (input_tests, `--reopen-closed-tab`) with the
  documents in `tests/fixtures/reopen-closed-tab/` (headings, lists, a table,
  a quote, code and maths):
  - each close path records once, in order, and Ctrl+Shift+T restores index,
    activity, scroll and pin;
  - Close all but this reopens as a group, in order;
  - an editor tab comes back in the editor with its caret, scroll and text;
  - Don't save comes back unsaved with the discarded text, for a file and
    for an untitled note;
  - an open file activates instead of duplicating; a deleted file is
    skipped with the next entry reopened;
  - internal closes and the start page are not recorded; the list stops at
    25;
  - Ctrl+T still opens the start page; the tab menu rows reopen the entry
    they name.
- `tests/reopen-closed-tab-validation.md` records the manual pass in the
  built app.
