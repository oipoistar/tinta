#include "input.h"
#include "file_utils.h"
#include "utils.h"
#include "search.h"
#include "d2d_init.h"
#include "settings.h"

#include <windowsx.h>
#include <shellapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

// Cached cursor handles - avoid calling LoadCursor() on every WM_MOUSEMOVE
static HCURSOR cursorArrow = LoadCursor(nullptr, IDC_ARROW);
static HCURSOR cursorHand  = LoadCursor(nullptr, IDC_HAND);
static HCURSOR cursorIBeam = LoadCursor(nullptr, IDC_IBEAM);

void handleMouseWheel(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;

    // Handle folder browser scroll
    if (app.showFolderBrowser) {
        float panelWidth = std::min(300.0f, std::max(250.0f, app.width * 0.2f));
        float panelX = -panelWidth * (1.0f - app.folderBrowserAnimation);
        if (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth) {
            // Scroll folder list
            app.folderBrowserScroll -= delta * 60.0f;
            float itemHeight = 28.0f;
            float headerHeight = 48.0f;
            float listHeight = app.height - headerHeight - 20.0f;
            float totalItemsHeight = app.folderItems.size() * itemHeight;
            float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);
            app.folderBrowserScroll = std::max(0.0f, std::min(app.folderBrowserScroll, maxScroll));
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }

    // Handle TOC scroll
    if (app.showToc) {
        float panelWidth = std::min(280.0f, std::max(220.0f, app.width * 0.2f));
        float panelX = app.width - panelWidth * app.tocAnimation;
        if (app.mouseX >= panelX && app.mouseX <= panelX + panelWidth) {
            app.tocScroll -= delta * 60.0f;
            float itemHeight = 28.0f;
            float headerHeight = 48.0f;
            float listHeight = app.height - headerHeight - 20.0f;
            float totalItemsHeight = app.headings.size() * itemHeight;
            float maxScroll = std::max(0.0f, totalItemsHeight - listHeight);
            app.tocScroll = std::max(0.0f, std::min(app.tocScroll, maxScroll));
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }

    if (ctrl) {
        // Zoom in/out
        float zoomDelta = delta * 0.1f;
        app.zoomFactor = std::max(0.5f, std::min(3.0f, app.zoomFactor + zoomDelta));
        updateTextFormats(app);
    } else {
        // Normal scroll
        app.targetScrollY -= delta * 60.0f;
        float maxScroll = std::max(0.0f, app.contentHeight - app.height);
        app.targetScrollY = std::max(0.0f, std::min(app.targetScrollY, maxScroll));
        app.scrollY = app.targetScrollY;
    }

    InvalidateRect(hwnd, nullptr, FALSE);
}

void handleMouseHWheel(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam) {
    // Horizontal scroll
    float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 60.0f;
    app.targetScrollX += delta;

    float maxScrollX = std::max(0.0f, app.contentWidth - app.width);
    app.targetScrollX = std::max(0.0f, std::min(app.targetScrollX, maxScrollX));
    app.scrollX = app.targetScrollX;

    InvalidateRect(hwnd, nullptr, FALSE);
}

void handleMouseMove(App& app, HWND hwnd, LPARAM lParam) {
    app.mouseX = GET_X_LPARAM(lParam);
    app.mouseY = GET_Y_LPARAM(lParam);
    float docX = app.mouseX + app.scrollX;
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
        float maxScroll = std::max(0.0f, app.contentWidth - app.width);
        if (maxScroll > 0 && app.contentWidth > app.width) {
            float sbWidth = (float)app.width / app.contentWidth * app.width;
            sbWidth = std::max(sbWidth, 30.0f);
            float trackWidth = app.width - sbWidth;

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
        float sbWidth = 14.0f;  // hit area
        if (app.mouseX >= app.width - sbWidth) {
            app.scrollbarHovered = true;
        }
    }

    // Check horizontal scrollbar hover
    bool wasHHovered = app.hScrollbarHovered;
    app.hScrollbarHovered = false;
    if (app.contentWidth > app.width) {
        float sbHeight = 14.0f;  // hit area
        if (app.mouseY >= app.height - sbHeight) {
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

    // Update cursor (using cached handles)
    if (app.showFolderBrowser) {
        float panelWidth = std::min(300.0f, std::max(250.0f, app.width * 0.2f));
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
        float panelWidth = std::min(280.0f, std::max(220.0f, app.width * 0.2f));
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
    } else if (!app.hoveredLink.empty()) {
        SetCursor(cursorHand);
    } else if (app.overText) {
        SetCursor(cursorIBeam);
    } else {
        SetCursor(cursorArrow);
    }

    if (wasHovered != app.scrollbarHovered ||
        wasHHovered != app.hScrollbarHovered ||
        prevHoveredLink != app.hoveredLink) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void handleMouseDown(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam) {
    // If theme chooser, folder browser, or TOC is open, don't start selection - just record for click handling
    if (app.showThemeChooser || app.showFolderBrowser || app.showToc) {
        return;
    }

    app.mouseDown = true;
    app.mouseX = GET_X_LPARAM(lParam);
    app.mouseY = GET_Y_LPARAM(lParam);
    SetCapture(hwnd);
    float docX = app.mouseX + app.scrollX;
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
    else if (app.hScrollbarHovered && app.contentWidth > app.width) {
        app.hScrollbarDragging = true;
        app.hScrollbarDragStartX = (float)app.mouseX;
        app.hScrollbarDragStartScroll = app.scrollX;

        // Check if clicking in track (not thumb) - jump to position
        float maxScroll = std::max(0.0f, app.contentWidth - app.width);
        float sbWidth = (float)app.width / app.contentWidth * app.width;
        sbWidth = std::max(sbWidth, 30.0f);
        float sbX = (maxScroll > 0) ? (app.scrollX / maxScroll * (app.width - sbWidth)) : 0;

        // If clicked outside thumb, jump
        if (app.mouseX < sbX || app.mouseX > sbX + sbWidth) {
            float trackWidth = app.width - sbWidth;
            float clickPos = (float)app.mouseX - sbWidth / 2;
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

void handleMouseUp(App& app, HWND hwnd, WPARAM wParam, LPARAM lParam) {
    ReleaseCapture();

    // TOC click handling
    if (app.showToc) {
        int clickX = GET_X_LPARAM(lParam);

        float panelWidth = std::min(280.0f, std::max(220.0f, app.width * 0.2f));
        float panelX = app.width - panelWidth * app.tocAnimation;

        if (clickX >= panelX && (float)clickX <= panelX + panelWidth) {
            // Click inside panel
            if (app.hoveredTocIndex >= 0 && app.hoveredTocIndex < (int)app.headings.size()) {
                // Scroll document to heading
                float headingY = app.headings[app.hoveredTocIndex].y - 20.0f;
                float maxScroll = std::max(0.0f, app.contentHeight - app.height);
                app.scrollY = std::max(0.0f, std::min(headingY, maxScroll));
                app.targetScrollY = app.scrollY;

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
        float panelWidth = std::min(300.0f, std::max(250.0f, app.width * 0.2f));
        float panelX = -panelWidth * (1.0f - app.folderBrowserAnimation);

        // Check if click is inside panel
        if (clickX >= panelX && clickX <= panelX + panelWidth) {
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
                    // Open .md file
                    std::wstring fullPath = app.folderBrowserPath;
                    if (fullPath.back() != L'\\' && fullPath.back() != L'/') {
                        fullPath += L'\\';
                    }
                    fullPath += item.name;

                    // Load the file
                    std::ifstream file(fullPath);
                    if (file) {
                        std::stringstream buffer;
                        buffer << file.rdbuf();
                        auto result = app.parser.parse(buffer.str());
                        if (result.success) {
                            app.root = result.root;
                            app.parseTimeUs = result.parseTimeUs;
                            // Convert wide path to UTF-8 for currentFile
                            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
                            app.currentFile.resize(utf8Len - 1);
                            WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), -1, &app.currentFile[0], utf8Len, nullptr, nullptr);
                            app.scrollY = 0;
                            app.targetScrollY = 0;
                            app.contentHeight = 0;
                            app.docText.clear();
                            app.docTextLower.clear();
                            app.searchMatches.clear();
                            app.searchMatchYs.clear();
                            app.layoutDirty = true;
                            updateFileWriteTime(app);

                            // Close folder browser after opening file
                            app.showFolderBrowser = false;
                            app.folderBrowserAnimation = 0;
                        }
                    }
                }
            }
        } else {
            // Click outside panel = close browser
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
        float panelWidth = std::min(900.0f, (float)app.width - 80.0f);
        float panelHeight = std::min(620.0f, (float)app.height - 80.0f);
        float panelX = (app.width - panelWidth) / 2;
        float panelY = (app.height - panelHeight) / 2;
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
            applyTheme(app, clickedTheme);
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
    } else if (app.selecting) {
        // Finalize selection based on mode
        if (app.selectionMode == App::SelectionMode::Word ||
            app.selectionMode == App::SelectionMode::Line) {
            // Word/Line selection: keep the bounds set during mouse down/move
            // hasSelection was already set to true in WM_LBUTTONDOWN
        } else {
            // Normal selection: finalize with current mouse position (document coordinates)
            float docX = app.mouseX + app.scrollX;
            float docY = app.mouseY + app.scrollY;
            app.selEndX = (int)docX;
            app.selEndY = (int)docY;

            // Check if this was a meaningful drag (not just a click)
            // Use screen coordinates stored from mouse down
            int dx = abs(app.mouseX - app.lastClickX);
            int dy = abs(app.mouseY - app.lastClickY);
            if (dx > 5 || dy > 5) {
                app.hasSelection = true;
            } else if (!app.hoveredLink.empty()) {
                // It was just a click on a link
                openUrl(app.hoveredLink);
                app.hasSelection = false;
            } else {
                app.hasSelection = false;
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (!app.hoveredLink.empty()) {
        // Click on link
        openUrl(app.hoveredLink);
    }

    app.mouseDown = false;
    app.selecting = false;
}

void handleKeyDown(App& app, HWND hwnd, WPARAM wParam) {
    float pageSize = app.height * 0.8f;
    float maxScroll = std::max(0.0f, app.contentHeight - app.height);
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

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
            case 'A': {
                // Select All - extract all text from document
                if (app.root) {
                    app.selectedText.clear();
                    extractText(app.root, app.selectedText);
                    app.hasSelection = true;
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
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            }
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
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                break;
        }
    } else {
        switch (wParam) {
            case VK_ESCAPE:
                // Priority: Search > FolderBrowser > TOC > Theme chooser > Quit
                if (app.showSearch) {
                    app.showSearch = false;
                    app.searchActive = false;
                    app.searchQuery.clear();
                    app.searchMatches.clear();
                    app.searchAnimation = 0;
                } else if (app.showFolderBrowser) {
                    app.showFolderBrowser = false;
                    app.folderBrowserAnimation = 0;
                } else if (app.showToc) {
                    app.showToc = false;
                    app.tocAnimation = 0;
                } else if (app.showThemeChooser) {
                    app.showThemeChooser = false;
                    app.themeChooserAnimation = 0;
                } else {
                    PostQuitMessage(0);
                }
                break;
            case 'Q':
                if (!app.showThemeChooser && !app.showSearch && !app.showFolderBrowser && !app.showToc) {
                    PostQuitMessage(0);
                }
                break;
            case 'B':
                // B to toggle folder browser
                if (!app.showSearch && !app.showThemeChooser && !app.showToc) {
                    app.showFolderBrowser = !app.showFolderBrowser;
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
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                break;
            case VK_UP:
            case 'K':
                if (!app.showSearch) {
                    app.targetScrollY -= 50;
                }
                break;
            case VK_DOWN:
            case 'J':
                if (!app.showSearch) {
                    app.targetScrollY += 50;
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
                    auto result = app.parser.parse(buffer.str());
                    if (result.success) {
                        app.root = result.root;
                        app.parseTimeUs = result.parseTimeUs;
                        app.currentFile = filepath;
                        app.scrollY = 0;
                        app.targetScrollY = 0;
                        app.contentHeight = 0;
                        app.docText.clear();
                        app.docTextLower.clear();
                        app.searchMatches.clear();
                        app.searchMatchYs.clear();
                        app.layoutDirty = true;
                        updateFileWriteTime(app);
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
    }
    DragFinish(hDrop);
}

void handleFileWatchTimer(App& app, HWND hwnd) {
    if (app.currentFile.empty() || !app.fileWatchEnabled) return;

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
                auto result = app.parser.parse(buffer.str());
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
