# Tables written in HTML (#325)

Validated on Windows with the Release build on 2026-11-18.

## What was reported

A Markdown file with a `<table>` in it "renders completely gibberish". The
sample is a table of keyboard shortcuts in three columns: `rowspan` on the
first two, several rows with no `</tr>`, a `<br>` inside two cells, and a
`**bold**` line right above `<table>` with no blank line between.

Tinta had no HTML tables at all. The tags of a table were dropped without a
word, so the cells' text ran together ("ActionKey"); the line breaks and
the indentation of the block stayed in its text, where they made lines the
layout never counted, so the lines were drawn over each other and the
paragraph after the block over them. The second half happened to any HTML
block written over several lines, a `<div>` or a `<p>` as much as a table.

## What changed

A table written in HTML **is a table**: the same element a pipe table
makes, drawn by the same code, with the same header, bands, lines, theme
colours and Fit button.

| In the source                              | On the page                                         |
|--------------------------------------------|-----------------------------------------------------|
| `<tr>`, `<td>`, `<th>`                     | rows, cells, header cells (bold, the heading's ink) |
| `<thead>`, or leading rows of `<th>` alone | the header rows: none, one or several               |
| `<tfoot>`                                  | its rows last, wherever they were written           |
| `<caption>`                                | a caption over the table                            |
| `rowspan`, `colspan`                       | a cell over several rows, columns, or both          |
| `align`, `style="text-align: …"`           | the cell's alignment                                |
| `<br>`, `<b>`, `<i>`, `<code>`, `<a>`, …   | the same inline content a pipe table's cell takes   |

- **End tags may be left out**, as HTML lets them be: a new `<tr>` ends the
  open row, a new `<td>` or `<th>` the open cell, `</table>` all of it, and
  so does the end of the block. An end tag with nothing of its own open
  ends nothing.
- **Joined cells.** A cell takes the first slot of its row that no cell
  from a row above covers, so the rows under a `rowspan` start in the right
  column. Its columns widen by equal shares when it needs more room than
  they have; a cell over several rows that needs more height lengthens the
  last of them. No line is drawn across a joined cell, a row's band stops
  at the cells that come down from the rows above, and a cell over several
  rows carries its own row's band, as in a browser. `rowspan="0"` runs to
  the last row; a span past the table's edge, or over a neighbour, is cut.
  A grid is 128 columns at most, as a pipe table's is.
- **No header unless there is one.** The report's table has none: its first
  row is a row like the others. A pipe table keeps its one header row.
- **A cell holds inline content**, as a pipe table's does. A `<p>`, a
  `<div>` or a heading inside a cell is a line of it, a list's items are
  lines with a bullet or a number, and a table inside a cell is read as
  lines of that cell.
- **A blank line does not end a table**: the HTML blocks right after a
  block that left its `<table>` open are the rest of it. Markdown in
  between does end it; a table left with nothing in any cell is then not
  drawn, and rows with no `<table>` around them make a table of their own.
- **Text between the cells** goes above the table, where a browser puts it.
- **Any HTML block**: runs of spaces and line breaks read as one space, as
  in a browser, so a `<div>` or `<p>` written over several lines is one
  paragraph that takes the height it is drawn at. What a `<div>`,
  `<details>`, `<summary>`, `<section>`, `<dl>` and their like hold stands
  on lines of its own. `&lt;` `&gt;` `&amp;` `&quot;` `&apos;` `&nbsp;` and
  numbered references are the characters they name.
- **Selecting, finding, copying.** The page's text has a row a line and a
  tab between its cells, as a pipe table's has. The table's Copy button
  puts every cell in the column it starts in, the slots a joined cell
  covers left empty, so the grid pastes into a spreadsheet as it stands; a
  `<br>` is a space there.
- **Editing.** In Live the table is the table while the caret is elsewhere
  and its lines of HTML while the caret is in it; beside the source, the
  preview draws it. It is **not** edited in place and has no + row or
  + column: those write pipes.
- **Print and PDF** draw what the page draws. **HTML export** writes
  `<caption>`, `<th>`/`<td>`, `colspan`, `rowspan` and `align`. **Word**
  gets `gridSpan` for columns and `vMerge` for rows, with a cell in every
  row a joined cell covers, the caption as a Caption paragraph, and a table
  inside a list item as a table.
- A pipe table is laid out, copied and exported exactly as before.

## Fixtures

- `fixtures/html-table-325.md`: the report's table as written, under a
  heading and an introduction, with a paragraph, a list and a quote after.
- `fixtures/html-table-mixed-325.md`: a plain table and the same table with
  pipes; head, body, foot and caption with aligned columns; cells over
  columns, rows and both; inline content in cells (bold, italic, code, a
  link, `<br>`, sub- and superscript, struck, inserted and marked text,
  character references, paragraphs and a list, a long sentence); rows and
  cells with no end tags and a table with no `</table>`; rows a blank line
  apart; a table in a list item and in a quote; a table inside a fence
  (not a table); a `<div>` and a `<p>` over indented lines, a `<details>`
  with a `<summary>`, two `<div>`s; uneven rows; a table inside a cell.

## Automated checks

**`html_tables`** (`html_table_tests.cpp`, 3,010 checks).

- The tree: 50 sources against the tree they must make, among them every
  case of the list above and the report's bold line; then CRLF line ends,
  a table 300 quotes deep and spans of 100,000.
- The grid: for the report's table, 50 rows, 3 columns, no header, a cell
  for every `<td>`, **no slot left empty**, and the row and column of 25
  cells by name; a table of spans with two header rows; a pipe table.
- Both fixtures, in a light and a dark theme at 1300, 760 and 420: no two
  stretches of text over each other anywhere on the page; every table's
  text, lines and bands inside it and no other text reaching into it; the
  page as tall as its text; what follows each table under it. For the
  report: each cell's text at its column's left edge, joined cells on
  their first row, the rows under them one below the other, no line across
  a joined cell and one under it, the `<br>`'s second line under its
  first, the bold line above and the paragraph below. For the mixed page:
  the HTML table as wide and tall as its pipe twin and copied the same,
  the caption over its table, the foot last, right-aligned figures on one
  edge, the spans' columns, the link a link, the multi-line HTML blocks
  one paragraph, the `<summary>` and the `<div>`s a line each.
- Text: the page's text and the Copy button's rows, a click in a cell, a
  selection over two cells and its bands, find marks in their column; in
  edit mode no cell of an HTML table to edit, the pipe table's six still
  there.
- Print (both fixtures, nothing shrunk), HTML and Word exports read back.
- 1,500 rows as pipes, as HTML and as HTML with joined cells, and 1,500
  sections with a table each: the times printed, the tables as tall as
  each other.
- Live and Source on the report's file: the table drawn in both, the
  caret's stretch covering the whole table, a letter typed in a cell and a
  row typed before `</table>` (end tags left out) showing in the table.

Each of 23 parts taken out makes it fail: line breaks kept in HTML blocks;
character references left as written; `rowspan` or `colspan` not placed;
a new row or a new cell not ending the open one; a table open at a blank
line not carried on; an empty table drawn; the foot left where it was
written; a block's edge left as an empty line; `<div>` and `<summary>`
starting no line; text between cells kept in the table; a table in a cell
opening a table; lines drawn across joined cells; a joined cell's height
given to its first row; a joined cell not widening its columns; the copy
ignoring the columns, or the `<br>`; the caption not drawn; the HTML
export dropping its `rowspan` or its caption; Word not merging down, or
dropping `gridSpan`. A 24th part, a line that kept an HTML table out of
in-place editing, changed nothing when taken out (nothing in such a table
has a place in the source to edit) and was removed.

The other 83 suites pass: 80 run four at a time, `clipboard_keys`,
`editor_context_selection` and `editor_features` one at a time. Run in
parallel, one or two of those three failed on the clipboard in each of
two runs, and not the same ones; one at a time all three pass. Whether
they do that without this change was not tried.
`notification_setting`, `copy_file_path` and `code_block_layout` were
not run: they overwrite the clipboard.

## In the built app

`tinta.exe` from a portable folder (`settings.ini` beside it).

- `--printpages` on both fixtures with the default theme, and with a
  `themes.ini` theme that draws the whole grid (the lines down as well as
  across): every page looked at. The report's table reads as its author
  wrote it over three pages; down-lines stop at cells over several
  columns.
- `--exporthtml`, `--exportdocx`, `--exportpdf` on both: the HTML read; the
  Word files unzipped, `document.xml` well formed, every row of the 12
  tables filling its grid, `gridSpan`, `vMerge`, `shd` in the schema's
  order; the PDFs rendered with PyMuPDF and looked at, their text holding
  the table's first and last cells and the paragraph after.
- The frames the test draws through the app's own context (both fixtures,
  light and dark, at 1300, 760 and 420) looked at.
- **Not done:** the window itself. Its copy came back blank (the desktop
  is not shown to this session), so nothing was checked by eye in the
  live window, in Word or in a browser.

## Size and time

Against the 4.1.1 sources (7e8dc0f), built the same way on the same
machine. Times are the best of a few runs inside the test binary, three
such readings each, with a probe of the same pages built into the old
sources.

|                                                         | Before              | After                             |
|---------------------------------------------------------|---------------------|-----------------------------------|
| `tinta.exe`                                             | 5,266,944 bytes     | 5,293,056 bytes (+26,112, +0.5%)  |
| A pipe table of 1,500 rows: read + laid out             | 8.0-8.5 + 188-192 ms | 8.0-8.2 + 184-198 ms             |
| The same table in HTML                                  |                     | 12.5-12.6 + 192-207 ms            |
| In HTML, a cell over three rows leading every third row |                     | 12.1-12.9 + 161-167 ms            |
| 1,500 sections with a pipe table each (1 MB)            | 125-128 + 1,318-1,352 ms | 125-128 + 1,301-1,327 ms     |
| The same, its tables in HTML with joined cells          |                     | 134-142 + 1,158-1,208 ms          |

A pipe table and a long document take what they took. The HTML export
of the long document with pipe tables is byte for byte what it was
(1,550,052 bytes from either build).

## Limits

- A cell holds inline content only. Markdown between `<td>` and `</td>`
  set off by blank lines (GitHub's way to put a code block or a list in a
  cell) ends the table: the Markdown shows as blocks of its own and the
  rows after it as a second table. Block content in cells would need the
  table layout to lay out blocks, not lines.
- Text in a joined cell starts at its top, as in every cell here; a
  browser centres it.
- `width`, `valign`, `bgcolor`, `<col>` and `<colgroup>` are not read.
- A `<div>`'s `align` is still not read; `<details>` is always open.
- Printed with a theme that draws the lines down, a table's down-lines
  and a cut row's band run into the page's margin at a page break. Pipe
  tables did that before (checked with the 4.1.1 build); a band of a cell
  over several rows now can too.
