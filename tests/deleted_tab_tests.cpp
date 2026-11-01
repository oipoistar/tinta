#include "tabs.h"
#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "render.h"
#include "print.h"
#include "settings.h"
#include "utils.h"
#include "overlays.h"
#include "i18n.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <commdlg.h>
#include <dlgs.h>

#include "settings_preview.h"
#include "startpage.h"

#include "save_picker.h"

int runDeletedTabTests() {
    namespace fs = std::filesystem;
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    auto read = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const auto dir = fs::current_path();
    const auto settings = read(dir / "settings.ini");
    if (settings != "; Isolated deleted tab test configuration\n" &&
        settings != "; Isolated deleted tab test configuration\r\n") return 2;
    loadLanguageOverrides();
    std::string source = read(TINTA_DELETED_TAB_FIXTURE);
    // Git's Windows checkout may use CRLF; the viewer reads in text mode.
    source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
    check(!source.empty(), "mixed Markdown fixture loads");
    const auto a = dir / L"Temporary \u0437\u0430\u043c\u0435\u0442\u043a\u0430.md";
    const auto b = dir / "Other.md";
    const auto empty = dir / "Empty.md";
    std::ofstream(a, std::ios::binary) << source;
    std::ofstream(b) << "# Other document\n";
    std::ofstream(empty).close();
    auto state = std::make_unique<App>();
    App& app = *state;
    check(!app.keepDeletedTabs && !loadSettings().keepDeletedTabs,
          "retaining deleted files is off by default");
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Deleted tab test", WS_POPUP,
        0, 0, 1000, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 1;
    check(openDocumentInViewer(app, a.wstring()), "initial file opens");
    tabsInit(app);
    check(!app.tabs[0].viewerSource, "disabled setting keeps no per-tab source copy");
    app.showSettings = true;
    app.settingsAnimation = 1;
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {650, 1050}) {
            app.width = width;
            app.height = 900;
            app.renderTarget->BeginDraw();
            renderSettingsOverlay(app);
            check(SUCCEEDED(app.renderTarget->EndDraw()), "settings render at normal and narrow widths");
            bool found = false;
            const auto panel = settingsPanelRect(app);
            for (const auto& hit : app.settingsHits) if (hit.second == SET_TOGGLE_KEEPDELETED) {
                found = true;
                check(hit.first.top > panel.top && hit.first.bottom < panel.bottom,
                      "memory toggle remains inside the settings panel");
            }
            check(found, "General settings expose the memory toggle");
            check(saveSettingsPreview(app, dir / ("settings-" + std::to_string(theme) + "-" + std::to_string(width) + ".png")),
                  "settings preview exports for visual inspection");
        }
    }
    check(std::wstring(tr(app, "settings.keep_deleted_tabs.hint")).find(L"RAM") != std::wstring::npos,
          "the setting explicitly warns about RAM usage");
    auto clickMemoryToggle = [&]() {
        D2D1_RECT_F rect{};
        for (const auto& hit : app.settingsHits)
            if (hit.second == SET_TOGGLE_KEEPDELETED) rect = hit.first;
        const auto point = MAKELPARAM((int)((rect.left + rect.right) / 2),
                                     (int)((rect.top + rect.bottom) / 2));
        handleMouseUp(app, app.hwnd, 0, point);
    };
    clickMemoryToggle();
    check(app.keepDeletedTabs && loadSettings().keepDeletedTabs && app.tabs[0].viewerSource == source,
          "settings click enables retention immediately and persists the choice");
    clickMemoryToggle();
    check(!app.keepDeletedTabs && !loadSettings().keepDeletedTabs && !app.tabs[0].viewerSource,
          "disabling retention frees existing copies immediately and persists the choice");
    clickMemoryToggle();
    app.showSettings = false;
    fs::remove(a); // deletion while the tab is still active
    tabOpenPath(app, app.hwnd, toUtf8(b.wstring()));
    check(app.sourceText == "# Other document\n", "second tab shows its own source");
    tabActivate(app, app.hwnd, 0);
    check(app.currentFile == toUtf8(a.wstring()) && app.sourceText == source,
          "deleted active file returns with its path and full source");
    check(app.tabs[0].fileMissing, "missing indicator remains truthful");
    check(!fs::exists(dir / "tab-cache"), "viewing deleted documents creates no disk cache");
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {500, 1000}) {
            app.width = width;
            app.height = 800;
            layoutDocument(app);
            check(app.root && app.contentHeight > 0, "cached document lays out at normal and narrow widths");
        }
    }
    check(printDebugPages(app, (dir / "cached-pages").wstring()) > 0,
          "cached document renders through the native print harness");
    enterEditMode(app);
    check(app.editMode && app.editorDirty && toUtf8(app.editorText) == source,
          "missing file can be edited and recovered");
    app.editorText += L"\nUnsaved change\n";
    editorReparse(app, true);
    confirmExitAction(app, app.hwnd, 2);
    check(!app.editMode && app.sourceText == source,
          "discarding edits to a deleted file restores its last viewed source");
    enterEditMode(app);
    tabActivate(app, app.hwnd, 1);
    tabActivate(app, app.hwnd, 0);
    check(app.editMode && app.editorDirty && toUtf8(app.editorText) == source,
          "parked recovery editor survives switching");
    const auto cancelled = withSavePicker(nullptr, [&] { saveEditorFile(app, app.hwnd); });
    // Windows hides known extensions by default, and the dialog then shows
    // the name without .md
    check(cancelled.shown == 1 && (cancelled.suggestedName == a.filename().wstring() ||
                                   cancelled.suggestedName == a.stem().wstring()),
          "Save on a missing original opens Save As with its previous name");
    check(!fs::exists(a) && app.editMode && app.editorDirty &&
          app.currentFile == toUtf8(a.wstring()) && toUtf8(app.editorText) == source,
          "cancelling keeps the original deleted and preserves the editor text and path");
    const auto recovered = dir / "Recovered.md";
    fs::remove(recovered);
    const std::wstring recoveredName = recovered.wstring();
    const auto chosen = withSavePicker(recoveredName.c_str(), [&] { saveEditorFile(app, app.hwnd); });
    check(chosen.shown == 1 && !app.editorDirty && read(recovered) == source && !fs::exists(a),
          "confirmation saves to the explicitly chosen new path without recreating the original");
    check(app.currentFile == toUtf8(recoveredName) && app.tabs[0].path == app.currentFile,
          "the recovered tab follows the chosen path");
    fs::remove(recovered); // deletion while a clean editor is still open
    const auto cancelExit = withSavePicker(nullptr, [&] { confirmExitAction(app, app.hwnd, 1); });
    check(cancelExit.shown == 1 && app.editMode && app.editorDirty && !fs::exists(recovered),
          "cancelling Save As during save-and-exit also preserves a clean recovery buffer");
    const auto sameName = withSavePicker(recoveredName.c_str(), [&] { saveEditorFile(app, app.hwnd); });
    check(sameName.shown == 1 && !app.editorDirty && read(recovered) == source,
          "the old name is permitted after explicit Save As confirmation");
    app.editorText += L"\nExisting-file edit\n";
    app.editorDirty = true;
    const auto ordinary = withSavePicker(nullptr, [&] { saveEditorFile(app, app.hwnd); });
    check(ordinary.shown == 0 && !app.editorDirty && read(recovered) == toUtf8(app.editorText),
          "an existing file still saves directly");
    exitEditMode(app);
    tabActivate(app, app.hwnd, 1);
    std::ofstream(recovered) << "# Recreated externally\n";
    tabActivate(app, app.hwnd, 0);
    check(app.sourceText == "# Recreated externally\n" && !app.tabs[0].fileMissing,
          "recreated file supersedes cached source");
    tabOpenPath(app, app.hwnd, toUtf8(empty.wstring()), false);
    fs::remove(empty);
    tabActivate(app, app.hwnd, 2);
    check(app.activeTab == 2 && app.sourceText.empty(), "empty file has a valid cached copy");
    tabOpenPath(app, app.hwnd, toUtf8((dir / "Never-existed.md").wstring()));
    check(app.activeTab == 2 && app.currentFile == toUtf8(empty.wstring()),
          "uncached failed open cannot steal the current document");
    // Session-seeded inactive tabs must be cached before their first activation.
    std::ofstream(a) << source;
    openDocumentInViewer(app, b.wstring());
    tabsSeedSession(app, {toUtf8(a.wstring()), toUtf8(b.wstring())});
    fs::remove(a);
    tabActivate(app, app.hwnd, 0);
    check(app.sourceText == source, "inactive restored tab survives deletion before first click");
    tabBecomeStartPage(app, app.hwnd);
    check(!app.tabs[0].viewerSource && !app.tabs[0].fileMissing && app.sourceText.empty(),
          "closing last document clears cached state");
    std::ofstream(a) << source;
    tabOpenPath(app, app.hwnd, toUtf8(a.wstring()));
    const int closing = app.activeTab;
    fs::remove(a);
    tabCloseIndex(app, app.hwnd, closing);
    check(app.activeTab != closing && !fs::exists(dir / "tab-cache"),
          "closing an active tab releases its copy without writing a disk cache");
    app.tabs.clear();
    tabsSeedSession(app, {toUtf8(a.wstring()), toUtf8(b.wstring())});
    tabActivate(app, app.hwnd, 0);
    check(app.activeTab == 1 && app.currentFile == toUtf8(b.wstring()),
          "discarding all in-memory tabs leaves no recovery copy for the missing file");
    std::ofstream(a) << source;
    tabsSetKeepDeleted(app, false);
    tabOpenPath(app, app.hwnd, toUtf8(a.wstring()));
    const int disabledTab = app.activeTab;
    tabActivate(app, app.hwnd, 1);
    fs::remove(a);
    tabActivate(app, app.hwnd, disabledTab);
    check(app.activeTab == 1 && app.currentFile == toUtf8(b.wstring()),
          "disabled retention does not reopen a deleted file");
    for (const auto& tab : app.tabs) check(!tab.viewerSource, "disabled tabs keep no source copies");
    const auto removedFolder = dir / "Removed-folder";
    fs::create_directory(removedFolder);
    const auto removedFile = removedFolder / "Gone.md";
    std::ofstream(removedFile, std::ios::binary) << source;
    tabOpenPath(app, app.hwnd, toUtf8(removedFile.wstring()));
    enterEditMode(app);
    fs::remove(removedFile);
    fs::remove(removedFolder);
    const auto folderCopy = dir / "Folder-recovery.md";
    fs::remove(folderCopy);
    const auto folderName = folderCopy.wstring();
    const auto folderSave = withSavePicker(folderName.c_str(), [&] { saveEditorFile(app, app.hwnd); });
    check(folderSave.shown == 1 &&
              (folderSave.suggestedName == L"Gone.md" || folderSave.suggestedName == L"Gone") &&
          !app.editorDirty && read(folderCopy) == source && !fs::exists(removedFolder),
          "a deleted parent folder also requires Save As and is never recreated silently");
    // Closing must handle a neighbor that cannot be activated with retention off.
    // Use fresh native app states so an invalid index is reported before any
    // later operation could accidentally index the broken tab list.
    for (int scenario = 0; scenario < 4; ++scenario) {
        const auto closeDir = dir / ("close-missing-" + std::to_string(scenario));
        fs::create_directories(closeDir);
        const auto first = closeDir / "First.md";
        const auto second = closeDir / "Second.md";
        const auto third = closeDir / "Third.md";
        for (const auto& path : {first, second, third})
            std::ofstream(path, std::ios::binary) << source;
        auto closeState = std::make_unique<App>();
        App& closingApp = *closeState;
        closingApp.hwnd = CreateWindowExW(0, L"STATIC", L"Close missing tab test", WS_POPUP,
            0, 0, 1000, 800, nullptr, nullptr, nullptr, nullptr);
        if (!closingApp.hwnd || !initD2D(closingApp) || !createRenderTarget(closingApp)) return 1;
        applyTheme(closingApp, 0);
        check(!closingApp.keepDeletedTabs, "close regression uses default disabled retention");
        openDocumentInViewer(closingApp, first.wstring());
        tabsInit(closingApp);
        tabOpenPath(closingApp, closingApp.hwnd, toUtf8(second.wstring()), false);
        if (scenario >= 2) tabOpenPath(closingApp, closingApp.hwnd, toUtf8(third.wstring()), false);
        const int active = scenario == 0 ? 1 : scenario == 3 ? 2 : 0;
        tabActivate(closingApp, closingApp.hwnd, active);
        if (scenario == 0 || scenario == 3) fs::remove(first);
        if (scenario != 0) fs::remove(second);
        if (scenario == 2) {
            tabCycle(closingApp, closingApp.hwnd, 1);
            check(closingApp.activeTab == 2 && closingApp.currentFile == toUtf8(third.wstring()),
                  "forward tab cycling skips an unavailable neighbor");
            tabCycle(closingApp, closingApp.hwnd, -1);
            check(closingApp.activeTab == 0 && closingApp.currentFile == toUtf8(first.wstring()),
                  "backward tab cycling skips an unavailable neighbor");
            tabActivate(closingApp, closingApp.hwnd, active);
        }
        tabCloseIndex(closingApp, closingApp.hwnd, active);
        const bool valid = closingApp.activeTab >= 0 &&
            closingApp.activeTab < (int)closingApp.tabs.size();
        check(valid, "closing beside deleted tabs leaves a valid active index");
        if (valid) {
            const auto& selected = closingApp.tabs[closingApp.activeTab];
            check(selected.path == closingApp.currentFile,
                  "the surviving tab stays attached to its own document");
            if (scenario >= 2 && scenario != 3) {
                check(closingApp.tabs.size() == 2 && closingApp.currentFile == toUtf8(third.wstring()) &&
                      closingApp.sourceText == source,
                      "closing skips the missing neighbor and selects another available tab");
            } else {
                check(closingApp.currentFile.empty() && selected.path.empty() &&
                      closingApp.sourceText.empty() && !closingApp.editMode && startPageActive(closingApp),
                      "when every other tab is missing closing lands on the start page");
                check(closingApp.tabs.size() == (scenario == 3 ? 3u : 2u),
                      "unavailable tabs are retained beside the replacement start page");
            }
        }
        DestroyWindow(closingApp.hwnd);
    }
    {
        const auto missing = dir / "audit-missing.md";
        const auto live = dir / "audit-live.md";
        std::ofstream(live, std::ios::binary) << source;
        auto auditState = std::make_unique<App>();
        App& audit = *auditState;
        audit.hwnd = CreateWindowExW(0, L"STATIC", L"Unavailable activation audit", WS_POPUP,
            0, 0, 1050, 900, nullptr, nullptr, nullptr, nullptr);
        if (!audit.hwnd || !initD2D(audit) || !createRenderTarget(audit)) return 1;
        audit.width = 1050; audit.height = 900;
        applyTheme(audit, 0);
        auto reset = [&] {
            if (audit.editMode) { audit.editorDirty = false; exitEditMode(audit); }
            std::ofstream(missing, std::ios::binary) << source;
            openDocumentInViewer(audit, live.wstring());
            tabsSeedSession(audit, {toUtf8(missing.wstring()), toUtf8(live.wstring())});
            fs::remove(missing);
        };
        reset();
        audit.navBack = {{toUtf8(missing.wstring()), 12345.0f}};
        audit.navForward.clear();
        audit.pendingScrollRestore = -1;
        navigateBack(audit, audit.hwnd);
        check(audit.currentFile == toUtf8(live.wstring()) && audit.navForward.empty() &&
              audit.pendingScrollRestore != 12345.0f,
              "history rejects a missing tab without applying its scroll to another document");
        audit.renderTarget->BeginDraw(); renderTabStrip(audit); audit.renderTarget->EndDraw();
        for (const auto& hit : audit.tabHits) if (hit.index == 0) {
            const int x = (int)hit.rect.left + 10;
            const int y = (int)((hit.rect.top + hit.rect.bottom) / 2);
            tabStripMouseDown(audit, audit.hwnd, x, y, false);
            check(audit.tabDragIndex == -1 && audit.activeTab == 1,
                  "an unavailable tab cannot arm a drag of another document");
            tabDragCancel(audit, audit.hwnd);
            break;
        }
        const std::string missingPath = toUtf8(missing.wstring());
        COPYDATASTRUCT unavailable{1, (DWORD)missingPath.size() + 1, (void*)missingPath.c_str()};
        check(!tabReceiveCopyData(audit, audit.hwnd, unavailable) && audit.activeTab == 1,
              "a receiver does not acknowledge a missing file it could not activate");
        enterRecoveredDraft(audit, audit.hwnd, "# Recovered draft", missingPath);
        check(audit.currentFile == missingPath && audit.editorDirty &&
              audit.editorText == L"# Recovered draft" && audit.tabs[1].path == toUtf8(live.wstring()),
              "recovering a missing-file draft never replaces a different live document");
        reset();
        audit.tabBulkCloseMode = 1;
        audit.tabBulkKeepId = audit.tabs[0].id;
        tabBulkCloseStep(audit, audit.hwnd);
        check(audit.tabBulkCloseMode == 0 && audit.activeTab >= 0 &&
              audit.activeTab < (int)audit.tabs.size() && audit.currentFile.empty(),
              "Close Others with an unavailable kept tab stops safely on the launcher");
        DestroyWindow(audit.hwnd);
    }
    DestroyWindow(app.hwnd);
    std::cout << "Deleted tabs: " << failures << " failures\n";
    return failures ? 1 : 0;
}
