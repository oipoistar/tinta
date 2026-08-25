#pragma once

#include "app.h"

// Left tool rail (design t8/t11): slides in with edit mode over the old
// gutter column, carrying the formatting controls. Line numbers live in
// the editor's own slim gutter; the thread seam between the panes ties
// source blocks to their render.
//
// Hit ids: 1 bold, 2 italic, 3 strike, 4 inline code, 5 link;
// 10 bullet list, 11 task list, 12 quote; 20 table, 21 diagram, 22 image.

void renderEditRail(App& app);
int editRailHitAt(const App& app, float x, float y);

// Mouse glue for the editor handlers; down/move return true when the
// event belonged to the rail
bool editRailMouseDown(App& app, HWND hwnd, int x, int y);
bool editRailMouseMove(App& app, HWND hwnd, int x, int y);

// Rail button actions (defined in editor.cpp beside the edit helpers)
void editRailInvoke(App& app, HWND hwnd, int id);

// Floating render sheet (design 10a): desk + shadow + sheet surface,
// drawn before the document clips into the sheet; the caret block wash
// draws inside the preview transform before the document content
void renderEditSheetChrome(App& app);
void renderPreviewCaretBlock(App& app, float previewWidth);

// Raw editor insert menu (design t9)
void openEditCtxMenu(App& app, HWND hwnd, float x, float y);
// The rail's table / diagram buttons open the same submenu standalone:
// sub 1 = table size grid, 2 = diagram templates
void openEditRailFlyout(App& app, HWND hwnd, int sub);
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
