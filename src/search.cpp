#include "search.h"
#include "utils.h"

void recordSearchMatchPositions(App& app, size_t segStart, size_t segEnd, float lineY) {
    if (segEnd <= segStart) return;
    if (app.searchMatchCursor >= app.searchMatches.size()) return;
    if (app.searchMatchYs.size() != app.searchMatches.size()) return;

    while (app.searchMatchCursor < app.searchMatches.size()) {
        const auto& match = app.searchMatches[app.searchMatchCursor];
        if (match.startPos < segStart) {
            app.searchMatchCursor++;
            continue;
        }
        if (match.startPos >= segEnd) {
            break;
        }
        if (app.searchMatchYs[app.searchMatchCursor] < 0.0f) {
            app.searchMatchYs[app.searchMatchCursor] = lineY;
        }
        app.searchMatchCursor++;
    }
}

void performSearch(App& app) {
    app.searchMatches.clear();
    app.searchCurrentIndex = 0;

    if (app.searchQuery.empty() || !app.root) return;

    // Use render-built document text when available
    std::wstring fullText = app.docText;
    if (fullText.empty()) {
        extractText(app.root, fullText);
        app.docText = fullText;
    }

    std::wstring queryLower = toLower(app.searchQuery);
    std::wstring textLower = toLower(fullText);

    // Count all matches in full document
    size_t pos = 0;
    while ((pos = textLower.find(queryLower, pos)) != std::wstring::npos) {
        App::SearchMatch match;
        match.textRectIndex = 0;  // Not used anymore
        match.startPos = pos;
        match.length = app.searchQuery.length();
        match.highlightRect = D2D1::RectF(0, 0, 0, 0);  // Not used anymore
        app.searchMatches.push_back(match);
        pos += app.searchQuery.length();
    }

    app.searchMatchYs.assign(app.searchMatches.size(), -1.0f);
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
        std::wstring fullText = app.docText;
        if (fullText.empty()) {
            extractText(app.root, fullText);
        }
        if (fullText.empty()) return;

        float positionRatio = (float)match.startPos / (float)fullText.length();
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
