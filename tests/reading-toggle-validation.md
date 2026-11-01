# Ctrl+E reads the page in Live, and comes back (#308)

Validated on Windows with the Release build on 2026-11-01.

## What was asked

From Live the reporter wanted the page as the reader shows it, and back,
with Ctrl+E as other Markdown editors have it. Live's Ctrl+E showed Source
with its preview beside it; the reading view was on Ctrl+Shift+E only, and
the reader did nothing with Ctrl+E.

## The change

- In Live, Ctrl+E opens the reading view: the page as the reader draws it,
  with the edits not yet saved. Ctrl+E there comes back to Live with the
  caret where it was (the reading view already took Ctrl+E back).
- In the reader, Ctrl+E opens the editor, as E does, Live when that is the
  editor in use.
- Source keeps Ctrl+E for its preview pane, and Ctrl+Shift+E still reads
  from either; Ctrl+/ still switches between them.
- The Read pill names Ctrl+E in Live (`editorReadingButtonLabel` puts it in
  each language's label) and Ctrl+Shift+E in Source. The help lists E /
  Ctrl+E for editing, and when Live is the editor in use, Ctrl+E for reading
  and Ctrl+/, Ctrl+E for Source's preview pane.
- README's shortcuts and `docs/live-editor.md` say the same.

## Fixture

`tests/fixtures/live/Reading toggle.md`: the steps, with headings, a
numbered list, emphasis, a table of the keys, a quote, a task, a link,
inline code and a code block.

## Automated checks

`live_editor` (`readingToggle`): from the reader, Ctrl+E opens Live and
types nothing; Live's pill names Ctrl+E; Ctrl+E reads, the pill leads back,
and Ctrl+E comes back to Live with the caret and the text as they were;
Ctrl+Shift+E reads and comes back too; in Source the pill keeps
Ctrl+Shift+E and Ctrl+E hides and shows the preview without reading.

Neutering, each part on its own: with Live's Ctrl+E showing Source again, 7
checks fail; without the reader's Ctrl+E, 10 (the rest of the walk with it);
with the pill keeping Ctrl+Shift+E in Live, 1.

## In the built app

An isolated portable copy (`out/read-308/`, ignored) on the long read,
Live remembered, at 1100 x 800, driven with posted messages; Ctrl was held
by sharing the window thread's key state for the press
(`AttachThreadInput`, `SetKeyboardState`), as posted keys carry no
modifiers. Captured with PrintWindow (`out/read-308/shots/`):

- Ctrl+E in Live: the reading view, full width, without the rail and the
  heading's #; Ctrl+E again: Live, the caret in the heading.
- Esc twice to the reader, then Ctrl+E: Live.
- A word typed in the title, then Ctrl+E: the reading view shows it, the
  tab marked unsaved; Ctrl+E: back in Live with it.
- The pill's Source side, then Ctrl+E: the preview beside the source goes,
  as before.
- The Read pill, fading, reads Read · Ctrl+E in Live.

## Size

`tinta.exe` grows by 512 bytes (3,941,888 to 3,942,400).

## Limitations

- Live's Ctrl+E no longer shows Source with its preview in one key: it is
  Ctrl+/ and then Ctrl+E, as the help says.
- The other languages' help keeps its words; only its keys changed, and the
  pill's label is each language's own with Ctrl+E in place of Ctrl+Shift+E.
