# Read pill clearance (#250)

Validated on Windows with the Release build on 2026-10-08.

## Problem

The Read · Ctrl+Shift+E pill floats over the source editor's bottom-left
corner, 12 to 42 pixels above the window bottom at 100 % scale. Caret-follow
kept the caret line only one line height above the bottom, and at maximum scroll
the last line ended 8 pixels above it, so both landed inside the pill's band.
Typing on the bottom row hid the current line, and the end of a long note could
never scroll out from under the pill. The full-width reading view floats the
same pill, labelled Edit · Esc. Its page tail scales with zoom while the pill
does not, so zoomed out the last line ended under the pill as well: at 50 %
zoom it ended 15 pixels inside the pill's band.

## Change

- `kReadPillLift`, `kReadPillHeight` and `readPillClearance()` in `app.h` name
  the pill's band, and `editorReadingButtonRect()` uses them.
- The source keeps the clearance, or a line height when that is larger, both in
  caret-follow and in its scrollable height. The caret line always stays above
  the pill, and scrolled to its end the last line rises above it.
- In the reading view the page tail is at least the clearance at every zoom.
  The reader outside edit mode, the split editor's preview sheet and exports are
  unchanged.

## Fixtures and automated checks

`fixtures/read-pill-250.md` is a long mixed note with headings, a table, lists,
tasks, a quote, code, inline math, a footnote, CJK text and a marked last line.
It describes the expected behaviour for manual checks.

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

- `wrapped_cursor_navigation` gains a Read pill check across Paper and
  Midnight, scales 1.0 and 1.5, widths 650 and 1050 pixels, with and without
  the preview, wrapped and unwrapped. Walking Down through a 241-line note must
  keep the caret line above the pill after every step, and the wheel scrolled
  to the end must lift the last line above it. In the reading view at 50, 100
  and 300 % zoom, the scrolled-to-end last line must clear the Edit pill. The
  suite now runs 272 sequences with no failures.
- Against the 3.7.4 editor code both editor checks fail; against the 3.7.4
  layout code the reading-view check fails at 50 % zoom and passes at 100 and
  300 %.

## Desktop check

The built application ran in portable mode from a scratch folder with the
fixture, in Paper at 1000 x 800 and 96 DPI with the preview on. Input was posted
window messages and the window was captured with `PrintWindow`.

- E opened the editor and 34 Down presses put the caret on line 35. On 3.7.4
  the caret line's highlight and number sat behind the pill; with the fix the
  line sat just above it.
- 200 more Down presses reached the end. On 3.7.4 the caret's last row was
  behind the pill; with the fix it and the marked last line sat above it. The
  preview pane scrolled to the same place on both builds.
- At 50 % zoom the Read pill opened the reading view and the wheel scrolled to
  the end. On 3.7.4 the footnote's back link, the last line, was cut by the
  Edit · Esc pill; with the fix it was fully visible.

## Exports

The fixture and `fixtures/reading-view-242.md` rendered to HTML, DOCX, PDF and
print pages in Paper and Midnight with the 3.7.4 and the fixed build. HTML,
every print page and every DOCX member were identical, and all PDFs were
valid. Outputs are under the ignored `out/open-issue-fixes/exports/`
directory.

## Limits

- The pill still floats over text in the middle of a note while it scrolls;
  only the caret line and the end of the note are kept clear. This follows the
  option the reporters preferred, automatic scrolling, rather than moving or
  hiding the pill.
- The pill's label is drawn with the zoomed code font while the pill keeps its
  size, so at 50 % zoom the label is small. This is unchanged and out of scope.

## Draft reply (not posted)

### #250

Thanks to both of you. In the next release the editor keeps the line you are
typing on above the Read · Ctrl+Shift+E button: when the caret reaches the
bottom of the window the text scrolls up, and at the end of a document the last
line scrolls clear of the button too. The full-width reading view does the same
for its Edit · Esc button, including when zoomed out. @Ra0EL, this is the
automatic scrolling you preferred. The button stays where it is, so text can
still pass under it while you scroll, but the line you are working on is no
longer hidden.

感谢两位的反馈。在下一个版本中，编辑器会让正在输入的那一行始终显示在“阅读 ·
Ctrl+Shift+E”按钮上方：光标到达窗口底部时，文本会自动向上滚动；滚动到文档末尾时，
最后一行也会完全显示在按钮上方。全宽阅读视图中的“编辑 · Esc”按钮同样如此，缩小显示
比例时也不例外。按钮本身的位置不变，滚动时文字仍会从它下面经过，但你正在编辑的那一行
不会再被遮挡。
