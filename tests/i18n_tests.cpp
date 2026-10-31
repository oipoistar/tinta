#include "i18n.h"

#include <windows.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <set>
#include <regex>

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

bool identifierChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// The sources build without /utf-8, so MSVC reads them in the system code
// page. A narrow literal keeps its bytes, so the UTF-8 text in one (the
// start page's documents) passes through, but a wide literal is converted:
// a raw en dash in the go-to-line prompt's range showed as three Latin-1
// characters (#276). Wide, u8, u and U literals spell such text with \u
// escapes; comments may hold anything.
void wideLiteralsAreAscii(const std::filesystem::path& path, int& scanned) {
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ++scanned;
    int line = 1;
    auto report = [&](size_t from, size_t to) {
        for (size_t i = from; i < to; ++i) {
            if ((unsigned char)text[i] < 0x80) continue;
            const std::string message = "raw non-ASCII text in a wide literal at " +
                                        path.filename().string() + ":" + std::to_string(line);
            check(false, message.c_str());
            return;
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n') { ++line; continue; }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i + 1 < text.size() && text[i + 1] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const size_t end = text.find("*/", i + 2);
            const size_t stop = end == std::string::npos ? text.size() : end + 2;
            for (size_t j = i; j < stop; ++j) line += text[j] == '\n';
            i = stop - 1;
            continue;
        }
        // Numbers first, so 1'000 is not taken for a character literal
        if (std::isdigit((unsigned char)c) && (i == 0 || !identifierChar(text[i - 1]))) {
            while (i + 1 < text.size() && (identifierChar(text[i + 1]) || text[i + 1] == '.' || text[i + 1] == '\'')) ++i;
            continue;
        }
        if (c != '"' && c != '\'') continue;
        size_t prefix = i;
        while (prefix > 0 && identifierChar(text[prefix - 1])) --prefix;
        const std::string encoding = text.substr(prefix, i - prefix);
        if (c == '"' && !encoding.empty() && encoding.back() == 'R') {
            // R"delimiter( ... )delimiter"
            const size_t open = text.find('(', i);
            if (open == std::string::npos) break;
            const std::string close = ")" + text.substr(i + 1, open - i - 1) + "\"";
            const size_t end = text.find(close, open);
            const size_t stop = end == std::string::npos ? text.size() : end + close.size();
            if (encoding != "R") report(i, stop);
            for (size_t j = i; j < stop; ++j) line += text[j] == '\n';
            i = stop - 1;
            continue;
        }
        const size_t start = i;
        for (++i; i < text.size() && text[i] != c && text[i] != '\n'; ++i) {
            if (text[i] == '\\') ++i;
        }
        if (!encoding.empty()) report(start, std::min(i, text.size()));
        if (i < text.size() && text[i] == '\n') ++line;
    }
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
    int ja = languageIndexById("ja");
    int ko = languageIndexById("ko");
    int ru = languageIndexById("ru");
    check(ru >= 0 && languageHasTranslations(ru), "Russian ships built-in translations and is selectable");
    check(languageIndexById("RU-ru") == ru, "ru-RU aliases to Russian");
    check(std::wstring(languageNameAt(ru)) == L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439", "Russian has its native name");
    static const struct { const char* key; const wchar_t* text; } russian[] = {
#include "../src/i18n_ru.inc"
    };
    std::set<std::string> russianKeys;
    const std::wregex placeholder(L"%(?:[0-9]+\\$)?(?:\\.[0-9]+)?(?:zu|ls|[sdf])");
    auto formats = [&](const wchar_t* text) {
        std::wstring value(text);
        std::wstring result;
        for (auto it = std::wsregex_iterator(value.begin(), value.end(), placeholder);
             it != std::wsregex_iterator(); ++it) result += it->str() + L";";
        return result;
    };
    // A count lists its plural forms split by '|', as many as the
    // language's rule has; each form keeps the English argument
    const std::set<std::string> counts = {"fm.ago.minutes", "fm.ago.hours", "fm.ago.days", "fm.in.minutes",
                                          "fm.in.hours", "fm.in.days", "fm.items",
                                          "sheet.sorted", "sheet.tray", "sheet.words"};
    auto arguments = [&](const char* key, const wchar_t* text) {
        if (!counts.count(key)) return formats(text);
        std::set<std::wstring> forms;
        const std::wstring value(text);
        for (size_t start = 0;;) {
            const size_t bar = value.find(L'|', start);
            forms.insert(formats(value.substr(start, bar == std::wstring::npos ? bar : bar - start).c_str()));
            if (bar == std::wstring::npos) break;
            start = bar + 1;
        }
        std::wstring result;
        for (const auto& form : forms) result += form + L"|";
        return result;
    };
    for (const auto& entry : russian) {
        check(russianKeys.insert(entry.key).second, "Russian translation keys are unique");
        check(std::wstring(tr(ru, entry.key)) == entry.text, "Russian translation lookup resolves the built-in text");
        check(arguments(entry.key, tr(en, entry.key)) == arguments(entry.key, entry.text),
              "Russian preserves printf arguments and order");
    }
    check(russianKeys.size() == 470, "Russian covers every current interface key");
    // Every shipped language has its own text for every key, with the
    // English printf arguments in order. A few read the same everywhere
    // and stay out of the de/fr/it table: the window titles and the key
    // profiles
    const std::set<std::string> sameEverywhere = {"title.plain", "title.dirty", "title.no_file",
                                                  "settings.profile.windows", "settings.profile.vim"};
    for (int lang : {zh, ja, ko, de, fr, it, ru}) {
        for (const auto& entry : russian) {
            if (sameEverywhere.count(entry.key)) continue;
            const std::string where = languageIdAt(lang) + " " + entry.key;
            check(languageTranslatesKey(lang, entry.key), ("translated: " + where).c_str());
            check(arguments(entry.key, tr(lang, entry.key)) == arguments(entry.key, tr(en, entry.key)),
                  ("printf arguments kept: " + where).c_str());
        }
    }
    check(en == 0, "en is the first registry entry");
    check(zh == 1, "zh is the second registry entry");
    check(de >= 0, "de is registered");
    check(fr >= 0, "fr is registered");
    check(it >= 0, "it is registered");
    check(ja >= 0, "ja is registered");
    check(ko >= 0, "ko is registered");

    // The Settings dropdown lists exactly the languages that carry
    // translations; the shipped set must always qualify.
    check(languageHasTranslations(en), "en counts as translated");
    check(languageHasTranslations(zh), "zh counts as translated");
    check(languageHasTranslations(de), "de ships built-in translations");
    check(languageHasTranslations(fr), "fr ships built-in translations");
    check(languageHasTranslations(it), "it ships built-in translations");
    check(languageHasTranslations(ja), "ja ships built-in translations");
    check(languageHasTranslations(ko), "ko ships built-in translations");

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
    check(std::wstring(tr(ja, "settings.language")) == L"\u8A00\u8A9E",
          "Japanese language setting label");
    check(std::wstring(tr(ko, "settings.language")) == L"\uC5B8\uC5B4",
          "Korean language setting label");
    check(std::wstring(tr(ja, "toast.copied")) == L"\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F\uFF01",
          "Japanese copy toast");
    check(std::wstring(tr(ko, "toast.copied")) == L"\uBCF5\uC0AC\uD588\uC2B5\uB2C8\uB2E4!",
          "Korean copy toast");
    check(std::wstring(tr(zh, "theme.editor.title")) == L"\u65B0\u5EFA\u4E3B\u9898",
          "Chinese theme editor title");
    check(std::wstring(tr(ja, "theme.editor.save")) == L"\u30C6\u30FC\u30DE\u3092\u4FDD\u5B58",
          "Japanese theme editor save action");
    check(std::wstring(tr(ko, "theme.editor.color.background")) == L"\uBC30\uACBD",
          "Korean theme editor color label");
    check(std::wstring(tr(de, "theme.editor.cancel")) == L"Abbrechen",
          "German theme editor cancel action");
    check(std::wstring(tr(ja, "codeblock.copy")) == L"\u30B3\u30D4\u30FC",
          "Japanese code block copy button");
    check(std::wstring(tr(ko, "codeblock.copied")) == L"\uBCF5\uC0AC\uD588\uC2B5\uB2C8\uB2E4!",
          "Korean code block copied toast");
    check(std::wstring(tr(ja, "ctx.settings")) == L"\u8A2D\u5B9A",
          "Japanese context menu settings");
    check(std::wstring(tr(ko, "confirm.discard")) == L"\uBCC0\uACBD \uBC84\uB9AC\uAE30",
          "Korean discard confirmation");

    // Strings every language gained, and typos fixed in shipped ones
    check(std::wstring(tr(zh, "fm.section")) == L"\u5143\u6570\u636E", "Chinese frontmatter section");
    check(std::wstring(tr(ja, "fm.title")) == L"\u30C9\u30AD\u30E5\u30E1\u30F3\u30C8\u306E\u30D7\u30ED\u30D1\u30C6\u30A3", "Japanese frontmatter title");
    check(std::wstring(tr(fr, "fm.section")) == L"M\u00E9tadonn\u00E9es", "French frontmatter section");
    check(std::wstring(tr(de, "fm.up_to")) == L"H\u00F6chstens", "German frontmatter limit");
    check(std::wstring(tr(it, "rail.bold")) == L"Grassetto", "Italian bold button");
    check(std::wstring(tr(ko, "settings.word_wrap")) == L"\uC790\uB3D9 \uC904\uBC14\uAFC8", "Korean word wrap setting");
    check(std::wstring(tr(ja, "settings.footer")) == L"ESC\u3067\u9589\u3058\u308B \u2022 \u5909\u66F4\u306F\u3059\u3050\u306B\u53CD\u6620", "Japanese settings footer");
    check(std::wstring(tr(ja, "settings.section.editor")) == L"\u30A8\u30C7\u30A3\u30BF\u30FC", "Japanese editor section");

    // Keys left out of the built-in table fall back to English
    check(std::wstring(tr(de, "title.no_file")) == L"Tinta",
          "untranslated key falls back to English");

    // Quick-note strings (Ctrl+N) ship translated too
    check(std::wstring(tr(de, "title.untitled")) == L"Unbenannt",
          "German untitled title");
    check(std::wstring(tr(it, "help.general.new_note")) == L"Nuova nota rapida",
          "Italian quick-note help row");

    // Keeping deleted files in memory reads in every shipped language (#283)
    for (int lang : {zh, ja, ko, de, fr, it, ru}) {
        for (const char* key : {"settings.keep_deleted_tabs", "settings.keep_deleted_tabs.hint"}) {
            check(std::wstring(tr(lang, key)) != tr(en, key), "the deleted-files setting is translated");
        }
    }
    check(std::wstring(tr(zh, "settings.keep_deleted_tabs")) ==
              L"\u5C06\u5DF2\u5220\u9664\u6587\u4EF6\u4FDD\u7559\u5728\u5185\u5B58\u4E2D",
          "Chinese deleted-files setting");
    check(std::wstring(tr(fr, "settings.keep_deleted_tabs.hint")) ==
              L"Consommation de RAM accrue jusqu'\u00E0 la fermeture de Tinta",
          "French deleted-files hint");

    // search.match_count is the one runtime-printf'd string: the fixed
    // argument order (%d current, %zu total) must survive translation
    for (int lang : {de, fr, it}) {
        const wchar_t* fmt = tr(lang, "search.match_count");
        check(contains(fmt, L"%d"), "match_count keeps %d");
        check(contains(fmt, L"%zu"), "match_count keeps %zu");
    }

    // Counts take the form of the language's plural rule: the property
    // strip's relative dates and list counts
    check(trCount(en, "fm.ago.days", 1) == L"1 day ago" && trCount(en, "fm.ago.days", 5) == L"5 days ago",
          "English counts are singular for one");
    check(trCount(en, "fm.items", 1) == L"1 item" && trCount(en, "fm.items", 0) == L"0 items",
          "English list count");
    check(trCount(de, "fm.ago.days", 1) == L"vor 1 Tag" && trCount(de, "fm.in.days", 3) == L"in 3 Tagen",
          "German counts");
    check(trCount(fr, "fm.ago.hours", 1) == L"il y a 1 heure" && trCount(fr, "fm.ago.hours", 2) == L"il y a 2 heures",
          "French counts");
    const struct { long long count; const wchar_t* text; } russianDays[] = {
        {1, L"1 \u0434\u0435\u043D\u044C \u043D\u0430\u0437\u0430\u0434"},
        {2, L"2 \u0434\u043D\u044F \u043D\u0430\u0437\u0430\u0434"},
        {5, L"5 \u0434\u043D\u0435\u0439 \u043D\u0430\u0437\u0430\u0434"},
        {11, L"11 \u0434\u043D\u0435\u0439 \u043D\u0430\u0437\u0430\u0434"},
        {21, L"21 \u0434\u0435\u043D\u044C \u043D\u0430\u0437\u0430\u0434"},
        {22, L"22 \u0434\u043D\u044F \u043D\u0430\u0437\u0430\u0434"},
        {112, L"112 \u0434\u043D\u0435\u0439 \u043D\u0430\u0437\u0430\u0434"},
        {124, L"124 \u0434\u043D\u044F \u043D\u0430\u0437\u0430\u0434"},
    };
    for (const auto& c : russianDays) {
        check(trCount(ru, "fm.ago.days", c.count) == c.text, "Russian counts take one, few or many");
    }
    check(trCount(ja, "fm.ago.days", 1) == L"1 \u65E5\u524D" && trCount(ja, "fm.ago.days", 3) == L"3 \u65E5\u524D",
          "Japanese counts have one form");
    // Text that falls back to English is counted by English's rule
    const int es = languageIndexById("es");
    check(es >= 0 && trCount(es, "fm.ago.days", 21) == L"21 days ago", "an untranslated count reads as English");

    // Dates follow the user's regional format when it is in the interface
    // language, else the language's own
    check(dateLocaleFor("en", L"en-GB") == L"en-GB", "English dates follow a British region");
    check(dateLocaleFor("en", L"de-DE") == L"en-US", "a region in another language gives the language's own");
    check(dateLocaleFor("de", L"de-AT") == L"de-AT", "German dates follow an Austrian region");
    check(dateLocaleFor("ja", L"") == L"ja-JP", "Japanese dates resolve to Japan");
    check(dateLocaleFor("zh-tw", L"zh-CN") == L"zh-TW", "a language with its own region keeps it");
    // A long date loses its weekday and abbreviates its month; quoted text
    // and the order stay as the locale has them
    const struct { const wchar_t* from; const wchar_t* to; } pictures[] = {
        {L"dddd, MMMM d, yyyy", L"MMM d, yyyy"},
        {L"dd MMMM yyyy", L"d MMM yyyy"},
        {L"dddd, d. MMMM yyyy", L"d. MMM yyyy"},
        {L"dddd d MMMM yyyy", L"d MMM yyyy"},
        {L"d MMMM yyyy '\u0433.'", L"d MMM yyyy '\u0433.'"},
        {L"yyyy'\u5E74'M'\u6708'd'\u65E5'", L"yyyy'\u5E74'M'\u6708'd'\u65E5'"},
        {L"yyyy'\uB144' M'\uC6D4' d'\uC77C' dddd", L"yyyy'\uB144' M'\uC6D4' d'\uC77C'"},
        {L"dddd, d' de 'MMMM' de 'yyyy", L"d' de 'MMM' de 'yyyy"},
        {L"d MMMM yyyy dddd", L"d MMM yyyy"},
        {L"'den 'd MMMM yyyy", L"'den 'd MMM yyyy"},
        {L"d 'o''clock' MMMM", L"d 'o''clock' MMM"},
        {L"MMMM d", L"MMM d"},
        {L"d. MMMM", L"d. MMM"},
    };
    for (const auto& p : pictures) check(shortDatePicture(p.from) == p.to, "a short date picture");
    check(formatDate(L"en-US", 2026, 1, 3) == L"Jan 3, 2026", "US English date");
    check(formatDate(L"en-US", 2026, 1, 3, false) == L"Jan 3", "US English month and day");
    check(formatDate(L"en-GB", 2026, 1, 3) == L"3 Jan 2026", "British English date");
    check(formatDate(L"ja-JP", 2026, 1, 3) == L"2026\u5E741\u67083\u65E5", "Japanese date");
    check(formatDate(L"ja-JP", 2026, 1, 3, false) == L"1\u67083\u65E5", "Japanese month and day");
    check(formatDate(L"ko-KR", 2026, 1, 3) == L"2026\uB144 1\uC6D4 3\uC77C", "Korean date without its weekday");
    check(formatDate(L"ko-KR", 2026, 1, 3, false) == L"1\uC6D4 3\uC77C", "Korean month and day");
    // The rest name the month as the locale abbreviates it, and 3 January
    // 2026, a Saturday, has no weekday
    for (const wchar_t* locale : {L"de-DE", L"fr-FR", L"it-IT", L"ru-RU", L"zh-CN"}) {
        wchar_t saturday[80] = {}, january[80] = {};
        const bool names = GetLocaleInfoEx(locale, LOCALE_SDAYNAME6, saturday, 80) > 1 &&
                           GetLocaleInfoEx(locale, LOCALE_SABBREVMONTHNAME1, january, 80) > 1;
        const std::wstring date = formatDate(locale, 2026, 1, 3);
        check(names && date.find(L"2026") != std::wstring::npos && date.find(january) != std::wstring::npos &&
                  date.find(saturday) == std::wstring::npos,
              "a date names its month without the weekday");
    }

    int scanned = 0;
    for (const char* folder : {"src", "include"}) {
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(TINTA_SOURCE_DIR) / folder)) {
            const auto extension = entry.path().extension();
            if (extension == ".cpp" || extension == ".h" || extension == ".inc") wideLiteralsAreAscii(entry.path(), scanned);
        }
    }
    check(scanned > 50, "the source scan finds the sources");

    if (failures == 0) {
        std::cout << "i18n tests passed\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
