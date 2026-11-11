// The interface's scale: the spacings, sizes and type roles its surfaces
// are built from, so that the same job looks the same everywhere. Lengths
// are DIPs and go through dpi(app, ...). A surface moved onto the scale
// uses no length of its own for a gap, a row or a corner.
#pragma once

#include "app.h"

namespace uiscale {

// Spacing, on a base of 4
constexpr float sp2 = 2.0f;    // optical nudges only
constexpr float sp4 = 4.0f;    // inside chips, icon to badge
constexpr float sp8 = 8.0f;    // icon to label, between related controls
constexpr float sp12 = 12.0f;  // control and panel padding
constexpr float sp16 = 16.0f;  // card padding, gutters
constexpr float sp24 = 24.0f;  // dialog padding, between sections
constexpr float sp32 = 32.0f;  // between groups
constexpr float sp48 = 48.0f;  // page-level margins

// Rows: a line of a panel's list, of a menu, and a row with a control
constexpr float rowList = 28.0f;
constexpr float rowMenu = 32.0f;
constexpr float rowControl = 40.0f;
// A row with a label and a line of description under it:
// sp8 + Body's line + sp2 + Small's line + sp8
constexpr float rowTwoLine = 52.0f;
// A control in a row: a button, a dropdown, a segmented switch
constexpr float control = 24.0f;
constexpr float controlBox = 28.0f;

// Corners: controls up to controlBox high; cards, menus and popups; dialogs
constexpr float radiusControl = 4.0f;
constexpr float radiusCard = 8.0f;
constexpr float radiusDialog = 12.0f;

// Ink, as a share of the interface's text colour: what is read, what
// explains it (4.5 to 1 or more on a card in every built-in theme), and
// what is not available
constexpr float inkPrimary = 0.95f;
constexpr float inkSecondary = 0.72f;
constexpr float inkDisabled = 0.4f;

// Type: Segoe UI in two weights. Nothing sentence-like is set in Micro,
// and one level of hierarchy is one step in size or one in weight.
enum class Type {
    Micro,     // 11/14 semibold: upper-case section labels, badges, key hints
    Small,     // 12/16: descriptions, footers, snippets
    Body,      // 13/18: rows, menu items, buttons, labels
    Strong,    // 13/18 semibold: panel titles, the selected row
    Subtitle,  // 16/22 semibold: group headings, the search input
    Title,     // 20/28 semibold: dialog titles
};
float fontSize(Type role);
float lineHeight(Type role);
bool semibold(Type role);
// The role's format at the window's DPI scale, on one line and trimmed with
// an ellipsis; `wrap` for text that may take several lines
IDWriteTextFormat* font(App& app, Type role, bool wrap = false);

}  // namespace uiscale
