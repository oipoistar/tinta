# Long lines in code blocks (#324)

Validated on Windows with the Release build on 2026-11-15.

## What was reported

A prompt kept in a fenced code block, each paragraph of it one long line.
The block grew as wide as its longest line, far past the window (the
screenshot is 3371 pixels wide and the text still runs off it), so the
page had to be scrolled sideways to read a sentence. The request: wrap
long lines, and make that the default.

## What changed

A line of a code block that is longer than the block **wraps at the
block's edge**. **Settings → Appearance → Wrap long lines in code blocks**
turns that off; `settings.ini` keeps it as `wrapCode=1` or `0`, and a file
from before the setting, or a value that is neither, wraps.

| `wrapCode=` | A long line                              | The page                    |
|-------------|------------------------------------------|-----------------------------|
| `1`         | goes over as many rows as it needs       | never scrolls sideways      |
| `0`         | stays on one row; the block grows        | scrolls sideways, as before |

- **Where a row ends.** At a space, a hyphen or another place a line may
  break (between two Chinese characters, after a slash in a path) where
  the row has one; inside a word where it has none, so a long URL or a
  string with no space is cut at the edge. DirectWrite decides, with the
  block's font, its features and its fallback fonts, so a row of mixed
  scripts is measured as it is drawn.
- **What stays.** A line's indentation leads its first row; the rows
  after it start at the block's left. The spaces and tabs a row ends on
  are not drawn, as in any wrapped text. Colours of highlighted code
  carry over the rows. Lines that fit are laid out exactly as before, and
  a block of such lines is the same block with the setting on as off.
- **The text is one line still.** Selecting, copying (the selection or
  the block's Copy button) and searching see the line as written: no
  break is put into it. A match that runs over the end of a row is marked
  on both rows.
- **Print and PDF** follow the setting. A block with a long line used to
  be shrunk as a whole to fit the paper, down to letters too small to
  read; wrapped, it prints at the size of the text around it.
- **The HTML export** follows it too: `pre` gets `white-space: pre-wrap`
  and `overflow-wrap: anywhere`; off, the block scrolls sideways in the
  browser as before. Word wraps the DOCX export's paragraphs itself.
- Plain text files, which are one code block, wrap the same way, and so
  do the blocks in the editor's docked preview, a link's peek and Live.
- A block too narrow for a few letters (deep in nested lists in a narrow
  window) is left to grow as before: one letter a row would be worse
  than a sideways scroll.
- Wide tables and diagrams are untouched and keep their Fit button.

The setting is read at the start and written when the window closes, as
Heading separators is.

## Fixture

`fixtures/code-wrap-324.md`: a prompt in a fence with no language, its
paragraphs long lines of Chinese with paths and Latin words among them; a
highlighted Python block with a long signature, a long docstring and a URL
with no space; a short C++ block; a JSON block inside a nested list; a
block inside a quote; a table of the setting; a plain block with an
indented line and a word of 265 letters; with headings, bold, italic,
links, inline code, struck text, a quote in Chinese, task boxes, sub- and
superscript around them.

## Automated checks

**`code_wrap`** (`code_wrap_tests.cpp`, 1,176 checks).

- The line: on in a new setup and in a file without it; written and read
  as `0` and `1`; by hand `0`, `1` and values that are neither; the
  settings beside it untouched.
- The fixture, in a light and a dark theme at 1300, 760 and 420: for each
  of its six blocks, the page no wider than the window, the block's
  background as wide as the column and no wider, every stretch of text
  and every row inside the padding, rows a whole number of rows apart,
  the rows showing the code from end to end with nothing twice and
  nothing left out, the document's text holding the code as written, as
  much room under the last row as over the first. The prompt, the
  highlighted block, the list's block, the quote's block and the plain
  block each take more rows than they have lines, the short block one.
  Rows of Chinese run most of the way across; the line of words breaks
  only after a space or a hyphen; the long word fills each row to within
  a letter or two; the indented line's eight spaces lead its first row
  and its second starts at the block's left; the highlighted block has
  the same colours wrapped as not, its coloured stretches following one
  another without a gap from the left padding.
- Selecting a wrapped line marks one band a row inside the block and
  copies it as the one line it is; a click on its second row lands in
  that row's text. A word found on a wrapped row is marked on that row;
  letters found across the end of a row are marked on both rows.
- The setting off: every line one row, the long blocks grown past the
  column, the page scrolling sideways and shorter by the rows; the blocks
  below move by exactly the rows added above them.
- Awkward lines at 900 and 420: spaces in the middle and at the end, a
  line of nothing but spaces, tabs in the middle of a highlighted and of
  a plain line, a line that leads with tabs, Windows line ends, a string
  of 700 letters in highlighted JSON, emoji and Chinese among Latin
  letters. A text file. Short lines, blank lines and an empty block make
  the same block on as off. A nested block narrower than a few letters
  keeps its line whole. Zoomed to 160%, the rows follow the bigger
  letters.
- Print: with the setting on nothing is shrunk to fit the paper; off, the
  wide blocks are, as before. HTML: `pre-wrap` on, not off.
- Settings: in eight languages at 1300, 760 and 420 the row is in the
  reading group right under Heading separators and over Fold sections,
  inside the dialog, its toggle the size of its neighbour's and under it,
  the row as tall, label and hint whole at 760 and wider; a click
  toggles, has the page behind laid out again (shorter and wider off, as
  it was on), and redraws the toggle, the dialog and its neighbours as
  they were.
- 400 long highlighted lines wrap into 1,600 rows, in a time that is printed (below) and held to a loose bound.

Each part taken out in turn fails the test:

| Taken out                                                  | Failing checks |
|------------------------------------------------------------|---------------:|
| settings.ini is written with the line                      |              5 |
| settings.ini's line is read                                |              2 |
| A value that is neither wraps                              |              1 |
| Long lines wrap                                            |            266 |
| Off, they do not                                           |             29 |
| A block too narrow for a few letters is left to grow       |              1 |
| Rows end at the padding, not at the card                   |            145 |
| The block as tall as its rows                              |            126 |
| A row's place in the document                              |             82 |
| A plain row's own layout placed in the document            |              6 |
| The spaces a row ends on are not drawn                     |             69 |
| Nor the tab a row ends on                                  |              4 |
| A word longer than the block is cut inside                 |             47 |
| Rows measured with the fonts they are drawn in             |             22 |
| A line that is one row by measure loses its hanging spaces |              8 |
| The pieces of a row side by side                           |             12 |
| A piece cut where the row ends                             |            203 |
| A piece cut where the row begins                           |            188 |
| The HTML export wraps                                      |              1 |
| The HTML export follows the setting                        |              1 |
| A click in Settings toggles                                |             10 |
| A click in Settings lays the page out again                |              8 |
| The row on the page                                        |             28 |
| The toggle drawn as the setting stands                     |              4 |
| The row under Heading separators                           |             24 |
| A hint too long for its row                                |              2 |
| The row in Russian                                         |              3 |

Two of them were not caught at first. "The pieces of a row side by side"
passed with every coloured stretch of a row drawn at its left edge, one
over another; the test now walks each row from its first stretch to its
last. "A word longer than the block is cut inside" passed because the
code asked DirectWrite for a second way of wrapping that does the same as
the first; the line that asked was taken out of the code instead.

The start's and the close's own lines in `main_d2d.cpp` are not in the
test binary; the built app below is what shows them.

Four older tests counted on a long code line staying on one row and failed
until they were told so:

- `code_block_layout`: its mixed fixture is laid out with the setting off
  for the checks it had (rows, sideways scrolling), then on: the page no
  wider than the window, each block taller by whole rows or not at all,
  no background past the column, the wide line's block taller.
- `syntax_highlighting`: row heights with the setting off, as before; then
  the three fixtures wrapped at 520, where laying the page out bit by bit
  gives the same text, blocks and colours as all at once.
- `print_and_word_export`: with long lines wrapping no code block has to
  shrink; with the setting off the wide one shrinks as it did.
- `touch_gestures`: its page needs to pan sideways, so its long code line
  is kept on one row.

`settings_layout` counts the Appearance page's rows, five in the reading
group now; its dialog is drawn 960 high, since the page with a ninth row
no longer fits a window 900 high in German at 650 and scrolls there.

The whole suite passes, 83 of 83, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which overwrite the clipboard.
`code_block_layout`, whose area this is, was run by itself with every
format on the clipboard kept aside and put back afterwards, and passes.

## Looked at

Pictures from the test (`build/code-wrap-test-Release/frames/`): the
fixture at 1300, 760 and 420 in a light and a dark theme, and at 760 with
the setting off; the print pages on and off; the Appearance page in a
wide and a narrow dialog in each of the eight languages. Wrapped, every
block ends at the column's edge with its padding on both sides, the
Chinese rows are full, the Python block keeps its colours over the rows,
the URL is cut after slashes, and the block in the list and the one in
the quote wrap at their own edges. Printed, the blocks are at the size of
the text around them; with the setting off they are shrunk to a few
pixels a letter, as they were.

## In the built app

`tinta.exe` in a portable folder of its own with the fixture and a
`settings.ini` (`out/audit/realapp_324.py`, not in the repository). Its
own print layout is written as pictures (`--printpages`), its HTML export
is read, and a window is started on the fixture, left to settle and closed
through its message queue.

| Build    | `wrapCode=` at the start | Printed pages             | HTML `pre-wrap` | `settings.ini` after the close |
|----------|--------------------------|---------------------------|-----------------|--------------------------------|
| this one | (no line)                | 3, blocks wrapped         | yes             | `wrapCode=1`                   |
| this one | `1`                      | 3, blocks wrapped         | yes             | `wrapCode=1`                   |
| this one | `0`                      | 2, the wide blocks shrunk | no              | `wrapCode=0`                   |
| before   | `0`                      | 2, the wide blocks shrunk | no              | no line                        |

With the setting off, this build's two printed pages and its HTML export
are the same files, byte for byte, as those of the build before it
(master at 9b92506). The window's title named the fixture each time, every
run closed with exit code 0, and the fixture was unchanged. The printed
pages were looked at: wrapped, the blocks are at the size of the text
around them, each row inside its block.

## Speed and size

- Lines that fit take the path they took: one layout a coloured stretch,
  nothing measured twice. A block of 4,000 such lines prints its 84 pages
  in 4,158 to 4,165 ms; the build before took 4,165 to 4,194 ms.
- A line that does not fit is laid out whole, measured once more by
  DirectWrite for its rows, and laid out again where a row cuts a
  stretch. 400 long highlighted lines: 49 ms on one row each, 74 ms
  wrapped into 1,600 rows.
- The window is there 168 ms after the launch, whichever way the setting
  stands, as with the build before (169 ms).
- `tinta.exe` is 5,266,944 bytes, 4,608 more than the build before.

## Limitations

- The toggle was not clicked in the built app's own window; the test
  clicks it in the dialog as drawn, and the built app was given the line
  in `settings.ini`.
- Screen copies come back black here, so the built app's page was not
  looked at on screen: its print pages were, and the test's pictures of
  the same layout.
- A wrapped row is not marked or set in: it starts at the block's left
  like a new line. Text drawn in characters (a table of dashes and bars,
  a tree of folders) loses its shape when it is wider than the block;
  the setting off brings the old layout back for every block, and there
  is no switch for one block.
- When the first word after a line's indentation does not fit beside it,
  the indentation is a row of its own, as in a browser.
- Tab stops in highlighted code are measured from each coloured stretch,
  as before, while the rows are measured from the line's start. Should a
  row come out wider than its block for that, the block grows to hold it
  rather than cut it; the lines with tabs tried here did not.
- The setting is written when the window closes, as the other Appearance
  settings are: of two windows, the one closed last decides.
- In Live, the block being typed in shows its Markdown and follows the
  editor's own word wrap; the blocks around it wrap as on the page.
- Not part of this: a code block that runs over a page break prints the
  row at the break on both pages, in the margin of the second. 4.1.1
  does it too; it is noted for a change of its own.
