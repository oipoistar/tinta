# A file another program changes while Tinta shows it (#311)

Validated on Windows with the Release build on 2026-11-02.

## What was reported

An AI agent or another editor changes a `.md` file open in Tinta, and Tinta
does not notice: the file has to be opened again. The reporter asked for
what Notepad++ does, a "This file has been modified by another program. Do
you want to reload it?" question with Yes and No.

## What Tinta did

Reproduced in the built app (an isolated copy in `out/watch-311/`):

- The reader polls the file every 500 ms and reloads it: a write in place
  and a temp file renamed over the note both showed within a second.
- An editor, Source or Live, with nothing unsaved ignored the change for
  good: the timer returned in edit mode. Since 4.0 an editor can stay open
  for a whole session in Live, which reads like the page.
- With unsaved edits the change brought up the "Unsaved changes" dialog,
  Save and exit or Discard changes, with nothing about the disk. Worse, a
  change while the editor was clean was remembered: the first key typed
  later brought up that dialog, and Save and exit wrote over the other
  program's text.
- A tab parked in the editor behind another tab took the file's time when
  it came back, so a change made while it was away was never noticed.

## The change

- An editor with nothing unsaved takes the new text as the reader does
  (`editorReloadFromDisk`): only the part that changed is replaced, so the
  caret, the selection and other carets stay in the text around it, moving
  with it when the change is above them; the replacement is one Ctrl+Z, after
  which the buffer is unsaved; the preview and Live follow, and a brief note
  says "Reloaded from disk" with the file's name.
- With unsaved edits the dialog asks about the disk: "Changed on disk —
  name", Reload or Keep mine (Y, N, Esc). Reload takes the disk's text as one
  step Ctrl+Z takes back; Keep mine goes on editing, and the next change asks
  again. As it comes up unasked, Y and Enter wait out its first 600 ms as N
  and Esc always did, and Ctrl+S does not answer it. Closing the window while
  it is up asks about leaving instead.
- A key the dialog takes types nothing after it (`handleKeyDown`): Keep mine
  leaves the editor open, and its N went on to type an n.
- A parked editor tab keeps the time of the text it holds, and checks it
  when it comes back.
- The strings are in all eight languages; the README lists the behaviour.

## Fixture

`fixtures/file-changes-311.md`: front matter, headings, emphasis, inline
code and a link, a numbered list, a quote, a table, a Python block and two
tasks, with a marker the test and an agent can rewrite.

## Automated checks

`file_changes` (`tests/file_change_tests.cpp`, a folder of its own), each
change written as another program writes it, its time stepped on:

- The reader shows the new text.
- Source with nothing unsaved takes it, stays clean, keeps the caret under a
  change below it and moves it with a change above; the preview shows it;
  the "Reloaded from disk" note shows; Ctrl+Z takes the reload back
  (unsaved), Ctrl+Y brings it again.
- Unsaved edits: Tinta asks and changes nothing; the dialog draws Reload
  and Keep mine inside the window in Paper and Midnight at 1050 and 650 px;
  Y, Enter and Ctrl+S just as it appears answer nothing; N keeps mine,
  unsaved, with no n typed; the next change asks again; the Reload button
  takes the disk's text, clean, and one Ctrl+Z gives mine back.
- Live with nothing unsaved takes the new text, its page in step.
- Behind another tab: a clean editor asks nothing, then takes the change
  when its tab comes back; unsaved edits bring the tab forward with the
  question; Esc keeps mine.
- A file rewritten with a byte order mark and CRLF lines reloads as any file
  opens. Esc with unsaved changes still asks Save and exit or Discard.

Neutering one part at a time: the clean editor's reload (9 failures), the
question (14), the parked tab's time (2), the key the dialog takes (2), the
grace for Y and Enter (3).

## In the built app

The isolated copy (`out/watch-311/`, Paper, 1280 x 900), the note changed
by a script while Tinta showed it, captured with PrintWindow
(`out/watch-311/shots/`):

- Reader: the marker line changed within a second.
- Source, caret on line 8: a task ticked below and the heading renamed
  above it both came in, the preview with them, the caret still after
  "this", and "Reloaded from disk — Watched note.md" faded in and out.
- A Q typed, then a change: "Changed on disk — Watched note.md" with Reload
  and Keep mine. Keep mine: the Q stayed, the tab still unsaved, the disk's
  task unticked in the buffer. The next change asked again; Reload: the
  disk's text, the tab saved; Ctrl+Z: the Q's version, unsaved again.
- Live, saved: the heading renamed outside came in with the caret kept.

## Size

`tinta.exe` grows by 4,608 bytes (3,948,032 to 3,952,640), most of it the
four strings in eight languages.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
56 of 56, `file_changes` among them.

## Limitations

- The watch still polls every 500 ms, and a program that writes a file in
  two steps (truncate, then write) may be seen half way; the next poll
  brings the rest, as in the reader.
- A clean editor reloads without asking, unlike Notepad++'s default: there
  is nothing to lose, the note shows it happened and Ctrl+Z takes it back.
- After Ctrl+/ from Source with the preview open, Live lays out the block
  with the caret at the preview's width until the caret leaves it. 4.0.1
  does the same; it is unrelated to this change and noted separately.
