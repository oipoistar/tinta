#ifndef TINTA_MATH_RENDER_H
#define TINTA_MATH_RENDER_H

#include "app.h"

#include <memory>
#include <string>

// Native TeX-subset math rendering (#80). No MathJax, no KaTeX, no web
// engine: a recursive-descent parser builds a box tree (runs, scripts,
// fractions, stretchy delimiters, decorations), DirectWrite measures it,
// Direct2D draws it. Unsupported input returns null and the caller falls
// back to showing the raw source in code style - the mermaid pattern.

struct MathBox;
using MathBoxPtr = std::shared_ptr<MathBox>;

// Parse + lay out one equation at the given base font size. Display mode
// uses slightly more generous spacing. Results are cached per
// (source, size, display); the cache is cleared by mathClearCache.
MathBoxPtr mathParse(App& app, const std::wstring& latex, float fontSize,
                     bool display);

float mathBoxWidth(const MathBoxPtr& box);
float mathBoxHeight(const MathBoxPtr& box);
// Distance from the box top to the text baseline (for inline alignment)
float mathBoxBaseline(const MathBoxPtr& box);

// Draw with the box's top-left corner at (x, y) in the current target
void mathBoxDraw(App& app, const MathBoxPtr& box, float x, float y,
                 const D2D1_COLOR_F& color);

// Push the box's runs/rules/lines into the retained paint lists at (x, y).
// Run layouts are AddRef'd: the retained list releases its references on
// clearLayoutCache, the cached MathBox keeps its own.
void mathBoxRetain(App& app, const MathBoxPtr& box, float x, float y,
                   const D2D1_COLOR_F& color);

// Drop all cached boxes (zoom / theme font changed)
void mathClearCache();

// Standalone SVG markup for a laid-out box (HTML export): text runs at
// their measured positions, rules as rects, arrow strokes as lines
std::string mathBoxSvg(const MathBoxPtr& box, const std::string& colorCss,
                       const std::string& fontFamilyCss);

#endif // TINTA_MATH_RENDER_H
