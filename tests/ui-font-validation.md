# Interface font follows the interface language (#254)

Validated on Windows with the Release build on 2026-10-08.

## Report

With the interface set to Simplified Chinese, the context menus, the settings
panel and the tab titles did not use a Simplified Chinese font.

## Cause

Interface text is drawn with Segoe UI formats in the en-us locale, and those
formats had no font fallback of their own. Segoe UI has no Chinese glyphs, so
every Chinese character went to the Windows system fallback, which chooses by
its own rules and knows nothing about Tinta's interface language. On the test
machine (Windows display language English) it drew 设 in Microsoft JhengHei UI
(Traditional Chinese) and 置 and the full-width comma in Yu Gothic UI
(Japanese), so a single label could mix two foreign faces. Documents were not
affected: they have their own fallback chain, which since #155 orders the CJK
fonts by the Windows display language.

## Change

- A second fallback chain for interface text. Kana, hangul and ideographs go
  first to the fonts of Tinta's interface language: Microsoft YaHei UI for
  Simplified Chinese, Yu Gothic UI for Japanese, Malgun Gothic for Korean, and
  Microsoft JhengHei UI for Traditional Chinese (zh-TW or zh-HK translations
  from `languages.ini`). Every other character still goes to the system
  fallback, so symbols, emoji and other scripts draw as before. Any other
  interface language builds no second chain and keeps the system fallback,
  exactly as in 3.7.5.
- Building the chain took about 0.4 ms on the test machine (measured on its
  own, after the document chain), and only a Chinese, Japanese or Korean
  interface builds it.
- The shared interface formats carry it: menus, settings, tab titles, the
  contents and file panels, search, signal chips, the theme chooser and the
  statistics. So do the formats the start page, the edit rail and the print
  preview create, and the labels drawn in the code font: Read · Ctrl+Shift+E,
  the word count, and the Copy, Copy TSV and Image buttons on code blocks,
  tables and diagrams.
- Picking a language in Settings rebuilds the interface formats, so the fonts
  change at once, without a restart.
- Documents, the editor's text and every export keep the document chain.

## Fixtures and automated checks

`fixtures/ui-font-254.md` is a Chinese document with front matter, headings,
emphasis, inline code, a link, lists, tasks, a quote, a table, a code block
and a Mermaid diagram, so the contents panel, the front matter card and the
Copy, Copy TSV and Image buttons all show interface text next to Chinese
content.

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

`ui_font_language` (new) drives the real Settings language handler and:

- checks that the shared interface formats, the theme cards and a label laid
  out in the code font draw 阅读设置， in Microsoft YaHei UI for Simplified
  Chinese, and 設定 in Microsoft JhengHei UI, Yu Gothic UI and Malgun Gothic
  for Traditional Chinese, Japanese and Korean. A language whose font is
  missing is skipped;
- checks that with English, and after switching on to German, no interface
  format carries a fallback of its own;
- checks that documents keep their face whatever the interface language;
- checks that symbols (✎ ◈ ⌘ ☐ ▦ ★ ✓), an emoji, Arabic, Thai, Hebrew and
  Devanagari still get the system fallback's fonts;
- renders, in Chinese, the settings panel (all four sections), the context
  menu, the shortcut list, the start page, the tab strip with two Chinese
  file names, the edit rail's tooltip, the Read button and the word count. It
  records the text each one sends through the interface fallback and checks
  every translated label is there.

With `useUiFontFallback` disabled, the test fails 114 of its 267 checks:
every surface, and every format in Simplified Chinese, Traditional Chinese
and Korean, for example `zh interface text uses Microsoft YaHei UI, not
Microsoft JhengHei UI|Yu Gothic UI`. Japanese passes there only because the
system fallback happens to pick Yu Gothic UI for 設定.

The full suite passed 31 of 31. After each of the two builds,
`editor_context_selection` crashed (0xc000041d) on its first run, like the
clipboard flake seen before this change, and it passed every rerun.

## Exports

Two builds, 3.7.5 and the new one, rendered the fixture to print pages, HTML
and DOCX in Paper and Midnight, with the interface in English and in Chinese.
In all four cases the two builds produced identical print pages, HTML and
DOCX members.

## Desktop check

Not run: the workstation was locked, which blocks window capture. Instead,
`out/issue-254/before-after.png` (ignored) lays out the same Chinese labels
the way the interface formats do (Segoe UI at 13 DIP, en-us), once with the
system fallback and once with the new chain. Each sample is captioned with the
fonts DirectWrite picked.

## Limits

- With any other interface language, English included, CJK text in the
  interface (file names on tabs, headings in the contents panel) still takes
  the system fallback's fonts, as in 3.7.5.
- The tab drag image is drawn with GDI, which follows Windows' own font
  linking for Segoe UI rather than the interface language.
- Native Windows dialogs (open and save, message boxes) keep the system
  font.
- The automated surface checks cannot reach the print preview or the
  buttons on code blocks, tables and diagrams, which are drawn outside the
  test build. Those were checked in the code only.

## Draft reply (not posted)

### #254

Thanks for the report. Tinta drew its interface labels in Segoe UI and left
the Chinese characters to Windows' font fallback, which doesn't know Tinta's
interface language. On an English Windows, for example, it picks Microsoft
JhengHei UI (a Traditional Chinese font) for some characters and Yu Gothic UI
(a Japanese font) for others, sometimes within one label.

In the next release, interface text uses the font of the interface language:
Microsoft YaHei UI for Simplified Chinese, Yu Gothic UI for Japanese and
Malgun Gothic for Korean. This covers menus, the settings panel, tab titles,
the contents and file panels, the start page and the editor's buttons, and it
changes as soon as you pick another language. Documents keep their own font.

感谢反馈。Tinta 的界面文字使用 Segoe UI 字体，其中的中文交给 Windows 的字体回退来显示，
而 Windows 并不知道 Tinta 当前的界面语言。例如在英文版 Windows 上，它会为一部分字选用
Microsoft JhengHei UI（繁体中文字体），为另一部分字选用 Yu Gothic UI（日文字体），同一个
标签里有时会混用两种字体。

在下一个版本中，界面文字会使用界面语言对应的字体：简体中文使用 Microsoft YaHei UI
（微软雅黑），日文使用 Yu Gothic UI，韩文使用 Malgun Gothic。菜单、设置面板、标签页标题、
目录和文件面板、开始页以及编辑器中的按钮都会使用该字体，切换界面语言后立即生效。文档正文
的字体保持不变。
