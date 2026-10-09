# Table cells in the selectable text (#246)

Validated on Windows with the Release build on 2026-10-09. Spotted by Alex
(@Lex987) in #246.

## Problem

The rendered document keeps its text in one string for selection, copy and
find. A table row's cells went into it back to back, with nothing between
them, so a row of Delta, Echo and Foxtrot read "DeltaEchoFoxtrot".
Double-clicking a word in a cell selected the neighbouring cells too, and a
copied row came out as one run-together word.

## Change

A tab now separates the cells of a row (the row itself still ends in a line
break). Tab is already a word boundary, so double-click stops at the cell's
edge, and a copied row pastes into a spreadsheet as separate columns. Drawing,
table editing (which works from cell rectangles) and the Copy TSV button are
unchanged.

## Fixture and automated checks

`fixtures/table-cells-246.md` has a control paragraph, a table with plain,
inline-code, bold, link and Chinese, Japanese and Korean cells, and a quote, a
list and a code block around it.

`wide_table_layout` now also lays the fixture out in Paper and Midnight at
1050 and 650 px and checks that each row's cells are tab-separated, that
hit-testing the middle of each cell word and taking the word there (the
double-click path) selects exactly that word, and that paragraph words still
select one at a time. Without the separator the check fails 44 times.

The full suite passed 33 of 33.
