#ifndef TINTA_SIGNALS_H
#define TINTA_SIGNALS_H

#include "app.h"

// Signal chips (design t13): one anatomy for every notification. Chips
// stack bottom-right, drain over a visible rail, hold while hovered,
// pin to stay, and park into the bell tray instead of vanishing.

enum SignalSeverity { SIG_INFO = 0, SIG_SUCCESS, SIG_WARN, SIG_ERROR };
enum SignalIcon {
    SIGI_INFO = 0,   // circle + i
    SIGI_CHECK,      // check mark
    SIGI_COPY,       // two sheets
    SIGI_WARNING,    // triangle + !
    SIGI_FILE,       // page with fold
    SIGI_UPDATE,     // arrow into tray
    SIGI_EYE,        // preview visibility
};
enum SignalAction {
    SIGA_NONE = 0,
    SIGA_OPEN_RELEASE,    // update chip: open the latest release page
    SIGA_RESTORE_DRAFTS,  // draft chip: recover crash leftovers as tabs
    SIGA_RETRY_SAVE,      // save-failed chip: try the save again
};
enum SignalClose {
    SIGC_NONE = 0,
    SIGC_DISMISS_UPDATE,  // remember the dismissed version
    SIGC_DISCARD_DRAFTS,  // delete the recovered drafts for good
};

// Push a chip. text renders regular, emph renders bold after it;
// context becomes the tray subtitle (a timestamp is appended when the
// chip parks). drains=false keeps the chip until answered or dismissed.
// anchor (document coords) renders a mini pill at the source instead of
// the stack - the code-block Copied case.
void signalPush(App& app, int severity, int icon, std::wstring text,
                std::wstring emph = L"", std::wstring context = L"",
                int action = SIGA_NONE, int closeAction = SIGC_NONE,
                bool drains = true, const D2D1_RECT_F* docAnchor = nullptr);
// Convenience for plain translated one-liners
void signalPushKey(App& app, int severity, int icon, const char* trKey,
                   std::wstring emph = L"", std::wstring context = L"");

// True while chips are draining or animating (keeps TIMER_NOTIFICATION)
bool signalsNeedTicks(const App& app);
// The severity hue on the current theme's luminance row (prompt chips in
// overlays.cpp share the palette)
D2D1_COLOR_F signalSeverityHue(const App& app, int severity);
// Stack + bell + tray; call late in render so chips ride above overlays
void renderSignalChips(App& app);
// Mouse press over a chip, the bell, or the open tray. Returns true when
// consumed (the caller swallows the matching release).
bool signalMouseDown(App& app, HWND hwnd, float mx, float my);
// Hover tracking (drain hold + hand cursor). True = pointer is over an
// interactive signal element.
bool signalMouseMove(App& app, float mx, float my);
// A press anywhere else parks every draining unpinned chip into the tray
void signalTuckPassive(App& app);

#endif  // TINTA_SIGNALS_H
