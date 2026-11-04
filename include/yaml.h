#pragma once
// A small YAML reader (docs/specs/scholar.md): the YAML that front matter and
// CSL YAML references are written in, read into the JSON values json.h holds,
// so the CSL-JSON reader and anything else that reads JSON read it alike.
// Block mappings and sequences, flow [ ] and { }, plain, quoted and block
// scalars, comments and document markers. Anchors, aliases, tags, directives
// and a second document are refused with an error. Plain data and text only.
#include "json.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace yaml {

// The first mistake in a text: where it is and what it is
struct Error {
    int line = 0;       // 1-based
    size_t offset = 0;  // into the text read
    std::string message;
};

// One YAML document as JSON values: a mapping becomes an object with its
// keys in order, a sequence an array, and a scalar a string, except that a
// plain null (nothing, ~, null), true or false, or whole number written as
// one (12, -3; not 007 or 1.50) is typed, its text kept as written. begin
// and end are offsets into `text`. False at the first mistake, which `error`
// holds; `out` keeps what was read up to it
bool parse(std::string_view text, json::Json& out, Error& error);

// A Markdown document's front matter, as Tinta's front matter reads it: the
// lines between a first line `---` and the next line `---` or `...`. False
// when it has none; else [begin, end) is the YAML between the markers
bool frontMatter(std::string_view document, size_t& begin, size_t& end);

}
