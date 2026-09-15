# Editor context menu

Double-click **selection** in this sentence, then right-click the highlighted word.
Copy and Cut should be enabled, and the selection should stay highlighted.

中文测试：双击这段中文，再在选中的文字上点击右键，复制应当保持可用。

A long English paragraph for wrapping: select text across several visual rows, then right-click in the middle of the selected area. The selection should survive at normal and narrow widths, with the preview shown or hidden, so copying and cutting use precisely the highlighted text. Right-clicking on unselected text or blank space beside a selected line should move the caret there and disable Copy and Cut until new text is selected.

## Surrounding content

| Feature | Example |
| --- | --- |
| **Bold table cell** | $a^2+b^2=c^2$ |
| Link and code | [Tinta](https://tinta.cc) and `inline code` |

> A quote with *emphasis*, **bold**, and $E=mc^2$.

- Select this list text with Shift+Arrow, then right-click inside it.
- Drag a selection backwards and check that Copy keeps the same range.

```cpp
// A fenced code block should retain its formatting.
int selected = 230;
```

## More selection cases

Unicode clusters: A😀é中文. Copying should preserve all characters.

Select a paragraph including its blank line. The highlighted newline cell
also belongs to the selection when opening the context menu.

Outside the selection, the context menu's insertion commands should still
insert Markdown at the right-click position.
