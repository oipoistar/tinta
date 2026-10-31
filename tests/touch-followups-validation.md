# Touch follow-ups from SHM-white (#295 to #303)

Validated on Windows with the Release build on 2026-11-01, on the nine pull
requests merged onto master after sheets and Live (4.0.0 development).

## What landed

Nine pull requests from SHM-white, each one commit on 3.9.6 with its own
checks, merged as they were (merge commits resolve the conflicts with sheets,
Live and each other):

- #295: in the source editor, a held finger selects the word, extends by
  whole words either way as it slides, and the editor's menu on the lift
  keeps that selection.
- #296: touches on the editor's menu where it hangs over the preview, on the
  stacked Replace buttons of a narrow window, and on a side panel's resize
  edge go to that control instead of panning what is under it.
- #297: a finger that catches a flicked page only stops it: no link
  followed, no row opened, no selection, even if it is held.
- #298: a pan, a canceled contact or a second finger starts a new tap
  chain; a quick tap on the next list row opens that row; a hold or a fling
  stops when the window loses focus or the tab, file or editing mode
  changes.
- #299: a link to a heading in the split preview moves the source pane too,
  so the next frame's sync no longer pulls the preview back.
- #300: **Exit editing** in the application menu while editing, through the
  same exit as Esc (unsaved changes still ask).
- #301: a finger held in the split preview selects the rendered words and
  opens Copy / Select all where it lifts; held on a table cell, it selects
  instead of opening the cell.
- #302: Help scrolls and flings under a finger, and a tap (or a click)
  beside its panel closes it without reaching the document.
- #303: a table's Fit and Copy TSV take a press before the cell under them,
  and draw above a cell being edited.

Four follow-up commits keep what sheets and Live depend on:

- #298 had a finger's second tap snap onto its first on text only. A sheet
  counts a double-click when its two clicks land within 4 px, which a finger
  seldom manages, so a double tap on open ground stopped making a note. Only
  list rows keep their taps apart now.
- #302 armed a hold only on text, so a held finger on a sheet no longer
  opened the sheet's menu, the only way to it on a touchscreen
  (docs/sheets.md). Holds arm everywhere but on a list's row and in Help.
- #301's hold in the split preview also took Live's page, which is the
  document viewport too: it made a selection of the rendered page Live never
  shows and left Live's caret where it was. It applies outside Live only.
- #300's label had English and Chinese words only. It has all eight
  languages, and the i18n test, which walked the Russian table as the list
  of keys and so never saw a key missing from it, first requires every
  compiled key there (`translationKeyCount`, `translationKeyAt`).

## Automated checks

- Each pull request's own checks, put over master's source without its fix,
  fail on exactly what it reports (#295, #296, #297, #298, #299, #301,
  #303); #300's and #302's need their own code to build. With the fixes they
  pass.
- New: `sheet_canvas` double-taps open ground with the second tap 7 and 6 px
  off the first and finds a note, and holds a finger on open ground and
  finds the sheet's menu (Esc puts it away); `live_editor` holds a finger on
  a word in Live and finds Live's caret in the word, no selection of the
  rendered page, and Live's menu on the lift; `i18n` requires every compiled
  key in the Russian table.
- Neutering: with `touch.cpp` and the Russian table as merged, before the
  four fixes, exactly those six new checks fail and the rest pass.
- The full suite passes 58 of 58 on the merged branch.

## In the built app

The build ran from an isolated portable copy (`out/touch-prs-playground/`,
ignored), driven with posted messages through its message loop and captured
with PrintWindow (`out/touch-prs-playground/shots/`), on
`tests/fixtures/touch-gestures-286.md` in Source with the preview:

- Midnight at 1280 px: a click on *jump to the end* in the preview took the
  preview to the end and the source pane with it, and both stayed there
  through the following frames (#299).
- Over the wide table the Fit and Copy TSV buttons show at its top right.
  A click on the lower half of the header cell under Fit opened that cell's
  editor, and both buttons stayed drawn above it; a click on Fit then fitted
  the table and closed the editor, the source line unchanged (#303). The
  same in Paper at 860 px, where the buttons sit over Bed 5 to Bed 7.
- The application menu, opened from the logo while editing, lists Exit
  editing in a group of its own below Save As; choosing it returned the
  unchanged document to reading in the same tab, with no prompt (#300).
- `?` opened Help; a click on the page's scrollbar track beside the panel
  closed Help and the page did not move (#302).
- Each run closed without a prompt, and the file was byte for byte as
  before.

Copy TSV was not clicked in the app, so as not to write over the clipboard;
`table_and_modifier_input` covers it with the clipboard kept.

## Size

`tinta.exe` grows by 6 KB (3,911,168 to 3,917,312 bytes).

## Limitations

- Touch itself went through the native harness (`touchDown`, `touchMove`,
  `touchHold`, `touchUp` on the real handlers), not injected into a window
  as in tests/touch-gestures-validation.md, and not on a touchscreen here;
  SHM-white tried the pull requests on theirs.
- In Live a held finger puts the caret where it rests and opens Live's menu,
  but selects no word and does not extend as the finger slides: the hold's
  press reaches Live as a single click, and releasing the press's capture
  ends Live's drag. A card being typed in on a sheet is likely the same (not
  checked).
- Live's menu and a sheet card's text menu are not among the overlays a
  finger leaves to the mouse, so a pan that starts on one scrolls the page
  under it; taps on them work.
- While a header cell under Fit or Copy TSV is being edited, the buttons
  (translucent) sit over the start of its text.
- `live_editor` failed about one run in twenty here, on master as well, for
  a reason found separately: Live compared parse roots by address, and a
  newly opened document's root can land where the last one was (about one
  open in 150). Test suites running side by side share the clipboard and
  the CPU, so their clipboard and timing checks can fail each other's.
