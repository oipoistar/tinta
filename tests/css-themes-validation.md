# CSS themes, phase 1

Validated on Windows with the Release build on 2026-11-03. The design is in
`docs/specs/theming.md`; the user's page is `docs/css-themes.md`.

## What was asked

Theming beyond `themes.ini`'s flat keys: people ask for CSS, and a web
engine is out of the question, so a profile of CSS that Tinta draws
natively.

## The change

- **The engine** (`css.h`, `src/css*.cpp`): CSS Syntax 3 tokens and error
  recovery; selectors (type, class, id, attributes, combinators, `:is()`,
  `:where()`, `:not()`, the structural pseudo-classes, `:checked`,
  `:lang()`, `::before`, `::after`, `::marker`, `::selection`) with CSS's
  specificity; the cascade by origin, importance and order; inheritance;
  custom properties and `var()`; `calc()`, `min()`, `max()`, `clamp()`;
  every colour syntax including `oklch()`, `color-mix()` and
  `light-dark()`; `@import`, `@media`, `@font-face`, `@supports`;
  diagnostics with file, line and column, and a suggestion for a
  misspelt property. Styles are computed once per element context, and
  contexts are shared by elements no selector tells apart.
- **Themes** (`themes_css.cpp`): every `.css` file in `<config>\themes\`
  is a theme, after the `themes.ini` ones, named by its file. A sheet is
  read into the theme Tinta draws with: the page, headings (colour, face,
  size, weight, slant), links, inline code and code blocks, the code
  tokens, quotes, marks, bold, italic, the selection, rules, the accent,
  the body's size and line height. It starts from Paper, or Midnight when
  dark. `color-scheme: light dark` makes a theme follow Windows, read once
  for each side. `@import "Paper"` (or a `themes.ini` theme's name) starts
  from that theme: built-in and `themes.ini` themes are turned into CSS by
  one mapping, `themeToCss`.
- **Following the file**: the 500 ms timer that follows documents reads
  the active sheet again when it, or a file it imports, changes, and
  applies it; replaced themes stay in memory so no copy of one dangles.
- **Problems**: when the theme or its sheet changes, a notice gives the
  count and the first problem, with the file and line; the tray lists all.
- **Settings > Appearance > Edit** on a CSS theme opens its sheet in Tinta's
  editor; Tinta reads `.css` files as highlighted text.
- **HTML export** carries the sheet, imports inlined, after its own styles.
- **Type sizes** for every theme: body, code and heading sizes, heading
  weights and slants and the body's line height reach the text formats,
  the reading view and Live (`96506d2`).

## Fixtures

`fixtures/theme-css/`: `paper-light.css`, `night-dark.css`,
`ink-light-dark.css` (with `ink-base.css` and `ink-print.css` it imports),
`mistakes.css`, and `mixed.md`, a page with every element.

## Automated checks

- `css_engine` (css_tests, 369 checks): tokenizer and parser edge cases,
  error recovery, every selector and its specificity, the cascade and
  `!important`, inheritance, `var()` fallbacks and cycles, `calc()`, every
  colour function against reference values, `light-dark()` and `@media`,
  `@import` with a cycle, `@font-face`, `@supports`, shorthands,
  `serialize`, diagnostics and their lines, interning (a 2,000-row table
  makes two row contexts), and timing.
- `inline_code_theme` (theme_tests, `tests/theme_css_tests.cpp`): all ten
  built-in themes come back from their CSS with the same colours, faces and
  sizes; a sheet's sizes, faces, colours, line height, quote bar, mark,
  selection and rule; light-dark() read for each side; `@import
  "Midnight"`; `display` and a misspelt `colour` reported on their line
  while the rest applies; a sheet in `themes\` listed by name, applied
  (body colour and text format size), drawn (h1's colour in layout) and
  exported to HTML; an unchanged sheet left alone, a saved one read again
  and applied, its problem naming the file and line.

## In the built app

The theme test draws `fixtures/theme-css/mixed.md` with the real layout
and saves the frames beside itself (`build/theme-test-Release/css-*.png`),
which were inspected:

- `paper-light.css`: a light page, its own heading rules and code boxes.
- `night-dark.css`: a dark page with gold headings, the tag chips and
  links in its colours.
- `ink-light-dark.css` with Windows light: cream paper, the body in
  Georgia at 17 px (its first two faces are not installed), headings in
  Segoe UI (`system-ui`), inline code without a box in orange, as the sheet
  says.
- The same sheet with Windows dark: the dark side of each `light-dark()`,
  and a dark mark under light text.

A real window could not be captured in this remote session (PrintWindow
and the screen copy return no Direct2D content); the frames come from the
same drawing code.

## Limitations

- Only what the theme can say today is drawn: colours, faces, type sizes
  and the line height, per element kind. A rule that depends on where an
  element stands (`blockquote strong`) and spacing, borders, letter
  spacing and list markers are read and kept for the export, and drawn
  from phase 2 on.
- `@font-face` fonts are not loaded yet (phase 2); the theme uses the
  next face in its list that is installed.
- Every `.css` file in `themes\` is listed, including sheets meant only to
  be imported; keep those in a subfolder.
