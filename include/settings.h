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
bool registerFileAssociation();
void openDefaultAppsSettings();
void askAndRegisterFileAssociation(Settings& settings);

#endif // TINTA_SETTINGS_H
