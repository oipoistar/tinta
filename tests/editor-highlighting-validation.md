# Markdown in colour in the source editor (#268)

Validated on Windows with the Release build on 2026-10-31, on the local
`sheets` branch.

## Request

Syntax highlighting while editing Markdown: large files are hard to find
your way around, and to keep free of errors, when the source is plain text.

## Change

- The source editor colours each line when it lays it out to draw it
  (`src/source_style.cpp`), from the theme:
  - Markdown's marks go faint (the text colour at 45 %): a heading's `#`,
    emphasis' asterisks, code's backticks, a link's brackets with its
    address, a table's pipes and delimiter row, fences' backticks, maths'
    dollars, dividers, setext underlines, a backslash's escape;
  - headings, ATX and setext (over several lines too), take the theme's
    heading colour for their level and are bold; bold words are bold,
    italic ones italic, struck ones struck through;
  - the marks that start a block take the accent: bullets, numbers, task
    boxes, a quote's `>` and a callout's `[!NOTE]`; a finished task's words
    go faint, as on the page;
  - links, `[[notes]]`, footnotes, pictures' words (italic), addresses and
    an address in `<>` take the link colour; a link's or a footnote's
    definition its label in the link colour;
  - inline code, inline maths and maths blocks take the inline code colour;
    HTML tags go faint, HTML comments (on a line and over lines) take the
    comment colour;
  - fenced code takes its language's colours through the same lexer as the
    preview, with the lexer's state carried down the fence; a fence's
    language takes the accent; a fence Tinta has no language for, the code
    colour; the properties at the top read as YAML;
  - a table's header row is bold.
- What a line is (code, the properties, maths, a comment, a setext
  heading, a table's header) is worked out from the top, once per change
  to the text, and only down to the lines drawn. A fence opened anywhere
  above keeps the lines below it code.
- Inline Markdown is read by one scanner, `live::inlineSpans`, lifted out
  of Live's drawing: Live hides and styles what it finds, the source
  colours it. They agree on what is code, a link or bold.
- Bold and italic are used only when the editor's font keeps its widths
  with them, as monospace fonts do. Hit tests and word wrap lay lines out
  plain, so the caret, clicks and wrapping measure exactly what is drawn.
- Settings, Editor: **Markdown in colour in the raw editor** (on by
  default) turns it off, and is saved. A new theme, font, size or render
  target colours every line again.
- Two strings in all eight interface languages.

## Automated checks

`source_highlight` (new, isolated portable run,
`tests/source_highlight_tests.cpp`, on a copy of
`tests/fixtures/editor-highlighting-268.md`) reads each check's colour,
weight, style or strikethrough back from the layout the editor drew the
line with:

- the properties' fences faint, a key in YAML's key colour;
- a heading's `#` faint, its words in the first level's colour and bold;
  a heading in a quote in the second level's colour;
- bold, italic, struck, inline code, a link's words and its faint address,
  a note, a picture's words (italic), a footnote, an escape's backslash
  with the stars after it plain, an address, plain words in the text
  colour;
- a setext heading on one line and one over two lines in the heading
  colour, their underlines faint;
- bullets, a nested bullet, a number, a task box in the accent, an item's
  words in the text colour, a finished task's words faint, a quote's `>`
  and a callout's kind in the accent;
- a table's pipes and delimiter row faint, its header bold and its rows
  not, code in a cell, an escaped pipe not taken for a cell's edge;
- a fence's marks faint and its language in the accent; C++ in C++'s
  colours, a comment, a number, and a comment over two lines still a
  comment on its second (alone, that line would not read as one); `#`
  and `-` lines inside a fence are code; a language Tinta does not know
  takes the code colour; a fence inside a longer fence stays its code;
- inline maths and a maths block, an HTML tag, an address in `<>`, an
  inline comment and every line of a comment over lines, a divider, a
  footnote's and a link's definitions, the last line;
- a coloured heading is as wide as the same line laid out plain;
- all of it again in Midnight, the colours taken from the new theme;
- with word wrap, lines keep their colours;
- the Editor tab's switch turns highlighting off (a heading and a bullet
  back in the text colour) and saves the setting; on again, saved;
- a long document (290 KB, 14,801 lines, a fence every tenth section):
  frames at its top and foot and a key typed at its foot, timed with
  highlighting off and on; the last heading at its foot is coloured.
- Frames (`build/source-highlight-test-Release/frames/`, ignored) were
  looked at: the top, middle and foot of the fixture in Paper and Midnight,
  and both themes at 1050 and 650 px with word wrap at the narrow width.

`live_editor` and `sheet_canvas` pass with Live's drawing reading lines
through the shared scanner, and its frames (an open paragraph with every
inline span) are as before.

## In the built app

The build, run from an isolated portable copy (`out/highlight-playground/`,
Midnight, Source with the preview, `openInTabs=0`), was driven through its
message loop with posted messages and captured with PrintWindow
(`out/highlight-playground/shots/`):

- `tinta.exe editor-highlighting-268.md`, then E: the source editor opened
  in colour beside the preview, every case as in the test's frames;
- a click on a setext underline, Home, and ```` ``` ```` with Enter: every
  line below the new fence turned the code colour down to the next bare
  fence, the two-line setext heading above it went back to plain words,
  and the preview beside it agreed;
- closing the window and N discarded the changes; the file was as it was.

## Performance and size

- 290 KB, 14,801 lines: a frame at the top 4.3 ms off and 4.0 ms on, at the
  foot 2.2 and 1.7 ms, a key typed at the foot with its frame 3.4 and
  4.2 ms. The first frame at the foot after a change walks every line above
  it; the frames after it reuse the walk.
- `tinta.exe` grows by 22 KB (3,889,152 to 3,911,168 bytes).

## Limitations

- The lexers' own limits stay: Python's lexer, for one, does not carry a
  triple-quoted string over lines, in the editor as in the preview.
- `==marked==` words keep the text colour; only their marks go faint.
- Indented code blocks (four spaces) are not told from list items' lines,
  so they read as Markdown.
- An HTML comment that starts after text on its line colours to its end
  only on that line.
