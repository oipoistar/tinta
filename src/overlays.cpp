#include "overlays.h"
#include "utils.h"
#include "d2d_init.h"

#include <chrono>
#include <algorithm>

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
    float barY = dpi(app, 20.0f) * anim - barHeight * (1.0f - anim);  // Slide down from top

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
            app.renderTarget->DrawText(L"Search...", 9, searchTextFormat,
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
                wcscpy_s(countText, L"No matches");
                // Red color for no matches
                app.brush->SetColor(D2D1::ColorF(0.9f, 0.3f, 0.3f, anim));
            } else {
                swprintf_s(countText, L"%d of %zu", currentIdx + 1, matchCount);
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

void renderFolderBrowser(App& app) {
    // Animate in (slide from left) - only invalidate while progressing
    if (app.folderBrowserAnimation < 1.0f) {
        float prev = app.folderBrowserAnimation;
        app.folderBrowserAnimation = std::min(1.0f, app.folderBrowserAnimation + 0.15f);
        if (app.folderBrowserAnimation != prev)
            InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.folderBrowserAnimation;

    // Panel dimensions
    float panelWidth = std::min(dpi(app, 300.0f), std::max(dpi(app, 250.0f), app.width * 0.2f));
    float panelX = -panelWidth * (1.0f - anim);  // Slide in from left
    float panelY = 0;
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

    // Panel dimensions
    float panelWidth = std::min(dpi(app, 280.0f), std::max(dpi(app, 220.0f), app.width * 0.2f));
    float panelX = app.width - panelWidth * anim;  // Slide in from right
    float panelY = 0;
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
        app.renderTarget->DrawText(L"Contents", 8, tocBold,
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
            app.renderTarget->DrawText(L"No headings", 11, tocNormal,
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

    // Semi-transparent backdrop with blur effect simulation
    float backdropAlpha = 0.85f * anim;
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    // Panel dimensions - 2 columns (Light | Dark), 5 rows
    float panelWidth = std::min(dpi(app, 900.0f), app.width - dpi(app, 80.0f));
    float panelHeight = std::min(dpi(app, 620.0f), app.height - dpi(app, 80.0f));
    float panelX = (app.width - panelWidth) / 2;
    float panelY = (app.height - panelHeight) / 2 + (1 - anim) * dpi(app, 50.0f);

    // Panel background with subtle gradient simulation
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(
        D2D1::RectF(panelX, panelY, panelX + panelWidth, panelY + panelHeight),
        dpi(app, 16.0f), dpi(app, 16.0f));
    app.brush->SetColor(hexColor(0x1A1A1E, 0.98f * anim));
    app.renderTarget->FillRoundedRectangle(panelRect, app.brush);

    // Subtle border
    app.brush->SetColor(hexColor(0x3A3A40, 0.6f * anim));
    app.renderTarget->DrawRoundedRectangle(panelRect, app.brush, 1.0f);

    // Title
    IDWriteTextFormat* titleFormat = app.themeTitleFormat;
    if (titleFormat) {
        titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        app.brush->SetColor(D2D1::ColorF(1, 1, 1, anim));
        app.renderTarget->DrawText(L"Choose Theme", 12, titleFormat,
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

        app.brush->SetColor(D2D1::ColorF(1, 1, 1, 0.75f * anim));
        app.renderTarget->DrawText(L"Follow Windows", 14, app.folderBrowserFormat,
            D2D1::RectF(togX - dpi(app, 130.0f), togY + dpi(app, 1.0f),
                        togX - dpi(app, 8.0f), togY + togH), app.brush);
    }

    // Theme grid - 2 columns, 5 rows
    float gridStartY = panelY + dpi(app, 75.0f);
    float cardWidth = (panelWidth - dpi(app, 60.0f)) / 2;  // 2 columns with padding
    float cardHeight = (panelHeight - dpi(app, 130.0f)) / 5;  // 5 rows
    float cardPadding = dpi(app, 8.0f);

    app.hoveredThemeIndex = -1;

    for (int i = 0; i < THEME_COUNT; i++) {
        const D2DTheme& t = THEMES[i];
        int col = t.isDark ? 1 : 0;  // Light themes left, dark themes right
        int row = t.isDark ? (i - 5) : i;

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
        app.brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, anim));

        // Light themes header
        app.renderTarget->DrawText(L"LIGHT THEMES", 12, headerFormat,
            D2D1::RectF(panelX + dpi(app, 20.0f), gridStartY - dpi(app, 20.0f), panelX + dpi(app, 20.0f) + cardWidth, gridStartY - dpi(app, 5.0f)), app.brush);

        // Dark themes header
        app.renderTarget->DrawText(L"DARK THEMES", 11, headerFormat,
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

    // Semi-transparent backdrop
    app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.85f * anim));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

    // Panel dimensions — fit to window
    float panelWidth = std::min(dpi(app, 520.0f), app.width - dpi(app, 40.0f));
    float panelHeight = std::min(dpi(app, 700.0f), app.height - dpi(app, 40.0f));
    float panelX = (app.width - panelWidth) / 2;
    float panelY = (app.height - panelHeight) / 2 + (1 - anim) * dpi(app, 50.0f);

    // Panel background
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(
        D2D1::RectF(panelX, panelY, panelX + panelWidth, panelY + panelHeight),
        dpi(app, 16.0f), dpi(app, 16.0f));
    app.brush->SetColor(hexColor(0x1A1A1E, 0.98f * anim));
    app.renderTarget->FillRoundedRectangle(panelRect, app.brush);

    // Border
    app.brush->SetColor(hexColor(0x3A3A40, 0.6f * anim));
    app.renderTarget->DrawRoundedRectangle(panelRect, app.brush, 1.0f);

    // Title (fixed, not scrolled)
    float titleBottomY = panelY + dpi(app, 55.0f);
    IDWriteTextFormat* titleFormat = app.themeTitleFormat;
    if (titleFormat) {
        titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        app.brush->SetColor(D2D1::ColorF(1, 1, 1, anim));
        app.renderTarget->DrawText(L"Keyboard Shortcuts", 18, titleFormat,
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

    const HelpEntry navEntries[] = {
        {L"J / \x2193",   L"Scroll down"},
        {L"K / \x2191",   L"Scroll up"},
        {L"Space / PgDn", L"Page down"},
        {L"PgUp",         L"Page up"},
        {L"Home / End",   L"Jump to start / end"},
        {L"Ctrl+Scroll",  L"Zoom in / out"},
    };

    const HelpEntry overlayEntries[] = {
        {L"F / Ctrl+F",   L"Search"},
        {L"Enter",        L"Next search match"},
        {L"B",            L"Toggle folder browser"},
        {L"Tab",          L"Toggle table of contents"},
        {L"T",            L"Theme chooser"},
        {L"S",            L"Toggle stats"},
        {L"?",            L"This help"},
    };

    const HelpEntry editEntries[] = {
        {L":",             L"Enter edit mode"},
        {L"Ctrl+S",       L"Save (in edit mode)"},
        {L"Ctrl+P",       L"Show / hide preview pane"},
        {L"Ctrl+W",       L"Toggle word wrap"},
        {L"ESC ESC",      L"Exit edit mode"},
    };

    const HelpEntry generalEntries[] = {
        {L"Ctrl+A",       L"Select all text"},
        {L"Ctrl+C",       L"Copy selection"},
        {L"ESC",          L"Close overlay / Quit"},
        {L"Q",            L"Quit"},
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
    float totalContentHeight = sectionHeight(6) + sectionHeight(7) + sectionHeight(3) + sectionHeight(4) + footerH;

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
            app.brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.9f * anim));
            app.renderTarget->DrawText(entries[i].key, (UINT32)wcslen(entries[i].key), boldFmt,
                D2D1::RectF(leftX + dpi(app, 8.0f), y, leftX + keyColWidth, y + lineH), app.brush);

            // Description
            normalFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            app.brush->SetColor(D2D1::ColorF(0.7f, 0.7f, 0.7f, 0.9f * anim));
            app.renderTarget->DrawText(entries[i].desc, (UINT32)wcslen(entries[i].desc), normalFmt,
                D2D1::RectF(descX, y, rightEdge, y + lineH), app.brush);

            y += lineH;
        }
        y += sectionGap;
    };

    drawSection(L"NAVIGATION", navEntries, 6);
    drawSection(L"VIEW", overlayEntries, 7);
    drawSection(L"EDITING", editEntries, 3);
    drawSection(L"GENERAL", generalEntries, 4);

    // Footer hint
    normalFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    app.brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, anim));
    app.renderTarget->DrawText(L"Press ESC or ? to close", 23, normalFmt,
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
    { L"Edit",               L":",      false },
    { L"Search",             L"F",      false },
    { L"Table of Contents",  L"Tab",    true  },
    { L"Browse Files",       L"B",      false },
    { L"Reveal in Explorer", L"",       true  },
    { L"Theme",              L"T",      false },
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
        case CTX_COPY:   return app.hasSelection && !app.selectedText.empty();
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
        app.renderTarget->DrawText(
            CTX_ENTRIES[i].label, (UINT32)wcslen(CTX_ENTRIES[i].label), format,
            D2D1::RectF(x + inset, textY, x + w - inset, top + itemH), app.brush);

        if (CTX_ENTRIES[i].shortcut[0]) {
            IDWriteTextLayout* hint = nullptr;
            app.dwriteFactory->CreateTextLayout(
                CTX_ENTRIES[i].shortcut, (UINT32)wcslen(CTX_ENTRIES[i].shortcut),
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

