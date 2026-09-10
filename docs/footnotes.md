# Footnotes

Tinta supports named and numbered Markdown footnotes:

```markdown
A reference[^note], and the same reference again[^note].

[^note]: A note with **formatting**, links and math.

    A second paragraph, indented by four spaces.
```

Notes are numbered by their first reference and collected after the document.
Click a superscript number to visit its note. Small text links return to the
reference; repeated references each have their own numbered return link.
Definitions may contain paragraphs, lists, tables and fenced code. Two-space
continuations are also accepted. Unresolved and escaped references, inline code,
and fenced examples remain literal. The first duplicate definition wins.

HTML exports retain navigation and accessible note roles. DOCX exports use real
Word footnotes; repeated occurrences use NOTEREF cross-reference fields. Native
printing lists notes after the document. Nested footnotes and Obsidian's inline
`^[note]` extension are not supported in this version.

Try [the mixed sample](../tests/fixtures/footnotes.md).
