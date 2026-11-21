#ifndef TINTA_AUTOSCROLL_H
#define TINTA_AUTOSCROLL_H

#include "app.h"

// Middle-click autoscroll (#331), as in browsers. A middle click on the
// page or on the editor's text leaves a mark there, and the view scrolls
// towards the pointer: faster the further the pointer is from the mark,
// and not at all while it rests on it. A page wider than its view goes
// sideways the same way. Any click, any key or the wheel ends it, and so
// does the window losing the keyboard. With the button kept down instead,
// the view scrolls while the pointer is dragged and stops where the
// button is let go. Suggested by @fengctm.
//
// A sheet keeps its own middle-button pan and the tab strip its
// middle-click close. `middleClickScroll=0` in settings.ini turns it off.

bool autoScrollActive(const App& app);

// A middle press at a point in client pixels, at a time in milliseconds
// (touchClockMs); true when the view under it scrolls and autoscroll is on
bool autoScrollStart(App& app, HWND hwnd, POINT pt, double ms);
// Where the pointer is now
void autoScrollPointer(App& app, HWND hwnd, POINT pt);
// The middle button that began it came up: after a drag that ends it
void autoScrollRelease(App& app, HWND hwnd);
// TIMER_AUTO_SCROLL: the view moves by what its speed makes of the time
// since the last step; false once autoscroll is over
bool autoScrollStep(App& app, HWND hwnd, double ms);
void autoScrollStop(App& app, HWND hwnd);

// Pixels per millisecond for a pointer `offset` pixels from the mark along
// one axis, signed as the offset; 0 within the mark
float autoScrollSpeed(const App& app, float offset);
// The mark's radius, which is also how far the pointer can rest from it
float autoScrollRest(const App& app);

// A message for the window while autoscroll is on, its timer, and the
// release of the click that ended it; true when the message is taken here
bool autoScrollMessage(App& app, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// The mark, over the page and under the window's chrome
void renderAutoScrollMark(App& app);

#endif  // TINTA_AUTOSCROLL_H
