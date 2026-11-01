# Strikethrough and highlight around bold, italic, links and code

Validated on Windows with the Release build on 2026-11-01. No GitHub issue;
the #310 validation left it alone in `fixtures/theme-emphasis-310.md`, where
`~~**Bold_struck**~~` and `==**Bold_in_mark**==` kept their `~~` and `==` as
text while `**==Mark_in_bold==**` worked. GitHub renders `~~**x**~~` as
struck bold.

## Cause

md4c reads the bold correctly. Without its strikethrough flag `~` and `=`
are punctuation to it, so the `**` after `~~` can open (punctuation before,
a letter after) and the `**` before `~~` can close. Tinta pairs `~~` and `==`
itself, in the post-pass `splitInlineExtensions` (`src/markdown.cpp`), because
md4c's strikethrough also pairs single tildes, which are subscripts here.
That pass read text, math and inline HTML spans as one string, but an md4c
span ended the string: `~~`, the bold, and `~~ is struck` were three runs,
each holding half a pair. `**==x==**` worked only because both delimiters
sat in the bold's own text.

## Change

- md4c's spans (bold, italic, links, code and images), footnote references
  and anchors join the run as one opaque part each, as math and inline HTML
  spans already did. Their own text is split first, once, and they move whole
  into the new span: `~~**x**~~`, `==*x*==`, `~~a **b** c~~`,
  `==[a link](#x)==`, ``~~`code`~~``, `~~claim[^1]~~`, `x^**2**^`.
- `~~` and `==` follow the rule md4c gives its own `~~`: a delimiter opens
  before a non-blank and closes after one, and a closer that cannot close is
  passed over with its run of signs. `==` is two signs alone. Without this,
  joining spans into the run would have paired `` `a` == `b` and `c` == `d` ``,
  `**a** == **b** and **c** == **d**` and `` `x` === `y` `` across them. It also
  changes plain text, as on GitHub: `x ~~ y ~~ z` and `a == b == c` were struck
  and marked and now stay text, and `~~a ~~ b~~` strikes `a ~~ b`, not `a `.
- Escapes: md4c hands an escaped character over alone, past its backslash,
  so the pass could not tell `\~` from `~`. An escaped `~`, `=` or `^` now
  waits as a control-byte stand-in while the pass runs and is put back after
  it. `\~\~**x**\~\~`, `\=\=y\=\=`, `H\~2\~O` and `x\^2\^` stay text (they
  were struck, marked, a subscript and a superscript), and `[\^n]` is no
  footnote reference, as on GitHub.
- Depth: each `~~` around a span adds a level, so 5000 levels of
  `~~**a ... b**~~` would double the tree's depth to 265, past the 150 the
  deep nesting walkers are tested to. A span that would put anything below
  `kMaxNesting` is not made and its delimiters stay text, as md4c's spans and
  inline HTML spans stop. This also caps `==<sub>a ... b</sub>==`, which
  already reached 265 before the change.
- Reader: a code chip inside a strikethrough had no line through it, though
  HTML and Word struck it. Only ``<del>`code`</del>`` could put it there
  before; ``~~`code`~~`` now does too, so the chip is struck like the text
  around it (`src/render.cpp`).

## Fixture

`fixtures/struck-and-marked-emphasis.md` puts strikethrough and highlight
around italic, bold, both, underscore forms, a word run on, links, bold links,
code and math. It also nests them both ways and in each other. They sit in
headings, a paragraph that wraps, a table, a quote, a callout, bullets, a
nested bullet, tasks, a numbered list, a footnote reference and the footnote
itself. Its last section keeps every delimiter as typed: in code, with a blank
inside, in comparisons between code spans and bold words (`==` and `===`),
with no closer, across a bold span's edge, escaped, and in a fence.
`fixtures/theme-emphasis-310.md` now renders its second paragraph as written.

## Automated checks

- `document_types`: 22 more tree shapes, for the forms above, the cases that
  stay text, escapes, and blank-led closers. It also walks the fixture: each
  of its 22 `Struck_` words sits under a strikethrough and each of its 19
  `Marked_` words under a highlight. The 16 words of the last section sit
  under neither. No `~~` or `==` stays as text above that section, and its
  table, quote, lists and fence remain. An escaped caret makes no footnote
  reference.
- `inline_styles`: bold in a strikethrough is bold and struck; italic in a
  highlight is italic and marked; code in a strikethrough is struck; a link in
  a highlight is marked and stays a link; the text after them is plain.
- `highlight_layout`: seven highlights that hold bold, italic, links and
  struck bold, in a paragraph, a list, a table and one that wraps over three
  lines, each paint one even band from `alpha` to `omega`, in Paper and
  Midnight at 100/150/200% scale, 80/100/150% zoom and 420/650/1050 px. The
  fixture joins the documents whose highlight bands may not overlap.
- `html_spans`: ``~~`code`~~`` and ``<del>`code`</del>`` draw a line through
  the chip above its baseline, and a plain chip has none, in Paper and
  Midnight at two scales and widths. The fixture's HTML nests `<del><strong>`,
  `<mark><em>`, `<del><code>`, `<mark><a>`, `<del><mark><strong>` and a
  footnote reference in `<del>`, and keeps the last section's delimiters as
  text. Word gives the runs `<w:b/><w:strike/>`, `<w:i/><w:highlight/>` and
  all three, and leaves the escaped pair's bold plain.
- `deep_nesting`: 5000 levels of `~~**a ` and of `==<sub>a ` stop at depth
  133, kept text and all, and lay out, export, copy and free.

Neutering one part at a time (`neuter.py` in the session's scratchpad),
against the final source:

- Without spans joining the run: `document_types` fails 10 checks,
  `inline_styles` 10, `html_spans` 18 and `highlight_layout` 564 (no band at
  all).
- Without the `~~` and `==` rule: `document_types` 8, `html_spans` 2.
- Without the depth cap: `deep_nesting` 2 (depth 265 and 264).
- Without the escapes: `document_types` 6, `html_spans` 2.
- Without the code chip's line: `html_spans` 16, both chips in all eight
  layouts.
- With the old `markdown.cpp`: `document_types` 14, `inline_styles` 10,
  `html_spans` 26, `highlight_layout` 564 and `deep_nesting` 1. The last is
  the `==<sub>` alternation that already went 265 deep.

`cmake --build build --config Release`: no warnings.
`ctest --test-dir build -C Release -E '^(notification_setting|copy_file_path|code_block_layout)$'`
(the three left out overwrite the clipboard): **55/55 passed**.

## Exports compared with the pre-fix build

The pre-fix build (`259657e`) and this one exported print pages, HTML and
Word for all 85 fixtures in Paper and Midnight.

- Only `struck-and-marked-emphasis` and `theme-emphasis-310` change, in both
  themes. In the latter, HTML changes only
  `==<strong>Bold_in_mark</strong>==. ~~<strong>Bold_struck</strong>~~` to
  `<mark><strong>Bold_in_mark</strong></mark>. <del><strong>Bold_struck</strong></del>`.
  Word's runs change the same way, to bold with a highlight and bold with a
  strike, and only that page changes in print.
- Five other fixtures differed in one Paper print page each (2 to 7 pixels).
  The pre-fix build's own second run differs from its first on the same
  pages and matches this build's byte for byte. Their HTML and Word are
  identical, and the parsed trees of all 83 other fixtures are unchanged.

## In the built app

Computer use was not available in this session. Instead, the built
`tinta.exe` ran from an isolated portable folder with the fixtures open,
and each window was captured with `PrintWindow`, paging with PageDown posted
to the window. Captures are in `out/marked-emphasis/app/`, exports in
`out/compare/` (both ignored).

- Paper and Midnight at 1100 and 560 px, the whole fixture through its
  footnote: every struck and marked word keeps its own style, bold, italic,
  a link's colour and underline, and the code chips `struck_code()` and
  `--quiet` have their line. Strikes and markers continue over wrapped lines
  in the paragraph, the long list item and the table, and the last section
  shows each delimiter as typed. The pre-fix build showed `~~` and `==`
  around every span, and marked the `~~ and ` between
  `~~==**...**==~~` and `==~~*...*~~==`.
- `theme-emphasis-310.md` in its theme "310 all keys light": `Bold_in_mark`
  sits on the marker and `Bold_struck` is struck, both in the theme's red
  bold. The pre-fix build showed `==Bold_in_mark==` and `~~Bold_struck~~`.
- E with Live on: Live's page shows the fixture as the reading view does;
  the heading being typed shows `~~**bold**~~` struck with its `**` as text
  (see Limits). E with Source: the docked preview is the reading view's
  page.

## Size and speed

`tinta.exe` grows by 3,072 bytes (3,942,400 to 3,945,472). Parsing every
fixture ten times over (2.5 MB) takes 223 to 227 ms, against 210 to 222 ms
before, best of seven in each of two runs. Three paragraphs of 20,000 spans
each take 223 to 233 ms, against 229 to 231 ms. A run with no `~`, `=` or `^`
in its text is left as it is, unread.

## Limits

- A span still ends at a line break in its own text: a hard-wrapped
  `~~**a**\nb~~` or `==a\nb==` stays text, as before; GitHub strikes it. A line
  break inside the bold it wraps does not end it: `==a **b\nc** d==` is marked.
- A delimiter pair split by a span's edge stays text (`~~a **b~~ c**`). GitHub
  would strike `a **b` there; Tinta keeps md4c's bold.
- Escaping one tilde of a pair, `\~~x~~`, leaves `~x~~`, which is a subscript
  followed by a tilde, as `~x~~` always was. Escape both: `\~\~x\~\~`.
- Delimiters that touch their words on both sides still pair, as md4c's
  `~~` would: `x==y and a==b` marks `y and a`, as before.
- A code chip inside a highlight keeps its own background and draws no
  marker, as it did inside `<mark>` (`highlight-seams-validation.md`).
- Live's line being typed and Source style spans without nesting them
  (`live::inlineSpans`), as `theme-emphasis-validation.md` notes: there
  `~~**bold**~~` is struck with its `**` drawn as text instead of faint
  marks, the way `**==x==**` shows its `==`. The rest of Live's page is
  drawn like the reading view and nests them.
