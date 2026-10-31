// Small drawing helpers shared by sheets, the Live editor and its text:
// colours, text formats made once, layouts and rounded shapes on the
// window's target.
#pragma once

#include "app.h"

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

// A raised surface on the theme: a menu, a popup, a bar
struct Surface {
    D2D1_COLOR_F fill, border, text, muted, accent, shadow;
};
Surface surface(const App& app);

}  // namespace paint
