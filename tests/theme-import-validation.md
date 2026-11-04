# Importing Typora and Obsidian themes, and explaining the cascade

Checked on Windows with the Release build on 2026-11-04. The design is in
`docs/specs/theming.md` (phase 3); the user's page is `docs/css-themes.md`,
"Importing Typora and Obsidian themes".

## What was asked

People want their Typora or Obsidian theme in Tinta, and the coming CSS
inspector (Ctrl+Alt+click) must show which rules styled an element.

## The change

- **`css::importTheme`** (`src/css_import.cpp`): a Typora or Obsidian sheet
  rewritten onto Tinta's page. `#write`, `.typora-export #write`,
  `.markdown-preview-view` and `.markdown-rendered` are the body;
  `.typora-export` rules hold for print; Obsidian's `.theme-light` and
  `.theme-dark` merge into one `:root { color-scheme: light dark }` with
  `light-dark()` for colours set on both sides (anything else by
  `prefers-color-scheme`); Typora's own blocks (`.md-fences`, CodeMirror's
  tokens, tasks, alerts, front matter, maths, diagrams, `[mdtype]`,
  `[md-inline]`) and Obsidian's (callouts, tasks, wiki links, front matter,
  maths) become Tinta's elements; the apps' variables become declarations
  on those elements. Rules for the apps' windows and editors, scrollbars,
  callout kinds Tinta has not, web fonts and at-rules Tinta does not read
  are dropped with a note each, with its line; so are the variables a
  theme reads and never sets. `@import`ed sheets are read and rewritten
  through an `ImportSource`. `css::detectFlavor` tells the apps apart.
- **`css::Engine::explain`** (`src/css_cascade.cpp`): for each property an
  element's rules declare, the winner and the declarations it beat in
  cascade order, with selector and declaration as written, specificity,
  origin, `!important`, file, line and column; inherited properties from
  the nearest ancestor that declares them; the custom properties its
  `var()`s read. For `::before`, `::after`, `::marker` and `::selection`
  too. `css::selectorPath` writes the element path.
- **A fix found on the way** (`2390499`): a hash starting with a digit
  (`#3366cc`) passed through a second `var()` was written back as a hex
  escape and lost.

## Fixtures

`fixtures/theme-css/import/`: `typora-night.css` (a dark Typora theme with
web fonts, CodeMirror tokens, alerts, tasks, sidebar and menu rules, an
export rule and `@keyframes`), `obsidian-dusk.css` (light and dark, with
callouts, editor and workspace rules, Style Settings classes and variables
Obsidian sets), `obsidian-midnight.css` (dark only). View an imported
theme on `fixtures/theme-css/mixed.md`.

## What was checked

- `css_import_tests` (97 checks): each fixture imported, read back by
  `css::parse` without errors (and in fact without warnings), and styled
  through `css::Engine` as the app styles it: the body, each heading, links,
  inline code against code blocks, the tokens, quotes, marks, the
  selection, tasks, alerts and their titles, the front matter, light and
  dark, print. The notes for each dropped selector name it and its line.
  Imports through an `ImportSource` (rewritten in place, with their media,
  a missing one, a cycle), without one (kept), a sheet of neither app,
  3,000 random sheets and 300 nested `@media` blocks.
- `css_import_tests --print` writes each imported fixture and its notes;
  read through by hand.
- `css_tests` (394 checks; 24 new for explain(), 1 for the fix):
  explain() on competing specificities, origins and `!important`,
  shorthands, imported sheets' file names and lines, inheritance, custom
  properties read through `var()`,
  pseudo-elements, media, selectors and declarations over several lines,
  and the selector path. style() takes about 4 us a context before and
  after (3.76-4.30 us and 3.88-4.57 us over six alternating runs of the old
  and new `css_tests`); explain() about 35 us.

## Size

Linked alone into a probe with `/OPT:REF`: the importer adds 71 KB,
explain() and selectorPath() 19 KB. In `tinta.exe` their `std::` code is
partly shared with what is there already.

## Limitations

- Not in the app yet: Settings > Appearance > Import theme..., and the
  inspector's card, are for the integrator; nothing here was seen in a
  window.
- The dropped `#write` and `.markdown-preview-view` prefixes carried
  specificity; where a theme relied on it rather than on order, a rule can
  lose to an earlier one. Light and dark rules are placed after the rest,
  which keeps Obsidian's usual case.
- Fonts beside the theme must be copied beside the imported one; Typora's
  callout-like extensions and Obsidian's callouts beyond GitHub's five have
  no element in Tinta.
