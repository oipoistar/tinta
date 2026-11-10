# The built-in themes' look, polished in their CSS

Validated on Windows with the Release build on 2026-11-10.

## What changed

With the built-in themes drawn from their sheets, their look can be said
in their files (`resources/themes/*.css`). Four things were added, all of
them paint: no text moves and no page grows or shrinks by a pixel.

- **Code blocks** have corners of 8 px and a hairline one shade off their
  ground (`pre { border: 1px solid ...; border-radius: 8px }`), each
  theme's own shade: its code ground mixed a tenth towards its text, a
  seventh in the dark themes.
- **Inline code** sits in chips with corners of 4 px
  (`code { border-radius: 4px }`).
- **A table's header row** stands on the code's ground at full strength
  (`th { background-color }`); it had the same colour at half strength,
  which on most themes could not be told from the page.
- **Marked words in the five dark themes** are set in the theme's own gold
  on a tint of it (`mark { background-color; color }`). Tinta's yellow
  marker over a dark page gave an olive patch under white text. The light
  themes keep the yellow marker.

A strike through the letters' middle came with the step before
(`builtin-themes-styled-validation.md`).

Not changed: any colour of text, headings, links, quotes or callouts; any
size, face, margin or line height.

## Fixture

`fixtures/theme-showcase.md`, with two callouts added and the struck words
on one line (a strike does not pair across a line break of the source,
here or in 4.0.3). `fixtures/builtin-themes-styled.md` as before.
`fixtures/theme-css/builtin-themes.txt` changed in five lines, on purpose:
the `mark` colours of Midnight, Dracula, Forest, Ember and Abyss.

## Automated checks

**`inline_code_theme`** (`page_style_tests.cpp`, `builtInsStyled`), for
each of the ten themes at 1000 and 600 px: the four chips of inline code
have corners of 4; the code block has corners of 8 and an edge of 1 in
another colour than its ground; the table's header has a box on code's
ground at full strength; no other box is rounded or edged; a dark theme's
marked words are in its `mark` colour and a light theme sets none. And as
before every run, box, line and shape stands where it stands under a
`themes.ini` twin without a sheet, text in the same colour, boxes in the
same hue. `unstyled` holds Paper's boxes to those three kinds.
`theme_css_tests.cpp`, `builtIns`: each theme is what
`builtin-themes.txt` says.

Failing checks with one rule taken out of a theme's file:

| Taken out | Failing checks |
|:----------|---------------:|
| Paper's `border-radius: 8px` | 5 |
| Paper's hairline | 5 |
| Paper's `code { border-radius }` | 2 |
| Paper's `th { background-color }` | 2 |
| Midnight's `mark` | 4 |

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
75 of 75.

## Drawn, against 4.0.3

The 150 Markdown fixtures of 4.0.3, drawn by 4.0.3 and by this build with
the window's own drawing code, each run from a fresh settings file:

| What | Frames | Paint differs | Text moved | Height changed |
|:-----|-------:|--------------:|-----------:|---------------:|
| Reading view, ten built-in themes, 1050 and 620 px | 8,510 | 6,975 | 0 | 0 |
| Reading view, eight `themes.ini` themes | 6,810 | 674 | 0 | 0 |
| Source and Live, Paper and Midnight | 2,394 | 1,803 | | |

"Text moved" compares every run's place and the page's height, not its
pixels. The `themes.ini` themes differ only where text is struck, as
before this step: they have no sheet and keep square boxes. The editors
differ in the preview beside the editor and in Live, which draws code
blocks, chips and tables with the theme's look.

Looked at as pictures: the showcase page in all ten themes beside 4.0.3's
(`out/audit/themes/gallery`, not in the repository); code blocks of one
line, of several and with a blank last line at 1050 and 620 px in Arctic
and Midnight; a print page from Midnight (white page, round chips, the
header row tinted in the print palette's grey); Live in Paper.

## Speed and size

Laying out all 150 fixtures in Paper and Midnight takes 897 ms against
843 ms in 4.0.3 (6% more, as in the step before); drawing a frame takes
the same. `tinta.exe` is 5,195,776 bytes, 2,560 more than before this
step, 6,144 more than 4.0.3's themes as a table.

## Limitations

- A theme made in Settings from a built-in one is a `themes.ini` theme: it
  takes the built-in's colours, the `mark` colours too, but not the round
  corners, the hairline or the stronger header row, which only a sheet
  can say. Until `themes.ini` themes are CSS as well, such a copy looks as
  the theme looked in 4.0.3.
- Whether the look is better is a matter of taste; the pictures are for
  the maintainer to judge. Each of the four changes is one rule per file
  and can be taken out alone.
- Screen copies of the real window were black in this session, so the
  pictures come from the drawing code run by the comparison tool.
- Selection, search marks and the caret were not restyled.
