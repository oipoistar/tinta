#ifndef TINTA_EDITOR_H
#define TINTA_EDITOR_H

#include "app.h"
#include <windows.h>

// Mode transitions
void enterEditMode(App& app);
void exitEditMode(App& app);

// Editor input handlers
void handleEditorKeyDown(App& app, HWND hwnd, WPARAM wParam);
void handleEditorCharInput(App& app, HWND hwnd, WPARAM wParam);
void handleEditorMouseDown(App& app, HWND hwnd, int x, int y);
void handleEditorMouseUp(App& app, HWND hwnd, int x, int y);
void handleEditorMouseMove(App& app, HWND hwnd, int x, int y);
void handleEditorMouseWheel(App& app, HWND hwnd, float delta);

// Editor rendering
void renderEditor(App& app, float editorWidth);
void renderSeparator(App& app);
void renderEditModeNotification(App& app);

// File save
void saveEditorFile(App& app, HWND hwnd);

// Editor reparse (called from timer)
void editorReparse(App& app);

// Editor search
void performEditorSearch(App& app);
void scrollEditorToMatch(App& app);

// Utility
void rebuildLineStarts(App& app);
std::string toUtf8(const std::wstring& wstr);

#endif // TINTA_EDITOR_H
