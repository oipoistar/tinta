# Tabs from last time (#323)

Tinta starts with the tabs it was closed with. **Settings → General →
Reopen tabs from last time** turns that off: a file opened from Explorer
then comes up *alone*, and a launch with no file shows the start page.

| `reopenTabs=` | A launch with a file          | A launch with none                 |
|---------------|-------------------------------|------------------------------------|
| `1` (default) | the last tabs, and the file   | the last tabs                      |
| `0`           | the pinned tabs, and the file | the pinned tabs, or the start page |

## Try it

1. Open this file, then [the notes](reopen-tabs-323-notes.md) beside it.
2. Open [the log](reopen-tabs-323-log.md) as a third tab.
3. Close Tinta and open **only this file** again: with the setting on,
   three tabs are back; with it `off` there is one.
4. Pin the notes (*right-click the tab → Pin tab*), close, and open this
   file once more: the notes are back, pinned, however the setting stands.

> A pinned tab stays when the others are closed, so it stays here too.

## What is kept all the same

- [x] The choice itself, the moment it is toggled
- [x] Unsaved text: a draft comes back by itself
- [ ] The place each file was read to, H~2~O and x^2^ included
- A file that was ~~forgotten~~ closed is still among the ==recent files==

```ini
[Settings]
openInTabs=1
reopenTabs=0
```

See also [[reopen-tabs-323-notes]] and <https://github.com/oipoistar/tinta/issues/323>.
