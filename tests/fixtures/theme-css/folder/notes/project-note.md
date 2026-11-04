---
title: A project note
tags: [themes, folder]
cssclasses: [wide]
---

# A project note

This note sits two folders under `tinta.css`, which styles it whatever theme
is in use: blue headings with a rule under the second level, airier lines,
and its own list markers. The **chip** at the top right says so; click it to
see the note as the theme alone draws it, and again to bring the folder's
sheet back. The front matter's `cssclasses: [wide]` puts `.wide` on the
page, which the sheet uses to lift its reading column.

## What the folder sets

- Headings in the folder's blue
  - with a rule under the second level
- Markers on every list
- A [link](https://example.com) keeps the theme's colour

1. Numbered items
2. take the marker colour too

| Rule | Where it comes from |
| --- | --- |
| `h1, h2 { color }` | the folder's sheet |
| `th { background }` | the folder's sheet |
| the rest | the theme in use |

> A quote, whose bar takes the folder's colour, with *emphasis* and
> `code` inside.

```python
def styled_by(folder):
    return folder / "tinta.css"
```

---

Saving `tinta.css` restyles this note at once.
