#include "d2d_init.h"
#include "export.h"
#include "input.h"
#include "overlays.h"
#include "print.h"
#include "render.h"
#include "settings.h"
#include "syntax.h"
#include "utils.h"

#include "glyph_fonts.h"

#include <dwrite_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

int testThemeColours(App& app, const std::filesystem::path& dir);
int testEmphasisColours(App& app, const std::filesystem::path& dir);


namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};
}
bool sameColor(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    return std::abs(a.r-b.r) < 0.001f && std::abs(a.g-b.g) < 0.001f &&
           std::abs(a.b-b.b) < 0.001f && std::abs(a.a-b.a) < 0.001f;
}
void checkRun(const App& app, const wchar_t* marker, D2D1_COLOR_F color) {
    bool found = false;
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && app.docText.substr(run.docStart, run.docLength) == marker) {
            found = true;
            check(sameColor(run.color, color), "native text run uses the right inline/block/link color");
        }
    }
    if (!found) std::wcerr << L"Missing native marker: " << marker << L'\n';
    check(found, "native fixture marker renders");
}
// themes.ini lines of one [theme] section
std::string themeSection(std::string ini, const std::string& name) {
    ini.erase(std::remove(ini.begin(), ini.end(), '\r'), ini.end());  // text-mode CRLF
    size_t at = ini.find("name=" + name + "\n");
    if (at == std::string::npos) return {};
    size_t next = ini.find("[theme]", at);
    return ini.substr(at, next == std::string::npos ? std::string::npos : next - at);
}

// #256: the theme editor sets the text, heading and code faces through its
// own clicks, and every saved theme lists headingfont=. #284: its font list
// shows the face it sets
void themeEditorFonts(const std::filesystem::path& dir) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Theme editor tests", WS_POPUP, 0, 0, 1180, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        check(false, "the theme editor test window opens");
        return;
    }
    app.width = 1180;
    app.height = 900;
    applyTheme(app, 0);
    updateTextFormats(app);
    auto paint = [&] {
        app.renderTarget->BeginDraw();
        if (app.showThemeEditor) renderThemeEditor(app);
        else renderSettingsOverlay(app);
        app.renderTarget->EndDraw();
    };
    auto click = [&](const std::vector<std::pair<D2D1_RECT_F, int>>& hits, int id) {
        for (const auto& [r, action] : hits) {
            if (action != id) continue;
            LPARAM p = MAKELPARAM((int)((r.left + r.right) / 2), (int)((r.top + r.bottom) / 2));
            handleMouseDown(app, app.hwnd, MK_LBUTTON, p);
            handleMouseUp(app, app.hwnd, 0, p);
            paint();
            return true;
        }
        return false;
    };
    auto open = [&](const wchar_t* name) {
        app.showSettings = true;
        app.settingsSection = 1;  // Appearance
        app.settingsAnimation = 1.0f;
        paint();
        check(click(app.settingsHits, SET_NEW_THEME) && app.showThemeEditor,
              "Appearance settings open the theme editor");
        app.themeEditorName = name;
    };
    // Switch the list to a face, scroll the family into view, pick it
    auto pick = [&](int role, const wchar_t* family) {
        check(click(app.themeEditorHits, role), "the font switch has the role");
        size_t index = 0;
        while (index < app.systemFontFamilies.size() &&
               _wcsicmp(app.systemFontFamilies[index].c_str(), family) != 0) index++;
        check(index < app.systemFontFamilies.size(), "the family is installed");
        int row = (int)index + (role == TE_FONT_HEADING ? 1 : 0);
        app.themeEditorFontScroll = row * dpi(app, 26.0f);
        paint();
        check(click(app.themeEditorHits, TE_FONT_BASE + (int)index), "the family's row can be clicked");
    };

    open(L"Editor fonts");
    check(app.themeEditorFont == L"Segoe UI" && app.themeEditorHeadingFont.empty() &&
          app.themeEditorCodeFont == L"Consolas", "the editor starts from Paper's faces");
    pick(TE_FONT_HEADING, L"Georgia");
    pick(TE_FONT_CODE, L"Courier New");
    pick(TE_FONT_TEXT, L"Verdana");
    check(app.themeEditorFont == L"Verdana" && app.themeEditorHeadingFont == L"Georgia" &&
          app.themeEditorCodeFont == L"Courier New", "each role keeps its own face");
    check(click(app.themeEditorHits, TE_SAVE) && !app.showThemeEditor, "the theme saves");
    std::string section = themeSection(read(dir / "themes.ini"), "Editor fonts");
    check(section.find("\nfont=Verdana\n") != std::string::npos &&
          section.find("\nheadingfont=Georgia\n") != std::string::npos &&
          section.find("\ncodefont=Courier New\n") != std::string::npos,
          "themes.ini holds the text, heading and code faces");
    check(_wcsicmp(app.theme.fontFamily, L"Verdana") == 0 &&
          _wcsicmp(themeHeadingFont(app.theme), L"Georgia") == 0 &&
          _wcsicmp(app.theme.codeFontFamily, L"Courier New") == 0,
          "the saved theme applies all three faces");

    // A new themes.ini names every font key
    const std::string seed = themesIniTemplate();
    check(seed.find("; font=") != std::string::npos && seed.find("; headingfont=") != std::string::npos &&
          seed.find("; codefont=") != std::string::npos, "a new themes.ini names the three font keys");

    // Headings back to the text face: the key stays, empty
    open(L"Editor same");
    click(app.themeEditorHits, TE_FONT_HEADING);
    app.themeEditorFontScroll = 0.0f;  // the switch scrolled to Georgia
    paint();
    check(click(app.themeEditorHits, TE_FONT_SAME) && app.themeEditorHeadingFont.empty(),
          "headings can follow the text face again");
    check(click(app.themeEditorHits, TE_SAVE), "the second theme saves");
    section = themeSection(read(dir / "themes.ini"), "Editor same");
    check(section.find("\nheadingfont=\n") != std::string::npos,
          "themes.ini writes an empty headingfont= line");
    loadCustomThemes();
    for (int i = THEME_COUNT; i < themeCount(); i++) {
        if (wcscmp(themeAt(i).name, L"Editor same") == 0) {
            check(wcscmp(themeHeadingFont(themeAt(i)), themeAt(i).fontFamily) == 0,
                  "an empty headingfont= sets headings in font=");
        }
    }

    // The list opens on the face it sets, and comes back to the choice when
    // the base theme or the face it sets changes (#284)
    const float rowHeight = dpi(app, 26.0f);
    auto familyIndex = [&](const std::wstring& family) {
        for (size_t i = 0; i < app.systemFontFamilies.size(); i++) {
            if (_wcsicmp(app.systemFontFamilies[i].c_str(), family.c_str()) == 0) return (int)i;
        }
        return -1;
    };
    // The family's whole row inside the list, or an empty rect
    auto shownRow = [&](const std::wstring& family) {
        const int index = familyIndex(family);
        for (const auto& [r, action] : app.themeEditorHits) {
            if (index >= 0 && action == TE_FONT_BASE + index && r.bottom - r.top > rowHeight - 0.5f) return r;
        }
        return D2D1_RECT_F{};
    };
    auto centred = [&](const D2D1_RECT_F& row) {
        const D2D1_RECT_F& list = app.themeEditorFontListRect;
        return std::abs((row.top + row.bottom) / 2 - (list.top + list.bottom) / 2) <= rowHeight / 2;
    };
    applyTheme(app, 0);  // Paper: Segoe UI, far down the list on any system
    open(L"Editor follow");
    D2D1_RECT_F row = shownRow(app.themeEditorFont);
    check(row.bottom > row.top, "the font list opens on the text face");
    check(centred(row), "the text face opens in the middle of the list");
    check(click(app.themeEditorHits, TE_FONT_CODE) && shownRow(app.themeEditorCodeFont).bottom > 0,
          "switching to the code face shows it");
    check(click(app.themeEditorHits, TE_FONT_TEXT) && centred(shownRow(app.themeEditorFont)),
          "switching back to the text face shows it again");
    app.themeEditorFontScroll = 0.0f;  // browsed to the top
    paint();
    check(shownRow(app.themeEditorFont).bottom == 0, "browsing scrolls the face out of view");
    check(click(app.themeEditorHits, TE_BASE_NEXT) && app.themeEditorBase == 1, "the base switches to Sakura");
    check(centred(shownRow(app.themeEditorFont)), "a new base shows its text face");
    // The last family stops at the list's end instead of the middle
    app.themeEditorFont = app.systemFontFamilies.back();
    click(app.themeEditorHits, TE_FONT_TEXT);
    row = shownRow(app.themeEditorFont);
    check(row.bottom > row.top && std::abs(row.bottom - app.themeEditorFontListRect.bottom) < 1.0f,
          "the last family sits at the bottom of a list scrolled to its end");
    check(click(app.themeEditorHits, TE_CANCEL) && !app.showThemeEditor, "the theme editor cancels");
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
}

std::wstring formatFace(IDWriteTextFormat* format) {
    wchar_t name[128]{};
    if (!format || FAILED(format->GetFontFamilyName(name, 128))) return {};
    return name;
}

// The faces the laid-out document actually uses, from each run's layout
bool documentUses(const App& app, const wchar_t* face) {
    for (const auto& run : app.layoutTextRuns) {
        wchar_t name[128]{};
        if (run.layout && SUCCEEDED(run.layout->GetFontFamilyName(0, name, 128)) &&
            std::wstring(name) == face) {
            return true;
        }
    }
    return false;
}

// #265: fonts saved in the theme editor take effect at once. The editor
// saves its copy of the theme in use under the same name, which rewrote the
// very strings the "fonts unchanged?" check read through app.theme, so it saw
// no change; a heading-only change was never compared at all.
void themeEditorFontsApply(App& app, const std::string& source) {
    app.root = app.parser.parse(source).root;
    app.width = 1050;
    const std::wstring name = L"Editor fonts 265";
    int index = saveCustomTheme(themeAt(0), name, L"Georgia", L"Consolas", L"");
    applyTheme(app, index);
    layoutDocument(app);
    check(formatFace(app.textFormat) == L"Georgia" && documentUses(app, L"Georgia"),
          "a saved custom theme lays out in its text font");

    struct Save { const wchar_t* text; const wchar_t* code; const wchar_t* heading; const char* what; };
    for (const auto& save : {
            Save{L"Verdana", L"Courier New", L"", "new text and code fonts, saved over the theme in use"},
            Save{L"Palatino Linotype", L"Lucida Console", L"", "longer names, so the theme's strings move"},
            Save{L"Palatino Linotype", L"Lucida Console", L"Tahoma", "only the heading font changes"},
            Save{L"Palatino Linotype", L"Lucida Console", L"", "the heading font goes back to the text font"}}) {
        // As the editor's Save does: its copy of the theme in use, same name
        const D2DTheme edited = app.theme;
        index = saveCustomTheme(edited, name, save.text, save.code, save.heading);
        app.layoutDirty = false;
        applyTheme(app, index);
        check(app.layoutDirty, save.what);
        layoutDocument(app);
        const std::wstring heading = *save.heading ? save.heading : save.text;
        check(formatFace(app.textFormat) == save.text, save.what);
        check(formatFace(app.codeFormat) == save.code, save.what);
        for (auto* format : app.headingFormats)
            check(formatFace(format) == heading, save.what);
        check(documentUses(app, save.text) && documentUses(app, save.code) &&
              documentUses(app, heading.c_str()),
              "the open document is laid out in the saved fonts at once");
    }
}

std::string cssHex(const D2D1_COLOR_F& c) {
    char buf[8];
    snprintf(buf, sizeof buf, "#%02x%02x%02x", (int)std::lround(c.r * 255), (int)std::lround(c.g * 255),
             (int)std::lround(c.b * 255));
    return buf;
}

std::string docxRun(const std::string& zip, const std::string& marker);

// #278: inlinecodebackground= gives inline code its own box, or none,
// while fenced code keeps codebackground
void inlineCodeBackground(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    const fs::path fixture = TINTA_INLINE_BG_FIXTURE;
    { std::ofstream out(dir / "themes.ini", std::ios::binary);
      out << read(fixture.parent_path() / "inline-code-background-themes.ini"); }
    loadCustomThemes();
    auto indexOf = [](const wchar_t* name) {
        for (int i = 0; i < themeCount(); ++i)
            if (themeAt(i).name && std::wstring(themeAt(i).name) == name) return i;
        return -1;
    };
    const int inherit = indexOf(L"Inline inherit"), none = indexOf(L"Inline none"),
              tinted = indexOf(L"Inline tinted");
    check(inherit >= 0 && none >= 0 && tinted >= 0, "the three inline background themes load");
    if (inherit < 0 || none < 0 || tinted < 0) return;
    check(!themeAt(inherit).inlineCodeBackground, "a theme without the key inherits codebackground");
    check(themeAt(none).inlineCodeBackground && themeAt(none).inlineCodeBackground->a == 0.0f,
          "inlinecodebackground=none loads as no box");
    check(themeAt(tinted).inlineCodeBackground &&
              sameColor(*themeAt(tinted).inlineCodeBackground, hexColor(0x45475A)),
          "an inline background colour loads");

    auto state = std::make_unique<App>();
    App& app = *state;
    if (!initD2D(app)) { check(false, "D2D initializes"); return; }
    app.height = 900;
    app.readingWidthPct = 100;
    const std::string source = read(fixture);
    const std::wstring markers[] = {L"inline_heading", L"inline_one", L"inline_two", L"inline_bold",
                                    L"inline_list", L"inline_quote", L"inline_wrap"};
    for (int index : {inherit, none, tinted}) {
        applyTheme(app, index);
        // What each fixture theme asks for, independent of the code under test
        const D2D1_COLOR_F block = app.theme.codeBackground;
        const D2D1_COLOR_F box = index == none     ? D2D1::ColorF(0, 0, 0, 0)
                                 : index == tinted ? hexColor(0x45475A)
                                                   : block;
        for (int width : {1050, 650}) {
            app.width = width;
            app.root = app.parser.parse(source).root;
            layoutDocument(app);
            // The rects drawn under a laid-out run, by colour
            auto under = [&](const std::wstring& marker, const D2D1_COLOR_F& color) {
                int found = -1;
                for (const auto& run : app.layoutTextRuns) {
                    if (!run.docLength) continue;
                    if (app.docText.compare(run.docStart, marker.size(), marker) != 0) continue;
                    const float x = run.bounds.left + 2.0f;
                    const float y = (run.bounds.top + run.bounds.bottom) * 0.5f;
                    found = 0;
                    for (const auto& rect : app.layoutRects)
                        if (x >= rect.rect.left && x <= rect.rect.right && y >= rect.rect.top &&
                            y <= rect.rect.bottom && sameColor(rect.color, color)) {
                            found++;
                        }
                    break;
                }
                return found;
            };
            for (const auto& marker : markers) {
                if (box.a > 0.0f) {
                    check(under(marker, box) >= 1, "inline code sits on its theme's inline box");
                } else {
                    check(under(marker, block) == 0, "with none, inline code has no box behind it");
                }
            }
            check(under(L"block_plain", block) >= 1, "fenced code keeps codebackground");

            check(exportHtmlFile(app, (dir / "inline-bg.html").wstring()), "HTML export succeeds");
            const std::string html = read(dir / "inline-bg.html");
            const size_t rule = html.find("code{font-family:");
            const std::string inlineRule =
                rule == std::string::npos ? "" : html.substr(rule, html.find('}', rule) - rule);
            check(inlineRule.find("background:" + (box.a > 0.0f ? cssHex(box) : std::string("none")) + ";") !=
                      std::string::npos,
                  "the HTML export's inline code rule follows the theme");
            check(html.find("pre{background:" + cssHex(block)) != std::string::npos,
                  "the HTML export's code blocks keep codebackground");

            check(exportDocxFile(app, (dir / "inline-bg.docx").wstring()), "Word export succeeds");
            const std::string zip = read(dir / "inline-bg.docx");
            const std::string run = docxRun(zip, "inline_one");
            check(!run.empty(), "the Word export holds the inline span");
            if (box.a > 0.0f) {
                const std::string fill = app.theme.isDark ? "F2F0EE" : cssHex(box).substr(1);
                std::string upper = fill;
                for (char& c : upper) c = (char)toupper((unsigned char)c);
                check(run.find("w:fill=\"" + upper + "\"") != std::string::npos,
                      "the Word export shades inline code with its inline colour");
            } else {
                check(run.find("<w:shd") == std::string::npos, "with none, the Word export leaves inline code unshaded");
            }
        }
    }
    check(printTheme(themeAt(none)).inlineCodeBackground &&
              printTheme(themeAt(none)).inlineCodeBackground->a == 0.0f,
          "printing keeps inline code without a box");
    check(!printTheme(themeAt(tinted)).inlineCodeBackground,
          "printing gives a coloured inline box the paper's code shade");

    // themes.ini keeps the key through a save from the editor
    const D2DTheme saveNone = themeAt(none), saveTinted = themeAt(tinted);
    saveCustomTheme(saveNone, L"Saved none", saveNone.fontFamily, saveNone.codeFontFamily);
    saveCustomTheme(saveTinted, L"Saved tinted", saveTinted.fontFamily, saveTinted.codeFontFamily);
    const std::string ini = read(dir / "themes.ini");
    check(ini.find("inlinecodebackground=none") != std::string::npos &&
              ini.find("inlinecodebackground=45475A") != std::string::npos,
          "themes.ini writes inlinecodebackground");
    loadCustomThemes();
    check(indexOf(L"Saved none") >= 0 && themeAt(indexOf(L"Saved none")).inlineCodeBackground &&
              themeAt(indexOf(L"Saved none")).inlineCodeBackground->a == 0.0f,
          "a saved none survives reloading themes.ini");
}

std::string docxRun(const std::string& zip, const std::string& marker) {
    // Tinta writes stored ZIP entries, so the actual XML can be inspected
    // here without introducing a ZIP dependency into the native tests.
    size_t text = zip.find(">" + marker + "</w:t>");
    if (text == std::string::npos) return {};
    size_t start = zip.rfind("<w:r>", text);
    return start == std::string::npos ? std::string() : zip.substr(start, text-start);
}
}

// #264: a theme naming faces this PC lacks (Windows 10 has no Cascadia
// Code) draws with installed ones, and the fallback list keeps the signs
// Segoe UI Emoji maps to the wrong glyph away from it
void missingFaces(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    using Microsoft::WRL::ComPtr;
    const fs::path fixture = TINTA_MISSING_FONT_FIXTURE;
    { std::ofstream out(dir / "themes.ini", std::ios::binary);
      out << read(fixture.parent_path() / "missing-font-themes.ini"); }
    loadCustomThemes();
    int missing = -1;
    for (int i = 0; i < themeCount(); ++i)
        if (themeAt(i).name && std::wstring(themeAt(i).name) == L"Missing faces") missing = i;
    check(missing >= 0, "the theme with missing faces loads");
    if (missing < 0) return;

    auto state = std::make_unique<App>();
    App& app = *state;
    if (!initD2D(app)) { check(false, "D2D initializes"); return; }
    app.width = 1050;
    app.height = 900;
    app.readingWidthPct = 100;
    ComPtr<IDWriteFontCollection> fonts;
    app.dwriteFactory->GetSystemFontCollection(&fonts);
    auto installed = [&](const wchar_t* name) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        return name && SUCCEEDED(fonts->FindFamilyName(name, &index, &exists)) && exists;
    };
    auto familyOf = [](IDWriteTextFormat* format) {
        if (!format) return std::wstring();
        std::wstring name(format->GetFontFamilyNameLength() + 1, L'\0');
        format->GetFontFamilyName(name.data(), (UINT32)name.size());
        name.resize(name.size() - 1);
        return name;
    };
    // The faces a layout draws its glyphs in, after the document fallback
    auto drawnIn = [&](IDWriteTextLayout* layout) {
        ComPtr<IDWriteTextLayout2> layout2;
        if (app.fontFallback && SUCCEEDED(layout->QueryInterface(IID_PPV_ARGS(&layout2))))
            layout2->SetFontFallback(app.fontFallback);
        glyphfonts::RunFonts runs(fonts.Get());
        layout->Draw(nullptr, &runs, 0, 0);
        return runs.families;
    };
    auto textIn = [&](IDWriteTextFormat* format, const std::wstring& text) {
        ComPtr<IDWriteTextLayout> layout;
        if (!format || FAILED(app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                format, 2000.0f, 200.0f, &layout)))
            return std::wstring(L"(no layout)");
        return drawnIn(layout.Get());
    };

    applyTheme(app, missing);
    check(std::wstring(themeAt(missing).fontFamily) == L"NoSuchText264" &&
              std::wstring(themeAt(missing).codeFontFamily) == L"NoSuchMono264",
          "themes.ini and the theme editor keep the faces the theme names");
    const wchar_t* nearest = installed(L"Cascadia Mono")   ? L"Cascadia Mono"
                             : installed(L"Cascadia Code") ? L"Cascadia Code"
                                                           : L"Consolas";
    const std::wstring code = app.theme.codeFontFamily ? app.theme.codeFontFamily : L"";
    check(code == nearest, "a missing code face gives way to the nearest installed one");
    check(app.theme.fontFamily && std::wstring(app.theme.fontFamily) == L"Segoe UI",
          "a missing text face gives way to Segoe UI");
    check(!app.theme.headingFontFamily, "a missing heading face leaves headings to the body's");
    check(familyOf(app.codeFormat) == code && familyOf(app.codeBoldFormat) == code &&
              familyOf(app.editorTextFormat) == code,
          "code blocks and the editor take the installed face");
    check(familyOf(app.textFormat) == L"Segoe UI" && familyOf(app.headingFormats[0]) == L"Segoe UI",
          "body text and headings take Segoe UI");
    // The editor's columns assume one pitch
    auto width = [&](const wchar_t* text) {
        ComPtr<IDWriteTextLayout> layout;
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(app.dwriteFactory->CreateTextLayout(text, (UINT32)wcslen(text),
                app.editorTextFormat, 2000.0f, 200.0f, &layout)))
            layout->GetMetrics(&metrics);
        return metrics.widthIncludingTrailingWhitespace;
    };
    check(std::abs(width(L"iiiiiiiiii") - width(L"MMMMMMMMMM")) < 0.01f &&
              std::abs(width(L"M") - app.editorCharWidth) < 0.01f,
          "the editor keeps a fixed pitch");
    check(textIn(app.editorTextFormat, L"Plain text 0123") == code,
          "plain text in the editor is all in the code face");
    check(textIn(app.editorTextFormat, L"\u221A2 \u221E \u222B \u2260 \u2219")
                  .find(L"Segoe UI Emoji") == std::wstring::npos,
          "the editor draws the signs without the emoji face");

    // The fixture laid out: no run but the emoji draws from the emoji face
    auto doc = app.parser.parse(read(fixture));
    check(doc.success, "the missing-font fixture parses");
    app.root = doc.root;
    for (int pageWidth : {1050, 520}) {
        app.width = pageWidth;
        layoutDocument(app);
        check(app.codeBlocks.size() == 2, "both code blocks lay out");
        int runs = 0, emojiFace = 0;
        for (const auto& run : app.layoutTextRuns) {
            if (!run.layout || !run.docLength) continue;
            const std::wstring text = app.docText.substr(run.docStart, run.docLength);
            bool emoji = text.find(L"\u2705") != std::wstring::npos;
            for (wchar_t c : text) emoji = emoji || (c >= 0xD800 && c <= 0xDFFF);
            if (emoji) continue;
            ++runs;
            if (drawnIn(run.layout).find(L"Segoe UI Emoji") != std::wstring::npos) ++emojiFace;
        }
        check(runs > 20 && emojiFace == 0, "the page draws no sign from the emoji face");
    }

    // Applying the theme again keeps its formats: the check compares the
    // faces the formats were made from with those the theme draws with
    IDWriteTextFormat* before = app.textFormat;
    applyTheme(app, missing);
    check(app.textFormat == before, "applying the same theme again keeps its formats");
    // The theme chooser previews the theme in the faces it draws with
    ensureThemePreviewFormats(app);
    check((int)app.themePreviewFormats.size() > missing &&
              familyOf(app.themePreviewFormats[missing].code) == code &&
              familyOf(app.themePreviewFormats[missing].preview) == L"Segoe UI",
          "the theme chooser previews the installed faces");
    // A theme whose faces are installed keeps them
    applyTheme(app, 0);
    check(app.theme.codeFontFamily == themeAt(0).codeFontFamily &&
              app.theme.fontFamily == themeAt(0).fontFamily,
          "installed faces stay as the theme names them");

    // The fallback list, for a face without the signs: Segoe UI, then
    // Segoe UI Symbol draw them; the emoji face keeps the emoji
    ComPtr<IDWriteTextFormat> lacking;
    app.dwriteFactory->CreateTextFormat(L"NoSuchFace264", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"en-us", &lacking);
    for (const wchar_t* sign : {L"\u221A", L"\u221E", L"\u222B", L"\u2260", L"\u2219",
                                L"\u02D8", L"\u02D9", L"\uFB02"})
        check(textIn(lacking.Get(), sign) == L"Segoe UI", "a sign the face lacks comes from Segoe UI");
    check(textIn(lacking.Get(), L"\u221B") == L"Segoe UI Symbol",
          "maths Segoe UI lacks comes from Segoe UI Symbol");
    check(textIn(lacking.Get(), L"\U0001F600") == L"Segoe UI Emoji", "emoji still come from the emoji face");
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    // CTest stages this executable in an isolated portable folder. Refuse
    // to touch any real configuration if the binary is launched directly.
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = fs::path(exe).parent_path();
    const auto settings = read(dir / "settings.ini");
    if (argc != 2 || std::string(argv[1]) != "--portable-test" ||
        !fs::equivalent(dir, fs::current_path()) ||
        (settings != "; Isolated native theme test configuration\n" &&
         settings != "; Isolated native theme test configuration\r\n")) {
        std::cerr << "Run this test through CTest's isolated portable wrapper\n";
        return 2;
    }
    const auto fixture = fs::path(TINTA_THEME_FIXTURE);
    {
        std::ofstream out(dir / "themes.ini", std::ios::binary);
        out << read(fixture.parent_path() / "inline-code-themes.ini");
        out << "\n[theme]\nname=Reverse order\ninlinecode=#b00020\ncode=334455\n"
               "\n[theme]\nname=Invalid override\ninlinecode=broken\ncode=334455\n";
    }
    check(fs::equivalent(tintaConfigDir(), dir), "theme persistence uses only the portable test folder");
    loadCustomThemes();
    check(themeCount() == THEME_COUNT + 5, "all custom theme sections load");
    for (int i = 0; i < THEME_COUNT; ++i) {
        check(!themeAt(i).inlineCode, "built-in themes retain inheritance");
        check(sameColor(themeInlineCodeColor(themeAt(i)), themeAt(i).code),
              "built-in inline color remains exactly the existing code color");
    }
    check(!themeAt(THEME_COUNT).inlineCode, "legacy custom theme inherits code");
    check(sameColor(themeInlineCodeColor(themeAt(THEME_COUNT)), hexColor(0x334455)),
          "legacy custom code value is the fallback");
    check(sameColor(themeInlineCodeColor(themeAt(THEME_COUNT+1)), hexColor(0xB00020)),
          "explicit inlinecode loads");
    check(sameColor(themeInlineCodeColor(themeAt(THEME_COUNT+3)), hexColor(0xB00020)),
          "inlinecode before code and optional hash/lowercase load correctly");
    check(!themeAt(THEME_COUNT+4).inlineCode, "invalid optional color retains inheritance");
    D2DTheme inherited = themeAt(THEME_COUNT);
    inherited.code = hexColor(0x123456);
    check(sameColor(themeInlineCodeColor(inherited), inherited.code), "fallback follows changes to code");
    D2DTheme explicitTheme = themeAt(THEME_COUNT+1);
    explicitTheme.code = hexColor(0x123456);
    check(sameColor(themeInlineCodeColor(explicitTheme), hexColor(0xB00020)),
          "explicit inline color remains independent when code changes");

    // The theme editor saves a full D2DTheme copy through this same writer.
    auto original = themeAt(THEME_COUNT+1);
    int saved = saveCustomTheme(original, L"Saved from editor", original.fontFamily, original.codeFontFamily);
    check(saved >= THEME_COUNT, "custom theme saves");
    std::string ini = read(dir / "themes.ini");
    size_t legacy = ini.find("name=Inline Code Legacy");
    size_t next = ini.find("[theme]", legacy);
    check(ini.substr(legacy, next-legacy).find("inlinecode=") == std::string::npos,
          "saving does not materialize an inherited color in legacy themes");
    loadCustomThemes();
    check(themeAt(saved).inlineCode &&
          sameColor(themeInlineCodeColor(themeAt(saved)), hexColor(0xB00020)),
          "saved explicit color survives restart/reload");

    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!initD2D(app)) return 2;
        app.height = 900;
        app.readingWidthPct = 100;
        auto doc = app.parser.parse(read(fixture));
        check(doc.success, "mixed Markdown/theme fixture parses");
        app.root = doc.root;
        for (int index : {THEME_COUNT, THEME_COUNT+1, THEME_COUNT+2}) {
            applyTheme(app, index);
            for (int width : {1050, 650}) {
                app.width = width;
                layoutDocument(app);
                for (const wchar_t* marker : {L"inline_heading", L"inline_plain", L"inline_bold",
                        L"inline_italic", L"inline_table", L"inline_table_second", L"inline_quote", L"inline_list"})
                    checkRun(app, marker, themeInlineCodeColor(app.theme));
                checkRun(app, L"block_plain", app.theme.code);
                checkRun(app, L"linked_code", app.theme.link);
                check(sameColor(getTokenColor(app.theme, SyntaxTokenType::Plain), app.theme.code) &&
                      sameColor(getTokenColor(app.theme, SyntaxTokenType::Operator), app.theme.code),
                      "syntax plain/operator tokens retain the fenced-code base color");
                check(app.codeBlocks.size() == 3, "all mixed fenced blocks remain intact");
                auto before = app.layoutTextRuns;
                float height = app.contentHeight;
                app.theme.inlineCode = hexColor(0x00BB88);
                layoutDocument(app);
                check(app.layoutTextRuns.size() == before.size() && app.contentHeight == height,
                      "changing only inline color leaves document geometry unchanged");
                for (size_t i = 0; i < before.size() && i < app.layoutTextRuns.size(); ++i)
                    check(before[i].pos.x == app.layoutTextRuns[i].pos.x && before[i].pos.y == app.layoutTextRuns[i].pos.y,
                          "headings, tables, math and surrounding text keep their positions");
                app.theme = themeAt(index);
            }
        }
        applyTheme(app, THEME_COUNT+1);
        check(exportHtmlFile(app, (dir / "separate.html").wstring()), "HTML export succeeds");
        std::string html = read(dir / "separate.html");
        check(html.find(";color:#b00020;") != std::string::npos &&
              html.find("pre code{color:#334455;}") != std::string::npos &&
              html.find("a code{color:inherit;}") != std::string::npos,
              "HTML separates inline, fenced and linked code colors");
        check(exportDocxFile(app, (dir / "separate.docx").wstring()), "DOCX export succeeds");
        std::string zip = read(dir / "separate.docx");
        for (const char* marker : {"inline_plain", "inline_heading", "inline_quote", "inline_table"})
            check(docxRun(zip, marker).find("<w:color w:val=\"B00020\"/>") != std::string::npos,
                  "DOCX inline spans preserve the explicit color in mixed content");
        check(!docxRun(zip, "block_plain").empty() &&
              docxRun(zip, "block_plain").find("B00020") == std::string::npos,
              "DOCX fenced code does not acquire the inline color");
        check(docxRun(zip, "linked_code").find("<w:color w:val=\"B85A3C\"/>") != std::string::npos,
              "DOCX linked code preserves the link color");
        failures += testThemeColours(app, dir);
        failures += testEmphasisColours(app, dir);
        themeEditorFontsApply(app, read(TINTA_THEME_FONTS_FIXTURE));
    }
    themeEditorFonts(dir);
    inlineCodeBackground(dir);  // these two rewrite themes.ini
    missingFaces(dir);
    CoUninitialize();
    std::cout << "Inline code themes: " << failures << " failures\n";
    return failures ? 1 : 0;
}
