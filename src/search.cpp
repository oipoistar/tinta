#include "search.h"
#include "utils.h"
#include "render.h"

#include <algorithm>
#include <limits>

namespace {
constexpr size_t kNoTextRect = std::numeric_limits<size_t>::max();
}

void performSearch(App& app) {
    app.searchMatches.clear();
    app.searchCurrentIndex = 0;
    app.searchMatchCursor = 0;

    if (app.searchQuery.empty() || !app.root) return;

    // docText and textRects must cover the whole document before searching
    ensureLayoutComplete(app);

    // Use layout-built document text when available
    if (app.docText.empty()) {
        extractText(app.root, app.docText);
    }
    if (app.docText.empty()) return;
    if (app.docTextLower.empty()) {
        app.docTextLower = toLower(app.docText);
    }

    std::wstring queryLower = toLower(app.searchQuery);
    const std::wstring& textLower = app.docTextLower;

    // Estimate match count to avoid repeated vector reallocation
    app.searchMatches.reserve(64);

    size_t pos = 0;
    while ((pos = textLower.find(queryLower, pos)) != std::wstring::npos) {
        App::SearchMatch match;
        match.textRectIndex = kNoTextRect;
        match.startPos = pos;
        match.length = app.searchQuery.length();
        match.highlightRect = D2D1::RectF(0, 0, 0, 0);
        app.searchMatches.push_back(match);
        pos += app.searchQuery.length();
    }

    mapSearchMatchesToLayout(app);
}

void mapSearchMatchesToLayout(App& app) {
    for (auto& match : app.searchMatches) {
        match.textRectIndex = kNoTextRect;
        match.highlightRect = D2D1::RectF(0, 0, 0, 0);
    }
    if (app.searchMatches.empty()) return;
    if (app.textRects.empty()) return;

    size_t matchIndex = 0;
    for (size_t textRectIndex = 0; textRectIndex < app.textRects.size();
         textRectIndex++) {
        const auto& tr = app.textRects[textRectIndex];
        size_t rectStart = tr.docStart;
        size_t rectEnd = rectStart + tr.docLength;
        if (rectEnd <= rectStart) continue;

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
            auto& m = app.searchMatches[mi];
            if (m.startPos >= rectEnd) break;

            size_t mEnd = m.startPos + m.length;
            size_t overlapStart = std::max(rectStart, m.startPos);
            size_t overlapEnd = std::min(rectEnd, mEnd);
            if (overlapStart < overlapEnd) {
                float totalWidth = tr.rect.right - tr.rect.left;
                float charWidth = totalWidth / static_cast<float>(tr.docLength);
                float startX =
                    tr.rect.left + static_cast<float>(overlapStart - rectStart) * charWidth;
                float endX =
                    startX + static_cast<float>(overlapEnd - overlapStart) * charWidth;
                D2D1_RECT_F fragment =
                    D2D1::RectF(startX, tr.rect.top, endX, tr.rect.bottom);

                if (m.textRectIndex == kNoTextRect) {
                    m.textRectIndex = textRectIndex;
                    m.highlightRect = fragment;
                } else {
                    m.highlightRect.left = std::min(m.highlightRect.left, fragment.left);
                    m.highlightRect.top = std::min(m.highlightRect.top, fragment.top);
                    m.highlightRect.right = std::max(m.highlightRect.right, fragment.right);
                    m.highlightRect.bottom = std::max(m.highlightRect.bottom, fragment.bottom);
                }
            }

            if (mEnd <= rectEnd) {
                mi++;
            } else {
                break;
            }
        }

        matchIndex = mi;
    }
}

void scrollToCurrentMatch(App& app) {
    if (app.searchMatches.empty() || app.searchCurrentIndex < 0 ||
        app.searchCurrentIndex >= (int)app.searchMatches.size()) return;

    const auto& match = app.searchMatches[app.searchCurrentIndex];

    bool hasLayoutBounds = match.textRectIndex != kNoTextRect;
    float estimatedY = hasLayoutBounds ? match.highlightRect.top : -1.0f;

    // Fallback: estimate based on character ratio
    if (estimatedY < 0.0f) {
        if (app.docText.empty()) return;
        float positionRatio = (float)match.startPos / (float)app.docText.length();
        estimatedY = positionRatio * app.contentHeight;
    }

    // Center this position in viewport (account for search bar)
    float searchBarHeight = 60.0f;
    app.targetScrollY = estimatedY - (app.height - searchBarHeight) / 2.0f;

    // Clamp scroll
    float maxScroll = std::max(0.0f, app.contentHeight - app.height);
    app.targetScrollY = std::max(0.0f, std::min(app.targetScrollY, maxScroll));
    app.scrollY = app.targetScrollY;

    if (hasLayoutBounds) {
        float viewportWidth = documentViewportWidth(app);
        float viewportLeft = app.scrollX;
        float viewportRight = viewportLeft + viewportWidth;
        if (viewportWidth > 0.0f &&
            (match.highlightRect.left < viewportLeft ||
             match.highlightRect.right > viewportRight)) {
            float matchCenter =
                (match.highlightRect.left + match.highlightRect.right) * 0.5f;
            app.targetScrollX = matchCenter - viewportWidth * 0.5f;
        } else {
            app.targetScrollX = app.scrollX;
        }

        float maxScrollX = std::max(0.0f, app.contentWidth - viewportWidth);
        app.targetScrollX =
            std::max(0.0f, std::min(app.targetScrollX, maxScrollX));
        app.scrollX = app.targetScrollX;
    }
}
