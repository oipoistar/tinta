# Issue #223: Find/Replace and table input focus

Validated on Windows, 2026-09-13, against v3.7.1 / master 97f1c37.
Issue: https://github.com/oipoistar/tinta/issues/223, reported by @a86022.

## Changes

- Find and Replace have independent caret, selection and horizontal-scroll
  state. Left/Right, Home/End, Backspace/Delete, Ctrl word navigation/deletion,
  Shift selection, Ctrl+A/C/X/V, mouse placement and dragging work in both
  fields. DirectWrite clusters preserve surrogate pairs and combining sequences.
  Reading-mode Find uses the same input handling.
- Source, table cells, Find and Replace coordinate typing focus. Pending cell
  edits commit before source hit testing, so changed cell lengths cannot move
  the clicked insertion point. Reacquiring search refreshes matches after edits.
  Only the active surface draws its caret, including an empty Find field.
- Ctrl+F/H focus an already visible bar and cannot leave a table owning input.
  Opening keystrokes are consumed directly. Escape, Enter, Tab/Shift+Tab, saving
  and source Undo remain usable. Search teardown also releases selection capture.
- Chinese composition/candidate positioning uses the active search/table caret.
  Narrow replacement fields put buttons below the text, the bar clears the
  editor rail, and long queries leave room for the localized match count.
  Clipboard operations retry brief locks; Cut removes text only after copying.

Regex is deferred. Matching and replacement text remain literal.

## Automated checks

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
python tests/validate_find_focus.py
```

All-target Release build: zero compiler warnings/errors. **22/22 CTests passed
(8.09 s)**, including `search_input_focus`, using production input handlers and
native rendering with isolated portable settings.

`tests/fixtures/find-table-focus.md` mixes a table, headings, lists, emphasis,
quote, inline/fenced code, math, Chinese, emoji and combining accents. Native
checks cover field navigation/selection/clipboard/Unicode, empty/long queries,
mouse placement/dragging, the reported focus sequences with and without Tab,
cell-to-search and search-to-source transfers, table Tab/Shift+Tab/Escape,
source Undo after committing cells, literal Replace, Ctrl+S saving the actual
buffer while search retains focus, viewer Find, and IME anchor geometry.
Rendering checks span 650/1050 widths, scale 1/1.5, Paper/Midnight,
wrapped/unwrapped source, and preview visible/hidden.

Final exports match published v3.7.1 in Paper and Midnight: **8 identical
artifacts** (four native PNG pages, two HTML files, two DOCX packages compared
by uncompressed members). Both PDF exports succeed and have valid headers.
The fixture is unchanged; surrounding Markdown is checked in exported HTML.

## Live Computer Use

- Paper at 1050 x 900: empty Find caret; `abc`, Left, X -> `abXc`.
- Midnight at 650 x 900: usable stacked replacement controls, toolbar clearance,
  Chinese/emoji search text, search-to-cell focus, Tab to the next cell, then
  clicking a source heading and typing `FIX223-`. Saving produced
  `## SurroFIX223-unding content`; `| alpha | beta |` remained unchanged.
  The preview updated to the edited heading.
- Final Paper build opened with a clean sample for maintainer review.

Live tests use copies/settings in `out/issue-223/ui-*`. The user's own settings
and documents are not used. Chinese characters and emoji were entered through
Computer Use; native tests verify IME anchor geometry. An actual Chinese IME
candidate popup was not exercised live.

Harness corrections: independent synthetic source clicks initially counted as
a double-click, so each case now resets its gesture. Clipboard tests needed
OLE initialization/restoration and exposed brief clipboard locks; the harness
initializes OLE, and production operations retry the locks. Final checks pass.

## Artifacts

- Build log: `out/issue-223/build-verified.log`
- Tests: `out/issue-223/ctest-verified.log`
- Exports: `out/issue-223/exports/results.json`
- Unposted bilingual reply: `out/issue-223/reply-draft.md`
- Review executable: `build-meta/tinta.exe` and `build-meta/Release/tinta.exe`

Both review copies match `build/Release/tinta.exe`: **2,551,808 bytes**, SHA256
`370908B337594237BE12E4CBF2219C2082179D6793D89E5143F84A0DDAC4CFEC`.
The published 3.7.1 archive remains available separately for comparison/rollback.
