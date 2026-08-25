#pragma once

#include "app.h"

// Start page (design t7): a quiet launcher replacing the tutorial sample
// document on bare launches. Recents are the hero, the sample document
// and tutorials live on as three Learn cards, the whole window stays a
// drop target, and everything on the page carries its key.

// True while the launcher should draw instead of a document
bool startPageActive(const App& app);

// Reloads app.startPageRecents from settings.ini; called on every
// appearance transition so the list is always current
void startPageRefreshRecents(App& app);

// Lazily creates the shared hero icon bitmap (also used by the edit rail)
void startPageEnsureIcon(App& app);

// Draws the page and rebuilds app.startPageHits (screen coordinates)
void renderStartPage(App& app);

// Hit id at a client-space point, 0 for none. Ids: 1 open, 2 new
// document, 3 browse, 4 clear recents, 5 all shortcuts, 10+i recent row,
// 20+i learn card.
int startPageHitAt(const App& app, float x, float y);

// Opens one of the embedded Learn documents (0 sample, 1 mermaid,
// 2 markdown basics) into this window
void startPageOpenEmbedded(App& app, HWND hwnd, int card);

// The embedded sample document (the old bare-launch tutorial), still
// used as the fallback when a requested file cannot be loaded
const char* startPageSampleDoc();
