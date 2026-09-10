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
## Footnotes

Validated on Windows, 2026-09-10. The mixed `footnotes.md` fixture combines named,
numbered, repeated, unresolved and escaped references with frontmatter, headings,
tables, quotes, lists, emphasis, highlights, code and math. Notes themselves
contain paragraphs, a list, a table and a SQL block.

- All 19 CTests passed after the parser/navigation/export additions. The footnote
  target also passes after replacing the return icon with small text links.
- Native layout covers Paper/Midnight, 1050/650 widths, 100/150% scale, all
  reference and return targets, selectable note text, and equal full/incremental
  layouts. Literal examples, first-use numbering, duplicate definitions,
  two-space continuations and CRLF are checked.
- Native print pages were visually inspected. Superscript references no longer
  carry a stray baseline underline; the emoji-like return glyph is replaced by
  `Back to reference` / `Back to references: 1, 2, ...`.
- HTML IDs/links and actual DOCX ZIP parts are checked. Every XML part parses;
  Word has two native footnotes, five repeated-reference NOTEREF fields, note
  tables and hyperlinks, and no illegal nested footnote references. Word itself
  has not been used for visual validation.
- Computer Use was interrupted with Escape before interactive footnote testing;
  native navigation and renderer checks above were used instead.

Nested footnotes and inline `^[note]` syntax are outside this implementation.
