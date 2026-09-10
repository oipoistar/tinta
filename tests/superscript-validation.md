# Issue #206 validation

Validated on Windows on 2026-09-10. Reported by [@Ra0EL](https://github.com/Ra0EL)
in [#206](https://github.com/oipoistar/tinta/issues/206).

## Changes

- Keep the superscript/subscript document format alive when overlay fonts refresh;
  release it with the other document formats at shutdown. Existing theme fonts
  and the existing 68% body-font sizing are retained; no font asset is added.
- Open any Learn example in the editor as a dirty untitled copy. Normal Save As,
  tab parking and unsaved-content confirmation apply; embedded originals remain intact.
- Resolve example edit hints from the active keymap, escaping Markdown punctuation.
- Consume the editor's Save shortcut before its native modal dialog can release
  Ctrl and allow the original S to be translated into a stray editor character.

## Automated checks

`cmake --build build --config Release --parallel 6` and
`ctest --test-dir build -C Release --output-on-failure`: **16/16 passed**.

The new `superscript_and_learn` test uses real DirectWrite formats and layouts:

- Paper and Midnight; 100%, 150%, 200% DPI scale; 80%, 100%, 150% zoom;
  650 and 1050 logical-pixel widths (36 combinations).
- Small font size, selected font family, overlay refresh lifetime, native body
  baselines, script runs inside headings, tables, quotes, lists, emphasis and links.
- Mixed fixture table, code fence, literal inline code and final paragraph.
- All three Learn cards with Windows, Vim and custom F2 shortcuts; custom `*`
  hint rendering; editor source, untitled title, tab round trips, close/Keep editing,
  ordinary save writer, Save-key consumption, unchanged embedded originals.
- Empty launcher remains unchanged; shutdown releases the script format.

Tests run inside an isolated portable directory and never use the user's settings.

## Native app and exports

`tests/render_superscript_fixtures.ps1` renders `superscript-subscript.md`,
`markdown-regression-control.md` and `tab-drop-position.md` through Tinta's
native print-pages, HTML and DOCX commands. Each produced two PNG pages.

Compared with the pre-fix executable at commit `4f18026`:

- Both control documents: every PNG, HTML and DOCX is byte-identical.
- Script fixture: page 1 changes where the scripts are repaired; page 2,
  HTML and DOCX are byte-identical. All six equations and surrounding blocks remain.
- Both script print pages were visually inspected.

Computer Use verified the actual built application: Markdown basics displays
raised/lowered small text and the E hint; E opens its source and preview as Untitled;
Ctrl+S opens Save As; saving `out/issue-206/ui/Learn final.md` leaves the editor
clean with no extra S. The resulting file contains the original example source.
The mixed fixture was inspected in Paper at 1050px and Midnight at 650px,
including its final display equation and paragraph. Native layout tests additionally
cover both widths in both themes and the DPI/zoom combinations above.

Artifacts are ignored under `out/issue-206/`; the runnable Markdown fixture and
render script are tracked. The live test copies use isolated settings.

## Scope limits

Scripts nested inside `==highlight==` or `~~strikethrough~~` still have the existing
parser limitation (inner delimiters remain literal). This change restores the
font lifecycle and example editing; it does not rewrite nested extension parsing.
The frontmatter property strip also uses this document small font and now retains
its intended smaller size. Physical printer output and switching between actual
monitors were not tested; native print output and simulated DPI layout were tested.
