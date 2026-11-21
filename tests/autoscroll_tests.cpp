// #331: a middle click on the page or on the editor's text leaves a mark,
// and the view scrolls towards the pointer, faster the further the pointer
// is from the mark. Any click, any key or the wheel ends it; with the
// button kept down it ends where the button is let go.
//
// The steps are driven by hand, at times of the test's own clock, as the
// touch tests drive a fling: the timer only calls the same step.
#include "autoscroll.h"
#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "overlays.h"
#include "render.h"
#include "settings.h"
#include "sidepanels.h"
#include "touch.h"
#include "ui_frame.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

namespace {
namespace fs = std::filesystem;

int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) {
        if (failures < 60) std::cerr << "FAIL middle-click scroll: " << message << '\n';
        ++failures;
    }
}

double ms = 1000.0;  // the test's clock

bool about(float a, float b, float within) { return std::fabs(a - b) <= within; }

std::string num(float v) { return std::to_string(v); }

// Steps `every` ms apart with the pointer where it is; false as soon as a
// step leaves the view between two pixels
bool run(App& app, int steps, double every = 8.0) {
    bool whole = true;
    for (int i = 0; i < steps; i++) {
        autoScrollStep(app, app.hwnd, ms += every);
        whole = whole && app.scrollY == std::round(app.scrollY) && app.scrollX == std::round(app.scrollX);
    }
    return whole;
}

bool message(App& app, UINT msg, WPARAM wParam = 0, LPARAM lParam = 0) {
    return autoScrollMessage(app, app.hwnd, msg, wParam, lParam);
}

POINT below(const App& app, POINT at, float past) {
    return POINT{at.x, at.y + (LONG)std::lround(autoScrollRest(app) + dpi(app, past))};
}
POINT above(const App& app, POINT at, float past) {
    return POINT{at.x, at.y - (LONG)std::lround(autoScrollRest(app) + dpi(app, past))};
}
POINT rightOf(const App& app, POINT at, float past) {
    return POINT{at.x + (LONG)std::lround(autoScrollRest(app) + dpi(app, past)), at.y};
}

// A click of the wheel: down and up again on the spot
bool middleClick(App& app, POINT at) {
    if (!autoScrollStart(app, app.hwnd, at, ms += 100)) return false;
    message(app, WM_MBUTTONUP, 0, MAKELPARAM(at.x, at.y));
    return true;
}

void open(App& app, const std::wstring& path, bool wrapCode) {
    if (app.editMode) exitEditMode(app);
    app.wrapCode = wrapCode;
    check(openDocumentInViewer(app, path), "the document opens");
    ensureLayoutComplete(app);
    app.verticalScrollbarVisible = app.contentHeight > app.height;
    app.scrollX = app.targetScrollX = app.scrollY = app.targetScrollY = 0;
    app.hasSelection = app.selecting = false;
}

// A frame in edit mode: the editor paints (its scrollable height comes
// from that) and the preview follows it, as each paint does
void editFrame(App& app) {
    ensureLayoutComplete(app);
    app.renderTarget->BeginDraw();
    renderEditor(app, editorPaneWidth(app));
    check(SUCCEEDED(app.renderTarget->EndDraw()), "the editor paints");
    syncPreviewToEditor(app);
}

// The mark six times its size, as a picture to look at
void savePicture(App& app, const uiframe::Frame& frame, POINT at, const std::string& name) {
    const int half = 24, zoom = 6, side = half * 2 * zoom;
    uiframe::Frame big;
    big.width = big.height = side;
    big.pixels.resize((size_t)side * side * 4);
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            const D2D1_COLOR_F c = frame.at((float)(at.x - half + x / zoom), (float)(at.y - half + y / zoom));
            BYTE* p = big.pixels.data() + ((size_t)y * side + x) * 4;
            p[0] = (BYTE)std::lround(c.b * 255);
            p[1] = (BYTE)std::lround(c.g * 255);
            p[2] = (BYTE)std::lround(c.r * 255);
            p[3] = 255;
        }
    }
    std::error_code ec;
    fs::create_directories(fs::current_path() / "frames", ec);
    uiframe::save(app, big, (fs::current_path() / "frames" / (name + ".png")).wstring());
}

void speedCurve(App& app, const std::string& where) {
    const float unit = dpi(app, 1.0f), rest = autoScrollRest(app);
    check(autoScrollSpeed(app, 0) == 0 && autoScrollSpeed(app, rest) == 0 && autoScrollSpeed(app, -rest) == 0,
          "a pointer on the mark pulls nothing" + where);
    check(autoScrollSpeed(app, rest + unit) > 0 && autoScrollSpeed(app, -rest - unit) < 0,
          "a pixel past the mark it pulls, down below it and up above" + where);
    // Pixels a second for pixels past the mark, both of a 100% screen
    const float table[][2] = {{10, 20}, {50, 200}, {100, 650}, {200, 2300}, {300, 4950}};
    for (const auto& row : table) {
        const float got = autoScrollSpeed(app, rest + row[0] * unit) * 1000.0f / unit;
        check(about(got, row[1], 0.01f * row[1]),
              num(row[0]) + " past the mark is " + num(row[1]) + " a second" + where + ": " + num(got));
    }
    bool rising = true, mirrored = true;
    for (float d = rest + unit; d < rest + 500 * unit; d += 3 * unit) {
        rising = rising && autoScrollSpeed(app, d + 3 * unit) > autoScrollSpeed(app, d);
        mirrored = mirrored && autoScrollSpeed(app, -d) == -autoScrollSpeed(app, d);
    }
    check(rising, "further from the mark is always faster" + where);
    check(mirrored, "up is as fast as down" + where);
    check(about(autoScrollSpeed(app, 1.0e6f), 15.0f * unit, 0.001f) &&
              about(autoScrollSpeed(app, -1.0e6f), -15.0f * unit, 0.001f),
          "the speed has a top" + where);
}

void pageScenario(App& app, const std::wstring& path, int width, float scale) {
    const std::string where = " (" + std::to_string(width) + ", " + num(scale).substr(0, 3) + "x, theme " +
                              std::to_string(app.currentThemeIndex) + ")";
    app.contentScale = scale;
    app.width = (int)(width * scale);
    app.height = (int)(800 * scale);
    updateTextFormats(app);
    open(app, path, true);
    const float unit = dpi(app, 1.0f);
    const float maxY = app.contentHeight - app.height;
    check(maxY > 1500 * unit, "the page is some screens long" + where + ": " + num(maxY));
    check(app.contentWidth <= documentViewportWidth(app), "and no wider than its view" + where);
    speedCurve(app, where);

    // Where it starts and where it does not
    const POINT at{app.width / 2, app.height / 2};
    const POINT strip{app.width / 2, (LONG)(chromeTopHeight(app) / 2)};
    check(!autoScrollStart(app, app.hwnd, strip, ms += 100) && !autoScrollActive(app),
          "a middle click on the tab strip is the strip's" + where);
    app.showSettings = true;
    check(!autoScrollStart(app, app.hwnd, at, ms += 100), "nor does it start under an open dialog" + where);
    app.showSettings = false;
    app.middleClickScroll = false;
    check(!autoScrollStart(app, app.hwnd, at, ms += 100), "nor with middleClickScroll=0" + where);
    app.middleClickScroll = true;
    {
        // A side panel's list scrolls for a finger, and is not the page:
        // in a window too low for its rows, a middle click on it starts
        // nothing
        const int tall = app.height;
        app.height = (int)(260 * scale);
        app.showToc = true;
        app.tocAnimation = 1;
        app.tocScroll = 0;
        app.tocPinned = false;
        app.tocFilter.clear();
        const D2D1_RECT_F rows = tocListRect(app);
        const POINT inList{(LONG)(rows.left + dpi(app, 40)), (LONG)((rows.top + rows.bottom) / 2)};
        PanelList list = PanelList::None;
        float listX = 0, listY = 0;
        check(touchTargetAt(app, inList, list) == TouchTarget::List, "the Contents list is under the point" + where);
        touchScrollRange(app, TouchTarget::List, list, listX, listY);
        check(listY > 0, "and has more rows than it shows" + where);
        check(!autoScrollStart(app, app.hwnd, inList, ms += 100) && !autoScrollActive(app),
              "a middle click on the Contents list starts nothing" + where);
        app.showToc = false;
        app.height = tall;
    }

    // The press: a mark, the pointer on it, nothing moving
    check(autoScrollStart(app, app.hwnd, at, ms += 100), "a middle press on the page starts it" + where);
    const auto& a = app.autoScroll;
    check(a.active && a.held && !a.dragged && a.target == TouchTarget::Document && !a.across &&
              a.origin.x == at.x && a.origin.y == at.y,
          "the mark is where the press fell, with the page to scroll up and down" + where);
    check(GetCapture() == app.hwnd, "the window holds the mouse" + where);
    check(app.mouseX == -1 && app.mouseY == -1, "the pointer hovers over nothing while it steers" + where);
    check(!autoScrollStart(app, app.hwnd, at, ms), "a second start while it is on is none" + where);
    run(app, 125);
    check(app.scrollY == 0, "the page stands still while the pointer rests on the mark" + where);
    autoScrollPointer(app, app.hwnd, POINT{at.x + 3, at.y + (LONG)autoScrollRest(app)});
    run(app, 125);
    check(app.scrollY == 0 && !a.dragged, "and anywhere within the mark" + where);

    // Let go without having left the mark: a click, and it stays on
    check(message(app, WM_MBUTTONUP, 0, MAKELPARAM(at.x, at.y)) && a.active && !a.held,
          "let go on the mark, it stays on" + where);
    check(!message(app, WM_MBUTTONUP), "a second release is nobody's" + where);

    // 100 past the mark is 650 a second
    check(message(app, WM_MOUSEMOVE, 0, MAKELPARAM(at.x, below(app, at, 100).y)) &&
              a.pointer.y == below(app, at, 100).y,
          "the pointer's moves steer it" + where);
    bool whole = run(app, 125);
    check(about(app.scrollY, 650 * unit, 2.0f) && app.targetScrollY == app.scrollY,
          "a second at 100 below the mark is 650 down" + where + ": " + num(app.scrollY));
    check(whole, "every step leaves the page on a whole pixel" + where);
    check(a.active && !a.dragged, "and it stays on" + where);
    // The same in fewer, longer steps: the time counts, not the steps
    float before = app.scrollY;
    run(app, 25, 40.0);
    check(about(app.scrollY - before, 650 * unit, 2.0f),
          "a second in 40 ms steps goes as far" + where + ": " + num(app.scrollY - before));
    // Something else moves the page meanwhile, a jump or a reload: it
    // goes on from there
    app.scrollY = app.targetScrollY = std::round(300 * unit);
    autoScrollStep(app, app.hwnd, ms += 16);
    check(app.scrollY > 300 * unit && app.scrollY < 315 * unit,
          "moved by something else, the page goes on from where it was put" + where + ": " + num(app.scrollY));
    // A stall is not made up for in one leap
    before = app.scrollY;
    autoScrollStep(app, app.hwnd, ms += 5000);
    check(app.scrollY - before <= 66 * unit, "a step after a pause moves a tenth of a second's worth" + where);

    // Slow near the mark: the fractions are kept until they make a pixel
    autoScrollPointer(app, app.hwnd, below(app, at, 10));
    before = app.scrollY;
    whole = run(app, 250);
    check(about(app.scrollY - before, 40 * unit, 2.0f) && whole,
          "two seconds at 10 below the mark is 40 down, a pixel at a time" + where + ": " +
              num(app.scrollY - before));

    // Sideways the pointer is slack on a page that fits its width
    autoScrollPointer(app, app.hwnd, rightOf(app, at, 300));
    before = app.scrollY;
    run(app, 125);
    check(app.scrollX == 0 && app.scrollY == before, "sideways the pointer pulls nothing on a page that fits" + where);

    // Up, and to rest at the top
    autoScrollPointer(app, app.hwnd, above(app, at, 100));
    before = app.scrollY;
    run(app, 25);
    check(about(before - app.scrollY, 130 * unit, 2.0f), "above the mark it goes up" + where);
    autoScrollPointer(app, app.hwnd, above(app, at, 300));
    run(app, 400);
    check(app.scrollY == 0, "it stops at the top" + where);
    run(app, 60);
    check(app.scrollY == 0 && a.active, "and rests there, still on" + where);
    // Pulled gently against the top, for however long, it turns back as
    // promptly: nothing piles up beyond the end
    bool prompt = true;
    for (int extra = 0; extra < 12; extra++) {
        autoScrollPointer(app, app.hwnd, above(app, at, 10));
        run(app, 250 + extra);
        prompt = prompt && app.scrollY == 0;
        autoScrollPointer(app, app.hwnd, below(app, at, 10));
        run(app, 6);
        prompt = prompt && app.scrollY >= 1;
    }
    check(prompt, "a pull against the top is not stored up: 48 ms below the mark and it moves" + where);
    autoScrollPointer(app, app.hwnd, above(app, at, 300));
    run(app, 60);
    autoScrollPointer(app, app.hwnd, below(app, at, 100));
    autoScrollStep(app, app.hwnd, ms += 16);
    check(app.scrollY >= 9 * unit, "from the top it goes down on the next step" + where);

    // To the end, where it rests without a tremor
    autoScrollPointer(app, app.hwnd, POINT{at.x, at.y + 20000});
    for (int i = 0; i < 2000 && app.scrollY < maxY - 0.01f; i++) autoScrollStep(app, app.hwnd, ms += 16);
    check(about(app.scrollY, maxY, 0.01f), "far below the mark it reaches the end" + where + ": " + num(app.scrollY));
    const float end = app.scrollY;
    bool still = true;
    for (int i = 0; i < 60; i++) {
        autoScrollStep(app, app.hwnd, ms += 16);
        still = still && app.scrollY == end;
    }
    check(still && a.active, "and rests there, still on" + where);
    autoScrollPointer(app, app.hwnd, above(app, at, 100));
    autoScrollStep(app, app.hwnd, ms += 16);
    check(app.scrollY <= end - 9 * unit, "from the end it goes up on the next step" + where);

    // What ends it. A click does nothing else, nor does its release
    const LPARAM lp = MAKELPARAM(at.x, at.y);
    const struct { UINT down, up; const char* name; } buttons[] = {
        {WM_LBUTTONDOWN, WM_LBUTTONUP, "left"},
        {WM_RBUTTONDOWN, WM_RBUTTONUP, "right"},
        {WM_MBUTTONDOWN, WM_MBUTTONUP, "middle"},
        {WM_XBUTTONDOWN, WM_XBUTTONUP, "side"},
    };
    for (const auto& button : buttons) {
        if (!autoScrollActive(app)) check(middleClick(app, at), "it starts again" + where);
        const float y = app.scrollY;
        check(message(app, button.down, 0, lp) && !autoScrollActive(app) && GetCapture() != app.hwnd,
              std::string("a ") + button.name + " click ends it and is taken" + where);
        check(message(app, button.up, 0, lp), std::string("the ") + button.name + " button's release is taken too" + where);
        check(!message(app, button.up, 0, lp) && !message(app, button.down, 0, lp),
              std::string("the next ") + button.name + " click is the window's again" + where);
        check(app.scrollY == y && !app.selecting && !app.hasSelection && !app.showContextMenu,
              "and the click did nothing else" + where);
        run(app, 30);
        check(app.scrollY == y, "nothing moves once it is over" + where);
    }
    // The release of another button than the one that ended it is not taken
    check(middleClick(app, at) && message(app, WM_LBUTTONDOWN, 0, lp) && !message(app, WM_RBUTTONUP, 0, lp) &&
              message(app, WM_LBUTTONUP, 0, lp),
          "only the ending click's own release is taken" + where);

    // The wheel, a finger, Alt's keys and the window losing the keyboard
    // end it and go on to the window
    for (UINT msg : {WM_MOUSEWHEEL, WM_MOUSEHWHEEL, WM_POINTERDOWN, WM_SYSKEYDOWN, WM_KILLFOCUS}) {
        check(middleClick(app, at) && !message(app, msg) && !autoScrollActive(app),
              "message " + std::to_string(msg) + " ends it and goes on" + where);
    }
    check(middleClick(app, at) && !message(app, WM_CAPTURECHANGED, 0, (LPARAM)app.hwnd) && autoScrollActive(app),
          "its own capture does not end it" + where);
    check(!message(app, WM_CAPTURECHANGED, 0, 0) && !autoScrollActive(app), "another window's does" + where);

    // Keys: Ctrl or Shift by itself is no key, Esc only ends it, and any
    // other key ends it and does what it does
    app.scrollY = app.targetScrollY = 0;
    check(middleClick(app, at), "it starts for the keys" + where);
    keystroke(app, VK_CONTROL, {true, false, false});
    keystroke(app, VK_SHIFT, {false, true, false});
    check(autoScrollActive(app), "Ctrl and Shift by themselves leave it on" + where);
    check(keystroke(app, VK_ESCAPE) && !autoScrollActive(app) && app.scrollY == 0 && !app.showSearch,
          "Esc ends it and does nothing else" + where);
    check(middleClick(app, at), "it starts again" + where);
    keystroke(app, VK_NEXT);
    check(!autoScrollActive(app) && app.targetScrollY > app.height / 2, "PgDn ends it and pages" + where);
    app.scrollY = app.targetScrollY = 0;
    check(middleClick(app, at), "it starts again" + where);
    keystroke(app, 'F', {true, false, false});
    check(!autoScrollActive(app) && app.showSearch, "Ctrl+F ends it and opens the bar" + where);
    keystroke(app, VK_ESCAPE);
    check(!app.showSearch, "and Esc then closes the bar" + where);

    // Held and dragged: it scrolls while the button is down and ends
    // where the button is let go
    app.scrollY = app.targetScrollY = 0;
    check(autoScrollStart(app, app.hwnd, at, ms += 100), "a press starts it" + where);
    message(app, WM_MOUSEMOVE, 0, MAKELPARAM(at.x, below(app, at, 100).y));
    check(a.dragged, "leaving the mark with the button down is a drag" + where);
    run(app, 125);
    check(about(app.scrollY, 650 * unit, 2.0f), "which scrolls as a click does" + where);
    // Back on the mark it is still a drag
    message(app, WM_MOUSEMOVE, 0, lp);
    check(message(app, WM_MBUTTONUP, 0, lp) && !autoScrollActive(app), "and ends where the button is let go" + where);
    const float dropped = app.scrollY;
    run(app, 30);
    check(app.scrollY == dropped, "the page stays where it was left" + where);

    // Another tab or document under it: it belonged to the other one
    check(middleClick(app, at), "it starts again" + where);
    autoScrollPointer(app, app.hwnd, below(app, at, 100));
    app.activeTab += 1;
    check(!autoScrollStep(app, app.hwnd, ms += 16) && !autoScrollActive(app) && app.scrollY == dropped,
          "another tab coming up ends it without a move" + where);
    app.activeTab -= 1;

    // The mark: drawn where the press fell and nowhere else, and gone after
    app.scrollY = app.targetScrollY = 0;
    const uiframe::Frame plain = uiframe::draw(app, [&] { renderAutoScrollMark(app); });
    check(middleClick(app, at), "it starts for the mark" + where);
    const uiframe::Frame marked = uiframe::draw(app, [&] { renderAutoScrollMark(app); });
    const float r = autoScrollRest(app);
    const auto box = [&](float half) { return D2D1::RectF(at.x - half, at.y - half, at.x + half, at.y + half); };
    check(!plain.empty() && !marked.empty() && marked.differs(plain, box(r)), "the mark is drawn where the press fell" + where);
    // An arrow is ink on the mark's own fill, where its point is
    const D2D1_COLOR_F fill = marked.at(at.x - 6 * unit, at.y - 6 * unit);
    const auto inked = [&](const uiframe::Frame& frame, float dx, float dy) {
        const float tip = 8.5f * unit;
        for (float along = -unit; along <= 3 * unit; along += 1) {
            for (float aside = -1.5f * unit; aside <= 1.5f * unit; aside += 1) {
                const float x = at.x + dx * (tip - along) + dy * aside, y = at.y + dy * (tip - along) + dx * aside;
                if (std::fabs(uiframe::luminance(frame.at(x, y)) - uiframe::luminance(fill)) > 0.2f) return true;
            }
        }
        return false;
    };
    check(inked(marked, 0, -1) && inked(marked, 0, 1), "with an arrow up and one down" + where);
    check(!inked(marked, -1, 0) && !inked(marked, 1, 0), "and none sideways on a page that fits its width" + where);
    savePicture(app, marked, at, "mark-" + std::to_string(app.currentThemeIndex) + "-" + num(scale).substr(0, 3));
    bool elsewhere = false;
    const float keep = r + 8 * unit;  // the mark and its shadow
    for (const D2D1_RECT_F& rest : {D2D1::RectF(0, 0, (float)app.width, at.y - keep),
                                    D2D1::RectF(0, at.y + keep, (float)app.width, (float)app.height),
                                    D2D1::RectF(0, 0, at.x - keep, (float)app.height),
                                    D2D1::RectF(at.x + keep, 0, (float)app.width, (float)app.height)}) {
        elsewhere = elsewhere || marked.differs(plain, rest);
    }
    check(!elsewhere, "and nothing is drawn away from it" + where);
    const D2D1_COLOR_F ink = marked.at((float)at.x, (float)at.y), page = plain.at((float)at.x, (float)at.y);
    check(std::fabs(uiframe::luminance(ink) - uiframe::luminance(page)) > 0.2f,
          "its centre dot stands out from the page" + where);
    autoScrollStop(app, app.hwnd);
    const uiframe::Frame after = uiframe::draw(app, [&] { renderAutoScrollMark(app); });
    check(!after.differs(plain, D2D1::RectF(0, 0, (float)app.width, (float)app.height)), "the mark goes when it ends" + where);

    // A page wider than its view goes sideways too, and says so
    open(app, path, false);
    const float maxX = app.contentWidth - documentViewportWidth(app);
    check(maxX > 100 * unit, "with code unwrapped the page is wider than its view" + where + ": " + num(maxX));
    check(middleClick(app, at) && a.across, "a wide page goes sideways too" + where);
    const uiframe::Frame four = uiframe::draw(app, [&] { renderAutoScrollMark(app); });
    check(inked(four, -1, 0) && inked(four, 1, 0) && inked(four, 0, -1) && inked(four, 0, 1),
          "its mark has an arrow left and one right as well" + where);
    savePicture(app, four, at, "mark-wide-" + std::to_string(app.currentThemeIndex) + "-" + num(scale).substr(0, 3));
    autoScrollPointer(app, app.hwnd, rightOf(app, at, 100));
    run(app, 125);
    check(about(app.scrollX, std::min(650 * unit, maxX), 2.0f) && app.scrollY == 0,
          "right of the mark it goes right, and neither up nor down" + where + ": " + num(app.scrollX));
    autoScrollPointer(app, app.hwnd, POINT{at.x - 20000, at.y});
    run(app, 250);
    check(app.scrollX == 0, "and back to the left edge" + where);
    autoScrollPointer(app, app.hwnd, POINT{rightOf(app, at, 50).x, below(app, at, 50).y});
    run(app, 125);
    check(about(app.scrollX, std::min(200 * unit, maxX), 2.0f) && about(app.scrollY, 200 * unit, 2.0f),
          "aslant it goes both ways" + where);
    keystroke(app, VK_ESCAPE);
    check(!autoScrollActive(app), "Esc ends it" + where);
}

void editorScenario(App& app, const std::wstring& path) {
    app.contentScale = 1.0f;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    open(app, path, true);
    enterEditMode(app);
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, {true, false, false});
    app.editorShowPreview = true;
    check(app.editMode && !editorLiveOn(app), "the source editor opens");
    editFrame(app);
    PanelList list = PanelList::None;
    const POINT text{(LONG)(editRailWidth(app) + 160), app.height / 2};
    const POINT page{(LONG)(documentViewportX(app) + documentViewportWidth(app) / 2), app.height / 2};
    check(touchTargetAt(app, text, list) == TouchTarget::Editor && touchTargetAt(app, page, list) == TouchTarget::Document,
          "the editor's text on the left, the page on the right");
    const std::wstring source = app.editorText;

    check(middleClick(app, text) && app.autoScroll.target == TouchTarget::Editor && !app.autoScroll.across,
          "a middle click on the editor's text starts it there");
    autoScrollPointer(app, app.hwnd, below(app, text, 100));
    run(app, 125);
    check(about(app.editorScrollY, 650, 2.0f), "the text scrolls as the page does: " + num(app.editorScrollY));
    check(app.editorText == source && !app.editorHasSelection, "nothing is typed or selected by it");
    autoScrollPointer(app, app.hwnd, POINT{text.x, -20000});
    run(app, 400);
    check(app.editorScrollY == 0, "and stops at its top");
    keystroke(app, VK_DOWN);
    check(!autoScrollActive(app) && app.editorText == source, "an arrow key ends it");

    // The page beside the editor: the editor follows it, as it follows a
    // drag on the page's scrollbar
    editFrame(app);
    check(middleClick(app, page) && app.autoScroll.target == TouchTarget::Document,
          "a middle click on the page beside the editor starts it there");
    autoScrollPointer(app, app.hwnd, below(app, page, 100));
    const float editorWas = app.editorScrollY;
    run(app, 125);
    check(about(app.scrollY, 650, 2.0f), "the page beside the editor scrolls: " + num(app.scrollY));
    check(app.editorScrollY > editorWas, "and the editor follows it");
    check(message(app, WM_LBUTTONDOWN) && !autoScrollActive(app), "a click ends it");
    message(app, WM_LBUTTONUP);

    // Leaving the editor under it ends it: it belonged to the editor
    check(middleClick(app, text), "it starts on the text again");
    autoScrollPointer(app, app.hwnd, below(app, text, 100));
    exitEditMode(app);
    check(!autoScrollStep(app, app.hwnd, ms += 16) && !autoScrollActive(app), "the editor closing under it ends it");

    // Live: the page is the document's view
    enterEditMode(app);
    if (!editorLiveOn(app)) keystroke(app, VK_OEM_2, {true, false, false});
    check(editorLiveOn(app), "Live opens");
    ensureLayoutComplete(app);
    app.scrollY = app.targetScrollY = 0;
    const POINT live{app.width / 2, app.height / 2};
    if (touchTargetAt(app, live, list) == TouchTarget::Document) {
        check(middleClick(app, live), "a middle click on Live's page starts it");
        autoScrollPointer(app, app.hwnd, below(app, live, 100));
        run(app, 125);
        check(about(app.scrollY, 650, 2.0f), "Live's page scrolls: " + num(app.scrollY));
        keystroke(app, VK_ESCAPE);
        check(!autoScrollActive(app) && app.editMode, "Esc ends it and stays in the editor");
    } else {
        check(false, "Live's page is the document's view");
    }
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, {true, false, false});
    exitEditMode(app);
}

void shortPage(App& app) {
    const fs::path path = fs::current_path() / "short.md";
    {
        std::ofstream out(path, std::ios::binary);
        out << "# A short page\n\nIt fits its window: there is nowhere to scroll to.\n";
    }
    open(app, path.wstring(), true);
    check(app.contentHeight <= app.height, "the short page fits its window");
    const POINT at{app.width / 2, app.height / 2};
    check(!autoScrollStart(app, app.hwnd, at, ms += 100) && !autoScrollActive(app) && GetCapture() != app.hwnd,
          "a middle click on a page that fits starts nothing");
}

void setting() {
    Settings settings = loadSettings();
    check(settings.middleClickScroll && Settings().middleClickScroll && std::make_unique<App>()->middleClickScroll,
          "it is on unless settings.ini says otherwise");
    settings.middleClickScroll = false;
    saveSettings(settings);
    std::ifstream in(getSettingsPath(), std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(written.find("middleClickScroll=0") != std::string::npos, "settings.ini has the line middleClickScroll=0");
    check(!loadSettings().middleClickScroll, "and it is read back");
    settings.middleClickScroll = true;
    saveSettings(settings);
    check(loadSettings().middleClickScroll, "and so is the way back");
}

}  // namespace

int runAutoScrollTests(const char* file) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Middle-click scroll tests", WS_POPUP, 0, 0, 1050, 800, nullptr,
                               nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    const std::wstring path = fs::path(file).wstring();
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650})
            for (float scale : {1.0f, 1.5f}) pageScenario(app, path, width, scale);
    }
    applyTheme(app, 0);
    editorScenario(app, path);
    shortPage(app);
    setting();

    // What a step costs by itself: the frame it asks for is a scroll's
    open(app, path, true);
    const POINT at{app.width / 2, app.height / 2};
    if (middleClick(app, at)) {
        LARGE_INTEGER frequency, from, to;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&from);
        const int steps = 20000;
        for (int i = 0; i < steps; i++) {
            autoScrollPointer(app, app.hwnd, i % 2000 < 1000 ? below(app, at, 60) : above(app, at, 60));
            autoScrollStep(app, app.hwnd, ms += 16);
        }
        QueryPerformanceCounter(&to);
        std::cout << "A step: " << (double)(to.QuadPart - from.QuadPart) * 1.0e6 / (double)frequency.QuadPart / steps
                  << " microseconds\n";
        autoScrollStop(app, app.hwnd);
    }

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Middle-click scroll: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
