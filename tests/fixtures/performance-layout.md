# Alignment and wrapping

A paragraph before the tables, with **bold**, *emphasis*, `inline code`, and a [link](https://example.com).

| Left | Center | Right |
|:-----|:------:|------:|
| short | several words in this centered cell wrap at a narrow width | 1234 |
| **bold words** | *emphasis* and [a link](https://example.com) | `code` |
| next row | center | aligned to the right |

> A quote after the table with **bold text**, followed by a list.

- First item with `code`.
- Second item with *emphasis*.

## Joined cells

<table>
<tr><th>Group</th><th>Task</th><th>Notes</th></tr>
<tr><td rowspan="2">Shared group with several words</td><td>First</td><td>A longer cell that wraps onto several lines when the window narrows.</td></tr>
<tr><td>Second</td><td><b>Bold</b> and <i>italic</i></td></tr>
<tr><td colspan="2">A joined cell across two columns</td><td>Last</td></tr>
</table>

## Another aligned table

| Name | Value |
|:----:|------:|
| a centered name | 42 |
| last row | 1000 |

```cpp
const auto result = calculate("a long code line that also wraps at narrow widths");
```

The final paragraph must remain below the tables and code. Unicode: café, 中文, 日本語, 한글, 😀.
