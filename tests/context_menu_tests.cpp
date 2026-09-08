#include "overlays.h"

#include <iostream>
#include <memory>
#include <set>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.width = 650;
    app.height = 600;
    app.contentScale = 1.0f;
    openContextMenu(app, 4, 40, true);
    check(!contextMenuItemEnabled(app, CTX_SAVE), "launcher cannot save a nonexistent document");
    check(!contextMenuItemEnabled(app, CTX_SAVE_AS), "launcher cannot save a copy");
    check(!contextMenuItemEnabled(app, CTX_PRINT), "launcher cannot print");
    check(!contextMenuItemEnabled(app, CTX_EXPORT), "launcher cannot export");
    check(nextContextMenuItem(app, CTX_OPEN, 1) == CTX_THEME,
          "keyboard skips all unavailable document commands");
    check(nextContextMenuItem(app, CTX_QUICK_NOTE, -1) == CTX_EXIT,
          "keyboard wraps to the last action");
    app.contextMenuKeyboard = true;
    app.hoveredContextMenuItem = CTX_EXIT;
    float firstRowY = app.contextMenuY + contextMenuPadding(app) + 5;
    check(!updateContextMenuHover(app, 20, firstRowY, false) &&
          app.hoveredContextMenuItem == CTX_EXIT,
          "synthetic mouse moves cannot overwrite keyboard selection");
    check(updateContextMenuHover(app, 20, firstRowY, true) &&
          app.hoveredContextMenuItem == CTX_QUICK_NOTE && !app.contextMenuKeyboard,
          "a physical pointer move resumes mouse selection");
    app.currentFile = "fixture.md";
    check(contextMenuItemEnabled(app, CTX_SAVE_AS), "reading mode can save a copy");
    check(!contextMenuItemEnabled(app, CTX_SAVE), "reading mode cannot overwrite from a stale editor buffer");
    app.currentFile.clear();
    app.editMode = true;
    app.editorText = L"unsaved text";
    app.editorDirty = true;
    check(contextMenuItemEnabled(app, CTX_SAVE) && contextMenuItemEnabled(app, CTX_SAVE_AS),
          "untitled editor can save and save as");
    check(contextMenuItemEnabled(app, CTX_PRINT) && contextMenuItemEnabled(app, CTX_EXPORT),
          "unsaved editor content can print and export");
    check(appMenuButtonAt(app, 46, 20), "editor logo uses the full rail width");
    check(!appMenuButtonAt(app, 55, 20), "tabs are outside the logo button");
    app.editMode = false;
    check(!appMenuButtonAt(app, 46, 20), "reading logo does not steal title dragging");
    app.confirmExitPending = true;
    check(!appMenuAvailable(app), "menu cannot bypass the unsaved-changes dialog");
    app.confirmExitPending = false;
    app.showThemeEditor = true;
    check(!appMenuAvailable(app), "menu cannot discard a custom theme in progress");
    app.showThemeEditor = false;
    app.zenMode = true;
    check(!appMenuButtonAt(app, 20, 20), "zen mode has no invisible logo hit target");
    app.zenMode = false;

    for (float scale : {1.0f, 1.5f, 2.0f}) {
        app.contentScale = scale;
        for (bool application : {false, true}) {
            openContextMenu(app, 900, 800, application);
            check(app.contextMenuX + contextMenuWidth(app) <= app.width + 0.01f,
                  "menu clamps to the right edge");
            check(app.contextMenuY >= chromeTopHeight(app), "menu clears title-bar controls");
            check(app.contextMenuY + contextMenuHeight(app) <= app.height + 0.01f,
                  "all rows fit in a short scaled window");
            const auto& entries = contextMenuEntries(app);
            std::set<int> actions;
            for (int row = 0; row < (int)entries.size(); row++) {
                const auto& entry = entries[row];
                check(actions.insert(entry.action).second, "actions occur once per menu");
                float x = app.contextMenuX + 10;
                float y = app.contextMenuY + contextMenuItemTop(app, row);
                check(contextMenuItemAt(app, x, y + contextMenuItemHeight(app) / 2) == entry.action,
                      "hit testing resolves actions, not row indices");
                if (entry.separatorAfter)
                    check(contextMenuItemAt(app, x, y + contextMenuItemHeight(app) + 1) == -1,
                          "separators never activate a neighboring command");
            }
            check(application ? actions.count(CTX_COPY) == 0 : actions.count(CTX_COPY) == 1,
                  "document-only commands stay in the document menu");
        }
    }
    closeContextMenu(app);
    check(!app.showContextMenu && app.hoveredContextMenuItem == -1, "dismissal clears menu selection");
    return failures ? 1 : 0;
}
