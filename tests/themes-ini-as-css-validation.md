# themes.ini themes as CSS files

Validated on Windows with the Release build on 2026-11-11.

## What changed

Every theme is to be a CSS file. `themes.ini` is still read; each of its
sections is now also written as `themes\<name>.css` and the page is drawn
from that sheet, as a built-in theme's is from its own. The theme keeps its
name and its place in the list, and looks as it did.

- **The file follows its section** while it stays as Tinta wrote it: a
  hand edit of `themes.ini` (#310) or a save from the theme editor writes
  it again. Its first comment holds a sum of what follows. Once the two
  disagree the file was changed by its owner: from then on the file is the
  theme, read as any CSS theme is, **Edit** opens it, and its section is
  read no more. A file taken away is written again.
- **A section taken out** of `themes.ini` takes its unchanged file with
  it; a changed file stays as a CSS theme of its own. With no `themes.ini`
  at all the files are the themes. A sheet of the user's own that has a
  section's name is left alone, and both are listed, as before.
- **The chosen themes are kept by name** (`themeName=`, `lightThemeName=`,
  `darkThemeName=` in `settings.ini`) beside their places in the list. A
  theme added or taken away moved every place after it; a
  `settings.ini` without names is read by place as before. A theme saved
  from the theme editor while CSS themes exist now comes back at its own
  place (it came back as the last theme of the list), and the themes for
  light and dark are found again after such a save or an import.
- **Heading separators**, the setting, still decides the lines under level
  1 and 2 headings under every one of these themes: the written files hold
  no border for headings.

What the written CSS had to say for the look to stay, found by drawing
both ways:

- Inside a link everything is the link's colour, code and emphasis too;
  CSS would give them their own. The files colour `code:not(a *)`,
  `strong:not(a *)` and `em:not(a *)`.
- Text that is both bold and italic takes `bolditalic=`, else `bold=`,
  else `italic=`, whichever is inside the other; CSS would let the inner
  one win.
- A table's header row is in `heading=`, which `h1` to `h6` no longer say
  once a level has a colour of its own: the files then give `th` it.
- A section starts from Paper, dark or not; a CSS theme starts from
  Midnight when dark, whose marker is gold now. A section's file starts
  from Paper as its section does.
- Faces are followed by the ones Tinta falls back to, in its own order,
  so the HTML export's page finds a face on another system.

## Fixture

`fixtures/themes-ini-as-css.md`: code, bold, italic and both inside links,
headings, marks, strikes, quotes, a callout, table cells and list items; a
link to a missing file with code in it; footnotes; a code block; math.
`fixtures/themes-ini-as-css.ini`: four themes: one of three keys; a dark
one with every key, a colour for each heading level and three faces; one
with faces this PC does not have and inline code without a box; one whose
name a file cannot have (`Ini: "odd" / name?`).

## Automated checks

**`inline_code_theme`** (`page_style_tests.cpp`):

`iniThemesAsCss`: each of the four themes lays the fixture and
`builtin-themes-styled.md` out at 1000 and 600 px three ways: without the
files (as when they cannot be written), from its file with the section as
the theme, and with the file changed by its owner, so that the file is the
theme. In all 32 comparisons every run, box, line and shape (up to 285
parts) is in the same place, colour, strength and shape, and the page as
tall. Each theme keeps its name and place; its file exists, named for it
(`Ini_ _odd_ _ name_.css`). The file starts with its note, holds the
section's colours and the faces with their fallbacks, and no heading
border. Heading separators off takes exactly the three lines under h1 and
h2 away under Paper, Midnight and two of the themes. A section changed by
hand writes its file again; a file changed by hand is noticed, is the
theme under the section's name and in its place, and stays as left when
the section changes; taken away it is written again; a section with the
name of a sheet without the note is drawn without a sheet and the sheet is
untouched and listed; a theme saved from Settings gets its section and
file, and follows a second save.

`chosenByName`: the chosen themes are written by place and by name; after
a sheet and a section are added they are found where they stand now; one
that is gone gives way to Paper, or Midnight for dark; a `settings.ini`
without names is read by place; a built-in is found by its name.

The tests that were there for `themes.ini` (#208, #264, #265, #278, #310
and the theme editor's) now run with every section drawn from its file,
and pass unchanged. `builtInsStyled` keeps its twins without files.

Failing checks with one thing taken out at a time:

| Taken out | Failing checks |
|:----------|---------------:|
| Code in a link keeps the link's colour | 22 |
| Bold in a link keeps the link's colour | 8 |
| Both bold and italic: bolditalic, else bold, else italic | 4 |
| `th` in the heading colour | 4 |
| A changed file is told from a written one | 7 |
| A section's file is not listed a second time | 23 |
| A gone section takes its unchanged file | 5 |
| A section's file starts from Paper | 4 |
| The chosen theme is found by name | 3 |
| A changed section writes its file again | 4 |

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
75 of 75.

## Drawn, against 4.0.3

The 150 Markdown fixtures of 4.0.3 in eight `themes.ini` themes (inline
code with and without a box, a hand-written section with notes and odd
spacing, heading levels, bold and italic colours, a serif face), 1050 and
620 px, drawn by 4.0.3 from the sections and by this build from their
files: of 6,810 frames, 674 differ, in the 21 fixtures that strike text
(the strike's new height, kept since the step before); no run of text
moved and no page changed its height. Before the rules above were in the
files, 96 more frames differed, all of them code inside links.

Exports of all 150 fixtures in four of those themes, with and without the
files (1,200 files): every HTML page is the same but for the theme's
sheet, which now follows Tinta's own styles; 588 of 600 DOCX files are the
same byte for byte, and the 12 others differ in pictures of diagrams by 8
to 309 pixels of about a million each, with nothing to see (two runs with
the files give the same bytes). Print pages (first six, A4): in three of
the four themes every one of 1,410 pages is the same; the fourth theme's
pages differ from run to run by themselves (274 of 470 between two runs
without the files), so their sums say nothing, and two of its documents
compared as pictures have no differing pixel.

## In the built app

`tinta.exe` in a portable folder of its own with the eight themes'
`themes.ini` and a `settings.ini` that names themes by place only (16, 17
and 12): it starts, writes the eight files into `themes`, answers a
message in 0.3 ms, and at closing keeps `themeName=Hand written`,
`lightThemeName=Light serif` and `darkThemeName=Inline Code Dark`, the
themes those places meant. Started again: a section's link colour changed
by hand is in its file four seconds later; a rule added to the file by
hand, and then the section changed again, leaves the file as its owner
left it. The window closes with exit code 0 both times.

## Speed and size

Laying out the 150 fixtures in a themes.ini theme takes 399 ms from its
file against 363 ms without (10% more, the cost of the styled path, as
for the built-in themes); drawing a frame takes the same. `tinta.exe` is
5,211,648 bytes, 15,872 more.

## Limitations

- Screen copies of the real window are black in this session; what the
  window draws was compared through the drawing code, as above.
- A theme made in Settings from a built-in one still takes the built-in's
  colours only: its file has no round corners or hairline, since nothing
  in a section says them. They can be added to the file by hand, which
  then is the theme.
- A CSS theme of the user's own that sets a border under headings keeps
  it whatever Heading separators says, as before this change.
- An older Tinta reading the same `settings.ini` goes by place, and does
  not know the files.
- The diagram pictures in DOCX exports differ by a few pixels with and
  without the files; why was not found.
