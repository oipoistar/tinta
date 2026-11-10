# The built-in themes as CSS files

Validated on Windows with the Release build on 2026-11-10.

## What changed

The ten built-in themes were a table of colours in `themes.cpp`. Each is
now a CSS file under `resources/themes` (`paper.css` to `abyss.css`), the
same language a theme of your own is written in. The build compiles the
files' text into the executable, and a theme is read from its sheet when
it is first asked for. `@import "Paper"` in a theme of your own starts
from that file as written.

Nothing about how the themes look changes in this step: the files say what
the table said, colour for colour and face for face.

## How

- `CMakeLists.txt` writes `builtin_themes.inc` from the files, in the
  chooser's order, five light and five dark. A changed file configures the
  build again.
- `builtinTheme(index)` (`themes.cpp`) reads a sheet with
  `compileCssTheme`, once, from nothing: a built-in theme starts from no
  other, so all it is stands in its file. `THEMES[i]` reads as before, for
  the code and tests that use it.
- `compileCssTheme` takes the theme a sheet starts from; left out, it is
  Paper or Midnight, as for a theme of your own.

## Fixture

`fixtures/theme-showcase.md`: one page with every kind of block a theme
colours: headings of five levels, a paragraph with bold, italic, inline
code, a link, marked and struck words, lists three levels deep, tasks,
numbered steps, a quote, a table with aligned columns, a `cpp` block,
inline and display math, a rule and a footnote.
`fixtures/theme-css/builtin-themes.txt` is what each of the ten themes
comes to, as text, taken from the table before it was removed.

## Automated checks

**`inline_code_theme`** (`theme_css_tests.cpp`, `builtIns`): there are ten
themes; each one's faces, its fifteen colours and everything it leaves to
Tinta are, as text, what `builtin-themes.txt` holds for it; the first five
are light and the last five dark; and a theme is read once and stays
where it is. With one digit of one colour changed in `paper.css` the test
fails 3 checks.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
75 of 75.

## Drawn

Every Markdown fixture of 4.0.3, 150 files, drawn by 4.0.3 and by this
build with the window's own drawing code, a window's height at a time,
each frame's pixels hashed, each run from a fresh settings file:

| What | Frames | Identical |
|:-----|-------:|----------:|
| The reading view, ten built-in themes, 1050 and 620 px | 8,510 | 8,510 |
| The reading view, eight `themes.ini` themes | 6,810 | 6,810 |

The tool is not part of the repository; it is kept under the ignored
`out/audit/sweep/`.

## Speed and size

The first theme is read in 2.1 ms, which includes DirectWrite's list of
installed faces, and each further one in 0.13 ms; only the themes asked
for are read. `tinta.exe` is 5,191,680 bytes, 2,048 more than with the
table.

## Limitations

- Exports, print pages, the editors and the window's panels were not
  drawn again for this step. The editors were compared before it, on the
  merge, and use the same themes.
- A built-in theme has no sheet of its own on the page yet: it reaches
  the page as the colours and faces read from its file, as before. Rules
  for single elements (a quote's padding, a table's rows) in a built-in
  theme's file would not be drawn until that is turned on.
- A face a theme names that is not installed is kept by name, as the
  table kept it, and Windows substitutes one.
