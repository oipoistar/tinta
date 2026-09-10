# Heading and highlight colours

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
