# Native math compatibility

Tinta renders a growing subset of LaTeX mathematics using DirectWrite and Direct2D. Use `$...$` for inline math and `$$...$$` for a centered display equation. For large matrices and derivations, display math gives the clearest layout. Paragraphs containing tall inline equations use additional line spacing.

## Environments

| Environment | Behavior |
| --- | --- |
| `matrix`, `pmatrix`, `bmatrix`, `Bmatrix`, `vmatrix`, `Vmatrix` | Centered cells, with no delimiters, parentheses, brackets, braces, bars or double bars respectively |
| Starred matrix variants, for example `bmatrix*` | Optional `[l]`, `[c]` or `[r]` cell alignment |
| `smallmatrix` | Smaller cells for inline notation |
| `cases`, `rcases` | Two left-aligned columns with a brace on the left or right |
| `aligned`, `split` | Alternating right/left alignment for equation steps; `split` has at most two columns |
| `gathered` | One centered equation per row |
| `array` | A column specification containing `l`, `c`, `r` and vertical `|` rules; `\hline` between rows |

Use `&` between cells and `\\` or `\cr` between rows. Nested environments, grouped expressions and fractions inside cells are supported. Empty cells and a final row separator are accepted. Environment names must match. Row-height options such as `\\[4pt]`, repeated array specifications and column spanning are not implemented.

The example from [#190](https://github.com/oipoistar/tinta/issues/190) now renders:

```latex
$$
\begin{bmatrix}1&2\\5&6\end{bmatrix}\longrightarrow 6,
\qquad
\begin{bmatrix}3&4\\7&8\end{bmatrix}\longrightarrow 8
$$
```

## Expressions and styles

- Fractions: `\frac`, `\dfrac`, `\tfrac`, `\cfrac`.
- Binomial coefficients: `\binom`, `\dbinom`, `\tbinom`.
- Square and indexed roots: `\sqrt{x}`, `\sqrt[3]{x}`.
- Subscripts and superscripts, including nested scripts.
- Display, text, script and scriptscript styles: `\displaystyle`, `\textstyle`, `\scriptstyle`, `\scriptscriptstyle`. A style declaration applies to the remaining contents of its group or cell.
- Operators such as `\sum`, `\prod`, `\coprod`, `\int`, `\iint`, `\iiint`, `\oint`, `\bigcup` and `\bigcap`. Display sums and limits use above/below placement; integrals use side scripts unless `\limits` is requested. `\nolimits` forces side scripts.
- `\substack{...\\...}` for multiline limits.
- `\overset`, `\underset`, `\stackrel` and `\xrightarrow[below]{above}` / `\xleftarrow[below]{above}`.
- Common Greek letters and variants, relations, set and logical symbols, arrows, harpoons and named functions. The exact command mappings are in [math_parser.cpp](../src/math_parser.cpp).
- Modular notation: `\pmod{n}`, `\pod{n}`, `\bmod`.

## Text, alphabets and decoration

`\text{for all }` preserves spaces and escaped punctuation, including `\&`, `\%` and `\{`. Nested `\textbf`, `\textit`, `\textrm` and `\text` are supported. Nested math delimiters inside text mode are not implemented.

Math alphabets include `\mathrm`, `\mathit`, `\mathbf`, `\boldsymbol` / `\bm`, `\mathbb`, `\mathcal` / `\mathscr`, `\mathfrak`, `\mathsf` and `\mathtt`. Script and calligraphic commands use the same Unicode script alphabet. Available shapes depend on the installed fonts.

Decorations include `\bar`, `\overline`, `\underline`, `\vec`, `\overrightarrow`, `\hat`, `\widehat`, `\tilde`, `\widetilde`, `\dot`, `\ddot`, `\acute`, `\grave`, `\breve`, `\check`, `\overbrace`, `\underbrace`, `\boxed`, `\cancel`, `\bcancel`, `\xcancel` and `\not`. Braces can carry annotations using scripts.

## Delimiters and spacing

`\left...\right` and `\middle` size delimiters with their contents. A dot denotes an invisible delimiter. The `\big`, `\Big`, `\bigg`, `\Bigg` families and their `l`, `r`, `m` variants provide explicit sizes.

Spacing includes `\,`, `\:`, `\;`, `\!`, `\quad`, `\qquad`, `\enspace`, and thin/medium/thick space aliases. `\hspace`, `\kern` and `\mkern` accept `em`, `ex`, `mu`, `pt` and `px` dimensions; `ex` is approximated as half an em. `\phantom`, `\hphantom`, `\vphantom` and `\smash` control invisible layout space.

## Custom macros

Definitions are local to one `$...$` or `$$...$$` expression, with brace-group scoping. They never carry over to another equation, tab or document.

```latex
$$
\newcommand{\norm}[1]{\left\lVert#1\right\rVert}
\DeclareMathOperator*{\argmin}{arg min}
\argmin_x \norm{x-b}^2
$$
```

Supported definitions are `\newcommand`, `\renewcommand`, `\providecommand`, basic `\def` with sequential undelimited parameters, and `\DeclareMathOperator` / `\DeclareMathOperator*`. Up to nine arguments and an optional default first argument are supported. Recursive expansion, input size and nesting have bounds so incomplete or pathological expressions fall back to source.

## Rendering and remaining scope

Tinta selects the installed **Cambria Math** family when available and otherwise uses the theme font. It does not download or bundle a font. Layout uses native text, rules and lines; this is not a full OpenType MATH typesetting engine.

The viewer, native raster exports and SVG math exports share the same layout. SVG references the selected font family without embedding its font data. Unsupported commands or malformed structure make the whole expression fall back to raw TeX.

This extension does not implement complete LaTeX documents, arbitrary packages, document-wide macros, automatic equation numbering, `\tag`, `\label`, `\eqref`, `\(...\)` / `\[...\]` Markdown delimiters, or standalone `equation` / `align` environments outside dollar delimiters. Use `aligned` inside `$$...$$` for multiline derivations. Chemistry, units packages, color commands and TikZ remain outside the supported subset.

## Regression samples

[math-compatibility.md](../tests/fixtures/math-compatibility.md) contains renderable examples. `math_parser_tests` covers syntax and deterministic malformed-input mutations. `math_layout_tests` checks metrics, Markdown preservation, inline line spacing, retained drawing primitives, native offscreen drawing and SVG output. It can also generate an HTML and PNG sample sheet:

```powershell
out/math-compat/Release/math_layout_tests.exe samples.html samples.png
```

Mixed-document fixtures exercise the surrounding Markdown as well as the equations:

- [math-mixed-layout.md](../tests/fixtures/math-mixed-layout.md): math in headings, aligned tables, nested lists, emphasis, links, highlights, strikethrough, quotes and callouts, alongside literal code and Mermaid.
- [math-inline-stress.md](../tests/fixtures/math-inline-stress.md): tall matrices, fractions and annotations in wrapping paragraphs, table cells and lists.
- [markdown-regression-control.md](../tests/fixtures/markdown-regression-control.md): existing formatting without math, for comparison with a baseline executable.

The automated layout suite checks all 49 equations across the three math fixtures, their enclosing Markdown structures, table column counts and literal code. On Windows with a graphics session, generate native print pages and HTML from the actual application:

```powershell
tests/render_math_fixtures.ps1 -Binary out/math-compat/Release/tinta.exe
# Optional: compare control PNGs and HTML byte-for-byte with a baseline build.
tests/render_math_fixtures.ps1 -Binary out/math-compat/Release/tinta.exe -BaselineBinary path/to/baseline/tinta.exe
```

The script creates a fresh ignored output directory and a portable copy of Tinta with isolated settings. It checks export completion and equation counts. Inspect its PNGs and HTML, then open the fixtures in the viewer at normal and narrow widths and in light and dark themes. Verify table borders, the text before and after tall formulas, link clicks, code literals and the final block. Export success alone is not visual validation.

Tall inline equations currently expand every line in their paragraph uniformly. This avoids overlap but can leave substantial vertical space in long paragraphs; display math is more compact for large matrices. Native print pagination can continue table borders and quotes onto the next page.
