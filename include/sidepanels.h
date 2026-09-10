#pragma once

#include "app.h"

SidePanel sidePanelResizeAt(const App& app, float x, float y);
float sidePanelResizeEdge(const App& app, SidePanel panel);
bool sidePanelResizeBegin(App& app, HWND hwnd, float x, float y);
void sidePanelResizeMove(App& app, float x);
void sidePanelResizeEnd(App& app, HWND hwnd, bool cancel);
void renderSidePanelResizeGrip(App& app, SidePanel panel);
bool documentScrollbarEdgeHovered(const App& app);
float sidePanelDocumentScrollbarOpacity(const App& app, ULONGLONG now);
