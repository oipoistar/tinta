# Equation numbers, signs, accents and brackets (#314)

Validated on Windows with the Release build on 2026-11-03.

## What was reported

- `\tag`, `\label` and `\eqref` were not supported: an equation with any of
  them fell back to its TeX source.
- Two `$$` equations in a row rendered as one line of text: the first inline,
  the second as `$ \rho_{e,c} = ...` source with a `$` under it.
- In `[\![-p + ...]\!]` the minus was spaced as if it subtracted, `[ − p`,
  and in `\varepsilon\left(E_n^2 - E_t^2\right)` the bracket cut into the
  epsilon and the `²` into the `E`.
- The tilde of `\tilde\phi` floated high over the letter.
- A setting for another math face, Latin Modern Math, `mathFont=`.

## Cause

- The parser had no `\tag`, `\label`, `\eqref` or `\ref`, and an unknown
  command fails the whole equation.
- Only a paragraph holding nothing but one `$$` equation was laid out as a
  centred equation; any other was laid out inline, and its source, when it
  failed, was shown between single dollars. Word exports drew no `$$`
  equation at all: it sits in a paragraph, and the paragraph writer skipped
  it.
- A sign counted as unary only at a row's start or after another sign or a
  relation; a comma took its space on both sides.
- Cambria Math has no italic: DirectWrite slants the upright letters, whose
  ink then leans past their width. A bracket or a superscript set at the
  letter's width ran into the lean, and a drawn bracket's bulge stood at its
  box's very edge.
- Accents stood over the face's line ascent, 0.95 em, whatever the letter.

## Change

- `\tag{1.8}` gives the equation a number box, `\tag*{A}` one without
  parentheses; `\label`, `\notag` and `\nonumber` set nothing. On display the
  reader sets the number at the column's right and moves the equation off
  centre to keep an em clear of it; with no room for both, the number goes
  under it at the right. Inline, the number follows a quad on.
- The layout gathers each equation's `\label` and its `\tag` as it starts;
  `\eqref{eq:jump}` and `\ref` read the number, and one with none reads
  (??), as in LaTeX. HTML and Word exports read them the same way.
- A `$$` equation stands on its own line wherever it sits in a paragraph; the
  words before and after make lines of their own. A display equation that
  falls back, in a list item or a cell, shows its `$$`. Word exports write
  each `$$` equation as a centred paragraph, its number at a right tab, and
  keep one in a list item on its line.
- Spacing follows TeX's atom classes: a sign after an opening bracket, an
  operator, a relation, a comma or another sign, or before a relation, a
  closing bracket or a comma, is unary. Punctuation takes its space after
  it, and nothing stands between a bracket and what it encloses.
- Each run measures its ink: how high it reaches and how far a slanted
  letter leans past its width. The lean is kept clear before an upright
  neighbour and a superscript (TeX's italic correction), but not between two
  slanted letters. Drawn brackets keep 0.07 em clear outside.
- Accents sit over the letter's ink, an x's height at least, and move with
  the slant. An accented x no longer makes its box taller.
- `mathFont=` in settings.ini names a face for equations; one not installed
  falls back to Cambria Math.

## Fixture

`fixtures/math-tags-314.md`: a heading, a paragraph with bold, italic, code
and a link; the reported equation with `\tag` and `\label`; the reported two
equations in one paragraph, the second tagged; `\eqref` to both and to a
missing label; a `$$` equation between words with `\tag*`; accents; signs;
a list with a failing `$$` and an inline tag; a quote; a table with a
`\ref`; a code block with literal `$$`; a last paragraph.

## Automated checks

- `math_parser`: `\tag`, `\tag*`, the first of two tags, `\label`, `\notag`,
  `\nonumber`, an unresolved reference reading (??), an unclosed tag failing,
  the reported equation parsing.
- `math_layout`: both reported tagged equations render; six signs are as
  tight as the same sign in braces while `a-b` stays binary; a comma takes
  0.12 em after it only; a bracket after a slanted `x` and the `2` of `E^2`
  stand off by the lean while `E_n` tucks in; a drawn bracket stands clear
  of the box's edge; `\tilde`, `\hat`, `\bar` and `\dot` over x sit within
  0.85 em of the baseline and leave its height alone; the tilde sits on the
  phi; inline and display tags; labels and references; `mathFont=` and its
  fallback.
- `math_markdown` (new, `tests/math_markdown_tests.cpp`), the fixture in
  Paper and Midnight at 100 and 150 % and 1050, 650 and 420 px: the tagged
  equation's number at the column's right, an em clear of it or under it;
  two equations in a row one under the other, the first centred; the
  equation between words on its own line; labels and `\eqref`; the failing
  `$$` source; the code block's dollars; no equation running into its
  number. Exports: three numbers in HTML; Word's right tab with (1.8), (2.1)
  and circle, and the list item's `$$` source.

Neutering one part at a time, each fails:

| Part taken out | Failures |
| --- | --- |
| `\tag` | 49: parser, layout and every reader check of a number |
| unary signs | 6 sign pairs |
| no space before a comma | the comma check |
| the lean in a row | the bracket after a slanted letter |
| the superscript's lean | the superscript check |
| the bracket's bearing | both bracket checks |
| accents over the ink | 5: accents, height, the phi |
| bars and hats over the ink | 4 |
| `$$` on its own line | 40: two in a row, words around, numbers |
| the number drawn | 40 |
| `$$` in the fallback | 12 |
| labels gathered | 24 |
| `mathFont=` | its check |

## In the built app

An isolated portable copy (`out/issues-314-315/`, ignored):

- The reported document in Paper at 1280 px, before (435a3b2's build) and
  after: the tagged equation renders with (1.8) at the right; the two
  equations stand centred one under the other, (2.1) beside the second; the
  minus after `[` sets tight; `ε(` and `E²` clear; the tilde sits on the phi.
- The fixture in Midnight at 1280 and 650 px: at 650 the wide equation fills
  the column and (1.8) goes under it; `\eqref` reads (1.8), (2.1) and (??);
  circle beside the equation between words; the failing `$$` shows its
  source; the inline tag reads (3).
- Live, the caret in the two equations: their source with its wash on the
  caret's line, the equations under it.

## Size

`tinta.exe` grows by 17,408 bytes (3,963,904 to 3,981,312).

The full suite without the three tests that use the clipboard passes: 58 of
58, `math_markdown` new among them.

## Limitations

- No automatic numbering: an equation without `\tag` has no number, and
  `\eqref` to it reads (??).
- A tag holding TeX commands (`\tag{$\ast$}`) is set as written.
- `mathFont=` is read at start; the layout's constants are tuned for Cambria
  Math and a face's MATH table is not read.
- A bullet beside a line made taller by an inline equation stays at the
  line's first baseline, as before.
