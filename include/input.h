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
void handleCharInput(App& app, HWND hwnd, WPARAM wParam);
void handleDropFiles(App& app, HWND hwnd, WPARAM wParam);
void handleFileWatchTimer(App& app, HWND hwnd);

#endif // TINTA_INPUT_H
