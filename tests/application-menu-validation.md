# Application menu validation (#194)

Validated on Windows on 2026-09-08 using a portable test build and copies of
`fixtures/application-menu.md` and `fixtures/application-menu-companion.md`.
Test settings, saved files, drafts, exports and native page images live under
the ignored `out/app-menu/` directory.

## Checked in the built app

- The caption icon opens the menu in reading mode and on the start page; the
  editor rail icon opens it with the preview shown or hidden.
- New, Open, Save, Save As, Theme, Settings, Help, Print / PDF, Export and Exit
  are reachable. Save is disabled in reading mode; document commands are
  disabled on the start page.
- F10 opens the menu, arrows and End change selection, and Enter invokes an
  action. Escape and an outside click dismiss it. Keyboard selection survives
  repeated Windows mouse messages without physical pointer movement.
- Open from the start page replaces the empty launcher. Opening the companion
  while an untitled note is dirty preserves the note and its source in a tab.
- Returning to a dirty file restores its unsaved heading in both source and
  preview. The fixture on disk stays unchanged.
- Save on an untitled note opens the native Save As dialog and writes the
  expected Markdown. Save As creates a second copy with the same file hash;
  cancelling the dialog keeps the original tab.
- Settings and Help cover both editor panes. Typing while Settings is open
  does not modify a clean editor. Settings also works with the preview hidden.
- Choosing Paper from Midnight applies the theme without leaving editing mode.
- Exit with a dirty editor opens the existing unsaved-changes prompt; cancelling
  keeps the draft and window open.
- Print preview includes unsaved headings and the table's math. Export through
  the menu includes changes made with the preview hidden, while leaving the
  source file untouched.
- The document right-click menu and editor insert menu remain available.
- The menu stays anchored below the icon with Contents pinned. Clicking outside
  the menu preserves the pinned panel. Dragging empty title-bar space still
  moves the window (observed movement: 50 pixels right and 40 pixels down).
- Reviewed light and dark themes at window sizes 1050 x 900 and 650 x 760.

## Automated and native export checks

Build all Release targets and run:

```powershell
cmake --build build --config Release --parallel 6
ctest --test-dir build -C Release --output-on-failure
```

All eight suites pass. `context_menu` checks command availability, action hit
testing, separator gaps, keyboard wrapping/skipping, physical versus synthetic
mouse movement, icon boundaries, modal guards and menu bounds at 100%, 150%
and 200% scaling. `math_layout` parses both fixtures, checks table column counts,
keeps code literal and lays out each equation.

The app's `--printpages` and `--exporthtml` commands rendered both fixtures and
`markdown-regression-control.md`. The new fixtures retain six equations in
total, their headings and tables. All five native PNG pages and three HTML
files are byte-identical to the same exports from the published v3.5.6 binary.
Both pages of the main fixture were visually inspected.

Physical printing, screen-reader navigation, and a live multi-monitor DPI
transition were not exercised. High-DPI menu geometry is covered by automated
checks; live interaction used the test desktop's current display scaling.
