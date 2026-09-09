# Copy file path: $E = mc^2$

Regression fixture for [#203](https://github.com/oipoistar/tinta/issues/203).
Right-click the document and choose **Copy file path**, beside **Reveal in Explorer**.
Paste into an untitled note to verify that the clipboard contains this document's
absolute path, including its extension, spaces and any Unicode characters.

## Existing formatting

| Feature | Example |
| --- | --- |
| Inline code | `clipboard_path` |
| Math | $\frac{1}{2}$ |
| Emphasis | **bold**, *italic* and a [link](https://example.com) |

> A quote with `code` and $a+b=c$ should retain its corrected bar height.

- Copy the active document's path.
- Right-click an inactive tab and copy that tab's path without switching tabs.
- After Save As, copy the new path.
- An untitled note has no file path; its tab command should be disabled.

```text
A code block with one row.
```

$$
\begin{bmatrix}1 & 2 \\ 3 & 4\end{bmatrix}
$$

Use the [companion document](copy-file-path-companion.md) for the inactive-tab check.
