#include "frontmatter_ui.h"
#include "editor.h"
#include "live_editor.h"
#include "live_text.h"
#include "document.h"
#include "drafts.h"
#include "signals.h"
#include "tableedit.h"
#include "editrail.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"
#include "file_utils.h"
#include "render.h"
#include "d2d_init.h"
#include "search.h"
#include "print.h"
#include "i18n.h"
#include "input.h"
#include "sidepanels.h"
#include "source_style.h"

#include "export.h"
#include "sheet_ui.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <functional>
#include <imm.h>
#include <commdlg.h>

#define TIMER_EDITOR_REPARSE 2

// Markdown assists (defined near the char handler)
static void editorToggleInlineMark(App& app, HWND hwnd,
                                   const std::wstring& mark);
static size_t editorPosFromClick(App& app, int x, int y);

// --- UTF conversion ---

std::string toUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &out[0], len, nullptr, nullptr);
    return out;
}

static std::wstring fromUtf8(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], len);
    return out;
}

// --- DirectWrite line helpers ---
//
// The editor font is monospace for ASCII, but CJK and other full-width
// glyphs render wider than editorCharWidth, so caret, click, and selection
// math must go through DirectWrite hit testing instead of multiplying a
// column index by a fixed character width.

// Width available for line text in the editor pane (after the rail, the
// slim line-number gutter, and padding)
static float editorTextMaxWidth(const App& app) {
    float chrome = dpi(app, 48.0f) + editorGutterWidth(app);
    float padding = dpi(app, 8.0f);
    return std::max(10.0f, editorPaneWidth(app) - chrome - padding * 2.0f);
}

// Left edge of the line text: right of the rail column and the in-editor
// line-number gutter (design t11)
static float editorTextX(const App& app) {
    return dpi(app, 48.0f) + editorGutterWidth(app);
}

static IDWriteTextLayout* createEditorLineLayout(App& app, size_t lineStart, size_t lineLen) {
    if (!app.dwriteFactory || !app.editorTextFormat || lineLen == 0) return nullptr;
    float maxWidth = editorWrapOn(app) ? editorTextMaxWidth(app) : 1e7f;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(
        app.editorText.data() + lineStart, (UINT32)lineLen,
        app.editorTextFormat, maxWidth, 1e7f, &layout);
    // The shared editor format is NO_WRAP; the wrap toggle overrides per layout
    if (layout && editorWrapOn(app)) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
    // Language-aware CJK fallback (#48): without this the editor pane falls
    // back through the system chain and shows Japanese-variant glyphs for
    // Chinese text (#81 "门") while the preview beside it renders correctly
    if (layout && app.fontFallback) {
        IDWriteTextLayout2* layout2 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout2),
                reinterpret_cast<void**>(&layout2)))) {
            layout2->SetFontFallback(app.fontFallback);
            layout2->Release();
        }
    }
    return layout;
}

// Paint-path variant: returns a cached layout owned by the cache — callers
// must NOT release it. The cache is cleared on any text/format/width change.
static IDWriteTextLayout* cachedEditorLineLayout(App& app, size_t lineStart, size_t lineLen) {
    auto it = app.editorLineLayoutCache.find(lineStart);
    if (it != app.editorLineLayoutCache.end()) {
        if (it->second.len == lineLen) {
            it->second.lastUsed = ++app.editorLayoutUseClock;
            return it->second.layout;
        }
        if (it->second.layout) it->second.layout->Release();
        app.editorLineLayoutCache.erase(it);
    }
    IDWriteTextLayout* layout = createEditorLineLayout(app, lineStart, lineLen);
    // Drawn lines in colour (#268); hit tests lay lines out plain, at the same widths
    sourceHighlightLine(app, layout, lineStart, lineLen);
    app.editorLineLayoutCache[lineStart] = {
        layout, lineLen, ++app.editorLayoutUseClock};

    if (app.editorLineLayoutCache.size() > App::EDITOR_LAYOUT_CACHE_MAX) {
        auto victim = app.editorLineLayoutCache.end();
        for (auto candidate = app.editorLineLayoutCache.begin();
             candidate != app.editorLineLayoutCache.end(); ++candidate) {
            if (candidate->first == lineStart) continue;
            if (victim == app.editorLineLayoutCache.end() ||
                candidate->second.lastUsed < victim->second.lastUsed) {
                victim = candidate;
            }
        }
        if (victim != app.editorLineLayoutCache.end()) {
            if (victim->second.layout) victim->second.layout->Release();
            app.editorLineLayoutCache.erase(victim);
        }
    }
    return layout;
}

// Caret x,y within a (possibly wrapped) line layout for the caret placed
// before column `col`
static void editorCaretXY(IDWriteTextLayout* layout, size_t col, float& x, float& y,
                          bool upstream = false) {
    x = 0.0f;
    y = 0.0f;
    if (!layout) return;
    FLOAT hx = 0, hy = 0;
    DWRITE_HIT_TEST_METRICS m{};
    // Before this character and after the preceding one are different visual
    // positions at a soft wrap (#224). Default to the new row; preserve the
    // preceding row only when hit testing explicitly placed the caret there.
    HRESULT hr = (col > 0 && upstream)
        ? layout->HitTestTextPosition((UINT32)(col - 1), TRUE, &hx, &hy, &m)
        : layout->HitTestTextPosition((UINT32)col, FALSE, &hx, &hy, &m);
    if (SUCCEEDED(hr)) {
        x = hx;
        y = hy;
    }
}

static bool editorHitCaret(App& app, IDWriteTextLayout* layout, float x, float y,
                           size_t lineStart, size_t lineLen, size_t& position) {
    if (!layout) return false;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hit{};
    if (FAILED(layout->HitTestPoint(x, y, &trailing, &inside, &hit))) return false;
    size_t col = std::min(lineLen, (size_t)hit.textPosition + (trailing ? hit.length : 0));
    position = lineStart + col;
    app.editorCaretUpstreamPos = trailing && col ? position : std::wstring::npos;
    return true;
}

// X offset of the caret placed before column `col`, relative to the text origin.
static float editorColToX(const App& app, IDWriteTextLayout* layout, size_t col) {
    if (col == 0) return 0.0f;
    if (layout) {
        FLOAT x = 0, y = 0;
        DWRITE_HIT_TEST_METRICS m{};
        if (SUCCEEDED(layout->HitTestTextPosition((UINT32)(col - 1), TRUE, &x, &y, &m)))
            return x;
    }
    float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth
        : (app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 0.6f : 10.0f);
    return col * charWidth;
}

// --- Surrogate-pair helpers ---

static bool isHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
static bool isLowSurrogate(wchar_t c)  { return c >= 0xDC00 && c <= 0xDFFF; }

// Start of the character (code point) preceding pos
static size_t editorPrevCharStart(const App& app, size_t pos) {
    if (pos == 0) return 0;
    size_t p = pos - 1;
    if (p > 0 && isLowSurrogate(app.editorText[p]) && isHighSurrogate(app.editorText[p - 1])) p--;
    return p;
}

// End of the character (code point) starting at pos
static size_t editorNextCharEnd(const App& app, size_t pos) {
    size_t len = app.editorText.size();
    if (pos >= len) return len;
    size_t p = pos + 1;
    if (p < len && isHighSurrogate(app.editorText[pos]) && isLowSurrogate(app.editorText[p])) p++;
    return p;
}

// --- Line starts ---

static void rebuildEditorRowMetrics(App& app);

void rebuildLineStarts(App& app) {
    app.editorCaretUpstreamPos = std::wstring::npos;
    ++app.editorTextRevision;
    app.clearEditorLineLayoutCache();
    app.editorLineStarts.clear();
    app.editorLineStarts.push_back(0);
    for (size_t i = 0; i < app.editorText.size(); i++) {
        if (app.editorText[i] == L'\n') {
            app.editorLineStarts.push_back(i + 1);
        }
    }
    rebuildEditorRowMetrics(app);
}

static size_t getLineFromPos(const App& app, size_t pos) {
    // Binary search for line containing pos
    size_t lo = 0, hi = app.editorLineStarts.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (app.editorLineStarts[mid] <= pos) lo = mid;
        else hi = mid;
    }
    return lo;
}

static size_t getColFromPos(const App& app, size_t pos) {
    size_t line = getLineFromPos(app, pos);
    return pos - app.editorLineStarts[line];
}

static size_t getLineEnd(const App& app, size_t line) {
    if (line + 1 < app.editorLineStarts.size())
        return app.editorLineStarts[line + 1] - 1; // before '\n'
    return app.editorText.size();
}

static size_t getLineLength(const App& app, size_t line) {
    return getLineEnd(app, line) - app.editorLineStarts[line];
}

// --- Soft-wrap row metrics ---
//
// In wrap mode each logical line occupies one or more visual rows.
// editorRowStarts holds the cumulative row count before each line so
// scroll, click, and caret math can map between rows and lines.

static void rebuildEditorRowMetrics(App& app) {
    app.editorRowStarts.clear();
    app.editorTotalRows = 0;
    app.editorRowMetricsWidth = -1.0f;
    if (!editorWrapOn(app) || !app.editMode || editorLiveOn(app)) return;

    float maxTextWidth = editorTextMaxWidth(app);
    app.editorRowMetricsWidth = maxTextWidth;
    float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth
        : (app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 0.6f : 10.0f);

    size_t lineCount = app.editorLineStarts.size();
    app.editorRowStarts.reserve(lineCount + 1);
    app.editorRowStarts.push_back(0);
    for (size_t i = 0; i < lineCount; i++) {
        size_t lineLen = getLineLength(app, i);
        // A line can't wrap unless it could exceed the pane width even at
        // full-width glyph advances (2x the ASCII cell) — skip the layout
        // for the common short line.
        size_t rows = 1;
        if (lineLen > 0 && (float)lineLen * charWidth * 2.0f > maxTextWidth) {
            IDWriteTextLayout* layout =
                createEditorLineLayout(app, app.editorLineStarts[i], lineLen);
            if (layout) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(layout->GetMetrics(&tm)) && tm.lineCount > 0) {
                    rows = tm.lineCount;
                }
                layout->Release();
            }
        }
        app.editorRowStarts.push_back(app.editorRowStarts.back() + rows);
    }
    app.editorTotalRows = app.editorRowStarts.back();
}

// Rebuild row metrics if the wrap width changed (resize, zoom, splitter,
// preview toggle) or the line count is out of sync
static void ensureEditorRowMetrics(App& app) {
    if (!editorWrapOn(app)) return;
    if (app.editorRowStarts.size() != app.editorLineStarts.size() + 1 ||
        std::abs(editorTextMaxWidth(app) - app.editorRowMetricsWidth) > 0.5f) {
        rebuildEditorRowMetrics(app);
    }
}

// Logical line containing a global visual row
static size_t editorLineFromRow(const App& app, size_t row) {
    if (app.editorRowStarts.size() < 2) return 0;
    size_t lo = 0, hi = app.editorRowStarts.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (app.editorRowStarts[mid] <= row) lo = mid;
        else hi = mid;
    }
    return lo;
}

// Top visible logical line for the current editor scroll position
size_t editorTopVisibleLine(App& app) {
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    size_t row = (size_t)std::max(0.0f, app.editorScrollY / lineHeight);
    if (editorWrapOn(app)) {
        ensureEditorRowMetrics(app);
        return editorLineFromRow(app, row);
    }
    return row;
}

// The other way round from syncPreviewToEditor: the editor scrolls to the
// source line at the preview's top, and the next sync keeps the preview
// there. A drag on the preview's scrollbar moves both panes (#280), as the
// wheel over the preview does. Live's page has no source beside it.
void editorFollowPreview(App& app) {
    if (!editSplitPreview(app) || app.scrollAnchors.empty() ||
        app.editorLineByteOffsets.empty() || app.editorLineStarts.empty()) {
        return;
    }
    // The page's point at the preview's top, as the sync aligns it
    const float y = app.scrollY + documentContentTop(app);
    const auto& anchors = app.scrollAnchors;
    size_t lo = 0, hi = anchors.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (anchors[mid].renderedY <= y) lo = mid;
        else hi = mid;
    }
    // Between two anchors the sync interpolates by source offset; past the
    // last one it spreads the remaining source over the remaining page
    double offset = (double)anchors[lo].sourceOffset;
    float span = 0.0f;
    double bytes = 0.0;
    if (lo + 1 < anchors.size()) {
        span = anchors[lo + 1].renderedY - anchors[lo].renderedY;
        bytes = (double)anchors[lo + 1].sourceOffset - (double)anchors[lo].sourceOffset;
    } else {
        span = app.contentHeight - anchors[lo].renderedY;
        bytes = (double)app.editorLineByteOffsets.back() - (double)anchors[lo].sourceOffset;
    }
    if (span > 0.0f && bytes > 0.0 && y > anchors[lo].renderedY) {
        offset += std::min(1.0f, (y - anchors[lo].renderedY) / span) * bytes;
    }
    // Above the first block the page's top leads back to the source's
    // first line, as the sync maps that stretch (#291)
    const float top = documentContentTop(app);
    if (y < anchors[0].renderedY && anchors[0].renderedY > top) {
        offset = std::clamp((y - top) / (anchors[0].renderedY - top), 0.0f, 1.0f) *
                 (double)anchors[0].sourceOffset;
    }
    const auto next = std::upper_bound(app.editorLineByteOffsets.begin(),
                                       app.editorLineByteOffsets.end(), (size_t)offset);
    size_t line = next == app.editorLineByteOffsets.begin()
                      ? 0 : (size_t)(next - app.editorLineByteOffsets.begin() - 1);
    line = std::min(line, app.editorLineStarts.size() - 1);

    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float row = (float)line;
    if (editorWrapOn(app)) {
        ensureEditorRowMetrics(app);
        if (line < app.editorRowStarts.size()) row = (float)app.editorRowStarts[line];
    }
    const float editorMax = std::max(0.0f, app.editorContentHeight - (float)app.height);
    app.editorScrollY = std::clamp(row * lineHeight, 0.0f, editorMax);
    // Rounding must not put the top a row short of the line
    if (editorTopVisibleLine(app) < line && app.editorScrollY < editorMax) {
        app.editorScrollY = std::min(editorMax, app.editorScrollY + 0.5f);
    }
}

// The edit-mode preview follows the editor: the editor's top line maps
// through the source-offset scroll anchors the layout records to a
// preview scroll position (#77, #85). Called once per frame. Live's page
// scrolls on its own: following the source, whose height a visit to it
// had measured, held the page still under the wheel and pulled it back
// from the caret (#307)
void syncPreviewToEditor(App& app) {
    if (editSplitPreview(app) && !app.scrollAnchors.empty() && !app.editorLineByteOffsets.empty()) {
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
        if (topByteOffset < app.scrollAnchors[0].sourceOffset) {
            // Above the first block (front matter, a heading's marker): from
            // the page's top to that block. The offsets' difference, being
            // unsigned, wrapped here and sent the preview on to the next
            // block (#291)
            const float top = documentContentTop(app);
            const float t = (float)topByteOffset / (float)app.scrollAnchors[0].sourceOffset;
            targetY = top + t * (app.scrollAnchors[0].renderedY - top);
        } else if (lo + 1 < app.scrollAnchors.size() &&
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

        // The docked pane runs to the window's bottom edge (#245), so the
        // preview scrolls exactly as far as the reader does
        float previewMaxScroll =
            std::max(0.0f, app.contentHeight - (float)app.height);
        // renderedY includes the page's top padding below the strip;
        // subtract it so an editor at its top means a page at its top, and
        // snap the last half-line so the page top is always reachable
        // (t11 feedback)
        float alignY = targetY - documentContentTop(app);
        float synced = std::max(0.0f, std::min(alignY, previewMaxScroll));
        if (app.editorScrollY <= 0.5f) synced = 0.0f;
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
}

// Up/Down in wrap mode moves by VISUAL row, staying near the same x —
// within a wrapped line first, then into the adjacent logical line
static void editorMoveCursorVertical(App& app, bool down) {
    ensureEditorRowMetrics(app);
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    size_t line = getLineFromPos(app, app.editorCursorPos);
    size_t col = app.editorCursorPos - app.editorLineStarts[line];
    bool upstream = app.editorCaretUpstreamPos == app.editorCursorPos;

    IDWriteTextLayout* layout = createEditorLineLayout(
        app, app.editorLineStarts[line], getLineLength(app, line));
    float cx = 0, cy = 0;
    editorCaretXY(layout, col, cx, cy, upstream);
    if (app.editorDesiredCol < 0) {
        app.editorDesiredX = cx;
        app.editorDesiredCol = 0;  // wrap mode uses the flag only; x is authoritative
    }

    auto rowsFor = [](IDWriteTextLayout* lay) {
        UINT32 count = 0;
        if (lay) lay->GetLineMetrics(nullptr, 0, &count);
        std::vector<DWRITE_LINE_METRICS> rows(count);
        if (count && FAILED(lay->GetLineMetrics(rows.data(), count, &count))) rows.clear();
        return rows;
    };

    bool moved = false;
    if (layout) {
        auto rows = rowsFor(layout);
        size_t row = 0, rowEnd = 0;
        float rowTop = 0;
        // Resolve row membership by text offsets and the retained wrap side,
        // not glyph top coordinates (which differ for fallback fonts).
        for (; row + 1 < rows.size(); ++row) {
            rowEnd += rows[row].length;
            if (col < rowEnd || (col == rowEnd && upstream)) break;
            rowTop += rows[row].height;
        }
        if (!rows.empty() && ((down && row + 1 < rows.size()) || (!down && row > 0))) {
            float targetY = down ? rowTop + rows[row].height + rows[row+1].height * 0.5f
                                 : rowTop - rows[row-1].height * 0.5f;
            moved = editorHitCaret(app, layout, app.editorDesiredX, targetY,
                app.editorLineStarts[line], getLineLength(app, line), app.editorCursorPos);
        }
        layout->Release();
    }

    if (!moved) {
        bool hasAdjacent = down ? (line + 1 < app.editorLineStarts.size()) : (line > 0);
        if (!hasAdjacent) return;
        size_t adjacent = down ? line + 1 : line - 1;
        size_t adjacentLen = getLineLength(app, adjacent);
        IDWriteTextLayout* adjacentLayout = createEditorLineLayout(
            app, app.editorLineStarts[adjacent], adjacentLen);
        float targetY = lineHeight * 0.5f;
        auto rows = rowsFor(adjacentLayout);
        if (!rows.empty()) {
            targetY = rows.front().height * 0.5f;
            if (!down) {
                targetY = -rows.back().height * 0.5f;
                for (const auto& row : rows) targetY += row.height;
            }
        }
        if (!adjacentLen) {
            app.editorCursorPos = app.editorLineStarts[adjacent];
            app.editorCaretUpstreamPos = std::wstring::npos;
        } else {
            editorHitCaret(app, adjacentLayout, app.editorDesiredX, targetY,
                app.editorLineStarts[adjacent], adjacentLen, app.editorCursorPos);
        }
        if (adjacentLayout) adjacentLayout->Release();
    }
}

// Screen-space origin of a caret at pos (the primary one or an extra)
static bool editorCaretPointAt(App& app, size_t pos, bool upstream,
                               D2D1_POINT_2F& point) {
    if (!app.editMode || !app.editorTextFormat || app.editorLineStarts.empty() ||
        pos > app.editorText.size()) {
        return false;
    }
    ensureEditorRowMetrics(app);
    size_t line = getLineFromPos(app, pos);
    size_t start = app.editorLineStarts[line];
    auto* layout = cachedEditorLineLayout(app, start, getLineLength(app, line));
    float x = 0, y = 0;
    editorCaretXY(layout, pos - start, x, y, upstream);
    float height = app.editorTextFormat->GetFontSize() * 1.5f;
    float row = editorWrapOn(app) ? (float)app.editorRowStarts[line] : (float)line;
    point.x = editorTextX(app) + x - (editorWrapOn(app) ? 0.0f : app.editorScrollX);
    point.y = chromeTopHeight(app) + dpi(app,8) + row*height - app.editorScrollY +
        (editorWrapOn(app) ? y : 0.0f);
    return true;
}

bool editorCaretPoint(App& app, D2D1_POINT_2F& point) {
    return editorCaretPointAt(app, app.editorCursorPos,
                              app.editorCaretUpstreamPos == app.editorCursorPos, point);
}

// --- Undo/Redo ---

// Actions pushed while a scope is alive undo and redo as one step: an
// edit at several carets, a line move, a mark toggle (#251)
struct UndoGroupScope {
    App& app;
    bool owner;
    explicit UndoGroupScope(App& a) : app(a), owner(a.editorUndoGroup == 0) {
        if (owner) {
            if (++a.editorUndoGroupSerial == 0) ++a.editorUndoGroupSerial;
            a.editorUndoGroup = a.editorUndoGroupSerial;
        }
    }
    ~UndoGroupScope() {
        if (owner) app.editorUndoGroup = 0;
    }
    UndoGroupScope(const UndoGroupScope&) = delete;
    UndoGroupScope& operator=(const UndoGroupScope&) = delete;
};

static void pushUndo(App& app, App::EditAction::Type type, size_t pos,
                      const std::wstring& text, size_t curBefore, size_t curAfter) {
    // Coalesce consecutive single-char inserts (grouped edits stay apart)
    if (type == App::EditAction::Insert && text.size() == 1 && !app.undoStack.empty() &&
        app.editorUndoGroup == 0) {
        auto& last = app.undoStack.back();
        if (last.type == App::EditAction::Insert && last.group == 0 &&
            last.position + last.text.size() == pos &&
            text[0] != L'\n' && text[0] != L' ') {
            last.text += text;
            last.cursorAfter = curAfter;
            return;
        }
    }
    App::EditAction action{type, pos, text, curBefore, curAfter};
    action.group = app.editorUndoGroup;
    app.undoStack.push_back(std::move(action));
    app.redoStack.clear();
}

// One action that swaps removed for inserted at pos
static void pushReplaceUndo(App& app, size_t pos, const std::wstring& removed,
                            const std::wstring& inserted, size_t curBefore,
                            size_t curAfter) {
    App::EditAction action{App::EditAction::Replace, pos, removed, curBefore,
                           curAfter, inserted};
    action.group = app.editorUndoGroup;
    app.undoStack.push_back(std::move(action));
    app.redoStack.clear();
}

// Undo-capable range replace for out-of-pane editors (the table cell
// editor, #148): one committed cell is one atomic undo action.
void editorReplaceRangeExternal(App& app, size_t start, size_t end,
                                const std::wstring& repl) {
    if (start > app.editorText.size()) start = app.editorText.size();
    if (end > app.editorText.size()) end = app.editorText.size();
    if (end < start) end = start;
    std::wstring removed = app.editorText.substr(start, end - start);
    if (removed == repl) return;  // no-op edits stay off the undo stack
    pushReplaceUndo(app, start, removed, repl, app.editorCursorPos, start + repl.size());
    app.editorExtraCarets.clear();  // positions past the cell would go stale
    app.editorText.replace(start,end-start,repl);
    app.editorCursorPos = start + repl.size();
    app.editorHasSelection = false;
    app.editorDesiredCol = -1;
    rebuildLineStarts(app);
    app.clearEditorLineLayoutCache();
    app.editorRowMetricsWidth = -1.0f;
}

static void scheduleReparse(App& app);

void editorApplyLiveEdits(App& app, const std::vector<LiveText::Change>& changes, size_t caret, size_t anchor) {
    if (!changes.empty()) {
        const size_t before = app.editorCursorPos;
        const LiveText::Change& first = changes.front();
        if (changes.size() == 1 && !first.removed && first.inserted.size() == 1) {
            app.editorText.insert(first.at, first.inserted);
            pushUndo(app, App::EditAction::Insert, first.at, first.inserted, before, caret);
        } else {
            UndoGroupScope group(app);
            for (const LiveText::Change& c : changes) {
                size_t at = std::min(c.at, app.editorText.size());
                size_t removed = std::min(c.removed, app.editorText.size() - at);
                pushReplaceUndo(app, at, app.editorText.substr(at, removed), c.inserted, before, caret);
                app.editorText.replace(at, removed, c.inserted);
            }
        }
        app.editorExtraCarets.clear();
        rebuildLineStarts(app);
        scheduleReparse(app);
    }
    app.editorCursorPos = std::min(caret, app.editorText.size());
    app.editorSelStart = std::min(anchor, app.editorText.size());
    app.editorSelEnd = app.editorCursorPos;
    app.editorHasSelection = app.editorSelStart != app.editorSelEnd;
    app.editorCaretUpstreamPos = std::wstring::npos;
    app.editorDesiredCol = -1;
}

// Pad a markdown table for insertion at pos: a table cannot interrupt a
// paragraph, so a blank line must separate it on both sides (#181)
static std::wstring padTableForInsert(const App& app,
                                      const std::wstring& table, size_t pos) {
    int nlBefore = 0;
    for (size_t k = pos; k > 0 && app.editorText[k - 1] == L'\n' && nlBefore < 2; k--) {
        nlBefore++;
    }
    std::wstring pre;
    if (pos > 0 && nlBefore < 2) pre = (nlBefore == 1) ? L"\n" : L"\n\n";
    std::wstring post = L"\n";
    if (pos < app.editorText.size() && app.editorText[pos] != L'\n') post = L"\n\n";
    return pre + table + post;
}

// A grouped step comes back as a whole, and with a single caret: the
// extra carets' positions belong to the text before the step
static void editorUndo(App& app) {
    if (app.undoStack.empty()) return;
    app.editorExtraCarets.clear();
    const unsigned group = app.undoStack.back().group;
    do {
        auto action = app.undoStack.back();
        app.undoStack.pop_back();

        if (action.type == App::EditAction::Replace) {
            app.editorText.replace(action.position, action.replacement.size(), action.text);
        } else if (action.type == App::EditAction::Insert) {
            // Reverse: delete the inserted text
            app.editorText.erase(action.position, action.text.size());
        } else {
            // Reverse: re-insert the deleted text
            app.editorText.insert(action.position, action.text);
        }
        app.editorCursorPos = action.cursorBefore;
        app.redoStack.push_back(action);
    } while (group != 0 && !app.undoStack.empty() && app.undoStack.back().group == group);
    rebuildLineStarts(app);
    app.editorHasSelection = false;
    app.editorDesiredCol = -1;
}

static void editorRedo(App& app);

static void editorEnsureCursorVisible(App& app);

void editorRevealCaret(App& app) {
    rebuildEditorRowMetrics(app);
    editorEnsureCursorVisible(app);
}

void editorUndoRedo(App& app, bool redo) {
    if (redo) editorRedo(app);
    else editorUndo(app);
    scheduleReparse(app);
}

static void editorRedo(App& app) {
    if (app.redoStack.empty()) return;
    app.editorExtraCarets.clear();
    const unsigned group = app.redoStack.back().group;
    do {
        auto action = app.redoStack.back();
        app.redoStack.pop_back();

        if (action.type == App::EditAction::Replace) {
            app.editorText.replace(action.position, action.text.size(), action.replacement);
        } else if (action.type == App::EditAction::Insert) {
            app.editorText.insert(action.position, action.text);
        } else {
            app.editorText.erase(action.position, action.text.size());
        }
        app.editorCursorPos = action.cursorAfter;
        app.undoStack.push_back(action);
    } while (group != 0 && !app.redoStack.empty() && app.redoStack.back().group == group);
    rebuildLineStarts(app);
    app.editorHasSelection = false;
    app.editorDesiredCol = -1;
}

// --- Selection helpers ---

static void editorDeleteSelection(App& app) {
    if (!app.editorHasSelection) return;
    size_t selMin = std::min(app.editorSelStart, app.editorSelEnd);
    size_t selMax = std::max(app.editorSelStart, app.editorSelEnd);
    std::wstring deleted = app.editorText.substr(selMin, selMax - selMin);
    pushUndo(app, App::EditAction::Delete, selMin, deleted, app.editorCursorPos, selMin);
    app.editorText.erase(selMin, selMax - selMin);
    app.editorCursorPos = selMin;
    app.editorHasSelection = false;
    rebuildLineStarts(app);
}

static size_t editorSelMin(const App& app) {
    return std::min(app.editorSelStart, app.editorSelEnd);
}

static size_t editorSelMax(const App& app) {
    return std::max(app.editorSelStart, app.editorSelEnd);
}

static std::wstring editorGetSelectedText(const App& app) {
    if (!app.editorHasSelection) return {};
    size_t mn = editorSelMin(app);
    size_t mx = editorSelMax(app);
    return app.editorText.substr(mn, mx - mn);
}

// Start selection from current cursor if shift held, otherwise clear
static void editorStartOrExtendSelection(App& app, bool shift) {
    if (shift) {
        if (!app.editorHasSelection) {
            app.editorSelStart = app.editorCursorPos;
            app.editorHasSelection = true;
        }
    } else {
        app.editorHasSelection = false;
    }
}

static void editorUpdateSelEnd(App& app) {
    app.editorSelEnd = app.editorCursorPos;
    if (app.editorSelStart == app.editorSelEnd)
        app.editorHasSelection = false;
}

// --- Word boundary helpers ---

static bool isEditorWordChar(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= L'0' && c <= L'9') || c == L'_';
}

// Character class for double-click selection: 0 = whitespace/ASCII
// punctuation, 1 = ASCII word chars, 2 = CJK and any other non-ASCII text.
// Double-click selects a contiguous run of the same class.
static int editorCharClass(wchar_t c) {
    if (isEditorWordChar(c)) return 1;
    if ((unsigned)c > 127 && !iswspace(c)) return 2;
    return 0;
}

static size_t editorWordLeft(const App& app, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && !isEditorWordChar(app.editorText[pos])) pos--;
    while (pos > 0 && isEditorWordChar(app.editorText[pos - 1])) pos--;
    return pos;
}

static size_t editorWordRight(const App& app, size_t pos) {
    size_t len = app.editorText.size();
    while (pos < len && !isEditorWordChar(app.editorText[pos])) pos++;
    while (pos < len && isEditorWordChar(app.editorText[pos])) pos++;
    return pos;
}

// The run of one character class around pos, as double-click selects it
static void editorWordAt(const App& app, size_t pos, size_t& start, size_t& end) {
    start = end = pos;
    int cls = pos < app.editorText.size() ? editorCharClass(app.editorText[pos]) : 0;
    if (cls == 0 && pos > 0) {
        cls = editorCharClass(app.editorText[pos - 1]);
        if (cls != 0) start = pos - 1;
    }
    if (cls == 0) return;
    while (start > 0 && editorCharClass(app.editorText[start - 1]) == cls) start--;
    while (end < app.editorText.size() && editorCharClass(app.editorText[end]) == cls) end++;
}

// --- Extra carets (#251) ---
//
// The primary caret lives in editorCursorPos / editorSelStart / editorSelEnd,
// the others in editorExtraCarets. Multi-caret commands swap each caret into
// the primary slot in turn and run the one-caret code, so every caret types,
// deletes and moves exactly like the single one does.

static App::EditorCaret editorPrimaryCaret(const App& app) {
    App::EditorCaret caret;
    caret.pos = app.editorCursorPos;
    caret.hasSelection = app.editorHasSelection;
    caret.anchor = !caret.hasSelection ? caret.pos
                   : app.editorCursorPos == app.editorSelStart ? app.editorSelEnd
                                                               : app.editorSelStart;
    caret.desiredCol = app.editorDesiredCol;
    caret.desiredX = app.editorDesiredX;
    caret.upstream = app.editorCaretUpstreamPos;
    return caret;
}

static void editorSetPrimaryCaret(App& app, const App::EditorCaret& caret) {
    app.editorCursorPos = caret.pos;
    app.editorSelStart = caret.anchor;
    app.editorSelEnd = caret.pos;
    app.editorHasSelection = caret.hasSelection && caret.anchor != caret.pos;
    app.editorDesiredCol = caret.desiredCol;
    app.editorDesiredX = caret.desiredX;
    app.editorCaretUpstreamPos = caret.upstream;
}

static size_t caretStart(const App::EditorCaret& caret) {
    return caret.hasSelection ? std::min(caret.anchor, caret.pos) : caret.pos;
}

static size_t caretEnd(const App::EditorCaret& caret) {
    return caret.hasSelection ? std::max(caret.anchor, caret.pos) : caret.pos;
}

// Every caret, the primary last
static std::vector<App::EditorCaret> editorAllCarets(const App& app) {
    std::vector<App::EditorCaret> carets = app.editorExtraCarets;
    carets.push_back(editorPrimaryCaret(app));
    return carets;
}

// Writes carets back clamped to the text, off surrogate-pair middles, and
// merged where they meet or overlap; carets[primary] stays the primary
static void editorStoreCarets(App& app, std::vector<App::EditorCaret> carets,
                              size_t primary) {
    const size_t size = app.editorText.size();
    auto settle = [&](size_t pos) {
        pos = std::min(pos, size);
        if (pos > 0 && pos < size && isLowSurrogate(app.editorText[pos]) &&
            isHighSurrogate(app.editorText[pos - 1])) {
            --pos;
        }
        return pos;
    };
    for (auto& caret : carets) {
        caret.pos = settle(caret.pos);
        caret.anchor = settle(caret.anchor);
        if (!caret.hasSelection || caret.anchor == caret.pos) {
            caret.hasSelection = false;
            caret.anchor = caret.pos;
        }
    }
    std::vector<size_t> order(carets.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (caretStart(carets[a]) != caretStart(carets[b]))
            return caretStart(carets[a]) < caretStart(carets[b]);
        return caretEnd(carets[a]) < caretEnd(carets[b]);
    });
    std::vector<App::EditorCaret> merged;
    size_t mergedPrimary = 0;
    for (size_t index : order) {
        const App::EditorCaret& caret = carets[index];
        if (!merged.empty()) {
            App::EditorCaret& last = merged.back();
            bool meet = caretStart(caret) < caretEnd(last) ||
                        (caretStart(caret) == caretEnd(last) &&
                         (!caret.hasSelection || !last.hasSelection));
            if (meet) {
                const size_t start = caretStart(last);
                const size_t end = std::max(caretEnd(last), caretEnd(caret));
                const App::EditorCaret& shape = index == primary ? caret : last;
                const bool forward = !shape.hasSelection || shape.anchor <= shape.pos;
                if (index == primary) {
                    mergedPrimary = merged.size() - 1;
                    last.desiredCol = caret.desiredCol;
                    last.desiredX = caret.desiredX;
                    last.upstream = caret.upstream;
                }
                last.hasSelection = end > start;
                last.anchor = forward ? start : end;
                last.pos = forward ? end : start;
                continue;
            }
        }
        if (index == primary) mergedPrimary = merged.size();
        merged.push_back(caret);
    }
    editorSetPrimaryCaret(app, merged[mergedPrimary]);
    app.editorExtraCarets.clear();
    for (size_t i = 0; i < merged.size(); ++i) {
        if (i != mergedPrimary) app.editorExtraCarets.push_back(merged[i]);
    }
}

// Runs a one-caret edit at every caret, the last caret first: each edit then
// leaves the text before it unchanged, so the carets still to come keep
// their positions. The round is one undo step. edit gets the caret's rank
// in document order.
static void editorEditAtCarets(App& app, const std::function<void(size_t)>& edit) {
    if (app.editorExtraCarets.empty()) {
        edit(0);
        return;
    }
    editorStoreCarets(app, editorAllCarets(app), app.editorExtraCarets.size());
    std::vector<App::EditorCaret> carets = editorAllCarets(app);
    const size_t primary = carets.size() - 1;
    std::vector<size_t> order(carets.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return caretStart(carets[a]) > caretStart(carets[b]);
    });
    UndoGroupScope group(app);
    for (size_t k = 0; k < order.size(); ++k) {
        App::EditorCaret& caret = carets[order[k]];
        const size_t before = app.editorText.size();
        editorSetPrimaryCaret(app, caret);
        edit(order.size() - 1 - k);
        caret = editorPrimaryCaret(app);
        const ptrdiff_t delta = (ptrdiff_t)app.editorText.size() - (ptrdiff_t)before;
        if (delta == 0) continue;
        for (size_t j = 0; j < k; ++j) {
            App::EditorCaret& later = carets[order[j]];
            later.pos = (size_t)std::max<ptrdiff_t>(0, (ptrdiff_t)later.pos + delta);
            later.anchor = (size_t)std::max<ptrdiff_t>(0, (ptrdiff_t)later.anchor + delta);
        }
    }
    editorStoreCarets(app, std::move(carets), primary);
}

// Moves every caret with a one-caret motion, then merges those that meet
static void editorMoveAtCarets(App& app, const std::function<void()>& move) {
    if (app.editorExtraCarets.empty()) {
        move();
        return;
    }
    std::vector<App::EditorCaret> carets = editorAllCarets(app);
    const size_t primary = carets.size() - 1;
    for (auto& caret : carets) {
        editorSetPrimaryCaret(app, caret);
        move();
        caret = editorPrimaryCaret(app);
    }
    editorStoreCarets(app, std::move(carets), primary);
}

// Line-start edits (indent, outdent) would run twice on a line holding two
// carets; keep the first caret of each line
static void editorOneCaretPerLine(App& app) {
    if (app.editorExtraCarets.empty()) return;
    const std::vector<App::EditorCaret> carets = editorAllCarets(app);
    // The primary claims its line first; extras keep the lines still free
    std::vector<App::EditorCaret> kept{carets.back()};
    std::vector<size_t> lines{getLineFromPos(app, carets.back().pos)};
    for (size_t i = 0; i + 1 < carets.size(); ++i) {
        const size_t line = getLineFromPos(app, carets[i].pos);
        if (std::find(lines.begin(), lines.end(), line) != lines.end()) continue;
        lines.push_back(line);
        kept.push_back(carets[i]);
    }
    editorStoreCarets(app, std::move(kept), 0);
}

void editorClearExtraCarets(App& app) {
    app.editorExtraCarets.clear();
}

// Alt+click: a caret where there was none, or none where there was one
// (the last caret always stays). The new caret becomes the primary.
static void editorToggleCaretAt(App& app, size_t pos) {
    std::vector<App::EditorCaret> carets = editorAllCarets(app);
    for (size_t i = 0; i < carets.size(); ++i) {
        if (carets[i].hasSelection || carets[i].pos != pos) continue;
        if (carets.size() == 1) return;
        carets.erase(carets.begin() + (ptrdiff_t)i);
        const size_t primary = carets.size() - 1;
        editorStoreCarets(app, std::move(carets), primary);
        return;
    }
    App::EditorCaret added;
    added.pos = added.anchor = pos;
    carets.push_back(added);
    const size_t primary = carets.size() - 1;
    editorStoreCarets(app, std::move(carets), primary);
}

// Ctrl+Alt+Up / Ctrl+Alt+Down: a caret on the line above the top caret or
// below the bottom one, at the same column (kept across short lines). It
// becomes the primary, so the view follows it.
static void editorAddCaretVertical(App& app, bool down) {
    std::vector<App::EditorCaret> carets = editorAllCarets(app);
    const App::EditorCaret* edge = &carets.back();
    for (const auto& caret : carets) {
        if (down ? caret.pos > edge->pos : caret.pos < edge->pos) edge = &caret;
    }
    const size_t line = getLineFromPos(app, edge->pos);
    if (down ? line + 1 >= app.editorLineStarts.size() : line == 0) return;
    // In wrap mode desiredCol is only a flag beside desiredX, never a column
    const bool wrap = editorWrapOn(app);
    const size_t column = !wrap && edge->desiredCol >= 0
                              ? (size_t)edge->desiredCol
                              : edge->pos - app.editorLineStarts[line];
    const size_t target = down ? line + 1 : line - 1;
    App::EditorCaret added;
    added.pos = added.anchor =
        app.editorLineStarts[target] + std::min(column, getLineLength(app, target));
    added.desiredCol = wrap ? -1 : (int)column;
    carets.push_back(added);
    const size_t primary = carets.size() - 1;
    editorStoreCarets(app, std::move(carets), primary);
}

// Ctrl+D: select the word at the caret; with a selection, add the next
// occurrence of its text (wrapping at the end) as another selected caret
static void editorSelectNextOccurrence(App& app) {
    if (!app.editorHasSelection) {
        size_t start = 0, end = 0;
        editorWordAt(app, app.editorCursorPos, start, end);
        if (start == end) return;
        app.editorSelStart = start;
        app.editorSelEnd = end;
        app.editorCursorPos = end;
        app.editorHasSelection = true;
        app.editorDesiredCol = -1;
        return;
    }
    const std::wstring needle = app.editorText.substr(
        std::min(app.editorSelStart, app.editorSelEnd),
        std::max(app.editorSelStart, app.editorSelEnd) - std::min(app.editorSelStart, app.editorSelEnd));
    std::vector<App::EditorCaret> carets = editorAllCarets(app);
    size_t from = 0;
    for (const auto& caret : carets) from = std::max(from, caretEnd(caret));
    auto taken = [&](size_t at) {
        for (const auto& caret : carets) {
            if (caretStart(caret) < at + needle.size() && at < caretEnd(caret)) return true;
        }
        return false;
    };
    size_t found = app.editorText.find(needle, from);
    if (found == std::wstring::npos || taken(found)) {
        found = app.editorText.find(needle);
        while (found != std::wstring::npos && taken(found)) {
            found = app.editorText.find(needle, found + 1);
        }
    }
    if (found == std::wstring::npos) return;
    App::EditorCaret added;
    added.anchor = found;
    added.pos = found + needle.size();
    added.hasSelection = true;
    carets.push_back(added);
    const size_t primary = carets.size() - 1;
    editorStoreCarets(app, std::move(carets), primary);
}

// Alt+Up / Alt+Down: each caret's line, or every line its selection
// touches, trades places with the line above or below, carets riding
// along. Neighbouring blocks move together; nothing moves when one block
// is already at the edge. One undo step (#251).
static bool editorMoveLines(App& app, bool down) {
    std::vector<App::EditorCaret> carets = editorAllCarets(app);
    struct Block { size_t first, last; };
    std::vector<Block> blocks;
    for (const auto& caret : carets) {
        const size_t start = caretStart(caret), end = caretEnd(caret);
        Block block{getLineFromPos(app, start), getLineFromPos(app, end)};
        // A selection ending at a line start does not take that line along
        if (end > start && block.last > block.first &&
            end == app.editorLineStarts[block.last]) {
            block.last--;
        }
        blocks.push_back(block);
    }
    std::sort(blocks.begin(), blocks.end(),
              [](const Block& a, const Block& b) { return a.first < b.first; });
    std::vector<Block> merged;
    for (const Block& block : blocks) {
        if (!merged.empty() && block.first <= merged.back().last + 1) {
            merged.back().last = std::max(merged.back().last, block.last);
        } else {
            merged.push_back(block);
        }
    }
    const size_t lineCount = app.editorLineStarts.size();
    if (down ? merged.back().last + 1 >= lineCount : merged.front().first == 0) return false;

    UndoGroupScope group(app);
    const size_t primary = carets.size() - 1;
    const size_t cursorBefore = app.editorCursorPos;
    for (const Block& block : merged) {
        // Swapping equal-length text keeps every other block's offsets
        const size_t blockStart = app.editorLineStarts[block.first];
        const size_t blockEnd = getLineEnd(app, block.last);
        const size_t other = down ? block.last + 1 : block.first - 1;
        const size_t otherStart = app.editorLineStarts[other];
        const size_t otherEnd = getLineEnd(app, other);
        const std::wstring blockText = app.editorText.substr(blockStart, blockEnd - blockStart);
        const std::wstring otherText = app.editorText.substr(otherStart, otherEnd - otherStart);
        const size_t regionStart = down ? blockStart : otherStart;
        const size_t regionEnd = down ? otherEnd : blockEnd;
        const std::wstring removed = app.editorText.substr(regionStart, regionEnd - regionStart);
        const std::wstring inserted = down ? otherText + L"\n" + blockText
                                           : blockText + L"\n" + otherText;
        const ptrdiff_t shift = down ? (ptrdiff_t)otherText.size() + 1
                                     : -((ptrdiff_t)otherText.size() + 1);
        for (auto& caret : carets) {
            if (caret.pos >= blockStart && caret.pos <= blockEnd)
                caret.pos = (size_t)((ptrdiff_t)caret.pos + shift);
            if (caret.anchor >= blockStart && caret.anchor <= blockEnd)
                caret.anchor = (size_t)((ptrdiff_t)caret.anchor + shift);
        }
        pushReplaceUndo(app, regionStart, removed, inserted, cursorBefore,
                        carets[primary].pos);
        app.editorText.replace(regionStart, removed.size(), inserted);
    }
    rebuildLineStarts(app);
    for (auto& caret : carets) caret.desiredCol = -1;
    editorStoreCarets(app, std::move(carets), primary);
    return true;
}

// --- Clipboard ---

// Another program can hold the clipboard open for a moment, as Remote
// Desktop's clipboard sync and clipboard history do after each change: the
// copy waits for it briefly, and held for longer, copies nothing and says
// so. Returns whether the text is on the clipboard: Cut deletes only what
// it copied.
static bool editorCopyToClipboard(App& app, HWND hwnd, const std::wstring& text) {
    bool held = false;
    if (copyToClipboard(hwnd, text, &held)) return true;
    if (held) signalClipboardBusy(app);
    return false;
}

static std::wstring editorGetClipboard(HWND hwnd) {
    if (!openClipboard(hwnd)) return {};
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return {}; }
    wchar_t* ptr = (wchar_t*)GlobalLock(hData);
    std::wstring text = ptr ? ptr : L"";
    GlobalUnlock(hData);
    CloseClipboard();
    // Normalize \r\n to \n
    std::wstring result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == L'\r') {
            result += L'\n';
            if (i + 1 < text.size() && text[i + 1] == L'\n') i++;
        } else {
            result += text[i];
        }
    }
    return result;
}

// --- Scroll helpers ---

// Space the source keeps clear above the window bottom: at least a line,
// and enough for the caret line to sit above the Read pill (#250)
static float editorBottomClearance(const App& app, float lineHeight) {
    return std::max(lineHeight, readPillClearance(app));
}

// Top of the row holding a text position, in the source's scroll
// coordinates: with word wrap a logical line spans several rows (#260)
static float editorPosRowY(App& app, size_t pos, bool upstream) {
    size_t line = getLineFromPos(app, pos);
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float padding = dpi(app, 8.0f);
    if (!editorWrapOn(app)) return padding + line * lineHeight;
    ensureEditorRowMetrics(app);
    IDWriteTextLayout* layout = createEditorLineLayout(
        app, app.editorLineStarts[line], getLineLength(app, line));
    float cx = 0, cy = 0;
    editorCaretXY(layout, pos - app.editorLineStarts[line], cx, cy, upstream);
    if (layout) layout->Release();
    size_t rowStart = (line < app.editorRowStarts.size()) ? app.editorRowStarts[line] : line;
    return padding + rowStart * lineHeight + cy;
}

static void editorEnsureCursorVisible(App& app) {
    if (app.editorLineStarts.empty()) return;
    size_t line = getLineFromPos(app, app.editorCursorPos);
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float cursorY = editorPosRowY(app, app.editorCursorPos,
                                  app.editorCaretUpstreamPos == app.editorCursorPos);

    float viewportH = (float)app.height - chromeTopHeight(app);
    float bottomClearance = editorBottomClearance(app, lineHeight);
    if (cursorY < app.editorScrollY + lineHeight) {
        app.editorScrollY = std::max(0.0f, cursorY - lineHeight);
    }
    if (cursorY + lineHeight > app.editorScrollY + viewportH - bottomClearance) {
        app.editorScrollY = cursorY + lineHeight + bottomClearance - viewportH;
    }
    app.editorScrollY = std::max(0.0f, app.editorScrollY);

    // Horizontal caret-follow: long unwrapped lines scroll sideways instead
    // of disappearing under the pane separator (#77)
    if (editorWrapOn(app)) {
        app.editorScrollX = 0.0f;
    } else {
        IDWriteTextLayout* layout = createEditorLineLayout(
            app, app.editorLineStarts[line], getLineLength(app, line));
        float cx = 0, cy = 0;
        editorCaretXY(layout, app.editorCursorPos - app.editorLineStarts[line], cx, cy,
            app.editorCaretUpstreamPos == app.editorCursorPos);
        if (layout) layout->Release();
        float viewW = editorTextMaxWidth(app) - dpi(app, 12.0f);  // caret + scrollbar slack
        float margin = dpi(app, 8.0f);
        if (cx < app.editorScrollX + margin) {
            app.editorScrollX = std::max(0.0f, cx - margin);
        } else if (cx > app.editorScrollX + viewW) {
            app.editorScrollX = cx - viewW;
        }
    }
}

// --- Editor search ---

void performEditorSearch(App& app) {
    app.editorSearchMatches.clear();
    app.editorSearchCurrentIndex = 0;

    if (app.searchQuery.empty() || app.editorText.empty()) return;

    // Build lowercase versions for case-insensitive search
    std::wstring textLower;
    textLower.resize(app.editorText.size());
    for (size_t i = 0; i < app.editorText.size(); i++) {
        textLower[i] = towlower(app.editorText[i]);
    }

    std::wstring queryLower;
    queryLower.resize(app.searchQuery.size());
    for (size_t i = 0; i < app.searchQuery.size(); i++) {
        queryLower[i] = towlower(app.searchQuery[i]);
    }

    app.editorSearchMatches.reserve(64);

    size_t pos = 0;
    while ((pos = textLower.find(queryLower, pos)) != std::wstring::npos) {
        app.editorSearchMatches.push_back({pos, app.searchQuery.length()});
        pos += app.searchQuery.length();
    }
}

// --- Find & replace (#121) ---

static void scheduleReparse(App& app);  // defined below with the debounce

void editorReplaceCurrent(App& app, HWND hwnd) {
    if (!app.editMode || app.searchQuery.empty()) return;
    if (app.editorSearchMatches.empty()) return;
    app.editorExtraCarets.clear();  // find and replace work at one caret
    int idx = app.editorSearchCurrentIndex;
    if (idx < 0 || idx >= (int)app.editorSearchMatches.size()) idx = 0;
    const App::EditorSearchMatch m = app.editorSearchMatches[idx];

    std::wstring removed = app.editorText.substr(m.startPos, m.length);
    pushUndo(app, App::EditAction::Delete, m.startPos, removed,
             app.editorCursorPos, m.startPos);
    app.editorText.erase(m.startPos, m.length);
    if (!app.replaceText.empty()) {
        app.editorText.insert(m.startPos, app.replaceText);
        pushUndo(app, App::EditAction::Insert, m.startPos, app.replaceText,
                 m.startPos, m.startPos + app.replaceText.size());
    }
    app.editorCursorPos = m.startPos + app.replaceText.size();
    app.editorHasSelection = false;
    rebuildLineStarts(app);
    scheduleReparse(app);

    // Land on the next remaining match past the replacement
    performEditorSearch(app);
    if (!app.editorSearchMatches.empty()) {
        app.editorSearchCurrentIndex = 0;
        for (size_t i = 0; i < app.editorSearchMatches.size(); i++) {
            if (app.editorSearchMatches[i].startPos >= app.editorCursorPos) {
                app.editorSearchCurrentIndex = (int)i;
                break;
            }
        }
        scrollEditorToMatch(app);
    }
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void editorReplaceAll(App& app, HWND hwnd) {
    if (!app.editMode || app.searchQuery.empty()) return;
    performEditorSearch(app);
    if (app.editorSearchMatches.empty()) return;
    app.editorExtraCarets.clear();

    // Back to front so earlier offsets stay valid; each hunk is its own
    // undo entry, so Ctrl+Z walks the replacement back out
    for (int i = (int)app.editorSearchMatches.size() - 1; i >= 0; i--) {
        const App::EditorSearchMatch m = app.editorSearchMatches[i];
        std::wstring removed = app.editorText.substr(m.startPos, m.length);
        pushUndo(app, App::EditAction::Delete, m.startPos, removed,
                 app.editorCursorPos, m.startPos);
        app.editorText.erase(m.startPos, m.length);
        if (!app.replaceText.empty()) {
            app.editorText.insert(m.startPos, app.replaceText);
            pushUndo(app, App::EditAction::Insert, m.startPos, app.replaceText,
                     m.startPos, m.startPos + app.replaceText.size());
        }
        app.editorCursorPos = m.startPos + app.replaceText.size();
    }
    app.editorHasSelection = false;
    rebuildLineStarts(app);
    scheduleReparse(app);
    performEditorSearch(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void scrollEditorToMatch(App& app) {
    if (app.editorSearchMatches.empty() || app.editorSearchCurrentIndex < 0 ||
        app.editorSearchCurrentIndex >= (int)app.editorSearchMatches.size()) return;
    app.editorExtraCarets.clear();

    const auto& match = app.editorSearchMatches[app.editorSearchCurrentIndex];

    // Move cursor to match position
    app.editorCursorPos = match.startPos;
    app.editorCaretUpstreamPos = std::wstring::npos;
    app.editorDesiredCol = -1;

    // Center the match's row below the strip. With word wrap that row is
    // counted in wrapped rows, not lines, or a match deep in a long line
    // stays below the window (#260).
    if (app.editorLineStarts.empty()) return;
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float matchY = editorPosRowY(app, match.startPos, false);
    float viewportH = (float)app.height - chromeTopHeight(app);
    app.editorScrollY = matchY + lineHeight * 0.5f - viewportH * 0.5f;
    float maxScroll = std::max(0.0f, app.editorContentHeight - app.height);
    app.editorScrollY = std::max(0.0f, std::min(app.editorScrollY, maxScroll));
    // Without wrap a match far into a long line also needs the source to
    // scroll sideways; the caret now sits on it
    editorEnsureCursorVisible(app);
}

// --- Debounced reparse ---

// --- Word and character count (#240) ---

// Counts what a reader sees: the parsed Markdown's prose, link text and
// inline code. Plain-text and diagram documents count as they stand.
static TextCounts editorCountSource(App& app, const std::wstring& source) {
    if (isPlainTextDocumentPath(app.currentFile) || isMermaidDocumentPath(app.currentFile) ||
        isPlantUmlDocumentPath(app.currentFile)) {
        return countPlainText(source);
    }
    auto result = parseDocument(app.parser, toUtf8(source), app.currentFile);
    std::wstring visible;
    if (result.success) appendCountableText(result.root, visible);
    return countPlainText(visible);
}

// Document totals from a tree the reparse just built, or by parsing the
// buffer when the preview pane is hidden
static void editorRefreshDocCounts(App& app, const ElementPtr& parsed) {
    if (parsed && !isPlainTextDocumentPath(app.currentFile) &&
        !isMermaidDocumentPath(app.currentFile) && !isPlantUmlDocumentPath(app.currentFile)) {
        std::wstring visible;
        appendCountableText(parsed, visible);
        app.editorDocCounts = countPlainText(visible);
    } else {
        app.editorDocCounts = editorCountSource(app, app.editorText);
    }
    app.editorDocCountsStale = false;
    app.editorSelCountsKey.clear();
}

static void scheduleReparse(App& app) {
    if (!app.editorDirty) {
        app.editorDirty = true;
        // Update window title with dirty marker
        std::wstring wpath = toWide(app.currentFile);
        size_t lastSep = wpath.find_last_of(L"\\/");
        std::wstring fname = (lastSep != std::wstring::npos) ? wpath.substr(lastSep + 1) : wpath;
        if (fname.empty()) fname = tr(app, "title.untitled");  // quick note
        std::wstring title = L"Tinta - * " + fname;
        SetWindowTextW(app.hwnd, title.c_str());
    }
    // No preview pane and no word count — nothing to keep in sync until
    // one of them is shown again
    if (!app.editorShowPreview && !app.showWordCount && !editorLiveOn(app)) return;
    // Debounce: coalesce rapid typing into one reparse per pause. WM_TIMER
    // calls editorReparse, which kills the timer. 300ms so brief pauses
    // mid-typing (common with IME input) don't trigger a full preview
    // relayout on every committed character.
    SetTimer(app.hwnd, TIMER_EDITOR_REPARSE, 300, nullptr);
}

// External edits (table cell editor #148) want the dirty bookkeeping AND
// an immediate reparse so their source offsets stay fresh
void editorMarkDirtyAndReparse(App& app) {
    scheduleReparse(app);
    editorReparse(app);
}

void editorReparse(App& app, bool force) {
    KillTimer(app.hwnd, TIMER_EDITOR_REPARSE);
    if (!app.editMode) return;
    std::string utf8 = toUtf8(app.editorText);
    if (!isPlainTextDocumentPath(app.currentFile) &&
        !isMermaidDocumentPath(app.currentFile) &&
        !isPlantUmlDocumentPath(app.currentFile)) {
        auto metadata = fm::parse(utf8);
        if (metadata.present) observeFrontmatter(app, metadata.properties);
    }
    if (!editorPreviewVisible(app) && !force) {
        if (app.showWordCount) editorRefreshDocCounts(app, nullptr);
        return;
    }

    // Build line-to-byte-offset mapping for scroll sync
    app.editorLineByteOffsets.clear();
    app.editorLineByteOffsets.push_back(0);
    for (size_t i = 0; i < utf8.size(); i++) {
        if (utf8[i] == '\n') {
            app.editorLineByteOffsets.push_back(i + 1);
        }
    }

    auto result = parseDocument(app.parser, utf8, app.currentFile);
    if (result.success) {
        app.root = result.root;
        app.parseTimeUs = result.parseTimeUs;
        app.layoutDirty = true;
        liveTextParsed(app);
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    if (app.showWordCount) editorRefreshDocCounts(app, result.success ? result.root : nullptr);
}

// --- Mode transitions ---

// New-user hints (#245): the first few edit sessions show the Esc hint
// and fade the Read button in; later sessions keep both quiet
constexpr int kEditHintSessions = 3;
constexpr double kReadButtonIntroSeconds = 1.6;
constexpr float kReadButtonFadeIn = 0.15f;
constexpr float kReadButtonFadeOut = 0.4f;

static double hintClockSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static void editorReadingButtonIntro(App& app) {
    app.readButtonIntroUntil = hintClockSeconds() + kReadButtonIntroSeconds;
    app.readButtonLastTick = hintClockSeconds();
    startNotificationTimer(app);
}

static void editHintSessionBegin(App& app) {
    app.editHintSession = app.editHintsShown < kEditHintSessions;
    if (!app.editHintSession) return;
    app.editHintsShown++;
    signalHintKey(app, SIGI_INFO, "toast.exit_edit_hint");
    editorReadingButtonIntro(app);
}

// intro = false when a parked edit buffer comes back on a tab switch: the
// session goes on, it does not start over
static void enterEditModeWithContent(App& app, const std::string& content,
                                     bool intro = true) {
    app.editorReadingPreview = false;
    app.editorRawDocument = isPlainTextDocumentPath(app.currentFile) || isMermaidDocumentPath(app.currentFile) ||
                            isPlantUmlDocumentPath(app.currentFile);
    // The unified editor owns the whole layout: side panels close on
    // entry, pinned or not (#156) - the pin survives for the next open
    app.showToc = false;
    app.tocAnimation = 0.0f;
    app.tocFilter.clear();
    app.showSearchPanel = false;  // the search results panel too (#246)
    app.searchPanelRestoreToc = false;
    app.showFolderBrowser = false;
    app.folderBrowserAnimation = 0.0f;
    app.folderBrowserEditingPath = false;
    app.folderBrowserNaming = 0;
    tableEditCancel(app);  // no stale cell editor from a previous buffer

    // A new buffer starts with one caret and a fresh count (#251, #240)
    app.editorExtraCarets.clear();
    app.editorDocCountsStale = true;
    app.editorSelCountsKey.clear();
    // A byte order mark belongs to the file, not the text: nothing typed
    // can land ahead of it, and writeEditorFile puts it back. The walks
    // below still count its bytes, as the reading view's offsets do.
    app.editorBom = startsWithUtf8Bom(content);
    app.editorText = fromUtf8(content.substr(app.editorBom ? UTF8_BOM.size() : 0));
    // Normalize \r\n to \n
    std::wstring normalized;
    normalized.reserve(app.editorText.size());
    for (size_t i = 0; i < app.editorText.size(); i++) {
        if (app.editorText[i] == L'\r') {
            normalized += L'\n';
            if (i + 1 < app.editorText.size() && app.editorText[i + 1] == L'\n') i++;
        } else {
            normalized += app.editorText[i];
        }
    }
    // Map the current reading position to a source line so the editor opens
    // where the user was, not at the top (#77). Anchors carry UTF-8 offsets
    // into the text the reading view parsed, which the viewer reads in text
    // mode: a CRLF there is one byte. The raw content is walked the same way
    // (as toggleTaskOnDisk does), or a CRLF file opened the editor further
    // above the reading position the further down it was (#267).
    size_t targetLine = 0;
    if (app.scrollY > 1.0f && !app.scrollAnchors.empty()) {
        size_t lo = 0, hi = app.scrollAnchors.size();
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (app.scrollAnchors[mid].renderedY <= app.scrollY) lo = mid;
            else hi = mid;
        }
        const size_t target = app.scrollAnchors[lo].sourceOffset;
        size_t textPos = 0;  // position in the CR-stripped text
        for (size_t i = 0; i < content.size() && textPos < target; i++) {
            if (content[i] == '\r' && i + 1 < content.size() && content[i + 1] == '\n') continue;
            if (content[i] == '\n') targetLine++;
            textPos++;
        }
    }

    // Build the line→byte-offset table against the raw content so the
    // preview scroll sync works immediately on entry: the anchors from the
    // entry parse carry offsets into this exact byte stream (\r\n included),
    // and until now the table only existed after the first reparse — which
    // is why the panes stopped responding to each other right after ':'
    app.editorLineByteOffsets.clear();
    app.editorLineByteOffsets.push_back(0);
    for (size_t i = 0; i < content.size(); i++) {
        if (content[i] == '\n') app.editorLineByteOffsets.push_back(i + 1);
    }

    app.editorText = std::move(normalized);

    rebuildLineStarts(app);
    app.editorCursorPos = 0;
    app.editorDesiredCol = -1;
    app.editorScrollY = 0;
    app.editorScrollX = 0;
    app.editorHasSelection = false;
    app.editorDirty = false;
    app.undoStack.clear();
    app.redoStack.clear();
    app.editorSearchMatches.clear();
    app.editorSearchCurrentIndex = 0;
    app.editMode = true;
    app.escPressedOnce = false;
    app.confirmExitPending = false;
    // The tool rail slides in fresh with every edit-mode entry
    app.editRailAnim = 0.0f;
    app.editRailHover = 0;

    // Resume at the reading position rather than the top (#77)
    if (targetLine > 0 && targetLine < app.editorLineStarts.size()) {
        app.editorCursorPos = app.editorLineStarts[targetLine];
        float lineHeight = app.editorTextFormat
            ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
        float row = (float)targetLine;
        if (editorWrapOn(app)) {
            ensureEditorRowMetrics(app);
            if (targetLine < app.editorRowStarts.size()) {
                row = (float)app.editorRowStarts[targetLine];
            }
        }
        app.editorScrollY = std::max(0.0f, row * lineHeight);
    }

    // The file-watch tick keeps running: its reload path guards editMode
    // itself, and the per-tab deleted-file sweep needs the heartbeat

    // The Read button starts hidden; the first few sessions teach the
    // exits, then stay quiet (#245)
    app.readButtonAlpha = 0.0f;
    app.readButtonHover = false;
    app.readButtonIntroUntil = 0.0;
    app.editSeamHover = false;
    if (intro) editHintSessionBegin(app);
    updateBlinkTimer(app);

    // Force layout at new width
    app.focusMermaidOnNextLayout = isMermaidDocumentPath(app.currentFile) ||
                                   isPlantUmlDocumentPath(app.currentFile);
    app.layoutDirty = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void editorScrollToSourceOffset(App& app, size_t byteOffset) {
    auto next = std::upper_bound(app.editorLineByteOffsets.begin(),
                                app.editorLineByteOffsets.end(), byteOffset);
    size_t line = next == app.editorLineByteOffsets.begin() ? 0 :
                  static_cast<size_t>(next - app.editorLineByteOffsets.begin() - 1);
    if (line >= app.editorLineStarts.size()) return;
    app.editorCursorPos = app.editorLineStarts[line];
    app.editorCaretUpstreamPos = std::wstring::npos;
    app.editorDesiredCol = -1;
    app.editorHasSelection = false;
    float row = static_cast<float>(line);
    if (editorWrapOn(app)) {
        ensureEditorRowMetrics(app);
        row = static_cast<float>(app.editorRowStarts[line]);
    }
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    app.editorScrollY = std::max(0.0f, row * lineHeight);
    app.editorScrollX = 0;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void restoreEditBuffer(App& app, const std::wstring& text, bool dirty,
                       float scrollY, size_t cursor, bool bom) {
    std::string content = toUtf8(text);
    if (bom) content.insert(0, UTF8_BOM);
    enterEditModeWithContent(app, content, false);
    app.editorDirty = dirty;
    app.editorCursorPos = std::min(cursor, app.editorText.size());
    app.editorScrollY = std::max(0.0f, scrollY);
    // Opening another file parks the unsaved source; restore its preview
    // from that source too, rather than the last saved file or empty note.
    editorReparse(app);
    updateWindowTitle(app);
}

void enterEditMode(App& app) {
    if (app.editMode) { setEditorReadingPreview(app, false); return; }
    // A sheet saves before its own text opens in the editor
    if (sheetActive(app)) sheetLeave(app);
    if (app.currentFile.empty()) {
        if (app.startPageEmbeddedOpen) {
            // Learn documents have no backing file. Edit an untitled copy;
            // the first save asks for a path and the embedded original stays intact.
            const std::string content = app.sourceText;
            app.startPageEmbeddedOpen = false;
            enterEditModeWithContent(app, content);
            app.editorDirty = true;
            editorReparse(app, true);
            updateWindowTitle(app);
            return;
        }
        // Show brief "No file loaded" notification
        signalPushKey(app, SIG_WARN, SIGI_WARNING, "toast.no_file");
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }

    // Load raw file content
    std::wstring widePath = toWide(app.currentFile);
    std::ifstream file(widePath, std::ios::binary);
    if (!file) {
        // The viewer still owns the last readable source of a deleted
        // file. Let the user recover it with Save or Save As.
        if (!app.keepDeletedTabs || GetFileAttributesW(widePath.c_str()) != INVALID_FILE_ATTRIBUTES) return;
        enterEditModeWithContent(app, app.sourceText);
        app.editorDirty = true;
        updateWindowTitle(app);
        return;
    }

    std::stringstream buf;
    buf << file.rdbuf();
    enterEditModeWithContent(app, buf.str());
}

void enterQuickNoteMode(App& app) {
    app.currentFile.clear();
    app.startPageEmbeddedOpen = false;  // the note replaces any Learn doc
    enterEditModeWithContent(app, std::string());
    updateWindowTitle(app);  // "Tinta - Untitled" until the first save
}

// Draft recovery: a crash leftover reopens as a dirty edit tab, pointed
// back at its original file when it had one
void enterRecoveredDraft(App& app, HWND hwnd, const std::string& content,
                         const std::string& origPath) {
    // The draft of a file that is open already, as a restored session tab
    // usually is (#252), takes over that tab while it has no unsaved edits
    // of its own, instead of opening a second tab for the same file
    if (!origPath.empty()) {
        for (size_t i = 0; i < app.tabs.size(); i++) {
            const bool active = (int)i == app.activeTab;
            const std::string& path = active ? app.currentFile : app.tabs[i].path;
            const bool dirty = active ? (app.editMode && app.editorDirty)
                                      : (app.tabs[i].editMode && app.tabs[i].editorDirty);
            if (dirty || _stricmp(path.c_str(), origPath.c_str()) != 0) continue;
            tabActivate(app, hwnd, (int)i);
            if (app.activeTab != (int)i) continue;
            enterEditModeWithContent(app, content);
            app.editorDirty = true;
            updateWindowTitle(app);
            return;
        }
    }
    tabOpenQuickNote(app, hwnd);
    app.currentFile = origPath;
    enterEditModeWithContent(app, content);
    app.editorDirty = true;
    if (!origPath.empty()) {
        App::DocTab& tab = app.tabs[app.activeTab];
        tab.path = origPath;
        size_t slash = origPath.find_last_of("/\\");
        tab.title = toWide(slash == std::string::npos
                               ? origPath
                               : origPath.substr(slash + 1));
    }
    updateWindowTitle(app);
}

void exitEditMode(App& app) {
    // An open table cell commits before the mode question is asked (#148)
    tableEditCommit(app);
    if (app.editorDirty) {
        // Unsaved changes: a modal dialog with clickable buttons. The
        // timestamp opens a grace window so smashed Esc presses land on a
        // stable dialog instead of instantly cancelling it.
        app.confirmExitPending = true;
        app.confirmExitOpenedAt = std::chrono::steady_clock::now();
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }

    app.editMode = false;
    app.editorReadingPreview = false;
    signalFadeHints(app);  // the mode's hints go with it (#245)
    app.clearEditorLineLayoutCache();
    app.editorText.clear();
    app.editorLineStarts.clear();
    ++app.editorTextRevision;
    app.editorExtraCarets.clear();
    app.undoStack.clear();
    app.redoStack.clear();
    app.editorSearchMatches.clear();
    app.editorSearchCurrentIndex = 0;
    // Close search if open
    if (app.showSearch) closeSearchInput(app);
    KillTimer(app.hwnd, TIMER_EDITOR_REPARSE);
    updateBlinkTimer(app);

    // Re-enable file watch
    updateFileWriteTime(app);
    SetTimer(app.hwnd, 1, 500, nullptr); // TIMER_FILE_WATCH = 1

    // Reload file to pick up saved changes. A discarded untitled quick
    // note has no file — reset the viewer to an empty document instead of
    // leaving the discarded text rendered.
    std::wstring widePath = toWide(app.currentFile);
    std::ifstream file(widePath);
    if (app.currentFile.empty()) {
        auto result = parseDocument(app.parser, std::string(), app.currentFile);
        if (result.success) {
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
        }
        // The empty viewer is the start page now; the tab label follows
        if (!app.tabs.empty() && app.activeTab >= 0 &&
            app.activeTab < (int)app.tabs.size()) {
            app.tabs[app.activeTab].title = L"Tinta";
        }
    } else if (file) {
        std::stringstream buf;
        buf << file.rdbuf();
        auto result = parseDocument(app.parser, buf.str(), app.currentFile);
        if (result.success) {
            app.sourceText = buf.str();
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
        }
    } else {
        // Discard edits back to the last viewed source, even if the file
        // disappeared. This source lives only in the current process.
        auto result = parseDocument(app.parser, app.sourceText, app.currentFile);
        if (result.success) {
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
        }
    }

    // Update window title (remove dirty marker)
    updateWindowTitle(app);

    app.focusMermaidOnNextLayout = isMermaidDocumentPath(app.currentFile) ||
                                   isPlantUmlDocumentPath(app.currentFile);
    app.layoutDirty = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void setEditorReadingPreview(App& app, bool reading) {
    if (!app.editMode || reading == app.editorReadingPreview) return;
    tableEditCommit(app);
    closeEditCtxMenu(app);
    if (app.showSearch) closeSearchInput(app);
    app.editorReadingPreview = reading;
    app.editorSelecting = app.selecting = app.hasSelection = false;
    app.escPressedOnce = false;
    // A hint session shows the button that leads back (#245)
    if (app.editHintSession) editorReadingButtonIntro(app);
    editorReparse(app, true);
    app.layoutDirty = true;
    updateBlinkTimer(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// The pill names its shortcut, so it grows to fit its label: Read carries
// Ctrl+Shift+E since Esc leaves edit mode again (#242). It never sits over
// the text for good (#245): see editorReadingButtonShown.
D2D1_RECT_F editorReadingButtonRect(const App& app) {
    float width = dpi(app, 132);
    if (app.dwriteFactory && app.codeFormat) {
        const wchar_t* label = tr(app, app.editorReadingPreview ? "editor.resume" : "editor.read");
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(app.dwriteFactory->CreateTextLayout(label, static_cast<UINT32>(wcslen(label)),
                app.codeFormat, 4096.0f, 256.0f, layout.GetAddressOf()))) {
            useUiFontFallback(app, layout.Get());
            if (SUCCEEDED(layout->GetMetrics(&metrics)))
                width = std::max(width, metrics.widthIncludingTrailingWhitespace + dpi(app, 28));
        }
    }
    return {dpi(app, 56), static_cast<float>(app.height)-dpi(app, kReadPillLift + kReadPillHeight),
            dpi(app, 56) + width, static_cast<float>(app.height)-dpi(app, kReadPillLift)};
}

// The insert menu may open over the button's corner: the menu wins
static bool readButtonWanted(const App& app, double now) {
    return !app.editCtxOpen &&
           (app.readButtonHover || now < app.readButtonIntroUntil);
}

bool editorReadingButtonShown(const App& app) {
    return app.editMode && !app.editCtxOpen &&
           (app.readButtonHover || app.readButtonAlpha >= 0.5f);
}

bool editorReadingButtonNeedsTicks(const App& app) {
    if (!app.editMode) return false;
    double now = hintClockSeconds();
    // The intro's end needs a tick too, to start the fade-out
    if (now < app.readButtonIntroUntil) return true;
    return readButtonWanted(app, now) ? app.readButtonAlpha < 1.0f
                                      : app.readButtonAlpha > 0.0f;
}

void editorReadingButtonHover(App& app, float x, float y) {
    D2D1_RECT_F r = editorReadingButtonRect(app);
    bool over = app.editMode && !app.editorSelecting && !app.editorTextDrag.armed &&
                !app.draggingSeparator &&
                !app.editCtxOpen &&
                x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    if (over == app.readButtonHover) return;
    app.readButtonHover = over;
    app.readButtonLastTick = hintClockSeconds();
    startNotificationTimer(app);
    if (app.hwnd) {
        if (over) {
            // The fade-out needs to hear the pointer leave the window
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, app.hwnd, 0};
            TrackMouseEvent(&track);
        }
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

void renderEditorReadingButton(App& app) {
    if (!app.editMode || !app.brush || !app.codeFormat) return;
    // Fade toward the wanted state at a pace set by the clock, not the
    // frame rate
    double now = hintClockSeconds();
    float dt = app.readButtonLastTick > 0.0
                   ? (float)std::min(0.25, std::max(0.0, now - app.readButtonLastTick))
                   : 0.0f;
    app.readButtonLastTick = now;
    if (readButtonWanted(app, now)) {
        app.readButtonAlpha = std::min(1.0f, app.readButtonAlpha + dt / kReadButtonFadeIn);
    } else {
        app.readButtonAlpha = std::max(0.0f, app.readButtonAlpha - dt / kReadButtonFadeOut);
    }
    float alpha = app.readButtonAlpha;
    if (alpha <= 0.01f) return;
    auto r=editorReadingButtonRect(app);
    D2D1_COLOR_F fill = app.theme.codeBackground;
    fill.a *= alpha;
    app.brush->SetColor(fill);
    app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(r,dpi(app,5),dpi(app,5)),app.brush);
    D2D1_COLOR_F ink = app.theme.accent;
    ink.a *= alpha;
    app.brush->SetColor(ink);
    auto label=tr(app,app.editorReadingPreview ? "editor.resume" : "editor.read");
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (SUCCEEDED(app.dwriteFactory->CreateTextLayout(label,static_cast<UINT32>(wcslen(label)),app.codeFormat,r.right-r.left,r.bottom-r.top,layout.GetAddressOf()))) {
        useUiFontFallback(app, layout.Get());
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.renderTarget->DrawTextLayout({r.left,r.top},layout.Get(),app.brush);
    }
}

// --- File save ---

// Save As for quick notes, explicit copies and deleted originals. Returns false
// when the user cancels; on success app.currentFile carries the new path
// and the note becomes a normal file-backed session.
static bool promptSaveAsPath(App& app, HWND hwnd) {
    wchar_t path[MAX_PATH] = L"";
    const std::filesystem::path previous(toWide(app.currentFile));
    const std::wstring name = previous.filename().wstring();
    if (!name.empty()) wcsncpy_s(path, name.c_str(), _TRUNCATE);
    const std::wstring directory = previous.parent_path().wstring();
    const DWORD attributes = directory.empty() ? INVALID_FILE_ATTRIBUTES
        : GetFileAttributesW(directory.c_str());
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Markdown (*.md)\0*.md;*.markdown\0"
                      L"Mermaid (*.mmd)\0*.mmd\0"
                      L"PlantUML (*.puml;*.plantuml)\0*.puml;*.plantuml\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        ofn.lpstrInitialDir = directory.c_str();
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"md";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!runSaveFileDialog(ofn)) return false;
    app.currentFile = toUtf8(path);
    return true;
}

// --- Quick-note empty state (design 4a) ---
//
// An untitled buffer with no text yet shows a centered "Open a file"
// button in the preview pane; it disappears at the first typed character.
// The button (or Ctrl+O) opens the classic file picker and the picked
// document takes over this tab.

bool quickNoteEmptyStateActive(const App& app) {
    return app.editMode && app.currentFile.empty() && app.editorText.empty();
}

void openFileDialog(App& app, HWND hwnd) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Documents (*.md;*.mmd;*.smd;*.txt;*.json;*.yaml;...)\0"
                      L"*.md;*.markdown;*.mmd;*.smd;*.canvas;*.txt;*.json;*.yaml;*.yml;*.toml;"
                      L"*.ini;*.csv;*.log;*.puml;*.plantuml\0"
                      L"Sheets (*.smd;*.canvas)\0*.smd;*.canvas\0"
                      L"Markdown / Mermaid (*.md;*.markdown;*.mmd)\0"
                      L"*.md;*.markdown;*.mmd;*.puml;*.plantuml\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_HIDEREADONLY;
    if (!runOpenFileDialog(ofn)) return;

    // Only an empty launcher/note is replaced. A document or unsaved
    // buffer stays in its tab, including when the picked path is open.
    bool replaceEmpty = app.currentFile.empty() && !app.startPageEmbeddedOpen &&
                        (!app.editMode || (app.editorText.empty() && !app.editorDirty));
    tabsInit(app);
    int untitledId = replaceEmpty ? app.tabs[app.activeTab].id : -1;
    tabOpenPath(app, hwnd, toUtf8(path));
    for (int i = 0; i < (int)app.tabs.size(); i++) {
        if (app.tabs[i].id == untitledId) {
            tabCloseIndex(app, hwnd, i);
            break;
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void renderQuickNoteEmptyState(App& app) {
    app.quickNoteButtonRect = {};
    app.quickNoteSheetRect = {};
    if (!quickNoteEmptyStateActive(app) || !app.editorShowPreview) return;
    IDWriteTextFormat* format = app.folderBrowserFormat;
    if (!format || !app.renderTarget || !app.brush || !app.dwriteFactory)
        return;

    float paneX = documentViewportX(app);
    float paneW = documentViewportWidth(app);
    if (paneW < dpi(app, 180.0f)) return;  // pane too narrow to bother

    // Two buttons: Open a file with its Ctrl+O keycap, and New sheet
    const wchar_t* labels[2] = { tr(app, "empty.open_button"),
                                 tr(app, "start.new_sheet") };
    const wchar_t* chip = L"Ctrl+O";

    IDWriteTextLayout* labelLayouts[2] = {};
    IDWriteTextLayout* chipLayout = nullptr;
    for (int i = 0; i < 2; i++)
        app.dwriteFactory->CreateTextLayout(labels[i], (UINT32)wcslen(labels[i]),
                                            format, 2000.0f, 100.0f,
                                            &labelLayouts[i]);
    app.dwriteFactory->CreateTextLayout(chip, (UINT32)wcslen(chip), format,
                                        2000.0f, 100.0f, &chipLayout);
    if (!labelLayouts[0] || !labelLayouts[1] || !chipLayout) {
        for (IDWriteTextLayout* layout : labelLayouts)
            if (layout) layout->Release();
        if (chipLayout) chipLayout->Release();
        return;
    }
    DWRITE_TEXT_METRICS lm[2] = {}, cm{};
    labelLayouts[0]->GetMetrics(&lm[0]);
    labelLayouts[1]->GetMetrics(&lm[1]);
    chipLayout->GetMetrics(&cm);

    // Button geometry: an icon, the label, and for Open its keycap chip;
    // side by side when the pane has room, else one over the other
    float iconW = dpi(app, 20.0f);
    float chipW = cm.width + dpi(app, 16.0f);
    float widths[2] = {
        dpi(app, 18.0f) + iconW + dpi(app, 10.0f) + lm[0].width +
            dpi(app, 12.0f) + chipW + dpi(app, 14.0f),
        dpi(app, 18.0f) + iconW + dpi(app, 10.0f) + lm[1].width +
            dpi(app, 18.0f) };
    float btnH = dpi(app, 44.0f);
    float gap = dpi(app, 10.0f);
    float chromeTop = chromeTopHeight(app);
    float cx = paneX + paneW * 0.5f;
    float cy = chromeTop + ((float)app.height - chromeTop) * 0.5f -
               dpi(app, 40.0f);
    D2D1_RECT_F rects[2];
    if (widths[0] + gap + widths[1] <= paneW - dpi(app, 32.0f)) {
        float left = cx - (widths[0] + gap + widths[1]) * 0.5f;
        rects[0] = D2D1::RectF(left, cy - btnH * 0.5f, left + widths[0],
                               cy + btnH * 0.5f);
        rects[1] = D2D1::RectF(rects[0].right + gap, rects[0].top,
                               rects[0].right + gap + widths[1],
                               rects[0].bottom);
    } else {
        float w = std::max(widths[0], widths[1]);
        rects[0] = D2D1::RectF(cx - w * 0.5f, cy - btnH - gap * 0.5f,
                               cx + w * 0.5f, cy - gap * 0.5f);
        rects[1] = D2D1::RectF(rects[0].left, cy + gap * 0.5f,
                               rects[0].right, cy + btnH + gap * 0.5f);
    }
    app.quickNoteButtonRect = rects[0];
    app.quickNoteSheetRect = rects[1];

    bool hover[2] = { app.quickNoteButtonHover, app.quickNoteSheetHover };
    for (int i = 0; i < 2; i++) {
        const D2D1_RECT_F& btn = rects[i];
        float my = (btn.top + btn.bottom) * 0.5f;
        // Raised card over the empty pane; a touch stronger under the cursor
        D2D1_COLOR_F fill = app.theme.text;
        fill.a = hover[i] ? 0.10f : 0.06f;
        app.brush->SetColor(fill);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(btn, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush);
        D2D1_COLOR_F border = app.theme.text;
        border.a = 0.12f;
        app.brush->SetColor(border);
        app.renderTarget->DrawRoundedRectangle(
            D2D1::RoundedRect(btn, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush,
            1.0f);

        float ix = btn.left + dpi(app, 18.0f);
        float iy = my - dpi(app, 7.5f);
        app.brush->SetColor(app.theme.accent);
        if (i == 0) {
            // Folder icon: tab flap over a rounded body
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(ix, iy, ix + dpi(app, 9.0f),
                                              iy + dpi(app, 6.0f)),
                                  dpi(app, 1.5f), dpi(app, 1.5f)),
                app.brush);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(ix, iy + dpi(app, 2.5f),
                                              ix + iconW, iy + dpi(app, 15.0f)),
                                  dpi(app, 2.0f), dpi(app, 2.0f)),
                app.brush);
        } else {
            // Sheet icon: four cards, as the start page lists a sheet
            float card = dpi(app, 8.0f), cardGap = dpi(app, 3.0f);
            for (int k = 0; k < 4; k++) {
                float kx = ix + dpi(app, 0.5f) + (float)(k % 2) * (card + cardGap);
                float ky = my - card - cardGap * 0.5f + (float)(k / 2) * (card + cardGap);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(kx, ky, kx + card, ky + card),
                                      dpi(app, 1.5f), dpi(app, 1.5f)),
                    app.brush);
            }
        }

        // Label
        float labelX = ix + iconW + dpi(app, 10.0f);
        app.brush->SetColor(app.theme.text);
        app.renderTarget->DrawTextLayout(
            D2D1::Point2F(labelX, my - lm[i].height * 0.5f), labelLayouts[i],
            app.brush);
        if (i != 0) continue;

        // Ctrl+O keycap chip
        float chipX = labelX + lm[0].width + dpi(app, 12.0f);
        float chipH = dpi(app, 22.0f);
        D2D1_RECT_F chipRect = D2D1::RectF(chipX, my - chipH * 0.5f,
                                           chipX + chipW, my + chipH * 0.5f);
        D2D1_COLOR_F chipBg = app.theme.text;
        chipBg.a = 0.08f;
        app.brush->SetColor(chipBg);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(chipRect, dpi(app, 4.0f), dpi(app, 4.0f)),
            app.brush);
        D2D1_COLOR_F chipText = app.theme.text;
        chipText.a = 0.55f;
        app.brush->SetColor(chipText);
        app.renderTarget->DrawTextLayout(
            D2D1::Point2F(chipX + dpi(app, 8.0f), my - cm.height * 0.5f),
            chipLayout, app.brush);
    }
    labelLayouts[0]->Release();
    labelLayouts[1]->Release();
    chipLayout->Release();

    // Hint lines, centered under the buttons, wrapping in a narrow pane.
    // Live's page says how to begin on its own first line instead.
    const char* hintKeys[2] = { "empty.hint1", "empty.hint2" };
    float hintY = rects[1].bottom + dpi(app, 18.0f);
    float hintW = paneW - dpi(app, 32.0f);
    for (const char* key : hintKeys) {
        if (editorLiveOn(app)) break;
        const wchar_t* hint = tr(app, key);
        IDWriteTextLayout* layout = nullptr;
        app.dwriteFactory->CreateTextLayout(hint, (UINT32)wcslen(hint),
                                            format, hintW, 1000.0f, &layout);
        if (layout) {
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            DWRITE_TRIMMING noTrim = { DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0 };
            layout->SetTrimming(&noTrim, nullptr);
            DWRITE_TEXT_METRICS hm{};
            layout->GetMetrics(&hm);
            D2D1_COLOR_F muted = app.theme.text;
            muted.a = 0.5f;
            app.brush->SetColor(muted);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(cx - hintW * 0.5f, hintY), layout,
                app.brush);
            hintY += hm.height + dpi(app, 4.0f);
            layout->Release();
        }
    }
}

// The destination was checked or explicitly confirmed by the user.
// Save As may choose a new file, so this writer must not prompt again.
static void writeEditorFile(App& app, HWND hwnd) {
    // The open table cell belongs in the save (#148)
    tableEditCommit(app);
    auto seenKey = frontmatterSeenKey(app);
    std::string firstSeen = app.frontmatterFirstSeen.count(seenKey) ? app.frontmatterFirstSeen[seenKey] : fm::utcNow();

    std::string utf8 = toUtf8(app.editorText);
    if (!isPlainTextDocumentPath(app.currentFile) &&
        !isMermaidDocumentPath(app.currentFile) &&
        !isPlantUmlDocumentPath(app.currentFile))
        utf8 = fm::stamp(utf8, app.frontmatter, firstSeen, fm::utcNow());
    const std::wstring savedText = fromUtf8(utf8);

    // Detect original line ending style by reading first line
    std::wstring widePath = toWide(app.currentFile);
    bool useCRLF = false;
    {
        std::ifstream check(widePath, std::ios::binary);
        if (check) {
            char buf[4096];
            check.read(buf, sizeof(buf));
            auto count = check.gcount();
            for (int i = 0; i < count - 1; i++) {
                if (buf[i] == '\n') break;
                if (buf[i] == '\r' && buf[i + 1] == '\n') { useCRLF = true; break; }
            }
        }
    }

    // Convert \n to \r\n if original used CRLF
    if (useCRLF) {
        std::string crlf;
        crlf.reserve(utf8.size() + utf8.size() / 20);
        for (size_t i = 0; i < utf8.size(); i++) {
            if (utf8[i] == '\n') {
                crlf += "\r\n";
            } else {
                crlf += utf8[i];
            }
        }
        utf8 = std::move(crlf);
    }
    // The byte order mark the file was opened with goes back in front
    if (app.editorBom) utf8.insert(0, UTF8_BOM);

    std::ofstream out(widePath, std::ios::binary);
    if (out) {
        out.write(utf8.data(), utf8.size());
        out.close();
    }
    if (out.good()) {
        if (savedText != app.editorText) {
            size_t begin = 0, oldEnd = app.editorText.size(), newEnd = savedText.size();
            while (begin < oldEnd && begin < newEnd && app.editorText[begin] == savedText[begin]) ++begin;
            while (oldEnd > begin && newEnd > begin && app.editorText[oldEnd-1] == savedText[newEnd-1]) { --oldEnd; --newEnd; }
            auto shifted = [&](size_t position) {
                if (position <= begin) return position;
                if (position < oldEnd) return std::min(newEnd, begin + position - begin);
                return position - oldEnd + newEnd;
            };
            size_t cursor = shifted(app.editorCursorPos);
            App::EditAction action{App::EditAction::Replace, begin, app.editorText.substr(begin, oldEnd-begin), app.editorCursorPos, cursor};
            action.replacement = savedText.substr(begin, newEnd-begin);
            app.undoStack.push_back(std::move(action));app.redoStack.clear();
            app.editorSelStart = shifted(app.editorSelStart);app.editorSelEnd = shifted(app.editorSelEnd);
            app.editorText = savedText;app.editorCursorPos = cursor;
            rebuildLineStarts(app);app.clearEditorLineLayoutCache();app.editorRowMetricsWidth = -1;
        }
        app.frontmatterFirstSeen[frontmatterSeenKey(app)] = firstSeen;
        app.editorDirty = false;
        updateFileWriteTime(app);

        // The saved buffer no longer needs its crash-recovery draft
        tabsInit(app);
        draftsDeleteForTab(app, app.tabs[app.activeTab].id);

        // Reparse and update preview immediately
        editorReparse(app);

        app.sourceText = utf8;
        if (app.keepDeletedTabs) app.tabs[app.activeTab].viewerSource = app.sourceText;

        // Show "Saved!" notification
        signalPush(app, SIG_SUCCESS, SIGI_CHECK,
                   std::wstring(tr(app, "toast.saved")) + L" \u2014 ",
                   app.tabs[app.activeTab].title,
                   app.tabs[app.activeTab].title);

        // Update window title
        updateWindowTitle(app);

        InvalidateRect(hwnd, nullptr, FALSE);
    } else {
        // Surface the failure — a silent no-op here leaves the document
        // permanently dirty and traps the user in the exit-confirm prompt.
        // The chip stays until answered and offers a retry from the tray.
        signalPush(app, SIG_ERROR, SIGI_WARNING,
                   std::wstring(tr(app, "toast.save_failed")) + L" \u2014 ",
                   app.tabs[app.activeTab].title,
                   app.tabs[app.activeTab].title, SIGA_RETRY_SAVE,
                   SIGC_NONE, false);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void saveEditorFile(App& app, HWND hwnd) {
    tableEditCommit(app);
    if (app.currentFile.empty()) {
        saveEditorFileAs(app, hwnd);
        return;
    }
    // Check the disk directly: deletion may precede the file-watch tick.
    if (GetFileAttributesW(toWide(app.currentFile).c_str()) == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            app.editorDirty = true; // cancelling must also keep a clean recovery buffer open
            saveEditorFileAs(app, hwnd);
            return;
        }
    }
    writeEditorFile(app, hwnd);
}

// Save As (#121): pick a new path, write there, and the tab follows
void saveEditorFileAs(App& app, HWND hwnd) {
    auto key = frontmatterSeenKey(app);
    std::string firstSeen = app.frontmatterFirstSeen.count(key) ? app.frontmatterFirstSeen[key] : "";
    if (!promptSaveAsPath(app, hwnd)) return;  // cancel keeps the old path
    if (!firstSeen.empty()) app.frontmatterFirstSeen[frontmatterSeenKey(app)] = firstSeen;
    app.editorDirty = true;  // force the write even for a clean buffer
    writeEditorFile(app, hwnd);
    if (app.editorDirty) return;  // write failed (toast already shown)

    // The active tab takes on the new identity
    tabsInit(app);
    App::DocTab& tab = app.tabs[app.activeTab];
    tab.path = app.currentFile;
    size_t slash = app.currentFile.find_last_of("/\\");
    tab.title = toWide(slash == std::string::npos
                           ? app.currentFile
                           : app.currentFile.substr(slash + 1));
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Save As from anywhere: the editor variant writes the buffer, the viewer
// variant copies the viewed file — either way the document continues
// under the chosen name
void saveFileAs(App& app, HWND hwnd) {
    if (app.editMode) {
        saveEditorFileAs(app, hwnd);
        return;
    }
    if (app.currentFile.empty()) return;  // welcome doc: nothing to save

    std::string previous = app.currentFile;
    if (!promptSaveAsPath(app, hwnd)) return;  // sets currentFile on OK
    std::wstring src = toWide(previous);
    std::wstring dst = toWide(app.currentFile);
    bool ok = CopyFileW(src.c_str(), dst.c_str(), FALSE) != 0;
    if (!ok && app.keepDeletedTabs && GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream file(dst, std::ios::binary);
        file.write(app.sourceText.data(), (std::streamsize)app.sourceText.size());
        file.close();
        ok = !file.fail();
    }
    if (!ok) {
        app.currentFile = previous;
        signalPushKey(app, SIG_ERROR, SIGI_WARNING, "toast.save_failed");
    } else {
        tabsInit(app);
        App::DocTab& tab = app.tabs[app.activeTab];
        tab.path = app.currentFile;
        if (app.keepDeletedTabs) tab.viewerSource = app.sourceText;
        size_t slash = app.currentFile.find_last_of("/\\");
        tab.title = toWide(slash == std::string::npos
                               ? app.currentFile
                               : app.currentFile.substr(slash + 1));
        updateWindowTitle(app);
        signalPush(app, SIG_SUCCESS, SIGI_CHECK,
                   std::wstring(tr(app, "toast.saved")) + L" \u2014 ",
                   tab.title, tab.title);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Input handlers ---

// Unsaved-changes dialog outcomes (shared by keyboard and mouse):
// 1 = save and exit, 2 = discard changes, 3 = keep editing
void confirmExitAction(App& app, HWND hwnd, int action) {
    if (action == 1) {
        app.confirmExitPending = false;
        saveEditorFile(app, hwnd);
        if (!app.editorDirty) {
            exitEditMode(app);
        }
        // Save failed (or Save As cancelled): dialog closes, editing
        // continues — the failure toast explains itself
    } else if (action == 2) {
        app.confirmExitPending = false;
        app.editorDirty = false;
        exitEditMode(app);
    } else {
        app.confirmExitPending = false;
        app.escPressedOnce = false;
    }
    // A tab close that opened this dialog completes once the buffer is
    // resolved (Keep editing cancels the close). A resolved LONE tab
    // lands on the start page directly: the user asked to close the
    // content, not the window, and after a discard the view may already
    // be the launcher — re-entering tabCloseIndex would close the window.
    if (app.pendingTabClose >= 0) {
        int tab = app.pendingTabClose;
        app.pendingTabClose = -1;
        if (action != 3 && !app.editMode) {
            if ((int)app.tabs.size() <= 1) {
                tabBecomeStartPage(app, hwnd);
            } else {
                tabCloseIndex(app, hwnd, tab);
            }
        }
    }
    // A Close-others/left/right sweep paused on this dialog: continue with
    // the remaining tabs, or abort the whole sweep on Keep editing
    if (app.tabBulkCloseMode) {
        if (action == 3 || app.editMode) {
            app.tabBulkCloseMode = 0;
        } else {
            tabBulkCloseStep(app, hwnd);
        }
    }
    // A window close paused on this dialog resumes once the buffer is
    // resolved: the next dirty tab prompts, or the window finally closes
    if (app.pendingWindowClose) {
        if (action == 3 || app.editMode) {
            app.pendingWindowClose = false;  // cancelled (or save failed)
        } else {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// The carets' selections in document order, one per line (copy, cut)
std::wstring editorCaretSelectionsText(const App& app) {
    std::vector<App::EditorCaret> carets = editorAllCarets(app);
    std::sort(carets.begin(), carets.end(),
              [](const App::EditorCaret& a, const App::EditorCaret& b) {
                  return caretStart(a) < caretStart(b);
              });
    std::wstring text;
    bool first = true;
    for (const auto& caret : carets) {
        if (!caret.hasSelection) continue;
        if (!first) text += L'\n';
        text.append(app.editorText, caretStart(caret), caretEnd(caret) - caretStart(caret));
        first = false;
    }
    return text;
}

// Paste at every caret: a clipboard holding one line per caret (what a
// multi-caret copy produces) hands each caret its own line; anything else
// is pasted whole at each (#251)
void editorPasteAtCarets(App& app, HWND hwnd, const std::wstring& paste) {
    if (paste.empty()) return;
    std::vector<std::wstring> lines;
    for (size_t start = 0;;) {
        size_t newline = paste.find(L'\n', start);
        lines.push_back(paste.substr(start, newline == std::wstring::npos
                                                ? std::wstring::npos : newline - start));
        if (newline == std::wstring::npos) break;
        start = newline + 1;
    }
    if (lines.size() > 1 && lines.back().empty()) lines.pop_back();
    const bool spread = lines.size() == app.editorExtraCarets.size() + 1;
    editorEditAtCarets(app, [&](size_t rank) {
        const std::wstring& text = spread ? lines[rank] : paste;
        if (app.editorHasSelection) editorDeleteSelection(app);
        size_t before = app.editorCursorPos;
        app.editorText.insert(before, text);
        app.editorCursorPos = before + text.size();
        pushUndo(app, App::EditAction::Insert, before, text, before, app.editorCursorPos);
        rebuildLineStarts(app);
    });
    app.editorDesiredCol = -1;
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Counts for the selected text of every caret, cached by the selections'
// ranges; false without a selection. A drag across a huge range recounts
// once it settles rather than on every mouse move.
static bool editorSelectionCounts(App& app, TextCounts& counts) {
    std::wstring key;
    size_t selected = 0;
    for (const auto& caret : editorAllCarets(app)) {
        if (!caret.hasSelection) continue;
        key += std::to_wstring(caretStart(caret)) + L'-' + std::to_wstring(caretEnd(caret)) + L';';
        selected += caretEnd(caret) - caretStart(caret);
    }
    if (key.empty()) return false;
    key += std::to_wstring(app.editorText.size()) + L'/' +
           std::to_wstring(app.undoStack.size()) + L'/' + std::to_wstring(app.redoStack.size());
    if (key != app.editorSelCountsKey &&
        !(app.editorSelecting && selected > 200000 && !app.editorSelCountsKey.empty())) {
        app.editorSelCounts = editorCountSource(app, editorCaretSelectionsText(app));
        app.editorSelCountsKey = key;
    }
    counts = app.editorSelCounts;
    return true;
}

static std::wstring editorCountNumber(size_t value) {
    std::wstring digits = std::to_wstring(value);
    for (ptrdiff_t at = (ptrdiff_t)digits.size() - 3; at > 0; at -= 3) {
        digits.insert((size_t)at, L",");
    }
    return digits;
}

// "1,234 words · 5,678 characters", "12 / 1,234 words · ..." while text is
// selected, or the words alone
static std::wstring editorCountLabel(const App& app, const TextCounts* selection,
                                     bool wordsOnly) {
    auto part = [&](size_t selected, size_t total, const char* one, const char* many) {
        std::wstring number = selection ? editorCountNumber(selected) + L" / " +
                                              editorCountNumber(total)
                                        : editorCountNumber(total);
        wchar_t text[96];
        swprintf_s(text, _countof(text), tr(app, total == 1 ? one : many), number.c_str());
        return std::wstring(text);
    };
    const TextCounts& doc = app.editorDocCounts;
    std::wstring label = part(selection ? selection->words : 0, doc.words,
                              "editor.count.word", "editor.count.words");
    if (!wordsOnly) {
        label += L" \xb7 ";
        label += part(selection ? selection->characters : 0, doc.characters,
                      "editor.count.char", "editor.count.chars");
    }
    return label;
}

// The count sits beside the Read pill in its shape, quieter (#240). The
// characters drop out, then the whole chip, when the pane is too narrow.
void renderEditorWordCount(App& app) {
    app.editorWordCountRect = {};
    if (!app.editMode || !app.showWordCount || !app.brush || !app.codeFormat ||
        !app.dwriteFactory) {
        return;
    }
    if (app.editorDocCountsStale) editorRefreshDocCounts(app, nullptr);
    TextCounts selection;
    const bool selected = !app.editorReadingPreview && editorSelectionCounts(app, selection);
    const D2D1_RECT_F pill = editorReadingButtonRect(app);
    const float left = pill.right + dpi(app, 8.0f);
    const float limit = (app.editorReadingPreview ? (float)app.width : editorPaneWidth(app)) -
                        dpi(app, 12.0f);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    float width = 0.0f;
    for (bool wordsOnly : {false, true}) {
        const std::wstring label = editorCountLabel(app, selected ? &selection : nullptr, wordsOnly);
        layout.Reset();
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(app.dwriteFactory->CreateTextLayout(label.c_str(), (UINT32)label.size(),
                app.codeFormat, 4096.0f, pill.bottom - pill.top, layout.GetAddressOf()))) {
            return;
        }
        useUiFontFallback(app, layout.Get());
        if (FAILED(layout->GetMetrics(&metrics))) return;
        width = metrics.widthIncludingTrailingWhitespace + dpi(app, 28.0f);
        if (left + width <= limit) break;
        layout.Reset();
    }
    if (!layout) return;
    const D2D1_RECT_F chip = D2D1::RectF(left, pill.top, left + width, pill.bottom);
    app.editorWordCountRect = chip;
    app.brush->SetColor(app.theme.codeBackground);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(chip, dpi(app, 5.0f), dpi(app, 5.0f)), app.brush);
    layout->SetMaxWidth(chip.right - chip.left);
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    D2D1_COLOR_F ink = app.theme.text;
    ink.a = 0.7f;
    app.brush->SetColor(ink);
    app.renderTarget->DrawTextLayout({chip.left, chip.top}, layout.Get(), app.brush);
}

// Keys that only move the caret: arrows, Home/End and the page keys (with
// Ctrl, the word and document motions; Ctrl+Up/Down and Ctrl+Page stay inert)
static bool editorNavigationKey(WPARAM key, bool ctrl) {
    switch (key) {
        case VK_LEFT: case VK_RIGHT: case VK_HOME: case VK_END:
            return true;
        case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT:
            return !ctrl;
        default:
            return false;
    }
}

// Moves the primary caret for a navigation key; Shift extends its selection.
// Multi-caret callers run it once per caret (#251).
static void editorNavigate(App& app, WPARAM key, bool ctrl, bool shift) {
    // These keys choose a new logical insertion position. Vertical movement
    // and modifier-only events retain the visual-row side of a prior hit.
    if (key != VK_UP && key != VK_DOWN) {
        app.editorCaretUpstreamPos = std::wstring::npos;
    }
    if (ctrl) {
        editorStartOrExtendSelection(app, shift);
        switch (key) {
            case VK_HOME: app.editorCursorPos = 0; break;
            case VK_END: app.editorCursorPos = app.editorText.size(); break;
            case VK_LEFT: app.editorCursorPos = editorWordLeft(app, app.editorCursorPos); break;
            case VK_RIGHT: app.editorCursorPos = editorWordRight(app, app.editorCursorPos); break;
        }
        app.editorDesiredCol = -1;
        if (shift) editorUpdateSelEnd(app);
        else app.editorHasSelection = false;
        return;
    }
    switch (key) {
        case VK_LEFT:
            editorStartOrExtendSelection(app, shift);
            if (!shift && app.editorHasSelection) {
                app.editorCursorPos = editorSelMin(app);
                app.editorHasSelection = false;
            } else if (app.editorCursorPos > 0) {
                app.editorCursorPos = editorPrevCharStart(app, app.editorCursorPos);
            }
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            return;

        case VK_RIGHT:
            editorStartOrExtendSelection(app, shift);
            if (!shift && app.editorHasSelection) {
                app.editorCursorPos = editorSelMax(app);
                app.editorHasSelection = false;
            } else if (app.editorCursorPos < app.editorText.size()) {
                app.editorCursorPos = editorNextCharEnd(app, app.editorCursorPos);
            }
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            return;

        case VK_UP:
        case VK_DOWN: {
            bool down = (key == VK_DOWN);
            editorStartOrExtendSelection(app, shift);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            if (editorWrapOn(app)) {
                editorMoveCursorVertical(app, down);
            } else if (!down && line > 0) {
                size_t col = (app.editorDesiredCol >= 0) ? (size_t)app.editorDesiredCol : getColFromPos(app, app.editorCursorPos);
                if (app.editorDesiredCol < 0) app.editorDesiredCol = (int)col;
                size_t prevLineLen = getLineLength(app, line - 1);
                app.editorCursorPos = app.editorLineStarts[line - 1] + std::min(col, prevLineLen);
            } else if (down && line + 1 < app.editorLineStarts.size()) {
                size_t col = (app.editorDesiredCol >= 0) ? (size_t)app.editorDesiredCol : getColFromPos(app, app.editorCursorPos);
                if (app.editorDesiredCol < 0) app.editorDesiredCol = (int)col;
                size_t nextLineLen = getLineLength(app, line + 1);
                app.editorCursorPos = app.editorLineStarts[line + 1] + std::min(col, nextLineLen);
            }
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            return;
        }

        case VK_HOME: {
            editorStartOrExtendSelection(app, shift);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            app.editorCursorPos = app.editorLineStarts[line];
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            return;
        }

        case VK_END: {
            editorStartOrExtendSelection(app, shift);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            app.editorCursorPos = getLineEnd(app, line);
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            return;
        }

        case VK_PRIOR:
        case VK_NEXT: {
            editorStartOrExtendSelection(app, shift);
            float scale = app.contentScale * app.zoomFactor;
            float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f * scale;
            int pageLines = std::max(1, (int)(app.height / lineHeight) - 2);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            size_t col = getColFromPos(app, app.editorCursorPos);
            size_t targetLine = key == VK_PRIOR
                ? ((line > (size_t)pageLines) ? line - pageLines : 0)
                : std::min(line + pageLines, app.editorLineStarts.size() - 1);
            size_t targetLineLen = getLineLength(app, targetLine);
            app.editorCursorPos = app.editorLineStarts[targetLine] + std::min(col, targetLineLen);
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            return;
        }
    }
}

static void editorInsertLink(App& app, HWND hwnd);

void handleEditorKeyDown(App& app, HWND hwnd, WPARAM wParam) {
    if (!shortcutModifiersAllowed(static_cast<unsigned>(wParam))) return;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // The insert menu closes on Esc; any other key falls through to the
    // editor after dismissing it
    if (app.editCtxOpen) {
        closeEditCtxMenu(app);
        if (wParam == VK_ESCAPE) return;
    }

    if (ctrl && !shift && (wParam == 'F' || wParam == 'H')) {
        openSearchInput(app, wParam == 'H');
        return;
    }

    // Ctrl+O on an untitled, still-empty note opens the file picker
    // (the empty state's keycap hint; works with the preview hidden too)
    if (ctrl && wParam == 'O' && quickNoteEmptyStateActive(app)) {
        openFileDialog(app, hwnd);
        return;
    }

    if (app.showSearch && app.searchActive) {
        if (ctrl && wParam == 'S') saveEditorFile(app, hwnd);
        else searchInputKeyDown(app, hwnd, wParam);
        return;
    }

    // Unsaved-changes dialog. Only its own keys respond; other keys —
    // including bare modifiers like the Ctrl of a Ctrl+S chord — must NOT
    // silently dismiss it. Esc and N are ignored during the grace window
    // so a smashed Esc lands on a stable, readable dialog instead of
    // cancelling it before it was ever seen.
    if (app.confirmExitPending) {
        auto sinceOpen = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - app.confirmExitOpenedAt).count();
        bool inGrace = sinceOpen < 600;
        if (wParam == 'Y' || wParam == VK_RETURN || (ctrl && wParam == 'S')) {
            confirmExitAction(app, hwnd, 1);
        } else if (wParam == 'N' && !inGrace) {
            confirmExitAction(app, hwnd, 2);
        } else if (wParam == VK_ESCAPE && !inGrace) {
            confirmExitAction(app, hwnd, 3);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Esc first folds extra carets back into one (#251)
    if (wParam == VK_ESCAPE && !app.editorExtraCarets.empty() && !app.editorReadingPreview) {
        app.editorExtraCarets.clear();
        app.escPressedOnce = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (wParam == VK_ESCAPE) {
        // The reading view (#236) is a preview laid over the editor: Esc
        // closes it and returns to the source. Everywhere else Esc keeps
        // its documented meaning and leaves edit mode (#242).
        if (app.editorReadingPreview) {
            setEditorReadingPreview(app, false);
            return;
        }
        // Dirty buffer: straight to the dialog, it is the guard (#106).
        // Clean buffer: keep the documented double-Esc exit.
        if (app.editorDirty) {
            exitEditMode(app);
            app.escPressedOnce = false;
            return;
        }
        auto now = std::chrono::steady_clock::now();
        if (app.escPressedOnce) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - app.lastEscTime).count();
            if (elapsed < 500) {
                exitEditMode(app);
                app.escPressedOnce = false;
                return;
            }
        }
        app.escPressedOnce = true;
        app.lastEscTime = now;
        signalHintKey(app, SIGI_INFO, "toast.exit_confirm");
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    app.escPressedOnce = false;

    if (app.editorReadingPreview) {
        if ((!ctrl && keyBindingMatches(app.keymap[KA_EDIT], static_cast<unsigned>(wParam), false)) || (ctrl && wParam=='E')) {
            setEditorReadingPreview(app,false);
            return;
        }
        // Reading a live buffer never edits it at an invisible source caret.
        if (ctrl && wParam=='A') {
            ensureLayoutComplete(app);
            app.selAnchor=0; app.selFocus=app.docText.size(); app.hasSelection=true;
        } else if (!ctrl && (wParam==VK_UP || wParam==VK_DOWN || wParam==VK_PRIOR || wParam==VK_NEXT || wParam==VK_SPACE || wParam==VK_HOME || wParam==VK_END)) {
            float step=dpi(app,40);
            if (wParam==VK_PRIOR || wParam==VK_NEXT || wParam==VK_SPACE) step=app.height*0.85f;
            if (wParam==VK_UP || wParam==VK_PRIOR || (wParam==VK_SPACE && shift)) step=-step;
            float end=std::max(0.0f,app.contentHeight-app.height);
            app.scrollY=app.targetScrollY=wParam==VK_HOME ? 0 : wParam==VK_END ? end : std::clamp(app.scrollY+step,0.0f,end);
        } else if (ctrl && (wParam=='S' || wParam=='N' || wParam=='O' || wParam=='P')) {
            // Existing save/open/print commands retain ownership of the buffer.
        } else return;
        if (!(ctrl && (wParam=='S' || wParam=='N' || wParam=='O' || wParam=='P'))) {
            InvalidateRect(hwnd,nullptr,FALSE);
            return;
        }
    }

    // Motion keys move every caret (#251)
    if (editorNavigationKey(wParam, ctrl)) {
        editorMoveAtCarets(app, [&] { editorNavigate(app, wParam, ctrl, shift); });
        editorEnsureCursorVisible(app);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (ctrl) {
        switch (wParam) {
            case 'S':
                if (shift) {
                    saveEditorFileAs(app, hwnd);
                    return;
                }
                saveEditorFile(app, hwnd);
                return;
            case 'N':
                // Notepad model: Ctrl+N opens a fresh note tab (this
                // buffer parks in its tab), Ctrl+Shift+N a new window
                if (shift) {
                    launchQuickNoteWindow();
                } else {
                    app.swallowCharsUntil =
                        std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(150);
                    tabOpenQuickNote(app, hwnd);
                }
                return;
            case 'Z':
                editorUndo(app);
                scheduleReparse(app);
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case 'Y':
                editorRedo(app);
                scheduleReparse(app);
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case 'A':
                app.editorExtraCarets.clear();
                app.editorCaretUpstreamPos = std::wstring::npos;
                app.editorSelStart = 0;
                app.editorSelEnd = app.editorText.size();
                app.editorCursorPos = app.editorText.size();
                app.editorHasSelection = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case 'C':
                if (!app.editorExtraCarets.empty()) {
                    editorCopyToClipboard(app, hwnd, editorCaretSelectionsText(app));
                } else if (app.editorHasSelection) {
                    editorCopyToClipboard(app, hwnd, editorGetSelectedText(app));
                }
                return;
            case 'X':
                if (!app.editorExtraCarets.empty()) {
                    std::wstring cut = editorCaretSelectionsText(app);
                    if (cut.empty() || !editorCopyToClipboard(app, hwnd, cut)) return;
                    editorEditAtCarets(app, [&](size_t) { editorDeleteSelection(app); });
                    scheduleReparse(app);
                    editorEnsureCursorVisible(app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (app.editorHasSelection && editorCopyToClipboard(app, hwnd, editorGetSelectedText(app))) {
                    editorDeleteSelection(app);
                    scheduleReparse(app);
                    editorEnsureCursorVisible(app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return;
            case 'V':
                editorPasteClipboard(app, hwnd, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
                return;
            case 'P':
                // Print preview of the document as currently edited, saved
                // or not (openPrintPreview re-parses; Ctrl+E is the pane)
                openPrintPreview(app, hwnd);
                return;
            case 'E': {
                if (shift) { setEditorReadingPreview(app,true); return; }
                // Ctrl+E shows / hides the live render beside the source
                // Ctrl+Shift+E opens the full-width unsaved reading view.
                app.editorShowPreview = !app.editorShowPreview;
                app.clearEditorLineLayoutCache();
                if (app.editorShowPreview) {
                    // Re-parse so the preview catches up with edits made
                    // while it was hidden
                    editorReparse(app);
                }
                app.editorRowMetricsWidth = -1.0f;  // pane width changed
                signalHintKey(app, SIGI_EYE,
                              app.editorShowPreview ? "toast.preview_shown"
                                                    : "toast.preview_hidden");
                app.layoutDirty = app.editorShowPreview;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            case 'B':
            case 'I':
                if (app.editorAssists) {
                    const wchar_t* mark = wParam == 'B' ? L"**" : L"*";
                    editorEditAtCarets(app, [&](size_t) { editorToggleInlineMark(app, hwnd, mark); });
                }
                return;
            case 'W': {
                app.editorWordWrap = !app.editorWordWrap;
                app.clearEditorLineLayoutCache();
                app.editorDesiredCol = -1;
                app.editorDesiredX = -1.0f;
                rebuildEditorRowMetrics(app);
                editorEnsureCursorVisible(app);
                signalHintKey(app, SIGI_INFO,
                              editorWrapOn(app) ? "toast.wrap_on"
                                                : "toast.wrap_off");
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            case 'K':
                // The rail's Link button's shortcut
                editorInsertLink(app, hwnd);
                return;
            case VK_OEM_2:
                // Ctrl+/ switches to Live (docs/live-editor.md)
                setEditorLive(app, hwnd, true);
                return;
            case 'D':
                // Select the word, then add its next occurrence (#251)
                editorSelectNextOccurrence(app);
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
        }
        return; // Don't process other ctrl+key combos as text
    }

    // Non-Ctrl keys
    if (wParam == VK_DELETE) {
        editorEditAtCarets(app, [&](size_t) {
            if (app.editorHasSelection) {
                editorDeleteSelection(app);
            } else if (app.editorCursorPos < app.editorText.size()) {
                size_t delEnd = editorNextCharEnd(app, app.editorCursorPos);
                std::wstring deleted = app.editorText.substr(app.editorCursorPos, delEnd - app.editorCursorPos);
                pushUndo(app, App::EditAction::Delete, app.editorCursorPos, deleted,
                         app.editorCursorPos, app.editorCursorPos);
                app.editorText.erase(app.editorCursorPos, delEnd - app.editorCursorPos);
                rebuildLineStarts(app);
            }
            app.editorDesiredCol = -1;
        });
        scheduleReparse(app);
        editorEnsureCursorVisible(app);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// Alt+Up/Down move lines, Ctrl+Alt+Up/Down add carets (#251). Called for
// WM_SYSKEYDOWN, and ahead of the AltGr guard for WM_KEYDOWN: arrows type no
// character, so that guard has nothing to protect for them.
bool editorAltArrowKey(App& app, HWND hwnd, WPARAM wParam) {
    if (wParam != VK_UP && wParam != VK_DOWN) return false;
    if (!(GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000)) return false;
    const bool live = editorLiveOn(app) && !(app.showSearch && app.searchActive) && !app.tableEditActive;
    if ((!live && !sourceEditorHasFocus(app)) || app.confirmExitPending || app.createRefPending ||
        app.editCtxOpen || app.showSettings || app.showHelp || app.showThemeChooser ||
        app.showThemeEditor || app.showShortcutEditor || app.showPrintPreview ||
        app.showContextMenu || app.showTabMenu || app.showTabSwitcher) {
        return false;
    }
    if (live) return liveAltArrow(app, hwnd, wParam);
    const bool down = wParam == VK_DOWN;
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        editorAddCaretVertical(app, down);
    } else if (editorMoveLines(app, down)) {
        scheduleReparse(app);
    }
    resetCursorBlink(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

// --- Markdown assists ---

// Start offset of the line containing pos
static size_t editorLineStartBefore(const App& app, size_t pos) {
    size_t ls = 0;
    for (size_t s : app.editorLineStarts) {
        if (s <= pos) ls = s;
        else break;
    }
    return ls;
}

// A parsed list marker at the head of one line: "- ", "3. ", "- [ ] "
struct ListMarkerInfo {
    bool isList = false;
    std::wstring indent;      // leading whitespace
    std::wstring marker;      // marker text incl. trailing space(s)
    bool ordered = false;
    int number = 0;
    wchar_t delim = L'.';     // '.' or ')' for ordered lists
    bool task = false;
    size_t contentStart = 0;  // offset from line start past the marker
};

static ListMarkerInfo parseListMarker(const std::wstring& text,
                                      size_t lineStart) {
    ListMarkerInfo info;
    size_t i = lineStart;
    while (i < text.size() && (text[i] == L' ' || text[i] == L'\t')) i++;
    info.indent = text.substr(lineStart, i - lineStart);
    size_t markerStart = i;
    if (i < text.size() &&
        (text[i] == L'-' || text[i] == L'*' || text[i] == L'+')) {
        if (i + 1 >= text.size() || text[i + 1] != L' ') return info;
        i += 2;
        // Task boxes continue as unchecked task items
        if (i + 4 <= text.size() && text[i] == L'[' &&
            (text[i + 1] == L' ' || text[i + 1] == L'x' ||
             text[i + 1] == L'X') &&
            text[i + 2] == L']' && text[i + 3] == L' ') {
            info.task = true;
            i += 4;
        }
    } else {
        size_t d = i;
        while (d < text.size() && iswdigit(text[d]) && d - i < 9) d++;
        if (d == i || d >= text.size() ||
            (text[d] != L'.' && text[d] != L')')) {
            return info;
        }
        if (d + 1 >= text.size() || text[d + 1] != L' ') return info;
        info.ordered = true;
        info.number = _wtoi(text.substr(i, d - i).c_str());
        info.delim = text[d];
        i = d + 2;
    }
    info.isList = true;
    info.marker = text.substr(markerStart, i - markerStart);
    info.contentStart = i - lineStart;
    return info;
}

// Ctrl+B / Ctrl+I: wrap the selection in mark..mark, unwrap when it (or
// its immediate surroundings) already carry the mark, or drop an empty
// pair at the caret
static void editorToggleInlineMark(App& app, HWND hwnd,
                                   const std::wstring& mark) {
    UndoGroupScope group(app);  // the swap below is one undo step
    size_t ml = mark.size();
    if (app.editorHasSelection) {
        size_t s = std::min(app.editorSelStart, app.editorSelEnd);
        size_t e = std::max(app.editorSelStart, app.editorSelEnd);
        std::wstring sel = app.editorText.substr(s, e - s);
        bool inside = sel.size() >= ml * 2 &&
                      sel.compare(0, ml, mark) == 0 &&
                      sel.compare(sel.size() - ml, ml, mark) == 0;
        bool outside = !inside && s >= ml &&
                       e + ml <= app.editorText.size() &&
                       app.editorText.compare(s - ml, ml, mark) == 0 &&
                       app.editorText.compare(e, ml, mark) == 0;
        size_t rs = outside ? s - ml : s;
        size_t re = outside ? e + ml : e;
        std::wstring repl;
        if (inside) {
            repl = sel.substr(ml, sel.size() - ml * 2);
        } else if (outside) {
            repl = sel;
        } else {
            repl = mark + sel + mark;
        }
        std::wstring removed = app.editorText.substr(rs, re - rs);
        app.editorText.erase(rs, re - rs);
        pushUndo(app, App::EditAction::Delete, rs, removed, e, rs);
        app.editorText.insert(rs, repl);
        pushUndo(app, App::EditAction::Insert, rs, repl, rs,
                 rs + repl.size());
        if (inside || outside) {
            app.editorSelStart = rs;
            app.editorSelEnd = rs + repl.size();
        } else {
            app.editorSelStart = rs + ml;
            app.editorSelEnd = rs + ml + sel.size();
        }
        app.editorCursorPos = app.editorSelEnd;
        app.editorHasSelection = app.editorSelEnd > app.editorSelStart;
    } else {
        size_t before = app.editorCursorPos;
        std::wstring ins = mark + mark;
        app.editorText.insert(before, ins);
        app.editorCursorPos = before + ml;
        pushUndo(app, App::EditAction::Insert, before, ins, before,
                 app.editorCursorPos);
    }
    rebuildLineStarts(app);
    app.editorDesiredCol = -1;
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Edit rail actions (design t8) --------------------------------------

// A list or quote mark on every line a caret is on or a selection takes in
// (#251, #306), or off them all when they all have it, as Live's rail makes
// it: one undo step, the carets keeping to the text after the marks
static void editorToggleLineMarks(App& app, HWND hwnd, live::LineMark::Kind kind) {
    std::vector<App::EditorCaret> carets = editorAllCarets(app);
    const std::wstring& t = app.editorText;
    auto blankLine = [&](size_t line) {
        size_t ls = app.editorLineStarts[line], le = t.find(L'\n', ls);
        for (size_t i = ls; i < std::min(le, t.size()); ++i) {
            if (t[i] != L' ' && t[i] != L'\t') return false;
        }
        return true;
    };
    // Each caret's lines; a selection ending at a line's start leaves it out
    std::vector<std::pair<size_t, size_t>> spans;
    size_t top = (size_t)-1, bottom = 0;
    for (const auto& caret : carets) {
        size_t a = caretStart(caret), b = caretEnd(caret);
        size_t first = getLineFromPos(app, a), last = getLineFromPos(app, b);
        if (b > a && last > first && app.editorLineStarts[last] == b) --last;
        spans.push_back({first, last});
        top = std::min(top, first);
        bottom = std::max(bottom, last);
    }
    // A blank line inside a selection of several lines is passed over; a
    // caret's own line takes the mark even when blank
    std::vector<char> take(bottom - top + 1, 0);  // 1: unless blank, 2: always
    for (const auto& [first, last] : spans) {
        for (size_t line = first; line <= last; ++line) {
            take[line - top] = std::max(take[line - top], first != last ? (char)1 : (char)2);
        }
    }
    std::vector<size_t> starts;
    for (size_t line = top; line <= bottom; ++line) {
        const char want = take[line - top];
        if (want == 2 || (want == 1 && !blankLine(line))) starts.push_back(app.editorLineStarts[line]);
    }
    const std::vector<live::MarkEdit> edits = live::lineMarkEdits(t, starts, kind);
    if (edits.empty()) return;
    // One replace over the lines, however many
    const size_t primary = carets.size() - 1, before = app.editorCursorPos;
    for (auto& caret : carets) {
        caret.pos = live::throughMarks(caret.pos, edits);
        caret.anchor = live::throughMarks(caret.anchor, edits);
        caret.desiredCol = -1;
    }
    const size_t from = edits.front().at, to = edits.back().at + edits.back().removed;
    const std::wstring marked = live::markedSpan(t, edits);
    pushReplaceUndo(app, from, t.substr(from, to - from), marked, before, carets[primary].pos);
    app.editorText.replace(from, to - from, marked);
    editorStoreCarets(app, std::move(carets), primary);
    rebuildLineStarts(app);
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Insert a block snippet at the caret (own line), placing the caret
// caretOffset characters into the snippet
static void editorInsertSnippet(App& app, HWND hwnd,
                                const std::wstring& snippet,
                                size_t caretOffset) {
    app.editorExtraCarets.clear();  // blocks go in once, at the primary caret
    if (app.editorHasSelection) editorDeleteSelection(app);
    size_t before = app.editorCursorPos;
    std::wstring ins = snippet;
    size_t lead = 0;
    if (before > 0 && app.editorText[before - 1] != L'\n') {
        ins = L"\n" + ins;
        lead = 1;
    }
    app.editorText.insert(before, ins);
    app.editorCursorPos = before + lead + caretOffset;
    pushUndo(app, App::EditAction::Insert, before, ins, before,
             app.editorCursorPos);
    rebuildLineStarts(app);
    app.editorDesiredCol = -1;
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Wrap the selection as a link ([sel](url) with "url" selected), or drop
// a [text](url) template with "text" selected
static void editorInsertLink(App& app, HWND hwnd) {
    app.editorExtraCarets.clear();
    if (app.editorHasSelection) {
        size_t s = editorSelMin(app);
        size_t e = editorSelMax(app);
        std::wstring sel = app.editorText.substr(s, e - s);
        std::wstring repl = L"[" + sel + L"](url)";
        std::wstring removed = app.editorText.substr(s, e - s);
        app.editorText.erase(s, e - s);
        pushUndo(app, App::EditAction::Delete, s, removed, e, s);
        app.editorText.insert(s, repl);
        pushUndo(app, App::EditAction::Insert, s, repl, s, s + repl.size());
        app.editorSelStart = s + sel.size() + 3;
        app.editorSelEnd = app.editorSelStart + 3;  // "url"
        app.editorCursorPos = app.editorSelEnd;
        app.editorHasSelection = true;
    } else {
        size_t before = app.editorCursorPos;
        std::wstring ins = L"[text](url)";
        app.editorText.insert(before, ins);
        app.editorSelStart = before + 1;
        app.editorSelEnd = before + 5;  // "text"
        app.editorCursorPos = app.editorSelEnd;
        app.editorHasSelection = true;
        pushUndo(app, App::EditAction::Insert, before, ins, before,
                 app.editorCursorPos);
    }
    rebuildLineStarts(app);
    app.editorDesiredCol = -1;
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Insert-menu helpers (design t9) ------------------------------------

static bool editorSelectionContainsPoint(App& app, size_t line, float x, float y) {
    if (!app.editorHasSelection || !app.editorTextFormat) return false;
    size_t start = app.editorLineStarts[line], len = getLineLength(app, line);
    size_t low = editorSelMin(app), high = editorSelMax(app);
    if (low == high || high <= start || low > start + len) return false;

    // Match the painted selection, rather than the nearest insertion offset:
    // blank space outside a line can otherwise look like its selected endpoint.
    bool wrap = editorWrapOn(app);
    float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    float row = wrap ? (float)app.editorRowStarts[line] : (float)line;
    x -= editorTextX(app) - (wrap ? 0.0f : app.editorScrollX);
    y -= chromeTopHeight(app) + dpi(app, 8.0f) + row * lineHeight - app.editorScrollY;
    auto contains = [&](float left, float top, float width, float height) {
        return x >= left && x < left + width && y >= top && y < top + height;
    };
    auto* layout = cachedEditorLineLayout(app, start, len);
    size_t from = low > start ? low - start : 0;
    size_t to = std::min(high - start, len);
    if (to > from && layout) {
        if (!wrap) {
            float left = editorColToX(app, layout, from);
            if (contains(left, 0, editorColToX(app, layout, to) - left, lineHeight)) return true;
        } else {
            UINT32 count = 0;
            layout->HitTestTextRange((UINT32)from, (UINT32)(to - from), 0, 0, nullptr, 0, &count);
            std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
            if (count && SUCCEEDED(layout->HitTestTextRange((UINT32)from, (UINT32)(to - from),
                    0, 0, hits.data(), count, &count))) {
                for (const auto& hit : hits)
                    if (contains(hit.left, hit.top, hit.width, hit.height)) return true;
            }
        }
    }
    // Both editor renderers paint a selected newline as one extra character cell.
    if (high > start + len && low <= start + len) {
        float ex = 0, ey = 0;
        if (wrap) editorCaretXY(layout, len, ex, ey);
        else ex = editorColToX(app, layout, len);
        float width = app.editorCharWidth > 0 ? app.editorCharWidth : app.editorTextFormat->GetFontSize() * 0.6f;
        return contains(ex, ey, width, lineHeight);
    }
    return false;
}

void editorTouchSelection(App& app, int x, int y, bool begin) {
    if (begin) {
        focusSourceEditor(app);
        app.editorExtraCarets.clear();
    }
    size_t first, last;
    editorWordAt(app, editorPosFromClick(app, x, y), first, last);
    auto& t = app.touch;
    if (begin) {
        t.editorWordStart = first;
        t.editorWordEnd = last;
    }
    app.editorSelStart = std::min(first, t.editorWordStart);
    app.editorSelEnd = std::max(last, t.editorWordEnd);
    app.editorCursorPos = first < t.editorWordStart ? app.editorSelStart : app.editorSelEnd;
    app.editorHasSelection = app.editorSelStart != app.editorSelEnd;
    app.editorCaretUpstreamPos = std::wstring::npos;
    app.editorDesiredCol = -1;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void editorPrepareContextMenuAt(App& app, int x, int y) {
    if (editorLiveOn(app)) return;  // Live placed its caret already
    focusSourceEditor(app);
    size_t affinity = app.editorCaretUpstreamPos;
    size_t position = editorPosFromClick(app, x, y);
    if (!app.editorLineStarts.empty() &&
        editorSelectionContainsPoint(app, getLineFromPos(app, position), (float)x, (float)y)) {
        app.editorCaretUpstreamPos = affinity;
        return;
    }
    // A right-click away from the selection places one caret, as a click does
    app.editorExtraCarets.clear();
    app.editorCursorPos = position;
    app.editorSelStart = app.editorSelEnd = position;
    app.editorHasSelection = false;
    app.editorDesiredCol = -1;
}

void editorInsertTableGrid(App& app, HWND hwnd, int cols, int rows) {
    cols = std::max(1, std::min(cols, 8));
    rows = std::max(1, std::min(rows, 6));
    std::wstring snippet;
    for (int c = 0; c < cols; c++) snippet += L"| Col ";
    snippet += L"|\n";
    for (int c = 0; c < cols; c++) snippet += L"|---";
    snippet += L"|\n";
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) snippet += L"|  ";
        snippet += L"|\n";
    }
    editorInsertSnippet(app, hwnd, snippet, 2);
}

const wchar_t* diagramTemplate(int kind) {
    static const wchar_t* kTemplates[] = {
        // 0 flowchart
        L"```mermaid\nflowchart TD\n    A[Start] --> B{Decide}\n"
        L"    B -->|Yes| C[Do it]\n    B -->|No| D[Skip]\n```\n",
        // 1 sequence
        L"```mermaid\nsequenceDiagram\n    participant E as Editor\n"
        L"    participant P as Preview\n    E->>P: render()\n"
        L"    P-->>E: done\n```\n",
        // 2 class
        L"```mermaid\nclassDiagram\n    class Document {\n        +title\n"
        L"        +render()\n    }\n    Document <|-- Note\n```\n",
        // 3 state
        L"```mermaid\nstateDiagram-v2\n    [*] --> Idle\n"
        L"    Idle --> Busy : start\n    Busy --> Idle : done\n```\n",
        // 4 gantt
        L"```mermaid\ngantt\n    title Plan\n    dateFormat YYYY-MM-DD\n"
        L"    section Work\n    Task :a, 2026-08-25, 3d\n```\n",
        // 5 pie
        L"```mermaid\npie title Split\n    \"A\" : 60\n    \"B\" : 40\n```\n",
        // 6 empty block
        L"```mermaid\n\n```\n",
        // 7 PlantUML sequence
        L"```plantuml\n@startuml\nparticipant Editor\n"
        L"participant Preview\nEditor -> Preview : render()\n"
        L"Preview --> Editor : rendered\n@enduml\n```\n",
        // 8 PlantUML class
        L"```plantuml\n@startuml\nclass Document {\n"
        L"    +title: String\n    +render()\n}\n"
        L"Document <|-- Note\n@enduml\n```\n",
        // 9 PlantUML activity
        L"```plantuml\n@startuml\nstart\nif (Edited?) then (yes)\n"
        L"    :Save file;\nelse (no)\n    :Keep editing;\n"
        L"endif\nstop\n@enduml\n```\n",
        // 10 mind map
        L"```mermaid\nmindmap\n  root((Idea))\n    First\n    Second\n```\n",
    };
    return kind >= 0 && kind <= 10 ? kTemplates[kind] : L"";
}

void editorInsertDiagramTemplate(App& app, HWND hwnd, int kind) {
    if (kind < 0 || kind > 9) return;
    // PlantUML fences are one char wider: caret lands inside the body
    editorInsertSnippet(app, hwnd, diagramTemplate(kind), kind >= 7 ? 12 : 11);
}

void editorInsertSnippetPublic(App& app, HWND hwnd,
                               const std::wstring& snippet,
                               size_t caretOffset) {
    editorInsertSnippet(app, hwnd, snippet, caretOffset);
}

void editorClipboardCut(App& app, HWND hwnd) {
    if (!app.editorExtraCarets.empty()) {
        std::wstring cut = editorCaretSelectionsText(app);
        if (cut.empty() || !editorCopyToClipboard(app, hwnd, cut)) return;
        editorEditAtCarets(app, [&](size_t) { editorDeleteSelection(app); });
    } else {
        if (!app.editorHasSelection || !editorCopyToClipboard(app, hwnd, editorGetSelectedText(app))) return;
        editorDeleteSelection(app);
    }
    rebuildLineStarts(app);
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void editorClipboardCopy(App& app, HWND hwnd) {
    if (!app.editorExtraCarets.empty()) {
        editorCopyToClipboard(app, hwnd, editorCaretSelectionsText(app));
    } else if (app.editorHasSelection) {
        editorCopyToClipboard(app, hwnd, editorGetSelectedText(app));
    }
}

void editorPasteClipboard(App& app, HWND hwnd, bool raw) {
    if (!app.editorExtraCarets.empty()) {
        editorPasteAtCarets(app, hwnd, editorGetClipboard(hwnd));
        return;
    }
    std::wstring paste = editorGetClipboard(hwnd);
    // A bitmap without text (a screenshot) saves as PNG beside
    // the document and pastes as its markdown link
    if (paste.empty() && !app.currentFile.empty() &&
        IsClipboardFormatAvailable(CF_BITMAP)) {
        namespace fs = std::filesystem;
        fs::path doc(toWide(app.currentFile));
        std::wstring stem = doc.stem().wstring();
        std::wstring name;
        fs::path target;
        for (int i = 1; i < 100; i++) {
            wchar_t suffix[24];
            swprintf_s(suffix, _countof(suffix),
                       L"-img-%02d.png", i);
            std::wstring candidate = stem + suffix;
            fs::path p = doc.parent_path() / candidate;
            std::error_code ec;
            if (!fs::exists(p, ec)) {
                name = candidate;
                target = p;
                break;
            }
        }
        if (!name.empty() &&
            clipboardImageToPngFile(app, hwnd,
                                    target.wstring())) {
            std::wstring linkName = name;
            size_t sp = 0;  // spaces break the link target
            while ((sp = linkName.find(L' ', sp)) !=
                   std::wstring::npos) {
                linkName.replace(sp, 1, L"%20");
                sp += 3;
            }
            paste = L"![image](" + linkName + L")";
        }
    }
    if (paste.empty()) return;
    if (app.editorHasSelection) editorDeleteSelection(app);
    // Excel range copies arrive as TSV: paste them as a
    // markdown table (#181). Ctrl+Shift+V pastes raw.
    if (!raw && tsvLooksLikeTable(paste)) {
        paste = padTableForInsert(
            app, tsvToMarkdownTable(paste), app.editorCursorPos);
        signalPushKey(app, SIG_SUCCESS, SIGI_CHECK,
                      "toast.paste_table");
    } else if (!raw && app.editorAssists) {
        // Lines pasted on a list item are its items, as Enter makes them,
        // and lines pasted in a quote its lines (#305)
        paste = live::pasteMarks(app.editorText, app.editorCursorPos,
                                 app.editorCursorPos, paste);
    }
    size_t before = app.editorCursorPos;
    app.editorText.insert(app.editorCursorPos, paste);
    app.editorCursorPos += paste.size();
    pushUndo(app, App::EditAction::Insert, before, paste, before, app.editorCursorPos);
    rebuildLineStarts(app);
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void editorClipboardPaste(App& app, HWND hwnd) {
    std::wstring paste = editorGetClipboard(hwnd);
    if (paste.empty()) return;
    if (!app.editorExtraCarets.empty()) {
        editorPasteAtCarets(app, hwnd, paste);
        return;
    }
    if (app.editorHasSelection) editorDeleteSelection(app);
    // Same Excel/TSV conversion as Ctrl+V (#181); the rail button has
    // no modifier, Ctrl+Shift+V is the raw-paste escape hatch
    if (tsvLooksLikeTable(paste)) {
        paste = padTableForInsert(app, tsvToMarkdownTable(paste),
                                  app.editorCursorPos);
        signalPushKey(app, SIG_SUCCESS, SIGI_CHECK, "toast.paste_table");
    } else if (app.editorAssists) {
        paste = live::pasteMarks(app.editorText, app.editorCursorPos,
                                 app.editorCursorPos, paste);  // #305
    }
    size_t before = app.editorCursorPos;
    app.editorText.insert(app.editorCursorPos, paste);
    app.editorCursorPos += paste.size();
    pushUndo(app, App::EditAction::Insert, before, paste, before,
             app.editorCursorPos);
    rebuildLineStarts(app);
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void editRailInvoke(App& app, HWND hwnd, int id) {
    if (id == 30 || id == 31) {
        setEditorLive(app, hwnd, id == 31);
        return;
    }
    if (editorLiveOn(app) && liveRailCommand(app, hwnd, id)) return;
    // Marks act at every caret, and list and quote marks on every line the
    // carets and the selections take in (#251, #306); links, snippets and
    // flyouts work at the primary caret
    auto mark = [&](const wchar_t* text) {
        editorEditAtCarets(app, [&](size_t) { editorToggleInlineMark(app, hwnd, text); });
    };
    switch (id) {
        case 1: mark(L"**"); break;
        case 2: mark(L"*"); break;
        case 3: mark(L"~~"); break;
        case 4: mark(L"`"); break;
        case 5: editorInsertLink(app, hwnd); break;
        case 10: editorToggleLineMarks(app, hwnd, live::LineMark::Bullet); break;
        case 13: editorToggleLineMarks(app, hwnd, live::LineMark::Number); break;
        case 11: editorToggleLineMarks(app, hwnd, live::LineMark::Task); break;
        case 12: editorToggleLineMarks(app, hwnd, live::LineMark::Quote); break;
        case 20: openEditRailFlyout(app, hwnd, 1); break;  // size grid
        case 21: openEditRailFlyout(app, hwnd, 2); break;  // templates
        case 23: openEditRailFlyout(app, hwnd, 3); break;  // pandoc
        case 22:
            editorInsertSnippet(app, hwnd, L"![](image.png)\n", 4);
            break;
    }
}

// Characters that wrap a selection instead of replacing it, with their
// closing partner (#251)
static wchar_t editorWrapCloser(wchar_t ch) {
    switch (ch) {
        case L'(': return L')';
        case L'[': return L']';
        case L'{': return L'}';
        case L'"': case L'\'': case L'`': case L'*': case L'~':
        case L'^': case L'=': case L':':
            return ch;
        default:
            return 0;
    }
}

// open + selection + close as one undo step; the text stays selected in
// its original direction, so a second * makes the first one bold
static void editorWrapSelection(App& app, wchar_t open, wchar_t close) {
    const size_t start = editorSelMin(app), end = editorSelMax(app);
    const bool forward = app.editorCursorPos == end;
    const std::wstring selected = app.editorText.substr(start, end - start);
    const std::wstring wrapped = std::wstring(1, open) + selected + close;
    const size_t cursorBefore = app.editorCursorPos;
    app.editorSelStart = forward ? start + 1 : end + 1;
    app.editorSelEnd = forward ? end + 1 : start + 1;
    app.editorCursorPos = app.editorSelEnd;
    app.editorHasSelection = true;
    pushReplaceUndo(app, start, selected, wrapped, cursorBefore, app.editorCursorPos);
    app.editorText.replace(start, end - start, wrapped);
    app.editorDesiredCol = -1;
    rebuildLineStarts(app);
}

static void editorCharInputAtCaret(App& app, HWND hwnd, WPARAM wParam);

void handleEditorCharInput(App& app, HWND hwnd, WPARAM wParam) {
    if (app.editorReadingPreview) return;
    // Swallow characters while confirm-exit prompt is active
    if (app.confirmExitPending) return;

    resetCursorBlink(app);

    if (app.showSearch && app.searchActive) {
        searchInputChar(app, hwnd, static_cast<wchar_t>(wParam));
        return;
    }

    if (app.editorExtraCarets.empty()) {
        editorCharInputAtCaret(app, hwnd, wParam);
        return;
    }
    // Every caret types (#251). Tab and Shift+Tab may edit at the line
    // start, so they keep one caret per line.
    const wchar_t ch = (wchar_t)wParam;
    if (ch == 27 || (ch < 32 && ch != 8 && ch != 9 && ch != 13 && ch != L'\n')) return;
    if (ch == 9) {
        if (GetKeyState(VK_CONTROL) & 0x8000) return;  // Ctrl+I, handled on keydown
        editorOneCaretPerLine(app);
    }
    editorEditAtCarets(app, [&](size_t) { editorCharInputAtCaret(app, hwnd, wParam); });
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void editorCharInputAtCaret(App& app, HWND hwnd, WPARAM wParam) {
    wchar_t ch = (wchar_t)wParam;

    if (ch == 8) { // Backspace
        if (app.editorHasSelection) {
            editorDeleteSelection(app);
        } else if (app.editorCursorPos > 0) {
            size_t before = app.editorCursorPos;
            size_t delStart = editorPrevCharStart(app, app.editorCursorPos);
            std::wstring deleted = app.editorText.substr(delStart, before - delStart);
            app.editorText.erase(delStart, before - delStart);
            app.editorCursorPos = delStart;
            pushUndo(app, App::EditAction::Delete, app.editorCursorPos, deleted, before, app.editorCursorPos);
            rebuildLineStarts(app);
        }
        app.editorDesiredCol = -1;
        scheduleReparse(app);
        editorEnsureCursorVisible(app);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (ch == 13) { // Enter -> \n
        ch = L'\n';
    }

    // Enter on a list item continues the list; Enter on an empty item
    // removes its marker and ends it. The assists are a setting.
    if (ch == L'\n' && !app.editorHasSelection && app.editorAssists) {
        size_t ls = editorLineStartBefore(app, app.editorCursorPos);
        ListMarkerInfo lm = parseListMarker(app.editorText, ls);
        if (lm.isList && app.editorCursorPos >= ls + lm.contentStart) {
            size_t le = app.editorText.find(L'\n', ls);
            if (le == std::wstring::npos) le = app.editorText.size();
            bool emptyItem = (le == ls + lm.contentStart);
            if (emptyItem) {
                std::wstring removed =
                    app.editorText.substr(ls, lm.contentStart);
                app.editorText.erase(ls, lm.contentStart);
                pushUndo(app, App::EditAction::Delete, ls, removed,
                         app.editorCursorPos, ls);
                app.editorCursorPos = ls;
            } else {
                std::wstring next = lm.marker;
                if (lm.ordered) {
                    next = std::to_wstring(lm.number + 1);
                    next += lm.delim;
                    next += L' ';
                } else if (lm.task) {
                    next = next.substr(0, 2) + L"[ ] ";
                }
                std::wstring cont = L"\n" + lm.indent + next;
                size_t before = app.editorCursorPos;
                app.editorText.insert(before, cont);
                app.editorCursorPos = before + cont.size();
                pushUndo(app, App::EditAction::Insert, before, cont,
                         before, app.editorCursorPos);
            }
            rebuildLineStarts(app);
            app.editorDesiredCol = -1;
            scheduleReparse(app);
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }

    if (ch == 9) { // Tab
        // Ctrl+I arrives as the Tab control character: keydown handled it
        if (GetKeyState(VK_CONTROL) & 0x8000) return;
        bool assists = app.editorAssists;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shift && !assists) return;
        if (shift) {
            // Shift+Tab: outdent the caret line by up to 4 spaces / a tab
            size_t ls = editorLineStartBefore(app, app.editorCursorPos);
            size_t n = 0;
            while (n < 4 && ls + n < app.editorText.size() &&
                   app.editorText[ls + n] == L' ') {
                n++;
            }
            if (n == 0 && ls < app.editorText.size() &&
                app.editorText[ls] == L'\t') {
                n = 1;
            }
            if (n > 0) {
                std::wstring removed = app.editorText.substr(ls, n);
                size_t before = app.editorCursorPos;
                app.editorText.erase(ls, n);
                pushUndo(app, App::EditAction::Delete, ls, removed,
                         before, before > ls + n ? before - n : ls);
                app.editorCursorPos =
                    before > ls + n ? before - n : ls;
                rebuildLineStarts(app);
                app.editorDesiredCol = -1;
                scheduleReparse(app);
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return;
        }
        if (!app.editorHasSelection && assists) {
            // Tab on a list item indents the whole line one level
            size_t ls = editorLineStartBefore(app, app.editorCursorPos);
            ListMarkerInfo lm = parseListMarker(app.editorText, ls);
            if (lm.isList) {
                std::wstring spaces = L"    ";
                size_t before = app.editorCursorPos;
                app.editorText.insert(ls, spaces);
                app.editorCursorPos = before + 4;
                pushUndo(app, App::EditAction::Insert, ls, spaces, before,
                         app.editorCursorPos);
                rebuildLineStarts(app);
                app.editorDesiredCol = -1;
                scheduleReparse(app);
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
        std::wstring spaces = L"    ";
        if (app.editorHasSelection) editorDeleteSelection(app);
        size_t before = app.editorCursorPos;
        app.editorText.insert(app.editorCursorPos, spaces);
        app.editorCursorPos += 4;
        pushUndo(app, App::EditAction::Insert, before, spaces, before, app.editorCursorPos);
        rebuildLineStarts(app);
        app.editorDesiredCol = -1;
        scheduleReparse(app);
        editorEnsureCursorVisible(app);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (ch == 27) return; // ESC handled in KeyDown
    if (ch < 32 && ch != L'\n') return; // Ignore other control chars

    // A quote, bracket or Markdown marker typed over a selection wraps it
    // (#251); the assists switch turns this off with the other helpers
    if (app.editorHasSelection && app.editorAssists) {
        if (wchar_t close = editorWrapCloser(ch)) {
            editorWrapSelection(app, ch, close);
            scheduleReparse(app);
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
    }

    // Normal character insertion
    if (app.editorHasSelection) editorDeleteSelection(app);
    std::wstring ins(1, ch);
    size_t before = app.editorCursorPos;
    app.editorText.insert(app.editorCursorPos, ins);
    app.editorCursorPos++;
    pushUndo(app, App::EditAction::Insert, before, ins, before, app.editorCursorPos);
    rebuildLineStarts(app);
    app.editorDesiredCol = -1;
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- IME support ---

// Place the IME composition window at the caret so candidate lists for
// CJK input appear where the user is typing instead of the window corner.
void editorPositionImeWindow(App& app, HWND hwnd) {
    D2D1_POINT_2F inputPoint{};
    bool inputField = searchInputCaretPoint(app, inputPoint) || tableEditCaretPoint(app, inputPoint);
    if (!inputField && (!app.editMode || app.editorLineStarts.empty())) return;
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return;
    if (inputField) {
        COMPOSITIONFORM composition{};
        composition.dwStyle = CFS_POINT;
        composition.ptCurrentPos = {static_cast<LONG>(inputPoint.x), static_cast<LONG>(inputPoint.y)};
        ImmSetCompositionWindow(himc, &composition);
        CANDIDATEFORM candidate{};
        candidate.dwStyle = CFS_CANDIDATEPOS;
        candidate.ptCurrentPos = composition.ptCurrentPos;
        ImmSetCandidateWindow(himc, &candidate);
        ImmReleaseContext(hwnd, himc);
        return;
    }

    D2D1_POINT_2F point{};
    float liveHeight = 0;
    if (liveCaretClientPoint(app, point, liveHeight)) {
        // The Live page's caret
        COMPOSITIONFORM cf{};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos = {(LONG)point.x, (LONG)(point.y + liveHeight)};
        ImmSetCompositionWindow(himc, &cf);
    } else if (editorCaretPoint(app, point)) {
        float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
        COMPOSITIONFORM cf{};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos = {(LONG)point.x, (LONG)(point.y + lineHeight)};
        ImmSetCompositionWindow(himc, &cf);
    }
    ImmReleaseContext(hwnd, himc);
}

// --- Mouse handling ---

static size_t editorPosFromClick(App& app, int x, int y) {
    app.editorCaretUpstreamPos = std::wstring::npos;
    if (!app.editorTextFormat || app.editorLineStarts.empty()) return 0;

    float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    float padding = dpi(app, 8.0f);

    float adjustedY = y - chromeTopHeight(app) + app.editorScrollY - padding;
    size_t line;
    float localY = lineHeight * 0.5f;
    if (editorWrapOn(app)) {
        ensureEditorRowMetrics(app);
        size_t row = (size_t)std::max(0, (int)(adjustedY / lineHeight));
        line = editorLineFromRow(app, row);
        if (line < app.editorRowStarts.size()) {
            localY = adjustedY - app.editorRowStarts[line] * lineHeight;
            localY = std::max(0.0f, localY);
        }
    } else {
        line = (size_t)std::max(0, (int)(adjustedY / lineHeight));
    }
    if (line >= app.editorLineStarts.size()) line = app.editorLineStarts.size() - 1;

    size_t lineStart = app.editorLineStarts[line];
    size_t lineLen = getLineLength(app, line);

    float adjustedX = (float)x - editorTextX(app);
    if (!editorWrapOn(app)) adjustedX += app.editorScrollX;
    if (lineLen == 0) return lineStart;
    if (adjustedX <= 0.0f && !editorWrapOn(app)) return lineStart;
    adjustedX = std::max(0.0f, adjustedX);

    size_t col;
    IDWriteTextLayout* layout = createEditorLineLayout(app, lineStart, lineLen);
    if (layout) {
        size_t position = lineStart;
        editorHitCaret(app, layout, adjustedX, localY, lineStart, lineLen, position);
        layout->Release();
        col = position - lineStart;
    } else {
        float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth : app.editorTextFormat->GetFontSize() * 0.6f;
        col = (size_t)std::max(0, (int)(adjustedX / charWidth + 0.5f));
    }
    col = std::min(col, lineLen);

    return lineStart + col;
}

// --- Dragging the selection (#289) ---

// A press on the selected text: strictly inside the selection, or on one of
// its edges with the pointer over the selected character beside it (the
// hit rounds to the nearer edge)
static bool editorPointInSelection(App& app, int x, int y, size_t pos) {
    if (!app.editorHasSelection) return false;
    const size_t selMin = editorSelMin(app), selMax = editorSelMax(app);
    if (selMin == selMax || pos < selMin || pos > selMax) return false;
    if (pos > selMin && pos < selMax) return true;
    D2D1_POINT_2F edge{};
    if (!editorCaretPointAt(app, pos, false, edge)) return false;
    const float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    if ((float)y < edge.y || (float)y >= edge.y + lineHeight) return false;
    return pos == selMax ? (float)x < edge.x : (float)x > edge.x;
}

static void editorTextDragStop(App& app, HWND hwnd) {
    app.editorTextDrag = {};  // first: losing the capture calls the cancel
    KillTimer(hwnd, TIMER_EDITOR_DRAG_SCROLL);
    if (GetCapture() == hwnd) ReleaseCapture();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void editorTextDragCancel(App& app, HWND hwnd) {
    if (app.editorTextDrag.armed) editorTextDragStop(app, hwnd);
}

// Moves the selection to pos, or copies it there, as one undo step; the
// text stays selected where it lands
static void editorDropSelection(App& app, HWND hwnd, size_t pos, bool copy) {
    const size_t selMin = editorSelMin(app), selMax = editorSelMax(app);
    if (!app.editorHasSelection || selMin == selMax || pos > app.editorText.size()) return;
    // Onto the selection nothing happens, and a move to either of its ends
    // would leave the text as it is
    if ((pos > selMin && pos < selMax) || (!copy && (pos == selMin || pos == selMax))) return;
    const std::wstring text = app.editorText.substr(selMin, selMax - selMin);
    UndoGroupScope group(app);
    const size_t caretBefore = app.editorCursorPos;
    size_t at = pos;
    if (!copy) {
        pushUndo(app, App::EditAction::Delete, selMin, text, caretBefore, selMin);
        app.editorText.erase(selMin, text.size());
        if (at > selMin) at -= text.size();
    }
    app.editorText.insert(at, text);
    pushUndo(app, App::EditAction::Insert, at, text, copy ? caretBefore : selMin, at + text.size());
    app.editorSelStart = at;
    app.editorSelEnd = at + text.size();
    app.editorCursorPos = app.editorSelEnd;
    app.editorHasSelection = true;
    app.editorCaretUpstreamPos = std::wstring::npos;
    app.editorDesiredCol = -1;
    rebuildLineStarts(app);
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Past the system's drag distance the drag starts. The drop caret then
// follows the pointer over the text, and past the editor's top or bottom
// edge the editor scrolls, faster the further out.
static void editorTextDragMove(App& app, HWND hwnd, int x, int y) {
    auto& drag = app.editorTextDrag;
    drag.x = x;
    drag.y = y;
    if (!drag.active) {
        if (std::abs(x - drag.startX) <= GetSystemMetrics(SM_CXDRAG) &&
            std::abs(y - drag.startY) <= GetSystemMetrics(SM_CYDRAG)) {
            return;
        }
        drag.active = true;
    }
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    // Over the preview there is nowhere to drop
    drag.dropPos = (float)x < editorPaneWidth(app) ? editorPosFromClick(app, x, y) : SIZE_MAX;
    const float top = chromeTopHeight(app), band = dpi(app, 24.0f);
    float vel = 0.0f;
    if ((float)y < top + band) {
        vel = -(top + band - (float)y) * 0.35f;
    } else if ((float)y > (float)app.height - band) {
        vel = ((float)y - ((float)app.height - band)) * 0.35f;
    }
    if (vel != 0.0f && drag.scrollVel == 0.0f) {
        SetTimer(hwnd, TIMER_EDITOR_DRAG_SCROLL, 16, nullptr);
    } else if (vel == 0.0f && drag.scrollVel != 0.0f) {
        KillTimer(hwnd, TIMER_EDITOR_DRAG_SCROLL);
    }
    drag.scrollVel = vel;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void editorTextDragScroll(App& app, HWND hwnd) {
    auto& drag = app.editorTextDrag;
    if (!drag.active || drag.scrollVel == 0.0f || !app.editMode) {
        KillTimer(hwnd, TIMER_EDITOR_DRAG_SCROLL);
        drag.scrollVel = 0.0f;
        return;
    }
    const float maxScroll = std::max(0.0f, app.editorContentHeight - (float)app.height);
    const float next = std::clamp(app.editorScrollY + drag.scrollVel, 0.0f, maxScroll);
    if (next == app.editorScrollY) return;  // at an end: the timer waits for a turn
    app.editorScrollY = next;
    if (drag.dropPos != SIZE_MAX) drag.dropPos = editorPosFromClick(app, drag.x, drag.y);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// The release: before the drag started it was a click, which puts the
// caret where it pressed; after, the text lands where it lets go, a copy
// with Ctrl held
static void editorTextDragRelease(App& app, HWND hwnd, int x, int y) {
    App::EditorTextDrag drag = app.editorTextDrag;
    if (drag.active) {
        drag.dropPos = (float)x < editorPaneWidth(app) ? editorPosFromClick(app, x, y) : SIZE_MAX;
    }
    editorTextDragStop(app, hwnd);
    if (!drag.active) {
        app.editorCursorPos = app.editorSelStart = app.editorSelEnd = drag.pressPos;
        app.editorHasSelection = false;
        app.editorDesiredCol = -1;
        return;
    }
    if (drag.dropPos != SIZE_MAX) {
        editorDropSelection(app, hwnd, drag.dropPos, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
    }
}

// The band the editor's scrollbar takes presses in, at the pane's right
// edge below the tab strip
static bool overEditorScrollbar(const App& app, float x, float y) {
    const float editorWidth = editorPaneWidth(app);
    return app.editorContentHeight > app.height && x >= editorWidth - dpi(app, 14.0f) &&
           x <= editorWidth - dpi(app, 2.0f) && y >= chromeTopHeight(app);
}

void handleEditorMouseDown(App& app, HWND hwnd, int x, int y) {
    float editorWidth = editorPaneWidth(app);

    // The insert menu resolves its rows first (design t9)
    if (editCtxMouseDown(app, hwnd, x, y)) return;

    // The tool rail owns its column (design t8)
    if (editRailMouseDown(app, hwnd, x, y)) return;

    // Editor scrollbar: a track click jumps, and the drag follows (#121).
    // Checked before the separator, whose grab zone overlaps the thumb —
    // the thin scrollbar is the more deliberate target
    if (overEditorScrollbar(app, (float)x, (float)y)) {
        // The track as drawn, below the tab strip (#291)
        const ScrollbarGeometry bar = editorScrollbarV(app);
        if ((float)y < bar.thumbStart || (float)y > bar.thumbStart + bar.thumbLength) {
            // Track click: center the thumb on the click point
            app.editorScrollY = bar.scrollForThumb((float)y - bar.thumbLength * 0.5f);
        }
        app.editorScrollbarDragging = true;
        app.editorScrollbarDragStartY = (float)y;
        app.editorScrollbarDragStartScroll = app.editorScrollY;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // The thread seam doubles as the split handle (design t11)
    if (editSplitPreview(app)) {
        float paneW = editorPaneWidth(app);
        if ((float)x >= paneW && (float)x < paneW + editSeamWidth(app)) {
            app.draggingSeparator = true;
            app.separatorDragStartX = (float)x;
            app.separatorDragStartRatio = app.editorSplitRatio;
            SetCapture(hwnd);
            return;
        }
    }

    // Only handle clicks in the editor pane (left side)
    if (x > editorWidth) return;

    // Commit before hit testing: the cell may have changed source lengths.
    focusSourceEditor(app);
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    size_t clickPos = editorPosFromClick(app, x, y);

    // Alt+click adds a caret, or removes one (#251); any other click starts
    // over from a single caret. The Alt release must not then open the
    // window menu.
    if (GetKeyState(VK_MENU) & 0x8000) {
        editorToggleCaretAt(app, clickPos);
        app.altClickGuard = true;
        app.lastClickTime = {};
        resetCursorBlink(app);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    // A press on the selection may drag it (#289): with a single caret, and
    // not from a finger, whose slide extends the selection (#286)
    const bool onSelection = !shift && app.editorExtraCarets.empty() && !app.touch.active &&
                             editorPointInSelection(app, x, y, clickPos);
    app.editorExtraCarets.clear();

    // Detect double/triple click
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - app.lastClickTime).count();
    bool isRepeat = (elapsed < 500 && std::abs(x - app.lastClickX) < 5 && std::abs(y - app.lastClickY) < 5);

    if (isRepeat) {
        app.clickCount = std::min(app.clickCount + 1, 3);
    } else {
        app.clickCount = 1;
    }
    app.lastClickTime = now;
    app.lastClickX = x;
    app.lastClickY = y;

    if (app.clickCount == 2) {
        app.editorCaretUpstreamPos = std::wstring::npos;
        // Double-click: select run of same character class (handles CJK,
        // which has no spaces between words)
        size_t wordStart = clickPos;
        size_t wordEnd = clickPos;
        int cls = 0;
        if (clickPos < app.editorText.size()) cls = editorCharClass(app.editorText[clickPos]);
        if (cls == 0 && clickPos > 0) {
            // Clicked just past the end of a word: select the run before
            cls = editorCharClass(app.editorText[clickPos - 1]);
            if (cls != 0) { wordStart = clickPos - 1; wordEnd = clickPos; }
        }
        if (cls != 0) {
            while (wordStart > 0 && editorCharClass(app.editorText[wordStart - 1]) == cls) wordStart--;
            while (wordEnd < app.editorText.size() && editorCharClass(app.editorText[wordEnd]) == cls) wordEnd++;
        }
        app.editorSelStart = wordStart;
        app.editorSelEnd = wordEnd;
        app.editorCursorPos = wordEnd;
        app.editorHasSelection = (wordStart != wordEnd);
    } else if (app.clickCount == 3) {
        app.editorCaretUpstreamPos = std::wstring::npos;
        // Triple-click: select line
        size_t line = getLineFromPos(app, clickPos);
        app.editorSelStart = app.editorLineStarts[line];
        app.editorSelEnd = getLineEnd(app, line);
        if (app.editorSelEnd < app.editorText.size()) app.editorSelEnd++; // include \n
        app.editorCursorPos = app.editorSelEnd;
        app.editorHasSelection = true;
    } else {
        // Single click
        if (shift && app.editorHasSelection) {
            // Extend selection
            app.editorSelEnd = clickPos;
            app.editorCursorPos = clickPos;
        } else if (shift) {
            app.editorSelStart = app.editorCursorPos;
            app.editorSelEnd = clickPos;
            app.editorCursorPos = clickPos;
            app.editorHasSelection = true;
        } else if (onSelection) {
            // The release decides: a click puts the caret here, a drag
            // takes the selection along
            app.editorTextDrag = {};
            app.editorTextDrag.armed = true;
            app.editorTextDrag.startX = app.editorTextDrag.x = x;
            app.editorTextDrag.startY = app.editorTextDrag.y = y;
            app.editorTextDrag.pressPos = clickPos;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        } else {
            app.editorCursorPos = clickPos;
            app.editorSelStart = clickPos;
            app.editorSelEnd = clickPos;
            app.editorHasSelection = false;
        }
        app.editorSelecting = true;
        SetCapture(hwnd);
    }

    app.editorDesiredCol = -1;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void handleEditorMouseUp(App& app, HWND hwnd, int x, int y) {
    if (app.editorTextDrag.armed) {
        editorTextDragRelease(app, hwnd, x, y);
        return;
    }
    if (app.editorScrollbarDragging) {
        app.editorScrollbarDragging = false;
        ReleaseCapture();
        return;
    }
    if (app.draggingSeparator) {
        app.draggingSeparator = false;
        ReleaseCapture();
        return;
    }
    if (app.editorSelecting) {
        app.editorSelecting = false;
        ReleaseCapture();
        if (app.editorSelStart == app.editorSelEnd) {
            app.editorHasSelection = false;
        }
    }
}

void handleEditorMouseMove(App& app, HWND hwnd, int x, int y) {
    float editorWidth = editorPaneWidth(app);

    // Hover state for the Read button's fade and the seam's hairline (#245)
    editorReadingButtonHover(app, (float)x, (float)y);
    bool seam = editSplitPreview(app) && (float)y >= chromeTopHeight(app) &&
                (float)x >= editorWidth && (float)x < editorWidth + editSeamWidth(app);
    if (seam != app.editSeamHover) {
        app.editSeamHover = seam;
        if (seam) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    // A dragged selection owns the pointer until its release (#289)
    if (app.editorTextDrag.armed) {
        editorTextDragMove(app, hwnd, x, y);
        return;
    }

    // Insert-menu hover: rows highlight, parents open their submenu
    if (editCtxMouseMove(app, x, y)) {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return;
    }

    // Rail hover (design t8/t11)
    if (editRailMouseMove(app, hwnd, x, y)) {
        SetCursor(LoadCursor(nullptr,
                             app.editRailHover ? IDC_HAND : IDC_ARROW));
        return;
    }

    if (app.editorScrollbarDragging) {
        // The pointer stays an arrow for the whole drag, over the text too (#280)
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        // The thumb moves with the pointer along the track as drawn (#291)
        const ScrollbarGeometry bar = editorScrollbarV(app);
        const float travel = bar.trackLength - bar.thumbLength;
        if (bar.maxScroll > 0 && travel > 0) {
            const float delta = ((float)y - app.editorScrollbarDragStartY) / travel * bar.maxScroll;
            app.editorScrollY = std::clamp(app.editorScrollbarDragStartScroll + delta, 0.0f, bar.maxScroll);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (app.draggingSeparator) {
        static HCURSOR cursorSizeWE = LoadCursor(nullptr, IDC_SIZEWE);
        SetCursor(cursorSizeWE);
        float dx = (float)x - app.separatorDragStartX;
        float newRatio = app.separatorDragStartRatio + dx / app.width;
        app.editorSplitRatio = std::max(0.2f, std::min(0.8f, newRatio));
        app.clearEditorLineLayoutCache();
        app.layoutDirty = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (app.editorSelecting) {
        static HCURSOR cursorIBeam = LoadCursor(nullptr, IDC_IBEAM);
        SetCursor(cursorIBeam);
        size_t pos = editorPosFromClick(app, x, y);
        app.editorSelEnd = pos;
        app.editorCursorPos = pos;
        app.editorHasSelection = (app.editorSelStart != app.editorSelEnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Cursor shape: the seam column is the split handle
    static HCURSOR cursorSizeWE = LoadCursor(nullptr, IDC_SIZEWE);
    static HCURSOR cursorIBeam = LoadCursor(nullptr, IDC_IBEAM);
    static HCURSOR cursorArrow = LoadCursor(nullptr, IDC_ARROW);

    if (app.readButtonHover) {
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return;
    }
    if (editSplitPreview(app) && (float)x >= editorWidth &&
        (float)x < editorWidth + editSeamWidth(app)) {
        SetCursor(cursorSizeWE);
        return;
    } else if (overEditorScrollbar(app, (float)x, (float)y)) {
        SetCursor(cursorArrow);  // a control, not text (#280)
        return;
    } else if (x < editorWidth) {
        SetCursor(cursorIBeam);
        return;
    }
    // Preview pane cursor is handled by the normal handleMouseMove.
}

void handleEditorMouseWheel(App& app, HWND hwnd, float delta) {
    // Shift+wheel pans long unwrapped lines horizontally (#77)
    if (!editorWrapOn(app) && (GetKeyState(VK_SHIFT) & 0x8000)) {
        app.editorScrollX = std::max(0.0f, app.editorScrollX - delta * dpi(app, 60.0f));
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    app.editorScrollY -= delta * dpi(app, 60.0f);
    app.editorScrollY = std::max(0.0f, app.editorScrollY);
    float maxScroll = std::max(0.0f, app.editorContentHeight - app.height);
    app.editorScrollY = std::min(app.editorScrollY, maxScroll);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Rendering ---

// Fill highlight rectangles for a text range within a wrapped line layout.
// HitTestTextRange returns one rect per visual row the range touches.
// One right-aligned number in the slim gutter, level with its line
// (design t11); the caret line's number brightens toward the accent
static void editorDrawLineNumber(App& app, size_t lineIdx, float lineY,
                                 float lineHeight, bool isCaretLine) {
    if (!app.editorGutterFormat) return;
    wchar_t num[16];
    int len = swprintf_s(num, _countof(num), L"%u",
                         (unsigned)(lineIdx + 1));
    if (len <= 0) return;
    float railW = dpi(app, 48.0f);
    D2D1_COLOR_F c = isCaretLine ? app.theme.accent : app.theme.text;
    c.a = isCaretLine ? 0.8f : 0.3f;
    app.brush->SetColor(c);
    app.renderTarget->DrawTextW(
        num, (UINT32)len, app.editorGutterFormat,
        D2D1::RectF(railW, lineY,
                    railW + editorGutterWidth(app) - dpi(app, 8.0f),
                    lineY + lineHeight),
        app.brush);
}

// The caret line's soft accent wash, spanning gutter and text (design t11)
static void editorFillCaretLine(App& app, float lineY, float rowsHeight,
                                float editorWidth) {
    D2D1_COLOR_F c = app.theme.accent;
    c.a = 0.08f;
    app.brush->SetColor(c);
    app.renderTarget->FillRectangle(
        D2D1::RectF(dpi(app, 48.0f), lineY, editorWidth, lineY + rowsHeight),
        app.brush);
}

static void editorFillRangeRects(App& app, IDWriteTextLayout* layout,
                                 float originX, float originY,
                                 size_t rangeStart, size_t rangeLen,
                                 const D2D1_COLOR_F& color) {
    if (!layout || rangeLen == 0) return;
    UINT32 count = 0;
    layout->HitTestTextRange((UINT32)rangeStart, (UINT32)rangeLen, 0, 0,
                             nullptr, 0, &count);
    if (count == 0) return;
    std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
    if (FAILED(layout->HitTestTextRange((UINT32)rangeStart, (UINT32)rangeLen,
                                        0, 0, metrics.data(), count, &count))) {
        return;
    }
    app.brush->SetColor(color);
    for (UINT32 i = 0; i < count; i++) {
        app.renderTarget->FillRectangle(
            D2D1::RectF(originX + metrics[i].left, originY + metrics[i].top,
                        originX + metrics[i].left + metrics[i].width,
                        originY + metrics[i].top + metrics[i].height),
            app.brush);
    }
}

// The extra carets' selections on one logical line, painted like the
// primary selection (#251)
static void editorFillExtraSelections(App& app, IDWriteTextLayout* layout,
                                      float originX, float lineY, size_t lineStart,
                                      size_t lineLen, float lineHeight, float charWidth) {
    const D2D1_COLOR_F color = D2D1::ColorF(0.2f, 0.4f, 0.9f, 0.35f);
    for (const auto& caret : app.editorExtraCarets) {
        if (!caret.hasSelection) continue;
        const size_t start = caretStart(caret), end = caretEnd(caret);
        if (end <= lineStart || start > lineStart + lineLen) continue;
        const size_t from = start > lineStart ? start - lineStart : 0;
        const size_t to = std::min(end - lineStart, lineLen);
        editorFillRangeRects(app, layout, originX, lineY, from, to - from, color);
        if (end > lineStart + lineLen) {
            // Newline included: one cell past the line's end
            float x = 0, y = 0;
            editorCaretXY(layout, lineLen, x, y);
            app.brush->SetColor(color);
            app.renderTarget->FillRectangle(
                D2D1::RectF(originX + x, lineY + y, originX + x + charWidth,
                            lineY + y + lineHeight),
                app.brush);
        }
    }
}

static void editorDrawExtraCarets(App& app, float lineHeight) {
    if (app.editorExtraCarets.empty() || !app.cursorBlinkOn || !sourceEditorHasFocus(app)) {
        return;
    }
    app.brush->SetColor(app.theme.text);
    for (const auto& caret : app.editorExtraCarets) {
        D2D1_POINT_2F point{};
        if (!editorCaretPointAt(app, caret.pos, caret.upstream == caret.pos, point)) continue;
        if (point.y + lineHeight < 0 || point.y > app.height) continue;
        app.renderTarget->FillRectangle(
            D2D1::RectF(point.x, point.y, point.x + dpi(app, 2.0f), point.y + lineHeight),
            app.brush);
    }
}

// Where a dragged selection would land (#289): a steady caret in the
// accent colour
static void editorDrawDropCaret(App& app, float lineHeight) {
    const auto& drag = app.editorTextDrag;
    D2D1_POINT_2F point{};
    if (!drag.active || drag.dropPos == SIZE_MAX ||
        !editorCaretPointAt(app, drag.dropPos, false, point)) {
        return;
    }
    app.brush->SetColor(app.theme.accent);
    app.renderTarget->FillRectangle(
        D2D1::RectF(point.x, point.y, point.x + dpi(app, 2.0f), point.y + lineHeight), app.brush);
}

// The thumb at the editor pane's right edge, on the track below the tab
// strip: drawn from the window's top, it went under the strip (#291)
static void renderEditorScrollbar(App& app, float editorWidth) {
    if (app.editorContentHeight <= app.height) return;
    const ScrollbarGeometry bar = editorScrollbarV(app);
    float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;
    app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, 0.3f));
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(editorWidth - dpi(app, 10.0f), bar.thumbStart,
                                       editorWidth - dpi(app, 4.0f), bar.thumbStart + bar.thumbLength), 3, 3),
        app.brush);
}

// Soft-wrap rendering: each logical line spans editorRowStarts-many visual
// rows; highlights and the caret come from DirectWrite hit testing on the
// wrapped per-line layouts
static void renderEditorWrapped(App& app, float editorWidth) {
    ensureEditorRowMetrics(app);
    if (app.editorRowStarts.size() != app.editorLineStarts.size() + 1) return;

    float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    float padding = dpi(app, 8.0f);
    float textX = editorTextX(app);

    // The source sits directly on the desk — no pane box
    app.brush->SetColor(editDeskColor(app));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, editorWidth, (float)app.height), app.brush);

    app.renderTarget->PushAxisAlignedClip(
        D2D1::RectF(0, 0, editorWidth, (float)app.height),
        D2D1_ANTIALIAS_MODE_ALIASED);

    size_t selMin = 0, selMax = 0;
    if (app.editorHasSelection) {
        selMin = editorSelMin(app);
        selMax = editorSelMax(app);
    }
    bool hasSearchMatches = app.showSearch && !app.searchQuery.empty() &&
                            !app.editorSearchMatches.empty();
    size_t searchScanIdx = 0;

    size_t curLine = getLineFromPos(app, app.editorCursorPos);
    float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth
        : app.editorTextFormat->GetFontSize() * 0.6f;

    size_t firstRow = (size_t)std::max(0.0f, (app.editorScrollY - padding) / lineHeight);
    size_t firstLine = editorLineFromRow(app, firstRow);

    for (size_t i = firstLine; i < app.editorLineStarts.size(); i++) {
        float lineY = chromeTopHeight(app) + padding + app.editorRowStarts[i] * lineHeight - app.editorScrollY;
        if (lineY > app.height) break;

        size_t lineStart = app.editorLineStarts[i];
        size_t lineLen = getLineLength(app, i);
        IDWriteTextLayout* lineLayout = cachedEditorLineLayout(app, lineStart, lineLen);

        // Caret-line wash + gutter number (design t11)
        if (i == curLine && i + 1 < app.editorRowStarts.size()) {
            float rowsH = (app.editorRowStarts[i + 1] - app.editorRowStarts[i]) *
                          lineHeight;
            editorFillCaretLine(app, lineY, rowsH, editorWidth);
        }
        editorDrawLineNumber(app, i, lineY, lineHeight, i == curLine);

        // Selection highlight
        if (app.editorHasSelection && selMax > lineStart && selMin < lineStart + lineLen + 1) {
            size_t hlStart = (selMin > lineStart) ? selMin - lineStart : 0;
            size_t hlEnd = std::min(selMax - lineStart, lineLen);
            D2D1_COLOR_F selColor = D2D1::ColorF(0.2f, 0.4f, 0.9f, 0.35f);
            editorFillRangeRects(app, lineLayout, textX, lineY, hlStart, hlEnd - hlStart, selColor);
            if (selMax > lineStart + lineLen && selMin <= lineStart + lineLen) {
                // Newline included: mark one cell past the end of the last row
                float ex = 0, ey = 0;
                editorCaretXY(lineLayout, lineLen, ex, ey);
                app.brush->SetColor(selColor);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(textX + ex, lineY + ey,
                                textX + ex + charWidth, lineY + ey + lineHeight),
                    app.brush);
            }
        }
        editorFillExtraSelections(app, lineLayout, textX, lineY, lineStart, lineLen,
                                  lineHeight, charWidth);

        // Search match highlights
        if (hasSearchMatches) {
            size_t lineEnd = lineStart + lineLen;
            while (searchScanIdx < app.editorSearchMatches.size() &&
                   app.editorSearchMatches[searchScanIdx].startPos +
                       app.editorSearchMatches[searchScanIdx].length <= lineStart) {
                searchScanIdx++;
            }
            for (size_t si = searchScanIdx; si < app.editorSearchMatches.size(); si++) {
                const auto& m = app.editorSearchMatches[si];
                if (m.startPos >= lineEnd) break;
                size_t overlapStart = std::max(lineStart, m.startPos);
                size_t overlapEnd = std::min(lineEnd, m.startPos + m.length);
                if (overlapStart >= overlapEnd) continue;
                bool isCurrent = ((int)si == app.editorSearchCurrentIndex);
                D2D1_COLOR_F hlColor = isCurrent
                    ? D2D1::ColorF(1.0f, 0.6f, 0.0f, 0.5f)
                    : D2D1::ColorF(1.0f, 0.9f, 0.0f, 0.3f);
                editorFillRangeRects(app, lineLayout, textX, lineY,
                                     overlapStart - lineStart,
                                     overlapEnd - overlapStart, hlColor);
            }
        }

        // Line text
        if (lineLayout) {
            app.brush->SetColor(app.theme.text);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(textX, lineY), lineLayout, app.brush);
        }

        // Caret
        if (app.cursorBlinkOn && sourceEditorHasFocus(app) && i == curLine) {
            D2D1_POINT_2F point{};
            if (editorCaretPoint(app, point)) {
                app.brush->SetColor(app.theme.text);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(point.x, point.y, point.x + dpi(app, 2.0f), point.y + lineHeight),
                    app.brush);
            }
        }

    }
    editorDrawExtraCarets(app, lineHeight);
    editorDrawDropCaret(app, lineHeight);

    // Rows draw offset by the chrome strip, so the scrollable height
    // includes it — otherwise the last strip-height of source can never
    // scroll into view. The bottom keeps the Read pill's band clear (#250)
    app.editorContentHeight = chromeTopHeight(app) + padding +
                              app.editorTotalRows * lineHeight +
                              editorBottomClearance(app, lineHeight);

    renderEditorScrollbar(app, editorWidth);

    app.renderTarget->PopAxisAlignedClip();
}

void renderEditor(App& app, float editorWidth) {
    if (!app.editorTextFormat || app.editorLineStarts.empty()) return;
    sourceHighlightFrame(app);

    if (editorWrapOn(app)) {
        renderEditorWrapped(app, editorWidth);
        return;
    }

    float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    float padding = dpi(app, 8.0f);
    float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth : app.editorTextFormat->GetFontSize() * 0.6f;

    // Editor background: the desk surface
    app.brush->SetColor(editDeskColor(app));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, editorWidth, (float)app.height), app.brush);

    // Clip to editor area
    app.renderTarget->PushAxisAlignedClip(
        D2D1::RectF(0, 0, editorWidth, (float)app.height),
        D2D1_ANTIALIAS_MODE_ALIASED);

    // Calculate visible line range
    int firstVisible = std::max(0, (int)((app.editorScrollY - padding) / lineHeight));
    int lastVisible = (int)((app.editorScrollY + app.height) / lineHeight) + 1;
    lastVisible = std::min(lastVisible, (int)app.editorLineStarts.size() - 1);

    // Selection range
    size_t selMin = 0, selMax = 0;
    if (app.editorHasSelection) {
        selMin = editorSelMin(app);
        selMax = editorSelMax(app);
    }

    size_t curLine = getLineFromPos(app, app.editorCursorPos);

    // Text origin shifts left as the pane scrolls horizontally; the rail +
    // gutter column is repainted after the text so lines slide underneath
    // it (#77)
    float gutterRight = editorTextX(app);
    float textBase = gutterRight - app.editorScrollX;

    // Search match scanning index (both sorted by position, so we advance together)
    size_t searchScanIdx = 0;
    bool hasSearchMatches = app.showSearch && !app.searchQuery.empty() && !app.editorSearchMatches.empty();

    // Advance search scan index to first match that could overlap visible lines
    if (hasSearchMatches && firstVisible > 0) {
        size_t firstVisiblePos = app.editorLineStarts[firstVisible];
        while (searchScanIdx < app.editorSearchMatches.size() &&
               app.editorSearchMatches[searchScanIdx].startPos + app.editorSearchMatches[searchScanIdx].length <= firstVisiblePos) {
            searchScanIdx++;
        }
    }

    for (int i = firstVisible; i <= lastVisible && i < (int)app.editorLineStarts.size(); i++) {
        float lineY = chromeTopHeight(app) + padding + i * lineHeight - app.editorScrollY;
        size_t lineStart = app.editorLineStarts[i];
        size_t lineLen = getLineLength(app, i);

        // One DirectWrite layout per visible line: reused for highlight
        // metrics and drawing so overlays always match the actual glyphs
        // (CJK and other full-width characters are wider than charWidth)
        IDWriteTextLayout* lineLayout = cachedEditorLineLayout(app, lineStart, lineLen);

        // Caret-line wash (design t11); the gutter number comes after the
        // gutter repaint below so it sits above slid text
        if ((size_t)i == curLine) {
            editorFillCaretLine(app, lineY, lineHeight, editorWidth);
        }

        // Selection highlight on this line
        if (app.editorHasSelection && selMax > lineStart && selMin < lineStart + lineLen + 1) {
            size_t hlStart = (selMin > lineStart) ? selMin - lineStart : 0;
            size_t hlEnd = std::min(selMax - lineStart, lineLen + 1);
            float hlX1 = textBase + editorColToX(app, lineLayout, hlStart);
            float hlX2;
            if (hlEnd > lineLen) {
                // Selection includes the newline: extend one char past line end
                hlX2 = textBase + editorColToX(app, lineLayout, lineLen) + charWidth;
            } else {
                hlX2 = textBase + editorColToX(app, lineLayout, hlEnd);
            }
            app.brush->SetColor(D2D1::ColorF(0.2f, 0.4f, 0.9f, 0.35f));
            app.renderTarget->FillRectangle(
                D2D1::RectF(hlX1, lineY, hlX2, lineY + lineHeight), app.brush);
        }
        editorFillExtraSelections(app, lineLayout, textBase, lineY, lineStart, lineLen,
                                  lineHeight, charWidth);

        // Search match highlights on this line
        if (hasSearchMatches) {
            size_t lineEnd = lineStart + lineLen;
            size_t si = searchScanIdx;
            while (si < app.editorSearchMatches.size()) {
                const auto& m = app.editorSearchMatches[si];
                if (m.startPos >= lineEnd) break; // past this line
                size_t mEnd = m.startPos + m.length;
                if (mEnd <= lineStart) { si++; continue; } // before this line

                // Compute overlap with this line
                size_t overlapStart = std::max(lineStart, m.startPos);
                size_t overlapEnd = std::min(lineEnd, mEnd);
                if (overlapStart < overlapEnd) {
                    float hlX1 = textBase + editorColToX(app, lineLayout, overlapStart - lineStart);
                    float hlX2 = textBase + editorColToX(app, lineLayout, overlapEnd - lineStart);

                    bool isCurrent = ((int)si == app.editorSearchCurrentIndex);
                    if (isCurrent) {
                        app.brush->SetColor(D2D1::ColorF(1.0f, 0.6f, 0.0f, 0.5f));  // Orange
                    } else {
                        app.brush->SetColor(D2D1::ColorF(1.0f, 0.9f, 0.0f, 0.3f));  // Yellow
                    }
                    app.renderTarget->FillRectangle(
                        D2D1::RectF(hlX1, lineY, hlX2, lineY + lineHeight), app.brush);
                }
                si++;
            }
            // Advance scan index past matches that ended before or at this line's start
            while (searchScanIdx < app.editorSearchMatches.size() &&
                   app.editorSearchMatches[searchScanIdx].startPos + app.editorSearchMatches[searchScanIdx].length <= lineEnd) {
                searchScanIdx++;
            }
        }

        // Line text
        if (lineLayout) {
            app.brush->SetColor(app.theme.text);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(textBase, lineY), lineLayout, app.brush);
        }

    }

    // Cursor (blink state driven by TIMER_CURSOR_BLINK)
    if (app.cursorBlinkOn && sourceEditorHasFocus(app)) {
        D2D1_POINT_2F point{};
        if (editorCaretPoint(app, point)) {
            app.brush->SetColor(app.theme.text);
            app.renderTarget->FillRectangle(
                D2D1::RectF(point.x, point.y, point.x + dpi(app, 2.0f), point.y + lineHeight), app.brush);
        }
    }
    editorDrawExtraCarets(app, lineHeight);
    editorDrawDropCaret(app, lineHeight);

    // The rail + gutter column last: horizontally scrolled text slides
    // under it, then the line numbers draw on top (design t11)
    app.brush->SetColor(editDeskColor(app));
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, gutterRight, (float)app.height), app.brush);
    for (int i = firstVisible; i <= lastVisible && i < (int)app.editorLineStarts.size(); i++) {
        float lineY = chromeTopHeight(app) + padding + i * lineHeight - app.editorScrollY;
        if ((size_t)i == curLine) {
            // Re-lay the caret wash over the blanked gutter strip
            D2D1_COLOR_F c = app.theme.accent;
            c.a = 0.08f;
            app.brush->SetColor(c);
            app.renderTarget->FillRectangle(
                D2D1::RectF(dpi(app, 48.0f), lineY, gutterRight,
                            lineY + lineHeight),
                app.brush);
        }
        editorDrawLineNumber(app, (size_t)i, lineY, lineHeight,
                             (size_t)i == curLine);
    }

    // Update content height for scrolling
    // Includes the chrome strip offset the rows draw below and the Read
    // pill's band (see the wrapped variant)
    app.editorContentHeight = chromeTopHeight(app) + padding +
                              app.editorLineStarts.size() * lineHeight +
                              editorBottomClearance(app, lineHeight);

    renderEditorScrollbar(app, editorWidth);

    app.renderTarget->PopAxisAlignedClip();
}

// --- Go to line (#276) ---

// The editor's source when it shows, the reading view's otherwise (the
// reading view inside edit mode included)
static bool gotoLineInEditor(const App& app) {
    return app.editMode && !app.editorReadingPreview;
}

size_t gotoLineCount(const App& app) {
    // Inside edit mode both views show the editor's text
    if (app.editMode) return std::max<size_t>(1, app.editorLineStarts.size());
    return 1 + (size_t)std::count(app.sourceText.begin(), app.sourceText.end(), '\n');
}

void gotoLineOpenPrompt(App& app) {
    if (app.showSearch) closeSearchInput(app);  // both sit at the top
    app.gotoLineOpen = true;
    app.gotoLineInput.clear();
}

void gotoLineJump(App& app, size_t line) {
    line = std::clamp<size_t>(line, 1, gotoLineCount(app));
    if (gotoLineInEditor(app)) {
        app.editorExtraCarets.clear();
        app.editorHasSelection = false;
        app.editorCursorPos = app.editorLineStarts[line - 1];
        app.editorSelStart = app.editorSelEnd = app.editorCursorPos;
        app.editorDesiredCol = -1;
        app.editorCaretUpstreamPos = std::wstring::npos;
        // The line a little above the middle, as search matches land (#260)
        ensureEditorRowMetrics(app);
        const float viewportH = (float)app.height - chromeTopHeight(app);
        app.editorScrollY = std::max(0.0f, editorPosRowY(app, app.editorCursorPos, false) - viewportH * 0.4f);
        editorEnsureCursorVisible(app);
        resetCursorBlink(app);
        return;
    }
    // The reading view: through the scroll anchors to where the line's
    // block is drawn. An anchor's offset is its block's first text, after
    // markers such as "## " or "- ", so the last anchor up to the line's
    // end is the block that starts on the line, or the one it continues,
    // as for the caret's block in the preview. The offsets count in the
    // text that was parsed: the editor's inside edit mode, whose reading
    // view may show edits the viewer's source has not seen.
    size_t end = SIZE_MAX;  // the last line: every anchor
    if (app.editMode) {
        if (line < app.editorLineByteOffsets.size()) end = app.editorLineByteOffsets[line] - 1;
    } else {
        size_t start = 0;
        for (size_t i = 0, current = 1; i < app.sourceText.size() && current < line; ++i) {
            if (app.sourceText[i] == '\n') {
                ++current;
                start = i + 1;
            }
        }
        end = std::min(end, app.sourceText.find('\n', start));
    }
    ensureLayoutComplete(app);  // anchors far down exist only once laid out
    float y = 0.0f;
    for (const auto& anchor : app.scrollAnchors) {
        if (anchor.sourceOffset > end) break;
        y = anchor.renderedY - documentContentTop(app);
    }
    const float maxScroll = std::max(0.0f, app.contentHeight - (float)app.height);
    app.scrollY = app.targetScrollY = std::clamp(y, 0.0f, maxScroll);
}

bool gotoLineKeyDown(App& app, HWND hwnd, WPARAM key) {
    if (key == VK_ESCAPE || key == VK_RETURN) {
        const std::wstring typed = app.gotoLineInput;
        app.gotoLineOpen = false;
        app.gotoLineInput.clear();
        if (key == VK_RETURN && !typed.empty()) gotoLineJump(app, (size_t)std::wcstoull(typed.c_str(), nullptr, 10));
    } else if (key == VK_BACK) {
        if (!app.gotoLineInput.empty()) app.gotoLineInput.pop_back();
    } else {
        // Every other key stays with the prompt but is left unconsumed: the
        // message loop translates only unconsumed keys into the characters
        // gotoLineChar reads (#282), in whatever layout types the digits
        return false;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    // Consumed, so Enter, Esc and Backspace make no character for the
    // editor once the prompt has closed
    return true;
}

void gotoLineChar(App& app, wchar_t ch) {
    if (ch >= L'0' && ch <= L'9' && app.gotoLineInput.size() < 9) app.gotoLineInput += ch;
}

// Centred at the top of the pane it acts on
D2D1_RECT_F gotoLinePromptBox(const App& app) {
    const float paneLeft = gotoLineInEditor(app) ? editRailWidth(app) : documentViewportX(app);
    const float paneWidth = gotoLineInEditor(app) ? editorPaneWidth(app) - editRailWidth(app)
                                                  : documentViewportWidth(app);
    const float w = std::min(dpi(app, 300.0f), paneWidth - dpi(app, 16.0f));
    const float h = dpi(app, 38.0f);
    const float x = paneLeft + (paneWidth - w) * 0.5f;
    const float y = chromeTopHeight(app) + dpi(app, 12.0f);
    return D2D1::RectF(x, y, x + w, y + h);
}

bool gotoLinePromptHit(const App& app, float x, float y) {
    if (!app.gotoLineOpen) return false;
    const D2D1_RECT_F box = gotoLinePromptBox(app);
    return x >= box.left && x < box.right && y >= box.top && y < box.bottom;
}

void renderGotoLinePrompt(App& app) {
    IDWriteTextFormat* format = app.searchTextFormat;
    if (!app.gotoLineOpen || !format) return;
    const D2D1_RECT_F box = gotoLinePromptBox(app);
    const float h = box.bottom - box.top;
    D2D1_COLOR_F shadow = D2D1::ColorF(0, 0, 0, app.theme.isDark ? 0.35f : 0.12f);
    app.brush->SetColor(shadow);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(box.left, box.top + dpi(app, 2.0f), box.right, box.bottom + dpi(app, 3.0f)),
                          dpi(app, 9.0f), dpi(app, 9.0f)), app.brush);
    app.brush->SetColor(app.theme.background);
    app.renderTarget->FillRoundedRectangle(D2D1::RoundedRect(box, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush);
    D2D1_COLOR_F border = app.theme.accent;
    border.a = 0.6f;
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(D2D1::RoundedRect(box, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush,
                                           dpi(app, 1.2f));
    // "Go to line  123|        1–N"
    D2D1_COLOR_F muted = app.theme.text;
    muted.a = 0.55f;
    const float pad = dpi(app, 12.0f);
    // The format draws from the top: centre its line in the box
    const float textTop = box.top + (h - format->GetFontSize() * 1.33f) * 0.5f;
    const std::wstring label = tr(app, "editor.goto_line");
    const float labelW = measureText(app, label.c_str(), format);
    app.brush->SetColor(muted);
    app.renderTarget->DrawText(label.c_str(), (UINT32)label.size(), format,
                               D2D1::RectF(box.left + pad, textTop, box.right - pad, box.bottom), app.brush);
    const std::wstring range = L"1\u2013" + std::to_wstring(gotoLineCount(app));
    const float rangeW = measureText(app, range.c_str(), format);
    app.renderTarget->DrawText(range.c_str(), (UINT32)range.size(), format,
                               D2D1::RectF(box.right - pad - rangeW, textTop, box.right - pad, box.bottom), app.brush);
    const float inputX = box.left + pad + labelW + dpi(app, 12.0f);
    app.brush->SetColor(app.theme.text);
    app.renderTarget->DrawText(app.gotoLineInput.c_str(), (UINT32)app.gotoLineInput.size(), format,
                               D2D1::RectF(inputX, textTop, box.right - pad - rangeW, box.bottom), app.brush);
    const float caretX = inputX + measureText(app, app.gotoLineInput.c_str(), format) + dpi(app, 1.0f);
    app.brush->SetColor(app.theme.accent);
    app.renderTarget->FillRectangle(
        D2D1::RectF(caretX, box.top + h * 0.25f, caretX + dpi(app, 1.5f), box.bottom - h * 0.25f), app.brush);
}
