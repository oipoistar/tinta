# Search results panel (#246)

Validated on Windows with the Release build on 2026-10-09. Suggested by Alex
(@Lex987) in #246, whose pull request added a results dock for Ctrl+Shift+F.

## Problem

Find (F or Ctrl+F) showed one match at a time ("3 of 50") plus ticks on the
scrollbar, with no overview of where the matches are.

Sibling files that matched floated in a box over the page. The box and its
toggle were still drawn where the search bar sat before the title-bar tab
strip, so the toggle landed in the caption. A click on a file replaced the open
document instead of opening the file beside it, which is the bug Alex
described.

## Change

**Opening the panel.** In the reader, Ctrl+Shift+F, or a new list button at
the end of the search bar, opens a Results panel in the Contents slot. It uses
the same side and width as Contents. Contents hands over its place without a
slide and comes back when the panel closes. Esc closes the bar and the panel
together.

**The document's matches.** The panel lists the document first, with its title
and match count. Its matches follow, grouped under the heading each falls in.
Each match is a snippet of its line, with the match in semibold over an accent
wash. The current match is marked like the current heading in Contents and
stays in view while Enter steps through. A row jumps to its match, and a
heading row jumps to its first match.

**Other files.** After the document come the other open tabs, then the files
of the document's folder in name order. A tab being edited is searched in its
unsaved buffer. The folder scan looks at up to 400 files of at most 1 MB and
lists up to 50. Neither the open document nor a file already listed as a tab
is repeated. Each file shows its count and its first three matches by source
line, then "+N more".

A click opens the file in its own tab, or switches to it if it is already
open. It then lands on the first rendered match at or after the clicked line,
found through the document's scroll anchors.

**Scope.** Two rows at the top switch the open tabs and the folder on and off,
and the choice is saved. The tabs row is the new `tabSearchEnabled` setting;
the folder row uses the existing `folderSearchEnabled`, which also keeps its
Settings toggle.

**Scanning.** The other files are scanned on a worker thread, 250 ms after
typing stops, and only while the panel is open. Hits for an earlier query clear
as soon as the query changes.

**Other changes.**

- An open search now follows tab switches and file reloads. Before, it kept
  the old document's matches, so the bar said "No matches" or pointed at
  stale text.
- The search bar centers over the page between any side panels instead of
  over the whole window.
- Ctrl+Shift+F does nothing in the editor, which has no side panels. The two
  Ctrl+F handlers now require Shift to be up, so the chord never stands in for
  Find (#235).
- Print layout leaves the panel's width out of the page.
- The floating box and its toggle are removed. Help and the README list
  Ctrl+Shift+F.

## Fixture

`fixtures/search-panel/` is a folder searched for `needle`:

- `current.md`, the open document. It has 50 rendered matches across a
  heading, bold, italics and a link, a table, a quote, ordered and bulleted
  lists with strikethrough, a code block, a Chinese, Japanese and Korean line,
  a 30-row list and a closing section.
- `sibling.md`, with 3 matches in prose, a list and inline code.
- `alpha.md` with 1 match, `bravo.md` with 5 (3 listed plus "+2 more") and
  `quiet.md` with none.
- `elsewhere/notes.md`, a file in another folder that is opened as a tab.

## Automated checks

The new `search_results_panel` suite (`--search-panel-tests`) opens
`current.md` with `notes.md` as a second tab and drives the real key,
character, mouse and wheel handlers.

**Opening and grouping.** It checks that:

- Ctrl+Shift+F opens the bar and the panel, and an empty query asks for one;
- typing `needle` finds 50 matches and starts the scan of other files;
- every match gets exactly one row, in document order;
- each match sits under the heading it falls in;
- the sections follow the six headings that hold matches.

**Other files.** It checks that:

- the scan lists `notes.md` as a tab, then `alpha.md`, `bravo.md` and
  `sibling.md`, without `quiet.md` or the document itself;
- `bravo.md` lists its matches on lines 3, 5 and 7 and gets "+2 more";
- the captions are in place.

**Layout.** In Paper and Midnight, at 100% and 150% scale, in 650 and 1050 px
windows, it checks that:

- the panel takes the Contents slot and the page narrows by its width;
- the bar stays over the page with the results button inside it;
- the list lies inside the panel.

**Interaction.** It checks that:

- forty presses of Enter reach match 41 and its row scrolls into view;
- a match row selects its match and scrolls the page to it;
- a heading row selects its first match;
- the wheel scrolls the list to its end and no further, without moving the
  page;
- the scope rows switch off, save, rescan and switch back on, and with both
  off nothing else is searched.

**Opening files and refreshing.** It checks that:

- a click on `sibling.md`'s line 5 opens it in a third tab with the search and
  panel carried over, and lands on match 2 of 3;
- the earlier document is now listed once, as a tab;
- a click on `notes.md` switches to its tab rather than opening it again;
- switching back searches the document again;
- appending a line on disk and running the file watcher finds 51 matches.

**Closing.** It checks that:

- the results button closes and reopens the panel;
- Esc closes the bar and the panel;
- open Contents hands over without a slide and returns with Esc;
- print preview lays the page out without the panel and gives it back;
- entering the editor closes the panel;
- Ctrl+Shift+F leaves the editor alone.

Disabling each piece in turn fails the suite:

| Disabled | Failed checks |
| --- | --- |
| heading grouping | 3 |
| search on tab switch | 3 |
| tab and folder dedupe | 1 |
| landing on the line | 1 |
| search on reload | 1 |
| Contents returning | 1 |
| closing on entering the editor | 1 |
| centering the bar over the page | 8 |
| saving the scopes | 1 |
| following the current match | 1 |
| print-layout guard | 1 |

The editor guard sits both in the key handler and in `openSearchPanel`, so
removing either one alone fails nothing. Without the new Shift checks in either
Ctrl+F handler, the existing `table_and_modifier_input` suite (#235) fails 4
checks, starting with "Ctrl+Shift+F cannot alias Find". Writing that change
first broke the same suite, which is why the panel stays out of the editor.

The full suite passed 35 of 35.

## Desktop check

A portable copy of the built `tinta.exe` opened `current.md`, with `notes.md`
handed over as a tab. F and the typed query went to the window as posted
messages. The captures are in `out/issue-246/panel/` (ignored).

- Paper, 1000 px window: the results button opens the panel on the right and
  the bar moves over the page. The sections and snippets read as intended,
  with the Chinese, Japanese and Korean line in the interface font.
- Scrolled to the end of the list: `notes.md` sits under "Open tabs" with
  "elsewhere" beside it. `alpha.md`, `bravo.md` with "+2 more" and
  `sibling.md` sit under "This folder", and `quiet.md` is absent.
- Clicking `notes.md`'s match opens its tab at "1 of 1". Clicking
  `sibling.md`'s line 5 opens a third tab at "2 of 3", and the panel then lists
  `current.md` once, as a tab.
- Midnight with the Chinese interface in a 640 px window: the title, the scope
  rows, "还有 2 处" and the footer are translated, and the bar fits over the
  narrowed page.

## Limits

- Only the reader has the panel; Find in the editor is unchanged.
- Other files are matched in their Markdown source, so their snippets show
  syntax, and their counts can differ from the rendered text. `current.md` has
  51 matches in its source but 50 rendered, because one is the link target
  `#closing-needle`.
- The folder scan covers one folder without subfolders and lists up to 50
  files.
- A match that exists only in markup lands on the next rendered match.
- The panel uses Contents' width and cannot be resized while the bar is up.
