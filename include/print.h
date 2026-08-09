#ifndef TINTA_PRINT_H
#define TINTA_PRINT_H

#include "app.h"

// Prints the current document through the Windows print pipeline
// (ID2D1PrintControl). The user picks any installed printer — including
// "Microsoft Print to PDF" — from the standard dialog. Rendering uses a
// dedicated light print palette regardless of the active theme.
bool printDocument(App& app);

// Debug/testing: renders each paginated page to <outDir>\page-N.png using
// the same layout and pagination as printing. Returns the page count.
int printDebugPages(App& app, const std::wstring& outDir);

#endif // TINTA_PRINT_H
