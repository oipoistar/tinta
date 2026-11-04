// CSS themes (docs/specs/theming.md, phase 1): every built-in theme read
// back from its CSS as it was; a sheet's colours, faces and type sizes in
// the theme Tinta draws with; light and dark from one sheet; @import of a
// built-in; problems reported; a sheet in <config>\themes listed, applied,
// followed when saved, and carried by the HTML export.
#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "export.h"
#include "frame_preview.h"
#include "render.h"
#include "themes_css.h"
#include "utils.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
std::string read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()};
}
bool closeTo(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    const float e = 1.5f / 255.0f;
    return std::abs(a.r - b.r) < e && std::abs(a.g - b.g) < e && std::abs(a.b - b.b) < e && std::abs(a.a - b.a) < e;
}
bool sameFace(const wchar_t* a, const wchar_t* b) {
    return std::wstring(a ? a : L"") == std::wstring(b ? b : L"");
}
const css::ImportSource kNoImports = [](std::string_view, std::string_view, std::string&, std::string&) {
    return false;
};
const css::ImportSource kBuiltins = [](std::string_view name, std::string_view, std::string& out, std::string& file) {
    for (int i = 0; i < THEME_COUNT; i++) {
        if (toUtf8(THEMES[i].name) == name) {
            out = themeToCss(THEMES[i]);
            file = std::string(name);
            return true;
        }
    }
    return false;
};

void roundTrips() {
    for (int i = 0; i < THEME_COUNT; i++) {
        const D2DTheme& t = THEMES[i];
        const std::string name = toUtf8(t.name);
        CustomTheme back;
        CssTheme read = compileCssTheme(themeToCss(t), name + ".css", t.isDark, kNoImports, back);
        const D2DTheme& r = back.theme;
        check(read.diagnostics.empty(), name + ": its CSS has no problems");
        check(r.isDark == t.isDark, name + ": dark or light");
        check(closeTo(r.background, t.background) && closeTo(r.text, t.text) && closeTo(r.heading, t.heading) &&
                  closeTo(r.link, t.link) && closeTo(r.code, t.code) && closeTo(r.codeBackground, t.codeBackground) &&
                  closeTo(r.blockquoteBorder, t.blockquoteBorder) && closeTo(r.accent, t.accent),
              name + ": the page, heading, link, code, quote and accent colours come back");
        check(closeTo(r.syntaxKeyword, t.syntaxKeyword) && closeTo(r.syntaxString, t.syntaxString) &&
                  closeTo(r.syntaxComment, t.syntaxComment) && closeTo(r.syntaxNumber, t.syntaxNumber) &&
                  closeTo(r.syntaxFunction, t.syntaxFunction) && closeTo(r.syntaxType, t.syntaxType) &&
                  closeTo(r.syntaxControlFlow, t.syntaxControlFlow),
              name + ": the code token colours come back");
        check(closeTo(themeInlineCodeColor(r), themeInlineCodeColor(t)) &&
                  closeTo(themeInlineCodeBackground(r), themeInlineCodeBackground(t)),
              name + ": inline code looks the same");
        for (int level = 1; level <= 6; level++) {
            check(closeTo(themeHeadingColor(r, level), themeHeadingColor(t, level)), name + ": heading colours");
        }
        check(sameFace(back.fontFamily.c_str(), t.fontFamily) && sameFace(back.codeFontFamily.c_str(), t.codeFontFamily),
              name + ": the faces come back");
        check(sameFace(back.headingFontFamily.empty() ? t.fontFamily : back.headingFontFamily.c_str(), themeHeadingFont(t)),
              name + ": the heading face comes back");
        check(r.bodySize == 0 && r.codeSize == 0 && r.lineHeight == 0 && r.headingSize[0] == 0,
              name + ": and Tinta's own type sizes");
    }
}

void sheets() {
    CustomTheme type;
    CssTheme typed = compileCssTheme(
        "body { font: 17px/1.6 \"No Such Face\", Georgia, serif; color: #222; background: #fbf8f1 }\n"
        "h1 { font-size: 2.2em; font-weight: 600 } h2 { font-style: italic; color: #a33 }\n"
        "pre, code { font-family: Consolas, monospace; font-size: 0.875rem }\n"
        "blockquote { border-left: 3px solid #b35900 } mark { background: #ffe08a }\n"
        "::selection { background: rgb(51 102 204 / 0.3) } hr { border-top: 1px solid #ccc }\n"
        ".token.keyword { color: oklch(55% 0.2 300) }\n",
        "type.css", false, kNoImports, type);
    const D2DTheme& t = type.theme;
    check(typed.diagnostics.empty(), "a clean sheet has no problems");
    check(std::abs(t.bodySize - 17) < 0.01f && std::abs(t.lineHeight - 1.6f) < 0.01f, "font: sizes the body and its line height");
    check(type.fontFamily == L"Georgia", "the first face this PC has is the body's");
    check(std::abs(t.headingSize[0] - 37.4f) < 0.05f && t.headingWeight[0] == 600, "h1's size in em and weight");
    check(t.headingItalic[1] && t.headingColors[1] && closeTo(*t.headingColors[1], hexColor(0xAA3333)),
          "h2 is italic and red");
    check(closeTo(t.text, hexColor(0x222222)) && closeTo(t.background, hexColor(0xFBF8F1)) && !t.isDark,
          "the body's colours, a light theme");
    check(std::abs(t.codeSize - 14) < 0.01f && type.codeFontFamily == L"Consolas", "code at 0.875rem in Consolas");
    check(closeTo(t.blockquoteBorder, hexColor(0xB35900)) && t.highlightBackground &&
              closeTo(*t.highlightBackground, hexColor(0xFFE08A)),
          "the quote bar and the mark");
    check(t.selection && t.rule && closeTo(*t.rule, hexColor(0xCCCCCC)), "the selection and the rule");

    const std::string both = ":root { color-scheme: light dark }\n"
                             "body { color: light-dark(#111, #eee); background: light-dark(#fff, #121212) }\n";
    CustomTheme light, dark;
    CssTheme readLight = compileCssTheme(both, "both.css", false, kNoImports, light);
    compileCssTheme(both, "both.css", true, kNoImports, dark);
    check(readLight.lightDark, "color-scheme: light dark follows Windows");
    check(!light.theme.isDark && closeTo(light.theme.background, hexColor(0xFFFFFF)) &&
              dark.theme.isDark && closeTo(dark.theme.background, hexColor(0x121212)) &&
              closeTo(dark.theme.text, hexColor(0xEEEEEE)),
          "light-dark() gives each its colours");

    CustomTheme imported;
    compileCssTheme("@import \"Midnight\";\na { color: #ff8800 }\n", "imp.css", false, kBuiltins, imported);
    check(imported.theme.isDark && closeTo(imported.theme.background, THEMES[5].background) &&
              closeTo(imported.theme.link, hexColor(0xFF8800)),
          "@import \"Midnight\" starts from Midnight");

    CustomTheme wrong;
    CssTheme mistakes = compileCssTheme("body { display: flex; colour: red; color: #123456 }\n", "wrong.css", false,
                                        kNoImports, wrong);
    bool display = false, colour = false;
    for (const auto& d : mistakes.diagnostics) {
        display |= d.line == 1 && d.message.find("display") != std::string::npos;
        colour |= d.message.find("colour") != std::string::npos && d.message.find("color") != std::string::npos;
    }
    check(display && colour, "display and a misspelt colour are reported, on their line");
    check(closeTo(wrong.theme.text, hexColor(0x123456)), "and the rest of the sheet applies");
}

void installed(App& app, const fs::path& dir) {
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    const fs::path sheet = themes / "Ink.css";
    { std::ofstream(sheet, std::ios::binary) << "@import \"Paper\";\nbody { color: #1a2b3c; font-size: 18px }\nh1 { color: #aa0022 }\n"; }
    const int before = themeCount();
    loadCustomThemes();
    check(themeCount() == before + 1, "a sheet in themes\\ joins the list");
    int ink = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"Ink") ink = i;
    }
    check(ink >= 0 && themeSourcePath(ink) == sheet.wstring(), "named by its file, with its path");
    if (ink < 0) return;
    applyTheme(app, ink);
    check(closeTo(app.theme.text, hexColor(0x1A2B3C)) && app.textFormat &&
              std::abs(app.textFormat->GetFontSize() - 18 * app.contentScale * app.zoomFactor) < 0.01f,
          "applied, it colours and sizes the body");
    app.root = app.parser.parse("# Heading_one\n\nA paragraph.\n").root;
    layoutDocument(app);
    bool red = false;
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && app.docText.substr(run.docStart, run.docLength) == L"Heading_one") red = closeTo(run.color, hexColor(0xAA0022));
    }
    check(red, "the page draws h1 in the sheet's colour");
    check(exportHtmlFile(app, (dir / "ink.html").wstring()), "HTML export succeeds");
    const std::string html = read(dir / "ink.html");
    check(html.find("#1a2b3c") != std::string::npos && html.find("#aa0022") != std::string::npos,
          "the export carries the sheet");
    check(!themeReloadIfChanged(ink), "an unchanged sheet is left alone");
    { std::ofstream(sheet, std::ios::binary) << "@import \"Paper\";\nbody { color: #445566; display: grid }\n"; }
    FILETIME later{};
    GetSystemTimeAsFileTime(&later);
    HANDLE file = CreateFileW(sheet.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        SetFileTime(file, nullptr, nullptr, &later);
        CloseHandle(file);
    }
    check(themeReloadIfChanged(ink), "a saved sheet is read again");
    applyTheme(app, ink);
    check(closeTo(app.theme.text, hexColor(0x445566)), "and its new colour applies");
    check(!themeProblems(ink).empty() && themeProblems(ink)[0].find(L"Ink.css, line 2") != std::wstring::npos,
          "its problem names the file and the line");
    fs::remove(sheet);
    loadCustomThemes();
    applyTheme(app, 0);
}

// The fixture themes over a page with every element, drawn by the real
// layout and saved beside the test: Ink once as Windows is light, once dark
void previews(const fs::path& dir) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"CSS theme previews", WS_POPUP, 0, 0, 1050, 900, nullptr, nullptr,
                               nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        check(false, "a window to draw the previews in");
        return;
    }
    app.width = 1050;
    app.height = 900;
    const fs::path fixtures = TINTA_THEME_CSS_FIXTURES;
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    for (const auto& entry : fs::directory_iterator(fixtures)) {
        if (entry.path().extension() == ".css") fs::copy_file(entry.path(), themes / entry.path().filename(), fs::copy_options::overwrite_existing);
    }
    loadCustomThemes();
    const std::string page = read(fixtures / "mixed.md");
    struct Look { const wchar_t* name; bool dark; const char* file; uint32_t text; bool isDark; };
    const Look looks[] = {
        {L"paper-light", false, "css-paper-light.png", 0, false},
        {L"night-dark", false, "css-night-dark.png", 0, true},
        {L"ink-light-dark", false, "css-ink-light.png", 0x1F2328, false},
        {L"ink-light-dark", true, "css-ink-dark.png", 0xD8DEE9, true},
        {L"book", false, "css-book.png", 0, false},
    };
    for (const auto& look : looks) {
        int index = -1;
        for (int i = 0; i < themeCount(); i++) {
            if (std::wstring(themeAt(i).name) == look.name) index = i;
        }
        check(index >= 0, "the fixture theme is listed: " + toUtf8(look.name));
        if (index < 0) continue;
        themesSetSystemDark(look.dark);
        applyTheme(app, index);
        check(app.theme.isDark == look.isDark, std::string(look.file) + ": dark as its sheet says");
        if (look.text) check(closeTo(app.theme.text, hexColor(look.text)), std::string(look.file) + ": light-dark() picks the text colour");
        app.root = parseDocument(app.parser, page, (fixtures / "mixed.md").wstring()).root;
        app.scrollY = app.targetScrollY = 0;
        layoutDocument(app);
        check(saveFramePreview(app, dir / look.file), std::string("saved ") + look.file);
    }
    themesSetSystemDark(false);
    for (const auto& entry : fs::directory_iterator(themes)) fs::remove(entry.path());
    loadCustomThemes();
    DestroyWindow(app.hwnd);
}
}  // namespace

int testCssThemes(App& app, const fs::path& dir) {
    failures = 0;
    roundTrips();
    sheets();
    installed(app, dir);
    previews(dir);
    std::cout << "CSS themes: " << failures << " failures\n";
    return failures;
}
