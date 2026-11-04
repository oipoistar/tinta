# Scholar, phase 3: paper export

Validated on Windows with the Release build on 2026-11-04. The design is in
`docs/specs/scholar.md` (Paper export); the user's page is
`docs/citations.md` (Printing a paper).

## What was asked

A paper written in Tinta prints and exports like a journal manuscript,
reading pandoc's own front matter variables so the same file builds with
pandoc's LaTeX: a title block, two columns, page numbers and running heads,
pandoc's paper and type size, and Word styles a journal's template can
restyle, with captions numbered as Word fields.

## The change

- **Reading the front matter** (`src/paper.cpp`, `include/paper.h`):
  `title`, `subtitle`, `shorttitle`, `author` (a string, a flow or block
  list, pandoc's author objects by their `name`), `institute`, `date`,
  `abstract` (plain, quoted, or a `|` / `>` block scalar with its
  indentation and chomping), `keywords`, `thanks`, `lang`, `classoption`
  (a string or list; `twocolumn`), `papersize`, `fontsize` and
  `geometry: margin=`. The Properties element now keeps the YAML between
  the fences in its `text` (`document.cpp`); `fm::parse` is unchanged and
  the screen's strip reads the same properties as before.
- **Title block** in print, PDF and Word when the front matter has a title
  and authors or an abstract, in the strip's place: the title (a `*` when
  there is a thanks), subtitle, authors joined with commas and "and", the
  institutes on lines below, the date, an "Abstract" heading and the
  abstract set in from both sides, "Keywords: a, b, c", and the thanks as
  a footnote line under a short rule; a hairline across the page closes
  it. The words follow `lang:` (ten languages; Chinese sets 摘要, 关键词：
  a；b and 张三、李四). It is one element in the layout: `render.cpp`'s
  only change is the Properties dispatch calling `layoutTitleBlock()`,
  which sets each line as a text run of its own, so a page can break
  between any two.
- **Two columns** (`classoption: twocolumn` or the preview's toggle): the
  document is laid out at a column's width (the text width less a quarter
  inch, halved), the title block across both. Pages are cut into frames:
  the title block at the top of page 1, then each page's left column and
  its right, each stretch ended where a page would end (above any line,
  picture, shape or shrunk block), each column clipped at the gutter's
  middle. Wider figures, tables, code and equations shrink into their
  column, as they shrink into the page in one column.
- **Page numbers** centred in the bottom margin, **running heads** in the
  top one from page 2 (short title or title on the left; the first
  author's surname, "et al." for more, on the right), in the body face at
  three quarters of its size in a muted grey, never over the text (a
  margin too narrow for the line drops them). On for a paper, off
  otherwise; the preview toggles them for the document.
- `papersize` picks the preview's paper (and the PDF's and `--printpages`'),
  `fontsize` scales the printed type (11pt is 11/12 of the page's 12 pt),
  `geometry: margin=` sets the margins until a margin chip is clicked.
- **The preview** has a row under its header: Two columns, Page numbers,
  Running heads, filled while on, each as wide as its label, in all eight
  languages. The margin chips show the margin that prints.
- **Word**: the title block in styles named Title, Subtitle, Author,
  Affiliation, Date, Abstract Title, Abstract and Keywords (defined in
  styles.xml); the thanks a real footnote with a `*` custom mark; numbered
  figure and table captions as "Figure " + `{ SEQ Figure \* ARABIC }` +
  ": ..." and "Table " + `{ SEQ Table \* ARABIC }`, written in Word's own
  begin/code/separate/result/end form so an update keeps the number bold,
  in the Caption style as before. Citations and the bibliography are
  written as before.

## Fixture

`fixtures/scholar/paper-print.md`: a short two-column paper with every
title block key (`abstract: |` of two paragraphs with emphasis and code,
a block list of authors, one an author object, two institutes, keywords
with a quoted comma, thanks, a short title), `classoption: twocolumn`,
`papersize: a4`, `fontsize: 11pt`; and headings, a numbered list, a bullet
list, a quote cut by the first column's end, citations (APA), a footnote,
inline maths, two numbered equations, the figure `nib.png`, a captioned
table, a Python code block too wide for a column, a Mermaid flowchart, an
e-mail address and an @mention, and the references.

## Automated checks

`paper_export` (`tests/paper_export_tests.cpp`, `input_tests
--paper-export-tests`, a folder of its own):

- Front matter reading: quoted, single-quoted, commented and folded plain
  scalars; `|` and `>-` blocks (lines kept, lines folded, a break for an
  empty line, the chomping); flow lists over a quoted comma, an apostrophe
  in a bare item and a flow mapping's name; block lists of strings and of
  author objects; classoption, papersize, fontsize and geometry in their
  string and list forms, a size out of range and a paper not offered left
  alone; what makes a title block and what does not; authors joined and
  the running head's surname in English, German and Chinese; the words in
  Chinese and French.
- The two-column fixture in a 1050 x 900 window: on screen the strip, no
  title block; in the preview A4 (over a Letter choice), the type at 11/12,
  all three options on, the heads "Ink on the move" / "Okafor et al."; the
  layout at the column width (313 DIPs on A4); the title on page 1's first
  line spanning the text width, every title block line across the page,
  the abstract set in, the closing hairline; three frames on page 1 (the
  title block unclipped at the top margin, the columns under it) and two
  on page 2; each column at the left margin or right of the gutter,
  starting under the title block on page 1, no longer than the page
  allows, clipped at the gutter's middle; no line or picture cut by a
  column's end; the figure within its column, text wider than it inside a
  shrink band that fits it, the title block never shrunk. In the
  rasterized pages: no head on page 1, its number; text in page 1's right
  column; heads left and right on page 2, its number centred, nothing in
  the gutter, text in the right column.
- The toggles clicked through the window's handlers: heads off (page 2's
  top margin empty), numbers off (bottom margin empty), one column (the
  text width, a page a frame, the title block still on top and across),
  and all three back on; the chips in their row, apart, and inside a 650
  px window. A toggle outside the preview is kept for the document; the
  pages `printDebugPages` writes match the preview's count.
- A document that is no paper (`crossref.md`, a title only): no options,
  the page's type and margin, the strip, a frame a page at the margins
  unclipped; **every page rasterizes pixel for pixel as the old path drew
  it** (`drawLayoutRange` over the page's bounds at the margins, as
  `rasterizePreviewPage` did before); toggled on, it takes page numbers and
  its title as the running head.
- Word: the eight title block paragraphs before the first heading and
  their styles in styles.xml, the title's words, the Abstract heading, the
  thanks footnote with its custom mark, `SEQ Figure` and `SEQ Table`
  fields with the label's words before them and their parts in Word's
  order with a bold code, the Caption and Bibliography styles; notes
  without a title block write none and still number their captions as
  fields.

`paper_export` passes with 1725 checks. `print_and_word_export` (109
checks), `inline_code_theme`, `scholar`, `i18n` (the three new keys in
all eight languages) and `russian_interface` pass unchanged.

## In the built app

`tinta.exe` copied beside a portable `settings.ini` in `out/new/` (theme
5), `--printpages`, `--exportpdf` and `--exportdocx`; pages and frames
inspected:

- `paper-print.md`, A4, three pages. Page 1: the title with its `*`, the
  subtitle, "Ana Okafor and Lars Lindqvist", the two institutes in grey
  italics, the date, Abstract over two paragraphs set in from both sides
  with *Poiseuille's law* in italics, "Keywords: capillary flow, fountain
  pens, paper sizing, feathering", the thanks under its short rule, the
  hairline; below it the left column (Introduction, the numbered list)
  and the right (the quote cut by the left column's end, its bar in both
  columns and nowhere else, Method, equation (1)); the number 1 centred
  under the text. Page 2: "Ink on the move" and "Okafor et al." in the top
  margin, the figure and its caption, equation (2), the code block and the
  flowchart shrunk into the left column, the table, a paragraph running
  on at the top of the right column, the bullet list, Discussion and the
  References hanging; page 3: the references in both columns, then the
  footnote under its rule.
- A Letter copy with `geometry: margin=1in` and `fontsize: 10pt`: 816 x
  1056 pages, inch margins, the smaller type in narrower columns.
- A Chinese paper (`lang: zh-CN`): 摘要, 关键词：毛细流动；钢笔；纸张,
  张三、李四、王五, two columns of Chinese text, a table and an equation.
- A custom serif theme (themes.ini: Georgia, Cambria headings): the title
  block in Cambria and Georgia, as the text around it.
- The preview, from the test's frames (`build/paper-export-test-Release`):
  the row under the header with Two columns, Page numbers and Running
  heads, all three on for the paper and off after the clicks; the
  one-column page with the title block across it; at 650 px the row fits
  (the top bar's paper chips touching the page title at that width is as
  before).
- `--exportpdf`: an A4 PDF of three pages (Microsoft Print to PDF) whose
  text, read back with PyMuPDF, holds the title block, the running heads
  and the page numbers.
- `--exportdocx`, opened in Word 365 through COM, invisible and
  read-only: every paragraph in its style (Title, Subtitle, Author,
  Affiliation x2, Date, Abstract Title, Abstract x2, Keywords), after an
  update of all fields `SEQ Figure` and `SEQ Table` read 1 and stay bold,
  the thanks is a footnote with the `*` mark, and Word's own table of
  figures and table of tables list "Figure 1: The medium nib, split along
  its slit" (page 2) and "Table 1: Flow by nib width" (page 3). Word's
  PDF of it shows the title block centred and the thanks at the foot of
  page 1. The Chinese paper's Word export carries 摘要 and the keywords
  with full-width marks.
- Documents that are no paper print as before: `--printpages` of
  `print-word-export-256.md` (2 pages), `footnotes.md` (2),
  `list-block-order.md` (4) and `scholar/crossref.md` (3) are byte for
  byte the pages the build before this change (f60a9e7) writes.
- Documents whose front matter has a title and an author now print as
  papers: phase 1's `scholar/paper.md` (one column, "How ink finds paper"
  / "Okafor et al." from page 2) and `frontmatter-settings.md` (title and
  author centred, numbered pages).

## Size

`tinta.exe` grows from 4,708,352 to 4,776,960 bytes (+68,608). From the
linker map, `paper.cpp` (the YAML reader and the title block, with their
unwind data) is about 36 KB; the rest is the frames, the margins'
furniture and the preview's row in `print.cpp` and `overlays.cpp`, and the
Word styles, title block and fields in `export_docx.cpp`.

## Limitations

- The title block keeps Markdown's emphasis, bold and code in its fields;
  maths and citations in them read as written.
- Authors are joined as written: numbered affiliations matched to
  authors, ORCID, e-mail and a corresponding author are not read.
- No floats: a figure or diagram taller than what is left of a column
  moves to the next and leaves the space (a block taller than what is left
  under the title block can leave page 1's columns empty). Figures do not
  span both columns (`{.wide}` is not built), and Word stays in one
  column.
- Code and diagrams too wide for a column shrink into it, as they shrink
  into a page, and leave their unscaled height below; table cells at
  column width wrap inside words, as narrow tables always did.
- Any document with a title and an author or an abstract prints as a
  paper, as asked: notes with `title` and `author` now get a title block,
  page numbers and running heads (the preview turns the last two off).
- Two authors read "Okafor et al." in the running head, as asked; LaTeX
  classes often name both.
- Seen while validating, as before this change: under a dark theme the
  Word export's equation images keep the theme's dark background.
