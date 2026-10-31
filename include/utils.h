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
// Opens the clipboard, trying again for up to about 50 ms: Remote Desktop's
// clipboard sync, clipboard history and clipboard managers hold it open for
// a moment after each change. held is set when it stayed held.
bool openClipboard(HWND hwnd, bool* held = nullptr);
// A copy that found the clipboard held for longer copied nothing: a
// warning says so (warnings show with notifications off too)
void signalClipboardBusy(App& app);
bool copyToClipboard(HWND hwnd, const std::wstring& text, bool* held = nullptr);
// The clipboard's text, or nothing when it holds none or stays held
std::wstring readClipboardText(HWND hwnd);
// Plain text plus an HTML fragment ("HTML Format") for rich paste targets
bool copyRichTextToClipboard(HWND hwnd, const std::wstring& text,
                             const std::wstring& htmlFragment, bool* held = nullptr);
// The CF_HTML payload (UTF-8, with its offset header) for a fragment
std::string clipboardHtml(const std::wstring& htmlFragment);
// Outlook's compose font (its ComposeFontComplex style) as CSS that only
// Word-based editors read: a paste into Outlook matches the mail around
// it, and web mail keeps its own font
std::string composeFontCss(const std::string& style);
// What a code block's Copy button puts on the clipboard: its lines without
// the line break that ends the last one (#269)
std::wstring codeCopyText(const std::wstring& code);
// Resolve UTF-8 document paths without a MAX_PATH-sized buffer (#203).
std::wstring absoluteFilePath(const std::string& path);
void copyFilePath(App& app, HWND hwnd, const std::string& path);
void extractText(const ElementPtr& elem, std::wstring& out);

std::string slugifyHeading(const std::wstring& text);
void scrollToHeadingY(App& app, float headingY);
// Document Y of an in-document link target: a footnote, a heading slug, or
// an <a id>/<a name> anchor (#255)
bool documentTargetY(App& app, const std::string& id, float& y);
bool scrollToHeadingId(App& app, const std::string& id);
void handleLinkClick(App& app);

#endif // TINTA_UTILS_H
