# The clipboard's older keys (#320)

Validated on Windows with the Release build on 2026-11-12.

## What was wrong

The report: Shift+Insert does not paste; Shift+Delete works.

Windows has two sets of clipboard keys: Ctrl+C, Ctrl+X and Ctrl+V, and
the older Ctrl+Insert, Shift+Delete and Shift+Insert. Tinta took the
letters everywhere and the older keys almost nowhere:

| Place                        | Ctrl+Insert | Shift+Insert | Shift+Delete |
|------------------------------|-------------|--------------|--------------|
| Live's page, a sheet's text  | copied      | pasted       | deleted      |
| Source editor                | nothing     | nothing      | deleted      |
| Find and replace fields      | nothing     | nothing      | deleted      |
| A table cell                 | nothing     | nothing      | deleted      |
| A sheet's cards              | nothing     | nothing      | deleted      |
| A note, the folder field     | (no copy)   | nothing      | (no cut)     |
| Reading view, the preview    | nothing     | (no paste)   | (no cut)     |

Shift+Delete did not work either: it took the selection away and kept
nothing of it, so the paste that should follow had nothing to paste. In
Live, Shift+Insert pasted text but not a picture or a spreadsheet range,
which Ctrl+V does.

## What changed

`clipboard_keys.h` says which letter an older key stands for
(`clipboardKeyLetter`) and turns such a key-down into Ctrl with its
letter in a handler's own key and modifiers (`takeClipboardKey`). Every
handler that knows the letters reads its key through it, and then goes on
as it did: the source editor, Live's page, the shared text engine, the
find and replace fields, a table cell and a sheet's cards. A note and the
folder field paste with Shift+Insert; the reading view and the preview's
selection copy with Ctrl+Insert.

So the older keys do exactly what the letters do in each place: Shift+
Insert pastes a spreadsheet range as a table and a picture as a file, as
Ctrl+V does (Ctrl+Shift+V still pastes as it is).

One thing is other than before: **Shift+Delete with nothing selected
does nothing**, as Ctrl+X does. It used to delete the letter after the
caret, as plain Delete does and still does. Ctrl+Delete, Insert alone
and Ctrl+Shift+Insert are what they were.

## Fixture

`fixtures/clipboard-keys-320.md`: a table of the two sets of keys (a cell
of it is edited in the test), a numbered list of the places to try them
in with bold and italic, a quote, task boxes with sub- and superscript, a
mark and a link, and a code block holding the words the test cuts.
The sheet is `fixtures/sheets/Launch plan.smd`, on a copy.

## Automated checks

**`clipboard_keys`** (`clipboard_keys_tests.cpp`, 213 checks). Each
place is put through the same steps twice, with the letters and with the
older keys, and has to end the same.

- The keys: which letter each of the eight Insert and Delete chords
  stands for, and that other keys are left alone.
- Source editor and Live: copy puts the selected word on the clipboard
  and leaves the text; cut puts it there and takes it out; paste puts it
  back; cut with nothing selected changes neither; a spreadsheet range
  arrives as a table; Delete takes one letter; Insert alone and
  Ctrl+Shift+Insert do nothing; Ctrl+Shift+V still pastes with its tabs.
- The shared text engine asked directly, as a card's text asks it: copy,
  cut, paste twice, cut with nothing selected, Delete.
- The find field and the replace field: the same, and the source is left
  alone.
- A table cell beside the preview: the same.
- The preview's selection from the editor, and the reading view after
  Ctrl+A: Ctrl+Insert copies what Ctrl+C copies; Ctrl+Shift+Insert and
  Insert alone copy nothing.
- A note and the folder browser's path field: paste.
- A sheet: a pasted flowchart arrives as two cards, copy takes them, cut
  takes them off the sheet, paste puts them back.

Each place's share taken out in turn fails the test:

| Taken out                                   | Failing checks |
|---------------------------------------------|---------------:|
| The source editor                           |              8 |
| Shift+Insert pastes as Ctrl+V, not as it is |              4 |
| Live                                        |              2 |
| The shared text engine                      |              5 |
| The find and replace fields                 |             12 |
| A table cell                                |              6 |
| A sheet's cards                             |              2 |
| A note                                      |              1 |
| The folder field                            |              1 |
| The reading view                            |              1 |
| The preview's selection                     |              1 |

The test keeps everything the clipboard held, in every format, and puts
it back when it ends (`clipboard_snapshot.h`), as `search_input_focus`
does; none of its messages says what was on the clipboard.

The whole suite passes, 80 of 80, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which overwrite the clipboard
and whose area this does not touch.

## In the built app

`tinta.exe` in a portable folder of its own with the fixture, driven
through its message queue in the source editor: `E`, Ctrl+Home, Shift+
Right five times, Shift+Delete, Ctrl+End, Shift+Insert twice, Ctrl+S. The
saved file tells what happened. The clipboard was kept before and put
back after.

| Build          | The file after                                      |
|----------------|-----------------------------------------------------|
| before the fix | five letters shorter: deleted, nothing pasted       |
| with the fix   | five letters longer: cut, and pasted twice at the end |

Both answer in under 1 ms after every step and close with exit code 0.

## Speed and size

- A key-down costs two comparisons more in a handler.
- `tinta.exe` is 5,256,192 bytes, 512 more than before.

## Limitations

- In the built app only the source editor was driven; the other places
  were checked through their handlers in the test.
- A sheet's own text fields (a card's text, a label, its find field) were
  checked through the shared engine, not by opening one on a sheet.
- Fields that take no clipboard at all with the letters (the go to line
  prompt, the theme and shortcut editors' fields) take none with the
  older keys either.
- A picture on the clipboard was not tried with Shift+Insert; it takes
  the path Ctrl+V takes.
- The shortcut list (`F1`) names only the letters.
