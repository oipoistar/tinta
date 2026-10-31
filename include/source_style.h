// Markdown in the source editor, in colour (#268; docs/syntax-highlighting.md):
// the marks muted; headings, links, code and the marks that start a block in
// the page's colours; fenced code and the properties in their language's.
// Each line is coloured once, when the editor lays it out to draw it.
#pragma once

#include "app.h"

#include <dwrite.h>

// Before each frame of the source editor: a new theme, font, render target
// or setting colours every line it lays out afresh
void sourceHighlightFrame(App& app);
// A line's colours, and bold and italic where the editor's font keeps its
// widths with them, on the layout of the line starting at `lineStart`;
// nothing while the setting is off
void sourceHighlightLine(App& app, IDWriteTextLayout* layout, size_t lineStart, size_t lineLength);
