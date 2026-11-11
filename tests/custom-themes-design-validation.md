# A theme made from a built-in one keeps its design

Validated on Windows with the Release build on 2026-11-11.

## What changed

The ten built-in themes are one design since their redesign: type sizes,
hairlines, round code blocks, callouts on a tint, tables with lines across
only. All of that is rules in their CSS files. A theme made in **Settings >
Appearance** from one of them is a `themes.ini` section, which has keys for
colours and faces only, so it took the built-in theme's colours and drew
them in the look themes had up to 4.0.3.

- **`design=1`.** A theme carries the design of the theme it was made from
  (`D2DTheme::design`): the built-in themes have it, a theme made from one
  in the theme editor gets `design=1` in its section, and a theme made
  from that one again.
- **The section's file.** For such a section `themes\<name>.css` starts
  from a built-in theme's sheet (`@import "Paper"`, or `"Midnight"` when
  dark) and names the section's colours as that sheet's variables (`--page`,
  `--ink`, `--link` and the others). Rules follow only for what no variable
  carries: the faces, the code tokens, the link's underline tint, and the
  optional keys (heading levels, inline code, marks, emphasis, selection).
  The section's type sizes are the built-in themes', for what draws from
  the theme and not the sheet.
- **Worked out, not copied.** The quieter ink (`--ink-soft`), the firm line
  under a table's header and the code block's edge are worked out from the
  section's page and ink with the arithmetic the built-in themes' own were
  made with. So is the hairline (`--line`): the theme editor does not take
  a built-in theme's along as `hr=`, since it has no control to change it
  and it would stay behind when the page's colour changes. `hr=` written
  by hand sets it.
- **A mark's tint.** A dark built-in theme marks words on a translucent
  ground, and a section's colours are RRGGBB: saved, the mark's ground is
  the colour the tint comes to on the page. Before, it was saved as the
  full colour, light text on a light ground.
- **Every theme there was keeps its look.** A section without the key is
  read, written and drawn as before, also when it is saved again from the
  editor or a new theme is made from it. `design=1` can be written into a
  section by hand, and taken out again; the file follows.

## Fixture

`fixtures/themes-ini-design.ini`: a light theme in the design with a
heading face of its own; a dark one whose headings follow its text face,
with a colour for one heading level, for inline code, for marks and for
rules (`hr=`); and the light one's colours again without the key. Shown
with `fixtures/builtin-themes-styled.md` and `fixtures/theme-showcase.md`
(headings, callouts, quotes, lists, a table, code, links, marks, struck
text, math). `fixtures/themes-ini-as-css.md` is the second document of the
comparison below.

## Automated checks

**`inline_code_theme`** (`page_style_tests.cpp`, `madeFromBuiltIns`):

- Each of the ten built-in themes is taken as the theme editor takes it
  (`themeAsBase`) and saved under a new name. The new theme has the design,
  is still its section for the editor, has the built-in theme's type sizes
  and faces, and its file starts from Paper's or Midnight's sheet with the
  colours as variables. It then lays out two documents at 1000 and 600 px,
  as does the built-in theme: in all 40 comparisons every run, box, line
  and shape (up to 285 parts) is in the same place, colour, strength and
  shape, and the page as tall. A dark theme's mark is compared as the
  colour it comes to. The ten sections say `design=1` and none holds
  `hr=`.
- A colour changed: the variable and the underline's tint follow, the
  page is drawn in the design with it, and Paper's hairline comes out as
  Paper's own (`#dfdad1`) without being a key. Made dark, the file starts
  from Midnight and its lines are worked out anew.
- A section without the key: no built-in sheet, Tinta's own sizes, no
  round code block; saved again it has no `design=` and draws what it
  drew, part for part; a theme made from it has its look too. With
  `design=1` written by hand the file follows into the design; without its
  file (as when it cannot be written) the section still has the type
  sizes; a sheet that imports the theme by name gets the design in its
  colours; its file changed by hand is the theme, design and all.
- The fixture's three themes at both widths: the round code block with an
  edge, the table with lines across only, a quote's text set back and
  still at 4.5 to 1 in the two with the key; the old code block, the grid
  and the page's ink in the one without. Headings take the section's
  heading face, or its text face where it has none; `hr=`, a heading
  level's, inline code's and a mark's colours are in the file.

**`inline_code_theme`** (`theme_tests.cpp`): a theme made through the
theme editor's own clicks from Paper has `design=1` in its section and the
design's sizes once applied.

`iniThemesAsCss` passes with one expectation changed on purpose: a theme
made from Arctic holds its link as `--link`. Its four themes without the
key still draw from their files as without them, part for part.

Failing checks with one thing taken out at a time:

| Taken out | Failing checks |
|:----------|---------------:|
| A built-in theme hands its design on | 89 |
| `design=` is written into the section | 88 |
| `design=` is read from the section | 109 |
| The section's file starts from a built-in sheet | 84 |
| The section has the design's type sizes | 34 |
| The quiet ink worked out as the built-in themes' was | 40 |
| The firm line worked out as the built-in themes' was | 40 |
| A mark's tint kept as it comes out on the page | 5 |
| Headings without a face take the text's | 1 |
| The link's underline is its tint | 33 |
| The section's sheet finds the built-in one | 54 |
| A file changed by hand keeps the design | 1 |
| Imported by name, the design comes along | 1 |
| A built-in theme's hairline is not taken along | 1 |
| A worked-out hairline is not written as `hr=` | 2 |

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
75 of 75.

## Drawn

Looked at as pictures (`out/audit/themes/gmade`, not in the repository):
the showcase page at 1000 px in the fixture's light theme without the key
beside the same colours with it, and in Midnight beside the fixture's dark
theme. With the key: the heading sizes and hairlines, the tinted callouts,
the table without a grid, the round code block; without it, the page as
4.0.3 drew it.

Against 4.0.3, for the themes there were: the eight `themes.ini` themes
of the earlier comparison (none has the key) over 150 fixtures at two
widths, 6,810 frames, differ in the same 674 frames that strike text as
before this change and in no other, with no text moved.

## In the built app

`tinta.exe` in a portable folder of its own with the fixture's
`themes.ini` and a `settings.ini` that names `Design dark`: it starts on
`theme-showcase.md`, writes the three files into `themes` (`Design
dark.css` with `@import "Midnight";`, `Design light.css` with `@import
"Paper";`, `Design none.css` with no import), answers a message in 0.2 ms,
and four seconds after the dark section's `link=` is changed by hand its
file holds the new `--link` and underline tint. The window closes with
exit code 0 and `themeName=Design dark` kept. No theme was made through
the real window's theme editor; that path is the test's, through the
editor's own click handling.

## Speed and size

Laying out the 150 fixtures takes 442 ms in the fixture's theme with the
key, 452 ms in Paper and 428 ms in the theme without the key (two runs
each, within 3 ms of one another); drawing a frame takes the same in all
three (815 to 828 ms for 150). The first layout after a change of theme
is 7% slower with the key than without (815 against 761 ms); Paper's
takes 831 ms. `tinta.exe` is 5,238,784 bytes, 9,728 more.

## Limitations

- The theme editor has no switch for the look: a theme has the design of
  what it was made from. To change an existing theme, write `design=1` into
  its section or take it out.
- Colours the editor has no control for stay the base theme's, as before:
  the code tokens, the accent, a quote's bar. Made dark from a light theme,
  a new theme keeps the light theme's token colours.
- A dark section in the design with no `highlightbackground=` gets
  Midnight's gold mark on the page, since its file starts from Midnight's
  sheet; Live and the exports use Tinta's own dark mark, a gold of nearly
  the same strength.
- Where the file cannot be written the section has the design's type
  sizes but not its rules.
- An older Tinta reading the same `themes.ini` does not know the key and
  draws the theme in the old look.
- Screen copies of the real window are black in this session; the
  pictures come from the drawing code run by the comparison tool.
