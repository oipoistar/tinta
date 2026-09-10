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
    D2D1_COLOR_F color = active ? app.theme.accent : app.theme.text;
    color.a = active ? 0.8f : 0.2f;
    app.brush->SetColor(color);
    const float cy = (chromeTopHeight(app) + app.height) * 0.5f;
    app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(x - dpi(app, 1.5f), cy - dpi(app, 18.0f),
                   x + dpi(app, 1.5f), cy + dpi(app, 18.0f)),
        dpi(app, 1.5f), dpi(app, 1.5f)), app.brush);
}
