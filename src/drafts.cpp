#include "drafts.h"
#include "i18n.h"
#include "settings.h"
#include "signals.h"
#include "tabs.h"
#include "editor.h"
#include "utils.h"

#include <shlobj.h>
#include <fstream>
#include <sstream>

// The config home's drafts folder (portable beside the exe, else
// %APPDATA%\Tinta), created on first use
static std::wstring draftsDirPath() {
    std::wstring path = tintaConfigDir();
    if (path.empty()) return L"";
    path += L"\\drafts";
    CreateDirectoryW(path.c_str(), nullptr);
    return path;
}

// Draft identity: process + window + tab, so several windows (and several
// Tinta processes) never overwrite each other's autosaves
static std::wstring draftFileFor(const App& app, int tabId) {
    std::wstring dir = draftsDirPath();
    if (dir.empty()) return L"";
    wchar_t name[96];
    swprintf_s(name, _countof(name), L"\\draft-%lu-%llu-%d.md",
               GetCurrentProcessId(),
               (unsigned long long)(uintptr_t)app.hwnd, tabId);
    return dir + name;
}

// A draft is the buffer prefixed with one comment line naming the file it
// belongs to ('>' cannot appear in a Windows path, so the terminator is
// unambiguous); an untitled quick note stores an empty path
static void writeDraft(const std::wstring& draftPath,
                       const std::string& origPath,
                       const std::wstring& text) {
    if (draftPath.empty()) return;
    std::ofstream out(draftPath, std::ios::binary);
    if (!out) return;
    std::string header = "<!-- tinta-draft:" + origPath + " -->\n";
    std::string body = toUtf8(text);
    out.write(header.data(), header.size());
    out.write(body.data(), body.size());
}

void draftsSweep(App& app) {
    tabsInit(app);
    for (int i = 0; i < (int)app.tabs.size(); i++) {
        const App::DocTab& tab = app.tabs[i];
        bool active = (i == app.activeTab);
        bool dirty = active ? (app.editMode && app.editorDirty)
                            : (tab.editMode && tab.editorDirty);
        std::wstring draftPath = draftFileFor(app, tab.id);
        if (draftPath.empty()) continue;
        if (dirty) {
            writeDraft(draftPath, active ? app.currentFile : tab.path,
                       active ? app.editorText : tab.editorText);
        } else {
            DeleteFileW(draftPath.c_str());
        }
    }
}

void draftsDeleteForTab(const App& app, int tabId) {
    std::wstring draftPath = draftFileFor(app, tabId);
    if (!draftPath.empty()) DeleteFileW(draftPath.c_str());
}

void draftsDeleteAll(const App& app) {
    for (const App::DocTab& tab : app.tabs) {
        draftsDeleteForTab(app, tab.id);
    }
}

// True when the pid is a running Tinta process (its drafts are live
// autosaves, not crash leftovers). Reused pids of other programs fail
// the image-name check and stay recoverable.
static bool isLiveTintaProcess(DWORD pid) {
    HANDLE proc =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return false;
    wchar_t image[MAX_PATH];
    DWORD size = _countof(image);
    bool live = false;
    if (QueryFullProcessImageNameW(proc, 0, image, &size)) {
        std::wstring path(image, size);
        size_t slash = path.find_last_of(L'\\');
        std::wstring base =
            slash == std::wstring::npos ? path : path.substr(slash + 1);
        for (wchar_t& c : base) c = (wchar_t)towlower(c);
        live = (base == L"tinta.exe");
    }
    CloseHandle(proc);
    return live;
}

void draftsScanForRecovery(App& app) {
    app.recoveredDrafts.clear();
    std::wstring dir = draftsDirPath();
    if (dir.empty()) return;

    WIN32_FIND_DATAW find;
    HANDLE h = FindFirstFileW((dir + L"\\draft-*.md").c_str(), &find);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = find.cFileName;
        DWORD pid = (DWORD)_wtoi(name.c_str() + 6);  // after "draft-"
        if (pid == GetCurrentProcessId()) continue;
        if (pid != 0 && isLiveTintaProcess(pid)) continue;
        app.recoveredDrafts.push_back(dir + L"\\" + name);
    } while (FindNextFileW(h, &find));
    FindClose(h);

    if (!app.recoveredDrafts.empty()) {
        // Offer the leftovers as a signal chip: click or Restore brings
        // them back as dirty tabs, the cross discards them for good
        wchar_t label[96];
        swprintf_s(label, _countof(label), tr(app, "draft.recovered"),
                   (int)app.recoveredDrafts.size());
        signalPush(app, SIG_WARN, SIGI_FILE, label, L"", L"",
                   SIGA_RESTORE_DRAFTS, SIGC_DISCARD_DRAFTS, false);
    }
}

void draftsRecoverAll(App& app, HWND hwnd) {
    for (const std::wstring& path : app.recoveredDrafts) {
        std::ifstream file(path, std::ios::binary);
        if (!file) continue;
        std::stringstream buf;
        buf << file.rdbuf();
        std::string content = buf.str();
        file.close();

        std::string origPath;
        const std::string prefix = "<!-- tinta-draft:";
        if (content.rfind(prefix, 0) == 0) {
            size_t end = content.find(" -->\n", prefix.size());
            if (end != std::string::npos) {
                origPath = content.substr(prefix.size(),
                                          end - prefix.size());
                content = content.substr(end + 5);
            }
        }
        enterRecoveredDraft(app, hwnd, content, origPath);
        DeleteFileW(path.c_str());
    }
    app.recoveredDrafts.clear();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void draftsDiscardAll(App& app) {
    for (const std::wstring& path : app.recoveredDrafts) {
        DeleteFileW(path.c_str());
    }
    app.recoveredDrafts.clear();
}
