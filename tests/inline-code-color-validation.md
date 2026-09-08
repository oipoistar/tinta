# Issue #197 validation

Reported by @hochun836 in [Themes: separate inline-code text color from fenced code block text color](https://github.com/oipoistar/tinta/issues/197).

## Behavior

Reproduced on the pre-fix build at `fff531c`: inline spans and plain fenced code
both use `code=334455`, even with `inlinecode=B00020` in the custom theme.
The fixed build applies the optional override only to inline code. Missing or
invalid overrides inherit `code`; fenced base text, plain/operator tokens and
the existing syntax palette remain independent. Linked code retains link color.

The theme chooser and editor specimens display the override. Saving a theme
preserves it, while legacy themes keep an absent key. The advanced setting is
edited in `themes.ini`, alongside the existing `code` setting.

## Automated checks

MSVC Release build and all 12 CTest suites passed. The new `inline_code_theme`
suite runs a staged executable with isolated portable settings, so it does not
touch the user's configuration. It exercises the actual theme loader/writer,
Markdown parser, DirectWrite layout and HTML/DOCX exporters.

- All built-in themes and legacy custom themes retain their fallback.
- Explicit colors, optional `#`, lowercase hex, reversed key order and invalid
  values are handled correctly. Inheritance follows subsequent `code` changes.
- Save/reload preserves the override without adding it to legacy themes.
- The mixed fixture renders eight inline contexts: headings, paragraphs, bold,
  italic, table cells, quotes and lists. Linked code keeps its link color.
- Plain fenced text and syntax plain/operator tokens keep the base code color.
- Legacy, separate light and separate dark themes pass at widths 1050 and 650.
- Changing only the inline color preserves every text position, run count and
  total document height, including surrounding tables and LaTeX equations.
- HTML separates inline, fenced and linked code colors; DOCX preserves explicit
  inline colors in mixed content without recoloring fenced blocks or links.

Run `cmake --build build --config Release`, then
`ctest --test-dir build -C Release --output-on-failure`.

## Computer-use and export checks

Used isolated portable copies of `tests/fixtures/inline-code-color.md` and
`inline-code-themes.ini`. Inspected the baseline and fixed app at 1050 x 900 in
the separate light theme, and the fixed app at 650 x 760 in the separate dark
theme. Inline spans use red/pink, plain blocks stay neutral, and mixed headings,
tables, math, quotes and lists remain intact. Inspected the theme chooser and
editor specimens, saved the active custom theme through the UI, and verified
that `inlinecode=B00020` survived in the written file.

`tests/render_inline_code_fixtures.ps1` exported the mixed fixture in built-in,
legacy, separate light and separate dark themes, plus the existing
`markdown-regression-control.md` in the built-in theme. Each document produced
two native PNG print pages, HTML and DOCX, for both baseline and fixed builds.

- All 10 native print PNGs are byte-identical to the baseline. Native printing
  intentionally retains its existing light print palette.
- All three built-in/legacy HTML files are byte-identical to the baseline.
- Every ZIP entry in the three built-in/legacy DOCX files is unchanged (11, 7
  and 11 entries).
- Separate light/dark HTML differs only in inline color and the corresponding
  fenced/link CSS rules. Each DOCX differs only in ten inline run colors inside
  `word/document.xml`; all other entries and run content are unchanged.

For comparison, run the script with `-Binary` and `-Output` for the pre-fix
executable and again with the fixed executable and a different output directory.
Generated artifacts and the comparison script from this check are ignored under
`out/issue-197/`.

DOCX was verified structurally, not opened in Word. No physical printer or
separate Windows 10 machine was used. No release or issue reply was published.
