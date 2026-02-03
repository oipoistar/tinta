#ifndef TINTA_RENDER_H
#define TINTA_RENDER_H

#include "app.h"

void renderInlineContent(App& app, const std::vector<ElementPtr>& elements,
                         float startX, float& y, float maxWidth,
                         IDWriteTextFormat* baseFormat, D2D1_COLOR_F baseColor,
                         const std::string& baseLinkUrl = "", float customLineHeight = 0);

void renderParagraph(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);
void renderHeading(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);
void renderCodeBlock(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);
void renderBlockquote(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);
void renderList(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);
void renderHorizontalRule(App& app, float& y, float indent, float maxWidth);
void renderElement(App& app, const ElementPtr& elem, float& y, float indent, float maxWidth);

#endif // TINTA_RENDER_H
