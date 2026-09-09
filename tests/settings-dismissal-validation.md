# Settings dismissal validation

Validated on Windows on 2026-09-09 against v3.6.7.

## Behavior and native regression checks

Settings now closes on a left-click outside the panel or on the small top-right
close button. Blank space inside the panel keeps it open. Changes remain applied.
Dismissal clears dropdown and slider state, just like Escape, and consumes the
click so the editor, document links, app icon and tab controls cannot act beneath it.
Window minimize, maximize and close controls retain their native behavior.

Build all Release targets and run:

```powershell
cmake --build build --config Release --parallel 6
ctest --test-dir build -C Release --output-on-failure
```

All 14 suites pass. The existing `input_shortcuts` executable now also renders
the actual settings panel into a hidden native test window and exercises both
mouse handlers. It checks:

- Close-button hit registration and geometry during opening animation and at rest.
- All four backdrop edges, blank panel space, and clicks over the icon and + tab control.
- Preserving dirty editor text and the cursor; no selection or missing-file link activation.
- Paper and Midnight, widths 650 and 1050, 100%/150%/200% scale, reading and editing modes.
- Dropdown choices outside the panel still work; an inside blank click collapses only the dropdown.
- Releasing a slider outside ends the drag without closing Settings; Escape clears capture and keeps the value.
- The icon-menu click that opens Settings is consumed and does not immediately dismiss it.

## Mixed Markdown and visual checks

`fixtures/settings-dismissal.md` contains headings, a table, emphasis, a link,
inline and fenced code, a quote, a list, and four equations.

```powershell
./tests/render_settings_fixtures.ps1
./tests/render_settings_fixtures.ps1 -Binary ../tinta-3.6.7.exe -Output out/settings-dismiss/baseline
```

The new fixture, `copy-file-path.md` and `markdown-regression-control.md` were
exported by both builds. All five native PNG pages and all HTML bytes and DOCX
ZIP entries match v3.6.7. Generated files and the comparison script are under
the ignored `out/settings-dismiss/` directory.

Computer Use showed the mixed fixture and Settings in Paper at 1050 x 900,
including the close button in the top-right corner. An outside click dismissed
the panel without changing the document. The mixed fixture was also inspected
in Midnight at 650 x 760. Subsequent live input was inconsistent and included
user-input guard interruptions, so close-button activation, editing-mode
dismissal and scaled/dark dialog geometry are verified by the native tests,
not claimed as completed interactive checks.
