// Build step: packs a CSL style or locale for tinta.rc. Drops what the
// processor never reads (comments, the <info> block, indentation between
// tags) and compresses the rest with the Windows compression API; scholar.cpp
// reverses it at first use.
//
//   csl_pack <input.csl|.xml> <output.bin>

#include <windows.h>
#include <compressapi.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

static std::string minify(const std::string& xml) {
    std::string out;
    out.reserve(xml.size());
    size_t at = 0;
    while (at < xml.size()) {
        if (xml.compare(at, 4, "<!--") == 0) {
            size_t end = xml.find("-->", at + 4);
            at = end == std::string::npos ? xml.size() : end + 3;
            continue;
        }
        if (xml.compare(at, 6, "<info>") == 0) {
            size_t end = xml.find("</info>", at + 6);
            at = end == std::string::npos ? xml.size() : end + 7;
            continue;
        }
        // Whitespace between two tags that spans lines is indentation; a
        // term's own text never does
        if (xml[at] == '>') {
            size_t next = at + 1;
            bool newline = false;
            while (next < xml.size() && (xml[next] == ' ' || xml[next] == '\t' ||
                                         xml[next] == '\r' || xml[next] == '\n')) {
                newline |= xml[next] == '\n';
                ++next;
            }
            out += '>';
            if (newline && next < xml.size() && xml[next] == '<') {
                at = next;
            } else {
                at += 1;
            }
            continue;
        }
        out += xml[at++];
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: csl_pack <input> <output>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "csl_pack: cannot read %s\n", argv[1]);
        return 1;
    }
    std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string packed = minify(xml);

    COMPRESSOR_HANDLE compressor = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &compressor)) {
        std::fprintf(stderr, "csl_pack: no compressor (%lu)\n", GetLastError());
        return 1;
    }
    SIZE_T needed = 0;
    Compress(compressor, packed.data(), packed.size(), nullptr, 0, &needed);
    std::string compressed(needed, '\0');
    SIZE_T written = 0;
    BOOL ok = Compress(compressor, packed.data(), packed.size(), compressed.data(), needed, &written);
    CloseCompressor(compressor);
    if (!ok) {
        std::fprintf(stderr, "csl_pack: compression failed (%lu)\n", GetLastError());
        return 1;
    }
    compressed.resize(written);

    std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
    out.write(compressed.data(), (std::streamsize)compressed.size());
    if (!out) {
        std::fprintf(stderr, "csl_pack: cannot write %s\n", argv[2]);
        return 1;
    }
    std::printf("csl_pack: %s %zu -> %zu -> %zu bytes\n", argv[1], xml.size(), packed.size(),
                compressed.size());
    return 0;
}
