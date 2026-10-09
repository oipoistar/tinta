# Close without asking (#252)

Validated on Windows with the Release build on 2026-10-08.

## Request

Closing Tinta with unsaved edits always opens the Save / Discard / Cancel
dialog. The reporter asked for a setting that closes without the dialog and
keeps the edits in draft recovery for the next launch, which the README
already promises for quick notes.

## Change

- Settings → Editor → **Close without asking** (`closeKeepsDrafts`, off by
  default). With it on, `WM_CLOSE` writes every unsaved buffer as a draft
  (`draftsSweep`) and lets the window close; `WM_DESTROY` then keeps the
  drafts instead of deleting them. The next launch finds them as drafts of a
  process that is gone and offers them back with the existing recovery chip.
- Restoring a draft of a file that is already open in a clean tab, as a
  restored session tab usually is, takes that tab over instead of opening a
  second tab for the same file. A tab with unsaved edits of its own keeps
  them, and the draft opens beside it.
- Q and Esc in the reader used to call `PostQuitMessage` and end the process
  without `WM_CLOSE` or `WM_DESTROY`: no dialog for unsaved tabs, no drafts
  for them, and no saved session or window placement. They now post
  `WM_CLOSE`, like the title bar's close button.
- The switch persists the moment it changes and the exit save leaves it as
  on disk, like "Open files in tabs", so an older window cannot write back a
  stale value. Closing a single tab still asks, with or without the setting.

## Fixtures and automated checks

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

`editor_features` covers recovery in a portable test folder: a draft takes
over the clean tab of its file, an inactive clean tab is reused too, and a
tab with unsaved edits keeps them while the draft opens beside it. After
`draftsSweep` writes one draft per unsaved tab, a second editor standing in
for the next launch restores all three as unsaved tabs, the first in the
session's own tab, and the draft files are gone afterwards.

## Desktop check

Portable copies of the 3.7.4 and the new build ran from scratch folders with
input as posted window messages:

| Check | 3.7.4 | New build |
| --- | --- | --- |
| Setting on: typed in the editor, window closed | n/a | closed at once; one draft with the typed line |
| Setting on: relaunch | n/a | the session reopened `note.md` |
| Setting off: typed, window closed | dialog held the close; N discarded; file untouched; no drafts | same |
| Q in the reader | closed without saving the session | closed and saved the session |

## Limits

- The workstation was locked during the relaunch, so the recovery chip could
  not be seen or clicked live; `editor_features` covers the restore it
  triggers.
- Signing out or shutting Windows down still ends Tinta without `WM_CLOSE`;
  drafts from the 30-second sweep survive that, as before.

## Draft reply (not posted)

### #252

Thanks for the suggestion. The next release adds Settings → Editor → **Close
without asking**. With it on, closing Tinta with unsaved edits skips the
Save / Discard / Cancel dialog: each unsaved document is kept as a draft, and
the next launch offers them back. Restore reopens each one as an unsaved tab,
in the file's own tab when the session reopened it. With the setting off,
closing asks as before. Q and Esc in the reader now close the same way as the
window's close button, so they follow the setting too and remember your open
tabs.
