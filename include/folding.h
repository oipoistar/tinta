#pragma once

#include "app.h"

#include <string>

// Folding sections in the reading view (#316). The arrow beside a heading
// of the page's top level hides the blocks after it, up to the next
// heading of its level or above; the footnotes stay. Folded blocks are not
// laid out, but their headings are still counted at the folded heading's
// place, so slugs, Contents and anchor links read as if they were open.
// The editor, its preview and print show every section.

// Whether this layout folds: the setting is on and the page is read
bool foldingActive(const App& app);

// A heading's state, by its slug: foldAll, flipped when foldToggled names it
bool headingFolded(const App& app, const std::string& id);
void setHeadingFolded(App& app, const std::string& id, bool folded);
void toggleFold(App& app, const std::string& id);
// Every section folded, or none (the context menu's Fold/Unfold all)
void foldAllSections(App& app, bool folded);
// A section of the current layout is folded
bool anySectionFolded(const App& app);

// The document being left keeps its folds for the session; one opening
// takes its own, or the setting's. A document opening folded starts at its
// top: a reading position from the open page would land elsewhere.
void foldsLeave(App& app);
void foldsEnter(App& app);

// Where a heading is, opening the folded sections around it first
float headingTargetY(App& app, size_t heading);
// Opens the folded sections whose hidden text holds the lowercase query,
// and those inside them that do, so a search finds it. True when any opened.
bool unfoldSectionsMatching(App& app, const std::wstring& queryLower);

// The fold arrow whose box holds the document point, else -1
int foldArrowAt(const App& app, float docX, float docY);
// The fold arrow whose heading line holds the document point, else -1
int foldLineAt(const App& app, float docX, float docY);
// Folds or opens the arrow's section
void foldArrowClick(App& app, int arrow);

// Draws the arrows of folded headings, and of the heading under the mouse,
// in the page's coordinates as the frame draws them
void renderFoldArrows(App& app, float viewportTop, float viewportBottom);
