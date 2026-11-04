# CSS themes

A theme can be written in CSS. Tinta reads the part of CSS a theme needs:
real CSS syntax, selectors over the HTML that Markdown becomes, the cascade,
variables and colour functions. It draws the page itself, with no web
engine, so layout properties such as `display` or `position` do nothing,
and Tinta says so.

## Making one

Put a `.css` file in the `themes` folder beside `themes.ini`:
`%APPDATA%\Tinta\themes\`, or the folder of `tinta.exe` in portable mode.
Its file name is the theme's name in the theme chooser. With the theme in
use, **Settings > Appearance > Edit** opens its sheet in Tinta. Each save
restyles the window.

```css
/* Ink */
@import "Paper";                    /* start from a built-in theme */

:root {
  color-scheme: light dark;         /* follows Windows; light-dark() picks */
  accent-color: oklch(55% 0.15 250);
  --ink: light-dark(#1f2328, #d8dee9);
}
body {
  color: var(--ink);
  background: light-dark(#fbf8f1, #1b1d22);
  font: 17px/1.65 Georgia, serif;
  max-width: 44em;                  /* the reading column */
}
h1 { font-size: 2.2em; font-weight: 600; text-align: center }
h2 { border-bottom: 1px solid #ddd; padding-bottom: .3em }
p + p { text-indent: 1.5em }
a { color: #3366cc; text-decoration: none }
code { background: #f4f1ea; padding: .1em .4em; border-radius: 4px }
pre { background: #f4f1ea; padding: 16px; border-radius: 8px }
blockquote { border-left: 3px solid #b35900; color: #666; font-style: italic }
blockquote strong { color: #b35900 }
ul ul { list-style-type: circle }
li::marker { color: #b35900 }
tbody tr:nth-child(even) td { background: #f6f3ec }
::selection { background: rgb(51 102 204 / 0.3) }
.token.keyword { color: #a626a4 }
```

## What it draws

Every element of the page takes its own rules, as in a browser: the second
paragraph's indent, a heading's border, the quote's padding, the third
row's background, a `strong` inside a quote. Where the sheet says nothing,
Tinta's own look stays.

- **Elements**, as the HTML export names them: `body`, `p`, `h1`-`h6`, `a`,
  `strong`, `em`, `del`, `ins`, `mark`, `sup`, `sub`, `code` (inline) and
  `pre` (code blocks), `blockquote` (callouts are `blockquote.alert-note`,
  `-tip`, `-important`, `-warning`, `-caution`, with `p.alert-title`), `ul`,
  `ol`, `li` (tasks are `li.task`) and `li::marker`, `table` with `thead`,
  `tbody`, `tr`, `th` and `td`, `hr`, `figure` and `figcaption`,
  `section.footnotes` and `div.footnote`, `span.citation` and the
  references' `div#refs` and `div.csl-entry`, `::selection`, and the code
  tokens `.token.keyword`, `.string`, `.comment`, `.number`, `.function`,
  `.class-name` and `.control`. Sibling and position selectors work:
  `h1 + p`, `li:last-child`, `tr:nth-child(even)`.
- **Text:** `color`, `font-family` (the first face this PC or the sheet
  has), `font-size`, `font-weight`, `font-style`, `font-stretch`,
  `line-height`, `letter-spacing`, `text-transform`, `text-align` (with
  `justify`), `text-indent`, `font-feature-settings` and `font-variant-*`,
  and `text-decoration` on links, `del` and `ins`.
- **Boxes:** `margin` (between blocks they collapse as CSS's do),
  `padding`, `background-color`, `border` and `border-radius` for
  paragraphs, headings, captions, quotes, code blocks, inline code and
  table cells; `max-width`, which on `body` is the reading column.
- **Lists:** `list-style-type` (`disc`, `circle`, `square`, `decimal`,
  `decimal-leading-zero`, `lower-alpha`, `upper-alpha`, `lower-roman`,
  `upper-roman`, `lower-greek`, `cjk-decimal`, `none` or a string),
  `padding-left` on `ul` and `ol` as the indent the markers hang in, and
  `::marker`'s `content` and `color`.
- **Tables:** the cells' `padding`, their borders (top and bottom across,
  left and right down) and the rows' backgrounds.
- **Rules:** `hr`'s top border, or its background as a bar when it has
  none.
- **The language:** comments, `!important`, every selector CSS has for a
  document (descendants, children, siblings, `:is()`, `:not()`,
  `:nth-child()`...), `var()`, `calc()`, `min()`, `max()`, `clamp()`, hex and
  named colours, `rgb()`, `hsl()`, `hwb()`, `lab()`, `lch()`, `oklab()`,
  `oklch()`, `color-mix()`, `light-dark()`, `@import` (a built-in theme by
  name, a `themes.ini` theme by name, or a file beside the sheet), `@media`
  (`prefers-color-scheme`, `print`, widths), `@supports` and `@font-face`.

Mistakes and properties Tinta does not use are reported in a notice with
the file and line, and the rest of the sheet applies.

## Fonts

`@font-face` loads a theme's own fonts: TTF, OTF, TTC, WOFF and WOFF2 files
beside the sheet (`src: url(fonts/Literata.woff2)`), or a face this PC has
(`src: local("Georgia")`). Nothing is installed; the faces are the
theme's alone. The HTML export copies the files beside the page.

```css
@font-face {
  font-family: "Literata";
  src: url("fonts/Literata.ttf");
}
@font-face {
  font-family: "Literata";
  src: url("fonts/Literata-Italic.ttf");
  font-style: italic;
}
body { font-family: "Literata", Georgia, serif }
```

## Print

`@media print` rules apply when printing. A sheet without them prints in
Tinta's print palette, black on white, with its fonts and spacing.

## Importing Typora and Obsidian themes

**Settings > Appearance > Import theme...** takes a Typora or Obsidian
theme's `.css` file and writes a Tinta theme beside your others, named as
the file was. Tinta works out which app the theme is for, reads the sheets
it imports along with it, and lists what it could not carry over, each
with its line; the new theme is then in the chooser like any other.

What the import rewrites:

| In the app's theme | In Tinta's |
| --- | --- |
| Typora's `#write`, `.typora-export #write`; Obsidian's `.markdown-preview-view`, `.markdown-rendered` | `body`, or nothing before another selector: `#write h1` is `h1` |
| `.typora-export` rules | the same rules for print only |
| Obsidian's `.theme-light` and `.theme-dark` | one sheet that follows Windows: `:root { color-scheme: light dark }`, each variable set on both sides as `light-dark(light, dark)`; a theme with only `.theme-dark` is a dark theme |
| Typora's `.md-fences`, `.CodeMirror`, `.cm-s-inner` | `pre` (a code block) |
| CodeMirror's `.cm-keyword`, `.cm-string`, `.cm-comment`... | Tinta's `.token.keyword`, `.token.string`, `.token.comment`... |
| Typora's `code` | `:not(pre) > code`: Typora's code blocks hold no `code`, so it means inline code |
| `.md-task-list-item`, `.task-list-item` (`.task-list-done`, `.is-checked`) | `li.task` (`:checked`) |
| Typora's `.md-alert-note`... and Obsidian's `.callout[data-callout="note"]`... | `blockquote.alert-note`, `-tip`, `-important`, `-warning`, `-caution`; their titles `p.alert-title` |
| `.md-meta-block`, `.md-math-block`, `.md-diagram-panel`, Obsidian's `.internal-link` | `div.frontmatter`, `div.math-display`, `div.diagram`, `span.wikilink` |

The variables each app themes with become declarations on Tinta's
elements, ahead of the theme's own rules, which still win:

- Typora: `--bg-color` and `--text-color` (the body), `--monospace` (code),
  `--select-text-bg-color` (the selection), `--primary-color` (the accent),
  `--side-bar-bg-color` (Tinta's panels).
- Obsidian: `--background-primary`, `--text-normal`, `--font-text-theme`,
  `--text-accent` or `--link-color`, `--interactive-accent`, `--h1-color`
  to `--h6-color` with their sizes, weights and faces, `--bold-color`,
  `--italic-color`, `--code-background`, `--code-normal`,
  `--font-monospace-theme`, the `--code-*` token colours,
  `--blockquote-border-color`, `--text-highlight-bg`, `--text-selection`,
  `--hr-color`, the table, list and checkbox colours, and the callout
  colours (`--callout-tip`, or `--callout-color` in a callout's rule, as an
  RGB triplet or a colour).

What is left out, with a note each:

- Rules for the app's window and editor: Typora's sidebar, toolbar, menus,
  footer, source mode and editing marks (`.md-focus`); Obsidian's
  workspace, panes, file list, status bar, modals and its editor
  (`.markdown-source-view`, `.cm-*`); scrollbars; and classes the app puts
  on its window (`body.is-mobile`, a theme's own settings classes).
- Callouts Tinta has not: it draws GitHub's five, so `info`, `todo`,
  `bug` and the rest are dropped.
- Web fonts: `@import url(https://fonts...)`, Typora's
  `@include-when-export` and `@font-face` rules whose fonts are on the web.
  Tinta fetches nothing: install the font, or put its files beside the
  theme with `@font-face`.
- At-rules Tinta does not read, such as `@keyframes` and `@page`;
  `@layer` blocks are unwrapped.

The variables a theme reads but never sets, because the app does
(`var(--color-red)`), are noted too; Obsidian's sizes and weights
(`--font-smaller`, `--font-semibold`...) are given their usual values. The
theme's opening comment, its name and licence, is kept at the top.

Limits: the dropped `#write` and `.markdown-preview-view` prefixes carried
specificity, so where the theme relied on it rather than on order a rule
can now lose to an earlier one; light and dark rules are placed after the
others, which keeps the usual case right. Fonts a theme loads from files
beside it must be copied beside the imported theme.

## Exports

The HTML export carries the sheet after its own styles, so a browser shows
what Tinta shows, and also draws the properties Tinta leaves to it.

## themes.ini

`themes.ini` keeps working. Each of its keys is a CSS declaration: `text=`
is `body { color }`, `h1=` is `h1 { color }`, `inlinecode=` is
`code { color }` and so on; a built-in theme or a `themes.ini` section is
what `@import "Name"` reads.

The design, and what comes next, is in
[docs/specs/theming.md](specs/theming.md). Try the themes in
[tests/fixtures/theme-css](../tests/fixtures/theme-css).
