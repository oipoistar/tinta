# Editor context-menu selection (#230)

Report: https://github.com/oipoistar/tinta/issues/230 by @a86022.

Opening the source editor's context menu previously cleared the selection before
the menu checked whether Copy and Cut should be enabled. The fix preserves the
selection, its direction, and caret wrap affinity when the pointer is inside the
painted selection. An outside right-click still moves the insertion point.

## Manual sample

Open [editor-context-selection.md](fixtures/editor-context-selection.md) and enter
edit mode. Double-click **selection** in the first paragraph, then right-click
inside the highlight. The highlight should remain, with Copy and Cut enabled.
Copy should preserve the document; Cut should remove only the selected text and
Undo should restore it.

Repeat with the Chinese paragraph and a selection spanning wrapped lines. Also
try a reversed drag, Shift+Arrow selection, the final selected character, and
the highlighted cell representing a selected newline. Right-clicking unselected
text or empty space outside the highlight should clear the selection and move
the caret. Confirm surrounding headings, tables, lists, quotes, code and math.

## Automated validation

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
python tests/validate_find_focus.py --fixture tests/fixtures/editor-context-selection.md --baseline out/issue-230/baseline.exe --output out/issue-230/exports
```

The export comparison accepts any pre-fix binary via `--baseline`; the recorded
baseline is commit c0054c5. Generated binaries, exports and logs stay in ignored
`out/issue-230/`.

## Results recorded on Windows, 2026-09-15

- The initial native regression run reproduced selection loss and disabled menu
  actions against the pre-fix implementation.
- Final all-target Release build had no compiler warnings. **25/25 CTests passed**
  in 9.31 seconds. The new suite covers **92 selection cases** at 650/1050 pixels,
  Paper/Midnight, preview shown/hidden, wrapping enabled/disabled, and scrolling.
- Native tests exercise actual mouse handlers and rendered menu hit regions,
  Copy/Cut/Undo, Unicode clipboard text, selection endpoints, blank space, caret
  affinity, and handing Find focus back to the source. Clipboard tests initialize
  OLE, preserve/restore clipboard contents and allow transient clipboard read locks.
- Computer Use: Paper at 1050 pixels, English double-click selection followed by
  right-click; Midnight at 650 pixels, Chinese selection across wrapped lines
  followed by right-click. Both retain the highlight and enable Copy and Cut.
  The live samples were not edited; clipboard mutations were checked natively.
- Mixed fixture exports match the pre-fix build: four PNG pages, two HTML files,
  and all members of two DOCX files. Both PDF exports are valid. Tables, headings,
  quotes, lists, code and math were also inspected in the app.
