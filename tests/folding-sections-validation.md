# Folding sections (#316)

Validated on Windows with the Release build on 2026-11-05.

## What was asked

- Folding and unfolding the content of H1, H2 and lower sections, in the
  reading view at least, and in the editor if possible.
- A setting to turn folding on or off.
- A setting to open every document folded, so that only the top headings
  show until a section is opened.

## What was built

**In the reading view**

- **The arrow.** A heading of the page's top level gets an arrow in the
  margin left of its first line, when it has something under it.
  - It shows while the pointer is on the heading's line.
  - It stays beside a folded heading.
  - A click folds the section, and the next click opens it.
- **What a fold hides.** A section is the blocks after its heading, up to
  the next heading of its level or above. The footnotes are never hidden.
- **Fold all.** Right-clicking the page offers *Fold all sections*. While
  any section is folded, the item reads *Unfold all sections*.
- **Hidden headings still count**, at the folded heading's place, so slugs
  stay as they are open: the second `## Tables` is still `tables-1`.
  - Contents lists every heading. A heading in a folded section is
    fainter, and the current-heading highlight skips it.
  - A click in Contents, or Enter, opens the folds around the heading and
    goes to it.
  - A link to a hidden heading (`#deep-inside`) opens them too.
  - An `<a id>` target or a footnote's way back, inside a folded section,
    leads to the folded heading.
- **Search opens what it finds.** A query found only in folded sections
  opens the sections around each hit, nested ones too, and then shows the
  matches. A query found nowhere opens nothing.
- **What never folds:** the editor and its preview, print, and PDF.

**Settings**

- Settings > Appearance > **Fold sections**: Off, On or Folded.
  - Off: no arrows.
  - On, the default: arrows, and documents open as they are.
  - Folded: documents open with every section folded, at their top. Choosing
    it folds the page being read too.
- `settings.ini` keeps `foldSections` and `foldOnOpen`.

**Across documents**

- A document keeps its folds for the session: when you leave it for another
  tab or file and come back, and when it reloads after a change on disk.

## Change

- **`folding.cpp`** (new) holds the fold state, the arrows' hit tests and
  drawing, the session memory per document, and the openers for Contents,
  links and search.
- **`render.cpp`:**
  - layout skips a folded section's top-level blocks, but counts their
    headings, anchors and footnote references at the folded heading's place
    (`registerHidden`);
  - a heading's slug comes from one function, used whether it is laid out
    or hidden (`registerHeading`);
  - each foldable heading records its arrow (`addFoldArrow`).
- **Contents, the search panel and links:**
  - the contents panel's current-heading highlight and the search panel's
    heading lookup skip hidden headings;
  - `documentTargetY` and the contents panel open the folds around a hidden
    heading;
  - `performSearch` opens the sections holding the query.
- **Settings, menu and strings:**
  - the settings row has three segments, each as wide as its word;
  - the Appearance panel is 68 px taller;
  - the context menu has one more item;
  - seven strings are added in all eight shipped languages.
- `README.md` describes the feature.

## Fixture

`fixtures/folding-sections-316.md` holds:

- front matter;
- two top-level headings with second- and third-level sections;
- a list with emphasis, code, a link and tasks;
- a table with styled and wrapping cells;
- a word found only in one section (`zanzibar`);
- an `<a id>` target, a quote and a code block in a nested section;
- an empty section;
- a second `## Tables`;
- a rule and two footnotes.

## Automated checks

**`folding_sections`** has 156 checks, run in Paper and Midnight at 1050 and
650 px.

- **Open page:**
  - eight headings, and seven arrows: the empty section has none;
  - each arrow sits in the margin beside its heading's first line, and is
    hit there and along the line.
- **One section folded with its arrow:**
  - its table, word, nested heading, quote and code are gone, and the
    sections after it stay;
  - slugs are unchanged;
  - the nested heading counts at the folded heading's place, and has no
    arrow;
  - the page is shorter;
  - the anchor in it leads to the folded heading;
  - selection holds (#318's checks on the folded page).
- **Following a link** to the nested heading opens the section and lands on
  the heading.
- **Fold all:**
  - only the two top-level headings show, and all eight still count;
  - the footnotes stay, and slugs are unchanged;
  - a footnote's way back leads to the folded heading;
  - selection holds.
- **Contents** reaches a heading two folds deep and opens only those two
  folds.
- **Search:** with everything folded, `zanzibar` opens only its two
  sections, and its match is placed. A query found nowhere opens nothing.
- **Nothing folds** in the editor, in print, or with the setting off. Fold
  all is not offered in the editor.
- **Across documents:** a document keeps its folds across leaving it for
  another and coming back. With Folded set, a new document opens folded,
  with its reading position dropped.
- **`settings.ini`** keeps both choices; the defaults are On, and unfolded.

**`i18n`** and **`russian_interface`** pass with the new keys in every
language. **`context_menu`** passes with the new item.

## In the built app

The build ran as isolated portable copies, driven with posted messages and
captured with PrintWindow (`out/fold-316/`).

**Midnight, English, 1000 x 820:**

- No arrows until a heading is hovered. Hovering *Lists and emphasis* shows
  a down arrow.
- A click folds the list section, and a right arrow marks it.
- The right-click menu shows *Unfold all sections* between Search and Table
  of Contents. Unfold all, then *Fold all sections*: only the front matter,
  the two top-level headings with their arrows, and the footnotes are left.
- Contents lists all eight headings, the six folded ones fainter. A click on
  *Deep inside* opens *Folding sections* and *Tables* and goes there; the
  other sections stay folded.
- Settings > Appearance shows *Fold sections* with Off | On | Folded, and
  the panel holds every row.

**Paper, German, 650 x 820:**

- Hover, click and fold work the same.
- The empty section has no arrow.
- *Alle Abschnitte ausklappen* fits the menu.
- The settings row first squeezed its label to "A." with three
  equal-width segments. Each segment now takes its own word's width, and
  the row reads *Abschnitte einklap...* like its neighbours at this width.
- Choosing *Eingeklappt* folds the page behind the panel at once.

## Size

`tinta.exe` grows by 14,848 bytes (3,985,920 to 4,000,768).

## Limitations

- **The editor does not fold.** Source and Live have no arrows, and their
  preview shows every section.
- **Copy takes what is shown.** Select all and copy leave out a folded
  section's text.
- **Some targets don't open their fold.** An `<a id>` target or a
  footnote's way back inside a folded section leads to the folded heading,
  without opening it.
- **Folds last for the session.** They are not kept after Tinta closes. A
  reading position saved while sections were folded is restored on the
  next launch to a page that opens unfolded (with Folded set, at the top).
- **No keyboard shortcut.** Folding is by the arrows, the context menu and
  the setting.
- **Only top-level headings fold.** A heading inside a list or a quote
  stays as it is.
