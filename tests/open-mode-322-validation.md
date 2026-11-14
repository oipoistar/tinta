# The view a file opens in (#322)

Validated on Windows with the Release build on 2026-11-14.

## What was asked

Tinta opens every file in the reading view. For writing, each file then
needs `E` before a letter can be typed. The request: a setting for the
view files open in, Reading (the default), Live or Source, in Settings →
Editor and in `settings.ini`.

## What changed

**Settings → Editor → Open Markdown files in** has three segments:
Reading, Live, Source. `settings.ini` keeps the choice by name, as
`openMode=reading`, `live` or `source`; a line by hand may have capitals,
and a value that is none of the three is the reading view. The choice is
written the moment it is picked, so the next window started reads it.

With Live or Source, a Markdown file that is opened comes up in the
editor (`applyOpenMode` in `editor.cpp`):

| Opened by                                   | With Live or Source      |
|---------------------------------------------|--------------------------|
| Explorer or the command line, a new window  | the editor               |
| the same, handed to a window already open   | the editor, in its tab   |
| `Ctrl+O`, a recent file, a file dropped     | the editor, in its tab   |
| the folder browser's row and its path field | the editor, in this tab  |
| a link to another file, a wiki link         | the editor               |
| the tabs a session comes back with          | the editor, when shown   |
| a find result                               | the reading view         |
| Back and Forward                            | the reading view         |
| a closed tab reopened (`Ctrl+Shift+T`)      | as it was closed         |

- A tab is placed once, when it is first shown. After that it stays as
  the reader leaves it: `Esc` returns to the reading view, and coming
  back to the tab finds it there.
- The editor opens at the place the file was left at, as `E` opens it at
  the place being read (#77): the page is laid out as far as that place
  first, and `E`'s own way from a place on the page to its line of the
  source does the rest. A link to a heading lands on the heading.
- The rail's Source | Live pill is put where the file opens. The pill
  still switches the editor; the next file opened follows the setting.
- A find result opens to show its match, with the results beside it, so
  it is read. Back and Forward return to a place that was being read.
- Files that are their own source (`.mmd`, `.puml`, text files), sheets,
  and a file that is gone from the disk open as they always did.
- At launch the file is put in its view before the window's first frame.

The editor has no side panels, so a file picked in the folder browser
closes the browser when it opens in the editor, as `E` does.

## Fixtures

- `fixtures/open-mode-322.md`: the setting's table, a numbered list of
  what to try with bold, italic and code, a link to another file, a link
  to a heading of it and a wiki link, a quote, task boxes with sub- and
  superscript, a mark, struck text and a link, and a code block.
- `fixtures/open-mode-322-notes.md`: the second file those links open,
  with two headings, a nested list, a table, a quote and a code block.

The test works on copies of them, and writes a diagram, a PlantUML file
and two text files beside the copies.

## Automated checks

**`open_mode`** (`open_mode_tests.cpp`, 532 checks).

- The name: each view's name and back, capitals and spaces by hand, a
  wrong name; `settings.ini` written, read, changed by hand; the choice
  kept alone when picked, other settings untouched.
- A file opened in a tab with each choice: the view it is in, the editor
  holding the file unchanged, a letter typed landing in the text and
  `Ctrl+Z` taking it back, `Esc` showing the page, the file as it was.
- The pill: taken to the other side, the next file still opens by the
  setting, the knob in its place, the pill kept as it then stands.
- Tabs as left: back on a tab left in the reading view it is read; a tab
  left in Live is in Live; a path open already keeps its view; a file
  opened behind the tab in front waits, is placed when first shown, and
  only once; a waiting tab opens by the setting as it is when shown.
- Other kinds: `.mmd`, `.puml`, `.txt`, `.json` and a sheet with Live and
  with Source; `.markdown`; a file deleted before its tab was shown.
- In place: the folder browser's row (clicked) with Reading and with
  Live, its path field with Source, a wiki link (clicked) with Source,
  and Back after it.
- A link to a heading of another file (clicked), with Live and Source:
  the note joins as a tab in the editor, the caret on the heading.
- A find result with Live: read, the find bar open on its match; its tab
  stays in the reading view; the next file opens in Live.
- From the start page: the file takes the empty tab's place, in Live.
- A closed tab reopened, read and edited; a session's three tabs.
- The place: a file of a thousand lines left far down opens with the
  caret on the line `E` puts it on, the source scrolled as `E` scrolls
  it, the Live page at the place; with no place kept, at the top.
- Settings: in eight languages at 1300, 760 and 420 the row is the first
  of the page, inside the dialog, its three segments in order, none over
  another, each as wide as its word, the label before the control or
  over it; a click picks, writes `settings.ini` and draws the segment
  picked; picking leaves the open file as it is.

Each part taken out in turn fails the test:

| Taken out                                   | Failing checks |
|---------------------------------------------|---------------:|
| `settings.ini` is written with the line     |              7 |
| `settings.ini`'s line is read               |              5 |
| A name by hand, with capitals               |              2 |
| The choice kept when picked                 |              3 |
| Source as well as Live                      |             19 |
| Files that are their own source, and sheets |             10 |
| A file that is gone                         |              3 |
| The pill put where the file opens           |             17 |
| The pill's knob in its place                |              1 |
| The pill kept as it stands                  |              1 |
| The place the file was left at              |              7 |
| A new tab waits to be placed                |             47 |
| A tab placed when first shown               |             49 |
| A tab placed once only                      |              7 |
| A session's tabs behind                     |              4 |
| The folder browser's row                    |              3 |
| The folder browser's path field             |              1 |
| A wiki link                                 |              1 |
| A find result stays on the page             |              3 |
| The hold on the setting let go after it     |             21 |
| A click in Settings picks                   |              8 |
| A click in Settings is kept at once         |              2 |
| The picked segment drawn                    |              3 |
| The row's width from its words              |             27 |
| The row on the page                         |             28 |

The launch's own two lines (the setting read, and the file placed
before the first frame) are not in the test binary; the built app
below is what shows them.

`settings_layout` counts the Editor page's rows and has seven now. It
puts the page through its own checks in the eight languages at 1050 and
650: no label is cut short. `i18n` has the five new texts in every
language.

The whole suite passes, 81 of 81, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which overwrite the clipboard
and whose area this does not touch.

## Looked at

Pictures from the test (`build/open-mode-test-Release/frames/`): the
Editor page in a wide and a narrow dialog in each of the eight
languages, light and dark for English; a file opened in Live and in
Source; the long file opened in Live far down. In all eight languages the
row's label and hint are whole at both widths; in the narrow dialog the
segments stand under the label. The Russian hint was cut short in the
wide dialog and was reworded.

## In the built app

`tinta.exe` in a portable folder of its own with the fixtures and a
`settings.ini`, driven through its message queue: an `x`, then `Ctrl+S`.
The saved file tells whether the file had opened in the editor.

| Build    | `openMode=` | Opened by                | The file after      |
|----------|-------------|--------------------------|---------------------|
| before   | `live`      | the command line         | as it was           |
| this one | (no line)   | the command line         | as it was           |
| this one | `reading`   | the command line         | as it was           |
| this one | `live`      | the command line         | an `x` at its start |
| this one | `source`    | the command line         | an `x` at its start |
| this one | `live`      | the session, no file     | an `x` at its start |
| this one | `source`    | a second launch's handing over | an `x` at its start |
| this one | `reading`   | a second launch's handing over | as it was     |

The other file in the folder was unchanged every time, and every run
closed with exit code 0. A build from before this change drops the line
when it writes `settings.ini`.

## Speed and size

- A file of a thousand lines left 4200 px down opens in the editor at
  that place in 38 ms in Live and 41 ms in Source; opened to be read,
  with its first frame laid out, it takes 33 ms.
- The window is there 207 to 242 ms after the launch in every run, the
  editor's or the reading view's.
- A file that opens to be read costs one comparison more.
- `tinta.exe` is 5,260,288 bytes, 4,096 more than before.

## Limitations

- The launch's first frame was not looked at: screen copies come back
  black here. That the file is in the editor before that frame is read
  from the code's order, and from the typed letter landing.
- `Ctrl+O`'s dialog and a file dropped on the window were not driven;
  both open their file through the tab path the test takes.
- The folder browser closes when its file opens in the editor, pinned or
  not: the editor has no side panels (#156).
- Source without the preview pane lays the page out as far as the place
  the file was left at, once, to find its line; a file with no place
  kept is not laid out.
- A window already open keeps its choice until it is changed there;
  another window reads `settings.ini` when it starts.
- A new file (`N`, `Ctrl+N`) opens in the editor as before, in the view
  the pill was left in.
