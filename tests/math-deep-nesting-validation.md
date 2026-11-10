# Equations nested deep

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code; nobody reported it. A short equation with
accents or delimiters inside one another stopped the window. Thirty
accents in a row, `$\bar\bar...\bar x$`, 124 letters, was enough, and so
were 25 pairs of `\left( ... \right)`. Opening a file that holds one, or
typing one with the preview on, was enough to hang Tinta.

## Cause

In `math_render.cpp` an accent, a pair of delimiters and others measure
what they hold and then lay it out. The laying out measured what that
holds again, on a measuring pass as on a drawing one, so every level
doubled the work: thirty levels are two thousand million text layouts.
The parser's limits (96 levels, 4,096 atoms) do not bound that.

## Change

What a piece measures depends on the piece, the size, the style and the
height its `\middle` takes, and on nothing else: the layout's context is
passed by value, and a measuring pass records nothing. `layoutNodeImpl`
keeps each measure by those four for as long as one box is laid out, and
a piece is measured once for them. The drawing pass is as it was.

## Fixture

`fixtures/math-deep-nesting.md`: thirty accents inline in a paragraph;
25 pairs of delimiters and powers 20 deep in a table; delimiters with a
bar in the middle 12 deep, a continued fraction of 16 levels and 20
nested roots as display equations under list items; two chains of
accents in a quote beside bold text; and one in a code block, where it
stays text.

## Automated checks

**`math_layout`** (`math_layout_tests.cpp`), in `deepNesting`:

- the fixture parses to its eight equations, the one in code left as
  text; each is laid out, and none makes more than eight text layouts a
  letter. The deepest makes 98;
- nine equations with accents, delimiters, `\middle`, fractions, roots,
  scripts and sums, inline and as display: laid out with the measures
  kept and with each made anew, every letter and stroke is in the same
  place and the box has the same size and baseline, to the bit.

With the measures not kept, the test does not finish: it was stopped
after 60 seconds, where it takes 0.17 s.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
64 of 64.

## In the built app

An isolated portable copy opening the fixture, and a message sent to its
window after 8 seconds:

| Build | The window |
|:------|:-----------|
| v4.0.2 | does not answer |
| This one | answers |

The window's pixels could not be looked at in this session, as copies of
the screen came back black. What the equations look like is the drawing
pass, which did not change; the test compares it.

## Size

`tinta.exe` grows by 2,048 bytes with this change and the next, to
4,030,464.

## Limitations

- The measures are kept for one box and dropped with it. An equation is
  still laid out once for each size it is shown at.
- The HTML and Word exports call the same layout and gain the same; they
  were not run on the fixture.
