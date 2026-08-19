#ifndef TINTA_INPUT_H
#define TINTA_INPUT_H

#include "app.h"
#include <windows.h>

void handleMouseWheel(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam);
void handleMouseHWheel(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam);
void handleMouseMove(App& app, HWND hwnd, LPARAM lParam);
void handleMouseDown(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam);
void handleMouseUp(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam);
void handleKeyDown(App& app, HWND hwnd, WPARAM wParam);
void handleContextMenu(App& app, HWND hwnd, LPARAM lParam);
void handleCharInput(App& app, HWND hwnd, WPARAM wParam);
void handleDropFiles(App& app, HWND hwnd, WPARAM wParam);
// Load a document into the viewer, saving the outgoing reading position
// and restoring the incoming one (shared with tabs.cpp)
bool openDocumentInViewer(App& app, const std::wstring& fullPath);
void handleFileWatchTimer(App& app, HWND hwnd);
void handleSelectScrollTimer(App& app, HWND hwnd);

#endif // TINTA_INPUT_H
