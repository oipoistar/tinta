# Live notices every document opened past it

Validated on Windows with the Release build on 2026-11-01, after the
scrolling fix for #307.

## What was wrong

Live keeps the text its page was laid out from, and notices a document
parsed past it (another file, a reload) by comparing the parse's root with
the one it last saw. It compared their addresses only. Leaving the editor
parses the file for the reader, which frees the parse Live had seen, and
opening the next file parses again; that parse could be given the freed
address. Live then took the new document for the old one: it kept the old
text, found it differing from the new text from the first difference on,
and drew all of that, with the caret's block, as Markdown being typed in,
until the caret left it.

In the tests this showed as `live_editor` failing about one run in fifteen
on master (4 of 60 runs), in different places:

- the menu's checks on a task and a table, the right-click landing in the
  wrong block, since the page under the open stretch was 700 px lower;
- "a paragraph after a footnote opens alone";
- "a key in a long document takes under 40 ms", one key taking 1.4 s while
  the 247 KB document was drawn as Markdown.

## Finding it

- A log line whenever the root's address matched the one Live had seen
  while the parse was another one (told apart by its parse time), over 60
  runs: 48 runs had no such switch and none failed; 12 had at least one;
  both failing runs were among them. No run failed without one.
- A first log that held the old root weakly found nothing in 40 runs, and
  no run failed: holding it keeps its memory, so the address cannot come
  back, which is the fix below.

## The fix

`LiveEditor::parsedRoot` holds the parse as a `std::weak_ptr<Element>` and
compares by `lock()`. A freed parse locks to nothing, so a new one is always
noticed; and since the root is made by `make_shared`, a weak pointer to it
keeps its block allocated, so its address is not handed to another parse
while Live holds it. Nothing else changes.

## Automated checks

`live_editor` (`documentsSwitched` in `tests/live_tests.cpp`) writes two
small documents of a title and four paragraphs, opens them in Live in turn
250 times and checks each time that, with the caret in the title, Live
opens the title alone (under 90 DIP; a missed switch opens 292).

Neutering (the address alone again): the check failed in each of 8 runs,
at switches 3, 4, 7, 13, 30, 46, 51 and 104, about one in thirty, so 250
switches find one all but always. The check adds about 4.5 s to
`live_editor`.

With the fix, `live_editor` passed 60 runs out of 60 (master: 56 of 60),
and the whole suite passes (55 tests, the three that leave the clipboard
written aside).

## Limitations

- The fault depends on where the heap places a parse, so it cannot be made
  to happen on demand; the check repeats the switch instead, and the built
  app was not shown missing a switch (about one switch through the editor
  in seventy did, by the counts above).
- `tinta.exe` grows by 512 bytes (3,934,720 to 3,935,232).
