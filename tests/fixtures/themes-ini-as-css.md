# A themes.ini theme, drawn from its CSS file

Each section of `themes.ini` is written as a CSS file and the page is drawn
from that. This page holds what the two could disagree on: where one element
sits inside another and both have a colour of their own.

## Code inside other things

Plain `inline code`, a [link with `code` inside](https://example.com), a
[link with **bold `code`** inside](https://example.com), a link to a file
that is not there with [`code` in it](no-such-file-at-all.md), **bold with
`code`**, *italic with `code`*, ==marked `code`==, ~~struck `code`~~ and a
footnote[^one] after `code`.

### A heading with `code`, a [link](https://example.com), **bold** and *italic*

## Emphasis inside other things

**Bold**, *italic*, ***both***, a [link with **bold** and *italic*
words](https://example.com), ==marked **bold** and *italic*==, ~~struck
**bold**~~, and **bold with a [link](https://example.com) inside**.

> A quote with **bold**, *italic*, `code`, a [link](https://example.com) and
> ==marked words==.
>
> > [!TIP]
> > A tip inside it, with **bold** and `code`.

| Kind | Sample | Linked |
|:-----|:------:|-------:|
| **bold** | *italic* | [`code`](https://example.com) |
| ==marked== | ~~struck~~ | [[No such wiki page]] |
| `code` | ***both*** | [plain](https://example.com) |

- A bullet with **bold**, *italic* and `code`
  - [x] A done task with a [link](https://example.com)
  - [ ] An open one with ==a mark==

1. A step with H~2~O and x^2^
2. A step with <u>underlined</u>, <ins>inserted</ins> and <del>deleted</del> text

---

```cpp
// A comment
class Theme {
public:
    int keys(const std::string& section) {
        if (section.empty()) return 0;
        return 30 + static_cast<int>(section.size());
    }
    const char* name = "themes.ini";
};
```

Inline math $a^2 + b^2 = c^2$ and after it a rule.

---

[^one]: The footnote, with `code` and a [link](https://example.com).
