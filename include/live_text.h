// Live text: Markdown typed with the page drawn around it (docs/live-editor.md).
// The editing engine Tinta's Live editor and sheet cards share: the text,
// its caret and selection, undo, the edits Markdown's rules call for, the
// keys and characters that make them, the text laid out live (the caret's
// line as typed, the others as they read), the slash menu and [[ names
// under the caret, the format bar over a selection, and the menu a
// right-click opens on the text.
#pragma once

#include "app.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct LiveText {
    std::wstring text;
    size_t caret = 0, anchor = 0;
    bool multiline = true;              // Markdown; false: a one-line field
    float preferredX = -1.0f;           // the column Up and Down keep, in layout units

    // Undo keeps what each step changed, not the whole text
    struct Piece {
        size_t at = 0;
        std::wstring removed, inserted;
    };
    struct Step {
        std::vector<Piece> pieces;
        size_t caretBefore = 0, anchorBefore = 0, caretAfter = 0, anchorAfter = 0;
    };
    std::vector<Step> undo, redo;
    unsigned long long lastTypeMs = 0;  // typing within a pause is one step
    size_t coalesceAt = (size_t)-1;     // where the caret was when it was

    // A host keeping its own copy of the text follows these
    struct Change {
        size_t at = 0, removed = 0;
        std::wstring inserted;
    };
    bool trackChanges = false;
    std::vector<Change> changes;
    unsigned long long revision = 0;    // counts every change
    bool changed = false;               // since the owner last took the text

    float scrollY = 0.0f;               // layout units a box is scrolled by
    bool selectingWithMouse = false;
    size_t clickCount = 0;
    unsigned long long lastClickMs = 0;
    size_t pressFrom = 0, pressTo = 0;  // the word or line a double or triple click took

    // The slash menu and [[ names under the caret
    enum class Popup { None, Slash, Names };
    struct Choice {
        std::wstring label, detail;     // what the row says, and what it says beside it
        int insert = -1;                // Slash: the block it makes (live::Insert)
        std::string id;                 // Names: the owner's id for it
    };
    Popup popup = Popup::None;
    size_t popupFrom = 0;               // where what is typed for it starts
    size_t dismissedSlash = (size_t)-1; // a / whose menu Esc put away
    std::vector<Choice> choices;
    int choice = 0;
    int popupTop = 0;                   // the first row shown
    // Drawn rows and buttons, in client pixels, and the one under the pointer
    std::vector<std::pair<D2D1_RECT_F, int>> popupHits, barHits;
    int hoverHit = -1;

    // The menu a right-click opens on the text, and the submenus open from it
    struct MenuRow {
        enum Kind { Action, Submenu, Formats, Grid, Separator } kind = Action;
        int id = 0;                     // a live::MenuCommand, or the live::MenuSub it opens
        std::wstring label, keys;       // keys: its shortcut, on the right
        int badge = -1;                 // its picture: a live::Insert, as the slash menu draws it
        bool enabled = true, checked = false;
    };
    struct MenuPanel {
        int sub = 0;                    // which (live::MenuSub)
        int parentRow = -1;             // the row of the panel before it that opened it
        std::vector<MenuRow> rows;
        int focus = -1;                 // the row lit, under the pointer or by the keys
        int button = -1;                // on the formats row: the button lit
        float width = 0;                // as wide as its widest row
        // Where it is (client pixels), worked out from where the menu opened
        D2D1_RECT_F box{};
        std::vector<D2D1_RECT_F> rowBoxes, buttonBoxes;
    };
    struct MenuState {
        bool open = false;
        float x = 0, top = 0, bottom = 0;  // where it opened: below `bottom`, or ending at `top`
        D2D1_RECT_F bounds{};           // where it may draw
        size_t at = 0;                  // the text under the pointer, which decides its rows
        std::vector<MenuPanel> panels;  // the menu, then each submenu open from it
        bool keys = false;              // the keys moved last: the pointer lights nothing until it moves
        D2D1_POINT_2F pointer{-1, -1};  // the pointer's last place, for the way to a submenu
        int cols = 3, rows = 3;         // the table grid's size
        // What the host can do beyond the text
        bool canOpen = false, canPickImage = false, canPasteMore = false;
    } menu;

    bool hasSelection() const { return caret != anchor; }
    size_t selStart() const { return caret < anchor ? caret : anchor; }
    size_t selEnd() const { return caret < anchor ? anchor : caret; }
};

namespace live {

// --- Text --------------------------------------------------------------------

bool isWordChar(wchar_t c);
size_t wordLeft(const std::wstring& text, size_t at);
size_t wordRight(const std::wstring& text, size_t at);
size_t lineStart(const std::wstring& text, size_t at);
size_t lineEnd(const std::wstring& text, size_t at);
// A press at `at`, its click the e.clickCount-th: the first puts the caret
// there (with Shift the anchor stays), the second takes the word, the third
// the line. A drag on to `at` grows the selection by the same steps and
// keeps that word or line whichever way it runs
void pressAt(LiveText& e, size_t at, bool shift);
void dragTo(LiveText& e, size_t at);
// The line starting at `begin`: 0 outside a fence, 1 opening one, 2 inside, 3 closing it
int fenceState(const std::wstring& text, size_t begin);
// The same for every line at once, given where they start
void fenceLines(const std::wstring& text, const std::vector<size_t>& lineStarts, std::vector<char>& states);

// The mark a line starts with: its indent, then a list, task, quote or
// heading mark ("- ", "3. ", "- [ ] ", "> ", "## ")
struct LineMark {
    enum Kind { None, Bullet, Number, Task, Quote, Heading } kind = None;
    size_t indent = 0;      // spaces before the mark
    size_t length = 0;      // the mark with its space
    int number = 0;         // a numbered item's number, a heading's level
    wchar_t symbol = 0;     // '-', '*', '+', '.', ')', '>' or '#'
    bool done = false;      // a ticked task
};
LineMark lineMark(const std::wstring& text, size_t lineBegin);
// How far a line's quote marks run ("> ", "> > "), from its start; 0: none
size_t quoteLength(const std::wstring& text, size_t lineBegin);

// The link around a text position: [words](address), ![words](picture),
// [[name]] or [[name|words]], or an address written out
struct LinkSpan {
    enum Kind { None, Inline, Image, Wiki, Address } kind = None;
    size_t from = 0, to = 0;                // all of it
    size_t textFrom = 0, textTo = 0;        // the words it shows
    size_t addressFrom = 0, addressTo = 0;  // where it goes (a wiki link: the name)
};
LinkSpan linkAt(const std::wstring& text, size_t at);

// Where a text position is in a table (rows starting with |, the second
// its delimiter row)
struct TableSpot {
    bool in = false;
    size_t first = 0, last = 0;     // its first and last rows' starts
    size_t row = 0;                 // the start of the position's row
    int index = 0;                  // that row: 0 the header, 1 the delimiter row, then the rest
    int column = 0, columns = 0;    // the position's cell, and how many the header has
    int align = 0;                  // that column's alignment: 0 none, 1 left, 2 centre, 3 right
};
TableSpot tableAt(const std::wstring& text, size_t at);

// The code between a fence's lines around a text position; false outside one
bool fenceAround(const std::wstring& text, size_t at, size_t& from, size_t& to);

// The block a line is, and what Turn into makes of lines
enum Block { BLOCK_TEXT, BLOCK_H1, BLOCK_H2, BLOCK_H3, BLOCK_QUOTE, BLOCK_BULLETS, BLOCK_NUMBERS, BLOCK_TASKS, BLOCK_CODE,
             BLOCK_OTHER };
int blockAt(const std::wstring& text, size_t at);

// The Markdown inside a line, as the page draws it. Live's drawing and the
// source editor's colours both read lines through this, so they agree on
// what is code, a link or bold
enum InlineKind {
    INL_ESCAPE,                         // a backslash, keeping the character after it as typed
    INL_CODE_MARK, INL_CODE,            // `code`: its backticks, then its code
    INL_MATH_MARK, INL_MATH,            // $x$
    INL_WIKI_MARK, INL_WIKI,            // [[name|words]]: the brackets and the name, the words shown
    INL_FOOTNOTE,                       // [^1]
    INL_LINK_MARK, INL_LINK,            // [words](address): [ and ](address), the words
    INL_IMAGE_MARK, INL_IMAGE,          // ![words](address)
    INL_STRONG_MARK, INL_STRONG,        // **bold**
    INL_EM_MARK, INL_EM,                // *italic*
    INL_STRIKE_MARK, INL_STRIKE,        // ~~struck~~
    INL_HIGHLIGHT_MARK, INL_HIGHLIGHT,  // ==marked==
    INL_ADDRESS,                        // an address written out
};
struct InlineSpan {
    int kind;
    size_t at, length;                  // in the line
};
// The spans of a line from `from` on (past its list, quote or heading mark), in order
void inlineSpans(std::wstring_view line, size_t from, std::vector<InlineSpan>& spans);

// --- Edits -------------------------------------------------------------------
// Each is one undo step (typing within a pause is one); the caret ends
// after what changed

void pushUndo(LiveText& e, bool coalesce);
void splice(LiveText& e, size_t at, size_t removed, const std::wstring& with);  // after pushUndo
void replace(LiveText& e, const std::wstring& with, bool coalesce = false);    // the selection
void typeCharacter(LiveText& e, wchar_t ch);   // a selection on one line is wrapped by a pair
void newline(LiveText& e, bool plain);         // lists, quotes, tables and fences go on
void backspace(LiveText& e, bool word);        // a line's mark goes first
void deleteForward(LiveText& e, bool word);
void indent(LiveText& e, bool out);            // a list item under the one above
void tab(LiveText& e, bool back);              // table cells, a link's address, indents
void paste(LiveText& e, const std::wstring& text);  // an address over words links them
bool undo(LiveText& e, bool redo);
// Formats: on the selection, else the word at the caret; off when on already
void toggleWrap(LiveText& e, const std::wstring& mark);
void makeLink(LiveText& e);
void setHeading(LiveText& e, int level);       // the same level again: a paragraph
// The rail's list and quote buttons, on every line of the selection
void toggleLineMark(LiveText& e, LineMark::Kind kind);
// A line's mark put on, swapped or taken off
struct MarkEdit {
    size_t at = 0, removed = 0;
    std::wstring with;
};
// What toggleLineMark makes of these lines (their starts, top down), for the
// source editor's own text: the mark on each one, or off them all when they
// all have it. Numbers count on at each indent; the edits come top down.
std::vector<MarkEdit> lineMarkEdits(const std::wstring& text, const std::vector<size_t>& lines, LineMark::Kind kind);
// The text from the first edit's start to the last one's end as they leave
// it, to put in with one splice; and a position through them (at or inside
// a mark, after the mark that takes its place)
std::wstring markedSpan(const std::wstring& text, const std::vector<MarkEdit>& edits);
size_t throughMarks(size_t at, const std::vector<MarkEdit>& edits);
void toggleTask(LiveText& e, size_t lineBegin);
void moveLines(LiveText& e, bool up);          // the caret's lines past the one above or below
// The selection's lines made a block of another kind: their marks go and
// the block's come, quotes and lists keeping their place; code gets fences
void turnInto(LiveText& e, int block);
// A table's rows and columns, at the cell of `at`; false when there is no
// table there, or the edit would take its header or its last column
enum TableEdit { TABLE_ROW_ABOVE, TABLE_ROW_BELOW, TABLE_COLUMN_LEFT, TABLE_COLUMN_RIGHT, TABLE_DELETE_ROW,
                 TABLE_DELETE_COLUMN, TABLE_ALIGN_LEFT, TABLE_ALIGN_CENTER, TABLE_ALIGN_RIGHT };
bool editTable(LiveText& e, size_t at, int edit);
// The link at `at` made plain text (its words stay), or pointed elsewhere
void unlink(LiveText& e, size_t at);
void relink(LiveText& e, size_t at, const std::wstring& address);
// A note's text and back: without and with its CRLF and byte order mark
std::wstring forEditing(std::string utf8);
std::string forSaving(const std::wstring& text, const std::string& original);

// --- Insertions (the slash menu) ----------------------------------------------

enum Insert {
    INS_H1, INS_H2, INS_H3, INS_QUOTE, INS_DIVIDER, INS_BULLETS, INS_NUMBERS, INS_TASKS,
    INS_NOTE, INS_TIP, INS_IMPORTANT, INS_WARNING, INS_CAUTION,
    INS_CODE, INS_INLINE_MATH, INS_MATH,
    INS_FLOWCHART, INS_SEQUENCE, INS_CLASS, INS_STATE, INS_GANTT, INS_PIE, INS_MINDMAP, INS_PLANTUML,
    INS_TABLE, INS_IMAGE, INS_LINK, INS_FOOTNOTE, INS_PROPERTIES, INS_COUNT
};
// What the slash menu offers for what was typed after the /, best first
std::vector<LiveText::Choice> slashChoices(App& app, const std::wstring& typed);
// Makes it in place of the / and what followed (or of the selection); the
// caret lands where its content goes. An image's path comes from the host;
// a table has `columns` and, under its header, `rows`
void applyInsert(LiveText& e, int insert, const std::wstring& image = {}, int columns = 2, int rows = 1);

// The format bar's buttons
enum Format { FMT_BOLD, FMT_ITALIC, FMT_STRIKE, FMT_CODE, FMT_MARK, FMT_LINK, FMT_H1, FMT_H2, FMT_H3, FMT_COUNT };
void applyFormat(LiveText& e, int format);
// Whether a format is on the selection, or with none on the word at the caret
bool formatOn(const LiveText& e, int format);

// --- Keys ----------------------------------------------------------------------

// What the box around the text answers for the engine
struct Host {
    // The caret, and the text position at a point, in layout units; Up and
    // Down say which way they go, for a host with more text past its box
    std::function<bool(float& x, float& y, float& height)> caretPoint;
    std::function<size_t(float x, float y, int direction)> positionAt;
    // [[ names for what was typed after the brackets, and one picked (Ctrl held)
    std::function<std::vector<LiveText::Choice>(const std::wstring& typed)> names;
    std::function<void(const LiveText::Choice& choice, bool ctrl)> picked;
    // An image to insert, as the text will link it (empty: none)
    std::function<std::wstring()> pickImage;
    // Undo and redo kept by the host instead
    std::function<void(bool redo)> undo;
    // A link to follow, from the menu: an address, or "wiki:" and a note's
    // name. Without it the menu offers no Open
    std::function<void(const std::string& url)> openLink;
    // A paste of what is more than text (a picture, a spreadsheet's range):
    // true when the host made it, false to paste the clipboard's text
    std::function<bool()> paste;
};
enum class Result { Unhandled, Handled, Commit, Cancel };
// A key: Esc comes back as Cancel, Enter in a field or with Ctrl as Commit;
// typing keys come back Unhandled, as their characters follow
Result key(App& app, HWND hwnd, LiveText& e, WPARAM key, bool ctrl, bool shift, const Host& host);
// A character typed
void character(App& app, LiveText& e, wchar_t ch, const Host& host);
// The popup follows the caret: opened, narrowed or put away
void updatePopup(App& app, LiveText& e, const Host& host);
void closePopup(LiveText& e);
void pickChoice(App& app, LiveText& e, int index, bool ctrl, const Host& host);

// --- Layout and drawing --------------------------------------------------------

// How live text looks, sizes in layout units
struct Look {
    std::wstring face, headingFace, codeFace;
    float size = 15.0f, codeSize = 14.0f;
    float headingSize[6] = {24.0f, 20.25f, 17.7f, 16.2f, 15.0f, 15.0f};
    float lineHeight = 0.0f;            // as a multiple of a line's font size; 0: the font's own
    float codeInset = 6.0f;             // a fenced block's background past its text
    float listIndent = 0.0f;            // a list item's text past its mark, as the page has it; 0: as typed
    D2D1_COLOR_F text{}, accent{}, link{}, code{}, codeBack{}, inlineCodeBack{}, quote{}, mark{};
    D2D1_COLOR_F heading[6]{};
    bool markdown = true;               // false: a plain field on one line
};
// The look of the page around it: the reading view's faces, sizes and colours
Look pageLook(const App& app);
// The layout of text[from, to), made again when the text, the width, the
// look, the caret's line or the render target change
struct LayoutCache {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    std::wstring text;
    float width = -1;
    size_t from = 0;
    size_t caretLine = (size_t)-1;
    std::wstring key;                   // the look it was made for
    unsigned generation = 0;
    float factor = 1.0f;                // line spacing over the font's own
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> markBrush, linkBrush, glyphBrush, codeBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> headingBrush[6];
    // A code block's colours, as the page gives its tokens
    std::vector<std::pair<unsigned, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>>> tokenBrushes;
    // Spans drawn behind the text: inline code and ==marks== (layout positions)
    struct Back {
        size_t at, length;
        D2D1_COLOR_F color;
        bool block;                     // a fenced block: across the box's width
    };
    std::vector<Back> backs;
};
// `caretLine`: the line shown as typed, counted from `from`; -1 for none
IDWriteTextLayout* layoutOf(App& app, const LiveText& e, size_t from, size_t to, float width, const Look& look,
                            LayoutCache& cache, size_t caretLine);
size_t caretLineOf(const LiveText& e, size_t from, size_t to);
// Where text position `at` is in a layout of text from `from`
bool caretPoint(IDWriteTextLayout* layout, size_t from, size_t at, float& x, float& y, float& height);
// The text position at a layout point
size_t positionAt(IDWriteTextLayout* layout, size_t from, size_t to, float x, float y);
// Draws the backgrounds, the selection, the text, the caret (when on) and
// the mark where a #word wants its space. The target's transform is in
// layout units with the layout's origin at 0,0; `pixel` is one pixel there
void draw(App& app, const LiveText& e, IDWriteTextLayout* layout, const LayoutCache& cache, size_t from,
          const Look& look, bool caretOn, float pixel);
// The task box drawn at a layout point, or (size_t)-1: its line's start
size_t taskBoxAt(IDWriteTextLayout* layout, const LiveText& e, size_t from, size_t to, float x, float y);

// The popup under a caret at `caret` (its top and bottom, client pixels),
// kept inside `bounds`; rows hit by the pointer go in e.popupHits
void drawPopup(App& app, LiveText& e, D2D1_POINT_2F caretTop, float caretBottom, const D2D1_RECT_F& bounds);
// The format bar over a selection starting at `top` (client pixels)
void drawFormatBar(App& app, LiveText& e, D2D1_POINT_2F top, float bottom, const D2D1_RECT_F& bounds);
// What a click at a client point hits: a popup row (its index), a format
// button (FMT_* + 1000) or -1
int hitAt(const LiveText& e, float x, float y);
// The slash menu's picture for an insert, in a square; the menu's own for
// text and a column's alignments
constexpr int kBadgeText = 1000, kBadgeAlignLeft = 1001, kBadgeAlignCenter = 1002, kBadgeAlignRight = 1003;
void drawBadge(App& app, int insert, const D2D1_RECT_F& r);
// A format's button face: its letter or drawing, lit when the format is on,
// faint when it cannot be used
void drawFormatButton(App& app, int format, const D2D1_RECT_F& r, bool on, bool hover, bool enabled = true);

// --- The menu ---------------------------------------------------------------------
// A right-click on the text opens it (docs/live-editor.md, "The menu"): the
// formats, the clipboard, what is under the pointer (a link, a picture, a
// task, a list item, a table, code), Turn into and Insert. While it is open
// it takes the pointer and the keys.

enum MenuCommand {
    CMD_CUT = 1, CMD_COPY, CMD_PASTE, CMD_SELECT_ALL,
    CMD_OPEN_LINK, CMD_COPY_LINK, CMD_EDIT_LINK, CMD_REMOVE_LINK, CMD_REPLACE_IMAGE,
    CMD_TOGGLE_TASK, CMD_INDENT, CMD_OUTDENT, CMD_COPY_CODE,
    CMD_TABLE,                  // a table of the grid's size
    CMD_FORMAT = 100,           // + Format
    CMD_INSERT = 200,           // + Insert
    CMD_TURN = 300,             // + Block
    CMD_TABLE_EDIT = 400,       // + TableEdit
};
enum MenuSub { SUB_MENU, SUB_TURN, SUB_INSERT, SUB_TABLE, SUB_CALLOUT, SUB_DIAGRAM, SUB_ALIGN };
enum class MenuResult { None, Kept, Closed, Ran };  // None: it was not open; Ran: the text may have changed

// Opens it at a client point for the text at `at` (a click outside the
// selection should have put the caret there first). It goes below `bottom`,
// or ends at `top` when there is no room, inside `bounds`. Opened by the
// keys, its first row is lit
void openMenu(App& app, LiveText& e, float x, float top, float bottom, size_t at, const D2D1_RECT_F& bounds,
              const Host& host, bool byKeys);
// One of its submenus alone (the rail's table grid and diagrams), at a point
void openMenuPanel(App& app, LiveText& e, float x, float y, int sub, const D2D1_RECT_F& bounds, const Host& host);
void closeMenu(LiveText& e);
void drawMenu(App& app, LiveText& e);
// The pointer moved to a client point: true when what it lights changed
bool menuHover(App& app, LiveText& e, float x, float y);
// A press anywhere while it is open: a row runs, outside it closes
MenuResult menuPress(App& app, HWND hwnd, LiveText& e, float x, float y, const Host& host);
// A key while it is open: it takes them all
MenuResult menuKey(App& app, HWND hwnd, LiveText& e, WPARAM key, const Host& host);
// Runs one of its commands at the text it was opened on
void menuCommand(App& app, HWND hwnd, LiveText& e, int command, const Host& host);

}  // namespace live
