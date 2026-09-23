#include "sidepanels.h"
#include "overlays.h"
#include "settings.h"

namespace {
bool canResize(const App& app) {
    return !app.editMode && !app.showSettings && !app.showThemeEditor &&
        !app.showShortcutEditor && !app.showThemeChooser && !app.showHelp &&
        !app.showPrintPreview && !app.showLightbox && !app.showContextMenu &&
        !app.showTabMenu && !app.showTabSwitcher && !app.confirmExitPending &&
        !app.createRefPending && !app.annotEditorOpen && !app.showSearch &&
        !app.signalTrayOpen;
}
}

float sidePanelResizeEdge(const App& app, SidePanel panel) {
    if (panel == SidePanel::Browser)
        return folderBrowserPanelWidth(app) * app.folderBrowserAnimation;
    if (panel == SidePanel::Search)
        return (float)app.width -
               searchResultsPanelWidth(app) * app.searchResultsAnimation;
    const float width = tocPanelWidth(app);
    return tocPanelX(app, width) + (app.tocOnLeft ? width : 0);
}

SidePanel sidePanelResizeAt(const App& app, float x, float y) {
    if (y < chromeTopHeight(app) + dpi(app, 16.0f) ||
        y > app.height - dpi(app, 20.0f)) return SidePanel::None;
    // The search-results panel is resizable beside the search bar and in
    // edit mode: Ctrl+Shift+F opens the panel with the search bar up, and
    // the user balances the split against the source it lists.
    if (app.showSearchResults && app.searchResultsAnimation >= 1.0f &&
        std::abs(x - sidePanelResizeEdge(app, SidePanel::Search)) <= dpi(app, 5.0f)) {
        return SidePanel::Search;
    }
    if (canResize(app)) {
        for (SidePanel panel : {SidePanel::Contents, SidePanel::Browser}) {
            if (panel == SidePanel::Contents && (!app.showToc || app.tocAnimation < 1)) continue;
            if (panel == SidePanel::Browser && (!app.showFolderBrowser || app.folderBrowserAnimation < 1)) continue;
            if (std::abs(x - sidePanelResizeEdge(app, panel)) <= dpi(app, 5.0f)) return panel;
        }
    }
    return SidePanel::None;
}

bool sidePanelResizeBegin(App& app, HWND hwnd, float x, float y) {
    const SidePanel panel = sidePanelResizeAt(app, x, y);
    if (panel == SidePanel::None) return false;
    const auto widths = sidePanelWidths(app);
    const float scale = std::max(0.01f, app.contentScale);
    float startWidth = 0;
    switch (panel) {
        case SidePanel::Contents: startWidth = widths.toc / scale; break;
        case SidePanel::Browser: startWidth = widths.browser / scale; break;
        case SidePanel::Search: startWidth = widths.search / scale; break;
        default: break;
    }
    app.panelResize = {panel, x, startWidth, app.tocWidth, app.browserWidth,
                       app.searchResultsWidth};
    // Start from the widths the user can see, holding the neighbouring
    // panels steady throughout this explicit resize gesture.
    if (app.showToc) app.tocWidth = widths.toc / scale;
    if (app.showFolderBrowser) app.browserWidth = widths.browser / scale;
    if (app.showSearchResults) app.searchResultsWidth = widths.search / scale;
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
    const float sign = panel == SidePanel::Search ||
                              (panel == SidePanel::Contents && !app.tocOnLeft)
                          ? -1.0f : 1.0f;
    const auto widths = sidePanelWidths(app);
    float other = 0;
    float minimumBase = 200.0f;
    switch (panel) {
        case SidePanel::Search:
            other = widths.browser + widths.toc;
            minimumBase = 200.0f;
            break;
        case SidePanel::Contents:
            other = widths.browser;
            minimumBase = 180.0f;
            break;
        default:
            other = widths.toc;
            minimumBase = 200.0f;
            break;
    }
    const float maximum = std::max(0.0f, (sidePanelBudget(app) - other) / scale);
    const float minimum = std::min(maximum, minimumBase);
    const float wanted = app.panelResize.startWidth + sign * (x - app.panelResize.startX) / scale;
    const float width = std::clamp(wanted, minimum, std::min(4000.0f, std::max(minimum, maximum)));
    if (panel == SidePanel::Contents) app.tocWidth = width;
    else if (panel == SidePanel::Browser) app.browserWidth = width;
    else app.searchResultsWidth = width;
    app.layoutDirty = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void sidePanelResizeEnd(App& app, HWND hwnd, bool cancel) {
    if (app.panelResize.panel == SidePanel::None) return;
    if (cancel) {
        app.tocWidth = app.panelResize.oldTocWidth;
        app.browserWidth = app.panelResize.oldBrowserWidth;
        app.searchResultsWidth = app.panelResize.oldSearchResultsWidth;
    } else {
        app.tocWidth = std::clamp(app.tocWidth, 180.0f, 4000.0f);
        app.browserWidth = std::clamp(app.browserWidth, 200.0f, 4000.0f);
        app.searchResultsWidth = std::clamp(app.searchResultsWidth, 200.0f, 4000.0f);
        Settings settings = loadSettings();
        settings.tocWidth = app.tocWidth;
        settings.browserWidth = app.browserWidth;
        settings.searchResultsWidth = app.searchResultsWidth;
        saveSettings(settings);
    }
    app.panelResize.panel = SidePanel::None;  // clear before ReleaseCapture sends WM_CAPTURECHANGED
    app.swallowNextMouseUp = cancel;
    if (GetCapture() == hwnd) ReleaseCapture();
    app.layoutDirty = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void renderSidePanelResizeGrip(App& app, SidePanel panel) {
    if (panel == SidePanel::Search) {
        if (!app.showSearchResults || app.searchResultsAnimation < 1.0f) return;
    } else if (!canResize(app)) {
        return;
    }
    const float x = sidePanelResizeEdge(app, panel);
    const bool active = app.panelResize.panel == panel ||
        sidePanelResizeAt(app, static_cast<float>(app.mouseX), static_cast<float>(app.mouseY)) == panel;
    D2D1_COLOR_F color = active ? app.theme.accent : app.theme.text;
    color.a = active ? 0.55f : 0.10f;
    app.brush->SetColor(color);
    const float left = std::floor(x) - (panel == SidePanel::Browser || panel == SidePanel::Search || app.tocOnLeft ? 1.0f : 0.0f);
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
    if (app.editMode || (!app.showToc && !app.showFolderBrowser &&
                         !app.showSearchResults)) {
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
    if (app.editMode || app.showPrintPreview ||
        (!app.showToc && !app.showFolderBrowser && !app.showSearchResults)) return false;
    return app.panelScrollbarFade.running ||
        (app.lastPanelScrollActivity && now - app.lastPanelScrollActivity <= 800);
}
