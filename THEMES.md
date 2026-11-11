# Themes

Tinta has ten built-in themes (press `T` to choose one), and a theme of
your own is a CSS file. Tinta draws the page itself, with no web engine,
and reads the part of CSS a theme needs: selectors over the HTML that
Markdown becomes, the cascade, variables and colour functions.

This is the short version. Everything a theme can hold is in
[docs/css-themes.md](docs/css-themes.md).

## Where the file goes

Put a `.css` file in Tinta's `themes` folder:

- `%APPDATA%\Tinta\themes\` for an installed Tinta;
- `themes\` beside `tinta.exe` in portable mode, which is when a
  `settings.ini` lies beside the exe.

Create the folder if it is not there. The file's name is the theme's name:
`Harbour.css` is listed as **Harbour**. Tinta reads the folder when it
starts, so start it again after adding a file, then press `T` and pick the
theme.

While the theme is in use, every save of its file restyles the window at
once. **Settings > Appearance > Edit** opens the file in Tinta itself.

## The quickest theme: your colours on a built-in design

The ten built-in themes share one design and differ in their colours,
which each keeps as variables. Start from one with `@import` and set the
variables:

```css
/* Harbour.css */
@import "Midnight";

:root {
  accent-color: #ffb454;   /* the window's accent: focus, toggles */
  --page: #101820;         /* the page */
  --ink: #d7dde4;          /* text */
  --ink-strong: #f2f5f8;   /* headings */
  --ink-soft: #9aa5b1;     /* quotes and list markers */
  --link: #ffb454;
  --line: #2a3644;         /* hairlines: under headings, between rows, rules */
  --line-firm: #4a5868;    /* under a table's header */
  --surface: #17222d;      /* code blocks and inline code */
  --surface-edge: #2a3644; /* the code block's edge */
  --code: #9fd6c2;         /* code */
  --quote: #3d5166;        /* a quote's bar */
}
a { text-decoration-color: rgba(255, 180, 84, 0.4); }
```

Start from `"Paper"` for a light theme and from `"Midnight"` for a dark
one; any of the ten names works (`Paper`, `Sakura`, `Arctic`, `Meadow`,
`Dusk`, `Midnight`, `Dracula`, `Forest`, `Ember`, `Abyss`). Two things are
rules and not variables: the tint of a link's underline, as above, and the
colours of code, which are `.token.keyword`, `.token.string`,
`.token.comment`, `.token.number`, `.token.function`, `.token.class-name`
and `.token.control`.

## Rules of your own

After the variables, style any element as you would on a web page:

```css
/* Novel.css */
@import "Paper";

body {
  font-family: Georgia, serif;
  font-size: 17px;
  line-height: 1.65;
  max-width: 44em;                 /* the reading column */
}
h1 { text-align: center; }
p + p { text-indent: 1.5em; }
blockquote { font-style: italic; }
li::marker { color: #b35900; }
tbody tr:nth-child(even) td { background-color: #f1ece0; }
.token.keyword { color: #a626a4; }
::selection { background-color: rgb(51 102 204 / 0.3); }
```

The elements are the ones the HTML export writes: `body`, `p`, `h1` to
`h6`, `a`, `strong`, `em`, `mark`, `code` (inline) and `pre` (code
blocks), `blockquote` (callouts are `blockquote.alert-note`, `-tip`,
`-important`, `-warning` and `-caution`), `ul`, `ol`, `li`, `table`, `th`,
`td`, `hr`. Text, spacing, borders, round corners and backgrounds are
drawn; layout properties such as `display` or `position` do nothing, and
Tinta says so in a notice with the file and the line.

A theme needs no `@import`. Without one it starts from Paper's colours and
type, or Midnight's when its page is dark, and gets none of their rules.

## Light and dark in one file

`color-scheme: light dark` makes a theme follow Windows, and `light-dark()`
gives each colour its two values:

```css
/* Daylight.css */
@import "Paper";

:root {
  color-scheme: light dark;
  --page: light-dark(#fbf8f1, #1b1d22);
  --ink: light-dark(#1f2328, #d8dee9);
  --ink-strong: light-dark(#14171a, #eceff4);
  --ink-soft: light-dark(#5b6168, #a3abb8);
  --link: light-dark(#0b6e99, #88c0d0);
  --line: light-dark(#e2ded5, #33373f);
  --line-firm: light-dark(#bdb8ae, #565c66);
  --surface: light-dark(#f1ece0, #23262d);
  --surface-edge: light-dark(#e2ddd0, #33373f);
  --code: light-dark(#3b4a5a, #a3be8c);
  --quote: light-dark(#c9c4b8, #4c566a);
}
.token.keyword { color: light-dark(#8250df, #81a1c1); }
.token.string { color: light-dark(#116329, #a3be8c); }
.token.comment { color: light-dark(#6e7781, #7b88a1); }
```

## Good to know

- **See what styles what.** With a CSS theme in use, `Ctrl+Alt+click`
  anything on the page: a card lists the rules that style it, with file
  and line, and a click on one opens the sheet there.
- **Fonts.** `font-family` takes the first face the PC has. A theme can
  bring its own with `@font-face` and font files beside the sheet.
- **The window.** `--ui-background`, `--ui-text`, `--ui-accent`,
  `--ui-panel` and `--ui-border` on `:root` colour the tab strip, panels
  and menus.
- **Heading lines.** Settings > Appearance > Heading separators decides
  the line under level 1 and 2 headings under every theme, yours too.
- **One folder only.** A `tinta.css` in a document's folder styles that
  folder's documents on top of whichever theme is in use.

## Without writing CSS

- **Settings > Appearance > New** opens the colour editor: start from any
  theme, change its colours and faces, save. The theme is kept in
  `themes.ini` and also written as a CSS file in the `themes` folder,
  which you can go on editing by hand.
- **Settings > Appearance > Import** takes a Typora or Obsidian theme's
  `.css` file and writes a Tinta theme from it.
