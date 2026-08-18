#ifndef TINTA_UTILS_H
#define TINTA_UTILS_H

#include "app.h"
#include <string>
#include <string_view>

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
std::wstring_view textViewForRect(const App& app, const App::TextRect& tr);

// Word/line boundary helpers (offset-based word/line selection lives in
// selection.h)
bool isWordBoundary(wchar_t c);
const App::TextRect* findTextRectAt(const App& app, int x, int y);

void updateWindowTitle(App& app);
// Ctrl+N: spawns a second Tinta window on an untitled quick note
void launchQuickNoteWindow();
void openUrl(const std::string& url);
void copyToClipboard(HWND hwnd, const std::wstring& text);
void extractText(const ElementPtr& elem, std::wstring& out);

std::string slugifyHeading(const std::wstring& text);
void scrollToHeadingY(App& app, float headingY);
bool scrollToHeadingId(App& app, const std::string& id);
void handleLinkClick(App& app);

#endif // TINTA_UTILS_H
