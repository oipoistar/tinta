#include "i18n.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

bool contains(const wchar_t* haystack, const wchar_t* needle) {
    return std::wstring(haystack).find(needle) != std::wstring::npos;
}

} // namespace

int main() {
    // Registry + built-in override seeding (a user languages.ini may add
    // languages on a dev machine, but can only add translations, so every
    // positive assertion below holds regardless of local state).
    loadLanguageOverrides();

    int en = languageIndexById("en");
    int zh = languageIndexById("zh");
    int de = languageIndexById("de");
    int fr = languageIndexById("fr");
    int it = languageIndexById("it");
    check(en == 0, "en is the first registry entry");
    check(zh == 1, "zh is the second registry entry");
    check(de >= 0, "de is registered");
    check(fr >= 0, "fr is registered");
    check(it >= 0, "it is registered");

    // The Settings dropdown lists exactly the languages that carry
    // translations; the shipped set must always qualify.
    check(languageHasTranslations(en), "en counts as translated");
    check(languageHasTranslations(zh), "zh counts as translated");
    check(languageHasTranslations(de), "de ships built-in translations");
    check(languageHasTranslations(fr), "fr ships built-in translations");
    check(languageHasTranslations(it), "it ships built-in translations");

    // Alias normalization onto compiled languages
    check(languageIndexById("zh-CN") == zh, "zh-CN aliases to zh");
    check(languageIndexById("EN-us") == en, "en-US aliases to en");

    // Spot-check the shipped strings (a local languages.ini could override
    // these keys in theory; the template it seeds keeps every key commented)
    check(std::wstring(tr(de, "settings.title")) == L"Einstellungen",
          "German settings title");
    check(std::wstring(tr(fr, "settings.title")) == L"Param\u00E8tres",
          "French settings title");
    check(std::wstring(tr(it, "ctx.settings")) == L"Impostazioni",
          "Italian settings menu item");

    // Keys left out of the built-in table fall back to English
    check(std::wstring(tr(de, "title.no_file")) == L"Tinta",
          "untranslated key falls back to English");

    // search.match_count is the one runtime-printf'd string: the fixed
    // argument order (%d current, %zu total) must survive translation
    for (int lang : {de, fr, it}) {
        const wchar_t* fmt = tr(lang, "search.match_count");
        check(contains(fmt, L"%d"), "match_count keeps %d");
        check(contains(fmt, L"%zu"), "match_count keeps %zu");
    }

    if (failures == 0) {
        std::cout << "i18n tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
