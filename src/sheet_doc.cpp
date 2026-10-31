// Sheets: the sheet behind a tab. Loading and saving, the notes behind the
// cards, undo, the folder the sheet lives on, and the commands the tool
// bar, the menu and the keys share (sheet_ui.h).

// Sheets favour small code over fast: they are drawn by Direct2D and
// laid out by the document layout, which keep their own optimisation
#pragma optimize("s", on)

#include "sheet_ui.h"
#include "document.h"
#include "editor.h"
#include "file_utils.h"
#include "i18n.h"
#include "network_path.h"
#include "settings.h"
#include "signals.h"
#include "tabs.h"
#include "utils.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace sheet;
using namespace sheetui;

// --- Sheet files ---------------------------------------------------------

namespace {

bool endsWithNoCase(const std::wstring& text, const wchar_t* suffix) {
    size_t n = wcslen(suffix);
    return text.size() >= n && _wcsicmp(text.c_str() + text.size() - n, suffix) == 0;
}

std::string lowerKey(const std::string& path) {
    std::string key = path;
    for (auto& c : key) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == '/') c = '\\';
    }
    return key;
}

std::wstring parentOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring stemOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    return dot == std::wstring::npos || dot == 0 ? name : name.substr(0, dot);
}

bool fileExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool folderExists(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

unsigned long long fileStamp(const std::wstring& path, bool* exists = nullptr) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (exists) *exists = false;
        return 0;
    }
    if (exists) *exists = true;
    unsigned long long time = ((unsigned long long)data.ftLastWriteTime.dwHighDateTime << 32) |
                              data.ftLastWriteTime.dwLowDateTime;
    unsigned long long size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    return time ^ (size * 0x9E3779B97F4A7C15ull);
}

bool isNotePath(const std::wstring& path) {
    return endsWithNoCase(path, L".md") || endsWithNoCase(path, L".markdown");
}

bool isImagePath(const std::wstring& path) {
    for (const wchar_t* ext : {L".png", L".jpg", L".jpeg", L".gif", L".bmp", L".webp", L".tif",
                               L".tiff", L".ico", L".jxr"}) {
        if (endsWithNoCase(path, ext)) return true;
    }
    return false;
}

bool isCompiledName(const std::wstring& path) { return endsWithNoCase(path, L".compiled.md"); }

// Where the Obsidian vault holding a .canvas starts: the nearest folder up
// with a .obsidian folder in it, else the canvas's own folder
std::wstring vaultRoot(const std::wstring& dir) {
    std::wstring at = dir;
    for (int depth = 0; depth < 32 && !at.empty(); ++depth) {
        if (folderExists(at + L"\\.obsidian")) return at;
        std::wstring up = parentOf(at);
        if (up == at || up.empty() || up.size() < 3) break;
        at = up;
    }
    return dir;
}

std::wstring sheetsIniPath() {
    std::wstring dir = tintaConfigDir();
    return dir.empty() ? std::wstring() : dir + L"\\sheets.ini";
}

std::wstring viewKey(const std::string& path) {
    // A short stable key per sheet path (INI keys stay readable)
    unsigned long long hash = 1469598103934665603ull;
    for (char c : lowerKey(path)) {
        hash ^= (unsigned char)c;
        hash *= 1099511628211ull;
    }
    wchar_t buf[24];
    swprintf_s(buf, L"%016llx", hash);
    return buf;
}

void loadView(SheetDoc& doc) {
    std::wstring ini = sheetsIniPath();
    if (ini.empty()) return;
    wchar_t buf[128] = {};
    GetPrivateProfileStringW(L"Views", viewKey(doc.path).c_str(), L"", buf, 128, ini.c_str());
    float x = 0, y = 0, z = 0;
    if (swscanf_s(buf, L"%f %f %f", &x, &y, &z) == 3 && std::isfinite(x) && std::isfinite(y) &&
        z >= 0.05f && z <= 8.0f) {
        doc.camX = x;
        doc.camY = y;
        doc.zoom = z;
        doc.placed = true;
    }
}

}  // namespace

void sheetSaveView(SheetDoc& doc) {
    std::wstring ini = sheetsIniPath();
    if (ini.empty() || !doc.placed) return;
    // Only a view that moved since it was last written
    if (doc.camX == doc.savedCamX && doc.camY == doc.savedCamY && doc.zoom == doc.savedZoom) return;
    doc.savedCamX = doc.camX;
    doc.savedCamY = doc.camY;
    doc.savedZoom = doc.zoom;
    wchar_t buf[128];
    swprintf_s(buf, L"%.1f %.1f %.4f", doc.camX, doc.camY, doc.zoom);
    WritePrivateProfileStringW(L"Views", viewKey(doc.path).c_str(), buf, ini.c_str());
}

bool isSheetPath(const std::wstring& path) {
    return endsWithNoCase(path, L".smd") || endsWithNoCase(path, L".canvas");
}

bool isSheetPath(const std::string& path) { return isSheetPath(toWide(path)); }

bool sheetActive(const App& app) {
    return !app.currentFile.empty() && !app.editMode && !app.showPrintPreview &&
           isSheetPath(app.currentFile);
}

namespace {

void loadModel(App& app, SheetDoc& doc) {
    doc.loadedSource = app.sourceText;
    doc.loadError.clear();
    doc.readOnly = false;
    Sheet model;
    SmdInfo info;
    std::string error;
    bool ok = doc.canvasFile ? parseCanvas(app.sourceText, model, &error)
                             : parseSmd(app.sourceText, model, info, &error);
    if (!ok) {
        // Shown as far as it read, never written over
        doc.readOnly = true;
        doc.loadError = error;
    }
    doc.model = std::move(model);
    doc.info = info;
    if (doc.info.title.empty()) doc.info.title = toUtf8(stemOf(doc.file));
    // New notes: the front matter's folder, relative to the sheet only
    // (an absolute folder or a drive is not the sheet's to choose)
    doc.cardDir = doc.dir;
    if (!doc.canvasFile && !doc.info.folder.empty() && doc.info.folder != ".") {
        std::wstring folder = toWide(doc.info.folder);
        bool absolute = folder.size() >= 2 && (folder[1] == L':' || folder[0] == L'\\' || folder[0] == L'/');
        if (!absolute) {
            std::filesystem::path p = std::filesystem::path(doc.dir) / folder;
            doc.cardDir = p.lexically_normal().wstring();
            while (!doc.cardDir.empty() && (doc.cardDir.back() == L'\\' || doc.cardDir.back() == L'/'))
                doc.cardDir.pop_back();
        }
    }
    if (!qmd::mayOpenFromDocument(doc.cardDir, doc.dir)) doc.cardDir = doc.dir;
    // Drop selection and runtime state of nodes that are gone
    std::set<std::string> ids;
    for (const Node& n : doc.model.nodes) ids.insert(n.id);
    doc.selected.erase(std::remove_if(doc.selected.begin(), doc.selected.end(),
                                      [&](const std::string& id) { return !ids.count(id); }),
                       doc.selected.end());
    for (auto it = doc.cards.begin(); it != doc.cards.end();) {
        if (!ids.count(it->first)) it = doc.cards.erase(it);
        else ++it;
    }
    if (sheet::edgeIndex(doc.model, doc.selectedEdge) < 0) doc.selectedEdge.clear();
}

}  // namespace

SheetDoc* activeSheet(App& app) {
    if (!sheetActive(app)) return nullptr;
    std::string key = lowerKey(app.currentFile);
    auto it = app.sheetDocs.find(key);
    SheetDoc* doc = nullptr;
    if (it == app.sheetDocs.end()) {
        auto made = std::make_shared<SheetDoc>();
        made->path = app.currentFile;
        made->file = toWide(app.currentFile);
        made->dir = parentOf(made->file);
        made->canvasFile = endsWithNoCase(made->file, L".canvas");
        made->base = made->canvasFile ? vaultRoot(made->dir) : made->dir;
        if (!qmd::mayOpenFromDocument(made->base, made->dir)) made->base = made->dir;
        loadModel(app, *made);
        loadView(*made);
        doc = made.get();
        app.sheetDocs[key] = std::move(made);
        pollFolder(app, *doc, true);
    } else {
        doc = it->second.get();
        if (doc->loadedSource != app.sourceText && !doc->dirty) {
            // Changed on disk by someone else: read it again, keep the view
            loadModel(app, *doc);
            for (auto& entry : doc->cards) entry.second.stamp = 0;
        }
    }
    return doc;
}

// --- Paths and files -----------------------------------------------------

namespace sheetui {

unsigned long long nowMs() {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::wstring absolutePath(const SheetDoc& doc, const std::string& relative) {
    std::wstring wide = toWide(relative);
    for (auto& c : wide) {
        if (c == L'/') c = L'\\';
    }
    bool absolute = wide.size() >= 2 && (wide[1] == L':' || (wide[0] == L'\\' && wide[1] == L'\\'));
    std::filesystem::path p = absolute ? std::filesystem::path(wide) : std::filesystem::path(doc.base) / wide;
    return p.lexically_normal().wstring();
}

std::string relativePath(const SheetDoc& doc, const std::wstring& absolute) {
    std::filesystem::path rel = std::filesystem::path(absolute).lexically_normal().lexically_relative(
        std::filesystem::path(doc.base).lexically_normal());
    std::wstring text = rel.empty() ? absolute : rel.wstring();
    // Another drive cannot be reached from the base: keep the full path
    if (rel.empty() || (text.size() >= 2 && text[1] == L':')) text = absolute;
    for (auto& c : text) {
        if (c == L'\\') c = L'/';
    }
    return toUtf8(text);
}

bool readFileUtf8(const std::wstring& path, std::string& out, size_t cap) {
    out.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    size_t want = (size_t)std::min<long long>(size.QuadPart, cap ? (long long)cap : (long long)64 * 1024 * 1024);
    out.resize(want);
    DWORD read = 0;
    bool ok = want == 0 || (ReadFile(file, out.data(), (DWORD)want, &read, nullptr) && read > 0);
    out.resize(read);
    CloseHandle(file);
    return ok;
}

bool writeFileUtf8(const std::wstring& path, const std::string& text) {
    // Written beside the file and swapped in, so a crash never leaves half a note
    std::wstring temp = path + L".tinta-tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = text.empty() || (WriteFile(file, text.data(), (DWORD)text.size(), &written, nullptr) &&
                               written == text.size());
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}

// --- Cards ---------------------------------------------------------------

SheetCard& card(App& app, SheetDoc& doc, const Node& node) {
    auto it = doc.cards.find(node.id);
    if (it == doc.cards.end()) {
        it = doc.cards.emplace(node.id, SheetCard{}).first;
        refreshCard(app, doc, node, true);
    }
    return it->second;
}

void refreshCard(App& app, SheetDoc& doc, const Node& node, bool force) {
    SheetCard& c = doc.cards[node.id];
    (void)app;
    if (node.type == NodeType::Text) {
        unsigned long long key = std::hash<std::string>{}(node.text);
        if (!force && c.stamp == key && c.kind == SheetCard::Kind::Note) return;
        c.kind = SheetCard::Kind::Note;
        c.path.clear();
        c.stamp = key;
        c.source = node.text;
    } else if (node.type == NodeType::File) {
        std::wstring path = absolutePath(doc, node.file);
        if (!qmd::mayOpenFromDocument(path, doc.dir)) {
            c.kind = SheetCard::Kind::Blocked;
            c.path = path;
            c.title = toUtf8(stemOf(path));
            c.source.clear();
            return;
        }
        bool exists = false;
        unsigned long long stamp = fileStamp(path, &exists);
        if (!force && c.path == path && c.stamp == stamp && exists == (c.kind != SheetCard::Kind::Missing)) return;
        c.path = path;
        c.stamp = stamp;
        if (!exists) {
            c.kind = SheetCard::Kind::Missing;
            c.source.clear();
        } else if (isNotePath(path)) {
            c.kind = SheetCard::Kind::Note;
            readFileUtf8(path, c.source, 256 * 1024);
            if (startsWithUtf8Bom(c.source)) c.source.erase(0, 3);
        } else if (isImagePath(path)) {
            c.kind = SheetCard::Kind::Image;
            c.source.clear();
        } else {
            c.kind = SheetCard::Kind::Other;
            c.source.clear();
        }
        c.bitmap.Reset();  // the drawing belongs to the old note
        c.bmKey = 0;
    } else {
        c.kind = SheetCard::Kind::Other;
        c.source.clear();
    }
    // Figures the card shows
    std::wstring stem = node.type == NodeType::File ? stemOf(c.path) : std::wstring();
    if (c.kind == SheetCard::Kind::Note) {
        c.title = noteTitle(c.source, toUtf8(stem));
        noteTasks(c.source, c.tasksDone, c.tasksTotal);
        c.words = noteWords(c.source);
    } else {
        c.title = node.type == NodeType::Link ? node.url : node.type == NodeType::Group ? node.label : toUtf8(stem);
        c.tasksDone = c.tasksTotal = c.words = 0;
    }
    // What its text links to: a relative .md link resolves to a path, a
    // [[wiki]] link names a note (ghostLinks matches them to cards)
    c.linkKeys.clear();
    if (c.kind == SheetCard::Kind::Note) {
        std::wstring noteDir = node.type == NodeType::File ? parentOf(c.path) : doc.dir;
        for (const std::string& target : noteLinks(c.source)) {
            std::wstring wideTarget = toWide(target);
            for (auto& ch : wideTarget) {
                if (ch == L'/') ch = L'\\';
            }
            bool isPath = endsWithNoCase(wideTarget, L".md") || endsWithNoCase(wideTarget, L".markdown");
            std::wstring key = isPath ? (std::filesystem::path(noteDir) / wideTarget).lexically_normal().wstring()
                                      : L"*" + stemOf(wideTarget);
            for (auto& ch : key) ch = (wchar_t)towlower(ch);
            c.linkKeys.push_back(key);
        }
    }
}

std::string cardTitle(App& app, SheetDoc& doc, const Node& node) {
    if (node.type == NodeType::Group) return node.label;
    if (node.type == NodeType::Link) return node.url;
    return card(app, doc, node).title;
}

D2D1_COLOR_F presetColor(const App& app, const std::string& color, bool* has) {
    if (has) *has = true;
    // The six JSON Canvas presets: red, orange, yellow, green, cyan, purple,
    // a little deeper on light themes so they hold against paper
    static const unsigned dark[6] = {0xF2555A, 0xF7873B, 0xF2C94C, 0x4CC38A, 0x3BC4DB, 0xA77BEB};
    static const unsigned light[6] = {0xD93A3F, 0xE0661C, 0xC9A200, 0x2C9A63, 0x1597B0, 0x8A55D6};
    unsigned rgb = 0;
    if (color.size() == 1 && color[0] >= '1' && color[0] <= '6') {
        rgb = (app.theme.isDark ? dark : light)[color[0] - '1'];
    } else if (color.size() == 7 && color[0] == '#') {
        rgb = (unsigned)std::strtoul(color.c_str() + 1, nullptr, 16);
    } else if (color.size() == 4 && color[0] == '#') {
        unsigned short3 = (unsigned)std::strtoul(color.c_str() + 1, nullptr, 16);
        unsigned r = (short3 >> 8) & 0xF, g = (short3 >> 4) & 0xF, b = short3 & 0xF;
        rgb = (r * 17) << 16 | (g * 17) << 8 | (b * 17);
    } else {
        if (has) *has = false;
        return app.theme.text;
    }
    return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

bool isSelected(const SheetDoc& doc, const std::string& id) {
    return std::find(doc.selected.begin(), doc.selected.end(), id) != doc.selected.end();
}

std::vector<int> selectedIndexes(const SheetDoc& doc) {
    std::vector<int> out;
    for (const std::string& id : doc.selected) {
        int i = nodeIndex(doc.model, id);
        if (i >= 0) out.push_back(i);
    }
    return out;
}

// --- Saving and undo ------------------------------------------------------

void ensureTimer(App& app) {
    if (app.hwnd) SetTimer(app.hwnd, SHEET_TIMER_ID, 16, nullptr);
}

void markChanged(App& app, SheetDoc& doc) {
    if (doc.readOnly) return;
    doc.dirty = true;
    ensureTimer(app);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

void saveNow(App& app, SheetDoc& doc) {
    if (!doc.dirty || doc.readOnly) return;
    std::string text;
    if (doc.canvasFile) {
        text = writeCanvas(doc.model);
    } else {
        text = writeSmd(doc.model, doc.info, doc.loadedSource,
                        [&](const Node& n) { return n.type == NodeType::File ? cardTitle(app, doc, n) : std::string(); });
    }
    doc.dirty = false;
    if (text == doc.loadedSource) return;
    if (!writeFileUtf8(doc.file, text)) {
        signalPush(app, SIG_ERROR, SIGI_WARNING, tr(app, "sheet.save_failed"));
        doc.dirty = true;
        return;
    }
    doc.loadedSource = text;
    // The viewer's copy of the source follows, so the file watcher sees
    // our own write as nothing new
    if (_stricmp(app.currentFile.c_str(), doc.path.c_str()) == 0) {
        app.sourceText = text;
        updateFileWriteTime(app);
        if (!app.tabs.empty() && app.activeTab >= 0 && app.activeTab < (int)app.tabs.size() &&
            app.keepDeletedTabs) {
            app.tabs[app.activeTab].viewerSource = text;
        }
    }
}

void beginStep(SheetDoc& doc) {
    SheetDoc::Step step;
    step.model = doc.model;
    step.selected = doc.selected;
    doc.undo.push_back(std::move(step));
    if (doc.undo.size() > 200) doc.undo.erase(doc.undo.begin());
    doc.redo.clear();
    doc.pendingFiles.clear();
}

void commitStep(App& app, SheetDoc& doc) {
    if (!doc.undo.empty()) {
        auto& files = doc.undo.back().files;
        files.insert(files.end(), doc.pendingFiles.begin(), doc.pendingFiles.end());
    }
    doc.pendingFiles.clear();
    markChanged(app, doc);
}

namespace {

// Puts a step's file changes back (or forward again), only where the file
// still holds what the step left in it
void replayFiles(App& app, SheetDoc& doc, const std::vector<SheetDoc::FileChange>& files, bool backward) {
    std::vector<SheetDoc::FileChange> list = files;
    if (backward) std::reverse(list.begin(), list.end());
    for (const auto& change : list) {
        std::string current;
        bool exists = fileExists(change.path);
        switch (change.kind) {
            case SheetDoc::FileChange::Created:
                if (backward) {
                    if (exists && readFileUtf8(change.path, current) && current == change.after)
                        DeleteFileW(change.path.c_str());
                } else if (!exists) {
                    writeFileUtf8(change.path, change.after);
                }
                break;
            case SheetDoc::FileChange::Edited:
                if (exists && readFileUtf8(change.path, current) &&
                    current == (backward ? change.after : change.before)) {
                    writeFileUtf8(change.path, backward ? change.before : change.after);
                }
                break;
            case SheetDoc::FileChange::Deleted:
                if (backward) {
                    if (!exists) writeFileUtf8(change.path, change.before);
                } else if (exists && readFileUtf8(change.path, current) && current == change.before) {
                    DeleteFileW(change.path.c_str());
                }
                break;
            case SheetDoc::FileChange::Renamed: {
                const std::wstring& from = backward ? change.path : change.otherPath;
                const std::wstring& to = backward ? change.otherPath : change.path;
                if (fileExists(from) && !fileExists(to)) MoveFileW(from.c_str(), to.c_str());
                break;
            }
        }
    }
    for (auto& entry : doc.cards) entry.second.stamp = 0;  // read the notes again
    (void)app;
}

}  // namespace

void undoStep(App& app, SheetDoc& doc, bool redo) {
    auto& from = redo ? doc.redo : doc.undo;
    auto& to = redo ? doc.undo : doc.redo;
    if (from.empty() || doc.readOnly) return;
    if (doc.editing != SheetDoc::Edit::None) commitEdit(app, doc);
    if (from.empty()) return;
    SheetDoc::Step step = std::move(from.back());
    from.pop_back();
    SheetDoc::Step back;
    back.model = doc.model;
    back.selected = doc.selected;
    back.files = step.files;
    // Glide the cards that move
    doc.glide.from.clear();
    for (const Node& n : doc.model.nodes) doc.glide.from[n.id] = boxOf(n);
    doc.glide.active = true;
    doc.glide.startMs = nowMs();
    replayFiles(app, doc, step.files, !redo);
    doc.model = std::move(step.model);
    doc.selected = step.selected;
    doc.selectedEdge.clear();
    to.push_back(std::move(back));
    for (const Node& n : doc.model.nodes) refreshCard(app, doc, n, true);
    markChanged(app, doc);
    ensureTimer(app);
}

// --- The camera ----------------------------------------------------------

void flyTo(SheetDoc& doc, float x, float y, float zoom, float ms) {
    doc.flight.active = ms > 0;
    doc.flight.fromX = doc.camX;
    doc.flight.fromY = doc.camY;
    doc.flight.fromZoom = doc.zoom;
    doc.flight.toX = x;
    doc.flight.toY = y;
    doc.flight.toZoom = std::clamp(zoom, 0.05f, 4.0f);
    doc.flight.startMs = nowMs();
    doc.flight.durationMs = ms;
    if (ms <= 0) {
        doc.camX = x;
        doc.camY = y;
        doc.zoom = doc.flight.toZoom;
    }
    doc.placed = true;
}

void fitBox(App& app, SheetDoc& doc, const Box& box, bool animate, float maxZoom) {
    D2D1_RECT_F view = viewRect(app);
    float vw = std::max(1.0f, view.right - view.left), vh = std::max(1.0f, view.bottom - view.top);
    float w = std::max(1.0f, box.right - box.left), h = std::max(1.0f, box.bottom - box.top);
    // Room for the tool bar below and a margin all round
    float margin = dpi(app, 56.0f);
    float zoom = std::min((vw - margin * 2) / (w * app.contentScale), (vh - margin * 2 - dpi(app, 56.0f)) / (h * app.contentScale));
    zoom = std::clamp(zoom, 0.05f, maxZoom);
    float cx = (box.left + box.right) * 0.5f;
    float cy = (box.top + box.bottom) * 0.5f + dpi(app, 20.0f) / (zoom * app.contentScale);
    flyTo(doc, cx, cy, zoom, animate ? 420.0f : 0.0f);
    if (animate) ensureTimer(app);
}

void hint(App& app, const wchar_t* text) { signalHint(app, SIGI_INFO, text); }

// --- Notes ---------------------------------------------------------------

std::string writeNoteFile(App& app, SheetDoc& doc, const std::wstring& stem, const std::string& text,
                          std::wstring* created) {
    if (!folderExists(doc.cardDir)) SHCreateDirectoryExW(nullptr, doc.cardDir.c_str(), nullptr);
    std::wstring base = safeFileStem(stem);
    std::wstring path;
    for (int n = 1; n < 10000; ++n) {
        path = doc.cardDir + L"\\" + base + (n == 1 ? std::wstring() : L" " + std::to_wstring(n)) + L".md";
        if (!fileExists(path)) break;
    }
    if (!writeFileUtf8(path, text)) {
        signalPush(app, SIG_ERROR, SIGI_WARNING, tr(app, "sheet.note_failed"));
        return {};
    }
    SheetDoc::FileChange change;
    change.kind = SheetDoc::FileChange::Created;
    change.path = path;
    change.after = text;
    doc.pendingFiles.push_back(change);
    if (created) *created = path;
    return relativePath(doc, path);
}

std::string newNoteCard(App& app, SheetDoc& doc, float x, float y, const std::string& text, float width,
                        float height) {
    std::wstring stem = text.empty() ? std::wstring(tr(app, "title.untitled")) : toWide(noteTitle(text, ""));
    if (stem.empty()) stem = tr(app, "title.untitled");
    std::wstring created;
    std::string rel = writeNoteFile(app, doc, stem, text, &created);
    if (rel.empty()) return {};
    Node node;
    node.id = newId();
    node.type = NodeType::File;
    node.file = rel;
    node.width = width;
    node.height = height;
    node.x = std::round(x / kGrid) * kGrid;
    node.y = std::round(y / kGrid) * kGrid;
    doc.model.nodes.push_back(node);
    SheetCard& c = doc.cards[node.id];
    c.createdHere = true;
    c.autoNamed = text.empty();
    refreshCard(app, doc, node, true);
    return node.id;
}

// --- Lanes ---------------------------------------------------------------

void applyLanes(App& app, SheetDoc& doc, const std::vector<std::string>& ids) {
    for (const std::string& id : ids) {
        int i = nodeIndex(doc.model, id);
        if (i < 0 || doc.model.nodes[i].type != NodeType::File) continue;
        int g = groupOf(doc.model, i);
        if (g < 0 || doc.model.nodes[g].laneKey.empty()) continue;
        const Node& lane = doc.model.nodes[g];
        SheetCard& c = card(app, doc, doc.model.nodes[i]);
        if (c.kind != SheetCard::Kind::Note) continue;
        std::string current;
        if (!readFileUtf8(c.path, current)) continue;
        std::string bom;
        if (startsWithUtf8Bom(current)) {
            bom = current.substr(0, 3);
            current.erase(0, 3);
        }
        if (frontmatterValue(current, lane.laneKey) == lane.label) continue;
        std::string updated = current;
        if (!setFrontmatterValue(updated, lane.laneKey, lane.label)) {
            signalPush(app, SIG_WARN, SIGI_WARNING, tr(app, "sheet.lane_skipped"));
            continue;
        }
        if (writeFileUtf8(c.path, bom + updated)) {
            SheetDoc::FileChange change;
            change.kind = SheetDoc::FileChange::Edited;
            change.path = c.path;
            change.before = bom + current;
            change.after = bom + updated;
            doc.pendingFiles.push_back(change);
            refreshCard(app, doc, doc.model.nodes[i], true);
        }
    }
}

// --- The folder ----------------------------------------------------------

void pollFolder(App& app, SheetDoc& doc, bool force) {
    unsigned long long now = nowMs();
    if (!force && now - doc.lastPollMs < 1500) return;
    doc.lastPollMs = now;
    // Notes changed on disk redraw; missing ones are marked
    bool changed = false;
    for (const Node& n : doc.model.nodes) {
        if (n.type != NodeType::File) continue;
        SheetCard& c = card(app, doc, n);
        unsigned long long before = c.stamp;
        SheetCard::Kind kind = c.kind;
        refreshCard(app, doc, n, false);
        if (c.stamp != before || c.kind != kind) changed = true;
    }
    // Notes in the folder that are not on the sheet wait in the tray
    std::vector<std::wstring> found;
    if (folderExists(doc.cardDir) && qmd::mayOpenFromDocument(doc.cardDir, doc.dir)) {
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW((doc.cardDir + L"\\*").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) continue;
                std::wstring path = doc.cardDir + L"\\" + data.cFileName;
                if (!isNotePath(path) || isCompiledName(path)) continue;
                found.push_back(path);
            } while (FindNextFileW(find, &data) && found.size() < 2000);
            FindClose(find);
        }
    }
    std::set<std::wstring> onSheet;
    for (const Node& n : doc.model.nodes) {
        if (n.type != NodeType::File) continue;
        std::wstring p = absolutePath(doc, n.file);
        for (auto& ch : p) ch = (wchar_t)towlower(ch);
        onSheet.insert(p);
    }
    std::vector<std::wstring> unplaced;
    for (const auto& path : found) {
        std::wstring lower = path;
        for (auto& ch : lower) ch = (wchar_t)towlower(ch);
        if (!onSheet.count(lower)) unplaced.push_back(path);
    }
    std::sort(unplaced.begin(), unplaced.end(),
              [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    // A note renamed outside Tinta: its card was left missing and a new
    // file with the same text turned up; the card follows it
    for (Node& n : doc.model.nodes) {
        if (n.type != NodeType::File) continue;
        SheetCard& c = doc.cards[n.id];
        if (c.kind != SheetCard::Kind::Missing || c.source.empty()) continue;
        for (auto it = unplaced.begin(); it != unplaced.end(); ++it) {
            std::string text;
            if (!readFileUtf8(*it, text, 256 * 1024)) continue;
            if (startsWithUtf8Bom(text)) text.erase(0, 3);
            if (text != c.source) continue;
            n.file = relativePath(doc, *it);
            unplaced.erase(it);
            refreshCard(app, doc, n, true);
            markChanged(app, doc);
            changed = true;
            break;
        }
    }
    if (unplaced != doc.unplaced) {
        doc.unplaced = std::move(unplaced);
        changed = true;
    }
    if (changed && app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
}

// --- Ghost links -----------------------------------------------------------

std::vector<std::pair<std::string, std::string>> ghostLinks(App& app, SheetDoc& doc) {
    std::vector<std::pair<std::string, std::string>> out;
    // The index of cards by path and name, rebuilt when the files change
    size_t key = doc.model.nodes.size();
    for (const Node& n : doc.model.nodes) {
        if (n.type == NodeType::File)
            key = key * 1000003u ^ std::hash<std::string>{}(n.file) ^ std::hash<std::string>{}(n.id);
    }
    if (key != doc.linkIndexKey) {
        doc.linkIndexKey = key;
        doc.cardByPath.clear();
        doc.cardByName.clear();
        for (const Node& n : doc.model.nodes) {
            if (n.type != NodeType::File) continue;
            std::wstring path = absolutePath(doc, n.file);
            for (auto& ch : path) ch = (wchar_t)towlower(ch);
            doc.cardByPath.emplace(path, n.id);
            doc.cardByName.emplace(L"*" + stemOf(path), n.id);
        }
    }
    std::set<std::pair<std::string, std::string>> joined;
    for (const Edge& e : doc.model.edges) {
        joined.insert({e.fromNode, e.toNode});
        joined.insert({e.toNode, e.fromNode});
    }
    for (const Node& n : doc.model.nodes) {
        if (n.type != NodeType::File && n.type != NodeType::Text) continue;
        SheetCard& c = card(app, doc, n);
        for (const std::wstring& linkKey : c.linkKeys) {
            const auto& index = !linkKey.empty() && linkKey[0] == L'*' ? doc.cardByName : doc.cardByPath;
            auto found = index.find(linkKey);
            if (found == index.end() || found->second == n.id) continue;
            const std::string& target = found->second;
            if (joined.count({n.id, target})) continue;
            joined.insert({n.id, target});
            joined.insert({target, n.id});
            out.emplace_back(n.id, target);
        }
    }
    return out;
}

bool ghostShown(const SheetDoc& doc, size_t count, const std::pair<std::string, std::string>& ghost) {
    if (count <= 60) return true;
    return ghost.first == doc.hoverNode || ghost.second == doc.hoverNode || isSelected(doc, ghost.first) ||
           isSelected(doc, ghost.second);
}

}  // namespace sheetui

// --- Leaving, timers, images ---------------------------------------------

void sheetLeave(App& app) {
    for (auto& entry : app.sheetDocs) {
        SheetDoc& doc = *entry.second;
        if (doc.editing != SheetDoc::Edit::None) commitEdit(app, doc);
        doc.gesture = SheetDoc::Gesture::None;
        doc.menuOpen = false;
        saveNow(app, doc);
        sheetSaveView(doc);
    }
    if (app.hwnd) KillTimer(app.hwnd, SHEET_TIMER_ID);
}

void sheetImagesArrived(App& app) {
    for (auto& entry : app.sheetDocs) {
        for (auto& c : entry.second->cards) c.second.bmKey = 0;
    }
}

void sheetTimer(App& app, HWND hwnd) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) {
        // Saves still go out when the sheet is no longer in front
        for (auto& entry : app.sheetDocs) saveNow(app, *entry.second);
        KillTimer(hwnd, SHEET_TIMER_ID);
        return;
    }
    unsigned long long now = nowMs();
    bool animating = false;
    if (doc->flight.active) {
        animating = true;
        if (now - doc->flight.startMs >= doc->flight.durationMs) {
            doc->flight.active = false;
            doc->camX = doc->flight.toX;
            doc->camY = doc->flight.toY;
            doc->zoom = doc->flight.toZoom;
            doc->zoomChangedMs = now;
        }
    }
    if (doc->glide.active && now - doc->glide.startMs > 320) doc->glide.active = false;
    if (doc->glide.active) animating = true;
    // Edits reach the note a moment after typing stops
    if (doc->editing == SheetDoc::Edit::Card && doc->edit.changed && now - doc->edit.lastTypeMs > 700) {
        commitEdit(app, *doc, true);
    }
    // The caret blinks while a text box is open
    static unsigned long long lastBlink = 0;
    if (doc->editing != SheetDoc::Edit::None && now - lastBlink > 530) {
        lastBlink = now;
        app.cursorBlinkOn = !app.cursorBlinkOn;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (doc->dirty && !animating && doc->gesture == SheetDoc::Gesture::None) saveNow(app, *doc);
    pollFolder(app, *doc, false);
    bool cardsWaiting = false;
    for (const Node& n : doc->model.nodes) {
        auto it = doc->cards.find(n.id);
        if (it != doc->cards.end() && it->second.kind == SheetCard::Kind::Note && it->second.bmKey == 0) {
            cardsWaiting = true;
            break;
        }
    }
    if (animating || cardsWaiting || now - doc->zoomChangedMs < 400) InvalidateRect(hwnd, nullptr, FALSE);
    // Fast while something moves, slow while it only watches the folder
    SetTimer(hwnd, SHEET_TIMER_ID,
             (animating || cardsWaiting) ? 16 : doc->editing != SheetDoc::Edit::None ? 120 : 500, nullptr);
    static unsigned long long lastViewSave = 0;
    if (now - lastViewSave > 3000) {
        lastViewSave = now;
        sheetSaveView(*doc);
    }
}

// --- Creating sheets -----------------------------------------------------

bool sheetCreateAt(App& app, const std::wstring& sheetPath, std::string* error) {
    std::wstring dir = parentOf(sheetPath);
    std::wstring stem = stemOf(sheetPath);
    std::wstring folder = dir + L"\\" + stem;
    bool hadFolder = folderExists(folder);
    if (!hadFolder && !CreateDirectoryW(folder.c_str(), nullptr)) {
        if (error) *error = "the folder could not be made";
        return false;
    }
    Sheet model;
    SmdInfo info;
    info.title = toUtf8(stem);
    info.folder = toUtf8(stem);
    // An existing folder's notes come onto the new sheet in a grid
    if (hadFolder) {
        std::vector<std::wstring> notes;
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW((folder + L"\\*").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) continue;
                std::wstring path = folder + L"\\" + data.cFileName;
                if (isNotePath(path) && !isCompiledName(path)) notes.push_back(path);
            } while (FindNextFileW(find, &data) && notes.size() < 500);
            FindClose(find);
        }
        std::sort(notes.begin(), notes.end(),
                  [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
        auto places = gridPlaces((int)notes.size(), 0, 0, 360, 240, 40);
        for (size_t i = 0; i < notes.size(); ++i) {
            Node node;
            node.id = newId();
            node.type = NodeType::File;
            node.file = toUtf8(stem) + "/" + toUtf8(notes[i].substr(folder.size() + 1));
            node.x = places[i].first;
            node.y = places[i].second;
            node.width = 360;
            node.height = 240;
            model.nodes.push_back(node);
        }
    }
    std::string text = writeSmd(model, info, "");
    if (!writeFileUtf8(sheetPath, text)) {
        if (error) *error = "the sheet could not be written";
        return false;
    }
    (void)app;
    return true;
}

bool sheetCreateInteractive(App& app, HWND hwnd) {
    wchar_t file[MAX_PATH] = {};
    std::wstring suggested = std::wstring(tr(app, "sheet.default_name")) + L".smd";
    wcsncpy_s(file, suggested.c_str(), _TRUNCATE);
    std::wstring initial = app.currentFile.empty() ? launchDirectory() : getDirectoryFromFile(app.currentFile);
    std::wstring filter = std::wstring(tr(app, "sheet.filter")) + L" (*.smd)";
    filter.push_back(L'\0');
    filter += L"*.smd";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initial.empty() ? nullptr : initial.c_str();
    ofn.lpstrDefExt = L"smd";
    std::wstring title = tr(app, "sheet.create_title");
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!runSaveFileDialog(ofn)) return false;
    std::wstring path = file;
    if (!endsWithNoCase(path, L".smd")) path += L".smd";
    std::string error;
    if (!sheetCreateAt(app, path, &error)) {
        signalPush(app, SIG_ERROR, SIGI_WARNING, tr(app, "sheet.create_failed"));
        return false;
    }
    // The launcher hands its tab to the sheet; a document keeps its own
    bool launcher = app.currentFile.empty() && !app.editMode;
    tabsInit(app);
    int shellId = app.tabs[app.activeTab].id;
    tabOpenPath(app, hwnd, toUtf8(path));
    if (launcher) {
        for (int i = 0; i < (int)app.tabs.size(); i++) {
            if (app.tabs[i].id == shellId && i != app.activeTab) {
                tabCloseIndex(app, hwnd, i);
                break;
            }
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}
