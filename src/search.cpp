#include "search.h"
#include "utils.h"
#include "render.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

namespace {
constexpr size_t kNoTextRect = std::numeric_limits<size_t>::max();

struct FolderScanMsg {
    int generation = 0;
    std::vector<App::FolderFileResult> files;
};

std::wstring utf8ToWideString(const std::string& bytes) {
    if (bytes.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), wide.data(), len);
    return wide;
}
}

void clearFolderSearch(App& app) {
    app.folderSearchGeneration++;   // orphan any scan in flight
    app.folderResults.clear();
    app.folderSearchPending = false;
    if (app.hwnd) KillTimer(app.hwnd, TIMER_FOLDER_SEARCH);
}

namespace {

// One file for the scan, gathered on the UI thread: another tab's path
// and, while that tab is being edited, its parked buffer
struct ScanSource {
    std::filesystem::path path;
    bool hasText = false;
    std::wstring text;
};

// Every match in one file: the total, and the first few with their source
// line and a snippet of the line around them
App::FolderFileResult scanText(const std::wstring& content,
                               const std::wstring& queryLower) {
    App::FolderFileResult result;
    std::wstring lower = content;
    for (auto& c : lower) c = (wchar_t)std::towlower(c);
    size_t pos = 0, counted = 0;
    int line = 1;
    while ((pos = lower.find(queryLower, pos)) != std::wstring::npos) {
        result.totalMatches++;
        if (result.matches.size() < 3) {
            for (; counted < pos; counted++) {
                if (content[counted] == L'\n') line++;
            }
            size_t lineStart = content.rfind(L'\n', pos);
            lineStart = (lineStart == std::wstring::npos) ? 0 : lineStart + 1;
            size_t lineEnd = content.find(L'\n', pos);
            if (lineEnd == std::wstring::npos) lineEnd = content.size();
            size_t snipStart = pos > lineStart + 40 ? pos - 40 : lineStart;
            size_t snipEnd = std::min(lineEnd, pos + queryLower.size() + 70);
            App::FolderMatch m;
            m.snippet = content.substr(snipStart, snipEnd - snipStart);
            for (auto& c : m.snippet) {
                if (c == L'\r' || c == L'\t') c = L' ';
            }
            m.matchStart = pos - snipStart;
            m.matchLen = queryLower.size();
            m.line = line;
            result.matches.push_back(std::move(m));
        }
        pos += queryLower.size();
    }
    return result;
}

bool readSmallFile(const std::filesystem::path& path, std::wstring& text) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size > 1024 * 1024) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::string bytes((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    text = utf8ToWideString(bytes);
    return true;
}

bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

}  // namespace

void startFolderSearchScan(App& app) {
    app.folderSearchPending = false;
    if (app.editMode || !app.showSearchPanel || !app.showSearch ||
        app.searchQuery.empty() ||
        (!app.folderSearchEnabled && !app.tabSearchEnabled)) {
        return;
    }
    // Other tabs first, in strip order; a tab being edited is searched in
    // its parked buffer, unsaved changes included
    std::vector<ScanSource> tabs;
    if (app.tabSearchEnabled) {
        for (size_t i = 0; i < app.tabs.size(); i++) {
            const App::DocTab& tab = app.tabs[i];
            if ((int)i == app.activeTab || tab.path.empty()) continue;
            ScanSource source;
            source.path = toWide(tab.path);
            if (tab.editMode && !tab.editorText.empty()) {
                source.hasText = true;
                source.text = tab.editorText;
            }
            tabs.push_back(std::move(source));
        }
    }
    std::filesystem::path current, folder;
    if (!app.currentFile.empty()) current = toWide(app.currentFile);
    if (app.folderSearchEnabled && !current.empty()) folder = current.parent_path();
    if (tabs.empty() && folder.empty()) return;
    int generation = ++app.folderSearchGeneration;
    app.folderSearchPending = true;
    std::wstring queryLower = toLower(app.searchQuery);
    HWND hwnd = app.hwnd;

    std::thread([generation, queryLower, tabs, folder, current, hwnd] {
        auto* msg = new FolderScanMsg{generation, {}};
        auto add = [&](const std::filesystem::path& path, bool openTab,
                       const std::wstring& content) {
            App::FolderFileResult result = scanText(content, queryLower);
            if (result.totalMatches == 0) return false;
            result.fileName = path.filename().wstring();
            result.fullPath = path.wstring();
            result.openTab = openTab;
            msg->files.push_back(std::move(result));
            return true;
        };
        for (const auto& tab : tabs) {
            std::wstring content;
            if (tab.hasText) content = tab.text;
            else if (!readSmallFile(tab.path, content)) continue;
            add(tab.path, true, content);
        }
        if (!folder.empty()) {
            // The folder's other Markdown files, by name; the open document
            // and files already listed as tabs are not repeated
            std::vector<std::filesystem::path> siblings;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
                if (siblings.size() >= 400) break;
                if (!entry.is_regular_file(ec)) continue;
                std::wstring ext = entry.path().extension().wstring();
                for (auto& c : ext) c = (wchar_t)std::towlower(c);
                if (ext != L".md" && ext != L".markdown") continue;
                if (!current.empty() && sameFile(entry.path(), current)) continue;
                bool listed = false;
                for (const auto& tab : tabs) listed = listed || sameFile(entry.path(), tab.path);
                if (!listed) siblings.push_back(entry.path());
            }
            std::sort(siblings.begin(), siblings.end(),
                      [](const std::filesystem::path& a, const std::filesystem::path& b) {
                          return _wcsicmp(a.filename().c_str(), b.filename().c_str()) < 0;
                      });
            int found = 0;
            for (const auto& path : siblings) {
                if (found >= 50) break;
                std::wstring content;
                if (readSmallFile(path, content) && add(path, false, content)) found++;
            }
        }
        if (!PostMessageW(hwnd, WM_APP_FOLDER_SEARCH, 0, (LPARAM)msg)) delete msg;
    }).detach();
}

void completeFolderSearch(App& app, void* results) {
    auto* msg = static_cast<FolderScanMsg*>(results);
    if (!msg) return;
    if (msg->generation != app.folderSearchGeneration ||
        !app.showSearch || !app.showSearchPanel || app.editMode) {
        delete msg;
        return;
    }
    app.folderResults = std::move(msg->files);
    app.folderSearchPending = false;
    delete msg;
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void performSearch(App& app) {
    app.searchMatches.clear();
    app.searchCurrentIndex = 0;
    app.searchMatchCursor = 0;
    app.searchPanelFollow = -1;

    // The results panel's other files follow the query, debounced on a
    // timer so fast typing doesn't spawn a scan per keystroke. Earlier
    // hits go at once: they belong to the previous query (#246)
    if (!app.editMode && app.hwnd) {
        clearFolderSearch(app);
        if (app.showSearchPanel && !app.searchQuery.empty() &&
            (app.folderSearchEnabled || app.tabSearchEnabled)) {
            app.folderSearchPending = true;
            SetTimer(app.hwnd, TIMER_FOLDER_SEARCH, 250, nullptr);
        }
    }

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
