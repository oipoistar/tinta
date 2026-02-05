#include "file_utils.h"
#include "utils.h"

#include <algorithm>

void updateFileWriteTime(App& app) {
    if (app.currentFile.empty()) return;
    std::wstring widePath = toWide(app.currentFile);
    HANDLE h = CreateFileW(widePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        GetFileTime(h, nullptr, nullptr, &app.lastFileWriteTime);
        CloseHandle(h);
    }
}

bool isRootPath(const std::wstring& path) {
    if (path.length() == 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        return true;
    }
    return false;
}

std::wstring getParentPath(const std::wstring& path) {
    if (path.empty()) return path;
    // Remove trailing slash if present
    std::wstring p = path;
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) {
        p.pop_back();
    }
    // Find last separator
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return p;
    // Handle root case (C:\ -> C:\)
    if (pos == 2 && p[1] == L':') return p.substr(0, 3);
    return p.substr(0, pos);
}

std::wstring getDirectoryFromFile(const std::string& filePath) {
    std::wstring wide = toWide(filePath);
    size_t pos = wide.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return wide.substr(0, pos);
}

void populateFolderItems(App& app) {
    app.folderItems.clear();
    app.hoveredFolderIndex = -1;
    app.folderBrowserScroll = 0.0f;

    if (app.folderBrowserPath.empty()) return;

    // Add ".." entry if not at root
    if (!isRootPath(app.folderBrowserPath)) {
        app.folderItems.push_back({L"..", true});
    }

    // Build search pattern
    std::wstring searchPath = app.folderBrowserPath;
    if (searchPath.back() != L'\\' && searchPath.back() != L'/') {
        searchPath += L'\\';
    }
    searchPath += L"*";

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;

    std::vector<App::FolderItem> folders;
    std::vector<App::FolderItem> files;

    do {
        std::wstring name = findData.cFileName;

        // Skip . and ..
        if (name == L"." || name == L"..") continue;

        // Skip hidden files
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) continue;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            folders.push_back({name, true});
        } else {
            // Check file extension
            size_t dotPos = name.rfind(L'.');
            if (dotPos != std::wstring::npos) {
                std::wstring ext = name.substr(dotPos);
                // Convert to lowercase for comparison
                for (auto& c : ext) c = towlower(c);
                if (ext == L".md" || ext == L".markdown") {
                    files.push_back({name, false});
                }
            }
        }
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    // Sort alphabetically (case-insensitive)
    auto cmpFunc = [](const App::FolderItem& a, const App::FolderItem& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    };
    std::sort(folders.begin(), folders.end(), cmpFunc);
    std::sort(files.begin(), files.end(), cmpFunc);

    // Add folders first, then files
    for (auto& f : folders) app.folderItems.push_back(std::move(f));
    for (auto& f : files) app.folderItems.push_back(std::move(f));
}
