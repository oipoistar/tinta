# Cross-document Markdown fragments (discussion #225)

Report: https://github.com/oipoistar/tinta/discussions/225 by @vkstudio2015.

Open [the source document](fixtures/file-fragments/index.md) and click
**Open the destination heading**. Tinta should open `target notes.md` in a tab
and place **Destination heading** just below the tab bar. The source deliberately
contains the same heading ID at a different vertical position.

## Cases to try

- Follow the Chinese and duplicate-heading links. They land at `中文标题` and the
  second **Repeated** heading respectively.
- Click **Filename with a hash** and **Literal percent**. Their filenames remain
  `hash#notes.md` and `literal%23.md`.
- Follow **Top of destination** to scroll to the top. Repeated links reuse the tab.
- A missing heading leaves the target file open at its normal reading position.
  A missing file offers to create only the filename, without `#fragment`.
- Hover a local fragment link to see the file preview. Back/Forward traverse files.
- Edit the destination, add `## Unsaved heading`, switch back to the source and
  follow `target%20notes.md#unsaved-heading`. The existing unsaved buffer is used;
  its source caret reaches that heading with preview shown or hidden.
- Inspect the table link, headings, quote, emphasis, lists, code, and math in both
  source and destination.

## Automated checks

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
python tests/validate_file_fragments.py
```

`file_fragment_navigation` exercises the real mouse handlers and native layout,
including a click using a rendered link rectangle. It covers fresh/stale layout,
saved reading position precedence, tab reuse, Back/Forward, encoded paths and
fragments, duplicate IDs, empty/missing fragments, missing files, hover previews,
unsaved editor tabs, and unchanged external URLs. Navigation runs at 650 and
1050 pixels in Paper and Midnight. The Markdown sources remain unchanged.

## Validation recorded on Windows, 2026-09-14

- Full Release build completed without compiler warnings; all 24 CTests passed.
- Computer Use: visually inspected the source's mixed content in Paper at 1050
  pixels; clicked the first link and confirmed the target opened in a second tab
  with the requested heading immediately below the tab bar.
- Export harness: both source and target exported with Paper and Midnight settings
  to HTML, DOCX, PDF, and 12 total PNG pages. Original fragment URLs survived HTML
  and DOCX export; headings, tables, quotes, code, lists and emphasis remained.
  Native print output uses its existing paper palette. Visually inspected the
  source print page, including its table, quote, code and math.
- Narrow widths and the unsaved-editor cases were checked in the native automated
  suite, rather than a second interactive window.

Generated outputs and logs are under ignored `out/discussion-225/`.
This change adds no dependency. Wiki-link syntax and non-Markdown fragment
handling retain their existing behavior.
