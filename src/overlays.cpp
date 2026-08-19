#include "overlays.h"
#include "utils.h"
#include "d2d_init.h"
#include "print.h"
#include "settings.h"
#include "i18n.h"

#include <chrono>
#include <algorithm>
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
            // Query width is cached — it only changes when the query or the
            // text format changes, not per frame.
            if (app.searchActive && app.cursorBlinkOn) {
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
}

// Item index at a client point, sharing renderFolderBrowser's geometry.
// Clicks must hit-test their own coordinates: the render-time hover index
// lags one paint behind the mouse, so a fast move-and-click would act on
// the previously highlighted row.
int folderItemIndexAt(const App& app, float x, float y) {
    float panelWidth = folderBrowserPanelWidth(app);
    float panelX = -panelWidth * (1.0f - app.folderBrowserAnimation);
    float padding = dpi(app, 12.0f);
    float itemHeight = dpi(app, 28.0f);
    float headerHeight = dpi(app, 40.0f);
    float listStartY =
        chromeTopHeight(app) + padding + headerHeight + dpi(app, 8.0f);
    float panelHeight = (float)app.height;
    float namingOffset =
        app.folderBrowserNaming != 0 ? itemHeight : 0.0f;

    float itemX = panelX + padding;
    float itemW = panelWidth - padding * 2.0f;
    if (x < itemX || x > itemX + itemW) return -1;
    if (y < listStartY || y > panelHeight - padding) return -1;
    float offset =
        y - (listStartY + namingOffset - app.folderBrowserScroll);
    if (offset < 0.0f) return -1;
    int index = (int)(offset / itemHeight);
    if (index < 0 || index >= (int)app.folderItems.size()) return -1;
    return index;
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

    // Panel dimensions (below the title-bar tab strip)
    float panelWidth = folderBrowserPanelWidth(app);
    float panelX = -panelWidth * (1.0f - anim);  // Slide in from left
    float panelY = chromeTopHeight(app);
    float panelHeight = (float)app.height;

    // Semi-transparent backdrop (only on the panel area)
    D2D1_COLOR_F panelBg = app.theme.isDark ? hexColor(0x1E1E1E, 0.95f) : hexColor(0xF5F5F5, 0.95f);
    app.brush->SetColor(panelBg);
    app.renderTarget->FillRectangle(
        D2D1::RectF(panelX, panelY, panelX + panelWidth, panelY + panelHeight), app.brush);

    // Border on the right edge
    D2D1_COLOR_F borderColor = app.theme.isDark ? hexColor(0x3A3A40, 0.8f) : hexColor(0xD0D0D0, 0.8f);
    app.brush->SetColor(borderColor);
    app.renderTarget->DrawLine(
        D2D1::Point2F(panelX + panelWidth, panelY),
        D2D1::Point2F(panelX + panelWidth, panelY + panelHeight),
        app.brush, 1.0f);

    IDWriteTextFormat* browserFormat = app.folderBrowserFormat;
    if (browserFormat) {
        float padding = dpi(app, 12.0f);
        float itemHeight = dpi(app, 28.0f);
        float headerHeight = dpi(app, 40.0f);

        // Current path header — click converts it to an edit box (#52)
        float headerY = panelY + padding;

        auto textWidth = [&](const std::wstring& s) {
            float w = 0.0f;
            IDWriteTextLayout* layout = nullptr;
            if (app.dwriteFactory && SUCCEEDED(app.dwriteFactory->CreateTextLayout(
                    s.c_str(), (UINT32)s.length(), browserFormat,
                    1000000.0f, headerHeight, &layout)) && layout) {
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

        float btnSize = dpi(app, 24.0f);
        float btnGap = dpi(app, 6.0f);
        float fileBtnX = panelX + panelWidth - padding - btnSize;
        float folderBtnX = fileBtnX - btnGap - btnSize;
        float btnY = headerY + (headerHeight - btnSize) / 2 - dpi(app, 6.0f);

        // Path text, edit box, and buttons all center on the button midline
        float headerCenterY = btnY + btnSize / 2;

        if (app.folderBrowserEditingPath) {
            drawInputBox(panelX + padding, panelX + panelWidth - padding,
                         headerCenterY - boxHeight / 2);
        } else {
            D2D1_COLOR_F headerColor = app.theme.heading;
            headerColor.a = anim;

            // Truncate path if too long, keeping the tail (leaf folder) visible:
            // measure with the actual font instead of estimating character widths,
            // which breaks down for CJK and other wide scripts
            std::wstring displayPath = app.folderBrowserPath;
            float maxPathWidth = panelWidth - padding * 2 - (btnSize * 2 + btnGap * 2);

            if (!displayPath.empty() && textWidth(displayPath) > maxPathWidth) {
                std::wstring tail = displayPath;
                while (textWidth(L"..." + tail) > maxPathWidth) {
                    size_t sep = tail.find(L'\\', 1);
                    if (sep == std::wstring::npos) break;  // one long component: ellipsis trimming cuts it
                    tail = tail.substr(sep);
                }
                displayPath = L"..." + tail;
            }

            // Subtle hover backdrop signals the path is clickable
            bool pathHovered = app.mouseX >= panelX + padding - dpi(app, 4.0f) &&
                               app.mouseX < folderBtnX - dpi(app, 2.0f) &&
                               app.mouseY >= btnY && app.mouseY <= btnY + btnSize;
            if (pathHovered) {
                D2D1_COLOR_F hoverColor = app.theme.accent;
                hoverColor.a = 0.12f * anim;
                app.brush->SetColor(hoverColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(panelX + padding - dpi(app, 4.0f), btnY,
                                                  folderBtnX - dpi(app, 2.0f), btnY + btnSize), 4, 4),
                    app.brush);
            }

            app.brush->SetColor(headerColor);
            app.renderTarget->DrawText(displayPath.c_str(), (UINT32)displayPath.length(), browserFormat,
                D2D1::RectF(panelX + padding, headerCenterY - dpi(app, 9.0f),
                            folderBtnX - dpi(app, 4.0f), headerY + headerHeight),
                app.brush);

            // + folder / + file buttons
            auto drawAddButton = [&](float bx, bool isFolder) {
                bool hovered = app.mouseX >= bx && app.mouseX <= bx + btnSize &&
                               app.mouseY >= btnY && app.mouseY <= btnY + btnSize;
                if (hovered) {
                    D2D1_COLOR_F hoverColor = app.theme.accent;
                    hoverColor.a = 0.2f * anim;
                    app.brush->SetColor(hoverColor);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(bx, btnY, bx + btnSize, btnY + btnSize), 4, 4),
                        app.brush);
                }
                float gx = bx + dpi(app, 3.0f);
                float gy = btnY + dpi(app, 6.0f);
                if (isFolder) {
                    D2D1_COLOR_F folderColor = app.theme.isDark ? hexColor(0xE8A848) : hexColor(0xD4941A);
                    folderColor.a = anim;
                    app.brush->SetColor(folderColor);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(gx, gy + dpi(app, 4.0f), gx + dpi(app, 13.0f), gy + dpi(app, 13.0f)), 2, 2),
                        app.brush);
                    app.renderTarget->FillRectangle(
                        D2D1::RectF(gx, gy + dpi(app, 2.0f), gx + dpi(app, 6.0f), gy + dpi(app, 5.0f)),
                        app.brush);
                } else {
                    D2D1_COLOR_F fileColor = app.theme.text;
                    fileColor.a = 0.6f * anim;
                    app.brush->SetColor(fileColor);
                    app.renderTarget->DrawRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(gx + dpi(app, 1.0f), gy, gx + dpi(app, 11.0f), gy + dpi(app, 13.0f)), 1, 1),
                        app.brush, 1.0f);
                }
                // Accent "+" badge in the top-right corner of the button
                float px = bx + btnSize - dpi(app, 7.0f);
                float py = btnY + dpi(app, 2.0f);
                app.brush->SetColor(accentColor);
                app.renderTarget->DrawLine(
                    D2D1::Point2F(px, py + dpi(app, 3.0f)), D2D1::Point2F(px + dpi(app, 6.0f), py + dpi(app, 3.0f)),
                    app.brush, 2.0f);
                app.renderTarget->DrawLine(
                    D2D1::Point2F(px + dpi(app, 3.0f), py), D2D1::Point2F(px + dpi(app, 3.0f), py + dpi(app, 6.0f)),
                    app.brush, 2.0f);
            };
            drawAddButton(folderBtnX, true);
            drawAddButton(fileBtnX, false);
        }

        // Divider line
        float dividerY = headerY + headerHeight;
        app.brush->SetColor(borderColor);
        app.renderTarget->DrawLine(
            D2D1::Point2F(panelX + padding, dividerY),
            D2D1::Point2F(panelX + panelWidth - padding, dividerY),
            app.brush, 1.0f);

        // Items list (with scrolling); an active naming row occupies the
        // first slot and the real items shift down beneath it
        float listStartY = dividerY + dpi(app, 8.0f);
        float listHeight = panelHeight - listStartY - padding;
        float namingOffset = app.folderBrowserNaming != 0 ? itemHeight : 0.0f;
        float totalItemsHeight = app.folderItems.size() * itemHeight + namingOffset;

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

        for (size_t i = 0; i < app.folderItems.size(); i++) {
            float itemY = listStartY + namingOffset + i * itemHeight - app.folderBrowserScroll;

            // Skip items outside visible area
            if (itemY + itemHeight < listStartY || itemY > panelHeight - padding) continue;

            const auto& item = app.folderItems[i];
            float itemX = panelX + padding;
            float itemW = panelWidth - padding * 2;

            // Check hover
            bool isHovered = (app.mouseX >= itemX && app.mouseX <= itemX + itemW &&
                              app.mouseY >= itemY && app.mouseY <= itemY + itemHeight &&
                              app.mouseY >= listStartY && app.mouseY <= panelHeight - padding);

            bool isCurrent = !item.isDirectory && !currentName.empty() &&
                             _wcsicmp(item.name.c_str(), currentName.c_str()) == 0;
            if (isCurrent) {
                D2D1_COLOR_F curColor = app.theme.accent;
                curColor.a = 0.22f * anim;
                app.brush->SetColor(curColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(itemX - dpi(app, 4.0f), itemY, itemX + itemW + dpi(app, 4.0f), itemY + itemHeight), 4, 4),
                    app.brush);
            }

            if (isHovered) {
                app.hoveredFolderIndex = (int)i;

                // Hover highlight
                D2D1_COLOR_F hoverColor = app.theme.accent;
                hoverColor.a = 0.15f * anim;
                app.brush->SetColor(hoverColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(itemX - dpi(app, 4.0f), itemY, itemX + itemW + dpi(app, 4.0f), itemY + itemHeight), 4, 4),
                    app.brush);
            }

            // Icon and text
            float iconX = itemX + dpi(app, 4.0f);
            float textX = itemX + dpi(app, 26.0f);

            // Simple folder/file indicator
            if (item.isDirectory) {
                // Folder icon (simple filled rectangle with tab)
                D2D1_COLOR_F folderColor = app.theme.isDark ? hexColor(0xE8A848) : hexColor(0xD4941A);
                folderColor.a = anim;
                app.brush->SetColor(folderColor);
                // Main body
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(iconX, itemY + dpi(app, 10.0f), iconX + dpi(app, 16.0f), itemY + dpi(app, 22.0f)), 2, 2),
                    app.brush);
                // Tab
                app.renderTarget->FillRectangle(
                    D2D1::RectF(iconX, itemY + dpi(app, 8.0f), iconX + dpi(app, 8.0f), itemY + dpi(app, 11.0f)),
                    app.brush);
            } else {
                // File icon (simple document shape)
                D2D1_COLOR_F fileColor = app.theme.text;
                fileColor.a = 0.6f * anim;
                app.brush->SetColor(fileColor);
                app.renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(iconX + dpi(app, 2.0f), itemY + dpi(app, 6.0f), iconX + dpi(app, 14.0f), itemY + dpi(app, 22.0f)), 1, 1),
                    app.brush, 1.0f);
            }

            // Item name
            D2D1_COLOR_F textColor = item.isDirectory ? app.theme.heading : app.theme.text;
            textColor.a = anim;
            app.brush->SetColor(textColor);

            app.renderTarget->DrawText(item.name.c_str(), (UINT32)item.name.length(), browserFormat,
                D2D1::RectF(textX, itemY + dpi(app, 4.0f), panelX + panelWidth - padding, itemY + itemHeight),
                app.brush);
        }

        // Naming row for a new file/folder: pinned to the top of the list,
        // drawn after the items so they scroll underneath it
        if (app.folderBrowserNaming != 0) {
            bool isFolder = app.folderBrowserNaming == 2;
            float rowY = listStartY;
            float iconX = panelX + padding + dpi(app, 4.0f);
            float textX = panelX + padding + dpi(app, 26.0f);

            app.brush->SetColor(panelBg);
            app.renderTarget->FillRectangle(
                D2D1::RectF(panelX + padding - dpi(app, 4.0f), rowY,
                            panelX + panelWidth - padding + dpi(app, 4.0f), rowY + itemHeight),
                app.brush);

            if (isFolder) {
                D2D1_COLOR_F folderColor = app.theme.isDark ? hexColor(0xE8A848) : hexColor(0xD4941A);
                folderColor.a = anim;
                app.brush->SetColor(folderColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(iconX, rowY + dpi(app, 10.0f), iconX + dpi(app, 16.0f), rowY + dpi(app, 22.0f)), 2, 2),
                    app.brush);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(iconX, rowY + dpi(app, 8.0f), iconX + dpi(app, 8.0f), rowY + dpi(app, 11.0f)),
                    app.brush);
            } else {
                D2D1_COLOR_F fileColor = app.theme.text;
                fileColor.a = 0.6f * anim;
                app.brush->SetColor(fileColor);
                app.renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(iconX + dpi(app, 2.0f), rowY + dpi(app, 6.0f), iconX + dpi(app, 14.0f), rowY + dpi(app, 22.0f)), 1, 1),
                    app.brush, 1.0f);
            }

            drawInputBox(textX, panelX + panelWidth - padding, rowY + dpi(app, 1.0f));
        }

        // Scrollbar if needed
        if (totalItemsHeight > listHeight) {
            float sbHeight = listHeight / totalItemsHeight * listHeight;
            sbHeight = std::max(sbHeight, dpi(app, 20.0f));
            float sbY = listStartY + (maxScroll > 0 ? (app.folderBrowserScroll / maxScroll * (listHeight - sbHeight)) : 0);

            D2D1_COLOR_F sbColor = app.theme.text;
            sbColor.a = 0.3f * anim;
            app.brush->SetColor(sbColor);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(panelX + panelWidth - dpi(app, 8.0f), sbY,
                                              panelX + panelWidth - dpi(app, 4.0f), sbY + sbHeight), 2, 2),
                app.brush);
        }
    }
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

    // Panel dimensions (below the title-bar tab strip)
    float panelWidth = tocPanelWidth(app);
    float panelX = tocPanelX(app, panelWidth);  // slides from the chosen side
    float panelY = chromeTopHeight(app);
    // Doubles as the absolute bottom edge in the list math below (same
    // convention as the folder browser)
    float panelHeight = (float)app.height;

    // Background
    D2D1_COLOR_F panelBg = app.theme.isDark ? hexColor(0x1E1E1E, 0.95f) : hexColor(0xF5F5F5, 0.95f);
    app.brush->SetColor(panelBg);
    app.renderTarget->FillRectangle(
        D2D1::RectF(panelX, panelY, panelX + panelWidth, panelY + panelHeight), app.brush);

    // Left border
    D2D1_COLOR_F borderColor = app.theme.isDark ? hexColor(0x3A3A40, 0.8f) : hexColor(0xD0D0D0, 0.8f);
    app.brush->SetColor(borderColor);
    app.renderTarget->DrawLine(
        D2D1::Point2F(panelX, panelY),
        D2D1::Point2F(panelX, panelY + panelHeight),
        app.brush, 1.0f);

    IDWriteTextFormat* tocBold = app.tocFormatBold;
    IDWriteTextFormat* tocNormal = app.tocFormat;
    if (tocBold && tocNormal) {
        float padding = dpi(app, 12.0f);
        float itemHeight = dpi(app, 28.0f);
        float headerHeight = dpi(app, 40.0f);

        // Header: "Contents"
        float headerY = panelY + padding;
        D2D1_COLOR_F headerColor = app.theme.heading;
        headerColor.a = anim;
        app.brush->SetColor(headerColor);
        const wchar_t* tocTitle = tr(app, "toc.title");
        app.renderTarget->DrawText(tocTitle, (UINT32)wcslen(tocTitle), tocBold,
            D2D1::RectF(panelX + padding, headerY, panelX + panelWidth - padding, headerY + headerHeight),
            app.brush);

        // Divider
        float dividerY = headerY + headerHeight;
        app.brush->SetColor(borderColor);
        app.renderTarget->DrawLine(
            D2D1::Point2F(panelX + padding, dividerY),
            D2D1::Point2F(panelX + panelWidth - padding, dividerY),
            app.brush, 1.0f);

        // Items list
        float listStartY = dividerY + dpi(app, 8.0f);
        float listHeight = panelHeight - listStartY - padding;

        if (app.headings.empty()) {
            // "No headings" message
            D2D1_COLOR_F dimColor = app.theme.text;
            dimColor.a = 0.5f * anim;
            app.brush->SetColor(dimColor);
            const wchar_t* tocEmpty = tr(app, "toc.empty");
            app.renderTarget->DrawText(tocEmpty, (UINT32)wcslen(tocEmpty), tocNormal,
                D2D1::RectF(panelX + padding, listStartY + dpi(app, 8.0f), panelX + panelWidth - padding, listStartY + dpi(app, 40.0f)),
                app.brush);
        } else {
            float totalItemsHeight = app.headings.size() * itemHeight;

            // Clamp scroll
            float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);
            app.tocScroll = std::max(0.0f, std::min(app.tocScroll, maxScroll));

            app.hoveredTocIndex = -1;

            for (size_t i = 0; i < app.headings.size(); i++) {
                float itemY = listStartY + i * itemHeight - app.tocScroll;

                // Skip items outside visible area
                if (itemY + itemHeight < listStartY || itemY > panelHeight - padding) continue;

                const auto& heading = app.headings[i];
                float indent = (heading.level - 1) * dpi(app, 16.0f);
                float itemX = panelX + padding + indent;

                // Check hover (use full item width for hit area)
                float hitX = panelX + padding;
                float hitW = panelWidth - padding * 2;
                bool isHovered = (app.mouseX >= hitX && app.mouseX <= hitX + hitW &&
                                  app.mouseY >= itemY && app.mouseY <= itemY + itemHeight &&
                                  app.mouseY >= listStartY && app.mouseY <= panelHeight - padding);

                if (isHovered) {
                    app.hoveredTocIndex = (int)i;

                    // Hover highlight
                    D2D1_COLOR_F hoverColor = app.theme.accent;
                    hoverColor.a = 0.15f * anim;
                    app.brush->SetColor(hoverColor);
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(panelX + padding - dpi(app, 4.0f), itemY,
                            panelX + panelWidth - padding + dpi(app, 4.0f), itemY + itemHeight), 4, 4),
                        app.brush);
                }

                // Text color and format based on heading level
                IDWriteTextFormat* fmt = (heading.level == 1) ? tocBold : tocNormal;
                D2D1_COLOR_F textColor;
                if (heading.level == 1) {
                    textColor = app.theme.heading;
                } else if (heading.level == 3) {
                    textColor = app.theme.text;
                    textColor.a = 0.7f * anim;
                } else {
                    textColor = app.theme.text;
                    textColor.a = anim;
                }
                app.brush->SetColor(textColor);

                app.renderTarget->DrawText(heading.text.c_str(), (UINT32)heading.text.length(), fmt,
                    D2D1::RectF(itemX, itemY + dpi(app, 4.0f), panelX + panelWidth - padding, itemY + itemHeight),
                    app.brush);
            }

            // Scrollbar if needed
            if (totalItemsHeight > listHeight) {
                float sbHeight = listHeight / totalItemsHeight * listHeight;
                sbHeight = std::max(sbHeight, dpi(app, 20.0f));
                float sbY = listStartY + (maxScroll > 0 ? (app.tocScroll / maxScroll * (listHeight - sbHeight)) : 0);

                D2D1_COLOR_F sbColor = app.theme.text;
                sbColor.a = 0.3f * anim;
                app.brush->SetColor(sbColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(panelX + dpi(app, 4.0f), sbY,
                                                  panelX + dpi(app, 8.0f), sbY + sbHeight), 2, 2),
                    app.brush);
            }
        }
    }
}

float tocPanelX(const App& app, float panelWidth) {
    return app.tocOnLeft ? -panelWidth * (1.0f - app.tocAnimation)
                         : app.width - panelWidth * app.tocAnimation;
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

    // Animate in - only invalidate while progressing
    if (app.themeChooserAnimation < 1.0f) {
        float prev = app.themeChooserAnimation;
        app.themeChooserAnimation = std::min(1.0f, app.themeChooserAnimation + 0.15f);
        if (app.themeChooserAnimation != prev)
            InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.themeChooserAnimation;

    // Keep the underlying document visible while the chooser is open.
    float backdropAlpha = (app.theme.isDark ? 0.34f : 0.24f) * anim;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    // Panel dimensions - 2 columns (Light | Dark), 5 rows
    float panelWidth = std::min(dpi(app, 900.0f), app.width - dpi(app, 80.0f));
    float panelHeight = std::min(dpi(app, 620.0f), app.height - dpi(app, 80.0f));
    float panelX = (app.width - panelWidth) / 2;
    float panelY = (app.height - panelHeight) / 2 + (1 - anim) * dpi(app, 50.0f);

    // The chooser is chrome, so it follows the active theme while each card
    // still previews its own document palette.
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(
        D2D1::RectF(panelX, panelY, panelX + panelWidth, panelY + panelHeight),
        dpi(app, 16.0f), dpi(app, 16.0f));
    D2D1_COLOR_F panelColor = app.theme.background;
    panelColor.a = 0.98f * anim;
    app.brush->SetColor(panelColor);
    app.renderTarget->FillRoundedRectangle(panelRect, app.brush);

    // Subtle border
    D2D1_COLOR_F panelBorder = app.theme.text;
    panelBorder.a = 0.35f * anim;
    app.brush->SetColor(panelBorder);
    app.renderTarget->DrawRoundedRectangle(panelRect, app.brush, 1.0f);

    // Title
    IDWriteTextFormat* titleFormat = app.themeTitleFormat;
    if (titleFormat) {
        titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_COLOR_F titleColor = app.theme.heading;
        titleColor.a = anim;
        app.brush->SetColor(titleColor);
        const wchar_t* chooseTheme = tr(app, "theme.chooser.title");
        app.renderTarget->DrawText(chooseTheme, (UINT32)wcslen(chooseTheme), titleFormat,
            D2D1::RectF(panelX, panelY + dpi(app, 15.0f), panelX + panelWidth, panelY + dpi(app, 55.0f)), app.brush);
    }

    // "Follow Windows" toggle, top-right of the panel. While it's on, picking
    // a light card sets the light-mode preference and a dark card the dark
    // one — the OS switch then flips between the two automatically.
    if (app.folderBrowserFormat) {
        float togW = dpi(app, 34.0f);
        float togH = dpi(app, 18.0f);
        float togX = panelX + panelWidth - dpi(app, 30.0f) - togW;
        float togY = panelY + dpi(app, 24.0f);
        bool on = app.followSystemTheme;

        D2D1_COLOR_F trackColor = on ? hexColor(0x3FB950, 0.9f * anim)
                                     : hexColor(0x4A4A52, 0.9f * anim);
        app.brush->SetColor(trackColor);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(togX, togY, togX + togW, togY + togH),
                              togH / 2, togH / 2), app.brush);
        float knobR = togH / 2 - dpi(app, 2.0f);
        float knobX = on ? togX + togW - togH / 2 : togX + togH / 2;
        app.brush->SetColor(D2D1::ColorF(1, 1, 1, anim));
        app.renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(knobX, togY + togH / 2), knobR, knobR),
            app.brush);

        D2D1_COLOR_F toggleText = app.theme.text;
        toggleText.a = 0.75f * anim;
        app.brush->SetColor(toggleText);
        const wchar_t* followWindows = tr(app, "settings.follow_windows");
        float followWidth = measureText(app, followWindows, app.folderBrowserFormat);
        app.renderTarget->DrawText(followWindows, (UINT32)wcslen(followWindows), app.folderBrowserFormat,
            D2D1::RectF(togX - followWidth - dpi(app, 10.0f), togY + dpi(app, 1.0f),
                        togX - dpi(app, 8.0f), togY + togH), app.brush);
    }

    // Theme grid - 2 columns (Light | Dark); rows grow with custom themes
    float gridStartY = panelY + dpi(app, 75.0f);
    float cardWidth = (panelWidth - dpi(app, 60.0f)) / 2;  // 2 columns with padding
    float cardHeight = (panelHeight - dpi(app, 130.0f)) / themeChooserRows();
    float cardPadding = dpi(app, 8.0f);

    app.hoveredThemeIndex = -1;

    for (int i = 0; i < themeCount(); i++) {
        const D2DTheme& t = themeAt(i);
        int col, row;
        themeChooserCell(i, col, row);

        float cardX = panelX + dpi(app, 20.0f) + col * (cardWidth + dpi(app, 20.0f));
        float cardY = gridStartY + row * cardHeight;
        float innerX = cardX + cardPadding;
        float innerY = cardY + cardPadding;
        float innerW = cardWidth - cardPadding * 2;
        float innerH = cardHeight - cardPadding * 2;

        // Check hover
        bool isHovered = (app.mouseX >= innerX && app.mouseX <= innerX + innerW &&
                          app.mouseY >= innerY && app.mouseY <= innerY + innerH);
        bool isSelected = (i == app.currentThemeIndex);

        if (isHovered) {
            app.hoveredThemeIndex = i;
        }

        // Card background (theme preview)
        D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(
            D2D1::RectF(innerX, innerY, innerX + innerW, innerY + innerH),
            dpi(app, 10.0f), dpi(app, 10.0f));

        // Selection/hover glow
        if (isSelected || isHovered) {
            float glowSize = isSelected ? 3.0f : 2.0f;
            D2D1_ROUNDED_RECT glowRect = D2D1::RoundedRect(
                D2D1::RectF(innerX - glowSize, innerY - glowSize,
                            innerX + innerW + glowSize, innerY + innerH + glowSize),
                12, 12);
            D2D1_COLOR_F glowColor = t.accent;
            glowColor.a = (isSelected ? 0.8f : 0.5f) * anim;
            app.brush->SetColor(glowColor);
            app.renderTarget->DrawRoundedRectangle(glowRect, app.brush, 2.0f);
        }

        // Theme background preview
        D2D1_COLOR_F bgColor = t.background;
        bgColor.a = anim;
        app.brush->SetColor(bgColor);
        app.renderTarget->FillRoundedRectangle(cardRect, app.brush);

        // Theme name
        IDWriteTextFormat* nameFormat = (i < (int)app.themePreviewFormats.size()) ?
            app.themePreviewFormats[i].name : nullptr;
        if (nameFormat) {
            D2D1_COLOR_F nameColor = t.heading;
            nameColor.a = anim;
            app.brush->SetColor(nameColor);
            app.renderTarget->DrawText(t.name, (UINT32)wcslen(t.name), nameFormat,
                D2D1::RectF(innerX + dpi(app, 12.0f), innerY + dpi(app, 8.0f), innerX + innerW - dpi(app, 10.0f), innerY + dpi(app, 28.0f)), app.brush);
        }

        // Preview text samples
        IDWriteTextFormat* previewFormat = (i < (int)app.themePreviewFormats.size()) ?
            app.themePreviewFormats[i].preview : nullptr;
        if (previewFormat) {
            // Sample text
            D2D1_COLOR_F textColor = t.text;
            textColor.a = anim;
            app.brush->SetColor(textColor);
            app.renderTarget->DrawText(L"The quick brown fox", 19, previewFormat,
                D2D1::RectF(innerX + dpi(app, 12.0f), innerY + dpi(app, 30.0f), innerX + innerW - dpi(app, 10.0f), innerY + dpi(app, 45.0f)), app.brush);

            // Link sample
            D2D1_COLOR_F linkColor = t.link;
            linkColor.a = anim;
            app.brush->SetColor(linkColor);
            app.renderTarget->DrawText(L"hyperlink", 9, previewFormat,
                D2D1::RectF(innerX + dpi(app, 12.0f), innerY + dpi(app, 44.0f), innerX + dpi(app, 80.0f), innerY + dpi(app, 58.0f)), app.brush);

            // Code sample background
            D2D1_COLOR_F codeBgColor = t.codeBackground;
            codeBgColor.a = anim;
            app.brush->SetColor(codeBgColor);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(innerX + dpi(app, 75.0f), innerY + dpi(app, 44.0f), innerX + dpi(app, 140.0f), innerY + dpi(app, 58.0f)), 3, 3),
                app.brush);

            // Code text
            IDWriteTextFormat* codePreviewFormat = (i < (int)app.themePreviewFormats.size()) ?
                app.themePreviewFormats[i].code : nullptr;
            if (codePreviewFormat) {
                D2D1_COLOR_F codeColor = t.code;
                codeColor.a = anim;
                app.brush->SetColor(codeColor);
                app.renderTarget->DrawText(L"code()", 6, codePreviewFormat,
                    D2D1::RectF(innerX + dpi(app, 78.0f), innerY + dpi(app, 45.0f), innerX + dpi(app, 138.0f), innerY + dpi(app, 58.0f)), app.brush);
            }
        }

        // Checkmark for selected theme
        if (isSelected) {
            D2D1_COLOR_F checkColor = t.accent;
            checkColor.a = anim;
            app.brush->SetColor(checkColor);
            app.renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(innerX + innerW - dpi(app, 18.0f), innerY + dpi(app, 15.0f)), dpi(app, 8.0f), dpi(app, 8.0f)),
                app.brush);
            app.brush->SetColor(t.isDark ? hexColor(0x000000, anim) : hexColor(0xFFFFFF, anim));
            // Draw checkmark using lines
            app.renderTarget->DrawLine(
                D2D1::Point2F(innerX + innerW - dpi(app, 22.0f), innerY + dpi(app, 15.0f)),
                D2D1::Point2F(innerX + innerW - dpi(app, 18.0f), innerY + dpi(app, 19.0f)),
                app.brush, dpi(app, 2.0f));
            app.renderTarget->DrawLine(
                D2D1::Point2F(innerX + innerW - dpi(app, 18.0f), innerY + dpi(app, 19.0f)),
                D2D1::Point2F(innerX + innerW - dpi(app, 13.0f), innerY + dpi(app, 11.0f)),
                app.brush, dpi(app, 2.0f));
        }

        // Border
        D2D1_COLOR_F borderColor = t.isDark ? hexColor(0x404040) : hexColor(0xD0D0D0);
        borderColor.a = 0.5f * anim;
        app.brush->SetColor(borderColor);
        app.renderTarget->DrawRoundedRectangle(cardRect, app.brush, 1.0f);
    }

    // Column headers
    IDWriteTextFormat* headerFormat = app.themeHeaderFormat;
    if (headerFormat) {
        headerFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_COLOR_F headerColor = app.theme.text;
        headerColor.a = 0.6f * anim;
        app.brush->SetColor(headerColor);

        // Light themes header
        const wchar_t* lightHdr = tr(app, "theme.chooser.light");
        app.renderTarget->DrawText(lightHdr, (UINT32)wcslen(lightHdr), headerFormat,
            D2D1::RectF(panelX + dpi(app, 20.0f), gridStartY - dpi(app, 20.0f), panelX + dpi(app, 20.0f) + cardWidth, gridStartY - dpi(app, 5.0f)), app.brush);

        // Dark themes header
        const wchar_t* darkHdr = tr(app, "theme.chooser.dark");
        app.renderTarget->DrawText(darkHdr, (UINT32)wcslen(darkHdr), headerFormat,
            D2D1::RectF(panelX + dpi(app, 40.0f) + cardWidth, gridStartY - dpi(app, 20.0f), panelX + dpi(app, 40.0f) + cardWidth * 2, gridStartY - dpi(app, 5.0f)), app.brush);
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

    // Panel dimensions — fit to window
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
    { L"Select All",         L"Ctrl+A", true  },
    { L"New File",           L"N",      false },
    { L"Print / PDF",        L"Ctrl+P", false },
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
        "ctx.copy", "ctx.select_all", "ctx.new", "ctx.print", "ctx.edit",
        "ctx.search", "ctx.toc", "ctx.browse", "ctx.reveal", "ctx.theme",
        "ctx.settings", "ctx.help",
    };

    // Shortcut hints reflect the user keymap ([Keys] in settings.ini)
    auto shortcutLabel = [&](int item) -> std::wstring {
        switch (item) {
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
// scale — app.theme and app.contentScale hold the print palette and 1.0
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

void settingsToggle(App& app, float x, float y, bool on, int action, float anim) {
    float w = dpi(app, 34.0f), h = dpi(app, 18.0f);
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

    float panelW = std::min(dpi(app, 660.0f), app.width - dpi(app, 80.0f));
    float panelH = std::min(dpi(app, 420.0f), app.height - dpi(app, 80.0f));
    float px = (app.width - panelW) / 2;
    float py = (app.height - panelH) / 2 + (1 - anim) * dpi(app, 30.0f);

    D2D1_ROUNDED_RECT panel = D2D1::RoundedRect(
        D2D1::RectF(px, py, px + panelW, py + panelH), dpi(app, 12.0f), dpi(app, 12.0f));
    D2D1_COLOR_F bg = app.theme.background; bg.a = 0.99f * anim;
    app.brush->SetColor(bg);
    app.renderTarget->FillRoundedRectangle(panel, app.brush);
    D2D1_COLOR_F border = app.theme.text; border.a = 0.25f * anim;
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
    float railW = dpi(app, 130.0f);
    for (int i = 0; i < 3; i++) {
        float rowY = railY + i * dpi(app, 34.0f);
        D2D1_RECT_F r = D2D1::RectF(railX, rowY, railX + railW, rowY + dpi(app, 28.0f));
        if (i == app.settingsSection) {
            D2D1_COLOR_F hl = app.theme.accent; hl.a = 0.16f * anim;
            app.brush->SetColor(hl);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(r, dpi(app, 5.0f), dpi(app, 5.0f)), app.brush);
        }
        D2D1_COLOR_F tc = app.theme.text;
        tc.a = (i == app.settingsSection ? 1.0f : 0.6f) * anim;
        app.brush->SetColor(tc);
        app.renderTarget->DrawText(sections[i], (UINT32)wcslen(sections[i]), fmt,
            D2D1::RectF(r.left + dpi(app, 10.0f), r.top + dpi(app, 5.0f), r.right, r.bottom),
            app.brush);
        app.settingsHits.push_back({r, sectionActions[i]});
    }

    // Content column
    float cx = railX + railW + dpi(app, 28.0f);
    float cw = px + panelW - dpi(app, 28.0f) - cx;
    float cy = railY;
    float rowH = dpi(app, 44.0f);

    // hintRight reserves space for the row's right-side control so a long
    // hint (or a translation) never runs beneath it
    auto rowLabel = [&](const wchar_t* label, const wchar_t* hint,
                        float hintRight = 0.0f) {
        D2D1_COLOR_F tc = app.theme.text; tc.a = 0.95f * anim;
        app.brush->SetColor(tc);
        app.renderTarget->DrawText(label, (UINT32)wcslen(label), fmt,
            D2D1::RectF(cx, cy + dpi(app, 3.0f), cx + cw - hintRight,
                        cy + dpi(app, 21.0f)), app.brush);
        if (hint) {
            tc.a = 0.45f * anim;
            app.brush->SetColor(tc);
            app.renderTarget->DrawText(hint, (UINT32)wcslen(hint), fmt,
                D2D1::RectF(cx, cy + dpi(app, 21.0f), cx + cw - hintRight,
                            cy + dpi(app, 39.0f)), app.brush);
        }
    };
    auto hairline = [&]() {
        D2D1_COLOR_F hc = app.theme.text; hc.a = 0.08f * anim;
        app.brush->SetColor(hc);
        app.renderTarget->DrawLine(D2D1::Point2F(cx, cy + rowH - dpi(app, 4.0f)),
                                   D2D1::Point2F(cx + cw, cy + rowH - dpi(app, 4.0f)),
                                   app.brush, 1.0f);
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
        rowLabel(tr(app, "settings.language"), tr(app, "settings.language.hint"), dpi(app, 200.0f));
        {
            // Dropdown value box with a pencil beside it (opens languages.ini)
            float boxH = dpi(app, 26.0f);
            float penW = dpi(app, 26.0f);
            float boxW = dpi(app, 150.0f);
            float boxX = cx + cw - penW - dpi(app, 8.0f) - boxW;
            float boxY = cy + dpi(app, 2.0f);
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
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.shortcuts"), tr(app, "settings.shortcuts.hint"), dpi(app, 200.0f));
        {
            // Profile dropdown with a pencil beside it (opens the shortcut
            // editor) — geometry mirrors the language row above exactly
            float boxH = dpi(app, 26.0f);
            float penW = dpi(app, 26.0f);
            float boxW = dpi(app, 150.0f);
            float boxX = cx + cw - penW - dpi(app, 8.0f) - boxW;
            float boxY = cy + dpi(app, 2.0f);
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
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.folder_search"), tr(app, "settings.folder_search.hint"), dpi(app, 50.0f));
        settingsToggle(app, cx + cw - dpi(app, 40.0f), cy + dpi(app, 6.0f),
                       app.folderSearchEnabled, SET_TOGGLE_FOLDERSEARCH, anim);
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.browse_focus_path"), tr(app, "settings.browse_focus_path.hint"), dpi(app, 50.0f));
        settingsToggle(app, cx + cw - dpi(app, 40.0f), cy + dpi(app, 6.0f),
                       app.browserFocusPath, SET_TOGGLE_BROWSEFOCUS, anim);
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.open_in_tabs"), tr(app, "settings.open_in_tabs.hint"), dpi(app, 50.0f));
        settingsToggle(app, cx + cw - dpi(app, 40.0f), cy + dpi(app, 6.0f),
                       app.openInTabs, SET_TOGGLE_OPENTABS, anim);
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.open_ini"), tr(app, "settings.open_ini.hint"), dpi(app, 70.0f));
        float bx = cx + cw - dpi(app, 60.0f);
        settingsChip(app, bx, cy + dpi(app, 2.0f), tr(app, "settings.open"), false, SET_OPEN_INI, anim, fmt);
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.open_themes_ini"), tr(app, "settings.open_themes_ini.hint"), dpi(app, 70.0f));
        bx = cx + cw - dpi(app, 60.0f);
        settingsChip(app, bx, cy + dpi(app, 2.0f), tr(app, "settings.open"), false, SET_OPEN_THEMES_INI, anim, fmt);
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.open_langs"), tr(app, "settings.open_langs.hint"), dpi(app, 70.0f));
        bx = cx + cw - dpi(app, 60.0f);
        settingsChip(app, bx, cy + dpi(app, 2.0f), tr(app, "settings.open"), false, SET_OPEN_LANGS_INI, anim, fmt);
    } else if (app.settingsSection == 1) {  // Appearance
        rowLabel(tr(app, "settings.reading_width_window"), tr(app, "settings.reading_width_window.hint"));
        cy += rowH + dpi(app, 6.0f);
        slider(cx, cy, cw, app.readingWidthPct, SET_SLIDER_READING, 0);
        cy += dpi(app, 30.0f);
        rowLabel(tr(app, "settings.reading_width_full"), tr(app, "settings.reading_width_full.hint"));
        cy += rowH + dpi(app, 6.0f);
        slider(cx, cy, cw, app.zenWidthPct, SET_SLIDER_ZEN, 1);
        cy += dpi(app, 30.0f);
        rowLabel(tr(app, "settings.follow_windows"), tr(app, "settings.follow_windows.hint"), dpi(app, 50.0f));
        settingsToggle(app, cx + cw - dpi(app, 40.0f), cy + dpi(app, 6.0f),
                       app.followSystemTheme, SET_TOGGLE_FOLLOW, anim);
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.toc_side"), tr(app, "settings.toc_side.hint"));
        {
            float tx = cx + cw - dpi(app, 118.0f);
            settingsChip(app, tx, cy + dpi(app, 2.0f), tr(app, "settings.toc.left"), app.tocOnLeft,
                         SET_TOC_LEFT, anim, fmt);
            settingsChip(app, tx, cy + dpi(app, 2.0f), tr(app, "settings.toc.right"), !app.tocOnLeft,
                         SET_TOC_RIGHT, anim, fmt);
        }
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.themes"), tr(app, "settings.themes.hint"));
        float bx2 = cx + cw - dpi(app, 208.0f);
        settingsChip(app, bx2, cy + dpi(app, 2.0f), tr(app, "settings.browse"), false, SET_OPEN_THEMES, anim, fmt);
        settingsChip(app, bx2, cy + dpi(app, 2.0f), tr(app, "settings.edit"), false, SET_EDIT_THEME, anim, fmt);
        settingsChip(app, bx2, cy + dpi(app, 2.0f), tr(app, "settings.new"), false, SET_NEW_THEME, anim, fmt);
    } else {  // Editor
        rowLabel(tr(app, "settings.word_wrap"), tr(app, "settings.word_wrap.hint"), dpi(app, 50.0f));
        settingsToggle(app, cx + cw - dpi(app, 40.0f), cy + dpi(app, 6.0f),
                       app.editorWordWrap, SET_TOGGLE_WRAP, anim);
        hairline(); cy += rowH;
        rowLabel(tr(app, "settings.preview_pane"), tr(app, "settings.preview_pane.hint"), dpi(app, 50.0f));
        settingsToggle(app, cx + cw - dpi(app, 40.0f), cy + dpi(app, 6.0f),
                       app.editorShowPreview, SET_TOGGLE_PREVIEW, anim);
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
void renderConfirmExitDialog(App& app) {
    IDWriteTextFormat* fmt = app.folderBrowserFormat;
    if (!fmt) return;
    app.confirmExitHits.clear();
    const D2DTheme& base = app.theme;

    float backdropAlpha = base.isDark ? 0.34f : 0.24f;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    struct Btn { std::wstring label; int action; };
    Btn btns[3] = {
        {std::wstring(tr(app, "confirm.save")) + L"   Enter", 1},
        {std::wstring(tr(app, "confirm.discard")) + L"   N", 2},
        {std::wstring(tr(app, "confirm.keep")) + L"   Esc", 3},
    };
    float padX = dpi(app, 14.0f);
    float gap = dpi(app, 10.0f);
    float btnH = dpi(app, 30.0f);
    float widths[3];
    float total = 0;
    for (int i = 0; i < 3; i++) {
        widths[i] = measureText(app, btns[i].label, fmt) + padX * 2;
        total += widths[i];
    }
    float panelW = std::max(dpi(app, 440.0f), total + gap * 2 + dpi(app, 48.0f));
    float panelH = dpi(app, 138.0f);
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
        const wchar_t* title = tr(app, "confirm.title");
        app.renderTarget->DrawText(title, (UINT32)wcslen(title), app.themeTitleFormat,
            D2D1::RectF(px + dpi(app, 24.0f), py + dpi(app, 16.0f),
                        px + panelW, py + dpi(app, 50.0f)), app.brush);
    }
    {
        D2D1_COLOR_F c = base.text; c.a = 0.7f;
        app.brush->SetColor(c);
        const wchar_t* body = tr(app, "confirm.body");
        app.renderTarget->DrawText(body, (UINT32)wcslen(body), fmt,
            D2D1::RectF(px + dpi(app, 24.0f), py + dpi(app, 52.0f),
                        px + panelW - dpi(app, 24.0f), py + dpi(app, 78.0f)), app.brush);
    }

    float bx = px + (panelW - (total + gap * 2)) / 2;
    float by = py + panelH - btnH - dpi(app, 18.0f);
    for (int i = 0; i < 3; i++) {
        D2D1_RECT_F r = D2D1::RectF(bx, by, bx + widths[i], by + btnH);
        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, dpi(app, 6.0f), dpi(app, 6.0f));
        if (btns[i].action == 1) {
            // Save: the primary, accent-filled action
            D2D1_COLOR_F c = base.accent; c.a = 0.92f;
            app.brush->SetColor(c);
            app.renderTarget->FillRoundedRectangle(rr, app.brush);
            app.brush->SetColor(D2D1::ColorF(1, 1, 1, 0.97f));
        } else if (btns[i].action == 2) {
            // Discard: danger-tinted outline
            app.brush->SetColor(D2D1::ColorF(0.82f, 0.28f, 0.22f, 0.85f));
            app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);
        } else {
            D2D1_COLOR_F c = base.text; c.a = 0.3f;
            app.brush->SetColor(c);
            app.renderTarget->DrawRoundedRectangle(rr, app.brush, 1.0f);
            c.a = 0.9f;
            app.brush->SetColor(c);
        }
        app.renderTarget->DrawText(btns[i].label.c_str(), (UINT32)btns[i].label.size(), fmt,
            D2D1::RectF(r.left + padX, r.top + dpi(app, 6.0f), r.right, r.bottom),
            app.brush);
        app.confirmExitHits.push_back({r, btns[i].action});
        bx += widths[i] + gap;
    }
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
