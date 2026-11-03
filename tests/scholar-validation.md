# Scholar, phase 1: citations, references, figures, tables and equations

Validated on Windows with the Release build on 2026-11-03. The design is in
`docs/specs/scholar.md`; the user's page is `docs/citations.md`.

## What was asked

From the todo list: citations in pandoc's syntax from a BibTeX or CSL-JSON
library, a bibliography in a real citation style, numbered figures and
tables that can be referred to, and paper export, without LaTeX.

## The change

- **Libraries** (`bib.h`): BibTeX and BibLaTeX (macros, `#`, LaTeX accents
  and commands, case-protecting braces, names with particles and suffixes,
  `and others`, crossref inheritance, EDTF dates) and CSL-JSON, read into
  CSL's item model; problems carry their file and line.
- **Citation syntax** (`cite.h`): pandoc's, bracketed and in the sentence,
  prefixes, locators (English terms, bare pages, braces), suffixes,
  `-@key`; an `@` inside a word (an e-mail address) never cites.
- **CSL 1.0.2 processor** (`csl.h`): the five bundled styles (APA 7,
  Chicago author-date 18th, IEEE, Nature, GB/T 7714-2015 numeric) with the
  en-US and zh-CN locales, packed into `tinta.exe` (28 KB for seven files,
  `tools/csl_pack`); any `.csl` file by path. Sorting, numbering,
  disambiguation (names, initials, year suffixes), collapsing, et al.
  rules, page ranges, text case, locale terms and dates.
- **In the document** (`scholar.cpp`): a document with `bibliography:`
  cites; bracketed keys always, a bare `@key` only when the library has it;
  without a bibliography `@` is text. Citations read as their style formats
  them, in the link colour; a click goes to the reference, a resting pointer
  shows the references on a card. The references follow the last block,
  before the footnotes, under `reference-section-title` unless the last
  heading names them; numeric styles set a number column, author-date
  styles a hanging indent. Missing keys, unreadable libraries and styles
  and missing figures are reported a line each.
- **Figures, tables and equations**: `![caption](x.png){#fig:id}`,
  `Table: caption {#tbl:id}` before or after a table, `$$...$$ {#eq:id}`;
  `@fig:id`, `@Fig:id`, `[@fig:a; @fig:b]` and `[-@fig:id]` as
  pandoc-crossref words them, from its front matter keys, in Chinese for
  `lang: zh`; each id is a place a click goes to.
- **Exports**: HTML in pandoc's markup (`span.citation`, `div#refs`,
  `div.csl-entry`, `figure`, `figcaption`); Word with a Bibliography style
  and Word's Caption style; print and PDF as on screen; pandoc formats get
  `--citeproc` (and pandoc-crossref when installed), run from the
  document's folder.
- **Source and Live** colour bracketed citations and `@fig:` references as
  links.

## Fixtures

`fixtures/scholar/`: `references.bib` (30 entries over every type and
field), `references.json`, `broken.bib` (a mistake on each of seven
lines), `citation-syntax.md`, `paper.md` (APA: headings, a numbered list, a
quote, a table with citations in its cells, code, a Mermaid diagram,
footnotes with citations, two equations, a figure, an e-mail address and an
@mention), `paper-zh.md` (GB/T 7714 in Chinese with English entries) and
`crossref.md` (figures, tables and equations in quotes and lists).

## Automated checks

- `bibliography` (bib_tests, 469 checks), `citation_syntax` (cite_tests, 195)
  and `csl_processor` (csl_tests, 1045: every bundled style against
  hand-checked output for a dozen item types, disambiguation, collapsing,
  locators, author in text, missing keys, locale fallback).
- `scholar` (scholar_tests): figures, captions above and below tables,
  equation ids and numbers, the front matter's words and Chinese ones; in
  `paper.md` all 17 citations found (in a table, a footnote and a quote),
  APA's "(Brooks, 1995; Knuth, 1984)" and "Okafor (2021)", each citation's
  link, keys and anchor, no citation left as text, the e-mail address and
  @mention untouched, the five reference forms worded, 21 references
  hanging under "References" before the footnotes, uncited works left out;
  GB/T's number column; nothing cites without a bibliography while
  `@fig:` still does; a broken library, an unknown style, a missing key and
  a missing figure each reported.

- `scholar_in_app` (input_tests, `--scholar-app-tests`): `paper.md` in a
  window at 1050 and 650 px: the first citation's link goes to
  `#ref-knuth1984` and its anchor knows both its keys, every reference,
  the figure, the table and the equations are places to go, the hover
  card holds Knuth's reference, APA's entries hang (the second line starts
  right of the first), a click on the citation scrolls to its reference;
  `paper-zh.md` sets [1] in a column beside its entry. Frames are saved
  beside the test.

## In the built app

`tinta.exe --printpages` and `--exporthtml` on the fixtures
(`out/scholar/`):

- `paper.md`, APA: citations in the link colour, "(Doe, 2024a, 2024b; see
  also García et al., 2022, pp. 3–5)", "(A. Smith, 2019; J. Q. Smith, 2020,
  Chapter 2)", "(Abe et al., 2022)"; "eq. 2", "fig. 1", "tbl. 1",
  "eqns. 1, 2", "(Fig. 1)"; equations (1) and (2); Figure 1 and Table 1
  captioned; in the table, "(La Fontaine, 1668)", "Calvino (1979/1981)"
  and "(2023)". The References heading, 21 entries with hanging indents,
  italic titles and DOI links, then the footnote with its citations.
- `paper-zh.md`, GB/T 7714: superscript [1,2], [3,4], [5,6] and [1–3];
  图 1：, 表 1：, 式 1; 参考文献 with a number column, [J], [M], [D], [EB/OL]
  and foreign names in capitals.
- `crossref.md`: Figure 1 and 2, Table 1 and 2, an unnumbered caption,
  equations (1), (2) and (P).
- The HTML exports carry the same text in pandoc's markup.
- The frames from `scholar_in_app` (`build/scholar-app-test-Release`): at
  650 px the references wrap with their hanging indent, DOI links
  underlined; the Chinese paper's numbers stand in their column.

## Size

`tinta.exe` grows from 3,995,136 to 4,708,352 bytes for Scholar and CSS
themes together (713,216 bytes). From the linker map, Scholar's own code
is about 312 KB (BibTeX reader 80, CSL processor 162 with its XML reader,
`scholar.cpp` 50, citation syntax 17) plus 28 KB of packed styles; the CSS
engine and theme compiler are about 190 KB; the rest is their share of
`std::` templates (about 150 KB in all) and unwind tables. `/O1` or `/Ob1`
on these files changes nothing worth having (+512 and +6,144 bytes).

## Limitations

- In-text citations in numeric styles name authors by a fallback (family
  names, the locale's "and", et al. from three), since the styles give
  none; pandoc's own fallback may differ.
- Note styles (Chicago notes) format as in-text citations; footnotes from
  citations come in phase 2, with completion in Live, Zotero, inline
  `references:` and `::: {#refs}` placement.
- A BibTeX name with no comma and no space ("张三") is all family name, as
  in pandoc.
- Hover cards show in the reading view; Live shows them on its replayed
  page but not on the block being typed.
