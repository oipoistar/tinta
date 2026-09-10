# A calmer reading space

This document combines everyday Markdown with six heading levels. Open Contents
with **Tab** and the file browser with **B**. Try both panels together.

## Panel edges

At rest, the document and side panels should meet at a thin divider. Hover over
that divider to find the resize cursor, then drag to change the width.

The document scrollbar appears when you scroll or hover near it, then fades.
Contents marks the current heading with a short accent beside its row.

### Mixed formatting

**Bold**, *italic*, `inline code`, x^2^, H~2~O, and $a+b=c$ should remain readable
when the document gets narrower.

| Check | What to look for |
| --- | --- |
| Panel header | Title and buttons fit without overlapping |
| Long headings | A single line with an ellipsis when needed |
| Document | Tables and paragraphs reflow as the panels resize |
| Math | $E=mc^2$ keeps its usual appearance |

#### A quoted passage

> A quote with **bold**, `code`, H~2~O, and $x^2$.
>
> This second paragraph should keep its normal spacing.

##### A short reading list

- Try the Contents panel on either side of the document.
- Narrow and widen the window with both panels open.
- Compare the same layout in Paper and Midnight.
- [Jump to the conclusion](#conclusion).

###### A deep heading with a deliberately long label to check the available space

The deeper rows should remain readable and clickable, without a tall red rail
beside the document scrollbar.

```markdown
###### A fenced heading must not become a Contents entry.
```

## Equations

$$
\frac{x^2+y^2}{H_2O}
$$

Move the divider past this formula and check the surrounding text as well.

## Conclusion

The short active-heading marker should follow your reading position. Resizing
must not select text, open a file, or follow a link when you release the mouse.

[Open the companion document](toc-browser-companion-with-a-long-file-name.md).
