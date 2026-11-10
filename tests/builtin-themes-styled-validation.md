# The built-in themes style the page from their sheets

Validated on Windows with the Release build on 2026-11-10.

## What changed

A built-in theme's CSS file was read into the theme's colours and faces
and then put aside: the page was drawn the old way, and only a theme of
your own went through the rules for single elements (`page_style.h`). The
built-in themes now hand the page their sheets too, so all ten are drawn
by the same code as a CSS theme of your own, and a rule added to one of
their files (a corner's radius, a selection colour) takes effect.

The page has to look as it did. Drawn from their sheets, the themes first
differed from 4.0.3 in 1,459 of 8,510 frames, in paint only: no text
moved. What was behind it is fixed, and holds for a theme of your own that
imports a built-in or sets the same properties:

- A callout lost its colour. `blockquote { border-left-color }` reached
  the note, tip, important, warning and caution boxes, whose bar and title
  turned the plain quote's grey. A callout keeps its own colour unless the
  sheet has a rule that gives callouts another than plain quotes.
- A footnote's number, and other inline elements Tinta colours itself,
  took the body's colour, which had only come down to them by inheritance.
  They take a sheet's colour when a rule names them.
- A link to a file that is not there was no longer drawn fainter once the
  sheet gave links a colour.
- The page's background was set a second time, to the theme's own colour.

One difference is kept, and now holds without a sheet too: a strike goes
through the middle of a lowercase letter (0.28 of the size above the
baseline), where it sat low on the line (commit 58c1f94 had it so for CSS
themes only). It is the one thing that looks different from 4.0.3.

Also:

- The theme files name fallbacks (`"Segoe UI", sans-serif`), because the
  HTML export carries a theme's sheet and a browser on another system
  would otherwise fall to its default face. A built-in theme is the first
  face it names on every PC; Tinta stands in for a missing one as before.
- Ctrl+Alt+click (the inspector) works under a built-in theme and names
  the theme where a file's name and line would stand; such a row opens
  nothing.

## Fixture

`fixtures/builtin-themes-styled.md`: the five callouts and a note inside a
plain quote; links to the web, to missing files and wiki pages; two
footnotes; a heading with a link, struck and marked words; struck, marked,
underlined, inserted and deleted text, subscript and superscript; a table
with struck, marked and linked cells; nested lists with tasks; a code
block; inline math inside struck text; a rule.

## Automated checks

**`inline_code_theme`** (`page_style_tests.cpp`, `builtInsStyled`): each
of the ten themes hands the page a sheet. Each is laid out at 1000 and
600 px from its sheet and again as a `themes.ini` twin with the same
colours and faces, which has none: the page is as tall, and every run,
box, line and shape (223 or more parts) stands in the same place, to a
hundredth of a pixel, in the same colour. Looked at one by one: a note's
title and bar are the note's colour and a plain quote's bar the theme's; a
link to a missing file has 0.45 of a found one's opacity; a strike is at
the letters' middle with and without a sheet; the page has no background
of its own. `inspecting` opens the inspector under Paper and finds
`color: #3d3329   body   Paper` and no `.css`.

Failing checks with one fix taken out at a time:

| Taken out | Failing checks |
|:----------|---------------:|
| A callout's bar keeps its colour | 60 |
| A callout's title keeps its colour | 40 |
| An inherited colour is not an inline element's own | 20 |
| A missing file's link stays faint | 40 |
| The strike's height without a sheet | 40 |
| The page background not set twice | 21 |
| The built-in themes hand over their sheets | 12 |

**`superscript_and_learn`** chose its pairs of body and code faces by
writing into the theme in use, which a sheet now overrules; it makes a CSS
theme for each pair (`@import` of the built-in, then the two faces), as a
user would.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
75 of 75.

## Drawn, against 4.0.3

Every Markdown fixture of 4.0.3, 150 files, drawn by 4.0.3 and by this
build with the window's own drawing code, each run from a fresh settings
file. With the strike put back where 4.0.3 has it, for the comparison
only:

| What | Frames | Identical |
|:-----|-------:|----------:|
| Reading view, ten built-in themes, 1050 and 620 px | 8,510 | 8,510 |
| Reading view, eight `themes.ini` themes | 6,810 | 6,810 |
| Source and Live with the preview beside them, Paper and Midnight | 2,394 | 2,394 |

As committed, with the strike at the letters' middle: 842 reading-view
frames differ, in 21 fixtures, all of which strike text; in none of them
did a run of text move or the page's height change. The `themes.ini`
themes differ in 674 frames of the same 21 fixtures, the editors in 216
frames of 18 of them (the preview beside the editor).

Print pages (A4, first six of each document, Paper, Arctic, Midnight and
Ember): the pages drawn from the sheets and those drawn without differ
only in the 20 fixtures that strike text. HTML and DOCX exports of all 150
fixtures in the same four themes, 1,200 files: every DOCX is the same,
byte for byte, with and without the sheets, and every HTML page is the
same but for the theme's sheet, which now follows Tinta's own styles as a
CSS theme's does. Its rules repeat those styles' colours and faces;
`accent-color` and `color-scheme` are new to the page and tint a
browser's checkboxes and scrollbars to the theme.

The tool is not part of the repository; it is kept under the ignored
`out/audit/sweep/`.

## Speed and size

Laying out all 150 fixtures in Paper and Midnight takes 901 ms against
843 ms in 4.0.3, 7% more; the longest (`highlight-long-document.md`) 79 ms
against 74 ms. Drawing a frame takes the same (1,635 ms against 1,621 ms
for all 300). The first layout after a theme change is 14% slower, which
is the sheet being read. `tinta.exe` is 5,193,216 bytes, 1,536 more.

## Limitations

- Screen copies of the real window were black in this session (the remote
  desktop was not shown), so the frames above come from the drawing code
  run by the comparison tool and by the tests, not from the window.
- A theme changed in memory after it was applied (as tests did for faces)
  is overruled by the theme's sheet where the sheet names the same thing.
  Nothing in the app changes a theme that way.
- The comparison covers the fixtures at two widths, at 100% zoom and
  scale; other zooms were not swept.
