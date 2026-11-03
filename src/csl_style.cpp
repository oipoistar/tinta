// CSL styles and locales read from their XML (csl.h) into the element trees
// csl_render.cpp walks.

#pragma optimize("s", on)

#include "csl_internal.h"
#include "xml.h"

#include <cstdlib>
#include <functional>
#include <unordered_set>

namespace csl {

namespace {
const char* const nameVariables[] = {
    "author", "chair", "collection-editor", "compiler", "composer", "container-author", "contributor",
    "curator", "director", "editor", "editor-translator", "editorial-director", "executive-producer",
    "guest", "host", "illustrator", "interviewer", "narrator", "organizer", "original-author",
    "performer", "producer", "recipient", "reviewed-author", "script-writer", "series-creator", "translator"};
const char* const dateVariables[] = {"accessed", "available-date", "event-date", "issued", "original-date", "submitted"};
}

bool isNameVariable(std::string_view v) {
    for (const char* name : nameVariables) {
        if (v == name) return true;
    }
    return false;
}

bool isDateVariable(std::string_view v) {
    for (const char* name : dateVariables) {
        if (v == name) return true;
    }
    return false;
}

void fillValues(const Style& style, ItemState& state) {
    state.values.assign(style.variables.size(), nullptr);
    const bib::Item& item = *state.item;
    auto type = style.typeIndex.find(item.type);
    state.type = type == style.typeIndex.end() ? -1 : type->second;
    auto slot = [&](const std::string& name, uint8_t kind) {
        auto it = style.variableIndex.find(name);
        return it != style.variableIndex.end() && style.variableKinds[it->second] == kind ? it->second : -1;
    };
    for (const auto& field : item.fields) {
        int s = field.second.empty() ? -1 : slot(field.first, VarStandard);
        if (s >= 0) state.values[s] = &field.second;
    }
    for (const auto& list : item.names) {
        int s = list.second.empty() ? -1 : slot(list.first, VarName);
        if (s >= 0) state.values[s] = &list.second;
    }
    for (const auto& date : item.dates) {
        int s = date.second.empty() ? -1 : slot(date.first, VarDate);
        if (s >= 0) state.values[s] = &date.second;
    }
    // Fields are rich text, entities and all: links want the plain text
    if (const auto* url = static_cast<const std::string*>(state.values[style.slotURL])) {
        state.urlLink = stripMarkup(*url);
    }
    if (const auto* doi = static_cast<const std::string*>(state.values[style.slotDOI])) {
        std::string plain = stripMarkup(*doi);
        state.doiLink = startsWith(plain, "http") ? plain : "https://doi.org/" + plain;
    }
}

std::string termKey(std::string_view name, Form form, uint8_t genderForm) {
    std::string key(name);
    key += '\x1F';
    key += (char)('0' + (int)form);
    key += (char)('0' + genderForm);
    return key;
}

uint64_t termHash(std::string_view name, Form form, uint8_t genderForm) {
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](unsigned char c) {
        hash ^= c;
        hash *= 1099511628211ull;
    };
    for (char c : name) mix((unsigned char)c);
    mix(0x1F);
    mix((unsigned char)('0' + (int)form));
    mix((unsigned char)('0' + genderForm));
    return hash;
}

void NameOptions::inherit(const NameOptions& outer) {
    if (!(has & HasAnd)) andMode = outer.andMode;
    if (!(has & HasPrecedesEtAl)) precedesEtAl = outer.precedesEtAl;
    if (!(has & HasPrecedesLast)) precedesLast = outer.precedesLast;
    if (!(has & HasEtAlMin)) etAlMin = outer.etAlMin;
    if (!(has & HasEtAlUseFirst)) etAlUseFirst = outer.etAlUseFirst;
    if (!(has & HasSubsequentMin)) subsequentMin = outer.subsequentMin;
    if (!(has & HasSubsequentUseFirst)) subsequentUseFirst = outer.subsequentUseFirst;
    if (!(has & HasEtAlUseLast)) etAlUseLast = outer.etAlUseLast;
    if (!(has & HasInitialize)) initialize = outer.initialize;
    if (!(has & HasInitializeWith)) initializeWith = outer.initializeWith;
    if (!(has & HasSortOrder)) sortOrder = outer.sortOrder;
    if (!(has & HasSortSeparator)) sortSeparator = outer.sortSeparator;
    if (!(has & HasForm)) form = outer.form;
    if (!(has & HasDelimiter)) delimiter = outer.delimiter;
    has |= outer.has;
}

const Term* Engine::term(std::string_view name, Form form, uint8_t genderForm) const {
    // A missing form falls back: verb-short to verb to long, symbol to short
    // to long, short to long
    Form chain[3];
    int count = 0;
    chain[count++] = form;
    if (form == Form::VerbShort) chain[count++] = Form::Verb;
    if (form == Form::Symbol) chain[count++] = Form::Short;
    if (form != Form::Long) chain[count++] = Form::Long;
    auto find = [&](Form f, uint8_t gender) -> const Term* {
        auto it = terms.find(termHash(name, f, gender));
        if (it == terms.end()) return nullptr;
        const std::string& key = *it->second.key;
        bool same = key.size() == name.size() + 3 && key.compare(0, name.size(), name) == 0 &&
                    key[name.size() + 1] == (char)('0' + (int)f) && key[name.size() + 2] == (char)('0' + gender);
        return same ? it->second.term : nullptr;
    };
    for (int i = 0; i < count; ++i) {
        if (genderForm) {
            if (const Term* t = find(chain[i], genderForm)) return t;
        }
        if (const Term* t = find(chain[i], 0)) return t;
    }
    return nullptr;
}

const std::string& Engine::termText(std::string_view name, Form form, bool plural) const {
    static const std::string none;
    const Term* found = term(name, form);
    if (!found) return none;
    return plural && found->hasMultiple ? found->multiple : found->single;
}

namespace {

std::vector<std::string> words(std::string_view text) {
    std::vector<std::string> list;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        size_t begin = i;
        while (i < text.size() && text[i] != ' ') ++i;
        if (i > begin) list.emplace_back(text.substr(begin, i - begin));
    }
    return list;
}

int number(std::string_view text, int fallback) {
    std::string value(text);
    char* end = nullptr;
    long parsed = std::strtol(value.c_str(), &end, 10);
    return end && end != value.c_str() ? (int)parsed : fallback;
}

Form readForm(std::string_view text) {
    if (text == "short") return Form::Short;
    if (text == "verb") return Form::Verb;
    if (text == "verb-short") return Form::VerbShort;
    if (text == "symbol") return Form::Symbol;
    return Form::Long;
}

uint8_t readTextCase(std::string_view text) {
    if (text == "lowercase") return Lowercase;
    if (text == "uppercase") return Uppercase;
    if (text == "capitalize-first") return CapitalizeFirst;
    if (text == "capitalize-all") return CapitalizeAll;
    if (text == "sentence") return Sentence;
    if (text == "title") return TitleCase;
    return CaseNone;
}

Formatting readFormatting(const xml::Node& node) {
    Formatting f;
    f.prefix = std::string(node.get("prefix"));
    f.suffix = std::string(node.get("suffix"));
    std::string_view value = node.get("font-style");
    if (value == "italic" || value == "oblique") f.flip |= Italic;
    else if (value == "normal") f.clear |= Italic;
    value = node.get("font-variant");
    if (value == "small-caps") f.flip |= SmallCaps;
    else if (value == "normal") f.clear |= SmallCaps;
    value = node.get("font-weight");
    if (value == "bold") f.set |= Bold;
    else if (value == "normal" || value == "light") f.clear |= Bold;
    value = node.get("text-decoration");
    if (value == "underline") f.set |= Underline;
    else if (value == "none") f.clear |= Underline;
    value = node.get("vertical-align");
    if (value == "sup") {
        f.set |= Superscript;
        f.clear |= Subscript;
    } else if (value == "sub") {
        f.set |= Subscript;
        f.clear |= Superscript;
    } else if (value == "baseline") {
        f.clear |= Superscript | Subscript;
    }
    f.textCase = readTextCase(node.get("text-case"));
    f.stripPeriods = node.get("strip-periods") == "true";
    f.quotes = node.get("quotes") == "true";
    return f;
}

uint8_t readPrecedes(std::string_view text) {
    if (text == "after-inverted-name") return AfterInvertedName;
    if (text == "always") return Always;
    if (text == "never") return Never;
    return Contextual;
}

// The inheritable name options an element sets. On <style>, <citation> and
// <bibliography> the name's form and delimiter are name-form and
// name-delimiter
void readNameOptions(const xml::Node& node, NameOptions& o, bool inheritable) {
    if (const std::string* v = node.attribute("and")) {
        o.has |= NameOptions::HasAnd;
        o.andMode = *v == "text" ? 1 : *v == "symbol" ? 2 : 0;
    }
    if (const std::string* v = node.attribute("delimiter-precedes-et-al")) {
        o.has |= NameOptions::HasPrecedesEtAl;
        o.precedesEtAl = readPrecedes(*v);
    }
    if (const std::string* v = node.attribute("delimiter-precedes-last")) {
        o.has |= NameOptions::HasPrecedesLast;
        o.precedesLast = readPrecedes(*v);
    }
    if (const std::string* v = node.attribute("et-al-min")) {
        o.has |= NameOptions::HasEtAlMin;
        o.etAlMin = number(*v, 0);
    }
    if (const std::string* v = node.attribute("et-al-use-first")) {
        o.has |= NameOptions::HasEtAlUseFirst;
        o.etAlUseFirst = number(*v, 0);
    }
    if (const std::string* v = node.attribute("et-al-subsequent-min")) {
        o.has |= NameOptions::HasSubsequentMin;
        o.subsequentMin = number(*v, 0);
    }
    if (const std::string* v = node.attribute("et-al-subsequent-use-first")) {
        o.has |= NameOptions::HasSubsequentUseFirst;
        o.subsequentUseFirst = number(*v, 0);
    }
    if (const std::string* v = node.attribute("et-al-use-last")) {
        o.has |= NameOptions::HasEtAlUseLast;
        o.etAlUseLast = *v == "true";
    }
    if (const std::string* v = node.attribute("initialize")) {
        o.has |= NameOptions::HasInitialize;
        o.initialize = *v != "false";
    }
    if (const std::string* v = node.attribute("initialize-with")) {
        o.has |= NameOptions::HasInitializeWith;
        o.initializeWith = *v;
    }
    if (const std::string* v = node.attribute("name-as-sort-order")) {
        o.has |= NameOptions::HasSortOrder;
        o.sortOrder = *v == "first" ? SortFirst : *v == "all" ? SortAll : SortNone;
    }
    if (const std::string* v = node.attribute("sort-separator")) {
        o.has |= NameOptions::HasSortSeparator;
        o.sortSeparator = *v;
    }
    if (const std::string* v = node.attribute(inheritable ? "name-form" : "form")) {
        o.has |= NameOptions::HasForm;
        o.form = *v == "short" ? NameShort : *v == "count" ? NameCount : NameLong;
    }
    if (const std::string* v = node.attribute(inheritable ? "name-delimiter" : "delimiter")) {
        o.has |= NameOptions::HasDelimiter;
        o.delimiter = *v;
    }
}

void readNameLevel(const xml::Node& node, NameLevel& level) {
    readNameOptions(node, level.options, true);
    if (const std::string* v = node.attribute("names-delimiter")) {
        level.namesDelimiter = *v;
        level.hasNamesDelimiter = true;
    }
}

DatePart readDatePart(const xml::Node& node) {
    DatePart part;
    std::string_view name = node.get("name");
    part.name = name == "month" ? PartMonth : name == "day" ? PartDay : PartYear;
    if (const std::string* form = node.attribute("form")) {
        part.hasForm = true;
        if (*form == "short") part.form = PartShort;
        else if (*form == "numeric") part.form = PartNumeric;
        else if (*form == "numeric-leading-zeros") part.form = PartLeadingZeros;
        else if (*form == "ordinal") part.form = PartOrdinal;
        else part.form = PartLong;
    } else {
        part.form = part.name == PartDay ? PartNumeric : PartLong;
    }
    if (const std::string* delimiter = node.attribute("range-delimiter")) {
        part.hasRangeDelimiter = true;
        part.rangeDelimiter = *delimiter;
    }
    part.fmt = readFormatting(node);
    return part;
}

struct Reader {
    std::string error;
    std::vector<std::pair<Element*, int>> macroCalls;  // resolved once every macro is read

    bool fail(int line, const std::string& what) {
        if (error.empty()) error = "line " + std::to_string(line) + ": " + what;
        return false;
    }

    bool children(const xml::Node& node, std::vector<Element>& out) {
        for (const xml::Node& child : node.children) {
            if (child.isText()) continue;
            Element e;
            bool known = false;
            if (!element(child, e, known)) return false;
            if (known) out.push_back(std::move(e));
        }
        return true;
    }

    bool branch(const xml::Node& node, Branch& b, bool otherwise) {
        if (!otherwise) {
            std::string_view match = node.get("match", "all");
            b.match = match == "any" ? MatchAny : match == "none" ? MatchNone : MatchAll;
            static const struct {
                const char* attribute;
                Test::Kind kind;
            } tests[] = {{"type", Test::Type},
                         {"variable", Test::Variable},
                         {"is-numeric", Test::IsNumeric},
                         {"is-uncertain-date", Test::IsUncertainDate},
                         {"locator", Test::Locator},
                         {"position", Test::Position}};
            for (const auto& t : tests) {
                if (const std::string* value = node.attribute(t.attribute)) {
                    for (std::string& word : words(*value)) b.tests.push_back({t.kind, std::move(word)});
                }
            }
            if (const std::string* value = node.attribute("disambiguate")) {
                b.tests.push_back({Test::Disambiguate, *value});
            }
            // A condition testing nothing never holds
            if (b.tests.empty()) b.tests.push_back({Test::Type, std::string()});
        }
        return children(node, b.children);
    }

    bool names(const xml::Node& node, Element& e) {
        e.kind = Kind::Names;
        e.names = std::make_unique<NamesSpec>();
        NamesSpec& spec = *e.names;
        spec.variables = words(node.get("variable"));
        // Name options on <names> itself, spelled as on <citation>: its own
        // delimiter is the one between variables
        readNameOptions(node, spec.level, true);
        for (const xml::Node& child : node.children) {
            if (child.isText()) continue;
            if (child.name == "name") {
                spec.hasName = true;
                readNameOptions(child, spec.name, false);
                spec.nameFmt = readFormatting(child);
                for (const xml::Node& part : child.children) {
                    if (part.name != "name-part") continue;
                    if (part.get("name") == "given") spec.givenFmt = readFormatting(part);
                    else if (part.get("name") == "family") spec.familyFmt = readFormatting(part);
                }
            } else if (child.name == "et-al") {
                spec.hasEtAl = true;
                spec.etAlTerm = std::string(child.get("term", "et-al"));
                spec.etAlFmt = readFormatting(child);
            } else if (child.name == "label") {
                spec.label = std::make_unique<Element>();
                bool known = false;
                if (!element(child, *spec.label, known)) return false;
                spec.labelFirst = !spec.hasName;
            } else if (child.name == "substitute") {
                spec.hasSubstitute = true;
                if (!children(child, spec.substitute)) return false;
            }
        }
        return true;
    }

    bool date(const xml::Node& node, Element& e) {
        e.kind = Kind::Date;
        e.value = std::string(node.get("variable"));
        e.date = std::make_unique<DateSpec>();
        DateSpec& spec = *e.date;
        std::string_view form = node.get("form");
        spec.localized = form == "text" ? 0 : form == "numeric" ? 1 : -1;
        std::string_view parts = node.get("date-parts", "year-month-day");
        spec.precision = parts == "year" ? 1 : parts == "year-month" ? 2 : 3;
        for (const xml::Node& child : node.children) {
            if (child.name == "date-part") spec.parts.push_back(readDatePart(child));
        }
        return true;
    }

    // Reads a rendering element; `known` is false for elements CSL 1.0.2
    // does not render, which are skipped
    bool element(const xml::Node& node, Element& e, bool& known) {
        known = true;
        e.line = node.line;
        e.fmt = readFormatting(node);
        if (const std::string* delimiter = node.attribute("delimiter")) {
            e.delimiter = *delimiter;
            e.hasDelimiter = true;
        }
        e.form = readForm(node.get("form"));
        const std::string& name = node.name;
        if (name == "text") {
            e.kind = Kind::Text;
            if (const std::string* variable = node.attribute("variable")) {
                e.source = FromVariable;
                e.value = *variable;
            } else if (const std::string* macro = node.attribute("macro")) {
                e.source = FromMacro;
                e.value = *macro;
            } else if (const std::string* term = node.attribute("term")) {
                e.source = FromTerm;
                e.value = *term;
                e.plural = node.get("plural") == "true" ? PluralAlways : PluralNever;
            } else {
                e.source = FromValue;
                e.value = std::string(node.get("value"));
            }
            return true;
        }
        if (name == "number") {
            e.kind = Kind::Number;
            e.value = std::string(node.get("variable"));
            std::string_view form = node.get("form");
            e.numberForm = form == "ordinal" ? Ordinal : form == "long-ordinal" ? LongOrdinal : form == "roman" ? Roman : Numeric;
            return true;
        }
        if (name == "label") {
            e.kind = Kind::Label;
            e.value = std::string(node.get("variable"));
            std::string_view plural = node.get("plural");
            e.plural = plural == "always" ? PluralAlways : plural == "never" ? PluralNever : PluralContextual;
            return true;
        }
        if (name == "names") return names(node, e);
        if (name == "date") return date(node, e);
        if (name == "group") {
            e.kind = Kind::Group;
            return children(node, e.children);
        }
        if (name == "choose") {
            e.kind = Kind::Choose;
            for (const xml::Node& child : node.children) {
                if (child.isText()) continue;
                bool otherwise = child.name == "else";
                if (child.name != "if" && child.name != "else-if" && !otherwise) continue;
                e.branches.emplace_back();
                if (!branch(child, e.branches.back(), otherwise)) return false;
            }
            return true;
        }
        known = false;
        return true;
    }

    bool layout(const xml::Node& node, Layout& out) {
        for (const xml::Node& child : node.children) {
            if (child.name != "layout") continue;
            out.fmt = readFormatting(child);
            out.delimiter = std::string(child.get("delimiter"));
            return children(child, out.children);
        }
        return fail(node.line, "<" + node.name + "> has no <layout>");
    }

    void sort(const xml::Node& node, std::vector<SortKey>& keys) {
        for (const xml::Node& child : node.children) {
            if (child.name != "sort") continue;
            for (const xml::Node& k : child.children) {
                if (k.name != "key") continue;
                SortKey key;
                key.variable = std::string(k.get("variable"));
                if (const std::string* macro = k.attribute("macro")) key.variable = "\x01" + *macro;
                key.descending = k.get("sort") == "descending";
                key.namesMin = number(k.get("names-min"), -1);
                key.namesUseFirst = number(k.get("names-use-first"), -1);
                if (const std::string* last = k.attribute("names-use-last")) key.namesUseLast = *last == "true";
                keys.push_back(std::move(key));
            }
        }
    }
};

// Visits every element below `list`, depth first
void visit(std::vector<Element>& list, const std::function<void(Element&)>& f) {
    for (Element& e : list) {
        f(e);
        visit(e.children, f);
        for (Branch& b : e.branches) visit(b.children, f);
        if (e.names) {
            if (e.names->label) f(*e.names->label);
            visit(e.names->substitute, f);
        }
    }
}

// Gives every variable the style names a slot
void assignSlots(Style& s) {
    auto intern = [&](const std::string& name) {
        auto it = s.variableIndex.find(name);
        if (it != s.variableIndex.end()) return it->second;
        int slot = (int)s.variables.size();
        s.variables.push_back(name);
        s.variableKinds.push_back(isNameVariable(name) ? VarName
                                  : isDateVariable(name) ? VarDate
                                  : name == "citation-number" ? VarCitationNumber
                                  : name == "year-suffix" ? VarYearSuffix
                                  : name == "locator" ? VarLocator
                                  : name == "page-first" ? VarPageFirst
                                  : name == "first-reference-note-number" ? VarNoteNumber
                                                                          : VarStandard);
        s.variableIndex.emplace(name, slot);
        return slot;
    };
    // Names and dates always have slots: the in-text author fallback uses them
    for (const char* name : nameVariables) intern(name);
    for (const char* name : dateVariables) intern(name);
    s.slotPage = intern("page");
    s.slotPageFirst = intern("page-first");
    s.slotURL = intern("URL");
    s.slotDOI = intern("DOI");
    s.slotIssued = intern("issued");
    s.slotAuthor = intern("author");
    s.slotEditor = intern("editor");
    s.slotTranslator = intern("translator");
    s.slotLanguage = intern("language");
    auto element = [&](Element& e) {
        switch (e.kind) {
            case Kind::Text:
                if (e.source == FromVariable) {
                    e.var = intern(e.value);
                    if (e.form == Form::Short) e.shortVar = intern(e.value + "-short");
                }
                break;
            case Kind::Number:
            case Kind::Label:
            case Kind::Date:
                if (!e.value.empty()) e.var = intern(e.value);
                break;
            case Kind::Names:
                e.names->vars.clear();
                for (const std::string& v : e.names->variables) e.names->vars.push_back(intern(v));
                break;
            case Kind::Choose:
                for (Branch& b : e.branches) {
                    for (Test& t : b.tests) {
                        if (t.kind == Test::Variable || t.kind == Test::IsNumeric || t.kind == Test::IsUncertainDate) {
                            t.var = intern(t.value);
                        }
                        if (t.kind == Test::Type && !t.value.empty()) {
                            t.var = s.typeIndex.emplace(t.value, (int)s.typeIndex.size()).first->second;
                        }
                        if (t.kind == Test::Disambiguate) s.testsDisambiguate = true;
                    }
                }
                break;
            case Kind::Group: break;
        }
    };
    for (auto& macro : s.macros) visit(macro, element);
    visit(s.citation.children, element);
    visit(s.bibliography.children, element);
    for (std::vector<SortKey>* keys : {&s.citationSort, &s.bibliographySort}) {
        for (SortKey& key : *keys) {
            if (key.macro < 0 && !key.variable.empty()) key.var = intern(key.variable);
        }
    }
}

// Whether `list` renders `variable`, following macros
bool renders(const Style& style, const std::vector<Element>& list, std::string_view variable,
             std::unordered_set<int>& seen) {
    for (const Element& e : list) {
        if ((e.kind == Kind::Text || e.kind == Kind::Number) && e.source == FromVariable && e.value == variable) return true;
        if (e.kind == Kind::Number && e.value == variable) return true;
        if (e.kind == Kind::Text && e.source == FromMacro && e.macro >= 0 && seen.insert(e.macro).second &&
            renders(style, style.macros[e.macro], variable, seen)) {
            return true;
        }
        if (renders(style, e.children, variable, seen)) return true;
        for (const Branch& b : e.branches) {
            if (renders(style, b.children, variable, seen)) return true;
        }
        if (e.names && renders(style, e.names->substitute, variable, seen)) return true;
    }
    return false;
}
}

void readLocaleNode(const xml::Node& node, Locale& locale) {
    locale.lang = std::string(node.get("xml:lang"));
    for (const xml::Node& child : node.children) {
        if (child.name == "style-options") {
            if (const std::string* v = child.attribute("punctuation-in-quote")) {
                locale.punctuationInQuote = *v == "true" ? 1 : 0;
            }
            if (const std::string* v = child.attribute("limit-day-ordinals-to-day-1")) {
                locale.limitDayOrdinals = *v == "true" ? 1 : 0;
            }
        } else if (child.name == "date") {
            DateFormat& format = child.get("form") == "numeric" ? locale.numeric : locale.text;
            format.set = true;
            format.delimiter = std::string(child.get("delimiter"));
            format.parts.clear();
            for (const xml::Node& part : child.children) {
                if (part.name == "date-part") format.parts.push_back(readDatePart(part));
            }
        } else if (child.name == "terms") {
            for (const xml::Node& t : child.children) {
                if (t.name != "term") continue;
                Term term;
                std::string_view gender = t.get("gender");
                term.gender = gender == "masculine" ? 1 : gender == "feminine" ? 2 : 0;
                std::string_view genderForm = t.get("gender-form");
                uint8_t formGender = genderForm == "masculine" ? 1 : genderForm == "feminine" ? 2 : 0;
                std::string_view match = t.get("match");
                term.match = match == "last-digit" ? 1 : match == "last-two-digits" ? 2 : match == "whole-number" ? 3 : 0;
                bool split = false;
                for (const xml::Node& part : t.children) {
                    if (part.name == "single") {
                        term.single = part.innerText();
                        split = true;
                    } else if (part.name == "multiple") {
                        term.multiple = part.innerText();
                        term.hasMultiple = true;
                        split = true;
                    }
                }
                if (!split) term.single = t.innerText();
                locale.terms[termKey(t.get("name"), readForm(t.get("form")), formGender)] = std::move(term);
            }
        }
    }
}

std::shared_ptr<const Style> readStyle(std::string_view text, std::string& error) {
    xml::Node root;
    int line = 0;
    if (!xml::parse(text, root, error, line)) {
        error = "line " + std::to_string(line) + ": " + error;
        return nullptr;
    }
    if (root.name != "style") {
        error = "not a CSL style: the document is a <" + root.name + ">";
        return nullptr;
    }
    auto style = std::make_shared<Style>();
    Style& s = *style;
    Reader reader;
    s.info.note = root.get("class") == "note";
    s.info.defaultLocale = std::string(root.get("default-locale"));
    std::string_view demote = root.get("demote-non-dropping-particle");
    s.demote = demote == "never" ? DemoteNever : demote == "sort-only" ? DemoteSortOnly : DemoteDisplayAndSort;
    s.initializeWithHyphen = root.get("initialize-with-hyphen") != "false";
    std::string_view pages = root.get("page-range-format");
    s.pageFormat = pages == "expanded" ? PagesExpanded
                 : pages == "minimal" ? PagesMinimal
                 : pages == "minimal-two" ? PagesMinimalTwo
                 : pages == "chicago" || pages == "chicago-15" ? PagesChicago15
                 : pages == "chicago-16" ? PagesChicago16
                 : PagesAsWritten;
    readNameLevel(root, s.names);
    bool hasCitation = false;
    for (const xml::Node& child : root.children) {
        if (child.isText()) continue;
        if (child.name == "info") {
            for (const xml::Node& field : child.children) {
                if (field.name == "title") s.info.title = field.innerText();
                else if (field.name == "id") s.info.id = field.innerText();
                else if (field.name == "category" && field.get("citation-format") == "numeric") s.info.numeric = true;
            }
        } else if (child.name == "locale") {
            s.locales.emplace_back();
            readLocaleNode(child, s.locales.back());
        } else if (child.name == "macro") {
            s.macroNames.emplace_back(child.get("name"));
            s.macros.emplace_back();
            if (!reader.children(child, s.macros.back())) break;
        } else if (child.name == "citation") {
            hasCitation = true;
            readNameLevel(child, s.citationNames);
            std::string_view collapse = child.get("collapse");
            s.collapse = collapse == "citation-number" ? CollapseNumber
                       : collapse == "year" ? CollapseYear
                       : collapse == "year-suffix" ? CollapseYearSuffix
                       : collapse == "year-suffix-ranged" ? CollapseYearSuffixRanged
                       : CollapseNone;
            if (const std::string* v = child.attribute("cite-group-delimiter")) {
                s.citeGroupDelimiter = *v;
                s.hasCiteGroupDelimiter = true;
            }
            if (const std::string* v = child.attribute("year-suffix-delimiter")) {
                s.yearSuffixDelimiter = *v;
                s.hasYearSuffixDelimiter = true;
            }
            if (const std::string* v = child.attribute("after-collapse-delimiter")) {
                s.afterCollapseDelimiter = *v;
                s.hasAfterCollapseDelimiter = true;
            }
            s.addNames = child.get("disambiguate-add-names") == "true";
            s.addGivenname = child.get("disambiguate-add-givenname") == "true";
            s.addYearSuffix = child.get("disambiguate-add-year-suffix") == "true";
            std::string_view rule = child.get("givenname-disambiguation-rule");
            s.givenRule = rule == "all-names" ? AllNames
                        : rule == "all-names-with-initials" ? AllNamesWithInitials
                        : rule == "primary-name" ? PrimaryName
                        : rule == "primary-name-with-initials" ? PrimaryNameWithInitials
                        : ByCite;
            s.nearNoteDistance = number(child.get("near-note-distance"), 5);
            reader.sort(child, s.citationSort);
            if (!reader.layout(child, s.citation)) break;
        } else if (child.name == "bibliography") {
            s.hasBibliography = true;
            readNameLevel(child, s.bibliographyNames);
            s.hangingIndent = child.get("hanging-indent") == "true";
            std::string_view align = child.get("second-field-align");
            s.alignField = align == "flush" || align == "margin";
            s.lineSpacing = number(child.get("line-spacing"), 1);
            s.entrySpacing = number(child.get("entry-spacing"), 1);
            if (const std::string* v = child.attribute("subsequent-author-substitute")) {
                s.hasAuthorSubstitute = true;
                s.authorSubstitute = *v;
            }
            std::string_view rule = child.get("subsequent-author-substitute-rule");
            s.authorSubstituteRule = rule == "complete-each" ? CompleteEach
                                   : rule == "partial-each" ? PartialEach
                                   : rule == "partial-first" ? PartialFirst
                                   : CompleteAll;
            reader.sort(child, s.bibliographySort);
            if (!reader.layout(child, s.bibliography)) break;
        }
    }
    if (reader.error.empty() && !hasCitation) reader.fail(root.line, "the style has no <citation>");
    if (!reader.error.empty()) {
        error = reader.error;
        return nullptr;
    }
    // Macros by name, for text macro= and sort keys
    std::unordered_map<std::string, int> macroIndex;
    for (size_t i = 0; i < s.macroNames.size(); ++i) macroIndex.emplace(s.macroNames[i], (int)i);
    auto resolve = [&](Element& e) {
        if (e.kind != Kind::Text || e.source != FromMacro || reader.error.size()) return;
        auto it = macroIndex.find(e.value);
        if (it == macroIndex.end()) {
            reader.fail(e.line, "no macro named \"" + e.value + "\"");
            return;
        }
        e.macro = it->second;
    };
    for (auto& macro : s.macros) visit(macro, resolve);
    visit(s.citation.children, resolve);
    visit(s.bibliography.children, resolve);
    for (std::vector<SortKey>* keys : {&s.citationSort, &s.bibliographySort}) {
        for (SortKey& key : *keys) {
            if (key.variable.empty() || key.variable[0] != '\x01') continue;
            auto it = macroIndex.find(key.variable.substr(1));
            if (it == macroIndex.end()) {
                reader.fail(root.line, "no macro named \"" + key.variable.substr(1) + "\" for a sort key");
                break;
            }
            key.macro = it->second;
            key.variable.clear();
        }
    }
    if (!reader.error.empty()) {
        error = reader.error;
        return nullptr;
    }
    assignSlots(s);
    std::unordered_set<int> seen;
    s.usesYearSuffix = renders(s, s.citation.children, "year-suffix", seen);
    seen.clear();
    s.usesYearSuffix = s.usesYearSuffix || renders(s, s.bibliography.children, "year-suffix", seen);
    seen.clear();
    if (renders(s, s.citation.children, "citation-number", seen)) s.info.numeric = true;
    error.clear();
    return style;
}

std::shared_ptr<const Locale> readLocale(std::string_view text, std::string& error) {
    xml::Node root;
    int line = 0;
    if (!xml::parse(text, root, error, line)) {
        error = "line " + std::to_string(line) + ": " + error;
        return nullptr;
    }
    if (root.name != "locale") {
        error = "not a CSL locale: the document is a <" + root.name + ">";
        return nullptr;
    }
    auto locale = std::make_shared<Locale>();
    readLocaleNode(root, *locale);
    error.clear();
    return locale;
}

const StyleInfo& info(const Style& style) { return style.info; }
}
