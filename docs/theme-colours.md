# Theme colours

## Headings and highlights

Custom themes can override individual headings and highlighted text:

```ini
heading=334455
h1=AA3344
h2=7755AA
h3=228844
h4=3366AA
h5=775500
h6=008877
highlightbackground=99DDCC
highlighttext=102030
```

Each missing `h1`-`h6` key inherits `heading`. Highlight colours are optional:
without them, Tinta keeps its light/dark translucent yellow background and the
inherited text colour. Explicit colours use opaque RRGGBB hex. Nested links and
inline code retain their own text styles.

In Settings > Appearance > Edit, scroll the colour controls to **Heading colours**
and expand the six-level palette. **Highlight** changes the marked background;
**Marked text** changes the foreground. Clear an optional hex field to restore
**Auto**. The preview shows H1 and a marked-text sample. Saving a theme preserves
both explicit values and inheritance.

Native viewing and HTML/DOCX exports honour these overrides. Word uses arbitrary
colour shading for custom highlights. Native printing retains Tinta's established
light print palette. Existing themes look the same unless overrides are added.

Try `tests/fixtures/theme-colours.md` with `tests/fixtures/theme-colours.ini`.

## Bold, italic, selection and rules

A theme can also colour emphasised text, the selection and horizontal rules:

```ini
bold=AA2200
italic=007755
bolditalic=7722AA
selection=CC0000
hr=00AA44
```

- `bold` colours **bold** text and `italic` *italic* text, in every block:
  headings, lists, quotes, callouts, tables and footnotes. Text both bold and
  italic takes `bolditalic`, or else `bold`, or else `italic`. Links keep the
  link colour and inline code its own; marked words take `highlighttext` when
  it is set.
- `selection` is the selection's hue. Each view keeps its own translucency, so
  the text stays readable under it: the reading view and Source tint it at
  35%, Live at 28%.
- `hr` colours horizontal rules, including the one above the footnotes. Without
  it they keep `blockquoteborder` on screen.

Each key is optional and takes opaque RRGGBB hex; a missing or invalid one
keeps today's look. In Live the line being typed reads its Markdown one span at
a time, so `***words***` show as bold until the caret moves away. Source
colours bold and italic words the same way.

The reading view, Live, Source and the HTML and DOCX exports honour these keys;
in HTML, `selection` becomes the page's `::selection`. Themes saved from the
editor keep them, though the editor has no controls for them yet; edit them in
`themes.ini`. Printing keeps its light palette.

Try `tests/fixtures/theme-emphasis-310.md` with
`tests/fixtures/theme-emphasis-310.ini`.
