// Small drawing helpers shared by sheets, the Live editor and its text:
// colours, text formats made once, layouts and rounded shapes on the
// window's target.
#pragma once

#include "app.h"
#include "ui_scale.h"

#include <d2d1.h>
#include <dwrite.h>

#include <string>

namespace paint {

D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t);
D2D1_COLOR_F alpha(D2D1_COLOR_F c, float a);

// Text formats by face, size and weight, kept for the factory's life (a
// format's address names it for layouts cached on it); `wrap` false keeps
// to one line and trims with an ellipsis
IDWriteTextFormat* textFormat(App& app, const wchar_t* family, float px, DWRITE_FONT_WEIGHT weight, bool wrap = true);
const wchar_t* uiFace();
IDWriteTextLayout* makeLayout(App& app, const std::wstring& text, IDWriteTextFormat* fmt, float maxW, float maxH);
DWRITE_TEXT_METRICS metrics(IDWriteTextLayout* layout);
void drawText(App& app, IDWriteTextLayout* layout, float x, float y, D2D1_COLOR_F color);

void fillRound(App& app, const D2D1_RECT_F& r, float radius, D2D1_COLOR_F color);
void strokeRound(App& app, const D2D1_RECT_F& r, float radius, D2D1_COLOR_F color, float width,
                 ID2D1StrokeStyle* style = nullptr);
void line(App& app, D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F color, float width, ID2D1StrokeStyle* style = nullptr);
ID2D1StrokeStyle* dashedStyle(App& app);
ID2D1StrokeStyle* roundStyle(App& app);
// A soft shadow under something raised: two widening, fading rounded rectangles
void shadow(App& app, const D2D1_RECT_F& r, float radius, D2D1_COLOR_F color, float strength);

// A raised surface on the theme: a menu, a popup, a bar. A CSS theme's
// --ui-* colours apply; pageSurface keeps the page's, for bars whose ink is
// the page's own (a sheet's canvas)
struct Surface {
    D2D1_COLOR_F fill, border, text, muted, accent, shadow;
};
Surface surface(const App& app);
Surface pageSurface(const App& app);

// --- Pieces every surface on the interface's scale draws with (ui_scale.h)

// One line of interface text in a type role, trimmed with an ellipsis
void label(App& app, const wchar_t* text, uiscale::Type role, const D2D1_RECT_F& box, D2D1_COLOR_F color,
           DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
float labelWidth(App& app, const wchar_t* text, uiscale::Type role);
// Where a line of `role` goes to stand centred in a box `height` high
float lineTop(const App& app, float top, float height, uiscale::Type role);
// A section's label: Micro, upper case, a little air between the letters
void sectionLabel(App& app, const wchar_t* text, float x, float y, float maxW, D2D1_COLOR_F color);
// The interface's text colour at a share of its strength
D2D1_COLOR_F ink(const D2DTheme& theme, float share, float alpha = 1.0f);
// A switch, 40 by 20, at the right of `slot` and centred in its height.
// Returns where it stands.
D2D1_RECT_F toggle(App& app, const D2D1_RECT_F& slot, bool on, const D2DTheme& theme, float alpha);
// A dialog's close cross in its box
void closeCross(App& app, const D2D1_RECT_F& box, D2D1_COLOR_F color);

}  // namespace paint
