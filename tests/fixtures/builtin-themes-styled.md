# Built-in themes, drawn from their sheets

Each built-in theme is a CSS file, and its rules style the page as a theme
of your own does. This page holds what a theme's sheet could get wrong while
doing so: every part here keeps the colour and the place it had before.

## Callouts keep their own colours

> [!NOTE]
> A note, in the note's blue: its bar and its title.

> [!TIP]
> A tip, in green, with **bold**, *italic* and `code` inside.

> [!IMPORTANT]
> Important, in purple.

> [!WARNING]
> A warning, in amber, with a [link](https://example.com) and ~~struck~~ words.

> [!CAUTION]
> Caution, in red.

> A plain quote beside them takes the theme's bar.
>
> > [!NOTE]
> > And a note inside a quote is still a note.

## Links, found and missing

A [web link](https://example.com), a link to [a file that is not
there](no-such-file-here.md), a wiki link to [[No such page anywhere]] and
one footnote reference[^first] beside a second[^second].

### A heading with a [link](https://example.com), ~~struck~~ and ==marked== words

Body text with ~~struck words~~, ==marked words==, ~~==both at once==~~,
H~2~O and x^2^, <u>underlined</u>, <ins>inserted</ins> and <del>deleted</del>
text, and `inline code with ~~no strike~~ inside`.

| Kind | Sample | Note |
|:-----|:------:|-----:|
| struck | ~~old price~~ | **new** |
| marked | ==look here== | `code` |
| link | [missing](also-not-there.md) | [found](https://example.com) |

- A bullet with ~~struck~~ text
  - [x] A done task, ~~struck through~~
  - [ ] An open one with a footnote[^first]

1. First, with `code`
2. Second, with a [[Missing wiki page]]

```python
def strike(word):  # a comment
    return "~~" + word + "~~"
```

Inline math $a^2 + b^2 = c^2$ sits in ~~a struck $x^2$ phrase~~ too.

---

[^first]: The first footnote, with a [link](https://example.com).
[^second]: The second, ~~struck~~.
