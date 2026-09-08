#include "math_parser.h"
#include "math_macros.h"

#include <algorithm>
#include <cwctype>
#include <climits>
#include <cstdlib>
#include <cmath>

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
    {L"delta", L"\u03B4"}, {L"epsilon", L"\u03F5"}, {L"varepsilon", L"\u03B5"},
    {L"zeta", L"\u03B6"}, {L"eta", L"\u03B7"}, {L"theta", L"\u03B8"},
    {L"vartheta", L"\u03D1"}, {L"iota", L"\u03B9"}, {L"kappa", L"\u03BA"},
    {L"lambda", L"\u03BB"}, {L"mu", L"\u03BC"}, {L"nu", L"\u03BD"},
    {L"xi", L"\u03BE"}, {L"pi", L"\u03C0"}, {L"rho", L"\u03C1"},
    {L"sigma", L"\u03C3"}, {L"tau", L"\u03C4"}, {L"upsilon", L"\u03C5"},
    {L"phi", L"\u03D5"}, {L"varphi", L"\u03C6"}, {L"chi", L"\u03C7"},
    {L"varrho", L"\u03F1"}, {L"varpi", L"\u03D6"}, {L"varsigma", L"\u03C2"},
    {L"varkappa", L"\u03F0"}, {L"digamma", L"\u03DD"}, {L"omicron", L"o"},
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
    {L"iint", L"\u222C"}, {L"iiint", L"\u222D"}, {L"oiint", L"\u222F"},
    {L"coprod", L"\u2210"}, {L"bigcup", L"\u22C3"}, {L"bigcap", L"\u22C2"},
    {L"bigvee", L"\u22C1"}, {L"bigwedge", L"\u22C0"}, {L"bigoplus", L"\u2A01"},
    {L"bigotimes", L"\u2A02"}, {L"biguplus", L"\u2A04"},
    {L"gets", L"\u2190"}, {L"uparrow", L"\u2191"}, {L"downarrow", L"\u2193"},
    {L"updownarrow", L"\u2195"}, {L"Uparrow", L"\u21D1"}, {L"Downarrow", L"\u21D3"},
    {L"Updownarrow", L"\u21D5"}, {L"nearrow", L"\u2197"}, {L"searrow", L"\u2198"},
    {L"nwarrow", L"\u2196"}, {L"swarrow", L"\u2199"},
    {L"hookrightarrow", L"\u21AA"}, {L"hookleftarrow", L"\u21A9"},
    {L"rightharpoonup", L"\u21C0"}, {L"rightharpoondown", L"\u21C1"},
    {L"leftharpoonup", L"\u21BC"}, {L"leftharpoondown", L"\u21BD"},
    {L"rightleftharpoons", L"\u21CC"}, {L"leftrightharpoons", L"\u21CB"},
    {L"leadsto", L"\u21DD"}, {L"rightsquigarrow", L"\u21DD"},
    {L"twoheadrightarrow", L"\u21A0"}, {L"twoheadleftarrow", L"\u219E"},
    {L"ll", L"\u226A"}, {L"gg", L"\u226B"}, {L"simeq", L"\u2243"},
    {L"cong", L"\u2245"}, {L"asymp", L"\u224D"}, {L"doteq", L"\u2250"},
    {L"lesssim", L"\u2272"}, {L"gtrsim", L"\u2273"},
    {L"leqslant", L"\u2A7D"}, {L"geqslant", L"\u2A7E"},
    {L"prec", L"\u227A"}, {L"succ", L"\u227B"},
    {L"preceq", L"\u2AAF"}, {L"succeq", L"\u2AB0"},
    {L"nleq", L"\u2270"}, {L"ngeq", L"\u2271"}, {L"nless", L"\u226E"}, {L"ngtr", L"\u226F"},
    {L"nsubseteq", L"\u2288"}, {L"nsupseteq", L"\u2289"}, {L"subsetneq", L"\u228A"},
    {L"supsetneq", L"\u228B"}, {L"nexists", L"\u2204"},
    {L"vdash", L"\u22A2"}, {L"dashv", L"\u22A3"}, {L"models", L"\u22A8"},
    {L"vDash", L"\u22A8"}, {L"Vdash", L"\u22A9"},
    {L"top", L"\u22A4"}, {L"bot", L"\u22A5"}, {L"complement", L"\u2201"},
    {L"uplus", L"\u228E"}, {L"sqcup", L"\u2294"}, {L"sqcap", L"\u2293"},
    {L"odot", L"\u2299"}, {L"ominus", L"\u2296"}, {L"oslash", L"\u2298"},
    {L"circledast", L"\u229B"}, {L"ast", L"\u2217"}, {L"diamond", L"\u22C4"},
    {L"ltimes", L"\u22C9"}, {L"rtimes", L"\u22CA"}, {L"wr", L"\u2240"},
    {L"imath", L"\u0131"}, {L"jmath", L"\u0237"}, {L"ell", L"\u2113"},
    {L"lparen", L"("}, {L"rparen", L")"}, {L"colon", L":"},
    {L"dotsc", L"\u2026"}, {L"dotsb", L"\u22EF"}, {L"dotsm", L"\u22EF"},
};

// Function names rendered upright with a trailing thin space
const wchar_t* kFunctions[] = {
    L"log", L"ln", L"lg", L"sin", L"cos", L"tan", L"cot", L"sec", L"csc",
    L"arcsin", L"arccos", L"arctan", L"sinh", L"cosh", L"tanh", L"coth",
    L"lim", L"limsup", L"liminf", L"max", L"min", L"sup", L"inf",
    L"gcd", L"det", L"dim", L"ker", L"deg", L"arg", L"exp", L"mod", L"Pr",
    L"hom", L"erf", L"sgn",
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

std::wstring codepoint(unsigned cp) {
#if WCHAR_MAX <= 0xFFFF
    if (cp > 0xFFFF) {
        cp -= 0x10000;
        return {static_cast<wchar_t>(0xD800 + (cp >> 10)),
                static_cast<wchar_t>(0xDC00 + (cp & 0x3FF))};
    }
#endif
    return std::wstring(1, static_cast<wchar_t>(cp));
}

void mathAlphabet(const MNodePtr& node, const std::wstring& command) {
    if (!node) return;
    node->roman = true;
    if (node->kind == MNode::Sym) {
        std::wstring mapped;
        for (wchar_t c : node->text) {
            unsigned cp = c;
            bool upper = c >= L'A' && c <= L'Z', lower = c >= L'a' && c <= L'z';
            if (command == L"mathbb") {
                if (upper) cp = 0x1D538 + c - L'A';
                if (lower) cp = 0x1D552 + c - L'a';
                if (c >= L'0' && c <= L'9') cp = 0x1D7D8 + c - L'0';
                switch (c) {
                    case L'C': cp = 0x2102; break; case L'H': cp = 0x210D; break;
                    case L'N': cp = 0x2115; break; case L'P': cp = 0x2119; break;
                    case L'Q': cp = 0x211A; break; case L'R': cp = 0x211D; break;
                    case L'Z': cp = 0x2124; break;
                }
            } else if (command == L"mathcal" || command == L"mathscr") {
                if (upper) cp = 0x1D49C + c - L'A';
                if (lower) cp = 0x1D4B6 + c - L'a';
                switch (c) {
                    case L'B': cp = 0x212C; break; case L'E': cp = 0x2130; break;
                    case L'F': cp = 0x2131; break; case L'H': cp = 0x210B; break;
                    case L'I': cp = 0x2110; break; case L'L': cp = 0x2112; break;
                    case L'M': cp = 0x2133; break; case L'R': cp = 0x211B; break;
                    case L'e': cp = 0x212F; break; case L'g': cp = 0x210A; break;
                    case L'o': cp = 0x2134; break;
                }
            } else if (command == L"mathfrak") {
                if (upper) cp = 0x1D504 + c - L'A';
                if (lower) cp = 0x1D51E + c - L'a';
                switch (c) {
                    case L'C': cp = 0x212D; break; case L'H': cp = 0x210C; break;
                    case L'I': cp = 0x2111; break; case L'R': cp = 0x211C; break;
                    case L'Z': cp = 0x2128; break;
                }
            } else {
                unsigned base = command == L"mathsf" ? 0x1D5A0 : 0x1D670;
                if (upper) cp = base + c - L'A';
                if (lower) cp = base + 26 + c - L'a';
                if (c >= L'0' && c <= L'9') cp = (command == L"mathsf" ? 0x1D7E2 : 0x1D7F6) + c - L'0';
            }
            mapped += codepoint(cp);
        }
        node->text = std::move(mapped);
    }
    for (const auto& kid : node->kids) mathAlphabet(kid, command);
    for (const auto& row : node->cells) for (const auto& cell : row) mathAlphabet(cell, command);
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
    if ((c >= 0x2190 && c <= 0x21FF) || (c >= 0x27F0 && c <= 0x27FF) ||
        (c >= 0x223C && c <= 0x223D) || (c >= 0x2243 && c <= 0x228D) ||
        (c >= 0x22A2 && c <= 0x22AF) || c == 0x2A7D || c == 0x2A7E ||
        c == 0x2AAF || c == 0x2AB0) return 0.26f;
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
        case 0x2228: case 0x2216: case 0x2295: case 0x2296: case 0x2297:
        case 0x2298: case 0x2299: case 0x229B: case 0x228E: case 0x2293:
        case 0x2294: case 0x22C9: case 0x22CA:
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
    int delimiterDepth = 0;

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

    wchar_t readDelimiter() {
        skipWs();
        if (eof()) { failed = true; return 0; }
        wchar_t value = src[pos++];
        if (value == L'\\') {
            std::wstring name = readCommand();
            if (name == L"{" || name == L"}") value = name[0];
            else if (name == L"|") value = L'\u2016';
            else {
                const wchar_t* symbol = lookupSymbol(name);
                if (!symbol || !symbol[0] || symbol[1]) { failed = true; return 0; }
                value = symbol[0];
            }
        }
        if (std::wstring(L".()[]{}|/\\\u2016\u2223\u2225\u2308\u2309\u230A\u230B\u27E8\u27E9\u2191\u2193\u2195\u21D1\u21D3\u21D5").find(value) == std::wstring::npos)
            failed = true;
        return value == L'.' ? 0 : value;
    }

    MNodePtr dimension() {
        skipWs();
        bool group = peek() == L'{';
        if (group) ++pos;
        const wchar_t* start = src.c_str() + pos;
        wchar_t* end = nullptr;
        double number = std::wcstod(start, &end);
        if (start == end || !std::isfinite(number) || std::abs(number) > 1000) {
            failed = true; return nullptr;
        }
        pos += static_cast<size_t>(end - start);
        skipWs();
        std::wstring unit = src.substr(pos, 2);
        if (unit.size() == 2) pos += 2;
        auto space = mk(MNode::Space);
        space->space = static_cast<float>(number);
        if (unit == L"ex") space->space *= 0.5f;
        else if (unit == L"mu") space->space /= 18;
        else if (unit == L"pt" || unit == L"px") space->text = unit;
        else if (unit != L"em") failed = true;
        skipWs();
        if (group) { if (peek() != L'}') failed = true; else ++pos; }
        return space;
    }

    MNodePtr textGroup() {
        Nest nest(depth);
        if (depth > 96) { failed = true; return nullptr; }
        skipWs();
        if (peek() != L'{') { failed = true; return nullptr; }
        ++pos;
        auto row = mk(MNode::Row);
        row->literalText = true;
        std::wstring text;
        auto flush = [&]() {
            if (text.empty()) return;
            auto run = mk(MNode::Sym);
            run->text = std::move(text); text.clear();
            run->roman = run->literalText = true;
            row->kids.push_back(run);
        };
        while (!eof() && !failed && peek() != L'}') {
            if (peek() == L'{') { flush(); row->kids.push_back(textGroup()); continue; }
            wchar_t c = src[pos++];
            if (c == L'%') { while (!eof() && peek() != L'\n') ++pos; continue; }
            if (c == L'\\') {
                std::wstring cmd = readCommand();
                if (cmd.size() == 1 && std::wstring(L"{}$%&#_ ").find(cmd[0]) != std::wstring::npos) {
                    text += cmd; continue;
                }
                if (cmd == L"textbf" || cmd == L"textit" || cmd == L"text" || cmd == L"textrm") {
                    flush(); auto child = textGroup();
                    if (child && cmd == L"textbf") markBold(child);
                    if (child && cmd == L"textit") markItalic(child);
                    row->kids.push_back(child); continue;
                }
                failed = true; break;
            }
            text += c == L'~' || iswspace(c) ? L' ' : c;
        }
        flush();
        if (peek() != L'}') failed = true; else ++pos;
        return row;
    }

    MNodePtr parseEnvironment() {
        std::wstring name = literalGroup();
        std::wstring endName = name;
        bool starred = !name.empty() && name.back() == L'*';
        if (starred) name.pop_back();
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
            auto spec = literalGroup();
            for (wchar_t c : spec) {
                if (c == L'|') grid->verticalRules.push_back(grid->alignment.size());
                else if (c == L'l' || c == L'c' || c == L'r') grid->alignment += c;
                else if (!iswspace(c)) failed = true;
            }
            if (grid->alignment.empty() || grid->alignment.size() > 64) failed = true;
        } else if (name != L"matrix" && name != L"gathered") failed = true;
        if (starred) {
            if (name.find(L"matrix") == std::wstring::npos || name == L"smallmatrix") failed = true;
            skipWs();
            if (peek() == L'[') {
                ++pos; skipWs();
                wchar_t align = peek(); if (!eof()) ++pos;
                skipWs();
                if ((align != L'l' && align != L'c' && align != L'r') || peek() != L']') failed = true;
                else ++pos;
                grid->alignment.assign(1, align); grid->uniformAlignment = true;
            }
        }
        if (failed) return nullptr;

        grid->cells.emplace_back();
        while (!failed) {
            skipWs();
            while (name == L"array" && grid->cells.back().empty() && atCommand(L"\\hline")) {
                grid->horizontalRules.push_back(grid->cells.size() - 1);
                pos += 6; skipWs();
            }
            if (atCommand(L"\\end") && grid->cells.back().empty() && grid->cells.size() > 1) {
                grid->cells.pop_back(); break;
            }
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
        if (literalGroup() != endName) failed = true;
        for (const auto& row : grid->cells) {
            if ((!grid->uniformAlignment && !grid->alignment.empty() && row.size() > grid->alignment.size()) ||
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
        auto atom = parseAtom();
        if (!atom) failed = true;
        return atom;
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
        if (cmd == L"hspace" || cmd == L"kern" || cmd == L"mkern") {
            if (cmd == L"hspace" && peek() == L'*') ++pos;
            return dimension();
        }
        if (cmd == L"pmod" || cmd == L"bmod" || cmd == L"pod") {
            auto row = mk(MNode::Row);
            row->spacing = cmd == L"bmod" ? 0.2f : 0.3f;
            if (cmd != L"pod") {
                auto mod = mk(MNode::Sym); mod->roman = true; mod->text = L"mod";
                row->kids.push_back(mod);
                auto space = mk(MNode::Space); space->space = 0.33f; row->kids.push_back(space);
            }
            if (cmd != L"bmod") {
                row->kids.push_back(parseArg());
                auto delim = mk(MNode::Delim); delim->open = L'('; delim->close = L')';
                delim->kids = {row}; delim->spacing = 0.3f; return delim;
            }
            return row;
        }
        const std::pair<const wchar_t*, float> sizes[] = {{L"big", 1.2f}, {L"Big", 1.6f}, {L"bigg", 2.0f}, {L"Bigg", 2.4f}};
        for (const auto& entry : sizes) {
            std::wstring name = entry.first;
            if (cmd == name || cmd == name + L"l" || cmd == name + L"r" || cmd == name + L"m") {
                auto delimiter = mk(MNode::Middle);
                delimiter->open = readDelimiter(); delimiter->delimiterSize = entry.second;
                return delimiter;
            }
        }
        if (cmd == L"middle") {
            if (!delimiterDepth) { failed = true; return nullptr; }
            auto delimiter = mk(MNode::Middle); delimiter->open = readDelimiter();
            delimiter->spacing = 0.26f;
            return delimiter;
        }
        if (cmd == L"text" || cmd == L"textrm" || cmd == L"textbf" || cmd == L"textit") {
            auto text = textGroup();
            if (text && cmd == L"textbf") markBold(text);
            if (text && cmd == L"textit") markItalic(text);
            return text;
        }
        if (cmd == L"operatorname") {
            bool limits = peek() == L'*';
            if (limits) ++pos;
            auto op = textGroup();
            if (op) { op->spacing = 0.16f; op->limitOp = limits; }
            return op;
        }

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
            cmd == L"quad" || cmd == L"qquad" || cmd == L"!" || cmd == L">" ||
            cmd == L"thinspace" || cmd == L"medspace" || cmd == L"thickspace" ||
            cmd == L"enspace" || cmd == L"negthinspace") {
            MNodePtr s = mk(MNode::Space);
            if (cmd == L"," || cmd == L"thinspace") s->space = 3.0f / 18;
            else if (cmd == L":" || cmd == L">" || cmd == L"medspace") s->space = 4.0f / 18;
            else if (cmd == L";" || cmd == L"thickspace") s->space = 5.0f / 18;
            else if (cmd == L" ") s->space = 0.33f;
            else if (cmd == L"quad") s->space = 1.0f;
            else if (cmd == L"qquad") s->space = 2.0f;
            else if (cmd == L"enspace") s->space = 0.5f;
            else s->space = -3.0f / 18;
            return s;
        }

        if (cmd == L"frac" || cmd == L"dfrac" || cmd == L"tfrac" || cmd == L"cfrac" ||
            cmd == L"binom" || cmd == L"dbinom" || cmd == L"tbinom") {
            MNodePtr f = mk(MNode::Frac);
            if (cmd == L"dfrac" || cmd == L"dbinom" || cmd == L"cfrac") f->style = 0;
            if (cmd == L"tfrac" || cmd == L"tbinom") f->style = 1;
            f->kids.push_back(parseArg());
            f->kids.push_back(parseArg());
            if (!f->kids[0] || !f->kids[1]) failed = true;
            if (cmd == L"binom" || cmd == L"dbinom" || cmd == L"tbinom") {
                f->noBar = true;
                auto d = mk(MNode::Delim);
                d->open = L'('; d->close = L')'; d->kids = {f};
                return d;
            }
            return f;
        }
        if (cmd == L"sqrt") {
            MNodePtr d = mk(MNode::Deco);
            d->decoKind = 3;
            skipWs();
            MNodePtr index;
            if (peek() == L'[') {
                ++pos; index = parseRow(L']');
                if (peek() != L']') failed = true; else ++pos;
            }
            d->kids.push_back(parseArg());
            d->kids.push_back(index);
            if (!d->kids[0]) failed = true;
            return d;
        }
        if (cmd == L"overset" || cmd == L"underset" || cmd == L"stackrel") {
            auto annotation = parseArg();
            auto base = parseArg();
            if (!annotation || !base) failed = true;
            auto stack = mk(MNode::Stack);
            stack->kids = {base, cmd == L"underset" ? annotation : nullptr,
                                cmd != L"underset" ? annotation : nullptr};
            return stack;
        }
        if (cmd == L"xrightarrow" || cmd == L"xleftarrow") {
            auto stack = mk(MNode::Stack);
            stack->decoKind = cmd == L"xrightarrow" ? 1 : 2;
            stack->spacing = 0.26f;
            skipWs(); MNodePtr below;
            if (peek() == L'[') {
                ++pos; below = parseRow(L']');
                if (peek() != L']') failed = true; else ++pos;
            }
            auto above = parseArg();
            if (!above) failed = true;
            stack->kids = {nullptr, below, above};
            return stack;
        }
        if (cmd == L"substack") {
            skipWs();
            if (peek() != L'{') { failed = true; return nullptr; }
            ++pos; auto grid = mk(MNode::Grid);
            // The surrounding script already supplies the smaller math style.
            do {
                grid->cells.push_back({parseRow(L'}', false, true)});
                if (grid->cells.size() > 64) { failed = true; break; }
                if (src.compare(pos, 2, L"\\\\") != 0) break;
                pos += 2;
            } while (!failed);
            if (peek() != L'}') failed = true; else ++pos;
            return grid;
        }
        const std::pair<const wchar_t*, int> decorations[] = {
            {L"dot", 5}, {L"ddot", 6}, {L"tilde", 7}, {L"widetilde", 7},
            {L"underline", 8}, {L"boxed", 9}, {L"cancel", 10}, {L"bcancel", 11},
            {L"overbrace", 12}, {L"underbrace", 13}, {L"acute", 14}, {L"grave", 15},
            {L"breve", 16}, {L"check", 17}, {L"not", 18}, {L"xcancel", 19},
        };
        for (const auto& deco : decorations) if (cmd == deco.first) {
            auto d = mk(MNode::Deco);
            d->decoKind = deco.second; d->kids = {parseArg()};
            d->limitOp = d->decoKind == 12 || d->decoKind == 13;
            if (!d->kids[0]) failed = true;
            return d;
        }
        if (cmd == L"phantom" || cmd == L"hphantom" || cmd == L"vphantom" || cmd == L"smash") {
            auto p = mk(MNode::Phantom);
            p->decoKind = cmd == L"hphantom" ? 1 : cmd == L"vphantom" ? 2 : cmd == L"smash" ? 3 : 0;
            p->kids = {parseArg()};
            if (!p->kids[0]) failed = true;
            return p;
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
            Nest delimiter(delimiterDepth);
            wchar_t open = readDelimiter();
            auto content = parseRow(0, true);
            wchar_t close = 0;
            if (!atCommand(L"\\right")) failed = true;
            else { pos += 6; close = readDelimiter(); }
            auto d = mk(MNode::Delim);
            d->open = open; d->close = close;
            d->kids = {content ? content : mk(MNode::Row)};
            return d;
        }
        if (cmd == L"mathrm") {
            MNodePtr arg = parseArg();
            if (arg) arg = markRoman(arg);
            return arg;
        }
        if (cmd == L"mathbf" || cmd == L"boldsymbol" || cmd == L"bm") {
            MNodePtr arg = parseArg();
            if (arg) arg = markBold(arg);
            if (arg && cmd == L"mathbf") markRoman(arg);
            return arg;
        }
        if (cmd == L"mathbb" || cmd == L"mathcal" || cmd == L"mathscr" ||
            cmd == L"mathfrak" || cmd == L"mathsf" || cmd == L"mathtt") {
            MNodePtr arg = parseArg();
            mathAlphabet(arg, cmd);
            return arg;
        }
        if (cmd == L"mathit") {
            auto arg = parseArg();
            if (arg) markItalic(arg);
            return arg;
        }
        // Convenience spellings remain definable with DeclareMathOperator,
        // as they are not predefined operators in amsmath.
        if (isFunctionName(cmd) || cmd == L"argmax" || cmd == L"argmin") {
            MNodePtr f = mk(MNode::Sym);
            f->text = cmd;
            f->roman = true;
            f->spacing = 0.16f;
            f->limitOp = cmd == L"lim" || cmd == L"limsup" || cmd == L"liminf" ||
                         cmd == L"max" || cmd == L"min" || cmd == L"sup" || cmd == L"inf" ||
                         cmd == L"argmax" || cmd == L"argmin";
            return f;
        }
        if (const wchar_t* sym = lookupSymbol(cmd)) {
            MNodePtr s = mk(MNode::Sym);
            s->text = sym;
            s->largeOp = cmd == L"sum" || cmd == L"prod" || cmd == L"coprod" ||
                         cmd == L"int" || cmd == L"iint" || cmd == L"iiint" ||
                         cmd == L"oint" || cmd == L"oiint" || cmd == L"bigcup" ||
                         cmd == L"bigcap" || cmd == L"bigvee" || cmd == L"bigwedge" ||
                         cmd == L"bigoplus" || cmd == L"bigotimes" || cmd == L"biguplus";
            s->limitOp = s->largeOp && cmd.find(L"int") == std::wstring::npos;
            if (s->largeOp) s->spacing = 0.16f;
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
        for (auto& k : n->kids) {
            if (k) markBold(k);
        }
        for (auto& row : n->cells) for (auto& cell : row) if (cell) markBold(cell);
        return n;
    }
    static void markItalic(const MNodePtr& n) {
        if (!n) return;
        n->forceItalic = true;
        for (const auto& k : n->kids) markItalic(k);
        for (const auto& row : n->cells) for (const auto& cell : row) markItalic(cell);
    }

    MNodePtr parseRow(wchar_t terminator, bool stopAtRight = false, bool cell = false) {
        Nest nest(depth);
        if (depth > 96) { failed = true; return nullptr; }
        MNodePtr row = mk(MNode::Row);
        int activeStyle = -1;
        while (!eof() && !failed) {
            skipWs();
            if (eof()) break;
            if (terminator && peek() == terminator) break;
            if (cell && (peek() == L'&' || src.compare(pos, 2, L"\\\\") == 0 ||
                         atCommand(L"\\cr") || atCommand(L"\\end"))) break;
            if (stopAtRight && atCommand(L"\\right")) {
                break;
            }
            const wchar_t* styles[] = {L"\\displaystyle", L"\\textstyle", L"\\scriptstyle", L"\\scriptscriptstyle"};
            bool directive = false;
            for (int style = 0; style < 4; ++style) if (atCommand(styles[style])) {
                pos += std::char_traits<wchar_t>::length(styles[style]);
                activeStyle = style; directive = true; break;
            }
            if (directive) continue;
            if (atCommand(L"\\limits") || atCommand(L"\\nolimits")) {
                bool limits = atCommand(L"\\limits");
                pos += limits ? 7 : 9;
                if (row->kids.empty()) { failed = true; break; }
                auto base = row->kids.back();
                if (base->kind == MNode::Script) base = base->kids[0];
                if (!base->largeOp && !base->limitOp) { failed = true; break; }
                base->limits = limits ? 1 : 0;
                continue;
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
                    script->style = activeStyle;
                }
                while (!eof() && (peek() == L'^' || peek() == L'_')) {
                    wchar_t which = src[pos++];
                    MNodePtr arg = parseArg();
                    if (!arg) { failed = true; break; }
                    int slot = which == L'^' ? 2 : 1;
                    if (script->kids[slot]) { failed = true; break; }
                    script->kids[slot] = arg;
                    skipWs();
                }
                row->kids.push_back(script);
                continue;
            }
            MNodePtr atom = parseAtom();
            if (atom) {
                if (atom->style < 0) atom->style = activeStyle;
                row->kids.push_back(atom);
            }
        }
        return row;
    }
};


MNodePtr parseLatex(const std::wstring& source) {
    if (source.size() > 32768) return nullptr;
    std::wstring expanded;
    if (!expandLatexMacros(source, expanded)) return nullptr;
    Parser parser(expanded);
    auto root = parser.parseRow(0);
    return parser.failed || !root || root->kids.empty() ? nullptr : root;
}

bool isBuiltinMathCommand(const std::wstring& command) {
    if (lookupSymbol(command) || isFunctionName(command)) return true;
    const std::wstring names = L" begin end frac dfrac tfrac cfrac binom dbinom tbinom sqrt "
        L"left right middle big Big bigg Bigg bigl bigr Bigl Bigr biggl biggr Biggl Biggr "
        L"text textrm textbf textit operatorname mathrm mathit mathbb mathcal mathscr mathfrak mathsf mathtt "
        L"mathbf boldsymbol bm displaystyle textstyle scriptstyle scriptscriptstyle limits nolimits "
        L"overline bar overrightarrow vec hat widehat dot ddot tilde widetilde underline boxed "
        L"cancel bcancel xcancel overbrace underbrace acute grave breve check not "
        L"phantom hphantom vphantom smash hspace kern mkern quad qquad thinspace medspace thickspace enspace "
        L"overset underset stackrel substack xrightarrow xleftarrow pmod bmod pod newcommand renewcommand "
        L"providecommand def DeclareMathOperator ";
    return names.find(L" " + command + L" ") != std::wstring::npos;
}

} // namespace tinta_math
