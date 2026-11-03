// Properties of Tinta's CSS (css.h): each longhand's value computed, the
// shorthands split into longhands, and font features from font-variant.

#pragma optimize("s", on)

#include "css_internal.h"

#include <algorithm>
#include <cstring>

namespace css {
namespace {

void warn(const Env& env, std::string message) {
    if (env.warnings) env.warnings->push_back(std::move(message));
}

template <size_t N>
int keywordIndex(const Node& n, const char* const (&words)[N]) {
    if (!n.is(Tok::Ident)) return -1;
    for (size_t i = 0; i < N; ++i) {
        if (n.ident(words[i])) return (int)i;
    }
    return -1;
}

std::vector<Node> keyword(const char* word) {
    Node n;
    n.token.type = Tok::Ident;
    n.token.text = word;
    return {n};
}

bool fontSize(const Node& v, float parent, float root, float& out) {
    static const char* const words[] = {"xx-small", "x-small", "small", "medium", "large", "x-large",
                                        "xx-large", "xxx-large", "larger", "smaller"};
    static const float scale[] = {3.0f / 5, 3.0f / 4, 8.0f / 9, 1, 6.0f / 5, 3.0f / 2, 2, 3};
    int k = keywordIndex(v, words);
    if (k >= 8) out = k == 8 ? parent * 1.2f : parent / 1.2f;
    else if (k >= 0) out = 16 * scale[k];
    else return readLength(v, NumEnv{parent, root, parent}, out, false);
    return true;
}

bool fontWeight(const Node& v, int parent, int& out) {
    double n;
    if (v.ident("normal")) out = 400;
    else if (v.ident("bold")) out = 700;
    else if (v.ident("bolder")) out = parent < 350 ? 400 : parent < 550 ? 700 : 900;
    else if (v.ident("lighter")) out = parent < 100 ? parent : parent < 550 ? 100 : parent < 750 ? 400 : 700;
    else if (readNumber(v, n) && n >= 1 && n <= 1000) out = (int)std::lround(n);
    else return false;
    return true;
}

bool fontStyle(const std::vector<const Node*>& p, FontStyle& out) {
    double angle;
    if (p.size() == 1 && p[0]->ident("normal")) out = FontStyle::Normal;
    else if (p.size() == 1 && p[0]->ident("italic")) out = FontStyle::Italic;
    else if (p[0]->ident("oblique") && (p.size() == 1 || (p.size() == 2 && readAngle(*p[1], angle)))) out = FontStyle::Oblique;
    else return false;
    return true;
}

bool fontStretch(const Node& v, float& out) {
    static const char* const words[] = {"ultra-condensed", "extra-condensed", "condensed", "semi-condensed", "normal",
                                        "semi-expanded", "expanded", "extra-expanded", "ultra-expanded"};
    static const float percent[] = {50, 62.5f, 75, 87.5f, 100, 112.5f, 125, 150, 200};
    int k = keywordIndex(v, words);
    Dim d;
    if (k >= 0) out = percent[k];
    else if (readDim(v, NumEnv(), d) && d.kind == '%' && d.value >= 0) out = (float)d.value;
    else return false;
    return true;
}

bool fontFamily(const std::vector<Node>& value, std::vector<std::string>& out) {
    std::vector<std::string> families;
    for (const auto& p : splitParts(value)) {
        if (p.empty()) return false;
        if (p.size() == 1 && p[0]->is(Tok::String)) {
            families.push_back(p[0]->token.text);
            continue;
        }
        std::string name;
        for (const Node* n : p) {
            if (!n->is(Tok::Ident)) return false;
            if (!name.empty()) name += ' ';
            name += n->token.text;
        }
        if (p.size() == 1 && (cssWideKeyword(*p[0]) || p[0]->ident("default"))) return false;
        families.push_back(name);
    }
    out = std::move(families);
    return true;
}

bool featureTag(const Node& n, uint32_t& tag) {
    const std::string& t = n.token.text;
    if (!n.is(Tok::String) || t.size() != 4) return false;
    tag = 0;
    for (int i = 0; i < 4; ++i) {
        if (t[i] < 0x20 || t[i] > 0x7E) return false;
        tag |= (uint32_t)(unsigned char)t[i] << (8 * i);
    }
    return true;
}

bool featureList(const std::vector<Node>& value, bool variations, std::vector<FontFeature>& out) {
    std::vector<FontFeature> list;
    if (value.size() == 1 && value[0].ident("normal")) {
        out.clear();
        return true;
    }
    for (const auto& p : splitParts(value)) {
        FontFeature f;
        double n = 1;
        if (p.empty() || p.size() > 2 || !featureTag(*p[0], f.tag)) return false;
        if (variations) {
            if (p.size() != 2 || !readNumber(*p[1], n)) return false;
        } else if (p.size() == 2) {
            if (p[1]->ident("on")) n = 1;
            else if (p[1]->ident("off")) n = 0;
            else if (!p[1]->is(Tok::Number) || !p[1]->token.integer || p[1]->token.number < 0) return false;
            else n = p[1]->token.number;
        }
        f.value = (float)n;
        list.push_back(f);
    }
    out = std::move(list);
    return true;
}

// font-variant-numeric: lnum onum pnum tnum frac afrc ordn zero, one bit each
const char* const numericWords[] = {"lining-nums", "oldstyle-nums", "proportional-nums", "tabular-nums",
                                    "diagonal-fractions", "stacked-fractions", "ordinal", "slashed-zero"};
// font-variant-ligatures: on and off for liga+clig, dlig, hlig and calt
const char* const ligatureWords[] = {"common-ligatures", "no-common-ligatures", "discretionary-ligatures",
                                     "no-discretionary-ligatures", "historical-ligatures",
                                     "no-historical-ligatures", "contextual", "no-contextual"};
const char* const capsWords[] = {"normal", "small-caps", "all-small-caps", "petite-caps", "all-petite-caps",
                                 "unicase", "titling-caps"};
const uint16_t ligaturesNone = 0x100;

// Bits from keywords where each pair of bits excludes the other, or `none`
bool flagList(const std::vector<const Node*>& p, const char* const (&words)[8], bool pairs, uint16_t& out) {
    uint16_t flags = 0;
    for (const Node* n : p) {
        int k = keywordIndex(*n, words);
        if (k < 0) return false;
        uint16_t bit = (uint16_t)(1 << k), group = pairs || k < 6 ? (uint16_t)(3 << (k & ~1)) : bit;
        if (flags & group) return false;
        flags |= bit;
    }
    out = flags;
    return true;
}

bool lineStyle(const Node& v, LineStyle& out, const Env& env) {
    static const char* const words[] = {"none", "hidden", "solid", "dashed", "dotted", "double",
                                        "groove", "ridge", "inset", "outset"};
    int k = keywordIndex(v, words);
    if (k < 0) return false;
    static const LineStyle styles[] = {LineStyle::None, LineStyle::None, LineStyle::Solid, LineStyle::Dashed,
                                       LineStyle::Dotted, LineStyle::Double};
    if (k >= 6) warn(env, "`" + v.token.text + "` borders are drawn solid");
    out = k < 6 ? styles[k] : LineStyle::Solid;
    return true;
}

bool lineWidth(const Node& v, const NumEnv& num, float& out) {
    if (v.ident("thin")) out = 1;
    else if (v.ident("medium")) out = 3;
    else if (v.ident("thick")) out = 5;
    else return readLength(v, num, out, false);
    return true;
}

bool listStyleNamed(const Node& v, ListStyle& out, const Env& env) {
    static const char* const words[] = {"disc", "circle", "square", "decimal", "decimal-leading-zero",
                                        "lower-alpha", "upper-alpha", "lower-roman", "upper-roman",
                                        "lower-greek", "cjk-decimal", "none", "lower-latin", "upper-latin"};
    int k = keywordIndex(v, words);
    if (k >= 12) out = k == 12 ? ListStyle::LowerAlpha : ListStyle::UpperAlpha;
    else if (k >= 0) out = (ListStyle)k;
    else if (v.is(Tok::Ident) && !v.ident("default")) {
        warn(env, "the counter style `" + v.token.text + "` is drawn as decimal");
        out = ListStyle::Decimal;
    } else {
        return false;
    }
    return true;
}

bool readGradient(const Node& fn, const ColorEnv& colors, Gradient& out, const Env& env) {
    bool repeating = fn.function("repeating-linear-gradient");
    if (!fn.function("linear-gradient") && !repeating) return false;
    std::vector<std::vector<const Node*>> args = splitParts(fn.children);
    Gradient g;
    size_t first = 0;
    const std::vector<const Node*>& head = args[0];
    double degrees;
    if (head.size() == 1 && readAngle(*head[0], degrees)) {
        g.angle = (float)degrees;
        first = 1;
    } else if (!head.empty() && head[0]->ident("to")) {
        bool top = false, bottom = false, left = false, right = false;
        if (head.size() < 2 || head.size() > 3) return false;
        for (size_t i = 1; i < head.size(); ++i) {
            if (head[i]->ident("top")) top = true;
            else if (head[i]->ident("bottom")) bottom = true;
            else if (head[i]->ident("left")) left = true;
            else if (head[i]->ident("right")) right = true;
            else return false;
        }
        if ((top && bottom) || (left && right) || (head.size() == 3 && (top == bottom || left == right))) return false;
        if (top) g.angle = left ? 315.0f : right ? 45.0f : 0.0f;
        else if (bottom) g.angle = left ? 225.0f : right ? 135.0f : 180.0f;
        else g.angle = left ? 270.0f : 90.0f;
        first = 1;
    }
    for (size_t i = first; i < args.size(); ++i) {
        const std::vector<const Node*>& p = args[i];
        if (p.empty() || p.size() > 3) return false;
        GradientStop stop;
        if (!readColor(*p[0], colors, stop.color)) {
            Dim hint;
            if (p.size() == 1 && readDim(*p[0], NumEnv(), hint) && hint.kind != 'n' && hint.kind != 'a') {
                warn(env, "colour hints in gradients are not drawn");
                continue;
            }
            return false;
        }
        g.stops.push_back(stop);
        for (size_t k = 1; k < p.size(); ++k) {
            Dim d;
            if (!readDim(*p[k], NumEnv(), d) || d.kind == 'a') return false;
            if (k == 2) g.stops.push_back(stop);
            if (d.kind == '%') {
                g.stops.back().position = (float)(d.value / 100);
            } else if (d.kind == 'n' && d.value != 0) {
                return false;
            } else if (d.kind == 'l' && d.value != 0) {
                warn(env, "gradient stops at lengths are spread evenly; use percentages");
            } else {
                g.stops.back().position = 0;
            }
        }
    }
    if (g.stops.size() < 2) return false;
    if (repeating) warn(env, "repeating gradients are drawn once");
    out = std::move(g);
    return true;
}

bool backgroundImage(const std::vector<Node>& value, const ColorEnv& colors, std::optional<Gradient>& out,
                     const Env& env) {
    std::optional<Gradient> result;
    for (const auto& layer : splitParts(value)) {
        if (layer.size() != 1) return false;
        const Node& n = *layer[0];
        Gradient g;
        if (n.ident("none")) continue;
        if (readGradient(n, colors, g, env)) {
            if (result) warn(env, "only the first background layer is drawn");
            else result = std::move(g);
            continue;
        }
        if (n.is(Tok::Url) || n.function("url") || n.function("radial-gradient") || n.function("conic-gradient") ||
            n.function("repeating-radial-gradient") || n.function("repeating-conic-gradient") ||
            n.function("image-set")) {
            warn(env, "`" + (n.is(Tok::Url) ? std::string("url") : n.token.text) + "()` backgrounds are not drawn");
            continue;
        }
        return false;
    }
    out = std::move(result);
    return true;
}

bool content(const std::vector<Node>& value, std::vector<ContentPart>& out, const Env& env) {
    std::vector<const Node*> p = parts(value);
    out.clear();
    if (p.size() == 1 && (p[0]->ident("normal") || p[0]->ident("none"))) return true;
    for (const Node* n : p) {
        if (n->delim('/')) break;  // the alternative text after it
        ContentPart part;
        if (n->is(Tok::String)) {
            part.text = n->token.text;
        } else if (n->ident("open-quote") || n->ident("close-quote")) {
            part.text = n->ident("open-quote") ? "\xE2\x80\x9C" : "\xE2\x80\x9D";
        } else if (n->ident("no-open-quote") || n->ident("no-close-quote")) {
            continue;
        } else if (n->function("counter") || n->function("counters")) {
            bool many = n->function("counters");
            std::vector<std::vector<const Node*>> args = splitParts(n->children);
            size_t need = many ? 2 : 1;
            if (args.size() < need || args.size() > need + 1 || args[0].size() != 1 || !args[0][0]->is(Tok::Ident)) {
                return false;
            }
            part.kind = many ? ContentPart::Counters : ContentPart::Counter;
            part.text = args[0][0]->token.text;
            if (many) {
                if (args[1].size() != 1 || !args[1][0]->is(Tok::String)) return false;
                part.separator = args[1][0]->token.text;
            }
            if (args.size() > need && (args[need].size() != 1 || !listStyleNamed(*args[need][0], part.style, env))) {
                return false;
            }
        } else if (n->function("attr")) {
            std::vector<const Node*> a = parts(n->children);
            if (a.empty() || !a[0]->is(Tok::Ident)) return false;
            part.kind = ContentPart::Attr;
            part.text = toLower(a[0]->token.text);
        } else if (n->is(Tok::Url) || n->function("url")) {
            warn(env, "images in `content` are not drawn");
            continue;
        } else {
            return false;
        }
        out.push_back(std::move(part));
    }
    return true;
}

bool counters(const std::vector<Node>& value, int fallback, std::vector<std::pair<std::string, int>>& out) {
    std::vector<const Node*> p = parts(value);
    out.clear();
    if (p.size() == 1 && p[0]->ident("none")) return true;
    for (size_t i = 0; i < p.size(); ++i) {
        if (!p[i]->is(Tok::Ident) || p[i]->ident("none")) return false;
        std::string name = p[i]->token.text;
        int amount = fallback;
        if (i + 1 < p.size() && p[i + 1]->is(Tok::Number)) {
            if (!p[i + 1]->token.integer) return false;
            amount = (int)p[++i]->token.number;
        }
        out.emplace_back(std::move(name), amount);
    }
    return true;
}

bool shadows(const std::vector<Node>& value, const ColorEnv& colors, const NumEnv& num, std::vector<Shadow>& out) {
    out.clear();
    if (value.size() == 1 && value[0].ident("none")) return true;
    for (const auto& layer : splitParts(value)) {
        Shadow s;
        s.color = colors.current;
        float lengths[4];
        int count = 0;
        bool colored = false;
        for (const Node* n : layer) {
            float px;
            if (n->ident("inset") && !s.inset) s.inset = true;
            else if (count < 4 && readLength(*n, num, px)) lengths[count++] = px;
            else if (!colored && readColor(*n, colors, s.color)) colored = true;
            else return false;
        }
        if (count < 2 || (count > 2 && lengths[2] < 0)) return false;
        s.x = lengths[0];
        s.y = lengths[1];
        s.blur = count > 2 ? lengths[2] : 0;
        s.spread = count > 3 ? lengths[3] : 0;
        out.push_back(s);
    }
    return true;
}

}  // namespace

bool computeLonghand(Prop prop, const std::vector<Node>& value, const Env& env, Computed& out) {
    thread_local std::vector<const Node*> p;  // reused: this never calls itself
    p.clear();
    for (const Node& n : value) {
        if (!n.is(Tok::Space)) p.push_back(&n);
    }
    if (p.empty()) return false;
    const Computed& parent = env.parent ? *env.parent : initialStyle();
    ComputedStyle& s = out.s;
    NumEnv own{s.fontSize, env.rootFontSize, NAN};
    NumEnv wide{s.fontSize, env.rootFontSize, env.width};
    NumEnv byFont{s.fontSize, env.rootFontSize, s.fontSize};
    ColorEnv colors{env.dark, prop == Prop::Color ? parent.s.color : s.color};
    const Node& v = *p[0];
    bool one = p.size() == 1;
    float px;
    double number;
    Dim d;
    switch (prop) {
        case Prop::Color: return one && readColor(v, colors, s.color);
        case Prop::BackgroundColor: return one && readColor(v, colors, s.backgroundColor);
        case Prop::BackgroundImage: return backgroundImage(value, colors, s.backgroundImage, env);
        case Prop::FontFamily: return fontFamily(value, s.fontFamily);
        case Prop::FontSize: return one && fontSize(v, parent.s.fontSize, env.rootFontSize, s.fontSize);
        case Prop::FontWeight: return one && fontWeight(v, parent.s.fontWeight, s.fontWeight);
        case Prop::FontStyle: return fontStyle(p, s.fontStyle);
        case Prop::FontStretch: return one && fontStretch(v, s.fontStretch);
        case Prop::FontFeatureSettings: return featureList(value, false, out.settings);
        case Prop::FontVariationSettings: return featureList(value, true, s.fontVariations);
        case Prop::FontVariantCaps: {
            int k = keywordIndex(v, capsWords);
            if (!one || k < 0) return false;
            out.caps = (uint8_t)k;
            return true;
        }
        case Prop::FontVariantNumeric:
            if (one && v.ident("normal")) {
                out.numeric = 0;
                return true;
            }
            return flagList(p, numericWords, false, out.numeric);
        case Prop::FontVariantLigatures:
            if (one && (v.ident("normal") || v.ident("none"))) {
                out.ligatures = v.ident("none") ? ligaturesNone : 0;
                return true;
            }
            return flagList(p, ligatureWords, true, out.ligatures);
        case Prop::LineHeight:
            if (!one) return false;
            if (v.ident("normal")) {
                s.lineHeight = s.lineHeightFactor = 0;
            } else if (readNumber(v, number) && number >= 0) {
                s.lineHeightFactor = (float)number;
                s.lineHeight = (float)number * s.fontSize;
            } else if (readLength(v, byFont, px, false)) {
                s.lineHeight = px;
                s.lineHeightFactor = 0;
            } else {
                return false;
            }
            return true;
        case Prop::LetterSpacing:
            if (one && v.ident("normal")) s.letterSpacing = 0;
            else if (!one || !readLength(v, byFont, s.letterSpacing)) return false;
            return true;
        case Prop::TextAlign: {
            static const char* const words[] = {"start", "left", "right", "center", "justify", "end",
                                                "match-parent", "justify-all"};
            int k = keywordIndex(v, words);
            if (!one || k < 0) return false;
            s.textAlign = k < 5 ? (TextAlign)k : k == 5 ? TextAlign::Right : k == 6 ? parent.s.textAlign : TextAlign::Justify;
            return true;
        }
        case Prop::TextIndent: {
            bool length = false;
            for (const Node* n : p) {
                if (n->ident("hanging") || n->ident("each-line")) warn(env, "`" + n->token.text + "` indents are not drawn");
                else if (!length && readLength(*n, wide, s.textIndent)) length = true;
                else return false;
            }
            return length;
        }
        case Prop::TextTransform: {
            static const char* const words[] = {"none", "uppercase", "lowercase", "capitalize"};
            int k = keywordIndex(v, words);
            if (!one || k < 0) return false;
            s.textTransform = (TextTransform)k;
            return true;
        }
        case Prop::TextDecorationLine: {
            Decoration& dec = s.decoration;
            dec.underline = dec.overline = dec.lineThrough = false;
            if (one && v.ident("none")) return true;
            for (const Node* n : p) {
                bool* flag = n->ident("underline") ? &dec.underline : n->ident("overline") ? &dec.overline
                           : n->ident("line-through") ? &dec.lineThrough : nullptr;
                if (n->ident("blink")) continue;
                if (!flag || *flag) return false;
                *flag = true;
            }
            return true;
        }
        case Prop::TextDecorationColor: {
            Color c;
            if (!one || !readColor(v, colors, c)) return false;
            s.decoration.color = v.ident("currentcolor") ? std::optional<Color>() : c;
            return true;
        }
        case Prop::TextDecorationStyle: {
            static const char* const words[] = {"solid", "dashed", "dotted", "double", "wavy"};
            static const LineStyle styles[] = {LineStyle::Solid, LineStyle::Dashed, LineStyle::Dotted,
                                               LineStyle::Double, LineStyle::Wavy};
            int k = keywordIndex(v, words);
            if (!one || k < 0) return false;
            s.decoration.style = styles[k];
            return true;
        }
        case Prop::TextDecorationThickness:
        case Prop::TextUnderlineOffset: {
            float& field = prop == Prop::TextUnderlineOffset ? s.decoration.offset : s.decoration.thickness;
            if (one && (v.ident("auto") || (prop == Prop::TextDecorationThickness && v.ident("from-font")))) field = -1;
            else if (!one || !readLength(v, byFont, field)) return false;
            return true;
        }
        case Prop::BorderTopWidth: case Prop::BorderRightWidth:
        case Prop::BorderBottomWidth: case Prop::BorderLeftWidth:
            return one && lineWidth(v, own, s.border[((int)prop - (int)Prop::BorderTopWidth) / 3].width);
        case Prop::BorderTopStyle: case Prop::BorderRightStyle:
        case Prop::BorderBottomStyle: case Prop::BorderLeftStyle:
            return one && lineStyle(v, s.border[((int)prop - (int)Prop::BorderTopWidth) / 3].style, env);
        case Prop::BorderTopColor: case Prop::BorderRightColor:
        case Prop::BorderBottomColor: case Prop::BorderLeftColor:
            return one && readColor(v, colors, s.border[((int)prop - (int)Prop::BorderTopWidth) / 3].color);
        case Prop::BorderTopLeftRadius: case Prop::BorderTopRightRadius:
        case Prop::BorderBottomRightRadius: case Prop::BorderBottomLeftRadius: {
            if (p.size() > 2) return false;
            float& radius = s.radius[(int)prop - (int)Prop::BorderTopLeftRadius];
            for (const Node* n : p) {
                if (!readDim(*n, own, d) || d.value < 0 || (d.kind != 'l' && d.kind != '%' && !(d.kind == 'n' && d.value == 0))) {
                    return false;
                }
            }
            if (p.size() == 2) warn(env, "elliptical corners are drawn round");
            readDim(v, own, d);
            if (d.kind == '%' && d.value != 0) warn(env, "percentages in `border-radius` are not drawn");
            radius = d.kind == 'l' ? (float)d.value : 0;
            return true;
        }
        case Prop::MarginTop: case Prop::MarginRight: case Prop::MarginBottom: case Prop::MarginLeft: {
            float& margin = s.margin[(int)prop - (int)Prop::MarginTop];
            if (one && v.ident("auto")) margin = 0;
            else if (!one || !readLength(v, wide, margin)) return false;
            return true;
        }
        case Prop::PaddingTop: case Prop::PaddingRight: case Prop::PaddingBottom: case Prop::PaddingLeft:
            return one && readLength(v, wide, s.padding[(int)prop - (int)Prop::PaddingTop], false);
        case Prop::MaxWidth:
            if (!one) return false;
            if (v.ident("none")) {
                s.maxWidth = {Length::None, 0};
            } else if (v.ident("min-content") || v.ident("max-content") || v.ident("fit-content")) {
                warn(env, "`" + v.token.text + "` widths are not drawn");
                s.maxWidth = {Length::None, 0};
            } else if (v.is(Tok::Percentage) && v.token.number >= 0) {
                s.maxWidth = {Length::Percent, (float)v.token.number};
            } else if (readLength(v, wide, px, false)) {
                s.maxWidth = {Length::Px, px};
            } else {
                return false;
            }
            return true;
        case Prop::ListStyleType:
            if (!one) return false;
            if (v.is(Tok::String)) {
                s.listStyle = ListStyle::String;
                s.listString = v.token.text;
                return true;
            }
            s.listString.clear();
            return listStyleNamed(v, s.listStyle, env);
        case Prop::Content: return content(value, s.content, env);
        case Prop::CounterReset: return counters(value, 0, s.counterReset);
        case Prop::CounterIncrement: return counters(value, 1, s.counterIncrement);
        case Prop::Opacity:
            if (!one || !readDim(v, NumEnv(), d) || (d.kind != 'n' && d.kind != '%')) return false;
            s.opacity = (float)std::min(1.0, std::max(0.0, d.kind == '%' ? d.value / 100 : d.value));
            return true;
        case Prop::BoxShadow: return shadows(value, colors, own, s.boxShadow);
        case Prop::AccentColor:
        case Prop::CaretColor: {
            std::optional<Color>& field = prop == Prop::AccentColor ? s.accentColor : s.caretColor;
            Color c;
            if (one && v.ident("auto")) field.reset();
            else if (one && readColor(v, colors, c)) field = c;
            else return false;
            return true;
        }
        case Prop::ColorScheme: {
            bool light = false, dark = false;
            if (one && v.ident("normal")) {
                s.colorScheme = ColorScheme::Normal;
                return true;
            }
            for (const Node* n : p) {
                if (!n->is(Tok::Ident) || n->ident("normal") || cssWideKeyword(*n)) return false;
                light |= n->ident("light");
                dark |= n->ident("dark");
            }
            s.colorScheme = light && dark ? ColorScheme::LightDark : light ? ColorScheme::Light
                          : dark ? ColorScheme::Dark : ColorScheme::Normal;
            return true;
        }
        default: return false;
    }
}

// --- Shorthands -------------------------------------------------------------------------

namespace {

using Longhands = std::vector<std::pair<Prop, std::vector<Node>>>;

bool valid(Prop prop, const std::vector<Node>& value, const Env& quiet) {
    Computed scratch;
    return computeLonghand(prop, value, quiet, scratch);
}

void put(Longhands& out, Prop prop, std::vector<Node> value) {
    out.emplace_back(prop, value.empty() ? keyword("initial") : std::move(value));
}

// One to four values for top, right, bottom and left
bool box(const std::vector<Node>& value, const std::vector<Prop>& props, const Env& quiet, Longhands& out) {
    static const int sides[4][4] = {{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
    std::vector<const Node*> p = parts(value);
    if (p.empty() || p.size() > 4) return false;
    for (int i = 0; i < 4; ++i) {
        std::vector<Node> part{*p[sides[p.size() - 1][i]]};
        if (!valid(props[i], part, quiet)) return false;
        out.emplace_back(props[i], std::move(part));
    }
    return true;
}

// A border's width, style and colour, in any order
bool borderParts(const std::vector<Node>& value, const Env& quiet, std::vector<Node> (&got)[3]) {
    static const Prop props[3] = {Prop::BorderTopWidth, Prop::BorderTopStyle, Prop::BorderTopColor};
    std::vector<const Node*> p = parts(value);
    if (p.empty()) return false;
    for (const Node* n : p) {
        std::vector<Node> part{*n};
        int slot = 0;
        while (slot < 3 && !(got[slot].empty() && valid(props[slot], part, quiet))) ++slot;
        if (slot == 3) return false;
        got[slot] = std::move(part);
    }
    return true;
}

bool fontShorthand(const std::vector<Node>& value, const Env& env, const Env& quiet, Longhands& out) {
    static const char* const system[] = {"caption", "icon", "menu", "message-box", "small-caption", "status-bar"};
    std::vector<size_t> at;
    for (size_t i = 0; i < value.size(); ++i) {
        if (!value[i].is(Tok::Space)) at.push_back(i);
    }
    if (at.size() == 1 && keywordIndex(value[at[0]], system) >= 0) {
        warn(env, "system fonts are not supported");
        return false;
    }
    std::vector<Node> style, caps, weight, stretch, size, lineHeight;
    size_t k = 0;
    for (size_t prefix = 0; k < at.size() && prefix < 4; ++prefix, ++k) {
        const Node& n = value[at[k]];
        std::vector<Node> part{n};
        double angle;
        if (n.ident("normal")) continue;
        if (style.empty() && (n.ident("italic") || n.ident("oblique"))) {
            style = part;
            if (n.ident("oblique") && k + 1 < at.size() && value[at[k + 1]].is(Tok::Dimension) &&
                readAngle(value[at[k + 1]], angle)) {
                style.push_back(Node());
                style.push_back(value[at[++k]]);
            }
        } else if (caps.empty() && n.ident("small-caps")) {
            caps = part;
        } else if (weight.empty() && (n.is(Tok::Number) || n.ident("bold") || n.ident("bolder") ||
                                      n.ident("lighter")) && valid(Prop::FontWeight, part, quiet)) {
            weight = part;
        } else if (stretch.empty() && n.is(Tok::Ident) && valid(Prop::FontStretch, part, quiet)) {
            stretch = part;
        } else {
            break;
        }
    }
    if (k >= at.size()) return false;
    size = {value[at[k++]]};
    if (!valid(Prop::FontSize, size, quiet)) return false;
    if (k < at.size() && value[at[k]].delim('/')) {
        if (k + 1 >= at.size()) return false;
        lineHeight = {value[at[k + 1]]};
        if (!valid(Prop::LineHeight, lineHeight, quiet)) return false;
        k += 2;
    }
    if (k >= at.size()) return false;
    std::vector<Node> family(value.begin() + (ptrdiff_t)at[k], value.end());
    if (!valid(Prop::FontFamily, family, quiet)) return false;
    put(out, Prop::FontStyle, style);
    put(out, Prop::FontVariantCaps, caps);
    put(out, Prop::FontVariantNumeric, {});
    put(out, Prop::FontVariantLigatures, {});
    put(out, Prop::FontWeight, weight);
    put(out, Prop::FontStretch, stretch);
    put(out, Prop::FontSize, size);
    put(out, Prop::LineHeight, lineHeight);
    put(out, Prop::FontFamily, family);
    return true;
}

bool backgroundShorthand(const std::vector<Node>& value, const Env& env, const Env& quiet, Longhands& out) {
    static const char* const extras[] = {"left", "right", "top", "bottom", "center", "repeat", "repeat-x",
                                         "repeat-y", "no-repeat", "space", "round", "scroll", "fixed",
                                         "local", "border-box", "padding-box", "content-box", "text",
                                         "cover", "contain", "auto"};
    std::vector<std::vector<const Node*>> layers = splitParts(value);
    std::vector<Node> color, image;
    bool ignored = false;
    for (size_t l = 0; l < layers.size(); ++l) {
        bool hasImage = false;
        const std::vector<const Node*>& p = layers[l];
        if (p.empty()) return false;
        for (const Node* n : p) {
            std::vector<Node> part{*n};
            Dim d;
            if (!hasImage && !n->is(Tok::Ident) && valid(Prop::BackgroundImage, part, quiet)) {
                hasImage = true;
                if (image.empty()) image = part;
                else warn(env, "only the first background layer is drawn");
            } else if (!hasImage && n->ident("none")) {
                hasImage = true;
            } else if (l + 1 == layers.size() && color.empty() && valid(Prop::BackgroundColor, part, quiet)) {
                color = part;
            } else if (keywordIndex(*n, extras) >= 0 || n->delim('/') || readDim(*n, NumEnv(), d)) {
                ignored = true;
            } else {
                return false;
            }
        }
    }
    if (ignored) warn(env, "`background` positions, sizes and repeats are not drawn");
    put(out, Prop::BackgroundColor, color);
    put(out, Prop::BackgroundImage, image);
    return true;
}

bool decorationShorthand(const std::vector<Node>& value, const Env& quiet, Longhands& out) {
    static const char* const lines[] = {"none", "underline", "overline", "line-through", "blink"};
    std::vector<Node> line, style, color, thickness;
    for (const Node* n : parts(value)) {
        std::vector<Node> part{*n};
        if (keywordIndex(*n, lines) >= 0) {
            if (!line.empty()) line.push_back(Node());
            line.push_back(*n);
        } else if (style.empty() && valid(Prop::TextDecorationStyle, part, quiet)) {
            style = part;
        } else if (color.empty() && valid(Prop::TextDecorationColor, part, quiet)) {
            color = part;
        } else if (thickness.empty() && valid(Prop::TextDecorationThickness, part, quiet)) {
            thickness = part;
        } else {
            return false;
        }
    }
    if (line.empty() && style.empty() && color.empty() && thickness.empty()) return false;
    if (!line.empty() && !valid(Prop::TextDecorationLine, line, quiet)) return false;
    put(out, Prop::TextDecorationLine, line);
    put(out, Prop::TextDecorationStyle, style);
    put(out, Prop::TextDecorationColor, color);
    put(out, Prop::TextDecorationThickness, thickness);
    return true;
}

bool variantShorthand(const std::vector<Node>& value, const Env& env, const Env& quiet, Longhands& out) {
    static const char* const others[] = {"jis78", "jis83", "jis90", "jis04", "simplified", "traditional",
                                         "full-width", "proportional-width", "ruby", "sub", "super",
                                         "historical-forms"};
    static const char* const otherFunctions[] = {"stylistic", "styleset", "character-variant", "swash",
                                                 "ornaments", "annotation"};
    std::vector<const Node*> p = parts(value);
    std::vector<Node> caps, numeric, ligatures;
    if (p.size() == 1 && (p[0]->ident("normal") || p[0]->ident("none"))) {
        if (p[0]->ident("none")) ligatures = keyword("none");
    } else {
        bool ignored = false;
        for (const Node* n : p) {
            std::vector<Node>* list = keywordIndex(*n, capsWords) > 0 ? &caps
                                    : keywordIndex(*n, numericWords) >= 0 ? &numeric
                                    : keywordIndex(*n, ligatureWords) >= 0 ? &ligatures : nullptr;
            if (list == &caps && !caps.empty()) return false;
            if (list) {
                if (!list->empty()) list->push_back(Node());
                list->push_back(*n);
                continue;
            }
            bool known = keywordIndex(*n, others) >= 0;
            for (const char* name : otherFunctions) known |= n->function(name);
            if (!known) return false;
            ignored = true;
        }
        if (ignored) warn(env, "only the caps, numeric and ligature parts of `font-variant` are drawn");
        if ((!numeric.empty() && !valid(Prop::FontVariantNumeric, numeric, quiet)) ||
            (!ligatures.empty() && !valid(Prop::FontVariantLigatures, ligatures, quiet))) {
            return false;
        }
    }
    put(out, Prop::FontVariantCaps, caps);
    put(out, Prop::FontVariantNumeric, numeric);
    put(out, Prop::FontVariantLigatures, ligatures);
    return true;
}

bool listShorthand(const std::vector<Node>& value, const Env& env, const Env& quiet, Longhands& out) {
    std::vector<const Node*> p = parts(value);
    std::vector<Node> type;
    int nones = 0;
    bool position = false, image = false;
    if (p.empty() || p.size() > 3) return false;
    for (const Node* n : p) {
        std::vector<Node> part{*n};
        if (n->ident("none")) {
            ++nones;
        } else if (!position && (n->ident("inside") || n->ident("outside"))) {
            position = true;
            if (n->ident("inside")) warn(env, "`inside` list markers are drawn outside");
        } else if (!image && (n->is(Tok::Url) || n->function("url") || n->function("linear-gradient"))) {
            image = true;
            warn(env, "list-style images are not drawn");
        } else if (type.empty() && valid(Prop::ListStyleType, part, quiet)) {
            type = part;
        } else {
            return false;
        }
    }
    if (type.empty() && nones > 0) {
        type = keyword("none");
        --nones;
    }
    if (nones > (image ? 0 : 1)) return false;
    put(out, Prop::ListStyleType, type);
    return true;
}

}  // namespace

bool expandShorthand(int id, const std::vector<Node>& value, const Env& env, Longhands& out) {
    const Env& quiet = env;  // a failed attempt to read a part never warns
    switch (id) {
        case ShFont: return fontShorthand(value, env, quiet, out);
        case ShBackground: return backgroundShorthand(value, env, quiet, out);
        case ShTextDecoration: return decorationShorthand(value, quiet, out);
        case ShFontVariant: return variantShorthand(value, env, quiet, out);
        case ShListStyle: return listShorthand(value, env, quiet, out);
        case ShMargin:
        case ShPadding:
        case ShBorderWidth:
        case ShBorderStyle:
        case ShBorderColor: return box(value, longhands(id), quiet, out);
        case ShBorderRadius: {
            std::vector<Node> horizontal;
            for (const Node& n : value) {
                if (n.delim('/')) {
                    warn(env, "elliptical corners are drawn round");
                    break;
                }
                horizontal.push_back(n);
            }
            return box(horizontal, longhands(id), quiet, out);
        }
        case ShBorder:
        case ShBorderTop:
        case ShBorderRight:
        case ShBorderBottom:
        case ShBorderLeft: {
            std::vector<Node> got[3];
            if (!borderParts(value, quiet, got)) return false;
            const std::vector<Prop>& props = longhands(id);
            for (size_t i = 0; i < props.size(); ++i) put(out, props[i], got[i % 3]);
            return true;
        }
        default: return false;
    }
}

void copyLonghand(Prop prop, const Computed& from, Computed& to) {
    const ComputedStyle& f = from.s;
    ComputedStyle& t = to.s;
    int i = (int)prop;
    switch (prop) {
        case Prop::Color: t.color = f.color; break;
        case Prop::BackgroundColor: t.backgroundColor = f.backgroundColor; break;
        case Prop::BackgroundImage: t.backgroundImage = f.backgroundImage; break;
        case Prop::FontFamily: t.fontFamily = f.fontFamily; break;
        case Prop::FontSize: t.fontSize = f.fontSize; break;
        case Prop::FontWeight: t.fontWeight = f.fontWeight; break;
        case Prop::FontStyle: t.fontStyle = f.fontStyle; break;
        case Prop::FontStretch: t.fontStretch = f.fontStretch; break;
        case Prop::FontFeatureSettings: to.settings = from.settings; break;
        case Prop::FontVariationSettings: t.fontVariations = f.fontVariations; break;
        case Prop::FontVariantCaps: to.caps = from.caps; break;
        case Prop::FontVariantNumeric: to.numeric = from.numeric; break;
        case Prop::FontVariantLigatures: to.ligatures = from.ligatures; break;
        case Prop::LineHeight:
            t.lineHeight = f.lineHeight;
            t.lineHeightFactor = f.lineHeightFactor;
            break;
        case Prop::LetterSpacing: t.letterSpacing = f.letterSpacing; break;
        case Prop::TextAlign: t.textAlign = f.textAlign; break;
        case Prop::TextIndent: t.textIndent = f.textIndent; break;
        case Prop::TextTransform: t.textTransform = f.textTransform; break;
        case Prop::TextDecorationLine:
            t.decoration.underline = f.decoration.underline;
            t.decoration.overline = f.decoration.overline;
            t.decoration.lineThrough = f.decoration.lineThrough;
            break;
        case Prop::TextDecorationColor: t.decoration.color = f.decoration.color; break;
        case Prop::TextDecorationStyle: t.decoration.style = f.decoration.style; break;
        case Prop::TextDecorationThickness: t.decoration.thickness = f.decoration.thickness; break;
        case Prop::TextUnderlineOffset: t.decoration.offset = f.decoration.offset; break;
        case Prop::MaxWidth: t.maxWidth = f.maxWidth; break;
        case Prop::ListStyleType:
            t.listStyle = f.listStyle;
            t.listString = f.listString;
            break;
        case Prop::Content: t.content = f.content; break;
        case Prop::CounterReset: t.counterReset = f.counterReset; break;
        case Prop::CounterIncrement: t.counterIncrement = f.counterIncrement; break;
        case Prop::Opacity: t.opacity = f.opacity; break;
        case Prop::BoxShadow: t.boxShadow = f.boxShadow; break;
        case Prop::AccentColor: t.accentColor = f.accentColor; break;
        case Prop::CaretColor: t.caretColor = f.caretColor; break;
        case Prop::ColorScheme: t.colorScheme = f.colorScheme; break;
        default:
            if (prop >= Prop::BorderTopWidth && prop <= Prop::BorderLeftColor) {
                int side = (i - (int)Prop::BorderTopWidth) / 3, part = (i - (int)Prop::BorderTopWidth) % 3;
                if (part == 0) t.border[side].width = f.border[side].width;
                else if (part == 1) t.border[side].style = f.border[side].style;
                else t.border[side].color = f.border[side].color;
            } else if (prop >= Prop::BorderTopLeftRadius && prop <= Prop::BorderBottomLeftRadius) {
                t.radius[i - (int)Prop::BorderTopLeftRadius] = f.radius[i - (int)Prop::BorderTopLeftRadius];
            } else if (prop >= Prop::MarginTop && prop <= Prop::MarginLeft) {
                t.margin[i - (int)Prop::MarginTop] = f.margin[i - (int)Prop::MarginTop];
            } else if (prop >= Prop::PaddingTop && prop <= Prop::PaddingLeft) {
                t.padding[i - (int)Prop::PaddingTop] = f.padding[i - (int)Prop::PaddingTop];
            }
    }
}

void buildFeatures(Computed& out) {
    std::vector<FontFeature> list;
    auto add = [&](const char* name, float value) {
        uint32_t tag = (uint32_t)name[0] | (uint32_t)name[1] << 8 | (uint32_t)name[2] << 16 | (uint32_t)name[3] << 24;
        for (FontFeature& f : list) {
            if (f.tag == tag) {
                f.value = value;
                return;
            }
        }
        list.push_back({tag, value});
    };
    static const char* const capsTags[][2] = {{nullptr, nullptr}, {"smcp", nullptr}, {"smcp", "c2sc"},
                                              {"pcap", nullptr}, {"pcap", "c2pc"}, {"unic", nullptr},
                                              {"titl", nullptr}};
    for (const char* tag : capsTags[out.caps < 7 ? out.caps : 0]) {
        if (tag) add(tag, 1);
    }
    static const char* const numericTags[] = {"lnum", "onum", "pnum", "tnum", "frac", "afrc", "ordn", "zero"};
    for (int i = 0; i < 8; ++i) {
        if (out.numeric >> i & 1) add(numericTags[i], 1);
    }
    static const char* const ligatureTags[] = {"liga", "liga", "dlig", "dlig", "hlig", "hlig", "calt", "calt"};
    for (int i = 0; i < 8; ++i) {
        if (!(out.ligatures == ligaturesNone || (out.ligatures >> i & 1))) continue;
        if (out.ligatures == ligaturesNone && i % 2 == 0) continue;
        add(ligatureTags[i], i % 2 ? 0.0f : 1.0f);
        if (i < 2) add("clig", i % 2 ? 0.0f : 1.0f);
    }
    for (const FontFeature& f : out.settings) {
        bool replaced = false;
        for (FontFeature& existing : list) {
            if (existing.tag == f.tag) {
                existing.value = f.value;
                replaced = true;
            }
        }
        if (!replaced) list.push_back(f);
    }
    out.s.fontFeatures = std::move(list);
}

// Borders start medium; a side without a style computes to 0 wide
const Computed& initialStyle() {
    static const Computed initial = [] {
        Computed c;
        for (Border& b : c.s.border) b.width = 3;
        return c;
    }();
    return initial;
}

std::string ComputedStyle::custom(std::string_view name) const {
    if (!customs) return {};
    auto it = std::lower_bound(customs->begin(), customs->end(), name,
                               [](const std::pair<std::string, std::string>& e, std::string_view key) { return e.first < key; });
    return it != customs->end() && it->first == name ? it->second : std::string();
}

std::optional<Color> ComputedStyle::customColor(std::string_view name) const {
    std::vector<Node> nodes = parseNodes(custom(name));
    trimSpace(nodes);
    ColorEnv env{dark, color};
    Color out;
    if (nodes.size() == 1 && readColor(nodes[0], env, out)) return out;
    return std::nullopt;
}

}
