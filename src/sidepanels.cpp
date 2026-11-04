#include "sidepanels.h"
#include "overlays.h"
#include "search_panel.h"
#include "settings.h"

#include <algorithm>

namespace {
bool canResize(const App& app) {
    return !app.editMode && !app.showSettings && !app.showThemeEditor &&
        !app.showShortcutEditor && !app.showThemeChooser && !app.showHelp &&
        !app.showPrintPreview && !app.showLightbox && !app.showContextMenu &&
        !app.showTabMenu && !app.showTabSwitcher && !app.confirmExitPending &&
        !app.createRefPending && !app.annotEditorOpen && !app.showSearch &&
        !app.signalTrayOpen;
}

// A side panel's list viewport and its right edge, without the content
// (the search results build their rows to measure it)
bool panelListFrame(const App& app, PanelList which, PanelListView& out) {
    if (app.editMode) return false;
    switch (which) {
        case PanelList::Contents: {
            if (!app.showToc || app.showSearchPanel || app.headings.empty()) return false;
            const float width = tocPanelWidth(app);
            out.list = tocListRect(app);
            out.cardRight = tocPanelX(app, width) + width;
            break;
        }
        case PanelList::Browser: {
            if (!app.showFolderBrowser) return false;
            const FolderBrowserMetrics g = folderBrowserMetrics(app);
            out.list = D2D1::RectF(g.cardLeft, g.listStartY, g.cardRight, g.listBottom);
            out.cardRight = g.cardRight;
            break;
        }
        case PanelList::Search:
            if (!app.showSearchPanel) return false;
            out.list = searchPanelListRect(app);
            out.cardRight = searchPanelCardRect(app).right;
            break;
        default:
            return false;
    }
    // Where the card's edge resizes the panel (the browser, a Contents
    // slot on the left) the thumb keeps clear of the resize band, as the
    // document's thumb does beside a divider
    const bool edgeResizes = which == PanelList::Browser || app.tocOnLeft;
    out.barRight = out.cardRight - dpi(app, edgeResizes ? 7.0f : 3.0f);
    return out.list.bottom > out.list.top;
}

float panelListContent(const App& app, PanelList which) {
    switch (which) {
        case PanelList::Contents: return tocListContentHeight(app);
        case PanelList::Browser: return folderBrowserContentHeight(app, folderBrowserMetrics(app));
        case PanelList::Search: return searchPanelContentHeight(app);
        default: return 0;
    }
}

float panelListScrollValue(const App& app, PanelList which) {
    switch (which) {
        case PanelList::Contents: return app.tocScroll;
        case PanelList::Browser: return app.folderBrowserScroll;
        case PanelList::Search: return app.searchPanelScroll;
        default: return 0;
    }
}
}

float sidePanelResizeEdge(const App& app, SidePanel panel) {
    if (panel == SidePanel::Browser)
        return folderBrowserPanelWidth(app) * app.folderBrowserAnimation;
    const float width = tocPanelWidth(app);
    return tocPanelX(app, width) + (app.tocOnLeft ? width : 0);
}

SidePanel sidePanelResizeAt(const App& app, float x, float y) {
    if (!canResize(app) || y < chromeTopHeight(app) + dpi(app, 16.0f) ||
        y > app.height - dpi(app, 20.0f)) return SidePanel::None;
    for (SidePanel panel : {SidePanel::Contents, SidePanel::Browser}) {
        if (panel == SidePanel::Contents && (!app.showToc || app.tocAnimation < 1)) continue;
        if (panel == SidePanel::Browser && (!app.showFolderBrowser || app.folderBrowserAnimation < 1)) continue;
        if (std::abs(x - sidePanelResizeEdge(app, panel)) <= dpi(app, 5.0f)) return panel;
    }
    return SidePanel::None;
}

bool sidePanelResizeBegin(App& app, HWND hwnd, float x, float y) {
    const SidePanel panel = sidePanelResizeAt(app, x, y);
    if (panel == SidePanel::None) return false;
    const auto widths = sidePanelWidths(app);
    const float scale = std::max(0.01f, app.contentScale);
    app.panelResize = {panel, x,
        (panel == SidePanel::Contents ? widths.toc : widths.browser) / scale,
        app.tocWidth, app.browserWidth};
    // Start from the widths the user can see, holding the neighbouring
    // panel steady throughout this explicit resize gesture.
    if (app.showToc) app.tocWidth = widths.toc / scale;
    if (app.showFolderBrowser) app.browserWidth = widths.browser / scale;
    app.selecting = app.mouseDown = false;
    app.swallowNextMouseUp = false;
    SetCapture(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void sidePanelResizeMove(App& app, float x) {
    const SidePanel panel = app.panelResize.panel;
    if (panel == SidePanel::None) return;
    const float scale = std::max(0.01f, app.contentScale);
    const float sign = panel == SidePanel::Contents && !app.tocOnLeft ? -1.0f : 1.0f;
    const auto widths = sidePanelWidths(app);
    const float other = panel == SidePanel::Contents ? widths.browser : widths.toc;
    const float maximum = std::max(0.0f, (sidePanelBudget(app) - other) / scale);
    const float minimum = std::min(maximum, panel == SidePanel::Contents ? 180.0f : 200.0f);
    const float wanted = app.panelResize.startWidth + sign * (x - app.panelResize.startX) / scale;
    const float width = std::clamp(wanted, minimum, std::min(4000.0f, std::max(minimum, maximum)));
    if (panel == SidePanel::Contents) app.tocWidth = width;
    else app.browserWidth = width;
    app.layoutDirty = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void sidePanelResizeEnd(App& app, HWND hwnd, bool cancel) {
    if (app.panelResize.panel == SidePanel::None) return;
    if (cancel) {
        app.tocWidth = app.panelResize.oldTocWidth;
        app.browserWidth = app.panelResize.oldBrowserWidth;
    } else {
        app.tocWidth = std::clamp(app.tocWidth, 180.0f, 4000.0f);
        app.browserWidth = std::clamp(app.browserWidth, 200.0f, 4000.0f);
        Settings settings = loadSettings();
        settings.tocWidth = app.tocWidth;
        settings.browserWidth = app.browserWidth;
        saveSettings(settings);
    }
    app.panelResize.panel = SidePanel::None;  // clear before ReleaseCapture sends WM_CAPTURECHANGED
    app.swallowNextMouseUp = cancel;
    if (GetCapture() == hwnd) ReleaseCapture();
    app.layoutDirty = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void renderSidePanelResizeGrip(App& app, SidePanel panel) {
    if (!canResize(app)) return;
    const float x = sidePanelResizeEdge(app, panel);
    const bool active = app.panelResize.panel == panel ||
        sidePanelResizeAt(app, static_cast<float>(app.mouseX), static_cast<float>(app.mouseY)) == panel;
    // The panel's edge: --ui-border when a CSS theme gives one
    D2D1_COLOR_F color = active ? themeUiAccent(app.theme) : themeUiText(app.theme);
    color.a = active ? 0.55f : 0.10f;
    if (!active) color = themeUiBorder(app.theme, color);
    app.brush->SetColor(color);
    const float left = std::floor(x) - (panel == SidePanel::Browser || app.tocOnLeft ? 1.0f : 0.0f);
    app.renderTarget->FillRectangle(
        D2D1::RectF(left, chromeTopHeight(app), left + 1, (float)app.height), app.brush);
}

bool documentScrollbarEdgeHovered(const App& app) {
    if (app.mouseY < chromeTopHeight(app) || app.mouseY > app.height ||
        app.panelResize.panel != SidePanel::None ||
        sidePanelResizeAt(app, (float)app.mouseX, (float)app.mouseY) != SidePanel::None) return false;
    const float left = documentViewportX(app);
    const float width = documentViewportWidth(app);
    const float right = left + width;
    const bool vertical = app.verticalScrollbarVisible && app.mouseX >= right - dpi(app, 14) && app.mouseX < right;
    const bool horizontal = app.contentWidth > width && app.mouseY >= app.height - dpi(app, 14) &&
                            app.mouseX >= left && app.mouseX < right;
    return vertical || horizontal;
}

float sidePanelDocumentScrollbarOpacity(App& app, ULONGLONG now) {
    auto& fade = app.panelScrollbarFade;
    if (app.editMode || (!contentsSlotOpen(app) && !app.showFolderBrowser)) {
        fade = {};
        return 1;
    }

    // Advance the current transition before retargeting. A new wheel event or
    // a quick pointer re-entry continues from the visible opacity, never a jump.
    if (fade.running) {
        const float duration = fade.target > fade.from ? 120.0f : 300.0f;
        const float t = std::min(1.0f, (float)(now - fade.started) / duration);
        const float eased = t * t * (3.0f - 2.0f * t);
        fade.opacity = fade.from + (fade.target - fade.from) * eased;
        if (t >= 1) fade.running = false;
    }
    const bool recentScroll = app.lastPanelScrollActivity && now - app.lastPanelScrollActivity <= 800;
    const bool visible = app.panelResize.panel == SidePanel::None &&
        (recentScroll || app.scrollbarDragging || app.hScrollbarDragging ||
         documentScrollbarEdgeHovered(app) || app.showSearch);
    const float target = visible ? 1.0f : 0.0f;
    if (target != fade.target) {
        fade.from = fade.opacity;
        fade.target = target;
        fade.started = now;
        fade.running = fade.from != target;
    }
    return fade.opacity;
}

bool sidePanelScrollbarNeedsTicks(const App& app, ULONGLONG now) {
    if (app.editMode || app.showPrintPreview || (!contentsSlotOpen(app) && !app.showFolderBrowser)) return false;
    return app.panelScrollbarFade.running ||
        (app.lastPanelScrollActivity && now - app.lastPanelScrollActivity <= 800);
}

float ScrollbarGeometry::scrollForThumb(float thumb) const {
    const float travel = trackLength - thumbLength;
    if (travel <= 0 || maxScroll <= 0) return 0;
    return std::clamp((thumb - trackStart) / travel, 0.0f, 1.0f) * maxScroll;
}

ScrollbarGeometry documentScrollbarV(const App& app) {
    ScrollbarGeometry bar;
    const float height = static_cast<float>(app.height);
    // While a layout is still growing, the bar's extent only grows with it;
    // each frame latches it from contentHeight before drawing
    const float extent = std::max({height, app.scrollbarContentHeight, app.contentHeight});
    const bool horizontal = app.contentWidth > documentViewportWidth(app);
    bar.maxScroll = std::max(0.0f, extent - height);
    bar.trackStart = chromeTopHeight(app);
    bar.trackLength = std::max(0.0f, height - bar.trackStart - (horizontal ? dpi(app, 14.0f) : 0.0f));
    bar.thumbLength = std::max(extent > 0 ? bar.trackLength / extent * bar.trackLength : 0.0f, dpi(app, 30.0f));
    bar.thumbStart = bar.trackStart;
    if (bar.maxScroll > 0)
        bar.thumbStart += std::clamp(app.scrollY / bar.maxScroll, 0.0f, 1.0f) * (bar.trackLength - bar.thumbLength);
    return bar;
}

ScrollbarGeometry documentScrollbarH(const App& app) {
    ScrollbarGeometry bar;
    const float width = documentViewportWidth(app);
    bar.maxScroll = std::max(0.0f, app.contentWidth - width);
    bar.trackStart = documentViewportX(app);
    bar.trackLength = std::max(0.0f, width - (app.verticalScrollbarVisible ? dpi(app, 14.0f) : 0.0f));
    bar.thumbLength = std::max(app.contentWidth > 0 ? bar.trackLength / app.contentWidth * bar.trackLength : 0.0f,
                               dpi(app, 30.0f));
    bar.thumbStart = bar.trackStart;
    if (bar.maxScroll > 0)
        bar.thumbStart += std::clamp(app.scrollX / bar.maxScroll, 0.0f, 1.0f) * (bar.trackLength - bar.thumbLength);
    return bar;
}

ScrollbarGeometry editorScrollbarV(const App& app) {
    ScrollbarGeometry bar;
    const float height = static_cast<float>(app.height);
    const float extent = std::max(height, app.editorContentHeight);
    bar.maxScroll = std::max(0.0f, extent - height);
    bar.trackStart = chromeTopHeight(app);
    bar.trackLength = std::max(0.0f, height - bar.trackStart);
    bar.thumbLength = std::max(extent > 0 ? bar.trackLength / extent * bar.trackLength : 0.0f, dpi(app, 30.0f));
    bar.thumbStart = bar.trackStart;
    if (bar.maxScroll > 0)
        bar.thumbStart += std::clamp(app.editorScrollY / bar.maxScroll, 0.0f, 1.0f) * (bar.trackLength - bar.thumbLength);
    return bar;
}

bool panelListView(const App& app, PanelList which, PanelListView& out) {
    if (!panelListFrame(app, which, out)) return false;
    out.contentHeight = panelListContent(app, which);
    out.scroll = panelListScrollValue(app, which);
    return true;
}

ScrollbarGeometry panelListScrollbar(const App& app, const PanelListView& view) {
    ScrollbarGeometry bar;
    bar.trackStart = view.list.top;
    bar.trackLength = std::max(0.0f, view.list.bottom - view.list.top);
    bar.maxScroll = std::max(0.0f, view.contentHeight - bar.trackLength);
    if (view.contentHeight > 0)
        bar.thumbLength = std::max(bar.trackLength / view.contentHeight * bar.trackLength, dpi(app, 20.0f));
    bar.thumbStart = bar.trackStart;
    if (bar.maxScroll > 0)
        bar.thumbStart += std::clamp(view.scroll / bar.maxScroll, 0.0f, 1.0f) * (bar.trackLength - bar.thumbLength);
    return bar;
}

PanelList panelScrollbarAt(const App& app, float x, float y) {
    if (sidePanelResizeAt(app, x, y) != SidePanel::None) return PanelList::None;
    for (PanelList which : {PanelList::Search, PanelList::Contents, PanelList::Browser}) {
        PanelListView view;
        if (!panelListFrame(app, which, view) || x < view.barRight - dpi(app, 11.0f) ||
            x > view.cardRight || y < view.list.top || y > view.list.bottom) continue;
        // Only a list that outgrows its room has a bar
        if (panelListContent(app, which) > view.list.bottom - view.list.top) return which;
    }
    return PanelList::None;
}

bool panelScrollbarHot(const App& app, PanelList which) {
    if (app.panelScrollbarDrag.list != PanelList::None) return app.panelScrollbarDrag.list == which;
    return app.panelResize.panel == SidePanel::None &&
           panelScrollbarAt(app, static_cast<float>(app.mouseX), static_cast<float>(app.mouseY)) == which;
}

void setPanelListScroll(App& app, PanelList which, float scroll) {
    switch (which) {
        case PanelList::Contents: app.tocScroll = scroll; break;
        case PanelList::Browser: app.folderBrowserScroll = scroll; break;
        case PanelList::Search: app.searchPanelScroll = scroll; break;
        default: break;
    }
}

bool panelScrollbarBegin(App& app, HWND hwnd, float x, float y) {
    const PanelList which = panelScrollbarAt(app, x, y);
    PanelListView view;
    if (which == PanelList::None || !panelListView(app, which, view)) return false;
    const ScrollbarGeometry bar = panelListScrollbar(app, view);
    float grab = y - bar.thumbStart;
    if (grab < 0 || grab > bar.thumbLength) {
        // A press on the track centres the thumb there, and drags on
        grab = bar.thumbLength * 0.5f;
        setPanelListScroll(app, which, bar.scrollForThumb(y - grab));
    }
    app.panelScrollbarDrag = {which, grab};
    app.selecting = app.mouseDown = false;
    SetCapture(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool panelScrollbarMove(App& app, float y) {
    const PanelList which = app.panelScrollbarDrag.list;
    if (which == PanelList::None) return false;
    PanelListView view;
    // A panel that closed under the drag holds still until the release
    if (!panelListView(app, which, view)) return true;
    const ScrollbarGeometry bar = panelListScrollbar(app, view);
    setPanelListScroll(app, which, bar.scrollForThumb(y - app.panelScrollbarDrag.grab));
    InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
}

void panelScrollbarEnd(App& app, HWND hwnd, bool cancel) {
    if (app.panelScrollbarDrag.list == PanelList::None) return;
    app.panelScrollbarDrag = {};  // clear before ReleaseCapture sends WM_CAPTURECHANGED
    app.swallowNextMouseUp = cancel;
    if (GetCapture() == hwnd) ReleaseCapture();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void renderPanelScrollbar(App& app, PanelList which, float alpha) {
    PanelListView view;
    if (!panelListView(app, which, view) || view.contentHeight <= view.list.bottom - view.list.top) return;
    const ScrollbarGeometry bar = panelListScrollbar(app, view);
    // Under the pointer and while dragged the thumb widens and darkens
    const bool hot = panelScrollbarHot(app, which);
    D2D1_COLOR_F color = themeUiText(app.theme);
    color.a = (hot ? 0.45f : 0.25f) * alpha;
    app.brush->SetColor(color);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(view.barRight - dpi(app, hot ? 5.0f : 3.0f), bar.thumbStart,
                                      view.barRight, bar.thumbStart + bar.thumbLength),
                          1.5f, 1.5f),
        app.brush);
}
