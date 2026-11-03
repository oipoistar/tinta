---
title: Citation syntax
bibliography: references.bib
lang: en-US
---

# Citation syntax

Pandoc's citations in a mixed document (docs/specs/scholar.md). Every key
here is in `references.bib`; the e-mail addresses, @mentions and code stay
text. See [pandoc's manual](https://pandoc.org/MANUAL.html#citation-syntax).

## In brackets

Literate programming began with a paper [@knuth1984], and a book of essays
says why big teams are slow [see @brooks1995, pp. 33-35; also @smith2020,
chap. 1].

Two works at once [@doe2024; @doe2024b], a locator in braces
[@lafontaine1668, {xiv-xvi}], and a suffix after the pages
[@garcia2022, pp. 3-5, 7 and *passim*].

Only the year: Doe shows why nibs skip [-@doe2024b].

## In the text

@vaswani2017 changed the field, and @devlin2018 [p. 4] built on it.
@okafor2021 [chap. 2, n. 3] measured capillary flow.

> As @frank2019 puts it in her introduction [@frank2019, p. ix], pens
> have makers.

1. @zhang2020 segments Chinese words.
2. 王五的教材 [@wang2019, 第 3 章] is in Chinese, and so is [@li2021].
3. Organisations write too [@who2023; @rcore2024, sec. 2].

| Work     | Cited as           |
| -------- | ------------------ |
| Dataset  | [@lindqvist2023]   |
| Software | @rcore2024         |
| Webpage  | [@macfarlane2024]  |

Figures and tables are referred to the same way [@fig:nib; @Tbl:density].

## Not citations

Write to ana@example.com or ping @nobody on GitHub; `@knuth1984` in code
stays code, and so does \@knuth1984 escaped. [This bracket has no key.]
An @unknown2099 key stays text in running prose.

```text
@knuth1984 in a code block
```
