# Wrapped cursor navigation: issue 224

Reported by @a86022: https://github.com/oipoistar/tinta/issues/224.
Validated on Windows, 2026-09-14, on top of the #223 fixes (7f2e56b).

## Fix

Retain the leading/trailing side of mouse and arrow hits at soft-wrap boundaries.
Vertical movement resolves the current visual row from DirectWrite line metrics
and targets the adjacent row. Caret rendering, scrolling and IME anchoring use
the same wrap side; logical navigation and text changes clear obsolete state.

Before the fix, a native probe reproduced stalls in all 16 wrapped cases and
none of 16 unwrapped controls. Repeated Down produced offsets `0,25,25,25`.

## Verification

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
python tests/validate_find_focus.py --fixture tests/fixtures/wrapped-cursor-selection.md --output out/issue-224/exports
```

- All-target Release build: zero compiler warnings/errors. **23/23 CTests pass**
  (10.67 seconds). The new `wrapped_cursor_navigation` suite passes **192
  sequences**, exercising the production input handlers and caret renderer.
- Matrix: English, Chinese, emoji/combining sequences; Down/Up and Shift
  selection; widths 650/1050; preview on/off; wrap on/off; Paper/Midnight;
  display scale 1/1.5. Additional checks cover clicks on both wrap edges,
  modifier-only events, dragging, horizontal crossing, short/empty lines,
  preferred horizontal position, BOF/EOF, typing and Undo, and caret visibility.
- Fixture mixes the long paragraphs with headings, a table, emphasis, links,
  quotes, lists, inline/fenced code and inline/display math. Navigation leaves
  its source unchanged.
- Live Computer Use: Paper at 1050x900, two Shift+Down presses advance through
  the English paragraph and Shift+Up reverses one row. Midnight at 650x900,
  Chinese selection advances past the formerly stuck boundary and scrolling
  follows the caret. Reader view and exported Paper page 2 were inspected.
- Paper/Midnight exports match published v3.7.1: four PNG pages, two HTML files,
  and two DOCX packages (uncompressed members compared). Both PDFs export
  successfully with valid headers. The fixture file remains unchanged.

IME anchoring shares the tested caret geometry; an actual IME composition popup
was not exercised live. PDF content was not compared byte-for-byte.

Logs, baseline probe, exports and unposted bilingual reply are under
`out/issue-224/`. The current executable is **2,552,832 bytes**, SHA256
`444E36757BAB7F78735390F885765090939091CF4BE3B3094BD38A0F8BDFC93A`.
Both `build-meta` copies and the isolated Paper/Midnight test copies match it.
