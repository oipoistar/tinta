# Syntax highlighting extension (#207)

Implemented for [@hochun836's request](https://github.com/oipoistar/tinta/issues/207).
Validated on Windows on 2026-09-10, against the preceding build at `76e6916`.

## Coverage

- SQL, PowerShell, Java, PHP, HTML, XML, CSS, YAML and Markdown, including the
  requested `ps`, `pwsh`, `htm`, `yml` and `md` aliases and case-insensitive labels.
- Language-specific comments, strings, keyword/type/control-flow categories,
  calls, numbers, tags/attributes/entities, CSS properties/values, YAML keys and
  block values, and Markdown markers. Stateful constructs end at their real
  delimiters and never carry over to another outer fenced block.
- Existing language rules, unknown-language fallback and theme keys retained.
  The built-in feature list and `docs/syntax-highlighting.md` describe support.

## Automated and native checks

- Release build and all **18 CTests passed**. The `syntax_highlighting` test covers
  exact token categories, aliases, case handling, escaped/doubled quotes, nested
  comments, multiline delimiters, URLs/fragments, PHP attributes, YAML dedents,
  shorter inner Markdown fences and state reset between separate code blocks.
- 10,800 deterministic malformed/random UTF-16 fragments exercise source-slice
  continuity, nonempty tokens, termination and state across lines.
- Native DirectWrite layout checks use Paper, Midnight and a custom seven-colour
  palette, at 100/150% content scale and 1050/650-pixel document widths. They verify
  actual theme colours, original copy/search text, code row heights, non-overlap,
  surrounding Markdown/math, and matching full/incremental layout results.
- Runnable fixtures: `syntax-languages.md`, `syntax-multiline.md` and
  `syntax-legacy-control.md`. Every fixture mixes code with headings, tables,
  quotes, lists, emphasis, links and inline/display math.
- `tests/render_syntax_fixtures.ps1` exported those three fixtures and the existing
  `markdown-regression-control.md` from both builds with Paper/Midnight settings:
  **24 native PNG pages, 8 HTML files and 8 DOCX files per build**. Printing uses
  Tinta's fixed print palette; the dark/custom palettes are checked by native
  layout tests, not inferred from printed pages.
- All 16 HTML/DOCX exports and all 8 existing-language/Markdown control PNG pages
  are byte-identical. Four trailing new-fixture PNG pages are also identical;
  the only 12 changed artifacts are print pages containing newly coloured code.
  Fixture source hashes remain unchanged.
- Visually inspected pages 1-3 of both new-language and multiline fixtures:
  all nine languages, aliases, multiline terminators, independent fences and
  surrounding headings/tables/quotes/math render. No interactive mouse checks
  were needed for this tokenizer change.

Generated files and the preceding executable are ignored under `out/issue-207/`.
Reproduce with a Release build, `ctest --test-dir build -C Release
--output-on-failure`, and `tests/render_syntax_fixtures.ps1`.

## Limits

The extension uses lexical heuristics, with no added fonts or external runtime.
It does not fully parse every dialect, colour interpolations independently, or
recursively highlight embedded HTML script/style bodies and inner Markdown code
fences. HTML/DOCX syntax-colour export remains outside this change; those exports
keep their previous plain-code formatting. See `docs/syntax-highlighting.md`.

No issue reply has been posted. Provide a maintainer-reviewable draft with the PR.
