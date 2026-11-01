#include "inline_style.h"
#include "link_target.h"
#include "network_path.h"

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

// A real [text](target) whose target is a local text-file path gets the
// same live/broken treatment as a plain-text reference - any extension
// the plain-path scanner recognizes, not just markdown (#162)
bool isLocalFileRefUrl(const std::string& url) {
    if (url.empty()) return false;
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
// per resolved path until the next full relayout. A target on another
// computer's share is not checked, which would sign in to its server
// (network_path.h): it badges as missing.
bool resolveFileRef(App& app, const std::string& raw, std::string& absOut) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path path(refToWide(raw));
    fs::path folder;
    if (!app.currentFile.empty()) folder = fs::path(refToWide(app.currentFile)).parent_path();
    if (path.is_relative()) {
        if (folder.empty()) {
            absOut = raw;
            return false;  // unsaved buffer: nothing to resolve against
        }
        path = folder / path;
    }
    path = path.lexically_normal();
    absOut = refToUtf8(path.wstring());
    auto found = app.fileRefCache.find(absOut);
    if (found != app.fileRefCache.end()) return found->second;
    bool exists = qmd::mayOpenFromDocument(path.wstring(), folder.wstring()) &&
                  fs::exists(path, ec) && !fs::is_directory(path, ec);
    app.fileRefCache[absOut] = exists;
    return exists;
}

}  // namespace

IDWriteTextFormat* emphasisFormat(App& app, IDWriteTextFormat* base, bool bold, bool italic) {
    // A heading is bold already; its emphasis keeps the heading's size
    // rather than dropping to the body faces (and, inside it, inline code
    // keeps the heading's baseline, #259)
    for (int i = 0; i < 6; i++) {
        if (base && (base == app.headingFormats[i] || base == app.headingItalicFormats[i])) {
            if (italic && app.headingItalicFormats[i]) return app.headingItalicFormats[i];
            return app.headingFormats[i] ? app.headingFormats[i] : base;
        }
    }
    IDWriteTextFormat* fmt = nullptr;
    if (bold && italic) fmt = app.boldItalicFormat ? app.boldItalicFormat : app.boldFormat;
    else if (bold) fmt = app.boldFormat;
    else if (italic) fmt = app.italicFormat;
    return fmt ? fmt : base;
}

// The small face for ^sup^/~sub~ and <sup>/<sub>: a heading's own, in
// proportion to its size, or the body's
static IDWriteTextFormat* scriptFormat(App& app, IDWriteTextFormat* around) {
    for (int i = 0; i < 6; i++) {
        if (around && (around == app.headingFormats[i] || around == app.headingItalicFormats[i]) &&
            app.headingScriptFormats[i]) {
            return app.headingScriptFormats[i];
        }
    }
    return app.supSubFormat;
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
                // The theme's bold and italic colours (#310); a link keeps its own
                if (auto color = themeEmphasisColor(app.theme, st.bold, st.italic); color && !st.isLink) st.color = *color;
                break;

            case ElementType::Strikethrough:
                st.hasStrike = true;
                break;

            case ElementType::Underline:
                st.hasUnderline = true;
                st.underlineFormat = st.format;
                st.underlineYOffset = st.drawYOffset;
                break;

            case ElementType::Anchor:
                // Zero-width target the layout pins to its line (#255),
                // then any text it wraps flows on as usual
                out.push_back({elem, st});
                break;

            case ElementType::Highlight:
                // ==text== renders on a marker-pen background
                st.hasBg = true;
                st.bgColor = themeHighlightBackground(app.theme);
                if (app.theme.highlightText) st.color = *app.theme.highlightText;
                break;

            case ElementType::Superscript:
            case ElementType::Subscript:
                // Small text, a heading's in proportion to it (#263); NEAR
                // alignment already sits sup at the top of the line
                if (IDWriteTextFormat* face = scriptFormat(app, st.format)) {
                    st.format = face;
                    st.fixedSize = true;
                }
                if (elem->type == ElementType::Subscript) st.drawYOffset = lineHeight * 0.38f;
                break;

            case ElementType::FootnoteReference:
                st.anchorId = elem->title;
                if (app.supSubFormat) { st.format = app.supSubFormat; st.fixedSize = true; }
                [[fallthrough]];
            case ElementType::FootnoteBacklink:
            case ElementType::Link: {
                st.color = app.theme.link;
                st.linkUrl = elem->url;
                st.isLink = true;
                qmd::LinkTarget target;
                if (elem->url.rfind("fileref:", 0) == 0) {
                    target.path = qmd::decodeLinkComponent(elem->url.substr(8));
                } else {
                    auto parsed = qmd::splitLinkTarget(elem->url);
                    if (isLocalFileRefUrl(parsed.path) &&
                        (!parsed.hasFragment || qmd::fileRefIsMarkdown(parsed.path))) {
                        target = std::move(parsed);
                    }
                }
                if (!target.path.empty()) {
                    std::string resolved;
                    bool exists = resolveFileRef(app, target.path, resolved);
                    target.path = std::move(resolved);
                    st.linkUrl = (exists ? "fileref-ok:" : "fileref-missing:") +
                                 qmd::encodeLinkTarget(target);
                    if (!exists) st.color.a *= 0.45f;
                }
                break;
            }

            default:
                break;  // unknown wrapper: keep the style and descend
        }
        flattenInline(app, elem->children, st, lineHeight, out);
    }
}
