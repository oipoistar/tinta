# Matrices $A\longrightarrow B$

This document checks new math inside existing Markdown. No formula should cover surrounding text, a table border, or the following block.

## Heading with $\begin{bmatrix}1&2\\3&4\end{bmatrix}$ and text

The paragraph after the heading must remain separate. **Bold** and *italic* text still work.

### Fractions $\dfrac{1}{1+x^2}$ in a smaller heading

## Tables and alignment

| Left label | Centered math $x^2$ | Right result |
| :--- | :---: | ---: |
| **Matrix** | $\begin{bmatrix}1&2\\3&4\\5&6\end{bmatrix}$ | $\longrightarrow 6$ |
| Fraction | $\dfrac{1}{1+\dfrac{1}{x}}$ | $\sqrt[3]{8}=2$ |
| Norm with an escaped pipe | $\left\|x\right\|$ | $\mathbb{R}$ |
| Plain following row | `a & b` and *text* | 42 |

The paragraph after the table must not overlap its final row.

## Lists, links and inline formatting

- A matrix $\begin{pmatrix}a&b\\c&d\end{pmatrix}$ inside a list item, with enough surrounding words to wrap in a narrow window without crossing the next item.
  - Nested item with $\binom{n}{k}$ and **bold text**.
- The following item remains visible with [a local fixture link](math-compatibility.md).

1. First compute $\sum_{\substack{i>0\\j>0}}a_{ij}$.
2. Then compare $\boxed{x=2}$ with `x = 2`.

- [x] Matrix example covered.
- [ ] Inspect $\underbrace{a+b+c}_{\text{sum}}$ next to a checkbox.

**Bold $\begin{bmatrix}1\\2\\3\end{bmatrix}$ continuation**, *italic $\dfrac{x}{y}$ continuation*, ~~old $x^2$ result~~, ==highlighted $\sqrt{x}$ result==, and [$\mathbb{R}$ link](math-compatibility.md) share a paragraph with `inline code`. The next wrapped line must remain readable, including its link underline and code background.

## Quotes and display equations

> A quoted derivation with $\alpha\leq\beta$.
>
> $$\begin{aligned}a+b&=c\\x&=y+z\end{aligned}$$
>
> The final quoted paragraph stays inside the quote.

> [!NOTE]
> A case distinction follows $f(x)$ without hiding this callout text.
>
> $$f(x)=\begin{cases}x^2&\text{if }x>0\\0&\text{otherwise}\end{cases}$$

---

## Code remains literal

Inline code `$\begin{bmatrix}1&2\\3&4\end{bmatrix}$` must keep its dollar signs and backslashes.

```latex
$$\begin{bmatrix}1&2\\3&4\end{bmatrix}\longrightarrow 6$$
\newcommand{\norm}[1]{\left\lVert#1\right\rVert}
```

## Diagram beside math

```mermaid
flowchart LR
    A[Input matrix] --> B[Transform] --> C[Result]
```

$$\newcommand{\norm}[1]{\left\lVert#1\right\rVert}\norm{\frac{x}{y}}\geq0$$

The final paragraph, **final emphasis**, and [final link](math-compatibility.md) must all survive after the diagram and equation.
