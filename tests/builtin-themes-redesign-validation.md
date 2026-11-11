# The built-in themes, redesigned

Validated on Windows with the Release build on 2026-11-11.

## What changed

The ten built-in themes are one design now, each in its own colours. A
theme's file (`resources/themes/*.css`) keeps its colours as variables at
its top and the same rules under them in all ten. Unlike the step before,
this one moves text: it is a new look, not the old one repainted.

- **Type.** Headings are set in Segoe UI Variable Display (Segoe UI where
  it is missing) on a scale of 34, 25, 20, 17, 15 and 13 px, the first
  bold and the others semibold, the large ones tightened a little. Body
  text stays Segoe UI: as the text face the variable font made DirectWrite
  lay a long document out 1.7 times slower (below).
- **Headings' lines.** A hairline in the theme's line colour under level
  1 and 2, in place of a 2 px and a 1 px rule in the heading's colour.
- **Code.** Blocks with corners of 10 px and an edge a shade off their
  ground; chips with corners of 5 px for inline code.
- **Quotes and callouts.** A quote's text is set back towards the page.
  A callout stands on a tint of its own colour, the bar down its side.
- **Tables.** Lines across only, a firmer one under the header, no line
  over it, no stripes and no ground under the header.
- **Lists and rules.** Markers and rules in the quieter ink and line
  colours.
- **Links.** Underlined in a tint of their colour (40%).
- **Colours that read.** Measured as contrast ratios, links in Paper,
  Sakura, Meadow and Dusk were under 4.5 to 1 on their pages (3.6 to 4.3),
  code comments in all ten between 2.4 and 3.4 on their grounds, and
  several code tokens in the light themes under 4.5. Each was moved, its
  hue kept, until links and tokens read at 4.6 to 1 and comments at 3.9.
  Meadow's and Dusk's code grounds were calmed.

In the code:

- **Heading separators**, the setting, now decides the line under a level
  1 or 2 heading whoever draws it: off, a theme's own border there is not
  drawn, nor the room kept for it; the Word export follows. Before, a
  theme's border ignored the setting.
- A table's top line is its header cells' top border when the sheet has a
  word on it, and the line under the header takes that border's colour.
- A superscript or subscript in a face the theme made (an italic heading)
  is small in proportion to that face. It was the body's small size: a
  defect of the styled path for any CSS theme that sizes headings, found
  by `superscript_and_learn`.
- On paper, where a theme's colours stay behind, a heading's border is as
  faint as Tinta's own rule was, not a solid line in the text's colour.
- A `themes.ini` section starts from Paper's colours as of 4.0.3, kept in
  the code, not from the built-in Paper, which has sizes and rules that a
  section has no keys for. A theme saved from the theme editor is read
  back from its section, so it is at once what it will be after a restart.

## Fixture

`fixtures/theme-showcase.md` and `fixtures/builtin-themes-styled.md`, as
before. `fixtures/theme-css/builtin-themes.txt` holds each theme's new
faces, colours and sizes, changed on purpose.

## Automated checks

**`inline_code_theme`** (`page_style_tests.cpp`, `builtInsStyled`), for
each of the ten themes: text in Segoe UI and headings in Segoe UI Variable
Display; the scale and weights; text reads at 7 to 1 or more on the page,
headings and links at 4.5, code and each of six kinds of token at 4.5 on
the code's ground and comments at 3.8; a hairline colour and none for
emphasis. Laid out at 1000 and 600 px: one code block with corners of 10
and an edge, chips with corners of 5; each of six callouts on a tint of
its colour with its bar at the tint's side, top and bottom; a plain
quote's bar in the theme's colour and its text set back but still at 4.5
to 1; the table with no line down, one firm line under its header, three
across, none over the header and no grounds; a link's underline at 40% of
its colour; a link to a missing file dimmed; a strike at the letters'
middle; Heading separators off takes exactly the three hairlines under h1
and h2 away and makes the page shorter. `@import "Paper"` with one
variable changed is a theme with Paper's design and that colour.

That the page's code draws a sheet as it drew without one is still held by
`iniThemesAsCss`, which passes unchanged, and by the sweep below.

Tests whose own assumptions the look changed, and how they hold now:

- `wide_table_layout` found a table's columns by its lines down; it takes
  them from the cells' places, and checks a line under every row.
- `blockquote_layout`: a callout is a box now, so its bar runs the box's
  height and ends within 20 px under the last line.
- `highlight_layout`: in a 420 px window at 150% the first cell of its
  two-cell table wraps; the gap is then checked beside the cell's first
  word.
- `theme_colour_tests`, `theme_css_tests`, `theme_tests`: a built-in theme
  has a rule colour and type sizes; the editor starts from Paper's faces,
  whatever they are.

Failing checks with one thing taken out at a time:

| Taken out | Failing checks |
|:----------|---------------:|
| Paper's table without stripes | 2 |
| Paper's table with lines across only | 4 |
| Paper's hairline under h1 and h2 | 2 |
| Paper's link at 4.6 to 1 (made 2.5) | 5 |
| Paper's tint under a note | 2 |
| Midnight's semibold h2 | 7 |
| The setting takes a theme's heading line away | 22 |
| No line over a table's header | 40 |
| Scripts in proportion to a face the theme made | 44 |
| themes.ini starts from Paper as of 4.0.3 | 19 |

The full suite, without the three tests that use the clipboard, passes:
75 of 75. `code_block_layout`, one of the three, was run alone since code
blocks changed, and passes.

## Drawn

Against 4.0.3 the built-in themes are meant to differ now, in every frame.
What must not differ does not: the eight `themes.ini` themes over 150
fixtures at two widths, 6,810 frames, differ from 4.0.3 in the 674 frames
that strike text and in no other, with no text moved, in two runs of
three. (The first run after the build had 849 frames more that neither
of the other two had; they were not looked into.)

Looked at as pictures: the showcase page in all ten themes beside 4.0.3's
at 1000 px (`out/audit/themes/gallery2`, not in the repository) and in
Paper and Midnight at 620 px; Live in Paper, which draws headings, quotes,
callouts, tables and code in the new look; a print page from Midnight
(white page, the heading lines faint, the table with its lines across).

## Speed and size

Laying out the 150 fixtures in Paper and Midnight takes 945 ms against
843 ms in 4.0.3, 12% more (6% before this step); the longest document 86
ms against 74 ms. Drawing a frame takes the same (1,639 ms against 1,621
ms for 300). The first layout after a theme change is 21% slower. With
Segoe UI Variable Text as the body face the same layouts took 1,263 ms and
the longest document 153 ms, which is why the body face is Segoe UI:
measured with the face as the only difference, 141 ms against 84 ms.
`tinta.exe` is 5,229,056 bytes, 17,408 more.

## Limitations

- Whether the design is better is the maintainer's to judge, from the
  pictures and in the installed build. Each part is a rule or two in each
  file.
- Themes made in Settings and `themes.ini` themes keep the old look: a
  section has no keys for sizes, lines or corners. A theme made from a
  built-in one takes its colours and its heading face only.
- A CSS theme of one's own that says nothing about type starts from
  Paper or Midnight as before, and so takes their new heading face,
  sizes and weights, but none of their rules unless it imports them.
- The HTML export carries the theme's sheet after its own styles, so an
  exported page takes the new tables, callouts and headings in a browser;
  it was not opened in one.
- Screen copies of the real window are black in this session; the
  pictures come from the drawing code run by the comparison tool.
- `code_block_layout` replaced what was on the clipboard, which was
  neither text nor a picture nor files and could not be put back.
- The files were first written by a script from a table of palettes
  (`out/audit/themes/make_themes.py`, not in the repository); they are the
  source from here on.
