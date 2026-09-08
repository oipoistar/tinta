# Inline-code color: `inline_heading` and $E = mc^2$

Theme regression for [#197](https://github.com/oipoistar/tinta/issues/197).
Use the accompanying `inline-code-themes.ini` in an isolated portable test folder.

Inline `inline_plain` should use the inline-code color. The fenced text below
should keep the base code color. **Bold `inline_bold`** and *italic `inline_italic`*
keep their formatting. A [`linked_code`](https://example.com) span keeps the link color.

```text
block_plain
```

```cpp
int identifier = 42; // highlighted code retains its syntax colors
```

## Table with inline code and math

| Style | Inline code | Math |
| --- | --- | --- |
| Neutral body text | `inline_table` | $\frac{1}{2}$ |
| **Emphasis** | `inline_table_second` | $\sqrt{x^2+y^2}$ |

> A quote with `inline_quote`, **bold text** and $a+b=c$.

- A list item containing `inline_list`.
- Another item with a [normal link](https://example.com).

$$
\begin{bmatrix}1 & 2 \\ 3 & 4\end{bmatrix}
\begin{pmatrix}x \\ y\end{pmatrix}
= \begin{pmatrix}x+2y \\ 3x+4y\end{pmatrix}
$$

## Literal code and inheritance

```latex
\frac{1}{2} + \sqrt{x} % Fenced TeX stays literal and uses the block palette.
```

With the legacy theme, inline spans and plain fenced text share `code` as before.
With the separate-color themes, only inline spans change. Reopen the theme editor,
save the theme, restart Tinta and verify that the separate color persists.

The [existing mixed Markdown control](markdown-regression-control.md) should
retain its layout and formatting with themes that omit the new key.
