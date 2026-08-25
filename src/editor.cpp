#include "editor.h"
#include "document.h"
#include "drafts.h"
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

#include "export.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
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

// Width available for line text in the editor pane (after gutter + padding)
static float editorTextMaxWidth(const App& app) {
    float gutterWidth = dpi(app, 48.0f);
    float padding = dpi(app, 8.0f);
    if (app.editorWysiwyg) {
        // The canvas reads like a page: a capped centered column
        return std::min(
            dpi(app, 680.0f),
            std::max(10.0f, editorPaneWidth(app) - gutterWidth -
                                dpi(app, 40.0f) * 2.0f));
    }
    return std::max(10.0f, editorPaneWidth(app) - gutterWidth - padding * 2.0f);
}

// Left edge of the line text: raw sits right of the rail column,
// the WYSIWYG column centers in the remaining pane
static float editorTextX(const App& app) {
    float base = dpi(app, 48.0f) + dpi(app, 8.0f);
    if (!app.editorWysiwyg) return base;
    float railW = dpi(app, 48.0f);
    return std::max(base,
                    railW + (editorPaneWidth(app) - railW -
                             editorTextMaxWidth(app)) * 0.5f);
}

// --- WYSIWYG line styling (design t8) -----------------------------------

// Leading-# heading level (1..6) when the line is a heading
static int wysiwygHeadingLevel(const wchar_t* t, size_t len) {
    size_t h = 0;
    while (h < len && t[h] == L'#') h++;
    if (h == 0 || h > 6 || h >= len || t[h] != L' ') return 0;
    return (int)h;
}

// Grid rows a line's base style occupies (H1/H2 span two rows)
static int wysiwygRowMul(const App& app, size_t lineStart, size_t lineLen) {
    int lvl = wysiwygHeadingLevel(app.editorText.data() + lineStart, lineLen);
    return (lvl == 1 || lvl == 2) ? 2 : 1;
}

static void wysiwygEnsureBrushes(App& app) {
    if (!app.renderTarget) return;
    if (!app.editorDimBrush) {
        D2D1_COLOR_F c = app.theme.text;
        c.a = 0.4f;
        app.renderTarget->CreateSolidColorBrush(c, &app.editorDimBrush);
    }
    if (!app.editorCodeBrush) {
        app.renderTarget->CreateSolidColorBrush(app.theme.code,
                                                &app.editorCodeBrush);
    }
}

// Styled source: markdown stays visible but dimmed, content renders with
// its real weight and size — headings big, **bold** bold, `code` mono
static void wysiwygStyleLayout(App& app, IDWriteTextLayout* layout,
                               size_t lineStart, size_t lineLen) {
    const wchar_t* t = app.editorText.data() + lineStart;
    float base = app.wysiwygTextFormat->GetFontSize();
    float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    wysiwygEnsureBrushes(app);

    int lvl = wysiwygHeadingLevel(t, lineLen);
    int rowMul = (lvl == 1 || lvl == 2) ? 2 : 1;
    layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                           rowMul * lineHeight, rowMul * lineHeight * 0.76f);

    auto dim = [&](size_t s, size_t n) {
        if (app.editorDimBrush && n > 0) {
            DWRITE_TEXT_RANGE r{(UINT32)s, (UINT32)n};
            layout->SetDrawingEffect(app.editorDimBrush, r);
        }
    };

    DWRITE_TEXT_RANGE all{0, (UINT32)lineLen};
    if (lvl) {
        float size = lvl == 1   ? base * 1.6f
                     : lvl == 2 ? base * 1.32f
                                : base * 1.12f;
        layout->SetFontSize(size, all);
        layout->SetFontWeight(lvl == 1 ? DWRITE_FONT_WEIGHT_EXTRA_BOLD
                                       : DWRITE_FONT_WEIGHT_BOLD,
                              all);
        DWRITE_TEXT_RANGE marker{0, (UINT32)std::min<size_t>(lvl + 1, lineLen)};
        layout->SetFontSize(base * 0.85f, marker);
        layout->SetFontWeight(DWRITE_FONT_WEIGHT_NORMAL, marker);
        dim(0, marker.length);
    } else if (lineLen >= 2 && t[0] == L'>' && t[1] == L' ') {
        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, all);
        dim(0, 2);
    }

    // Inline code first: its content is opaque to the other markers
    std::vector<std::pair<size_t, size_t>> codeSpans;
    for (size_t i = 0; i + 1 < lineLen; i++) {
        if (t[i] != L'`') continue;
        size_t close = i + 1;
        while (close < lineLen && t[close] != L'`') close++;
        if (close >= lineLen) break;
        if (close > i + 1) {
            DWRITE_TEXT_RANGE inner{(UINT32)(i + 1),
                                    (UINT32)(close - i - 1)};
            layout->SetFontFamilyName(app.theme.codeFontFamily, inner);
            layout->SetFontSize(base * 0.9f, inner);
            if (app.editorCodeBrush) {
                layout->SetDrawingEffect(app.editorCodeBrush, inner);
            }
        }
        dim(i, 1);
        dim(close, 1);
        codeSpans.push_back({i, close});
        i = close;
    }
    auto inCode = [&](size_t p) {
        for (const auto& cs : codeSpans) {
            if (p >= cs.first && p <= cs.second) return true;
        }
        return false;
    };

    auto styleSpans = [&](const wchar_t* mark, size_t ml, int kind) {
        size_t i = 0;
        while (i + 2 * ml <= lineLen) {
            bool open = !inCode(i) && wcsncmp(t + i, mark, ml) == 0 &&
                        i + ml < lineLen && t[i + ml] != L' ';
            if (ml == 1 && mark[0] == L'*' && open) {
                // A lone * that is really part of ** stays untouched
                if ((i + 1 < lineLen && t[i + 1] == L'*') ||
                    (i > 0 && t[i - 1] == L'*')) {
                    open = false;
                }
            }
            if (!open) {
                i++;
                continue;
            }
            size_t close = SIZE_MAX;
            for (size_t j = i + ml; j + ml <= lineLen; j++) {
                if (inCode(j) || wcsncmp(t + j, mark, ml) != 0) continue;
                if (t[j - 1] == L' ') continue;
                if (ml == 1 && mark[0] == L'*' &&
                    ((j + 1 < lineLen && t[j + 1] == L'*') ||
                     t[j - 1] == L'*')) {
                    continue;
                }
                close = j;
                break;
            }
            if (close == SIZE_MAX) {
                i += ml;
                continue;
            }
            DWRITE_TEXT_RANGE inner{(UINT32)(i + ml),
                                    (UINT32)(close - i - ml)};
            if (kind == 1) {
                layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, inner);
            } else if (kind == 2) {
                layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, inner);
            } else {
                layout->SetStrikethrough(TRUE, inner);
            }
            dim(i, ml);
            dim(close, ml);
            i = close + ml;
        }
    };
    styleSpans(L"**", 2, 1);
    styleSpans(L"~~", 2, 3);
    styleSpans(L"*", 1, 2);
}

static IDWriteTextLayout* createEditorLineLayout(App& app, size_t lineStart, size_t lineLen) {
    if (!app.dwriteFactory || !app.editorTextFormat || lineLen == 0) return nullptr;
    bool wysiwyg = app.editorWysiwyg && app.wysiwygTextFormat;
    IDWriteTextFormat* baseFormat =
        wysiwyg ? app.wysiwygTextFormat : app.editorTextFormat;
    float maxWidth = editorWrapOn(app) ? editorTextMaxWidth(app) : 1e7f;
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(
        app.editorText.data() + lineStart, (UINT32)lineLen,
        baseFormat, maxWidth, 1e7f, &layout);
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
    if (layout && wysiwyg) {
        wysiwygStyleLayout(app, layout, lineStart, lineLen);
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
static void editorCaretXY(IDWriteTextLayout* layout, size_t col, float& x, float& y) {
    x = 0.0f;
    y = 0.0f;
    if (!layout) return;
    FLOAT hx = 0, hy = 0;
    DWRITE_HIT_TEST_METRICS m{};
    HRESULT hr = (col == 0)
        ? layout->HitTestTextPosition(0, FALSE, &hx, &hy, &m)
        : layout->HitTestTextPosition((UINT32)(col - 1), TRUE, &hx, &hy, &m);
    if (SUCCEEDED(hr)) {
        x = hx;
        y = hy;
    }
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
    if (!editorWrapOn(app) || !app.editMode) return;

    float maxTextWidth = editorTextMaxWidth(app);
    app.editorRowMetricsWidth = maxTextWidth;
    float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth
        : (app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 0.6f : 10.0f);

    float lineHeight = app.editorTextFormat
                           ? app.editorTextFormat->GetFontSize() * 1.5f
                           : 20.0f;
    size_t lineCount = app.editorLineStarts.size();
    app.editorRowStarts.reserve(lineCount + 1);
    app.editorRowStarts.push_back(0);
    for (size_t i = 0; i < lineCount; i++) {
        size_t lineLen = getLineLength(app, i);
        // A line can't wrap unless it could exceed the pane width even at
        // full-width glyph advances (2x the ASCII cell) — skip the layout
        // for the common short line. In WYSIWYG the base row count comes
        // from the line's style (H1/H2 span two grid rows) and long lines
        // measure their real height against the uniform grid.
        size_t rows = app.editorWysiwyg
                          ? (size_t)wysiwygRowMul(app, app.editorLineStarts[i],
                                                  lineLen)
                          : 1;
        if (lineLen > 0 && (float)lineLen * charWidth * 2.0f > maxTextWidth) {
            IDWriteTextLayout* layout =
                createEditorLineLayout(app, app.editorLineStarts[i], lineLen);
            if (layout) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(layout->GetMetrics(&tm)) && tm.lineCount > 0) {
                    rows = app.editorWysiwyg
                               ? (size_t)std::max(
                                     1.0f,
                                     floorf(tm.height / lineHeight + 0.5f))
                               : tm.lineCount;
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

// Up/Down in wrap mode moves by VISUAL row, staying near the same x —
// within a wrapped line first, then into the adjacent logical line
static void editorMoveCursorVertical(App& app, bool down) {
    ensureEditorRowMetrics(app);
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    size_t line = getLineFromPos(app, app.editorCursorPos);
    size_t col = app.editorCursorPos - app.editorLineStarts[line];

    IDWriteTextLayout* layout = createEditorLineLayout(
        app, app.editorLineStarts[line], getLineLength(app, line));
    float cx = 0, cy = 0;
    editorCaretXY(layout, col, cx, cy);
    if (app.editorDesiredCol < 0) {
        app.editorDesiredX = cx;
        app.editorDesiredCol = 0;  // wrap mode uses the flag only; x is authoritative
    }

    auto hitCol = [&](IDWriteTextLayout* lay, float x, float y, size_t lineLen) {
        if (!lay) return (size_t)0;
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS m{};
        lay->HitTestPoint(x, y, &trailing, &inside, &m);
        size_t c = (size_t)m.textPosition + (trailing ? (size_t)m.length : 0);
        return std::min(c, lineLen);
    };

    bool moved = false;
    if (layout) {
        DWRITE_TEXT_METRICS tm{};
        float layoutHeight = SUCCEEDED(layout->GetMetrics(&tm)) ? tm.height : lineHeight;
        float targetY = cy + (down ? lineHeight : -lineHeight) + lineHeight * 0.5f;
        if (targetY >= 0.0f && targetY < layoutHeight) {
            app.editorCursorPos = app.editorLineStarts[line] +
                hitCol(layout, app.editorDesiredX, targetY, getLineLength(app, line));
            moved = true;
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
        if (!down && adjacentLayout) {
            // entering from below: land on the LAST visual row
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(adjacentLayout->GetMetrics(&tm))) {
                targetY = tm.height - lineHeight * 0.5f;
            }
        }
        app.editorCursorPos = app.editorLineStarts[adjacent] +
            hitCol(adjacentLayout, app.editorDesiredX, targetY, adjacentLen);
        if (adjacentLayout) adjacentLayout->Release();
    }
}

// --- Undo/Redo ---

static void pushUndo(App& app, App::EditAction::Type type, size_t pos,
                      const std::wstring& text, size_t curBefore, size_t curAfter) {
    // Coalesce consecutive single-char inserts
    if (type == App::EditAction::Insert && text.size() == 1 && !app.undoStack.empty()) {
        auto& last = app.undoStack.back();
        if (last.type == App::EditAction::Insert &&
            last.position + last.text.size() == pos &&
            text[0] != L'\n' && text[0] != L' ') {
            last.text += text;
            last.cursorAfter = curAfter;
            return;
        }
    }
    app.undoStack.push_back({type, pos, text, curBefore, curAfter});
    app.redoStack.clear();
}

static void editorUndo(App& app) {
    if (app.undoStack.empty()) return;
    auto action = app.undoStack.back();
    app.undoStack.pop_back();

    if (action.type == App::EditAction::Insert) {
        // Reverse: delete the inserted text
        app.editorText.erase(action.position, action.text.size());
        app.editorCursorPos = action.cursorBefore;
        app.redoStack.push_back(action);
    } else {
        // Reverse: re-insert the deleted text
        app.editorText.insert(action.position, action.text);
        app.editorCursorPos = action.cursorBefore;
        app.redoStack.push_back(action);
    }
    rebuildLineStarts(app);
    app.editorHasSelection = false;
    app.editorDesiredCol = -1;
}

static void editorRedo(App& app) {
    if (app.redoStack.empty()) return;
    auto action = app.redoStack.back();
    app.redoStack.pop_back();

    if (action.type == App::EditAction::Insert) {
        app.editorText.insert(action.position, action.text);
        app.editorCursorPos = action.cursorAfter;
        app.undoStack.push_back(action);
    } else {
        app.editorText.erase(action.position, action.text.size());
        app.editorCursorPos = action.cursorAfter;
        app.undoStack.push_back(action);
    }
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

// --- Clipboard ---

static void editorCopyToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        wchar_t* ptr = (wchar_t*)GlobalLock(hMem);
        memcpy(ptr, text.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}

static std::wstring editorGetClipboard(HWND hwnd) {
    if (!OpenClipboard(hwnd)) return {};
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

static void editorEnsureCursorVisible(App& app) {
    if (app.editorLineStarts.empty()) return;
    size_t line = getLineFromPos(app, app.editorCursorPos);
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float padding = dpi(app, 8.0f);
    float cursorY;
    if (editorWrapOn(app)) {
        ensureEditorRowMetrics(app);
        IDWriteTextLayout* layout = createEditorLineLayout(
            app, app.editorLineStarts[line], getLineLength(app, line));
        float cx = 0, cy = 0;
        editorCaretXY(layout, app.editorCursorPos - app.editorLineStarts[line], cx, cy);
        if (layout) layout->Release();
        size_t rowStart = (line < app.editorRowStarts.size()) ? app.editorRowStarts[line] : line;
        cursorY = padding + rowStart * lineHeight + cy;
    } else {
        cursorY = padding + line * lineHeight;
    }

    float viewportH = (float)app.height - chromeTopHeight(app);
    if (cursorY < app.editorScrollY + lineHeight) {
        app.editorScrollY = std::max(0.0f, cursorY - lineHeight);
    }
    if (cursorY + lineHeight > app.editorScrollY + viewportH - lineHeight) {
        app.editorScrollY = cursorY + lineHeight * 2 - viewportH;
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
        editorCaretXY(layout, app.editorCursorPos - app.editorLineStarts[line], cx, cy);
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

    const auto& match = app.editorSearchMatches[app.editorSearchCurrentIndex];

    // Move cursor to match position
    app.editorCursorPos = match.startPos;
    app.editorDesiredCol = -1;

    // Find the line containing the match
    size_t line = getLineFromPos(app, match.startPos);
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float padding = 8.0f;
    float matchY = padding + line * lineHeight;

    // Center match in viewport
    app.editorScrollY = matchY - app.height / 2.0f;
    app.editorScrollY = std::max(0.0f, app.editorScrollY);
    float maxScroll = std::max(0.0f, app.editorContentHeight - app.height);
    if (maxScroll > 0) {
        app.editorScrollY = std::min(app.editorScrollY, maxScroll);
    }
}

// --- Debounced reparse ---

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
    // No preview pane — nothing to keep in sync until it's shown again
    if (!app.editorShowPreview) return;
    // Debounce: coalesce rapid typing into one reparse per pause. WM_TIMER
    // calls editorReparse, which kills the timer. 300ms so brief pauses
    // mid-typing (common with IME input) don't trigger a full preview
    // relayout on every committed character.
    SetTimer(app.hwnd, TIMER_EDITOR_REPARSE, 300, nullptr);
}

void editorReparse(App& app) {
    KillTimer(app.hwnd, TIMER_EDITOR_REPARSE);
    if (!app.editMode || !app.editorShowPreview) return;
    std::string utf8 = toUtf8(app.editorText);

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
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

// --- Mode transitions ---

static void enterEditModeWithContent(App& app, const std::string& content) {
    app.editorText = fromUtf8(content);
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
    // where the user was, not at the top (#77). Anchors carry UTF-8 source
    // offsets; count newlines in the raw content to get the line number
    // (unaffected by the \r\n normalization below).
    size_t targetLine = 0;
    if (app.scrollY > 1.0f && !app.scrollAnchors.empty()) {
        size_t lo = 0, hi = app.scrollAnchors.size();
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) / 2;
            if (app.scrollAnchors[mid].renderedY <= app.scrollY) lo = mid;
            else hi = mid;
        }
        size_t srcOffset = std::min(app.scrollAnchors[lo].sourceOffset, content.size());
        for (size_t i = 0; i < srcOffset; i++) {
            if (content[i] == '\n') targetLine++;
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

    // Show notification
    app.editorNotificationMsg = tr(app, "toast.exit_edit_hint");
    app.showEditModeNotification = true;
    app.editModeNotificationAlpha = 1.0f;
    app.editModeNotificationStart = std::chrono::steady_clock::now();
    startNotificationTimer(app);
    updateBlinkTimer(app);

    // Force layout at new width
    app.focusMermaidOnNextLayout = isMermaidDocumentPath(app.currentFile);
    app.layoutDirty = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void restoreEditBuffer(App& app, const std::wstring& text, bool dirty,
                       float scrollY, size_t cursor) {
    enterEditModeWithContent(app, toUtf8(text));
    app.editorDirty = dirty;
    app.editorCursorPos = std::min(cursor, app.editorText.size());
    app.editorScrollY = std::max(0.0f, scrollY);
    // No re-entry toast: the user is returning to their own session
    app.showEditModeNotification = false;
    updateWindowTitle(app);
}

void enterEditMode(App& app) {
    if (app.currentFile.empty()) {
        // Show brief "No file loaded" notification
        app.editorNotificationMsg = tr(app, "toast.no_file");
        app.showEditModeNotification = true;
        app.editModeNotificationAlpha = 1.0f;
        app.editModeNotificationStart = std::chrono::steady_clock::now();
        startNotificationTimer(app);
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }

    // Load raw file content
    std::wstring widePath = toWide(app.currentFile);
    std::ifstream file(widePath, std::ios::binary);
    if (!file) return;

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
    app.clearEditorLineLayoutCache();
    app.editorText.clear();
    app.editorLineStarts.clear();
    app.undoStack.clear();
    app.redoStack.clear();
    app.editorSearchMatches.clear();
    app.editorSearchCurrentIndex = 0;
    // Close search if open
    if (app.showSearch) {
        app.showSearch = false;
        app.searchActive = false;
        app.searchQuery.clear();
        app.searchAnimation = 0;
    }
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
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
        }
    }

    // Update window title (remove dirty marker)
    updateWindowTitle(app);

    app.focusMermaidOnNextLayout = isMermaidDocumentPath(app.currentFile);
    app.layoutDirty = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// --- File save ---

// Classic Save As dialog for untitled quick notes (Ctrl+N). Returns false
// when the user cancels; on success app.currentFile carries the new path
// and the note becomes a normal file-backed session.
static bool promptSaveAsPath(App& app, HWND hwnd) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Markdown (*.md)\0*.md;*.markdown\0"
                      L"Mermaid (*.mmd)\0*.mmd\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"md";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return false;
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

void quickNoteOpenFile(App& app, HWND hwnd) {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Markdown / Mermaid (*.md;*.markdown;*.mmd)\0"
                      L"*.md;*.markdown;*.mmd\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;

    // The picked document takes over: open it as a tab (dedupe and
    // activation included — this parks and leaves the empty editor),
    // then drop the untitled shell tab
    tabsInit(app);
    int untitledId = app.tabs[app.activeTab].id;
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
    if (!quickNoteEmptyStateActive(app) || !app.editorShowPreview) return;
    IDWriteTextFormat* format = app.folderBrowserFormat;
    if (!format || !app.renderTarget || !app.brush || !app.dwriteFactory)
        return;

    float paneX = (float)app.width * app.editorSplitRatio;
    float paneW = (float)app.width - paneX;
    if (paneW < dpi(app, 180.0f)) return;  // pane too narrow to bother

    const wchar_t* label = tr(app, "empty.open_button");
    const wchar_t* chip = L"Ctrl+O";

    IDWriteTextLayout* labelLayout = nullptr;
    IDWriteTextLayout* chipLayout = nullptr;
    app.dwriteFactory->CreateTextLayout(label, (UINT32)wcslen(label), format,
                                        2000.0f, 100.0f, &labelLayout);
    app.dwriteFactory->CreateTextLayout(chip, (UINT32)wcslen(chip), format,
                                        2000.0f, 100.0f, &chipLayout);
    if (!labelLayout || !chipLayout) {
        if (labelLayout) labelLayout->Release();
        if (chipLayout) chipLayout->Release();
        return;
    }
    DWRITE_TEXT_METRICS lm{}, cm{};
    labelLayout->GetMetrics(&lm);
    chipLayout->GetMetrics(&cm);

    // Button geometry: folder icon, label, and a Ctrl+O keycap chip
    float iconW = dpi(app, 20.0f);
    float chipW = cm.width + dpi(app, 16.0f);
    float btnW = dpi(app, 18.0f) + iconW + dpi(app, 10.0f) + lm.width +
                 dpi(app, 12.0f) + chipW + dpi(app, 14.0f);
    float btnH = dpi(app, 44.0f);
    float chromeTop = chromeTopHeight(app);
    float cx = paneX + paneW * 0.5f;
    float cy = chromeTop + ((float)app.height - chromeTop) * 0.5f -
               dpi(app, 40.0f);
    D2D1_RECT_F btn = D2D1::RectF(cx - btnW * 0.5f, cy - btnH * 0.5f,
                                  cx + btnW * 0.5f, cy + btnH * 0.5f);
    app.quickNoteButtonRect = btn;

    // Raised card over the empty pane; a touch stronger under the cursor
    D2D1_COLOR_F fill = app.theme.text;
    fill.a = app.quickNoteButtonHover ? 0.10f : 0.06f;
    app.brush->SetColor(fill);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(btn, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush);
    D2D1_COLOR_F border = app.theme.text;
    border.a = 0.12f;
    app.brush->SetColor(border);
    app.renderTarget->DrawRoundedRectangle(
        D2D1::RoundedRect(btn, dpi(app, 8.0f), dpi(app, 8.0f)), app.brush,
        1.0f);

    // Folder icon: tab flap over a rounded body, in the accent color
    float ix = btn.left + dpi(app, 18.0f);
    float iy = cy - dpi(app, 7.5f);
    app.brush->SetColor(app.theme.accent);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(ix, iy, ix + dpi(app, 9.0f),
                                      iy + dpi(app, 6.0f)),
                          dpi(app, 1.5f), dpi(app, 1.5f)),
        app.brush);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(ix, iy + dpi(app, 2.5f), ix + iconW,
                                      iy + dpi(app, 15.0f)),
                          dpi(app, 2.0f), dpi(app, 2.0f)),
        app.brush);

    // Label
    float labelX = ix + iconW + dpi(app, 10.0f);
    app.brush->SetColor(app.theme.text);
    app.renderTarget->DrawTextLayout(
        D2D1::Point2F(labelX, cy - lm.height * 0.5f), labelLayout, app.brush);

    // Ctrl+O keycap chip
    float chipX = labelX + lm.width + dpi(app, 12.0f);
    float chipH = dpi(app, 22.0f);
    D2D1_RECT_F chipRect = D2D1::RectF(chipX, cy - chipH * 0.5f,
                                       chipX + chipW, cy + chipH * 0.5f);
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
        D2D1::Point2F(chipX + dpi(app, 8.0f), cy - cm.height * 0.5f),
        chipLayout, app.brush);
    labelLayout->Release();
    chipLayout->Release();

    // Hint lines, centered under the button
    const char* hintKeys[2] = { "empty.hint1", "empty.hint2" };
    float hintY = btn.bottom + dpi(app, 18.0f);
    for (const char* key : hintKeys) {
        const wchar_t* hint = tr(app, key);
        IDWriteTextLayout* layout = nullptr;
        app.dwriteFactory->CreateTextLayout(hint, (UINT32)wcslen(hint),
                                            format, 2000.0f, 100.0f, &layout);
        if (layout) {
            DWRITE_TEXT_METRICS hm{};
            layout->GetMetrics(&hm);
            D2D1_COLOR_F muted = app.theme.text;
            muted.a = 0.5f;
            app.brush->SetColor(muted);
            app.renderTarget->DrawTextLayout(
                D2D1::Point2F(cx - hm.width * 0.5f, hintY), layout,
                app.brush);
            hintY += hm.height + dpi(app, 4.0f);
            layout->Release();
        }
    }
}

void saveEditorFile(App& app, HWND hwnd) {
    if (app.currentFile.empty()) {
        // Untitled quick note: name it now; cancel keeps editing untitled
        if (!promptSaveAsPath(app, hwnd)) return;
    }

    std::string utf8 = toUtf8(app.editorText);

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

    std::ofstream out(widePath, std::ios::binary);
    if (out) {
        out.write(utf8.data(), utf8.size());
        out.close();
        app.editorDirty = false;
        updateFileWriteTime(app);

        // The saved buffer no longer needs its crash-recovery draft
        tabsInit(app);
        draftsDeleteForTab(app, app.tabs[app.activeTab].id);

        // Reparse and update preview immediately
        editorReparse(app);

        // Show "Saved!" notification
        app.editorNotificationMsg = tr(app, "toast.saved");
        app.showEditModeNotification = true;
        app.editModeNotificationAlpha = 1.0f;
        app.editModeNotificationStart = std::chrono::steady_clock::now();
        startNotificationTimer(app);

        // Update window title
        updateWindowTitle(app);

        InvalidateRect(hwnd, nullptr, FALSE);
    } else {
        // Surface the failure — a silent no-op here leaves the document
        // permanently dirty and traps the user in the exit-confirm prompt
        app.editorNotificationMsg = tr(app, "toast.save_failed");
        app.showEditModeNotification = true;
        app.editModeNotificationAlpha = 1.0f;
        app.editModeNotificationStart = std::chrono::steady_clock::now();
        startNotificationTimer(app);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// Save As (#121): pick a new path, write there, and the tab follows
void saveEditorFileAs(App& app, HWND hwnd) {
    if (!promptSaveAsPath(app, hwnd)) return;  // cancel keeps the old path
    app.editorDirty = true;  // force the write even for a clean buffer
    saveEditorFile(app, hwnd);
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
    if (!ok) {
        app.currentFile = previous;
        app.copiedNotificationKey = "toast.save_failed";
    } else {
        tabsInit(app);
        App::DocTab& tab = app.tabs[app.activeTab];
        tab.path = app.currentFile;
        size_t slash = app.currentFile.find_last_of("/\\");
        tab.title = toWide(slash == std::string::npos
                               ? app.currentFile
                               : app.currentFile.substr(slash + 1));
        updateWindowTitle(app);
        app.copiedNotificationKey = "toast.saved";
    }
    app.showCopiedNotification = true;
    app.copiedNotificationStart = std::chrono::steady_clock::now();
    startNotificationTimer(app);
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

void handleEditorKeyDown(App& app, HWND hwnd, WPARAM wParam) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // The insert menu closes on Esc; any other key falls through to the
    // editor after dismissing it
    if (app.editCtxOpen) {
        closeEditCtxMenu(app);
        if (wParam == VK_ESCAPE) return;
    }

    // Search still works in edit mode via Ctrl+F
    if (ctrl && wParam == 'F') {
        if (!app.showSearch) {
            app.showSearch = true;
            app.searchActive = true;
            app.searchAnimation = 0;
            app.searchQuery.clear();
            app.editorSearchMatches.clear();
            app.editorSearchCurrentIndex = 0;
            app.searchCurrentIndex = 0;
            app.searchJustOpened = true;
            updateBlinkTimer(app);
        }
        app.searchReplaceMode = false;
        app.replaceFieldActive = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Ctrl+H: search with the replace row (#121)
    if (ctrl && wParam == 'H') {
        if (!app.showSearch) {
            app.showSearch = true;
            app.searchActive = true;
            app.searchAnimation = 0;
            app.searchQuery.clear();
            app.editorSearchMatches.clear();
            app.editorSearchCurrentIndex = 0;
            app.searchCurrentIndex = 0;
            app.searchJustOpened = true;
            updateBlinkTimer(app);
        }
        app.searchReplaceMode = true;
        app.replaceFieldActive = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Ctrl+O on an untitled, still-empty note opens the file picker
    // (the empty state's keycap hint; works with the preview hidden too)
    if (ctrl && wParam == 'O' && quickNoteEmptyStateActive(app)) {
        quickNoteOpenFile(app, hwnd);
        return;
    }

    // If search is open, route to search handling
    if (app.showSearch && app.searchActive) {
        // Ctrl+S still saves while the search bar is open
        if (ctrl && wParam == 'S') {
            saveEditorFile(app, hwnd);
            return;
        }
        // Ctrl+H from an open search reveals the replace row (#121)
        if (ctrl && wParam == 'H') {
            app.searchReplaceMode = true;
            app.replaceFieldActive = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        // Ctrl+Enter replaces every match at once
        if (ctrl && wParam == VK_RETURN && app.searchReplaceMode) {
            editorReplaceAll(app, hwnd);
            return;
        }
        switch (wParam) {
            case VK_ESCAPE:
                app.showSearch = false;
                app.searchActive = false;
                app.searchQuery.clear();
                app.searchReplaceMode = false;
                app.replaceFieldActive = false;
                app.editorSearchMatches.clear();
                app.editorSearchCurrentIndex = 0;
                app.searchAnimation = 0;
                updateBlinkTimer(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case VK_TAB:
                // Replace mode: Tab hops between the two fields
                if (app.searchReplaceMode) {
                    app.replaceFieldActive = !app.replaceFieldActive;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return;
            case VK_RETURN: {
                if (app.searchReplaceMode && app.replaceFieldActive) {
                    // Enter in the replace row: replace current, move on
                    editorReplaceCurrent(app, hwnd);
                    return;
                }
                if (!app.editorSearchMatches.empty()) {
                    app.editorSearchCurrentIndex = (app.editorSearchCurrentIndex + 1) % (int)app.editorSearchMatches.size();
                    app.searchCurrentIndex = app.editorSearchCurrentIndex;
                    scrollEditorToMatch(app);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            case VK_BACK: {
                if (app.searchReplaceMode && app.replaceFieldActive) {
                    if (!app.replaceText.empty()) app.replaceText.pop_back();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return;
                }
                if (!app.searchQuery.empty()) {
                    app.searchQuery.pop_back();
                    performEditorSearch(app);
                    app.searchCurrentIndex = app.editorSearchCurrentIndex;
                    if (!app.editorSearchMatches.empty()) scrollEditorToMatch(app);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
        return; // Let WM_CHAR handle text input for search
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

    if (wParam == VK_ESCAPE) {
        // Dirty buffer: straight to the dialog — it is the guard, and a
        // second Esc stage on top of it is what made the old flow a maze.
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

        // Show brief hint
        app.editorNotificationMsg = tr(app, "toast.exit_confirm");
        app.showEditModeNotification = true;
        app.editModeNotificationAlpha = 1.0f;
        app.editModeNotificationStart = now;
        startNotificationTimer(app);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    app.escPressedOnce = false;

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
                app.editorSelStart = 0;
                app.editorSelEnd = app.editorText.size();
                app.editorCursorPos = app.editorText.size();
                app.editorHasSelection = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case 'C':
                if (app.editorHasSelection) {
                    editorCopyToClipboard(hwnd, editorGetSelectedText(app));
                }
                return;
            case 'X':
                if (app.editorHasSelection) {
                    editorCopyToClipboard(hwnd, editorGetSelectedText(app));
                    editorDeleteSelection(app);
                    scheduleReparse(app);
                    editorEnsureCursorVisible(app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return;
            case 'V': {
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
                if (!paste.empty()) {
                    if (app.editorHasSelection) editorDeleteSelection(app);
                    size_t before = app.editorCursorPos;
                    app.editorText.insert(app.editorCursorPos, paste);
                    app.editorCursorPos += paste.size();
                    pushUndo(app, App::EditAction::Insert, before, paste, before, app.editorCursorPos);
                    rebuildLineStarts(app);
                    scheduleReparse(app);
                    editorEnsureCursorVisible(app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return;
            }
            case 'P':
                // Print preview of the document as currently edited, saved
                // or not (openPrintPreview re-parses; Ctrl+E is the pane)
                openPrintPreview(app, hwnd);
                return;
            case 'E': {
                // Ctrl+E flips WYSIWYG <-> raw (design t8); the split
                // preview toggle lives on Ctrl+Shift+E in raw mode
                if (!shift) {
                    editorSetWysiwyg(app, hwnd, !app.editorWysiwyg);
                    return;
                }
                if (app.editorWysiwyg) return;  // no preview to toggle
                app.editorShowPreview = !app.editorShowPreview;
                app.clearEditorLineLayoutCache();
                if (app.editorShowPreview) {
                    // Re-parse so the preview catches up with edits made
                    // while it was hidden
                    editorReparse(app);
                }
                app.editorRowMetricsWidth = -1.0f;  // pane width changed
                app.editorNotificationMsg = app.editorShowPreview
                    ? tr(app, "toast.preview_shown")
                    : tr(app, "toast.preview_hidden");
                app.showEditModeNotification = true;
                app.editModeNotificationAlpha = 1.0f;
                app.editModeNotificationStart = std::chrono::steady_clock::now();
                startNotificationTimer(app);
                app.layoutDirty = app.editorShowPreview;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            case 'B':
                if (app.editorAssists || app.editorWysiwyg) {
                    editorToggleInlineMark(app, hwnd, L"**");
                }
                return;
            case 'I':
                if (app.editorAssists || app.editorWysiwyg) {
                    editorToggleInlineMark(app, hwnd, L"*");
                }
                return;
            case 'W': {
                app.editorWordWrap = !app.editorWordWrap;
                app.clearEditorLineLayoutCache();
                app.editorDesiredCol = -1;
                app.editorDesiredX = -1.0f;
                rebuildEditorRowMetrics(app);
                editorEnsureCursorVisible(app);
                app.editorNotificationMsg = editorWrapOn(app)
                    ? tr(app, "toast.wrap_on")
                    : tr(app, "toast.wrap_off");
                app.showEditModeNotification = true;
                app.editModeNotificationAlpha = 1.0f;
                app.editModeNotificationStart = std::chrono::steady_clock::now();
                startNotificationTimer(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            case VK_HOME:
                editorStartOrExtendSelection(app, shift);
                app.editorCursorPos = 0;
                app.editorDesiredCol = -1;
                if (shift) editorUpdateSelEnd(app);
                else app.editorHasSelection = false;
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case VK_END:
                editorStartOrExtendSelection(app, shift);
                app.editorCursorPos = app.editorText.size();
                app.editorDesiredCol = -1;
                if (shift) editorUpdateSelEnd(app);
                else app.editorHasSelection = false;
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case VK_LEFT:
                editorStartOrExtendSelection(app, shift);
                app.editorCursorPos = editorWordLeft(app, app.editorCursorPos);
                app.editorDesiredCol = -1;
                if (shift) editorUpdateSelEnd(app);
                else app.editorHasSelection = false;
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case VK_RIGHT:
                editorStartOrExtendSelection(app, shift);
                app.editorCursorPos = editorWordRight(app, app.editorCursorPos);
                app.editorDesiredCol = -1;
                if (shift) editorUpdateSelEnd(app);
                else app.editorHasSelection = false;
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
        }
        return; // Don't process other ctrl+key combos as text
    }

    // Non-Ctrl keys
    switch (wParam) {
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
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
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
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;

        case VK_UP:
        case VK_DOWN: {
            bool down = (wParam == VK_DOWN);
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
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_HOME: {
            editorStartOrExtendSelection(app, shift);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            app.editorCursorPos = app.editorLineStarts[line];
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_END: {
            editorStartOrExtendSelection(app, shift);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            app.editorCursorPos = getLineEnd(app, line);
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_PRIOR: { // Page Up
            editorStartOrExtendSelection(app, shift);
            float scale = app.contentScale * app.zoomFactor;
            float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f * scale;
            int pageLines = std::max(1, (int)(app.height / lineHeight) - 2);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            size_t col = getColFromPos(app, app.editorCursorPos);
            size_t targetLine = (line > (size_t)pageLines) ? line - pageLines : 0;
            size_t targetLineLen = getLineLength(app, targetLine);
            app.editorCursorPos = app.editorLineStarts[targetLine] + std::min(col, targetLineLen);
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_NEXT: { // Page Down
            editorStartOrExtendSelection(app, shift);
            float scale = app.contentScale * app.zoomFactor;
            float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f * scale;
            int pageLines = std::max(1, (int)(app.height / lineHeight) - 2);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            size_t col = getColFromPos(app, app.editorCursorPos);
            size_t targetLine = std::min(line + pageLines, app.editorLineStarts.size() - 1);
            size_t targetLineLen = getLineLength(app, targetLine);
            app.editorCursorPos = app.editorLineStarts[targetLine] + std::min(col, targetLineLen);
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_DELETE:
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
            scheduleReparse(app);
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
    }
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

// Toggle a prefix ("- ", "> ", "- [ ] ") at the caret line's start
static void editorToggleLinePrefix(App& app, HWND hwnd,
                                   const std::wstring& prefix) {
    size_t line = getLineFromPos(app, app.editorCursorPos);
    size_t ls = app.editorLineStarts[line];
    bool has = app.editorText.size() >= ls + prefix.size() &&
               app.editorText.compare(ls, prefix.size(), prefix) == 0;
    size_t before = app.editorCursorPos;
    if (has) {
        std::wstring removed = app.editorText.substr(ls, prefix.size());
        app.editorText.erase(ls, prefix.size());
        app.editorCursorPos =
            before >= ls + prefix.size() ? before - prefix.size() : ls;
        pushUndo(app, App::EditAction::Delete, ls, removed, before,
                 app.editorCursorPos);
    } else {
        app.editorText.insert(ls, prefix);
        app.editorCursorPos = before + prefix.size();
        pushUndo(app, App::EditAction::Insert, ls, prefix, before,
                 app.editorCursorPos);
    }
    rebuildLineStarts(app);
    app.editorDesiredCol = -1;
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Insert a block snippet at the caret (own line), placing the caret
// caretOffset characters into the snippet
static void editorInsertSnippet(App& app, HWND hwnd,
                                const std::wstring& snippet,
                                size_t caretOffset) {
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

void editorSetWysiwyg(App& app, HWND hwnd, bool wysiwyg) {
    if (app.editorWysiwyg == wysiwyg) return;
    app.editorWysiwyg = wysiwyg;
    app.clearEditorLineLayoutCache();
    app.editorRowMetricsWidth = -1.0f;
    app.editorDesiredCol = -1;
    app.editorScrollX = 0.0f;
    rebuildEditorRowMetrics(app);
    // The pill choice is the remembered preference (next ':' opens here)
    persistEditorMode(app);
    if (!wysiwyg && app.editorShowPreview) editorReparse(app);
    app.layoutDirty = true;
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Insert-menu helpers (design t9) ------------------------------------

void editorMoveCaretToPoint(App& app, int x, int y) {
    app.editorCursorPos = editorPosFromClick(app, x, y);
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

void editorInsertDiagramTemplate(App& app, HWND hwnd, int kind) {
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
    };
    if (kind < 0 || kind > 6) return;
    editorInsertSnippet(app, hwnd, kTemplates[kind], 11);
}

void editorInsertSnippetPublic(App& app, HWND hwnd,
                               const std::wstring& snippet,
                               size_t caretOffset) {
    editorInsertSnippet(app, hwnd, snippet, caretOffset);
}

void editorClipboardCut(App& app, HWND hwnd) {
    if (!app.editorHasSelection) return;
    editorCopyToClipboard(hwnd, editorGetSelectedText(app));
    editorDeleteSelection(app);
    rebuildLineStarts(app);
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void editorClipboardCopy(App& app, HWND hwnd) {
    if (app.editorHasSelection) {
        editorCopyToClipboard(hwnd, editorGetSelectedText(app));
    }
}

void editorClipboardPaste(App& app, HWND hwnd) {
    std::wstring paste = editorGetClipboard(hwnd);
    if (paste.empty()) return;
    if (app.editorHasSelection) editorDeleteSelection(app);
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
    switch (id) {
        case 1: editorToggleInlineMark(app, hwnd, L"**"); break;
        case 2: editorToggleInlineMark(app, hwnd, L"*"); break;
        case 3: editorToggleInlineMark(app, hwnd, L"~~"); break;
        case 4: editorToggleInlineMark(app, hwnd, L"`"); break;
        case 5: editorInsertLink(app, hwnd); break;
        case 10: editorToggleLinePrefix(app, hwnd, L"- "); break;
        case 11: editorToggleLinePrefix(app, hwnd, L"- [ ] "); break;
        case 12: editorToggleLinePrefix(app, hwnd, L"> "); break;
        case 20:
            editorInsertSnippet(
                app, hwnd,
                L"| Column | Column | Column |\n"
                L"|---|---|---|\n"
                L"|  |  |  |\n",
                2);
            break;
        case 21:
            editorInsertSnippet(app, hwnd,
                                L"```mermaid\n"
                                L"flowchart LR\n"
                                L"    A[Start] --> B[Next]\n"
                                L"```\n",
                                11);
            break;
        case 22:
            editorInsertSnippet(app, hwnd, L"![](image.png)\n", 4);
            break;
        case 30: editorSetWysiwyg(app, hwnd, true); break;
        case 31: editorSetWysiwyg(app, hwnd, false); break;
    }
}

void handleEditorCharInput(App& app, HWND hwnd, WPARAM wParam) {
    // Swallow characters while confirm-exit prompt is active
    if (app.confirmExitPending) return;

    resetCursorBlink(app);

    // If search is active, route characters there
    if (app.showSearch && app.searchActive) {
        if (app.searchJustOpened) {
            app.searchJustOpened = false;
            return;
        }
        wchar_t ch = (wchar_t)wParam;
        bool toReplace = app.searchReplaceMode && app.replaceFieldActive;
        // Ctrl+V arrives here as the SYN control character (#121)
        if (ch == 0x16) {
            std::wstring pasted = clipboardLine(hwnd);
            if (!pasted.empty()) {
                if (toReplace) {
                    app.replaceText += pasted;
                } else {
                    app.searchQuery += pasted;
                    performEditorSearch(app);
                    if (!app.editorSearchMatches.empty()) {
                        app.editorSearchCurrentIndex = 0;
                        app.searchCurrentIndex = 0;
                        scrollEditorToMatch(app);
                    }
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return;
        }
        if (ch >= 32 && ch != 127) {
            if (toReplace) {
                app.replaceText += ch;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            app.searchQuery += ch;
            performEditorSearch(app);
            if (!app.editorSearchMatches.empty()) {
                app.editorSearchCurrentIndex = 0;
                app.searchCurrentIndex = 0;
                scrollEditorToMatch(app);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

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
    // removes its marker and ends it. The raw editor's assists are a
    // setting; the WYSIWYG canvas always assists.
    if (ch == L'\n' && !app.editorHasSelection &&
        (app.editorAssists || app.editorWysiwyg)) {
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
        bool assists = app.editorAssists || app.editorWysiwyg;
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
    if (!app.editMode || app.editorLineStarts.empty()) return;
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return;

    size_t line = getLineFromPos(app, app.editorCursorPos);
    size_t lineStart = app.editorLineStarts[line];
    size_t lineLen = getLineLength(app, line);
    size_t col = std::min(app.editorCursorPos - lineStart, lineLen);

    IDWriteTextLayout* layout = createEditorLineLayout(app, lineStart, lineLen);
    float xOff = 0, yOff = 0;
    editorCaretXY(layout, col, xOff, yOff);
    if (layout) layout->Release();

    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float padding = dpi(app, 8.0f);
    float gutterWidth = dpi(app, 48.0f);

    float lineTop;
    if (editorWrapOn(app)) {
        ensureEditorRowMetrics(app);
        size_t rowStart = (line < app.editorRowStarts.size()) ? app.editorRowStarts[line] : line;
        lineTop = padding + rowStart * lineHeight;
    } else {
        lineTop = padding + line * lineHeight;
    }

    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = (LONG)(editorTextX(app) + xOff -
                               (editorWrapOn(app) ? 0.0f : app.editorScrollX));
    cf.ptCurrentPos.y = (LONG)(chromeTopHeight(app) + lineTop + yOff -
                               app.editorScrollY + lineHeight);
    ImmSetCompositionWindow(himc, &cf);
    ImmReleaseContext(hwnd, himc);
}

// --- Mouse handling ---

static size_t editorPosFromClick(App& app, int x, int y) {
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
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS m{};
        layout->HitTestPoint(adjustedX, localY, &trailing, &inside, &m);
        layout->Release();
        // trailing hit means the click was past the glyph's midpoint: the
        // caret goes after the full character (m.length covers surrogate pairs)
        col = (size_t)m.textPosition + (trailing ? (size_t)m.length : 0);
    } else {
        float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth : app.editorTextFormat->GetFontSize() * 0.6f;
        col = (size_t)std::max(0, (int)(adjustedX / charWidth + 0.5f));
    }
    col = std::min(col, lineLen);

    return lineStart + col;
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
    if (app.editorContentHeight > app.height &&
        (float)x >= editorWidth - dpi(app, 14.0f) &&
        (float)x <= editorWidth - dpi(app, 2.0f)) {
        float maxScroll = app.editorContentHeight - app.height;
        float sbHeight = (float)app.height / app.editorContentHeight * app.height;
        sbHeight = std::max(sbHeight, dpi(app, 30.0f));
        float track = std::max(1.0f, app.height - sbHeight);
        float sbY = app.editorScrollY / maxScroll * track;
        if ((float)y < sbY || (float)y > sbY + sbHeight) {
            // Track click: center the thumb on the click point
            float t = ((float)y - sbHeight * 0.5f) / track;
            app.editorScrollY =
                std::max(0.0f, std::min(maxScroll, t * maxScroll));
        }
        app.editorScrollbarDragging = true;
        app.editorScrollbarDragStartY = (float)y;
        app.editorScrollbarDragStartScroll = app.editorScrollY;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Check for separator (only exists while the preview is visible)
    if (app.editorShowPreview) {
        float sepX = app.width * app.editorSplitRatio;
        if (std::abs((float)x - sepX) < dpi(app, 6.0f)) {
            app.draggingSeparator = true;
            app.separatorDragStartX = (float)x;
            app.separatorDragStartRatio = app.editorSplitRatio;
            SetCapture(hwnd);
            return;
        }
    }

    // Only handle clicks in the editor pane (left side)
    if (x > editorWidth) return;

    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    size_t clickPos = editorPosFromClick(app, x, y);

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
    editRailMouseUp(app, y);
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

    // Insert-menu hover: rows highlight, parents open their submenu
    if (editCtxMouseMove(app, x, y)) {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return;
    }

    // Rail hover / document-map drag (design t8)
    if (editRailMouseMove(app, hwnd, x, y)) {
        if (!app.editRailMapDragging) {
            SetCursor(LoadCursor(nullptr,
                                 app.editRailHover ? IDC_HAND : IDC_ARROW));
        }
        return;
    }

    if (app.editorScrollbarDragging) {
        float maxScroll = std::max(0.0f, app.editorContentHeight - app.height);
        float sbHeight = (float)app.height /
                         std::max(1.0f, app.editorContentHeight) * app.height;
        sbHeight = std::max(sbHeight, dpi(app, 30.0f));
        float track = std::max(1.0f, app.height - sbHeight);
        float delta =
            ((float)y - app.editorScrollbarDragStartY) / track * maxScroll;
        app.editorScrollY = std::max(
            0.0f,
            std::min(maxScroll, app.editorScrollbarDragStartScroll + delta));
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

    // Cursor shape
    float sepX = app.width * app.editorSplitRatio;
    static HCURSOR cursorSizeWE = LoadCursor(nullptr, IDC_SIZEWE);
    static HCURSOR cursorIBeam = LoadCursor(nullptr, IDC_IBEAM);
    static HCURSOR cursorArrow = LoadCursor(nullptr, IDC_ARROW);

    if (app.editorShowPreview && std::abs((float)x - sepX) < dpi(app, 6.0f)) {
        SetCursor(cursorSizeWE);
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

// Soft-wrap rendering: each logical line spans editorRowStarts-many visual
// rows; highlights and the caret come from DirectWrite hit testing on the
// wrapped per-line layouts
static void renderEditorWrapped(App& app, float editorWidth) {
    ensureEditorRowMetrics(app);
    if (app.editorRowStarts.size() != app.editorLineStarts.size() + 1) return;

    float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    float padding = dpi(app, 8.0f);
    float textX = editorTextX(app);

    app.brush->SetColor(app.theme.background);
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

        // Line numbers moved to the rail's document map (design t8); the
        // gutter column is where the rail now lives

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
        if (app.cursorBlinkOn && i == curLine) {
            float cx = 0, cy = 0;
            editorCaretXY(lineLayout, app.editorCursorPos - lineStart, cx, cy);
            app.brush->SetColor(app.theme.text);
            app.renderTarget->FillRectangle(
                D2D1::RectF(textX + cx, lineY + cy,
                            textX + cx + dpi(app, 2.0f), lineY + cy + lineHeight),
                app.brush);
        }

    }

    app.editorContentHeight = padding * 2 + app.editorTotalRows * lineHeight;

    // Editor scrollbar (same as unwrapped)
    if (app.editorContentHeight > app.height) {
        float maxScroll = app.editorContentHeight - app.height;
        float sbHeight = (float)app.height / app.editorContentHeight * app.height;
        sbHeight = std::max(sbHeight, dpi(app, 30.0f));
        float sbY = (maxScroll > 0) ? (app.editorScrollY / maxScroll * (app.height - sbHeight)) : 0;

        float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;
        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, 0.3f));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(editorWidth - dpi(app, 10.0f), sbY,
                                           editorWidth - dpi(app, 4.0f), sbY + sbHeight), 3, 3),
            app.brush);
    }

    app.renderTarget->PopAxisAlignedClip();
}

void renderEditor(App& app, float editorWidth) {
    if (!app.editorTextFormat || app.editorLineStarts.empty()) return;

    if (editorWrapOn(app)) {
        renderEditorWrapped(app, editorWidth);
        return;
    }

    float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    float padding = dpi(app, 8.0f);
    float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth : app.editorTextFormat->GetFontSize() * 0.6f;

    // Editor background
    app.brush->SetColor(app.theme.background);
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

    // Text origin shifts left as the pane scrolls horizontally; the old
    // gutter column is repainted after the text so lines slide underneath
    // the rail (#77)
    float gutterWidth = dpi(app, 48.0f);
    float textBase = editorTextX(app) - app.editorScrollX;

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
    if (app.cursorBlinkOn) {
        size_t curLine = getLineFromPos(app, app.editorCursorPos);
        size_t curCol = getColFromPos(app, app.editorCursorPos);
        size_t curLineStart = app.editorLineStarts[curLine];
        size_t curLineLen = getLineLength(app, curLine);
        IDWriteTextLayout* curLayout = cachedEditorLineLayout(app, curLineStart, curLineLen);
        float curX = textBase + editorColToX(app, curLayout, std::min(curCol, curLineLen));
        float curY = chromeTopHeight(app) + padding + curLine * lineHeight - app.editorScrollY;

        app.brush->SetColor(app.theme.text);
        app.renderTarget->FillRectangle(
            D2D1::RectF(curX, curY, curX + dpi(app, 2.0f), curY + lineHeight), app.brush);
    }

    // The old gutter column last, blanked: horizontally scrolled text
    // slides under it, and the rail (with the document map as the only
    // line numbering) draws on top of it (design t8)
    app.brush->SetColor(app.theme.background);
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, gutterWidth, (float)app.height), app.brush);

    // Update content height for scrolling
    app.editorContentHeight = padding * 2 + app.editorLineStarts.size() * lineHeight;

    // Editor scrollbar
    if (app.editorContentHeight > app.height) {
        float maxScroll = app.editorContentHeight - app.height;
        float sbHeight = (float)app.height / app.editorContentHeight * app.height;
        sbHeight = std::max(sbHeight, dpi(app, 30.0f));
        float sbY = (maxScroll > 0) ? (app.editorScrollY / maxScroll * (app.height - sbHeight)) : 0;

        float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;
        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, 0.3f));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(editorWidth - dpi(app, 10.0f), sbY,
                                           editorWidth - dpi(app, 4.0f), sbY + sbHeight), 3, 3),
            app.brush);
    }

    app.renderTarget->PopAxisAlignedClip();
}

void renderSeparator(App& app) {
    float sepX = app.width * app.editorSplitRatio;
    float sepWidth = dpi(app, 6.0f);

    // Separator background
    D2D1_COLOR_F sepColor = app.theme.isDark ? hexColor(0x3A3A40) : hexColor(0xD0D0D0);
    app.brush->SetColor(sepColor);
    app.renderTarget->FillRectangle(
        D2D1::RectF(sepX - sepWidth / 2, 0, sepX + sepWidth / 2, (float)app.height), app.brush);

    // Grip dots (3 dots in center)
    float dotRadius = dpi(app, 2.0f);
    float dotSpacing = dpi(app, 10.0f);
    float centerY = app.height / 2.0f;
    D2D1_COLOR_F dotColor = app.theme.isDark ? hexColor(0x808080) : hexColor(0x808080);
    app.brush->SetColor(dotColor);

    for (int i = -1; i <= 1; i++) {
        app.renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(sepX, centerY + i * dotSpacing), dotRadius, dotRadius),
            app.brush);
    }
}

void renderEditModeNotification(App& app) {
    if (!app.showEditModeNotification) return;

    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - app.editModeNotificationStart).count();

    float alpha = 1.0f;
    if (app.confirmExitPending) {
        // The exit-confirm prompt stays fully visible until answered —
        // fading it out while confirmExitPending is still armed leaves the
        // app in an invisible modal state
    } else {
        if (elapsed > 3.0f) {
            app.showEditModeNotification = false;
            return;
        }
        if (elapsed > 1.5f) {
            alpha = 1.0f - (elapsed - 1.5f) / 1.5f;
        }
    }

    const wchar_t* msg = app.editorNotificationMsg.c_str();
    size_t msgLen = app.editorNotificationMsg.size();

    // Size the pill to the message so long prompts aren't clipped
    IDWriteTextFormat* measureFmt = app.searchTextFormat ? app.searchTextFormat : app.textFormat;
    float pillWidth = dpi(app, 120.0f);
    if (measureFmt) {
        float textWidth = measureText(app, app.editorNotificationMsg, measureFmt);
        pillWidth = std::min(textWidth + dpi(app, 40.0f), (float)app.width - dpi(app, 20.0f));
    }
    float pillHeight = dpi(app, 30.0f);
    float pillX = (float)(app.width - pillWidth) / 2.0f;
    float pillY = (float)app.height - dpi(app, 60.0f);

    // Use the active theme for transient editor feedback as well as the
    // translated message, so a light theme does not carry a fixed green UI.
    D2D1_COLOR_F pillColor = app.theme.accent;
    pillColor.a = 0.92f * alpha;
    app.brush->SetColor(pillColor);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + pillWidth, pillY + pillHeight), dpi(app, 15.0f), dpi(app, 15.0f)),
        app.brush);

    // Text
    D2D1_COLOR_F textColor = app.theme.background;
    textColor.a = alpha;
    app.brush->SetColor(textColor);
    IDWriteTextFormat* fmt = app.searchTextFormat ? app.searchTextFormat : app.textFormat;
    if (fmt) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.renderTarget->DrawText(msg, (UINT32)msgLen, fmt,
            D2D1::RectF(pillX, pillY, pillX + pillWidth, pillY + pillHeight), app.brush);
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
}
