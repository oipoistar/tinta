---
title: "Scholar: write the paper"
status: Phases 1 and 3 built (2026-11-04); phase 2 built but for inline references (2026-11-04)
created: 2026-11-03
---

# Scholar: write the paper

Citations, a bibliography, numbered figures and tables, and journal-ready
export, so a researcher can write a paper in Tinta without LaTeX. The syntax
is pandoc's (citeproc) and pandoc-crossref's, so a paper written in Tinta
builds unchanged with pandoc and the reverse.

Status: phase 1 is built: citations, references, numbered figures, tables
and equations, hover cards, and the exports (see [Phases](#phases) and
`tests/scholar-validation.md`). Phase 3, paper export, is built too:
title block, two columns, page numbers, running heads and Word's paper
styles (`tests/paper-export-validation.md`). From phase 2, the
writing aids (completion in Live and Source, the Cite button and
Zotero's picker, `tests/citation-completion-validation.md`), note
styles, section numbers with `@sec:`, and following a saved library
are built; inline `references:` is to come.

## Why

Tinta already sets maths as TeX does, numbers equations (#314), draws 22
Mermaid families and exports to Word and PDF. What a paper still needs is
the scholarly apparatus: citations that resolve against a reference library,
a bibliography in the journal's style, and figures and tables that number
themselves and can be referred to. Today that means leaving for LaTeX, Word
with Zotero, or a pandoc command line. Many of the people who write to us
(and many writing in Chinese) are students and researchers.

## Goals

- Cite with pandoc's syntax and get the citation and bibliography in a real
  citation style, in the reader view, Live, print and every export.
- Read references from the files people already have: BibTeX/BibLaTeX
  (`.bib`, from Zotero, JabRef, Mendeley, Google Scholar) and CSL-JSON.
- Ship five styles that cover most of our readers: APA 7, Chicago
  author-date (pandoc's default), IEEE, Nature and GB/T 7714-2015. Any other
  of the 10,000+ CSL styles works from a file.
- Number figures, tables and equations and resolve `@fig:id` references,
  pandoc-crossref compatible.
- Stay native and small: no web engine, no bundled runtime, a few hundred
  KB at most.

## Non-goals

- A reference manager. Tinta reads libraries; Zotero, JabRef and friends
  keep them.
- Full BibLaTeX data model fidelity (crossref inheritance across
  entry types, `\cite` commands inside fields). We read what CSL can print.
- LaTeX output of our own. LaTeX goes through the existing pandoc export.
- CSL-M extensions (legal citation) and CSL 1.1 drafts.

## What the writer types

### Front matter

```yaml
---
title: Rivers of ink
bibliography: references.bib          # or a list: [main.bib, extra.json]
csl: apa                              # a bundled name or a .csl path; default chicago-author-date
lang: en-US                           # the locale terms and dates follow (zh-CN, de...)
link-citations: true                  # citations link to their entry (default true in Tinta)
reference-section-title: References   # adds a heading above the bibliography
nocite: |
  @frank2019, @smith2020              # listed in the bibliography though not cited; @* lists all
figureTitle: Figure                   # pandoc-crossref names (defaults shown)
tableTitle: Table
figPrefix: fig.
tblPrefix: tbl.
eqnPrefix: eq.
---
```

Paths are relative to the document's folder. Bundled style names: `apa`,
`chicago-author-date`, `ieee`, `nature`,
`china-national-standard-gb-t-7714-2015-numeric` (alias `gb-t-7714`).
`citation-style:` is accepted as an alias of `csl:`, as pandoc does.

A document with no `bibliography:` (and no inline `references:`) never
treats `@` as a citation. GitHub-style @mentions in READMEs stay text.

### Citations

| Markdown | APA 7 | IEEE |
| --- | --- | --- |
| `[@doe2024]` | (Doe, 2024) | [1] |
| `[@doe2024, p. 4]` | (Doe, 2024, p. 4) | [1, p. 4] |
| `[see @doe2024, pp. 33-35; also @smith2020, chap. 1]` | (see Doe, 2024, pp. 33–35; also Smith, 2020, Chapter 1) | [1, pp. 33–35], [2, ch. 1] |
| `[-@doe2024]` | (2024) | [1] |
| `@doe2024 says` | Doe (2024) says | Doe [1] says |
| `@doe2024 [p. 4] says` | Doe (2024, p. 4) says | Doe [1, p. 4] says |

Rules, all pandoc's:

- A key starts with a letter, digit or `_` and continues with letters,
  digits and the internal punctuation `:.#$%&-+?<>~/`; a trailing
  punctuation mark is not part of the key. `@{odd key}` takes anything up
  to the brace.
- Bracketed citations are separated by `;`. Text before the `@` is the
  prefix; text after the key is the locator and suffix.
- Locators: `p.`/`pp.` page, `chap.` chapter, `sec.`/`§` section, `fig.`,
  `n.` note, `para.`, `vol.`, `l.` line, `v.` verse, `col.`, `bk.`, `pt.`,
  `op.`, `s.v.`, their long forms and plurals. A bare number is a page
  (`[@doe, 33]`). Braces take a locator literally: `[@doe, {xiv-xvi}]`.
- `@key` in running text cites only when the library has that key and the
  `@` does not follow a letter or digit (so `ana@example.com` stays an
  address). Bracketed citations always cite; an unknown key renders as
  `@key?` in the warning colour, with a hover saying why.
- Not inside code spans, code blocks, maths, link targets or URLs.

### Cross-references

```markdown
![Flow of ink through the nib](nib.png){#fig:nib}

Table: Ink densities by brand {#tbl:density}

| Brand | Density |
| ----- | ------- |

$$ E = mc^2 $$ {#eq:energy}

As @fig:nib shows, and [@tbl:density; @eq:energy] confirm...
```

- `{#fig:id}` right after an image makes it a numbered figure, captioned
  "Figure 1: Flow of ink through the nib" under it. The caption is the
  image's alt text; Markdown in it renders.
- A paragraph `Table: caption {#tbl:id}` (or `: caption {#tbl:id}`)
  directly before or after a table captions it, "Table 1: Ink densities by
  brand", above the table.
- `{#eq:id}` after a display equation numbers it like `\label` does (#314);
  `\label{eq:id}` and `\tag` keep working, and `@eq:id` resolves `\label`s
  too.
- `@fig:nib` reads "fig. 1"; `@Fig:nib` (capital) reads "Fig. 1";
  `[@fig:a; @fig:b]` reads "figs. 1, 2"; `[-@fig:nib]` reads "1". The words
  come from the front matter keys above, then from the document's `lang:`
  (中文: 图 1, 表 1, 式 1), then English.
- A Mermaid or PlantUML block becomes a figure the same way, with the info
  string's attributes: ```` ```mermaid {#fig:states caption="States of a nib"} ````.
- Clicking a reference scrolls to its target; hovering shows the target
  (the figure's thumbnail, the table's caption and first rows, the
  equation).
- Unresolved references read `@fig:missing?` in the warning colour.

## What the reader sees

### Citations in the page

A citation draws as its formatted text in the body colour, with the
link colour on hover. Clicking it scrolls to the bibliography entry (with
`link-citations`), and Back returns, as footnote references do. Hovering
shows a card with the full entry for each cited item, its DOI or URL as a
link, and the source file and line of the entry.

Numeric styles that superscript (Nature) draw the numbers as superscript
runs, like footnote markers.

### The bibliography

The entries follow the document's last block, before the footnotes, under
`reference-section-title` when it is set. A pandoc-style `::: {#refs}` div,
when present, receives them instead. When the document's last heading is
"References", "Bibliography", "Works cited", "参考文献" (or the locale's
term), the entries follow it and no title is added.

Each entry is a block with the style's layout: hanging indent (APA,
Chicago), or a number column (IEEE, Nature, GB/T: `second-field-align`).
Italic, bold, small caps, superscript and links come from the style. Each
entry is an anchor (`#ref-doe2024`, pandoc's id) that citations and the
table of contents can reach. The bibliography is part of search, word
count excludes it (as pandoc's word count does), and copy as rich text
keeps its formatting.

### Problems

Nothing fails silently and nothing blocks reading:

- Missing or unreadable bibliography file, unknown style, bad CSL XML,
  BibTeX syntax errors and duplicate keys produce one notification per
  document load ("references.bib, line 42: missing comma after a field"),
  and a status chip in the properties strip that lists them all.
- Unknown keys render as `@key?` (warning colour) with a hover.

### Source and Live

- Source highlights citation keys and cross-reference ids like link
  targets. Hovering a key shows the same card as the reader.
- Live renders a citation as formatted text until the caret enters it,
  then shows the Markdown, as for links.
- Typing `[@` or `@` (in a document with a bibliography) opens a completion
  list of keys, filtered by key, author, year and title as you type; Enter
  inserts the key. Typing `@fig:` lists the figure ids in the document.
  (Phase 2, built: `@tbl:`, `@eq:` and `@sec:` list theirs too, and the
  rail's Cite button opens Zotero's picker, or types `[@` without it.)

### Following changes

The bibliography files and a `.csl` file are followed like the document
(#311): saving `references.bib` from Zotero/Better BibTeX's auto-export
re-renders open documents that use it.

## Export and print

- HTML: citations as `<span class="citation" data-cites="doe2024">` with
  an `<a href="#ref-doe2024">`, the bibliography as
  `<div id="refs" class="references csl-bib-body">` with
  `<div id="ref-doe2024" class="csl-entry">` children, the same markup as
  pandoc's. Figures as `<figure>` with `<figcaption>`, table captions as
  `<caption>`.
- Word: citations as text runs with the style's formatting, the
  bibliography as paragraphs in a "Bibliography" style (hanging indent or a
  tab stop for the number column), captions in Word's "Caption" style with
  a SEQ field, so Word can rebuild a table of figures.
- Print and PDF: drawn as on screen; a paper as [Paper export](#paper-export-phase-3)
  sets it.
- Pandoc formats: Tinta passes `--citeproc` when the document has a
  bibliography, plus `--filter pandoc-crossref` when that filter is on the
  PATH. The Markdown goes to pandoc unchanged, so pandoc's own processing
  produces the same citations.

## Paper export (phase 3)

From front matter, in pandoc's variables so the file builds with pandoc's
LaTeX: `title`, `subtitle`, `shorttitle`, `author` (a string, a list, or
pandoc's author objects, of which the `name` is read), `institute`,
`date`, `abstract` (plain, quoted or a `|`/`>` block), `keywords`,
`thanks`, `lang`, `classoption`, `papersize`, `fontsize` and `geometry`.
`src/paper.cpp` reads them from the YAML the Properties element keeps
(`fm::parse` leaves block scalars opaque and is unchanged).

- A title with authors or an abstract makes a title block in print, PDF
  and Word, in place of the properties strip (the screen keeps the strip):
  the title, subtitle, authors joined with commas and "and", the
  institutes on lines below, the date, an "Abstract" heading and the
  abstract set in from both sides, "Keywords: a, b, c", and the thanks as
  a footnote line under a short rule, marked * on the title. The words
  follow `lang:`. Inline Markdown in the fields keeps emphasis, bold and
  code; maths and citations read as written.
- Print and PDF: one or two columns (`classoption: twocolumn`, or the
  preview's toggle). The title block spans both columns on page 1; the
  text runs down the left column, then the right, each column cut where a
  page would be, figures, tables, code and equations laid out at the
  column's width (wider ones shrink into it). Page numbers centred in the
  bottom margin and running heads in the top one from page 2 (the short
  title or title; the first author's surname, et al. for more), in a
  small muted face, on by default for a paper and toggled in the
  preview. `papersize` (A4, Letter; Legal and A3) and `fontsize` (8 to 14
  pt against the page's 12) set the preview's paper and type size,
  `geometry: margin=` its margins until a chip is picked.
- Word: the title block in Title, Subtitle, Author, Affiliation, Date,
  Abstract Title and Abstract, and Keywords styles; the thanks a footnote
  with a * mark; figure and table captions as "Figure " + `SEQ Figure`
  field + ": ...", tables in a `SEQ Table` sequence, in Word's Caption
  style; the bibliography style as before. A journal's template can
  restyle it all.
- Not built: numbered affiliations matched to authors, figures spanning
  both columns with `{.wide}`, floats (a figure too tall for what is left
  of a column moves to the next and leaves the space), two columns in
  Word.

## Design

### Modules

| Module | Holds | Depends on |
| --- | --- | --- |
| `include/bib.h`, `src/bib.cpp` | the CSL item model; BibTeX/BibLaTeX and CSL-JSON readers; LaTeX-to-rich-text; names and dates | `json.h` |
| `include/json.h`, `src/json.cpp` | the JSON reader Sheets use today, moved out of `sheet_model.cpp` | |
| `include/cite.h`, `src/cite.cpp` | pandoc citation syntax: finding citations in text, locators | |
| `include/xml.h`, `src/xml.cpp` | a small non-validating XML reader (elements, attributes, text, entities, comments, CDATA) | |
| `include/csl.h`, `src/csl.cpp` (+ `csl_*.cpp`) | CSL styles and locales, and the processor | `bib.h`, `cite.h`, `xml.h` |
| `src/scholar.cpp`, `include/scholar.h` | the document side: loading libraries and styles per document, caching, collecting citations and cross-references from the parsed document, giving layout and exports their text | everything above, `markdown.h`, `frontmatter.h` |
| `tools/csl_pack.cpp` | build-time: strips `<info>`, comments and indentation from the bundled styles and locales and compresses them | |
| `include/zotero.h`, `src/zotero.cpp` | phase 2: Better BibTeX's Cite As You Write picker, probed and asked over WinHTTP on a worker thread | |

The first five are plain C++ with no Windows calls, tested as console
programs. Only `scholar.cpp` knows Tinta's document and settings.

### The data model (bib.h)

CSL's: an item has an id, a CSL type, standard/number variables (as CSL
rich text), name variables and date variables. BibTeX entries are mapped
into it on reading, so the processor knows one model.

BibTeX reading:

- `@type{key, field = {value} # "value" # macro, ...}`, `@string`,
  `@preamble` (ignored), `@comment`, text between entries ignored; both
  braces and parentheses as entry delimiters; month macros `jan`..`dec`.
- LaTeX in values: accent commands for Latin letters (`\'`, `` \` ``,
  `\^`, `\"`, `\~`, `\=`, `\.`, `\u`, `\v`, `\H`, `\c`, `\k`, `\r`), the
  special letters (`\ss`, `\o`, `\O`, `\ae`, `\AE`, `\oe`, `\l`, `\L`,
  `\aa`, `\AA`, `\i`, `\j`), `\&` `\%` `\$` `\#` `\_`, `--` en dash, `---`
  em dash, `~` no-break space, `\textit`/`\emph` italic, `\textbf` bold,
  `\textsc` small caps, `\textsuperscript`, `\textsubscript`, `\url{}`,
  `$...$` kept as written. Braces that protect case become
  `<span class="nocase">`; braces that only group vanish.
- Names: `and`-separated; "First von Last", "von Last, First",
  "von Last, Jr, First"; braced names are literal (`{World Health
  Organization}`); `and others` sets et al.
- Type map: article -> article-journal (article-magazine/-newspaper by
  `entrysubtype`), book/mvbook -> book, inbook/incollection/bookinbook ->
  chapter, inproceedings/conference -> paper-conference, proceedings ->
  book, phdthesis/mastersthesis/thesis -> thesis (genre from `type` or the
  kind), techreport/report -> report, manual -> book, online/electronic/www
  -> webpage, patent -> patent, dataset -> dataset, software -> software,
  unpublished -> manuscript, misc -> document.
- Field map: title, subtitle (joined with ": "), journal/journaltitle ->
  container-title, booktitle -> container-title, shortjournal ->
  container-title-short, year/month/day/date -> issued, urldate -> accessed,
  volume, number -> issue (articles) or number, pages -> page, edition,
  publisher/school/institution/organization -> publisher,
  address/location -> publisher-place, series -> collection-title, doi ->
  DOI (without a `https://doi.org/` prefix), url -> URL, isbn -> ISBN, issn
  -> ISSN, note, abstract, keywords, language/langid -> language, chapter
  -> chapter-number, eventtitle -> event-title, venue -> event-place,
  version, howpublished -> medium or URL, type -> genre, editor, translator,
  bookauthor -> container-author.

CSL-JSON reading takes the array as CSL defines it; dates as `date-parts`,
`raw`, `literal`, EDTF strings, `circa`, `season`.

### Citations in the document (cite.h, scholar.cpp)

After md4c and Tinta's own inline passes, `scholar.cpp` walks the text
runs of each paragraph, heading, list item, table cell and footnote, asks
`cite::find` for citations, and replaces their bytes with a citation span
that carries the citation's index in document order. Citations and
cross-references are collected in one pass in document order; then the CSL
processor runs once for the whole document and every citation span gets its
formatted runs. Layout draws the runs (italics, small caps and superscript
map onto the inline styles Tinta already has).

Cross-references are numbered in the same pass: images with `{#fig:...}`,
table captions, equations with `{#eq:...}` or `\label`, diagrams with
`#fig:` attributes. Numbers follow document order.

### The CSL processor (csl.h)

A CSL 1.0.2 processor. It must support everything the five bundled styles
use, which a survey of them shows to be:

- Rendering elements: `text` (variable, macro, term, value; forms long,
  short, verb, verb-short, symbol; plural; quotes), `number` (numeric,
  ordinal), `label` (forms, plural, text-case), `names` with `name`
  (and = text/symbol, delimiter, delimiter-precedes-last always/never,
  et-al-min/use-first/use-last, form short, initialize, initialize-with,
  name-as-sort-order first/all, sort-separator), `name-part`
  (text-case), `et-al`, `substitute`; `date` (form text/numeric,
  date-parts, delimiter) with `date-part` (name, form long/short/numeric/
  numeric-leading-zeros, range-delimiter, affixes); `group` (delimiter,
  affixes, formatting, suppressed when every variable it calls is empty);
  `choose` with `if`/`else-if`/`else` testing type, variable,
  is-numeric, is-uncertain-date, locator, position, disambiguate, with
  match all/any/none.
- Style options: class in-text (note styles in phase 2),
  demote-non-dropping-particle never/sort-only, initialize-with(-hyphen),
  name-as-sort-order, names-delimiter, page-range-format expanded and
  chicago-16 (and minimal, minimal-two, chicago-15), sort-separator,
  default-locale; citation: collapse citation-number/year,
  after-collapse-delimiter, disambiguate-add-givenname/-names/-year-suffix,
  givenname-disambiguation-rule primary-name(-with-initials), et-al
  options; bibliography: hanging-indent, second-field-align flush,
  line-spacing, entry-spacing, et-al options and et-al-use-last.
- Sorting with `key` by variable or macro, names-min/names-use-first,
  ascending and descending, empty values last.
- Formatting: font-style italic, font-weight bold, vertical-align sup,
  text-decoration, strip-periods, text-case lowercase, uppercase,
  capitalize-first, capitalize-all, sentence, title (English titles only;
  `nocase` spans immune), quotes with the locale's open/close quotes and
  inner quotes, punctuation-in-quote, affixes and delimiters without
  doubled punctuation.
- Locales: the style's own `<locale>` blocks first (matching language,
  then none), then the locale file for the document language, its base
  dialect (zh-TW -> zh-CN, en-GB -> en-US), then en-US. Bundled: en-US and
  zh-CN. Terms with forms, plurals and gender; date formats; ordinals;
  style options.
- Citation numbers in bibliography order, or first-cited order without a
  bibliography sort.
- Disambiguation: add names, then given names or initials, then year
  suffixes (a, b...), then `disambiguate="true"` branches.
- Position: first, subsequent, ibid, ibid-with-locator (for styles that
  test them; note styles in phase 2).
- Author in text (`@key`): the author part (the first `names` of the
  citation layout, or its substitute) followed by the citation with the
  author suppressed: "Doe (2024)", "Doe [1]".
- Rich text in variables (`<i>`, `<b>`, `<sup>`, `<sub>`, small caps,
  `nocase`), with CSL's flip-flop for italics inside italics.

Output is `csl::Rich`, runs of text with flags. `csl::html` and
`csl::plain` serve exports and tests.

The processor runs once per document change on the parse thread; it caches
the parsed style and locales and the library per file (keyed by path and
modification time). A document of 200 citations and 300 references formats
in well under 10 ms.

### Bundled styles

`resources/csl/` holds the five styles and two locales from the official
CSL repositories (CC BY-SA 3.0; credited in `resources/csl/README.md`).
`tools/csl_pack` strips each to its working parts and compresses it with
the Windows compression API (XPRESS Huffman); the results are RCDATA in
`tinta.rc`. At first use, `Decompress` restores the XML. Budget: under
100 KB added to `tinta.exe` for all seven files.

## Testing

- `bib_tests`: BibTeX syntax (macros, concatenation, nested braces,
  comments, errors with lines), LaTeX decoding, name splitting (von, Jr,
  literals, `and others`), the type and field map, CSL-JSON, dates.
- `cite_tests`: the citation grammar from pandoc's manual, locators,
  e-mail addresses, keys with punctuation, cross-reference keys.
- `csl_tests`: each bundled style against hand-checked output for a
  journal article, a book, an edited book chapter, a conference paper, a
  thesis, a report, a web page and a dataset; two authors, three, and
  twenty-one; disambiguation (same author and year, same surname);
  citation collapsing; locators; author-in-text; a missing key; the zh-CN
  locale with GB/T 7714 (Chinese and English entries).
- `scholar` in-app tests: the fixture documents' layout (citation runs,
  bibliography blocks, figure and table numbers), hover, click to the
  entry and back, reload when the `.bib` changes.
- Fixtures in `tests/fixtures/scholar/`: a mixed paper with headings,
  lists, a table, a quote, code, maths, a Mermaid figure and footnotes
  around the citations; one document per bundled style; a broken
  bibliography; a document with @mentions and no bibliography.

## Phases

1. **Citations and cross-references** (built): the bib, cite, xml
   and csl modules; the bundled styles; citations and the bibliography in
   the reader view, print, HTML and Word; figures, tables and equations
   numbered and referenced; hover cards and click-through; diagnostics;
   pandoc export flags.
2. **Writing aids**: `[@` completion in Live and Source; Zotero through
   Better BibTeX (`http://127.0.0.1:23119/better-bibtex/cayw?format=pandoc`
   for its picker, and its auto-exported `.bib`/`.json` followed live);
   note styles (Chicago notes-bibliography: citations become footnotes);
   inline `references:` in front matter; section numbers and `@sec:`; a
   "Cite" button on the tool rail. Built: the completion (with `@sec:`
   ids listed), the picker and the Cite button, note styles, sections
   and following the library; inline `references:` to come.
3. **Paper export** (built): title block, authors and affiliations,
   abstract, two-column PDF, running heads and page numbers, Word paper
   styles and SEQ fields.

## Decisions

- Pandoc's syntax and defaults (Chicago author-date when no `csl:`), so
  files stay portable.
- `@key` cites only keys the library knows; brackets always cite.
- A native CSL processor rather than hand-written styles: any journal's
  CSL file works, and the five bundled ones are the official files.
