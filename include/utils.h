#ifndef TINTA_UTILS_H
#define TINTA_UTILS_H

#include "app.h"
#include <string>

// Simple inline element rendering
struct InlineSpan {
    std::wstring text;
    D2D1_COLOR_F color;
    IDWriteTextFormat* format;
    std::string linkUrl;
    bool underline;
};

std::wstring toWide(const std::string& str);
float measureText(App& app, const std::wstring& text, IDWriteTextFormat* format);
std::wstring toLower(const std::wstring& str);

// Word/line boundary helpers
bool isWordBoundary(wchar_t c);
const App::TextRect* findTextRectAt(const App& app, int x, int y);
bool findWordBoundsAt(const App& app, const App::TextRect& tr, int x,
                      float& wordLeft, float& wordRight);
void findLineRects(const App& app, float y, float& lineLeft, float& lineRight,
                   float& lineTop, float& lineBottom);

void openUrl(const std::string& url);
void copyToClipboard(HWND hwnd, const std::wstring& text);
void extractText(const ElementPtr& elem, std::wstring& out);

#endif // TINTA_UTILS_H
