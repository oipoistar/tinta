# Editor conveniences (#251)

Validated on Windows with the Release build on 2026-10-08.

## Request

Three editor conveniences from code editors: Alt+Up / Alt+Down to move the
current line, a quote, bracket or Markdown marker typed over a selection
wrapping it instead of replacing it, and multiple cursors, for example to line
up the pipes of a table.

## Change

- **Move lines.** Alt+Up / Alt+Down swap the caret line, or every line a
  selection touches, with the line above or below; a selection ending at a
  line start leaves that line. Carets and selections ride along, nothing moves
  at the document edges, and one Ctrl+Z puts the lines back. Alt+arrows arrive
  as `WM_SYSKEYDOWN` and go to `editorAltArrowKey`, next to the existing
  Alt+Left / Alt+Right navigation.
- **Wrap a selection.** `" ' ` * ~ ^ = :` wrap a selection in the same
  character, `( [ {` in their pairs. The text stays selected in its original
  direction, so a second `*` makes it bold. One undo step; the Editor assists
  switch turns it off with the other assists. Other characters still replace
  the selection.
- **Multiple carets.** Alt+Click adds a caret or removes one, Ctrl+Alt+Up /
  Ctrl+Alt+Down add one above or below in the same column, and Ctrl+D selects
  the word at the caret, then each next occurrence. The newest caret is the
  primary one, which the view follows. Typing, Enter, Tab, Backspace, Delete,
  arrows, Home/End, Page Up/Down, Ctrl+arrows, Shift selection, Ctrl+B/I, the
  rail's marks and line prefixes, copy, cut and paste act at every caret.
  Copy joins the selections line by line, and pasting that back hands each
  caret its own line. Esc, a plain click or Ctrl+A return to one caret; undo
  and redo return to one caret too.
- **How.** The extra carets live beside the existing primary caret. Each
  command swaps every caret into the primary slot in turn and runs the
  existing one-caret code, editing from the last caret to the first so earlier
  positions stay valid. Every keystroke is one undo group: the new `group` on
  `EditAction` makes undo and redo take a whole group back. Ctrl+B/I, which
  used to take two undo steps, take one now.
- Ctrl+Alt+Up/Down are handled ahead of the AltGr guard in `handleKeyDown`;
  arrows type nothing, so the guard has nothing to protect for them. An
  Alt+click arms `altClickGuard`, which swallows the window-menu request
  (`SC_KEYMENU`) its Alt release would otherwise send.
- The editor's motion keys moved into `editorNavigate` unchanged, so one copy
  serves every caret.

## Fixtures and automated checks

`fixtures/editor-multicursor-251.md` is a mixed note with headings, lists, a
quote, a misaligned table, code and CJK text, with instructions for each
gesture.

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

`editor_features` (131 checks) drives the real key, character and mouse
handlers with real modifier state, unwrapped and wrapped. It covers line moves
(column kept, document edges, the last line without a newline, selections,
several carets, neighbouring blocks, one-step undo, inert outside the source
editor), every wrap pair, bold by two `*`, backward and multi-line selections,
the assists switch, column carets typing, deleting and undoing in a table,
Esc, carets keeping their column on Down, End/Home/Up with merging carets,
Shift selection, Ctrl+Right, Alt+click add and remove, the Alt-release guard,
a plain click, Ctrl+D through every occurrence, and the copy and paste text.
All 29 CTest suites pass.

## Exports

The fixture rendered to HTML, DOCX, PDF and print pages in Paper and Midnight
with the 3.7.4 and the new build. HTML, every print page and every DOCX
member were identical, and the PDFs were valid. Outputs are under the ignored
`out/open-issue-features/exports/` directory.

## Desktop check

Not run yet: the workstation was locked while the build was ready, which
blocks keyboard input and window capture.

## Limits

- Ctrl+Alt+Up/Down follow logical lines, also with word wrap on.
- Undo restores the text and leaves one caret.
- The native checks see the Alt-release guard armed; a live Alt+click with a
  real Alt release is still to be watched.
- Some graphics drivers claim Ctrl+Alt+arrow keys for screen rotation; on
  those machines the driver wins.
- Find and replace, links, snippets and table or diagram insertion act at the
  primary caret and drop the others first.

## Draft reply (not posted)

### #251

Thanks for the ideas — all three are in the next release:

1. Alt+Up / Alt+Down move the current line, or every line of a selection,
   up or down. One Ctrl+Z undoes the move.
2. Typing `"`, `'`, `` ` ``, `*`, `~`, `^`, `=`, `:`, `(`, `[` or `{` over a
   selection wraps it instead of replacing it and keeps the text selected, so
   `*` twice makes it bold. It follows the Editor assists setting.
3. Multiple cursors: Alt+Click adds or removes a cursor, Ctrl+Alt+Up /
   Ctrl+Alt+Down add one above or below in the same column, and Ctrl+D selects
   the word and then each next occurrence. Typing, deleting, the arrow keys,
   copy and paste work at every cursor, and Esc returns to one. For a table,
   put a cursor in the same column on each row and type spaces until the
   pipes line up.
