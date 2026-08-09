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

// Paper formats offered by the preview. The choice is preset into the
// printer dialog's DEVMODE, and the dialog's final answer still wins.
struct PrintPaper {
    const wchar_t* name;
    float w, h;      // portrait DIPs at 96/inch
    short dmPaper;   // DMPAPER_* constant
};
inline constexpr PrintPaper PRINT_PAPERS[] = {
    {L"A4",     794.0f,  1123.0f, 9},   // DMPAPER_A4
    {L"Letter", 816.0f,  1056.0f, 1},   // DMPAPER_LETTER
    {L"Legal",  816.0f,  1344.0f, 5},   // DMPAPER_LEGAL
    {L"A3",     1123.0f, 1587.0f, 8},   // DMPAPER_A3
};
inline constexpr int PRINT_PAPER_COUNT = 4;

// Re-paginates the open preview for a new paper size / orientation
void printPreviewSetFormat(App& app, int paper, bool landscape);

// Debug/testing: renders each paginated page to <outDir>\page-N.png using
// the same layout and pagination as printing. Returns the page count.
int printDebugPages(App& app, const std::wstring& outDir);

#endif // TINTA_PRINT_H
