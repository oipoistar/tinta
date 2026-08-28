#ifndef TINTA_TABLEEDIT_H
#define TINTA_TABLEEDIT_H

#include "app.h"

// In-place table editing in the rendered preview (#148, edit mode).
// Clicking a cell opens an inline input over it; typing edits the raw
// markdown through the editor's undo stack. Tab commits and moves to
// the next cell (adding a row past the last one), Enter commits, Esc
// cancels. Hovering a table offers + row and + column affordances.

// Mouse press in the preview pane (document coords already resolved by
// the caller). Returns true when the press was consumed.
bool tableEditMouseDown(App& app, HWND hwnd, float docX, float docY);
// Keyboard while a cell editor is open; true = consumed
bool tableEditKeyDown(App& app, HWND hwnd, WPARAM key);
// Printable character while a cell editor is open; true = consumed
bool tableEditChar(App& app, wchar_t ch);
// Commit any open cell editor (mode exits, saves, clicks elsewhere)
void tableEditCommit(App& app);
// Drop the editor without applying (document switch, exit edit mode)
void tableEditCancel(App& app);
// Cell input overlay plus the hover + affordances; screen coordinates
// (call after the edit-mode clip pops)
void renderTableEditOverlay(App& app);

#endif  // TINTA_TABLEEDIT_H
