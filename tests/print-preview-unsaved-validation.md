# Unsaved edits under the print preview

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code; nobody reported it. With a document edited
and not saved, Ctrl+P opens the print preview of it as edited. While the
preview showed:

- closing the window (Alt+F4, the taskbar's Close window) closed it at
  once, without the unsaved-changes dialog. The edits were gone, and so
  was their recovery draft;
- the draft sweep, every 30 seconds, deleted the edits' draft, so a crash
  then had nothing to offer back either;
- a change of the file by another program was taken without the Reload or
  Keep mine prompt;
- another document opened into the window (a second launch, a dropped
  file) left the tab marked as not edited, with the first document's text
  still in the editor: Esc and Ctrl+S could then write it into the other
  file.

## Cause

The preview lays the page out with the editor put aside:
`enterPrintLayout` (`print.cpp`) sets `app.editMode` to false and keeps
the real mode in `app.printSaved` until the preview closes. Eleven places
asked whether the active document has unsaved edits with
`app.editMode && app.editorDirty`, and got "no" for as long as a preview
showed.

## Change

- `App::editingNow()` and `App::unsavedNow()` (`app.h`) answer for the
  document, through a preview. The eleven places ask them: the draft
  sweep, the window's and a tab's closing, the file watch, the tab strip's
  dot and star, the hover card, a save that finds the file open in
  another tab, and a window dropped on another's tabs.
- The preview is closed first where its document is left or a dialog has
  to ask over it: when the window is asked to close, when a tab closes,
  when the file watch has to ask, and when the editor's buffer is parked
  for another document.
- The window's check moved from `WndProc` into `windowCloseAsks`
  (`tabs.cpp`), unchanged but for this, so a test can call it.

## Fixture

`fixtures/print-preview-unsaved.md`: the steps to try by hand, as a
numbered list, with a table of what is asked and what happens, a quote
and a code block.

## Automated checks

**`print_preview_unsaved`** (`preview_unsaved_tests.cpp`, in
`input_tests`), a portable copy with its own drafts folder. Six letters
are typed through `keystroke.h` and not saved, and the preview opened
over them. Then:

- the document reads as having unsaved edits, and the draft sweep leaves
  one draft;
- the window asked to close: it asks, the preview is closed, the editor
  has its text, the dialog is open for the window, and Keep editing keeps
  everything;
- the tab asked to close: the same dialog, for the tab;
- the file written by another program, then the file watch: Reload or
  Keep mine is asked, and the edits are still in the editor;
- another document opened: it gets a tab, the preview closes, the first
  tab keeps its text marked unsaved, the other file is untouched, and the
  text is back in the editor when its tab is chosen;
- a preview closed by itself gives the editor back, and with nothing
  unsaved the window closes without asking.

Asked by `editMode` alone again, 4 checks fail. With the preview also left
open where it is now closed, as the code was, 13 fail.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
62 of 62.

## In the built app

An isolated portable copy with the fixture (`out/audit/app-preview/`,
ignored): E, six letters, Ctrl+P, and `WM_CLOSE` posted to the window, as
Alt+F4 and the taskbar post it.

| Build | After `WM_CLOSE` |
|:------|:-----------------|
| v4.0.2 | the window is gone; no draft is left; the file has no letters |
| This one | the window stays, titled `Tinta - * print-preview-unsaved.md` |

The window's pixels could not be looked at in this session (copies of the
screen came back black), so the dialog was not seen; the title's star
and the process still running were.

## Size

`tinta.exe` is 4,017,152 bytes, as before the change.

## Limitations

- Printing, PDF export and the link's hover preview enter the same print
  layout without a preview. They run from start to end with no message
  handled in between, so nothing can ask about the document meanwhile;
  they were left as they are.
- Not tried in the window: the tab's close, the change on disk and the
  dropped file. The test calls the functions the window calls for them.
