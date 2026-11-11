# Settings on the interface's scale

Open **Settings** with `Ctrl+,` over this page. The dialog is built from one
scale of spacings and one of type, and this page is what stays visible
behind it: headings, a table, lists, a quote and code, so that the dialog's
shade, its edge and its cards can be judged against real content.

## What to look at

1. Each page is a few **groups**, each under a small upper-case label, and a
   group is one card whose rows are parted by hairlines.
2. A row has its *name*, a line of description in smaller, softer letters,
   and its control at the right.
3. The title, the names, the descriptions and the labels are four sizes
   apart: 20, 13, 12 and 11.
4. Narrow the window: under about 680 wide the sections go across the top,
   and narrower still the four theme buttons move under their label.
5. Shorten the window: the page scrolls between the title and the footer
   with the wheel, and the footer stays where it is.

| Page       | Groups                                 | Rows |
|------------|----------------------------------------|-----:|
| General    | Interface, Files and tabs, config files |    7 |
| Appearance | Reading, Theme, Window                 |    8 |
| Editor     | Editing, External tools                |    8 |

> The front matter page keeps its own layout for now; only the frame around
> it, the title and the sections, is on the scale.

- [x] Light and dark: try `T` and pick Midnight, then open Settings again
- [ ] Another language: General, Interface language
  - German and Russian have the longest names
  - Chinese, Japanese and Korean take their own faces

```ini
; what the switches write
[Settings]
headingRules=1
showNotifications=1
```

A [link](#what-to-look-at), ==marked words==, `inline code` and H~2~O with
x^2^ fill the last line behind the dialog's lower edge.
