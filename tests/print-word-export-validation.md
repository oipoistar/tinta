# Print, PDF and Word export follow the screen (#256, #257, #258)

Validated on Windows with the Release build on 2026-10-09.

## Reports

- #256: exported to Word, the layout is fine but the fonts differ from the
  screen.
- #257: printed or exported to PDF, the text is too narrow, with wide left and
  right margins and no setting to change them. The layout also differs from
  the screen, code blocks in particular: some wide, some narrow. Word export
  lays out correctly.
- #258: with a custom theme (`font=Noto Sans SC`, `codefont=Noto Sans Mono`)
  the screen uses the theme's fonts, but print and PDF use Microsoft YaHei UI
  and Consolas.

## Causes

- To get a white page, print swapped the whole theme for the built-in Paper
  theme, fonts included: Segoe UI with Chinese falling back to Microsoft
  YaHei UI, and Consolas for code (#258).
- Print laid the document out at the printable width but kept the screen's
  40 DIP side padding inside the 0.75 in margins, so the text sat 112 DIP
  (about 30 mm) from each edge, where Word export uses 20 mm. It also kept the
  reading width from Settings (a 70% column printed about 400 of the page's
  794 DIP) and the title strip's offset above the first page's text. A code
  block wider than the page was shrunk to the full margins rather than to the
  text column, so it stuck out on both sides of the narrower blocks (#257).
- Word export named the theme's fonts for Latin and complex scripts only.
  Without `w:eastAsia`, Word sets Chinese in its own default East Asian font
  (DengXian or SimSun on Chinese Office), not the face the screen shows. It
  also ignored `headingfont=` (#256).

## Change

- Print keeps its light white-page palette but takes the active theme's body,
  heading and code faces (#258).
- Laid out for paper, the page margins frame the text: no screen padding, no
  reading-width column and no title-strip offset. Text runs between the
  margins, a wide code block shrinks to the same edges as the others, and the
  first page starts at its top margin (#257). The link preview, which borrows
  the print layout, keeps the screen's look.
- The margins can be chosen (#257): chips at the bottom left of the print
  preview offer 12.7, 19.05 (0.75 in, still the default) and 25.4 mm, in
  inches where the locale measures in inches. A pick re-lays the preview out
  at once and is saved as `printMarginMm`, which also takes any value from 5
  to 50 mm by hand; a value outside the presets shows as its own chip.
  Printing, PDF export and the debug page renderer all use it. The preview's
  key hint now shows after the chips when it fits on one line.
- Word export names, as `w:eastAsia`, the face the screen draws CJK in: the
  theme's font when it has CJK glyphs (Noto Sans SC, SimSun), else the first
  installed face of the document fallback (Microsoft YaHei UI on Chinese and
  English Windows). Body text, code blocks, inline code and, when the theme
  sets `headingfont=`, the heading styles carry it (#256).

## Fixtures and automated checks

`fixtures/print-word-export-256.md` mixes Chinese and English prose, emphasis,
inline code, a link, a list, a quote, a table and three code blocks, one wider
than any page. `fixtures/print-word-export-themes.ini` holds the reporter's
theme with SimSun and Courier New standing in for Noto Sans SC and Noto Sans
Mono, which the test machine lacks: a CJK body face and a Latin-only code face.

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

`print_and_word_export` (new) loads that theme from `themes.ini`, sets the
reading width to 70%, opens the fixture and:

- checks the screen draws Chinese in SimSun;
- opens the print preview on A4 and checks the print formats use SimSun and
  Courier New, printed Chinese and code land in the faces the screen uses,
  and the page stays white;
- checks the text column starts at the left margin and spans the printable
  width, and the first heading sits at the top margin;
- checks every code block starts at the left margin and ends at the right
  one, and the wide one shrinks to those same edges;
- checks closing the preview restores the screen's theme and column, and the
  link preview still renders;
- renders the preview and clicks each margin chip through the real mouse
  handlers, checking the column narrows or widens to sit between the chosen
  margins and the choice is saved; `printMarginMm=15` loads as 15, while 3, 60
  and `abc` keep the default;
- exports to Word and checks body text names SimSun as its East Asian font
  and code blocks and inline code name the face the screen falls back to under
  Courier New; with Georgia as the body face and Cambria as `headingfont=`,
  the body and heading styles name the faces the screen falls back to.

All 43 checks pass. With each change disabled in turn, the test fails 2 checks
(print fonts), 8 (print layout), 4 (margin chips) and 4 (Word fonts).
`ui_font_language` now reads glyph faces through the same shared
`tests/glyph_fonts.h`.

The full suite passed 32 of 32. `editor_context_selection` crashed
(0xc000041d) on its first run after the build, like the clipboard flake seen
before this change, and passed every rerun.

## Exports

The current master (1b69939) and the fix exported three controls
(`markdown-regression-control`, `footnotes`, `table-input-236`) and the new
fixture in Paper and Midnight. The HTML was identical, every DOCX part was
identical apart from the added `w:eastAsia` attributes, and the print pages
changed as intended with the same page counts.

The reporter's setup through both builds, with the custom theme and a 70%
reading width: before, the printed column is about 400 DIP wide, in Segoe UI
and Microsoft YaHei UI; after, it spans the 650 DIP between the margins in
SimSun, with Courier New for code. The Word styles name no East Asian font
before; after, SimSun for the body and Microsoft YaHei UI for code.

`out/issue-256/before-after.png` (ignored) shows page 1 from both builds.

The built app rendered the fixture's print pages with `printMarginMm` at 12.7,
19.05 and 25.4: the first ink on page 1 sat at x = 48, 72 and 96 DIP, exactly
the margin each time.

## Desktop check

Not run: the workstation was locked, which blocks window capture.

## Limits

- One margin applies to all four sides, and only to printing and PDF; Word
  export keeps its 20 mm margins, which Word itself can change.
- A code block much wider than the page still shrinks as a whole, so a very
  long line prints small; Word wraps it instead.
- Word has one East Asian face per run, so Japanese or Korean text in a
  Chinese setup uses the same face, where the screen picks Yu Gothic UI or
  Malgun Gothic for kana and hangul.
- Print keeps the light palette: a dark theme's colours do not print.

## Draft replies (not posted)

### #256

Thanks for the report. The Word file only named the theme's font for Latin
text, so Word set Chinese in its own default East Asian font. In the next
release the Word export also names the font the screen uses for Chinese: the
theme's font when it has Chinese glyphs, otherwise Microsoft YaHei UI. Code,
and headings when the theme sets `headingfont=`, get the same treatment.

感谢反馈。Word 导出时只为西文文字指定了主题字体，中文因此使用了 Word 自己的默认东亚字体。
下一个版本中，Word 导出也会为中文指定与屏幕显示相同的字体：主题字体包含中文字形时使用主题
字体，否则使用 Microsoft YaHei UI。代码，以及主题设置了 `headingfont=` 时的标题，也同样处理。

### #257

Thanks for the report. Printing kept the screen's own side padding inside the
page margins, and the reading width from Settings narrowed the text further,
while a code block wider than the page was shrunk to the full margins, so the
code blocks came out at different widths. In the next release printed text
runs between the page margins whatever the reading width, close to the Word
export's layout, and every code block lines up with the text. The margins can
be set too: the print preview offers 12.7, 19.05 and 25.4 mm at the bottom
left, remembers the choice, and `printMarginMm=` in settings.ini takes any
value from 5 to 50 mm.

感谢反馈。打印时在页边距之内还保留了屏幕上的左右留白，设置中的阅读宽度又让正文进一步变窄；
而比页面更宽的代码段会缩小到整个页边距的宽度，所以代码段有宽有窄。下一个版本中，打印的正文会
占满页边距之间的宽度，不受阅读宽度影响，与 Word 导出的版面相近，所有代码段都与正文左右对齐。
页边距也可以调节了：打印预览左下角提供 12.7、19.05 和 25.4 毫米三个选项，选择会被记住；
settings.ini 中的 `printMarginMm=` 可以设置 5 到 50 毫米之间的任意值。

### #258

Thanks for the report and the theme. To print on a white page, Tinta switched
to a built-in light palette and its fonts came along with it. In the next
release print and PDF keep the white page but use your theme's `font=`,
`headingfont=` and `codefont=`.

感谢反馈和提供的主题。为了打印在白色页面上，Tinta 切换到了内置的浅色配色，字体也随之被替换。
下一个版本中，打印和 PDF 仍使用白色页面，但会使用主题中的 `font=`、`headingfont=` 和
`codefont=`。
