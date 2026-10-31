// Live (docs/live-editor.md): the document's page, typed in where it reads.
// The text stays app.editorText, the source's own buffer and undo; the page
// is the reading view's layout of it, and the blocks the caret and the
// selection are in are drawn by the live text engine (live_text.h) as
// Markdown being typed: the caret's line as typed, the others as they read.
#pragma once

#include "app.h"

// Switches Source and Live (the rail's pill, Ctrl+/); the caret stays on
// its character and at its height in the window
void setEditorLive(App& app, HWND hwnd, bool live);
// The page, drawn in place of the document pass (in its transform)
void renderLivePage(App& app);
// The page's height with its blocks drawn live
float livePageHeight(const App& app);
// Input while Live shows; false leaves it to the editor. A key the page
// takes is consumed (no character follows it) unless it types one
bool liveKeyDown(App& app, HWND hwnd, WPARAM key, bool& consumed);
bool liveAltArrow(App& app, HWND hwnd, WPARAM key);
void liveCharInput(App& app, HWND hwnd, wchar_t ch);
bool liveMouseDown(App& app, HWND hwnd, int x, int y);
bool liveMouseMove(App& app, HWND hwnd, int x, int y);
bool liveMouseUp(App& app, HWND hwnd, int x, int y);
// The mouse went elsewhere mid-drag (WM_CAPTURECHANGED, WM_CANCELMODE)
void liveCaptureLost(App& app);
// A right-click: the caret goes there unless it is in the selection
void livePrepareContextMenu(App& app, HWND hwnd, int x, int y);
// A rail button on the Live text; false for the ones that insert through the source
bool liveRailCommand(App& app, HWND hwnd, int id);
// The caret on the screen, for the IME's windows
bool liveCaretClientPoint(App& app, D2D1_POINT_2F& top, float& height);
// Where the live stretch is on the page as shown: its top and bottom, and
// how far the page below it moved
bool livePageGeometry(const App& app, float& top, float& bottom, float& shift);
// editorReparse took the text the page is about to be laid out from
void liveTextParsed(App& app);
