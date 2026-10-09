# Launch folder release (#253)

Validated on Windows with the Release build on 2026-10-08.

## Problem

The report says a document closed with Ctrl+F4 could not be deleted until Tinta
exited. On 3.7.4 the document itself was never locked: an exclusive open of the
file succeeded with its tab open, after Ctrl+F4 and after exit. Its folder was
locked from launch until exit. Explorer starts Tinta with the document's folder
as the working directory, terminals start it in the current folder, and Windows
will not delete or rename a folder that a running process works in. Closing the
tab could not help, since the lock did not belong to the tab. Started from
another working directory, nothing was locked.

## Change

- Right after the command line is parsed, the file argument, the debug export
  paths and the legacy `syntax.md` fallback resolve against the launch folder.
  Then `parkWorkingDirectory()` moves the process into the system folder.
- `launchDirectory()` remembers where Tinta started. The file browser's
  no-document fallback, which read the working directory, and an untitled
  note's relative images resolve against it, so both behave as before.
- The single-instance hand-off sends the already absolute path instead of
  resolving it again with a `MAX_PATH` buffer.
- All five common file dialogs go through `runOpenFileDialog()` or
  `runSaveFileDialog()`, which park the working directory again afterwards. The
  Pandoc export and Locate Pandoc dialogs set no `OFN_NOCHANGEDIR`, so per the
  dialog documentation they would move the working directory into the picked
  folder and lock it.

## Automated checks

Run from the repository root:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
powershell -ExecutionPolicy Bypass -File tests\validate_launch_folder.ps1
```

- `copy_file_path` first starts inside a scratch folder, confirms that the
  working directory blocks renaming it, parks, and then requires that the
  folder renames, that `launchDirectory()` still names it, that the file
  browser opened without a document starts in it, and that it deletes.
- `validate_launch_folder.ps1` runs a portable copy of the built executable.
  It launches the way Explorer does, with a relative path from a terminal, with
  no argument beside a `syntax.md`, and through the single-instance hand-off
  with a relative path from another folder. Each launch must open the expected
  document and leave its folder free to rename. The fixed build passes all five
  checks; the 3.7.4 build fails the three launch checks and passes the hand-off
  and after-exit checks.

## Desktop check

The 3.7.4 and fixed builds ran in portable mode from scratch folders:

| Launch | 3.7.4 | Fixed |
| --- | --- | --- |
| Document opened from its folder, as Explorer does | folder locked | free, document open |
| Relative path from a terminal | folder locked | free, document open |
| No argument, `syntax.md` beside it | folder locked | free, `syntax.md` loaded |
| Relative path handed to the running window | b.md opened as a tab | same |
| Ctrl+O from a third folder | free, c.md open | same |

On 3.7.4 the document file stayed deletable throughout, including after
Ctrl+F4.

## Exports

The new fixtures and `fixtures/reading-view-242.md` exported identically with
both builds; see `read-pill-validation.md`. Export paths resolve before the
process leaves the launch folder: `note.md --exporthtml note.html` and
`note.md --printpages pages`, run from the note's folder, wrote `note.html` and
`pages\page-1.png` there.

## Limits

- Pandoc is not installed on the test machine, so the Pandoc export and Locate
  Pandoc dialogs were not run. Ctrl+O did not move the working directory even
  on 3.7.4, so the dialog wrappers are a safeguard rather than an observed fix.
- The Microsoft Store build was not tested; it runs the same start-up code.
- The reporter wrote that the file could not be deleted. In testing only the
  folder was locked; the draft reply asks them to confirm.

## Draft reply (not posted)

### #253

Thanks for the report. It was not the file that stayed in use but its folder:
when a document is opened from File Explorer, Windows starts Tinta with the
document's folder as its working directory, and that folder stays in use for as
long as Tinta runs, so closing the tab did not help. In the next release Tinta
leaves that folder right after it starts, so you can delete or rename it while
Tinta is open. If it was really the file itself that could not be deleted,
please tell me what Windows showed.

感谢反馈。被占用的其实不是文件本身，而是它所在的文件夹：从资源管理器打开文档时，
Windows 会把文档所在的文件夹设为 Tinta 的工作目录，只要 Tinta 还在运行，这个文件夹
就一直被占用，所以关闭标签页也没有用。在下一个版本中，Tinta 启动后会立即离开该文件夹，
因此在 Tinta 运行时也可以删除或重命名它。如果你当时无法删除的确实是文件本身，请告诉我
Windows 显示的提示内容。
