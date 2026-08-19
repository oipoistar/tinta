#include "selection.h"
#include "utils.h"

#include <algorithm>
#include <cmath>

namespace {

// The layout run backing a text rect, when one exists. Word-level rects
// (wrapped text) point at their covering segment run; syntax-highlighted
// code lines have no single run and fall back to monospace approximation.
const App::LayoutTextRun* runFor(const App& app, const App::TextRect& tr) {
    if (tr.runIndex >= app.layoutTextRuns.size()) return nullptr;
    const App::LayoutTextRun& run = app.layoutTextRuns[tr.runIndex];
    if (!run.layout || run.docLength == 0) return nullptr;
    return &run;
}

// Caret offset for a document-space point inside (or near) one rect.
size_t offsetInRect(const App& app, const App::TextRect& tr, float docX, float docY) {
    if (const App::LayoutTextRun* run = runFor(app, tr)) {
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS m{};
        if (SUCCEEDED(run->layout->HitTestPoint(docX - run->pos.x, docY - run->pos.y,
                                                &trailing, &inside, &m))) {
            // Caret goes after the cluster on a trailing hit — this is the
            // glyph-boundary snapping the geometric model lacked (#83)
            size_t off = run->docStart + m.textPosition + (trailing ? m.length : 0);
            size_t lo = run->docStart;
            size_t hi = run->docStart + run->docLength;
            return std::clamp(off, lo, hi);
        }
    }
    // No layout: interpolate. Code lines are monospace, so this stays exact
    // for ASCII and close for CJK.
    if (tr.docLength == 0) return tr.docStart;
    float w = tr.rect.right - tr.rect.left;
    if (w <= 0.0f) return tr.docStart;
    float frac = std::clamp((docX - tr.rect.left) / w, 0.0f, 1.0f);
    return tr.docStart + (size_t)std::lround(frac * (float)tr.docLength);
}

// Doc-offset range covered by a line bucket
void bucketRange(const App& app, const App::LineBucket& b, size_t& lo, size_t& hi) {
    lo = SIZE_MAX;
    hi = 0;
    for (size_t idx : b.textRectIndices) {
        const App::TextRect& tr = app.textRects[idx];
        lo = std::min(lo, tr.docStart);
        hi = std::max(hi, tr.docStart + tr.docLength);
    }
}

} // namespace

size_t selectionOffsetAtPoint(const App& app, float docX, float docY) {
    if (app.lineBuckets.empty() || app.docText.empty()) return 0;

    // Nearest line by vertical distance. Buckets follow layout order; a
    // linear scan at mouse rate is cheap and copes with table rows.
    const App::LineBucket* best = nullptr;
    float bestDist = 1e9f;
    for (const App::LineBucket& b : app.lineBuckets) {
        float dist = docY < b.top ? b.top - docY
                                  : (docY > b.bottom ? docY - b.bottom : 0.0f);
        if (dist < bestDist) {
            bestDist = dist;
            best = &b;
        }
    }
    if (!best) return 0;

    const App::TextRect* nearest = nullptr;
    float nearestDx = 1e9f;
    for (size_t idx : best->textRectIndices) {
        const App::TextRect& tr = app.textRects[idx];
        if (docX >= tr.rect.left && docX <= tr.rect.right) {
            return std::min(offsetInRect(app, tr, docX, docY), app.docText.size());
        }
        float dx = docX < tr.rect.left ? tr.rect.left - docX : docX - tr.rect.right;
        if (dx < nearestDx) {
            nearestDx = dx;
            nearest = &tr;
        }
    }
    if (!nearest) return 0;
    // Off the line's text: snap to the near edge of the nearest run, so
    // drags starting in the margin select from the line start/end
    size_t off = docX < nearest->rect.left ? nearest->docStart
                                           : nearest->docStart + nearest->docLength;
    return std::min(off, app.docText.size());
}

void selectionWordRange(const App& app, size_t offset, size_t& start, size_t& end) {
    const std::wstring& t = app.docText;
    if (t.empty()) {
        start = end = 0;
        return;
    }
    if (offset >= t.size()) offset = t.size() - 1;
    // A caret sitting just after a word (trailing hit) belongs to that word
    if (offset > 0 && isWordBoundary(t[offset]) && !isWordBoundary(t[offset - 1])) {
        offset--;
    }
    if (isWordBoundary(t[offset])) {
        start = offset;
        end = offset + 1;
        return;
    }
    start = offset;
    while (start > 0 && !isWordBoundary(t[start - 1])) start--;
    end = offset + 1;
    while (end < t.size() && !isWordBoundary(t[end])) end++;
}

bool selectionLineRange(const App& app, size_t offset, size_t& start, size_t& end) {
    for (const App::LineBucket& b : app.lineBuckets) {
        size_t lo, hi;
        bucketRange(app, b, lo, hi);
        if (lo != SIZE_MAX && lo <= offset && offset <= hi) {
            start = lo;
            end = hi;
            return true;
        }
    }
    return false;
}

void selectionHighlightRects(const App& app, size_t start, size_t end,
                             std::vector<D2D1_RECT_F>& out) {
    if (start >= end) return;

    // A line-wide bar is visibly wrong for tables: it paints the padding and
    // the gaps between cells. Mark the runs that have a selectable text rect,
    // then ask DirectWrite for the exact horizontal span of the selected
    // range in each run. This is the same model used by native text views.
    std::vector<bool> runHasTextRect(app.layoutTextRuns.size(), false);
    for (const App::TextRect& tr : app.textRects) {
        if (tr.runIndex < runHasTextRect.size()) {
            runHasTextRect[tr.runIndex] = true;
        }
    }

    for (size_t i = 0; i < app.layoutTextRuns.size(); i++) {
        const App::LayoutTextRun& run = app.layoutTextRuns[i];
        if (!run.layout || run.docLength == 0 || !runHasTextRect[i]) continue;

        size_t lo = std::max(start, run.docStart);
        size_t hi = std::min(end, run.docStart + run.docLength);
        if (lo >= hi) continue;

        DWRITE_HIT_TEST_METRICS metrics{};
        UINT32 metricCount = 0;
        HRESULT hr = run.layout->HitTestTextRange(
            (UINT32)(lo - run.docStart), (UINT32)(hi - lo),
            run.pos.x, run.pos.y, &metrics, 1, &metricCount);
        if (SUCCEEDED(hr) && metricCount == 1 && metrics.width > 0.0f) {
            out.push_back(D2D1::RectF(
                metrics.left, run.bounds.top,
                metrics.left + metrics.width, run.bounds.bottom));
            continue;
        }

        // These runs are normally single-line layouts. If DirectWrite
        // returns no single range metric, caret positions still give an
        // exact boundary without falling back to the whole line.
        FLOAT x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        DWRITE_HIT_TEST_METRICS edge{};
        if (SUCCEEDED(run.layout->HitTestTextPosition(
                (UINT32)(lo - run.docStart), FALSE, &x0, &y0, &edge)) &&
            SUCCEEDED(run.layout->HitTestTextPosition(
                (UINT32)(hi - run.docStart), FALSE, &x1, &y1, &edge))) {
            float left = run.pos.x + std::min(x0, x1);
            float right = run.pos.x + std::max(x0, x1);
            if (right > left) {
                out.push_back(D2D1::RectF(left, run.bounds.top,
                                          right, run.bounds.bottom));
            }
        }
    }

    // Syntax-highlighted code lines and atomic math boxes do not have a
    // covering text layout. Keep their existing geometry fallback, but never
    // use it for a run already handled above.
    for (const App::TextRect& tr : app.textRects) {
        if (tr.runIndex < app.layoutTextRuns.size() &&
            app.layoutTextRuns[tr.runIndex].docLength > 0) {
            continue;
        }
        size_t lo = std::max(start, tr.docStart);
        size_t hi = std::min(end, tr.docStart + tr.docLength);
        if (lo >= hi) continue;

        float width = tr.rect.right - tr.rect.left;
        if (width <= 0.0f || tr.docLength == 0) continue;
        float x0 = tr.rect.left +
                   (float)(lo - tr.docStart) / (float)tr.docLength * width;
        float x1 = tr.rect.left +
                   (float)(hi - tr.docStart) / (float)tr.docLength * width;
        if (x1 > x0) {
            out.push_back(D2D1::RectF(x0, tr.rect.top, x1, tr.rect.bottom));
        }
    }
}

bool selectionRangeRect(const App& app, const App::TextRect& tr,
                        size_t start, size_t end, D2D1_RECT_F& out) {
    size_t lo = std::max(start, tr.docStart);
    size_t hi = std::min(end, tr.docStart + tr.docLength);
    if (lo >= hi) return false;
    if (const App::LayoutTextRun* run = runFor(app, tr)) {
        DWRITE_HIT_TEST_METRICS m{};
        UINT32 count = 0;
        if (SUCCEEDED(run->layout->HitTestTextRange(
                (UINT32)(lo - run->docStart), (UINT32)(hi - lo),
                run->pos.x, run->pos.y, &m, 1, &count)) && count == 1) {
            out = D2D1::RectF(m.left, tr.rect.top, m.left + m.width, tr.rect.bottom);
            return true;
        }
    }
    // Uniform interpolation without a covering layout (monospace code)
    float w = tr.rect.right - tr.rect.left;
    float cw = tr.docLength ? w / (float)tr.docLength : 0.0f;
    float x0 = tr.rect.left + (float)(lo - tr.docStart) * cw;
    float x1 = x0 + (float)(hi - lo) * cw;
    out = D2D1::RectF(x0, tr.rect.top, x1, tr.rect.bottom);
    return true;
}

std::wstring selectionTextForRange(const App& app, size_t start, size_t end) {
    start = std::min(start, app.docText.size());
    end = std::min(end, app.docText.size());
    if (start >= end) return std::wstring();
    return app.docText.substr(start, end - start);
}

void clearSelection(App& app) {
    app.selecting = false;
    app.hasSelection = false;
    app.selAnchor = 0;
    app.selFocus = 0;
    app.selAutoScrollVel = 0.0f;
    app.selShiftExtend = false;
}
