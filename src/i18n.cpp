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
    { "help.general.new_note",     L"New quick note",               L"\u65B0\u5EFA\u5FEB\u901F\u7B14\u8BB0",              nullptr, nullptr },
    { "help.general.close",        L"Close overlay / Quit",         L"\u5173\u95ED\u6D6E\u5C42 / \u9000\u51FA",           nullptr, nullptr },
    { "help.general.quit",         L"Quit",                         L"\u9000\u51FA",                                      nullptr, nullptr },

    // ----- Search overlay -----
    { "search.placeholder",    L"Search...",                                L"\u641C\u7D22...",                                      nullptr, nullptr },
    { "search.no_matches",     L"No matches",                               L"\u65E0\u5339\u914D",                                   nullptr, nullptr },
    // Fixed argument order (current, total): MSVC's printf family has no
    // positional specifiers, so locales phrase around the same order
    { "search.match_count",    L"%d of %zu",                                L"\u7B2C %d / %zu \u4E2A",                       nullptr, nullptr },

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
    { "toast.exit_edit_hint",  L"Press Esc to exit edit mode",        L"\u6309 ESC \u9000\u51FA\u7F16\u8F91\u6A21\u5F0F", nullptr, nullptr },
    { "toast.unsaved_exit",    L"Unsaved changes! Y = save & exit, N = discard, ESC = cancel",
                               L"\u6709\u672A\u4FDD\u5B58\u7684\u66F4\u6539\uFF01Y = \u4FDD\u5B58\u5E76\u9000\u51FA\uFF0CN = \u653E\u5F03\uFF0CESC = \u53D6\u6D88",
                               nullptr, nullptr },
    { "toast.saved",           L"Saved!",                                   L"\u5DF2\u4FDD\u5B58\uFF01",                             nullptr, nullptr },
    { "toast.save_failed",     L"Save failed \u2014 file may be locked or read-only",
                               L"\u4FDD\u5B58\u5931\u8D25 \u2014 \u6587\u4EF6\u53EF\u80FD\u88AB\u9501\u5B9A\u6216\u4E3A\u53EA\u8BFB",
                               nullptr, nullptr },
    { "toast.exit_cancelled",  L"Exit cancelled",                           L"\u5DF2\u53D6\u6D88\u9000\u51FA",                       nullptr, nullptr },
    { "toast.exit_confirm",    L"Press ESC again to exit edit mode",        L"\u518D\u6309\u4E00\u6B21 ESC \u9000\u51FA\u7F16\u8F91\u6A21\u5F0F", nullptr, nullptr },
    { "toast.preview_shown",   L"Preview shown (Ctrl+E to hide)",           L"\u5DF2\u663E\u793A\u9884\u89C8\uFF08Ctrl+E \u9690\u85CF\uFF09", nullptr, nullptr },
    { "toast.preview_hidden",  L"Preview hidden (Ctrl+E to show)",          L"\u5DF2\u9690\u85CF\u9884\u89C8\uFF08Ctrl+E \u663E\u793A\uFF09", nullptr, nullptr },
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
    // Quick note (Ctrl+N) before its first save
    { "title.untitled", L"Untitled",                L"\u65E0\u6807\u9898",      nullptr, nullptr },

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

    // ----- Context menu (post-#84 surface) -----
    { "ctx.copy", L"Copy", L"\u590D\u5236", nullptr, nullptr },
    { "ctx.select_all", L"Select All", L"\u5168\u9009", nullptr, nullptr },
    { "ctx.new", L"New File", L"\u65B0\u5EFA\u6587\u4EF6", nullptr, nullptr },
    { "ctx.print", L"Print / PDF", L"\u6253\u5370 / PDF", nullptr, nullptr },
    { "ctx.edit", L"Edit", L"\u7F16\u8F91", nullptr, nullptr },
    { "ctx.search", L"Search", L"\u641C\u7D22", nullptr, nullptr },
    { "ctx.toc", L"Table of Contents", L"\u76EE\u5F55", nullptr, nullptr },
    { "ctx.browse", L"Browse Files", L"\u6D4F\u89C8\u6587\u4EF6", nullptr, nullptr },
    { "ctx.reveal", L"Reveal in Explorer", L"\u5728\u8D44\u6E90\u7BA1\u7406\u5668\u4E2D\u663E\u793A", nullptr, nullptr },
    { "ctx.theme", L"Theme", L"\u4E3B\u9898", nullptr, nullptr },
    { "ctx.settings", L"Settings", L"\u8BBE\u7F6E", nullptr, nullptr },
    { "ctx.help", L"Help", L"\u5E2E\u52A9", nullptr, nullptr },

    // ----- Settings dialog (post-#84 surface) -----
    { "settings.title", L"Settings", L"\u8BBE\u7F6E", nullptr, nullptr },
    { "settings.section.general", L"General", L"\u5E38\u89C4", nullptr, nullptr },
    { "settings.section.appearance", L"Appearance", L"\u5916\u89C2", nullptr, nullptr },
    { "settings.section.editor", L"Editor", L"\u7F16\u8F91\u5668", nullptr, nullptr },
    { "settings.folder_search", L"Folder search results", L"\u6587\u4EF6\u5939\u641C\u7D22\u7ED3\u679C", nullptr, nullptr },
    { "settings.folder_search.hint", L"Sibling files matched while searching", L"\u641C\u7D22\u65F6\u540C\u65F6\u5339\u914D\u540C\u76EE\u5F55\u7684\u5176\u4ED6\u6587\u4EF6", nullptr, nullptr },
    // Unsaved-changes exit dialog
    { "confirm.title", L"Unsaved changes", L"\u672A\u4FDD\u5B58\u7684\u66F4\u6539", nullptr, nullptr },
    { "confirm.body", L"Save your changes before leaving?", L"\u79BB\u5F00\u524D\u4FDD\u5B58\u66F4\u6539\u5417\uFF1F", nullptr, nullptr },
    { "confirm.save", L"Save and exit", L"\u4FDD\u5B58\u5E76\u9000\u51FA", nullptr, nullptr },
    { "confirm.discard", L"Discard changes", L"\u653E\u5F03\u66F4\u6539", nullptr, nullptr },
    { "confirm.keep", L"Keep editing", L"\u7EE7\u7EED\u7F16\u8F91", nullptr, nullptr },
    { "shortcuts.zen", L"Zen mode", L"\u7985\u6A21\u5F0F", nullptr, nullptr },
    { "shortcuts.editor.title", L"Edit Shortcuts", L"\u7F16\u8F91\u5FEB\u6377\u952E", nullptr, nullptr },
    { "shortcuts.editor.hint", L"Click an action, then press its new key", L"\u70B9\u51FB\u64CD\u4F5C\uFF0C\u7136\u540E\u6309\u4E0B\u65B0\u6309\u952E", nullptr, nullptr },
    { "shortcuts.editor.press", L"Press a key\u2026", L"\u8BF7\u6309\u952E\u2026", nullptr, nullptr },
    { "shortcuts.editor.reset", L"Reset all", L"\u5168\u90E8\u91CD\u7F6E", nullptr, nullptr },
    { "settings.shortcuts", L"Keyboard shortcuts", L"\u952E\u76D8\u5FEB\u6377\u952E", nullptr, nullptr },
    { "settings.shortcuts.hint", L"Windows-style, vim-style, or your own bindings", L"Windows \u98CE\u683C\u3001vim \u98CE\u683C\u6216\u81EA\u5B9A\u4E49\u6309\u952E", nullptr, nullptr },
    { "settings.profile.windows", L"Windows", L"Windows", nullptr, nullptr },
    { "settings.profile.vim", L"Vim", L"Vim", nullptr, nullptr },
    { "settings.profile.custom", L"Custom", L"\u81EA\u5B9A\u4E49", nullptr, nullptr },
    { "settings.browse_focus_path", L"Focus path on open", L"\u6253\u5F00\u65F6\u805A\u7126\u8DEF\u5F84\u6846", nullptr, nullptr },
    { "settings.browse_focus_path.hint", L"B opens the file browser ready to paste a path", L"\u6309 B \u6253\u5F00\u6D4F\u89C8\u5668\u5373\u53EF\u76F4\u63A5\u7C98\u8D34\u8DEF\u5F84", nullptr, nullptr },
    { "settings.open_ini", L"Edit settings.ini", L"\u7F16\u8F91 settings.ini", nullptr, nullptr },
    { "settings.open_ini.hint", L"Keys, positions, and everything else", L"\u5FEB\u6377\u952E\u3001\u9605\u8BFB\u4F4D\u7F6E\u7B49\u5168\u90E8\u914D\u7F6E", nullptr, nullptr },
    { "settings.open_themes_ini", L"Edit themes.ini", L"\u7F16\u8F91 themes.ini", nullptr, nullptr },
    { "settings.open_themes_ini.hint", L"Hand-tune custom themes, syntax colors included", L"\u624B\u52A8\u8C03\u6574\u81EA\u5B9A\u4E49\u4E3B\u9898\uFF08\u542B\u8BED\u6CD5\u914D\u8272\uFF09", nullptr, nullptr },
    { "settings.open", L"Open", L"\u6253\u5F00", nullptr, nullptr },
    { "settings.reading_width_window", L"Reading width \u2014 window", L"\u9605\u8BFB\u5BBD\u5EA6 \u2014 \u7A97\u53E3", nullptr, nullptr },
    { "settings.reading_width_window.hint", L"Center the document in a column on wide screens", L"\u5BBD\u5C4F\u4E0B\u5C06\u6587\u6863\u5C45\u4E2D\u4E3A\u7A84\u680F", nullptr, nullptr },
    { "settings.reading_width_full", L"Reading width \u2014 fullscreen", L"\u9605\u8BFB\u5BBD\u5EA6 \u2014 \u5168\u5C4F", nullptr, nullptr },
    { "settings.reading_width_full.hint", L"Column used in zen mode (F11)", L"\u7985\u6A21\u5F0F\uFF08F11\uFF09\u4F7F\u7528\u7684\u680F\u5BBD", nullptr, nullptr },
    { "settings.follow_windows", L"Follow Windows theme", L"\u8DDF\u968F Windows \u4E3B\u9898", nullptr, nullptr },
    { "settings.follow_windows.hint", L"Switch light and dark with the system", L"\u968F\u7CFB\u7EDF\u5207\u6362\u6D45\u8272\u4E0E\u6DF1\u8272", nullptr, nullptr },
    { "settings.toc_side", L"Table of contents", L"\u76EE\u5F55\u4F4D\u7F6E", nullptr, nullptr },
    { "settings.toc_side.hint", L"Which side the Tab panel slides from", L"Tab \u76EE\u5F55\u9762\u677F\u4ECE\u54EA\u4E00\u4FA7\u6ED1\u51FA", nullptr, nullptr },
    { "settings.toc.left", L"Left", L"\u5DE6\u4FA7", nullptr, nullptr },
    { "settings.toc.right", L"Right", L"\u53F3\u4FA7", nullptr, nullptr },
    { "settings.themes", L"Themes", L"\u4E3B\u9898", nullptr, nullptr },
    { "settings.themes.hint", L"Browse, edit the current one, or start fresh", L"\u6D4F\u89C8\u3001\u7F16\u8F91\u5F53\u524D\u4E3B\u9898\u6216\u65B0\u5EFA", nullptr, nullptr },
    { "settings.browse", L"Browse", L"\u6D4F\u89C8", nullptr, nullptr },
    { "settings.edit", L"Edit", L"\u7F16\u8F91", nullptr, nullptr },
    { "settings.new", L"+ New", L"+ \u65B0\u5EFA", nullptr, nullptr },
    { "settings.word_wrap", L"Word wrap", L"\u81EA\u52A8\u6362\u884C", nullptr, nullptr },
    { "settings.word_wrap.hint", L"Wrap long lines in the editor pane", L"\u7F16\u8F91\u5668\u5185\u957F\u884C\u81EA\u52A8\u6362\u884C", nullptr, nullptr },
    { "settings.preview_pane", L"Preview pane", L"\u9884\u89C8\u7A97\u683C", nullptr, nullptr },
    { "settings.preview_pane.hint", L"Show the rendered preview beside the editor", L"\u5728\u7F16\u8F91\u5668\u65C1\u663E\u793A\u6E32\u67D3\u9884\u89C8", nullptr, nullptr },
    { "settings.language", L"Language", L"\u754C\u9762\u8BED\u8A00", nullptr, nullptr },
    { "settings.language.hint", L"Auto follows the Windows display language", L"\u81EA\u52A8 = \u8DDF\u968F Windows \u663E\u793A\u8BED\u8A00", nullptr, nullptr },
    { "settings.lang.auto", L"Auto", L"\u81EA\u52A8", nullptr, nullptr },
    { "settings.footer", L"Esc to close \u2022 changes apply immediately", L"\u6309 Esc \u5173\u95ED \u2022 \u66F4\u6539\u7ACB\u5373\u751F\u6548", nullptr, nullptr },
    { "settings.open_langs", L"Edit languages.ini", L"\u7F16\u8F91 languages.ini", nullptr, nullptr },
    { "settings.open_langs.hint", L"Add or fix translations \u2014 more languages welcome", L"\u8865\u5145\u6216\u4FEE\u6B63\u7FFB\u8BD1\uFF08\u6B22\u8FCE\u66F4\u591A\u8BED\u8A00\uFF09", nullptr, nullptr },
    { "settings.full", L"Full", L"\u5168\u5BBD", nullptr, nullptr },

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
      L"Parsen: %1$zu us | Layout: %2$zu us | Zeichenaufrufe: %3$zu",
      L"Analyse: %1$zu us | Mise en page: %2$zu us | Appels de dessin: %3$zu",
      L"Parsing: %1$zu us | Layout: %2$zu us | Chiamate di disegno: %3$zu" },
    { "stats.line2",
      L"Start: %4$.1fms (Fenster: %5$.1f | D2D: %6$.1f | DWrite: %7$.1f | Datei: %8$.1f)",
      L"D\u00E9marrage: %4$.1fms (Fen\u00EAtre: %5$.1f | D2D: %6$.1f | DWrite: %7$.1f | Fichier: %8$.1f)",
      L"Avvio: %4$.1fms (Finestra: %5$.1f | D2D: %6$.1f | DWrite: %7$.1f | File: %8$.1f)" },
    { "fileassoc.title",
      L"Tinta - Dateizuordnung",
      L"Tinta - Association de fichiers",
      L"Tinta - Associazione file" },
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
    // en and zh ship complete; ja/ko columns are empty until translated
    if (lang.compiledColumn == 0 || lang.compiledColumn == 1) return true;
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
