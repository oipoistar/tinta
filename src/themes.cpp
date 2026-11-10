#include "app.h"

#include "themes_css.h"

#include <mutex>

// The ten built-in themes, five light and five dark. Each is a CSS file
// under resources/themes, compiled into the executable as text
// (CMakeLists.txt) and read into the theme Tinta draws with when it is
// first asked for: a sheet read from nothing, so all a built-in theme is
// stands in its file
namespace {
struct BuiltinSheet {
    const wchar_t* name;
    const char* css;
};
const BuiltinSheet kBuiltinSheets[] = {
#include "builtin_themes.inc"
};
}  // namespace

const int THEME_COUNT = (int)(sizeof(kBuiltinSheets) / sizeof(kBuiltinSheets[0]));

namespace {
struct BuiltinMade {
    std::once_flag once;
    CustomTheme theme;
    std::shared_ptr<const css::Sheet> sheet;  // its rules for single elements, for the page (page_style.h)
};
BuiltinMade& builtinMade(int index) {
    static BuiltinMade made[sizeof(kBuiltinSheets) / sizeof(kBuiltinSheets[0])];
    return made[index < 0 || index >= THEME_COUNT ? 0 : index];
}
}  // namespace

const D2DTheme& builtinTheme(int index) {
    BuiltinMade& m = builtinMade(index);
    if (index < 0 || index >= THEME_COUNT) index = 0;
    std::call_once(m.once, [&] {
        const D2DTheme blank{};
        const css::ImportSource none = [](std::string_view, std::string_view, std::string&, std::string&) { return false; };
        m.sheet = compileCssTheme(kBuiltinSheets[index].css, std::string(), false, none, m.theme, &blank).sheet;
        m.theme.name = kBuiltinSheets[index].name;
        D2DTheme& t = m.theme.theme;
        t.name = m.theme.name.c_str();
        t.fontFamily = m.theme.fontFamily.c_str();
        // None of its own inherits the main font (themeHeadingFont)
        t.headingFontFamily = m.theme.headingFontFamily.empty() ? nullptr : m.theme.headingFontFamily.c_str();
        t.codeFontFamily = m.theme.codeFontFamily.c_str();
    });
    return m.theme.theme;
}

// A built-in theme's sheet as written, for a theme that starts from it
// with @import "Paper"; null for a name that is none of them
const char* builtinThemeCss(const std::wstring& name) {
    for (const BuiltinSheet& sheet : kBuiltinSheets) {
        if (_wcsicmp(sheet.name, name.c_str()) == 0) return sheet.css;
    }
    return nullptr;
}

// ---- The window's colours (--ui-*, docs/specs/theming.md) ----

D2D1_COLOR_F themeUiText(const D2DTheme& theme) { return theme.uiText.value_or(theme.text); }
D2D1_COLOR_F themeUiAccent(const D2DTheme& theme) { return theme.uiAccent.value_or(theme.accent); }
D2D1_COLOR_F themeUiHeading(const D2DTheme& theme) { return theme.uiText.value_or(theme.heading); }

D2D1_COLOR_F themeUiPanel(const D2DTheme& theme, D2D1_COLOR_F own) {
    if (!theme.uiPanel) return own;
    D2D1_COLOR_F color = *theme.uiPanel;
    color.a *= own.a;
    return color;
}

D2D1_COLOR_F themeUiBorder(const D2DTheme& theme, D2D1_COLOR_F own, float fade) {
    if (!theme.uiBorder) return own;
    D2D1_COLOR_F color = *theme.uiBorder;
    color.a *= fade;
    return color;
}

bool themeColorIsDark(const D2D1_COLOR_F& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b < 0.5f;
}

bool themeUiDark(const D2DTheme& theme) {
    return theme.uiPanel ? themeColorIsDark(*theme.uiPanel) : theme.isDark;
}

// ---- Dynamic theme registry (#82) ----
//
// User themes live in %APPDATA%\Tinta\themes.ini as repeated [theme]
// sections. Every field the editor writes is optional when hand-authored;
// missing colors fall back to Paper. Example:
//   [theme]
//   name=Nordish
//   dark=1
//   font=Segoe UI
//   headingfont=Segoe UI    ; optional, headings inherit font= when absent
//
// Lines read as a person means them (#310): spaces around keys and values,
// keys in any case, and a note after a value, from a ';' or in parentheses
// as the template's examples carry, are all fine. A change to the file
// applies while Tinta runs (customThemesChangedOnDisk).
//   codefont=Cascadia Mono
//   background=2E3440
//   text=D8DEE9
//   ...

#include "settings.h"
#include "themes_css.h"

#include <shlobj.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace {

std::vector<std::unique_ptr<CustomTheme>> g_customThemes;

std::wstring themesIniPath() {
    // Follows the config home: portable beside the exe, else %APPDATA%
    std::wstring dir = tintaConfigDir();
    if (dir.empty()) return L"";
    return dir + L"\\themes.ini";
}

// themes.ini's time when it was last read or written here; zero while the
// file is missing
FILETIME g_themesIniTime{};

FILETIME themesIniTimeNow() {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    const std::wstring path = themesIniPath();
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return FILETIME{};
    return data.ftLastWriteTime;
}

std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// A line as a person means it (#310): key and value without the spaces
// around them, the key in any case, and a note after the value dropped,
// from a ';' or in parentheses, as the template's examples carry their
// notes: an example uncommented loads. A theme's name keeps all but the
// spaces at its ends.
void readThemeLine(const std::string& line, size_t eq, std::string& key, std::string& value) {
    key = trimmed(line.substr(0, eq));
    for (char& c : key) c = (char)tolower((unsigned char)c);
    value = trimmed(line.substr(eq + 1));
    if (key == "name") return;
    for (size_t i = 1; i < value.size(); ++i) {
        if (value[i] == '(' && isspace((unsigned char)value[i - 1])) {
            value.erase(i);
            break;
        }
    }
    const size_t note = value.find(';');
    if (note != std::string::npos) value.erase(note);
    value = trimmed(value);
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
    // Empty means "inherit the main font" - themeHeadingFont resolves it
    ct.theme.headingFontFamily = ct.headingFontFamily.c_str();
    ct.theme.codeFontFamily = ct.codeFontFamily.c_str();
}

void applyThemeKey(CustomTheme& ct, const std::string& key, const std::string& value) {
    D2DTheme& t = ct.theme;
    if (key == "name") ct.name = utf8ToWideTheme(value);
    else if (key == "font") ct.fontFamily = utf8ToWideTheme(value);
    else if (key == "headingfont") ct.headingFontFamily = utf8ToWideTheme(value);
    else if (key == "codefont") ct.codeFontFamily = utf8ToWideTheme(value);
    else if (key == "dark") t.isDark = (value == "1");
    else if (key == "background") parseHexColor(value, t.background);
    else if (key == "text") parseHexColor(value, t.text);
    else if (key == "heading") parseHexColor(value, t.heading);
    else if (key.size() == 2 && key[0] == 'h' && key[1] >= '1' && key[1] <= '6') {
        D2D1_COLOR_F color;
        if (parseHexColor(value, color)) t.headingColors[key[1] - '1'] = color;
    }
    else if (key == "highlightbackground" || key == "highlighttext") {
        D2D1_COLOR_F color;
        if (parseHexColor(value, color))
            (key == "highlightbackground" ? t.highlightBackground : t.highlightText) = color;
    }
    else if (key == "bold" || key == "italic" || key == "bolditalic" || key == "selection" || key == "hr") {
        D2D1_COLOR_F color;
        if (parseHexColor(value, color)) {
            (key == "bold" ? t.boldText : key == "italic" ? t.italicText
             : key == "bolditalic" ? t.boldItalicText : key == "selection" ? t.selection : t.rule) = color;
        }
    }
    else if (key == "link") parseHexColor(value, t.link);
    else if (key == "code") parseHexColor(value, t.code);
    else if (key == "inlinecode") {
        D2D1_COLOR_F color;
        if (parseHexColor(value, color)) t.inlineCode = color;
    }
    else if (key == "codebackground") parseHexColor(value, t.codeBackground);
    else if (key == "inlinecodebackground") {
        // A colour, or none/transparent for inline code without a box (#278)
        std::string word = value;
        for (char& c : word) c = (char)tolower((unsigned char)c);
        D2D1_COLOR_F color;
        if (word == "none" || word == "transparent") t.inlineCodeBackground = D2D1::ColorF(0, 0, 0, 0);
        else if (parseHexColor(value, color)) t.inlineCodeBackground = color;
    }
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
    file << "; User themes. Colors are RRGGBB hex. Saved edits apply at once,\n";
    file << "; whether from the theme editor or by hand.\n";
    file << "; Optional inlinecode overrides code for inline spans only.\n";
    file << "; Optional inlinecodebackground (a colour, or none) overrides\n";
    file << "; codebackground for inline spans only.\n";
    file << "; Optional bold, italic and bolditalic colour emphasised text;\n";
    file << "; selection tints the selection and hr colours horizontal rules.\n";
    file << "; An empty headingfont= sets headings in font=.\n";
    for (const auto& ct : g_customThemes) {
        const D2DTheme& t = ct->theme;
        file << "\n[theme]\n";
        file << "name=" << wideToUtf8Theme(ct->name) << "\n";
        file << "dark=" << (t.isDark ? 1 : 0) << "\n";
        file << "font=" << wideToUtf8Theme(ct->fontFamily) << "\n";
        // Always written, empty while headings use font=, so the key is
        // there to fill in by hand (#256)
        file << "headingfont=" << wideToUtf8Theme(ct->headingFontFamily) << "\n";
        file << "codefont=" << wideToUtf8Theme(ct->codeFontFamily) << "\n";
        file << "background=" << colorToHex(t.background) << "\n";
        file << "text=" << colorToHex(t.text) << "\n";
        file << "heading=" << colorToHex(t.heading) << "\n";
        for (int i = 0; i < 6; ++i)
            if (t.headingColors[i]) file << "h" << i + 1 << "=" << colorToHex(*t.headingColors[i]) << "\n";
        if (t.highlightBackground) file << "highlightbackground=" << colorToHex(*t.highlightBackground) << "\n";
        if (t.highlightText) file << "highlighttext=" << colorToHex(*t.highlightText) << "\n";
        if (t.boldText) file << "bold=" << colorToHex(*t.boldText) << "\n";
        if (t.italicText) file << "italic=" << colorToHex(*t.italicText) << "\n";
        if (t.boldItalicText) file << "bolditalic=" << colorToHex(*t.boldItalicText) << "\n";
        if (t.selection) file << "selection=" << colorToHex(*t.selection) << "\n";
        if (t.rule) file << "hr=" << colorToHex(*t.rule) << "\n";
        file << "link=" << colorToHex(t.link) << "\n";
        file << "code=" << colorToHex(t.code) << "\n";
        if (t.inlineCode) file << "inlinecode=" << colorToHex(*t.inlineCode) << "\n";
        file << "codebackground=" << colorToHex(t.codeBackground) << "\n";
        if (t.inlineCodeBackground) {
            file << "inlinecodebackground="
                 << (t.inlineCodeBackground->a <= 0.0f ? std::string("none")
                                                       : colorToHex(*t.inlineCodeBackground))
                 << "\n";
        }
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
    file.close();
    // Written here: no change to read back
    g_themesIniTime = themesIniTimeNow();
}

} // namespace

bool customThemesChangedOnDisk() {
    const FILETIME now = themesIniTimeNow();
    return CompareFileTime(&now, &g_themesIniTime) != 0;
}

const char* themesIniTemplate() {
    return "; Custom themes: [theme] sections, RRGGBB colors. For example:\n"
           ";\n"
           "; [theme]\n"
           "; name=My theme\n"
           "; dark=0\n"
           "; font=Segoe UI\n"
           "; headingfont=Georgia      (empty: headings use font=)\n"
           "; codefont=Cascadia Mono\n"
           "; background=F7FAFC\n"
           "; text=2D3748\n"
           "; heading=1A365D           (h1= to h6= set single levels)\n"
           "; link=2B6CB0\n"
           "; code=285E61              (inlinecode= for inline spans only)\n"
           "; codebackground=EBF8FF\n"
           "; inlinecodebackground=none  (inline spans only; or RRGGBB)\n"
           "; blockquoteborder=A0C4E8\n"
           "; accent=2B6CB0\n"
           "; highlightbackground=FEF3C7\n"
           "; highlighttext=2D3748\n"
           "; bold=1A365D              (italic=, and bolditalic= for both)\n"
           "; selection=2B6CB0         (drawn as a translucent tint)\n"
           "; hr=A0C4E8                (horizontal rules; else blockquoteborder)\n"
           "; syntaxkeyword=, syntaxstring=, syntaxcomment=, syntaxnumber=,\n"
           "; syntaxfunction=, syntaxtype=, syntaxcontrolflow= color code blocks\n";
}

// ---- CSS themes (docs/specs/theming.md) ----

namespace {

struct CssEntry {
    std::wstring path;
    // The sheet and the files it imports, with their times as read
    std::vector<std::pair<std::wstring, FILETIME>> files;
    std::unique_ptr<CustomTheme> light, dark;  // dark: a sheet that follows Windows
    std::shared_ptr<const css::Sheet> sheet;
    std::vector<css::Diagnostic> diagnostics;
};
std::vector<std::unique_ptr<CssEntry>> g_cssThemes;
// Themes a reload replaced: copies of a D2DTheme may still point into
// their strings (print, the chooser's previews), so they stay for the run
std::vector<std::unique_ptr<CustomTheme>> g_retiredThemes;
bool g_systemDark = false;

bool fileTime(const std::wstring& path, FILETIME& time) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    time = data.ftLastWriteTime;
    return true;
}

bool readUtf8(const std::wstring& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    return true;
}

CssEntry* cssEntryAt(int index) {
    const int css = index - THEME_COUNT - (int)g_customThemes.size();
    return css >= 0 && css < (int)g_cssThemes.size() ? g_cssThemes[css].get() : nullptr;
}

// Reads the entry's sheet into its light theme, and its dark one when the
// sheet follows Windows. @import "Paper" or the name of a themes.ini theme
// starts from that theme; any other import is a file beside the sheet
void readCssTheme(CssEntry& entry) {
    entry.files.clear();
    std::string text;
    FILETIME time{};
    fileTime(entry.path, time);
    entry.files.push_back({entry.path, time});
    const bool readable = readUtf8(entry.path, text);
    const css::ImportSource imports = [&](std::string_view name, std::string_view from, std::string& out,
                                          std::string& file) {
        const std::wstring wanted = utf8ToWideTheme(std::string(name));
        if (const char* builtin = builtinThemeCss(wanted)) {
            out = builtin;
            file = std::string(name);
            return true;
        }
        for (const auto& custom : g_customThemes) {
            if (_wcsicmp(custom->name.c_str(), wanted.c_str()) == 0) {
                out = themeToCss(custom->theme);
                file = std::string(name);
                return true;
            }
        }
        std::wstring base = utf8ToWideTheme(std::string(from));
        size_t slash = base.find_last_of(L"\\/");
        std::wstring path = slash == std::wstring::npos ? wanted : base.substr(0, slash + 1) + wanted;
        if (!readUtf8(path, out)) return false;
        FILETIME imported{};
        fileTime(path, imported);
        entry.files.push_back({path, imported});
        file = wideToUtf8Theme(path);
        return true;
    };
    std::wstring name = entry.path.substr(entry.path.find_last_of(L"\\/") + 1);
    if (name.size() > 4) name.resize(name.size() - 4);  // .css
    auto light = std::make_unique<CustomTheme>();
    light->name = name;
    CssTheme compiled = compileCssTheme(readable ? text : std::string(), wideToUtf8Theme(entry.path), false,
                                        imports, *light);
    light->sourcePath = entry.path;
    bindStrings(*light);
    light->theme.sourcePath = light->sourcePath.c_str();
    std::unique_ptr<CustomTheme> dark;
    if (compiled.lightDark) {
        dark = std::make_unique<CustomTheme>();
        dark->name = name;
        compileCssTheme(text, wideToUtf8Theme(entry.path), true, imports, *dark);
        dark->sourcePath = entry.path;
        bindStrings(*dark);
        dark->theme.sourcePath = dark->sourcePath.c_str();
    }
    if (entry.light) g_retiredThemes.push_back(std::move(entry.light));
    if (entry.dark) g_retiredThemes.push_back(std::move(entry.dark));
    entry.light = std::move(light);
    entry.dark = std::move(dark);
    entry.sheet = compiled.sheet;
    entry.diagnostics = std::move(compiled.diagnostics);
    if (!readable) entry.diagnostics.push_back({css::Diagnostic::Error, wideToUtf8Theme(entry.path), 0, 0, "the file cannot be read"});
}

void loadCssThemes() {
    g_cssThemes.clear();
    const std::wstring dir = tintaConfigDir();
    if (dir.empty()) return;
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((dir + L"\\themes\\*.css").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> paths;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) paths.push_back(dir + L"\\themes\\" + found.cFileName);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    std::sort(paths.begin(), paths.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    for (const auto& path : paths) {
        auto entry = std::make_unique<CssEntry>();
        entry->path = path;
        readCssTheme(*entry);
        g_cssThemes.push_back(std::move(entry));
    }
}

}  // namespace

void themesSetSystemDark(bool dark) { g_systemDark = dark; }

bool themeFollowsSystem(int index) {
    const CssEntry* entry = cssEntryAt(index);
    return entry && entry->dark;
}

std::wstring themeSourcePath(int index) {
    const CssEntry* entry = cssEntryAt(index);
    return entry ? entry->path : std::wstring();
}

bool themeReloadIfChanged(int index) {
    CssEntry* entry = cssEntryAt(index);
    if (!entry) return false;
    bool changed = false;
    for (const auto& file : entry->files) {
        FILETIME now{};
        if (!fileTime(file.first, now) || CompareFileTime(&now, &file.second) != 0) changed = true;
    }
    if (changed) readCssTheme(*entry);
    return changed;
}

std::vector<std::wstring> themeProblems(int index) {
    std::vector<std::wstring> problems;
    const CssEntry* entry = cssEntryAt(index);
    if (!entry) return problems;
    for (const auto& d : entry->diagnostics) {
        std::wstring file = utf8ToWideTheme(d.file);
        size_t slash = file.find_last_of(L"\\/");
        if (slash != std::wstring::npos) file = file.substr(slash + 1);
        std::wstring line = file;
        if (d.line > 0) line += L", line " + std::to_wstring(d.line);
        problems.push_back(line + L": " + utf8ToWideTheme(d.message));
    }
    return problems;
}

std::shared_ptr<const css::Sheet> themeSheet(int index) {
    // A built-in theme's rules reach the page as a sheet of one's own does
    if (index >= 0 && index < THEME_COUNT) {
        builtinTheme(index);
        return builtinMade(index).sheet;
    }
    const CssEntry* entry = cssEntryAt(index);
    return entry ? entry->sheet : nullptr;
}

std::string themeSheetCss(int index) {
    const std::shared_ptr<const css::Sheet> sheet = themeSheet(index);
    return sheet ? css::serialize(*sheet) : std::string();
}

int themeCount() {
    return THEME_COUNT + (int)g_customThemes.size() + (int)g_cssThemes.size();
}

const D2DTheme& themeAt(int index) {
    if (index >= 0 && index < THEME_COUNT) return THEMES[index];
    int custom = index - THEME_COUNT;
    if (custom >= 0 && custom < (int)g_customThemes.size()) {
        return g_customThemes[custom]->theme;
    }
    if (const CssEntry* entry = cssEntryAt(index)) {
        return g_systemDark && entry->dark ? entry->dark->theme : entry->light->theme;
    }
    return THEMES[0];
}

static void loadCustomThemesIni();

void loadCustomThemes() {
    loadCustomThemesIni();
    loadCssThemes();
}

static void loadCustomThemesIni() {
    g_customThemes.clear();
    g_themesIniTime = themesIniTimeNow();
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
        line = trimmed(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
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
        std::string key, value;
        readThemeLine(line, eq, key, value);
        applyThemeKey(*current, key, value);
    }
    finalize();
}

int saveCustomTheme(const D2DTheme& t, const std::wstring& name,
                    const std::wstring& fontFamily,
                    const std::wstring& codeFontFamily,
                    const std::wstring& headingFontFamily) {
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
    slot->headingFontFamily = headingFontFamily;  // empty inherits font
    slot->codeFontFamily = codeFontFamily.empty() ? L"Consolas" : codeFontFamily;
    slot->theme = t;
    bindStrings(*slot);
    writeThemesIni();
    return index;
}
