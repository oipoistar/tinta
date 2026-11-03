// Citations and bibliographies (csl.h): the locale chain, numbering,
// sorting, positions, disambiguation, collapsing and in-text authors, around
// the element rendering in csl_render.cpp.

#pragma optimize("s", on)

#include "csl_internal.h"

#include <algorithm>
#include <mutex>
#include <numeric>
#include <unordered_set>

namespace csl {
namespace {

uint64_t hashText(std::string_view text) {
    uint64_t hash = 1469598103934665603ull;
    for (char c : text) {
        hash ^= (unsigned char)c;
        hash *= 1099511628211ull;
    }
    return hash;
}

// Locale files parsed once and found again by their text: a document
// formats on every change, and the files rarely do
std::shared_ptr<const Locale> cachedLocale(const std::string& text, std::string& error) {
    struct Entry {
        uint64_t hash;
        std::string text;
        std::shared_ptr<const Locale> locale;
    };
    static std::mutex lock;
    static std::vector<Entry> cache;
    uint64_t hash = hashText(text);
    {
        std::lock_guard<std::mutex> guard(lock);
        for (const Entry& entry : cache) {
            if (entry.hash == hash && entry.text == text) return entry.locale;
        }
    }
    std::shared_ptr<const Locale> locale = readLocale(text, error);
    if (!locale) return nullptr;
    std::lock_guard<std::mutex> guard(lock);
    if (cache.size() >= 8) cache.erase(cache.begin());
    cache.push_back({hash, text, locale});
    return locale;
}

std::string lowerAscii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    }
    return out;
}

bool sameTag(std::string_view a, std::string_view b) { return lowerAscii(a) == lowerAscii(b); }

// CSL's primary dialects: the locale a bare language falls back to
std::string primaryDialect(std::string_view language) {
    static const char* const table[][2] = {
        {"af", "af-ZA"}, {"ar", "ar"},    {"bg", "bg-BG"}, {"ca", "ca-AD"}, {"cs", "cs-CZ"}, {"cy", "cy-GB"},
        {"da", "da-DK"}, {"de", "de-DE"}, {"el", "el-GR"}, {"en", "en-US"}, {"es", "es-ES"}, {"et", "et-EE"},
        {"eu", "eu"},    {"fa", "fa-IR"}, {"fi", "fi-FI"}, {"fr", "fr-FR"}, {"he", "he-IL"}, {"hi", "hi-IN"},
        {"hr", "hr-HR"}, {"hu", "hu-HU"}, {"id", "id-ID"}, {"is", "is-IS"}, {"it", "it-IT"}, {"ja", "ja-JP"},
        {"km", "km-KH"}, {"ko", "ko-KR"}, {"la", "la"},    {"lt", "lt-LT"}, {"lv", "lv-LV"}, {"mn", "mn-MN"},
        {"nb", "nb-NO"}, {"nl", "nl-NL"}, {"nn", "nn-NO"}, {"pl", "pl-PL"}, {"pt", "pt-PT"}, {"ro", "ro-RO"},
        {"ru", "ru-RU"}, {"sk", "sk-SK"}, {"sl", "sl-SI"}, {"sr", "sr-RS"}, {"sv", "sv-SE"}, {"th", "th-TH"},
        {"tr", "tr-TR"}, {"uk", "uk-UA"}, {"vi", "vi-VN"}, {"zh", "zh-CN"}};
    for (const auto& entry : table) {
        if (language == entry[0]) return entry[1];
    }
    return {};
}

std::string languageOf(const Style& style, const Options& options) {
    std::string lang = !options.lang.empty() ? options.lang : style.info.defaultLocale;
    if (lang.empty()) lang = "en-US";
    std::replace(lang.begin(), lang.end(), '_', '-');
    return lang;
}

// The locale files a language falls back through: its own, its primary
// dialect's, then en-US
std::vector<std::string> fallbackTags(const std::string& lang) {
    std::string language = lowerAscii(lang.substr(0, lang.find('-')));
    std::vector<std::string> tags = {lang};
    std::string primary = primaryDialect(language);
    if (!primary.empty() && !sameTag(primary, lang)) tags.push_back(primary);
    if (!sameTag(lang, "en-US") && !sameTag(primary, "en-US")) tags.push_back("en-US");
    return tags;
}

void buildEngine(Engine& eng, const Style& style, const std::string& lang, const std::vector<std::string>& tags,
                 const std::vector<std::string>& texts) {
    std::vector<std::string>& warnings = eng.warnings;
    eng.style = &style;
    eng.lang = lang;
    std::string language = lowerAscii(lang.substr(0, lang.find('-')));
    // The chain, strongest first: the style's own locales for the tag, the
    // language and no language; then the files for the tag, its primary
    // dialect and en-US
    std::vector<const Locale*> chain;
    for (const Locale& l : style.locales) {
        if (!l.lang.empty() && sameTag(l.lang, lang)) chain.push_back(&l);
    }
    if (!sameTag(language, lang)) {
        for (const Locale& l : style.locales) {
            if (sameTag(l.lang, language)) chain.push_back(&l);
        }
    }
    for (const Locale& l : style.locales) {
        if (l.lang.empty()) chain.push_back(&l);
    }
    bool anyFile = false;
    for (size_t k = 0; k < tags.size(); ++k) {
        const std::string& tag = tags[k];
        const std::string& text = texts[k];
        if (text.empty()) continue;
        std::string error;
        std::shared_ptr<const Locale> locale = cachedLocale(text, error);
        if (!locale) {
            warnings.push_back("locale " + tag + ": " + error);
            continue;
        }
        eng.files.push_back(locale);
        chain.push_back(locale.get());
        anyFile = true;
    }
    if (!anyFile) warnings.push_back("no locale for " + lang);
    // Ordinal suffixes come as a set from the strongest locale that has
    // any, never mixed: zh-CN's empty "ordinal" must not meet en-US's "nd"
    auto set = [](const std::string& key) {
        auto named = [&](const char* name) {
            size_t length = std::char_traits<char>::length(name);
            return key.compare(0, length, name) == 0 && key.size() > length &&
                   (key[length] == '\x1F' || key[length] == '-');
        };
        return named("ordinal") ? 1 : named("long-ordinal") ? 2 : 0;
    };
    const Locale* source[3] = {nullptr, nullptr, nullptr};
    for (const Locale* l : chain) {
        for (const auto& term : l->terms) {
            int which = set(term.first);
            if (which && !source[which]) source[which] = l;
        }
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        for (const auto& term : (*it)->terms) {
            int which = set(term.first);
            if (which && source[which] != *it) continue;
            // The key is name, separator, form and gender: its hash is termHash's
            eng.terms[hashText(term.first)] = {&term.first, &term.second};
        }
    }
    int8_t punctuation = -1, ordinals = -1;
    for (const Locale* l : chain) {
        if (!eng.dateText && l->text.set) eng.dateText = &l->text;
        if (!eng.dateNumeric && l->numeric.set) eng.dateNumeric = &l->numeric;
        if (punctuation < 0) punctuation = l->punctuationInQuote;
        if (ordinals < 0) ordinals = l->limitDayOrdinals;
    }
    eng.punctuationInQuote = punctuation == 1;
    eng.limitDayOrdinals = ordinals == 1;
    eng.citationNames = style.citationNames;
    eng.citationNames.options.inherit(style.names.options);
    if (!eng.citationNames.hasNamesDelimiter && style.names.hasNamesDelimiter) {
        eng.citationNames.namesDelimiter = style.names.namesDelimiter;
        eng.citationNames.hasNamesDelimiter = true;
    }
    eng.bibliographyNames = style.bibliographyNames;
    eng.bibliographyNames.options.inherit(style.names.options);
    if (!eng.bibliographyNames.hasNamesDelimiter && style.names.hasNamesDelimiter) {
        eng.bibliographyNames.namesDelimiter = style.names.namesDelimiter;
        eng.bibliographyNames.hasNamesDelimiter = true;
    }
    auto termOr = [&](const char* name, const char* fallback) {
        const std::string& text = eng.termText(name);
        return text.empty() ? std::string(fallback) : text;
    };
    eng.openQuote = termOr("open-quote", "\xE2\x80\x9C");
    eng.closeQuote = termOr("close-quote", "\xE2\x80\x9D");
    eng.openInnerQuote = termOr("open-inner-quote", "\xE2\x80\x98");
    eng.closeInnerQuote = termOr("close-inner-quote", "\xE2\x80\x99");
    eng.pageDelimiter = termOr("page-range-delimiter", "\xE2\x80\x93");
    // The in-text author of a style whose citations name nobody: the
    // authors' family names, or the editors' or translators'
    auto fallback = std::make_unique<Element>();
    fallback->kind = Kind::Names;
    fallback->names = std::make_unique<NamesSpec>();
    NamesSpec& spec = *fallback->names;
    spec.variables = {"author"};
    spec.vars = {style.slotAuthor};
    spec.hasName = true;
    spec.name.has = NameOptions::HasForm | NameOptions::HasAnd;
    spec.name.form = NameShort;
    spec.name.andMode = 1;
    if (!(eng.citationNames.options.has & NameOptions::HasEtAlMin)) {
        spec.name.has |= NameOptions::HasEtAlMin | NameOptions::HasEtAlUseFirst;
        spec.name.etAlMin = 3;
        spec.name.etAlUseFirst = 1;
    }
    spec.hasSubstitute = true;
    for (int slot : {style.slotEditor, style.slotTranslator}) {
        Element sub;
        sub.kind = Kind::Names;
        sub.names = std::make_unique<NamesSpec>();
        sub.names->variables = {style.variables[slot]};
        sub.names->vars = {slot};
        spec.substitute.push_back(std::move(sub));
    }
    eng.authorFallback = std::move(fallback);
}

// The style's engine for the language, built once for the locale texts the
// source gives and kept with the style while they stay the same
std::shared_ptr<const Engine> engineFor(const Style& style, const LocaleSource& locales, const Options& options) {
    std::string lang = languageOf(style, options);
    std::vector<std::string> tags = fallbackTags(lang);
    std::vector<std::string> texts;
    std::vector<uint64_t> sources;
    for (const std::string& tag : tags) {
        texts.push_back(locales ? locales(tag) : std::string());
        sources.push_back(texts.back().empty() ? 0 : hashText(texts.back()) ^ texts.back().size());
    }
    {
        std::lock_guard<std::mutex> guard(style.engineLock);
        for (const auto& engine : style.engines) {
            if (engine->lang == lang && engine->sources == sources) return engine;
        }
    }
    auto engine = std::make_shared<Engine>();
    buildEngine(*engine, style, lang, tags, texts);
    engine->sources = std::move(sources);
    std::lock_guard<std::mutex> guard(style.engineLock);
    if (style.engines.size() >= 4) style.engines.erase(style.engines.begin());
    style.engines.push_back(engine);
    return engine;
}

// Sort keys, each worked out the first time a comparison reaches it: most
// comparisons are settled by the first key
class SortKeys {
public:
    SortKeys(const Engine& eng, const std::vector<ItemState>& states, const std::vector<SortKey>& keys,
             bool bibliography)
        : eng(eng), states(states), keys(keys), bibliography(bibliography), values(states.size() * keys.size()),
          done(values.size(), 0) {}

    int compare(size_t a, size_t b) {
        for (size_t k = 0; k < keys.size(); ++k) {
            const std::string& x = value(a, k);
            const std::string& y = value(b, k);
            if (x == y) continue;
            // Items without a value come last, either way
            if (x.empty()) return 1;
            if (y.empty()) return -1;
            int c = x < y ? -1 : 1;
            return keys[k].descending ? -c : c;
        }
        return 0;
    }

private:
    const Engine& eng;
    const std::vector<ItemState>& states;
    const std::vector<SortKey>& keys;
    bool bibliography;
    std::vector<std::string> values;
    std::vector<uint8_t> done;

    const std::string& value(size_t state, size_t k) {
        size_t at = state * keys.size() + k;
        if (!done[at]) {
            const SortKey& key = keys[k];
            RenderJob job;
            job.engine = &eng;
            job.state = &states[state];
            job.bibliography = bibliography;
            job.sorting = true;
            job.namesMin = key.namesMin;
            job.namesUseFirst = key.namesUseFirst;
            job.namesUseLast = key.namesUseLast;
            values[at] = sortValue(job, key);
            done[at] = 1;
        }
        return values[at];
    }
};

// a, b ... z, aa, ab ...
std::string suffixLetters(int k) {
    std::string out;
    ++k;
    while (k > 0) {
        --k;
        out.insert(out.begin(), (char)('a' + k % 26));
        k /= 26;
    }
    return out;
}

int suffixNumber(const std::string& letters) {
    int n = 0;
    for (char c : letters) n = n * 26 + (c - 'a' + 1);
    return n;
}

std::string personKey(const bib::Name& n) {
    return n.family + '\x1F' + n.given + '\x1F' + n.nonDroppingParticle + '\x1F' + n.droppingParticle + '\x1F' +
           n.suffix;
}

size_t listLength(const bib::Item& item) {
    size_t longest = 0;
    for (const auto& entry : item.names) longest = std::max(longest, entry.second.size());
    return std::min<size_t>(longest, 64);
}

// Disambiguation as CSL orders it: names hidden by et al., then given
// names, then year suffixes, then disambiguate="true"
void disambiguate(const Engine& eng, const Style& style, std::vector<ItemState>& states) {
    size_t n = states.size();
    if (n < 2 || !(style.addNames || style.addGivenname || style.addYearSuffix || style.testsDisambiguate)) return;
    // With a global given-name rule every cite's names count, so the first
    // rendering records them
    bool global = style.addGivenname && style.givenRule != ByCite;
    std::vector<std::string> text(n);
    std::vector<std::vector<NameRecord>> records(n);
    CiteState first;
    // Rendered as a cite without a locator renders, so citations can reuse it
    uint16_t flags = style.citation.fmt.apply(0);
    bool grouping = style.collapse == CollapseYear || style.collapse == CollapseYearSuffix ||
                    style.collapse == CollapseYearSuffixRanged || style.hasCiteGroupDelimiter;
    auto renderOne = [&](size_t i, bool withRecords) {
        ItemState& state = states[i];
        RenderJob job;
        job.engine = &eng;
        job.state = &state;
        job.cite = &first;
        job.namesMode = grouping ? NamesMark : NamesKeep;
        if (withRecords) {
            records[i].clear();
            job.records = &records[i];
        }
        state.citePositional = false;
        job.positional = &state.citePositional;
        state.cite.clear();
        render(job, style.citation.children, flags, state.cite);
        state.citeValid = true;
        text[i] = plainText(state.cite);
    };
    for (size_t i = 0; i < n; ++i) renderOne(i, global);
    // Items whose cites read the same, in groups, each in bibliography order
    auto ambiguous = [&]() {
        std::vector<size_t> byText(n);
        std::iota(byText.begin(), byText.end(), 0);
        std::sort(byText.begin(), byText.end(), [&](size_t a, size_t b) {
            int c = text[a].compare(text[b]);
            return c != 0 ? c < 0 : states[a].order < states[b].order;
        });
        std::vector<std::vector<size_t>> groups;
        for (size_t i = 0; i < n;) {
            size_t j = i + 1;
            while (j < n && text[byText[j]] == text[byText[i]]) ++j;
            if (j - i >= 2) groups.emplace_back(byText.begin() + i, byText.begin() + j);
            i = j;
        }
        std::sort(groups.begin(), groups.end(),
                  [&](const auto& a, const auto& b) { return states[a[0]].order < states[b[0]].order; });
        return groups;
    };
    auto unique = [&](const std::vector<size_t>& group) {
        size_t count = 0;
        for (size_t i : group) {
            size_t same = 0;
            for (size_t j : group) same += text[j] == text[i];
            count += same == 1;
        }
        return count;
    };
    // Shows ambiguous names fuller; returns the items that changed
    auto expand = [&](const std::vector<size_t>& members, bool primaryOnly, int maxLevel) {
        struct Occurrence {
            size_t state;
            const NameRecord* record;
            std::string person;
            int level;
        };
        std::vector<Occurrence> occurrences;
        for (size_t i : members) {
            for (const NameRecord& r : records[i]) {
                if (primaryOnly && !r.primary) continue;
                occurrences.push_back({i, &r, personKey(*r.name), 0});
            }
        }
        for (int level = 0; level < maxLevel; ++level) {
            std::unordered_map<std::string, const std::string*> owner;
            std::unordered_set<std::string> clash;
            for (const Occurrence& o : occurrences) {
                if (o.level != level) continue;
                auto found = owner.emplace(o.record->forms[level], &o.person);
                if (!found.second && *found.first->second != o.person) clash.insert(o.record->forms[level]);
            }
            if (clash.empty()) break;
            for (Occurrence& o : occurrences) {
                if (o.level == level && clash.count(o.record->forms[level])) o.level = level + 1;
            }
        }
        std::vector<size_t> changed;
        for (const Occurrence& o : occurrences) {
            if (o.level == 0) continue;
            auto& list = states[o.state].expand;
            bool updated = false;
            for (auto& entry : list) {
                if (entry.first == o.record->name) {
                    entry.second = (uint8_t)std::max<int>(entry.second, o.level);
                    updated = true;
                }
            }
            if (!updated) list.emplace_back(o.record->name, (uint8_t)o.level);
            if (changed.empty() || changed.back() != o.state) changed.push_back(o.state);
        }
        return changed;
    };
    std::vector<std::vector<size_t>> groups = ambiguous();
    if (style.addNames) {
        for (const auto& group : groups) {
            size_t most = 0;
            for (size_t i : group) most = std::max(most, listLength(*states[i].item));
            int best = 0;
            size_t bestUnique = unique(group);
            for (int k = 1; k <= (int)most && bestUnique < group.size(); ++k) {
                for (size_t i : group) {
                    states[i].addNames = k;
                    renderOne(i, global);
                }
                size_t count = unique(group);
                if (count > bestUnique) {
                    best = k;
                    bestUnique = count;
                }
            }
            for (size_t i : group) {
                states[i].addNames = best;
                renderOne(i, global);
            }
        }
        groups = ambiguous();
    }
    if (style.addGivenname) {
        uint8_t rule = style.givenRule;
        int maxLevel = rule == AllNamesWithInitials || rule == PrimaryNameWithInitials ? 1 : 2;
        bool primaryOnly = rule == PrimaryName || rule == PrimaryNameWithInitials;
        if (rule == ByCite) {
            for (const auto& group : groups) {
                for (size_t i : group) renderOne(i, true);
                expand(group, false, maxLevel);
                for (size_t i : group) renderOne(i, false);
            }
        } else {
            // Names are told apart wherever they are ambiguous, cites or not
            std::vector<size_t> all(n);
            std::iota(all.begin(), all.end(), 0);
            for (size_t i : expand(all, primaryOnly, maxLevel)) renderOne(i, false);
        }
        groups = ambiguous();
    }
    if (style.addYearSuffix) {
        for (const auto& group : groups) {
            for (size_t k = 0; k < group.size(); ++k) {
                states[group[k]].yearSuffix = suffixLetters((int)k);
                renderOne(group[k], false);
            }
        }
        groups = ambiguous();
    }
    for (const auto& group : groups) {
        for (size_t i : group) {
            states[i].disambiguate = true;
            if (style.testsDisambiguate) renderOne(i, false);
        }
    }
}

struct CiteEntry {
    const cite::Item* item = nullptr;
    ItemState* state = nullptr;  // null when the library lacks the key
    CiteState cite;
    Pieces out;
    std::string author;  // the first names' text, for cite grouping
};

void wrapCite(const cite::Item& item, uint16_t flags, Pieces& out, bool punctuationInQuote) {
    if (!item.prefix.empty()) {
        std::string prefix = item.prefix;
        char last = prefix.back();
        if (last != ' ' && last != '(' && last != '[') prefix += ' ';
        Pieces whole;
        whole.emplace_back();
        whole.back().text = prefix;
        whole.back().flags = flags;
        join(whole, out, punctuationInQuote);
        out.swap(whole);
    }
    if (!item.suffix.empty()) {
        std::string suffix = item.suffix;
        char first = suffix.front();
        bool attached = first == ' ' || first == ',' || first == '.' || first == ';' || first == ':' || first == ')' ||
                        first == ']' || first == '!' || first == '?';
        if (!attached) suffix.insert(suffix.begin(), ' ');
        addText(out, suffix, flags, punctuationInQuote);
    }
}

std::string firstNamesText(const Pieces& pieces) {
    std::string text;
    for (const Piece& p : pieces) {
        if (p.mark & FirstNames) text += p.text;
    }
    return text;
}

Rich formatCitation(const Engine& eng, const cite::Citation& citation, std::vector<CiteEntry>& entries) {
    const Style& style = *eng.style;
    const Layout& layout = style.citation;
    bool piq = eng.punctuationInQuote;
    uint16_t flags = layout.fmt.apply(0);
    bool yearCollapse = style.collapse == CollapseYear || style.collapse == CollapseYearSuffix ||
                        style.collapse == CollapseYearSuffixRanged;
    bool grouping = yearCollapse || style.hasCiteGroupDelimiter;
    auto renderCite = [&](CiteEntry& e, uint8_t mode, Pieces& out) {
        RenderJob job;
        job.engine = &eng;
        job.state = e.state;
        job.cite = &e.cite;
        job.namesMode = mode;
        render(job, layout.children, flags, out);
    };
    // The author of an in-text cite stands outside the citation: "Doe (2024)"
    Pieces author;
    bool inText = citation.authorInText && !entries.empty() && entries[0].state;
    if (inText) {
        RenderJob job;
        job.engine = &eng;
        job.state = entries[0].state;
        job.cite = &entries[0].cite;
        job.namesMode = NamesCapture;
        job.captured = &author;
        Pieces rest;
        render(job, layout.children, 0, rest);
        if (author.empty()) {
            RenderJob plainJob;
            plainJob.engine = &eng;
            plainJob.state = entries[0].state;
            plainJob.cite = &entries[0].cite;
            renderElement(plainJob, *eng.authorFallback, 0, author);
        }
    }
    bool anyFound = false;
    for (size_t i = 0; i < entries.size(); ++i) {
        CiteEntry& e = entries[i];
        if (!e.state) {
            // Plain, whatever the style's formatting: it is a problem to see
            e.out.emplace_back();
            e.out.back().text = "@" + e.item->id + "?";
            e.out.back().flags = Missing;
            continue;
        }
        anyFound = true;
        bool suppress = e.item->suppressAuthor || (inText && i == 0);
        // A cite without a locator reads as disambiguation rendered it
        bool reuse = !suppress && e.item->locator.empty() && e.state->citeValid &&
                     (!e.state->citePositional || e.cite.position == First);
        if (reuse) e.out = e.state->cite;
        else renderCite(e, suppress ? NamesSuppress : grouping ? NamesMark : NamesKeep, e.out);
        if (grouping && !suppress) e.author = firstNamesText(e.out);
    }
    auto wrapped = [&](CiteEntry& e) {
        Pieces out;
        out.swap(e.out);
        wrapCite(*e.item, flags, out, piq);
        return out;
    };
    std::string afterCollapse = style.hasAfterCollapseDelimiter ? style.afterCollapseDelimiter : layout.delimiter;
    std::vector<std::pair<Pieces, bool>> units;  // with whether it collapsed
    size_t n = entries.size();
    if (style.collapse == CollapseNumber) {
        auto collapsible = [&](size_t k) {
            const CiteEntry& e = entries[k];
            return e.state && e.item->locator.empty() && e.item->prefix.empty() && e.item->suffix.empty() &&
                   !(inText && k == 0);
        };
        for (size_t i = 0; i < n;) {
            size_t j = i;
            if (collapsible(i)) {
                while (j + 1 < n && collapsible(j + 1) && entries[j + 1].state->number == entries[j].state->number + 1) ++j;
            }
            if (j >= i + 2) {
                Pieces unit = wrapped(entries[i]);
                addText(unit, "\xE2\x80\x93", flags, piq);
                Pieces end = wrapped(entries[j]);
                join(unit, end, piq);
                units.emplace_back(std::move(unit), true);
                i = j + 1;
            } else {
                units.emplace_back(wrapped(entries[i]), false);
                ++i;
            }
        }
    } else if (grouping) {
        // Cites with the same names sit together, the later ones without them
        std::vector<size_t> sequence;
        std::vector<bool> used(n, false);
        for (size_t i = 0; i < n; ++i) {
            if (used[i]) continue;
            sequence.push_back(i);
            used[i] = true;
            if (entries[i].author.empty()) continue;
            for (size_t j = i + 1; j < n; ++j) {
                if (!used[j] && entries[j].author == entries[i].author) {
                    sequence.push_back(j);
                    used[j] = true;
                }
            }
        }
        std::string suffixDelimiter = style.hasYearSuffixDelimiter ? style.yearSuffixDelimiter : layout.delimiter;
        bool bySuffix = style.collapse == CollapseYearSuffix || style.collapse == CollapseYearSuffixRanged;
        auto year = [](const CiteEntry& e) {
            const bib::Date* d = e.state->item->date("issued");
            return d ? d->from.year : 0;
        };
        for (size_t a = 0; a < sequence.size();) {
            const std::string& key = entries[sequence[a]].author;
            size_t b = a + 1;
            if (!key.empty()) {
                while (b < sequence.size() && entries[sequence[b]].author == key) ++b;
            }
            if (b - a == 1) {
                units.emplace_back(wrapped(entries[sequence[a]]), false);
                a = b;
                continue;
            }
            Pieces unit = wrapped(entries[sequence[a]]);
            size_t k = a + 1;
            while (k < b) {
                CiteEntry& e = entries[sequence[k]];
                const CiteEntry& previous = entries[sequence[k - 1]];
                auto suffixOnly = [&](const CiteEntry& x, const CiteEntry& before) {
                    return bySuffix && !x.state->yearSuffix.empty() && !before.state->yearSuffix.empty() &&
                           year(x) == year(before) && year(x) != 0 && x.item->locator.empty() &&
                           before.item->locator.empty() && x.item->prefix.empty() && x.item->suffix.empty();
                };
                if (suffixOnly(e, previous)) {
                    // A run of three or more suffixes in a row: 2020a to c
                    size_t run = k;
                    while (style.collapse == CollapseYearSuffixRanged && run + 1 < b &&
                           suffixOnly(entries[sequence[run + 1]], entries[sequence[run]]) &&
                           suffixNumber(entries[sequence[run + 1]].state->yearSuffix) ==
                               suffixNumber(entries[sequence[run]].state->yearSuffix) + 1) {
                        ++run;
                    }
                    bool ranged = run > k && suffixNumber(e.state->yearSuffix) == suffixNumber(previous.state->yearSuffix) + 1;
                    if (ranged) {
                        addText(unit, "\xE2\x80\x93", flags, piq);
                        addText(unit, entries[sequence[run]].state->yearSuffix, flags, piq);
                        k = run + 1;
                        continue;
                    }
                    addText(unit, suffixDelimiter, flags, piq);
                    addText(unit, e.state->yearSuffix, flags, piq);
                    ++k;
                    continue;
                }
                Pieces again;
                renderCite(e, NamesSuppress, again);
                wrapCite(*e.item, flags, again, piq);
                addText(unit, style.citeGroupDelimiter, flags, piq);
                join(unit, again, piq);
                ++k;
            }
            units.emplace_back(std::move(unit), true);
            a = b;
        }
    } else {
        for (CiteEntry& e : entries) units.emplace_back(wrapped(e), false);
    }
    Pieces body;
    for (size_t k = 0; k < units.size(); ++k) {
        if (k > 0) addText(body, units[k - 1].second ? afterCollapse : layout.delimiter, flags, piq);
        join(body, units[k].first, piq);
    }
    if (!body.empty() && anyFound) {
        Pieces whole;
        if (!layout.fmt.prefix.empty()) {
            whole.emplace_back();
            whole.back().text = layout.fmt.prefix;
            whole.back().flags = flags;
        }
        join(whole, body, piq);
        addText(whole, layout.fmt.suffix, flags, piq);
        body.swap(whole);
    }
    if (inText) {
        Pieces whole = std::move(author);
        if (!body.empty()) {
            if (!whole.empty()) addText(whole, " ", 0, piq);
            join(whole, body, piq);
        }
        body.swap(whole);
    }
    return toRich(body);
}

// Repeats of the previous entry's names, replaced as the style asks
void substituteAuthors(const Style& style, std::vector<Pieces>& texts) {
    std::string previous;
    std::vector<std::string> previousNames;
    for (Pieces& text : texts) {
        std::string whole = firstNamesText(text);
        std::vector<std::string> names;
        for (const Piece& p : text) {
            if (!(p.mark & FirstNames) || p.name == 0) continue;
            if (names.size() < p.name) names.resize(p.name);
            names[p.name - 1] += p.text;
        }
        if (!whole.empty() && !previous.empty()) {
            std::vector<bool> replace(names.size(), false);
            bool all = false;
            switch (style.authorSubstituteRule) {
                case CompleteAll: all = whole == previous; break;
                case CompleteEach:
                    if (whole == previous) std::fill(replace.begin(), replace.end(), true);
                    break;
                case PartialEach:
                    for (size_t i = 0; i < names.size() && i < previousNames.size() && names[i] == previousNames[i]; ++i) {
                        replace[i] = true;
                    }
                    break;
                case PartialFirst:
                    if (!names.empty() && !previousNames.empty() && names[0] == previousNames[0]) replace[0] = true;
                    break;
            }
            Pieces out;
            bool placed = false;
            uint8_t current = 0;
            for (Piece& p : text) {
                bool inNames = (p.mark & FirstNames) != 0;
                if (all && inNames) {
                    if (!placed) {
                        Piece substitute;
                        substitute.text = style.authorSubstitute;
                        substitute.flags = p.flags;
                        out.push_back(std::move(substitute));
                        placed = true;
                    }
                    continue;
                }
                if (inNames && p.name > 0 && replace[p.name - 1]) {
                    if (p.name != current) {
                        Piece substitute;
                        substitute.text = style.authorSubstitute;
                        substitute.flags = p.flags;
                        out.push_back(std::move(substitute));
                        current = p.name;
                    }
                    continue;
                }
                current = 0;
                out.push_back(std::move(p));
            }
            text.swap(out);
        }
        previous = std::move(whole);
        previousNames = std::move(names);
    }
}
}

Result process(const Style& style, const LocaleSource& locales, const bib::Library& library,
               const std::vector<cite::Citation>& citations, const Options& options) {
    Result result;
    std::shared_ptr<const Engine> engine = engineFor(style, locales, options);
    const Engine& eng = *engine;
    result.warnings = eng.warnings;
    bool piq = eng.punctuationInQuote;

    // The items, cited ones first in the order they are first cited
    // At most every item of the library: sized once, never moved
    size_t cites = 0;
    for (const cite::Citation& c : citations) cites += c.items.size();
    bool everything = std::find(options.nocite.begin(), options.nocite.end(), "*") != options.nocite.end();
    size_t most = std::min(library.items.size(), everything ? library.items.size() : cites + options.nocite.size());
    std::vector<ItemState> states;
    states.reserve(most);
    std::unordered_map<const bib::Item*, int> indexOf;
    indexOf.reserve(most);
    auto add = [&](const bib::Item* item, bool cited) {
        auto found = indexOf.emplace(item, (int)states.size());
        if (found.second) {
            states.emplace_back();
            states.back().item = item;
        }
        if (cited) states[found.first->second].cited = true;
        return found.first->second;
    };
    std::unordered_set<std::string> warned;
    auto missing = [&](const std::string& key) {
        if (warned.insert(key).second) result.warnings.push_back("no reference for @" + key);
    };
    std::vector<std::vector<int>> stateOf(citations.size());
    for (size_t c = 0; c < citations.size(); ++c) {
        for (const cite::Item& ci : citations[c].items) {
            const bib::Item* item = library.find(ci.id);
            if (!item) missing(ci.id);
            stateOf[c].push_back(item ? add(item, true) : -1);
        }
    }
    for (const std::string& listed : options.nocite) {
        std::string key = !listed.empty() && listed[0] == '@' ? listed.substr(1) : listed;
        if (key == "*") {
            for (const bib::Item& item : library.items) add(&item, false);
            continue;
        }
        if (const bib::Item* item = library.find(key)) add(item, false);
        else if (!key.empty()) missing(key);
    }

    // Numbers follow the bibliography's sort, or first citation without one
    size_t count = states.size();
    for (size_t i = 0; i < count; ++i) {
        fillValues(style, states[i]);
        states[i].number = (int)i + 1;
        states[i].numberText = std::to_string(i + 1);
    }
    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    if (style.hasBibliography && !style.bibliographySort.empty()) {
        SortKeys keys(eng, states, style.bibliographySort, true);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return keys.compare(a, b) < 0; });
        bool byNumber = false;
        for (const SortKey& key : style.bibliographySort) byNumber = byNumber || key.variable == "citation-number";
        if (!byNumber) {
            for (size_t k = 0; k < count; ++k) {
                states[order[k]].number = (int)k + 1;
                states[order[k]].numberText = std::to_string(k + 1);
            }
        }
    }
    for (size_t k = 0; k < count; ++k) states[order[k]].order = (int)k;

    // Cites in each citation, in the citation's sort order
    SortKeys citeKeys(eng, states, style.citationSort, false);
    std::vector<std::vector<size_t>> sorted(citations.size());
    for (size_t c = 0; c < citations.size(); ++c) {
        std::vector<size_t>& list = sorted[c];
        for (size_t k = 0; k < citations[c].items.size(); ++k) {
            if (stateOf[c][k] >= 0) list.push_back(k);
        }
        if (!style.citationSort.empty() && list.size() > 1) {
            std::stable_sort(list.begin(), list.end(), [&](size_t a, size_t b) {
                return citeKeys.compare((size_t)stateOf[c][a], (size_t)stateOf[c][b]) < 0;
            });
        }
        for (size_t k = 0; k < citations[c].items.size(); ++k) {
            if (stateOf[c][k] < 0) list.push_back(k);
        }
    }

    // Positions, in document order
    std::vector<std::vector<CiteState>> positions(citations.size());
    {
        std::vector<int> lastCitation(count, -1);
        int previousState = -1;
        const cite::Item* previousCite = nullptr;
        size_t previousSize = 0;
        int previousCitation = -1;
        for (size_t c = 0; c < citations.size(); ++c) {
            const std::vector<size_t>& list = sorted[c];
            positions[c].resize(list.size());
            size_t found = 0;
            for (size_t k : list) found += stateOf[c][k] >= 0;
            for (size_t p = 0; p < list.size(); ++p) {
                const cite::Item& ci = citations[c].items[list[p]];
                int s = stateOf[c][list[p]];
                CiteState& cs = positions[c][p];
                cs.cite = &ci;
                if (s < 0) {
                    previousState = -1;
                    continue;
                }
                if (lastCitation[s] < 0) {
                    cs.position = First;
                } else {
                    bool ibid = previousState == s &&
                                (p > 0 || (previousCitation == (int)c - 1 && previousSize == 1));
                    if (!ibid) {
                        cs.position = Subsequent;
                    } else if (ci.locator.empty()) {
                        cs.position = previousCite->locator.empty() ? Ibid : Subsequent;
                    } else {
                        cs.position = ci.locator == previousCite->locator && ci.label == previousCite->label
                                          ? Ibid
                                          : IbidWithLocator;
                    }
                    cs.nearNote = (int)c - lastCitation[s] <= style.nearNoteDistance;
                }
                lastCitation[s] = (int)c;
                previousState = s;
                previousCite = &ci;
                previousCitation = (int)c;
                previousSize = found;
            }
        }
    }

    disambiguate(eng, style, states);

    result.citations.reserve(citations.size());
    for (size_t c = 0; c < citations.size(); ++c) {
        std::vector<CiteEntry> entries(sorted[c].size());
        for (size_t p = 0; p < sorted[c].size(); ++p) {
            entries[p].item = &citations[c].items[sorted[c][p]];
            int s = stateOf[c][sorted[c][p]];
            entries[p].state = s >= 0 ? &states[s] : nullptr;
            entries[p].cite = positions[c][p];
        }
        result.citations.push_back(formatCitation(eng, citations[c], entries));
    }

    if (style.hasBibliography) {
        Bibliography& bibliography = result.bibliography;
        bibliography.hangingIndent = style.hangingIndent;
        bibliography.alignField = style.alignField;
        bibliography.lineSpacing = style.lineSpacing;
        bibliography.entrySpacing = style.entrySpacing;
        const Layout& layout = style.bibliography;
        uint16_t flags = layout.fmt.apply(0);
        std::vector<Pieces> texts, fields;
        std::vector<const bib::Item*> items;
        texts.reserve(count);
        fields.reserve(count);
        items.reserve(count);
        for (size_t k = 0; k < count; ++k) {
            const ItemState& state = states[order[k]];
            RenderJob job;
            job.engine = &eng;
            job.state = &state;
            job.bibliography = true;
            job.namesMode = style.hasAuthorSubstitute ? NamesMark : NamesKeep;
            Pieces field, text;
            if (style.alignField) renderSplit(job, layout.children, flags, field, text);
            else render(job, layout.children, flags, text);
            if (field.empty() && text.empty()) continue;
            prependText(field.empty() ? text : field, layout.fmt.prefix, flags);
            addText(text.empty() ? field : text, layout.fmt.suffix, flags, piq);
            texts.push_back(std::move(text));
            fields.push_back(std::move(field));
            items.push_back(state.item);
        }
        if (style.hasAuthorSubstitute) substituteAuthors(style, texts);
        bibliography.entries.reserve(texts.size());
        for (size_t k = 0; k < texts.size(); ++k) {
            Entry entry;
            entry.id = items[k]->id;
            entry.text = toRich(texts[k]);
            entry.field = toRich(fields[k]);
            bibliography.entries.push_back(std::move(entry));
        }
    }
    return result;
}
}
