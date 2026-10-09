// Search results panel (#246). Ctrl+Shift+F opens the search bar with
// every match of its query listed in the Contents slot, so a long document
// can be read through its hits instead of stepping one match at a time.
// The document's matches line up under the headings they fall in, each a
// snippet with the match marked. Other open tabs and the files of the
// document's folder follow, each with its first matches and their lines;
// one press opens such a file in its own tab, at that match. Suggested by
// Alex (@Lex987).

#include "search_panel.h"

#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "overlays.h"
#include "search.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr size_t kNoTextRect = std::numeric_limits<size_t>::max();

HCURSOR cursorArrow() {
    static HCURSOR cursor = LoadCursor(nullptr, IDC_ARROW);
    return cursor;
}
HCURSOR cursorHand() {
    static HCURSOR cursor = LoadCursor(nullptr, IDC_HAND);
    return cursor;
}

bool inside(const D2D1_RECT_F& r, float x, float y) {
    return r.right > r.left && x >= r.left && x <= r.right && y >= r.top &&
           y <= r.bottom;
}

float rowHeight(const App& app, SearchPanelRowKind kind) {
    switch (kind) {
        case SPR_DOCUMENT:
        case SPR_FILE: return dpi(app, 30.0f);
        case SPR_CAPTION: return dpi(app, 28.0f);
        case SPR_MORE: return dpi(app, 22.0f);
        case SPR_NOTE: return dpi(app, 26.0f);
        default: return dpi(app, 24.0f);
    }
}

bool rowActs(SearchPanelRowKind kind) {
    return kind != SPR_CAPTION && kind != SPR_NOTE;
}

bool matchPlaced(const App::SearchMatch& m) {
    return m.textRectIndex != kNoTextRect &&
           m.highlightRect.bottom > m.highlightRect.top;
}

// The panel's frame: header with the title and the close cross, two scope
// rows (other open tabs, this folder), the list, the footer
D2D1_RECT_F cardRect(const App& app) {
    float width = tocPanelWidth(app);
    float x = tocPanelX(app, width);
    return D2D1::RectF(x, chromeTopHeight(app), x + width, (float)app.height);
}
D2D1_RECT_F closeRect(const App& app) {
    D2D1_RECT_F card = cardRect(app);
    float pad = dpi(app, 12.0f);
    return D2D1::RectF(card.right - pad - dpi(app, 14.0f), card.top,
                       card.right, card.top + dpi(app, 29.0f));
}
D2D1_RECT_F scopeRect(const App& app, int scope) {
    D2D1_RECT_F card = cardRect(app);
    float pad = dpi(app, 12.0f);
    float top = card.top + dpi(app, 40.0f) + dpi(app, 24.0f) * (float)scope;
    return D2D1::RectF(card.left + pad, top, card.right - pad,
                       top + dpi(app, 24.0f));
}

float rowsHeight(const std::vector<SearchPanelRow>& rows) {
    return rows.empty() ? 0.0f : rows.back().top + rows.back().height;
}

float maxScroll(const App& app, const std::vector<SearchPanelRow>& rows) {
    D2D1_RECT_F list = searchPanelListRect(app);
    return std::max(0.0f, rowsHeight(rows) - (list.bottom - list.top));
}

int rowAt(const App& app, const std::vector<SearchPanelRow>& rows, float x,
          float y) {
    D2D1_RECT_F list = searchPanelListRect(app);
    if (!inside(list, x, y)) return -1;
    float ly = y - list.top + app.searchPanelScroll;
    auto it = std::upper_bound(rows.begin(), rows.end(), ly,
        [](float value, const SearchPanelRow& row) { return value < row.top; });
    if (it == rows.begin()) return -1;
    int index = (int)(it - rows.begin()) - 1;
    const SearchPanelRow& row = rows[index];
    return ly < row.top + row.height ? index : -1;
}

// The document's first match at or after a row, for headings and the
// document row itself
int firstMatchFrom(const std::vector<SearchPanelRow>& rows, int index) {
    for (size_t i = (size_t)index; i < rows.size(); i++) {
        if (rows[i].kind == SPR_MATCH) return rows[i].index;
        if (rows[i].kind != SPR_SECTION && rows[i].kind != SPR_DOCUMENT) break;
    }
    return -1;
}

std::wstring documentTitle(const App& app) {
    if (app.activeTab >= 0 && app.activeTab < (int)app.tabs.size() &&
        !app.tabs[app.activeTab].title.empty()) {
        return app.tabs[app.activeTab].title;
    }
    if (app.currentFile.empty()) return tr(app, "title.untitled");
    std::wstring wide = toWide(app.currentFile);
    size_t sep = wide.find_last_of(L"\\/");
    return sep == std::wstring::npos ? wide : wide.substr(sep + 1);
}

// A line of text around a match: some lead-in, cut with an ellipsis, and
// the rest of the line after it. Tabs between table cells become spaces.
std::wstring snippetAround(const std::wstring& text, size_t pos, size_t length,
                           size_t& start) {
    pos = std::min(pos, text.size());
    size_t lineStart = pos == 0 ? std::wstring::npos : text.rfind(L'\n', pos - 1);
    lineStart = lineStart == std::wstring::npos ? 0 : lineStart + 1;
    size_t lineEnd = text.find(L'\n', pos);
    if (lineEnd == std::wstring::npos) lineEnd = text.size();
    size_t from = pos > lineStart + 28 ? pos - 24 : lineStart;
    if (from > lineStart && from < text.size() && IS_LOW_SURROGATE(text[from])) from++;
    size_t to = std::min(lineEnd, pos + length + 120);
    std::wstring out;
    if (from > lineStart) out += wchar_t(0x2026);
    size_t lead = out.size();
    for (size_t i = from; i < to; i++) {
        wchar_t ch = text[i];
        out += (ch < 0x20) ? L' ' : ch;
    }
    // Leading blanks would only push the match right
    size_t blanks = 0;
    while (lead + blanks < out.size() && out[lead + blanks] == L' ' &&
           from + blanks < pos) {
        blanks++;
    }
    out.erase(lead, blanks);
    start = lead + (pos - from) - blanks;
    return out;
}

IDWriteTextLayout* rowLayout(App& app, const std::wstring& text,
                             IDWriteTextFormat* format, float width,
                             float height) {
    IDWriteTextLayout* layout = nullptr;
    if (text.empty() || !format || width <= 0.0f ||
        FAILED(app.dwriteFactory->CreateTextLayout(
            text.c_str(), (UINT32)text.size(), format, width, height, &layout))) {
        return nullptr;
    }
    useUiFontFallback(app, layout);
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    IDWriteInlineObject* ellipsis = nullptr;
    if (SUCCEEDED(app.dwriteFactory->CreateEllipsisTrimmingSign(layout, &ellipsis)) &&
        ellipsis) {
        layout->SetTrimming(&trimming, ellipsis);
        ellipsis->Release();
    }
    return layout;
}

// A snippet with its match in semibold over an accent wash
void drawSnippet(App& app, const std::wstring& text, size_t start, size_t length,
                 const D2D1_RECT_F& box, float alpha) {
    IDWriteTextLayout* layout =
        rowLayout(app, text, app.tocFormat, box.right - box.left, box.bottom - box.top);
    if (!layout) return;
    start = std::min(start, text.size());
    length = std::min(length, text.size() - start);
    DWRITE_TEXT_RANGE range{(UINT32)start, (UINT32)length};
    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    float ty = box.top + std::max(0.0f, (box.bottom - box.top - tm.height) * 0.5f);
    if (length > 0) {
        DWRITE_HIT_TEST_METRICS hits[8];
        UINT32 count = 0;
        if (SUCCEEDED(layout->HitTestTextRange(range.startPosition, range.length,
                                               0.0f, 0.0f, hits, 8, &count))) {
            D2D1_COLOR_F wash = app.theme.accent;
            wash.a = 0.26f * alpha;
            app.brush->SetColor(wash);
            for (UINT32 i = 0; i < count; i++) {
                float left = box.left + hits[i].left;
                float right = std::min(left + hits[i].width, box.right);
                if (right <= left) continue;
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(left - 1.0f, ty + hits[i].top,
                                                  right + 1.0f,
                                                  ty + hits[i].top + hits[i].height),
                                      dpi(app, 2.0f), dpi(app, 2.0f)),
                    app.brush);
            }
        }
    }
    D2D1_COLOR_F ink = app.theme.text;
    ink.a = 0.86f * alpha;
    app.brush->SetColor(ink);
    app.renderTarget->DrawTextLayout(D2D1::Point2F(box.left, ty), layout, app.brush,
                                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    layout->Release();
}

void drawLabel(App& app, const std::wstring& text, IDWriteTextFormat* format,
               const D2D1_RECT_F& box, D2D1_COLOR_F color,
               DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
    IDWriteTextLayout* layout =
        rowLayout(app, text, format, box.right - box.left, box.bottom - box.top);
    if (!layout) return;
    if (weight != DWRITE_FONT_WEIGHT_NORMAL) {
        layout->SetFontWeight(weight, {0, (UINT32)text.size()});
    }
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    float ty = box.top + std::max(0.0f, (box.bottom - box.top - tm.height) * 0.5f);
    app.brush->SetColor(color);
    app.renderTarget->DrawTextLayout(D2D1::Point2F(box.left, ty), layout, app.brush,
                                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    layout->Release();
}

// A count at the right end of a row; returns where the row's text must end
float drawCount(App& app, int count, float right, float cy, float alpha) {
    if (!app.signalSmallFormat) return right;
    wchar_t label[16];
    swprintf_s(label, L"%d", count);
    float w = measureText(app, label, app.signalSmallFormat) + dpi(app, 11.0f);
    D2D1_RECT_F badge = D2D1::RectF(right - w, cy - dpi(app, 7.5f), right,
                                    cy + dpi(app, 7.5f));
    D2D1_COLOR_F fill = app.theme.text;
    fill.a = 0.07f * alpha;
    app.brush->SetColor(fill);
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(badge, dpi(app, 7.0f), dpi(app, 7.0f)), app.brush);
    D2D1_COLOR_F ink = app.theme.text;
    ink.a = 0.6f * alpha;
    app.brush->SetColor(ink);
    app.renderTarget->DrawText(label, (UINT32)wcslen(label), app.signalSmallFormat,
        D2D1::RectF(badge.left + dpi(app, 5.5f), badge.top + dpi(app, 1.5f),
                    badge.right, badge.bottom),
        app.brush);
    return badge.left - dpi(app, 8.0f);
}

void drawCheckbox(App& app, float x, float cy, bool on, float alpha) {
    float size = dpi(app, 13.0f);
    D2D1_RECT_F box = D2D1::RectF(x, cy - size * 0.5f, x + size, cy + size * 0.5f);
    D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(box, dpi(app, 3.0f), dpi(app, 3.0f));
    if (on) {
        D2D1_COLOR_F fill = app.theme.accent;
        fill.a = alpha;
        app.brush->SetColor(fill);
        app.renderTarget->FillRoundedRectangle(rounded, app.brush);
        app.brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, alpha));
        float s = size / 13.0f;
        app.renderTarget->DrawLine(D2D1::Point2F(x + 3.2f * s, cy + 0.2f * s),
                                   D2D1::Point2F(x + 5.6f * s, cy + 2.6f * s),
                                   app.brush, 1.6f * s);
        app.renderTarget->DrawLine(D2D1::Point2F(x + 5.6f * s, cy + 2.6f * s),
                                   D2D1::Point2F(x + 10.0f * s, cy - 2.4f * s),
                                   app.brush, 1.6f * s);
    } else {
        D2D1_COLOR_F edge = app.theme.text;
        edge.a = 0.38f * alpha;
        app.brush->SetColor(edge);
        app.renderTarget->DrawRoundedRectangle(rounded, app.brush, 1.0f);
    }
}

// Keep the current match's row in view whenever the current match moves
// (Enter in the bar, a new query); a click on a row has already set it
void followCurrent(App& app, const std::vector<SearchPanelRow>& rows) {
    if (app.searchCurrentIndex == app.searchPanelFollow) return;
    app.searchPanelFollow = app.searchCurrentIndex;
    D2D1_RECT_F list = searchPanelListRect(app);
    float height = list.bottom - list.top;
    for (const SearchPanelRow& row : rows) {
        if (row.kind != SPR_MATCH || row.index != app.searchCurrentIndex) continue;
        if (row.top < app.searchPanelScroll ||
            row.top + row.height > app.searchPanelScroll + height) {
            app.searchPanelScroll = row.top - height * 0.4f;
        }
        break;
    }
    app.searchPanelScroll =
        std::clamp(app.searchPanelScroll, 0.0f, maxScroll(app, rows));
}

// Scope chips start a fresh scan of the other files; the document's own
// matches stay as they are
void rescanOtherFiles(App& app) {
    clearFolderSearch(app);
    startFolderSearchScan(app);
}

}  // namespace

D2D1_RECT_F searchPanelListRect(const App& app) {
    D2D1_RECT_F card = cardRect(app);
    return D2D1::RectF(card.left + dpi(app, 6.0f), card.top + dpi(app, 96.0f),
                       card.right - dpi(app, 6.0f),
                       std::max(card.top + dpi(app, 96.0f),
                                card.bottom - dpi(app, 26.0f)));
}

std::vector<SearchPanelRow> searchPanelRows(const App& app) {
    std::vector<SearchPanelRow> rows;
    float y = 0.0f;
    auto push = [&](SearchPanelRowKind kind, int index, int file,
                    const wchar_t* note = nullptr) {
        SearchPanelRow row;
        row.kind = kind;
        row.index = index;
        row.file = file;
        row.note = note;
        row.top = y;
        row.height = rowHeight(app, kind);
        y += row.height;
        rows.push_back(row);
    };
    if (app.searchQuery.empty()) {
        push(SPR_NOTE, -1, -1, tr(app, "search.results.type"));
        return rows;
    }
    push(SPR_DOCUMENT, -1, -1);
    if (app.searchMatches.empty()) {
        push(SPR_NOTE, -1, -1, tr(app, "search.no_matches"));
    }
    // Headings and matches both run in document order: walk them together
    int heading = -1, shown = -1;
    for (size_t i = 0; i < app.searchMatches.size(); i++) {
        const App::SearchMatch& m = app.searchMatches[i];
        if (matchPlaced(m)) {
            while (heading + 1 < (int)app.headings.size() &&
                   app.headings[heading + 1].y <= m.highlightRect.top + 0.5f) {
                heading++;
            }
        }
        if (heading != shown) {
            push(SPR_SECTION, heading, -1);
            shown = heading;
        }
        push(SPR_MATCH, (int)i, -1);
    }
    bool tabsCaption = false, folderCaption = false;
    for (size_t f = 0; f < app.folderResults.size(); f++) {
        const App::FolderFileResult& file = app.folderResults[f];
        if (file.openTab && !tabsCaption) {
            push(SPR_CAPTION, 0, -1, tr(app, "search.results.tabs"));
            tabsCaption = true;
        } else if (!file.openTab && !folderCaption) {
            push(SPR_CAPTION, 1, -1, tr(app, "search.results.folder"));
            folderCaption = true;
        }
        push(SPR_FILE, -1, (int)f);
        for (size_t k = 0; k < file.matches.size(); k++) {
            push(SPR_FILE_MATCH, (int)k, (int)f);
        }
        if ((size_t)file.totalMatches > file.matches.size()) push(SPR_MORE, -1, (int)f);
    }
    if (app.folderSearchPending) {
        push(SPR_NOTE, -1, -1, tr(app, "search.results.scanning"));
    }
    return rows;
}

void openSearchPanel(App& app) {
    if (app.editMode) return;  // the editor has no side panels
    openSearchInput(app);
    if (app.showSearchPanel) return;
    // The panel takes the Contents slot; the outline returns on close.
    // Contents at full width hands its place over without a slide.
    app.searchPanelRestoreToc = app.showToc;
    if (!app.showToc) app.tocAnimation = 0.0f;
    app.showToc = false;
    app.tocFilter.clear();
    app.showSearchPanel = true;
    app.searchPanelScroll = 0.0f;
    app.searchPanelFollow = -1;
    // The document's matches are there already; the other files follow
    clearFolderSearch(app);
    startFolderSearchScan(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void toggleSearchPanel(App& app) {
    if (app.showSearchPanel) {
        closeSearchPanel(app);
    } else {
        openSearchPanel(app);
    }
}

void closeSearchPanel(App& app) {
    if (!app.showSearchPanel) return;
    app.showSearchPanel = false;
    clearFolderSearch(app);
    if (app.searchPanelRestoreToc && !app.editMode) {
        app.showToc = true;
        app.tocAnimation = 1.0f;
    }
    app.searchPanelRestoreToc = false;
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void searchPanelJumpTo(App& app, int matchIndex) {
    if (matchIndex < 0 || matchIndex >= (int)app.searchMatches.size()) return;
    app.searchCurrentIndex = matchIndex;
    app.searchPanelFollow = matchIndex;  // the pressed row is in view
    scrollToCurrentMatch(app);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void searchPanelLandOnLine(App& app, int line) {
    if (app.searchMatches.empty()) return;
    int target = 0;
    if (line > 1 && !app.scrollAnchors.empty() && !app.sourceText.empty()) {
        // The end of that line in the loaded source: anchors point at a
        // block's first text, past markers such as "## "
        size_t offset = 0;
        for (int current = 1; current < line; current++) {
            size_t next = app.sourceText.find('\n', offset);
            if (next == std::string::npos) break;
            offset = next + 1;
        }
        size_t lineEnd = app.sourceText.find('\n', offset);
        if (lineEnd == std::string::npos) lineEnd = app.sourceText.size();
        size_t anchor = 0;
        for (size_t i = 0; i < app.scrollAnchors.size(); i++) {
            if (app.scrollAnchors[i].sourceOffset > lineEnd) break;
            anchor = i;
        }
        float blockTop = app.scrollAnchors[anchor].renderedY;
        target = (int)app.searchMatches.size() - 1;
        for (size_t i = 0; i < app.searchMatches.size(); i++) {
            const App::SearchMatch& m = app.searchMatches[i];
            if (matchPlaced(m) && m.highlightRect.bottom > blockTop) {
                target = (int)i;
                break;
            }
        }
    }
    searchPanelJumpTo(app, target);
}

void searchPanelOpenFile(App& app, HWND hwnd, int file, int match) {
    if (file < 0 || file >= (int)app.folderResults.size()) return;
    // Opening the tab searches the new document and rescans the others,
    // which replaces this list: keep what is needed first
    const App::FolderFileResult result = app.folderResults[file];
    int line = 0;
    if (match >= 0 && match < (int)result.matches.size()) {
        line = result.matches[match].line;
    } else if (!result.matches.empty()) {
        line = result.matches.front().line;
    }
    tabOpenPath(app, hwnd, toUtf8(result.fullPath), true);
    if (app.showSearch && !app.editMode) {
        searchPanelLandOnLine(app, line);
        app.searchPanelScroll = 0.0f;
        app.searchPanelFollow = -1;  // bring the landed match's row in view
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool searchPanelMouseDown(App& app, HWND hwnd, float x, float y) {
    if (!app.showSearch || app.editMode) return false;
    if (inside(searchResultsButtonRect(app), x, y)) {
        toggleSearchPanel(app);
        app.swallowNextMouseUp = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (!app.showSearchPanel || !inside(cardRect(app), x, y)) return false;
    // The panel owns every press inside it, rows or not
    app.swallowNextMouseUp = true;
    if (inside(closeRect(app), x, y)) {
        closeSearchPanel(app);
    } else if (inside(scopeRect(app, 0), x, y)) {
        app.tabSearchEnabled = !app.tabSearchEnabled;
        persistSearchScopes(app);
        rescanOtherFiles(app);
    } else if (inside(scopeRect(app, 1), x, y)) {
        app.folderSearchEnabled = !app.folderSearchEnabled;
        persistSearchScopes(app);
        rescanOtherFiles(app);
    } else {
        std::vector<SearchPanelRow> rows = searchPanelRows(app);
        int index = rowAt(app, rows, x, y);
        if (index >= 0) {
            const SearchPanelRow& row = rows[index];
            switch (row.kind) {
                case SPR_DOCUMENT:
                case SPR_SECTION:
                    searchPanelJumpTo(app, firstMatchFrom(rows, index));
                    break;
                case SPR_MATCH:
                    searchPanelJumpTo(app, row.index);
                    break;
                case SPR_FILE:
                case SPR_MORE:
                    searchPanelOpenFile(app, hwnd, row.file, -1);
                    break;
                case SPR_FILE_MATCH:
                    searchPanelOpenFile(app, hwnd, row.file, row.index);
                    break;
                default:
                    break;
            }
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool searchPanelWheel(App& app, float x, float y, float delta) {
    if (!app.showSearchPanel || app.editMode || !inside(cardRect(app), x, y)) {
        return false;
    }
    std::vector<SearchPanelRow> rows = searchPanelRows(app);
    app.searchPanelScroll = std::clamp(
        app.searchPanelScroll - delta * dpi(app, 60.0f), 0.0f, maxScroll(app, rows));
    return true;
}

bool searchPanelSetCursor(App& app, float x, float y) {
    if (!app.showSearch || app.editMode) return false;
    if (inside(searchResultsButtonRect(app), x, y)) {
        SetCursor(cursorHand());
        return true;
    }
    if (!app.showSearchPanel || !inside(cardRect(app), x, y)) return false;
    bool hand = inside(closeRect(app), x, y) || inside(scopeRect(app, 0), x, y) ||
                inside(scopeRect(app, 1), x, y);
    if (!hand) {
        std::vector<SearchPanelRow> rows = searchPanelRows(app);
        int index = rowAt(app, rows, x, y);
        hand = index >= 0 && rowActs(rows[index].kind);
    }
    SetCursor(hand ? cursorHand() : cursorArrow());
    return true;
}

void renderSearchPanel(App& app) {
    if (!app.showSearchPanel) return;
    // Slides in like Contents, unless Contents handed over its place
    if (app.tocAnimation < 1.0f) {
        float prev = app.tocAnimation;
        app.tocAnimation = std::min(1.0f, app.tocAnimation + 0.15f);
        if (app.tocAnimation != prev) InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    float anim = app.tocAnimation;
    IDWriteTextFormat* bold = app.tocFormatBold;
    IDWriteTextFormat* normal = app.tocFormat;
    D2D1_RECT_F card = cardRect(app);
    D2D1_COLOR_F surface = promptChipSurface(app);
    surface.a = 1.0f;
    app.brush->SetColor(surface);
    app.renderTarget->FillRectangle(card, app.brush);
    if (!bold || !normal) return;

    float pad = dpi(app, 12.0f);
    D2D1_COLOR_F text = app.theme.text;
    D2D1_COLOR_F muted = text;
    muted.a = 0.55f * anim;
    D2D1_COLOR_F divider = text;
    divider.a = 0.1f;
    float mx = (float)app.mouseX, my = (float)app.mouseY;

    // Header: title and the close cross
    float headerY = card.top + dpi(app, 9.0f);
    D2D1_COLOR_F heading = app.theme.heading;
    heading.a = anim;
    app.brush->SetColor(heading);
    const wchar_t* title = tr(app, "search.results.title");
    app.renderTarget->DrawText(title, (UINT32)wcslen(title), bold,
        D2D1::RectF(card.left + pad, headerY, card.right - pad - dpi(app, 24.0f),
                    headerY + dpi(app, 26.0f)),
        app.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    {
        D2D1_RECT_F close = closeRect(app);
        float cx = close.left + dpi(app, 7.0f);
        float cy = headerY + dpi(app, 9.0f);
        D2D1_COLOR_F c = text;
        c.a = (inside(close, mx, my) ? 0.8f : 0.45f) * anim;
        app.brush->SetColor(c);
        float s = dpi(app, 3.6f);
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s, cy - s),
                                   D2D1::Point2F(cx + s, cy + s), app.brush, 1.3f);
        app.renderTarget->DrawLine(D2D1::Point2F(cx - s, cy + s),
                                   D2D1::Point2F(cx + s, cy - s), app.brush, 1.3f);
    }
    app.brush->SetColor(divider);
    app.renderTarget->FillRectangle(
        D2D1::RectF(card.left + pad, card.top + dpi(app, 35.0f), card.right - pad,
                    card.top + dpi(app, 36.0f)),
        app.brush);

    // Scope: other open tabs and the document's folder join the search
    const bool scopes[2] = {app.tabSearchEnabled, app.folderSearchEnabled};
    const char* scopeKeys[2] = {"search.results.tabs", "search.results.folder"};
    for (int scope = 0; scope < 2; scope++) {
        D2D1_RECT_F r = scopeRect(app, scope);
        if (inside(r, mx, my)) {
            D2D1_COLOR_F hover = text;
            hover.a = 0.05f * anim;
            app.brush->SetColor(hover);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(r, dpi(app, 5.0f), dpi(app, 5.0f)), app.brush);
        }
        float cy = (r.top + r.bottom) * 0.5f;
        drawCheckbox(app, r.left + dpi(app, 4.0f), cy, scopes[scope], anim);
        D2D1_COLOR_F ink = text;
        ink.a = 0.82f * anim;
        drawLabel(app, tr(app, scopeKeys[scope]), normal,
                  D2D1::RectF(r.left + dpi(app, 25.0f), r.top, r.right, r.bottom), ink);
    }
    app.brush->SetColor(divider);
    app.renderTarget->FillRectangle(
        D2D1::RectF(card.left + pad, card.top + dpi(app, 91.0f), card.right - pad,
                    card.top + dpi(app, 92.0f)),
        app.brush);

    // The list
    std::vector<SearchPanelRow> rows = searchPanelRows(app);
    followCurrent(app, rows);
    D2D1_RECT_F list = searchPanelListRect(app);
    app.searchPanelScroll =
        std::clamp(app.searchPanelScroll, 0.0f, maxScroll(app, rows));
    int hovered = rowAt(app, rows, mx, my);
    app.renderTarget->PushAxisAlignedClip(list, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    std::wstring folder;
    if (!app.currentFile.empty()) {
        std::wstring wide = toWide(app.currentFile);
        size_t sep = wide.find_last_of(L"\\/");
        if (sep != std::wstring::npos) folder = wide.substr(0, sep);
    }
    for (size_t i = 0; i < rows.size(); i++) {
        const SearchPanelRow& row = rows[i];
        float top = list.top + row.top - app.searchPanelScroll;
        if (top + row.height < list.top) continue;
        if (top > list.bottom) break;
        D2D1_RECT_F r = D2D1::RectF(list.left, top, list.right, top + row.height);
        bool current = row.kind == SPR_MATCH && row.index == app.searchCurrentIndex;
        if ((int)i == hovered && rowActs(row.kind)) {
            D2D1_COLOR_F hover = text;
            hover.a = 0.06f * anim;
            app.brush->SetColor(hover);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(r.left, r.top + 1.0f, r.right, r.bottom - 1.0f),
                                  dpi(app, 6.0f), dpi(app, 6.0f)),
                app.brush);
        } else if (current) {
            D2D1_COLOR_F wash = app.theme.accent;
            wash.a = 0.13f * anim;
            app.brush->SetColor(wash);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(r.left, r.top + 1.0f, r.right, r.bottom - 1.0f),
                                  dpi(app, 6.0f), dpi(app, 6.0f)),
                app.brush);
        }
        if (current) {
            D2D1_COLOR_F bar = app.theme.accent;
            bar.a = anim;
            app.brush->SetColor(bar);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(r.left + dpi(app, 3.0f), r.top + dpi(app, 6.0f),
                                              r.left + dpi(app, 5.5f), r.bottom - dpi(app, 6.0f)),
                                  1.5f, 1.5f),
                app.brush);
        }
        float left = r.left + dpi(app, 10.0f);
        float right = r.right - dpi(app, 8.0f);
        float cy = (r.top + r.bottom) * 0.5f;
        switch (row.kind) {
            case SPR_DOCUMENT: {
                float end = drawCount(app, (int)app.searchMatches.size(), right, cy, anim);
                drawLabel(app, documentTitle(app), bold,
                          D2D1::RectF(left, r.top, end, r.bottom), heading);
                break;
            }
            case SPR_SECTION: {
                if (row.index < 0 || row.index >= (int)app.headings.size()) break;
                const App::HeadingInfo& h = app.headings[row.index];
                D2D1_COLOR_F ink = app.theme.heading;
                ink.a = 0.78f * anim;
                float indent = dpi(app, 4.0f) * (float)std::clamp(h.level - 1, 0, 3);
                drawLabel(app, h.text, normal,
                          D2D1::RectF(left + indent, r.top, right, r.bottom), ink,
                          DWRITE_FONT_WEIGHT_SEMI_BOLD);
                break;
            }
            case SPR_MATCH: {
                if (row.index < 0 || row.index >= (int)app.searchMatches.size()) break;
                const App::SearchMatch& m = app.searchMatches[row.index];
                size_t start = 0;
                std::wstring snippet = snippetAround(app.docText, m.startPos, m.length, start);
                drawSnippet(app, snippet, start, m.length,
                            D2D1::RectF(left + dpi(app, 12.0f), r.top, right, r.bottom),
                            anim);
                break;
            }
            case SPR_CAPTION: {
                if (!row.note || !app.themeHeaderFormat) break;
                drawLabel(app, row.note, app.themeHeaderFormat,
                          D2D1::RectF(left, r.top + dpi(app, 8.0f), right, r.bottom), muted);
                break;
            }
            case SPR_FILE: {
                if (row.file < 0 || row.file >= (int)app.folderResults.size()) break;
                const App::FolderFileResult& file = app.folderResults[row.file];
                float end = drawCount(app, file.totalMatches, right, cy, anim);
                // A tab from elsewhere names its folder after the file
                std::wstring where;
                size_t sep = file.fullPath.find_last_of(L"\\/");
                std::wstring parent = sep == std::wstring::npos ? L"" : file.fullPath.substr(0, sep);
                if (!parent.empty() && _wcsicmp(parent.c_str(), folder.c_str()) != 0) {
                    size_t up = parent.find_last_of(L"\\/");
                    where = up == std::wstring::npos ? parent : parent.substr(up + 1);
                }
                D2D1_COLOR_F ink = text;
                ink.a = 0.9f * anim;
                float nameW = std::min(end - left, measureText(app, file.fileName, bold));
                drawLabel(app, file.fileName, bold, D2D1::RectF(left, r.top, end, r.bottom), ink);
                if (!where.empty() && left + nameW + dpi(app, 20.0f) < end) {
                    drawLabel(app, where, normal,
                              D2D1::RectF(left + nameW + dpi(app, 8.0f), r.top, end, r.bottom),
                              muted);
                }
                break;
            }
            case SPR_FILE_MATCH: {
                if (row.file < 0 || row.file >= (int)app.folderResults.size()) break;
                const App::FolderFileResult& file = app.folderResults[row.file];
                if (row.index < 0 || row.index >= (int)file.matches.size()) break;
                const App::FolderMatch& m = file.matches[row.index];
                wchar_t line[16];
                swprintf_s(line, L"%d", m.line);
                float numberW = dpi(app, 34.0f);
                drawLabel(app, line, normal,
                          D2D1::RectF(left + dpi(app, 4.0f), r.top, left + numberW, r.bottom),
                          muted);
                size_t start = 0;
                std::wstring snippet = snippetAround(m.snippet, m.matchStart, m.matchLen, start);
                drawSnippet(app, snippet, start, m.matchLen,
                            D2D1::RectF(left + numberW + dpi(app, 4.0f), r.top, right, r.bottom),
                            anim);
                break;
            }
            case SPR_MORE: {
                if (row.file < 0 || row.file >= (int)app.folderResults.size()) break;
                const App::FolderFileResult& file = app.folderResults[row.file];
                wchar_t more[64];
                swprintf_s(more, tr(app, "search.results.more"),
                           file.totalMatches - (int)file.matches.size());
                drawLabel(app, more, normal,
                          D2D1::RectF(left + dpi(app, 38.0f), r.top, right, r.bottom), muted);
                break;
            }
            case SPR_NOTE: {
                if (!row.note) break;
                drawLabel(app, row.note, normal, D2D1::RectF(left, r.top, right, r.bottom),
                          muted);
                break;
            }
        }
    }
    app.renderTarget->PopAxisAlignedClip();

    // Scrollbar once the list outgrows its room
    float total = rowsHeight(rows);
    float height = list.bottom - list.top;
    if (total > height && height > 0.0f) {
        float thumb = std::max(dpi(app, 20.0f), height / total * height);
        float range = std::max(1.0f, maxScroll(app, rows));
        float thumbY = list.top + app.searchPanelScroll / range * (height - thumb);
        D2D1_COLOR_F c = text;
        c.a = 0.25f * anim;
        app.brush->SetColor(c);
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(card.right - dpi(app, 6.0f), thumbY,
                                          card.right - dpi(app, 3.0f), thumbY + thumb),
                              1.5f, 1.5f),
            app.brush);
    }

    // Footer: how to step through, and the shortcut
    float footTop = list.bottom;
    app.brush->SetColor(divider);
    app.renderTarget->FillRectangle(
        D2D1::RectF(card.left + pad, footTop, card.right - pad, footTop + 1.0f), app.brush);
    if (app.signalSmallFormat) {
        float keyW = promptKeycap(app, L"Ctrl+Shift+F", card.right - pad,
                                  footTop + dpi(app, 13.0f));
        D2D1_COLOR_F hint = text;
        hint.a = 0.48f * anim;
        app.brush->SetColor(hint);
        panelFooterText(app, tr(app, "search.results.hint"),
                        D2D1::RectF(card.left + pad, footTop + dpi(app, 7.0f),
                                    card.right - pad - keyW, card.bottom));
    }
}
