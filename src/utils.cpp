#include "utils.h"
#include "link_target.h"
#include "render.h"
#include "i18n.h"
#include "signals.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

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


void updateWindowTitle(App& app) {
    std::wstring title = L"Tinta";
    if (!app.currentFile.empty()) {
        std::wstring wpath = toWide(app.currentFile);
        size_t lastSep = wpath.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos)
            title = L"Tinta - " + wpath.substr(lastSep + 1);
        else
            title = L"Tinta - " + wpath;
    } else if (app.editMode) {
        // Quick note (Ctrl+N): no backing file until the first save
        title = L"Tinta - ";
        title += tr(app, "title.untitled");
    }
    SetWindowTextW(app.hwnd, title.c_str());
}

void launchQuickNoteWindow() {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    ShellExecuteW(nullptr, L"open", exePath, L"--cascade --new", nullptr,
                  SW_SHOWNORMAL);
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
    // The title-bar tab strip draws over the document's top band, so the
    // landing margin must clear it or the heading arrives half-hidden
    float targetY = headingY - chromeTopHeight(app) - dpi(app, 14.0f);
    float maxScroll = std::max(0.0f, app.contentHeight - app.height);
    app.scrollY = std::max(0.0f, std::min(targetY, maxScroll));
    app.targetScrollY = app.scrollY;
    app.pendingScrollRestore = -1.0f;
    // Not every click path repaints afterwards (a bare link click outside a
    // selection doesn't) — internal jumps must show immediately
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

bool documentTargetY(App& app, const std::string& id, float& y) {
    if (app.layoutDirty || !app.layoutComplete) ensureLayoutComplete(app);
    auto note = app.footnoteAnchors.find(id);
    if (note != app.footnoteAnchors.end()) {
        y = note->second;
        return true;
    }
    for (const auto& h : app.headings) {
        if (h.id == id) {
            y = h.y;
            return true;
        }
    }
    for (const auto& anchor : app.htmlAnchors) {
        if (anchor.first == id) {
            y = anchor.second;
            return true;
        }
    }
    return false;
}

bool scrollToHeadingId(App& app, const std::string& id) {
    // A newly opened document may still have the previous file's headings.
    // Finish the destination layout before looking up even an existing id.
    if (app.layoutDirty || !app.layoutComplete) ensureLayoutComplete(app);
    if (id.empty()) {
        scrollToHeadingY(app, 0.0f);
        return true;
    }
    float y = 0.0f;
    if (!documentTargetY(app, id, y)) return false;
    scrollToHeadingY(app, y);
    return true;
}

void handleLinkClick(App& app) {
    if (app.hoveredLink.empty()) return;
    if (app.hoveredLink[0] == '#') {
        scrollToHeadingId(app, qmd::decodeLinkComponent(app.hoveredLink.substr(1)));
    } else {
        openUrl(app.hoveredLink);
    }
}

bool openClipboard(HWND hwnd, bool* held) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(hwnd)) return true;
        Sleep(5);
    }
    if (held) *held = true;
    return false;
}

void signalClipboardBusy(App& app) {
    signalPushKey(app, SIG_WARN, SIGI_WARNING, "toast.clipboard_busy");
}

std::wstring readClipboardText(HWND hwnd) {
    std::wstring text;
    if (!openClipboard(hwnd)) return text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = p;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

bool copyToClipboard(HWND hwnd, const std::wstring& text, bool* held) {
    if (text.empty()) return false;
    size_t size = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return false;
    wchar_t* dest = (wchar_t*)GlobalLock(hMem);
    if (!dest) {
        GlobalFree(hMem);
        return false;
    }
    memcpy(dest, text.c_str(), size);
    GlobalUnlock(hMem);
    if (!openClipboard(hwnd, held)) {
        GlobalFree(hMem);
        return false;
    }
    bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, hMem);
    if (!copied) GlobalFree(hMem);  // ownership transfers only on success
    CloseClipboard();
    return copied;
}

// Outlook's own font for new mail, as CSS, so pasted text matches what is
// typed around it; without it Word-based editors fall back to Times New
// Roman for HTML. Empty when Outlook has not stored one.
std::string composeFontCss(const std::string& style) {
    auto declaration = [&](const char* property) -> std::string {
        size_t at = style.find(property);
        // Skip mso-ascii-font-family and the like: only the property named
        while (at != std::string::npos && at > 0 && style[at - 1] == '-') {
            at = style.find(property, at + 1);
        }
        if (at == std::string::npos) return {};
        size_t valueStart = at + strlen(property);
        size_t valueEnd = style.find_first_of(";}", valueStart);
        if (valueEnd == std::string::npos) return {};
        std::string value = style.substr(valueStart, valueEnd - valueStart);
        size_t first = value.find_first_not_of(" \t\r\n");
        size_t last = value.find_last_not_of(" \t\r\n");
        value = first == std::string::npos ? std::string() : value.substr(first, last - first + 1);
        if (value.find_first_of("<>'") != std::string::npos) return {};
        return value;
    };
    // Only Word-based editors (Outlook among them) read mso- properties;
    // browsers drop them, so web mail keeps its own font (#285)
    std::string ascii = declaration("mso-ascii-font-family:");
    if (ascii.empty()) {
        // The first face of the plain list
        ascii = declaration("font-family:");
        ascii = ascii.substr(0, ascii.find(','));
    }
    std::string hansi = declaration("mso-hansi-font-family:");
    if (hansi.empty()) hansi = ascii;
    std::string css;
    if (!ascii.empty()) css += "mso-ascii-font-family:" + ascii + ";";
    if (!hansi.empty()) css += "mso-hansi-font-family:" + hansi + ";";
    std::string sizePt = declaration("mso-ansi-font-size:");
    if (!sizePt.empty()) css += "mso-ansi-font-size:" + sizePt + ";";
    return css;
}

static std::string mailComposeCss() {
    BYTE data[8192];
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Office\\16.0\\Common\\MailSettings",
                     L"ComposeFontComplex", RRF_RT_REG_BINARY, nullptr, data,
                     &size) != ERROR_SUCCESS) {
        return {};
    }
    return composeFontCss(std::string(reinterpret_cast<const char*>(data), size));
}

std::string clipboardHtml(const std::wstring& htmlFragment) {
    std::string fragment;
    if (!htmlFragment.empty()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, htmlFragment.c_str(),
                                      (int)htmlFragment.size(), nullptr, 0,
                                      nullptr, nullptr);
        fragment.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, htmlFragment.c_str(),
                            (int)htmlFragment.size(), fragment.data(), len,
                            nullptr, nullptr);
    }
    std::string css = mailComposeCss();
    std::string open = css.empty() ? "<div>" : "<div style='" + css + "'>";
    const std::string close = "</div>";
    const char* header =
        "Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\n"
        "StartFragment:%010zu\r\nEndFragment:%010zu\r\n";
    size_t headerLength = (size_t)snprintf(nullptr, 0, header,
                                           (size_t)0, (size_t)0, (size_t)0, (size_t)0);
    const std::string prefix =
        "<html><head><meta http-equiv='Content-Type' "
        "content='text/html; charset=utf-8'></head><body>\r\n<!--StartFragment-->";
    const std::string suffix = "<!--EndFragment-->\r\n</body></html>";
    size_t startHtml = headerLength;
    size_t startFragment = startHtml + prefix.size();
    size_t endFragment = startFragment + open.size() + fragment.size() + close.size();
    size_t endHtml = endFragment + suffix.size();
    std::string out(headerLength + 1, '\0');
    snprintf(out.data(), out.size(), header, startHtml, endHtml, startFragment, endFragment);
    out.resize(headerLength);
    return out + prefix + open + fragment + close + suffix;
}

bool copyRichTextToClipboard(HWND hwnd, const std::wstring& text,
                             const std::wstring& htmlFragment, bool* held) {
    if (text.empty()) return false;
    static const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
    std::string html = clipboardHtml(htmlFragment);
    auto globalCopy = [](const void* bytes, size_t size) -> HGLOBAL {
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!mem) return nullptr;
        void* dest = GlobalLock(mem);
        if (!dest) { GlobalFree(mem); return nullptr; }
        memcpy(dest, bytes, size);
        GlobalUnlock(mem);
        return mem;
    };
    HGLOBAL textMem = globalCopy(text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    if (!textMem) return false;
    HGLOBAL htmlMem = htmlFormat ? globalCopy(html.c_str(), html.size() + 1) : nullptr;
    if (!openClipboard(hwnd, held)) {
        GlobalFree(textMem);
        if (htmlMem) GlobalFree(htmlMem);
        return false;
    }
    bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, textMem);
    if (!copied) GlobalFree(textMem);  // ownership transfers only on success
    if (htmlMem && !(copied && SetClipboardData(htmlFormat, htmlMem))) GlobalFree(htmlMem);
    CloseClipboard();
    return copied;
}

// The block text ends with its last line's break, which the copy button
// leaves out, as GitHub's does. A blank line written before the closing
// fence is content and stays.
std::wstring codeCopyText(const std::wstring& code) {
    std::wstring text = code;
    if (!text.empty() && text.back() == L'\n') {
        text.pop_back();
        if (!text.empty() && text.back() == L'\r') text.pop_back();
    }
    return text;
}

std::wstring absoluteFilePath(const std::string& path) {
    if (path.empty()) return {};
    std::wstring wide = toWide(path);
    DWORD capacity = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
    if (!capacity) return {};
    std::wstring full(capacity, L'\0');
    DWORD length = GetFullPathNameW(wide.c_str(), capacity, full.data(), nullptr);
    if (!length || length >= capacity) return {};
    full.resize(length);
    return full;
}

void copyFilePath(App& app, HWND hwnd, const std::string& path) {
    bool held = false;
    if (copyToClipboard(hwnd, absoluteFilePath(path), &held))
        signalPushKey(app, SIG_INFO, SIGI_COPY, "toast.copied");
    else if (held)
        signalClipboardBusy(app);
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
