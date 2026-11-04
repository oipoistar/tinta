#pragma once
// Per-element styles (docs/specs/theming.md, phase 2). Layout carries a
// style context down the page: each block it lays out, and each span
// inside one, asks the active CSS theme what it says about that element.
// A property draws from the sheet only where the theme set it, on the
// element or, for an inherited property, above it; everything else draws
// as it always has. Tinta's own look sits under the theme as a base sheet
// (heading sizes, bold, code faces), so a size or colour the theme gives
// the page reaches only the elements a browser would let it reach.
//
// Without a CSS theme no style is ever asked for: the built-in and
// themes.ini themes draw exactly as before.
#include "css.h"

#include <d2d1.h>
#include <dwrite.h>

#include <bitset>
#include <memory>
#include <string>
#include <vector>

struct App;
namespace qmd { struct Element; }

namespace page {

using qmd::Element;

using Bits = std::bitset<(size_t)css::Prop::Count>;

// One element's style as layout reads it
struct Style {
    const css::ComputedStyle* computed = nullptr;  // null: no CSS theme
    Bits set;                                      // what the theme decides for it
    css::Engine::Context context = 0;

    bool has(css::Prop prop) const { return computed && set.test((size_t)prop); }
    explicit operator bool() const { return computed != nullptr; }
    const css::ComputedStyle* operator->() const { return computed; }
    const css::ComputedStyle& operator*() const { return *computed; }
};

// Readies the theme's sheet, and the document's folder sheet, for a layout
// of the page, `width` the reading area's width in CSS pixels for
// (min-width) and (max-width)
void begin(App& app, float width);
// The folder sheet styling a document: tinta.css in its folder or a
// parent, empty when there is none; looked up again after two seconds
std::wstring folderSheetOf(App& app, const std::string& document);
// The folder sheet as CSS, for the HTML export; empty when none applies
std::string folderSheetCss(App& app);
// Whether the folder sheet is turned off for the open document, and turns it on or off
bool folderSheetOff(const App& app);
void setFolderSheetOff(App& app, bool off);
// A CSS theme styles this layout
bool active(const App& app);
// The style of the block being laid out (empty without a CSS theme)
const Style& current(const App& app);
// The page itself, body
const Style& body(const App& app);

// Enters a block for its layout and leaves it at the end of the scope. An
// element is named as the HTML export writes it (p, h2, blockquote.alert-note,
// li.task...), with its place among its siblings for :first-child and +
class Block {
public:
    Block(App& app, const Element* element);
    // A block with no element of its own (thead, tbody, a row or cell), or
    // one placed by hand among siblings described in `previous`
    Block(App& app, const css::Element& element, const css::Element* previous = nullptr, size_t count = 0,
          const Element* children = nullptr);
    ~Block();
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    const Style& style() const;

private:
    App& app_;
    bool pushed_ = false;
};

// How an element is named in the sheet; false for text and breaks
bool describe(const Element& element, css::Element& out);

// An inline element under a style (strong, em, code, a, mark...), or a
// part of a block placed among siblings described in `previous` (a
// table's rows and cells)
Style child(App& app, const Style& parent, const css::Element& element, const css::Element* previous = nullptr,
            size_t count = 0);
Style child(App& app, const Style& parent, const Element& element);
// A pseudo-element: ::marker, ::before, ::after; null when no rule styles it
const css::ComputedStyle* pseudo(App& app, const Style& style, css::Pseudo which);
// A pseudo-element's style as layout reads it; empty when no rule styles it
Style pseudoStyle(App& app, const Style& style, css::Pseudo which);
// The text ::before or ::after gives an element (strings, counter(),
// counters(), attr()), empty when it has none. `count` applies the
// pseudo-element's own counter-reset and counter-increment first, as a
// block laid out once in the page's order does
std::wstring generated(App& app, const Style& style, css::Pseudo which, bool count);
// The marker text a list-style-type gives the nth item ("3", "c", "iii")
std::wstring counterText(int n, css::ListStyle style);

// Vertical space in layout units. A block's margin-top collapses with the
// margin above it as CSS's do; without one, the renderer's own space (in
// CSS pixels, scaled here) is added as before
float before(App& app, float fallback);
// Below a block whose content just ended
float after(App& app, float fallback);
// Below a block whose children carried their own margins (a quote, a list)
float afterContainer(App& app, float fallback);
// Content or a border between margins: nothing collapses across it
void boundary(App& app);

// CSS pixels to layout units
float px(const App& app, float css);
D2D1_COLOR_F color(const css::Color& c);
// The style's colour when the theme set it, else `fallback`
D2D1_COLOR_F textColor(const Style& style, D2D1_COLOR_F fallback);

// The text format for a style: `fallback` changed in what the theme set
// for the font (family, size, weight, style, stretch), or `fallback` itself
IDWriteTextFormat* format(App& app, const Style& style, IDWriteTextFormat* fallback);
// Bold or italic of a format made here; null for Tinta's own formats
IDWriteTextFormat* variant(App& app, IDWriteTextFormat* base, bool bold, bool italic);
// The OpenType features a style turns on or off, on top of `fallback`'s
IDWriteTypography* typography(App& app, const Style& style, IDWriteTypography* fallback);
// The line height for text in `format`, when the theme set one; else `fallback`
float lineHeight(App& app, const Style& style, IDWriteTextFormat* format, float fallback);
// Letter spacing in layout units, 0 when not set
float letterSpacing(App& app, const Style& style);
// text-transform applied, keeping the text's length
std::wstring transform(std::wstring text, const Style& style);

// Text formats made for styles go when the theme's faces or the zoom change
void releaseFormats(App& app);

// The inspector's record of a layout (inspector.h): each block and run of
// text, where it was drawn and the style it was drawn in, with its path
// from the page down
struct Inspected {
    D2D1_RECT_F bounds{};
    css::Engine::Context context = 0;
    std::vector<css::Element> path;  // body first
    const Element* element = nullptr;
    bool block = true;
};
// Records the next layout (the record lasts until a layout without it)
void record(App& app, bool on);
bool recording(const App& app);
void recordBlock(App& app, const D2D1_RECT_F& bounds);
void recordRun(App& app, const D2D1_RECT_F& bounds, const Style& style, const Element* leaf);
const std::vector<Inspected>& recorded(const App& app);
// The rules behind an element's style, in cascade order (css::Engine::explain)
std::vector<css::Applied> explain(App& app, css::Engine::Context context);
// The style a context computed to; null without a CSS theme
const css::ComputedStyle* computedStyle(App& app, css::Engine::Context context);
// An element's style outside layout, by its tags from the page down
// ({"blockquote", "p"}), as its first of each; empty without a CSS sheet.
// Live reads its code, list and quote looks from it
Style lookup(App& app, std::initializer_list<const char*> tags);

}  // namespace page
