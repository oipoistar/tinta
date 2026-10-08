# Word and character count (#240)

Validated on Windows with the Release build on 2026-10-08.

## Request

A word and character count like Word's, shown at the bottom left of the
editor.

## Change

- A chip beside the Read · Ctrl+Shift+E button, in its shape and quieter
  ink, shows `1,234 words · 5,678 characters` for the document. While text is
  selected it reads `12 / 1,234 words · 60 / 5,678 characters`, adding up the
  selections of every caret. It also shows in the full-width reading view.
  The characters drop out when the pane is too narrow, then the whole chip.
  Clicks on it do nothing, so no caret lands in text hidden under it.
- It counts what a reader sees: prose, table cells, link text and inline code
  from the parsed document. Markdown syntax, link addresses, front matter,
  code blocks, diagrams, math, image alt text and footnote markers are left
  out. Plain-text and diagram documents count as they stand.
- Every CJK ideograph and kana is one word, as in Word's count for Chinese
  and Japanese; other words are runs between spaces holding a letter or
  digit, so `don't` and `e-mail` are one word and a lone dash is none.
  Characters are code points, spaces included and line breaks not, so an
  emoji is one character.
- The document count refreshes with the editor's 300 ms reparse debounce,
  which now also runs while the preview is hidden. With the preview visible
  it reuses the preview's parse. A selection is counted when it changes; while
  a drag selects more than 200,000 characters, the chip keeps the last count
  and recounts when the drag ends.
- Settings → Editor → **Word count** (`showWordCount`, on by default), saved
  the moment it changes. English, Chinese, Japanese and Korean labels.

## Fixtures and automated checks

`fixtures/word-count-240.md` mixes front matter, a heading, emphasis,
strikethrough, highlight, a link, inline code, an image, a table with Chinese
and Japanese cells, lists, a quote, a code block, display and inline math,
Chinese prose and a footnote.

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

- `word_count` (new) checks the plain-text rules (English, punctuation,
  line breaks, Chinese with full-width punctuation, kanji and katakana,
  Korean, the ideographic space, emoji, an extension-B ideograph, full-width
  digits, CJK inside Latin text) and the Markdown rules (emphasis markers,
  link URLs, inline code against code blocks, front matter, tables). The
  fixture counts 69 visible words, worked out by hand.
- `editor_features` checks the editor's own count after a reparse, the chip
  painting beside the Read button, a selection's count, a plain-text document
  counted raw, and the switch hiding the chip.

## Exports

The fixture rendered to HTML, DOCX, PDF and print pages in Paper and Midnight
with the 3.7.4 and the new build. HTML, every print page and every DOCX
member were identical, and the PDFs were valid.

## Desktop check

Not run yet: the workstation was locked while the build was ready, which
blocks window capture.

## Limits

- A selection is counted by parsing the selected source, so a selection that
  starts inside a code block or link counts that fragment as prose.
- The chip floats over the editor like the Read button; the caret line and
  the end of the note stay clear of both (#250).

## Draft reply (not posted)

### #240

The next release shows a word and character count in the editor's bottom-left
corner, next to the Read · Ctrl+Shift+E button. It counts the text a reader
sees, so Markdown syntax, link addresses, front matter, code blocks and
formulas are left out. As in Word, each Chinese or Japanese character counts
as one word; characters include spaces but not line breaks. While text is
selected it shows the selection and the total, for example 12 / 345. You can
turn it off in Settings → Editor → Word count.

下一个版本会在编辑器左下角、“阅读 · Ctrl+Shift+E”按钮旁边显示字数和字符数。统计的是
读者看到的文字：Markdown 语法、链接地址、文档属性（front matter）、代码块和公式不计入。
与 Word 一样，每个汉字或日文字符算作一个字；字符数包含空格，但不包含换行。选中文字时会
显示选中部分和全文的数量，例如 12 / 345。可以在“设置 → 编辑器 → 字数统计”中关闭。
