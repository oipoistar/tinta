# Review notes after the text changed

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code; nobody reported it. A review note is a
comment in the Markdown, `<!-- review "quoted words": the note -->`, and
Tinta keeps where each one stands in the text. After an edit saved from
the editor, deleting a note or changing its words wrote to where the
comment had stood before the edit. Other text was removed or overwritten,
pieces of the comment stayed behind, and the reading view has no undo.
The test reproduces it: a line typed at the top and saved, then the first
note deleted, and the file is no longer the saved text without that note.

## Cause

`annotationsParseSource` reads the notes and their places from
`app.sourceText`. Three places in `editor.cpp` replace that text without
reading the notes again: leaving the editor, saving, and taking a change
from the disk into a clean buffer. `annotationEditorConfirm` and
`annotationEditorDelete` (`annotations.cpp`) then used the old places,
checked only against the length of the text. A new note's place, found
when its box opened, was used the same way when the text changed under
the box.

## Change

- The three places read the notes again, and the two that empty the text
  for a tab without a file empty the notes with it.
- The note box remembers its note by what it says: the quoted words, the
  note, and which of the notes that say the same it is. Confirm and Delete
  read the notes again and find it by that before writing. A note that is
  no longer in the text is not written: the box closes and a toast says
  it could not be located.
- A new note is written only into the text its place was found in. When
  the text has changed, nothing is written, the toast shows, and the box
  stays open with what was typed.

## Fixture

`fixtures/review-notes-edited.md`: a heading, the steps to try, a
paragraph with a note under it, a table, a list with a note under it, a
quote and a code block.

## Automated checks

**`review_notes_after_edit`** (`review_note_tests.cpp`, in `input_tests`),
on a copy of the fixture:

- a line typed at the top through `keystroke.h`, Ctrl+S, the editor left:
  both notes are there and each one's place holds its comment, whole;
- the first note deleted: the file is the saved text without that
  comment's line, byte for byte;
- the other note changed while its place is out of date (a line put into
  the text as a reload without a reading would leave it): the file
  differs by the note's words only;
- a note taken out of the text while its box is open, then deleted: the
  file is as it was, the box is closed, the notes are read again;
- a new note begun on selected words, the text changed under its box:
  nothing is written and the box stays; with the text unchanged, the note
  is written as a line of its own after its paragraph and nothing else
  changes.

Without the notes read again after a save, 1 check fails. Without the
note found again before writing, 5 fail. With both taken out, as the code
was, 7 fail, among them the deletion after an edit.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
63 of 63.

## Size

`tinta.exe` is 4,018,176 bytes, 1,024 more than before.

## Limitations

- Not driven in the built window: a note is opened from its square on
  the rail, and the screen could not be copied in this session to find
  it. The test calls the functions the rail's click and the box's buttons
  call.
- The toast for a note that is gone is the existing "Couldn't locate
  selection", in every language. A text of its own would need
  translating.
- Two notes that quote the same words and say the same are told apart by
  their order. If one of them is removed elsewhere while the other's box
  is open, the box may end up on the remaining one, which says the same.
