# Frontmatter properties

Settings → Frontmatter controls the property strip above a Markdown document.
An ordered table lists keys Tinta has encountered. Drag a row's handle to change
its order, select its left/right group, and choose whether to show its label.
The preview uses the current document, or sample properties when none exist.
At narrow widths, the right group stacks below the left group.

- Text: plain or large text.
- Dates: calendar date, relative time, or the original value.
- Lists: chips, #hashtags, comma-separated values, or a count. A limit of zero
  shows all items. Otherwise remaining values appear behind a `+N` chip; hover
  over it in the document to read them.
- Booleans: their plain value. Nested objects are identified as structured
  values rather than flattened into misleading scalar fields.

Newly encountered keys follow the **Any new key** rule. Rows already listed keep
their individual choices. Settings are global and saved in `settings.ini`; the
field names are encoded in the ordered `frontmatterRule` entries so arbitrary
keys cannot break the INI syntax. The defaults show title and tags; date rows
start unchecked.

## Dates and saving

**Showing `created` or `updated` also enables its save behavior.** The master
Hidden switch disables the strip and automatic date maintenance together.

- `updated` refreshes on every explicit editor save, including a clean buffer
  and editor Save As.
- `created` keeps its existing value. If absent, empty or null, it is initialized
  using the UTC time when Tinta first encountered this document's frontmatter
  during the current session. That time is recorded in memory until a successful
  save. It is not inferred from the file's creation time.
- Missing enabled fields are inserted into an existing frontmatter block.
  Documents without frontmatter are left alone.
- Opening, reading, changing display settings, exporting and crash-recovery
  autosaves do not modify these dates. Viewer Save As copies the file unchanged.
- A successful save updates both the file and editor. The automatic metadata
  change is one Undo/Redo action. A failed write leaves the buffer and its undo
  history unchanged.

Dates are written as quoted UTC RFC 3339 values, such as
`"2026-09-10T11:02:03Z"`. Surrounding comments, other fields, the UTF-8 BOM and the
file's line-ending style are retained. This is a targeted edit, not a YAML
reserialization. Duplicate keys, unclosed frontmatter, YAML merges and ambiguous
multiline values are not automatically rewritten. Existing non-scalar date
values are also left alone. Common top-level scalars and inline/block lists are
supported for display; this is not a complete YAML processor.

The strip is part of native viewing and printing. HTML and DOCX keep the previous
behavior of exporting the document body without its frontmatter strip. Markdown
headings remain independent of a large title property.

## Samples

- [Display controls and mixed Markdown](../tests/fixtures/frontmatter-settings.md)
- [Missing dates on first save](../tests/fixtures/frontmatter-first-save.md)
- [A document without frontmatter](../tests/fixtures/frontmatter-no-block.md)
