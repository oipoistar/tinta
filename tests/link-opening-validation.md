# What a click on a link opens

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code, by three reviewers independently; nobody
reported it. A click on a link in the reading view handed the address to
the shell as the document wrote it. One click on a link in a file from
someone else was enough to:

- start a program by its path, or by its bare name: the shell looked for
  `notepad.exe` in its own folder and along PATH;
- run a script or a shortcut lying beside the document, in Live and
  through a `fileref:` address;
- open a file on a server the document chose, which signs in to that
  server with the user's Windows account;
- start any registered program with an address of the document's
  choosing (`ms-settings:` and the like).

The link's text does not show where it goes. The address also went to the
ANSI call relative to Tinta's own folder, so a relative link to a PDF or
one with letters outside the code page did not open at all.

## Change

`linkOpening` (`input.cpp`, declared in `input.h`) reads an address
written in a document:

- `http:`, `https:` and `mailto:` open at once, as before;
- a path, or a `file:` address, is a file. A name alone is a file beside
  the document, never something found along PATH. It opens in its own
  application; a type Windows itself calls dangerous (`AssocIsDangerous`:
  programs, scripts, shortcuts, installers) asks first, and so does a
  file on another server than the document's own, which is not looked for
  before the user agrees;
- any other scheme is another program's own address and asks first.

Asking is the dialog a missing file's link already used, with "Open this
link?", the target in full, Open and Cancel; Enter and Esc as there. Every
route a document's link takes goes through it: the reading view, Live's
Ctrl+click and its menu, file references, and on a sheet a link card, a
file card and a link in a card's text. A file that is not there says so
in a toast.

The dialog's path line kept the start of a long path and lost its end.
It now keeps the end, the file's name, for both of its questions.

The two new texts are in English, Chinese, Japanese, Korean, German,
French, Italian and Russian.

## Fixture

`fixtures/link-opening.md`: a table with one link of each kind and what
a click on it does, the same link again in a list beside bold text and
in a quote, and a code block where the name stays text.

## Automated checks

**`link_opening`** (`link_open_tests.cpp`, in `input_tests`). The shell is
not called: the test takes what would go to it.

- The rules, for 38 addresses: web addresses in any case and with spaces
  around; six other programs' addresses; programs and scripts by path,
  beside the document and by a bare name, in nine types; documents by a
  relative path, with `%20` and a fragment, with `..`, on another drive
  and as `file:`; files on another server and on the document's own; and
  nothing to open.
- Clicks through the mouse handlers on the fixture's links, with a PDF, a
  script and a folder beside it: the web address, the PDF and the folder
  go to the shell at once, the PDF and the folder by their full paths;
  the script asks with its full path, Esc opens nothing, Enter opens it
  and nothing is created or changed; the bare program name opens nothing;
  the program, the other program's address and the file on a server each
  ask and open nothing on Esc; and a missing text file's link still asks
  to create it.
- Frames of the dialog under `build/link-opening-test-Release/frames/`,
  in Paper and Midnight at 1050 and 560 px. Looked at: Paper at both
  widths and Midnight at 560 px. At 560 px the path line ends in the
  script's name.

With nothing asking, 8 checks fail; with programs and scripts not asking,
21; with other servers not asking, 5; with other programs' addresses not
asking, 9.

`file_fragment_navigation` passes as it was: a file that vanished after
the page was laid out is still reported with its path.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
64 of 64.

## Size

`tinta.exe` is 4,027,904 bytes, 9,728 more than before.

## Limitations

- Not driven in the built window: the screen could not be copied in this
  session to find the links, and a real click would start the browser.
  The test goes through the handlers the window calls.
- What counts as a dangerous type is Windows' own list. A document type
  whose application runs what the file says (a macro, for one) opens as
  any document does.
- Links that ask now and did not before: `obsidian://`, `zotero://`,
  `vscode://` and such. One more key for those who use them; there is no
  setting to trust a scheme.
- An address in a sheet's card goes through the same rules, but was
  checked by the rules and the build only, not by a click on a sheet.
- The Russian, Chinese, Japanese and Korean texts are mine and were not
  read by a speaker.
