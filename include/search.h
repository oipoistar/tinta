#ifndef TINTA_SEARCH_H
#define TINTA_SEARCH_H

#include "app.h"

void performSearch(App& app);
void mapSearchMatchesToLayout(App& app);
void scrollToCurrentMatch(App& app);

// Find/Replace share real single-line editing and exclusive typing focus (#223).
void openSearchInput(App& app, bool replace = false);
void focusSearchInput(App& app, bool replace);
void releaseSearchInput(App& app);
void closeSearchInput(App& app);
void focusSourceEditor(App& app);
bool sourceEditorHasFocus(const App& app);
bool searchInputKeyDown(App& app, HWND hwnd, WPARAM key);
void searchInputChar(App& app, HWND hwnd, wchar_t ch);
bool searchInputMouseDown(App& app, HWND hwnd, float x, float y);
bool searchInputMouseMove(App& app, float x, float y);
void searchInputMouseUp(App& app);
void renderSearchField(App& app, int field, const D2D1_RECT_F& textRect,
                       const D2D1_RECT_F& hitRect, const wchar_t* placeholder, float alpha);
bool searchInputCaretPoint(App& app, D2D1_POINT_2F& point);

// Folder-wide search: scans sibling .md files for the current query on a
// worker thread; results arrive via WM_APP_FOLDER_SEARCH
void startFolderSearchScan(App& app);
void completeFolderSearch(App& app, void* results);
void clearFolderSearch(App& app);

#endif // TINTA_SEARCH_H
