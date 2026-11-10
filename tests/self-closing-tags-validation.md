# Tags that close themselves, nested without end

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code; nobody reported it. An HTML block with
many tags written as closing themselves, `<b/>` a hundred thousand times,
ended the program as the file opened. The same for `<i/>`, `<p/>`,
`<blockquote/>` and the other tags that hold content.

## Cause

`parseHtmlIntoElements` (`markdown.cpp`) opens an element for `<b/>` as
for `<b>`, which is how a browser reads it, and no close tag follows.
The limit on how deep HTML may nest skipped such tags, taking them to
open nothing. Each one went a level deeper, and the first walk down the
tree ran out of stack.

## Change

A tag written as closing itself counts against the depth like any other.
Past the limit it opens nothing; and as no close tag will follow it, none
is set aside to be skipped for it.

## Fixture

`fixtures/deep-nesting.md` has a new part before its closing heading:
2,000 such tags in a `<div>`, with text after them. The rest of the
fixture, its lists, quotes, code block and headings, is as it was.

## Automated checks

**`deep_nesting`** (`nesting_tests.cpp`) has two more rows in its table:
100,000 `<b/>` in a `<div>`, and 30,000 times `<blockquote/><p/><i/>`.
For each, as for the rows before them: the document parses, its tree is
at most 150 levels deep, the innermost text and the text before are
kept, and the tree goes through what the test's `exercise` does with
every row. The fixture is still checked as a whole.

Without the change the test ends with a segmentation fault at the first
of the two rows.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
64 of 64.

## In the built app

An isolated portable copy opening a file of `<div>`, 100,000 `<b/>` and
`deep</div>`:

| Build | After 8 seconds |
|:------|:----------------|
| v4.0.2 | the program has ended, exit code 0xc0000005 |
| This one | a window that answers |

## Limitations

- Text after more such tags than the limit allows is as bold as the
  limit's last level, not deeper: nothing shows the difference.
- The window's pixels could not be looked at in this session.
