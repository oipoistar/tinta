# Existing Markdown control

This document intentionally contains no math nodes. Its rendering should match the pre-extension build.

## Headings and inline styles

Plain text, **bold text**, *italic text*, ***bold italic***, ~~strikethrough~~, ==highlight==, `inline code` and [a local link](math-compatibility.md) must keep their spacing and decoration. This paragraph is long enough to wrap in both a normal document and a narrower window.

### Table

| Left | Center | Right |
| :--- | :---: | ---: |
| **Bold** | *Italic* | 123 |
| `a & b` | [Link](math-compatibility.md) | 456 |
| Wrapped text with several words | Ordinary text | 789 |

### Lists

- First item with **emphasis**.
  - Nested item with `code`.
- Second item with a link [here](math-compatibility.md).

1. First ordered item.
2. Second ordered item.

- [x] Completed task.
- [ ] Remaining task.

> A block quote with **bold** text.
>
> A second paragraph within the quote.

> [!NOTE]
> Existing callout text remains readable.

---

```cpp
int sum(int a, int b) {
    return a + b;
}
```

Literal math in code: `$x^2$`.

The final paragraph must remain visible.
