# Text smoothed for the monitor the window is on

Validated on Windows with the Release build on 2026-11-11.

## What changed

Tinta draws its letters with ClearType, and ClearType wants three things
of a screen: its gamma, how much contrast to add, and the order of its
subpixels. Windows keeps these per monitor (ClearType's tuner sets them).
Tinta read them once, when it made its render target, and asked for the
primary monitor's whichever screen the window was on. A window on a second
monitor with its subpixels in another order, or tuned otherwise, drew
letters with coloured edges or the wrong weight.

- **The window's monitor.** The render target takes the values of the
  monitor the window is on (`setMonitorText`, `d2d_init.cpp`), at start
  and whenever the target is made again: after the GPU warm-up, a DPI
  change, a lost device.
- **Followed.** A moved window is asked which monitor it is on
  (`followMonitorText`, on `WM_MOVE`); on the same one nothing is read. On
  another, the target takes that monitor's values and the window is
  repainted. A change of the displays (`WM_DISPLAYCHANGE`) or of Windows'
  settings (`WM_SETTINGCHANGE`) reads the monitor's values anew and
  repaints only when they are others.
- **Unchanged.** The ClearType level stays full and the rendering mode
  natural symmetric, as before, whatever the monitor's own. A monitor
  whose values cannot be read leaves the primary monitor's. No letter
  moves: the values change how a glyph is filled, not its size.

## Fixture

`fixtures/monitor-text-smoothing.md`: words of thin upright strokes
(`Illinois lilt`, `||||`, `1111`), which show a wrong subpixel order
first, in body text, italic, bold, a link and a mark; headings with
inline code; a quote in softer ink, which shows a wrong gamma; a table
with code and emphasis in its cells; a nested list with sub- and
superscript and task boxes; a code block; Chinese, Japanese and Korean
with an emoji; a rule.

## Automated checks

**`monitor_text`** (`monitor_text_tests.cpp`, 30 checks) uses the real
render target of a test window. This PC's monitor is the real one; a
second is stood in through `monitorTextSource()`, the one place the code
learns a window's monitor and a monitor's values.

- A new window's target has the gamma, contrast and subpixel order
  `CreateMonitorRenderingParams` gives for the monitor the window is on,
  at ClearType level 1 in the natural symmetric mode; staying on the
  monitor asks for no repaint and makes no settings again.
- Moved to the second monitor (gamma 1.2, contrast 0.25, blue-green-red,
  with a level and a mode of its own that Tinta does not take): a repaint
  is wanted, the target has the second monitor's values, and further
  moves there read nothing. The fixture, laid out once, is drawn
  differently, with all its runs where they were and the page as tall.
- A second monitor that differs only in its subpixel order: a sample in
  black on white has tinted edges under both orders, and more than 99% of
  its pixels are the first monitor's with red and blue changed over. (On
  a PC whose monitor has no subpixel order, the test checks instead that
  the first has no tints and the second has.)
- The second monitor's values change: a move does not look for them, a
  settings change takes them, and reading the same values twice asks for
  one repaint only.
- The target made again on the second monitor draws for the second, not
  the primary one.
- A monitor that tells nothing, and a window on no monitor, leave the
  primary monitor's values. Back on the first monitor the fixture is
  drawn pixel for pixel as at first. A window without a target is left
  alone.

Each part of the change taken out in turn fails the test:

| Taken out                                         | Failing checks |
|---------------------------------------------------|---------------:|
| A new target takes the window's monitor           |              3 |
| The window is followed to another monitor         |             10 |
| A settings change reads the values anew           |              2 |
| The same monitor is no work                       |              5 |
| A repaint only when the values changed            |              1 |
| The fallback to the primary monitor               |              2 |
| The subpixel order is the monitor's               |              5 |
| The gamma is the monitor's                        |              3 |
| The contrast is the monitor's                     |              3 |
| The ClearType level stays full                    |              6 |
| The values reach the target                       |              8 |

The whole suite passes, 76 of 76, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which use the clipboard and
whose area this does not touch.

## Drawn

The reading view of all 150 fixtures in the ten built-in themes at two
widths, drawn with the build before and the build after on this PC's one
monitor: 8,590 of 8,590 frames are the same, pixel for pixel. On the
monitor a window was already drawn for, nothing changes.

## In the built app

`tinta.exe` in a portable folder of its own with the fixture: starts,
answers in 1.1 ms; moved 300 times about the screen through
`SetWindowPos`, it answers in 1.1 ms after; sent `WM_DISPLAYCHANGE` and
`WM_SETTINGCHANGE` fifty times each through its message queue, it answers
in 1.2 ms; closes with exit code 0.

## Speed and size

- 300 moves take 681 ms, against 634 to 672 ms for the build before: the
  time is Windows' own, and the question a move asks is one
  `MonitorFromWindow`.
- `tinta.exe` is 5,239,808 bytes, 1,024 more than before.

## Limitations

- This PC has one monitor, and its ClearType was never tuned: the second
  monitor of the test is stood in, and no window was looked at on a real
  second screen with other values. That the real `WM_MOVE` reaches the
  code is covered only by the built app taking the moves without harm.
- Whether DirectWrite gives a running program a monitor's new values
  right after the tuner was run was not tried, since that would change
  this PC's settings. If it does not, a tuning shows at the next start,
  as it did before.
- Tinta still draws with ClearType at full level whatever Windows'
  settings say, also when ClearType is turned off there. That is another
  change.
- What is drawn into a bitmap first (the link peek, Sheets cards, print
  pages, exported images) was not looked at. Where such a drawing goes
  through the window's own context it follows the monitor; where it has a
  context of its own it is as it was.
