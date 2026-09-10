# Window-to-tab insertion: $A+B=C$

Open copies named **A.md**, **B.md**, and **C.md** in one window, and
**Incoming.md** in another. Drop Incoming between A and B: the order should be
**A, Incoming, B, C**, with Incoming active.

## Positions to check

| Drop location | Expected order |
| --- | --- |
| Before A | Incoming, A, B, C |
| Between A and B | A, Incoming, B, C |
| Between B and C | A, B, Incoming, C |
| After C | A, B, C, Incoming |

Repeat with a detached tab dragged from another window. The left and right
halves of a tab should select the gap before and after it.

## Tear off without leaving the window

Drag B about **50 pixels below the tab bar**, then release over this document.
It should open in a separate window at the drop position. You do not need to
drag past the window border. Repeat above the bar where screen space allows.
Dragging back near the bar before release keeps B in the original window;
Escape cancels the drag. Unsaved and untitled tabs remain protected.

> Keep a **dirty editor buffer**, `inline code`, ==highlight== and $x^2$
> intact while inserting a tab before the active editor tab.

- Narrow and wide windows; Paper and Midnight.
- Different monitor scales and a destination on the left-hand monitor.
- A single-document destination and a crowded tab strip.
- Dropping an already-open file activates its existing tab without a duplicate.
- Opening a file normally still appends it.

```cpp
const char* expected = "A, Incoming, B, C";
```

### Mixed content after the transfer

The [positions section](#positions-to-check), table and quote should remain
readable. The equation $\frac{a}{b}$ must retain its baseline and spacing.
