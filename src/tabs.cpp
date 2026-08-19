// Tabbed interface (design doc t3): Win11 Notepad-style tabs in a custom
// title bar. The strip is the caption -- the active tab lifts to the
// document surface with an accent underline, unsaved buffers show a dot,
// a chevron opens the open-files switcher when the strip is crowded.

#include "tabs.h"

#include "editor.h"
#include "file_utils.h"
#include "i18n.h"
#include "input.h"
#include "utils.h"

#include <algorithm>

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
        app.currentFile.clear();
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

namespace {

D2D1_COLOR_F stripBackground(const App& app) {
    D2D1_COLOR_F c = app.theme.background;
    float f = app.theme.isDark ? 0.78f : 0.955f;
    c.r *= f;
    c.g *= f;
    c.b *= f;
    return c;
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
            bool active = (int)i == app.activeTab;
            bool hovered = (int)i == app.hoveredTab;
            float x = m.tabsLeft + (m.tabWidth + gap) * i;
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
            // + opens the folder browser; the next pick lands in a new tab
            if (!middle) {
                app.tabNewTabIntent = true;
                if (!app.showFolderBrowser) {
                    app.showFolderBrowser = true;
                    app.folderBrowserAnimation = 0.0f;
                    if (!app.currentFile.empty()) {
                        app.folderBrowserPath =
                            getDirectoryFromFile(app.currentFile);
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
        return true;
    }
    return true;  // strip clicks never fall through to the document
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
