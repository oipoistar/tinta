// Per-element styles (docs/specs/theming.md, phase 2): the active CSS
// theme asked, element by element, what it says about the page.

#include "page_style.h"

#include "app.h"
#include "font_faces.h"
#include "markdown.h"

#include <cstdio>
#include <cwctype>
#include <tuple>

namespace page {

namespace {

using css::Prop;

Bits inheritedBits() {
    Bits bits;
    for (size_t p = 0; p < (size_t)Prop::Count; ++p) {
        if (css::inherited((Prop)p)) bits.set(p);
    }
    return bits;
}

// What a page printed without @media print rules leaves to Tinta's print
// palette: a dark theme's colours would print dark on paper
Bits colorBits() {
    Bits bits;
    for (Prop p : {Prop::Color, Prop::BackgroundColor, Prop::BackgroundImage, Prop::BorderTopColor,
                   Prop::BorderRightColor, Prop::BorderBottomColor, Prop::BorderLeftColor,
                   Prop::TextDecorationColor, Prop::AccentColor, Prop::CaretColor, Prop::BoxShadow}) {
        bits.set((size_t)p);
    }
    return bits;
}

const Bits kInherited = inheritedBits();
const Bits kColors = colorBits();

struct Frame {
    Style style;
    const Element* element = nullptr;  // whose children `kids` describes
    std::vector<css::Element> kids;
    std::vector<const Element*> kidOf;
    size_t cursor = 0;
    bool described = false;
};

struct FontKey {
    std::wstring family;
    float size = 0;
    int weight = 400, style = 0, stretch = 5;
    auto tie() const { return std::tie(family, size, weight, style, stretch); }
    bool operator<(const FontKey& o) const { return tie() < o.tie(); }
    bool operator==(const FontKey& o) const { return tie() == o.tie(); }
};

using Features = std::vector<std::pair<uint32_t, uint32_t>>;

const char* const kHeadings[6] = {"h1", "h2", "h3", "h4", "h5", "h6"};
const char* const kAlerts[5] = {"alert-note", "alert-tip", "alert-important", "alert-warning", "alert-caution"};

// Tinta's own look where a browser's would stop a page's font or colour
// reaching an element: heading sizes and weights, bold and italic, code
// faces, link and caption colours. Only what the theme sets is drawn from
// the sheet, so these values matter for what inherits from them (an em
// inside an h2), not for what is drawn
std::string baseSheet(const D2DTheme& t) {
    static const float sizes[6] = {32, 26, 22, 18, 16, 14};
    std::string css;
    char buf[160];
    for (int i = 0; i < 6; i++) {
        snprintf(buf, sizeof buf, "h%d{font-size:%gpx;font-weight:%d%s}", i + 1,
                 t.headingSize[i] > 0 ? t.headingSize[i] : sizes[i], t.headingWeight[i] > 0 ? t.headingWeight[i] : 700,
                 t.headingItalic[i] ? ";font-style:italic" : "");
        css += buf;
    }
    snprintf(buf, sizeof buf, "body{font-size:%gpx}code,pre,kbd,samp{font-family:monospace;font-size:%gpx}",
             t.bodySize > 0 ? t.bodySize : 16.0f, t.codeSize > 0 ? t.codeSize : 14.0f);
    css += buf;
    css +=
        "strong,b,th{font-weight:700}em,i{font-style:italic}sup,sub{font-size:68%}"
        "a,.citation{color:blue}figcaption,th,pre,code{color:gray}.alert-title{font-weight:600}"
        "ul{list-style-type:disc}ol{list-style-type:decimal}";
    return css;
}

}  // namespace

struct State {
    css::Engine engine;
    std::shared_ptr<const css::Sheet> sheet, base;
    std::string baseText;
    css::Media media;
    bool active = false;
    bool colors = true;
    std::vector<std::unique_ptr<Frame>> frames;
    size_t depth = 0;
    float gap = 0;  // the margin above the current line, layout units
    std::map<FontKey, IDWriteTextFormat*> formats;
    std::unordered_map<IDWriteTextFormat*, FontKey> made, known;
    std::map<Features, IDWriteTypography*> typographies;
    std::map<std::vector<std::string>, std::wstring> faces;  // a family list, the face it draws with

    ~State() { release(); }
    void release() {
        for (auto& f : formats) {
            if (f.second) f.second->Release();
        }
        for (auto& t : typographies) {
            if (t.second) t.second->Release();
        }
        formats.clear();
        made.clear();
        known.clear();
        typographies.clear();
    }

    Frame& top() { return *frames[depth - 1]; }

    Frame& push(const Element* children) {
        if (frames.size() <= depth) frames.push_back(std::make_unique<Frame>());
        Frame& f = *frames[depth++];
        f.element = children;
        f.kids.clear();
        f.kidOf.clear();
        f.cursor = 0;
        f.described = false;
        return f;
    }

    Style styleOf(css::Engine::Context context, const Style* parent) {
        Style s;
        s.context = context;
        s.computed = &engine.style(context);
        s.set = s.computed->authored;
        if (parent) s.set |= parent->set & kInherited & ~s.computed->declared;
        if (!colors) s.set &= ~kColors;
        return s;
    }
};

namespace {

State* state(const App& app) {
    State* s = app.pageState.get();
    return s && s->active && s->depth ? s : nullptr;
}

const Style& none() {
    static const Style empty;
    return empty;
}

void describeKids(Frame& f) {
    f.described = true;
    if (!f.element) return;
    for (const auto& c : f.element->children) {
        // The front matter strip is the screen's alone: the export, which
        // the sheet is written against, leaves it out
        if (!c || c->type == ElementType::Properties) continue;
        css::Element d;
        if (!describe(*c, d)) continue;
        f.kids.push_back(d);
        f.kidOf.push_back(c.get());
    }
    const int count = (int)f.kids.size();
    std::vector<std::pair<std::string_view, int>> tags;
    for (int i = 0; i < count; ++i) {
        css::Element& d = f.kids[(size_t)i];
        d.index = i + 1;
        d.count = count;
        auto it = std::find_if(tags.begin(), tags.end(), [&](const auto& t) { return t.first == d.tag; });
        if (it == tags.end()) {
            tags.push_back({d.tag, 1});
            d.typeIndex = 1;
        } else {
            d.typeIndex = ++it->second;
        }
    }
    for (css::Element& d : f.kids) {
        d.typeCount = std::find_if(tags.begin(), tags.end(), [&](const auto& t) { return t.first == d.tag; })->second;
    }
}

size_t findKid(Frame& f, const Element* e) {
    for (size_t k = f.cursor; k < f.kidOf.size(); ++k) {
        if (f.kidOf[k] == e) return f.cursor = k;
    }
    for (size_t k = 0; k < f.cursor && k < f.kidOf.size(); ++k) {
        if (f.kidOf[k] == e) return f.cursor = k;
    }
    return SIZE_MAX;
}

std::wstring familyOf(IDWriteTextFormat* format) {
    UINT32 n = format->GetFontFamilyNameLength();
    std::wstring name(n + 1, L'\0');
    if (FAILED(format->GetFontFamilyName(name.data(), n + 1))) return {};
    name.resize(n);
    return name;
}

FontKey keyOf(State& s, IDWriteTextFormat* format) {
    auto made = s.made.find(format);
    if (made != s.made.end()) return made->second;
    auto known = s.known.find(format);
    if (known != s.known.end()) return known->second;
    FontKey key;
    key.family = familyOf(format);
    key.size = format->GetFontSize();
    key.weight = (int)format->GetFontWeight();
    key.style = (int)format->GetFontStyle();
    key.stretch = (int)format->GetFontStretch();
    s.known.emplace(format, key);
    return key;
}

IDWriteTextFormat* formatFor(App& app, State& s, const FontKey& key) {
    auto found = s.formats.find(key);
    if (found != s.formats.end()) return found->second;
    IDWriteTextFormat* format = nullptr;
    if (app.dwriteFactory) {
        app.dwriteFactory->CreateTextFormat(fontfaces::name(key.family.c_str()), fontfaces::collection(key.family.c_str()),
                                            (DWRITE_FONT_WEIGHT)key.weight,
                                            (DWRITE_FONT_STYLE)key.style, (DWRITE_FONT_STRETCH)key.stretch, key.size,
                                            L"en-us", &format);
    }
    if (format) {
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        s.made.emplace(format, key);
    }
    s.formats.emplace(key, format);
    return format;
}

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)(len > 0 ? len : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

// The first family of a font-family list this PC has, generic names as
// Windows sets them; empty when it has none of them
std::wstring installedFace(App& app, const std::vector<std::string>& families) {
    IDWriteFontCollection* fonts = nullptr;
    if (!app.dwriteFactory || FAILED(app.dwriteFactory->GetSystemFontCollection(&fonts)) || !fonts) return {};
    std::wstring face;
    for (const auto& name : families) {
        std::wstring family = wide(name);
        if (name == "serif" || name == "ui-serif") family = L"Georgia";
        else if (name == "sans-serif" || name == "system-ui" || name == "ui-sans-serif") family = L"Segoe UI";
        else if (name == "monospace" || name == "ui-monospace") family = L"Consolas";
        else if (name == "cursive") family = L"Segoe Script";
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (fontfaces::has(family) || (SUCCEEDED(fonts->FindFamilyName(family.c_str(), &index, &exists)) && exists)) {
            face = family;
            break;
        }
    }
    fonts->Release();
    return face;
}

DWRITE_FONT_STRETCH stretchOf(float percent) {
    static const float steps[] = {50, 62.5f, 75, 87.5f, 100, 112.5f, 125, 150, 200};
    int best = 4;
    for (int i = 0; i < 9; ++i) {
        if (std::fabs(steps[i] - percent) < std::fabs(steps[best] - percent)) best = i;
    }
    return (DWRITE_FONT_STRETCH)(best + 1);
}

}  // namespace

bool describe(const Element& e, css::Element& d) {
    d = css::Element{};
    switch (e.type) {
        case ElementType::Paragraph:
            d.tag = "p";
            if (e.language == "footnote-backlinks") d.classes = {"footnote-backlinks"};
            break;
        case ElementType::Heading:
            d.tag = kHeadings[std::clamp(e.level, 1, 6) - 1];
            d.id = e.title;
            break;
        case ElementType::CodeBlock: d.tag = "pre"; break;
        case ElementType::MermaidDiagram:
            d.tag = "div";
            d.classes = {"diagram"};
            break;
        case ElementType::BlockQuote:
            d.tag = "blockquote";
            if (e.alertKind >= 1 && e.alertKind <= 5) d.classes = {kAlerts[e.alertKind - 1]};
            break;
        case ElementType::List: d.tag = e.ordered ? "ol" : "ul"; break;
        case ElementType::ListItem:
            d.tag = "li";
            if (e.isTask) {
                d.classes = {"task"};
                d.checked = e.taskChecked;
            }
            break;
        case ElementType::Table: d.tag = "table"; break;
        case ElementType::TableRow: d.tag = "tr"; break;
        case ElementType::TableCell: d.tag = "td"; break;
        case ElementType::HorizontalRule:
            d.tag = "hr";
            d.empty = true;
            break;
        case ElementType::HtmlBlock: d.tag = "div"; break;
        case ElementType::Properties:
            d.tag = "div";
            d.classes = {"frontmatter"};
            break;
        case ElementType::Footnotes:
            d.tag = "section";
            d.classes = {"footnotes"};
            break;
        case ElementType::FootnoteDefinition:
            d.tag = "div";
            d.classes = {"footnote"};
            d.id = e.url;
            break;
        case ElementType::Figure:
            d.tag = "figure";
            if (e.language == "tbl") d.classes = {"table"};
            d.id = e.title;
            break;
        case ElementType::Caption: d.tag = "figcaption"; break;
        case ElementType::Bibliography:
            d.tag = "div";
            d.id = "refs";
            d.classes = {"references", "csl-bib-body"};
            break;
        case ElementType::BibEntry:
            d.tag = "div";
            d.classes = {"csl-entry"};
            d.id = e.title;
            break;
        case ElementType::MathDisplay:
            d.tag = "div";
            d.classes = {"math-display"};
            break;
        case ElementType::Image:
            d.tag = "img";
            d.empty = true;
            break;
        case ElementType::Code: d.tag = "code"; break;
        case ElementType::Emphasis: d.tag = "em"; break;
        case ElementType::Strong: d.tag = "strong"; break;
        case ElementType::Link:
        case ElementType::CrossReference:
            d.tag = "a";
            d.attributes = {{"href", e.url}};
            break;
        case ElementType::Anchor:
            d.tag = "a";
            d.id = e.title;
            break;
        case ElementType::Ruby: d.tag = "ruby"; break;
        case ElementType::RubyText: d.tag = "rt"; break;
        case ElementType::Highlight: d.tag = "mark"; break;
        case ElementType::Superscript: d.tag = "sup"; break;
        case ElementType::Subscript: d.tag = "sub"; break;
        case ElementType::Strikethrough: d.tag = "del"; break;
        case ElementType::Underline: d.tag = "ins"; break;
        case ElementType::MathInline:
            d.tag = "span";
            d.classes = {"math-inline"};
            break;
        case ElementType::FootnoteReference:
            d.tag = "sup";
            d.id = e.title;
            break;
        case ElementType::FootnoteBacklink:
            d.tag = "a";
            d.attributes = {{"role", "doc-backlink"}, {"href", e.url}};
            break;
        case ElementType::Citation:
            d.tag = "span";
            d.classes = {"citation"};
            d.attributes = {{"data-cites", e.language}};
            break;
        default:
            return false;
    }
    if (d.tag != "hr" && d.tag != "img") d.empty = e.children.empty() && e.text.empty();
    d.index = d.count = d.typeIndex = d.typeCount = 1;
    return true;
}

void begin(App& app, float width) {
    std::shared_ptr<const css::Sheet> sheet = themeSheet(app.currentThemeIndex);
    if (!sheet) {
        if (app.pageState) {
            app.pageState->active = false;
            app.pageState->depth = 0;
        }
        return;
    }
    if (!app.pageState) app.pageState = std::make_shared<State>();
    State& s = *app.pageState;
    css::Media media{app.theme.isDark, app.printLayout, width};
    std::string base = baseSheet(app.theme);
    if (sheet != s.sheet || base != s.baseText) {
        std::vector<css::Diagnostic> ignored;
        s.base = css::parse(base, "base", css::Origin::Base, nullptr, ignored);
        s.baseText = std::move(base);
        s.sheet = sheet;
        s.engine.load({s.base, s.sheet}, media);
        s.media = media;
    } else if (media.dark != s.media.dark || media.print != s.media.print || media.width != s.media.width) {
        s.engine.setMedia(media);
        s.media = media;
    }
    s.active = true;
    s.colors = !app.printLayout || css::mentionsPrint(*sheet);
    s.gap = 0;
    s.depth = 0;
    Frame& root = s.push(nullptr);
    root.style = s.styleOf(s.engine.root(), nullptr);
    css::Element bodyElement;
    bodyElement.tag = "body";
    bodyElement.index = bodyElement.count = bodyElement.typeIndex = bodyElement.typeCount = 1;
    const Style rootStyle = root.style;
    Frame& page = s.push(app.root.get());
    page.style = s.styleOf(s.engine.child(rootStyle.context, bodyElement), &rootStyle);
}

bool active(const App& app) { return state(app) != nullptr; }

const Style& current(const App& app) {
    State* s = state(app);
    return s ? s->top().style : none();
}

const Style& body(const App& app) {
    State* s = state(app);
    return s && s->depth >= 2 ? s->frames[1]->style : none();
}

Block::Block(App& app, const Element* e) : app_(app) {
    State* s = state(app);
    if (!s) return;
    Frame& parent = s->top();
    const Style parentStyle = parent.style;
    css::Element d;
    const css::Element* previous = nullptr;
    size_t count = 0;
    bool placed = false;
    if (e && parent.element && e->parent == parent.element) {
        if (!parent.described) describeKids(parent);
        size_t k = findKid(parent, e);
        if (k != SIZE_MAX) {
            d = parent.kids[k];
            previous = parent.kids.data();
            count = k;
            placed = true;
        }
    }
    if (!placed && (!e || !describe(*e, d))) {
        // Text and the like: the block it sits in styles it
        s->push(e).style = parentStyle;
        pushed_ = true;
        return;
    }
    const css::Engine::Context context = s->engine.child(parentStyle.context, d, previous, count);
    Frame& f = s->push(e);
    f.style = s->styleOf(context, &parentStyle);
    pushed_ = true;
}

Block::Block(App& app, const css::Element& d, const css::Element* previous, size_t count, const Element* children)
    : app_(app) {
    State* s = state(app);
    if (!s) return;
    const Style parentStyle = s->top().style;
    const css::Engine::Context context = s->engine.child(parentStyle.context, d, previous, count);
    Frame& f = s->push(children);
    f.style = s->styleOf(context, &parentStyle);
    pushed_ = true;
}

Block::~Block() {
    if (!pushed_) return;
    State* s = app_.pageState.get();
    if (s && s->depth > 2) s->depth--;
}

const Style& Block::style() const { return pushed_ ? current(app_) : none(); }

Style child(App& app, const Style& parent, const css::Element& element, const css::Element* previous, size_t count) {
    State* s = state(app);
    if (!s || !parent) return parent;
    return s->styleOf(s->engine.child(parent.context, element, previous, count), &parent);
}

Style child(App& app, const Style& parent, const Element& element) {
    if (!parent) return parent;
    css::Element d;
    if (!describe(element, d)) return parent;
    return child(app, parent, d);
}

const css::ComputedStyle* pseudo(App& app, const Style& style, css::Pseudo which) {
    State* s = state(app);
    if (!s || !style) return nullptr;
    const css::ComputedStyle* p = s->engine.pseudo(style.context, which);
    // A print without print rules keeps Tinta's palette for markers too
    return p;
}

float px(const App& app, float css) { return css * app.contentScale * app.zoomFactor; }

D2D1_COLOR_F color(const css::Color& c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

D2D1_COLOR_F textColor(const Style& style, D2D1_COLOR_F fallback) {
    return style.has(Prop::Color) ? color(style->color) : fallback;
}

float before(App& app, float fallback) {
    State* s = state(app);
    const float scale = app.contentScale * app.zoomFactor;
    if (!s) return fallback * scale;
    const Style& style = s->top().style;
    if (!style.has(Prop::MarginTop)) {
        const float d = fallback * scale;
        s->gap += d;
        return d;
    }
    const float m = style->margin[0] * scale;
    if (m < 0) return m;
    const float add = m > s->gap ? m - s->gap : 0.0f;
    s->gap = std::max(s->gap, m);
    return add;
}

float after(App& app, float fallback) {
    State* s = state(app);
    const float scale = app.contentScale * app.zoomFactor;
    if (!s) return fallback * scale;
    const Style& style = s->top().style;
    const float d = style.has(Prop::MarginBottom) ? style->margin[2] * scale : fallback * scale;
    s->gap = std::max(0.0f, d);
    return d;
}

float afterContainer(App& app, float fallback) {
    State* s = state(app);
    const float scale = app.contentScale * app.zoomFactor;
    if (!s) return fallback * scale;
    const Style& style = s->top().style;
    if (!style.has(Prop::MarginBottom)) {
        const float d = fallback * scale;
        s->gap += d;
        return d;
    }
    const float m = style->margin[2] * scale;
    if (m < 0) return m;
    const float add = m > s->gap ? m - s->gap : 0.0f;
    s->gap = std::max(s->gap, m);
    return add;
}

void boundary(App& app) {
    if (State* s = state(app)) s->gap = 0;
}

IDWriteTextFormat* format(App& app, const Style& style, IDWriteTextFormat* fallback) {
    State* s = state(app);
    if (!s || !fallback || !style) return fallback;
    const bool family = style.has(Prop::FontFamily), size = style.has(Prop::FontSize),
               weight = style.has(Prop::FontWeight), italic = style.has(Prop::FontStyle),
               stretch = style.has(Prop::FontStretch);
    if (!family && !size && !weight && !italic && !stretch) return fallback;
    const FontKey own = keyOf(*s, fallback);
    FontKey key = own;
    if (family) {
        auto found = s->faces.find(style->fontFamily);
        if (found == s->faces.end()) found = s->faces.emplace(style->fontFamily, installedFace(app, style->fontFamily)).first;
        if (!found->second.empty()) key.family = found->second;
    }
    if (size) key.size = std::max(1.0f, style->fontSize * app.contentScale * app.zoomFactor);
    if (weight) key.weight = std::clamp(style->fontWeight, 1, 999);
    if (italic) {
        key.style = style->fontStyle == css::FontStyle::Italic    ? DWRITE_FONT_STYLE_ITALIC
                    : style->fontStyle == css::FontStyle::Oblique ? DWRITE_FONT_STYLE_OBLIQUE
                                                                  : DWRITE_FONT_STYLE_NORMAL;
    }
    if (stretch) key.stretch = (int)stretchOf(style->fontStretch);
    if (key == own) return fallback;
    IDWriteTextFormat* made = formatFor(app, *s, key);
    return made ? made : fallback;
}

IDWriteTextFormat* variant(App& app, IDWriteTextFormat* base, bool bold, bool italic) {
    State* s = app.pageState.get();
    if (!s || !base) return nullptr;
    auto found = s->made.find(base);
    if (found == s->made.end()) return nullptr;
    FontKey key = found->second;
    if (bold) key.weight = std::max(key.weight, 700);
    if (italic && key.style == DWRITE_FONT_STYLE_NORMAL) key.style = DWRITE_FONT_STYLE_ITALIC;
    if (key == found->second) return base;
    IDWriteTextFormat* made = formatFor(app, *s, key);
    return made ? made : base;
}

IDWriteTypography* typography(App& app, const Style& style, IDWriteTypography* fallback) {
    State* s = state(app);
    if (!s || !style || style->fontFeatures.empty()) return fallback;
    if (!style.has(Prop::FontFeatureSettings) && !style.has(Prop::FontVariantCaps) &&
        !style.has(Prop::FontVariantNumeric) && !style.has(Prop::FontVariantLigatures)) {
        return fallback;
    }
    Features features;
    if (fallback) {
        const UINT32 n = fallback->GetFontFeatureCount();
        for (UINT32 i = 0; i < n; ++i) {
            DWRITE_FONT_FEATURE f{};
            if (SUCCEEDED(fallback->GetFontFeature(i, &f))) features.push_back({(uint32_t)f.nameTag, f.parameter});
        }
    }
    for (const auto& f : style->fontFeatures) {
        const uint32_t value = (uint32_t)std::max(0.0f, f.value);
        auto it = std::find_if(features.begin(), features.end(), [&](const auto& x) { return x.first == f.tag; });
        if (it != features.end()) it->second = value;
        else features.push_back({f.tag, value});
    }
    auto found = s->typographies.find(features);
    if (found != s->typographies.end()) return found->second ? found->second : fallback;
    IDWriteTypography* made = nullptr;
    if (app.dwriteFactory && SUCCEEDED(app.dwriteFactory->CreateTypography(&made)) && made) {
        for (const auto& f : features) made->AddFontFeature({(DWRITE_FONT_FEATURE_TAG)f.first, f.second});
    }
    s->typographies.emplace(features, made);
    return made ? made : fallback;
}

float lineHeight(App& app, const Style& style, IDWriteTextFormat* format, float fallback) {
    if (!style.has(Prop::LineHeight) || !format) return fallback;
    if (style->lineHeightFactor > 0) return style->lineHeightFactor * format->GetFontSize();
    if (style->lineHeight > 0) return style->lineHeight * app.contentScale * app.zoomFactor;
    return fallback;
}

float letterSpacing(App& app, const Style& style) {
    return style.has(Prop::LetterSpacing) ? style->letterSpacing * app.contentScale * app.zoomFactor : 0.0f;
}

std::wstring transform(std::wstring text, const Style& style) {
    if (!style.has(Prop::TextTransform) || text.empty()) return text;
    switch (style->textTransform) {
        case css::TextTransform::Uppercase:
            CharUpperBuffW(text.data(), (DWORD)text.size());
            break;
        case css::TextTransform::Lowercase:
            CharLowerBuffW(text.data(), (DWORD)text.size());
            break;
        case css::TextTransform::Capitalize: {
            bool start = true;
            for (wchar_t& c : text) {
                if (std::iswalpha(c)) {
                    if (start) CharUpperBuffW(&c, 1);
                    start = false;
                } else {
                    start = std::iswspace(c) || c == L'-' || c == L'(' || c == L'"';
                }
            }
            break;
        }
        default:
            break;
    }
    return text;
}

void releaseFormats(App& app) {
    if (State* s = app.pageState.get()) {
        if (!s->formats.empty()) app.formatBaselines.clear();
        s->release();
    }
}

}  // namespace page
