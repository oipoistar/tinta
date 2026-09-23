# Tab title tooltip validation

Long file names are ellipsized in the tab strip, so hovering a tab reveals
the whole name on a floating card below the strip after a short dwell.

## Fixture

- `tests/fixtures/tab-tooltip/tab-tooltip.md` — short-named document
- `tests/fixtures/tab-tooltip/a-very-long-file-name-that-can-never-fit-inside-a-tab-card-without-the-tooltip.md`
  — a title the tab card can never show in full

## Behavior

- Hovering a tab arms a dwell timer (600 ms, `TIMER_TAB_TOOLTIP`); moving to
  another tab or off the strip cancels it.
- Once the dwell fires, `handleTabTooltipTimer` grants the hovered tab; the
  strip render draws a card only when:
  - the title genuinely overflows the tab's label (measured), and
  - the pointer rests on the label, not the close/dot zone (`mouseX <=
    labelRight`), and
  - no tab drag is in progress.
- The card is drawn below the tab, clamped inside the strip's right edge,
  theme-aware background, ellipsized if longer than 460 px, accent border.

## What the automated checks cover (`tabTitleTooltip` in
`tests/search_input_tests.cpp`)

- Opening both fixtures yields exactly two tabs titled by their file names;
  the second becomes active (real app sequencing with `tabsInit`).
- Dwelling on a non-tab area stays hidden; dwelling on a tab grants it.
- Painting `renderTabStrip` after the dwell publishes a non-empty
  `app.tabTooltipRect` below the strip (`top >= chromeTopHeight`).
- Pointer over the close button withdraws the card even mid-dwell.
- A short title that fully fits raises no card.
- No card before the dwell fires, and no card after the pointer leaves the
  strip.

## Remaining limitations

- The dwell delay is fixed (no setting).
- Names longer than ~460 px are shown with an ellipsis rather than in full.
- No CJK/emoji smoothness pass; trimming granularity is per-character.