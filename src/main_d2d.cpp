// Direct2D + DirectWrite renderer for Windows
// Much faster startup than OpenGL

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <unordered_set>

#include "markdown.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using namespace qmd;

// Timing helpers
using Clock = std::chrono::high_resolution_clock;
static int64_t usElapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
}

// Startup metrics
struct StartupMetrics {
    int64_t windowInitUs = 0;
    int64_t d2dInitUs = 0;
    int64_t dwriteInitUs = 0;
    int64_t renderTargetUs = 0;
    int64_t fileLoadUs = 0;
    int64_t showWindowUs = 0;
    int64_t consoleInitUs = 0;
    int64_t totalStartupUs = 0;
};

// Syntax highlighting token types
enum class SyntaxTokenType { Plain, Keyword, String, Comment, Number, Function, TypeName, Operator };

// Theme colors
struct D2DTheme {
    const wchar_t* name;
    const wchar_t* fontFamily;       // Main font
    const wchar_t* codeFontFamily;   // Monospace font
    bool isDark;
    D2D1_COLOR_F background;
    D2D1_COLOR_F text;
    D2D1_COLOR_F heading;
    D2D1_COLOR_F link;
    D2D1_COLOR_F code;
    D2D1_COLOR_F codeBackground;
    D2D1_COLOR_F blockquoteBorder;
    D2D1_COLOR_F accent;             // For UI elements
    // Syntax highlighting colors
    D2D1_COLOR_F syntaxKeyword;
    D2D1_COLOR_F syntaxString;
    D2D1_COLOR_F syntaxComment;
    D2D1_COLOR_F syntaxNumber;
    D2D1_COLOR_F syntaxFunction;
    D2D1_COLOR_F syntaxType;
};

// Helper to create color from hex
inline D2D1_COLOR_F hexColor(uint32_t hex, float alpha = 1.0f) {
    return D2D1::ColorF(
        ((hex >> 16) & 0xFF) / 255.0f,
        ((hex >> 8) & 0xFF) / 255.0f,
        (hex & 0xFF) / 255.0f,
        alpha
    );
}

// 10 Distinctive Themes - 5 Light, 5 Dark
static const D2DTheme THEMES[] = {
    // ═══════════════════════════════════════════════════════════
    // LIGHT THEMES
    // ═══════════════════════════════════════════════════════════

    // 1. Paper - Warm sepia, literary manuscript feel
    {
        L"Paper", L"Segoe UI", L"Consolas", false,
        hexColor(0xF5F1E8),    // background - warm cream
        hexColor(0x3D3329),    // text - deep brown
        hexColor(0x2A1F16),    // heading - dark brown
        hexColor(0xB85A3C),    // link - terracotta
        hexColor(0x6B5344),    // code - brown
        hexColor(0xEDE6DA),    // codeBackground - parchment
        hexColor(0xC4B8A8),    // blockquoteBorder
        hexColor(0xB85A3C),    // accent
        // Syntax colors
        hexColor(0x8B4513),    // syntaxKeyword - saddle brown
        hexColor(0x2E8B57),    // syntaxString - sea green
        hexColor(0x808080),    // syntaxComment - gray
        hexColor(0xB8860B),    // syntaxNumber - dark goldenrod
        hexColor(0x4169E1),    // syntaxFunction - royal blue
        hexColor(0x9932CC)     // syntaxType - dark orchid
    },

    // 2. Sakura - Japanese cherry blossom, soft pink elegance
    {
        L"Sakura", L"Segoe UI", L"Consolas", false,
        hexColor(0xFDF8F8),    // background - soft blush white
        hexColor(0x404040),    // text - soft charcoal
        hexColor(0xC44569),    // heading - deep rose
        hexColor(0xE8749A),    // link - coral pink
        hexColor(0x8B5570),    // code - plum
        hexColor(0xF8ECF0),    // codeBackground - blush
        hexColor(0xE8B4C0),    // blockquoteBorder
        hexColor(0xC44569),    // accent
        // Syntax colors
        hexColor(0xC44569),    // syntaxKeyword - rose
        hexColor(0x2E8B57),    // syntaxString - sea green
        hexColor(0x999999),    // syntaxComment - gray
        hexColor(0xE8749A),    // syntaxNumber - coral pink
        hexColor(0x6A5ACD),    // syntaxFunction - slate blue
        hexColor(0x8B5570)     // syntaxType - plum
    },

    // 3. Arctic - Nordic ice blues, crisp and clean
    {
        L"Arctic", L"Segoe UI", L"Cascadia Code", false,
        hexColor(0xF7FAFC),    // background - ice white
        hexColor(0x2D3748),    // text - deep slate
        hexColor(0x1A365D),    // heading - navy
        hexColor(0x3182CE),    // link - bright blue
        hexColor(0x285E61),    // code - teal
        hexColor(0xEBF8FF),    // codeBackground - light blue
        hexColor(0xA0C4E8),    // blockquoteBorder
        hexColor(0x3182CE),    // accent
        // Syntax colors
        hexColor(0x0066CC),    // syntaxKeyword - blue
        hexColor(0x2E8B57),    // syntaxString - sea green
        hexColor(0x708090),    // syntaxComment - slate gray
        hexColor(0xD2691E),    // syntaxNumber - chocolate
        hexColor(0x1A365D),    // syntaxFunction - navy
        hexColor(0x6B5B95)     // syntaxType - purple
    },

    // 4. Meadow - Fresh organic greens, nature-inspired
    {
        L"Meadow", L"Segoe UI", L"Consolas", false,
        hexColor(0xF7FAF7),    // background - soft white-green
        hexColor(0x1A2F1A),    // text - forest
        hexColor(0x1C4532),    // heading - deep green
        hexColor(0x38A169),    // link - fresh green
        hexColor(0x4A5568),    // code - slate
        hexColor(0xE6FFED),    // codeBackground - mint
        hexColor(0x9AE6B4),    // blockquoteBorder
        hexColor(0x38A169),    // accent
        // Syntax colors
        hexColor(0x1C4532),    // syntaxKeyword - deep green
        hexColor(0x8B4513),    // syntaxString - saddle brown
        hexColor(0x708090),    // syntaxComment - slate gray
        hexColor(0xD2691E),    // syntaxNumber - chocolate
        hexColor(0x38A169),    // syntaxFunction - fresh green
        hexColor(0x6B5B95)     // syntaxType - purple
    },

    // 5. Dusk - Golden hour warmth, sunset tones
    {
        L"Dusk", L"Segoe UI", L"Consolas", false,
        hexColor(0xFFFBF5),    // background - warm white
        hexColor(0x553C10),    // text - deep amber
        hexColor(0x9C4221),    // heading - burnt orange
        hexColor(0xB7791F),    // link - gold
        hexColor(0x5F5030),    // code - olive
        hexColor(0xFEF5E7),    // codeBackground - cream
        hexColor(0xE8C48D),    // blockquoteBorder
        hexColor(0xB7791F),    // accent
        // Syntax colors
        hexColor(0x9C4221),    // syntaxKeyword - burnt orange
        hexColor(0x2E8B57),    // syntaxString - sea green
        hexColor(0x808080),    // syntaxComment - gray
        hexColor(0xB7791F),    // syntaxNumber - gold
        hexColor(0x4169E1),    // syntaxFunction - royal blue
        hexColor(0x8B4513)     // syntaxType - saddle brown
    },

    // ═══════════════════════════════════════════════════════════
    // DARK THEMES
    // ═══════════════════════════════════════════════════════════

    // 6. Midnight - Deep space, cosmic tranquility
    {
        L"Midnight", L"Segoe UI", L"Cascadia Code", true,
        hexColor(0x0D1B2A),    // background - deep navy
        hexColor(0xE0E1DD),    // text - soft blue-white
        hexColor(0xF0F4F8),    // heading - moonlight
        hexColor(0x00B4D8),    // link - electric blue
        hexColor(0x90E0EF),    // code - cyan
        hexColor(0x1B263B),    // codeBackground - deep blue
        hexColor(0x415A77),    // blockquoteBorder
        hexColor(0x00B4D8),    // accent
        // Syntax colors
        hexColor(0x00B4D8),    // syntaxKeyword - electric blue
        hexColor(0x98FB98),    // syntaxString - pale green
        hexColor(0x6C7A89),    // syntaxComment - gray-blue
        hexColor(0xFFD700),    // syntaxNumber - gold
        hexColor(0x90E0EF),    // syntaxFunction - cyan
        hexColor(0xDDA0DD)     // syntaxType - plum
    },

    // 7. Dracula - Classic dark, purples and pinks
    {
        L"Dracula", L"Segoe UI", L"Consolas", true,
        hexColor(0x282A36),    // background - deep purple-gray
        hexColor(0xF8F8F2),    // text - light gray
        hexColor(0xFF79C6),    // heading - pink
        hexColor(0x8BE9FD),    // link - cyan
        hexColor(0x50FA7B),    // code - green
        hexColor(0x21222C),    // codeBackground - darker
        hexColor(0x6272A4),    // blockquoteBorder
        hexColor(0xBD93F9),    // accent - purple
        // Syntax colors (Dracula palette)
        hexColor(0xFF79C6),    // syntaxKeyword - pink
        hexColor(0xF1FA8C),    // syntaxString - yellow
        hexColor(0x6272A4),    // syntaxComment - gray
        hexColor(0xBD93F9),    // syntaxNumber - purple
        hexColor(0x50FA7B),    // syntaxFunction - green
        hexColor(0x8BE9FD)     // syntaxType - cyan
    },

    // 8. Forest - Deep mystical greens
    {
        L"Forest", L"Segoe UI", L"Consolas", true,
        hexColor(0x0D1512),    // background - deep green-black
        hexColor(0xB8C5B2),    // text - sage
        hexColor(0x9AE6B4),    // heading - bright green
        hexColor(0x68D391),    // link - lime
        hexColor(0x81E6D9),    // code - mint
        hexColor(0x1A2A23),    // codeBackground - dark green
        hexColor(0x4A6E5A),    // blockquoteBorder
        hexColor(0x68D391),    // accent
        // Syntax colors
        hexColor(0x9AE6B4),    // syntaxKeyword - bright green
        hexColor(0xF0E68C),    // syntaxString - khaki
        hexColor(0x5F7A6A),    // syntaxComment - muted green
        hexColor(0xFFB86C),    // syntaxNumber - orange
        hexColor(0x68D391),    // syntaxFunction - lime
        hexColor(0x81E6D9)     // syntaxType - mint
    },

    // 9. Ember - Warm charcoal with fire accents
    {
        L"Ember", L"Segoe UI", L"Consolas", true,
        hexColor(0x1A1614),    // background - warm black
        hexColor(0xD4C5B9),    // text - warm gray
        hexColor(0xF6AD55),    // heading - amber
        hexColor(0xED8936),    // link - orange
        hexColor(0xFC8181),    // code - coral
        hexColor(0x252019),    // codeBackground - dark warm
        hexColor(0x5C4A3A),    // blockquoteBorder
        hexColor(0xED8936),    // accent
        // Syntax colors
        hexColor(0xF6AD55),    // syntaxKeyword - amber
        hexColor(0x98FB98),    // syntaxString - pale green
        hexColor(0x6B5B4F),    // syntaxComment - warm gray
        hexColor(0xFC8181),    // syntaxNumber - coral
        hexColor(0xED8936),    // syntaxFunction - orange
        hexColor(0xDDA0DD)     // syntaxType - plum
    },

    // 10. Abyss - True black, neon accents (OLED-friendly)
    {
        L"Abyss", L"Segoe UI Light", L"Cascadia Mono", true,
        hexColor(0x000000),    // background - pure black
        hexColor(0xFFFFFF),    // text - pure white
        hexColor(0x00FFE1),    // heading - cyan
        hexColor(0xFF00FF),    // link - magenta
        hexColor(0xAAFF00),    // code - lime
        hexColor(0x0A0A0A),    // codeBackground - near black
        hexColor(0x333333),    // blockquoteBorder
        hexColor(0x00FFE1),    // accent
        // Syntax colors (neon)
        hexColor(0xFF00FF),    // syntaxKeyword - magenta
        hexColor(0xAAFF00),    // syntaxString - lime
        hexColor(0x666666),    // syntaxComment - dark gray
        hexColor(0xFF6600),    // syntaxNumber - orange
        hexColor(0x00FFE1),    // syntaxFunction - cyan
        hexColor(0xFFFF00)     // syntaxType - yellow
    }
};

static const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

// ═══════════════════════════════════════════════════════════════════════════
// PERSISTENT SETTINGS
// ═══════════════════════════════════════════════════════════════════════════

struct Settings {
    int themeIndex = 5;          // Default to Midnight
    float zoomFactor = 1.0f;
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;
    int windowWidth = 1024;
    int windowHeight = 768;
    bool windowMaximized = false;
    bool hasAskedFileAssociation = false;  // Have we asked about .md association?
};

static std::wstring getSettingsPath() {
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

static void saveSettings(const Settings& settings) {
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
}

static Settings loadSettings() {
    Settings settings;
    std::wstring path = getSettingsPath();
    if (path.empty()) return settings;

    std::ifstream file(path);
    if (!file) return settings;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '[') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        if (key == "themeIndex") {
            int idx = std::stoi(value);
            if (idx >= 0 && idx < THEME_COUNT) settings.themeIndex = idx;
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
        } else if (key == "windowMaximized") {
            settings.windowMaximized = (value == "1");
        } else if (key == "hasAskedFileAssociation") {
            settings.hasAskedFileAssociation = (value == "1");
        }
    }
    return settings;
}

// ═══════════════════════════════════════════════════════════════════════════
// FILE ASSOCIATION
// ═══════════════════════════════════════════════════════════════════════════

static bool registerFileAssociation() {
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
    const wchar_t* desc = L"Markdown Document";
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
    const wchar_t* appDesc = L"A fast, lightweight markdown reader";
    RegSetValueExW(hKey, L"ApplicationName", 0, REG_SZ, (BYTE*)appName, (DWORD)((wcslen(appName) + 1) * sizeof(wchar_t)));
    RegSetValueExW(hKey, L"ApplicationDescription", 0, REG_SZ, (BYTE*)appDesc, (DWORD)((wcslen(appDesc) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Register file associations in capabilities
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Tinta\\Capabilities\\FileAssociations", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueExW(hKey, L".md", 0, REG_SZ, (BYTE*)progId, (DWORD)((wcslen(progId) + 1) * sizeof(wchar_t)));
    RegSetValueExW(hKey, L".markdown", 0, REG_SZ, (BYTE*)progId, (DWORD)((wcslen(progId) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Add to RegisteredApplications
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    const wchar_t* capPath = L"Software\\Tinta\\Capabilities";
    RegSetValueExW(hKey, L"Tinta", 0, REG_SZ, (BYTE*)capPath, (DWORD)((wcslen(capPath) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Add OpenWithProgids for .md
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.md\\OpenWithProgids", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueExW(hKey, progId, 0, REG_NONE, nullptr, 0);
    RegCloseKey(hKey);

    // Add OpenWithProgids for .markdown
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.markdown\\OpenWithProgids", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    RegSetValueExW(hKey, progId, 0, REG_NONE, nullptr, 0);
    RegCloseKey(hKey);

    // Notify shell of the change
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return true;
}

static void openDefaultAppsSettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
}

static void askAndRegisterFileAssociation(Settings& settings) {
    if (settings.hasAskedFileAssociation) return;

    int result = MessageBoxW(
        nullptr,
        L"Would you like to set Tinta as the default viewer for .md files?\n\n"
        L"Windows will open Settings where you can select Tinta.",
        L"Tinta - File Association",
        MB_YESNO | MB_ICONQUESTION
    );

    if (result == IDYES) {
        if (registerFileAssociation()) {
            MessageBoxW(nullptr,
                       L"Tinta has been registered.\n\n"
                       L"In the Settings window that opens:\n"
                       L"1. Search for '.md'\n"
                       L"2. Click on the current default app\n"
                       L"3. Select 'Tinta' from the list",
                       L"Almost done!", MB_OK | MB_ICONINFORMATION);
            openDefaultAppsSettings();
        } else {
            MessageBoxW(nullptr, L"Failed to register file association. Try running as administrator.",
                       L"Error", MB_OK | MB_ICONWARNING);
        }
    }

    settings.hasAskedFileAssociation = true;
    saveSettings(settings);
}

// Application state
struct App {
    // Win32
    HWND hwnd = nullptr;
    int width = 1024;
    int height = 768;
    bool running = true;

    // Direct2D
    ID2D1Factory* d2dFactory = nullptr;
    ID2D1HwndRenderTarget* renderTarget = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;

    // DirectWrite
    IDWriteFactory* dwriteFactory = nullptr;
    IDWriteTextFormat* textFormat = nullptr;
    IDWriteTextFormat* headingFormat = nullptr;
    IDWriteTextFormat* codeFormat = nullptr;
    IDWriteTextFormat* boldFormat = nullptr;
    IDWriteTextFormat* italicFormat = nullptr;

    // OpenType typography
    IDWriteTypography* bodyTypography = nullptr;
    IDWriteTypography* codeTypography = nullptr;

    // Markdown
    MarkdownParser parser;
    ElementPtr root;
    std::string currentFile;
    size_t parseTimeUs = 0;
    float contentHeight = 0;
    float contentWidth = 0;

    // State
    float scrollY = 0;
    float scrollX = 0;
    float targetScrollY = 0;
    float targetScrollX = 0;
    float contentScale = 1.0f;  // DPI scale
    float zoomFactor = 1.0f;    // User zoom (Ctrl+scroll)
    bool darkMode = true;
    bool showStats = false;
    int currentThemeIndex = 5;  // Default to "Midnight" (first dark theme)
    D2DTheme theme = THEMES[5];

    // Theme chooser overlay
    bool showThemeChooser = false;
    int hoveredThemeIndex = -1;
    float themeChooserAnimation = 0.0f;  // 0 to 1 for fade in

    // Mouse
    bool mouseDown = false;
    int mouseX = 0;
    int mouseY = 0;

    // Vertical scrollbar
    bool scrollbarHovered = false;
    bool scrollbarDragging = false;
    float scrollbarDragStartY = 0;
    float scrollbarDragStartScroll = 0;

    // Horizontal scrollbar
    bool hScrollbarHovered = false;
    bool hScrollbarDragging = false;
    float hScrollbarDragStartX = 0;
    float hScrollbarDragStartScroll = 0;

    // Links - tracked during render for click detection
    struct LinkRect {
        D2D1_RECT_F bounds;
        std::string url;
    };
    std::vector<LinkRect> linkRects;
    std::string hoveredLink;

    // Text bounds - tracked for cursor changes and selection
    struct TextRect {
        D2D1_RECT_F rect;
        std::wstring text;
        size_t docStart = 0;  // Start position in docText
    };
    std::vector<TextRect> textRects;

    // Search match info
    struct SearchMatch {
        size_t textRectIndex;       // Index into textRects
        size_t startPos;            // Character offset in text
        size_t length;              // Match length
        D2D1_RECT_F highlightRect;  // Computed highlight bounds
    };
    std::vector<SearchMatch> searchMatches;
    bool overText = false;

    // Text selection
    bool selecting = false;
    int selStartX = 0, selStartY = 0;
    int selEndX = 0, selEndY = 0;
    bool hasSelection = false;
    std::wstring selectedText;

    // Multi-click selection (double/triple click)
    std::chrono::steady_clock::time_point lastClickTime;
    int clickCount = 0;
    int lastClickX = 0, lastClickY = 0;
    enum class SelectionMode { Normal, Word, Line } selectionMode = SelectionMode::Normal;
    // Anchor bounds for word/line selection (the original word/line that was clicked)
    float anchorLeft = 0, anchorRight = 0, anchorTop = 0, anchorBottom = 0;

    // Document text built during render (used for search/mapping)
    std::wstring docText;

    // Search match layout mapping (document Y for each match)
    std::vector<float> searchMatchYs;
    size_t searchMatchCursor = 0;

    // Copied notification (fades out over 2 seconds)
    bool showCopiedNotification = false;
    float copiedNotificationAlpha = 0.0f;
    std::chrono::steady_clock::time_point copiedNotificationStart;

    // Search overlay
    bool showSearch = false;
    float searchAnimation = 0.0f;
    std::wstring searchQuery;
    int searchCurrentIndex = 0;
    bool searchActive = false;
    bool searchJustOpened = false;  // Skip WM_CHAR after opening with F key

    // Metrics
    StartupMetrics metrics;
    size_t drawCalls = 0;

    ~App() { shutdown(); }

    void shutdown() {
        if (brush) { brush->Release(); brush = nullptr; }
        if (renderTarget) { renderTarget->Release(); renderTarget = nullptr; }
        if (textFormat) { textFormat->Release(); textFormat = nullptr; }
        if (headingFormat) { headingFormat->Release(); headingFormat = nullptr; }
        if (codeFormat) { codeFormat->Release(); codeFormat = nullptr; }
        if (boldFormat) { boldFormat->Release(); boldFormat = nullptr; }
        if (italicFormat) { italicFormat->Release(); italicFormat = nullptr; }
        if (bodyTypography) { bodyTypography->Release(); bodyTypography = nullptr; }
        if (codeTypography) { codeTypography->Release(); codeTypography = nullptr; }
        if (dwriteFactory) { dwriteFactory->Release(); dwriteFactory = nullptr; }
        if (d2dFactory) { d2dFactory->Release(); d2dFactory = nullptr; }
    }
};

static App* g_app = nullptr;

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void render(App& app);
void openUrl(const std::string& url);
void updateTextFormats(App& app);
void createTypography(App& app);
void applyTheme(App& app, int themeIndex);
void extractText(const ElementPtr& elem, std::wstring& out);
void recordSearchMatchPositions(App& app, size_t segStart, size_t segEnd, float lineY);

// Helper: Check if character is a word boundary
inline bool isWordBoundary(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' ||
           c == L'.' || c == L',' || c == L';' || c == L':' ||
           c == L'!' || c == L'?' || c == L'"' || c == L'\'' ||
           c == L'(' || c == L')' || c == L'[' || c == L']' ||
           c == L'{' || c == L'}' || c == L'<' || c == L'>' ||
           c == L'/' || c == L'\\' || c == L'-' || c == L'=' ||
           c == L'+' || c == L'*' || c == L'&' || c == L'|';
}

// Find the TextRect at a given screen position
const App::TextRect* findTextRectAt(const App& app, int x, int y) {
    for (const auto& tr : app.textRects) {
        if (x >= tr.rect.left && x <= tr.rect.right &&
            y >= tr.rect.top && y <= tr.rect.bottom) {
            return &tr;
        }
    }
    return nullptr;
}

// Find word boundaries in a TextRect at screen position x
// Returns the word's left and right X positions
bool findWordBoundsAt(const App& app, const App::TextRect& tr, int x,
                      float& wordLeft, float& wordRight) {
    if (tr.text.empty()) return false;

    float totalWidth = tr.rect.right - tr.rect.left;
    float charWidth = totalWidth / (float)tr.text.length();

    // Find which character was clicked
    int charIndex = (int)((x - tr.rect.left) / charWidth);
    charIndex = std::max(0, std::min(charIndex, (int)tr.text.length() - 1));

    // Find word start (scan left)
    int wordStart = charIndex;
    while (wordStart > 0 && !isWordBoundary(tr.text[wordStart - 1])) {
        wordStart--;
    }

    // Find word end (scan right)
    int wordEnd = charIndex;
    while (wordEnd < (int)tr.text.length() - 1 && !isWordBoundary(tr.text[wordEnd + 1])) {
        wordEnd++;
    }

    wordLeft = tr.rect.left + wordStart * charWidth;
    wordRight = tr.rect.left + (wordEnd + 1) * charWidth;
    return true;
}

// Find all TextRects on the same line (similar Y position)
void findLineRects(const App& app, float y, float& lineLeft, float& lineRight,
                   float& lineTop, float& lineBottom) {
    lineLeft = 99999.0f;
    lineRight = 0.0f;
    lineTop = 0.0f;
    lineBottom = 0.0f;
    bool found = false;

    for (const auto& tr : app.textRects) {
        float centerY = (tr.rect.top + tr.rect.bottom) / 2.0f;
        if (std::abs(centerY - y) < 20) {  // Same line if within 20px
            if (!found) {
                lineTop = tr.rect.top;
                lineBottom = tr.rect.bottom;
                found = true;
            }
            lineLeft = std::min(lineLeft, tr.rect.left);
            lineRight = std::max(lineRight, tr.rect.right);
            lineTop = std::min(lineTop, tr.rect.top);
            lineBottom = std::max(lineBottom, tr.rect.bottom);
        }
    }
}

bool initD2D(App& app) {
    auto t0 = Clock::now();

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &app.d2dFactory);
    if (FAILED(hr)) return false;

    app.metrics.d2dInitUs = usElapsed(t0);
    t0 = Clock::now();

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&app.dwriteFactory));
    if (FAILED(hr)) return false;

    app.metrics.dwriteInitUs = usElapsed(t0);

    return true;
}

void applyTheme(App& app, int themeIndex) {
    if (themeIndex < 0 || themeIndex >= THEME_COUNT) return;

    app.currentThemeIndex = themeIndex;
    app.theme = THEMES[themeIndex];
    app.darkMode = app.theme.isDark;

    // Update fonts for the new theme
    updateTextFormats(app);

    // Force a redraw
    if (app.hwnd) {
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

void updateTextFormats(App& app) {
    // Release existing formats
    if (app.textFormat) { app.textFormat->Release(); app.textFormat = nullptr; }
    if (app.headingFormat) { app.headingFormat->Release(); app.headingFormat = nullptr; }
    if (app.codeFormat) { app.codeFormat->Release(); app.codeFormat = nullptr; }
    if (app.boldFormat) { app.boldFormat->Release(); app.boldFormat = nullptr; }
    if (app.italicFormat) { app.italicFormat->Release(); app.italicFormat = nullptr; }

    // Create text formats with current zoom and theme fonts
    float scale = app.contentScale * app.zoomFactor;
    float fontSize = 16.0f * scale;
    float headingSize = 28.0f * scale;
    float codeSize = 14.0f * scale;

    const wchar_t* fontFamily = app.theme.fontFamily;
    const wchar_t* codeFont = app.theme.codeFontFamily;

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.textFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        headingSize, L"en-us", &app.headingFormat);

    app.dwriteFactory->CreateTextFormat(codeFont, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        codeSize, L"en-us", &app.codeFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.boldFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.italicFormat);

    // Set consistent baseline alignment for all formats
    if (app.textFormat) app.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.headingFormat) app.headingFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.codeFormat) app.codeFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.boldFormat) app.boldFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.italicFormat) app.italicFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

void createTypography(App& app) {
    // Release existing typography objects
    if (app.bodyTypography) { app.bodyTypography->Release(); app.bodyTypography = nullptr; }
    if (app.codeTypography) { app.codeTypography->Release(); app.codeTypography = nullptr; }

    // Body typography - standard ligatures, kerning, contextual alternates
    app.dwriteFactory->CreateTypography(&app.bodyTypography);
    if (app.bodyTypography) {
        app.bodyTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES, 1});
        app.bodyTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_KERNING, 1});
        app.bodyTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES, 1});
    }

    // Code typography - programming ligatures (for fonts like Cascadia Code, Fira Code)
    app.dwriteFactory->CreateTypography(&app.codeTypography);
    if (app.codeTypography) {
        app.codeTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES, 1});
        app.codeTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_DISCRETIONARY_LIGATURES, 1});
    }
}

bool createRenderTarget(App& app) {
    if (app.renderTarget) {
        app.renderTarget->Release();
        app.renderTarget = nullptr;
    }
    if (app.brush) {
        app.brush->Release();
        app.brush = nullptr;
    }

    RECT rc;
    GetClientRect(app.hwnd, &rc);

    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rtProps.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    rtProps.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    HRESULT hr = app.d2dFactory->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(app.hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &app.renderTarget
    );
    if (FAILED(hr)) return false;

    hr = app.renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &app.brush);
    if (FAILED(hr)) return false;

    // Enable high-quality text
    app.renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    // Create custom rendering params for improved text quality
    IDWriteRenderingParams* defaultParams = nullptr;
    IDWriteRenderingParams* customParams = nullptr;

    app.dwriteFactory->CreateRenderingParams(&defaultParams);
    if (defaultParams) {
        app.dwriteFactory->CreateCustomRenderingParams(
            defaultParams->GetGamma(),
            defaultParams->GetEnhancedContrast(),
            1.0f,  // ClearType level
            defaultParams->GetPixelGeometry(),
            DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
            &customParams
        );
        defaultParams->Release();

        if (customParams) {
            app.renderTarget->SetTextRenderingParams(customParams);
            customParams->Release();
        }
    }

    return true;
}

// Simple inline element rendering
struct InlineSpan {
    std::wstring text;
    D2D1_COLOR_F color;
    IDWriteTextFormat* format;
    std::string linkUrl;
    bool underline;
};

float measureText(App& app, const std::wstring& text, IDWriteTextFormat* format) {
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(),
        format, 10000.0f, 100.0f, &layout);
    if (!layout) return 0;

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    layout->Release();
    return metrics.widthIncludingTrailingWhitespace;
}

std::wstring toWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &result[0], len);
    return result;
}

// Render element recursively
void renderElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);

void renderInlineContent(App& app, const std::vector<ElementPtr>& elements,
                         float startX, float& y, float maxWidth,
                         IDWriteTextFormat* baseFormat, D2D1_COLOR_F baseColor,
                         const std::string& baseLinkUrl = "", float customLineHeight = 0) {
    float x = startX;
    float scale = app.contentScale * app.zoomFactor;
    // Use custom line height if provided, otherwise calculate from font size
    // Use 1.7x multiplier to accommodate scripts with stacking diacritics (Vietnamese, etc.)
    float lineHeight = customLineHeight > 0 ? customLineHeight : baseFormat->GetFontSize() * 1.7f;
    float maxX = startX + maxWidth;
    float spaceWidth = measureText(app, L" ", baseFormat);

    for (const auto& elem : elements) {
        IDWriteTextFormat* format = baseFormat;
        D2D1_COLOR_F color = baseColor;
        std::string linkUrl = baseLinkUrl;
        bool isLink = !baseLinkUrl.empty();

        std::wstring text;

        switch (elem->type) {
            case ElementType::Text:
                text = toWide(elem->text);
                break;

            case ElementType::Strong:
                format = app.boldFormat;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text += toWide(child->text);
                    }
                }
                break;

            case ElementType::Emphasis:
                format = app.italicFormat;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text += toWide(child->text);
                    }
                }
                break;

            case ElementType::Code: {
                format = app.codeFormat;
                color = app.theme.code;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text = toWide(child->text);
                    }
                }

                size_t codeDocStart = app.docText.size();

                // Draw background
                float textWidth = measureText(app, text, format);
                if (x + textWidth > maxX && x > startX) {
                    x = startX;
                    y += lineHeight;
                }

                float renderY = y - app.scrollY;
                app.brush->SetColor(app.theme.codeBackground);
                app.renderTarget->FillRectangle(
                    D2D1::RectF(x - 2, renderY, x + textWidth + 4, renderY + lineHeight),
                    app.brush);
                app.drawCalls++;

                // Draw text vertically centered in the box
                if (renderY > -lineHeight && renderY < app.height + lineHeight) {
                    float codeFontHeight = format->GetFontSize() * 1.2f;  // Approximate line height for code font
                    float verticalOffset = (lineHeight - codeFontHeight) / 2.0f;

                    app.brush->SetColor(color);
                    IDWriteTextLayout* layout = nullptr;
                    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(),
                        format, textWidth + 50, lineHeight, &layout);
                    if (layout) {
                        if (app.codeTypography) {
                            layout->SetTypography(app.codeTypography, {0, (UINT32)text.length()});
                        }
                        app.renderTarget->DrawTextLayout(D2D1::Point2F(x, renderY + verticalOffset), layout, app.brush);
                        layout->Release();
                    } else {
                        app.renderTarget->DrawText(text.c_str(), (UINT32)text.length(), format,
                            D2D1::RectF(x, renderY + verticalOffset, x + textWidth + 50, renderY + lineHeight),
                            app.brush);
                    }
                    app.drawCalls++;

                    // Track text bounds
                    app.textRects.push_back({D2D1::RectF(x, renderY, x + textWidth, renderY + lineHeight), text, codeDocStart});
                }

                recordSearchMatchPositions(app, codeDocStart, codeDocStart + text.length(), y);
                app.docText += text;
                x += textWidth + spaceWidth;
                continue;  // Skip common text drawing code
            }

            case ElementType::Link:
                color = app.theme.link;
                linkUrl = elem->url;
                isLink = true;
                for (const auto& child : elem->children) {
                    if (child->type == ElementType::Text) {
                        text += toWide(child->text);
                    }
                }
                break;

            case ElementType::SoftBreak:
                text = L" ";
                break;

            case ElementType::HardBreak:
                app.docText += L"\n";
                x = startX;
                y += lineHeight;
                continue;

            default:
                renderInlineContent(app, elem->children, x, y, maxWidth - (x - startX), format, color, linkUrl);
                continue;
        }

        if (text.empty()) continue;

        size_t textDocStart = app.docText.size();

        // For links, track start position for continuous underline
        float linkLineStartX = x;
        float linkLineY = y;

        // Word wrap
        size_t pos = 0;
        while (pos < text.length()) {
            size_t spacePos = text.find(L' ', pos);
            if (spacePos == std::wstring::npos) spacePos = text.length();

            size_t wordStart = pos;
            std::wstring word = text.substr(wordStart, spacePos - wordStart);
            if (word.empty()) {
                if (spacePos < text.length()) {
                    x += spaceWidth;
                    pos = spacePos + 1;
                } else {
                    pos = spacePos;
                }
                continue;
            }

            size_t wordDocStart = textDocStart + wordStart;
            float wordWidth = measureText(app, word, format);

            // Check if need to wrap
            if (x + wordWidth > maxX && x > startX) {
                // Draw underline for link segment before wrapping
                if (isLink && x > linkLineStartX) {
                    float renderY = linkLineY - app.scrollY;
                    if (renderY > -lineHeight && renderY < app.height + lineHeight) {
                        float underlineY = renderY + lineHeight - 2;
                        app.brush->SetColor(color);
                        app.renderTarget->DrawLine(
                            D2D1::Point2F(linkLineStartX, underlineY),
                            D2D1::Point2F(x, underlineY),
                            app.brush, 1.0f);
                        app.drawCalls++;

                        // Track link bounds for this line segment
                        App::LinkRect lr;
                        lr.bounds = D2D1::RectF(linkLineStartX, renderY, x, renderY + lineHeight);
                        lr.url = linkUrl;
                        app.linkRects.push_back(lr);
                    }
                }

                x = startX;
                y += lineHeight;
                linkLineStartX = x;
                linkLineY = y;
            }

            recordSearchMatchPositions(app, wordDocStart, wordDocStart + word.length(), y);

            // Draw word with typography
            float renderY = y - app.scrollY;
            if (renderY > -lineHeight && renderY < app.height + lineHeight) {
                app.brush->SetColor(color);

                // Use TextLayout for typography support
                IDWriteTextLayout* layout = nullptr;
                app.dwriteFactory->CreateTextLayout(word.c_str(), (UINT32)word.length(),
                    format, wordWidth + 100, lineHeight, &layout);
                if (layout) {
                    // Apply typography (body typography for regular text)
                    if (app.bodyTypography) {
                        layout->SetTypography(app.bodyTypography, {0, (UINT32)word.length()});
                    }
                    app.renderTarget->DrawTextLayout(D2D1::Point2F(x, renderY), layout, app.brush);
                    layout->Release();
                } else {
                    // Fallback to DrawText if layout creation fails
                    app.renderTarget->DrawText(word.c_str(), (UINT32)word.length(), format,
                        D2D1::RectF(x, renderY, x + wordWidth + 100, renderY + lineHeight), app.brush);
                }
                app.drawCalls++;

                // Track text bounds for cursor and selection (add small buffer for selection highlight)
                app.textRects.push_back({D2D1::RectF(x, renderY, x + wordWidth + 2, renderY + lineHeight), word, wordDocStart});
            }

            x += wordWidth;

            // Add space (include in link extent)
            if (spacePos < text.length()) {
                x += spaceWidth;
                pos = spacePos + 1;
            } else {
                pos = spacePos;
            }
        }

        app.docText += text;

        // Draw final underline segment for link
        if (isLink && x > linkLineStartX) {
            float renderY = linkLineY - app.scrollY;
            if (renderY > -lineHeight && renderY < app.height + lineHeight) {
                float underlineY = renderY + lineHeight - 2;
                // Trim trailing space from underline
                float underlineEndX = x;
                if (text.length() > 0 && text.back() != L' ') {
                    underlineEndX = x;
                } else {
                    underlineEndX = x - spaceWidth;
                }
                app.brush->SetColor(color);
                app.renderTarget->DrawLine(
                    D2D1::Point2F(linkLineStartX, underlineY),
                    D2D1::Point2F(underlineEndX, underlineY),
                    app.brush, 1.0f);
                app.drawCalls++;

                // Track link bounds
                App::LinkRect lr;
                lr.bounds = D2D1::RectF(linkLineStartX, renderY, underlineEndX, renderY + lineHeight);
                lr.url = linkUrl;
                app.linkRects.push_back(lr);
            }
        }
    }

    y += lineHeight;
}

void renderParagraph(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    renderInlineContent(app, elem->children, indent, y, maxWidth, app.textFormat, app.theme.text);
    app.docText += L"\n\n";
    float scale = app.contentScale * app.zoomFactor;
    y += 14 * scale;  // Increased paragraph spacing
}

void renderHeading(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float sizes[] = {32, 26, 22, 18, 16, 14};
    float size = sizes[std::min(elem->level - 1, 5)] * scale;

    IDWriteTextFormat* format = nullptr;
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"en-us", &format);

    // Add top margin for headings (more for H1)
    if (elem->level == 1) {
        y += 16 * scale;
    } else {
        y += 20 * scale;
    }

    renderInlineContent(app, elem->children, indent, y, maxWidth, format, app.theme.heading);

    // Underline for H1/H2 - add gap before the line
    if (elem->level <= 2) {
        y += 6 * scale;  // Gap between text and underline
        float renderY = y - app.scrollY;
        app.brush->SetColor(D2D1::ColorF(app.theme.heading.r, app.theme.heading.g, app.theme.heading.b, 0.3f));
        float lineWidth = (elem->level == 1) ? 2.0f * scale : 1.0f * scale;
        app.renderTarget->DrawLine(
            D2D1::Point2F(indent, renderY),
            D2D1::Point2F(indent + maxWidth, renderY),
            app.brush, lineWidth);
        y += lineWidth;
        app.drawCalls++;
    }

    app.docText += L"\n\n";
    format->Release();
    y += 12 * scale;  // Bottom margin after heading
}

// ═══════════════════════════════════════════════════════════════════════════
// SYNTAX HIGHLIGHTING
// ═══════════════════════════════════════════════════════════════════════════

// Language keyword sets
static const std::unordered_set<std::wstring> CPP_KEYWORDS = {
    L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"break", L"continue",
    L"return", L"goto", L"default", L"void", L"int", L"char", L"float", L"double", L"bool",
    L"long", L"short", L"unsigned", L"signed", L"const", L"static", L"extern", L"volatile",
    L"class", L"struct", L"union", L"enum", L"typedef", L"template", L"typename", L"namespace",
    L"public", L"private", L"protected", L"virtual", L"override", L"final", L"explicit",
    L"inline", L"constexpr", L"consteval", L"constinit", L"auto", L"register", L"mutable",
    L"new", L"delete", L"this", L"nullptr", L"true", L"false", L"throw", L"try", L"catch",
    L"using", L"operator", L"sizeof", L"alignof", L"decltype", L"noexcept", L"static_assert",
    L"friend", L"concept", L"requires", L"co_await", L"co_return", L"co_yield",
    L"#include", L"#define", L"#ifdef", L"#ifndef", L"#endif", L"#if", L"#else", L"#pragma"
};

static const std::unordered_set<std::wstring> CPP_TYPES = {
    L"size_t", L"int8_t", L"int16_t", L"int32_t", L"int64_t",
    L"uint8_t", L"uint16_t", L"uint32_t", L"uint64_t",
    L"string", L"wstring", L"vector", L"map", L"set", L"unordered_map", L"unordered_set",
    L"shared_ptr", L"unique_ptr", L"weak_ptr", L"optional", L"variant", L"any",
    L"HRESULT", L"HWND", L"HINSTANCE", L"LPARAM", L"WPARAM", L"LRESULT", L"BOOL",
    L"DWORD", L"WORD", L"BYTE", L"UINT", L"INT", L"LONG", L"ULONG", L"FLOAT",
    L"IDWriteFactory", L"ID2D1Factory", L"ID2D1RenderTarget", L"IDWriteTextFormat"
};

static const std::unordered_set<std::wstring> PYTHON_KEYWORDS = {
    L"if", L"elif", L"else", L"for", L"while", L"break", L"continue", L"pass", L"return",
    L"def", L"class", L"import", L"from", L"as", L"try", L"except", L"finally", L"raise",
    L"with", L"yield", L"lambda", L"global", L"nonlocal", L"assert", L"del", L"in", L"is",
    L"not", L"and", L"or", L"True", L"False", L"None", L"async", L"await", L"match", L"case"
};

static const std::unordered_set<std::wstring> JS_KEYWORDS = {
    L"if", L"else", L"for", L"while", L"do", L"switch", L"case", L"break", L"continue",
    L"return", L"function", L"var", L"let", L"const", L"class", L"extends", L"new", L"this",
    L"super", L"try", L"catch", L"finally", L"throw", L"async", L"await", L"yield",
    L"import", L"export", L"default", L"from", L"as", L"of", L"in", L"typeof", L"instanceof",
    L"true", L"false", L"null", L"undefined", L"NaN", L"Infinity", L"void", L"delete",
    L"debugger", L"with", L"static", L"get", L"set", L"=>"
};

static const std::unordered_set<std::wstring> RUST_KEYWORDS = {
    L"if", L"else", L"match", L"for", L"while", L"loop", L"break", L"continue", L"return",
    L"fn", L"let", L"mut", L"const", L"static", L"struct", L"enum", L"trait", L"impl",
    L"pub", L"mod", L"use", L"crate", L"super", L"self", L"Self", L"where", L"as", L"in",
    L"type", L"unsafe", L"async", L"await", L"move", L"ref", L"dyn", L"box", L"extern",
    L"true", L"false", L"Some", L"None", L"Ok", L"Err"
};

static const std::unordered_set<std::wstring> GO_KEYWORDS = {
    L"if", L"else", L"for", L"range", L"switch", L"case", L"break", L"continue", L"return",
    L"func", L"var", L"const", L"type", L"struct", L"interface", L"map", L"chan",
    L"package", L"import", L"go", L"defer", L"select", L"default", L"fallthrough", L"goto",
    L"true", L"false", L"nil", L"iota", L"make", L"new", L"append", L"len", L"cap", L"copy"
};

// Token for syntax highlighting
struct SyntaxToken {
    std::wstring text;
    SyntaxTokenType tokenType;
};

// Detect language from code fence language hint
static int detectLanguage(const std::wstring& lang) {
    std::wstring lower = lang;
    for (auto& c : lower) c = towlower(c);
    if (lower == L"cpp" || lower == L"c++" || lower == L"c" || lower == L"h" || lower == L"hpp" || lower == L"cxx")
        return 1;  // C/C++
    if (lower == L"python" || lower == L"py")
        return 2;  // Python
    if (lower == L"javascript" || lower == L"js" || lower == L"jsx" || lower == L"ts" || lower == L"typescript" || lower == L"tsx")
        return 3;  // JavaScript/TypeScript
    if (lower == L"rust" || lower == L"rs")
        return 4;  // Rust
    if (lower == L"go" || lower == L"golang")
        return 5;  // Go
    return 0;  // Unknown
}

// Get keywords set for language
static const std::unordered_set<std::wstring>* getKeywordsForLanguage(int lang) {
    switch (lang) {
        case 1: return &CPP_KEYWORDS;
        case 2: return &PYTHON_KEYWORDS;
        case 3: return &JS_KEYWORDS;
        case 4: return &RUST_KEYWORDS;
        case 5: return &GO_KEYWORDS;
        default: return nullptr;
    }
}

// Tokenize a line of code for syntax highlighting
static std::vector<SyntaxToken> tokenizeLine(const std::wstring& line, int language, bool& inBlockComment) {
    std::vector<SyntaxToken> tokens;
    const std::unordered_set<std::wstring>* keywords = getKeywordsForLanguage(language);

    size_t i = 0;
    while (i < line.length()) {
        // Handle block comment continuation
        if (inBlockComment) {
            size_t endComment = line.find(L"*/", i);
            if (endComment != std::wstring::npos) {
                tokens.push_back({line.substr(i, endComment + 2 - i), SyntaxTokenType::Comment});
                i = endComment + 2;
                inBlockComment = false;
            } else {
                tokens.push_back({line.substr(i), SyntaxTokenType::Comment});
                return tokens;
            }
            continue;
        }

        wchar_t c = line[i];

        // Skip whitespace
        if (iswspace(c)) {
            size_t start = i;
            while (i < line.length() && iswspace(line[i])) i++;
            tokens.push_back({line.substr(start, i - start), SyntaxTokenType::Plain});
            continue;
        }

        // Line comments
        if (i + 1 < line.length()) {
            if ((line[i] == L'/' && line[i+1] == L'/') ||  // C/C++/JS/Rust/Go
                (language == 2 && line[i] == L'#')) {       // Python
                tokens.push_back({line.substr(i), SyntaxTokenType::Comment});
                return tokens;
            }
            // Block comment start
            if (line[i] == L'/' && line[i+1] == L'*') {
                size_t endComment = line.find(L"*/", i + 2);
                if (endComment != std::wstring::npos) {
                    tokens.push_back({line.substr(i, endComment + 2 - i), SyntaxTokenType::Comment});
                    i = endComment + 2;
                } else {
                    tokens.push_back({line.substr(i), SyntaxTokenType::Comment});
                    inBlockComment = true;
                    return tokens;
                }
                continue;
            }
        }
        // Python single # comment
        if (language == 2 && c == L'#') {
            tokens.push_back({line.substr(i), SyntaxTokenType::Comment});
            return tokens;
        }

        // Strings
        if (c == L'"' || c == L'\'') {
            size_t start = i;
            wchar_t quote = c;
            i++;
            while (i < line.length()) {
                if (line[i] == L'\\' && i + 1 < line.length()) {
                    i += 2;  // Skip escaped char
                } else if (line[i] == quote) {
                    i++;
                    break;
                } else {
                    i++;
                }
            }
            tokens.push_back({line.substr(start, i - start), SyntaxTokenType::String});
            continue;
        }

        // Numbers (including hex)
        if (iswdigit(c) || (c == L'.' && i + 1 < line.length() && iswdigit(line[i+1]))) {
            size_t start = i;
            if (c == L'0' && i + 1 < line.length() && (line[i+1] == L'x' || line[i+1] == L'X')) {
                i += 2;
                while (i < line.length() && iswxdigit(line[i])) i++;
            } else {
                while (i < line.length() && (iswdigit(line[i]) || line[i] == L'.' ||
                       line[i] == L'e' || line[i] == L'E' || line[i] == L'f' || line[i] == L'L')) i++;
            }
            tokens.push_back({line.substr(start, i - start), SyntaxTokenType::Number});
            continue;
        }

        // Identifiers and keywords
        if (iswalpha(c) || c == L'_') {
            size_t start = i;
            while (i < line.length() && (iswalnum(line[i]) || line[i] == L'_')) i++;
            std::wstring word = line.substr(start, i - start);

            // Check if it's a function call (followed by parenthesis)
            size_t next = i;
            while (next < line.length() && iswspace(line[next])) next++;
            bool isFunction = (next < line.length() && line[next] == L'(');

            // Check if keyword
            if (keywords && keywords->count(word)) {
                tokens.push_back({word, SyntaxTokenType::Keyword});
            } else if (language == 1 && CPP_TYPES.count(word)) {
                tokens.push_back({word, SyntaxTokenType::TypeName});
            } else if (isFunction) {
                tokens.push_back({word, SyntaxTokenType::Function});
            } else {
                tokens.push_back({word, SyntaxTokenType::Plain});
            }
            continue;
        }

        // Preprocessor directives (C/C++)
        if (language == 1 && c == L'#') {
            size_t start = i;
            i++;
            while (i < line.length() && (iswalnum(line[i]) || line[i] == L'_')) i++;
            std::wstring directive = line.substr(start, i - start);
            if (keywords && keywords->count(directive)) {
                tokens.push_back({directive, SyntaxTokenType::Keyword});
            } else {
                tokens.push_back({directive, SyntaxTokenType::Plain});
            }
            continue;
        }

        // Operators and punctuation
        tokens.push_back({std::wstring(1, c), SyntaxTokenType::Operator});
        i++;
    }

    return tokens;
}

// Get color for token type
static D2D1_COLOR_F getTokenColor(const D2DTheme& theme, SyntaxTokenType ttype) {
    switch (ttype) {
        case SyntaxTokenType::Keyword:  return theme.syntaxKeyword;
        case SyntaxTokenType::String:   return theme.syntaxString;
        case SyntaxTokenType::Comment:  return theme.syntaxComment;
        case SyntaxTokenType::Number:   return theme.syntaxNumber;
        case SyntaxTokenType::Function: return theme.syntaxFunction;
        case SyntaxTokenType::TypeName:     return theme.syntaxType;
        case SyntaxTokenType::Operator: return theme.code;
        default:                  return theme.code;
    }
}

void renderCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    std::string code;
    for (const auto& child : elem->children) {
        if (child->type == ElementType::Text) {
            code += child->text;
        }
    }

    // Get language from element
    std::wstring langHint = toWide(elem->language);
    int language = detectLanguage(langHint);

    float scale = app.contentScale * app.zoomFactor;
    float lineHeight = 20.0f * scale;
    float padding = 12.0f * scale;

    // Count lines
    int lineCount = 1;
    for (char c : code) if (c == '\n') lineCount++;

    app.docText += L"\n";

    float blockHeight = lineCount * lineHeight + padding * 2;

    // Background
    float renderY = y - app.scrollY;
    app.brush->SetColor(app.theme.codeBackground);
    app.renderTarget->FillRectangle(
        D2D1::RectF(indent, renderY, indent + maxWidth, renderY + blockHeight),
        app.brush);
    app.drawCalls++;

    // Render lines with syntax highlighting
    std::wstring wcode = toWide(code);
    float textY = y + padding;
    bool inBlockComment = false;
    size_t codeDocStart = app.docText.size();
    size_t lineStart = 0;

    while (lineStart <= wcode.length()) {
        size_t lineEnd = wcode.find(L'\n', lineStart);
        if (lineEnd == std::wstring::npos) lineEnd = wcode.length();

        std::wstring wline = wcode.substr(lineStart, lineEnd - lineStart);
        if (!wline.empty() && wline.back() == L'\r') wline.pop_back();

        size_t lineDocStart = codeDocStart + lineStart;
        recordSearchMatchPositions(app, lineDocStart, lineDocStart + wline.length(), textY);

        float lineRenderY = textY - app.scrollY;
        if (lineRenderY > -lineHeight && lineRenderY < app.height + lineHeight) {
            float textWidth = measureText(app, wline, app.codeFormat);

            // Apply syntax highlighting if language is known
            if (language > 0) {
                std::vector<SyntaxToken> tokens = tokenizeLine(wline, language, inBlockComment);
                float tokenX = indent + padding;

                for (const auto& token : tokens) {
                    if (token.text.empty()) continue;

                    D2D1_COLOR_F tokenColor = getTokenColor(app.theme, token.tokenType);
                    app.brush->SetColor(tokenColor);

                    float tokenWidth = measureText(app, token.text, app.codeFormat);

                    // Use TextLayout for code typography
                    IDWriteTextLayout* layout = nullptr;
                    app.dwriteFactory->CreateTextLayout(token.text.c_str(), (UINT32)token.text.length(),
                        app.codeFormat, tokenWidth + 50, lineHeight, &layout);
                    if (layout) {
                        if (app.codeTypography) {
                            layout->SetTypography(app.codeTypography, {0, (UINT32)token.text.length()});
                        }
                        app.renderTarget->DrawTextLayout(D2D1::Point2F(tokenX, lineRenderY), layout, app.brush);
                        layout->Release();
                    } else {
                        app.renderTarget->DrawText(token.text.c_str(), (UINT32)token.text.length(), app.codeFormat,
                            D2D1::RectF(tokenX, lineRenderY, tokenX + tokenWidth + 50, lineRenderY + lineHeight),
                            app.brush);
                    }
                    app.drawCalls++;
                    tokenX += tokenWidth;
                }
            } else {
                // No syntax highlighting - just render plain text
                app.brush->SetColor(app.theme.code);
                app.renderTarget->DrawText(wline.c_str(), (UINT32)wline.length(), app.codeFormat,
                    D2D1::RectF(indent + padding, lineRenderY, indent + maxWidth - padding, lineRenderY + lineHeight),
                    app.brush);
                app.drawCalls++;
            }

            // Track text bounds for selection
            if (!wline.empty()) {
                app.textRects.push_back({D2D1::RectF(indent + padding, lineRenderY,
                    indent + padding + textWidth, lineRenderY + lineHeight), wline, lineDocStart});
            }
        }

        textY += lineHeight;

        if (lineEnd == wcode.length()) break;
        lineStart = lineEnd + 1;
    }

    app.docText += wcode;
    app.docText += L"\n\n";

    y += blockHeight + 14 * scale;  // Increased spacing after code blocks
}

void renderBlockquote(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float quoteIndent = 20.0f * scale;
    float startY = y;

    for (const auto& child : elem->children) {
        renderElement(app, child, y, indent + quoteIndent, maxWidth - quoteIndent);
    }

    // Border
    app.brush->SetColor(app.theme.blockquoteBorder);
    app.renderTarget->FillRectangle(
        D2D1::RectF(indent, startY - app.scrollY, indent + 4, y - app.scrollY),
        app.brush);
    app.drawCalls++;
}

void renderList(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    float listIndent = 24.0f * scale;
    int itemNum = elem->start;

    for (const auto& child : elem->children) {
        if (child->type != ElementType::ListItem) continue;

        // Draw bullet/number
        std::wstring marker = elem->ordered ?
            std::to_wstring(itemNum++) + L"." : L"\x2022";

        float renderY = y - app.scrollY;
        if (renderY > -30 && renderY < app.height + 30) {
            app.brush->SetColor(app.theme.text);
            app.renderTarget->DrawText(marker.c_str(), (UINT32)marker.length(), app.textFormat,
                D2D1::RectF(indent, renderY, indent + listIndent, renderY + 24),
                app.brush);
            app.drawCalls++;
        }

        // Check for block vs inline content
        bool hasBlockChildren = false;
        for (const auto& itemChild : child->children) {
            if (itemChild->type == ElementType::Paragraph ||
                itemChild->type == ElementType::List ||
                itemChild->type == ElementType::CodeBlock ||
                itemChild->type == ElementType::BlockQuote) {
                hasBlockChildren = true;
                break;
            }
        }

        float itemStartY = y;
        if (hasBlockChildren) {
            std::vector<ElementPtr> inlineElements, blockElements;
            for (const auto& itemChild : child->children) {
                if (itemChild->type == ElementType::Paragraph ||
                    itemChild->type == ElementType::List ||
                    itemChild->type == ElementType::CodeBlock ||
                    itemChild->type == ElementType::BlockQuote) {
                    blockElements.push_back(itemChild);
                } else {
                    inlineElements.push_back(itemChild);
                }
            }

            if (!inlineElements.empty()) {
                renderInlineContent(app, inlineElements, indent + listIndent, y,
                    maxWidth - listIndent, app.textFormat, app.theme.text);
            }

            for (const auto& blockChild : blockElements) {
                renderElement(app, blockChild, y, indent + listIndent, maxWidth - listIndent);
            }
        } else {
            renderInlineContent(app, child->children, indent + listIndent, y,
                maxWidth - listIndent, app.textFormat, app.theme.text);
        }

        app.docText += L"\n\n";

        if (y < itemStartY + 28 * scale) {
            y = itemStartY + 28 * scale;  // Slightly more space per list item
        }
    }
    y += 8 * scale;  // Extra spacing after list
}

void renderHorizontalRule(App& app, float& y, float indent, float maxWidth) {
    float scale = app.contentScale * app.zoomFactor;
    y += 16 * scale;  // Increased spacing before rule
    float renderY = y - app.scrollY;

    app.brush->SetColor(app.theme.blockquoteBorder);
    app.renderTarget->DrawLine(
        D2D1::Point2F(indent, renderY),
        D2D1::Point2F(indent + maxWidth, renderY),
        app.brush, scale);
    app.drawCalls++;

    y += 16 * scale;  // Increased spacing after rule
}

void renderElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth) {
    if (!elem) return;

    switch (elem->type) {
        case ElementType::Paragraph:
            renderParagraph(app, elem, y, indent, maxWidth);
            break;
        case ElementType::Heading:
            renderHeading(app, elem, y, indent, maxWidth);
            break;
        case ElementType::CodeBlock:
            renderCodeBlock(app, elem, y, indent, maxWidth);
            break;
        case ElementType::BlockQuote:
            renderBlockquote(app, elem, y, indent, maxWidth);
            break;
        case ElementType::List:
            renderList(app, elem, y, indent, maxWidth);
            break;
        case ElementType::HorizontalRule:
            renderHorizontalRule(app, y, indent, maxWidth);
            break;
        case ElementType::HtmlBlock:
            // HTML blocks are parsed into child elements, render them
            for (const auto& child : elem->children) {
                renderElement(app, child, y, indent, maxWidth);
            }
            break;
        default:
            for (const auto& child : elem->children) {
                renderElement(app, child, y, indent, maxWidth);
            }
            break;
    }
}

// Content height is calculated during render pass

// ═══════════════════════════════════════════════════════════════════════════
// SEARCH FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

std::wstring toLower(const std::wstring& str) {
    std::wstring result = str;
    for (auto& c : result) {
        c = towlower(c);
    }
    return result;
}

inline void recordSearchMatchPositions(App& app, size_t segStart, size_t segEnd, float lineY) {
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

void render(App& app) {
    if (!app.renderTarget) return;

    app.renderTarget->BeginDraw();
    app.drawCalls = 0;

    // Clear tracking for this frame
    app.linkRects.clear();
    app.textRects.clear();
    app.docText.clear();

    if (!app.searchMatches.empty()) {
        if (app.searchMatchYs.size() != app.searchMatches.size()) {
            app.searchMatchYs.resize(app.searchMatches.size(), -1.0f);
        } else {
            std::fill(app.searchMatchYs.begin(), app.searchMatchYs.end(), -1.0f);
        }
        app.searchMatchCursor = 0;
    } else {
        app.searchMatchYs.clear();
    }

    // Clear background
    app.renderTarget->Clear(app.theme.background);
    app.drawCalls++;

    // Render document
    if (app.root) {
        float scale = app.contentScale * app.zoomFactor;
        float y = 20.0f * scale;
        float indent = 40.0f * scale;

        // Text flows within window width minus margins
        // As zoom increases, indent grows, leaving less room for text (more wrapping)
        float maxWidth = app.width - indent * 2;

        // Content width matches window (text wraps to fit)
        app.contentWidth = app.width;

        for (const auto& child : app.root->children) {
            renderElement(app, child, y, indent - app.scrollX, maxWidth);
        }

        // Update content height (track the final y position)
        app.contentHeight = y + 40.0f * scale;

        // Clamp scroll values
        float maxScrollX = std::max(0.0f, app.contentWidth - app.width);
        float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
        app.scrollX = std::min(app.scrollX, maxScrollX);
        app.scrollY = std::min(app.scrollY, maxScrollY);
    }

    // Determine scrollbar visibility
    bool needsVScroll = app.contentHeight > app.height;
    bool needsHScroll = app.contentWidth > app.width;
    float scrollbarSize = 14.0f;

    // Scrollbar color: dark on light themes, light on dark themes
    float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;

    // Draw vertical scrollbar
    if (needsVScroll) {
        float maxScrollY = std::max(0.0f, app.contentHeight - app.height);
        float trackHeight = app.height - (needsHScroll ? scrollbarSize : 0);
        float sbHeight = trackHeight / app.contentHeight * trackHeight;
        sbHeight = std::max(sbHeight, 30.0f);
        float sbY = (maxScrollY > 0) ? (app.scrollY / maxScrollY * (trackHeight - sbHeight)) : 0;

        float sbWidth = (app.scrollbarHovered || app.scrollbarDragging) ? 10.0f : 6.0f;
        float sbAlpha = (app.scrollbarHovered || app.scrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(app.width - sbWidth - 4, sbY,
                                          app.width - 4, sbY + sbHeight), 3, 3),
            app.brush);
        app.drawCalls++;
    }

    // Draw horizontal scrollbar
    if (needsHScroll) {
        float maxScrollX = std::max(0.0f, app.contentWidth - app.width);
        float trackWidth = app.width - (needsVScroll ? scrollbarSize : 0);
        float sbWidth = trackWidth / app.contentWidth * trackWidth;
        sbWidth = std::max(sbWidth, 30.0f);
        float sbX = (maxScrollX > 0) ? (app.scrollX / maxScrollX * (trackWidth - sbWidth)) : 0;

        float sbHeight = (app.hScrollbarHovered || app.hScrollbarDragging) ? 10.0f : 6.0f;
        float sbAlpha = (app.hScrollbarHovered || app.hScrollbarDragging) ? 0.5f : 0.3f;

        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, sbAlpha));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(sbX, app.height - sbHeight - 4,
                                          sbX + sbWidth, app.height - 4), 3, 3),
            app.brush);
        app.drawCalls++;
    }

    // Draw selection highlights
    if ((app.selecting || app.hasSelection) && !app.textRects.empty()) {
        // Calculate selection bounds (normalized so start is always before end)
        // Selection is stored in document coordinates, convert to screen coordinates
        float selStartX = (float)app.selStartX;
        float selStartY = (float)app.selStartY - app.scrollY;
        float selEndX = (float)app.selEndX;
        float selEndY = (float)app.selEndY - app.scrollY;

        // Swap if selection was made bottom-to-top
        if (selStartY > selEndY || (selStartY == selEndY && selStartX > selEndX)) {
            std::swap(selStartX, selEndX);
            std::swap(selStartY, selEndY);
        }

        // Check if this is a "select all" (selectedText is set but selection coords are same)
        bool isSelectAll = app.hasSelection && !app.selectedText.empty() &&
                          app.selStartX == app.selEndX && app.selStartY == app.selEndY;

        app.brush->SetColor(D2D1::ColorF(0.2f, 0.4f, 0.9f, 0.35f));

        // Group text rects by line (Y position)
        struct LineInfo {
            float top, bottom;
            float minX, maxX;
            std::vector<const App::TextRect*> rects;
        };
        std::vector<LineInfo> lines;

        for (const auto& tr : app.textRects) {
            const D2D1_RECT_F& rect = tr.rect;
            bool foundLine = false;
            for (auto& line : lines) {
                if (std::abs(rect.top - line.top) < 5) {
                    line.minX = std::min(line.minX, rect.left);
                    line.maxX = std::max(line.maxX, rect.right);
                    line.rects.push_back(&tr);
                    foundLine = true;
                    break;
                }
            }
            if (!foundLine) {
                lines.push_back({rect.top, rect.bottom, rect.left, rect.right, {&tr}});
            }
        }

        // Sort lines by Y position
        std::sort(lines.begin(), lines.end(), [](const LineInfo& a, const LineInfo& b) {
            return a.top < b.top;
        });

        std::wstring collectedText;
        size_t selectedCount = 0;

        for (size_t i = 0; i < lines.size(); i++) {
            const auto& line = lines[i];
            float lineCenterY = (line.top + line.bottom) / 2;

            bool lineInSelection = false;
            float drawLeft = line.minX;
            float drawRight = line.maxX;

            if (isSelectAll) {
                lineInSelection = true;
            } else if (lineCenterY >= selStartY - 10 && lineCenterY <= selEndY + 10) {
                float lineHeight = line.bottom - line.top;
                bool isSingleLine = (selEndY - selStartY) <= lineHeight;

                if (isSingleLine) {
                    // Single line selection
                    drawLeft = std::max(line.minX, selStartX);
                    drawRight = std::min(line.maxX, selEndX);
                    if (drawLeft < drawRight) lineInSelection = true;
                } else if (lineCenterY < selStartY + lineHeight) {
                    // First line - from selection start to end of line
                    drawLeft = std::max(line.minX, selStartX);
                    lineInSelection = true;
                } else if (lineCenterY > selEndY - lineHeight) {
                    // Last line - from start of line to selection end
                    drawRight = std::min(line.maxX, selEndX);
                    lineInSelection = true;
                } else {
                    // Middle line - full width
                    lineInSelection = true;
                }
            }

            if (lineInSelection) {
                // Draw continuous selection bar for this line
                app.renderTarget->FillRectangle(
                    D2D1::RectF(drawLeft, line.top, drawRight, line.bottom),
                    app.brush);
                selectedCount++;

                // Collect text from rects in this line that fall within selection
                if (!collectedText.empty()) collectedText += L"\n";
                for (const auto* tr : line.rects) {
                    const D2D1_RECT_F& rect = tr->rect;
                    if (rect.left < drawRight && rect.right > drawLeft) {
                        if (!collectedText.empty() && collectedText.back() != L'\n') {
                            collectedText += L" ";
                        }
                        collectedText += tr->text;
                    }
                }
            }
        }
        app.drawCalls += selectedCount;

        // Update selectedText for mouse selections (not select-all)
        if (!isSelectAll && app.hasSelection && selectedCount > 0) {
            app.selectedText = collectedText;
        }
    }

    // Draw search match highlights (search live through visible textRects)
    if (app.showSearch && !app.searchQuery.empty() && !app.textRects.empty() && !app.searchMatches.empty()) {
        // Collect visible match rects by intersecting search matches with text rects
        struct VisibleMatch {
            D2D1_RECT_F rect;
            size_t matchIndex;
        };
        std::vector<VisibleMatch> visibleMatches;

        size_t matchIndex = 0;
        for (const auto& tr : app.textRects) {
            size_t textLen = tr.text.length();
            if (textLen == 0) continue;

            size_t rectStart = tr.docStart;
            size_t rectEnd = rectStart + textLen;

            // Advance to first match that could overlap this rect
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

                size_t mStart = m.startPos;
                size_t mEnd = m.startPos + m.length;
                size_t overlapStart = std::max(rectStart, mStart);
                size_t overlapEnd = std::min(rectEnd, mEnd);

                if (overlapStart < overlapEnd) {
                    float totalWidth = tr.rect.right - tr.rect.left;
                    float charWidth = totalWidth / (float)textLen;
                    float startX = tr.rect.left + (overlapStart - rectStart) * charWidth;
                    float matchWidth = (overlapEnd - overlapStart) * charWidth;

                    // Extend highlight slightly for better visibility
                    D2D1_RECT_F highlightRect = D2D1::RectF(
                        startX - 1, tr.rect.top,
                        startX + matchWidth + 1, tr.rect.bottom
                    );

                    visibleMatches.push_back({highlightRect, mi});
                }

                if (mEnd <= rectEnd) {
                    mi++;
                } else {
                    break;  // Match spans beyond this rect; continue on next rect
                }
            }

            matchIndex = mi;
        }

        // Draw all matches - orange if it's the current match, yellow otherwise
        for (const auto& vm : visibleMatches) {
            bool isCurrent = (app.searchCurrentIndex >= 0 &&
                              vm.matchIndex == (size_t)app.searchCurrentIndex);

            if (isCurrent) {
                app.brush->SetColor(D2D1::ColorF(1.0f, 0.6f, 0.0f, 0.5f));  // Orange
            } else {
                app.brush->SetColor(D2D1::ColorF(1.0f, 0.9f, 0.0f, 0.3f));  // Yellow
            }

            app.renderTarget->FillRectangle(vm.rect, app.brush);
            app.drawCalls++;
        }
    }

    // "Copied!" notification with fade out
    if (app.showCopiedNotification) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - app.copiedNotificationStart).count();

        if (elapsed < 2.0f) {
            // Fade out over 2 seconds (stay solid for first 0.5s, then fade)
            float alpha = 1.0f;
            if (elapsed > 0.5f) {
                alpha = 1.0f - (elapsed - 0.5f) / 1.5f;
            }
            app.copiedNotificationAlpha = alpha;

            const wchar_t* copyText = L"Copied!";
            float copyWidth = 100.0f;
            float copyHeight = 26;
            float pillX = (app.width - copyWidth) / 2;
            float pillY = 10;

            // Background pill (green for success)
            app.brush->SetColor(D2D1::ColorF(0.2f, 0.7f, 0.3f, 0.9f * alpha));
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + copyWidth, pillY + copyHeight), 13, 13),
                app.brush);

            // Text
            app.brush->SetColor(D2D1::ColorF(1, 1, 1, alpha));
            app.renderTarget->DrawText(copyText, (UINT32)wcslen(copyText), app.textFormat,
                D2D1::RectF(pillX + 10, pillY + 3, pillX + copyWidth - 10, pillY + copyHeight),
                app.brush);
            app.drawCalls++;

            // Keep animating
            InvalidateRect(app.hwnd, nullptr, FALSE);
        } else {
            app.showCopiedNotification = false;
        }
    }

    // Draw stats
    if (app.showStats) {
        wchar_t stats[512];
        swprintf(stats, 512,
            L"Parse: %zu us | Draw calls: %zu\n"
            L"Startup: %.1fms (Win: %.1f | D2D: %.1f | DWrite: %.1f | File: %.1f)",
            app.parseTimeUs,
            app.drawCalls,
            app.metrics.totalStartupUs / 1000.0,
            app.metrics.windowInitUs / 1000.0,
            app.metrics.d2dInitUs / 1000.0,
            app.metrics.dwriteInitUs / 1000.0,
            app.metrics.fileLoadUs / 1000.0);

        float statsWidth = 600;
        float statsHeight = 50;

        app.brush->SetColor(D2D1::ColorF(0, 0, 0, 0.8f));
        app.renderTarget->FillRectangle(
            D2D1::RectF(app.width - statsWidth - 10, app.height - statsHeight - 10,
                       app.width - 10, app.height - 10),
            app.brush);

        app.brush->SetColor(D2D1::ColorF(0.7f, 0.9f, 0.7f));
        app.renderTarget->DrawText(stats, (UINT32)wcslen(stats), app.codeFormat,
            D2D1::RectF(app.width - statsWidth - 5, app.height - statsHeight - 5,
                       app.width - 15, app.height - 15),
            app.brush);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // SEARCH OVERLAY
    // ═══════════════════════════════════════════════════════════════════════════
    if (app.showSearch) {
        // Animate in
        if (app.searchAnimation < 1.0f) {
            app.searchAnimation = std::min(1.0f, app.searchAnimation + 0.2f);
            InvalidateRect(app.hwnd, nullptr, FALSE);
        }
        float anim = app.searchAnimation;

        // Search bar dimensions
        float barWidth = std::min(500.0f, app.width - 40.0f);
        float barHeight = 44.0f;
        float barX = (app.width - barWidth) / 2;
        float barY = 20.0f * anim - barHeight * (1.0f - anim);  // Slide down from top

        // Background with rounded corners
        D2D1_ROUNDED_RECT barRect = D2D1::RoundedRect(
            D2D1::RectF(barX, barY, barX + barWidth, barY + barHeight),
            8, 8);

        // Semi-transparent background based on theme
        if (app.theme.isDark) {
            app.brush->SetColor(D2D1::ColorF(0.12f, 0.12f, 0.14f, 0.95f * anim));
        } else {
            app.brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f * anim));
        }
        app.renderTarget->FillRoundedRectangle(barRect, app.brush);

        // Border
        if (app.theme.isDark) {
            app.brush->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.35f, 0.8f * anim));
        } else {
            app.brush->SetColor(D2D1::ColorF(0.7f, 0.7f, 0.75f, 0.8f * anim));
        }
        app.renderTarget->DrawRoundedRectangle(barRect, app.brush, 1.0f);

        // Search icon (simple circle for magnifying glass look)
        {
            D2D1_COLOR_F iconColor = app.theme.text;
            iconColor.a = 0.5f * anim;
            app.brush->SetColor(iconColor);
            // Draw a simple magnifying glass shape
            float iconX = barX + 22;
            float iconY = barY + 22;
            app.renderTarget->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(iconX, iconY - 2), 7, 7),
                app.brush, 2.0f);
            app.renderTarget->DrawLine(
                D2D1::Point2F(iconX + 5, iconY + 3),
                D2D1::Point2F(iconX + 9, iconY + 7),
                app.brush, 2.0f);
        }

        // Search text
        IDWriteTextFormat* searchTextFormat = nullptr;
        app.dwriteFactory->CreateTextFormat(app.theme.fontFamily, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            16, L"en-us", &searchTextFormat);
        if (searchTextFormat) {
            float textX = barX + 42;
            float textWidth = barWidth - 120;  // Leave room for count

            if (app.searchQuery.empty()) {
                // Placeholder text
                D2D1_COLOR_F placeholderColor = app.theme.text;
                placeholderColor.a = 0.4f * anim;
                app.brush->SetColor(placeholderColor);
                app.renderTarget->DrawText(L"Search...", 9, searchTextFormat,
                    D2D1::RectF(textX, barY + 12, textX + textWidth, barY + barHeight), app.brush);
            } else {
                // Actual search query
                D2D1_COLOR_F textColor = app.theme.text;
                textColor.a = anim;
                app.brush->SetColor(textColor);
                app.renderTarget->DrawText(app.searchQuery.c_str(), (UINT32)app.searchQuery.length(),
                    searchTextFormat,
                    D2D1::RectF(textX, barY + 12, textX + textWidth, barY + barHeight), app.brush);

                // Blinking cursor
                auto now = std::chrono::steady_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                bool cursorVisible = (ms % 1000) < 500;
                if (app.searchActive && cursorVisible) {
                    float queryWidth = measureText(app, app.searchQuery, searchTextFormat);
                    float cursorX = textX + queryWidth + 2;
                    app.brush->SetColor(textColor);
                    app.renderTarget->DrawLine(
                        D2D1::Point2F(cursorX, barY + 12),
                        D2D1::Point2F(cursorX, barY + 32),
                        app.brush, 1.5f);
                    // Keep animating cursor
                    InvalidateRect(app.hwnd, nullptr, FALSE);
                }
            }

            // Match count
            float countX = barX + barWidth - 80;
            if (!app.searchQuery.empty()) {
                wchar_t countText[32];
                if (app.searchMatches.empty()) {
                    wcscpy_s(countText, L"No matches");
                    // Red color for no matches
                    app.brush->SetColor(D2D1::ColorF(0.9f, 0.3f, 0.3f, anim));
                } else {
                    swprintf_s(countText, L"%d of %zu", app.searchCurrentIndex + 1, app.searchMatches.size());
                    D2D1_COLOR_F countColor = app.theme.text;
                    countColor.a = 0.7f * anim;
                    app.brush->SetColor(countColor);
                }
                app.renderTarget->DrawText(countText, (UINT32)wcslen(countText), searchTextFormat,
                    D2D1::RectF(countX, barY + 12, barX + barWidth - 10, barY + barHeight), app.brush);
            }

            searchTextFormat->Release();
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // THEME CHOOSER OVERLAY
    // ═══════════════════════════════════════════════════════════════════════════
    if (app.showThemeChooser) {
        // Animate in
        if (app.themeChooserAnimation < 1.0f) {
            app.themeChooserAnimation = std::min(1.0f, app.themeChooserAnimation + 0.15f);
            // Keep invalidating to continue animation
            InvalidateRect(app.hwnd, nullptr, FALSE);
        }
        float anim = app.themeChooserAnimation;

        // Semi-transparent backdrop with blur effect simulation
        float backdropAlpha = 0.85f * anim;
        app.brush->SetColor(D2D1::ColorF(0, 0, 0, backdropAlpha));
        app.renderTarget->FillRectangle(
            D2D1::RectF(0, 0, (float)app.width, (float)app.height), app.brush);

        // Panel dimensions - 2 columns (Light | Dark), 5 rows
        float panelWidth = std::min(900.0f, app.width - 80.0f);
        float panelHeight = std::min(620.0f, app.height - 80.0f);
        float panelX = (app.width - panelWidth) / 2;
        float panelY = (app.height - panelHeight) / 2 + (1 - anim) * 50;

        // Panel background with subtle gradient simulation
        D2D1_ROUNDED_RECT panelRect = D2D1::RoundedRect(
            D2D1::RectF(panelX, panelY, panelX + panelWidth, panelY + panelHeight),
            16, 16);
        app.brush->SetColor(hexColor(0x1A1A1E, 0.98f * anim));
        app.renderTarget->FillRoundedRectangle(panelRect, app.brush);

        // Subtle border
        app.brush->SetColor(hexColor(0x3A3A40, 0.6f * anim));
        app.renderTarget->DrawRoundedRectangle(panelRect, app.brush, 1.0f);

        // Title
        IDWriteTextFormat* titleFormat = nullptr;
        app.dwriteFactory->CreateTextFormat(L"Segoe UI Light", nullptr,
            DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            28, L"en-us", &titleFormat);
        if (titleFormat) {
            titleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            app.brush->SetColor(D2D1::ColorF(1, 1, 1, anim));
            app.renderTarget->DrawText(L"Choose Theme", 12, titleFormat,
                D2D1::RectF(panelX, panelY + 15, panelX + panelWidth, panelY + 55), app.brush);
            titleFormat->Release();
        }

        // Theme grid - 2 columns, 5 rows
        float gridStartY = panelY + 75;
        float cardWidth = (panelWidth - 60) / 2;  // 2 columns with padding
        float cardHeight = (panelHeight - 130) / 5;  // 5 rows
        float cardPadding = 8;

        app.hoveredThemeIndex = -1;

        for (int i = 0; i < THEME_COUNT; i++) {
            const D2DTheme& t = THEMES[i];
            int col = t.isDark ? 1 : 0;  // Light themes left, dark themes right
            int row = t.isDark ? (i - 5) : i;

            float cardX = panelX + 20 + col * (cardWidth + 20);
            float cardY = gridStartY + row * cardHeight;
            float innerX = cardX + cardPadding;
            float innerY = cardY + cardPadding;
            float innerW = cardWidth - cardPadding * 2;
            float innerH = cardHeight - cardPadding * 2;

            // Check hover
            bool isHovered = (app.mouseX >= innerX && app.mouseX <= innerX + innerW &&
                              app.mouseY >= innerY && app.mouseY <= innerY + innerH);
            bool isSelected = (i == app.currentThemeIndex);

            if (isHovered) {
                app.hoveredThemeIndex = i;
            }

            // Card background (theme preview)
            D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(
                D2D1::RectF(innerX, innerY, innerX + innerW, innerY + innerH),
                10, 10);

            // Selection/hover glow
            if (isSelected || isHovered) {
                float glowSize = isSelected ? 3.0f : 2.0f;
                D2D1_ROUNDED_RECT glowRect = D2D1::RoundedRect(
                    D2D1::RectF(innerX - glowSize, innerY - glowSize,
                                innerX + innerW + glowSize, innerY + innerH + glowSize),
                    12, 12);
                D2D1_COLOR_F glowColor = t.accent;
                glowColor.a = (isSelected ? 0.8f : 0.5f) * anim;
                app.brush->SetColor(glowColor);
                app.renderTarget->DrawRoundedRectangle(glowRect, app.brush, 2.0f);
            }

            // Theme background preview
            D2D1_COLOR_F bgColor = t.background;
            bgColor.a = anim;
            app.brush->SetColor(bgColor);
            app.renderTarget->FillRoundedRectangle(cardRect, app.brush);

            // Theme name
            IDWriteTextFormat* nameFormat = nullptr;
            app.dwriteFactory->CreateTextFormat(t.fontFamily, nullptr,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                14, L"en-us", &nameFormat);
            if (nameFormat) {
                D2D1_COLOR_F nameColor = t.heading;
                nameColor.a = anim;
                app.brush->SetColor(nameColor);
                app.renderTarget->DrawText(t.name, (UINT32)wcslen(t.name), nameFormat,
                    D2D1::RectF(innerX + 12, innerY + 8, innerX + innerW - 10, innerY + 28), app.brush);
                nameFormat->Release();
            }

            // Preview text samples
            IDWriteTextFormat* previewFormat = nullptr;
            app.dwriteFactory->CreateTextFormat(t.fontFamily, nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                11, L"en-us", &previewFormat);
            if (previewFormat) {
                // Sample text
                D2D1_COLOR_F textColor = t.text;
                textColor.a = anim;
                app.brush->SetColor(textColor);
                app.renderTarget->DrawText(L"The quick brown fox", 19, previewFormat,
                    D2D1::RectF(innerX + 12, innerY + 30, innerX + innerW - 10, innerY + 45), app.brush);

                // Link sample
                D2D1_COLOR_F linkColor = t.link;
                linkColor.a = anim;
                app.brush->SetColor(linkColor);
                app.renderTarget->DrawText(L"hyperlink", 9, previewFormat,
                    D2D1::RectF(innerX + 12, innerY + 44, innerX + 80, innerY + 58), app.brush);

                // Code sample background
                D2D1_COLOR_F codeBgColor = t.codeBackground;
                codeBgColor.a = anim;
                app.brush->SetColor(codeBgColor);
                app.renderTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(innerX + 75, innerY + 44, innerX + 140, innerY + 58), 3, 3),
                    app.brush);

                // Code text
                IDWriteTextFormat* codePreviewFormat = nullptr;
                app.dwriteFactory->CreateTextFormat(t.codeFontFamily, nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    10, L"en-us", &codePreviewFormat);
                if (codePreviewFormat) {
                    D2D1_COLOR_F codeColor = t.code;
                    codeColor.a = anim;
                    app.brush->SetColor(codeColor);
                    app.renderTarget->DrawText(L"code()", 6, codePreviewFormat,
                        D2D1::RectF(innerX + 78, innerY + 45, innerX + 138, innerY + 58), app.brush);
                    codePreviewFormat->Release();
                }

                previewFormat->Release();
            }

            // Checkmark for selected theme
            if (isSelected) {
                D2D1_COLOR_F checkColor = t.accent;
                checkColor.a = anim;
                app.brush->SetColor(checkColor);
                app.renderTarget->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(innerX + innerW - 18, innerY + 15), 8, 8),
                    app.brush);
                app.brush->SetColor(t.isDark ? hexColor(0x000000, anim) : hexColor(0xFFFFFF, anim));
                // Draw checkmark using lines
                app.renderTarget->DrawLine(
                    D2D1::Point2F(innerX + innerW - 22, innerY + 15),
                    D2D1::Point2F(innerX + innerW - 18, innerY + 19),
                    app.brush, 2.0f);
                app.renderTarget->DrawLine(
                    D2D1::Point2F(innerX + innerW - 18, innerY + 19),
                    D2D1::Point2F(innerX + innerW - 13, innerY + 11),
                    app.brush, 2.0f);
            }

            // Border
            D2D1_COLOR_F borderColor = t.isDark ? hexColor(0x404040) : hexColor(0xD0D0D0);
            borderColor.a = 0.5f * anim;
            app.brush->SetColor(borderColor);
            app.renderTarget->DrawRoundedRectangle(cardRect, app.brush, 1.0f);
        }

        // Column headers
        IDWriteTextFormat* headerFormat = nullptr;
        app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11, L"en-us", &headerFormat);
        if (headerFormat) {
            headerFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            app.brush->SetColor(D2D1::ColorF(0.5f, 0.5f, 0.5f, anim));

            // Light themes header
            app.renderTarget->DrawText(L"LIGHT THEMES", 12, headerFormat,
                D2D1::RectF(panelX + 20, gridStartY - 20, panelX + 20 + cardWidth, gridStartY - 5), app.brush);

            // Dark themes header
            app.renderTarget->DrawText(L"DARK THEMES", 11, headerFormat,
                D2D1::RectF(panelX + 40 + cardWidth, gridStartY - 20, panelX + 40 + cardWidth * 2, gridStartY - 5), app.brush);

            headerFormat->Release();
        }
    }

    app.renderTarget->EndDraw();
}

void openUrl(const std::string& url) {
    if (!url.empty()) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void copyToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return;

    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();

    size_t size = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hMem) {
        wchar_t* dest = (wchar_t*)GlobalLock(hMem);
        if (dest) {
            memcpy(dest, text.c_str(), size);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }

    CloseClipboard();
}

// Extract plain text from element tree (for select all)
void extractText(const ElementPtr& elem, std::wstring& out) {
    if (!elem) return;

    switch (elem->type) {
        case ElementType::Text:
            out += toWide(elem->text);
            break;
        case ElementType::SoftBreak:
            out += L" ";
            break;
        case ElementType::HardBreak:
            out += L"\n";
            break;
        case ElementType::Paragraph:
        case ElementType::Heading:
        case ElementType::ListItem:
            for (const auto& child : elem->children) {
                extractText(child, out);
            }
            out += L"\n\n";
            break;
        case ElementType::CodeBlock: {
            out += L"\n";
            for (const auto& child : elem->children) {
                if (child->type == ElementType::Text) {
                    out += toWide(child->text);
                }
            }
            out += L"\n\n";
            break;
        }
        default:
            for (const auto& child : elem->children) {
                extractText(child, out);
            }
            break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = g_app;

    switch (msg) {
        case WM_SIZE:
            if (app && app->d2dFactory) {
                app->width = LOWORD(lParam);
                app->height = HIWORD(lParam);
                createRenderTarget(*app);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_DPICHANGED:
            if (app) {
                UINT dpi = HIWORD(wParam);
                app->contentScale = dpi / 96.0f;

                // Resize window to suggested new size
                RECT* newRect = (RECT*)lParam;
                SetWindowPos(hwnd, nullptr,
                    newRect->left, newRect->top,
                    newRect->right - newRect->left,
                    newRect->bottom - newRect->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);

                // Recreate text formats and render target for new DPI
                updateTextFormats(*app);
                createRenderTarget(*app);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (app) render(*app);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEWHEEL:
            if (app) {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;

                if (ctrl) {
                    // Zoom in/out
                    float zoomDelta = delta * 0.1f;
                    app->zoomFactor = std::max(0.5f, std::min(3.0f, app->zoomFactor + zoomDelta));
                    updateTextFormats(*app);
                } else {
                    // Normal scroll
                    app->targetScrollY -= delta * 60.0f;
                    float maxScroll = std::max(0.0f, app->contentHeight - app->height);
                    app->targetScrollY = std::max(0.0f, std::min(app->targetScrollY, maxScroll));
                    app->scrollY = app->targetScrollY;
                }

                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_MOUSEHWHEEL:
            if (app) {
                // Horizontal scroll
                float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 60.0f;
                app->targetScrollX += delta;

                float maxScrollX = std::max(0.0f, app->contentWidth - app->width);
                app->targetScrollX = std::max(0.0f, std::min(app->targetScrollX, maxScrollX));
                app->scrollX = app->targetScrollX;

                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_MOUSEMOVE:
            if (app) {
                app->mouseX = GET_X_LPARAM(lParam);
                app->mouseY = GET_Y_LPARAM(lParam);

                // Text selection dragging
                if (app->selecting) {
                    if (app->selectionMode == App::SelectionMode::Word) {
                        // Extend selection by words - merge anchor with current word
                        const App::TextRect* tr = findTextRectAt(*app, app->mouseX, app->mouseY);
                        if (tr) {
                            float wordLeft, wordRight;
                            if (findWordBoundsAt(*app, *tr, app->mouseX, wordLeft, wordRight)) {
                                // Selection spans from min(anchor, current) to max(anchor, current)
                                // Store in document coordinates (add scrollY)
                                app->selStartX = (int)std::min(app->anchorLeft, wordLeft);
                                app->selEndX = (int)std::max(app->anchorRight, wordRight);
                                app->selStartY = (int)(std::min(app->anchorTop, tr->rect.top) + app->scrollY);
                                app->selEndY = (int)(std::max(app->anchorBottom, tr->rect.bottom) + app->scrollY);
                                app->hasSelection = true;
                            }
                        }
                    } else if (app->selectionMode == App::SelectionMode::Line) {
                        // Extend selection by lines - merge anchor with current line
                        float lineLeft, lineRight, lineTop, lineBottom;
                        findLineRects(*app, (float)app->mouseY, lineLeft, lineRight, lineTop, lineBottom);
                        if (lineRight > lineLeft) {
                            // Selection spans from min(anchor, current) to max(anchor, current)
                            // Store in document coordinates (add scrollY)
                            app->selStartX = (int)std::min(app->anchorLeft, lineLeft);
                            app->selEndX = (int)std::max(app->anchorRight, lineRight);
                            app->selStartY = (int)(std::min(app->anchorTop, lineTop) + app->scrollY);
                            app->selEndY = (int)(std::max(app->anchorBottom, lineBottom) + app->scrollY);
                            app->hasSelection = true;
                        }
                    } else {
                        // Normal selection - store in document coordinates
                        app->selEndX = app->mouseX;
                        app->selEndY = (int)(app->mouseY + app->scrollY);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                // Vertical scrollbar dragging
                if (app->scrollbarDragging) {
                    float maxScroll = std::max(0.0f, app->contentHeight - app->height);
                    if (maxScroll > 0 && app->contentHeight > app->height) {
                        float sbHeight = (float)app->height / app->contentHeight * app->height;
                        sbHeight = std::max(sbHeight, 30.0f);
                        float trackHeight = app->height - sbHeight;

                        float deltaY = (float)app->mouseY - app->scrollbarDragStartY;
                        float scrollDelta = (deltaY / trackHeight) * maxScroll;
                        app->scrollY = app->scrollbarDragStartScroll + scrollDelta;
                        app->scrollY = std::max(0.0f, std::min(app->scrollY, maxScroll));
                        app->targetScrollY = app->scrollY;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }

                // Horizontal scrollbar dragging
                if (app->hScrollbarDragging) {
                    float maxScroll = std::max(0.0f, app->contentWidth - app->width);
                    if (maxScroll > 0 && app->contentWidth > app->width) {
                        float sbWidth = (float)app->width / app->contentWidth * app->width;
                        sbWidth = std::max(sbWidth, 30.0f);
                        float trackWidth = app->width - sbWidth;

                        float deltaX = (float)app->mouseX - app->hScrollbarDragStartX;
                        float scrollDelta = (deltaX / trackWidth) * maxScroll;
                        app->scrollX = app->hScrollbarDragStartScroll + scrollDelta;
                        app->scrollX = std::max(0.0f, std::min(app->scrollX, maxScroll));
                        app->targetScrollX = app->scrollX;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }

                // Check vertical scrollbar hover
                bool wasHovered = app->scrollbarHovered;
                app->scrollbarHovered = false;
                if (app->contentHeight > app->height) {
                    float sbWidth = 14.0f;  // hit area
                    if (app->mouseX >= app->width - sbWidth) {
                        app->scrollbarHovered = true;
                    }
                }

                // Check horizontal scrollbar hover
                bool wasHHovered = app->hScrollbarHovered;
                app->hScrollbarHovered = false;
                if (app->contentWidth > app->width) {
                    float sbHeight = 14.0f;  // hit area
                    if (app->mouseY >= app->height - sbHeight) {
                        app->hScrollbarHovered = true;
                    }
                }

                // Check link hover
                std::string prevHoveredLink = app->hoveredLink;
                app->hoveredLink.clear();
                for (const auto& lr : app->linkRects) {
                    if (app->mouseX >= lr.bounds.left && app->mouseX <= lr.bounds.right &&
                        app->mouseY >= lr.bounds.top && app->mouseY <= lr.bounds.bottom) {
                        app->hoveredLink = lr.url;
                        break;
                    }
                }

                // Check if over text
                bool wasOverText = app->overText;
                app->overText = false;
                for (const auto& tr : app->textRects) {
                    if (app->mouseX >= tr.rect.left && app->mouseX <= tr.rect.right &&
                        app->mouseY >= tr.rect.top && app->mouseY <= tr.rect.bottom) {
                        app->overText = true;
                        break;
                    }
                }

                // Update cursor
                if (app->scrollbarHovered || app->scrollbarDragging ||
                    app->hScrollbarHovered || app->hScrollbarDragging) {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                } else if (!app->hoveredLink.empty()) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                } else if (app->overText) {
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                } else {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                }

                if (wasHovered != app->scrollbarHovered ||
                    wasHHovered != app->hScrollbarHovered ||
                    prevHoveredLink != app->hoveredLink) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (app) {
                // If theme chooser is open, don't start selection - just record for click handling
                if (app->showThemeChooser) {
                    return 0;
                }

                app->mouseDown = true;
                app->mouseX = GET_X_LPARAM(lParam);
                app->mouseY = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);

                // Check if clicking vertical scrollbar
                if (app->scrollbarHovered && app->contentHeight > app->height) {
                    app->scrollbarDragging = true;
                    app->scrollbarDragStartY = (float)app->mouseY;
                    app->scrollbarDragStartScroll = app->scrollY;

                    // Check if clicking in track (not thumb) - jump to position
                    float maxScroll = std::max(0.0f, app->contentHeight - app->height);
                    float sbHeight = (float)app->height / app->contentHeight * app->height;
                    sbHeight = std::max(sbHeight, 30.0f);
                    float sbY = (maxScroll > 0) ? (app->scrollY / maxScroll * (app->height - sbHeight)) : 0;

                    // If clicked outside thumb, jump
                    if (app->mouseY < sbY || app->mouseY > sbY + sbHeight) {
                        float trackHeight = app->height - sbHeight;
                        float clickPos = (float)app->mouseY - sbHeight / 2;
                        clickPos = std::max(0.0f, std::min(clickPos, trackHeight));
                        app->scrollY = (clickPos / trackHeight) * maxScroll;
                        app->targetScrollY = app->scrollY;
                        app->scrollbarDragStartScroll = app->scrollY;
                        app->scrollbarDragStartY = (float)app->mouseY;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                // Check if clicking horizontal scrollbar
                else if (app->hScrollbarHovered && app->contentWidth > app->width) {
                    app->hScrollbarDragging = true;
                    app->hScrollbarDragStartX = (float)app->mouseX;
                    app->hScrollbarDragStartScroll = app->scrollX;

                    // Check if clicking in track (not thumb) - jump to position
                    float maxScroll = std::max(0.0f, app->contentWidth - app->width);
                    float sbWidth = (float)app->width / app->contentWidth * app->width;
                    sbWidth = std::max(sbWidth, 30.0f);
                    float sbX = (maxScroll > 0) ? (app->scrollX / maxScroll * (app->width - sbWidth)) : 0;

                    // If clicked outside thumb, jump
                    if (app->mouseX < sbX || app->mouseX > sbX + sbWidth) {
                        float trackWidth = app->width - sbWidth;
                        float clickPos = (float)app->mouseX - sbWidth / 2;
                        clickPos = std::max(0.0f, std::min(clickPos, trackWidth));
                        app->scrollX = (clickPos / trackWidth) * maxScroll;
                        app->targetScrollX = app->scrollX;
                        app->hScrollbarDragStartScroll = app->scrollX;
                        app->hScrollbarDragStartX = (float)app->mouseX;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    // Detect double/triple clicks
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - app->lastClickTime).count();

                    // Check if this is a repeated click (within 500ms and 5px of last click)
                    bool isRepeatedClick = (elapsed < 500 &&
                        std::abs(app->mouseX - app->lastClickX) < 5 &&
                        std::abs(app->mouseY - app->lastClickY) < 5);

                    if (isRepeatedClick) {
                        app->clickCount = std::min(app->clickCount + 1, 3);
                    } else {
                        app->clickCount = 1;
                    }

                    app->lastClickTime = now;
                    app->lastClickX = app->mouseX;
                    app->lastClickY = app->mouseY;

                    // Handle based on click count
                    if (app->clickCount == 2) {
                        // Double-click: select word
                        const App::TextRect* tr = findTextRectAt(*app, app->mouseX, app->mouseY);
                        if (tr) {
                            float wordLeft, wordRight;
                            if (findWordBoundsAt(*app, *tr, app->mouseX, wordLeft, wordRight)) {
                                app->selectionMode = App::SelectionMode::Word;
                                // Store anchor (the original word bounds) in screen coords for drag extension
                                app->anchorLeft = wordLeft;
                                app->anchorRight = wordRight;
                                app->anchorTop = tr->rect.top;
                                app->anchorBottom = tr->rect.bottom;
                                // Set selection to the word (document coordinates - add scrollY)
                                app->selStartX = (int)wordLeft;
                                app->selEndX = (int)wordRight;
                                app->selStartY = (int)(tr->rect.top + app->scrollY);
                                app->selEndY = (int)(tr->rect.bottom + app->scrollY);
                                app->selecting = true;
                                app->hasSelection = true;
                            }
                        }
                    } else if (app->clickCount == 3) {
                        // Triple-click: select line
                        float lineLeft, lineRight, lineTop, lineBottom;
                        findLineRects(*app, (float)app->mouseY, lineLeft, lineRight, lineTop, lineBottom);
                        if (lineRight > lineLeft) {
                            app->selectionMode = App::SelectionMode::Line;
                            // Store anchor (the original line bounds) in screen coords for drag extension
                            app->anchorLeft = lineLeft;
                            app->anchorRight = lineRight;
                            app->anchorTop = lineTop;
                            app->anchorBottom = lineBottom;
                            // Set selection to the line (document coordinates - add scrollY)
                            app->selStartX = (int)lineLeft;
                            app->selEndX = (int)lineRight;
                            app->selStartY = (int)(lineTop + app->scrollY);
                            app->selEndY = (int)(lineBottom + app->scrollY);
                            app->selecting = true;
                            app->hasSelection = true;
                        }
                    } else {
                        // Single click: start normal selection (document coordinates)
                        app->selectionMode = App::SelectionMode::Normal;
                        app->selecting = true;
                        app->selStartX = app->mouseX;
                        app->selStartY = (int)(app->mouseY + app->scrollY);
                        app->selEndX = app->mouseX;
                        app->selEndY = (int)(app->mouseY + app->scrollY);
                        app->hasSelection = false;
                        app->selectedText.clear();
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (app) {
                ReleaseCapture();

                // Theme chooser click handling
                if (app->showThemeChooser) {
                    int clickX = GET_X_LPARAM(lParam);
                    int clickY = GET_Y_LPARAM(lParam);

                    // Calculate which theme was clicked (replicate layout logic)
                    float panelWidth = std::min(900.0f, (float)app->width - 80.0f);
                    float panelHeight = std::min(620.0f, (float)app->height - 80.0f);
                    float panelX = (app->width - panelWidth) / 2;
                    float panelY = (app->height - panelHeight) / 2;
                    float gridStartY = panelY + 75;
                    float cardWidth = (panelWidth - 60) / 2;
                    float cardHeight = (panelHeight - 130) / 5;
                    float cardPadding = 8;

                    int clickedTheme = -1;
                    for (int i = 0; i < THEME_COUNT; i++) {
                        const D2DTheme& t = THEMES[i];
                        int col = t.isDark ? 1 : 0;
                        int row = t.isDark ? (i - 5) : i;

                        float cardX = panelX + 20 + col * (cardWidth + 20);
                        float cardY = gridStartY + row * cardHeight;
                        float innerX = cardX + cardPadding;
                        float innerY = cardY + cardPadding;
                        float innerW = cardWidth - cardPadding * 2;
                        float innerH = cardHeight - cardPadding * 2;

                        if (clickX >= innerX && clickX <= innerX + innerW &&
                            clickY >= innerY && clickY <= innerY + innerH) {
                            clickedTheme = i;
                            break;
                        }
                    }

                    if (clickedTheme >= 0) {
                        applyTheme(*app, clickedTheme);
                        app->showThemeChooser = false;
                        app->themeChooserAnimation = 0;
                    }
                    // If clicked outside themes, just close the chooser
                    else {
                        app->showThemeChooser = false;
                        app->themeChooserAnimation = 0;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                if (app->scrollbarDragging) {
                    app->scrollbarDragging = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (app->hScrollbarDragging) {
                    app->hScrollbarDragging = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (app->selecting) {
                    // Finalize selection based on mode
                    if (app->selectionMode == App::SelectionMode::Word ||
                        app->selectionMode == App::SelectionMode::Line) {
                        // Word/Line selection: keep the bounds set during mouse down/move
                        // hasSelection was already set to true in WM_LBUTTONDOWN
                    } else {
                        // Normal selection: finalize with current mouse position (document coordinates)
                        app->selEndX = app->mouseX;
                        app->selEndY = (int)(app->mouseY + app->scrollY);

                        // Check if this was a meaningful drag (not just a click)
                        // Use screen coordinates stored from mouse down
                        int dx = abs(app->mouseX - app->lastClickX);
                        int dy = abs(app->mouseY - app->lastClickY);
                        if (dx > 5 || dy > 5) {
                            app->hasSelection = true;
                        } else if (!app->hoveredLink.empty()) {
                            // It was just a click on a link
                            openUrl(app->hoveredLink);
                            app->hasSelection = false;
                        } else {
                            app->hasSelection = false;
                        }
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (!app->hoveredLink.empty()) {
                    // Click on link
                    openUrl(app->hoveredLink);
                }

                app->mouseDown = false;
                app->selecting = false;
            }
            return 0;

        case WM_SETCURSOR:
            if (app && LOWORD(lParam) == HTCLIENT) {
                // We handle cursor in WM_MOUSEMOVE
                return TRUE;
            }
            break;

        case WM_KEYDOWN:
            if (app) {
                float pageSize = app->height * 0.8f;
                float maxScroll = std::max(0.0f, app->contentHeight - app->height);
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

                // Handle search-specific keys when search is active
                if (app->showSearch && app->searchActive) {
                    switch (wParam) {
                        case VK_ESCAPE:
                            // Close search
                            app->showSearch = false;
                            app->searchActive = false;
                            app->searchQuery.clear();
                            app->searchMatches.clear();
                            app->searchAnimation = 0;
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        case VK_RETURN:
                            // Cycle to next match
                            if (!app->searchMatches.empty()) {
                                app->searchCurrentIndex = (app->searchCurrentIndex + 1) % (int)app->searchMatches.size();
                                scrollToCurrentMatch(*app);
                                InvalidateRect(hwnd, nullptr, FALSE);
                            }
                            return 0;
                        case VK_BACK:
                            // Delete last character
                            if (!app->searchQuery.empty()) {
                                app->searchQuery.pop_back();
                                performSearch(*app);
                                if (!app->searchMatches.empty()) {
                                    scrollToCurrentMatch(*app);
                                }
                                InvalidateRect(hwnd, nullptr, FALSE);
                            }
                            return 0;
                    }
                }

                if (ctrl) {
                    switch (wParam) {
                        case 'A': {
                            // Select All - extract all text from document
                            if (app->root) {
                                app->selectedText.clear();
                                extractText(app->root, app->selectedText);
                                app->hasSelection = true;
                            }
                            break;
                        }
                        case 'C': {
                            // Copy - copy selected text or all text if select all was used
                            bool copied = false;
                            if (app->hasSelection && !app->selectedText.empty()) {
                                copyToClipboard(hwnd, app->selectedText);
                                app->hasSelection = false;
                                app->selectedText.clear();
                                copied = true;
                            } else if (app->root) {
                                // If no selection, copy all
                                std::wstring allText;
                                extractText(app->root, allText);
                                copyToClipboard(hwnd, allText);
                                copied = true;
                            }
                            // Show "Copied!" notification
                            if (copied) {
                                app->showCopiedNotification = true;
                                app->copiedNotificationAlpha = 1.0f;
                                app->copiedNotificationStart = std::chrono::steady_clock::now();
                                InvalidateRect(hwnd, nullptr, FALSE);
                            }
                            break;
                        }
                        case 'F':
                            // Ctrl+F to open search
                            if (!app->showSearch) {
                                app->showSearch = true;
                                app->searchActive = true;
                                app->searchAnimation = 0;
                                app->searchQuery.clear();
                                app->searchMatches.clear();
                                app->searchCurrentIndex = 0;
                                app->searchJustOpened = true;
                            }
                            InvalidateRect(hwnd, nullptr, FALSE);
                            break;
                    }
                } else {
                    switch (wParam) {
                        case VK_ESCAPE:
                            // Priority: Search > Theme chooser > Quit
                            if (app->showSearch) {
                                app->showSearch = false;
                                app->searchActive = false;
                                app->searchQuery.clear();
                                app->searchMatches.clear();
                                app->searchAnimation = 0;
                            } else if (app->showThemeChooser) {
                                app->showThemeChooser = false;
                                app->themeChooserAnimation = 0;
                            } else {
                                PostQuitMessage(0);
                            }
                            break;
                        case 'Q':
                            if (!app->showThemeChooser && !app->showSearch) {
                                PostQuitMessage(0);
                            }
                            break;
                        case 'T':
                            if (!app->showSearch) {
                                app->showThemeChooser = !app->showThemeChooser;
                                if (app->showThemeChooser) {
                                    app->themeChooserAnimation = 0;
                                }
                            }
                            InvalidateRect(hwnd, nullptr, FALSE);
                            break;
                        case 'F':
                            // F to open search (when not in search mode)
                            if (!app->showSearch && !app->showThemeChooser) {
                                app->showSearch = true;
                                app->searchActive = true;
                                app->searchAnimation = 0;
                                app->searchQuery.clear();
                                app->searchMatches.clear();
                                app->searchCurrentIndex = 0;
                                app->searchJustOpened = true;
                                InvalidateRect(hwnd, nullptr, FALSE);
                            }
                            break;
                        case VK_UP:
                        case 'K':
                            if (!app->showSearch) {
                                app->targetScrollY -= 50;
                            }
                            break;
                        case VK_DOWN:
                        case 'J':
                            if (!app->showSearch) {
                                app->targetScrollY += 50;
                            }
                            break;
                        case VK_PRIOR: // Page Up
                            app->targetScrollY -= pageSize;
                            break;
                        case VK_NEXT: // Page Down
                        case VK_SPACE:
                            if (!app->showSearch) {
                                app->targetScrollY += pageSize;
                            }
                            break;
                        case VK_HOME:
                            app->targetScrollY = 0;
                            break;
                        case VK_END:
                            app->targetScrollY = maxScroll;
                            break;
                        case 'S':
                            if (!app->showSearch) {
                                app->showStats = !app->showStats;
                            }
                            break;
                    }
                }

                app->targetScrollY = std::max(0.0f, std::min(app->targetScrollY, maxScroll));
                app->scrollY = app->targetScrollY;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_CHAR:
            if (app && app->showSearch && app->searchActive) {
                // Skip the character that opened search (F key)
                if (app->searchJustOpened) {
                    app->searchJustOpened = false;
                    return 0;
                }
                wchar_t ch = (wchar_t)wParam;
                // Only handle printable characters (not control chars)
                if (ch >= 32 && ch != 127) {
                    app->searchQuery += ch;
                    performSearch(*app);
                    if (!app->searchMatches.empty()) {
                        app->searchCurrentIndex = 0;
                        scrollToCurrentMatch(*app);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_DROPFILES:
            if (app) {
                HDROP hDrop = (HDROP)wParam;
                char path[MAX_PATH];
                if (DragQueryFileA(hDrop, 0, path, MAX_PATH)) {
                    std::string filepath = path;
                    size_t dotPos = filepath.rfind('.');
                    if (dotPos != std::string::npos) {
                        std::string ext = filepath.substr(dotPos);
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".md" || ext == ".markdown" || ext == ".txt") {
                            // Load file
                            std::ifstream file(filepath);
                            if (file) {
                                std::stringstream buffer;
                                buffer << file.rdbuf();
                                auto result = app->parser.parse(buffer.str());
                                if (result.success) {
                                    app->root = result.root;
                                    app->parseTimeUs = result.parseTimeUs;
                                    app->currentFile = filepath;
                                    app->scrollY = 0;
                                    app->targetScrollY = 0;
                                    app->contentHeight = 0;
                                }
                            }
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                    }
                }
                DragFinish(hDrop);
            }
            return 0;

        case WM_DESTROY:
            {
                // Load existing settings to preserve values like hasAskedFileAssociation
                Settings settings = loadSettings();
                settings.themeIndex = app->currentThemeIndex;
                settings.zoomFactor = app->zoomFactor;

                // Get window placement for position/size/maximized state
                WINDOWPLACEMENT wp = {};
                wp.length = sizeof(wp);
                if (GetWindowPlacement(hwnd, &wp)) {
                    settings.windowMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);
                    // Save the restored (non-maximized) position
                    settings.windowX = wp.rcNormalPosition.left;
                    settings.windowY = wp.rcNormalPosition.top;
                    settings.windowWidth = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
                    settings.windowHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
                }

                saveSettings(settings);
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static const char* sampleMarkdown = R"(# Welcome to Tinta

**Tinta** is a fast, lightweight markdown reader for Windows.

## Features

- Lightning-fast startup with Direct2D
- Hardware-accelerated text rendering via DirectWrite
- Minimal dependencies
- Small binary size

## Code Example

```cpp
int main() {
    printf("Hello, World!\n");
    return 0;
}
```

## Keyboard Shortcuts

- **F** or **Ctrl+F** - Open search
- **T** - Open theme chooser
- **S** - Toggle stats overlay
- **Ctrl+C** - Copy text
- **Ctrl+A** - Select all
- **Q** or **ESC** - Quit
)";

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    // Enable per-monitor DPI V2 awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto startupStart = Clock::now();

    App app;
    auto t0 = startupStart;
    g_app = &app;

    // Load saved settings
    Settings savedSettings = loadSettings();
    app.currentThemeIndex = savedSettings.themeIndex;
    app.theme = THEMES[savedSettings.themeIndex];
    app.darkMode = app.theme.isDark;
    app.zoomFactor = savedSettings.zoomFactor;

    // Parse command line
    std::string inputFile;
    bool lightMode = false;
    bool forceRegister = false;

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];
        if (arg == L"-l" || arg == L"--light") {
            lightMode = true;
        } else if (arg == L"-s" || arg == L"--stats") {
            app.showStats = true;
        } else if (arg == L"/register" || arg == L"--register") {
            forceRegister = true;
        } else if (arg[0] != L'-' && arg[0] != L'/') {
            // Convert to UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, nullptr, 0, nullptr, nullptr);
            inputFile.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, arg.c_str(), -1, &inputFile[0], len, nullptr, nullptr);
        }
    }
    LocalFree(argv);

    // Handle /register command
    if (forceRegister) {
        if (registerFileAssociation()) {
            MessageBoxW(nullptr,
                       L"Tinta has been registered.\n\n"
                       L"In the Settings window that opens:\n"
                       L"1. Search for '.md'\n"
                       L"2. Click on the current default app\n"
                       L"3. Select 'Tinta' from the list",
                       L"Almost done!", MB_OK | MB_ICONINFORMATION);
            openDefaultAppsSettings();
        } else {
            MessageBoxW(nullptr, L"Failed to register file association. Try running as administrator.",
                       L"Error", MB_OK | MB_ICONWARNING);
        }
        return 0;  // Exit after registering
    }

    // Ask about file association on first run
    askAndRegisterFileAssociation(savedSettings);

    if (lightMode) {
        app.currentThemeIndex = 0;  // Paper (first light theme)
        app.theme = THEMES[0];
        app.darkMode = false;
    }

    // Create window with saved position/size
    t0 = Clock::now();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, L"IDI_ICON1");
    wc.hIconSm = LoadIconW(hInstance, L"IDI_ICON1");
    wc.lpszClassName = L"Tinta";
    RegisterClassExW(&wc);

    app.hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"Tinta",
        L"Tinta",
        WS_OVERLAPPEDWINDOW,
        savedSettings.windowX, savedSettings.windowY,
        savedSettings.windowWidth, savedSettings.windowHeight,
        nullptr, nullptr, hInstance, nullptr
    );

    app.metrics.windowInitUs = usElapsed(t0);

    // Get DPI using per-monitor aware API
    app.contentScale = GetDpiForWindow(app.hwnd) / 96.0f;

    // Initialize D2D
    if (!initD2D(app)) {
        MessageBoxW(nullptr, L"Failed to initialize Direct2D", L"Error", MB_OK);
        return 1;
    }

    // Create text formats and typography
    updateTextFormats(app);
    createTypography(app);

    // Get window size
    RECT rc;
    GetClientRect(app.hwnd, &rc);
    app.width = rc.right - rc.left;
    app.height = rc.bottom - rc.top;

    // Create render target
    t0 = Clock::now();
    if (!createRenderTarget(app)) {
        MessageBoxW(nullptr, L"Failed to create render target", L"Error", MB_OK);
        return 1;
    }
    app.metrics.renderTargetUs = usElapsed(t0);

    // Load document
    t0 = Clock::now();

    auto loadMarkdown = [&](const std::string& content) {
        auto result = app.parser.parse(content);
        if (result.success) {
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
        }
    };

    auto loadFile = [&](const std::string& path) -> bool {
        std::ifstream file(path);
        if (!file) return false;
        std::stringstream buffer;
        buffer << file.rdbuf();
        loadMarkdown(buffer.str());
        return true;
    };

    if (!inputFile.empty()) {
        loadFile(inputFile);
        app.currentFile = inputFile;
    } else {
        // Try syntax.md
        if (loadFile("syntax.md")) {
            app.currentFile = "syntax.md";
        } else {
            loadMarkdown(sampleMarkdown);
        }
    }

    app.metrics.fileLoadUs = usElapsed(t0);

    // Show window (respect saved maximized state)
    t0 = Clock::now();
    if (savedSettings.windowMaximized) {
        ShowWindow(app.hwnd, SW_SHOWMAXIMIZED);
    } else {
        ShowWindow(app.hwnd, nCmdShow);
    }
    UpdateWindow(app.hwnd);
    app.metrics.showWindowUs = usElapsed(t0);

    app.metrics.totalStartupUs = usElapsed(startupStart);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_app = nullptr;
    return (int)msg.wParam;
}
