# Ctrl+Backspace and Ctrl+Delete in the source (#330)

Validated on Windows with the Release build on 2026-11-20.

## What was reported

In the source editor Ctrl+Backspace deletes nothing, and the count of
characters in the status bar goes up with every press. In Live the same
key takes the word, as a second reader of the issue points out.

## The cause

The source editor had no Ctrl+Backspace. Its key-down was left unconsumed,
so the message loop made the key's character, and Windows' character for
Ctrl+Backspace is 0x7F, DEL. The editor took every character from 0x20 up
as typed text, and typed it: a character that draws as nothing, saved into
the file with the rest. Ctrl+Delete makes no character and did nothing.

A note on the page (Ctrl+click a selection) had the same fault in a
smaller form: Ctrl+Backspace took one letter and typed a DEL in its place.

## What changed

- **Ctrl+Backspace** takes the word before the caret and **Ctrl+Delete**
  the word after it, in the source as in Live and the find bar.
- **What a word is.** The spaces and ASCII punctuation beside the caret,
  then the run a double click would select: a word of letters, digits and
  `_` in any alphabet, or a run of Chinese, Japanese or Korean (or of
  emoji) by itself. `foo, bar|` loses `bar`; `foo, |` loses `foo, `;
  `abc 你好|` loses `你好` and keeps `abc`.
- **No further than the line.** At a line's start Ctrl+Backspace takes the
  line break alone, and Ctrl+Delete at a line's end; inside a line neither
  goes past the line's edge, so indentation goes without the line above.
- **A selection** goes as Backspace takes it, and no word with it.
- **Several carets** each take their own word, and none goes past its
  neighbour; the round is one step to undo. With one caret each press is
  its own step.
- **0x7F is never typed**, in the source or in a note, whatever key made
  it. A note's Ctrl+Backspace and Ctrl+Delete take a word as Live's do.
- The README's key table has the two keys.

Files that an older build has put DELs into keep them until they are
deleted: they are part of the text. Ctrl+Backspace behind such a word
takes them with it.

## Fixture

`fixtures/word-delete-330.md`: plain words with commas and a full stop, a
`snake_case_name` and a hyphenated pair; a quote of two lines; a nested
list with a link, task boxes and a numbered entry; a table; a fenced C++
block with an indented line and a comment; a line of Turkish, a line of
Chinese with Latin words in it, a line with emoji and a flag; a last line
with no line break after it; under a heading with bold, italic and inline
code.

## Automated checks

**`word_delete`** (`word_delete_tests.cpp`, 2,470 checks), every key
through `keystroke.h`, so Ctrl+Backspace's DEL arrives as it does in the
app.

- 40 cases in a text of their own, each with its result, no DEL in it, no
  selection left, one undo back to the text and the caret, redo forward:
  a word, the spaces behind it, the caret inside a word, punctuation, a
  call's bracket, an underscore, the text's start and end, a line's start
  and end, empty lines one at a time, indentation in spaces and in tabs, a
  task's mark, nothing but spaces, Turkish letters, a letter with its
  accent typed after it, Chinese beside and against Latin words, emoji,
  the DELs of an older build; two and three carets, on one line, in one
  word, at a line's start and the end of the line before.
- With Shift held as well nothing goes and nothing is typed (Ctrl+Shift is
  no chord of the editor's but the few it names). A selection goes alone, one step.
  Backspace and Delete without Ctrl take a letter as they did. Four words
  go in four presses and four steps, and the fifth press does nothing.
- 30 places in the fixture, in a light and a dark theme, at 1050 with the
  preview and at 420 without, wrapped and not: the text after the key, the
  caret where the word began, the text shorter by what went and never
  longer, no DEL, one step on the undo list, the editor painting, the
  caret in the pane, undo back.
- Six presses at the end of the fixture leave it shorter, without a DEL,
  and counted as changed. With the find bar open the bar's word goes and
  the source stays. The reading view laid over the editor takes no word.
  Saved, the file has lost the word and has no 0x7F byte in it.
- Live, which had the keys: Ctrl+Backspace takes the word typed at the
  page's end, and no DEL.
- A note: Ctrl+Backspace and Ctrl+Delete take a word and type nothing,
  nothing happens at the note's edges, and the plain keys take a letter.

Each part taken out in turn fails the test:

| Taken out                                              | Failing checks |
|--------------------------------------------------------|---------------:|
| Ctrl+Backspace and Ctrl+Delete are keys of the source  |            920 |
| Ctrl+Delete as well                                    |            236 |
| Ctrl+Backspace's DEL is not typed                      |          1,072 |
| A selection goes alone                                 |              2 |
| Back: the line break alone at a line's start           |             35 |
| Back: no further than the line                         |             75 |
| Back: no further than the caret before                 |  the test ends |
| Forward: the line break alone at a line's end          |             49 |
| Forward: no further than the line                      |              1 |
| Forward: no further than the next caret                |              1 |
| Back: spaces and punctuation go with the word          |             34 |
| Back: Chinese and letters apart                        |              1 |
| Forward: spaces and punctuation go with the word       |             66 |
| Forward: Chinese and letters apart                     |              1 |
| The caret goes to where the word began                 |             12 |
| Undo knows where the caret was                         |            181 |
| The word is on the undo list                           |            559 |
| The caret is brought into view                         |             16 |
| The text counts as changed                             |              1 |
| A note: Ctrl+Backspace's DEL is not typed              |              2 |
| A note: Ctrl+Backspace takes a word                    |              1 |
| A note: Ctrl+Delete takes a word                       |              1 |

Without the limit at the caret before, two carets in one word leave a
caret past the end of the text and the test ends there, with an error.
Three were not caught at first: the limit at the next caret going forward,
Chinese against letters going forward, and the changed flag, which an
earlier case had already set. Each has a case of its own now.

The whole suite passes, 86 of 86, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which overwrite the clipboard and
whose areas this does not touch.

## In the built app

`tinta.exe` in a portable folder of its own with the fixture and a
`settings.ini` (`out/audit/realapp_330.py`, not in the repository). The
keys are posted to the window, with Ctrl down in the window's own input
state, so they pass through the message loop: `E`, Ctrl+End,
Ctrl+Backspace twice, Ctrl+Home, Ctrl+Delete, Ctrl+S. The saved file is
read.

| Build      | Saved file | DELs in it | First line              | Last line ends             |
|------------|-----------:|-----------:|-------------------------|----------------------------|
| 4.1.2      | 2 bytes longer | 2      | `# Deleting a word ...` | `... line break after it` and two DELs |
| this build | 18 bytes shorter | 0    | ` a word with the keys` | `... has no line break `   |

Both windows named the file in their title, answered after the keys and
closed with exit code 0. The clipboard is not used.

## Speed and size

- A press does what Backspace did, with one look back to the line's start
  before it; it was not timed.
- `tinta.exe` is 5,322,752 bytes, 2,560 more than the 4.1.2 build
  installed here.

## Limitations

- Screen copies come back black here, so the built app's window was not
  looked at: the saved file is what shows the keys' effect there, and the
  test paints the editor after every case.
- Ctrl+Left and Ctrl+Right in the source are as they were. They step over
  Chinese to the next word of letters, so on a line of Chinese
  Ctrl+Backspace takes less than Ctrl+Shift+Left selects.
- Live's own Ctrl+Backspace is unchanged: at a line's start it takes the
  line break and the word before it, where the source now takes the line
  break alone.
- A file that already holds DELs is not cleaned, and nothing shows where
  they are.
- A note has no selection and no word steps with Ctrl+Left; only the two
  deleting keys were added there.
