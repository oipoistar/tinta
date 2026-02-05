#include "search.h"
#include "utils.h"

void performSearch(App& app) {
    app.searchMatches.clear();
    app.searchCurrentIndex = 0;
    app.searchMatchCursor = 0;

    if (app.searchQuery.empty() || !app.root) return;

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
        match.textRectIndex = 0;
        match.startPos = pos;
        match.length = app.searchQuery.length();
        match.highlightRect = D2D1::RectF(0, 0, 0, 0);
        app.searchMatches.push_back(match);
        pos += app.searchQuery.length();
    }

    app.searchMatchYs.assign(app.searchMatches.size(), -1.0f);
    mapSearchMatchesToLayout(app);
}

void mapSearchMatchesToLayout(App& app) {
    if (app.searchMatches.empty()) {
        app.searchMatchYs.clear();
        return;
    }
    if (app.textRects.empty()) return;
    if (app.searchMatchYs.size() != app.searchMatches.size()) {
        app.searchMatchYs.assign(app.searchMatches.size(), -1.0f);
    }

    size_t matchIndex = 0;
    for (const auto& tr : app.textRects) {
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
            const auto& m = app.searchMatches[mi];
            if (m.startPos >= rectEnd) break;

            if (app.searchMatchYs[mi] < 0.0f) {
                // Use line top as match Y (document coordinates)
                app.searchMatchYs[mi] = tr.rect.top;
            }
            mi++;
        }

        matchIndex = mi;
    }
}

void scrollToCurrentMatch(App& app) {
    if (app.searchMatches.empty() || app.searchCurrentIndex < 0 ||
        app.searchCurrentIndex >= (int)app.searchMatches.size()) return;

    const auto& match = app.searchMatches[app.searchCurrentIndex];

    float estimatedY = -1.0f;

    // Prefer exact line Y recorded during render
    if (app.searchCurrentIndex >= 0 &&
        app.searchCurrentIndex < (int)app.searchMatchYs.size()) {
        float matchY = app.searchMatchYs[app.searchCurrentIndex];
        if (matchY >= 0.0f) {
            estimatedY = matchY;
        }
    }

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
}
