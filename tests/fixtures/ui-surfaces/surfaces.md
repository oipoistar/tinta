---
title: The interface's surfaces
tags: [interface, scale]
---

# The interface's surfaces

A page to open the interface's surfaces over, one after the other. Each is
built from one scale of spacings and type (`include/ui_scale.h`), and this
page gives them real content to stand on: headings for the contents panel,
words to search for, a table, lists, a quote and code.

Copy `themes.ini` from this folder beside a portable `tinta.exe` to give the
theme chooser eighteen more themes than it has room for.

## Theme chooser

Press `T`. Every card is the same size whatever the number of themes; with
more than fit, the two columns scroll with the wheel and the title, the
columns' labels and the footer stay. The theme in use is in sight when the
dialog opens. A long name ends before the check at the card's corner.

## Menus

Right-click the page for the reading view's menu; press `E` and right-click
for the editor's. Both have rows of one height, their shortcuts in the
interface's face, and the same corners.

## Side panels

`Tab` opens the contents, `B` the folder browser, `Ctrl+Shift+F` the search
results. Their titles, rows, section labels and footers share sizes.

| Surface       | Opens with      | Scrolls |
|---------------|-----------------|---------|
| Theme chooser | `T`             | yes     |
| Contents      | `Tab`           | yes     |
| Browser       | `B`             | yes     |
| Results       | `Ctrl+Shift+F`  | yes     |
| Help          | `F1`            | yes     |

## Help and find

`F1` lists the shortcuts; `F` opens the find bar. Search for *surface* to
fill the results panel: surface, Surface, surfaces.

> A surface on the scale uses no length of its own for a gap, a row or a
> corner.

- [x] Light and dark: Paper and Midnight
- [ ] Narrow: drag the window to about 650 wide
  1. the dialogs keep to the window
  2. long names end in an ellipsis

```cpp
// include/ui_scale.h
constexpr float sp8 = 8.0f;
```

### A third level heading

Text with a [link](#menus), ==marked words==, `inline code`, H~2~O and x^2^.

#### A fourth level heading

The last paragraph, so that the contents panel has four levels to show.
