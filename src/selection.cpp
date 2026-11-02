#include "selection.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

// The selected span of one run, from DirectWrite: the exact horizontal
// extent of the range, the same model used by native text views
bool runPiece(const App::LayoutTextRun& run, size_t start, size_t end, D2D1_RECT_F& out) {
    size_t lo = std::max(start, run.docStart);
    size_t hi = std::min(end, run.docStart + run.docLength);
    if (lo >= hi) return false;

    DWRITE_HIT_TEST_METRICS metrics{};
    UINT32 metricCount = 0;
    HRESULT hr = run.layout->HitTestTextRange(
        (UINT32)(lo - run.docStart), (UINT32)(hi - lo),
        run.pos.x, run.pos.y, &metrics, 1, &metricCount);
    if (SUCCEEDED(hr) && metricCount == 1 && metrics.width > 0.0f) {
        out = D2D1::RectF(metrics.left, run.bounds.top,
                          metrics.left + metrics.width, run.bounds.bottom);
        return true;
    }

    // These runs are normally single-line layouts. If DirectWrite
    // returns no single range metric, caret positions still give an
    // exact boundary without falling back to the whole line.
    FLOAT x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    DWRITE_HIT_TEST_METRICS edge{};
    if (FAILED(run.layout->HitTestTextPosition(
            (UINT32)(lo - run.docStart), FALSE, &x0, &y0, &edge)) ||
        FAILED(run.layout->HitTestTextPosition(
            (UINT32)(hi - run.docStart), FALSE, &x1, &y1, &edge))) {
        return false;
    }
    out = D2D1::RectF(run.pos.x + std::min(x0, x1), run.bounds.top,
                      run.pos.x + std::max(x0, x1), run.bounds.bottom);
    return true;
}

// Syntax-highlighted code lines and atomic math boxes do not have a
// covering text layout: their selected span is interpolated
bool rectPiece(const App::TextRect& tr, size_t start, size_t end, D2D1_RECT_F& out) {
    size_t lo = std::max(start, tr.docStart);
    size_t hi = std::min(end, tr.docStart + tr.docLength);
    float width = tr.rect.right - tr.rect.left;
    if (lo >= hi || width <= 0.0f) return false;
    float x0 = tr.rect.left + (float)(lo - tr.docStart) / (float)tr.docLength * width;
    float x1 = tr.rect.left + (float)(hi - tr.docStart) / (float)tr.docLength * width;
    out = D2D1::RectF(x0, tr.rect.top, x1, tr.rect.bottom);
    return true;
}

bool sameLine(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
    return std::abs(a.top - b.top) < 0.5f && std::abs(a.bottom - b.bottom) < 0.5f;
}

// The selected part of the space between two text rects of a line that
// belong to different runs. A run ends at its last word, so the spaces
// between two runs ("sit **amet**", "*commodo* consequat", a soft line
// break) and the padding after a code chip belong to none; filling them
// runs the selection on over bold, italic, links and code (#313). Table
// cells are apart by a tab in docText and stay apart.
bool gapPiece(const App& app, const App::TextRect& left, const App::TextRect& right,
              size_t start, size_t end, D2D1_RECT_F& out) {
    // Words of one run: its own span covers the spaces between them
    if (right.runIndex == left.runIndex && right.runIndex < app.layoutTextRuns.size()) return false;
    const size_t gapLo = left.docStart + left.docLength, gapHi = right.docStart;
    if (gapLo > gapHi || gapHi < start || gapLo > end || !sameLine(left.rect, right.rect)) return false;
    const float x0 = left.rect.right, x1 = right.rect.left;
    if (x1 - x0 < 0.01f) return false;
    for (size_t i = gapLo; i < gapHi; i++) {
        if (app.docText[i] != L' ') return false;
    }
    float from = x0, to = x1;
    if (gapLo < gapHi) {
        const size_t lo = std::max(start, gapLo), hi = std::min(end, gapHi);
        if (lo >= hi) return false;
        const float perChar = (x1 - x0) / (float)(gapHi - gapLo);
        from = x0 + perChar * (float)(lo - gapLo);
        to = x0 + perChar * (float)(hi - gapLo);
    } else if (start >= gapLo || end <= gapHi) {
        return false;  // padding alone, between runs that meet in docText
    }
    out = D2D1::RectF(from, left.rect.top, to, left.rect.bottom);
    return true;
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
    const size_t first = out.size();

    // A line-wide bar is visibly wrong for tables: it paints the padding and
    // the gaps between cells. Each run with a selectable text rect gives the
    // span of its selected range, and the spaces between runs are filled.
    // Text rects follow the layout, so a line's pieces come left to right;
    // those that meet join into one rectangle. The colour is translucent,
    // so two would leave an antialiased seam where they meet and a darker
    // strip where they overlap.
    auto add = [&](const D2D1_RECT_F& piece) {
        if (piece.right <= piece.left) return;
        if (out.size() > first) {
            D2D1_RECT_F& last = out.back();
            if (sameLine(last, piece) && piece.left <= last.right + 0.5f &&
                piece.right >= last.left - 0.5f) {
                last.left = std::min(last.left, piece.left);
                last.right = std::max(last.right, piece.right);
                return;
            }
        }
        out.push_back(piece);
    };

    const App::TextRect* prev = nullptr;
    size_t prevRun = SIZE_MAX;
    D2D1_RECT_F piece{};
    for (const App::TextRect& tr : app.textRects) {
        if (tr.docLength == 0) continue;
        if (prev && gapPiece(app, *prev, tr, start, end, piece)) add(piece);
        prev = &tr;
        if (const App::LayoutTextRun* run = runFor(app, tr)) {
            // The words of a run share its span
            if (tr.runIndex != prevRun && runPiece(*run, start, end, piece)) add(piece);
            prevRun = tr.runIndex;
        } else if (rectPiece(tr, start, end, piece)) {
            add(piece);
        }
    }
}

D2D1_RECT_F selectionBarOnScreen(const D2D1_RECT_F& bar, float scrollX, float scrollY) {
    return D2D1::RectF(bar.left - scrollX, std::round(bar.top - scrollY),
                       bar.right - scrollX, std::round(bar.bottom - scrollY));
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

// A link's address as an HTML attribute value
static std::wstring hrefAttribute(const std::string& url) {
    std::wstring out;
    for (wchar_t c : toWide(url)) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Lists and their items add no space of their own: Word and Outlook put a
// gap above and below each list, on top of the empty lines the copy keeps,
// and a task line split its list into separately spaced pieces
static const wchar_t kListNoMargins[] = L" style=\"margin-top:0;margin-bottom:0\"";

std::wstring selectionHtmlForRange(const App& app, size_t start, size_t end) {
    start = std::min(start, app.docText.size());
    end = std::min(end, app.docText.size());
    std::wstring html;
    auto bold = app.docBoldRanges.begin();
    auto italic = app.docItalicRanges.begin();
    auto link = app.docLinkRanges.begin();
    // What is open: a link around bold around italic, so the tags nest
    const App::DocLinkRange* inLink = nullptr;
    bool inBold = false, inItalic = false;
    auto closeFrom = [&](bool linkToo, bool boldToo, bool italicToo) {
        if (italicToo && inItalic) { html += L"</i>"; inItalic = false; }
        if (boldToo && inBold) { html += L"</b>"; inBold = false; }
        if (linkToo && inLink) { html += L"</a>"; inLink = nullptr; }
    };
    // List lines paste as real lists: the items whose <li> is open, outermost
    // first, each inside its open <ul> or <ol>
    std::vector<const App::DocListItem*> openItems;
    auto closeListsFrom = [&](size_t level) {
        while (openItems.size() > level) {
            html += openItems.back()->ordered ? L"</li></ol>\r\n" : L"</li></ul>\r\n";
            openItems.pop_back();
        }
    };
    // Makes the open items chain, whose first `same` are open already. A
    // list split by a task line reopens at its next item's number.
    auto openItemsFor = [&](const std::vector<const App::DocListItem*>& chain, size_t same) {
        closeListsFrom(same + 1);
        // The next item of the same list closes only the previous item
        bool sibling = false;
        if (openItems.size() > same) {
            sibling = same < chain.size() && openItems[same]->list == chain[same]->list;
            if (sibling) {
                html += L"</li>\r\n";
                openItems.pop_back();
            } else {
                closeListsFrom(same);
            }
        }
        for (size_t level = openItems.size(); level < chain.size(); level++) {
            const App::DocListItem* item = chain[level];
            if (!(sibling && level == same)) {
                if (item->ordered) {
                    html += L"<ol";
                    if (item->number != 1) html += L" start=\"" + std::to_wstring(item->number) + L"\"";
                } else {
                    html += L"<ul";
                }
                html += kListNoMargins;
                html += L">";
            }
            html += L"<li";
            html += kListNoMargins;
            html += L">";
            openItems.push_back(item);
        }
    };
    // A line is a <div> or <p> (lineEnd closes it) or part of an open <li>.
    // listContext: the last line was a list line. The empty lines after it
    // wait in blankAfterList: they go between the items of one list, and
    // stand as one empty line before anything else, like the one before.
    const wchar_t* lineEnd = nullptr;
    bool listContext = false;
    bool blankAfterList = false;
    const void* lastList = nullptr;  // the outermost list of the last list line
    auto leaveLists = [&]() {
        closeListsFrom(0);
        if (blankAfterList) html += L"<div><br></div>\r\n";
        blankAfterList = false;
    };
    const App::DocListItem* marked = nullptr;  // the last item given a text marker
    // Opens the line at i: a <div>, or the <li> of the list item around it
    // (an empty item counts as around its own position)
    auto openLine = [&](size_t i) {
        std::vector<const App::DocListItem*> chain;  // items around i, outermost first
        for (const App::DocListItem& item : app.docListItems) {
            if (item.start > i) break;
            if (i < item.end || item.start == i) chain.push_back(&item);
        }
        // From a task item in, lines paste as indented paragraphs with the
        // marker as text: Word bullets every list item whatever its style,
        // which put a bullet before each checkbox
        size_t firstTask = 0;
        while (firstTask < chain.size() && !chain[firstTask]->task) firstTask++;
        std::vector<const App::DocListItem*> asText(chain.begin() + firstTask, chain.end());
        chain.resize(firstTask);
        listContext = !chain.empty() || !asText.empty();
        if (!listContext) {
            leaveLists();
            lastList = nullptr;
            html += L"<div>";
            lineEnd = L"</div>";
            return;
        }
        // Another list, even right after one, stands apart as on screen
        const void* list = (chain.empty() ? asText.front() : chain.front())->list;
        if (list != lastList) leaveLists();
        blankAfterList = false;
        lastList = list;
        size_t same = 0;
        while (same < openItems.size() && same < chain.size() && openItems[same] == chain[same])
            same++;
        if (same == chain.size() && same == openItems.size()) {
            if (asText.empty()) html += L"<br>";  // another line of the same item
        } else {
            openItemsFor(chain, same);
        }
        if (!asText.empty()) {
            // A <p>: Word drops a <div>'s margin, so the line lost its indent
            html += L"<p style=\"margin:0 0 0 " + std::to_wstring(36 * asText.size()) + L"pt\">";
            lineEnd = L"</p>";
            const App::DocListItem* item = asText.back();
            if (item != marked) {
                marked = item;
                if (item->task) html += item->task == 2 ? L"\u2611 " : L"\u2610 ";
                else if (item->ordered) html += std::to_wstring(item->number) + L". ";
                else html += L"\u2022 ";
            }
        }
    };
    bool inLine = false;
    bool lineStart = true;
    for (size_t i = start; i < end; i++) {
        while (bold != app.docBoldRanges.end() && bold->second <= i) ++bold;
        while (italic != app.docItalicRanges.end() && italic->second <= i) ++italic;
        while (link != app.docLinkRanges.end() && link->end <= i) ++link;
        wchar_t c = app.docText[i];
        if (c == L'\r') continue;
        // Each line is a <div>, so Word and Outlook paste a paragraph per
        // line as they do the plain text (with <br> the whole copy became
        // one paragraph); what is open closes with its line. Inside a list
        // a line belongs to its <li>, and the empty lines between items go.
        if (c == L'\n') {
            closeFrom(true, true, true);
            // An empty item has no character to open it: open it at its line
            if (!inLine) {
                for (const App::DocListItem& item : app.docListItems) {
                    if (item.start > i) break;
                    if (item.start == i && item.end == i) { openLine(i); inLine = true; break; }
                }
            }
            if (lineEnd) {
                html += lineEnd;
                html += L"\r\n";
                lineEnd = nullptr;
            } else if (!inLine) {
                if (listContext) blankAfterList = true;
                else html += L"<div><br></div>\r\n";
            }
            inLine = false;
            lineStart = true;
            continue;
        }
        if (!inLine) { openLine(i); inLine = true; }
        const App::DocLinkRange* wantLink =
            link != app.docLinkRanges.end() && link->start <= i ? &*link : nullptr;
        bool wantBold = bold != app.docBoldRanges.end() && bold->first <= i;
        bool wantItalic = italic != app.docItalicRanges.end() && italic->first <= i;
        // A change closes its own tag and those inside it, then reopens
        bool linkChanges = wantLink != inLink;
        bool boldChanges = linkChanges || wantBold != inBold;
        bool italicChanges = boldChanges || wantItalic != inItalic;
        closeFrom(linkChanges, boldChanges, italicChanges);
        if (linkChanges && wantLink) {
            html += L"<a href=\"" + hrefAttribute(wantLink->url) + L"\">";
            inLink = wantLink;
        }
        if (boldChanges && wantBold) { html += L"<b>"; inBold = true; }
        if (italicChanges && wantItalic) { html += L"<i>"; inItalic = true; }
        switch (c) {
            case L'&': html += L"&amp;"; break;
            case L'<': html += L"&lt;"; break;
            case L'>': html += L"&gt;"; break;
            case L'"': html += L"&quot;"; break;
            // A plain tab is one tab in Word; mso-tab-count around it made two
            case L'\t': html += L"&#9;"; break;
            case L' ': {
                // HTML collapses runs of spaces; keep the copied ones
                bool next = i + 1 < end && app.docText[i + 1] == L' ';
                html += (lineStart || next) ? L"&nbsp;" : L" ";
                break;
            }
            default: html += c; break;
        }
        lineStart = false;
    }
    closeFrom(true, true, true);
    if (lineEnd) html += lineEnd;
    closeListsFrom(0);
    return html;
}

bool copySelectionRange(const App& app, HWND hwnd, size_t start, size_t end, bool* held) {
    std::wstring text = selectionTextForRange(app, start, end);
    if (text.empty()) return false;
    return copyRichTextToClipboard(hwnd, text, selectionHtmlForRange(app, start, end), held);
}

void clearSelection(App& app) {
    app.selecting = false;
    app.hasSelection = false;
    app.selAnchor = 0;
    app.selFocus = 0;
    app.selAutoScrollVel = 0.0f;
    app.selShiftExtend = false;
}
