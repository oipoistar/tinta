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
// Interface text takes the fonts of a Chinese, Japanese or Korean interface
// language (#254); for other languages these leave the system fallback.
// The shared overlay formats already carry it; formats made on the spot
// and layouts of interface text in a document format need it set.
void useUiFontFallback(const App& app, IDWriteTextFormat* format);
void useUiFontFallback(const App& app, IDWriteTextLayout* layout);
// The face a document draws CJK ideographs in when set in family: the
// family itself when it has them, else the first installed CJK face of the
// document fallback. Word export names it as the East Asian font (#256).
std::wstring documentCjkFamily(App& app, const wchar_t* family);
void createTypography(App& app);
bool createRenderTarget(App& app);

#endif // TINTA_D2D_INIT_H
