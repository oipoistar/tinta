# Reading view and Esc (#242)

Open this file next to a second tab, press **E** to edit, and check:

- **Esc** once shows *Press ESC again to exit edit mode*; a second **Esc** returns to the reader with the tab strip intact.
- Type a character, then **Esc**: the *Save / Discard / Keep editing* dialog appears.
- **Read · Ctrl+Shift+E**, the button or the chord, opens the full-width reading view of unsaved edits. The tabs stay visible, the empty title bar drags the window, and the page starts below the strip.
- **Esc** or **Edit** in the reading view returns to the editor with text, caret and undo intact.

## Keys at a glance

| Key | In the editor | In the reading view |
| --- | --- | --- |
| Esc | Leaves edit mode, twice when clean | Back to the editor |
| Ctrl+Shift+E | Opens the reading view | Back to the editor |
| E | Types *e* | Back to the editor |

## Mixed content

1. A numbered item with `inline code` and a [relative link](table-input-236.md).
2. A second item with **bold**, *italic*, ~~strikethrough~~ and ==highlight==.

- [x] Tab strip visible in the reading view
- [ ] Title bar drags the window in the reading view

> A quote long enough to wrap across the reader's full column in the reading
> view, and inside the floating sheet in the split editor.

中文段落：阅读视图应与阅读模式一致，标签栏保持可见。

```cpp
// Code keeps its own background in both views
int main() { return 0; }
```

Inline math such as $e^{i\pi} + 1 = 0$ stays inline, and display math stays centered:

$$\int_0^1 x^2\,dx = \frac{1}{3}$$

### Final heading

The last line proves the whole document laid out.
