# Find and Replace in the editor while the text changes

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code; nobody reported it. In the Source editor
the matches of Find are places in the text as it was when it was
searched, and the text was searched only when the query changed or the
bar took the keyboard again. With the bar open the text still changes: a
button of the tool rail, typing in the text, a change taken from the
disk. Then:

- Enter in the Replace field, or the Replace button, replaced whatever
  stood where the match had been. When that was past the text's end the
  program ended: the test, run without the fix, stops with exit code
  0xc0000409 at the rail's Bold button followed by Enter;
- Enter in the Find field put the caret where the match had been, past
  the end of a text cut short, and the next letter typed ended the
  program;
- the marks and the count on the page stayed where the words had been.

## Change

- The matches carry the revision of the text they were found in
  (`editorSearchRevision` beside `editorTextRevision`).
  `editorSearchRefresh` (`editor.cpp`) searches again when the text has
  changed, and keeps the match that was current, or the next one after
  where it was.
- It is asked for before the matches are used: by Replace, by the jump to
  a match, by a key or a click in the bar, and by a frame before it draws
  the marks.
- The frame's scan for the first match in view no longer reads past the
  list of lines when the view is scrolled beyond a text cut short.

## Fixture

`fixtures/find-after-edits.md`: the three cases as numbered steps to try
by hand, with the word in a line of its own, a table, emphasis, a quote,
code and a code block. The test builds its own short texts, as each case
needs an exact one.

## Automated checks

**`search_input_focus`** (`search_input_tests.cpp`), in `staleMatches`,
through `keystroke.h` and the rail's own function:

- `**bold** ab` with `bold` selected, Ctrl+H, `ab`, Tab, `x`, Bold on the
  rail (the text becomes `bold ab`), Enter: the text is `bold x`;
- lines put in above a match with the bar open: a frame leaves every
  match on its word, and Replace changes the word and nothing else;
- the text cut short under a match in its last line: Enter in Replace
  changes nothing and no match is left; Enter in Find leaves the caret
  in the text, and a frame scrolled beyond the text draws.

Without the refresh the test does not get past the first case: it ends
with exit code 0xc0000409, the program's end the finding describes.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
64 of 64.

## Size

`tinta.exe` is 4,028,416 bytes, 512 more than before.

## Limitations

- Not driven in the built window: the screen could not be copied in this
  session to find the rail's button. The test calls the function the
  button calls.
- With the bar open, each change of the text searches all of it again
  for the next frame, as a letter typed into Find does.
- When the bar takes the keyboard again the search still starts over at
  the first match, so a click into the text and back makes Replace act
  on the first match of the file, as before.
- Live has its own page and does not draw Source's marks; it was not
  looked at here.
