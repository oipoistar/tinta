// Tabbed interface (design doc t3): Win11 Notepad-style tabs in a custom
// title bar. The strip is the caption -- the active tab lifts to the
// document surface with an accent underline, unsaved buffers show a dot,
// a chevron opens the open-files switcher when the strip is crowded.

#include "tabs.h"

#include "document.h"
#include "drafts.h"
#include "signals.h"
#include "editor.h"
#include "startpage.h"
#include "file_utils.h"
#include "i18n.h"
#include "input.h"
#include "utils.h"

#include <algorithm>
#include <cstdint>
#include <shellapi.h>

namespace {

constexpr int TIMER_FILE_WATCH_ID = 1;
constexpr int TIMER_EDITOR_REPARSE_ID = 2;  // matches editor.cpp

std::wstring titleForPath(App& app, const std::string& path) {
    if (path.empty()) return tr(app, "title.untitled");
    std::wstring wide = toWide(path);
    size_t lastSep = wide.find_last_of(L"\\/");
    return lastSep == std::wstring::npos ? wide : wide.substr(lastSep + 1);
}

// Keep the active tab's bookkeeping in sync with the app state (path can
// change when a quick note is saved; dirty drives the unsaved dot)
void syncActiveTab(App& app) {
    if (app.tabs.empty()) return;
    if (app.activeTab < 0 || app.activeTab >= (int)app.tabs.size()) {
        app.activeTab = 0;
    }
    App::DocTab& tab = app.tabs[app.activeTab];
    tab.path = app.currentFile;
    // A pathless viewer tab with a custom title (an embedded Learn
    // document from the start page) keeps it; quick notes re-derive
    if (!app.currentFile.empty() || tab.title.empty() || app.editMode) {
        tab.title = titleForPath(app, app.currentFile);
    }
    tab.editMode = app.editMode;
    tab.editorDirty = app.editMode && app.editorDirty;
}

// Park the active editor buffer into its tab so the switch away loses
// nothing: the teardown mirrors exitEditMode minus the dialog and reload
// (another document loads immediately after).
void parkActiveEditBuffer(App& app) {
    if (!app.editMode || app.tabs.empty()) return;
    App::DocTab& tab = app.tabs[app.activeTab];
    tab.editMode = true;
    tab.editorDirty = app.editorDirty;
    tab.editorText = std::move(app.editorText);
    tab.editorScrollY = app.editorScrollY;
    tab.editorCursor = app.editorCursorPos;
    tab.wordWrap = app.editorWordWrap;
    tab.lastWrite = app.lastFileWriteTime;  // external-change detection

    app.editMode = false;
    app.editorDirty = false;
    app.clearEditorLineLayoutCache();
    app.editorText.clear();
    app.editorLineStarts.clear();
    app.undoStack.clear();
    app.redoStack.clear();
    app.editorSearchMatches.clear();
    app.editorSearchCurrentIndex = 0;
    app.editorHasSelection = false;
    app.confirmExitPending = false;
    if (app.showSearch) {
        app.showSearch = false;
        app.searchActive = false;
        app.searchQuery.clear();
        app.searchAnimation = 0;
    }
    KillTimer(app.hwnd, TIMER_EDITOR_REPARSE_ID);
    updateBlinkTimer(app);
    SetTimer(app.hwnd, TIMER_FILE_WATCH_ID, 500, nullptr);
}

}  // namespace

void tabsInit(App& app) {
    if (!app.tabs.empty()) return;
    App::DocTab tab;
    tab.id = ++app.tabIdCounter;
    tab.path = app.currentFile;
    tab.title = titleForPath(app, app.currentFile);
    // A pathless viewer tab is the start page, not an untitled note
    if (app.currentFile.empty() && !app.editMode) tab.title = L"Tinta";
    app.tabs.push_back(std::move(tab));
    app.activeTab = 0;
}

void tabsSeedSession(App& app, const std::vector<std::string>& paths) {
    // Rebuilds the tab row from a saved session, in order; the already
    // loaded startup document becomes the active tab (appended if it was
    // not part of the session, e.g. a file-argument launch)
    app.tabs.clear();
    app.activeTab = -1;
    for (const auto& path : paths) {
        App::DocTab tab;
        tab.id = ++app.tabIdCounter;
        tab.path = path;
        tab.title = titleForPath(app, path);
        if (app.activeTab < 0 &&
            _stricmp(path.c_str(), app.currentFile.c_str()) == 0) {
            app.activeTab = (int)app.tabs.size();
        }
        app.tabs.push_back(std::move(tab));
    }
    if (app.activeTab < 0) {
        App::DocTab tab;
        tab.id = ++app.tabIdCounter;
        tab.path = app.currentFile;
        tab.title = titleForPath(app, app.currentFile);
        app.tabs.push_back(std::move(tab));
        app.activeTab = (int)app.tabs.size() - 1;
    }
}

void tabActivate(App& app, HWND hwnd, int index) {
    tabsInit(app);
    if (index < 0 || index >= (int)app.tabs.size()) return;
    if (index == app.activeTab) return;

    // Leaving an embedded Learn document: an empty tab activated later
    // shows the launcher again
    app.startPageEmbeddedOpen = false;

    syncActiveTab(app);
    parkActiveEditBuffer(app);
    app.activeTab = index;
    App::DocTab& tab = app.tabs[index];

    if (!tab.path.empty()) {
        // openDocumentInViewer saves the old reading position and restores
        // the new one; parse + viewport-first layout makes this feel instant
        openDocumentInViewer(app, toWide(tab.path));
    } else {
        // Untitled note: swap in an empty document so the preview pane
        // does not keep showing the previous tab's content
        app.currentFile.clear();
        auto result = parseDocument(app.parser, std::string(), app.currentFile);
        if (result.success) {
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
        }
        app.scrollY = app.targetScrollY = 0;
        app.scrollX = app.targetScrollX = 0;
        app.layoutDirty = true;
        // An empty viewer tab in a launcher window shows the start page
        // again; its label follows suit
        if (!tab.editMode && startPageActive(app)) {
            tab.title = L"Tinta";
        }
    }
    if (tab.editMode) {
        app.editorWordWrap = tab.wordWrap;
        restoreEditBuffer(app, tab.editorText, tab.editorDirty,
                          tab.editorScrollY, tab.editorCursor);
        tab.editorText.clear();  // ownership moved back to the app
    }
    app.showTabSwitcher = false;
    updateWindowTitle(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void tabOpenPath(App& app, HWND hwnd, const std::string& utf8Path,
                 bool activate) {
    tabsInit(app);
    // A path that is already open switches to its tab instead of duplicating
    for (size_t i = 0; i < app.tabs.size(); i++) {
        const std::string& existing =
            (int)i == app.activeTab ? app.currentFile : app.tabs[i].path;
        if (!utf8Path.empty() && _stricmp(existing.c_str(),
                                          utf8Path.c_str()) == 0) {
            if (activate) tabActivate(app, hwnd, (int)i);
            return;
        }
    }
    App::DocTab tab;
    tab.id = ++app.tabIdCounter;
    tab.path = utf8Path;
    tab.title = titleForPath(app, utf8Path);
    app.tabs.push_back(std::move(tab));
    if (activate) {
        tabActivate(app, hwnd, (int)app.tabs.size() - 1);
    } else {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void tabCloseIndex(App& app, HWND hwnd, int index) {
    tabsInit(app);
    if (index < 0 || index >= (int)app.tabs.size()) return;

    // A dirty buffer routes through the unsaved-changes dialog (#106):
    // activate the tab, open the dialog, and finish the close from
    // confirmExitAction once the user decides
    bool dirty = (index == app.activeTab)
                     ? (app.editMode && app.editorDirty)
                     : (app.tabs[index].editMode && app.tabs[index].editorDirty);
    if (dirty) {
        if (index != app.activeTab) tabActivate(app, hwnd, index);
        app.pendingTabClose = app.activeTab;
        app.confirmExitPending = true;
        app.confirmExitOpenedAt = std::chrono::steady_clock::now();
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // The closed tab's crash-recovery draft goes with it
    draftsDeleteForTab(app, app.tabs[index].id);

    if ((int)app.tabs.size() <= 1) {
        // The last tab closes into the start page instead of taking the
        // window with it; closing the start page itself closes the window
        if (startPageActive(app)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else {
            tabBecomeStartPage(app, hwnd);
        }
        return;
    }

    if (index == app.activeTab) {
        // Leave a clean edit session silently before the neighbor loads
        if (app.editMode) {
            parkActiveEditBuffer(app);
            app.tabs[index].editMode = false;
            app.tabs[index].editorText.clear();
        }
        int next = index + 1 < (int)app.tabs.size() ? index + 1 : index - 1;
        // Activate first (so state lands in the surviving tab), then drop
        tabActivate(app, hwnd, next);
        int erase = index;
        app.tabs.erase(app.tabs.begin() + erase);
        if (app.activeTab > erase) app.activeTab--;
    } else {
        app.tabs.erase(app.tabs.begin() + index);
        if (app.activeTab > index) app.activeTab--;
    }
    app.hoveredTab = -1;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void tabCycle(App& app, HWND hwnd, int direction) {
    tabsInit(app);
    if (app.tabs.size() < 2) return;
    int count = (int)app.tabs.size();
    int next = (app.activeTab + direction + count) % count;
    tabActivate(app, hwnd, next);
}

// The lone surviving tab turns into the start page in place: closing
// the last document lands on the launcher instead of the window
// vanishing. Idempotent — a tab that is already the launcher stays one.
void tabBecomeStartPage(App& app, HWND hwnd) {
    tabsInit(app);
    parkActiveEditBuffer(app);  // clean editor teardown if one was open
    App::DocTab& tab = app.tabs[app.activeTab];
    draftsDeleteForTab(app, tab.id);
    tab.path.clear();
    tab.title = L"Tinta";
    tab.editMode = false;
    tab.editorDirty = false;
    tab.editorText.clear();
    app.currentFile.clear();
    auto result = parseDocument(app.parser, std::string(), app.currentFile);
    if (result.success) {
        app.root = result.root;
        app.parseTimeUs = result.parseTimeUs;
    }
    app.scrollY = app.targetScrollY = 0;
    app.scrollX = app.targetScrollX = 0;
    app.layoutDirty = true;
    app.startPageEmbeddedOpen = false;
    app.hoveredTab = -1;
    updateWindowTitle(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Ctrl+T: a fresh empty viewer tab, which is the start page (the
// browser-style new-tab page with recents and open actions)
void tabOpenStartPage(App& app, HWND hwnd) {
    tabsInit(app);
    syncActiveTab(app);
    parkActiveEditBuffer(app);
    App::DocTab tab;
    tab.id = ++app.tabIdCounter;
    tab.title = L"Tinta";
    app.tabs.push_back(std::move(tab));
    app.activeTab = (int)app.tabs.size() - 1;
    app.currentFile.clear();
    auto result = parseDocument(app.parser, std::string(), app.currentFile);
    if (result.success) {
        app.root = result.root;
        app.parseTimeUs = result.parseTimeUs;
    }
    app.scrollY = app.targetScrollY = 0;
    app.scrollX = app.targetScrollX = 0;
    app.layoutDirty = true;
    app.startPageEmbeddedOpen = false;
    app.showTabSwitcher = false;
    updateWindowTitle(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void tabOpenQuickNote(App& app, HWND hwnd) {
    // The + button behaves like Ctrl+N, in-window: a fresh untitled
    // quick-note tab straight into the editor; the first Ctrl+S runs the
    // classic Save As flow and the tab picks up the chosen name
    tabsInit(app);
    syncActiveTab(app);
    parkActiveEditBuffer(app);
    App::DocTab tab;
    tab.id = ++app.tabIdCounter;
    tab.title = tr(app, "title.untitled");
    app.tabs.push_back(std::move(tab));
    app.activeTab = (int)app.tabs.size() - 1;
    // The preview pane renders app.root: swap in an empty document so the
    // fresh note does not preview the previous tab's content
    app.currentFile.clear();
    auto result = parseDocument(app.parser, std::string(), app.currentFile);
    if (result.success) {
        app.root = result.root;
        app.parseTimeUs = result.parseTimeUs;
    }
    enterQuickNoteMode(app);
    app.showTabSwitcher = false;
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- geometry ---

namespace {

float captionButtonWidth(const App& app) { return dpi(app, 46.0f); }
// Always-on-top pin, left of the caption buttons
float pinButtonWidth(const App& app) { return dpi(app, 34.0f); }
D2D1_RECT_F pinButtonRect(const App& app) {
    float right = (float)app.width - captionButtonWidth(app) * 3.0f;
    return D2D1::RectF(right - pinButtonWidth(app), 0.0f, right,
                       chromeTopHeight(app));
}

struct StripMetrics {
    float height = 0.0f;
    float tabsLeft = 0.0f;    // after the icon cell
    float tabsRight = 0.0f;   // before + / chevron / caption buttons
    float tabWidth = 0.0f;
    bool compressed = false;  // chevron shown
    float plusX = 0.0f;
    float chevronX = 0.0f;
};

StripMetrics stripMetrics(const App& app) {
    StripMetrics m;
    m.height = chromeTopHeight(app);
    // Tabs clear the edit-mode tool rail instead of hiding under it
    m.tabsLeft = std::max(dpi(app, 40.0f),
                          editRailWidth(app) + dpi(app, 8.0f));
    float buttons = captionButtonWidth(app) * 3.0f + pinButtonWidth(app);
    float plusW = dpi(app, 30.0f);
    float chevronW = dpi(app, 32.0f);
    float gap = dpi(app, 2.0f);
    size_t count = std::max<size_t>(app.tabs.size(), 1);

    // With the floating sheet the tab row lives above the source column
    // only; the caption buttons float on the sheet as their own island
    float rowRight = editorPreviewVisible(app)
                         ? editorPaneWidth(app)
                         : (float)app.width - buttons;
    float available = rowRight - m.tabsLeft - plusW - dpi(app, 16.0f);
    float natural = dpi(app, 190.0f);
    float minimum = dpi(app, 60.0f);
    float per = (available - gap * (count - 1)) / (float)count;
    m.tabWidth = std::max(minimum, std::min(natural, per));
    m.compressed = per < dpi(app, 150.0f);
    if (m.compressed) {
        available -= chevronW + gap;
        per = (available - gap * (count - 1)) / (float)count;
        m.tabWidth = std::max(minimum, std::min(natural, per));
    }
    m.tabsRight = m.tabsLeft + (m.tabWidth + gap) * count - gap;
    m.plusX = m.tabsRight + dpi(app, 4.0f);
    m.chevronX = m.plusX + plusW + gap;
    return m;
}

}  // namespace

D2D1_RECT_F captionButtonRect(const App& app, int button) {
    float w = captionButtonWidth(app);
    float right = (float)app.width - w * (2 - button);
    return D2D1::RectF(right - w, 0.0f, right, chromeTopHeight(app));
}

// Left edge of the caption island (pin + window buttons); in sheet mode
// clicks left of this and right of the source column belong to the page
float captionIslandLeft(const App& app) {
    return (float)app.width - captionButtonWidth(app) * 3.0f -
           pinButtonWidth(app) - dpi(app, 8.0f);
}

int captionHitTest(const App& app, float x, float y) {
    if (y < 0.0f || y >= chromeTopHeight(app)) return 0;
    for (int i = 0; i < 3; i++) {
        D2D1_RECT_F r = captionButtonRect(app, i);
        if (x >= r.left && x < r.right) return i + 1;
    }
    return 0;
}

// --- rendering ---

D2D1_COLOR_F tabStripBackground(const App& app) {
    D2D1_COLOR_F c = app.theme.background;
    float f = app.theme.isDark ? 0.78f : 0.955f;
    c.r *= f;
    c.g *= f;
    c.b *= f;
    return c;
}

namespace {

D2D1_COLOR_F stripBackground(const App& app) {
    return tabStripBackground(app);
}

// Rounded-top tab body: rounded rect extended past the strip bottom, the
// caller clips to the strip so the bottom corners stay square
void fillTabBody(App& app, const D2D1_RECT_F& r, float radius,
                 const D2D1_COLOR_F& color) {
    app.brush->SetColor(color);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(r.left, r.top, r.right, r.bottom + radius * 2.0f),
            radius, radius),
        app.brush);
}

void drawCloseGlyph(App& app, float cx, float cy, float arm,
                    const D2D1_COLOR_F& color) {
    app.brush->SetColor(color);
    app.renderTarget->DrawLine(D2D1::Point2F(cx - arm, cy - arm),
                               D2D1::Point2F(cx + arm, cy + arm),
                               app.brush, dpi(app, 1.1f));
    app.renderTarget->DrawLine(D2D1::Point2F(cx - arm, cy + arm),
                               D2D1::Point2F(cx + arm, cy - arm),
                               app.brush, dpi(app, 1.1f));
}

// The + new-tab button, shared by the full strip and the tabless title
// (where it is the visible hint that the window can hold tabs)
void drawPlusButton(App& app, float plusX, float stripH,
                    const D2D1_COLOR_F& faint, const D2D1_COLOR_F& muted) {
    float plusSize = dpi(app, 30.0f);
    float plusY = (stripH - plusSize) * 0.5f;
    D2D1_RECT_F plusRect = D2D1::RectF(plusX, plusY, plusX + plusSize,
                                       plusY + plusSize);
    bool plusHover = app.mouseX >= plusRect.left &&
                     app.mouseX <= plusRect.right &&
                     app.mouseY >= plusRect.top &&
                     app.mouseY <= plusRect.bottom;
    if (plusHover) {
        app.brush->SetColor(faint);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(plusRect, dpi(app, 6.0f), dpi(app, 6.0f)),
            app.brush);
    }
    app.brush->SetColor(muted);
    float pcx = (plusRect.left + plusRect.right) * 0.5f;
    float pcy = (plusRect.top + plusRect.bottom) * 0.5f;
    float parm = dpi(app, 5.0f);
    app.renderTarget->DrawLine(D2D1::Point2F(pcx - parm, pcy),
                               D2D1::Point2F(pcx + parm, pcy), app.brush,
                               dpi(app, 1.2f));
    app.renderTarget->DrawLine(D2D1::Point2F(pcx, pcy - parm),
                               D2D1::Point2F(pcx, pcy + parm), app.brush,
                               dpi(app, 1.2f));
    App::TabHit plusHit;
    plusHit.rect = plusRect;
    plusHit.index = -2;
    app.tabHits.push_back(plusHit);
}

}  // namespace

void renderTabStrip(App& app) {
    float stripH = chromeTopHeight(app);
    if (stripH <= 0.0f) return;
    tabsInit(app);
    syncActiveTab(app);

    D2D1_COLOR_F text = app.theme.text;
    D2D1_COLOR_F muted = text;
    muted.a = 0.7f;
    D2D1_COLOR_F faint = text;
    faint.a = 0.06f;

    // Strip background: with the floating sheet the strip only spans the
    // source side — the desk and the sheet own the top band to its right
    bool sheetMode = editorPreviewVisible(app);
    float stripRight = sheetMode ? editorPaneWidth(app) : (float)app.width;
    app.brush->SetColor(stripBackground(app));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, stripRight, stripH), app.brush);

    // App icon (16px, centered in a 40px cell)
    float iconCell = dpi(app, 40.0f);
    if (!app.titleIconBitmap && app.wicFactory && app.renderTarget) {
        HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), L"IDI_ICON1",
                                       IMAGE_ICON, 32, 32, 0);
        if (icon) {
            IWICBitmap* wicBitmap = nullptr;
            if (SUCCEEDED(app.wicFactory->CreateBitmapFromHICON(
                    icon, &wicBitmap)) && wicBitmap) {
                IWICFormatConverter* converter = nullptr;
                if (SUCCEEDED(app.wicFactory->CreateFormatConverter(
                        &converter)) && converter) {
                    if (SUCCEEDED(converter->Initialize(
                            wicBitmap, GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapDitherTypeNone, nullptr, 0.0,
                            WICBitmapPaletteTypeCustom))) {
                        app.renderTarget->CreateBitmapFromWicBitmap(
                            converter, nullptr, &app.titleIconBitmap);
                    }
                    converter->Release();
                }
                wicBitmap->Release();
            }
            DestroyIcon(icon);
        }
    }
    if (app.titleIconBitmap) {
        float iconSize = dpi(app, 16.0f);
        float ix = (iconCell - iconSize) * 0.5f;
        float iy = (stripH - iconSize) * 0.5f;
        app.renderTarget->DrawBitmap(
            app.titleIconBitmap,
            D2D1::RectF(ix, iy, ix + iconSize, iy + iconSize));
    }

    app.tabHits.clear();

    bool tabless = !tabStripVisible(app);
    if (tabless) {
        // Single document: the caption shows the plain window title, with
        // the + button right after it so the tab row is discoverable
        std::wstring title = L"Tinta";
        if (!app.tabs.empty() && !app.tabs[0].title.empty() &&
            !startPageActive(app)) {
            title = app.tabs[0].title;
            if (app.editMode && app.editorDirty) title = L"* " + title;
        }
        float textLeft = std::max(iconCell + dpi(app, 4.0f),
                                  editRailWidth(app) + dpi(app, 8.0f));
        float textRight = textLeft;
        float maxRight = sheetMode
                             ? editorPaneWidth(app) - dpi(app, 8.0f)
                             : (float)app.width -
                                   captionButtonWidth(app) * 3 -
                                   pinButtonWidth(app);
        if (app.folderBrowserFormat) {
            app.brush->SetColor(muted);
            app.renderTarget->DrawText(
                title.c_str(), (UINT32)title.size(), app.folderBrowserFormat,
                D2D1::RectF(textLeft, (stripH - dpi(app, 17.0f)) * 0.5f,
                            maxRight, stripH),
                app.brush);
            IDWriteTextLayout* layout = nullptr;
            app.dwriteFactory->CreateTextLayout(
                title.c_str(), (UINT32)title.size(), app.folderBrowserFormat,
                std::max(1.0f, maxRight - textLeft), stripH, &layout);
            if (layout) {
                DWRITE_TEXT_METRICS tm{};
                layout->GetMetrics(&tm);
                textRight += tm.width;
                layout->Release();
            }
        }
        // A document's lone tab still closes: a small x after the title
        // lands on the start page. The launcher itself shows none - the
        // window's own close button is the way out there.
        float nextX = textRight + dpi(app, 10.0f);
        if (!startPageActive(app)) {
            float cbSize = dpi(app, 20.0f);
            float cbX = std::min(nextX, maxRight - dpi(app, 38.0f) -
                                            cbSize - dpi(app, 6.0f));
            float cbY = (stripH - cbSize) * 0.5f;
            D2D1_RECT_F cb =
                D2D1::RectF(cbX, cbY, cbX + cbSize, cbY + cbSize);
            bool closeHover =
                app.mouseX >= cb.left && app.mouseX <= cb.right &&
                app.mouseY >= cb.top && app.mouseY <= cb.bottom;
            if (closeHover) {
                D2D1_COLOR_F cbBg = text;
                cbBg.a = 0.12f;
                app.brush->SetColor(cbBg);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(cb, dpi(app, 4.0f), dpi(app, 4.0f)),
                    app.brush);
            }
            drawCloseGlyph(app, (cb.left + cb.right) * 0.5f,
                           (cb.top + cb.bottom) * 0.5f, dpi(app, 3.6f),
                           muted);
            App::TabHit hit;
            hit.rect = cb;
            hit.index = 0;
            hit.closeRect = cb;
            hit.hasClose = true;
            app.tabHits.push_back(hit);
            nextX = cb.right + dpi(app, 6.0f);
        }
        float plusX = std::min(nextX, maxRight - dpi(app, 38.0f));
        drawPlusButton(app, plusX, stripH, faint, muted);
    } else {
        StripMetrics m = stripMetrics(app);
        float radius = dpi(app, 8.0f);
        float gap = dpi(app, 2.0f);

        app.renderTarget->PushAxisAlignedClip(
            D2D1::RectF(0, 0, stripRight, stripH),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        for (size_t i = 0; i < app.tabs.size(); i++) {
            // Pulled clear of the strip: the slot stays as a gap while
            // the tab rides along as the ghost card
            if (app.tabDragDetached && (int)i == app.tabDragIndex) continue;
            bool active = (int)i == app.activeTab;
            bool hovered = (int)i == app.hoveredTab;
            float x = m.tabsLeft + (m.tabWidth + gap) * i;
            if (app.tabDragging && (int)i == app.tabDragIndex) {
                // The dragged tab follows the pointer within the row
                float follow = (float)app.mouseX - app.tabDragOffsetX;
                x = std::max(m.tabsLeft,
                             std::min(follow, m.tabsRight - m.tabWidth));
            }
            float top = active ? dpi(app, 6.0f) : dpi(app, 10.0f);
            D2D1_RECT_F r = D2D1::RectF(x, top, x + m.tabWidth, stripH);

            if (active) {
                // The active tab drops a soft shadow onto its neighbors
                // instead of an underline: layered halos, clipped to the
                // strip so they spread sideways over the inactive tabs
                float shadowAlpha = app.theme.isDark ? 0.34f : 0.16f;
                for (int ring = 3; ring >= 1; ring--) {
                    float spread = dpi(app, (float)ring * 2.2f);
                    D2D1_COLOR_F shadow =
                        D2D1::ColorF(0.0f, 0.0f, 0.0f,
                                     shadowAlpha / (float)(ring * ring));
                    fillTabBody(app,
                                D2D1::RectF(r.left - spread,
                                            r.top - spread * 0.6f,
                                            r.right + spread, r.bottom),
                                radius + spread * 0.5f, shadow);
                }
                fillTabBody(app, r, radius, app.theme.background);
            } else if (hovered) {
                fillTabBody(app, r, radius, faint);
            }

            bool dirty = active ? (app.editMode && app.editorDirty)
                                : (app.tabs[i].editMode &&
                                   app.tabs[i].editorDirty);
            bool missing = app.tabs[i].fileMissing && !app.tabs[i].path.empty();
            bool showClose = active || hovered;
            // Status dot: orange = unsaved changes, red-grey = the file
            // vanished from disk
            bool showDot = dirty || missing;
            D2D1_COLOR_F dotColor =
                dirty ? D2D1::ColorF(0.94f, 0.56f, 0.12f)
                      : D2D1::ColorF(0.74f, 0.36f, 0.34f);

            // Label, ellipsis-trimmed, leaving room for the close/dot
            float labelRight = r.right - dpi(app, showClose ? 30.0f : 20.0f);
            if (app.folderBrowserFormat) {
                app.brush->SetColor(active ? text : muted);
                app.renderTarget->DrawText(
                    app.tabs[i].title.c_str(), (UINT32)app.tabs[i].title.size(),
                    app.folderBrowserFormat,
                    D2D1::RectF(r.left + dpi(app, 12.0f),
                                top + (stripH - top - dpi(app, 17.0f)) * 0.5f,
                                labelRight, stripH),
                    app.brush);
            }

            App::TabHit hit;
            hit.rect = r;
            hit.index = (int)i;
            if (showClose) {
                float cbSize = dpi(app, 20.0f);
                float cbX = r.right - cbSize - dpi(app, 6.0f);
                float cbY = top + (stripH - top - cbSize) * 0.5f;
                D2D1_RECT_F cb = D2D1::RectF(cbX, cbY, cbX + cbSize,
                                             cbY + cbSize);
                bool closeHover =
                    app.mouseX >= cb.left && app.mouseX <= cb.right &&
                    app.mouseY >= cb.top && app.mouseY <= cb.bottom;
                if (closeHover || !active) {
                    D2D1_COLOR_F cbBg = text;
                    cbBg.a = closeHover ? 0.12f : 0.07f;
                    app.brush->SetColor(cbBg);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(cb, dpi(app, 4.0f), dpi(app, 4.0f)),
                        app.brush);
                }
                if (showDot && !closeHover && !hovered) {
                    // status dot instead of x until the pointer commits
                    app.brush->SetColor(dotColor);
                    float dotR = dpi(app, 3.5f);
                    app.renderTarget->FillEllipse(
                        D2D1::Ellipse(
                            D2D1::Point2F((cb.left + cb.right) * 0.5f,
                                          (cb.top + cb.bottom) * 0.5f),
                            dotR, dotR),
                        app.brush);
                } else {
                    drawCloseGlyph(app, (cb.left + cb.right) * 0.5f,
                                   (cb.top + cb.bottom) * 0.5f,
                                   dpi(app, 3.6f), muted);
                }
                hit.closeRect = cb;
                hit.hasClose = true;
            } else if (showDot) {
                app.brush->SetColor(dotColor);
                float dotR = dpi(app, 3.5f);
                float cx = r.right - dpi(app, 13.0f);
                app.renderTarget->FillEllipse(
                    D2D1::Ellipse(
                        D2D1::Point2F(cx, top + (stripH - top) * 0.5f),
                        dotR, dotR),
                    app.brush);
            }
            app.tabHits.push_back(hit);
        }
        app.renderTarget->PopAxisAlignedClip();

        // + new tab
        drawPlusButton(app, m.plusX, stripH, faint, muted);
        float plusSize = dpi(app, 30.0f);
        float plusY = (stripH - plusSize) * 0.5f;

        // Overflow chevron when compressed
        if (m.compressed) {
            float chW = dpi(app, 30.0f);
            D2D1_RECT_F chRect = D2D1::RectF(m.chevronX, plusY,
                                             m.chevronX + chW,
                                             plusY + plusSize);
            D2D1_COLOR_F chBg = text;
            chBg.a = 0.07f;
            app.brush->SetColor(chBg);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(chRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                app.brush);
            app.brush->SetColor(muted);
            float ccx = (chRect.left + chRect.right) * 0.5f;
            float ccy = (chRect.top + chRect.bottom) * 0.5f - dpi(app, 1.0f);
            float carm = dpi(app, 3.5f);
            app.renderTarget->DrawLine(
                D2D1::Point2F(ccx - carm, ccy),
                D2D1::Point2F(ccx, ccy + carm), app.brush, dpi(app, 1.2f));
            app.renderTarget->DrawLine(
                D2D1::Point2F(ccx, ccy + carm),
                D2D1::Point2F(ccx + carm, ccy), app.brush, dpi(app, 1.2f));
            App::TabHit chHit;
            chHit.rect = chRect;
            chHit.index = -3;
            app.tabHits.push_back(chHit);
        }
    }

    // Caption buttons: minimize, maximize/restore, close. On the
    // floating sheet they ride a rounded island so the page can rise to
    // the window's top edge behind them (design 10a)
    float islandTop = sheetMode ? dpi(app, 6.0f) : 0.0f;
    float islandBottom = sheetMode ? stripH - dpi(app, 6.0f) : stripH;
    if (sheetMode) {
        D2D1_RECT_F island = D2D1::RectF(
            pinButtonRect(app).left - dpi(app, 8.0f), islandTop,
            (float)app.width - dpi(app, 6.0f), islandBottom);
        D2D1_COLOR_F ibg = stripBackground(app);
        ibg.a = 0.94f;
        app.brush->SetColor(ibg);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(island, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush);
        D2D1_COLOR_F ibd = text;
        ibd.a = 0.1f;
        app.brush->SetColor(ibd);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(island, dpi(app, 8.0f), dpi(app, 8.0f)),
            app.brush, 1.0f);
    }
    for (int b = 0; b < 3; b++) {
        D2D1_RECT_F r = captionButtonRect(app, b);
        r.top = islandTop;
        r.bottom = islandBottom;
        if (sheetMode) r.right = std::min(r.right, (float)app.width - dpi(app, 8.0f));
        bool hover = app.captionButtonHover == b + 1;
        bool pressed = app.captionButtonPressed == b + 1;
        if (hover || pressed) {
            D2D1_COLOR_F bg;
            if (b == 2) {
                bg = D2D1::ColorF(0.77f, 0.17f, 0.11f, pressed ? 0.8f : 1.0f);
            } else {
                bg = text;
                bg.a = pressed ? 0.12f : 0.07f;
            }
            app.brush->SetColor(bg);
            if (sheetMode) {
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(r.left + dpi(app, 2.0f),
                                    r.top + dpi(app, 2.0f),
                                    r.right - dpi(app, 2.0f),
                                    r.bottom - dpi(app, 2.0f)),
                        dpi(app, 6.0f), dpi(app, 6.0f)),
                    app.brush);
            } else {
                app.renderTarget->FillRectangle(r, app.brush);
            }
        }
        D2D1_COLOR_F glyph = (b == 2 && (hover || pressed))
                                 ? D2D1::ColorF(1.0f, 1.0f, 1.0f)
                                 : text;
        float cx = (r.left + r.right) * 0.5f;
        float cy = (r.top + r.bottom) * 0.5f;
        app.brush->SetColor(glyph);
        if (b == 0) {
            app.renderTarget->DrawLine(
                D2D1::Point2F(cx - dpi(app, 5.0f), cy),
                D2D1::Point2F(cx + dpi(app, 5.0f), cy), app.brush,
                dpi(app, 1.0f));
        } else if (b == 1) {
            float half = dpi(app, 4.5f);
            if (IsZoomed(app.hwnd)) {
                // restore: two offset squares
                float off = dpi(app, 2.0f);
                app.renderTarget->DrawRectangle(
                    D2D1::RectF(cx - half + off, cy - half - off + dpi(app, 1.0f),
                                cx + half + off - dpi(app, 1.0f), cy + half - off),
                    app.brush, dpi(app, 1.0f));
                app.brush->SetColor(stripBackground(app));
                app.renderTarget->FillRectangle(
                    D2D1::RectF(cx - half, cy - half, cx + half, cy + half),
                    app.brush);
                app.brush->SetColor(glyph);
            }
            app.renderTarget->DrawRectangle(
                D2D1::RectF(cx - half, cy - half, cx + half, cy + half),
                app.brush, dpi(app, 1.0f));
        } else {
            drawCloseGlyph(app, cx, cy, dpi(app, 4.5f), glyph);
        }
    }

    // Always-on-top pin: an upright pushpin, accent-filled while pinned
    {
        D2D1_RECT_F r = pinButtonRect(app);
        r.top = islandTop;
        r.bottom = islandBottom;
        bool hover = (float)app.mouseX >= r.left && (float)app.mouseX < r.right &&
                     (float)app.mouseY >= r.top && (float)app.mouseY < r.bottom;
        if (hover) {
            D2D1_COLOR_F bg = text;
            bg.a = 0.07f;
            app.brush->SetColor(bg);
            if (sheetMode) {
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(r.left + dpi(app, 2.0f),
                                    r.top + dpi(app, 2.0f),
                                    r.right - dpi(app, 2.0f),
                                    r.bottom - dpi(app, 2.0f)),
                        dpi(app, 6.0f), dpi(app, 6.0f)),
                    app.brush);
            } else {
                app.renderTarget->FillRectangle(r, app.brush);
            }
        }
        float cx = (r.left + r.right) * 0.5f;
        float cy = (r.top + r.bottom) * 0.5f - dpi(app, 1.0f);
        D2D1_COLOR_F pin = app.alwaysOnTop ? app.theme.accent : text;
        if (!app.alwaysOnTop && !hover) pin.a = 0.45f;
        app.brush->SetColor(pin);
        float bw = dpi(app, 3.0f);   // body half-width
        float bh = dpi(app, 4.0f);   // body half-height
        D2D1_RECT_F body = D2D1::RectF(cx - bw, cy - bh, cx + bw, cy + bh - 1);
        if (app.alwaysOnTop) {
            app.renderTarget->FillRectangle(body, app.brush);
        } else {
            app.renderTarget->DrawRectangle(body, app.brush, dpi(app, 1.0f));
        }
        // Head bar, flange, and needle
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx - bw - dpi(app, 1.0f), cy - bh),
            D2D1::Point2F(cx + bw + dpi(app, 1.0f), cy - bh), app.brush,
            dpi(app, 1.2f));
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx - bw - dpi(app, 2.0f), cy + bh - 1),
            D2D1::Point2F(cx + bw + dpi(app, 2.0f), cy + bh - 1), app.brush,
            dpi(app, 1.2f));
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx, cy + bh - 1),
            D2D1::Point2F(cx, cy + bh + dpi(app, 4.0f)), app.brush,
            dpi(app, 1.2f));
        App::TabHit pinHit;
        pinHit.rect = r;
        pinHit.index = -4;
        app.tabHits.push_back(pinHit);
    }
}

void renderTabSwitcher(App& app) {
    if (!app.showTabSwitcher || app.tabs.size() < 2) return;
    float stripH = chromeTopHeight(app);
    float width = dpi(app, 280.0f);
    float rowH = dpi(app, 32.0f);
    float headH = dpi(app, 26.0f);
    float padY = dpi(app, 4.0f);
    float height = headH + padY * 2.0f + rowH * (float)app.tabs.size();
    float x = (float)app.width - captionButtonWidth(app) * 3.0f - width -
              dpi(app, 8.0f);
    if (x < dpi(app, 8.0f)) x = dpi(app, 8.0f);
    float y = stripH + dpi(app, 4.0f);

    D2D1_COLOR_F text = app.theme.text;
    D2D1_COLOR_F panel = stripBackground(app);
    panel.a = 0.97f;
    D2D1_COLOR_F border = text;
    border.a = 0.12f;

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(x, y, x + width, y + height), dpi(app, 8.0f),
        dpi(app, 8.0f));
    app.brush->SetColor(panel);
    app.renderTarget->FillRoundedRectangle(rr, app.brush);
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);

    app.tabSwitcherHits.clear();

    // Caps header: OPEN FILES - N
    if (app.themeHeaderFormat) {
        wchar_t header[96];
        swprintf_s(header, _countof(header), L"%ls \u2014 %d",
                   tr(app, "tabs.switcher.header"), (int)app.tabs.size());
        D2D1_COLOR_F headColor = text;
        headColor.a = 0.5f;
        app.brush->SetColor(headColor);
        app.renderTarget->DrawText(
            header, (UINT32)wcslen(header), app.themeHeaderFormat,
            D2D1::RectF(x + dpi(app, 14.0f), y + dpi(app, 9.0f), x + width,
                        y + headH),
            app.brush);
    }

    for (size_t i = 0; i < app.tabs.size(); i++) {
        bool active = (int)i == app.activeTab;
        bool hovered = (int)i == app.tabSwitcherHover;
        float rowY = y + headH + padY + rowH * (float)i;
        D2D1_RECT_F row = D2D1::RectF(x + dpi(app, 4.0f), rowY,
                                      x + width - dpi(app, 4.0f),
                                      rowY + rowH - dpi(app, 2.0f));
        if (active || hovered) {
            D2D1_COLOR_F rowBg = text;
            rowBg.a = active ? 0.09f : 0.05f;
            app.brush->SetColor(rowBg);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(row, dpi(app, 5.0f), dpi(app, 5.0f)),
                app.brush);
        }
        if (active) {
            app.brush->SetColor(app.theme.accent);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(row.left, rowY + dpi(app, 7.0f),
                                row.left + dpi(app, 3.0f),
                                rowY + rowH - dpi(app, 9.0f)),
                    dpi(app, 1.5f), dpi(app, 1.5f)),
                app.brush);
        }
        // Document glyph
        D2D1_COLOR_F glyph = text;
        glyph.a = 0.55f;
        app.brush->SetColor(glyph);
        float gx = row.left + dpi(app, 12.0f);
        float gy = rowY + (rowH - dpi(app, 14.0f)) * 0.5f - dpi(app, 1.0f);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(gx, gy, gx + dpi(app, 11.0f), gy + dpi(app, 14.0f)),
                dpi(app, 2.0f), dpi(app, 2.0f)),
            app.brush, 1.0f);

        bool dirty = active ? (app.editMode && app.editorDirty)
                            : (app.tabs[i].editMode && app.tabs[i].editorDirty);
        bool missing = app.tabs[i].fileMissing && !app.tabs[i].path.empty();
        bool showDot = dirty || missing;
        float nameRight = row.right - dpi(app, showDot ? 66.0f : 56.0f);
        if (app.folderBrowserFormat) {
            D2D1_COLOR_F nameColor = text;
            if (!active) nameColor.a = 0.8f;
            app.brush->SetColor(nameColor);
            app.renderTarget->DrawText(
                app.tabs[i].title.c_str(), (UINT32)app.tabs[i].title.size(),
                app.folderBrowserFormat,
                D2D1::RectF(gx + dpi(app, 19.0f),
                            rowY + (rowH - dpi(app, 17.0f)) * 0.5f - 1.0f,
                            nameRight, rowY + rowH),
                app.brush);
        }
        if (showDot) {
            app.brush->SetColor(dirty ? D2D1::ColorF(0.94f, 0.56f, 0.12f)
                                      : D2D1::ColorF(0.74f, 0.36f, 0.34f));
            float dotR = dpi(app, 3.5f);
            app.renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(row.right - dpi(app, 60.0f),
                                            rowY + rowH * 0.5f - 1.0f),
                              dotR, dotR),
                app.brush);
        }
        // Ctrl+1..9 hint
        if (i < 9 && app.folderBrowserFormat) {
            wchar_t hint[16];
            swprintf_s(hint, _countof(hint), L"Ctrl+%d", (int)i + 1);
            D2D1_COLOR_F hintColor = text;
            hintColor.a = 0.4f;
            app.brush->SetColor(hintColor);
            app.renderTarget->DrawText(
                hint, (UINT32)wcslen(hint), app.folderBrowserFormat,
                D2D1::RectF(row.right - dpi(app, 54.0f),
                            rowY + (rowH - dpi(app, 17.0f)) * 0.5f - 1.0f,
                            row.right - dpi(app, 4.0f), rowY + rowH),
                app.brush);
        }
        app.tabSwitcherHits.push_back({row, (int)i});
    }
}

// --- input ---

bool tabStripMouseDown(App& app, HWND hwnd, int x, int y, bool middle) {
    if ((float)y >= chromeTopHeight(app)) return false;
    for (const App::TabHit& hit : app.tabHits) {
        if (x < hit.rect.left || x > hit.rect.right || y < hit.rect.top ||
            y > hit.rect.bottom) {
            continue;
        }
        if (hit.index == -2) {
            // + = new untitled quick-note tab (Ctrl+T still opens the
            // browser into a new tab for picking an existing file)
            if (!middle) tabOpenQuickNote(app, hwnd);
            return true;
        }
        if (hit.index == -3) {
            if (!middle) {
                app.showTabSwitcher = !app.showTabSwitcher;
                app.tabSwitcherHover = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        }
        if (hit.index == -4) {
            // Pin: toggle always-on-top for this window
            if (!middle) {
                app.alwaysOnTop = !app.alwaysOnTop;
                SetWindowPos(hwnd,
                             app.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        }
        if (middle) {
            tabCloseIndex(app, hwnd, hit.index);
            return true;
        }
        if (hit.hasClose && x >= hit.closeRect.left &&
            x <= hit.closeRect.right && y >= hit.closeRect.top &&
            y <= hit.closeRect.bottom) {
            tabCloseIndex(app, hwnd, hit.index);
            return true;
        }
        tabActivate(app, hwnd, hit.index);
        // Arm a potential drag: moving past the threshold starts
        // reordering, pulling away from the strip detaches the tab
        app.tabDragIndex = hit.index;
        app.tabDragging = false;
        app.tabDragOffsetX = (float)x - hit.rect.left;
        app.tabDragStartX = x;
        app.tabDragStartY = y;
        SetCapture(hwnd);
        return true;
    }
    return true;  // strip clicks never fall through to the document
}

bool tabStripVisible(const App& app) {
    return app.tabs.size() > 1 || (app.forceTabStrip && !app.tabs.empty());
}

// --- tab context menu: right-click a tab for NPP-style close operations
// (close, close others, close left/right) plus path utilities ---

namespace {

enum TabMenuItem {
    TM_CLOSE = 0,
    TM_CLOSE_OTHERS,
    TM_CLOSE_LEFT,
    TM_CLOSE_RIGHT,
    TM_COPY_PATH,
    TM_REVEAL,
    TM_ITEM_COUNT,
};

struct TabMenuEntry {
    const char* key;         // translation key
    const wchar_t* shortcut;
    bool separatorAfter;
};

const TabMenuEntry TAB_MENU[TM_ITEM_COUNT] = {
    { "tab.menu.close",        L"Ctrl+W", false },
    { "tab.menu.close_others", L"",       false },
    { "tab.menu.close_left",   L"",       false },
    { "tab.menu.close_right",  L"",       true  },
    { "tab.menu.copy_path",    L"",       false },
    { "ctx.reveal",            L"",       false },
};

float tabMenuItemHeight(const App& app) { return dpi(app, 30.0f); }
float tabMenuSeparator(const App& app) { return dpi(app, 9.0f); }
float tabMenuWidth(const App& app) { return dpi(app, 235.0f); }
float tabMenuPadding(const App& app) { return dpi(app, 6.0f); }

float tabMenuItemTop(const App& app, int index) {
    float y = tabMenuPadding(app);
    for (int i = 0; i < index; i++) {
        y += tabMenuItemHeight(app);
        if (TAB_MENU[i].separatorAfter) y += tabMenuSeparator(app);
    }
    return y;
}

float tabMenuTotalHeight(const App& app) {
    return tabMenuItemTop(app, TM_ITEM_COUNT) + tabMenuPadding(app);
}

bool tabMenuItemEnabled(const App& app, int item) {
    int index = app.tabMenuIndex;
    if (index < 0 || index >= (int)app.tabs.size()) return false;
    switch (item) {
        case TM_CLOSE:        return true;
        case TM_CLOSE_OTHERS: return app.tabs.size() > 1;
        case TM_CLOSE_LEFT:   return index > 0;
        case TM_CLOSE_RIGHT:  return index + 1 < (int)app.tabs.size();
        case TM_COPY_PATH:
        case TM_REVEAL:       return !app.tabs[index].path.empty();
    }
    return false;
}

int tabIndexById(const App& app, int id) {
    for (size_t i = 0; i < app.tabs.size(); i++) {
        if (app.tabs[i].id == id) return (int)i;
    }
    return -1;
}

void tabBulkCloseBegin(App& app, HWND hwnd, int mode, int keepIndex) {
    if (keepIndex < 0 || keepIndex >= (int)app.tabs.size()) return;
    app.tabBulkCloseMode = mode;
    app.tabBulkKeepId = app.tabs[keepIndex].id;
    tabBulkCloseStep(app, hwnd);
}

}  // namespace

void tabBulkCloseStep(App& app, HWND hwnd) {
    // Sweeps clean victims immediately; a dirty one opens the #106 dialog
    // and the sweep resumes from confirmExitAction (Keep editing aborts)
    while (app.tabBulkCloseMode) {
        int keep = tabIndexById(app, app.tabBulkKeepId);
        if (keep < 0) {
            app.tabBulkCloseMode = 0;
            return;
        }
        int victim = -1;
        if (app.tabBulkCloseMode != 3 && keep > 0) {
            victim = 0;  // everything left of the kept tab, one by one
        } else if (app.tabBulkCloseMode != 2 &&
                   keep + 1 < (int)app.tabs.size()) {
            victim = keep + 1;  // everything to its right
        }
        if (victim < 0) {
            app.tabBulkCloseMode = 0;
            if (keep != app.activeTab) tabActivate(app, hwnd, keep);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        tabCloseIndex(app, hwnd, victim);
        if (app.confirmExitPending) return;  // waiting on the dialog
    }
}

void openTabMenu(App& app, int tabIndex, float x, float y) {
    float w = tabMenuWidth(app);
    float h = tabMenuTotalHeight(app);
    app.tabMenuIndex = tabIndex;
    app.tabMenuX = std::max(0.0f, std::min(x, (float)app.width - w));
    app.tabMenuY = std::max(0.0f, std::min(y, (float)app.height - h));
    app.showTabMenu = true;
    app.tabMenuHover = -1;
    app.tabMenuAnimation = 0.0f;
    // The strip's own hover state stays quiet underneath
    app.hoveredTab = -1;
}

void closeTabMenu(App& app) {
    app.showTabMenu = false;
    app.tabMenuHover = -1;
    app.tabMenuAnimation = 0.0f;
}

int tabMenuItemAt(const App& app, float x, float y) {
    if (x < app.tabMenuX || x > app.tabMenuX + tabMenuWidth(app)) return -1;
    float rel = y - app.tabMenuY;
    for (int i = 0; i < TM_ITEM_COUNT; i++) {
        float top = tabMenuItemTop(app, i);
        if (rel >= top && rel < top + tabMenuItemHeight(app)) return i;
    }
    return -1;
}

void renderTabMenu(App& app) {
    if (!app.showTabMenu) return;
    if (app.tabMenuIndex < 0 || app.tabMenuIndex >= (int)app.tabs.size()) {
        closeTabMenu(app);
        return;
    }
    if (app.tabMenuAnimation < 1.0f) {
        float prev = app.tabMenuAnimation;
        app.tabMenuAnimation = std::min(1.0f, app.tabMenuAnimation + 0.25f);
        if (app.tabMenuAnimation != prev)
            InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.tabMenuAnimation;

    IDWriteTextFormat* format = app.folderBrowserFormat;
    if (!format) return;

    float x = app.tabMenuX;
    float y = app.tabMenuY;
    float w = tabMenuWidth(app);
    float h = tabMenuTotalHeight(app);

    D2D1_COLOR_F panelBg = app.theme.isDark ? hexColor(0x1E1E1E, 0.97f)
                                            : hexColor(0xF8F8F8, 0.97f);
    D2D1_COLOR_F borderColor = app.theme.isDark ? hexColor(0x3A3A40, 0.9f)
                                                : hexColor(0xC8C8C8, 0.9f);
    app.brush->SetColor(panelBg);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 6, 6), app.brush);
    app.brush->SetColor(borderColor);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 6, 6), app.brush,
        1.0f);

    app.tabMenuHover = tabMenuItemAt(app, (float)app.mouseX, (float)app.mouseY);

    float pad = tabMenuPadding(app);
    float itemH = tabMenuItemHeight(app);
    float inset = dpi(app, 12.0f);
    for (int i = 0; i < TM_ITEM_COUNT; i++) {
        float top = y + tabMenuItemTop(app, i);
        bool enabled = tabMenuItemEnabled(app, i);

        if (i == app.tabMenuHover && enabled) {
            D2D1_COLOR_F hoverColor = app.theme.accent;
            hoverColor.a = 0.18f * anim;
            app.brush->SetColor(hoverColor);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(x + pad, top, x + w - pad, top + itemH), 4, 4),
                app.brush);
        }

        D2D1_COLOR_F textColor = app.theme.text;
        textColor.a = (enabled ? 1.0f : 0.35f) * anim;
        app.brush->SetColor(textColor);
        float textY = top + (itemH - dpi(app, 18.0f)) / 2;
        const wchar_t* label = tr(app, TAB_MENU[i].key);
        app.renderTarget->DrawText(
            label, (UINT32)wcslen(label), format,
            D2D1::RectF(x + inset, textY, x + w - inset, top + itemH),
            app.brush);

        if (TAB_MENU[i].shortcut[0]) {
            IDWriteTextLayout* hint = nullptr;
            app.dwriteFactory->CreateTextLayout(
                TAB_MENU[i].shortcut, (UINT32)wcslen(TAB_MENU[i].shortcut),
                format, 1000.0f, itemH, &hint);
            if (hint) {
                DWRITE_TEXT_METRICS metrics{};
                hint->GetMetrics(&metrics);
                D2D1_COLOR_F hintColor = app.theme.text;
                hintColor.a = 0.4f * anim;
                app.brush->SetColor(hintColor);
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(x + w - inset - metrics.width, textY), hint,
                    app.brush);
                hint->Release();
            }
        }

        if (TAB_MENU[i].separatorAfter) {
            float sepY = top + itemH + tabMenuSeparator(app) * 0.5f;
            app.brush->SetColor(borderColor);
            app.renderTarget->DrawLine(D2D1::Point2F(x + pad, sepY),
                                       D2D1::Point2F(x + w - pad, sepY),
                                       app.brush, 1.0f);
        }
    }
}

bool tabMenuMouseDown(App& app, HWND hwnd, int x, int y) {
    if (!app.showTabMenu) return false;
    int item = tabMenuItemAt(app, (float)x, (float)y);
    int index = app.tabMenuIndex;
    closeTabMenu(app);
    app.swallowNextMouseUp = true;
    InvalidateRect(hwnd, nullptr, FALSE);
    if (item < 0 || index < 0 || index >= (int)app.tabs.size()) return true;
    // Re-validate against the surviving tab row (indices can shift while
    // the menu is up only via external means; the guard is cheap)
    App& a = app;
    switch (item) {
        case TM_CLOSE:
            if (tabMenuItemEnabled(a, TM_CLOSE)) tabCloseIndex(a, hwnd, index);
            break;
        case TM_CLOSE_OTHERS:
            if (a.tabs.size() > 1) tabBulkCloseBegin(a, hwnd, 1, index);
            break;
        case TM_CLOSE_LEFT:
            if (index > 0) tabBulkCloseBegin(a, hwnd, 2, index);
            break;
        case TM_CLOSE_RIGHT:
            if (index + 1 < (int)a.tabs.size())
                tabBulkCloseBegin(a, hwnd, 3, index);
            break;
        case TM_COPY_PATH:
            if (!a.tabs[index].path.empty()) {
                std::wstring wide = toWide(a.tabs[index].path);
                wchar_t fullPath[MAX_PATH];
                if (GetFullPathNameW(wide.c_str(), MAX_PATH, fullPath,
                                     nullptr)) {
                    wide = fullPath;
                }
                copyToClipboard(hwnd, wide);
                signalPushKey(a, SIG_INFO, SIGI_COPY, "toast.copied");
            }
            break;
        case TM_REVEAL:
            if (!a.tabs[index].path.empty()) {
                std::wstring wide = toWide(a.tabs[index].path);
                wchar_t fullPath[MAX_PATH];
                if (GetFullPathNameW(wide.c_str(), MAX_PATH, fullPath,
                                     nullptr)) {
                    std::wstring params =
                        L"/select,\"" + std::wstring(fullPath) + L"\"";
                    ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                  params.c_str(), nullptr, SW_SHOWNORMAL);
                }
            }
            break;
    }
    return true;
}

// --- drag ghost: a layered popup card that follows the cursor while a
// tab is pulled clear of the strip, so the detach reads as a motion
// instead of a jump ---

namespace {

const wchar_t* kGhostClass = L"TintaTabGhost";

COLORREF toColorref(const D2D1_COLOR_F& c) {
    return RGB((BYTE)(c.r * 255.0f + 0.5f), (BYTE)(c.g * 255.0f + 0.5f),
               (BYTE)(c.b * 255.0f + 0.5f));
}

void ghostDestroy(App& app) {
    if (app.tabGhostWnd) {
        DestroyWindow(app.tabGhostWnd);
        app.tabGhostWnd = nullptr;
    }
}

// Paints the tab card into a premultiplied ARGB surface and shows it via
// UpdateLayeredWindow (GDI text writes alpha 0, so alpha is rebuilt from
// rounded-rect membership afterwards)
void ghostCreate(App& app, const std::wstring& title, int screenX,
                 int screenY) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kGhostClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    int width = (int)dpi(app, 190.0f);
    int height = (int)dpi(app, 34.0f);
    float radius = dpi(app, 8.0f);

    app.tabGhostWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kGhostClass, L"", WS_POPUP, screenX, screenY, width, height, nullptr,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!app.tabGhostWnd) return;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits,
                                   nullptr, 0);
    if (dib && bits) {
        HGDIOBJ oldBitmap = SelectObject(memory, dib);
        memset(bits, 0, (size_t)width * height * 4);

        // Card: strip-colored fill, subtle border
        HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                         (int)(radius * 2), (int)(radius * 2));
        HBRUSH fill = CreateSolidBrush(toColorref(tabStripBackground(app)));
        FillRgn(memory, region, fill);
        D2D1_COLOR_F borderColor = app.theme.text;
        HBRUSH border = CreateSolidBrush(toColorref(borderColor));
        FrameRgn(memory, region, border, 1, 1);
        DeleteObject(fill);
        DeleteObject(border);
        DeleteObject(region);

        // Title
        HFONT font = CreateFontW(-(int)dpi(app, 13.0f), 0, 0, 0, FW_NORMAL,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                 L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(memory, font);
        SetBkMode(memory, TRANSPARENT);
        SetTextColor(memory, toColorref(app.theme.text));
        RECT textRect = {(int)dpi(app, 12.0f), 0,
                         width - (int)dpi(app, 10.0f), height};
        DrawTextW(memory, title.c_str(), (int)title.size(), &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
        SelectObject(memory, oldFont);
        DeleteObject(font);

        // Rebuild alpha: opaque inside the rounded card, clear outside
        uint32_t* pixels = static_cast<uint32_t*>(bits);
        float rr = radius;
        for (int py = 0; py < height; py++) {
            for (int px = 0; px < width; px++) {
                float dx = 0.0f, dy = 0.0f;
                if (px < rr && py < rr) { dx = rr - px; dy = rr - py; }
                else if (px >= width - rr && py < rr) { dx = px - (width - 1 - rr); dy = rr - py; }
                else if (px < rr && py >= height - rr) { dx = rr - px; dy = py - (height - 1 - rr); }
                else if (px >= width - rr && py >= height - rr) { dx = px - (width - 1 - rr); dy = py - (height - 1 - rr); }
                bool inside = dx * dx + dy * dy <= rr * rr;
                uint32_t& p = pixels[(size_t)py * width + px];
                p = inside ? (p | 0xFF000000u) : 0u;
            }
        }

        POINT source = {0, 0};
        POINT position = {screenX, screenY};
        SIZE size = {width, height};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 235, AC_SRC_ALPHA};
        UpdateLayeredWindow(app.tabGhostWnd, screen, &position, &size,
                            memory, &source, 0, &blend, ULW_ALPHA);
        SelectObject(memory, oldBitmap);
        DeleteObject(dib);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    ShowWindow(app.tabGhostWnd, SW_SHOWNOACTIVATE);
}

void ghostMoveTo(App& app, int screenX, int screenY) {
    if (!app.tabGhostWnd) return;
    SetWindowPos(app.tabGhostWnd, HWND_TOPMOST, screenX, screenY, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

// Close the dragged tab locally after it moved elsewhere
void dragCloseLocal(App& app, HWND hwnd, int index) {
    if (index < 0 || index >= (int)app.tabs.size()) return;
    if ((int)app.tabs.size() <= 1) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return;
    }
    if (index == app.activeTab) {
        int next = index + 1 < (int)app.tabs.size() ? index + 1 : index - 1;
        tabActivate(app, hwnd, next);
    }
    app.tabs.erase(app.tabs.begin() + index);
    if (app.activeTab > index) app.activeTab--;
    app.hoveredTab = -1;
}

}  // namespace

void tabDragMove(App& app, HWND hwnd, int x, int y) {
    if (app.tabDragIndex < 0) return;
    if (app.tabDragIndex >= (int)app.tabs.size()) {
        tabDragEnd(app, hwnd, x, y);
        return;
    }

    if (!app.tabDragging) {
        if (std::abs(x - app.tabDragStartX) < (int)dpi(app, 6.0f) &&
            std::abs(y - app.tabDragStartY) < (int)dpi(app, 6.0f)) {
            return;
        }
        app.tabDragging = true;
    }

    int index = app.tabDragIndex;
    bool active = index == app.activeTab;
    bool dirty = active ? (app.editMode && app.editorDirty)
                        : (app.tabs[index].editMode &&
                           app.tabs[index].editorDirty);
    const std::string& path = active ? app.currentFile : app.tabs[index].path;
    bool detachable = !dirty && !path.empty();

    // Clear of the strip: the tab floats as a ghost card until release
    float stripH = chromeTopHeight(app);
    bool outside = (float)y > stripH + dpi(app, 48.0f) ||
                   (float)y < -dpi(app, 48.0f);
    if (outside && detachable) {
        POINT screen = {x, y};
        ClientToScreen(hwnd, &screen);
        // Keep the cursor on the card even when the tab was wider than it
        int grab = std::min((int)app.tabDragOffsetX,
                            (int)dpi(app, 166.0f));
        int ghostX = screen.x - std::max(grab, (int)dpi(app, 12.0f));
        int ghostY = screen.y - (int)dpi(app, 17.0f);
        if (!app.tabDragDetached) {
            app.tabDragDetached = true;
            ghostCreate(app, app.tabs[index].title, ghostX, ghostY);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            ghostMoveTo(app, ghostX, ghostY);
        }
        return;
    }
    if (app.tabDragDetached) {
        // Back over the strip: the tab snaps home and reordering resumes
        app.tabDragDetached = false;
        ghostDestroy(app);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    // Reorder: the dragged tab claims the slot under the pointer
    StripMetrics m = stripMetrics(app);
    float gap = dpi(app, 2.0f);
    int slot = (int)(((float)x - m.tabsLeft) / (m.tabWidth + gap));
    slot = std::max(0, std::min((int)app.tabs.size() - 1, slot));
    if (slot != app.tabDragIndex) {
        App::DocTab moved = std::move(app.tabs[app.tabDragIndex]);
        app.tabs.erase(app.tabs.begin() + app.tabDragIndex);
        app.tabs.insert(app.tabs.begin() + slot, std::move(moved));
        // The dragged tab is the active one (the press activated it)
        app.activeTab = slot;
        app.tabDragIndex = slot;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void tabDragEnd(App& app, HWND hwnd, int x, int y) {
    if (app.tabDragIndex < 0) return;
    int index = app.tabDragIndex;
    bool detached = app.tabDragDetached;
    app.tabDragIndex = -1;
    app.tabDragging = false;
    app.tabDragDetached = false;
    ghostDestroy(app);
    if (GetCapture() == hwnd) ReleaseCapture();

    if (detached && index < (int)app.tabs.size()) {
        bool active = index == app.activeTab;
        const std::string& path =
            active ? app.currentFile : app.tabs[index].path;
        POINT screen = {x, y};
        ClientToScreen(hwnd, &screen);

        // Dropped onto another Tinta window: the tab moves there
        HWND under = WindowFromPoint(screen);
        HWND root = under ? GetAncestor(under, GA_ROOT) : nullptr;
        wchar_t className[16] = {};
        if (root && root != hwnd &&
            GetClassNameW(root, className, _countof(className)) &&
            wcscmp(className, L"Tinta") == 0) {
            COPYDATASTRUCT data;
            data.dwData = 1;
            data.cbData = (DWORD)path.size() + 1;
            data.lpData = (void*)path.c_str();
            SendMessageW(root, WM_COPYDATA, 0, (LPARAM)&data);
            SetForegroundWindow(root);
            dragCloseLocal(app, hwnd, index);
        } else if (root == hwnd) {
            // Dropped back onto this window's content: snap home
        } else if (app.tabs.size() > 1) {
            // Free drop: a new window appears where the ghost was
            wchar_t exePath[MAX_PATH];
            if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
                wchar_t args[MAX_PATH * 2];
                swprintf_s(args, _countof(args),
                           L"--cascade --tabbed --pos %d %d \"%hs\"",
                           screen.x - (int)dpi(app, 120.0f),
                           screen.y - (int)dpi(app, 20.0f), path.c_str());
                ShellExecuteW(nullptr, L"open", exePath, args, nullptr,
                              SW_SHOWNORMAL);
            }
            dragCloseLocal(app, hwnd, index);
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void tabWindowDropMerge(App& app, HWND hwnd) {
    // A tabless window dropped so the cursor lands on another Tinta
    // window's tab strip donates its document there (Notepad-style);
    // multi-tab windows hand tabs over through per-tab drags instead
    if (app.tabs.size() != 1) return;
    if (app.editMode && app.editorDirty) return;
    const std::string& path = app.currentFile;
    if (path.empty()) return;

    POINT cursor;
    if (!GetCursorPos(&cursor)) return;
    HWND target = nullptr;
    // Top-level Tinta windows in z-order; the first one under the cursor
    // (other than us) is the drop candidate
    for (HWND w = FindWindowExW(nullptr, nullptr, L"Tinta", nullptr); w;
         w = FindWindowExW(nullptr, w, L"Tinta", nullptr)) {
        if (w == hwnd || !IsWindowVisible(w) || IsIconic(w)) continue;
        RECT r;
        if (!GetWindowRect(w, &r) || !PtInRect(&r, cursor)) continue;
        // Only the strip band counts as a drop zone; anywhere lower is
        // an ordinary overlapping window placement
        UINT targetDpi = GetDpiForWindow(w);
        if (cursor.y > r.top + MulDiv(46, (int)targetDpi, 96)) break;
        target = w;
        break;
    }
    if (!target) return;

    COPYDATASTRUCT data;
    data.dwData = 1;
    data.cbData = (DWORD)path.size() + 1;
    data.lpData = (void*)path.c_str();
    SendMessageW(target, WM_COPYDATA, 0, (LPARAM)&data);
    SetForegroundWindow(target);
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

void tabDragCancel(App& app, HWND hwnd) {
    if (app.tabDragIndex < 0) return;
    app.tabDragIndex = -1;
    app.tabDragging = false;
    app.tabDragDetached = false;
    ghostDestroy(app);
    if (GetCapture() == hwnd) ReleaseCapture();
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool tabSwitcherMouseDown(App& app, HWND hwnd, int x, int y) {
    if (!app.showTabSwitcher) return false;
    for (const auto& [rect, index] : app.tabSwitcherHits) {
        if (x >= rect.left && x <= rect.right && y >= rect.top &&
            y <= rect.bottom) {
            app.showTabSwitcher = false;
            tabActivate(app, hwnd, index);
            return true;
        }
    }
    app.showTabSwitcher = false;
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}
