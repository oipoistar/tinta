#include "i18n.h"
#include "app.h"

#include <windows.h>
#include <string.h>
#include <cstring>

// Language metadata. nativeFont is a hint — the chooser uses it so each
// language's name shows in its native typeface. Empty string falls back to
// the theme font (the font fallback chain already handles CJK glyphs).
const Language LANGUAGES[LANG_COUNT] = {
    { L"English",                L"English",                     L"" },
    { L"\u7B80\u4F53\u4E2D\u6587", L"Chinese (Simplified)",       L"Microsoft YaHei UI" },
    { L"\u65E5\u672C\u8A9E",       L"Japanese",                   L"Yu Gothic UI" },
    { L"\uD55C\uAD6D\uC5B4",       L"Korean",                     L"Malgun Gothic" },
};

// Translation table.
//
// Every key MUST have an English entry — English is the fallback when a
// language lacks a translation. The zh/ja/ko columns may be nullptr to mean
// "fall back to English". Once a translation is added it should never be
// removed (only amended); keys are stable identifiers referenced across the
// codebase.
//
// Conventions:
//   - Keys use dotted lowercase namespaces: help.title, toast.saved, ...
//   - Format strings use ordinary printf-style placeholders. MSVC does not
//     support POSIX positional placeholders, so translated strings keep the
//     argument order used by the call site.
//   - Em dash (U+2014) is written as a literal L"\u2014" for readability.
namespace {

struct Entry {
    const char* key;
    const wchar_t* en;
    const wchar_t* zh;  // Simplified Chinese
    const wchar_t* ja;  // Japanese (may be null -> fallback to en)
    const wchar_t* ko;  // Korean   (may be null -> fallback to en)
};

// Note: kept as a plain array (not unordered_map) on purpose. ~80 entries,
// linear scan is cache-friendly and avoids a static-init ordering fiasco.
const Entry kEntries[] = {
    // ----- Help overlay -----
    { "help.title",            L"Keyboard Shortcuts",                       L"\u952E\u76D8\u5FEB\u6377\u952E",                       L"\u30AD\u30FC\u30DC\u30FC\u30C9\u30B7\u30E7\u30FC\u30C8\u30AB\u30C3\u30C8", L"\uD0A4\uBCF4\uB4DC \uB2E8\uCD95\uD0A4" },
    { "help.section.navigation", L"NAVIGATION",                             L"\u5BFC\u822A",                                          L"\u30CA\u30D3\u30B2\u30FC\u30B7\u30E7\u30F3", L"\uD0D0\uC0C9" },
    { "help.section.view",     L"VIEW",                                     L"\u89C6\u56FE",                                          L"\u8868\u793A", L"\uBCF4\uAE30" },
    { "help.section.editing",  L"EDITING",                                  L"\u7F16\u8F91",                                          L"\u7DE8\u96C6", L"\uD3B8\uC9D1" },
    { "help.section.general",  L"GENERAL",                                  L"\u5E38\u89C4",                                          L"\u4E00\u822C", L"\uC77C\uBC18" },
    { "help.footer",           L"Press ESC or ? to close",                  L"\u6309 ESC \u6216 ? \u5173\u95ED",                      L"ESC \u307E\u305F\u306F ? \u3067\u9589\u3058\u308B", L"ESC \uB610\uB294 ?\uB97C \uB20C\uB7EC \uB2EB\uAE30" },

    // Help entries: only the description column is translated; keys (Ctrl+S etc.) stay verbatim.
    { "help.nav.scroll_down",      L"Scroll down",                  L"\u5411\u4E0B\u6EDA\u52A8",                          L"\u4E0B\u306B\u30B9\u30AF\u30ED\u30FC\u30EB", L"\uC544\uB798\uB85C \uC2A4\uD06C\uB864" },
    { "help.nav.scroll_up",        L"Scroll up",                    L"\u5411\u4E0A\u6EDA\u52A8",                          L"\u4E0A\u306B\u30B9\u30AF\u30ED\u30FC\u30EB", L"\uC704\uB85C \uC2A4\uD06C\uB864" },
    { "help.nav.page_down",        L"Page down",                    L"\u5411\u4E0B\u7FFB\u9875",                          L"\u4E0B\u306B\u30DA\u30FC\u30B8\u79FB\u52D5", L"\uD398\uC774\uC9C0 \uB0B4\uB9AC\uAE30" },
    { "help.nav.page_up",          L"Page up",                      L"\u5411\u4E0A\u7FFB\u9875",                          L"\u4E0A\u306B\u30DA\u30FC\u30B8\u79FB\u52D5", L"\uD398\uC774\uC9C0 \uC62C\uB9AC\uAE30" },
    { "help.nav.jump_start_end",   L"Jump to start / end",          L"\u8DF3\u8F6C\u5230\u5F00\u5934 / \u7ED3\u5C3E",     L"\u958B\u59CB / \u7D42\u4E86\u306B\u79FB\u52D5", L"\uC2DC\uC791 / \uB05D\uC73C\uB85C \uC774\uB3D9" },
    { "help.nav.zoom",             L"Zoom in / out",                L"\u653E\u5927 / \u7F29\u5C0F",                       L"\u62E1\u5927 / \u7E2E\u5C0F", L"\uD655\uB300 / \uCD95\uC18C" },

    { "help.view.search",          L"Search",                       L"\u641C\u7D22",                                      L"\u691C\u7D22", L"\uAC80\uC0C9" },
    { "help.view.next_match",      L"Next search match",            L"\u4E0B\u4E00\u4E2A\u5339\u914D\u7ED3\u679C",        L"\u6B21\u306E\u691C\u7D22\u7D50\u679C", L"\uB2E4\uC74C \uAC80\uC0C9 \uACB0\uACFC" },
    { "help.view.folder_browser",  L"Toggle folder browser",        L"\u5207\u6362\u6587\u4EF6\u5939\u6D4F\u89C8",        L"\u30D5\u30A9\u30EB\u30C0\u30FC\u30D6\u30E9\u30A6\u30B6\u30FC\u3092\u5207\u308A\u66FF\u3048", L"\uD3F4\uB354 \uBE0C\uB77C\uC6B0\uC800 \uC804\uD658" },
    { "help.view.toc",             L"Toggle table of contents",     L"\u5207\u6362\u76EE\u5F55",                          L"\u76EE\u6B21\u3092\u5207\u308A\u66FF\u3048", L"\uBAA9\uCC28 \uC804\uD658" },
    { "help.view.theme",           L"Theme chooser",                L"\u4E3B\u9898\u9009\u62E9\u5668",                    L"\u30C6\u30FC\u30DE\u9078\u629E", L"\uD14C\uB9C8 \uC120\uD0DD\uAE30" },
    { "help.view.stats",           L"Toggle stats",                 L"\u5207\u6362\u7EDF\u8BA1\u4FE1\u606F",              L"\u7D71\u8A08\u3092\u5207\u308A\u66FF\u3048", L"\uD1B5\uACC4 \uC804\uD658" },
    { "help.view.help",            L"This help",                    L"\u672C\u5E2E\u52A9",                                L"\u3053\u306E\u30D8\u30EB\u30D7", L"\uC774 \uB3C4\uC6C0\uB9D0" },

    { "help.edit.enter_edit",      L"Enter edit mode",              L"\u8FDB\u5165\u7F16\u8F91\u6A21\u5F0F",              L"\u7DE8\u96C6\u30E2\u30FC\u30C9\u306B\u5165\u308B", L"\uD3B8\uC9D1 \uBAA8\uB4DC \uC9C4\uC785" },
    { "help.edit.save",            L"Save (in edit mode)",          L"\u4FDD\u5B58\uFF08\u7F16\u8F91\u6A21\u5F0F\u4E0B\uFF09", L"\u4FDD\u5B58\uFF08\u7DE8\u96C6\u30E2\u30FC\u30C9\uFF09", L"\uC800\uC7A5 (\uD3B8\uC9D1 \uBAA8\uB4DC)" },
    { "help.edit.preview",         L"Show / hide preview pane",     L"\u663E\u793A / \u9690\u85CF\u9884\u89C8\u7A97\u683C", L"\u30D7\u30EC\u30D3\u30E5\u30FC\u30DA\u30A4\u30F3\u3092\u8868\u793A / \u975E\u8868\u793A", L"\uBBF8\uB9AC\uBCF4\uAE30 \uD328\uB110 \uD45C\uC2DC / \uC228\uAE40" },
    { "help.edit.word_wrap",       L"Toggle word wrap",             L"\u5207\u6362\u81EA\u52A8\u6362\u884C",              L"\u6298\u308A\u8FD4\u3057\u3092\u5207\u308A\u66FF\u3048", L"\uC790\uB3D9 \uC904\uBC14\uAFC0 \uC804\uD658" },
    { "help.edit.exit_edit",       L"Exit edit mode",               L"\u9000\u51FA\u7F16\u8F91\u6A21\u5F0F",              L"\u7DE8\u96C6\u30E2\u30FC\u30C9\u3092\u7D42\u4E86", L"\uD3B8\uC9D1 \uBAA8\uB4DC \uC885\uB8CC" },

    { "help.general.select_all",   L"Select all text",              L"\u5168\u9009\u6587\u672C",                          L"\u3059\u3079\u3066\u306E\u30C6\u30AD\u30B9\u30C8\u3092\u9078\u629E", L"\uBAA8\uB4E0 \uD14D\uC2A4\uD2B8 \uC120\uD0DD" },
    { "help.general.copy",         L"Copy selection",               L"\u590D\u5236\u9009\u4E2D\u5185\u5BB9",              L"\u9078\u629E\u7BC4\u56F2\u3092\u30B3\u30D4\u30FC", L"\uC120\uD0DD \uB0B4\uC6A9 \uBCF5\uC0AC" },
    { "help.general.new_note",     L"New quick note",               L"\u65B0\u5EFA\u5FEB\u901F\u7B14\u8BB0",              L"\u65B0\u3057\u3044\u30AF\u30A4\u30C3\u30AF\u30CE\u30FC\u30C8", L"\uC0C8 \uBE60\uB978 \uBA54\uBAA8" },
    { "help.general.close",        L"Close overlay / Quit",         L"\u5173\u95ED\u6D6E\u5C42 / \u9000\u51FA",           L"\u30AA\u30FC\u30D0\u30FC\u30EC\u30A4\u3092\u9589\u3058\u308B / \u7D42\u4E86", L"\uC624\uBC84\uB808\uC774 \uB2EB\uAE30 / \uC885\uB8CC" },
    { "help.general.quit",         L"Quit",                         L"\u9000\u51FA",                                      L"\u7D42\u4E86", L"\uC885\uB8CC" },

    // ----- Search overlay -----
    { "search.placeholder",    L"Search...",                                L"\u641C\u7D22...",                                      L"\u691C\u7D22...", L"\uAC80\uC0C9..." },
    { "search.no_matches",     L"No matches",                               L"\u65E0\u5339\u914D",                                   L"\u4E00\u81F4\u3059\u308B\u7D50\u679C\u306A\u3057", L"\uAC80\uC0C9 \uACB0\uACFC \uC5C6\uC74C" },
    // Fixed argument order (current, total): MSVC's printf family has no
    // positional specifiers, so locales phrase around the same order
    { "search.match_count",    L"%d of %zu",                                L"\u7B2C %d / %zu \u4E2A",                       L"%d / %zu \u4EF6", L"%zu\uAC1C \uC911 %d\uBC88\uC9F8" },

    // ----- Table of contents -----
    { "toc.title",             L"Contents",                                 L"\u76EE\u5F55",                                         L"\u76EE\u6B21", L"\uBAA9\uCC28" },
    { "toc.empty",             L"No headings",                              L"\u65E0\u6807\u9898",                                   L"\u898B\u51FA\u3057\u306A\u3057", L"\uD5E4\uB529 \uC5C6\uC74C" },

    // ----- Theme chooser -----
    { "theme.chooser.title",   L"Choose Theme",                             L"\u9009\u62E9\u4E3B\u9898",                             L"\u30C6\u30FC\u30DE\u3092\u9078\u629E",                       L"\uD14C\uB9C8 \uC120\uD0DD" },
    { "theme.chooser.light",   L"LIGHT THEMES",                             L"\u6D45\u8272\u4E3B\u9898",                             L"\u30E9\u30A4\u30C8\u30C6\u30FC\u30DE",                       L"\uBC1D\uC740 \uD14C\uB9C8" },
    { "theme.chooser.dark",    L"DARK THEMES",                              L"\u6DF1\u8272\u4E3B\u9898",                             L"\u30C0\u30FC\u30AF\u30C6\u30FC\u30DE",                       L"\uC5B4\uB450\uC6B4 \uD14C\uB9C8" },

    // ----- Language chooser -----
    { "lang.chooser.title",    L"Choose Language",                          L"\u9009\u62E9\u8BED\u8A00",                             L"\u8A00\u8A9E\u3092\u9078\u629E",                         L"\uC5B8\uC5B4 \uC120\uD0DD" },
    { "lang.chooser.footer",   L"Press ESC to close",                       L"\u6309 ESC \u5173\u95ED",                              L"ESC\u3067\u9589\u3058\u308B",                         L"ESC\uB97C \uB20C\uB7EC \uB2EB\uAE30" },

    // ----- Toast / editor notifications -----
    { "toast.no_file",         L"No file loaded",                           L"\u672A\u52A0\u8F7D\u6587\u4EF6",                       L"\u30D5\u30A1\u30A4\u30EB\u304C\u8AAD\u307F\u8FBC\u307E\u308C\u3066\u3044\u307E\u305B\u3093", L"\uD30C\uC77C\uC774 \uB85C\uB4DC\uB418\uC9C0 \uC54A\uC74C" },
    { "toast.exit_edit_hint",  L"Press Esc to exit edit mode",        L"\u6309 ESC \u9000\u51FA\u7F16\u8F91\u6A21\u5F0F", L"ESC\u3067\u7DE8\u96C6\u30E2\u30FC\u30C9\u3092\u7D42\u4E86", L"ESC\uB97C \uB20C\uB7EC \uD3B8\uC9D1 \uBAA8\uB4DC \uC885\uB8CC" },
    { "toast.unsaved_exit",    L"Unsaved changes! Y = save & exit, N = discard, ESC = cancel",
                                L"\u6709\u672A\u4FDD\u5B58\u7684\u66F4\u6539\uFF01Y = \u4FDD\u5B58\u5E76\u9000\u51FA\uFF0CN = \u653E\u5F03\uFF0CESC = \u53D6\u6D88",
                                 L"\u672A\u4FDD\u5B58\u306E\u5909\u66F4\u304C\u3042\u308A\u307E\u3059\uFF01Y = \u4FDD\u5B58\u3057\u3066\u7D42\u4E86\u3001N = \u7834\u68C4\u3001ESC = \u30AD\u30E3\u30F3\u30BB\u30EB", L"\uC800\uC7A5\uD558\uC9C0 \uC54A\uC740 \uBCC0\uACBD\uC774 \uC788\uC2B5\uB2C8\uB2E4! Y = \uC800\uC7A5 \uD6C4 \uC885\uB8CC, N = \uBC84\uB9AC\uAE30, ESC = \uCDE8\uC18C" },
    { "toast.saved",           L"Saved!",                                   L"\u5DF2\u4FDD\u5B58\uFF01",                             L"\u4FDD\u5B58\u3057\u307E\u3057\u305F\uFF01",             L"\uC800\uC7A5\uD588\uC2B5\uB2C8\uB2E4!" },
    { "toast.save_failed",     L"Save failed \u2014 file may be locked or read-only",
                                L"\u4FDD\u5B58\u5931\u8D25 \u2014 \u6587\u4EF6\u53EF\u80FD\u88AB\u9501\u5B9A\u6216\u4E3A\u53EA\u8BFB",
                                L"\u4FDD\u5B58\u306B\u5931\u6557\u3057\u307E\u3057\u305F \u2014 \u30D5\u30A1\u30A4\u30EB\u304C\u30ED\u30C3\u30AF\u3055\u308C\u3066\u3044\u308B\u304B\u8AAD\u307F\u53D6\u308A\u5C02\u7528\u3067\u3059", L"\uC800\uC7A5 \uC2E4\uD328 \u2014 \uD30C\uC77C\uC774 \uC7A0\uACA8 \uC788\uAC70\uB098 \uC77D\uAE30 \uC804\uC6A9\uC785\uB2C8\uB2E4" },
    { "toast.exit_cancelled",  L"Exit cancelled",                           L"\u5DF2\u53D6\u6D88\u9000\u51FA",                       L"\u7D42\u4E86\u3092\u30AD\u30E3\u30F3\u30BB\u30EB\u3057\u307E\u3057\u305F", L"\uC885\uB8CC \uCDE8\uC18C" },
    { "toast.exit_confirm",    L"Press ESC again to exit edit mode",        L"\u518D\u6309\u4E00\u6B21 ESC \u9000\u51FA\u7F16\u8F91\u6A21\u5F0F", L"ESC\u3092\u3082\u3046\u4E00\u5EA6\u62BC\u3057\u3066\u7DE8\u96C6\u30E2\u30FC\u30C9\u3092\u7D42\u4E86", L"ESC\uB97C \uB2E4\uC2DC \uB20C\uB7EC \uD3B8\uC9D1 \uBAA8\uB4DC \uC885\uB8CC" },
    { "toast.preview_shown",   L"Preview shown (Ctrl+E to hide)",           L"\u5DF2\u663E\u793A\u9884\u89C8\uFF08Ctrl+E \u9690\u85CF\uFF09", L"\u30D7\u30EC\u30D3\u30E5\u30FC\u3092\u8868\u793A\u3057\u307E\u3057\u305F (Ctrl+E\u3067\u975E\u8868\u793A)", L"\uBBF8\uB9AC\uBCF4\uAE30 \uD45C\uC2DC (Ctrl+E\uB85C \uC228\uAE30\uAE30)" },
    { "toast.preview_hidden",  L"Preview hidden (Ctrl+E to show)",          L"\u5DF2\u9690\u85CF\u9884\u89C8\uFF08Ctrl+E \u663E\u793A\uFF09", L"\u30D7\u30EC\u30D3\u30E5\u30FC\u3092\u975E\u8868\u793A\u306B\u3057\u307E\u3057\u305F (Ctrl+E\u3067\u8868\u793A)", L"\uBBF8\uB9AC\uBCF4\uAE30 \uC228\uAE40 (Ctrl+E\uB85C \uD45C\uC2DC)" },
    { "toast.wrap_on",         L"Word wrap on (Ctrl+W to turn off)",        L"\u5DF2\u5F00\u542F\u81EA\u52A8\u6362\u884C\uFF08Ctrl+W \u5173\u95ED\uFF09", L"\u81EA\u52D5\u6539\u884C\u3092\u30AA\u30F3\u306B\u3057\u307E\u3057\u305F (Ctrl+W\u3067\u30AA\u30D5)", L"\uC790\uB3D9 \uC904\uBC14\uAFC0 \uCF1C\uAE30 (Ctrl+W\uB85C \uB044\uAE30)" },
    { "toast.wrap_off",        L"Word wrap off (Ctrl+W to turn on)",        L"\u5DF2\u5173\u95ED\u81EA\u52A8\u6362\u884C\uFF08Ctrl+W \u5F00\u542F\uFF09", L"\u81EA\u52D5\u6539\u884C\u3092\u30AA\u30D5\u306B\u3057\u307E\u3057\u305F (Ctrl+W\u3067\u30AA\u30F3)", L"\uC790\uB3D9 \uC904\uBC14\uAFC0 \uB044\uAE30 (Ctrl+W\uB85C \uCF1C\uAE30)" },
    { "toast.copied",          L"Copied!",                                  L"\u5DF2\u590D\u5236\uFF01",                             L"\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F\uFF01",             L"\uBCF5\uC0AC\uD588\uC2B5\uB2C8\uB2E4!" },

    // Code block hover button + confirmation pill
    { "codeblock.copy",        L"Copy",                                     L"\u590D\u5236",                                         L"\u30B3\u30D4\u30FC",                              L"\uBCF5\uC0AC" },
    { "codeblock.copied",      L"Copied!",                                  L"\u5DF2\u590D\u5236\uFF01",                             L"\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F\uFF01",   L"\uBCF5\uC0AC\uD588\uC2B5\uB2C8\uB2E4!" },

    // ----- Stats overlay -----
    // Two-line format. Labels translatable; D2D/DWrite kept as technical abbreviations.
    // Placeholders are consumed in order: parse/layout/draw calls, then
    // startup/window/D2D/DWrite/file timings.
    { "stats.line1",           L"Parse: %zu us | Layout: %zu us | Draw calls: %zu",
                               L"\u89E3\u6790: %zu us | \u5E03\u5C40: %zu us | \u7ED8\u5236\u8C03\u7528: %zu",
                               L"\u89E3\u6790: %zu us | \u30EC\u30A4\u30A2\u30A6\u30C8: %zu us | \u63CF\u753B\u547C\u3073\u51FA\u3057: %zu",
                               L"\uBD84\uC11D: %zu us | \uB808\uC774\uC544\uC6C3: %zu us | \uADF8\uB9AC\uAE30 \uD638\uCD9C: %zu" },
    { "stats.line2",           L"Startup: %.1fms (Win: %.1f | D2D: %.1f | DWrite: %.1f | File: %.1f)",
                               L"\u542F\u52A8: %.1fms (\u7A97\u53E3: %.1f | D2D: %.1f | DWrite: %.1f | \u6587\u4EF6: %.1f)",
                               L"\u8D77\u52D5: %.1fms (\u30A6\u30A3\u30F3\u30C9\u30A6: %.1f | D2D: %.1f | DWrite: %.1f | \u30D5\u30A1\u30A4\u30EB: %.1f)",
                               L"\uC2DC\uC791: %.1fms (\uCC3D: %.1f | D2D: %.1f | DWrite: %.1f | \uD30C\uC77C: %.1f)" },

    // ----- File-association dialogs (shared by settings.cpp and main_d2d.cpp /register) -----
    { "fileassoc.title",                L"Tinta - File Association",                 L"Tinta - \u6587\u4EF6\u5173\u8054",                              L"Tinta - \u30D5\u30A1\u30A4\u30EB\u95A2\u9023\u4ED8\u3051", L"Tinta - \uD30C\uC77C \uC5F0\uACB0" },
    { "fileassoc.packaged.title",       L"Tinta - Default apps",                     L"Tinta - \u9ED8\u8BA4\u5E94\u7528",                              L"Tinta - \u65E2\u5B9A\u306E\u30A2\u30D7\u30EA", L"Tinta - \uAE30\uBCF8 \uC571" },
    { "fileassoc.packaged.ask_body",
        L"Would you like to make Tinta the default viewer for Markdown and Mermaid files?\n\nWindows will open Settings on Tinta's page \u2014 set .md, .markdown, and .mmd to Tinta Markdown Viewer.",
        L"\u662F\u5426\u5C06 Tinta \u8BBE\u4E3A Markdown \u548C Mermaid \u6587\u4EF6\u7684\u9ED8\u8BA4\u67E5\u770B\u5668\uFF1F\n\nWindows \u5C06\u6253\u5F00 Tinta \u7684\u8BBE\u7F6E\u9875\uFF0C\u8BF7\u5C06 .md\u3001.markdown \u548C .mmd \u8BBE\u4E3A Tinta Markdown Viewer", L"Tinta\u3092 Markdown \u3068 Mermaid \u30D5\u30A1\u30A4\u30EB\u306E\u65E2\u5B9A\u30D3\u30E5\u30FC\u30A2\u306B\u3057\u307E\u3059\u304B\uFF1F\n\nWindows \u306E\u8A2D\u5B9A\u3067 .md\u3001.markdown\u3001.mmd \u3092 Tinta Markdown Viewer \u306B\u8A2D\u5B9A\u3067\u304D\u307E\u3059\u3002", L"Tinta\uB97C Markdown \uBC0F Mermaid \uD30C\uC77C\uC758 \uAE30\uBCF8 \uBDF0\uC5B4\uB85C \uC124\uC815\uD560\uAE4C\uC694?\n\nWindows \uC124\uC815\uC5D0\uC11C .md, .markdown, .mmd\uB97C Tinta Markdown Viewer\uB85C \uC9C0\uC815\uD560 \uC218 \uC788\uC2B5\uB2C8\uB2E4." },
    { "fileassoc.add_mmd_failed_body",  L"Failed to add the .mmd file association. Run tinta.exe /register to try again.",
                                         L"\u6DFB\u52A0 .mmd \u6587\u4EF6\u5173\u8054\u5931\u8D25\u3002\u8BF7\u8FD0\u884C tinta.exe /register \u91CD\u8BD5\u3002",
                                         L".mmd \u30D5\u30A1\u30A4\u30EB\u306E\u95A2\u9023\u4ED8\u3051\u306B\u5931\u6557\u3057\u307E\u3057\u305F\u3002tinta.exe /register \u3092\u5B9F\u884C\u3057\u3066\u518D\u8A66\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002", L".mmd \uD30C\uC77C \uC5F0\uACB0 \uC2E4\uD328. tinta.exe /register\uB97C \uC2E4\uD589\uD574 \uB2E4\uC2DC \uC2DC\uB3C4\uD558\uC138\uC694." },
    { "fileassoc.ask_body",
        L"Would you like to set Tinta as the default viewer for Markdown and Mermaid files?\n\nWindows will open Settings where you can select Tinta.",
        L"\u662F\u5426\u5C06 Tinta \u8BBE\u4E3A Markdown \u548C Mermaid \u6587\u4EF6\u7684\u9ED8\u8BA4\u67E5\u770B\u5668\uFF1F\n\nWindows \u5C06\u6253\u5F00\u8BBE\u7F6E\uFF0C\u60A8\u53EF\u5728\u5176\u4E2D\u9009\u62E9 Tinta\u3002",
         L"Tinta\u3092 Markdown \u3068 Mermaid \u30D5\u30A1\u30A4\u30EB\u306E\u65E2\u5B9A\u30D3\u30E5\u30FC\u30A2\u306B\u3057\u307E\u3059\u304B\uFF1F\n\nWindows \u306E\u8A2D\u5B9A\u3067 .md\u3001.markdown\u3001.mmd \u3092 Tinta Markdown Viewer \u306B\u8A2D\u5B9A\u3067\u304D\u307E\u3059\u3002", L"Tinta\uB97C Markdown \uBC0F Mermaid \uD30C\uC77C\uC758 \uAE30\uBCF8 \uBDF0\uC5B4\uB85C \uC124\uC815\uD560\uAE4C\uC694?\n\nWindows \uC124\uC815\uC5D0\uC11C .md, .markdown, .mmd\uB97C Tinta Markdown Viewer\uB85C \uC9C0\uC815\uD560 \uC218 \uC788\uC2B5\uB2C8\uB2E4." },
    { "fileassoc.done_title",   L"Almost done!",                              L"\u5C31\u5FEB\u5B8C\u6210\u4E86\uFF01",                          L"\u3042\u3068\u5C11\u3057\u3067\u3059\uFF01", L"\uAC70\uC758 \uB05D\uB0AC\uC2B5\uB2C8\uB2E4!" },
    { "fileassoc.done_body",
        L"Tinta has been registered.\n\nIn the Settings window that opens:\n1. Search for '.md' or '.mmd'\n2. Click on the current default app\n3. Select 'Tinta' from the list",
        L"Tinta \u5DF2\u6CE8\u518C\u3002\n\n\u5728\u6253\u5F00\u7684\u8BBE\u7F6E\u7A97\u53E3\u4E2D\uFF1A\n1. \u641C\u7D22 \u201C.md\u201D \u6216 \u201C.mmd\u201D\n2. \u70B9\u51FB\u5F53\u524D\u9ED8\u8BA4\u5E94\u7528\n3. \u4ECE\u5217\u8868\u4E2D\u9009\u62E9 \u201CTinta\u201D",
         L"Tinta\u3092\u767B\u9332\u3057\u307E\u3057\u305F\u3002\n\n\u958B\u3044\u305F\u8A2D\u5B9A\u3067\uFF1A\n1. '.md' \u307E\u305F\u306F '.mmd' \u3092\u691C\u7D22\n2. \u73FE\u5728\u306E\u65E2\u5B9A\u30A2\u30D7\u30EA\u3092\u30AF\u30EA\u30C3\u30AF\n3. \u4E00\u89A7\u304B\u3089 'Tinta' \u3092\u9078\u629E", L"Tinta\uB97C \uB4F1\uB85D\uD588\uC2B5\uB2C8\uB2E4.\n\n\uC5F4\uB9B0 \uC124\uC815 \uCC3D\uC5D0\uC11C:\n1. '.md' \uB610\uB294 '.mmd' \uAC80\uC0C9\n2. \uD604\uC7AC \uAE30\uBCF8 \uC571 \uD074\uB9AD\n3. \uBAA9\uB85D\uC5D0\uC11C 'Tinta' \uC120\uD0DD" },
    { "fileassoc.register_failed_body", L"Failed to register file association. Try running as administrator.",
                                         L"\u6CE8\u518C\u6587\u4EF6\u5173\u8054\u5931\u8D25\u3002\u8BF7\u5C1D\u8BD5\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u8FD0\u884C\u3002",
                                         L"\u30D5\u30A1\u30A4\u30EB\u95A2\u9023\u4ED8\u3051\u306E\u767B\u9332\u306B\u5931\u6557\u3057\u307E\u3057\u305F\u3002\u7BA1\u7406\u8005\u3068\u3057\u3066\u5B9F\u884C\u3057\u3066\u304F\u3060\u3055\u3044\u3002", L"\uD30C\uC77C \uC5F0\uACB0 \uB4F1\uB85D \uC2E4\uD328. \uAD00\uB9AC\uC790\uB85C \uC2E4\uD589\uD574 \uBCF4\uC138\uC694." },

    // ----- Init-failure dialogs -----
    { "error.title",               L"Error",                                       L"\u9519\u8BEF",                                  L"\u30A8\u30E9\u30FC", L"\uC624\uB958" },
    { "error.d2d_init_failed",     L"Failed to initialize Direct2D",              L"Direct2D \u521D\u59CB\u5316\u5931\u8D25",        L"Direct2D\u306E\u521D\u671F\u5316\u306B\u5931\u6557\u3057\u307E\u3057\u305F", L"Direct2D \uCD08\uAE30\uD654 \uC2E4\uD328" },
    { "error.render_target_failed", L"Failed to create render target",             L"\u521B\u5EFA\u6E32\u67D3\u76EE\u6807\u5931\u8D25", L"\u30EC\u30F3\u30C0\u30FC\u30BF\u30FC\u30B2\u30C3\u30C8\u306E\u4F5C\u6210\u306B\u5931\u6557\u3057\u307E\u3057\u305F", L"\uB80C\uB354\uB9C1 \uD0C0\uAC9F \uC0DD\uC131 \uC2E4\uD328" },

    // ----- Window title -----
    // Format with positional args: %1$s = filename (or full path), %2$s is unused
    // for the non-dirty case. For the dirty case see title.dirty below.
    { "title.plain",   L"Tinta - %1$s",            L"Tinta - %1$s",            L"Tinta - %1$s", L"Tinta - %1$s" },
    { "title.dirty",   L"Tinta - * %1$s",           L"Tinta - * %1$s",          L"Tinta - * %1$s", L"Tinta - * %1$s" },
    { "title.no_file", L"Tinta",                    L"Tinta",                   L"Tinta", L"Tinta" },
    // Quick note (Ctrl+N) before its first save
    { "title.untitled", L"Untitled",                L"\u65E0\u6807\u9898",      L"\u7121\u984C\u540D", L"\uC81C\uBAA9 \uC5C6\uC74C" },

    // ----- GitHub-style alert titles (render.cpp) -----
    // Emoji prefix + variation selector are added by the renderer; we only
    // translate the trailing word. The keys below carry the bare word.
    { "alert.note",      L"Note",      L"\u6CE8\u91CA",       L"\u6CE8\u8A18", L"\uCC38\uACE0" },
    { "alert.tip",       L"Tip",       L"\u63D0\u793A",       L"\u30D2\u30F3\u30C8", L"\uD301" },
    { "alert.important", L"Important", L"\u91CD\u8981",       L"\u91CD\u8981", L"\uC911\uC694" },
    { "alert.warning",   L"Warning",   L"\u8B66\u544A",       L"\u8B66\u544A", L"\uACBD\uACE0" },
    { "alert.caution",   L"Caution",   L"\u6CE8\u610F",       L"\u6CE8\u610F", L"\uC8FC\uC758" },

    // ----- Image alt-text placeholder (render.cpp) -----
    // Renders as "[image: <alt>]". The brackets and separator are added by
    // the renderer; only the word "image" is translated here.
    { "image.placeholder", L"image", L"\u56FE\u7247", L"\u753B\u50CF", L"\uC774\uBBF8\uC9C0" },

    // ----- Context menu (post-#84 surface) -----
    { "ctx.copy", L"Copy", L"\u590D\u5236", L"\u30B3\u30D4\u30FC", L"\uBCF5\uC0AC" },
    { "ctx.select_all", L"Select All", L"\u5168\u9009", L"\u3059\u3079\u3066\u9078\u629E", L"\uBAA8\uB450 \uC120\uD0DD" },
    { "ctx.annotate", L"Annotate", L"\u6DFB\u52A0\u6279\u6CE8", L"\u6CE8\u91C8\u3092\u8FFD\u52A0", L"\uC8FC\uC11D \uCD94\uAC00" },
    { "annot.note_hint", L"Note (markdown)", L"\u6279\u6CE8\uFF08Markdown\uFF09", L"\u30E1\u30E2\uFF08Markdown\uFF09", L"\uBA54\uBAA8 (Markdown)" },
    { "annot.nolocate", L"Couldn't locate selection", L"\u65E0\u6CD5\u5B9A\u4F4D\u6240\u9009\u5185\u5BB9", L"\u9078\u629E\u7BC4\u56F2\u3092\u7279\u5B9A\u3067\u304D\u307E\u305B\u3093", L"\uC120\uD0DD \uC601\uC5ED\uC744 \uCC3E\uC744 \uC218 \uC5C6\uC74C" },
    { "toast.agent_copied", L"Copied for agent", L"\u5DF2\u590D\u5236\u7ED9\u667A\u80FD\u4F53", L"\u30A8\u30FC\u30B8\u30A7\u30F3\u30C8\u7528\u306B\u30B3\u30D4\u30FC", L"\uC5D0\uC774\uC804\uD2B8\uC6A9\uC73C\uB85C \uBCF5\uC0AC\uB428" },
    { "table.copy", L"Copy TSV", L"\u590D\u5236TSV", L"TSV\u3092\u30B3\u30D4\u30FC", L"TSV \uBCF5\uC0AC" },
    { "table.copied", L"Table copied", L"\u5DF2\u590D\u5236\u8868\u683C", L"\u8868\u3092\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F", L"\uD45C \uBCF5\uC0AC\uB428" },
    { "diagram.png", L"Image", L"\u56FE\u7247", L"\u753B\u50CF", L"\uC774\uBBF8\uC9C0" },
    { "diagram.copied", L"Image copied", L"\u5DF2\u590D\u5236\u56FE\u7247", L"\u753B\u50CF\u3092\u30B3\u30D4\u30FC\u3057\u307E\u3057\u305F", L"\uC774\uBBF8\uC9C0 \uBCF5\uC0AC\uB428" },
    { "update.available", L"Tinta %ls is available", L"Tinta %ls \u5DF2\u53D1\u5E03", L"Tinta %ls \u304C\u5229\u7528\u53EF\u80FD\u3067\u3059", L"Tinta %ls \uC0AC\uC6A9 \uAC00\uB2A5" },
    { "ctx.new", L"New File", L"\u65B0\u5EFA\u6587\u4EF6", L"\u65B0\u898F\u30D5\u30A1\u30A4\u30EB", L"\uC0C8 \uD30C\uC77C" },
    { "ctx.print", L"Print / PDF", L"\u6253\u5370 / PDF", L"\u5370\u5237 / PDF", L"\uCD9C\uB825 / PDF" },
    { "ctx.edit", L"Edit", L"\u7F16\u8F91", L"\u7DE8\u96C6", L"\uD3B8\uC9D1" },
    { "ctx.search", L"Search", L"\u641C\u7D22", L"\u691C\u7D22", L"\uAC80\uC0C9" },
    { "ctx.toc", L"Table of Contents", L"\u76EE\u5F55", L"\u76EE\u6B21", L"\uBAA9\uCC28" },
    { "ctx.browse", L"Browse Files", L"\u6D4F\u89C8\u6587\u4EF6", L"\u30D5\u30A1\u30A4\u30EB\u3092\u53C2\u7167", L"\uD30C\uC77C \uCC3E\uAE30" },
    { "ctx.reveal", L"Reveal in Explorer", L"\u5728\u8D44\u6E90\u7BA1\u7406\u5668\u4E2D\u663E\u793A", L"\u30A8\u30AF\u30B9\u30D7\u30ED\u30FC\u30E9\u30FC\u3067\u8868\u793A", L"\uD0D0\uC0C9\uAE30\uC5D0\uC11C \uD45C\uC2DC" },
    { "ctx.export", L"Export as...", L"\u5BFC\u51FA\u4E3A...", L"\u30A8\u30AF\u30B9\u30DD\u30FC\u30C8...", L"\uB0B4\uBCF4\uB0B4\uAE30..." },
    { "toast.exported", L"Exported!", L"\u5DF2\u5BFC\u51FA!", L"\u30A8\u30AF\u30B9\u30DD\u30FC\u30C8\u3057\u307E\u3057\u305F!", L"\uB0B4\uBCF4\uB0C8\uC2B5\uB2C8\uB2E4!" },
    { "tab.menu.close", L"Close", L"\u5173\u95ED", L"\u9589\u3058\u308B", L"\uB2EB\uAE30" },
    { "tab.menu.close_others", L"Close all but this", L"\u5173\u95ED\u5176\u4ED6\u6807\u7B7E\u9875", L"\u4ED6\u306E\u30BF\u30D6\u3092\u9589\u3058\u308B", L"\uB2E4\uB978 \uD0ED \uB2EB\uAE30" },
    { "tab.menu.close_left", L"Close all to the left", L"\u5173\u95ED\u5DE6\u4FA7\u6807\u7B7E\u9875", L"\u5DE6\u5074\u306E\u30BF\u30D6\u3092\u9589\u3058\u308B", L"\uC67C\uCABD \uD0ED \uB2EB\uAE30" },
    { "tab.menu.close_right", L"Close all to the right", L"\u5173\u95ED\u53F3\u4FA7\u6807\u7B7E\u9875", L"\u53F3\u5074\u306E\u30BF\u30D6\u3092\u9589\u3058\u308B", L"\uC624\uB978\uCABD \uD0ED \uB2EB\uAE30" },
    { "tab.menu.copy_path", L"Copy file path", L"\u590D\u5236\u6587\u4EF6\u8DEF\u5F84", L"\u30D5\u30A1\u30A4\u30EB\u30D1\u30B9\u3092\u30B3\u30D4\u30FC", L"\uD30C\uC77C \uACBD\uB85C \uBCF5\uC0AC" },
    { "empty.open_button", L"Open a file", L"\u6253\u5F00\u6587\u4EF6", L"\u30D5\u30A1\u30A4\u30EB\u3092\u958B\u304F", L"\uD30C\uC77C \uC5F4\uAE30" },
    { "empty.hint1", L"Markdown (.md) and Mermaid (.mmd) files \u2014 or", L"Markdown (.md) \u548C Mermaid (.mmd) \u6587\u4EF6\uFF0C\u6216", L"Markdown (.md) \u3068 Mermaid (.mmd) \u30D5\u30A1\u30A4\u30EB \u2014 \u307E\u305F\u306F", L"Markdown (.md) \uBC0F Mermaid (.mmd) \uD30C\uC77C \u2014 \uB610\uB294" },
    { "empty.hint2", L"just start typing on the left", L"\u76F4\u63A5\u5728\u5DE6\u4FA7\u5F00\u59CB\u8F93\u5165", L"\u5DE6\u5074\u3067\u305D\u306E\u307E\u307E\u5165\u529B\u3067\u304D\u307E\u3059", L"\uC67C\uCABD\uC5D0\uC11C \uBC14\uB85C \uC785\uB825\uC744 \uC2DC\uC791\uD558\uC138\uC694" },
    { "search.replace", L"Replace", L"\u66FF\u6362", L"\u7F6E\u63DB", L"\uBC14\uAFB8\uAE30" },
    { "search.replace_all", L"All", L"\u5168\u90E8", L"\u3059\u3079\u3066", L"\uBAA8\uB450" },
    { "search.replace_placeholder", L"Replace with...", L"\u66FF\u6362\u4E3A...", L"\u7F6E\u63DB\u5F8C\u306E\u6587\u5B57\u5217...", L"\uBC14\uAFC0 \uB0B4\uC6A9..." },
    { "settings.config_files", L"Configuration files", L"\u914D\u7F6E\u6587\u4EF6", L"\u8A2D\u5B9A\u30D5\u30A1\u30A4\u30EB", L"\uC124\uC815 \uD30C\uC77C" },
    { "settings.config_files.hint", L"settings.ini, themes.ini, and languages.ini \u2014 plain text, edit by hand", L"\u7EAF\u6587\u672C\u6587\u4EF6\uFF0C\u53EF\u624B\u52A8\u7F16\u8F91", L"\u30C6\u30AD\u30B9\u30C8\u5F62\u5F0F\u3067\u76F4\u63A5\u7DE8\u96C6\u3067\u304D\u307E\u3059", L"\uD14D\uC2A4\uD2B8 \uD30C\uC77C\uB85C \uC9C1\uC811 \uD3B8\uC9D1\uD560 \uC218 \uC788\uC2B5\uB2C8\uB2E4" },
    { "ctx.theme", L"Theme", L"\u4E3B\u9898", L"\u30C6\u30FC\u30DE", L"\uD14C\uB9C8" },
    { "ctx.settings", L"Settings", L"\u8BBE\u7F6E", L"\u8A2D\u5B9A", L"\uC124\uC815" },
    { "ctx.help", L"Help", L"\u5E2E\u52A9", L"\u30D8\u30EB\u30D7", L"\uB3C4\uC6C0\uB9D0" },

    // ----- Settings dialog (post-#84 surface) -----
    { "settings.title", L"Settings", L"\u8BBE\u7F6E", L"\u8A2D\u5B9A", L"\uC124\uC815" },
    { "settings.section.general", L"General", L"\u5E38\u89C4", L"\u4E00\u822C", L"\uC77C\uBC18" },
    { "settings.section.appearance", L"Appearance", L"\u5916\u89C2", L"\u5916\u89B3", L"\uC678\uAD00" },
    { "settings.section.editor", L"Editor", L"\u7F16\u8F91\u5668", L"\u7DE8\u96C6\u30A8\u30C7\u30A3\u30BF\u30FC", L"\uD3B8\uC9D1\uAE30" },
    { "settings.folder_search", L"Folder search results", L"\u6587\u4EF6\u5939\u641C\u7D22\u7ED3\u679C", L"\u30D5\u30A9\u30EB\u30C0\u30FC\u691C\u7D22\u7D50\u679C", L"\uD3F4\uB354 \uAC80\uC0C9 \uACB0\uACFC" },
    { "settings.folder_search.hint", L"Sibling files matched while searching", L"\u641C\u7D22\u65F6\u540C\u65F6\u5339\u914D\u540C\u76EE\u5F55\u7684\u5176\u4ED6\u6587\u4EF6", L"\u691C\u7D22\u4E2D\u306B\u540C\u3058\u30D5\u30A9\u30EB\u30C0\u30FC\u306E\u30D5\u30A1\u30A4\u30EB\u3082\u4E00\u81F4", L"\uAC80\uC0C9 \uC911 \uAC19\uC740 \uD3F4\uB354\uC758 \uD30C\uC77C\uB3C4 \uD45C\uC2DC" },
    // Unsaved-changes exit dialog
    { "confirm.title", L"Unsaved changes", L"\u672A\u4FDD\u5B58\u7684\u66F4\u6539", L"\u672A\u4FDD\u5B58\u306E\u5909\u66F4", L"\uC800\uC7A5\uD558\uC9C0 \uC54A\uC740 \uBCC0\uACBD" },
    { "confirm.body", L"Save your changes before leaving?", L"\u79BB\u5F00\u524D\u4FDD\u5B58\u66F4\u6539\u5417\uFF1F", L"\u7D42\u4E86\u524D\u306B\u5909\u66F4\u3092\u4FDD\u5B58\u3057\u307E\u3059\u304B\uFF1F", L"\uB098\uAC00\uAE30 \uC804\uC5D0 \uBCC0\uACBD \uC0AC\uD56D\uC744 \uC800\uC7A5\uD560\uAE4C\uC694?" },
    { "confirm.save", L"Save and exit", L"\u4FDD\u5B58\u5E76\u9000\u51FA", L"\u4FDD\u5B58\u3057\u3066\u7D42\u4E86", L"\uC800\uC7A5\uD558\uACE0 \uC885\uB8CC" },
    { "confirm.discard", L"Discard changes", L"\u653E\u5F03\u66F4\u6539", L"\u5909\u66F4\u3092\u7834\u68C4", L"\uBCC0\uACBD \uBC84\uB9AC\uAE30" },
    { "confirm.keep", L"Keep editing", L"\u7EE7\u7EED\u7F16\u8F91", L"\u7DE8\u96C6\u3092\u7D9A\u3051\u308B", L"\uACC4\uC18D \uD3B8\uC9D1" },
    { "shortcuts.zen", L"Zen mode", L"\u7985\u6A21\u5F0F", L"\u30BC\u30F3\u30E2\u30FC\u30C9", L"\uC820 \uBAA8\uB4DC" },
    { "shortcuts.editor.title", L"Edit Shortcuts", L"\u7F16\u8F91\u5FEB\u6377\u952E", L"\u30B7\u30E7\u30FC\u30C8\u30AB\u30C3\u30C8\u3092\u7DE8\u96C6", L"\uB2E8\uCD95\uD0A4 \uD3B8\uC9D1" },
    { "shortcuts.editor.hint", L"Click an action, then press its new key", L"\u70B9\u51FB\u64CD\u4F5C\uFF0C\u7136\u540E\u6309\u4E0B\u65B0\u6309\u952E", L"\u64CD\u4F5C\u3092\u30AF\u30EA\u30C3\u30AF\u3057\u3001\u65B0\u3057\u3044\u30AD\u30FC\u3092\u62BC\u3059", L"\uC791\uC5C5\uC744 \uD074\uB9AD\uD55C \uD6C4 \uC0C8 \uD0A4\uB97C \uB204\uB974\uC138\uC694" },
    { "shortcuts.editor.press", L"Press a key\u2026", L"\u8BF7\u6309\u952E\u2026", L"\u30AD\u30FC\u3092\u62BC\u3057\u3066\u304F\u3060\u3055\u3044\u2026", L"\uD0A4\uB97C \uB204\uB974\uC138\uC694\u2026" },
    { "shortcuts.editor.reset", L"Reset all", L"\u5168\u90E8\u91CD\u7F6E", L"\u3059\u3079\u3066\u30EA\u30BB\u30C3\u30C8", L"\uBAA8\uB450 \uCD08\uAE30\uD654" },
    { "shortcuts.editor.footer", L"Press ESC to close", L"\u6309 ESC \u5173\u95ED", L"ESC\u3067\u9589\u3058\u308B", L"ESC\uB97C \uB20C\uB7EC \uB2EB\uAE30" },
    { "settings.shortcuts", L"Keyboard shortcuts", L"\u952E\u76D8\u5FEB\u6377\u952E", L"\u30AD\u30FC\u30DC\u30FC\u30C9\u30B7\u30E7\u30FC\u30C8\u30AB\u30C3\u30C8", L"\uD0A4\uBCF4\uB4DC \uB2E8\uCD95\uD0A4" },
    { "settings.shortcuts.hint", L"Windows-style, vim-style, or your own bindings", L"Windows \u98CE\u683C\u3001vim \u98CE\u683C\u6216\u81EA\u5B9A\u4E49\u6309\u952E", L"Windows \u30B9\u30BF\u30A4\u30EB\u3001Vim \u30B9\u30BF\u30A4\u30EB\u3001\u307E\u305F\u306F\u81EA\u5206\u306E\u30AD\u30FC", L"Windows \uBC29\uC2DD, Vim \uBC29\uC2DD \uB610\uB294 \uC0AC\uC6A9\uC790 \uC124\uC815" },
    { "settings.profile.windows", L"Windows", L"Windows", L"Windows", L"Windows" },
    { "settings.profile.vim", L"Vim", L"Vim", L"Vim", L"Vim" },
    { "settings.profile.custom", L"Custom", L"\u81EA\u5B9A\u4E49", L"\u30AB\u30B9\u30BF\u30E0", L"\uC0AC\uC6A9\uC790 \uC815\uC758" },
    { "settings.browse_focus_path", L"Focus path on open", L"\u6253\u5F00\u65F6\u805A\u7126\u8DEF\u5F84\u6846", L"\u958B\u304F\u3068\u304D\u306B\u30D1\u30B9\u3092\u30D5\u30A9\u30FC\u30AB\u30B9", L"\uC5F4 \uB54C \uACBD\uB85C \uC785\uB825\uC5D0 \uD3EC\uCEE4\uC2A4" },
    { "settings.browse_focus_path.hint", L"B opens the file browser ready to paste a path", L"\u6309 B \u6253\u5F00\u6D4F\u89C8\u5668\u5373\u53EF\u76F4\u63A5\u7C98\u8D34\u8DEF\u5F84", L"B\u3067\u30D5\u30A1\u30A4\u30EB\u30D6\u30E9\u30A6\u30B6\u30FC\u3092\u958B\u304D\u3001\u30D1\u30B9\u3092\u3059\u3050\u8CBC\u308A\u4ED8\u3051", L"B\uB85C \uD30C\uC77C \uBE0C\uB77C\uC6B0\uC800\uB97C \uC5F4\uACE0 \uACBD\uB85C\uB97C \uBC14\uB85C \uBD99\uC5EC\uB123\uC73C\uC138\uC694" },
    { "help.view.new_tab", L"Open file in new tab", L"\u5728\u65B0\u6807\u7B7E\u9875\u4E2D\u6253\u5F00\u6587\u4EF6", L"\u65B0\u3057\u3044\u30BF\u30D6\u3067\u30D5\u30A1\u30A4\u30EB\u3092\u958B\u304F", L"\uC0C8 \uD0ED\uC5D0\uC11C \uD30C\uC77C \uC5F4\uAE30" },
    { "help.view.next_tab", L"Next tab", L"\u4E0B\u4E00\u4E2A\u6807\u7B7E\u9875", L"\u6B21\u306E\u30BF\u30D6", L"\uB2E4\uC74C \uD0ED" },
    { "help.view.close_tab", L"Close tab", L"\u5173\u95ED\u6807\u7B7E\u9875", L"\u30BF\u30D6\u3092\u9589\u3058\u308B", L"\uD0ED \uB2EB\uAE30" },
    { "settings.open_in_tabs", L"Open files in tabs", L"\u5728\u6807\u7B7E\u9875\u4E2D\u6253\u5F00\u6587\u4EF6", L"\u30BF\u30D6\u3067\u30D5\u30A1\u30A4\u30EB\u3092\u958B\u304F", L"\uD0ED\uC5D0\uC11C \uD30C\uC77C \uC5F4\uAE30" },
    { "settings.open_in_tabs.hint", L"Files opened from Explorer join this window as new tabs", L"\u4ECE\u8D44\u6E90\u7BA1\u7406\u5668\u6253\u5F00\u7684\u6587\u4EF6\u5C06\u4F5C\u4E3A\u65B0\u6807\u7B7E\u9875\u52A0\u5165\u6B64\u7A97\u53E3", L"\u30A8\u30AF\u30B9\u30D7\u30ED\u30FC\u30E9\u30FC\u304B\u3089\u958B\u3044\u305F\u30D5\u30A1\u30A4\u30EB\u306F\u65B0\u3057\u3044\u30BF\u30D6\u3068\u3057\u3066\u3053\u306E\u30A6\u30A3\u30F3\u30C9\u30A6\u306B\u8FFD\u52A0\u3055\u308C\u307E\u3059", L"\uD0D0\uC0C9\uAE30\uC5D0\uC11C \uC5F0 \uD30C\uC77C\uC740 \uC0C8 \uD0ED\uC73C\uB85C \uC774 \uCC3D\uC5D0 \uCD94\uAC00\uB429\uB2C8\uB2E4" },
    { "tabs.switcher.header", L"OPEN FILES", L"\u6253\u5F00\u7684\u6587\u4EF6", L"\u958B\u3044\u3066\u3044\u308B\u30D5\u30A1\u30A4\u30EB", L"\uC5F4\uB9B0 \uD30C\uC77C" },
    { "settings.open_ini", L"Edit settings.ini", L"\u7F16\u8F91 settings.ini", L"settings.ini\u3092\u7DE8\u96C6", L"settings.ini \uD3B8\uC9D1" },
    { "settings.open_ini.hint", L"Keys, positions, and everything else", L"\u5FEB\u6377\u952E\u3001\u9605\u8BFB\u4F4D\u7F6E\u7B49\u5168\u90E8\u914D\u7F6E", L"\u30AD\u30FC\u3001\u4F4D\u7F6E\u3001\u305D\u306E\u4ED6\u3059\u3079\u3066", L"\uB2E8\uCD95\uD0A4, \uC704\uCE58 \uB4F1 \uBAA8\uB4E0 \uC124\uC815" },
    { "settings.open_themes_ini", L"Edit themes.ini", L"\u7F16\u8F91 themes.ini", L"themes.ini\u3092\u7DE8\u96C6", L"themes.ini \uD3B8\uC9D1" },
    { "settings.open_themes_ini.hint", L"Hand-tune custom themes, syntax colors included", L"\u624B\u52A8\u8C03\u6574\u81EA\u5B9A\u4E49\u4E3B\u9898\uFF08\u542B\u8BED\u6CD5\u914D\u8272\uFF09", L"\u30B7\u30F3\u30BF\u30C3\u30AF\u30B9\u8272\u3092\u542B\u3080\u30AB\u30B9\u30BF\u30E0\u30C6\u30FC\u30DE\u3092\u8ABF\u6574", L"\uC0AC\uC6A9\uC790 \uC815\uC758 \uD14C\uB9C8\uC640 \uBB38\uBC95 \uC0C9\uC0C1\uC744 \uC9C1\uC811 \uC870\uC815" },
    { "settings.open", L"Open", L"\u6253\u5F00", L"\u958B\u304F", L"\uC5F4\uAE30" },
    { "settings.reading_width_window", L"Reading width \u2014 window", L"\u9605\u8BFB\u5BBD\u5EA6 \u2014 \u7A97\u53E3", L"\u8AAD\u66F8\u5E45 \u2014 \u30A6\u30A3\u30F3\u30C9\u30A6", L"\uC77D\uAE30 \uB108\uBE44 \u2014 \uCC3D" },
    { "settings.reading_width_window.hint", L"Center the document in a column on wide screens", L"\u5BBD\u5C4F\u4E0B\u5C06\u6587\u6863\u5C45\u4E2D\u4E3A\u7A84\u680F", L"\u5E83\u3044\u753B\u9762\u3067\u6587\u66F8\u3092\u5217\u306E\u4E2D\u592E\u306B\u914D\u7F6E", L"\uB113\uC740 \uD654\uBA74\uC5D0\uC11C \uBB38\uC11C\uB97C \uC5F4\uC758 \uC911\uC559\uC5D0 \uBC30\uCE58" },
    { "settings.reading_width_full", L"Reading width \u2014 fullscreen", L"\u9605\u8BFB\u5BBD\u5EA6 \u2014 \u5168\u5C4F", L"\u8AAD\u66F8\u5E45 \u2014 \u30D5\u30EB\u30B9\u30AF\u30EA\u30FC\u30F3", L"\uC77D\uAE30 \uB108\uBE44 \u2014 \uC804\uCCB4 \uD654\uBA74" },
    { "settings.reading_width_full.hint", L"Column used in zen mode (F11)", L"\u7985\u6A21\u5F0F\uFF08F11\uFF09\u4F7F\u7528\u7684\u680F\u5BBD", L"\u30BC\u30F3\u30E2\u30FC\u30C9\uFF08F11\uFF09\u3067\u4F7F\u7528\u3059\u308B\u5217\u5E45", L"\uC820 \uBAA8\uB4DC(F11)\uC5D0\uC11C \uC0AC\uC6A9\uD558\uB294 \uC5F4 \uB108\uBE44" },
    { "settings.follow_windows", L"Follow Windows theme", L"\u8DDF\u968F Windows \u4E3B\u9898", L"Windows \u306E\u30C6\u30FC\u30DE\u306B\u5F93\u3046", L"Windows \uD14C\uB9C8 \uB530\uB974\uAE30" },
    { "settings.follow_windows.hint", L"Switch light and dark with the system", L"\u968F\u7CFB\u7EDF\u5207\u6362\u6D45\u8272\u4E0E\u6DF1\u8272", L"\u30B7\u30B9\u30C6\u30E0\u306B\u5408\u308F\u305B\u3066\u30E9\u30A4\u30C8 / \u30C0\u30FC\u30AF\u3092\u5207\u66FF", L"\uC2DC\uC2A4\uD15C\uC5D0 \uB530\uB77C \uBC1D\uC740 / \uC5B4\uB450\uC6B4 \uD14C\uB9C8 \uC804\uD658" },
    { "settings.toc_side", L"Table of contents", L"\u76EE\u5F55\u4F4D\u7F6E", L"\u76EE\u6B21\u306E\u4F4D\u7F6E", L"\uBAA9\uCC28 \uC704\uCE58" },
    { "settings.toc_side.hint", L"Which side the Tab panel slides from", L"Tab \u76EE\u5F55\u9762\u677F\u4ECE\u54EA\u4E00\u4FA7\u6ED1\u51FA", L"Tab \u30D1\u30CD\u30EB\u304C\u3069\u3061\u3089\u304B\u3089\u30B9\u30E9\u30A4\u30C9\u3059\u308B\u304B", L"Tab \uD328\uB110\uC774 \uC5B4\uB290 \uCABD\uC5D0\uC11C \uC2AC\uB77C\uC774\uB4DC\uB418\uB294\uC9C0" },
    { "settings.toc.left", L"Left", L"\u5DE6\u4FA7", L"\u5DE6", L"\uC67C\uCABD" },
    { "settings.toc.right", L"Right", L"\u53F3\u4FA7", L"\u53F3", L"\uC624\uB978\uCABD" },
    { "settings.themes", L"Themes", L"\u4E3B\u9898", L"\u30C6\u30FC\u30DE", L"\uD14C\uB9C8" },
    { "settings.themes.hint", L"Browse, edit the current one, or start fresh", L"\u6D4F\u89C8\u3001\u7F16\u8F91\u5F53\u524D\u4E3B\u9898\u6216\u65B0\u5EFA", L"\u95B2\u89A7\u3001\u73FE\u5728\u306E\u30C6\u30FC\u30DE\u3092\u7DE8\u96C6\u3001\u307E\u305F\u306F\u65B0\u898F\u4F5C\u6210", L"\uD0D0\uC0C9, \uD604\uC7AC \uD14C\uB9C8 \uD3B8\uC9D1 \uB610\uB294 \uC0C8\uB85C \uC2DC\uC791" },
    { "settings.browse", L"Browse", L"\u6D4F\u89C8", L"\u53C2\u7167", L"\uCC3E\uAE30" },
    { "settings.edit", L"Edit", L"\u7F16\u8F91", L"\u7DE8\u96C6", L"\uD3B8\uC9D1" },
    { "settings.new", L"+ New", L"+ \u65B0\u5EFA", L"+ \u65B0\u898F", L"+ \uC0C8\uB85C \uB9CC\uB4E4\uAE30" },
    { "settings.word_wrap", L"Word wrap", L"\u81EA\u52A8\u6362\u884C", L"\u81EA\u52D5\u6539\u884C", L"\uC790\uB3D9 \uC904\uBC14\uAFC0" },
    { "settings.word_wrap.hint", L"Wrap long lines in the editor pane", L"\u7F16\u8F91\u5668\u5185\u957F\u884C\u81EA\u52A8\u6362\u884C", L"\u7DE8\u96C6\u30DA\u30A4\u30F3\u3067\u9577\u3044\u884C\u3092\u6298\u308A\u8FD4\u3059", L"\uD3B8\uC9D1\uAE30\uC5D0\uC11C \uAE34 \uC904\uC744 \uC790\uB3D9\uC73C\uB85C \uBC14\uAFC0" },
    { "settings.preview_pane", L"Preview pane", L"\u9884\u89C8\u7A97\u683C", L"\u30D7\u30EC\u30D3\u30E5\u30FC\u30DA\u30A4\u30F3", L"\uBBF8\uB9AC\uBCF4\uAE30 \uD328\uB110" },
    { "settings.preview_pane.hint", L"Show the rendered preview beside the editor", L"\u5728\u7F16\u8F91\u5668\u65C1\u663E\u793A\u6E32\u67D3\u9884\u89C8", L"\u7DE8\u96C6\u30DA\u30A4\u30F3\u306E\u6A2A\u306B\u30EC\u30F3\u30C0\u30EA\u30F3\u30B0\u30D7\u30EC\u30D3\u30E5\u30FC\u3092\u8868\u793A", L"\uD3B8\uC9D1\uAE30 \uC606\uC5D0 \uB80C\uB354\uB9C1 \uBBF8\uB9AC\uBCF4\uAE30 \uD45C\uC2DC" },
    { "settings.language", L"Language", L"\u754C\u9762\u8BED\u8A00", L"\u8A00\u8A9E", L"\uC5B8\uC5B4" },
    { "settings.language.hint", L"Auto follows the Windows display language", L"\u81EA\u52A8 = \u8DDF\u968F Windows \u663E\u793A\u8BED\u8A00", L"\u81EA\u52D5 = Windows \u306E\u8868\u793A\u8A00\u8A9E\u306B\u5F93\u3046", L"\uC790\uB3D9 = Windows \uD45C\uC2DC \uC5B8\uC5B4 \uB530\uB984" },
    { "settings.lang.auto", L"Auto", L"\u81EA\u52A8", L"\u81EA\u52D5", L"\uC790\uB3D9" },
    { "settings.footer", L"Esc to close \u2022 changes apply immediately", L"\u6309 Esc \u5173\u95ED \u2022 \u66F4\u6539\u7ACB\u5373\u751F\u6548", L"ESC\u3067\u9589\u3058\u308B \u2022 \u5909\u66F4\u306F\u3050\u3059\u306B\u53CD\u6620", L"ESC\uB85C \uB2EB\uAE30 \u2022 \uBCC0\uACBD \uC989\uC2DC \uC801\uC6A9" },
    { "settings.open_langs", L"Edit languages.ini", L"\u7F16\u8F91 languages.ini", L"languages.ini\u3092\u7DE8\u96C6", L"languages.ini \uD3B8\uC9D1" },
    { "settings.open_langs.hint", L"Add or fix translations \u2014 more languages welcome", L"\u8865\u5145\u6216\u4FEE\u6B63\u7FFB\u8BD1\uFF08\u6B22\u8FCE\u66F4\u591A\u8BED\u8A00\uFF09", L"\u7FFB\u8A33\u3092\u8FFD\u52A0\u307E\u305F\u306F\u4FEE\u6B63 \u2014 \u65B0\u3057\u3044\u8A00\u8A9E\u3082\u6B53\u8FCE", L"\uBC88\uC5ED \uCD94\uAC00 \uB610\uB294 \uC218\uC815 \u2014 \uB354 \uB9CE\uC740 \uC5B8\uC5B4\uB97C \uD658\uC601\uD569\uB2C8\uB2E4" },
    { "settings.full", L"Full", L"\u5168\u5BBD", L"\u5168\u5E45", L"\uC804\uCCB4" },

    // ----- Theme editor -----
    { "theme.editor.title", L"New theme", L"\u65B0\u5EFA\u4E3B\u9898", L"\u65B0\u3057\u3044\u30C6\u30FC\u30DE", L"\uC0C8 \uD14C\uB9C8" },
    { "theme.editor.name", L"Name", L"\u540D\u79F0", L"\u540D\u524D", L"\uC774\uB984" },
    { "theme.editor.based_on", L"Based on", L"\u57FA\u4E8E", L"\u30D9\u30FC\u30B9", L"\uAE30\uBC18" },
    { "theme.editor.dark", L"Dark theme", L"\u6DF1\u8272\u4E3B\u9898", L"\u30C0\u30FC\u30AF\u30C6\u30FC\u30DE", L"\uC5B4\uB450\uC6B4 \uD14C\uB9C8" },
    { "theme.editor.color.background", L"Background", L"\u80CC\u666F", L"\u80CC\u666F", L"\uBC30\uACBD" },
    { "theme.editor.color.text", L"Text", L"\u6587\u672C", L"\u30C6\u30AD\u30B9\u30C8", L"\uD14D\uC2A4\uD2B8" },
    { "theme.editor.color.heading", L"Heading", L"\u6807\u9898", L"\u898B\u51FA\u3057", L"\uD5E4\uB529" },
    { "theme.editor.color.link", L"Link", L"\u94FE\u63A5", L"\u30EA\u30F3\u30AF", L"\uB9C1\uD06C" },
    { "theme.editor.color.accent", L"Accent", L"\u5F3A\u8C03\u8272", L"\u30A2\u30AF\u30BB\u30F3\u30C8", L"\uAC15\uC870\uC0C9" },
    { "theme.editor.color.code_background", L"Code background", L"\u4EE3\u7801\u80CC\u666F", L"\u30B3\u30FC\u30C9\u80CC\u666F", L"\uCF54\uB4DC \uBC30\uACBD" },
    { "theme.editor.save", L"Save theme", L"\u4FDD\u5B58\u4E3B\u9898", L"\u30C6\u30FC\u30DE\u3092\u4FDD\u5B58", L"\uD14C\uB9C8 \uC800\uC7A5" },
    { "theme.editor.cancel", L"Cancel", L"\u53D6\u6D88", L"\u30AD\u30E3\u30F3\u30BB\u30EB", L"\uCDE8\uC18C" },
    { "theme.editor.open_ini", L"Open themes.ini for full control", L"\u6253\u5F00 themes.ini \u8FDB\u884C\u5B8C\u6574\u914D\u7F6E", L"themes.ini\u3092\u958B\u3044\u3066\u3059\u3079\u3066\u3092\u8ABF\u6574", L"themes.ini\uC5D0\uC11C \uC804\uCCB4 \uC124\uC815 \uD3B8\uC9D1" },

    // ----- Print preview -----
    { "print.title", L"Print preview - page %d of %d", L"\u6253\u5370\u9884\u89C8 - \u7B2C %d / %d \u9875", L"\u5370\u5237\u30D7\u30EC\u30D3\u30E5\u30FC - %d / %d \u30DA\u30FC\u30B8", L"\uC778\uC1C4 \uBBF8\uB9AC\uBCF4\uAE30 - %d / %d\uD398\uC774\uC9C0" },
    { "print.portrait", L"Portrait", L"\u7AD6\u5411", L"\u7E26", L"\uC138\uB85C" },
    { "print.landscape", L"Landscape", L"\u6A2A\u5411", L"\u6A2A", L"\uAC00\uB85C" },
    { "print.print", L"Print...", L"\u6253\u5370...", L"\u5370\u5237...", L"\uC778\uC1C4..." },
    { "print.cancel", L"Cancel", L"\u53D6\u6D88", L"\u30AD\u30E3\u30F3\u30BB\u30EB", L"\uCDE8\uC18C" },
    { "print.hint", L"Scroll or PgUp/PgDn to flip pages - Enter to print - Esc to close", L"\u6EDA\u52A8\u6216 PgUp/PgDn \u7FFB\u9875 - Enter \u6253\u5370 - Esc \u5173\u95ED", L"\u30B9\u30AF\u30ED\u30FC\u30EB\u307E\u305F\u306F PgUp/PgDn \u3067\u30DA\u30FC\u30B8\u79FB\u52D5 - Enter \u3067\u5370\u5237 - Esc \u3067\u9589\u3058\u308B", L"\uC2A4\uD06C\uB864 \uB610\uB294 PgUp/PgDn\uC73C\uB85C \uD398\uC774\uC9C0 \uC804\uD658 - Enter\uB85C \uC778\uC1C4 - Esc\uB85C \uB2EB\uAE30" },

};

constexpr size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

bool compiledColumnHasTranslations(int column) {
    if (column == 0) return true;
    for (size_t i = 0; i < kEntryCount; i++) {
        const Entry& entry = kEntries[i];
        if ((column == 1 && entry.zh) ||
            (column == 2 && entry.ja) ||
            (column == 3 && entry.ko)) {
            return true;
        }
    }
    return false;
}

const Entry* findEntry(const char* key) {
    // Linear scan — ~80 entries, called a few hundred times per frame at most.
    for (size_t i = 0; i < kEntryCount; i++) {
        if (std::strcmp(kEntries[i].key, key) == 0) {
            return &kEntries[i];
        }
    }
    return nullptr;
}

const wchar_t* pick(const Entry* e, int langIndex) {
    if (!e) return nullptr;
    switch (langIndex) {
        case 1: if (e->zh) return e->zh; break;
        case 2: if (e->ja) return e->ja; break;
        case 3: if (e->ko) return e->ko; break;
        default: break;
    }
    return e->en;  // English is the fallback (also handles langIndex == 0)
}

} // namespace

namespace {
struct RuntimeLanguage;
const wchar_t* overrideFor(const std::string& id, const char* key);
}

const wchar_t* tr(int langIndex, const char* key) {
    if (!key) return L"";
    langIndex = clampLanguageIndex(langIndex);
    if (const wchar_t* o = overrideFor(languageIdAt(langIndex), key)) {
        return o;  // languages.ini overrides the compiled table
    }
    const Entry* e = findEntry(key);
    // ini-only languages have no compiled column: fall through to English
    int column = languageCompiledColumn(langIndex);
    const wchar_t* v = pick(e, column >= 0 ? column : 0);
    if (v) return v;
    // Unknown key: return the key itself so breakage is visible in dev
    // without crashing. Convert char* to a stable wide buffer.
    static thread_local std::wstring fallback;
    fallback.clear();
    for (const char* p = key; *p; ++p) fallback.push_back((wchar_t)(unsigned char)*p);
    return fallback.c_str();
}

const wchar_t* tr(const App& app, const char* key) {
    return tr(app.currentLanguageIndex, key);
}

int clampLanguageIndex(int index) {
    return (index >= 0 && index < languageCount()) ? index : LANG_INDEX_EN;
}

int detectSystemLanguage() {
    // GetUserPreferredUILanguages returns a double-NUL-terminated list of
    // BCP 47 tags ("en-US\0zh-Hans-CN\0\0"). We pick the first one we recognize.
    ULONG numLanguages = 0;
    ULONG bufferSize = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLanguages, nullptr, &bufferSize)) {
        std::wstring buffer(bufferSize, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &numLanguages, buffer.data(), &bufferSize) && numLanguages > 0) {
            // Walk each NUL-terminated tag in the buffer.
            const wchar_t* p = buffer.c_str();
            for (ULONG i = 0; i < numLanguages; ++i) {
                std::wstring tag(p);
                // ci-compare prefix
                auto startsWith = [&](const wchar_t* prefix) {
                    size_t n = 0;
                    while (prefix[n]) ++n;
                    if (tag.size() < n) return false;
                    for (size_t k = 0; k < n; ++k) {
                        wchar_t a = tag[k], b = prefix[k];
                        if (a >= L'A' && a <= L'Z') a += (L'a' - L'A');
                        if (b >= L'A' && b <= L'Z') b += (L'a' - L'A');
                        if (a != b) return false;
                    }
                    return true;
                };
                // zh-Hans* (Simplified) -> Simplified Chinese.
                // zh-Hant* (Traditional) is not yet translated -> falls through to English.
                if (startsWith(L"zh-hans") || startsWith(L"zh-cn") || startsWith(L"zh-sg")) {
                    return 1;
                }
                if (startsWith(L"zh-hant") || startsWith(L"zh-tw") || startsWith(L"zh-hk")) {
                    int idx = languageIndexById("zh-tw");
                    if (idx >= 0) return idx;  // translated via languages.ini
                }
                if (startsWith(L"ja")) return 2;
                if (startsWith(L"ko")) return 3;
                // Languages registered from languages.ini match by prefix
                for (int li = LANG_COUNT; li < languageCount(); li++) {
                    std::string id = languageIdAt(li);
                    std::wstring wid(id.begin(), id.end());
                    if (startsWith(wid.c_str())) return li;
                }
                p += tag.size() + 1;  // skip past the NUL
            }
        }
    }
    return LANG_INDEX_EN;
}

// ---- Dynamic language registry + languages.ini ----
//
// The compiled table ships en/zh complete with ja/ko columns; the registry
// grows at runtime from %APPDATA%\Tinta\languages.ini, where any [section]
// becomes a selectable language (name= sets its dropdown label) and
// key=value lines override or fill strings. Lookup order per language:
// override -> compiled column (if any) -> compiled English -> the key.

#include <shlobj.h>
#include <shellapi.h>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace {

struct RuntimeLanguage {
    std::string id;           // section name, lowercase ("en", "de", "zh-tw")
    std::wstring nativeName;  // dropdown label
    int compiledColumn;       // 0..3 into Entry, or -1 for ini-only languages
};

std::vector<RuntimeLanguage> g_langs;
std::unordered_map<std::string,
                   std::unordered_map<std::string, std::wstring>> g_overrides;

// Common languages pre-registered for system-language detection and the
// languages.ini translator template. Only languages that actually carry
// translations appear in the Settings dropdown. Native names are UTF-8.
struct SeedLanguage { const char* id; const char* nativeUtf8; };
const SeedLanguage kSeedLanguages[] = {
    {"de", "Deutsch"},
    {"es", "Espa\xc3\xb1ol"},
    {"fr", "Fran\xc3\xa7" "ais"},
    {"it", "Italiano"},
    {"nl", "Nederlands"},
    {"pl", "Polski"},
    {"pt", "Portugu\xc3\xaas"},
    {"ru", "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"},
    {"tr", "T\xc3\xbcrk\xc3\xa7" "e"},
    {"zh-tw", "\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87"},
};

// Built-in translations for languages beyond the compiled 4-column table.
// Seeded into g_overrides through the same mechanism languages.ini uses, so
// any single string can be amended in the ini without a rebuild (ini wins).
// A nullptr column falls back to English; keys absent here (title.*) are
// identical across these locales.
struct BuiltinTranslation {
    const char* key;
    const wchar_t* de;
    const wchar_t* fr;
    const wchar_t* it;
};
const BuiltinTranslation kBuiltinTranslations[] = {
    { "help.title",
      L"Tastaturk\u00FCrzel",
      L"Raccourcis clavier",
      L"Scorciatoie da tastiera" },
    { "help.section.navigation",
      L"NAVIGATION",
      L"NAVIGATION",
      L"NAVIGAZIONE" },
    { "help.section.view",
      L"ANSICHT",
      L"AFFICHAGE",
      L"VISTA" },
    { "help.section.editing",
      L"BEARBEITUNG",
      L"\u00C9DITION",
      L"MODIFICA" },
    { "help.section.general",
      L"ALLGEMEIN",
      L"G\u00C9N\u00C9RAL",
      L"GENERALE" },
    { "help.footer",
      L"ESC oder ? zum Schlie\u00DFen",
      L"ESC ou ? pour fermer",
      L"ESC o ? per chiudere" },
    { "help.nav.scroll_down",
      L"Nach unten scrollen",
      L"D\u00E9filer vers le bas",
      L"Scorri verso il basso" },
    { "help.nav.scroll_up",
      L"Nach oben scrollen",
      L"D\u00E9filer vers le haut",
      L"Scorri verso l'alto" },
    { "help.nav.page_down",
      L"Seite nach unten",
      L"Page suivante",
      L"Pagina gi\u00F9" },
    { "help.nav.page_up",
      L"Seite nach oben",
      L"Page pr\u00E9c\u00E9dente",
      L"Pagina su" },
    { "help.nav.jump_start_end",
      L"Zum Anfang / Ende springen",
      L"Aller au d\u00E9but / \u00E0 la fin",
      L"Vai all'inizio / alla fine" },
    { "help.nav.zoom",
      L"Vergr\u00F6\u00DFern / Verkleinern",
      L"Zoom avant / arri\u00E8re",
      L"Ingrandisci / Riduci" },
    { "help.view.search",
      L"Suchen",
      L"Rechercher",
      L"Cerca" },
    { "help.view.next_match",
      L"N\u00E4chster Treffer",
      L"R\u00E9sultat suivant",
      L"Risultato successivo" },
    { "help.view.folder_browser",
      L"Dateibrowser ein-/ausblenden",
      L"Afficher/masquer l'explorateur de dossier",
      L"Mostra/nascondi esplora cartella" },
    { "help.view.toc",
      L"Inhaltsverzeichnis ein-/ausblenden",
      L"Afficher/masquer le sommaire",
      L"Mostra/nascondi indice" },
    { "help.view.theme",
      L"Designauswahl",
      L"Choix du th\u00E8me",
      L"Scelta del tema" },
    { "help.view.stats",
      L"Statistik ein-/ausblenden",
      L"Afficher/masquer les statistiques",
      L"Mostra/nascondi statistiche" },
    { "help.view.help",
      L"Diese Hilfe",
      L"Cette aide",
      L"Questa guida" },
    { "help.edit.enter_edit",
      L"Bearbeitungsmodus starten",
      L"Passer en mode \u00E9dition",
      L"Entra in modalit\u00E0 modifica" },
    { "help.edit.save",
      L"Speichern (im Bearbeitungsmodus)",
      L"Enregistrer (en mode \u00E9dition)",
      L"Salva (in modalit\u00E0 modifica)" },
    { "help.edit.preview",
      L"Vorschau ein-/ausblenden",
      L"Afficher / masquer l'aper\u00E7u",
      L"Mostra / nascondi anteprima" },
    { "help.edit.word_wrap",
      L"Zeilenumbruch umschalten",
      L"Activer/d\u00E9sactiver le retour \u00E0 la ligne",
      L"Attiva/disattiva a capo automatico" },
    { "help.edit.exit_edit",
      L"Bearbeitungsmodus verlassen",
      L"Quitter le mode \u00E9dition",
      L"Esci dalla modalit\u00E0 modifica" },
    { "help.general.select_all",
      L"Gesamten Text ausw\u00E4hlen",
      L"Tout s\u00E9lectionner",
      L"Seleziona tutto" },
    { "help.general.copy",
      L"Auswahl kopieren",
      L"Copier la s\u00E9lection",
      L"Copia la selezione" },
    { "help.general.close",
      L"Overlay schlie\u00DFen / Beenden",
      L"Fermer le panneau / Quitter",
      L"Chiudi il pannello / Esci" },
    { "help.general.quit",
      L"Beenden",
      L"Quitter",
      L"Esci" },
    { "search.placeholder",
      L"Suchen...",
      L"Rechercher...",
      L"Cerca..." },
    { "search.no_matches",
      L"Keine Treffer",
      L"Aucun r\u00E9sultat",
      L"Nessun risultato" },
    { "search.match_count",
      L"%d von %zu",
      L"%d sur %zu",
      L"%d di %zu" },
    { "toc.title",
      L"Inhalt",
      L"Sommaire",
      L"Indice" },
    { "toc.empty",
      L"Keine \u00DCberschriften",
      L"Aucun titre",
      L"Nessuna intestazione" },
    { "theme.chooser.title",
      L"Design w\u00E4hlen",
      L"Choisir un th\u00E8me",
      L"Scegli un tema" },
    { "theme.chooser.light",
      L"HELLE DESIGNS",
      L"TH\u00C8MES CLAIRS",
      L"TEMI CHIARI" },
    { "theme.chooser.dark",
      L"DUNKLE DESIGNS",
      L"TH\u00C8MES SOMBRES",
      L"TEMI SCURI" },
    { "lang.chooser.title",
      L"Sprache w\u00E4hlen",
      L"Choisir la langue",
      L"Scegli la lingua" },
    { "lang.chooser.footer",
      L"ESC zum Schlie\u00DFen",
      L"ESC pour fermer",
      L"ESC per chiudere" },
    { "toast.no_file",
      L"Keine Datei geladen",
      L"Aucun fichier charg\u00E9",
      L"Nessun file caricato" },
    { "toast.exit_edit_hint",
      L"ESC dr\u00FCcken, um den Bearbeitungsmodus zu verlassen",
      L"Appuyez sur ESC pour quitter le mode \u00E9dition",
      L"Premi ESC per uscire dalla modalit\u00E0 modifica" },
    { "toast.unsaved_exit",
      L"Ungespeicherte \u00C4nderungen! Y = speichern & beenden, N = verwerfen, ESC = abbrechen",
      L"Modifications non enregistr\u00E9es ! Y = enregistrer et quitter, N = abandonner, ESC = annuler",
      L"Modifiche non salvate! Y = salva ed esci, N = scarta, ESC = annulla" },
    { "toast.saved",
      L"Gespeichert!",
      L"Enregistr\u00E9 !",
      L"Salvato!" },
    { "toast.copied",
      L"Kopiert!",
      L"Copi\u00E9 !",
      L"Copiato!" },
    { "toast.save_failed",
      L"Speichern fehlgeschlagen \u2014 Datei ist evtl. gesperrt oder schreibgesch\u00FCtzt",
      L"\u00C9chec de l'enregistrement \u2014 le fichier est peut-\u00EAtre verrouill\u00E9 ou en lecture seule",
      L"Salvataggio non riuscito \u2014 il file potrebbe essere bloccato o di sola lettura" },
    { "toast.exit_cancelled",
      L"Beenden abgebrochen",
      L"Sortie annul\u00E9e",
      L"Uscita annullata" },
    { "toast.exit_confirm",
      L"Erneut ESC dr\u00FCcken, um den Bearbeitungsmodus zu verlassen",
      L"Appuyez de nouveau sur ESC pour quitter le mode \u00E9dition",
      L"Premi di nuovo ESC per uscire dalla modalit\u00E0 modifica" },
    { "toast.preview_shown",
      L"Vorschau eingeblendet (Ctrl+E zum Ausblenden)",
      L"Aper\u00E7u affich\u00E9 (Ctrl+E pour masquer)",
      L"Anteprima visibile (Ctrl+E per nascondere)" },
    { "toast.preview_hidden",
      L"Vorschau ausgeblendet (Ctrl+E zum Einblenden)",
      L"Aper\u00E7u masqu\u00E9 (Ctrl+E pour afficher)",
      L"Anteprima nascosta (Ctrl+E per mostrare)" },
    { "toast.wrap_on",
      L"Zeilenumbruch an (Ctrl+W zum Ausschalten)",
      L"Retour \u00E0 la ligne activ\u00E9 (Ctrl+W pour d\u00E9sactiver)",
      L"A capo automatico attivo (Ctrl+W per disattivare)" },
    { "toast.wrap_off",
      L"Zeilenumbruch aus (Ctrl+W zum Einschalten)",
      L"Retour \u00E0 la ligne d\u00E9sactiv\u00E9 (Ctrl+W pour activer)",
      L"A capo automatico disattivato (Ctrl+W per attivare)" },
    { "codeblock.copy",
      L"Kopieren",
      L"Copier",
      L"Copia" },
    { "codeblock.copied",
      L"Kopiert!",
      L"Copi\u00E9 !",
      L"Copiato!" },
    { "stats.line1",
      L"Parsen: %zu us | Layout: %zu us | Zeichenaufrufe: %zu",
      L"Analyse: %zu us | Mise en page: %zu us | Appels de dessin: %zu",
      L"Parsing: %zu us | Layout: %zu us | Chiamate di disegno: %zu" },
    { "stats.line2",
      L"Start: %.1fms (Fenster: %.1f | D2D: %.1f | DWrite: %.1f | Datei: %.1f)",
      L"D\u00E9marrage: %.1fms (Fen\u00EAtre: %.1f | D2D: %.1f | DWrite: %.1f | Fichier: %.1f)",
      L"Avvio: %.1fms (Finestra: %.1f | D2D: %.1f | DWrite: %.1f | File: %.1f)" },
    { "fileassoc.title",
      L"Tinta - Dateizuordnung",
      L"Tinta - Association de fichiers",
      L"Tinta - Associazione file" },
    { "fileassoc.packaged.title",
      L"Tinta - Standard-Apps",
      L"Tinta - Applications par d\u00E9faut",
      L"Tinta - App predefinite" },
    { "fileassoc.packaged.ask_body",
      L"M\u00F6chten Sie Tinta als Standardanzeige f\u00FCr Markdown- und Mermaid-Dateien festlegen?\n\nWindows \u00F6ffnet die Tinta-Seite in den Einstellungen. Setzen Sie dort .md, .markdown und .mmd auf Tinta Markdown Viewer.",
      L"Voulez-vous d\u00E9finir Tinta comme visionneuse par d\u00E9faut des fichiers Markdown et Mermaid ?\n\nWindows ouvrira la page de Tinta dans les Param\u00E8tres. D\u00E9finissez .md, .markdown et .mmd sur Tinta Markdown Viewer.",
      L"Vuoi impostare Tinta come visualizzatore predefinito per i file Markdown e Mermaid?\n\nWindows aprir\u00E0 la pagina di Tinta nelle Impostazioni. Imposta .md, .markdown e .mmd su Tinta Markdown Viewer." },
    { "fileassoc.add_mmd_failed_body",
      L"Die .mmd-Dateizuordnung konnte nicht hinzugef\u00FCgt werden. F\u00FChren Sie tinta.exe /register aus, um es erneut zu versuchen.",
      L"Impossible d'ajouter l'association du fichier .mmd. Ex\u00E9cutez tinta.exe /register pour r\u00E9essayer.",
      L"Impossibile aggiungere l'associazione dei file .mmd. Esegui tinta.exe /register per riprovare." },
    { "fileassoc.ask_body",
      L"M\u00F6chten Sie Tinta als Standardanzeige f\u00FCr Markdown- und Mermaid-Dateien festlegen?\n\nWindows \u00F6ffnet die Einstellungen, dort k\u00F6nnen Sie Tinta ausw\u00E4hlen.",
      L"Voulez-vous d\u00E9finir Tinta comme visionneuse par d\u00E9faut des fichiers Markdown et Mermaid ?\n\nWindows ouvrira les Param\u00E8tres, o\u00F9 vous pourrez s\u00E9lectionner Tinta.",
      L"Vuoi impostare Tinta come visualizzatore predefinito per i file Markdown e Mermaid?\n\nWindows aprir\u00E0 le Impostazioni, dove potrai selezionare Tinta." },
    { "fileassoc.done_title",
      L"Fast geschafft!",
      L"Presque termin\u00E9 !",
      L"Quasi fatto!" },
    { "fileassoc.done_body",
      L"Tinta wurde registriert.\n\nIm sich \u00F6ffnenden Einstellungsfenster:\n1. Nach '.md' oder '.mmd' suchen\n2. Auf die aktuelle Standard-App klicken\n3. 'Tinta' aus der Liste ausw\u00E4hlen",
      L"Tinta a \u00E9t\u00E9 enregistr\u00E9.\n\nDans la fen\u00EAtre Param\u00E8tres qui s'ouvre :\n1. Recherchez '.md' ou '.mmd'\n2. Cliquez sur l'application par d\u00E9faut actuelle\n3. S\u00E9lectionnez 'Tinta' dans la liste",
      L"Tinta \u00E8 stato registrato.\n\nNella finestra Impostazioni che si apre:\n1. Cerca '.md' o '.mmd'\n2. Fai clic sull'app predefinita corrente\n3. Seleziona 'Tinta' dall'elenco" },
    { "fileassoc.register_failed_body",
      L"Dateizuordnung konnte nicht registriert werden. Versuchen Sie es als Administrator.",
      L"Impossible d'enregistrer l'association de fichiers. Essayez en tant qu'administrateur.",
      L"Impossibile registrare l'associazione file. Prova a eseguire come amministratore." },
    { "error.title",
      L"Fehler",
      L"Erreur",
      L"Errore" },
    { "error.d2d_init_failed",
      L"Direct2D konnte nicht initialisiert werden",
      L"\u00C9chec de l'initialisation de Direct2D",
      L"Inizializzazione di Direct2D non riuscita" },
    { "error.render_target_failed",
      L"Renderziel konnte nicht erstellt werden",
      L"\u00C9chec de la cr\u00E9ation de la cible de rendu",
      L"Creazione della destinazione di rendering non riuscita" },
    { "alert.note",
      L"Hinweis",
      L"Note",
      L"Nota" },
    { "alert.tip",
      L"Tipp",
      L"Astuce",
      L"Suggerimento" },
    { "alert.important",
      L"Wichtig",
      L"Important",
      L"Importante" },
    { "alert.warning",
      L"Warnung",
      L"Avertissement",
      L"Avviso" },
    { "alert.caution",
      L"Achtung",
      L"Attention",
      L"Attenzione" },
    { "image.placeholder",
      L"Bild",
      L"image",
      L"immagine" },
    { "ctx.copy",
      L"Kopieren",
      L"Copier",
      L"Copia" },
    { "ctx.select_all",
      L"Alles ausw\u00E4hlen",
      L"Tout s\u00E9lectionner",
      L"Seleziona tutto" },
    { "ctx.new",
      L"Neue Datei",
      L"Nouveau fichier",
      L"Nuovo file" },
    { "ctx.print",
      L"Drucken / PDF",
      L"Imprimer / PDF",
      L"Stampa / PDF" },
    { "ctx.edit",
      L"Bearbeiten",
      L"Modifier",
      L"Modifica" },
    { "ctx.search",
      L"Suchen",
      L"Rechercher",
      L"Cerca" },
    { "ctx.toc",
      L"Inhaltsverzeichnis",
      L"Sommaire",
      L"Indice" },
    { "ctx.browse",
      L"Dateien durchsuchen",
      L"Parcourir les fichiers",
      L"Sfoglia file" },
    { "ctx.reveal",
      L"Im Explorer anzeigen",
      L"Afficher dans l'Explorateur",
      L"Mostra in Esplora file" },
    { "ctx.export",
      L"Exportieren als...",
      L"Exporter sous...",
      L"Esporta come..." },
    { "toast.exported",
      L"Exportiert!",
      L"Export\u00E9 !",
      L"Esportato!" },
    { "tab.menu.close",
      L"Schlie\u00DFen",
      L"Fermer",
      L"Chiudi" },
    { "tab.menu.close_others",
      L"Alle anderen schlie\u00DFen",
      L"Fermer les autres",
      L"Chiudi gli altri" },
    { "tab.menu.close_left",
      L"Alle links schlie\u00DFen",
      L"Fermer tout \u00E0 gauche",
      L"Chiudi tutte a sinistra" },
    { "tab.menu.close_right",
      L"Alle rechts schlie\u00DFen",
      L"Fermer tout \u00E0 droite",
      L"Chiudi tutte a destra" },
    { "tab.menu.copy_path",
      L"Dateipfad kopieren",
      L"Copier le chemin du fichier",
      L"Copia percorso file" },
    { "empty.open_button",
      L"Datei \u00F6ffnen",
      L"Ouvrir un fichier",
      L"Apri un file" },
    { "empty.hint1",
      L"Markdown- (.md) und Mermaid-Dateien (.mmd) \u2014 oder",
      L"Fichiers Markdown (.md) et Mermaid (.mmd) \u2014 ou",
      L"File Markdown (.md) e Mermaid (.mmd) \u2014 oppure" },
    { "empty.hint2",
      L"einfach links lostippen",
      L"commencez simplement \u00E0 taper \u00E0 gauche",
      L"inizia a digitare a sinistra" },
    { "search.replace",
      L"Ersetzen",
      L"Remplacer",
      L"Sostituisci" },
    { "search.replace_all",
      L"Alle",
      L"Tout",
      L"Tutte" },
    { "search.replace_placeholder",
      L"Ersetzen durch...",
      L"Remplacer par...",
      L"Sostituisci con..." },
    { "settings.config_files",
      L"Konfigurationsdateien",
      L"Fichiers de configuration",
      L"File di configurazione" },
    { "settings.config_files.hint",
      L"Reine Textdateien \u2014 von Hand editierbar",
      L"Fichiers texte \u2014 modifiables \u00E0 la main",
      L"File di testo \u2014 modificabili a mano" },
    { "ctx.theme",
      L"Design",
      L"Th\u00E8me",
      L"Tema" },
    { "ctx.settings",
      L"Einstellungen",
      L"Param\u00E8tres",
      L"Impostazioni" },
    { "ctx.help",
      L"Hilfe",
      L"Aide",
      L"Aiuto" },
    { "settings.title",
      L"Einstellungen",
      L"Param\u00E8tres",
      L"Impostazioni" },
    { "settings.section.general",
      L"Allgemein",
      L"G\u00E9n\u00E9ral",
      L"Generale" },
    { "settings.section.appearance",
      L"Darstellung",
      L"Apparence",
      L"Aspetto" },
    { "settings.section.editor",
      L"Editor",
      L"\u00C9diteur",
      L"Editor" },
    { "settings.folder_search",
      L"Ordner-Suchergebnisse",
      L"R\u00E9sultats de recherche du dossier",
      L"Risultati della ricerca nella cartella" },
    { "settings.folder_search.hint",
      L"Passende Nachbardateien bei der Suche anzeigen",
      L"Afficher les fichiers voisins correspondants pendant la recherche",
      L"Mostra i file adiacenti corrispondenti durante la ricerca" },
    { "settings.open_ini",
      L"settings.ini bearbeiten",
      L"Modifier settings.ini",
      L"Modifica settings.ini" },
    { "settings.open_ini.hint",
      L"Tastenbelegung, Lesepositionen und alles Weitere",
      L"Raccourcis, positions de lecture et tout le reste",
      L"Tasti, posizioni di lettura e tutto il resto" },
    { "settings.open_themes_ini",
      L"themes.ini bearbeiten",
      L"Modifier themes.ini",
      L"Modifica themes.ini" },
    { "settings.open_themes_ini.hint",
      L"Eigene Designs von Hand anpassen, inkl. Syntaxfarben",
      L"Ajuster les th\u00E8mes personnalis\u00E9s \u00E0 la main, couleurs de syntaxe comprises",
      L"Regola a mano i temi personalizzati, colori di sintassi inclusi" },
    { "settings.open",
      L"\u00D6ffnen",
      L"Ouvrir",
      L"Apri" },
    { "settings.reading_width_window",
      L"Lesebreite \u2014 Fenster",
      L"Largeur de lecture \u2014 fen\u00EAtre",
      L"Larghezza di lettura \u2014 finestra" },
    { "settings.reading_width_window.hint",
      L"Dokument auf breiten Bildschirmen als Spalte zentrieren",
      L"Centrer le document en colonne sur les \u00E9crans larges",
      L"Centra il documento in una colonna sugli schermi larghi" },
    { "settings.reading_width_full",
      L"Lesebreite \u2014 Vollbild",
      L"Largeur de lecture \u2014 plein \u00E9cran",
      L"Larghezza di lettura \u2014 schermo intero" },
    { "settings.reading_width_full.hint",
      L"Spaltenbreite im Zen-Modus (F11)",
      L"Colonne utilis\u00E9e en mode zen (F11)",
      L"Colonna usata in modalit\u00E0 zen (F11)" },
    { "settings.follow_windows",
      L"Windows-Design folgen",
      L"Suivre le th\u00E8me Windows",
      L"Segui il tema di Windows" },
    { "settings.follow_windows.hint",
      L"Hell und Dunkel mit dem System wechseln",
      L"Basculer entre clair et sombre avec le syst\u00E8me",
      L"Passa da chiaro a scuro con il sistema" },
    { "settings.toc_side",
      L"Inhaltsverzeichnis",
      L"Sommaire",
      L"Indice" },
    { "settings.toc_side.hint",
      L"Von welcher Seite das Tab-Panel hereinf\u00E4hrt",
      L"C\u00F4t\u00E9 d'o\u00F9 glisse le panneau (touche Tab)",
      L"Lato da cui scorre il pannello (tasto Tab)" },
    { "settings.toc.left",
      L"Links",
      L"Gauche",
      L"Sinistra" },
    { "settings.toc.right",
      L"Rechts",
      L"Droite",
      L"Destra" },
    { "settings.themes",
      L"Designs",
      L"Th\u00E8mes",
      L"Temi" },
    { "settings.themes.hint",
      L"Durchsuchen, das aktuelle bearbeiten oder neu beginnen",
      L"Parcourir, modifier le th\u00E8me actuel ou repartir de z\u00E9ro",
      L"Sfoglia, modifica quello attuale o parti da zero" },
    { "settings.browse",
      L"Durchsuchen",
      L"Parcourir",
      L"Sfoglia" },
    { "settings.edit",
      L"Bearbeiten",
      L"Modifier",
      L"Modifica" },
    { "settings.new",
      L"+ Neu",
      L"+ Nouveau",
      L"+ Nuovo" },
    { "settings.word_wrap",
      L"Zeilenumbruch",
      L"Retour \u00E0 la ligne",
      L"A capo automatico" },
    { "settings.word_wrap.hint",
      L"Lange Zeilen im Editor umbrechen",
      L"Couper les lignes longues dans l'\u00E9diteur",
      L"Manda a capo le righe lunghe nell'editor" },
    { "settings.preview_pane",
      L"Vorschaubereich",
      L"Volet d'aper\u00E7u",
      L"Riquadro anteprima" },
    { "settings.preview_pane.hint",
      L"Gerenderte Vorschau neben dem Editor anzeigen",
      L"Afficher l'aper\u00E7u rendu \u00E0 c\u00F4t\u00E9 de l'\u00E9diteur",
      L"Mostra l'anteprima renderizzata accanto all'editor" },
    { "settings.language",
      L"Sprache",
      L"Langue",
      L"Lingua" },
    { "settings.language.hint",
      L"Auto folgt der Windows-Anzeigesprache",
      L"Auto suit la langue d'affichage de Windows",
      L"Auto segue la lingua di visualizzazione di Windows" },
    { "settings.lang.auto",
      L"Auto",
      L"Auto",
      L"Auto" },
    { "settings.footer",
      L"Esc zum Schlie\u00DFen \u2022 \u00C4nderungen gelten sofort",
      L"Esc pour fermer \u2022 les changements s'appliquent imm\u00E9diatement",
      L"Esc per chiudere \u2022 le modifiche si applicano subito" },
    { "settings.open_langs",
      L"languages.ini bearbeiten",
      L"Modifier languages.ini",
      L"Modifica languages.ini" },
    { "settings.open_langs.hint",
      L"\u00DCbersetzungen erg\u00E4nzen oder korrigieren \u2014 weitere Sprachen willkommen",
      L"Ajouter ou corriger des traductions \u2014 d'autres langues bienvenues",
      L"Aggiungi o correggi traduzioni \u2014 altre lingue benvenute" },
    { "settings.full",
      L"Voll",
      L"Pleine",
      L"Piena" },
    { "theme.editor.title",
      L"Neues Design",
       L"Nouveau th\u00E8me",
      L"Nuovo tema" },
    { "theme.editor.name",
      L"Name",
      L"Nom",
      L"Nome" },
    { "theme.editor.based_on",
      L"Basierend auf",
       L"Bas\u00E9 sur",
      L"Basato su" },
    { "theme.editor.dark",
      L"Dunkles Design",
       L"Th\u00E8me sombre",
      L"Tema scuro" },
    { "theme.editor.color.background",
      L"Hintergrund",
       L"Arri\u00E8re-plan",
      L"Sfondo" },
    { "theme.editor.color.text",
      L"Text",
      L"Texte",
      L"Testo" },
    { "theme.editor.color.heading",
      L"\u00DCberschrift",
      L"Titre",
      L"Intestazione" },
    { "theme.editor.color.link",
      L"Link",
      L"Lien",
      L"Link" },
    { "theme.editor.color.accent",
      L"Akzent",
      L"Accent",
      L"Accento" },
    { "theme.editor.color.code_background",
      L"Code-Hintergrund",
       L"Arri\u00E8re-plan du code",
      L"Sfondo del codice" },
    { "theme.editor.save",
      L"Design speichern",
       L"Enregistrer le th\u00E8me",
      L"Salva tema" },
    { "theme.editor.cancel",
      L"Abbrechen",
      L"Annuler",
      L"Annulla" },
    { "theme.editor.open_ini",
       L"themes.ini f\u00FCr vollst\u00E4ndige Kontrolle \u00F6ffnen",
       L"Ouvrir themes.ini pour tout contr\u00F4ler",
      L"Apri themes.ini per il controllo completo" },
    { "print.title",
      L"Druckvorschau - Seite %d von %d",
      L"Aper\u00E7u avant impression - page %d sur %d",
      L"Anteprima di stampa - pagina %d di %d" },
    { "print.portrait", L"Hochformat", L"Portrait", L"Verticale" },
    { "print.landscape", L"Querformat", L"Paysage", L"Orizzontale" },
    { "print.print", L"Drucken...", L"Imprimer...", L"Stampa..." },
    { "print.cancel", L"Abbrechen", L"Annuler", L"Annulla" },
    { "print.hint", L"Scrollen oder PgUp/PgDn zum Bl\u00E4ttern - Enter zum Drucken - Esc zum Schlie\u00DFen", L"Faites d\u00E9filer ou utilisez PgUp/PgDn pour tourner les pages - Entr\u00E9e pour imprimer - Esc pour fermer", L"Scorri o usa PgUp/PgDn per cambiare pagina - Invio per stampare - Esc per chiudere" },
    { "title.untitled",
      L"Unbenannt",
      L"Sans titre",
      L"Senza titolo" },
    { "help.general.new_note",
      L"Neue Schnellnotiz",
      L"Nouvelle note rapide",
      L"Nuova nota rapida" },
    { "settings.browse_focus_path",
      L"Pfadfeld beim \u00D6ffnen fokussieren",
      L"Focus sur le chemin \u00E0 l'ouverture",
      L"Campo percorso attivo all'apertura" },
    { "settings.browse_focus_path.hint",
      L"B \u00F6ffnet den Dateibrowser bereit zum Einf\u00FCgen eines Pfads",
      L"B ouvre l'explorateur pr\u00EAt \u00E0 coller un chemin",
      L"B apre l'esplora file pronto per incollare un percorso" },
    { "help.view.new_tab",
      L"Datei in neuem Tab \u00F6ffnen",
      L"Ouvrir le fichier dans un nouvel onglet",
      L"Apri file in una nuova scheda" },
    { "help.view.next_tab",
      L"N\u00E4chster Tab",
      L"Onglet suivant",
      L"Scheda successiva" },
    { "help.view.close_tab",
      L"Tab schlie\u00DFen",
      L"Fermer l'onglet",
      L"Chiudi scheda" },
    { "settings.open_in_tabs",
      L"Dateien in Tabs \u00F6ffnen",
      L"Ouvrir les fichiers dans des onglets",
      L"Apri i file in schede" },
    { "settings.open_in_tabs.hint",
      L"Aus dem Explorer ge\u00F6ffnete Dateien werden diesem Fenster als neue Tabs hinzugef\u00FCgt",
      L"Les fichiers ouverts depuis l'Explorateur rejoignent cette fen\u00EAtre comme nouveaux onglets",
      L"I file aperti da Esplora risorse si aggiungono a questa finestra come nuove schede" },
    { "tabs.switcher.header",
      L"GE\u00D6FFNETE DATEIEN",
      L"FICHIERS OUVERTS",
      L"FILE APERTI" },
    { "settings.shortcuts",
      L"Tastaturprofil",
      L"Raccourcis clavier",
      L"Scorciatoie da tastiera" },
    { "settings.shortcuts.hint",
      L"Windows-Stil, Vim-Stil oder eigene Belegung",
      L"Style Windows, style vim ou raccourcis personnalis\u00E9s",
      L"Stile Windows, stile vim o tasti personalizzati" },
    { "settings.profile.custom",
      L"Benutzerdefiniert",
      L"Personnalis\u00E9",
      L"Personalizzato" },
    { "shortcuts.zen",
      L"Zen-Modus",
      L"Mode zen",
      L"Modalit\u00E0 zen" },
    { "shortcuts.editor.title",
      L"Tastenk\u00FCrzel bearbeiten",
      L"Modifier les raccourcis",
      L"Modifica scorciatoie" },
    { "shortcuts.editor.hint",
      L"Aktion anklicken, dann die neue Taste dr\u00FCcken",
      L"Cliquez sur une action, puis appuyez sur la nouvelle touche",
      L"Fai clic su un'azione, poi premi il nuovo tasto" },
    { "shortcuts.editor.press",
      L"Taste dr\u00FCcken\u2026",
      L"Appuyez sur une touche\u2026",
      L"Premi un tasto\u2026" },
    { "shortcuts.editor.reset",
      L"Alle zur\u00FCcksetzen",
      L"Tout r\u00E9initialiser",
      L"Reimposta tutto" },
    { "shortcuts.editor.footer",
      L"ESC zum Schlie\u00DFen",
      L"ESC pour fermer",
      L"ESC per chiudere" },
    { "confirm.title",
      L"Ungespeicherte \u00C4nderungen",
      L"Modifications non enregistr\u00E9es",
      L"Modifiche non salvate" },
    { "confirm.body",
      L"\u00C4nderungen vor dem Verlassen speichern?",
      L"Enregistrer les modifications avant de quitter ?",
      L"Salvare le modifiche prima di uscire?" },
    { "confirm.save",
      L"Speichern und beenden",
      L"Enregistrer et quitter",
      L"Salva ed esci" },
    { "confirm.discard",
      L"\u00C4nderungen verwerfen",
      L"Abandonner les modifications",
      L"Scarta le modifiche" },
    { "confirm.keep",
      L"Weiter bearbeiten",
      L"Continuer l'\u00E9dition",
      L"Continua a modificare" },
};

void seedBuiltinOverrides() {
    for (const BuiltinTranslation& t : kBuiltinTranslations) {
        if (t.de) g_overrides["de"][t.key] = t.de;
        if (t.fr) g_overrides["fr"][t.key] = t.fr;
        if (t.it) g_overrides["it"][t.key] = t.it;
    }
}

std::wstring langUtf8ToWide(const std::string& s);

void ensureBaseLanguages() {
    if (!g_langs.empty()) return;
    for (int i = 0; i < LANG_COUNT; i++) {
        const char* ids[] = {"en", "zh", "ja", "ko"};
        g_langs.push_back({ids[i], LANGUAGES[i].nativeName, i});
    }
    for (const SeedLanguage& seed : kSeedLanguages) {
        g_langs.push_back({seed.id, langUtf8ToWide(seed.nativeUtf8), -1});
    }
}

std::wstring langUtf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len > 0 ? len - 1 : 0, L'\0');
    if (len > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    return w;
}

std::wstring languagesIniPath() {
    wchar_t appData[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        return L"";
    }
    std::wstring path = appData;
    path += L"\\Tinta";
    CreateDirectoryW(path.c_str(), nullptr);
    return path + L"\\languages.ini";
}

// Section-name aliases onto the compiled languages
std::string normalizeLanguageId(std::string id) {
    for (char& c : id) c = (char)tolower((unsigned char)c);
    if (id == "zh-cn" || id == "zh-hans" || id.rfind("en-", 0) == 0) {
        return id[0] == 'e' ? "en" : "zh";
    }
    if (id == "ja-jp") return "ja";
    if (id == "ko-kr") return "ko";
    return id;
}

const wchar_t* overrideFor(const std::string& id, const char* key) {
    auto oit = g_overrides.find(id);
    if (oit == g_overrides.end()) return nullptr;
    auto kit = oit->second.find(key);
    return kit != oit->second.end() ? kit->second.c_str() : nullptr;
}


} // namespace

int languageCount() {
    ensureBaseLanguages();
    return (int)g_langs.size();
}

const wchar_t* languageNameAt(int index) {
    ensureBaseLanguages();
    if (index < 0 || index >= (int)g_langs.size()) index = 0;
    return g_langs[index].nativeName.c_str();
}

std::string languageIdAt(int index) {
    ensureBaseLanguages();
    if (index < 0 || index >= (int)g_langs.size()) index = 0;
    return g_langs[index].id;
}

bool languageHasTranslations(int index) {
    ensureBaseLanguages();
    if (index < 0 || index >= (int)g_langs.size()) return true;
    const RuntimeLanguage& lang = g_langs[index];
    if (lang.compiledColumn >= 0 &&
        compiledColumnHasTranslations(lang.compiledColumn)) return true;
    auto it = g_overrides.find(lang.id);
    return it != g_overrides.end() && !it->second.empty();
}

int languageCompiledColumn(int index) {
    ensureBaseLanguages();
    if (index < 0 || index >= (int)g_langs.size()) return 0;
    return g_langs[index].compiledColumn;
}

int languageIndexById(const std::string& rawId) {
    ensureBaseLanguages();
    std::string id = normalizeLanguageId(rawId);
    for (size_t i = 0; i < g_langs.size(); i++) {
        if (g_langs[i].id == id) return (int)i;
    }
    return -1;
}

void loadLanguageOverrides() {
    g_langs.clear();   // rebuild: compiled + seeded roster, then ini extras
    g_overrides.clear();
    ensureBaseLanguages();
    seedBuiltinOverrides();  // de/fr/it ship built-in; ini values override

    std::wstring path = languagesIniPath();
    if (path.empty()) return;
    std::ifstream file(path);
    if (!file) return;

    int locale = -1;
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            std::string id = normalizeLanguageId(line.substr(1, line.size() - 2));
            locale = languageIndexById(id);
            if (locale < 0 && !id.empty()) {
                g_langs.push_back({id, langUtf8ToWide(id), -1});
                locale = (int)g_langs.size() - 1;
            }
            continue;
        }
        if (locale < 0) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string key = line.substr(0, eq);
        std::wstring value = langUtf8ToWide(line.substr(eq + 1));
        if (value.empty()) continue;
        if (key == "name") {
            g_langs[locale].nativeName = std::move(value);
        } else {
            g_overrides[g_langs[locale].id][key] = std::move(value);
        }
    }
}

// Seeds a translator-friendly template on first open: a roster of common
// languages, each with its native name and every key commented out with
// the English text as reference. Any section with translations becomes
// selectable in Settings after a restart.
void openLanguagesIniFile() {
    std::wstring path = languagesIniPath();
    if (path.empty()) return;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream f(path, std::ios::binary);
        f << "; Tinta UI translations \xe2\x80\x94 UTF-8, key=value under a language section.\n";
        f << "; Uncomment a line, translate the right-hand side, restart Tinta \xe2\x80\x94\n";
        f << "; the language appears in Settings once it has translations.\n";
        f << "; name= sets the label shown in the language dropdown.\n";
        f << "; Values override the built-in strings \xe2\x80\x94 including the shipped\n";
        f << "; de/fr/it translations, so corrections work per-key; missing keys\n";
        f << "; fall back to English.\n";
        for (const SeedLanguage& lang : kSeedLanguages) {
            f << "\n[" << lang.id << "]\n";
            f << "name=" << lang.nativeUtf8 << "\n";
            for (const Entry& e : kEntries) {
                int len = WideCharToMultiByte(CP_UTF8, 0, e.en, -1, nullptr, 0,
                                              nullptr, nullptr);
                std::string en(len > 0 ? len - 1 : 0, '\0');
                if (len > 1) {
                    WideCharToMultiByte(CP_UTF8, 0, e.en, -1, &en[0], len,
                                        nullptr, nullptr);
                }
                f << ";" << e.key << "=" << en << "\n";
            }
        }
    }
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
