#include "settings.h"
#include "document.h"
#include "i18n.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <appmodel.h>
#include <fstream>
#include <string>

static int settingsLanguageIndex(const Settings& settings) {
    if (settings.language != "auto") {
        int index = languageIndexById(settings.language);
        if (index >= 0) return index;
    }
    return detectSystemLanguage();
}

std::wstring getSettingsPath() {
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        std::wstring path = appDataPath;
        path += L"\\Tinta";
        CreateDirectoryW(path.c_str(), nullptr);  // Create if not exists
        path += L"\\settings.ini";
        return path;
    }
    return L"";
}

void saveSettings(const Settings& settings) {
    std::wstring path = getSettingsPath();
    if (path.empty()) return;

    std::ofstream file(path);
    if (!file) return;

    file << "[Settings]\n";
    file << "themeIndex=" << settings.themeIndex << "\n";
    file << "zoomFactor=" << settings.zoomFactor << "\n";
    file << "windowX=" << settings.windowX << "\n";
    file << "windowY=" << settings.windowY << "\n";
    file << "windowWidth=" << settings.windowWidth << "\n";
    file << "windowHeight=" << settings.windowHeight << "\n";
    file << "windowMaximized=" << (settings.windowMaximized ? 1 : 0) << "\n";
    file << "hasAskedFileAssociation=" << (settings.hasAskedFileAssociation ? 1 : 0) << "\n";
    file << "editorShowPreview=" << (settings.editorShowPreview ? 1 : 0) << "\n";
    file << "editorWordWrap=" << (settings.editorWordWrap ? 1 : 0) << "\n";
    file << "editorAssists=" << (settings.editorAssists ? 1 : 0) << "\n";
    if (!settings.pandocPath.empty()) {
        file << "pandocPath=" << settings.pandocPath << "\n";
    }
    file << "followSystemTheme=" << (settings.followSystemTheme ? 1 : 0) << "\n";
    file << "lightThemeIndex=" << settings.lightThemeIndex << "\n";
    file << "darkThemeIndex=" << settings.darkThemeIndex << "\n";
    file << "folderSearchEnabled=" << (settings.folderSearchEnabled ? 1 : 0) << "\n";
    file << "browserFocusPath=" << (settings.browserFocusPath ? 1 : 0) << "\n";
    file << "openInTabs=" << (settings.openInTabs ? 1 : 0) << "\n";
    file << "readingWidthPct=" << settings.readingWidthPct << "\n";
    file << "zenWidthPct=" << settings.zenWidthPct << "\n";
    file << "tocOnLeft=" << (settings.tocOnLeft ? 1 : 0) << "\n";
    file << "language=" << settings.language << "\n";
    file << "keyProfile=" << settings.keyProfile << "\n";
    file << "checkUpdates=" << (settings.checkUpdates ? 1 : 0) << "\n";
    if (!settings.lastUpdateCheck.empty()) {
        file << "lastUpdateCheck=" << settings.lastUpdateCheck << "\n";
    }
    if (!settings.dismissedUpdate.empty()) {
        file << "dismissedUpdate=" << settings.dismissedUpdate << "\n";
    }

    // Remappable keys ("custom" profile), written with every save so the
    // section documents itself: change a value, restart Tinta
    file << "[Keys]\n";
    file << "; single letters/digits, Tab, Space, F1-F12, or one character\n";
    for (int i = 0; i < KEY_ACTION_COUNT; i++) {
        std::string value = keyIniName(KEY_ACTIONS[i].defaultKey);
        for (const auto& kv : settings.keyOverrides) {
            if (kv.first == KEY_ACTIONS[i].name) { value = kv.second; break; }
        }
        file << KEY_ACTIONS[i].name << "=" << value << "\n";
    }

    if (!settings.sessionTabs.empty()) {
        // Open tabs from the last session, restored on the next plain launch
        file << "[Session]\n";
        file << "sessionActive=" << settings.sessionActive << "\n";
        for (const auto& path : settings.sessionTabs) {
            file << "tab=" << path << "\n";
        }
    }

    if (!settings.recentFiles.empty()) {
        // Recently opened files for the start page, most recent first
        file << "[Recent]\n";
        for (const auto& recent : settings.recentFiles) {
            file << "recent=" << recent.when << "|" << recent.path << "\n";
        }
    }

    if (!settings.readingPositions.empty()) {
        file << "[Positions]\n";
        for (const auto& pos : settings.readingPositions) {
            // Windows paths cannot contain '|'; the second field is the
            // per-document zoom (0 = none)
            file << "pos=" << pos.scrollY << "|" << pos.zoom << "|"
                 << pos.path << "\n";
        }
    }
}

// "G" -> 'G', "Tab"/"Space"/"F1".."F12" -> VK code, ":" -> ':'; 0 = invalid
static unsigned parseKeyName(const std::string& value) {
    if (value.empty()) return 0;
    if (value.size() == 1) return (unsigned)toupper((unsigned char)value[0]);
    std::string lower;
    for (char c : value) lower += (char)tolower((unsigned char)c);
    if (lower == "tab") return VK_TAB;
    if (lower == "space") return VK_SPACE;
    if (lower.size() >= 2 && lower[0] == 'f') {
        int n = atoi(lower.c_str() + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    return 0;
}

std::string keyIniName(unsigned key) {
    if (key == VK_TAB) return "Tab";
    if (key == VK_SPACE) return "Space";
    if (key >= VK_F1 && key <= VK_F12) return "F" + std::to_string(key - VK_F1 + 1);
    return std::string(1, (char)key);
}

std::wstring keyLabel(unsigned key) {
    std::string narrow = keyIniName(key);
    return std::wstring(narrow.begin(), narrow.end());
}

int keyProfileIndexById(const std::string& id) {
    for (int i = 0; i < KEY_PROFILE_COUNT; i++) {
        if (id == KEY_PROFILES[i].id) return i;
    }
    return -1;
}

void applyKeymap(App& app, const Settings& settings) {
    int prof = keyProfileIndexById(settings.keyProfile);
    for (int i = 0; i < KEY_ACTION_COUNT; i++) {
        app.keymap[i] = prof >= 0 ? KEY_PROFILES[prof].keys[i]
                                  : KEY_ACTIONS[i].defaultKey;
    }
    if (prof < 0) {
        // Custom: [Keys] overrides over the defaults, the #77 semantics
        for (const auto& kv : settings.keyOverrides) {
            for (int i = 0; i < KEY_ACTION_COUNT; i++) {
                if (kv.first == KEY_ACTIONS[i].name) {
                    unsigned key = parseKeyName(kv.second);
                    if (key) app.keymap[i] = key;
                    break;
                }
            }
        }
    }
    app.keyProfile = settings.keyProfile;
}

void rememberReadingPosition(Settings& settings, const std::string& path,
                             float scrollY, float zoom) {
    if (path.empty()) return;
    auto& list = settings.readingPositions;
    for (size_t i = 0; i < list.size(); i++) {
        if (_stricmp(list[i].path.c_str(), path.c_str()) == 0) {
            if (zoom <= 0.0f) zoom = list[i].zoom;  // keep a known zoom
            list.erase(list.begin() + i);
            break;
        }
    }
    list.insert(list.begin(), {path, scrollY, zoom});
    if (list.size() > 50) list.resize(50);
}

void persistReadingPosition(const std::string& path, float scrollY,
                            float zoom) {
    if (path.empty()) return;
    Settings settings = loadSettings();
    rememberReadingPosition(settings, path, scrollY, zoom);
    saveSettings(settings);
}

// The editor assists toggle persists immediately so fresh windows pick
// it up without waiting for this one's exit save
void persistEditorMode(const App& app) {
    Settings settings = loadSettings();
    settings.editorAssists = app.editorAssists;
    saveSettings(settings);
}

// "Open files in tabs" persists the moment it is toggled: the join-or-
// spawn decision is made by the NEXT launched process reading
// settings.ini, so an exit-time save would leave every Explorer open on
// the old mode until all windows closed (#147 follow-up). The ini is the
// single source of truth for this flag - the exit save leaves it alone.
void persistOpenInTabs(const App& app) {
    Settings settings = loadSettings();
    settings.openInTabs = app.openInTabs;
    saveSettings(settings);
}

// A theme change persists immediately: windows spawned afterwards
// (quick notes, drag-outs) read settings.ini at startup and would
// otherwise come up in the look from the previous session
void persistThemeChoice(const App& app) {
    Settings settings = loadSettings();
    settings.themeIndex = app.currentThemeIndex;
    settings.followSystemTheme = app.followSystemTheme;
    settings.lightThemeIndex = app.lightThemeIndex;
    settings.darkThemeIndex = app.darkThemeIndex;
    saveSettings(settings);
}

// Start page recents: every successful document open moves (or inserts)
// its path at the head of the [Recent] list
void persistRecentFile(const std::string& path) {
    if (path.empty()) return;
    Settings settings = loadSettings();
    auto& list = settings.recentFiles;
    for (size_t i = 0; i < list.size(); i++) {
        if (_stricmp(list[i].path.c_str(), path.c_str()) == 0) {
            list.erase(list.begin() + i);
            break;
        }
    }
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER now;
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;
    list.insert(list.begin(), {now.QuadPart, path});
    if (list.size() > 10) list.resize(10);
    saveSettings(settings);
}

void clearRecentFiles() {
    Settings settings = loadSettings();
    settings.recentFiles.clear();
    saveSettings(settings);
}

float findReadingPosition(const Settings& settings, const std::string& path) {
    for (const auto& pos : settings.readingPositions) {
        if (_stricmp(pos.path.c_str(), path.c_str()) == 0) return pos.scrollY;
    }
    return -1.0f;
}

float findReadingZoom(const Settings& settings, const std::string& path) {
    for (const auto& pos : settings.readingPositions) {
        if (_stricmp(pos.path.c_str(), path.c_str()) == 0) return pos.zoom;
    }
    return 0.0f;
}

Settings loadSettings() {
    Settings settings;
    std::wstring path = getSettingsPath();
    if (path.empty()) return settings;

    std::ifstream file(path);
    if (!file) return settings;

    bool sawKeyProfile = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '[' || line[0] == ';') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        if (key == "themeIndex") {
            int idx = std::stoi(value);
            if (idx >= 0) settings.themeIndex = idx;  // clamped at apply via themeCount()
        } else if (key == "zoomFactor") {
            float z = std::stof(value);
            if (z >= 0.5f && z <= 3.0f) settings.zoomFactor = z;
        } else if (key == "windowX") {
            settings.windowX = std::stoi(value);
        } else if (key == "windowY") {
            settings.windowY = std::stoi(value);
        } else if (key == "windowWidth") {
            int w = std::stoi(value);
            if (w >= 200) settings.windowWidth = w;
        } else if (key == "windowHeight") {
            int h = std::stoi(value);
            if (h >= 200) settings.windowHeight = h;
        } else if (key == "readingWidthPct") {
            int w = std::stoi(value);
            if (w >= 30 && w <= 100) settings.readingWidthPct = w;
        } else if (key == "zenWidthPct") {
            int w = std::stoi(value);
            if (w >= 30 && w <= 100) settings.zenWidthPct = w;
        } else if (key == "tocOnLeft") {
            settings.tocOnLeft = (value == "1");
        } else if (key == "language") {
            if (!value.empty()) settings.language = value;  // "auto" or an id
        } else if (key == "keyProfile") {
            if (value == "custom" || keyProfileIndexById(value) >= 0) {
                settings.keyProfile = value;
                sawKeyProfile = true;
            }
        } else if (key == "languageIndex") {
            // Legacy numeric form from the first i18n build
            int idx = std::stoi(value);
            const char* ids[] = {"en", "zh", "ja", "ko"};
            settings.language = (idx >= 0 && idx < 4) ? ids[idx] : "auto";
        } else if (key == "folderSearchEnabled") {
            settings.folderSearchEnabled = (value == "1");
        } else if (key == "browserFocusPath") {
            settings.browserFocusPath = (value == "1");
        } else if (key == "openInTabs") {
            settings.openInTabs = (value == "1");
        } else if (key == "checkUpdates") {
            settings.checkUpdates = (value == "1");
        } else if (key == "lastUpdateCheck") {
            settings.lastUpdateCheck = value;
        } else if (key == "dismissedUpdate") {
            settings.dismissedUpdate = value;
        } else if (key == "followSystemTheme") {
            settings.followSystemTheme = (value == "1");
        } else if (key == "lightThemeIndex") {
            int idx = std::stoi(value);
            if (idx >= 0) settings.lightThemeIndex = idx;
        } else if (key == "darkThemeIndex") {
            int idx = std::stoi(value);
            if (idx >= 0) settings.darkThemeIndex = idx;
        } else if (key == "windowMaximized") {
            settings.windowMaximized = (value == "1");
        } else if (key == "hasAskedFileAssociation") {
            settings.hasAskedFileAssociation = (value == "1");
        } else if (key == "editorShowPreview") {
            settings.editorShowPreview = (value == "1");
        } else if (key == "editorWordWrap") {
            settings.editorWordWrap = (value == "1");
        } else if (key == "editorAssists") {
            settings.editorAssists = (value == "1");
        } else if (key == "pandocPath") {
            settings.pandocPath = value;
        } else if (key == "sessionActive") {
            int idx = std::stoi(value);
            if (idx >= 0) settings.sessionActive = idx;
        } else if (key == "tab") {
            if (!value.empty() && settings.sessionTabs.size() < 64) {
                settings.sessionTabs.push_back(value);
            }
        } else if (key == "recent") {
            // when(FILETIME)|path, most recent first
            size_t sep = value.find('|');
            if (sep != std::string::npos && sep + 1 < value.size() &&
                settings.recentFiles.size() < 10) {
                try {
                    unsigned long long when =
                        std::stoull(value.substr(0, sep));
                    std::string recentPath = value.substr(sep + 1);
                    if (!recentPath.empty()) {
                        settings.recentFiles.push_back({when, recentPath});
                    }
                } catch (...) {}
            }
        } else if (key == "pos") {
            // New format: scrollY|zoom|path; legacy: scrollY|path
            size_t sep = value.find('|');
            if (sep != std::string::npos && sep + 1 < value.size()) {
                try {
                    float y = std::stof(value.substr(0, sep));
                    float zoom = 0.0f;
                    std::string path = value.substr(sep + 1);
                    size_t sep2 = path.find('|');
                    if (sep2 != std::string::npos && sep2 + 1 < path.size()) {
                        zoom = std::stof(path.substr(0, sep2));
                        path = path.substr(sep2 + 1);
                    }
                    if (y >= 0.0f && !path.empty()) {
                        settings.readingPositions.push_back({path, y, zoom});
                    }
                } catch (...) {}
            }
        } else {
            for (int i = 0; i < KEY_ACTION_COUNT; i++) {
                if (key == KEY_ACTIONS[i].name) {
                    settings.keyOverrides.push_back({key, value});
                    break;
                }
            }
        }
    }
    if (settings.readingPositions.size() > 50) settings.readingPositions.resize(50);

    // Migration from pre-profile builds (#77-era [Keys] only): users who
    // had customized any binding keep exactly what they had via the custom
    // profile; everyone else gets the Windows profile (':' and '?' stay
    // hardwired, so nothing they know stops working)
    if (!sawKeyProfile) {
        bool customized = false;
        for (const auto& kv : settings.keyOverrides) {
            for (int i = 0; i < KEY_ACTION_COUNT; i++) {
                if (kv.first == KEY_ACTIONS[i].name &&
                    kv.second != keyIniName(KEY_ACTIONS[i].defaultKey)) {
                    customized = true;
                    break;
                }
            }
            if (customized) break;
        }
        settings.keyProfile = customized ? "custom" : "windows";
    }
    return settings;
}

// Packaged (MSIX/Store) installs declare file associations in AppxManifest.xml;
// runtime registry writes would only land in the package's virtual registry.
static bool isRunningPackaged() {
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
}

static bool hasRegisteredFileAssociation(std::wstring_view extension) {
    HKEY hKey;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Tinta\\Capabilities\\FileAssociations",
        0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) return false;

    wchar_t value[64] = {};
    DWORD type = 0;
    DWORD size = sizeof(value);
    result = RegQueryValueExW(
        hKey, extension.data(), nullptr, &type,
        reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS &&
        type == REG_SZ &&
        wcscmp(value, L"Tinta.MarkdownFile") == 0;
}

bool registerFileAssociation() {
    // Get the path to the current executable
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    HKEY hKey;
    LONG result;
    const wchar_t* progId = L"Tinta.MarkdownFile";

    // Create ProgID entry in Classes
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Tinta.MarkdownFile", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    const wchar_t* desc = L"Tinta Document";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)desc, (DWORD)((wcslen(desc) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Create DefaultIcon entry
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Tinta.MarkdownFile\\DefaultIcon", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    std::wstring iconPath = exePath;
    iconPath += L",0";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)iconPath.c_str(), (DWORD)((iconPath.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Create shell\open\command entry
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Tinta.MarkdownFile\\shell\\open\\command", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    std::wstring command = L"\"";
    command += exePath;
    command += L"\" \"%1\"";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)command.c_str(), (DWORD)((command.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Register app capabilities (required for Windows 10/11)
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Tinta\\Capabilities", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    const wchar_t* appName = L"Tinta";
    const wchar_t* appDesc = L"A fast, lightweight Markdown and Mermaid reader";
    RegSetValueExW(hKey, L"ApplicationName", 0, REG_SZ, (BYTE*)appName, (DWORD)((wcslen(appName) + 1) * sizeof(wchar_t)));
    RegSetValueExW(hKey, L"ApplicationDescription", 0, REG_SZ, (BYTE*)appDesc, (DWORD)((wcslen(appDesc) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Register file associations in capabilities
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Tinta\\Capabilities\\FileAssociations", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    for (std::wstring_view extension : DOCUMENT_FILE_EXTENSIONS) {
        result = RegSetValueExW(
            hKey, extension.data(), 0, REG_SZ, (BYTE*)progId,
            (DWORD)((wcslen(progId) + 1) * sizeof(wchar_t)));
        if (result != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return false;
        }
    }
    RegCloseKey(hKey);

    // Add to RegisteredApplications
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    const wchar_t* capPath = L"Software\\Tinta\\Capabilities";
    RegSetValueExW(hKey, L"Tinta", 0, REG_SZ, (BYTE*)capPath, (DWORD)((wcslen(capPath) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    for (std::wstring_view extension : DOCUMENT_FILE_EXTENSIONS) {
        std::wstring keyPath = L"Software\\Classes\\";
        keyPath.append(extension);
        keyPath += L"\\OpenWithProgids";
        result = RegCreateKeyExW(
            HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
        if (result != ERROR_SUCCESS) return false;
        result = RegSetValueExW(hKey, progId, 0, REG_NONE, nullptr, 0);
        RegCloseKey(hKey);
        if (result != ERROR_SUCCESS) return false;
    }

    // Notify shell of the change
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return true;
}

void openDefaultAppsSettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
}

void askAndRegisterFileAssociation(Settings& settings) {
    int lang = settingsLanguageIndex(settings);
    if (isRunningPackaged()) {
        // Store installs: the package manifest already registers the file
        // types, so only the "make default" choice is missing. Ask once per
        // install (settings.ini lives in the package's private AppData, so
        // it resets on uninstall) and deep-link straight to Tinta's page in
        // Settings > Default apps. Windows 10 ignores the query part and
        // opens the general Default Apps page instead.
        if (settings.hasAskedFileAssociation) return;
        int result = MessageBoxW(
            nullptr,
            tr(lang, "fileassoc.packaged.ask_body"),
            tr(lang, "fileassoc.packaged.title"),
            MB_YESNO | MB_ICONQUESTION);
        if (result == IDYES) {
            ShellExecuteW(nullptr, L"open",
                L"ms-settings:defaultapps?registeredAppUser=Tinta%20Markdown%20Viewer",
                nullptr, nullptr, SW_SHOWNORMAL);
        }
        settings.hasAskedFileAssociation = true;
        saveSettings(settings);
        return;
    }
    if (settings.hasAskedFileAssociation) {
        if (hasRegisteredFileAssociation(L".md") &&
            !hasRegisteredFileAssociation(L".mmd") &&
            !registerFileAssociation()) {
            MessageBoxW(
                nullptr,
                tr(lang, "fileassoc.add_mmd_failed_body"),
                tr(lang, "fileassoc.title"),
                MB_OK | MB_ICONWARNING);
        }
        return;
    }

    int result = MessageBoxW(
        nullptr,
        tr(lang, "fileassoc.ask_body"),
        tr(lang, "fileassoc.title"),
        MB_YESNO | MB_ICONQUESTION
    );

    if (result == IDYES) {
        if (registerFileAssociation()) {
            MessageBoxW(nullptr,
                       tr(lang, "fileassoc.done_body"),
                       tr(lang, "fileassoc.done_title"),
                       MB_OK | MB_ICONINFORMATION);
            openDefaultAppsSettings();
        } else {
            MessageBoxW(nullptr, tr(lang, "fileassoc.register_failed_body"),
                       tr(lang, "error.title"), MB_OK | MB_ICONWARNING);
        }
    }

    settings.hasAskedFileAssociation = true;
    saveSettings(settings);
}
