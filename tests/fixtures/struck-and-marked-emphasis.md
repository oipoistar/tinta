# Struck ~~**bold**~~ and marked ==*italic*== words

Strikethrough and highlight wrap bold, italic, links and code, as on GitHub.
Above the last section every `~~` and `==` is drawn as a line through its
words or a marker behind them; the last section keeps each one as typed.

## Around emphasis

~~*Struck_italic*~~ and ==*Marked_italic*==; ~~**Struck_bold**~~ text after it,
==**Marked_bold**== text after it, and ~~***Struck_both***~~ beside
==***Marked_both***==. Underscores work too: ~~_Struck_under_~~ and
==__Marked_under__==, and so does a word run on: ~~**Struck_tight**~~text.

Nested the other way round, as before: **~~Bold_struck~~**, *==Italic_marked==*,
***~~Both_struck~~*** and **==Bold_marked==**.

Several spans in one: ~~a **Struck_mixed** and an *italic* word~~,
==a **Marked_mixed** word, x^2^ and H~2~O==, ~~**bold ==Marked_in_struck== inside**~~,
and all of them: ~~==**Struck_marked_bold**==~~ and ==~~*Marked_struck_italic*~~==.

## Around links, code and math

~~[Struck_link](#around-emphasis)~~, ==[Marked_link](#around-emphasis)==,
~~**[Struck_bold_link](#around-emphasis)**~~, ~~`struck_code()`~~,
==`marked_code()`== and ~~**Struck_math** $x^2$ result~~.

### A heading with ~~**Struck_heading**~~ and ==*Marked_heading*==

| Change | Before | After |
| --- | --- | --- |
| Renamed | ~~**Struck_cell**~~ | ==**Marked_cell**== |
| Flag | ~~`--quiet`~~ | ==`--silent`== |
| Page | ~~[Struck_cell_link](#around-emphasis)~~ | ==*[Marked_cell_link](#around-emphasis)*== |

> A quote with ~~**Struck_quote**~~, ==*Marked_quote*==, `code` and $a+b=c$.
>
> > [!NOTE]
> > A callout with ==**Marked_callout**== and ~~*Struck_callout*~~.

- A bullet with ~~**Struck_list**~~ and ==*Marked_list*==
  - A nested bullet with ==~~**Marked_struck_nested**~~==
- [ ] A task with ~~*Struck_task*~~
- [x] A done task with ==**Marked_task**==
- A longer item that wraps in a narrow window: ~~the **Struck_old** plan with *three* steps and [a link](#around-emphasis)~~ while ==the **Marked_new** plan keeps its *marker* behind every wrapped line== to the end.

1. A numbered item with ~~**Struck_numbered**~~
2. A numbered item with ==*Marked_numbered*== and a ~~Struck_claim[^note]~~

## Stays as typed

- In code: `~~**Code_struck**~~` and `==*Code_marked*==`
- With a blank inside: ~~ **Spaced_struck** ~~ and == *Spaced_marked* ==
- Comparisons: `a` == `b` and `c` == `d`, `x` === `y` and `z` === `w`,
  **Compared_left** == **Compared_right** and **Compared_up** == **Compared_down**
- With no closer: ~~**Open_struck** and ==*Open_marked*
- Across a bold edge: ~~a **Crossing_struck~~ c** and **d ==Crossing_marked** f==
- Escaped: \~\~**Escaped_struck**\~\~ and \=\=*Escaped_marked*\=\=

```markdown
~~**Fence_struck**~~ and ==*Fence_marked*== stay literal in a fence.
```

Final_paragraph with **Bold_last**, *Italic_last* and ~~Struck_last~~ text.

[^note]: A footnote with ~~**Struck_footnote**~~ and ==*Marked_footnote*==.
