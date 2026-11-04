#pragma once
// The CSS inspector (docs/specs/theming.md, phase 3). Ctrl+Alt+click an
// element of the page: a card shows its path from the page down
// (body > blockquote > p > strong), the rules that style it in cascade order
// with their files and lines, the ones they beat, and what it inherits.
// Clicking a rule opens its sheet at that line.
#include "app.h"

// Opens the card for what is under a point of the window; false when no CSS
// theme styles the page or nothing is there
bool inspectorOpen(App& app, float x, float y);
bool inspectorShown(const App& app);
void inspectorClose(App& app);
// While the card is open it takes a press: a rule opens its sheet, a press
// anywhere else puts the card away. True when the card took it
bool inspectorMouseDown(App& app, HWND hwnd, float x, float y);
// Esc puts the card away; true when it did
bool inspectorKeyDown(App& app, HWND hwnd, WPARAM key);
// Drawn over the page, in the document view's coordinates
void renderInspector(App& app);
// The card as text: its path, then a line a rule ("color: #333   p   Ink.css:4"),
// a beaten one marked with ~; empty when it is closed
std::wstring inspectorText(const App& app);
