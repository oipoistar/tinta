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

## Visual prototype

The original checkpoint is `3448a168182a94a83c108c1b37e896445318d9d2` on
`toc-browser-resize-210`; its executable is saved in
`out/issue-210/visual-before/tinta.exe`. The prototype lives separately on
`toc-sidebar-visual-prototype-210`. Reverting its single visual commit restores
the original appearance while retaining H1-H6 support and resizing. The saved
executable also allows restoring the dev builds without waiting for a rebuild.

- Dock the panels without floating card borders, shadows or outer gaps. Each
  boundary has one hairline that highlights on hover; the existing resize hit
  area stays 10 logical pixels wide. This recovers 20 logical pixels within each
  panel for labels and controls at the same preferred width.
- Remove the duplicate document-position rail in Contents, retaining the short
  active-heading marker and a separate Contents scrollbar when its list overflows.
- With a side panel open, document scrollbars appear during scrolling, dragging,
  search or edge hover. After 800 ms of inactivity they fade over 300 ms. A timer
  stops after the fade. The editor and viewer without side panels retain their
  previous scrollbar appearance.
- Added `sidebar-visual-prototype.md`, with H1-H6, a table, quotes, lists, links,
  code, scripts and equations. It is included in the native export runner.
- Release build and **17/17 CTests passed**, including new interaction checks
  for scrollbar dragging beside the resize target, fade timing, divider hover
  and preserved ordinary-viewer behavior. Existing geometry, theme, scale and
  print checks also pass.
- All **19 export artifacts** (nine native PNG pages, five HTML and five DOCX
  files) match the original-visual checkpoint byte for byte across five fixtures.
- Computer-use screenshots inspected Paper at 1180x900 with browser left and
  Contents right, and Midnight at 700x900 with both panels left. The shared
  dividers, single-line footers, deep labels, active heading and surrounding
  mixed content fit the intended layout. Automated native input tests provide
  the deterministic resize/scrollbar interaction coverage.

Generated preview builds, settings and export comparisons are isolated under
`out/issue-210/`; physical monitor DPI transitions remain untested manually.

## Scrollbar refinement

The document scrollbar in the sidebar layout now stays **4 logical pixels**
wide on hover and while dragging. It keeps its existing 14-pixel hit area;
the vertical thumb sits fully clear of the divider's resize target. Opacity
is consistent across hover and ordinary scrolling, avoiding a second visual jump.

Visibility eases in over **120 ms**, holds for **800 ms** after scrolling,
then eases out over **300 ms**. Hover-only entry also animates. A new scroll
during fade-out resumes from the current opacity instead of snapping to full
visibility. The animation timer stops after settling, including when a panel
closes or the editor/print preview opens.

Release build and **17/17 CTests passed**. Native regression checks use controlled
timestamps to cover the fade-in, hold, fade-out, hover-only entry, interruption,
resize suppression, timer eligibility and reopening without stale opacity.
Scrollbar-versus-divider hit testing and the existing theme/scale/layout checks
also pass. `sidebar-visual-prototype.md` now contains a repeatable test sequence.

All **20 artifacts** from the five mixed Markdown fixtures (ten PNG pages, five
HTML and five DOCX files) match the pre-refinement executable byte for byte.
The added instructions make the prototype sample three print pages in both
builds. Source files remain unchanged by the export runner. Generated results
are in `out/issue-210/scrollbar-baseline` and `out/issue-210/scrollbar-exports`;
the preceding executable is in `out/issue-210/before-scrollbar-fade/tinta.exe`.
Animation timing was verified by the native tests; perceived animation feel
remains for the maintainer to review in the running app.
