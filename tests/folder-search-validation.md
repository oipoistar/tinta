# Extra-file search: two checkboxes in the results panel

Validation for the folder/open-file search rework. The bug it fixes: opening a
document from the start-page history and searching used to float a panel of
sibling-file hits above the text, and clicking a hit swapped the open document
for that file. Extra-file results now live only in the Ctrl+Shift+F results
dock, behind two checkboxes that default **off**.

## Fixture

`tests/fixtures/folder-search/` is a self-contained folder:

- `current.md` — the document that is open; mixes a heading, a table, a list,
  emphasis, a link, a quote and a code block, all mentioning `needle`.
- `sibling.md` — a sibling `.md` with `needle` in prose, a list and inline code.
- `alpha.md`, `bravo.md`, `charlie.md` — extra siblings with one, several
  (>3, so a "+N more" row appears) and one match, giving the dock several file
  blocks to click.
- `quiet.md` — a sibling with **no** matches, proving the scan filters files.

The search term is `needle`.

Every result row shows its 1-based source line as `N:` before the snippet, so a
hit can be located in the file without opening it first.

## What the automated harness checks

`tests/search_input_tests.cpp` (`--search-input-tests`, CTest `search_input_focus`)
loads `current.md`, opens the dock with Ctrl+Shift+F and asserts:

1. Both checkboxes start off and the dock lists only the open document; calling
   `startFolderSearchScan` while both are off produces no extra results.
2. Clicking the folder checkbox enables the folder scan; the worker (drained
   from `WM_APP_FOLDER_SEARCH`) returns `sibling.md`, skips `current.md`
   (the open file) and skips `quiet.md` (no match).
3. Clicking a file row opens that sibling in its **own tab** — the previously
   open document stays open in its tab — and clears the extra rows. The
   `folderSearchLastRow` case repeats this for the **last** file row after
   scrolling the dock to the bottom, guarding against a wrong-file open when
   several siblings are listed.
4. With the folder checkbox off, enabling the open-files checkbox makes a
   second open tab (`sibling.md`, outside the current folder's scan) appear.
5. Changing the query clears the previous extra-file hits at once, so a row
   clicked during the debounce window can never open a file that no longer
   matches the current query.

Run it with:

```
cmake --build build --config Release --target input_tests
build/Release/input_tests.exe --search-input-tests
```

## Surrounding content

The fixtures keep ordinary Markdown around every match so the scan reads real
documents rather than plain strings:

| Source   | Heading | Table | List | Emphasis | Link | Quote | Code |
| -------- | ------- | ----- | ---- | -------- | ---- | ----- | ---- |
| current  | yes     | yes   | yes  | yes      | yes  | yes   | yes  |
| sibling  | yes     | no    | yes  | no       | no   | no    | no   |
| quiet    | yes     | yes   | yes  | no       | no   | no    | no   |

## Remaining limitations

- The automated check drives the handlers directly; the checkbox glyphs and the
  dock layout were not captured as screenshots in this environment. A manual
  pass in the built app at normal and narrow widths is still worthwhile.
- Open-file scanning reads the other tabs from disk by path. Unsaved edits in
  another tab (edit mode) are not searched, matching the viewer-only scope of
  the dock.
