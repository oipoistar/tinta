#include "inline_style.h"

#include <cctype>
#include <filesystem>

namespace {

// Local converters keep this file free of editor.cpp for the test target
std::wstring refToWide(const std::string& str) {
    if (str.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
                                     (int)str.size(), nullptr, 0);
    std::wstring out(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0],
                        length);
    return out;
}

std::string refToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                     (int)wide.size(), nullptr, 0, nullptr,
                                     nullptr);
    std::string out(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &out[0],
                        length, nullptr, nullptr);
    return out;
}

// %20 and friends, for [text](my%20notes.md) targets
std::string percentDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '%' && i + 2 < value.size() &&
            std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
            out += static_cast<char>(
                std::stoi(value.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += value[i];
        }
    }
    return out;
}

// A real [text](target) whose target is a local text-file path gets the
// same live/broken treatment as a plain-text reference - any extension
// the plain-path scanner recognizes, not just markdown (#162)
bool isLocalFileRefUrl(const std::string& url) {
    if (url.empty() || url[0] == '#') return false;
    if (url.find("://") != std::string::npos) return false;
    if (url.rfind("//", 0) == 0 || url.rfind("\\\\", 0) == 0) return false;
    for (size_t i = 0; i < url.size(); i++) {
        if (url[i] == ':' &&
            !(i == 1 && std::isalpha(static_cast<unsigned char>(url[0])))) {
            return false;  // wiki:, mailto:, and other schemes
        }
    }
    return qmd::fileRefKnownExtension(url);
}

// Resolves against the current file's folder and checks the disk, cached
// per resolved path until the next full relayout
bool resolveFileRef(App& app, const std::string& raw, std::string& absOut) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path path(refToWide(percentDecode(raw)));
    if (path.is_relative()) {
        if (app.currentFile.empty()) {
            absOut = raw;
            return false;  // unsaved buffer: nothing to resolve against
        }
        path = fs::path(refToWide(app.currentFile)).parent_path() / path;
    }
    path = path.lexically_normal();
    absOut = refToUtf8(path.wstring());
    auto found = app.fileRefCache.find(absOut);
    if (found != app.fileRefCache.end()) return found->second;
    bool exists = fs::exists(path, ec) && !fs::is_directory(path, ec);
    app.fileRefCache[absOut] = exists;
    return exists;
}

}  // namespace

IDWriteTextFormat* emphasisFormat(App& app, IDWriteTextFormat* base, bool bold, bool italic) {
    IDWriteTextFormat* fmt = nullptr;
    if (bold && italic) fmt = app.boldItalicFormat ? app.boldItalicFormat : app.boldFormat;
    else if (bold) fmt = app.boldFormat;
    else if (italic) fmt = app.italicFormat;
    return fmt ? fmt : base;
}

IDWriteTextFormat* inlineCodeFormat(App& app, bool bold, bool italic) {
    IDWriteTextFormat* fmt = nullptr;
    if (bold && italic) fmt = app.codeBoldItalicFormat;
    else if (bold) fmt = app.codeBoldFormat;
    else if (italic) fmt = app.codeItalicFormat;
    return fmt ? fmt : app.codeFormat;
}

void flattenInline(App& app, const std::vector<ElementPtr>& elements,
                   const InlineStyle& inherited, float lineHeight,
                   std::vector<StyledRun>& out) {
    for (const auto& elem : elements) {
        InlineStyle st = inherited;
        switch (elem->type) {
            case ElementType::Text:
            case ElementType::Code:
            case ElementType::Image:
            case ElementType::Ruby:
            case ElementType::SoftBreak:
            case ElementType::HardBreak:
            case ElementType::MathInline:
            case ElementType::MathDisplay:
                out.push_back({elem, st});
                continue;

            case ElementType::Strong:
            case ElementType::Emphasis:
                st.bold = st.bold || elem->type == ElementType::Strong;
                st.italic = st.italic || elem->type == ElementType::Emphasis;
                if (!st.fixedSize) st.format = emphasisFormat(app, st.format, st.bold, st.italic);
                break;

            case ElementType::Strikethrough:
                st.hasStrike = true;
                break;

            case ElementType::Highlight:
                // ==text== renders on a marker-pen background
                st.hasBg = true;
                st.bgColor = app.theme.isDark
                    ? D2D1::ColorF(0.98f, 0.80f, 0.25f, 0.28f)
                    : D2D1::ColorF(1.00f, 0.88f, 0.20f, 0.45f);
                break;

            case ElementType::Superscript:
            case ElementType::Subscript:
                // Small text; NEAR alignment already sits sup at the top of the line
                if (app.supSubFormat) {
                    st.format = app.supSubFormat;
                    st.fixedSize = true;
                }
                if (elem->type == ElementType::Subscript) st.drawYOffset = lineHeight * 0.38f;
                break;

            case ElementType::Link: {
                st.color = app.theme.link;
                st.linkUrl = elem->url;
                st.isLink = true;
                std::string target;
                if (elem->url.rfind("fileref:", 0) == 0) {
                    target = elem->url.substr(8);
                } else if (isLocalFileRefUrl(elem->url)) {
                    target = elem->url;
                }
                if (!target.empty()) {
                    std::string resolved;
                    if (resolveFileRef(app, target, resolved)) {
                        st.linkUrl = "fileref-ok:" + resolved;
                    } else {
                        // Missing target: a ghost of the link color, inert
                        st.linkUrl = "fileref-missing:" + resolved;
                        st.color.a *= 0.45f;
                    }
                }
                break;
            }

            default:
                break;  // unknown wrapper: keep the style and descend
        }
        flattenInline(app, elem->children, st, lineHeight, out);
    }
}
