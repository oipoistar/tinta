#ifndef TINTA_INLINE_STYLE_H
#define TINTA_INLINE_STYLE_H

#include "app.h"
#include "page_style.h"

#include <optional>
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
    std::string anchorId;
    bool isLink = false;
    bool hasBg = false;
    D2D1_COLOR_F bgColor{};
    bool hasStrike = false;
    bool hasUnderline = false;  // <ins> or <u> (#263)
    // Where the underline began: its rule sits under that face's baseline,
    // so <ins>x<sup>2</sup></ins> keeps one rule while <sup><ins>2</ins></sup>
    // gets its own under the 2
    IDWriteTextFormat* underlineFormat = nullptr;
    float underlineYOffset = 0.0f;
    float drawYOffset = 0.0f;
    bool bold = false;
    bool italic = false;
    bool fixedSize = false;  // ^sup^/~sub~ own their font size

    // A CSS theme's style for the span (page_style.h); empty without one
    page::Style css;
    // A link's rule as the theme sets it: -1 Tinta's own, 0 none, 1 drawn
    int8_t linkRule = -1;
    std::optional<D2D1_COLOR_F> ruleColor;  // text-decoration-color
    // Inline code the theme styles: its face, colour, box and padding
    IDWriteTextFormat* codeFormat = nullptr;
    std::optional<D2D1_COLOR_F> codeColor, codeBox, codeEdge;
    float codePadLeft = -1.0f, codePadRight = -1.0f;  // layout units; -1 Tinta's
    float codeRadius = 0.0f, codeEdgeWidth = 0.0f;
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
