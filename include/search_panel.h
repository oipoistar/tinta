#ifndef TINTA_SEARCH_PANEL_H
#define TINTA_SEARCH_PANEL_H

#include "app.h"

#include <vector>

// Search results panel (#246): Ctrl+Shift+F opens the search bar with
// every match of its query listed in the Contents slot. The document's
// matches line up under the headings they fall in; other open tabs and the
// files of the document's folder follow with their first matches by line.
// The panel lives no longer than the search bar, and only in the reader.

void openSearchPanel(App& app);    // Ctrl+Shift+F, in the reader
void toggleSearchPanel(App& app);  // the search bar's results button
void closeSearchPanel(App& app);   // Contents comes back if it was up

enum SearchPanelRowKind {
    SPR_DOCUMENT,    // the open document: title and match count
    SPR_SECTION,     // a heading the following matches fall under
    SPR_MATCH,       // one match in the open document
    SPR_CAPTION,     // "Open tabs" / "This folder" ahead of their files
    SPR_FILE,        // another file: name and match count
    SPR_FILE_MATCH,  // one of its first matches, with the line
    SPR_MORE,        // "+N more" in that file
    SPR_NOTE,        // nothing found yet, or still searching files
};

struct SearchPanelRow {
    SearchPanelRowKind kind = SPR_NOTE;
    int index = -1;  // heading (section), match (document or file)
    int file = -1;   // App::folderResults index
    const wchar_t* note = nullptr;
    float top = 0.0f;     // list coordinates
    float height = 0.0f;
};

// The list as drawn, shared by rendering, hit-testing and the tests
std::vector<SearchPanelRow> searchPanelRows(const App& app);
D2D1_RECT_F searchPanelListRect(const App& app);

// Pointer input; each returns true when the panel or the bar's results
// button took it
bool searchPanelMouseDown(App& app, HWND hwnd, float x, float y);
bool searchPanelWheel(App& app, float x, float y, float delta);
bool searchPanelSetCursor(App& app, float x, float y);

// Select a document match and scroll the page to it
void searchPanelJumpTo(App& app, int matchIndex);
// Open another file's tab at one of its listed matches
void searchPanelOpenFile(App& app, HWND hwnd, int file, int match);
// After a file opens: select the first match from that source line on
void searchPanelLandOnLine(App& app, int line);

void renderSearchPanel(App& app);

#endif  // TINTA_SEARCH_PANEL_H
