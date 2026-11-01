# Theme colours for **Bold_heading** and *Italic_heading*

Plain_text sits beside **Bold_plain**, *Italic_plain*, ***Both_plain***,
**bold with _Both_nested_ inside** and *italic with **Both_inner** inside*.
Underscores work too: __Bold_under__ and _Italic_under_.

A link keeps its colour: **[Link_in_bold](https://example.com)** and
[**Bold_in_link**](https://example.com); code keeps its own:
**`Code_in_bold`**; a mark inside bold: **==Mark_in_bold==**; bold inside
a mark: ==**Bold_in_mark**==. ~~**Bold_struck**~~ is struck through.

---

## Lists and tasks

- A bullet with **Bold_list** and *Italic_list*
  - A nested bullet with ***Both_list***
- [ ] A task with **Bold_task**
- [x] A done task with *Italic_task*

1. A numbered item with **Bold_numbered**
2. And [a plain link](https://example.com) with *Italic_numbered*

> A quote with **Bold_quote**, *Italic_quote* and ***Both_quote***.
>
> > [!NOTE]
> > A callout with **Bold_callout**.

***

| Cell | Sample |
| :--- | :--- |
| Bold | **Bold_table** |
| Italic | *Italic_table* |
| Both | ***Both_table*** |

```cpp
// **not bold** in a fence: Fence_text keeps the code colours
int main() { return 0; }
```

Inline math stays as it was: $a^2 + b^2 = c^2$, and a footnote[^note]
closes the page.

___

Final_paragraph with **Bold_last** after the third rule.

[^note]: A footnote with **Bold_footnote**, below the footnotes' own rule.
