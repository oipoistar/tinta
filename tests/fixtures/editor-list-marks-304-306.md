# List marks from the rail and from a paste (#304, #305, #306)

The edit rail's **block buttons** mark *every* line a selection takes in, and
a fourth one, under the bullets, numbers them. Lines pasted on a list item
become its items. Live does the same ([Live's notes](../../docs/live-editor.md)).

## Lines to turn into a list (#306)

Select the three lines below and press the rail's bullet button: each one
gets a bullet. Pressing it again takes them all off. Each press is one
Ctrl+Z.

Milk
Eggs
Bread

## Lines to number (#304)

The numbered list button numbers them 1, 2, 3, across a selection or across
several carets (Ctrl+Alt+Down), and pressing it again leaves plain text.

Wake up
Make coffee
Read the news

## Lists to paste into (#305)

Copy the three plain lines above. Put the caret at the end of a list's last
item, press Enter and paste: each line becomes an item. The numbers count
on, a task gets an empty box, and in the quote each line is quoted.

- Apples
- Pears

1. First
2. Second

- [ ] Write the notes
- [x] Send them

> A quoted line

## Around them

| Kind | Mark |
| --- | --- |
| Bullet | `- ` |
| Number | `1. ` |
| Task | `- [ ] ` |
| Quote | `> ` |

```text
Code keeps its lines as they are:
- not a list here
```

- Nested lists keep their levels
  - like this one
  - and this one
- back out

Ctrl+Shift+V pastes the text as it is, without marks.
