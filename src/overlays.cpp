#include "overlays.h"
#include "utils.h"

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
    float barWidth = std::min(500.0f, app.width - 40.0f);
    float barHeight = 44.0f;
    float barCenterWidth = (float)app.width;
    if (app.editMode) {
        // Center over editor pane (left side)
        float paneWidth = app.width * app.editorSplitRatio - 3;
        barWidth = std::min(barWidth, paneWidth - 40.0f);
        barCenterWidth = paneWidth;
    }
    float barX = (barCenterWidth - barWidth) / 2;
    float barY = 20.0f * anim - barHeight * (1.0f - anim);  // Slide down from top

    // Background with rounded corners
    D2D1_ROUNDED_RECT barRect = D2D1::RoundedRect(
        D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight),
        8, 8);

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
        float iconX = barX + 22;
        float iconY = barY + 22;
        app.renderTarget->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(iconX, iconY - 2), 7, 7),
            app.brush, 2.0f);
        app.renderTarget->DrawLine(
            D2D1::Point2F(iconX + 5, iconY + 3),
            D2D1::Point2F(iconX + 9, iconY + 7),
            app.brush, 2.0f);
    }

    // Search text
    IDWriteTextFormat* searchTextFormat = app.searchTextFormat;
    if (searchTextFormat) {
        float textX = barX + 42;
        float textWidth = barWidth - 120;  // Leave room for count

        if (app.searchQuery.empty()) {
            // Placeholder text
            D2D1_COLOR_F placeholderColor = app.theme.text;
            placeholderColor.a = 0.4f * anim;
            app.brush->SetColor(placeholderColor);
            app.renderTarget->DrawText(L"Search...", 9, searchTextFormat,
                D2D1::RectF(textX, barY + 12, textX + textWidth, barY + barHeight), app.brush);
        } else {
            // Actual search query
            D2D1_COLOR_F textColor = app.theme.text;
            textColor.a = anim;
            app.brush->SetColor(textColor);
            app.renderTarget->DrawText(app.searchQuery.c_str(), (UINT32)app.searchQuery.length(),
                searchTextFormat,
                D2D1::RectF(textX, barY + 12, textX + textWidth, barY + barHeight), app.brush);

            // Blinking cursor
            auto now = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            bool cursorVisible = (ms % 1000) < 500;
            if (app.searchActive && cursorVisible) {
                float queryWidth = measureText(app, app.searchQuery, searchTextFormat);
                float cursorX = textX + queryWidth + 2;
                app.brush->SetColor(textColor);
                app.renderTarget->DrawLine(
                    D2D1::Point2F(cursorX, barY + 12),
                    D2D1::Point2F(cursorX, barY + 32),
                    app.brush, 1.5f);
                // Keep animating cursor
                InvalidateRect(app.hwnd, nullptr, FALSE);
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
            float countX = barX + barWidth - countTextWidth - 14;
            app.renderTarget->DrawText(countText, (UINT32)wcslen(countText), searchTextFormat,
                D2D1::RectF(countX, barY + 12, barX + barWidth - 10, barY + barHeight), app.brush);
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
    float panelWidth = std::min(300.0f, std::max(250.0f, app.width * 0.2f));
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
        float padding = 12.0f;
        float itemHeight = 28.0f;
        float headerHeight = 40.0f;

        // Current path header
        float headerY = panelY + padding;
        D2D1_COLOR_F headerColor = app.theme.heading;
        headerColor.a = anim;
        app.brush->SetColor(headerColor);

        // Truncate path if too long
        std::wstring displayPath = app.folderBrowserPath;
        float maxPathWidth = panelWidth - padding * 2;

        // Truncation: estimate max chars from average char width, then measure once
        if (!displayPath.empty()) {
            float avgCharWidth = browserFormat->GetFontSize() * 0.55f;
            size_t maxChars = (size_t)(maxPathWidth / avgCharWidth);
            if (displayPath.length() > maxChars && maxChars > 6) {
                // Find a separator near the truncation point for clean breaks
                size_t keepLen = maxChars - 3;  // room for "..."
                size_t sepPos = displayPath.rfind(L'\\', displayPath.length() - keepLen);
                if (sepPos != std::wstring::npos && sepPos > 3) {
                    displayPath = L"..." + displayPath.substr(sepPos);
                } else {
                    displayPath = L"..." + displayPath.substr(displayPath.length() - keepLen);
                }
            }
        }

        app.renderTarget->DrawText(displayPath.c_str(), (UINT32)displayPath.length(), browserFormat,
            D2D1::RectF(panelX + padding, headerY, panelX + panelWidth - padding, headerY + headerHeight),
            app.brush);

        // Divider line
        float dividerY = headerY + headerHeight;
        app.brush->SetColor(borderColor);
        app.renderTarget->DrawLine(
            D2D1::Point2F(panelX + padding, dividerY),
            D2D1::Point2F(panelX + panelWidth - padding, dividerY),
            app.brush, 1.0f);

        // Items list (with scrolling)
        float listStartY = dividerY + 8.0f;
        float listHeight = panelHeight - listStartY - padding;
        float totalItemsHeight = app.folderItems.size() * itemHeight;

        // Clamp scroll
        float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);
        app.folderBrowserScroll = std::max(0.0f, std::min(app.folderBrowserScroll, maxScroll));

        app.hoveredFolderIndex = -1;

        for (size_t i = 0; i < app.folderItems.size(); i++) {
            float itemY = listStartY + i * itemHeight - app.folderBrowserScroll;

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
                    D2D1::RoundedRect(D2D1::RectF(itemX - 4, itemY, itemX + itemW + 4, itemY + itemHeight), 4, 4),
                    app.brush);
            }

            // Icon and text
            float iconX = itemX + 4;
            float textX = itemX + 26;

            // Simple folder/file indicator
            if (item.isDirectory) {
                // Folder icon (simple filled rectangle with tab)
                D2D1_COLOR_F folderColor = app.theme.isDark ? hexColor(0xE8A848) : hexColor(0xD4941A);
                folderColor.a = anim;
                app.brush->SetColor(folderColor);
                // Main body
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(iconX, itemY + 10, iconX + 16, itemY + 22), 2, 2),
                    app.brush);
                // Tab
                app.renderTarget->FillRectangle(
                    D2D1::RectF(iconX, itemY + 8, iconX + 8, itemY + 11),
                    app.brush);
            } else {
                // File icon (simple document shape)
                D2D1_COLOR_F fileColor = app.theme.text;
                fileColor.a = 0.6f * anim;
                app.brush->SetColor(fileColor);
                app.renderTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(iconX + 2, itemY + 6, iconX + 14, itemY + 22), 1, 1),
                    app.brush, 1.0f);
            }

            // Item name
            D2D1_COLOR_F textColor = item.isDirectory ? app.theme.heading : app.theme.text;
            textColor.a = anim;
            app.brush->SetColor(textColor);

            app.renderTarget->DrawText(item.name.c_str(), (UINT32)item.name.length(), browserFormat,
                D2D1::RectF(textX, itemY + 4, panelX + panelWidth - padding, itemY + itemHeight),
                app.brush);
        }

        // Scrollbar if needed
        if (totalItemsHeight > listHeight) {
            float sbHeight = listHeight / totalItemsHeight * listHeight;
            sbHeight = std::max(sbHeight, 20.0f);
            float sbY = listStartY + (maxScroll > 0 ? (app.folderBrowserScroll / maxScroll * (listHeight - sbHeight)) : 0);

            D2D1_COLOR_F sbColor = app.theme.text;
            sbColor.a = 0.3f * anim;
            app.brush->SetColor(sbColor);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(panelX + panelWidth - 8, sbY,
                                              panelX + panelWidth - 4, sbY + sbHeight), 2, 2),
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
    float panelWidth = std::min(280.0f, std::max(220.0f, app.width * 0.2f));
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
        float padding = 12.0f;
        float itemHeight = 28.0f;
        float headerHeight = 40.0f;

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
        float listStartY = dividerY + 8.0f;
        float listHeight = panelHeight - listStartY - padding;

        if (app.headings.empty()) {
            // "No headings" message
            D2D1_COLOR_F dimColor = app.theme.text;
            dimColor.a = 0.5f * anim;
            app.brush->SetColor(dimColor);
            app.renderTarget->DrawText(L"No headings", 11, tocNormal,
                D2D1::RectF(panelX + padding, listStartY + 8, panelX + panelWidth - padding, listStartY + 40),
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
                float indent = (heading.level - 1) * 16.0f;
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
                        D2D1::RoundedRect(D2D1::RectF(panelX + padding - 4, itemY,
                            panelX + panelWidth - padding + 4, itemY + itemHeight), 4, 4),
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
                    D2D1::RectF(itemX, itemY + 4, panelX + panelWidth - padding, itemY + itemHeight),
                    app.brush);
            }

            // Scrollbar if needed
            if (totalItemsHeight > listHeight) {
                float sbHeight = listHeight / totalItemsHeight * listHeight;
                sbHeight = std::max(sbHeight, 20.0f);
                float sbY = listStartY + (maxScroll > 0 ? (app.tocScroll / maxScroll * (listHeight - sbHeight)) : 0);

                D2D1_COLOR_F sbColor = app.theme.text;
                sbColor.a = 0.3f * anim;
                app.brush->SetColor(sbColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(panelX + 4, sbY,
                                                  panelX + 8, sbY + sbHeight), 2, 2),
                    app.brush);
            }
        }
    }
}

void renderThemeChooser(App& app) {
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
    float panelWidth = std::min(900.0f, app.width - 80.0f);
    float panelHeight = std::min(620.0f, app.height - 80.0f);
    float panelX = (app.width - panelWidth) / 2;
    float panelY = (app.height - panelHeight) / 2 + (1 - anim) * 50;

    // Panel background with subtle gradient simulation
    D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(
        D2D1::RectF(panelX, panelY, panelX + panelWidth, panelY + panelHeight),
        16, 16);
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
            D2D1::RectF(panelX, panelY + 15, panelX + panelWidth, panelY + 55), app.brush);
    }

    // Theme grid - 2 columns, 5 rows
    float gridStartY = panelY + 75;
    float cardWidth = (panelWidth - 60) / 2;  // 2 columns with padding
    float cardHeight = (panelHeight - 130) / 5;  // 5 rows
    float cardPadding = 8;

    app.hoveredThemeIndex = -1;

    for (int i = 0; i < THEME_COUNT; i++) {
        const D2DTheme& t = THEMES[i];
        int col = t.isDark ? 1 : 0;  // Light themes left, dark themes right
        int row = t.isDark ? (i - 5) : i;

        float cardX = panelX + 20 + col * (cardWidth + 20);
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
            10, 10);

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
                D2D1::RectF(innerX + 12, innerY + 8, innerX + innerW - 10, innerY + 28), app.brush);
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
                D2D1::RectF(innerX + 12, innerY + 30, innerX + innerW - 10, innerY + 45), app.brush);

            // Link sample
            D2D1_COLOR_F linkColor = t.link;
            linkColor.a = anim;
            app.brush->SetColor(linkColor);
            app.renderTarget->DrawText(L"hyperlink", 9, previewFormat,
                D2D1::RectF(innerX + 12, innerY + 44, innerX + 80, innerY + 58), app.brush);

            // Code sample background
            D2D1_COLOR_F codeBgColor = t.codeBackground;
            codeBgColor.a = anim;
            app.brush->SetColor(codeBgColor);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(innerX + 75, innerY + 44, innerX + 140, innerY + 58), 3, 3),
                app.brush);

            // Code text
            IDWriteTextFormat* codePreviewFormat = (i < (int)app.themePreviewFormats.size()) ?
                app.themePreviewFormats[i].code : nullptr;
            if (codePreviewFormat) {
                D2D1_COLOR_F codeColor = t.code;
                codeColor.a = anim;
                app.brush->SetColor(codeColor);
                app.renderTarget->DrawText(L"code()", 6, codePreviewFormat,
                    D2D1::RectF(innerX + 78, innerY + 45, innerX + 138, innerY + 58), app.brush);
            }
        }

        // Checkmark for selected theme
        if (isSelected) {
            D2D1_COLOR_F checkColor = t.accent;
            checkColor.a = anim;
            app.brush->SetColor(checkColor);
            app.renderTarget->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(innerX + innerW - 18, innerY + 15), 8, 8),
                app.brush);
            app.brush->SetColor(t.isDark ? hexColor(0x000000, anim) : hexColor(0xFFFFFF, anim));
            // Draw checkmark using lines
            app.renderTarget->DrawLine(
                D2D1::Point2F(innerX + innerW - 22, innerY + 15),
                D2D1::Point2F(innerX + innerW - 18, innerY + 19),
                app.brush, 2.0f);
            app.renderTarget->DrawLine(
                D2D1::Point2F(innerX + innerW - 18, innerY + 19),
                D2D1::Point2F(innerX + innerW - 13, innerY + 11),
                app.brush, 2.0f);
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
            D2D1::RectF(panelX + 20, gridStartY - 20, panelX + 20 + cardWidth, gridStartY - 5), app.brush);

        // Dark themes header
        app.renderTarget->DrawText(L"DARK THEMES", 11, headerFormat,
            D2D1::RectF(panelX + 40 + cardWidth, gridStartY - 20, panelX + 40 + cardWidth * 2, gridStartY - 5), app.brush);
    }
}
