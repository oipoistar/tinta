# Issue #208 validation

Requested by [@Ra0EL](https://github.com/Ra0EL) in
[#208](https://github.com/oipoistar/tinta/issues/208).

## Heading and highlight colours

Validated on Windows, 2026-09-10. Optional H1-H6, highlight background and highlight
text colours load/save through themes.ini, with inheritance retained when absent
or cleared. Document heading separators follow the heading colour. HTML and DOCX
exports honour overrides; native printing retains its existing light palette.

- `theme-colours.md` mixes all six headings and highlights with tables, quotes,
  lists, links, emphasis, inline code, SQL and math. Three matching INI themes
  exercise light/dark overrides, missing keys and invalid input.
- Release build and all 18 CTests pass. Native theme tests cover loading order, fallback, save/reload and clearing,
  DirectWrite text/background colours, unchanged mixed-document geometry at
  1050/650 widths and 100/150% scale, plus actual HTML/DOCX output.
- All 40 exported PNG/HTML/DOCX artifacts for the existing syntax/Markdown
  fixtures match the preceding `d1e4df1` build byte for byte. The comparison used
  `tests/render_syntax_fixtures.ps1`; output is ignored under `out/issue-208/`.
- Computer use inspected the mixed document in the custom light theme, opened
  Appearance > Edit, expanded Heading colours and scrolled through the overrides
  and highlight controls. The live preview displays the custom H1 and highlight
  colours. A resize gesture did not resize the window, so narrow editor UI is
  not claimed as manually verified; narrow document layout is checked natively.

No issue comments are posted automatically. Draft replies are supplied for review.
