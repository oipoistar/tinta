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
#include <cmath>
#include <cstdio>
#include <functional>
#include "settings.h"
#include "document.h"
#include "d2d_init.h"
#include "utils.h"
#include "syntax.h"
#include "search.h"
#include "render.h"
#include "file_utils.h"
#include "overlays.h"
#include "input.h"
#include "editor.h"

static App* g_app = nullptr;

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void render(App& app);

void render(App& app) {
    if (!app.renderTarget) return;

    app.renderTarget->BeginDraw();
    app.drawCalls = 0;

    if (app.layoutDirty) {
        if (app.editMode && !app.editorShowPreview) {
            // Preview hidden: defer document layout until it's shown again
            // (the viewport is zero-width, so laying out now would be wasted
            // work against a nonsense max width)
        } else if (app.editMode) {
            // Edit mode needs complete scroll anchors for preview sync
            layoutDocument(app);
        } else {
            // Lay out the visible region first so this frame presents
            // immediately; the rest continues in WM_APP_LAYOUT_CHUNK slices
            layoutDocumentViewportFirst(app);
            if (!app.layoutComplete) {
                PostMessage(app.hwnd, WM_APP_LAYOUT_CHUNK, 0, 0);
            }
        }
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
        app.scrollY = std::max(0.0f, std::min(targetY, previewMaxScroll));
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

render_document:

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

    ID2D1StrokeStyle* dashedStrokeStyle = nullptr;
    if (std::any_of(
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
            properties, nullptr, 0, &dashedStrokeStyle);
    }

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
    if (dashedStrokeStyle) dashedStrokeStyle->Release();

    auto drawPolygon = [&](const D2D1_POINT_2F* points, size_t count,
                           const App::LayoutShape& shape) {
        if (count < 3) return;

        ID2D1PathGeometry* geometry = nullptr;
        if (FAILED(app.d2dFactory->CreatePathGeometry(&geometry)) || !geometry) return;

        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geometry->Open(&sink)) && sink) {
            sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(points + 1, static_cast<UINT32>(count - 1));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();

            if (shape.fill.a > 0.0f) {
                app.brush->SetColor(shape.fill);
                app.renderTarget->FillGeometry(geometry, app.brush);
                app.drawCalls++;
            }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                app.brush->SetColor(shape.stroke);
                app.renderTarget->DrawGeometry(
                    geometry, app.brush, shape.strokeWidth);
                app.drawCalls++;
            }
        }
        geometry->Release();
    };

    for (const auto& shape : app.layoutShapes) {
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

        if (shape.type == App::LayoutShapeType::Diamond) {
            float centerX = (rect.left + rect.right) * 0.5f;
            float centerY = (rect.top + rect.bottom) * 0.5f;
            D2D1_POINT_2F points[] = {
                D2D1::Point2F(centerX, rect.top),
                D2D1::Point2F(rect.right, centerY),
                D2D1::Point2F(centerX, rect.bottom),
                D2D1::Point2F(rect.left, centerY),
            };
            drawPolygon(points, 4, shape);
            continue;
        }

        if (shape.type == App::LayoutShapeType::Hexagon) {
            float inset = (rect.right - rect.left) * 0.18f;
            float centerY = (rect.top + rect.bottom) * 0.5f;
            D2D1_POINT_2F points[] = {
                D2D1::Point2F(rect.left + inset, rect.top),
                D2D1::Point2F(rect.right - inset, rect.top),
                D2D1::Point2F(rect.right, centerY),
                D2D1::Point2F(rect.right - inset, rect.bottom),
                D2D1::Point2F(rect.left + inset, rect.bottom),
                D2D1::Point2F(rect.left, centerY),
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
            float btnW = dpi(app, 52.0f);
            float btnH = dpi(app, 26.0f);
            float btnPad = 8.0f * app.contentScale * app.zoomFactor;
            float btnX = cb.bounds.right - btnW - btnPad - app.scrollX;
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

            // "Copy" label centered in button
            app.brush->SetColor(D2D1::ColorF(
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                app.theme.isDark ? 0.9f : 0.15f,
                1.0f));
            IDWriteTextLayout* btnLayout = nullptr;
            app.dwriteFactory->CreateTextLayout(L"Copy", 4, app.codeFormat,
                btnW, btnH, &btnLayout);
            if (btnLayout) {
                btnLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                btnLayout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                app.renderTarget->DrawTextLayout(
                    D2D1::Point2F(btnX, btnY), btnLayout, app.brush);
                btnLayout->Release();
            }
            app.drawCalls++;
        }
    }

    // Determine scrollbar visibility
    bool needsVScroll = app.contentHeight > app.height;
    bool needsHScroll = app.contentWidth > documentWidth;
    float scrollbarSize = dpi(app, 14.0f);

    // Scrollbar color: dark on light themes, light on dark themes
    float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;

    // Draw vertical scrollbar
    if (needsVScroll) {
        float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
        float trackHeight = app.height - (needsHScroll ? scrollbarSize : 0);
        float sbHeight = trackHeight / app.contentHeight * trackHeight;
        sbHeight = std::max(sbHeight, dpi(app, 30.0f));
        float sbY = (maxScrollY > 0) ? (app.scrollY / maxScrollY * (trackHeight - sbHeight)) : 0;

        float sbWidth = (app.scrollbarHovered || app.scrollbarDragging) ? dpi(app, 10.0f) : dpi(app, 6.0f);
        float sbAlpha = (app.scrollbarHovered || app.scrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(documentWidth - sbWidth - dpi(app, 4.0f), sbY,
                                          documentWidth - dpi(app, 4.0f), sbY + sbHeight), 3, 3),
            app.brush);
        app.drawCalls++;
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

            float copyWidth = dpi(app, 100.0f);
            float copyHeight = dpi(app, 26.0f);
            float pillX = (app.width - copyWidth) / 2;
            float pillY = dpi(app, 10.0f);

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
        } else {
            app.showCopiedNotification = false;
        }
    }

    // Draw stats
    if (app.showStats) {
        wchar_t stats[512];
        swprintf(stats, 512,
            L"Parse: %zu us | Layout: %zu us | Draw calls: %zu\n"
            L"Startup: %.1fms (Win: %.1f | D2D: %.1f | DWrite: %.1f | File: %.1f)",
            app.parseTimeUs,
            app.layoutTimeUs,
            app.drawCalls,
            app.metrics.totalStartupUs / 1000.0,
            app.metrics.windowInitUs / 1000.0,
            app.metrics.d2dInitUs / 1000.0,
            app.metrics.dwriteInitUs / 1000.0,
            app.metrics.fileLoadUs / 1000.0);

        float statsWidth = dpi(app, 600.0f);
        float statsHeight = dpi(app, 50.0f);

        app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.8f));
        app.renderTarget->FillRectangle(
            D2D1::RectF(app.width - statsWidth - dpi(app, 10.0f), app.height - statsHeight - dpi(app, 10.0f),
                       app.width - dpi(app, 10.0f), app.height - dpi(app, 10.0f)),
            app.brush);

        app.brush->SetColor(D2D1::ColorF(0.7f, 0.9f, 0.7f));
        app.renderTarget->DrawText(stats, (UINT32)wcslen(stats), app.codeFormat,
            D2D1::RectF(app.width - statsWidth - dpi(app, 5.0f), app.height - statsHeight - dpi(app, 5.0f),
                       app.width - dpi(app, 15.0f), app.height - dpi(app, 15.0f)),
            app.brush);
    }

    // Render overlays (search overlay handled separately for edit mode)
    if (app.showSearch && !app.editMode) renderSearchOverlay(app);
    if (app.showFolderBrowser) renderFolderBrowser(app);
    if (app.showToc) renderToc(app);
    if (app.showThemeChooser) renderThemeChooser(app);
    if (app.showHelp) renderHelpOverlay(app);

    // Close edit mode split view clipping
    if (app.editMode) {
        D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
        app.renderTarget->SetTransform(identity);
        app.renderTarget->PopAxisAlignedClip();

        // Render search overlay in screen coordinates (over editor pane)
        if (app.showSearch) renderSearchOverlay(app);

        // Render edit mode notification (on top of everything)
        renderEditModeNotification(app);
    }

    // "Saved!" notification (reuses "Copied!" infrastructure)

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
    app.editorShowPreview = savedSettings.editorShowPreview;
    app.editorWordWrap = savedSettings.editorWordWrap;

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
                       L"1. Search for '.md' or '.mmd'\n"
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

    auto loadDocumentContent = [&](const std::string& content, std::string_view path) {
        auto result = parseDocument(app.parser, content, path);
        if (result.success) {
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
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

    if (!inputFile.empty()) {
        if (loadFile(inputFile)) {
            app.currentFile = inputFile;
            app.focusMermaidOnNextLayout = isMermaidDocumentPath(inputFile);
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

    // Set window title with filename
    updateWindowTitle(app);

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
