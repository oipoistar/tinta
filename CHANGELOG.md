# Changelog

## [Unreleased]

### Changed
- The executable now embeds proper version metadata (company, product, description, version) — files without it disproportionately trip antivirus ML heuristics
- md4c dependency pinned by commit hash instead of tag

## [v2.1.0] - 2026-07-10

### Added
- Native Mermaid `flowchart`/`graph` rendering for `.mmd` files and fenced `mermaid` blocks
- `.mmd` support in the folder browser, drag-and-drop, live edit preview, file watching, and Windows file association registration
- Shift+mouse wheel scrolls horizontally (same as tilt wheels)
- Edit mode: Ctrl+P shows/hides the preview pane; the editor takes the full window while it's hidden (#17)
- Edit mode: Ctrl+W toggles soft word wrap in the editor (off by default); caret, clicks, selection, and search highlights follow wrapped rows (#17)
- Both editor view options persist across sessions

### Fixed
- Mermaid: edges that skip over intermediate ranks now route around the diagram through an exterior lane instead of cutting straight through nodes — their labels no longer land on unrelated edges
- Mermaid: edge labels render as bordered chips on the edge instead of erasing the line beneath them
- Mermaid: unsupported v11 `@{ }` attribute syntax now falls back to a readable code block instead of rendering raw attributes as a diamond
- Mermaid: edge label chips slide along their edge to avoid stacking on top of each other when several labeled edges share the same corridor
- Mermaid: a literal `\n` in node and edge labels renders as a line break, matching mermaid.js
- Long code block lines now extend the block background and participate in horizontal scrolling instead of being clipped with no way to reach them

## [v2.0.1] - 2026-07-08

### Fixed
- Edit mode: caret, mouse clicks, and selection highlights were misaligned on lines containing CJK or other full-width characters — column math assumed a fixed character width; all caret/click/selection positioning now goes through DirectWrite hit testing (#12)
- Edit mode: clicking past the end of a line with full-width characters could not place the cursor at the line end (#12)
- Edit mode: double-click now selects a contiguous CJK run instead of nothing (#12)
- Edit mode: Backspace/Delete and Left/Right no longer split surrogate pairs (emoji and supplementary-plane characters)
- Edit mode: IME composition window (Chinese/Japanese/Korean input) now appears at the caret instead of the window corner
- Edit mode: reparse debounce raised to 300 ms to reduce stutter while typing with an IME

## [v2.0.0] - 2026-06-12

### Added
- Layout time metric in the stats overlay (press S)
- "Save failed" notification when the file can't be written (locked or read-only)

### Performance
- Viewport-first layout: documents present the first screenful immediately; the rest lays out in background slices that yield to input
- Merged text layouts: one DirectWrite layout per line segment instead of per word, and one per color run instead of per syntax token
- Editor reparse debounced (150 ms) instead of re-parsing the document on every keystroke
- Cursor blink and notification fades are timer-driven — idle CPU usage is now zero
- Theme chooser preview formats created lazily instead of at startup; switching between same-font themes no longer recreates text formats
- Ctrl+scroll zoom coalesces rapid wheel ticks instead of rebuilding formats per tick

### Fixed
- CJK full-width punctuation (（）：！etc.) rendered as missing-glyph boxes; other scripts now fall back through the system font chain (#9)
- Unsaved-changes exit prompt is now persistent and sized to its text instead of fading out after 3 seconds while still armed
- Modifier keys (e.g. the Ctrl of Ctrl+S) no longer silently dismiss the exit prompt; only Y / N / ESC respond
- Ctrl+S saves from the exit prompt and while the search bar is open in edit mode
- Failed saves no longer leave the document permanently dirty and edit mode impossible to exit

## [v1.9.0] - 2026-03-02

### Added
- Code block copy button: hover any code block to reveal a "Copy" button in the top-right corner
- In-app unsaved changes prompt (Y/N/ESC) replaces modal dialog when exiting edit mode

### Fixed
- Text selection in edit mode preview pane: coordinates now account for the preview pane offset
- Ctrl+C in edit mode now copies preview pane selection instead of being swallowed by the editor handler
- Double-click word select and triple-click line select work correctly in edit mode preview
- Link clicking in edit mode preview pane works at the correct position
- Double-ESC exit from edit mode with unsaved changes no longer gets blocked by ESC key-repeat auto-dismissing the save dialog

## [v1.8.0] - 2026-02-06

### Added
- Edit mode with live preview (press `:` to enter, double-ESC to exit)
  - Split view: editor on left, rendered preview on right
  - Draggable separator between panes
  - Syntax-aware monospace editor with line numbers
  - Undo/redo, clipboard, word/line selection
  - Scroll sync between editor and preview
  - Save with Ctrl+S, auto-reparse on edits
- Editor search (Ctrl+F in edit mode)
  - Search operates on raw editor text with highlights in the editor pane
  - Search bar centered over editor pane
  - Yellow highlights for all matches, orange for current match
  - Enter to cycle through matches, ESC to close
- Performance optimizations (cached cursors, regex patterns, vector pre-allocation)
- Extracted input/overlays/file_utils into separate modules

## [v1.7.0] - 2026-02-05

### Added
- Rich inline formatting in table cells (bold, italic, code, links)
  - Links render as clickable with underline and link color
  - Bold, italic, and inline code render with proper styling
  - Cell alignment (center, right) works with inline-formatted content

### Fixed
- Table cells no longer render links, bold, italic, and code as plain text
- Table cell content no longer overflows cell boundaries

## [v1.6.5] - 2026-02-05

### Added
- Table of contents side panel (press Tab to toggle)
  - Slides in from the right side
  - Shows H1, H2, H3 headings with indentation
  - Click a heading to jump to that section
  - Scrollable list with mouse wheel
  - Theme-aware colors (light/dark)
  - "No headings" message for files without headings

## [v1.6.0] - 2026-02-05

### Added
- Folder browser panel (press B to toggle)
  - Slides in from the left side
  - Navigate directories with single-click
  - Open .md/.markdown files directly
  - Scrollable file list with mouse wheel
  - Theme-aware colors (light/dark)

## [v1.5.1] - 2026-02-04

### Fixed
- Fix ruby/furigana rendering in standalone HTML blocks
- Fix `<rp>` parser to avoid dangling pointer on element stack

## [v1.5.0] - 2026-02-04

### Added
- Color emoji rendering via DirectWrite font fallback and D2D device context
- CJK font fallback (Yu Gothic UI, Meiryo, Microsoft YaHei UI, Malgun Gothic)
- Ruby annotation support (`<ruby>`, `<rt>`, `<rp>` HTML tags) with furigana rendering
- Multi-resolution icon (256x256, 48x48, 32x32, 16x16)

### Fixed
- Unicode file path support for drag & drop and command line (e.g. Japanese folder names)
- Search bar "No matches" text positioning using dynamic measurement

## [v1.4.0] - 2026-02-03

### Changed
- Refactored main_d2d.cpp into logical modules

### Improved
- Cached layout pipeline and rendering performance

## [v1.3.0] - 2026-02-03

### Added
- Syntax highlighting for code blocks
- Rendering quality improvements (ClearType, OpenType typography)

## [v1.2.0] - 2026-02-02

### Fixed
- Improved text selection and Vietnamese rendering

## [v1.1.0] - 2026-02-02

### Added
- Search feature (F/Ctrl+F) with real-time highlighting and match cycling
- Updated icon with improved quality
- Hero section with download badge in README

## [v1.0.1] - 2025-12-06

### Fixed
- Improved text selection and file association handling

## [v1.0.0] - 2025-12-05

### Added
- Initial release of Tinta markdown reader
- Direct2D hardware-accelerated rendering
- Icon and file association for .md/.markdown files
- GitHub Actions CI/CD with automatic releases
