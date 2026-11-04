---
title: Ink that remembers
author:
  - name: Ana Lima
    affiliation: University of Ink
    orcid: 0000-0002-1825-0097
    corresponding: true
  - name: Bo Chen
    affiliation: Paper Institute
csl: apa
link-citations: true
reference-section-title: References
references:
- type: article-journal
  id: WatsonCrick1953
  author:
  - family: Watson
    given: J. D.
  - family: Crick
    given: F. H. C.
  issued:
    date-parts:
    - - 1953
      - 4
      - 25
  title: 'Molecular structure of nucleic acids: a structure for
    deoxyribose nucleic acid'
  title-short: Molecular structure of nucleic acids
  container-title: Nature
  volume: 171
  issue: 4356
  page: 737-738
  DOI: 10.1038/171737a0
  URL: https://www.nature.com/articles/171737a0
  language: en-GB
- type: book
  id: lima2021
  author:
  - family: Lima
    given: Ana
  title: The chemistry of *Sepia* ink, from the sea to the page
  publisher: Tinta Press
  publisher-place: Lisbon
  edition: 2
  issued: 2021
- id: chen2019
  type: chapter
  author: [{family: Chen, given: Bo}]
  editor:
    - {family: Okafor, given: Ana}
  title: "Why paper drinks: capillary rise in cellulose"
  container-title: Handbook of Writing Surfaces
  publisher: Paper Institute
  page: 33-51
  issued: 2019-05
  URL: https://example.org/handbook/chen_capillary_rise
tags: [scholar, fixture, yaml]
---

# Ink that remembers

This note keeps its references in its front matter, under `references:`,
as pandoc reads them: there is no `.bib` file beside it. The double helix
was first described on a single page [@WatsonCrick1953], and @lima2021
traces *Sepia* ink from the animal to the inkwell.

## What the references cover

| Key | Kind | Cited as |
| --- | ---- | -------- |
| WatsonCrick1953 | article | `[@WatsonCrick1953]` |
| lima2021 | book | `@lima2021` |
| chen2019 | chapter | `[see @chen2019, pp. 33-35]` |

- A journal article, from pandoc's manual, its date written as `date-parts`;
- a **book** whose title holds Markdown emphasis;
- a chapter whose date is written `2019-05` [see @chen2019, pp. 33-35].

> Paper drinks ink by capillary rise, as a chapter on cellulose explains
> [@chen2019; @lima2021, chap. 3].

The same references could live in a file of their own:

```yaml
references:
- id: lima2021
  type: book
  title: The chemistry of *Sepia* ink
```

Code such as `@lima2021` and the address ana@example.org stay text; see
[pandoc's manual](https://pandoc.org/MANUAL.html#specifying-bibliographic-data).
