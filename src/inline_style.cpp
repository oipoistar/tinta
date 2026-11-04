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
    // A face a CSS theme gave the text keeps its family and size
    if (IDWriteTextFormat* own = page::variant(app, base, bold, italic)) return own;
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

namespace {

bool linkLike(ElementType type) {
    return type == ElementType::Link || type == ElementType::FootnoteReference ||
           type == ElementType::FootnoteBacklink || type == ElementType::CrossReference ||
           type == ElementType::Citation;
}

// What a CSS theme says about a span: its colour and face, a background,
// and the rules over and through it (page_style.h)
void applyCss(App& app, const Element& elem, const page::Style& css, const InlineStyle& inherited,
              InlineStyle& st) {
    using css::Prop;
    st.css = css;
    if (css.has(Prop::Color)) st.color = page::color(css->color);
    st.format = page::format(app, css, st.format);
    if (css.has(Prop::BackgroundColor)) {
        st.bgColor = page::color(css->backgroundColor);
        st.hasBg = st.bgColor.a > 0.0f;
    }
    if (css.has(Prop::TextDecorationLine)) {
        const css::Decoration& d = css->decoration;
        if (linkLike(elem.type)) {
            st.linkRule = d.underline ? 1 : 0;
        } else {
            // A span's own rule can be taken away; one from around it cannot
            if (elem.type == ElementType::Strikethrough) st.hasStrike = inherited.hasStrike || d.lineThrough;
            else if (d.lineThrough) st.hasStrike = true;
            if (elem.type == ElementType::Underline) st.hasUnderline = inherited.hasUnderline || d.underline;
            else if (d.underline && !st.hasUnderline) {
                st.hasUnderline = true;
                st.underlineFormat = st.format;
                st.underlineYOffset = st.drawYOffset;
            }
        }
    }
    if (css.has(Prop::TextDecorationColor) && css->decoration.color) st.ruleColor = page::color(*css->decoration.color);
}

// Inline code a CSS theme styles: the face it sets, its colour, and the
// box, padding, corners and edge of its chip
void applyCodeCss(App& app, const page::Style& css, InlineStyle& st) {
    using css::Prop;
    st.css = css;
    IDWriteTextFormat* own = inlineCodeFormat(app, st.bold, st.italic);
    IDWriteTextFormat* face = page::format(app, css, own);
    if (face != own) st.codeFormat = face;
    if (css.has(Prop::Color)) st.codeColor = page::color(css->color);
    if (css.has(Prop::BackgroundColor)) st.codeBox = page::color(css->backgroundColor);
    if (css.has(Prop::PaddingLeft)) st.codePadLeft = page::px(app, css->padding[3]);
    if (css.has(Prop::PaddingRight)) st.codePadRight = page::px(app, css->padding[1]);
    if (css.has(Prop::BorderTopLeftRadius)) st.codeRadius = page::px(app, css->radius[0]);
    if (css.has(Prop::BorderTopWidth) || css.has(Prop::BorderTopStyle)) {
        const css::Border& edge = css->border[0];
        st.codeEdgeWidth = edge.style == css::LineStyle::None ? 0.0f : page::px(app, edge.width);
        st.codeEdge = page::color(edge.color);
    }
}

}  // namespace

void flattenInline(App& app, const std::vector<ElementPtr>& elements,
                   const InlineStyle& inherited, float lineHeight,
                   std::vector<StyledRun>& out) {
    for (const auto& elem : elements) {
        InlineStyle st = inherited;
        // A CSS theme's word on this span (page_style.h)
        page::Style css;
        if (inherited.css && elem->type != ElementType::Text && elem->type != ElementType::SoftBreak &&
            elem->type != ElementType::HardBreak) {
            css = page::child(app, inherited.css, *elem);
        }
        switch (elem->type) {
            case ElementType::Code:
                if (css) applyCodeCss(app, css, st);
                out.push_back({elem, st});
                continue;

            case ElementType::Image:
            case ElementType::MathInline:
            case ElementType::MathDisplay:
                if (css && css.has(css::Prop::Color)) st.color = page::color(css->color);
                out.push_back({elem, st});
                continue;

            case ElementType::Text:
            case ElementType::Ruby:
            case ElementType::SoftBreak:
            case ElementType::HardBreak:
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

            case ElementType::Citation:
            case ElementType::CrossReference:
                // Scholar: what it reads as, in the link colour without a
                // rule under it, going where its url says (#ref-key,
                // #fig:id). Its anchor, cite-N, is where it stands
                if (!elem->url.empty()) {
                    st.color = app.theme.link;
                    st.linkUrl = elem->url;
                    st.isLink = true;
                    st.anchorId = elem->title;
                }
                // A key or figure that is not there, in the notices' warning colour
                if (elem->level) st.color = app.theme.isDark ? hexColor(0xF0C27A) : hexColor(0xA66A12);
                if (elem->type == ElementType::Citation) app.citationKeys[elem->title] = elem->language;
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
        if (css) {
            applyCss(app, *elem, css, inherited, st);
            // A key or figure that is not there stays in the warning colour
            if ((elem->type == ElementType::Citation || elem->type == ElementType::CrossReference) && elem->level) {
                st.color = app.theme.isDark ? hexColor(0xF0C27A) : hexColor(0xA66A12);
            }
        }
        flattenInline(app, elem->children, st, lineHeight, out);
    }
}
