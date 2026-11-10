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

The ten built-in themes are written the same way: each is a CSS file in
Tinta's source, under `resources/themes`, and `@import "Paper"` starts from
Paper's file as it is written.

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
- **Generated content:** `::before` and `::after` with `content` (strings,
  `counter()` and `counters()` in any list style), with `counter-reset`
  and `counter-increment`, on paragraphs, headings, list items, captions
  and inline elements. `h2::before { counter-increment: h2; content:
  counter(h2) ". " }` numbers the headings, in Contents too, and
  `a[href^="http"]::after { content: " ↗" }` marks the links that leave
  the page.
- **Depth and the caret:** `box-shadow` under code blocks, quotes and boxed
  paragraphs and headings, `opacity` on pictures, and `caret-color` for
  the editors' caret.
- **A document's own classes:** front matter `cssclasses: [wide, serif]`
  (Obsidian's key) puts them on `body`, so a `body.wide` rule reaches that
  document alone.
- **The language:** comments, `!important`, every selector CSS has for a
  document (descendants, children, siblings, `:is()`, `:not()`,
  `:nth-child()`...), `var()`, `calc()`, `min()`, `max()`, `clamp()`, hex and
  named colours, `rgb()`, `hsl()`, `hwb()`, `lab()`, `lch()`, `oklab()`,
  `oklch()`, `color-mix()`, `light-dark()`, `@import` (a built-in theme by
  name, a `themes.ini` theme by name, or a file beside the sheet), `@media`
  (`prefers-color-scheme`, `print`, widths), `@supports` and `@font-face`.
- **Variables:** `--ui-*` colour the window and `--mermaid-*` the diagrams,
  below.

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

## A folder's own sheet

A `tinta.css` in a document's folder, or in a folder above it, styles
that folder's documents on top of the theme in use, whichever it is,
built-in themes included: a project's fonts, spacing or heading colours.
A chip at the top of the page says **Styled by tinta.css**; a click on it
turns the sheet off for that document, and another turns it on again, so
a folder from elsewhere cannot restyle what you read without saying so.
Saving the sheet restyles the page, and the HTML export carries it.

## The inspector

With a CSS theme in use, **Ctrl+Alt+click** anything on the page: a card
shows its path from the page down (`body > blockquote > p > strong`), each
rule that styles it in cascade order with its selector, file and line,
the rules it beat struck through, where Tinta's own look decides, and
what the element inherits, with colours as swatches. Click a rule to
open its sheet at that line; Esc or a click elsewhere puts the card away.

## Print

`@media print` rules apply when printing. A sheet without them prints in
Tinta's print palette, black on white, with its fonts and spacing.

## Importing Typora and Obsidian themes

**Settings > Appearance > Themes > Import** takes a Typora or Obsidian
theme's `.css` file and writes a Tinta theme beside your others, named as
the file was, and switches to it. Tinta works out which app the theme is
for, reads the sheets it imports along with it, copies the font files its
`@font-face` rules load from beside it, and lists what it could not carry
over at the top of the new sheet, each with its line; the theme is then in
the chooser like any other.

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
others, which keeps the usual case right.

## The window

Five variables on `:root` colour the window around the page. Each one a
theme leaves out keeps the colour Tinta takes from the page, as before.

```css
:root {
  --ui-background: #1d2b38;  /* the tab strip and the title bar */
  --ui-text: #e6edf3;        /* tab names, panel, menu and dialog text */
  --ui-accent: #f59e42;      /* the row in use, focus, toggles, buttons */
  --ui-panel: #243444;       /* side panels, menus, popups, cards, notices */
  --ui-border: #3b4f63;      /* their edges and dividers */
}
```

`light-dark()` works in each. The tab in use keeps the page's colours,
since it carries on into the page, and so does text that stands on the page
itself, such as the start page's title. `--ui-text` is the text on
`--ui-background` and `--ui-panel`, so a dark window on a light page wants
all three.

## Diagrams

Mermaid diagrams take Mermaid's own theme variables, written in kebab case
after `--mermaid-`: `primaryColor` is `--mermaid-primary-color`. Set them on
`:root`, or on `.diagram` for the diagrams alone. The HTML and Word exports
draw the same colours.

| Variable | Colours |
| --- | --- |
| `--mermaid-main-bkg`, else `--mermaid-primary-color` | nodes: flowchart boxes, actors, classes, states |
| `--mermaid-node-border`, else `--mermaid-primary-border-color` | their outlines |
| `--mermaid-node-text-color`, else `--mermaid-primary-text-color` | flowchart labels |
| `--mermaid-text-color`, else `--mermaid-primary-text-color` | the rest of the text: titles, messages, legends |
| `--mermaid-line-color` | edges, messages, arrows and frames |
| `--mermaid-default-link-color` | flowchart edges, before `--mermaid-line-color` |
| `--mermaid-secondary-color` | notes, activations and highlighted bands |
| `--mermaid-edge-label-background`, else `--mermaid-secondary-color` | flowchart edge labels |
| `--mermaid-cluster-bkg`, else `--mermaid-tertiary-color` | subgraphs |
| `--mermaid-cluster-border`, else `--mermaid-tertiary-border-color` | their borders |
| `--mermaid-title-color`, else `--mermaid-tertiary-text-color` | their titles |
| `--mermaid-background` | what a diagram draws in the page's colour: slice edges, text on slices |
| `--mermaid-pie1` to `--mermaid-pie12` | pie slices and every chart's series, in turn |

A `style` or `classDef` in the diagram still wins. Tinta reads no
`%%{init: ...}%%` directive, so `themeVariables` given there do nothing.
PlantUML diagrams keep the page's colours. Mermaid's other variables are
reported as not drawn, and a misspelt name with the one it is nearest.

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
