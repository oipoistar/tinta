# Code block spacing: $E = mc^2$

Regression fixture for [#196](https://github.com/oipoistar/tinta/issues/196).
A final newline should end the last code line. It should not add a blank row.

## One line

```text
single line
```

This **paragraph** should follow the code block with normal spacing.

## Highlighted code

```cpp
int answer = 42;
```

## Two lines

```text
first line
second line
```

## Intentional blank lines

The next block has an internal blank line and one blank line after `last line`.
Both should remain visible.

```text
first line

last line

```

## Empty fence

```text
```

The empty block keeps its existing minimum height and copy-button area.

## Mixed table and math

| Item | Expression | Literal code |
| --- | --- | --- |
| Fraction | $\frac{1}{2}$ | `value = 0.5` |
| Root | $\sqrt{x^2+y^2}$ | `sqrt(x*x+y*y)` |

> A quote with *emphasis* and inline math $a+b=c$.
>
> ```text
> quoted line
> ```

- A list item before a nested block:

  ```python
  print("one line")
  ```

- Another item follows the nested block.

$$
\begin{bmatrix}1 & 2 \\ 3 & 4\end{bmatrix}
\begin{pmatrix}x \\ y\end{pmatrix}
= \begin{pmatrix}x+2y \\ 3x+4y\end{pmatrix}
$$

## Literal TeX and a wide line

```latex
\frac{1}{2} + \sqrt{x} % This stays literal code.
```

```text
wide line: 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz 0123456789 abcdefghijklmnopqrstuvwxyz
```

## Final heading

The [existing Markdown control](markdown-regression-control.md), table, math,
copyable source text and content after each block should remain intact.
