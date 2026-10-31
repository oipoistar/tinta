# Sheets

Validated on Windows on 2026-10-31, on the local `sheets` branch. Sheets are
an infinite canvas of notes (`docs/sheets.md`): a `.smd` file beside the
folder of its notes, with Obsidian's `.canvas` opening the same way.

## Fixtures

- `fixtures/sheets/Launch plan.smd` with its folder `Launch plan/`: eight
  notes mixing headings, tables, lists, task lists, quotes, code, emphasis,
  wiki and Markdown links, front matter and an image; a PNG card, a link
  card and a text card; a group with a colour, three lanes writing
  `status`; six arrows with labels, colours, fixed and floating sides and
  both ends; and one note in the folder that is not on the sheet.
- `fixtures/sheets/vault/`: an Obsidian vault (`.obsidian/`) with a
  `Boards/Board.canvas` whose paths run from the vault root.

## Checks

- `sheet_model` (new): JSON Canvas read and written back unchanged (escapes,
  a surrogate pair, unknown fields and node types, edges to missing nodes,
  duplicate ids, nesting limit); the `.smd` front matter, title, outline and
  block, CRLF and a byte order mark, a block found past other code; reading
  order along arrows with a cycle cut at its top-left card; arrow-key
  neighbours; tidy left to right and top to bottom with a cycle, a group
  kept around its cards, a selection-only tidy; note links, titles, tasks,
  words; front matter writes (added, replaced, inserted, CRLF kept, an
  ambiguous block left alone, after a byte order mark); file names from
  titles; heading shifts; Mermaid flowcharts in and out, subgraphs as
  groups, round trip.
- `sheet_canvas` (new, `input_tests --sheet-tests`) works on a copy of the
  fixture through the real handlers:
  - loading: 14 nodes, 6 arrows, the notes' titles and tasks, the image
    card, three ghost links (none where an arrow already joins), the tray;
  - typing: a double-click on open ground makes a note and opens it, Enter
    continues a list, Esc names the file after its first line, undo removes
    the card and the file it made, redo brings both back; Tab grows a joined
    child to the right; the arrow keys walk back;
  - the pointer: click and Shift+click select, a drag moves a card onto the
    grid and undo puts it back, a drag on a selected card's corner resizes
    it in place and undo restores it, a box from open ground selects the
    cards it touches, a card dropped into a lane gets the lane's
    `status` in its note, Sort into lanes follows a note changed on disk, a
    drag from a side dot joins two cards with a floating arrow, a click on a
    ghost and the menu's Turn links into arrows make arrows, Ctrl+G groups
    and names;
  - a drag on any selected card moves the whole selection, with Shift held
    too (a Shift press used to take a selected card out at once, so the
    rest moved without it); a Shift+click without a drag takes it out;
  - naming (its own sheet, `Naming.smd`): an arrow dropped on open ground
    opens a new note; Enter, `#This is a header`, a paragraph and a
    `### This is a subheader` are typed; the window's timer saves the
    typing and polls the folder; a click on open ground ends it, and the
    note is `This is a header.md` (it used to take the first heading,
    `This is a subheader`). An `Untitled.md` already in the sheet's folder
    names itself from a new first line; `Start.md` keeps its name;
  - the start page (Midnight and Paper, 1050 and 650 px): Open a file and
    Browse over New document and New sheet, two columns of one width;
  - a new tab's empty page (650 and 1050 px): New sheet under Open a file in
    the narrow pane, beside it in the wide one; the pointer lights it alone;
    a sheet made from it takes the empty tab's place;
  - the test window passes `WM_CAPTURECHANGED` to the sheet as Tinta's
    window does, and every drag checks that the press holds the mouse.
    Testing the build showed moved cards snapping back and new arrows
    vanishing: the release let go of the mouse before the sheet had taken
    its gesture, and the capture change in between counted as a lost drag.
    With the old order seven of these checks fail (move, resize, box,
    lane drop, arrow).
  - commands: find matches titles and note text and steps through them,
    P presents and steps, S spotlights, Copy as Mermaid, a pasted flowchart
    becomes three notes and two arrows, Ctrl+L tidies without overlaps,
    compiling writes and opens one document with sections, shifted
    headings and working image links;
  - touch: a finger pans both ways, a double tap makes a note;
  - saving: the `.smd` on disk holds every card and arrow and reads back;
    the Obsidian canvas saves back as JSON Canvas with the change; a sheet
    whose block does not parse is read-only and never written; a new sheet
    beside a folder of notes places them;
  - a sheet of 400 linked notes: open, ghost links and frame times printed,
    on the software and the hardware render target.
- The frames it draws (`build/sheet-test-Release/frames/`, ignored) were
  looked at: the whole sheet, close up and far out in Paper and Midnight at
  1050 and 650 px; typing in a card; the menu; find; presenting; spotlight;
  the help overlay's Sheets section; the Obsidian canvas; the start page
  and a new tab's empty page. They led to fixes: characters the sources
  need escaped, the tool bar running into the zoom control at 650 px, the
  minimap crowding small windows, arrow labels cluttering far out, blank
  link cards far out, selection chrome while presenting, and the empty
  page's hint running out of a narrow preview pane (it wraps now).
- `i18n`: the 100 new interface strings have their Chinese, Japanese,
  Korean, German, French, Italian and Russian text, counts with their
  plural forms.
- The full suite passed, 56 of 56.

## The real app

From an isolated portable copy (`out/sheets-playground/runner/`):

- `tinta.exe "Launch plan.smd"` opened the sheet in its tab, ran, and
  closed with exit code 0; it reopened at the view it closed on
  (`sheets.ini`).
- The window, captured with PrintWindow (`out/sheets-shots/`), draws the
  sheet as the frames do.
- Driven through its message loop with posted messages: a double-click on
  open ground opened a new card at 100% for typing, the posted key-downs
  typed `real window note`, Esc wrote `Launch plan/real window note.md`
  and the sheet saved the card. An earlier run that missed the double-click
  typed into the sheet instead: `E` with nothing selected opens the sheet's
  own text in the editor, as E does for any document.
- After the capture fix, posted drags in a window on a fresh copy of the
  fixture (`out/drag-check/`): Budget dragged by its middle, an arrow from
  Kickoff's side dot onto Design, Research resized by its corner, a box over
  Kickoff and Research and then `3`. The `.smd` saved on close held all
  four: Budget at 1040,300, the new arrow, Research 440 x 480 at the same
  place, both boxed cards in colour 3. That run's PrintWindow capture came
  back blank, so the saved file was the check.
- The next build, the same way: a double-click on open ground, Enter,
  `#this is a header` (the # posted as a character), a paragraph and
  `### this is a subheader`, the window's timer, a click on open ground;
  the note became `this is a header.md` (the build before named it after
  the subheading). A box over Kickoff and Research, then Kickoff dragged by
  its middle: both saved 200,300 further on, Budget where it was. Shift
  cannot be posted to a window, so the Shift+drag is the harness's check.
  Three earlier runs of the build before, with arrows dropped on open
  ground and double-clicks, all renamed their notes: what kept the
  reported card Untitled was not found, so naming no longer hangs on a
  card's runtime flag.

## Performance and size

- 400 linked notes: open about 0.2 s, ghost links 1.4 ms, a whole-sheet
  frame about 20 ms on the hardware target (30 ms on the software one), a
  close-up frame about 13 ms once the notes are drawn.
- Panning makes no new geometry or text layouts: curves are built in sheet
  units and drawn through the view's transform, arrowheads share one
  triangle, and titles keep their layouts on their cards. On a sheet with
  more than 60 ghost links only those of the card under the pointer or
  selected are drawn.
- `tinta.exe` grows from 3.12 MB to 3.57 MB (local Release builds of
  `master` and this branch); the sheet sources are compiled for small code.

## Limitations

- No web page embeds or video players, by design; PDFs and other files are
  cards that open in their own app. No freehand drawing, no image export.
- A card's text is edited as Markdown with light styling; the IME's
  candidate window is not placed at a card's caret.
- Touch has no pinch zoom on a screen yet; moving cards needs a mouse or a
  pen.
- High DPI was drawn at 150% only (`fit-midnight-150pct.png`), not at
  125% or 200%.
