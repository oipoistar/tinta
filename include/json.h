#pragma once
// A small JSON reader: one value and the offsets it spans in its source, for
// JSON Canvas (sheet.h) and CSL-JSON (bib.h). Plain data and text only.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json {

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;
    size_t begin = 0, end = 0;  // where the value sits in the source

    const Json* get(std::string_view key) const {
        for (const auto& field : fields) {
            if (field.first == key) return &field.second;
        }
        return nullptr;
    }
};

// Reads values from `src` starting at `at`. The first failure is kept in
// `error` ("expected ':' at offset 12"), and `at` stays where it happened
struct JsonReader {
    std::string_view src;
    size_t at = 0;
    int depth = 0;
    std::string error;

    bool fail(const char* what);
    void space();
    bool literal(std::string_view word);
    bool hex4(uint32_t& out);
    bool string(std::string& out);
    bool value(Json& out);
};

// A code point as UTF-8
void appendUtf8(std::string& out, uint32_t code);
}
