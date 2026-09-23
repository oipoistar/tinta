# Find, Replace and table focus

Search words: alpha replaced alpha. Unicode: 中文 café 😀 and é.

| Name | Value |
| --- | --- |
| alpha | beta |
| gamma | delta |

## Surrounding content

> A quote with **bold**, `inline code` and $a+b=c$.

- First list item
- Second list item

```cpp
int alpha = 123;
```

## Manual checks for issue 223

1. Open Ctrl+H. Only the empty Find field should have a blinking caret.
2. Enter abc, press Left, type X. Expect abXc. Repeat in Replace.
3. Check Home/End, Delete/Backspace, Shift selection, Ctrl+A/C/X/V,
   clicking inside a word, dragging a selection, and a long search string.
4. Try Chinese text, emoji and combining accents. Cursor navigation and
   deletion should not split a surrogate pair or combining cluster.
5. Edit alpha in the rendered table, Tab to beta, click the source heading
   and type X. The edit should commit and X should enter the clicked source.
6. Open Ctrl+H directly from a cell. Typing belongs to Find; Tab switches
   to Replace. Click source with search visible to resume source editing.
7. Check Shift+Tab, Enter and Escape in table cells, plus save and undo
   after returning to the source. Escape in search closes it.
8. Repeat with Ctrl+F in reading mode, preview hidden, wrapped source,
   a narrow window, and Paper/Midnight themes. All surrounding content
   should continue to render and export normally.
