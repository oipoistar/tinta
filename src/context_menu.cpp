#include "overlays.h"

#include <algorithm>

const std::vector<ContextMenuEntry>& contextMenuEntries(const App& app) {
    static const std::vector<ContextMenuEntry> document = {
        {CTX_COPY, "ctx.copy", L"Ctrl+C", false},
        {CTX_SELECT_ALL, "ctx.select_all", L"Ctrl+A", false},
        {CTX_ANNOTATE, "ctx.annotate", L"", true},
        {CTX_NEW, "ctx.new", L"", false},
        {CTX_PRINT, "ctx.print", L"Ctrl+P", false},
        {CTX_EXPORT, "ctx.export", L"", false},
        {CTX_EDIT, "ctx.edit", L"", false},
        {CTX_SEARCH, "ctx.search", L"", false},
        {CTX_TOC, "ctx.toc", L"", true},
        {CTX_BROWSE, "ctx.browse", L"", false},
        {CTX_COPY_PATH, "tab.menu.copy_path", L"", false},
        {CTX_REVEAL, "ctx.reveal", L"", true},
        {CTX_THEME, "ctx.theme", L"", false},
        {CTX_SETTINGS, "ctx.settings", L"Ctrl+,", false},
        {CTX_HELP, "ctx.help", L"", false},
    };
    static const std::vector<ContextMenuEntry> application = {
        {CTX_QUICK_NOTE, "ctx.new", L"Ctrl+N", false},
        {CTX_OPEN, "ctx.open", L"Ctrl+O", false},
        {CTX_SAVE, "ctx.save", L"Ctrl+S", false},
        {CTX_SAVE_AS, "ctx.save_as", L"Ctrl+Shift+S", true},
        {CTX_PRINT, "ctx.print", L"Ctrl+P", false},
        {CTX_EXPORT, "ctx.export", L"", true},
        {CTX_THEME, "ctx.theme", L"", false},
        {CTX_SETTINGS, "ctx.settings", L"Ctrl+,", false},
        {CTX_HELP, "ctx.help", L"", true},
        {CTX_EXIT, "ctx.exit", L"Alt+F4", false},
    };
    return app.applicationMenu ? application : document;
}

float contextMenuPadding(const App& app) { return dpi(app, 6.0f); }
float contextMenuSeparatorHeight(const App& app) { return dpi(app, 9.0f); }
float contextMenuWidth(const App& app) {
    return std::min(dpi(app, app.applicationMenu ? 260.0f : 210.0f), (float)app.width);
}

// Keep every action reachable on short windows and at high display scaling.
float contextMenuItemHeight(const App& app) {
    const auto& entries = contextMenuEntries(app);
    float gaps = contextMenuPadding(app) * 2;
    for (const auto& entry : entries)
        if (entry.separatorAfter) gaps += contextMenuSeparatorHeight(app);
    float available = app.height - chromeTopHeight(app) - gaps;
    return std::max(1.0f, std::min(dpi(app, 30.0f), available / (float)entries.size()));
}

float contextMenuHeight(const App& app) {
    float h = contextMenuPadding(app) * 2;
    for (const auto& entry : contextMenuEntries(app)) {
        h += contextMenuItemHeight(app);
        if (entry.separatorAfter) h += contextMenuSeparatorHeight(app);
    }
    return h;
}

float contextMenuItemTop(const App& app, int row) {
    float y = contextMenuPadding(app);
    const auto& entries = contextMenuEntries(app);
    for (int i = 0; i < row; i++) {
        y += contextMenuItemHeight(app);
        if (entries[i].separatorAfter) y += contextMenuSeparatorHeight(app);
    }
    return y;
}

bool contextMenuItemEnabled(const App& app, int item) {
    bool document = app.editMode || !app.currentFile.empty() || app.startPageEmbeddedOpen;
    switch (item) {
        case CTX_COPY: return app.hasSelection && app.selAnchor != app.selFocus;
        case CTX_ANNOTATE:
            return !app.editMode && !app.currentFile.empty() &&
                   app.hasSelection && app.selAnchor != app.selFocus;
        case CTX_SAVE: return app.editMode;
        case CTX_SAVE_AS: return app.editMode || !app.currentFile.empty();
        case CTX_PRINT: case CTX_EXPORT: case CTX_EDIT:
        case CTX_SELECT_ALL: case CTX_SEARCH: case CTX_TOC:
            return document;
        case CTX_COPY_PATH: case CTX_REVEAL: return !app.currentFile.empty();
        default: return item >= 0 && item < CTX_ITEM_COUNT;
    }
}

int contextMenuItemAt(const App& app, float x, float y) {
    if (x < app.contextMenuX || x >= app.contextMenuX + contextMenuWidth(app)) return -1;
    const auto& entries = contextMenuEntries(app);
    for (int row = 0; row < (int)entries.size(); row++) {
        float top = app.contextMenuY + contextMenuItemTop(app, row);
        if (y >= top && y < top + contextMenuItemHeight(app)) return entries[row].action;
    }
    return -1;
}

int nextContextMenuItem(const App& app, int current, int direction) {
    const auto& entries = contextMenuEntries(app);
    int count = (int)entries.size();
    int row = direction > 0 ? -1 : 0;
    for (int i = 0; i < count; i++) if (entries[i].action == current) row = i;
    for (int i = 0; i < count; i++) {
        row = (row + direction + count) % count;
        if (contextMenuItemEnabled(app, entries[row].action)) return entries[row].action;
    }
    return -1;
}

void closeContextMenu(App& app) {
    app.showContextMenu = false;
    app.contextMenuAnimation = 0;
    app.hoveredContextMenuItem = -1;
    app.contextMenuKeyboard = false;
}

bool updateContextMenuHover(App& app, float x, float y, bool mouseMoved) {
    // Repainting/activation can resend WM_MOUSEMOVE at the same position.
    if (app.contextMenuKeyboard && !mouseMoved) return false;
    int item = contextMenuItemAt(app, x, y);
    bool changed = app.contextMenuKeyboard || item != app.hoveredContextMenuItem;
    app.contextMenuKeyboard = false;
    app.hoveredContextMenuItem = item;
    return changed;
}

void openContextMenu(App& app, float x, float y, bool application) {
    app.applicationMenu = application;
    app.contextMenuX = std::max(0.0f, std::min(x, app.width - contextMenuWidth(app)));
    app.contextMenuY = std::max(chromeTopHeight(app),
                              std::min(y, app.height - contextMenuHeight(app)));
    app.showContextMenu = true;
    app.hoveredContextMenuItem = -1;
    app.contextMenuKeyboard = false;
    app.contextMenuAnimation = 0;
    app.hoveredCodeBlock = -1;
    app.hoveredLink.clear();
}

D2D1_RECT_F appMenuButtonRect(const App& app) {
    return D2D1::RectF(0, 0, dpi(app, app.editMode ? 48.0f : 40.0f), chromeTopHeight(app));
}

bool appMenuButtonAt(const App& app, float x, float y) {
    auto r = appMenuButtonRect(app);
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

bool appMenuAvailable(const App& app) {
    return !app.zenMode && !app.confirmExitPending && !app.createRefPending &&
           !app.showPrintPreview && !app.showLightbox && !app.annotEditorOpen &&
           !app.showThemeEditor && !app.showShortcutEditor;
}
