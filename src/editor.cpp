#include "editor.h"
#include "utils.h"
#include "file_utils.h"
#include "render.h"
#include "d2d_init.h"
#include "search.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

#define TIMER_EDITOR_REPARSE 2

// --- UTF conversion ---

std::string toUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &out[0], len, nullptr, nullptr);
    return out;
}

static std::wstring fromUtf8(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], len);
    return out;
}

// --- Line starts ---

void rebuildLineStarts(App& app) {
    app.editorLineStarts.clear();
    app.editorLineStarts.push_back(0);
    for (size_t i = 0; i < app.editorText.size(); i++) {
        if (app.editorText[i] == L'\n') {
            app.editorLineStarts.push_back(i + 1);
        }
    }
}

static size_t getLineFromPos(const App& app, size_t pos) {
    // Binary search for line containing pos
    size_t lo = 0, hi = app.editorLineStarts.size();
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (app.editorLineStarts[mid] <= pos) lo = mid;
        else hi = mid;
    }
    return lo;
}

static size_t getColFromPos(const App& app, size_t pos) {
    size_t line = getLineFromPos(app, pos);
    return pos - app.editorLineStarts[line];
}

static size_t getLineEnd(const App& app, size_t line) {
    if (line + 1 < app.editorLineStarts.size())
        return app.editorLineStarts[line + 1] - 1; // before '\n'
    return app.editorText.size();
}

static size_t getLineLength(const App& app, size_t line) {
    return getLineEnd(app, line) - app.editorLineStarts[line];
}

// --- Undo/Redo ---

static void pushUndo(App& app, App::EditAction::Type type, size_t pos,
                      const std::wstring& text, size_t curBefore, size_t curAfter) {
    // Coalesce consecutive single-char inserts
    if (type == App::EditAction::Insert && text.size() == 1 && !app.undoStack.empty()) {
        auto& last = app.undoStack.back();
        if (last.type == App::EditAction::Insert &&
            last.position + last.text.size() == pos &&
            text[0] != L'\n' && text[0] != L' ') {
            last.text += text;
            last.cursorAfter = curAfter;
            return;
        }
    }
    app.undoStack.push_back({type, pos, text, curBefore, curAfter});
    app.redoStack.clear();
}

static void editorUndo(App& app) {
    if (app.undoStack.empty()) return;
    auto action = app.undoStack.back();
    app.undoStack.pop_back();

    if (action.type == App::EditAction::Insert) {
        // Reverse: delete the inserted text
        app.editorText.erase(action.position, action.text.size());
        app.editorCursorPos = action.cursorBefore;
        app.redoStack.push_back(action);
    } else {
        // Reverse: re-insert the deleted text
        app.editorText.insert(action.position, action.text);
        app.editorCursorPos = action.cursorBefore;
        app.redoStack.push_back(action);
    }
    rebuildLineStarts(app);
    app.editorHasSelection = false;
    app.editorDesiredCol = -1;
}

static void editorRedo(App& app) {
    if (app.redoStack.empty()) return;
    auto action = app.redoStack.back();
    app.redoStack.pop_back();

    if (action.type == App::EditAction::Insert) {
        app.editorText.insert(action.position, action.text);
        app.editorCursorPos = action.cursorAfter;
        app.undoStack.push_back(action);
    } else {
        app.editorText.erase(action.position, action.text.size());
        app.editorCursorPos = action.cursorAfter;
        app.undoStack.push_back(action);
    }
    rebuildLineStarts(app);
    app.editorHasSelection = false;
    app.editorDesiredCol = -1;
}

// --- Selection helpers ---

static void editorDeleteSelection(App& app) {
    if (!app.editorHasSelection) return;
    size_t selMin = std::min(app.editorSelStart, app.editorSelEnd);
    size_t selMax = std::max(app.editorSelStart, app.editorSelEnd);
    std::wstring deleted = app.editorText.substr(selMin, selMax - selMin);
    pushUndo(app, App::EditAction::Delete, selMin, deleted, app.editorCursorPos, selMin);
    app.editorText.erase(selMin, selMax - selMin);
    app.editorCursorPos = selMin;
    app.editorHasSelection = false;
    rebuildLineStarts(app);
}

static size_t editorSelMin(const App& app) {
    return std::min(app.editorSelStart, app.editorSelEnd);
}

static size_t editorSelMax(const App& app) {
    return std::max(app.editorSelStart, app.editorSelEnd);
}

static std::wstring editorGetSelectedText(const App& app) {
    if (!app.editorHasSelection) return {};
    size_t mn = editorSelMin(app);
    size_t mx = editorSelMax(app);
    return app.editorText.substr(mn, mx - mn);
}

// Start selection from current cursor if shift held, otherwise clear
static void editorStartOrExtendSelection(App& app, bool shift) {
    if (shift) {
        if (!app.editorHasSelection) {
            app.editorSelStart = app.editorCursorPos;
            app.editorHasSelection = true;
        }
    } else {
        app.editorHasSelection = false;
    }
}

static void editorUpdateSelEnd(App& app) {
    app.editorSelEnd = app.editorCursorPos;
    if (app.editorSelStart == app.editorSelEnd)
        app.editorHasSelection = false;
}

// --- Word boundary helpers ---

static bool isEditorWordChar(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= L'0' && c <= L'9') || c == L'_';
}

static size_t editorWordLeft(const App& app, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && !isEditorWordChar(app.editorText[pos])) pos--;
    while (pos > 0 && isEditorWordChar(app.editorText[pos - 1])) pos--;
    return pos;
}

static size_t editorWordRight(const App& app, size_t pos) {
    size_t len = app.editorText.size();
    while (pos < len && !isEditorWordChar(app.editorText[pos])) pos++;
    while (pos < len && isEditorWordChar(app.editorText[pos])) pos++;
    return pos;
}

// --- Clipboard ---

static void editorCopyToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        wchar_t* ptr = (wchar_t*)GlobalLock(hMem);
        memcpy(ptr, text.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
}

static std::wstring editorGetClipboard(HWND hwnd) {
    if (!OpenClipboard(hwnd)) return {};
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return {}; }
    wchar_t* ptr = (wchar_t*)GlobalLock(hData);
    std::wstring text = ptr ? ptr : L"";
    GlobalUnlock(hData);
    CloseClipboard();
    // Normalize \r\n to \n
    std::wstring result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == L'\r') {
            result += L'\n';
            if (i + 1 < text.size() && text[i + 1] == L'\n') i++;
        } else {
            result += text[i];
        }
    }
    return result;
}

// --- Scroll helpers ---

static void editorEnsureCursorVisible(App& app) {
    if (app.editorLineStarts.empty()) return;
    size_t line = getLineFromPos(app, app.editorCursorPos);
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float padding = 8.0f;
    float cursorY = padding + line * lineHeight;

    if (cursorY < app.editorScrollY + lineHeight) {
        app.editorScrollY = std::max(0.0f, cursorY - lineHeight);
    }
    if (cursorY + lineHeight > app.editorScrollY + app.height - lineHeight) {
        app.editorScrollY = cursorY + lineHeight * 2 - app.height;
    }
    app.editorScrollY = std::max(0.0f, app.editorScrollY);
}

// --- Editor search ---

void performEditorSearch(App& app) {
    app.editorSearchMatches.clear();
    app.editorSearchCurrentIndex = 0;

    if (app.searchQuery.empty() || app.editorText.empty()) return;

    // Build lowercase versions for case-insensitive search
    std::wstring textLower;
    textLower.resize(app.editorText.size());
    for (size_t i = 0; i < app.editorText.size(); i++) {
        textLower[i] = towlower(app.editorText[i]);
    }

    std::wstring queryLower;
    queryLower.resize(app.searchQuery.size());
    for (size_t i = 0; i < app.searchQuery.size(); i++) {
        queryLower[i] = towlower(app.searchQuery[i]);
    }

    app.editorSearchMatches.reserve(64);

    size_t pos = 0;
    while ((pos = textLower.find(queryLower, pos)) != std::wstring::npos) {
        app.editorSearchMatches.push_back({pos, app.searchQuery.length()});
        pos += app.searchQuery.length();
    }
}

void scrollEditorToMatch(App& app) {
    if (app.editorSearchMatches.empty() || app.editorSearchCurrentIndex < 0 ||
        app.editorSearchCurrentIndex >= (int)app.editorSearchMatches.size()) return;

    const auto& match = app.editorSearchMatches[app.editorSearchCurrentIndex];

    // Move cursor to match position
    app.editorCursorPos = match.startPos;
    app.editorDesiredCol = -1;

    // Find the line containing the match
    size_t line = getLineFromPos(app, match.startPos);
    float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f;
    float padding = 8.0f;
    float matchY = padding + line * lineHeight;

    // Center match in viewport
    app.editorScrollY = matchY - app.height / 2.0f;
    app.editorScrollY = std::max(0.0f, app.editorScrollY);
    float maxScroll = std::max(0.0f, app.editorContentHeight - app.height);
    if (maxScroll > 0) {
        app.editorScrollY = std::min(app.editorScrollY, maxScroll);
    }
}

// --- Debounced reparse ---

static void scheduleReparse(App& app) {
    if (!app.editorDirty) {
        app.editorDirty = true;
        // Update window title with dirty marker
        std::wstring title = L"* ";
        std::wstring wpath = toWide(app.currentFile);
        size_t lastSep = wpath.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos) title += wpath.substr(lastSep + 1);
        else title += wpath;
        title += L" - Tinta";
        SetWindowTextW(app.hwnd, title.c_str());
    }
    // Reparse immediately to update preview
    editorReparse(app);
}

void editorReparse(App& app) {
    KillTimer(app.hwnd, TIMER_EDITOR_REPARSE);
    std::string utf8 = toUtf8(app.editorText);

    // Build line-to-byte-offset mapping for scroll sync
    app.editorLineByteOffsets.clear();
    app.editorLineByteOffsets.push_back(0);
    for (size_t i = 0; i < utf8.size(); i++) {
        if (utf8[i] == '\n') {
            app.editorLineByteOffsets.push_back(i + 1);
        }
    }

    auto result = app.parser.parse(utf8);
    if (result.success) {
        app.root = result.root;
        app.parseTimeUs = result.parseTimeUs;
        app.layoutDirty = true;
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

// --- Mode transitions ---

void enterEditMode(App& app) {
    if (app.currentFile.empty()) {
        // Show brief "No file loaded" notification
        app.editorNotificationMsg = L"No file loaded";
        app.showEditModeNotification = true;
        app.editModeNotificationAlpha = 1.0f;
        app.editModeNotificationStart = std::chrono::steady_clock::now();
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }

    // Load raw file content
    std::wstring widePath = toWide(app.currentFile);
    std::ifstream file(widePath, std::ios::binary);
    if (!file) return;

    std::stringstream buf;
    buf << file.rdbuf();
    std::string content = buf.str();

    app.editorText = fromUtf8(content);
    // Normalize \r\n to \n
    std::wstring normalized;
    normalized.reserve(app.editorText.size());
    for (size_t i = 0; i < app.editorText.size(); i++) {
        if (app.editorText[i] == L'\r') {
            normalized += L'\n';
            if (i + 1 < app.editorText.size() && app.editorText[i + 1] == L'\n') i++;
        } else {
            normalized += app.editorText[i];
        }
    }
    app.editorText = std::move(normalized);

    rebuildLineStarts(app);
    app.editorCursorPos = 0;
    app.editorDesiredCol = -1;
    app.editorScrollY = 0;
    app.editorHasSelection = false;
    app.editorDirty = false;
    app.undoStack.clear();
    app.redoStack.clear();
    app.editorSearchMatches.clear();
    app.editorSearchCurrentIndex = 0;
    app.editMode = true;
    app.escPressedOnce = false;

    // Disable file watch while editing
    KillTimer(app.hwnd, 1); // TIMER_FILE_WATCH = 1

    // Show notification
    app.editorNotificationMsg = L"Press ESC twice to exit edit mode";
    app.showEditModeNotification = true;
    app.editModeNotificationAlpha = 1.0f;
    app.editModeNotificationStart = std::chrono::steady_clock::now();

    // Force layout at new width
    app.layoutDirty = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void exitEditMode(App& app) {
    if (app.editorDirty) {
        int result = MessageBoxW(app.hwnd,
            L"You have unsaved changes. Save before exiting?",
            L"Unsaved Changes",
            MB_YESNOCANCEL | MB_ICONWARNING);
        if (result == IDCANCEL) return;
        if (result == IDYES) {
            saveEditorFile(app, app.hwnd);
        }
    }

    app.editMode = false;
    app.editorText.clear();
    app.editorLineStarts.clear();
    app.undoStack.clear();
    app.redoStack.clear();
    app.editorSearchMatches.clear();
    app.editorSearchCurrentIndex = 0;
    // Close search if open
    if (app.showSearch) {
        app.showSearch = false;
        app.searchActive = false;
        app.searchQuery.clear();
        app.searchAnimation = 0;
    }
    KillTimer(app.hwnd, TIMER_EDITOR_REPARSE);

    // Re-enable file watch
    updateFileWriteTime(app);
    SetTimer(app.hwnd, 1, 500, nullptr); // TIMER_FILE_WATCH = 1

    // Reload file to pick up saved changes
    std::wstring widePath = toWide(app.currentFile);
    std::ifstream file(widePath);
    if (file) {
        std::stringstream buf;
        buf << file.rdbuf();
        auto result = app.parser.parse(buf.str());
        if (result.success) {
            app.root = result.root;
            app.parseTimeUs = result.parseTimeUs;
        }
    }

    // Update window title (remove dirty marker)
    std::wstring title = L"Tinta";
    if (!app.currentFile.empty()) {
        std::wstring wpath = toWide(app.currentFile);
        size_t lastSep = wpath.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos)
            title = wpath.substr(lastSep + 1) + L" - Tinta";
        else
            title = wpath + L" - Tinta";
    }
    SetWindowTextW(app.hwnd, title.c_str());

    app.layoutDirty = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// --- File save ---

void saveEditorFile(App& app, HWND hwnd) {
    if (app.currentFile.empty()) return;

    std::string utf8 = toUtf8(app.editorText);

    // Detect original line ending style by reading first line
    std::wstring widePath = toWide(app.currentFile);
    bool useCRLF = false;
    {
        std::ifstream check(widePath, std::ios::binary);
        if (check) {
            char buf[4096];
            check.read(buf, sizeof(buf));
            auto count = check.gcount();
            for (int i = 0; i < count - 1; i++) {
                if (buf[i] == '\n') break;
                if (buf[i] == '\r' && buf[i + 1] == '\n') { useCRLF = true; break; }
            }
        }
    }

    // Convert \n to \r\n if original used CRLF
    if (useCRLF) {
        std::string crlf;
        crlf.reserve(utf8.size() + utf8.size() / 20);
        for (size_t i = 0; i < utf8.size(); i++) {
            if (utf8[i] == '\n') {
                crlf += "\r\n";
            } else {
                crlf += utf8[i];
            }
        }
        utf8 = std::move(crlf);
    }

    std::ofstream out(widePath, std::ios::binary);
    if (out) {
        out.write(utf8.data(), utf8.size());
        out.close();
        app.editorDirty = false;
        updateFileWriteTime(app);

        // Reparse and update preview immediately
        editorReparse(app);

        // Show "Saved!" notification
        app.editorNotificationMsg = L"Saved!";
        app.showEditModeNotification = true;
        app.editModeNotificationAlpha = 1.0f;
        app.editModeNotificationStart = std::chrono::steady_clock::now();

        // Update window title
        std::wstring title = toWide(app.currentFile);
        size_t lastSep = title.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos) title = title.substr(lastSep + 1);
        title += L" - Tinta";
        SetWindowTextW(hwnd, title.c_str());

        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// --- Input handlers ---

void handleEditorKeyDown(App& app, HWND hwnd, WPARAM wParam) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    // Search still works in edit mode via Ctrl+F
    if (ctrl && wParam == 'F') {
        if (!app.showSearch) {
            app.showSearch = true;
            app.searchActive = true;
            app.searchAnimation = 0;
            app.searchQuery.clear();
            app.editorSearchMatches.clear();
            app.editorSearchCurrentIndex = 0;
            app.searchCurrentIndex = 0;
            app.searchJustOpened = true;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // If search is open, route to search handling
    if (app.showSearch && app.searchActive) {
        switch (wParam) {
            case VK_ESCAPE:
                app.showSearch = false;
                app.searchActive = false;
                app.searchQuery.clear();
                app.editorSearchMatches.clear();
                app.editorSearchCurrentIndex = 0;
                app.searchAnimation = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case VK_RETURN: {
                if (!app.editorSearchMatches.empty()) {
                    app.editorSearchCurrentIndex = (app.editorSearchCurrentIndex + 1) % (int)app.editorSearchMatches.size();
                    app.searchCurrentIndex = app.editorSearchCurrentIndex;
                    scrollEditorToMatch(app);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
            case VK_BACK: {
                if (!app.searchQuery.empty()) {
                    app.searchQuery.pop_back();
                    performEditorSearch(app);
                    app.searchCurrentIndex = app.editorSearchCurrentIndex;
                    if (!app.editorSearchMatches.empty()) scrollEditorToMatch(app);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            }
        }
        return; // Let WM_CHAR handle text input for search
    }

    if (wParam == VK_ESCAPE) {
        auto now = std::chrono::steady_clock::now();
        if (app.escPressedOnce) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - app.lastEscTime).count();
            if (elapsed < 500) {
                exitEditMode(app);
                return;
            }
        }
        app.escPressedOnce = true;
        app.lastEscTime = now;

        // Show brief hint
        app.editorNotificationMsg = L"Press ESC again to exit edit mode";
        app.showEditModeNotification = true;
        app.editModeNotificationAlpha = 1.0f;
        app.editModeNotificationStart = now;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    app.escPressedOnce = false;

    if (ctrl) {
        switch (wParam) {
            case 'S':
                saveEditorFile(app, hwnd);
                return;
            case 'Z':
                editorUndo(app);
                scheduleReparse(app);
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case 'Y':
                editorRedo(app);
                scheduleReparse(app);
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case 'A':
                app.editorSelStart = 0;
                app.editorSelEnd = app.editorText.size();
                app.editorCursorPos = app.editorText.size();
                app.editorHasSelection = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case 'C':
                if (app.editorHasSelection) {
                    editorCopyToClipboard(hwnd, editorGetSelectedText(app));
                }
                return;
            case 'X':
                if (app.editorHasSelection) {
                    editorCopyToClipboard(hwnd, editorGetSelectedText(app));
                    editorDeleteSelection(app);
                    scheduleReparse(app);
                    editorEnsureCursorVisible(app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return;
            case 'V': {
                std::wstring paste = editorGetClipboard(hwnd);
                if (!paste.empty()) {
                    if (app.editorHasSelection) editorDeleteSelection(app);
                    size_t before = app.editorCursorPos;
                    app.editorText.insert(app.editorCursorPos, paste);
                    app.editorCursorPos += paste.size();
                    pushUndo(app, App::EditAction::Insert, before, paste, before, app.editorCursorPos);
                    rebuildLineStarts(app);
                    scheduleReparse(app);
                    editorEnsureCursorVisible(app);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return;
            }
            case VK_HOME:
                editorStartOrExtendSelection(app, shift);
                app.editorCursorPos = 0;
                app.editorDesiredCol = -1;
                if (shift) editorUpdateSelEnd(app);
                else app.editorHasSelection = false;
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case VK_END:
                editorStartOrExtendSelection(app, shift);
                app.editorCursorPos = app.editorText.size();
                app.editorDesiredCol = -1;
                if (shift) editorUpdateSelEnd(app);
                else app.editorHasSelection = false;
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case VK_LEFT:
                editorStartOrExtendSelection(app, shift);
                app.editorCursorPos = editorWordLeft(app, app.editorCursorPos);
                app.editorDesiredCol = -1;
                if (shift) editorUpdateSelEnd(app);
                else app.editorHasSelection = false;
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
            case VK_RIGHT:
                editorStartOrExtendSelection(app, shift);
                app.editorCursorPos = editorWordRight(app, app.editorCursorPos);
                app.editorDesiredCol = -1;
                if (shift) editorUpdateSelEnd(app);
                else app.editorHasSelection = false;
                editorEnsureCursorVisible(app);
                InvalidateRect(hwnd, nullptr, FALSE);
                return;
        }
        return; // Don't process other ctrl+key combos as text
    }

    // Non-Ctrl keys
    switch (wParam) {
        case VK_LEFT:
            editorStartOrExtendSelection(app, shift);
            if (!shift && app.editorHasSelection) {
                app.editorCursorPos = editorSelMin(app);
                app.editorHasSelection = false;
            } else if (app.editorCursorPos > 0) {
                app.editorCursorPos--;
            }
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;

        case VK_RIGHT:
            editorStartOrExtendSelection(app, shift);
            if (!shift && app.editorHasSelection) {
                app.editorCursorPos = editorSelMax(app);
                app.editorHasSelection = false;
            } else if (app.editorCursorPos < app.editorText.size()) {
                app.editorCursorPos++;
            }
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;

        case VK_UP: {
            editorStartOrExtendSelection(app, shift);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            if (line > 0) {
                size_t col = (app.editorDesiredCol >= 0) ? (size_t)app.editorDesiredCol : getColFromPos(app, app.editorCursorPos);
                if (app.editorDesiredCol < 0) app.editorDesiredCol = (int)col;
                size_t prevLineLen = getLineLength(app, line - 1);
                app.editorCursorPos = app.editorLineStarts[line - 1] + std::min(col, prevLineLen);
            }
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_DOWN: {
            editorStartOrExtendSelection(app, shift);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            if (line + 1 < app.editorLineStarts.size()) {
                size_t col = (app.editorDesiredCol >= 0) ? (size_t)app.editorDesiredCol : getColFromPos(app, app.editorCursorPos);
                if (app.editorDesiredCol < 0) app.editorDesiredCol = (int)col;
                size_t nextLineLen = getLineLength(app, line + 1);
                app.editorCursorPos = app.editorLineStarts[line + 1] + std::min(col, nextLineLen);
            }
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_HOME: {
            editorStartOrExtendSelection(app, shift);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            app.editorCursorPos = app.editorLineStarts[line];
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_END: {
            editorStartOrExtendSelection(app, shift);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            app.editorCursorPos = getLineEnd(app, line);
            app.editorDesiredCol = -1;
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_PRIOR: { // Page Up
            editorStartOrExtendSelection(app, shift);
            float scale = app.contentScale * app.zoomFactor;
            float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f * scale;
            int pageLines = std::max(1, (int)(app.height / lineHeight) - 2);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            size_t col = getColFromPos(app, app.editorCursorPos);
            size_t targetLine = (line > (size_t)pageLines) ? line - pageLines : 0;
            size_t targetLineLen = getLineLength(app, targetLine);
            app.editorCursorPos = app.editorLineStarts[targetLine] + std::min(col, targetLineLen);
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_NEXT: { // Page Down
            editorStartOrExtendSelection(app, shift);
            float scale = app.contentScale * app.zoomFactor;
            float lineHeight = app.editorTextFormat ? app.editorTextFormat->GetFontSize() * 1.5f : 20.0f * scale;
            int pageLines = std::max(1, (int)(app.height / lineHeight) - 2);
            size_t line = getLineFromPos(app, app.editorCursorPos);
            size_t col = getColFromPos(app, app.editorCursorPos);
            size_t targetLine = std::min(line + pageLines, app.editorLineStarts.size() - 1);
            size_t targetLineLen = getLineLength(app, targetLine);
            app.editorCursorPos = app.editorLineStarts[targetLine] + std::min(col, targetLineLen);
            if (shift) editorUpdateSelEnd(app);
            else app.editorHasSelection = false;
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case VK_DELETE:
            if (app.editorHasSelection) {
                editorDeleteSelection(app);
            } else if (app.editorCursorPos < app.editorText.size()) {
                std::wstring deleted(1, app.editorText[app.editorCursorPos]);
                pushUndo(app, App::EditAction::Delete, app.editorCursorPos, deleted,
                         app.editorCursorPos, app.editorCursorPos);
                app.editorText.erase(app.editorCursorPos, 1);
                rebuildLineStarts(app);
            }
            app.editorDesiredCol = -1;
            scheduleReparse(app);
            editorEnsureCursorVisible(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
    }
}

void handleEditorCharInput(App& app, HWND hwnd, WPARAM wParam) {
    // If search is active, route characters there
    if (app.showSearch && app.searchActive) {
        if (app.searchJustOpened) {
            app.searchJustOpened = false;
            return;
        }
        wchar_t ch = (wchar_t)wParam;
        if (ch >= 32 && ch != 127) {
            app.searchQuery += ch;
            performEditorSearch(app);
            if (!app.editorSearchMatches.empty()) {
                app.editorSearchCurrentIndex = 0;
                app.searchCurrentIndex = 0;
                scrollEditorToMatch(app);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    wchar_t ch = (wchar_t)wParam;

    if (ch == 8) { // Backspace
        if (app.editorHasSelection) {
            editorDeleteSelection(app);
        } else if (app.editorCursorPos > 0) {
            size_t before = app.editorCursorPos;
            std::wstring deleted(1, app.editorText[app.editorCursorPos - 1]);
            app.editorText.erase(app.editorCursorPos - 1, 1);
            app.editorCursorPos--;
            pushUndo(app, App::EditAction::Delete, app.editorCursorPos, deleted, before, app.editorCursorPos);
            rebuildLineStarts(app);
        }
        app.editorDesiredCol = -1;
        scheduleReparse(app);
        editorEnsureCursorVisible(app);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (ch == 13) { // Enter -> \n
        ch = L'\n';
    }

    if (ch == 9) { // Tab -> 4 spaces
        std::wstring spaces = L"    ";
        if (app.editorHasSelection) editorDeleteSelection(app);
        size_t before = app.editorCursorPos;
        app.editorText.insert(app.editorCursorPos, spaces);
        app.editorCursorPos += 4;
        pushUndo(app, App::EditAction::Insert, before, spaces, before, app.editorCursorPos);
        rebuildLineStarts(app);
        app.editorDesiredCol = -1;
        scheduleReparse(app);
        editorEnsureCursorVisible(app);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (ch == 27) return; // ESC handled in KeyDown
    if (ch < 32 && ch != L'\n') return; // Ignore other control chars

    // Normal character insertion
    if (app.editorHasSelection) editorDeleteSelection(app);
    std::wstring ins(1, ch);
    size_t before = app.editorCursorPos;
    app.editorText.insert(app.editorCursorPos, ins);
    app.editorCursorPos++;
    pushUndo(app, App::EditAction::Insert, before, ins, before, app.editorCursorPos);
    rebuildLineStarts(app);
    app.editorDesiredCol = -1;
    scheduleReparse(app);
    editorEnsureCursorVisible(app);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Mouse handling ---

static size_t editorPosFromClick(const App& app, int x, int y) {
    if (!app.editorTextFormat || app.editorLineStarts.empty()) return 0;

    float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    float padding = 8.0f;
    float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth : app.editorTextFormat->GetFontSize() * 0.6f;

    float adjustedY = y + app.editorScrollY - padding;
    size_t line = (size_t)std::max(0, (int)(adjustedY / lineHeight));
    if (line >= app.editorLineStarts.size()) line = app.editorLineStarts.size() - 1;

    size_t lineStart = app.editorLineStarts[line];
    size_t lineLen = getLineLength(app, line);

    float gutterWidth = 48.0f;
    float adjustedX = (float)x - gutterWidth - padding;
    size_t col = (size_t)std::max(0, (int)(adjustedX / charWidth + 0.5f));
    col = std::min(col, lineLen);

    return lineStart + col;
}

void handleEditorMouseDown(App& app, HWND hwnd, int x, int y) {
    float editorWidth = app.width * app.editorSplitRatio - 3;

    // Check for separator
    float sepX = app.width * app.editorSplitRatio;
    if (std::abs((float)x - sepX) < 6.0f) {
        app.draggingSeparator = true;
        app.separatorDragStartX = (float)x;
        app.separatorDragStartRatio = app.editorSplitRatio;
        SetCapture(hwnd);
        return;
    }

    // Only handle clicks in the editor pane (left side)
    if (x > editorWidth) return;

    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    size_t clickPos = editorPosFromClick(app, x, y);

    // Detect double/triple click
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - app.lastClickTime).count();
    bool isRepeat = (elapsed < 500 && std::abs(x - app.lastClickX) < 5 && std::abs(y - app.lastClickY) < 5);

    if (isRepeat) {
        app.clickCount = std::min(app.clickCount + 1, 3);
    } else {
        app.clickCount = 1;
    }
    app.lastClickTime = now;
    app.lastClickX = x;
    app.lastClickY = y;

    if (app.clickCount == 2) {
        // Double-click: select word
        size_t wordStart = clickPos;
        size_t wordEnd = clickPos;
        while (wordStart > 0 && isEditorWordChar(app.editorText[wordStart - 1])) wordStart--;
        while (wordEnd < app.editorText.size() && isEditorWordChar(app.editorText[wordEnd])) wordEnd++;
        app.editorSelStart = wordStart;
        app.editorSelEnd = wordEnd;
        app.editorCursorPos = wordEnd;
        app.editorHasSelection = (wordStart != wordEnd);
    } else if (app.clickCount == 3) {
        // Triple-click: select line
        size_t line = getLineFromPos(app, clickPos);
        app.editorSelStart = app.editorLineStarts[line];
        app.editorSelEnd = getLineEnd(app, line);
        if (app.editorSelEnd < app.editorText.size()) app.editorSelEnd++; // include \n
        app.editorCursorPos = app.editorSelEnd;
        app.editorHasSelection = true;
    } else {
        // Single click
        if (shift && app.editorHasSelection) {
            // Extend selection
            app.editorSelEnd = clickPos;
            app.editorCursorPos = clickPos;
        } else if (shift) {
            app.editorSelStart = app.editorCursorPos;
            app.editorSelEnd = clickPos;
            app.editorCursorPos = clickPos;
            app.editorHasSelection = true;
        } else {
            app.editorCursorPos = clickPos;
            app.editorSelStart = clickPos;
            app.editorSelEnd = clickPos;
            app.editorHasSelection = false;
        }
        app.editorSelecting = true;
        SetCapture(hwnd);
    }

    app.editorDesiredCol = -1;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void handleEditorMouseUp(App& app, HWND hwnd, int x, int y) {
    if (app.draggingSeparator) {
        app.draggingSeparator = false;
        ReleaseCapture();
        return;
    }
    if (app.editorSelecting) {
        app.editorSelecting = false;
        ReleaseCapture();
        if (app.editorSelStart == app.editorSelEnd) {
            app.editorHasSelection = false;
        }
    }
}

void handleEditorMouseMove(App& app, HWND hwnd, int x, int y) {
    float editorWidth = app.width * app.editorSplitRatio - 3;

    if (app.draggingSeparator) {
        float dx = (float)x - app.separatorDragStartX;
        float newRatio = app.separatorDragStartRatio + dx / app.width;
        app.editorSplitRatio = std::max(0.2f, std::min(0.8f, newRatio));
        app.layoutDirty = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (app.editorSelecting) {
        size_t pos = editorPosFromClick(app, x, y);
        app.editorSelEnd = pos;
        app.editorCursorPos = pos;
        app.editorHasSelection = (app.editorSelStart != app.editorSelEnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    // Cursor shape
    float sepX = app.width * app.editorSplitRatio;
    static HCURSOR cursorSizeWE = LoadCursor(nullptr, IDC_SIZEWE);
    static HCURSOR cursorIBeam = LoadCursor(nullptr, IDC_IBEAM);
    static HCURSOR cursorArrow = LoadCursor(nullptr, IDC_ARROW);

    if (std::abs((float)x - sepX) < 6.0f) {
        SetCursor(cursorSizeWE);
    } else if (x < editorWidth) {
        SetCursor(cursorIBeam);
    }
    // Preview pane cursor is handled by the normal handleMouseMove
}

void handleEditorMouseWheel(App& app, HWND hwnd, float delta) {
    app.editorScrollY -= delta * 60.0f;
    app.editorScrollY = std::max(0.0f, app.editorScrollY);
    float maxScroll = std::max(0.0f, app.editorContentHeight - app.height);
    app.editorScrollY = std::min(app.editorScrollY, maxScroll);
    InvalidateRect(hwnd, nullptr, FALSE);
}

// --- Rendering ---

void renderEditor(App& app, float editorWidth) {
    if (!app.editorTextFormat || app.editorLineStarts.empty()) return;

    float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
    float padding = 8.0f;
    float charWidth = app.editorCharWidth > 0 ? app.editorCharWidth : app.editorTextFormat->GetFontSize() * 0.6f;

    // Editor background
    app.brush->SetColor(app.theme.background);
    app.renderTarget->FillRectangle(
        D2D1::RectF(0, 0, editorWidth, (float)app.height), app.brush);

    // Clip to editor area
    app.renderTarget->PushAxisAlignedClip(
        D2D1::RectF(0, 0, editorWidth, (float)app.height),
        D2D1_ANTIALIAS_MODE_ALIASED);

    // Calculate visible line range
    int firstVisible = std::max(0, (int)((app.editorScrollY - padding) / lineHeight));
    int lastVisible = (int)((app.editorScrollY + app.height) / lineHeight) + 1;
    lastVisible = std::min(lastVisible, (int)app.editorLineStarts.size() - 1);

    // Selection range
    size_t selMin = 0, selMax = 0;
    if (app.editorHasSelection) {
        selMin = editorSelMin(app);
        selMax = editorSelMax(app);
    }

    // Gutter width for line numbers
    float gutterWidth = 48.0f;

    // Search match scanning index (both sorted by position, so we advance together)
    size_t searchScanIdx = 0;
    bool hasSearchMatches = app.showSearch && !app.searchQuery.empty() && !app.editorSearchMatches.empty();

    // Advance search scan index to first match that could overlap visible lines
    if (hasSearchMatches && firstVisible > 0) {
        size_t firstVisiblePos = app.editorLineStarts[firstVisible];
        while (searchScanIdx < app.editorSearchMatches.size() &&
               app.editorSearchMatches[searchScanIdx].startPos + app.editorSearchMatches[searchScanIdx].length <= firstVisiblePos) {
            searchScanIdx++;
        }
    }

    for (int i = firstVisible; i <= lastVisible && i < (int)app.editorLineStarts.size(); i++) {
        float lineY = padding + i * lineHeight - app.editorScrollY;
        size_t lineStart = app.editorLineStarts[i];
        size_t lineLen = getLineLength(app, i);

        // Line number
        wchar_t lineNum[16];
        swprintf(lineNum, 16, L"%d", i + 1);
        D2D1_COLOR_F gutterColor = app.theme.text;
        gutterColor.a = 0.3f;
        app.brush->SetColor(gutterColor);
        app.renderTarget->DrawText(lineNum, (UINT32)wcslen(lineNum), app.editorTextFormat,
            D2D1::RectF(4, lineY, gutterWidth - 4, lineY + lineHeight), app.brush);

        // Selection highlight on this line
        if (app.editorHasSelection && selMax > lineStart && selMin < lineStart + lineLen + 1) {
            size_t hlStart = (selMin > lineStart) ? selMin - lineStart : 0;
            size_t hlEnd = std::min(selMax - lineStart, lineLen + 1);
            float hlX1 = gutterWidth + padding + hlStart * charWidth;
            float hlX2 = gutterWidth + padding + hlEnd * charWidth;
            app.brush->SetColor(D2D1::ColorF(0.2f, 0.4f, 0.9f, 0.35f));
            app.renderTarget->FillRectangle(
                D2D1::RectF(hlX1, lineY, hlX2, lineY + lineHeight), app.brush);
        }

        // Search match highlights on this line
        if (hasSearchMatches) {
            size_t lineEnd = lineStart + lineLen;
            size_t si = searchScanIdx;
            while (si < app.editorSearchMatches.size()) {
                const auto& m = app.editorSearchMatches[si];
                if (m.startPos >= lineEnd) break; // past this line
                size_t mEnd = m.startPos + m.length;
                if (mEnd <= lineStart) { si++; continue; } // before this line

                // Compute overlap with this line
                size_t overlapStart = std::max(lineStart, m.startPos);
                size_t overlapEnd = std::min(lineEnd, mEnd);
                if (overlapStart < overlapEnd) {
                    float hlX1 = gutterWidth + padding + (overlapStart - lineStart) * charWidth;
                    float hlX2 = gutterWidth + padding + (overlapEnd - lineStart) * charWidth;

                    bool isCurrent = ((int)si == app.editorSearchCurrentIndex);
                    if (isCurrent) {
                        app.brush->SetColor(D2D1::ColorF(1.0f, 0.6f, 0.0f, 0.5f));  // Orange
                    } else {
                        app.brush->SetColor(D2D1::ColorF(1.0f, 0.9f, 0.0f, 0.3f));  // Yellow
                    }
                    app.renderTarget->FillRectangle(
                        D2D1::RectF(hlX1, lineY, hlX2, lineY + lineHeight), app.brush);
                }
                si++;
            }
            // Advance scan index past matches that ended before or at this line's start
            while (searchScanIdx < app.editorSearchMatches.size() &&
                   app.editorSearchMatches[searchScanIdx].startPos + app.editorSearchMatches[searchScanIdx].length <= lineEnd) {
                searchScanIdx++;
            }
        }

        // Line text
        if (lineLen > 0) {
            std::wstring_view lineView(app.editorText.data() + lineStart, lineLen);
            app.brush->SetColor(app.theme.text);
            app.renderTarget->DrawText(lineView.data(), (UINT32)lineView.size(), app.editorTextFormat,
                D2D1::RectF(gutterWidth + padding, lineY,
                            editorWidth - padding, lineY + lineHeight), app.brush);
        }
    }

    // Cursor (blinking)
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    bool cursorVisible = (ms % 1000) < 500;
    if (cursorVisible) {
        size_t curLine = getLineFromPos(app, app.editorCursorPos);
        size_t curCol = getColFromPos(app, app.editorCursorPos);
        float curX = gutterWidth + padding + curCol * charWidth;
        float curY = padding + curLine * lineHeight - app.editorScrollY;

        app.brush->SetColor(app.theme.text);
        app.renderTarget->FillRectangle(
            D2D1::RectF(curX, curY, curX + 2, curY + lineHeight), app.brush);
    }
    // Keep redrawing for cursor blink
    InvalidateRect(app.hwnd, nullptr, FALSE);

    // Update content height for scrolling
    app.editorContentHeight = padding * 2 + app.editorLineStarts.size() * lineHeight;

    // Editor scrollbar
    if (app.editorContentHeight > app.height) {
        float maxScroll = app.editorContentHeight - app.height;
        float sbHeight = (float)app.height / app.editorContentHeight * app.height;
        sbHeight = std::max(sbHeight, 30.0f);
        float sbY = (maxScroll > 0) ? (app.editorScrollY / maxScroll * (app.height - sbHeight)) : 0;

        float sbColorValue = app.theme.isDark ? 1.0f : 0.0f;
        app.brush->SetColor(D2D1::ColorF(sbColorValue, sbColorValue, sbColorValue, 0.3f));
        app.renderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(editorWidth - 10, sbY,
                                           editorWidth - 4, sbY + sbHeight), 3, 3),
            app.brush);
    }

    app.renderTarget->PopAxisAlignedClip();
}

void renderSeparator(App& app) {
    float sepX = app.width * app.editorSplitRatio;
    float sepWidth = 6.0f;

    // Separator background
    D2D1_COLOR_F sepColor = app.theme.isDark ? hexColor(0x3A3A40) : hexColor(0xD0D0D0);
    app.brush->SetColor(sepColor);
    app.renderTarget->FillRectangle(
        D2D1::RectF(sepX - sepWidth / 2, 0, sepX + sepWidth / 2, (float)app.height), app.brush);

    // Grip dots (3 dots in center)
    float dotRadius = 2.0f;
    float dotSpacing = 10.0f;
    float centerY = app.height / 2.0f;
    D2D1_COLOR_F dotColor = app.theme.isDark ? hexColor(0x808080) : hexColor(0x808080);
    app.brush->SetColor(dotColor);

    for (int i = -1; i <= 1; i++) {
        app.renderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(sepX, centerY + i * dotSpacing), dotRadius, dotRadius),
            app.brush);
    }
}

void renderEditModeNotification(App& app) {
    if (!app.showEditModeNotification) return;

    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - app.editModeNotificationStart).count();

    if (elapsed > 3.0f) {
        app.showEditModeNotification = false;
        return;
    }

    float alpha = 1.0f;
    if (elapsed > 1.5f) {
        alpha = 1.0f - (elapsed - 1.5f) / 1.5f;
    }

    const wchar_t* msg = app.editorNotificationMsg.c_str();
    size_t msgLen = app.editorNotificationMsg.size();

    float pillWidth = (msgLen <= 10) ? 120.0f : 300.0f;
    float pillHeight = 30.0f;
    float pillX = (float)(app.width - pillWidth) / 2.0f;
    float pillY = (float)app.height - 60.0f;

    // Green pill background
    app.brush->SetColor(D2D1::ColorF(0.2f, 0.6f, 0.3f, 0.9f * alpha));
    app.renderTarget->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(pillX, pillY, pillX + pillWidth, pillY + pillHeight), 15, 15),
        app.brush);

    // Text
    app.brush->SetColor(D2D1::ColorF(1, 1, 1, alpha));
    IDWriteTextFormat* fmt = app.searchTextFormat ? app.searchTextFormat : app.textFormat;
    if (fmt) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.renderTarget->DrawText(msg, (UINT32)msgLen, fmt,
            D2D1::RectF(pillX, pillY, pillX + pillWidth, pillY + pillHeight), app.brush);
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    InvalidateRect(app.hwnd, nullptr, FALSE);
}
