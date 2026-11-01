# The rail's list marks and pastes on lists (#304, #305, #306)

Validated on Windows with the Release build on 2026-11-01, on master after
the touch follow-ups (4.0.0 development).

## What changed

- #306: the rail's bullet, task and quote buttons mark every line a
  selection takes in, or take the mark off them all when they all have it,
  as one Ctrl+Z. A selection ending at a line's start leaves that line out,
  and a blank line inside a selection of several lines stays blank. Several
  carets mark their lines as before, each line once.
- #304: a numbered list button under the bullets (`1≡`, "Numbered list" in
  the eight languages). The numbers count on across the selection or the
  carets, at each level of a nested list; pressed again it leaves plain text.
- #305: lines pasted on a list item become its items, as Enter makes them:
  numbers count on (in Live the numbers below follow), a task gets an empty
  box, a nested item keeps its indent, and the rest of the line becomes the
  last item. Lines pasted in a quote are quoted, its blank lines keeping the
  quote going. Blank lines stay blank, lines with a mark or an indent of
  their own keep it, a paste ending in a line break leaves no empty item,
  and text holding a table or a fence goes in as it is. Ctrl+Shift+V pastes
  as it is (in Live too, where it pasted like Ctrl+V), and so does Ctrl+V
  with the editor's assists off.

Source's buttons now share Live's (`live::lineMarkEdits`), so they also stop
doing what the old prefix toggle did: the bullet button on `- [ ] task` left
`[ ] task`, the task button on `- item` made `- [ ] - item`, and an indented
item got a second mark in front of its indent. Lines given a bullet take the
bullet the selection's list already has, so the list stays one.

All the lines change in one replace and one undo step: numbering 20,000
lines takes about 5 ms, where a change per line copied the document once a
line. A short window sets the rail's buttons closer (down to 26 DIP apart)
so the last stays above the Pandoc button and the window's bottom. In Live,
a pointer leaving a rail button straight for the page left the button's
name showing; it goes now.

## Automated checks

- `editor_features`: the rail's marks on selections, carets, nested lists
  and blank lines, with and without word wrap; the numbered button's place,
  name and click; the rail at 900, 600 and 540 px, with and without the
  Pandoc button (frames saved); 20,000 lines in one step; the fixture's two
  groups of lines with the rest of it untouched; `pasteMarks` on its own
  (bullets, numbers, tasks, quotes, nested items, blank lines, own marks,
  fences, tables, code, a paste before the mark), then Ctrl+V,
  Ctrl+Shift+V, the assists switched off and the insert menu's Paste
  through the editor, and the fixture's pastes after Enter at each list's
  end.
- `live_editor`: the numbered button in Live (a click, one Ctrl+Z), its
  name cleared when the pointer goes on to the page, and pastes on a
  numbered item (the numbers below follow, one Ctrl+Z), in a quote and with
  Ctrl+Shift+V.
- `sheet_canvas`: lines pasted on a list item in a card being typed in.
- Neutering: with the caret's own line only, no numbered button and the
  paste as it is, exactly the 47 new checks fail (42 in `editor_features`,
  4 in `live_editor`, 1 in `sheet_canvas`); without the hover fix its one
  check fails.
- The suite passes 55 of 55, run without notification_setting,
  copy_file_path and code_block_layout, which write the clipboard and leave
  it so. The paste checks put the clipboard back as they found it.

## In the built app

An isolated portable copy (`out/list-marks-playground/`, ignored) was driven
with posted messages through its message loop and captured with PrintWindow
(`out/list-marks-playground/shots/`), on
`tests/fixtures/editor-list-marks-304-306.md` in Paper at 1280 x 900:

- Source: a drag over Milk, Eggs and Bread and the bullet button made three
  bullets, the preview a list, the selection still on the three lines. The
  numbered button on the second group made 1, 2 and 3 and the preview a
  numbered list; pressed again each group was plain text, at the fixture's
  1,132 characters.
- Live (the pill): the selection came along, and the numbered button made
  the three lines a numbered list, its name showing beside it; pressed
  again they were plain.
- At 1000 x 560 the rail's twelve buttons sat closer, all inside the
  window.
- The file on disk stayed byte for byte as it was.

Pasting was not tried in the app, so as not to write over the clipboard;
the native harness pastes through the real handlers with the clipboard
saved and put back. The rail frames of `editor_features` are in the test
window's dark default.

## Size

`tinta.exe` grows by 7 KB (3,918,336 to 3,925,504 bytes).

## Limitations

- Source's Enter does not renumber the items below the new one, and a
  paste in Source does not either; Live's do.
- Items indented with tabs are not continued by a paste, as Live's Enter
  does not continue them.
- A pasted first line's own mark still goes after the item's (pasting
  `- a` on an empty `- ` item gives `- - a`), as before.
- A paste at several carets goes in as it is.
