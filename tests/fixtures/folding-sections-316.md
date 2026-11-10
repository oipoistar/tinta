---
title: Folding sections
tags: [folding, reading]
---

# Folding sections

Click the arrow beside a heading to fold its section, and again to open it
(#316). The arrow shows while the pointer is on the heading's line, and
stays beside a folded heading. **Settings > Appearance > Fold sections**
turns the arrows off, or opens every document folded.

Links go to [a heading inside a fold](#deep-inside) and to
[an anchor inside one](#hidden-anchor), opening what hides them.[^1]

## Lists and emphasis

- A list with **bold**, *italic* and `code`
  - nested, with a [link](https://example.com)
- [ ] an open task
- [x] a done task

1. Numbered
2. Again

## Tables

| Fold | What it hides |
| --- | --- |
| `## Tables` | this table and everything up to the next second-level heading |
| **Bold cell** | a cell with enough words to wrap inside its column at a narrow width |

A paragraph after the table, holding a word found nowhere else: zanzibar.

### Deep inside

A heading under a heading: a fold inside a fold.

<a id="hidden-anchor"></a>

> A quote with *emphasis* and a [link back to the top](#folding-sections).

```python
def fold(section):
    return section.heading
```

## Empty section
## Tables

The second "Tables" heading takes the slug `tables-1`, folded or not. The
heading above it has nothing under it, so it has no arrow.

---

# A second part

Text under the second top-level heading, with a footnote.[^2]

## Last section

The footnotes below stay when this section folds.

[^1]: The first footnote.
[^2]: The second footnote.
