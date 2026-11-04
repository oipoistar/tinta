# CSS themes, phase 1

Validated on Windows with the Release build on 2026-11-03. The design is in
`docs/specs/theming.md`; the user's page is `docs/css-themes.md`.

## What was asked

Theming beyond `themes.ini`'s flat keys: people ask for CSS, and a web
engine is out of the question, so a profile of CSS that Tinta draws
natively.

## The change

- **The engine** (`css.h`, `src/css*.cpp`): CSS Syntax 3 tokens and error
  recovery; selectors (type, class, id, attributes, combinators, `:is()`,
  `:where()`, `:not()`, the structural pseudo-classes, `:checked`,
  `:lang()`, `::before`, `::after`, `::marker`, `::selection`) with CSS's
  specificity; the cascade by origin, importance and order; inheritance;
  custom properties and `var()`; `calc()`, `min()`, `max()`, `clamp()`;
  every colour syntax including `oklch()`, `color-mix()` and
  `light-dark()`; `@import`, `@media`, `@font-face`, `@supports`;
  diagnostics with file, line and column, and a suggestion for a
  misspelt property. Styles are computed once per element context, and
  contexts are shared by elements no selector tells apart.
- **Themes** (`themes_css.cpp`): every `.css` file in `<config>\themes\`
  is a theme, after the `themes.ini` ones, named by its file. A sheet is
  read into the theme Tinta draws with: the page, headings (colour, face,
  size, weight, slant), links, inline code and code blocks, the code
  tokens, quotes, marks, bold, italic, the selection, rules, the accent,
  the body's size and line height. It starts from Paper, or Midnight when
  dark. `color-scheme: light dark` makes a theme follow Windows, read once
  for each side. `@import "Paper"` (or a `themes.ini` theme's name) starts
  from that theme: built-in and `themes.ini` themes are turned into CSS by
  one mapping, `themeToCss`.
- **Following the file**: the 500 ms timer that follows documents reads
  the active sheet again when it, or a file it imports, changes, and
  applies it; replaced themes stay in memory so no copy of one dangles.
- **Problems**: when the theme or its sheet changes, a notice gives the
  count and the first problem, with the file and line; the tray lists all.
- **Settings > Appearance > Edit** on a CSS theme opens its sheet in Tinta's
  editor; Tinta reads `.css` files as highlighted text.
- **HTML export** carries the sheet, imports inlined, after its own styles.
- **Type sizes** for every theme: body, code and heading sizes, heading
  weights and slants and the body's line height reach the text formats,
  the reading view and Live (`96506d2`).

## Fixtures

`fixtures/theme-css/`: `paper-light.css`, `night-dark.css`,
`ink-light-dark.css` (with `ink-base.css` and `ink-print.css` it imports),
`mistakes.css`, and `mixed.md`, a page with every element.

## Automated checks

- `css_engine` (css_tests, 369 checks): tokenizer and parser edge cases,
  error recovery, every selector and its specificity, the cascade and
  `!important`, inheritance, `var()` fallbacks and cycles, `calc()`, every
  colour function against reference values, `light-dark()` and `@media`,
  `@import` with a cycle, `@font-face`, `@supports`, shorthands,
  `serialize`, diagnostics and their lines, interning (a 2,000-row table
  makes two row contexts), and timing.
- `inline_code_theme` (theme_tests, `tests/theme_css_tests.cpp`): all ten
  built-in themes come back from their CSS with the same colours, faces and
  sizes; a sheet's sizes, faces, colours, line height, quote bar, mark,
  selection and rule; light-dark() read for each side; `@import
  "Midnight"`; `display` and a misspelt `colour` reported on their line
  while the rest applies; a sheet in `themes\` listed by name, applied
  (body colour and text format size), drawn (h1's colour in layout) and
  exported to HTML; an unchanged sheet left alone, a saved one read again
  and applied, its problem naming the file and line.

## In the built app

The theme test draws `fixtures/theme-css/mixed.md` with the real layout
and saves the frames beside itself (`build/theme-test-Release/css-*.png`),
which were inspected:

- `paper-light.css`: a light page, its own heading rules and code boxes.
- `night-dark.css`: a dark page with gold headings, the tag chips and
  links in its colours.
- `ink-light-dark.css` with Windows light: cream paper, the body in
  Georgia at 17 px (its first two faces are not installed), headings in
  Segoe UI (`system-ui`), inline code without a box in orange, as the sheet
  says.
- The same sheet with Windows dark: the dark side of each `light-dark()`,
  and a dark mark under light text.

A real window could not be captured in this remote session (PrintWindow
and the screen copy return no Direct2D content); the frames come from the
same drawing code.

## Size

The CSS engine and the theme compiler are about 190 KB of `tinta.exe`'s
growth to 4,708,352 bytes (measured from the linker map with Scholar's;
see `tests/scholar-validation.md`), with their share of `std::`
templates. That is over the 120 KB the spec set for phase 1; trimming it
means dropping parts of CSS the later phases need.

## Limitations

- Only what the theme can say today is drawn: colours, faces, type sizes
  and the line height, per element kind. A rule that depends on where an
  element stands (`blockquote strong`) and spacing, borders, letter
  spacing and list markers are read and kept for the export, and drawn
  from phase 2 on.
- `@font-face` fonts are not loaded yet (phase 2); the theme uses the
  next face in its list that is installed.
- Every `.css` file in `themes\` is listed, including sheets meant only to
  be imported; keep those in a subfolder.

# Phase 3: the window and diagrams

Validated on Windows with the Release build on 2026-11-04.

## What was asked

Two of phase 3's variable families: `--ui-*` for a window that differs
from the page (a dark frame around a light page, say), and `--mermaid-*`
so a theme can colour Mermaid diagrams with Mermaid's own variable names.
A theme without them, every built-in and `themes.ini` theme among them,
must draw exactly as before.

## The change

- **`--ui-background`, `--ui-text`, `--ui-accent`, `--ui-panel`,
  `--ui-border`** are read from the page (`:root`, or `body`) into optional
  `D2DTheme` fields, light and dark. The chrome draws through
  `themeUiText`, `themeUiAccent`, `themeUiHeading`, `themeUiPanel` and
  `themeUiBorder` (`app.h`, `themes.cpp`), which return today's derivation
  when a field is unset. `--ui-background` is the tab strip, the edit rail, the Windows
  caption and its dark-mode flag, and the print preview's backdrop;
  `--ui-panel` the Contents and browser panels, the search results, the
  find bar and go-to-line prompt, every menu (context, tab, editor, Live's),
  the tab switcher and hover card, notices and prompts, the settings (its
  cards lift from it), help, the theme chooser, the shortcut and theme
  editors, annotation popups, the link peek and the start page's buttons and
  cards. Dark or light tuned details (warning hues, shadows, wells) follow
  the panel's own lightness.
- **`--mermaid-*`**: Mermaid's theme variables in kebab case map onto the
  parts of a diagram Tinta draws (`DiagramColors`), the narrower variable
  first: `mainBkg`/`primaryColor` the nodes, `nodeBorder`/
  `primaryBorderColor` their outlines, `nodeTextColor`/`primaryTextColor`
  flowchart labels, `textColor`/`primaryTextColor` the rest of the text,
  `lineColor` (and `defaultLinkColor` for flowcharts) edges, messages and
  frames, `secondaryColor` notes, activations and bands,
  `edgeLabelBackground` edge labels, `clusterBkg`/`tertiaryColor`,
  `clusterBorder`/`tertiaryBorderColor` and `titleColor`/
  `tertiaryTextColor` subgraphs, `background`, and `pie1`-`pie12` the series
  palette. They are read from `body > div.diagram`, so a rule on
  `.diagram` reaches them. The page and the HTML and Word exports share the
  role resolver and a new `flowchartColors`; a `style`/`classDef` still
  wins. PlantUML's preamble keeps the page's colours. Tinta reads no
  `%%{init}%%` directive (such lines are comments to it), so there is no
  per-diagram `themeVariables` to win over the sheet.
- **Problems**: a misspelt `--ui-*` or `--mermaid-*` name is a warning
  with the name it is nearest; an unknown `--ui-*` lists the five; a
  Mermaid variable Tinta has no part for is a note; a value that is no
  colour is a warning. Each has its file and line.

## Fixtures

`fixtures/theme-css/chrome.css` ("Harbour": a light page in a dark window,
following Windows, with diagram colours) and `chrome.md`, a mixed page: a
table, a quote, tasks, a flowchart with a subgraph, edge labels and a node
styled in the diagram, a sequence diagram with a note and an activation, a
pie, code and a list.

## Automated checks

`inline_code_theme` (theme_tests) runs `tests/theme_chrome_tests.cpp`:

- chrome.css compiles cleanly; the five `--ui-*` colours and the Mermaid
  parts, light and dark; what it leaves out stays unset; the precedence
  (`mainBkg` over `primaryColor`, `secondaryColor` filling edge labels,
  `tertiaryTextColor` titling subgraphs, `textColor` against
  `primaryTextColor`), a rule on `.diagram`, `--ui-*` on `body`.
- All ten built-ins: `themeToCss` names no new variable and compiles back
  with none set.
- Five problems on their lines, nothing for a `var()` or a sheet's own
  variable, and a `var()` fallback read as a colour.
- `tabStripBackground` and `promptChipSurface` keep their derivations for
  Paper and Midnight and every helper returns the page's colour; with the
  sheet, the strip is `--ui-background`, the side panels `--ui-panel`
  (at their own opacity), `paint::surface` takes all four.
- The page laid out with the sheet: nodes and actors, the styled node's own
  colours, edge labels, every edge and message in `lineColor`, the
  subgraph and its title, node and edge labels, the note, the four pie
  slices; the table beside them in the page's text; PlantUML's role colours
  stay the page's. The HTML export's SVG carries eight of the colours, the
  export's flowchart mirror matches the page, the Word export succeeds.
  Paper's diagrams keep today's colours.

## In the built app

The test draws each chrome surface over the fixture with the real drawing
code and saves the frames (`build/theme-test-Release/chrome-frames/`): the
strip, Contents, the browser, the find bar, the context and tab menus, the
tab switcher, settings, help, notices, the unsaved-changes prompt, the
theme chooser, the edit rail with the editor's menu, the start page, and
the page at its diagrams (replayed through `drawLayoutRange`, shapes and
connectors included).

- **Pixel for pixel**: the same capture was built into the parent commit
  (`886d159`, with `TINTA_CHROME_BASELINE`, which leaves out the new
  checks) and run with `TINTA_CHROME_ALL_THEMES=1`. All 160 frames, 16
  surfaces in each of the ten built-in themes, are identical between the
  two builds.
- **chrome.css, Windows light**: a navy strip with light tab names, the
  active tab in the page's cream with its dark name; Contents, the browser
  and every menu in the navy panel with light text, orange current rows and
  pins, hairlines in `--ui-border`; settings cards lifted from the panel;
  notices and the prompt on the panel with hues tuned for a dark surface;
  the start page's buttons and Learn cards in the panel, its title and
  recent list on the page. Diagrams: pale blue nodes with blue outlines,
  slate edges, cream edge labels, an orange subgraph title, the styled node
  in its own yellow, the note and activation in the secondary peach, the pie
  in pie1-pie4.
- **chrome.css, Windows dark**: the same window darker, the page dark, the
  diagrams on their dark side.

As in phase 1, a real window could not be captured in this session; the
caption Windows draws (DWM) was not seen, only set from the same strip
colour. The Word export's three diagram images were opened and match the
page; the HTML export's are checked by their SVG colours.

The rest of the suite, 65 tests without the three that use the clipboard,
passes. `editor_context_selection`'s case for a clipboard held for a moment
failed once while other builds shared the machine; it passed run alone and
in the next full run.

## Size

`tinta.exe` grew by 20,992 bytes, from 4,738,560 to 4,759,552 (Release,
against the parent commit built the same way). The chrome's colour
helpers are called out of line: inlined at their few hundred call sites
they cost another 11,776.

## Limitations (phase 3)

- A sheet's canvas keeps the page's colours, its toolbars included: their
  icons are the page's ink. Scholar's reference card, the editor's reading
  button and word count, table editing and the code block buttons stand on
  the page and keep its colours too.
- Mermaid's per-diagram variables (`actorBkg`, `noteBkgColor`,
  `git0`...) have no part of their own in Tinta's diagrams; they are
  reported as not drawn. Every chart's series take `pie1`-`pie12`.
- Print and PDF keep Paper's print palette for diagrams, as for the page.


# Phase 2, and the rest of phase 3: the page element by element

Validated on Windows with the Release build on 2026-11-04.

## What was asked

The renderer reading a CSS theme's computed style for each element, not a
theme-wide palette: spacing, borders, radii, letter spacing, alignment,
OpenType features, list markers and the reading width; the theme's own
fonts with `@font-face`; Word and print from the styles; Live reading them
too. Then phase 3: counters and generated content, folder sheets and
`cssclasses`, the inspector, and importing Typora and Obsidian themes.

## The change

- **Per-element styles** (`page_style.h`, `src/page_style.cpp`): layout
  carries a style context down the page, block by block (with their places
  among siblings, so `p + p` and `tr:nth-child(even)` work) and span by
  span. A base sheet holds Tinta's own heading sizes, bold and code faces,
  so a page's font or colour reaches only what a browser would let it
  reach; only what the theme authored is drawn from the sheet
  (`ComputedStyle::authored`). Margins collapse between blocks as CSS's
  do. Paragraphs, headings and captions take boxes (padding, background,
  borders, corners), text-align with justify, text-indent; quotes their
  bar, padding and background; code blocks padding, corners and an edge;
  inline code its chip; lists their indent, `list-style-type` and
  `::marker`; tables padding, borders and row backgrounds; `hr` its border;
  links, `del` and `ins` their decorations; `body` its `max-width`.
  Fonts per element are formats made and cached per face, size, weight,
  slant and stretch; OpenType features are typographies per feature set.
- **Fonts** (`font_faces.h`): TTF, OTF, TTC, WOFF and WOFF2 files load
  into DirectWrite collections of their own; `local()` names a face this
  PC has. The HTML export copies the files beside the page.
- **Print and Word**: print layout asks the sheet with print media; its
  colours apply on paper only when it has `@media print` rules. Word's
  styles take the theme's type sizes and line height and the sheet's
  margins, heading borders and alignment, code shading and quote bar.
- **Live** reads the theme's code padding, list indent and quote colour.
- **Counters and generated content**: `::before` and `::after` with
  `counter()` and `counters()` around paragraphs, headings (Contents shows
  the numbers), list items, captions and inline spans; `counter-reset`
  and `counter-increment` as the page is laid out.
- **Folder sheets**: `tinta.css` in a document's folder or above styles its
  documents under any theme, as a third origin; a chip at the top of the
  page says so and turns it off per document; saves restyle; the HTML
  export carries it.
- **`cssclasses`** in the front matter put classes on `body`.
- **The inspector** (`inspector.h`): Ctrl+Alt+click relays the page out
  recording where each block and run was drawn and in which style; the
  card lists `Engine::explain()`'s rules, beaten ones struck through, with
  swatches; a click on a rule opens its sheet at its line.
- **Import**: Settings > Appearance > Themes > Import runs
  `css::importTheme` on a Typora or Obsidian sheet, writes it beside the
  other themes with what was left out at its top, copies its font files and
  switches to it.
- **`box-shadow`**, **`opacity`** on pictures and **`caret-color`**.
- **Scholar** also gained inline `references:` (`tests/inline-references-validation.md`).

## Fixtures

- `tests/fixtures/theme-css/book.css` over `mixed.md`: justified text with
  a first-line indent, numbered small-caps headings, square and roman
  markers, boxed quotes and code with shadows, a striped table with only
  bottom borders, an arrow after the outside link.
- `tests/fixtures/theme-css/folder/tinta.css` with
  `folder/notes/project-note.md`: a folder sheet under any theme, its chip,
  and `cssclasses: [wide]`.
- `tests/fixtures/theme-css/import/` (Typora's Night, Obsidian's Dusk and
  Midnight) for the import.

## Automated checks

`tests/page_style_tests.cpp`, run by `inline_code_theme`, lays the page out
under sheets and checks the drawn primitives: the reading column; centred
text; the `p + p` indent and `p`'s margin; a link with its rule taken away;
the inline code chip's corners; a heading's border as its rule; the quote's
bar, indent and colours, and `blockquote strong`; square and roman markers
in `::marker`'s colour; the code block's corners, edge and padding; the
table's grid, padding and a stripe on the second body row only; `hr`'s
border; and the same page under Paper with its round bullets and square
boxes. Then `@font-face` (a file under a family of the sheet's own, a
missing file reported, a heading falling through to the sheet's face, the
HTML export's copy), counters and generated content (Contents included,
the arrow on the outside link only, `li::before`), `cssclasses`, the
import (listed, applied, notes at the top, the next name on a second
import), Settings with its Import button (saved wide and narrow), the
inspector's card for `blockquote strong` (path, winner with file and line,
the beaten rule, Tinta's bold), folder sheets (found a folder up, under a
built-in theme, the chip, the export, off and on again), print with and
without `@media print`, Live's look, Word's styles, `box-shadow`, image
`opacity` and `caret-color`. `css_tests`, `css_import` and `yaml` cover
the engine's `explain()`, the import and the YAML reader.

## In the built app

Frames saved by the tests and looked at: `page-styles.png`,
`css-book.png`, `css-paper-light.png`, `css-ink-light.png`,
`css-ink-dark.png`, `css-night-dark.png`, `inspector.png`,
`folder-sheet.png`, `settings-appearance.png` and
`settings-appearance-narrow.png`. Two things the Book preview showed were
fixed: a strike through sat near the baseline under a sheet's line height,
and a table whose sheet set only bottom borders still drew vertical lines
in the quote's colour. Real windows cannot be captured over this machine's
remote session, so the inspector's card, the chip's click and Settings'
Import were driven through their functions rather than by the mouse.

## Limitations

- Justified text widens spaces; Chinese, Japanese and Korean lines, which
  have none, stay as they are.
- Margins collapse between siblings, and through a quote or list into its
  first child; Tinta's own spacing, where the sheet sets none, adds to
  a margin rather than collapsing with it.
- `li::before` draws in a list item's first run of text; an item that
  starts with a paragraph shows the paragraph's own `p::before`.
- Inline code's vertical padding is not drawn: the chip spans the line.
- `border-radius` rounds boxes, not pictures; an image takes `opacity`
  only.
- The inspector shows the rules and opens the sheet at a line; it does not
  edit values in place.
- An arrow such as U+2197 in generated content takes the emoji font's
  shape where the theme's face lacks it, as elsewhere in Tinta.
