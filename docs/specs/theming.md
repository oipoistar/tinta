---
title: "Theming: CSS themes"
status: Phases 1, 2 and 3 built (2026-11-04)
created: 2026-11-03
---

# Theming: CSS themes

Themes written in CSS, the language people already know and ask for, without
a web engine. Tinta reads a profile of CSS: real CSS syntax and cascade,
selectors over the HTML elements Markdown becomes, and the properties Tinta's
own layout can draw. Anything else is reported, never silently ignored.

## Why

Themes today are `themes.ini` sections of flat `key=RRGGBB` pairs: 35 keys,
about 20 of them colours, three font names. Every request adds another key
(#197 inline code colour, #278 its background, #154 heading rules, #310
emphasis, selection and rules), and none can say "links inside headings" or
"the second level of a list". Sizes and spacing can't be themed at all:
heading sizes are a hard-coded `{32, 26, 22, 18, 16, 14}` in
`d2d_init.cpp`, and the line height, paragraph, list, quote, table and code
spacing are constants in `render.cpp`, copied into `live_draw.cpp`,
`export_docx.cpp` and `export_html.cpp`. #310 asks in so many words for
editing "as flexible as adding custom CSS".

CSS is huge because the web is open-ended: unlimited element types, four
layout systems (flow, flex, grid, positioning), floats, transforms and a live
DOM that scripts restyle. That part needs a browser. A Markdown page has a
closed set of elements, and Tinta lays them out itself. What a theme needs
from CSS is the language: selectors, the cascade, inheritance, variables and
colour functions. Native toolkits have shipped exactly this profile for
years: Qt Style Sheets and GTK's CSS themes.

## Goals

- A theme is a `.css` file. Any CSS editor highlights and completes it;
  snippets from Typora and Obsidian themes mostly work.
- One file can carry light and dark (`light-dark()`, `prefers-color-scheme`).
- Everything themes.ini does keeps working, unchanged files included.
- Saving the file restyles open documents at once, and every problem is
  reported with its line.
- The HTML export carries the theme, so the page looks the same in a browser.
- No cost per frame: styles are computed once per element context and
  cached; a few tens of KB of code.

## Non-goals

- Layout: `display`, flex, grid, floats, `position`, transforms, `width`/
  `height` of text blocks. Tinta lays blocks out itself.
- Animation, transitions, `:hover` on arbitrary elements, scripting,
  `@keyframes`, `@container`, `:has()`.
- Network: `url()` only reads files beside the sheet; nothing is fetched.
- Styling the app's chrome with selectors (it follows variables instead).

## A theme

```css
/* Ink: a Tinta theme */
@import "Paper";                     /* start from a built-in theme */
@font-face { font-family: "Literata"; src: url("fonts/Literata.ttf"); }

:root {
  color-scheme: light dark;          /* follows Windows; light-dark() picks */
  accent-color: oklch(55% 0.15 250);
  --ink: light-dark(#1f2328, #d8dee9);
}
body {
  color: var(--ink);
  background: light-dark(#fbf8f1, #1b1d22);
  font: 17px/1.65 "Literata", "Source Han Serif SC", serif;
  max-width: 44em;
}
h1, h2 { font-family: "Inter", system-ui; letter-spacing: -0.01em; }
h2 { border-bottom: 1px solid color-mix(in oklab, var(--ink) 15%, transparent); }
a { color: var(--accent, #3366cc); text-underline-offset: 3px; }
code { background: none; color: #b35900; }
pre { border-radius: 8px; padding: 12px 16px; }
blockquote { border-left: 3px solid #b35900; font-style: italic; }
ul ul > li::marker { content: "- "; }
tr:nth-child(even) td { background: color-mix(in oklab, var(--ink) 4%, transparent); }
.callout[data-callout="warning"] { border-color: #d97706; }
.token.keyword { color: #a626a4; }

@media print { body { background: white; font-size: 11pt; } }
```

## Where themes live

- `<config>\themes\*.css`, beside `themes.ini` (`%APPDATA%\Tinta`, or the
  exe's folder in portable mode). One file is one theme; its name is the
  file name without `.css`.
- The theme chooser lists built-in themes, then `themes.ini` sections, then
  CSS themes. Settings remember the choice by name (`themeName=`) as well
  as by index, so adding or removing a theme no longer shifts the choice.
- `color-scheme` on `:root` says what the theme is: `dark`, `light`, or
  `light dark` for one that follows Windows. Without it, the background's
  lightness decides.
- Settings > Appearance: **New CSS theme** writes a commented starter file
  (the current theme as CSS) and opens it in a tab; **Edit** on a CSS theme
  opens its file. The colour editor keeps editing `themes.ini` themes.
- A `themes.ini` section is written as a CSS file of its name and drawn
  from it, in its place among the sections. The file follows the section
  while it is as written (a sum in its first comment tells); changed by
  hand it is the theme, and Edit opens it (docs/css-themes.md).

## The language

### Syntax

CSS Syntax Level 3: the tokenizer and rule grammar a browser uses, comments,
escapes, `!important`, error recovery (a bad declaration drops itself, a bad
selector drops its rule). Nesting (`&`) is reported as unsupported in phase
1.

### Elements

The selectors name the HTML that Tinta's HTML export writes, so one sheet
serves the screen and the export:

| Markdown | Element |
| --- | --- |
| the page | `:root` (`html`) > `body` |
| paragraph | `p` |
| headings | `h1`-`h6` |
| emphasis, strong, both | `em`, `strong`, `strong em` / `em strong` |
| strikethrough, highlight, underline | `del`, `mark`, `ins` |
| inline code | `code` |
| code block | `pre > code.language-<lang>`; tokens `span.token.<kind>` |
| quote | `blockquote` |
| callout (GitHub alert) | `blockquote.alert-note` (`-tip`, `-important`, `-warning`, `-caution`) > `p.alert-title` |
| lists | `ul`, `ol` > `li`; `li::marker`; tasks `li.task` > `input[type=checkbox]` (`:checked`) |
| table | `table` > `thead`/`tbody` > `tr` > `th`/`td` |
| rule | `hr` |
| image, figure | `img`; Scholar's figures `figure` > `img` + `figcaption` |
| link | `a[href]`; wiki links `span.wikilink` |
| superscript, subscript, ruby | `sup`, `sub`, `ruby` > `rt` |
| footnotes | `sup > a[role=doc-noteref]`, `section.footnotes` > `div.footnote` |
| maths | `span.math-inline`, `div.math-display` > `span.math-tag` |
| diagrams | `div.diagram` (Mermaid and PlantUML) |
| front matter | `div.frontmatter` > `.property` (screen only; the export leaves it out) |
| citations (Scholar) | `span.citation`, `div#refs.references` > `div.csl-entry` |
| selection | `::selection` |

These are the export's names today. Obsidian's callout markup
(`.callout[data-callout]`) is accepted as an alias of the alerts when themes
are imported (phase 3).

Syntax token classes follow Prism (`.token.keyword`, `.string`,
`.comment`, `.number`, `.function`, `.class-name`, `.operator`), with
`.token.control` for Tinta's control-flow colour; highlight.js names
(`.hljs-keyword` and friends) are accepted as aliases.

### Selectors

Type, class, id, universal and attribute selectors (`[a]`, `=`, `~=`, `|=`,
`^=`, `$=`, `*=`, `i`), compound and complex selectors with descendant, `>`,
`+` and `~` combinators, selector lists, `:is()`, `:where()`, `:not()`,
`:root`, `:first-child`, `:last-child`, `:only-child`, `:nth-child()`,
`:nth-last-child()`, `:nth-of-type()`, `:first-of-type`, `:last-of-type`,
`:empty`, `:checked`, `:lang()`, and the pseudo-elements `::before`,
`::after`, `::marker`, `::selection`. Specificity is CSS's.

`:hover`, `:focus` and other states are reported as unsupported, except
`a:hover`, which Tinta already draws (phase 2).

### At-rules

- `@import "Paper"` or `@import url("base.css")`: a built-in theme by name
  (the built-ins are themselves CSS), or a file beside the sheet. Media
  queries on imports apply.
- `@media`: `screen`, `print`, `(prefers-color-scheme: dark|light)`,
  `(min-width)`/`(max-width)` of the reading column, `and`, `not`, `only`,
  lists.
- `@font-face`: `font-family`, `src: url()` (files beside the sheet; TTF,
  OTF, TTC) and `local()`, `font-weight` (ranges for variable fonts),
  `font-style`. Loaded into a DirectWrite custom font collection; nothing
  is installed.
- `@supports` evaluates against Tinta's own property support, so a theme
  can serve a browser and Tinta at once.
- Anything else (`@keyframes`, `@layer`, `@container`, `@page` until print
  styling lands) is reported.

### Values

Keywords, numbers, percentages, lengths in `px`, `pt`, `pc`, `in`, `cm`,
`mm`, `em`, `rem`, `ex`, `ch`; `calc()`, `min()`, `max()`, `clamp()`;
`var()` with fallbacks and custom properties (inherited, cycles detected);
`inherit`, `initial`, `unset`, `revert`; colours as hex (3, 4, 6, 8 digits),
the 148 named colours, `transparent`, `currentColor`, `rgb()`, `hsl()`,
`hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`, `color-mix()`,
`light-dark()`; `linear-gradient()` for backgrounds.

### Properties

| Property | Phase | Drives |
| --- | --- | --- |
| `color` | 1 | text, headings, links, code, marks, emphasis, quote text |
| `background-color`, `background` | 1 / 2 | page, code, inline code, marks, selection; blocks in 2 |
| `accent-color` | 1 | Tinta's accent: checkboxes, focus, UI highlights |
| `color-scheme` | 1 | dark or light, and which `light-dark()` applies |
| `font-family` (stacks, generic names) | 1 | body, headings, code; every element in 2 |
| `font-size` | 1 | body, code, each heading level; every element in 2 |
| `font-weight`, `font-style` | 1 | each heading level; every element in 2 |
| `line-height` | 1 | the body's line height (today 1.7) |
| `border-*-color` | 1 | quote bar, rules, table grid |
| `border-*` widths and styles, `border-radius` | 2 | quote bar, heading rules (#154), code, tables, inline code |
| `margin-*`, `padding-*` | 2 | block spacing, code and quote padding, cell padding |
| `max-width` on `body` | 2 | the reading column (#82) |
| `letter-spacing`, `text-transform`, `text-align` (incl. `justify`), `text-indent` | 2 | every text element |
| `font-feature-settings`, `font-variant-*`, `font-variation-settings`, `font-stretch` | 2 | OpenType features and variable axes |
| `text-decoration-*`, `text-underline-offset` | 2 | links, `del`, `ins` |
| `list-style-type`, `::marker` `content` | 2 | bullets and numbers per level (#315) |
| `::before`/`::after` `content` with `counter()` | 3 | heading numbering, decorations |
| `counter-reset`, `counter-increment` | 3 | numbering |
| `box-shadow`, `opacity` | 3 | code blocks, images, callouts |
| `caret-color` | 3 | the editor caret |
| `--ui-*` custom properties | 3 (built) | tab strip and caption, panels, menus, popups, cards |
| `--mermaid-*` custom properties | 3 (built) | Mermaid's theme variables, on screen and in the exports |

A property listed for a later phase parses and inherits from phase 1; until
its phase lands, a diagnostic says it is accepted but not drawn yet.

## Diagnostics

Every sheet load produces a list:

- Errors: unparsable rules, unknown at-rules, invalid values.
- Warnings: properties Tinta does not draw (`display has no effect: Tinta
  lays out blocks itself`), selectors that can never match a Markdown page
  (`div.sidebar`), unsupported pseudo-classes, unknown properties with a
  suggestion (`colour`: did you mean `color`?).
- Notes: properties accepted for a later phase.

When the active theme has errors or warnings, a notice shows the first and
the count, and Settings > Appearance lists them all with file and line;
clicking one opens the sheet at that line.

## How it fits

### The engine (`include/css.h`, `src/css*.cpp`)

Plain C++, no Windows calls, tested as a console program:

- `css::parse` turns text into a `Sheet`: rules with parsed selectors and
  declarations, imports inlined, `@font-face` collected, diagnostics with
  file, line and column.
- `css::Engine` holds the sheets in cascade order and a media state (dark,
  print, width). Layout asks it for contexts: `root()`, then
  `child(parent, element, previousSiblings)` for each element on the way
  down. Contexts are interned: two elements that no selector of the loaded
  sheets can tell apart (same tag, classes and attributes the sheets test,
  same answers to the structural pseudo-classes and sibling selectors the
  sheets use, same parent context) share one. A document has hundreds of
  contexts, not one per element.
- `style(context)` computes the `ComputedStyle` once (cascade by origin,
  importance, specificity and order; inheritance; `var()`; `calc()`;
  `em`/`rem`/`%` resolution) and caches it. `pseudo(context, which)` does
  the same for `::before`, `::after`, `::marker` and `::selection`.
- `generation()` changes when sheets or media change, for caches that hold
  styles.

### Origins

1. **Base**: Tinta's look today, written as CSS (`resources/base.css`,
   embedded): body 16px, line height 1.7, code 14px, headings 32/26/22/18/
   16/14 bold, the spacing constants of `render.cpp`. It replaces those
   constants as phases land.
2. **Theme**: the chosen theme. Built-in themes and `themes.ini` sections
   are turned into CSS by one mapping, so every theme goes through the same
   path.
3. **Folder** (phase 3): a `tinta.css` in the document's folder or a parent,
   for that folder's documents and their exports. A chip in the properties
   strip says it is active and turns it off for the document.

Front matter `cssclasses: [wide, serif]` (Obsidian's key) adds classes to
`body` for that document (phase 3).

### themes.ini, mapped

| Key | CSS |
| --- | --- |
| `background`, `text` | `body { background-color; color }` |
| `font`, `headingfont`, `codefont` | `body`, `h1-h6`, `code, pre` `font-family` |
| `dark` | `:root { color-scheme: dark }` |
| `heading`, `h1`-`h6` | `h1-h6 { color }`, then `hN { color }` |
| `link` | `a { color }` |
| `code`, `codebackground` | `pre { color; background-color }`, `code` inherits |
| `inlinecode`, `inlinecodebackground` (`none`) | `code { color; background: none }` |
| `blockquoteborder` | `blockquote { border-left-color }`, `table` grid |
| `accent` | `:root { accent-color }` |
| `syntax*` | `.token.keyword` and the other token classes |
| `highlightbackground`, `highlighttext` | `mark { background-color; color }` |
| `bold`, `italic`, `bolditalic` | `strong`, `em`, `strong em, em strong` `{ color }` |
| `selection` | `::selection { background-color }` |
| `hr` | `hr { border-color }` |

The mapping goes both ways: `themeToCss(D2DTheme)` writes the sheet for a
built-in or INI theme (and the starter file), and the compiler reads a
sheet back into a `D2DTheme` (below), so the INI themes and the built-ins
look exactly as before.

### Phase 1: compiling to the theme Tinta draws with

Phase 1 changes no drawing code. The compiler resolves the contexts a
theme can colour today (`body`, `body > p`, each heading, `a`, `strong`,
`em`, `strong em`, `code`, `pre > code`, `blockquote`, `mark`, `hr`,
`table`, `::selection`, each token class) and fills `D2DTheme`: its
colours, its optional overrides (set only when a rule declared them, so
"inherit" keeps working), its fonts, and a new typography block (body,
code and heading sizes, heading weights and styles, line height) that
`updateTextFormats` and the line-height sites read in place of their
constants. Context-dependent rules that the D2DTheme can't express yet
(`blockquote strong`) get a note saying they apply from phase 2.

Theme strings outlive reloads: compiled themes keep their font names in
storage that is replaced, never mutated, so a `D2DTheme` held by print,
the chooser or a preview never points at freed text (the #265 class of
bug).

### Phase 2: the renderer reads styles

Layout carries a style context down the block tree; each layout function
asks for its element's `ComputedStyle` instead of reading `app.theme` and
constants. Text formats are cached per font key (family stack, size,
weight, style, stretch, features). Spacing, borders, radii, letter spacing,
alignment, list markers and decorations become per element. `live_draw.cpp`
reads the same styles, removing its copied constants.

### Exports and print

- HTML: the export's own CSS stays as the base; a CSS theme's sheet
  follows it, serialized with imports inlined and with the declarations
  Tinta doesn't draw kept, since a browser draws them. `@font-face` files
  are copied beside the export (phase 2).
- Word: headings, body, code, quote and table styles from computed styles
  (phase 2), in place of the copied constants.
- Print and PDF: `@media print` rules apply; without them, today's Paper
  print palette stays (phase 2).

### Following the files

The active theme's file and the files it imports are checked by the 500 ms
file timer that follows documents (#311). A change re-parses, recompiles,
re-applies the theme (a relayout, or a text-format rebuild when fonts or
sizes changed) and refreshes diagnostics. `themes.ini` is followed the same
way.

### The app's chrome

Tabs, panels, menus and the start page keep deriving from the page's
background, text and accent. Phase 3 adds `--ui-background`, `--ui-text`,
`--ui-accent`, `--ui-panel`, `--ui-border` on `:root` for themes that want a
different window.

Built (2026-11-04): the compiler reads the five into optional `D2DTheme`
fields, and the chrome draws through `themeUiText`, `themeUiAccent`,
`themeUiPanel` and `themeUiBorder` (`app.h`, `themes.cpp`), which return
today's derivation when a field is unset, so every other theme draws pixel
for pixel as before. `--ui-background` is the tab strip, the edit rail, the
caption Windows draws (DWM) and the print preview's backdrop; `--ui-panel`
the side panels, menus, popups, notices, dialogs, the settings (its cards
lift from it) and the start page's buttons and cards. The tab in use, a
sheet's canvas bars and text standing on the page keep the page's colours.

Mermaid's theme variables, `--mermaid-primary-color` and the rest in kebab
case, map onto the parts of a diagram Tinta draws (`DiagramColors`: node
fill, border and text, the other text, lines, notes, edge labels,
subgraphs, the background and twelve series colours), read from
`body > div.diagram` so a rule on `.diagram` reaches them. The role resolver
and the flowchart's colours (`flowchartColors`) are shared by the page and
the exports; PlantUML's preamble keeps the page's. Tinta honours no
`%%{init}%%` directive, so nothing there competes with the sheet. The two
families cost `tinta.exe` 20,992 bytes.

### The inspector (phase 3)

Ctrl+Alt+click an element: a card shows its selector path (`blockquote > p >
strong`), the rules that apply in cascade order with file and line, the
ones they beat struck through, Tinta's base sheet where it decides, what
the element inherits, and swatches. Clicking a rule opens its sheet at that
line, where the value is edited; the card does not write the sheet itself.

## Performance and size

- Parsing a 500-line theme takes under 2 ms; computing a context's style
  under 20 us; a document has a few hundred contexts.
- The engine is about 3,000 lines; its cost in `tinta.exe` is measured and
  reported with each phase (budget: 120 KB for phase 1).

## Testing

- `css_tests`: tokenizer and parser edge cases from CSS Syntax 3, error
  recovery, selectors and specificity, the cascade and inheritance,
  `var()` (fallbacks, cycles), `calc()`, every colour function against
  reference values, `light-dark()` and `@media`, `@import`, diagnostics with
  lines and columns, and context interning.
- `theme_css` in-app tests: every built-in theme and the `themes.ini`
  fixtures produce the same `D2DTheme` through CSS as before; a CSS theme
  colours headings, emphasis, code, marks, selection and rules in layout,
  HTML and Word; typography changes heading and body formats; editing the
  file restyles; diagnostics appear.
- Fixtures: `tests/fixtures/theme-css/` with themes (light, dark, light-dark,
  imports, errors) and a mixed document (headings, lists, tasks, a table,
  a quote, callouts, code, maths, a diagram, footnotes).

## Phases

1. **CSS themes** (built; see `tests/css-themes-validation.md`): the engine; the base sheet for colours,
   fonts, sizes and line height; built-ins and themes.ini through the
   mapping; `.css` themes in the chooser; following the file; diagnostics;
   HTML export carrying the sheet.
2. **Per-element styles** (built; see `tests/css-themes-validation.md`):
   the renderer reads computed styles per element, Live its code, list and
   quote looks; spacing, borders, radii, letter spacing, alignment (with
   justify), OpenType features, list markers, reading width; `@font-face`;
   Word and print from styles.
3. **Beyond the page** (built; see `tests/css-themes-validation.md`):
   counters and generated content, folder sheets and `cssclasses`,
   `--ui-*` and `--mermaid-*`, `box-shadow`, `opacity` on pictures and
   `caret-color`, the inspector, importing Typora (`#write`) and Obsidian
   (its variables) themes.

## Decisions

- The extension is `.css`: it is CSS, editors treat it so, and the HTML
  export uses it unchanged.
- Folder sheets apply automatically, with a chip to turn them off (a
  downloaded folder can't hide its own text without the chip saying so).
- The chrome follows variables, not selectors.
