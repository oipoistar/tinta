#include "math_render.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Box model
// ---------------------------------------------------------------------------

namespace {

struct MathRun {
    IDWriteTextLayout* layout = nullptr;  // owned by the MathBox
    float x = 0, y = 0;                   // top-left, box-relative
};

struct MathRule {   // fraction bars, overlines, radical bars
    float x = 0, y = 0, w = 0, h = 0;
};

struct MathLine {   // arrowheads
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

} // namespace

struct MathBox {
    std::vector<MathRun> runs;
    std::vector<MathRule> rules;
    std::vector<MathLine> lines;
    float width = 0, height = 0, baseline = 0;
    ~MathBox() {
        for (auto& r : runs) {
            if (r.layout) r.layout->Release();
        }
    }
};

namespace {

// ---------------------------------------------------------------------------
// Parse tree
// ---------------------------------------------------------------------------

struct MNode;
using MNodePtr = std::shared_ptr<MNode>;

struct MNode {
    enum Kind {
        Row,     // kids in sequence
        Sym,     // text (variable, number, operator, mapped symbol)
        Frac,    // kids = {num, den}
        Script,  // kids = {base, sub-or-null, sup-or-null}
        Delim,   // \left..\right, kids = {content}
        Deco,    // kids = {content}; decoKind below
        Space,   // explicit spacing; scale in "space"
    } kind = Row;

    std::wstring text;
    std::vector<MNodePtr> kids;
    wchar_t open = 0, close = 0;   // Delim
    int decoKind = 0;              // 1 overline/bar 2 overrightarrow/vec 3 sqrt 4 hat
    bool roman = false;            // upright (functions, \mathrm, \text)
    bool bold = false;             // \mathbf
    float space = 0.0f;            // Space: width in em
};

MNodePtr mk(MNode::Kind k) {
    auto n = std::make_shared<MNode>();
    n->kind = k;
    return n;
}

// ---------------------------------------------------------------------------
// Command tables
// ---------------------------------------------------------------------------

struct CmdSym { const wchar_t* name; const wchar_t* text; };

// Symbols and greek letters mapped straight to Unicode text runs
const CmdSym kSymbols[] = {
    {L"alpha", L"\u03B1"}, {L"beta", L"\u03B2"}, {L"gamma", L"\u03B3"},
    {L"delta", L"\u03B4"}, {L"epsilon", L"\u03B5"}, {L"varepsilon", L"\u03B5"},
    {L"zeta", L"\u03B6"}, {L"eta", L"\u03B7"}, {L"theta", L"\u03B8"},
    {L"vartheta", L"\u03D1"}, {L"iota", L"\u03B9"}, {L"kappa", L"\u03BA"},
    {L"lambda", L"\u03BB"}, {L"mu", L"\u03BC"}, {L"nu", L"\u03BD"},
    {L"xi", L"\u03BE"}, {L"pi", L"\u03C0"}, {L"rho", L"\u03C1"},
    {L"sigma", L"\u03C3"}, {L"tau", L"\u03C4"}, {L"upsilon", L"\u03C5"},
    {L"phi", L"\u03C6"}, {L"varphi", L"\u03C6"}, {L"chi", L"\u03C7"},
    {L"psi", L"\u03C8"}, {L"omega", L"\u03C9"},
    {L"Gamma", L"\u0393"}, {L"Delta", L"\u0394"}, {L"Theta", L"\u0398"},
    {L"Lambda", L"\u039B"}, {L"Xi", L"\u039E"}, {L"Pi", L"\u03A0"},
    {L"Sigma", L"\u03A3"}, {L"Upsilon", L"\u03A5"}, {L"Phi", L"\u03A6"},
    {L"Psi", L"\u03A8"}, {L"Omega", L"\u03A9"},
    {L"in", L"\u2208"}, {L"notin", L"\u2209"}, {L"ni", L"\u220B"},
    {L"cup", L"\u222A"}, {L"cap", L"\u2229"},
    {L"subset", L"\u2282"}, {L"supset", L"\u2283"},
    {L"subseteq", L"\u2286"}, {L"supseteq", L"\u2287"},
    {L"geq", L"\u2265"}, {L"ge", L"\u2265"}, {L"leq", L"\u2264"},
    {L"le", L"\u2264"}, {L"neq", L"\u2260"}, {L"ne", L"\u2260"},
    {L"equiv", L"\u2261"}, {L"approx", L"\u2248"}, {L"sim", L"\u223C"},
    {L"propto", L"\u221D"}, {L"pm", L"\u00B1"}, {L"mp", L"\u2213"},
    {L"times", L"\u00D7"}, {L"div", L"\u00F7"}, {L"cdot", L"\u22C5"},
    {L"cdots", L"\u22EF"}, {L"ldots", L"\u2026"}, {L"dots", L"\u2026"},
    {L"vdots", L"\u22EE"}, {L"ddots", L"\u22F1"},
    {L"mid", L"\u2223"}, {L"parallel", L"\u2225"}, {L"perp", L"\u22A5"},
    {L"angle", L"\u2220"}, {L"triangle", L"\u25B3"},
    {L"bigtriangleup", L"\u25B3"}, {L"square", L"\u25A1"},
    {L"infty", L"\u221E"}, {L"partial", L"\u2202"}, {L"nabla", L"\u2207"},
    {L"forall", L"\u2200"}, {L"exists", L"\u2203"},
    {L"emptyset", L"\u2205"}, {L"varnothing", L"\u2205"},
    {L"because", L"\u2235"}, {L"therefore", L"\u2234"},
    {L"to", L"\u2192"}, {L"rightarrow", L"\u2192"},
    {L"leftarrow", L"\u2190"}, {L"Rightarrow", L"\u21D2"},
    {L"Leftarrow", L"\u21D0"}, {L"Leftrightarrow", L"\u21D4"},
    {L"leftrightarrow", L"\u2194"}, {L"mapsto", L"\u21A6"},
    {L"circ", L"\u2218"}, {L"bullet", L"\u2219"}, {L"star", L"\u22C6"},
    {L"oplus", L"\u2295"}, {L"otimes", L"\u2297"},
    {L"sum", L"\u2211"}, {L"prod", L"\u220F"}, {L"int", L"\u222B"},
    {L"oint", L"\u222E"}, {L"sqrt", nullptr},  // structural, handled apart
    {L"prime", L"\u2032"}, {L"degree", L"\u00B0"},
    {L"lfloor", L"\u230A"}, {L"rfloor", L"\u230B"},
    {L"lceil", L"\u2308"}, {L"rceil", L"\u2309"},
    {L"langle", L"\u27E8"}, {L"rangle", L"\u27E9"},
    {L"lbrack", L"["}, {L"rbrack", L"]"},
    {L"lbrace", L"{"}, {L"rbrace", L"}"},
    {L"lvert", L"|"}, {L"rvert", L"|"}, {L"vert", L"|"},
    {L"lVert", L"\u2016"}, {L"rVert", L"\u2016"}, {L"Vert", L"\u2016"},
    {L"backslash", L"\\"}, {L"setminus", L"\u2216"},
    {L"hbar", L"\u210F"}, {L"ell", L"\u2113"}, {L"Re", L"\u211C"},
    {L"Im", L"\u2111"}, {L"aleph", L"\u2135"}, {L"wp", L"\u2118"},
    {L"neg", L"\u00AC"}, {L"lnot", L"\u00AC"},
    {L"land", L"\u2227"}, {L"wedge", L"\u2227"},
    {L"lor", L"\u2228"}, {L"vee", L"\u2228"},
};

// Function names rendered upright with a trailing thin space
const wchar_t* kFunctions[] = {
    L"log", L"ln", L"lg", L"sin", L"cos", L"tan", L"cot", L"sec", L"csc",
    L"arcsin", L"arccos", L"arctan", L"sinh", L"cosh", L"tanh", L"coth",
    L"lim", L"limsup", L"liminf", L"max", L"min", L"sup", L"inf",
    L"gcd", L"det", L"dim", L"ker", L"deg", L"arg", L"exp", L"mod", L"Pr",
};

// \mathbb single letters -> double-struck
const CmdSym kBlackboard[] = {
    {L"R", L"\u211D"}, {L"N", L"\u2115"}, {L"Z", L"\u2124"},
    {L"Q", L"\u211A"}, {L"C", L"\u2102"}, {L"P", L"\u2119"},
    {L"E", L"\U0001D53C"}, {L"F", L"\U0001D53D"},
};

const wchar_t* lookupSymbol(const std::wstring& cmd) {
    for (const auto& s : kSymbols) {
        if (cmd == s.name) return s.text;
    }
    return nullptr;
}

bool isFunctionName(const std::wstring& cmd) {
    for (const auto* f : kFunctions) {
        if (cmd == f) return true;
    }
    return false;
}

// Spacing classes for row layout (fractions of an em on each side)
float symbolSpacing(const std::wstring& t, bool roman) {
    if (t.size() != 1) {
        if (t == L"\u22EF" || t == L"\u2026") return 0.16f;
        // Upright function names (sin, log, ...) take thin spaces so
        // \sin m\alpha reads "sin m\u03B1" rather than "sinm\u03B1"
        if (roman) {
            bool allAlpha = true;
            for (wchar_t c : t) {
                if (!iswalpha(c)) { allAlpha = false; break; }
            }
            if (allAlpha) return 0.16f;
        }
        return 0.0f;
    }
    wchar_t c = t[0];
    switch (c) {
        case L'=': case L'<': case L'>':
        case 0x2264: case 0x2265: case 0x2260: case 0x2261: case 0x2248:
        case 0x2208: case 0x2209: case 0x2282: case 0x2283: case 0x2286:
        case 0x2287: case 0x2192: case 0x21D2: case 0x2194: case 0x21D4:
        case 0x223C: case 0x221D: case 0x2223: case 0x2225:
            return 0.26f;   // relations
        case L'+': case 0x2212: case 0x00B1: case 0x2213: case 0x00D7:
        case 0x00F7: case 0x22C5: case 0x222A: case 0x2229: case 0x2227:
        case 0x2228: case 0x2216:
            return 0.2f;    // binary operators
        case L',': case L';':
            return 0.12f;   // punctuation (space after only, approximated)
        default:
            return 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Tokenizer + parser
// ---------------------------------------------------------------------------

struct Parser {
    const std::wstring& src;
    size_t pos = 0;
    bool failed = false;

    explicit Parser(const std::wstring& s) : src(s) {}

    void skipWs() {
        while (pos < src.size() && iswspace(src[pos])) pos++;
    }
    bool eof() { return pos >= src.size(); }
    wchar_t peek() { return pos < src.size() ? src[pos] : 0; }

    std::wstring readCommand() {  // after the backslash
        std::wstring cmd;
        while (pos < src.size() && iswalpha(src[pos])) cmd += src[pos++];
        if (cmd.empty() && pos < src.size()) cmd += src[pos++];  // \{ \, etc.
        return cmd;
    }

    // One brace group or a single atom (TeX: \frac ab == \frac{a}{b})
    MNodePtr parseArg() {
        skipWs();
        if (peek() == L'{') {
            pos++;
            MNodePtr row = parseRow(L'}');
            if (peek() == L'}') pos++;
            else failed = true;
            return row;
        }
        return parseAtom();
    }

    MNodePtr parseAtom() {
        skipWs();
        if (eof()) return nullptr;
        wchar_t c = src[pos];

        if (c == L'{') {
            pos++;
            MNodePtr row = parseRow(L'}');
            if (peek() == L'}') pos++;
            else failed = true;
            return row;
        }
        if (c == L'\\') {
            pos++;
            return parseCommand();
        }
        pos++;
        MNodePtr sym = mk(MNode::Sym);
        if (c == L'-') sym->text = L"\u2212";       // proper minus
        else if (c == L'\'') sym->text = L"\u2032"; // prime
        else if (c == L'*') sym->text = L"\u2217";
        else sym->text.assign(1, c);
        return sym;
    }

    MNodePtr parseCommand() {
        std::wstring cmd = readCommand();
        if (cmd.empty()) { failed = true; return nullptr; }

        // Escapes and explicit spacing
        if (cmd == L"{" || cmd == L"}" || cmd == L"$" || cmd == L"%" ||
            cmd == L"&" || cmd == L"#" || cmd == L"_") {
            MNodePtr s = mk(MNode::Sym);
            s->text = cmd;
            return s;
        }
        if (cmd == L"|") {
            MNodePtr s = mk(MNode::Sym);
            s->text = L"\u2016";
            return s;
        }
        if (cmd == L"," || cmd == L":" || cmd == L";" || cmd == L" " ||
            cmd == L"quad" || cmd == L"qquad" || cmd == L"!") {
            MNodePtr s = mk(MNode::Space);
            if (cmd == L",") s->space = 0.17f;
            else if (cmd == L":") s->space = 0.22f;
            else if (cmd == L";") s->space = 0.28f;
            else if (cmd == L" ") s->space = 0.33f;
            else if (cmd == L"quad") s->space = 1.0f;
            else if (cmd == L"qquad") s->space = 2.0f;
            else s->space = 0.0f;  // \! ignored (negative space)
            return s;
        }

        if (cmd == L"frac" || cmd == L"dfrac" || cmd == L"tfrac") {
            MNodePtr f = mk(MNode::Frac);
            f->kids.push_back(parseArg());
            f->kids.push_back(parseArg());
            if (!f->kids[0] || !f->kids[1]) failed = true;
            return f;
        }
        if (cmd == L"sqrt") {
            MNodePtr d = mk(MNode::Deco);
            d->decoKind = 3;
            d->kids.push_back(parseArg());
            if (!d->kids[0]) failed = true;
            return d;
        }
        if (cmd == L"overline" || cmd == L"bar") {
            MNodePtr d = mk(MNode::Deco);
            d->decoKind = 1;
            d->kids.push_back(parseArg());
            return d;
        }
        if (cmd == L"overrightarrow" || cmd == L"vec") {
            MNodePtr d = mk(MNode::Deco);
            d->decoKind = 2;
            d->kids.push_back(parseArg());
            return d;
        }
        if (cmd == L"hat" || cmd == L"widehat") {
            MNodePtr d = mk(MNode::Deco);
            d->decoKind = 4;
            d->kids.push_back(parseArg());
            return d;
        }
        if (cmd == L"left") {
            skipWs();
            wchar_t open = 0;
            if (peek() == L'\\') {
                pos++;
                std::wstring dc = readCommand();
                const wchar_t* mapped = lookupSymbol(dc);
                open = mapped && mapped[0] ? mapped[0] : 0;
                if (dc == L"{") open = L'{';
                if (dc == L"}") open = L'}';
            } else {
                open = peek();
                if (!eof()) pos++;
            }
            MNodePtr content = parseRow(0, /*stopAtRight=*/true);
            // consume \right and its delimiter
            wchar_t close = 0;
            if (pos + 5 < src.size() && src.compare(pos, 6, L"\\right") == 0) {
                pos += 6;
                skipWs();
                if (peek() == L'\\') {
                    pos++;
                    std::wstring dc = readCommand();
                    const wchar_t* mapped = lookupSymbol(dc);
                    close = mapped && mapped[0] ? mapped[0] : 0;
                    if (dc == L"{") close = L'{';
                    if (dc == L"}") close = L'}';
                } else {
                    close = peek();
                    if (!eof()) pos++;
                }
            } else {
                failed = true;
            }
            MNodePtr d = mk(MNode::Delim);
            d->open = open == L'.' ? 0 : open;
            d->close = close == L'.' ? 0 : close;
            d->kids.push_back(content ? content : mk(MNode::Row));
            return d;
        }
        if (cmd == L"mathrm" || cmd == L"text" || cmd == L"operatorname" ||
            cmd == L"textrm") {
            MNodePtr arg = parseArg();
            if (arg) arg = markRoman(arg);
            return arg;
        }
        if (cmd == L"mathbf" || cmd == L"boldsymbol" || cmd == L"bm" ||
            cmd == L"textbf") {
            MNodePtr arg = parseArg();
            if (arg) arg = markBold(arg);
            return arg;
        }
        if (cmd == L"mathbb") {
            MNodePtr arg = parseArg();
            // single-letter blackboard bold via Unicode double-struck
            if (arg && arg->kind == MNode::Sym && arg->text.size() == 1) {
                for (const auto& b : kBlackboard) {
                    if (arg->text == b.name) {
                        arg->text = b.text;
                        break;
                    }
                }
            }
            return arg;
        }
        if (cmd == L"mathcal" || cmd == L"mathscr" || cmd == L"mathit") {
            return parseArg();  // rendered as regular italic
        }
        if (cmd == L"displaystyle" || cmd == L"textstyle" || cmd == L"nolimits" ||
            cmd == L"limits" || cmd == L"middle") {
            return parseAtom();  // pass-through niladics: render what follows
        }
        if (isFunctionName(cmd)) {
            MNodePtr f = mk(MNode::Sym);
            f->text = cmd;
            f->roman = true;
            return f;
        }
        if (const wchar_t* sym = lookupSymbol(cmd)) {
            MNodePtr s = mk(MNode::Sym);
            s->text = sym;
            return s;
        }

        failed = true;  // unknown command: whole span falls back to source
        return nullptr;
    }

    static MNodePtr markRoman(MNodePtr n) {
        n->roman = true;
        for (auto& k : n->kids) {
            if (k) markRoman(k);
        }
        return n;
    }
    static MNodePtr markBold(MNodePtr n) {
        n->bold = true;
        n->roman = true;
        for (auto& k : n->kids) {
            if (k) markBold(k);
        }
        return n;
    }

    MNodePtr parseRow(wchar_t terminator, bool stopAtRight = false) {
        MNodePtr row = mk(MNode::Row);
        while (!eof() && !failed) {
            skipWs();
            if (eof()) break;
            if (terminator && peek() == terminator) break;
            if (stopAtRight && peek() == L'\\' &&
                src.compare(pos, 6, L"\\right") == 0) {
                break;
            }
            if (peek() == L'^' || peek() == L'_') {
                // Attach scripts to the previous atom (or an empty base)
                MNodePtr base = row->kids.empty() ? mk(MNode::Row)
                                                  : row->kids.back();
                if (!row->kids.empty()) row->kids.pop_back();
                MNodePtr script;
                if (base->kind == MNode::Script) {
                    script = base;  // x_1^m: add the second script
                } else {
                    script = mk(MNode::Script);
                    script->kids = {base, nullptr, nullptr};
                }
                while (!eof() && (peek() == L'^' || peek() == L'_')) {
                    wchar_t which = src[pos++];
                    MNodePtr arg = parseArg();
                    if (!arg) { failed = true; break; }
                    if (which == L'^') script->kids[2] = arg;
                    else script->kids[1] = arg;
                    skipWs();
                }
                row->kids.push_back(script);
                continue;
            }
            MNodePtr atom = parseAtom();
            if (atom) row->kids.push_back(atom);
        }
        return row;
    }
};

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

struct Metrics {
    float width = 0, ascent = 0, descent = 0;
    float height() const { return ascent + descent; }
};

struct LayoutCtx {
    App& app;
    MathBox& box;
    // TeX text style vs display style: inline fractions use near-script
    // sizes so they fit the surrounding line
    bool display = false;
};

IDWriteTextLayout* makeRunLayout(App& app, const std::wstring& text,
                                 float size, bool italic, bool bold,
                                 float& outBaseline, Metrics& m) {
    IDWriteTextFormat* fmt = nullptr;
    app.dwriteFactory->CreateTextFormat(
        app.theme.fontFamily, nullptr,
        bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &fmt);
    if (!fmt) return nullptr;

    IDWriteTextLayout* layout = nullptr;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                                        fmt, 100000.0f, 1000.0f, &layout);
    fmt->Release();
    if (!layout) return nullptr;

    if (app.fontFallback) {
        IDWriteTextLayout2* l2 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout2),
                                             reinterpret_cast<void**>(&l2)))) {
            l2->SetFontFallback(app.fontFallback);
            l2->Release();
        }
    }

    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    DWRITE_LINE_METRICS lm{};
    UINT32 lineCount = 1;
    layout->GetLineMetrics(&lm, 1, &lineCount);
    outBaseline = lm.baseline;
    m.width = tm.widthIncludingTrailingWhitespace;
    m.ascent = lm.baseline;
    m.descent = std::max(0.0f, lm.height - lm.baseline);
    return layout;
}

// Forward declaration
Metrics layoutNode(LayoutCtx& ctx, const MNodePtr& node, float size,
                   float x, float baselineY);

Metrics measureNode(LayoutCtx& ctx, const MNodePtr& node, float size);

// Lay a run out and record it (position given by baseline)
Metrics emitRun(LayoutCtx& ctx, const std::wstring& text, float size,
                bool italic, bool bold, float x, float baselineY,
                bool record) {
    Metrics m;
    float runBaseline = 0;
    IDWriteTextLayout* layout =
        makeRunLayout(ctx.app, text, size, italic, bold, runBaseline, m);
    if (!layout) return m;
    if (record) {
        ctx.box.runs.push_back({layout, x, baselineY - runBaseline});
    } else {
        layout->Release();
    }
    return m;
}

bool isMathVariableChar(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

// The workhorse: recursively lay out `node` with the baseline at
// `baselineY`, starting at `x`. Returns the node's metrics. When
// record=false (measure pass), nothing is emitted.
Metrics layoutNodeImpl(LayoutCtx& ctx, const MNodePtr& node, float size,
                       float x, float baselineY, bool record) {
    Metrics m;
    if (!node) return m;
    float em = size;

    switch (node->kind) {
        case MNode::Space: {
            m.width = node->space * em;
            m.ascent = 0;
            m.descent = 0;
            return m;
        }
        case MNode::Sym: {
            bool italic = !node->roman && node->text.size() >= 1 &&
                          isMathVariableChar(node->text[0]) &&
                          !(node->text.size() > 1 && node->roman);
            Metrics rm = emitRun(ctx, node->text, size, italic, node->bold,
                                 x, baselineY, record);
            return rm;
        }
        case MNode::Row: {
            float cx = x;
            float prevSpace = 0.0f;
            bool first = true;
            for (const auto& kid : node->kids) {
                if (!kid) continue;
                float kidSpace = 0.0f;
                if (kid->kind == MNode::Sym) {
                    kidSpace = symbolSpacing(kid->text, kid->roman) * em;
                }
                if (!first) cx += std::max(prevSpace, kidSpace);
                Metrics km = layoutNodeImpl(ctx, kid, size, cx, baselineY, record);
                cx += km.width;
                m.ascent = std::max(m.ascent, km.ascent);
                m.descent = std::max(m.descent, km.descent);
                prevSpace = kidSpace;
                first = false;
            }
            m.width = cx - x;
            return m;
        }
        case MNode::Script: {
            const MNodePtr& base = node->kids[0];
            const MNodePtr& sub = node->kids[1];
            const MNodePtr& sup = node->kids[2];
            float scriptSize = std::max(8.0f, size * 0.68f);

            Metrics bm = layoutNodeImpl(ctx, base, size, x, baselineY, record);
            float sx = x + bm.width + em * 0.03f;
            float supShift = std::max(bm.ascent * 0.55f, em * 0.38f);
            float subShift = std::max(bm.descent + em * 0.05f, em * 0.16f);

            Metrics supM, subM;
            if (sup) {
                supM = layoutNodeImpl(ctx, sup, scriptSize, sx,
                                      baselineY - supShift, record);
            }
            if (sub) {
                subM = layoutNodeImpl(ctx, sub, scriptSize, sx,
                                      baselineY + subShift, record);
            }
            m.width = bm.width + em * 0.03f + std::max(supM.width, subM.width);
            m.ascent = std::max(bm.ascent, sup ? supShift + supM.ascent : 0.0f);
            m.descent = std::max(bm.descent, sub ? subShift + subM.descent : 0.0f);
            return m;
        }
        case MNode::Frac: {
            float inner = std::max(8.0f, size * (ctx.display ? 0.92f : 0.72f));
            Metrics num = measureNode(ctx, node->kids[0], inner);
            Metrics den = measureNode(ctx, node->kids[1], inner);
            float pad = em * 0.12f;
            float ruleW = std::max(num.width, den.width) + pad * 2;
            float ruleH = std::max(1.0f, em * 0.055f);
            float axis = em * 0.26f;   // fraction line sits on the math axis
            float gap = em * (ctx.display ? 0.14f : 0.1f);

            float ruleY = baselineY - axis - ruleH / 2;
            if (record) {
                // numerator baseline so its descent clears the rule
                layoutNodeImpl(ctx, node->kids[0], inner,
                               x + pad + (ruleW - 2 * pad - num.width) / 2,
                               ruleY - gap - num.descent, true);
                layoutNodeImpl(ctx, node->kids[1], inner,
                               x + pad + (ruleW - 2 * pad - den.width) / 2,
                               ruleY + ruleH + gap + den.ascent, true);
                ctx.box.rules.push_back({x, ruleY, ruleW, ruleH});
            }
            m.width = ruleW;
            m.ascent = axis + ruleH / 2 + gap + num.height();
            m.descent = -axis + ruleH / 2 + gap + den.height();
            m.descent = std::max(m.descent, 0.0f);
            return m;
        }
        case MNode::Delim: {
            Metrics cm = measureNode(ctx, node->kids[0], size);
            // Delimiters stretch to the content: scale the glyph size so a
            // paren grows with a fraction inside it
            float contentH = std::max(cm.height(), em);
            float glyphSize = size;
            if (contentH > em * 1.15f) {
                glyphSize = size * std::min(3.5f, contentH / em);
            }
            float cx = x;
            auto emitDelim = [&](wchar_t d) -> float {
                if (!d) return 0.0f;
                std::wstring dt(1, d);
                Metrics dm;
                float bl = 0;
                IDWriteTextLayout* layout = makeRunLayout(
                    ctx.app, dt, glyphSize, false, false, bl, dm);
                if (!layout) return 0.0f;
                if (record) {
                    // center the glyph on the content's vertical center
                    float centerY = baselineY - (cm.ascent - cm.descent) / 2;
                    float top = centerY - (dm.ascent + dm.descent) / 2;
                    ctx.box.runs.push_back({layout, cx, top});
                } else {
                    layout->Release();
                }
                return dm.width;
            };
            float ow = emitDelim(node->open);
            cx += ow;
            Metrics inner = layoutNodeImpl(ctx, node->kids[0], size, cx,
                                           baselineY, record);
            cx += inner.width;
            float cw = emitDelim(node->close);
            cx += cw;
            m.width = cx - x;
            float glyphHalf = (glyphSize * 1.1f) / 2;
            float centerOff = (cm.ascent - cm.descent) / 2;
            m.ascent = std::max(cm.ascent, centerOff + glyphHalf);
            m.descent = std::max(cm.descent, glyphHalf - centerOff);
            return m;
        }
        case MNode::Deco: {
            if (node->decoKind == 3) {  // sqrt
                Metrics cm = measureNode(ctx, node->kids[0], size);
                float radSize = size * std::min(
                    3.0f, std::max(1.0f, cm.height() / em));
                Metrics rm;
                float bl = 0;
                float gap = em * 0.1f;
                float ruleH = std::max(1.0f, em * 0.05f);
                IDWriteTextLayout* rad = makeRunLayout(
                    ctx.app, L"\u221A", radSize, false, false, bl, rm);
                float cx = x;
                if (rad) {
                    if (record) {
                        float radTop = baselineY - cm.ascent - gap - ruleH -
                                       (rm.height() - (cm.height() + gap + ruleH)) * 0.5f;
                        // anchor: radical bottom near content bottom
                        radTop = baselineY + cm.descent - rm.height();
                        ctx.box.runs.push_back({rad, cx, radTop});
                    } else {
                        rad->Release();
                    }
                    cx += rm.width * 0.95f;
                }
                Metrics inner = layoutNodeImpl(ctx, node->kids[0], size, cx,
                                               baselineY, record);
                if (record) {
                    ctx.box.rules.push_back(
                        {cx - em * 0.05f, baselineY - cm.ascent - gap - ruleH,
                         inner.width + em * 0.15f, ruleH});
                }
                m.width = (cx - x) + inner.width + em * 0.15f;
                m.ascent = cm.ascent + gap + ruleH + em * 0.05f;
                m.descent = cm.descent;
                return m;
            }
            // overline / overrightarrow / hat
            Metrics cm = measureNode(ctx, node->kids[0], size);
            float gap = em * 0.12f;
            float ruleH = std::max(1.0f, em * 0.05f);
            Metrics inner = layoutNodeImpl(ctx, node->kids[0], size, x,
                                           baselineY, record);
            float topY = baselineY - cm.ascent - gap - ruleH;
            if (record) {
                if (node->decoKind == 1) {
                    ctx.box.rules.push_back({x, topY, cm.width, ruleH});
                } else if (node->decoKind == 2) {
                    float aw = em * 0.22f;   // arrowhead
                    float ay = topY + ruleH / 2;
                    ctx.box.rules.push_back({x, topY, cm.width, ruleH});
                    ctx.box.lines.push_back(
                        {x + cm.width - aw, ay - aw * 0.6f, x + cm.width, ay});
                    ctx.box.lines.push_back(
                        {x + cm.width - aw, ay + aw * 0.6f, x + cm.width, ay});
                } else if (node->decoKind == 4) {
                    float cxm = x + cm.width / 2;
                    float hw = std::min(cm.width / 2, em * 0.28f);
                    ctx.box.lines.push_back(
                        {cxm - hw, topY + ruleH + em * 0.08f, cxm, topY});
                    ctx.box.lines.push_back(
                        {cxm, topY, cxm + hw, topY + ruleH + em * 0.08f});
                }
            }
            m.width = std::max(inner.width, cm.width);
            m.ascent = cm.ascent + gap + ruleH + em * 0.08f;
            m.descent = cm.descent;
            return m;
        }
    }
    return m;
}

Metrics layoutNode(LayoutCtx& ctx, const MNodePtr& node, float size,
                   float x, float baselineY) {
    return layoutNodeImpl(ctx, node, size, x, baselineY, true);
}

Metrics measureNode(LayoutCtx& ctx, const MNodePtr& node, float size) {
    return layoutNodeImpl(ctx, node, size, 0, 0, false);
}

// ---------------------------------------------------------------------------
// Cache + public API
// ---------------------------------------------------------------------------

std::unordered_map<std::wstring, MathBoxPtr> g_mathCache;

} // namespace

MathBoxPtr mathParse(App& app, const std::wstring& latex, float fontSize,
                     bool display) {
    std::wstring key = latex + L"|" + std::to_wstring((int)(fontSize * 4)) +
                       (display ? L"|d" : L"|i");
    auto it = g_mathCache.find(key);
    if (it != g_mathCache.end()) return it->second;

    Parser parser(latex);
    MNodePtr root = parser.parseRow(0);
    if (parser.failed || !root || root->kids.empty()) {
        g_mathCache[key] = nullptr;   // remember the failure too
        return nullptr;
    }

    auto box = std::make_shared<MathBox>();
    LayoutCtx ctx{app, *box, display};
    Metrics probe = measureNode(ctx, root, fontSize);
    float pad = display ? fontSize * 0.15f : 0.0f;
    float baseline = probe.ascent + pad;
    Metrics final = layoutNode(ctx, root, fontSize, pad, baseline);
    box->width = final.width + pad * 2;
    box->height = final.ascent + final.descent + pad * 2;
    box->baseline = baseline;

    g_mathCache[key] = box;
    return box;
}

float mathBoxWidth(const MathBoxPtr& box) { return box ? box->width : 0; }
float mathBoxHeight(const MathBoxPtr& box) { return box ? box->height : 0; }
float mathBoxBaseline(const MathBoxPtr& box) { return box ? box->baseline : 0; }

void mathBoxDraw(App& app, const MathBoxPtr& box, float x, float y,
                 const D2D1_COLOR_F& color) {
    if (!box || !app.renderTarget || !app.brush) return;
    app.brush->SetColor(color);
    for (const auto& run : box->runs) {
        app.renderTarget->DrawTextLayout(
            D2D1::Point2F(x + run.x, y + run.y), run.layout, app.brush,
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    for (const auto& rule : box->rules) {
        app.renderTarget->FillRectangle(
            D2D1::RectF(x + rule.x, y + rule.y, x + rule.x + rule.w,
                        y + rule.y + rule.h),
            app.brush);
    }
    for (const auto& line : box->lines) {
        app.renderTarget->DrawLine(
            D2D1::Point2F(x + line.x1, y + line.y1),
            D2D1::Point2F(x + line.x2, y + line.y2), app.brush,
            std::max(1.0f, mathBoxHeight(box) * 0.02f));
    }
}

void mathBoxRetain(App& app, const MathBoxPtr& box, float x, float y,
                   const D2D1_COLOR_F& color) {
    if (!box) return;
    for (const auto& run : box->runs) {
        if (!run.layout) continue;
        run.layout->AddRef();
        App::LayoutTextRun r;
        r.layout = run.layout;
        r.pos = D2D1::Point2F(x + run.x, y + run.y);
        // Whole-box bounds keep viewport culling conservative and correct
        r.bounds = D2D1::RectF(x, y, x + box->width, y + box->height);
        r.color = color;
        r.docStart = 0;
        r.docLength = 0;
        r.selectable = false;
        app.layoutTextRuns.push_back(r);
    }
    for (const auto& rule : box->rules) {
        app.layoutRects.push_back(
            {D2D1::RectF(x + rule.x, y + rule.y, x + rule.x + rule.w,
                         y + rule.y + rule.h),
             color});
    }
    for (const auto& line : box->lines) {
        app.layoutLines.push_back({D2D1::Point2F(x + line.x1, y + line.y1),
                                   D2D1::Point2F(x + line.x2, y + line.y2),
                                   color, 1.2f});
    }
}

void mathClearCache() {
    g_mathCache.clear();
}
