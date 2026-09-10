# Superscript x^2^ and subscript H~2~O

Markdown scripts should use smaller text from the document font. Superscripts
sit above the normal baseline; subscripts sit below it.

Plain Markdown: x^2^ and H~2~O. Ordinary text: x2 and H2O.

Unicode comparison: x² and H₂O. LaTeX comparison: $x^2$ and $H_2O$.

Longer scripts: a^123^, a^word^, a~123~, and a~word~.

## Mixed styles and links

**Bold x^2^**, *italic H~2~O*, ~~struck x^2^~~, ==highlight==,
and [linked x^2^](#mixed-styles-and-links). Inline code stays literal: `x^2^ H~2~O`.

| Context | Superscript | Subscript | Math |
| --- | --- | --- | --- |
| Plain | x^2^ | H~2~O | $x^2$ |
| Emphasis | **a^123^** | *a~word~* | $H_2O$ |

> A quote with x^2^, H~2~O, **bold**, `code`, and $a+b=c$.
>
> This second paragraph must keep its normal spacing.

- List item with x^2^ and H~2~O.
- A [link](#mixed-styles-and-links), **bold**, and *italic* beside a^word^.

### Smaller heading x^2^ and H~2~O

```text
x^2^ and H~2~O remain literal inside code fences.
```

$$
\frac{x^2 + y^2}{H_2O}
$$

All surrounding content must remain visible, including this final paragraph.
