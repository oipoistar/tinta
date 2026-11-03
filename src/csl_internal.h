#pragma once
// The CSL processor's insides (csl.h), shared by csl_text.cpp (text, case and
// rich text), csl_style.cpp (reading styles and locales), csl_render.cpp
// (rendering elements for one item) and csl_process.cpp (citations and the
// bibliography). Plain data and text only.
#include "csl.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xml {
struct Node;
}

namespace csl {

// --- Output pieces -----------------------------------------------------------

// What a piece is, beyond its formatting
enum Mark : uint8_t {
    NoCase = 1,      // immune to text-case: <span class="nocase">, literal names
    CloseQuote = 2,  // a closing quotation mark the style added
    FirstNames = 4,  // the name list of the output's first names element
};

struct Piece {
    std::string text;
    uint16_t flags = 0;  // Flag
    uint8_t mark = 0;    // Mark
    uint8_t name = 0;    // in the first names element: the name's place, from 1
    const std::string* link = nullptr;  // the URL, kept by the item or its state
};
using Pieces = std::vector<Piece>;

// --- Text (csl_text.cpp) -----------------------------------------------------

enum TextCase : uint8_t { CaseNone, Lowercase, Uppercase, CapitalizeFirst, CapitalizeAll, Sentence, TitleCase };

uint32_t nextChar(std::string_view text, size_t& at);
void appendChar(std::string& out, uint32_t code);
uint32_t upper(uint32_t code);
uint32_t lower(uint32_t code);
bool hasCjk(std::string_view text);
bool startsWith(std::string_view text, std::string_view prefix);
std::string trim(std::string_view text);

// Appends `add` to `out`, mending the punctuation where they meet: doubled
// marks, ", ." and the like, double spaces, and with punctuationInQuote a
// comma or period after a closing quotation mark moves inside it
void join(Pieces& out, Pieces& add, bool punctuationInQuote);
void addText(Pieces& out, std::string_view text, uint16_t flags, bool punctuationInQuote);
// Puts `text` before the pieces, mending the punctuation where they meet
void prependText(Pieces& pieces, std::string_view text, uint16_t flags, const std::string* link = nullptr);
// CSL rich text (<i>, <b>, <sup>, <sub>, small caps, nocase spans, &amp;
// &lt; &gt;) as pieces; italics, bold and small caps flip inside their like
void parseRich(std::string_view text, uint16_t flags, Pieces& out);
std::string plainText(const Pieces& pieces);
std::string stripMarkup(std::string_view rich);
void applyCase(Pieces& pieces, uint8_t textCase, bool english);
void stripPeriods(Pieces& pieces);
Rich toRich(const Pieces& pieces);

// The length of a range dash at text[at]: a hyphen, or an en or em dash
// (BibTeX's -- arrives as an en dash), else 0
size_t dashAt(std::string_view text, size_t at);
// Numbers as CSL reads them: "12", "2nd", "L2d", "2-4", "2, 3 & 5"
bool isNumeric(std::string_view text);
// A range or list of numbers, for plural labels
bool hasSeveral(std::string_view text);
std::string roman(int number);
// A key that compares like the text, for sorting: lower case, accents folded
std::string collationKey(std::string_view text);

// --- Styles and locales (csl_style.cpp) --------------------------------------

enum class Form : uint8_t { Long, Short, Verb, VerbShort, Symbol };

struct Term {
    std::string single, multiple;
    bool hasMultiple = false;
    uint8_t gender = 0;  // a noun's gender: 0 neuter, 1 masculine, 2 feminine
    uint8_t match = 0;   // ordinal-NN: 1 last-digit, 2 last-two-digits, 3 whole-number
};
std::string termKey(std::string_view name, Form form, uint8_t genderForm);
// The same key hashed, for lookups that build no string
uint64_t termHash(std::string_view name, Form form, uint8_t genderForm);

struct Formatting {
    std::string prefix, suffix;
    uint16_t set = 0, clear = 0, flip = 0;  // flags turned on, off, and over
    uint8_t textCase = CaseNone;
    bool stripPeriods = false, quotes = false;
    uint16_t apply(uint16_t flags) const { return (uint16_t)(((flags & ~clear) | set) ^ flip); }
};

enum DatePartName : uint8_t { PartYear, PartMonth, PartDay };
enum DatePartForm : uint8_t { PartLong, PartShort, PartNumeric, PartLeadingZeros, PartOrdinal };

struct DatePart {
    uint8_t name = PartYear;
    uint8_t form = PartLong;
    bool hasForm = false, hasRangeDelimiter = false;
    std::string rangeDelimiter;
    Formatting fmt;
};

struct DateFormat {
    bool set = false;
    std::string delimiter;
    std::vector<DatePart> parts;
};

struct Locale {
    std::string lang;  // xml:lang; empty for a style's language-less block
    std::unordered_map<std::string, Term> terms;
    DateFormat text, numeric;
    int8_t punctuationInQuote = -1, limitDayOrdinals = -1;
};

enum Precedes : uint8_t { Contextual, AfterInvertedName, Always, Never };
enum NameForm : uint8_t { NameLong, NameShort, NameCount };
enum SortOrder : uint8_t { SortNone, SortFirst, SortAll };

// The inheritable name options: on <style>, <citation>, <bibliography> and
// <name>, each level taking what it leaves unset from the one above
struct NameOptions {
    enum : uint32_t {
        HasAnd = 1 << 0, HasPrecedesEtAl = 1 << 1, HasPrecedesLast = 1 << 2,
        HasEtAlMin = 1 << 3, HasEtAlUseFirst = 1 << 4, HasSubsequentMin = 1 << 5,
        HasSubsequentUseFirst = 1 << 6, HasEtAlUseLast = 1 << 7, HasInitialize = 1 << 8,
        HasInitializeWith = 1 << 9, HasSortOrder = 1 << 10, HasSortSeparator = 1 << 11,
        HasForm = 1 << 12, HasDelimiter = 1 << 13,
    };
    uint32_t has = 0;
    uint8_t andMode = 0;  // 0 none, 1 text, 2 symbol
    uint8_t precedesEtAl = Contextual, precedesLast = Contextual;
    int etAlMin = 0, etAlUseFirst = 0, subsequentMin = 0, subsequentUseFirst = 0;
    bool etAlUseLast = false, initialize = true;
    std::string initializeWith;
    uint8_t sortOrder = SortNone;
    std::string sortSeparator = ", ";
    uint8_t form = NameLong;
    std::string delimiter = ", ";

    void inherit(const NameOptions& outer);
};

enum class Kind : uint8_t { Text, Number, Label, Names, Date, Group, Choose };
enum Source : uint8_t { FromValue, FromVariable, FromMacro, FromTerm };
enum NumberForm : uint8_t { Numeric, Ordinal, LongOrdinal, Roman };
enum Plural : uint8_t { PluralContextual, PluralAlways, PluralNever };

struct Element;

// Variables are found by slot, a number the style gives each name it uses;
// each item keeps its values in a table by slot (ItemState::values)
enum VariableKind : uint8_t {
    VarStandard, VarName, VarDate, VarCitationNumber, VarYearSuffix, VarLocator, VarPageFirst, VarNoteNumber
};

struct Test {
    enum Kind : uint8_t { Type, Variable, IsNumeric, IsUncertainDate, Locator, Position, Disambiguate };
    Kind kind = Type;
    std::string value;
    int var = -1;  // the slot of a variable test's variable, or the type's number
};

enum Match : uint8_t { MatchAll, MatchAny, MatchNone };

struct Branch {
    uint8_t match = MatchAll;
    std::vector<Test> tests;  // none for <else>
    std::vector<Element> children;
};

struct NamesSpec {
    std::vector<std::string> variables;
    std::vector<int> vars;  // their slots
    bool hasName = false, hasEtAl = false;
    NameOptions name;
    Formatting nameFmt, givenFmt, familyFmt;
    std::string etAlTerm = "et-al";
    Formatting etAlFmt;
    std::unique_ptr<Element> label;
    bool labelFirst = false;
    bool hasSubstitute = false;
    std::vector<Element> substitute;
    // No cs:name, cs:et-al or cs:label: in a substitute it borrows them
    bool shorthand() const { return !hasName && !hasEtAl && !label; }
};

struct DateSpec {
    int8_t localized = -1;  // -1 the style's own parts; 0 the locale's text form, 1 numeric
    uint8_t precision = 3;  // date-parts: 3 year-month-day, 2 year-month, 1 year
    std::vector<DatePart> parts;
};

struct Element {
    Kind kind = Kind::Text;
    uint8_t source = FromValue;
    Form form = Form::Long;
    uint8_t numberForm = Numeric;
    uint8_t plural = PluralContextual;
    bool hasDelimiter = false;
    int macro = -1;
    int var = -1, shortVar = -1;  // the variable's slot, and its -short twin's for form="short"
    int line = 0;
    std::string value;  // the variable, term or literal text
    std::string delimiter;
    Formatting fmt;
    std::vector<Element> children;  // a group's
    std::vector<Branch> branches;   // a choose's
    std::unique_ptr<NamesSpec> names;
    std::unique_ptr<DateSpec> date;
};

struct Layout {
    Formatting fmt;
    std::string delimiter;
    std::vector<Element> children;
};

struct SortKey {
    std::string variable;
    int var = -1;
    int macro = -1;
    bool descending = false;
    int namesMin = -1, namesUseFirst = -1, namesUseLast = -1;
};

enum Collapse : uint8_t { CollapseNone, CollapseNumber, CollapseYear, CollapseYearSuffix, CollapseYearSuffixRanged };
enum GivenRule : uint8_t { ByCite, AllNames, AllNamesWithInitials, PrimaryName, PrimaryNameWithInitials };
enum Demote : uint8_t { DemoteDisplayAndSort, DemoteSortOnly, DemoteNever };
enum PageFormat : uint8_t { PagesAsWritten, PagesExpanded, PagesMinimal, PagesMinimalTwo, PagesChicago15, PagesChicago16 };
enum SubstituteRule : uint8_t { CompleteAll, CompleteEach, PartialEach, PartialFirst };

struct Engine;

// Name options and the names delimiter of one level
struct NameLevel {
    NameOptions options;
    std::string namesDelimiter;
    bool hasNamesDelimiter = false;
};

struct Style {
    StyleInfo info;
    std::vector<Locale> locales;  // the style's own <locale> blocks
    std::vector<std::string> macroNames;
    std::vector<std::vector<Element>> macros;
    NameLevel names;  // on <style>
    uint8_t demote = DemoteDisplayAndSort;
    bool initializeWithHyphen = true;
    uint8_t pageFormat = PagesAsWritten;
    bool usesYearSuffix = false;  // some element renders year-suffix itself
    bool testsDisambiguate = false;  // some condition tests disambiguate="true"

    Layout citation;
    std::vector<SortKey> citationSort;
    NameLevel citationNames;
    uint8_t collapse = CollapseNone;
    std::string citeGroupDelimiter = ", ";
    std::string yearSuffixDelimiter, afterCollapseDelimiter;
    bool hasYearSuffixDelimiter = false, hasAfterCollapseDelimiter = false;
    bool hasCiteGroupDelimiter = false;
    bool addNames = false, addGivenname = false, addYearSuffix = false;
    uint8_t givenRule = ByCite;
    int nearNoteDistance = 5;

    bool hasBibliography = false;
    Layout bibliography;
    std::vector<SortKey> bibliographySort;
    NameLevel bibliographyNames;
    bool hangingIndent = false, alignField = false;
    int lineSpacing = 1, entrySpacing = 1;
    bool hasAuthorSubstitute = false;
    std::string authorSubstitute;
    uint8_t authorSubstituteRule = CompleteAll;

    // Every variable name the style uses, and every CSL name and date variable
    std::vector<std::string> variables;
    std::vector<uint8_t> variableKinds;
    std::unordered_map<std::string, int> variableIndex;
    std::unordered_map<std::string, int> typeIndex;  // the item types its conditions name
    int slotPage = -1, slotURL = -1, slotDOI = -1, slotIssued = -1, slotTranslator = -1, slotEditor = -1,
        slotAuthor = -1, slotPageFirst = -1, slotLanguage = -1;

    // Engines built for this style, kept for the runs that follow
    mutable std::mutex engineLock;
    mutable std::vector<std::shared_ptr<const Engine>> engines;
};

// Reads one <locale> element, of a style or a locale file
void readLocaleNode(const xml::Node& node, Locale& locale);

// --- Processing --------------------------------------------------------------

// The locale chain, terms and options process() works with; built once for
// a style, a language and the locale texts, and kept with the style
struct Engine {
    const Style* style = nullptr;
    std::string lang;
    std::vector<uint64_t> sources;      // the hashes of the locale texts it was built from
    std::vector<std::string> warnings;  // what building it found wrong
    std::vector<std::shared_ptr<const Locale>> files;  // keeps the locale files alive
    struct TermEntry {
        const std::string* key;  // termKey, to tell hash collisions apart
        const Term* term;
    };
    std::unordered_map<uint64_t, TermEntry> terms;  // by termHash
    const DateFormat* dateText = nullptr;
    const DateFormat* dateNumeric = nullptr;
    bool punctuationInQuote = false, limitDayOrdinals = false;
    NameLevel citationNames, bibliographyNames;  // the style's level folded in
    std::string openQuote, closeQuote, openInnerQuote, closeInnerQuote;
    std::string pageDelimiter;
    std::unique_ptr<Element> authorFallback;  // the author of an in-text cite when the style names none

    const Term* term(std::string_view name, Form form, uint8_t genderForm = 0) const;
    const std::string& termText(std::string_view name, Form form = Form::Long, bool plural = false) const;
};

enum Position : uint8_t { First, Subsequent, Ibid, IbidWithLocator };

// One item as this run formats it: its number and how it is disambiguated
struct ItemState {
    const bib::Item* item = nullptr;
    std::vector<const void*> values;  // by slot: a std::string, name list or date, or null
    int type = -1;                    // the item's type in Style::typeIndex
    std::string urlLink, doiLink;     // the URL and https://doi.org/ and the DOI, unescaped
    int number = 0;
    std::string numberText;
    std::string yearSuffix;
    int addNames = 0;
    std::vector<std::pair<const bib::Name*, uint8_t>> expand;  // names shown fuller
    bool disambiguate = false;
    int order = 0;  // in the bibliography
    bool cited = false;
    // Its cite as disambiguation last rendered it, without a locator; any
    // cite of it without one reads the same unless the rendering asked
    // where the cite stands
    Pieces cite;
    bool citeValid = false, citePositional = false;
};

struct CiteState {
    const cite::Item* cite = nullptr;  // null when no cite gives a locator
    uint8_t position = First;
    bool nearNote = false;
};

// A name a citation rendered, in its three widths, for disambiguation
struct NameRecord {
    const bib::Name* name = nullptr;
    bool primary = false;
    std::string forms[3];  // as styled, with initials, with the full given name
};

enum NamesMode : uint8_t { NamesKeep, NamesSuppress, NamesCapture, NamesMark };

struct RenderJob {
    const Engine* engine = nullptr;
    const ItemState* state = nullptr;
    const CiteState* cite = nullptr;
    bool bibliography = false;  // the context's name options
    bool sorting = false;       // a sort key: names inverted, dates sortable
    int namesMin = -1, namesUseFirst = -1, namesUseLast = -1;
    uint8_t namesMode = NamesKeep;
    Pieces* captured = nullptr;              // NamesCapture: the first names element
    std::vector<NameRecord>* records = nullptr;  // names rendered, for disambiguation
    bool* positional = nullptr;  // set when the rendering depends on the cite's position
};

// Fills the item's table of values by slot
void fillValues(const Style& style, ItemState& state);
// Renders `elements`, the children of a layout, for the job's item
void render(const RenderJob& job, const std::vector<Element>& elements, uint16_t flags, Pieces& out);
// As render, with the first element that prints apart (second-field-align)
void renderSplit(const RenderJob& job, const std::vector<Element>& elements, uint16_t flags, Pieces& first,
                 Pieces& rest);
// Renders one element; for the in-text author fallback
void renderElement(const RenderJob& job, const Element& element, uint16_t flags, Pieces& out);
// A sort key's value for the job's item; empty when the item has none
std::string sortValue(const RenderJob& job, const SortKey& key);
// Page ranges as the style writes them: "321-8" -> "321" en dash "328" (expanded)
std::string formatPages(std::string_view pages, uint8_t format, std::string_view delimiter);
bool isNameVariable(std::string_view variable);
bool isDateVariable(std::string_view variable);
}
