// Direct2D + DirectWrite renderer for Windows
// Much faster startup than OpenGL

#include "app.h"

#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>

#include <d3d11.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include "settings.h"
#include "selection.h"
#include "document.h"
#include "d2d_init.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"
#include "render.h"
#include "file_utils.h"
#include "overlays.h"
#include "annotations.h"

#include <appmodel.h>
#include <winhttp.h>
#include <thread>
#pragma comment(lib, "winhttp.lib")
#include "input.h"
#include "editor.h"
#include "print.h"
#include "export.h"
#include "i18n.h"
#include "tabs.h"
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static App* g_app = nullptr;

// Initializes the GPU driver on a worker thread after the first (software)
// frame is on screen. D3D device creation costs ~200 ms dominated by
// process-global driver initialization — done here once, the hardware
// render target created on WM_APP_GPU_READY takes ~40 ms. The warm-up
// device is held only until that target exists (its own device keeps the
// driver initialized), then released — it pins tens of MB otherwise.
static ID3D11Device* g_warmupDevice = nullptr;
static void startGpuWarmup() {
    std::thread([] {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        D3D_FEATURE_LEVEL fl;
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                          D3D11_SDK_VERSION, &device, &fl, &ctx);
        if (ctx) ctx->Release();
        if (device && g_app && g_app->hwnd) {
            g_warmupDevice = device;
            PostMessageW(g_app->hwnd, WM_APP_GPU_READY, 0, 0);
        } else if (device) {
            device->Release();
        }
    }).detach();
}

// Windows light/dark preference (Settings > Personalization > Colors)
static bool systemPrefersLight() {
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return true;
    }
    return value != 0;
}

// The theme the auto mode wants right now
static int autoThemeIndex(const App& app) {
    return systemPrefersLight() ? app.lightThemeIndex : app.darkThemeIndex;
}

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void render(App& app);

void render(App& app) {
    if (!app.renderTarget) return;

    app.renderTarget->BeginDraw();
    app.drawCalls = 0;

    // Print preview replaces the whole frame: the document is in print
    // layout while it is open, so the normal paths would draw nonsense
    if (app.showPrintPreview) {
        renderPrintPreview(app);
        app.renderTarget->EndDraw();
        return;
    }

    // Any viewport width change (folder browser toggle, preview split)
    // re-flows the reading column for the space that remains
    {
        float vw = documentViewportWidth(app);
        if (vw > 0.0f && vw != app.lastViewportWidth) {
            app.lastViewportWidth = vw;
            app.layoutDirty = true;
        }
    }

    if (app.layoutDirty) {
        if (app.editMode && !app.editorShowPreview) {
            // Preview hidden: defer document layout until it's shown again
            // (the viewport is zero-width, so laying out now would be wasted
            // work against a nonsense max width)
        } else if (app.editMode) {
            // The preview streams in like the viewer: visible region first,
            // the rest in background chunks. The anchor sync clamps to
            // whatever is laid out and self-corrects as chunks land, so
            // entering edit mode no longer blocks on a full layout (#77)
            layoutDocumentViewportFirst(app);
            if (!app.layoutComplete) {
                PostMessage(app.hwnd, WM_APP_LAYOUT_CHUNK, 0, 0);
            }
        } else {
            // Lay out the visible region first so this frame presents
            // immediately; the rest continues in WM_APP_LAYOUT_CHUNK slices
            layoutDocumentViewportFirst(app);
            if (!app.layoutComplete) {
                PostMessage(app.hwnd, WM_APP_LAYOUT_CHUNK, 0, 0);
            }
        }
    }

    // Incremental layout grows contentHeight in small steps. Keep scrollbar
    // visibility latched for the duration of that layout and only let its
    // extent grow, so the right edge cannot blink as chunks arrive.
    if (app.layoutComplete) {
        app.scrollbarContentHeight = app.contentHeight;
        app.verticalScrollbarVisible = app.contentHeight > app.height;
    } else if (app.contentHeight > app.height) {
        app.verticalScrollbarVisible = true;
        app.scrollbarContentHeight = std::max(app.scrollbarContentHeight,
                                              app.contentHeight);
    }

    // Sync preview scroll to editor scroll position using source-offset anchors
    if (app.editMode && app.editorShowPreview &&
        !app.scrollAnchors.empty() && !app.editorLineByteOffsets.empty()) {
        // Find the editor's top visible line (row-aware in wrap mode)
        int topLine = (int)editorTopVisibleLine(app);
        topLine = std::max(0, std::min(topLine, (int)app.editorLineByteOffsets.size() - 1));
        size_t topByteOffset = app.editorLineByteOffsets[topLine];

        // Binary search for the anchor just before this byte offset
        size_t lo = 0, hi = app.scrollAnchors.size();
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (app.scrollAnchors[mid].sourceOffset <= topByteOffset) lo = mid;
            else hi = mid;
        }

        // Interpolate between anchor[lo] and anchor[lo+1]
        float targetY;
        if (lo + 1 < app.scrollAnchors.size() &&
            app.scrollAnchors[lo + 1].sourceOffset > app.scrollAnchors[lo].sourceOffset) {
            float t = (float)(topByteOffset - app.scrollAnchors[lo].sourceOffset) /
                      (float)(app.scrollAnchors[lo + 1].sourceOffset - app.scrollAnchors[lo].sourceOffset);
            t = std::max(0.0f, std::min(t, 1.0f));
            targetY = app.scrollAnchors[lo].renderedY +
                       t * (app.scrollAnchors[lo + 1].renderedY - app.scrollAnchors[lo].renderedY);
        } else {
            // Last anchor or single anchor — use ratio for remaining content
            targetY = app.scrollAnchors[lo].renderedY;
            if (app.contentHeight > app.scrollAnchors[lo].renderedY) {
                size_t lastOffset = app.scrollAnchors[lo].sourceOffset;
                size_t totalBytes = app.editorLineByteOffsets.back();
                if (totalBytes > lastOffset) {
                    float t = (float)(topByteOffset - lastOffset) / (float)(totalBytes - lastOffset);
                    t = std::max(0.0f, std::min(t, 1.0f));
                    targetY += t * (app.contentHeight - app.scrollAnchors[lo].renderedY);
                }
            }
        }

        float previewMaxScroll = std::max(0.0f, app.contentHeight - (float)app.height);
        float synced = std::max(0.0f, std::min(targetY, previewMaxScroll));
        float editorMax = std::max(0.0f, app.editorContentHeight - (float)app.height);
        if (app.editorScrollY >= editorMax - 1.0f) {
            // The editor has bottomed out, but rendered content is taller
            // than its source (images, diagrams): let the preview overshoot
            // to reach its tail (#85). Scrolling the editor up re-syncs.
            app.scrollY = std::min(std::max(app.scrollY, synced), previewMaxScroll);
        } else {
            app.scrollY = synced;
        }
        app.targetScrollY = app.scrollY;
    }

    // Edit mode: split view rendering
    if (app.editMode) {
        app.renderTarget->Clear(app.theme.background);

        float editorWidth = editorPaneWidth(app);
        float previewX = documentViewportX(app);
        float previewWidth = documentViewportWidth(app);

        // Render editor (left pane; full width when the preview is hidden)
        renderEditor(app, editorWidth);

        // Render separator
        if (app.editorShowPreview) renderSeparator(app);

        // Render preview (right pane) using clip + transform
        app.renderTarget->PushAxisAlignedClip(
            D2D1::RectF(previewX, 0, (float)app.width, (float)app.height),
            D2D1_ANTIALIAS_MODE_ALIASED);

        D2D1_MATRIX_3X2_F originalTransform;
        app.renderTarget->GetTransform(&originalTransform);
        app.renderTarget->SetTransform(
            D2D1::Matrix3x2F::Translation(previewX, 0) * originalTransform);

        // Clear preview background
        app.brush->SetColor(app.theme.background);
        app.renderTarget->FillRectangle(
            D2D1::RectF(0, 0, previewWidth, (float)app.height), app.brush);

        goto render_document;
    }

    // Clear background
    app.renderTarget->Clear(app.theme.background);
    app.drawCalls++;

    // Folder browser open: the document shifts right in sync with the
    // panel's slide so it stays fully readable beside it
    if (documentViewportX(app) > 0.0f) {
        app.renderTarget->SetTransform(
            D2D1::Matrix3x2F::Translation(documentViewportX(app), 0));
    }

render_document:

    // Apply a saved reading position once the layout can reach it (#77)
    if (app.pendingScrollRestore >= 0.0f &&
        (app.layoutComplete ||
         app.contentHeight >= app.pendingScrollRestore + app.height)) {
        app.scrollY = app.pendingScrollRestore;
        app.targetScrollY = app.pendingScrollRestore;
        app.pendingScrollRestore = -1.0f;
    }

    // Clamp scroll values
    float documentWidth = documentViewportWidth(app);
    float maxScrollX = std::max(0.0f, app.contentWidth - documentWidth);
    float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
    app.scrollX = std::max(0.0f, std::min(app.scrollX, maxScrollX));
    app.scrollY = std::max(0.0f, std::min(app.scrollY, maxScrollY));

    // Render cached layout (document coordinates -> screen)
    const float viewportTop = app.scrollY;
    const float viewportBottom = app.scrollY + app.height;
    const float viewportLeft = app.scrollX;
    const float viewportRight = app.scrollX + documentWidth;
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

    // Dashed stroke is a factory object: create once, reuse every frame
    if (!app.dashedStrokeStyle &&
        std::any_of(
            app.layoutConnectors.begin(), app.layoutConnectors.end(),
            [](const App::LayoutConnector& connector) { return connector.dashed; })) {
        D2D1_STROKE_STYLE_PROPERTIES properties = {
            D2D1_CAP_STYLE_FLAT,
            D2D1_CAP_STYLE_FLAT,
            D2D1_CAP_STYLE_FLAT,
            D2D1_LINE_JOIN_MITER,
            10.0f,
            D2D1_DASH_STYLE_DASH,
            0.0f,
        };
        app.d2dFactory->CreateStrokeStyle(
            properties, nullptr, 0, &app.dashedStrokeStyle);
    }
    ID2D1StrokeStyle* dashedStrokeStyle = app.dashedStrokeStyle;

    for (const auto& connector : app.layoutConnectors) {
        if (connector.bounds.bottom < viewportTop - cullMargin ||
            connector.bounds.top > viewportBottom + cullMargin ||
            connector.bounds.right < viewportLeft - cullMargin ||
            connector.bounds.left > viewportRight + cullMargin ||
            connector.points.size() < 2) {
            continue;
        }

        app.brush->SetColor(connector.color);
        for (size_t i = 1; i < connector.points.size(); i++) {
            const auto& from = connector.points[i - 1];
            const auto& to = connector.points[i];
            app.renderTarget->DrawLine(
                D2D1::Point2F(from.x - app.scrollX, from.y - app.scrollY),
                D2D1::Point2F(to.x - app.scrollX, to.y - app.scrollY),
                app.brush, connector.stroke,
                connector.dashed ? dashedStrokeStyle : nullptr);
            app.drawCalls++;
        }

        if (connector.directed) {
            const auto& tip = connector.points.back();
            const auto& previous = connector.points[connector.points.size() - 2];
            float dx = tip.x - previous.x;
            float dy = tip.y - previous.y;
            float length = std::sqrt(dx * dx + dy * dy);
            if (length > 0.001f) {
                dx /= length;
                dy /= length;
                float wing = connector.arrowSize * 0.5f;
                D2D1_POINT_2F left = D2D1::Point2F(
                    tip.x - dx * connector.arrowSize + dy * wing,
                    tip.y - dy * connector.arrowSize - dx * wing);
                D2D1_POINT_2F right = D2D1::Point2F(
                    tip.x - dx * connector.arrowSize - dy * wing,
                    tip.y - dy * connector.arrowSize + dx * wing);
                D2D1_POINT_2F screenTip =
                    D2D1::Point2F(tip.x - app.scrollX, tip.y - app.scrollY);
                app.renderTarget->DrawLine(
                    screenTip,
                    D2D1::Point2F(left.x - app.scrollX, left.y - app.scrollY),
                    app.brush, connector.stroke);
                app.renderTarget->DrawLine(
                    screenTip,
                    D2D1::Point2F(right.x - app.scrollX, right.y - app.scrollY),
                    app.brush, connector.stroke);
                app.drawCalls += 2;
            }
        }
    }
    // Draws a cached polygon geometry (built lazily in local space) at the
    // shape's screen position via a translation transform
    auto drawPolygon = [&](const D2D1_POINT_2F* localPoints, size_t count,
                           App::LayoutShape& shape) {
        if (count < 3) return;

        if (!shape.geometry) {
            ID2D1PathGeometry* geometry = nullptr;
            if (FAILED(app.d2dFactory->CreatePathGeometry(&geometry)) || !geometry) return;
            ID2D1GeometrySink* sink = nullptr;
            if (SUCCEEDED(geometry->Open(&sink)) && sink) {
                sink->BeginFigure(localPoints[0], D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLines(localPoints + 1, static_cast<UINT32>(count - 1));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                sink->Release();
                shape.geometry = geometry;
            } else {
                geometry->Release();
                return;
            }
        }

        D2D1_MATRIX_3X2_F previous;
        app.renderTarget->GetTransform(&previous);
        app.renderTarget->SetTransform(
            D2D1::Matrix3x2F::Translation(shape.rect.left - app.scrollX,
                                          shape.rect.top - app.scrollY) * previous);
        if (shape.fill.a > 0.0f) {
            app.brush->SetColor(shape.fill);
            app.renderTarget->FillGeometry(shape.geometry, app.brush);
            app.drawCalls++;
        }
        if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
            app.brush->SetColor(shape.stroke);
            app.renderTarget->DrawGeometry(
                shape.geometry, app.brush, shape.strokeWidth);
            app.drawCalls++;
        }
        app.renderTarget->SetTransform(previous);
    };

    for (auto& shape : app.layoutShapes) {
        if (shape.rect.bottom < viewportTop - cullMargin ||
            shape.rect.top > viewportBottom + cullMargin ||
            shape.rect.right < viewportLeft - cullMargin ||
            shape.rect.left > viewportRight + cullMargin) {
            continue;
        }

        D2D1_RECT_F rect = D2D1::RectF(
            shape.rect.left - app.scrollX,
            shape.rect.top - app.scrollY,
            shape.rect.right - app.scrollX,
            shape.rect.bottom - app.scrollY);

        if (shape.type == App::LayoutShapeType::Path) {
            // Geometry was built eagerly at layout time in local space
            if (!shape.geometry) continue;
            D2D1_MATRIX_3X2_F previous;
            app.renderTarget->GetTransform(&previous);
            app.renderTarget->SetTransform(
                D2D1::Matrix3x2F::Translation(shape.rect.left - app.scrollX,
                                              shape.rect.top - app.scrollY) *
                previous);
            if (shape.fill.a > 0.0f) {
                app.brush->SetColor(shape.fill);
                app.renderTarget->FillGeometry(shape.geometry, app.brush);
                app.drawCalls++;
            }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                app.brush->SetColor(shape.stroke);
                app.renderTarget->DrawGeometry(
                    shape.geometry, app.brush, shape.strokeWidth);
                app.drawCalls++;
            }
            app.renderTarget->SetTransform(previous);
            continue;
        }

        if (shape.type == App::LayoutShapeType::Diamond) {
            float w = shape.rect.right - shape.rect.left;
            float h = shape.rect.bottom - shape.rect.top;
            D2D1_POINT_2F points[] = {
                D2D1::Point2F(w * 0.5f, 0),
                D2D1::Point2F(w, h * 0.5f),
                D2D1::Point2F(w * 0.5f, h),
                D2D1::Point2F(0, h * 0.5f),
            };
            drawPolygon(points, 4, shape);
            continue;
        }

        if (shape.type == App::LayoutShapeType::Hexagon) {
            float w = shape.rect.right - shape.rect.left;
            float h = shape.rect.bottom - shape.rect.top;
            float inset = w * 0.18f;
            D2D1_POINT_2F points[] = {
                D2D1::Point2F(inset, 0),
                D2D1::Point2F(w - inset, 0),
                D2D1::Point2F(w, h * 0.5f),
                D2D1::Point2F(w - inset, h),
                D2D1::Point2F(inset, h),
                D2D1::Point2F(0, h * 0.5f),
            };
            drawPolygon(points, 6, shape);
            continue;
        }

        app.brush->SetColor(shape.fill);
        if (shape.type == App::LayoutShapeType::Ellipse) {
            D2D1_ELLIPSE ellipse = D2D1::Ellipse(
                D2D1::Point2F(
                    (rect.left + rect.right) * 0.5f,
                    (rect.top + rect.bottom) * 0.5f),
                (rect.right - rect.left) * 0.5f,
                (rect.bottom - rect.top) * 0.5f);
            if (shape.fill.a > 0.0f) {
                app.renderTarget->FillEllipse(ellipse, app.brush);
                app.drawCalls++;
            }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                app.brush->SetColor(shape.stroke);
                app.renderTarget->DrawEllipse(
                    ellipse, app.brush, shape.strokeWidth);
                app.drawCalls++;
            }
            continue;
        }

        if (shape.type == App::LayoutShapeType::RoundedRectangle ||
            shape.type == App::LayoutShapeType::Stadium) {
            float radius = shape.type == App::LayoutShapeType::Stadium
                ? (rect.bottom - rect.top) * 0.5f
                : shape.radius;
            D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(rect, radius, radius);
            if (shape.fill.a > 0.0f) {
                app.renderTarget->FillRoundedRectangle(rounded, app.brush);
                app.drawCalls++;
            }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                app.brush->SetColor(shape.stroke);
                app.renderTarget->DrawRoundedRectangle(
                    rounded, app.brush, shape.strokeWidth);
                app.drawCalls++;
            }
            continue;
        }

        if (shape.fill.a > 0.0f) {
            app.renderTarget->FillRectangle(rect, app.brush);
            app.drawCalls++;
        }
        if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
            app.brush->SetColor(shape.stroke);
            app.renderTarget->DrawRectangle(
                rect, app.brush, shape.strokeWidth);
            app.drawCalls++;
        }
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

    // Render code block copy button on hover
    if (app.hoveredCodeBlock >= 0 && app.hoveredCodeBlock < (int)app.codeBlocks.size()) {
        const auto& cb = app.codeBlocks[app.hoveredCodeBlock];
        if (cb.bounds.bottom >= viewportTop - cullMargin &&
            cb.bounds.top <= viewportBottom + cullMargin) {
            float btnW = dpi(app, 72.0f);
            float btnH = dpi(app, 26.0f);
            float btnPad = 8.0f * app.contentScale * app.zoomFactor;
            // Blocks wider than the viewport keep their buttons reachable
            float rightEdge = std::min(
                cb.bounds.right, app.scrollX + documentViewportWidth(app));
            float btnX = rightEdge - btnW - btnPad - app.scrollX;
            float btnY = cb.bounds.top + btnPad - app.scrollY;

            // Button background
            app.brush->SetColor(D2D1::ColorF(
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                0.9f));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(btnX, btnY, btnX + btnW, btnY + btnH), 4, 4),
                app.brush);

            // Localized copy label centered in the button
            app.brush->SetColor(D2D1::ColorF(
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                1.0f));
            IDWriteTextLayout* btnLayout = nullptr;
            const wchar_t* copyLabel = tr(app, "codeblock.copy");
            app.dwriteFactory->CreateTextLayout(copyLabel, (UINT32)wcslen(copyLabel), app.codeFormat,
                btnW, btnH, &btnLayout);
            if (btnLayout) {
                btnLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                btnLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(btnX, btnY), btnLayout, app.brush);
                btnLayout->Release();
            }

            // Diagrams add a copy-as-image button beside the source copy
            if (cb.isDiagram) {
                float pngW = dpi(app, 52.0f);
                float pngX = btnX - pngW - dpi(app, 6.0f);

                // Oversized diagrams add the fit-to-width toggle
                if (cb.fitCandidate) {
                    float fitW = dpi(app, 44.0f);
                    float fitX = pngX - fitW - dpi(app, 6.0f);
                    app.brush->SetColor(D2D1::ColorF(
                        app.theme.isDark ? 0.3f : 0.85f,
                        app.theme.isDark ? 0.3f : 0.85f,
                        app.theme.isDark ? 0.3f : 0.85f,
                        0.9f));
                    app.renderTarget->FillRoundedRectangle(
                        D2D1::RoundedRect(
                            D2D1::RectF(fitX, btnY, fitX + fitW,
                                        btnY + btnH), 4, 4),
                        app.brush);
                    app.brush->SetColor(D2D1::ColorF(
                        app.theme.isDark ? 0.9f : 0.15f,
                        app.theme.isDark ? 0.9f : 0.15f,
                        app.theme.isDark ? 0.9f : 0.15f,
                        1.0f));
                    const wchar_t* fitLabel =
                        cb.fitActive ? L"1:1" : L"Fit";
                    IDWriteTextLayout* fitLayout = nullptr;
                    app.dwriteFactory->CreateTextLayout(
                        fitLabel, (UINT32)wcslen(fitLabel), app.codeFormat,
                        fitW, btnH, &fitLayout);
                    if (fitLayout) {
                        fitLayout->SetTextAlignment(
                            DWRITE_TEXT_ALIGNMENT_CENTER);
                        fitLayout->SetParagraphAlignment(
                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                        app.renderTarget->DrawTextLayout(
                            D2D1::Point2F(fitX, btnY), fitLayout,
                            app.brush);
                        fitLayout->Release();
                    }
                }
                app.brush->SetColor(D2D1::ColorF(
                    app.theme.isDark ? 0.3f : 0.85f,
                    app.theme.isDark ? 0.3f : 0.85f,
                    app.theme.isDark ? 0.3f : 0.85f,
                    0.9f));
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(pngX, btnY, pngX + pngW, btnY + btnH), 4, 4),
                    app.brush);
                app.brush->SetColor(D2D1::ColorF(
                    app.theme.isDark ? 0.9f : 0.15f,
                    app.theme.isDark ? 0.9f : 0.15f,
                    app.theme.isDark ? 0.9f : 0.15f,
                    1.0f));
                IDWriteTextLayout* pngLayout = nullptr;
                const wchar_t* pngLabel = tr(app, "diagram.png");
                app.dwriteFactory->CreateTextLayout(
                    pngLabel, (UINT32)wcslen(pngLabel), app.codeFormat, pngW,
                    btnH, &pngLayout);
                if (pngLayout) {
                    pngLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    pngLayout->SetParagraphAlignment(
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    app.renderTarget->DrawTextLayout(
                        D2D1::Point2F(pngX, btnY), pngLayout, app.brush);
                    pngLayout->Release();
                }
            }
            app.drawCalls++;
        }
    }

    // Table copy-as-TSV button on hover (same pattern as code blocks;
    // wide tables clamp the button into the visible viewport)
    if (app.hoveredTable >= 0 && app.hoveredTable < (int)app.tableRects.size()) {
        const auto& tb = app.tableRects[app.hoveredTable];
        if (tb.bounds.bottom >= viewportTop - cullMargin &&
            tb.bounds.top <= viewportBottom + cullMargin) {
            float btnW = dpi(app, 88.0f);
            float btnH = dpi(app, 26.0f);
            float btnPad = 8.0f * app.contentScale * app.zoomFactor;
            float rightEdge = std::min(
                tb.bounds.right, app.scrollX + documentViewportWidth(app));
            float btnX = rightEdge - btnW - btnPad - app.scrollX;
            float btnY = tb.bounds.top + btnPad - app.scrollY;

            app.brush->SetColor(D2D1::ColorF(
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                app.theme.isDark ? 0.3f : 0.85f,
                0.9f));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(btnX, btnY, btnX + btnW, btnY + btnH), 4, 4),
                app.brush);

            app.brush->SetColor(D2D1::ColorF(
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                1.0f));
            IDWriteTextLayout* btnLayout = nullptr;
            const wchar_t* copyLabel = tr(app, "table.copy");
            app.dwriteFactory->CreateTextLayout(
                copyLabel, (UINT32)wcslen(copyLabel), app.codeFormat, btnW,
                btnH, &btnLayout);
            if (btnLayout) {
                btnLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                btnLayout->SetParagraphAlignment(
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(btnX, btnY), btnLayout, app.brush);
                btnLayout->Release();
            }

            // Oversized tables add the fit-to-width toggle
            if (tb.fitCandidate) {
                float fitW = dpi(app, 44.0f);
                float fitX = btnX - fitW - dpi(app, 6.0f);
                app.brush->SetColor(D2D1::ColorF(
                    app.theme.isDark ? 0.3f : 0.85f,
                    app.theme.isDark ? 0.3f : 0.85f,
                    app.theme.isDark ? 0.3f : 0.85f,
                    0.9f));
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(fitX, btnY, fitX + fitW, btnY + btnH),
                        4, 4),
                    app.brush);
                app.brush->SetColor(D2D1::ColorF(
                    app.theme.isDark ? 0.9f : 0.15f,
                    app.theme.isDark ? 0.9f : 0.15f,
                    app.theme.isDark ? 0.9f : 0.15f,
                    1.0f));
                const wchar_t* fitLabel = tb.fitActive ? L"1:1" : L"Fit";
                IDWriteTextLayout* fitLayout = nullptr;
                app.dwriteFactory->CreateTextLayout(
                    fitLabel, (UINT32)wcslen(fitLabel), app.codeFormat,
                    fitW, btnH, &fitLayout);
                if (fitLayout) {
                    fitLayout->SetTextAlignment(
                        DWRITE_TEXT_ALIGNMENT_CENTER);
                    fitLayout->SetParagraphAlignment(
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    app.renderTarget->DrawTextLayout(
                        D2D1::Point2F(fitX, btnY), fitLayout, app.brush);
                    fitLayout->Release();
                }
            }
            app.drawCalls++;
        }
    }

    // Determine scrollbar visibility
    bool needsVScroll = app.verticalScrollbarVisible;
    bool needsHScroll = app.contentWidth > documentWidth;
    float scrollbarSize = dpi(app, 14.0f);

    // Scrollbar color: dark on light themes, light on dark themes
    float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;

    // Draw vertical scrollbar
    if (needsVScroll) {
        float scrollExtent = std::max((float)app.height, app.scrollbarContentHeight);
        float maxScrollY = std::max(0.0f, scrollExtent - app.height);
        float trackTop = chromeTopHeight(app);
        float trackHeight =
            app.height - trackTop - (needsHScroll ? scrollbarSize : 0);
        float sbHeight = trackHeight / scrollExtent * trackHeight;
        sbHeight = std::max(sbHeight, dpi(app, 30.0f));
        float sbY = trackTop +
            ((maxScrollY > 0) ? (app.scrollY / maxScrollY * (trackHeight - sbHeight)) : 0);

        float sbWidth = (app.scrollbarHovered || app.scrollbarDragging) ? dpi(app, 10.0f) : dpi(app, 6.0f);
        float sbAlpha = (app.scrollbarHovered || app.scrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(documentWidth - sbWidth - dpi(app, 4.0f), sbY,
                                          documentWidth - dpi(app, 4.0f), sbY + sbHeight), 3, 3),
            app.brush);
        app.drawCalls++;

        // Find-match ticks along the track: the document's match silhouette
        // at a glance (overview-ruler pattern). Dense results thin out so a
        // one-letter query cannot flood the frame with fills.
        if (app.showSearch && !app.searchQuery.empty() &&
            !app.searchMatches.empty() && !app.editMode) {
            size_t step =
                std::max<size_t>(1, app.searchMatches.size() / 400);
            float tickLeft = documentWidth - dpi(app, 12.0f);
            float tickRight = documentWidth - dpi(app, 2.0f);
            auto tickY = [&](const App::SearchMatch& m, float& ty) {
                float docY;
                if (m.highlightRect.bottom > m.highlightRect.top) {
                    docY = m.highlightRect.top;
                } else if (!app.docText.empty()) {
                    docY = (float)m.startPos / (float)app.docText.size() *
                           scrollExtent;
                } else {
                    return false;
                }
                ty = trackTop + docY / scrollExtent * trackHeight;
                ty = std::min(ty, trackTop + trackHeight - dpi(app, 2.0f));
                return true;
            };
            app.brush->SetColor(D2D1::ColorF(1.0f, 0.82f, 0.0f, 0.55f));
            for (size_t i = 0; i < app.searchMatches.size(); i += step) {
                float ty;
                if (!tickY(app.searchMatches[i], ty)) continue;
                app.renderTarget->FillRectangle(
                    D2D1::RectF(tickLeft, ty, tickRight, ty + dpi(app, 2.0f)),
                    app.brush);
            }
            // The current match always gets its tick, stepping or not
            if (app.searchCurrentIndex >= 0 &&
                app.searchCurrentIndex < (int)app.searchMatches.size()) {
                float ty;
                if (tickY(app.searchMatches[app.searchCurrentIndex], ty)) {
                    app.brush->SetColor(D2D1::ColorF(1.0f, 0.6f, 0.0f, 0.95f));
                    app.renderTarget->FillRectangle(
                        D2D1::RectF(tickLeft, ty - dpi(app, 0.5f), tickRight,
                                    ty + dpi(app, 2.5f)),
                        app.brush);
                }
            }
            app.drawCalls++;
        }
    }

    // Draw horizontal scrollbar
    if (needsHScroll) {
        float maxScrollX = std::max(0.0f, app.contentWidth - documentWidth);
        float trackWidth = documentWidth - (needsVScroll ? scrollbarSize : 0);
        float sbWidth = trackWidth / app.contentWidth * trackWidth;
        sbWidth = std::max(sbWidth, dpi(app, 30.0f));
        float sbX = (maxScrollX > 0) ? (app.scrollX / maxScrollX * (trackWidth - sbWidth)) : 0;

        float sbHeight = (app.hScrollbarHovered || app.hScrollbarDragging) ? dpi(app, 10.0f) : dpi(app, 6.0f);
        float sbAlpha = (app.hScrollbarHovered || app.hScrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(sbX, app.height - sbHeight - dpi(app, 4.0f),
                                          sbX + sbWidth, app.height - dpi(app, 4.0f)), 3, 3),
            app.brush);
        app.drawCalls++;
    }

    // Draw selection highlights from the exact [selAnchor, selFocus) glyph
    // ranges. The copied text comes from the same range, so highlight and
    // clipboard always agree.
    if ((app.selecting || app.hasSelection) && !app.textRects.empty() &&
        app.selAnchor != app.selFocus) {
        size_t selA = std::min(app.selAnchor, app.selFocus);
        size_t selB = std::max(app.selAnchor, app.selFocus);

        std::vector<D2D1_RECT_F> bars;
        selectionHighlightRects(app, selA, selB, bars);

        app.brush->SetColor(D2D1::ColorF(0.2f, 0.4f, 0.9f, 0.35f));
        float viewTop = app.scrollY - 50.0f;
        float viewBottom = app.scrollY + app.height + 50.0f;
        for (const auto& bar : bars) {
            if (bar.bottom < viewTop || bar.top > viewBottom) continue;
            app.renderTarget->FillRectangle(
                D2D1::RectF(bar.left - app.scrollX, bar.top - app.scrollY,
                            bar.right - app.scrollX, bar.bottom - app.scrollY),
                app.brush);
            app.drawCalls++;
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
                    // Glyph-precise via the run layout (shared with text
                    // selection); falls back to interpolation for code
                    D2D1_RECT_F highlightRect;
                    if (selectionRangeRect(app, tr, overlapStart, overlapEnd,
                                           highlightRect)) {
                        // Extend slightly for better visibility
                        highlightRect.left -= 1;
                        highlightRect.right += 1;
                        visibleMatches.push_back({highlightRect, mi});
                    }
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

    // Back to screen coordinates: notifications, stats, and overlays
    // (including the folder browser panel itself) are not shifted
    if (!app.editMode && documentViewportX(app) > 0.0f) {
        app.renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    // Review annotations: text tint, marker rail, hover preview (#126)
    renderAnnotations(app);

    // Link peek popup (dwell over a local .md link)
    renderLinkPeek(app);

    // Localized copy notification with fade out.
    if (app.showCopiedNotification) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - app.copiedNotificationStart).count();

        if (elapsed < 2.0f) {
            float alpha = 1.0f;
            if (elapsed > 0.5f) {
                alpha = 1.0f - (elapsed - 0.5f) / 1.5f;
            }
            app.copiedNotificationAlpha = alpha;

            const wchar_t* copied = tr(app, app.copiedNotificationKey);
            float copyWidth = dpi(app, 100.0f);
            if (app.textFormat) {
                copyWidth = std::max(copyWidth,
                                     measureText(app, copied, app.textFormat) + dpi(app, 40.0f));
            }
            copyWidth = std::min(copyWidth, (float)app.width - dpi(app, 20.0f));
            float copyHeight = dpi(app, 26.0f);
            float pillX = (app.width - copyWidth) / 2;
            float pillY = chromeTopHeight(app) + dpi(app, 10.0f);

            D2D1_COLOR_F pillColor = app.theme.accent;
            pillColor.a = 0.92f * alpha;
            app.brush->SetColor(pillColor);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + copyWidth, pillY + copyHeight), 13, 13),
                app.brush);

            if (app.textFormat) {
                D2D1_COLOR_F textColor = app.theme.background;
                textColor.a = alpha;
                app.brush->SetColor(textColor);
                // A dedicated layout centers both axes; the shared textFormat
                // is leading/near-aligned and must not be mutated here
                IDWriteTextLayout* toastLayout = nullptr;
                app.dwriteFactory->CreateTextLayout(
                    copied, (UINT32)wcslen(copied), app.textFormat,
                    copyWidth, copyHeight, &toastLayout);
                if (toastLayout) {
                    toastLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    toastLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    app.renderTarget->DrawTextLayout(
                        D2D1::Point2F(pillX, pillY), toastLayout, app.brush);
                    toastLayout->Release();
                }
            }
            app.drawCalls++;
        } else {
            app.showCopiedNotification = false;
        }
    }

    // Draw stats
    // Update-available chip: small, silent, bottom right; the cross
    // dismisses this version for good, a click opens the release page
    app.updateChipRect = D2D1_RECT_F{};
    app.updateCloseRect = D2D1_RECT_F{};
    if (app.updateAvailable && !app.updateDismissed && !app.zenMode &&
        !app.showPrintPreview && !app.showLightbox && app.dwriteFactory &&
        (app.folderBrowserFormat || app.textFormat)) {
        wchar_t label[96];
        std::wstring wideVersion = toWide(app.updateVersion);
        swprintf_s(label, _countof(label), tr(app, "update.available"),
                   wideVersion.c_str());
        IDWriteTextLayout* layout = nullptr;
        app.dwriteFactory->CreateTextLayout(
            label, (UINT32)wcslen(label),
            app.folderBrowserFormat ? app.folderBrowserFormat
                                    : app.textFormat,
            600.0f, 40.0f, &layout);
        if (layout) {
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            DWRITE_TEXT_METRICS m{};
            layout->GetMetrics(&m);
            float pad = dpi(app, 12.0f);
            float closeW = dpi(app, 20.0f);
            float chipH = dpi(app, 32.0f);
            float chipW =
                pad + m.width + dpi(app, 8.0f) + closeW + pad * 0.5f;
            float cx = (float)app.width - chipW - dpi(app, 18.0f);
            float cy = (float)app.height - chipH - dpi(app, 18.0f) -
                       (app.showStats ? dpi(app, 55.0f) : 0.0f);
            D2D1_RECT_F chip = D2D1::RectF(cx, cy, cx + chipW, cy + chipH);

            D2D1_COLOR_F bg = app.theme.isDark ? hexColor(0x1E1E1E, 0.97f)
                                               : hexColor(0xF8F8F8, 0.97f);
            app.brush->SetColor(bg);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(chip, chipH * 0.5f, chipH * 0.5f),
                app.brush);
            D2D1_COLOR_F border = app.theme.accent;
            border.a = 0.6f;
            app.brush->SetColor(border);
            app.renderTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(chip, chipH * 0.5f, chipH * 0.5f),
                app.brush, 1.0f);

            app.brush->SetColor(app.theme.text);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(cx + pad, cy + (chipH - m.height) * 0.5f),
                layout, app.brush);
            layout->Release();

            float ccx = cx + pad + m.width + dpi(app, 8.0f) + closeW * 0.5f;
            float ccy = cy + chipH * 0.5f;
            float s = dpi(app, 4.0f);
            D2D1_COLOR_F cross = app.theme.text;
            cross.a = 0.6f;
            app.brush->SetColor(cross);
            app.renderTarget->DrawLine(D2D1::Point2F(ccx - s, ccy - s),
                                       D2D1::Point2F(ccx + s, ccy + s),
                                       app.brush, 1.4f);
            app.renderTarget->DrawLine(D2D1::Point2F(ccx - s, ccy + s),
                                       D2D1::Point2F(ccx + s, ccy - s),
                                       app.brush, 1.4f);

            app.updateChipRect = chip;
            app.updateCloseRect = D2D1::RectF(ccx - closeW * 0.5f, cy,
                                              ccx + closeW * 0.5f,
                                              cy + chipH);
        }
    }

    if (app.showStats) {
        wchar_t stats[768];
        wchar_t statsLine1[256];
        wchar_t statsLine2[512];
        swprintf_s(statsLine1, _countof(statsLine1), tr(app, "stats.line1"),
            app.parseTimeUs,
            app.layoutTimeUs,
            app.drawCalls);
        swprintf_s(statsLine2, _countof(statsLine2), tr(app, "stats.line2"),
            app.metrics.totalStartupUs / 1000.0,
            app.metrics.windowInitUs / 1000.0,
            app.metrics.d2dInitUs / 1000.0,
            app.metrics.dwriteInitUs / 1000.0,
            app.metrics.fileLoadUs / 1000.0);
        swprintf_s(stats, _countof(stats), L"Tinta v%d.%d.%d  |  %ls\n%ls",
                   TINTA_VERSION_MAJOR, TINTA_VERSION_MINOR,
                   TINTA_VERSION_PATCH, statsLine1, statsLine2);

        float statsWidth = dpi(app, 600.0f);
        float statsHeight = dpi(app, 50.0f);

        app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.8f));
        app.renderTarget->FillRectangle(
            D2D1::RectF(app.width - statsWidth - dpi(app, 10.0f), app.height - statsHeight - dpi(app, 10.0f),
                       app.width - dpi(app, 10.0f), app.height - dpi(app, 10.0f)),
            app.brush);

        app.brush->SetColor(D2D1::ColorF(0.7f, 0.9f, 0.7f));
        app.renderTarget->DrawText(stats, (UINT32)wcslen(stats),
            app.statsFormat ? app.statsFormat : app.codeFormat,
            D2D1::RectF(app.width - statsWidth - dpi(app, 5.0f), app.height - statsHeight - dpi(app, 5.0f),
                       app.width - dpi(app, 15.0f), app.height - dpi(app, 15.0f)),
            app.brush);
    }

    // Render overlays (search overlay handled separately for edit mode)
    if (app.showSearch && !app.editMode) {
        renderSearchOverlay(app);
        renderFolderSearchResults(app);
    }
    if (app.showFolderBrowser) renderFolderBrowser(app);
    if (app.showToc) renderToc(app);
    if (app.annotEditorOpen) renderAnnotationEditor(app);
    if (app.showContextMenu) renderContextMenu(app);
    if (app.showThemeChooser) renderThemeChooser(app);
    if (app.showHelp) renderHelpOverlay(app);
    if (app.showSettings) renderSettingsOverlay(app);
    if (app.showLightbox) renderLightbox(app);
    if (app.showThemeEditor) renderThemeEditor(app);
    if (app.showShortcutEditor) renderShortcutEditor(app);

    // Close edit mode split view clipping
    if (app.editMode) {
        D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
        app.renderTarget->SetTransform(identity);
        app.renderTarget->PopAxisAlignedClip();

        // Quick-note empty state: Open button in the blank preview pane
        renderQuickNoteEmptyState(app);

        // Render search overlay in screen coordinates (over editor pane)
        if (app.showSearch) renderSearchOverlay(app);

        // Render edit mode notification (on top of everything)
        renderEditModeNotification(app);
    }

    // Title-bar tab strip: the caption itself, above every panel
    renderTabStrip(app);
    renderTabSwitcher(app);
    renderTabMenu(app);

    // Unsaved-changes dialog above it all (also entered from tab closes)
    if (app.confirmExitPending) renderConfirmExitDialog(app);

    app.renderTarget->EndDraw();
}

// --- Update check ---
//
// Portable builds ask GitHub for the latest release tag at most once a
// day, on a worker thread that starts ten seconds after startup. Store
// installs update through the Store and skip all of this. The result
// surfaces as a small dismissable chip in the corner, never a dialog.

static bool isStorePackaged() {
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) !=
           APPMODEL_ERROR_NO_PACKAGE;
}

static std::string todayString() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    sprintf_s(buf, _countof(buf), "%04u-%02u-%02u", st.wYear, st.wMonth,
              st.wDay);
    return buf;
}

// "X.Y.Z" newer than the running build?
static bool isNewerVersion(const std::string& version) {
    int major = 0, minor = 0, patch = 0;
    if (sscanf_s(version.c_str(), "%d.%d.%d", &major, &minor, &patch) < 2) {
        return false;
    }
    if (major != TINTA_VERSION_MAJOR) return major > TINTA_VERSION_MAJOR;
    if (minor != TINTA_VERSION_MINOR) return minor > TINTA_VERSION_MINOR;
    return patch > TINTA_VERSION_PATCH;
}

static void updateCheckWorker(HWND hwnd) {
    std::string version;
    HINTERNET session = WinHttpOpen(
        L"tinta-update-check", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session) {
        HINTERNET connect = WinHttpConnect(session, L"api.github.com",
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connect) {
            HINTERNET request = WinHttpOpenRequest(
                connect, L"GET", L"/repos/oipoistar/tinta/releases/latest",
                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE);
            if (request) {
                if (WinHttpSendRequest(request,
                                       WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(request, nullptr)) {
                    std::string body;
                    DWORD avail = 0;
                    while (WinHttpQueryDataAvailable(request, &avail) &&
                           avail > 0 && body.size() < 262144) {
                        size_t at = body.size();
                        body.resize(at + avail);
                        DWORD got = 0;
                        if (!WinHttpReadData(request, &body[at], avail,
                                             &got) ||
                            got == 0) {
                            body.resize(at);
                            break;
                        }
                        body.resize(at + got);
                    }
                    size_t tag = body.find("\"tag_name\"");
                    if (tag != std::string::npos) {
                        size_t colon = body.find(':', tag);
                        size_t open = body.find('"', colon + 1);
                        size_t close = open == std::string::npos
                                           ? std::string::npos
                                           : body.find('"', open + 1);
                        if (close != std::string::npos && close > open + 1) {
                            version = body.substr(open + 1, close - open - 1);
                            if (!version.empty() && version[0] == 'v') {
                                version.erase(0, 1);
                            }
                        }
                    }
                }
                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(connect);
        }
        WinHttpCloseHandle(session);
    }
    auto* result = new std::string(std::move(version));
    if (!PostMessageW(hwnd, WM_APP_UPDATE_CHECK, 0, (LPARAM)result)) {
        delete result;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = g_app;

    switch (msg) {
        case WM_NCCALCSIZE: {
            // Reclaim the caption: the title bar becomes the tab strip.
            // DefWindowProc computes the resize borders, then the top edge
            // is restored so the client area reaches the top of the window.
            if (!wParam || !app || app->zenMode) break;
            NCCALCSIZE_PARAMS* params =
                reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            RECT original = params->rgrc[0];
            DefWindowProcW(hwnd, WM_NCCALCSIZE, wParam, lParam);
            params->rgrc[0].top = original.top;
            if (IsZoomed(hwnd)) {
                params->rgrc[0].top += GetSystemMetrics(SM_CYSIZEFRAME) +
                                       GetSystemMetrics(SM_CXPADDEDBORDER);
            }
            return 0;
        }

        case WM_NCHITTEST: {
            if (!app || app->zenMode) break;
            LRESULT def = DefWindowProcW(hwnd, WM_NCHITTEST, wParam, lParam);
            if (def != HTCLIENT) return def;  // resize borders
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            float x = (float)pt.x, y = (float)pt.y;
            if (y >= chromeTopHeight(*app)) return HTCLIENT;
            if (!IsZoomed(hwnd) &&
                y < (float)GetSystemMetrics(SM_CYSIZEFRAME)) {
                return HTTOP;
            }
            int button = captionHitTest(*app, x, y);
            if (button == 1) return HTMINBUTTON;
            if (button == 2) return HTMAXBUTTON;  // Win11 snap flyout
            if (button == 3) return HTCLOSE;
            for (const App::TabHit& hit : app->tabHits) {
                if (x >= hit.rect.left && x <= hit.rect.right &&
                    y >= hit.rect.top && y <= hit.rect.bottom) {
                    return HTCLIENT;  // tabs, +, chevron take normal clicks
                }
            }
            return HTCAPTION;  // empty strip drags the window
        }

        case WM_NCMOUSEMOVE: {
            if (!app || app->zenMode) break;
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            int hover = captionHitTest(*app, (float)pt.x, (float)pt.y);
            if (hover != app->captionButtonHover) {
                app->captionButtonHover = hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT track = {sizeof(track), TME_NONCLIENT | TME_LEAVE,
                                     hwnd, 0};
            TrackMouseEvent(&track);
            break;
        }

        case WM_NCMOUSELEAVE:
            if (app && (app->captionButtonHover || app->captionButtonPressed)) {
                app->captionButtonHover = 0;
                app->captionButtonPressed = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;

        case WM_NCLBUTTONDOWN:
            if (app && !app->zenMode &&
                (wParam == HTMINBUTTON || wParam == HTMAXBUTTON ||
                 wParam == HTCLOSE)) {
                app->captionButtonPressed =
                    wParam == HTMINBUTTON ? 1 : wParam == HTMAXBUTTON ? 2 : 3;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;  // consume: no legacy button painting
            }
            break;

        case WM_NCLBUTTONUP:
            if (app && app->captionButtonPressed) {
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                int released = captionHitTest(*app, (float)pt.x, (float)pt.y);
                int pressed = app->captionButtonPressed;
                app->captionButtonPressed = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                if (released == pressed) {
                    if (pressed == 1) {
                        PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                    } else if (pressed == 2) {
                        PostMessageW(hwnd, WM_SYSCOMMAND,
                                     IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE,
                                     0);
                    } else {
                        PostMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
                    }
                }
                return 0;
            }
            break;

        case WM_NCRBUTTONUP:
            if (app && !app->zenMode && wParam == HTCAPTION) {
                HMENU menu = GetSystemMenu(hwnd, FALSE);
                if (menu) {
                    int cmd = TrackPopupMenu(
                        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                        GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, hwnd,
                        nullptr);
                    if (cmd) PostMessageW(hwnd, WM_SYSCOMMAND, cmd, 0);
                }
                return 0;
            }
            break;

        case WM_MBUTTONDOWN:
            if (app && app->renderTarget) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                if ((float)my < chromeTopHeight(*app)) {
                    tabStripMouseDown(*app, hwnd, mx, my, true);
                    return 0;
                }
            }
            break;

        case WM_CLOSE:
            // Unsaved buffers (active or parked in tabs) get the dialog
            // before the window may close; tabs stay open so the session
            // save still remembers them
            if (app && app->confirmExitPending) {
                app->pendingWindowClose = true;  // dialog is already asking
                return 0;
            }
            if (app) {
                for (size_t i = 0; i < app->tabs.size(); i++) {
                    bool dirty = (int)i == app->activeTab
                                     ? (app->editMode && app->editorDirty)
                                     : (app->tabs[i].editMode &&
                                        app->tabs[i].editorDirty);
                    if (!dirty) continue;
                    if ((int)i != app->activeTab) {
                        tabActivate(*app, hwnd, (int)i);
                    }
                    app->pendingWindowClose = true;
                    app->confirmExitPending = true;
                    app->confirmExitOpenedAt = std::chrono::steady_clock::now();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            break;

        case WM_COPYDATA:
            if (app) {
                COPYDATASTRUCT* data =
                    reinterpret_cast<COPYDATASTRUCT*>(lParam);
                if (data && data->dwData == 1 && data->lpData &&
                    data->cbData > 0) {
                    std::string path(static_cast<const char*>(data->lpData),
                                     data->cbData - 1);
                    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    tabOpenPath(*app, hwnd, path, true);
                    return TRUE;
                }
            }
            break;

        case WM_SIZE:
            if (app && app->d2dFactory) {
                // The preview's geometry is stale after a resize: restore
                // the document at the new size and close the overlay
                if (app->showPrintPreview) {
                    app->printSaved.width = LOWORD(lParam);
                    app->printSaved.height = HIWORD(lParam);
                    closePrintPreview(*app, hwnd);
                }
                app->width = LOWORD(lParam);
                app->height = HIWORD(lParam);
                app->clearEditorLineLayoutCache();
                // Resize the target in place: device resources (image and
                // diagram bitmaps) stay valid, unlike a full recreation
                if (!app->renderTarget ||
                    FAILED(app->renderTarget->Resize(
                        D2D1::SizeU(app->width, app->height)))) {
                    createRenderTarget(*app);
                }
                app->layoutDirty = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_SETTINGCHANGE:
            // Windows switched light/dark mode
            if (app && app->followSystemTheme && lParam &&
                wcscmp((const wchar_t*)lParam, L"ImmersiveColorSet") == 0) {
                int want = autoThemeIndex(*app);
                if (want != app->currentThemeIndex) {
                    applyTheme(*app, want);
                }
            }
            return 0;

        case WM_DPICHANGED:
            if (app) {
                if (app->showPrintPreview) closePrintPreview(*app, hwnd);
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

        case WM_ENTERSIZEMOVE:
            if (app) {
                app->windowMoveTracking = true;
                GetWindowRect(hwnd, &app->windowMoveStartRect);
            }
            break;

        case WM_EXITSIZEMOVE:
            // A finished native window MOVE may be a drop onto another
            // Tinta window's strip; resizes land here too, so only a
            // changed position with an unchanged size qualifies
            if (app && app->windowMoveTracking) {
                app->windowMoveTracking = false;
                RECT now;
                const RECT& was = app->windowMoveStartRect;
                if (GetWindowRect(hwnd, &now) &&
                    (now.left != was.left || now.top != was.top) &&
                    now.right - now.left == was.right - was.left &&
                    now.bottom - now.top == was.bottom - was.top) {
                    tabWindowDropMerge(*app, hwnd);
                }
            }
            break;

        case WM_CAPTURECHANGED:
            // Losing capture mid tab-drag (Alt+Tab, a popup stealing the
            // mouse) aborts the drag instead of leaving a stray ghost
            if (app && app->tabDragIndex >= 0 && (HWND)lParam != hwnd) {
                tabDragCancel(*app, hwnd);
            }
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

        case WM_XBUTTONUP:
            // Mouse side buttons walk back/forward across link jumps
            if (app) {
                if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) {
                    navigateBack(*app, hwnd);
                } else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) {
                    navigateForward(*app, hwnd);
                }
            }
            return TRUE;

        case WM_SYSKEYDOWN:
            // Alt+Left / Alt+Right mirror the mouse side buttons
            if (app && (lParam & (1 << 29)) &&
                (wParam == VK_LEFT || wParam == VK_RIGHT)) {
                if (wParam == VK_LEFT) navigateBack(*app, hwnd);
                else navigateForward(*app, hwnd);
                return 0;
            }
            break;

        case WM_CHAR:
            if (app) handleCharInput(*app, hwnd, wParam);
            return 0;

        case WM_IME_STARTCOMPOSITION:
        case WM_IME_COMPOSITION:
            // Anchor the IME composition/candidate window at the caret in
            // edit mode, then let DefWindowProc run default IME handling
            if (app && app->editMode) editorPositionImeWindow(*app, hwnd);
            break;

        case WM_DROPFILES:
            if (app) handleDropFiles(*app, hwnd, wParam);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_LINK_PEEK && app) handleLinkPeekTimer(*app, hwnd);
            if (wParam == TIMER_FILE_WATCH && app) handleFileWatchTimer(*app, hwnd);
            if (wParam == 2 && app) editorReparse(*app); // TIMER_EDITOR_REPARSE
            if (wParam == TIMER_CURSOR_BLINK && app) {
                app->cursorBlinkOn = !app->cursorBlinkOn;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (wParam == TIMER_NOTIFICATION && app) {
                // Only fading notifications need repaints; the persistent
                // exit-confirm prompt is static until answered
                bool fading = app->showCopiedNotification ||
                    (app->showEditModeNotification && !app->confirmExitPending);
                if (fading) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    KillTimer(hwnd, TIMER_NOTIFICATION);
                }
            }
            if (wParam == TIMER_FOLDER_SEARCH && app) {
                KillTimer(hwnd, TIMER_FOLDER_SEARCH);
                startFolderSearchScan(*app);
            }
            if (wParam == TIMER_UPDATE_CHECK && app) {
                KillTimer(hwnd, TIMER_UPDATE_CHECK);
                std::thread(updateCheckWorker, hwnd).detach();
            }
            if (wParam == TIMER_SELECT_SCROLL && app) {
                handleSelectScrollTimer(*app, hwnd);
            }
            if (wParam == TIMER_IMAGE_REFLOW && app) {
                // One relayout for however many images arrived since armed
                KillTimer(hwnd, TIMER_IMAGE_REFLOW);
                app->layoutDirty = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (wParam == TIMER_ZOOM_APPLY && app) {
                if (app->zoomFactor != app->appliedZoomFactor) {
                    // More zoom ticks arrived since the last apply
                    updateTextFormats(*app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    KillTimer(hwnd, TIMER_ZOOM_APPLY);
                    app->zoomApplyPending = false;
                }
            }
            return 0;

        case WM_APP_IMAGE_READY:
            // A background image download finished: swap it into the cache
            // and reflow (handler takes ownership of the result)
            if (app) completeAsyncImage(*app, (void*)lParam);
            return 0;

        case WM_CONTEXTMENU:
            if (app) handleContextMenu(*app, hwnd, lParam);
            return 0;

        case WM_APP_FOLDER_SEARCH:
            // Folder search worker finished (handler takes ownership)
            if (app) completeFolderSearch(*app, (void*)lParam);
            return 0;

        case WM_APP_UPDATE_CHECK: {
            // Update worker finished (handler takes ownership). A failed
            // check does not stamp the day, so it retries next launch.
            auto* version = (std::string*)lParam;
            if (app && version && !version->empty()) {
                app->updateLastCheck = todayString();
                if (isNewerVersion(*version) &&
                    *version != app->updateDismissedVersion) {
                    app->updateVersion = *version;
                    app->updateAvailable = true;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            delete version;
            return 0;
        }

        case WM_APP_GPU_READY:
            // Driver is warm: swap the startup software render target for a
            // hardware one. The recreated target's own device keeps the
            // driver initialized, so the warm-up device can go.
            if (app && !app->useHardwareRT) {
                app->useHardwareRT = true;
                createRenderTarget(*app);
                app->layoutDirty = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (g_warmupDevice) {
                g_warmupDevice->Release();
                g_warmupDevice = nullptr;
            }
            return 0;

        case WM_APP_LAYOUT_CHUNK:
            // Continue an incomplete document layout in ~10ms slices, yielding
            // to input between slices
            if (app && !app->layoutDirty && !app->layoutComplete) {
                bool done = layoutDocumentContinue(*app, 10000);
                InvalidateRect(hwnd, nullptr, FALSE);  // scrollbar grows as layout fills in
                if (!done) PostMessage(hwnd, WM_APP_LAYOUT_CHUNK, 0, 0);
            }
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_FILE_WATCH);
            KillTimer(hwnd, 2); // TIMER_EDITOR_REPARSE
            KillTimer(hwnd, TIMER_CURSOR_BLINK);
            KillTimer(hwnd, TIMER_NOTIFICATION);
            KillTimer(hwnd, TIMER_ZOOM_APPLY);
            {
                // Load existing settings to preserve values like hasAskedFileAssociation
                Settings settings = loadSettings();
                settings.themeIndex = app->currentThemeIndex;
                settings.zoomFactor = app->zoomFactor;
                settings.editorShowPreview = app->editorShowPreview;
                settings.editorWordWrap = app->editorWordWrap;
                settings.followSystemTheme = app->followSystemTheme;
                settings.lightThemeIndex = app->lightThemeIndex;
                settings.darkThemeIndex = app->darkThemeIndex;
                settings.checkUpdates = app->updateCheckEnabled;
                settings.lastUpdateCheck = app->updateLastCheck;
                settings.dismissedUpdate = app->updateDismissedVersion;
                settings.folderSearchEnabled = app->folderSearchEnabled;
                settings.browserFocusPath = app->browserFocusPath;
                settings.openInTabs = app->openInTabs;
                // Remember the open tab set for the next launch (untitled
                // quick-note buffers have no path to restore). Satellite
                // windows never own the session: they re-read the owner's
                // stored session so their full-file save cannot clobber a
                // newer one with launch-time data.
                if (app->sessionOwner) {
                    settings.sessionTabs.clear();
                    settings.sessionActive = 0;
                    for (size_t i = 0; i < app->tabs.size(); i++) {
                        const std::string& path =
                            (int)i == app->activeTab ? app->currentFile
                                                     : app->tabs[i].path;
                        if (path.empty()) continue;
                        if ((int)i == app->activeTab) {
                            settings.sessionActive =
                                (int)settings.sessionTabs.size();
                        }
                        settings.sessionTabs.push_back(path);
                    }
                } else {
                    Settings onDisk = loadSettings();
                    settings.sessionTabs = onDisk.sessionTabs;
                    settings.sessionActive = onDisk.sessionActive;
                }
                settings.readingWidthPct = app->readingWidthPct;
                settings.zenWidthPct = app->zenWidthPct;
                settings.tocOnLeft = app->tocOnLeft;
                settings.language = app->languageSetting >= 0
                    ? languageIdAt(app->languageSetting) : "auto";
                settings.keyProfile = app->keyProfile;
                settings.keyOverrides = app->keyOverrides;
                if (!app->currentFile.empty()) {
                    rememberReadingPosition(settings, app->currentFile,
                                            app->scrollY, app->zoomFactor);
                }

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

**Tinta** is a fast, lightweight Markdown and Mermaid viewer for Windows.

## Getting Started

- **Drag & drop** a `.md` or `.mmd` file onto this window
- Press **B** to browse and open files from a folder
- Or run `tinta.exe readme.md` from the command line
- Press **?** for all available keyboard shortcuts

## Features

- 10 beautiful themes — press **T** to choose
- Native Mermaid flowchart rendering for `.mmd` files
- Edit mode with live preview — press **:**
- Search — press **F**
- Table of contents — press **Tab**
- Text selection and copy
- Syntax highlighting in code blocks for C/C++, C#, Python, JavaScript, Rust, Go, and Bash

## Code Example

```cpp
int main() {
    printf("Hello, World!\n");
    return 0;
}
```

## Keyboard Shortcuts

Press **?** at any time to see all shortcuts.

### Navigation

- **J / K** - Scroll down / up
- **Space / PgDn** - Page down
- **PgUp** - Page up
- **Home / End** - Jump to start / end
- **Ctrl+Scroll** - Zoom in / out

### View

- **F** or **Ctrl+F** - Search
- **Enter** - Next search match
- **B** - Toggle folder browser
- **Tab** - Toggle table of contents
- **T** - Theme chooser
- **S** - Toggle stats

### Editing

- **:** - Enter edit mode
- **Ctrl+S** - Save (in edit mode)
- **ESC ESC** - Exit edit mode

### General

- **Ctrl+A** - Select all
- **Ctrl+C** - Copy selection
- **ESC** - Close overlay / Quit
- **Q** - Quit
)";

// Last-chance handler: writes %LOCALAPPDATA%\Tinta\crash.dmp so crash
// reports from the field carry a usable stack
static LONG WINAPI writeCrashDump(EXCEPTION_POINTERS* exceptionInfo) {
    wchar_t path[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return EXCEPTION_CONTINUE_SEARCH;
    std::wstring dir = std::wstring(path) + L"\\Tinta";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring file = dir + L"\\crash.dmp";
    HANDLE dump = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info;
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = exceptionInfo;
        info.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                          MiniDumpWithIndirectlyReferencedMemory, &info,
                          nullptr, nullptr);
        CloseHandle(dump);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    SetUnhandledExceptionFilter(writeCrashDump);
    // Enable per-monitor DPI V2 awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto startupStart = Clock::now();

    App app;
    auto t0 = startupStart;
    g_app = &app;

    // Load user themes before settings: saved theme indices may point at them
    loadCustomThemes();
    loadLanguageOverrides();

    // Load saved settings
    Settings savedSettings = loadSettings();
    app.followSystemTheme = savedSettings.followSystemTheme;

    // Update check state (the timer arms after the window exists)
    app.updateCheckEnabled = savedSettings.checkUpdates;
    app.updateLastCheck = savedSettings.lastUpdateCheck;
    app.updateDismissedVersion = savedSettings.dismissedUpdate;
    app.lightThemeIndex = savedSettings.lightThemeIndex;
    app.darkThemeIndex = savedSettings.darkThemeIndex;
    app.folderSearchEnabled = savedSettings.folderSearchEnabled;
    app.browserFocusPath = savedSettings.browserFocusPath;
    app.openInTabs = savedSettings.openInTabs;
    int startTheme = app.followSystemTheme ? autoThemeIndex(app)
                                           : savedSettings.themeIndex;
    app.currentThemeIndex = startTheme;
    if (startTheme >= themeCount()) startTheme = 0;
    app.currentThemeIndex = startTheme;
    app.theme = themeAt(startTheme);
    app.darkMode = app.theme.isDark;
    app.zoomFactor = savedSettings.zoomFactor;
    app.editorShowPreview = savedSettings.editorShowPreview;
    app.editorWordWrap = savedSettings.editorWordWrap;
    app.readingWidthPct = savedSettings.readingWidthPct;
    app.zenWidthPct = savedSettings.zenWidthPct;
    app.tocOnLeft = savedSettings.tocOnLeft;
    app.languageSetting = savedSettings.language == "auto"
        ? -1 : languageIndexById(savedSettings.language);
    app.currentLanguageIndex = app.languageSetting >= 0
        ? app.languageSetting : detectSystemLanguage();
    app.keyOverrides = savedSettings.keyOverrides;
    applyKeymap(app, savedSettings);

    // Parse command line
    std::string inputFile;
    bool lightMode = false;
    bool forceRegister = false;
    bool cascadeWindow = false;   // offset from the saved position (new-file windows)
    bool startInEditMode = false; // open straight into the editor
    bool quickNote = false;       // Ctrl+N: untitled buffer, no backing file
    std::wstring printPagesDir;   // debug: render print pages as PNGs and exit
    std::wstring exportHtmlPath;  // debug: write the HTML export and exit
    std::wstring exportDocxPath;  // debug: write the DOCX export and exit
    std::wstring exportPdfPath;   // debug: write the PDF export and exit
    int posX = 0, posY = 0;       // --pos: spawn position (tab drag-out)
    bool hasPos = false;

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
        } else if (arg == L"--cascade") {
            cascadeWindow = true;
        } else if (arg == L"--tabbed") {
            // Drag-out satellites keep the tab row even with one tab
            app.forceTabStrip = true;
        } else if (arg == L"--edit") {
            startInEditMode = true;
        } else if (arg == L"--new") {
            quickNote = true;
        } else if (arg == L"--printpages" && i + 1 < argc) {
            printPagesDir = argv[++i];
        } else if (arg == L"--exporthtml" && i + 1 < argc) {
            exportHtmlPath = argv[++i];
        } else if (arg == L"--exportdocx" && i + 1 < argc) {
            exportDocxPath = argv[++i];
        } else if (arg == L"--exportpdf" && i + 1 < argc) {
            exportPdfPath = argv[++i];
        } else if (arg == L"--pos" && i + 2 < argc) {
            // Drag-out spawn: place the new window at the drop point
            posX = _wtoi(argv[++i]);
            posY = _wtoi(argv[++i]);
            hasPos = true;
        } else if (arg[0] != L'-' && arg[0] != L'/') {
            // Convert to UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, nullptr, 0, nullptr, nullptr);
            inputFile.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, &inputFile[0], len, nullptr, nullptr);
        }
    }
    LocalFree(argv);

    // Single instance: a plain file launch joins the existing window as a
    // new tab (Win11 Notepad model). --new/--cascade windows and the
    // openInTabs=false setting keep the one-window-per-document behavior.
    if (!inputFile.empty() && !quickNote && !cascadeWindow &&
        printPagesDir.empty() && exportHtmlPath.empty() &&
        exportDocxPath.empty() && exportPdfPath.empty() &&
        savedSettings.openInTabs) {
        HWND existing = FindWindowW(L"Tinta", nullptr);
        if (existing) {
            // Resolve to an absolute path: the receiving window has its own
            // working directory
            std::wstring wide = toWide(inputFile);
            wchar_t full[MAX_PATH];
            if (GetFullPathNameW(wide.c_str(), MAX_PATH, full, nullptr)) {
                int len = WideCharToMultiByte(CP_UTF8, 0, full, -1, nullptr, 0,
                                              nullptr, nullptr);
                std::string absolute(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, full, -1, &absolute[0], len,
                                    nullptr, nullptr);
                COPYDATASTRUCT data;
                data.dwData = 1;
                data.cbData = (DWORD)absolute.size() + 1;
                data.lpData = (void*)absolute.c_str();
                SendMessageW(existing, WM_COPYDATA, 0, (LPARAM)&data);
                if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
                SetForegroundWindow(existing);
                return 0;
            }
        }
    }

    // Handle /register command
    if (forceRegister) {
        if (registerFileAssociation()) {
            MessageBoxW(nullptr,
                       tr(app, "fileassoc.done_body"),
                       tr(app, "fileassoc.done_title"),
                       MB_OK | MB_ICONINFORMATION);
            openDefaultAppsSettings();
        } else {
            MessageBoxW(nullptr, tr(app, "fileassoc.register_failed_body"),
                       tr(app, "error.title"), MB_OK | MB_ICONWARNING);
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

    // Validate the saved window position against the monitors that exist
    // NOW — a position saved on a since-disconnected screen (docked laptop,
    // unplugged external monitor) would otherwise restore off-screen (#25)
    int windowX = savedSettings.windowX;
    int windowY = savedSettings.windowY;
    if (windowX != CW_USEDEFAULT && windowY != CW_USEDEFAULT) {
        RECT saved = { windowX, windowY,
                       windowX + savedSettings.windowWidth,
                       windowY + savedSettings.windowHeight };
        HMONITOR monitor = MonitorFromRect(&saved, MONITOR_DEFAULTTONULL);
        if (!monitor) {
            // Fully off every live monitor: clamp into the nearest one
            monitor = MonitorFromRect(&saved, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (monitor && GetMonitorInfoW(monitor, &mi)) {
                const RECT& wa = mi.rcWork;
                int width = std::min<int>(savedSettings.windowWidth, wa.right - wa.left);
                int height = std::min<int>(savedSettings.windowHeight, wa.bottom - wa.top);
                windowX = std::max<int>(wa.left, std::min<int>(windowX, wa.right - width));
                windowY = std::max<int>(wa.top, std::min<int>(windowY, wa.bottom - height));
            } else {
                windowX = CW_USEDEFAULT;
                windowY = CW_USEDEFAULT;
            }
        }
    }

    // A window spawned for a newly created document steps down-right from
    // the saved position so it does not cover the window that spawned it
    if (hasPos) {
        // Tab drag-out: appear under the drop point
        windowX = posX;
        windowY = posY;
    } else if (cascadeWindow && windowX != CW_USEDEFAULT && windowY != CW_USEDEFAULT) {
        windowX += 40;
        windowY += 40;
    }

    // Only the primary window persists and restores the tab session;
    // deliberate offshoots (new-note windows, drag-outs) are satellites
    app.sessionOwner = !cascadeWindow && !quickNote;

    app.hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"Tinta",
        L"Tinta",
        WS_OVERLAPPEDWINDOW,
        windowX, windowY,
        savedSettings.windowWidth, savedSettings.windowHeight,
        nullptr, nullptr, hInstance, nullptr
    );

    app.metrics.windowInitUs = usElapsed(t0);

    // Get DPI using per-monitor aware API
    app.contentScale = GetDpiForWindow(app.hwnd) / 96.0f;

    // Theme the native title bar before the window is ever shown
    applyWindowChrome(app);

    // Apply the custom-caption frame (WM_NCCALCSIZE) to the fresh window
    SetWindowPos(app.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);

    // Initialize D2D
    if (!initD2D(app)) {
        MessageBoxW(nullptr, tr(app, "error.d2d_init_failed"),
                   tr(app, "error.title"), MB_OK);
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
        MessageBoxW(nullptr, tr(app, "error.render_target_failed"),
                   tr(app, "error.title"), MB_OK);
        return 1;
    }
    app.metrics.renderTargetUs = usElapsed(t0);

    // Load document
    t0 = Clock::now();

    auto loadDocumentContent = [&](const std::string& content, std::string_view path) {
        auto result = parseDocument(app.parser, content, path);
        if (result.success) {
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
            // Review annotations derive from the raw source (#126)
            app.sourceText = content;
            annotationsParseSource(app);
        }
        return result.success;
    };

    auto loadFile = [&](const std::string& path) -> bool {
        // Use wide string path for ifstream to support non-ASCII paths (MSVC extension)
        std::wstring widePath = toWide(path);
        std::ifstream file(widePath);
        if (!file) return false;
        std::stringstream buffer;
        buffer << file.rdbuf();
        return loadDocumentContent(buffer.str(), path);
    };

    // Last session's tabs come back on plain launches (files that vanished
    // from disk in the meantime are dropped)
    bool restoreSession = !quickNote && !cascadeWindow && !startInEditMode &&
                          printPagesDir.empty() &&
                          !savedSettings.sessionTabs.empty();
    std::vector<std::string> sessionPaths;
    int sessionActive = 0;
    if (restoreSession) {
        for (size_t i = 0; i < savedSettings.sessionTabs.size(); i++) {
            const auto& path = savedSettings.sessionTabs[i];
            if (GetFileAttributesW(toWide(path).c_str()) !=
                INVALID_FILE_ATTRIBUTES) {
                // The active slot follows its path through the filtering
                if ((int)i == savedSettings.sessionActive) {
                    sessionActive = (int)sessionPaths.size();
                }
                sessionPaths.push_back(path);
            }
        }
        if (sessionPaths.empty()) restoreSession = false;
    }

    if (quickNote) {
        // Untitled quick note: an empty document, no backing file
        loadDocumentContent(std::string(), {});
    } else if (!inputFile.empty()) {
        if (loadFile(inputFile)) {
            app.currentFile = inputFile;
            app.focusMermaidOnNextLayout = isMermaidDocumentPath(inputFile);
        } else {
            loadDocumentContent(sampleMarkdown, {});
        }
    } else if (restoreSession) {
        // No file argument: reopen where the last session left off
        if (loadFile(sessionPaths[sessionActive])) {
            app.currentFile = sessionPaths[sessionActive];
            app.focusMermaidOnNextLayout =
                isMermaidDocumentPath(sessionPaths[sessionActive]);
        } else {
            loadDocumentContent(sampleMarkdown, {});
        }
    } else {
        // Try syntax.md
        if (loadFile("syntax.md")) {
            app.currentFile = "syntax.md";
        } else {
            loadDocumentContent(sampleMarkdown, {});
        }
    }

    app.metrics.fileLoadUs = usElapsed(t0);

    // Resume the saved reading position (#77); new-file windows start in the
    // editor at the top instead
    if (!startInEditMode && !app.currentFile.empty()) {
        app.pendingScrollRestore = findReadingPosition(savedSettings, app.currentFile);
        // Per-document zoom overrides the global one for known documents
        float docZoom = findReadingZoom(savedSettings, app.currentFile);
        if (docZoom > 0.0f && fabsf(docZoom - app.zoomFactor) > 0.01f) {
            app.zoomFactor = docZoom;
            app.appliedZoomFactor = docZoom;
            updateTextFormats(app);
            app.layoutDirty = true;
        }
    }

    // Set window title with filename
    updateWindowTitle(app);

    // The startup document becomes the first (tabless) tab; a restored
    // session rebuilds the whole row in its saved order
    if (restoreSession && !app.currentFile.empty()) {
        tabsSeedSession(app, sessionPaths);
    } else {
        tabsInit(app);
    }

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

    // Update check: portable builds only, at most once a day, and only
    // long after startup has finished (10 s timer arms a worker thread)
    if (app.updateCheckEnabled && !isStorePackaged() &&
        app.updateLastCheck != todayString()) {
        SetTimer(app.hwnd, TIMER_UPDATE_CHECK, 10000, nullptr);
    }

    app.metrics.totalStartupUs = usElapsed(startupStart);

    // New-file windows open straight into the editor
    if (quickNote) {
        enterQuickNoteMode(app);
    } else if (startInEditMode) {
        enterEditMode(app);
    }

    // Debug: rasterize the print pagination to PNGs and exit
    if (!printPagesDir.empty()) {
        int pages = printDebugPages(app, printPagesDir);
        wchar_t msg[64];
        swprintf_s(msg, L"printpages: %d", pages);
        OutputDebugStringW(msg);
        return pages > 0 ? 0 : 1;
    }

    // Debug: write the HTML export and exit (#export_as harness)
    if (!exportHtmlPath.empty()) {
        bool ok = exportHtmlFile(app, exportHtmlPath);
        return ok ? 0 : 1;
    }
    if (!exportDocxPath.empty()) {
        bool ok = exportDocxFile(app, exportDocxPath);
        return ok ? 0 : 1;
    }
    if (!exportPdfPath.empty()) {
        bool ok = exportPdfFile(app, exportPdfPath);
        return ok ? 0 : 1;
    }

    // First frame is on screen — now pay for the GPU in the background
    startGpuWarmup();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_app = nullptr;
    return (int)msg.wParam;
}
