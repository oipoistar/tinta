#ifndef TINTA_OVERLAYS_H
#define TINTA_OVERLAYS_H

#include "app.h"

void renderSearchOverlay(App& app);
void renderFolderBrowser(App& app);
// Folder-browser item under a client point (-1 = none); shares the
// renderer's geometry so click handling never trusts stale hover state
int folderItemIndexAt(const App& app, float x, float y);
void renderToc(App& app);
void renderThemeChooser(App& app);
// Chooser grid geometry, shared between render and hit-testing: rows in the
// taller column (never below the built-in 5), and a theme's cell position
// (light themes fill the left column top-down, dark the right)
int themeChooserRows();
void themeChooserCell(int themeIndex, int& col, int& row);
void renderHelpOverlay(App& app);
// Full-frame print preview; render() short-circuits to this while it is open
void renderPrintPreview(App& app);

// Settings overlay (Ctrl+,). Render stores (rect, action) pairs in
// app.settingsHits; handleMouseUp resolves them against these ids.
enum SettingsAction {
    SET_NONE = 0,
    SET_SECTION_GENERAL, SET_SECTION_APPEARANCE, SET_SECTION_EDITOR,
    SET_TOGGLE_FOLLOW, SET_TOGGLE_FOLDERSEARCH, SET_TOGGLE_BROWSEFOCUS,
    SET_TOGGLE_OPENTABS,
    SET_TOGGLE_WRAP, SET_TOGGLE_PREVIEW, SET_TOGGLE_ASSISTS,
    SET_SLIDER_READING, SET_SLIDER_ZEN,
    SET_OPEN_THEMES, SET_NEW_THEME, SET_EDIT_THEME,
    SET_OPEN_INI, SET_OPEN_THEMES_INI,
    SET_TOC_LEFT, SET_TOC_RIGHT,
    SET_LANG_DROPDOWN, SET_OPEN_LANGS_INI,
    SET_KEYS_DROPDOWN, SET_EDIT_KEYS,
    // Language picks encode as SET_LANG_PICK_BASE + i: 0 = Auto, then the
    // registry languages in order (the list is dynamic via languages.ini)
    SET_LANG_PICK_BASE = 1000,
    // Shortcut profile picks: SET_KEYS_PICK_BASE + profile index, with
    // KEY_PROFILE_COUNT meaning the custom [Keys] profile
    SET_KEYS_PICK_BASE = 2000,
};
// TOC panel X for the configured side at the current animation state;
// geometry shared between render and the input hit tests
float tocPanelX(const App& app, float panelWidth);
void renderSettingsOverlay(App& app);

// Theme editor ("+ New" in settings). Font-list entries encode their
// absolute family index as TE_FONT_BASE + i; everything else stays < 100.
enum ThemeEditorAction {
    TE_NONE = 0,
    TE_FIELD_BG, TE_FIELD_TEXT, TE_FIELD_HEADING, TE_FIELD_LINK,
    TE_FIELD_ACCENT, TE_FIELD_CODEBG,   // order matches themeEditorHex[0..5]
    TE_FIELD_NAME,
    TE_BASE_PREV, TE_BASE_NEXT,
    TE_DARK, TE_SAVE, TE_CANCEL, TE_OPEN_INI,
    TE_FONT_BASE = 100,
};
void renderThemeEditor(App& app);
void renderShortcutEditor(App& app);
void renderConfirmExitDialog(App& app);
void renderCreateRefDialog(App& app);

// Right-click context menu: theme-drawn like the other overlays.
// Item indices are shared between rendering and input handling.
enum ContextMenuItem {
    CTX_COPY = 0,
    CTX_SELECT_ALL,
    CTX_ANNOTATE,
    CTX_NEW,
    CTX_PRINT,
    CTX_EXPORT,
    CTX_EDIT,
    CTX_SEARCH,
    CTX_TOC,
    CTX_BROWSE,
    CTX_REVEAL,
    CTX_THEME,
    CTX_SETTINGS,
    CTX_HELP,
    CTX_ITEM_COUNT
};
void renderContextMenu(App& app);

// Hovering a local .md link for a beat previews the target's first lines
void renderLinkPeek(App& app);

// Image lightbox (click an inline image)
void openLightbox(App& app, ID2D1Bitmap* bitmap);
void closeLightbox(App& app);
void renderLightbox(App& app);
// Where the image currently draws, in screen coordinates
D2D1_RECT_F lightboxImageRect(const App& app);

// Folder-wide search results beside the search bar; the toggle button sits
// at the bar's right edge (geometry shared with input hit-testing)
void renderFolderSearchResults(App& app);
bool folderSearchToggleAt(const App& app, float x, float y);
// Opens at (x, y) client coordinates, clamped so the menu stays on screen
void openContextMenu(App& app, float x, float y);
// Item index under the point, or -1 (separators and gaps count as none)
int contextMenuItemAt(const App& app, float x, float y);
bool contextMenuItemEnabled(const App& app, int item);

#endif // TINTA_OVERLAYS_H
