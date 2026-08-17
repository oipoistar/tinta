#include "input.h"
#include "document.h"
#include "editor.h"
#include "file_utils.h"
#include "utils.h"
#include "search.h"
#include "d2d_init.h"
#include "settings.h"
#include "render.h"
#include "overlays.h"
#include "print.h"

#include <windowsx.h>
#include <shellapi.h>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

// Cached cursor handles - avoid calling LoadCursor() on every WM_MOUSEMOVE
static HCURSOR cursorArrow = LoadCursor(nullptr, IDC_ARROW);
static HCURSOR cursorHand  = LoadCursor(nullptr, IDC_HAND);
static HCURSOR cursorIBeam = LoadCursor(nullptr, IDC_IBEAM);

// Loads a document into the viewer and resets per-document state.
// Shared by the folder browser's item clicks, path input, and new-file flow.
static bool openDocumentInViewer(App& app, const std::wstring& fullPath) {
    std::ifstream file(fullPath);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto result = parseDocument(app.parser, buffer.str(), fullPath);
    if (!result.success) return false;

    // Reading position memory (#77): keep the old document's position,
    // resume the new one's
    if (!app.currentFile.empty()) {
        persistReadingPosition(app.currentFile, app.scrollY);
    }

    app.root = result.root;
    app.parseTimeUs = result.parseTimeUs;
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    app.currentFile.resize(utf8Len - 1);
    WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, &app.currentFile[0], utf8Len, nullptr, nullptr);
    app.scrollY = 0;
    app.scrollX = 0;
    app.targetScrollY = 0;
    app.targetScrollX = 0;
    app.pendingScrollRestore = findReadingPosition(loadSettings(), app.currentFile);
    app.focusMermaidOnNextLayout = isMermaidDocumentPath(fullPath);
    app.contentHeight = 0;
    app.docText.clear();
    app.docTextLower.clear();
    app.searchMatches.clear();
    app.layoutDirty = true;
    updateFileWriteTime(app);
    updateWindowTitle(app);
    return true;
}

// Spawns a second Tinta window, offset from this one, showing the given
// document in edit mode (#74: creating a note no longer replaces the
// document you were reading)
static void launchDocumentWindow(const std::wstring& fullPath) {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    std::wstring params = L"--cascade --edit \"" + fullPath + L"\"";
    ShellExecuteW(nullptr, L"open", exePath, params.c_str(), nullptr, SW_SHOWNORMAL);
}

// N key / context menu: open the folder browser with the new-file naming
// row already active
static void startNewFileFlow(App& app, HWND hwnd) {
    if (!app.showFolderBrowser) {
        app.showFolderBrowser = true;
        app.folderBrowserAnimation = 0;
        if (!app.currentFile.empty()) {
            app.folderBrowserPath = getDirectoryFromFile(app.currentFile);
        } else {
            wchar_t cwd[MAX_PATH];
            if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
                app.folderBrowserPath = cwd;
            }
        }
        populateFolderItems(app);
    }
    app.folderBrowserNaming = 1;  // new file
    app.folderBrowserInput.clear();
    app.folderBrowserInputSelectAll = false;
    app.folderBrowserInputError = false;
    app.folderInputJustOpened = true;
    app.folderBrowserScroll = 0.0f;
    updateBlinkTimer(app);
    resetCursorBlink(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Folder browser path/name input (#52) ---

static void closeFolderBrowserInput(App& app) {
    app.folderBrowserEditingPath = false;
    app.folderBrowserNaming = 0;
    app.folderBrowserInput.clear();
    app.folderBrowserInputSelectAll = false;
    app.folderBrowserInputError = false;
    updateBlinkTimer(app);
}

// Single-line clipboard text: newlines and tabs become spaces
static std::wstring clipboardLine(HWND hwnd) {
    if (!OpenClipboard(hwnd)) return {};
    std::wstring text;
    if (HANDLE hData = GetClipboardData(CF_UNICODETEXT)) {
        if (wchar_t* ptr = (wchar_t*)GlobalLock(hData)) {
            text = ptr;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    for (wchar_t& ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    return text;
}

// Trims whitespace and the quotes Explorer's "Copy as path" adds
static std::wstring cleanPathInput(std::wstring s) {
    auto trim = [](std::wstring& t) {
        size_t a = t.find_first_not_of(L" ");
        size_t b = t.find_last_not_of(L" ");
        t = (a == std::wstring::npos) ? L"" : t.substr(a, b - a + 1);
    };
    trim(s);
    if (s.length() >= 2 && s.front() == L'"' && s.back() == L'"') {
        s = s.substr(1, s.length() - 2);
        trim(s);
    }
    return s;
}

// Enter in the path box: a directory browses there, a file opens
static void commitFolderBrowserPath(App& app) {
    std::wstring path = cleanPathInput(app.folderBrowserInput);
    if (path.empty()) {
        closeFolderBrowserInput(app);
        return;
    }
    wchar_t expanded[MAX_PATH];
    if (ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH) > 0) {
        path = expanded;
    }

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        app.folderBrowserInputError = true;
        return;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        // Strip a trailing separator (but keep drive roots like C:\)
        while (path.length() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
            path.pop_back();
        }
        app.folderBrowserPath = path;
        populateFolderItems(app);
        closeFolderBrowserInput(app);
        return;
    }
    if (isSupportedDropPath(path) && openDocumentInViewer(app, path)) {
        closeFolderBrowserInput(app);
        app.showFolderBrowser = false;
        app.folderBrowserAnimation = 0;
    } else {
        app.folderBrowserInputError = true;
    }
}

// Enter in the naming box: creates the file/folder in the browsed directory.
// A new folder is entered; a new file opens straight into edit mode.
static void commitFolderBrowserNaming(App& app) {
    std::wstring name = cleanPathInput(app.folderBrowserInput);
    if (name.empty()) {
        closeFolderBrowserInput(app);
        return;
    }
    if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos ||
        name == L"." || name == L"..") {
        app.folderBrowserInputError = true;
        return;
    }

    bool isFolder = app.folderBrowserNaming == 2;
    if (!isFolder && name.find(L'.') == std::wstring::npos) {
        name += L".md";
    }
    std::wstring fullPath = app.folderBrowserPath;
    if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') {
        fullPath += L'\\';
    }
    fullPath += name;

    if (isFolder) {
        if (!CreateDirectoryW(fullPath.c_str(), nullptr)) {
            app.folderBrowserInputError = true;
            return;
        }
        app.folderBrowserPath = fullPath;
        populateFolderItems(app);
        closeFolderBrowserInput(app);
        return;
    }

    // CREATE_NEW fails if the file already exists instead of truncating it
    HANDLE h = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        app.folderBrowserInputError = true;
        return;
    }
    CloseHandle(h);
    // The new note opens in its own window, offset from this one, so the
    // document being read stays exactly where it is (#74)
    closeFolderBrowserInput(app);
    app.showFolderBrowser = false;
    app.folderBrowserAnimation = 0;
    launchDocumentWindow(fullPath);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

// Ctrl+wheel zoom. The scroll anchor scales immediately, but text format
// recreation (~47 COM objects) + full relayout is applied on the leading
// tick and then coalesced via TIMER_ZOOM_APPLY while the wheel keeps spinning.
static void applyZoomDelta(App& app, float delta) {
    float oldZoom = app.zoomFactor;
    app.zoomFactor = std::max(0.5f, std::min(3.0f, app.zoomFactor + delta * 0.1f));
    float zoomRatio = app.zoomFactor / oldZoom;
    app.scrollY *= zoomRatio;
    app.targetScrollY *= zoomRatio;
    if (!app.zoomApplyPending) {
        updateTextFormats(app);
        app.zoomApplyPending = true;
        SetTimer(app.hwnd, TIMER_ZOOM_APPLY, 80, nullptr);
    }
}

// Reading-width sliders: map a mouse X onto the stored track rect (#82)
static void settingsSliderApply(App& app, int which, float mx) {
    const D2D1_RECT_F& track =
        app.settingsSliderTrack[which == SET_SLIDER_ZEN ? 1 : 0];
    float t = (mx - track.left) / std::max(1.0f, track.right - track.left);
    int pct = 30 + (int)(t * 70.0f + 0.5f);
    pct = std::max(30, std::min(100, pct));
    if (which == SET_SLIDER_ZEN) app.zenWidthPct = pct;
    else app.readingWidthPct = pct;
    app.layoutDirty = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// Applies one settings-overlay action (id from app.settingsHits)
static void settingsAction(App& app, HWND hwnd, int action) {
    switch (action) {
        case SET_SECTION_GENERAL: app.settingsSection = 0; break;
        case SET_SECTION_APPEARANCE: app.settingsSection = 1; break;
        case SET_SECTION_EDITOR: app.settingsSection = 2; break;
        case SET_TOGGLE_FOLDERSEARCH:
            app.folderSearchEnabled = !app.folderSearchEnabled;
            if (!app.folderSearchEnabled) clearFolderSearch(app);
            break;
        case SET_TOGGLE_FOLLOW:
            app.followSystemTheme = !app.followSystemTheme;
            if (app.followSystemTheme) {
                if (app.theme.isDark) app.darkThemeIndex = app.currentThemeIndex;
                else app.lightThemeIndex = app.currentThemeIndex;
                PostMessageW(hwnd, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
            }
            break;
        case SET_TOGGLE_WRAP:
            app.editorWordWrap = !app.editorWordWrap;
            app.clearEditorLineLayoutCache();
            break;
        case SET_TOGGLE_PREVIEW:
            app.editorShowPreview = !app.editorShowPreview;
            app.layoutDirty = true;
            break;
        case SET_OPEN_THEMES:
            app.showSettings = false;
            app.showThemeChooser = true;
            app.themeChooserAnimation = 0;
            break;
        case SET_NEW_THEME:
            // Theme editor arrives in the next commit; the chip is wired
            break;
        case SET_OPEN_INI:
            ShellExecuteW(nullptr, L"open", getSettingsPath().c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case SET_OPEN_THEMES_INI: {
            std::wstring p = getSettingsPath();
            size_t slash = p.find_last_of(L'\\');
            if (slash != std::wstring::npos) {
                p = p.substr(0, slash + 1) + L"themes.ini";
                if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
                    std::ofstream f(p);  // seed an empty file so the editor opens
                    f << "; Custom themes: [theme] sections, RRGGBB colors.\n";
                }
                ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            break;
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Maps a pressed key through the user keymap ([Keys] in settings.ini): a
// remapped key becomes the built-in default the switches below expect, and
// a default key the user moved elsewhere is swallowed. Non-action keys pass
// through untouched. (#77)
static WPARAM translateActionKey(App& app, WPARAM key, bool isChar) {
    auto norm = [&](unsigned k) {
        return isChar ? (unsigned)towupper((wint_t)k) : k;
    };
    unsigned pressed = norm((unsigned)key);
    for (int i = 0; i < KEY_ACTION_COUNT; i++) {
        if (KEY_ACTIONS[i].isChar != isChar) continue;
        if (norm(app.keymap[i]) == pressed) {
            return (WPARAM)KEY_ACTIONS[i].defaultKey;
        }
    }
    for (int i = 0; i < KEY_ACTION_COUNT; i++) {
        if (KEY_ACTIONS[i].isChar != isChar) continue;
        if (norm(KEY_ACTIONS[i].defaultKey) == pressed &&
            norm(app.keymap[i]) != pressed) {
            return 0;  // default key rebound elsewhere: swallow
        }
    }
    return key;
}

void handleMouseWheel(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam) {
    // Settings overlay: consume the wheel (nothing scrolls yet)
    if (app.showSettings) return;

    // Print preview: the wheel flips pages
    if (app.showPrintPreview) {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        printPreviewSetPage(app, app.printPreviewPage + (delta < 0 ? 1 : -1));
        return;
    }
    if (app.showContextMenu) {
        app.showContextMenu = false;
        app.contextMenuAnimation = 0;
        app.hoveredContextMenuItem = -1;
    }
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;

    // Help overlay scroll
    if (app.showHelp) {
        app.helpScroll -= delta * dpi(app, 60.0f);
        if (app.helpScroll < 0) app.helpScroll = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Edit mode: the wheel scrolls the editor from either pane — the preview
    // follows through the scroll-anchor sync, so the panes cannot drift
    // apart and both sides always respond (#77)
    if (app.editMode) {
        float sepX = app.editorShowPreview
            ? app.width * app.editorSplitRatio
            : static_cast<float>(app.width);
        if (ctrl && app.mouseX < sepX) {
            applyZoomDelta(app, delta);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (!ctrl) {
            handleEditorMouseWheel(app, hwnd, delta);
            return;
        }
        // Ctrl over the preview pane: fall through to document zoom
    }

    // Handle folder browser scroll (not when Ctrl is held — that's zoom)
    if (app.showFolderBrowser && !ctrl) {
        float panelWidth = std::min(dpi(app, 300.0f), std::max(dpi(app, 250.0f), app.width * 0.2f));
        float panelX = -panelWidth * (1.0f - app.folderBrowserAnimation);
        if (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth) {
            // Scroll folder list
            app.folderBrowserScroll -= delta * dpi(app, 60.0f);
            float itemHeight = dpi(app, 28.0f);
            float headerHeight = dpi(app, 48.0f);
            float listHeight = app.height - headerHeight - dpi(app, 20.0f);
            float totalItemsHeight = app.folderItems.size() * itemHeight +
                (app.folderBrowserNaming != 0 ? itemHeight : 0.0f);
            float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);
            app.folderBrowserScroll = std::max(0.0f, std::min(app.folderBrowserScroll, maxScroll));
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }

    // Handle TOC scroll (not when Ctrl is held — that's zoom)
    if (app.showToc && !ctrl) {
        float panelWidth = std::min(dpi(app, 280.0f), std::max(dpi(app, 220.0f), app.width * 0.2f));
        float panelX = app.width - panelWidth * app.tocAnimation;
        if (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth) {
            app.tocScroll -= delta * dpi(app, 60.0f);
            float itemHeight = dpi(app, 28.0f);
            float headerHeight = dpi(app, 48.0f);
            float listHeight = app.height - headerHeight - dpi(app, 20.0f);
            float totalItemsHeight = app.headings.size() * itemHeight;
            float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);
            app.tocScroll = std::max(0.0f, std::min(app.tocScroll, maxScroll));
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }

    if (ctrl) {
        // Zoom in/out — scale scroll position to keep content anchored
        applyZoomDelta(app, delta);
    } else if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        // Shift+wheel: horizontal scroll (wheel up = left), same as tilt wheels
        app.targetScrollX -= delta * dpi(app, 60.0f);
        float maxScrollX = std::max(
            0.0f, app.contentWidth - documentViewportWidth(app));
        app.targetScrollX = std::max(0.0f, std::min(app.targetScrollX, maxScrollX));
        app.scrollX = app.targetScrollX;
    } else {
        // Normal scroll
        app.targetScrollY -= delta * dpi(app, 60.0f);
        float maxScroll = std::max(0.0f, app.contentHeight - app.height);
        app.targetScrollY = std::max(0.0f, std::min(app.targetScrollY, maxScroll));
        app.scrollY = app.targetScrollY;
    }

    InvalidateRect(hwnd, nullptr, FALSE);
}

void handleMouseHWheel(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam) {
    // Horizontal scroll
    float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * dpi(app, 60.0f);
    app.targetScrollX += delta;

    float maxScrollX = std::max(
        0.0f, app.contentWidth - documentViewportWidth(app));
    app.targetScrollX = std::max(0.0f, std::min(app.targetScrollX, maxScrollX));
    app.scrollX = app.targetScrollX;

    InvalidateRect(hwnd, nullptr, FALSE);
}

void handleMouseMove(App& app, HWND hwnd, LPARAM lParam) {
    app.mouseX = GET_X_LPARAM(lParam);
    app.mouseY = GET_Y_LPARAM(lParam);

    // Print preview: no hover states; document hit-testing below would run
    // against print-layout coordinates anyway
    if (app.showPrintPreview) return;

    // Settings: drag a slider if one is held, and show a hand over controls
    // instead of the document's text caret
    if (app.showSettings) {
        if (app.settingsDragSlider) {
            settingsSliderApply(app, app.settingsDragSlider, (float)app.mouseX);
            return;
        }
        bool overControl = false;
        for (const auto& hit : app.settingsHits) {
            if (app.mouseX >= hit.first.left && app.mouseX <= hit.first.right &&
                app.mouseY >= hit.first.top && app.mouseY <= hit.first.bottom) {
                overControl = true;
                break;
            }
        }
        SetCursor(LoadCursorW(nullptr, overControl ? IDC_HAND : IDC_ARROW));
        return;
    }

    // Context menu open: hover tracks menu items only — document hover
    // (code-block copy button, link underline) stays suppressed underneath
    if (app.showContextMenu) {
        bool repaint = false;
        int item = contextMenuItemAt(app, (float)app.mouseX, (float)app.mouseY);
        if (item != app.hoveredContextMenuItem) {
            app.hoveredContextMenuItem = item;
            repaint = true;
        }
        if (app.hoveredCodeBlock != -1) {
            app.hoveredCodeBlock = -1;
            repaint = true;
        }
        if (!app.hoveredLink.empty()) {
            app.hoveredLink.clear();
            repaint = true;
        }
        if (repaint) InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Help overlay scrollbar dragging
    if (app.helpScrollbarDragging) {
        float maxScroll = std::max(0.0f, app.helpContentHeight - app.helpVisibleHeight);
        if (maxScroll > 0) {
            float sbHeight = app.helpVisibleHeight / app.helpContentHeight * app.helpVisibleHeight;
            sbHeight = std::max(sbHeight, dpi(app, 20.0f));
            float trackHeight = app.helpVisibleHeight - sbHeight;

            float deltaY = (float)app.mouseY - app.helpScrollbarDragStartY;
            float scrollDelta = (trackHeight > 0) ? (deltaY / trackHeight) * maxScroll : 0;
            app.helpScroll = app.helpScrollbarDragStartScroll + scrollDelta;
            app.helpScroll = std::max(0.0f, std::min(app.helpScroll, maxScroll));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    // Edit mode: handle separator drag, editor selection, cursor shape
    if (app.editMode) {
        handleEditorMouseMove(app, hwnd, app.mouseX, app.mouseY);
        // If mouse is in the preview pane and not dragging separator, fall through for link hover etc.
        float sepX = app.editorShowPreview
            ? app.width * app.editorSplitRatio
            : static_cast<float>(app.width);
        if (app.mouseX < sepX || app.draggingSeparator || app.editorSelecting) return;
        // For preview pane, adjust mouseX to be relative to preview offset
        // but we leave the existing code to work with document coordinates
    }

    float previewOffsetX = documentViewportX(app);
    float docX = (app.mouseX - previewOffsetX) + app.scrollX;
    float docY = app.mouseY + app.scrollY;

    // Text selection dragging
    if (app.selecting) {
        if (app.selectionMode == App::SelectionMode::Word) {
            // Extend selection by words - merge anchor with current word
            const App::TextRect* tr = findTextRectAt(app, (int)docX, (int)docY);
            if (tr) {
                float wordLeft, wordRight;
                if (findWordBoundsAt(app, *tr, (int)docX, wordLeft, wordRight)) {
                    // Selection spans from min(anchor, current) to max(anchor, current)
                    app.selStartX = (int)std::min(app.anchorLeft, wordLeft);
                    app.selEndX = (int)std::max(app.anchorRight, wordRight);
                    app.selStartY = (int)(std::min(app.anchorTop, tr->rect.top));
                    app.selEndY = (int)(std::max(app.anchorBottom, tr->rect.bottom));
                    app.hasSelection = true;
                }
            }
        } else if (app.selectionMode == App::SelectionMode::Line) {
            // Extend selection by lines - merge anchor with current line
            float lineLeft, lineRight, lineTop, lineBottom;
            findLineRects(app, docY, lineLeft, lineRight, lineTop, lineBottom);
            if (lineRight > lineLeft) {
                // Selection spans from min(anchor, current) to max(anchor, current)
                app.selStartX = (int)std::min(app.anchorLeft, lineLeft);
                app.selEndX = (int)std::max(app.anchorRight, lineRight);
                app.selStartY = (int)(std::min(app.anchorTop, lineTop));
                app.selEndY = (int)(std::max(app.anchorBottom, lineBottom));
                app.hasSelection = true;
            }
        } else {
            // Normal selection - store in document coordinates
            app.selEndX = (int)docX;
            app.selEndY = (int)docY;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Vertical scrollbar dragging
    if (app.scrollbarDragging) {
        float maxScroll = std::max(0.0f, app.contentHeight - app.height);
        if (maxScroll > 0 && app.contentHeight > app.height) {
            float sbHeight = (float)app.height / app.contentHeight * app.height;
            sbHeight = std::max(sbHeight, 30.0f);
            float trackHeight = app.height - sbHeight;

            float deltaY = (float)app.mouseY - app.scrollbarDragStartY;
            float scrollDelta = (deltaY / trackHeight) * maxScroll;
            app.scrollY = app.scrollbarDragStartScroll + scrollDelta;
            app.scrollY = std::max(0.0f, std::min(app.scrollY, maxScroll));
            app.targetScrollY = app.scrollY;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    // Horizontal scrollbar dragging
    if (app.hScrollbarDragging) {
        float viewportWidth = documentViewportWidth(app);
        float maxScroll = std::max(0.0f, app.contentWidth - viewportWidth);
        if (maxScroll > 0 && app.contentWidth > viewportWidth) {
            float sbWidth = viewportWidth / app.contentWidth * viewportWidth;
            sbWidth = std::max(sbWidth, 30.0f);
            float trackWidth = viewportWidth - sbWidth;

            float deltaX = (float)app.mouseX - app.hScrollbarDragStartX;
            float scrollDelta = (deltaX / trackWidth) * maxScroll;
            app.scrollX = app.hScrollbarDragStartScroll + scrollDelta;
            app.scrollX = std::max(0.0f, std::min(app.scrollX, maxScroll));
            app.targetScrollX = app.scrollX;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    // Check vertical scrollbar hover
    bool wasHovered = app.scrollbarHovered;
    app.scrollbarHovered = false;
    if (app.contentHeight > app.height) {
        float sbWidth = dpi(app, 14.0f);  // hit area
        if (app.mouseX >= app.width - sbWidth) {
            app.scrollbarHovered = true;
        }
    }

    // Check horizontal scrollbar hover
    bool wasHHovered = app.hScrollbarHovered;
    app.hScrollbarHovered = false;
    float viewportX = documentViewportX(app);
    float viewportWidth = documentViewportWidth(app);
    if (app.contentWidth > viewportWidth) {
        float sbHeight = dpi(app, 14.0f);  // hit area
        if (app.mouseY >= app.height - sbHeight &&
            app.mouseX >= viewportX &&
            app.mouseX <= viewportX + viewportWidth) {
            app.hScrollbarHovered = true;
        }
    }

    // Check link hover
    std::string prevHoveredLink = app.hoveredLink;
    app.hoveredLink.clear();
    for (const auto& lr : app.linkRects) {
        if (docX >= lr.bounds.left && docX <= lr.bounds.right &&
            docY >= lr.bounds.top && docY <= lr.bounds.bottom) {
            app.hoveredLink = lr.url;
            break;
        }
    }

    // Check if over text
    bool wasOverText = app.overText;
    app.overText = (findTextRectAt(app, (int)docX, (int)docY) != nullptr);

    // Check if hovering over any code block (show copy button on whole block)
    int prevHoveredCodeBlock = app.hoveredCodeBlock;
    app.hoveredCodeBlock = -1;
    for (int i = 0; i < (int)app.codeBlocks.size(); i++) {
        const auto& cb = app.codeBlocks[i];
        if (docX >= cb.bounds.left && docX <= cb.bounds.right &&
            docY >= cb.bounds.top && docY <= cb.bounds.bottom) {
            app.hoveredCodeBlock = i;
            break;
        }
    }

    // Update cursor (using cached handles)
    if (app.showFolderBrowser) {
        float panelWidth = std::min(dpi(app, 300.0f), std::max(dpi(app, 250.0f), app.width * 0.2f));
        float panelX = -panelWidth * (1.0f - app.folderBrowserAnimation);
        bool inPanel = (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth);
        if (inPanel && app.hoveredFolderIndex >= 0) {
            SetCursor(cursorHand);
        } else {
            SetCursor(cursorArrow);
        }
        // Only invalidate when mouse is over the panel (hover tracking needed)
        if (inPanel)
            InvalidateRect(hwnd, nullptr, FALSE);
    } else if (app.showToc) {
        float panelWidth = std::min(dpi(app, 280.0f), std::max(dpi(app, 220.0f), app.width * 0.2f));
        float panelX = app.width - panelWidth * app.tocAnimation;
        bool inPanel = (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth);
        if (inPanel && app.hoveredTocIndex >= 0) {
            SetCursor(cursorHand);
        } else {
            SetCursor(cursorArrow);
        }
        // Only invalidate when mouse is over the panel (hover tracking needed)
        if (inPanel)
            InvalidateRect(hwnd, nullptr, FALSE);
    } else if (app.scrollbarHovered || app.scrollbarDragging ||
        app.hScrollbarHovered || app.hScrollbarDragging) {
        SetCursor(cursorArrow);
    } else if (app.hoveredCodeBlock >= 0) {
        // Show hand cursor only when over the copy button area
        const auto& cb = app.codeBlocks[app.hoveredCodeBlock];
        float btnW = dpi(app, 52.0f);
        float btnH = dpi(app, 26.0f);
        float btnPad = 8.0f * app.contentScale * app.zoomFactor;
        float btnX = cb.bounds.right - btnW - btnPad;
        float btnY = cb.bounds.top + btnPad;
        if (docX >= btnX && docX <= btnX + btnW &&
            docY >= btnY && docY <= btnY + btnH) {
            SetCursor(cursorHand);
        } else if (app.overText) {
            SetCursor(cursorIBeam);
        } else {
            SetCursor(cursorArrow);
        }
    } else if (!app.hoveredLink.empty()) {
        SetCursor(cursorHand);
    } else if (app.overText) {
        SetCursor(cursorIBeam);
    } else {
        SetCursor(cursorArrow);
    }

    if (wasHovered != app.scrollbarHovered ||
        wasHHovered != app.hScrollbarHovered ||
        prevHoveredLink != app.hoveredLink ||
        prevHoveredCodeBlock != app.hoveredCodeBlock) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}


// --- Right-click context menu ---

static void closeContextMenu(App& app) {
    app.showContextMenu = false;
    app.contextMenuAnimation = 0.0f;
    app.hoveredContextMenuItem = -1;
}

static void closeSearchIfOpen(App& app) {
    if (!app.showSearch) return;
    app.showSearch = false;
    app.searchActive = false;
    app.searchQuery.clear();
    app.searchMatches.clear();
    app.searchAnimation = 0;
    clearFolderSearch(app);
    updateBlinkTimer(app);
}

static void invokeContextMenuAction(App& app, HWND hwnd, int item) {
    switch (item) {
        case CTX_COPY:
            if (app.hasSelection && !app.selectedText.empty()) {
                copyToClipboard(hwnd, app.selectedText);
                app.hasSelection = false;
                app.selectedText.clear();
                app.showCopiedNotification = true;
                app.copiedNotificationAlpha = 1.0f;
                app.copiedNotificationStart = std::chrono::steady_clock::now();
                startNotificationTimer(app);
            }
            break;
        case CTX_SELECT_ALL:
            if (app.root) {
                app.selectedText.clear();
                extractText(app.root, app.selectedText);
                app.hasSelection = true;
                // Equal coords are the renderer's select-all signal; a stale
                // drag range would re-extract the old selection every frame
                app.selStartX = app.selEndX = 0;
                app.selStartY = app.selEndY = 0;
            }
            break;
        case CTX_NEW:
            closeSearchIfOpen(app);
            startNewFileFlow(app, hwnd);
            break;
        case CTX_PRINT:
            closeSearchIfOpen(app);
            openPrintPreview(app, hwnd);
            break;
        case CTX_EDIT:
            closeSearchIfOpen(app);
            enterEditMode(app);
            break;
        case CTX_SEARCH:
            if (app.showSearch) { app.searchActive = true; break; }
            app.showSearch = true;
            app.searchActive = true;
            app.searchAnimation = 0;
            app.searchQuery.clear();
            app.searchMatches.clear();
            app.searchCurrentIndex = 0;
            app.searchJustOpened = false;
            updateBlinkTimer(app);
            break;
        case CTX_TOC:
            closeSearchIfOpen(app);
            ensureLayoutComplete(app);
            app.showToc = true;
            app.tocAnimation = 0;
            app.tocScroll = 0;
            app.hoveredTocIndex = -1;
            break;
        case CTX_BROWSE:
            closeSearchIfOpen(app);
            app.showFolderBrowser = true;
            closeFolderBrowserInput(app);
            app.folderBrowserAnimation = 0;
            if (!app.currentFile.empty()) {
                app.folderBrowserPath = getDirectoryFromFile(app.currentFile);
            } else {
                wchar_t cwd[MAX_PATH];
                if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
                    app.folderBrowserPath = cwd;
                }
            }
            populateFolderItems(app);
            break;
        case CTX_REVEAL:
            if (!app.currentFile.empty()) {
                std::wstring widePath = toWide(app.currentFile);
                wchar_t fullPath[MAX_PATH];
                if (GetFullPathNameW(widePath.c_str(), MAX_PATH, fullPath, nullptr)) {
                    std::wstring params = L"/select,\"" + std::wstring(fullPath) + L"\"";
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(),
                                  nullptr, SW_SHOWNORMAL);
                }
            }
            break;
        case CTX_THEME:
            closeSearchIfOpen(app);
            app.showThemeChooser = true;
            app.themeChooserAnimation = 0;
            break;
        case CTX_SETTINGS:
            closeSearchIfOpen(app);
            app.showSettings = true;
            app.settingsAnimation = 0;
            break;
        case CTX_HELP:
            closeSearchIfOpen(app);
            app.showHelp = true;
            app.helpAnimation = 0;
            break;
    }
}

void handleContextMenu(App& app, HWND hwnd, LPARAM lParam) {
    // Viewer mode only, and never on top of a modal overlay. The search
    // bar is fine — it shares the viewport rather than covering it.
    if (app.editMode || app.showThemeChooser || app.showToc ||
        app.showFolderBrowser || app.showHelp || app.showPrintPreview ||
        app.showSettings) {
        return;
    }

    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    if (pt.x == -1 && pt.y == -1) {
        // Keyboard menu key: open near the viewport center
        pt.x = app.width / 2;
        pt.y = app.height / 2;
    } else {
        ScreenToClient(hwnd, &pt);
    }
    openContextMenu(app, (float)pt.x, (float)pt.y);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void handleMouseDown(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam) {
    // Settings: sliders drag from the press; everything else acts on release
    if (app.showSettings) {
        float mx = (float)GET_X_LPARAM(lParam);
        float my = (float)GET_Y_LPARAM(lParam);
        for (const auto& hit : app.settingsHits) {
            if ((hit.second == SET_SLIDER_READING || hit.second == SET_SLIDER_ZEN) &&
                mx >= hit.first.left && mx <= hit.first.right &&
                my >= hit.first.top && my <= hit.first.bottom) {
                app.settingsDragSlider = hit.second;
                SetCapture(hwnd);
                settingsSliderApply(app, hit.second, mx);
                return;
            }
        }
        return;
    }
    // Print preview: controls act on the release
    if (app.showPrintPreview) return;

    // Context menu: a click lands on an item or dismisses the menu; either
    // way the click is consumed
    if (app.showContextMenu) {
        int clickX = GET_X_LPARAM(lParam);
        int clickY = GET_Y_LPARAM(lParam);
        int item = contextMenuItemAt(app, (float)clickX, (float)clickY);
        closeContextMenu(app);
        app.swallowNextMouseUp = true;
        if (item >= 0 && contextMenuItemEnabled(app, item)) {
            invokeContextMenuAction(app, hwnd, item);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Edit mode: route to editor or preview
    if (app.editMode) {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        float sepX = app.editorShowPreview
            ? app.width * app.editorSplitRatio
            : static_cast<float>(app.width);
        if (x < sepX + 6) {
            handleEditorMouseDown(app, hwnd, x, y);
            return;
        }
        // Fall through for preview pane clicks
    }

    // Folder search: the bar's toggle button and result-panel clicks
    if (app.showSearch && !app.editMode) {
        float clickX = (float)GET_X_LPARAM(lParam);
        float clickY = (float)GET_Y_LPARAM(lParam);
        if (folderSearchToggleAt(app, clickX, clickY)) {
            app.folderSearchEnabled = !app.folderSearchEnabled;
            if (!app.folderSearchEnabled) {
                clearFolderSearch(app);
            } else {
                performSearch(app);  // re-arms the scan timer
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        for (const auto& hit : app.folderResultHits) {
            if (clickX >= hit.rect.left && clickX <= hit.rect.right &&
                clickY >= hit.rect.top && clickY <= hit.rect.bottom &&
                hit.fileIndex >= 0 && hit.fileIndex < (int)app.folderResults.size()) {
                // Open the file and land on the first match of the same query
                if (openDocumentInViewer(app, app.folderResults[hit.fileIndex].fullPath)) {
                    app.folderResults.clear();
                    app.folderResultHits.clear();
                    ensureLayoutComplete(app);
                    performSearch(app);
                    if (!app.searchMatches.empty()) {
                        app.searchCurrentIndex = 0;
                        scrollToCurrentMatch(app);
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
    }

    // Help overlay: check scrollbar click
    if (app.showHelp) {
        float maxScroll = std::max(0.0f, app.helpContentHeight - app.helpVisibleHeight);
        if (maxScroll > 0) {
            int clickX = GET_X_LPARAM(lParam);
            int clickY = GET_Y_LPARAM(lParam);
            // Scrollbar hit area: right edge of panel
            float panelWidth = std::min(dpi(app, 520.0f), app.width - dpi(app, 40.0f));
            float panelRight = (app.width + panelWidth) / 2;
            float sbHitWidth = dpi(app, 16.0f);
            if (clickX >= panelRight - sbHitWidth && clickX <= panelRight &&
                clickY >= app.helpScrollbarTop && clickY <= app.helpScrollbarTop + app.helpVisibleHeight) {
                app.helpScrollbarDragging = true;
                app.helpScrollbarDragStartY = (float)clickY;
                app.helpScrollbarDragStartScroll = app.helpScroll;
                SetCapture(hwnd);

                // Jump if clicked outside thumb
                float sbHeight = app.helpVisibleHeight / app.helpContentHeight * app.helpVisibleHeight;
                sbHeight = std::max(sbHeight, dpi(app, 20.0f));
                float sbY = app.helpScrollbarTop + (app.helpScroll / maxScroll * (app.helpVisibleHeight - sbHeight));
                if (clickY < sbY || clickY > sbY + sbHeight) {
                    float trackHeight = app.helpVisibleHeight - sbHeight;
                    float clickPos = (float)clickY - app.helpScrollbarTop - sbHeight / 2;
                    clickPos = std::max(0.0f, std::min(clickPos, trackHeight));
                    app.helpScroll = (trackHeight > 0) ? (clickPos / trackHeight) * maxScroll : 0;
                    app.helpScrollbarDragStartScroll = app.helpScroll;
                    app.helpScrollbarDragStartY = (float)clickY;
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return;
    }

    // If theme chooser, folder browser, or TOC is open, don't start selection - just record for click handling
    if (app.showThemeChooser || app.showFolderBrowser || app.showToc) {
        return;
    }

    app.mouseDown = true;
    app.mouseX = GET_X_LPARAM(lParam);
    app.mouseY = GET_Y_LPARAM(lParam);
    SetCapture(hwnd);
    float previewOffsetX = documentViewportX(app);
    float docX = (app.mouseX - previewOffsetX) + app.scrollX;
    float docY = app.mouseY + app.scrollY;

    // Check if clicking vertical scrollbar
    if (app.scrollbarHovered && app.contentHeight > app.height) {
        app.scrollbarDragging = true;
        app.scrollbarDragStartY = (float)app.mouseY;
        app.scrollbarDragStartScroll = app.scrollY;

        // Check if clicking in track (not thumb) - jump to position
        float maxScroll = std::max(0.0f, app.contentHeight - app.height);
        float sbHeight = (float)app.height / app.contentHeight * app.height;
        sbHeight = std::max(sbHeight, 30.0f);
        float sbY = (maxScroll > 0) ? (app.scrollY / maxScroll * (app.height - sbHeight)) : 0;

        // If clicked outside thumb, jump
        if (app.mouseY < sbY || app.mouseY > sbY + sbHeight) {
            float trackHeight = app.height - sbHeight;
            float clickPos = (float)app.mouseY - sbHeight / 2;
            clickPos = std::max(0.0f, std::min(clickPos, trackHeight));
            app.scrollY = (clickPos / trackHeight) * maxScroll;
            app.targetScrollY = app.scrollY;
            app.scrollbarDragStartScroll = app.scrollY;
            app.scrollbarDragStartY = (float)app.mouseY;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    // Check if clicking horizontal scrollbar
    else if (app.hScrollbarHovered &&
             app.contentWidth > documentViewportWidth(app)) {
        app.hScrollbarDragging = true;
        app.hScrollbarDragStartX = (float)app.mouseX;
        app.hScrollbarDragStartScroll = app.scrollX;

        // Check if clicking in track (not thumb) - jump to position
        float viewportX = documentViewportX(app);
        float viewportWidth = documentViewportWidth(app);
        float localMouseX = app.mouseX - viewportX;
        float maxScroll = std::max(0.0f, app.contentWidth - viewportWidth);
        float sbWidth = viewportWidth / app.contentWidth * viewportWidth;
        sbWidth = std::max(sbWidth, 30.0f);
        float sbX = (maxScroll > 0)
            ? (app.scrollX / maxScroll * (viewportWidth - sbWidth))
            : 0;

        // If clicked outside thumb, jump
        if (localMouseX < sbX || localMouseX > sbX + sbWidth) {
            float trackWidth = viewportWidth - sbWidth;
            float clickPos = localMouseX - sbWidth / 2;
            clickPos = std::max(0.0f, std::min(clickPos, trackWidth));
            app.scrollX = (clickPos / trackWidth) * maxScroll;
            app.targetScrollX = app.scrollX;
            app.hScrollbarDragStartScroll = app.scrollX;
            app.hScrollbarDragStartX = (float)app.mouseX;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    } else {
        // Detect double/triple clicks
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - app.lastClickTime).count();

        // Check if this is a repeated click (within 500ms and 5px of last click)
        bool isRepeatedClick = (elapsed < 500 &&
            std::abs(app.mouseX - app.lastClickX) < 5 &&
            std::abs(app.mouseY - app.lastClickY) < 5);

        if (isRepeatedClick) {
            app.clickCount = std::min(app.clickCount + 1, 3);
        } else {
            app.clickCount = 1;
        }

        app.lastClickTime = now;
        app.lastClickX = app.mouseX;
        app.lastClickY = app.mouseY;

        // Handle based on click count
        if (app.clickCount == 2) {
            // Double-click: select word
            const App::TextRect* tr = findTextRectAt(app, (int)docX, (int)docY);
            if (tr) {
                float wordLeft, wordRight;
                if (findWordBoundsAt(app, *tr, (int)docX, wordLeft, wordRight)) {
                    app.selectionMode = App::SelectionMode::Word;
                    // Store anchor (the original word bounds) in document coords for drag extension
                    app.anchorLeft = wordLeft;
                    app.anchorRight = wordRight;
                    app.anchorTop = tr->rect.top;
                    app.anchorBottom = tr->rect.bottom;
                    // Set selection to the word (document coordinates)
                    app.selStartX = (int)wordLeft;
                    app.selEndX = (int)wordRight;
                    app.selStartY = (int)tr->rect.top;
                    app.selEndY = (int)tr->rect.bottom;
                    app.selecting = true;
                    app.hasSelection = true;
                }
            }
        } else if (app.clickCount == 3) {
            // Triple-click: select line
            float lineLeft, lineRight, lineTop, lineBottom;
            findLineRects(app, docY, lineLeft, lineRight, lineTop, lineBottom);
            if (lineRight > lineLeft) {
                app.selectionMode = App::SelectionMode::Line;
                // Store anchor (the original line bounds) in document coords for drag extension
                app.anchorLeft = lineLeft;
                app.anchorRight = lineRight;
                app.anchorTop = lineTop;
                app.anchorBottom = lineBottom;
                // Set selection to the line (document coordinates)
                app.selStartX = (int)lineLeft;
                app.selEndX = (int)lineRight;
                app.selStartY = (int)lineTop;
                app.selEndY = (int)lineBottom;
                app.selecting = true;
                app.hasSelection = true;
            }
        } else {
            // Single click: start normal selection (document coordinates)
            app.selectionMode = App::SelectionMode::Normal;
            app.selecting = true;
            app.selStartX = (int)docX;
            app.selStartY = (int)docY;
            app.selEndX = (int)docX;
            app.selEndY = (int)docY;
            app.hasSelection = false;
            app.selectedText.clear();
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// Resolves an Obsidian [[wiki link]] target against the current file's
// folder and opens it: the name as written, then with .md / .markdown
// appended, then a case-insensitive scan of the directory listing.
static void openWikiLink(App& app, const std::string& target) {
    if (app.editMode || app.currentFile.empty()) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    std::wstring wideTarget = toWide(target);
    fs::path dir = fs::path(toWide(app.currentFile)).parent_path();

    auto tryOpen = [&](const fs::path& p) {
        return fs::exists(p, ec) && !fs::is_directory(p, ec) &&
               openDocumentInViewer(app, p.wstring());
    };
    if (tryOpen(dir / wideTarget)) return;
    if (tryOpen(dir / (wideTarget + L".md"))) return;
    if (tryOpen(dir / (wideTarget + L".markdown"))) return;

    auto lower = [](std::wstring s) {
        for (auto& c : s) c = (wchar_t)std::towlower(c);
        return s;
    };
    std::wstring want = lower(wideTarget);
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::wstring stem = lower(entry.path().stem().wstring());
        std::wstring name = lower(entry.path().filename().wstring());
        if (stem == want || name == want) {
            if (openDocumentInViewer(app, entry.path().wstring())) return;
        }
    }
    // Target doesn't exist: leave the document as-is (a viewer doesn't
    // create notes the way Obsidian would)
}

// Task checkbox under the mouse, or nullptr (viewer mode only)
static const App::TaskRect* taskRectAt(const App& app) {
    if (app.editMode) return nullptr;
    float docX = (app.mouseX - documentViewportX(app)) + app.scrollX;
    float docY = app.mouseY + app.scrollY;
    for (const auto& task : app.taskRects) {
        if (docX >= task.rect.left && docX <= task.rect.right &&
            docY >= task.rect.top && docY <= task.rect.bottom) {
            return &task;
        }
    }
    return nullptr;
}

// Flips a - [ ] / - [x] mark in the file on disk. Parse offsets were
// computed on CR-stripped text, so the position is remapped against the
// raw bytes; the neighbors are verified so a file that changed since the
// last layout is left alone. The file watcher then reloads the view.
static void toggleTaskOnDisk(App& app, HWND hwnd, size_t markOffset, bool wasChecked) {
    if (app.currentFile.empty()) return;
    std::wstring widePath = toWide(app.currentFile);
    std::ifstream in(widePath, std::ios::binary);
    if (!in) return;
    std::string disk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    size_t translated = 0, diskPos = std::string::npos;
    for (size_t i = 0; i < disk.size(); i++) {
        if (disk[i] == '\r') continue;
        if (translated == markOffset) { diskPos = i; break; }
        translated++;
    }
    if (diskPos == std::string::npos || diskPos == 0 || diskPos + 1 >= disk.size()) return;
    char c = disk[diskPos];
    bool checked = (c == 'x' || c == 'X');
    if (disk[diskPos - 1] != '[' || disk[diskPos + 1] != ']' ||
        checked != wasChecked || (!checked && c != ' ')) {
        return;
    }
    disk[diskPos] = checked ? ' ' : 'x';

    std::ofstream out(widePath, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(disk.data(), (std::streamsize)disk.size());
    out.close();
    handleFileWatchTimer(app, hwnd);  // reload immediately instead of on the timer
}

// True when (mouseX, mouseY) is on the hovered code block's copy button
static bool codeCopyButtonAt(const App& app, int mouseX, int mouseY) {
    if (app.hoveredCodeBlock < 0 ||
        app.hoveredCodeBlock >= (int)app.codeBlocks.size()) {
        return false;
    }
    const auto& cb = app.codeBlocks[app.hoveredCodeBlock];
    float previewOffsetX = documentViewportX(app);
    float docX = (mouseX - previewOffsetX) + app.scrollX;
    float docY = mouseY + app.scrollY;
    float btnW = dpi(app, 52.0f);
    float btnH = dpi(app, 26.0f);
    float btnPad = 8.0f * app.contentScale * app.zoomFactor;
    float btnX = cb.bounds.right - btnW - btnPad;
    float btnY = cb.bounds.top + btnPad;
    return docX >= btnX && docX <= btnX + btnW &&
           docY >= btnY && docY <= btnY + btnH;
}

void handleMouseUp(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam) {
    // Release belonging to a context-menu item click: already handled
    if (app.swallowNextMouseUp) {
        app.swallowNextMouseUp = false;
        return;
    }

    // Settings overlay: resolve against the hit rects stored during render
    if (app.showSettings) {
        if (app.settingsDragSlider) {
            app.settingsDragSlider = 0;
            ReleaseCapture();
            return;
        }
        float mx = (float)GET_X_LPARAM(lParam);
        float my = (float)GET_Y_LPARAM(lParam);
        for (const auto& hit : app.settingsHits) {
            if (mx >= hit.first.left && mx <= hit.first.right &&
                my >= hit.first.top && my <= hit.first.bottom) {
                settingsAction(app, hwnd, hit.second);
                return;
            }
        }
        return;
    }

    // Print preview: buttons and format chips
    if (app.showPrintPreview) {
        float mx = (float)GET_X_LPARAM(lParam);
        float my = (float)GET_Y_LPARAM(lParam);
        auto hit = [&](const D2D1_RECT_F& r) {
            return mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom;
        };
        if (hit(app.printPreviewPrintBtn)) {
            printPreviewConfirm(app, hwnd);
            return;
        }
        if (hit(app.printPreviewCancelBtn)) {
            closePrintPreview(app, hwnd);
            return;
        }
        for (int i = 0; i < PRINT_PAPER_COUNT; i++) {
            if (hit(app.printPreviewPaperBtn[i])) {
                printPreviewSetFormat(app, i, app.printPreviewLandscape);
                return;
            }
        }
        if (hit(app.printPreviewOrientBtn[0])) {
            printPreviewSetFormat(app, app.printPreviewPaper, false);
        } else if (hit(app.printPreviewOrientBtn[1])) {
            printPreviewSetFormat(app, app.printPreviewPaper, true);
        }
        return;
    }
    // Help scrollbar release
    if (app.helpScrollbarDragging) {
        app.helpScrollbarDragging = false;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Edit mode: route to editor
    if (app.editMode && (app.draggingSeparator || app.editorSelecting)) {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        handleEditorMouseUp(app, hwnd, x, y);
        return;
    }

    ReleaseCapture();

    // TOC click handling
    if (app.showToc) {
        int clickX = GET_X_LPARAM(lParam);

        float panelWidth = std::min(dpi(app, 280.0f), std::max(dpi(app, 220.0f), app.width * 0.2f));
        float panelX = app.width - panelWidth * app.tocAnimation;

        if (clickX >= panelX && (float)clickX <= panelX + panelWidth) {
            // Click inside panel
            if (app.hoveredTocIndex >= 0 && app.hoveredTocIndex < (int)app.headings.size()) {
                // Scroll document to heading
                scrollToHeadingY(app, app.headings[app.hoveredTocIndex].y);

                // Close TOC
                app.showToc = false;
                app.tocAnimation = 0;
            }
        } else {
            // Click outside panel = close TOC
            app.showToc = false;
            app.tocAnimation = 0;
        }

        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Folder browser click handling
    if (app.showFolderBrowser) {
        int clickX = GET_X_LPARAM(lParam);
        int clickY = GET_Y_LPARAM(lParam);

        // Calculate panel bounds (must match render code)
        float panelWidth = std::min(dpi(app, 300.0f), std::max(dpi(app, 250.0f), app.width * 0.2f));
        float panelX = -panelWidth * (1.0f - app.folderBrowserAnimation);

        // Check if click is inside panel
        if (clickX >= panelX && clickX <= panelX + panelWidth) {
            // Header geometry (must match renderFolderBrowser)
            float padding = dpi(app, 12.0f);
            float headerY = padding;
            float headerHeight = dpi(app, 40.0f);
            float btnSize = dpi(app, 24.0f);
            float btnGap = dpi(app, 6.0f);
            float fileBtnX = panelX + panelWidth - padding - btnSize;
            float folderBtnX = fileBtnX - btnGap - btnSize;
            float btnY = headerY + (headerHeight - btnSize) / 2 - dpi(app, 6.0f);
            bool inHeader = clickY >= headerY && clickY <= headerY + headerHeight;

            // An active input keeps focus when clicked, anything else cancels it
            if (app.folderBrowserEditingPath || app.folderBrowserNaming != 0) {
                bool onInput = app.folderBrowserEditingPath
                    ? inHeader
                    : (clickY >= headerY + headerHeight + dpi(app, 8.0f) &&
                       clickY <= headerY + headerHeight + dpi(app, 8.0f) + dpi(app, 28.0f));
                if (onInput) {
                    app.folderBrowserInputSelectAll = false;
                } else {
                    closeFolderBrowserInput(app);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }

            if (inHeader && clickY >= btnY && clickY <= btnY + btnSize &&
                clickX >= folderBtnX && clickX <= fileBtnX + btnSize) {
                // + folder / + file buttons: start naming a new item
                app.folderBrowserNaming = (clickX < folderBtnX + btnSize + btnGap / 2) ? 2 : 1;
                app.folderBrowserInput.clear();
                app.folderBrowserInputSelectAll = false;
                app.folderBrowserInputError = false;
                app.folderBrowserScroll = 0.0f;
                updateBlinkTimer(app);
                resetCursorBlink(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            if (inHeader) {
                // Path header becomes an edit box with everything selected,
                // so click + paste + Enter jumps straight to a new path (#52)
                app.folderBrowserEditingPath = true;
                app.folderBrowserInput = app.folderBrowserPath;
                app.folderBrowserInputSelectAll = true;
                app.folderBrowserInputError = false;
                updateBlinkTimer(app);
                resetCursorBlink(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }

            // Hit-test items
            if (app.hoveredFolderIndex >= 0 && app.hoveredFolderIndex < (int)app.folderItems.size()) {
                const auto& item = app.folderItems[app.hoveredFolderIndex];

                if (item.isDirectory) {
                    // Navigate into folder
                    if (item.name == L"..") {
                        // Go up to parent
                        app.folderBrowserPath = getParentPath(app.folderBrowserPath);
                    } else {
                        // Enter subdirectory
                        if (app.folderBrowserPath.back() != L'\\' && app.folderBrowserPath.back() != L'/') {
                            app.folderBrowserPath += L'\\';
                        }
                        app.folderBrowserPath += item.name;
                    }
                    populateFolderItems(app);
                } else {
                    // Open document
                    std::wstring fullPath = app.folderBrowserPath;
                    if (fullPath.back() != L'\\' && fullPath.back() != L'/') {
                        fullPath += L'\\';
                    }
                    fullPath += item.name;

                    if (openDocumentInViewer(app, fullPath)) {
                        // Close folder browser after opening file
                        app.showFolderBrowser = false;
                        app.folderBrowserAnimation = 0;
                    }
                }
            }
        } else {
            // Click outside panel = close browser
            closeFolderBrowserInput(app);
            app.showFolderBrowser = false;
            app.folderBrowserAnimation = 0;
        }

        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Theme chooser click handling
    if (app.showThemeChooser) {
        int clickX = GET_X_LPARAM(lParam);
        int clickY = GET_Y_LPARAM(lParam);

        // Calculate which theme was clicked (replicate layout logic)
        float panelWidth = std::min(dpi(app, 900.0f), (float)app.width - dpi(app, 80.0f));
        float panelHeight = std::min(dpi(app, 620.0f), (float)app.height - dpi(app, 80.0f));
        float panelX = (app.width - panelWidth) / 2;
        float panelY = (app.height - panelHeight) / 2;
        float gridStartY = panelY + dpi(app, 75.0f);
        float cardWidth = (panelWidth - dpi(app, 60.0f)) / 2;
        float cardHeight = (panelHeight - dpi(app, 130.0f)) / themeChooserRows();
        float cardPadding = dpi(app, 8.0f);

        // "Follow Windows" toggle (label + switch, must match render geometry)
        {
            float togW = dpi(app, 34.0f);
            float togH = dpi(app, 18.0f);
            float togX = panelX + panelWidth - dpi(app, 30.0f) - togW;
            float togY = panelY + dpi(app, 24.0f);
            if (clickX >= togX - dpi(app, 130.0f) && clickX <= togX + togW &&
                clickY >= togY - dpi(app, 4.0f) && clickY <= togY + togH + dpi(app, 4.0f)) {
                app.followSystemTheme = !app.followSystemTheme;
                if (app.followSystemTheme) {
                    // Adopt the current theme as this mode's preference, then
                    // snap to whatever the system mode wants
                    if (app.theme.isDark) app.darkThemeIndex = app.currentThemeIndex;
                    else app.lightThemeIndex = app.currentThemeIndex;
                    PostMessageW(hwnd, WM_SETTINGCHANGE, 0,
                                 (LPARAM)L"ImmersiveColorSet");
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }

        int clickedTheme = -1;
        for (int i = 0; i < themeCount(); i++) {
            int col, row;
            themeChooserCell(i, col, row);

            float cardX = panelX + dpi(app, 20.0f) + col * (cardWidth + dpi(app, 20.0f));
            float cardY = gridStartY + row * cardHeight;
            float innerX = cardX + cardPadding;
            float innerY = cardY + cardPadding;
            float innerW = cardWidth - cardPadding * 2;
            float innerH = cardHeight - cardPadding * 2;

            if (clickX >= innerX && clickX <= innerX + innerW &&
                clickY >= innerY && clickY <= innerY + innerH) {
                clickedTheme = i;
                break;
            }
        }

        if (clickedTheme >= 0) {
            applyTheme(app, clickedTheme);
            // While auto mode is on, picking a card records it as the
            // preference for that card's light/dark class
            if (app.followSystemTheme) {
                if (themeAt(clickedTheme).isDark) app.darkThemeIndex = clickedTheme;
                else app.lightThemeIndex = clickedTheme;
            }
            app.showThemeChooser = false;
            app.themeChooserAnimation = 0;
        }
        // If clicked outside themes, just close the chooser
        else {
            app.showThemeChooser = false;
            app.themeChooserAnimation = 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (app.scrollbarDragging) {
        app.scrollbarDragging = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (app.hScrollbarDragging) {
        app.hScrollbarDragging = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (codeCopyButtonAt(app, app.mouseX, app.mouseY)) {
        // Releasing anywhere else on a code block must NOT consume the
        // mouse-up: a drag selection ending on the block would never
        // finalize, stay "selecting", and follow the next mouse move
        copyToClipboard(hwnd, app.codeBlocks[app.hoveredCodeBlock].codeText);
        app.showCopiedNotification = true;
        app.copiedNotificationStart = std::chrono::steady_clock::now();
        startNotificationTimer(app);
        app.hoveredCodeBlock = -1;
        app.selecting = false;
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (app.selecting) {
        // Finalize selection based on mode
        if (app.selectionMode == App::SelectionMode::Word ||
            app.selectionMode == App::SelectionMode::Line) {
            // Word/Line selection: keep the bounds set during mouse down/move
            // hasSelection was already set to true in WM_LBUTTONDOWN
        } else {
            // Normal selection: finalize with current mouse position (document coordinates)
            float previewOffsetX = documentViewportX(app);
            float docX = (app.mouseX - previewOffsetX) + app.scrollX;
            float docY = app.mouseY + app.scrollY;
            app.selEndX = (int)docX;
            app.selEndY = (int)docY;

            // Check if this was a meaningful drag (not just a click)
            // Use screen coordinates stored from mouse down
            int dx = abs(app.mouseX - app.lastClickX);
            int dy = abs(app.mouseY - app.lastClickY);
            if (dx > 5 || dy > 5) {
                app.hasSelection = true;
            } else if (const App::TaskRect* task = taskRectAt(app)) {
                // Click on a task checkbox: flip the mark in the file
                toggleTaskOnDisk(app, hwnd, task->markOffset, task->checked);
                app.hasSelection = false;
            } else if (!app.hoveredLink.empty()) {
                // It was just a click on a link
                if (app.hoveredLink.rfind("wiki:", 0) == 0) {
                    openWikiLink(app, app.hoveredLink.substr(5));
                } else {
                    handleLinkClick(app);
                }
                app.hasSelection = false;
            } else {
                app.hasSelection = false;
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (!app.hoveredLink.empty()) {
        // Click on link
        handleLinkClick(app);
    }

    app.mouseDown = false;
    app.selecting = false;
}

// Zen mode: borderless fullscreen + centered reading column (F11)
static void toggleZenMode(App& app, HWND hwnd) {
    app.zenMode = !app.zenMode;
    if (app.zenMode) {
        app.zenRestorePlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &app.zenRestorePlacement);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(hwnd, &app.zenRestorePlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    app.layoutDirty = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void handleKeyDown(App& app, HWND hwnd, WPARAM wParam) {
    float pageSize = app.height * 0.8f;
    float maxScroll = std::max(0.0f, app.contentHeight - app.height);
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    // Print preview captures the whole keyboard while open
    if (app.showPrintPreview) {
        switch (wParam) {
            case VK_ESCAPE:
                closePrintPreview(app, hwnd);
                break;
            case VK_RETURN:
                printPreviewConfirm(app, hwnd);
                break;
            case 'P':  // a second Ctrl+P also proceeds to the dialog
                if (ctrl) printPreviewConfirm(app, hwnd);
                break;
            case VK_NEXT: case VK_DOWN: case VK_RIGHT: case VK_SPACE: case 'J':
                printPreviewSetPage(app, app.printPreviewPage + 1);
                break;
            case VK_PRIOR: case VK_UP: case VK_LEFT: case 'K':
                printPreviewSetPage(app, app.printPreviewPage - 1);
                break;
            case VK_HOME:
                printPreviewSetPage(app, 0);
                break;
            case VK_END:
                printPreviewSetPage(app, (int)app.printPreviewBounds.size() - 2);
                break;
        }
        return;
    }

    // Edit mode: Ctrl+C with preview pane selection should copy from preview
    if (app.editMode) {
        if (ctrl && wParam == 'C' && app.hasSelection && !app.selectedText.empty()) {
            copyToClipboard(hwnd, app.selectedText);
            app.showCopiedNotification = true;
            app.copiedNotificationStart = std::chrono::steady_clock::now();
            startNotificationTimer(app);
            app.hasSelection = false;
            app.selectedText.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        handleEditorKeyDown(app, hwnd, wParam);
        return;
    }

    // Folder browser path/name input captures the keyboard while active
    // (printable characters arrive separately via WM_CHAR)
    if (app.showFolderBrowser &&
        (app.folderBrowserEditingPath || app.folderBrowserNaming != 0)) {
        if (ctrl && wParam == 'V') {
            std::wstring pasted = clipboardLine(hwnd);
            if (!pasted.empty()) {
                if (app.folderBrowserInputSelectAll) {
                    app.folderBrowserInput.clear();
                    app.folderBrowserInputSelectAll = false;
                }
                app.folderBrowserInput += pasted;
                app.folderBrowserInputError = false;
                resetCursorBlink(app);
            }
        } else if (ctrl && wParam == 'A') {
            if (!app.folderBrowserInput.empty()) app.folderBrowserInputSelectAll = true;
        } else if (!ctrl) {
            switch (wParam) {
                case VK_ESCAPE:
                    closeFolderBrowserInput(app);
                    break;
                case VK_RETURN:
                    if (app.folderBrowserEditingPath) commitFolderBrowserPath(app);
                    else commitFolderBrowserNaming(app);
                    break;
                case VK_BACK:
                    if (app.folderBrowserInputSelectAll) {
                        app.folderBrowserInput.clear();
                        app.folderBrowserInputSelectAll = false;
                    } else if (!app.folderBrowserInput.empty()) {
                        app.folderBrowserInput.pop_back();
                    }
                    app.folderBrowserInputError = false;
                    resetCursorBlink(app);
                    break;
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // An open context menu takes Esc before any overlay
    if (app.showContextMenu && wParam == VK_ESCAPE) {
        app.showContextMenu = false;
        app.contextMenuAnimation = 0;
        app.hoveredContextMenuItem = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Handle search-specific keys when search is active
    if (app.showSearch && app.searchActive) {
        switch (wParam) {
            case VK_ESCAPE:
                // Close search
                app.showSearch = false;
                app.searchActive = false;
                app.searchQuery.clear();
                app.searchMatches.clear();
                app.searchAnimation = 0;
                clearFolderSearch(app);
                updateBlinkTimer(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case VK_RETURN:
                // Cycle to next match
                if (!app.searchMatches.empty()) {
                    app.searchCurrentIndex = (app.searchCurrentIndex + 1) % (int)app.searchMatches.size();
                    scrollToCurrentMatch(app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return;
            case VK_BACK:
                // Delete last character
                if (!app.searchQuery.empty()) {
                    app.searchQuery.pop_back();
                    resetCursorBlink(app);
                    performSearch(app);
                    if (!app.searchMatches.empty()) {
                        scrollToCurrentMatch(app);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return;
        }
    }

    if (ctrl) {
        switch (wParam) {
            case 'P':
                // Print preview (edit mode handles its own Ctrl+P in the
                // editor path)
                openPrintPreview(app, hwnd);
                break;
            case 'A': {
                // Select All - extract all text from document
                if (app.root) {
                    app.selectedText.clear();
                    extractText(app.root, app.selectedText);
                    app.hasSelection = true;
                    // Equal coords signal select-all to the renderer; clear
                    // any drag range so it doesn't overwrite the selection
                    app.selStartX = app.selEndX = 0;
                    app.selStartY = app.selEndY = 0;
                }
                break;
            }
            case 'C': {
                // Copy - copy selected text or all text if select all was used
                bool copied = false;
                if (app.hasSelection && !app.selectedText.empty()) {
                    copyToClipboard(hwnd, app.selectedText);
                    app.hasSelection = false;
                    app.selectedText.clear();
                    copied = true;
                } else if (app.root) {
                    // If no selection, copy all
                    std::wstring allText;
                    extractText(app.root, allText);
                    copyToClipboard(hwnd, allText);
                    copied = true;
                }
                // Show "Copied!" notification
                if (copied) {
                    app.showCopiedNotification = true;
                    app.copiedNotificationAlpha = 1.0f;
                    app.copiedNotificationStart = std::chrono::steady_clock::now();
                    startNotificationTimer(app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            }
            case VK_OEM_COMMA:
                // Ctrl+, opens settings (viewer mode)
                if (!app.editMode) {
                    closeSearchIfOpen(app);
                    app.showSettings = !app.showSettings;
                    app.settingsAnimation = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            case 'F':
                // Ctrl+F to open search
                if (!app.showSearch) {
                    app.showSearch = true;
                    app.searchActive = true;
                    app.searchAnimation = 0;
                    app.searchQuery.clear();
                    app.searchMatches.clear();
                    app.searchCurrentIndex = 0;
                    app.searchJustOpened = true;
                    updateBlinkTimer(app);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
        }
    } else {
        // Settings overlay captures the keyboard while open
        if (app.showSettings) {
            if (wParam == VK_ESCAPE) {
                app.showSettings = false;
                app.settingsAnimation = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return;
        }
        wParam = translateActionKey(app, wParam, false);
        switch (wParam) {
            case VK_ESCAPE:
                // Priority: ContextMenu > Help > Search > FolderBrowser > TOC > Theme chooser > Quit
                if (app.showContextMenu) {
                    app.showContextMenu = false;
                    app.contextMenuAnimation = 0;
                } else if (app.showHelp) {
                    app.showHelp = false;
                    app.helpAnimation = 0;
                } else if (app.showSearch) {
                    app.showSearch = false;
                    app.searchActive = false;
                    app.searchQuery.clear();
                    app.searchMatches.clear();
                    app.searchAnimation = 0;
                    clearFolderSearch(app);
                    updateBlinkTimer(app);
                } else if (app.showFolderBrowser) {
                    app.showFolderBrowser = false;
                    app.folderBrowserAnimation = 0;
                } else if (app.showToc) {
                    app.showToc = false;
                    app.tocAnimation = 0;
                } else if (app.showThemeChooser) {
                    app.showThemeChooser = false;
                    app.themeChooserAnimation = 0;
                } else if (app.zenMode) {
                    toggleZenMode(app, hwnd);
                } else {
                    PostQuitMessage(0);
                }
                break;
            case VK_F11:
                toggleZenMode(app, hwnd);
                break;
            case 'Q':
                if (!app.showThemeChooser && !app.showSearch && !app.showFolderBrowser && !app.showToc) {
                    PostQuitMessage(0);
                }
                break;
            case 'N':
                // New file: folder browser with the naming row active (#74)
                if (!app.showSearch && !app.showThemeChooser && !app.showToc) {
                    startNewFileFlow(app, hwnd);
                }
                break;
            case 'B':
                // B to toggle folder browser
                if (!app.showSearch && !app.showThemeChooser && !app.showToc) {
                    app.showFolderBrowser = !app.showFolderBrowser;
                    closeFolderBrowserInput(app);
                    if (app.showFolderBrowser) {
                        app.folderBrowserAnimation = 0;
                        // Initialize to directory of current file, or working directory
                        if (!app.currentFile.empty()) {
                            app.folderBrowserPath = getDirectoryFromFile(app.currentFile);
                        } else {
                            wchar_t cwd[MAX_PATH];
                            if (GetCurrentDirectoryW(MAX_PATH, cwd)) {
                                app.folderBrowserPath = cwd;
                            }
                        }
                        populateFolderItems(app);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            case VK_TAB:
                if (!app.showSearch && !app.showThemeChooser && !app.showFolderBrowser) {
                    app.showToc = !app.showToc;
                    if (app.showToc) {
                        ensureLayoutComplete(app);  // headings list is built during layout
                        app.tocAnimation = 0;
                        app.tocScroll = 0;
                        app.hoveredTocIndex = -1;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            case 'T':
                if (!app.showSearch) {
                    app.showThemeChooser = !app.showThemeChooser;
                    if (app.showThemeChooser) {
                        app.themeChooserAnimation = 0;
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
            case 'F':
                // F to open search (when not in search mode)
                if (!app.showSearch && !app.showThemeChooser) {
                    app.showSearch = true;
                    app.searchActive = true;
                    app.searchAnimation = 0;
                    app.searchQuery.clear();
                    app.searchMatches.clear();
                    app.searchCurrentIndex = 0;
                    app.searchJustOpened = true;
                    updateBlinkTimer(app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            case VK_UP:
            case 'K':
                if (!app.showSearch) {
                    app.targetScrollY -= dpi(app, 50.0f);
                }
                break;
            case VK_DOWN:
            case 'J':
                if (!app.showSearch) {
                    app.targetScrollY += dpi(app, 50.0f);
                }
                break;
            case VK_PRIOR: // Page Up
                app.targetScrollY -= pageSize;
                break;
            case VK_NEXT: // Page Down
            case VK_SPACE:
                if (!app.showSearch) {
                    app.targetScrollY += pageSize;
                }
                break;
            case VK_HOME:
                app.targetScrollY = 0;
                break;
            case VK_END:
                // Jump-to-bottom needs the final content height
                ensureLayoutComplete(app);
                maxScroll = std::max(0.0f, app.contentHeight - app.height);
                app.targetScrollY = maxScroll;
                break;
            case 'S':
                if (!app.showSearch) {
                    app.showStats = !app.showStats;
                }
                break;
        }
    }

    app.targetScrollY = std::max(0.0f, std::min(app.targetScrollY, maxScroll));
    app.scrollY = app.targetScrollY;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void handleCharInput(App& app, HWND hwnd, WPARAM wParam) {
    // Print preview: all shortcuts are handled as key-downs
    if (app.showPrintPreview) return;

    // Edit mode: ':' enters edit mode, otherwise route to editor
    if (app.editMode) {
        handleEditorCharInput(app, hwnd, wParam);
        return;
    }

    // ':' to enter edit mode, '?' to toggle help — when no overlay is active
    if (!app.showSearch && !app.showFolderBrowser && !app.showToc && !app.showThemeChooser) {
        wchar_t ch = (wchar_t)translateActionKey(app, wParam, true);
        if (ch == L':' && !app.showHelp) {
            enterEditMode(app);
            return;
        }
        if (ch == L'?') {
            app.showHelp = !app.showHelp;
            if (app.showHelp) {
                app.helpAnimation = 0;
            }
            InvalidateRect(app.hwnd, nullptr, FALSE);
            return;
        }
    }

    if (app.showFolderBrowser &&
        (app.folderBrowserEditingPath || app.folderBrowserNaming != 0)) {
        if (app.folderInputJustOpened) {
            // The keystroke that opened this input must not type into it
            app.folderInputJustOpened = false;
            return;
        }
        wchar_t ch = (wchar_t)wParam;
        if (ch >= 32 && ch != 127) {
            if (app.folderBrowserInputSelectAll) {
                app.folderBrowserInput.clear();
                app.folderBrowserInputSelectAll = false;
            }
            app.folderBrowserInput += ch;
            app.folderBrowserInputError = false;
            resetCursorBlink(app);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    if (app.showSearch && app.searchActive) {
        // Skip the character that opened search (F key)
        if (app.searchJustOpened) {
            app.searchJustOpened = false;
            return;
        }
        wchar_t ch = (wchar_t)wParam;
        // Only handle printable characters (not control chars)
        if (ch >= 32 && ch != 127) {
            app.searchQuery += ch;
            resetCursorBlink(app);
            performSearch(app);
            if (!app.searchMatches.empty()) {
                app.searchCurrentIndex = 0;
                scrollToCurrentMatch(app);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
}

void handleDropFiles(App& app, HWND hwnd, WPARAM wParam) {
    HDROP hDrop = (HDROP)wParam;
    wchar_t wpath[MAX_PATH];
    if (DragQueryFileW(hDrop, 0, wpath, MAX_PATH) &&
        isSupportedDropPath(wpath)) {
        // Convert wide path to UTF-8 for std::string usage
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
        std::string filepath(utf8Len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, &filepath[0], utf8Len, nullptr, nullptr);

        // Load file - use wide path for non-ASCII support
        std::ifstream file(wpath);
        if (file) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            auto result = parseDocument(app.parser, buffer.str(), wpath);
            if (result.success) {
                app.root = result.root;
                app.parseTimeUs = result.parseTimeUs;
                app.currentFile = filepath;
                app.scrollY = 0;
                app.scrollX = 0;
                app.targetScrollY = 0;
                app.targetScrollX = 0;
                app.focusMermaidOnNextLayout = isMermaidDocumentPath(wpath);
                app.contentHeight = 0;
                app.docText.clear();
                app.docTextLower.clear();
                app.searchMatches.clear();
                app.layoutDirty = true;
                updateFileWriteTime(app);
                updateWindowTitle(app);
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    DragFinish(hDrop);
}

void handleFileWatchTimer(App& app, HWND hwnd) {
    if (app.currentFile.empty() || !app.fileWatchEnabled || app.editMode) return;

    std::wstring widePath = toWide(app.currentFile);
    HANDLE h = CreateFileW(widePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        FILETIME ft;
        GetFileTime(h, nullptr, nullptr, &ft);
        CloseHandle(h);
        if (CompareFileTime(&ft, &app.lastFileWriteTime) != 0) {
            app.lastFileWriteTime = ft;
            // Reload file
            std::ifstream file(widePath);
            if (file) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                auto result = parseDocument(app.parser, buffer.str(), app.currentFile);
                if (result.success) {
                    float savedScroll = app.scrollY;
                    float savedTargetScroll = app.targetScrollY;
                    app.root = result.root;
                    app.parseTimeUs = result.parseTimeUs;
                    app.layoutDirty = true;
                    app.scrollY = savedScroll;
                    app.targetScrollY = savedTargetScroll;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
    }
}
