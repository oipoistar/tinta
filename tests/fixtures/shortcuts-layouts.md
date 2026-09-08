# Shortcut regression: $E = mc^2$

Open a **copy** of this document to check issue [#195](https://github.com/oipoistar/tinta/issues/195).
Select the Windows shortcut profile in Settings. Start in reading mode.

| Key | Expected behavior | Math stays intact |
| --- | --- | --- |
| F1 | Help opens; F1 closes it | $x^2 + y^2$ |
| P | Help stays closed | $\frac{1}{2}$ |
| E | Editor opens with no extra character | $\sqrt{a+b}$ |

## Reading and editing

1. Press F1 twice. Check that the table, heading and equations still render.
2. Press P. It must not open Help.
3. Press E, then immediately type a letter. Only the typed letter should be inserted.
4. Undo, then open and close Help with F1 in the editor. The buffer stays clean.
5. Enter `e p : ?` and non-Latin text in the editor and search field. These remain text.
6. With a non-Latin layout active, press the key that Windows reports as E. Edit opens.
7. Repeat with pinned Contents, then unpin Contents and verify typing filters headings.

> Text samples: **English**, *Hrvatski*, русский: у, Ελληνικά: ε.
> These characters are document content; shortcut matching must not consume normal typing.

$$
\begin{bmatrix}a & b \\ c & d\end{bmatrix}
\begin{pmatrix}x \\ y\end{pmatrix}
= \begin{pmatrix}ax+by \\ cx+dy\end{pmatrix}
$$

## Vim and custom bindings

- In the Vim profile, `:` enters Edit, `?` toggles Help and `/` opens Search.
- The first character typed after `/` must remain in the query.
- Raw `:` and `?` also work in the Windows profile; full-width `：` and `？` remain supported.
- In a portable custom profile, bind Edit to F2 and Help to F3, then to G and H.
- Check the same commands in the top-left menu and the document right-click menu.

```latex
% Literal code must stay literal after opening and closing overlays.
\frac{1}{2} + \sqrt{x} % E, F1, :, ?
```

## Final heading

The [existing mixed-content control](markdown-regression-control.md), lists, emphasis,
links, quote and fenced code should remain unchanged when switching modes.
