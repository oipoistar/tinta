#ifndef TINTA_TABS_H
#define TINTA_TABS_H

#include "app.h"
#include <windows.h>

// Tabbed interface: Win11 Notepad-style tabs living in the custom title
// bar. Single-file windows stay tabless (the strip shows the title text);
// the tab row appears when a second document opens.

// Model
void tabsInit(App& app);                       // adopt the startup document
// Rebuild the row from last session's paths (startup doc becomes active)
void tabsSeedSession(App& app, const std::vector<std::string>& paths);
void tabActivate(App& app, HWND hwnd, int index);
void tabOpenPath(App& app, HWND hwnd, const std::string& utf8Path,
                 bool activate = true, int insertBefore = -1);
// Cross-window drops use client-space tab midpoints; ordinary opens append.
int tabDropInsertionIndex(const App& app, POINT clientPoint);
bool tabReceiveCopyData(App& app, HWND hwnd, const COPYDATASTRUCT& data);
bool tabSendDrop(HWND target, const std::string& utf8Path, POINT screenPoint);
void tabCloseIndex(App& app, HWND hwnd, int index);
void tabCycle(App& app, HWND hwnd, int direction);

// Title-bar geometry shared by rendering and non-client hit testing
D2D1_RECT_F captionButtonRect(const App& app, int button);  // 0 min 1 max 2 close
float captionIslandLeft(const App& app);
D2D1_RECT_F titleDragRect(const App& app); // reserved empty space beside tab controls
int captionHitTest(const App& app, float x, float y);       // 0 none, 1..3
// Strip surface color derived from the theme (shared with the DWM frame
// tint so border and transition flashes match the drawn caption)
D2D1_COLOR_F tabStripBackground(const App& app);
// + button: a new untitled quick-note tab, straight into the editor
void tabOpenQuickNote(App& app, HWND hwnd);
void tabOpenStartPage(App& app, HWND hwnd);
void tabBecomeStartPage(App& app, HWND hwnd);

// Rendering (main_d2d render loop)
void renderTabStrip(App& app);
void renderTabSwitcher(App& app);

// Tab row vs tabless title text (drag-out satellites force the row)
bool tabStripVisible(const App& app);

// Hover card (#246): a pointer resting on a tab shows its whole name, its
// folder and what its dot means on a card below the strip
struct TabHoverCardText {
    std::wstring name;
    std::wstring folder;  // empty for a tab without a file
    std::wstring status;  // unsaved changes / gone from disk, else empty
    bool dirty = false;   // status colour follows the strip's dot
};
TabHoverCardText tabHoverCardText(const App& app, int tab);
void tabHoverCardTrack(App& app, HWND hwnd, int tab);  // pointer's tab, -1 none
void tabHoverCardDismiss(App& app, HWND hwnd);         // click, key or wheel
void tabHoverCardLeave(App& app, HWND hwnd);           // pointer left the client
void handleTabHoverCardTimer(App& app, HWND hwnd);
void renderTabHoverCard(App& app);

// Right-click tab context menu (NPP-style close operations)
int tabContextMenuIndexAt(const App& app, float x, float y);
void openTabMenu(App& app, int tabIndex, float x, float y);
void closeTabMenu(App& app);
int tabMenuItemAt(const App& app, float x, float y);
void renderTabMenu(App& app);
bool tabMenuMouseDown(App& app, HWND hwnd, int x, int y);
// Continue a Close-others/left/right sweep after a dirty tab's dialog
void tabBulkCloseStep(App& app, HWND hwnd);

// Mouse handling for the strip area; return true when consumed
bool tabStripMouseDown(App& app, HWND hwnd, int x, int y, bool middle);
bool tabSwitcherMouseDown(App& app, HWND hwnd, int x, int y);
// Drag lifecycle: move reorders inside the strip or floats the tab as a
// ghost card once clear of it; release decides (drop into another Tinta
// window, spawn a new one, or snap back); Esc/capture loss cancels
void tabDragMove(App& app, HWND hwnd, int x, int y);
void tabDragEnd(App& app, HWND hwnd, int x, int y);
void tabDragCancel(App& app, HWND hwnd);
// After a native window move: a tabless window dropped onto another
// Tinta window's strip merges its document there
void tabWindowDropMerge(App& app, HWND hwnd);

#endif  // TINTA_TABS_H
