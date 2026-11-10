// The print preview over a document being edited (print.h). The preview lays
// the page out with the editor put aside: app.editMode is off until it
// closes. Whatever asked "are there unsaved edits?" by editMode alone got
// "no" while a preview showed: the window closed over them without asking and
// took their draft with it, a change on disk replaced them, another document
// opened over them. Checked here through the functions the window calls.
#include "d2d_init.h"
#include "drafts.h"
#include "editor.h"
#include "input.h"
#include "keystroke.h"
#include "print.h"
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

namespace {
namespace fs = std::filesystem;

int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL preview unsaved: " << message << '\n';
        ++failures;
    }
}

std::string bytesOf(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

size_t draftsKept() {
    std::error_code ec;
    size_t n = 0;
    for (const auto& entry : fs::directory_iterator(fs::current_path() / "drafts", ec)) n += entry.is_regular_file();
    return n;
}

void preview(App& app) {
    app.printPreviewPaper = 0;  // A4, without asking the default printer
    app.printPreviewLandscape = false;
    openPrintPreview(app, app.hwnd);
    check(app.showPrintPreview && !app.editMode, "the preview shows, the editor put aside");
}

void everything(App& app, const char* fixture) {
    const fs::path dir = fs::current_path() / "preview-unsaved";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path doc = dir / "Edited.md", other = dir / "Other.md";
    fs::copy_file(fixture, doc, ec);
    fs::copy_file(fixture, other, ec);
    check(!ec, "the fixture copies");
    const std::string onDisk = bytesOf(doc);

    tabsInit(app);
    check(openDocumentInViewer(app, doc.wstring()), "the fixture opens");
    enterEditMode(app);
    for (int i = 0; i < 6; ++i) keystroke(app, 'Q');
    check(app.editMode && app.editorDirty && app.editorText.find(L"qqqqqq") != std::wstring::npos, "six letters are typed and not saved");
    const std::wstring edited = app.editorText;

    // A draft is kept while the preview shows, not deleted
    preview(app);
    check(app.unsavedNow(), "under the preview the document still has its unsaved edits");
    draftsSweep(app);
    check(draftsKept() == 1, "the draft sweep keeps the edits' draft while the preview shows: " + std::to_string(draftsKept()));

    // The window asked to close: the dialog, not the end of the edits
    check(windowCloseAsks(app, app.hwnd), "closing the window over the preview asks about the edits");
    check(!app.showPrintPreview && app.editMode && app.editorDirty && app.editorText == edited, "the preview is closed and the editor has its text");
    check(app.confirmExitPending && app.pendingWindowClose, "the unsaved-changes dialog is open, for the window");
    confirmExitAction(app, app.hwnd, 3);
    check(!app.confirmExitPending && !app.pendingWindowClose && app.editMode && app.editorText == edited, "Keep editing keeps them");

    // The tab asked to close
    preview(app);
    tabCloseIndex(app, app.hwnd, app.activeTab);
    check(!app.showPrintPreview && app.confirmExitPending && app.pendingTabClose == app.activeTab, "closing the tab over the preview asks too");
    confirmExitAction(app, app.hwnd, 3);
    check(app.tabs.size() == 1 && app.editMode && app.editorText == edited, "and Keep editing keeps the tab");

    // The file changed by another program: the prompt, not the other text
    preview(app);
    Sleep(1100);
    std::ofstream(doc, std::ios::binary) << "# Written by another program\n";
    handleFileWatchTimer(app, app.hwnd);
    check(!app.showPrintPreview && app.confirmExitPending && app.confirmExitExternal, "a change on disk under the preview asks: Reload or Keep mine");
    check(app.editMode && app.editorText == edited, "and the edits are still in the editor");
    app.confirmExitPending = app.confirmExitExternal = false;

    // Another document opened: the edits are parked with their tab
    preview(app);
    tabOpenPath(app, app.hwnd, toUtf8(other.wstring()), true);
    check(!app.showPrintPreview && app.tabs.size() == 2 && fs::path(toWide(app.currentFile)) == other, "another document opens in its tab, the preview closed");
    check(!app.editMode || app.editorText != edited, "the other document does not hold the first one's text");
    check(app.tabs[0].editMode && app.tabs[0].editorDirty && app.tabs[0].editorText == edited, "the first tab keeps its unsaved text");
    check(bytesOf(other) == onDisk, "the other file is as it was");
    tabActivate(app, app.hwnd, 0);
    check(app.editMode && app.editorDirty && app.editorText == edited, "back in its tab, the text is in the editor, unsaved");

    // A preview closed by itself gives the editor back; and with nothing
    // unsaved the window closes
    preview(app);
    closePrintPreview(app, app.hwnd);
    check(app.editMode && app.editorText == edited, "a preview closed by itself gives the editor back");
    app.editorDirty = false;
    preview(app);
    check(!app.unsavedNow() && !windowCloseAsks(app, app.hwnd) && !app.confirmExitPending, "with nothing unsaved the window closes without asking");
    exitEditMode(app);
}

}  // namespace

int runPreviewUnsavedTests(const char* fixture) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Preview unsaved tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    everything(app, fixture);

    app.editorDirty = false;
    draftsDeleteAll(app);
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Preview unsaved: " << failures << " failures\n";
    return failures ? 1 : 0;
}
