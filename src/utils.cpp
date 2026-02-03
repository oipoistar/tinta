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

static const App::LineBucket* findLineBucketAt(const App& app, float y) {
    if (app.lineBuckets.empty()) return nullptr;

    int lo = 0;
    int hi = (int)app.lineBuckets.size() - 1;
    const float tolerance = 5.0f;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const auto& line = app.lineBuckets[mid];
        if (y < line.top - tolerance) {
            hi = mid - 1;
        } else if (y > line.bottom + tolerance) {
            lo = mid + 1;
        } else {
            return &line;
        }
    }
    return nullptr;
}

std::wstring_view textViewForRect(const App& app, const App::TextRect& tr) {
    if (tr.docStart >= app.docText.size()) return {};
    size_t len = tr.docLength;
    if (tr.docStart + len > app.docText.size()) {
        len = app.docText.size() - tr.docStart;
    }
    return std::wstring_view(app.docText.data() + tr.docStart, len);
}

const App::TextRect* findTextRectAt(const App& app, int x, int y) {
    const auto* line = findLineBucketAt(app, (float)y);
    if (!line) return nullptr;

    for (size_t idx : line->textRectIndices) {
        const auto& tr = app.textRects[idx];
        if (x >= tr.rect.left && x <= tr.rect.right &&
            y >= tr.rect.top && y <= tr.rect.bottom) {
            return &tr;
        }
    }
    return nullptr;
}

bool findWordBoundsAt(const App& app, const App::TextRect& tr, int x,
                      float& wordLeft, float& wordRight) {
    std::wstring_view text = textViewForRect(app, tr);
    if (text.empty()) return false;

    float totalWidth = tr.rect.right - tr.rect.left;
    float charWidth = totalWidth / (float)text.length();

    // Find which character was clicked
    int charIndex = (int)((x - tr.rect.left) / charWidth);
    charIndex = std::max(0, std::min(charIndex, (int)text.length() - 1));

    // Find word start (scan left)
    int wordStart = charIndex;
    while (wordStart > 0 && !isWordBoundary(text[wordStart - 1])) {
        wordStart--;
    }

    // Find word end (scan right)
    int wordEnd = charIndex;
    while (wordEnd < (int)text.length() - 1 && !isWordBoundary(text[wordEnd + 1])) {
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
    const auto* line = findLineBucketAt(app, y);
    if (!line) return;

    lineLeft = line->minX;
    lineRight = line->maxX;
    lineTop = line->top;
    lineBottom = line->bottom;
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
