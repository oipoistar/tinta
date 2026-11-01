// The real modal Save dialog, driven in a test's own UI thread: a CBT
// hook finds it as it opens, reads its suggested name, and types a
// destination and presses OK, or cancels. Nothing on the desktop changes.
#pragma once

#include <windows.h>
#include <commdlg.h>
#include <dlgs.h>

#include <string>

namespace {
// Exercise the real modal file picker in this test's UI thread only.
// No production hooks or changes to the user's desktop are needed.
struct SavePickerProbe {
    const wchar_t* destination = nullptr; // null = Cancel
    int shown = 0;
    std::wstring suggestedName;
};
SavePickerProbe* pickerProbe = nullptr;
// Runs every 50 ms until the dialog is ready: on a slow machine the dialog
// is still filling in its default name when it activates, and a name typed
// before that was put back, so OK saved under the original name
void CALLBACK pickerAction(HWND dialog, UINT, UINT_PTR timer, DWORD) {
    HWND filename = GetDlgItem(dialog, edt1);
    // Modern Windows nests the filename edit (1001) inside DirectUI;
    // legacy dialogs expose edt1 directly.
    if (!filename) EnumChildWindows(dialog, [](HWND child, LPARAM result) -> BOOL {
        wchar_t cls[80]{};
        GetClassNameW(child, cls, 80);
        if (GetDlgCtrlID(child) == 1001 && std::wstring(cls) == L"Edit") {
            *(HWND*)result = child;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&filename);
    wchar_t name[MAX_PATH]{};
    if (!filename || !IsWindowVisible(filename) || !GetWindowTextW(filename, name, MAX_PATH))
        return;  // not ready yet: the default name is not in the box
    if (pickerProbe->suggestedName.empty()) pickerProbe->suggestedName = name;
    if (pickerProbe->destination) {
        // Type the name, and press OK only once a later tick still finds
        // it. Typed, not set: the modern dialog ignores text set with
        // SetWindowText and returns its own default name on OK.
        if (std::wstring(name) != pickerProbe->destination) {
            SendMessageW(filename, EM_SETSEL, 0, -1);
            for (const wchar_t* c = pickerProbe->destination; *c; ++c)
                SendMessageW(filename, WM_CHAR, *c, 1);
            return;
        }
        KillTimer(dialog, timer);
        PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    } else {
        KillTimer(dialog, timer);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    }
}
void CALLBACK pickerTimeout(HWND dialog, UINT, UINT_PTR timer, DWORD) {
    KillTimer(dialog, timer);
    PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
}
LRESULT CALLBACK pickerHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE && pickerProbe && pickerProbe->shown == 0) {
        const HWND dialog = (HWND)wParam;
        wchar_t name[32]{};
        GetClassNameW(dialog, name, 32);
        if (std::wstring(name) == L"#32770") {
            ++pickerProbe->shown;
            SetTimer(dialog, 501, 50, pickerAction);
            SetTimer(dialog, 502, 5000, pickerTimeout);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
template<class Action>
SavePickerProbe withSavePicker(const wchar_t* destination, Action action) {
    SavePickerProbe probe;
    probe.destination = destination;
    pickerProbe = &probe;
    const HHOOK hook = SetWindowsHookExW(WH_CBT, pickerHook, nullptr, GetCurrentThreadId());
    if (hook) action();
    if (hook) UnhookWindowsHookEx(hook);
    pickerProbe = nullptr;
    return probe;
}
}
