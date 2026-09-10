#pragma once
#include "app.h"

enum FrontmatterAction {
    FM_HIDDEN=3000, FM_SHOWN, FM_OTHER,
    FM_PICK=7000, FM_LIMIT_DOWN=7100, FM_LIMIT_UP,
    FM_ROW=10000 // row * 8 + drag, show, left, right, label, format
};
void persistFrontmatter(const App& app);
void observeFrontmatter(App& app, const std::vector<fm::Property>& properties);
std::string frontmatterSeenKey(const App& app);
void renderFrontmatterSettings(App& app, const D2D1_RECT_F& area);
bool frontmatterAction(App& app, int action);
bool frontmatterMouseDown(App& app, float x, float y);
void frontmatterMouseMove(App& app, float x, float y);
bool frontmatterMouseUp(App& app, float x, float y);
void frontmatterScroll(App& app, float delta);
// Same layout for the document and the settings preview.
float layoutFrontmatterStrip(App& app, const std::vector<fm::Property>& properties,
                             float x, float y, float width, float scale, bool preview=false);
void renderFrontmatterOverflow(App& app);
