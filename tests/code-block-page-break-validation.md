# Blocks across a page break in print

Validated on Windows with the Release build on 2026-11-15. No issue: found
while working on code wrapping (#324), and not caused by it. Both faults are
in the released 4.1.1 and older.

## What was wrong

Reproduced with 4.1.1's sources (`9b92506`), `tinta.exe --printpages` in a
portable folder, on a heading, a paragraph and a `text` fence of 70 rows:

- **The row at the break printed twice.** "row 41" stood at the foot of
  page 1 (y 1030 of the 794 x 1123 page) and again on page 2 at y 60, in the
  top margin, 20 px over "row 42" where the page's text starts (y 72).
  It was not only code: the last line of a paragraph before a break was
  printed again over the next page's top margin too. Of the 552 pages the
  fixtures print, 82 had text in a margin.
- **The card ran through the margins.** The block's grey card went on from
  the last row of page 1 to the paper's edge, and came down from the paper's
  edge on page 2. A table's bands, a quote's bar and a diagram's arrows cut
  by a break did the same.

Print, Export as PDF, the print preview and `--printpages` draw every page
through one routine (`drawPaperPage`, `drawDocumentRange` in
`src/print.cpp`), so all four showed both. The PDF of the 70 rows had
"row 41" in its text on both pages.

## What changed

- **A line belongs to one page.** A break is cut along the top of the first
  line that does not fit, which is exactly the bottom of the line above it.
  `drawDocumentRange` took what ended on a stretch's first Y as part of the
  stretch, so that line was drawn on both pages. What ends where a stretch
  starts now belongs to the stretch before.
- **A page is clipped to its stretch** (`paperFrames`): from the top margin
  to the cut, across the paper's width. A card, a band, a bar or an arrow
  cut by the break stops at the cut and goes on from the top of the next
  page. Two columns were clipped like this already.
- **A box keeps its edges** (`boxEdges`, `breakAfter`). With pages clipped, a
  break in a box's padding would leave a sliver of the box alone on a page:
  the top edge of a card whose first row did not fit, or the bottom edge of
  one whose last row just did. A break now falls over a box's top edge when
  it would fall in its top padding or first line, and over its last line
  when it would fall in its bottom padding. A box is a painted rectangle
  with lines of text, pictures or shapes inside it: a code block's card, a
  table row's band, a callout's tint. A chip or a mark set in a line of text
  is not one and breaks with its line.

## The card in the margins: not intended

The card ran into the margins because one-column pages were never clipped,
not because it was meant to. `paper_export` had a check that "a quote cut by
the page break still runs into the margin, as it always did"; it was written
to show that paper export left other documents alone, and it is changed with
this. Reasons to stop the card at the cut:

- A margin is where page numbers and running heads go; the card ran under
  them.
- The card ran to the paper's edge, where a printer cuts it off wherever its
  own unprintable border starts.
- Two columns already stop at the cut, so one column was the exception.

The first change stands without the other two (it is its own commit): with
it alone every line prints once and boxes run through the margins as before.

## Automated checks

`print_and_word_export` (246 checks, 109 before), two new parts:

- `codeBlockPageBreak`, on `fixtures/code-block-page-break.md` in Paper and
  Midnight at 19.05 and 25.4 mm: no row straddles a cut; a cut in a code
  block lies between two rows; each side of such a cut, replayed at 1:1,
  has the row above on the page before only and the row under on the page
  after only; every page's stretch is clipped to itself; the card reaches
  the cut and goes on from the top of the next page; the top and bottom
  margins of every page are white.
- `boxEdgesAtPageBreak`, on `fixtures/code-block-page-edge.md`: the foot of
  the first page is moved through a table's header band and a four-row card
  by the margin alone (the lines are too short to wrap again), seven places
  in all, and the break is read back. In the top padding and in the first
  row it is the box's top; in the bottom padding it is the last row's top;
  in a middle row and under the card it is where it was before. The card
  then opens the next page with its padding.

Each of the three changes taken out fails its checks: 6 without the first,
24 without the clip, 4 without the box's edges. `paper_export` draws its
reference page clipped as well and expects empty margins on notes.

All suites but the three that take the clipboard (`notification_setting`,
`copy_file_path`, `code_block_layout`): 82 of 82 pass, in the worktree's own
`build/`.

## Checked with the built app

All with `tinta.exe` from the worktree's `build/Release`, in portable
folders under the ignored `out/page-break/`, the unchanged build beside the
changed one:

- **The 70 rows**, `--printpages`: page 1 ends under "row 41" and its card
  at y 1042; page 2 starts with "row 42" at y 72, the card from y 72. Both
  pages looked at.
- **The same, `--exportpdf`** (Microsoft Print to PDF), text read back with
  `pdftotext`: page 1 "row 01" to "row 41", page 2 "row 42" to "row 70";
  before, page 2 started with "row 41". The mixed fixture's PDF has its 70
  plain rows once (71 before). The PDF's pages were not looked at as
  pictures (no rasterizer here); they are the same command lists
  `--printpages` draws.
- **The mixed fixture**, `--printpages`, five pages, all looked at around
  their breaks: the plain block breaks between rows 38 and 39, the
  highlighted one after `step08` and after `step20`; the heading, list,
  quote and text
  around them as before. The table's header band, which stood empty in the
  bottom margin of page 2, opens page 3.
- **Narrow, another theme**: both fixtures with `themeIndex=5` and
  `printMarginMm=50` (a 416 px column), seven and two pages: rows run on
  across each break, and no pixel in a top or bottom margin is other than
  white.
- **Every Markdown fixture** (182 files), `--printpages` before and after,
  compared pixel for pixel: 301 pages the same, 130 changed, and two
  documents a page longer (`highlight-long-document` 83 to 84,
  `live-large-block` 38 to 39). Of the 130, 60 changed in the margins only.
  The others change at a page's foot and the next one's head: a box moved
  whole (`byte-order-mark`, `goto-line-276`, `syntax-languages`), or a
  diagram's arrow stopped at the cut (`mermaid-asymmetric-shape`). Four
  pages differ in a few pixels in the middle of the page; a second run of
  the unchanged build differs from the first in the same places.
  Text-dark pixels in a top or bottom margin: 82 pages before, 13 after,
  all 13 the page numbers and running heads of documents with a title
  block.

## Not checked, and what is left

- No sheet of paper was printed. Print and the PDF export share
  `renderPages`; the PDF was checked as above.
- The print preview was checked through its page pixels in the tests, not
  by eye in a window.
- A chip of inline code reaches a fraction of a pixel under its line. At
  the foot of a page that last faint row of the chip is now cut (it was
  whole, and the whole chip stood a second time in the next margin).
- A block that is shrunk to fit the width and is taller than a page is
  still cut through a row, as before; it is now cut clean, where its rows
  near the cut were printed twice.
- A diagram that does not fit the page is still cut between its shapes.
- A table row without a band of its own breaks at its text, as before: the
  line over it stays at the foot of the page before.
- A two-column paper's title block is drawn unclipped, as before.
