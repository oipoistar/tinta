#include "overlays.h"
#include "utils.h"
#include "d2d_init.h"
#include "editor.h"
#include "markdown.h"
#include "pandoc.h"
#include "print.h"
#include "settings.h"
#include "signals.h"
#include "i18n.h"

// Prompt-chip helpers defined with the dialogs below; the floating panels
// share their surface, shadow, and keycap language (t13)
static void promptChipShadow(App& app, const D2D1_RECT_F& r, float radius);
static D2D1_COLOR_F promptChipSurface(const App& app);
static float promptKeycap(App& app, const wchar_t* label, float rightX,
                          float cy);

#include <chrono>
#include <algorithm>
#include <cmath>
#include <utility>

void renderSearchOverlay(App& app) {
    // Animate in (only invalidate if animation is still progressing)
    if (app.searchAnimation < 1.0f) {
        float prev = app.searchAnimation;
        app.searchAnimation = std::min(1.0f, app.searchAnimation + 0.2f);
        if (app.searchAnimation != prev)
            InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.searchAnimation;

    // Search bar dimensions
    float barWidth = std::min(dpi(app, 500.0f), app.width - dpi(app, 40.0f));
    float barHeight = dpi(app, 44.0f);
    float barCenterWidth = (float)app.width;
    if (app.editMode) {
        // Center over editor pane (left side)
        float paneWidth = app.width * app.editorSplitRatio - 3;
        barWidth = std::min(barWidth, paneWidth - dpi(app, 40.0f));
        barCenterWidth = paneWidth;
    }
    float barX = (barCenterWidth - barWidth) / 2;
    float barY = chromeTopHeight(app) + dpi(app, 20.0f) * anim -
                 barHeight * (1.0f - anim);  // Slide down from under the strip

    // Background with rounded corners
    D2D1_ROUNDED_RECT barRect = D2D1::RoundedRect(
        D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight),
        dpi(app, 8.0f), dpi(app, 8.0f));

    // Semi-transparent background based on theme
    if (app.theme.isDark) {
        app.brush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.95f * anim));
    } else {
        app.brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f * anim));
    }
    app.renderTarget->FillRoundedRectangle(barRect, app.brush);

    // Border
    if (app.theme.isDark) {
        app.brush->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.35f, 0.8f * anim));
    } else {
        app.brush->SetColor(D2D1::ColorF(0.7f, 0.7f, 0.75f, 0.8f * anim));
    }
    app.renderTarget->DrawRoundedRectangle(barRect, app.brush, 1.0f);

    // Search icon (simple circle for magnifying glass look)
    {
        D2D1_COLOR_F iconColor = app.theme.text;
        iconColor.a = 0.5f * anim;
        app.brush->SetColor(iconColor);
        // Draw a simple magnifying glass shape
        float iconX = barX + dpi(app, 22.0f);
        float iconY = barY + dpi(app, 22.0f);
        float iconR = dpi(app, 7.0f);
        app.renderTarget->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(iconX, iconY - dpi(app, 2.0f)), iconR, iconR),
            app.brush, dpi(app, 2.0f));
        app.renderTarget->DrawLine(
            D2D1::Point2F(iconX + dpi(app, 5.0f), iconY + dpi(app, 3.0f)),
            D2D1::Point2F(iconX + dpi(app, 9.0f), iconY + dpi(app, 7.0f)),
            app.brush, dpi(app, 2.0f));
    }

    // Search text
    IDWriteTextFormat* searchTextFormat = app.searchTextFormat;
    if (searchTextFormat) {
        float textX = barX + dpi(app, 42.0f);
        float textWidth = barWidth - dpi(app, 120.0f);  // Leave room for count

        if (app.searchQuery.empty()) {
            // Placeholder text
            D2D1_COLOR_F placeholderColor = app.theme.text;
            placeholderColor.a = 0.4f * anim;
            app.brush->SetColor(placeholderColor);
            const wchar_t* ph = tr(app, "search.placeholder");
            app.renderTarget->DrawText(ph, (UINT32)wcslen(ph), searchTextFormat,
                D2D1::RectF(textX, barY + dpi(app, 12.0f), textX + textWidth, barY + barHeight), app.brush);
        } else {
            // Actual search query
            D2D1_COLOR_F textColor = app.theme.text;
            textColor.a = anim;
            app.brush->SetColor(textColor);
            app.renderTarget->DrawText(app.searchQuery.c_str(), (UINT32)app.searchQuery.length(),
                searchTextFormat,
                D2D1::RectF(textX, barY + dpi(app, 12.0f), textX + textWidth, barY + barHeight), app.brush);

            // Blinking cursor (blink state driven by TIMER_CURSOR_BLINK).
            // Query width is cached - it only changes when the query or the
            // text format changes, not per frame.
            if (app.searchActive && app.cursorBlinkOn &&
                !(app.searchReplaceMode && app.replaceFieldActive)) {
                static std::wstring cachedQuery;
                static IDWriteTextFormat* cachedFormat = nullptr;
                static float cachedQueryWidth = 0.0f;
                if (cachedQuery != app.searchQuery || cachedFormat != searchTextFormat) {
                    cachedQueryWidth = measureText(app, app.searchQuery, searchTextFormat);
                    cachedQuery = app.searchQuery;
                    cachedFormat = searchTextFormat;
                }
                float cursorX = textX + cachedQueryWidth + 2;
                app.brush->SetColor(textColor);
                app.renderTarget->DrawLine(
                    D2D1::Point2F(cursorX, barY + dpi(app, 12.0f)),
                    D2D1::Point2F(cursorX, barY + dpi(app, 32.0f)),
                    app.brush, dpi(app, 1.5f));
            }
        }

        // Match count
        if (!app.searchQuery.empty()) {
            wchar_t countText[32];
            size_t matchCount = app.editMode ? app.editorSearchMatches.size() : app.searchMatches.size();
            int currentIdx = app.editMode ? app.editorSearchCurrentIndex : app.searchCurrentIndex;
            if (matchCount == 0) {
                wcscpy_s(countText, tr(app, "search.no_matches"));
                // Red color for no matches
                app.brush->SetColor(D2D1::ColorF(0.9f, 0.3f, 0.3f, anim));
            } else {
                swprintf_s(countText, tr(app, "search.match_count"), currentIdx + 1, matchCount);
                D2D1_COLOR_F countColor = app.theme.text;
                countColor.a = 0.7f * anim;
                app.brush->SetColor(countColor);
            }
            float countTextWidth = measureText(app, countText, searchTextFormat);
            float countX = barX + barWidth - countTextWidth - dpi(app, 14.0f);
            app.renderTarget->DrawText(countText, (UINT32)wcslen(countText), searchTextFormat,
                D2D1::RectF(countX, barY + dpi(app, 12.0f), barX + barWidth - dpi(app, 10.0f), barY + barHeight), app.brush);
        }

    }

    // Replace row (#121): a second input under the bar plus Replace / All
    app.searchReplaceHits.clear();
    if (app.editMode && app.searchReplaceMode && searchTextFormat) {
        float rowY = barY + barHeight + dpi(app, 6.0f);
        float rowH = dpi(app, 44.0f);
        D2D1_ROUNDED_RECT rowRect = D2D1::RoundedRect(
            D2D1::RectF(barX, rowY, barX + barWidth, rowY + rowH),
            dpi(app, 8.0f), dpi(app, 8.0f));
        if (app.theme.isDark) {
            app.brush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.95f * anim));
        } else {
            app.brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f * anim));
        }
        app.renderTarget->FillRoundedRectangle(rowRect, app.brush);
        if (app.theme.isDark) {
            app.brush->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.35f, 0.8f * anim));
        } else {
            app.brush->SetColor(D2D1::ColorF(0.7f, 0.7f, 0.75f, 0.8f * anim));
        }
        app.renderTarget->DrawRoundedRectangle(rowRect, app.brush, 1.0f);

        // Buttons on the right; Replace is the accent-filled primary
        const wchar_t* repLabel = tr(app, "search.replace");
        const wchar_t* allLabel = tr(app, "search.replace_all");
        float repW = measureText(app, repLabel, searchTextFormat) + dpi(app, 24.0f);
        float allW = measureText(app, allLabel, searchTextFormat) + dpi(app, 24.0f);
        float chipH = dpi(app, 28.0f);
        float chipY = rowY + (rowH - chipH) * 0.5f;
        float allX = barX + barWidth - dpi(app, 10.0f) - allW;
        float repX = allX - dpi(app, 8.0f) - repW;
        auto chip = [&](float cx, float cw, const wchar_t* label, int id,
                        bool primary) {
            D2D1_ROUNDED_RECT cr = D2D1::RoundedRect(
                D2D1::RectF(cx, chipY, cx + cw, chipY + chipH),
                dpi(app, 5.0f), dpi(app, 5.0f));
            D2D1_COLOR_F c;
            if (primary) {
                c = app.theme.accent; c.a = anim;
                app.brush->SetColor(c);
                app.renderTarget->FillRoundedRectangle(cr, app.brush);
                app.brush->SetColor(D2D1::ColorF(1, 1, 1, anim));
            } else {
                c = app.theme.text; c.a = 0.3f * anim;
                app.brush->SetColor(c);
                app.renderTarget->DrawRoundedRectangle(cr, app.brush, 1.0f);
                c.a = 0.85f * anim;
                app.brush->SetColor(c);
            }
            app.renderTarget->DrawText(label, (UINT32)wcslen(label),
                searchTextFormat,
                D2D1::RectF(cx + dpi(app, 12.0f), chipY + dpi(app, 5.0f),
                            cx + cw, chipY + chipH), app.brush);
            app.searchReplaceHits.push_back(
                {D2D1::RectF(cx, chipY, cx + cw, chipY + chipH), id});
        };
        chip(repX, repW, repLabel, 3, true);
        chip(allX, allW, allLabel, 4, false);

        // Arrow icon marking the replace row
        D2D1_COLOR_F iconColor = app.theme.text;
        iconColor.a = 0.5f * anim;
        app.brush->SetColor(iconColor);
        float ix = barX + dpi(app, 20.0f);
        float iy = rowY + rowH * 0.5f;
        app.renderTarget->DrawLine(
            D2D1::Point2F(ix, iy - dpi(app, 7.0f)),
            D2D1::Point2F(ix, iy + dpi(app, 3.0f)), app.brush, dpi(app, 2.0f));
        app.renderTarget->DrawLine(
            D2D1::Point2F(ix, iy + dpi(app, 3.0f)),
            D2D1::Point2F(ix + dpi(app, 8.0f), iy + dpi(app, 3.0f)),
            app.brush, dpi(app, 2.0f));

        // Replace text (or placeholder), left of the buttons
        float textX = barX + dpi(app, 42.0f);
        float textRight = repX - dpi(app, 12.0f);
        if (app.replaceText.empty()) {
            D2D1_COLOR_F ph = app.theme.text;
            ph.a = 0.4f * anim;
            app.brush->SetColor(ph);
            const wchar_t* p = tr(app, "search.replace_placeholder");
            app.renderTarget->DrawText(p, (UINT32)wcslen(p), searchTextFormat,
                D2D1::RectF(textX, rowY + dpi(app, 12.0f), textRight,
                            rowY + rowH), app.brush);
        } else {
            D2D1_COLOR_F tc = app.theme.text;
            tc.a = anim;
            app.brush->SetColor(tc);
            app.renderTarget->DrawText(app.replaceText.c_str(),
                (UINT32)app.replaceText.size(), searchTextFormat,
                D2D1::RectF(textX, rowY + dpi(app, 12.0f), textRight,
                            rowY + rowH), app.brush);
        }

        // The active field carries the accent focus ring and the caret
        D2D1_COLOR_F focus = app.theme.accent;
        focus.a = 0.9f * anim;
        app.brush->SetColor(focus);
        if (app.replaceFieldActive) {
            app.renderTarget->DrawRoundedRectangle(rowRect, app.brush, 1.5f);
            if (app.searchActive && app.cursorBlinkOn) {
                float cw2 = measureText(app, app.replaceText, searchTextFormat);
                float cx2 = textX + cw2 + 2;
                D2D1_COLOR_F tc = app.theme.text;
                tc.a = anim;
                app.brush->SetColor(tc);
                app.renderTarget->DrawLine(
                    D2D1::Point2F(cx2, rowY + dpi(app, 12.0f)),
                    D2D1::Point2F(cx2, rowY + dpi(app, 32.0f)), app.brush,
                    dpi(app, 1.5f));
            }
        } else {
            app.renderTarget->DrawRoundedRectangle(barRect, app.brush, 1.5f);
        }

        // Field rects come after the buttons so button clicks win
        app.searchReplaceHits.push_back(
            {D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight), 1});
        app.searchReplaceHits.push_back(
            {D2D1::RectF(barX, rowY, barX + barWidth, rowY + rowH), 2});
    }
}

// One source of truth for the floating browser card's geometry (t13):
// render, cursor, and click hit-tests all read from here.
FolderBrowserMetrics folderBrowserMetrics(const App& app) {
    FolderBrowserMetrics g;
    g.panelWidth = folderBrowserPanelWidth(app);
    g.panelX = -g.panelWidth * (1.0f - app.folderBrowserAnimation);
    float m = dpi(app, 10.0f);
    g.cardLeft = g.panelX + m;
    g.cardRight = g.panelX + g.panelWidth - m;
    g.cardTop = chromeTopHeight(app) + dpi(app, 10.0f);
    g.cardBottom = (float)app.height - dpi(app, 12.0f);
    g.headerY = g.cardTop + dpi(app, 8.0f);
    g.headerH = dpi(app, 28.0f);
    g.btnSize = dpi(app, 22.0f);
    g.fileBtnX = g.cardRight - dpi(app, 10.0f) - g.btnSize;
    g.folderBtnX = g.fileBtnX - dpi(app, 4.0f) - g.btnSize;
    g.pinBtnX = g.folderBtnX - dpi(app, 4.0f) - g.btnSize;
    g.btnY = g.headerY + (g.headerH - g.btnSize) * 0.5f;
    float dividerY = g.headerY + g.headerH + dpi(app, 4.0f);
    g.listStartY = dividerY + dpi(app, 3.0f);
    g.listBottom = g.cardBottom - dpi(app, 26.0f);  // footer band
    g.itemHeight = dpi(app, 26.0f);
    g.labelH = dpi(app, 19.0f);
    g.namingOffset = app.folderBrowserNaming != 0 ? g.itemHeight : 0.0f;
    g.dirCount = 0;
    for (const auto& it : app.folderItems) {
        if (it.isDirectory) g.dirCount++;
    }
    g.hasDirs = g.dirCount > 0;
    g.hasFiles = g.dirCount < (int)app.folderItems.size();
    return g;
}

// Top edge of item i inside the scrolled content: the FOLDERS and FILES
// section labels sit inside the scroll like rows do
float folderItemContentY(const FolderBrowserMetrics& g, int index) {
    float y = 0.0f;
    if (g.hasDirs) y += g.labelH;
    if (index < g.dirCount) return y + index * g.itemHeight;
    y += g.dirCount * g.itemHeight;
    if (g.hasFiles) y += g.labelH;
    return y + (index - g.dirCount) * g.itemHeight;
}

// Item index at a client point, sharing renderFolderBrowser's geometry.
// Clicks must hit-test their own coordinates: the render-time hover index
// lags one paint behind the mouse, so a fast move-and-click would act on
// the previously highlighted row.
int folderItemIndexAt(const App& app, float x, float y) {
    FolderBrowserMetrics g = folderBrowserMetrics(app);
    float rowLeft = g.cardLeft + dpi(app, 6.0f);
    float rowRight = g.cardRight - dpi(app, 6.0f);
    if (x < rowLeft || x > rowRight) return -1;
    if (y < g.listStartY + g.namingOffset || y > g.listBottom) return -1;
    float contentY =
        y - (g.listStartY + g.namingOffset - app.folderBrowserScroll);
    for (int i = 0; i < (int)app.folderItems.size(); i++) {
        float top = folderItemContentY(g, i);
        if (contentY >= top && contentY < top + g.itemHeight) return i;
    }
    return -1;
}

// Map-pin for the panel pin toggles (#156): head circle plus needle
static void drawPinGlyph(App& app, float cx, float cy, D2D1_COLOR_F color) {
    app.brush->SetColor(color);
    app.renderTarget->DrawEllipse(
        D2D1::Ellipse(D2D1::Point2F(cx, cy - dpi(app, 2.0f)),
                      dpi(app, 3.0f), dpi(app, 3.0f)),
        app.brush, 1.3f);
    app.renderTarget->DrawLine(D2D1::Point2F(cx, cy + dpi(app, 1.0f)),
                               D2D1::Point2F(cx, cy + dpi(app, 6.0f)),
                               app.brush, 1.3f);
}

// Monoline glyphs shared by the browser card's rows and header buttons
static void drawFolderGlyph(App& app, float cx, float cy, float s,
                            D2D1_COLOR_F color, bool plus) {
    app.brush->SetColor(color);
    float w = 1.2f;
    float left = cx - s * 0.46f, right = cx + s * 0.46f;
    float top = cy - s * 0.3f, bot = cy + s * 0.34f;
    float tabW = s * 0.34f, tabH = s * 0.14f;
    app.renderTarget->DrawLine(D2D1::Point2F(left, bot), D2D1::Point2F(left, top), app.brush, w);
    app.renderTarget->DrawLine(D2D1::Point2F(left, top), D2D1::Point2F(left + tabW, top), app.brush, w);
    app.renderTarget->DrawLine(D2D1::Point2F(left + tabW, top),
                               D2D1::Point2F(left + tabW + tabH, top + tabH), app.brush, w);
    app.renderTarget->DrawLine(D2D1::Point2F(left + tabW + tabH, top + tabH),
                               D2D1::Point2F(right, top + tabH), app.brush, w);
    app.renderTarget->DrawLine(D2D1::Point2F(right, top + tabH), D2D1::Point2F(right, bot), app.brush, w);
    app.renderTarget->DrawLine(D2D1::Point2F(right, bot), D2D1::Point2F(left, bot), app.brush, w);
    if (plus) {
        app.renderTarget->DrawLine(D2D1::Point2F(cx, cy - s * 0.1f),
                                   D2D1::Point2F(cx, cy + s * 0.22f), app.brush, w);
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s * 0.16f, cy + s * 0.06f),
                                   D2D1::Point2F(cx + s * 0.16f, cy + s * 0.06f), app.brush, w);
    }
}

static void drawPageGlyph(App& app, float cx, float cy, float s,
                          D2D1_COLOR_F color, bool plus) {
    app.brush->SetColor(color);
    float w = 1.2f;
    float left = cx - s * 0.3f, right = cx + s * 0.3f;
    float top = cy - s * 0.44f, bot = cy + s * 0.44f;
    float fold = s * 0.22f;
    app.renderTarget->DrawLine(D2D1::Point2F(left, top), D2D1::Point2F(right - fold, top), app.brush, w);
    app.renderTarget->DrawLine(D2D1::Point2F(right - fold, top),
                               D2D1::Point2F(right, top + fold), app.brush, w);
    app.renderTarget->DrawLine(D2D1::Point2F(right, top + fold), D2D1::Point2F(right, bot), app.brush, w);
    app.renderTarget->DrawLine(D2D1::Point2F(right, bot), D2D1::Point2F(left, bot), app.brush, w);
    app.renderTarget->DrawLine(D2D1::Point2F(left, bot), D2D1::Point2F(left, top), app.brush, w);
    if (plus) {
        app.renderTarget->DrawLine(D2D1::Point2F(cx, cy - s * 0.12f),
                                   D2D1::Point2F(cx, cy + s * 0.2f), app.brush, w);
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s * 0.16f, cy + s * 0.04f),
                                   D2D1::Point2F(cx + s * 0.16f, cy + s * 0.04f), app.brush, w);
    }
}

void renderFolderBrowser(App& app) {
    // Animate in (slide from left) - only invalidate while progressing
    if (app.folderBrowserAnimation < 1.0f) {
        float prev = app.folderBrowserAnimation;
        app.folderBrowserAnimation = std::min(1.0f, app.folderBrowserAnimation + 0.15f);
        if (app.folderBrowserAnimation != prev)
            InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.folderBrowserAnimation;

    // Floating card inside the slide envelope (t13 design 13b)
    FolderBrowserMetrics g = folderBrowserMetrics(app);
    float panelWidth = g.panelWidth;
    float panelX = g.panelX;
    D2D1_RECT_F card = D2D1::RectF(g.cardLeft, g.cardTop, g.cardRight, g.cardBottom);
    float radius = dpi(app, 12.0f);
    promptChipShadow(app, card, radius);
    D2D1_COLOR_F panelBg = promptChipSurface(app);
    panelBg.a = 0.96f;
    app.brush->SetColor(panelBg);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(card, radius, radius), app.brush);
    D2D1_COLOR_F hi = D2D1::ColorF(1, 1, 1, app.theme.isDark ? 0.06f : 0.5f);
    app.brush->SetColor(hi);
    app.renderTarget->DrawLine(
        D2D1::Point2F(g.cardLeft + radius, g.cardTop + 1.0f),
        D2D1::Point2F(g.cardRight - radius, g.cardTop + 1.0f), app.brush, 1.0f);
    D2D1_COLOR_F borderColor = app.theme.text;
    borderColor.a = 0.13f;
    app.brush->SetColor(borderColor);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(card, radius, radius), app.brush, 1.0f);

    app.folderCrumbHits.clear();
    app.folderPinRect = D2D1_RECT_F{};
    IDWriteTextFormat* browserFormat = app.folderBrowserFormat;
    if (browserFormat) {
        float padding = dpi(app, 12.0f);
        float itemHeight = g.itemHeight;

        // Breadcrumb header - segments navigate, the pencil (or any other
        // spot on the row) opens the edit box (#52)
        float headerY = g.headerY;

        auto textWidth = [&](const std::wstring& s) {
            float w = 0.0f;
            IDWriteTextLayout* layout = nullptr;
            if (app.dwriteFactory && SUCCEEDED(app.dwriteFactory->CreateTextLayout(
                    s.c_str(), (UINT32)s.length(), browserFormat,
                    1000000.0f, g.headerH, &layout)) && layout) {
                DWRITE_TEXT_METRICS metrics;
                if (SUCCEEDED(layout->GetMetrics(&metrics))) {
                    w = metrics.widthIncludingTrailingWhitespace;
                }
                layout->Release();
            }
            return w;
        };

        D2D1_COLOR_F accentColor = app.theme.accent;
        accentColor.a = anim;
        D2D1_COLOR_F errorColor = hexColor(app.theme.isDark ? 0xF85149 : 0xCF222E, anim);
        float inputPad = dpi(app, 6.0f);
        float boxHeight = dpi(app, 26.0f);

        // Draws a single-line input box; the text is clipped from the left so
        // its tail (where typing happens) stays visible, with select-all
        // highlight and a caret at the end
        auto drawInputBox = [&](float boxX, float boxRight, float boxY) {
            D2D1_RECT_F box = D2D1::RectF(boxX, boxY, boxRight, boxY + boxHeight);
            D2D1_COLOR_F boxBg = app.theme.isDark ? hexColor(0x2A2A30, 0.9f * anim)
                                                  : hexColor(0xFFFFFF, 0.9f * anim);
            app.brush->SetColor(boxBg);
            app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, 4, 4), app.brush);
            app.brush->SetColor(app.folderBrowserInputError ? errorColor : accentColor);
            app.renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, 4, 4), app.brush, 1.0f);

            std::wstring display = app.folderBrowserInput;
            float maxTextWidth = (boxRight - boxX) - inputPad * 2 - dpi(app, 2.0f);
            while (!display.empty() && textWidth(display) > maxTextWidth) {
                display.erase(0, 1);
            }
            float shownWidth = textWidth(display);
            float textY = boxY + (boxHeight - dpi(app, 18.0f)) / 2;

            if (app.folderBrowserInputSelectAll && !display.empty()) {
                D2D1_COLOR_F selColor = app.theme.accent;
                selColor.a = 0.35f * anim;
                app.brush->SetColor(selColor);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(boxX + inputPad - 1, boxY + dpi(app, 3.0f),
                                boxX + inputPad + shownWidth + 1, boxY + boxHeight - dpi(app, 3.0f)),
                    app.brush);
            }

            D2D1_COLOR_F inputTextColor = app.theme.text;
            inputTextColor.a = anim;
            app.brush->SetColor(inputTextColor);
            app.renderTarget->DrawText(display.c_str(), (UINT32)display.length(), browserFormat,
                D2D1::RectF(boxX + inputPad, textY, boxRight - inputPad, boxY + boxHeight),
                app.brush);

            if (!app.folderBrowserInputSelectAll && app.cursorBlinkOn) {
                float caretX = boxX + inputPad + shownWidth + 1;
                app.brush->SetColor(inputTextColor);
                app.renderTarget->DrawLine(
                    D2D1::Point2F(caretX, boxY + dpi(app, 4.0f)),
                    D2D1::Point2F(caretX, boxY + boxHeight - dpi(app, 4.0f)),
                    app.brush, 1.0f);
            }
        };

        float btnSize = g.btnSize;
        float fileBtnX = g.fileBtnX;
        float folderBtnX = g.folderBtnX;
        float btnY = g.btnY;
        float headerCenterY = btnY + btnSize / 2;

        if (app.folderBrowserEditingPath) {
            drawInputBox(g.cardLeft + padding, g.cardRight - padding,
                         headerCenterY - boxHeight / 2);
        } else {
            // Breadcrumb: drive > ellipsis > parent > leaf; each visible
            // segment records how many components a click keeps
            std::vector<std::wstring> parts;
            {
                std::wstring rest = app.folderBrowserPath;
                size_t pos = 0;
                while (pos <= rest.size()) {
                    size_t sep = rest.find_first_of(L"\\/", pos);
                    if (sep == std::wstring::npos) {
                        if (pos < rest.size()) parts.push_back(rest.substr(pos));
                        break;
                    }
                    if (sep > pos) parts.push_back(rest.substr(pos, sep - pos));
                    pos = sep + 1;
                }
            }
            struct Crumb { std::wstring text; int keep; bool leaf; };
            std::vector<Crumb> crumbs;
            int n = (int)parts.size();
            if (n <= 3) {
                for (int i = 0; i < n; i++) {
                    crumbs.push_back({parts[i], i + 1, i == n - 1});
                }
            } else {
                crumbs.push_back({parts[0], 1, false});
                crumbs.push_back({L"\u2026", 0, false});
                crumbs.push_back({parts[n - 2], n - 1, false});
                crumbs.push_back({parts[n - 1], n, true});
            }

            float cx = g.cardLeft + padding;
            float crumbMax = g.pinBtnX - dpi(app, 26.0f);

            // The leaf always survives: drop the parent crumb first when
            // the row runs out of room, then trim the leaf as a last resort
            {
                float sepW = dpi(app, 10.0f);
                auto totalW = [&]() {
                    float w = 0.0f;
                    for (size_t ci = 0; ci < crumbs.size(); ci++) {
                        w += textWidth(crumbs[ci].text) + dpi(app, 4.0f);
                        if (ci + 1 < crumbs.size()) w += sepW;
                    }
                    return w;
                };
                if (crumbs.size() >= 4 && cx + totalW() > crumbMax) {
                    crumbs.erase(crumbs.end() - 2);  // the parent yields
                }
                if (crumbs.size() >= 3 && cx + totalW() > crumbMax) {
                    crumbs.erase(crumbs.begin());  // then the drive
                }
            }
            D2D1_COLOR_F sepColor = app.theme.text; sepColor.a = 0.35f * anim;
            for (size_t ci = 0; ci < crumbs.size(); ci++) {
                std::wstring seg = crumbs[ci].text;
                float segW = textWidth(seg);
                // The leaf keeps at least a readable stub; middle crumbs
                // give way when the row runs out
                if (cx + segW > crumbMax) {
                    // Trim with an ellipsis; the leaf keeps a readable stub
                    while (seg.size() > 4 &&
                           cx + textWidth(seg + L"\u2026") > crumbMax) {
                        seg.pop_back();
                    }
                    if (!crumbs[ci].leaf &&
                        cx + textWidth(seg + L"\u2026") > crumbMax) {
                        break;
                    }
                    seg += L"\u2026";
                    segW = textWidth(seg);
                }
                bool leaf = crumbs[ci].leaf;
                D2D1_COLOR_F segColor = leaf ? app.theme.heading : app.theme.text;
                segColor.a = (leaf ? 1.0f : 0.6f) * anim;
                app.brush->SetColor(segColor);
                app.renderTarget->DrawText(seg.c_str(), (UINT32)seg.size(),
                    leaf && app.tocFormatBold ? app.tocFormatBold : browserFormat,
                    D2D1::RectF(cx, headerCenterY - dpi(app, 9.0f),
                                cx + segW + dpi(app, 4.0f), headerY + g.headerH),
                    app.brush);
                if (!leaf && crumbs[ci].keep > 0) {
                    app.folderCrumbHits.push_back(
                        {D2D1::RectF(cx - dpi(app, 2.0f), btnY,
                                     cx + segW + dpi(app, 2.0f), btnY + btnSize),
                         crumbs[ci].keep});
                }
                cx += segW + dpi(app, 4.0f);
                if (ci + 1 < crumbs.size()) {
                    app.brush->SetColor(sepColor);
                    app.renderTarget->DrawText(L"\u203A", 1, browserFormat,
                        D2D1::RectF(cx, headerCenterY - dpi(app, 9.0f),
                                    cx + dpi(app, 8.0f), headerY + g.headerH),
                        app.brush);
                    cx += dpi(app, 10.0f);
                }
            }
            // The pencil: edit the path as text
            {
                D2D1_COLOR_F pc = app.theme.text; pc.a = 0.45f * anim;
                app.brush->SetColor(pc);
                app.renderTarget->DrawText(L"\u270E", 1, browserFormat,
                    D2D1::RectF(cx + dpi(app, 2.0f), headerCenterY - dpi(app, 9.0f),
                                cx + dpi(app, 18.0f), headerY + g.headerH),
                    app.brush);
            }

            // New folder / new file: monoline glyph buttons; the active
            // naming target wears an accent tint and ring
            auto drawAddButton = [&](float bx, bool isFolder) {
                bool naming = app.folderBrowserNaming == (isFolder ? 2 : 1);
                bool hovered = app.mouseX >= bx && app.mouseX <= bx + btnSize &&
                               app.mouseY >= btnY && app.mouseY <= btnY + btnSize;
                if (naming || hovered) {
                    D2D1_COLOR_F bg = app.theme.accent;
                    bg.a = (naming ? 0.16f : 0.1f) * anim;
                    app.brush->SetColor(bg);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(bx, btnY, bx + btnSize, btnY + btnSize),
                                          dpi(app, 5.0f), dpi(app, 5.0f)),
                        app.brush);
                    if (naming) {
                        D2D1_COLOR_F ring = app.theme.accent; ring.a = 0.4f * anim;
                        app.brush->SetColor(ring);
                        app.renderTarget->DrawRoundedRectangle(
                            D2D1::RoundedRect(D2D1::RectF(bx, btnY, bx + btnSize, btnY + btnSize),
                                              dpi(app, 5.0f), dpi(app, 5.0f)),
                            app.brush, 1.0f);
                    }
                }
                D2D1_COLOR_F glyphColor;
                if (naming) {
                    glyphColor = app.theme.accent; glyphColor.a = anim;
                } else {
                    glyphColor = app.theme.text; glyphColor.a = 0.6f * anim;
                }
                float gcx = bx + btnSize * 0.5f, gcy = btnY + btnSize * 0.5f;
                if (isFolder) drawFolderGlyph(app, gcx, gcy, dpi(app, 13.0f), glyphColor, true);
                else drawPageGlyph(app, gcx, gcy, dpi(app, 13.0f), glyphColor, true);
            };
            drawAddButton(folderBtnX, true);
            drawAddButton(fileBtnX, false);

            // Pin toggle (#156): a pinned browser survives document clicks
            {
                bool hoveredP =
                    app.mouseX >= g.pinBtnX && app.mouseX <= g.pinBtnX + btnSize &&
                    app.mouseY >= btnY && app.mouseY <= btnY + btnSize;
                if (app.browserPinned || hoveredP) {
                    D2D1_COLOR_F bg = app.theme.accent;
                    bg.a = (app.browserPinned ? 0.16f : 0.1f) * anim;
                    app.brush->SetColor(bg);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(
                            D2D1::RectF(g.pinBtnX, btnY, g.pinBtnX + btnSize,
                                        btnY + btnSize),
                            dpi(app, 5.0f), dpi(app, 5.0f)),
                        app.brush);
                }
                D2D1_COLOR_F pc = app.browserPinned ? app.theme.accent
                                                    : app.theme.text;
                pc.a = (app.browserPinned ? 0.95f : 0.6f) * anim;
                drawPinGlyph(app, g.pinBtnX + btnSize * 0.5f,
                             btnY + btnSize * 0.5f, pc);
                app.folderPinRect = D2D1::RectF(g.pinBtnX, btnY,
                                                g.pinBtnX + btnSize,
                                                btnY + btnSize);
            }
        }

        // Divider line
        float dividerY = headerY + g.headerH + dpi(app, 4.0f);
        D2D1_COLOR_F div = app.theme.text; div.a = 0.1f;
        app.brush->SetColor(div);
        app.renderTarget->FillRectangle(
            D2D1::RectF(g.cardLeft + padding, dividerY, g.cardRight - padding,
                        dividerY + 1.0f),
            app.brush);

        // Items list (with scrolling); an active naming row occupies the
        // first slot and the real items shift down beneath it
        float listStartY = g.listStartY;
        float listHeight = g.listBottom - listStartY;
        float namingOffset = g.namingOffset;
        float totalItemsHeight = namingOffset +
            (app.folderItems.empty()
                 ? 0.0f
                 : folderItemContentY(g, (int)app.folderItems.size() - 1) +
                       g.itemHeight);

        // Clamp scroll
        float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);
        app.folderBrowserScroll = std::max(0.0f, std::min(app.folderBrowserScroll, maxScroll));

        app.hoveredFolderIndex = -1;

        // Preview mode: mark the file currently loaded in the viewer so
        // the list reads as a picker while clicking through documents
        std::wstring currentName;
        {
            std::wstring wcur = toWide(app.currentFile);
            std::wstring wdir = app.folderBrowserPath;
            if (!wdir.empty() && wdir.back() != L'\\' && wdir.back() != L'/') {
                wdir += L'\\';
            }
            if (wcur.size() > wdir.size() &&
                _wcsnicmp(wcur.c_str(), wdir.c_str(), wdir.size()) == 0 &&
                wcur.find_first_of(L"\\/", wdir.size()) == std::wstring::npos) {
                currentName = wcur.substr(wdir.size());
            }
        }

        float rowLeft = g.cardLeft + dpi(app, 6.0f);
        float rowRight = g.cardRight - dpi(app, 6.0f);
        app.renderTarget->PushAxisAlignedClip(
            D2D1::RectF(g.cardLeft, listStartY + namingOffset, g.cardRight,
                        g.listBottom),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // Section labels ride inside the scroll like rows do
        auto drawLabel = [&](const char* key, float y) {
            if (!app.signalSmallFormat) return;
            D2D1_COLOR_F lc = app.theme.text; lc.a = 0.45f * anim;
            app.brush->SetColor(lc);
            const wchar_t* text = tr(app, key);
            app.renderTarget->DrawText(text, (UINT32)wcslen(text),
                app.signalSmallFormat,
                D2D1::RectF(rowLeft + dpi(app, 8.0f), y + dpi(app, 4.0f),
                            rowRight, y + g.labelH),
                app.brush);
        };
        float contentBase = listStartY + namingOffset - app.folderBrowserScroll;
        if (g.hasDirs) drawLabel("browser.folders", contentBase);
        if (g.hasFiles) {
            float filesLabelY = contentBase + (g.hasDirs ? g.labelH : 0.0f) +
                                g.dirCount * itemHeight;
            drawLabel("browser.files", filesLabelY);
        }

        for (size_t i = 0; i < app.folderItems.size(); i++) {
            float itemY = contentBase + folderItemContentY(g, (int)i);

            // Skip items outside visible area
            if (itemY + itemHeight < listStartY || itemY > g.listBottom) continue;

            const auto& item = app.folderItems[i];

            // Check hover
            bool isHovered = (app.mouseX >= rowLeft && app.mouseX <= rowRight &&
                              app.mouseY >= itemY && app.mouseY <= itemY + itemHeight &&
                              app.mouseY >= listStartY + namingOffset &&
                              app.mouseY <= g.listBottom);

            bool isCurrent = !item.isDirectory && !currentName.empty() &&
                             _wcsicmp(item.name.c_str(), currentName.c_str()) == 0;
            bool isMd = !item.isDirectory &&
                        qmd::fileRefIsMarkdown(toUtf8(item.name));

            D2D1_RECT_F rowRect = D2D1::RectF(rowLeft, itemY + 1.0f, rowRight,
                                              itemY + itemHeight - 1.0f);
            if (isCurrent) {
                // Preview mode: the open file wears the accent wash + ring
                D2D1_COLOR_F curColor = app.theme.accent;
                curColor.a = 0.14f * anim;
                app.brush->SetColor(curColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(rowRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                    app.brush);
                D2D1_COLOR_F ring = app.theme.accent;
                ring.a = 0.4f * anim;
                app.brush->SetColor(ring);
                app.renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(rowRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                    app.brush, 1.0f);
            }
            if (isHovered) {
                app.hoveredFolderIndex = (int)i;
                D2D1_COLOR_F hoverColor = app.theme.text;
                hoverColor.a = 0.06f * anim;
                app.brush->SetColor(hoverColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(rowRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                    app.brush);
            }

            // Monoline glyphs: folders and markdown carry the accent,
            // everything else stays quiet ink (design 13b)
            float gcx = rowLeft + dpi(app, 15.0f);
            float gcy = itemY + itemHeight * 0.5f;
            float textX = rowLeft + dpi(app, 28.0f);
            D2D1_COLOR_F glyphColor;
            if (item.isDirectory || isMd) {
                glyphColor = app.theme.accent;
                glyphColor.a = (item.isDirectory ? 0.85f : 0.9f) * anim;
            } else {
                glyphColor = app.theme.text;
                glyphColor.a = 0.45f * anim;
            }
            if (item.isDirectory) {
                drawFolderGlyph(app, gcx, gcy, dpi(app, 13.0f), glyphColor, false);
            } else {
                drawPageGlyph(app, gcx, gcy, dpi(app, 13.0f), glyphColor, false);
            }

            // Item name: markdown first-class, other files dimmed
            D2D1_COLOR_F textColor;
            if (isCurrent) {
                textColor = app.theme.heading;
                textColor.a = anim;
            } else if (!item.isDirectory && !isMd) {
                textColor = app.theme.text;
                textColor.a = 0.5f * anim;
            } else {
                textColor = app.theme.text;
                textColor.a = 0.85f * anim;
            }
            app.brush->SetColor(textColor);
            app.renderTarget->DrawText(item.name.c_str(), (UINT32)item.name.length(),
                isCurrent && app.tocFormatBold ? app.tocFormatBold : browserFormat,
                D2D1::RectF(textX, itemY + dpi(app, 4.0f), rowRight - dpi(app, 6.0f),
                            itemY + itemHeight),
                app.brush);
        }
        app.renderTarget->PopAxisAlignedClip();

        // Naming row for a new file/folder: pinned to the top of the list,
        // drawn after the items so they scroll underneath it
        if (app.folderBrowserNaming != 0) {
            bool isFolder = app.folderBrowserNaming == 2;
            float rowY = listStartY;
            float gcx = rowLeft + dpi(app, 15.0f);
            float textX = rowLeft + dpi(app, 28.0f);

            app.brush->SetColor(panelBg);
            app.renderTarget->FillRectangle(
                D2D1::RectF(rowLeft, rowY, rowRight, rowY + itemHeight),
                app.brush);

            D2D1_COLOR_F glyphColor = app.theme.accent;
            glyphColor.a = anim;
            if (isFolder) {
                drawFolderGlyph(app, gcx, rowY + itemHeight * 0.5f,
                                dpi(app, 13.0f), glyphColor, true);
            } else {
                drawPageGlyph(app, gcx, rowY + itemHeight * 0.5f,
                              dpi(app, 13.0f), glyphColor, true);
            }
            drawInputBox(textX, rowRight, rowY);
        }

        // Scrollbar if needed
        if (totalItemsHeight > listHeight) {
            float sbHeight = listHeight / totalItemsHeight * listHeight;
            sbHeight = std::max(sbHeight, dpi(app, 20.0f));
            float sbY = listStartY + (maxScroll > 0 ? (app.folderBrowserScroll / maxScroll * (listHeight - sbHeight)) : 0);

            D2D1_COLOR_F sbColor = app.theme.text;
            sbColor.a = 0.25f * anim;
            app.brush->SetColor(sbColor);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(g.cardRight - dpi(app, 6.0f), sbY,
                                              g.cardRight - dpi(app, 3.0f), sbY + sbHeight), 1.5f, 1.5f),
                app.brush);
        }

        // Footer: counts on the left, the key hints on the right
        float footTop = g.listBottom;
        app.brush->SetColor(div);
        app.renderTarget->FillRectangle(
            D2D1::RectF(g.cardLeft + padding, footTop, g.cardRight - padding,
                        footTop + 1.0f),
            app.brush);
        if (app.signalSmallFormat) {
            D2D1_COLOR_F hc = app.theme.text; hc.a = 0.48f * anim;
            app.brush->SetColor(hc);
            wchar_t counts[96];
            if (app.folderBrowserNaming != 0) {
                wcscpy_s(counts, tr(app, app.folderBrowserNaming == 2
                                             ? "browser.naming_folder"
                                             : "browser.naming_file"));
            } else {
                int dirs = g.dirCount;
                for (const auto& it : app.folderItems) {
                    if (it.isDirectory && it.name == L"..") { dirs--; break; }
                }
                swprintf_s(counts, tr(app, "browser.counts"), dirs,
                           (int)app.folderItems.size() - g.dirCount);
            }
            app.renderTarget->DrawText(counts, (UINT32)wcslen(counts),
                app.signalSmallFormat,
                D2D1::RectF(g.cardLeft + padding, footTop + dpi(app, 7.0f),
                            g.cardRight - padding, g.cardBottom),
                app.brush);
            const wchar_t* hint = tr(app, app.folderBrowserNaming != 0
                                              ? "browser.naming_hint"
                                              : "browser.hint");
            float hw = measureText(app, hint, app.signalSmallFormat);
            app.renderTarget->DrawText(hint, (UINT32)wcslen(hint),
                app.signalSmallFormat,
                D2D1::RectF(g.cardRight - padding - hw, footTop + dpi(app, 7.0f),
                            g.cardRight - padding + dpi(app, 2.0f), g.cardBottom),
                app.brush);
        }
    }
}

// Click hit-test sharing the renderer's card geometry (#114 pattern:
// render-time hover must never drive click actions). Keep in step with
// renderToc below.
int tocItemIndexAt(App& app, float x, float y) {
    float panelWidth = tocPanelWidth(app);
    float panelX = tocPanelX(app, panelWidth);
    float m = dpi(app, 10.0f);
    float cardLeft = panelX + m;
    float cardRight = panelX + panelWidth - m;
    float cardTop = chromeTopHeight(app) + dpi(app, 10.0f);
    float cardBottom = (float)app.height - dpi(app, 12.0f);
    float headerY = cardTop + dpi(app, 9.0f);
    float dividerY = headerY + dpi(app, 34.0f) - dpi(app, 8.0f);
    float listStartY = dividerY + dpi(app, 8.0f);
    float listBottom = cardBottom - dpi(app, 26.0f);
    float rowLeft = cardLeft + dpi(app, 14.0f);
    float rowRight = cardRight - dpi(app, 8.0f);
    float itemHeight = dpi(app, 26.0f);
    if (x < rowLeft || x > rowRight || y < listStartY || y > listBottom)
        return -1;
    int row = (int)((y - listStartY + app.tocScroll) / itemHeight);
    if (row < 0) return -1;
    std::wstring needle = toLower(app.tocFilter);
    int visIdx = -1;
    for (size_t i = 0; i < app.headings.size(); i++) {
        if (!needle.empty() &&
            toLower(app.headings[i].text).find(needle) == std::wstring::npos) {
            continue;
        }
        if (++visIdx == row) return (int)i;
    }
    return -1;
}

void renderToc(App& app) {
    // Animate in (slide from right) - only invalidate while progressing
    if (app.tocAnimation < 1.0f) {
        float prev = app.tocAnimation;
        app.tocAnimation = std::min(1.0f, app.tocAnimation + 0.15f);
        if (app.tocAnimation != prev)
            InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.tocAnimation;

    // Floating outline card (t13 design 13a): the panel leaves its
    // full-height slab and floats inside the slab's slide envelope
    float panelWidth = tocPanelWidth(app);
    float panelX = tocPanelX(app, panelWidth);  // slides from the chosen side
    float m = dpi(app, 10.0f);
    float cardLeft = panelX + m;
    float cardRight = panelX + panelWidth - m;
    float cardTop = chromeTopHeight(app) + dpi(app, 10.0f);
    float cardBottom = (float)app.height - dpi(app, 12.0f);
    D2D1_RECT_F card = D2D1::RectF(cardLeft, cardTop, cardRight, cardBottom);
    float radius = dpi(app, 12.0f);

    promptChipShadow(app, card, radius);
    D2D1_COLOR_F surf = promptChipSurface(app);
    surf.a = 0.96f;
    app.brush->SetColor(surf);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(card, radius, radius), app.brush);
    // Inset top highlight, then the hairline border
    D2D1_COLOR_F hi = D2D1::ColorF(1, 1, 1, app.theme.isDark ? 0.06f : 0.5f);
    app.brush->SetColor(hi);
    app.renderTarget->DrawLine(
        D2D1::Point2F(cardLeft + radius, cardTop + 1.0f),
        D2D1::Point2F(cardRight - radius, cardTop + 1.0f), app.brush, 1.0f);
    D2D1_COLOR_F borderColor = app.theme.text;
    borderColor.a = 0.13f;
    app.brush->SetColor(borderColor);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(card, radius, radius), app.brush, 1.0f);

    app.tocCloseRect = D2D1_RECT_F{};
    app.tocPinRect = D2D1_RECT_F{};
    IDWriteTextFormat* tocBold = app.tocFormatBold;
    IDWriteTextFormat* tocNormal = app.tocFormat;
    if (tocBold && tocNormal) {
        float padding = dpi(app, 12.0f);
        float itemHeight = dpi(app, 26.0f);
        float headerHeight = dpi(app, 34.0f);
        float footerHeight = dpi(app, 26.0f);

        // Header: title, count badge, live filter, close cross
        float headerY = cardTop + dpi(app, 9.0f);
        D2D1_COLOR_F headerColor = app.theme.heading;
        headerColor.a = anim;
        app.brush->SetColor(headerColor);
        const wchar_t* tocTitle = tr(app, "toc.title");
        app.renderTarget->DrawText(tocTitle, (UINT32)wcslen(tocTitle), tocBold,
            D2D1::RectF(cardLeft + padding, headerY, cardRight - padding,
                        headerY + headerHeight),
            app.brush);
        float titleW = measureText(app, tocTitle, tocBold);

        if (!app.headings.empty() && app.signalSmallFormat) {
            wchar_t count[8];
            swprintf_s(count, L"%d", (int)app.headings.size());
            float cw = measureText(app, count, app.signalSmallFormat);
            D2D1_RECT_F badge = D2D1::RectF(
                cardLeft + padding + titleW + dpi(app, 8.0f),
                headerY + dpi(app, 2.0f),
                cardLeft + padding + titleW + dpi(app, 8.0f) + cw + dpi(app, 11.0f),
                headerY + dpi(app, 17.0f));
            D2D1_COLOR_F bb = app.theme.text; bb.a = 0.07f * anim;
            app.brush->SetColor(bb);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(badge, dpi(app, 7.0f), dpi(app, 7.0f)),
                app.brush);
            D2D1_COLOR_F bi = app.theme.text; bi.a = 0.6f * anim;
            app.brush->SetColor(bi);
            app.renderTarget->DrawText(count, (UINT32)wcslen(count),
                app.signalSmallFormat,
                D2D1::RectF(badge.left + dpi(app, 5.5f), badge.top + dpi(app, 1.5f),
                            badge.right, badge.bottom),
                app.brush);
        }

        // Close cross at the right edge of the header
        {
            float ccx = cardRight - padding - dpi(app, 4.0f);
            float ccy = headerY + dpi(app, 9.0f);
            D2D1_COLOR_F c = app.theme.text; c.a = 0.45f * anim;
            app.brush->SetColor(c);
            float s = dpi(app, 3.6f);
            app.renderTarget->DrawLine(D2D1::Point2F(ccx - s, ccy - s),
                                       D2D1::Point2F(ccx + s, ccy + s), app.brush, 1.3f);
            app.renderTarget->DrawLine(D2D1::Point2F(ccx - s, ccy + s),
                                       D2D1::Point2F(ccx + s, ccy - s), app.brush, 1.3f);
            app.tocCloseRect = D2D1::RectF(ccx - dpi(app, 10.0f), cardTop,
                                           cardRight, headerY + dpi(app, 20.0f));

            // Pin toggle left of the cross (#156)
            float pcx = ccx - dpi(app, 20.0f);
            D2D1_COLOR_F pc = app.tocPinned ? app.theme.accent : app.theme.text;
            pc.a = (app.tocPinned ? 0.95f : 0.45f) * anim;
            drawPinGlyph(app, pcx, ccy, pc);
            app.tocPinRect = D2D1::RectF(pcx - dpi(app, 9.0f), cardTop,
                                         pcx + dpi(app, 9.0f),
                                         headerY + dpi(app, 20.0f));
        }

        // Active filter, right-aligned before the cross
        if (!app.tocFilter.empty() && app.dwriteFactory) {
            IDWriteTextLayout* filterLayout = nullptr;
            app.dwriteFactory->CreateTextLayout(
                app.tocFilter.c_str(), (UINT32)app.tocFilter.size(),
                tocNormal, panelWidth, headerHeight, &filterLayout);
            if (filterLayout) {
                DWRITE_TEXT_METRICS fm{};
                filterLayout->GetMetrics(&fm);
                D2D1_COLOR_F filterColor = app.theme.accent;
                filterColor.a = anim;
                app.brush->SetColor(filterColor);
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(cardRight - padding - dpi(app, 38.0f) - fm.width,
                                  headerY + dpi(app, 1.0f)),
                    filterLayout, app.brush);
                filterLayout->Release();
            }
        }

        // Divider
        float dividerY = headerY + headerHeight - dpi(app, 8.0f);
        D2D1_COLOR_F div = app.theme.text; div.a = 0.1f;
        app.brush->SetColor(div);
        app.renderTarget->FillRectangle(
            D2D1::RectF(cardLeft + padding, dividerY, cardRight - padding,
                        dividerY + 1.0f),
            app.brush);

        // Items list between header and footer
        float listStartY = dividerY + dpi(app, 8.0f);
        float listBottom = cardBottom - footerHeight;
        float listHeight = listBottom - listStartY;

        // Scroll-thread on the card edge: the document's viewport mapped
        // onto the panel span (design 13a)
        if (app.contentHeight > (float)app.height) {
            float tx = cardLeft + dpi(app, 6.0f);
            D2D1_RECT_F track = D2D1::RectF(tx, listStartY + dpi(app, 4.0f),
                                            tx + dpi(app, 3.0f),
                                            listBottom - dpi(app, 4.0f));
            D2D1_COLOR_F tc = app.theme.text; tc.a = 0.08f * anim;
            app.brush->SetColor(tc);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(track, dpi(app, 1.5f), dpi(app, 1.5f)),
                app.brush);
            float span = track.bottom - track.top;
            float docSpan = std::max(1.0f, app.contentHeight);
            float segTop = track.top + span * (app.scrollY / docSpan);
            float segH = std::max(dpi(app, 18.0f),
                                  span * ((float)app.height / docSpan));
            segTop = std::min(segTop, track.bottom - segH);
            D2D1_COLOR_F ac = app.theme.accent; ac.a = anim;
            app.brush->SetColor(ac);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(track.left, segTop,
                                              track.right, segTop + segH),
                                  dpi(app, 1.5f), dpi(app, 1.5f)),
                app.brush);
        }

        if (app.headings.empty()) {
            // "No headings" message
            D2D1_COLOR_F dimColor = app.theme.text;
            dimColor.a = 0.5f * anim;
            app.brush->SetColor(dimColor);
            const wchar_t* tocEmpty = tr(app, "toc.empty");
            app.renderTarget->DrawText(tocEmpty, (UINT32)wcslen(tocEmpty), tocNormal,
                D2D1::RectF(cardLeft + padding + dpi(app, 6.0f),
                            listStartY + dpi(app, 8.0f), cardRight - padding,
                            listStartY + dpi(app, 40.0f)),
                app.brush);
        } else {
            // Typing filters the list; the section under the reading line
            // stays highlighted and in view (scroll-spy)
            std::vector<int> visible;
            visible.reserve(app.headings.size());
            std::wstring needle = toLower(app.tocFilter);
            for (size_t i = 0; i < app.headings.size(); i++) {
                if (needle.empty() ||
                    toLower(app.headings[i].text).find(needle) !=
                        std::wstring::npos) {
                    visible.push_back((int)i);
                }
            }

            // A clicked heading stays active until the user scrolls: the
            // spy line cannot reach bottom sections in short documents
            int currentIdx = -1;
            if (app.tocSpyOverride >= 0 &&
                app.tocSpyOverride < (int)app.headings.size() &&
                std::fabs(app.targetScrollY - app.tocSpyOverrideScroll) <
                    1.0f) {
                currentIdx = app.tocSpyOverride;
            } else {
                app.tocSpyOverride = -1;
                float spyLine = app.scrollY + (float)app.height * 0.25f;
                for (size_t i = 0; i < app.headings.size(); i++) {
                    if (app.headings[i].y <= spyLine) currentIdx = (int)i;
                }
                if (currentIdx < 0 && !app.headings.empty()) currentIdx = 0;
            }

            float totalItemsHeight = visible.size() * itemHeight;
            float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);

            // Follow the document: when the current section changes, keep
            // its row inside the list viewport
            static int lastSpyIndex = -1;
            if (currentIdx != lastSpyIndex && app.tocFilter.empty()) {
                lastSpyIndex = currentIdx;
                for (size_t row = 0; row < visible.size(); row++) {
                    if (visible[row] != currentIdx) continue;
                    float rowTop = (float)row * itemHeight;
                    if (rowTop < app.tocScroll ||
                        rowTop + itemHeight > app.tocScroll + listHeight) {
                        app.tocScroll = rowTop - listHeight * 0.4f;
                    }
                    break;
                }
            }
            app.tocScroll = std::max(0.0f, std::min(app.tocScroll, maxScroll));

            app.hoveredTocIndex = -1;

            if (visible.empty()) {
                D2D1_COLOR_F dimColor = app.theme.text;
                dimColor.a = 0.5f * anim;
                app.brush->SetColor(dimColor);
                const wchar_t* tocEmpty = tr(app, "toc.empty");
                app.renderTarget->DrawText(tocEmpty, (UINT32)wcslen(tocEmpty),
                    tocNormal,
                    D2D1::RectF(cardLeft + padding + dpi(app, 6.0f),
                                listStartY + dpi(app, 8.0f),
                                cardRight - padding,
                                listStartY + dpi(app, 40.0f)),
                    app.brush);
            }

            float rowLeft = cardLeft + dpi(app, 14.0f);
            float rowRight = cardRight - dpi(app, 8.0f);
            app.renderTarget->PushAxisAlignedClip(
                D2D1::RectF(cardLeft, listStartY, cardRight, listBottom),
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for (size_t row = 0; row < visible.size(); row++) {
                float itemY = listStartY + row * itemHeight - app.tocScroll;

                // Skip items outside visible area
                if (itemY + itemHeight < listStartY || itemY > listBottom) continue;

                const auto& heading = app.headings[visible[row]];
                float indent = (heading.level - 1) * dpi(app, 13.0f);
                float itemX = rowLeft + dpi(app, 10.0f) + indent;
                bool isCurrent = visible[row] == currentIdx;

                // Check hover (use full item width for hit area)
                bool isHovered = (app.mouseX >= rowLeft && app.mouseX <= rowRight &&
                                  app.mouseY >= itemY && app.mouseY <= itemY + itemHeight &&
                                  app.mouseY >= listStartY && app.mouseY <= listBottom);

                D2D1_RECT_F rowRect = D2D1::RectF(rowLeft, itemY + 1.0f,
                                                  rowRight, itemY + itemHeight - 1.0f);
                if (isHovered) {
                    app.hoveredTocIndex = visible[row];
                    D2D1_COLOR_F hoverColor = app.theme.text;
                    hoverColor.a = 0.06f * anim;
                    app.brush->SetColor(hoverColor);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(rowRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                        app.brush);
                } else if (isCurrent) {
                    // Scroll-spy pill: the accent-washed active row
                    D2D1_COLOR_F spyColor = app.theme.accent;
                    spyColor.a = 0.13f * anim;
                    app.brush->SetColor(spyColor);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(rowRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                        app.brush);
                }
                if (isCurrent) {
                    D2D1_COLOR_F barColor = app.theme.accent;
                    barColor.a = anim;
                    app.brush->SetColor(barColor);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(
                            D2D1::RectF(rowLeft + dpi(app, 3.0f),
                                        itemY + dpi(app, 6.0f),
                                        rowLeft + dpi(app, 5.5f),
                                        itemY + itemHeight - dpi(app, 6.0f)),
                            1.5f, 1.5f),
                        app.brush);
                }

                // Type scale by level: H1 bold heading ink, H2 body, H3 quiet
                IDWriteTextFormat* fmt =
                    (heading.level == 1 || isCurrent) ? tocBold : tocNormal;
                D2D1_COLOR_F textColor;
                if (heading.level == 1 || isCurrent) {
                    textColor = app.theme.heading;
                    textColor.a = anim;
                } else if (heading.level >= 3) {
                    textColor = app.theme.text;
                    textColor.a = 0.55f * anim;
                } else {
                    textColor = app.theme.text;
                    textColor.a = 0.82f * anim;
                }
                app.brush->SetColor(textColor);
                app.renderTarget->DrawText(heading.text.c_str(),
                    (UINT32)heading.text.length(), fmt,
                    D2D1::RectF(itemX, itemY + dpi(app, 4.0f),
                                rowRight - dpi(app, 16.0f), itemY + itemHeight),
                    app.brush);

                if (isHovered) {
                    D2D1_COLOR_F arr = app.theme.text; arr.a = 0.45f * anim;
                    app.brush->SetColor(arr);
                    app.renderTarget->DrawText(L"\u2192", 1, tocNormal,
                        D2D1::RectF(rowRight - dpi(app, 15.0f),
                                    itemY + dpi(app, 4.0f), rowRight,
                                    itemY + itemHeight),
                        app.brush);
                }
            }
            app.renderTarget->PopAxisAlignedClip();

            // Scrollbar if needed
            if (totalItemsHeight > listHeight) {
                float sbHeight = listHeight / totalItemsHeight * listHeight;
                sbHeight = std::max(sbHeight, dpi(app, 20.0f));
                float sbY = listStartY + (maxScroll > 0 ? (app.tocScroll / maxScroll * (listHeight - sbHeight)) : 0);

                D2D1_COLOR_F sbColor = app.theme.text;
                sbColor.a = 0.25f * anim;
                app.brush->SetColor(sbColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(cardRight - dpi(app, 6.0f), sbY,
                                                  cardRight - dpi(app, 3.0f), sbY + sbHeight), 1.5f, 1.5f),
                    app.brush);
            }
        }

        // Footer: the hint plus the T keycap
        float footTop = listBottom;
        app.brush->SetColor(div);
        app.renderTarget->FillRectangle(
            D2D1::RectF(cardLeft + padding, footTop, cardRight - padding,
                        footTop + 1.0f),
            app.brush);
        if (app.signalSmallFormat) {
            const wchar_t* hint = tr(app, "toc.hint");
            D2D1_COLOR_F hc = app.theme.text; hc.a = 0.48f * anim;
            app.brush->SetColor(hc);
            app.renderTarget->DrawText(hint, (UINT32)wcslen(hint),
                app.signalSmallFormat,
                D2D1::RectF(cardLeft + padding, footTop + dpi(app, 7.0f),
                            cardRight - padding - dpi(app, 26.0f), cardBottom),
                app.brush);
            promptKeycap(app, L"T", cardRight - padding,
                         footTop + footerHeight * 0.5f);
        }
    }
}

// --- Link peek ---

void renderLinkPeek(App& app) {
    if (!app.linkPeekActive || !app.linkPeekBitmap ||
        app.hoveredLink != app.linkPeekUrl || !app.folderBrowserFormat) {
        return;
    }
    const D2D1_RECT_F& p = app.linkPeekPanel;
    float titleH = dpi(app, 22.0f);

    // Soft drop shadow so the page reads as floating above the document
    D2D1_COLOR_F shadow = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.28f);
    app.brush->SetColor(shadow);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(p.left + 4, p.top + 5, p.right + 4,
                                      p.bottom + 5),
                          4, 4),
        app.brush);

    D2D1_COLOR_F bg = app.theme.isDark ? hexColor(0x1E1E1E, 1.0f)
                                       : hexColor(0xFCFCFA, 1.0f);
    app.brush->SetColor(bg);
    app.renderTarget->FillRectangle(p, app.brush);

    // Title strip: the target's file name
    D2D1_COLOR_F titleColor = app.theme.accent;
    app.brush->SetColor(titleColor);
    app.renderTarget->DrawText(
        app.linkPeekTitle.c_str(), (UINT32)app.linkPeekTitle.size(),
        app.folderBrowserFormat,
        D2D1::RectF(p.left + dpi(app, 10.0f), p.top + dpi(app, 3.0f),
                    p.right - dpi(app, 10.0f), p.top + titleH),
        app.brush);

    // The rendered page
    app.renderTarget->DrawBitmap(
        app.linkPeekBitmap,
        D2D1::RectF(p.left + 1.0f, p.top + titleH, p.right - 1.0f,
                    p.bottom - 1.0f),
        1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);

    D2D1_COLOR_F border = app.theme.accent;
    border.a = 0.55f;
    app.brush->SetColor(border);
    app.renderTarget->DrawRectangle(p, app.brush, 1.0f);
}

// --- Image lightbox ---

void openLightbox(App& app, ID2D1Bitmap* bitmap) {
    if (!bitmap) return;
    closeLightbox(app);
    bitmap->AddRef();
    app.lightboxBitmap = bitmap;
    app.showLightbox = true;
    app.lightboxZoom = 1.0f;
    app.lightboxPanX = 0.0f;
    app.lightboxPanY = 0.0f;
    app.lightboxDragging = false;
}

void closeLightbox(App& app) {
    if (app.lightboxBitmap) {
        app.lightboxBitmap->Release();
        app.lightboxBitmap = nullptr;
    }
    app.showLightbox = false;
    app.lightboxDragging = false;
}

D2D1_RECT_F lightboxImageRect(const App& app) {
    if (!app.lightboxBitmap) return D2D1_RECT_F{};
    D2D1_SIZE_F size = app.lightboxBitmap->GetSize();
    float availW = (float)app.width * 0.94f;
    float availH = (float)app.height - chromeTopHeight(app) - dpi(app, 24.0f);
    // Fit large images to the view, keep small ones at natural size
    float fit = std::min(1.0f, std::min(availW / std::max(1.0f, size.width),
                                        availH / std::max(1.0f, size.height)));
    float scale = fit * app.lightboxZoom;
    float w = size.width * scale;
    float h = size.height * scale;
    float cx = (float)app.width * 0.5f + app.lightboxPanX;
    float cy = (chromeTopHeight(app) + (float)app.height) * 0.5f +
               app.lightboxPanY;
    return D2D1::RectF(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f,
                       cy + h * 0.5f);
}

void renderLightbox(App& app) {
    if (!app.lightboxBitmap || !app.renderTarget || !app.brush) return;
    app.brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.82f));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);
    D2D1_RECT_F dest = lightboxImageRect(app);
    app.renderTarget->DrawBitmap(
        app.lightboxBitmap, dest, 1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
    // Thin border so light images do not bleed into the backdrop
    app.brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.18f));
    app.renderTarget->DrawRectangle(dest, app.brush, 1.0f);
}

float tocPanelX(const App& app, float panelWidth) {
    if (app.tocOnLeft) {
        // A left-docked TOC sits beside an open browser, not on top of it
        float base = app.showFolderBrowser
                         ? folderBrowserPanelWidth(app) *
                               app.folderBrowserAnimation
                         : 0.0f;
        return base - panelWidth * (1.0f - app.tocAnimation);
    }
    return app.width - panelWidth * app.tocAnimation;
}

int themeChooserRows() {
    int light = 0, dark = 0;
    for (int i = 0; i < themeCount(); i++) {
        (themeAt(i).isDark ? dark : light)++;
    }
    return std::max(5, std::max(light, dark));
}

void themeChooserCell(int themeIndex, int& col, int& row) {
    int light = 0, dark = 0;
    col = 0;
    row = 0;
    for (int i = 0; i <= themeIndex && i < themeCount(); i++) {
        bool d = themeAt(i).isDark;
        if (i == themeIndex) {
            col = d ? 1 : 0;
            row = d ? dark : light;
        }
        (d ? dark : light)++;
    }
}

void renderThemeChooser(App& app) {
    // Preview formats are created lazily on first open (not at startup)
    ensureThemePreviewFormats(app);
    app.themeChooserHits.clear();

    // Animate in - only invalidate while progressing
    if (app.themeChooserAnimation < 1.0f) {
        float prev = app.themeChooserAnimation;
        app.themeChooserAnimation = std::min(1.0f, app.themeChooserAnimation + 0.15f);
        if (app.themeChooserAnimation != prev)
            InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.themeChooserAnimation;

    // The panel chrome follows the COMMITTED theme: while a hover preview
    // repaints the app behind the scrim, the chooser itself stays put
    // (design 12b)
    const D2DTheme& chrome = themeAt(app.themeChooserPreviewBase >= 0
                                         ? app.themeChooserPreviewBase
                                         : app.currentThemeIndex);
    D2D1_COLOR_F ink = chrome.text;

    // Scrim: dim the live app but keep it recognizable for hover preview
    float backdropAlpha = (chrome.isDark ? 0.45f : 0.3f) * anim;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    // Layout: two columns of cards under section labels, header row above,
    // footer row below (design t12)
    int rows = themeChooserRows();
    float pad = dpi(app, 22.0f);
    float headerH = dpi(app, 46.0f);
    float labelH = dpi(app, 22.0f);
    float footerH = dpi(app, 32.0f);
    float gap = dpi(app, 8.0f);
    float panelWidth = std::min(dpi(app, 860.0f), app.width - dpi(app, 64.0f));
    float cardH = std::min(
        dpi(app, 74.0f),
        (app.height - dpi(app, 60.0f) - headerH - labelH - footerH -
         pad * 2.0f - gap * (rows - 1)) /
            (float)rows);
    float panelHeight = pad * 2.0f + headerH + labelH + footerH +
                        rows * cardH + gap * (rows - 1);
    float panelX = (app.width - panelWidth) / 2;
    float panelY = (app.height - panelHeight) / 2 + (1 - anim) * dpi(app, 40.0f);
    app.themeChooserPanel = D2D1::RectF(panelX, panelY, panelX + panelWidth,
                                        panelY + panelHeight);

    // Panel: the active theme's strip color as an acrylic-ish sheet
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(
        app.themeChooserPanel, dpi(app, 12.0f), dpi(app, 12.0f));
    {
        // Soft drop shadow
        app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.35f * anim));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(panelX + dpi(app, 2.0f), panelY + dpi(app, 6.0f),
                            panelX + panelWidth + dpi(app, 2.0f),
                            panelY + panelHeight + dpi(app, 10.0f)),
                dpi(app, 14.0f), dpi(app, 14.0f)),
            app.brush);
        // The committed theme's strip color, even while previewing
        D2D1_COLOR_F cb = chrome.background;
        float f = chrome.isDark ? 0.78f : 0.955f;
        D2D1_COLOR_F panelColor =
            D2D1::ColorF(cb.r * f, cb.g * f, cb.b * f, 0.96f * anim);
        app.brush->SetColor(panelColor);
        app.renderTarget->FillRoundedRectangle(panelRect, app.brush);
        D2D1_COLOR_F panelBorder = ink;
        panelBorder.a = 0.14f * anim;
        app.brush->SetColor(panelBorder);
        app.renderTarget->DrawRoundedRectangle(panelRect, app.brush, 1.0f);
    }

    // Header: title, key hint, follow toggle, close
    float hy = panelY + pad;
    if (app.themeTitleFormat) {
        app.themeTitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F titleColor = chrome.heading;
        titleColor.a = anim;
        app.brush->SetColor(titleColor);
        const wchar_t* chooseTheme = tr(app, "theme.chooser.title");
        app.renderTarget->DrawText(
            chooseTheme, (UINT32)wcslen(chooseTheme), app.themeTitleFormat,
            D2D1::RectF(panelX + pad, hy - dpi(app, 4.0f),
                        panelX + panelWidth * 0.5f, hy + dpi(app, 26.0f)),
            app.brush);
    }
    if (app.tocFormat) {
        // Key hint beside the title, built from the live keymap
        std::wstring hint = keyLabel(app.keymap[KA_THEME]) + L" / Esc";
        app.tocFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F hintColor = ink;
        hintColor.a = 0.45f * anim;
        app.brush->SetColor(hintColor);
        float titleW = app.themeTitleFormat
                           ? measureText(app, tr(app, "theme.chooser.title"),
                                         app.themeTitleFormat)
                           : dpi(app, 60.0f);
        app.renderTarget->DrawText(
            hint.c_str(), (UINT32)hint.size(), app.tocFormat,
            D2D1::RectF(panelX + pad + titleW + dpi(app, 14.0f),
                        hy + dpi(app, 3.0f), panelX + panelWidth * 0.6f,
                        hy + dpi(app, 22.0f)),
            app.brush);
    }
    // Close button, top-right
    {
        float cs = dpi(app, 26.0f);
        D2D1_RECT_F cb = D2D1::RectF(panelX + panelWidth - pad - cs,
                                     hy - dpi(app, 2.0f),
                                     panelX + panelWidth - pad,
                                     hy - dpi(app, 2.0f) + cs);
        D2D1_COLOR_F xc = ink;
        xc.a = 0.6f * anim;
        app.brush->SetColor(xc);
        float ccx = (cb.left + cb.right) * 0.5f;
        float ccy = (cb.top + cb.bottom) * 0.5f;
        float arm = dpi(app, 4.5f);
        app.renderTarget->DrawLine(D2D1::Point2F(ccx - arm, ccy - arm),
                                   D2D1::Point2F(ccx + arm, ccy + arm),
                                   app.brush, dpi(app, 1.2f));
        app.renderTarget->DrawLine(D2D1::Point2F(ccx - arm, ccy + arm),
                                   D2D1::Point2F(ccx + arm, ccy - arm),
                                   app.brush, dpi(app, 1.2f));
        app.themeChooserHits.push_back({cb, -2});
    }
    // "Follow Windows theme" label + toggle, right of center
    if (app.folderBrowserFormat) {
        float togW = dpi(app, 38.0f);
        float togH = dpi(app, 19.0f);
        float togX = panelX + panelWidth - pad - dpi(app, 26.0f) -
                     dpi(app, 14.0f) - togW;
        float togY = hy + dpi(app, 1.0f);
        bool on = app.followSystemTheme;

        const wchar_t* followWindows = tr(app, "settings.follow_windows");
        float followWidth =
            measureText(app, followWindows, app.folderBrowserFormat);
        D2D1_COLOR_F toggleText = ink;
        toggleText.a = 0.7f * anim;
        app.folderBrowserFormat->SetTextAlignment(
            DWRITE_TEXT_ALIGNMENT_LEADING);
        app.brush->SetColor(toggleText);
        app.renderTarget->DrawText(
            followWindows, (UINT32)wcslen(followWindows),
            app.folderBrowserFormat,
            D2D1::RectF(togX - followWidth - dpi(app, 10.0f),
                        togY + dpi(app, 1.0f), togX - dpi(app, 6.0f),
                        togY + togH),
            app.brush);

        if (on) {
            D2D1_COLOR_F track = chrome.accent;
            track.a = anim;
            app.brush->SetColor(track);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(togX, togY, togX + togW, togY + togH),
                    togH / 2, togH / 2),
                app.brush);
            float lum = 0.299f * chrome.accent.r + 0.587f * chrome.accent.g +
                        0.114f * chrome.accent.b;
            app.brush->SetColor(lum > 0.45f
                                    ? D2D1::ColorF(0.05f, 0.07f, 0.1f, anim)
                                    : D2D1::ColorF(1, 1, 1, anim));
            app.renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(togX + togW - togH / 2,
                                            togY + togH / 2),
                              togH / 2 - dpi(app, 3.0f),
                              togH / 2 - dpi(app, 3.0f)),
                app.brush);
        } else {
            D2D1_COLOR_F track = ink;
            track.a = 0.35f * anim;
            app.brush->SetColor(track);
            app.renderTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(togX, togY, togX + togW, togY + togH),
                    togH / 2, togH / 2),
                app.brush, 1.0f);
            app.brush->SetColor(track);
            app.renderTarget->FillEllipse(
                D2D1::Ellipse(
                    D2D1::Point2F(togX + togH / 2, togY + togH / 2),
                    togH / 2 - dpi(app, 4.0f), togH / 2 - dpi(app, 4.0f)),
                app.brush);
        }
        app.themeChooserHits.push_back(
            {D2D1::RectF(togX - followWidth - dpi(app, 10.0f),
                         togY - dpi(app, 4.0f), togX + togW,
                         togY + togH + dpi(app, 4.0f)),
             -1});
    }

    // Section labels + card columns
    float gridY = panelY + pad + headerH;
    float cardW = (panelWidth - pad * 2.0f - dpi(app, 16.0f)) / 2.0f;
    float colX[2] = {panelX + pad, panelX + pad + cardW + dpi(app, 16.0f)};
    if (app.themeHeaderFormat) {
        app.themeHeaderFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F headerColor = ink;
        headerColor.a = 0.5f * anim;
        app.brush->SetColor(headerColor);
        const wchar_t* lightHdr = tr(app, "theme.chooser.light");
        app.renderTarget->DrawText(
            lightHdr, (UINT32)wcslen(lightHdr), app.themeHeaderFormat,
            D2D1::RectF(colX[0] + dpi(app, 2.0f), gridY,
                        colX[0] + cardW, gridY + labelH),
            app.brush);
        const wchar_t* darkHdr = tr(app, "theme.chooser.dark");
        app.renderTarget->DrawText(
            darkHdr, (UINT32)wcslen(darkHdr), app.themeHeaderFormat,
            D2D1::RectF(colX[1] + dpi(app, 2.0f), gridY,
                        colX[1] + cardW, gridY + labelH),
            app.brush);
    }

    app.hoveredThemeIndex = -1;
    float cardsTop = gridY + labelH;

    for (int i = 0; i < themeCount(); i++) {
        const D2DTheme& t = themeAt(i);
        int col, row;
        themeChooserCell(i, col, row);
        float x = colX[col];
        float y = cardsTop + row * (cardH + gap);
        D2D1_RECT_F card = D2D1::RectF(x, y, x + cardW, y + cardH);
        D2D1_ROUNDED_RECT cardRect =
            D2D1::RoundedRect(card, dpi(app, 10.0f), dpi(app, 10.0f));

        bool isHovered = (app.mouseX >= card.left && app.mouseX <= card.right &&
                          app.mouseY >= card.top && app.mouseY <= card.bottom);
        // The ring and check stay on the COMMITTED theme while a hover
        // preview repaints the app behind (design 12b)
        bool isSelected = (i == (app.themeChooserPreviewBase >= 0
                                     ? app.themeChooserPreviewBase
                                     : app.currentThemeIndex));
        if (isHovered) app.hoveredThemeIndex = i;

        // Card = the theme's own document surface
        D2D1_COLOR_F bgColor = t.background;
        bgColor.a = anim;
        app.brush->SetColor(bgColor);
        app.renderTarget->FillRoundedRectangle(cardRect, app.brush);

        // Ring: accent for the active card, soft ink otherwise
        if (isSelected) {
            D2D1_COLOR_F glow = t.accent;
            glow.a = 0.35f * anim;
            app.brush->SetColor(glow);
            app.renderTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(card.left - dpi(app, 3.0f),
                                card.top - dpi(app, 3.0f),
                                card.right + dpi(app, 3.0f),
                                card.bottom + dpi(app, 3.0f)),
                    dpi(app, 12.0f), dpi(app, 12.0f)),
                app.brush, dpi(app, 2.0f));
            D2D1_COLOR_F ring = t.accent;
            ring.a = anim;
            app.brush->SetColor(ring);
            app.renderTarget->DrawRoundedRectangle(cardRect, app.brush,
                                                   dpi(app, 1.5f));
        } else {
            D2D1_COLOR_F border = ink;
            border.a = (isHovered ? 0.4f : 0.16f) * anim;
            app.brush->SetColor(border);
            app.renderTarget->DrawRoundedRectangle(cardRect, app.brush, 1.0f);
        }

        float cx = x + dpi(app, 12.0f);
        float rowY = y + dpi(app, 7.0f);
        IDWriteTextFormat* nameFormat =
            (i < (int)app.themePreviewFormats.size())
                ? app.themePreviewFormats[i].name
                : nullptr;
        IDWriteTextFormat* previewFormat =
            (i < (int)app.themePreviewFormats.size())
                ? app.themePreviewFormats[i].preview
                : nullptr;
        IDWriteTextFormat* codeFormat =
            (i < (int)app.themePreviewFormats.size())
                ? app.themePreviewFormats[i].code
                : nullptr;

        // Row 1: name in the theme's heading voice + accent dash (+ pin)
        float nameW = 0.0f;
        if (nameFormat) {
            nameFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            D2D1_COLOR_F nameColor = t.heading;
            nameColor.a = anim;
            app.brush->SetColor(nameColor);
            app.renderTarget->DrawText(
                t.name, (UINT32)wcslen(t.name), nameFormat,
                D2D1::RectF(cx, rowY, x + cardW - dpi(app, 30.0f),
                            rowY + dpi(app, 20.0f)),
                app.brush);
            nameW = measureText(app, t.name, nameFormat);
        }
        {
            D2D1_COLOR_F dash = t.accent;
            dash.a = anim;
            app.brush->SetColor(dash);
            float dx = cx + nameW + dpi(app, 8.0f);
            float dy = rowY + dpi(app, 8.0f);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(dx, dy, dx + dpi(app, 22.0f),
                                              dy + dpi(app, 3.0f)),
                                  dpi(app, 1.5f), dpi(app, 1.5f)),
                app.brush);
            // Default pin while follow-system is on: sun / moon on the
            // card the OS switch would pick for its side
            bool pinned = app.followSystemTheme &&
                          ((t.isDark && i == app.darkThemeIndex) ||
                           (!t.isDark && i == app.lightThemeIndex));
            if (pinned && app.tocFormat) {
                float px = dx + dpi(app, 30.0f);
                D2D1_RECT_F pin = D2D1::RectF(px, rowY + dpi(app, 1.0f),
                                              px + dpi(app, 22.0f),
                                              rowY + dpi(app, 17.0f));
                D2D1_COLOR_F pc = t.accent;
                pc.a = anim;
                app.brush->SetColor(pc);
                app.renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(pin, dpi(app, 8.0f), dpi(app, 8.0f)),
                    app.brush, 1.0f);
                app.tocFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                app.renderTarget->DrawText(
                    t.isDark ? L"\u263E" : L"\u2600", 1, app.tocFormat,
                    D2D1::RectF(pin.left, pin.top - dpi(app, 1.0f), pin.right,
                                pin.bottom),
                    app.brush);
                app.tocFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
        }

        // Row 2: body voice
        if (previewFormat && cardH > dpi(app, 52.0f)) {
            previewFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            previewFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            D2D1_COLOR_F textColor = t.text;
            textColor.a = anim;
            app.brush->SetColor(textColor);
            app.renderTarget->DrawText(
                L"The quick brown fox jumps over the lazy dog", 43,
                previewFormat,
                D2D1::RectF(cx, y + dpi(app, 27.0f), x + cardW - dpi(app, 10.0f),
                            y + dpi(app, 43.0f)),
                app.brush);
        }

        // Row 3: link, code chip, blockquote bar, syntax dots
        float by = y + cardH - dpi(app, 24.0f);
        if (previewFormat) {
            D2D1_COLOR_F linkColor = t.link;
            linkColor.a = anim;
            app.brush->SetColor(linkColor);
            app.renderTarget->DrawText(
                L"hyperlink", 9, previewFormat,
                D2D1::RectF(cx, by, cx + dpi(app, 70.0f), by + dpi(app, 16.0f)),
                app.brush);
            float lw = measureText(app, L"hyperlink", previewFormat);
            app.renderTarget->DrawLine(
                D2D1::Point2F(cx, by + dpi(app, 14.0f)),
                D2D1::Point2F(cx + lw, by + dpi(app, 14.0f)), app.brush, 1.0f);

            float chipX = cx + lw + dpi(app, 10.0f);
            D2D1_COLOR_F codeBg = t.codeBackground;
            codeBg.a = anim;
            app.brush->SetColor(codeBg);
            D2D1_RECT_F chip = D2D1::RectF(chipX, by - dpi(app, 1.0f),
                                           chipX + dpi(app, 52.0f),
                                           by + dpi(app, 15.0f));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(chip, dpi(app, 4.0f), dpi(app, 4.0f)),
                app.brush);
            if (codeFormat) {
                codeFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                D2D1_COLOR_F codeColor = t.code;
                codeColor.a = anim;
                app.brush->SetColor(codeColor);
                app.renderTarget->DrawText(L"code()", 6, codeFormat,
                                           D2D1::RectF(chip.left, chip.top + dpi(app, 1.0f),
                                                       chip.right, chip.bottom),
                                           app.brush);
            }

            D2D1_COLOR_F bq = t.blockquoteBorder;
            bq.a = anim;
            app.brush->SetColor(bq);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(chip.right + dpi(app, 10.0f), by,
                                chip.right + dpi(app, 13.0f),
                                by + dpi(app, 13.0f)),
                    dpi(app, 1.5f), dpi(app, 1.5f)),
                app.brush);
        }
        {
            // Seven syntax dots, right-aligned: the palette at a glance
            const D2D1_COLOR_F* dots[7] = {
                &t.syntaxKeyword, &t.syntaxString,  &t.syntaxComment,
                &t.syntaxNumber,  &t.syntaxFunction, &t.syntaxType,
                &t.syntaxControlFlow};
            float dotR = dpi(app, 3.5f);
            float step = dpi(app, 10.0f);
            float dxr = x + cardW - dpi(app, 13.0f);
            float dy = by + dpi(app, 7.0f);
            for (int d = 6; d >= 0; d--) {
                D2D1_COLOR_F c = *dots[d];
                c.a = anim;
                app.brush->SetColor(c);
                app.renderTarget->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(dxr - (6 - d) * step, dy),
                                  dotR, dotR),
                    app.brush);
            }
        }

        // Active check bead, top-right
        if (isSelected) {
            float bx2 = card.right - dpi(app, 18.0f);
            float by2 = card.top + dpi(app, 18.0f);
            D2D1_COLOR_F bead = t.accent;
            bead.a = anim;
            app.brush->SetColor(bead);
            app.renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(bx2, by2), dpi(app, 9.0f),
                              dpi(app, 9.0f)),
                app.brush);
            float lum = 0.299f * t.accent.r + 0.587f * t.accent.g +
                        0.114f * t.accent.b;
            app.brush->SetColor(lum > 0.45f
                                    ? D2D1::ColorF(0.05f, 0.07f, 0.1f, anim)
                                    : D2D1::ColorF(1, 1, 1, anim));
            app.renderTarget->DrawLine(
                D2D1::Point2F(bx2 - dpi(app, 4.0f), by2),
                D2D1::Point2F(bx2 - dpi(app, 1.0f), by2 + dpi(app, 3.0f)),
                app.brush, dpi(app, 1.8f));
            app.renderTarget->DrawLine(
                D2D1::Point2F(bx2 - dpi(app, 1.0f), by2 + dpi(app, 3.0f)),
                D2D1::Point2F(bx2 + dpi(app, 4.0f), by2 - dpi(app, 3.0f)),
                app.brush, dpi(app, 1.8f));
        }

        app.themeChooserHits.push_back({card, i});
    }

    // Footer: edit-theme entry (left), follow-system explainer (right)
    if (app.tocFormat) {
        float fy = panelY + panelHeight - pad - dpi(app, 18.0f);
        app.tocFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        std::wstring edit = std::wstring(L"\u270E ") + tr(app, "settings.edit");
        edit += L" - themes.ini";
        D2D1_COLOR_F ec = ink;
        ec.a = 0.55f * anim;
        app.brush->SetColor(ec);
        app.renderTarget->DrawText(
            edit.c_str(), (UINT32)edit.size(), app.tocFormat,
            D2D1::RectF(panelX + pad, fy, panelX + panelWidth * 0.6f,
                        fy + dpi(app, 18.0f)),
            app.brush);
        float ew = measureText(app, edit.c_str(), app.tocFormat);
        app.themeChooserHits.push_back(
            {D2D1::RectF(panelX + pad - dpi(app, 4.0f), fy - dpi(app, 4.0f),
                         panelX + pad + ew + dpi(app, 8.0f),
                         fy + dpi(app, 20.0f)),
             -3});
    }
}

void renderHelpOverlay(App& app) {
    // Animate in
    if (app.helpAnimation < 1.0f) {
        float prev = app.helpAnimation;
        app.helpAnimation = std::min(1.0f, app.helpAnimation + 0.15f);
        if (app.helpAnimation != prev)
            InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.helpAnimation;

    // Keep the document readable behind the shortcut sheet. The panel and
    // all of its ink use the active theme below; the mask only separates it.
    float backdropAlpha = app.theme.isDark ? 0.34f : 0.24f;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha * anim));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    // Panel dimensions - fit to window
    float panelWidth = std::min(dpi(app, 520.0f), app.width - dpi(app, 40.0f));
    float panelHeight = std::min(dpi(app, 700.0f), app.height - dpi(app, 40.0f));
    float panelX = (app.width - panelWidth) / 2;
    float panelY = (app.height - panelHeight) / 2 + (1 - anim) * dpi(app, 50.0f);

    // Panel background follows the active document theme.
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(
        D2D1::RectF(panelX, panelY, panelX + panelWidth, panelY + panelHeight),
        dpi(app, 16.0f), dpi(app, 16.0f));
    D2D1_COLOR_F panelColor = app.theme.background;
    panelColor.a = 0.98f * anim;
    app.brush->SetColor(panelColor);
    app.renderTarget->FillRoundedRectangle(panelRect, app.brush);

    // Border
    D2D1_COLOR_F panelBorder = app.theme.text;
    panelBorder.a = 0.35f * anim;
    app.brush->SetColor(panelBorder);
    app.renderTarget->DrawRoundedRectangle(panelRect, app.brush, 1.0f);

    // Title (fixed, not scrolled)
    float titleBottomY = panelY + dpi(app, 55.0f);
    IDWriteTextFormat* titleFormat = app.themeTitleFormat;
    if (titleFormat) {
        titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_COLOR_F titleColor = app.theme.heading;
        titleColor.a = anim;
        app.brush->SetColor(titleColor);
        const wchar_t* helpTitle = tr(app, "help.title");
        app.renderTarget->DrawText(helpTitle, (UINT32)wcslen(helpTitle), titleFormat,
            D2D1::RectF(panelX, panelY + dpi(app, 15.0f), panelX + panelWidth, titleBottomY), app.brush);
    }

    // Version badge in the panel's top-right corner (from CMake's
    // TINTA_VERSION_* defines, the single source of truth)
    if (app.tocFormat) {
        wchar_t version[32];
        swprintf_s(version, _countof(version), L"v%d.%d.%d",
                   TINTA_VERSION_MAJOR, TINTA_VERSION_MINOR,
                   TINTA_VERSION_PATCH);
        IDWriteTextLayout* verLayout = nullptr;
        app.dwriteFactory->CreateTextLayout(
            version, (UINT32)wcslen(version), app.tocFormat,
            panelWidth - dpi(app, 22.0f), dpi(app, 20.0f), &verLayout);
        if (verLayout) {
            // Shared formats may carry another overlay's alignment; pin it
            verLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            D2D1_COLOR_F verColor = app.theme.text;
            verColor.a = 0.45f * anim;
            app.brush->SetColor(verColor);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(panelX, panelY + dpi(app, 18.0f)), verLayout,
                app.brush);
            verLayout->Release();
        }
    }

    IDWriteTextFormat* boldFmt = app.tocFormatBold;
    IDWriteTextFormat* normalFmt = app.tocFormat;
    if (!boldFmt || !normalFmt) return;

    // Shortcut entries
    struct HelpEntry {
        const wchar_t* key;
        const wchar_t* desc;
    };

    // Remappable keys show their current binding ([Keys] in settings.ini)
    std::wstring kDown = keyLabel(app.keymap[KA_SCROLLDOWN]) + L" / \x2193";
    std::wstring kUp = keyLabel(app.keymap[KA_SCROLLUP]) + L" / \x2191";
    std::wstring kSearch = keyLabel(app.keymap[KA_SEARCH]) + L" / Ctrl+F";
    std::wstring kBrowse = keyLabel(app.keymap[KA_BROWSE]);
    std::wstring kToc = keyLabel(app.keymap[KA_TOC]);
    std::wstring kTheme = keyLabel(app.keymap[KA_THEME]);
    std::wstring kStats = keyLabel(app.keymap[KA_STATS]);
    std::wstring kHelp = keyLabel(app.keymap[KA_HELP]);
    std::wstring kEdit = keyLabel(app.keymap[KA_EDIT]);
    std::wstring kQuit = keyLabel(app.keymap[KA_QUIT]);

    const HelpEntry navEntries[] = {
        {kDown.c_str(),   tr(app, "help.nav.scroll_down")},
        {kUp.c_str(),     tr(app, "help.nav.scroll_up")},
        {L"Space / PgDn", tr(app, "help.nav.page_down")},
        {L"PgUp",         tr(app, "help.nav.page_up")},
        {L"Home / End",   tr(app, "help.nav.jump_start_end")},
        {L"Ctrl+Scroll",  tr(app, "help.nav.zoom")},
    };

    const HelpEntry overlayEntries[] = {
        {kSearch.c_str(), tr(app, "help.view.search")},
        {L"Enter",        tr(app, "help.view.next_match")},
        {kBrowse.c_str(), tr(app, "help.view.folder_browser")},
        {kToc.c_str(),    tr(app, "help.view.toc")},
        {L"Ctrl+T",       tr(app, "help.view.new_tab")},
        {L"Ctrl+Tab",     tr(app, "help.view.next_tab")},
        {L"Ctrl+W",       tr(app, "help.view.close_tab")},
        {kTheme.c_str(),  tr(app, "help.view.theme")},
        {kStats.c_str(),  tr(app, "help.view.stats")},
        {kHelp.c_str(),   tr(app, "help.view.help")},
    };

    const HelpEntry editEntries[] = {
        {kEdit.c_str(),   tr(app, "help.edit.enter_edit")},
        {L"Ctrl+S",       tr(app, "help.edit.save")},
        {L"Ctrl+E",       tr(app, "help.edit.preview")},
        {L"Ctrl+W",       tr(app, "help.edit.word_wrap")},
        {L"ESC ESC",      tr(app, "help.edit.exit_edit")},
    };

    const HelpEntry generalEntries[] = {
        {L"Ctrl+A",       tr(app, "help.general.select_all")},
        {L"Ctrl+C",       tr(app, "help.general.copy")},
        {L"Ctrl+N",       tr(app, "help.general.new_note")},
        {L"Ctrl+Shift+N", tr(app, "help.general.new_window")},
        {L"Ctrl+P",       tr(app, "ctx.print")},
        {L"ESC",          tr(app, "help.general.close")},
        {kQuit.c_str(),   tr(app, "help.general.quit")},
    };

    float padding = dpi(app, 20.0f);
    float keyColWidth = dpi(app, 150.0f);
    float leftX = panelX + padding;
    float descX = leftX + keyColWidth;
    float rightEdge = panelX + panelWidth - padding;
    float lineH = dpi(app, 22.0f);
    float sectionGap = dpi(app, 10.0f);
    float sectionHeaderExtra = dpi(app, 4.0f);

    // Calculate total content height
    auto sectionHeight = [&](int entryCount) {
        return lineH + sectionHeaderExtra + entryCount * lineH + sectionGap;
    };
    float footerH = dpi(app, 35.0f);
    float totalContentHeight = sectionHeight((int)_countof(navEntries)) +
                               sectionHeight((int)_countof(overlayEntries)) +
                               sectionHeight((int)_countof(editEntries)) +
                               sectionHeight((int)_countof(generalEntries)) + footerH;

    // Scrollable area
    float contentTopY = titleBottomY + dpi(app, 10.0f);
    float contentBottomY = panelY + panelHeight;
    float visibleHeight = contentBottomY - contentTopY;

    // Store dimensions for input handling (scrollbar drag)
    app.helpContentHeight = totalContentHeight;
    app.helpVisibleHeight = visibleHeight;
    app.helpScrollbarTop = contentTopY;

    // Clamp scroll
    float maxScroll = std::max(0.0f, totalContentHeight - visibleHeight);
    app.helpScroll = std::max(0.0f, std::min(app.helpScroll, maxScroll));

    // Push clip so content doesn't draw outside panel
    app.renderTarget->PushAxisAlignedClip(
        D2D1::RectF(panelX, contentTopY, panelX + panelWidth, contentBottomY),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float y = contentTopY - app.helpScroll;

    auto drawSection = [&](const wchar_t* title, const HelpEntry* entries, int count) {
        // Section header
        boldFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F headerColor = app.theme.accent;
        headerColor.a = anim;
        app.brush->SetColor(headerColor);
        app.renderTarget->DrawText(title, (UINT32)wcslen(title), boldFmt,
            D2D1::RectF(leftX, y, rightEdge, y + lineH), app.brush);
        y += lineH + sectionHeaderExtra;

        for (int i = 0; i < count; i++) {
            // Key
            boldFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            D2D1_COLOR_F keyColor = app.theme.text;
            keyColor.a = 0.9f * anim;
            app.brush->SetColor(keyColor);
            app.renderTarget->DrawText(entries[i].key, (UINT32)wcslen(entries[i].key), boldFmt,
                D2D1::RectF(leftX + dpi(app, 8.0f), y, leftX + keyColWidth, y + lineH), app.brush);

            // Description
            normalFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            D2D1_COLOR_F descriptionColor = app.theme.text;
            descriptionColor.a = 0.7f * anim;
            app.brush->SetColor(descriptionColor);
            app.renderTarget->DrawText(entries[i].desc, (UINT32)wcslen(entries[i].desc), normalFmt,
                D2D1::RectF(descX, y, rightEdge, y + lineH), app.brush);

            y += lineH;
        }
        y += sectionGap;
    };

    drawSection(tr(app, "help.section.navigation"), navEntries, (int)_countof(navEntries));
    drawSection(tr(app, "help.section.view"), overlayEntries, (int)_countof(overlayEntries));
    drawSection(tr(app, "help.section.editing"), editEntries, (int)_countof(editEntries));
    drawSection(tr(app, "help.section.general"), generalEntries, (int)_countof(generalEntries));

    // Footer hint
    normalFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    D2D1_COLOR_F footerColor = app.theme.text;
    footerColor.a = 0.55f * anim;
    app.brush->SetColor(footerColor);
    const wchar_t* helpFooter = tr(app, "help.footer");
    app.renderTarget->DrawText(helpFooter, (UINT32)wcslen(helpFooter), normalFmt,
        D2D1::RectF(panelX, y, panelX + panelWidth, y + lineH), app.brush);

    app.renderTarget->PopAxisAlignedClip();

    // Scrollbar if content overflows
    if (maxScroll > 0) {
        float sbHeight = visibleHeight / totalContentHeight * visibleHeight;
        sbHeight = std::max(sbHeight, dpi(app, 20.0f));
        float sbY = contentTopY + (app.helpScroll / maxScroll * (visibleHeight - sbHeight));

        D2D1_COLOR_F sbColor = app.theme.text;
        sbColor.a = 0.3f * anim;
        app.brush->SetColor(sbColor);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(panelX + panelWidth - dpi(app, 12.0f), sbY,
                                          panelX + panelWidth - dpi(app, 8.0f), sbY + sbHeight), 2, 2),
            app.brush);
    }
}

// --- Right-click context menu ---
//
// Drawn in the current theme like the folder browser and theme chooser.
// Every entry shows its keyboard shortcut, so the menu doubles as
// discoverable documentation for the single-key commands.

namespace {

struct ContextMenuEntry {
    const wchar_t* label;
    const wchar_t* shortcut;
    bool separatorAfter;
};

const ContextMenuEntry CTX_ENTRIES[CTX_ITEM_COUNT] = {
    { L"Copy",               L"Ctrl+C", false },
    { L"Select All",         L"Ctrl+A", false },
    { L"Annotate",           L"",       true  },
    { L"New File",           L"N",      false },
    { L"Print / PDF",        L"Ctrl+P", false },
    { L"Export as...",       L"",       false },
    { L"Edit",               L":",      false },
    { L"Search",             L"F",      false },
    { L"Table of Contents",  L"Tab",    true  },
    { L"Browse Files",       L"B",      false },
    { L"Reveal in Explorer", L"",       true  },
    { L"Theme",              L"T",      false },
    { L"Settings",           L"Ctrl+,", false },
    { L"Help",               L"?",      false },
};

float ctxItemHeight(const App& app) { return dpi(app, 30.0f); }
float ctxSeparatorHeight(const App& app) { return dpi(app, 9.0f); }
float ctxWidth(const App& app) { return dpi(app, 210.0f); }
float ctxPadding(const App& app) { return dpi(app, 6.0f); }

float ctxTotalHeight(const App& app) {
    float h = ctxPadding(app) * 2;
    for (const auto& entry : CTX_ENTRIES) {
        h += ctxItemHeight(app);
        if (entry.separatorAfter) h += ctxSeparatorHeight(app);
    }
    return h;
}

// Top y of the item at `index` relative to the menu top
float ctxItemTop(const App& app, int index) {
    float y = ctxPadding(app);
    for (int i = 0; i < index; i++) {
        y += ctxItemHeight(app);
        if (CTX_ENTRIES[i].separatorAfter) y += ctxSeparatorHeight(app);
    }
    return y;
}

} // namespace

bool contextMenuItemEnabled(const App& app, int item) {
    switch (item) {
        case CTX_COPY:   return app.hasSelection && app.selAnchor != app.selFocus;
        case CTX_ANNOTATE:
            // Viewer-only: annotating writes a comment into the file (#126)
            return !app.editMode && !app.currentFile.empty() &&
                   app.hasSelection && app.selAnchor != app.selFocus;
        case CTX_REVEAL: return !app.currentFile.empty();
        default:         return item >= 0 && item < CTX_ITEM_COUNT;
    }
}

int contextMenuItemAt(const App& app, float x, float y) {
    if (x < app.contextMenuX || x > app.contextMenuX + ctxWidth(app)) return -1;
    float rel = y - app.contextMenuY;
    for (int i = 0; i < CTX_ITEM_COUNT; i++) {
        float top = ctxItemTop(app, i);
        if (rel >= top && rel < top + ctxItemHeight(app)) return i;
    }
    return -1;
}

void openContextMenu(App& app, float x, float y) {
    float w = ctxWidth(app);
    float h = ctxTotalHeight(app);
    app.contextMenuX = std::max(0.0f, std::min(x, (float)app.width - w));
    app.contextMenuY = std::max(0.0f, std::min(y, (float)app.height - h));
    app.showContextMenu = true;
    app.hoveredContextMenuItem = -1;
    app.contextMenuAnimation = 0.0f;
    app.hoveredCodeBlock = -1;
    app.hoveredLink.clear();
}

void renderContextMenu(App& app) {
    if (app.contextMenuAnimation < 1.0f) {
        float prev = app.contextMenuAnimation;
        app.contextMenuAnimation = std::min(1.0f, app.contextMenuAnimation + 0.25f);
        if (app.contextMenuAnimation != prev)
            InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.contextMenuAnimation;

    IDWriteTextFormat* format = app.folderBrowserFormat;
    if (!format) return;

    float x = app.contextMenuX;
    float y = app.contextMenuY;
    float w = ctxWidth(app);
    float h = ctxTotalHeight(app);

    D2D1_COLOR_F panelBg = app.theme.isDark ? hexColor(0x1E1E1E, 0.97f)
                                            : hexColor(0xF8F8F8, 0.97f);
    D2D1_COLOR_F borderColor = app.theme.isDark ? hexColor(0x3A3A40, 0.9f)
                                                : hexColor(0xC8C8C8, 0.9f);
    app.brush->SetColor(panelBg);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 6, 6), app.brush);
    app.brush->SetColor(borderColor);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 6, 6), app.brush, 1.0f);

    app.hoveredContextMenuItem = contextMenuItemAt(app, app.mouseX, app.mouseY);

    // Labels come from the translation table (order matches ContextMenuItem)
    static const char* kCtxKeys[CTX_ITEM_COUNT] = {
        "ctx.copy", "ctx.select_all", "ctx.annotate", "ctx.new", "ctx.print",
        "ctx.export", "ctx.edit", "ctx.search", "ctx.toc", "ctx.browse",
        "ctx.reveal", "ctx.theme", "ctx.settings", "ctx.help",
    };

    // Shortcut hints reflect the user keymap ([Keys] in settings.ini)
    auto shortcutLabel = [&](int item) -> std::wstring {
        switch (item) {
            case CTX_ANNOTATE: return keyLabel(app.keymap[KA_ANNOTATE]);
            case CTX_NEW:    return keyLabel(app.keymap[KA_NEWFILE]);
            case CTX_EDIT:   return keyLabel(app.keymap[KA_EDIT]);
            case CTX_SEARCH: return keyLabel(app.keymap[KA_SEARCH]);
            case CTX_TOC:    return keyLabel(app.keymap[KA_TOC]);
            case CTX_BROWSE: return keyLabel(app.keymap[KA_BROWSE]);
            case CTX_THEME:  return keyLabel(app.keymap[KA_THEME]);
            case CTX_HELP:   return keyLabel(app.keymap[KA_HELP]);
            default:         return CTX_ENTRIES[item].shortcut;
        }
    };

    float pad = ctxPadding(app);
    float itemH = ctxItemHeight(app);
    float inset = dpi(app, 12.0f);
    for (int i = 0; i < CTX_ITEM_COUNT; i++) {
        float top = y + ctxItemTop(app, i);
        bool enabled = contextMenuItemEnabled(app, i);

        if (i == app.hoveredContextMenuItem && enabled) {
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
        const wchar_t* itemLabel = tr(app, kCtxKeys[i]);
        app.renderTarget->DrawText(
            itemLabel, (UINT32)wcslen(itemLabel), format,
            D2D1::RectF(x + inset, textY, x + w - inset, top + itemH), app.brush);

        std::wstring hintText = shortcutLabel(i);
        if (!hintText.empty()) {
            IDWriteTextLayout* hint = nullptr;
            app.dwriteFactory->CreateTextLayout(
                hintText.c_str(), (UINT32)hintText.size(),
                format, 1000.0f, itemH, &hint);
            if (hint) {
                DWRITE_TEXT_METRICS metrics{};
                hint->GetMetrics(&metrics);
                D2D1_COLOR_F hintColor = app.theme.text;
                hintColor.a = 0.4f * anim;
                app.brush->SetColor(hintColor);
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(x + w - inset - metrics.width, textY),
                    hint, app.brush);
                hint->Release();
            }
        }

        if (CTX_ENTRIES[i].separatorAfter) {
            float sepY = top + itemH + ctxSeparatorHeight(app) * 0.5f;
            app.brush->SetColor(borderColor);
            app.renderTarget->DrawLine(
                D2D1::Point2F(x + pad, sepY), D2D1::Point2F(x + w - pad, sepY),
                app.brush, 1.0f);
        }
    }
}

// --- Folder-wide search results ---
//
// Sibling markdown files matching the search query, shown as a panel under
// the right end of the search bar: filename, a few highlighted snippet
// lines each, and a +N counter. Clicking a file opens it at the match.

namespace {

// The toggle button hangs off the right edge of the search bar
void folderToggleGeometry(const App& app, float& btnX, float& btnY, float& btnSize) {
    float barWidth = std::min(dpi(app, 500.0f), app.width - dpi(app, 40.0f));
    float barHeight = dpi(app, 44.0f);
    float barX = ((float)app.width - barWidth) / 2;
    float barY = dpi(app, 20.0f);
    btnSize = dpi(app, 30.0f);
    btnX = barX + barWidth + dpi(app, 8.0f);
    btnY = barY + (barHeight - btnSize) / 2;
}

} // namespace

bool folderSearchToggleAt(const App& app, float x, float y) {
    if (!app.showSearch || app.editMode) return false;
    float btnX, btnY, btnSize;
    folderToggleGeometry(app, btnX, btnY, btnSize);
    return x >= btnX && x <= btnX + btnSize && y >= btnY && y <= btnY + btnSize;
}

void renderFolderSearchResults(App& app) {
    float anim = app.searchAnimation;
    IDWriteTextFormat* fmt = app.folderBrowserFormat;

    // Toggle button (always drawn with the bar so the feature is discoverable)
    {
        float btnX, btnY, btnSize;
        folderToggleGeometry(app, btnX, btnY, btnSize);
        D2D1_COLOR_F btnBg = app.theme.isDark ? hexColor(0x1E1E22, 0.95f * anim)
                                              : hexColor(0xFFFFFF, 0.95f * anim);
        app.brush->SetColor(btnBg);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(btnX, btnY, btnX + btnSize, btnY + btnSize),
                              dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
        D2D1_COLOR_F folderColor = app.folderSearchEnabled
            ? app.theme.accent
            : (app.theme.isDark ? hexColor(0x5A5A62) : hexColor(0xB0B0B6));
        folderColor.a = anim;
        app.brush->SetColor(folderColor);
        float gx = btnX + dpi(app, 7.0f);
        float gy = btnY + dpi(app, 9.0f);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(gx, gy + dpi(app, 3.0f), gx + dpi(app, 16.0f), gy + dpi(app, 13.0f)), 2, 2),
            app.brush);
        app.renderTarget->FillRectangle(
            D2D1::RectF(gx, gy, gx + dpi(app, 7.0f), gy + dpi(app, 4.0f)), app.brush);
        if (!app.folderSearchEnabled) {
            // Slash = disabled
            D2D1_COLOR_F slash = app.theme.isDark ? hexColor(0xF85149, anim) : hexColor(0xCF222E, anim);
            app.brush->SetColor(slash);
            app.renderTarget->DrawLine(
                D2D1::Point2F(btnX + dpi(app, 6.0f), btnY + btnSize - dpi(app, 6.0f)),
                D2D1::Point2F(btnX + btnSize - dpi(app, 6.0f), btnY + dpi(app, 6.0f)),
                app.brush, 2.0f);
        }
    }

    app.folderResultHits.clear();
    if (!app.folderSearchEnabled || app.folderResults.empty() ||
        app.searchQuery.empty() || !fmt) {
        return;
    }

    float panelW = std::min(dpi(app, 460.0f), app.width * 0.35f);
    float panelX = app.width - panelW - dpi(app, 16.0f);
    float panelY = dpi(app, 20.0f) + dpi(app, 44.0f) + dpi(app, 12.0f);
    float pad = dpi(app, 12.0f);
    float headerH = dpi(app, 24.0f);
    float lineH = dpi(app, 20.0f);
    float moreH = dpi(app, 16.0f);
    float sectionGap = dpi(app, 10.0f);
    float maxBottom = app.height - dpi(app, 20.0f);

    // Height first, so the panel can be drawn before its content
    float contentH = pad;
    size_t shownFiles = 0;
    for (const auto& file : app.folderResults) {
        float sectionH = headerH + file.matches.size() * lineH +
            ((size_t)file.totalMatches > file.matches.size() ? moreH : 0.0f) + sectionGap;
        if (panelY + contentH + sectionH + pad > maxBottom) break;
        contentH += sectionH;
        shownFiles++;
    }
    if (shownFiles == 0) return;
    contentH += pad - sectionGap;

    D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(
        D2D1::RectF(panelX, panelY, panelX + panelW, panelY + contentH),
        dpi(app, 8.0f), dpi(app, 8.0f));
    D2D1_COLOR_F panelBg = app.theme.isDark ? hexColor(0x18181C, 0.96f * anim)
                                            : hexColor(0xFCFCFC, 0.97f * anim);
    app.brush->SetColor(panelBg);
    app.renderTarget->FillRoundedRectangle(panel, app.brush);
    D2D1_COLOR_F borderColor = app.theme.isDark ? hexColor(0x3A3A40, 0.8f * anim)
                                                : hexColor(0xC8C8C8, 0.8f * anim);
    app.brush->SetColor(borderColor);
    app.renderTarget->DrawRoundedRectangle(panel, app.brush, 1.0f);

    float rowY = panelY + pad;
    for (size_t i = 0; i < shownFiles; i++) {
        const auto& file = app.folderResults[i];
        float sectionTop = rowY;

        D2D1_COLOR_F nameColor = app.theme.accent;
        nameColor.a = anim;
        app.brush->SetColor(nameColor);
        app.renderTarget->DrawText(
            file.fileName.c_str(), (UINT32)file.fileName.length(), fmt,
            D2D1::RectF(panelX + pad, rowY, panelX + panelW - pad, rowY + headerH),
            app.brush);
        rowY += headerH;

        for (const auto& match : file.matches) {
            // Highlight behind the matched substring
            std::wstring prefix = match.snippet.substr(0, match.matchStart);
            std::wstring matched = match.snippet.substr(match.matchStart, match.matchLen);
            float prefixW = prefix.empty() ? 0.0f : measureText(app, prefix, fmt);
            float matchW = matched.empty() ? 0.0f : measureText(app, matched, fmt);
            float textX = panelX + pad;
            float availW = panelW - pad * 2;
            if (prefixW + matchW > availW) {
                // Keep the match visible: drop the head of the prefix
                while (!prefix.empty() && prefixW + matchW > availW * 0.7f) {
                    prefix.erase(0, 8);
                    prefixW = prefix.empty() ? 0.0f : measureText(app, prefix, fmt);
                }
            }
            D2D1_COLOR_F hl = app.theme.accent;
            hl.a = 0.28f * anim;
            app.brush->SetColor(hl);
            app.renderTarget->FillRectangle(
                D2D1::RectF(textX + prefixW, rowY + dpi(app, 1.0f),
                            textX + prefixW + matchW, rowY + lineH - dpi(app, 1.0f)),
                app.brush);

            std::wstring shown = prefix + match.snippet.substr(match.matchStart);
            D2D1_COLOR_F snippetColor = app.theme.text;
            snippetColor.a = 0.85f * anim;
            app.brush->SetColor(snippetColor);
            app.renderTarget->DrawText(
                shown.c_str(), (UINT32)shown.length(), fmt,
                D2D1::RectF(textX, rowY + dpi(app, 1.0f), textX + availW, rowY + lineH),
                app.brush);
            rowY += lineH;
        }

        if ((size_t)file.totalMatches > file.matches.size()) {
            wchar_t more[32];
            swprintf_s(more, L"+%d more", file.totalMatches - (int)file.matches.size());
            D2D1_COLOR_F moreColor = app.theme.text;
            moreColor.a = 0.45f * anim;
            app.brush->SetColor(moreColor);
            app.renderTarget->DrawText(more, (UINT32)wcslen(more), fmt,
                D2D1::RectF(panelX + pad, rowY, panelX + panelW - pad, rowY + moreH),
                app.brush);
            rowY += moreH;
        }

        app.folderResultHits.push_back({
            D2D1::RectF(panelX, sectionTop, panelX + panelW, rowY), (int)i});

        if (i + 1 < shownFiles) {
            app.brush->SetColor(borderColor);
            app.renderTarget->DrawLine(
                D2D1::Point2F(panelX + pad, rowY + sectionGap / 2),
                D2D1::Point2F(panelX + panelW - pad, rowY + sectionGap / 2),
                app.brush, 1.0f);
        }
        rowY += sectionGap;
    }
}


// --- Print preview ---
//
// Replaces the whole frame while open: the document itself is re-laid-out
// at page width in the print palette, so the normal render paths would draw
// nonsense underneath. Chrome is styled with the SAVED screen theme and
// scale - app.theme and app.contentScale hold the print palette and 1.0
// while the preview is active, so dpi(app, ...) cannot be used here.

void renderPrintPreview(App& app) {
    const auto& sv = app.printSaved;
    float w = (float)sv.width, h = (float)sv.height;
    float ui = sv.contentScale;
    const D2DTheme& th = sv.theme;

    app.brush->SetColor(th.background);
    app.renderTarget->FillRectangle(D2D1::RectF(0, 0, w, h), app.brush);

    float topBarH = 52.0f * ui;
    float bottomBarH = 64.0f * ui;

    // Current page, uploaded from the CPU rasterization. Preview repaints
    // are interaction-driven, so the per-paint upload cost is irrelevant.
    if (!app.printPreviewPixels.empty() && app.printPreviewPxW && app.printPreviewPxH) {
        ID2D1Bitmap* bmp = nullptr;
        app.renderTarget->CreateBitmap(
            D2D1::SizeU(app.printPreviewPxW, app.printPreviewPxH),
            app.printPreviewPixels.data(), app.printPreviewPxW * 4,
            D2D1::BitmapProperties(D2D1::PixelFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            &bmp);
        if (bmp) {
            float pw = (float)app.printPreviewPxW, ph = (float)app.printPreviewPxH;
            float px = (w - pw) / 2.0f;
            float py = topBarH + (h - topBarH - bottomBarH - ph) / 2.0f;
            for (int i = 3; i >= 1; i--) {  // soft shadow ring
                app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.06f * i));
                app.renderTarget->DrawRectangle(
                    D2D1::RectF(px - i, py - i, px + pw + i, py + ph + i),
                    app.brush, 1.5f);
            }
            app.renderTarget->DrawBitmap(bmp,
                D2D1::RectF(px, py, px + pw, py + ph),
                1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            bmp->Release();
        }
    }

    IDWriteTextFormat* uiFmt = nullptr;
    IDWriteTextFormat* btnFmt = nullptr;
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f * ui, L"en-us", &uiFmt);
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f * ui, L"en-us", &btnFmt);

    int pageCount = (int)app.printPreviewBounds.size() - 1;
    if (uiFmt) {
        uiFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        uiFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        wchar_t label[64];
         swprintf_s(label, tr(app, "print.title"),
                    app.printPreviewPage + 1, pageCount > 0 ? pageCount : 1);
        app.brush->SetColor(th.text);
        app.renderTarget->DrawText(label, (UINT32)wcslen(label), uiFmt,
            D2D1::RectF(0, 0, w, topBarH), app.brush);
    }

    // Format chips: paper sizes top-left, orientation top-right. The active
    // chip fills with the accent color.
    auto drawChip = [&](const D2D1_RECT_F& rect, const wchar_t* text, bool active) {
        float cr = 5.0f * ui;
        if (active) {
            app.brush->SetColor(th.accent);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(rect, cr, cr), app.brush);
        } else {
            D2D1_COLOR_F bc = th.text;
            bc.a = 0.35f;
            app.brush->SetColor(bc);
            app.renderTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(rect, cr, cr), app.brush, 1.0f);
        }
        if (uiFmt) {
            app.brush->SetColor(active ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : th.text);
            uiFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            app.renderTarget->DrawText(text, (UINT32)wcslen(text), uiFmt, rect, app.brush);
        }
    };

    float chipH = 26.0f * ui;
    float chipY = (topBarH - chipH) / 2.0f;
    float chipGap = 8.0f * ui;
    float paperW = 64.0f * ui;
    float px2 = 24.0f * ui;
    for (int i = 0; i < PRINT_PAPER_COUNT; i++) {
        D2D1_RECT_F rect = D2D1::RectF(px2, chipY, px2 + paperW, chipY + chipH);
        app.printPreviewPaperBtn[i] = rect;
        drawChip(rect, PRINT_PAPERS[i].name, i == app.printPreviewPaper);
        px2 += paperW + chipGap;
    }
    float orientW = 88.0f * ui;
    D2D1_RECT_F landR = D2D1::RectF(w - 24.0f * ui - orientW, chipY,
                                    w - 24.0f * ui, chipY + chipH);
    D2D1_RECT_F portR = D2D1::RectF(landR.left - chipGap - orientW, chipY,
                                    landR.left - chipGap, chipY + chipH);
    app.printPreviewOrientBtn[0] = portR;
    app.printPreviewOrientBtn[1] = landR;
    drawChip(portR, tr(app, "print.portrait"), !app.printPreviewLandscape);
    drawChip(landR, tr(app, "print.landscape"), app.printPreviewLandscape);

    // Buttons, bottom right
    float btnH = 34.0f * ui;
    float printW = 110.0f * ui, cancelW = 90.0f * ui;
    float gap = 12.0f * ui, margin = 24.0f * ui;
    float by = h - bottomBarH + (bottomBarH - btnH) / 2.0f;
    D2D1_RECT_F printR = D2D1::RectF(w - margin - printW, by, w - margin, by + btnH);
    D2D1_RECT_F cancelR = D2D1::RectF(printR.left - gap - cancelW, by,
                                      printR.left - gap, by + btnH);
    app.printPreviewPrintBtn = printR;
    app.printPreviewCancelBtn = cancelR;

    float r = 6.0f * ui;
    app.brush->SetColor(th.accent);
    app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(printR, r, r), app.brush);
    D2D1_COLOR_F borderC = th.text;
    borderC.a = 0.4f;
    app.brush->SetColor(borderC);
    app.renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(cancelR, r, r), app.brush, 1.0f);

    if (btnFmt) {
        btnFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        btnFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        const wchar_t* printLabel = tr(app, "print.print");
        app.renderTarget->DrawText(printLabel, (UINT32)wcslen(printLabel), btnFmt, printR, app.brush);
        app.brush->SetColor(th.text);
        const wchar_t* cancelLabel = tr(app, "print.cancel");
        app.renderTarget->DrawText(cancelLabel, (UINT32)wcslen(cancelLabel), btnFmt, cancelR, app.brush);
    }

    // Hint, bottom left
    if (uiFmt) {
        uiFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F hintC = th.text;
        hintC.a = 0.5f;
        app.brush->SetColor(hintC);
         const wchar_t* hint = tr(app, "print.hint");
        app.renderTarget->DrawText(hint, (UINT32)wcslen(hint), uiFmt,
            D2D1::RectF(margin, h - bottomBarH, cancelR.left - gap, h), app.brush);
    }

    if (uiFmt) uiFmt->Release();
    if (btnFmt) btnFmt->Release();
}

// --- Settings overlay (Ctrl+,) ---
//
// The dialog inherits the active theme deliberately: it is the one piece of
// chrome that demonstrates the theme while you adjust it. Section rail on
// the left, rows of toggles and chips on the right; every interactive rect
// is pushed into app.settingsHits for the mouse-up hit test.

namespace {

// Surface elevation (design 2e): panels and cards lift from the theme
// background toward white - a small step in dark themes, a larger one in
// light themes where the cards read as near-white paper
D2D1_COLOR_F settingsLift(const App& app, float darkT, float lightT,
                          float alpha) {
    float t = app.theme.isDark ? darkT : lightT;
    D2D1_COLOR_F c = app.theme.background;
    c.r += (1.0f - c.r) * t;
    c.g += (1.0f - c.g) * t;
    c.b += (1.0f - c.b) * t;
    c.a = alpha;
    return c;
}

void settingsToggle(App& app, float x, float y, bool on, int action, float anim) {
    float w = dpi(app, 40.0f), h = dpi(app, 20.0f);
    D2D1_COLOR_F track = on ? app.theme.accent : app.theme.text;
    track.a = (on ? 0.9f : 0.25f) * anim;
    app.brush->SetColor(track);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), h / 2, h / 2), app.brush);
    float knobR = h / 2 - dpi(app, 2.0f);
    float knobX = on ? x + w - h / 2 : x + h / 2;
    app.brush->SetColor(D2D1::ColorF(1, 1, 1, anim));
    app.renderTarget->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(knobX, y + h / 2), knobR, knobR), app.brush);
    // Generous hit area: the whole row height around the switch
    app.settingsHits.push_back({D2D1::RectF(x - dpi(app, 8.0f), y - dpi(app, 8.0f),
                                            x + w + dpi(app, 8.0f), y + h + dpi(app, 8.0f)),
                                action});
}

void settingsChip(App& app, float& x, float y, const wchar_t* label, bool active,
                  int action, float anim, IDWriteTextFormat* fmt) {
    float h = dpi(app, 24.0f);
    float padX = dpi(app, 12.0f);
    IDWriteTextLayout* tl = nullptr;
    app.dwriteFactory->CreateTextLayout(label, (UINT32)wcslen(label), fmt, 500.0f, h, &tl);
    float textW = dpi(app, 40.0f);
    if (tl) { DWRITE_TEXT_METRICS m{}; tl->GetMetrics(&m); textW = m.width; }
    float w = textW + padX * 2;
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h),
                                             dpi(app, 5.0f), dpi(app, 5.0f));
    if (active) {
        D2D1_COLOR_F c = app.theme.accent; c.a = anim;
        app.brush->SetColor(c);
        app.renderTarget->FillRoundedRectangle(rr, app.brush);
        app.brush->SetColor(D2D1::ColorF(1, 1, 1, anim));
    } else {
        D2D1_COLOR_F c = app.theme.text; c.a = 0.3f * anim;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);
        c.a = 0.85f * anim;
        app.brush->SetColor(c);
    }
    if (tl) {
        app.renderTarget->DrawTextLayout(
            D2D1::Point2F(x + padX, y + (h - dpi(app, 17.0f)) / 2), tl, app.brush);
        tl->Release();
    }
    app.settingsHits.push_back({D2D1::RectF(x, y, x + w, y + h), action});
    x += w + dpi(app, 8.0f);
}

} // namespace

void renderSettingsOverlay(App& app) {
    if (app.settingsAnimation < 1.0f) {
        float prev = app.settingsAnimation;
        app.settingsAnimation = std::min(1.0f, app.settingsAnimation + 0.2f);
        if (app.settingsAnimation != prev) InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.settingsAnimation;
    app.settingsHits.clear();

    IDWriteTextFormat* fmt = app.folderBrowserFormat;
    if (!fmt) return;

    // Keep the underlying document visible while settings are open.
    float backdropAlpha = (app.theme.isDark ? 0.34f : 0.24f) * anim;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    float panelW = std::min(dpi(app, 760.0f), app.width - dpi(app, 80.0f));
    float panelH = std::min(dpi(app, 540.0f), app.height - dpi(app, 64.0f));
    float px = (app.width - panelW) / 2;
    float py = (app.height - panelH) / 2 + (1 - anim) * dpi(app, 30.0f);

    // Elevated panel: soft drop shadow, lifted surface, hairline border
    for (int ring = 3; ring >= 1; ring--) {
        float spread = dpi(app, (float)ring * 4.0f);
        D2D1_COLOR_F shadow = D2D1::ColorF(
            0.0f, 0.0f, 0.0f, 0.10f * anim / (float)(ring * ring));
        app.brush->SetColor(shadow);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(px - spread, py - spread * 0.4f, px + panelW + spread,
                            py + panelH + spread),
                dpi(app, 14.0f) + spread, dpi(app, 14.0f) + spread),
            app.brush);
    }
    D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(
        D2D1::RectF(px, py, px + panelW, py + panelH), dpi(app, 14.0f), dpi(app, 14.0f));
    app.brush->SetColor(settingsLift(app, 0.045f, 0.5f, 0.99f * anim));
    app.renderTarget->FillRoundedRectangle(panel, app.brush);
    D2D1_COLOR_F border = app.theme.text; border.a = 0.14f * anim;
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(panel, app.brush, 1.0f);

    // Title
    if (app.themeTitleFormat) {
        app.themeTitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F tc = app.theme.heading; tc.a = anim;
        app.brush->SetColor(tc);
        const wchar_t* title = tr(app, "settings.title");
        app.renderTarget->DrawText(title, (UINT32)wcslen(title), app.themeTitleFormat,
            D2D1::RectF(px + dpi(app, 24.0f), py + dpi(app, 16.0f),
                        px + panelW, py + dpi(app, 56.0f)), app.brush);
    }

    // Section rail
    const wchar_t* sections[] = {tr(app, "settings.section.general"),
                                 tr(app, "settings.section.appearance"),
                                 tr(app, "settings.section.editor")};
    const int sectionActions[] = {SET_SECTION_GENERAL, SET_SECTION_APPEARANCE, SET_SECTION_EDITOR};
    float railX = px + dpi(app, 24.0f);
    float railY = py + dpi(app, 68.0f);
    float railW = dpi(app, 140.0f);
    for (int i = 0; i < 3; i++) {
        float rowY = railY + i * dpi(app, 38.0f);
        D2D1_RECT_F r = D2D1::RectF(railX, rowY, railX + railW, rowY + dpi(app, 30.0f));
        if (i == app.settingsSection) {
            // Active section: lifted pill plus an accent indicator bar
            app.brush->SetColor(settingsLift(app, 0.12f, 0.95f, anim));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(r, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
            D2D1_COLOR_F bar = app.theme.accent; bar.a = anim;
            app.brush->SetColor(bar);
            float barH = dpi(app, 14.0f);
            float barY = (r.top + r.bottom - barH) * 0.5f;
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(r.left, barY, r.left + dpi(app, 3.0f), barY + barH),
                    dpi(app, 1.5f), dpi(app, 1.5f)),
                app.brush);
        }
        D2D1_COLOR_F tc = app.theme.text;
        tc.a = (i == app.settingsSection ? 1.0f : 0.55f) * anim;
        app.brush->SetColor(tc);
        app.renderTarget->DrawText(sections[i], (UINT32)wcslen(sections[i]), fmt,
            D2D1::RectF(r.left + dpi(app, 12.0f), r.top + dpi(app, 6.0f), r.right, r.bottom),
            app.brush);
        app.settingsHits.push_back({r, sectionActions[i]});
    }

    // Content column: one lifted card per setting (design 2e), with the
    // control right-aligned and vertically centered inside its card
    float cx = railX + railW + dpi(app, 24.0f);
    float cw = px + panelW - dpi(app, 24.0f) - cx;
    float cy = railY;
    float cardPad = dpi(app, 14.0f);
    float cardGap = dpi(app, 10.0f);
    float rowCardH = dpi(app, 58.0f);
    float sliderCardH = dpi(app, 84.0f);

    D2D1_COLOR_F cardBg = settingsLift(app, 0.085f, 0.85f, anim);
    D2D1_COLOR_F cardBorder = app.theme.isDark
        ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.05f * anim)
        : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f * anim);

    auto card = [&](float h) {
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(cx, cy, cx + cw, cy + h), dpi(app, 8.0f), dpi(app, 8.0f));
        app.brush->SetColor(cardBg);
        app.renderTarget->FillRoundedRectangle(rr, app.brush);
        app.brush->SetColor(cardBorder);
        app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);
    };
    // Title + muted hint in the card's top-left; reserveRight keeps long
    // hints (or translations) clear of the row's control
    auto cardLabel = [&](const wchar_t* label, const wchar_t* hint,
                         float reserveRight = 0.0f) {
        D2D1_COLOR_F tc = app.theme.text; tc.a = 0.95f * anim;
        app.brush->SetColor(tc);
        app.renderTarget->DrawText(label, (UINT32)wcslen(label), fmt,
            D2D1::RectF(cx + cardPad, cy + dpi(app, 10.0f),
                        cx + cw - cardPad - reserveRight, cy + dpi(app, 28.0f)),
            app.brush);
        if (hint) {
            tc.a = 0.5f * anim;
            app.brush->SetColor(tc);
            app.renderTarget->DrawText(hint, (UINT32)wcslen(hint), fmt,
                D2D1::RectF(cx + cardPad, cy + dpi(app, 28.0f),
                            cx + cw - cardPad - reserveRight, cy + dpi(app, 46.0f)),
                app.brush);
        }
    };
    // Right-aligned chip rows: total advance width of settingsChip calls
    auto chipsWidth = [&](std::initializer_list<const wchar_t*> labels) {
        float total = 0.0f;
        for (const wchar_t* l : labels) {
            total += measureText(app, l, fmt) + dpi(app, 24.0f) + dpi(app, 8.0f);
        }
        return total - dpi(app, 8.0f);
    };

    // Percentage slider: track + accent fill + knob + value label. The
    // track rect is stored for drag math; the hit rect covers the row.
    auto slider = [&](float x, float y, float w, int pct, int action, int trackSlot) {
        float trackH = dpi(app, 4.0f);
        float knobR = dpi(app, 7.0f);
        float labelW = dpi(app, 44.0f);
        float trackW = w - labelW;
        D2D1_RECT_F track = D2D1::RectF(x, y - trackH / 2, x + trackW, y + trackH / 2);
        app.settingsSliderTrack[trackSlot] = track;
        D2D1_COLOR_F c = app.theme.text; c.a = 0.18f * anim;
        app.brush->SetColor(c);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(track, trackH / 2, trackH / 2), app.brush);
        float t = (pct - 30) / 70.0f;
        float fillX = x + trackW * t;
        c = app.theme.accent; c.a = anim;
        app.brush->SetColor(c);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(x, track.top, fillX, track.bottom),
                              trackH / 2, trackH / 2), app.brush);
        app.renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(fillX, y), knobR, knobR), app.brush);
        wchar_t label[16];
        if (pct >= 100) wcscpy_s(label, tr(app, "settings.full"));
        else swprintf_s(label, L"%d%%", pct);
        c = app.theme.text; c.a = 0.7f * anim;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(label, (UINT32)wcslen(label), fmt,
            D2D1::RectF(x + trackW + dpi(app, 12.0f), y - dpi(app, 9.0f),
                        x + w, y + dpi(app, 12.0f)), app.brush);
        app.settingsHits.push_back({D2D1::RectF(x - knobR, y - dpi(app, 12.0f),
                                                x + trackW + knobR, y + dpi(app, 12.0f)),
                                    action});
    };

    if (app.settingsSection == 0) {  // General
        card(rowCardH);
        cardLabel(tr(app, "settings.language"), tr(app, "settings.language.hint"), dpi(app, 200.0f));
        {
            // Dropdown value box with a pencil beside it (opens languages.ini)
            float boxH = dpi(app, 26.0f);
            float penW = dpi(app, 26.0f);
            float boxW = dpi(app, 150.0f);
            float boxX = cx + cw - cardPad - penW - dpi(app, 8.0f) - boxW;
            float boxY = cy + (rowCardH - boxH) * 0.5f;
            D2D1_RECT_F box = D2D1::RectF(boxX, boxY, boxX + boxW, boxY + boxH);
            app.settingsLangBox = box;
            D2D1_COLOR_F c = app.theme.text; c.a = (app.settingsLangOpen ? 0.6f : 0.3f) * anim;
            app.brush->SetColor(c);
            app.renderTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(box, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush, 1.0f);
            std::wstring current = app.languageSetting < 0
                ? std::wstring(tr(app, "settings.lang.auto"))
                : std::wstring(languageNameAt(app.languageSetting));
            current += L"  \x25BE";
            c = app.theme.text; c.a = 0.95f * anim;
            app.brush->SetColor(c);
            app.renderTarget->DrawText(current.c_str(), (UINT32)current.size(), fmt,
                D2D1::RectF(box.left + dpi(app, 10.0f), box.top + dpi(app, 4.0f),
                            box.right - dpi(app, 6.0f), box.bottom), app.brush);
            app.settingsHits.push_back({box, SET_LANG_DROPDOWN});

            D2D1_RECT_F pen = D2D1::RectF(box.right + dpi(app, 8.0f), boxY,
                                          box.right + dpi(app, 8.0f) + penW, boxY + boxH);
            c = app.theme.text; c.a = 0.7f * anim;
            app.brush->SetColor(c);
            app.renderTarget->DrawText(L"\x270E", 1, fmt,
                D2D1::RectF(pen.left + dpi(app, 6.0f), pen.top + dpi(app, 4.0f),
                            pen.right, pen.bottom), app.brush);
            app.settingsHits.push_back({pen, SET_OPEN_LANGS_INI});
        }
        cy += rowCardH + cardGap;
        card(rowCardH);
        cardLabel(tr(app, "settings.shortcuts"), tr(app, "settings.shortcuts.hint"), dpi(app, 200.0f));
        {
            // Profile dropdown with a pencil beside it (opens the shortcut
            // editor) - geometry mirrors the language row above exactly
            float boxH = dpi(app, 26.0f);
            float penW = dpi(app, 26.0f);
            float boxW = dpi(app, 150.0f);
            float boxX = cx + cw - cardPad - penW - dpi(app, 8.0f) - boxW;
            float boxY = cy + (rowCardH - boxH) * 0.5f;
            D2D1_RECT_F box = D2D1::RectF(boxX, boxY, boxX + boxW, boxY + boxH);
            app.settingsKeysBox = box;
            D2D1_COLOR_F c = app.theme.text;
            c.a = (app.settingsKeysOpen ? 0.6f : 0.3f) * anim;
            app.brush->SetColor(c);
            app.renderTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(box, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush, 1.0f);
            const char* profKey = app.keyProfile == "vim" ? "settings.profile.vim"
                : app.keyProfile == "custom" ? "settings.profile.custom"
                                             : "settings.profile.windows";
            std::wstring current = tr(app, profKey);
            current += L"  \x25BE";
            c = app.theme.text;
            c.a = 0.95f * anim;
            app.brush->SetColor(c);
            app.renderTarget->DrawText(current.c_str(), (UINT32)current.size(), fmt,
                D2D1::RectF(box.left + dpi(app, 10.0f), box.top + dpi(app, 4.0f),
                            box.right - dpi(app, 6.0f), box.bottom), app.brush);
            app.settingsHits.push_back({box, SET_KEYS_DROPDOWN});

            D2D1_RECT_F pen = D2D1::RectF(box.right + dpi(app, 8.0f), boxY,
                                          box.right + dpi(app, 8.0f) + penW, boxY + boxH);
            c = app.theme.text;
            c.a = 0.7f * anim;
            app.brush->SetColor(c);
            app.renderTarget->DrawText(L"\x270E", 1, fmt,
                D2D1::RectF(pen.left + dpi(app, 6.0f), pen.top + dpi(app, 4.0f),
                            pen.right, pen.bottom), app.brush);
            app.settingsHits.push_back({pen, SET_EDIT_KEYS});
        }
        cy += rowCardH + cardGap;
        card(rowCardH);
        cardLabel(tr(app, "settings.folder_search"), tr(app, "settings.folder_search.hint"), dpi(app, 60.0f));
        settingsToggle(app, cx + cw - cardPad - dpi(app, 40.0f),
                       cy + (rowCardH - dpi(app, 20.0f)) * 0.5f,
                       app.folderSearchEnabled, SET_TOGGLE_FOLDERSEARCH, anim);
        cy += rowCardH + cardGap;
        card(rowCardH);
        cardLabel(tr(app, "settings.browse_focus_path"), tr(app, "settings.browse_focus_path.hint"), dpi(app, 60.0f));
        settingsToggle(app, cx + cw - cardPad - dpi(app, 40.0f),
                       cy + (rowCardH - dpi(app, 20.0f)) * 0.5f,
                       app.browserFocusPath, SET_TOGGLE_BROWSEFOCUS, anim);
        cy += rowCardH + cardGap;
        card(rowCardH);
        cardLabel(tr(app, "settings.open_in_tabs"), tr(app, "settings.open_in_tabs.hint"), dpi(app, 60.0f));
        settingsToggle(app, cx + cw - cardPad - dpi(app, 40.0f),
                       cy + (rowCardH - dpi(app, 20.0f)) * 0.5f,
                       app.openInTabs, SET_TOGGLE_OPENTABS, anim);
        cy += rowCardH + cardGap;
        // One card for the three hand-editable ini files (was three rows)
        card(rowCardH);
        float filesW = chipsWidth({L"settings.ini", L"themes.ini", L"languages.ini"});
        cardLabel(tr(app, "settings.config_files"), tr(app, "settings.config_files.hint"),
                  filesW + dpi(app, 10.0f));
        {
            float bx = cx + cw - cardPad - filesW;
            float chipY = cy + (rowCardH - dpi(app, 24.0f)) * 0.5f;
            settingsChip(app, bx, chipY, L"settings.ini", false, SET_OPEN_INI, anim, fmt);
            settingsChip(app, bx, chipY, L"themes.ini", false, SET_OPEN_THEMES_INI, anim, fmt);
            settingsChip(app, bx, chipY, L"languages.ini", false, SET_OPEN_LANGS_INI, anim, fmt);
        }
    } else if (app.settingsSection == 1) {  // Appearance
        card(sliderCardH);
        cardLabel(tr(app, "settings.reading_width_window"), tr(app, "settings.reading_width_window.hint"));
        slider(cx + cardPad, cy + dpi(app, 62.0f), cw - cardPad * 2,
               app.readingWidthPct, SET_SLIDER_READING, 0);
        cy += sliderCardH + cardGap;
        card(sliderCardH);
        cardLabel(tr(app, "settings.reading_width_full"), tr(app, "settings.reading_width_full.hint"));
        slider(cx + cardPad, cy + dpi(app, 62.0f), cw - cardPad * 2,
               app.zenWidthPct, SET_SLIDER_ZEN, 1);
        cy += sliderCardH + cardGap;
        card(rowCardH);
        cardLabel(tr(app, "settings.heading_rules"), tr(app, "settings.heading_rules.hint"), dpi(app, 60.0f));
        settingsToggle(app, cx + cw - cardPad - dpi(app, 40.0f),
                       cy + (rowCardH - dpi(app, 20.0f)) * 0.5f,
                       app.headingRules, SET_TOGGLE_HEADRULES, anim);
        cy += rowCardH + cardGap;
        card(rowCardH);
        cardLabel(tr(app, "settings.follow_windows"), tr(app, "settings.follow_windows.hint"), dpi(app, 60.0f));
        settingsToggle(app, cx + cw - cardPad - dpi(app, 40.0f),
                       cy + (rowCardH - dpi(app, 20.0f)) * 0.5f,
                       app.followSystemTheme, SET_TOGGLE_FOLLOW, anim);
        cy += rowCardH + cardGap;
        card(rowCardH);
        {
            // Left | Right segmented control inside a sunken container
            const wchar_t* labels[2] = {tr(app, "settings.toc.left"),
                                        tr(app, "settings.toc.right")};
            float segH = dpi(app, 26.0f);
            float segW = std::max(measureText(app, labels[0], fmt),
                                  measureText(app, labels[1], fmt)) +
                         dpi(app, 26.0f);
            float inset = dpi(app, 3.0f);
            float segRight = cx + cw - cardPad - inset;
            float segY = cy + (rowCardH - segH) * 0.5f;
            cardLabel(tr(app, "settings.toc_side"), tr(app, "settings.toc_side.hint"),
                      segW * 2 + inset * 3 + dpi(app, 10.0f));
            D2D1_RECT_F container = D2D1::RectF(
                segRight - segW * 2 - inset * 2, segY - inset,
                segRight + inset, segY + segH + inset);
            D2D1_COLOR_F well = app.theme.isDark
                ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f * anim)
                : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.07f * anim);
            app.brush->SetColor(well);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(container, dpi(app, 8.0f), dpi(app, 8.0f)),
                app.brush);
            for (int s = 0; s < 2; s++) {
                float sx = container.left + inset + s * (segW + inset);
                D2D1_RECT_F sr = D2D1::RectF(sx, segY, sx + segW, segY + segH);
                bool active = (s == 0) == app.tocOnLeft;
                if (active) {
                    app.brush->SetColor(settingsLift(app, 0.22f, 1.0f, anim));
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(sr, dpi(app, 6.0f), dpi(app, 6.0f)),
                        app.brush);
                }
                D2D1_COLOR_F tc = app.theme.text;
                tc.a = (active ? 0.95f : 0.55f) * anim;
                app.brush->SetColor(tc);
                float tw = measureText(app, labels[s], fmt);
                app.renderTarget->DrawText(labels[s], (UINT32)wcslen(labels[s]), fmt,
                    D2D1::RectF(sx + (segW - tw) * 0.5f, segY + dpi(app, 4.0f),
                                sx + segW, segY + segH), app.brush);
                app.settingsHits.push_back({sr, s == 0 ? SET_TOC_LEFT : SET_TOC_RIGHT});
            }
        }
        cy += rowCardH + cardGap;
        card(rowCardH);
        {
            float themesW = chipsWidth({tr(app, "settings.browse"),
                                        tr(app, "settings.edit"),
                                        tr(app, "settings.new")});
            cardLabel(tr(app, "settings.themes"), tr(app, "settings.themes.hint"),
                      themesW + dpi(app, 10.0f));
            float bx2 = cx + cw - cardPad - themesW;
            float chipY = cy + (rowCardH - dpi(app, 24.0f)) * 0.5f;
            settingsChip(app, bx2, chipY, tr(app, "settings.browse"), false, SET_OPEN_THEMES, anim, fmt);
            settingsChip(app, bx2, chipY, tr(app, "settings.edit"), false, SET_EDIT_THEME, anim, fmt);
            // The create action is the accent-filled primary button
            settingsChip(app, bx2, chipY, tr(app, "settings.new"), true, SET_NEW_THEME, anim, fmt);
        }
    } else {  // Editor
        card(rowCardH);
        cardLabel(tr(app, "settings.word_wrap"), tr(app, "settings.word_wrap.hint"), dpi(app, 60.0f));
        settingsToggle(app, cx + cw - cardPad - dpi(app, 40.0f),
                       cy + (rowCardH - dpi(app, 20.0f)) * 0.5f,
                       app.editorWordWrap, SET_TOGGLE_WRAP, anim);
        cy += rowCardH + cardGap;
        card(rowCardH);
        cardLabel(tr(app, "settings.preview_pane"), tr(app, "settings.preview_pane.hint"), dpi(app, 60.0f));
        settingsToggle(app, cx + cw - cardPad - dpi(app, 40.0f),
                       cy + (rowCardH - dpi(app, 20.0f)) * 0.5f,
                       app.editorShowPreview, SET_TOGGLE_PREVIEW, anim);
        cy += rowCardH + cardGap;
        card(rowCardH);
        cardLabel(tr(app, "settings.editor_assists"),
                  tr(app, "settings.editor_assists.hint"), dpi(app, 60.0f));
        settingsToggle(app, cx + cw - cardPad - dpi(app, 40.0f),
                       cy + (rowCardH - dpi(app, 20.0f)) * 0.5f,
                       app.editorAssists, SET_TOGGLE_ASSISTS, anim);
        cy += rowCardH + cardGap;
        // Pandoc bridge: hint shows the resolved executable when found
        card(rowCardH);
        pandocResolve(app);
        cardLabel(tr(app, "settings.pandoc"),
                  app.pandocExe.empty()
                      ? tr(app, "settings.pandoc.hint")
                      : app.pandocExe.c_str(),
                  dpi(app, 110.0f));
        {
            float chipW = chipsWidth({tr(app, "settings.browse")});
            float bx = cx + cw - cardPad - chipW;
            float chipY = cy + (rowCardH - dpi(app, 24.0f)) * 0.5f;
            settingsChip(app, bx, chipY, tr(app, "settings.browse"), false,
                         SET_LOCATE_PANDOC, anim, fmt);
        }
    }

    // Shortcut profile popup, drawn last so it sits above the rows
    if (app.settingsKeysOpen && app.settingsSection == 0) {
        float itemH = dpi(app, 26.0f);
        const char* rowKeys[] = {"settings.profile.windows",
                                 "settings.profile.vim",
                                 "settings.profile.custom"};
        const char* rowIds[] = {"windows", "vim", "custom"};
        int count = 3;
        D2D1_RECT_F box = app.settingsKeysBox;
        D2D1_RECT_F pop = D2D1::RectF(box.left, box.bottom + dpi(app, 4.0f),
                                      box.right,
                                      box.bottom + dpi(app, 4.0f) + itemH * count);
        D2D1_COLOR_F pc = app.theme.background;
        pc.a = anim;
        app.brush->SetColor(pc);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(pop, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
        pc = app.theme.text;
        pc.a = 0.35f * anim;
        app.brush->SetColor(pc);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(pop, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush, 1.0f);
        for (int i = 0; i < count; i++) {
            float ry = pop.top + i * itemH;
            D2D1_RECT_F row = D2D1::RectF(pop.left, ry, pop.right, ry + itemH);
            if (app.keyProfile == rowIds[i]) {
                D2D1_COLOR_F hl = app.theme.accent;
                hl.a = 0.18f * anim;
                app.brush->SetColor(hl);
                app.renderTarget->FillRectangle(row, app.brush);
            }
            const wchar_t* label = tr(app, rowKeys[i]);
            pc = app.theme.text;
            pc.a = 0.95f * anim;
            app.brush->SetColor(pc);
            app.renderTarget->DrawText(label, (UINT32)wcslen(label), fmt,
                D2D1::RectF(row.left + dpi(app, 10.0f), row.top + dpi(app, 4.0f),
                            row.right - dpi(app, 6.0f), row.bottom), app.brush);
            app.settingsHits.push_back({row, SET_KEYS_PICK_BASE + i});
        }
    }

    // Footer hint
    // Language dropdown popup, drawn last so it sits above the rows.
    // Hit-testing iterates settingsHits in reverse, so these win overlaps.
    if (app.settingsLangOpen && app.settingsSection == 0) {
        float itemH = dpi(app, 30.0f);
        // Auto plus only the languages that actually carry translations;
        // untranslated roster entries stay out of the list until
        // languages.ini fills them in.
        std::vector<int> rows;  // registry indices; -1 = Auto
        rows.push_back(-1);
        for (int i = 0; i < languageCount(); i++) {
            if (languageHasTranslations(i)) rows.push_back(i);
        }
        int count = (int)rows.size();
        D2D1_RECT_F box = app.settingsLangBox;
        std::vector<std::wstring> labels;
        labels.reserve(rows.size());
        float popupW = box.right - box.left;
        for (int lang : rows) {
            std::wstring label = lang < 0 ? std::wstring(tr(app, "settings.lang.auto"))
                                          : std::wstring(languageNameAt(lang));
            popupW = std::max(popupW, measureText(app, label, fmt) + dpi(app, 34.0f));
            labels.push_back(std::move(label));
        }
        popupW = std::min(popupW, (float)app.width - dpi(app, 32.0f));
        float popupRight = std::min(box.right, (float)app.width - dpi(app, 16.0f));
        float popupLeft = popupRight - popupW;
        if (popupLeft < dpi(app, 16.0f)) {
            popupLeft = dpi(app, 16.0f);
            popupRight = popupLeft + popupW;
        }

        float belowTop = box.bottom + dpi(app, 4.0f);
        float popupTop = belowTop;
        float popupBottom = popupTop + itemH * count;
        float settingsBottom = py + panelH - dpi(app, 42.0f);
        if (popupBottom > settingsBottom &&
            box.top - dpi(app, 4.0f) - itemH * count >= py + dpi(app, 62.0f)) {
            popupTop = box.top - dpi(app, 4.0f) - itemH * count;
        }
        D2D1_RECT_F pop = D2D1::RectF(popupLeft, popupTop, popupRight,
                                      popupTop + itemH * count);
        D2D1_COLOR_F pc = app.theme.background; pc.a = anim;
        app.brush->SetColor(pc);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(pop, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
        pc = app.theme.text; pc.a = 0.35f * anim;
        app.brush->SetColor(pc);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(pop, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush, 1.0f);
        for (int r = 0; r < count; r++) {
            int lang = rows[r];
            float ry = pop.top + r * itemH;
            D2D1_RECT_F row = D2D1::RectF(pop.left, ry, pop.right, ry + itemH);
            bool active = lang < 0 ? app.languageSetting < 0
                                   : app.languageSetting == lang;
            bool hovered = app.mouseX >= row.left && app.mouseX <= row.right &&
                           app.mouseY >= row.top && app.mouseY <= row.bottom;
            if (active || hovered) {
                D2D1_COLOR_F hl = app.theme.accent; hl.a = 0.18f * anim;
                if (!active) hl.a = 0.10f * anim;
                app.brush->SetColor(hl);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(row.left + dpi(app, 2.0f), row.top,
                                                  row.right - dpi(app, 2.0f), row.bottom),
                                      dpi(app, 4.0f), dpi(app, 4.0f)), app.brush);
            }
            pc = app.theme.text; pc.a = 0.95f * anim;
            app.brush->SetColor(pc);
            app.renderTarget->DrawText(labels[r].c_str(), (UINT32)labels[r].size(), fmt,
                D2D1::RectF(row.left + dpi(app, 10.0f), row.top + dpi(app, 4.0f),
                            row.right - dpi(app, 30.0f), row.bottom), app.brush);
            if (active) {
                D2D1_COLOR_F checkColor = app.theme.accent; checkColor.a = anim;
                app.brush->SetColor(checkColor);
                app.renderTarget->DrawText(L"\x2713", 1, fmt,
                    D2D1::RectF(row.right - dpi(app, 23.0f), row.top + dpi(app, 4.0f),
                                row.right - dpi(app, 8.0f), row.bottom), app.brush);
            }
            // -1 (Auto) lands on SET_LANG_PICK_BASE; input.cpp decodes
            // pick - 1 as the registry index, so gaps are fine.
            app.settingsHits.push_back({row, SET_LANG_PICK_BASE + 1 + lang});
        }
    }

    D2D1_COLOR_F fc = app.theme.text; fc.a = 0.4f * anim;
    app.brush->SetColor(fc);
    const wchar_t* footer = tr(app, "settings.footer");
    app.renderTarget->DrawText(footer, (UINT32)wcslen(footer), fmt,
        D2D1::RectF(px + dpi(app, 24.0f), py + panelH - dpi(app, 32.0f),
                    px + panelW, py + panelH), app.brush);
}

// --- Theme editor ---
//
// Left column: name, base theme, dark flag, six hex color fields with
// swatches. Right column: the live ink specimen (repainted with the working
// theme and font on every keystroke) above the system font list, each
// family drawn in its own face. Fixed panel size keeps the geometry simple.

// --- Unsaved-changes dialog ---
//
// A real modal with clickable buttons instead of the old toast prompt:
// the state is unmistakable, mouse users have a way out, and the safe
// action rides Enter. Buttons auto-size to their (translated) labels.
// --- Prompt chips (t13): the unsaved-exit and create-file prompts wear
// the signal chip anatomy, pinned to the corner and idle until answered.

// Ring shadow shared with the signal stack
static void promptChipShadow(App& app, const D2D1_RECT_F& r, float radius) {
    for (int ring = 3; ring >= 1; ring--) {
        float spread = dpi(app, (float)ring * 2.2f);
        D2D1_COLOR_F shadow = D2D1::ColorF(
            0.0f, 0.0f, 0.0f,
            (app.theme.isDark ? 0.18f : 0.08f) / (float)(ring * ring));
        app.brush->SetColor(shadow);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(r.left - spread, r.top - spread * 0.3f,
                                          r.right + spread, r.bottom + spread),
                              radius + spread, radius + spread),
            app.brush);
    }
}

static D2D1_COLOR_F promptChipSurface(const App& app) {
    D2D1_COLOR_F bg = app.theme.background;
    float t = app.theme.isDark ? 0.012f : 0.31f;
    bg.r += (1.0f - bg.r) * t;
    bg.g += (1.0f - bg.g) * t;
    bg.b += (1.0f - bg.b) * t;
    bg.a = 0.98f;
    return bg;
}

// One keycap; returns the width consumed (drawn right-to-left caller side)
static float promptKeycap(App& app, const wchar_t* label, float rightX,
                          float cy) {
    if (!app.signalSmallFormat) return 0.0f;
    float tw = measureText(app, label, app.signalSmallFormat);
    float w = tw + dpi(app, 9.0f);
    float h = dpi(app, 15.0f);
    D2D1_RECT_F r = D2D1::RectF(rightX - w, cy - h * 0.5f, rightX, cy + h * 0.5f);
    D2D1_COLOR_F c = app.theme.text; c.a = 0.25f;
    app.brush->SetColor(c);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(r, dpi(app, 3.0f), dpi(app, 3.0f)), app.brush, 1.0f);
    c.a = 0.6f;
    app.brush->SetColor(c);
    app.renderTarget->DrawText(label, (UINT32)wcslen(label),
        app.signalSmallFormat,
        D2D1::RectF(r.left + dpi(app, 4.5f), r.top + dpi(app, 1.5f),
                    r.right, r.bottom),
        app.brush);
    return w + dpi(app, 4.0f);
}

void renderConfirmExitDialog(App& app) {
    IDWriteTextFormat* fmt = app.folderBrowserFormat;
    if (!fmt) return;
    app.confirmExitHits.clear();
    const D2DTheme& base = app.theme;
    D2D1_COLOR_F warn = signalSeverityHue(app, SIG_WARN);

    // Title: "Unsaved changes" with the file it guards in bold
    std::wstring name;
    if (!app.tabs.empty() && app.activeTab >= 0 &&
        app.activeTab < (int)app.tabs.size()) {
        name = app.tabs[app.activeTab].title;
    }
    std::wstring title = tr(app, "confirm.title");
    if (!name.empty()) title += L" \u2014 " + name;

    std::wstring saveLbl = tr(app, "confirm.save");
    std::wstring discardLbl = tr(app, "confirm.discard");
    const wchar_t* stays = tr(app, "signal.stays");

    float pad = dpi(app, 12.0f);
    float iconBox = dpi(app, 22.0f);
    float btnH = dpi(app, 23.0f);
    float titleW = measureText(app, title, fmt);
    float saveW = measureText(app, saveLbl, fmt) + dpi(app, 20.0f);
    float discardW = measureText(app, discardLbl, fmt) + dpi(app, 20.0f);
    float staysW = app.signalSmallFormat
                       ? measureText(app, stays, app.signalSmallFormat)
                       : 0.0f;
    float keycapsW = dpi(app, 92.0f);  // Y, N, Esc plus the cross
    float w = std::max(pad + iconBox + dpi(app, 9.0f) + titleW +
                           dpi(app, 12.0f) + keycapsW + pad,
                       pad + saveW + dpi(app, 6.0f) + discardW +
                           dpi(app, 14.0f) + staysW + pad);
    w = std::max(w, dpi(app, 340.0f));
    w = std::min(w, (float)app.width - dpi(app, 28.0f));
    float h = dpi(app, 72.0f);
    float right = (float)app.width - dpi(app, 14.0f);
    float bottom = (float)app.height - dpi(app, 14.0f) -
                   (app.showStats ? dpi(app, 55.0f) : 0.0f);
    D2D1_RECT_F r = D2D1::RectF(right - w, bottom - h, right, bottom);

    promptChipShadow(app, r, dpi(app, 10.0f));
    app.brush->SetColor(promptChipSurface(app));
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(r, dpi(app, 10.0f), dpi(app, 10.0f)), app.brush);
    D2D1_COLOR_F border = warn; border.a = 0.45f;
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(r, dpi(app, 10.0f), dpi(app, 10.0f)), app.brush, 1.0f);

    // Row 1: warning icon square, title, keycaps, cross (= keep editing)
    float row1cy = r.top + dpi(app, 10.0f) + iconBox * 0.5f;
    D2D1_RECT_F ib = D2D1::RectF(r.left + pad, row1cy - iconBox * 0.5f,
                                 r.left + pad + iconBox, row1cy + iconBox * 0.5f);
    D2D1_COLOR_F tint = warn; tint.a = 0.14f;
    app.brush->SetColor(tint);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(ib, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
    {   // monoline warning triangle
        float cx = (ib.left + ib.right) * 0.5f, cy = (ib.top + ib.bottom) * 0.5f;
        float s = dpi(app, 12.0f);
        app.brush->SetColor(warn);
        D2D1_POINT_2F a = D2D1::Point2F(cx, cy - s * 0.42f);
        D2D1_POINT_2F b = D2D1::Point2F(cx + s * 0.46f, cy + s * 0.36f);
        D2D1_POINT_2F c = D2D1::Point2F(cx - s * 0.46f, cy + s * 0.36f);
        app.renderTarget->DrawLine(a, b, app.brush, 1.3f);
        app.renderTarget->DrawLine(b, c, app.brush, 1.3f);
        app.renderTarget->DrawLine(c, a, app.brush, 1.3f);
        app.renderTarget->DrawLine(D2D1::Point2F(cx, cy - s * 0.08f),
                                   D2D1::Point2F(cx, cy + s * 0.12f), app.brush, 1.3f);
    }

    // Cross first (right edge), then the keycaps walking left
    float kx = r.right - pad;
    {
        D2D1_COLOR_F c = base.text; c.a = 0.45f;
        app.brush->SetColor(c);
        float s = dpi(app, 3.6f);
        float ccx = kx - dpi(app, 5.0f);
        app.renderTarget->DrawLine(D2D1::Point2F(ccx - s, row1cy - s),
                                   D2D1::Point2F(ccx + s, row1cy + s), app.brush, 1.3f);
        app.renderTarget->DrawLine(D2D1::Point2F(ccx - s, row1cy + s),
                                   D2D1::Point2F(ccx + s, row1cy - s), app.brush, 1.3f);
        app.confirmExitHits.push_back(
            {D2D1::RectF(kx - dpi(app, 12.0f), r.top, r.right, r.top + dpi(app, 26.0f)), 3});
        kx -= dpi(app, 18.0f);
    }
    kx -= promptKeycap(app, L"Esc", kx, row1cy);
    kx -= promptKeycap(app, L"N", kx, row1cy);
    kx -= promptKeycap(app, L"Y", kx, row1cy);

    IDWriteTextLayout* tl = nullptr;
    app.dwriteFactory->CreateTextLayout(
        title.c_str(), (UINT32)title.size(), fmt,
        kx - ib.right - dpi(app, 12.0f), iconBox, &tl);
    if (tl) {
        tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimOpt{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        tl->SetTrimming(&trimOpt, nullptr);
        tl->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD,
                          {0, (UINT32)title.size()});
        DWRITE_TEXT_METRICS m{};
        tl->GetMetrics(&m);
        D2D1_COLOR_F c = base.text; c.a = 0.95f;
        app.brush->SetColor(c);
        app.renderTarget->DrawTextLayout(
            D2D1::Point2F(ib.right + dpi(app, 9.0f), row1cy - m.height * 0.5f),
            tl, app.brush);
        tl->Release();
    }

    // Row 2: Save & exit (filled), Discard (danger outline), the stays hint
    float by = r.bottom - dpi(app, 10.0f) - btnH;
    float bx = r.left + pad;
    {
        D2D1_RECT_F br = D2D1::RectF(bx, by, bx + saveW, by + btnH);
        D2D1_COLOR_F c = base.accent; c.a = 0.94f;
        app.brush->SetColor(c);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(br, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
        app.brush->SetColor(D2D1::ColorF(1, 1, 1, 0.97f));
        app.renderTarget->DrawText(saveLbl.c_str(), (UINT32)saveLbl.size(), fmt,
            D2D1::RectF(br.left + dpi(app, 10.0f), br.top + dpi(app, 3.0f),
                        br.right, br.bottom), app.brush);
        app.confirmExitHits.push_back({br, 1});
        bx = br.right + dpi(app, 6.0f);
    }
    {
        D2D1_RECT_F br = D2D1::RectF(bx, by, bx + discardW, by + btnH);
        app.brush->SetColor(D2D1::ColorF(0.82f, 0.28f, 0.22f, 0.8f));
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(br, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush, 1.0f);
        D2D1_COLOR_F c = base.text; c.a = 0.85f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(discardLbl.c_str(), (UINT32)discardLbl.size(),
            fmt,
            D2D1::RectF(br.left + dpi(app, 10.0f), br.top + dpi(app, 3.0f),
                        br.right, br.bottom), app.brush);
        app.confirmExitHits.push_back({br, 2});
    }
    if (app.signalSmallFormat) {
        D2D1_COLOR_F c = base.text; c.a = 0.42f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(stays, (UINT32)wcslen(stays),
            app.signalSmallFormat,
            D2D1::RectF(r.right - pad - staysW, by + dpi(app, 6.0f),
                        r.right - pad + dpi(app, 2.0f), r.bottom), app.brush);
    }
    app.promptChipRect = r;
}

// --- Create-missing-reference dialog ---
//
// Same shell as the unsaved-changes dialog: a clicked ghost reference
// offers to create the file and start typing. The body line shows the
// resolved target path.
void renderCreateRefDialog(App& app) {
    IDWriteTextFormat* fmt = app.folderBrowserFormat;
    if (!fmt) return;
    app.createRefHits.clear();
    const D2DTheme& base = app.theme;
    D2D1_COLOR_F hue = base.accent;

    std::wstring title = tr(app, "createref.title");
    std::wstring createLbl = tr(app, "createref.create");
    std::wstring cancelLbl = tr(app, "createref.cancel");
    std::wstring path = toWide(app.createRefPath);

    float pad = dpi(app, 12.0f);
    float iconBox = dpi(app, 22.0f);
    float btnH = dpi(app, 23.0f);
    float titleW = measureText(app, title, fmt);
    float createW = measureText(app, createLbl, fmt) + dpi(app, 20.0f);
    float cancelW = measureText(app, cancelLbl, fmt) + dpi(app, 20.0f);
    float keycapsW = dpi(app, 88.0f);  // Enter, Esc plus the cross
    float w = std::max(pad + iconBox + dpi(app, 9.0f) + titleW +
                           dpi(app, 12.0f) + keycapsW + pad,
                       pad + createW + dpi(app, 6.0f) + cancelW + pad);
    w = std::max(w, dpi(app, 360.0f));
    w = std::min(w, (float)app.width - dpi(app, 28.0f));
    float h = dpi(app, 88.0f);
    float right = (float)app.width - dpi(app, 14.0f);
    float bottom = (float)app.height - dpi(app, 14.0f) -
                   (app.showStats ? dpi(app, 55.0f) : 0.0f);
    D2D1_RECT_F r = D2D1::RectF(right - w, bottom - h, right, bottom);

    promptChipShadow(app, r, dpi(app, 10.0f));
    app.brush->SetColor(promptChipSurface(app));
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(r, dpi(app, 10.0f), dpi(app, 10.0f)), app.brush);
    D2D1_COLOR_F border = hue; border.a = 0.4f;
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(r, dpi(app, 10.0f), dpi(app, 10.0f)), app.brush, 1.0f);

    // Row 1: page icon, question, keycaps, cross (= cancel)
    float row1cy = r.top + dpi(app, 10.0f) + iconBox * 0.5f;
    D2D1_RECT_F ib = D2D1::RectF(r.left + pad, row1cy - iconBox * 0.5f,
                                 r.left + pad + iconBox, row1cy + iconBox * 0.5f);
    D2D1_COLOR_F tint = hue; tint.a = 0.13f;
    app.brush->SetColor(tint);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(ib, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
    {   // monoline page with a fold
        float cx = (ib.left + ib.right) * 0.5f, cy = (ib.top + ib.bottom) * 0.5f;
        float s = dpi(app, 12.0f);
        float left = cx - s * 0.3f, rightE = cx + s * 0.3f;
        float top = cy - s * 0.44f, bot = cy + s * 0.44f;
        float fold = s * 0.22f;
        app.brush->SetColor(hue);
        app.renderTarget->DrawLine(D2D1::Point2F(left, top),
                                   D2D1::Point2F(rightE - fold, top), app.brush, 1.3f);
        app.renderTarget->DrawLine(D2D1::Point2F(rightE - fold, top),
                                   D2D1::Point2F(rightE, top + fold), app.brush, 1.3f);
        app.renderTarget->DrawLine(D2D1::Point2F(rightE, top + fold),
                                   D2D1::Point2F(rightE, bot), app.brush, 1.3f);
        app.renderTarget->DrawLine(D2D1::Point2F(rightE, bot),
                                   D2D1::Point2F(left, bot), app.brush, 1.3f);
        app.renderTarget->DrawLine(D2D1::Point2F(left, bot),
                                   D2D1::Point2F(left, top), app.brush, 1.3f);
    }

    float kx = r.right - pad;
    {
        D2D1_COLOR_F c = base.text; c.a = 0.45f;
        app.brush->SetColor(c);
        float s = dpi(app, 3.6f);
        float ccx = kx - dpi(app, 5.0f);
        app.renderTarget->DrawLine(D2D1::Point2F(ccx - s, row1cy - s),
                                   D2D1::Point2F(ccx + s, row1cy + s), app.brush, 1.3f);
        app.renderTarget->DrawLine(D2D1::Point2F(ccx - s, row1cy + s),
                                   D2D1::Point2F(ccx + s, row1cy - s), app.brush, 1.3f);
        app.createRefHits.push_back(
            {D2D1::RectF(kx - dpi(app, 12.0f), r.top, r.right, r.top + dpi(app, 26.0f)), 2});
        kx -= dpi(app, 18.0f);
    }
    kx -= promptKeycap(app, L"Esc", kx, row1cy);
    kx -= promptKeycap(app, L"Enter", kx, row1cy);

    {
        IDWriteTextLayout* tl = nullptr;
        app.dwriteFactory->CreateTextLayout(
            title.c_str(), (UINT32)title.size(), fmt,
            kx - ib.right - dpi(app, 12.0f), iconBox, &tl);
        if (tl) {
            tl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trimOpt{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            tl->SetTrimming(&trimOpt, nullptr);
            tl->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              {0, (UINT32)title.size()});
            DWRITE_TEXT_METRICS m{};
            tl->GetMetrics(&m);
            D2D1_COLOR_F c = base.text; c.a = 0.95f;
            app.brush->SetColor(c);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(ib.right + dpi(app, 9.0f),
                              row1cy - m.height * 0.5f),
                tl, app.brush);
            tl->Release();
        }
    }

    // Row 2: the resolved target path, quiet and end-trimmed
    if (app.signalSmallFormat) {
        IDWriteTextLayout* pl = nullptr;
        app.dwriteFactory->CreateTextLayout(
            path.c_str(), (UINT32)path.size(), app.signalSmallFormat,
            r.right - pad - (ib.right + dpi(app, 9.0f)), dpi(app, 14.0f), &pl);
        if (pl) {
            pl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trimOpt{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            pl->SetTrimming(&trimOpt, nullptr);
            D2D1_COLOR_F c = base.text; c.a = 0.5f;
            app.brush->SetColor(c);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(ib.right + dpi(app, 9.0f),
                              r.top + dpi(app, 34.0f)),
                pl, app.brush);
            pl->Release();
        }
    }

    // Row 3: Create (filled) and Cancel (outline)
    float by = r.bottom - dpi(app, 10.0f) - btnH;
    float bx = r.left + pad;
    {
        D2D1_RECT_F br = D2D1::RectF(bx, by, bx + createW, by + btnH);
        D2D1_COLOR_F c = base.accent; c.a = 0.94f;
        app.brush->SetColor(c);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(br, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
        app.brush->SetColor(D2D1::ColorF(1, 1, 1, 0.97f));
        app.renderTarget->DrawText(createLbl.c_str(), (UINT32)createLbl.size(),
            fmt,
            D2D1::RectF(br.left + dpi(app, 10.0f), br.top + dpi(app, 3.0f),
                        br.right, br.bottom), app.brush);
        app.createRefHits.push_back({br, 1});
        bx = br.right + dpi(app, 6.0f);
    }
    {
        D2D1_RECT_F br = D2D1::RectF(bx, by, bx + cancelW, by + btnH);
        D2D1_COLOR_F c = base.text; c.a = 0.3f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(br, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush, 1.0f);
        c.a = 0.85f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(cancelLbl.c_str(), (UINT32)cancelLbl.size(),
            fmt,
            D2D1::RectF(br.left + dpi(app, 10.0f), br.top + dpi(app, 3.0f),
                        br.right, br.bottom), app.brush);
        app.createRefHits.push_back({br, 2});
    }
    app.promptChipRect = r;
}

// --- Shortcut editor (profiles follow-up) ---
//
// Twelve action rows with their current binding. Clicking a row arms it;
// the next keypress becomes the binding (letters/digits/F-keys via
// keydown, punctuation via WM_CHAR so any keyboard layout works). Every
// edit lands in the custom profile.
static const char* shortcutActionNameKey(int i) {
    switch (i) {
        case KA_SEARCH:     return "help.view.search";
        case KA_BROWSE:     return "help.view.folder_browser";
        case KA_TOC:        return "help.view.toc";
        case KA_THEME:      return "help.view.theme";
        case KA_STATS:      return "help.view.stats";
        case KA_QUIT:       return "help.general.quit";
        case KA_NEWFILE:    return "ctx.new";
        case KA_ZEN:        return "shortcuts.zen";
        case KA_SCROLLUP:   return "help.nav.scroll_up";
        case KA_SCROLLDOWN: return "help.nav.scroll_down";
        case KA_EDIT:       return "help.edit.enter_edit";
        case KA_HELP:       return "help.view.help";
        case KA_ANNOTATE:   return "ctx.annotate";
    }
    return "help.view.help";
}

void renderShortcutEditor(App& app) {
    IDWriteTextFormat* fmt = app.folderBrowserFormat;
    if (!fmt) return;
    app.shortcutHits.clear();
    const D2DTheme& base = app.theme;

    float backdropAlpha = base.isDark ? 0.34f : 0.24f;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    float rowH = dpi(app, 30.0f);
    float panelW = dpi(app, 430.0f);
    float panelH = dpi(app, 100.0f) + rowH * KEY_ACTION_COUNT + dpi(app, 46.0f);
    float px = (app.width - panelW) / 2, py = (app.height - panelH) / 2;
    D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(
        D2D1::RectF(px, py, px + panelW, py + panelH), dpi(app, 12.0f), dpi(app, 12.0f));
    D2D1_COLOR_F bg = base.background; bg.a = 0.99f;
    app.brush->SetColor(bg);
    app.renderTarget->FillRoundedRectangle(panel, app.brush);
    D2D1_COLOR_F border = base.text; border.a = 0.25f;
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(panel, app.brush, 1.0f);

    if (app.themeTitleFormat) {
        app.themeTitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F tc = base.heading; tc.a = 1.0f;
        app.brush->SetColor(tc);
        const wchar_t* title = tr(app, "shortcuts.editor.title");
        app.renderTarget->DrawText(title, (UINT32)wcslen(title), app.themeTitleFormat,
            D2D1::RectF(px + dpi(app, 24.0f), py + dpi(app, 16.0f),
                        px + panelW, py + dpi(app, 52.0f)), app.brush);
    }
    {
        D2D1_COLOR_F hc = base.text; hc.a = 0.55f;
        app.brush->SetColor(hc);
        const wchar_t* hint = tr(app, "shortcuts.editor.hint");
        app.renderTarget->DrawText(hint, (UINT32)wcslen(hint), fmt,
            D2D1::RectF(px + dpi(app, 24.0f), py + dpi(app, 56.0f),
                        px + panelW - dpi(app, 24.0f), py + dpi(app, 80.0f)), app.brush);
    }

    float rowsTop = py + dpi(app, 88.0f);
    for (int i = 0; i < KEY_ACTION_COUNT; i++) {
        float ry = rowsTop + i * rowH;
        D2D1_RECT_F row = D2D1::RectF(px + dpi(app, 16.0f), ry,
                                      px + panelW - dpi(app, 16.0f), ry + rowH - dpi(app, 4.0f));
        bool armed = app.shortcutEditorRow == i;
        if (armed) {
            D2D1_COLOR_F hl = base.accent; hl.a = 0.18f;
            app.brush->SetColor(hl);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(row, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush);
        }
        const wchar_t* name = tr(app, shortcutActionNameKey(i));
        D2D1_COLOR_F c = base.text; c.a = 0.9f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(name, (UINT32)wcslen(name), fmt,
            D2D1::RectF(row.left + dpi(app, 8.0f), row.top + dpi(app, 4.0f),
                        row.right - dpi(app, 96.0f), row.bottom), app.brush);

        // Key chip, right-aligned
        std::wstring key = armed ? tr(app, "shortcuts.editor.press")
                                 : keyLabel(app.keymap[i]);
        float chipW = dpi(app, armed ? 110.0f : 72.0f);
        D2D1_RECT_F chip = D2D1::RectF(row.right - chipW - dpi(app, 6.0f),
                                       row.top + dpi(app, 2.0f),
                                       row.right - dpi(app, 6.0f),
                                       row.bottom - dpi(app, 2.0f));
        c = base.text; c.a = armed ? 0.6f : 0.3f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(chip, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush,
            armed ? 1.5f : 1.0f);
        c = base.text; c.a = 0.95f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(key.c_str(), (UINT32)key.size(), fmt,
            D2D1::RectF(chip.left + dpi(app, 8.0f), chip.top + dpi(app, 2.0f),
                        chip.right - dpi(app, 4.0f), chip.bottom), app.brush);

        app.shortcutHits.push_back({row, i});
    }

    // Footer: reset chip + close hint
    float fy = rowsTop + KEY_ACTION_COUNT * rowH + dpi(app, 8.0f);
    {
        const wchar_t* reset = tr(app, "shortcuts.editor.reset");
        D2D1_RECT_F chip = D2D1::RectF(px + dpi(app, 24.0f), fy,
                                       px + dpi(app, 24.0f) + dpi(app, 90.0f),
                                       fy + dpi(app, 24.0f));
        D2D1_COLOR_F c = base.text; c.a = 0.3f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(chip, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush, 1.0f);
        c = base.text; c.a = 0.8f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(reset, (UINT32)wcslen(reset), fmt,
            D2D1::RectF(chip.left + dpi(app, 8.0f), chip.top + dpi(app, 2.0f),
                        chip.right, chip.bottom), app.brush);
        app.shortcutHits.push_back({chip, 100});
    }
    {
        D2D1_COLOR_F c = base.text; c.a = 0.4f;
        app.brush->SetColor(c);
        const wchar_t* esc = tr(app, "shortcuts.editor.footer");
        app.renderTarget->DrawText(esc, (UINT32)wcslen(esc), fmt,
            D2D1::RectF(px + dpi(app, 140.0f), fy + dpi(app, 2.0f),
                        px + panelW - dpi(app, 24.0f), fy + dpi(app, 26.0f)), app.brush);
    }
}

void renderThemeEditor(App& app) {
    IDWriteTextFormat* fmt = app.folderBrowserFormat;
    if (!fmt) return;
    app.themeEditorHits.clear();
    const D2DTheme& base = app.theme;      // dialog chrome uses the ACTIVE theme
    const D2DTheme& work = app.themeEditorTheme;  // specimen uses the WORKING one

    float backdropAlpha = base.isDark ? 0.34f : 0.24f;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    float panelW = dpi(app, 760.0f), panelH = dpi(app, 520.0f);
    float px = (app.width - panelW) / 2, py = (app.height - panelH) / 2;
    D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(
        D2D1::RectF(px, py, px + panelW, py + panelH), dpi(app, 12.0f), dpi(app, 12.0f));
    D2D1_COLOR_F bg = base.background; bg.a = 0.99f;
    app.brush->SetColor(bg);
    app.renderTarget->FillRoundedRectangle(panel, app.brush);
    D2D1_COLOR_F border = base.text; border.a = 0.25f;
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(panel, app.brush, 1.0f);

    if (app.themeTitleFormat) {
        app.themeTitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F tc = base.heading; tc.a = 1.0f;
        app.brush->SetColor(tc);
        const wchar_t* title = tr(app, "theme.editor.title");
        app.renderTarget->DrawText(title, (UINT32)wcslen(title), app.themeTitleFormat,
            D2D1::RectF(px + dpi(app, 24.0f), py + dpi(app, 16.0f),
                        px + panelW, py + dpi(app, 52.0f)), app.brush);
    }

    float x0 = px + dpi(app, 24.0f);
    float colW = dpi(app, 300.0f);
    float y = py + dpi(app, 64.0f);

    auto inputBox = [&](float bx, float by, float bw, const std::wstring& text,
                        bool focused, int action) {
        D2D1_RECT_F r = D2D1::RectF(bx, by, bx + bw, by + dpi(app, 26.0f));
        D2D1_COLOR_F c = base.text; c.a = focused ? 0.6f : 0.25f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(r, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush,
            focused ? 1.5f : 1.0f);
        std::wstring shown = text;
        if (focused) shown += L"_";
        c = base.text; c.a = 0.95f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(shown.c_str(), (UINT32)shown.size(), fmt,
            D2D1::RectF(r.left + dpi(app, 8.0f), r.top + dpi(app, 4.0f),
                        r.right - dpi(app, 4.0f), r.bottom), app.brush);
        app.themeEditorHits.push_back({r, action});
    };
    auto label = [&](float lx, float ly, const wchar_t* text, float alpha) {
        D2D1_COLOR_F c = base.text; c.a = alpha;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(text, (UINT32)wcslen(text), fmt,
            D2D1::RectF(lx, ly, lx + colW, ly + dpi(app, 18.0f)), app.brush);
    };

    // Name
    label(x0, y, tr(app, "theme.editor.name"), 0.55f);
    inputBox(x0, y + dpi(app, 20.0f), dpi(app, 276.0f), app.themeEditorName,
             app.themeEditorField == 6, TE_FIELD_NAME);
    y += dpi(app, 56.0f);

    // Base theme cycler + dark toggle on one row
    label(x0, y, tr(app, "theme.editor.based_on"), 0.55f);
    {
        float by = y + dpi(app, 20.0f);
        D2D1_RECT_F prev = D2D1::RectF(x0, by, x0 + dpi(app, 26.0f), by + dpi(app, 26.0f));
        D2D1_RECT_F next = D2D1::RectF(x0 + dpi(app, 250.0f), by,
                                       x0 + dpi(app, 276.0f), by + dpi(app, 26.0f));
        D2D1_COLOR_F c = base.text; c.a = 0.7f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(L"\x2039", 1, fmt,
            D2D1::RectF(prev.left + dpi(app, 9.0f), prev.top + dpi(app, 3.0f),
                        prev.right, prev.bottom), app.brush);
        app.renderTarget->DrawText(L"\x203A", 1, fmt,
            D2D1::RectF(next.left + dpi(app, 9.0f), next.top + dpi(app, 3.0f),
                        next.right, next.bottom), app.brush);
        const D2DTheme& bt = themeAt(app.themeEditorBase);
        c = base.text; c.a = 0.95f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(bt.name, (UINT32)wcslen(bt.name), fmt,
            D2D1::RectF(prev.right + dpi(app, 10.0f), by + dpi(app, 4.0f),
                        next.left - dpi(app, 6.0f), by + dpi(app, 24.0f)), app.brush);
        app.themeEditorHits.push_back({prev, TE_BASE_PREV});
        app.themeEditorHits.push_back({next, TE_BASE_NEXT});
    }
    y += dpi(app, 56.0f);

    // Dark flag
    label(x0, y + dpi(app, 3.0f), tr(app, "theme.editor.dark"), 0.95f);
    {
        float tx = x0 + dpi(app, 242.0f), ty = y;
        float tw = dpi(app, 34.0f), th = dpi(app, 18.0f);
        bool on = work.isDark;
        D2D1_COLOR_F track = on ? base.accent : base.text;
        track.a = on ? 0.9f : 0.25f;
        app.brush->SetColor(track);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + tw, ty + th), th / 2, th / 2),
            app.brush);
        float knobX = on ? tx + tw - th / 2 : tx + th / 2;
        app.brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
        app.renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(knobX, ty + th / 2),
                          th / 2 - dpi(app, 2.0f), th / 2 - dpi(app, 2.0f)), app.brush);
        app.themeEditorHits.push_back({D2D1::RectF(tx - dpi(app, 6.0f), ty - dpi(app, 6.0f),
                                                   tx + tw + dpi(app, 6.0f), ty + th + dpi(app, 6.0f)),
                                       TE_DARK});
    }
    y += dpi(app, 34.0f);

    // Six color fields
    static const char* kColorLabelKeys[6] = {
        "theme.editor.color.background", "theme.editor.color.text",
        "theme.editor.color.heading", "theme.editor.color.link",
        "theme.editor.color.accent", "theme.editor.color.code_background"};
    const D2D1_COLOR_F* slots[6] = {&work.background, &work.text, &work.heading,
                                    &work.link, &work.accent, &work.codeBackground};
    for (int i = 0; i < 6; i++) {
        float ry = y + i * dpi(app, 36.0f);
        label(x0, ry + dpi(app, 5.0f), tr(app, kColorLabelKeys[i]), 0.95f);
        inputBox(x0 + dpi(app, 138.0f), ry, dpi(app, 96.0f), app.themeEditorHex[i],
                 app.themeEditorField == i, TE_FIELD_BG + i);
        D2D1_RECT_F sw = D2D1::RectF(x0 + dpi(app, 244.0f), ry,
                                     x0 + dpi(app, 270.0f), ry + dpi(app, 26.0f));
        app.brush->SetColor(*slots[i]);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(sw, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush);
        D2D1_COLOR_F c = base.text; c.a = 0.3f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(sw, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush, 1.0f);
    }

    // ---- Right column: specimen + font list ----
    float xr = px + dpi(app, 350.0f);
    float xrw = px + panelW - dpi(app, 24.0f) - xr;

    // Live ink specimen in the working theme + font
    {
        D2D1_RECT_F spec = D2D1::RectF(xr, py + dpi(app, 64.0f),
                                       xr + xrw, py + dpi(app, 218.0f));
        app.brush->SetColor(work.background);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(spec, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush);
        D2D1_COLOR_F c = base.text; c.a = 0.2f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(spec, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush, 1.0f);

        const wchar_t* fam = app.themeEditorFont.empty()
            ? L"Segoe UI" : app.themeEditorFont.c_str();
        IDWriteTextFormat* h = nullptr; IDWriteTextFormat* b = nullptr;
        IDWriteTextFormat* m = nullptr;
        app.dwriteFactory->CreateTextFormat(fam, nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, dpi(app, 20.0f),
            L"en-us", &h);
        app.dwriteFactory->CreateTextFormat(fam, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, dpi(app, 13.5f),
            L"en-us", &b);
        app.dwriteFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, dpi(app, 12.5f),
            L"en-us", &m);
        float sx = spec.left + dpi(app, 18.0f);
        float sw2 = spec.right - dpi(app, 18.0f) - sx;
        if (h) {
            app.brush->SetColor(work.heading);
            app.renderTarget->DrawText(L"The Quick Brown Fox", 19, h,
                D2D1::RectF(sx, spec.top + dpi(app, 14.0f), sx + sw2,
                            spec.top + dpi(app, 44.0f)), app.brush);
            h->Release();
        }
        if (b) {
            app.brush->SetColor(work.text);
            const wchar_t* body =
                L"Body text set in your chosen face, with enough of a line to judge its color and rhythm.";
            app.renderTarget->DrawText(body, (UINT32)wcslen(body), b,
                D2D1::RectF(sx, spec.top + dpi(app, 50.0f), sx + sw2,
                            spec.top + dpi(app, 92.0f)), app.brush);
            app.brush->SetColor(work.link);
            app.renderTarget->DrawText(L"A hyperlink,", 12, b,
                D2D1::RectF(sx, spec.top + dpi(app, 96.0f), sx + dpi(app, 90.0f),
                            spec.top + dpi(app, 118.0f)), app.brush);
            app.brush->SetColor(work.accent);
            app.renderTarget->DrawText(L"an accent,", 10, b,
                D2D1::RectF(sx + dpi(app, 92.0f), spec.top + dpi(app, 96.0f),
                            sx + dpi(app, 170.0f), spec.top + dpi(app, 118.0f)), app.brush);
            b->Release();
        }
        if (m) {
            D2D1_RECT_F pill = D2D1::RectF(sx, spec.top + dpi(app, 122.0f),
                                           sx + dpi(app, 128.0f), spec.top + dpi(app, 142.0f));
            app.brush->SetColor(work.codeBackground);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(pill, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush);
            app.brush->SetColor(work.code);
            app.renderTarget->DrawText(L"inline_code()", 13, m,
                D2D1::RectF(pill.left + dpi(app, 8.0f), pill.top + dpi(app, 2.0f),
                            pill.right, pill.bottom), app.brush);
            m->Release();
        }
    }

    // Font list: every system family, drawn in itself
    {
        D2D1_RECT_F list = D2D1::RectF(xr, py + dpi(app, 230.0f),
                                       xr + xrw, py + panelH - dpi(app, 60.0f));
        app.themeEditorFontListRect = list;
        D2D1_COLOR_F c = base.text; c.a = 0.2f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(list, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush, 1.0f);
        app.renderTarget->PushAxisAlignedClip(list, D2D1_ANTIALIAS_MODE_ALIASED);

        float rowH = dpi(app, 26.0f);
        float maxScroll = std::max(0.0f,
            (float)app.systemFontFamilies.size() * rowH - (list.bottom - list.top));
        app.themeEditorFontScroll = std::max(0.0f, std::min(app.themeEditorFontScroll, maxScroll));
        int first = (int)(app.themeEditorFontScroll / rowH);
        int visible = (int)((list.bottom - list.top) / rowH) + 2;
        for (int i = first; i < first + visible &&
                            i < (int)app.systemFontFamilies.size(); i++) {
            float ry = list.top + i * rowH - app.themeEditorFontScroll;
            const std::wstring& fam = app.systemFontFamilies[i];
            bool selected = (_wcsicmp(fam.c_str(), app.themeEditorFont.c_str()) == 0);
            if (selected) {
                D2D1_COLOR_F hl = base.accent; hl.a = 0.18f;
                app.brush->SetColor(hl);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(list.left + 1, ry, list.right - 1, ry + rowH), app.brush);
            }
            IDWriteTextFormat* ff = nullptr;
            app.dwriteFactory->CreateTextFormat(fam.c_str(), nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, dpi(app, 14.0f), L"en-us", &ff);
            D2D1_COLOR_F tc = base.text; tc.a = selected ? 1.0f : 0.8f;
            app.brush->SetColor(tc);
            if (ff) {
                app.renderTarget->DrawText(fam.c_str(), (UINT32)fam.size(), ff,
                    D2D1::RectF(list.left + dpi(app, 12.0f), ry + dpi(app, 3.0f),
                                list.right - dpi(app, 8.0f), ry + rowH), app.brush);
                ff->Release();
            }
            // Clamp the hit rect to the visible list: rows drawn under the
            // clip must not steal clicks from the buttons below
            D2D1_RECT_F hr = D2D1::RectF(list.left, std::max(ry, list.top),
                                         list.right, std::min(ry + rowH, list.bottom));
            if (hr.bottom > hr.top) {
                app.themeEditorHits.push_back({hr, TE_FONT_BASE + i});
            }
        }
        app.renderTarget->PopAxisAlignedClip();
    }

    // Save / Cancel
    {
        float bh = dpi(app, 30.0f);
        float by = py + panelH - dpi(app, 46.0f);
        const wchar_t* saveLabel = tr(app, "theme.editor.save");
        const wchar_t* cancelLabel = tr(app, "theme.editor.cancel");
        float saveW = std::max(dpi(app, 94.0f),
                               measureText(app, saveLabel, fmt) + dpi(app, 20.0f));
        float cancelW = std::max(dpi(app, 84.0f),
                                 measureText(app, cancelLabel, fmt) + dpi(app, 20.0f));
        float saveRight = px + panelW - dpi(app, 24.0f);
        D2D1_RECT_F save = D2D1::RectF(saveRight - saveW, by, saveRight, by + bh);
        D2D1_RECT_F cancel = D2D1::RectF(save.left - dpi(app, 12.0f) - cancelW, by,
                                         save.left - dpi(app, 12.0f), by + bh);
        app.brush->SetColor(base.accent);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(save, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
        D2D1_COLOR_F saveText = base.background;
        saveText.a = 1.0f;
        app.brush->SetColor(saveText);
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        app.renderTarget->DrawText(saveLabel, (UINT32)wcslen(saveLabel), fmt,
            D2D1::RectF(save.left, save.top + dpi(app, 6.0f), save.right, save.bottom),
            app.brush);
        D2D1_COLOR_F c = base.text; c.a = 0.4f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(cancel, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush, 1.0f);
        c.a = 0.9f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(cancelLabel, (UINT32)wcslen(cancelLabel), fmt,
            D2D1::RectF(cancel.left, cancel.top + dpi(app, 6.0f), cancel.right, cancel.bottom),
            app.brush);
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        app.themeEditorHits.push_back({save, TE_SAVE});
        app.themeEditorHits.push_back({cancel, TE_CANCEL});

        // themes.ini link, bottom-left: hand-tune what the editor exposes
        // and everything it does not (syntax palette, blockquote, code font)
        const wchar_t* ini = tr(app, "theme.editor.open_ini");
        D2D1_RECT_F iniR = D2D1::RectF(px + dpi(app, 24.0f), by + dpi(app, 6.0f),
                                       cancel.left - dpi(app, 18.0f), by + bh);
        c = base.link; c.a = 0.85f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(ini, (UINT32)wcslen(ini), fmt, iniR, app.brush);
        app.themeEditorHits.push_back({iniR, TE_OPEN_INI});
    }
}
