# Wrapped cursor selection: issue 224

Start at the beginning of either long paragraph. Press Down repeatedly, then repeat with Shift+Down. With wrapping enabled, the caret and selection should advance through every visual row. Try both narrow and wide editor panes; disable wrapping as a control.

A long English paragraph should wrap over several visual rows while keeping arrow navigation usable from the first column. This sentence deliberately repeats enough ordinary words to span a narrow pane and a wider pane, and the cursor should continue down through the entire paragraph instead of stopping at its first wrap boundary. Headings, tables, quotes and mathematics below provide surrounding Markdown content for later regression checks.
After the English paragraph.

中文段落用于检查自动换行之后的光标移动和连续选择文本。把光标放在这一行的开头，连续按向下箭头，然后按住Shift并连续按向下箭头。光标应该经过每个显示行，不应该停在自动换行的位置。继续重复一些中文文字以保证在不同宽度的编辑窗口中都能够产生多次自动换行，并检查表格和标题是否保持正常。
After the Chinese paragraph.

Unicode clusters: 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文 😀 é 中文.
After the Unicode paragraph.

## Surrounding content

| Feature | Example |
| --- | --- |
| Emphasis | **bold** and *italic* |
| Formula | $a^2+b^2=c^2$ |

> A quote with `inline code` and a [link](https://example.com).

- First list item
- Second list item

```cpp
int value = 224;
```

$$
\sum_{k=1}^{n} k = \frac{n(n+1)}{2}
$$
