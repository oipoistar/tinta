#include "search.h"
#include "editor.h"
#include "input.h"
#include "tableedit.h"
#include "utils.h"
#include <algorithm>
#include <cwctype>

namespace {
using Microsoft::WRL::ComPtr;
int activeField(const App& app) { return app.searchReplaceMode && app.replaceFieldActive ? 1 : 0; }
std::wstring& textFor(App& app, int field) { return field ? app.replaceText : app.searchQuery; }
bool inside(float x, float y, const D2D1_RECT_F& r) {
    return r.right > r.left && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
void clamp(App::SearchField& state, const std::wstring& text) {
    auto boundary = [&](size_t pos) {
        pos = std::min(pos, text.size());
        if (pos && pos < text.size() && text[pos] >= 0xdc00 && text[pos] <= 0xdfff &&
            text[pos-1] >= 0xd800 && text[pos-1] <= 0xdbff) --pos;
        return pos;
    };
    state.caret = boundary(state.caret);
    state.anchor = boundary(state.anchor);
}
ComPtr<IDWriteTextLayout> fieldLayout(App& app, const std::wstring& text) {
    ComPtr<IDWriteTextLayout> layout;
    if (!app.dwriteFactory || !app.searchTextFormat) return layout;
    app.dwriteFactory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()),
        app.searchTextFormat, 1e7f, dpi(app, 40), layout.GetAddressOf());
    if (layout) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteTextLayout2> fallback;
        if (app.fontFallback && SUCCEEDED(layout.As(&fallback))) fallback->SetFontFallback(app.fontFallback);
    }
    return layout;
}
float caretX(IDWriteTextLayout* layout, size_t caret) {
    if (!layout || !caret) return 0;
    float x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS hit{};
    layout->HitTestTextPosition(static_cast<UINT32>(caret-1), TRUE, &x, &y, &hit);
    return x;
}
// DirectWrite clusters keep surrogate pairs and combining sequences intact.
size_t step(App& app, const std::wstring& text, size_t pos, bool forward) {
    auto layout = fieldLayout(app, text);
    if (layout) {
        UINT32 count = 0;
        layout->GetClusterMetrics(nullptr, 0, &count);
        std::vector<DWRITE_CLUSTER_METRICS> clusters(count);
        if (count && SUCCEEDED(layout->GetClusterMetrics(clusters.data(), count, &count))) {
            size_t start = 0;
            for (const auto& cluster : clusters) {
                size_t end = start + cluster.length;
                if ((forward && end > pos) || (!forward && end >= pos)) return forward ? end : start;
                start = end;
            }
        }
    }
    if (forward) {
        if (pos >= text.size()) return text.size();
        if (text[pos] >= 0xd800 && text[pos] <= 0xdbff && pos+1 < text.size() &&
            text[pos+1] >= 0xdc00 && text[pos+1] <= 0xdfff) return pos+2;
        return pos+1;
    }
    if (!pos) return 0;
    --pos;
    if (pos && text[pos] >= 0xdc00 && text[pos] <= 0xdfff &&
        text[pos-1] >= 0xd800 && text[pos-1] <= 0xdbff) --pos;
    return pos;
}
size_t wordStep(App& app, const std::wstring& text, size_t pos, bool forward) {
    auto category = [&](size_t at) { return iswspace(text[at]) ? 0 : (iswalnum(text[at]) || text[at] == L'_' ? 1 : 2); };
    if (forward) {
        if (pos == text.size()) return pos;
        int kind = category(pos);
        while (pos < text.size() && category(pos) == kind) pos = step(app, text, pos, true);
        while (pos < text.size() && !category(pos)) pos = step(app, text, pos, true);
    } else {
        while (pos && !category(pos-1)) pos = step(app, text, pos, false);
        if (!pos) return 0;
        int kind = category(pos-1);
        while (pos && category(pos-1) == kind) pos = step(app, text, pos, false);
    }
    return pos;
}
void changed(App& app, int field) {
    if (field) return;
    if (app.editMode) {
        performEditorSearch(app);
        app.searchCurrentIndex = app.editorSearchCurrentIndex;
        scrollEditorToMatch(app);
    } else {
        performSearch(app);
        scrollToCurrentMatch(app);
    }
}
void insert(App& app, int field, const std::wstring& value) {
    auto& text = textFor(app, field);
    auto& state = app.searchFields[field];
    clamp(state, text);
    size_t start = std::min(state.caret, state.anchor), end = std::max(state.caret, state.anchor);
    text.replace(start, end-start, value);
    state.caret = state.anchor = start + value.size();
    changed(app, field);
}
bool copySelection(HWND hwnd, const std::wstring& text) {
    bool opened = false;
    for (int attempt = 0; attempt < 10 && !(opened = OpenClipboard(hwnd) != FALSE); ++attempt) Sleep(5);
    if (!opened) return false;
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (text.size()+1)*sizeof(wchar_t));
    void* dest = data ? GlobalLock(data) : nullptr;
    bool success = false;
    if (dest) {
        memcpy(dest, text.c_str(), (text.size()+1)*sizeof(wchar_t));
        GlobalUnlock(data);
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data)) success = true;
    }
    if (!success && data) GlobalFree(data);
    CloseClipboard();
    return success;
}
size_t hitPosition(App& app, int field, float x) {
    auto& state = app.searchFields[field];
    const auto& text = textFor(app, field);
    auto layout = fieldLayout(app, text);
    if (!layout || text.empty()) return 0;
    BOOL trailing = FALSE, within = FALSE;
    DWRITE_HIT_TEST_METRICS hit{};
    layout->HitTestPoint(x-state.textRect.left+state.scrollX, 0, &trailing, &within, &hit);
    return std::min(text.size(), static_cast<size_t>(hit.textPosition) + (trailing ? hit.length : 0));
}
void followCaret(App::SearchField& state, IDWriteTextLayout* layout) {
    float width = std::max(1.0f, state.textRect.right-state.textRect.left-3);
    float x = caretX(layout, state.caret);
    if (x < state.scrollX) state.scrollX = x;
    if (x > state.scrollX+width) state.scrollX = x-width;
    DWRITE_TEXT_METRICS metrics{};
    if (layout) layout->GetMetrics(&metrics);
    state.scrollX = std::clamp(state.scrollX, 0.0f, std::max(0.0f, metrics.widthIncludingTrailingWhitespace-width));
}
}

void searchInputMouseUp(App& app) {
    if (app.searchSelectingField < 0) return;
    app.searchSelectingField = -1;
    if (GetCapture() == app.hwnd) ReleaseCapture();
}
void releaseSearchInput(App& app) {
    searchInputMouseUp(app);
    app.searchActive = false;
    resetCursorBlink(app);
}
void focusSourceEditor(App& app) {
    tableEditCommit(app);
    releaseSearchInput(app);
}
bool sourceEditorHasFocus(const App& app) {
    return app.editMode && !app.tableEditActive && !(app.showSearch && app.searchActive);
}
void focusSearchInput(App& app, bool replace) {
    bool reacquire = !app.searchActive || app.tableEditActive;
    tableEditCommit(app);
    searchInputMouseUp(app);
    app.searchActive = true;
    app.replaceFieldActive = replace && app.searchReplaceMode;
    clamp(app.searchFields[activeField(app)], textFor(app, activeField(app)));
    if (reacquire) changed(app, 0);
    resetCursorBlink(app);
    updateBlinkTimer(app);
}
void openSearchInput(App& app, bool replace) {
    if (!app.showSearch) {
        app.searchAnimation = 0;
        app.searchQuery.clear();
        app.searchFields[0] = {};
        app.searchMatches.clear();
        app.editorSearchMatches.clear();
        app.searchCurrentIndex = app.editorSearchCurrentIndex = 0;
    }
    app.showSearch = true;
    app.searchReplaceMode = app.editMode && replace;
    focusSearchInput(app, false);
    auto& state = app.searchFields[0];
    state.caret = app.searchQuery.size();
    state.anchor = 0;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}
void closeSearchInput(App& app) {
    releaseSearchInput(app);
    app.showSearch = false;
    app.searchQuery.clear();
    app.searchFields[0] = {};
    app.searchMatches.clear();
    app.editorSearchMatches.clear();
    app.searchReplaceMode = app.replaceFieldActive = false;
    app.searchCurrentIndex = app.editorSearchCurrentIndex = 0;
    app.searchReplaceHits.clear();
    app.searchAnimation = 0;
    std::wstring().swap(app.docTextLower);
    clearFolderSearch(app);
    updateBlinkTimer(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

bool searchInputKeyDown(App& app, HWND hwnd, WPARAM key) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    int field = activeField(app);
    auto& text = textFor(app, field);
    auto& state = app.searchFields[field];
    clamp(state, text);
    if (ctrl && (key == 'A' || key == 'C' || key == 'X' || key == 'V')) {
        size_t start = std::min(state.caret, state.anchor), end = std::max(state.caret, state.anchor);
        if (key == 'A') { state.anchor = 0; state.caret = text.size(); }
        if ((key == 'C' || key == 'X') && start != end && copySelection(hwnd, text.substr(start, end-start)) && key == 'X') insert(app, field, L"");
        if (key == 'V') {
            auto pasted = clipboardLine(hwnd);
            if (!pasted.empty()) insert(app, field, pasted);
        }
    } else if (key == VK_ESCAPE) {
        closeSearchInput(app);
    } else if (key == VK_TAB) {
        if (app.searchReplaceMode) focusSearchInput(app, !app.replaceFieldActive);
    } else if (key == VK_RETURN || key == VK_UP || key == VK_DOWN) {
        if (key == VK_RETURN && app.searchReplaceMode && ctrl) editorReplaceAll(app, hwnd);
        else if (key == VK_RETURN && field) editorReplaceCurrent(app, hwnd);
        else {
            int count = static_cast<int>(app.editMode ? app.editorSearchMatches.size() : app.searchMatches.size());
            int& index = app.editMode ? app.editorSearchCurrentIndex : app.searchCurrentIndex;
            if (count) index = (index + ((key == VK_UP || (key == VK_RETURN && shift)) ? count-1 : 1)) % count;
            if (app.editMode) { app.searchCurrentIndex = index; scrollEditorToMatch(app); }
            else scrollToCurrentMatch(app);
        }
    } else if (key == VK_LEFT || key == VK_RIGHT || key == VK_HOME || key == VK_END) {
        bool forward = key == VK_RIGHT;
        if (key == VK_HOME) state.caret = 0;
        else if (key == VK_END) state.caret = text.size();
        else if (!shift && state.caret != state.anchor) state.caret = forward ? std::max(state.caret, state.anchor) : std::min(state.caret, state.anchor);
        else {
            state.caret = ctrl ? wordStep(app, text, state.caret, forward) : step(app, text, state.caret, forward);
        }
        if (!shift) state.anchor = state.caret;
    } else if (key == VK_BACK || key == VK_DELETE) {
        if (state.caret == state.anchor) state.anchor = ctrl ? wordStep(app, text, state.caret, key == VK_DELETE) : step(app, text, state.caret, key == VK_DELETE);
        if (state.caret != state.anchor) insert(app, field, L"");
    } else return ctrl; // Printable keys still translate to WM_CHAR; Ctrl chords cannot edit the source.
    resetCursorBlink(app);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}
void searchInputChar(App& app, HWND hwnd, wchar_t ch) {
    if (ch >= 32 && ch != 127) {
        insert(app, activeField(app), std::wstring(1, ch));
        resetCursorBlink(app);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}
bool searchInputMouseDown(App& app, HWND hwnd, float x, float y) {
    if (!app.showSearch) return false;
    if (app.editMode && app.searchReplaceMode) {
        for (const auto& hit : app.searchReplaceHits) {
            if (hit.second < 3 || !inside(x, y, hit.first)) continue;
            focusSearchInput(app, true);
            if (hit.second == 3) editorReplaceCurrent(app, hwnd);
            else editorReplaceAll(app, hwnd);
            app.swallowNextMouseUp = true;
            return true;
        }
    }
    for (int field = 0; field < (app.searchReplaceMode ? 2 : 1); ++field) {
        auto& state = app.searchFields[field];
        if (!inside(x, y, state.hitRect)) continue;
        focusSearchInput(app, field != 0);
        state.caret = hitPosition(app, field, x);
        if (!(GetKeyState(VK_SHIFT) & 0x8000)) state.anchor = state.caret;
        app.searchSelectingField = field;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    return false;
}
bool searchInputMouseMove(App& app, float x, float) {
    if (app.searchSelectingField < 0) return false;
    int field = app.searchSelectingField;
    auto& state = app.searchFields[field];
    if (x < state.textRect.left) state.scrollX = std::max(0.0f, state.scrollX-dpi(app, 16));
    if (x > state.textRect.right) state.scrollX += dpi(app, 16);
    state.caret = hitPosition(app, field, x);
    resetCursorBlink(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
}

void renderSearchField(App& app, int field, const D2D1_RECT_F& textRect,
                       const D2D1_RECT_F& hitRect, const wchar_t* placeholder, float alpha) {
    auto& state = app.searchFields[field];
    const auto& text = textFor(app, field);
    state.textRect = textRect; state.hitRect = hitRect;
    clamp(state, text);
    auto layout = fieldLayout(app, text);
    followCaret(state, layout.Get());
    bool active = app.searchActive && activeField(app) == field;
    D2D1_POINT_2F origin = D2D1::Point2F(textRect.left-state.scrollX, textRect.top);
    app.renderTarget->PushAxisAlignedClip(textRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (active && state.anchor != state.caret && layout) {
        UINT32 count = 0;
        UINT32 start = static_cast<UINT32>(std::min(state.caret, state.anchor));
        UINT32 length = static_cast<UINT32>(std::max(state.caret, state.anchor)-start);
        layout->HitTestTextRange(start, length, origin.x, origin.y, nullptr, 0, &count);
        std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
        if (count && SUCCEEDED(layout->HitTestTextRange(start, length, origin.x, origin.y, hits.data(), count, &count))) {
            auto color = app.theme.accent; color.a = 0.25f * alpha; app.brush->SetColor(color);
            for (const auto& hit : hits) app.renderTarget->FillRectangle(
                D2D1::RectF(hit.left, hit.top, hit.left+hit.width, hit.top+hit.height), app.brush);
        }
    }
    auto color = app.theme.text; color.a = (text.empty() ? 0.4f : 1.0f) * alpha;
    app.brush->SetColor(color);
    if (text.empty()) app.renderTarget->DrawText(placeholder, static_cast<UINT32>(wcslen(placeholder)), app.searchTextFormat, textRect, app.brush);
    else if (layout) app.renderTarget->DrawTextLayout(origin, layout.Get(), app.brush);
    if (active && app.cursorBlinkOn) {
        color.a = alpha; app.brush->SetColor(color);
        float x = origin.x + caretX(layout.Get(), state.caret);
        app.renderTarget->DrawLine(D2D1::Point2F(x, textRect.top), D2D1::Point2F(x, textRect.bottom), app.brush, dpi(app, 1.5f));
    }
    app.renderTarget->PopAxisAlignedClip();
}
bool searchInputCaretPoint(App& app, D2D1_POINT_2F& point) {
    if (!app.showSearch || !app.searchActive) return false;
    int field = activeField(app);
    auto& state = app.searchFields[field];
    clamp(state, textFor(app, field));
    auto layout = fieldLayout(app, textFor(app, field));
    followCaret(state, layout.Get());
    point = D2D1::Point2F(state.textRect.left-state.scrollX+caretX(layout.Get(), state.caret), state.textRect.bottom);
    return true;
}
