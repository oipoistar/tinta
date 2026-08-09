# Changelog

## [v2.6.0] - 2026-08-09

The vacation release: print and PDF export plus a round of community-requested polish, shipped before the maintainer disappears for a few weeks.

### Added
- Print and PDF export: Ctrl+P (or right-click → Print / PDF) opens a print preview — flip through the paginated pages, pick A4/Letter/Legal/A3 and portrait/landscape, then print to any installed printer; choosing "Microsoft Print to PDF" produces the PDF. Output is vector text with fonts embedded by the print pipeline, including CJK, always in a clean light palette on a white page. No PDF library, zero binary growth (#74)
- Mermaid diagrams and tables wider than the printable area are scaled down to fit inside the margins instead of being cut off at the edge
- Creating a new document no longer replaces the one you are reading: it opens in its own window, slightly offset, straight into edit mode; N is the new-file shortcut (#74)
- Reading position memory: documents reopen where you left off (last 50 files, kept in settings.ini), and entering edit mode resumes at the reading position instead of the top (#77)
- Editor horizontal scroll: with word wrap off, the view follows the caret into long lines and Shift+wheel pans sideways — long lines used to vanish under the pane separator with no way to reach them (#77)
- Remappable shortcuts: settings.ini gains a self-documenting `[Keys]` section covering every single-key action — change a value and restart; the help overlay and context menu relabel themselves to your bindings (#77)

### Changed
- Print takes Ctrl+P everywhere (the universal Windows convention); the edit-mode preview pane toggle moved to Ctrl+E, the same key Obsidian uses
- The settings file at `%APPDATA%\Tinta\settings.ini` is now documented in the README

### Fixed
- Editor/preview scroll sync no longer gets lost: scrolling the preview pane was overwritten every frame by the one-way sync, and sync was dead until the first edit after entering edit mode — the wheel now drives both panes from either side (#77)
- Entering edit mode with `:` is instant on large documents: the transition no longer blocks on a full synchronous layout; the preview streams in viewport-first like the viewer (#77)
- The help overlay showed a truncated shortcut list: hardcoded per-section row counts had silently dropped Ctrl+W and ESC ESC from the EDITING section

## [v2.5.1] - 2026-08-09

The notes release — honestly a 2.6's worth of features, but they landed right after v2.5.0 and needed real-world testing before earning a version of their own.

### Added
- Right-click context menu: Copy / Select All, Edit, Search, Table of Contents, Browse Files, Reveal in Explorer, Theme, and Help — theme-drawn, with every entry showing its keyboard shortcut (#62)
- Follow Windows theme: an opt-in toggle in the theme chooser switches between a preferred light and preferred dark theme when Windows changes mode
- Zen mode: F11 for borderless fullscreen with a centered ~80-character reading column; Esc restores the window exactly
- Clickable task lists: `- [ ]` / `- [x]` render as real checkboxes, and clicking one updates the file on disk
- Obsidian wiki links: `[[Note]]` and `[[Note|alias]]` navigate to sibling notes, resolved case-insensitively
- Frontmatter properties: the YAML block is hidden (it used to render as a giant heading, #61) and its title/tags show as a subtle chip strip instead
- Folder-wide search: while searching, sibling markdown files are scanned in the background and matches appear in a side panel with highlighted snippets — click one to open that file at the match; a folder button on the search bar disables the feature
- The title bar and window border follow the theme instead of the stock white chrome (Windows 11; dark caption fallback on Windows 10)

### Fixed
- YAML frontmatter no longer renders as a setext heading (#61)
- Select All now works when text is already selected (also fixed for Ctrl+A)
- Finishing a text selection on top of a code block no longer loses the selection
- Menu actions opening the TOC or theme chooser are no longer closed by their own click

## [v2.5.0] - 2026-08-06

The performance release: Tinta now opens in about 130 ms — 2.6× faster than v2.4.5.

### Performance
- Instant first paint: the first frame renders on a software target (~20 ms) while the GPU driver initializes in the background (~200 ms, previously paid before anything appeared); the render target is upgraded to hardware invisibly once the driver is warm. Measured startup: 348.8 ms → 133.4 ms
- Documents lay out ~28% faster: inline elements that fit on one line no longer shape their text twice, and plain table cells that fit their column skip the trial height measurement
- Several images finishing their downloads close together now trigger one reflow instead of one per image
- Cursor blink in edit mode no longer re-shapes every visible line — editor idle CPU dropped to zero
- Mermaid diagrams stopped recreating stroke styles and node outline geometries on every frame
- Window resizing keeps all rendered resources instead of rebuilding them per size change

### Fixed
- Images no longer turn into their alt-text placeholders after resizing the window (long-standing) or shortly after opening a document (introduced with the startup work, never released)

## [v2.4.5] - 2026-08-05

### Added
- Folder browser: click the path at the top to turn it into an edit box with everything selected — paste a path and press Enter to jump anywhere; a folder path browses there, a file path opens the file. Explorer's quoted "Copy as path" output and environment variables like `%USERPROFILE%` are handled (#52)
- Folder browser: new +folder and +file buttons create items in the browsed directory; a new file (`.md` added automatically when no extension is given) opens straight into edit mode for immediate note-taking (#52)

### Fixed
- Nested inline spans render completely: `` `code` `` inside **bold** or a link, *italic* inside bold, and strikethrough/highlight inside emphasis previously vanished from the page; each combination now renders with its merged style, including bold/italic monospace faces (#51)
- Chinese text renders with Microsoft YaHei UI instead of Japanese font variants: the font fallback is now split by Unicode range (kana → Japanese fonts, Hangul → Malgun Gothic, shared ideographs → Chinese-first), fixing wrong glyph shapes and visually heavier strokes (#48)

### Changed
- Inline code spans no longer add a stray space after the pill — a comma following `` `code` `` now sits next to it (#51)

## [v2.4.2] - 2026-07-30

### Fixed
- Remote images no longer freeze the app while they download: documents render immediately with a placeholder, images pop in when ready, and unreachable URLs settle as their alt text instead of blocking layout for the full connection timeout (#44)
- Inline `<br>` tags render as line breaks instead of literal text — `<br>`, `<br/>`, and `<br />` are all recognized, case-insensitively (#45)

## [v2.4.0] - 2026-07-29

### Added
- GitHub alerts: `> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`, and `[!CAUTION]` blockquotes render as styled callouts with GitHub's alert colors, adapted to light and dark themes (#41)
- Microsoft Store releases: Tinta is on the Store as [Tinta Markdown Viewer](https://apps.microsoft.com/detail/9MZ5MZ3L9RKF); MSIX packaging lives in `packaging/msix/` and CI can publish new versions automatically

### Fixed
- Folder browser: long file and folder names no longer wrap over the entries below — they trim with an ellipsis, and the path header keeps the end of the path visible (correctly measured for CJK names too) (#39)

### Changed
- The C runtime is statically linked: the exe runs on clean Windows installs without the VC++ Redistributable (573 KB → 821 KB, still a single dependency-free file under 1 MB)
- Packaged (Store) installs skip the file-association prompt — associations are declared in the app manifest instead

## [v2.3.0] - 2026-07-16

### Added
- Document-internal anchor links: `[text](#section-title)` scrolls to the matching heading, with GitHub-style slugs (including CJK headings — `[跳转到顶部](#一基础文本)` works). Contributed by @learn-more (#32), CJK slugs and edge-case polish in #33

## [v2.2.0] - 2026-07-15

### Added
- Obsidian/Typora inline extensions: `==highlight==`, `^superscript^`, `~subscript~` (#24)
- `~~strikethrough~~` now actually renders with a line through the text — previously the markers were consumed but no styling was applied

### Fixed
- Chinese/Japanese/Korean text now wraps correctly everywhere — line breaking uses the Unicode line breaking algorithm (via DirectWrite) instead of only breaking at spaces, so CJK paragraphs fill the line and break between ideographs with proper punctuation rules, long URLs break at slashes, and table cells wrap their content within the column instead of overflowing into neighbors (#24)
- A single unbreakable run wider than the whole line now splits at glyph-cluster boundaries instead of being clipped at the window or cell edge (#24)
- Table columns distribute like a browser's auto layout: narrow columns keep their natural width and only wide columns shrink — a three-character CJK header no longer collapses into a vertical strip while one huge cell hogs the row (#24)
- Tables whose minimum column widths exceed the window width now participate in horizontal scrolling

## [v2.1.5] - 2026-07-14

### Fixed
- Window no longer restores onto a disconnected monitor — the saved position is validated against the monitors present at startup and clamped into the nearest one (#25)
- Deeply indented list continuations (common in AI-chat markdown exports) render as regular text instead of code blocks with literal `**` markers; fenced ``` blocks are unaffected (#24)

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
