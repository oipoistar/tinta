#ifndef TINTA_SEARCH_H
#define TINTA_SEARCH_H

#include "app.h"

void recordSearchMatchPositions(App& app, size_t segStart, size_t segEnd, float lineY);
void performSearch(App& app);
void scrollToCurrentMatch(App& app);

#endif // TINTA_SEARCH_H
