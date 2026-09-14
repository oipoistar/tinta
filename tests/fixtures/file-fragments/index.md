# Linked documents

[Open the destination heading](<target notes.md#destination-heading>)

[Chinese heading](target%20notes.md#%E4%B8%AD%E6%96%87%E6%A0%87%E9%A2%98) · [Duplicate heading](target%20notes.md#repeated-1)

[Filename with a hash](hash%23notes.md#destination-heading) · [Literal percent](literal%2523.md#destination-heading)

[Top of destination](target%20notes.md#) · [Missing heading](target%20notes.md#not-a-heading)

[Missing file](not-created.md#destination-heading) · [Same document](#destination-heading)

## Destination heading

This heading deliberately has the same ID as the destination, at a different position.

## Surrounding content

| Item | Value |
| --- | --- |
| **Table stays intact** | $a^2+b^2=c^2$ |
| Link inside a table | [Open target](target%20notes.md#destination-heading) |

> A quote with *emphasis* and `inline code`.

- List item with **bold text**.
- Another item with $E=mc^2$.

```cpp
// Links inside code must remain literal: target notes.md#destination-heading
int main() { return 0; }
```

[Web link remains external](https://example.com/file.md#heading)
