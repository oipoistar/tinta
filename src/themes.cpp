#include "app.h"

// 10 Distinctive Themes - 5 Light, 5 Dark
const D2DTheme THEMES[] = {
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
        hexColor(0x9932CC),    // syntaxType - dark orchid
        hexColor(0x8F08C4)     // syntaxControlFlow - VS purple
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
        hexColor(0x8B5570),    // syntaxType - plum
        hexColor(0x9B30FF)     // syntaxControlFlow - purple
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
        hexColor(0x6B5B95),    // syntaxType - purple
        hexColor(0x8B008B)     // syntaxControlFlow - dark magenta
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
        hexColor(0x6B5B95),    // syntaxType - purple
        hexColor(0x8F08C4)     // syntaxControlFlow - purple
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
        hexColor(0x8B4513),    // syntaxType - saddle brown
        hexColor(0x8B008B)     // syntaxControlFlow - dark magenta
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
        hexColor(0xDDA0DD),    // syntaxType - plum
        hexColor(0xDA70D6)     // syntaxControlFlow - orchid
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
        hexColor(0x8BE9FD),    // syntaxType - cyan
        hexColor(0xBD93F9)     // syntaxControlFlow - purple
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
        hexColor(0x81E6D9),    // syntaxType - mint
        hexColor(0xE6A0FF)     // syntaxControlFlow - lavender
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
        hexColor(0xDDA0DD),    // syntaxType - plum
        hexColor(0xFF69B4)     // syntaxControlFlow - hot pink
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
        hexColor(0xFFFF00),    // syntaxType - yellow
        hexColor(0xFF00FF)     // syntaxControlFlow - magenta
    }
};

const int THEME_COUNT = sizeof(THEMES) / sizeof(THEMES[0]);

// ---- Dynamic theme registry (#82) ----
//
// User themes live in %APPDATA%\Tinta\themes.ini as repeated [theme]
// sections. Every field the editor writes is optional when hand-authored;
// missing colors fall back to Paper. Example:
//   [theme]
//   name=Nordish
//   dark=1
//   font=Segoe UI
//   codefont=Cascadia Mono
//   background=2E3440
//   text=D8DEE9
//   ...

#include <shlobj.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace {

std::vector<std::unique_ptr<CustomTheme>> g_customThemes;

std::wstring themesIniPath() {
    wchar_t appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return L"";
    }
    std::wstring path = appData;
    path += L"\\Tinta";
    CreateDirectoryW(path.c_str(), nullptr);
    return path + L"\\themes.ini";
}

bool parseHexColor(const std::string& value, D2D1_COLOR_F& out) {
    std::string s = value;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() != 6) return false;
    unsigned v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return false;
    }
    out = hexColor(v);
    return true;
}

std::string colorToHex(const D2D1_COLOR_F& c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02X%02X%02X",
             (int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f),
             (int)(c.b * 255.0f + 0.5f));
    return buf;
}

std::wstring utf8ToWideTheme(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len > 0 ? len - 1 : 0, L'\0');
    if (len > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    return w;
}

std::string wideToUtf8Theme(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len > 0 ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    return s;
}

// Repoint the D2DTheme string members at the owned wstrings. Must be called
// whenever the strings change (they are the backing storage).
void bindStrings(CustomTheme& ct) {
    ct.theme.name = ct.name.c_str();
    ct.theme.fontFamily = ct.fontFamily.c_str();
    ct.theme.codeFontFamily = ct.codeFontFamily.c_str();
}

void applyThemeKey(CustomTheme& ct, const std::string& key, const std::string& value) {
    D2DTheme& t = ct.theme;
    if (key == "name") ct.name = utf8ToWideTheme(value);
    else if (key == "font") ct.fontFamily = utf8ToWideTheme(value);
    else if (key == "codefont") ct.codeFontFamily = utf8ToWideTheme(value);
    else if (key == "dark") t.isDark = (value == "1");
    else if (key == "background") parseHexColor(value, t.background);
    else if (key == "text") parseHexColor(value, t.text);
    else if (key == "heading") parseHexColor(value, t.heading);
    else if (key == "link") parseHexColor(value, t.link);
    else if (key == "code") parseHexColor(value, t.code);
    else if (key == "codebackground") parseHexColor(value, t.codeBackground);
    else if (key == "blockquoteborder") parseHexColor(value, t.blockquoteBorder);
    else if (key == "accent") parseHexColor(value, t.accent);
    else if (key == "syntaxkeyword") parseHexColor(value, t.syntaxKeyword);
    else if (key == "syntaxstring") parseHexColor(value, t.syntaxString);
    else if (key == "syntaxcomment") parseHexColor(value, t.syntaxComment);
    else if (key == "syntaxnumber") parseHexColor(value, t.syntaxNumber);
    else if (key == "syntaxfunction") parseHexColor(value, t.syntaxFunction);
    else if (key == "syntaxtype") parseHexColor(value, t.syntaxType);
    else if (key == "syntaxcontrolflow") parseHexColor(value, t.syntaxControlFlow);
}

void writeThemesIni() {
    std::wstring path = themesIniPath();
    if (path.empty()) return;
    std::ofstream file(path);
    if (!file) return;
    file << "; User themes. Colors are RRGGBB hex. Restart not required for\n";
    file << "; themes saved from the editor; hand edits load at next launch.\n";
    for (const auto& ct : g_customThemes) {
        const D2DTheme& t = ct->theme;
        file << "\n[theme]\n";
        file << "name=" << wideToUtf8Theme(ct->name) << "\n";
        file << "dark=" << (t.isDark ? 1 : 0) << "\n";
        file << "font=" << wideToUtf8Theme(ct->fontFamily) << "\n";
        file << "codefont=" << wideToUtf8Theme(ct->codeFontFamily) << "\n";
        file << "background=" << colorToHex(t.background) << "\n";
        file << "text=" << colorToHex(t.text) << "\n";
        file << "heading=" << colorToHex(t.heading) << "\n";
        file << "link=" << colorToHex(t.link) << "\n";
        file << "code=" << colorToHex(t.code) << "\n";
        file << "codebackground=" << colorToHex(t.codeBackground) << "\n";
        file << "blockquoteborder=" << colorToHex(t.blockquoteBorder) << "\n";
        file << "accent=" << colorToHex(t.accent) << "\n";
        file << "syntaxkeyword=" << colorToHex(t.syntaxKeyword) << "\n";
        file << "syntaxstring=" << colorToHex(t.syntaxString) << "\n";
        file << "syntaxcomment=" << colorToHex(t.syntaxComment) << "\n";
        file << "syntaxnumber=" << colorToHex(t.syntaxNumber) << "\n";
        file << "syntaxfunction=" << colorToHex(t.syntaxFunction) << "\n";
        file << "syntaxtype=" << colorToHex(t.syntaxType) << "\n";
        file << "syntaxcontrolflow=" << colorToHex(t.syntaxControlFlow) << "\n";
    }
}

} // namespace

int themeCount() {
    return THEME_COUNT + (int)g_customThemes.size();
}

const D2DTheme& themeAt(int index) {
    if (index >= 0 && index < THEME_COUNT) return THEMES[index];
    int custom = index - THEME_COUNT;
    if (custom >= 0 && custom < (int)g_customThemes.size()) {
        return g_customThemes[custom]->theme;
    }
    return THEMES[0];
}

void loadCustomThemes() {
    g_customThemes.clear();
    std::wstring path = themesIniPath();
    if (path.empty()) return;
    std::ifstream file(path);
    if (!file) return;

    std::unique_ptr<CustomTheme> current;
    auto finalize = [&]() {
        if (current && !current->name.empty()) {
            bindStrings(*current);
            g_customThemes.push_back(std::move(current));
        }
        current.reset();
    };

    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') {
            finalize();
            current = std::make_unique<CustomTheme>();
            current->theme = THEMES[0];  // colors default to Paper
            current->fontFamily = L"Segoe UI";
            current->codeFontFamily = L"Consolas";
            continue;
        }
        if (!current) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        applyThemeKey(*current, line.substr(0, eq), line.substr(eq + 1));
    }
    finalize();
}

int saveCustomTheme(const D2DTheme& t, const std::wstring& name,
                    const std::wstring& fontFamily,
                    const std::wstring& codeFontFamily) {
    if (name.empty()) return -1;
    CustomTheme* slot = nullptr;
    int index = -1;
    for (size_t i = 0; i < g_customThemes.size(); i++) {
        if (_wcsicmp(g_customThemes[i]->name.c_str(), name.c_str()) == 0) {
            slot = g_customThemes[i].get();
            index = THEME_COUNT + (int)i;
            break;
        }
    }
    if (!slot) {
        g_customThemes.push_back(std::make_unique<CustomTheme>());
        slot = g_customThemes.back().get();
        index = themeCount() - 1;
    }
    slot->name = name;
    slot->fontFamily = fontFamily.empty() ? L"Segoe UI" : fontFamily;
    slot->codeFontFamily = codeFontFamily.empty() ? L"Consolas" : codeFontFamily;
    slot->theme = t;
    bindStrings(*slot);
    writeThemesIni();
    return index;
}
