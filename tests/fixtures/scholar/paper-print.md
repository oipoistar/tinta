---
title: "Ink on the move: capillary flow in split nibs"
subtitle: A study in two columns
shorttitle: Ink on the move
author:
  - Ana Okafor
  - name: Lars Lindqvist
    affiliation: Institute of Paper Science
institute:
  - Department of Writing Instruments, University of Lagos
  - Institute of Paper Science, Uppsala University
date: 4 November 2026
abstract: |
  Fountain pens move ink by capillary action alone. We measure the flow
  through split nibs of three widths and set it beside *Poiseuille's law*,
  then follow the ink into paper of three sizings.

  The flow rises with the square of the slit's width, as the law says;
  the paper, not the nib, decides how far a line feathers. The data and
  the `flow()` model are public.
keywords: [capillary flow, fountain pens, "paper sizing, feathering"]
thanks: We thank the Tinta reading group for lending the nibs.
bibliography: references.bib
csl: apa
link-citations: true
reference-section-title: References
classoption: twocolumn
papersize: a4
fontsize: 11pt
tags: [scholar, fixture, print]
---

# Introduction

Writing tools have been studied for a long time [@knuth1984; @brooks1995],
but the physics of a nib meeting paper still surprises. @okafor2021
measured capillary flow in split nibs, and the field has grown since
[@doe2024; @doe2024b; see also @garcia2022, pp. 3-5]. This paper asks two
questions of the same pen: how fast the ink leaves the nib, and how far it
travels once it reaches the paper.

Two lines of work meet here:

1. **Ink chemistry**, from iron gall recipes [@lafontaine1668] to modern
   dyes [@who2023];
2. **paper structure**, where fibres wick ink away from the line
   [@smith2019; @smith2020, chap. 2];
3. *software*, which models both and predicts feathering [@vanrossum2009].

> A good ink forgives a bad pen; a good paper forgives a bad ink.
> Nothing forgives both [@frank2019, p. ix].

Large teams have measured the same effect at scale [@consortium2022], and
the datasets are public [@lindqvist2023; @rcore2024].[^data]

[^data]: The measurements cited here are in @lindqvist2023, re-analysed
    with R [@rcore2024].

# Method

The flow through the slit follows Poiseuille's law, where $r$ is the
slit's half-width and $\mu$ the ink's viscosity:

$$ Q = \frac{\pi r^4 \Delta p}{8 \mu L} $$ {#eq:flow}

and the rise in a fibre follows @eq:rise, as @fig:nib shows for the
medium nib.

![The medium nib, split along its slit](nib.png){#fig:nib}

$$ h = \frac{2 \gamma \cos\theta}{\rho g r} $$ {#eq:rise}

Each nib wrote a line of 20 cm on each paper at a steady 4 cm/s; a camera
over the page timed the ink's front every 10 ms. We fitted the model below
to every run:

```python
def flow(radius, pressure, viscosity, length):
    """Poiseuille flow through a slit, in ml/s."""
    return 3.14159 * radius**4 * pressure / (8 * viscosity * length)
```

The ink's path, from the reservoir to the sheet:

```mermaid
flowchart LR
    Reservoir --> Feed --> Slit --> Paper
```

# Results

Table: Flow by nib width {#tbl:flow}

| Nib | Slit (mm) | Flow (ml/min) | Feathering |
| --- | --------: | ------------: | ---------- |
| Fine | 0.04 | 0.012 | low |
| Medium | 0.06 | 0.061 | moderate |
| Broad | 0.09 | 0.307 | high |

The flows in @tbl:flow rise with the fourth power of the slit, as
[@eq:flow; @eq:rise] predict, within 4 % on every nib. Sizing mattered more
than the nib: on unsized paper even the fine nib feathered, and on heavily
sized paper the broad nib's line stayed within 0.1 mm of its edge.

- Unsized paper wicked the ink furthest, along its fibres.
- Lightly sized paper held the line for the fine and medium nibs.
- Heavily sized paper held every line, at the cost of drying time.

# Discussion

What remains open is the role of paper sizing over time [@tanaka2018], and
whether models trained on many papers [@vaswani2017; @devlin2018] can
predict feathering from a photograph of the sheet. Writers who e-mail
okafor@example.org about it, or tag @inkfan, stay text.
