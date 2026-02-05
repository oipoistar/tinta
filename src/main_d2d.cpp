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
#include "file_utils.h"
#include "overlays.h"
#include "input.h"

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

    // Render images (bitmaps)
    for (const auto& bmp : app.layoutBitmaps) {
        if (!bmp.bitmap) continue;
        if (bmp.destRect.bottom < viewportTop - cullMargin ||
            bmp.destRect.top > viewportBottom + cullMargin) continue;
        if (bmp.destRect.right < viewportLeft - cullMargin ||
            bmp.destRect.left > viewportRight + cullMargin) continue;
        app.renderTarget->DrawBitmap(bmp.bitmap,
            D2D1::RectF(bmp.destRect.left - app.scrollX,
                         bmp.destRect.top - app.scrollY,
                         bmp.destRect.right - app.scrollX,
                         bmp.destRect.bottom - app.scrollY));
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
            } else if (lineCenterY >= selStartY - 3 && lineCenterY <= selEndY + 3) {
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

    // "Copied!" notification with fade out (cached layout)
    if (app.showCopiedNotification) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - app.copiedNotificationStart).count();

        if (elapsed < 2.0f) {
            float alpha = 1.0f;
            if (elapsed > 0.5f) {
                alpha = 1.0f - (elapsed - 0.5f) / 1.5f;
            }
            app.copiedNotificationAlpha = alpha;

            float copyWidth = 100.0f;
            float copyHeight = 26;
            float pillX = (app.width - copyWidth) / 2;
            float pillY = 10;

            app.brush->SetColor(D2D1::ColorF(0.2f, 0.7f, 0.3f, 0.9f * alpha));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + copyWidth, pillY + copyHeight), 13, 13),
                app.brush);

            // Cache the "Copied!" text layout and metrics across frames
            static IDWriteTextLayout* cachedCopyLayout = nullptr;
            static float cachedTextOffsetX = 0, cachedTextOffsetY = 0;
            if (!cachedCopyLayout) {
                app.dwriteFactory->CreateTextLayout(L"Copied!", 7,
                    app.textFormat, copyWidth, copyHeight, &cachedCopyLayout);
                if (cachedCopyLayout) {
                    DWRITE_TEXT_METRICS m;
                    cachedCopyLayout->GetMetrics(&m);
                    cachedTextOffsetX = (copyWidth - m.width) / 2;
                    cachedTextOffsetY = (copyHeight - m.height) / 2;
                }
            }
            if (cachedCopyLayout) {
                app.brush->SetColor(D2D1::ColorF(1, 1, 1, alpha));
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(pillX + cachedTextOffsetX, pillY + cachedTextOffsetY),
                    cachedCopyLayout, app.brush);
            }
            app.drawCalls++;
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

    // Render overlays
    if (app.showSearch) renderSearchOverlay(app);
    if (app.showFolderBrowser) renderFolderBrowser(app);
    if (app.showToc) renderToc(app);
    if (app.showThemeChooser) renderThemeChooser(app);

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
            if (app) handleMouseWheel(*app, hwnd, wParam, lParam);
            return 0;

        case WM_MOUSEHWHEEL:
            if (app) handleMouseHWheel(*app, hwnd, wParam, lParam);
            return 0;

        case WM_MOUSEMOVE:
            if (app) handleMouseMove(*app, hwnd, lParam);
            return 0;

        case WM_LBUTTONDOWN:
            if (app) handleMouseDown(*app, hwnd, wParam, lParam);
            return 0;

        case WM_LBUTTONUP:
            if (app) handleMouseUp(*app, hwnd, wParam, lParam);
            return 0;

        case WM_SETCURSOR:
            if (app && LOWORD(lParam) == HTCLIENT) {
                // We handle cursor in WM_MOUSEMOVE
                return TRUE;
            }
            break;

        case WM_KEYDOWN:
            if (app) handleKeyDown(*app, hwnd, wParam);
            return 0;

        case WM_CHAR:
            if (app) handleCharInput(*app, hwnd, wParam);
            return 0;

        case WM_DROPFILES:
            if (app) handleDropFiles(*app, hwnd, wParam);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_FILE_WATCH && app) handleFileWatchTimer(*app, hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_FILE_WATCH);
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

    // Start file watch timer and record initial write time
    updateFileWriteTime(app);
    SetTimer(app.hwnd, TIMER_FILE_WATCH, 500, nullptr);

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
