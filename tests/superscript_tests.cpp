#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "render.h"
#include "settings.h"
#include "startpage.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}
bool closeEnough(float a, float b) { return std::abs(a-b) < 0.05f; }
void layout(App& app, const std::string& source) {
    const auto doc = app.parser.parse(source);
    check(doc.success, "script source parses");
    app.root = doc.root;
    layoutDocument(app);
}
const App::LayoutTextRun* runFor(const App& app, const std::wstring& text) {
    for (const auto& run : app.layoutTextRuns)
        if (run.docLength && app.docText.substr(run.docStart, run.docLength) == text) return &run;
    return nullptr;
}
float baseline(const App::LayoutTextRun& run) {
    UINT32 count = 0;
    run.layout->GetLineMetrics(nullptr, 0, &count);
    std::vector<DWRITE_LINE_METRICS> lines(count);
    if (!count || FAILED(run.layout->GetLineMetrics(lines.data(), count, &count))) {
        check(false, "DirectWrite supplies line metrics");
        return 0;
    }
    return run.pos.y + lines.front().baseline;
}
void scriptLayout(App& app, const std::string& fixture) {
    const float smallSize = 16.0f * app.contentScale * app.zoomFactor * 0.68f;
    check(app.supSubFormat && closeEnough(app.supSubFormat->GetFontSize(), smallSize),
          "document format refresh retains the small script font (#206)");
    auto* original = app.supSubFormat;
    updateOverlayFormats(app);
    check(original && app.supSubFormat == original,
          "overlay refresh cannot release the document script font");
    if (app.supSubFormat) {
        wchar_t font[256]{};
        app.supSubFormat->GetFontFamilyName(font, 256);
        check(std::wstring(font) == app.theme.fontFamily, "scripts use the selected document font");
    }
    for (const char* source : {
        "base^SUP^ base~SUB~\n", "# base^SUP^ base~SUB~\n",
        "### base^SUP^ base~SUB~\n", "## *base^SUP^ base~SUB~*\n",
        "> base^SUP^ base~SUB~\n",
        "- base^SUP^ base~SUB~\n", "**base^SUP^** *base~SUB~*\n",
        "[base^SUP^](#target) base~SUB~\n",
        "~~base^SUP^~~ ==base~SUB~==\n", "==base^SUP^ ~~base~SUB~~~==\n",
        "| Header |\n| --- |\n| base^SUP^ base~SUB~ |\n"}) {
        layout(app, source);
        const auto* base = runFor(app, L"base");
        const auto* sup = runFor(app, L"SUP");
        const auto* sub = runFor(app, L"SUB");
        check(base && sup && sub, "both script runs survive the surrounding Markdown");
        if (base && sup && sub) {
            // Small in proportion to the text around it: a heading's
            // scripts scale with the heading (#263)
            const float around = base->layout->GetFontSize() * 0.68f;
            check(closeEnough(sup->layout->GetFontSize(), around) && closeEnough(sub->layout->GetFontSize(), around),
                  "native heading, table, quote, list and nested runs use small text");
            check(baseline(*sub) > baseline(*sup), "subscript is lower than superscript");
            check(baseline(*sup) < baseline(*base) && baseline(*sub) > baseline(*base),
                  "scripts rise above and drop below the baseline of the text around them");
            check(sup->pos.y >= sup->bounds.top - 0.01f, "a superscript stays inside its line");
        }
    }
    layout(app, "base^SUP^ base~SUB~\n");
    const auto* normal = runFor(app, L"base");
    const auto* sup = runFor(app, L"SUP");
    const auto* sub = runFor(app, L"SUB");
    check(normal && sup && sub, "baseline comparison contains all three runs");
    if (normal && sup && sub) {
        check(baseline(*sup) < baseline(*normal), "superscript rises above the normal baseline");
        check(baseline(*sub) > baseline(*normal), "subscript falls below the normal baseline");
    }
    layout(app, fixture);
    check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1,
          "mixed table and code fence remain intact");
    check(app.docText.find(L"All surrounding content") != std::wstring::npos,
          "mixed fixture renders through the final paragraph");
    check(app.docText.find(L"x^2^ H~2~O") != std::wstring::npos,
          "inline code keeps script delimiters literal");
}
// Inline code sits on the baseline of the words beside it (#259, #262),
// whatever the ascents of the body and code fonts
std::wstring familyOf(const App::LayoutTextRun& run) {
    wchar_t name[128]{};
    run.layout->GetFontFamilyName(0, name, 128, nullptr);
    return name;
}
// A theme of its own with the built-in's colours and another pair of faces:
// how a body and a code face are chosen. Its place in the theme list.
int themeWithFaces(int builtin, const wchar_t* body, const wchar_t* code) {
    static int made = 0;
    const std::filesystem::path themes = std::filesystem::current_path() / "themes";
    std::filesystem::create_directories(themes);
    const std::wstring name = L"Faces" + std::to_wstring(++made);
    {
        std::ofstream(themes / (name + L".css"), std::ios::binary)
            << "@import " << '"' << toUtf8(THEMES[builtin].name) << '"' << ";\n"
            << "body { font-family: " << '"' << toUtf8(body) << '"' << "; }\n"
            << "code, pre { font-family: " << '"' << toUtf8(code) << '"' << "; }\n";
    }
    loadCustomThemes();
    for (int i = THEME_COUNT; i < themeCount(); i++) {
        if (name == themeAt(i).name) return i;
    }
    return -1;
}
void codeBaselines(App& app, const std::string& fixture) {
    struct Fonts { const wchar_t* body; const wchar_t* code; };
    const Fonts pairs[] = {{nullptr, nullptr}, {L"Segoe UI", L"Courier New"},
                           {L"Georgia", L"Lucida Console"},
                           {L"Microsoft YaHei UI", L"Cascadia Mono"}, {L"SimSun", L"Consolas"}};
    for (int theme : {0, 5}) {
        for (const auto& fonts : pairs) {
            for (float scale : {1.0f, 2.0f}) {
                const int chosen = fonts.body ? themeWithFaces(theme, fonts.body, fonts.code) : theme;
                check(chosen >= 0, "the theme with the pair of faces is listed");
                check(!fonts.body || chosen < 0 || (_wcsicmp(themeAt(chosen).fontFamily, fonts.body) == 0 &&
                                                    _wcsicmp(themeAt(chosen).codeFontFamily, fonts.code) == 0),
                      "and names them");
                applyTheme(app, chosen < 0 ? theme : chosen);
                app.contentScale = scale;
                app.zoomFactor = 1;
                updateTextFormats(app);
                for (int width : {650, 1050}) {
                    app.width = static_cast<int>(width * scale);
                    layout(app, fixture);
                    int codeRuns = 0, compared = 0, misaligned = 0, pinned = 0, displaced = 0;
                    auto inBlock = [&](const App::LayoutTextRun& run) {
                        for (const auto& block : app.codeBlocks) {
                            if (run.bounds.top >= block.bounds.top - 1 && run.bounds.bottom <= block.bounds.bottom + 1) {
                                return true;
                            }
                        }
                        return false;
                    };
                    for (const auto& code : app.layoutTextRuns) {
                        if (!code.docLength || _wcsicmp(familyOf(code).c_str(), app.theme.codeFontFamily) != 0) continue;
                        if (inBlock(code)) continue;  // the fenced block
                        ++codeRuns;
                        for (const auto& text : app.layoutTextRuns) {
                            // Text on the same line, Latin or drawn by a fallback font
                            if (&text == &code || !text.docLength || std::abs(text.bounds.top - code.bounds.top) > 0.5f ||
                                _wcsicmp(familyOf(text).c_str(), app.theme.codeFontFamily) == 0) continue;
                            ++compared;
                            const float gap = baseline(code) - baseline(text);
                            if (std::abs(gap) > 0.6f) {
                                ++misaligned;
                                std::cerr << "  code '" << toUtf8(app.docText.substr(code.docStart, code.docLength))
                                          << "' vs '" << toUtf8(app.docText.substr(text.docStart, text.docLength))
                                          << "': " << gap << " px (theme " << theme << ", scale " << scale
                                          << ", width " << width << ")\n";
                            }
                        }
                    }
                    check(codeRuns >= 16 && compared >= 16, "the fixture's inline code meets text on its lines");
                    check(misaligned == 0, "inline code shares the baseline of the text beside it");
                    // Plain Latin text keeps the place its own font gives it
                    // in the line box: the reference code is aligned to
                    for (const auto& text : app.layoutTextRuns) {
                        if (!text.docLength || inBlock(text) ||
                            _wcsicmp(familyOf(text).c_str(), app.theme.codeFontFamily) == 0) continue;
                        const std::wstring s = app.docText.substr(text.docStart, text.docLength);
                        if (!std::all_of(s.begin(), s.end(), [](wchar_t c) { return c < 128; })) continue;
                        ++pinned;
                        if (std::abs(text.pos.y - text.bounds.top) > 0.01f) {
                            ++displaced;
                            std::cerr << "  text '" << toUtf8(s) << "' moved " << text.pos.y - text.bounds.top
                                      << " px (theme " << theme << ", scale " << scale << ")\n";
                        }
                    }
                    check(pinned >= 20 && displaced == 0, "Latin text sits where its own font puts it");
                    // Emphasis in a heading keeps the heading's size
                    auto sizeOf = [&](const wchar_t* word) {
                        const size_t at = app.docText.find(word);
                        for (const auto& run : app.layoutTextRuns) {
                            if (run.docLength && run.docStart <= at && at < run.docStart + run.docLength) {
                                return run.layout->GetFontSize();
                            }
                        }
                        return 0.0f;
                    };
                    check(sizeOf(L"The ") > 0 && sizeOf(L"emphasis") == sizeOf(L"The "),
                          "italic heading words keep the heading size");
                }
            }
        }
    }
    applyTheme(app, 0);
}
void learnCopies(App& app, const std::filesystem::path& dir) {
    for (const char* profile : {"windows", "vim", "custom"}) {
        Settings settings;
        settings.keyProfile = profile;
        settings.keyOverrides = {{"edit", "F2"}};
        applyKeymap(app, settings);
        const std::string label = toUtf8(keyLabel(app.keymap[KA_EDIT]));
        for (int card = 0; card < 3; ++card) {
            tabBecomeStartPage(app, app.hwnd);
            startPageOpenEmbedded(app, app.hwnd, card);
            const std::string original = app.sourceText;
            const std::string hint = card == 1 ? "Press " + label + " to edit" : "**" + label + "**";
            check(original.find(hint) != std::string::npos && original.find("{{EDIT_KEY}}") == std::string::npos,
                  "Learn edit hint matches Windows, Vim and custom keymaps");
            const auto key = app.keymap[KA_EDIT];
            if (key.isChar) typeKeys(app, std::wstring(1, (wchar_t)key.key));
            else keystroke(app, key.key);
            check(app.editMode && app.editorDirty && app.currentFile.empty() && !app.startPageEmbeddedOpen,
                  "the configured shortcut opens an unsaved untitled Learn copy");
            check(toUtf8(app.editorText) == original, "editable source matches the displayed example");
            wchar_t title[256]{};
            GetWindowTextW(app.hwnd, title, 256);
            check(std::wstring(title).find(tr(app, "title.untitled")) != std::wstring::npos,
                  "Learn copy uses the untitled window title");
            editorReplaceRangeExternal(app, app.editorText.size(), app.editorText.size(), L"\nMy change: z^3^.\n");
            const std::wstring edited = app.editorText;
            const int copyTab = app.activeTab;
            tabOpenStartPage(app, app.hwnd);
            tabActivate(app, app.hwnd, copyTab);
            check(app.editMode && app.editorDirty && app.editorText == edited,
                  "switching tabs preserves the unsaved Learn copy");
            tabCloseIndex(app, app.hwnd, copyTab);
            check(app.confirmExitPending && app.pendingTabClose == copyTab,
                  "closing a Learn copy asks about its unsaved content");
            confirmExitAction(app, app.hwnd, 3);
            check(app.editMode && app.editorText == edited && !app.confirmExitPending,
                  "Keep editing retains the entire Learn copy");

            // Supply the path a Save As selection would return; do not open
            // a native modal dialog in the unattended test executable. The
            // file exists, as one Save As picked would: a save to a path with
            // no file behind it asks for a new one (#275), and that dialog
            // waits for an answer nobody gives here.
            const auto saved = dir / (std::string(profile) + "-learn-" + std::to_string(card) + ".md");
            std::ofstream(saved, std::ios::binary).close();
            app.currentFile = toUtf8(saved.wstring());
            saveEditorFile(app, app.hwnd);
            check(!app.editorDirty && read(saved) == toUtf8(edited), "Learn copy saves through the normal file writer");
            // An unconsumed S would type 's' if Ctrl were released before
            // the loop translated it
            const bool consumed = keystroke(app, 'S', {true}, L's');
            check(consumed, "Save consumes S before a released Ctrl can turn it into editor text");
            check(!app.editorDirty && app.editorText == edited, "saving cannot append a stray shortcut character");
            exitEditMode(app);
            tabBecomeStartPage(app, app.hwnd);
            startPageOpenEmbedded(app, app.hwnd, card);
            check(app.sourceText == original, "reopening Learn preserves its embedded original");
        }
    }
    Settings punctuation;
    punctuation.keyProfile = "custom";
    punctuation.keyOverrides = {{"edit", "*"}};
    applyKeymap(app, punctuation);
    tabBecomeStartPage(app, app.hwnd);
    startPageOpenEmbedded(app, app.hwnd, 2);
    layoutDocument(app);
    check(app.docText.find(L"Press * to edit") != std::wstring::npos,
          "custom punctuation shortcut cannot become Markdown formatting");
    tabBecomeStartPage(app, app.hwnd);
    enterEditMode(app);
    check(!app.editMode && startPageActive(app), "empty launcher still has no example to edit");
}
}

int runSuperscriptTests() {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = fs::path(exe).parent_path();
    const auto settings = read(dir / "settings.ini");
    if (!fs::equivalent(dir, fs::current_path()) ||
        (settings != "; Isolated superscript test configuration\n" &&
         settings != "; Isolated superscript test configuration\r\n")) {
        std::cerr << "Run through CTest's isolated superscript wrapper\n";
        return 2;
    }
    check(fs::equivalent(tintaConfigDir(), dir), "test persistence stays in its portable directory");
    const auto fixture = read(TINTA_SUPERSCRIPT_FIXTURE);
    check(!fixture.empty(), "mixed script fixture loads");
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        app.hwnd = CreateWindowExW(0, L"STATIC", L"Superscript tests", WS_POPUP,
            0, 0, 1050, 900, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!app.hwnd || !initD2D(app)) return 2;
        app.readingWidthPct = 100;
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f, 2.0f}) {
                app.contentScale = scale;
                for (float zoom : {0.8f, 1.0f, 1.5f}) {
                    app.zoomFactor = zoom;
                    updateTextFormats(app);
                    for (int width : {650, 1050}) {
                        app.width = static_cast<int>(width * scale);
                        app.height = static_cast<int>(900 * scale);
                        scriptLayout(app, fixture);
                    }
                }
            }
        }
        const auto codeFixture = read(TINTA_CODE_BASELINE_FIXTURE);
        check(!codeFixture.empty(), "inline code baseline fixture loads");
        codeBaselines(app, codeFixture);
        app.contentScale = app.zoomFactor = 1;
        updateTextFormats(app);
        learnCopies(app, dir);
        DestroyWindow(app.hwnd);
        app.hwnd = nullptr;
        app.shutdown();
        check(!app.supSubFormat, "shutdown releases the document script font");
    }
    CoUninitialize();
    std::cout << "Superscripts and Learn copies: " << failures << " failures\n";
    return failures ? 1 : 0;
}
