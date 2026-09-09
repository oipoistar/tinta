# Copy file path validation (#203)

Validated on Windows on 2026-09-09. Request by
[@Orcomp](https://github.com/Orcomp) in [#203](https://github.com/oipoistar/tinta/issues/203).
The maintainer also identified the single-tab title context-menu bug while testing.

## Native regression checks

```powershell
cmake --build build --config Release --parallel 6
ctest --test-dir build -C Release --output-on-failure
# Explicit opt-in: this command overwrites the Windows clipboard with test values.
& build/Release/file_path_tests.exe --clipboard-test
```

All 14 CTest suites and the explicit clipboard run pass. Ordinary CTest runs
do not read or modify the clipboard.

- `copy_file_path` renders Tinta's actual title strip into a hidden native
  test window and exercises the right-click handler. It covers two tabs,
  transition to one tab, and a forced single-tab row (detached windows),
  in reading and split-editor modes. Cases include short Unicode and long
  filenames, Paper and Midnight, widths 650 and 1050, and 100%, 150%, 200% scale.
- The lone title has a context-menu target while its normal hit rectangles
  still leave the text draggable. App icon, plus and pin targets stay separate;
  the start page and zen mode do not acquire a document title target.
- The native non-client right-click handler uses the same title hit test
  before showing the Windows system menu. Empty caption space retains the
  existing system-menu behavior. This message-routing branch was code-reviewed;
  a full interactive drag/right-click retest was not completed.
- The explicit clipboard run chooses Copy file path through the document
  menu, active and inactive tab menus, and the rendered single-title menu.
  It checks relative paths, slash normalization, spaces, Unicode, UNC paths,
  paths longer than MAX_PATH, and extended-length paths without truncation
  or added quotation marks.
- Active tabs use the live file path even when their saved tab snapshot is
  stale after Save As. Copying an inactive tab preserves the active editor's
  text and dirty flag. Untitled notes and embedded help do not copy a path.
- A clipboard held by another thread preserves its contents and does not
  produce a false Copied confirmation. Clipboard allocation/ownership failures
  return failure; only successful path copies show the confirmation.
- `context_menu` checks that Copy file path sits immediately before Reveal
  in Explorer, is disabled without a saved path, and that all menu rows still
  fit a short window at 100%, 150%, and 200% scale.

## Mixed Markdown fixtures and exports

Open `fixtures/copy-file-path.md` and `fixtures/copy-file-path-companion.md`
to exercise the feature with headings, tables, emphasis, links, lists,
inline code, a fenced code block, blockquotes, and inline/display math.
The companion includes instructions for reproducing the single-tab case.

Both fixtures and `fixtures/markdown-regression-control.md` were exported by
the built app through `--printpages`, `--exporthtml`, and `--exportdocx`.
Outputs and the comparison harness are under the ignored `out/issue-203/`.

- All four native PNG pages are byte-identical to exports of the same fixtures
  from the pre-change master build (`df9225590f48faeb67afac0ef32bf4ed59dc4564`).
- All three HTML files and all DOCX ZIP entries are unchanged.
- The main and companion fixtures retain their four and one equations,
  respectively, plus their headings, tables and quotes. Both fixture PNGs
  were visually inspected; the code row and quote spacing remain correct.

An earlier live Paper-window check confirmed the new document-menu row and
its placement. Computer Use was stopped by the maintainer before copy/paste
interaction completed; subsequent verification used the native tests above.
No issue reply was posted and no release was tagged.
