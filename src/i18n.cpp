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
//   - Format strings use positional printf-style placeholders so locales can
//     reorder arguments: "%1$d of %2$zu", not "%d of %zu".
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
    { "help.title",            L"Keyboard Shortcuts",                       L"\u952E\u76D8\u5FEB\u6377\u952E",                       nullptr, nullptr },
    { "help.section.navigation", L"NAVIGATION",                             L"\u5BFC\u822A",                                          nullptr, nullptr },
    { "help.section.view",     L"VIEW",                                     L"\u89C6\u56FE",                                          nullptr, nullptr },
    { "help.section.editing",  L"EDITING",                                  L"\u7F16\u8F91",                                          nullptr, nullptr },
    { "help.section.general",  L"GENERAL",                                  L"\u5E38\u89C4",                                          nullptr, nullptr },
    { "help.footer",           L"Press ESC or ? to close",                  L"\u6309 ESC \u6216 ? \u5173\u95ED",                      nullptr, nullptr },

    // Help entries: only the description column is translated; keys (Ctrl+S etc.) stay verbatim.
    { "help.nav.scroll_down",      L"Scroll down",                  L"\u5411\u4E0B\u6EDA\u52A8",                          nullptr, nullptr },
    { "help.nav.scroll_up",        L"Scroll up",                    L"\u5411\u4E0A\u6EDA\u52A8",                          nullptr, nullptr },
    { "help.nav.page_down",        L"Page down",                    L"\u5411\u4E0B\u7FFB\u9875",                          nullptr, nullptr },
    { "help.nav.page_up",          L"Page up",                      L"\u5411\u4E0A\u7FFB\u9875",                          nullptr, nullptr },
    { "help.nav.jump_start_end",   L"Jump to start / end",          L"\u8DF3\u8F6C\u5230\u5F00\u5934 / \u7ED3\u5C3E",     nullptr, nullptr },
    { "help.nav.zoom",             L"Zoom in / out",                L"\u653E\u5927 / \u7F29\u5C0F",                       nullptr, nullptr },

    { "help.view.search",          L"Search",                       L"\u641C\u7D22",                                      nullptr, nullptr },
    { "help.view.next_match",      L"Next search match",            L"\u4E0B\u4E00\u4E2A\u5339\u914D\u7ED3\u679C",        nullptr, nullptr },
    { "help.view.folder_browser",  L"Toggle folder browser",        L"\u5207\u6362\u6587\u4EF6\u5939\u6D4F\u89C8",        nullptr, nullptr },
    { "help.view.toc",             L"Toggle table of contents",     L"\u5207\u6362\u76EE\u5F55",                          nullptr, nullptr },
    { "help.view.theme",           L"Theme chooser",                L"\u4E3B\u9898\u9009\u62E9\u5668",                    nullptr, nullptr },
    { "help.view.stats",           L"Toggle stats",                 L"\u5207\u6362\u7EDF\u8BA1\u4FE1\u606F",              nullptr, nullptr },
    { "help.view.help",            L"This help",                    L"\u672C\u5E2E\u52A9",                                nullptr, nullptr },

    { "help.edit.enter_edit",      L"Enter edit mode",              L"\u8FDB\u5165\u7F16\u8F91\u6A21\u5F0F",              nullptr, nullptr },
    { "help.edit.save",            L"Save (in edit mode)",          L"\u4FDD\u5B58\uFF08\u7F16\u8F91\u6A21\u5F0F\u4E0B\uFF09", nullptr, nullptr },
    { "help.edit.preview",         L"Show / hide preview pane",     L"\u663E\u793A / \u9690\u85CF\u9884\u89C8\u7A97\u683C", nullptr, nullptr },
    { "help.edit.word_wrap",       L"Toggle word wrap",             L"\u5207\u6362\u81EA\u52A8\u6362\u884C",              nullptr, nullptr },
    { "help.edit.exit_edit",       L"Exit edit mode",               L"\u9000\u51FA\u7F16\u8F91\u6A21\u5F0F",              nullptr, nullptr },

    { "help.general.select_all",   L"Select all text",              L"\u5168\u9009\u6587\u672C",                          nullptr, nullptr },
    { "help.general.copy",         L"Copy selection",               L"\u590D\u5236\u9009\u4E2D\u5185\u5BB9",              nullptr, nullptr },
    { "help.general.close",        L"Close overlay / Quit",         L"\u5173\u95ED\u6D6E\u5C42 / \u9000\u51FA",           nullptr, nullptr },
    { "help.general.quit",         L"Quit",                         L"\u9000\u51FA",                                      nullptr, nullptr },

    // ----- Search overlay -----
    { "search.placeholder",    L"Search...",                                L"\u641C\u7D22...",                                      nullptr, nullptr },
    { "search.no_matches",     L"No matches",                               L"\u65E0\u5339\u914D",                                   nullptr, nullptr },
    // Positional format: %1$d = current (1-based), %2$zu = total. Locales may reorder.
    { "search.match_count",    L"%1$d of %2$zu",                            L"\u7B2C %1$d / %2$zu \u4E2A",                           nullptr, nullptr },

    // ----- Table of contents -----
    { "toc.title",             L"Contents",                                 L"\u76EE\u5F55",                                         nullptr, nullptr },
    { "toc.empty",             L"No headings",                              L"\u65E0\u6807\u9898",                                   nullptr, nullptr },

    // ----- Theme chooser -----
    { "theme.chooser.title",   L"Choose Theme",                             L"\u9009\u62E9\u4E3B\u9898",                             nullptr, nullptr },
    { "theme.chooser.light",   L"LIGHT THEMES",                             L"\u6D45\u8272\u4E3B\u9898",                             nullptr, nullptr },
    { "theme.chooser.dark",    L"DARK THEMES",                              L"\u6DF1\u8272\u4E3B\u9898",                             nullptr, nullptr },

    // ----- Language chooser -----
    { "lang.chooser.title",    L"Choose Language",                          L"\u9009\u62E9\u8BED\u8A00",                             nullptr, nullptr },
    { "lang.chooser.footer",   L"Press ESC to close",                       L"\u6309 ESC \u5173\u95ED",                              nullptr, nullptr },

    // ----- Toast / editor notifications -----
    { "toast.no_file",         L"No file loaded",                           L"\u672A\u52A0\u8F7D\u6587\u4EF6",                       nullptr, nullptr },
    { "toast.exit_edit_hint",  L"Press ESC twice to exit edit mode",        L"\u8FDE\u6309\u4E24\u6B21 ESC \u9000\u51FA\u7F16\u8F91\u6A21\u5F0F", nullptr, nullptr },
    { "toast.unsaved_exit",    L"Unsaved changes! Y = save & exit, N = discard, ESC = cancel",
                               L"\u6709\u672A\u4FDD\u5B58\u7684\u66F4\u6539\uFF01Y = \u4FDD\u5B58\u5E76\u9000\u51FA\uFF0CN = \u653E\u5F03\uFF0CESC = \u53D6\u6D88",
                               nullptr, nullptr },
    { "toast.saved",           L"Saved!",                                   L"\u5DF2\u4FDD\u5B58\uFF01",                             nullptr, nullptr },
    { "toast.save_failed",     L"Save failed \u2014 file may be locked or read-only",
                               L"\u4FDD\u5B58\u5931\u8D25 \u2014 \u6587\u4EF6\u53EF\u80FD\u88AB\u9501\u5B9A\u6216\u4E3A\u53EA\u8BFB",
                               nullptr, nullptr },
    { "toast.exit_cancelled",  L"Exit cancelled",                           L"\u5DF2\u53D6\u6D88\u9000\u51FA",                       nullptr, nullptr },
    { "toast.exit_confirm",    L"Press ESC again to exit edit mode",        L"\u518D\u6309\u4E00\u6B21 ESC \u9000\u51FA\u7F16\u8F91\u6A21\u5F0F", nullptr, nullptr },
    { "toast.preview_shown",   L"Preview shown (Ctrl+P to hide)",           L"\u5DF2\u663E\u793A\u9884\u89C8\uFF08Ctrl+P \u9690\u85CF\uFF09", nullptr, nullptr },
    { "toast.preview_hidden",  L"Preview hidden (Ctrl+P to show)",          L"\u5DF2\u9690\u85CF\u9884\u89C8\uFF08Ctrl+P \u663E\u793A\uFF09", nullptr, nullptr },
    { "toast.wrap_on",         L"Word wrap on (Ctrl+W to turn off)",        L"\u5DF2\u5F00\u542F\u81EA\u52A8\u6362\u884C\uFF08Ctrl+W \u5173\u95ED\uFF09", nullptr, nullptr },
    { "toast.wrap_off",        L"Word wrap off (Ctrl+W to turn on)",        L"\u5DF2\u5173\u95ED\u81EA\u52A8\u6362\u884C\uFF08Ctrl+W \u5F00\u542F\uFF09", nullptr, nullptr },

    // Code block hover button + confirmation pill
    { "codeblock.copy",        L"Copy",                                     L"\u590D\u5236",                                         nullptr, nullptr },
    { "codeblock.copied",      L"Copied!",                                  L"\u5DF2\u590D\u5236\uFF01",                             nullptr, nullptr },

    // ----- Stats overlay -----
    // Two-line format. Labels translatable; D2D/DWrite kept as technical abbreviations.
    // Placeholders: %1$zu parse us, %2$zu layout us, %3$zu draw calls,
    //               %4$.1f startup ms, %5$.1f window ms, %6$.1f d2d ms,
    //               %7$.1f dwrite ms, %8$.1f file ms
    { "stats.line1",           L"Parse: %1$zu us | Layout: %2$zu us | Draw calls: %3$zu",
                               L"\u89E3\u6790: %1$zu us | \u5E03\u5C40: %2$zu us | \u7ED8\u5236\u8C03\u7528: %3$zu",
                               nullptr, nullptr },
    { "stats.line2",           L"Startup: %4$.1fms (Win: %5$.1f | D2D: %6$.1f | DWrite: %7$.1f | File: %8$.1f)",
                               L"\u542F\u52A8: %4$.1fms (\u7A97\u53E3: %5$.1f | D2D: %6$.1f | DWrite: %7$.1f | \u6587\u4EF6: %8$.1f)",
                               nullptr, nullptr },

    // ----- File-association dialogs (shared by settings.cpp and main_d2d.cpp /register) -----
    { "fileassoc.title",                L"Tinta - File Association",                 L"Tinta - \u6587\u4EF6\u5173\u8054",                              nullptr, nullptr },
    { "fileassoc.add_mmd_failed_body",  L"Failed to add the .mmd file association. Run tinta.exe /register to try again.",
                                         L"\u6DFB\u52A0 .mmd \u6587\u4EF6\u5173\u8054\u5931\u8D25\u3002\u8BF7\u8FD0\u884C tinta.exe /register \u91CD\u8BD5\u3002",
                                         nullptr, nullptr },
    { "fileassoc.ask_body",
        L"Would you like to set Tinta as the default viewer for Markdown and Mermaid files?\n\nWindows will open Settings where you can select Tinta.",
        L"\u662F\u5426\u5C06 Tinta \u8BBE\u4E3A Markdown \u548C Mermaid \u6587\u4EF6\u7684\u9ED8\u8BA4\u67E5\u770B\u5668\uFF1F\n\nWindows \u5C06\u6253\u5F00\u8BBE\u7F6E\uFF0C\u60A8\u53EF\u5728\u5176\u4E2D\u9009\u62E9 Tinta\u3002",
        nullptr, nullptr },
    { "fileassoc.done_title",   L"Almost done!",                              L"\u5C31\u5FEB\u5B8C\u6210\u4E86\uFF01",                          nullptr, nullptr },
    { "fileassoc.done_body",
        L"Tinta has been registered.\n\nIn the Settings window that opens:\n1. Search for '.md' or '.mmd'\n2. Click on the current default app\n3. Select 'Tinta' from the list",
        L"Tinta \u5DF2\u6CE8\u518C\u3002\n\n\u5728\u6253\u5F00\u7684\u8BBE\u7F6E\u7A97\u53E3\u4E2D\uFF1A\n1. \u641C\u7D22 \u201C.md\u201D \u6216 \u201C.mmd\u201D\n2. \u70B9\u51FB\u5F53\u524D\u9ED8\u8BA4\u5E94\u7528\n3. \u4ECE\u5217\u8868\u4E2D\u9009\u62E9 \u201CTinta\u201D",
        nullptr, nullptr },
    { "fileassoc.register_failed_body", L"Failed to register file association. Try running as administrator.",
                                         L"\u6CE8\u518C\u6587\u4EF6\u5173\u8054\u5931\u8D25\u3002\u8BF7\u5C1D\u8BD5\u4EE5\u7BA1\u7406\u5458\u8EAB\u4EFD\u8FD0\u884C\u3002",
                                         nullptr, nullptr },

    // ----- Init-failure dialogs -----
    { "error.title",               L"Error",                                       L"\u9519\u8BEF",                                  nullptr, nullptr },
    { "error.d2d_init_failed",     L"Failed to initialize Direct2D",              L"Direct2D \u521D\u59CB\u5316\u5931\u8D25",        nullptr, nullptr },
    { "error.render_target_failed", L"Failed to create render target",             L"\u521B\u5EFA\u6E32\u67D3\u76EE\u6807\u5931\u8D25", nullptr, nullptr },

    // ----- Window title -----
    // Format with positional args: %1$s = filename (or full path), %2$s is unused
    // for the non-dirty case. For the dirty case see title.dirty below.
    { "title.plain",   L"Tinta - %1$s",            L"Tinta - %1$s",            nullptr, nullptr },
    { "title.dirty",   L"Tinta - * %1$s",           L"Tinta - * %1$s",          nullptr, nullptr },
    { "title.no_file", L"Tinta",                    L"Tinta",                   nullptr, nullptr },

    // ----- GitHub-style alert titles (render.cpp) -----
    // Emoji prefix + variation selector are added by the renderer; we only
    // translate the trailing word. The keys below carry the bare word.
    { "alert.note",      L"Note",      L"\u6CE8\u91CA",       nullptr, nullptr },
    { "alert.tip",       L"Tip",       L"\u63D0\u793A",       nullptr, nullptr },
    { "alert.important", L"Important", L"\u91CD\u8981",       nullptr, nullptr },
    { "alert.warning",   L"Warning",   L"\u8B66\u544A",       nullptr, nullptr },
    { "alert.caution",   L"Caution",   L"\u6CE8\u610F",       nullptr, nullptr },

    // ----- Image alt-text placeholder (render.cpp) -----
    // Renders as "[image: <alt>]". The brackets and separator are added by
    // the renderer; only the word "image" is translated here.
    { "image.placeholder", L"image", L"\u56FE\u7247", nullptr, nullptr },
};

constexpr size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

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

const wchar_t* tr(int langIndex, const char* key) {
    if (!key) return L"";
    const Entry* e = findEntry(key);
    const wchar_t* v = pick(e, langIndex);
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
    return (index >= 0 && index < LANG_COUNT) ? index : LANG_INDEX_EN;
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
                if (startsWith(L"ja")) return 2;
                if (startsWith(L"ko")) return 3;
                p += tag.size() + 1;  // skip past the NUL
            }
        }
    }
    return LANG_INDEX_EN;
}
