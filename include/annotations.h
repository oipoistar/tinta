#ifndef TINTA_ANNOTATIONS_H
#define TINTA_ANNOTATIONS_H

// Review annotations (#126 experiment): notes live as HTML comments in the
// raw markdown -- `<!-- review "anchor quote": note text -->` on its own
// line after the annotated block. The parser already hides HTML comments,
// so nothing renders; Tinta re-finds the quote in the laid-out text to
// tint it and to place a marker square on a rail beside the scrollbar.

#include "app.h"
#include <windows.h>

// Rebuild app.annotations from app.sourceText (call after every (re)load)
void annotationsParseSource(App& app);

// Tinted anchor text + rail squares with leader lines + hover preview.
// Screen-space pass: call after the document transform is reset.
void renderAnnotations(App& app);

// Modal note editor overlay; drawn with the other overlays
void renderAnnotationEditor(App& app);

// Annotation whose tinted text covers the document point, or -1
int annotationAtDocPoint(App& app, float docX, float docY);
// Rail square under the screen point, or -1
int annotationRailHit(const App& app, float x, float y);
bool annotationCopyButtonHit(const App& app, float x, float y);

void annotationOpenEditor(App& app, int index);
// From the current selection; false when the selection can't be located
// in the source (nothing opens in that case)
bool annotationBeginCreate(App& app);
void annotationEditorConfirm(App& app, HWND hwnd);
void annotationEditorCancel(App& app);
void annotationEditorDelete(App& app, HWND hwnd);

// True when the key/char was consumed by the open editor
bool annotationEditorKeyDown(App& app, HWND hwnd, WPARAM key, bool ctrl);
bool annotationEditorChar(App& app, WPARAM ch);
// 0 = none, 1 = confirm, 2 = cancel, 3 = delete
int annotationEditorButtonAt(const App& app, float x, float y);
bool annotationEditorContains(const App& app, float x, float y);

// Clipboard: full path on the first line, then one remark per annotation
void annotationsCopyForAgent(App& app, HWND hwnd);

#endif // TINTA_ANNOTATIONS_H
