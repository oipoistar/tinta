// Live text: Markdown typed with the page drawn around it (docs/live-editor.md).
// The editing engine Tinta's Live editor and sheet cards share: the text,
// its caret and selection, undo, the edits Markdown's rules call for, the
// keys and characters that make them, the text laid out live (the caret's
// line as typed, the others as they read), the slash menu and [[ names
// under the caret, and the format bar over a selection.
#pragma once

#include "app.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <string>
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
void toggleLineMark(LiveText& e, LineMark::Kind kind);
void toggleTask(LiveText& e, size_t lineBegin);
void moveLines(LiveText& e, bool up);          // the caret's lines past the one above or below
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
// caret lands where its content goes. An image's path comes from the host
void applyInsert(LiveText& e, int insert, const std::wstring& image = {});

// The format bar's buttons
enum Format { FMT_BOLD, FMT_ITALIC, FMT_STRIKE, FMT_CODE, FMT_MARK, FMT_LINK, FMT_H1, FMT_H2, FMT_H3, FMT_COUNT };
void applyFormat(LiveText& e, int format);

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

}  // namespace live
