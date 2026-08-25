#ifndef TINTA_EDITOR_H
#define TINTA_EDITOR_H

#include "app.h"
#include <windows.h>

// Mode transitions
void enterEditMode(App& app);
// Ctrl+N quick note: edit mode on an untitled empty buffer with no backing
// file; the first save routes through the classic Save As dialog
void enterQuickNoteMode(App& app);
void enterRecoveredDraft(App& app, HWND hwnd, const std::string& content,
                         const std::string& origPath);
void exitEditMode(App& app);

// Editor input handlers
void handleEditorKeyDown(App& app, HWND hwnd, WPARAM wParam);
void handleEditorCharInput(App& app, HWND hwnd, WPARAM wParam);
void editorPositionImeWindow(App& app, HWND hwnd);
void handleEditorMouseDown(App& app, HWND hwnd, int x, int y);
void handleEditorMouseUp(App& app, HWND hwnd, int x, int y);
void handleEditorMouseMove(App& app, HWND hwnd, int x, int y);
void handleEditorMouseWheel(App& app, HWND hwnd, float delta);

// Editor rendering
void renderEditor(App& app, float editorWidth);
void renderEditModeNotification(App& app);

// Re-enter edit mode from a parked tab buffer (tabs.cpp), restoring the
// dirty flag, scroll, and caret the switch-away captured
void restoreEditBuffer(App& app, const std::wstring& text, bool dirty,
                       float scrollY, size_t cursor);

// Quick-note empty state (design 4a): an untitled buffer with no text yet
// shows a centered "Open a file" button in the preview pane until typing
// starts; the button (or Ctrl+O) opens the classic file picker
bool quickNoteEmptyStateActive(const App& app);
void quickNoteOpenFile(App& app, HWND hwnd);
void renderQuickNoteEmptyState(App& app);

// File save; Save As (Ctrl+Shift+S) re-prompts for the path and moves the
// tab's identity to the chosen file
void saveEditorFile(App& app, HWND hwnd);
void saveEditorFileAs(App& app, HWND hwnd);
// Ctrl+Shift+S anywhere: editor writes the buffer to a new path, viewer
// copies the viewed file; the tab follows the new name either way
void saveFileAs(App& app, HWND hwnd);
// Unsaved-changes dialog outcome: 1 save+exit, 2 discard, 3 keep editing
void confirmExitAction(App& app, HWND hwnd, int action);

// Editor reparse (called from timer)
void editorReparse(App& app);

// Editor search
void performEditorSearch(App& app);
void scrollEditorToMatch(App& app);
// Find & replace (#121): replace the current match / every match with
// App::replaceText; each hunk is one undo entry
void editorReplaceCurrent(App& app, HWND hwnd);
void editorReplaceAll(App& app, HWND hwnd);

// Utility
void rebuildLineStarts(App& app);
size_t editorTopVisibleLine(App& app);
std::string toUtf8(const std::wstring& wstr);

#endif // TINTA_EDITOR_H
