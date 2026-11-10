# The theme at a glance

A paragraph of body text with **bold**, *italic*, ***both***, `inline code`,
a [link to somewhere](https://example.com), ==marked words== and ~~struck ones~~.
It runs long enough to wrap, so the line height and the measure can be
judged at a normal width as well as a narrow one.

## Lists and tasks

- A bullet, with a second line of thought that follows it down the page
  - A nested bullet
    - And one more level
- [x] A task that is done
- [ ] A task still open

1. A numbered step
2. And the next

> A quote, set off from the text around it.
> Its second line, with *emphasis* and a `code span`.

> [!NOTE]
> A note, for what the reader should know.

> [!WARNING]
> A warning, for what could go wrong.

### A table

| Theme | Kind | Mood |
|:------|:----:|-----:|
| Paper | light | warm |
| Midnight | dark | calm |
| **Bold cell** | `code` | 3.14 |

#### Code

```cpp
// A comment that explains
class Oracle {
public:
    int answer(const std::string& question) {
        if (question.empty()) return 0;   // control flow
        return 42 + static_cast<int>(question.size());
    }
    const char* name = "deep thought";
};
```

Inline math $E = mc^2$ and a display equation:

$$
\int_0^1 x^2\,dx = \frac{1}{3}
$$

---

##### A small heading

Text after the rule, and a footnote reference[^1].

[^1]: The footnote itself.
