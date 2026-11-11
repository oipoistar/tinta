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
// A face this PC lacks gives way to an installed one: for code the nearest
// of Cascadia Mono, Cascadia Code and Consolas, for text Segoe UI. Windows
// 10 has no Cascadia Code, and without the face DirectWrite drew every
// character from the fallback list, in no fixed pitch (#264).
const wchar_t* installedFace(App& app, const wchar_t* face, bool mono);
// The theme's body, heading and code faces as installedFace gives them; a
// heading face this PC lacks leaves the headings to the body's
void useInstalledFaces(App& app, D2DTheme& theme);
void createTypography(App& app);
bool createRenderTarget(App& app);
// Text is smoothed for the monitor the window is on: ClearType with that
// monitor's gamma, contrast and subpixel order, where it took the primary
// monitor's for every screen. createRenderTarget sets it up; this follows
// the window to another monitor, and with `reread` takes the monitor's
// values afresh (the displays or Windows' text settings changed). True when
// the target now draws with other values, so the window wants a repaint.
bool followMonitorText(App& app, bool reread = false);
// Where the above learns a window's monitor and a monitor's values; tests
// stand a second monitor in here
struct MonitorTextSource {
    HMONITOR (*monitorOf)(HWND window);
    HRESULT (*paramsOf)(IDWriteFactory* factory, HMONITOR monitor, IDWriteRenderingParams** params);
};
MonitorTextSource& monitorTextSource();

#endif // TINTA_D2D_INIT_H
