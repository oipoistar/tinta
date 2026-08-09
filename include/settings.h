#ifndef TINTA_SETTINGS_H
#define TINTA_SETTINGS_H

#include "app.h"

std::wstring getSettingsPath();
void saveSettings(const Settings& settings);
Settings loadSettings();

// Reading position memory (#77): most-recent-first list capped in remember.
// The persist/lookup helpers load and save settings.ini themselves.
void rememberReadingPosition(Settings& settings, const std::string& path, float scrollY);
void persistReadingPosition(const std::string& path, float scrollY);
float findReadingPosition(const Settings& settings, const std::string& path);

// User-remappable single-key actions (#77), written to settings.ini as a
// [Keys] section. Char actions trigger via WM_CHAR (edit ':', help '?');
// the rest are WM_KEYDOWN virtual-key codes. Ctrl combos stay fixed.
struct KeyActionDef {
    const char* name;     // ini key
    unsigned defaultKey;  // VK code, or character for isChar actions
    bool isChar;
};
inline constexpr KeyActionDef KEY_ACTIONS[] = {
    {"search",     'F',    false},
    {"browse",     'B',    false},
    {"toc",        VK_TAB, false},
    {"theme",      'T',    false},
    {"stats",      'S',    false},
    {"quit",       'Q',    false},
    {"newfile",    'N',    false},
    {"zen",        VK_F11, false},
    {"scrollup",   'K',    false},
    {"scrolldown", 'J',    false},
    {"edit",       ':',    true},
    {"help",       '?',    true},
};
inline constexpr int KEY_ACTION_COUNT = 12;
enum KeyActionIndex {
    KA_SEARCH = 0, KA_BROWSE, KA_TOC, KA_THEME, KA_STATS, KA_QUIT,
    KA_NEWFILE, KA_ZEN, KA_SCROLLUP, KA_SCROLLDOWN, KA_EDIT, KA_HELP
};

// Resolves settings.keyOverrides over the defaults into app.keymap
void applyKeymap(App& app, const Settings& settings);
// Key spelling used in settings.ini ("G", "Tab", "F5", ...)
std::string keyIniName(unsigned key);
// Same spelling as a wide string for overlay labels
std::wstring keyLabel(unsigned key);
bool registerFileAssociation();
void openDefaultAppsSettings();
void askAndRegisterFileAssociation(Settings& settings);

#endif // TINTA_SETTINGS_H
