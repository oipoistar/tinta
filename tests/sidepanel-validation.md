# Contents and panel resizing (#210)

Validated on Windows on 2026-09-10. Reported by [@125Q](https://github.com/125Q)
in [#210](https://github.com/oipoistar/tinta/issues/210).

Contents includes H1-H6, with compact indentation, filtered navigation and
distinct anchors for duplicate titles. Both panels resize from their inner
edges and persist independent widths in logical pixels. Window resizing clamps
the displayed widths while preserving the preferences. Escape and capture loss
cancel a drag; mouse release cannot activate content. Printing temporarily hides
the panels so their widths cannot change the page layout.

## Verification of the original visual treatment

- Release build and all 17 CTests passed. Native checks cover Paper/Midnight,
  100/150/200% scale, narrow/wide geometry, both TOC sides, either/both panels,
  capture/release/cancellation, persistence, malformed settings, filtering,
  skipped levels, duplicate anchors and a 120-heading incremental document.
- Native printing with both panels open versus closed produces identical pages.
- `tests/render_sidepanel_fixtures.ps1` exported the two new mixed fixtures plus
  `markdown-regression-control.md` and `superscript-subscript.md`. All seven PNG
  pages and all four HTML/DOCX pairs match the preceding build byte for byte.
  Source hashes remain unchanged. Generated files are under `out/issue-210/`.
- Computer use checked Paper at 1180x900: browser/TOC drags, minimum widths,
  deeper heading clicks and mixed content. The maintainer confirmed dragging
  works, then requested a simpler visual treatment of the stacked edge controls.
- Minimum-width footer wrapping was corrected and retested automatically;
  computer use stopped on the maintainer's Escape before the final visual pass.
  Physical multi-monitor DPI transitions have not been manually exercised.

The original visuals are checkpointed before the separate visual prototype.
