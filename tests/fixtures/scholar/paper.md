---
title: How ink finds paper
author: [Ana Okafor, Lars Lindqvist]
bibliography: references.bib
csl: apa
link-citations: true
reference-section-title: References
tags: [scholar, fixture]
---

# How ink finds paper

Writing tools have been studied for a long time [@knuth1984; @brooks1995],
but the physics of a nib meeting paper still surprises. @okafor2021 measured
capillary flow in split nibs, and the field has grown since [@doe2024;
@doe2024b; see also @garcia2022, pp. 3-5].

## Background

Two lines of work meet here:

1. **Ink chemistry**, from iron gall recipes [@lafontaine1668] to modern
   dyes [@who2023];
2. **paper structure**, where fibres wick ink away from the line
   [@smith2019; @smith2020, chap. 2].

> A good ink forgives a bad pen; a good paper forgives a bad ink.
> Nothing forgives both [@frank2019, p. ix].

Large teams have measured the same effect at scale [@consortium2022], and
the datasets are public [@lindqvist2023; @rcore2024].[^data]

[^data]: The measurements cited here are in @lindqvist2023, re-analysed
    with R [@rcore2024].

## Method

The flow through the slit follows Poiseuille's law:

$$ Q = \frac{\pi r^4 \Delta p}{8 \mu L} $$ {#eq:flow}

and the rise in a fibre follows @eq:rise below, as @fig:nib shows.

![The nib, split along its slit](nib.png){#fig:nib}

$$ h = \frac{2 \gamma \cos\theta}{\rho g r} $$ {#eq:rise}

Table: Inks measured, by density {#tbl:inks}

| Ink | Density (g/ml) | Source |
| --- | -------------: | ------ |
| Iron gall | 1.020 | [@lafontaine1668] |
| Carbon | 1.010 | @calvino1981 |
| Dye | 1.002 | [-@hart2023] |

```python
def flow(radius, pressure, viscosity, length):
    return 3.14159 * radius**4 * pressure / (8 * viscosity * length)
```

```mermaid
flowchart LR
    Ink --> Slit --> Paper
```

## Results

The densities in @tbl:inks agree with earlier work [@macfarlane2024],
and [@eq:flow; @eq:rise] fit the measured flow (@Fig:nib). Writers who
e-mail okafor@example.org about it, or tag @inkfan on social media, stay
text: neither is a citation.

## Discussion

What remains open is the role of paper sizing [@tanaka2018; @vaswani2017;
@devlin2018], and whether software can predict feathering [@vanrossum2009].
