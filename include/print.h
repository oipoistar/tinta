#ifndef TINTA_PRINT_H
#define TINTA_PRINT_H

#include "app.h"

// Prints the current document through the Windows print pipeline
// (ID2D1PrintControl). The user picks any installed printer — including
// "Microsoft Print to PDF" — from the standard dialog. Rendering uses a
// dedicated light print palette regardless of the active theme.
bool printDocument(App& app);

// Print preview overlay: Ctrl+P opens it, the user flips through the
// paginated pages, then confirms into the printer dialog or cancels.
// Pages are previewed at the default printer's paper size (A4 fallback);
// printDocument re-paginates for whatever printer is finally chosen.
void openPrintPreview(App& app, HWND hwnd);
void closePrintPreview(App& app, HWND hwnd);
void printPreviewSetPage(App& app, int page);   // clamps and re-rasterizes
void printPreviewConfirm(App& app, HWND hwnd);  // close overlay, run the dialog

// Debug/testing: renders each paginated page to <outDir>\page-N.png using
// the same layout and pagination as printing. Returns the page count.
int printDebugPages(App& app, const std::wstring& outDir);

#endif // TINTA_PRINT_H
