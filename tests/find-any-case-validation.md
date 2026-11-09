# Finding text in any case

Validated on Windows with the Release build on 2026-11-10.

## What was found

Found in a review of the code; nobody reported it. Wherever Tinta matches
text without regard to case, only A to Z counted. A search for `über`
missed `Über` and `ÜBER`, `čaj` missed `Čaj`, `привет` missed `Привет`,
and so on for Greek, French and every other alphabet with two cases. It
affected:

- Find in the reading view and in Source, and Replace with it;
- the Contents filter;
- the other files of the results panel (Ctrl+Shift+F);
- `[[wiki links]]`, which find their file in any case;
- Live's `/` menu, in a language whose block names begin with such a
  letter (German's Überschrift), and the notes it offers after `[[`;
- on a sheet: Find, the cards and notes offered after `[[`, the arrow from
  a card to the note it names, and Open note.

## Cause

Each of them lowered its text with the C runtime's `towlower`, or compared
with `_wcsicmp`. Tinta sets no locale, and in the C locale both fold A to Z
only.

## Change

- `caseFoldTable()` (`utils.cpp`) gives the small letter of each UTF-16
  unit. It is filled on first use by `LCMapStringEx` with the invariant
  locale, so the answer does not depend on the language of the machine;
  should the call fail, A to Z still fold. `toLower()` reads the table.
- A unit in gives a unit out, so a match's place in the folded text is its
  place in the document.
- Everything above goes through it. The reading view's Find, the Contents
  filter, the note names and a sheet's Find already called `toLower()`.
  `scanText` (`search.cpp`), `performEditorSearch` (`editor.cpp`),
  `wikiLinkPath` (`input.cpp`), `lowered` (`live_text.cpp`), `refreshCard`
  and `ghostLinks` (`sheet_doc.cpp`) and `followCardLink`
  (`sheet_input.cpp`) now do.
- Source's Find folds the document as it reads it. It used to make a
  lowered copy of the whole document on every keystroke.

## Fixture

`fixtures/find-any-case/` is a folder, since three of the places look
beside the document:

- `find-any-case.md`: a table of six words (German, Croatian, French,
  Greek, Russian, English) with the number of matches each has on the
  page; a section for each, with the word small, capitalised and in
  capitals in a heading, a list, a task item, a quote over two lines, a
  code block, a table, bold, italic and a link; `[[šuma]]`; and a last
  section holding a word written nowhere else, for a search to unfold.
- `Über Tee.md` and `Grüße.md`, the other files the results panel finds
  `über` in, and the notes offered after `[[ü`.
- `Šuma.txt`, the file `[[šuma]]` opens.
- `Teeplan.smd`, a sheet: a group, a card of `Über Tee.md`, and two text
  cards, one of which names `[[über tee]]`.

## Automated checks

**`find_any_case`** (`case_fold_tests.cpp`, in `input_tests`) works on a
copy of the fixture folder, as a portable copy with its own settings, and
types every query through `keystroke.h`:

- the table: A to Z; fifteen capitals and their small letters, accented
  Latin, Greek and Cyrillic; letters with no other case and the halves of
  a surrogate pair, which stay; and a mixed string, unit for unit;
- the reading view, in Paper and Midnight at 1050 and 650 px: each of the
  six words, small and in capitals, finds the number of places the table
  says, every one of them the word in some case and marked on the page;
- a word in a folded section is found in capitals, and its section opens;
- the Contents filter leaves one heading for a word typed in the other
  case, in four alphabets, and Enter goes to it;
- the results panel lists `Über Tee.md` with 3 matches and `Grüße.md`
  with 1 for `über`;
- `[[šuma]]` opens `Šuma.txt`;
- Source finds the same numbers as the reading view, and Replace all
  changes `ÖL`, `Öl` and `öl` alike;
- Live, in German: `/üb` offers Überschrift 1 first and Enter writes the
  heading; `[[ü` offers Über Tee, then Grüße, and Enter writes the link;
- the sheet: Find for `über` stops at the card, both text cards and the
  group; the arrow runs from the card naming `[[über tee]]` to the card of
  `Über Tee.md`; `[[ü` offers the card and the note; Open note, from the
  keyboard's menu, goes to the card.

It saves a frame of each place under `build/case-fold-test-Release/frames/`.
Looked at: the reading view in Paper at 1050 px and in Midnight at 650 px,
the folded section, the Contents filter, the results panel, Source, both of
Live's lists, and the sheet's Find, names and menu.

With the table folding A to Z only, as `towlower` did, it fails 100 checks:
16 on the table, 40 in the reading view, 2 for the folded section, 12 for
the Contents filter, 5 in the results panel, 2 for the wiki link, 13 in
Source, 5 in Live and 5 on the sheet.

The full suite, without the three tests that use the clipboard
(`notification_setting`, `copy_file_path`, `code_block_layout`), passes:
60 of 60.

## In the built app

An isolated portable copy with a copy of the fixture folder
(`out/audit/fold/app/`, ignored), in Midnight at 1008 x 760, driven with
posted messages and copied from the screen:

- Ctrl+F and `ÜBER`: "1 of 7", with `über`, `Über`, `ÜBER` and the start
  of `überall` and `Überschrift` marked.
- Ctrl+Shift+F: the page's seven, then `Grüße.md` with 1 (`ÜBERSEE`) and
  `Über Tee.md` with 3.
- Tab and `ПРИВЕТ`: Contents shows the heading Привет alone.
- E, Ctrl+F and `ΕΛΛΆΔΑ`: "1 of 4", `ελλάδα` marked in the table's source.

## Speed and size

Lowering 2,000,000 letters took 11.6 to 12.3 ms with `towlower` and takes
2.2 ms through the table. The table is built once, in 0.2 ms, and holds
128 KB from then on; none of it is in the executable, which grows by
1,024 bytes (4,004,864 to 4,005,888).

## Limitations

- A letter folds to one letter. Accents are not folded: `ΕΛΛΑΔΑ`, written
  without its accent as Greek capitals often are, does not find `ελλάδα`.
  The final `ς` is not `σ`, `ß` is not `SS`, and the Turkish `İ` and `ı`
  match only themselves.
- Letters outside the Basic Multilingual Plane, such as Deseret's, are two
  units each and do not fold.
- Heading anchors and comparisons of whole file paths are not part of this
  change.
