// Live (docs/live-editor.md): the document's page, typed in where it reads.
// The text stays app.editorText, the source's own buffer and undo; the page
// is the reading view's layout of it, and the blocks the caret and the
// selection are in are drawn by the live text engine (live_text.h) as
// Markdown being typed: the caret's line as typed, the others as they read.
#pragma once

#include "app.h"

struct LiveText;

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
// A finger held on the page (touch.cpp) takes its word, as a double-click
// does, without the mouse's capture; its moves grow the selection by words
// (liveMouseMove) until it lifts (liveMouseUp). False off the page and on
// the format bar or a popup, which take the hold as a press
bool liveTouchHold(App& app, HWND hwnd, int x, int y);
// The menu on the page (live_text.h): a right-click opens it at the click,
// with the caret moved there unless the click is in the selection; the
// menu key and Shift+F10 open it under the caret
void liveContextMenu(App& app, HWND hwnd, int x, int y, bool byKeys);
bool liveMenuOpen(const App& app);
// While it is open it takes every press, move and key: true when it did
bool liveMenuMouseDown(App& app, HWND hwnd, int x, int y);
bool liveMenuMouseMove(App& app, HWND hwnd, int x, int y);
bool liveMenuKey(App& app, HWND hwnd, WPARAM key);
// Put away by the wheel, the window losing focus or being resized
void liveCloseMenu(App& app);
// Drawn over the page and the rail, last
void renderLiveMenu(App& app);
// The page's text engine, as the tests read it
const LiveText& liveText(const App& app);
// A rail button on the Live text; false for the ones that insert through the source
bool liveRailCommand(App& app, HWND hwnd, int id);
// The caret on the screen, for the IME's windows
bool liveCaretClientPoint(App& app, D2D1_POINT_2F& top, float& height);
// Where the live stretch is on the page as shown: its top and bottom, and
// how far the page below it moved
bool livePageGeometry(const App& app, float& top, float& bottom, float& shift);
// editorReparse took the text the page is about to be laid out from
void liveTextParsed(App& app);
