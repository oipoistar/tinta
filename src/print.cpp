#include "print.h"
#include "render.h"
#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "paper.h"
#include "settings.h"
#include "utils.h"

#include <fstream>
#include <sstream>

#include <commdlg.h>
#include <documenttarget.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <winspool.h>

#include <algorithm>
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

// Paper-derived palette with a true white page, set in the screen theme's
// own body, heading and code faces (#258)
D2DTheme printTheme(const D2DTheme& screen) {
    D2DTheme t = THEMES[0];  // Paper (light)
    t.fontFamily = screen.fontFamily;
    t.headingFontFamily = screen.headingFontFamily;
    t.codeFontFamily = screen.codeFontFamily;
    t.background = D2D1::ColorF(1.0f, 1.0f, 1.0f);
    t.codeBackground = hexColor(0xF4F4F4);
    // Inline code without a box stays without one on paper (#278)
    if (screen.inlineCodeBackground && screen.inlineCodeBackground->a <= 0.0f)
        t.inlineCodeBackground = screen.inlineCodeBackground;
    return t;
}

namespace {

// What paper asks of the document (docs/specs/scholar.md, phase 3): its
// front matter in pandoc's variables, then the preview's choices for it
struct PaperSetup {
    paper::Front front;
    bool titleBlock = false, twoColumns = false, pageNumbers = false, runningHeads = false;
    float zoom = 1.0f;      // fontsize, against the 12 pt the page sets
    float marginMm = 19.05f;
    int paper = -1;         // papersize, into PRINT_PAPERS
};

PaperSetup paperSetup(const App& app) {
    PaperSetup s;
    s.front = paper::frontOf(app.root);
    s.titleBlock = s.front.titleBlock();
    const bool mine = app.printChoices.file == app.currentFile;
    auto choice = [&](int chosen, bool otherwise) { return mine && chosen >= 0 ? chosen != 0 : otherwise; };
    s.twoColumns = choice(app.printChoices.columns, s.front.twoColumn);
    // A paper numbers its pages and heads them; anything else asks
    s.pageNumbers = choice(app.printChoices.pageNumbers, s.titleBlock);
    s.runningHeads = choice(app.printChoices.runningHeads, s.titleBlock);
    if (s.front.fontSizePt > 0) {
        const float body = printTheme(app.theme).bodySize;
        s.zoom = s.front.fontSizePt * 96.0f / 72.0f / (body > 0 ? body : 16.0f);
    }
    // Page margins on every side (#257): the preview's choice, kept in
    // settings.ini, 0.75 in unless changed; a paper's geometry: first
    s.marginMm = s.front.marginMm > 0 && !(mine && app.printChoices.ownMargin) ? s.front.marginMm
                                                                                 : app.printMarginMm;
    s.paper = paper::paperIndex(s.front);
    return s;
}

using SavedView = App::PrintSavedView;

// Saves the screen view state and switches to the print palette at 96 DPI
// and the given zoom (a paper's fontsize). layoutAtPrintWidth does the
// actual re-layout; leavePrintLayout restores everything.
void enterPrintLayout(App& app, SavedView& saved, float zoom = 1.0f) {
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
    saved.editMode = app.editMode;
    saved.showToc = app.showToc;
    saved.showFolderBrowser = app.showFolderBrowser;
    saved.showSearchPanel = app.showSearchPanel;

    app.editMode = false;  // layout width must be the page, not a pane (#81)
    // Panel widths belong to the screen
    app.showToc = app.showFolderBrowser = app.showSearchPanel = false;
    app.printLayout = true;
    app.contentScale = 1.0f;
    app.zoomFactor = zoom;
    app.theme = printTheme(app.theme);
    updateTextFormats(app);
}

// Blocks wider than the printable area cannot reflow (mermaid diagrams and
// tables have fixed layouts), so they print shrunk to fit the margins. Each
// overflowing primitive seeds a vertical band that grows to swallow every
// primitive it overlaps — the whole diagram or table scales together about
// its own top-left, leaving whitespace below rather than moving later
// content (pagination keeps using the unscaled bounds). Above `fromY` lies
// a title block spanning both columns, which stays as it is.
void computeShrinkBands(App& app, float contentW, float fromY = 0.0f) {
    app.printShrinkBands.clear();
    const float eps = 0.5f;

    struct Item { float top, bottom, right; };
    std::vector<Item> items;
    items.reserve(app.layoutRects.size() + app.layoutLines.size() +
                  app.layoutConnectors.size() + app.layoutShapes.size() +
                  app.layoutBitmaps.size() + app.layoutTextRuns.size());
    for (const auto& r : app.layoutRects)
        items.push_back({r.rect.top, r.rect.bottom, r.rect.right});
    for (const auto& l : app.layoutLines)
        items.push_back({std::min(l.p1.y, l.p2.y), std::max(l.p1.y, l.p2.y),
                         std::max(l.p1.x, l.p2.x)});
    for (const auto& c : app.layoutConnectors)
        items.push_back({c.bounds.top, c.bounds.bottom, c.bounds.right});
    for (const auto& s : app.layoutShapes)
        items.push_back({s.rect.top, s.rect.bottom, s.rect.right});
    for (const auto& b : app.layoutBitmaps)
        items.push_back({b.destRect.top, b.destRect.bottom, b.destRect.right});
    for (const auto& t : app.layoutTextRuns)
        items.push_back({t.bounds.top, t.bounds.bottom, t.bounds.right});
    if (fromY > 0.0f) {
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [&](const Item& it) { return it.top < fromY; }),
                    items.end());
    }

    // Seed bands from overflowing items
    struct Band { float top, bottom, right; };
    std::vector<Band> bands;
    for (const auto& it : items) {
        if (it.right <= contentW + eps) continue;
        bands.push_back({it.top, it.bottom, it.right});
    }
    if (bands.empty()) return;

    // Grow bands over everything they vertically overlap, then merge bands
    // that grew into each other; repeat until stable
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& b : bands) {
            for (const auto& it : items) {
                if (it.bottom <= b.top + eps || it.top >= b.bottom - eps) continue;
                if (it.top < b.top)       { b.top = it.top;       changed = true; }
                if (it.bottom > b.bottom) { b.bottom = it.bottom; changed = true; }
                if (it.right > b.right)   { b.right = it.right;   changed = true; }
            }
        }
        std::sort(bands.begin(), bands.end(),
                  [](const Band& a, const Band& b) { return a.top < b.top; });
        for (size_t i = 0; i + 1 < bands.size();) {
            if (bands[i + 1].top < bands[i].bottom - eps) {
                bands[i].bottom = std::max(bands[i].bottom, bands[i + 1].bottom);
                bands[i].right = std::max(bands[i].right, bands[i + 1].right);
                bands.erase(bands.begin() + i + 1);
                changed = true;
            } else {
                i++;
            }
        }
    }

    for (const auto& b : bands) {
        float scale = std::max(0.1f, contentW / b.right);
        app.printShrinkBands.push_back({b.top, b.bottom, scale});
    }
}

// Re-layouts the document at page-content width and finds the oversized
// blocks that need shrinking
void layoutAtPrintWidth(App& app, float contentWidthDips) {
    app.width = (int)contentWidthDips;
    app.height = 4000;  // layout does not clip vertically; any tall value
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    computeShrinkBands(app, contentWidthDips);
}

void leavePrintLayout(App& app, const SavedView& saved) {
    app.printLayout = false;
    app.paperPrint = {};
    app.editMode = saved.editMode;
    app.showToc = saved.showToc;
    app.showFolderBrowser = saved.showFolderBrowser;
    app.showSearchPanel = saved.showSearchPanel;
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

// Where a stretch of the document that starts at pageTop and may run
// pageContentH ends. Breaks avoid slicing text lines, images, and diagram
// shapes by pulling the cut above any straddling atom; an atom taller than
// most of the stretch is sliced rather than looping - unless a taller
// stretch follows (a column under a title block), which then takes it
// whole, from the stretch's very top if need be.
float breakAfter(const App& app, float pageTop, float pageContentH, bool tallerFollows = false) {
    float proposed = pageTop + pageContentH;
    for (int pass = 0; pass < 4; pass++) {
        float adjusted = proposed;
        auto consider = [&](float top, float bottom) {
            if (top < adjusted && bottom > adjusted && (top > pageTop || (tallerFollows && top >= pageTop))) {
                adjusted = top;
            }
        };
        for (const auto& line : app.lineBuckets) consider(line.top, line.bottom);
        // List markers have no selection line bucket. Protect every text
        // run so it cannot straddle a cut and be replayed on both pages.
        for (const auto& run : app.layoutTextRuns) consider(run.bounds.top, run.bounds.bottom);
        for (const auto& bmp : app.layoutBitmaps) consider(bmp.destRect.top, bmp.destRect.bottom);
        for (const auto& shape : app.layoutShapes) consider(shape.rect.top, shape.rect.bottom);
        // Shrink bands page-break as a unit: a scaled diagram split
        // across pages would render discontinuously
        for (const auto& band : app.printShrinkBands) consider(band.top, band.bottom);
        if (adjusted == proposed) break;
        proposed = adjusted;
    }
    // Monster atom (taller than 70% of a page): slice it instead
    if (proposed - pageTop < pageContentH * 0.3f && !tallerFollows) {
        return pageTop + pageContentH;
    }
    return proposed;
}

// Page break positions in document Y, a page's worth apart at most
std::vector<float> computePageBreaks(const App& app, float pageContentH) {
    std::vector<float> breaks;
    float docEnd = app.contentHeight;
    float y = 0.0f;
    while (y + pageContentH < docEnd) {
        float next = breakAfter(app, y, pageContentH);
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
                       float offsetX, float offsetY,
                       D2D1_DRAW_TEXT_OPTIONS textOptions = D2D1_DRAW_TEXT_OPTIONS_NONE) {
    auto inRange = [&](float top, float bottom) {
        return bottom >= yStart && top < yEnd;
    };
    float dx = offsetX;
    float dy = offsetY - yStart;

    // Oversized blocks draw scaled about their band's top-left; the band
    // transform composes with whatever transform the caller set (the
    // preview rasterizer scales the whole page to display resolution)
    D2D1_MATRIX_3X2_F baseTransform;
    dc->GetTransform(&baseTransform);
    auto applyBand = [&](float top, float bottom) {
        for (const auto& band : app.printShrinkBands) {
            if (top >= band.top - 0.5f && bottom <= band.bottom + 0.5f) {
                dc->SetTransform(
                    D2D1::Matrix3x2F::Scale(band.scale, band.scale,
                        D2D1::Point2F(dx, band.top + dy)) * baseTransform);
                return true;
            }
        }
        return false;
    };
    auto resetBand = [&](bool wasBanded) {
        if (wasBanded) dc->SetTransform(baseTransform);
    };

    for (const auto& rect : app.layoutRects) {
        if (!inRange(rect.rect.top, rect.rect.bottom)) continue;
        bool banded = applyBand(rect.rect.top, rect.rect.bottom);
        const D2D1_RECT_F box = D2D1::RectF(rect.rect.left + dx, rect.rect.top + dy,
                                            rect.rect.right + dx, rect.rect.bottom + dy);
        if (rect.color.a > 0.0f) {
            brush->SetColor(rect.color);
            if (rect.radius > 0.0f) dc->FillRoundedRectangle(D2D1::RoundedRect(box, rect.radius, rect.radius), brush);
            else dc->FillRectangle(box, brush);
        }
        if (rect.strokeWidth > 0.0f && rect.stroke.a > 0.0f) {
            const float in = rect.strokeWidth * 0.5f;
            brush->SetColor(rect.stroke);
            dc->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(box.left + in, box.top + in, box.right - in, box.bottom - in),
                                  std::max(0.0f, rect.radius - in), std::max(0.0f, rect.radius - in)),
                brush, rect.strokeWidth);
        }
        resetBand(banded);
    }

    for (const auto& line : app.layoutLines) {
        float top = std::min(line.p1.y, line.p2.y);
        float bottom = std::max(line.p1.y, line.p2.y);
        if (!inRange(top, bottom)) continue;
        bool banded = applyBand(top, bottom);
        brush->SetColor(line.color);
        dc->DrawLine(D2D1::Point2F(line.p1.x + dx, line.p1.y + dy),
                     D2D1::Point2F(line.p2.x + dx, line.p2.y + dy),
                     brush, line.stroke);
        resetBand(banded);
    }

    // Mermaid connectors (with arrowheads and dashing)
    for (const auto& connector : app.layoutConnectors) {
        if (!inRange(connector.bounds.top, connector.bounds.bottom)) continue;
        if (connector.points.size() < 2) continue;
        bool banded = applyBand(connector.bounds.top, connector.bounds.bottom);
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
        resetBand(banded);
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
        bool banded = applyBand(shape.rect.top, shape.rect.bottom);
        D2D1_RECT_F r = D2D1::RectF(shape.rect.left + dx, shape.rect.top + dy,
                                    shape.rect.right + dx, shape.rect.bottom + dy);
        if (shape.type == App::LayoutShapeType::Path) {
            if (shape.geometry) {
                D2D1_MATRIX_3X2_F previous;
                dc->GetTransform(&previous);
                dc->SetTransform(
                    D2D1::Matrix3x2F::Translation(r.left, r.top) * previous);
                if (shape.fill.a > 0.0f) {
                    brush->SetColor(shape.fill);
                    dc->FillGeometry(shape.geometry, brush);
                }
                if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                    brush->SetColor(shape.stroke);
                    dc->DrawGeometry(shape.geometry, brush, shape.strokeWidth);
                }
                dc->SetTransform(previous);
            }
        } else if (shape.type == App::LayoutShapeType::Diamond) {
            float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
            D2D1_POINT_2F pts[] = {{cx, r.top}, {r.right, cy}, {cx, r.bottom}, {r.left, cy}};
            fillStrokePolygon(pts, 4, shape);
        } else if (shape.type == App::LayoutShapeType::Hexagon) {
            float inset = (r.right - r.left) * 0.18f;
            float cy = (r.top + r.bottom) * 0.5f;
            D2D1_POINT_2F pts[] = {{r.left + inset, r.top}, {r.right - inset, r.top},
                                   {r.right, cy}, {r.right - inset, r.bottom},
                                   {r.left + inset, r.bottom}, {r.left, cy}};
            fillStrokePolygon(pts, 6, shape);
        } else if (shape.type == App::LayoutShapeType::Ellipse) {
            D2D1_ELLIPSE e = D2D1::Ellipse(
                D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f),
                (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f);
            if (shape.fill.a > 0.0f) { brush->SetColor(shape.fill); dc->FillEllipse(e, brush); }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                brush->SetColor(shape.stroke);
                dc->DrawEllipse(e, brush, shape.strokeWidth);
            }
        } else if (shape.type == App::LayoutShapeType::RoundedRectangle ||
                   shape.type == App::LayoutShapeType::Stadium) {
            float radius = shape.type == App::LayoutShapeType::Stadium
                ? (r.bottom - r.top) * 0.5f : shape.radius;
            D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(r, radius, radius);
            if (shape.fill.a > 0.0f) { brush->SetColor(shape.fill); dc->FillRoundedRectangle(rr, brush); }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                brush->SetColor(shape.stroke);
                dc->DrawRoundedRectangle(rr, brush, shape.strokeWidth);
            }
        } else {
            if (shape.fill.a > 0.0f) { brush->SetColor(shape.fill); dc->FillRectangle(r, brush); }
            if (shape.stroke.a > 0.0f && shape.strokeWidth > 0.0f) {
                brush->SetColor(shape.stroke);
                dc->DrawRectangle(r, brush, shape.strokeWidth);
            }
        }
        resetBand(banded);
    }

    // Images
    for (const auto& bmp : app.layoutBitmaps) {
        if (!bmp.bitmap) continue;
        if (!inRange(bmp.destRect.top, bmp.destRect.bottom)) continue;
        bool banded = applyBand(bmp.destRect.top, bmp.destRect.bottom);
        dc->DrawBitmap(bmp.bitmap.Get(),
            D2D1::RectF(bmp.destRect.left + dx, bmp.destRect.top + dy,
                        bmp.destRect.right + dx, bmp.destRect.bottom + dy),
            bmp.opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        resetBand(banded);
    }

    // Text
    for (const auto& run : app.layoutTextRuns) {
        if (!run.layout) continue;
        if (!inRange(run.bounds.top, run.bounds.bottom)) continue;
        bool banded = applyBand(run.bounds.top, run.bounds.bottom);
        brush->SetColor(run.color);
        dc->DrawTextLayout(D2D1::Point2F(run.pos.x + dx, run.pos.y + dy),
                           run.layout, brush, textOptions);
        resetBand(banded);
    }
}

struct PageGeometry {
    float pageW, pageH;          // full page in DIPs
    float contentW, contentH;    // inside the margins
    float margin;                // on every side
};

PageGeometry pageGeometry(float pageW, float pageH, float marginMm) {
    const float margin = marginMm * 96.0f / 25.4f;
    return {pageW, pageH, pageW - margin * 2, pageH - margin * 2, margin};
}

// Paper size for previewing before the dialog runs, decided by locale only.
// Probing the default printer here (CreateDC) initializes third-party
// drivers and can pop their dialogs (Acrobat Distiller's broken-temp-folder
// error, for one) just to read a paper size — the real printer is consulted
// after the user confirms the print dialog, and re-pagination handles any
// difference.
PageGeometry defaultPageGeometry(float marginMm) {
    wchar_t measure[2] = L"0";
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE, measure, 2) &&
        measure[0] == L'1') {
        return pageGeometry(816.0f, 1056.0f, marginMm);  // US customary: Letter
    }
    return pageGeometry(794.0f, 1123.0f, marginMm);  // A4
}

// A paper's own paper, else the last preview's, else the locale's
PageGeometry setupPageGeometry(const App& app, const PaperSetup& setup) {
    const int index = setup.paper >= 0 ? setup.paper : app.printPreviewPaper;
    if (index < 0 || index >= PRINT_PAPER_COUNT) return defaultPageGeometry(setup.marginMm);
    const PrintPaper& paper = PRINT_PAPERS[index];
    return pageGeometry(app.printPreviewLandscape ? paper.h : paper.w,
                        app.printPreviewLandscape ? paper.w : paper.h, setup.marginMm);
}

// --- Paper (docs/specs/scholar.md, phase 3) ---------------------------------

// Two columns share the text width, a quarter inch apart
constexpr float kColumnGap = 24.0f;

float columnWidth(const PageGeometry& geo) { return std::floor((geo.contentW - kColumnGap) / 2.0f); }

// Lays the document out for paper: at the text width, or at a column's
// with two, a paper's title block across both. Returns the width laid out.
float layoutPaper(App& app, const PaperSetup& setup, const PageGeometry& geo) {
    const float width = setup.twoColumns ? columnWidth(geo) : geo.contentW;
    app.paperPrint.titleBlock = true;
    app.paperPrint.width = setup.twoColumns ? geo.contentW : 0.0f;
    app.paperPrint.titleBottom = 0.0f;
    app.width = (int)width;
    app.height = 4000;  // layout does not clip vertically; any tall value
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    computeShrinkBands(app, width, setup.twoColumns ? app.paperPrint.titleBottom : 0.0f);
    return width;
}

// Where the laid-out document lands on paper. One column: a page a
// stretch, as print always did. Two: the title block across the top of
// the first page, then each page's left column and its right, every
// stretch cut where a page would be.
std::vector<App::PrintFrame> paperFrames(const App& app, const PaperSetup& setup, const PageGeometry& geo,
                                          float columnW) {
    std::vector<App::PrintFrame> frames;
    const float end = app.contentHeight;
    if (!setup.twoColumns) {
        std::vector<float> breaks = computePageBreaks(app, geo.contentH);
        breaks.push_back(end + 1.0f);  // final page sentinel
        float y = 0.0f;
        for (size_t i = 0; i < breaks.size(); i++) {
            frames.push_back({(int)i, y, breaks[i], geo.margin, geo.margin, {}});
            y = breaks[i];
        }
        return frames;
    }
    int page = 0;
    float y = 0.0f, top = 0.0f;  // top: where the columns start on the page
    const float title = app.paperPrint.titleBottom;
    if (title > 0.0f) {
        // A title block longer than a page takes pages of its own
        while (title - y > geo.contentH) {
            const float next = breakAfter(app, y, geo.contentH);
            frames.push_back({page++, y, next, geo.margin, geo.margin, {}});
            y = next;
        }
        frames.push_back({page, y, title, geo.margin, geo.margin, {}});
        top = title - y;
        y = title;
        // Columns shorter than a fifth of the page start on the next
        if (geo.contentH - top < geo.contentH * 0.2f) {
            ++page;
            top = 0.0f;
        }
    }
    const float rightX = geo.margin + geo.contentW - columnW;
    const float gutter = (geo.margin + columnW + rightX) * 0.5f;
    int column = 0;
    while (y < end) {
        const float height = geo.contentH - top;
        const float next = y + height >= end ? end + 1.0f
                                             : breakAfter(app, y, height, height < geo.contentH - 0.5f);
        const float frameTop = geo.margin + top;
        // Each column is clipped to its own box: what straddles a cut shows
        // in one column, never over the title block or the other column
        const D2D1_RECT_F clip = column ? D2D1::RectF(gutter, frameTop, geo.pageW, frameTop + (next - y))
                                        : D2D1::RectF(0.0f, frameTop, gutter, frameTop + (next - y));
        frames.push_back({page, y, next, column ? rightX : geo.margin, frameTop, clip});
        y = next;
        if (column) {
            ++page;
            top = 0.0f;
        }
        column ^= 1;
    }
    return frames;
}

// Each page's first stretch and the end: the page boundaries the preview
// counts its pages by
std::vector<float> pageBounds(const std::vector<App::PrintFrame>& frames) {
    std::vector<float> bounds;
    for (const auto& frame : frames) {
        if ((int)bounds.size() == frame.page) bounds.push_back(frame.yStart);
    }
    bounds.push_back(frames.empty() ? 0.0f : frames.back().yEnd);
    return bounds;
}

// What the margins carry: page numbers, and running heads from the second page
struct Furniture {
    bool numbers = false, heads = false;
    std::wstring left, right;  // the short title or title; the first author et al.
};

Furniture paperFurniture(const App& app, const PaperSetup& setup) {
    Furniture f;
    f.numbers = setup.pageNumbers;
    f.heads = setup.runningHeads;
    const std::string& head = setup.front.shortTitle.empty() ? setup.front.title : setup.front.shortTitle;
    f.left = toWide(paper::plainText(head));
    // A document without a title is headed by its first heading
    if (f.left.empty() && !app.headings.empty()) f.left = app.headings.front().text;
    f.right = toWide(paper::headAuthors(setup.front.authors, setup.front.lang));
    return f;
}

// Page numbers centred in the bottom margin, running heads in the top one
// from the second page on, in a small muted face; a margin too narrow for
// the line drops them rather than set them over the text
void drawFurniture(App& app, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                   const PageGeometry& geo, int page, const Furniture& f) {
    const bool heads = f.heads && page > 0 && !(f.left.empty() && f.right.empty());
    if (!f.numbers && !heads) return;
    const float body = app.textFormat ? app.textFormat->GetFontSize() : 16.0f;
    const float size = std::min(body * 0.75f, (geo.margin - 4.0f) / 1.3f);
    if (size < 6.0f) return;
    IDWriteTextFormat* format = nullptr;
    app.dwriteFactory->CreateTextFormat(app.theme.fontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us",
                                        &format);
    if (!format) return;
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    const float lineH = size * 1.3f;
    D2D1_COLOR_F muted = app.theme.text;
    muted.a *= 0.6f;
    brush->SetColor(muted);
    auto layoutOf = [&](const std::wstring& text, float width, DWRITE_TEXT_ALIGNMENT align) {
        IDWriteTextLayout* layout = nullptr;
        app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), format, std::max(1.0f, width), lineH,
                                            &layout);
        if (!layout) return layout;
        IDWriteTextLayout2* layout2 = nullptr;
        if (app.fontFallback &&
            SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout2), (void**)&layout2))) {
            layout2->SetFontFallback(app.fontFallback);
            layout2->Release();
        }
        layout->SetTextAlignment(align);
        // A head too long for its room ends in an ellipsis
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        IDWriteInlineObject* ellipsis = nullptr;
        app.dwriteFactory->CreateEllipsisTrimmingSign(format, &ellipsis);
        layout->SetTrimming(&trimming, ellipsis);
        if (ellipsis) ellipsis->Release();
        return layout;
    };
    auto draw = [&](IDWriteTextLayout* layout, float left, float centreY) {
        if (!layout) return;
        dc->DrawTextLayout(D2D1::Point2F(left, centreY - lineH * 0.5f), layout, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        layout->Release();
    };
    const float left = geo.margin, width = geo.contentW;
    if (f.numbers) {
        draw(layoutOf(std::to_wstring(page + 1), width, DWRITE_TEXT_ALIGNMENT_CENTER), left,
             geo.pageH - geo.margin * 0.5f);
    }
    if (heads) {
        // The authors keep their width; the title takes what is left
        float rightW = 0.0f;
        if (!f.right.empty()) {
            if (IDWriteTextLayout* measure = layoutOf(f.right, width * 0.45f, DWRITE_TEXT_ALIGNMENT_TRAILING)) {
                DWRITE_TEXT_METRICS metrics{};
                measure->GetMetrics(&metrics);
                rightW = std::min(width * 0.45f, metrics.widthIncludingTrailingWhitespace + 1.0f);
                measure->Release();
            }
            draw(layoutOf(f.right, rightW, DWRITE_TEXT_ALIGNMENT_TRAILING), left + width - rightW, geo.margin * 0.5f);
        }
        if (!f.left.empty()) {
            draw(layoutOf(f.left, width - rightW - (rightW > 0 ? 24.0f : 0.0f), DWRITE_TEXT_ALIGNMENT_LEADING), left,
                 geo.margin * 0.5f);
        }
    }
    format->Release();
}

// One page: its stretches of the document, then the margins' furniture
void drawPaperPage(App& app, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                   const std::vector<App::PrintFrame>& frames, int page, const PageGeometry& geo,
                   const Furniture& furniture) {
    for (const auto& frame : frames) {
        if (frame.page != page) continue;
        const bool clip = frame.clip.right > frame.clip.left;
        if (clip) dc->PushAxisAlignedClip(frame.clip, D2D1_ANTIALIAS_MODE_ALIASED);
        drawDocumentRange(app, dc, brush, frame.yStart, frame.yEnd, frame.x, frame.y);
        if (clip) dc->PopAxisAlignedClip();
    }
    drawFurniture(app, dc, brush, geo, page, furniture);
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
            Furniture furniture;
            furniture.numbers = app.printOptionOn[PRINT_PAGE_NUMBERS];
            furniture.heads = app.printOptionOn[PRINT_RUNNING_HEADS];
            furniture.left = app.printHeadLeft;
            furniture.right = app.printHeadRight;
            drawPaperPage(app, dc, brush, app.printPreviewFrames, page,
                          pageGeometry(app.printPreviewPageW, app.printPreviewPageH, app.printPreviewMarginMm),
                          furniture);
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
int renderPages(App& app, const PageGeometry& geo, const PaperSetup& setup, EmitFn emitPage) {
    if (!app.deviceContext || !app.root) return -1;

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    if (!device) return -1;

    SavedView saved{};
    enterPrintLayout(app, saved, setup.zoom);
    // Print/PDF run synchronously and have no message pump: the async
    // PlantUML queue can never deliver here, so the print layout is the
    // single place allowed to render a missing diagram inline, bounded by
    // one shared millisecond budget. The print preview and the link peek
    // share layoutAtPrintWidth but stay async (they run with a pump); the
    // reflow timer refreshes an open preview when a render lands.
    app.plantumlPrintLayout = true;
    app.plantumlPrintBudgetMsLeft = 30000;
    const float columnW = layoutPaper(app, setup, geo);
    // Cleared right after the layout: the post-print relayout at screen
    // width is interactive again (message pump available), so it schedules
    // through the async queue instead of blocking the UI thread.
    app.plantumlPrintLayout = false;
    app.plantumlPrintBudgetMsLeft = 0;
    const std::vector<App::PrintFrame> frames = paperFrames(app, setup, geo, columnW);
    const Furniture furniture = paperFurniture(app, setup);
    const int pageCount = frames.empty() ? 0 : frames.back().page + 1;

    ID2D1DeviceContext* pageDc = nullptr;
    device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pageDc);
    int pages = 0;
    bool ok = pageDc != nullptr;

    for (int i = 0; ok && i < pageCount; i++) {
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
            drawPaperPage(app, pageDc, brush, frames, i, geo, furniture);
            brush->Release();
        }
        ok = SUCCEEDED(pageDc->EndDraw()) && SUCCEEDED(commandList->Close());
        if (ok) {
            ok = emitPage(commandList, pages);
            pages++;
        }
        commandList->Release();
        pageDc->SetTarget(nullptr);
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

    // Preset the preview's paper and orientation; whatever the user picks
    // in the dialog still wins (the geometry is re-read from the DC)
    if (app.printPreviewPaper >= 0) {
        pd.hDevMode = GlobalAlloc(GHND, sizeof(DEVMODEW));
        if (pd.hDevMode) {
            auto* dm = (DEVMODEW*)GlobalLock(pd.hDevMode);
            if (dm) {
                dm->dmSize = sizeof(DEVMODEW);
                dm->dmSpecVersion = DM_SPECVERSION;
                dm->dmFields = DM_ORIENTATION | DM_PAPERSIZE;
                dm->dmOrientation = app.printPreviewLandscape
                    ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
                dm->dmPaperSize = PRINT_PAPERS[app.printPreviewPaper].dmPaper;
                GlobalUnlock(pd.hDevMode);
            }
        }
    }

    if (!PrintDlgW(&pd) || !pd.hDC) {
        if (pd.hDevMode) GlobalFree(pd.hDevMode);
        if (pd.hDevNames) GlobalFree(pd.hDevNames);
        return false;  // cancelled
    }

    float dpiX = (float)GetDeviceCaps(pd.hDC, LOGPIXELSX);
    float dpiY = (float)GetDeviceCaps(pd.hDC, LOGPIXELSY);
    float pageW = GetDeviceCaps(pd.hDC, PHYSICALWIDTH) * 96.0f / dpiX;
    float pageH = GetDeviceCaps(pd.hDC, PHYSICALHEIGHT) * 96.0f / dpiY;
    if (pageW < 200 || pageH < 200) {  // driver returned nonsense
        pageW = 794.0f;   // A4
        pageH = 1123.0f;
    }
    const PaperSetup setup = paperSetup(app);
    PageGeometry geo = pageGeometry(pageW, pageH, setup.marginMm);

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
        int pages = renderPages(app, geo, setup, [&](ID2D1CommandList* list, int) {
            return SUCCEEDED(printControl->AddPage(
                list, D2D1::SizeF(geo.pageW, geo.pageH), nullptr));
        });
        ok = pages > 0 && SUCCEEDED(printControl->Close());
        printControl->Release();
    }
    target->Release();
    return ok;
}

// Lays out, paginates, fits and rasterizes for the currently selected
// paper format. Assumes enterPrintLayout has already run.
static void refreshPreviewLayout(App& app) {
    const PrintPaper& paper = PRINT_PAPERS[app.printPreviewPaper];
    float pageW = app.printPreviewLandscape ? paper.h : paper.w;
    float pageH = app.printPreviewLandscape ? paper.w : paper.h;
    app.printPreviewPageW = pageW;
    app.printPreviewPageH = pageH;
    const PaperSetup setup = paperSetup(app);
    app.printPreviewMarginMm = setup.marginMm;
    const PageGeometry geo = pageGeometry(pageW, pageH, setup.marginMm);

    const float columnW = layoutPaper(app, setup, geo);
    app.printPreviewFrames = paperFrames(app, setup, geo, columnW);
    app.printPreviewBounds = pageBounds(app.printPreviewFrames);
    const Furniture furniture = paperFurniture(app, setup);
    app.printOptionOn[PRINT_TWO_COLUMNS] = setup.twoColumns;
    app.printOptionOn[PRINT_PAGE_NUMBERS] = furniture.numbers;
    app.printOptionOn[PRINT_RUNNING_HEADS] = furniture.heads;
    app.printHeadLeft = furniture.left;
    app.printHeadRight = furniture.right;

    // Fit the page between the header, the paper options and the button bar
    float ui = app.printSaved.contentScale;
    float availW = app.printSaved.width - 80.0f * ui;
    float availH = app.printSaved.height - (130.0f + PRINT_OPTION_ROW_H) * ui;
    app.printPreviewFit = std::max(0.05f,
        std::min(availW / pageW, availH / pageH));
    app.printPreviewPxW = (unsigned)(pageW * app.printPreviewFit);
    app.printPreviewPxH = (unsigned)(pageH * app.printPreviewFit);

    int pageCount = (int)app.printPreviewBounds.size() - 1;
    app.printPreviewPage = std::max(0, std::min(app.printPreviewPage, pageCount - 1));
    rasterizePreviewPage(app, app.printPreviewPage);
}

// First open: start from the default printer's paper, matched to the
// closest offered format
static void detectDefaultFormat(App& app) {
    PageGeometry geo = defaultPageGeometry(app.printMarginMm);
    bool landscape = geo.pageW > geo.pageH;
    float w = landscape ? geo.pageH : geo.pageW;
    float h = landscape ? geo.pageW : geo.pageH;
    int best = 0;
    float bestDiff = 1e9f;
    for (int i = 0; i < PRINT_PAPER_COUNT; i++) {
        float diff = std::fabs(w - PRINT_PAPERS[i].w) + std::fabs(h - PRINT_PAPERS[i].h);
        if (diff < bestDiff) { bestDiff = diff; best = i; }
    }
    app.printPreviewPaper = best;
    app.printPreviewLandscape = landscape;
}

void openPrintPreview(App& app, HWND hwnd) {
    if (app.showPrintPreview || !app.root || !app.deviceContext) return;
    if (app.editMode) {
        editorReparse(app);  // include unsaved edits in the preview and printout
    }
    if (app.printPreviewPaper < 0) detectDefaultFormat(app);
    // A paper names its paper and type size (pandoc's papersize, fontsize)
    const PaperSetup setup = paperSetup(app);
    if (setup.paper >= 0) app.printPreviewPaper = setup.paper;

    enterPrintLayout(app, app.printSaved, setup.zoom);
    app.printPreviewPage = 0;
    refreshPreviewLayout(app);
    app.showPrintPreview = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void printPreviewToggle(App& app, int option) {
    if (option < 0 || option >= PRINT_OPTION_COUNT) return;
    const PaperSetup setup = paperSetup(app);
    const bool on[PRINT_OPTION_COUNT] = {setup.twoColumns, setup.pageNumbers, setup.runningHeads};
    if (app.printChoices.file != app.currentFile) app.printChoices = {app.currentFile};
    int* choice[PRINT_OPTION_COUNT] = {&app.printChoices.columns, &app.printChoices.pageNumbers,
                                       &app.printChoices.runningHeads};
    *choice[option] = on[option] ? 0 : 1;
    if (!app.showPrintPreview) return;
    refreshPreviewLayout(app);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void printPreviewSetFormat(App& app, int paper, bool landscape) {
    paper = std::max(0, std::min(paper, PRINT_PAPER_COUNT - 1));
    if (!app.showPrintPreview) {
        app.printPreviewPaper = paper;
        app.printPreviewLandscape = landscape;
        return;
    }
    if (paper == app.printPreviewPaper && landscape == app.printPreviewLandscape) return;
    app.printPreviewPaper = paper;
    app.printPreviewLandscape = landscape;
    refreshPreviewLayout(app);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void printPreviewRefresh(App& app) {
    if (!app.showPrintPreview) return;
    refreshPreviewLayout(app);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void printPreviewSetMargin(App& app, float mm) {
    mm = std::max(5.0f, std::min(mm, 50.0f));
    // A chip clicked wins over a paper's geometry: margin= for this document
    const float printed = paperSetup(app).marginMm;
    if (app.printChoices.file != app.currentFile) app.printChoices = {app.currentFile};
    app.printChoices.ownMargin = true;
    if (std::fabs(mm - app.printMarginMm) < 0.005f && std::fabs(mm - printed) < 0.005f) return;
    if (std::fabs(mm - app.printMarginMm) >= 0.005f) {
        app.printMarginMm = mm;
        persistPrintMargin(app);
    }
    if (!app.showPrintPreview) return;
    refreshPreviewLayout(app);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void closePrintPreview(App& app, HWND hwnd) {
    if (!app.showPrintPreview) return;
    app.showPrintPreview = false;
    leavePrintLayout(app, app.printSaved);
    app.printPreviewPixels.clear();
    app.printPreviewPixels.shrink_to_fit();
    app.printPreviewBounds.clear();
    app.printPreviewFrames.clear();
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
    // A4, or the paper a paper names
    const PaperSetup setup = paperSetup(app);
    const PrintPaper& paper = PRINT_PAPERS[setup.paper >= 0 ? setup.paper : 0];
    PageGeometry geo = pageGeometry(paper.w, paper.h, setup.marginMm);

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

    int pages = renderPages(app, geo, setup, [&](ID2D1CommandList* list, int index) {
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

// --- Export as PDF (#export_as) ---
//
// The print pipeline already produces vector pages with driver-side font
// embedding; routing the same job to the Microsoft Print to PDF driver
// with the job output redirected into a file stream yields the PDF at the
// chosen path with no dialog. Pages use the paper a paper names, else the
// last print-preview paper choice, or the locale default before any
// preview ran.
bool exportPdfFile(App& app, const std::wstring& path) {
    if (!app.root || !app.deviceContext) return false;
    if (app.editMode) {
        editorReparse(app);  // include unsaved edits, like the preview
    }

    const PaperSetup setup = paperSetup(app);
    const PageGeometry geo = setupPageGeometry(app, setup);

    IStream* fileStream = nullptr;
    if (FAILED(SHCreateStreamOnFileEx(
            path.c_str(), STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
            FILE_ATTRIBUTE_NORMAL, TRUE, nullptr, &fileStream))) {
        return false;
    }

    IPrintDocumentPackageTargetFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(
            __uuidof(PrintDocumentPackageTargetFactory), nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        fileStream->Release();
        return false;
    }
    std::wstring jobName = L"Tinta - " + toWide(app.currentFile);
    IPrintDocumentPackageTarget* target = nullptr;
    HRESULT hr = factory->CreateDocumentPackageTargetForPrintJob(
        L"Microsoft Print to PDF", jobName.c_str(), fileStream, nullptr,
        &target);
    factory->Release();
    if (FAILED(hr) || !target) {
        fileStream->Release();
        return false;
    }

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    ID2D1PrintControl* printControl = nullptr;
    if (device) {
        device->CreatePrintControl(app.wicFactory, target, nullptr,
                                   &printControl);
        device->Release();
    }

    bool ok = false;
    if (printControl) {
        int pages = renderPages(app, geo, setup, [&](ID2D1CommandList* list, int) {
            return SUCCEEDED(printControl->AddPage(
                list, D2D1::SizeF(geo.pageW, geo.pageH), nullptr));
        });
        ok = pages > 0 && SUCCEEDED(printControl->Close());
        printControl->Release();
    }
    target->Release();
    fileStream->Commit(STGC_DEFAULT);
    fileStream->Release();
    return ok;
}

// Sheets: a note card lays out at its card's width and draws a slice of
// itself into its own bitmap, through the print path's layout and replay
void layoutForWidth(App& app, float widthDips) { layoutAtPrintWidth(app, widthDips); }

void drawLayoutRange(App& app, ID2D1DeviceContext* dc, ID2D1SolidColorBrush* brush,
                     float yStart, float yEnd, float offsetX, float offsetY,
                     D2D1_DRAW_TEXT_OPTIONS textOptions) {
    drawDocumentRange(app, dc, brush, yStart, yEnd, offsetX, offsetY, textOptions);
}

// Link peek: renders the top of another markdown file at the live theme
// into a 2x bitmap. The document swap rides the print-layout save/restore
// (the palette switch is undone so the peek matches the screen).
ID2D1Bitmap* renderPeekBitmap(App& app, const std::wstring& path,
                              float widthDips, float& heightDips) {
    if (!app.deviceContext || !app.root) return nullptr;
    std::ifstream file(path);
    if (!file) return nullptr;
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto parsed = parseDocument(app.parser, buffer.str(), path);
    if (!parsed.success || !parsed.root) return nullptr;

    qmd::ElementPtr savedRoot = app.root;
    std::string savedFile = app.currentFile;
    SavedView saved{};
    enterPrintLayout(app, saved);
    // The peek must look exactly like the screen: keep the palette, the
    // DPI scale, the zoom and the screen's padding instead of the print
    // defaults; the reading column stays full width inside the small panel
    app.printLayout = false;
    app.theme = saved.theme;
    app.contentScale = saved.contentScale;
    app.zoomFactor = saved.zoomFactor;
    int savedPct = app.readingWidthPct;
    app.readingWidthPct = 100;
    updateTextFormats(app);
    app.root = parsed.root;
    app.currentFile = toUtf8(path);  // relative images resolve at the target
    // Plain-text targets drop the whole-file code block's card: the peek
    // panel is already the frame
    app.peekFrameless = isPlainTextDocumentPath(std::wstring_view(path));
    layoutAtPrintWidth(app, widthDips);
    app.peekFrameless = false;
    // The layout starts below the (invisible) title strip; the peek slice
    // begins there and shrinks to short targets
    float topPad = chromeTopHeight(app);
    heightDips = std::max(dpi(app, 60.0f),
                          std::min(heightDips,
                                   app.contentHeight - topPad + dpi(app, 8.0f)));

    ID2D1Device* device = nullptr;
    app.deviceContext->GetDevice(&device);
    ID2D1DeviceContext* dc = nullptr;
    ID2D1Bitmap1* bitmap = nullptr;
    ID2D1Bitmap* result = nullptr;
    if (device && SUCCEEDED(device->CreateDeviceContext(
                      D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc))) {
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED),
            192.0f, 192.0f);
        D2D1_SIZE_U px = D2D1::SizeU((UINT32)(widthDips * 2.0f),
                                     (UINT32)(heightDips * 2.0f));
        if (SUCCEEDED(dc->CreateBitmap(px, nullptr, 0, &props, &bitmap))) {
            dc->SetTarget(bitmap);
            dc->SetDpi(192.0f, 192.0f);  // 1 DIP = 2px: the 2x crispness
            dc->BeginDraw();
            dc->Clear(app.theme.background);
            ID2D1SolidColorBrush* brush = nullptr;
            dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush);
            if (brush) {
                drawDocumentRange(app, dc, brush, topPad, topPad + heightDips,
                                  0.0f, 0.0f);
                brush->Release();
            }
            if (SUCCEEDED(dc->EndDraw())) {
                result = bitmap;
                bitmap = nullptr;
            }
        }
    }
    if (bitmap) bitmap->Release();
    if (dc) dc->Release();
    if (device) device->Release();

    app.root = savedRoot;
    app.currentFile = savedFile;
    app.readingWidthPct = savedPct;
    leavePrintLayout(app, saved);
    return result;
}
