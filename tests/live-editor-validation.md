# Live

Validated on Windows on 2026-10-31, on the local `sheets` branch. Live
(`docs/live-editor.md`) is the second way to edit a document: the page as
it reads, typed in, with the Markdown shown only in the blocks being
typed in. The rail's pill and Ctrl+/ switch between it and Source. Sheet
cards are typed in with the same engine (`include/live_text.h`).

## Fixtures

- `fixtures/live/Live tour.md`: front matter; a title; a paragraph with
  bold, italic, inline code, strikethrough, a highlight, a link, a wiki
  link and a footnote; bulleted, nested, numbered and task lists; a quote
  that wraps and a NOTE callout; a C++ fence; a table; a Mermaid
  flowchart; inline and block maths; a picture beside it (`mock.png`); a
  divider; the footnote's text.
- `fixtures/live/Plans.md` and `Other note.md`: notes beside it, for the
  `[[` names.
- The test also writes `Big.md` (247 KB: 1,500 sections with a paragraph
  of bold, emphasis and a link, a list, and a fence every tenth) for the
  timings.

## Checks

- `live_editor` (new, `input_tests --live-tests`) works on a copy of the
  fixture through the real key, character and pointer handlers:
  - opening: Live on, the page beside the rail with no source pane, the
    layout recording its blocks, the caret's block placed;
  - typing: a heading opens with its `##` as typed; typing goes into the
    source and Ctrl+Z takes it back a word at a time, as Source does;
    Enter continues a list and ends it on an empty item; a numbered item
    counts on and the numbers below follow; Tab nests an item under the
    one above and Shift+Tab takes it out; Backspace at an item's text
    takes its whole mark and undo puts it back;
  - formatting: a double-click selects a word and the format bar stands
    over it; Ctrl+B makes it bold and again plain; `*` over a selection
    wraps it; Ctrl+K links it;
  - the slash menu: `/` at a line's start opens it, `/tab` and Enter make
    a table with its first cell selected, typing and Tab fill cells,
    `/flow` makes a flowchart, `/note` a callout to type in, `/h2` a
    heading and `/task` a task that take the typing after their marks,
    and Esc closes the menu keeping the typing;
  - `[[Pl` lists `Plans` from the document's folder and Enter writes
    `[[Plans]]`;
  - the pointer: a click on a rendered word puts the caret in it, a click
    on a task box ticks it and leaves the caret, Up and Down walk from
    block to block, a right-click opens Live's menu (not the source's) with
    the caret at the click, a caret moved elsewhere (find, go to line) is
    brought into view;
  - switching: Ctrl+/ shows Source with the caret on the same character
    and saves the choice, Ctrl+/ shows Live again, the pill's Source side
    switches back;
  - drawings: an open diagram keeps its drawing under its Markdown (also
    a formula and a picture, in the frames), the wheel scrolls the page,
    and a Mermaid file opens in Source while the tour opens in Live again;
  - the find bar keeps what is typed into it while Live shows (the page
    took the characters before the guard);
  - a new note opens in Live on its empty page, keeps the Open button,
    and `# Fresh`, Enter and a line make the note;
  - two themes (Paper, Midnight) at 1050 and 650 px;
  - the long document: switching to Live, a key, a frame and leaving the
    block typed in are timed, and a key and a frame must stay under 40 ms.
- `sheet_canvas` now types cards with the shared engine; its checks pass
  unchanged (live writing, `[[` names with Ctrl+Enter drawing the arrow,
  naming, undo), and new ones check that `/h2` offers Heading 2 first in a
  card, that Enter makes the line a heading to type in, that a selection
  shows the format bar and that Ctrl+B in a card makes the heading bold
  and the note holds it.
- `i18n`: the 35 new strings have their Chinese, Japanese, Korean,
  German, French, Italian and Russian text.
- The frames (`build/live-test-Release/frames/`, ignored) were looked at:
  the page with the front matter open, an open heading, an open list, the
  format bar, the slash menu before and after narrowing, a table being
  typed in, inserted blocks, the `[[` names, both themes at both widths,
  and the rail's pill. They led to these fixes:
  - text typed between blocks drew only the caret's line, so the live
    stretch now holds every change since the page was laid out;
  - an inserted heading's or list's mark went after the typing, so a
    line's mark now keeps the caret after it;
  - list items opened a few pixels left of the page's, so a list mark
    now takes the page's indent;
  - table pipes are muted;
  - a picture's block took the divider after it, so a divider with no
    text of its own is now a block of its own;
  - an open code block lost its colours, so its lines take the page's
    token colours (`live-code.png`), in cards too;
  - `## Next` after a space was not drawn as a heading in a card, and a
    heading's marks may now follow up to three spaces;
  - Live's empty page drew the source's hint about typing "on the left"
    under the Open and New sheet buttons; it now has only its own prompt
    (`live-empty.png`, `live-new-note.png`);
  - the test's frames drew the strip over the rail.

## Review

A review of the code after the first version found these, all fixed;
`live_editor` checks each on a fixture of its own (`Fixes.md`: a table with
no rows, a list, a footnote defined mid-document, a code fence):

- Ctrl+Z in Live left a stale selection: the page now takes the caret
  back from the source's undo.
- Enter at a table's header, and Tab past its last cell with no rows
  below, put the new row above the delimiter row.
- Down on a line after the last block, or Up on one before the first,
  jumped back into that block.
- Alt+arrows moved lines while the find bar or a dialog had the keys.
- A drag whose mouse went elsewhere (Alt+Tab) kept selecting; on a card the
  format bar stayed hidden after it.
- A Mermaid or plain-text file lost the source's preview chrome while Live
  was the remembered mode, and Ctrl+/ there set a mode it could not show.
- Backspace, Delete and Tab treated code lines starting `#`, `-`, `>` or
  `|` as Markdown.
- The popup went back to its first row on every key.
- Footnotes defined mid-document opened with the last paragraph.
- The rail's quote button took a list item's bullet instead of quoting it.
- A task box's `[` was read as a link's; with the preview and the word
  count off, Live never caught up after a pause; the popup's and the
  bar's clamps could invert in a very narrow window.

## The real app

The build, run from an isolated portable copy (`out/live-playground/`,
with `openInTabs=0` so it never joins another Tinta window and
`editorLive=1`), was driven through its message loop with posted messages
and captured with PrintWindow (`out/live-playground/shots/`):

- `tinta.exe "Live tour.md"`, then E: the editor opened in Live, the pill
  showing Live, the front matter open at the caret and the rest of the page
  as the reading view draws it;
- a click on the first paragraph opened it; End and typed characters
  added ` Typed in a real window.`; after a pause the paragraph drew
  again as the page;
- Enter twice, then `/tab`: the slash menu narrowed to Table; Enter, `Left`,
  Tab, `Right` filled the header;
- three Downs left the table for `## Lists`, which opened with its marks,
  and the table drew as a table. A first run stopped at the table's last
  row: the stretch's spacing below its text caught the move. Up and Down
  now go on past it, and `live_editor` checks it from a list into the
  heading after it and back;
- closing the window asked about the changes and Y saved them; the file
  held the typing and the table as made.

## The menu

Reported after the first version: Live's right-click menu made no sense and
did nothing. It was Source's insert menu. Its rows showed Markdown, its
Cut and Copy followed the source's state, and the page took every move and
click over it: no row lit, a click put the caret under the menu, and the
menu stayed open. The rail's table and diagram flyouts in Live had the same
fault. Reproduced in the built app before the fix (`shots-menu/menu-1-open`
to `menu-4-click`, since overwritten).

Live now has its own menu, in the shared engine (`src/live_menu.cpp`), so
sheet cards have it too (`docs/live-editor.md`, "The menu"). While it is
open it takes the window's presses, moves and keys before anything else.

- `fixtures/live/Menu tour.md`: a title; a paragraph with a link that has
  a title, a `[[note]]` and an address written out; a list with a nested
  item and a task; a table with centred and right-aligned columns; a
  JavaScript fence; a quote; a picture; a last line.
- `live_editor` checks, through the window's own handlers:
  - a paragraph's menu: the formats first, Cut waiting for a selection,
    Paste, Turn into, Insert, Select all, and no link or table rows; the
    row under the pointer lights; B lights with its name and key, makes
    the word under the pointer bold and puts the menu away;
  - Turn into opens beside its row with Text ticked; crossing Insert's row
    on the way keeps it open, resting on Insert opens Insert's; Heading 2
    makes the paragraph a heading;
  - Insert, Table: the grid takes its size from the pointer (4 x 2) and
    makes that table after the line;
  - a link: Open link, Copy link address (the address without its title,
    on the clipboard), Edit link (the address selected), Remove link (its
    words stay); a `[[note]]`: Open note and Remove link, no address;
  - a task ticks; a top-level item cannot go out; a nested one does;
  - a table: rows below, a column right in every row (the delimiter row
    gets `---`), a column deleted, the last row deleted; the header has no
    row above and stays; Align, Center centres its column;
  - code, opened at the caret: Copy code copies the fence's lines, and
    there are no formats, Turn into or Insert;
  - the keys: Shift+F10 and the menu key (its press, and the window
    request) open it under the caret with a row lit; End, Up, Right, Up,
    Up, Right, Down and Enter make a sequence diagram; Up from the
    clipboard reaches the formats, Right goes along them; a letter lights
    its row; Esc and the menu key again put it away;
  - a click off the menu puts it away and leaves the caret;
  - the rail's table and diagram buttons open the grid and the diagrams
    beside the rail and make a table and a pie chart at the caret; the
    rail's export flyout (Source's) lights its rows over the page;
  - both themes at 1050 and 650 px with Turn into open, every panel inside
    the window; every pick undone leaves the tour as it was.
- The engine's edits are checked on texts of their own: a link's words
  and address without its title, a picture's, a note's name and shown
  words, an address without the full stop after it, no link in a task box;
  a column into a short row and out with one undo, the only row deleted
  with the caret going to the header, a row from the header going under
  the delimiter row, an alignment set and taken off; numbers counting at
  each indent, Text taking a quote and a heading off, code getting
  fences, code lines staying code, tasks keeping their ticks; Open link and
  Open note handing the host the address or the note's name; Insert's Link
  over words; a divider asked for in a table going under it; no Open for a
  host that cannot follow links; the formats faint over lines.
- `sheet_canvas`: a right-click in a card being typed in opens the text's
  menu and keeps the card open; Turn into, Heading 3 makes the card's line
  a heading and undo takes it back; the menu key opens it and Esc closes
  it with the card still open.
- `i18n`: 29 new strings (28 for the menu, and the help row for Shift+F10)
  in all eight languages.
- The frames (`menu-*.png`, `ten-text-menu.png`) were looked at: the
  paragraph's menu, B's name and key, Turn into with Text ticked, the
  Insert submenu with the grid at 4 x 2, a link's, a table's and the
  align submenu, the keys' cascade to the diagrams, the rail's grid, and
  the menu flipping above the pointer near the window's foot and to the
  left at its right edge, on a card too.

In the built app (`out/live-playground/`, posted messages, PrintWindow):
a right-click on the first paragraph opened the menu there; resting on
Turn into opened it with Text ticked; Heading 2 lit under the pointer and a
click made the paragraph a heading. The menu key's press, posted, opened
nothing at first: Windows did not turn the posted release into a menu
request. The window now takes the menu key on its press, as it does
Shift+F10, and drops its release. Then the menu key opened it under the
caret, and End, Up and three Rights led to Insert's table grid at 4 x 3.
Enter made the table with its first header cell selected. Closing
discarded the changes, and the file was as it was.

## Performance and size

- 247 KB document: switching to Live 356 ms (parsing and laying out the
  page in view), a key 0.66 ms, a frame 2.3 ms, leaving the block typed
  in 186 ms (the page parsed and laid out again, as Source's preview
  does after a pause).
- `tinta.exe` grows by 141 KB (3,691,520 to 3,835,904 bytes) for Live,
  the engine, the strings in eight languages and the shared drawing
  helpers; the sheets' own text box code went into the engine.
- The menu adds 52 KB (to 3,889,152 bytes): about 24 KB for the menu
  itself, the engine's table, link and Turn into edits, and the strings.
  Its rows were first built inline at each call, 8.7 KB more; one
  out-of-line call per row took that back.

## Limitations

- While a block is open its pictures and diagrams show as Markdown.
- Tables are typed as Markdown; the reading view's cell editor is Source's.
- One caret; no heading shortcuts on Ctrl+digits, which switch tabs.
- The IME's candidate window follows the Live caret, but was not tried
  with a real IME.
