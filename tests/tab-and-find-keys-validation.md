# Ctrl+PgDn / Ctrl+PgUp for tabs (#334) and F3 for the next match (#335)

Validated on Windows with the Release build on 2026-11-21.

## What was asked for

Two keys Windows programs share: Ctrl+Page Down and Ctrl+Page Up for the
next and the previous tab, and F3 and Shift+F3 for the next and the
previous search match.

## What changed

### Tabs (#334)

- **Ctrl+PgDn** goes to the tab on the right and **Ctrl+PgUp** to the one
  on the left, round the ends: where Ctrl+Tab and Ctrl+Shift+Tab go, and
  from the same places, the reading view, the source editor, Live and with
  the find bar open. Text being edited stays in its tab, unsaved, as it
  does for Ctrl+Tab.
- With one tab the two keys are left as they were. Neither the reading
  view nor the source editor did anything with them.
- Ctrl+Shift+PgDn and Ctrl+Shift+PgUp are not these keys and change no
  tab. PgDn and PgUp by themselves page as before.

### Find (#335)

- **F3** goes to the next match and **Shift+F3** to the one before, round
  the ends, as Enter and Shift+Enter do from the bar. They work from the
  bar and from the editor's text with the bar still open, in the reading
  view, the source editor (with the replace row too) and Live, and type
  nothing.
- **With the bar closed** F3 opens it on the word searched last, which the
  bar forgets as it closes: at the match after the one it was closed on
  (Shift+F3: the one before), in the same document and view; in another
  document or view at the first match (Shift+F3: the last). The word is
  selected, so typing starts another search. With nothing searched yet it
  opens the empty bar, as Ctrl+F does.
- **A command bound to F3** in a Custom shortcut profile keeps the key.
  Ctrl+F3 is not the key.
- Help lists `Enter / F3` and `Ctrl+Tab / Ctrl+PgDn`.

## Fixtures

`fixtures/tab-find-keys/`, three files to open in one window:

- `Find next.md`: the word *lantern* ten times, in a paragraph, a quote
  with emphasis, a list and a nested item with strong text, a table cell, a
  code block, a link, further down the page and once in capitals, with
  Turkish and Chinese text that has none.
- `Second tab.md`: a table of the tab keys, a quote with the word once,
  and a list long enough to page through.
- `Third tab.md`: a short page with task boxes.

## Automated checks

**`tab_find_keys`** (`tab_find_keys_tests.cpp`, 80 checks), every
key through `keystroke.h`.

Tabs: with three tabs, Ctrl+PgDn four times and Ctrl+PgUp four times go
round the row, each press consumed, the shown document the tab's own; the
same places as Ctrl+Tab and Ctrl+Shift+Tab; PgDn and PgUp by themselves
page and stay; with Shift as well no tab changes; from the source editor
with a word typed, the next tab has its own text and the way back finds
the word there, unsaved; from Live; with the find bar open; and with one
tab left the keys do nothing in the reading view or the source editor,
while PgDn still pages.

Find: with nothing searched yet F3 and Shift+F3 open an empty bar; ten
matches; F3 three times, Shift+F3 back, Enter going on from where F3 left
it, round both ends, the page going to the match; nothing typed into the
bar; Ctrl+F3 nothing. Closed on the fourth match, F3 opens on the fifth
with the word selected, and typing replaces it; Shift+F3 opens on the
third; closed on the last, the first; closed on the first, Shift+F3 the
last. A word with no match is remembered too. In the source editor after
closing in the reading view, the first match; steps from the bar and from
the text, where the text is unchanged and the keyboard stays in it; closed
on the fourth there, the fifth; with the replace row open a step replaces
nothing. In Live, steps both ways. Back in the reading view Shift+F3 opens
on the last. In another document, that document's first match. A command
bound to F3 keeps it, and the key is the find key again with the Windows
profile. Under the Settings dialog F3 opens no bar.

Each part taken out in turn fails the test:

| Taken out                              | Failing checks |
|----------------------------------------|---------------:|
| Ctrl+PgDn and Ctrl+PgUp change tabs    |             15 |
| Ctrl+PgUp goes back                    |             49 |
| One tab leaves the keys alone          |              1 |
| Not with Shift                         |              1 |
| F3 is a key                            |             27 |
| Not Ctrl+F3                            |              1 |
| A command bound to F3 keeps it         |              1 |
| Shift+F3 goes back                     |             12 |
| The word is remembered                 |             12 |
| The place is remembered                |              3 |
| The same view                          |              5 |
| Elsewhere backwards starts at the last |              1 |
| The match after, not the same          |              4 |
| The match before, not the same         |              2 |
| Round the end                          |              2 |
| The word is selected                   |              2 |
| The bar open: a step                   |             16 |
| Only in the same document and view     |              6 |
| The same document                      |              1 |

Two were not caught at first: the test pressed Ctrl+Shift+PgDn and then
Ctrl+Shift+PgUp and looked once, where the two cancel out, and the place a
search was closed on was never taken to a document where it would matter.
Each has a check of its own now; the other rows' counts are from before
those were added.

The whole suite passes, 88 of 88, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which overwrite the clipboard.
`notification_setting` writes `settings.ini`, which has a new line: it was
run by itself with the clipboard kept and put back, and passes.

## In the built app

`tinta.exe` in a portable folder of its own (`out/audit/realapp_batch.py`,
not in the repository), its keys posted to the window with Ctrl or Shift
down in the window's own input state, so they pass through the message
loop. With tabs the window's title is the app's name, so the tab in front
is read from `settings.ini`: as it closes, the app writes the place of the
document it was showing first. Where the page was comes from the same
line.

Three files opened as tabs, the third in front:

| Keys | master before this | this build |
|------|--------------------|------------|
| none | Third tab | Third tab |
| Ctrl+PgDn | Third tab | Find next |
| Ctrl+PgDn twice | Third tab | Second tab |
| Ctrl+PgUp | Third tab | Second tab |
| Ctrl+Tab | Find next | Find next |
| Ctrl+Shift+Tab | Second tab | Second tab |

`Find next.md` alone, in a window 900 by 640: Ctrl+F, `lantern` typed, F3
eight times, Esc, close.

| Keys | master before this | this build |
|------|--------------------|------------|
| F3 with the bar open | page at 0 | page at 1,170 |
| Esc, then F3 with the bar closed | page at 0, and the last Esc closed the window | page at 1,170, the window still open |

Every window answered after its keys and closed with exit code 0.

## Limitations

- Screen copies come back black here, so the windows were not looked at.
- Ctrl+Shift+PgDn and PgUp do not move a tab along the row, as they do in
  browsers.
- The word searched last is kept for the window's life, not across
  starts, and one word for all tabs.
- F3 with the bar closed opens the bar; it does not step with the bar
  hidden.
- F3 was not added to the shortcut editor as an action of its own: it is
  fixed, and yields to a command bound to it.
