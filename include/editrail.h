#pragma once

#include "app.h"

// Left tool rail (design t8): slides in with edit mode over the old
// gutter column. WYSIWYG shows the formatting tools (text style, blocks,
// insert); raw mode shows the document map — the only line numbering.
// At the foot sits the Aa/M-down mode pill (Ctrl+E).
//
// Hit ids: 1 bold, 2 italic, 3 strike, 4 inline code, 5 link;
// 10 bullet list, 11 task list, 12 quote; 20 table, 21 diagram,
// 22 image; 30 pill WYSIWYG half, 31 pill raw half; 40 document map.

void renderEditRail(App& app);
int editRailHitAt(const App& app, float x, float y);

// Mouse glue for the editor handlers; down/move return true when the
// event belonged to the rail
bool editRailMouseDown(App& app, HWND hwnd, int x, int y);
bool editRailMouseMove(App& app, HWND hwnd, int x, int y);
void editRailMouseUp(App& app);

// Rail button actions (defined in editor.cpp beside the edit helpers)
void editRailInvoke(App& app, HWND hwnd, int id);
// The Ctrl+E mode switch, shared by the pill and the keyboard
void editorSetWysiwyg(App& app, HWND hwnd, bool wysiwyg);

// Raw editor insert menu (design t9)
void openEditCtxMenu(App& app, HWND hwnd, float x, float y);
void closeEditCtxMenu(App& app);
void renderEditCtxMenu(App& app);
bool editCtxMouseDown(App& app, HWND hwnd, int x, int y);
bool editCtxMouseMove(App& app, int x, int y);

// Editor helpers the menu leans on (defined in editor.cpp)
void editorMoveCaretToPoint(App& app, int x, int y);
void editorInsertTableGrid(App& app, HWND hwnd, int cols, int rows);
void editorInsertDiagramTemplate(App& app, HWND hwnd, int kind);
void editorClipboardCut(App& app, HWND hwnd);
void editorClipboardCopy(App& app, HWND hwnd);
void editorClipboardPaste(App& app, HWND hwnd);
void editorInsertSnippetPublic(App& app, HWND hwnd,
                               const std::wstring& snippet,
                               size_t caretOffset);
