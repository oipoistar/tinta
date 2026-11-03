#pragma once

#include "app.h"

SidePanel sidePanelResizeAt(const App& app, float x, float y);
float sidePanelResizeEdge(const App& app, SidePanel panel);
bool sidePanelResizeBegin(App& app, HWND hwnd, float x, float y);
void sidePanelResizeMove(App& app, float x);
void sidePanelResizeEnd(App& app, HWND hwnd, bool cancel);
void renderSidePanelResizeGrip(App& app, SidePanel panel);
bool documentScrollbarEdgeHovered(const App& app);
float sidePanelDocumentScrollbarOpacity(App& app, ULONGLONG now);
bool sidePanelScrollbarNeedsTicks(const App& app, ULONGLONG now);

// A scrollbar as drawn, in window coordinates along its axis (#287).
// Rendering, presses on the track and thumb drags all read it, so the
// thumb stays under the pointer.
struct ScrollbarGeometry {
    float trackStart = 0, trackLength = 0;
    float thumbStart = 0, thumbLength = 0;
    float maxScroll = 0;
    // The scroll that puts the thumb's leading edge at `thumb`
    float scrollForThumb(float thumb) const;
};
// The document's bars: the vertical one runs from below the tab strip
// down to the horizontal one, which runs along the viewport's bottom
ScrollbarGeometry documentScrollbarV(const App& app);
ScrollbarGeometry documentScrollbarH(const App& app);
// The editor's bar at its pane's right edge, on a track like the
// document's: from below the tab strip to the window's foot (#291)
ScrollbarGeometry editorScrollbarV(const App& app);
// The editor's bar along its foot, under the text column, while unwrapped
// lines run past the pane (#314); a maxScroll of 0 when they do not
ScrollbarGeometry editorScrollbarH(const App& app);

// A side panel's list and the thin bar at the card's right edge, once the
// list outgrows its room. The bar drags and takes track clicks like the
// document's (#287).
struct PanelListView {
    D2D1_RECT_F list{};  // the list's viewport
    float cardRight = 0;
    float barRight = 0;  // the thumb's right edge
    float contentHeight = 0;
    float scroll = 0;
};
bool panelListView(const App& app, PanelList which, PanelListView& out);
ScrollbarGeometry panelListScrollbar(const App& app, const PanelListView& view);
// The list whose bar takes a press at this point; a panel edge that
// resizes keeps its own band
PanelList panelScrollbarAt(const App& app, float x, float y);
// Drawn stronger under the pointer and while dragged
bool panelScrollbarHot(const App& app, PanelList which);
void setPanelListScroll(App& app, PanelList which, float scroll);
bool panelScrollbarBegin(App& app, HWND hwnd, float x, float y);
bool panelScrollbarMove(App& app, float y);
// A cancelled drag (capture lost, focus gone) also swallows the release
void panelScrollbarEnd(App& app, HWND hwnd, bool cancel);
// Draws the thumb when the list overflows
void renderPanelScrollbar(App& app, PanelList which, float alpha);
