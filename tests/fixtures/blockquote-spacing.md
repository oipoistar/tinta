# Blockquote spacing

Compare each quote's vertical bar with the last content line. The paragraph gap
below a quote should separate it from the next block.

> One line with `inline code`, **bold text** and $a+b=c$.

Following paragraph.

> First paragraph.
>
> Second paragraph with *emphasis* and a [link](https://example.com).

| Existing feature | Check |
| --- | --- |
| Table | Stays below the quote |
| Inline math | $\frac{1}{2}$ |

> A quote with two explicit lines.\
> This is the second line.

- A list item after the quote.
- Another item with `code`.

```text
A fenced block after the list.
```

## Nested quotes and callouts

> Outer paragraph.
>
> > Inner paragraph with $\sqrt{x^2+y^2}$.

The inner and outer bars should end together, with a gap before this paragraph.

> [!NOTE]
> A callout with `inline code` and **bold text**.

Following the callout.

## Quotes ending in other blocks

> ```text
> A code block with an intentional blank row below.
>
> ```

The bar should cover the code background, including its padding and blank row.

> | Quoted table | Math |
> | --- | --- |
> | Last row | $\frac{1}{2}$ |

The bar should cover the table, with spacing outside the quote.

> $$
> \begin{bmatrix}1 & 2 \\ 3 & 4\end{bmatrix}
> $$

Following display math.

> - A quoted list item.
> - A second item.

Following the quoted list.

## Final heading

All surrounding content should keep its position when only quote bars change.
