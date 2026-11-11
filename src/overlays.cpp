#include "search.h"
#include "frontmatter_ui.h"
#include "overlays.h"
#include "sidepanels.h"
#include "utils.h"
#include "d2d_init.h"
#include "editor.h"
#include "folding.h"
#include "markdown.h"
#include "pandoc.h"
#include "plantuml_app.h"
#include "print.h"
#include "settings.h"
#include "signals.h"
#include "i18n.h"
#include "ui_paint.h"
#include "ui_scale.h"

// Prompt-chip helpers defined with the dialogs below; the floating panels
// share their surface, shadow, and keycap language (t13)
static void promptChipShadow(App& app, const D2D1_RECT_F& r, float radius);
// promptChipSurface, promptKeycap and panelFooterText are shared with the
// search results panel through overlays.h

#include <chrono>
#include <algorithm>
#include <cmath>
#include <utility>

// Panel footers stay on one line even at the minimum panel width.
void panelFooterText(App& app, const wchar_t* text, const D2D1_RECT_F& rect) {
    if (!app.signalSmallFormat || rect.right <= rect.left || rect.bottom <= rect.top) return;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(app.dwriteFactory->CreateTextLayout(text, (UINT32)wcslen(text),
        app.signalSmallFormat, rect.right - rect.left, rect.bottom - rect.top, &layout))) return;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    IDWriteInlineObject* ellipsis = nullptr;
    if (SUCCEEDED(app.dwriteFactory->CreateEllipsisTrimmingSign(app.signalSmallFormat, &ellipsis))) {
        layout->SetTrimming(&trimming, ellipsis);
        ellipsis->Release();
    }
    app.renderTarget->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout,
                                    app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    layout->Release();
}

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
    D2D1_RECT_F rest = searchBarDrawRect(app);
    float barWidth = rest.right - rest.left;
    float barHeight = rest.bottom - rest.top;
    float barX = rest.left;
    float barY = rest.top;

    // Background with rounded corners
    D2D1_ROUNDED_RECT barRect = D2D1::RoundedRect(
        D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight),
        dpi(app, 8.0f), dpi(app, 8.0f));

    // Semi-transparent background based on theme
    const D2D1_COLOR_F barFill = themeUiPanel(app.theme, app.theme.isDark
        ? D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.95f * anim) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f * anim));
    app.brush->SetColor(barFill);
    app.renderTarget->FillRoundedRectangle(barRect, app.brush);

    // Border
    const D2D1_COLOR_F barBorder = themeUiBorder(app.theme, app.theme.isDark
        ? D2D1::ColorF(0.3f, 0.3f, 0.35f, 0.8f * anim) : D2D1::ColorF(0.7f, 0.7f, 0.75f, 0.8f * anim), anim);
    app.brush->SetColor(barBorder);
    app.renderTarget->DrawRoundedRectangle(barRect, app.brush, 1.0f);

    // Search icon (simple circle for magnifying glass look)
    {
        D2D1_COLOR_F iconColor = themeUiText(app.theme);
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
        size_t matchCount = app.editMode ? app.editorSearchMatches.size() : app.searchMatches.size();
        std::wstring countText;
        if (!app.searchQuery.empty()) {
            int currentIdx = app.editMode ? app.editorSearchCurrentIndex : app.searchCurrentIndex;
            if (matchCount == 0) countText = tr(app, "search.no_matches");
            else {
                wchar_t formatted[64];
                swprintf_s(formatted, tr(app, "search.match_count"), currentIdx + 1, matchCount);
                countText = formatted;
            }
        }
        float countWidth = measureText(app, countText, searchTextFormat);
        // The reader keeps the bar's right end for the results button (#246)
        float endPad = dpi(app, 14.0f) + (app.editMode ? 0.0f : dpi(app, 34.0f));
        float countX = barX + barWidth - countWidth - endPad;
        // A localized count must not paint over a long, scrolling query.
        bool showCount = !countText.empty() && countX-textX >= dpi(app, 70);
        float textRight = showCount ? countX-dpi(app, 10) : barX+barWidth-endPad;
        renderSearchField(app, 0,
            D2D1::RectF(textX, barY+dpi(app,12), std::max(textX+dpi(app,20),textRight), barY+dpi(app,32)),
            barRect.rect, tr(app, "search.placeholder"), anim);
        if (showCount) {
            auto countColor = matchCount ? themeUiText(app.theme) : D2D1::ColorF(0.9f, 0.3f, 0.3f);
            countColor.a = (matchCount ? 0.7f : 1.0f) * anim;
            app.brush->SetColor(countColor);
            app.renderTarget->DrawText(countText.data(), static_cast<UINT32>(countText.size()), searchTextFormat,
                D2D1::RectF(countX, barY + dpi(app, 12.0f), barX + barWidth - endPad + dpi(app, 4.0f), barY + barHeight), app.brush);
        }

    }

    // Results button: opens every match as a list beside the page (#246)
    if (!app.editMode) {
        D2D1_RECT_F button = searchResultsButtonRect(app);
        float slide = barY - rest.top;
        button.top += slide;
        button.bottom += slide;
        bool hover = (float)app.mouseX >= button.left && (float)app.mouseX <= button.right &&
                     (float)app.mouseY >= button.top && (float)app.mouseY <= button.bottom;
        if (app.showSearchPanel || hover) {
            D2D1_COLOR_F fill = app.showSearchPanel ? themeUiAccent(app.theme) : themeUiText(app.theme);
            fill.a = (app.showSearchPanel ? 0.16f : 0.07f) * anim;
            app.brush->SetColor(fill);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(button, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
        }
        D2D1_COLOR_F glyph = app.showSearchPanel ? themeUiAccent(app.theme) : themeUiText(app.theme);
        glyph.a = (app.showSearchPanel ? 1.0f : 0.55f) * anim;
        app.brush->SetColor(glyph);
        // A list: three rows, each a dot and a line
        float gx = button.left + dpi(app, 8.0f);
        float gy = (button.top + button.bottom) * 0.5f;
        for (int row = -1; row <= 1; row++) {
            float y = gy + dpi(app, 4.5f) * (float)row;
            app.renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(gx, y), dpi(app, 1.2f), dpi(app, 1.2f)),
                app.brush);
            app.renderTarget->DrawLine(D2D1::Point2F(gx + dpi(app, 3.5f), y),
                                       D2D1::Point2F(button.right - dpi(app, 7.0f), y),
                                       app.brush, dpi(app, 1.4f));
        }
    }

    // Replace row (#121): a second input under the bar plus Replace / All
    app.searchReplaceHits.clear();
    if (app.editMode && app.searchReplaceMode && searchTextFormat) {
        const D2D1_RECT_F row = searchReplaceRowRect(app);
        float rowY = row.top;
        bool stacked = barWidth < dpi(app, 390);
        float rowH = row.bottom - row.top;
        D2D1_ROUNDED_RECT rowRect = D2D1::RoundedRect(
            D2D1::RectF(barX, rowY, barX + barWidth, rowY + rowH),
            dpi(app, 8.0f), dpi(app, 8.0f));
        app.brush->SetColor(barFill);
        app.renderTarget->FillRoundedRectangle(rowRect, app.brush);
        app.brush->SetColor(barBorder);
        app.renderTarget->DrawRoundedRectangle(rowRect, app.brush, 1.0f);

        // Buttons on the right; Replace is the accent-filled primary
        const wchar_t* repLabel = tr(app, "search.replace");
        const wchar_t* allLabel = tr(app, "search.replace_all");
        float repW = measureText(app, repLabel, searchTextFormat) + dpi(app, 24.0f);
        float allW = measureText(app, allLabel, searchTextFormat) + dpi(app, 24.0f);
        float chipH = dpi(app, 28.0f);
        float chipY = rowY + dpi(app, stacked ? 50.0f : 8.0f);
        float allX = barX + barWidth - dpi(app, 10.0f) - allW;
        float repX = allX - dpi(app, 8.0f) - repW;
        auto chip = [&](float cx, float cw, const wchar_t* label, int id,
                        bool primary) {
            D2D1_ROUNDED_RECT cr = D2D1::RoundedRect(
                D2D1::RectF(cx, chipY, cx + cw, chipY + chipH),
                dpi(app, 5.0f), dpi(app, 5.0f));
            D2D1_COLOR_F c;
            if (primary) {
                c = themeUiAccent(app.theme); c.a = anim;
                app.brush->SetColor(c);
                app.renderTarget->FillRoundedRectangle(cr, app.brush);
                app.brush->SetColor(D2D1::ColorF(1, 1, 1, anim));
            } else {
                c = themeUiText(app.theme); c.a = 0.3f * anim;
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
        D2D1_COLOR_F iconColor = themeUiText(app.theme);
        iconColor.a = 0.5f * anim;
        app.brush->SetColor(iconColor);
        float ix = barX + dpi(app, 20.0f);
        float iy = rowY + dpi(app, 22);
        app.renderTarget->DrawLine(
            D2D1::Point2F(ix, iy - dpi(app, 7.0f)),
            D2D1::Point2F(ix, iy + dpi(app, 3.0f)), app.brush, dpi(app, 2.0f));
        app.renderTarget->DrawLine(
            D2D1::Point2F(ix, iy + dpi(app, 3.0f)),
            D2D1::Point2F(ix + dpi(app, 8.0f), iy + dpi(app, 3.0f)),
            app.brush, dpi(app, 2.0f));

        // Replace text (or placeholder), left of the buttons
        float textX = barX + dpi(app, 42.0f);
        float textRight = stacked ? barX+barWidth-dpi(app,14) : repX-dpi(app,12);
        renderSearchField(app, 1,
            D2D1::RectF(textX, rowY+dpi(app,12), textRight, rowY+dpi(app,32)),
            rowRect.rect, tr(app, "search.replace_placeholder"), anim);

        if (app.searchActive) {
            auto focus = themeUiAccent(app.theme);
            focus.a = 0.9f * anim;
            app.brush->SetColor(focus);
            app.renderTarget->DrawRoundedRectangle(app.replaceFieldActive ? rowRect : barRect, app.brush, 1.5f);
        }

        // Field rects come after the buttons so button clicks win
        app.searchReplaceHits.push_back(
            {D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight), 1});
        app.searchReplaceHits.push_back(
            {D2D1::RectF(barX, rowY, barX + barWidth, rowY + rowH), 2});
    }
}

// One source of truth for the docked browser panel's geometry:
// render, cursor, and click hit-tests all read from here.
FolderBrowserMetrics folderBrowserMetrics(const App& app) {
    FolderBrowserMetrics g;
    g.panelWidth = folderBrowserPanelWidth(app);
    g.panelX = -g.panelWidth * (1.0f - app.folderBrowserAnimation);
    g.cardLeft = g.panelX;
    g.cardRight = g.panelX + g.panelWidth;
    g.cardTop = chromeTopHeight(app);
    g.cardBottom = (float)app.height;
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

float folderBrowserContentHeight(const App& app, const FolderBrowserMetrics& g) {
    return g.namingOffset +
           (app.folderItems.empty()
                ? 0.0f
                : folderItemContentY(g, (int)app.folderItems.size() - 1) + g.itemHeight);
}

float folderBrowserMaxScroll(const App& app) {
    FolderBrowserMetrics g = folderBrowserMetrics(app);
    return std::max(0.0f, folderBrowserContentHeight(app, g) - (g.listBottom - g.listStartY));
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

    // A docked surface shares one divider with the neighbouring pane.
    FolderBrowserMetrics g = folderBrowserMetrics(app);
    D2D1_RECT_F card = D2D1::RectF(g.cardLeft, g.cardTop, g.cardRight, g.cardBottom);
    D2D1_COLOR_F panelBg = promptChipSurface(app);
    panelBg.a = 1;
    app.brush->SetColor(panelBg);
    app.renderTarget->FillRectangle(card, app.brush);

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

        D2D1_COLOR_F accentColor = themeUiAccent(app.theme);
        accentColor.a = anim;
        D2D1_COLOR_F errorColor = hexColor(themeUiDark(app.theme) ? 0xF85149 : 0xCF222E, anim);
        float inputPad = dpi(app, 6.0f);
        float boxHeight = dpi(app, 26.0f);

        // Draws a single-line input box; the text is clipped from the left so
        // its tail (where typing happens) stays visible, with select-all
        // highlight and a caret at the end
        auto drawInputBox = [&](float boxX, float boxRight, float boxY) {
            D2D1_RECT_F box = D2D1::RectF(boxX, boxY, boxRight, boxY + boxHeight);
            D2D1_COLOR_F boxBg = themeUiPanel(app.theme, app.theme.isDark ? hexColor(0x2A2A30, 0.9f * anim)
                                                                          : hexColor(0xFFFFFF, 0.9f * anim));
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
                D2D1_COLOR_F selColor = themeUiAccent(app.theme);
                selColor.a = 0.35f * anim;
                app.brush->SetColor(selColor);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(boxX + inputPad - 1, boxY + dpi(app, 3.0f),
                                boxX + inputPad + shownWidth + 1, boxY + boxHeight - dpi(app, 3.0f)),
                    app.brush);
            }

            D2D1_COLOR_F inputTextColor = themeUiText(app.theme);
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
            D2D1_COLOR_F sepColor = themeUiText(app.theme); sepColor.a = 0.35f * anim;
            for (size_t ci = 0; ci < crumbs.size(); ci++) {
                std::wstring seg = crumbs[ci].text;
                float segW = textWidth(seg);
                // The leaf keeps at least a readable stub; middle crumbs
                // give way when the row runs out
                if (cx + segW > crumbMax) {
                    // Trim with an ellipsis; the leaf keeps a readable stub
                    while (!seg.empty() &&
                           cx + textWidth(seg + L"\u2026") > crumbMax) {
                        seg.pop_back();
                    }
                    if (cx + textWidth(seg + L"\u2026") > crumbMax) {
                        break;
                    }
                    seg += L"\u2026";
                    segW = textWidth(seg);
                }
                bool leaf = crumbs[ci].leaf;
                D2D1_COLOR_F segColor = leaf ? themeUiHeading(app.theme) : themeUiText(app.theme);
                segColor.a = (leaf ? 1.0f : 0.6f) * anim;
                app.brush->SetColor(segColor);
                app.renderTarget->DrawText(seg.c_str(), (UINT32)seg.size(),
                    leaf && app.tocFormatBold ? app.tocFormatBold : browserFormat,
                    D2D1::RectF(cx, headerCenterY - dpi(app, 9.0f),
                                std::min(crumbMax, cx + segW + dpi(app, 4.0f)), headerY + g.headerH),
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
                D2D1_COLOR_F pc = themeUiText(app.theme); pc.a = 0.45f * anim;
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
                    D2D1_COLOR_F bg = themeUiAccent(app.theme);
                    bg.a = (naming ? 0.16f : 0.1f) * anim;
                    app.brush->SetColor(bg);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(bx, btnY, bx + btnSize, btnY + btnSize),
                                          dpi(app, 5.0f), dpi(app, 5.0f)),
                        app.brush);
                    if (naming) {
                        D2D1_COLOR_F ring = themeUiAccent(app.theme); ring.a = 0.4f * anim;
                        app.brush->SetColor(ring);
                        app.renderTarget->DrawRoundedRectangle(
                            D2D1::RoundedRect(D2D1::RectF(bx, btnY, bx + btnSize, btnY + btnSize),
                                              dpi(app, 5.0f), dpi(app, 5.0f)),
                            app.brush, 1.0f);
                    }
                }
                D2D1_COLOR_F glyphColor;
                if (naming) {
                    glyphColor = themeUiAccent(app.theme); glyphColor.a = anim;
                } else {
                    glyphColor = themeUiText(app.theme); glyphColor.a = 0.6f * anim;
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
                    D2D1_COLOR_F bg = themeUiAccent(app.theme);
                    bg.a = (app.browserPinned ? 0.16f : 0.1f) * anim;
                    app.brush->SetColor(bg);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(
                            D2D1::RectF(g.pinBtnX, btnY, g.pinBtnX + btnSize,
                                        btnY + btnSize),
                            dpi(app, 5.0f), dpi(app, 5.0f)),
                        app.brush);
                }
                D2D1_COLOR_F pc = app.browserPinned ? themeUiAccent(app.theme)
                                                    : themeUiText(app.theme);
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
        D2D1_COLOR_F div = themeUiText(app.theme); div.a = 0.1f;
        div = themeUiBorder(app.theme, div);
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
        float totalItemsHeight = folderBrowserContentHeight(app, g);

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
            D2D1_COLOR_F lc = themeUiText(app.theme); lc.a = 0.45f * anim;
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
                D2D1_COLOR_F curColor = themeUiAccent(app.theme);
                curColor.a = 0.14f * anim;
                app.brush->SetColor(curColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(rowRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                    app.brush);
                D2D1_COLOR_F ring = themeUiAccent(app.theme);
                ring.a = 0.4f * anim;
                app.brush->SetColor(ring);
                app.renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(rowRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                    app.brush, 1.0f);
            }
            if (isHovered) {
                app.hoveredFolderIndex = (int)i;
                D2D1_COLOR_F hoverColor = themeUiText(app.theme);
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
                glyphColor = themeUiAccent(app.theme);
                glyphColor.a = (item.isDirectory ? 0.85f : 0.9f) * anim;
            } else {
                glyphColor = themeUiText(app.theme);
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
                textColor = themeUiHeading(app.theme);
                textColor.a = anim;
            } else if (!item.isDirectory && !isMd) {
                textColor = themeUiText(app.theme);
                textColor.a = 0.5f * anim;
            } else {
                textColor = themeUiText(app.theme);
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

            D2D1_COLOR_F glyphColor = themeUiAccent(app.theme);
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
        renderPanelScrollbar(app, PanelList::Browser, anim);

        // Footer: counts on the left, the key hints on the right
        float footTop = g.listBottom;
        app.brush->SetColor(div);
        app.renderTarget->FillRectangle(
            D2D1::RectF(g.cardLeft + padding, footTop, g.cardRight - padding,
                        footTop + 1.0f),
            app.brush);
        if (app.signalSmallFormat) {
            D2D1_COLOR_F hc = themeUiText(app.theme); hc.a = 0.48f * anim;
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
            panelFooterText(app, counts,
                D2D1::RectF(g.cardLeft + padding, footTop + dpi(app, 7.0f),
                            g.cardRight - padding, g.cardBottom));
            const wchar_t* hint = tr(app, app.folderBrowserNaming != 0
                                              ? "browser.naming_hint"
                                              : "browser.hint");
            float hw = measureText(app, hint, app.signalSmallFormat);
            if (measureText(app, counts, app.signalSmallFormat) + hw + dpi(app, 12.0f) <
                g.cardRight - g.cardLeft - padding * 2) {
                app.renderTarget->DrawText(hint, (UINT32)wcslen(hint),
                    app.signalSmallFormat,
                    D2D1::RectF(g.cardRight - padding - hw, footTop + dpi(app, 7.0f),
                                g.cardRight - padding + dpi(app, 2.0f), g.cardBottom),
                    app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
    }
    renderSidePanelResizeGrip(app, SidePanel::Browser);
}

D2D1_RECT_F tocListRect(const App& app) {
    const float width = tocPanelWidth(app);
    const float x = tocPanelX(app, width);
    return D2D1::RectF(x + dpi(app, 14.0f), chromeTopHeight(app) + dpi(app, 43.0f),
                       x + width - dpi(app, 8.0f), app.height - dpi(app, 26.0f));
}

float tocHeadingIndent(const App& app, int level) {
    const float available = std::max(0.0f, tocPanelWidth(app) - dpi(app, 52.0f));
    const float step = std::min(dpi(app, 13.0f), available * 0.35f / 5);
    return std::clamp(level - 1, 0, 5) * step;
}

float tocListContentHeight(const App& app) {
    const auto needle = toLower(app.tocFilter);
    size_t count = 0;
    for (const auto& heading : app.headings)
        if (needle.empty() || toLower(heading.text).find(needle) != std::wstring::npos) ++count;
    return count * dpi(app, 26.0f);
}

float tocMaxScroll(const App& app) {
    const auto rect = tocListRect(app);
    return std::max(0.0f, tocListContentHeight(app) - std::max(0.0f, rect.bottom - rect.top));
}

// Click hit-test sharing the renderer's card geometry (#114 pattern:
// render-time hover must never drive click actions). Keep in step with
// renderToc below.
int tocItemIndexAt(App& app, float x, float y) {
    const auto rect = tocListRect(app);
    float itemHeight = dpi(app, 26.0f);
    if (x < rect.left || x > rect.right || y < rect.top || y > rect.bottom)
        return -1;
    int row = (int)((y - rect.top + app.tocScroll) / itemHeight);
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

    // Dock the outline directly against the document, without a second border.
    float panelWidth = tocPanelWidth(app);
    float panelX = tocPanelX(app, panelWidth);  // slides from the chosen side
    float cardLeft = panelX;
    float cardRight = panelX + panelWidth;
    float cardTop = chromeTopHeight(app);
    float cardBottom = (float)app.height;
    D2D1_RECT_F card = D2D1::RectF(cardLeft, cardTop, cardRight, cardBottom);

    D2D1_COLOR_F surf = promptChipSurface(app);
    surf.a = 1;
    app.brush->SetColor(surf);
    app.renderTarget->FillRectangle(card, app.brush);

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
        D2D1_COLOR_F headerColor = themeUiHeading(app.theme);
        headerColor.a = anim;
        app.brush->SetColor(headerColor);
        const wchar_t* tocTitle = tr(app, "toc.title");
        const float titleRight = cardRight - padding - dpi(app, 40.0f);
        if (app.tocFilter.empty()) {
            app.renderTarget->DrawText(tocTitle, (UINT32)wcslen(tocTitle), tocBold,
                D2D1::RectF(cardLeft + padding, headerY, titleRight, headerY + headerHeight),
                app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        float titleW = measureText(app, tocTitle, tocBold);

        if (app.tocFilter.empty() && !app.headings.empty() && app.signalSmallFormat) {
            wchar_t count[8];
            swprintf_s(count, L"%d", (int)app.headings.size());
            float cw = measureText(app, count, app.signalSmallFormat);
            D2D1_RECT_F badge = D2D1::RectF(
                cardLeft + padding + titleW + dpi(app, 8.0f),
                headerY + dpi(app, 2.0f),
                cardLeft + padding + titleW + dpi(app, 8.0f) + cw + dpi(app, 11.0f),
                headerY + dpi(app, 17.0f));
            if (badge.right <= titleRight) {
                D2D1_COLOR_F bb = themeUiText(app.theme); bb.a = 0.07f * anim;
                app.brush->SetColor(bb);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(badge, dpi(app, 7.0f), dpi(app, 7.0f)),
                    app.brush);
                D2D1_COLOR_F bi = themeUiText(app.theme); bi.a = 0.6f * anim;
                app.brush->SetColor(bi);
                app.renderTarget->DrawText(count, (UINT32)wcslen(count),
                    app.signalSmallFormat,
                    D2D1::RectF(badge.left + dpi(app, 5.5f), badge.top + dpi(app, 1.5f),
                                badge.right, badge.bottom),
                    app.brush);
            }
        }

        // Close cross at the right edge of the header
        {
            float ccx = cardRight - padding - dpi(app, 4.0f);
            float ccy = headerY + dpi(app, 9.0f);
            D2D1_COLOR_F c = themeUiText(app.theme); c.a = 0.45f * anim;
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
            D2D1_COLOR_F pc = app.tocPinned ? themeUiAccent(app.theme) : themeUiText(app.theme);
            pc.a = (app.tocPinned ? 0.95f : 0.45f) * anim;
            drawPinGlyph(app, pcx, ccy, pc);
            app.tocPinRect = D2D1::RectF(pcx - dpi(app, 9.0f), cardTop,
                                         pcx + dpi(app, 9.0f),
                                         headerY + dpi(app, 20.0f));
        }

        // The active filter replaces the title, leaving room for the buttons.
        if (!app.tocFilter.empty()) {
            D2D1_COLOR_F filterColor = themeUiAccent(app.theme);
            filterColor.a = anim;
            app.brush->SetColor(filterColor);
            const std::wstring label = L"/ " + app.tocFilter;
            app.renderTarget->DrawText(label.c_str(), (UINT32)label.size(), tocNormal,
                D2D1::RectF(cardLeft + padding, headerY + dpi(app, 1.0f),
                            titleRight, headerY + headerHeight),
                app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        // Divider
        float dividerY = headerY + headerHeight - dpi(app, 8.0f);
        D2D1_COLOR_F div = themeUiText(app.theme); div.a = 0.1f;
        div = themeUiBorder(app.theme, div);
        app.brush->SetColor(div);
        app.renderTarget->FillRectangle(
            D2D1::RectF(cardLeft + padding, dividerY, cardRight - padding,
                        dividerY + 1.0f),
            app.brush);

        // Items list between header and footer
        const auto listRect = tocListRect(app);
        float listStartY = listRect.top;
        float listBottom = listRect.bottom;
        float listHeight = listBottom - listStartY;

        if (app.headings.empty()) {
            // "No headings" message
            D2D1_COLOR_F dimColor = themeUiText(app.theme);
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
                    // One in a folded section shares the folded heading's place (#316)
                    if (app.headings[i].hiddenUnder < 0 && app.headings[i].y <= spyLine) currentIdx = (int)i;
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
                D2D1_COLOR_F dimColor = themeUiText(app.theme);
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

            float rowLeft = listRect.left;
            float rowRight = listRect.right;
            app.renderTarget->PushAxisAlignedClip(
                D2D1::RectF(cardLeft, listStartY, cardRight, listBottom),
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            for (size_t row = 0; row < visible.size(); row++) {
                float itemY = listStartY + row * itemHeight - app.tocScroll;

                // Skip items outside visible area
                if (itemY + itemHeight < listStartY || itemY > listBottom) continue;

                const auto& heading = app.headings[visible[row]];
                float indent = tocHeadingIndent(app, heading.level);
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
                    D2D1_COLOR_F hoverColor = themeUiText(app.theme);
                    hoverColor.a = 0.06f * anim;
                    app.brush->SetColor(hoverColor);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(rowRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                        app.brush);
                } else if (isCurrent) {
                    // Scroll-spy pill: the accent-washed active row
                    D2D1_COLOR_F spyColor = themeUiAccent(app.theme);
                    spyColor.a = 0.13f * anim;
                    app.brush->SetColor(spyColor);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(rowRect, dpi(app, 6.0f), dpi(app, 6.0f)),
                        app.brush);
                }
                if (isCurrent) {
                    D2D1_COLOR_F barColor = themeUiAccent(app.theme);
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

                // H1 stays bold; deeper levels remain legible at compact widths.
                IDWriteTextFormat* fmt =
                    (heading.level == 1 || isCurrent) ? tocBold : tocNormal;
                D2D1_COLOR_F textColor;
                if (heading.level == 1 || isCurrent) {
                    textColor = themeUiHeading(app.theme);
                    textColor.a = anim;
                } else if (heading.level >= 3) {
                    textColor = themeUiText(app.theme);
                    textColor.a = 0.72f * anim;
                } else {
                    textColor = themeUiText(app.theme);
                    textColor.a = 0.82f * anim;
                }
                // In a folded section: fainter, and a click opens it (#316)
                if (heading.hiddenUnder >= 0) textColor.a *= 0.55f;
                app.brush->SetColor(textColor);
                app.renderTarget->DrawText(heading.text.c_str(),
                    (UINT32)heading.text.length(), fmt,
                    D2D1::RectF(itemX, itemY + dpi(app, 4.0f),
                                rowRight - dpi(app, 16.0f), itemY + itemHeight),
                    app.brush);

                if (isHovered) {
                    D2D1_COLOR_F arr = themeUiText(app.theme); arr.a = 0.45f * anim;
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
            renderPanelScrollbar(app, PanelList::Contents, anim);
        }

        // Footer: the hint plus the active Contents shortcut
        float footTop = listBottom;
        app.brush->SetColor(div);
        app.renderTarget->FillRectangle(
            D2D1::RectF(cardLeft + padding, footTop, cardRight - padding,
                        footTop + 1.0f),
            app.brush);
        if (app.signalSmallFormat) {
            const wchar_t* hint = tr(app, "toc.hint");
            const std::wstring shortcut = keyLabel(app.keymap[KA_TOC]);
            const float keyWidth = promptKeycap(app, shortcut.c_str(), cardRight - padding,
                                               footTop + footerHeight * 0.5f);
            D2D1_COLOR_F hc = themeUiText(app.theme); hc.a = 0.48f * anim;
            app.brush->SetColor(hc);
            panelFooterText(app, hint,
                D2D1::RectF(cardLeft + padding, footTop + dpi(app, 7.0f),
                            cardRight - padding - keyWidth, cardBottom));
        }
    }
    renderSidePanelResizeGrip(app, SidePanel::Contents);
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

    D2D1_COLOR_F bg = themeUiPanel(app.theme, app.theme.isDark ? hexColor(0x1E1E1E, 1.0f)
                                                               : hexColor(0xFCFCFA, 1.0f));
    app.brush->SetColor(bg);
    app.renderTarget->FillRectangle(p, app.brush);

    // Title strip: the target's file name
    D2D1_COLOR_F titleColor = themeUiAccent(app.theme);
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

    D2D1_COLOR_F border = themeUiAccent(app.theme);
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

// --- The theme chooser on the interface's scale (ui_scale.h) ------------
//
// Two columns of cards, light and dark, each card a specimen of its theme
// in the theme's own faces and colours. A card has one size: with more
// themes than the dialog has room for, the columns scroll between the
// header and the footer (they shrank the cards until the names lay over
// the samples).
namespace {
constexpr float kChooserCard = 72.0f;      // sp8, the name, sp2, the sample, sp4, the swatches, sp8
constexpr float kChooserHeader = 28.0f + uiscale::sp16;   // the title's line and its gap
constexpr float kChooserLabel = 16.0f + uiscale::sp8;     // a column's label and its gap
constexpr float kChooserFooter = uiscale::sp16 + 16.0f;   // gap and the footer's line
constexpr float kChooserPitch = kChooserCard + uiscale::sp8;

struct ChooserLayout {
    D2D1_RECT_F panel{}, view{};
    float cardW = 0.0f, colX[2]{}, gridH = 0.0f;
};
ChooserLayout chooserLayout(const App& app, float anim) {
    using namespace uiscale;
    ChooserLayout g;
    const int rows = themeChooserRows();
    const float pad = dpi(app, sp24);
    const float w = std::min(dpi(app, 864.0f), app.width - dpi(app, 64.0f));
    g.gridH = dpi(app, rows * kChooserCard + (rows - 1) * sp8);
    const float around = pad * 2.0f + dpi(app, kChooserHeader + kChooserLabel + kChooserFooter);
    // At least a card and a half of room, however short the window
    const float h = std::max(around + dpi(app, kChooserCard * 1.5f),
                             std::min(around + g.gridH, app.height - dpi(app, 64.0f)));
    const float x = (app.width - w) / 2;
    const float y = (app.height - h) / 2 + (1 - anim) * dpi(app, 40.0f);
    g.panel = D2D1::RectF(x, y, x + w, y + h);
    g.cardW = (w - pad * 2.0f - dpi(app, sp16)) / 2.0f;
    g.colX[0] = x + pad;
    g.colX[1] = x + pad + g.cardW + dpi(app, sp16);
    const float top = y + pad + dpi(app, kChooserHeader + kChooserLabel);
    g.view = D2D1::RectF(x + pad, top, x + w - pad, y + h - pad - dpi(app, kChooserFooter));
    return g;
}
}  // namespace

bool themeChooserWheel(App& app, float notches) {
    if (!app.showThemeChooser) return false;
    const float before = app.themeChooserScroll;
    app.themeChooserScroll = std::clamp(app.themeChooserScroll - notches * dpi(app, kChooserPitch), 0.0f,
                                        std::max(0.0f, app.themeChooserScrollMax));
    if (app.themeChooserScroll != before) {
        // Another card is under the pointer now: the theme it was showing
        // goes, and the next move of the mouse shows the one that is there
        if (app.themeChooserPreviewBase >= 0) {
            applyTheme(app, app.themeChooserPreviewBase);
            app.themeChooserPreviewBase = -1;
        }
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    return true;
}

void renderThemeChooser(App& app) {
    using namespace uiscale;
    // Preview formats are created lazily on first open (not at startup)
    ensureThemePreviewFormats(app);
    app.themeChooserHits.clear();

    // Animate in - only invalidate while progressing
    const bool opening = app.themeChooserAnimation <= 0.0f;
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
    const int committed = app.themeChooserPreviewBase >= 0 ? app.themeChooserPreviewBase
                                                           : app.currentThemeIndex;
    const D2DTheme& chrome = themeAt(committed);

    // Scrim: dim the live app but keep it recognizable for hover preview
    float backdropAlpha = (chrome.isDark ? 0.45f : 0.3f) * anim;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    const ChooserLayout g = chooserLayout(app, anim);
    const float pad = dpi(app, sp24);
    const float panelX = g.panel.left, panelY = g.panel.top;
    const float panelWidth = g.panel.right - g.panel.left;
    const float panelHeight = g.panel.bottom - g.panel.top;
    app.themeChooserPanel = g.panel;
    app.themeChooserViewport = g.view;
    const float room = g.view.bottom - g.view.top;
    app.themeChooserScrollMax = std::max(0.0f, g.gridH - room);
    if (opening) {
        // Opened anew: the theme in use is in sight
        int col = 0, row = 0;
        themeChooserCell(committed, col, row);
        const float top = row * dpi(app, kChooserPitch);
        app.themeChooserScroll = top + dpi(app, kChooserCard) > room ? top + dpi(app, kChooserCard) - room : 0.0f;
    }
    app.themeChooserScroll = std::clamp(app.themeChooserScroll, 0.0f, app.themeChooserScrollMax);

    // Panel: the active theme's strip color as an acrylic-ish sheet
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(g.panel, dpi(app, radiusDialog), dpi(app, radiusDialog));
    {
        // Soft drop shadow
        app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.35f * anim));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(panelX + dpi(app, sp2), panelY + dpi(app, sp4),
                            panelX + panelWidth + dpi(app, sp2),
                            panelY + panelHeight + dpi(app, sp8)),
                dpi(app, radiusDialog), dpi(app, radiusDialog)),
            app.brush);
        // The committed theme's strip color, even while previewing; its
        // --ui-panel when a CSS theme gives one
        D2D1_COLOR_F cb = chrome.background;
        float f = chrome.isDark ? 0.78f : 0.955f;
        D2D1_COLOR_F panelColor =
            D2D1::ColorF(cb.r * f, cb.g * f, cb.b * f, 0.96f * anim);
        if (chrome.uiBackground) {
            panelColor = *chrome.uiBackground;
            panelColor.a *= 0.96f * anim;
        }
        panelColor = themeUiPanel(chrome, panelColor);
        app.brush->SetColor(panelColor);
        app.renderTarget->FillRoundedRectangle(panelRect, app.brush);
        app.brush->SetColor(themeUiBorder(chrome, paint::ink(chrome, 0.14f, anim), anim));
        app.renderTarget->DrawRoundedRectangle(panelRect, app.brush, 1.0f);
    }

    // Header: title, key hint, follow toggle, close
    const float hy = panelY + pad;
    const float titleLine = dpi(app, lineHeight(Type::Title));
    const D2D1_RECT_F closeBox = D2D1::RectF(g.panel.right - dpi(app, sp16 + controlBox), hy,
                                             g.panel.right - dpi(app, sp16), hy + dpi(app, controlBox));
    {
        const wchar_t* title = tr(app, "theme.chooser.title");
        D2D1_COLOR_F titleColor = themeUiHeading(chrome);
        titleColor.a = anim;
        const float titleW = paint::labelWidth(app, title, Type::Title);
        paint::label(app, title, Type::Title,
                     D2D1::RectF(panelX + pad, hy, panelX + panelWidth * 0.5f, hy + titleLine), titleColor);
        // Key hint beside the title, built from the live keymap
        std::wstring hint = keyLabel(app.keymap[KA_THEME]) + L" / Esc";
        const float hintX = panelX + pad + titleW + dpi(app, sp12);
        paint::label(app, hint.c_str(), Type::Small,
                     D2D1::RectF(hintX, paint::lineTop(app, hy, titleLine, Type::Small) + dpi(app, sp2),
                                 panelX + panelWidth * 0.5f, hy + titleLine),
                     paint::ink(chrome, inkSecondary, anim));
    }
    {
        const bool hover = app.mouseX >= closeBox.left && app.mouseX <= closeBox.right &&
                           app.mouseY >= closeBox.top && app.mouseY <= closeBox.bottom;
        if (hover) {
            app.brush->SetColor(paint::ink(chrome, 0.10f, anim));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(closeBox, dpi(app, radiusControl), dpi(app, radiusControl)), app.brush);
        }
        paint::closeCross(app, closeBox, paint::ink(chrome, hover ? 1.0f : inkSecondary, anim));
        app.themeChooserHits.push_back({closeBox, -2});
    }
    {
        // "Follow Windows theme": its name and the switch, left of the close cross
        const D2D1_RECT_F slot = D2D1::RectF(closeBox.left - dpi(app, sp16 + 40.0f), closeBox.top,
                                             closeBox.left - dpi(app, sp16), closeBox.bottom);
        const D2D1_RECT_F sw = paint::toggle(app, slot, app.followSystemTheme, chrome, anim);
        const wchar_t* follow = tr(app, "settings.follow_windows");
        const float left = std::max(panelX + panelWidth * 0.5f,
                                    sw.left - dpi(app, sp12) - paint::labelWidth(app, follow, Type::Body));
        paint::label(app, follow, Type::Body,
                     D2D1::RectF(left, paint::lineTop(app, slot.top, slot.bottom - slot.top, Type::Body),
                                 sw.left - dpi(app, sp12), slot.bottom),
                     paint::ink(chrome, inkSecondary, anim), DWRITE_TEXT_ALIGNMENT_TRAILING);
        app.themeChooserHits.push_back({D2D1::RectF(left, slot.top, sw.right, slot.bottom), -1});
    }

    // The columns' labels, on the cards' own text edge
    const float cardPad = dpi(app, sp12);
    const float labelY = panelY + pad + dpi(app, kChooserHeader);
    paint::sectionLabel(app, tr(app, "theme.chooser.light"), g.colX[0] + cardPad, labelY, g.cardW - cardPad,
                        paint::ink(chrome, inkSecondary, anim));
    paint::sectionLabel(app, tr(app, "theme.chooser.dark"), g.colX[1] + cardPad, labelY, g.cardW - cardPad,
                        paint::ink(chrome, inkSecondary, anim));

    app.hoveredThemeIndex = -1;
    const float cardH = dpi(app, kChooserCard);
    // The shorter column stops with its last card at the foot of the room
    // while the longer one scrolls on: no half of the dialog goes empty
    int colCards[2] = {0, 0};
    for (int i = 0; i < themeCount(); i++) colCards[themeAt(i).isDark ? 1 : 0]++;
    float colScroll[2];
    for (int c = 0; c < 2; c++) {
        const float colH = dpi(app, colCards[c] * kChooserCard + std::max(0, colCards[c] - 1) * sp8);
        colScroll[c] = std::min(app.themeChooserScroll, std::max(0.0f, colH - room));
    }
    const bool pointerInView = app.mouseY >= g.view.top && app.mouseY <= g.view.bottom;
    // A little past the cards' room, for the ring around the chosen one
    const float halo = dpi(app, sp4) + 1.0f;
    app.renderTarget->PushAxisAlignedClip(
        D2D1::RectF(g.view.left - halo, g.view.top - halo, g.view.right + halo, g.view.bottom + halo),
        D2D1_ANTIALIAS_MODE_ALIASED);

    for (int i = 0; i < themeCount(); i++) {
        const D2DTheme& t = themeAt(i);
        int col, row;
        themeChooserCell(i, col, row);
        float x = g.colX[col];
        float y = g.view.top + row * dpi(app, kChooserPitch) - colScroll[col];
        D2D1_RECT_F card = D2D1::RectF(x, y, x + g.cardW, y + cardH);
        if (card.bottom < g.view.top - halo || card.top > g.view.bottom + halo) continue;
        D2D1_ROUNDED_RECT cardRect =
            D2D1::RoundedRect(card, dpi(app, radiusCard), dpi(app, radiusCard));

        bool isHovered = pointerInView &&
                         (app.mouseX >= card.left && app.mouseX <= card.right &&
                          app.mouseY >= card.top && app.mouseY <= card.bottom);
        // The ring and check stay on the COMMITTED theme while a hover
        // preview repaints the app behind (design 12b)
        bool isSelected = i == committed;
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
                    D2D1::RectF(card.left - dpi(app, sp2), card.top - dpi(app, sp2),
                                card.right + dpi(app, sp2), card.bottom + dpi(app, sp2)),
                    dpi(app, radiusCard + sp2), dpi(app, radiusCard + sp2)),
                app.brush, dpi(app, sp2));
            D2D1_COLOR_F ring = t.accent;
            ring.a = anim;
            app.brush->SetColor(ring);
            app.renderTarget->DrawRoundedRectangle(cardRect, app.brush, dpi(app, 1.5f));
        } else {
            app.brush->SetColor(paint::ink(chrome, isHovered ? 0.4f : 0.16f, anim));
            app.renderTarget->DrawRoundedRectangle(cardRect, app.brush, 1.0f);
        }

        const float cx = x + cardPad;
        const float nameY = y + dpi(app, sp8);
        const float sampleY = nameY + dpi(app, 18.0f + sp2);
        const float swatchY = sampleY + dpi(app, 16.0f + sp4);
        const bool known = i < (int)app.themePreviewFormats.size();
        IDWriteTextFormat* nameFormat = known ? app.themePreviewFormats[i].name : nullptr;
        IDWriteTextFormat* previewFormat = known ? app.themePreviewFormats[i].preview : nullptr;
        IDWriteTextFormat* codeFormat = known ? app.themePreviewFormats[i].code : nullptr;

        // Row 1: name in the theme's heading voice + accent dash (+ pin)
        float nameW = 0.0f;
        if (nameFormat) {
            nameFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            nameFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            D2D1_COLOR_F nameColor = t.heading;
            nameColor.a = anim;
            app.brush->SetColor(nameColor);
            // Room for the dash, the pin and the check at the right
            const float nameRight = x + g.cardW - dpi(app, sp32 + sp48 + sp24);
            app.renderTarget->DrawText(
                t.name, (UINT32)wcslen(t.name), nameFormat,
                D2D1::RectF(cx, nameY, nameRight, nameY + dpi(app, 18.0f)),
                app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            nameW = std::min(measureText(app, t.name, nameFormat), nameRight - cx);
        }
        {
            D2D1_COLOR_F dash = t.accent;
            dash.a = anim;
            app.brush->SetColor(dash);
            float dx = cx + nameW + dpi(app, sp8);
            float dy = nameY + dpi(app, 8.0f);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(dx, dy, dx + dpi(app, sp24), dy + dpi(app, 3.0f)),
                                  dpi(app, 1.5f), dpi(app, 1.5f)),
                app.brush);
            // Default pin while follow-system is on: sun / moon on the
            // card the OS switch would pick for its side
            bool pinned = app.followSystemTheme &&
                          ((t.isDark && i == app.darkThemeIndex) ||
                           (!t.isDark && i == app.lightThemeIndex));
            if (pinned) {
                float px = dx + dpi(app, sp32);
                D2D1_RECT_F pin = D2D1::RectF(px, nameY + dpi(app, 1.0f), px + dpi(app, sp24),
                                              nameY + dpi(app, 17.0f));
                D2D1_COLOR_F pc = t.accent;
                pc.a = anim;
                app.brush->SetColor(pc);
                app.renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(pin, dpi(app, sp8), dpi(app, sp8)), app.brush, 1.0f);
                paint::label(app, t.isDark ? L"\x263E" : L"\x2600", Type::Small,
                             D2D1::RectF(pin.left, pin.top, pin.right, pin.bottom), pc,
                             DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        }

        // Row 2: body voice
        if (previewFormat) {
            previewFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            previewFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            D2D1_COLOR_F textColor = t.text;
            textColor.a = anim;
            app.brush->SetColor(textColor);
            app.renderTarget->DrawText(
                L"The quick brown fox jumps over the lazy dog", 43,
                previewFormat,
                D2D1::RectF(cx, sampleY, x + g.cardW - cardPad, sampleY + dpi(app, 16.0f)),
                app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }

        // Row 3: link, code chip, blockquote bar, syntax dots
        if (previewFormat) {
            D2D1_COLOR_F linkColor = t.link;
            linkColor.a = anim;
            app.brush->SetColor(linkColor);
            app.renderTarget->DrawText(
                L"hyperlink", 9, previewFormat,
                D2D1::RectF(cx, swatchY, cx + dpi(app, 80.0f), swatchY + dpi(app, 16.0f)),
                app.brush);
            float lw = measureText(app, L"hyperlink", previewFormat);
            app.renderTarget->DrawLine(
                D2D1::Point2F(cx, swatchY + dpi(app, 15.0f)),
                D2D1::Point2F(cx + lw, swatchY + dpi(app, 15.0f)), app.brush, 1.0f);

            float chipX = cx + lw + dpi(app, sp12);
            D2D1_COLOR_F codeBg = t.codeBackground;
            codeBg.a = anim;
            app.brush->SetColor(codeBg);
            D2D1_RECT_F chip = D2D1::RectF(chipX, swatchY, chipX + dpi(app, 56.0f), swatchY + dpi(app, 16.0f));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(chip, dpi(app, radiusControl), dpi(app, radiusControl)),
                app.brush);
            if (codeFormat) {
                codeFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                D2D1_COLOR_F codeColor = themeInlineCodeColor(t);
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
                    D2D1::RectF(chip.right + dpi(app, sp12), swatchY,
                                chip.right + dpi(app, sp12 + 3.0f), swatchY + dpi(app, 16.0f)),
                    dpi(app, 1.5f), dpi(app, 1.5f)),
                app.brush);
        }
        {
            // Seven syntax dots, right-aligned: the palette at a glance
            const D2D1_COLOR_F* dots[7] = {
                &t.syntaxKeyword, &t.syntaxString,  &t.syntaxComment,
                &t.syntaxNumber,  &t.syntaxFunction, &t.syntaxType,
                &t.syntaxControlFlow};
            float dotR = dpi(app, sp4);
            float step = dpi(app, sp12);
            float dxr = x + g.cardW - cardPad - dotR;
            float dy = swatchY + dpi(app, sp8);
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
            float bx2 = card.right - dpi(app, sp12 + sp8);
            float by2 = card.top + dpi(app, sp12 + sp8);
            D2D1_COLOR_F bead = t.accent;
            bead.a = anim;
            app.brush->SetColor(bead);
            app.renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(bx2, by2), dpi(app, sp8), dpi(app, sp8)),
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

        // What scrolled out of the cards' room cannot be clicked
        D2D1_RECT_F hit = card;
        hit.top = std::max(hit.top, g.view.top);
        hit.bottom = std::min(hit.bottom, g.view.bottom);
        if (hit.bottom > hit.top) app.themeChooserHits.push_back({hit, i});
    }
    app.renderTarget->PopAxisAlignedClip();

    if (app.themeChooserScrollMax > 0.0f) {
        const float thumb = std::max(dpi(app, sp24), room * room / g.gridH);
        const float ty = g.view.top + app.themeChooserScroll / app.themeChooserScrollMax * (room - thumb);
        const float tx = g.view.right + dpi(app, sp8);
        app.brush->SetColor(paint::ink(chrome, 0.22f, anim));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + dpi(app, sp4), ty + thumb), dpi(app, sp2), dpi(app, sp2)),
            app.brush);
    }

    // Footer: edit-theme entry
    {
        const float line = dpi(app, lineHeight(Type::Small));
        const float fy = g.panel.bottom - pad - line;
        std::wstring edit = std::wstring(L"\x270E ") + tr(app, "settings.edit");
        edit += L" - themes.ini";
        paint::label(app, edit.c_str(), Type::Small,
                     D2D1::RectF(panelX + pad + cardPad, fy, panelX + panelWidth * 0.6f, fy + line),
                     paint::ink(chrome, inkSecondary, anim));
        const float ew = paint::labelWidth(app, edit.c_str(), Type::Small);
        app.themeChooserHits.push_back(
            {D2D1::RectF(panelX + pad + cardPad - dpi(app, sp4), fy - dpi(app, sp4),
                         panelX + pad + cardPad + ew + dpi(app, sp4), fy + line + dpi(app, sp4)),
             -3});
    }
}

// Centred text in a rect, aligned on its own layout: the shared formats it
// draws with are also the TOC's, the search panel's and the tab hover
// card's, which expect leading alignment (#270)
static void drawCenteredText(App& app, const wchar_t* text, IDWriteTextFormat* format,
                             const D2D1_RECT_F& rect) {
    IDWriteTextLayout* layout = nullptr;
    if (!format || FAILED(app.dwriteFactory->CreateTextLayout(text, (UINT32)wcslen(text), format,
            rect.right - rect.left, rect.bottom - rect.top, &layout))) {
        return;
    }
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    app.renderTarget->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout, app.brush);
    layout->Release();
}

D2D1_RECT_F helpPanelRect(const App& app) {
    const float width = std::min(dpi(app, 520.0f), app.width - dpi(app, 40.0f));
    const float height = std::min(dpi(app, 700.0f), app.height - dpi(app, 40.0f));
    const float x = (app.width - width) / 2;
    const float y = (app.height - height) / 2 + (1 - app.helpAnimation) * dpi(app, 50.0f);
    return D2D1::RectF(x, y, x + width, y + height);
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
    const auto panel = helpPanelRect(app);
    float panelWidth = panel.right - panel.left;
    float panelHeight = panel.bottom - panel.top;
    float panelX = panel.left;
    float panelY = panel.top;

    // Panel background follows the active document theme.
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(
        D2D1::RectF(panelX, panelY, panelX + panelWidth, panelY + panelHeight),
        dpi(app, 16.0f), dpi(app, 16.0f));
    D2D1_COLOR_F panelColor = app.theme.background;
    panelColor.a = 0.98f * anim;
    panelColor = themeUiPanel(app.theme, panelColor);
    app.brush->SetColor(panelColor);
    app.renderTarget->FillRoundedRectangle(panelRect, app.brush);

    // Border
    D2D1_COLOR_F panelBorder = themeUiText(app.theme);
    panelBorder.a = 0.35f * anim;
    panelBorder = themeUiBorder(app.theme, panelBorder, anim);
    app.brush->SetColor(panelBorder);
    app.renderTarget->DrawRoundedRectangle(panelRect, app.brush, 1.0f);

    // Title (fixed, not scrolled)
    float titleBottomY = panelY + dpi(app, 55.0f);
    IDWriteTextFormat* titleFormat = app.themeTitleFormat;
    if (titleFormat) {
        D2D1_COLOR_F titleColor = themeUiHeading(app.theme);
        titleColor.a = anim;
        app.brush->SetColor(titleColor);
        drawCenteredText(app, tr(app, "help.title"), titleFormat,
            D2D1::RectF(panelX, panelY + dpi(app, 15.0f), panelX + panelWidth, titleBottomY));
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
            D2D1_COLOR_F verColor = themeUiText(app.theme);
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
    std::wstring kEdit = keyLabel(app.keymap[KA_EDIT]) + L" / Ctrl+E";
    std::wstring kQuit = keyLabel(app.keymap[KA_QUIT]);
    // Ctrl+E reads in Live and shows Source's preview pane (#308): the
    // keys of the editor E opens
    const bool live = app.editorLive;

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
        {L"Ctrl+Shift+F", tr(app, "help.view.search_results")},
        {kBrowse.c_str(), tr(app, "help.view.folder_browser")},
        {kToc.c_str(),    tr(app, "help.view.toc")},
        {L"Ctrl+T",       tr(app, "help.view.new_tab")},
        {L"Ctrl+Tab",     tr(app, "help.view.next_tab")},
        {L"Ctrl+W / Ctrl+F4", tr(app, "help.view.close_tab")},
        {L"Ctrl+Shift+T", tr(app, "tab.menu.reopen")},
        {kTheme.c_str(),  tr(app, "help.view.theme")},
        {kStats.c_str(),  tr(app, "help.view.stats")},
        {kHelp.c_str(),   tr(app, "help.view.help")},
    };

    const HelpEntry editEntries[] = {
        {kEdit.c_str(),   tr(app, "help.edit.enter_edit")},
        {L"Ctrl+S",       tr(app, "help.edit.save")},
        {L"Ctrl+/",       tr(app, "help.edit.live")},
        {L"/",            tr(app, "help.edit.slash")},
        {L"Shift+F10",    tr(app, "help.edit.menu")},
        {L"[[",           tr(app, "help.sheet.link")},
        {L"Ctrl+K",       tr(app, "help.edit.link")},
        {live ? L"Ctrl+/, Ctrl+E" : L"Ctrl+E", tr(app, "help.edit.preview")},
        {L"Ctrl+W",       tr(app, "help.edit.word_wrap")},
        {live ? L"Ctrl+E" : L"Ctrl+Shift+E", tr(app, "editor.read_help")},
        {L"ESC ESC",      tr(app, "help.edit.exit_edit")},
        {L"Alt+\x2191 / Alt+\x2193", tr(app, "help.edit.move_line")},
        {L"Ctrl+Alt+\x2191 / \x2193", tr(app, "help.edit.add_caret")},
        {L"Alt+Click",    tr(app, "help.edit.alt_click")},
        {L"Drag / Ctrl+Drag", tr(app, "help.edit.drag_text")},
        {L"Ctrl+D",       tr(app, "help.edit.next_match")},
    };

    const HelpEntry generalEntries[] = {
        {L"Ctrl+A",       tr(app, "help.general.select_all")},
        {L"Ctrl+C",       tr(app, "help.general.copy")},
        {L"Ctrl+N",       tr(app, "help.general.new_note")},
        {L"Ctrl+Shift+N", tr(app, "help.general.new_window")},
        {L"Ctrl+P",       tr(app, "ctx.print")},
        {L"Ctrl+G",       tr(app, "editor.goto_line")},
        {L"ESC",          tr(app, "help.general.close")},
        {kQuit.c_str(),   tr(app, "help.general.quit")},
    };

    // Sheets (sheet_ui.h): the keys a sheet's tab answers to
    const HelpEntry sheetEntries[] = {
        {L"N / Double-click", tr(app, "sheet.tool.note")},
        {L"Tab / Shift+Enter", tr(app, "help.sheet.child")},
        {L"\x2190\x2191\x2192\x2193",       tr(app, "help.sheet.walk")},
        {L"Ctrl+\x2190\x2191\x2192\x2193",  tr(app, "help.sheet.nudge")},
        {L"Enter / E",        tr(app, "help.sheet.edit")},
        {L"Drag side dot",    tr(app, "help.sheet.connect")},
        {L"Ctrl+G / Ctrl+Shift+G", tr(app, "help.sheet.group")},
        {L"1 \x2013 6 / 0",   tr(app, "help.sheet.color")},
        {L"F",                tr(app, "sheet.find_placeholder")},
        {L"S",                tr(app, "sheet.menu.spotlight")},
        {L"P",                tr(app, "sheet.tool.present")},
        {L"Ctrl+L",           tr(app, "sheet.tool.tidy")},
        {L"L / M",            tr(app, "help.sheet.layers")},
        {L"Shift+1 / Shift+2", tr(app, "help.sheet.fit")},
        {L"Space+Drag",       tr(app, "help.sheet.pan")},
        {L"J / Shift+J",      tr(app, "help.sheet.jump")},
        {L"Z / Shift+Z",      tr(app, "help.sheet.fold")},
        {L"[[",               tr(app, "help.sheet.link")},
        {L"Alt / Hover",      tr(app, "help.sheet.peek")},
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
                               sectionHeight((int)_countof(generalEntries)) +
                               sectionHeight((int)_countof(sheetEntries)) + footerH;

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
        D2D1_COLOR_F headerColor = themeUiAccent(app.theme);
        headerColor.a = anim;
        app.brush->SetColor(headerColor);
        app.renderTarget->DrawText(title, (UINT32)wcslen(title), boldFmt,
            D2D1::RectF(leftX, y, rightEdge, y + lineH), app.brush);
        y += lineH + sectionHeaderExtra;

        for (int i = 0; i < count; i++) {
            // Key
            boldFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            D2D1_COLOR_F keyColor = themeUiText(app.theme);
            keyColor.a = 0.9f * anim;
            app.brush->SetColor(keyColor);
            app.renderTarget->DrawText(entries[i].key, (UINT32)wcslen(entries[i].key), boldFmt,
                D2D1::RectF(leftX + dpi(app, 8.0f), y, leftX + keyColWidth, y + lineH), app.brush);

            // Description
            normalFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            D2D1_COLOR_F descriptionColor = themeUiText(app.theme);
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
    drawSection(tr(app, "help.section.sheets"), sheetEntries, (int)_countof(sheetEntries));

    // Footer hint
    D2D1_COLOR_F footerColor = themeUiText(app.theme);
    footerColor.a = 0.55f * anim;
    app.brush->SetColor(footerColor);
    drawCenteredText(app, tr(app, "help.footer"), normalFmt,
        D2D1::RectF(panelX, y, panelX + panelWidth, y + lineH));

    app.renderTarget->PopAxisAlignedClip();

    // Scrollbar if content overflows
    if (maxScroll > 0) {
        float sbHeight = visibleHeight / totalContentHeight * visibleHeight;
        sbHeight = std::max(sbHeight, dpi(app, 20.0f));
        float sbY = contentTopY + (app.helpScroll / maxScroll * (visibleHeight - sbHeight));

        D2D1_COLOR_F sbColor = themeUiText(app.theme);
        sbColor.a = 0.3f * anim;
        app.brush->SetColor(sbColor);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(panelX + panelWidth - dpi(app, 12.0f), sbY,
                                          panelX + panelWidth - dpi(app, 8.0f), sbY + sbHeight), 2, 2),
            app.brush);
    }
}

// The caption icon and document right-click share the same themed popup.
void renderAppMenuButtonBackground(App& app) {
    if (!appMenuAvailable(app) ||
        !(app.appMenuHover || (app.showContextMenu && app.applicationMenu))) return;
    auto r = appMenuButtonRect(app);
    float inset = dpi(app, 4.0f);
    r.left += inset;
    r.right -= inset;
    r.top += inset;
    r.bottom -= inset;
    D2D1_COLOR_F c = themeUiAccent(app.theme);
    c.a = app.showContextMenu && app.applicationMenu ? 0.2f : 0.1f;
    app.brush->SetColor(c);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(r, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
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
    float w = contextMenuWidth(app);
    float h = contextMenuHeight(app);

    D2D1_COLOR_F panelBg = themeUiPanel(app.theme, app.theme.isDark ? hexColor(0x1E1E1E, 0.97f)
                                                                    : hexColor(0xF8F8F8, 0.97f));
    D2D1_COLOR_F borderColor = themeUiBorder(app.theme, app.theme.isDark ? hexColor(0x3A3A40, 0.9f)
                                                                         : hexColor(0xC8C8C8, 0.9f));
    app.brush->SetColor(panelBg);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 6, 6), app.brush);
    app.brush->SetColor(borderColor);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 6, 6), app.brush, 1.0f);

    const auto& entries = contextMenuEntries(app);

    // Shortcut hints reflect the user keymap ([Keys] in settings.ini)
    auto shortcutLabel = [&](const ContextMenuEntry& entry) -> std::wstring {
        int item = entry.action;
        // Single-key reading commands type text in the editor.
        if (app.applicationMenu && app.editMode &&
            (item == CTX_THEME || item == CTX_HELP)) return L"";
        switch (item) {
            case CTX_ANNOTATE: return keyLabel(app.keymap[KA_ANNOTATE]);
            case CTX_NEW:    return keyLabel(app.keymap[KA_NEWFILE]);
            case CTX_EDIT:   return keyLabel(app.keymap[KA_EDIT]);
            case CTX_SEARCH: return keyLabel(app.keymap[KA_SEARCH]);
            case CTX_TOC:    return keyLabel(app.keymap[KA_TOC]);
            case CTX_BROWSE: return keyLabel(app.keymap[KA_BROWSE]);
            case CTX_THEME:  return keyLabel(app.keymap[KA_THEME]);
            case CTX_HELP:   return keyLabel(app.keymap[KA_HELP]);
            default:         return entry.shortcut;
        }
    };

    float pad = contextMenuPadding(app);
    float itemH = contextMenuItemHeight(app);
    float inset = dpi(app, 12.0f);
    for (int row = 0; row < (int)entries.size(); row++) {
        const auto& entry = entries[row];
        int i = entry.action;
        float top = y + contextMenuItemTop(app, row);
        bool enabled = contextMenuItemEnabled(app, i);

        if (i == app.hoveredContextMenuItem && enabled) {
            D2D1_COLOR_F hoverColor = themeUiAccent(app.theme);
            hoverColor.a = 0.18f * anim;
            app.brush->SetColor(hoverColor);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(x + pad, top, x + w - pad, top + itemH), 4, 4),
                app.brush);
        }

        D2D1_COLOR_F textColor = themeUiText(app.theme);
        textColor.a = (enabled ? 1.0f : 0.35f) * anim;
        app.brush->SetColor(textColor);
        float textY = top + (itemH - dpi(app, 18.0f)) / 2;
        // Fold all reads Unfold all while a section is folded (#316)
        const wchar_t* itemLabel =
            tr(app, entry.action == CTX_FOLD_ALL && anySectionFolded(app) ? "ctx.unfold_all" : entry.key);
        app.renderTarget->DrawText(
            itemLabel, (UINT32)wcslen(itemLabel), format,
            D2D1::RectF(x + inset, textY, x + w - inset, top + itemH), app.brush);

        std::wstring hintText = shortcutLabel(entry);
        if (!hintText.empty()) {
            IDWriteTextLayout* hint = nullptr;
            app.dwriteFactory->CreateTextLayout(
                hintText.c_str(), (UINT32)hintText.size(),
                format, 1000.0f, itemH, &hint);
            if (hint) {
                DWRITE_TEXT_METRICS metrics{};
                hint->GetMetrics(&metrics);
                D2D1_COLOR_F hintColor = themeUiText(app.theme);
                hintColor.a = 0.4f * anim;
                app.brush->SetColor(hintColor);
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(x + w - inset - metrics.width, textY),
                    hint, app.brush);
                hint->Release();
            }
        }

        if (entry.separatorAfter) {
            float sepY = top + itemH + contextMenuSeparatorHeight(app) * 0.5f;
            app.brush->SetColor(borderColor);
            app.renderTarget->DrawLine(
                D2D1::Point2F(x + pad, sepY), D2D1::Point2F(x + w - pad, sepY),
                app.brush, 1.0f);
        }
    }
}

// --- Search bar geometry ---

// Where the bar rests once it has slid in: over the editor pane while
// editing, else over the document between any side panels, so a docked
// panel such as the search results never sits under it
D2D1_RECT_F searchBarRect(const App& app) {
    float barWidth = std::min(dpi(app, 500.0f), app.width - dpi(app, 40.0f));
    float barHeight = dpi(app, 44.0f);
    float barLeft = 0.0f;
    float barCenterWidth = (float)app.width;
    if (app.editMode) {
        // Center over editor pane (left side)
        float paneWidth = editorPaneWidth(app);
        barLeft = dpi(app, 48); // The edit rail is painted above overlays.
        barWidth = std::min(barWidth, paneWidth - barLeft - dpi(app, 24.0f));
        barCenterWidth = paneWidth - barLeft;
    } else {
        barLeft = documentViewportX(app);
        barCenterWidth = documentViewportWidth(app);
        barWidth = std::max(std::min(barWidth, barCenterWidth - dpi(app, 40.0f)),
                            std::min(barWidth, dpi(app, 200.0f)));
    }
    float barX = barLeft + (barCenterWidth - barWidth) / 2;
    float barY = chromeTopHeight(app) + dpi(app, 20.0f);
    return D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight);
}

D2D1_RECT_F searchBarDrawRect(const App& app) {
    D2D1_RECT_F bar = searchBarRect(app);
    const float height = bar.bottom - bar.top;
    bar.top = chromeTopHeight(app) + dpi(app, 20.0f) * app.searchAnimation -
              height * (1.0f - app.searchAnimation);
    bar.bottom = bar.top + height;
    return bar;
}

D2D1_RECT_F searchReplaceRowRect(const App& app) {
    D2D1_RECT_F row = searchBarDrawRect(app);
    row.top = row.bottom + dpi(app, 6.0f);
    row.bottom = row.top + dpi(app, row.right - row.left < dpi(app, 390) ? 88.0f : 44.0f);
    return row;
}

// The reader's bar ends in a button that opens the results panel (#246)
D2D1_RECT_F searchResultsButtonRect(const App& app) {
    if (app.editMode || !app.showSearch) return D2D1::RectF(0, 0, 0, 0);
    D2D1_RECT_F bar = searchBarRect(app);
    float size = dpi(app, 28.0f);
    float x = bar.right - dpi(app, 8.0f) - size;
    float y = (bar.top + bar.bottom - size) * 0.5f;
    return D2D1::RectF(x, y, x + size, y + size);
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

    // The window's own surface while the page is a preview: --ui-background
    // when a CSS theme gives one, for its --ui-text and --ui-accent
    app.brush->SetColor(th.uiBackground.value_or(th.background));
    app.renderTarget->FillRectangle(D2D1::RectF(0, 0, w, h), app.brush);

    float topBarH = 52.0f * ui;
    float optionRowH = PRINT_OPTION_ROW_H * ui;  // the paper options, under the header
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
            float top = topBarH + optionRowH;
            float py = top + (h - top - bottomBarH - ph) / 2.0f;
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
    useUiFontFallback(app, uiFmt);
    useUiFontFallback(app, btnFmt);

    int pageCount = (int)app.printPreviewBounds.size() - 1;
    if (uiFmt) {
        uiFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        uiFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        wchar_t label[64];
         swprintf_s(label, tr(app, "print.title"),
                    app.printPreviewPage + 1, pageCount > 0 ? pageCount : 1);
        app.brush->SetColor(themeUiText(th));
        app.renderTarget->DrawText(label, (UINT32)wcslen(label), uiFmt,
            D2D1::RectF(0, 0, w, topBarH), app.brush);
    }

    // Format chips: paper sizes top-left, orientation top-right. The active
    // chip fills with the accent color.
    auto drawChip = [&](const D2D1_RECT_F& rect, const wchar_t* text, bool active) {
        float cr = 5.0f * ui;
        if (active) {
            app.brush->SetColor(themeUiAccent(th));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(rect, cr, cr), app.brush);
        } else {
            D2D1_COLOR_F bc = themeUiText(th);
            bc.a = 0.35f;
            app.brush->SetColor(bc);
            app.renderTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(rect, cr, cr), app.brush, 1.0f);
        }
        if (uiFmt) {
            app.brush->SetColor(active ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : themeUiText(th));
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

    // Paper options, centred under the header (docs/specs/scholar.md,
    // phase 3): toggles, each filled while on, as wide as its label
    {
        const char* keys[PRINT_OPTION_COUNT] = {"print.two_columns", "print.page_numbers",
                                                "print.running_heads"};
        float widths[PRINT_OPTION_COUNT];
        float total = 0.0f;
        for (int i = 0; i < PRINT_OPTION_COUNT; i++) {
            widths[i] = (uiFmt ? measureText(app, tr(app, keys[i]), uiFmt) : 90.0f * ui) + 28.0f * ui;
            total += widths[i] + (i ? chipGap : 0.0f);
        }
        float ox = (w - total) / 2.0f;
        const float oy = topBarH + (optionRowH - chipH) / 2.0f;
        for (int i = 0; i < PRINT_OPTION_COUNT; i++) {
            D2D1_RECT_F rect = D2D1::RectF(ox, oy, ox + widths[i], oy + chipH);
            app.printPreviewOptionBtn[i] = rect;
            drawChip(rect, tr(app, keys[i]), app.printOptionOn[i]);
            ox += widths[i] + chipGap;
        }
    }

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
    app.brush->SetColor(themeUiAccent(th));
    app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(printR, r, r), app.brush);
    D2D1_COLOR_F borderC = themeUiText(th);
    borderC.a = 0.4f;
    app.brush->SetColor(borderC);
    app.renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(cancelR, r, r), app.brush, 1.0f);

    if (btnFmt) {
        btnFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        btnFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        const wchar_t* printLabel = tr(app, "print.print");
        app.renderTarget->DrawText(printLabel, (UINT32)wcslen(printLabel), btnFmt, printR, app.brush);
        app.brush->SetColor(themeUiText(th));
        const wchar_t* cancelLabel = tr(app, "print.cancel");
        app.renderTarget->DrawText(cancelLabel, (UINT32)wcslen(cancelLabel), btnFmt, cancelR, app.brush);
    }

    // Margin presets, bottom left (#257), in mm or inches by the locale's
    // measurement system like the default paper; a settings.ini margin
    // outside the presets, or a paper's geometry: margin=, shows as its
    // own active chip
    wchar_t measure[2] = L"0";
    const bool inches = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE,
                                        measure, 2) && measure[0] == L'1';
    auto marginLabel = [&](float mm) {
        wchar_t text[32];
        if (inches) swprintf_s(text, L"%g in", std::round(mm / 25.4f * 100.0f) / 100.0f);
        else swprintf_s(text, L"%g mm", std::round(mm * 100.0f) / 100.0f);
        return std::wstring(text);
    };
    float mx = margin;
    float marginChipY = h - bottomBarH + (bottomBarH - chipH) / 2.0f;
    if (uiFmt) {
        uiFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        const wchar_t* label = tr(app, "print.margins");
        float labelW = measureText(app, label, uiFmt) + 10.0f * ui;
        D2D1_COLOR_F labelC = themeUiText(th);
        labelC.a = 0.7f;
        app.brush->SetColor(labelC);
        app.renderTarget->DrawText(label, (UINT32)wcslen(label), uiFmt,
            D2D1::RectF(mx, h - bottomBarH, mx + labelW, h), app.brush);
        mx += labelW;
    }
    const float marginChipW = 72.0f * ui;
    bool preset = false;
    for (int i = 0; i < PRINT_MARGIN_COUNT; i++) {
        D2D1_RECT_F rect = D2D1::RectF(mx, marginChipY, mx + marginChipW, marginChipY + chipH);
        app.printPreviewMarginBtn[i] = rect;
        const bool active = std::fabs(app.printPreviewMarginMm - PRINT_MARGINS_MM[i]) < 0.05f;
        preset = preset || active;
        drawChip(rect, marginLabel(PRINT_MARGINS_MM[i]).c_str(), active);
        mx += marginChipW + chipGap;
    }
    if (!preset) {
        D2D1_RECT_F rect = D2D1::RectF(mx, marginChipY, mx + marginChipW, marginChipY + chipH);
        drawChip(rect, marginLabel(app.printPreviewMarginMm).c_str(), true);
        mx += marginChipW + chipGap;
    }

    // Hint after the margins, when it fits on one line
    if (uiFmt) {
        uiFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        const wchar_t* hint = tr(app, "print.hint");
        float hintX = mx + 8.0f * ui;
        if (measureText(app, hint, uiFmt) <= cancelR.left - gap - hintX) {
            D2D1_COLOR_F hintC = themeUiText(th);
            hintC.a = 0.5f;
            app.brush->SetColor(hintC);
            app.renderTarget->DrawText(hint, (UINT32)wcslen(hint), uiFmt,
                D2D1::RectF(hintX, h - bottomBarH, cancelR.left - gap, h), app.brush);
        }
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
    // A CSS theme's --ui-panel is the panel, the lowest lift; the layers
    // above it lift from there toward white as they would from the page
    if (app.theme.uiPanel) {
        const bool dark = themeUiDark(app.theme);
        const float panel = dark ? 0.045f : 0.5f;
        const float t = std::max(0.0f, ((dark ? darkT : lightT) - panel) / (1.0f - panel));
        D2D1_COLOR_F c = *app.theme.uiPanel;
        c.r += (1.0f - c.r) * t;
        c.g += (1.0f - c.g) * t;
        c.b += (1.0f - c.b) * t;
        c.a *= alpha;
        return c;
    }
    float t = app.theme.isDark ? darkT : lightT;
    D2D1_COLOR_F c = app.theme.background;
    c.r += (1.0f - c.r) * t;
    c.g += (1.0f - c.g) * t;
    c.b += (1.0f - c.b) * t;
    c.a = alpha;
    return c;
}

// --- The dialog on the interface's scale (ui_scale.h) -------------------
//
// A page is groups of rows: a small upper-case label, then one card whose
// rows are parted by hairlines. A row has a label, a line of description
// under it and its control at the right; a control too wide for the row
// moves under the label. Every gap, row height and corner is the scale's.

D2D1_COLOR_F settingsInk(const App& app, float share, float anim) {
    return paint::ink(app.theme, share, anim);
}

// The shared pieces (ui_paint.h) under the names this dialog grew up with
void settingsText(App& app, const wchar_t* text, uiscale::Type role, const D2D1_RECT_F& box,
                  D2D1_COLOR_F color, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    paint::label(app, text, role, box, color, align);
}

float settingsTextWidth(App& app, const wchar_t* text, uiscale::Type role) {
    return paint::labelWidth(app, text, role);
}

float settingsLineTop(const App& app, float top, float height, uiscale::Type role) {
    return paint::lineTop(app, top, height, role);
}

constexpr float kSettingsToggleW = 40.0f;
constexpr float kSettingsToggleH = 20.0f;
// The least room a row's label keeps beside its control
constexpr float kSettingsLabelMin = 120.0f;
// A row whose control went under its label: the label's part, then a band
// for the control and the row's padding again
constexpr float kSettingsRowStacked = uiscale::rowTwoLine + uiscale::controlBox + uiscale::sp8;
// A slider's row: the label's part and the slider's line
constexpr float kSettingsRowSlider = uiscale::rowTwoLine + uiscale::sp24;
// The dialog's parts
constexpr float kSettingsHeader = uiscale::sp24 + 28.0f + uiscale::sp16;        // title's line and its gaps
constexpr float kSettingsFooter = uiscale::sp16 + 16.0f + uiscale::sp24;        // gap, the hint's line, padding
constexpr float kSettingsTabs = uiscale::rowMenu + uiscale::sp16;               // the sections across, when narrow
constexpr float kSettingsGroupHead = 16.0f + uiscale::sp8;                      // a group's label and its gap
constexpr float kSettingsRailW = 140.0f;
// The active section's bar: as wide as Windows' own navigation mark
constexpr float kSettingsMark = 3.0f;

D2D1_RECT_F settingsToggle(App& app, const D2D1_RECT_F& slot, bool on, int action, float anim) {
    const D2D1_RECT_F r = paint::toggle(app, slot, on, app.theme, anim);
    // Generous hit area: the whole row height around the switch
    const float reach = dpi(app, uiscale::sp8);
    app.settingsHits.push_back({D2D1::RectF(r.left - reach, r.top - reach, r.right + reach, r.bottom + reach), action});
    return r;
}

float settingsChipWidth(App& app, const wchar_t* label) {
    return settingsTextWidth(app, label, uiscale::Type::Body) + dpi(app, uiscale::sp12) * 2;
}
float settingsChipsWidth(App& app, std::initializer_list<const wchar_t*> labels) {
    float total = 0.0f;
    for (const wchar_t* label : labels) total += settingsChipWidth(app, label) + dpi(app, uiscale::sp8);
    return total - dpi(app, uiscale::sp8);
}

// A button of the row: outlined, or filled with the accent when it is the
// row's main action. Advances x past it.
void settingsChip(App& app, float& x, float y, const wchar_t* label, bool primary,
                  int action, float anim) {
    const float h = dpi(app, uiscale::control);
    const float w = settingsChipWidth(app, label);
    const float radius = dpi(app, uiscale::radiusControl);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius);
    D2D1_COLOR_F ink;
    if (primary) {
        D2D1_COLOR_F c = themeUiAccent(app.theme); c.a = anim;
        app.brush->SetColor(c);
        app.renderTarget->FillRoundedRectangle(rr, app.brush);
        ink = D2D1::ColorF(1, 1, 1, anim);
    } else {
        app.brush->SetColor(settingsInk(app, 0.3f, anim));
        app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);
        ink = settingsInk(app, uiscale::inkPrimary, anim);
    }
    settingsText(app, label, uiscale::Type::Body,
                 D2D1::RectF(x, settingsLineTop(app, y, h, uiscale::Type::Body), x + w, y + h), ink,
                 DWRITE_TEXT_ALIGNMENT_CENTER);
    app.settingsHits.push_back({D2D1::RectF(x, y, x + w, y + h), action});
    x += w + dpi(app, uiscale::sp8);
}

// A row as its page describes it: how wide its control is, and its kind
struct SettingsRowSpec {
    float controlW = 0.0f;   // in pixels; 0 for a row with no control at its right
    bool slider = false;     // a slider's line under the label
    bool controlOnly = false;  // buttons alone, no label
};

float settingsSegmentsWidth(App& app, std::initializer_list<const wchar_t*> labels, bool equal) {
    float total = 0.0f, widest = 0.0f;
    for (const wchar_t* label : labels) {
        const float w = settingsTextWidth(app, label, uiscale::Type::Body) + dpi(app, uiscale::sp12) * 2;
        total += w;
        widest = std::max(widest, w);
    }
    if (equal) total = widest * (float)labels.size();
    return total + dpi(app, uiscale::sp2) * ((float)labels.size() + 1.0f);
}

const wchar_t* const* settingsFoldLabels(App& app) {
    static const wchar_t* labels[3];
    labels[0] = tr(app, "settings.fold.off");
    labels[1] = tr(app, "settings.fold.on");
    labels[2] = tr(app, "settings.fold.folded");
    return labels;
}

float settingsDropdownWidth(const App& app) {
    return dpi(app, 152.0f + uiscale::sp8 + uiscale::controlBox);  // the box, a gap, the pencil
}

// The pages: their groups' labels and rows, in the order they are drawn
struct SettingsGroupSpec {
    const wchar_t* label;
    std::vector<SettingsRowSpec> rows;
};
std::vector<SettingsGroupSpec> settingsPage(App& app, int section) {
    const SettingsRowSpec toggle{dpi(app, kSettingsToggleW)};
    const SettingsRowSpec dropdown{settingsDropdownWidth(app)};
    SettingsRowSpec slider;
    slider.slider = true;
    std::vector<SettingsGroupSpec> page;
    if (section == 0) {
        SettingsRowSpec files;
        files.controlOnly = true;
        page.push_back({tr(app, "settings.group.interface"), {dropdown, dropdown}});
        page.push_back({tr(app, "settings.group.files"), {toggle, toggle, toggle, toggle}});
        page.push_back({tr(app, "settings.config_files"), {files}});
    } else if (section == 1) {
        const wchar_t* const* fold = settingsFoldLabels(app);
        const SettingsRowSpec folding{settingsSegmentsWidth(app, {fold[0], fold[1], fold[2]}, false)};
        const SettingsRowSpec side{settingsSegmentsWidth(
            app, {tr(app, "settings.toc.left"), tr(app, "settings.toc.right")}, true)};
        const SettingsRowSpec themes{settingsChipsWidth(
            app, {tr(app, "settings.browse"), tr(app, "settings.edit"), tr(app, "settings.import"),
                  tr(app, "settings.new")})};
        page.push_back({tr(app, "settings.group.reading"), {slider, slider, toggle, folding}});
        page.push_back({tr(app, "settings.group.theme"), {toggle, themes}});
        page.push_back({tr(app, "settings.group.window"), {side, toggle}});
    } else if (section == 2) {
        const SettingsRowSpec browse{settingsChipWidth(app, tr(app, "settings.browse"))};
        page.push_back({tr(app, "settings.group.editing"), {toggle, toggle, toggle, toggle, toggle, toggle}});
        page.push_back({tr(app, "settings.group.tools"), {browse, browse}});
    }
    return page;
}

bool settingsRowStacks(const App& app, const SettingsRowSpec& row, float contentW) {
    if (row.slider || row.controlOnly || row.controlW <= 0.0f) return false;
    const float labelW = contentW - dpi(app, uiscale::sp16) * 3 - row.controlW;
    return labelW < dpi(app, kSettingsLabelMin);
}

float settingsRowHeight(const App& app, const SettingsRowSpec& row, float contentW) {
    if (row.controlOnly) return dpi(app, uiscale::rowControl);
    if (row.slider) return dpi(app, kSettingsRowSlider);
    return dpi(app, settingsRowStacks(app, row, contentW) ? kSettingsRowStacked : uiscale::rowTwoLine);
}

float settingsPageHeight(const App& app, const std::vector<SettingsGroupSpec>& page, float contentW) {
    float h = 0.0f;
    for (size_t g = 0; g < page.size(); g++) {
        if (g) h += dpi(app, uiscale::sp24);
        h += dpi(app, kSettingsGroupHead);
        for (const SettingsRowSpec& row : page[g].rows) h += settingsRowHeight(app, row, contentW);
    }
    return h;
}

// The sections go across the top when the rail would take too much of a
// narrow dialog; the front matter page has more columns and goes earlier
bool settingsCompact(const App& app, float panelW) {
    return panelW < dpi(app, app.settingsSection == 3 ? 760.0f : 600.0f);
}

float settingsPanelWidth(const App& app) {
    return std::min(dpi(app, app.settingsSection == 3 ? 980.0f : 760.0f),
                    app.width - dpi(app, app.settingsSection == 3 ? 32.0f : 80.0f));
}

float settingsContentWidth(const App& app, float panelW) {
    const float inner = panelW - dpi(app, uiscale::sp24) * 2;
    return settingsCompact(app, panelW) ? inner : inner - dpi(app, kSettingsRailW + uiscale::sp24);
}

} // namespace

D2D1_RECT_F settingsPanelRect(const App& app) {
    const float w = settingsPanelWidth(app);
    // As tall as its page: the header, the page's groups and the footer. A
    // window too short for that scrolls the page between the two.
    float tall = dpi(app, 650.0f);
    if (app.settingsSection != 3) {
        App& measuring = const_cast<App&>(app);  // text is measured, nothing changes
        const auto page = settingsPage(measuring, app.settingsSection);
        tall = dpi(app, kSettingsHeader + kSettingsFooter) +
               (settingsCompact(app, w) ? dpi(app, kSettingsTabs) : 0.0f) +
               settingsPageHeight(app, page, settingsContentWidth(app, w));
    }
    float h = std::min(tall, app.height - dpi(app, 64.0f));
    float x = (app.width - w) / 2;
    float y = (app.height - h) / 2 + (1 - app.settingsAnimation) * dpi(app, 30.0f);
    return D2D1::RectF(x, y, x + w, y + h);
}

D2D1_RECT_F settingsCloseButtonRect(const App& app) {
    const auto panel = settingsPanelRect(app);
    float size = dpi(app, uiscale::controlBox);
    float right = panel.right - dpi(app, uiscale::sp16);
    float top = panel.top + dpi(app, uiscale::sp24);
    return D2D1::RectF(right - size, top, right, top + size);
}

bool settingsWheel(App& app, float notches) {
    if (!app.showSettings || app.settingsSection == 3) return false;
    const float before = app.settingsScroll;
    app.settingsScroll = std::clamp(app.settingsScroll - notches * dpi(app, uiscale::rowTwoLine), 0.0f,
                                    std::max(0.0f, app.settingsScrollMax));
    if (app.settingsScroll != before) {
        app.settingsLangOpen = false;  // their boxes move away under them
        app.settingsKeysOpen = false;
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    return true;
}

void renderSettingsOverlay(App& app) {
    using namespace uiscale;
    if (app.settingsAnimation <= 0.0f) app.settingsScroll = 0.0f;  // opened anew: from the top
    if (app.settingsAnimation < 1.0f) {
        float prev = app.settingsAnimation;
        app.settingsAnimation = std::min(1.0f, app.settingsAnimation + 0.2f);
        if (app.settingsAnimation != prev) InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.settingsAnimation;
    app.settingsHits.clear();
    app.settingsRows.clear();
    app.settingsCards.clear();
    app.settingsGroupLabels.clear();

    if (!font(app, Type::Body)) return;

    // Keep the underlying document visible while settings are open.
    float backdropAlpha = (app.theme.isDark ? 0.34f : 0.24f) * anim;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    const auto panelRect = settingsPanelRect(app);
    float panelW = panelRect.right - panelRect.left;
    float panelH = panelRect.bottom - panelRect.top;
    float px = panelRect.left;
    float py = panelRect.top;
    const auto closeRect = settingsCloseButtonRect(app);
    const float pad = dpi(app, sp24);

    // Elevated panel: soft drop shadow, lifted surface, hairline border
    const float panelRadius = dpi(app, radiusDialog);
    for (int ring = 3; ring >= 1; ring--) {
        float spread = dpi(app, (float)ring * sp4);
        D2D1_COLOR_F shadow = D2D1::ColorF(
            0.0f, 0.0f, 0.0f, 0.10f * anim / (float)(ring * ring));
        app.brush->SetColor(shadow);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(px - spread, py - spread * 0.4f, px + panelW + spread,
                            py + panelH + spread),
                panelRadius + spread, panelRadius + spread),
            app.brush);
    }
    D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(
        D2D1::RectF(px, py, px + panelW, py + panelH), panelRadius, panelRadius);
    app.brush->SetColor(settingsLift(app, 0.045f, 0.5f, 0.99f * anim));
    app.renderTarget->FillRoundedRectangle(panel, app.brush);
    D2D1_COLOR_F border = themeUiText(app.theme); border.a = 0.14f * anim;
    border = themeUiBorder(app.theme, border, anim);
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(panel, app.brush, 1.0f);

    // Title
    {
        D2D1_COLOR_F tc = themeUiHeading(app.theme); tc.a = anim;
        settingsText(app, tr(app, "settings.title"), Type::Title,
                     D2D1::RectF(px + pad, py + pad, closeRect.left - dpi(app, sp8),
                                 py + pad + dpi(app, lineHeight(Type::Title))),
                     tc);
    }

    bool closeHover = app.mouseX >= closeRect.left && app.mouseX <= closeRect.right &&
                      app.mouseY >= closeRect.top && app.mouseY <= closeRect.bottom;
    if (closeHover) {
        app.brush->SetColor(settingsInk(app, 0.10f, anim));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(closeRect, dpi(app, radiusControl), dpi(app, radiusControl)), app.brush);
    }
    paint::closeCross(app, closeRect, settingsInk(app, closeHover ? 1.0f : inkSecondary, anim));
    app.settingsHits.push_back({closeRect, SET_CLOSE});

    // Sections: a rail at the left, or across the top of a narrow dialog
    const wchar_t* sections[] = {tr(app, "settings.section.general"),
                                 tr(app, "settings.section.appearance"),
                                 tr(app, "settings.section.editor"), tr(app, "fm.section")};
    const int sectionActions[] = {SET_SECTION_GENERAL, SET_SECTION_APPEARANCE, SET_SECTION_EDITOR, SET_SECTION_FRONTMATTER};
    float railX = px + pad;
    float railY = py + dpi(app, kSettingsHeader);
    const bool compact = settingsCompact(app, panelW);
    float railW = dpi(app, kSettingsRailW);
    const float railRowH = dpi(app, rowMenu);
    for (int i = 0; i < 4; i++) {
        float rowY = railY + i * (railRowH + dpi(app, sp4));
        D2D1_RECT_F r = D2D1::RectF(railX, rowY, railX + railW, rowY + railRowH);
        if (compact) {
            float cell = (panelW - pad * 2) / 4;
            r = D2D1::RectF(railX + i * cell, railY, railX + (i + 1) * cell, railY + railRowH);
        }
        const bool active = i == app.settingsSection;
        if (active) {
            // Active section: lifted pill plus an accent indicator bar
            app.brush->SetColor(settingsLift(app, 0.12f, 0.95f, anim));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(r, dpi(app, radiusControl), dpi(app, radiusControl)), app.brush);
            D2D1_COLOR_F bar = themeUiAccent(app.theme); bar.a = anim;
            app.brush->SetColor(bar);
            float barH = dpi(app, sp16);
            float barY = (r.top + r.bottom - barH) * 0.5f;
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(r.left, barY, r.left + dpi(app, kSettingsMark), barY + barH),
                    dpi(app, kSettingsMark) * 0.5f, dpi(app, kSettingsMark) * 0.5f),
                app.brush);
        }
        settingsText(app, sections[i], active ? Type::Strong : Type::Body,
                     D2D1::RectF(r.left + dpi(app, sp12), settingsLineTop(app, r.top, railRowH, Type::Body),
                                 r.right - dpi(app, sp4), r.bottom),
                     settingsInk(app, active ? inkPrimary : inkSecondary, anim));
        app.settingsHits.push_back({r, sectionActions[i]});
    }

    // Content column
    float cx = compact ? railX : railX + railW + pad;
    float cw = px + panelW - pad - cx;
    float contentTop = compact ? railY + dpi(app, kSettingsTabs) : railY;
    if (app.settingsSection == 3) {
        app.settingsScroll = app.settingsScrollMax = 0.0f;
        renderFrontmatterSettings(app, D2D1::RectF(cx, contentTop, cx + cw, py + panelH - pad));
        return;
    }

    // The page's room, between the header and the footer; a page taller
    // than that scrolls in it
    const auto page = settingsPage(app, app.settingsSection);
    const float pageH = settingsPageHeight(app, page, cw);
    const D2D1_RECT_F viewport = D2D1::RectF(cx, contentTop, cx + cw,
                                             py + panelH - dpi(app, kSettingsFooter));
    app.settingsViewport = viewport;
    app.settingsScrollMax = std::max(0.0f, pageH - (viewport.bottom - viewport.top));
    app.settingsScroll = std::clamp(app.settingsScroll, 0.0f, app.settingsScrollMax);
    const size_t firstPageHit = app.settingsHits.size();
    app.renderTarget->PushAxisAlignedClip(
        D2D1::RectF(viewport.left - 1.0f, viewport.top, viewport.right + 1.0f, viewport.bottom),
        D2D1_ANTIALIAS_MODE_ALIASED);

    D2D1_COLOR_F cardBg = settingsLift(app, 0.085f, 0.85f, anim);
    D2D1_COLOR_F cardBorder = themeUiBorder(app.theme, themeUiDark(app.theme)
        ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.05f * anim)
        : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.08f * anim), anim);
    const float rowPad = dpi(app, sp16);
    const float twoLine = dpi(app, rowTwoLine);

    // The rows in order. A row is taken with its label; what comes back is
    // the slot its control is drawn in.
    float cy = contentTop - app.settingsScroll;
    size_t groupAt = 0, rowAt = 0;
    auto beginGroup = [&]() {
        const SettingsGroupSpec& group = page[groupAt];
        if (groupAt) cy += dpi(app, sp24);
        // The label: small capitals with a little air between the letters
        paint::sectionLabel(app, group.label, cx + rowPad, cy, std::max(1.0f, cw - rowPad * 2),
                            settingsInk(app, inkSecondary, anim));
        app.settingsGroupLabels.push_back(D2D1::RectF(cx + rowPad, cy, cx + cw - rowPad, cy + dpi(app, 16.0f)));
        cy += dpi(app, kSettingsGroupHead);
        float cardH = 0.0f;
        for (const SettingsRowSpec& row : group.rows) cardH += settingsRowHeight(app, row, cw);
        const D2D1_RECT_F cardRect = D2D1::RectF(cx, cy, cx + cw, cy + cardH);
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(cardRect, dpi(app, radiusCard), dpi(app, radiusCard));
        app.brush->SetColor(cardBg);
        app.renderTarget->FillRoundedRectangle(rr, app.brush);
        app.brush->SetColor(cardBorder);
        app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);
        app.settingsCards.push_back(cardRect);
        rowAt = 0;
    };
    auto row = [&](const wchar_t* label, const wchar_t* hint) -> D2D1_RECT_F {
        if (rowAt == 0) beginGroup();
        const SettingsRowSpec& spec = page[groupAt].rows[rowAt];
        const float h = settingsRowHeight(app, spec, cw);
        const bool stacked = settingsRowStacks(app, spec, cw);
        const D2D1_RECT_F rect = D2D1::RectF(cx, cy, cx + cw, cy + h);
        if (rowAt) {
            app.brush->SetColor(cardBorder);
            app.renderTarget->FillRectangle(
                D2D1::RectF(rect.left + rowPad, rect.top - 0.5f, rect.right - rowPad, rect.top + 0.5f), app.brush);
        }
        // The control's slot: at the right of the label's part, or in a
        // band of its own under the label
        const float box = dpi(app, controlBox);
        D2D1_RECT_F slot = D2D1::RectF(rect.right - rowPad - spec.controlW, rect.top + (twoLine - box) * 0.5f,
                                       rect.right - rowPad, rect.top + (twoLine + box) * 0.5f);
        if (stacked) {
            slot = D2D1::RectF(rect.left + rowPad, rect.top + twoLine,
                               std::min(rect.left + rowPad + spec.controlW, rect.right - rowPad),
                               rect.top + twoLine + box);
        } else if (spec.controlOnly) {
            slot = D2D1::RectF(rect.left + rowPad, rect.top, rect.right - rowPad, rect.bottom);
        } else if (spec.slider) {
            slot = D2D1::RectF(rect.left + rowPad, rect.top + twoLine - dpi(app, sp4),
                               rect.right - rowPad, rect.bottom - dpi(app, sp4));
        }
        float textRight = rect.right - rowPad;
        if (!stacked && !spec.slider && !spec.controlOnly && spec.controlW > 0.0f) textRight = slot.left - rowPad;
        App::SettingsRowBox boxes;
        boxes.group = (int)groupAt;
        boxes.row = rect;
        boxes.control = slot;
        boxes.stacked = stacked;
        if (label) {
            const float top = rect.top + dpi(app, sp8);
            const float lineB = dpi(app, lineHeight(Type::Body));
            boxes.text = D2D1::RectF(rect.left + rowPad, top, textRight,
                                     top + lineB + dpi(app, sp2) + dpi(app, lineHeight(Type::Small)));
            boxes.titleTrimmed = settingsTextWidth(app, label, Type::Body) > textRight - rect.left - rowPad + 0.5f;
            settingsText(app, label, Type::Body,
                         D2D1::RectF(rect.left + rowPad, top, textRight, top + lineB),
                         settingsInk(app, inkPrimary, anim));
            if (hint) {
                const float hintTop = top + lineB + dpi(app, sp2);
                settingsText(app, hint, Type::Small,
                             D2D1::RectF(rect.left + rowPad, hintTop, textRight,
                                         hintTop + dpi(app, lineHeight(Type::Small))),
                             settingsInk(app, inkSecondary, anim));
            }
        }
        app.settingsRows.push_back(boxes);
        cy += h;
        if (++rowAt == page[groupAt].rows.size()) {
            rowAt = 0;
            ++groupAt;
        }
        return slot;
    };
    auto toggleRow = [&](const char* label, const char* hint, bool on, int action) {
        const D2D1_RECT_F slot = row(tr(app, label), tr(app, hint));
        settingsToggle(app, slot, on, action, anim);
    };
    auto chipsTop = [&](const D2D1_RECT_F& slot) {
        return (slot.top + slot.bottom - dpi(app, control)) * 0.5f;
    };

    // Percentage slider: track + accent fill + knob + value label. The
    // track rect is stored for drag math; the hit rect covers the row.
    auto slider = [&](const D2D1_RECT_F& slot, int pct, int action, int trackSlot) {
        const float x = slot.left, w = slot.right - slot.left;
        const float y = (slot.top + slot.bottom) * 0.5f;
        float trackH = dpi(app, sp4);
        float knobR = dpi(app, sp8);
        float labelW = dpi(app, sp48);
        float trackW = w - labelW;
        D2D1_RECT_F track = D2D1::RectF(x, y - trackH / 2, x + trackW, y + trackH / 2);
        app.settingsSliderTrack[trackSlot] = track;
        app.brush->SetColor(settingsInk(app, 0.18f, anim));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(track, trackH / 2, trackH / 2), app.brush);
        float t = (pct - 30) / 70.0f;
        float fillX = x + trackW * t;
        D2D1_COLOR_F c = themeUiAccent(app.theme); c.a = anim;
        app.brush->SetColor(c);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(x, track.top, fillX, track.bottom),
                              trackH / 2, trackH / 2), app.brush);
        app.renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(fillX, y), knobR, knobR), app.brush);
        wchar_t label[16];
        if (pct >= 100) wcscpy_s(label, tr(app, "settings.full"));
        else swprintf_s(label, L"%d%%", pct);
        const float line = dpi(app, lineHeight(Type::Small));
        settingsText(app, label, Type::Small,
                     D2D1::RectF(x + trackW + dpi(app, sp12), y - line * 0.5f, x + w, y + line * 0.5f),
                     settingsInk(app, inkSecondary, anim), DWRITE_TEXT_ALIGNMENT_TRAILING);
        app.settingsHits.push_back({D2D1::RectF(x - knobR, y - dpi(app, sp12),
                                                x + trackW + knobR, y + dpi(app, sp12)),
                                    action});
    };

    // A dropdown's value box with a pencil beside it
    auto dropdown = [&](const D2D1_RECT_F& slot, const std::wstring& value, bool open, int action,
                        int penAction, D2D1_RECT_F& boxOut) {
        const float h = dpi(app, controlBox);
        const float y = (slot.top + slot.bottom - h) * 0.5f;
        const float penW = dpi(app, controlBox);
        D2D1_RECT_F box = D2D1::RectF(slot.left, y, slot.right - penW - dpi(app, sp8), y + h);
        boxOut = box;
        app.brush->SetColor(settingsInk(app, open ? 0.6f : 0.3f, anim));
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(box, dpi(app, radiusControl), dpi(app, radiusControl)), app.brush, 1.0f);
        const float lineTop = settingsLineTop(app, box.top, h, Type::Body);
        settingsText(app, value.c_str(), Type::Body,
                     D2D1::RectF(box.left + dpi(app, sp12), lineTop, box.right - dpi(app, sp24), box.bottom),
                     settingsInk(app, inkPrimary, anim));
        settingsText(app, L"\x25BE", Type::Body,
                     D2D1::RectF(box.right - dpi(app, sp24), lineTop, box.right - dpi(app, sp8), box.bottom),
                     settingsInk(app, inkSecondary, anim), DWRITE_TEXT_ALIGNMENT_CENTER);
        app.settingsHits.push_back({box, action});

        D2D1_RECT_F pen = D2D1::RectF(box.right + dpi(app, sp8), y, slot.right, y + h);
        settingsText(app, L"\x270E", Type::Body, D2D1::RectF(pen.left, lineTop, pen.right, pen.bottom),
                     settingsInk(app, inkSecondary, anim), DWRITE_TEXT_ALIGNMENT_CENTER);
        app.settingsHits.push_back({pen, penAction});
    };

    // A segmented switch in a sunken well: one segment lifted
    auto segments = [&](const D2D1_RECT_F& slot, const wchar_t* const* labels, const int* actions, int count,
                        int current, bool equal) {
        const float inset = dpi(app, sp2);
        const float h = dpi(app, controlBox);
        const float y = (slot.top + slot.bottom - h) * 0.5f;
        float widest = 0.0f;
        for (int s = 0; s < count; s++) {
            widest = std::max(widest, settingsTextWidth(app, labels[s], Type::Body) + dpi(app, sp12) * 2);
        }
        D2D1_RECT_F well = D2D1::RectF(slot.left, y, slot.right, y + h);
        app.brush->SetColor(themeUiDark(app.theme) ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f * anim)
                                                    : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.07f * anim));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(well, dpi(app, radiusControl), dpi(app, radiusControl)), app.brush);
        float sx = well.left + inset;
        for (int s = 0; s < count; s++) {
            const float segW = equal ? widest
                                     : settingsTextWidth(app, labels[s], Type::Body) + dpi(app, sp12) * 2;
            D2D1_RECT_F sr = D2D1::RectF(sx, well.top + inset, std::min(sx + segW, well.right - inset),
                                         well.bottom - inset);
            const bool active = s == current;
            if (active) {
                app.brush->SetColor(settingsLift(app, 0.22f, 1.0f, anim));
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(sr, dpi(app, sp2), dpi(app, sp2)), app.brush);
            }
            settingsText(app, labels[s], active ? Type::Strong : Type::Body,
                         D2D1::RectF(sr.left, settingsLineTop(app, sr.top, sr.bottom - sr.top, Type::Body),
                                     sr.right, sr.bottom),
                         settingsInk(app, active ? inkPrimary : inkSecondary, anim),
                         DWRITE_TEXT_ALIGNMENT_CENTER);
            app.settingsHits.push_back({sr, actions[s]});
            sx += segW + inset;
        }
    };

    if (app.settingsSection == 0) {  // General
        {
            const D2D1_RECT_F slot = row(tr(app, "settings.language"), tr(app, "settings.language.hint"));
            std::wstring current = app.languageSetting < 0
                ? std::wstring(tr(app, "settings.lang.auto"))
                : std::wstring(languageNameAt(app.languageSetting));
            dropdown(slot, current, app.settingsLangOpen, SET_LANG_DROPDOWN, SET_OPEN_LANGS_INI,
                     app.settingsLangBox);
        }
        {
            // The pencil opens the shortcut editor
            const D2D1_RECT_F slot = row(tr(app, "settings.shortcuts"), tr(app, "settings.shortcuts.hint"));
            const char* profKey = app.keyProfile == "vim" ? "settings.profile.vim"
                : app.keyProfile == "custom" ? "settings.profile.custom"
                                             : "settings.profile.windows";
            dropdown(slot, tr(app, profKey), app.settingsKeysOpen, SET_KEYS_DROPDOWN, SET_EDIT_KEYS,
                     app.settingsKeysBox);
        }
        toggleRow("settings.folder_search", "settings.folder_search.hint",
                  app.folderSearchEnabled, SET_TOGGLE_FOLDERSEARCH);
        toggleRow("settings.browse_focus_path", "settings.browse_focus_path.hint",
                  app.browserFocusPath, SET_TOGGLE_BROWSEFOCUS);
        toggleRow("settings.open_in_tabs", "settings.open_in_tabs.hint",
                  app.openInTabs, SET_TOGGLE_OPENTABS);
        toggleRow("settings.keep_deleted_tabs", "settings.keep_deleted_tabs.hint",
                  app.keepDeletedTabs, SET_TOGGLE_KEEPDELETED);
        {
            // The three ini files that can be edited by hand
            const D2D1_RECT_F slot = row(nullptr, nullptr);
            float bx = slot.left;
            const float chipY = chipsTop(slot);
            settingsChip(app, bx, chipY, L"settings.ini", false, SET_OPEN_INI, anim);
            settingsChip(app, bx, chipY, L"themes.ini", false, SET_OPEN_THEMES_INI, anim);
            settingsChip(app, bx, chipY, L"languages.ini", false, SET_OPEN_LANGS_INI, anim);
        }
    } else if (app.settingsSection == 1) {  // Appearance
        slider(row(tr(app, "settings.reading_width_window"), tr(app, "settings.reading_width_window.hint")),
               app.readingWidthPct, SET_SLIDER_READING, 0);
        slider(row(tr(app, "settings.reading_width_full"), tr(app, "settings.reading_width_full.hint")),
               app.zenWidthPct, SET_SLIDER_ZEN, 1);
        toggleRow("settings.heading_rules", "settings.heading_rules.hint",
                  app.headingRules, SET_TOGGLE_HEADRULES);
        {
            // Folding sections (#316): Off, On (arrows beside headings), or
            // Folded (documents open with every section folded). Each
            // segment as wide as its own word, so a long one (Eingeklappt)
            // leaves the label its room.
            const int actions[3] = {SET_FOLD_OFF, SET_FOLD_ON, SET_FOLD_FOLDED};
            const int current = !app.foldSections ? 0 : app.foldOnOpen ? 2 : 1;
            const D2D1_RECT_F slot = row(tr(app, "settings.fold_sections"), tr(app, "settings.fold_sections.hint"));
            segments(slot, settingsFoldLabels(app), actions, 3, current, false);
        }
        toggleRow("settings.follow_windows", "settings.follow_windows.hint",
                  app.followSystemTheme, SET_TOGGLE_FOLLOW);
        {
            const D2D1_RECT_F slot = row(tr(app, "settings.themes"), tr(app, "settings.themes.hint"));
            float bx = slot.left;
            const float chipY = chipsTop(slot);
            settingsChip(app, bx, chipY, tr(app, "settings.browse"), false, SET_OPEN_THEMES, anim);
            settingsChip(app, bx, chipY, tr(app, "settings.edit"), false, SET_EDIT_THEME, anim);
            settingsChip(app, bx, chipY, tr(app, "settings.import"), false, SET_IMPORT_THEME, anim);
            // The create action is the accent-filled primary button
            settingsChip(app, bx, chipY, tr(app, "settings.new"), true, SET_NEW_THEME, anim);
        }
        {
            const wchar_t* labels[2] = {tr(app, "settings.toc.left"), tr(app, "settings.toc.right")};
            const int actions[2] = {SET_TOC_LEFT, SET_TOC_RIGHT};
            const D2D1_RECT_F slot = row(tr(app, "settings.toc_side"), tr(app, "settings.toc_side.hint"));
            segments(slot, labels, actions, 2, app.tocOnLeft ? 0 : 1, true);
        }
        // Bottom-right messages (#273): warnings and questions always show
        toggleRow("settings.notifications", "settings.notifications.hint",
                  app.showNotifications, SET_TOGGLE_NOTIFICATIONS);
    } else {  // Editor
        toggleRow("settings.word_wrap", "settings.word_wrap.hint", app.editorWordWrap, SET_TOGGLE_WRAP);
        toggleRow("settings.preview_pane", "settings.preview_pane.hint",
                  app.editorShowPreview, SET_TOGGLE_PREVIEW);
        toggleRow("settings.editor_assists", "settings.editor_assists.hint",
                  app.editorAssists, SET_TOGGLE_ASSISTS);
        toggleRow("settings.highlight", "settings.highlight.hint", app.editorHighlight, SET_TOGGLE_HIGHLIGHT);
        toggleRow("settings.word_count", "settings.word_count.hint", app.showWordCount, SET_TOGGLE_WORDCOUNT);
        toggleRow("settings.close_drafts", "settings.close_drafts.hint",
                  app.closeKeepsDrafts, SET_TOGGLE_CLOSE_DRAFTS);
        {
            // Pandoc bridge: hint shows the resolved executable when found
            pandocResolve(app);
            const D2D1_RECT_F slot = row(tr(app, "settings.pandoc"),
                                         app.pandocExe.empty() ? tr(app, "settings.pandoc.hint")
                                                               : app.pandocExe.c_str());
            float bx = slot.left;
            settingsChip(app, bx, chipsTop(slot), tr(app, "settings.browse"), false, SET_LOCATE_PANDOC, anim);
        }
        {
            // PlantUML bridge: hint shows the resolved tool when found
            plantumlResolve(app);
            const std::wstring tool = app.plantumlTool.available ? app.plantumlTool.describe() : L"";
            const D2D1_RECT_F slot = row(tr(app, "settings.plantuml"),
                                         app.plantumlTool.available ? tool.c_str()
                                                                    : tr(app, "settings.plantuml.hint"));
            float bx = slot.left;
            settingsChip(app, bx, chipsTop(slot), tr(app, "settings.browse"), false, SET_LOCATE_PLANTUML, anim);
        }
    }
    app.renderTarget->PopAxisAlignedClip();

    // What scrolled out of the page's room cannot be clicked
    for (size_t i = firstPageHit; i < app.settingsHits.size(); i++) {
        D2D1_RECT_F& r = app.settingsHits[i].first;
        r.top = std::max(r.top, viewport.top);
        r.bottom = std::min(r.bottom, viewport.bottom);
    }
    app.settingsHits.erase(
        std::remove_if(app.settingsHits.begin() + firstPageHit, app.settingsHits.end(),
                       [](const auto& hit) { return hit.first.bottom <= hit.first.top; }),
        app.settingsHits.end());
    if (app.settingsScrollMax > 0.0f) {
        const float room = viewport.bottom - viewport.top;
        const float thumb = std::max(dpi(app, sp24), room * room / pageH);
        const float ty = viewport.top + app.settingsScroll / app.settingsScrollMax * (room - thumb);
        const float tx = viewport.right + dpi(app, sp8);
        app.brush->SetColor(settingsInk(app, 0.22f, anim));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + dpi(app, sp4), ty + thumb), dpi(app, sp2), dpi(app, sp2)),
            app.brush);
    }

    // A dropdown's list, drawn last so that it sits above the rows; the
    // hit test goes through settingsHits in reverse, so its rows win
    auto popupList = [&](const D2D1_RECT_F& box, const std::vector<std::wstring>& labels, int active,
                         const std::vector<int>& actions) {
        const float itemH = dpi(app, rowList);
        const int count = (int)labels.size();
        float popupW = box.right - box.left;
        for (const std::wstring& label : labels) {
            popupW = std::max(popupW, settingsTextWidth(app, label.c_str(), Type::Body) + dpi(app, sp12 + sp32));
        }
        popupW = std::min(popupW, (float)app.width - dpi(app, sp32));
        float popupRight = std::min(box.right, (float)app.width - dpi(app, sp16));
        float popupLeft = popupRight - popupW;
        if (popupLeft < dpi(app, sp16)) {
            popupLeft = dpi(app, sp16);
            popupRight = popupLeft + popupW;
        }
        const float listH = itemH * count + dpi(app, sp4) * 2;
        float popupTop = box.bottom + dpi(app, sp4);
        // Above its box when there is no room under it
        if (popupTop + listH > py + panelH - dpi(app, sp16) &&
            box.top - dpi(app, sp4) - listH >= py + dpi(app, sp16)) {
            popupTop = box.top - dpi(app, sp4) - listH;
        }
        D2D1_RECT_F pop = D2D1::RectF(popupLeft, popupTop, popupRight, popupTop + listH);
        D2D1_COLOR_F pc = app.theme.background; pc.a = anim;
        app.brush->SetColor(themeUiPanel(app.theme, pc));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(pop, dpi(app, radiusCard), dpi(app, radiusCard)), app.brush);
        app.brush->SetColor(themeUiBorder(app.theme, settingsInk(app, 0.35f, anim), anim));
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(pop, dpi(app, radiusCard), dpi(app, radiusCard)), app.brush, 1.0f);
        for (int r = 0; r < count; r++) {
            float ry = pop.top + dpi(app, sp4) + r * itemH;
            D2D1_RECT_F item = D2D1::RectF(pop.left, ry, pop.right, ry + itemH);
            const bool on = r == active;
            const bool hovered = app.mouseX >= item.left && app.mouseX <= item.right &&
                                 app.mouseY >= item.top && app.mouseY <= item.bottom;
            if (on || hovered) {
                D2D1_COLOR_F hl = themeUiAccent(app.theme);
                hl.a = (on ? 0.18f : 0.10f) * anim;
                app.brush->SetColor(hl);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(item.left + dpi(app, sp4), item.top,
                                                  item.right - dpi(app, sp4), item.bottom),
                                      dpi(app, radiusControl), dpi(app, radiusControl)), app.brush);
            }
            const float lineTop = settingsLineTop(app, item.top, itemH, Type::Body);
            settingsText(app, labels[r].c_str(), Type::Body,
                         D2D1::RectF(item.left + dpi(app, sp12), lineTop, item.right - dpi(app, sp32),
                                     item.bottom),
                         settingsInk(app, inkPrimary, anim));
            if (on) {
                D2D1_COLOR_F check = themeUiAccent(app.theme); check.a = anim;
                settingsText(app, L"\x2713", Type::Body,
                             D2D1::RectF(item.right - dpi(app, sp32), lineTop, item.right - dpi(app, sp8),
                                         item.bottom),
                             check, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            app.settingsHits.push_back({item, actions[r]});
        }
    };

    if (app.settingsKeysOpen && app.settingsSection == 0) {
        const char* rowIds[] = {"windows", "vim", "custom"};
        std::vector<std::wstring> labels = {tr(app, "settings.profile.windows"),
                                            tr(app, "settings.profile.vim"),
                                            tr(app, "settings.profile.custom")};
        int active = -1;
        std::vector<int> actions;
        for (int i = 0; i < 3; i++) {
            if (app.keyProfile == rowIds[i]) active = i;
            actions.push_back(SET_KEYS_PICK_BASE + i);
        }
        popupList(app.settingsKeysBox, labels, active, actions);
    }
    if (app.settingsLangOpen && app.settingsSection == 0) {
        // Auto plus only the languages that actually carry translations;
        // untranslated roster entries stay out of the list until
        // languages.ini fills them in.
        std::vector<std::wstring> labels = {tr(app, "settings.lang.auto")};
        // -1 (Auto) lands on SET_LANG_PICK_BASE; input.cpp decodes
        // pick - 1 as the registry index, so gaps are fine.
        std::vector<int> actions = {SET_LANG_PICK_BASE};
        int active = app.languageSetting < 0 ? 0 : -1;
        for (int i = 0; i < languageCount(); i++) {
            if (!languageHasTranslations(i)) continue;
            if (app.languageSetting == i) active = (int)labels.size();
            labels.push_back(languageNameAt(i));
            actions.push_back(SET_LANG_PICK_BASE + 1 + i);
        }
        popupList(app.settingsLangBox, labels, active, actions);
    }

    // Footer hint
    {
        const float line = dpi(app, lineHeight(Type::Small));
        const float top = py + panelH - pad - line;
        settingsText(app, tr(app, "settings.footer"), Type::Small,
                     D2D1::RectF(px + pad, top, px + panelW - pad, top + line),
                     settingsInk(app, inkSecondary, anim));
    }
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
            (themeUiDark(app.theme) ? 0.18f : 0.08f) / (float)(ring * ring));
        app.brush->SetColor(shadow);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(r.left - spread, r.top - spread * 0.3f,
                                          r.right + spread, r.bottom + spread),
                              radius + spread, radius + spread),
            app.brush);
    }
}

D2D1_COLOR_F promptChipSurface(const App& app) {
    // The side panels and the prompts: a CSS theme's --ui-panel, else the
    // page lifted toward white
    if (app.theme.uiPanel) return themeUiPanel(app.theme, D2D1::ColorF(0, 0, 0, 0.98f));
    D2D1_COLOR_F bg = app.theme.background;
    float t = app.theme.isDark ? 0.012f : 0.31f;
    bg.r += (1.0f - bg.r) * t;
    bg.g += (1.0f - bg.g) * t;
    bg.b += (1.0f - bg.b) * t;
    bg.a = 0.98f;
    return bg;
}

// One keycap; returns the width consumed (drawn right-to-left caller side)
float promptKeycap(App& app, const wchar_t* label, float rightX,
                   float cy) {
    if (!app.signalSmallFormat) return 0.0f;
    float tw = measureText(app, label, app.signalSmallFormat);
    float w = tw + dpi(app, 9.0f);
    float h = dpi(app, 15.0f);
    D2D1_RECT_F r = D2D1::RectF(rightX - w, cy - h * 0.5f, rightX, cy + h * 0.5f);
    D2D1_COLOR_F c = themeUiText(app.theme); c.a = 0.25f;
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
    // Asking about a file changed on disk under unsaved edits (#311)
    const bool external = app.confirmExitExternal;
    std::wstring title = tr(app, external ? "changed.title" : "confirm.title");
    if (!name.empty()) title += L" \x2014 " + name;

    std::wstring saveLbl = tr(app, external ? "changed.reload" : "confirm.save");
    std::wstring discardLbl = tr(app, external ? "changed.keep" : "confirm.discard");
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
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.45f;
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
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.95f;
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
        D2D1_COLOR_F c = themeUiAccent(base); c.a = 0.94f;
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
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.85f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(discardLbl.c_str(), (UINT32)discardLbl.size(),
            fmt,
            D2D1::RectF(br.left + dpi(app, 10.0f), br.top + dpi(app, 3.0f),
                        br.right, br.bottom), app.brush);
        app.confirmExitHits.push_back({br, 2});
    }
    if (app.signalSmallFormat) {
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.42f;
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
    D2D1_COLOR_F hue = themeUiAccent(base);

    // The same dialog asks before a link's target is opened
    std::wstring title = tr(app, app.createRefOpens ? "openlink.title" : "createref.title");
    std::wstring createLbl = tr(app, app.createRefOpens ? "openlink.open" : "createref.create");
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
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.45f;
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
            D2D1_COLOR_F c = themeUiText(base); c.a = 0.95f;
            app.brush->SetColor(c);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(ib.right + dpi(app, 9.0f),
                              row1cy - m.height * 0.5f),
                tl, app.brush);
            tl->Release();
        }
    }

    // Row 2: the resolved target path, quiet. A long one keeps its end, the
    // file's name: that is what the question is about
    if (app.signalSmallFormat) {
        const float room = r.right - pad - (ib.right + dpi(app, 9.0f));
        if (measureText(app, path, app.signalSmallFormat) > room) {
            const std::wstring dots(1, (wchar_t)0x2026);
            size_t low = 1, high = path.size();  // the letters left off its start
            while (low < high) {
                const size_t middle = (low + high) / 2;
                if (measureText(app, dots + path.substr(middle), app.signalSmallFormat) > room) low = middle + 1;
                else high = middle;
            }
            path = dots + path.substr(low);
        }
        IDWriteTextLayout* pl = nullptr;
        app.dwriteFactory->CreateTextLayout(
            path.c_str(), (UINT32)path.size(), app.signalSmallFormat,
            room, dpi(app, 14.0f), &pl);
        if (pl) {
            pl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trimOpt{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            pl->SetTrimming(&trimOpt, nullptr);
            D2D1_COLOR_F c = themeUiText(base); c.a = 0.5f;
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
        D2D1_COLOR_F c = themeUiAccent(base); c.a = 0.94f;
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
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.3f;
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
    app.brush->SetColor(themeUiPanel(base, bg));
    app.renderTarget->FillRoundedRectangle(panel, app.brush);
    D2D1_COLOR_F border = themeUiText(base); border.a = 0.25f;
    app.brush->SetColor(themeUiBorder(base, border));
    app.renderTarget->DrawRoundedRectangle(panel, app.brush, 1.0f);

    if (app.themeTitleFormat) {
        app.themeTitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F tc = themeUiHeading(base); tc.a = 1.0f;
        app.brush->SetColor(tc);
        const wchar_t* title = tr(app, "shortcuts.editor.title");
        app.renderTarget->DrawText(title, (UINT32)wcslen(title), app.themeTitleFormat,
            D2D1::RectF(px + dpi(app, 24.0f), py + dpi(app, 16.0f),
                        px + panelW, py + dpi(app, 52.0f)), app.brush);
    }
    {
        D2D1_COLOR_F hc = themeUiText(base); hc.a = 0.55f;
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
            D2D1_COLOR_F hl = themeUiAccent(base); hl.a = 0.18f;
            app.brush->SetColor(hl);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(row, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush);
        }
        const wchar_t* name = tr(app, shortcutActionNameKey(i));
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.9f;
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
        c = themeUiText(base); c.a = armed ? 0.6f : 0.3f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(chip, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush,
            armed ? 1.5f : 1.0f);
        c = themeUiText(base); c.a = 0.95f;
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
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.3f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(chip, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush, 1.0f);
        c = themeUiText(base); c.a = 0.8f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(reset, (UINT32)wcslen(reset), fmt,
            D2D1::RectF(chip.left + dpi(app, 8.0f), chip.top + dpi(app, 2.0f),
                        chip.right, chip.bottom), app.brush);
        app.shortcutHits.push_back({chip, 100});
    }
    {
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.4f;
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

    float panelW = std::min(dpi(app, 760.0f), app.width - dpi(app, 24.0f));
    float panelH = std::min(dpi(app, 520.0f), app.height - dpi(app, 24.0f));
    float px = (app.width - panelW) / 2, py = (app.height - panelH) / 2;
    D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(
        D2D1::RectF(px, py, px + panelW, py + panelH), dpi(app, 12.0f), dpi(app, 12.0f));
    D2D1_COLOR_F bg = base.background; bg.a = 0.99f;
    app.brush->SetColor(themeUiPanel(base, bg));
    app.renderTarget->FillRoundedRectangle(panel, app.brush);
    D2D1_COLOR_F border = themeUiText(base); border.a = 0.25f;
    app.brush->SetColor(themeUiBorder(base, border));
    app.renderTarget->DrawRoundedRectangle(panel, app.brush, 1.0f);

    if (app.themeTitleFormat) {
        app.themeTitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        D2D1_COLOR_F tc = themeUiHeading(base); tc.a = 1.0f;
        app.brush->SetColor(tc);
        const wchar_t* title = tr(app, "theme.editor.title");
        app.renderTarget->DrawText(title, (UINT32)wcslen(title), app.themeTitleFormat,
            D2D1::RectF(px + dpi(app, 24.0f), py + dpi(app, 16.0f),
                        px + panelW, py + dpi(app, 52.0f)), app.brush);
    }

    float x0 = px + dpi(app, 24.0f);
    float colW = panelW * 0.43f - dpi(app, 36.0f);
    float y = py + dpi(app, 64.0f);

    auto inputBox = [&](float bx, float by, float bw, const std::wstring& text,
                        bool focused, int action) {
        D2D1_RECT_F r = D2D1::RectF(bx, by, bx + bw, by + dpi(app, 26.0f));
        D2D1_COLOR_F c = themeUiText(base); c.a = focused ? 0.6f : 0.25f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(r, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush,
            focused ? 1.5f : 1.0f);
        std::wstring shown = text;
        if (shown.empty() && action >= TE_FIELD_H1 && action <= TE_FIELD_HIGHLIGHT_TEXT && !focused)
            shown = tr(app, "theme.editor.automatic");
        if (focused) shown += L"_";
        c = themeUiText(base); c.a = 0.95f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(shown.c_str(), (UINT32)shown.size(), fmt,
            D2D1::RectF(r.left + dpi(app, 8.0f), r.top + dpi(app, 4.0f),
                        r.right - dpi(app, 4.0f), r.bottom), app.brush);
        app.themeEditorHits.push_back({r, action});
    };
    auto label = [&](float lx, float ly, const wchar_t* text, float alpha) {
        D2D1_COLOR_F c = themeUiText(base); c.a = alpha;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(text, (UINT32)wcslen(text), fmt,
            D2D1::RectF(lx, ly, lx + colW, ly + dpi(app, 18.0f)), app.brush);
    };

    // Name
    label(x0, y, tr(app, "theme.editor.name"), 0.55f);
    inputBox(x0, y + dpi(app, 20.0f), colW, app.themeEditorName,
             app.themeEditorField == 14, TE_FIELD_NAME);
    y += dpi(app, 56.0f);

    // Base theme cycler + dark toggle on one row
    label(x0, y, tr(app, "theme.editor.based_on"), 0.55f);
    {
        float by = y + dpi(app, 20.0f);
        D2D1_RECT_F prev = D2D1::RectF(x0, by, x0 + dpi(app, 26.0f), by + dpi(app, 26.0f));
        D2D1_RECT_F next = D2D1::RectF(x0 + colW - dpi(app, 26.0f), by,
                                       x0 + colW, by + dpi(app, 26.0f));
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.7f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(L"\x2039", 1, fmt,
            D2D1::RectF(prev.left + dpi(app, 9.0f), prev.top + dpi(app, 3.0f),
                        prev.right, prev.bottom), app.brush);
        app.renderTarget->DrawText(L"\x203A", 1, fmt,
            D2D1::RectF(next.left + dpi(app, 9.0f), next.top + dpi(app, 3.0f),
                        next.right, next.bottom), app.brush);
        const D2DTheme& bt = themeAt(app.themeEditorBase);
        c = themeUiText(base); c.a = 0.95f;
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
        float tx = x0 + colW - dpi(app, 34.0f), ty = y;
        float tw = dpi(app, 34.0f), th = dpi(app, 18.0f);
        bool on = work.isDark;
        D2D1_COLOR_F track = on ? themeUiAccent(base) : themeUiText(base);
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

    // Scrollable colour controls keep the optional six-level palette compact.
    auto clip = D2D1::RectF(x0, y, x0 + colW, py + panelH - dpi(app, 60.0f));
    app.themeEditorColorListRect = clip;
    float rowH = dpi(app, 36.0f);
    float total = rowH * (app.themeEditorHeadingsOpen ? 15.0f : 9.0f);
    float maximum = std::max(0.0f, total - (clip.bottom - clip.top));
    app.themeEditorColorScroll = std::min(app.themeEditorColorScroll, maximum);
    float rowY = y - app.themeEditorColorScroll;
    size_t firstHit = app.themeEditorHits.size();
    app.renderTarget->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    auto colorField = [&](int field, const wchar_t* title, D2D1_COLOR_F color) {
        float fieldX = x0 + colW - dpi(app, 126.0f);
        label(x0, rowY + dpi(app, 5.0f), title, 0.95f);
        inputBox(fieldX, rowY, dpi(app, 86.0f), app.themeEditorHex[field],
                 app.themeEditorField == field, TE_FIELD_BG + field);
        auto swatch = D2D1::RectF(x0 + colW - dpi(app, 32.0f), rowY,
                                x0 + colW - dpi(app, 8.0f), rowY + dpi(app, 26.0f));
        app.brush->SetColor(color);
        app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(swatch, 4, 4), app.brush);
        rowY += rowH;
    };
    static const char* keys[6] = {"theme.editor.color.background", "theme.editor.color.text",
        "theme.editor.color.heading", "theme.editor.color.link", "theme.editor.color.accent",
        "theme.editor.color.code_background"};
    const D2D1_COLOR_F colors[6] = {work.background, work.text, work.heading, work.link, work.accent, work.codeBackground};
    for (int i = 0; i < 6; ++i) colorField(i, tr(app, keys[i]), colors[i]);
    std::wstring disclosure = (app.themeEditorHeadingsOpen ? L"- " : L"+ ") +
        std::wstring(tr(app, "theme.editor.heading_colors"));
    label(x0, rowY + dpi(app, 5.0f), disclosure.c_str(), 0.9f);
    app.themeEditorHits.push_back({D2D1::RectF(x0, rowY, x0 + colW, rowY + rowH), TE_HEADINGS});
    rowY += rowH;
    if (app.themeEditorHeadingsOpen)
        for (int i = 0; i < 6; ++i) {
            std::wstring title = L"H" + std::to_wstring(i + 1);
            colorField(i + 6, title.c_str(), themeHeadingColor(work, i + 1));
        }
    colorField(12, tr(app, "theme.editor.highlight_bg"), themeHighlightBackground(work));
    colorField(13, tr(app, "theme.editor.highlight_text"), work.highlightText.value_or(work.text));
    app.renderTarget->PopAxisAlignedClip();
    // Invisible fields must not catch clicks in the footer or other controls.
    for (size_t i = firstHit; i < app.themeEditorHits.size();) {
        auto& rect = app.themeEditorHits[i].first;
        rect.top = std::max(rect.top, clip.top); rect.bottom = std::min(rect.bottom, clip.bottom);
        if (rect.top >= rect.bottom) app.themeEditorHits.erase(app.themeEditorHits.begin() + i);
        else ++i;
    }
    if (maximum > 0) {
        float height = clip.bottom - clip.top;
        float thumb = std::max(dpi(app, 20.0f), height * height / total);
        float top = clip.top + (height - thumb) * app.themeEditorColorScroll / maximum;
        auto color = themeUiText(base); color.a = 0.3f; app.brush->SetColor(color);
        app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(clip.right - dpi(app, 3.0f), top, clip.right, top + thumb), 2, 2), app.brush);
    }

    // ---- Right column: specimen + font list ----
    float xr = x0 + colW + dpi(app, 28.0f);
    float xrw = px + panelW - dpi(app, 24.0f) - xr;

    // Live ink specimen in the working theme + font
    {
        D2D1_RECT_F spec = D2D1::RectF(xr, py + dpi(app, 64.0f),
                                       xr + xrw, py + dpi(app, 218.0f));
        app.brush->SetColor(work.background);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(spec, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush);
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.2f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(spec, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush, 1.0f);

        // A face this PC lacks shows as the one the theme will draw with:
        // a stand-in, or for headings the body's
        const wchar_t* fam = installedFace(app, app.themeEditorFont.empty()
            ? L"Segoe UI" : app.themeEditorFont.c_str(), false);
        const wchar_t* heading = app.themeEditorHeadingFont.c_str();
        const wchar_t* headingFam =
            heading[0] && installedFace(app, heading, false) == heading ? heading : fam;
        const wchar_t* codeFam = installedFace(app, app.themeEditorCodeFont.empty()
            ? L"Consolas" : app.themeEditorCodeFont.c_str(), true);
        IDWriteTextFormat* h = nullptr; IDWriteTextFormat* b = nullptr;
        IDWriteTextFormat* m = nullptr;
        app.dwriteFactory->CreateTextFormat(headingFam, nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, dpi(app, 20.0f),
            L"en-us", &h);
        app.dwriteFactory->CreateTextFormat(fam, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, dpi(app, 13.5f),
            L"en-us", &b);
        app.dwriteFactory->CreateTextFormat(codeFam, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, dpi(app, 12.5f),
            L"en-us", &m);
        float sx = spec.left + dpi(app, 18.0f);
        float sw2 = spec.right - dpi(app, 18.0f) - sx;
        if (h) {
            app.brush->SetColor(themeHeadingColor(work, 1));
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
            auto marker = D2D1::RectF(sx + dpi(app, 174.0f), spec.top + dpi(app, 96.0f),
                                     spec.right - dpi(app, 10.0f), spec.top + dpi(app, 117.0f));
            app.brush->SetColor(themeHighlightBackground(work));
            app.renderTarget->FillRectangle(marker, app.brush);
            app.brush->SetColor(work.highlightText.value_or(work.text));
            app.renderTarget->DrawText(L"Highlight", 9, b, marker, app.brush);
            b->Release();
        }
        if (m) {
            D2D1_RECT_F pill = D2D1::RectF(sx, spec.top + dpi(app, 122.0f),
                                           sx + dpi(app, 128.0f), spec.top + dpi(app, 142.0f));
            app.brush->SetColor(work.codeBackground);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(pill, dpi(app, 4.0f), dpi(app, 4.0f)), app.brush);
            app.brush->SetColor(themeInlineCodeColor(work));
            app.renderTarget->DrawText(L"inline_code()", 13, m,
                D2D1::RectF(pill.left + dpi(app, 8.0f), pill.top + dpi(app, 2.0f),
                            pill.right, pill.bottom), app.brush);
            m->Release();
        }
    }

    // Which face the font list sets: text, headings or code (#256), drawn
    // like the settings' segmented controls
    {
        const char* roleKeys[3] = {"theme.editor.font.text", "theme.editor.font.headings",
                                   "theme.editor.font.code"};
        float inset = dpi(app, 3.0f);
        float segH = dpi(app, 24.0f);
        float segY = py + dpi(app, 230.0f);
        float segW = (xrw - inset * 4) / 3.0f;
        D2D1_RECT_F container = D2D1::RectF(xr, segY - inset, xr + xrw, segY + segH + inset);
        app.brush->SetColor(themeUiDark(base) ? D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.22f)
                                              : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.07f));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(container, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush);
        for (int i = 0; i < 3; i++) {
            float sx = container.left + inset + i * (segW + inset);
            D2D1_RECT_F r = D2D1::RectF(sx, segY, sx + segW, segY + segH);
            bool on = app.themeEditorFontRole == i;
            if (on) {
                app.brush->SetColor(settingsLift(app, 0.22f, 1.0f, 1.0f));
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(r, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
            }
            const wchar_t* text = tr(app, roleKeys[i]);
            D2D1_COLOR_F c = themeUiText(base); c.a = on ? 0.95f : 0.55f;
            app.brush->SetColor(c);
            float tw = measureText(app, text, fmt);
            app.renderTarget->DrawText(text, (UINT32)wcslen(text), fmt,
                D2D1::RectF(sx + (segW - tw) * 0.5f, segY + dpi(app, 3.0f), sx + segW, segY + segH),
                app.brush);
            app.themeEditorHits.push_back({r, TE_FONT_TEXT + i});
        }
    }

    // Font list: every system family, drawn in itself. For headings the
    // first row follows the text face.
    {
        D2D1_RECT_F list = D2D1::RectF(xr, py + dpi(app, 262.0f),
                                       xr + xrw, py + panelH - dpi(app, 60.0f));
        app.themeEditorFontListRect = list;
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.2f;
        app.brush->SetColor(c);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(list, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush, 1.0f);
        app.renderTarget->PushAxisAlignedClip(list, D2D1_ANTIALIAS_MODE_ALIASED);

        const std::wstring& chosen = app.themeEditorFontRole == 1 ? app.themeEditorHeadingFont
                                   : app.themeEditorFontRole == 2 ? app.themeEditorCodeFont
                                   : app.themeEditorFont;
        const int lead = app.themeEditorFontRole == 1 ? 1 : 0;
        const int rows = (int)app.systemFontFamilies.size() + lead;
        float fontRowHeight = dpi(app, 26.0f);
        float maxScroll = std::max(0.0f,
            (float)rows * fontRowHeight - (list.bottom - list.top));
        if (app.themeEditorFontScroll < 0.0f) {
            // Opening the editor, its base arrows and Text / Headings / Code
            // centre the chosen face, here where the list's height is known
            // (#284)
            int row = 0;
            for (size_t i = 0; !chosen.empty() && i < app.systemFontFamilies.size(); i++) {
                if (_wcsicmp(app.systemFontFamilies[i].c_str(), chosen.c_str()) == 0) {
                    row = (int)i + lead;
                    break;
                }
            }
            app.themeEditorFontScroll = row * fontRowHeight - (list.bottom - list.top - fontRowHeight) / 2;
        }
        app.themeEditorFontScroll = std::max(0.0f, std::min(app.themeEditorFontScroll, maxScroll));
        int first = (int)(app.themeEditorFontScroll / fontRowHeight);
        int visible = (int)((list.bottom - list.top) / fontRowHeight) + 2;
        for (int i = first; i < first + visible && i < rows; i++) {
            float ry = list.top + i * fontRowHeight - app.themeEditorFontScroll;
            const bool same = lead && i == 0;
            const std::wstring fam = same ? (app.themeEditorFont.empty() ? std::wstring(L"Segoe UI")
                                                                         : app.themeEditorFont)
                                          : app.systemFontFamilies[i - lead];
            const std::wstring shown = same ? std::wstring(tr(app, "theme.editor.font.same")) : fam;
            bool selected = same ? chosen.empty() : _wcsicmp(fam.c_str(), chosen.c_str()) == 0;
            if (selected) {
                D2D1_COLOR_F hl = themeUiAccent(base); hl.a = 0.18f;
                app.brush->SetColor(hl);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(list.left + 1, ry, list.right - 1, ry + fontRowHeight), app.brush);
            }
            IDWriteTextFormat* ff = nullptr;
            app.dwriteFactory->CreateTextFormat(fam.c_str(), nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, dpi(app, 14.0f), L"en-us", &ff);
            D2D1_COLOR_F tc = themeUiText(base); tc.a = selected ? 1.0f : 0.8f;
            app.brush->SetColor(tc);
            if (ff) {
                app.renderTarget->DrawText(shown.c_str(), (UINT32)shown.size(), ff,
                    D2D1::RectF(list.left + dpi(app, 12.0f), ry + dpi(app, 3.0f),
                                list.right - dpi(app, 8.0f), ry + fontRowHeight), app.brush);
                ff->Release();
            }
            // Clamp the hit rect to the visible list: rows drawn under the
            // clip must not steal clicks from the buttons below
            D2D1_RECT_F hr = D2D1::RectF(list.left, std::max(ry, list.top),
                                         list.right, std::min(ry + fontRowHeight, list.bottom));
            if (hr.bottom > hr.top) {
                app.themeEditorHits.push_back({hr, same ? TE_FONT_SAME : TE_FONT_BASE + i - lead});
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
        app.brush->SetColor(themeUiAccent(base));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(save, dpi(app, 6.0f), dpi(app, 6.0f)), app.brush);
        D2D1_COLOR_F saveText = themeUiPanel(base, base.background);
        saveText.a = 1.0f;
        app.brush->SetColor(saveText);
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        app.renderTarget->DrawText(saveLabel, (UINT32)wcslen(saveLabel), fmt,
            D2D1::RectF(save.left, save.top + dpi(app, 6.0f), save.right, save.bottom),
            app.brush);
        D2D1_COLOR_F c = themeUiText(base); c.a = 0.4f;
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
        // and everything it does not (syntax palette, blockquote)
        const wchar_t* ini = tr(app, "theme.editor.open_ini");
        D2D1_RECT_F iniR = D2D1::RectF(px + dpi(app, 24.0f), by + dpi(app, 6.0f),
                                       cancel.left - dpi(app, 18.0f), by + bh);
        c = base.link; c.a = 0.85f;
        app.brush->SetColor(c);
        app.renderTarget->DrawText(ini, (UINT32)wcslen(ini), fmt, iniR, app.brush);
        app.themeEditorHits.push_back({iniR, TE_OPEN_INI});
    }
}
