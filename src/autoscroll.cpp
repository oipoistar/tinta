// Middle-click autoscroll (#331). See autoscroll.h.
#include "autoscroll.h"

#include "input.h"
#include "touch.h"
#include "ui_paint.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace {

// USER timers tick at about 15.6 ms whatever is asked below that; a step
// moves by the time that has really passed
constexpr UINT kStepMs = 10;
// Faster than this a page is a blur: about eight screens a second
constexpr float kTopSpeed = 15000.0f;

// The axes the view scrolls along pull on it; the others are slack
void pull(const App& app, float& vx, float& vy) {
    const auto& a = app.autoScroll;
    vx = a.across ? autoScrollSpeed(app, static_cast<float>(a.pointer.x - a.origin.x)) : 0.0f;
    vy = autoScrollSpeed(app, static_cast<float>(a.pointer.y - a.origin.y));
}

void showCursor(const App& app) {
    SetCursor(LoadCursor(nullptr, app.autoScroll.across ? IDC_SIZEALL : IDC_SIZENS));
}

UINT releaseOf(UINT press) {
    switch (press) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: return WM_LBUTTONUP;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: return WM_RBUTTONUP;
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: return WM_MBUTTONUP;
        case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: return WM_XBUTTONUP;
        default: return 0;
    }
}

}  // namespace

bool autoScrollActive(const App& app) { return app.autoScroll.active; }

float autoScrollRest(const App& app) { return dpi(app, 12.0f); }

float autoScrollSpeed(const App& app, float offset) {
    const float unit = dpi(app, 1.0f);
    // How far past the mark, in pixels of a 96 dpi screen, so the feel is
    // the same at every scale
    const float past = (std::fabs(offset) - autoScrollRest(app)) / unit;
    if (past <= 0.0f) return 0.0f;
    // Per second: slow enough to read by close to the mark (a line or so
    // at 10 pixels), a screen at about 130, and steeply more beyond
    const float perSecond = std::min(1.5f * past + 0.05f * past * past, kTopSpeed);
    return (offset < 0.0f ? -perSecond : perSecond) * unit / 1000.0f;
}

bool autoScrollStart(App& app, HWND hwnd, POINT pt, double ms) {
    auto& a = app.autoScroll;
    if (!app.middleClickScroll || a.active) return false;
    PanelList list = PanelList::None;
    const TouchTarget target = touchTargetAt(app, pt, list);
    if (target != TouchTarget::Document && target != TouchTarget::Editor) return false;
    float maxX = 0, maxY = 0;
    touchScrollRange(app, target, list, maxX, maxY);
    // Nowhere to go: the click is left to whatever else wants it
    if (maxX <= 0.0f && maxY <= 0.0f) return false;
    touchStopFling(app, hwnd);
    a = {};
    a.active = a.held = true;
    a.target = target;
    a.across = maxX > 0.0f;
    a.origin = a.pointer = pt;
    touchScrollPosition(app, target, list, a.x, a.y);
    a.ms = ms;
    a.tab = app.activeTab;
    a.file = app.currentFile;
    a.edit = app.editMode;
    // No link stays underlined and no preview opens under a pointer that
    // is steering
    handleMouseMove(app, hwnd, MAKELPARAM(-1, -1));
    app.mouseX = app.mouseY = -1;
    SetCapture(hwnd);
    SetTimer(hwnd, TIMER_AUTO_SCROLL, kStepMs, nullptr);
    showCursor(app);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void autoScrollStop(App& app, HWND hwnd) {
    auto& a = app.autoScroll;
    if (!a.active) return;
    a.active = a.held = false;
    KillTimer(hwnd, TIMER_AUTO_SCROLL);
    if (GetCapture() == hwnd) ReleaseCapture();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void autoScrollPointer(App& app, HWND, POINT pt) {
    auto& a = app.autoScroll;
    if (!a.active) return;
    a.pointer = pt;
    float vx, vy;
    pull(app, vx, vy);
    // The button went down and the pointer left the mark with it: a drag,
    // which ends where the button comes up
    if (a.held && (vx != 0.0f || vy != 0.0f)) a.dragged = true;
}

void autoScrollRelease(App& app, HWND hwnd) {
    auto& a = app.autoScroll;
    if (!a.active || !a.held) return;
    a.held = false;
    if (a.dragged) autoScrollStop(app, hwnd);
}

bool autoScrollStep(App& app, HWND hwnd, double ms) {
    auto& a = app.autoScroll;
    if (!a.active) return false;
    // Another tab, document or view came up under it
    if (app.activeTab != a.tab || app.currentFile != a.file || app.editMode != a.edit) {
        autoScrollStop(app, hwnd);
        return false;
    }
    const double dt = std::min(ms - a.ms, 100.0);
    if (dt <= 0) return true;
    a.ms = ms;
    float vx, vy;
    pull(app, vx, vy);
    if (vx == 0.0f && vy == 0.0f) return true;
    // Whatever else moved the view meanwhile, a reload or a jump, is where
    // it goes on from
    float atX = 0, atY = 0, maxX = 0, maxY = 0;
    touchScrollPosition(app, a.target, PanelList::None, atX, atY);
    touchScrollRange(app, a.target, PanelList::None, maxX, maxY);
    if (std::fabs(atX - std::round(a.x)) > 1.0f) a.x = atX;
    if (std::fabs(atY - std::round(a.y)) > 1.0f) a.y = atY;
    // Kept within the ends, so turning back from one answers at once
    a.x = std::clamp(a.x + vx * static_cast<float>(dt), 0.0f, maxX);
    a.y = std::clamp(a.y + vy * static_cast<float>(dt), 0.0f, maxY);
    // Whole pixels on screen keep the text sharp; the fractions wait
    // here. An end is where it is, between two pixels or not
    float toX = a.x >= maxX ? maxX : std::round(a.x), toY = a.y >= maxY ? maxY : std::round(a.y);
    touchScrollTo(app, a.target, PanelList::None, toX, toY);
    return true;
}

bool autoScrollMessage(App& app, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& a = app.autoScroll;
    if (msg == WM_TIMER && wParam == TIMER_AUTO_SCROLL) {
        if (!a.active) {
            KillTimer(hwnd, TIMER_AUTO_SCROLL);
            return true;
        }
        // With no button down the pointer's moves outside the window do
        // not arrive: ask where it is
        POINT pt;
        if (GetCursorPos(&pt) && ScreenToClient(hwnd, &pt)) autoScrollPointer(app, hwnd, pt);
        autoScrollStep(app, hwnd, touchClockMs());
        return true;
    }
    if (!a.active) {
        // The click that ended it does nothing more as it comes up
        if (!a.swallowUp) return false;
        const bool taken = msg == a.swallowUp;
        if (taken || releaseOf(msg)) a.swallowUp = 0;
        return taken;
    }
    switch (msg) {
        case WM_MOUSEMOVE:
            autoScrollPointer(app, hwnd, POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            showCursor(app);
            return true;
        case WM_MBUTTONUP:
            if (!a.held) return false;
            autoScrollRelease(app, hwnd);
            return true;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
            autoScrollStop(app, hwnd);
            a.swallowUp = releaseOf(msg);
            return true;
        // These end it and then do what they always do
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_POINTERDOWN:
        case WM_SYSKEYDOWN:
        case WM_KILLFOCUS:
            autoScrollStop(app, hwnd);
            return false;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(lParam) != hwnd) autoScrollStop(app, hwnd);
            return false;
        default:
            return false;
    }
}

void renderAutoScrollMark(App& app) {
    const auto& a = app.autoScroll;
    if (!a.active || !app.renderTarget || !app.brush) return;
    const paint::Surface s = paint::surface(app);
    const float cx = static_cast<float>(a.origin.x) + 0.5f, cy = static_cast<float>(a.origin.y) + 0.5f;
    const float r = autoScrollRest(app);
    const D2D1_RECT_F disc = D2D1::RectF(cx - r, cy - r, cx + r, cy + r);
    paint::shadow(app, disc, r, s.shadow, 0.6f);
    paint::fillRound(app, disc, r, paint::alpha(s.fill, 0.94f));
    paint::strokeRound(app, disc, r, s.border, dpi(app, 1.0f));
    const float dot = dpi(app, 1.75f);
    paint::fillRound(app, D2D1::RectF(cx - dot, cy - dot, cx + dot, cy + dot), dot, s.text);
    // An arrowhead for each way the view goes: its point `tip` from the
    // centre along (ux, uy), its wings `wing` behind and to the sides
    const float tip = dpi(app, 8.5f), wing = dpi(app, 3.0f), width = dpi(app, 1.5f);
    auto head = [&](float ux, float uy) {
        const D2D1_POINT_2F point = D2D1::Point2F(cx + ux * tip, cy + uy * tip);
        const float bx = cx + ux * (tip - wing), by = cy + uy * (tip - wing);
        paint::line(app, D2D1::Point2F(bx - uy * wing, by + ux * wing), point, s.text, width, paint::roundStyle(app));
        paint::line(app, D2D1::Point2F(bx + uy * wing, by - ux * wing), point, s.text, width, paint::roundStyle(app));
    };
    head(0, -1);
    head(0, 1);
    if (a.across) {
        head(-1, 0);
        head(1, 0);
    }
}
