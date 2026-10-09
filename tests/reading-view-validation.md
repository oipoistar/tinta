# Esc and the reading view (#242)

Validated on Windows with the Release build on 2026-09-22.

## Problem

3.7.3 (#236) rebound Esc in the editor to toggle a full-width reading view of
the live buffer. That view was the split editor with a zero-width source pane,
so everything sized to the pane collapsed with it: the tab strip, top-band click
routing, the title-bar hit test and the window drag area. Esc could no longer
leave edit mode, and the unsaved-changes dialog was only reachable by closing
the tab. Reproduced on the official 3.7.3 binary: in the reading view no point
of the title bar was draggable, the leftover + and chevron ignored clicks, and
right-click opened no menu.

## Change

- Esc in the editor works as it did before 3.7.3. A clean buffer leaves edit
  mode on a second Esc within 500 ms, and unsaved changes open the Save /
  Discard / Keep editing dialog (#106).
- The reading view keeps its entry points, the Read button and Ctrl+Shift+E.
  Esc, E, Ctrl+E or the Edit button return to the editor with text, caret and
  undo intact. The Read button now reads "Read · Ctrl+Shift+E" and sizes to its
  label.
- `editSheetLayout()` is true only for the split editor with its floating
  sheet. Title-bar and page geometry use it: tab strip width and caption
  buttons, the drag area and title-bar hit test, top-band clicks, the tab drop
  target, the page top offset and width, the sheet background and clip, and the
  split seam. Document rendering keeps `editorPreviewVisible()`.
- The reading view uses the reader's page: plain theme background, content
  below the tab strip, and the reading column setting.
- The app icon cell follows the edit rail rather than edit mode. In the reading
  view the rail is hidden, so the icon uses the reader's 40 px cell and the
  first tab keeps its full click target.

## Fixtures and automated checks

`fixtures/reading-view-242.md` is a mixed document with headings, a table,
lists, tasks, emphasis, a relative link, a quote, CJK text, code and inline and
display math. It also describes the expected Esc behaviour for manual checks.
The #236 fixture now points at Read · Ctrl+Shift+E.

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

- Full Release build succeeded with no compiler warnings.
- All 28 CTest suites passed.
- `table_and_modifier_input` gains an Esc and chrome check in Paper and Midnight
  at 650 and 1050 pixels. It covers the double-Esc exit on a clean buffer, the
  save dialog on unsaved edits, and the reading view: no sheet layout, full
  viewport width, page below the tab strip, the whole document laid out, a drag
  area beside the window buttons, drawn tabs and + button, Esc back to the
  editor, and a tab click through the strip that switches tabs and returns to
  the unsaved buffer.
- The 3.7.3 assertion that Esc reads a dirty buffer without a save prompt now
  asserts the dialog, then that Ctrl+Shift+E still reads the unsaved buffer.
- `window_drag_and_title_space` runs its strip checks in a third mode, the
  reading view, across both themes, scales 1.0, 1.5 and 2.0 and widths from 325
  to 1050 pixels. It asserts the reader's drag area beside the window buttons.
- Against the 3.7.3 product code, the new checks fail as expected: 34 table and
  modifier failures and 1,296 window-drag failures. On 3.7.3, Ctrl+Shift+E in
  that sequence toggled the view back off, so the tab-click check ran in the
  split editor there; the window-drag failures and the live 3.7.3 repro cover
  the reading-view strip instead.

## Desktop check

The built application ran in portable mode from a scratch folder with the new
fixture beside `table-input-236.md` as a second tab, in Paper and Midnight at
650 and 1050 pixels. Input was posted window messages:

- `:` entered edit mode. One Esc kept editing and showed "Press ESC again to
  exit edit mode"; two Esc presses 150 ms apart returned to the reader with
  both tabs visible.
- After a typed space, Esc opened the unsaved-changes dialog; Esc after the
  grace window kept editing.
- The Read button opened the reading view with both tabs, the + button, the
  normal window buttons and the page below the strip. A title-bar hit-test scan
  found the same draggable points as the reader in all four runs.
- Clicking the second tab switched to it with the first tab's unsaved dot
  intact; clicking back restored the split editor with the typed space.
- The Read · Ctrl+Shift+E label fits its pill at both widths.

## Exports

A comparison against the official 3.7.3 binary rendered both fixtures in Paper
and Midnight. HTML bytes and all DOCX archive members were identical, every
print page had zero changed pixels, and both PDF exports were valid. Outputs are
under the ignored `out/issue-242-exports/` directory.

## Limits

- The title-bar hit test lives in the window procedure, which the test binary
  does not link. It was verified live with real `WM_NCHITTEST` messages and
  shares the predicate that the native tests pin.
- Posted messages cannot hold Ctrl and Shift, so Ctrl+Shift+E was covered by
  the native tests; the live runs entered the reading view with the Read button.
- Reader features inside the reading view, such as the document menu, Contents,
  the file browser, annotations and task checkbox clicks, remain unavailable,
  and the Edit button still floats over text at the bottom left. Both are out
  of scope for this fix.

## Draft reply (not posted)

### #242

Thanks for the clear report. This was a regression in 3.7.3: Esc had been
changed to open a full-width reading view instead of leaving edit mode, and that
view dropped the tab bar. In the next release Esc works as before. Press it
twice to leave edit mode, and with unsaved changes it asks whether to save. The
full-width preview of unsaved edits stays available through the Read button or
Ctrl+Shift+E, and it now keeps the tab bar and a draggable title bar.
