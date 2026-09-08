# Blockquote spacing validation

The maintainer noticed that a quote looked as though it had an extra trailing
line. Reproduced on `a1603a4` using `blockquote-one-line.md` and the mixed
`blockquote-spacing.md` fixture. Paragraph layout adds a 14px bottom margin;
the quote bar previously extended through that margin.

The bar now ends at the retained content bounds. The layout cursor, text,
internal paragraph gaps and spacing before the next block remain unchanged.
Content bounds also preserve nested bars, code backgrounds and padding, table
borders, heading rules, equations and callout titles/bodies.

## Automated checks

MSVC Release build and all 13 CTest suites passed. The new `blockquote_layout`
suite uses the real Markdown parser and DirectWrite layout. Its bar-height
assertions fail on the original renderer and pass with the fix.

- Single-line paragraphs with LF, CRLF and EOF without a terminal newline.
- Multiple paragraphs, explicit hard breaks and wrapping text.
- Inline code, emphasis, links and math inside quotes.
- Following paragraphs retain the 14px scaled gap; internal paragraphs retain
  their spacing and hard breaks retain one line advance.
- Nested quote bars end together at the final line.
- Fenced code keeps padding, empty-block height and intentional blank rows.
- Quotes ending in a table, display equation, list or heading rule.
- Callouts with a body, title-only callouts and empty quotes.
- Light and dark themes, widths 1050/650, and content scaling 100%/150%.
- The mixed fixture keeps nine ordinary/nested bars, its callout, both tables,
  both fences and the final content.

Run `cmake --build build --config Release`, then
`ctest --test-dir build -C Release --output-on-failure`.

## Visual and export checks

Inspected the fixed sample through computer use in Paper at 1050 x 900 and
Midnight at 650 x 760. The one-line quote has a balanced bar, and nested quotes,
tables, code, lists, callouts and math remain intact. Also inspected both pages
of the mixed fixture's native print output.

`tests/render_blockquote_fixtures.ps1` exports the minimal quote, mixed quote
fixture, existing inline-code theme fixture and existing Markdown regression
control. Run it with `-Binary` and `-Output` for separate baseline/fixed copies.

- The four documents produce 1, 2, 2 and 2 native print pages respectively.
- All four HTML files are byte-identical to the baseline.
- Every DOCX ZIP entry is unchanged (7, 11, 11 and 7 entries).
- Across all seven PNGs, changed pixels are confined to the ordinary/nested
  quote-bar columns. All other pixels, including surrounding text and tables,
  are unchanged. One page is entirely identical.
- The minimal quote's solid bar measures 41px before and 27px after at 100%:
  the 14px paragraph gap is now outside the bar.

Generated artifacts and the comparison script are ignored under
`out/blockquote-spacing/`. DOCX was checked structurally, not opened in Word.
No physical printer or separate Windows 10 machine was used. No release or
issue reply was published.
