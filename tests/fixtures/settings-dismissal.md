# Settings dismissal: $E = mc^2$

Open **Settings** with **Ctrl+,** or the top-left icon menu.

1. Click inside the dialog's blank space: it stays open.
2. Click the dimmed document outside it: Settings closes and the click is consumed.
3. Open Settings again, then click its small **×** in the top-right corner.
4. Change a setting and close the dialog; the choice should remain applied.

## Mixed Markdown stays intact

| Feature | Example |
| --- | --- |
| Inline code | `settings_close` |
| Math | $\frac{1}{2}$ |
| Emphasis | **bold**, *italic*, and a [local link](settings-dismissal.md) |

> A quote with `inline code` and $a+b=c$ keeps its corrected bar height.

```text
One code row without a phantom blank row.
```

- Repeat dismissal while editing an unsaved change; text and caret stay put.
- Open a language dropdown. Clicking blank space inside Settings closes only the dropdown.
- Drag the reading-width slider and release outside the dialog; this ends the drag.
- Clicking outside Settings over the + button must not create a tab.

$$
\begin{bmatrix}1 & 2 \\ 3 & 4\end{bmatrix}
$$
