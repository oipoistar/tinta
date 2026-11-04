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

## Citing as you type

In Live and in Source, typing `[@`, or an `@` at the start of a word, opens
a list of the bibliography's keys under the caret. Each row is a key and
the work it names: the first author (both of two, "et al." for more), the
year and the title. Typing narrows the list by the key, an author's name
(`muller` finds Müller), the year or a word of the title, the best matches
first. The arrow keys or the mouse choose, Enter or Tab puts the key in,
with the `]` a `[@` still needs, and Esc closes the list and keeps what was
typed. An e-mail address, an `@` inside a word and code open nothing, and
neither does an `@` in a document that names no bibliography.

`@fig:`, `@tbl:`, `@eq:` and `@sec:` list the document's own figures,
tables, equations and sections, in any document: the ids their `{#fig:id}`
attributes and `\label{eq:id}` give them, each with its caption or heading.

## Zotero

The **Cite** button on the edit rail, **@**, opens Zotero's own citation
picker when Zotero runs with the [Better BibTeX](https://retorque.re/zotero-better-bibtex/)
add-on, and puts the citation picked, such as `[@doe2024; @smith2020]`, at
the caret of the document that asked, even if another tab came to the front
meanwhile. Cancelling the picker changes nothing. Without Zotero the button
types `[@` and opens the list above. Better BibTeX can also keep a `.bib`
file exported as the library changes: name that file as the bibliography
and open documents follow it.

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
  Caption style.
- **Print and PDF** are as on screen.
- **LaTeX and other pandoc formats:** Tinta hands the document to pandoc
  with `--citeproc`, and `--filter pandoc-crossref` when that filter is
  installed.

Try the [paper](../tests/fixtures/scholar/paper.md), its
[Chinese companion](../tests/fixtures/scholar/paper-zh.md), the
[citation syntax](../tests/fixtures/scholar/citation-syntax.md), the
[numbered figures](../tests/fixtures/scholar/crossref.md) and
[citing as you type](../tests/fixtures/scholar/completion.md). The design
is in [docs/specs/scholar.md](specs/scholar.md).
