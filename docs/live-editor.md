# Live: write on the page

Live is the second way to edit a document in Tinta. **Source** is the
editor Tinta has always had: the Markdown as typed, with the preview beside
it. **Live** shows the page as it reads while you write it, and shows the
Markdown only where you are typing. The pill at the top of the edit rail
switches between them, and so does **Ctrl+/**. Tinta remembers the choice.

Sheet cards are typed in with the same engine, so a note is written the
same way on a sheet and in its own tab.

## What Live keeps to

1. **Markdown is the document.** Live edits the same text as Source, byte
   for byte, with the same undo. Switching changes nothing in the file and
   keeps the caret on its character, at its height in the window.
2. **What you see is the reading view.** The page is the reading view's own
   layout: headings, lists, tables, code in its colours, Mermaid and
   PlantUML diagrams, maths, pictures, callouts, footnotes and properties.
   Live adds no second renderer that could disagree with it.
3. **Markdown where you type.** The blocks that hold the caret or the
   selection show their Markdown on a faint wash, in the page's own faces
   and sizes. The caret's line shows exactly what was typed, with its marks
   muted. The other lines keep their marks hidden: bullets show as bullets,
   task boxes as boxes, links in the link colour, and `**bold**` as bold.
   List items stand where the page has them, so text does not jump when a
   block opens.
4. **Fast.** A key lays out one block, not the page. When the caret leaves
   a block you changed, the page is laid out again. The page is also laid
   out again after a pause in typing, as Source's preview is.

## Typing

- **Enter** continues what the line is. A list item makes the next item:
  a number counts on and the numbers below it follow, and a task gets an
  empty box. A quote line makes the next quote line. Enter on an empty
  item ends the list (in a nested list, it moves the item out a level
  first). On a table row Enter makes the next row, and on an empty last
  row it ends the table. On an opening fence Enter writes the closing one
  too. Inside a fence, Enter keeps the line's indent.
- **Shift+Enter** is a new line inside the same item. **Ctrl+Enter** starts
  a new line below the caret's line, wherever the caret is in it.
- **Tab / Shift+Tab**:
  - On a list item, Tab nests it under the item above and Shift+Tab moves
    it back out to its parent.
  - In a table, they move to the next and the previous cell, selecting its
    text. Tab after the last cell adds a row.
  - In a link's text, Tab goes to its address.
  - In code, Tab adds four spaces.
- **Backspace** right after an item's, quote's or heading's mark takes the
  whole mark off. The next Backspace joins the line to the one above.
  **Delete** at a line's end joins the next line and takes its mark with
  it.
- **Pairs.** With words selected on one line, typing `*`, `_`, `` ` ``,
  `~` (makes `~~`), `=` (makes `==`), `[`, `(` or `"` wraps them. The
  selection stays inside, so a second `*` makes them bold.
- **Format keys** work on the selection or the word at the caret, and take
  the format off when it is already there: **Ctrl+B** bold, **Ctrl+I**
  italic, **Ctrl+K** link (a selected address becomes the link's target).
- **Paste.** An address pasted over selected words makes them its link. A
  picture is saved beside the document and a spreadsheet's range becomes a
  table, as in Source. Lines pasted on a list item become items of that
  list, as Enter makes them: numbers count on (and the numbers below
  follow), and a task gets an empty box. Lines pasted in a quote are
  quoted. Blank lines stay blank, lines that start with a mark of their own
  keep it, and text holding a table or a fence goes in as it is.
  **Ctrl+Shift+V** pastes the text as it is.
- **Alt+↑ / Alt+↓** move the caret's lines, or the selected ones, up and
  down.
- **[[** lists the notes in the document's folder. The arrow keys choose
  one and Enter writes the link.
- **Ctrl+Z / Ctrl+Y** are Source's undo and redo, which take typing back a
  word at a time.

## Moving and selecting

- The arrow keys move through the text. Up and Down go on into the blocks
  above and below, keeping the caret's column, and those blocks open as
  the caret enters them.
- **Page Up / Page Down** scroll a screen and take the caret along.
  **Home** goes to where the line's text starts, after its mark, and a
  second Home to the line's start. **Ctrl+Home / Ctrl+End** go to the
  document's ends.
- A click on the page opens the block there and puts the caret on the word
  clicked. A double-click selects a word and a triple-click selects a line.
  Dragging selects, across blocks too, and the page scrolls along at the
  window's edges; a drag after a double- or triple-click goes by words or
  lines, and keeps the first one whichever way it runs.
- On a touchscreen a finger pans the page and a tap clicks. A finger held on
  a word selects it, the selection grows by words as the finger slides, and
  the menu (below) opens over them where it lifts. While the menu is up, a
  finger works it as the mouse does.
- A click on a task box, on the page or in an open block, ticks or clears
  it without moving the caret.
- **Ctrl+click** follows a link: a note opens in a tab, a heading scrolls
  into view and an address opens in the browser.
- A right-click opens the menu (below) with the caret at the click.
- The page scrolls as the reading view does: the wheel scrolls it, the
  scrollbar at its right edge drags, and a press on the scrollbar's track
  moves the thumb there. The scrollbar takes the pointer, so a press on it
  leaves the caret where it is. The page keeps its own place apart from
  Source's, and a switch between them carries the caret across at its
  height in the window.

## Making things

**The slash menu.** Type `/` at a line's start or after a space and a menu
opens under the caret. Typing narrows it; English words find entries in
any language, so `/table` works everywhere. The arrow keys choose and Enter
makes the block, with the caret where its content goes. Esc closes the menu
and keeps what was typed. Each row shows the Markdown it writes.

| Group | Entries |
| --- | --- |
| Text | Heading 1, Heading 2, Heading 3, Quote, Divider |
| Lists | Bulleted list, Numbered list, Task list |
| Callouts | Note, Tip, Important, Warning, Caution |
| Code | Code block, Inline math, Math block |
| Diagrams | Flowchart, Sequence diagram, Class diagram, State diagram, Gantt chart, Pie chart, Mind map, PlantUML diagram |
| Insert | Table (its first cell selected), Image… (picked from disk, linked relative to the document), Link, Footnote (the mark at the caret, the note at the end where the caret goes), Properties (front matter) |

A heading, quote or list made on a line that has text keeps that text. A
block made on a line with text goes on new lines below it, with the blank
lines Markdown needs around it.

**The format bar.** Words selected on one line show a small bar over them:
bold, italic, strikethrough, code, highlight, link, and Heading 1 to 3. A
format already on the selection shows lit, and its button takes it off.

**The rail.** The edit rail's text and block buttons (bold, italic,
strikethrough, code, link, bullets, numbers, tasks, quote) act on the Live
text. A block button marks every line of the selection, the numbers
counting on, and takes the mark off them all when they all have it. Its
table and diagram buttons open the menu's table grid and diagrams beside
it, and its picture button picks a picture to link at the caret.

## The menu

A right-click on the page opens the menu at the pointer, and the caret
goes where the click was; a click inside the selection keeps the
selection. The menu key and Shift+F10 open it under the caret. What the
pointer is on decides what it offers:

- **The formats**, along its top: bold, italic, strikethrough, code,
  highlight and link, for the selection or the word under the pointer. A
  format already there shows lit, and its button takes it off. Pointing at
  a button shows its name and key. With lines selected the formats wait,
  as the format bar does.
- **Cut, Copy and Paste.** Paste makes a screenshot a picture saved beside
  the document and a spreadsheet's range a table, as Ctrl+V does.
- **A link:** Open link (Open note for `[[name]]`), Copy link address,
  Edit link (its address selected to type over) and Remove link (its words
  stay).
- **A picture:** Replace image… and Copy image address.
- **A task:** Mark as done, or as not done. **A list item:** Indent and
  Outdent.
- **A table:** Insert row above or below, Insert column left or right,
  Delete row, Delete column and Align column (left, centre or right; the
  same again takes it off). The header and its delimiter row stay.
- **Code:** Copy code, its lines without the fences. Code and the
  properties have no formats, Turn into or Insert.
- **Turn into** makes the caret's line, or the selected lines, a
  paragraph, a heading, a quote, a list or code, with the kind they are
  now ticked. Their marks give way to the new kind's; quoted lines keep
  their quote, list items their indents, and numbers count on at each
  level. A table's rows stay table rows.
- **Insert** makes what the slash menu makes: a table of a size picked on
  a grid, a picture, a link, a footnote, a divider, code, maths, a
  callout, a diagram, or the properties. A block goes below the caret's
  line (below the table, when asked for in one); a link asked for over
  selected words makes them its words.
- **Select all.**

Submenus open beside their rows when the pointer rests on them, and stay
open while the pointer heads for them across other rows. The keys work
the menu too: Up and Down go through its rows, Right opens a submenu and
Left or Esc goes back, Enter picks, and a letter goes to the next row it
starts. On the formats Left and Right go along the buttons; on the grid
the arrows size the table.

## The pill

The pill is at the top of the edit rail: **#** for Source and **Aa** for
Live, with a knob that slides to the one showing. Clicking either side
switches, as does **Ctrl+/**. **Ctrl+E** in Live shows Source with its
preview beside it. Ctrl+Shift+E opens the reading view from either. Live
fills the window beside the rail, in the reading view's column. A plain
text, Mermaid or PlantUML file is its own source: it opens in Source, and
the pill's Live side is dimmed.

## Sheets

A card being typed in uses the same engine:

- the same keys, pairs, lists, tables and fences;
- the live drawing of the lines away from the caret;
- the slash menu and the format bar;
- the menu, from a right-click in the card or the menu key, where Open
  note brings a note on the sheet into view;
- the `[[` names, where Ctrl+Enter draws the arrow too;
- task boxes that tick with a click.

A card keeps what is a sheet's own: where its box sits, growing to fit
what is typed, and naming a new note after its first line.

## Not in this version

- No dragging blocks by a handle: cut and paste moves them, and Alt+↑ / ↓
  moves lines.
- A block that is a diagram, a formula or a picture keeps its drawing
  under its Markdown while open; it catches up after a pause in the typing.
  A picture inside a longer block shows as its Markdown until the caret
  leaves.
- Tables are typed in as their Markdown, with rows and columns from the
  menu. The reading view's cell editor stays Source's.
- One caret. Source keeps its several carets and Ctrl+D.
- No headings on Ctrl+1 to Ctrl+6, which switch tabs in Tinta. Use `/h1`,
  the format bar or `# `.
- No spell checking yet.
