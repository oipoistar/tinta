#ifndef TINTA_SEARCH_H
#define TINTA_SEARCH_H

#include "app.h"

void performSearch(App& app);
void mapSearchMatchesToLayout(App& app);
void scrollToCurrentMatch(App& app);

// Folder-wide search: scans sibling .md files for the current query on a
// worker thread; results arrive via WM_APP_FOLDER_SEARCH
void startFolderSearchScan(App& app);
void completeFolderSearch(App& app, void* results);
void clearFolderSearch(App& app);

#endif // TINTA_SEARCH_H
