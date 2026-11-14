#ifndef TINTA_SETTINGS_H
#define TINTA_SETTINGS_H

#include "app.h"

std::wstring getSettingsPath();
void saveSettings(const Settings& settings);
Settings loadSettings();

// Reading position memory (#77): most-recent-first list capped in remember.
// The persist/lookup helpers load and save settings.ini themselves.
void rememberReadingPosition(Settings& settings, const std::string& path,
                             float scrollY, float zoom = 0.0f);
void persistReadingPosition(const std::string& path, float scrollY,
                            float zoom = 0.0f);
float findReadingPosition(const Settings& settings, const std::string& path);
// Per-document zoom, or 0 when the document has none stored
float findReadingZoom(const Settings& settings, const std::string& path);

// Start page recents ([Recent] section): record an open, or clear the list
void persistRecentFile(const std::string& path);
void clearRecentFiles();

// Theme changes persist immediately so new windows spawn in the same look
void persistThemeChoice(const App& app);
// The themes in use, for light and for dark, into settings by place and by
// name; and out of them, each where the theme of that name stands now
void settingsTakeThemes(Settings& settings, const App& app);
void settingsGiveThemes(const Settings& settings, int& current, int& light, int& dark);
// Editor mode pill choice (and the assists switch) persist immediately
void persistEditorMode(const App& app);
void persistWordCount(const App& app);
void persistCloseKeepsDrafts(const App& app);
// The view Markdown files open in (#322), and its name in settings.ini:
// reading, live or source
void persistOpenMode(const App& app);
const char* openModeName(int mode);
int openModeByName(const std::string& name);
// Print margins persist when picked in the preview (#257)
void persistPrintMargin(const App& app);
void persistSearchScopes(const App& app);
void persistOpenInTabs(const App& app);
void persistAlwaysOnTop(const App& app);

// The configuration home: the exe's folder in portable mode (a
// settings.ini beside tinta.exe opts in, #147), else %APPDATA%\Tinta
std::wstring tintaConfigDir();

// Resolve the actual file before handing it to an external editor, which
// does not share a Store/MSIX process's virtualized AppData view.
std::wstring configFilePathForShell(const std::wstring& path);

// Resolve the selected profile and custom [Keys] bindings.
void applyKeymap(App& app, const Settings& settings);
bool registerFileAssociation();
void openDefaultAppsSettings();
void askAndRegisterFileAssociation(Settings& settings);

#endif // TINTA_SETTINGS_H
