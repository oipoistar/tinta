# The find bar over the editor (#319)

Validated on Windows with the Release build on 2026-11-12.

## What was wrong

The report: in the source editor the word typed into the find bar is not
centred, and with a word in the bar nothing in the source can be selected.
Looked into, four faults:

- **The word stood low and was cut.** What is typed into the bar was drawn
  from the field's top in whatever face had the letters. A Chinese word
  comes in the document's face for Chinese (`cjkFont=` first), and a face
  with more room over its letters has its baseline lower in its line: the
  word stood under the line of the count beside it, and the field's lower
  edge cut its feet off. A face with less room (SimSun) stood high
  instead. Thai, Hindi, Burmese and Javanese words did the same without
  any setting.
- **No drag selected with the bar open.** Every move of the mouse ended
  at the bar's own code, wherever the pointer was. In the source and in
  Live a press put the caret down and the drag after it did nothing; in
  the reading view the selection came only with the release, with nothing
  to see on the way. The editor's text also had the arrow, not the beam.
- **Live kept the keys in the bar.** A press on Live's page left the bar
  with the keyboard, so what was typed next went into the bar.
- **In Live the bar was a sliver.** Its place was worked out from the
  source pane, which Live does not have: the bar was 72 px wide at the
  tool rail, with 20 px for a word. So since Live came (4.0.0).

## What changed

- The field's line is moved so that its baseline is where the bar's own
  face has it (`baselineShift`, `search_input.cpp`), for the find and the
  replace field. The field ends the text at its sides only; over and
  under it a tall letter has the bar's room. The marked text is as tall
  as the field whatever the face.
- A drag begun on the page goes on wherever the pointer is
  (`handleMouseMove`, `input.cpp`). The bar has the pointer over itself
  only: the beam over its fields, a hand over Replace and All, the arrow
  elsewhere on it (`searchBarCursor`, from the bar's real rectangles; the
  old code looked for the bar where it was before the tab row). Beside
  the bar the editor has its own pointer and hovers. The reading view
  keeps the arrow while it finds, as before.
- A press on Live's page gives the page the keys, as a press in the
  source does (`liveMouseDown`).
- In Live the bar stands over the page, in its middle (`searchBarRect`).

## Fixture

`fixtures/find-bar-319.md`: the reporter's kind of page. Chinese beside
Latin in a heading, in bold, italic, a link, code and a mark; inline and
display math; a quote; a table of one word each in Chinese, Japanese,
Korean, Hindi, Thai, Burmese, Javanese and Latin; a nested list with task
boxes, sub- and superscript; a code block.

## Automated checks

**`find_bar`** (`find_bar_tests.cpp`, 930 checks), drawn through the
window's own context and read back as pixels.

- **The word on the line.** Nine words (the eight scripts, and Chinese
  after a Latin letter) are typed into the bar in eleven places: the
  reading view, the source and Live; Paper and Midnight; 1050 and 650 px;
  100% and 150%; `cjkFont=` unset, SimSun, Microsoft JhengHei and a face
  without Chinese. Each word's ink is in the rows and columns where the
  test's own drawing of it has it with its baseline on the bar's, whole,
  and inside the bar. 26 of the 99 came in a face with another baseline
  than the bar's (a PC without such a face says so and checks less). The
  marked text is tinted from the field's top to its bottom and no
  further.
- **A drag beside the bar.** In the source, the source beside its
  preview and Live, with a word in the bar, an empty bar and the replace
  row open: the drag selects what it selects with the bar closed; the bar
  stays and the text has the keys; drawn up through the bar the drag goes
  on and the bar's field takes no part; back under the bar it ends where
  the plain drag ended. The bar is whole over its pane, in its middle,
  with a field to type into. In the reading view the selection follows
  the pointer before the release and is the bar-closed one.
- **The pointer.** The source's text has the beam beside the open bar;
  the beam over both fields, the arrow over the bar's glass and the
  replace row's mark, a hand over Replace and All; a drag in the find
  field selects the field's text, not the source's. Reading view: beam
  over the field, hand over the results button, arrow over the page.

Each part taken out in turn fails the test:

| Taken out                                      | Failing checks |
|------------------------------------------------|---------------:|
| The word on the bar's baseline                 |             26 |
| Room over and under the field                  |             11 |
| The marked text as tall as the field           |             11 |
| The field drawn as before (all three)          |             35 |
| A reading view drag goes on                    |              2 |
| A source drag goes on over the bar             |              6 |
| A Live drag goes on over the bar               |              3 |
| The editor's own pointer beside the bar        |              1 |
| Every move ends at the bar, as before          |             30 |
| A hand over the replace row's buttons          |              2 |
| The beam over the fields                       |              3 |
| The bar over the page in Live                  |             57 |
| A press in Live takes the keys                 |              6 |

The whole suite passes, 79 of 79, without `notification_setting`,
`copy_file_path` and `code_block_layout`, which use the clipboard and
whose area this does not touch.

## In the built app

`tinta.exe` in a portable folder of its own with the fixture, driven
through its message queue: `E`, `Ctrl+F`, a word typed, the mouse pressed
in the text under the bar, drawn over three lines and let go, `z` typed,
`Ctrl+S`. The saved file tells what happened.

| Build        | Source                          | Live                          |
|--------------|---------------------------------|-------------------------------|
| 4.1.0        | nothing selected, `z` put in    | nothing selected, no `z`      |
| with the fix | 158 letters selected, now a `z` | 111 letters selected, now `z` |

Both builds answer within 20 ms after every step and close with exit
code 0.

Pictures of the bar in each place are written to the test's `frames`
folder; the source, Live, SimSun, Javanese and narrow Midnight ones were
looked at.

## Speed and size

- A frame with the bar open lays out one empty line more to learn the
  bar's own baseline. Not measured apart; it is one `CreateTextLayout` of
  no text per field drawn, and only while the bar shows.
- `tinta.exe` is 5,255,680 bytes, 1,024 more than 4.1.0.

## Limitations

- The reporter's own face is not known. Faces with more room over their
  letters than Segoe UI (Javanese Text, Myanmar Text, Nirmala UI,
  Leelawadee UI) and one with less (SimSun) stood in for it.
- A word stands on the bar's baseline, not in the bar's optical middle:
  a face whose letters sit high or low on their own baseline still looks
  so, as it does in running text.
- A face taller than the bar's 44 px at 16 px type would still be cut,
  at the bar's own edge. None installed here is.
- The real window was not looked at: screen copies come back black
  here. What it draws is the same `renderSearchOverlay` the test draws,
  and what it does was read from the saved file.
- With the bar open the reading view still has the arrow over its page
  and no link or button lights up under the pointer. Left as it was.
- The reader's and the editors' frame sweeps were not run again: no
  frame without the find bar goes through the changed code.
