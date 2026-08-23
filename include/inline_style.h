#ifndef TINTA_INLINE_STYLE_H
#define TINTA_INLINE_STYLE_H

#include "app.h"

#include <string>
#include <vector>

// --- Nested inline spans ---
//
// Inline spans nest: `code` inside **bold**, a [link](x) inside *emphasis*.
// Flattening the tree into leaves that each carry the merged style of every
// span above them keeps the drawing loop flat, and nothing gets dropped on
// the way down.

struct InlineStyle {
    IDWriteTextFormat* format = nullptr;
    D2D1_COLOR_F color{};
    std::string linkUrl;
    bool isLink = false;
    bool hasBg = false;
    D2D1_COLOR_F bgColor{};
    bool hasStrike = false;
    float drawYOffset = 0.0f;
    bool bold = false;
    bool italic = false;
    bool fixedSize = false;  // ^sup^/~sub~ own their font size
    int fileRefBadge = 0;    // local .md reference: 1 = live, 2 = broken (#127)
};

struct StyledRun {
    ElementPtr elem;
    InlineStyle style;
};

// Body font face for a weight/style combination, falling back to base
IDWriteTextFormat* emphasisFormat(App& app, IDWriteTextFormat* base, bool bold, bool italic);

// Monospace face for an inline `code` span inside emphasis
IDWriteTextFormat* inlineCodeFormat(App& app, bool bold, bool italic);

// Appends one StyledRun per drawable leaf (Text, Code, Image, Ruby, breaks),
// each carrying the merged style of every span it sits inside
void flattenInline(App& app, const std::vector<ElementPtr>& elements,
                   const InlineStyle& inherited, float lineHeight,
                   std::vector<StyledRun>& out);

#endif // TINTA_INLINE_STYLE_H
