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

The rest of CSS that Tinta will draw in later versions, such as spacing,
borders, letter spacing and list markers, is read and kept but not drawn
yet. Mistakes and properties Tinta does not use are reported in a notice
with the file and line, and the rest of the sheet applies.

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
