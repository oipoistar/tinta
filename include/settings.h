#ifndef TINTA_SETTINGS_H
#define TINTA_SETTINGS_H

#include "app.h"

std::wstring getSettingsPath();
void saveSettings(const Settings& settings);
Settings loadSettings();
bool registerFileAssociation();
void openDefaultAppsSettings();
void askAndRegisterFileAssociation(Settings& settings);

#endif // TINTA_SETTINGS_H
