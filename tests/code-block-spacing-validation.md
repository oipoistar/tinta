# Issue #196 validation

Reported by @hochun836 in [Rendering: fenced code blocks reserve an extra trailing line](https://github.com/oipoistar/tinta/issues/196).

## Reproduction and fix

Reproduced with the pre-fix build at `9932c9e`, using portable settings and
`tests/fixtures/code-block-spacing.md`. A one-line fence had two rows' worth of
background height. The native A4 PNG from `code-block-one-line.md` measured 64 px
before the fix and 44 px after it: one 20 px line plus 12 px top/bottom padding.

The reporter's diagnosis was correct: both the line count and the layout loop
treated a terminal newline as another empty row. The renderer now stops after
the actual rows. It leaves the source, copy-button text, searchable text,
intentional blank lines, empty-block minimum height and padding unchanged.

## Automated regression checks

MSVC Release build and all 11 CTest suites passed. The new `code_block_layout`
suite uses the real parser, DirectWrite formats and `layoutDocument` renderer.
Its height assertions failed on the pre-fix renderer and pass with the fix.

- Single/multiple lines with and without a terminal newline, LF and CRLF.
- Internal blank lines, one/two intentional trailing blank lines and blank-only blocks.
- Empty fences, closing fences at EOF, and unclosed fences at EOF.
- Plain-text/JSON documents, highlighted C++ fences and invalid Mermaid fallback.
- Original copy/search text retains its newlines; no text run occupies a phantom row.
- Both themes 0 and 5, widths 1050 and 650, and content scaling 100% and 150%.
- Nine blocks in the mixed fixture, including nested quote/list blocks, literal
  TeX and a long line that still enables horizontal scrolling.
- Mixed headings, tables, inline/display math, lists, emphasis and links survive.

Run `cmake --build build --config Release`, then
`ctest --test-dir build -C Release --output-on-failure`.

## Visual and export checks

Used computer use with isolated copies of the mixed Markdown fixture. Inspected
the pre-fix and fixed application at 1050 x 900 with the light theme, and the fix
at 650 x 760 in Midnight. Single-line and highlighted blocks have even padding;
intentional blank lines, empty blocks and surrounding table/math remain visible.

`tests/render_code_block_fixtures.ps1` exported the minimal fixture, mixed fixture
and existing `markdown-regression-control.md` through native PNG print pages,
HTML and DOCX. They produced one, three and two native pages respectively.

- Inspected both minimal PNGs and all three fixed mixed-fixture pages.
- The mixed fixture retains all nine code blocks and five equations.
- All three HTML files match the pre-fix output byte for byte.
- Every ZIP entry in all three DOCX files matches the pre-fix output (7, 11 and 7 entries).
- The existing control's first native page is byte-identical. Its second page
  changes because its code block becomes one row shorter.

For an export comparison, run the script with `-Binary` and `-Output` for the
pre-fix executable, then again with the fixed executable and `-BaselineOutput`
pointing to the earlier output directory. Generated artifacts from this check
are ignored under `out/issue-196/`.

No physical printer or Windows 10 machine was used. Native print-page PNGs cover
the shared print layout. No release was published and no issue reply was posted.
