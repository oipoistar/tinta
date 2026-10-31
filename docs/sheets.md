# Sheets

A sheet is an infinite canvas of notes. You lay cards out, join them with
arrows, gather them into groups, and zoom from the whole picture down to a
single paragraph. Every card is a real Markdown note in the sheet's folder,
so the notes open, search, sync and diff like any other file, and the sheet
itself is a readable Markdown file.

Sheets are drawn the way Tinta draws documents: Direct2D and DirectWrite, no
web engine. A note card is the note itself, laid out by Tinta's own
Markdown layout and kept as a bitmap until the note, the card's width, the
theme or the zoom changes. A sheet of 400 notes opens in about a fifth of a
second and draws a whole-sheet frame in about 20 ms.

## Making a sheet

- On the start page, or on the empty page of a new tab (+), choose **New
  sheet**. A Save dialog names the sheet: `Roadmap.smd` is created with a
  folder `Roadmap` beside it, and every note you add goes into that folder.
  The sheet opens in the empty tab's place.
- Name the sheet after a folder that already holds notes, and the new sheet
  lays all of them out in a grid.
- A `.smd` opens like any file: from the start page's recents, the file
  browser (B), Ctrl+O, the command line, or by dropping it on the window.
- An Obsidian canvas (`.canvas`) opens as a sheet too, and saves back as
  plain JSON Canvas. Its file paths run from the vault root, the nearest
  folder up that holds `.obsidian`.

## Working on a sheet

| Do | With the mouse | With the keys |
| --- | --- | --- |
| A new note | Double-click open ground | N |
| A note joined to the selected one | Drag a card's side dot to open ground | Tab (a child), Shift+Enter (a sibling) |
| Write in a card | Double-click it | Enter or E; Esc when done |
| Join two cards | Drag from a side dot onto the other card | |
| Move between cards | | Arrow keys (Shift adds to the selection) |
| Move cards | Drag; Alt+drag leaves a copy; Ctrl turns snapping off | Ctrl+arrows (Shift+Ctrl: one unit) |
| Resize | Drag an edge or the corner; double-click the bottom edge to fit the note | |
| Select | Click, Shift+click (again to take a card out), drag a box on open ground; drag any selected card to move them all | Ctrl+A |
| Group | Context menu | Ctrl+G, Ctrl+Shift+G to ungroup |
| Colour | The swatches in the context menu | 1 to 6, 0 for none |
| Pan | Wheel, Space+drag, middle drag, the minimap | |
| Zoom | Ctrl+wheel or a pinch, the zoom control | Ctrl+= and Ctrl+-, Ctrl+0 for 100%, Shift+1 everything, Shift+2 the selection |
| Jump to a card | | J, then its letters (Shift+J joins the selection to it) |
| Fold what follows a card | Its +N badge opens it again | Z (Shift+Z unfolds everything) |
| Read a card from far out | Rest the pointer on it | Hold Alt |
| Link another note | | [[ while typing in a card |
| Undo | | Ctrl+Z, Ctrl+Y |
| Copy and paste | | Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+D to duplicate |
| Remove from the sheet | Context menu | Delete |

A removed card's note stays in the folder (it waits in the tray, below). A
note made on the sheet and never written to goes with its card.

Files dropped on a sheet become cards where they land: notes, images, and
any other file as a card that opens it. Pasted text becomes a note, a pasted
address a link card, and a pasted Mermaid flowchart a set of cards.

## Ten things Obsidian's canvas does not do

1. **Semantic zoom.** Far out, a card is its title in type big enough to
   read, with a bar for its tasks and its word count. Close up it is the
   rendered note. Obsidian shows the same text at every zoom until it hides
   it.
2. **Keyboard-first mind mapping.** The arrow keys walk from card to card by
   direction; Tab grows a child to the right and Shift+Enter a sibling, each
   ready to type in; every command has a key, listed in the help (?).
3. **The sheet is its folder.** Notes that appear in the folder from
   anywhere wait in a tray ("2 notes not on the sheet") to be dragged in or
   placed all at once. A note changed outside Tinta redraws on its card; a
   renamed one is followed by its text; a deleted one is marked missing.
4. **Ghost links.** A `[[wiki link]]` or a relative `.md` link in a note
   draws a dashed line to the card it names. A click on it, or **Turn its
   links into arrows**, makes it a real arrow. Your writing shows on the
   board without drawing it twice. (L hides them.)
5. **Lanes that write front matter.** Make a group a lane (**Make it a
   lane...**, property `status`), and a note dropped into the lane gets
   `status: Doing` written into its front matter. **Sort notes into their
   lanes** moves the cards whose notes say otherwise, so a board and the
   notes' properties never disagree.
6. **Compile into one document.** The sheet, or the selection, becomes one
   Markdown document along the arrows: groups become sections, each note's
   headings move one level down, and its links and images still work. It
   opens in a tab, ready for Tinta's HTML, Word and PDF export.
7. **Present along the arrows.** P flies the camera from card to card in
   reading order, showing a whole group before its first card. Arrow keys,
   Space or clicks go on; Esc lands where you were.
8. **Minimap and remembered views.** The minimap shows the whole sheet and
   where you are; drag in it to travel (M hides it). Each sheet reopens where
   you left it, at the same zoom.
9. **Tidy and the Mermaid bridge.** Ctrl+L lays cards out in layers along
   their arrows (Ctrl+Shift+L top to bottom), with each group tidied inside
   and moved as one block; cards without arrows form a grid. Paste a Mermaid
   flowchart and it arrives as cards, arrows and groups; **Copy as Mermaid**
   turns the sheet back into a flowchart for any Markdown file.
10. **Find and spotlight.** F finds cards by their title or anything in
    their note and flies to each match. S, on a card, dims everything except
    the card and its neighbours along arrows and links.

And a few smaller ones: connections pick the facing sides by themselves as
cards move (Obsidian pins them), z-order is in the menu, cards align and
distribute, and nothing in the sheet is a web view: a link card shows its
address and opens it in your browser.

A new note starts as `Untitled.md` and takes its first line as its name
when you finish typing in its card, a heading or not (`#Idea` and `# Idea`
both make `Idea.md`; only the second is a heading, as in any Markdown).
An Untitled left in the sheet's folder by an earlier session does the same
the next time you type into it; a note with a name of its own keeps it, and
so does an Untitled in an Obsidian vault, where other notes may link to it.

## Ten more

1. **A hint line.** Above the tool bar, one quiet line says what can be
   done now: with two cards selected, how to move them; while a new note
   is typed, that its first line names it. Each hint retires once it has
   been done three times; **Hide hints** in the menu puts them all away and
   **Show hints again** brings them back.
2. **The selection bar.** Over any selection: the six colours and none,
   Group, for several cards **Join in order** (arrows in rows, left to
   right, where there are none yet) and **Align**, for a card with arrows
   out Fold, and Remove.
3. **Live writing.** A card is typed in with the engine of Tinta's Live
   editor (`docs/live-editor.md`). The caret's line shows its Markdown as
   typed and every other line shows it drawn: headings without their #,
   bullets, task boxes (a click ticks them), bold and italic, code on its
   background, `[[links]]` and `[links](...)` in the accent, quotes with
   their bar. A line that starts `#word`, which Markdown does not take for
   a heading, gets a mark where the space belongs. `/` opens the slash menu
   of blocks to insert, words selected on one line show the format bar, and
   lists, tables and fences go on as in Live. A right-click in the card
   (or the menu key) opens Live's menu on its text: the formats, the
   clipboard, Turn into, Insert, and a table's rows and columns; elsewhere
   a right-click opens the sheet's own menu.
4. **`[[` names.** Typing `[[` in a card lists the notes on the sheet and
   in its folder; the arrow keys choose, Enter writes the link and
   Ctrl+Enter draws the arrow too.
5. **Peek.** Far out, rest the pointer on a card (or hold Alt) and its note
   opens in a lens beside it at reading size; the wheel scrolls it, a click
   on it flies there.
6. **Jump labels.** J puts a letter on every card in view; typing one picks
   its card, with Shift added to the selection. Shift+J joins the
   selection to the card picked.
7. **Folds.** Z folds away what follows a card along its arrows (a card
   something else still points at stays); a +N badge marks the fold and a
   click on it opens it. A folded card moves with what it holds; presenting,
   compiling, find and the arrow keys pass folded cards by; Shift+Z
   unfolds everything. Folds are saved with the sheet.
8. **Arrows written into notes.** With **Write arrows into the notes** (the
   menu on open ground) an arrow from one note to another adds
   `related: "[[Other]]"` to the first one's front matter, and removing
   the arrow takes it out again. Turning it on writes the arrows already
   drawn; a new note gets its links once it has a name.
9. **Explode a document.** **Explode a document into cards...** (on open
   ground) cuts a Markdown document at its `##` sections, each a note in
   the sheet's folder, chained in reading order; two or more `#` headings
   become groups, and images and links keep working. **Open as a sheet** in
   a document's context menu makes a new sheet of it beside it; **Split by
   headings** on a card does the same to one note.
10. **Lanes that stack.** A lane keeps its cards in a column: a card
    dropped in takes its place, the lane it left closes up, and the lane
    grows to hold them. **Card limit...** sets how many it holds before its
    border and count turn red.

## The .smd format

A `.smd` is Markdown: front matter, a title, an outline of the cards that
reads anywhere, and the canvas itself in a fenced block at the end.

````markdown
---
folder: Roadmap
---

# Roadmap

- [Kickoff](<Roadmap/Kickoff.md>) → Research (first)
- <https://jsoncanvas.org>

## Discovery

- [Research](<Roadmap/Research.md>)

```json canvas
{
	"nodes":[
		{"id":"d1","type":"group","label":"Discovery","x":-40,"y":-80,"width":800,"height":500},
		{"id":"k1","type":"file","file":"Roadmap/Kickoff.md","x":0,"y":0,"width":360,"height":240}
	],
	"edges":[
		{"id":"e1","fromNode":"k1","toNode":"r1","label":"first"}
	]
}
```
````

- `folder:` is where new notes go, relative to the `.smd`. It may name a
  sibling folder (`../Notes`), never a drive or another computer.
- The block is [JSON Canvas 1.0](https://jsoncanvas.org/spec/1.0/), the
  format of Obsidian's canvases, written one node or edge per line so it
  diffs well. Copy it into a `.canvas` file and Obsidian opens it. File paths
  run from the `.smd`'s folder.
- Tinta adds a few fields: `tintaLane` on a group names the property its
  lane writes and `tintaLimit` how many cards it holds; `tintaFolded` marks
  a folded card; `tintaWriteLinks` at the top writes arrows into the notes.
  Fields Tinta does not know are kept as they are.
- The outline is written fresh on every save; only the block is read back.
  A block that does not parse leaves the sheet read-only, so a broken file
  is never written over.

## Obsidian's canvas, studied

Obsidian's Canvas, the model for most note-taking canvases, was looked at as
of October 2026 (Obsidian 1.14, its help, changelog, forum and its most
downloaded canvas plugins).

**What it has.** Text, note, media, web page and group cards; adding by
double-click, toolbar, menu, paste and drag from the file list; connections
dragged from side handles, with labels, colours and arrow ends; groups with
labels, colours and background images; pan and zoom with the usual design
tool keys; selection, alignment, snapping, duplicate by Alt+drag, copy and
paste, undo; six preset colours; a zoom level below which card content is
hidden; PNG export. A canvas is a `.canvas` JSON file in the vault, and a
note card is the note itself, edited in place.

**What works.** Cards that are live files, many quick ways to make a card,
familiar keys, and an open, readable format that grew an ecosystem of more
than two hundred plugins.

**What people miss**, judging by the forum and the plugins they install:
search inside a canvas; keyboard creation and navigation (Canvas Mindmap);
presentation along the lines (Advanced Canvas, Canvas Presentation); a
minimap; a view that stays where it was; lines that re-anchor themselves;
lines that mean something to the notes they join; a titles-only overview
when zoomed out; collapsible groups, locks and shapes.

**Where Sheets lands.** Sheets keep the strengths (cards are files, the
format is JSON Canvas, the keys are the familiar ones) and build the
missing parts in: find, keyboard flow, presentation, minimap, remembered
views, floating connections, ghost links and lanes that make the board and
the notes agree, and semantic zoom. A sheet lives in a tab like any
document, so there is no sidebar to keep open: the start page makes one,
recents and tabs bring it back.

## Not yet

- No web page embeds or video players: a link card opens its address in
  your browser. This keeps the sheet free of a web engine.
- PDFs, videos and other files are cards that open in their own app.
- No freehand drawing or shapes, no nested sheets, no image export of the
  whole sheet.
- A card's text is edited as Markdown shown live (the caret's line as
  typed); images and diagrams show again once you leave the card.
- On a touchscreen a finger pans the sheet (a flick carries on), a tap
  selects, a double tap writes a note or opens a card, and a held press opens
  the menu. Pinch zoom works on a touchpad, not yet on a screen, and moving
  cards takes a mouse or a pen.
- Sheets add about 450 KB to `tinta.exe`, most of it the sheet code and its
  strings in eight languages.
