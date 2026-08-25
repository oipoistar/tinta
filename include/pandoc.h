#pragma once

#include "app.h"

// Optional pandoc bridge: when a pandoc executable can be found (or the
// user points Tinta at one), the editor rail grows an export button whose
// flyout offers the formats our native exporters don't cover - EPUB, ODT,
// PPTX, RTF and LaTeX. Rich formats are fed Tinta's own HTML export so
// diagrams and math survive as SVG; LaTeX gets the raw markdown.

// Number of formats in the flyout (ids 150..150+N-1 in editCtxSubHits)
inline constexpr int PANDOC_FORMAT_COUNT = 5;

// Row label + extension hint for the flyout (index = format id - 150)
const wchar_t* pandocFormatLabel(int fmt);
const wchar_t* pandocFormatExt(int fmt);

// Resolve the executable once per session: the settings override first,
// then PATH, then the common install locations. Cheap after the first call.
void pandocResolve(App& app);
bool pandocAvailable(App& app);

// The user picked an executable in settings: remember and re-resolve
void pandocSetUserPath(App& app, const std::wstring& path);

// Save dialog for the chosen format, then convert on a worker thread;
// completion arrives as WM_APP_PANDOC_DONE (wParam = success)
void pandocExportFlow(App& app, HWND hwnd, int fmt);
