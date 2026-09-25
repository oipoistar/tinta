# PlantUML diagram support - feature validation

Validation record for the PlantUML fence/`.puml` feature, following the repo
feature-validation workflow. The runnable fixtures are
`tests/fixtures/plantuml-diagrams.md` (mixed document, all fence states) and
`tests/fixtures/plantuml-standalone.puml` (single anchored sequence). The
automated export/render gates over them live in `tests/render_plantuml_fixtures.ps1`.
Nothing below is claimed unless the exact command ran and its artifacts are in
the evidence directories referenced per section.

## Scope

- `plantuml` fenced blocks inside Markdown: rendered SVG (HTML export), embedded
  PNG (DOCX export), themed preview/print raster (Direct2D), source-code fallback
  when the fence is unanchored, invalid, slow, or no tool is configured.
- Standalone `.puml` files opened directly and exported.
- Out of scope here: the interactive GUI flows (window chrome, live theme switch,
  narrow-width layout) - tracked as pending F3 below; the T14 harness reruns the
  headless gates in CI.

## Tool used for validation

PlantUML **1.2026.8** from the locally installed jar (the MIT-flavor
`plantuml-mit` build works the same way), invoked through the `java` on `PATH`
(Temurin JDK 21). No absolute machine paths are embedded in the fixtures; any
user can obtain the jar from the official PlantUML distribution site (or its
GitHub releases) and point Tinta at it in Settings.

## What was actually checked

### Print path (evidence `.omo/evidence/task-5-plantuml-diagram-support/`)

- `tinta.exe <fixture> --printpages <dir>` with the real jar, the fake tool, and
  the tool unset; page PNGs (`page1-real.png`, `page2-real-both-diagrams-after-fix.png`,
  `page3-real.png`) show rendered diagrams vs the source-fallback control page
  (`page2-control-source-fallback.png`).
- Print/PDF always renders diagrams through the light Paper print palette by
  design (`printTheme()` forces Paper); this is product behavior, not a defect.

### Themes (evidence `.omo/evidence/task-6-plantuml-diagram-support/`)

- Light (Paper) and dark (Midnight) theme fidelity through the live-theme
  DOCX/HTML export flows: per-theme pixel probes (`e2e-themes-discrimination.txt`,
  `probe-pngs/`) and preamble proof (`probe-export-light.html` / `probe-export-dark.html`
  show only the light / dark hex respectively).
- Cache keys are theme-scoped (`fake-light.log` / `fake-dark.log`): the same fence
  hashes to different keys per theme, and a missing-tool config prints source
  fallback with zero render spawns (`e2e-themes-discrimination.txt` gate 3).
- The live runtime theme switch (recolor without re-export) is **pending F3**.

### HTML export (evidence `.omo/evidence/task-7-plantuml-diagram-support/` and `task-13-plantuml-diagram-support/`)

- `tinta.exe tests/fixtures/plantuml-diagrams.md --exporthtml out.html` run for four
  scenarios (`gate-summary.md`): REAL jar 4 diagram divs / 2 fallbacks (6.1 s),
  FAKE tool 5 divs / 1 fallback (0.2 s), tool unset 1 div (mermaid) / 5 fallbacks
  (0.1 s), slow-tool budget 1 div / 5 fallbacks in 60.2 s (< 90 s cap).
- T13 re-ran the FAKE and REAL gates after hardening the fixture's header prose:
  the naive substring count for the fallback language marker now equals the
  precise `<code class="language-plantuml"` pattern count in both scenarios
  (`t13-gate-counts.txt`: FAKE 1==1, REAL 2==2; diagram divs 5 and 4).
- `plantuml-standalone.puml --exporthtml` with the fake tool yields exactly
  1 diagram div containing the fake sentinel (`t13-gate-counts.txt` gate 4).

### DOCX export (evidence `.omo/evidence/task-8-plantuml-diagram-support/`)

- `--exportdocx` with the real jar embeds genuine PNGs (233x235 and 153x267 px,
  `gate-report-real.txt`, `real-image*.png`); the fake and unset baselines prove
  media-count deltas and the fallback path (`gate-report-fake.txt`, `note.md` G1-G4).
- Dedupe by content hash (one spawn for two identical fences, G2) and the
  per-fence/cumulative budget caps (G5: 45.1 s with a 25 s sleeping tool).

### `.puml` open and export

- The standalone fixture exports headlessly as one diagram (gate 4 above; the T14
  harness repeats it with the real jar). Opening `.puml` through the GUI and its
  file-association flow is covered by the document-type and association tasks -
  interactive verification is **pending F3**.

### No-tool fallback

- With `plantumlPath` unset, every plantuml fence exports as a plain highlighted
  code block (T7 UNSET gate: 5 fallbacks, mermaid control untouched) and prints
  as source (T5 control run). The app never reaches the network for diagrams.

### Regression suite

- `ctest -C Release -R "document_types|code_block_layout|mermaid_parser"` green
  with the T13 fixture present (`t13-gate-counts.txt` gate 3); the task-level
  suites ran 3x green per the T5-T8 evidence logs.

## Layout inspection status

- Normal- and narrow-width visual inspection of the mixed fixture inside the
  running app (computer use) is **pending F3**, along with the live theme switch;
  until then the width behavior of PlantUML diagrams inherits the existing
  diagram fit-to-width path that the mermaid control exercises.

## Remaining limitations

- PlantUML rasterizes at 1x natural size into DOCX (mermaid embeds at 2x), so
  embedded PNGs can look soft on high-DPI displays or when enlarged in Word.
- Only the first `@startuml` block of a fence source is rendered; multi-block
  sources are by design not supported (the fixture keeps every fence single-block).
- Render caches live under the per-process adopted work directories in `%TEMP%`
  (`tinta-plantuml-*`) and are swept after 24 h rather than on exit; a killed
  export may leave a directory until then.
- Headless `--exportdocx` starts a cold cache (new per-process work root), so it
  exercises the synchronous render path; the cache-hit branch is only reachable
  for an already-open GUI document.
- Printed/PDF diagrams are Paper-toned even under dark themes (printTheme forces
  the light palette by design).

## Artifacts

Screenshots, page bitmaps, exports and gate logs listed above are under
`.omo/evidence/task-{5,6,7,8}-plantuml-diagram-support/` and
`.omo/evidence/task-13-plantuml-diagram-support/`; generated scratch exports live
only in ignored `out/` or `.omo/tmp/` directories and are not committed.
