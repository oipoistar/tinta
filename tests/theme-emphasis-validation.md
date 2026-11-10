# Bold, italic, selection and rule colours in themes.ini (#310)

Validated on Windows with the Release build on 2026-11-01.

## What was asked

`themes.ini` had no keys for bold, italic and bold-italic text, the
selection or horizontal rules: bold and italic took the colour around them,
the selection was a fixed blue (Live's the accent) and rules took
`blockquoteborder`.

## The change

- Five optional keys: `bold=`, `italic=`, `bolditalic=`, `selection=` and
  `hr=`, RRGGBB like the rest. A missing or invalid one keeps the look as it
  was, so the built-in themes and existing `themes.ini` files are unchanged.
- Bold and italic take their colours in every block, headings, lists, tasks,
  quotes, callouts, tables and footnotes included. Text both takes
  `bolditalic=`, else `bold=`, else `italic=` (`themeEmphasisColor`). Links
  keep the link colour and inline code its own.
- `selection=` is the hue of the reading view's, Source's (and its extra
  carets') and Live's selections, each at its own translucency (35%, 35%,
  28%), so the text under it stays readable (`themeSelectionColor`).
- `hr=` colours the reading view's rules and the footnotes' rule, Live's
  rules on the line being typed, and the HTML and Word rules.
- Live's line being typed and Source colour their bold and italic spans the
  same way. Their spans do not nest (`live::inlineSpans`), so `***words***`
  read as bold there, as their weight already did.
- HTML gets `strong`, `em`, `strong em, em strong` and `::selection` rules,
  with `a strong, a em` inheriting the link's colour; Word colours the runs,
  except code inside them, and the rule's border. Printing keeps its palette.
- The keys are written back when a theme is saved from the editor, and the
  template a new `themes.ini` starts with names them.

## Fixture

`tests/fixtures/theme-emphasis-310.md`: headings, paragraphs and underscore
forms, bold-italic nested both ways, a link in bold and bold in a link, code
in bold, marks, strikethrough, bullets, a nested list, tasks, numbers, a
quote, a callout, a table, a code block, math, three kinds of rule and a
footnote. `tests/fixtures/theme-emphasis-310.ini`: every key in a light and
a dark theme (the dark one with `#` and lower case), bold and italic only,
italic only, and invalid values.

## Automated checks

- `inline_code_theme` (`testEmphasisColours`): the keys load, invalid ones
  and the built-in themes keep inheritance; for the five themes at scale 1
  and 1.5 and 1050 and 650 wide, every bold, italic and bold-italic word of
  the fixture takes the colour the fallback gives, plain words, links and
  code keep theirs, the text keeps its place and size, and the three rules
  and the footnotes' take `hr=` (else `blockquoteborder`); HTML carries the
  rules (none without the keys); Word colours bold, italic and bold-italic
  runs, keeps links' and code's colours and colours the rule; the keys
  survive a save and reload, and cleared ones stay cleared.
- `live_editor` (`themeColours`): Live's line being typed draws bold and
  italic (both forms) in the theme's colours and links, code, marks and
  plain words as before; its selection, read back from a drawn bitmap, is
  `selection=` blended at 28%; its rule is drawn in `hr=`.
- `source_highlight`: Source draws bold and italic words in the theme's
  colours, keeps their marks faint, and keeps the text colour without the
  keys.

Neutering, a group at a time (`neuter_310.py` in the session's scratchpad):
without the page's colours, the rule colour, the HTML rules, Word's colours
and the writer, 7 checks fail; with Word colouring code inside bold, 1; without
Live's bold colour, selection and rule, 3; without Source's bold colour, 1.

The whole suite passes.

## In the built app

An isolated portable copy (`out/theme-310/`, ignored) with the fixture's
`themes.ini`, driven with posted messages and captured with PrintWindow
(`out/theme-310/shots/`):

- 1280 x 900, the light theme: the reading view draws every bold, italic and
  bold-italic word of the fixture in red, green and purple, in the heading,
  paragraphs, lists, tasks, the quote, the callout and the table; the links
  in and around bold stay blue and the code its colour; the rules are green.
- A dragged selection in the reading view: a pixel above the letters reads
  (237, 165, 161), `selection=CC0000` at 35% over the page's FFFDF8.
- E into Live: the heading being typed shows its marks faint and its words
  red and green, the page around it as the reading view; a selection there
  is red at Live's strength. In the paragraph being typed, a link or code
  inside bold reads as bold, as before.
- The pill to Source: bold and italic words red and green, marks faint, the
  selection red, fenced code in its own colours.
- 720 x 760, the dark theme: the reading view and Live in orange, purple and
  pink, rules teal, the selection amber.

## Size

`tinta.exe` grows by 6,656 bytes (3,935,232 to 3,941,888).

## Limitations

- The reading view's selection is drawn in `main_d2d.cpp`, outside the test
  harness; it was checked in the built app, and the helper it uses is
  tested.
- HTML and Word were checked through their export functions' output (the
  CSS rules and the document XML), not opened in a browser or Word.
- The theme editor has no controls for the new keys; they are `themes.ini`
  settings, kept when a theme is saved from the editor.
- Seen in the fixture and left alone: `==**words**==` and `~~**words**~~`
  keep their `==` and `~~` as text; `**==words==**` works.

## Follow-up: hand edits that did nothing (reported 2026-11-04)

Validated on Windows with the Release build on 2026-11-05.

### What was reported

On #310, after v4.0.2: "Commit 07a35ac doesn't work. I can't colour the
bold/italic/hr etc."

### Cause

- **The examples' own notes.** A new `themes.ini` lists every key as a
  commented example. The examples for `bold`, `selection` and `hr` carry a
  note on the same line, as do `heading`, `code`, `headingfont` and
  `inlinecodebackground`.
  - With the `; ` deleted, the value read as
    `1A365D              (italic=, and bolditalic= for both)`. That is not a
    colour, so it was dropped without a word.
  - The plain lines (`background`, `text`, `link`) loaded, so the theme
    half worked.
- **Spaces and capitals did the same.** `bold = AA2200` was the key `bold `
  with the value ` AA2200`, and `Bold=` was no key at all. `name = My theme`
  lost the whole theme.
- **Edits waited for a restart.** A hand edit loaded only at the next
  launch, and with "Open files in tabs" one window can stay open for days.
  A save from the theme editor rewrote `themes.ini` from what had loaded,
  so it threw away any hand edit made since.

### Change

- **Lines read as meant.** Keys and values lose the spaces around them, and
  keys match in any case. A note after a value is dropped, after a `;` or
  in parentheses after a space. Lines starting with `;` or `#` are comments.
  A theme's `name=` keeps everything but its outer spaces, parentheses
  included.
- **A saved change applies while Tinta runs.** The file watcher's half-second
  tick reloads the themes and reapplies the one in use, found by its name:
  - moved in the file, it stays in use;
  - deleted, Paper takes over;
  - the light and dark picks for following Windows move with their themes;
  - the chooser's previews are rebuilt.
- **The theme editor reads the file first.** Its save reloads `themes.ini`
  if it changed, so the rewrite keeps hand edits.
- **The docs.** The header Tinta writes into `themes.ini` says edits apply at
  once, and `docs/theme-colours.md` has a section on editing by hand.

### Fixture

`fixtures/theme-hand-edited-310.ini` holds:

- spaces around `=`;
- keys in capitals;
- `;` and parenthesised notes;
- a name with parentheses.

Open `fixtures/theme-emphasis-310.md` with it.

### Automated checks

`inline_code_theme` (`testHandEditedThemes`):

- A new file's examples with the `; ` deleted load every key, notes and all:
  - `bold`, `selection` and `hr`;
  - `heading` and `code`;
  - `inlinecodebackground=none`;
  - the Georgia and Cascadia Mono faces.
- The hand-written fixture loads every key.
- An edit saved while its theme is in use shows on it at once.
- A theme moved in the file stays in use.
- A theme deleted from the file gives way to Paper.

With the old line reading, 11 checks fail.

### In the built app

A portable copy ran at 1000 x 820 (`out/theme-310/`), with `themes.ini` as a
user would leave it: the examples uncommented, then the hand-written theme.

- **Hand (edited):** bold is pink, italic green, both purple, and rules light
  blue. Editing its `bold=` to `3366FF` while the page was open turned every
  bold word blue within 1.5 s, with no restart.
- **My theme**, the uncommented examples:
  - headings are in Georgia and navy, and bold is navy;
  - rules are light blue, and inline code has no box.

### Size

`tinta.exe` grows by 3,072 bytes (4,000,768 to 4,003,840).

### Limitations

- A value that still isn't a colour (`bold=red`) is dropped without a
  message, as before.
- The theme editor has no controls for these keys yet.
