// Direct2D + DirectWrite renderer for Windows
// Much faster startup than OpenGL

#include "app.h"

#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <functional>
#include "settings.h"
#include "d2d_init.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"
#include "render.h"

static App* g_app = nullptr;

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void render(App& app);

void render(App& app) {
    if (!app.renderTarget) return;

    app.renderTarget->BeginDraw();
    app.drawCalls = 0;

    if (app.layoutDirty) {
        layoutDocument(app);
    }

    // Clear background
    app.renderTarget->Clear(app.theme.background);
    app.drawCalls++;

    // Clamp scroll values
    float maxScrollX = std::max(0.0f, app.contentWidth - app.width);
    float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
    app.scrollX = std::max(0.0f, std::min(app.scrollX, maxScrollX));
    app.scrollY = std::max(0.0f, std::min(app.scrollY, maxScrollY));

    // Render cached layout (document coordinates -> screen)
    const float viewportTop = app.scrollY;
    const float viewportBottom = app.scrollY + app.height;
    const float viewportLeft = app.scrollX;
    const float viewportRight = app.scrollX + app.width;
    const float cullMargin = 100.0f;

    for (const auto& rect : app.layoutRects) {
        if (rect.rect.bottom < viewportTop - cullMargin ||
            rect.rect.top > viewportBottom + cullMargin) {
            continue;
        }
        if (rect.rect.right < viewportLeft - cullMargin ||
            rect.rect.left > viewportRight + cullMargin) {
            continue;
        }
        app.brush->SetColor(rect.color);
        app.renderTarget->FillRectangle(
            D2D1::RectF(rect.rect.left - app.scrollX, rect.rect.top - app.scrollY,
                       rect.rect.right - app.scrollX, rect.rect.bottom - app.scrollY),
            app.brush);
        app.drawCalls++;
    }

    for (const auto& run : app.layoutTextRuns) {
        if (run.bounds.bottom < viewportTop - cullMargin ||
            run.bounds.top > viewportBottom + cullMargin) {
            continue;
        }
        if (run.bounds.right < viewportLeft - cullMargin ||
            run.bounds.left > viewportRight + cullMargin) {
            continue;
        }
        app.brush->SetColor(run.color);
        D2D1_POINT_2F drawPos = D2D1::Point2F(run.pos.x - app.scrollX, run.pos.y - app.scrollY);
        if (app.deviceContext) {
            app.deviceContext->DrawTextLayout(drawPos, run.layout, app.brush,
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        } else {
            app.renderTarget->DrawTextLayout(drawPos, run.layout, app.brush);
        }
        app.drawCalls++;
    }

    for (const auto& line : app.layoutLines) {
        float minY = std::min(line.p1.y, line.p2.y);
        float maxY = std::max(line.p1.y, line.p2.y);
        if (maxY < viewportTop - cullMargin || minY > viewportBottom + cullMargin) {
            continue;
        }
        app.brush->SetColor(line.color);
        app.renderTarget->DrawLine(
            D2D1::Point2F(line.p1.x - app.scrollX, line.p1.y - app.scrollY),
            D2D1::Point2F(line.p2.x - app.scrollX, line.p2.y - app.scrollY),
            app.brush, line.stroke);
        app.drawCalls++;
    }

    // Determine scrollbar visibility
    bool needsVScroll = app.contentHeight > app.height;
    bool needsHScroll = app.contentWidth > app.width;
    float scrollbarSize = 14.0f;

    // Scrollbar color: dark on light themes, light on dark themes
    float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;

    // Draw vertical scrollbar
    if (needsVScroll) {
        float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
        float trackHeight = app.height - (needsHScroll ? scrollbarSize : 0);
        float sbHeight = trackHeight / app.contentHeight * trackHeight;
        sbHeight = std::max(sbHeight, 30.0f);
        float sbY = (maxScrollY > 0) ? (app.scrollY / maxScrollY * (trackHeight - sbHeight)) : 0;

        float sbWidth = (app.scrollbarHovered || app.scrollbarDragging) ? 10.0f : 6.0f;
        float sbAlpha = (app.scrollbarHovered || app.scrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(app.width - sbWidth - 4, sbY,
                                          app.width - 4, sbY + sbHeight), 3, 3),
            app.brush);
        app.drawCalls++;
    }

    // Draw horizontal scrollbar
    if (needsHScroll) {
        float maxScrollX = std::max(0.0f, app.contentWidth - app.width);
        float trackWidth = app.width - (needsVScroll ? scrollbarSize : 0);
        float sbWidth = trackWidth / app.contentWidth * trackWidth;
        sbWidth = std::max(sbWidth, 30.0f);
        float sbX = (maxScrollX > 0) ? (app.scrollX / maxScrollX * (trackWidth - sbWidth)) : 0;

        float sbHeight = (app.hScrollbarHovered || app.hScrollbarDragging) ? 10.0f : 6.0f;
        float sbAlpha = (app.hScrollbarHovered || app.hScrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(sbX, app.height - sbHeight - 4,
                                          sbX + sbWidth, app.height - 4), 3, 3),
            app.brush);
        app.drawCalls++;
    }

    // Draw selection highlights
    if ((app.selecting || app.hasSelection) && !app.textRects.empty()) {
        // Calculate selection bounds (normalized so start is always before end)
        // Selection is stored in document coordinates
        float selStartX = (float)app.selStartX;
        float selStartY = (float)app.selStartY;
        float selEndX = (float)app.selEndX;
        float selEndY = (float)app.selEndY;

        // Swap if selection was made bottom-to-top
        if (selStartY > selEndY || (selStartY == selEndY && selStartX > selEndX)) {
            std::swap(selStartX, selEndX);
            std::swap(selStartY, selEndY);
        }

        // Check if this is a "select all" (selectedText is set but selection coords are same)
        bool isSelectAll = app.hasSelection && !app.selectedText.empty() &&
                          app.selStartX == app.selEndX && app.selStartY == app.selEndY;

        app.brush->SetColor(D2D1::ColorF(0.2f, 0.4f, 0.9f, 0.35f));

        const auto& lines = app.lineBuckets;

        std::wstring collectedText;
        size_t selectedCount = 0;

        for (size_t i = 0; i < lines.size(); i++) {
            const auto& line = lines[i];
            float lineCenterY = (line.top + line.bottom) / 2;

            bool lineInSelection = false;
            float drawLeft = line.minX;
            float drawRight = line.maxX;

            if (isSelectAll) {
                lineInSelection = true;
            } else if (lineCenterY >= selStartY - 10 && lineCenterY <= selEndY + 10) {
                float lineHeight = line.bottom - line.top;
                bool isSingleLine = (selEndY - selStartY) <= lineHeight;

                if (isSingleLine) {
                    // Single line selection
                    drawLeft = std::max(line.minX, selStartX);
                    drawRight = std::min(line.maxX, selEndX);
                    if (drawLeft < drawRight) lineInSelection = true;
                } else if (lineCenterY < selStartY + lineHeight) {
                    // First line - from selection start to end of line
                    drawLeft = std::max(line.minX, selStartX);
                    lineInSelection = true;
                } else if (lineCenterY > selEndY - lineHeight) {
                    // Last line - from start of line to selection end
                    drawRight = std::min(line.maxX, selEndX);
                    lineInSelection = true;
                } else {
                    // Middle line - full width
                    lineInSelection = true;
                }
            }

            if (lineInSelection) {
                // Draw continuous selection bar for this line
                app.renderTarget->FillRectangle(
                    D2D1::RectF(drawLeft - app.scrollX, line.top - app.scrollY,
                                drawRight - app.scrollX, line.bottom - app.scrollY),
                    app.brush);
                selectedCount++;

                // Collect text from rects in this line that fall within selection
                if (!collectedText.empty()) collectedText += L"\n";
                for (size_t idx : line.textRectIndices) {
                    const auto& tr = app.textRects[idx];
                    const D2D1_RECT_F& rect = tr.rect;
                    if (rect.left < drawRight && rect.right > drawLeft) {
                        if (!collectedText.empty() && collectedText.back() != L'\n') {
                            collectedText += L" ";
                        }
                        std::wstring_view slice = textViewForRect(app, tr);
                        collectedText.append(slice.data(), slice.size());
                    }
                }
            }
        }
        app.drawCalls += selectedCount;

        // Update selectedText for mouse selections (not select-all)
        if (!isSelectAll && app.hasSelection && selectedCount > 0) {
            app.selectedText = collectedText;
        }
    }

    // Draw search match highlights (search live through visible textRects)
    if (app.showSearch && !app.searchQuery.empty() && !app.textRects.empty() && !app.searchMatches.empty()) {
        // Collect visible match rects by intersecting search matches with text rects
        struct VisibleMatch {
            D2D1_RECT_F rect;
            size_t matchIndex;
        };
        std::vector<VisibleMatch> visibleMatches;

        size_t matchIndex = 0;
        for (const auto& tr : app.textRects) {
            size_t textLen = tr.docLength;
            if (textLen == 0) continue;

            size_t rectStart = tr.docStart;
            size_t rectEnd = rectStart + textLen;

            // Advance to first match that could overlap this rect
            while (matchIndex < app.searchMatches.size()) {
                const auto& m = app.searchMatches[matchIndex];
                size_t mEnd = m.startPos + m.length;
                if (mEnd <= rectStart) {
                    matchIndex++;
                    continue;
                }
                break;
            }

            size_t mi = matchIndex;
            while (mi < app.searchMatches.size()) {
                const auto& m = app.searchMatches[mi];
                if (m.startPos >= rectEnd) break;

                size_t mStart = m.startPos;
                size_t mEnd = m.startPos + m.length;
                size_t overlapStart = std::max(rectStart, mStart);
                size_t overlapEnd = std::min(rectEnd, mEnd);

                if (overlapStart < overlapEnd) {
                    float totalWidth = tr.rect.right - tr.rect.left;
                    float charWidth = totalWidth / (float)textLen;
                    float startX = tr.rect.left + (overlapStart - rectStart) * charWidth;
                    float matchWidth = (overlapEnd - overlapStart) * charWidth;

                    // Extend highlight slightly for better visibility
                    D2D1_RECT_F highlightRect = D2D1::RectF(
                        startX - 1, tr.rect.top,
                        startX + matchWidth + 1, tr.rect.bottom
                    );

                    visibleMatches.push_back({highlightRect, mi});
                }

                if (mEnd <= rectEnd) {
                    mi++;
                } else {
                    break;  // Match spans beyond this rect; continue on next rect
                }
            }

            matchIndex = mi;
        }

        // Draw all matches - orange if it's the current match, yellow otherwise
        for (const auto& vm : visibleMatches) {
            bool isCurrent = (app.searchCurrentIndex >= 0 &&
                              vm.matchIndex == (size_t)app.searchCurrentIndex);

            if (isCurrent) {
                app.brush->SetColor(D2D1::ColorF(1.0f, 0.6f, 0.0f, 0.5f));  // Orange
            } else {
                app.brush->SetColor(D2D1::ColorF(1.0f, 0.9f, 0.0f, 0.3f));  // Yellow
            }

            app.renderTarget->FillRectangle(
                D2D1::RectF(vm.rect.left - app.scrollX, vm.rect.top - app.scrollY,
                            vm.rect.right - app.scrollX, vm.rect.bottom - app.scrollY),
                app.brush);
            app.drawCalls++;
        }
    }

    // "Copied!" notification with fade out
    if (app.showCopiedNotification) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - app.copiedNotificationStart).count();

        if (elapsed < 2.0f) {
            // Fade out over 2 seconds (stay solid for first 0.5s, then fade)
            float alpha = 1.0f;
            if (elapsed > 0.5f) {
                alpha = 1.0f - (elapsed - 0.5f) / 1.5f;
            }
            app.copiedNotificationAlpha = alpha;

            const wchar_t* copyText = L"Copied!";
            float copyWidth = 100.0f;
            float copyHeight = 26;
            float pillX = (app.width - copyWidth) / 2;
            float pillY = 10;

            // Background pill (green for success)
            app.brush->SetColor(D2D1::ColorF(0.2f, 0.7f, 0.3f, 0.9f * alpha));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + copyWidth, pillY + copyHeight), 13, 13),
                app.brush);

            // Text
            app.brush->SetColor(D2D1::ColorF(1, 1, 1, alpha));
            app.renderTarget->DrawText(copyText, (UINT32)wcslen(copyText), app.textFormat,
                D2D1::RectF(pillX + 10, pillY + 3, pillX + copyWidth - 10, pillY + copyHeight),
                app.brush);
            app.drawCalls++;

            // Keep animating
            InvalidateRect(app.hwnd, nullptr, FALSE);
        } else {
            app.showCopiedNotification = false;
        }
    }

    // Draw stats
    if (app.showStats) {
        wchar_t stats[512];
        swprintf(stats, 512,
            L"Parse: %zu us | Draw calls: %zu\n"
            L"Startup: %.1fms (Win: %.1f | D2D: %.1f | DWrite: %.1f | File: %.1f)",
            app.parseTimeUs,
            app.drawCalls,
            app.metrics.totalStartupUs / 1000.0,
            app.metrics.windowInitUs / 1000.0,
            app.metrics.d2dInitUs / 1000.0,
            app.metrics.dwriteInitUs / 1000.0,
            app.metrics.fileLoadUs / 1000.0);

        float statsWidth = 600;
        float statsHeight = 50;

        app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.8f));
        app.renderTarget->FillRectangle(
            D2D1::RectF(app.width - statsWidth - 10, app.height - statsHeight - 10,
                       app.width - 10, app.height - 10),
            app.brush);

        app.brush->SetColor(D2D1::ColorF(0.7f, 0.9f, 0.7f));
        app.renderTarget->DrawText(stats, (UINT32)wcslen(stats), app.codeFormat,
            D2D1::RectF(app.width - statsWidth - 5, app.height - statsHeight - 5,
                       app.width - 15, app.height - 15),
            app.brush);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // SEARCH OVERLAY
    // ═══════════════════════════════════════════════════════════════════════════
    if (app.showSearch) {
        // Animate in
        if (app.searchAnimation < 1.0f) {
            app.searchAnimation = std::min(1.0f, app.searchAnimation + 0.2f);
            InvalidateRect(app.hwnd, nullptr, FALSE);
        }
        float anim = app.searchAnimation;

        // Search bar dimensions
        float barWidth = std::min(500.0f, app.width - 40.0f);
        float barHeight = 44.0f;
        float barX = (app.width - barWidth) / 2;
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
                if (app.searchMatches.empty()) {
                    wcscpy_s(countText, L"No matches");
                    // Red color for no matches
                    app.brush->SetColor(D2D1::ColorF(0.9f, 0.3f, 0.3f, anim));
                } else {
                    swprintf_s(countText, L"%d of %zu", app.searchCurrentIndex + 1, app.searchMatches.size());
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

    // ═══════════════════════════════════════════════════════════════════════════
    // THEME CHOOSER OVERLAY
    // ═══════════════════════════════════════════════════════════════════════════
    if (app.showThemeChooser) {
        // Animate in
        if (app.themeChooserAnimation < 1.0f) {
            app.themeChooserAnimation = std::min(1.0f, app.themeChooserAnimation + 0.15f);
            // Keep invalidating to continue animation
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

    app.renderTarget->EndDraw();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = g_app;

    switch (msg) {
        case WM_SIZE:
            if (app && app->d2dFactory) {
                app->width = LOWORD(lParam);
                app->height = HIWORD(lParam);
                createRenderTarget(*app);
                app->layoutDirty = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_DPICHANGED:
            if (app) {
                UINT dpi = HIWORD(wParam);
                app->contentScale = dpi / 96.0f;

                // Resize window to suggested new size
                RECT* newRect = (RECT*)lParam;
                SetWindowPos(hwnd, nullptr,
                    newRect->left, newRect->top,
                    newRect->right - newRect->left,
                    newRect->bottom - newRect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);

                // Recreate text formats and render target for new DPI
                updateTextFormats(*app);
                createRenderTarget(*app);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (app) render(*app);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEWHEEL:
            if (app) {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;

                if (ctrl) {
                    // Zoom in/out
                    float zoomDelta = delta * 0.1f;
                    app->zoomFactor = std::max(0.5f, std::min(3.0f, app->zoomFactor + zoomDelta));
                    updateTextFormats(*app);
                } else {
                    // Normal scroll
                    app->targetScrollY -= delta * 60.0f;
                    float maxScroll = std::max(0.0f, app->contentHeight - app->height);
                    app->targetScrollY = std::max(0.0f, std::min(app->targetScrollY, maxScroll));
                    app->scrollY = app->targetScrollY;
                }

                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_MOUSEHWHEEL:
            if (app) {
                // Horizontal scroll
                float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 60.0f;
                app->targetScrollX += delta;

                float maxScrollX = std::max(0.0f, app->contentWidth - app->width);
                app->targetScrollX = std::max(0.0f, std::min(app->targetScrollX, maxScrollX));
                app->scrollX = app->targetScrollX;

                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_MOUSEMOVE:
            if (app) {
                app->mouseX = GET_X_LPARAM(lParam);
                app->mouseY = GET_Y_LPARAM(lParam);
                float docX = app->mouseX + app->scrollX;
                float docY = app->mouseY + app->scrollY;

                // Text selection dragging
                if (app->selecting) {
                    if (app->selectionMode == App::SelectionMode::Word) {
                        // Extend selection by words - merge anchor with current word
                        const App::TextRect* tr = findTextRectAt(*app, (int)docX, (int)docY);
                        if (tr) {
                            float wordLeft, wordRight;
                            if (findWordBoundsAt(*app, *tr, (int)docX, wordLeft, wordRight)) {
                                // Selection spans from min(anchor, current) to max(anchor, current)
                                app->selStartX = (int)std::min(app->anchorLeft, wordLeft);
                                app->selEndX = (int)std::max(app->anchorRight, wordRight);
                                app->selStartY = (int)(std::min(app->anchorTop, tr->rect.top));
                                app->selEndY = (int)(std::max(app->anchorBottom, tr->rect.bottom));
                                app->hasSelection = true;
                            }
                        }
                    } else if (app->selectionMode == App::SelectionMode::Line) {
                        // Extend selection by lines - merge anchor with current line
                        float lineLeft, lineRight, lineTop, lineBottom;
                        findLineRects(*app, docY, lineLeft, lineRight, lineTop, lineBottom);
                        if (lineRight > lineLeft) {
                            // Selection spans from min(anchor, current) to max(anchor, current)
                            app->selStartX = (int)std::min(app->anchorLeft, lineLeft);
                            app->selEndX = (int)std::max(app->anchorRight, lineRight);
                            app->selStartY = (int)(std::min(app->anchorTop, lineTop));
                            app->selEndY = (int)(std::max(app->anchorBottom, lineBottom));
                            app->hasSelection = true;
                        }
                    } else {
                        // Normal selection - store in document coordinates
                        app->selEndX = (int)docX;
                        app->selEndY = (int)docY;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                // Vertical scrollbar dragging
                if (app->scrollbarDragging) {
                    float maxScroll = std::max(0.0f, app->contentHeight - app->height);
                    if (maxScroll > 0 && app->contentHeight > app->height) {
                        float sbHeight = (float)app->height / app->contentHeight * app->height;
                        sbHeight = std::max(sbHeight, 30.0f);
                        float trackHeight = app->height - sbHeight;

                        float deltaY = (float)app->mouseY - app->scrollbarDragStartY;
                        float scrollDelta = (deltaY / trackHeight) * maxScroll;
                        app->scrollY = app->scrollbarDragStartScroll + scrollDelta;
                        app->scrollY = std::max(0.0f, std::min(app->scrollY, maxScroll));
                        app->targetScrollY = app->scrollY;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }

                // Horizontal scrollbar dragging
                if (app->hScrollbarDragging) {
                    float maxScroll = std::max(0.0f, app->contentWidth - app->width);
                    if (maxScroll > 0 && app->contentWidth > app->width) {
                        float sbWidth = (float)app->width / app->contentWidth * app->width;
                        sbWidth = std::max(sbWidth, 30.0f);
                        float trackWidth = app->width - sbWidth;

                        float deltaX = (float)app->mouseX - app->hScrollbarDragStartX;
                        float scrollDelta = (deltaX / trackWidth) * maxScroll;
                        app->scrollX = app->hScrollbarDragStartScroll + scrollDelta;
                        app->scrollX = std::max(0.0f, std::min(app->scrollX, maxScroll));
                        app->targetScrollX = app->scrollX;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }

                // Check vertical scrollbar hover
                bool wasHovered = app->scrollbarHovered;
                app->scrollbarHovered = false;
                if (app->contentHeight > app->height) {
                    float sbWidth = 14.0f;  // hit area
                    if (app->mouseX >= app->width - sbWidth) {
                        app->scrollbarHovered = true;
                    }
                }

                // Check horizontal scrollbar hover
                bool wasHHovered = app->hScrollbarHovered;
                app->hScrollbarHovered = false;
                if (app->contentWidth > app->width) {
                    float sbHeight = 14.0f;  // hit area
                    if (app->mouseY >= app->height - sbHeight) {
                        app->hScrollbarHovered = true;
                    }
                }

                // Check link hover
                std::string prevHoveredLink = app->hoveredLink;
                app->hoveredLink.clear();
                for (const auto& lr : app->linkRects) {
                    if (docX >= lr.bounds.left && docX <= lr.bounds.right &&
                        docY >= lr.bounds.top && docY <= lr.bounds.bottom) {
                        app->hoveredLink = lr.url;
                        break;
                    }
                }

                // Check if over text
                bool wasOverText = app->overText;
                app->overText = (findTextRectAt(*app, (int)docX, (int)docY) != nullptr);

                // Update cursor
                if (app->scrollbarHovered || app->scrollbarDragging ||
                    app->hScrollbarHovered || app->hScrollbarDragging) {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                } else if (!app->hoveredLink.empty()) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                } else if (app->overText) {
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                } else {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                }

                if (wasHovered != app->scrollbarHovered ||
                    wasHHovered != app->hScrollbarHovered ||
                    prevHoveredLink != app->hoveredLink) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (app) {
                // If theme chooser is open, don't start selection - just record for click handling
                if (app->showThemeChooser) {
                    return 0;
                }

                app->mouseDown = true;
                app->mouseX = GET_X_LPARAM(lParam);
                app->mouseY = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
                float docX = app->mouseX + app->scrollX;
                float docY = app->mouseY + app->scrollY;

                // Check if clicking vertical scrollbar
                if (app->scrollbarHovered && app->contentHeight > app->height) {
                    app->scrollbarDragging = true;
                    app->scrollbarDragStartY = (float)app->mouseY;
                    app->scrollbarDragStartScroll = app->scrollY;

                    // Check if clicking in track (not thumb) - jump to position
                    float maxScroll = std::max(0.0f, app->contentHeight - app->height);
                    float sbHeight = (float)app->height / app->contentHeight * app->height;
                    sbHeight = std::max(sbHeight, 30.0f);
                    float sbY = (maxScroll > 0) ? (app->scrollY / maxScroll * (app->height - sbHeight)) : 0;

                    // If clicked outside thumb, jump
                    if (app->mouseY < sbY || app->mouseY > sbY + sbHeight) {
                        float trackHeight = app->height - sbHeight;
                        float clickPos = (float)app->mouseY - sbHeight / 2;
                        clickPos = std::max(0.0f, std::min(clickPos, trackHeight));
                        app->scrollY = (clickPos / trackHeight) * maxScroll;
                        app->targetScrollY = app->scrollY;
                        app->scrollbarDragStartScroll = app->scrollY;
                        app->scrollbarDragStartY = (float)app->mouseY;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                // Check if clicking horizontal scrollbar
                else if (app->hScrollbarHovered && app->contentWidth > app->width) {
                    app->hScrollbarDragging = true;
                    app->hScrollbarDragStartX = (float)app->mouseX;
                    app->hScrollbarDragStartScroll = app->scrollX;

                    // Check if clicking in track (not thumb) - jump to position
                    float maxScroll = std::max(0.0f, app->contentWidth - app->width);
                    float sbWidth = (float)app->width / app->contentWidth * app->width;
                    sbWidth = std::max(sbWidth, 30.0f);
                    float sbX = (maxScroll > 0) ? (app->scrollX / maxScroll * (app->width - sbWidth)) : 0;

                    // If clicked outside thumb, jump
                    if (app->mouseX < sbX || app->mouseX > sbX + sbWidth) {
                        float trackWidth = app->width - sbWidth;
                        float clickPos = (float)app->mouseX - sbWidth / 2;
                        clickPos = std::max(0.0f, std::min(clickPos, trackWidth));
                        app->scrollX = (clickPos / trackWidth) * maxScroll;
                        app->targetScrollX = app->scrollX;
                        app->hScrollbarDragStartScroll = app->scrollX;
                        app->hScrollbarDragStartX = (float)app->mouseX;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    // Detect double/triple clicks
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - app->lastClickTime).count();

                    // Check if this is a repeated click (within 500ms and 5px of last click)
                    bool isRepeatedClick = (elapsed < 500 &&
                        std::abs(app->mouseX - app->lastClickX) < 5 &&
                        std::abs(app->mouseY - app->lastClickY) < 5);

                    if (isRepeatedClick) {
                        app->clickCount = std::min(app->clickCount + 1, 3);
                    } else {
                        app->clickCount = 1;
                    }

                    app->lastClickTime = now;
                    app->lastClickX = app->mouseX;
                    app->lastClickY = app->mouseY;

                    // Handle based on click count
                    if (app->clickCount == 2) {
                        // Double-click: select word
                        const App::TextRect* tr = findTextRectAt(*app, (int)docX, (int)docY);
                        if (tr) {
                            float wordLeft, wordRight;
                            if (findWordBoundsAt(*app, *tr, (int)docX, wordLeft, wordRight)) {
                                app->selectionMode = App::SelectionMode::Word;
                                // Store anchor (the original word bounds) in document coords for drag extension
                                app->anchorLeft = wordLeft;
                                app->anchorRight = wordRight;
                                app->anchorTop = tr->rect.top;
                                app->anchorBottom = tr->rect.bottom;
                                // Set selection to the word (document coordinates)
                                app->selStartX = (int)wordLeft;
                                app->selEndX = (int)wordRight;
                                app->selStartY = (int)tr->rect.top;
                                app->selEndY = (int)tr->rect.bottom;
                                app->selecting = true;
                                app->hasSelection = true;
                            }
                        }
                    } else if (app->clickCount == 3) {
                        // Triple-click: select line
                        float lineLeft, lineRight, lineTop, lineBottom;
                        findLineRects(*app, docY, lineLeft, lineRight, lineTop, lineBottom);
                        if (lineRight > lineLeft) {
                            app->selectionMode = App::SelectionMode::Line;
                            // Store anchor (the original line bounds) in document coords for drag extension
                            app->anchorLeft = lineLeft;
                            app->anchorRight = lineRight;
                            app->anchorTop = lineTop;
                            app->anchorBottom = lineBottom;
                            // Set selection to the line (document coordinates)
                            app->selStartX = (int)lineLeft;
                            app->selEndX = (int)lineRight;
                            app->selStartY = (int)lineTop;
                            app->selEndY = (int)lineBottom;
                            app->selecting = true;
                            app->hasSelection = true;
                        }
                    } else {
                        // Single click: start normal selection (document coordinates)
                        app->selectionMode = App::SelectionMode::Normal;
                        app->selecting = true;
                        app->selStartX = (int)docX;
                        app->selStartY = (int)docY;
                        app->selEndX = (int)docX;
                        app->selEndY = (int)docY;
                        app->hasSelection = false;
                        app->selectedText.clear();
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (app) {
                ReleaseCapture();

                // Theme chooser click handling
                if (app->showThemeChooser) {
                    int clickX = GET_X_LPARAM(lParam);
                    int clickY = GET_Y_LPARAM(lParam);

                    // Calculate which theme was clicked (replicate layout logic)
                    float panelWidth = std::min(900.0f, (float)app->width - 80.0f);
                    float panelHeight = std::min(620.0f, (float)app->height - 80.0f);
                    float panelX = (app->width - panelWidth) / 2;
                    float panelY = (app->height - panelHeight) / 2;
                    float gridStartY = panelY + 75;
                    float cardWidth = (panelWidth - 60) / 2;
                    float cardHeight = (panelHeight - 130) / 5;
                    float cardPadding = 8;

                    int clickedTheme = -1;
                    for (int i = 0; i < THEME_COUNT; i++) {
                        const D2DTheme& t = THEMES[i];
                        int col = t.isDark ? 1 : 0;
                        int row = t.isDark ? (i - 5) : i;

                        float cardX = panelX + 20 + col * (cardWidth + 20);
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
                        applyTheme(*app, clickedTheme);
                        app->showThemeChooser = false;
                        app->themeChooserAnimation = 0;
                    }
                    // If clicked outside themes, just close the chooser
                    else {
                        app->showThemeChooser = false;
                        app->themeChooserAnimation = 0;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                if (app->scrollbarDragging) {
                    app->scrollbarDragging = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (app->hScrollbarDragging) {
                    app->hScrollbarDragging = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (app->selecting) {
                    // Finalize selection based on mode
                    if (app->selectionMode == App::SelectionMode::Word ||
                        app->selectionMode == App::SelectionMode::Line) {
                        // Word/Line selection: keep the bounds set during mouse down/move
                        // hasSelection was already set to true in WM_LBUTTONDOWN
                    } else {
                        // Normal selection: finalize with current mouse position (document coordinates)
                        float docX = app->mouseX + app->scrollX;
                        float docY = app->mouseY + app->scrollY;
                        app->selEndX = (int)docX;
                        app->selEndY = (int)docY;

                        // Check if this was a meaningful drag (not just a click)
                        // Use screen coordinates stored from mouse down
                        int dx = abs(app->mouseX - app->lastClickX);
                        int dy = abs(app->mouseY - app->lastClickY);
                        if (dx > 5 || dy > 5) {
                            app->hasSelection = true;
                        } else if (!app->hoveredLink.empty()) {
                            // It was just a click on a link
                            openUrl(app->hoveredLink);
                            app->hasSelection = false;
                        } else {
                            app->hasSelection = false;
                        }
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (!app->hoveredLink.empty()) {
                    // Click on link
                    openUrl(app->hoveredLink);
                }

                app->mouseDown = false;
                app->selecting = false;
            }
            return 0;

        case WM_SETCURSOR:
            if (app && LOWORD(lParam) == HTCLIENT) {
                // We handle cursor in WM_MOUSEMOVE
                return TRUE;
            }
            break;

        case WM_KEYDOWN:
            if (app) {
                float pageSize = app->height * 0.8f;
                float maxScroll = std::max(0.0f, app->contentHeight - app->height);
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

                // Handle search-specific keys when search is active
                if (app->showSearch && app->searchActive) {
                    switch (wParam) {
                        case VK_ESCAPE:
                            // Close search
                            app->showSearch = false;
                            app->searchActive = false;
                            app->searchQuery.clear();
                            app->searchMatches.clear();
                            app->searchAnimation = 0;
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        case VK_RETURN:
                            // Cycle to next match
                            if (!app->searchMatches.empty()) {
                                app->searchCurrentIndex = (app->searchCurrentIndex + 1) % (int)app->searchMatches.size();
                                scrollToCurrentMatch(*app);
                                InvalidateRect(hwnd, nullptr, FALSE);
                            }
                            return 0;
                        case VK_BACK:
                            // Delete last character
                            if (!app->searchQuery.empty()) {
                                app->searchQuery.pop_back();
                                performSearch(*app);
                                if (!app->searchMatches.empty()) {
                                    scrollToCurrentMatch(*app);
                                }
                                InvalidateRect(hwnd, nullptr, FALSE);
                            }
                            return 0;
                    }
                }

                if (ctrl) {
                    switch (wParam) {
                        case 'A': {
                            // Select All - extract all text from document
                            if (app->root) {
                                app->selectedText.clear();
                                extractText(app->root, app->selectedText);
                                app->hasSelection = true;
                            }
                            break;
                        }
                        case 'C': {
                            // Copy - copy selected text or all text if select all was used
                            bool copied = false;
                            if (app->hasSelection && !app->selectedText.empty()) {
                                copyToClipboard(hwnd, app->selectedText);
                                app->hasSelection = false;
                                app->selectedText.clear();
                                copied = true;
                            } else if (app->root) {
                                // If no selection, copy all
                                std::wstring allText;
                                extractText(app->root, allText);
                                copyToClipboard(hwnd, allText);
                                copied = true;
                            }
                            // Show "Copied!" notification
                            if (copied) {
                                app->showCopiedNotification = true;
                                app->copiedNotificationAlpha = 1.0f;
                                app->copiedNotificationStart = std::chrono::steady_clock::now();
                                InvalidateRect(hwnd, nullptr, FALSE);
                            }
                            break;
                        }
                        case 'F':
                            // Ctrl+F to open search
                            if (!app->showSearch) {
                                app->showSearch = true;
                                app->searchActive = true;
                                app->searchAnimation = 0;
                                app->searchQuery.clear();
                                app->searchMatches.clear();
                                app->searchCurrentIndex = 0;
                                app->searchJustOpened = true;
                            }
                            InvalidateRect(hwnd, nullptr, FALSE);
                            break;
                    }
                } else {
                    switch (wParam) {
                        case VK_ESCAPE:
                            // Priority: Search > Theme chooser > Quit
                            if (app->showSearch) {
                                app->showSearch = false;
                                app->searchActive = false;
                                app->searchQuery.clear();
                                app->searchMatches.clear();
                                app->searchAnimation = 0;
                            } else if (app->showThemeChooser) {
                                app->showThemeChooser = false;
                                app->themeChooserAnimation = 0;
                            } else {
                                PostQuitMessage(0);
                            }
                            break;
                        case 'Q':
                            if (!app->showThemeChooser && !app->showSearch) {
                                PostQuitMessage(0);
                            }
                            break;
                        case 'T':
                            if (!app->showSearch) {
                                app->showThemeChooser = !app->showThemeChooser;
                                if (app->showThemeChooser) {
                                    app->themeChooserAnimation = 0;
                                }
                            }
                            InvalidateRect(hwnd, nullptr, FALSE);
                            break;
                        case 'F':
                            // F to open search (when not in search mode)
                            if (!app->showSearch && !app->showThemeChooser) {
                                app->showSearch = true;
                                app->searchActive = true;
                                app->searchAnimation = 0;
                                app->searchQuery.clear();
                                app->searchMatches.clear();
                                app->searchCurrentIndex = 0;
                                app->searchJustOpened = true;
                                InvalidateRect(hwnd, nullptr, FALSE);
                            }
                            break;
                        case VK_UP:
                        case 'K':
                            if (!app->showSearch) {
                                app->targetScrollY -= 50;
                            }
                            break;
                        case VK_DOWN:
                        case 'J':
                            if (!app->showSearch) {
                                app->targetScrollY += 50;
                            }
                            break;
                        case VK_PRIOR: // Page Up
                            app->targetScrollY -= pageSize;
                            break;
                        case VK_NEXT: // Page Down
                        case VK_SPACE:
                            if (!app->showSearch) {
                                app->targetScrollY += pageSize;
                            }
                            break;
                        case VK_HOME:
                            app->targetScrollY = 0;
                            break;
                        case VK_END:
                            app->targetScrollY = maxScroll;
                            break;
                        case 'S':
                            if (!app->showSearch) {
                                app->showStats = !app->showStats;
                            }
                            break;
                    }
                }

                app->targetScrollY = std::max(0.0f, std::min(app->targetScrollY, maxScroll));
                app->scrollY = app->targetScrollY;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_CHAR:
            if (app && app->showSearch && app->searchActive) {
                // Skip the character that opened search (F key)
                if (app->searchJustOpened) {
                    app->searchJustOpened = false;
                    return 0;
                }
                wchar_t ch = (wchar_t)wParam;
                // Only handle printable characters (not control chars)
                if (ch >= 32 && ch != 127) {
                    app->searchQuery += ch;
                    performSearch(*app);
                    if (!app->searchMatches.empty()) {
                        app->searchCurrentIndex = 0;
                        scrollToCurrentMatch(*app);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_DROPFILES:
            if (app) {
                HDROP hDrop = (HDROP)wParam;
                wchar_t wpath[MAX_PATH];
                if (DragQueryFileW(hDrop, 0, wpath, MAX_PATH)) {
                    // Convert wide path to UTF-8 for std::string usage
                    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                    std::string filepath(utf8Len - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, &filepath[0], utf8Len, nullptr, nullptr);
                    size_t dotPos = filepath.rfind('.');
                    if (dotPos != std::string::npos) {
                        std::string ext = filepath.substr(dotPos);
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".md" || ext == ".markdown" || ext == ".txt") {
                            // Load file - use wide path for non-ASCII support
                            std::ifstream file(wpath);
                            if (file) {
                                std::stringstream buffer;
                                buffer << file.rdbuf();
                                auto result = app->parser.parse(buffer.str());
                                if (result.success) {
                                    app->root = result.root;
                                    app->parseTimeUs = result.parseTimeUs;
                                    app->currentFile = filepath;
                                    app->scrollY = 0;
                                    app->targetScrollY = 0;
                                    app->contentHeight = 0;
                                    app->docText.clear();
                                    app->docTextLower.clear();
                                    app->searchMatches.clear();
                                    app->searchMatchYs.clear();
                                    app->layoutDirty = true;
                                }
                            }
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                    }
                }
                DragFinish(hDrop);
            }
            return 0;

        case WM_DESTROY:
            {
                // Load existing settings to preserve values like hasAskedFileAssociation
                Settings settings = loadSettings();
                settings.themeIndex = app->currentThemeIndex;
                settings.zoomFactor = app->zoomFactor;

                // Get window placement for position/size/maximized state
                WINDOWPLACEMENT wp = {};
                wp.length = sizeof(wp);
                if (GetWindowPlacement(hwnd, &wp)) {
                    settings.windowMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
                    // Save the restored (non-maximized) position
                    settings.windowX = wp.rcNormalPosition.left;
                    settings.windowY = wp.rcNormalPosition.top;
                    settings.windowWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
                    settings.windowHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
                }

                saveSettings(settings);
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static const char* sampleMarkdown = R"(# Welcome to Tinta

**Tinta** is a fast, lightweight markdown reader for Windows.

## Features

- Lightning-fast startup with Direct2D
- Hardware-accelerated text rendering via DirectWrite
- Minimal dependencies
- Small binary size

## Code Example

```cpp
int main() {
    printf("Hello, World!\n");
    return 0;
}
```

## Keyboard Shortcuts

- **F** or **Ctrl+F** - Open search
- **T** - Open theme chooser
- **S** - Toggle stats overlay
- **Ctrl+C** - Copy text
- **Ctrl+A** - Select all
- **Q** or **ESC** - Quit
)";

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    // Enable per-monitor DPI V2 awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto startupStart = Clock::now();

    App app;
    auto t0 = startupStart;
    g_app = &app;

    // Load saved settings
    Settings savedSettings = loadSettings();
    app.currentThemeIndex = savedSettings.themeIndex;
    app.theme = THEMES[savedSettings.themeIndex];
    app.darkMode = app.theme.isDark;
    app.zoomFactor = savedSettings.zoomFactor;

    // Parse command line
    std::string inputFile;
    bool lightMode = false;
    bool forceRegister = false;

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];
        if (arg == L"-l" || arg == L"--light") {
            lightMode = true;
        } else if (arg == L"-s" || arg == L"--stats") {
            app.showStats = true;
        } else if (arg == L"/register" || arg == L"--register") {
            forceRegister = true;
        } else if (arg[0] != L'-' && arg[0] != L'/') {
            // Convert to UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, nullptr, 0, nullptr, nullptr);
            inputFile.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, &inputFile[0], len, nullptr, nullptr);
        }
    }
    LocalFree(argv);

    // Handle /register command
    if (forceRegister) {
        if (registerFileAssociation()) {
            MessageBoxW(nullptr,
                       L"Tinta has been registered.\n\n"
                       L"In the Settings window that opens:\n"
                       L"1. Search for '.md'\n"
                       L"2. Click on the current default app\n"
                       L"3. Select 'Tinta' from the list",
                       L"Almost done!", MB_OK | MB_ICONINFORMATION);
            openDefaultAppsSettings();
        } else {
            MessageBoxW(nullptr, L"Failed to register file association. Try running as administrator.",
                       L"Error", MB_OK | MB_ICONWARNING);
        }
        return 0;  // Exit after registering
    }

    // Ask about file association on first run
    askAndRegisterFileAssociation(savedSettings);

    if (lightMode) {
        app.currentThemeIndex = 0;  // Paper (first light theme)
        app.theme = THEMES[0];
        app.darkMode = false;
    }

    // Create window with saved position/size
    t0 = Clock::now();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, L"IDI_ICON1");
    wc.hIconSm = LoadIconW(hInstance, L"IDI_ICON1");
    wc.lpszClassName = L"Tinta";
    RegisterClassExW(&wc);

    app.hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"Tinta",
        L"Tinta",
        WS_OVERLAPPEDWINDOW,
        savedSettings.windowX, savedSettings.windowY,
        savedSettings.windowWidth, savedSettings.windowHeight,
        nullptr, nullptr, hInstance, nullptr
    );

    app.metrics.windowInitUs = usElapsed(t0);

    // Get DPI using per-monitor aware API
    app.contentScale = GetDpiForWindow(app.hwnd) / 96.0f;

    // Initialize D2D
    if (!initD2D(app)) {
        MessageBoxW(nullptr, L"Failed to initialize Direct2D", L"Error", MB_OK);
        return 1;
    }

    // Create text formats and typography
    updateTextFormats(app);
    createTypography(app);

    // Get window size
    RECT rc;
    GetClientRect(app.hwnd, &rc);
    app.width = rc.right - rc.left;
    app.height = rc.bottom - rc.top;

    // Create render target
    t0 = Clock::now();
    if (!createRenderTarget(app)) {
        MessageBoxW(nullptr, L"Failed to create render target", L"Error", MB_OK);
        return 1;
    }
    app.metrics.renderTargetUs = usElapsed(t0);

    // Load document
    t0 = Clock::now();

    auto loadMarkdown = [&](const std::string& content) {
        auto result = app.parser.parse(content);
        if (result.success) {
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
        }
    };

    auto loadFile = [&](const std::string& path) -> bool {
        // Use wide string path for ifstream to support non-ASCII paths (MSVC extension)
        std::wstring widePath = toWide(path);
        std::ifstream file(widePath);
        if (!file) return false;
        std::stringstream buffer;
        buffer << file.rdbuf();
        loadMarkdown(buffer.str());
        return true;
    };

    if (!inputFile.empty()) {
        loadFile(inputFile);
        app.currentFile = inputFile;
    } else {
        // Try syntax.md
        if (loadFile("syntax.md")) {
            app.currentFile = "syntax.md";
        } else {
            loadMarkdown(sampleMarkdown);
        }
    }

    app.metrics.fileLoadUs = usElapsed(t0);

    // Show window (respect saved maximized state)
    t0 = Clock::now();
    if (savedSettings.windowMaximized) {
        ShowWindow(app.hwnd, SW_SHOWMAXIMIZED);
    } else {
        ShowWindow(app.hwnd, nCmdShow);
    }
    UpdateWindow(app.hwnd);
    app.metrics.showWindowUs = usElapsed(t0);

    app.metrics.totalStartupUs = usElapsed(startupStart);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_app = nullptr;
    return (int)msg.wParam;
}
