# Find and Replace while the text changes

Open this file in the Source editor (**E**). Each step used to end the
program or replace the wrong words; now the marks follow the text.

1. Select `bold` in the line below, press **Ctrl+H**, type `ab`, press
   Tab and type `x`. Click **B** on the tool rail, then press Enter in the
   Replace field: the line reads `bold x`.

**bold** ab

2. Press **Ctrl+F** and type `target`. Click into the text and type a few
   lines above this list. The mark stays on the word, and the count stays
   right.

| Word | Where |
|:-----|:------|
| target | in a table cell |
| *target* | in emphasis |

> A quote with the target in it, and `target` as code.

```text
target, in a code block
```

3. With Find still open on `target`, shorten this file in another program
   and save it there. After Tinta has taken the change, press Enter in the
   Find field: nothing happens, and typing goes on in the text.
