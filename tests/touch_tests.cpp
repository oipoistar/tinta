// #286: on a touchscreen a finger pans what it lands on instead of
// selecting text, a flick carries on, a tap clicks, a double tap selects a
// word, and a held press selects and then opens the context menu. Touches
// the window leaves to Windows' mouse emulation and the mouse itself are
// unchanged.
#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "input.h"
#include "overlays.h"
#include "render.h"
#include "selection.h"
#include "settings.h"
#include "sidepanels.h"
#include "touch.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { if (failures < 60) std::cerr << "FAIL touch: " << message << '\n'; ++failures; }
}

double ms = 1000.0;  // the gestures' own clock

// The middle of a word on screen at the current scroll
POINT wordPoint(App& app, const std::wstring& word) {
    POINT pt{-100, -100};
    const size_t at = app.docText.find(word);
    std::vector<D2D1_RECT_F> rects;
    if (at != std::wstring::npos) selectionHighlightRects(app, at, at + word.size(), rects);
    if (rects.empty()) { check(false, "the fixture shows the word"); return pt; }
    pt.x = (LONG)((rects[0].left + rects[0].right) / 2 + documentViewportX(app) - app.scrollX);
    pt.y = (LONG)((rects[0].top + rects[0].bottom) / 2 - app.scrollY);
    return pt;
}

std::wstring selected(const App& app) {
    if (!app.hasSelection) return L"";
    return selectionTextForRange(app, std::min(app.selAnchor, app.selFocus),
                                 std::max(app.selAnchor, app.selFocus));
}

POINT shifted(POINT p, int dx, int dy) { return POINT{p.x + dx, p.y + dy}; }

// A finger from a point along a straight line: steps of (dx, dy) every
// `every` ms, then a rest of `rest` ms before it lifts
void swipe(App& app, UINT32 id, POINT from, int dx, int dy, int steps, double every, double rest) {
    check(touchDown(app, app.hwnd, id, from, ms), "the finger is taken");
    for (int i = 1; i <= steps; i++)
        touchMove(app, app.hwnd, id, shifted(from, dx * i, dy * i), ms += every);
    touchUp(app, app.hwnd, id, shifted(from, dx * steps, dy * steps), ms += rest, false);
}

void tapAt(App& app, UINT32 id, POINT at) {
    check(touchDown(app, app.hwnd, id, at, ms += 400), "the tap is taken");
    touchUp(app, app.hwnd, id, at, ms += 60, false);
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

void controlRoutingScenario(App& app) {
    PanelList list;
    app.showSearch = app.searchReplaceMode = true;
    for (float animation : {0.4f, 1.0f}) {
        app.searchAnimation = animation;
        app.renderTarget->BeginDraw();
        renderSearchOverlay(app);
        check(SUCCEEDED(app.renderTarget->EndDraw()), "replace bar paints");
        check(!app.searchReplaceHits.empty(), "replace controls have painted hit regions");
        for (const auto& hit : app.searchReplaceHits) {
            const auto& r = hit.first;
            POINT p{(LONG)((r.left + r.right) / 2), (LONG)((r.top + r.bottom) / 2)};
            check(touchTargetAt(app, p, list) == TouchTarget::None,
                  "every painted replace control keeps mouse emulation, including stacked buttons");
        }
    }
    app.showSearch = app.searchReplaceMode = false;
    const POINT preview{(LONG)(documentViewportX(app) + dpi(app, 12)), app.height / 2};
    openEditCtxMenu(app, app.hwnd, (float)preview.x, (float)preview.y);
    app.renderTarget->BeginDraw();
    renderEditCtxMenu(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "editor menu paints across the pane boundary");
    POINT submenu{-1, -1};
    for (const auto& hit : app.editCtxHits) {
        if (hit.second == 110) submenu = {(LONG)((hit.first.left + hit.first.right) / 2),
                                        (LONG)((hit.first.top + hit.first.bottom) / 2)};
    }
    check(submenu.x >= documentViewportX(app), "the table menu entry is over the preview");
    check(!touchDown(app, app.hwnd, 60, submenu, ms += 1000), "an editor menu touch goes to mouse emulation");
    const auto source = app.editorText;
    const auto caret = app.editorCursorPos;
    handleMouseMove(app, app.hwnd, MAKELPARAM(submenu.x, submenu.y));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM(submenu.x, submenu.y));
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM(submenu.x, submenu.y));
    check(app.editCtxOpen && app.editCtxSub == 1 && app.editorText == source && app.editorCursorPos == caret,
          "the menu opens its submenu over the preview without changing source or caret");
    const POINT outside{app.width - (LONG)dpi(app, 24), (LONG)(chromeTopHeight(app) + dpi(app, 20))};
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM(outside.x, outside.y));
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM(outside.x, outside.y));
    check(!app.editCtxOpen && !app.selecting && app.editorText == source && app.editorCursorPos == caret,
          "dismissing the editor menu consumes the click over the preview");
}

void editorHoldScenario(App& app) {
    app.editorScrollY = 0;
    editFrame(app);
    const auto original = app.editorText;
    const bool dirty = app.editorDirty;
    auto point = [&](const std::wstring& word) {
        const size_t at = app.editorText.find(word);
        check(at != std::wstring::npos, "editor hold fixture word exists");
        app.editorCursorPos = at + 1;
        D2D1_POINT_2F p{};
        check(editorCaretPoint(app, p), "editor hold word has a screen position");
        return POINT{(LONG)(p.x + dpi(app, 1)), (LONG)(p.y + dpi(app, 6))};
    };
    const POINT alpha = point(L"alpha"), beta = point(L"beta"), gamma = point(L"gamma");
    const POINT chinese = point(L"\u4e2d\u6587\u9009\u62e9\u6d4b\u8bd5");
    auto selection = [&]() {
        return app.editorText.substr(app.editorSelStart, app.editorSelEnd - app.editorSelStart);
    };
    check(touchDown(app, app.hwnd, 50, beta, ms += 1000), "editor held press is taken");
    touchHold(app, app.hwnd);
    check(selection() == L"beta" && !app.editorTextDrag.armed, "editor hold selects a word without arming text drag");
    touchMove(app, app.hwnd, 50, gamma, ms += 100);
    check(selection() == L"beta gamma", "editor hold grows forward by whole words");
    touchMove(app, app.hwnd, 50, alpha, ms += 100);
    check(selection() == L"alpha beta", "editor hold reverses around its original word");
    touchMove(app, app.hwnd, 50, chinese, ms += 100);
    check(selection() == L"beta gamma\n\u4e2d\u6587\u9009\u62e9\u6d4b\u8bd5", "editor hold extends across lines and CJK runs");
    const auto grown = selection();
    touchUp(app, app.hwnd, 50, chinese, ms += 50, false);
    check(app.editCtxOpen && selection() == grown && !app.editorSelecting,
          "editor lift opens its menu and preserves the expanded selection");
    closeEditCtxMenu(app);
    check(touchDown(app, app.hwnd, 51, beta, ms += 1000), "a hold on selected source is taken");
    touchHold(app, app.hwnd);
    touchCancel(app, app.hwnd);
    check(!app.editCtxOpen && selection() == L"beta" && !app.editorTextDrag.armed,
          "canceling an editor hold keeps its word without moving text or opening a menu");
    check(app.editorText == original && app.editorDirty == dirty, "touch selection never edits the source");
}

void scenario(App& app, const char* path, int width, float scale) {
    const std::string where = " (" + std::to_string(width) + ", " + std::to_string(scale).substr(0, 3) + "x)";
    app.contentScale = scale;
    app.width = (int)(width * scale);
    app.height = (int)(800 * scale);
    updateTextFormats(app);
    check(openDocumentInViewer(app, toWide(path)), "the fixture opens" + where);
    ensureLayoutComplete(app);
    app.verticalScrollbarVisible = app.contentHeight > app.height;
    app.scrollX = app.targetScrollX = app.scrollY = app.targetScrollY = 0;
    app.hasSelection = app.selecting = false;
    const float maxY = app.contentHeight - app.height;
    check(maxY > 2000, "the page is several screens long" + where);
    PanelList list = PanelList::None;

    // What a finger drives where it lands
    const POINT middle{app.width / 2, app.height / 2};
    check(touchTargetAt(app, middle, list) == TouchTarget::Document, "a finger on the page drives the page" + where);
    check(touchTargetAt(app, POINT{app.width / 2, (LONG)(chromeTopHeight(app) / 2)}, list) == TouchTarget::None &&
              touchTargetAt(app, POINT{app.width - (LONG)dpi(app, 4), app.height / 2}, list) == TouchTarget::None,
          "the tab strip and the scrollbar keep Windows' mouse emulation" + where);
    app.showSettings = true;
    check(touchTargetAt(app, middle, list) == TouchTarget::None, "an open overlay keeps its mouse emulation" + where);
    app.showSettings = false;
    const POINT strip{app.width / 2, (LONG)(chromeTopHeight(app) / 2)};
    check(!touchDown(app, app.hwnd, 90, strip, ms) && !touchMove(app, app.hwnd, 90, shifted(strip, 40, 0), ms + 16) &&
              !touchUp(app, app.hwnd, 90, shifted(strip, 40, 0), ms + 32, false),
          "a touch on the tab strip goes on to Windows" + where);

    // A pan: within the slop nothing moves, then the page follows the
    // finger and nothing is selected; resting before lifting, no fling
    const POINT low{app.width / 2, app.height * 3 / 4};
    check(touchDown(app, app.hwnd, 1, low, ms += 500), "the pan's finger is taken" + where);
    touchMove(app, app.hwnd, 1, shifted(low, 0, -(int)dpi(app, 6)), ms += 16);
    check(app.scrollY == 0 && !app.selecting && !app.hasSelection, "a finger within a few pixels moves nothing" + where);
    for (int i = 1; i <= 15; i++) touchMove(app, app.hwnd, 1, shifted(low, 0, -20 * i), ms += 16);
    check(std::fabs(app.scrollY - 300) < 1, "the page moves with the finger" + where + ": " + std::to_string(app.scrollY));
    check(!app.selecting && !app.hasSelection, "a finger's drag selects nothing" + where);
    touchUp(app, app.hwnd, 1, shifted(low, 0, -300), ms += 200, false);
    check(!app.touch.flinging && std::fabs(app.scrollY - 300) < 1,
          "a finger that rests before lifting leaves the page there" + where);

    // A flick at 3 px/ms carries on about 3 x 325 px, slowing every step
    app.scrollY = app.targetScrollY = 0;
    swipe(app, 2, low, 0, -30, 8, 10, 5);
    check(app.touch.flinging && std::fabs(app.scrollY - 240) < 1, "a flick goes on after the finger lifts" + where);
    const float released = app.scrollY;
    float lastStep = 1e9f;
    bool slowing = true;
    int steps = 0;
    while (app.touch.flinging && steps < 1000) {
        const float before = app.scrollY;
        touchFlingStep(app, app.hwnd, ms += 16);
        const float step = app.scrollY - before;
        if (step > lastStep + 0.01f) slowing = false;
        lastStep = step;
        steps++;
    }
    const float carried = app.scrollY - released;
    check(!app.touch.flinging && slowing && carried > 3 * 325 * 0.9f && carried < 3 * 325 * 1.01f,
          "the fling slows step by step and stops, about speed x 325 ms on" + where + ": " +
              std::to_string(carried));
    // ... and stops at the page's end; a new touch stops it at once
    app.scrollY = app.targetScrollY = maxY - 100;
    swipe(app, 3, low, 0, -30, 8, 10, 5);
    for (int i = 0; i < 200 && app.touch.flinging; i++) touchFlingStep(app, app.hwnd, ms += 16);
    check(!app.touch.flinging && std::fabs(app.scrollY - maxY) < 0.5f, "a fling stops at the page's end" + where);
    app.scrollY = app.targetScrollY = 0;
    swipe(app, 4, low, 0, -30, 8, 10, 5);
    const float caught = app.scrollY;
    check(app.touch.flinging, "a second flick flings");
    touchDown(app, app.hwnd, 5, middle, ms += 16);
    touchFlingStep(app, app.hwnd, ms += 16);
    check(!app.touch.flinging && app.scrollY == caught, "a finger coming down stops the fling" + where);
    touchUp(app, app.hwnd, 5, middle, ms += 300, true);

    // A tap follows a link, and leaves no hover behind
    app.scrollY = app.targetScrollY = 0;
    swipe(app, 52, low, 0, -10, 2, 10, 5);
    check(app.touch.flinging, "the page is flinging before a braking tap");
    const POINT brakeLink = wordPoint(app, L"jump to the end");
    const float brakeY = app.scrollY;
    touchDown(app, app.hwnd, 53, brakeLink, ms += 16);
    touchHold(app, app.hwnd); // even a queued timer must not select
    touchUp(app, app.hwnd, 53, brakeLink, ms += 600, false);
    check(!app.touch.flinging && app.scrollY == brakeY && !app.hasSelection && !app.showContextMenu,
          "catching a fling over a link only stops it, even if held");
    tapAt(app, 54, brakeLink);
    check(app.scrollY > maxY - dpi(app, 400), "the next deliberate tap follows the link");
    app.scrollY = app.targetScrollY = 0;
    swipe(app, 55, low, 0, -20, 4, 10, 5);
    const float beforeDrag = app.scrollY;
    swipe(app, 56, middle, 0, -20, 4, 16, 200);
    check(!app.touch.flinging && std::fabs(app.scrollY - beforeDrag - 80) < 1,
          "a braking press can continue directly into another pan");
    app.scrollY = app.targetScrollY = 0;
    tapAt(app, 6, wordPoint(app, L"jump to the end"));
    check(app.scrollY > maxY - dpi(app, 400) && !app.hasSelection && app.mouseX == -1 && app.hoveredLink.empty(),
          "a tap follows a link" + where + ": " + std::to_string(app.scrollY));

    // A double tap selects a word, the second tap a little off the first
    app.scrollY = app.targetScrollY = 0;
    const POINT marigold = wordPoint(app, L"marigold");
    tapAt(app, 7, marigold);
    check(!app.hasSelection, "one tap on a word selects nothing" + where);
    check(touchDown(app, app.hwnd, 8, shifted(marigold, 6, 5), ms += 150), "the second tap is taken");
    touchUp(app, app.hwnd, 8, shifted(marigold, 6, 5), ms += 60, false);
    check(selected(app) == L"marigold", "a double tap selects the word" + where);
    check(touchDown(app, app.hwnd, 20, shifted(marigold, -4, 3), ms += 150), "the third tap is taken");
    touchUp(app, app.hwnd, 20, shifted(marigold, -4, 3), ms += 60, false);
    const std::wstring line = selected(app);
    check(app.selectionMode == App::SelectionMode::Line && line.size() > 20 &&
              line.find(L"marigold") != std::wstring::npos,
          "a third tap selects the line" + where);
    tapAt(app, 9, middle);
    check(!app.hasSelection, "a tap elsewhere puts the selection away" + where);

    // A held press selects the word, sliding on extends it, and the menu
    // opens where the finger lifts
    const POINT lavender = wordPoint(app, L"lavender"), rosemary = wordPoint(app, L"rosemary");
    check(touchDown(app, app.hwnd, 10, lavender, ms += 1000), "the held finger is taken");
    touchHold(app, app.hwnd);
    check(app.selecting && selected(app) == L"lavender", "a held press selects the word under it" + where);
    touchMove(app, app.hwnd, 10, shifted(lavender, 3, 2), ms += 100);
    check(selected(app) == L"lavender", "a held finger's tremor keeps the word" + where);
    touchMove(app, app.hwnd, 10, rosemary, ms += 200);
    const std::wstring grown = selected(app);
    check(grown.rfind(L"lavender", 0) == 0 && grown.size() > 16 &&
              grown.compare(grown.size() - 8, 8, L"rosemary") == 0 && app.scrollY == 0,
          "sliding on extends the selection word by word, without panning" + where);
    touchUp(app, app.hwnd, 10, rosemary, ms += 50, false);
    check(app.showContextMenu && !app.selecting && selected(app) == grown,
          "the context menu opens where the finger lifts, over the selection" + where);
    closeContextMenu(app);
    // Lost capture mid-hold keeps the word without the menu
    check(touchDown(app, app.hwnd, 11, marigold, ms += 1000), "another held finger is taken");
    touchHold(app, app.hwnd);
    touchCancel(app, app.hwnd);
    check(!app.selecting && !app.showContextMenu && selected(app) == L"marigold" && !app.touch.active,
          "a hold that loses its capture keeps the word, without the menu" + where);
    check(!touchUp(app, app.hwnd, 11, marigold, ms += 50, false), "its later lift is not taken");

    // A second finger: the first one's lift no longer taps
    app.scrollY = app.targetScrollY = 0;
    app.hasSelection = false;
    const POINT link = wordPoint(app, L"jump to the end");
    check(touchDown(app, app.hwnd, 12, link, ms += 1000) && touchDown(app, app.hwnd, 13, shifted(link, 0, 120), ms += 20),
          "a second finger is taken too");
    touchUp(app, app.hwnd, 13, shifted(link, 0, 120), ms += 40, false);
    touchUp(app, app.hwnd, 12, link, ms += 10, false);
    check(app.scrollY == 0, "with a second finger down, the first one's tap does not follow the link" + where);

    // Sideways: the wide table and code line pan across, held to that way
    const float across = app.contentWidth - documentViewportWidth(app);
    check(across > dpi(app, 100), "the wide table and code line give the page a sideways scroll" + where);
    swipe(app, 14, middle, -20, -2, 10, 16, 200);
    check(std::fabs(app.scrollX - std::min(200.0f, across)) < 1 && app.scrollY == 0,
          "a sideways pan moves the page across, and only across" + where);
    app.scrollX = app.targetScrollX = 0;

    // The Contents: the list pans under the finger, the page stays, and a
    // tap on a row opens its section
    app.showToc = true;
    app.tocAnimation = 1;
    app.tocScroll = 0;
    app.tocPinned = false;
    app.tocFilter.clear();
    app.tocSpyOverride = -1;
    const D2D1_RECT_F rows = tocListRect(app);
    const bool wasLeft = app.tocOnLeft;
    for (bool left : {false, true}) {
        app.tocOnLeft = left;
        const POINT edge{(LONG)sidePanelResizeEdge(app, SidePanel::Contents), app.height / 2};
        check(touchTargetAt(app, edge, list) == TouchTarget::None,
              "both Contents resize edges keep mouse emulation instead of panning the list");
    }
    app.tocOnLeft = wasLeft;
    const POINT inList{(LONG)(rows.left + dpi(app, 40)), (LONG)(rows.bottom - dpi(app, 60))};
    check(touchTargetAt(app, inList, list) == TouchTarget::List && list == PanelList::Contents,
          "a finger on the Contents drives its list" + where);
    swipe(app, 15, inList, 0, -20, 10, 16, 200);
    check(std::fabs(app.tocScroll - 200) < 1 && app.scrollY == 0, "the Contents list moves with the finger" + where);
    const POINT top{(LONG)(rows.left + dpi(app, 40)), (LONG)(rows.top + dpi(app, 13))};
    const int heading = tocItemIndexAt(app, (float)top.x, (float)top.y);
    tapAt(app, 16, top);
    check(heading > 0 && app.tocSpyOverride == heading && !app.showToc && app.scrollY > 0,
          "a tap on a Contents row opens its section" + where);
    app.showToc = false;

    // A mouse drag still selects
    app.scrollY = app.targetScrollY = 0;
    app.hasSelection = false;
    const POINT from = wordPoint(app, L"marigold"), to = wordPoint(app, L"rosemary");
    handleMouseMove(app, app.hwnd, MAKELPARAM(from.x, from.y));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM(from.x, from.y));
    handleMouseMove(app, app.hwnd, MAKELPARAM(to.x, to.y));
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM(to.x, to.y));
    check(app.hasSelection && selected(app).size() > 20 && app.scrollY == 0, "a mouse drag still selects text" + where);
    app.hasSelection = false;

    // Edit mode: the editor's text pans, a tap places the caret, and a pan
    // on the preview takes the editor along
    enterEditMode(app);
    app.editorShowPreview = true;
    app.editorScrollY = 0;
    editFrame(app);
    const float textX = editRailWidth(app) + (editorPaneWidth(app) - editRailWidth(app)) / 2;
    const POINT inEditor{(LONG)textX, app.height / 2};
    check(touchTargetAt(app, inEditor, list) == TouchTarget::Editor, "a finger on the editor's text drives the editor" + where);
    check(touchTargetAt(app, POINT{(LONG)(editorPaneWidth(app) - dpi(app, 6)), app.height / 2}, list) == TouchTarget::None,
          "the editor's scrollbar and seam keep the mouse emulation" + where);
    swipe(app, 17, inEditor, 0, -20, 15, 16, 200);
    check(std::fabs(app.editorScrollY - 300) < 1, "the editor's text moves with the finger" + where);
    const size_t caret = app.editorCursorPos;
    tapAt(app, 18, POINT{(LONG)textX, (LONG)(chromeTopHeight(app) + dpi(app, 80))});
    check(app.editorCursorPos != caret && !app.editorSelecting && !app.editorHasSelection,
          "a tap in the editor places the caret" + where);
    app.editorScrollY = 0;
    editFrame(app);
    const POINT inPreview{(LONG)(documentViewportX(app) + documentViewportWidth(app) / 2), app.height / 2};
    check(touchTargetAt(app, inPreview, list) == TouchTarget::Document, "a finger on the preview drives the page" + where);
    swipe(app, 19, inPreview, 0, -20, 15, 16, 200);
    editFrame(app);
    // The panes line up by source line (#280), so the page settles within
    // a line or so of where the finger took it
    check(app.editorScrollY > 0 && std::fabs(app.scrollY - 300) <= dpi(app, 90),
          "a pan on the preview takes the editor along" + where + ": page " + std::to_string(app.scrollY) +
              ", editor " + std::to_string(app.editorScrollY));
    // A preview link must survive the following frames' source-to-preview sync.
    app.editorScrollY = 0;
    editFrame(app);
    const auto sourceBeforeJump = app.editorText;
    const bool dirtyBeforeJump = app.editorDirty;
    const size_t caretBeforeJump = app.editorCursorPos;
    tapAt(app, 70, wordPoint(app, L"jump to the end"));
    check(app.editorScrollY > 0, "a preview anchor jump also moves the source viewport" + where);
    editFrame(app);
    editFrame(app);
    check(app.scrollY > app.height && app.editorScrollY > 0,
          "a preview anchor jump stays at its destination after repaint" + where);
    check(app.editorText == sourceBeforeJump && app.editorDirty == dirtyBeforeJump &&
              app.editorCursorPos == caretBeforeJump,
          "preview link navigation does not edit source or move its caret" + where);
    controlRoutingScenario(app);
    editorHoldScenario(app);
    exitEditMode(app);
}
}  // namespace

int runTouchTests(const char* path) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Touch tests", WS_POPUP, 0, 0, 1050, 800,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650})
            for (float scale : {1.0f, 1.5f}) scenario(app, path, width, scale);
    }
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Touch gestures: " << failures << " failures\n";
    return failures ? 1 : 0;
}
