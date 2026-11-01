# Touch gestures (#286)

alpha beta gamma
中文选择测试

In the source editor, hold **beta**, slide to gamma or back to alpha,
then lift: the whole words stay selected when the menu opens. Sliding
down to the Chinese line extends across the line break. Canceling a
hold keeps its selection and never moves the text.

On a touchscreen a finger dragged across this page scrolls it instead of
selecting text, and a flick carries the page on after the finger lifts.
A tap follows a link: [jump to the end](#the-end). A double tap selects a
word, such as marigold. Hold a finger on lavender until it is selected,
then slide to rosemary: the words between join the selection, and the
context menu opens where the finger lifts.

> The Contents (**Tab**), the file browser (**B**) and the search results
> (**Ctrl+Shift+F**) pan with a finger too. The mouse still selects text,
> and the scrollbars still drag.

Flick the page, then catch it with a finger over the [jump link](#the-end):
the first press must only stop the page. A second deliberate tap follows
the link. A catching finger can also continue straight into a new pan.
The same braking rule applies to the source editor and scrolling lists;
touching a different control still operates that control immediately.

In split edit mode, tap **jump to the end** in the preview. Both panes
should scroll, and the destination must remain visible on subsequent
repaints. The source text, unsaved state and caret must remain unchanged.

With split editing open, tap the wide table's **Fit** and **Copy TSV**
buttons, including while a header cell is being edited. The buttons must
remain on top and operate without opening the cell underneath. Ordinary
cell taps should still open the inline editor.

For control routing, enter **Edit** with split preview, open Find/Replace,
and narrow the window until Replace / All move below the replacement
field. Both buttons must remain tappable. Open the source context menu
near the split: its table submenu must work over the preview, and tapping
outside must only dismiss it. In reading mode, drag either Contents
resize edge with a finger; the edge must resize rather than scroll.

For tap cleanup, tap a word, pan, and quickly tap the same word again:
it must start a fresh tap rather than select a word. In Contents, tap two
adjacent rows near their shared boundary; each tap follows its own row.
Switch focus or editing mode during a held press or fling: no delayed
menu or scrolling should continue in the new context. Normal deliberate
double and triple taps must still select words and lines.

In split editing, hold lavender in the rendered preview and extend to
rosemary. Lifting should open a copy menu and keep the selected words.
Holding table text should select it too; a normal tap still edits a cell.

Open Help from the application menu: swipe and flick its shortcut list.
The document underneath must stay still. Tap the empty margin on either
side to close Help; that tap must not activate the document underneath.

To leave split editing without a keyboard, tap the top-left application
logo and choose **Exit editing**. A clean document returns to reading
immediately. With unsaved edits, test **Keep editing** first; then choose
Save or Discard deliberately. The document tab must stay open.

On a sheet, a double tap whose second tap lands a little off the first
still writes a note there, and a finger held on the canvas opens the
sheet's menu where it lifts. In a card being typed in, and on the page
with **Live** on, a held finger selects the word under it and slides on by
words, keeping that word if it slides back, and the menu opens over the
words where it lifts. While that menu is up, a finger works it as the
mouse does, and the page under it stays still.

| Bed 1 | Bed 2 | Bed 3 | Bed 4 | Bed 5 | Bed 6 | Bed 7 | Bed 8 | Bed 9 | Bed 10 | Bed 11 | Bed 12 | Bed 13 | Bed 14 | Bed 15 | Bed 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `12` cm | `24` cm | `36` cm | **Chard** | `60` cm | `72` cm | `84` cm | **Beans** | `108` cm | `120` cm | `132` cm | **Chard** | `156` cm | `168` cm | `180` cm | **Beans** |

- A list item with *emphasis*
- Another with `inline code`
  - and a nested one

```python
def water(bed):
    # beds=000,001,002,003,004,005,006,007,008,009,010,011,012,013,014,015,016,017,018,019,020,021,022,023,024,025,026,027,028,029,030,031,032,033,034,035,036,037,038,039,040,041,042,043,044,045,046,047,048,049,050,051,052,053,054,055,056,057,058,059,060,061,062,063,064,065,066,067,068,069
    return bed.moisture < 0.3
```

## Section 1: Kale

Section 1 is about *kale*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 2: Leeks

Section 2 is about *leeks*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 3: Peas

Section 3 is about *peas*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 4: Chard

Section 4 is about *chard*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about chard.

## Section 5: Onions

Section 5 is about *onions*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

1. Sow
2. Thin the onions
3. Harvest

## Section 6: Squash

Section 6 is about *squash*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 7: Radish

Section 7 is about *radish*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 8: Beans

Section 8 is about *beans*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about beans.

## Section 9: Kale

Section 9 is about *kale*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 10: Leeks

Section 10 is about *leeks*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

1. Sow
2. Thin the leeks
3. Harvest

## Section 11: Peas

Section 11 is about *peas*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 12: Chard

Section 12 is about *chard*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about chard.

## Section 13: Onions

Section 13 is about *onions*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 14: Squash

Section 14 is about *squash*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 15: Radish

Section 15 is about *radish*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

1. Sow
2. Thin the radish
3. Harvest

## Section 16: Beans

Section 16 is about *beans*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about beans.

## Section 17: Kale

Section 17 is about *kale*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 18: Leeks

Section 18 is about *leeks*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 19: Peas

Section 19 is about *peas*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 20: Chard

Section 20 is about *chard*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about chard.

1. Sow
2. Thin the chard
3. Harvest

## Section 21: Onions

Section 21 is about *onions*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 22: Squash

Section 22 is about *squash*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 23: Radish

Section 23 is about *radish*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 24: Beans

Section 24 is about *beans*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about beans.

## Section 25: Kale

Section 25 is about *kale*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

1. Sow
2. Thin the kale
3. Harvest

## Section 26: Leeks

Section 26 is about *leeks*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 27: Peas

Section 27 is about *peas*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 28: Chard

Section 28 is about *chard*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about chard.

## Section 29: Onions

Section 29 is about *onions*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 30: Squash

Section 30 is about *squash*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

1. Sow
2. Thin the squash
3. Harvest

## Section 31: Radish

Section 31 is about *radish*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 32: Beans

Section 32 is about *beans*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about beans.

## Section 33: Kale

Section 33 is about *kale*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 34: Leeks

Section 34 is about *leeks*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 35: Peas

Section 35 is about *peas*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

1. Sow
2. Thin the peas
3. Harvest

## Section 36: Chard

Section 36 is about *chard*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about chard.

## Section 37: Onions

Section 37 is about *onions*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 38: Squash

Section 38 is about *squash*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 39: Radish

Section 39 is about *radish*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

## Section 40: Beans

Section 40 is about *beans*, with **bold notes**, `code`
and a [link back to the top](#touch-gestures-286), in a paragraph that
wraps across a couple of lines in an ordinary window.

> A quote about beans.

1. Sow
2. Thin the beans
3. Harvest

## The end

The last paragraph of the page.
