# Find in any case

Find matches letters without regard to case in every alphabet that has two
cases, not only in A to Z. Press **Ctrl+F**, type a word from the table in
small letters, and its capitalised spellings are marked as well: the bar
counts what the table says. Typed in capitals, a word finds the same
places. It works alike in the reading view, in the editor (**Ctrl+E**,
then Ctrl+F), in the Contents filter (Tab, then type) and in Sheets.

| Word | Language | Matches |
|------|----------|--------:|
| `über` | German | 7 |
| `čaj` | Croatian | 6 |
| `école` | French | 6 |
| `ελλάδα` | Greek | 4 |
| `привет` | Russian | 6 |
| `tinta` | English | 3 |

## Über uns

Die Suche behandelt **ÜBER** und *über* gleich, überall im Text und in
jeder Überschrift.

- Ärger und ärger sind dasselbe Wort
- ÖL, Öl und öl ebenso
- [x] Straße bleibt Straße: das ß hat hier keine große Form

## Čaj i kava

> Jedan upit nalazi ČAJ, čaj i čajnik. Isto vrijedi za Šuma, Žaba i Đak
> s njihovim malim slovima.

1. šuma
2. žaba
3. đak

## Ελλάδα

Η Ελλάδα γράφεται και ΕΛΛΆΔΑ. A capital written without its accent, as
signs often do, is another spelling and is not matched: accents are kept.

## Привет

Привет, мир! ПРИВЕТ и приветствие находятся одним запросом.

| Слово  | Word   |
|--------|--------|
| Москва | Moscow |
| МОСКВА | MOSCOW |

## École

L'École est ouverte. Les écoles et l'ÉCOLE se trouvent avec le même mot,
as [Tinta](https://example.com/notes) and TINTA do.

```text
Code keeps its case and is searched alike: ÜBER, Čaj, Привет, École.
```

## Other files

The results panel (**Ctrl+Shift+F**) looks through the folder's other
notes the same way. A note is linked by its name in either case, so
[[šuma]] opens `Šuma.txt`, and in the Live editor `[[` with a small
letter after it offers the notes whose names begin with its capital.

## Folded away

Fold this section with the arrow beside its heading, then search for the
animal named here, typed in small letters: the section opens to show the
ŽIRAFA, which is written nowhere else in this file.
