// A file another program changes while Tinta shows it (#311): the reader
// shows the new text; an editor with nothing unsaved, Source or Live, takes
// it too, the caret kept and one Ctrl+Z to take it back; an editor with
// unsaved changes asks, Reload or Keep mine. Driven through the watch
// timer, the keys and the dialog's buttons, in a folder of its own.
#include "d2d_init.h"
#include "editor.h"
#include "file_utils.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "live_text.h"
#include "overlays.h"
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
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
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Writes the file as another program does, its time moved on a step at a
// time so the watch sees each change within the clock's resolution too
int g_step = 0;
void writeOutside(const fs::path& path, const std::string& bytes) {
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << bytes;
    }
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t{};
    t.LowPart = now.dwLowDateTime;
    t.HighPart = now.dwHighDateTime;
    t.QuadPart += (ULONGLONG)++g_step * 20000000ULL;
    FILETIME stamp{t.LowPart, t.HighPart};
    SetFileTime(h, nullptr, nullptr, &stamp);
    CloseHandle(h);
}
void replaceOutside(const fs::path& path, const std::string& from, const std::string& to) {
    std::string bytes = readBytes(path);
    size_t at = bytes.find(from);
    check(at != std::string::npos, "the file holds " + from);
    if (at != std::string::npos) bytes.replace(at, from.size(), to);
    writeOutside(path, bytes);
}

bool hinted(const App& app, const char* key) {
    for (const auto& chip : app.signalChips)
        if (chip.text.rfind(tr(app, key), 0) == 0) return true;
    return false;
}

void paintEditor(App& app) {
    if (app.layoutDirty) ensureLayoutComplete(app);
    app.renderTarget->BeginDraw();
    app.renderTarget->Clear(app.theme.background);
    if (editorLiveOn(app)) renderLivePage(app);
    else renderEditor(app, editorPaneWidth(app));
    if (app.confirmExitPending) renderConfirmExitDialog(app);
    app.renderTarget->EndDraw();
}

D2D1_RECT_F hitFor(const App& app, int action) {
    for (const auto& hit : app.confirmExitHits)
        if (hit.second == action) return hit.first;
    return {};
}

const KeyMods ctrl{true, false, false};
}  // namespace

int runFileChangeTests() {
    const fs::path dir = fs::current_path();
    const fs::path note = dir / "Watched note.md", other = dir / "Other note.md";
    std::string fixture = readBytes(TINTA_FILE_CHANGES_FIXTURE);
    check(fixture.find("CHANGE_MARKER") != std::string::npos, "the mixed fixture loads");
    writeOutside(note, fixture);
    writeOutside(other, "# Other note\n\nA second document in a tab of its own.\n");

    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"File change tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr,
                               nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    // The reader: the page takes the new text, as it always has
    check(openDocumentInViewer(app, note.wstring()), "the note opens in the reader");
    tabsInit(app);
    updateFileWriteTime(app);
    replaceOutside(note, "CHANGE_MARKER", "CHANGED_ONCE");
    handleFileWatchTimer(app, app.hwnd);
    check(!app.editMode && app.sourceText.find("CHANGED_ONCE") != std::string::npos, "#311: the reader shows the new text");

    // Source with nothing unsaved: the new text, the caret where it was
    app.editorLive = false;
    enterEditMode(app);
    check(app.editMode && !editorLiveOn(app) && !app.editorDirty, "the note opens in Source");
    const size_t caret = app.editorText.find(L"Another program") + 8;
    app.editorCursorPos = app.editorSelStart = app.editorSelEnd = caret;
    app.editorHasSelection = false;
    replaceOutside(note, "CHANGED_ONCE", "CHANGED_TWICE, by a second program");
    handleFileWatchTimer(app, app.hwnd);
    check(app.editorText.find(L"CHANGED_TWICE, by a second program") != std::wstring::npos && !app.editorDirty,
          "#311: Source with nothing unsaved takes the new text and stays clean");
    check(app.editorCursorPos == caret, "#311: a change below the caret leaves it where it was");
    check(hinted(app, "toast.reloaded"), "#311: a brief note says it was reloaded");
    const std::wstring once = app.editorText;
    replaceOutside(note, "# Watched note", "# Watched note, renamed");
    handleFileWatchTimer(app, app.hwnd);
    check(app.editorCursorPos == caret + 9, "#311: a change above the caret moves it along with its text");
    check(app.editorText.find(L"Watched note, renamed") != std::wstring::npos, "#311: and the heading changed");
    paintEditor(app);
    check(app.docText.find(L"Watched note, renamed") != std::wstring::npos || !app.editorShowPreview,
          "#311: the preview beside Source shows it");
    // The reload is one step back
    const std::wstring twice = app.editorText;
    keystroke(app, 'Z', ctrl);
    check(app.editorText == once && app.editorDirty, "#311: one Ctrl+Z takes the reload back, the buffer then unsaved");
    keystroke(app, 'Y', ctrl);
    check(app.editorText == twice, "#311: and Ctrl+Y brings it again");

    // Unsaved changes when the file changes: Tinta asks, and changes nothing
    // until answered
    keystroke(app, 'Z', ctrl);
    const std::wstring mine = app.editorText;
    replaceOutside(note, "- [ ] A task the agent ticks", "- [x] A task the agent ticks");
    handleFileWatchTimer(app, app.hwnd);
    check(app.confirmExitPending && app.confirmExitExternal && app.editorText == mine,
          "#311: unsaved edits and a change on disk: Tinta asks and changes nothing yet");
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            updateTextFormats(app);
            paintEditor(app);
            D2D1_RECT_F reload = hitFor(app, 1), keep = hitFor(app, 2);
            check(reload.right > reload.left && keep.right > keep.left && reload.left >= 0 &&
                      keep.right <= (float)width && reload.right <= keep.left,
                  "#311: the question draws its Reload and Keep mine buttons inside the window, theme " +
                      std::to_string(theme) + " at " + std::to_string(width));
        }
    }
    app.width = 1050;
    updateTextFormats(app);
    check(std::wstring(tr(app, "changed.reload")) == L"Reload" && std::wstring(tr(app, "changed.keep")) == L"Keep mine",
          "#311: its buttons read Reload and Keep mine");
    // It came up unasked: Y and Enter in its first moment, and Ctrl+S, are no answer
    keystroke(app, 'Y');
    keystroke(app, VK_RETURN);
    keystroke(app, 'S', ctrl);
    check(app.confirmExitPending && app.editorText == mine, "#311: Y, Enter or Ctrl+S just as it comes up answer nothing");
    app.confirmExitOpenedAt -= std::chrono::seconds(1);
    keystroke(app, 'N');
    check(!app.confirmExitPending && !app.confirmExitExternal && app.editorText == mine && app.editorDirty,
          "#311: N keeps mine, unsaved: pending " + std::to_string(app.confirmExitPending) + " external " +
              std::to_string(app.confirmExitExternal) + " same " + std::to_string(app.editorText == mine) + " dirty " +
              std::to_string(app.editorDirty));
    // The next change asks again; its Reload button takes the disk's text
    replaceOutside(note, "- [ ] Another one", "- [x] Another one");
    handleFileWatchTimer(app, app.hwnd);
    check(app.confirmExitPending && app.confirmExitExternal, "#311: the next change asks again");
    paintEditor(app);
    const D2D1_RECT_F reload = hitFor(app, 1);
    const LPARAM at = MAKELPARAM((int)((reload.left + reload.right) / 2), (int)((reload.top + reload.bottom) / 2));
    handleMouseUp(app, app.hwnd, 0, at);
    std::wstring disk = toWide(readBytes(note));
    disk.erase(std::remove(disk.begin(), disk.end(), L'\r'), disk.end());
    check(!app.confirmExitPending && !app.editorDirty && app.editorText == disk, "#311: Reload takes the text on disk");
    keystroke(app, 'Z', ctrl);
    check(app.editorText == mine, "#311: and one Ctrl+Z gives mine back");
    keystroke(app, 'Y', ctrl);

    // Live with nothing unsaved takes the change as Source does
    editorReloadFromDisk(app, app.hwnd);
    setEditorLive(app, app.hwnd, true);
    check(editorLiveOn(app) && !app.editorDirty, "Live with nothing unsaved");
    paintEditor(app);
    replaceOutside(note, "Last line:", "Last line, from the agent:");
    handleFileWatchTimer(app, app.hwnd);
    paintEditor(app);
    check(app.editorText.find(L"Last line, from the agent:") != std::wstring::npos && !app.editorDirty &&
              liveText(app).text == app.editorText,
          "#311: Live with nothing unsaved takes the new text");

    // Behind another tab: a clean editor takes the change once its tab is
    // back; unsaved edits bring the tab forward with the question
    tabOpenPath(app, app.hwnd, toUtf8(other.wstring()));
    check(app.activeTab == 1, "a second tab opens");
    replaceOutside(note, "## Steps", "## The steps");
    handleFileWatchTimer(app, app.hwnd);
    check(app.activeTab == 1 && !app.confirmExitPending, "#311: a clean editor behind another tab asks nothing");
    tabActivate(app, app.hwnd, 0);
    handleFileWatchTimer(app, app.hwnd);
    check(app.editMode && app.editorText.find(L"## The steps") != std::wstring::npos && !app.editorDirty,
          "#311: and takes the change when its tab comes back");
    typeKeys(app, L"x");
    check(app.editorDirty, "a word typed in Live");
    tabActivate(app, app.hwnd, 1);
    replaceOutside(note, "## The steps", "## Steps again");
    handleFileWatchTimer(app, app.hwnd);
    check(app.activeTab == 0 && app.confirmExitPending && app.confirmExitExternal,
          "#311: unsaved edits behind another tab: Tinta goes to the tab and asks");
    app.confirmExitOpenedAt -= std::chrono::seconds(1);
    keystroke(app, VK_ESCAPE);
    check(!app.confirmExitPending && app.editorDirty && app.editorText.find(L"## The steps") != std::wstring::npos,
          "#311: Esc keeps mine");

    // A byte order mark and CRLF lines from the other program
    confirmExitAction(app, app.hwnd, 3);
    editorReloadFromDisk(app, app.hwnd);
    std::string crlf;
    for (char c : readBytes(note)) {
        if (c == '\r') continue;
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    writeOutside(note, "\xEF\xBB\xBF" + crlf);
    handleFileWatchTimer(app, app.hwnd);
    check(app.editorBom && app.editorText.find(L'\r') == std::wstring::npos && !app.editorDirty,
          "#311: a file rewritten with a byte order mark and CRLF lines reloads as any file opens");

    // The unsaved-changes question still asks about leaving
    typeKeys(app, L"y");
    exitEditMode(app);
    check(app.confirmExitPending && !app.confirmExitExternal &&
              std::wstring(tr(app, "confirm.save")) != std::wstring(tr(app, "changed.reload")),
          "Esc with unsaved changes still asks Save and exit or Discard");
    confirmExitAction(app, app.hwnd, 2);
    check(!app.editMode, "Discard leaves the editor");

    app.editorDirty = false;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "File changes: " << failures << " failures\n";
    return failures ? 1 : 0;
}
