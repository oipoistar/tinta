#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "input.h"
#include "render.h"
#include "settings.h"
#include "startpage.h"
#include "tabs.h"
#include "utils.h"

#include <cmath>
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
        "### base^SUP^ base~SUB~\n", "> base^SUP^ base~SUB~\n",
        "- base^SUP^ base~SUB~\n", "**base^SUP^** *base~SUB~*\n",
        "[base^SUP^](#target) base~SUB~\n",
        "| Header |\n| --- |\n| base^SUP^ base~SUB~ |\n"}) {
        layout(app, source);
        const auto* sup = runFor(app, L"SUP");
        const auto* sub = runFor(app, L"SUB");
        check(sup && sub, "both script runs survive the surrounding Markdown");
        if (sup && sub) {
            check(closeEnough(sup->layout->GetFontSize(), smallSize) && closeEnough(sub->layout->GetFontSize(), smallSize),
                  "native heading, table, quote, list and nested runs use small text");
            check(baseline(*sub) > baseline(*sup), "subscript is lower than superscript");
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
            if (key.isChar) handleCharInput(app, app.hwnd, key.key);
            else handleKeyDown(app, app.hwnd, key.key);
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
            // a native modal dialog in the unattended test executable.
            const auto saved = dir / (std::string(profile) + "-learn-" + std::to_string(card) + ".md");
            app.currentFile = toUtf8(saved.wstring());
            saveEditorFile(app, app.hwnd);
            check(!app.editorDirty && read(saved) == toUtf8(edited), "Learn copy saves through the normal file writer");
            BYTE previousKeys[256]{};
            GetKeyboardState(previousKeys);
            BYTE saveKeys[256]{};
            saveKeys[VK_CONTROL] = 0x80;
            SetKeyboardState(saveKeys);
            const bool consumed = handleKeyDown(app, app.hwnd, 'S');
            SetKeyboardState(previousKeys);
            check(consumed, "Save consumes S before a released Ctrl can turn it into editor text");
            if (!consumed) handleCharInput(app, app.hwnd, 's');
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
