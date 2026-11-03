// Values for Tinta's CSS (css.h): numbers, lengths and angles with calc(),
// colours in every CSS Color 4 notation with color-mix() and light-dark(),
// and linear-gradient().

#pragma optimize("s", on)

#include "css_internal.h"

#include <algorithm>
#include <cstring>

namespace css {
namespace {

const double pi = 3.14159265358979323846;

bool lengthUnit(const std::string& unit, const NumEnv& env, double& px) {
    static const struct {
        const char* name;
        double px;
    } fixed[] = {{"px", 1}, {"pt", 96.0 / 72}, {"pc", 16}, {"in", 96},
                 {"cm", 96 / 2.54}, {"mm", 96 / 25.4}, {"q", 96 / 101.6}};
    for (const auto& u : fixed) {
        if (equalsLower(unit, u.name)) {
            px = u.px;
            return true;
        }
    }
    if (equalsLower(unit, "em")) px = env.em;
    else if (equalsLower(unit, "rem")) px = env.rem;
    else if (equalsLower(unit, "ex") || equalsLower(unit, "ch")) px = env.em * 0.5;  // without font metrics
    else return false;
    return true;
}

bool angleUnit(const std::string& unit, double& degrees) {
    if (equalsLower(unit, "deg")) degrees = 1;
    else if (equalsLower(unit, "grad")) degrees = 0.9;
    else if (equalsLower(unit, "rad")) degrees = 180 / pi;
    else if (equalsLower(unit, "turn")) degrees = 360;
    else return false;
    return true;
}

bool calcLeaf(const Node& n, const NumEnv& env, Dim& out, int depth);

bool calcProduct(const std::vector<const Node*>& p, size_t& i, const NumEnv& env, Dim& out, int depth) {
    if (i >= p.size() || !calcLeaf(*p[i++], env, out, depth)) return false;
    while (i < p.size() && (p[i]->delim('*') || p[i]->delim('/'))) {
        bool divide = p[i++]->delim('/');
        Dim rhs;
        if (i >= p.size() || !calcLeaf(*p[i++], env, rhs, depth)) return false;
        if (divide) {
            if (rhs.kind != 'n' || rhs.value == 0) return false;
            out.value /= rhs.value;
        } else if (rhs.kind == 'n') {
            out.value *= rhs.value;
        } else if (out.kind == 'n') {
            out.kind = rhs.kind;
            out.value *= rhs.value;
        } else {
            return false;
        }
    }
    return true;
}

bool calcSum(const std::vector<const Node*>& p, size_t& i, const NumEnv& env, Dim& out, int depth) {
    if (!calcProduct(p, i, env, out, depth)) return false;
    while (i < p.size() && (p[i]->delim('+') || p[i]->delim('-'))) {
        bool minus = p[i++]->delim('-');
        Dim rhs;
        if (!calcProduct(p, i, env, rhs, depth) || rhs.kind != out.kind) return false;
        out.value += minus ? -rhs.value : rhs.value;
    }
    return true;
}

bool calcWhole(const std::vector<const Node*>& p, const NumEnv& env, Dim& out, int depth) {
    size_t i = 0;
    return calcSum(p, i, env, out, depth) && i == p.size();
}

bool calcFunction(const Node& n, const NumEnv& env, Dim& out, int depth) {
    if (depth > 32) return false;
    if (n.function("calc")) return calcWhole(parts(n.children), env, out, depth + 1);
    bool isMin = n.function("min"), isMax = n.function("max"), isClamp = n.function("clamp");
    if (!isMin && !isMax && !isClamp) return false;
    std::vector<std::vector<const Node*>> args = splitParts(n.children);
    if (isClamp ? args.size() != 3 : args.empty()) return false;
    std::vector<Dim> values(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        if (!calcWhole(args[i], env, values[i], depth + 1) || values[i].kind != values[0].kind) return false;
    }
    out = values[0];
    if (isClamp) {
        out.value = std::max(values[0].value, std::min(values[1].value, values[2].value));
        return true;
    }
    for (const Dim& d : values) out.value = isMin ? std::min(out.value, d.value) : std::max(out.value, d.value);
    return true;
}

bool calcLeaf(const Node& n, const NumEnv& env, Dim& out, int depth) {
    if (n.is(Tok::OpenParen)) return calcWhole(parts(n.children), env, out, depth + 1);
    if (n.ident("pi") || n.ident("e")) {
        out = {n.ident("pi") ? pi : 2.71828182845904523536, 'n'};
        return true;
    }
    return readDim(n, env, out) || (n.is(Tok::Function) && calcFunction(n, env, out, depth + 1));
}

// --- Colour spaces -----------------------------------------------------------------------

struct Rgba {
    double r = 0, g = 0, b = 0, a = 1;
};

double toLinear(double c) {
    double v = std::fabs(c);
    v = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    return c < 0 ? -v : v;
}
double fromLinear(double c) {
    double v = std::fabs(c);
    v = v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
    return c < 0 ? -v : v;
}

void multiply(const double m[9], const double in[3], double out[3]) {
    double x = in[0], y = in[1], z = in[2];
    out[0] = m[0] * x + m[1] * y + m[2] * z;
    out[1] = m[3] * x + m[4] * y + m[5] * z;
    out[2] = m[6] * x + m[7] * y + m[8] * z;
}

const double linearToXyz[9] = {0.41239079926595950, 0.35758433938387796, 0.18048078840183430,
                               0.21263900587151036, 0.71516867876775590, 0.07219231536073371,
                               0.01933081871559185, 0.11919477979462599, 0.95053215224966060};
const double xyzToLinear[9] = {3.2409699419045226, -1.537383177570094, -0.4986107602930034,
                               -0.9692436362808796, 1.8759675015077202, 0.04155505740717559,
                               0.05563007969699366, -0.20397695888897652, 1.0569715142428786};
const double d65ToD50[9] = {1.0479297925449969, 0.022946870601609652, -0.05019226628920524,
                            0.02962780877005599, 0.9904344267538799, -0.017073799063418826,
                            -0.009243040646204504, 0.015055191490298152, 0.7518742814281371};
const double d50ToD65[9] = {0.955473421488075, -0.02309845494876471, 0.06325924320057072,
                            -0.0283697093338637, 1.0099953980813041, 0.021041441191917323,
                            0.012314014864481998, -0.020507649298898964, 1.330365926242124};
const double d50White[3] = {0.3457 / 0.3585, 1.0, (1.0 - 0.3457 - 0.3585) / 0.3585};
const double labKappa = 24389.0 / 27, labEpsilon = 216.0 / 24389;

void xyz50ToLab(const double xyz[3], double lab[3]) {
    double f[3];
    for (int i = 0; i < 3; ++i) {
        double v = xyz[i] / d50White[i];
        f[i] = v > labEpsilon ? std::cbrt(v) : (labKappa * v + 16) / 116;
    }
    lab[0] = 116 * f[1] - 16;
    lab[1] = 500 * (f[0] - f[1]);
    lab[2] = 200 * (f[1] - f[2]);
}

void labToXyz50(const double lab[3], double xyz[3]) {
    double fy = (lab[0] + 16) / 116, fx = lab[1] / 500 + fy, fz = fy - lab[2] / 200;
    double x = fx * fx * fx > labEpsilon ? fx * fx * fx : (116 * fx - 16) / labKappa;
    double y = lab[0] > labKappa * labEpsilon ? fy * fy * fy : lab[0] / labKappa;
    double z = fz * fz * fz > labEpsilon ? fz * fz * fz : (116 * fz - 16) / labKappa;
    xyz[0] = x * d50White[0];
    xyz[1] = y * d50White[1];
    xyz[2] = z * d50White[2];
}

void linearToOklab(const double rgb[3], double lab[3]) {
    double l = std::cbrt(0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2]);
    double m = std::cbrt(0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2]);
    double s = std::cbrt(0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2]);
    lab[0] = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    lab[1] = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    lab[2] = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

void oklabToLinear(const double lab[3], double rgb[3]) {
    double l = lab[0] + 0.3963377774 * lab[1] + 0.2158037573 * lab[2];
    double m = lab[0] - 0.1055613458 * lab[1] - 0.0638541728 * lab[2];
    double s = lab[0] - 0.0894841775 * lab[1] - 1.2914855480 * lab[2];
    l = l * l * l;
    m = m * m * m;
    s = s * s * s;
    rgb[0] = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    rgb[1] = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    rgb[2] = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
}

void hslToRgb(double hue, double sat, double light, double rgb[3]) {
    hue = std::fmod(hue, 360);
    if (hue < 0) hue += 360;
    for (int i = 0; i < 3; ++i) {
        double n = i == 0 ? 0 : i == 1 ? 8 : 4;
        double k = std::fmod(n + hue / 30, 12);
        double a = sat * std::min(light, 1 - light);
        rgb[i] = light - a * std::max(-1.0, std::min({k - 3, 9 - k, 1.0}));
    }
}

void hwbToRgb(double hue, double white, double black, double rgb[3]) {
    if (white + black >= 1) {
        double gray = white / (white + black);
        rgb[0] = rgb[1] = rgb[2] = gray;
        return;
    }
    hslToRgb(hue, 1, 0.5, rgb);
    for (int i = 0; i < 3; ++i) rgb[i] = rgb[i] * (1 - white - black) + white;
}

// Hue, saturation and lightness; the hue NaN when it has none
void rgbToHsl(const double rgb[3], double hsl[3]) {
    double mx = std::max({rgb[0], rgb[1], rgb[2]}), mn = std::min({rgb[0], rgb[1], rgb[2]});
    double l = (mx + mn) / 2, d = mx - mn, h = NAN, s = 0;
    if (d != 0) {
        s = (l == 0 || l == 1) ? 0 : (mx - l) / std::min(l, 1 - l);
        if (mx == rgb[0]) h = (rgb[1] - rgb[2]) / d + (rgb[1] < rgb[2] ? 6 : 0);
        else if (mx == rgb[1]) h = (rgb[2] - rgb[0]) / d + 2;
        else h = (rgb[0] - rgb[1]) / d + 4;
        h *= 60;
    }
    if (s < 0) {
        h += 180;
        s = -s;
    }
    if (h >= 360) h -= 360;
    hsl[0] = h;
    hsl[1] = s;
    hsl[2] = l;
}

enum Space { SRGB, SRGBLinear, Lab, Lch, OKLab, OKLch, HSL, HWB, XYZD65, XYZD50 };

int hueIndex(Space space) { return space == Lch || space == OKLch ? 2 : space == HSL || space == HWB ? 0 : -1; }

// sRGB to a space; a hue it does not have comes back NaN
void toSpace(Space space, const Rgba& c, double out[3]) {
    double rgb[3] = {c.r, c.g, c.b}, lin[3], xyz[3];
    for (int i = 0; i < 3; ++i) lin[i] = toLinear(rgb[i]);
    switch (space) {
        case SRGB: std::copy(rgb, rgb + 3, out); return;
        case SRGBLinear: std::copy(lin, lin + 3, out); return;
        case HSL: rgbToHsl(rgb, out); if (out[1] == 0) out[0] = NAN; return;
        case HWB: {
            double hsl[3];
            rgbToHsl(rgb, hsl);
            out[1] = std::min({rgb[0], rgb[1], rgb[2]});
            out[2] = 1 - std::max({rgb[0], rgb[1], rgb[2]});
            out[0] = out[1] + out[2] >= 1 - 1e-9 ? NAN : hsl[0];
            return;
        }
        case XYZD65: multiply(linearToXyz, lin, out); return;
        case XYZD50:
        case Lab:
        case Lch: {
            multiply(linearToXyz, lin, xyz);
            double d50[3];
            multiply(d65ToD50, xyz, d50);
            if (space == XYZD50) {
                std::copy(d50, d50 + 3, out);
                return;
            }
            xyz50ToLab(d50, out);
            break;
        }
        case OKLab:
        case OKLch: linearToOklab(lin, out); break;
    }
    if (space == Lch || space == OKLch) {
        double a = out[1], b = out[2], chroma = std::sqrt(a * a + b * b);
        double hue = std::atan2(b, a) * 180 / pi;
        if (hue < 0) hue += 360;
        out[1] = chroma;
        out[2] = chroma < (space == Lch ? 0.0015 : 0.000004) ? NAN : hue;
    }
}

Rgba fromSpace(Space space, const double in[3], double alpha) {
    double rgb[3], lin[3], xyz[3], lab[3];
    bool linear = true;
    switch (space) {
        case SRGB: std::copy(in, in + 3, rgb); linear = false; break;
        case SRGBLinear: std::copy(in, in + 3, lin); break;
        case HSL: hslToRgb(in[0], in[1], in[2], rgb); linear = false; break;
        case HWB: hwbToRgb(in[0], in[1], in[2], rgb); linear = false; break;
        case XYZD65: multiply(xyzToLinear, in, lin); break;
        case XYZD50: multiply(d50ToD65, in, xyz); multiply(xyzToLinear, xyz, lin); break;
        case Lab:
        case Lch: {
            std::copy(in, in + 3, lab);
            if (space == Lch) {
                double h = in[2] * pi / 180;
                lab[1] = in[1] * std::cos(h);
                lab[2] = in[1] * std::sin(h);
            }
            double d50[3];
            labToXyz50(lab, d50);
            multiply(d50ToD65, d50, xyz);
            multiply(xyzToLinear, xyz, lin);
            break;
        }
        case OKLab:
        case OKLch:
            std::copy(in, in + 3, lab);
            if (space == OKLch) {
                double h = in[2] * pi / 180;
                lab[1] = in[1] * std::cos(h);
                lab[2] = in[1] * std::sin(h);
            }
            oklabToLinear(lab, lin);
            break;
    }
    if (linear) {
        for (int i = 0; i < 3; ++i) rgb[i] = fromLinear(lin[i]);
    }
    return {rgb[0], rgb[1], rgb[2], alpha};
}

// --- Colour syntax --------------------------------------------------------------------------

struct Named {
    const char* name;
    uint32_t rgb;
};
const Named namedColors[] = {
    {"aliceblue", 0xf0f8ff}, {"antiquewhite", 0xfaebd7}, {"aqua", 0x00ffff}, {"aquamarine", 0x7fffd4},
    {"azure", 0xf0ffff}, {"beige", 0xf5f5dc}, {"bisque", 0xffe4c4}, {"black", 0x000000},
    {"blanchedalmond", 0xffebcd}, {"blue", 0x0000ff}, {"blueviolet", 0x8a2be2}, {"brown", 0xa52a2a},
    {"burlywood", 0xdeb887}, {"cadetblue", 0x5f9ea0}, {"chartreuse", 0x7fff00}, {"chocolate", 0xd2691e},
    {"coral", 0xff7f50}, {"cornflowerblue", 0x6495ed}, {"cornsilk", 0xfff8dc}, {"crimson", 0xdc143c},
    {"cyan", 0x00ffff}, {"darkblue", 0x00008b}, {"darkcyan", 0x008b8b}, {"darkgoldenrod", 0xb8860b},
    {"darkgray", 0xa9a9a9}, {"darkgreen", 0x006400}, {"darkgrey", 0xa9a9a9}, {"darkkhaki", 0xbdb76b},
    {"darkmagenta", 0x8b008b}, {"darkolivegreen", 0x556b2f}, {"darkorange", 0xff8c00},
    {"darkorchid", 0x9932cc}, {"darkred", 0x8b0000}, {"darksalmon", 0xe9967a},
    {"darkseagreen", 0x8fbc8f}, {"darkslateblue", 0x483d8b}, {"darkslategray", 0x2f4f4f},
    {"darkslategrey", 0x2f4f4f}, {"darkturquoise", 0x00ced1}, {"darkviolet", 0x9400d3},
    {"deeppink", 0xff1493}, {"deepskyblue", 0x00bfff}, {"dimgray", 0x696969}, {"dimgrey", 0x696969},
    {"dodgerblue", 0x1e90ff}, {"firebrick", 0xb22222}, {"floralwhite", 0xfffaf0},
    {"forestgreen", 0x228b22}, {"fuchsia", 0xff00ff}, {"gainsboro", 0xdcdcdc}, {"ghostwhite", 0xf8f8ff},
    {"gold", 0xffd700}, {"goldenrod", 0xdaa520}, {"gray", 0x808080}, {"green", 0x008000},
    {"greenyellow", 0xadff2f}, {"grey", 0x808080}, {"honeydew", 0xf0fff0}, {"hotpink", 0xff69b4},
    {"indianred", 0xcd5c5c}, {"indigo", 0x4b0082}, {"ivory", 0xfffff0}, {"khaki", 0xf0e68c},
    {"lavender", 0xe6e6fa}, {"lavenderblush", 0xfff0f5}, {"lawngreen", 0x7cfc00},
    {"lemonchiffon", 0xfffacd}, {"lightblue", 0xadd8e6}, {"lightcoral", 0xf08080},
    {"lightcyan", 0xe0ffff}, {"lightgoldenrodyellow", 0xfafad2}, {"lightgray", 0xd3d3d3},
    {"lightgreen", 0x90ee90}, {"lightgrey", 0xd3d3d3}, {"lightpink", 0xffb6c1},
    {"lightsalmon", 0xffa07a}, {"lightseagreen", 0x20b2aa}, {"lightskyblue", 0x87cefa},
    {"lightslategray", 0x778899}, {"lightslategrey", 0x778899}, {"lightsteelblue", 0xb0c4de},
    {"lightyellow", 0xffffe0}, {"lime", 0x00ff00}, {"limegreen", 0x32cd32}, {"linen", 0xfaf0e6},
    {"magenta", 0xff00ff}, {"maroon", 0x800000}, {"mediumaquamarine", 0x66cdaa},
    {"mediumblue", 0x0000cd}, {"mediumorchid", 0xba55d3}, {"mediumpurple", 0x9370db},
    {"mediumseagreen", 0x3cb371}, {"mediumslateblue", 0x7b68ee}, {"mediumspringgreen", 0x00fa9a},
    {"mediumturquoise", 0x48d1cc}, {"mediumvioletred", 0xc71585}, {"midnightblue", 0x191970},
    {"mintcream", 0xf5fffa}, {"mistyrose", 0xffe4e1}, {"moccasin", 0xffe4b5},
    {"navajowhite", 0xffdead}, {"navy", 0x000080}, {"oldlace", 0xfdf5e6}, {"olive", 0x808000},
    {"olivedrab", 0x6b8e23}, {"orange", 0xffa500}, {"orangered", 0xff4500}, {"orchid", 0xda70d6},
    {"palegoldenrod", 0xeee8aa}, {"palegreen", 0x98fb98}, {"paleturquoise", 0xafeeee},
    {"palevioletred", 0xdb7093}, {"papayawhip", 0xffefd5}, {"peachpuff", 0xffdab9},
    {"peru", 0xcd853f}, {"pink", 0xffc0cb}, {"plum", 0xdda0dd}, {"powderblue", 0xb0e0e6},
    {"purple", 0x800080}, {"rebeccapurple", 0x663399}, {"red", 0xff0000}, {"rosybrown", 0xbc8f8f},
    {"royalblue", 0x4169e1}, {"saddlebrown", 0x8b4513}, {"salmon", 0xfa8072},
    {"sandybrown", 0xf4a460}, {"seagreen", 0x2e8b57}, {"seashell", 0xfff5ee}, {"sienna", 0xa0522d},
    {"silver", 0xc0c0c0}, {"skyblue", 0x87ceeb}, {"slateblue", 0x6a5acd}, {"slategray", 0x708090},
    {"slategrey", 0x708090}, {"snow", 0xfffafa}, {"springgreen", 0x00ff7f}, {"steelblue", 0x4682b4},
    {"tan", 0xd2b48c}, {"teal", 0x008080}, {"thistle", 0xd8bfd8}, {"tomato", 0xff6347},
    {"turquoise", 0x40e0d0}, {"violet", 0xee82ee}, {"wheat", 0xf5deb3}, {"white", 0xffffff},
    {"whitesmoke", 0xf5f5f5}, {"yellow", 0xffff00}, {"yellowgreen", 0x9acd32},
};

bool namedColor(const std::string& name, Rgba& out) {
    std::string lower = toLower(name);
    auto it = std::lower_bound(std::begin(namedColors), std::end(namedColors), lower,
                               [](const Named& n, const std::string& key) { return std::strcmp(n.name, key.c_str()) < 0; });
    if (it == std::end(namedColors) || lower != it->name) return false;
    out = {((it->rgb >> 16) & 255) / 255.0, ((it->rgb >> 8) & 255) / 255.0, (it->rgb & 255) / 255.0, 1};
    return true;
}

bool hexColor(const std::string& hex, Rgba& out) {
    size_t n = hex.size();
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    int v[8];
    for (size_t i = 0; i < n; ++i) {
        char c = hex[i];
        if (c >= '0' && c <= '9') v[i] = c - '0';
        else if (c >= 'a' && c <= 'f') v[i] = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v[i] = c - 'A' + 10;
        else return false;
    }
    double ch[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < (n <= 4 ? n : n / 2); ++i) ch[i] = n <= 4 ? v[i] * 17 : v[2 * i] * 16 + v[2 * i + 1];
    out = {ch[0] / 255, ch[1] / 255, ch[2] / 255, ch[3] / 255};
    return true;
}

// A component: a number, a percentage scaled so 100% is `full`, or none
bool component(const Node* n, double full, double number, bool legacy, double& out) {
    if (n->ident("none")) {
        out = 0;
        return !legacy;
    }
    Dim d;
    if (!readDim(*n, NumEnv(), d)) return false;
    if (d.kind == 'n') out = d.value * number;
    else if (d.kind == '%') out = d.value / 100 * full;
    else return false;
    return true;
}

bool hue(const Node* n, bool legacy, double& out) {
    if (n->ident("none")) {
        out = 0;
        return !legacy;
    }
    Dim d;
    if (!readDim(*n, NumEnv(), d) || (d.kind != 'n' && d.kind != 'a')) return false;
    out = d.value;
    return true;
}

bool alphaValue(const Node* n, bool legacy, double& out) {
    if (!n) {
        out = 1;
        return true;
    }
    if (!component(n, 1, 1, legacy, out)) return false;
    out = std::min(1.0, std::max(0.0, out));
    return true;
}

// The parts of rgb() and friends: three values and an alpha, comma or space separated
bool colorArgs(const Node& fn, std::vector<const Node*>& values, const Node*& alpha, bool& legacy) {
    std::vector<const Node*> p = parts(fn.children);
    legacy = std::any_of(p.begin(), p.end(), [](const Node* n) { return n->is(Tok::Comma); });
    alpha = nullptr;
    if (legacy) {
        if (p.size() % 2 == 0) return false;
        for (size_t i = 0; i < p.size(); ++i) {
            if ((i % 2 == 1) != p[i]->is(Tok::Comma)) return false;
            if (i % 2 == 0) values.push_back(p[i]);
        }
        if (values.size() == 4) {
            alpha = values[3];
            values.pop_back();
        }
        return values.size() == 3;
    }
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i]->delim('/')) {
            if (i + 2 != p.size()) return false;
            alpha = p[i + 1];
            break;
        }
        values.push_back(p[i]);
    }
    return values.size() == 3;
}

bool readRgba(const Node& n, const ColorEnv& env, Rgba& out, int depth);

bool spaceNamed(const Node* n, Space& space) {
    static const struct {
        const char* name;
        Space space;
    } spaces[] = {{"srgb", SRGB}, {"srgb-linear", SRGBLinear}, {"lab", Lab}, {"lch", Lch},
                  {"oklab", OKLab}, {"oklch", OKLch}, {"hsl", HSL}, {"hwb", HWB},
                  {"xyz", XYZD65}, {"xyz-d65", XYZD65}, {"xyz-d50", XYZD50}};
    for (const auto& s : spaces) {
        if (n->ident(s.name)) {
            space = s.space;
            return true;
        }
    }
    return false;
}

bool colorMix(const Node& fn, const ColorEnv& env, Rgba& out, int depth) {
    std::vector<std::vector<const Node*>> args = splitParts(fn.children);
    if (args.size() != 3) return false;
    const std::vector<const Node*>& method = args[0];
    Space space;
    if (method.size() < 2 || !method[0]->ident("in") || !spaceNamed(method[1], space)) return false;
    char hueMethod = 's';
    if (method.size() == 4 && hueIndex(space) >= 0 && method[3]->ident("hue")) {
        if (method[2]->ident("longer")) hueMethod = 'l';
        else if (method[2]->ident("increasing")) hueMethod = 'i';
        else if (method[2]->ident("decreasing")) hueMethod = 'd';
        else if (!method[2]->ident("shorter")) return false;
    } else if (method.size() != 2) {
        return false;
    }
    Rgba colors[2];
    double share[2] = {NAN, NAN};
    for (int i = 0; i < 2; ++i) {
        const std::vector<const Node*>& p = args[i + 1];
        if (p.empty() || p.size() > 2) return false;
        bool found = false;
        for (const Node* part : p) {
            if (part->is(Tok::Percentage) && std::isnan(share[i])) {
                share[i] = part->token.number;
                if (share[i] < 0 || share[i] > 100) return false;
            } else if (!found && readRgba(*part, env, colors[i], depth + 1)) {
                found = true;
            } else {
                return false;
            }
        }
        if (!found) return false;
    }
    if (std::isnan(share[0]) && std::isnan(share[1])) share[0] = share[1] = 50;
    else if (std::isnan(share[0])) share[0] = 100 - share[1];
    else if (std::isnan(share[1])) share[1] = 100 - share[0];
    double sum = share[0] + share[1];
    if (sum <= 0) return false;
    double multiplier = sum < 100 ? sum / 100 : 1;
    double p0 = share[0] / sum, p1 = share[1] / sum;
    double a[3], b[3];
    toSpace(space, colors[0], a);
    toSpace(space, colors[1], b);
    int h = hueIndex(space);
    if (h >= 0) {
        if (std::isnan(a[h])) a[h] = std::isnan(b[h]) ? 0 : b[h];
        if (std::isnan(b[h])) b[h] = a[h];
        double d = b[h] - a[h];
        if (hueMethod == 's') {
            if (d > 180) a[h] += 360;
            else if (d < -180) b[h] += 360;
        } else if (hueMethod == 'l') {
            if (d > 0 && d < 180) a[h] += 360;
            else if (d > -180 && d <= 0) b[h] += 360;
        } else if (hueMethod == 'i') {
            if (d < 0) b[h] += 360;
        } else if (d > 0) {
            a[h] += 360;
        }
    }
    double alpha = colors[0].a * p0 + colors[1].a * p1, mixed[3];
    for (int i = 0; i < 3; ++i) {
        if (i == h) {
            mixed[i] = std::fmod(a[i] * p0 + b[i] * p1, 360);
        } else {
            mixed[i] = a[i] * colors[0].a * p0 + b[i] * colors[1].a * p1;
            if (alpha > 0) mixed[i] /= alpha;
        }
    }
    out = fromSpace(space, mixed, alpha * multiplier);
    return true;
}

bool colorFunction(const Node& fn, const ColorEnv& env, Rgba& out, int depth) {
    const std::string& name = fn.token.text;
    if (equalsLower(name, "color-mix")) return colorMix(fn, env, out, depth);
    if (equalsLower(name, "light-dark")) {
        std::vector<std::vector<const Node*>> args = splitParts(fn.children);
        Rgba light, dark;
        if (args.size() != 2 || args[0].size() != 1 || args[1].size() != 1 ||
            !readRgba(*args[0][0], env, light, depth + 1) || !readRgba(*args[1][0], env, dark, depth + 1)) {
            return false;
        }
        out = env.dark ? dark : light;
        return true;
    }
    std::vector<const Node*> v;
    const Node* alphaNode;
    bool legacy;
    if (!colorArgs(fn, v, alphaNode, legacy) || v[0]->ident("from")) return false;
    double c[3], alpha;
    if (!alphaValue(alphaNode, legacy, alpha)) return false;
    bool rgb = equalsLower(name, "rgb") || equalsLower(name, "rgba");
    bool hsl = equalsLower(name, "hsl") || equalsLower(name, "hsla");
    if (rgb) {
        for (int i = 0; i < 3; ++i) {
            if (!component(v[i], 1, 1.0 / 255, legacy, c[i])) return false;
        }
        out = {c[0], c[1], c[2], alpha};
        return true;
    }
    if (hsl || equalsLower(name, "hwb")) {
        if (!hue(v[0], legacy, c[0])) return false;
        for (int i = 1; i < 3; ++i) {
            if ((legacy && !v[i]->is(Tok::Percentage)) || !component(v[i], 1, 0.01, legacy, c[i])) return false;
        }
        if (!hsl && legacy) return false;
        double rgbOut[3];
        if (hsl) hslToRgb(c[0], std::max(0.0, c[1]), c[2], rgbOut);
        else hwbToRgb(c[0], c[1], c[2], rgbOut);
        out = {rgbOut[0], rgbOut[1], rgbOut[2], alpha};
        return true;
    }
    if (legacy) return false;
    bool ok = equalsLower(name, "lab") || equalsLower(name, "lch");
    bool okl = equalsLower(name, "oklab") || equalsLower(name, "oklch");
    if (!ok && !okl) return false;
    bool polar = equalsLower(name, "lch") || equalsLower(name, "oklch");
    double lightFull = ok ? 100 : 1, axisFull = ok ? 125 : 0.4, chromaFull = ok ? 150 : 0.4;
    if (!component(v[0], lightFull, 1, false, c[0])) return false;
    c[0] = std::min(lightFull, std::max(0.0, c[0]));
    if (polar) {
        if (!component(v[1], chromaFull, 1, false, c[1]) || !hue(v[2], false, c[2])) return false;
        c[1] = std::max(0.0, c[1]);
    } else if (!component(v[1], axisFull, 1, false, c[1]) || !component(v[2], axisFull, 1, false, c[2])) {
        return false;
    }
    out = fromSpace(ok ? (polar ? Lch : Lab) : (polar ? OKLch : OKLab), c, alpha);
    return true;
}

bool readRgba(const Node& n, const ColorEnv& env, Rgba& out, int depth) {
    if (depth > 16) return false;
    switch (n.token.type) {
        case Tok::Hash: return hexColor(n.token.text, out);
        case Tok::Ident:
            if (n.ident("transparent")) {
                out = {0, 0, 0, 0};
                return true;
            }
            if (n.ident("currentcolor")) {
                out = {env.current.r, env.current.g, env.current.b, env.current.a};
                return true;
            }
            return namedColor(n.token.text, out);
        case Tok::Function: return colorFunction(n, env, out, depth);
        default: return false;
    }
}

float clamp01(double v) { return (float)std::min(1.0, std::max(0.0, v)); }

}  // namespace

bool readDim(const Node& node, const NumEnv& env, Dim& out) {
    const Token& t = node.token;
    switch (t.type) {
        case Tok::Number: out = {t.number, 'n'}; return true;
        case Tok::Percentage:
            if (std::isnan(env.percent)) out = {t.number, '%'};
            else out = {t.number / 100 * env.percent, 'l'};
            return true;
        case Tok::Dimension: {
            double scale;
            if (lengthUnit(t.text, env, scale)) out = {t.number * scale, 'l'};
            else if (angleUnit(t.text, scale)) out = {t.number * scale, 'a'};
            else return false;
            return true;
        }
        case Tok::Function: return calcFunction(node, env, out, 0);
        default: return false;
    }
}

bool readLength(const Node& node, const NumEnv& env, float& px, bool negative) {
    Dim d;
    if (!readDim(node, env, d)) return false;
    if (d.kind == 'n' && d.value == 0 && node.is(Tok::Number)) d.kind = 'l';
    if (d.kind != 'l' || (!negative && d.value < 0) || !std::isfinite(d.value)) return false;
    px = (float)d.value;
    return true;
}

bool readNumber(const Node& node, double& out) {
    Dim d;
    if (!readDim(node, NumEnv(), d) || d.kind != 'n') return false;
    out = d.value;
    return true;
}

bool readAngle(const Node& node, double& degrees) {
    Dim d;
    if (!readDim(node, NumEnv(), d)) return false;
    if (d.kind == 'n' && d.value == 0) d.kind = 'a';
    if (d.kind != 'a') return false;
    degrees = d.value;
    return true;
}

bool readColor(const Node& node, const ColorEnv& env, Color& out) {
    Rgba c;
    if (!readRgba(node, env, c, 0)) return false;
    out = {clamp01(c.r), clamp01(c.g), clamp01(c.b), clamp01(c.a)};
    return true;
}

std::optional<Color> parseColor(std::string_view text, bool dark) {
    std::vector<Node> nodes = parseNodes(text);
    trimSpace(nodes);
    if (nodes.size() != 1 || nodes[0].ident("currentcolor")) return std::nullopt;
    ColorEnv env;
    env.dark = dark;
    Color out;
    if (readColor(nodes[0], env, out)) return out;
    Rgba hex;
    std::string bare(text);
    while (!bare.empty() && (bare.back() == ' ' || bare.back() == '\t')) bare.pop_back();
    while (!bare.empty() && (bare[0] == ' ' || bare[0] == '\t')) bare.erase(0, 1);
    if (hexColor(bare, hex)) return Color{clamp01(hex.r), clamp01(hex.g), clamp01(hex.b), clamp01(hex.a)};
    return std::nullopt;
}

}
