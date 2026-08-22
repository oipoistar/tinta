#ifndef TINTA_EXPORT_H
#define TINTA_EXPORT_H

#include "app.h"
#include <windows.h>
#include <string>

// "Export as..." (#export_as): a save dialog whose file-type filter picks
// the format. HTML is generated directly from the element tree with the
// active theme inlined; diagrams and math become standalone SVG.

// Opens the dialog and dispatches on the chosen filter/extension
void exportDocumentAs(App& app, HWND hwnd);

// Direct writers (also callable from tests/tools)
bool exportHtmlFile(App& app, const std::wstring& path);

#endif  // TINTA_EXPORT_H
