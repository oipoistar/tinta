#ifndef TINTA_RENDER_H
#define TINTA_RENDER_H

#include "app.h"
#include "mermaid_ext.h"

// Full synchronous layout of the whole document
void layoutDocument(App& app);

// Lays out from the top through ~2 viewports past the current scroll, then
// returns so the first frame can present. If blocks remain, layoutComplete is
// false and the caller posts WM_APP_LAYOUT_CHUNK to continue.
void layoutDocumentViewportFirst(App& app);

// Continues an incomplete layout for at most budgetUs. Returns true when done.
bool layoutDocumentContinue(App& app, int64_t budgetUs);

// Synchronously finishes any incomplete/dirty layout (search, TOC, End key)
void ensureLayoutComplete(App& app);

// UI-thread completion for WM_APP_IMAGE_READY: takes ownership of the
// AsyncImageResult, updates the image cache, and triggers a reflow
void completeAsyncImage(App& app, void* asyncResult);
void discardAsyncImage(void* asyncResult);

// Theme color for a diagram primitive's role (shared with the exporters)
D2D1_COLOR_F resolveDiagramRole(const App& app, const mermaidext::Prim& prim,
                                mermaidext::Role role);

// A list item's bullet as the page and Live draw it: a disc, a circle, then
// a square by how deep its list sits, and the next level round again (#315).
// It stands a little before the item's text at `textX`, halfway up the
// letters on `baseline`.
App::LayoutShape listBullet(float textX, float baseline, float em, int depth, D2D1_COLOR_F color);

// Under the pointer resting on a citation, the references it cites; on a
// reference to a figure or table, its caption (docs/specs/scholar.md).
// Drawn in document coordinates, as the front matter's overflow card is
void renderReferenceCard(App& app);

#endif // TINTA_RENDER_H
