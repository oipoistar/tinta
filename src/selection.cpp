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

// Precise caret x for an offset that lies on this bucket's line
float caretXInBucket(const App& app, const App::LineBucket& b, size_t offset,
                     float fallback) {
    for (size_t idx : b.textRectIndices) {
        const App::TextRect& tr = app.textRects[idx];
        const App::LayoutTextRun* run = runFor(app, tr);
        if (run && run->docStart <= offset &&
            offset <= run->docStart + run->docLength) {
            FLOAT x = 0, y = 0;
            DWRITE_HIT_TEST_METRICS m{};
            if (SUCCEEDED(run->layout->HitTestTextPosition(
                    (UINT32)(offset - run->docStart), FALSE, &x, &y, &m))) {
                return run->pos.x + x;
            }
        }
    }
    // Monospace / layout-less rects: interpolate
    for (size_t idx : b.textRectIndices) {
        const App::TextRect& tr = app.textRects[idx];
        if (tr.docStart <= offset && offset <= tr.docStart + tr.docLength &&
            tr.docLength > 0) {
            float frac = (float)(offset - tr.docStart) / (float)tr.docLength;
            return tr.rect.left + frac * (tr.rect.right - tr.rect.left);
        }
    }
    return fallback;
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
    for (const App::LineBucket& b : app.lineBuckets) {
        size_t lo, hi;
        bucketRange(app, b, lo, hi);
        if (lo == SIZE_MAX || hi <= start || lo >= end) continue;
        // Interior lines fill their text extents; boundary lines get a
        // precise caret edge. At most two caret lookups per frame.
        float left = start > lo ? caretXInBucket(app, b, start, b.minX) : b.minX;
        float right = end < hi ? caretXInBucket(app, b, end, b.maxX) : b.maxX;
        if (right > left) {
            out.push_back(D2D1::RectF(left, b.top, right, b.bottom));
        }
    }
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
    app.selectedText.clear();
}
