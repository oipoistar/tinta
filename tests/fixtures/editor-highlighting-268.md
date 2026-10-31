---
title: Highlighting tour
tags: [editor, "syntax"]
draft: true
---

# Highlighting tour

A paragraph with **bold**, *italic*, `inline code`, ~~struck~~, ==marked==
words, a [link](https://example.com/tour "Its title"), a [[Plans]] note, an
image ![Board mock](mock.png), a footnote[^1], an escaped \*star\* and an
address https://example.com/bare.

Setext heading
==============

A second setext heading
over two lines
--------------

## Lists and tasks

- First item with `code` and *emphasis*
- Second item
  - Nested item with a [link](./other.md)
1. One
2. Two
- [ ] An open task
- [x] A finished task

> A quote with **bold** inside.
> ## A heading in a quote

> [!NOTE]
> A callout's words.

| Name | Kind | Notes |
| --- | :---: | ---: |
| Paper | theme | `light` |
| Midnight | theme | a \| pipe |

## Code

```cpp
int main() {
    /* a block comment
       over lines */
    // a comment
    return 42;
}
```

```python
def greet(name):
    """A docstring
    over lines"""
    return f"Hello {name}"
```

~~~
# Not a heading: plain code
- not a list
~~~

````markdown
```js
const inner = true;
```
````

```unknown-language
plain code in the code colour
```

## Maths and markup

Inline $e^{i\pi} + 1 = 0$ and a block:

$$
\int_0^1 x^2 \, dx
$$

A <kbd>Ctrl</kbd> key, a <https://example.com/angle> address and an
inline <!-- comment --> here.

<!--
A comment
over three lines
-->

***

[^1]: The footnote's text with *emphasis*.
[ref]: https://example.com/reference "A reference"

The last line.
