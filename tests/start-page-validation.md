# Starting 4.0.0: the last session, the start page and the sheet tour

Validated on Windows with the Release build on 2026-11-01, on master after
the list marks (4.0.0 development).

## Starting as before

A plain launch still reopens the last session's tabs, as 3.9.6 does: "Open
files in tabs" is on by default, and the tabs come back in their order with
the one that was active in front. With no session to restore (a new
machine), the window shows the start page, as it has since 3.3.0.

Checked with an isolated portable copy (`out/fresh-start/`, ignored), its
settings.ini empty but for `[Settings]`, started with no file:

- First run: Windows' file association question (answered No in the copy's
  own settings), then the start page with an empty Recent.
- With a 3.9.6-style session written into its settings (`Alpha.md`,
  `Beta.md` active, `Launch plan.smd`), the next plain launch reopened the
  three tabs in order, Beta in front and the sheet a tab like the others;
  on closing, the same session was written back.

A sheet at startup comes from a session that ended with a sheet open. The
Store app keeps its settings in its package
(`%LOCALAPPDATA%\Packages\oipoi.TintaMarkdownViewer_*\LocalCache\Roaming\Tinta`),
while a build run from a folder reads `%APPDATA%\Tinta`, so a local build
does not see the Store app's session; on this machine that one held a sheet
from the sheets playground. 4.0.0 from the Store reads the Store's settings
and resumes its session.

## The start page

- **New sheet** shows its key, **S**, and S on the start page makes a sheet
  as the button does (the Save dialog names it). The key is spent, so no S
  reaches the new sheet. In the empty page of a new note the button keeps
  no key: letters type there.
- **Learn** starts level with the top of the page, its heading beside the
  icon, instead of 118 DIP down; with an empty Recent the two columns end
  about level.
- **Sheet tour**, a fourth Learn card ("Notes on an endless canvas, joined
  by arrows.", in the eight languages), opens a small sheet: eight notes
  (the moves, lanes with `status:` notes in them, presenting and
  compiling), a text card, five arrows and a ghost link. It is written to
  `%TEMP%\Tinta\Sheet tour.smd` and its folder each time, through Tinta's
  own .smd writer, fitted to the window, and opened in the start page's
  place. A tour that is open already is shown as it is; notes added to the
  tour's folder are never removed.

## Automated checks

`sheet_canvas`, drawn in Midnight and Paper at 1050 and 650 px:

- Learn has four cards, stacked, the tour last, and starts above the
  action buttons (at 1050; at 650 the page is one column, as before).
- Ctrl+T, then S: the real Save dialog opens (driven by the test's hook),
  names `Pressed S.smd`, and the empty sheet opens with nothing typed in it.
- With TMP and TEMP pointed at the test's folder, the tour's card opens it
  in the start page's place: 8 notes on disk, 3 lanes whose notes say their
  lane, a text card, 5 arrows and nothing in the tray. With the tour open,
  a second click goes to it and leaves a changed note alone; closed and
  opened again, the note is back and the view fitted again.

The Save dialog helper moved from `deleted_tab` into `tests/save_picker.h`
for both tests. Neutering: with S's key, Learn's old offset and three cards
put back, exactly the 9 new checks fail.

## In the built app

The isolated copy, driven with posted messages and captured with
PrintWindow (`out/fresh-start/shots/`, ignored), in Midnight at 1280 x 900:
the start page with S on New sheet, Learn level with the logo and four
cards; a click on Sheet tour opened the tour in the launcher's tab, every
card showing its whole note; written again, it opened fitted.

## Size

`tinta.exe` grows by 9 KB (3,925,504 to 3,934,208 bytes), most of it the
tour's notes.

## Limitations

- The tour is in English, as the other Learn documents are.
- Windows may empty the temp folder; the next click writes the tour again.
- S is the start page's key only; elsewhere New sheet is the start page or
  the empty page of a new tab.
