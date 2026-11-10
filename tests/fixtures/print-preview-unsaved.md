# The print preview over unsaved edits

Open this file, press **E** and type a few words. Do not save. Press
**Ctrl+P**: the preview shows the page as edited. Then, with the preview
still showing, try each of these:

1. Close the window, with Alt+F4 or from the taskbar. Tinta asks about the
   unsaved changes. *Keep editing* returns to the editor with the words.
2. Change this file in another program and save it there. Tinta asks:
   Reload, or Keep mine.
3. Drop another Markdown file on the window. It opens in a tab of its own,
   and this tab keeps its words and its dot.

| Asked while the preview shows | What happens |
|:------------------------------|:-------------|
| Close the window | the unsaved-changes dialog |
| Close the tab | the same dialog |
| The file changes on disk | Reload or Keep mine |
| Another file opens | the edits stay with their tab |

> Before, each of these read the document as having nothing unsaved, since
> the preview puts the editor aside while it lays the page out.

```text
The recovery draft is kept while the preview shows, too.
```
