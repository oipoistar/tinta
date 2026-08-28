#include "settings.h"

#include <windows.h>
#include <shlobj.h>
#include <string>

// Portable mode (#147): a settings.ini beside the executable makes that
// folder the configuration home - settings, themes.ini, languages.ini,
// and drafts all live there, so the whole setup travels with the exe.
// Without it, everything stays in %APPDATA%\Tinta as before (Store
// installs land here automatically: their folder is read-only, so an
// exe-adjacent settings.ini cannot exist). Resolved once per run.
// Lives in its own translation unit so the test targets that compile
// themes.cpp or i18n.cpp can link it without dragging in settings.cpp.
std::wstring tintaConfigDir() {
    static std::wstring cached;
    static bool resolved = false;
    if (resolved) return cached;
    resolved = true;

    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        std::wstring dir = exePath;
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            dir = dir.substr(0, slash);
            DWORD attrs =
                GetFileAttributesW((dir + L"\\settings.ini").c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                cached = dir;
                return cached;
            }
        }
    }

    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0,
                                   appDataPath))) {
        std::wstring path = appDataPath;
        path += L"\\Tinta";
        CreateDirectoryW(path.c_str(), nullptr);  // Create if not exists
        cached = path;
    }
    return cached;
}
