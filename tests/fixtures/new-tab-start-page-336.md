# New tab opens the start page (#336)

The **+** button at the end of the tab strip opens the start page in a new
tab, as `Ctrl+T` does. It used to open an empty note in the editor, which
left no way to pick a recent file from a new tab.

| Action                  | Result                                             |
|-------------------------|----------------------------------------------------|
| Click **+**             | a new tab on the start page, in the reading view   |
| `Ctrl+T`                | the same                                           |
| `Ctrl+N` or **New document** (`N`) | an untitled note, in the editor         |
| Middle-click **+**      | nothing                                            |

## Try it

1. Open this file and at least one other, so the tab strip shows.
2. Click **+**. The start page appears in a new tab with the recent files
   listed; the editor does not open.
3. Click a recent file. It opens in that same tab.
4. Click **+** again, then **New document** (`N`) on the start page. An
   untitled note opens in the editor in that tab.
5. Click **+**, then close the tab with `Ctrl+W`. `Ctrl+Shift+T` has nothing
   to reopen: the start page is not remembered.

> Check the surrounding content too: the strip's other tabs, their
> *emphasis*, `inline code` and the [link to itself](new-tab-start-page-336.md)
> must not change when the new tab opens.

```text
+  ->  start page   (recent files, Open, New document)
```

- Settings open over the **+** button: a click there closes Settings and
  creates no tab.
- Narrow window: **+** moves beside the tab-overflow chevron and behaves
  the same.
