#include "utils.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>

std::wstring toWide(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &result[0], len);
    return result;
}

float measureText(App& app, const std::wstring& text, IDWriteTextFormat* format) {
    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.length(),
        format, 10000.0f, 100.0f, &layout);
    if (!layout) return 0;

    DWRITE_TEXT_METRICS metrics;
    layout->GetMetrics(&metrics);
    layout->Release();
    return metrics.widthIncludingTrailingWhitespace;
}

std::wstring toLower(const std::wstring& str) {
    std::wstring result = str;
    for (auto& c : result) {
        c = towlower(c);
    }
    return result;
}

bool isWordBoundary(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' ||
           c == L'.' || c == L',' || c == L';' || c == L':' ||
           c == L'!' || c == L'?' || c == L'"' || c == L'\'' ||
           c == L'(' || c == L')' || c == L'[' || c == L']' ||
           c == L'{' || c == L'}' || c == L'<' || c == L'>' ||
           c == L'/' || c == L'\\' || c == L'-' || c == L'=' ||
           c == L'+' || c == L'*' || c == L'&' || c == L'|';
}

const App::TextRect* findTextRectAt(const App& app, int x, int y) {
    for (const auto& tr : app.textRects) {
        if (x >= tr.rect.left && x <= tr.rect.right &&
            y >= tr.rect.top && y <= tr.rect.bottom) {
            return &tr;
        }
    }
    return nullptr;
}

bool findWordBoundsAt(const App& app, const App::TextRect& tr, int x,
                      float& wordLeft, float& wordRight) {
    (void)app;
    if (tr.text.empty()) return false;

    float totalWidth = tr.rect.right - tr.rect.left;
    float charWidth = totalWidth / (float)tr.text.length();

    // Find which character was clicked
    int charIndex = (int)((x - tr.rect.left) / charWidth);
    charIndex = std::max(0, std::min(charIndex, (int)tr.text.length() - 1));

    // Find word start (scan left)
    int wordStart = charIndex;
    while (wordStart > 0 && !isWordBoundary(tr.text[wordStart - 1])) {
        wordStart--;
    }

    // Find word end (scan right)
    int wordEnd = charIndex;
    while (wordEnd < (int)tr.text.length() - 1 && !isWordBoundary(tr.text[wordEnd + 1])) {
        wordEnd++;
    }

    wordLeft = tr.rect.left + wordStart * charWidth;
    wordRight = tr.rect.left + (wordEnd + 1) * charWidth;
    return true;
}

void findLineRects(const App& app, float y, float& lineLeft, float& lineRight,
                   float& lineTop, float& lineBottom) {
    lineLeft = 99999.0f;
    lineRight = 0.0f;
    lineTop = 0.0f;
    lineBottom = 0.0f;
    bool found = false;

    for (const auto& tr : app.textRects) {
        float centerY = (tr.rect.top + tr.rect.bottom) / 2.0f;
        if (std::abs(centerY - y) < 20) {  // Same line if within 20px
            if (!found) {
                lineTop = tr.rect.top;
                lineBottom = tr.rect.bottom;
                found = true;
            }
            lineLeft = std::min(lineLeft, tr.rect.left);
            lineRight = std::max(lineRight, tr.rect.right);
            lineTop = std::min(lineTop, tr.rect.top);
            lineBottom = std::max(lineBottom, tr.rect.bottom);
        }
    }
}

void openUrl(const std::string& url) {
    if (!url.empty()) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void copyToClipboard(HWND hwnd, const std::wstring& text) {
    if (text.empty()) return;

    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();

    size_t size = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hMem) {
        wchar_t* dest = (wchar_t*)GlobalLock(hMem);
        if (dest) {
            memcpy(dest, text.c_str(), size);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }

    CloseClipboard();
}

void extractText(const ElementPtr& elem, std::wstring& out) {
    if (!elem) return;

    switch (elem->type) {
        case ElementType::Text:
            out += toWide(elem->text);
            break;
        case ElementType::SoftBreak:
            out += L" ";
            break;
        case ElementType::HardBreak:
            out += L"\n";
            break;
        case ElementType::Paragraph:
        case ElementType::Heading:
        case ElementType::ListItem:
            for (const auto& child : elem->children) {
                extractText(child, out);
            }
            out += L"\n\n";
            break;
        case ElementType::CodeBlock: {
            out += L"\n";
            for (const auto& child : elem->children) {
                if (child->type == ElementType::Text) {
                    out += toWide(child->text);
                }
            }
            out += L"\n\n";
            break;
        }
        default:
            for (const auto& child : elem->children) {
                extractText(child, out);
            }
            break;
    }
}
