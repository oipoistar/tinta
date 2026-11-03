// The JSON reader (json.h) that Sheets and the reference readers share.

#pragma optimize("s", on)

#include "json.h"

#include <cstdlib>

namespace json {

void appendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) {
        out += (char)code;
    } else if (code < 0x800) {
        out += (char)(0xC0 | (code >> 6));
        out += (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += (char)(0xE0 | (code >> 12));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    } else {
        out += (char)(0xF0 | (code >> 18));
        out += (char)(0x80 | ((code >> 12) & 0x3F));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    }
}

bool JsonReader::fail(const char* what) {
    if (error.empty()) error = std::string(what) + " at offset " + std::to_string(at);
    return false;
}

void JsonReader::space() {
    while (at < src.size() &&
           (src[at] == ' ' || src[at] == '\t' || src[at] == '\n' || src[at] == '\r')) {
        ++at;
    }
}

bool JsonReader::literal(std::string_view word) {
    if (src.substr(at, word.size()) != word) return false;
    at += word.size();
    return true;
}

bool JsonReader::hex4(uint32_t& out) {
    if (at + 4 > src.size()) return fail("short \\u escape");
    out = 0;
    for (int i = 0; i < 4; ++i) {
        char c = src[at++];
        out <<= 4;
        if (c >= '0' && c <= '9') out |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') out |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') out |= (uint32_t)(c - 'A' + 10);
        else return fail("bad \\u escape");
    }
    return true;
}

bool JsonReader::string(std::string& out) {
    if (at >= src.size() || src[at] != '"') return fail("expected a string");
    ++at;
    while (at < src.size()) {
        char c = src[at++];
        if (c == '"') return true;
        if ((unsigned char)c < 0x20) return fail("control character in a string");
        if (c != '\\') {
            out += c;
            continue;
        }
        if (at >= src.size()) break;
        char e = src[at++];
        switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t code = 0;
                if (!hex4(code)) return false;
                if (code >= 0xD800 && code <= 0xDBFF && src.substr(at, 2) == "\\u") {
                    size_t keep = at;
                    at += 2;
                    uint32_t low = 0;
                    if (!hex4(low)) return false;
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else {
                        at = keep;
                    }
                }
                if (code >= 0xD800 && code <= 0xDFFF) code = 0xFFFD;  // a lone half
                appendUtf8(out, code);
                break;
            }
            default:
                return fail("bad escape");
        }
    }
    return fail("unterminated string");
}

bool JsonReader::value(Json& out) {
    space();
    if (at >= src.size()) return fail("unexpected end");
    if (++depth > 64) return fail("nested too deep");
    out.begin = at;
    bool ok = true;
    char c = src[at];
    if (c == '{') {
        out.type = Json::Object;
        ++at;
        space();
        if (at < src.size() && src[at] == '}') {
            ++at;
        } else {
            while (ok) {
                space();
                std::string key;
                if (!string(key)) { ok = false; break; }
                space();
                if (at >= src.size() || src[at] != ':') { ok = fail("expected ':'"); break; }
                ++at;
                Json item;
                if (!value(item)) { ok = false; break; }
                out.fields.emplace_back(std::move(key), std::move(item));
                space();
                if (at < src.size() && src[at] == ',') { ++at; continue; }
                if (at < src.size() && src[at] == '}') { ++at; break; }
                ok = fail("expected ',' or '}'");
            }
        }
    } else if (c == '[') {
        out.type = Json::Array;
        ++at;
        space();
        if (at < src.size() && src[at] == ']') {
            ++at;
        } else {
            while (ok) {
                Json item;
                if (!value(item)) { ok = false; break; }
                out.items.push_back(std::move(item));
                space();
                if (at < src.size() && src[at] == ',') { ++at; continue; }
                if (at < src.size() && src[at] == ']') { ++at; break; }
                ok = fail("expected ',' or ']'");
            }
        }
    } else if (c == '"') {
        out.type = Json::String;
        ok = string(out.text);
    } else if (literal("true")) {
        out.type = Json::Bool;
        out.boolean = true;
    } else if (literal("false")) {
        out.type = Json::Bool;
    } else if (literal("null")) {
        out.type = Json::Null;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        size_t start = at;
        if (src[at] == '-') ++at;
        while (at < src.size() &&
               ((src[at] >= '0' && src[at] <= '9') || src[at] == '.' || src[at] == 'e' ||
                src[at] == 'E' || src[at] == '+' || src[at] == '-')) {
            ++at;
        }
        std::string digits(src.substr(start, at - start));
        char* endPtr = nullptr;
        out.type = Json::Number;
        out.number = std::strtod(digits.c_str(), &endPtr);
        if (!endPtr || *endPtr) ok = fail("bad number");
    } else {
        ok = fail("unexpected character");
    }
    --depth;
    out.end = at;
    return ok;
}

}
