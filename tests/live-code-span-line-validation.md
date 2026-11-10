# A line that begins with a code span, in Live

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code; nobody reported it. A paragraph that
starts with a code span written with three backticks, as in
` ```npm install``` installs it.`, is a paragraph to the page. Live read
its line as the opening of a fenced block that never closes. With such a
line in a document:

- a click on any paragraph under it put the caret at the document's end;
- with the caret under it, the rest of the document was one code block,
  and the page's own paragraphs showed again below it;
- Enter in a list under it made no next item, and the `/` menu and the
  format keys held back as they do in code.

## Change

`fenceRun` (`live_text.cpp`) takes backticks with another backtick later
on the line for what they are, a code span, as CommonMark and the page's
parser do. The stretch's own drawing (`live_draw.cpp`) reads the line the
same way.

## Fixture

`fixtures/live-code-span-line.md`: such a paragraph, two paragraphs to
click, a list to press Enter in, a table, a quote and a real fenced
block.

## Automated checks

**`live_editor`** (`live_tests.cpp`), in `codeSpanLine`, through the mouse
and key handlers: a click on the paragraph under the code span's line
puts the caret in it, and a click on the next one there; Down from the
code span's line comes to the next paragraph; Enter at the end of a list
item under it writes the next item's mark; and in a real fenced block
typed after it Enter writes none.

Without the change 5 checks fail.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
64 of 64.

## Limitations

- A fenced block indented four spaces or more, as in a nested list item,
  is a fence to the page and not to Live's scanner: inside it the `/`
  menu opens and Enter continues list marks. Not changed here.
- That the page no longer shows twice was not looked at in a frame; the
  caret's places and the keys were checked.
- Not driven in the built window; the screen could not be copied in this
  session.
