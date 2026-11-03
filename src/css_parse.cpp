// Sheets for Tinta's CSS (css.h): rules, selectors, declarations and the
// at-rules a theme uses, with CSS Syntax 3's error recovery; and the sheet
// written back out with its imports inlined.

#pragma optimize("s", on)

#include "css_internal.h"

#include <algorithm>
#include <cstring>

namespace css {
namespace {

// --- Selectors -------------------------------------------------------------------------

const uint32_t idWeight = 1u << 20, classWeight = 1u << 10, typeWeight = 1;

const char* const knownTags[] = {
    "a", "blockquote", "body", "br", "code", "del", "div", "em", "figcaption", "figure", "h1", "h2",
    "h3", "h4", "h5", "h6", "hr", "html", "img", "input", "ins", "kbd", "li", "mark", "ol", "p", "pre",
    "rp", "rt", "ruby", "s", "section", "span", "strong", "sub", "sup", "svg", "table", "tbody", "td",
    "th", "thead", "tr", "u", "ul",
};
const char* const knownClasses[] = {
    "alert-caution", "alert-important", "alert-note", "alert-tip", "alert-title", "alert-warning",
    "callout", "citation", "diagram", "footnote", "footnote-backlinks", "footnotes", "frontmatter",
    "math-display", "math-inline", "math-tag", "property", "references", "task", "token", "wikilink",
};
const char* const unsupportedClasses[] = {
    "hover", "active", "focus", "focus-within", "focus-visible", "visited", "link", "any-link",
    "local-link", "target", "target-within", "scope", "has", "disabled", "enabled", "indeterminate",
    "default", "required", "optional", "valid", "invalid", "user-valid", "user-invalid", "in-range",
    "out-of-range", "read-only", "read-write", "placeholder-shown", "autofill", "blank", "defined",
    "fullscreen", "modal", "popover-open", "picture-in-picture", "open", "closed", "playing", "paused",
    "current", "past", "future", "host", "host-context", "dir", "state", "nth-col", "nth-last-col",
    "first", "left", "right",
};
const char* const unsupportedElements[] = {
    "first-line", "first-letter", "placeholder", "backdrop", "file-selector-button", "cue",
    "cue-region", "part", "slotted", "highlight", "spelling-error", "grammar-error", "target-text",
    "details-content", "search-text",
};

template <size_t N>
bool listed(std::string_view name, const char* const (&list)[N]) {
    for (const char* entry : list) {
        if (name == entry) return true;
    }
    return false;
}

template <size_t N>
bool sortedListed(std::string_view name, const char* const (&list)[N]) {
    auto it = std::lower_bound(std::begin(list), std::end(list), name,
                               [](const char* entry, std::string_view key) { return key.compare(entry) > 0; });
    return it != std::end(list) && name == *it;
}

uint32_t addWeight(uint32_t specificity, uint32_t weight) {
    uint32_t sum = specificity;
    for (uint32_t shift : {20u, 10u, 0u}) {
        uint32_t field = std::min(1023u, ((sum >> shift) & 1023u) + ((weight >> shift) & 1023u));
        sum = (sum & ~(1023u << shift)) | field << shift;
    }
    return sum;
}

struct SelectorReader {
    std::string_view source;
    std::string error;
    bool unsupported = false;
    uint32_t errorAt = 0;
    std::vector<std::pair<uint32_t, uint32_t>> spans;  // where each top-level selector is written

    bool fail(uint32_t at, std::string message, bool notSupported = false) {
        if (error.empty()) {
            error = std::move(message);
            errorAt = at;
            unsupported = notSupported;
        }
        return false;
    }
    std::string text(const std::vector<Node>& nodes) const {
        if (nodes.empty()) return {};
        uint32_t begin = nodes.front().token.begin, end = nodes.back().token.end;
        return std::string(source.substr(begin, end - begin));
    }

    bool list(const std::vector<Node>& nodes, std::vector<Complex>& out, bool top) {
        bool one = std::none_of(nodes.begin(), nodes.end(), [](const Node& n) { return n.is(Tok::Comma); });
        std::vector<std::vector<Node>> split;
        if (!one) split = splitCommas(nodes);
        for (size_t i = 0; i < (one ? 1 : split.size()); ++i) {
            const std::vector<Node>& part = one ? nodes : split[i];
            if (part.empty()) {
                return fail(nodes.empty() ? 0 : nodes.front().token.begin, "an empty selector in a list");
            }
            Complex complex;
            if (!readComplex(part, complex)) return false;
            if (top) spans.emplace_back(part.front().token.begin, part.back().token.end);
            out.push_back(std::move(complex));
        }
        return !out.empty();
    }

    bool readComplex(const std::vector<Node>& n, Complex& out) {
        std::vector<Compound> sequence;
        std::vector<Combinator> combinators;
        int8_t pseudo = -1;
        uint32_t specificity = 0;
        size_t i = 0;
        for (;;) {
            if (pseudo >= 0) return fail(n[i].token.begin, "nothing may follow a pseudo-element in a selector");
            Compound compound;
            if (!readCompound(n, i, compound, pseudo, specificity)) return false;
            sequence.push_back(std::move(compound));
            bool space = false;
            while (i < n.size() && n[i].is(Tok::Space)) {
                ++i;
                space = true;
            }
            if (i >= n.size()) break;
            Combinator combinator = Combinator::Descendant;
            if (n[i].delim('>')) combinator = Combinator::Child;
            else if (n[i].delim('+')) combinator = Combinator::Next;
            else if (n[i].delim('~')) combinator = Combinator::Subsequent;
            else if (n[i].delim('|')) return fail(n[i].token.begin, "the column combinator `||` is not supported", true);
            else if (!space) return fail(n[i].token.begin, "unexpected `" + text({n[i]}) + "` in a selector");
            if (combinator != Combinator::Descendant || !space) {
                ++i;
                while (i < n.size() && n[i].is(Tok::Space)) ++i;
                if (i >= n.size()) return fail(n.back().token.begin, "a selector cannot end with a combinator");
            }
            combinators.push_back(combinator);
        }
        std::reverse(sequence.begin(), sequence.end());
        std::reverse(combinators.begin(), combinators.end());
        out.compounds = std::move(sequence);
        out.combinators = std::move(combinators);
        out.specificity = specificity;
        out.pseudo = pseudo;
        return true;
    }

    bool readCompound(const std::vector<Node>& n, size_t& i, Compound& c, int8_t& pseudo, uint32_t& specificity) {
        size_t start = i;
        if (i < n.size() && n[i].is(Tok::Ident)) {
            c.tag = toLower(n[i++].token.text);
            specificity = addWeight(specificity, typeWeight);
        } else if (i < n.size() && n[i].delim('*')) {
            ++i;
        }
        if (i < n.size() && n[i].delim('|')) return fail(n[i].token.begin, "namespaces in selectors are not supported", true);
        while (i < n.size()) {
            const Node& t = n[i];
            if (t.is(Tok::Hash)) {
                if (!t.token.idHash) return fail(t.token.begin, "`#" + t.token.text + "` is not a valid id");
                c.id = c.id.empty() || c.id == t.token.text ? t.token.text : std::string("\x01");
                specificity = addWeight(specificity, idWeight);
                ++i;
            } else if (t.delim('.')) {
                if (i + 1 >= n.size() || !n[i + 1].is(Tok::Ident)) return fail(t.token.begin, "expected a class name after `.`");
                c.classes.push_back(n[i + 1].token.text);
                specificity = addWeight(specificity, classWeight);
                i += 2;
            } else if (t.is(Tok::OpenSquare)) {
                AttrTest test;
                if (!readAttribute(t, test)) return false;
                c.attrs.push_back(std::move(test));
                specificity = addWeight(specificity, classWeight);
                ++i;
            } else if (t.is(Tok::Colon)) {
                if (!readPseudo(n, i, c, pseudo, specificity)) return false;
            } else {
                break;
            }
        }
        if (i == start) {
            return fail(i < n.size() ? n[i].token.begin : n.back().token.end,
                        i < n.size() ? "unexpected `" + text({n[i]}) + "` in a selector" : "expected a selector");
        }
        return true;
    }

    bool readAttribute(const Node& block, AttrTest& out) {
        std::vector<const Node*> p = parts(block.children);
        if (p.empty() || !p[0]->is(Tok::Ident)) return fail(block.token.begin, "expected an attribute name in `[...]`");
        if (p.size() > 2 && p[1]->delim('|') && !p[2]->delim('=')) {
            return fail(block.token.begin, "namespaces in selectors are not supported", true);
        }
        out.name = toLower(p[0]->token.text);
        size_t k = 1;
        if (k == p.size()) return true;
        if (p[k]->delim('=')) {
            out.op = '=';
            ++k;
        } else if (p[k]->is(Tok::Delim) && p[k]->token.delim && std::strchr("~|^$*", p[k]->token.delim) &&
                   k + 1 < p.size() && p[k + 1]->delim('=')) {
            out.op = p[k]->token.delim;
            k += 2;
        } else {
            return fail(p[k]->token.begin, "unexpected `" + text({*p[k]}) + "` in an attribute selector");
        }
        if (k >= p.size() || !(p[k]->is(Tok::Ident) || p[k]->is(Tok::String))) {
            return fail(block.token.begin, "expected a value in an attribute selector");
        }
        out.value = p[k++]->token.text;
        if (k < p.size()) {
            if (p[k]->ident("i")) out.caseless = true;
            else if (!p[k]->ident("s")) return fail(p[k]->token.begin, "unexpected `" + text({*p[k]}) + "` in an attribute selector");
            ++k;
        }
        if (k != p.size()) return fail(p[k]->token.begin, "unexpected `" + text({*p[k]}) + "` in an attribute selector");
        return true;
    }

    bool readNth(const Node& fn, PseudoClass& out) {
        std::vector<Node> args = fn.children;
        trimSpace(args);
        for (const Node& n : args) {
            if (n.ident("of")) return fail(n.token.begin, "`of` in `:" + toLower(fn.token.text) + "()` is not supported", true);
        }
        std::string s;
        for (char c : text(args)) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') s += (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        size_t p = 0;
        auto digits = [&](int& value) {
            size_t first = p;
            long long v = 0;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9') v = std::min(v * 10 + (s[p++] - '0'), 100000000LL);
            value = (int)v;
            return p > first;
        };
        auto bad = [&] { return fail(fn.token.begin, "`" + text(args) + "` is not a valid An+B"); };
        if (s == "odd" || s == "even") {
            out.a = 2;
            out.b = s == "odd" ? 1 : 0;
            return true;
        }
        int sign = 1, coefficient = 0;
        if (p < s.size() && (s[p] == '+' || s[p] == '-')) sign = s[p++] == '-' ? -1 : 1;
        bool hasCoefficient = digits(coefficient);
        if (p < s.size() && s[p] == 'n') {
            ++p;
            out.a = sign * (hasCoefficient ? coefficient : 1);
            out.b = 0;
            if (p == s.size()) return true;
            if (s[p] != '+' && s[p] != '-') return bad();
            int offsetSign = s[p++] == '-' ? -1 : 1, offset = 0;
            if (!digits(offset) || p != s.size()) return bad();
            out.b = offsetSign * offset;
            return true;
        }
        if (!hasCoefficient || p != s.size()) return bad();
        out.a = 0;
        out.b = sign * coefficient;
        return true;
    }

    bool readPseudo(const std::vector<Node>& n, size_t& i, Compound& c, int8_t& pseudo, uint32_t& specificity) {
        bool element = i + 1 < n.size() && n[i + 1].is(Tok::Colon);
        size_t at = i + (element ? 2 : 1);
        if (at >= n.size() || !(n[at].is(Tok::Ident) || n[at].is(Tok::Function))) {
            return fail(n[i].token.begin, "expected a pseudo-class name after `:`");
        }
        const Node& t = n[at];
        i = at + 1;
        std::string name = toLower(t.token.text);
        std::string written = (element ? "::" : ":") + name + (t.is(Tok::Function) ? "()" : "");
        if (pseudo >= 0) return fail(t.token.begin, "`" + written + "` after a pseudo-element is not supported", true);
        bool vendor = !name.empty() && name[0] == '-';
        if (element || (t.is(Tok::Ident) && (name == "before" || name == "after" || name == "first-line" ||
                                             name == "first-letter"))) {
            int8_t which = name == "before" ? (int8_t)Pseudo::Before : name == "after" ? (int8_t)Pseudo::After
                         : element && name == "marker" ? (int8_t)Pseudo::Marker
                         : element && name == "selection" ? (int8_t)Pseudo::Selection : (int8_t)-1;
            if (which < 0 || t.is(Tok::Function)) {
                bool known = vendor || listed(name, unsupportedElements) || name == "marker" || name == "selection";
                return fail(t.token.begin, known ? "`" + written + "` is not supported" : "unknown pseudo-element `" + written + "`", known);
            }
            pseudo = which;
            specificity = addWeight(specificity, typeWeight);
            return true;
        }
        PseudoClass pc;
        if (t.is(Tok::Ident)) {
            if (name == "root") {
                pc.kind = PseudoClass::Root;
            } else if (name == "empty") {
                pc.kind = PseudoClass::Empty;
            } else if (name == "checked") {
                pc.kind = PseudoClass::Checked;
            } else if (name == "first-child" || name == "last-child" || name == "only-child" ||
                       name == "first-of-type" || name == "last-of-type" || name == "only-of-type") {
                pc.kind = PseudoClass::Nth;
                pc.b = 1;
                pc.ofType = name.find("of-type") != std::string::npos;
                pc.fromEnd = name[0] == 'l';
                if (name[0] == 'o') {
                    PseudoClass last = pc;
                    last.fromEnd = true;
                    c.pseudos.push_back(last);
                }
            } else {
                bool known = vendor || listed(name, unsupportedClasses);
                return fail(t.token.begin, known ? "`" + written + "` is not supported" : "unknown pseudo-class `" + written + "`", known);
            }
            c.pseudos.push_back(std::move(pc));
            specificity = addWeight(specificity, classWeight);
            return true;
        }
        if (name == "is" || name == "where" || name == "not" || name == "matches") {
            pc.kind = name == "not" ? PseudoClass::Not : PseudoClass::Is;
            SelectorReader inner{source};
            std::vector<Node> args = t.children;
            trimSpace(args);
            if (!inner.list(args, pc.list, false)) {
                return fail(inner.error.empty() ? t.token.begin : inner.errorAt,
                            inner.error.empty() ? "`" + written + "` needs a selector" : inner.error, inner.unsupported);
            }
            for (const Complex& x : pc.list) {
                if (x.pseudo >= 0) return fail(t.token.begin, "pseudo-elements cannot appear in `" + written + "`");
            }
            uint32_t most = 0;
            for (const Complex& x : pc.list) most = std::max(most, x.specificity);
            if (name != "where") specificity = addWeight(specificity, most);
        } else if (name == "nth-child" || name == "nth-last-child" || name == "nth-of-type" || name == "nth-last-of-type") {
            pc.kind = PseudoClass::Nth;
            pc.fromEnd = name.find("last") != std::string::npos;
            pc.ofType = name.find("of-type") != std::string::npos;
            if (!readNth(t, pc)) return false;
            specificity = addWeight(specificity, classWeight);
        } else if (name == "lang") {
            pc.kind = PseudoClass::Lang;
            for (const auto& arg : splitCommas(t.children)) {
                if (arg.size() != 1 || !(arg[0].is(Tok::Ident) || arg[0].is(Tok::String))) {
                    return fail(t.token.begin, "`:lang()` takes language tags");
                }
                pc.langs.push_back(toLower(arg[0].token.text));
            }
            specificity = addWeight(specificity, classWeight);
        } else {
            bool known = vendor || listed(name, unsupportedClasses);
            return fail(t.token.begin, known ? "`" + written + "` is not supported" : "unknown pseudo-class `" + written + "`", known);
        }
        c.pseudos.push_back(std::move(pc));
        return true;
    }
};

// A selector that names something Markdown never makes, as the warning says it
std::string unreachable(const Complex& complex) {
    for (const Compound& c : complex.compounds) {
        if (!c.tag.empty() && !sortedListed(c.tag, knownTags)) return "`" + c.tag + "`";
        bool open = c.tag.empty() || c.tag == "span" || c.tag == "code" || c.tag == "pre" || c.tag == "body" ||
                    c.tag == "html" || c.tag == "svg";
        if (open) continue;
        for (const std::string& name : c.classes) {
            if (!sortedListed(name, knownClasses) && !startsWithLower(name, "language-") && !startsWithLower(name, "hljs-") &&
                !startsWithLower(name, "csl-")) {
                return "`" + c.tag + "." + name + "`";
            }
        }
    }
    return {};
}

// --- Media queries -------------------------------------------------------------------

MediaExpr mediaOf(MediaExpr::Kind kind, float px = 0) {
    MediaExpr e;
    e.kind = kind;
    e.px = px;
    return e;
}

struct MediaReader {
    std::vector<std::string> warnings;

    MediaExpr never(const std::string& what) {
        warnings.push_back("`" + what + "` is never true in Tinta");
        return mediaOf(MediaExpr::Never);
    }

    bool range(const std::vector<const Node*>& q, MediaExpr& out) {
        NumEnv env;
        size_t i = 0;
        auto op = [&](std::string& s) {
            if (i >= q.size() || !q[i]->is(Tok::Delim)) return false;
            char c = q[i]->token.delim;
            if (c != '<' && c != '>' && c != '=') return false;
            s = std::string(1, c);
            ++i;
            if (c != '=' && i < q.size() && q[i]->delim('=')) {
                s += '=';
                ++i;
            }
            return true;
        };
        float left = 0, right = 0;
        std::string leftOp, rightOp;
        if (!q[0]->is(Tok::Ident)) {
            if (!readLength(*q[0], env, left)) return false;
            ++i;
            if (!op(leftOp)) return false;
        }
        if (i >= q.size() || !q[i]->is(Tok::Ident)) return false;
        std::string name = toLower(q[i++]->token.text);
        if (i < q.size() && (!op(rightOp) || i >= q.size() || !readLength(*q[i++], env, right))) return false;
        if (i != q.size() || (leftOp.empty() && rightOp.empty())) return false;
        if (name != "width") {
            out = never("(" + name + ")");
            return true;
        }
        out = mediaOf(MediaExpr::And);
        if (!leftOp.empty()) {
            if (leftOp[0] != '>') out.args.push_back(mediaOf(MediaExpr::MinWidth, left));
            if (leftOp[0] != '<') out.args.push_back(mediaOf(MediaExpr::MaxWidth, left));
        }
        if (!rightOp.empty()) {
            if (rightOp[0] != '>') out.args.push_back(mediaOf(MediaExpr::MaxWidth, right));
            if (rightOp[0] != '<') out.args.push_back(mediaOf(MediaExpr::MinWidth, right));
        }
        return true;
    }

    bool feature(const std::vector<Node>& children, MediaExpr& out) {
        std::vector<const Node*> p = parts(children);
        if (p.size() == 1 && p[0]->is(Tok::Ident)) {
            std::string name = toLower(p[0]->token.text);
            out = name == "width" || name == "prefers-color-scheme" ? mediaOf(MediaExpr::All) : never("(" + name + ")");
            return true;
        }
        if (p.size() == 3 && p[0]->is(Tok::Ident) && p[1]->is(Tok::Colon)) {
            std::string name = toLower(p[0]->token.text);
            float px;
            if (name == "prefers-color-scheme") {
                if (p[2]->ident("dark")) out = mediaOf(MediaExpr::Dark);
                else if (p[2]->ident("light")) out = mediaOf(MediaExpr::Light);
                else return false;
            } else if (name == "min-width" || name == "max-width" || name == "width") {
                if (!readLength(*p[2], NumEnv(), px)) return false;
                if (name == "width") {
                    out = mediaOf(MediaExpr::And);
                    out.args = {mediaOf(MediaExpr::MinWidth, px), mediaOf(MediaExpr::MaxWidth, px)};
                } else {
                    out = mediaOf(name[1] == 'i' ? MediaExpr::MinWidth : MediaExpr::MaxWidth, px);
                }
            } else {
                out = never("(" + name + ")");
            }
            return true;
        }
        return p.size() >= 3 && range(p, out);
    }

    bool inParens(const Node& n, MediaExpr& out) {
        if (n.is(Tok::Function)) {
            out = never(n.token.text + "()");
            return true;
        }
        if (!n.is(Tok::OpenParen)) return false;
        std::vector<const Node*> p = parts(n.children);
        if (p.empty()) return false;
        if (p[0]->ident("not") || p[0]->is(Tok::OpenParen)) {
            size_t i = 0;
            return condition(p, i, out) && i == p.size();
        }
        return feature(n.children, out);
    }

    bool condition(const std::vector<const Node*>& p, size_t& i, MediaExpr& out) {
        if (i < p.size() && p[i]->ident("not")) {
            MediaExpr inner;
            if (++i >= p.size() || !inParens(*p[i++], inner)) return false;
            out = mediaOf(MediaExpr::Not);
            out.args.push_back(std::move(inner));
            return true;
        }
        MediaExpr first;
        if (i >= p.size() || !inParens(*p[i++], first)) return false;
        if (i == p.size() || !(p[i]->ident("and") || p[i]->ident("or"))) {
            out = std::move(first);
            return true;
        }
        const char* joiner = p[i]->ident("and") ? "and" : "or";
        out = mediaOf(joiner[0] == 'a' ? MediaExpr::And : MediaExpr::Or);
        out.args.push_back(std::move(first));
        while (i < p.size() && p[i]->ident(joiner)) {
            MediaExpr next;
            if (++i >= p.size() || !inParens(*p[i++], next)) return false;
            out.args.push_back(std::move(next));
        }
        return true;
    }

    bool query(const std::vector<Node>& q, MediaExpr& out) {
        std::vector<const Node*> p = parts(q);
        if (p.empty()) return false;
        size_t i = 0;
        bool negate = p[0]->ident("not") && p.size() > 1 && p[1]->is(Tok::Ident);
        if (negate || (p[0]->ident("only") && p.size() > 1)) ++i;
        if (p[i]->is(Tok::Ident) && !p[i]->ident("not")) {
            MediaExpr type = p[i]->ident("all") ? mediaOf(MediaExpr::All) : p[i]->ident("screen") ? mediaOf(MediaExpr::Screen)
                           : p[i]->ident("print") ? mediaOf(MediaExpr::Print) : mediaOf(MediaExpr::Never);
            ++i;
            out = type;
            if (i < p.size()) {
                MediaExpr cond;
                if (!p[i]->ident("and") || !condition(p, ++i, cond) || i != p.size() || cond.kind == MediaExpr::Or) {
                    return false;
                }
                out = mediaOf(MediaExpr::And);
                out.args = {type, cond};
            }
            if (negate) {
                MediaExpr inner = std::move(out);
                out = mediaOf(MediaExpr::Not);
                out.args.push_back(std::move(inner));
            }
            return true;
        }
        return !negate && condition(p, i, out) && i == p.size();
    }

    // A comma list; a query that does not parse never matches
    MediaExpr list(const std::vector<Node>& nodes, bool& ok) {
        ok = true;
        if (parts(nodes).empty()) return mediaOf(MediaExpr::All);
        MediaExpr any = mediaOf(MediaExpr::Or);
        for (const auto& q : splitCommas(nodes)) {
            MediaExpr e;
            if (!query(q, e)) {
                ok = false;
                e = mediaOf(MediaExpr::Never);
            }
            any.args.push_back(std::move(e));
        }
        return any.args.size() == 1 ? any.args[0] : any;
    }
};

// --- @supports ----------------------------------------------------------------------

bool supportedDeclaration(const std::string& name, std::vector<Node> value) {
    trimSpace(value);
    if (name.size() > 2 && name[0] == '-' && name[1] == '-') return true;
    int id = propertyId(toLower(name));
    if (id < 0 || value.empty()) return false;
    if (cssWideKeyword(value) || hasVar(value)) return true;
    Env env;
    Computed scratch;
    std::vector<std::pair<Prop, std::vector<Node>>> longhandValues;
    return id < (int)Prop::Count ? computeLonghand((Prop)id, value, env, scratch)
                                 : expandShorthand(id, value, env, longhandValues);
}

struct SupportsReader {
    std::string_view source;

    bool inParens(const Node& n, bool& out) {
        if (n.function("selector")) {
            SelectorReader reader{source};
            std::vector<Complex> list;
            std::vector<Node> args = n.children;
            trimSpace(args);
            out = reader.list(args, list, false);
            return true;
        }
        if (n.is(Tok::Function)) {
            out = false;
            return true;
        }
        if (!n.is(Tok::OpenParen)) return false;
        std::vector<const Node*> p = parts(n.children);
        if (p.empty()) return false;
        if (p[0]->ident("not") || p[0]->is(Tok::OpenParen) || p[0]->is(Tok::Function)) {
            size_t i = 0;
            return condition(p, i, out) && i == p.size();
        }
        if (p.size() < 2 || !p[0]->is(Tok::Ident) || !p[1]->is(Tok::Colon)) {
            out = false;
            return true;
        }
        auto colon = std::find_if(n.children.begin(), n.children.end(), [](const Node& c) { return c.is(Tok::Colon); });
        out = supportedDeclaration(p[0]->token.text, std::vector<Node>(colon + 1, n.children.end()));
        return true;
    }

    bool condition(const std::vector<const Node*>& p, size_t& i, bool& out) {
        if (i < p.size() && p[i]->ident("not")) {
            bool inner;
            if (++i >= p.size() || !inParens(*p[i++], inner)) return false;
            out = !inner;
            return true;
        }
        if (i >= p.size() || !inParens(*p[i++], out)) return false;
        if (i == p.size() || !(p[i]->ident("and") || p[i]->ident("or"))) return true;
        const char* joiner = p[i]->ident("and") ? "and" : "or";
        while (i < p.size() && p[i]->ident(joiner)) {
            bool next;
            if (++i >= p.size() || !inParens(*p[i++], next)) return false;
            out = joiner[0] == 'a' ? out && next : out || next;
        }
        return true;
    }
};

}  // namespace

bool matchMedia(const MediaExpr& e, const Media& media) {
    switch (e.kind) {
        case MediaExpr::All: return true;
        case MediaExpr::Screen: return !media.print;
        case MediaExpr::Print: return media.print;
        case MediaExpr::Never: return false;
        case MediaExpr::Dark: return media.dark;
        case MediaExpr::Light: return !media.dark;
        case MediaExpr::MinWidth: return media.width >= e.px;
        case MediaExpr::MaxWidth: return media.width <= e.px;
        case MediaExpr::Not: return !matchMedia(e.args[0], media);
        case MediaExpr::And:
            for (const MediaExpr& a : e.args) {
                if (!matchMedia(a, media)) return false;
            }
            return true;
        case MediaExpr::Or:
            for (const MediaExpr& a : e.args) {
                if (matchMedia(a, media)) return true;
            }
            return false;
    }
    return false;
}

// --- Rules -----------------------------------------------------------------------------------

namespace {

struct Scope {
    std::vector<std::shared_ptr<const MediaExpr>> media;
    bool supported = true;
};

std::string_view withoutBom(std::string_view text) {
    return text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF" ? text.substr(3) : text;
}

bool hasScheme(const std::string& url) {
    size_t colon = url.find(':');
    if (colon == std::string::npos || colon < 2) return false;
    for (size_t i = 0; i < colon; ++i) {
        char c = url[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '+' || c == '-' || c == '.')) return false;
    }
    return true;
}

struct Parser {
    Sheet& sheet;
    const ImportSource& imports;
    std::vector<Diagnostic>& diagnostics;
    std::vector<std::string>& stack;  // the files being read, for cycles
    std::vector<Token> tokens;
    Lines lines;
    std::shared_ptr<const std::string> file;
    bool rulesSeen = false;
    int depth = 0;  // of @media and @supports blocks

    Parser(Sheet& s, const ImportSource& i, std::vector<Diagnostic>& d, std::vector<std::string>& files)
        : sheet(s), imports(i), diagnostics(d), stack(files), tokens(tokenize(s.text)), lines(s.text),
          file(std::make_shared<const std::string>(s.file)) {}

    void run() { ruleList(0, tokens.size(), Scope(), true); }

    void nestedRules(size_t begin, size_t end, const Scope& scope, uint32_t at) {
        if (depth >= 32) {
            report(Diagnostic::Error, at, "at-rules nest too deeply; the rules inside are skipped");
            return;
        }
        ++depth;
        ruleList(begin, end, scope, false);
        --depth;
    }

    void report(Diagnostic::Level level, uint32_t offset, std::string message) {
        Diagnostic d;
        d.level = level;
        d.file = sheet.file;
        lines.at(sheet.text, offset, d.line, d.column);
        d.message = std::move(message);
        diagnostics.push_back(std::move(d));
    }
    std::string written(const std::vector<Node>& nodes) const {
        if (nodes.empty()) return {};
        return sheet.text.substr(nodes.front().token.begin, nodes.back().token.end - nodes.front().token.begin);
    }
    std::string written(size_t begin, size_t end) const {
        if (begin >= end) return {};
        return sheet.text.substr(tokens[begin].begin, tokens[end - 1].end - tokens[begin].begin);
    }
    // Source text for a message: on one line, and not too long
    static std::string quote(const std::string& text) {
        std::string out;
        for (char c : text) {
            bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
            if (!space) out += c;
            else if (!out.empty() && out.back() != ' ') out += ' ';
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (out.size() > 60) out = out.substr(0, 57) + "...";
        return out;
    }

    // The brace closing the block that opens at `open`, or `end`
    size_t closing(size_t open, size_t end) const {
        std::vector<Tok> want{Tok::CloseCurly};
        for (size_t at = open + 1; at < end; ++at) {
            Tok t = tokens[at].type;
            if (t == want.back()) {
                want.pop_back();
                if (want.empty()) return at;
            } else if (t == Tok::Function || t == Tok::OpenParen) {
                want.push_back(Tok::CloseParen);
            } else if (t == Tok::OpenSquare) {
                want.push_back(Tok::CloseSquare);
            } else if (t == Tok::OpenCurly) {
                want.push_back(Tok::CloseCurly);
            }
        }
        return end;
    }
    size_t past(size_t close, size_t end, size_t open) {
        if (close < end) return close + 1;
        report(Diagnostic::Warning, tokens[open].begin, "a block is missing its `}`");
        return end;
    }
    // Past a statement: its `;`, or the block it opens
    size_t skipStatement(size_t i, size_t end) {
        while (i < end && tokens[i].type != Tok::Semicolon && tokens[i].type != Tok::OpenCurly) i = skipNode(tokens, i, end);
        if (i < end && tokens[i].type == Tok::OpenCurly) return std::min(end, closing(i, end) + 1);
        return i < end ? i + 1 : end;
    }

    void ruleList(size_t begin, size_t end, const Scope& scope, bool top) {
        for (size_t i = begin; i < end;) {
            Tok t = tokens[i].type;
            if (t == Tok::Space || (top && (t == Tok::CDO || t == Tok::CDC))) {
                ++i;
            } else if (t == Tok::AtKeyword) {
                i = atRule(i, end, scope, top);
            } else {
                i = qualifiedRule(i, end, scope);
            }
        }
    }

    size_t qualifiedRule(size_t start, size_t end, const Scope& scope) {
        size_t open = start;
        while (open < end && tokens[open].type != Tok::OpenCurly) open = skipNode(tokens, open, end);
        if (open >= end) {
            report(Diagnostic::Error, tokens[start].begin, "`" + quote(written(start, end)) + "` has no `{` block");
            return end;
        }
        size_t close = closing(open, end), after = past(close, end, open);
        rulesSeen = true;
        std::vector<Node> prelude;
        size_t k = start;
        readNodes(tokens, k, open, prelude);
        trimSpace(prelude);
        Rule rule;
        SelectorReader reader{sheet.text};
        if (prelude.empty() || !reader.list(prelude, rule.selectors, true)) {
            bool warning = reader.unsupported;
            uint32_t at = reader.error.empty() ? tokens[start].begin : reader.errorAt;
            std::string why = reader.error.empty() ? "a rule needs a selector" : reader.error;
            report(warning ? Diagnostic::Warning : Diagnostic::Error, at, why + "; the rule is skipped");
            return after;
        }
        for (size_t s = 0; s < rule.selectors.size(); ++s) {
            std::string what = unreachable(rule.selectors[s]);
            if (!what.empty()) {
                const auto& span = reader.spans[s];
                std::string selector = "`" + quote(sheet.text.substr(span.first, span.second - span.first)) + "`";
                report(Diagnostic::Warning, prelude.front().token.begin,
                       selector + " can never match a Markdown page" + (what == selector ? "" : ": Markdown makes no " + what));
            }
        }
        rule.media = scope.media;
        rule.supported = scope.supported;
        rule.file = file;
        rule.declarations.reserve((size_t)std::count_if(tokens.begin() + (ptrdiff_t)open, tokens.begin() + (ptrdiff_t)std::min(close, end),
                                                        [](const Token& t) { return t.type == Tok::Semicolon; }) + 1);
        eachDeclaration(open + 1, std::min(close, end), [&](size_t i, size_t j) { declaration(i, j, rule.declarations); });
        if (!rule.declarations.empty()) sheet.rules.push_back(std::move(rule));
        return after;
    }

    // Each `name: value` in a block, nested rules and stray tokens reported
    template <class Each>
    void eachDeclaration(size_t begin, size_t end, Each each) {
        for (size_t i = begin; i < end;) {
            const Token& t = tokens[i];
            if (t.type == Tok::Space || t.type == Tok::Semicolon) {
                ++i;
                continue;
            }
            if (t.type == Tok::AtKeyword) {
                report(Diagnostic::Warning, t.begin, "`@" + toLower(t.text) + "` inside a rule is not supported; it is skipped");
                i = skipStatement(i, end);
                continue;
            }
            size_t j = i, block = end;
            while (j < end && tokens[j].type != Tok::Semicolon) {
                if (tokens[j].type == Tok::OpenCurly && block == end) block = j;
                j = skipNode(tokens, j, end);
            }
            bool custom = t.type == Tok::Ident && t.text.size() >= 2 && t.text[0] == '-' && t.text[1] == '-';
            if (block < end && !custom) {
                report(Diagnostic::Warning, t.begin, "nested rules are not supported; this one is skipped");
                i = std::min(end, closing(block, end) + 1);
                continue;
            }
            if (t.type != Tok::Ident) {
                report(Diagnostic::Error, t.begin, "unexpected `" + quote(written(i, i + 1)) + "`; expected a property");
                i = j;
                continue;
            }
            each(i, j);
            i = j;
        }
    }

    // A declaration's name and value, its !important taken off
    bool split(size_t i, size_t j, std::vector<Node>& value, bool& important) {
        size_t k = i + 1;
        while (k < j && tokens[k].type == Tok::Space) ++k;
        if (k >= j || tokens[k].type != Tok::Colon) {
            report(Diagnostic::Error, tokens[i].begin, "expected `:` after `" + tokens[i].text + "`");
            return false;
        }
        ++k;
        readNodes(tokens, k, j, value);
        trimSpace(value);
        important = false;
        size_t e = value.size();
        if (e && value[e - 1].ident("important")) {
            size_t b = e - 1;
            while (b && value[b - 1].is(Tok::Space)) --b;
            if (b && value[b - 1].delim('!')) {
                important = true;
                value.resize(b - 1);
                trimSpace(value);
            }
        }
        return true;
    }

    void declaration(size_t i, size_t j, std::vector<Declaration>& out) {
        const Token& nameToken = tokens[i];
        std::vector<Node> value;
        Declaration d;
        if (!split(i, j, value, d.important)) return;
        lines.at(sheet.text, nameToken.begin, d.line, d.column);
        const std::string& name = nameToken.text;
        if (name.size() >= 2 && name[0] == '-' && name[1] == '-') {
            d.prop = PropCustom;
            d.name = name;
            d.text = written(value);
            d.pending = hasVar(value);
            d.value = std::move(value);
            out.push_back(std::move(d));
            return;
        }
        std::string lower = toLower(name);
        if (value.empty()) {
            report(Diagnostic::Error, nameToken.begin, "`" + lower + "` has no value");
            return;
        }
        int id = propertyId(lower);
        if (id < 0) {
            if (const char* why = ignoredReason(lower)) {
                report(Diagnostic::Warning, nameToken.begin, "`" + lower + "` " + why);
            } else if (lower[0] == '-') {
                report(Diagnostic::Note, nameToken.begin, "`" + lower + "` belongs to one browser; Tinta ignores it");
            } else {
                std::string near = suggestProperty(lower);
                report(Diagnostic::Warning, nameToken.begin,
                       "unknown property `" + lower + "`" + (near.empty() ? std::string() : ": did you mean `" + near + "`?"));
            }
            return;
        }
        uint32_t valueAt = value.front().token.begin;
        d.prop = (uint16_t)id;
        bool wide = cssWideKeyword(value);
        if (wide || hasVar(value)) {
            d.pending = !wide;
            if (wide && id >= (int)Prop::Count) {
                for (Prop p : longhands(id)) {
                    Declaration l = d;
                    l.prop = (uint16_t)p;
                    l.value = value;
                    out.push_back(std::move(l));
                }
                return;
            }
            d.value = std::move(value);
            out.push_back(std::move(d));
            return;
        }
        std::vector<std::string> warnings;
        Env env;
        env.warnings = &warnings;
        Computed scratch;
        std::vector<std::pair<Prop, std::vector<Node>>> expanded;
        bool ok = id < (int)Prop::Count ? computeLonghand((Prop)id, value, env, scratch)
                                        : expandShorthand(id, value, env, expanded);
        if (!ok) {
            report(Diagnostic::Error, valueAt, "`" + quote(written(value)) + "` is not a valid value for `" + lower + "`");
            return;
        }
        if (id < (int)Prop::Count) {
            d.value = std::move(value);
            out.push_back(std::move(d));
        }
        for (auto& part : expanded) {
            Declaration l;
            l.prop = (uint16_t)part.first;
            l.important = d.important;
            l.line = d.line;
            l.column = d.column;
            l.value = std::move(part.second);
            out.push_back(std::move(l));
        }
        std::sort(warnings.begin(), warnings.end());
        warnings.erase(std::unique(warnings.begin(), warnings.end()), warnings.end());
        for (const std::string& w : warnings) report(Diagnostic::Warning, valueAt, "`" + lower + "`: " + w);
    }

    size_t atRule(size_t start, size_t end, const Scope& scope, bool top) {
        const Token& at = tokens[start];
        std::string name = toLower(at.text);
        size_t j = start + 1;
        while (j < end && tokens[j].type != Tok::Semicolon && tokens[j].type != Tok::OpenCurly) j = skipNode(tokens, j, end);
        std::vector<Node> prelude;
        size_t k = start + 1;
        readNodes(tokens, k, j, prelude);
        trimSpace(prelude);
        bool hasBlock = j < end && tokens[j].type == Tok::OpenCurly;
        size_t close = hasBlock ? closing(j, end) : j;
        size_t after = hasBlock ? past(close, end, j) : std::min(end, j + 1);
        if (name == "charset") {
            sheet.imports.push_back({at.begin, tokens[after - 1].end, nullptr, ""});
            return after;
        }
        if (name == "import") {
            importRule(start, after, prelude, hasBlock, top);
            return after;
        }
        if (name == "layer" && !hasBlock) {
            report(Diagnostic::Warning, at.begin, "`@layer` is not supported; it is skipped");
            return after;
        }
        rulesSeen = true;
        bool block = name == "media" || name == "supports" || name == "font-face";
        if (block && !hasBlock) {
            report(Diagnostic::Error, at.begin, "`@" + name + "` needs a `{` block");
            return after;
        }
        if (name == "media") {
            MediaReader reader;
            bool ok;
            MediaExpr e = reader.list(prelude, ok);
            if (!ok) report(Diagnostic::Error, at.begin, "`" + quote(written(prelude)) + "` is not a valid media query");
            for (const std::string& w : reader.warnings) report(Diagnostic::Warning, at.begin, w);
            Scope inner = scope;
            inner.media.push_back(std::make_shared<const MediaExpr>(std::move(e)));
            nestedRules(j + 1, close, inner, at.begin);
        } else if (name == "supports") {
            SupportsReader reader{sheet.text};
            std::vector<const Node*> p = parts(prelude);
            size_t q = 0;
            bool result = false;
            if (!reader.condition(p, q, result) || q != p.size()) {
                report(Diagnostic::Error, at.begin, "`" + quote(written(prelude)) + "` is not a valid @supports condition");
                result = false;
            }
            Scope inner = scope;
            inner.supported = scope.supported && result;
            nestedRules(j + 1, close, inner, at.begin);
        } else if (name == "font-face") {
            fontFace(at, j + 1, close);
        } else {
            static const char* const skipped[] = {
                "keyframes", "-webkit-keyframes", "-moz-keyframes", "layer", "container", "page",
                "namespace", "property", "counter-style", "font-feature-values", "font-palette-values",
                "scope", "starting-style", "view-transition", "position-try", "document",
                "-moz-document", "viewport", "-ms-viewport"};
            if (name.find("keyframes") != std::string::npos) {
                report(Diagnostic::Warning, at.begin, "`@keyframes` has no effect: Tinta does not animate");
            } else if (listed(name, skipped)) {
                report(Diagnostic::Warning, at.begin, "`@" + name + "` is not supported; it is skipped");
            } else {
                report(Diagnostic::Error, at.begin, "unknown at-rule `@" + name + "`");
            }
        }
        return after;
    }

    void importRule(size_t start, size_t after, const std::vector<Node>& prelude, bool hasBlock, bool top) {
        uint32_t begin = tokens[start].begin, finish = tokens[after - 1].end;
        auto drop = [&](const std::string& why) {
            report(Diagnostic::Error, begin, why);
            sheet.imports.push_back({begin, finish, nullptr, ""});
        };
        if (!top || rulesSeen) return drop("`@import` must come before the other rules; it is skipped");
        if (hasBlock) return drop("`@import` takes no `{` block");
        std::string name;
        size_t first = 0;
        while (first < prelude.size() && prelude[first].is(Tok::Space)) ++first;
        if (first < prelude.size()) {
            const Node& n = prelude[first];
            std::vector<const Node*> inner = parts(n.children);
            if (n.is(Tok::String) || n.is(Tok::Url)) name = n.token.text;
            else if (n.function("url") && inner.size() == 1 && inner[0]->is(Tok::String)) name = inner[0]->token.text;
        }
        if (name.empty()) return drop("`@import` needs a sheet's name or url()");
        std::vector<Node> rest(prelude.begin() + (ptrdiff_t)first + 1, prelude.end());
        trimSpace(rest);
        bool supported = true;
        std::string condition;
        while (!rest.empty() && (rest[0].ident("layer") || rest[0].function("layer") || rest[0].function("supports"))) {
            if (rest[0].function("supports")) {
                Node test;
                test.token.type = Tok::OpenParen;
                test.children = rest[0].children;
                SupportsReader reader{sheet.text};
                bool result = false;
                supported = reader.inParens(test, result) && result;
                condition = "(" + written(rest[0].children) + ")";
            } else {
                report(Diagnostic::Warning, rest[0].token.begin, "layers are not supported; the sheet applies in order");
            }
            rest.erase(rest.begin());
            trimSpace(rest);
        }
        std::shared_ptr<const MediaExpr> media;
        if (!rest.empty()) {
            MediaReader reader;
            bool ok;
            media = std::make_shared<const MediaExpr>(reader.list(rest, ok));
            if (!ok) report(Diagnostic::Error, rest.front().token.begin, "`" + quote(written(rest)) + "` is not a valid media query");
            for (const std::string& w : reader.warnings) report(Diagnostic::Warning, rest.front().token.begin, w);
        }
        std::string text, resolved;
        if (!imports || !imports(name, sheet.file, text, resolved)) return drop("cannot find `" + name + "` to import");
        if (std::find(stack.begin(), stack.end(), resolved) != stack.end()) {
            return drop("`@import \"" + name + "\"` imports a sheet that imports this one; it is skipped");
        }
        if (stack.size() > 16) return drop("`@import \"" + name + "\"` nests imports too deeply");
        auto child = std::make_shared<Sheet>();
        child->file = resolved;
        child->text = std::string(withoutBom(text));
        child->origin = sheet.origin;
        stack.push_back(resolved);
        Parser(*child, imports, diagnostics, stack).run();
        stack.pop_back();
        for (Rule& rule : child->rules) {
            if (media) rule.media.insert(rule.media.begin(), media);
            rule.supported = rule.supported && supported;
            sheet.rules.push_back(std::move(rule));
        }
        child->rules.clear();
        sheet.faces.insert(sheet.faces.end(), child->faces.begin(), child->faces.end());
        sheet.imports.push_back({begin, finish, child, media ? written(rest) : std::string(), condition});
    }

    std::string resolve(const std::string& url, uint32_t at) {
        if (hasScheme(url)) {
            if (startsWithLower(url, "file:///")) return url.substr(8);
            report(Diagnostic::Warning, at, "`" + url + "` is not fetched: fonts come from files beside the sheet");
            return {};
        }
        bool absolute = (!url.empty() && (url[0] == '/' || url[0] == '\\')) ||
                        (url.size() > 2 && url[1] == ':' && (url[2] == '/' || url[2] == '\\'));
        if (absolute) return url;
        size_t slash = sheet.file.find_last_of("/\\");
        return slash == std::string::npos ? url : sheet.file.substr(0, slash + 1) + url;
    }

    void fontFace(const Token& at, size_t begin, size_t end) {
        FontFace face;
        face.file = sheet.file;
        int column;
        lines.at(sheet.text, at.begin, face.line, column);
        static const char* const ignored[] = {"font-display", "font-stretch", "font-width", "font-feature-settings",
                                              "font-variation-settings", "ascent-override", "descent-override",
                                              "line-gap-override", "size-adjust", "font-named-instance",
                                              "font-language-override"};
        eachDeclaration(begin, end, [&](size_t i, size_t j) {
            std::vector<Node> value;
            bool important;
            if (!split(i, j, value, important)) return;
            std::string name = toLower(tokens[i].text);
            std::vector<const Node*> p = parts(value);
            uint32_t where = tokens[i].begin;
            auto bad = [&] { report(Diagnostic::Error, where, "`" + quote(written(value)) + "` is not a valid `" + name + "`"); };
            if (name == "font-family") {
                Computed scratch;
                if (!computeLonghand(Prop::FontFamily, value, Env(), scratch) || scratch.s.fontFamily.size() != 1) return bad();
                face.family = scratch.s.fontFamily[0];
            } else if (name == "src") {
                for (const auto& item : splitCommas(value)) {
                    std::vector<const Node*> q = parts(item);
                    if (q.empty()) return bad();
                    const Node& n = *q[0];
                    std::vector<const Node*> inner = parts(n.children);
                    std::string source;
                    if (n.is(Tok::Url)) {
                        source = resolve(n.token.text, where);
                    } else if (n.function("url") && inner.size() == 1 && inner[0]->is(Tok::String)) {
                        source = resolve(inner[0]->token.text, where);
                    } else if (n.function("local")) {
                        Computed scratch;
                        if (!computeLonghand(Prop::FontFamily, n.children, Env(), scratch) || scratch.s.fontFamily.size() != 1) {
                            return bad();
                        }
                        source = "local:" + scratch.s.fontFamily[0];
                    } else {
                        return bad();
                    }
                    if (!source.empty()) face.sources.push_back(source);
                }
            } else if (name == "font-weight") {
                int weights[2];
                if (p.empty() || p.size() > 2) return bad();
                for (size_t w = 0; w < p.size(); ++w) {
                    double number;
                    if (p[w]->ident("normal")) weights[w] = 400;
                    else if (p[w]->ident("bold")) weights[w] = 700;
                    else if (readNumber(*p[w], number) && number >= 1 && number <= 1000) weights[w] = (int)std::lround(number);
                    else return bad();
                }
                face.weightMin = std::min(weights[0], weights[p.size() - 1]);
                face.weightMax = std::max(weights[0], weights[p.size() - 1]);
            } else if (name == "font-style") {
                if (p.size() == 1 && p[0]->ident("normal")) face.style = FontStyle::Normal;
                else if (p.size() == 1 && p[0]->ident("italic")) face.style = FontStyle::Italic;
                else if (!p.empty() && p[0]->ident("oblique") && p.size() <= 3) face.style = FontStyle::Oblique;
                else return bad();
            } else if (name == "unicode-range") {
                report(Diagnostic::Warning, where, "`unicode-range` is not supported; the face covers every character");
            } else if (!listed(name, ignored)) {
                report(Diagnostic::Warning, where, "unknown descriptor `" + name + "` in `@font-face`");
            }
        });
        if (face.family.empty() || face.sources.empty()) {
            report(Diagnostic::Warning, at.begin, "`@font-face` needs a `font-family` and a `src` it can read; it is skipped");
            return;
        }
        sheet.faces.push_back(std::move(face));
    }
};

}  // namespace

std::shared_ptr<const Sheet> parse(std::string_view text, std::string_view file, Origin origin,
                                   const ImportSource& imports, std::vector<Diagnostic>& diagnostics) {
    auto sheet = std::make_shared<Sheet>();
    sheet->file = std::string(file);
    sheet->text = std::string(withoutBom(text));
    sheet->origin = origin;
    std::vector<std::string> stack{sheet->file};
    Parser(*sheet, imports, diagnostics, stack).run();
    return sheet;
}

const std::vector<FontFace>& fontFaces(const Sheet& sheet) { return sheet.faces; }

std::string serialize(const Sheet& sheet) {
    std::string out;
    size_t at = 0;
    for (const Sheet::Import& import : sheet.imports) {
        out.append(sheet.text, at, import.begin - at);
        at = import.end;
        if (!import.sheet) continue;
        if (!import.supports.empty()) out += "@supports " + import.supports + " {\n";
        if (!import.media.empty()) out += "@media " + import.media + " {\n";
        out += serialize(*import.sheet);
        if (!import.media.empty()) out += "\n}";
        if (!import.supports.empty()) out += "\n}";
    }
    out.append(sheet.text, at, std::string::npos);
    return out;
}

}
