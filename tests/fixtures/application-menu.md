# Application menu: mixed document

Open a **copy** of this fixture for issue [#194](https://github.com/oipoistar/tinta/issues/194).
Click the top-left Tinta icon in reading and editing modes. The menu must sit
above this document and the editor rail, and dismiss with Escape, another icon
click, or a click outside. F10, arrow keys and Enter also operate the menu.

## Table and inline math $E = mc^2$

| Action | Expected result | Formula |
| --- | --- | --- |
| Open File | Another file opens without losing an unsaved tab | $x^2 + y^2$ |
| Save | Edits are written; an untitled note asks for a name | $\frac{1}{2}$ |
| Save As | A new copy becomes the active file | $\sqrt{a+b}$ |

## Existing formatting

- **Bold**, *emphasis*, and `inline code` remain intact.
- [Local companion](application-menu-companion.md) opens normally.
- A nested list still works:
  1. Open Settings while editing, type a few keys, then close with Escape.
  2. Confirm the keys did not change the source or mark a clean file dirty.

> Theme and Help must cover both panes, including with the preview hidden.
> Try normal and narrow windows, light and dark themes, and pinned Contents.

$$
\begin{bmatrix} a & b \\ c & d \end{bmatrix}
\begin{pmatrix} x \\ y \end{pmatrix}
= \begin{pmatrix} ax+by \\ cx+dy \end{pmatrix}
$$

```latex
\begin{bmatrix} this & stays \\ literal & code \end{bmatrix}
```

## File safety checks

1. Create a new note from the menu, type text, and open the companion file.
2. Switch back: the unsaved note and its text must still exist.
3. Save it, edit again, choose Save As, and verify both files on disk.
4. Cancel an Open or Save As picker: the current document stays active.
5. Choose Exit with an unsaved tab: the existing save/discard dialog appears.

Print preview and HTML export must keep the headings, table, code and equations.
The document right-click menu and editor insert menu must keep working, and the
empty title-bar space must still drag the window.
