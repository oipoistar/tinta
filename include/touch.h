#ifndef TINTA_TOUCH_H
#define TINTA_TOUCH_H

#include "app.h"

// Touch (#286). A finger pans what it lands on - the page, the editor's
// text, a side panel's list - and a flick carries on, slowing to a stop.
// A tap clicks; a second tap selects the word, as a double-click does. A
// press held still selects the word and extends it word by word as the
// finger moves; where it lifts, the context menu opens. Touches on the tab
// strip, menus, dialogs, overlays and scrollbars keep Windows' own mouse
// emulation, and the mouse and the pen are unchanged.

// WM_POINTERDOWN, UPDATE, UP and CAPTURECHANGED: true when the touch is
// taken here, and DefWindowProc must not turn it into mouse input
bool handlePointerMessage(App& app, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// A finger in client pixels at a time in milliseconds; each returns true
// when the touch is taken here. The window procedure calls these from
// pointer messages, the tests directly.
bool touchDown(App& app, HWND hwnd, UINT32 pointer, POINT pt, double ms);
bool touchMove(App& app, HWND hwnd, UINT32 pointer, POINT pt, double ms);
bool touchUp(App& app, HWND hwnd, UINT32 pointer, POINT pt, double ms, bool canceled);
// TIMER_TOUCH_HOLD: the press has been held still
void touchHold(App& app, HWND hwnd);
// TIMER_TOUCH_FLING: one step of a fling; false once it has stopped
bool touchFlingStep(App& app, HWND hwnd, double ms);
// A click, the wheel, a key or another document stops a fling
void touchStopFling(App& app, HWND hwnd);
// Lost pointer capture: the gesture ends without acting
void touchCancel(App& app, HWND hwnd);

// What a finger landing at this point would drive
TouchTarget touchTargetAt(const App& app, POINT pt, PanelList& list);
// Where a target is scrolled to, how far each way it can go, and scrolling
// it to a place kept within that (written back); true when it moved.
// Middle-click autoscroll steers a view through these too (#331)
void touchScrollPosition(const App& app, TouchTarget target, PanelList list, float& x, float& y);
void touchScrollRange(const App& app, TouchTarget target, PanelList list, float& maxX, float& maxY);
bool touchScrollTo(App& app, TouchTarget target, PanelList list, float& x, float& y);
// The clock pointer messages and the fling run on, in milliseconds
double touchClockMs();

#endif  // TINTA_TOUCH_H
