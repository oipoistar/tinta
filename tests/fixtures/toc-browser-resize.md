# Level one

Open Contents with Tab and the file browser with B. Drag the small vertical
handle on each panel's inner edge. Widths should survive closing and reopening
Tinta; shrinking the window should leave space for this document.

Jump to [level four](#level-four), [level five](#level-five), or [level six](#level-six).

## Level two

**Bold**, *italic*, `inline code`, x^2^, H~2~O and $a+b=c$.

### Level three

| Heading | Expected in Contents |
| --- | --- |
| H1 through H6 | All six levels |
| A fenced fake heading | Excluded |

#### Level four

> A quote with **bold**, `code`, H~2~O and $x^2$.
>
> This second paragraph must keep its normal spacing.

##### Level five

- A list with a [level six link](#level-six).
- A [companion document](toc-browser-companion-with-a-long-file-name.md).

###### Level six

```markdown
#### This fenced heading must not appear in Contents
```

## Repeated title

This heading shares its title with a deeper heading below. Jump to the
[second occurrence](#repeated-title-1).

#### Repeated title

The second occurrence must have its own anchor and Contents row.

#### A long deep heading with enough words to exercise a narrow Contents panel

$$
\frac{x^2 + y^2}{H_2O}
$$

Final paragraph with **bold** and ordinary text. Resizing panels must not edit
this source, follow a link, select a heading, or open a file on mouse release.
