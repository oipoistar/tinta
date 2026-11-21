# Middle-click autoscroll

Click the mouse wheel anywhere on this page. A round mark with two arrows
appears where the click fell. Move the pointer below the mark and the page
scrolls down; move it above and the page scrolls up. The further the
pointer is from the mark, the faster the page goes. Rest the pointer on the
mark and the page stands still.

Any click, any key or a turn of the wheel ends it. So does **Esc**, which
does nothing else.

## Holding the button instead

Press the wheel, keep it down and drag: the page scrolls while the button
is held and stops where it is let go. A press that is let go again without
leaving the mark is the click above.

1. Middle-click on this line.
2. Move the pointer a finger's width below the mark: the page creeps, a
   line or so a second, slow enough to read by.
3. Move it to the bottom of the window: the page runs.
4. Bring it back to the mark: the page stops, and the mark stays.
5. Click, or press a key: the mark goes.

## Things on the way down

The page is long on purpose. It has what a document has, so that the
scrolling passes over every kind of block.

> A quote of two lines, with *emphasis*, **strong text** and `inline code`,
> and a [link to the top](#middle-click-autoscroll) that is not followed by
> a middle click.

- A list item with a [link](https://example.com/one)
- A second item
  - A nested item with ~~struck~~ text
  - And one with ==marked== text
- [ ] An open task
- [x] A task that is done

| Where the click falls | What happens |
|-----------------------|--------------|
| On the page | The page scrolls towards the pointer |
| On the editor's text | The text scrolls towards the pointer |
| On a tab | The tab closes, as before |
| On a sheet | The sheet pans while the button is held, as before |
| On a page that fits its window | Nothing: there is nowhere to go |

### A wide block

With **Wrap long lines in code blocks** off, the line below is longer than
the page and the page scrolls sideways too. The mark then has four arrows,
and the pointer pulls the page left and right as well.

```cpp
const std::vector<std::pair<std::string, std::string>> shortcuts = {{"middle click", "autoscroll"}, {"Ctrl+PgDn", "next tab"}, {"Ctrl+PgUp", "previous tab"}, {"F3", "next match"}, {"Shift+F3", "previous match"}, {"Esc", "ends autoscroll"}};
for (const auto& [keys, what] : shortcuts) {
    std::printf("%-14s %s\n", keys.c_str(), what.c_str());
}
```

## Speed

The speed grows with the distance from the mark, gently at first and then
steeply, and is the same on every screen scale: at 150% the same hand
movement scrolls the same share of a line.

| Distance past the mark | Speed |
|-----------------------:|------:|
| 10 | 20 a second |
| 50 | 200 a second |
| 100 | 650 a second |
| 200 | 2,300 a second |
| 300 | 4,950 a second |

Distances and speeds are in the pixels of a 100% screen.

## Filler, to scroll through

### One

The first of several short sections that are here to be scrolled past. A
paragraph of plain prose is the commonest thing a reader meets, and the one
that has to stay readable while it moves: the text is drawn on whole pixels
at every step, so the letters keep their edges.

### Two

Türkçe bir satır: İstanbul'da ılık bir akşam, çay ve simit. 中文的一行：
用鼠标中键点击页面，然后移动指针即可滚动。 日本語の一行：
マウスの中ボタンでクリックして、ポインターを動かすとスクロールします。

### Three

1. First
2. Second
3. Third
   1. Third, first
   2. Third, second

### Four

> [!NOTE]
> A note in a quote. It scrolls like everything else.

### Five

```python
def speed(past):
    """Pixels a second, for a pointer `past` pixels beyond the mark."""
    return min(1.5 * past + 0.05 * past * past, 15000)
```

### Six

A paragraph with a footnote[^one] and some math, $v = 1.5d + 0.05d^2$, set
in the line.

[^one]: The footnote sits at the end of the page, where the scrolling stops.

### Seven

| A | B | C |
|---|---|---|
| 1 | 2 | 3 |
| 4 | 5 | 6 |
| 7 | 8 | 9 |

### Eight

More prose. The page stops cleanly at its end: the last line comes to rest
at the bottom of the window and stays there, however far below the mark the
pointer is, and it starts back up as soon as the pointer goes above the
mark.

### Nine

- alpha
- beta
- gamma
- delta
- epsilon

### Ten

The same again at the top: scrolled up as far as it goes, the page rests on
its first line.

### Eleven

One more block of text, because a long page is the point of this file, and
a short one would not show the speed at the far end of the curve at all.

### Twelve

And a last one. If the page stopped here with the pointer still far below
the mark, the end is where it should be.

## The end

The last line of the page.
