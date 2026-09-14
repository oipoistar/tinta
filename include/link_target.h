#ifndef TINTA_LINK_TARGET_H
#define TINTA_LINK_TARGET_H

#include <string>

namespace qmd {
inline std::string decodeLinkComponent(const std::string& value) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            int high = hex(value[i + 1]), low = hex(value[i + 2]);
            // Keep encoded NUL literal: native file APIs must not truncate it.
            if (high >= 0 && low >= 0 && (high || low)) {
                out += static_cast<char>(high * 16 + low);
                i += 2;
                continue;
            }
        }
        out += value[i];
    }
    return out;
}

struct LinkTarget {
    std::string path;
    std::string fragment;
    bool hasFragment = false;
};

inline LinkTarget splitLinkTarget(const std::string& url) {
    auto hash = url.find('#');
    return {decodeLinkComponent(url.substr(0, hash)),
            hash == std::string::npos ? std::string{} : decodeLinkComponent(url.substr(hash + 1)),
            hash != std::string::npos};
}

// Internal fileref payloads carry resolved paths. Escape delimiters so a
// literal '#' or '%23' in a filename survives another split without ambiguity.
inline std::string encodeLinkTarget(const LinkTarget& target) {
    auto escape = [](const std::string& value) {
        std::string out;
        for (char c : value) {
            if (c == '%') out += "%25";
            else if (c == '#') out += "%23";
            else out += c;
        }
        return out;
    };
    return escape(target.path) + (target.hasFragment ? "#" + escape(target.fragment) : "");
}
} // namespace qmd
#endif
