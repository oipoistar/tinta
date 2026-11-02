---
title: Watched note
status: draft
---

# Watched note

Another program changes this file while Tinta shows it: an AI agent, a
script or a second editor. **First version** of the note, with *emphasis*,
`inline code` and [a link](#steps).

## Steps

1. Open the note in Tinta, in the reader, Source or Live.
2. Change it somewhere else and save it there.
3. Tinta shows the new text without being asked, the caret where it was.

> A quote that stays as it is, so the change lands below it.

| Where | What happens |
| --- | --- |
| Reader | the page reloads |
| Editor, nothing unsaved | the text reloads, Ctrl+Z takes it back |
| Editor, unsaved changes | Tinta asks: Reload or Keep mine |

```python
def changed(path):
    return path.stat().st_mtime
```

- [ ] A task the agent ticks
- [ ] Another one

Last line: CHANGE_MARKER
