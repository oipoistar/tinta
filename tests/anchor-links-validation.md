# HTML anchor links (#255)

Validated on Windows with the Release build on 2026-10-08.

## Report

A link from a table to an anchor in the same document, `**[A1](#a1)**`, did
nothing, and the anchor itself, `<a id="a1"></a>`, showed up as text. The same
document works in VS Code and on GitHub. The reporter's file puts the table at
the top, 40 filler lines below it, then
`<a id="a1"></a>A1. Table link should navigate here.`

## Cause

- Inline HTML other than `<br>` and `<font>` was kept as literal text, so
  `<a id="a1">` and `</a>` were drawn.
- A `#fragment` link looked only at footnotes and heading slugs. An HTML
  anchor was never a target, so the click silently did nothing.
- A heading holding an anchor (`## <a id="c3"></a>Title`) also lost its own
  slug, which was built from the tag text.
- Inside an HTML block, `<a name="x">` became an empty link: invisible, but not
  a target either.

## Change

- The parser turns `<a id="x">` and `<a name="x">` without an href (quoted or
  unquoted, any case, self-closing or not) into an invisible anchor element,
  and drops the `</a>` that closes it. `<a href>` is unchanged: inline raw HTML
  links still show as typed. An `</a>` with no open anchor in its paragraph
  stays literal.
- Layout records each anchor at its line, in document order; the first of a
  repeated id wins, as in a browser. Anchors recorded while a table measures
  its rows are rolled back with the rest of that trial layout.
- A paragraph or HTML block of nothing but empty anchors (the common
  `<a id="x"></a>` line above a heading) takes no room, like an empty
  paragraph in a browser.
- `#id` links try footnotes, then heading slugs, then anchors. Following a
  `file.md#id` link while editing moves the source to the same place.
- HTML export writes the anchors as `<a id="x"></a>`, so the links work in the
  exported page. DOCX and printed output just lose the stray tag text.

## Fixtures and automated checks

`fixtures/anchor-links-255.md` recreates the reporter's bold link in a table
cell and adds an anchor alone above a heading, one inside a heading, one in a
table cell long enough to wrap, one in an HTML block, and a Unicode id, with a
list, a quote, a code block, emphasis and a web link around them.

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

`file_fragment_navigation` now also opens that fixture in Paper and Midnight
at 650 and 1050 px and:

- checks that no anchor tag is drawn, while the text beside each anchor and
  the tags quoted in code spans still render;
- clicks the bold table link through its real hit rectangle and checks the
  view lands on the line of `<a id="a1">`, found from the rendered text;
- follows `#b2`, `#c3`, `#d4`, `#e5`, a percent-encoded Unicode id and a
  heading slug, and checks each lands on its line; `#c3` and `#third-heading`
  land on the same spot, and a missing id leaves the view alone;
- checks the table's trial measurements leave exactly one `d4` anchor;
- exports the fixture to HTML and checks the anchors are `<a id>` elements,
  not escaped text;
- on small documents: an inline `<a href>` stays literal; unquoted, uppercase
  and self-closing anchors are targets; an unclosed anchor does not swallow a
  later paragraph's `</a>`; the first of a repeated id wins; an anchor-only
  paragraph and an anchor-only HTML block take no room.

With the parser change disabled (3.7.5's literal tags), the test fails 37
checks. With only the rollback of trial anchors removed, it fails the 8
table-cell checks.

The full suite passed 31 of 31. `editor_context_selection` crashed
(0xc000041d) on its first run after the build, like the clipboard flake seen
before this change, and passed every rerun.

## Exports

The current master (1b69939) and the fix rendered print pages, HTML and DOCX
in Paper and Midnight for the new fixture and four controls
(`markdown-regression-control`, `footnotes`, `file-fragments/index`,
`table-input-236`). The controls were identical. The fixture changed where
intended: the HTML (anchors instead of escaped tags), the DOCX text (no tag
text) and print pages 2 and 3 (no tag text, and the anchor line above
"B2 heading" no longer takes a line). A first comparison showed one differing
print page in two controls in Paper; a full rerun and two more runs of each
build were identical, so that was run-to-run variation, not this change.

The reporter's own file through both builds: before, the HTML holds the
escaped tag and no anchor; after, it holds `<a id="a1"></a>`.

`out/issue-255/before-after.png` (ignored) shows print page 2 of the fixture
from both builds side by side.

## Desktop check

Not run: the workstation was locked, which blocks window capture. The native
test drives the real mouse handlers on a hidden window instead.

## Limits

- Only `<a>` without an href becomes a target. An `id` on other inline
  elements (`<span id>`) is not one, and inline raw HTML links (`<a href>`)
  still show as typed.
- In-document links do not work in DOCX or printed output, for headings as
  well; that is unchanged.
- HTML export gives headings no ids, so links to heading slugs do not work in
  exported pages; unchanged.
- Ids match exactly, as in a browser, so a link in a different case does not
  find its anchor.

## Draft reply (not posted)

### #255

Thanks for the report and the sample! Two things were wrong: Tinta drew raw
inline HTML as text, so `<a id="a1"></a>` showed up, and in-document links
only knew headings and footnotes, so `#a1` had nothing to jump to. In the next
release, `<a id="...">` and `<a name="...">` anchors are invisible and work as
link targets from tables, lists or anywhere else in the document, and an
anchor alone on the line above a heading no longer leaves a blank line.
Exported HTML keeps the anchors too.
