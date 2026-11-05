# Selecting words before a table

Double-click any word on this page, or triple-click a line: the selection
must land on what was clicked, at any window width (#318). Each table below
holds a cell Tinta measures before drawing it, and that measuring used to
move the words of the paragraphs above the table.

Paragraph preceding a plain table, long enough to wrap onto a second line
when the window is narrow.

| One | Two |
| --- | --- |
| Plain | Word |

Paragraph preceding a table with **bold** text in a cell, long enough to
wrap onto a second line when the window is narrow.

| One | Two |
| --- | --- |
| **Bold** | Word |

Paragraph preceding a table whose cell wraps: its column is narrower than
the sentence inside it.

| One | Two |
| --- | --- |
| Plain | A cell holding a sentence long enough to wrap inside its column, even when the window is wide |

## Styled cells

- A list item with *emphasis* and a [link](https://example.com)
- Another item with `inline code`

> A quote before a table with *italic*, [a link](#styled-cells) and `code`
> in its cells.

| Style | Cell |
| --- | --- |
| *italic* | [a link](#styled-cells) |
| `code` | ***both*** and a few more words to wrap |

```python
print("a code block between the tables and the last paragraph")
```

The last paragraph, after every table, selects its own words too.
