# Tab insertion and tear-off validation

Validated on Windows on 2026-09-10. These changes are unreleased.

## Behavior

- A window or detached tab dropped on another tab strip inserts at the gap
  selected by the pointer. Each tab's left/right half selects before/after it.
- The receiving window computes the position using its own scale and layout.
- Ordinary file opens and drops on document content still append. An already
  open file activates its existing tab without duplication or reordering.
- A saved tab pulled about 50 logical pixels above or below the tab bar can
  be released over its own document to create a separate window. It no longer
  needs to cross the window border. The existing threshold is 48 logical pixels.
- Returning near the tab bar cancels detachment; Escape/capture cancellation
  removes the ghost and keeps the tab. Dirty and untitled tabs retain their
  existing protection against detachment.
- The source removes the tab only after a receiving window acknowledges the
  transfer or Windows reports that the new process launched. New-window
  arguments preserve Unicode file paths.

## Automated checks

```powershell
cmake --build build --config Release --parallel 6
ctest --test-dir build -C Release --output-on-failure
```

The full Release build and all **15 CTest suites** pass. `tab_drop_position`
reuses the existing input-test executable, staged in an isolated portable
directory so test documents and reading-position persistence stay there.

The native tests render tab strips at logical widths 650 and 1050, in Paper
and Midnight, at 100%, 150% and 200% scale, in reading and editing layouts.
They exercise the actual WM_COPYDATA sender/receiver, including negative
screen coordinates, UTF-8 paths, first/middle/last insertion, duplicate paths,
ordinary launch messages, and malformed/rejected messages. A dirty destination
editor buffer and its cursor survive insertion before its tab and switching back.

The drag tests call the real move/cancel/release handlers. They check the
threshold on both sides of the strip and returning to the strip at three scales.
For release, an off-screen native test window is visible only outside the virtual
desktop, and WindowFromPoint confirms that the drop is over the source document.
The real launch path starts a guarded test child that records its arguments.
The test verifies the new-window position, Unicode document path, and removal
of only the dragged tab. It injects no desktop mouse or keyboard input.

## Mixed Markdown and manual coverage

`fixtures/tab-drop-position.md` describes insertion and tear-off scenarios and
mixes headings, a table, a quote, bold text, highlights, lists, links, inline and
fenced code, and three equations.

```powershell
./tests/render_tab_drop_fixtures.ps1
./tests/render_tab_drop_fixtures.ps1 -Binary out/tab-drop-position/baseline.exe -Output out/tab-drop-position/baseline
```

The new fixture and the existing Settings and file-path fixtures were exported
with the pre-change and final builds. All **five native PNG pages**, HTML bytes
and DOCX ZIP contents are identical. Original Markdown files remain unchanged.
Generated evidence is under ignored `out/tab-drop-position/`.

Computer Use showed the three-tab destination and mixed fixture, but the user
stopped Computer Use with Escape before the live drag gesture. The user then
tested the insertion build and confirmed that it works. The later tear-off
refinement is validated by the native launch test; a live desktop drag and
physical mixed-DPI monitor gesture are not claimed as completed checks.

Use the current build in both windows. A window running an older binary does
not understand the new positioned transfer message; the source keeps its tab
when that recipient does not acknowledge the transfer.
