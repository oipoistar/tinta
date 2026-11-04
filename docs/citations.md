# Citations, figures and tables

Tinta formats citations and numbers figures, tables and equations the way
pandoc does, so a paper written in Tinta builds unchanged with pandoc and
the other way round. Nothing to install: the styles are built in.

## Citing

Name a bibliography in the front matter, then cite its keys:

```markdown
---
bibliography: references.bib      # or a list: [main.bib, extra.json]
csl: apa                          # optional; Chicago author-date by default
reference-section-title: References
---

Literate programming began with a paper [@knuth1984], and big teams are
slow for reasons [see @brooks1995, pp. 33-35; also @smith2020, chap. 1].
@okafor2021 [p. 4] measured it; only the year: [-@doe2024].
```

- **Libraries:** BibTeX and BibLaTeX (`.bib`, as Zotero, JabRef, Mendeley
  and Google Scholar export them) and CSL-JSON (`.json`). Paths are relative
  to the document. Save the file from your reference manager and open
  documents update.
- **Styles:** `apa`, `chicago-author-date`, `ieee`, `nature` and `gb-t-7714`
  (China National Standard GB/T 7714-2015, numeric) are built in, the
  official files from the Citation Style Language project. Any other style
  works from its `.csl` file: `csl: styles/elsevier-harvard.csl`.
- **Syntax:** pandoc's. Several works go in one bracket, separated by `;`.
  Text before the key is a prefix, text after it a locator (`p.`, `pp.`,
  `chap.`, `sec.`, `fig.`, `n.`, `vol.`... or a bare number for pages) and
  a suffix. `-@key` leaves the author out; `@key` in the sentence names
  the author there.
- `lang: zh-CN` (or `en-US`) sets the language of the terms and dates.
  `nocite: '@*'` lists every work in the library; `link-citations: false`
  keeps citations from being links.

A citation reads as the style formats it, in the link colour. Click it to
go to its reference, and Back to return; rest the pointer on it to read the
whole reference. The references follow the document's last section, before
its footnotes, under `reference-section-title` unless the last heading
already says References, Bibliography or 参考文献.

Without a bibliography, `@` is ordinary text: `@mentions` and e-mail
addresses never become citations. Neither does a key the library does not
hold, outside brackets. In brackets, an unknown key is reported, and so is
an unreadable library or style: a notice names the file and the line.

## Figures, tables and equations

```markdown
![The nib, split along its slit](nib.png){#fig:nib}

Table: Ink densities by brand {#tbl:density}

| Brand | Density |
| ----- | ------- |

$$ E = mc^2 $$ {#eq:energy}

As @fig:nib shows, and [@tbl:density; @eq:energy] confirm...
```

- An image alone in its paragraph with `{#fig:id}` becomes "Figure 1:" and
  its alt text, under it. A paragraph starting `Table:` (or `: `) beside a
  table captions it, numbered with `{#tbl:id}`. `{#eq:id}` numbers an
  equation, as `\tag` does; `\label` names still work.
- `@fig:id` reads "fig. 1", `@Fig:id` "Fig. 1", `[@fig:a; @fig:b]`
  "figs. 1, 2" and `[-@fig:id]` "1". Click one to go to its figure; rest
  the pointer on it for the caption. The words follow pandoc-crossref's
  front matter keys (`figureTitle`, `tableTitle`, `titleDelim`,
  `figPrefix`, `tblPrefix`, `eqnPrefix`), and Chinese with `lang: zh`.

## Exporting

- **HTML** writes pandoc's markup: `span.citation`, `div#refs` with
  `div.csl-entry` per reference, `figure` and `figcaption`.
- **Word** keeps the formatted citations and references (in a
  Bibliography style with a hanging indent) and puts captions in Word's
  Caption style, their numbers as fields (`SEQ Figure`, `SEQ Table`), so
  Word's table of figures and cross-references find them.
- **Print and PDF** are as on screen; a paper prints as below.
- **LaTeX and other pandoc formats:** Tinta hands the document to pandoc
  with `--citeproc`, and `--filter pandoc-crossref` when that filter is
  installed.

## Printing a paper

Front matter in pandoc's own variables makes a document a paper, and the
same file builds with pandoc's LaTeX:

```yaml
---
title: "Ink on the move: capillary flow in split nibs"
subtitle: A study in two columns
shorttitle: Ink on the move        # the running head; else the title
author:
  - Ana Okafor
  - name: Lars Lindqvist           # pandoc's author objects give their name
institute: [University of Lagos, Uppsala University]
date: 4 November 2026
abstract: |
  Fountain pens move ink by capillary action alone...

  A second paragraph.
keywords: [capillary flow, fountain pens]
thanks: We thank the reading group.
classoption: twocolumn             # or [twocolumn, a4paper]
papersize: a4                      # a4, letter (legal, a3)
fontsize: 11pt                     # 10pt to 12pt; the page sets 12pt
geometry: margin=2cm               # the page margins
---
```

- A `title` with an `author` or an `abstract` prints a **title block** in
  place of the properties strip: the title, subtitle, authors joined with
  commas and "and", the institutes, the date, the abstract set in from both
  sides under its heading, the keywords, and the thanks as a footnote
  under a short rule, marked * on the title. The screen keeps the strip.
- **Two columns** with `classoption: twocolumn` or the preview's Two
  columns: the title block spans both on the first page, the text runs
  down the left column and then the right, and figures, tables, code and
  equations fit their column (wider ones shrink into it).
- **Page numbers** centred under the text, and **running heads** from the
  second page on: the short title or title on the left, the first author's
  surname on the right, "et al." when there are more. A paper has both;
  the preview turns either off, or on for any document.
- `papersize` and `fontsize` choose the preview's paper and the type size,
  and `geometry: margin=` the margins, until a margin is picked in the
  preview.
- **Word** sets the title block in the styles Word and pandoc name: Title,
  Subtitle, Author, Affiliation, Date, Abstract Title and Abstract, and
  Keywords, for a journal's template to restyle. The thanks is a footnote
  marked *.

The words follow `lang:` (Abstract, Zusammenfassung, Résumé, 摘要...).
The title block reads Markdown's emphasis, bold and code, but sets maths
and citations in it as written.

Try the [paper](../tests/fixtures/scholar/paper.md), its
[Chinese companion](../tests/fixtures/scholar/paper-zh.md), the
[citation syntax](../tests/fixtures/scholar/citation-syntax.md), the
[numbered figures](../tests/fixtures/scholar/crossref.md) and the
[two-column paper](../tests/fixtures/scholar/paper-print.md) (Ctrl+P). The
design is in [docs/specs/scholar.md](specs/scholar.md).
