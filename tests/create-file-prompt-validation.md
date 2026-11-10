# Creating a missing file from its link

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code; nobody reported it. A link to a file that
is not there is drawn dimmed, and a click offers to create the file.
Choosing Create emptied the file when it was there after all. That
happens in two ways:

- The file came after the page was laid out: saved from another window or
  another editor, or brought by a sync or a `git pull`. The page is not
  laid out again for it, so the link stays dimmed.
- The link names a file on another server than the document's own. Tinta
  does not look for such a file while laying a page out, as looking signs
  in to the server, so the link always reads as missing, also when the
  file is there.

## Cause

`createRefAction` (`input.cpp`) made the file with `std::ofstream`, which
empties a file that exists. Whether the file exists was asked once, when
the page was laid out (`resolveFileRef`, `inline_style.cpp`), and not
again before the file was made.

## Change

The file is made with `CreateFileW` and `CREATE_NEW`, as the folder
panel's new file already was. That makes a file only where there is none.
When the file is there, it is opened as it is, in the reading view. A file
that was made opens in the editor, as before.

## Fixture

`fixtures/file-fragments/index.md` has the link (*Missing file*) among its
other links, a table, a quote, a list and a code block, and now says under
it how to try both cases by hand.

## Automated checks

**`file_fragment_navigation`** (`file_fragment_tests.cpp`), through the
mouse handlers and `keystroke.h`:

- a click on the link opens the prompt; a `not-created.md` with words in
  it is then written beside the fixture, as another program would; Enter
  closes the prompt, the file holds its words, and it is the document
  shown, to be read;
- with the file gone again, the click opens the prompt again; Enter makes
  the file, empty, and it opens to be typed in.

The test removes the file both times, so the fixture folder is left as it
was. With the file made over an existing one again (`CREATE_ALWAYS`), 2
checks fail: the file's words, and that it opens to be read.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
61 of 61.

## Size

`tinta.exe` is 4,017,152 bytes, as before the change.

## Limitations

- Not driven in the built window: the screen could not be copied in this
  session, so there was no telling where the link is to click it. The test
  goes through the same handlers the window calls.
- The other-server case was not tried against a real share. It takes the
  same path through `createRefAction`: the link reads as missing, and the
  file is now opened and not emptied.
- A file that comes between the check and the opening is still opened,
  whatever it holds; nothing is written to it.
