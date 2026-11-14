# The view a file opens in

Tinta opened every file in the reading view. **Settings → Editor →
Open Markdown files in** now says where a Markdown file opens: the
reading view, or the editor in *Live* or on its *Source*.

| Setting | A file opens                | The way back or on            |
|---------|-----------------------------|-------------------------------|
| Reading | on the page, to be read     | `E` opens the editor          |
| Live    | in the editor, on the page  | `Esc` returns to the page     |
| Source  | in the editor, on its text  | `Ctrl+/` goes over to Live    |

## What to try

1. Pick **Live**, then open this file again: from Explorer, with
   `Ctrl+O`, by dropping it on the window, or from the folder browser
   (`B`). The caret is on the page and a letter typed lands in it.
2. Pick **Source** and open [the notes](open-mode-322-notes.md). The
   Markdown is there as typed.
3. Follow [a link to a heading](open-mode-322-notes.md#second-part) or
   the wiki link [[open-mode-322-notes]]: the note opens in the editor
   too.
4. Press `Esc` to read, go to another tab and come back: the tab is as
   you left it.

> A diagram (`.mmd`, `.puml`), a text file and a sheet open as they
> always did. So does a file opened from the find results, which opens
> to show its match.

- [x] the choice is kept in `settings.ini` as `openMode=`
- [ ] `E`, `Esc` and the rail's pill work as before
  - nested: H~2~O, x^2^, ==marked==, ~~struck~~, a [link](#what-to-try)

```ini
[Settings]
openMode=live
```

A last line with `inline code` to end the page.
