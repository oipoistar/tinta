---
title: Footnote compatibility
tags: [notes, regression]
---

# Footnotes with mixed Markdown

A named reference[^detail], a short note[^1], and the named reference again[^detail].
This keeps **bold**, *emphasis*, ==highlight==, H~2~O and x^2^ alongside $a^2+b^2=c^2$.

## Notes in other structures

| Item | Explanation |
| :--- | :--- |
| Table reference[^1] | A table with `inline code` |
| Another[^detail] | **Bold** and $\frac{1}{2}$ |

> A quote with a note[^1] and [an external link](https://example.com).

- A list reference[^detail].
- Another item with *emphasis*.

## Literal and unresolved references

Missing[^missing], escaped \[^1], inline code `[^detail]`, and a link [literal[^1]](https://example.com).

```markdown
[^fake]: This is an example, not a definition.
Literal reference: [^1]
```

$$
\sum_{k=1}^{n} k = \frac{n(n+1)}{2}
$$

Final body paragraph. Notes should appear after this paragraph.

[^1]: Short note with **bold** and a [link](https://example.com/notes).

[^detail]: A longer note with *emphasis*, ==highlight== and $x_i^2$.

    A second paragraph in the same note.

    - A nested list item.
    - A second item with `code`.

    | Note table | Value |
    | :--- | ---: |
    | Preserved | 42 |

    ```sql
    SELECT 42; -- syntax remains available inside a note
    ```

[^unused]: This unreferenced definition remains in the source.
