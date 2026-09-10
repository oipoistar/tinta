#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace fm {
enum class Kind { Text, Date, List, Boolean, Structured };
enum class Format { Plain, Large, Absolute, Relative, Chips, Hashtags, Comma, Count };
struct Property {
    std::string key, text;
    std::vector<std::string> items;
    Kind kind = Kind::Text;
    size_t valueStart = 0, valueEnd = 0;
    bool writable = false, nullValue = false;
};
struct Document {
    bool present = false, safeToWrite = true;
    size_t begin = 0, end = 0, closing = 0;
    std::string eol = "\n";
    std::vector<Property> properties;
};
struct Rule {
    std::string key;
    bool show = false, right = false, label = true;
    Format format = Format::Plain;
    int limit = 5; // zero means all
    Kind hint = Kind::Text;
};
struct Settings {
    bool shown = true, showOther = false;
    std::vector<Rule> rules = {
        {"title",true,false,false,Format::Large,5,Kind::Text},
        {"author",false,false,true,Format::Plain,5,Kind::Text},
        {"tags",true,false,false,Format::Chips,5,Kind::List},
        {"created",false,true,true,Format::Absolute,5,Kind::Date},
        {"updated",false,true,true,Format::Relative,5,Kind::Date},
        {"draft",false,false,true,Format::Plain,5,Kind::Boolean}
    };
};
Document parse(const std::string& source);
const Property* find(const Document& doc, const std::string& key);
const Rule* find(const Settings& settings, const std::string& key);
bool discover(Settings& settings, const std::vector<Property>& properties);
bool maintained(const Settings& settings, const std::string& key);
std::vector<Format> formats(Kind kind);
std::string formatValue(const Property& property, Format format, const std::string& now);
std::string utcNow();
// Edits only unambiguous scalar values, never reserializes the YAML document.
std::string stamp(const std::string& source, const Settings& settings,
                  const std::string& firstSeen, const std::string& now);
std::string encodeRule(const Rule& rule);
bool decodeRule(const std::string& value, Rule& rule);
}
