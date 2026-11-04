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
}
h1 { font-size: 2.2em; font-weight: 600 }
h2 { color: #a33; font-style: italic }
a { color: #3366cc }
code { background: none; color: #b35900 }
pre { background: #f4f1ea }
blockquote { border-left-color: #b35900 }
mark { background: #ffe08a }
strong { color: #222 }
::selection { background: rgb(51 102 204 / 0.3) }
hr { border-top-color: #ccc }
.token.keyword { color: #a626a4 }
```

## What it reads

- **Elements:** `body`, `p`, `h1`-`h6`, `a`, `strong`, `em`, `code` (inline)
  and `pre` (code blocks), `blockquote`, `mark`, `hr`, `::selection`, and the
  code tokens `.token.keyword`, `.string`, `.comment`, `.number`,
  `.function`, `.class-name` and `.control`.
- **Properties, in this version:** `color`, `background` and
  `background-color`, `accent-color`, `color-scheme`, `font-family` (the
  first face this PC has), `font-size` for the body, code and each heading,
  `font-weight` and `font-style` for headings, the body's `line-height`, and
  the border colours of quotes and rules.
- **The language:** comments, `!important`, every selector CSS has for a
  document (descendants, children, siblings, `:is()`, `:not()`,
  `:nth-child()`...), `var()`, `calc()`, `min()`, `max()`, `clamp()`, hex and
  named colours, `rgb()`, `hsl()`, `hwb()`, `lab()`, `lch()`, `oklab()`,
  `oklch()`, `color-mix()`, `light-dark()`, `@import` (a built-in theme by
  name, a `themes.ini` theme by name, or a file beside the sheet), `@media`
  (`prefers-color-scheme`, `print`, widths) and `@supports`.
- **Variables:** `--ui-*` colour the window and `--mermaid-*` the diagrams,
  below.

The rest of CSS that Tinta will draw in later versions, such as spacing,
borders, letter spacing and list markers, is read and kept but not drawn
yet. Mistakes and properties Tinta does not use are reported in a notice
with the file and line, and the rest of the sheet applies.

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
