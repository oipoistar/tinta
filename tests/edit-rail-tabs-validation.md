# The tool rail across tabs (#317)

Validated on Windows with the Release build on 2026-11-05.

## What was reported

A clip of Live: while tabs are opened and closed, the sidebar disappears
for a moment. Frame by frame, at 1.2 s, right after the strip's + opened a
note:

- the tool rail on the left is gone;
- the tab strip starts at the window's edge.

The rail then slides back in.

## Cause

- Every entry into edit mode slid the rail in from nothing:
  `editRailAnim = 0`, then a quarter more each frame.
- Opening a note with +, closing a tab, or switching tabs in the editor
  parks the leaving tab's buffer, which leaves edit mode. Restoring the
  next tab's buffer, or starting the note, enters edit mode again. So the
  rail slid in afresh each time.
- The tab strip and the page sit beside the rail, so they jumped with it.
- Source and Live share the rail, so both showed it.

## Change

- **Each frame records the rail.** It notes whether it shows the rail
  (`App::editRailShown`). `renderEditRail` is now called on every frame,
  in reading mode too.
- **Entering edit mode slides the rail in only after a frame without it**
  (`editor.cpp`). A tab opened, closed or switched to in the editor keeps
  the rail where it is. Edit mode entered from reading, or a return to the
  editor from a reading tab, still slides it in.

## Automated checks

**`editor_features`** (`railAcrossTabs`), through the tab functions behind
the strip's +, Ctrl+Tab and Ctrl+W:

- Entering edit mode, the rail starts out of sight, slides in, then lands.
- It stays at its full width, at once and on the next frame, when:
  - a note opens from the editor;
  - Ctrl+Tab goes to another tab in the editor;
  - the active tab closes onto a tab in the editor.
- A file opened in a new tab reads. Back in the editor from it, the rail
  slides in again.

With the old reset, five of these checks fail.

## In the built app

Portable copies of both builds ran at 1000 x 800, started with `--edit` on
`fixtures/table-selection-318.md` (`out/rail-317/`):

- The strip's + opened a note, and a middle click on its tab closed it.
- After each action, frames were captured as fast as PrintWindow allows,
  about one every 31 ms.
- Each frame was read for where the rail's Bold button starts along one
  row: x = 21 with the rail in place.

Results:

- **Old code** (the installed Experimental build): the rail was caught
  mid-slide in 2 of 7 runs, at x = 9 at 61 ms and at x = 0 at 41 ms.
- **This change:** x = 21 in all 60 frames of 12 runs. The first frames
  landed 32 to 59 ms after the click.

## Size

`tinta.exe` grows by 1,024 bytes (3,984,896 to 3,985,920), with #318's
change in the same build.

## Limitations

- The captures cannot see every frame: the slide takes four paints, about
  60 ms at 60 Hz. The automated check covers it frame by frame.
- Print preview does not call the rail. Its frames leave the record as it
  was: the preview opens and closes in one mode, so this changes nothing.
