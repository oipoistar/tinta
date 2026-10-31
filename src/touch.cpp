// Touch (#286): a finger pans instead of selecting, taps click, and a held
// press selects. Suggested by @SHM-white. See touch.h.
#include "touch.h"

#include "editor.h"
#include "editrail.h"
#include "input.h"
#include "overlays.h"
#include "render.h"
#include "selection.h"
#include "sheet_ui.h"
#include "sidepanels.h"
#include "tableedit.h"
#include "startpage.h"
#include "tabs.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr UINT kHoldMs = 500;          // a press held this long selects
constexpr double kSpeedWindowMs = 100;  // a flick's speed is taken over this
constexpr double kRestMs = 80;          // still this long before lifting: no fling
constexpr double kFlingTauMs = 325;     // a fling slows by 1/e in this time

using Phase = App::TouchGesture::Phase;

// Movement under this is still a tap or a hold
float slop(const App& app) { return dpi(app, 10.0f); }

bool sameContext(const App& app) {
    const auto& t = app.touch;
    return app.activeTab == t.contextTab && app.currentFile == t.contextFile &&
           app.editMode == t.contextEdit;
}

void resetTap(App& app) {
    app.touch.tapMs = 0;
    app.touch.tapTarget = TouchTarget::None;
    app.lastClickTime = {};
    app.clickCount = 0;
}

bool isExtra(const App::TouchGesture& t, UINT32 pointer) {
    return std::find(t.extra.begin(), t.extra.end(), pointer) != t.extra.end();
}

// Menus, dialogs and overlays own their touches through the mouse
bool overlayUp(const App& app) {
    return app.showSettings || app.showThemeEditor || app.showShortcutEditor ||
           app.showThemeChooser || app.showPrintPreview || app.showLightbox ||
           app.showContextMenu || app.editCtxOpen || app.showTabMenu || app.showTabSwitcher ||
           app.confirmExitPending || app.createRefPending ||
           (app.annotEditorOpen && !app.editMode) || app.signalTrayOpen || app.gotoLineOpen ||
           app.appMenuPressed || (startPageActive(app) && !app.showHelp);
}

bool onSidePanel(const App& app, float x) {
    if (app.editMode) return false;
    if (app.showFolderBrowser) {
        const FolderBrowserMetrics g = folderBrowserMetrics(app);
        if (x >= g.cardLeft && x <= g.cardRight) return true;
    }
    if (contentsSlotOpen(app)) {
        const float width = tocPanelWidth(app);
        const float left = tocPanelX(app, width);
        if (x >= left && x <= left + width) return true;
    }
    return false;
}

bool onSearchBar(const App& app, float x, float y) {
    if (!app.showSearch) return false;
    D2D1_RECT_F bar = searchBarDrawRect(app);
    // The replace row sits under the find row
    if (app.editMode && app.searchReplaceMode) bar.bottom = searchReplaceRowRect(app).bottom;
    const float margin = dpi(app, 8.0f);
    return x >= bar.left - margin && x <= bar.right + margin && y >= bar.top - margin &&
           y <= bar.bottom + margin;
}

// Where a target scrolls to, and how far each way it can go
void scrollOf(const App& app, TouchTarget target, PanelList list, float& x, float& y) {
    x = y = 0;
    switch (target) {
        case TouchTarget::Document: x = app.scrollX; y = app.scrollY; break;
        case TouchTarget::Editor: y = app.editorScrollY; break;
        case TouchTarget::Sheet:
            // A sheet's camera, in pixels so a finger's travel moves it
            if (SheetDoc* doc = activeSheet(const_cast<App&>(app))) {
                x = doc->camX * sheetui::scale(app, *doc);
                y = doc->camY * sheetui::scale(app, *doc);
            }
            break;
        case TouchTarget::Help: y = app.helpScroll; break;
        case TouchTarget::List: {
            PanelListView view;
            if (panelListView(app, list, view)) y = view.scroll;
            break;
        }
        default: break;
    }
}

void scrollRange(const App& app, TouchTarget target, PanelList list, float& maxX, float& maxY) {
    maxX = maxY = 0;
    switch (target) {
        case TouchTarget::Help:
            maxY = app.showHelp ? std::max(0.0f, app.helpContentHeight - app.helpVisibleHeight) : 0;
            break;
        case TouchTarget::Document:
            maxX = std::max(0.0f, app.contentWidth - documentViewportWidth(app));
            maxY = std::max(0.0f, app.contentHeight - static_cast<float>(app.height));
            break;
        case TouchTarget::Editor:
            // Unwrapped lines have no right end to stop at; the editor
            // pans down and up only
            maxY = std::max(0.0f, app.editorContentHeight - static_cast<float>(app.height));
            break;
        case TouchTarget::Sheet:
            // A sheet has no edges
            maxX = maxY = 1e9f;
            break;
        case TouchTarget::List: {
            PanelListView view;
            if (panelListView(app, list, view)) maxY = panelListScrollbar(app, view).maxScroll;
            break;
        }
        default: break;
    }
}

// Scrolls a target to a position, kept within its range (written back);
// true when it moved
bool scrollTo(App& app, TouchTarget target, PanelList list, float& x, float& y) {
    float maxX, maxY, oldX, oldY;
    scrollRange(app, target, list, maxX, maxY);
    scrollOf(app, target, list, oldX, oldY);
    if (target != TouchTarget::Sheet) {
        x = std::clamp(x, 0.0f, maxX);
        y = std::clamp(y, 0.0f, maxY);
    }
    if (std::fabs(x - oldX) < 0.01f && std::fabs(y - oldY) < 0.01f) return false;
    switch (target) {
        case TouchTarget::Document:
            app.scrollX = app.targetScrollX = x;
            app.scrollY = app.targetScrollY = y;
            // Beside the editor, the editor goes where the page is taken,
            // as for a drag on the preview's scrollbar (#280)
            if (app.editMode) editorFollowPreview(app);
            break;
        case TouchTarget::Editor: app.editorScrollY = y; break;
        case TouchTarget::Help: app.helpScroll = y; break;
        case TouchTarget::List: setPanelListScroll(app, list, y); break;
        case TouchTarget::Sheet:
            if (SheetDoc* doc = activeSheet(app)) {
                float s = sheetui::scale(app, *doc);
                doc->camX = x / s;
                doc->camY = y / s;
                doc->flight.active = false;
                doc->placed = true;
            }
            break;
        default: break;
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
}

bool pansAcross(const App& app, TouchTarget target) {
    if (target == TouchTarget::Sheet) return true;
    return target == TouchTarget::Document && app.contentWidth > documentViewportWidth(app);
}

// A finger leaves no hover behind: no underlined link, no link peek
void pointerAway(App& app, HWND hwnd) {
    handleMouseMove(app, hwnd, MAKELPARAM(-1, -1));
    app.mouseX = app.mouseY = -1;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// A preview hold selects rendered text directly. Synthesizing a mouse
// press here would open an inline table cell instead of selecting it.
void previewHoldSelection(App& app, POINT pt, bool begin) {
    if (begin && app.tableEditActive) {
        tableEditCommit(app);
        editorReparse(app, true);
    }
    ensureLayoutComplete(app);
    const size_t at = selectionOffsetAtPoint(app, pt.x - documentViewportX(app) + app.scrollX,
                                             pt.y + app.scrollY);
    size_t first, last;
    selectionWordRange(app, at, first, last);
    if (begin) {
        app.selAnchorRangeStart = first;
        app.selAnchorRangeEnd = last;
        app.selectionMode = App::SelectionMode::Word;
    }
    app.selAnchor = std::min(first, app.selAnchorRangeStart);
    app.selFocus = std::max(last, app.selAnchorRangeEnd);
    app.selecting = true;
    app.hasSelection = app.selAnchor != app.selFocus;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// A tap is a click at the point, through the mouse handlers, so links,
// rows, buttons, check boxes and the editor's caret all take it
void click(App& app, HWND hwnd, POINT at) {
    const LPARAM point = MAKELPARAM(at.x, at.y);
    handleMouseMove(app, hwnd, point);
    handleMouseDown(app, hwnd, MK_LBUTTON, point);
    handleMouseUp(app, hwnd, 0, point);
    pointerAway(app, hwnd);
}

void tap(App& app, HWND hwnd, POINT pt, double ms, TouchTarget target) {
    auto& t = app.touch;
    const bool text = target == TouchTarget::Document || target == TouchTarget::Editor;
    const bool again = text && t.tapMs > 0 && ms >= t.tapMs &&
                       ms - t.tapMs <= GetDoubleClickTime() && t.tapTarget == target &&
                       std::hypot(float(pt.x - t.tapPoint.x), float(pt.y - t.tapPoint.y)) <=
                           dpi(app, 24.0f);
    // A finger's second tap lands a little off the first: it clicks where
    // the first did, so the click handlers count the double-click it is
    const POINT at = again ? t.tapPoint : pt;
    if (!again) resetTap(app);
    else app.lastClickTime = std::chrono::steady_clock::now();
    t.tapPoint = at;
    t.tapMs = text ? ms : 0;
    t.tapTarget = target;
    click(app, hwnd, at);
}

// The finger's speed over its last moves before it lifted at `ms`, in
// pixels per millisecond; none when it rested before lifting
void releaseSpeed(const App::TouchGesture& t, double ms, float& vx, float& vy) {
    vx = vy = 0;
    if (t.samples.size() < 2) return;
    const auto& last = t.samples.back();
    if (ms - last.ms > kRestMs) return;
    const App::TouchGesture::Sample* first = &t.samples.front();
    for (const auto& s : t.samples) {
        if (last.ms - s.ms <= kSpeedWindowMs) { first = &s; break; }
    }
    const double span = last.ms - first->ms;
    if (span < 5) return;
    vx = static_cast<float>((last.x - first->x) / span);
    vy = static_cast<float>((last.y - first->y) / span);
}

void startFling(App& app, HWND hwnd, float vx, float vy, double ms) {
    auto& t = app.touch;
    if (t.axis == 1) vx = 0;
    if (t.axis == 2) vy = 0;
    const float speed = std::hypot(vx, vy);
    if (speed < dpi(app, 0.25f)) return;
    const float cap = dpi(app, 6.0f);
    if (speed > cap) {
        vx *= cap / speed;
        vy *= cap / speed;
    }
    t.flinging = true;
    t.flingTarget = t.target;
    t.flingList = t.list;
    scrollOf(app, t.target, t.list, t.flingX, t.flingY);
    t.flingVX = vx;
    t.flingVY = vy;
    t.flingMs = ms;
    t.flingTab = app.activeTab;
    t.flingFile = app.currentFile;
    t.flingEdit = app.editMode;
    SetTimer(hwnd, TIMER_TOUCH_FLING, 16, nullptr);
}

void endGesture(App::TouchGesture& t) {
    t.active = false;
    t.phase = Phase::Idle;
    t.multi = false;
    t.braking = false;
    t.samples.clear();
}

}  // namespace

double touchClockMs() {
    static const double perMs = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / perMs;
}

TouchTarget touchTargetAt(const App& app, POINT pt, PanelList& list) {
    list = PanelList::None;
    const float x = static_cast<float>(pt.x), y = static_cast<float>(pt.y);
    if (overlayUp(app) || x < 0 || y < chromeTopHeight(app) || x >= app.width || y >= app.height)
        return TouchTarget::None;
    if (app.showHelp) {
        const auto panel = helpPanelRect(app);
        return x >= panel.left && x < panel.right - dpi(app, 16) &&
               y >= app.helpScrollbarTop && y < app.helpScrollbarTop + app.helpVisibleHeight
                   ? TouchTarget::Help : TouchTarget::None;
    }
    if (sidePanelResizeAt(app, x, y) != SidePanel::None) return TouchTarget::None;
    // A side panel's list; its scrollbar drags as with the mouse
    for (PanelList which : {PanelList::Search, PanelList::Contents, PanelList::Browser}) {
        PanelListView view;
        if (!panelListView(app, which, view) || x < view.list.left || x > view.cardRight ||
            y < view.list.top || y > view.list.bottom) continue;
        if (panelScrollbarAt(app, x, y) != PanelList::None) return TouchTarget::None;
        list = which;
        return TouchTarget::List;
    }
    // A panel's header, buttons and footer, and the search bar, take clicks
    if (onSidePanel(app, x) || onSearchBar(app, x, y)) return TouchTarget::None;
    const float left = documentViewportX(app);
    // A sheet's canvas pans under the finger; taps reach its buttons
    if (sheetActive(app) && x >= left) return TouchTarget::Sheet;
    if (app.editMode && x < left) {
        // The editor's text, short of the rail, its scrollbar and the seam
        return x >= editRailWidth(app) && x < editorPaneWidth(app) - dpi(app, 16.0f)
                   ? TouchTarget::Editor
                   : TouchTarget::None;
    }
    // The page, short of its scrollbars
    const float width = documentViewportWidth(app);
    if (x < left || x >= left + width) return TouchTarget::None;
    if (app.verticalScrollbarVisible && x >= left + width - dpi(app, 14.0f)) return TouchTarget::None;
    if (app.contentWidth > width && y >= app.height - dpi(app, 14.0f)) return TouchTarget::None;
    return TouchTarget::Document;
}

bool touchDown(App& app, HWND hwnd, UINT32 pointer, POINT pt, double ms) {
    auto& t = app.touch;
    if (t.active && t.pointer != pointer) {
        // Another finger: the first goes on panning, but no longer taps
        // or holds
        if (!isExtra(t, pointer)) t.extra.push_back(pointer);
        t.multi = true;
        resetTap(app);
        KillTimer(hwnd, TIMER_TOUCH_HOLD);
        return true;
    }
    PanelList list = PanelList::None;
    const TouchTarget target = touchTargetAt(app, pt, list);
    const bool braking = t.flinging && target != TouchTarget::None &&
                         target == t.flingTarget && list == t.flingList &&
                         app.activeTab == t.flingTab && app.currentFile == t.flingFile &&
                         app.editMode == t.flingEdit;
    touchStopFling(app, hwnd);
    if (!sameContext(app) || target == TouchTarget::None) resetTap(app);
    if (target == TouchTarget::None) return false;
    endGesture(t);
    t.active = true;
    t.braking = braking;
    if (braking) {
        t.tapMs = 0;
        app.lastClickTime = {};
    }
    t.pointer = pointer;
    t.phase = Phase::Pending;
    t.target = target;
    t.list = list;
    t.start = t.last = pt;
    t.contextTab = app.activeTab;
    t.contextFile = app.currentFile;
    t.contextEdit = app.editMode;
    t.axis = 0;
    t.samples.push_back({ms, static_cast<float>(pt.x), static_cast<float>(pt.y)});
    // A held row is a slow tap: only text selects
    if ((target == TouchTarget::Document || target == TouchTarget::Editor) && !braking)
        SetTimer(hwnd, TIMER_TOUCH_HOLD, kHoldMs, nullptr);
    return true;
}

bool touchMove(App& app, HWND hwnd, UINT32 pointer, POINT pt, double ms) {
    auto& t = app.touch;
    if (!t.active || pointer != t.pointer) return isExtra(t, pointer);
    if (!sameContext(app)) { touchCancel(app, hwnd); return true; }
    t.samples.push_back({ms, static_cast<float>(pt.x), static_cast<float>(pt.y)});
    while (t.samples.size() > 2 && ms - t.samples[1].ms > kSpeedWindowMs * 2)
        t.samples.erase(t.samples.begin());
    const float dx = static_cast<float>(pt.x - t.start.x);
    const float dy = static_cast<float>(pt.y - t.start.y);
    if (t.phase == Phase::Pending && std::hypot(dx, dy) >= slop(app)) {
        // Moving now: a pan, along the way it set out unless the target
        // also pans across
        KillTimer(hwnd, TIMER_TOUCH_HOLD);
        t.phase = Phase::Panning;
        resetTap(app);
        // A sheet pans every way at once
        t.axis = t.target == TouchTarget::Sheet ? 3
                 : std::fabs(dx) > std::fabs(dy) && pansAcross(app, t.target) ? 2 : 1;
        scrollOf(app, t.target, t.list, t.originX, t.originY);
        tabHoverCardDismiss(app, hwnd);
    }
    if (t.phase == Phase::Panning) {
        // The content moves with the finger from where the pan began
        float x = t.originX - (t.axis >= 2 ? dx : 0.0f);
        float y = t.originY - (t.axis != 2 ? dy : 0.0f);
        scrollTo(app, t.target, t.list, x, y);
    } else if (t.phase == Phase::Holding) {
        // The selection grows word by word, as a double-click's drag does
        if (t.target == TouchTarget::Editor) editorTouchSelection(app, pt.x, pt.y, false);
        else if (t.target == TouchTarget::Document && app.editMode) previewHoldSelection(app, pt, false);
        else handleMouseMove(app, hwnd, MAKELPARAM(pt.x, pt.y));
    }
    t.last = pt;
    return true;
}

bool touchUp(App& app, HWND hwnd, UINT32 pointer, POINT pt, double ms, bool canceled) {
    auto& t = app.touch;
    if (!t.active || pointer != t.pointer) {
        auto it = std::find(t.extra.begin(), t.extra.end(), pointer);
        if (it == t.extra.end()) return false;
        t.extra.erase(it);
        return true;
    }
    if (canceled || !sameContext(app)) { touchCancel(app, hwnd); return true; }
    KillTimer(hwnd, TIMER_TOUCH_HOLD);
    const Phase phase = t.phase;
    const TouchTarget target = t.target;
    const bool multi = t.multi;
    if (phase == Phase::Holding) {
        // The selection ends where the finger lifts, and the menu opens
        // there (in the editor, its own menu)
        const LPARAM point = MAKELPARAM(pt.x, pt.y);
        if (target == TouchTarget::Editor) {
            if (!canceled) {
                editorTouchSelection(app, pt.x, pt.y, false);
                openEditCtxMenu(app, hwnd, (float)pt.x, (float)pt.y, true);
            }
        } else if (target == TouchTarget::Document && app.editMode) {
            if (!canceled) previewHoldSelection(app, pt, false);
            app.selecting = false;
        } else {
            handleMouseMove(app, hwnd, point);
            handleMouseUp(app, hwnd, 0, point);
        }
        if (!canceled && target != TouchTarget::Editor) {
            POINT screen = pt;
            ClientToScreen(hwnd, &screen);
            handleContextMenu(app, hwnd, MAKELPARAM(screen.x, screen.y));
        }
        pointerAway(app, hwnd);
    } else if (!canceled && phase == Phase::Panning) {
        float vx, vy;
        releaseSpeed(t, ms, vx, vy);
        startFling(app, hwnd, vx, vy, ms);
    } else if (!canceled && phase == Phase::Pending && !multi && !t.braking) {
        tap(app, hwnd, pt, ms, target);
    }
    endGesture(t);
    return true;
}

void touchHold(App& app, HWND hwnd) {
    KillTimer(hwnd, TIMER_TOUCH_HOLD);
    auto& t = app.touch;
    if (!t.active || t.phase != Phase::Pending || t.multi || t.braking) return;
    if (t.target != TouchTarget::Document && t.target != TouchTarget::Editor) return;
    if (!sameContext(app)) { touchCancel(app, hwnd); return; }
    resetTap(app);
    t.phase = Phase::Holding;
    if (t.target == TouchTarget::Editor) {
        editorTouchSelection(app, t.start.x, t.start.y, true);
        return;
    }
    if (t.target == TouchTarget::Document && app.editMode) {
        previewHoldSelection(app, t.start, true);
        return;
    }
    // The word under the finger, selected as a double-click selects it:
    // the click handlers see a second click at the same place
    const LPARAM point = MAKELPARAM(t.start.x, t.start.y);
    handleMouseMove(app, hwnd, point);
    app.lastClickTime = std::chrono::steady_clock::now();
    app.lastClickX = t.start.x;
    app.lastClickY = t.start.y;
    app.clickCount = 1;
    handleMouseDown(app, hwnd, MK_LBUTTON, point);
    // The finger's moves arrive here whatever the mouse does; the press's
    // mouse capture is not needed, and must not cost the finger its own
    if (GetCapture() == hwnd) ReleaseCapture();
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool touchFlingStep(App& app, HWND hwnd, double ms) {
    auto& t = app.touch;
    if (!t.flinging) return false;
    if (t.flingTarget == TouchTarget::Help && !app.showHelp) {
        touchStopFling(app, hwnd);
        return false;
    }
    // The document under it changed: the fling belonged to the other one
    if (app.activeTab != t.flingTab || app.currentFile != t.flingFile || app.editMode != t.flingEdit) {
        touchStopFling(app, hwnd);
        return false;
    }
    const double dt = std::min(ms - t.flingMs, 100.0);
    if (dt <= 0) return true;
    t.flingMs = ms;
    const double decay = std::exp(-dt / kFlingTauMs);
    const float travel = static_cast<float>(kFlingTauMs * (1.0 - decay));
    t.flingX -= t.flingVX * travel;
    t.flingY -= t.flingVY * travel;
    t.flingVX *= static_cast<float>(decay);
    t.flingVY *= static_cast<float>(decay);
    const bool moved = scrollTo(app, t.flingTarget, t.flingList, t.flingX, t.flingY);
    // It stops at the end, or once it has slowed to a crawl
    if (!moved || std::hypot(t.flingVX, t.flingVY) < dpi(app, 0.02f)) {
        touchStopFling(app, hwnd);
        return false;
    }
    return true;
}

void touchStopFling(App& app, HWND hwnd) {
    if (!app.touch.flinging) return;
    app.touch.flinging = false;
    KillTimer(hwnd, TIMER_TOUCH_FLING);
}

void touchCancel(App& app, HWND hwnd) {
    auto& t = app.touch;
    KillTimer(hwnd, TIMER_TOUCH_HOLD);
    touchStopFling(app, hwnd);
    resetTap(app);
    // A selection under way keeps what it has, without the menu
    if (t.active && t.phase == Phase::Holding) {
        // Cancel the owned selection directly; a new modal must not receive
        // a synthetic release that could activate one of its controls.
        if (t.target == TouchTarget::Editor) handleEditorMouseUp(app, hwnd, t.last.x, t.last.y);
        else app.selecting = false;
        pointerAway(app, hwnd);
    }
    endGesture(t);
    t.extra.clear();
}

bool handlePointerMessage(App& app, HWND hwnd, UINT msg, WPARAM wParam, LPARAM) {
    const UINT32 pointer = GET_POINTERID_WPARAM(wParam);
    if (msg == WM_POINTERCAPTURECHANGED) {
        if (app.touch.active && pointer == app.touch.pointer) touchCancel(app, hwnd);
        return false;
    }
    POINTER_INPUT_TYPE type = PT_POINTER;
    if (!GetPointerType(pointer, &type) || type != PT_TOUCH) return false;
    POINTER_INFO info{};
    if (!GetPointerInfo(pointer, &info)) return false;
    POINT pt = info.ptPixelLocation;
    ScreenToClient(hwnd, &pt);
    double ms = touchClockMs();
    if (info.PerformanceCount) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        ms = static_cast<double>(info.PerformanceCount) / (static_cast<double>(f.QuadPart) / 1000.0);
    }
    switch (msg) {
        case WM_POINTERDOWN: return touchDown(app, hwnd, pointer, pt, ms);
        case WM_POINTERUPDATE: return touchMove(app, hwnd, pointer, pt, ms);
        case WM_POINTERUP:
            return touchUp(app, hwnd, pointer, pt, ms, (info.pointerFlags & POINTER_FLAG_CANCELED) != 0);
        default: return false;
    }
}
