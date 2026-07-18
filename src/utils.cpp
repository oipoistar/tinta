#include "utils.h"
#include "render.h"

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

void updateWindowTitle(App& app) {
    std::wstring title = L"Tinta";
    if (!app.currentFile.empty()) {
        std::wstring wpath = toWide(app.currentFile);
        size_t lastSep = wpath.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos)
            title = L"Tinta - " + wpath.substr(lastSep + 1);
        else
            title = L"Tinta - " + wpath;
    }
    SetWindowTextW(app.hwnd, title.c_str());
}

void openUrl(const std::string& url) {
    if (!url.empty()) {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

std::string slugifyHeading(const std::wstring& text) {
    std::wstring filtered;
    filtered.reserve(text.size());
    auto isWidePunct = [](wchar_t c) {
        // Punctuation is stripped from slugs in any script (GitHub rule):
        // general punctuation (— …), CJK symbols/punctuation (、。「」【】),
        // vertical/compat forms, and fullwidth ASCII punctuation variants
        return (c >= 0x2000 && c <= 0x206F) ||
               (c >= 0x3000 && c <= 0x303F) ||
               (c >= 0xFE30 && c <= 0xFE4F) ||
               (c >= 0xFF00 && c <= 0xFF0F) ||
               (c >= 0xFF1A && c <= 0xFF20) ||
               (c >= 0xFF3B && c <= 0xFF40) ||
               (c >= 0xFF5B && c <= 0xFF65);
    };
    for (wchar_t c : text) {
        if (c >= 0x80) {
            // GitHub keeps non-ASCII letters in slugs (CJK headings form
            // valid anchors) but still strips punctuation; iswalnum() is
            // unreliable for both under the default C locale
            if (!isWidePunct(c)) filtered += c;
        } else if (iswalnum(c)) {
            filtered += towlower(c);
        } else if (c == L' ') {
            filtered += L'-';
        } else if (c == L'-' || c == L'_') {
            filtered += c;
        }
        // else: drop ASCII punctuation, matching GitHub's slug rules
    }
    if (filtered.empty()) return {};

    int len = WideCharToMultiByte(CP_UTF8, 0, filtered.c_str(), (int)filtered.size(),
                                   nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, filtered.c_str(), (int)filtered.size(),
                         &out[0], len, nullptr, nullptr);
    return out;
}

void scrollToHeadingY(App& app, float headingY) {
    float targetY = headingY - 20.0f;
    float maxScroll = std::max(0.0f, app.contentHeight - app.height);
    app.scrollY = std::max(0.0f, std::min(targetY, maxScroll));
    app.targetScrollY = app.scrollY;
    // Not every click path repaints afterwards (a bare link click outside a
    // selection doesn't) — internal jumps must show immediately
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

bool scrollToHeadingId(App& app, const std::string& id) {
    // Headings populate during the viewport-first background layout; an
    // early click on an anchor into a not-yet-laid-out section would miss.
    // Finish the layout synchronously before declaring the target absent.
    for (int attempt = 0; attempt < 2; attempt++) {
        for (const auto& h : app.headings) {
            if (h.id == id) {
                scrollToHeadingY(app, h.y);
                return true;
            }
        }
        if (app.layoutComplete && !app.layoutDirty) break;
        ensureLayoutComplete(app);
    }
    return false;
}

void handleLinkClick(App& app) {
    if (app.hoveredLink.empty()) return;
    if (app.hoveredLink[0] == '#') {
        scrollToHeadingId(app, app.hoveredLink.substr(1));
    } else {
        openUrl(app.hoveredLink);
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
        case ElementType::MermaidDiagram:
            out += toWide(elem->text);
            if (!out.empty() && out.back() != L'\n') out += L"\n";
            break;
        case ElementType::Ruby:
            // Include base text but skip RubyText (annotation)
            for (const auto& child : elem->children) {
                if (child->type != ElementType::RubyText) {
                    extractText(child, out);
                }
            }
            break;
        default:
            for (const auto& child : elem->children) {
                extractText(child, out);
            }
            break;
    }
}
