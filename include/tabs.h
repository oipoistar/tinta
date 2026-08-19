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
                 bool activate = true);
void tabCloseIndex(App& app, HWND hwnd, int index);
void tabCycle(App& app, HWND hwnd, int direction);

// Title-bar geometry shared by rendering and non-client hit testing
D2D1_RECT_F captionButtonRect(const App& app, int button);  // 0 min 1 max 2 close
int captionHitTest(const App& app, float x, float y);       // 0 none, 1..3
// Strip surface color derived from the theme (shared with the DWM frame
// tint so border and transition flashes match the drawn caption)
D2D1_COLOR_F tabStripBackground(const App& app);
// + button: a new untitled quick-note tab, straight into the editor
void tabOpenQuickNote(App& app, HWND hwnd);

// Rendering (main_d2d render loop)
void renderTabStrip(App& app);
void renderTabSwitcher(App& app);

// Mouse handling for the strip area; return true when consumed
bool tabStripMouseDown(App& app, HWND hwnd, int x, int y, bool middle);
bool tabSwitcherMouseDown(App& app, HWND hwnd, int x, int y);

#endif  // TINTA_TABS_H
