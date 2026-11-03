// Rendering CSL elements for one item (csl_internal.h): text, numbers,
// labels, names, dates, groups and choices, into formatted pieces.
// Formatting flows down the tree; affixes and quotation marks wrap each
// element's output on the way back up, and punctuation is mended where
// outputs meet.

#pragma optimize("s", on)

#include "csl_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace csl {
namespace {

bool digit(char c) { return c >= '0' && c <= '9'; }

// The first page of a range or list: "45" from "45-67" or "45&#8211;67"
std::string firstPage(std::string_view pages) {
    std::string plain = stripMarkup(pages);
    size_t end = 0;
    while (end < plain.size() && plain[end] != ',' && plain[end] != '&' && !dashAt(plain, end)) ++end;
    return trim(std::string_view(plain).substr(0, end));
}

std::string particle(const std::string& particle, const std::string& family) {
    if (particle.empty()) return family;
    if (family.empty()) return particle;
    char last = particle.back();
    if (last == '\'' || last == '-' || (particle.size() >= 3 && particle.compare(particle.size() - 3, 3, "\xE2\x80\x99") == 0)) {
        return particle + family;
    }
    return particle + " " + family;
}

std::string words(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + " " + b;
}

// Given names as initials: "John Ronald" -> "J. R.", "Jean-Paul" -> "J.-P.".
// With `all` false only what is already an initial is reshaped
std::string initials(std::string_view given, std::string_view with, bool all, bool hyphen) {
    std::string out;
    bool afterHyphen = false;
    auto trimEnd = [&]() {
        while (!out.empty() && out.back() == ' ') out.pop_back();
    };
    size_t i = 0;
    while (i < given.size()) {
        while (i < given.size() && given[i] == ' ') ++i;
        if (i >= given.size()) break;
        size_t start = i;
        while (i < given.size() && given[i] != ' ' && given[i] != '.' && given[i] != '-') ++i;
        std::string_view part = given.substr(start, i - start);
        bool dotted = i < given.size() && given[i] == '.';
        if (dotted) ++i;
        bool hyphenNext = i < given.size() && given[i] == '-';
        if (hyphenNext) ++i;
        if (part.empty()) continue;
        size_t k = 0;
        uint32_t first = nextChar(part, k);
        bool initial = k == part.size();
        if (afterHyphen && hyphen) {
            trimEnd();
            out += '-';
        } else if (!out.empty() && out.back() != ' ' && !(all || initial)) {
            out += ' ';
        }
        if (all || initial) {
            appendChar(out, first);
            out += with;
        } else {
            out += part;
            if (dotted) out += '.';
            out += ' ';
        }
        afterHyphen = hyphenNext;
    }
    trimEnd();
    return out;
}

std::string padded(long long value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%012lld", value);
    return buffer;
}

// Dates sort as YYYYYYMMDD, years offset so that BC comes first
std::string dateKey(const bib::Date& d, int precision) {
    auto key = [&](const bib::DateParts& p) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%06d%02d%02d", p.year + 100000, precision >= 2 ? p.month : 0,
                      precision >= 3 ? p.day : 0);
        return std::string(buffer);
    };
    if (d.from.year == 0) return collationKey(d.literal);
    std::string out = key(d.from);
    if (d.to.year != 0) out += key(d.to);
    return out;
}

struct Stats {
    bool called = false;  // a variable was asked for
    bool filled = false;  // and one had a value
};

// Piece lists for the elements under way, reused from render to render on a
// thread so that rendering seldom allocates. Taken and given back in turn,
// as the elements nest
class Scratch {
public:
    Pieces& take() {
        if (used == lists.size()) lists.push_back(std::make_unique<Pieces>());
        return *lists[used++];
    }
    void give(Pieces& pieces) {
        pieces.clear();
        --used;
    }

private:
    std::vector<std::unique_ptr<Pieces>> lists;
    size_t used = 0;
};

Scratch& scratch() {
    thread_local Scratch pool;
    return pool;
}

struct Buffer {
    Scratch& pool;
    Pieces& p;
    explicit Buffer(Scratch& pool) : pool(pool), p(pool.take()) {}
    ~Buffer() { pool.give(p); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

class Renderer {
public:
    explicit Renderer(const RenderJob& job)
        : job(job), eng(*job.engine), style(*job.engine->style), item(*job.state->item), state(*job.state),
          cite(job.cite ? job.cite->cite : nullptr), piq(job.engine->punctuationInQuote), pool(scratch()) {
        const auto* language = static_cast<const std::string*>(at(style.slotLanguage));
        english = !language || language->empty() ||
                  (language->size() >= 2 && ((*language)[0] | 0x20) == 'e' && ((*language)[1] | 0x20) == 'n');
    }

    // Renders the elements one after another, with `delimiter` between
    // those that print; a choose's branch counts as elements of the list.
    // Each element writes straight into `out`; the delimiter is owed until
    // it prints, and finish() pays it
    void list(const std::vector<Element>& elements, std::string_view delimiter, uint16_t flags, Pieces& out,
              Stats& stats, bool& any) {
        for (const Element& e : elements) {
            if (e.kind == Kind::Choose) {
                if (const Branch* b = choose(e)) list(b->children, delimiter, flags, out, stats, any);
                continue;
            }
            Owed saved = owed;
            if (any && !delimiter.empty()) owed = {&out, delimiter, flags};
            uint64_t before = prints;
            element(e, flags, out, stats);
            bool printed = prints != before && printedTo == &out;
            if (printed) any = true;
            owed = printed && (saved.target == &out || saved.target == nullptr) ? Owed{} : saved;
        }
    }

    // As list, with the first element that prints kept apart
    void split(const std::vector<Element>& elements, uint16_t flags, Pieces& first, Pieces& rest, Stats& stats,
               bool& any) {
        for (const Element& e : elements) {
            if (e.kind == Kind::Choose) {
                if (const Branch* b = choose(e)) split(b->children, flags, first, rest, stats, any);
                continue;
            }
            Buffer part(pool);
            element(e, flags, part.p, stats);
            if (part.p.empty()) continue;
            join(any ? rest : first, part.p, piq);
            any = true;
        }
    }

    void element(const Element& e, uint16_t flags, Pieces& out, Stats& stats) {
        switch (e.kind) {
            case Kind::Text: text(e, flags, out, stats); break;
            case Kind::Number: number(e, flags, out, stats); break;
            case Kind::Label: label(e, flags, out); break;
            case Kind::Names: names(e, flags, out, stats); break;
            case Kind::Date: date(e, flags, out, stats); break;
            case Kind::Group: group(e, flags, out, stats); break;
            case Kind::Choose:
                if (const Branch* b = choose(e)) {
                    bool any = false;
                    list(b->children, {}, flags, out, stats, any);
                }
                break;
        }
    }

    std::string sortKey(const SortKey& key) {
        if (key.macro >= 0) {
            Buffer out(pool);
            Stats stats;
            bool any = false;
            list(style.macros[key.macro], {}, 0, out.p, stats, any);
            return collationKey(plainText(out.p));
        }
        int slot = key.var;
        if (slot < 0) return {};
        uint8_t kind = style.variableKinds[slot];
        if (kind == VarName) {
            const auto* list = static_cast<const std::vector<bib::Name>*>(at(slot));
            if (!list) return {};
            static const NamesSpec plainSpec;
            NameOptions o;
            o.has = NameOptions::HasForm | NameOptions::HasSortOrder;
            o.form = NameLong;
            o.sortOrder = SortAll;
            o.inherit(job.bibliography ? eng.bibliographyNames.options : eng.citationNames.options);
            Buffer out(pool);
            int counter = 0;
            nameList(*list, o, plainSpec, 0, out.p, false, counter);
            return collationKey(plainText(out.p));
        }
        if (kind == VarDate) {
            const auto* d = static_cast<const bib::Date*>(at(slot));
            return d ? dateKey(*d, 3) : std::string();
        }
        std::string_view text = value(slot);
        if (text.empty()) return {};
        return numericKey(stripMarkup(text));
    }

private:
    const RenderJob& job;
    const Engine& eng;
    const Style& style;
    const bib::Item& item;
    const ItemState& state;
    const cite::Item* cite;
    bool piq;
    Scratch& pool;
    bool english = true;
    // A few variable slots, kept without allocating; a substitute takes a
    // handful at most
    struct Slots {
        int slots[32];
        int count = 0;
        void add(int slot) {
            if (count < 32 && !has(slot)) slots[count++] = slot;
        }
        bool has(int slot) const {
            for (int k = 0; k < count; ++k) {
                if (slots[k] == slot) return true;
            }
            return false;
        }
    };
    Slots suppressed;          // variables a substitute took
    Slots* tracked = nullptr;  // variables printed while substituting
    const NamesSpec* borrowed = nullptr;  // what a shorthand <names> borrows
    int namesDepth = 0;
    int depth = 0;
    int quoteDepth = 0;
    bool firstNamesDone = false;
    bool marking = false;
    bool yearSuffixDone = false;
    mutable bool firstPageDone = false;
    mutable std::string firstPageText;
    // A list's delimiter, owed to its pieces until the next element prints
    struct Owed {
        Pieces* target = nullptr;
        std::string_view text;
        uint16_t flags = 0;
    };
    Owed owed;
    uint64_t prints = 0;               // elements printed so far
    const Pieces* printedTo = nullptr;  // where the last one went

    bool isSuppressed(int slot) const {
        return suppressed.count && suppressed.has(slot);
    }

    void track(int slot) {
        if (tracked && slot >= 0) tracked->add(slot);
    }

    const void* at(int slot) const {
        return slot >= 0 && slot < (int)state.values.size() ? state.values[slot] : nullptr;
    }

    // Numbers compare as numbers when a sort key starts with one
    static std::string numericKey(std::string_view value) {
        std::string text(value);
        size_t i = 0;
        while (i < text.size() && !digit(text[i]) && i < 3 && ((text[i] | 0x20) >= 'a' && (text[i] | 0x20) <= 'z')) ++i;
        if (i < text.size() && digit(text[i])) {
            size_t end = i;
            while (end < text.size() && digit(text[end]) && end - i < 12) ++end;
            return collationKey(text.substr(0, i)) + padded(std::atoll(text.substr(i, end - i).c_str())) +
                   collationKey(text.substr(end));
        }
        return collationKey(text);
    }

    std::string_view firstPageOf() const {
        if (!firstPageDone) {
            firstPageDone = true;
            if (const std::string* p = item.field("page-first")) firstPageText = *p;
            else if (const auto* page = static_cast<const std::string*>(at(style.slotPage))) firstPageText = firstPage(*page);
        }
        return firstPageText;
    }

    // A standard or number variable's text, as rich text; empty when unset
    // or substituted. `shortSlot` is the -short twin form="short" prefers
    std::string_view value(int slot, int shortSlot = -1) const {
        if (slot < 0 || isSuppressed(slot)) return {};
        switch (style.variableKinds[slot]) {
            case VarCitationNumber: return state.numberText;
            case VarYearSuffix: return state.yearSuffix;
            case VarLocator: return cite ? std::string_view(cite->locator) : std::string_view();
            case VarPageFirst: return firstPageOf();
            case VarStandard: break;
            default: return {};
        }
        if (const auto* p = static_cast<const std::string*>(at(shortSlot))) return *p;
        if (const auto* p = static_cast<const std::string*>(at(slot))) return *p;
        return {};
    }

    bool has(int slot) const {
        if (slot < 0) return false;
        switch (style.variableKinds[slot]) {
            case VarCitationNumber: return state.number > 0;
            case VarYearSuffix: return !state.yearSuffix.empty();
            case VarLocator: return cite && !cite->locator.empty();
            case VarPageFirst: return !firstPageOf().empty();
            case VarNoteNumber: return false;
            default: return at(slot) != nullptr;
        }
    }

    bool check(const Test& t) const {
        switch (t.kind) {
            case Test::Type: return t.var >= 0 && state.type == t.var;
            case Test::Variable: return has(t.var);
            case Test::IsNumeric: {
                if (t.var < 0) return false;
                uint8_t kind = style.variableKinds[t.var];
                std::string_view v = kind == VarLocator ? (cite ? std::string_view(cite->locator) : std::string_view())
                                   : kind == VarStandard ? (at(t.var) ? std::string_view(*static_cast<const std::string*>(at(t.var))) : std::string_view())
                                   : value(t.var);
                if (v.find_first_of("&<") != std::string_view::npos) return isNumeric(stripMarkup(v));
                return !v.empty() && isNumeric(v);
            }
            case Test::IsUncertainDate: {
                const auto* d = t.var >= 0 && style.variableKinds[t.var] == VarDate ? static_cast<const bib::Date*>(at(t.var)) : nullptr;
                return d && d->circa;
            }
            case Test::Locator: {
                if (!cite || cite->locator.empty()) return false;
                std::string_view label = cite->label.empty() ? std::string_view("page") : std::string_view(cite->label);
                if (t.value == "sub-verbo" || t.value == "sub verbo") return label == "sub-verbo" || label == "sub verbo";
                return label == t.value;
            }
            case Test::Position: {
                if (!job.cite || job.bibliography || job.sorting) return false;
                if (job.positional) *job.positional = true;
                uint8_t p = job.cite->position;
                if (t.value == "first") return p == First;
                if (t.value == "subsequent") return p != First;
                if (t.value == "ibid") return p == Ibid || p == IbidWithLocator;
                if (t.value == "ibid-with-locator") return p == IbidWithLocator;
                if (t.value == "near-note") return p != First && job.cite->nearNote;
                return false;
            }
            case Test::Disambiguate:
                return !job.bibliography && !job.sorting && state.disambiguate == (t.value == "true");
        }
        return false;
    }

    const Branch* choose(const Element& e) const {
        for (const Branch& b : e.branches) {
            if (b.tests.empty()) return &b;
            bool result = b.match != MatchAny;
            for (const Test& t : b.tests) {
                bool r = check(t);
                if (b.match == MatchAll && !r) {
                    result = false;
                    break;
                }
                if (b.match == MatchAny && r) {
                    result = true;
                    break;
                }
                if (b.match == MatchNone && r) {
                    result = false;
                    break;
                }
            }
            if (result) return &b;
        }
        return nullptr;
    }

    static void plainPiece(Pieces& out, std::string_view text, uint16_t flags) {
        if (text.empty()) return;
        out.emplace_back();
        out.back().text = std::string(text);
        out.back().flags = flags;
    }

    // Text case, periods, quotation marks and affixes around an element's
    // content, all into `out` in their order
    void finish(const Formatting& fmt, uint16_t outer, Pieces& content, Pieces& out,
                const std::string* prefixLink = nullptr) {
        if (content.empty()) return;
        if (fmt.textCase) applyCase(content, fmt.textCase, english);
        if (fmt.stripPeriods) stripPeriods(content);
        if (owed.target == &out) {
            addText(out, owed.text, owed.flags, piq);
            owed = Owed{};
        }
        ++prints;
        printedTo = &out;
        if (!fmt.prefix.empty()) {
            size_t before = out.size();
            addText(out, fmt.prefix, outer, piq);
            if (prefixLink && out.size() > before) out.back().link = prefixLink;
        }
        if (fmt.quotes) {
            bool inner = quoteDepth % 2 == 1;
            if (!inner && !eng.openQuote.empty()) {
                // Quotation marks already in the text become the inner pair
                for (Piece& p : content) {
                    for (auto swap : {std::make_pair(&eng.openQuote, &eng.openInnerQuote),
                                      std::make_pair(&eng.closeQuote, &eng.closeInnerQuote)}) {
                        size_t at = 0;
                        while ((at = p.text.find(*swap.first, at)) != std::string::npos) {
                            p.text.replace(at, swap.first->size(), *swap.second);
                            at += swap.second->size();
                        }
                    }
                }
            }
            addText(out, inner ? eng.openInnerQuote : eng.openQuote, outer, piq);
            join(out, content, piq);
            out.emplace_back();
            out.back().text = inner ? eng.closeInnerQuote : eng.closeQuote;
            out.back().flags = outer;
            out.back().mark = CloseQuote;
        } else {
            join(out, content, piq);
        }
        if (!fmt.suffix.empty()) addText(out, fmt.suffix, outer, piq);
    }

    void text(const Element& e, uint16_t flags, Pieces& out, Stats& stats) {
        uint16_t inner = e.fmt.apply(flags);
        Buffer buffer(pool);
        Pieces& content = buffer.p;
        const std::string* link = nullptr;
        if (e.fmt.quotes) ++quoteDepth;
        switch (e.source) {
            case FromValue: plainPiece(content, e.value, inner); break;
            case FromTerm: plainPiece(content, eng.termText(e.value, e.form, e.plural == PluralAlways), inner); break;
            case FromMacro:
                if (e.macro >= 0 && depth < 48) {
                    ++depth;
                    bool any = false;
                    list(style.macros[e.macro], {}, inner, content, stats, any);
                    --depth;
                }
                break;
            case FromVariable: variable(e, inner, content, stats, link); break;
        }
        if (e.fmt.quotes) --quoteDepth;
        bool linkPrefix = link && e.var == style.slotDOI && e.fmt.prefix.find("doi.org/") != std::string::npos;
        finish(e.fmt, flags, content, out, linkPrefix ? link : nullptr);
    }

    bool pageLocator() const { return cite && (cite->label.empty() || cite->label == "page"); }

    void variable(const Element& e, uint16_t flags, Pieces& content, Stats& stats, const std::string*& link) {
        if (e.var < 0) return;
        uint8_t kind = style.variableKinds[e.var];
        if (kind == VarName || kind == VarDate) return;
        // A year suffix is no variable to a group, as citeproc has it: the
        // "n.d." beside an empty one in APA's and Chicago's dates stays
        bool counts = kind != VarYearSuffix;
        if (counts) stats.called = true;
        std::string_view v = value(e.var, e.shortVar);
        if (v.empty()) return;
        if (counts) stats.filled = true;
        track(e.var);
        if (e.var == style.slotPage || (kind == VarLocator && pageLocator())) {
            plainPiece(content, formatPages(stripMarkup(v), style.pageFormat, eng.pageDelimiter), flags);
        } else if (e.var == style.slotURL || e.var == style.slotDOI) {
            // Printed and linked as plain text: "a&amp;b" is a&b
            plainPiece(content, stripMarkup(v), flags);
            if (content.empty()) return;
            link = e.var == style.slotDOI ? &state.doiLink : &state.urlLink;
            content.back().link = link;
        } else {
            parseRich(v, flags, content);
        }
    }

    // The ordinal suffix for `n` in the gender of the variable's noun
    std::string ordinal(int n, std::string_view variable) const {
        uint8_t gender = 0;
        if (const Term* noun = eng.term(variable, Form::Long)) gender = noun->gender;
        auto find = [&](int k, uint8_t match) -> const Term* {
            char key[16];
            std::snprintf(key, sizeof key, "ordinal-%02d", k);
            const Term* t = eng.term(key, Form::Long, gender);
            if (!t) return nullptr;
            uint8_t m = t->match ? t->match : (k < 10 ? 1 : 2);
            return m == match ? t : nullptr;
        };
        const Term* t = n < 100 ? find(n, 3) : nullptr;
        if (!t) t = find(n % 100, 2);
        if (!t) t = find(n % 10, 1);
        if (!t) t = eng.term("ordinal", Form::Long, gender);
        return std::to_string(n) + (t ? t->single : std::string());
    }

    std::string numberText(int n, uint8_t form, std::string_view variable) const {
        if (form == Ordinal) return ordinal(n, variable);
        if (form == LongOrdinal) {
            if (n >= 1 && n <= 10) {
                char key[24];
                std::snprintf(key, sizeof key, "long-ordinal-%02d", n);
                uint8_t gender = 0;
                if (const Term* noun = eng.term(variable, Form::Long)) gender = noun->gender;
                if (const Term* t = eng.term(key, Form::Long, gender)) return t->single;
            }
            return ordinal(n, variable);
        }
        if (form == Roman) return roman(n);
        return std::to_string(n);
    }

    void number(const Element& e, uint16_t flags, Pieces& out, Stats& stats) {
        stats.called = true;
        std::string_view v = value(e.var);
        if (v.empty()) return;
        stats.filled = true;
        track(e.var);
        uint16_t inner = e.fmt.apply(flags);
        Buffer contentBuffer(pool);
        Pieces& content = contentBuffer.p;
        std::string plainValue = stripMarkup(v);
        if (!isNumeric(plainValue)) {
            parseRich(v, inner, content);
        } else if (job.sorting) {
            plainPiece(content, numericKey(plainValue), inner);
        } else {
            // Each number in its form; ranges take an en dash
            std::string text;
            size_t i = 0;
            std::string number = trim(plainValue);
            auto stop = [&](size_t at) {
                char c = number[at];
                return digit(c) || c == ',' || c == '&' || c == ' ' || dashAt(number, at);
            };
            while (i < number.size()) {
                size_t start = i;
                while (i < number.size() && !stop(i)) ++i;
                size_t digits = i;
                while (i < number.size() && digit(number[i])) ++i;
                size_t after = i;
                while (i < number.size() && !stop(i)) ++i;
                if (digits == start && after == i && after > digits) {
                    text += numberText(std::atoi(number.substr(digits, after - digits).c_str()), e.numberForm, e.value);
                } else {
                    text += number.substr(start, i - start);
                }
                while (i < number.size() && number[i] == ' ') ++i;
                if (i >= number.size()) break;
                if (size_t dash = dashAt(number, i)) {
                    text += "\xE2\x80\x93";
                    i += dash;
                } else if (number[i] == ',') {
                    text += ", ";
                    ++i;
                } else if (number[i] == '&') {
                    text += " & ";
                    ++i;
                }
                while (i < number.size() && number[i] == ' ') ++i;
            }
            plainPiece(content, text, inner);
        }
        finish(e.fmt, flags, content, out);
    }

    void label(const Element& e, uint16_t flags, Pieces& out) {
        std::string_view variable = e.value;
        std::string_view v;
        std::string_view term = variable;
        if (e.var >= 0 && style.variableKinds[e.var] == VarLocator) {
            if (!cite || cite->locator.empty()) return;
            v = cite->locator;
            term = cite->label.empty() ? std::string_view("page") : std::string_view(cite->label);
            if (term == "sub verbo") term = "sub-verbo";
        } else {
            v = value(e.var);
            if (v.empty()) return;
        }
        bool plural = e.plural == PluralAlways;
        std::string plainValue;
        if (v.find_first_of("&<") != std::string_view::npos) {
            plainValue = stripMarkup(v);
            v = plainValue;
        }
        if (e.plural == PluralContextual) {
            plural = variable == "number-of-pages" || variable == "number-of-volumes"
                         ? std::atoi(std::string(v).c_str()) > 1
                         : hasSeveral(v);
        }
        const std::string& text = eng.termText(term, e.form, plural);
        if (text.empty()) return;
        Buffer contentBuffer(pool);
        Pieces& content = contentBuffer.p;
        plainPiece(content, text, e.fmt.apply(flags));
        finish(e.fmt, flags, content, out);
    }

    void group(const Element& e, uint16_t flags, Pieces& out, Stats& stats) {
        uint16_t inner = e.fmt.apply(flags);
        Stats own;
        Buffer contentBuffer(pool);
        Pieces& content = contentBuffer.p;
        bool any = false;
        if (e.fmt.quotes) ++quoteDepth;
        list(e.children, e.delimiter, inner, content, own, any);
        if (e.fmt.quotes) --quoteDepth;
        stats.called |= own.called;
        stats.filled |= own.filled;
        // A group that asks for variables and finds none prints nothing
        if (own.called && !own.filled) return;
        finish(e.fmt, flags, content, out);
    }

    // --- Names ---------------------------------------------------------------

    NameOptions options(const NamesSpec& spec) const {
        NameOptions o = spec.name;
        if (spec.level.has) o.inherit(spec.level);
        o.inherit(job.bibliography ? eng.bibliographyNames.options : eng.citationNames.options);
        return o;
    }

    int expansion(const bib::Name& name) const {
        if (job.bibliography || job.sorting) return 0;
        for (const auto& entry : state.expand) {
            if (entry.first == &name) return entry.second;
        }
        return 0;
    }

    void namePart(const Formatting& fmt, const std::string& text, uint16_t flags, Pieces& out) {
        if (text.empty()) return;
        Buffer contentBuffer(pool);
        Pieces& content = contentBuffer.p;
        plainPiece(content, text, fmt.apply(flags));
        finish(fmt, flags, content, out);
    }

    // One name, as `o` asks; `level` 1 shows initials, 2 the full given name
    void personName(const bib::Name& n, const NameOptions& o, const NamesSpec& spec, bool inverted, int level,
                    uint16_t flags, Pieces& out) {
        if (!n.literal.empty()) {
            plainPiece(out, n.literal, flags);
            return;
        }
        bool shortForm = o.form == NameShort && level == 0;
        if (hasCjk(n.family) || hasCjk(n.given)) {
            // Chinese, Japanese and Korean names: family name first, unspaced
            namePart(spec.familyFmt, n.family, flags, out);
            if (!shortForm) namePart(spec.givenFmt, n.given, flags, out);
            return;
        }
        std::string given = n.given;
        if (!given.empty() && level < 2 && (o.has & NameOptions::HasInitializeWith)) {
            given = initials(given, o.initializeWith, o.initialize, style.initializeWithHyphen);
        }
        if (n.family.empty()) {
            // One name only, as Plato or Madonna: whole in every form
            namePart(spec.givenFmt, n.given, flags, out);
            return;
        }
        if (shortForm) {
            namePart(spec.familyFmt, particle(n.nonDroppingParticle, n.family), flags, out);
            return;
        }
        if (n.staticOrdering) {
            namePart(spec.familyFmt, particle(n.nonDroppingParticle, n.family), flags, out);
            if (!given.empty()) {
                plainPiece(out, " ", flags);
                namePart(spec.givenFmt, given, flags, out);
            }
            return;
        }
        if (!inverted) {
            std::string first = words(given, n.droppingParticle);
            if (!first.empty()) {
                namePart(spec.givenFmt, first, flags, out);
                plainPiece(out, " ", flags);
            }
            namePart(spec.familyFmt, particle(n.nonDroppingParticle, n.family), flags, out);
            if (!n.suffix.empty()) {
                plainPiece(out, n.commaSuffix ? ", " : " ", flags);
                plainPiece(out, n.suffix, flags);
            }
            return;
        }
        bool demote = style.demote == DemoteDisplayAndSort || (job.sorting && style.demote == DemoteSortOnly);
        std::string family = demote ? n.family : particle(n.nonDroppingParticle, n.family);
        std::string rest = demote ? words(given, words(n.droppingParticle, n.nonDroppingParticle))
                                  : words(given, n.droppingParticle);
        namePart(spec.familyFmt, family, flags, out);
        if (!rest.empty()) {
            plainPiece(out, o.sortSeparator, flags);
            namePart(spec.givenFmt, rest, flags, out);
        }
        if (!n.suffix.empty()) {
            plainPiece(out, o.sortSeparator, flags);
            plainPiece(out, n.suffix, flags);
        }
    }

    // Notes a name the citation printed, for disambiguation: as printed, and
    // keys that tell people apart by initials and by full given name
    void record(const bib::Name& n, const NameOptions& o, const NamesSpec& spec, bool inverted, bool primary,
                const Pieces& printed, int level) {
        if (!job.records || !n.literal.empty()) return;
        NameRecord r;
        r.name = &n;
        r.primary = primary;
        if (level == 0) {
            r.forms[0] = plainText(printed);
        } else {
            Buffer oneBuffer(pool);
            personName(n, o, spec, inverted, 0, 0, oneBuffer.p);
            r.forms[0] = plainText(oneBuffer.p);
        }
        std::string family = particle(n.nonDroppingParticle, n.family);
        r.forms[1] = family + '\x1F' + initials(n.given, ".", true, true);
        r.forms[2] = family + '\x1F' + n.given;
        job.records->push_back(std::move(r));
    }

    static bool precedes(uint8_t rule, bool contextual, bool afterInverted) {
        switch (rule) {
            case Contextual: return contextual;
            case AfterInvertedName: return afterInverted;
            case Always: return true;
            default: return false;
        }
    }

    static bool isOthers(const bib::Name& n) { return n.literal == "others" && n.family.empty() && n.given.empty(); }

    // A list of names with its delimiters, "and", et al. or the ellipsis
    void nameList(const std::vector<bib::Name>& names, const NameOptions& o, const NamesSpec& spec, uint16_t flags,
                  Pieces& out, bool firstList, int& counter) {
        size_t n = names.size();
        bool forced = false;
        if (n >= 2 && isOthers(names.back())) {
            --n;
            forced = true;
        }
        if (n == 0 || (n == 1 && isOthers(names[0]))) return;
        int min = o.etAlMin, useFirst = o.etAlUseFirst;
        bool useLast = o.etAlUseLast;
        if (job.cite && !job.bibliography && !job.sorting &&
            (o.has & (NameOptions::HasSubsequentMin | NameOptions::HasSubsequentUseFirst))) {
            if (job.positional) *job.positional = true;
            if (job.cite->position != First) {
                if (o.has & NameOptions::HasSubsequentMin) min = o.subsequentMin;
                if (o.has & NameOptions::HasSubsequentUseFirst) useFirst = o.subsequentUseFirst;
            }
        }
        if (job.sorting) {
            if (job.namesMin >= 0) min = job.namesMin;
            if (job.namesUseFirst >= 0) useFirst = job.namesUseFirst;
            if (job.namesUseLast >= 0) useLast = job.namesUseLast != 0;
        }
        bool truncated = min > 0 && useFirst > 0 && (int)n >= min && useFirst < (int)n;
        size_t shown = truncated ? (size_t)useFirst : n;
        if (truncated && !job.bibliography && !job.sorting && state.addNames > 0) {
            shown = std::min(n, shown + (size_t)state.addNames);
            if (shown == n) truncated = false;
        }
        bool etAl = truncated || forced;
        bool last = truncated && useLast && !forced && n >= shown + 2;
        if (o.form == NameCount) {
            plainPiece(out, std::to_string(shown + (last ? 1 : 0)), flags);
            return;
        }
        uint16_t nameFlags = spec.nameFmt.apply(flags);
        Buffer listBuffer(pool);
        Pieces& list = listBuffer.p;
        auto inverted = [&](size_t i) {
            return job.sorting || o.sortOrder == SortAll || (o.sortOrder == SortFirst && i == 0);
        };
        auto one = [&](size_t i) {
            Buffer nameBuffer(pool);
            Pieces& name = nameBuffer.p;
            int level = expansion(names[i]);
            personName(names[i], o, spec, inverted(i), level, nameFlags, name);
            record(names[i], o, spec, inverted(i), firstList && i == 0, name, level);
            if (marking) {
                ++counter;
                for (Piece& p : name) p.name = (uint8_t)std::min(counter, 255);
            }
            join(list, name, piq);
        };
        for (size_t i = 0; i < shown; ++i) {
            if (i > 0) {
                if (i + 1 == shown && !etAl && o.andMode) {
                    bool comma = precedes(o.precedesLast, shown >= 3, inverted(i - 1));
                    std::string connector = comma ? o.delimiter : std::string(" ");
                    connector += eng.termText("and", o.andMode == 2 ? Form::Symbol : Form::Long);
                    connector += ' ';
                    addText(list, connector, nameFlags, piq);
                } else {
                    addText(list, o.delimiter, nameFlags, piq);
                }
            }
            one(i);
        }
        if (last) {
            addText(list, o.delimiter, nameFlags, piq);
            addText(list, "\xE2\x80\xA6 ", nameFlags, piq);
            one(n - 1);
        }
        if (!spec.nameFmt.prefix.empty() || !spec.nameFmt.suffix.empty()) {
            Formatting affixes;
            affixes.prefix = spec.nameFmt.prefix;
            affixes.suffix = spec.nameFmt.suffix;
            Buffer wrappedBuffer(pool);
            Pieces& wrapped = wrappedBuffer.p;
            finish(affixes, flags, list, wrapped);
            list.swap(wrapped);
        }
        if (etAl && !last && !job.sorting) {
            const std::string& term = eng.termText(spec.etAlTerm, Form::Long);
            if (!term.empty()) {
                bool comma = precedes(o.precedesEtAl, shown >= 2, inverted(shown - 1));
                addText(list, comma ? o.delimiter : std::string(" "), nameFlags, piq);
                Pieces etAlText;
                plainPiece(etAlText, term, spec.etAlFmt.apply(flags));
                finish(spec.etAlFmt, flags, etAlText, list);
            }
        }
        if (marking) {
            for (Piece& p : list) p.mark |= FirstNames;
        }
        join(out, list, piq);
    }

    void nameLabel(const NamesSpec& spec, std::string_view variable, size_t count, uint16_t flags, Pieces& part) {
        const Element& l = *spec.label;
        bool plural = l.plural == PluralAlways || (l.plural == PluralContextual && count > 1);
        const std::string& text = eng.termText(variable, l.form, plural);
        if (text.empty()) return;
        Buffer contentBuffer(pool);
        Pieces& content = contentBuffer.p;
        Buffer labelledBuffer(pool);
        Pieces& labelled = labelledBuffer.p;
        plainPiece(content, text, l.fmt.apply(flags));
        finish(l.fmt, flags, content, labelled);
        if (spec.labelFirst) {
            join(labelled, part, piq);
            part.swap(labelled);
        } else {
            join(part, labelled, piq);
        }
    }

    static bool samePeople(const std::vector<bib::Name>& a, const std::vector<bib::Name>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].family != b[i].family || a[i].given != b[i].given || a[i].literal != b[i].literal ||
                a[i].nonDroppingParticle != b[i].nonDroppingParticle || a[i].suffix != b[i].suffix) {
                return false;
            }
        }
        return true;
    }

    void substitute(const NamesSpec& spec, const NamesSpec& look, uint16_t flags, Pieces& content) {
        const NamesSpec* saved = borrowed;
        borrowed = &look;
        for (const Element& sub : spec.substitute) {
            Slots seen;
            Slots* outer = tracked;
            tracked = &seen;
            Stats subStats;
            Buffer partBuffer(pool);
            Pieces& part = partBuffer.p;
            element(sub, flags, part, subStats);
            tracked = outer;
            if (part.empty()) continue;
            // What stood in is not printed again in this entry
            for (int k = 0; k < seen.count; ++k) {
                suppressed.add(seen.slots[k]);
                if (outer) outer->add(seen.slots[k]);
            }
            content.swap(part);
            break;
        }
        borrowed = saved;
    }

    void names(const Element& e, uint16_t flags, Pieces& out, Stats& stats) {
        const NamesSpec& spec = *e.names;
        const NamesSpec& look = spec.shorthand() && borrowed ? *borrowed : spec;
        uint16_t inner = e.fmt.apply(flags);
        stats.called = true;
        struct List {
            const std::string* variable;
            int slot;
            const std::vector<bib::Name>* names;
        };
        List lists[16];
        size_t listCount = 0;
        for (size_t i = 0; i < spec.vars.size() && listCount < 16; ++i) {
            int slot = spec.vars[i];
            if (slot < 0 || style.variableKinds[slot] != VarName || isSuppressed(slot)) continue;
            if (const auto* p = static_cast<const std::vector<bib::Name>*>(at(slot))) {
                lists[listCount++] = {&spec.variables[i], slot, p};
            }
        }
        // Editors who also translated: once, as editortranslator
        static const std::string editorTranslator = "editortranslator";
        int editor = -1, translator = -1;
        for (size_t i = 0; i < listCount; ++i) {
            if (lists[i].slot == style.slotEditor) editor = (int)i;
            if (lists[i].slot == style.slotTranslator) translator = (int)i;
        }
        bool merged = editor >= 0 && translator >= 0 && samePeople(*lists[editor].names, *lists[translator].names);
        if (merged) {
            for (size_t i = (size_t)translator; i + 1 < listCount; ++i) lists[i] = lists[i + 1];
            --listCount;
            if (translator < editor) --editor;
        }
        bool outermost = namesDepth == 0;
        bool savedMarking = marking;
        if (outermost && !firstNamesDone && job.namesMode == NamesMark) marking = true;
        ++namesDepth;
        Buffer contentBuffer(pool);
        Pieces& content = contentBuffer.p;
        if (listCount == 0) {
            if (spec.hasSubstitute) substitute(spec, look, inner, content);
        } else {
            NameOptions o = options(look);
            const NameLevel& level = job.bibliography ? eng.bibliographyNames : eng.citationNames;
            std::string_view delimiter = e.hasDelimiter ? std::string_view(e.delimiter)
                                       : level.hasNamesDelimiter ? std::string_view(level.namesDelimiter)
                                       : std::string_view(", ");
            bool any = false;
            int counter = 0;
            for (size_t k = 0; k < listCount; ++k) {
                const List& l = lists[k];
                Buffer partBuffer(pool);
                Pieces& part = partBuffer.p;
                nameList(*l.names, o, look, inner, part, k == 0, counter);
                if (part.empty()) continue;
                if (look.label) {
                    size_t count = l.names->size();
                    if (count >= 2 && isOthers(l.names->back())) --count;
                    bool both = merged && (int)k == editor;
                    nameLabel(look, both ? std::string_view(editorTranslator) : std::string_view(*l.variable), count,
                              inner, part);
                }
                if (any) addText(content, delimiter, inner, piq);
                join(content, part, piq);
                any = true;
                track(l.slot);
                if (merged && (int)k == editor) track(style.slotTranslator);
            }
        }
        --namesDepth;
        marking = savedMarking;
        if (content.empty()) return;
        if (outermost && !firstNamesDone) {
            firstNamesDone = true;
            if (job.namesMode == NamesSuppress) return;
            if (job.namesMode == NamesCapture) {
                Buffer wholeBuffer(pool);
                Pieces& whole = wholeBuffer.p;
                finish(e.fmt, flags, content, whole);
                if (job.captured) job.captured->swap(whole);
                return;
            }
        }
        stats.filled = true;
        finish(e.fmt, flags, content, out);
    }

    // --- Dates ---------------------------------------------------------------

    // The date's parts: its own, or the locale's with its overrides. Dates
    // never nest, so one list per thread serves
    const std::vector<DatePart>& resolveParts(const Element& e, std::string_view& delimiter) const {
        const DateSpec& spec = *e.date;
        if (spec.localized < 0) {
            delimiter = e.delimiter;
            return spec.parts;
        }
        const DateFormat* format = spec.localized == 0 ? eng.dateText : eng.dateNumeric;
        thread_local std::vector<DatePart> parts;
        parts.clear();
        if (!format) {
            parts.emplace_back();
            return parts;
        }
        delimiter = format->delimiter;
        for (const DatePart& p : format->parts) {
            if (p.name == PartMonth && spec.precision < 2) continue;
            if (p.name == PartDay && spec.precision < 3) continue;
            DatePart q = p;
            for (const DatePart& o : spec.parts) {
                if (o.name != p.name) continue;
                if (o.hasForm) q.form = o.form;
                if (o.hasRangeDelimiter) {
                    q.hasRangeDelimiter = true;
                    q.rangeDelimiter = o.rangeDelimiter;
                }
                q.fmt.set |= o.fmt.set;
                q.fmt.clear |= o.fmt.clear;
                q.fmt.flip ^= o.fmt.flip;
                if (o.fmt.textCase) q.fmt.textCase = o.fmt.textCase;
                if (o.fmt.stripPeriods) q.fmt.stripPeriods = true;
            }
            parts.push_back(std::move(q));
        }
        return parts;
    }

    std::string partText(const DatePart& part, const bib::DateParts& p, int season) {
        char buffer[16];
        switch (part.name) {
            case PartYear: {
                if (p.year == 0) return {};
                std::string text;
                if (p.year < 0) {
                    text = std::to_string(-p.year) + eng.termText("bc");
                } else if (part.form == PartShort) {
                    std::snprintf(buffer, sizeof buffer, "%02d", p.year % 100);
                    text = buffer;
                } else {
                    text = std::to_string(p.year);
                    if (p.year < 1000) text += eng.termText("ad");
                }
                if (!style.usesYearSuffix && !state.yearSuffix.empty() && !yearSuffixDone && !job.sorting) {
                    text += state.yearSuffix;
                    yearSuffixDone = true;
                }
                return text;
            }
            case PartMonth: {
                if (p.month >= 1 && p.month <= 12) {
                    if (part.form == PartNumeric) return std::to_string(p.month);
                    if (part.form == PartLeadingZeros) {
                        std::snprintf(buffer, sizeof buffer, "%02d", p.month);
                        return buffer;
                    }
                    std::snprintf(buffer, sizeof buffer, "month-%02d", p.month);
                    return eng.termText(buffer, part.form == PartShort ? Form::Short : Form::Long);
                }
                if (p.month == 0 && season >= 1 && season <= 4) {
                    std::snprintf(buffer, sizeof buffer, "season-%02d", season);
                    return eng.termText(buffer);
                }
                return {};
            }
            case PartDay: {
                if (p.day <= 0 || p.month <= 0) return {};
                if (part.form == PartLeadingZeros) {
                    std::snprintf(buffer, sizeof buffer, "%02d", p.day);
                    return buffer;
                }
                if (part.form == PartOrdinal && !(eng.limitDayOrdinals && p.day != 1)) {
                    std::snprintf(buffer, sizeof buffer, "month-%02d", p.month);
                    return ordinal(p.day, buffer);
                }
                return std::to_string(p.day);
            }
        }
        return {};
    }

    // The parts [from, to) of one date, joined by the delimiter
    void dateParts(const std::vector<DatePart>& parts, size_t from, size_t to, const bib::DateParts& p, int season,
                   std::string_view delimiter, bool keepPrefix, bool keepSuffix, uint16_t flags, Pieces& out) {
        bool any = false;
        for (size_t i = from; i < to; ++i) {
            const DatePart& part = parts[i];
            std::string text = partText(part, p, season);
            if (text.empty()) continue;
            Buffer contentBuffer(pool);
            Pieces& content = contentBuffer.p;
            plainPiece(content, text, part.fmt.apply(flags));
            Buffer renderedBuffer(pool);
            Pieces& rendered = renderedBuffer.p;
            if ((i == from && !keepPrefix) || (i + 1 == to && !keepSuffix)) {
                Formatting f = part.fmt;
                if (i == from && !keepPrefix) f.prefix.clear();
                if (i + 1 == to && !keepSuffix) f.suffix.clear();
                finish(f, flags, content, rendered);
            } else {
                finish(part.fmt, flags, content, rendered);
            }
            if (any && !delimiter.empty()) addText(out, delimiter, flags, piq);
            join(out, rendered, piq);
            any = true;
        }
    }

    void dateValue(const bib::Date& d, const std::vector<DatePart>& parts, std::string_view delimiter,
                   uint16_t flags, Pieces& out) {
        auto rendered = [&](uint8_t name) {
            for (const DatePart& p : parts) {
                if (p.name == name) return true;
            }
            return false;
        };
        if (d.to.year != 0) {
            int differs = -1;
            if (rendered(PartYear) && d.from.year != d.to.year) differs = PartYear;
            else if (rendered(PartMonth) && d.from.month != d.to.month) differs = PartMonth;
            else if (rendered(PartDay) && d.from.day != d.to.day) differs = PartDay;
            if (differs >= 0) {
                // The parts as fine as the difference or finer form the range;
                // coarser parts are shared and printed once
                size_t first = parts.size(), last = 0;
                for (size_t i = 0; i < parts.size(); ++i) {
                    if (parts[i].name >= differs) {
                        first = std::min(first, i);
                        last = i + 1;
                    }
                }
                bool contiguous = true;
                for (size_t i = first; i < last; ++i) contiguous = contiguous && parts[i].name >= differs;
                std::string range = "\xE2\x80\x93";
                for (const DatePart& p : parts) {
                    if (p.name == differs && p.hasRangeDelimiter) range = p.rangeDelimiter;
                }
                if (!contiguous) {
                    first = 0;
                    last = parts.size();
                }
                Buffer beforeBuffer(pool);
                Pieces& before = beforeBuffer.p;
                Buffer fromBuffer(pool);
                Pieces& from = fromBuffer.p;
                Buffer toBuffer(pool);
                Pieces& to = toBuffer.p;
                Buffer afterBuffer(pool);
                Pieces& after = afterBuffer.p;
                dateParts(parts, 0, first, d.from, d.season, delimiter, true, true, flags, before);
                dateParts(parts, first, last, d.from, d.season, delimiter, true, false, flags, from);
                dateParts(parts, first, last, d.to, 0, delimiter, false, true, flags, to);
                dateParts(parts, last, parts.size(), d.from, d.season, delimiter, true, true, flags, after);
                if (!before.empty() && !from.empty() && !delimiter.empty()) addText(before, delimiter, flags, piq);
                join(out, before, piq);
                join(out, from, piq);
                if (!to.empty()) {
                    addText(out, range, flags, piq);
                    join(out, to, piq);
                }
                if (!after.empty() && !out.empty() && !delimiter.empty()) addText(out, delimiter, flags, piq);
                join(out, after, piq);
                return;
            }
        }
        dateParts(parts, 0, parts.size(), d.from, d.season, delimiter, true, true, flags, out);
    }

    void date(const Element& e, uint16_t flags, Pieces& out, Stats& stats) {
        stats.called = true;
        if (e.var < 0 || style.variableKinds[e.var] != VarDate || isSuppressed(e.var)) return;
        const auto* d = static_cast<const bib::Date*>(at(e.var));
        if (!d) return;
        stats.filled = true;
        track(e.var);
        uint16_t inner = e.fmt.apply(flags);
        Buffer contentBuffer(pool);
        Pieces& content = contentBuffer.p;
        std::string_view delimiter;
        const std::vector<DatePart>& parts = resolveParts(e, delimiter);
        if (job.sorting) {
            int precision = 1;
            for (const DatePart& p : parts) precision = std::max(precision, p.name == PartDay ? 3 : p.name == PartMonth ? 2 : 1);
            plainPiece(content, dateKey(*d, precision), inner);
        } else if (d->from.year == 0) {
            parseRich(d->literal, inner, content);
        } else {
            dateValue(*d, parts, delimiter, inner, content);
        }
        finish(e.fmt, flags, content, out);
    }
};
}

namespace {

// Moves what a render made into the caller's list. The scratch list keeps
// its room for the next render, where swapping would hand it away
void handOver(Pieces& from, Pieces& to, bool punctuationInQuote) {
    if (!to.empty()) {
        join(to, from, punctuationInQuote);
        return;
    }
    to.reserve(from.size());
    for (Piece& piece : from) {
        if (!piece.text.empty()) to.push_back(std::move(piece));
    }
    from.clear();
}
}

void render(const RenderJob& job, const std::vector<Element>& elements, uint16_t flags, Pieces& out) {
    Renderer renderer(job);
    Stats stats;
    bool any = false;
    Buffer made(scratch());
    renderer.list(elements, {}, flags, made.p, stats, any);
    handOver(made.p, out, job.engine->punctuationInQuote);
}

void renderSplit(const RenderJob& job, const std::vector<Element>& elements, uint16_t flags, Pieces& first,
                 Pieces& rest) {
    Renderer renderer(job);
    Stats stats;
    bool any = false;
    Buffer madeFirst(scratch()), madeRest(scratch());
    renderer.split(elements, flags, madeFirst.p, madeRest.p, stats, any);
    handOver(madeFirst.p, first, job.engine->punctuationInQuote);
    handOver(madeRest.p, rest, job.engine->punctuationInQuote);
}

void renderElement(const RenderJob& job, const Element& element, uint16_t flags, Pieces& out) {
    Renderer renderer(job);
    Stats stats;
    Buffer made(scratch());
    renderer.element(element, flags, made.p, stats);
    handOver(made.p, out, job.engine->punctuationInQuote);
}

std::string sortValue(const RenderJob& job, const SortKey& key) {
    Renderer renderer(job);
    return renderer.sortKey(key);
}

std::string formatPages(std::string_view pages, uint8_t format, std::string_view delimiter) {
    auto rangeText = [&](std::string_view range) -> std::string {
        // Hyphens (CSL-JSON), "--" and en or em dashes (BibTeX) all make ranges
        size_t dash = std::string_view::npos, dashLength = 0;
        for (size_t i = 0; i < range.size(); ++i) {
            if (size_t length = dashAt(range, i)) {
                dash = i;
                dashLength = length;
                while (size_t more = dashAt(range, dash + dashLength)) dashLength += more;
                break;
            }
        }
        if (dash == std::string_view::npos) return std::string(range);
        size_t lead = 0;
        while (lead < range.size() && range[lead] == ' ') ++lead;
        std::string a = trim(range.substr(0, dash)), b = trim(range.substr(dash + dashLength));
        if (a.empty() || b.empty()) return std::string(range);
        // Digits after an optional prefix the two ends share ("S12-S15")
        auto splitNumber = [](const std::string& s, std::string& prefix, std::string& digits) {
            size_t i = 0;
            while (i < s.size() && !digit(s[i])) ++i;
            size_t j = i;
            while (j < s.size() && digit(s[j])) ++j;
            prefix = s.substr(0, i);
            digits = s.substr(i, j - i);
            return j == s.size() && !digits.empty();
        };
        std::string prefixA, digitsA, prefixB, digitsB;
        std::string second = b;
        if (format != PagesAsWritten && splitNumber(a, prefixA, digitsA) && splitNumber(b, prefixB, digitsB) &&
            (prefixB.empty() || prefixB == prefixA) && digitsA.size() <= 9 && digitsB.size() <= 9) {
            std::string full = digitsB;
            if (full.size() < digitsA.size()) full = digitsA.substr(0, digitsA.size() - full.size()) + full;
            long long na = std::atoll(digitsA.c_str()), nb = std::atoll(full.c_str());
            if (nb >= na) {
                auto minimal = [&](size_t keep) {
                    if (full.size() != digitsA.size()) return full;
                    size_t i = 0;
                    while (i < full.size() && full[i] == digitsA[i]) ++i;
                    size_t start = std::min(i, full.size() >= keep ? full.size() - keep : 0);
                    return full.substr(start);
                };
                std::string shortened = full;
                switch (format) {
                    case PagesMinimal: shortened = minimal(1); break;
                    case PagesMinimalTwo: shortened = minimal(2); break;
                    case PagesChicago15:
                    case PagesChicago16: {
                        if (na < 100 || na % 100 == 0) break;
                        if (na % 100 < 10) {
                            shortened = minimal(1);
                            break;
                        }
                        if (format == PagesChicago15 && digitsA.size() == 4 && full.size() == 4) {
                            size_t same = 0;
                            while (same < 4 && full[same] == digitsA[same]) ++same;
                            if (4 - same >= 3) break;
                        }
                        shortened = minimal(2);
                        break;
                    }
                    default: break;
                }
                second = prefixB + shortened;
            }
        }
        return std::string(range.substr(0, lead)) + a + std::string(delimiter) + second;
    };
    std::string out;
    size_t i = 0;
    while (i <= pages.size()) {
        size_t end = i;
        while (end < pages.size() && pages[end] != ',' && pages[end] != '&') ++end;
        out += rangeText(pages.substr(i, end - i));
        if (end >= pages.size()) break;
        out += pages[end];
        i = end + 1;
    }
    return out;
}
}
