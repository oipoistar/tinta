// #332: the theme editor's font list names each family in the interface's
// language, as Windows' own font lists do, where the font has such a name:
// Microsoft YaHei under its Chinese name in a Chinese interface. A theme
// still keeps the family's en-US name, and one written by hand may use
// either.
//
// The names are spelled as code units: the sources are compiled without
// /utf-8.
#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "input.h"
#include "overlays.h"
#include "render.h"
#include "settings.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) {
        if (failures < 60) std::cerr << "FAIL font names: " << message << '\n';
        ++failures;
    }
}

const wchar_t kYaHei[] = {0x5FAE, 0x8F6F, 0x96C5, 0x9ED1, 0};         // Microsoft YaHei, zh-cn
const wchar_t kSimSun[] = {0x5B8B, 0x4F53, 0};                        // SimSun, zh-cn
const wchar_t kSimHei[] = {0x9ED1, 0x4F53, 0};                        // SimHei, zh-cn
const wchar_t kJhengHei[] = {0x5FAE, 0x8EDF, 0x6B63, 0x9ED1, 0x9AD4, 0};  // Microsoft JhengHei, zh-tw
const wchar_t kYuGothic[] = {0x6E38, 0x30B4, 0x30B7, 0x30C3, 0x30AF, 0};  // Yu Gothic, ja-jp
const wchar_t kMeiryo[] = {0x30E1, 0x30A4, 0x30EA, 0x30AA, 0};        // Meiryo, ja-jp
const wchar_t kMalgun[] = {0xB9D1, 0xC740, 0x0020, 0xACE0, 0xB515, 0};  // Malgun Gothic, ko-kr

int choice(std::initializer_list<const wchar_t*> locales, const wchar_t* ui) {
    return fontNameChoice(std::vector<std::wstring>(locales.begin(), locales.end()), ui);
}

void choices() {
    check(choice({L"en-us", L"zh-cn"}, L"zh-CN") == 1, "a Chinese interface takes the zh-cn name");
    check(choice({L"en-us", L"zh-cn"}, L"en-US") == 0, "an English one the en-us name");
    check(choice({L"zh-cn", L"en-us"}, L"en-GB") == 1, "en-GB reads the en-us name");
    check(choice({L"en-us", L"zh-cn"}, L"zh-SG") == 1, "Singapore reads the Simplified name");
    check(choice({L"en-us", L"zh-cn"}, L"zh-TW") == 0, "Taiwan does not: without a Traditional name, the English one");
    check(choice({L"en-us", L"zh-cn", L"zh-tw"}, L"zh-HK") == 2, "Hong Kong reads the Traditional name");
    check(choice({L"en-us", L"zh-tw", L"zh-cn"}, L"zh-CN") == 2, "the exact locale before a related one");
    check(choice({L"en-us", L"zh-hk", L"zh-tw"}, L"zh-TW") == 2, "and before an earlier related one");
    check(choice({L"en-us", L"ja-jp"}, L"ja-JP") == 1 && choice({L"en-us", L"ja-jp"}, L"ko-KR") == 0,
          "Japanese names for a Japanese interface only");
    check(choice({L"en-us", L"ko-kr"}, L"ko-KR") == 1, "Korean names for a Korean interface");
    check(choice({L"en-us", L"de-de"}, L"de-AT") == 1, "a language's other region reads its name");
    check(choice({L"de-de", L"en-us"}, L"fr-FR") == 1, "a language the font has no name in reads the English one");
    check(choice({L"zh-cn"}, L"en-US") == 0 && choice({L"ja-jp", L"zh-cn"}, L"en-US") == 0,
          "a font with no English name is listed under its first");
    check(choice({L"EN-US", L"ZH-CN"}, L"zh-cn") == 1 && choice({L"EN-US", L"ZH-CN"}, L"de-DE") == 0,
          "capitals in a locale's name do not matter");
    check(choice({}, L"en-US") == -1, "no names, no choice");
    check(choice({L"en-us", L"zh-cn"}, L"") == 0, "no interface locale reads the English name");
}

int familyIndex(const App& app, const wchar_t* family) {
    for (size_t i = 0; i < app.systemFontFamilies.size(); i++) {
        if (_wcsicmp(app.systemFontFamilies[i].c_str(), family) == 0) return (int)i;
    }
    return -1;
}

void language(App& app, const char* id) {
    const int index = languageIndexById(id);
    check(index >= 0, std::string("the language is known: ") + id);
    app.languageSetting = app.currentLanguageIndex = index >= 0 ? index : 0;
    enumerateSystemFontFamilies(app);
}

bool sortedFor(const App& app, const std::wstring& locale) {
    for (size_t i = 1; i < app.systemFontLabels.size(); i++) {
        const std::wstring& a = app.systemFontLabels[i - 1];
        const std::wstring& b = app.systemFontLabels[i];
        if (CompareStringEx(locale.c_str(), LINGUISTIC_IGNORECASE, a.c_str(), (int)a.size(), b.c_str(), (int)b.size(),
                            nullptr, nullptr, 0) == CSTR_GREATER_THAN) {
            return false;
        }
    }
    return true;
}

// The label a family is listed under, or empty when it is not installed
std::wstring labelOf(const App& app, const wchar_t* family) {
    const int index = familyIndex(app, family);
    return index < 0 ? std::wstring() : app.systemFontLabels[index];
}

int named = 0;  // families found installed and checked under a localized name
void localized(const App& app, const wchar_t* family, const wchar_t* name, const std::string& where) {
    const std::wstring label = labelOf(app, family);
    if (label.empty()) return;
    ++named;
    check(label == name, toUtf8(family) + " is listed under its own name " + where);
}

void lists(App& app) {
    language(app, "en");
    const std::vector<std::wstring> english = app.systemFontFamilies;
    check(!english.empty() && app.systemFontLabels.size() == english.size(), "every family has a label");
    check(familyIndex(app, L"Segoe UI") >= 0 && familyIndex(app, L"Consolas") >= 0, "Windows' own faces are listed");
    check(sortedFor(app, dateLocale(app.currentLanguageIndex)), "the English list is in the order English sorts in");
    bool same = true, ascii = true;
    for (size_t i = 0; i < english.size(); i++) {
        // A family with an en-US name is listed under it in English
        const bool plain = std::all_of(english[i].begin(), english[i].end(), [](wchar_t c) { return c < 0x80; });
        if (plain) same = same && app.systemFontLabels[i] == english[i];
        ascii = ascii && !english[i].empty();
    }
    check(same && ascii, "in English a family is listed under the name a theme keeps");
    for (const wchar_t* family : {L"Microsoft YaHei", L"SimSun", L"Yu Gothic", L"Malgun Gothic"}) {
        const std::wstring label = labelOf(app, family);
        check(label.empty() || label == family, toUtf8(family) + " keeps its English name in English");
    }

    language(app, "zh");
    check(app.systemFontFamilies.size() == english.size() &&
              std::set<std::wstring>(app.systemFontFamilies.begin(), app.systemFontFamilies.end()) ==
                  std::set<std::wstring>(english.begin(), english.end()),
          "#332: a Chinese interface lists the same families, under the names themes keep");
    localized(app, L"Microsoft YaHei", kYaHei, "in Chinese");
    localized(app, L"SimSun", kSimSun, "in Chinese");
    localized(app, L"SimHei", kSimHei, "in Chinese");
    check(labelOf(app, L"Segoe UI") == L"Segoe UI" && labelOf(app, L"Consolas") == L"Consolas",
          "#332: a family without a Chinese name keeps its English one");
    {
        const std::wstring label = labelOf(app, L"Yu Gothic");
        check(label.empty() || label == L"Yu Gothic", "#332: a Japanese family is not given its Japanese name in Chinese");
    }
    check(sortedFor(app, dateLocale(app.currentLanguageIndex)), "#332: the Chinese list is in the order Chinese sorts in");
    if (labelOf(app, L"Microsoft YaHei") == kYaHei) {
        check(familyIndex(app, L"Microsoft YaHei") > familyIndex(app, L"Segoe UI"),
              "#332: Chinese names come after the Latin ones");
        // A theme written by hand may name the family in Chinese
        check(std::wstring(installedFace(app, kYaHei, false)) == kYaHei, "#332: DirectWrite finds the family by its Chinese name");
    }

    language(app, "ja");
    localized(app, L"Yu Gothic", kYuGothic, "in Japanese");
    localized(app, L"Meiryo", kMeiryo, "in Japanese");
    {
        const std::wstring label = labelOf(app, L"Microsoft YaHei");
        check(label.empty() || label == L"Microsoft YaHei", "#332: a Chinese family keeps its English name in Japanese");
    }
    language(app, "ko");
    localized(app, L"Malgun Gothic", kMalgun, "in Korean");
    if (languageIndexById("zh-tw") >= 0) {
        language(app, "zh-tw");
        localized(app, L"Microsoft JhengHei", kJhengHei, "in Traditional Chinese");
        const std::wstring label = labelOf(app, L"Microsoft YaHei");
        check(label.empty() || label == L"Microsoft YaHei",
              "#332: a Simplified name is not shown in a Traditional Chinese interface");
    }
    language(app, "en");
    check(app.systemFontFamilies == english, "back in English the list is as it was");
    std::cout << "Font names: " << named << " installed families checked under a localized name\n";
}

// The editor's list in a Chinese interface: the row of the face a theme
// names is found by either name, and a click keeps the en-US name
void editorList(App& app) {
    language(app, "zh");
    if (labelOf(app, L"Microsoft YaHei") != kYaHei) {
        std::cout << "Font names: Microsoft YaHei is not installed, the editor's list is not checked\n";
        language(app, "en");
        return;
    }
    auto paint = [&] {
        app.renderTarget->BeginDraw();
        if (app.showThemeEditor) renderThemeEditor(app);
        else renderSettingsOverlay(app);
        check(SUCCEEDED(app.renderTarget->EndDraw()), "the dialog paints");
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
    const float rowHeight = dpi(app, 26.0f);
    const int index = familyIndex(app, L"Microsoft YaHei");
    // The family's whole row inside the list, or an empty rect
    auto shownRow = [&] {
        for (const auto& [r, action] : app.themeEditorHits) {
            if (action == TE_FONT_BASE + index && r.bottom - r.top > rowHeight - 0.5f) return r;
        }
        return D2D1_RECT_F{};
    };
    // In the middle of the list, or as near to it as the list's end
    // lets a row come: Chinese names sort after the Latin ones
    auto inView = [&](const D2D1_RECT_F& row) {
        const D2D1_RECT_F& list = app.themeEditorFontListRect;
        return row.bottom > row.top && row.top >= list.top - 0.5f && row.bottom <= list.bottom + 0.5f;
    };
    app.showSettings = true;
    app.settingsSection = 1;  // Appearance
    app.settingsAnimation = 1.0f;
    paint();
    check(click(app.settingsHits, SET_NEW_THEME) && app.showThemeEditor, "Appearance settings open the theme editor");
    check(app.systemFontLanguage == app.currentLanguageIndex && labelOf(app, L"Microsoft YaHei") == kYaHei,
          "#332: the editor's list is the Chinese one");

    // A theme that names the face in English, then one that names it in Chinese
    for (const wchar_t* written : {L"Microsoft YaHei", kYaHei}) {
        const std::string how = written == kYaHei ? "in Chinese" : "in English";
        app.themeEditorFont = written;
        check(click(app.themeEditorHits, TE_FONT_CODE) && click(app.themeEditorHits, TE_FONT_TEXT),
              "the list is switched away and back");
        check(inView(shownRow()), "#332: the list opens on the face a theme names " + how);
        // Scrolled to the top, the row is out of sight: it was the choice that brought it
        app.themeEditorFontScroll = 0.0f;
        paint();
        check(!inView(shownRow()), "and it is not in view at the list's top");
    }
    app.themeEditorFont = kYaHei;
    check(click(app.themeEditorHits, TE_FONT_CODE) && click(app.themeEditorHits, TE_FONT_TEXT) && inView(shownRow()),
          "the row is in view again");
    check(click(app.themeEditorHits, TE_FONT_BASE + index) && app.themeEditorFont == L"Microsoft YaHei",
          "#332: a click on the row keeps the family's en-US name for the theme");
    check(click(app.themeEditorHits, TE_CANCEL) && !app.showThemeEditor, "the theme editor cancels");
    app.showSettings = false;
    language(app, "en");
}

}  // namespace

int testFontNames() {
    failures = 0;
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Font name tests", WS_POPUP, 0, 0, 1180, 900, nullptr, nullptr, nullptr,
                               nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        std::cerr << "FAIL font names: the test window opens\n";
        return 1;
    }
    app.width = 1180;
    app.height = 900;
    applyTheme(app, 0);
    updateTextFormats(app);
    choices();
    lists(app);
    editorList(app);
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    std::cout << "Font names: " << checks << " checks, " << failures << " failures\n";
    return failures;
}
