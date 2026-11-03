# A closing $ before a letter (#314)

Validated on Windows with the Release build on 2026-11-03.

## What was reported

`$R=50\,\mu$m，$\gamma$` rendered `R = 50 μ$m，` as one equation and
`\gamma$` as text: the `$` after `\mu` did not close, so the equation ran on
to the next `$` and took the second equation's opening one.

## Cause

md4c, the Markdown parser Tinta builds on, takes a `$` as closing only when a
space or punctuation follows it. Pandoc, Obsidian and Typora close it before
a letter too; only a digit after it keeps it open, so prices stay text.

## Change

CMake fetches md4c as before but builds it itself, from a copy of `md4c.c`
in the build directory with that one condition changed: a closing `$` may be
followed by a letter when no space stands before it. A digit after it still
leaves it open (`$5 and $6`), and so does a space before it (`Hello $name,
your total is $amount`), which pandoc reads the same way. The copy is written
again whenever md4c changes, and a md4c without the condition stops the
configure rather than building unchanged. md4c compiles at the `/W3` it
builds itself at; its own CMakeLists, the HTML renderer and `md2html` are no
longer built.

## Automated checks

`math_layout` (`closingDollars`): the reported line gives `R=50\,\mu` and
`\gamma`; `$x$y` and `$b$c` close; `$x$,`, `$y$ ` and `($z$)` still do;
prices, `$name ... $amount`, `$x$5`, code and an escaped `$` stay text. The
fixtures' equation counts are unchanged: 25, 9, 0, 5 and 1. Built with
md4c's own condition, the first two checks fail.

## In the built app

`repro-314.md` in Paper at 1280 px: `R = 50 μ` then `m，`, then `γ`; `$x$y`
an x then y; `$5 and $6` text.

## Size

`tinta.exe` is unchanged in size (3,981,312 bytes). The full suite without
the three tests that use the clipboard passes: 58 of 58.

## Limitations

- md4c's opening rule is unchanged: a `$` right after a letter or digit
  (`5$x$`) does not open, where pandoc's does.
- The equation box keeps its padding: `$50\,\mu$m` reads `50 μ m` with a
  little more room before the `m` than TeX gives.
