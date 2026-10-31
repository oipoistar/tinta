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
    block to block, a right-click opens the insert menu with the caret at
    the click, a caret moved elsewhere (find, go to line) is brought into
    view;
  - switching: Ctrl+/ shows Source with the caret on the same character
    and saves the choice, Ctrl+/ shows Live again, the pill's Source side
    switches back;
  - drawings: an open diagram keeps its drawing under its Markdown (also
    a formula and a picture, in the frames), the wheel scrolls the page,
    and a Mermaid file opens in Source while the tour opens in Live again;
  - the find bar keeps what is typed into it while Live shows (the page
    took the characters before the guard);
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
  - the test's frames drew the strip over the rail.

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

## Performance and size

- 247 KB document: switching to Live 356 ms (parsing and laying out the
  page in view), a key 0.66 ms, a frame 2.3 ms, leaving the block typed
  in 186 ms (the page parsed and laid out again, as Source's preview
  does after a pause).
- `tinta.exe` grows by 133 KB (3,691,520 to 3,827,200 bytes) for Live,
  the engine, the strings in eight languages and the shared drawing
  helpers; the sheets' own text box code went into the engine.

## Limitations

- While a block is open its pictures and diagrams show as Markdown.
- Tables are typed as Markdown; the reading view's cell editor is Source's.
- One caret; no heading shortcuts on Ctrl+digits, which switch tabs.
- The IME's candidate window follows the Live caret, but was not tried
  with a real IME.
