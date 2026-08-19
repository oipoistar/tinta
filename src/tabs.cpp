// Tabbed interface (design doc t3): Win11 Notepad-style tabs in a custom
// title bar. The strip is the caption -- the active tab lifts to the
// document surface with an accent underline, unsaved buffers show a dot,
// a chevron opens the open-files switcher when the strip is crowded.

#include "tabs.h"

#include "document.h"
#include "editor.h"
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
    tab.title = titleForPath(app, app.currentFile);
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
    tab.path = app.currentFile;
    tab.title = titleForPath(app, app.currentFile);
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

    if ((int)app.tabs.size() <= 1) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
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

void tabOpenQuickNote(App& app, HWND hwnd) {
    // The + button behaves like Ctrl+N, in-window: a fresh untitled
    // quick-note tab straight into the editor; the first Ctrl+S runs the
    // classic Save As flow and the tab picks up the chosen name
    tabsInit(app);
    syncActiveTab(app);
    parkActiveEditBuffer(app);
    App::DocTab tab;
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
    m.tabsLeft = dpi(app, 40.0f);
    float buttons = captionButtonWidth(app) * 3.0f;
    float plusW = dpi(app, 30.0f);
    float chevronW = dpi(app, 32.0f);
    float gap = dpi(app, 2.0f);
    size_t count = std::max<size_t>(app.tabs.size(), 1);

    float available = (float)app.width - m.tabsLeft - buttons - plusW -
                      dpi(app, 16.0f);
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

    // Strip background
    app.brush->SetColor(stripBackground(app));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, stripH), app.brush);

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

    bool tabless = app.tabs.size() < 2;
    if (tabless) {
        // Single document: the caption shows the plain window title
        std::wstring title = L"Tinta";
        if (!app.tabs.empty() && !app.tabs[0].title.empty()) {
            title = app.tabs[0].title;
            if (app.editMode && app.editorDirty) title = L"* " + title;
        }
        if (app.folderBrowserFormat) {
            app.brush->SetColor(muted);
            app.renderTarget->DrawText(
                title.c_str(), (UINT32)title.size(), app.folderBrowserFormat,
                D2D1::RectF(iconCell + dpi(app, 4.0f),
                            (stripH - dpi(app, 17.0f)) * 0.5f,
                            (float)app.width - captionButtonWidth(app) * 3,
                            stripH),
                app.brush);
        }
    } else {
        StripMetrics m = stripMetrics(app);
        float radius = dpi(app, 8.0f);
        float gap = dpi(app, 2.0f);

        app.renderTarget->PushAxisAlignedClip(
            D2D1::RectF(0, 0, (float)app.width, stripH),
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
        float plusSize = dpi(app, 30.0f);
        float plusY = (stripH - plusSize) * 0.5f;
        D2D1_RECT_F plusRect = D2D1::RectF(m.plusX, plusY, m.plusX + plusSize,
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

    // Caption buttons: minimize, maximize/restore, close
    for (int b = 0; b < 3; b++) {
        D2D1_RECT_F r = captionButtonRect(app, b);
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
            app.renderTarget->FillRectangle(r, app.brush);
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
                           L"--cascade --pos %d %d \"%hs\"",
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
