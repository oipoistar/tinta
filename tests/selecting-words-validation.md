# Selecting words and lines in Source and Live (#313)

Validated on Windows with the Release build on 2026-11-02.

## What was reported

- In Source, a double-click on a word holding Turkish letters (ı ü ğ ö ç)
  selected only part of it.
- In Source, "after double click on a word, you can't move on the other
  words". Read here as the drag after a double-click, which every other
  editor, and Live, grows by words; Source's selection stayed on the word.
- In Live, a clip: selecting over two or more lines that hold bold and
  italic words gives "a visual breakdown". Frame by frame, two things: the
  selection band had a brighter strip at each hidden `**` and `*`, and the
  line under the moving end of the selection kept switching to its Markdown
  and back, so the text reflowed under the pointer while it moved.

## Cause

- Source's word characters were ASCII letters, digits and `_`
  (`isEditorWordChar`). Every other character fell in the class kept for CJK
  runs, so `değişiklik` was the runs `de`, `ğ`, `i`, `ş`, `iklik`, for a
  double-click, Ctrl+Left/Right, Ctrl+Backspace/Delete and Ctrl+D alike.
- Source's second and third clicks selected the word or line but did not
  take the pointer, so a drag after them did nothing.
- Live filled the selection one hit-test piece at a time, each at least
  4 px wide. A hidden mark is a piece of no width between bold and plain
  words, so its 4 px overlapped its neighbours and the translucent fill
  doubled there.
- Live showed the Markdown of the caret's line, and during a selection the
  caret is its moving end.

## Change

- Source's word characters are the letters, digits and marks of every
  alphabetic script (`GetStringTypeW`: C1_ALPHA or C1_DIGIT, or a nonspacing
  mark or vowel sign, so `cafe` with a typed U+0301 is one word) and `_`.
  CJK, kana and Hangul, from U+2E80, keep their own run, so `中文测试`
  beside Latin words still selects alone.
- Source's double- and triple-click take the pointer, and a drag after them
  grows the selection by whole words or lines, keeping the one the clicks
  took whichever way it runs (`editorUnitAt`), as Live's `pressAt` and
  `dragTo` do. A plain click's drag still goes letter by letter.
- Live joins the pieces of each line before it fills: one band a line, at
  least 4 px for a selected empty line (`live_draw.cpp`).
- Live shows the Markdown of the line a selection started from, its anchor,
  rather than its moving end (`caretLineOf`). The lines it grows over stay
  as the page reads them, by the mouse or with Shift+arrows, and once the
  selection is gone the caret's line shows its Markdown again. Sheet cards
  share the function.
- `docs/live-editor.md` says so under Moving and selecting.

## Fixture

`fixtures/live/Selecting words.md`: Turkish words in plain text, bold and
italic; the cities Iğdır, Şırnak and Ağrı, dotless ı and all; Greek, Russian and Vietnamese
list items, a typed combining accent, Chinese beside Latin words; a quote
over two lines with bold and italic; a table with Turkish words and inline
code; the reporter's numbered list with bold, italic and a link; a code
block; strikethrough and a highlight.

## Automated checks

- `editor_context_selection` (`wordsOfEveryScript`), in Source on the
  fixture: a double-click on the first letter, a middle letter and the last
  letter of 13 words (Turkish, Greek, Russian, Vietnamese, a combining
  accent, a table cell, an identifier in code) selects the word; Chinese
  still selects alone; Ctrl+Right and Ctrl+Left step over whole Turkish
  words; a drag after a double-click takes whole words forward and back and
  keeps the word, a drag within it keeps just the word; a drag after a
  triple-click takes whole lines; a plain drag goes letter by letter.
- `live_editor` (`selectingLines`): Shift+Down twice from the list's first
  line grows the selection over the list, the first line keeps its Markdown
  and the page its height, and Right shows the caret's line again. On the
  engine: the first line's words stay where they were as the selection
  grows down, while its `**` is shown only while it is typed in; a
  selection over a line drawn as the page reads it is one even band (the
  pixels of its top row differ by less than 0.01).

Neutering one part at a time:

- ASCII word characters again: `editor_context_selection` fails 39 checks.
- No pointer for the second and third clicks: 7.
- Pieces filled one by one: `live_editor` fails 1 (a strip 0.133 brighter).
- The caret's line shown during a selection: 2.

## In the built app

An isolated portable copy (`out/select-313/`, ignored) in Midnight at
1280 x 900, driven with posted messages and captured with PrintWindow
(`out/select-313/shots/`):

- Source: a double-click on `değişiklik` selects it whole. Double-click,
  hold and drag down to `öğrencilerimizden`: whole words up to it; back to
  `bütün`: whole words back, `değişiklik` still in.
- Live: press on the list's first line and drag over the second and third
  (captured with the button held at each, and after the release): the first
  line keeps its faint `**` and `*` and does not move, the others stay as
  the page reads them, and the band is even over the bold words, the link
  and `xx`.

## Size

`tinta.exe` grows by 2,560 bytes (3,945,472 to 3,948,032).

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
55 of 55.

## Limitations

- Words without spaces in scripts below U+2E80, such as Thai, select as one
  run; there is no dictionary segmentation, as there is none for CJK.
- With a selection, the anchor's line shows its Markdown: after Ctrl+A that
  is the document's first line.
- The clip's other effect, a line switching to its Markdown when the caret
  enters it, stays for the caret: placing it in a line, by a click or the
  arrows, shows that line's Markdown, as Live always has.
