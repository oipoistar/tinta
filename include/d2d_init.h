#ifndef TINTA_D2D_INIT_H
#define TINTA_D2D_INIT_H

#include "app.h"

bool initD2D(App& app);
void applyTheme(App& app, int themeIndex);
void applyLanguage(App& app, int langIndex);
void updateTextFormats(App& app);
void updateOverlayFormats(App& app);
void ensureThemePreviewFormats(App& app);
void ensureLanguageFormats(App& app);
void createTypography(App& app);
bool createRenderTarget(App& app);

#endif // TINTA_D2D_INIT_H
