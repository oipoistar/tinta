# Citation completion and the Cite button

Validated on Windows with the Release build on 2026-11-04. The design is in
`docs/specs/scholar.md` (phase 2, "Source and Live"); the guide is
`docs/citations.md`.

## What was asked

Scholar's writing aids: typing `[@`, or an `@` that starts a word, lists
the bibliography's keys in Live and in Source, filtered as one types, and
`@fig:` and its kin list the document's own ids; a Cite button on the edit
rail asks Zotero's picker through Better BibTeX, or types `[@` and opens
the list without it.

## The change

- `scholar::suggest` (`src/scholar.cpp`) gives the rows for the key typed
  after an `@`. After `fig:`, `tbl:`, `eq:` or `sec:` they are the
  document's ids, in order, found in `{#fig:id}` attribute blocks (a
  Mermaid fence's `{#fig:id caption="..."}` too) and `\label{eq:id}`, each
  with the image's words, the caption, the heading or the TeX beside it;
  `@Fig:` keeps its capital. Otherwise they are the keys of the
  bibliography the properties name, read through `scholar::begin` and
  kept for two seconds while the properties and folder stay the same. Each
  row reads "key  Author Year, Title": the first author's family name with
  its particles ("van Rossum", "de la Fuente"), both of two ("Lindqvist &
  Okafor"), "et al." for three or more or `and others`, the issued year
  and the title cut at a word near 40 characters. Typing matches, best
  first: the start of a key, the start of an author's family name (with
  or without its particle), a name and a year run together (`knuth1984`),
  the start of the year, inside the key, the start of a title's word; ties
  go by key. Case and Latin accents are ignored (`muller` finds Müller,
  `lopez` López), and a CJK name is found by its characters.
- `live::citeAt` decides where a key is being typed: an `@` at a line's
  start or after a space, `[` or `;` (`[-@` too), the key's first letter a
  letter, digit or `_`, not after a letter (`ana@example.com`), not in
  inline code or a fenced block. `live::citeEdit` makes the pick: the key
  typed, and the rest of it after the caret, become the key, and a `]`
  follows when the `@` is inside a `[` that nothing closes yet on the line.
- Live: the popup has a fourth kind, `Cite`, with its own drawing: keys in
  the editor's monospaced face, the works beside them in a column, the list
  as wide as its rows need (240 to 460 px, never past the window). Typing
  opens it; moving the caret along the key keeps it and leaving the key
  closes it, but moving onto a key opens nothing. Esc closes it and typing
  on keeps it closed until the `@` is left. Up/Down, Enter, Tab and the
  mouse are the popup's own.
- Source: the same list, kept in a `LiveText` for its popup state and drawn
  by `live::drawPopup` under the `@`'s line (above it near the window's
  foot), over the page beside the source when it must. Its keys come
  before the editor's in `handleKeyDown`, so Enter puts a key in and no
  new line follows, and Esc does not count towards leaving the editor. A
  press on a row picks it, a press elsewhere closes the list.
- The rail's Cite button (**@**, after the picture, "Insert citation"):
  `src/zotero.cpp` probes `/better-bibtex/cayw?probe=true` on
  127.0.0.1:23119 and, if it answers `ready`, asks
  `/better-bibtex/cayw?format=pandoc&brackets=1`, on a thread of its own
  with WinHTTP; the answer comes back as `WM_APP_ZOTERO`. A pick goes in at
  the caret of the document that asked, or at its parked caret in its tab
  if another tab came to the front; a cancelled pick changes nothing; a
  failed one says "Zotero's citation picker did not answer". Without
  Zotero the button types `[@` and opens the list, and with nothing to
  list says to start Zotero or name a bibliography. Windows tries a
  refused connection again for two seconds, past WinHTTP's timeouts, so a
  non-blocking connection of its own answers the probe within 300 ms first.
- The tooltip and both notices are in all eight languages. `docs/citations.md`
  (Citing as you type, Zotero), `docs/live-editor.md`, the README and the
  spec's status describe it. No shortcut was added.

## Fixtures

`fixtures/scholar/completion.md`: a paper citing `references.bib` (30
works: articles, books, chapters, theses, a 22-author article, Chinese
entries) with headings carrying `{#sec:...}` ids, two figures, a captioned
table with citations in its cells, an equation with `{#eq:flow}` and one
with `\tag{2} \label{eq:rise}`, a list with a citation, an e-mail address
and a link, a quote with a locator and a Python block with `[@` in a
string. `fixtures/scholar/completion-plain.md`: a note with no
bibliography, `@mentions`, an address, a figure and a table with ids.

## Automated checks

`citation_completion` (`tests/citation_completion_tests.cpp`, input_tests
`--citation-completion-tests`, 165 checks, no clipboard): where an `@`
starts a key and where it does not (an address, a word, punctuation,
inline code, a fence, a space, a key starting with `-`); picks closing a
bracket, not doubling a `]`, replacing the rest of a key, a second key in
brackets; the rows for the fixture: all thirty in key order, Knuth's row
reading "Knuth 1984, Literate Programming", keys before co-authors for
`okafor`, `Muller` and `lopez` without accents, a year, a name and year
run together, a title's word, a particle, a Chinese name, "et al." and a
shortened title, two authors named both; `@fig:`, `@Fig:`, `@tbl:`,
`@eq:` (the `\label` too) and `@sec:` with their captions, an id starting
with what was typed before one holding it; nothing from `@` without a
bibliography but the figures still. Then, typed through
`tests/keystroke.h` in Live and in Source each: `[@` opens thirty keys,
`knu` narrows to one, Enter writes `[@knuth1984]` with no new line; `@oka`
lists keys then co-authors; Esc closes and leaves edit mode alone, and
typing on keeps it closed; `ana@ex` and `x@` open nothing; `[@2017` finds
Vaswani and Tab writes it; Down and Enter take `fig:bars`; a click on a row
writes `[@smith2020]`; Home closes it and End does not reopen it; nothing
in the code block; the list stays inside the window at 650 px. Without a
bibliography, `@oct` and `[@` open nothing and `@tbl:` lists the note's
table. The Cite button against a small HTTP server on 127.0.0.1 with an
ephemeral port: the probe answers ready; a pick goes in at the caret in
Source and in Live; a cancelled pick changes nothing; a pick held back
while a new note comes to the front goes into the asking tab's parked
text at its caret, dirty, and not into the note; a failed picker leaves
the text and shows its notice; with nothing listening the probe gives up
in about 300 ms and the button types `[@` with the list open, in both
editors; without a bibliography as well, `[@` alone and the notice.

`editor_features` now checks that the Cite button comes after the picture
button and, like the others, stays above Pandoc's export button in 900,
600 and 540 px windows. `input_shortcuts` (no direct `handleCharInput`
calls), `i18n` (coverage), `russian_interface`, `scholar`,
`scholar_in_app`, `wrapped_cursor_navigation`, `document_types` and
`word_count` pass.

## In the built app

The test saves frames of the real drawing code
(`build/citation-completion-test-Release/frames/`), and they were
inspected: the list open under `[@` in Live and above it in Source near
the window's foot; Okafor's works by author; the two figures with their
captions in a list narrowed to them; the list at 650 px in Paper and
Midnight, kept inside the window; the Cite button's `[@` with the list in
both editors. The rows read well: CJK titles and names through the UI
font's fallback, long titles ending in an ellipsis at a word, the
scrollbar thumb on a long list. The rail with the Cite button was looked
at in 900, 600 and 540 px windows, with and without the export button.

A copy of the built `tinta.exe` was also driven in a real window (an
isolated folder under `out/cite-check`, keys posted to the window so they
pass through its message loop, the screen copied for shots): in Source and
in Live, `See [@` opened the list, `knu` narrowed it, Enter wrote
`[@knuth1984]`, ` @fig:` listed the figures, Down and Enter wrote
`@fig:bars`, and the saved file ended `See [@knuth1984] @fig:bars`. The
Cite button with a stand-in for Better BibTeX listening on 127.0.0.1:23119
received the probe and the picker's request and wrote `[@doe2024;
@smith2020]` at the caret through the window's `WM_APP_ZOTERO`; with
nothing on that port it wrote `[@` and opened the list over the source
and the page.

## Size

`tinta.exe` grows by 38,912 bytes (4,708,352 to 4,747,264): about 17 KB the
matching, the rows and the document's ids (`scholar.cpp`, built for size),
7 KB the two editors' lists and the button, 4.5 KB the Zotero client and
its probe, the rest strings and unwind tables.

## Limitations

- Zotero itself is not installed here: the client was checked against a
  stand-in that answers as Better BibTeX's documented Cite As You Write
  endpoint does.
- The reader shows a heading's `{#sec:id}` as text: section numbers and
  `@sec:` references are a later part of phase 2. The list offers the ids
  already, as pandoc-crossref reads them.
- A space ends a key, so a query is one word; matching is by starts and by
  the key's inside, not tolerant of typos.
- One picker at a time: a click while Zotero's picker is open does nothing.
  A pick for a document that has left the editor since is dropped.
- The library is read again at most every two seconds while typing, so a
  file Zotero has just written shows in the list within two seconds.
- Frames were drawn at 96 DPI; the real window ran at the machine's scale.
