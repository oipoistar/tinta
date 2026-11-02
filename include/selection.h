#ifndef TINTA_SELECTION_H
#define TINTA_SELECTION_H

#include "app.h"

#include <vector>

// Offset-based text selection (#83). The selection is a pair of character
// positions into app.docText (selAnchor / selFocus); highlight rectangles
// and the copied text both derive from that range, so what is highlighted,
// what was aimed at, and what lands on the clipboard always agree. Caret
// positions snap to glyph clusters via each run's IDWriteTextLayout
// (App::LayoutTextRun, reachable from a TextRect through runIndex).

// Document-space point -> caret offset. Points that miss text vertically
// resolve against the nearest line; horizontally they snap to the nearest
// run's edge, so a drag can start on padding or margins.
size_t selectionOffsetAtPoint(const App& app, float docX, float docY);

// Word around an offset (double-click). Boundaries follow isWordBoundary
// over docText; a boundary character selects just itself.
void selectionWordRange(const App& app, size_t offset, size_t& start, size_t& end);

// Visual line (layout line bucket) containing an offset (triple-click).
bool selectionLineRange(const App& app, size_t offset, size_t& start, size_t& end);

// Exact text-run highlight rectangles for [start, end), document coordinates.
// Each covers the selected glyph range of a stretch of line, the spaces
// between its runs included; table-cell padding and gaps between cells are
// not.
void selectionHighlightRects(const App& app, size_t start, size_t end,
                             std::vector<D2D1_RECT_F>& out);

// One of those rectangles on screen at the given scroll. Its top and bottom
// fall on whole pixels, so the lines of a selection meet without an
// antialiased seam between their bands.
D2D1_RECT_F selectionBarOnScreen(const D2D1_RECT_F& bar, float scrollX, float scrollY);

// Precise rect for the docText range [start, end) clipped to one text rect
// (search match highlighting shares the selection machinery). Vertical
// extent comes from the rect, horizontal from the run layout; interpolates
// when no layout covers the rect.
bool selectionRangeRect(const App& app, const App::TextRect& tr,
                        size_t start, size_t end, D2D1_RECT_F& out);

// Exactly docText[start, end) — docText carries newlines between layout
// lines and blocks, so this pastes as sensible plain text.
std::wstring selectionTextForRange(const App& app, size_t start, size_t end);

// The same range as an HTML fragment: the plain text's lines (a <div> each)
// and spacing, with bold in <b>, italics in <i>, web and mail links in
// <a href> and list items in <ul>/<ol>, so a paste into Outlook or Word
// keeps them.
std::wstring selectionHtmlForRange(const App& app, size_t start, size_t end);

// Copies [start, end) as plain text plus its HTML; false when empty, or
// when the clipboard stayed held (then held is set).
bool copySelectionRange(const App& app, HWND hwnd, size_t start, size_t end,
                        bool* held = nullptr);

void clearSelection(App& app);

#endif // TINTA_SELECTION_H
