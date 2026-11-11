# The interface's surfaces on its scale, and the tab row's edges

Validated on Windows with the Release build on 2026-11-11.

## What changed

After Settings (`settings-scale-validation.md`), the other surfaces were
moved onto the scale of `include/ui_scale.h`, and the top of the window got
the edges it lacked.

- **Shared pieces** (`ui_paint.h`): a line of text in a type role, a
  section's label, the switch, a dialog's close cross, a menu's panel, row
  and separator. Settings draws with them too.
- **Theme chooser.** A card is 72 high whatever the number of themes; it
  shrank to fit, so that with 18 light themes in an 800-high window a card
  was 26 high and each name lay over its sample. The two columns scroll
  with the wheel between the header and the footer; the shorter column
  stops at its own last card; the dialog opens with the theme in use in
  sight; a long name ends before the chosen card's check; the wheel takes
  back a theme shown by resting on a card.
- **Menus.** The reading view's, the application's, a tab's and the two
  editors' share rows of 32 (they had 30 and 34), a padding of 4, a
  separator's room of 8, corners of 8 and of 4 for a lit row, the theme's
  raised surface with its shadow, labels in Body and shortcuts in Small at
  the secondary ink. In the Source editor's menu Markdown's own marks stay
  in the code face.
- **Side panels.** Contents, the folder browser and the search results
  have a list's rows (28; they had 24 and 26) and footers. The small
  format they share with the notices and the prompts is the scale's Micro
  (11, was 10), and their strong format is semibold, not bold.
- **Shortcut list.** Its five groups stand in two columns where the window
  is wide enough (one column of some sixty rows, 520 wide, before), in rows
  of 24, a key in Strong and what it does in Small; the title at the left
  in the scale's Title with the version beside it.
- **The editors' dialogs and the start page.** The shortcut editor's and
  the theme editor's titles are 20 semibold (28 light before), the
  shortcut editor's rows a menu's row apart; the start page's text at
  11.5, 12.5, 13.5 and 14 is at 12 and 13.
- **Front matter page.** Its column labels (9) and a key's kind (8) are at
  11 and at the secondary ink.
- **The tab row and the window's edge.**
  - The window's border is a hairline of the theme's own: the row's colour
    18% towards its ink. It was the row's own colour, so a dark window on
    a dark desktop had no edge.
  - The same hairline parts the row from the page and is open where the
    chosen tab meets the page. The row is 6% under the page in a light
    theme (4.5% before).
  - The chosen tab has an edge of that colour in place of its shadow.
  - A thin line stands between two tabs at rest, none beside the chosen
    tab or one under the pointer.
  - Every name, the +, the window's buttons, the pin and the icon are
    centred on one line (22 under the window's top). They stood on three:
    the buttons and the + at 20, the chosen tab's name at 23, a name at
    rest at 25, so that a name dropped by 2 when its tab was left.

## Fixture

`fixtures/ui-surfaces/surfaces.md`: what to open and what to look at, over
a page with front matter, four levels of headings, a table, lists, a
quote, code, a link and marked words. `fixtures/ui-surfaces/themes.ini`:
eighteen more themes (fifteen light, three dark, one with a long name), so
that the theme chooser has more cards than room.

## Automated checks

**`ui_surfaces`** (`ui_surfaces_tests.cpp`, 107 checks) draws each surface
through the window's own context and reads where its parts went or what
colour a pixel is.

- Theme chooser, with 10 and with 28 themes, at 1050 and 650 wide, in a
  short window, light and dark: every card 72 high; two columns inside
  their room; no card over another; header, labels and footer on the
  scale's steps; the dialog keeps to the window. With ten themes nothing
  scrolls. With 28: a notch of the wheel moves the columns by a card and
  its gap; the header and the footer stay; at the end each column's last
  card is whole at the foot of the room; a scrolled-out card cannot be
  clicked and a scrolled-in one picks its theme; resting on a card shows
  its theme and the wheel takes it back; opened again the theme in use is
  in sight; a long name ends before the check; the switch and the close
  cross work.
- Menus: the scale's numbers (32, 28, 4, 8, 24); the reading view's menu
  is as tall as its rows, a lit row's light stops a padding short of the
  edge, in a short window the rows give way; the application's menu has
  the same rows; a tab's menu has rows of that height and that padding;
  the Source editor's menu and its submenu too, the submenu's rows one on
  the other.
- Side panels: the shared formats are Body, Small, Strong (semibold) and
  Micro; the contents panel has a list's row for every heading; the
  browser's rows and footer are a list's row high.
- Shortcut list: every row 24; two columns at 1050, a dialog's padding
  inside; no two rows share a place; the wheel scrolls under the title; in
  a tall window the dialog is as tall as its columns; one column at 700.
- Dialogs: the title format is 20 semibold; the shortcut editor's rows;
  Settings' New opens the theme editor; the start page draws.
- Tab row, in Paper and Midnight: the hairline under the row in the
  edge's colour, open under the chosen tab; the edge stands out from the
  row; the chosen tab is the page's colour inside an edge and casts no
  shadow; a name stands at the same height chosen or not, on the row's
  centre line, and so do the + and the window's close cross; a line
  between two tabs at rest, none beside the chosen tab or a hovered one.
  In all ten themes the window's border is the edge's colour and not the
  row's, and in the light ones the row is a clear step under the page.

Each part taken out in turn fails the test:

| Taken out                                           | Failing checks |
|-----------------------------------------------------|---------------:|
| Chooser: a card is 72 (40)                          |             18 |
| Chooser: the wheel scrolls                          |              5 |
| Chooser: scrolled-out cards cannot be clicked       |              5 |
| Chooser: opens with the theme in use in sight       |              2 |
| Chooser: the shorter column stops at its end        |              1 |
| Chooser: the wheel takes the shown theme back       |              1 |
| Chooser: a long name ends before the check          |              1 |
| Menus: rows of 32 (30)                              |              1 |
| Menus: a padding of 4 (6)                           |              1 |
| Menus: a lit row stops short of the edge            |              1 |
| Menus: the Source submenu's rows touch              |              1 |
| Panels: rows of 28 (26)                             |              1 |
| Panels: small text at 11 (10)                       |              1 |
| Panels: semibold for the row in use (bold)          |              1 |
| Help: two columns                                   |              3 |
| Help: rows of 24 (22)                               |              1 |
| Help: as tall as its columns                        |              1 |
| Dialogs: titles at 20 semibold                      |              1 |
| Tab row: the window's border is the edge            |              1 |
| Tab row: the line under the row                     |              2 |
| Tab row: the chosen tab's edge                      |              2 |
| Tab row: a name at rest on the same line            |              2 |
| Tab row: the + on the line                          |              2 |
| Tab row: the window's buttons on the line           |              2 |
| Tab row: lines between tabs at rest                 |              2 |
| Tab row: none beside the chosen tab                 |              4 |
| Tab row: the row 6% under the page                  |              1 |
| Tab row: the edge 18% towards the ink               |              3 |

Four of these were not caught at first (the chooser's opening place and
long name, the menus' padding, the panels' rows); the checks were made
sharper until they were.

Three existing tests changed with the look they pinned: `find_any_case`
measured a contents row as 26, `inline_code_theme` held the row's colour
at 4.5% under the page, and `tab_hover_card` looked for the chosen end
tab's shadow on its outer side (#271) and looks for its edge there now.
That last check is weaker than it was: an edge lies inside its tab, where
the row's clip cannot cut it.

The whole suite passes, 78 of 78, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which use the clipboard.

## Drawn

- `build/ui-surfaces-test-Release/frames/`: the chooser with 10 and 28
  themes, scrolled, reopened, with the long name and at 650 in Midnight;
  the reading view's, the application's, a tab's and the Source editor's
  menus; contents and the browser; the shortcut list in two columns and
  in one; the two editors; the start page; the tab row in Paper and
  Midnight. Looked at: the chooser with 28 themes, scrolled to its end,
  with the long name and at 650 in Midnight; the reading view's and a
  tab's menu and the Source editor's with its submenu; the shortcut list
  in two columns and in one; the two editors; the start page. Not looked
  at from this folder: the chooser with ten themes and reopened, the
  application's menu.
- The theme test's frames (`build/theme-test-Release/chrome-frames/`):
  contents, the browser, the prompt, and the tab row before and after in
  Paper and Midnight, enlarged twice; the results panel from the find
  test's frames.
- Page frames, 150 fixtures. With the surfaces moved and the tab row as
  it was: the reading view's 8,590 frames pixel for pixel as before, and
  2,394 of 2,400 Source and Live frames (the six others are the first
  document's start-up frames, which differ between any two runs). With
  the tab row's change every frame differs, as the row is in each; in the
  reading view all 8,590 have every run of text in its place. The editors'
  frames carry no such measure and were not compared further.

## In the built app

`tinta.exe` in a portable folder of its own with the fixture and its
themes, driven through its message queue: `T` opens the chooser, which is
788 high for 20 rows of cards; forty notches of the wheel scroll it to
its end; a click on the last light card picks that theme
(`themeName=Chalk` in the settings.ini written at exit). `F1` opens the
shortcut list, the wheel scrolls it, `Esc` closes it. The app answered
within 1 ms after each step and closed with exit code 0. Run before and
after the tab row's change.

## Speed and size

- `tinta.exe` is 5,254,656 bytes: 1,536 more than with Settings alone.
- Nothing here is on the page's own path; frame times were not measured.

## Limitations

- The real window was not looked at: screen copies come back black here.
  The window's border is set through DWM and was checked as the colour
  handed to it, not as pixels; the rounded corners and how the border
  looks against other windows are Windows' own drawing.
- The find bar, the notices and prompts, the tab switcher and hover card,
  the print preview, the Live editor's format bar and tip, the Sheets
  toolbar and the edit rail were not moved: they keep their own lengths
  (the notices and prompts took the small format's step to 11).
- The reading view's menu still makes its rows lower in a window too short
  for it, where the chooser and Settings scroll.
- Inside the theme editor and the front matter page only the type moved;
  their rows and gaps are their own. On the front matter page the labels
  SHOW and SIDE stand close in English.
- The pictures are from the test harness, which draws each surface with
  the app's own code.
