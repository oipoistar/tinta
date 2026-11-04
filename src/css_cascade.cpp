// The cascade for Tinta's CSS (css.h): selectors matched against elements,
// contexts interned on what the loaded sheets can tell apart, and computed
// styles with inheritance, var() and the CSS-wide keywords.

#pragma optimize("s", on)

#include "css_internal.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace css {
namespace {

const uint32_t noContext = 0xFFFFFFFFu;

int lowestBit(uint64_t bits) {
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward64(&index, bits);
    return (int)index;
#else
    return __builtin_ctzll(bits);
#endif
}
bool test(const uint64_t* bits, uint32_t i) { return (bits[i >> 6] >> (i & 63)) & 1; }
void set(uint64_t* bits, uint32_t i) { bits[i >> 6] |= 1ull << (i & 63); }

bool sameText(std::string_view a, std::string_view b, bool caseless) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (caseless) {
            if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        }
        if (x != y) return false;
    }
    return true;
}

bool attributeMatches(const AttrTest& t, const Element& e, std::string& joined) {
    std::string_view value;
    bool present = false;
    if (t.name == "id") {
        present = !e.id.empty();
        value = e.id;
    } else if (t.name == "class") {
        present = !e.classes.empty();
        joined.clear();
        for (std::string_view c : e.classes) {
            if (!joined.empty()) joined += ' ';
            joined += c;
        }
        value = joined;
    } else {
        for (const auto& a : e.attributes) {
            if (sameText(a.first, t.name, true)) {
                present = true;
                value = a.second;
                break;
            }
        }
    }
    if (!present) return false;
    std::string_view want = t.value;
    bool caseless = t.caseless;
    switch (t.op) {
        case 0: return true;
        case '=': return sameText(value, want, caseless);
        case '|': return sameText(value, want, caseless) ||
                         (value.size() > want.size() && value[want.size()] == '-' &&
                          sameText(value.substr(0, want.size()), want, caseless));
        case '^': return !want.empty() && value.size() >= want.size() && sameText(value.substr(0, want.size()), want, caseless);
        case '$': return !want.empty() && value.size() >= want.size() &&
                         sameText(value.substr(value.size() - want.size()), want, caseless);
        case '*':
            if (want.empty()) return false;
            for (size_t i = 0; i + want.size() <= value.size(); ++i) {
                if (sameText(value.substr(i, want.size()), want, caseless)) return true;
            }
            return false;
        case '~': {
            if (want.empty() || want.find_first_of(" \t\n") != std::string_view::npos) return false;
            size_t i = 0;
            while (i < value.size()) {
                size_t j = value.find_first_of(" \t\n\r\f", i);
                if (j == std::string_view::npos) j = value.size();
                if (sameText(value.substr(i, j - i), want, caseless)) return true;
                i = j + 1;
            }
            return false;
        }
        default: return false;
    }
}

bool nthMatches(int a, int b, int index) {
    if (index < 1) return false;
    if (a == 0) return index == b;
    int diff = index - b;
    return diff % a == 0 && diff / a >= 0;
}

bool langMatches(const std::vector<std::string>& ranges, std::string_view lang) {
    for (const std::string& range : ranges) {
        if (range == "*" ? !lang.empty()
                         : sameText(lang, range, true) ||
                               (lang.size() > range.size() && lang[range.size()] == '-' &&
                                sameText(lang.substr(0, range.size()), range, true))) {
            return true;
        }
    }
    return false;
}

uint64_t fingerprint(const Element& e) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::string_view s) {
        for (char c : s) h = (h ^ (unsigned char)c) * 1099511628211ull;
        h = (h ^ 0xFF) * 1099511628211ull;
    };
    mix(e.tag);
    mix(e.id);
    for (std::string_view c : e.classes) mix(c);
    for (const auto& a : e.attributes) {
        mix(a.first);
        mix(a.second);
    }
    for (int v : {e.index, e.count, e.typeIndex, e.typeCount, (int)e.checked, (int)e.empty}) {
        h = (h ^ (uint64_t)(uint32_t)v) * 1099511628211ull;
    }
    return h;
}

// One compound of a compiled selector, and how it joins the next leftwards
struct Position {
    const Compound* compound = nullptr;
    Combinator combinator = Combinator::Descendant;
    bool last = true;     // the leftmost compound
    uint32_t nested = 0;  // where its :is() and :not() lists start in Impl::nested
};

struct Subject {
    uint32_t rule = 0, specificity = 0;
    int8_t pseudo = -1;
};

// Positions by what an element needs to match them: an id, a class or a tag
struct Buckets {
    std::map<std::string, std::vector<uint32_t>, std::less<>> byId, byClass, byTag;
    std::vector<uint32_t> any;

    void add(const Compound& c, uint32_t position) {
        if (!c.id.empty()) byId[c.id].push_back(position);
        else if (!c.classes.empty()) byClass[c.classes[0]].push_back(position);
        else if (!c.tag.empty()) byTag[c.tag].push_back(position);
        else any.push_back(position);
    }
    static void append(const std::map<std::string, std::vector<uint32_t>, std::less<>>& map, std::string_view key,
                       std::vector<uint32_t>& out) {
        auto it = map.find(key);
        if (it != map.end()) out.insert(out.end(), it->second.begin(), it->second.end());
    }
    void candidates(const Element& e, std::vector<uint32_t>& out) const {
        out = any;
        if (!e.id.empty()) append(byId, e.id, out);
        for (std::string_view c : e.classes) append(byClass, c, out);
        append(byTag, e.tag, out);
    }
};

struct Ctx {
    uint32_t parent = noContext;
    uint32_t bits = 0;  // its matches in Impl::pool, then the matches of it and its ancestors
    std::string lang;
    std::unique_ptr<Computed> style;
    std::unique_ptr<Computed> pseudo[4];
    uint8_t pseudoDone = 0;
};

struct Run {
    const Element* base = nullptr;
    size_t count = 0;
    uint64_t fingerprint = 0;
    std::vector<uint64_t> last, any;  // the sibling bits of the last sibling, and of any
};

struct Win {
    uint64_t priority = 0;
    const Declaration* decl = nullptr;
    uint32_t rule = 0;
};

using Longhands = std::vector<std::pair<Prop, std::vector<Node>>>;

// --- For explain() ---

// Source text on one line: each run of white space one space
std::string oneLine(std::string_view text) {
    std::string out;
    for (char c : text) {
        bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        if (!space) out += c;
        else if (!out.empty() && out.back() != ' ') out += ' ';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// A declaration as written, from its name at `at` to its `;` or its block's end
std::string declarationAt(std::string_view text, size_t at) {
    size_t i = at;
    int depth = 0;
    while (i < text.size()) {
        char c = text[i];
        if (c == '"' || c == '\'') {
            for (++i; i < text.size() && text[i] != c && text[i] != '\n'; ++i) {
                if (text[i] == '\\') ++i;
            }
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            size_t close = text.find("*/", i + 2);
            i = close == std::string_view::npos ? text.size() : close + 1;
        } else if (c == '(' || c == '[') {
            ++depth;
        } else if ((c == ')' || c == ']') && depth > 0) {
            --depth;
        } else if (depth == 0 && (c == ';' || c == '}' || c == '{')) {
            break;
        }
        ++i;
    }
    return oneLine(text.substr(at, std::min(i, text.size()) - at));
}

// The custom properties a value reads with var()
void varNames(const std::vector<Node>& nodes, std::vector<std::string>& out) {
    for (const Node& n : nodes) {
        if (n.function("var")) {
            for (const Node& arg : n.children) {
                if (arg.is(Tok::Space)) continue;
                if (arg.is(Tok::Ident) && arg.token.text.compare(0, 2, "--") == 0) out.push_back(arg.token.text);
                break;
            }
        }
        varNames(n.children, out);
    }
}

}  // namespace

struct Engine::Impl {
    std::vector<std::shared_ptr<const Sheet>> sheets;
    Media media;
    uint64_t generation = 1;

    struct RuleRef {
        const Rule* rule = nullptr;
        uint8_t origin = 0;
        uint32_t sequence = 0;
        bool active = true;
    };
    std::vector<RuleRef> rules;
    std::vector<Position> positions;
    std::vector<uint32_t> nested;
    std::vector<int32_t> subjectOf;
    std::vector<Subject> subjects;
    Buckets all, siblings;
    size_t words = 1;
    std::vector<uint64_t> keyMask, descendantMask, subjectMask;
    bool hasSiblings = false;

    std::vector<Ctx> contexts;
    std::vector<uint64_t> pool;
    std::unordered_map<std::string, uint32_t> interned;
    std::unordered_map<uint32_t, Run> runs;
    std::vector<Diagnostic> diagnostics;
    std::unordered_set<const Declaration*> reported;
    std::unordered_map<std::string, std::vector<Node>> customNodes;

    // Scratch, kept to save allocations
    std::vector<uint64_t> done, value, matched, siblingBits;
    std::vector<uint32_t> candidates;
    std::string key, joined;
    std::vector<std::pair<uint32_t, uint32_t>> matches;
    std::vector<std::tuple<const Declaration*, bool, Longhands>> expansions;

    struct Probe {
        const Element* element;
        const Ctx* parent;          // null at the root
        const uint64_t* previous;   // the previous sibling's sibling bits
        const uint64_t* earlier;    // any earlier sibling's
        std::string_view lang;
    };

    // --- Selectors ---

    uint32_t compile(const Complex& c) {
        std::vector<std::vector<uint32_t>> lists(c.compounds.size());
        for (size_t k = 0; k < c.compounds.size(); ++k) {
            for (const PseudoClass& pc : c.compounds[k].pseudos) {
                for (const Complex& x : pc.list) lists[k].push_back(compile(x));
            }
        }
        uint32_t base = (uint32_t)positions.size();
        for (size_t k = 0; k < c.compounds.size(); ++k) {
            Position p;
            p.compound = &c.compounds[k];
            p.last = k + 1 == c.compounds.size();
            if (!p.last) p.combinator = c.combinators[k];
            p.nested = (uint32_t)nested.size();
            nested.insert(nested.end(), lists[k].begin(), lists[k].end());
            positions.push_back(p);
        }
        return base;
    }

    void compileAll() {
        uint32_t sequence = 0;
        for (const auto& sheet : sheets) {
            for (const Rule& rule : sheet->rules) {
                RuleRef ref;
                ref.rule = &rule;
                ref.origin = (uint8_t)sheet->origin;
                ref.sequence = sequence;
                sequence += (uint32_t)rule.declarations.size();
                uint32_t index = (uint32_t)rules.size();
                rules.push_back(ref);
                for (const Complex& c : rule.selectors) {
                    uint32_t p = compile(c);
                    subjectOf.resize(positions.size(), -1);
                    subjectOf[p] = (int32_t)subjects.size();
                    subjects.push_back({index, c.specificity, c.pseudo});
                }
            }
        }
        subjectOf.resize(positions.size(), -1);
        words = std::max<size_t>(1, (positions.size() + 63) / 64);
        keyMask.assign(words, 0);
        descendantMask.assign(words, 0);
        subjectMask.assign(words, 0);
        for (uint32_t p = 0; p < positions.size(); ++p) {
            const Position& pos = positions[p];
            all.add(*pos.compound, p);
            if (subjectOf[p] >= 0) {
                set(subjectMask.data(), p);
                set(keyMask.data(), p);
            }
            if (pos.last) continue;
            if (pos.combinator == Combinator::Child || pos.combinator == Combinator::Descendant) {
                set(keyMask.data(), p + 1);
                if (pos.combinator == Combinator::Descendant) set(descendantMask.data(), p + 1);
            } else {
                siblings.add(*positions[p + 1].compound, p + 1);
                hasSiblings = true;
            }
        }
        done.assign(words, 0);
        value.assign(words, 0);
        matched.assign(words, 0);
        siblingBits.assign(words, 0);
    }

    void activate() {
        for (RuleRef& ref : rules) {
            ref.active = ref.rule->supported;
            for (const auto& m : ref.rule->media) ref.active = ref.active && matchMedia(*m, media);
        }
    }

    bool compoundMatches(const Position& pos, const Probe& s) {
        const Compound& c = *pos.compound;
        const Element& e = *s.element;
        if (!c.tag.empty() && !sameText(c.tag, e.tag, true)) return false;
        if (!c.id.empty() && c.id != e.id) return false;
        for (const std::string& name : c.classes) {
            if (std::find(e.classes.begin(), e.classes.end(), std::string_view(name)) == e.classes.end()) return false;
        }
        for (const AttrTest& t : c.attrs) {
            if (!attributeMatches(t, e, joined)) return false;
        }
        uint32_t n = pos.nested;
        for (const PseudoClass& pc : c.pseudos) {
            switch (pc.kind) {
                case PseudoClass::Root:
                    if (s.parent) return false;
                    break;
                case PseudoClass::Empty:
                    if (!e.empty) return false;
                    break;
                case PseudoClass::Checked:
                    if (!e.checked) return false;
                    break;
                case PseudoClass::Nth: {
                    int index = pc.ofType ? (pc.fromEnd ? e.typeCount - e.typeIndex + 1 : e.typeIndex)
                                          : (pc.fromEnd ? e.count - e.index + 1 : e.index);
                    if (!nthMatches(pc.a, pc.b, index)) return false;
                    break;
                }
                case PseudoClass::Lang:
                    if (!langMatches(pc.langs, s.lang)) return false;
                    break;
                case PseudoClass::Is:
                case PseudoClass::Not: {
                    bool any = false;
                    for (size_t k = 0; k < pc.list.size() && !any; ++k) any = evaluate(nested[n + k], s);
                    n += (uint32_t)pc.list.size();
                    if (any != (pc.kind == PseudoClass::Is)) return false;
                    break;
                }
            }
        }
        return true;
    }

    bool evaluate(uint32_t p, const Probe& s) {
        if (test(done.data(), p)) return test(value.data(), p);
        set(done.data(), p);
        const Position& pos = positions[p];
        bool ok = compoundMatches(pos, s);
        if (ok && !pos.last) {
            uint32_t q = p + 1;
            switch (pos.combinator) {
                case Combinator::Child: ok = s.parent && test(pool.data() + s.parent->bits, q); break;
                case Combinator::Descendant: ok = s.parent && test(pool.data() + s.parent->bits + words, q); break;
                case Combinator::Next: ok = s.previous && test(s.previous, q); break;
                case Combinator::Subsequent: ok = s.earlier && test(s.earlier, q); break;
            }
        }
        if (ok) set(value.data(), p);
        return ok;
    }

    // The positions of `from` an element matches, into `out`
    void matchInto(const Buckets& from, const Probe& s, std::vector<uint64_t>& out) {
        std::fill(done.begin(), done.end(), 0);
        std::fill(value.begin(), value.end(), 0);
        std::fill(out.begin(), out.end(), 0);
        from.candidates(*s.element, candidates);
        for (uint32_t p : candidates) {
            if (evaluate(p, s)) set(out.data(), p);
        }
    }

    std::string_view langOf(const Element& e, uint32_t parent) const {
        for (const auto& a : e.attributes) {
            if (sameText(a.first, "lang", true)) return a.second;
        }
        return parent == noContext ? std::string_view() : std::string_view(contexts[parent].lang);
    }

    // The sibling bits of the elements before one, carried on from the last call
    Run& siblingRun(uint32_t parent, const Element* previous, size_t count) {
        Run& run = runs[parent];
        if (run.last.size() != words || run.base != previous || run.count > count ||
            (run.count > 0 && fingerprint(previous[run.count - 1]) != run.fingerprint)) {
            run.base = previous;
            run.count = 0;
            run.last.assign(words, 0);
            run.any.assign(words, 0);
        }
        std::vector<uint64_t>& bits = siblingBits;
        while (run.count < count) {
            const Element& e = previous[run.count];
            Probe s{&e, &contexts[parent], run.count ? run.last.data() : nullptr, run.count ? run.any.data() : nullptr,
                    langOf(e, parent)};
            matchInto(siblings, s, bits);
            run.last = bits;
            for (size_t w = 0; w < words; ++w) run.any[w] |= bits[w];
            run.fingerprint = fingerprint(e);
            ++run.count;
        }
        return run;
    }

    uint32_t intern(uint32_t parent, const Element& e, const Element* previous, size_t count) {
        Run* run = hasSiblings && previous && count && parent != noContext ? &siblingRun(parent, previous, count) : nullptr;
        std::string_view lang = langOf(e, parent);
        Probe s{&e, parent == noContext ? nullptr : &contexts[parent], run ? run->last.data() : nullptr,
                run ? run->any.data() : nullptr, lang};
        matchInto(all, s, matched);
        for (size_t w = 0; w < words; ++w) matched[w] &= keyMask[w];
        key.assign((const char*)&parent, sizeof parent);
        key.append((const char*)matched.data(), words * sizeof(uint64_t));
        key.append(lang.data(), lang.size());
        auto found = interned.find(key);
        if (found != interned.end()) return found->second;
        uint32_t id = (uint32_t)contexts.size();
        Ctx c;
        c.parent = parent;
        c.bits = (uint32_t)pool.size();
        c.lang = std::string(lang);
        pool.insert(pool.end(), matched.begin(), matched.end());
        for (size_t w = 0; w < words; ++w) {
            uint64_t above = parent == noContext ? 0 : pool[contexts[parent].bits + words + w];
            pool.push_back(above | (matched[w] & descendantMask[w]));
        }
        contexts.push_back(std::move(c));
        interned.emplace(key, id);
        return id;
    }

    uint32_t root() {
        if (contexts.empty()) {
            Element html;
            html.tag = "html";
            html.index = html.count = html.typeIndex = html.typeCount = 1;
            intern(noContext, html, nullptr, 0);
        }
        return 0;
    }

    // --- Values ---

    using Table = std::vector<std::pair<std::string, std::string>>;

    static const std::string* findIn(const Table* table, const std::string& name) {
        if (!table) return nullptr;
        auto it = std::lower_bound(table->begin(), table->end(), name,
                                   [](const std::pair<std::string, std::string>& e, const std::string& k) { return e.first < k; });
        return it != table->end() && it->first == name ? &it->second : nullptr;
    }

    const std::vector<Node>& nodesOf(const std::string& text) {
        auto it = customNodes.find(text);
        if (it == customNodes.end()) {
            std::vector<Node> nodes = parseNodes(text);
            trimSpace(nodes);
            it = customNodes.emplace(text, std::move(nodes)).first;
        }
        return it->second;
    }

    template <class Lookup>
    bool substitute(const std::vector<Node>& in, Lookup& lookup, std::vector<Node>& out, int depth) {
        if (depth > 32) return false;
        for (const Node& n : in) {
            if (n.function("var")) {
                const std::vector<Node>& args = n.children;
                size_t k = 0;
                while (k < args.size() && args[k].is(Tok::Space)) ++k;
                if (k >= args.size() || !args[k].is(Tok::Ident) || args[k].token.text.compare(0, 2, "--") != 0) return false;
                const std::string& name = args[k++].token.text;
                while (k < args.size() && args[k].is(Tok::Space)) ++k;
                bool fallback = k < args.size();
                if (fallback && !args[k].is(Tok::Comma)) return false;
                if (const std::vector<Node>* found = lookup(name)) {
                    out.insert(out.end(), found->begin(), found->end());
                } else if (!fallback) {
                    return false;
                } else {
                    std::vector<Node> rest(args.begin() + (ptrdiff_t)k + 1, args.end());
                    trimSpace(rest);
                    if (!substitute(rest, lookup, out, depth + 1)) return false;
                }
            } else if (n.children.empty()) {
                out.push_back(n);
            } else {
                Node copy;
                copy.token = n.token;
                if (!substitute(n.children, lookup, copy.children, depth + 1)) return false;
                out.push_back(std::move(copy));
            }
        }
        return true;
    }

    struct Custom {
        const Declaration* decl = nullptr;
        uint32_t rule = 0;
        int state = 0;  // 0 waiting, 1 resolving, 2 done
        bool valid = false, cycle = false;
        std::string value;
    };

    bool resolveCustom(size_t i, std::vector<Custom>& own, const Table* inherited, std::vector<size_t>& stack) {
        if (own[i].state == 2) return own[i].valid;
        if (own[i].state == 1) {
            for (size_t k = stack.size(); k-- > 0;) {
                own[stack[k]].cycle = true;
                if (stack[k] == i) break;
            }
            return false;
        }
        own[i].state = 1;
        stack.push_back(i);
        const Declaration& d = *own[i].decl;
        bool ok = true;
        std::string result;
        if (cssWideKeyword(d.value)) {
            const std::string* up = findIn(inherited, d.name);
            ok = up && !d.value[0].ident("initial");
            if (ok) result = *up;
        } else if (!d.pending) {
            result = d.text;
        } else {
            auto lookup = [&](const std::string& name) -> const std::vector<Node>* {
                for (size_t j = 0; j < own.size(); ++j) {
                    if (own[j].decl->name == name) {
                        return resolveCustom(j, own, inherited, stack) ? &nodesOf(own[j].value) : nullptr;
                    }
                }
                const std::string* up = findIn(inherited, name);
                return up ? &nodesOf(*up) : nullptr;
            };
            std::vector<Node> out;
            ok = substitute(d.value, lookup, out, 0);
            if (ok) serializeNodes(out, result);
        }
        stack.pop_back();
        own[i].state = 2;
        own[i].value = std::move(result);
        own[i].valid = ok && !own[i].cycle;
        return own[i].valid;
    }

    void report(const Win& w, const std::string& message) {
        if (!reported.insert(w.decl).second) return;
        Diagnostic d;
        d.level = Diagnostic::Warning;
        if (rules[w.rule].rule->file) d.file = *rules[w.rule].rule->file;
        d.line = w.decl->line;
        d.column = w.decl->column;
        d.message = message;
        diagnostics.push_back(std::move(d));
    }

    void customs(const std::vector<std::pair<const Declaration*, Win>>& wins, const Computed* parent, Computed& out) {
        if (wins.empty()) return;
        const Table* inherited = parent ? parent->s.customs.get() : nullptr;
        std::vector<Custom> own(wins.size());
        for (size_t i = 0; i < wins.size(); ++i) {
            own[i].decl = wins[i].first;
            own[i].rule = wins[i].second.rule;
        }
        std::vector<size_t> stack;
        for (size_t i = 0; i < own.size(); ++i) resolveCustom(i, own, inherited, stack);
        auto table = std::make_shared<Table>(inherited ? *inherited : Table());
        for (size_t i = 0; i < own.size(); ++i) {
            const std::string& name = own[i].decl->name;
            auto it = std::lower_bound(table->begin(), table->end(), name,
                                       [](const std::pair<std::string, std::string>& e, const std::string& k) { return e.first < k; });
            bool exists = it != table->end() && it->first == name;
            if (own[i].valid) {
                if (exists) it->second = own[i].value;
                else table->insert(it, {name, own[i].value});
                continue;
            }
            if (exists) table->erase(it);
            if (own[i].cycle) {
                report(wins[i].second, "`" + name + "` is part of a var() cycle, so it has no value");
            } else if (!cssWideKeyword(own[i].decl->value)) {
                report(wins[i].second, "`" + name + "` uses a var() with no value and no fallback, so it has no value");
            }
        }
        out.s.customs = std::move(table);
    }

    // A pending declaration's value for one longhand, var() substituted
    bool resolvePending(const Declaration& decl, Prop prop, const Computed& out, const Env& env, std::vector<Node>& result) {
        auto lookup = [&](const std::string& name) -> const std::vector<Node>* {
            const std::string* text = findIn(out.s.customs.get(), name);
            return text ? &nodesOf(*text) : nullptr;
        };
        if (decl.prop < (uint16_t)Prop::Count) {
            if (!substitute(decl.value, lookup, result, 0)) return false;
            trimSpace(result);
            return true;
        }
        auto cached = std::find_if(expansions.begin(), expansions.end(),
                                   [&](const auto& e) { return std::get<0>(e) == &decl; });
        if (cached == expansions.end()) {
            std::vector<Node> whole;
            Longhands parts;
            bool ok = substitute(decl.value, lookup, whole, 0);
            if (ok) {
                trimSpace(whole);
                if (cssWideKeyword(whole)) {
                    for (Prop p : longhands(decl.prop)) parts.emplace_back(p, whole);
                } else {
                    ok = expandShorthand(decl.prop, whole, env, parts);
                }
            }
            expansions.emplace_back(&decl, ok, std::move(parts));
            cached = expansions.end() - 1;
        }
        if (!std::get<1>(*cached)) return false;
        for (const auto& part : std::get<2>(*cached)) {
            if (part.first == prop) {
                result = part.second;
                return true;
            }
        }
        return false;
    }

    // inherit, initial, unset and revert
    void keyword(Prop prop, bool inherit, const Computed& base, Computed& out) {
        if (inherit) {
            copyLonghand(prop, base, out);
            return;
        }
        copyLonghand(prop, initialStyle(), out);
        if (prop >= Prop::BorderTopWidth && prop <= Prop::BorderLeftColor &&
            ((int)prop - (int)Prop::BorderTopWidth) % 3 == 2) {
            out.s.border[((int)prop - (int)Prop::BorderTopWidth) / 3].color = out.s.color;
        }
    }

    // The style of a context, or of one of its pseudo-elements; false when no rule matched
    bool cascade(uint32_t id, int8_t pseudo, const Computed* parent, bool whole, Computed& out) {
        const uint64_t* bits = pool.data() + contexts[id].bits;
        matches.clear();
        for (size_t w = 0; w < words; ++w) {
            uint64_t b = bits[w] & subjectMask[w];
            while (b) {
                uint32_t p = (uint32_t)(w * 64) + (uint32_t)lowestBit(b);
                b &= b - 1;
                const Subject& sub = subjects[(size_t)subjectOf[p]];
                if (sub.pseudo != pseudo || !rules[sub.rule].active) continue;
                auto it = std::find_if(matches.begin(), matches.end(), [&](const auto& m) { return m.first == sub.rule; });
                if (it == matches.end()) matches.emplace_back(sub.rule, sub.specificity);
                else it->second = std::max(it->second, sub.specificity);
            }
        }
        if (pseudo >= 0 && matches.empty()) return false;

        Win wins[(size_t)Prop::Count];
        std::vector<std::pair<const Declaration*, Win>> customWins;
        auto offer = [](Win& slot, const Win& win) {
            if (!slot.decl || win.priority > slot.priority) slot = win;
        };
        for (const auto& m : matches) {
            const RuleRef& ref = rules[m.first];
            const std::vector<Declaration>& decls = ref.rule->declarations;
            for (size_t k = 0; k < decls.size(); ++k) {
                const Declaration& d = decls[k];
                uint64_t band = d.important ? 5u - ref.origin : ref.origin;
                Win win{band << 61 | (uint64_t)m.second << 31 | (ref.sequence + k), &d, m.first};
                if (d.prop == PropCustom) {
                    auto it = std::find_if(customWins.begin(), customWins.end(),
                                           [&](const auto& c) { return c.first->name == d.name; });
                    if (it == customWins.end()) {
                        customWins.emplace_back(&d, win);
                    } else if (win.priority > it->second.priority) {
                        *it = {&d, win};
                    }
                } else if (d.prop < (uint16_t)Prop::Count) {
                    offer(wins[d.prop], win);
                } else {
                    for (Prop p : longhands(d.prop)) offer(wins[(size_t)p], win);
                }
            }
        }

        const Computed& base = parent ? *parent : initialStyle();
        if (whole) {
            out = base;
        } else {
            out = initialStyle();
            for (size_t p = 0; p < (size_t)Prop::Count; ++p) {
                if (inherited((Prop)p)) copyLonghand((Prop)p, base, out);
            }
            out.s.customs = base.s.customs;
        }
        out.s.lang = contexts[id].lang;
        out.s.dark = media.dark;
        customs(customWins, parent, out);

        Env env;
        env.parent = parent;
        env.rootFontSize = id == 0 && pseudo < 0 ? 16 : contexts[0].style->s.fontSize;
        env.width = media.width;
        env.dark = media.dark;
        expansions.clear();
        static const std::vector<Prop> order = [] {
            std::vector<Prop> o{Prop::ColorScheme, Prop::FontSize, Prop::Color};
            for (size_t p = 0; p < (size_t)Prop::Count; ++p) {
                if (std::find(o.begin(), o.end(), (Prop)p) == o.end()) o.push_back((Prop)p);
            }
            return o;
        }();
        for (Prop p : order) {
            const Win& w = wins[(size_t)p];
            if (w.decl) {
                out.s.declared.set((size_t)p);
                const std::vector<Node>* v = &w.decl->value;
                std::vector<Node> substituted;
                bool ok = true;
                if (w.decl->pending) {
                    ok = resolvePending(*w.decl, p, out, env, substituted);
                    v = &substituted;
                }
                if (ok && cssWideKeyword(*v)) {
                    const Node& k = (*v)[0];
                    keyword(p, k.ident("inherit") || (!k.ident("initial") && inherited(p)), base, out);
                } else if (!ok || !computeLonghand(p, *v, env, out)) {
                    keyword(p, inherited(p), base, out);
                    std::string text;
                    serializeNodes(w.decl->value, text);
                    report(w, "`" + std::string(propertyName(w.decl->prop)) + ": " + text +
                                  "` is invalid once var() is substituted, so it is unset");
                }
            }
            if (p == Prop::Color && !whole) {
                for (int side = 0; side < 4; ++side) out.s.border[side].color = out.s.color;
            }
        }
        if (out.s.lineHeightFactor > 0) out.s.lineHeight = out.s.lineHeightFactor * out.s.fontSize;
        for (Border& b : out.s.border) {
            if (b.style == LineStyle::None) b.width = 0;
        }
        buildFeatures(out);
        return true;
    }

    // The ancestors without a style first, from the top, so deep pages never recurse
    const Computed& computedOf(uint32_t id) {
        if (!contexts[id].style) {
            std::vector<uint32_t> chain;
            for (uint32_t c = id; c != noContext && !contexts[c].style; c = contexts[c].parent) chain.push_back(c);
            for (size_t k = chain.size(); k-- > 0;) {
                uint32_t c = chain[k], parent = contexts[c].parent;
                auto style = std::make_unique<Computed>();
                cascade(c, -1, parent == noContext ? nullptr : contexts[parent].style.get(), false, *style);
                contexts[c].style = std::move(style);
            }
        }
        return *contexts[id].style;
    }

    // ::selection inherits from the parent's, so a :root rule reaches the whole page
    void selectionOf(uint32_t id) {
        const int w = (int)Pseudo::Selection;
        const Computed& element = computedOf(id);
        uint32_t parent = contexts[id].parent;
        const Computed* above = parent != noContext ? contexts[parent].pseudo[w].get() : nullptr;
        auto own = std::make_unique<Computed>();
        std::unique_ptr<Computed> style;
        if (cascade(id, (int8_t)w, above ? above : &element, above != nullptr, *own)) style = std::move(own);
        else if (above) style = std::make_unique<Computed>(*above);
        contexts[id].pseudo[w] = std::move(style);
        contexts[id].pseudoDone |= (uint8_t)(1 << w);
    }

    const ComputedStyle* pseudoOf(uint32_t id, Pseudo which) {
        int w = (int)which;
        if (which == Pseudo::Selection) {
            std::vector<uint32_t> chain;
            for (uint32_t c = id; c != noContext && !((contexts[c].pseudoDone >> w) & 1); c = contexts[c].parent) {
                chain.push_back(c);
            }
            for (size_t k = chain.size(); k-- > 0;) selectionOf(chain[k]);
        } else if (!((contexts[id].pseudoDone >> w) & 1)) {
            const Computed& element = computedOf(id);
            auto own = std::make_unique<Computed>();
            if (cascade(id, (int8_t)w, &element, false, *own) && (which == Pseudo::Marker || !own->s.content.empty())) {
                contexts[id].pseudo[w] = std::move(own);
            }
            contexts[id].pseudoDone |= (uint8_t)(1 << w);
        }
        return contexts[id].pseudo[w] ? &contexts[id].pseudo[w]->s : nullptr;
    }

    // --- Explaining, for the inspector ---

    struct Candidate {
        uint64_t priority = 0;
        const Declaration* decl = nullptr;
        uint32_t rule = 0, subject = 0;  // the subject: the rule's selector that matched, the most specific
    };

    // The declarations whose rules match a context, or a pseudo-element of
    // it, by property, the cascade's winner first
    struct Reach {
        std::vector<Candidate> props[(size_t)Prop::Count];
        std::map<std::string, std::vector<Candidate>> customs;
    };

    void reach(uint32_t id, int8_t pseudo, Reach& out) {
        const uint64_t* bits = pool.data() + contexts[id].bits;
        std::vector<std::pair<uint32_t, uint32_t>> best;  // a rule, and its subject of the highest specificity
        for (size_t w = 0; w < words; ++w) {
            uint64_t b = bits[w] & subjectMask[w];
            while (b) {
                uint32_t p = (uint32_t)(w * 64) + (uint32_t)lowestBit(b);
                b &= b - 1;
                uint32_t si = (uint32_t)subjectOf[p];
                const Subject& sub = subjects[si];
                if (sub.pseudo != pseudo || !rules[sub.rule].active) continue;
                auto it = std::find_if(best.begin(), best.end(), [&](const auto& m) { return m.first == sub.rule; });
                if (it == best.end()) best.emplace_back(sub.rule, si);
                else if (sub.specificity > subjects[it->second].specificity) it->second = si;
            }
        }
        for (const auto& m : best) {
            const RuleRef& ref = rules[m.first];
            const std::vector<Declaration>& decls = ref.rule->declarations;
            for (size_t k = 0; k < decls.size(); ++k) {
                const Declaration& d = decls[k];
                uint64_t band = d.important ? 5u - ref.origin : ref.origin;  // as cascade() ranks them
                Candidate c{band << 61 | (uint64_t)subjects[m.second].specificity << 31 | (ref.sequence + k), &d, m.first,
                            m.second};
                if (d.prop == PropCustom) {
                    out.customs[d.name].push_back(c);
                } else if (d.prop < (uint16_t)Prop::Count) {
                    out.props[d.prop].push_back(c);
                } else {
                    for (Prop p : longhands(d.prop)) out.props[(size_t)p].push_back(c);
                }
            }
        }
        auto first = [](const Candidate& a, const Candidate& b) { return a.priority > b.priority; };
        for (auto& list : out.props) std::sort(list.begin(), list.end(), first);
        for (auto& entry : out.customs) std::sort(entry.second.begin(), entry.second.end(), first);
    }

    std::vector<Applied> explain(uint32_t id, int8_t pseudo) {
        // Each rule's sheet text, through the file its rules point to
        std::unordered_map<const std::string*, const std::string*> texts;
        std::vector<const Sheet*> todo;
        for (const auto& s : sheets) todo.push_back(s.get());
        while (!todo.empty()) {
            const Sheet* s = todo.back();
            todo.pop_back();
            if (s->name) texts[s->name.get()] = &s->text;
            for (const Sheet::Import& i : s->imports) {
                if (i.sheet) todo.push_back(i.sheet.get());
            }
        }
        std::vector<std::string> reads;  // custom properties the listed declarations read
        auto describe = [&](const Candidate& c, const std::string& property, Applied::State state, int ancestor,
                            int8_t which) {
            Applied a;
            const RuleRef& ref = rules[c.rule];
            const Declaration& d = *c.decl;
            a.state = state;
            a.property = property;
            if (d.prop == PropCustom) a.value = d.text;
            else serializeNodes(d.value, a.value);
            a.specificity = subjects[c.subject].specificity;
            a.origin = (Origin)ref.origin;
            a.important = d.important;
            a.ancestor = ancestor;
            a.pseudo = which;
            if (ref.rule->file) a.file = *ref.rule->file;
            a.line = d.line;
            a.column = d.column;
            auto text = texts.find(ref.rule->file.get());
            if (text != texts.end()) {
                const std::string& source = *text->second;
                uint32_t first = c.subject;
                while (first > 0 && subjects[first - 1].rule == c.rule) --first;
                const Complex& selector = ref.rule->selectors[c.subject - first];
                if (selector.begin <= selector.end && selector.end <= source.size()) {
                    a.selector = oneLine(std::string_view(source).substr(selector.begin, selector.end - selector.begin));
                }
                if (d.begin < source.size()) a.declaration = declarationAt(source, d.begin);
            }
            if (state != Applied::Overridden) varNames(d.value, reads);
            return a;
        };

        std::vector<Applied> out;
        Reach own;
        reach(id, pseudo, own);
        for (size_t p = 0; p < (size_t)Prop::Count; ++p) {
            for (size_t k = 0; k < own.props[p].size(); ++k) {
                out.push_back(describe(own.props[p][k], propertyName((int)p), k ? Applied::Overridden : Applied::Winning, 0, pseudo));
            }
        }
        for (const auto& entry : own.customs) {
            for (size_t k = 0; k < entry.second.size(); ++k) {
                out.push_back(describe(entry.second[k], entry.first, k ? Applied::Overridden : Applied::Winning, 0, pseudo));
            }
        }

        // Where inherited values come from: the element itself first for its
        // ::before, ::after and ::marker, then its ancestors; ::selection
        // inherits from the ancestors' ::selection
        struct Link {
            uint32_t id;
            int depth;
            int8_t pseudo;
        };
        const bool selection = pseudo == (int8_t)Pseudo::Selection;
        std::vector<Link> chain, customChain;
        if (pseudo >= 0) customChain.push_back({id, 0, -1});
        int depth = 0;
        for (uint32_t c = contexts[id].parent; c != noContext; c = contexts[c].parent) {
            customChain.push_back({c, ++depth, -1});
            if (selection) chain.push_back({c, depth, pseudo});
        }
        if (!selection) chain = customChain;
        std::map<std::pair<uint32_t, int8_t>, std::unique_ptr<Reach>> reaches;
        auto reachOf = [&](const Link& link) -> const Reach& {
            std::unique_ptr<Reach>& r = reaches[{link.id, link.pseudo}];
            if (!r) {
                r = std::make_unique<Reach>();
                reach(link.id, link.pseudo, *r);
            }
            return *r;
        };
        for (size_t p = 0; p < (size_t)Prop::Count; ++p) {
            if (!own.props[p].empty() || !(selection || inherited((Prop)p))) continue;
            for (const Link& link : chain) {
                const std::vector<Candidate>& list = reachOf(link).props[p];
                if (list.empty()) continue;
                out.push_back(describe(list[0], propertyName((int)p), Applied::Inherited, link.depth, link.pseudo));
                break;
            }
        }
        // The custom properties those read, and those that these read in turn
        std::unordered_set<std::string> followed;
        for (size_t r = 0; r < reads.size(); ++r) {
            std::string name = reads[r];
            if (!followed.insert(name).second || own.customs.count(name)) continue;
            for (const Link& link : customChain) {
                const Reach& at = reachOf(link);
                auto found = at.customs.find(name);
                if (found == at.customs.end()) continue;
                out.push_back(describe(found->second[0], name, Applied::Inherited, link.depth, link.pseudo));
                break;
            }
        }
        return out;
    }

    Impl() { compileAll(); }

    void load(std::vector<std::shared_ptr<const Sheet>> list, const Media& m) {
        sheets.clear();
        for (auto& sheet : list) {
            if (sheet) sheets.push_back(std::move(sheet));
        }
        media = m;
        rules.clear();
        positions.clear();
        nested.clear();
        subjectOf.clear();
        subjects.clear();
        all = Buckets();
        siblings = Buckets();
        hasSiblings = false;
        contexts.clear();
        pool.clear();
        interned.clear();
        runs.clear();
        diagnostics.clear();
        reported.clear();
        customNodes.clear();
        compileAll();
        activate();
        ++generation;
    }

    void setMedia(const Media& m) {
        if (m.dark == media.dark && m.print == media.print && m.width == media.width) return;
        media = m;
        activate();
        for (Ctx& c : contexts) {
            c.style.reset();
            for (auto& p : c.pseudo) p.reset();
            c.pseudoDone = 0;
        }
        ++generation;
    }
};

Engine::Engine() : impl(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::load(std::vector<std::shared_ptr<const Sheet>> sheets, const Media& media) {
    impl->load(std::move(sheets), media);
}

void Engine::setMedia(const Media& media) { impl->setMedia(media); }
const Media& Engine::media() const { return impl->media; }
Engine::Context Engine::root() { return impl->root(); }

Engine::Context Engine::child(Context parent, const Element& element, const Element* previous, size_t previousCount) {
    impl->root();
    if (parent >= impl->contexts.size()) parent = 0;
    return impl->intern(parent, element, previous, previousCount);
}

const ComputedStyle& Engine::style(Context context) {
    impl->root();
    return impl->computedOf(context < impl->contexts.size() ? context : 0).s;
}

const ComputedStyle* Engine::pseudo(Context context, Pseudo which) {
    impl->root();
    return impl->pseudoOf(context < impl->contexts.size() ? context : 0, which);
}

std::vector<Applied> Engine::explain(Context context) {
    impl->root();
    return impl->explain(context < impl->contexts.size() ? context : 0, -1);
}

std::vector<Applied> Engine::explain(Context context, Pseudo which) {
    impl->root();
    return impl->explain(context < impl->contexts.size() ? context : 0, (int8_t)which);
}

uint64_t Engine::generation() const { return impl->generation; }
const std::vector<Diagnostic>& Engine::diagnostics() const { return impl->diagnostics; }

std::string selectorPath(const std::vector<Element>& path) {
    std::string out;
    for (size_t i = 0; i < path.size(); ++i) {
        const Element& e = path[i];
        if (i == 0 && e.tag == "html" && path.size() > 1) continue;
        if (!out.empty()) out += " > ";
        out += e.tag.empty() ? std::string_view("*") : e.tag;
        if (!e.id.empty()) out.append("#").append(e.id);
        for (std::string_view c : e.classes) out.append(".").append(c);
    }
    return out;
}

}
