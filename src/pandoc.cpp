// Pandoc bridge: hands the document to a locally installed pandoc for
// the formats our native exporters don't cover. Rich targets (docx, odt,
// epub, pptx, rtf) are fed Tinta's own HTML export, so diagrams and math
// arrive as SVG instead of decaying into code blocks; LaTeX is fed the
// raw markdown since that conversion is source-to-source.

#include "pandoc.h"

#include "editor.h"
#include "export.h"
#include "i18n.h"
#include "settings.h"

#include <commdlg.h>
#include <shlobj.h>

#include <fstream>
#include <string>

namespace {

struct PandocFormat {
    const wchar_t* label;
    const wchar_t* ext;      // with the dot
    const wchar_t* filter;   // save-dialog filter description
    bool htmlBridge;         // feed our HTML export instead of raw md
};

const PandocFormat kFormats[PANDOC_FORMAT_COUNT] = {
    {L"EPUB", L".epub", L"EPUB e-book (*.epub)", true},
    {L"OpenDocument", L".odt", L"OpenDocument text (*.odt)", true},
    {L"PowerPoint", L".pptx", L"PowerPoint deck (*.pptx)", true},
    {L"Rich Text", L".rtf", L"Rich Text Format (*.rtf)", true},
    {L"LaTeX", L".tex", L"LaTeX source (*.tex)", false},
};

std::wstring toWideStr(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                  nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

std::string toUtf8Str(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                  nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len,
                        nullptr, nullptr);
    return out;
}

bool fileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// The document's display stem, for default filenames and epub metadata
std::wstring docStem(const App& app) {
    std::wstring name = toWideStr(app.currentFile);
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
    if (name.empty()) name = L"Untitled";
    return name;
}

struct PandocJob {
    HWND hwnd = nullptr;
    std::wstring cmdLine;   // full command line, exe quoted first
    std::wstring tempFile;  // deleted when the run finishes
};

DWORD WINAPI pandocWorker(LPVOID param) {
    PandocJob* job = static_cast<PandocJob*>(param);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = job->cmdLine;  // CreateProcess needs it writable
    bool ok = false;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        // Big documents convert in seconds; two minutes is a hang
        if (WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0) {
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            ok = (code == 0);
        } else {
            TerminateProcess(pi.hProcess, 1);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (!job->tempFile.empty()) DeleteFileW(job->tempFile.c_str());
    PostMessageW(job->hwnd, WM_APP_PANDOC_DONE, ok ? 1 : 0, 0);
    delete job;
    return 0;
}

}  // namespace

const wchar_t* pandocFormatLabel(int fmt) {
    return fmt >= 0 && fmt < PANDOC_FORMAT_COUNT ? kFormats[fmt].label : L"";
}

const wchar_t* pandocFormatExt(int fmt) {
    return fmt >= 0 && fmt < PANDOC_FORMAT_COUNT ? kFormats[fmt].ext : L"";
}

void pandocResolve(App& app) {
    if (app.pandocChecked) return;
    app.pandocChecked = true;
    app.pandocExe.clear();

    // 1. The user's explicit choice wins
    if (!app.pandocUserPath.empty() && fileExists(app.pandocUserPath)) {
        app.pandocExe = app.pandocUserPath;
        return;
    }

    // 2. PATH
    wchar_t found[MAX_PATH]{};
    if (SearchPathW(nullptr, L"pandoc.exe", nullptr, MAX_PATH, found,
                    nullptr) > 0) {
        app.pandocExe = found;
        return;
    }

    // 3. The installer's default locations
    auto probe = [&](const wchar_t* env, const wchar_t* tail) {
        if (!app.pandocExe.empty()) return;
        wchar_t base[MAX_PATH]{};
        if (GetEnvironmentVariableW(env, base, MAX_PATH) > 0) {
            std::wstring p = std::wstring(base) + tail;
            if (fileExists(p)) app.pandocExe = p;
        }
    };
    probe(L"LOCALAPPDATA", L"\\Pandoc\\pandoc.exe");
    probe(L"ProgramFiles", L"\\Pandoc\\pandoc.exe");
    probe(L"ProgramFiles(x86)", L"\\Pandoc\\pandoc.exe");
}

bool pandocAvailable(App& app) {
    pandocResolve(app);
    return !app.pandocExe.empty();
}

void pandocSetUserPath(App& app, const std::wstring& path) {
    app.pandocUserPath = path;
    app.pandocChecked = false;
    pandocResolve(app);
    Settings settings = loadSettings();
    settings.pandocPath = toUtf8Str(path);
    saveSettings(settings);
}

void pandocExportFlow(App& app, HWND hwnd, int fmt) {
    if (fmt < 0 || fmt >= PANDOC_FORMAT_COUNT) return;
    if (!pandocAvailable(app) || app.pandocRunning) return;
    const PandocFormat& f = kFormats[fmt];

    // Save dialog for the target file
    std::wstring stem = docStem(app);
    wchar_t out[MAX_PATH]{};
    wcsncpy_s(out, (stem + f.ext).c_str(), _TRUNCATE);
    std::wstring filter = std::wstring(f.filter) + L'\0' + L"*" + f.ext +
                          L'\0' + L'\0';
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = out;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = f.ext + 1;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    // Stage the input on the UI thread: our HTML export (diagrams and
    // math as SVG) for rich targets, the raw buffer for LaTeX
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t tempFile[MAX_PATH]{};
    swprintf_s(tempFile, _countof(tempFile), L"%stinta-pandoc-%lu%s",
               tempDir, GetCurrentProcessId(),
               f.htmlBridge ? L".html" : L".md");
    if (f.htmlBridge) {
        // The parse tree can lag the buffer while the render is hidden
        if (app.editMode) editorReparse(app);
        if (!exportHtmlFile(app, tempFile)) return;
    } else {
        std::string md = app.editMode ? toUtf8Str(app.editorText)
                                      : std::string();
        if (md.empty() && !app.currentFile.empty()) {
            std::ifstream in(toWideStr(app.currentFile),
                             std::ios::binary);
            md.assign(std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>());
        }
        std::ofstream outFile(tempFile, std::ios::binary);
        if (!outFile) return;
        outFile.write(md.data(), (std::streamsize)md.size());
    }

    // Assemble the command line; the writer comes from the -o extension
    std::wstring cmd = L"\"" + app.pandocExe + L"\" -f " +
                       (f.htmlBridge ? L"html" : L"gfm") + L" -s ";
    std::wstring title = stem;
    for (auto& ch : title) {
        if (ch == L'"') ch = L'\'';
    }
    cmd += L"--metadata title=\"" + title + L"\" ";
    if (!f.htmlBridge && !app.currentFile.empty()) {
        // Relative image paths in the raw markdown resolve against the
        // document's own folder
        std::wstring dir = toWideStr(app.currentFile);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            cmd += L"--resource-path=\"" + dir.substr(0, slash) + L"\" ";
        }
    }
    cmd += L"-o \"" + std::wstring(out) + L"\" \"" + tempFile + L"\"";

    PandocJob* job = new PandocJob{hwnd, cmd, tempFile};
    app.pandocRunning = true;
    HANDLE thread = CreateThread(nullptr, 0, pandocWorker, job, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        app.pandocRunning = false;
        DeleteFileW(tempFile);
        delete job;
    }
}
