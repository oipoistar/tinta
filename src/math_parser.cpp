#include "math_parser.h"

#include <algorithm>
#include <cwctype>

namespace tinta_math {

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
    {L"longrightarrow", L"\u27F6"}, {L"longleftarrow", L"\u27F5"},
    {L"longleftrightarrow", L"\u27F7"}, {L"Longrightarrow", L"\u27F9"},
    {L"Longleftarrow", L"\u27F8"}, {L"Longleftrightarrow", L"\u27FA"},
    {L"implies", L"\u27F9"}, {L"impliedby", L"\u27F8"}, {L"iff", L"\u27FA"},
    {L"longmapsto", L"\u27FC"},
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
        case 0x2190: case 0x21D0: case 0x21A6:
        case 0x27F5: case 0x27F6: case 0x27F7: case 0x27F8:
        case 0x27F9: case 0x27FA: case 0x27FC:
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
    int depth = 0;
    size_t atoms = 0;

    struct Nest {
        int& depth;
        explicit Nest(int& d) : depth(d) { ++depth; }
        ~Nest() { --depth; }
    };

    explicit Parser(const std::wstring& s) : src(s) {}

    void skipWs() {
        while (pos < src.size()) {
            if (iswspace(src[pos])) { ++pos; continue; }
            if (src[pos] != L'%') break;
            while (pos < src.size() && src[pos] != L'\n') ++pos;
        }
    }
    bool eof() { return pos >= src.size(); }
    wchar_t peek() { return pos < src.size() ? src[pos] : 0; }

    bool atCommand(const wchar_t* cmd) const {
        size_t n = std::char_traits<wchar_t>::length(cmd);
        return src.compare(pos, n, cmd) == 0 &&
               (pos + n == src.size() || !iswalpha(src[pos + n]));
    }

    std::wstring literalGroup() {
        skipWs();
        if (peek() != L'{') { failed = true; return {}; }
        size_t start = ++pos;
        while (!eof() && peek() != L'}' && peek() != L'{' && peek() != L'\\') ++pos;
        std::wstring value = src.substr(start, pos - start);
        if (peek() != L'}') failed = true;
        else ++pos;
        return value;
    }

    MNodePtr parseEnvironment() {
        std::wstring name = literalGroup();
        auto grid = mk(MNode::Grid);
        wchar_t open = 0, close = 0;
        if (name == L"pmatrix") { open = L'('; close = L')'; }
        else if (name == L"bmatrix") { open = L'['; close = L']'; }
        else if (name == L"Bmatrix") { open = L'{'; close = L'}'; }
        else if (name == L"vmatrix") open = close = L'|';
        else if (name == L"Vmatrix") open = close = L'\u2016';
        else if (name == L"cases" || name == L"rcases") {
            if (name == L"cases") open = L'{'; else close = L'}';
            grid->alignment = L"ll";
        } else if (name == L"aligned" || name == L"split") grid->aligned = true;
        else if (name == L"smallmatrix") grid->compact = true;
        else if (name == L"array") {
            grid->alignment = literalGroup();
            if (grid->alignment.empty() || grid->alignment.size() > 64 ||
                grid->alignment.find_first_not_of(L"lcr") != std::wstring::npos)
                failed = true;
        } else if (name != L"matrix" && name != L"gathered") failed = true;
        if (failed) return nullptr;

        grid->cells.emplace_back();
        while (!failed) {
            if (grid->cells.size() > 64 || grid->cells.back().size() >= 64) {
                failed = true; break;
            }
            grid->cells.back().push_back(parseRow(0, false, true));
            skipWs();
            if (peek() == L'&') { ++pos; continue; }
            if (src.compare(pos, 2, L"\\\\") == 0 || atCommand(L"\\cr")) {
                pos += peek() == L'\\' && pos + 1 < src.size() && src[pos + 1] == L'\\' ? 2 : 3;
                skipWs();
                if (atCommand(L"\\end")) break; // optional final row separator
                grid->cells.emplace_back();
                continue;
            }
            break;
        }
        if (!atCommand(L"\\end")) { failed = true; return nullptr; }
        pos += 4;
        if (literalGroup() != name) failed = true;
        for (const auto& row : grid->cells) {
            if ((!grid->alignment.empty() && row.size() > grid->alignment.size()) ||
                (name == L"gathered" && row.size() != 1) ||
                (name == L"split" && row.size() > 2)) failed = true;
        }
        if (!open && !close) return grid;
        auto delim = mk(MNode::Delim);
        delim->open = open; delim->close = close;
        delim->kids = {grid};
        return delim;
    }

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
        Nest nest(depth);
        if (depth > 96 || ++atoms > 4096) { failed = true; return nullptr; }
        skipWs();
        if (eof()) return nullptr;
        wchar_t c = src[pos];
        if (c == L'}' || c == L'&' || c == L'^' || c == L'_') {
            failed = true; return nullptr;
        }

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
        if (cmd == L"begin") return parseEnvironment();

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
        for (auto& row : n->cells) for (auto& cell : row) if (cell) markRoman(cell);
        return n;
    }
    static MNodePtr markBold(MNodePtr n) {
        n->bold = true;
        n->roman = true;
        for (auto& k : n->kids) {
            if (k) markBold(k);
        }
        for (auto& row : n->cells) for (auto& cell : row) if (cell) markBold(cell);
        return n;
    }

    MNodePtr parseRow(wchar_t terminator, bool stopAtRight = false, bool cell = false) {
        Nest nest(depth);
        if (depth > 96) { failed = true; return nullptr; }
        MNodePtr row = mk(MNode::Row);
        while (!eof() && !failed) {
            skipWs();
            if (eof()) break;
            if (terminator && peek() == terminator) break;
            if (cell && (peek() == L'&' || src.compare(pos, 2, L"\\\\") == 0 ||
                         atCommand(L"\\cr") || atCommand(L"\\end"))) break;
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


MNodePtr parseLatex(const std::wstring& source) {
    if (source.size() > 32768) return nullptr;
    Parser parser(source);
    auto root = parser.parseRow(0);
    return parser.failed || !root || root->kids.empty() ? nullptr : root;
}

} // namespace tinta_math
