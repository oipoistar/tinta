# Changelog

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
