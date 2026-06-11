#ifndef TINTA_RENDER_H
#define TINTA_RENDER_H

#include "app.h"

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

#endif // TINTA_RENDER_H
