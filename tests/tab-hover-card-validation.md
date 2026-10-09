# Tab hover card (#246)

Validated on Windows with the Release build on 2026-10-09. Suggested by Alex
(@Lex987) in #246, whose pull request showed a cut-off tab title in a tooltip.

## Problem

The tab strip cuts a long file name short with an ellipsis, so the rest of the
name can't be read anywhere. Two files with the same name in different folders,
such as two `README.md`, give identical tabs. The orange and red-grey dots on a
tab are not explained anywhere either.

## Change

Resting the pointer on a tab for 600 ms brings up a card below the strip:

- the tab's whole name, semibold, wrapped onto up to four lines;
- the folder the file is in, muted, breaking after separators, up to four
  lines;
- for a tab with a dot, the dot again with *Unsaved changes* or *Moved or
  deleted on disk*, translated like the rest of the interface.

A tab without a file, such as a quick note, gets a card only when the strip cut
its title short or it has unsaved changes. Once a card has been up, the next
tab's card follows after 60 ms instead of 600 ms for 0.8 s, so the pointer can
glide along the strip.

A click, middle click, right click, key press, wheel turn or focus loss puts
the card away, and no card comes back until the pointer moves to another tab.
Moving off the tab hides the card. The pointer leaving the client area,
including into the caption gaps between tabs, now also clears the strip's hover,
so no tab stays highlighted. The card keeps 8 px inside the window and holds at
most 420 px of text. It doesn't show over menus, dialogs, the open-files list,
the lightbox, print preview or during a tab drag. It is opaque, on the strip's
own surface, with a border and a soft shadow.

Unlike the tooltip in #246, the card always shows for a file's tab, because the
folder line is new information. It wraps a long name instead of cutting it at
460 px, explains the dots, and lets the pointer glide between tabs.

## Fixture

`fixtures/tab-hover-card/` holds three files:

- `README.md` mixes a heading, a table with code, bold and italics, a list, a
  quote, a code block and a link;
- `notes/README.md` has the same name one folder down, with emphasis, an
  ordered list and a link back;
- a 100-character file name with a table and a quote.

## Automated checks

The new `tab_hover_card` suite (`--tab-hover-card-tests`) opens the three
fixtures and a Chinese-named copy as four tabs. In Paper and Midnight, at 100%
and 150% scale, in 650 and 1050 px windows, it checks for every tab that:

- hovering arms the dwell, with no card before it and a card after it;
- the card sits below the strip and inside the window, under its tab unless the
  window edge pushes it left, and within its maximum width;
- the strip can't hold the long name, and its card is taller than
  `README.md`'s because it wraps;
- moving within the tab keeps the card.

It then checks that:

- gliding to a neighbour hides the old card at once and arms a quick one;
- leaving the client area clears the card and the strip's hover;
- a key press, a wheel turn and a click each put the card away, and the tab
  stays quiet until another tab is hovered;
- a dwell that ends after the pointer moved on shows nothing;
- no card shows over the open-files list or once the pointer is off the tab;
- a clean untitled tab whose title fits has no card, but one with unsaved
  changes does;
- the status texts match the dots.

Disabling each piece in turn fails the suite:

| Disabled | Failed checks |
| --- | --- |
| click dismissal | 2 |
| hovered-tab check when the dwell ends | 1 |
| wrapping | 8 |
| pointer-on-tab check when drawing | 1 |
| quiet tab after a dismissal | 2 |
| hover reset on leave | 1 |
| overlay guard | 1 |
| untitled-tab rule | 1 |

Writing the suite found a bug in the first draft: moving off a tab while its
card was up left the card's state behind.

The full suite passed 34 of 34. In the first full run `editor_context_selection`
failed once, like the known first-run flake, and it passed in three later runs.

## Desktop check

A portable copy of the built `tinta.exe` opened the three fixtures as tabs, and
the real pointer was moved over each tab. The captures are in
`out/issue-246/card/` (ignored).

- Paper, 1000 px window: the first `README.md` card ends in `...\docs`, the
  second in `...\docs\notes`, and the long name wraps onto two lines.
- Midnight, 560 px window: the tabs shrink to "READ..." and "a-v...", and the
  long name's card moves left to stay inside the window.
- Paper with the Chinese interface, after deleting `notes/README.md` on disk:
  the card shows the red-grey dot and 已在磁盘上移动或删除 in the interface
  font.

Posted pointer messages alone don't show the card: the real pointer is
elsewhere, so the window gets an immediate mouse-leave. Simulated keystrokes
didn't reach the window, so the unsaved-changes card was checked by the
automated suite only.

## Limits

- The dwell is fixed at 600 ms, with no setting.
- A name or folder longer than four lines ends in an ellipsis.
- A single-document window has no tab row, and its title in the caption has no
  card.
