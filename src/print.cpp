#include "print.h"
#include "render.h"
#include "d2d_init.h"
#include "editor.h"
#include "utils.h"

#include <commdlg.h>
#include <documenttarget.h>
#include <wincodec.h>
#include <winspool.h>

#include <cstring>

#include <cmath>
#include <string>
#include <vector>

// --- Printing (#74) ---
//
// No PDF library: the document is re-laid-out at page width in a light
// print palette and each page is replayed into a D2D command list handed
// to ID2D1PrintControl. The Windows print pipeline does the rest — pick
// "Microsoft Print to PDF" and the OS produces the PDF, with font
// embedding (including CJK) handled by the driver.

namespace {

constexpr float kPageMarginDips = 72.0f;  // 0.75 in

// Paper-derived palette with a true white page
D2DTheme printTheme() {
    D2DTheme t = THEMES[0];  // Paper (light)
    t.background = D2D1::ColorF(1.0f, 1.0f, 1.0f);
    t.codeBackground = hexColor(0xF4F4F4);
    return t;
}

using SavedView = App::PrintSavedView;

// Re-layouts the document at page-content width, 96 DPI, zoom 1, in the
// print palette. Restores everything in leavePrintLayout.
void enterPrintLayout(App& app, float contentWidthDips, SavedView& saved) {
    saved.width = app.width;
    saved.height = app.height;
    saved.contentScale = app.contentScale;
    saved.zoomFactor = app.zoomFactor;
    saved.appliedZoomFactor = app.appliedZoomFactor;
    saved.scrollX = app.scrollX;
    saved.scrollY = app.scrollY;
    saved.targetScrollX = app.targetScrollX;
    saved.targetScrollY = app.targetScrollY;
    saved.theme = app.theme;

    app.width = (int)contentWidthDips;
    app.height = 4000;  // layout does not clip vertically; any tall value
    app.contentScale = 1.0f;
    app.zoomFactor = 1.0f;
    app.theme = printTheme();
    updateTextFormats(app);
    app.layoutDirty = true;
    ensureLayoutComplete(app);
}

void leavePrintLayout(App& app, const SavedView& saved) {
    app.width = saved.width;
    app.height = saved.height;
    app.contentScale = saved.contentScale;
    app.zoomFactor = saved.zoomFactor;
    app.theme = saved.theme;
    updateTextFormats(app);
    app.appliedZoomFactor = saved.appliedZoomFactor;
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    app.scrollX = saved.scrollX;
    app.scrollY = saved.scrollY;
    app.targetScrollX = saved.targetScrollX;
    app.targetScrollY = saved.targetScrollY;
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

// Page break positions in document Y. Breaks avoid slicing text lines,
// images, and diagram shapes by pulling the cut above any straddling atom;
// an atom taller than most of a page is sliced rather than looping.
std::vector<float> computePageBreaks(const App& app, float pageContentH) {
    std::vector<float> breaks;
    float docEnd = app.contentHeight;
    float y = 0.0f;

    auto pullAbove = [&](float proposed, float pageTop) {
        for (int pass = 0; pass < 4; pass++) {
            float adjusted = proposed;
            auto consider = [&](float top, float bottom) {
                if (top < adjusted && bottom > adjusted && top > pageTop) {
                    adjusted = top;
                }
            };
            for (const auto& line : app.lineBuckets) consider(line.top, line.bottom);
            for (const auto& bmp : app.layoutBitmaps) consider(bmp.destRect.top, bmp.destRect.bottom);
            for (const auto& shape : app.layoutShapes) consider(shape.rect.top, shape.rect.bottom);
            if (adjusted == proposed) break;
            proposed = adjusted;
        }
        // Monster atom (taller than 70% of a page): slice it instead
        if (proposed - pageTop < pageContentH * 0.3f) {
            return pageTop + pageContentH;
        }
        return proposed;
    };

    while (y + pageContentH < docEnd) {
        float next = pullAbove(y + pageContentH, y);
        breaks.push_back(next);
        y = next;
    }
    return breaks;  // implicit final page runs to docEnd
}

// Replays the cached layout for the document Y range [yStart, yEnd) into
// the given device context, translated so yStart lands at offsetY. A
// print-path sibling of render()'s document pass — selection, search, and
// hover chrome intentionally excluded.
void drawDocumentRange(App& app, ID2D1DeviceContext* dc,
                       ID2D1SolidColorBrush* brush,
                       float yStart, float yEnd,
                       float offsetX, float offsetY) {
    auto inRange = [&](float top, float bottom) {
        return bottom >= yStart && top < yEnd;
    };
    float dx = offsetX;
    float dy = offsetY - yStart;

    for (const auto& rect : app.layoutRects) {
        if (!inRange(rect.rect.top, rect.rect.bottom)) continue;
        brush->SetColor(rect.color);
        dc->FillRectangle(
            D2D1::RectF(rect.rect.left + dx, rect.rect.top + dy,
                        rect.rect.right + dx, rect.rect.bottom + dy),
            brush);
    }

    for (const auto& line : app.layoutLines) {
        float top = std::min(line.p1.y, line.p2.y);
        float bottom = std::max(line.p1.y, line.p2.y);
        if (!inRange(top, bottom)) continue;
        brush->SetColor(line.color);
        dc->DrawLine(D2D1::Point2F(line.p1.x + dx, line.p1.y + dy),
                     D2D1::Point2F(line.p2.x + dx, line.p2.y + dy),
                     brush, line.stroke);
    }

    // Mermaid connectors (with arrowheads and dashing)
    for (const auto& connector : app.layoutConnectors) {
        if (!inRange(connector.bounds.top, connector.bounds.bottom)) continue;
        if (connector.points.size() < 2) continue;
        brush->SetColor(connector.color);
        for (size_t i = 1; i < connector.points.size(); i++) {
            const auto& from = connector.points[i - 1];
            const auto& to = connector.points[i];
            dc->DrawLine(D2D1::Point2F(from.x + dx, from.y + dy),
                         D2D1::Point2F(to.x + dx, to.y + dy),
                         brush, connector.stroke,
                         connector.dashed ? app.dashedStrokeStyle : nullptr);
        }
        if (connector.directed) {
            const auto& tip = connector.points.back();
            const auto& prev = connector.points[connector.points.size() - 2];
            float vx = tip.x - prev.x, vy = tip.y - prev.y;
            float len = std::sqrt(vx * vx + vy * vy);
            if (len > 0.001f) {
                vx /= len; vy /= len;
                float wing = connector.arrowSize * 0.5f;
                D2D1_POINT_2F left = D2D1::Point2F(
                    tip.x - vx * connector.arrowSize + vy * wing,
                    tip.y - vy * connector.arrowSize - vx * wing);
                D2D1_POINT_2F right = D2D1::Point2F(
                    tip.x - vx * connector.arrowSize - vy * wing,
                    tip.y - vy * connector.arrowSize + vx * wing);
                D2D1_POINT_2F screenTip = D2D1::Point2F(tip.x + dx, tip.y + dy);
                dc->DrawLine(screenTip, D2D1::Point2F(left.x + dx, left.y + dy),
                             brush, connector.stroke);
                dc->DrawLine(screenTip, D2D1::Point2F(right.x + dx, right.y + dy),
                             brush, connector.stroke);
            }
        }
    }

    // Mermaid node shapes
    auto fillStrokePolygon = [&](const D2D1_POINT_2F* pts, size_t count,
                                 const App::LayoutShape& shape) {
        ID2D1PathGeometry* geometry = nullptr;
        if (FAILED(app.d2dFactory->CreatePathGeometry(&geometry)) || !geometry) return;
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geometry->Open(&sink)) && sink) {
            sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLines(pts + 1, (UINT32)(count - 1));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            if (shape.fill.a > 0.0f) {
                brush->SetColor(shape.fill);
                dc->FillGeometry(geometry, brush);
            }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                brush->SetColor(shape.stroke);
                dc->DrawGeometry(geometry, brush, shape.strokeWidth);
            }
        }
        geometry->Release();
    };

    for (const auto& shape : app.layoutShapes) {
        if (!inRange(shape.rect.top, shape.rect.bottom)) continue;
        D2D1_RECT_F r = D2D1::RectF(shape.rect.left + dx, shape.rect.top + dy,
                                    shape.rect.right + dx, shape.rect.bottom + dy);
        if (shape.type == App::LayoutShapeType::Diamond) {
            float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
            D2D1_POINT_2F pts[] = {{cx, r.top}, {r.right, cy}, {cx, r.bottom}, {r.left, cy}};
            fillStrokePolygon(pts, 4, shape);
            continue;
        }
        if (shape.type == App::LayoutShapeType::Hexagon) {
            float inset = (r.right - r.left) * 0.18f;
            float cy = (r.top + r.bottom) * 0.5f;
            D2D1_POINT_2F pts[] = {{r.left + inset, r.top}, {r.right - inset, r.top},
                                   {r.right, cy}, {r.right - inset, r.bottom},
                                   {r.left + inset, r.bottom}, {r.left, cy}};
            fillStrokePolygon(pts, 6, shape);
            continue;
        }
        if (shape.type == App::LayoutShapeType::Ellipse) {
            D2D1_ELLIPSE e = D2D1::Ellipse(
                D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f),
                (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f);
            if (shape.fill.a > 0.0f) { brush->SetColor(shape.fill); dc->FillEllipse(e, brush); }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                brush->SetColor(shape.stroke);
                dc->DrawEllipse(e, brush, shape.strokeWidth);
            }
            continue;
        }
        if (shape.type == App::LayoutShapeType::RoundedRectangle ||
            shape.type == App::LayoutShapeType::Stadium) {
            float radius = shape.type == App::LayoutShapeType::Stadium
                ? (r.bottom - r.top) * 0.5f : shape.radius;
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, radius, radius);
            if (shape.fill.a > 0.0f) { brush->SetColor(shape.fill); dc->FillRoundedRectangle(rr, brush); }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                brush->SetColor(shape.stroke);
                dc->DrawRoundedRectangle(rr, brush, shape.strokeWidth);
            }
            continue;
        }
        if (shape.fill.a > 0.0f) { brush->SetColor(shape.fill); dc->FillRectangle(r, brush); }
        if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
            brush->SetColor(shape.stroke);
            dc->DrawRectangle(r, brush, shape.strokeWidth);
        }
    }

    // Images
    for (const auto& bmp : app.layoutBitmaps) {
        if (!bmp.bitmap) continue;
        if (!inRange(bmp.destRect.top, bmp.destRect.bottom)) continue;
        dc->DrawBitmap(bmp.bitmap,
            D2D1::RectF(bmp.destRect.left + dx, bmp.destRect.top + dy,
                        bmp.destRect.right + dx, bmp.destRect.bottom + dy),
            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    // Text
    for (const auto& run : app.layoutTextRuns) {
        if (!run.layout) continue;
        if (!inRange(run.bounds.top, run.bounds.bottom)) continue;
        brush->SetColor(run.color);
        dc->DrawTextLayout(D2D1::Point2F(run.pos.x + dx, run.pos.y + dy),
                           run.layout, brush);
    }
}

struct PageGeometry {
    float pageW, pageH;          // full page in DIPs
    float contentW, contentH;    // inside the margins
};

// Paper size of the default printer, for previewing before the dialog runs.
// printDocument re-reads the size from whatever printer is finally picked.
PageGeometry defaultPageGeometry() {
    PageGeometry geo{794.0f, 1123.0f, 0.0f, 0.0f};  // A4 fallback
    wchar_t printer[256];
    DWORD len = 256;
    if (GetDefaultPrinterW(printer, &len)) {
        HDC dc = CreateDCW(L"WINSPOOL", printer, nullptr, nullptr);
        if (dc) {
            float dpiX = (float)GetDeviceCaps(dc, LOGPIXELSX);
            float dpiY = (float)GetDeviceCaps(dc, LOGPIXELSY);
            float w = GetDeviceCaps(dc, PHYSICALWIDTH) * 96.0f / dpiX;
            float h = GetDeviceCaps(dc, PHYSICALHEIGHT) * 96.0f / dpiY;
            if (w >= 200 && h >= 200) {
                geo.pageW = w;
                geo.pageH = h;
            }
            DeleteDC(dc);
        }
    }
    geo.contentW = geo.pageW - kPageMarginDips * 2;
    geo.contentH = geo.pageH - kPageMarginDips * 2;
    return geo;
}

// Rasterizes one page of the active print layout into the preview pixel
// buffer at display resolution (page DIPs scaled by printPreviewFit), so
// the preview stays crisp instead of downscaling a 96-DPI rendering.
bool rasterizePreviewPage(App& app, int page) {
    int pageCount = (int)app.printPreviewBounds.size() - 1;
    if (page < 0 || page >= pageCount || !app.deviceContext) return false;
    UINT pxW = app.printPreviewPxW, pxH = app.printPreviewPxH;
    if (pxW == 0 || pxH == 0) return false;

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    if (!device) return false;
    ID2D1DeviceContext* dc = nullptr;
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
    device->Release();
    if (!dc) return false;

    D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    D2D1_BITMAP_PROPERTIES1 stagingProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ID2D1Bitmap1* target = nullptr;
    ID2D1Bitmap1* staging = nullptr;
    dc->CreateBitmap(D2D1::SizeU(pxW, pxH), nullptr, 0, &targetProps, &target);
    dc->CreateBitmap(D2D1::SizeU(pxW, pxH), nullptr, 0, &stagingProps, &staging);

    bool ok = false;
    if (target && staging) {
        dc->SetTarget(target);
        dc->BeginDraw();
        dc->SetTransform(D2D1::Matrix3x2F::Scale(app.printPreviewFit, app.printPreviewFit));
        dc->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        ID2D1SolidColorBrush* brush = nullptr;
        dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush);
        if (brush) {
            drawDocumentRange(app, dc, brush,
                              app.printPreviewBounds[page],
                              app.printPreviewBounds[page + 1],
                              kPageMarginDips, kPageMarginDips);
            brush->Release();
        }
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        if (SUCCEEDED(dc->EndDraw())) {
            D2D1_POINT_2U zero{0, 0};
            D2D1_RECT_U all{0, 0, pxW, pxH};
            D2D1_MAPPED_RECT mapped{};
            if (SUCCEEDED(staging->CopyFromBitmap(&zero, target, &all)) &&
                SUCCEEDED(staging->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
                app.printPreviewPixels.resize((size_t)pxW * pxH * 4);
                for (UINT row = 0; row < pxH; row++) {
                    memcpy(app.printPreviewPixels.data() + (size_t)row * pxW * 4,
                           mapped.bits + (size_t)row * mapped.pitch,
                           (size_t)pxW * 4);
                }
                staging->Unmap();
                ok = true;
            }
        }
    }
    if (staging) staging->Release();
    if (target) target->Release();
    dc->Release();
    return ok;
}

// Runs layout + pagination + per-page drawing. emitPage receives a closed
// command list per page. Returns page count, or -1 on failure.
template <typename EmitFn>
int renderPages(App& app, const PageGeometry& geo, EmitFn emitPage) {
    if (!app.deviceContext || !app.root) return -1;

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    if (!device) return -1;

    SavedView saved{};
    enterPrintLayout(app, geo.contentW, saved);

    std::vector<float> breaks = computePageBreaks(app, geo.contentH);
    breaks.push_back(app.contentHeight + 1.0f);  // final page sentinel

    ID2D1DeviceContext* pageDc = nullptr;
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pageDc);
    int pages = 0;
    bool ok = pageDc != nullptr;

    float yStart = 0.0f;
    for (size_t i = 0; ok && i < breaks.size(); i++) {
        float yEnd = breaks[i];
        ID2D1CommandList* commandList = nullptr;
        if (FAILED(pageDc->CreateCommandList(&commandList)) || !commandList) {
            ok = false;
            break;
        }
        pageDc->SetTarget(commandList);
        pageDc->BeginDraw();
        pageDc->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        ID2D1SolidColorBrush* brush = nullptr;
        pageDc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush);
        if (brush) {
            drawDocumentRange(app, pageDc, brush, yStart, yEnd,
                              kPageMarginDips, kPageMarginDips);
            brush->Release();
        }
        ok = SUCCEEDED(pageDc->EndDraw()) && SUCCEEDED(commandList->Close());
        if (ok) {
            ok = emitPage(commandList, pages);
            pages++;
        }
        commandList->Release();
        pageDc->SetTarget(nullptr);
        yStart = yEnd;
    }

    if (pageDc) pageDc->Release();
    device->Release();
    leavePrintLayout(app, saved);
    return ok ? pages : -1;
}

} // namespace

bool printDocument(App& app) {
    // Standard printer picker; PD_RETURNDC so the paper size can be read
    PRINTDLGW pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = app.hwnd;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    if (!PrintDlgW(&pd) || !pd.hDC) {
        return false;  // cancelled
    }

    float dpiX = (float)GetDeviceCaps(pd.hDC, LOGPIXELSX);
    float dpiY = (float)GetDeviceCaps(pd.hDC, LOGPIXELSY);
    PageGeometry geo{};
    geo.pageW = GetDeviceCaps(pd.hDC, PHYSICALWIDTH) * 96.0f / dpiX;
    geo.pageH = GetDeviceCaps(pd.hDC, PHYSICALHEIGHT) * 96.0f / dpiY;
    if (geo.pageW < 200 || geo.pageH < 200) {  // driver returned nonsense
        geo.pageW = 794.0f;   // A4
        geo.pageH = 1123.0f;
    }
    geo.contentW = geo.pageW - kPageMarginDips * 2;
    geo.contentH = geo.pageH - kPageMarginDips * 2;

    // Printer name from the DEVNAMES block
    std::wstring printerName;
    if (pd.hDevNames) {
        auto* names = (DEVNAMES*)GlobalLock(pd.hDevNames);
        if (names) {
            printerName = (wchar_t*)names + names->wDeviceOffset;
            GlobalUnlock(pd.hDevNames);
        }
    }
    DeleteDC(pd.hDC);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    if (printerName.empty()) return false;

    IPrintDocumentPackageTargetFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(PrintDocumentPackageTargetFactory), nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        return false;
    }
    std::wstring jobName = L"Tinta - " + toWide(app.currentFile);
    IPrintDocumentPackageTarget* target = nullptr;
    HRESULT hr = factory->CreateDocumentPackageTargetForPrintJob(
        printerName.c_str(), jobName.c_str(), nullptr, nullptr, &target);
    factory->Release();
    if (FAILED(hr) || !target) return false;

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    ID2D1PrintControl* printControl = nullptr;
    if (device) {
        device->CreatePrintControl(app.wicFactory, target, nullptr, &printControl);
        device->Release();
    }

    bool ok = false;
    if (printControl) {
        int pages = renderPages(app, geo, [&](ID2D1CommandList* list, int) {
            return SUCCEEDED(printControl->AddPage(
                list, D2D1::SizeF(geo.pageW, geo.pageH), nullptr));
        });
        ok = pages > 0 && SUCCEEDED(printControl->Close());
        printControl->Release();
    }
    target->Release();
    return ok;
}

void openPrintPreview(App& app, HWND hwnd) {
    if (app.showPrintPreview || !app.root || !app.deviceContext) return;
    if (app.editMode) {
        editorReparse(app);  // include unsaved edits in the preview and printout
    }

    PageGeometry geo = defaultPageGeometry();
    app.printPreviewPageW = geo.pageW;
    app.printPreviewPageH = geo.pageH;

    enterPrintLayout(app, geo.contentW, app.printSaved);

    std::vector<float> breaks = computePageBreaks(app, geo.contentH);
    app.printPreviewBounds.clear();
    app.printPreviewBounds.push_back(0.0f);
    for (float b : breaks) app.printPreviewBounds.push_back(b);
    app.printPreviewBounds.push_back(app.contentHeight + 1.0f);

    // Fit the page between the header and the button bar
    float ui = app.printSaved.contentScale;
    float availW = app.printSaved.width - 80.0f * ui;
    float availH = app.printSaved.height - 130.0f * ui;
    app.printPreviewFit = std::max(0.05f,
        std::min(availW / geo.pageW, availH / geo.pageH));
    app.printPreviewPxW = (unsigned)(geo.pageW * app.printPreviewFit);
    app.printPreviewPxH = (unsigned)(geo.pageH * app.printPreviewFit);

    app.printPreviewPage = 0;
    rasterizePreviewPage(app, 0);
    app.showPrintPreview = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void closePrintPreview(App& app, HWND hwnd) {
    if (!app.showPrintPreview) return;
    app.showPrintPreview = false;
    leavePrintLayout(app, app.printSaved);
    app.printPreviewPixels.clear();
    app.printPreviewPixels.shrink_to_fit();
    app.printPreviewBounds.clear();
    if (app.editMode) {
        // Editor line layouts were built against the print-layout formats
        app.clearEditorLineLayoutCache();
        app.editorRowMetricsWidth = -1.0f;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void printPreviewSetPage(App& app, int page) {
    if (!app.showPrintPreview) return;
    int pageCount = (int)app.printPreviewBounds.size() - 1;
    page = std::max(0, std::min(page, pageCount - 1));
    if (page == app.printPreviewPage) return;
    app.printPreviewPage = page;
    rasterizePreviewPage(app, page);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void printPreviewConfirm(App& app, HWND hwnd) {
    if (!app.showPrintPreview) return;
    closePrintPreview(app, hwnd);
    printDocument(app);
}

int printDebugPages(App& app, const std::wstring& outDir) {
    PageGeometry geo{794.0f, 1123.0f, 794.0f - kPageMarginDips * 2,
                     1123.0f - kPageMarginDips * 2};  // A4

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    if (!device) return -1;
    ID2D1DeviceContext* rasterDc = nullptr;
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &rasterDc);
    device->Release();
    if (!rasterDc) return -1;

    UINT pw = (UINT)geo.pageW, ph = (UINT)geo.pageH;
    D2D1_BITMAP_PROPERTIES1 targetProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    D2D1_BITMAP_PROPERTIES1 stagingProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ID2D1Bitmap1* pageBitmap = nullptr;
    ID2D1Bitmap1* staging = nullptr;
    rasterDc->CreateBitmap(D2D1::SizeU(pw, ph), nullptr, 0, &targetProps, &pageBitmap);
    rasterDc->CreateBitmap(D2D1::SizeU(pw, ph), nullptr, 0, &stagingProps, &staging);
    if (!pageBitmap || !staging) {
        if (pageBitmap) pageBitmap->Release();
        if (staging) staging->Release();
        rasterDc->Release();
        return -1;
    }

    CreateDirectoryW(outDir.c_str(), nullptr);

    int pages = renderPages(app, geo, [&](ID2D1CommandList* list, int index) {
        rasterDc->SetTarget(pageBitmap);
        rasterDc->BeginDraw();
        rasterDc->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        rasterDc->DrawImage(list);
        if (FAILED(rasterDc->EndDraw())) return false;

        D2D1_POINT_2U zero{0, 0};
        D2D1_RECT_U all{0, 0, pw, ph};
        if (FAILED(staging->CopyFromBitmap(&zero, pageBitmap, &all))) return false;
        D2D1_MAPPED_RECT mapped{};
        if (FAILED(staging->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return false;

        bool written = false;
        IWICBitmapEncoder* encoder = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        std::wstring path = outDir + L"\\page-" + std::to_wstring(index + 1) + L".png";
        if (app.wicFactory &&
            SUCCEEDED(app.wicFactory->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(app.wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
            SUCCEEDED(frame->Initialize(nullptr))) {
            frame->SetSize(pw, ph);
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            frame->SetPixelFormat(&fmt);
            if (SUCCEEDED(frame->WritePixels(ph, mapped.pitch,
                                             mapped.pitch * ph, mapped.bits)) &&
                SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
                written = true;
            }
        }
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        staging->Unmap();
        return written;
    });

    staging->Release();
    pageBitmap->Release();
    rasterDc->Release();
    return pages;
}
