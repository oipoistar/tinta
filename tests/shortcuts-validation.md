# Issue #195 validation

Issue: [Shortcuts not working](https://github.com/oipoistar/tinta/issues/195), reported by @Fromville.

## Reproduction and cause

Reproduced in the published v3.5.6 executable, using isolated portable settings
with `keyProfile=windows` and an open Markdown document:

1. F1 does nothing.
2. P opens the Keyboard Shortcuts overlay, whose own hint says F1.
3. Latin E enters Edit normally.

Edit and Help were still classified by their original `:`/`?` defaults as
character actions. Windows-profile E therefore depended on a Latin WM_CHAR.
F1 only produced a key event, while its numeric VK value (`0x70`) was incorrectly
case-folded as the character `p`. The same namespace collision affected custom
function-key bindings. This is consistent with Microsoft's distinction between
[WM_KEYDOWN virtual keys](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-keydown)
and [WM_CHAR text](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-char).

The fix stores the input kind with each binding, dispatches Edit/Help on keydown
for letter/function-key bindings, and skips character translation when the action
consumes the key. It does not use a time delay that could swallow subsequent typing.
Punctuation and full-width `：`/`？` still use the character path. Vim `/` search now
keeps the first query character instead of waiting to swallow a second opening key.

## Automated checks

Built with MSVC Release; all 10 CTest suites passed.

- `keymap`: Windows/Vim/custom profiles, E versus Latin/Cyrillic/Greek/full-width
  text, P/F1 aliasing, F1-F12 binding resolution, punctuation versus virtual keys,
  F12 versus `{`, Tab/Space remaps, invalid settings and default suppression.
- `input_shortcuts`: real application input handlers and editor with a
  message-only window and `tests/fixtures/shortcuts-layouts.md`. Checks F1 open/close,
  E followed by Latin/Cyrillic/Greek character sequences, a clean initial buffer,
  immediate next input, Help over the editor, normal editor/search/filter text,
  pinned Contents, modal capture, Vim punctuation, full-width colon, custom Help
  on H and Edit on F2, and unsaved-changes confirmation.
- Existing context-menu, math-parser, math-layout, Mermaid, document-type,
  inline-style, translation and configuration suites passed.

Run with `cmake --build build --config Release`, then
`ctest --test-dir build -C Release --output-on-failure`.

## Native application and rendering checks

Used copies of `shortcuts-layouts.md` with portable settings, preserving the source
fixture and the user's settings. In the fixed Windows-profile app at 1050 x 900:
F1 opens and closes Help in both reading and editing modes; P leaves Help closed;
E enters the editor without inserting a character or marking the file dirty.
The heading math and table equations remain intact through these transitions.

At 650 x 760 in Midnight with the Vim profile, the mixed fixture renders normally;
`/` opens Search and the next E appears as `e` in the query.

Exported `shortcuts-layouts.md` and `markdown-regression-control.md` through the
native `--printpages` and `--exporthtml` paths. Both documents produced two PNG
pages and one HTML file. All six artifacts match the published v3.5.6 outputs
byte for byte. The shortcut fixture retains five equations, headings, table,
lists, emphasis, links, quote and fenced code. Both of its native pages were
visually inspected. Generated files are ignored under `out/issue-195/`.

## Coverage limits

Only English and Croatian input layouts are installed on this machine. The
non-Latin cases exercise explicit key/character sequences through the real input
handlers; the reporter's exact layout and IME were not tested interactively.
F10 remains reserved for the application menu. No release was published and no
reply was posted to the reporter.
