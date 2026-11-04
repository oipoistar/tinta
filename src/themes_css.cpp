// Themes written in CSS (docs/specs/theming.md, phase 1): every theme as a
// sheet, built-in and themes.ini ones through one mapping, and a sheet read
// back into the D2DTheme Tinta draws with.

#pragma optimize("s", on)

#include "themes_css.h"
#include "font_faces.h"

#include <dwrite.h>

#include <cstdio>
#include <mutex>

namespace {

std::string hexCss(const D2D1_COLOR_F& c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", (int)(c.r * 255.0f + 0.5f), (int)(c.g * 255.0f + 0.5f),
             (int)(c.b * 255.0f + 0.5f));
    return buf;
}

std::string utf8(const wchar_t* text) {
    if (!text || !*text) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(len > 0 ? len - 1 : 0, '\0');
    if (len > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
    return out;
}

// A family name as a CSS string
std::string family(const wchar_t* name) {
    std::string out = "\"";
    for (char c : utf8(name)) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

void rule(std::string& css, const char* selector, const std::string& declarations) {
    if (!declarations.empty()) css += std::string(selector) + " { " + declarations + "}\n";
}

std::string color(const char* property, const D2D1_COLOR_F& c) {
    return std::string(property) + ": " + hexCss(c) + "; ";
}

}  // namespace

std::string themeToCss(const D2DTheme& t) {
    std::string css = "/* " + utf8(t.name) + " */\n";
    rule(css, ":root", std::string("color-scheme: ") + (t.isDark ? "dark" : "light") + "; " +
                           color("accent-color", t.accent));
    std::string body = color("background-color", t.background) + color("color", t.text);
    if (t.fontFamily && *t.fontFamily) body += "font-family: " + family(t.fontFamily) + "; ";
    rule(css, "body", body);
    std::string headings = color("color", t.heading);
    if (t.headingFontFamily && *t.headingFontFamily) headings += "font-family: " + family(t.headingFontFamily) + "; ";
    rule(css, "h1, h2, h3, h4, h5, h6", headings);
    for (int i = 0; i < 6; i++) {
        if (!t.headingColors[i]) continue;
        const std::string tag = "h" + std::to_string(i + 1);
        rule(css, tag.c_str(), color("color", *t.headingColors[i]));
    }
    rule(css, "a", color("color", t.link));
    if (t.codeFontFamily && *t.codeFontFamily) rule(css, "code, pre", "font-family: " + family(t.codeFontFamily) + "; ");
    rule(css, "pre", color("background-color", t.codeBackground) + color("color", t.code));
    std::string inlineCode = color("color", themeInlineCodeColor(t));
    const D2D1_COLOR_F box = themeInlineCodeBackground(t);
    inlineCode += box.a == 0 ? std::string("background: none; ") : color("background-color", box);
    rule(css, ":not(pre) > code", inlineCode);
    rule(css, "blockquote", color("border-left-color", t.blockquoteBorder));
    std::string mark;
    if (t.highlightBackground) mark += color("background-color", *t.highlightBackground);
    if (t.highlightText) mark += color("color", *t.highlightText);
    rule(css, "mark", mark);
    if (t.boldText) rule(css, "strong", color("color", *t.boldText));
    if (t.italicText) rule(css, "em", color("color", *t.italicText));
    if (t.boldItalicText) rule(css, "strong em, em strong", color("color", *t.boldItalicText));
    if (t.selection) rule(css, "::selection", color("background-color", *t.selection));
    if (t.rule) rule(css, "hr", color("border-top-color", *t.rule));
    rule(css, ".token.keyword", color("color", t.syntaxKeyword));
    rule(css, ".token.string", color("color", t.syntaxString));
    rule(css, ".token.comment", color("color", t.syntaxComment));
    rule(css, ".token.number", color("color", t.syntaxNumber));
    rule(css, ".token.function", color("color", t.syntaxFunction));
    rule(css, ".token.class-name", color("color", t.syntaxType));
    rule(css, ".token.control", color("color", t.syntaxControlFlow));
    return css;
}

namespace {

D2D1_COLOR_F d2d(const css::Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

css::Element tag(std::string_view name) {
    css::Element element;
    element.tag = name;
    element.index = element.count = element.typeIndex = element.typeCount = 1;
    return element;
}

css::Element token(std::string_view kind) {
    css::Element element = tag("span");
    element.classes = {"token", kind};
    return element;
}

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)(len > 0 ? len : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

// Whether this PC has a font family, as DirectWrite names them
bool installed(const std::wstring& family) {
    static std::mutex lock;
    static IDWriteFontCollection* fonts = nullptr;
    std::lock_guard<std::mutex> guard(lock);
    if (!fonts) {
        IDWriteFactory* factory = nullptr;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(&factory)))) {
            return true;
        }
        factory->GetSystemFontCollection(&fonts);
        factory->Release();
        if (!fonts) return true;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(fonts->FindFamilyName(family.c_str(), &index, &exists)) && exists;
}

// The face a family list draws with: the first one this PC has, the
// generic names as Windows sets them. When it has none, the first named,
// which Tinta stands in for as it does for any theme's missing face (#264)
std::wstring faceOf(const css::ComputedStyle& style, const std::wstring& fallback) {
    std::wstring first;
    for (const auto& name : style.fontFamily) {
        std::wstring family = wide(name);
        if (name == "serif") family = L"Georgia";
        else if (name == "sans-serif" || name == "system-ui" || name == "ui-sans-serif") family = L"Segoe UI";
        else if (name == "monospace" || name == "ui-monospace") family = L"Consolas";
        else if (name == "cursive") family = L"Segoe Script";
        if (fontfaces::has(family) || installed(family)) return family;
        if (first.empty()) first = family;
    }
    return first.empty() ? fallback : first;
}

float luminance(const css::Color& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

bool sameColor(const D2D1_COLOR_F& x, const D2D1_COLOR_F& y) {
    return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
}

}  // namespace

CssTheme compileCssTheme(const std::string& text, const std::string& file, bool dark,
                         const css::ImportSource& imports, CustomTheme& out) {
    using css::Prop;
    CssTheme result;
    result.sheet = css::parse(text, file, css::Origin::Theme, imports, result.diagnostics);
    // Its own faces first, so a family it brings is one it can draw with
    fontfaces::load(css::fontFaces(*result.sheet), result.diagnostics);
    css::Engine engine;
    engine.load({result.sheet}, css::Media{dark, false, 800});
    const auto root = engine.root();
    const css::ComputedStyle rs = engine.style(root);
    const auto body = engine.child(root, tag("body"));
    const css::ComputedStyle bs = engine.style(body);
    // Set on the element, or inherited from a rule on the page
    auto declared = [&](const css::ComputedStyle& s, Prop prop) { return s.has(prop) || bs.has(prop) || rs.has(prop); };

    result.lightDark = rs.colorScheme == css::ColorScheme::LightDark;
    const css::Color page = bs.backgroundColor.a > 0 ? bs.backgroundColor : rs.backgroundColor;
    const bool isDark = rs.colorScheme == css::ColorScheme::Dark || (result.lightDark && dark) ||
                        (rs.colorScheme == css::ColorScheme::Normal && page.a > 0 && luminance(page) < 0.5f);

    // From Paper, or Midnight when dark: what the sheet leaves alone stays theirs
    D2DTheme& t = out.theme;
    t = isDark ? THEMES[5] : THEMES[0];
    t.isDark = isDark;
    out.fontFamily = t.fontFamily ? t.fontFamily : L"Segoe UI";
    out.headingFontFamily.clear();
    out.codeFontFamily = t.codeFontFamily ? t.codeFontFamily : L"Consolas";

    if (page.a > 0) t.background = d2d(page);
    if (declared(bs, Prop::Color)) t.text = t.heading = d2d(bs.color);
    if (bs.accentColor) t.accent = d2d(*bs.accentColor);
    if (declared(bs, Prop::FontFamily)) out.fontFamily = faceOf(bs, out.fontFamily);
    if (declared(bs, Prop::FontSize)) t.bodySize = bs.fontSize;
    if (declared(bs, Prop::LineHeight) && bs.fontSize > 0) {
        if (bs.lineHeightFactor > 0) t.lineHeight = bs.lineHeightFactor;
        else if (bs.lineHeight > 0) t.lineHeight = bs.lineHeight / bs.fontSize;
    }

    const auto p = engine.child(body, tag("p"));
    static const char* const headings[6] = {"h1", "h2", "h3", "h4", "h5", "h6"};
    for (int i = 0; i < 6; i++) {
        const css::ComputedStyle h = engine.style(engine.child(body, tag(headings[i])));
        if (h.has(Prop::Color)) t.headingColors[i] = d2d(h.color);
        if (h.has(Prop::FontFamily) && out.headingFontFamily.empty()) out.headingFontFamily = faceOf(h, out.fontFamily);
        if (h.has(Prop::FontSize)) t.headingSize[i] = h.fontSize;
        if (h.has(Prop::FontWeight)) t.headingWeight[i] = h.fontWeight;
        if (h.has(Prop::FontStyle)) t.headingItalic[i] = h.fontStyle != css::FontStyle::Normal;
    }
    // One colour for every level is the theme's heading colour
    bool same = t.headingColors[0].has_value();
    for (int i = 1; i < 6 && same; i++) same = t.headingColors[i] && sameColor(*t.headingColors[i], *t.headingColors[0]);
    if (same) {
        t.heading = *t.headingColors[0];
        for (auto& level : t.headingColors) level.reset();
    }

    const css::ComputedStyle a = engine.style(engine.child(p, tag("a")));
    if (declared(a, Prop::Color)) t.link = d2d(a.color);

    const css::ComputedStyle code = engine.style(engine.child(p, tag("code")));
    if (code.has(Prop::Color)) t.inlineCode = d2d(code.color);
    if (code.has(Prop::BackgroundColor)) t.inlineCodeBackground = d2d(code.backgroundColor);
    const auto pre = engine.child(body, tag("pre"));
    const css::ComputedStyle ps = engine.style(pre);
    const auto preCode = engine.child(pre, tag("code"));
    const css::ComputedStyle pcs = engine.style(preCode);
    if (ps.has(Prop::BackgroundColor)) t.codeBackground = d2d(ps.backgroundColor);
    if (ps.has(Prop::Color) || pcs.has(Prop::Color)) t.code = d2d(pcs.color);
    if (ps.has(Prop::FontFamily) || pcs.has(Prop::FontFamily)) out.codeFontFamily = faceOf(pcs, out.codeFontFamily);
    else if (code.has(Prop::FontFamily)) out.codeFontFamily = faceOf(code, out.codeFontFamily);
    if (ps.has(Prop::FontSize) || pcs.has(Prop::FontSize)) t.codeSize = pcs.fontSize;
    else if (code.has(Prop::FontSize)) t.codeSize = code.fontSize;
    struct Token {
        const char* kind;
        D2D1_COLOR_F D2DTheme::*color;
    };
    static const Token tokens[] = {
        {"keyword", &D2DTheme::syntaxKeyword},   {"string", &D2DTheme::syntaxString},
        {"comment", &D2DTheme::syntaxComment},   {"number", &D2DTheme::syntaxNumber},
        {"function", &D2DTheme::syntaxFunction}, {"class-name", &D2DTheme::syntaxType},
        {"control", &D2DTheme::syntaxControlFlow},
    };
    for (const auto& kind : tokens) {
        const css::ComputedStyle s = engine.style(engine.child(preCode, token(kind.kind)));
        if (s.has(Prop::Color)) t.*kind.color = d2d(s.color);
    }

    const css::ComputedStyle quote = engine.style(engine.child(body, tag("blockquote")));
    if (quote.has(Prop::BorderLeftColor)) t.blockquoteBorder = d2d(quote.border[3].color);

    const css::ComputedStyle mark = engine.style(engine.child(p, tag("mark")));
    if (mark.has(Prop::BackgroundColor)) t.highlightBackground = d2d(mark.backgroundColor);
    if (mark.has(Prop::Color)) t.highlightText = d2d(mark.color);

    const auto strong = engine.child(p, tag("strong"));
    const css::ComputedStyle ss = engine.style(strong);
    const css::ComputedStyle es = engine.style(engine.child(p, tag("em")));
    const css::ComputedStyle both = engine.style(engine.child(strong, tag("em")));
    if (ss.has(Prop::Color)) t.boldText = d2d(ss.color);
    if (es.has(Prop::Color)) t.italicText = d2d(es.color);
    if (both.has(Prop::Color)) t.boldItalicText = d2d(both.color);

    if (const css::ComputedStyle* selection = engine.pseudo(p, css::Pseudo::Selection)) {
        if (selection->has(Prop::BackgroundColor)) t.selection = d2d(selection->backgroundColor);
    }
    const css::ComputedStyle hr = engine.style(engine.child(body, tag("hr")));
    if (hr.has(Prop::BorderTopColor)) t.rule = d2d(hr.border[0].color);
    else if (hr.has(Prop::Color)) t.rule = d2d(hr.color);
    else if (hr.has(Prop::BackgroundColor)) t.rule = d2d(hr.backgroundColor);

    for (const auto& d : engine.diagnostics()) result.diagnostics.push_back(d);
    return result;
}
