# Settings on the interface's scale

Validated on Windows with the Release build on 2026-11-11.

## What changed

An audit of the interface found no scale behind it: at least 14 type
sizes, ten of them between 9.5 and 14, 138 different lengths, rows of seven
heights for the same job and corners of nine radii. Settings showed most of
it at once. Every setting was a card of its own in one flat list, about 68
units a setting; a setting's name and its description were one size and
differed in strength alone; related settings stood apart (Follow Windows
theme and Themes had Table of contents between them); and a page taller
than the window ran out of the dialog, where its last rows could not be
reached. The Appearance page did not fit a 1080p screen at 150%.

- **The scale** (`include/ui_scale.h`). Spacings on a base of 4 (2, 4, 8,
  12, 16, 24, 32, 48); rows of 28 for a list, 32 for a menu, 40 for a row
  with a control, 52 for a row with a description; corners of 4 for
  controls, 8 for cards and popups, 12 for dialogs; three strengths of ink;
  and six type roles in Segoe UI at two weights: Micro 11, Small 12, Body
  13, Strong 13, Subtitle 16, Title 20.
- **Groups.** A page is two or three groups under small upper-case labels
  (General: Interface, Files and tabs, Configuration files; Appearance:
  Reading, Theme, Window; Editor: Editing, External tools). A group is one
  card whose rows are parted by hairlines.
- **Three levels in a row's text.** The name in Body, its description in
  Small and softer, the group's label in Micro; the dialog's title in
  Title, where it was 28 light. The chosen section is in Strong.
- **Ink.** Descriptions, labels and the footer are drawn at 0.72 of the
  text colour, where they had 0.5 and 0.4: 4.5 to 1 or more on a card in
  all ten built-in themes.
- **As tall as its page.** The dialog's height comes from its page's rows,
  no longer from a number per section. In a window too short for the page,
  the page scrolls between the header and the footer with the wheel; what
  scrolled out of its room is clipped and cannot be clicked, a thumb at
  the right shows the place, and another section opens at its top.
- **Narrow.** Under 600 units of dialog (a window under 680) the sections
  go across the top and the rows take the whole width. A control that
  would leave its label less than 120 units moves under the label.
- **Controls.** Switches, buttons, dropdowns, their lists and the
  segmented switches are drawn from the scale: buttons 24 high, boxes 28,
  a list's rows 28. The two dropdown lists are one piece of code now.
- **Fonts after a language switch.** Text formats made once and kept
  (`paint::textFormat`) take the interface language's CJK faces when the
  language changes; before, a format made in English kept the system's
  fallback after Chinese was picked (#254 for these formats).

The front matter page keeps its own layout inside the new frame.

## Fixture

`fixtures/settings-scale.md`: what to look at in the dialog, over a page
with headings, a numbered and a task list, a table, a quote, a code block,
a link, marked words, inline code, sub- and superscript, so that the
dialog's shade and cards are seen against real content.

## Automated checks

**`settings_layout`** (`settings_layout_tests.cpp`, 1,023 checks) draws the
real dialog through the window's own context and reads where its parts
went (`settingsRows`, `settingsCards`, `settingsGroupLabels`, the hit
boxes).

- The scale: six roles at 11, 12, 13, 13, 16 and 20 in two weights, none
  under 11, Body and Strong apart in weight alone; spacings and rows steps
  of 4; a two-line row is its lines and paddings.
- Every page (3) in every language (8: English, German, French, Italian,
  Russian, Chinese, Japanese, Korean, each picked through the dropdown) at
  1050 and 650 wide, in Paper and Midnight, 96 dialogs: the page's
  settings stand in its groups; a group's rows fill its card; every row
  is one of four heights; rows, cards and labels stand on the base of 4
  with a section's gap between groups; a control stands clear of its label
  and inside its row; a label keeps 120 units; everything that can be
  clicked is inside the dialog and no two controls share a place; the
  dialog is as tall as its page. No setting's name is cut short in any
  language at either width.
- Hierarchy: the rail at 1050; labels and names on one left edge; Follow
  Windows theme and Themes neighbours in one group; a slider's row; the
  Appearance page no taller than as eight cards.
- Ink: descriptions at 4.5 to 1 or more on the card's ground, read from
  the drawn pixels, in all ten built-in themes; the card stands out from
  the dialog.
- A 560-high window: the page has more than fits; nothing outside its room
  can be clicked; the last setting is out of reach, the header's controls
  are in reach; a notch of the wheel (through `handleMouseWheel`) moves
  the page by a row; it stops with the last row at the foot of its room;
  the header is not drawn over; the last switch works there and the first
  setting is out of reach; the wheel back stops at the top; a window grown
  under a scrolled page shows the page from its top; another section opens
  at its top; a page that fits does not move.
- Narrow: at 650 the sections are across the top, the cards take the
  width and no control has moved; at 430 the theme buttons are under
  their label, on its left edge and inside their row, and a switch stays
  beside its label.
- Dropdowns: a box is 28 high, the language list's rows 28 and inside the
  dialog; the box closes its list; the other dropdown opens its own and
  its row picks the profile.
- Fonts: in Chinese, then Japanese, the dialog's formats fall back to that
  language's faces, and in German to the system's again.
- The front matter page draws in the frame, and the wheel there is its
  list's.

Each part of the change taken out in turn fails the test:

| Taken out                                            | Failing checks |
|------------------------------------------------------|---------------:|
| A two-line row is 52 (58, as the old cards)          |            101 |
| Groups a section's gap apart (10)                    |            194 |
| The dialog's height from its page                    |             97 |
| A label ends before its control                      |             97 |
| A wide control goes under its label                  |              4 |
| The sections across the top of a narrow dialog       |              7 |
| The wheel scrolls the page                           |              7 |
| What scrolled away cannot be clicked                 |              3 |
| Scrolled rows are clipped under the header           |              1 |
| The scroll is kept within the page                   |              1 |
| Another section opens at its top                     |              1 |
| Descriptions at 0.72 of the ink (0.5)                |              1 |
| Descriptions a size under the names                  |              1 |
| A dropdown's list has a list's rows                  |              1 |
| Kept formats follow the interface language           |              2 |
| Themes in Follow Windows theme's group               |             34 |

The whole suite passes, 77 of 77, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which use the clipboard. No
existing test had to change. `notification_setting` flips a switch of this
dialog, so it was run on its own and passes; the clipboard's text was
taken before and put back after.

## Drawn

- `build/settings-layout-test-Release/frames/`: 58 pictures, every page in
  every language at both widths in Paper, the English pages in Midnight
  too, the scrolled page, the dialog at 430, the open language list and
  the front matter page. Looked at: Appearance in English at 1050,
  General in Midnight, Appearance in Russian at 650, the Editor page in
  German, the dialog at 430, the scrolled page, the language list, the
  front matter page.
- Nothing else moved. Reading view, 150 fixtures in ten themes at two
  widths: 8,590 of 8,590 frames as before. Source and Live: 2,394 of
  2,400; the six others are the start-up frames of the first document,
  which differ between any two runs of one build. (A first comparison
  showed 29: its baseline had been swept while a build was running. Two
  baselines swept on a quiet machine from the sources before the change
  settle it.)

## In the built app

`tinta.exe` in a portable folder of its own with the fixture, driven
through its message queue: Ctrl+, opens the dialog (Ctrl set in the
window's own input state, not on the PC's keyboard); a click on Focus path
on open flips it; in a window 620 high the Appearance page is taller than
its room by 160, twelve notches of the wheel scroll it, and a click on the
last row flips Show notifications. The settings.ini written at exit holds
`browserFocusPath=1` and `showNotifications=0` and the other switches as
they were. The app answered within 1 ms after each step and closed with
exit code 0.

## Speed and size

- A frame of the dialog takes 3.4 to 3.9 ms on the test's software target,
  of which working out the dialog's size is 0.09 ms. The dialog before was
  not timed.
- `tinta.exe` is 5,253,120 bytes, 13,312 more than before.

## Limitations

- Only Settings is on the scale. The side panels, both menus, the theme
  chooser, the theme and shortcut editors, help, find, the start page and
  the front matter page still use their own sizes; next to them the dialog
  now looks a little different.
- The dialog has no keyboard access to its controls, as before.
- The seven group labels are new strings. The German, French, Italian,
  Russian, Chinese, Japanese and Korean ones are mine and were not read by
  a native speaker.
- In a dialog narrower than about 350 units the four section names are cut
  short with an ellipsis.
- The screen was not copied: the pictures are from the test harness, which
  draws the dialog with the app's own code over an empty ground.
- `notification_setting` leaves the clipboard with text only: a copied
  picture's or a rich text's other formats would not have come back.
