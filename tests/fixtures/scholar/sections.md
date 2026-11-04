---
title: Numbered sections
numbersections: true
---

# Numbered sections

The title above stands alone at the first level, so it is not numbered.
@sec:method explains the setup and @sec:results what came of it; the
appendix is in [@sec:appendix]. Two at once: [@sec:method; @sec:results].

## Method {#sec:method}

A paragraph, a list and a table under a numbered section.

- one
- two

| Ink | Density |
| --- | ------: |
| Iron gall | 1.020 |

### Apparatus {#sec:apparatus}

Measured with a balance, as @Sec:apparatus says.

## Results {#sec:results}

> A quote in the results.

```python
print("results")
```

## Acknowledgements {-}

Unnumbered, and the next section still counts on.

## Appendix {#sec:appendix .unnumbered}

Unnumbered too. A heading that only looks like attributes stays as it is:

## Sets {a, b}

Last line.
