#pragma once
// The Citation Style Language 1.0.2: styles and locales read from their XML,
// and a processor that formats citations and a bibliography from bib.h
// references (docs/specs/scholar.md). Plain data and text only.
#include "bib.h"
#include "cite.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace csl {

enum Flag : uint16_t {
    Italic = 1,
    Bold = 2,
    SmallCaps = 4,
    Superscript = 8,
    Subscript = 16,
    Underline = 32,
};

// A stretch of output text in one format
struct Run {
    std::string text;  // UTF-8
    uint16_t flags = 0;
    std::string link;  // the URL this text opens (a DOI or URL variable), or empty
};
using Rich = std::vector<Run>;

std::string plain(const Rich& rich);
// <i> <b> <span style="font-variant:small-caps"> <sup> <sub> <u> <a href>,
// with text escaped
std::string html(const Rich& rich);

struct Style;
struct Locale;

struct StyleInfo {
    std::string id, title;
    std::string defaultLocale;  // the style's default-locale, or empty
    bool note = false;          // class="note": citations belong in footnotes
    bool numeric = false;       // cites by number: [1], or a superscript 1
};

// Parse a style or a locale; null, with `error` saying why, when the text is
// not one
std::shared_ptr<const Style> readStyle(std::string_view xml, std::string& error);
std::shared_ptr<const Locale> readLocale(std::string_view xml, std::string& error);
const StyleInfo& info(const Style& style);

// The locale XML for a language tag ("en-US", "zh-CN", "de"), or empty when
// there is none. The processor falls back from zh-TW to zh to en-US
using LocaleSource = std::function<std::string(std::string_view lang)>;

struct Entry {
    std::string id;
    Rich text;   // the entry without its first field when `field` is set
    Rich field;  // second-field-align: the first field ("[1]"), drawn in a column
};

struct Bibliography {
    std::vector<Entry> entries;
    bool hangingIndent = false;
    bool alignField = false;  // second-field-align="flush" or "margin"
    int lineSpacing = 1, entrySpacing = 1;
};

struct Options {
    std::string lang;                 // the document's lang:, else the style's, else en-US
    std::vector<std::string> nocite;  // keys listed though not cited; "*" lists every one
};

struct Result {
    std::vector<Rich> citations;  // one per input citation, in order
    Bibliography bibliography;
    std::vector<std::string> warnings;  // "no reference for @smith2020", style problems
};

// Formats the citations of one document, in document order (numbering,
// disambiguation and ibid depend on it), and its bibliography
Result process(const Style& style, const LocaleSource& locales, const bib::Library& library,
               const std::vector<cite::Citation>& citations, const Options& options);
}
