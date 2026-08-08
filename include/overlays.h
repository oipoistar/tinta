#ifndef TINTA_OVERLAYS_H
#define TINTA_OVERLAYS_H

#include "app.h"

void renderSearchOverlay(App& app);
void renderFolderBrowser(App& app);
void renderToc(App& app);
void renderThemeChooser(App& app);
void renderHelpOverlay(App& app);

// Right-click context menu: theme-drawn like the other overlays.
// Item indices are shared between rendering and input handling.
enum ContextMenuItem {
    CTX_COPY = 0,
    CTX_SELECT_ALL,
    CTX_EDIT,
    CTX_SEARCH,
    CTX_TOC,
    CTX_BROWSE,
    CTX_REVEAL,
    CTX_THEME,
    CTX_HELP,
    CTX_ITEM_COUNT
};
void renderContextMenu(App& app);
// Opens at (x, y) client coordinates, clamped so the menu stays on screen
void openContextMenu(App& app, float x, float y);
// Item index under the point, or -1 (separators and gaps count as none)
int contextMenuItemAt(const App& app, float x, float y);
bool contextMenuItemEnabled(const App& app, int item);

#endif // TINTA_OVERLAYS_H
