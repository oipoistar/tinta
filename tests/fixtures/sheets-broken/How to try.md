# Sheets that used to break

Three things to try by hand, each beside this file.

1. **Cut short.smd** ends inside its canvas block, as a sync or a partial
   copy leaves a file. It opens read-only: nothing done on it is saved.
   Before, it opened as an empty sheet and the first change saved that
   over the two cards left in the file.
2. **Twin groups.canvas** has two groups with the same box and a card in
   them. Press **Ctrl+L** (Tidy): the sheet is tidied. Before, Tinta hung.
3. On any sheet, copy these two lines and paste them with **Ctrl+V**:

```text
flowchart TD
    A --- A
```

   One card arrives and no arrow, and the sheet's own arrows keep their
   heads. Before, the last arrow on the sheet lost its head, or Tinta
   ended when the sheet had none.

| File | What is wrong with it |
|:-----|:----------------------|
| Cut short.smd | the canvas block never closes |
| Twin groups.canvas | two groups, one box |

> A sheet larger than its window also drew itself sixty times a second
> while it stood still. Nothing to see there; the fan was the sign.
