#pragma once
// A small non-validating XML reader: elements, attributes, text, the five
// named entities and character references, comments, CDATA and processing
// instructions. Namespaces are ignored: names keep their local part, except
// xml:lang and xmlns declarations, which stay as written. Plain text only.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xml {

struct Node {
    std::string name;  // an element's local name; empty for a text node
    std::string text;  // a text node's characters, entities decoded
    std::vector<std::pair<std::string, std::string>> attributes;  // in document order
    std::vector<Node> children;
    int line = 0;  // where it starts, from 1

    bool isText() const { return name.empty(); }
    const std::string* attribute(std::string_view key) const;
    // The attribute's value, or `fallback` when it is absent
    std::string_view get(std::string_view key, std::string_view fallback = {}) const;
    bool has(std::string_view key) const { return attribute(key) != nullptr; }
    // The text of every text node below, in document order
    std::string innerText() const;
};

// Reads a document into its root element. On malformed input it returns
// false, with `error` saying why and `errorLine` where
bool parse(std::string_view text, Node& root, std::string& error, int& errorLine);
}
