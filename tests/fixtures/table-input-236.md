# Table editing and keyboard layouts

Edit this document without saving, then use **Read · Ctrl+Shift+E** to inspect the live
render at full width. Return with **Edit · Esc**; undo and unsaved text must remain.

## Reporter example

| 序号 | 编号 | 描述 | 原因 |
| --- | --- | --- | --- |
| 1 | WB202609171624520001 | 中文测试 | A long description that wraps in a narrow column. |
| 2 | | | |
| 3 | alpha beta | 😀 é 中文 | An escaped pipe: a\|b |
| 4 | | | |

Click between characters, drag across text, use Shift+arrows, Ctrl+A/C/X/V,
and undo with Ctrl+Z. Pasting into row 2 should fill the cell, leaving the
source caret and the table separators alone. Tab commits and moves between cells.

## Surrounding content stays intact

- A list item with **bold**, *italic* and `inline code`.
- [ ] A task beside $a+b=c$.

> [!TIP]
> The table editor must not capture input after clicking the source or Find.

```cpp
int answer = 42;
```

| Another | Table |
| --- | --- |
| Keep | these cells |

### AltGr exercise (#235)

On a layout where AltGr+W produces ✓, type it here. Word wrapping must not
toggle. Ctrl+Alt+F must not open Find; Ctrl+Shift+W must not toggle wrapping.
Plain Ctrl+W still toggles wrapping; Ctrl+Shift+S still opens Save As.

Final paragraph with a [relative link](shortcuts-layouts.md).
