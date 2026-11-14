# Tabs from last time (#323)

Validated on Windows with the Release build on 2026-11-14.

## What was asked

Tinta starts with the tabs it was closed with, and a file opened from
Explorer joins them. To look at one file tomorrow, every tab had to be
closed by hand today, or the old ones were there again. The request: a
setting for whether the open documents are remembered.

## What changed

**Settings → General → Reopen tabs from last time**, on as it always
was. `settings.ini` keeps it as `reopenTabs=1` or `0`; a file from before
the setting, or a value that is neither, keeps the tabs coming back.

| `reopenTabs=` | A launch with a file          | A launch with none                 |
|---------------|-------------------------------|------------------------------------|
| `1`           | the last tabs, and the file   | the last tabs                      |
| `0`           | the pinned tabs, and the file | the pinned tabs, or the start page |

- **Pinned tabs still come back.** A pinned tab stays through Close all
  but this and the sweeps to the left and right (#312); it stays through
  this one too, pinned, at the start of the row.
- **Off, the other tabs are not written.** The closing window leaves only
  its pinned tabs in `settings.ini`'s `[Session]`, and no `[Session]` at
  all when nothing is pinned. Turning the setting on later brings back
  what is closed from then on, not what was forgotten before.
- The toggle is written the moment it is made, as "Open files in tabs"
  is. The window that closes and the next one started both read the file,
  so a window opened before the toggle follows it too.
- Toggling changes nothing in the window: its tabs stay open.
- Recent files on the start page, the place each file was read to, and
  drafts of unsaved text ("Close without asking") are kept as before.
- With "Open files in tabs" off no tab came back before, and none does.

The launch's filter and the close's list moved out of `main_d2d.cpp` into
`sessionToReopen` and `sessionKeep` (`tabs.cpp`, beside the function that
seeds the row), so the test binary runs the same code the window does. A settings row now records whether
its hint was cut short (`hintTrimmed`), as it did for its label.

## Fixtures

- `fixtures/reopen-tabs-323.md`: the setting's table, a numbered list of
  what to try with bold, italic and code, links to the two files beside
  it, a wiki link and a bare link, a quote, task boxes with sub- and
  superscript, struck and marked text, and an `ini` code block.
- `fixtures/reopen-tabs-323-notes.md`: a nested list, a table with a
  centred column, a quote and a code block; the tab that is pinned.
- `fixtures/reopen-tabs-323-log.md`: a numbered list with a nested one, a
  table and a quote; the tab that is forgotten.

The test works on copies of them.

## Automated checks

**`reopen_tabs`** (`reopen_tabs_tests.cpp`, 377 checks).

- The line: on in a new setup and in a file without it; written and read
  as `0` and `1`; by hand `0`, `1` and values that are neither; the
  toggle kept alone, the session and the other settings untouched.
- What a launch brings back (`sessionToReopen`): on, every tab whose file
  is still there, the front tab followed past a file that is gone; off,
  nothing, or the pinned tabs in the row's order, a pinned path whatever
  its capitals, a pinned tab whose file is gone left out.
- What a close leaves (`sessionKeep`), from a window with three files
  between two tabs with no path: on, the three and the front one; off,
  nothing, then one and two pinned tabs with the front one marked; an
  older session in the file cleared.
- Closed and started again, the way the app does both: on, three tabs
  back with no file and four with one, the row drawing as many, each page
  laid out with its headings, table, list, quote and links; off, no
  `[Session]` and no tab's path in `settings.ini` while Recent still
  names the files, the file opened next alone, a launch with no file on
  an empty tab; off with the notes pinned, `[Session]` naming only them,
  the pinned tab back beside the file opened and still pinned, alone at a
  launch with no file; the pinned file itself opened; unpinned and
  closed, nothing left; the setting turned off in another window while
  this one is open; on again; "Open files in tabs" off.
- Settings: in eight languages at 1300, 760 and 420 the row is in the
  files and tabs group right under Open files in tabs, inside the dialog,
  its toggle the size of its neighbour's and under it, the row as tall,
  the label before the toggle, label and hint whole at 760 and wider; a
  click toggles, writes `settings.ini` and redraws the toggle, the
  dialog and its neighbours as they were; toggling leaves the open tabs.

Each part taken out in turn fails the test:

| Taken out                              | Failing checks |
|----------------------------------------|---------------:|
| settings.ini is written with the line  |             19 |
| settings.ini's line is read            |             16 |
| A value that is neither keeps the tabs |              1 |
| The toggle kept when made              |              4 |
| Start                                  | off, the other tabs stay away: 6 |
| Start                                  | off, the pinned tabs come back: 11 |
| Start                                  | a pinned path whatever its capitals: 1 |
| Start                                  | a file that is gone: 6 |
| Start                                  | the tab in front follows its path: 4 |
| Close                                  | off, the other tabs are not left: 9 |
| Close                                  | off, the pinned tabs are left: 11 |
| Close                                  | a tab with no path: 2 |
| Close                                  | the older session cleared: 11 |
| Close                                  | the pinned tabs named: 12 |
| Close                                  | the tab in front marked: 4 |
| A click in Settings toggles            |              7 |
| A click in Settings is kept at once    |              2 |
| The row on the page                    |             27 |
| The toggle drawn as the setting stands |              3 |
| The row under Open files in tabs       |             24 |
| A hint too long for its row            |              2 |
| The row in Russian                     |              3 |

The launch's and the close's own lines in `main_d2d.cpp`, each one call,
are not in the test binary; the built app below is what shows them.

`settings_layout` counts the General page's rows, five in the files group
now, and puts the page through its own checks in the eight languages at
1050 and 650. `i18n` and `pinned_tabs` pass unchanged.

The whole suite passes, 82 of 82, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which overwrite the clipboard
and whose area this does not touch.

## Looked at

Pictures from the test (`build/reopen-tabs-test-Release/frames/`): the
General page in a wide and a narrow dialog in each of the eight
languages, light and dark for English, the toggle on and off; the tab row
with the pinned tab beside the file opened. The label and the hint are
whole in the wide dialog in every language. In the narrow dialog the hint
ends in an ellipsis in German, as its neighbours' hints do there.

## In the built app

`tinta.exe` in a portable folder of its own with four copies of the
fixtures (`a` to `d`) and a `settings.ini` whose `[Session]` has `a`, `b`
and `c`, `c` in front. The window is started, left to settle and closed
through its message queue. The `[Session]` it writes says which tabs it
had; to see a start with the setting off, the line is set to `1` in the
file while the window is open, so the close writes the whole row.

| Build    | `reopenTabs=` at the start | Launch  | Pinned | The window's tabs   |
|----------|----------------------------|---------|--------|---------------------|
| 4.1.1    | `0`                        | `d.md`  | none   | a, b, c, d          |
| this one | (no line)                  | `d.md`  | none   | a, b, c, d          |
| this one | `1`                        | `d.md`  | none   | a, b, c, d          |
| this one | `1`                        | no file | none   | a, b, c             |
| this one | `0`                        | `d.md`  | none   | d                   |
| this one | `0`                        | no file | none   | the start page      |
| this one | `0`                        | `d.md`  | `b`    | b (pinned), d       |
| this one | `0`                        | no file | `b`    | b (pinned)          |

And what a close leaves with the line at `0` when the window closes:

| `reopenTabs=` at the start | Launch | Pinned | `[Session]` after the close |
|----------------------------|--------|--------|-----------------------------|
| `1`, set to `0` while open | `d.md` | none   | none                        |
| `1`, set to `0` while open | `d.md` | `b`    | b, pinned                   |
| `0`                        | `d.md` | `b`    | b, pinned                   |

The window's title named the file in front each time (`d.md`, `c.md`,
`b.md`, or none on the start page), every run closed with exit code 0,
and the four files were unchanged. 4.1.1 drops the line when it writes
`settings.ini`.

## Speed and size

- The window is there 202 to 259 ms after the launch in every run,
  whichever way the setting stands; 4.1.1 took 256 ms.
- A launch reads one more line; with the setting off it opens fewer
  files' tabs.
- `tinta.exe` is 5,262,336 bytes, 2,048 more than 4.1.1.

## Limitations

- The toggle was not clicked in the built app's own window; the test
  clicks it in the dialog as drawn, and the built app was given the line
  in `settings.ini`.
- Screen copies come back black here, so the built app's tab row was not
  looked at; its tabs are read from the session it writes.
- The session is written when the window closes. A window that is ended
  from outside leaves the session of the close before it, as it did.
- Off, a pinned tab comes back; there is no way to have none but to
  unpin it.
- Other windows already open show the toggle as it stood when they
  started, though their close follows the file.
