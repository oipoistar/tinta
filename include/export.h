#ifndef TINTA_EXPORT_H
#define TINTA_EXPORT_H

#include "app.h"
#include "mermaid_ext.h"
#include <windows.h>
#include <string>

// "Export as..." (#export_as): a save dialog whose file-type filter picks
// the format. HTML is generated directly from the element tree with the
// active theme inlined; diagrams and math become standalone SVG. DOCX packs
// OOXML into a stored zip, with diagrams and math rasterized to PNG through
// the same primitives.

// Opens the dialog and dispatches on the chosen filter/extension
void exportDocumentAs(App& app, HWND hwnd);

// Direct writers (also callable from tests/tools)
bool exportHtmlFile(App& app, const std::wstring& path);
bool exportDocxFile(App& app, const std::wstring& path);
// PDF rides the print pipeline into the Microsoft Print to PDF driver with
// the job output redirected to the file: vector pages, embedded fonts, no
// dialog (print.cpp)
bool exportPdfFile(App& app, const std::wstring& path);

// Any native diagram source -> prims at the given scale; flowcharts go
// through their export mirror, other families through mermaidext. ok=false
// means the caller should show the source instead. (export_html.cpp)
mermaidext::Built buildDiagramPrims(App& app, const std::string& sourceUtf8,
                                    float scale);
mermaidext::Built buildFlowchartPrims(App& app, const std::string& sourceUtf8,
                                      float scale);

#endif  // TINTA_EXPORT_H
