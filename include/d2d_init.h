#ifndef TINTA_D2D_INIT_H
#define TINTA_D2D_INIT_H

#include "app.h"

bool initD2D(App& app);
void applyTheme(App& app, int themeIndex);
// Tints the native title bar/border to the current theme (Windows 11;
// Windows 10 falls back to the stock dark/light caption)
void applyWindowChrome(App& app);
void updateTextFormats(App& app);
void enumerateSystemFontFamilies(App& app);
void updateOverlayFormats(App& app);
void ensureThemePreviewFormats(App& app);
void createTypography(App& app);
bool createRenderTarget(App& app);

#endif // TINTA_D2D_INIT_H
