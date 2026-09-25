// PlantUML bridge implementation. App-free and headless: standard C++ plus
// the Win32 process and file APIs (CreateProcessW / WaitForSingleObject /
// TerminateProcess, file attributes, SearchPathW). No UI, no graphics, no
// theme or App types, and no remote access of any kind.

#include "plantuml.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <windows.h>

namespace plantuml {
namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

void fnvByte(uint64_t& hash, unsigned char value) {
    hash ^= static_cast<uint64_t>(value);
    hash *= kFnvPrime;
}

void fnvBlock(uint64_t& hash, const void* data, size_t size) {
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) fnvByte(hash, bytes[i]);
}

// Length prefixing keeps the field stream unambiguous.
void fnvU64(uint64_t& hash, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        fnvByte(hash, static_cast<unsigned char>(value >> (8 * i)));
    }
}

void fnvText(uint64_t& hash, const std::string& text) {
    fnvU64(hash, static_cast<uint64_t>(text.size()));
    fnvBlock(hash, text.data(), text.size());
}

void fnvText(uint64_t& hash, const std::wstring& text) {
    fnvU64(hash, static_cast<uint64_t>(text.size()));
    for (wchar_t ch : text) {
        fnvByte(hash, static_cast<unsigned char>(ch & 0xFF));
        fnvByte(hash, static_cast<unsigned char>((ch >> 8) & 0xFF));
    }
}

bool fileExists(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool hasJarSuffix(const std::wstring& path) {
    if (path.size() < 4) return false;
    return _wcsicmp(path.c_str() + path.size() - 4, L".jar") == 0;
}

// Trims spaces, tabs and a CR terminator from a line range and matches the
// PlantUML anchor case-insensitively: the exact token `@startuml`, or the
// token followed by a block-name separator - whitespace (`@startuml Flow`)
// or an opening parenthesis (`@startuml(Flow)`). A lookalike such as
// `@startumlx`, and every non-UML start tag (`@startjson`, `@startyaml`,
// ...), never matches.
bool isAnchorLine(const std::string& source, size_t begin, size_t end) {
    while (begin < end && (source[begin] == ' ' || source[begin] == '\t')) ++begin;
    while (end > begin && (source[end - 1] == ' ' || source[end - 1] == '\t' ||
                           source[end - 1] == '\r')) {
        --end;
    }
    static const char kAnchor[] = "@startuml";
    const size_t anchorLen = 9;
    if (end - begin < anchorLen) return false;
    for (size_t i = 0; i < anchorLen; ++i) {
        char c = source[begin + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != kAnchor[i]) return false;
    }
    if (end - begin == anchorLen) return true;
    const char after = source[begin + anchorLen];
    return after == ' ' || after == '\t' || after == '(';
}

// UTF-8 (CP_UTF8) to wide for artifact names parsed out of the source.
std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (length <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), length);
    return out;
}

// A block name is used verbatim as an artifact file name, so anything that
// could escape the private work directory or confuse the file APIs is
// rejected and the unnamed path is taken instead.
bool isSafeArtifactName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (char c : name) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            return false;
        }
    }
    return true;
}

// The block name on the first `@startuml` anchor line, accepting both the
// `@startuml Flow` and the `@startuml(Flow)` forms. Returns an empty string
// when the first block is unnamed (or no anchor exists at all): the tool
// then names its output after the input file stem.
std::wstring namedBlockToken(const std::string& source) {
    const size_t size = source.size();
    size_t pos = 0;
    while (pos <= size) {
        const size_t newline = source.find('\n', pos);
        const size_t lineEnd = newline == std::string::npos ? size : newline;
        if (isAnchorLine(source, pos, lineEnd)) {
            size_t begin = pos;
            while (begin < lineEnd &&
                   (source[begin] == ' ' || source[begin] == '\t')) {
                ++begin;
            }
            const size_t after = begin + 9;  // strlen("@startuml")
            std::string token;
            if (after < lineEnd) {
                const char separator = source[after];
                if (separator == ' ' || separator == '\t') {
                    size_t first = after;
                    while (first < lineEnd && (source[first] == ' ' ||
                                               source[first] == '\t')) {
                        ++first;
                    }
                    size_t last = first;
                    while (last < lineEnd && source[last] != ' ' &&
                           source[last] != '\t' && source[last] != '\r') {
                        ++last;
                    }
                    token.assign(source, first, last - first);
                } else if (separator == '(') {
                    const size_t close = source.find(')', after + 1);
                    if (close != std::string::npos && close <= lineEnd) {
                        token.assign(source, after + 1, close - (after + 1));
                    }
                }
            }
            if (!isSafeArtifactName(token)) return std::wstring();
            return utf8ToWide(token);
        }
        if (newline == std::string::npos) break;
        pos = newline + 1;
    }
    return std::wstring();
}

// Locates the image the tool produced in `workDir`, in the real tool's own
// naming order:
//   1. the canonical `input.<ext>` (an unnamed single block);
//   2. the named-block artifact `<Name>.<ext>` parsed from the anchor line
//      (the tool writes `Flow.png` for `@startuml Flow`);
//   3. the only remaining `*.<ext>` file (lexicographically first when the
//      source produced several, as in multi-block sources).
// Every candidate must be a non-empty regular file. Returns an empty path
// when nothing usable exists.
std::filesystem::path resolveArtifact(const std::filesystem::path& workDir,
                                      const std::string& sourceWithPreamble,
                                      int format, std::error_code& ec) {
    const std::wstring suffix = format == 1 ? L".svg" : L".png";
    auto usable = [](const std::filesystem::path& candidate) {
        std::error_code localEc;
        if (!std::filesystem::is_regular_file(candidate, localEc) || localEc) {
            return false;
        }
        const uintmax_t bytes = std::filesystem::file_size(candidate, localEc);
        return !localEc && bytes > 0;
    };

    const std::filesystem::path canonical = workDir / (L"input" + suffix);
    if (usable(canonical)) return canonical;

    const std::wstring token = namedBlockToken(sourceWithPreamble);
    if (!token.empty()) {
        const std::filesystem::path named = workDir / (token + suffix);
        if (usable(named)) return named;
    }

    std::vector<std::filesystem::path> candidates;
    std::filesystem::directory_iterator it(workDir, ec);
    const std::filesystem::directory_iterator end;
    while (!ec && it != end) {
        const std::filesystem::directory_entry entry = *it;
        it.increment(ec);
        std::error_code typeEc;
        if (!entry.is_regular_file(typeEc) || typeEc) continue;
        std::wstring extension = entry.path().extension().wstring();
        for (wchar_t& c : extension) {
            c = static_cast<wchar_t>(std::towlower(c));
        }
        if (extension != suffix) continue;
        if (!usable(entry.path())) continue;
        candidates.push_back(entry.path());
    }
    if (candidates.empty()) return std::filesystem::path();
    std::sort(candidates.begin(), candidates.end(),
              [](const std::filesystem::path& left,
                 const std::filesystem::path& right) {
                  return left.wstring() < right.wstring();
              });
    return candidates.front();
}

// Removes every image artifact and any staged source from the private
// workDir, so a failed render can never be mistaken for a success and no
// temp source survives a failure.
void scrubWorkDir(const std::filesystem::path& workDir) {
    std::error_code ec;
    std::filesystem::directory_iterator it(workDir, ec);
    const std::filesystem::directory_iterator end;
    while (!ec && it != end) {
        const std::filesystem::directory_entry entry = *it;
        it.increment(ec);
        std::error_code typeEc;
        if (!entry.is_regular_file(typeEc)) continue;
        std::wstring ext = entry.path().extension().wstring();
        for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towlower(c));
        if (ext == L".png" || ext == L".svg" || ext == L".puml") {
            std::error_code removeEc;
            std::filesystem::remove(entry.path(), removeEc);
        }
    }
}

}  // namespace

bool isFenceLanguage(const std::string& lowercasedLanguage) {
    return lowercasedLanguage == "plantuml" || lowercasedLanguage == "puml" ||
           lowercasedLanguage == "pu";
}

std::wstring Tool::describe() const {
    if (!available) return std::wstring();
    if (isJar) return program + L" -jar " + jar;
    return program;
}

Tool resolveTool(const std::wstring& userPath) {
    Tool tool;
    if (userPath.empty() || !fileExists(userPath)) return tool;

    if (hasJarSuffix(userPath)) {
        std::wstring java(MAX_PATH, L'\0');
        DWORD length = SearchPathW(nullptr, L"java.exe", nullptr,
                                   static_cast<DWORD>(java.size()), java.data(),
                                   nullptr);
        if (length == 0) return tool;
        if (length >= java.size()) {
            java.resize(length);
            length = SearchPathW(nullptr, L"java.exe", nullptr,
                                 static_cast<DWORD>(java.size()), java.data(),
                                 nullptr);
            if (length == 0 || length >= java.size()) return tool;
        }
        java.resize(length);
        tool.available = true;
        tool.isJar = true;
        tool.program = java;
        tool.jar = userPath;
        return tool;
    }

    tool.available = true;
    tool.isJar = false;
    tool.program = userPath;
    return tool;
}

Tool resolveToolWithPathSearch(const std::wstring& userPath) {
    if (!userPath.empty()) return resolveTool(userPath);
    // SearchPathW runs its default search order, not PATH alone: the
    // application directory and the current directory are searched before
    // the system directories.

    std::wstring found(MAX_PATH, L'\0');
    DWORD length = SearchPathW(nullptr, L"plantuml.exe", nullptr,
                               static_cast<DWORD>(found.size()),
                               found.data(), nullptr);
    if (length == 0) return Tool();
    if (length >= found.size()) {
        found.resize(length);
        length = SearchPathW(nullptr, L"plantuml.exe", nullptr,
                             static_cast<DWORD>(found.size()),
                             found.data(), nullptr);
        if (length == 0 || length >= found.size()) return Tool();
    }
    found.resize(length);
    return resolveTool(found);
}

bool injectPreamble(std::string& source, const std::string& preambleLines) {
    const size_t size = source.size();
    size_t insertAt = std::string::npos;
    bool anchorAtEof = false;
    size_t pos = 0;
    while (pos <= size) {
        const size_t newline = source.find('\n', pos);
        const size_t lineEnd = newline == std::string::npos ? size : newline;
        if (isAnchorLine(source, pos, lineEnd)) {
            anchorAtEof = newline == std::string::npos;
            insertAt = anchorAtEof ? size : newline + 1;
            break;
        }
        if (newline == std::string::npos) break;
        pos = newline + 1;
    }
    if (insertAt == std::string::npos) return false;
    if (preambleLines.empty()) return true;

    std::string block = preambleLines;
    if (block.back() != '\n') block += '\n';
    if (anchorAtEof) {
        source += '\n';
        insertAt = source.size();
    }
    source.insert(insertAt, block);
    return true;
}

std::string preamble(const std::string& fontFamily, float fontBaseSize,
                     const std::string& textColorHex, bool darkPalette,
                     const std::string& fillColorHex,
                     const std::string& strokeColorHex) {
    char sizeText[64] = {};
    if (fontBaseSize >= 0.0f && fontBaseSize <= 4096.0f &&
        static_cast<float>(static_cast<int>(fontBaseSize)) == fontBaseSize) {
        std::snprintf(sizeText, sizeof(sizeText), "%d",
                      static_cast<int>(fontBaseSize));
    } else {
        std::snprintf(sizeText, sizeof(sizeText), "%.4g",
                      static_cast<double>(fontBaseSize));
    }

    std::string out;
    out += "skinparam backgroundColor transparent\n";
    out += "skinparam shadowing false\n";
    out += "skinparam defaultFontName " + fontFamily + "\n";
    out += "skinparam defaultFontSize " + std::string(sizeText) + "\n";
    out += "skinparam defaultFontColor " + textColorHex + "\n";
    if (!darkPalette) return out;

    // Dark palettes only: PlantUML's built-in light fills and dark strokes
    // would hide the themed text (light on light) or vanish against the page
    // (dark ink on a dark background). Theme every fill and stroke from the
    // supplied colors so the diagram reads on the dark page: fills use the
    // palette surface text is designed to sit on, strokes/borders/lifelines
    // use the accent. PlantUML ignores unknown skinparam names, so the set
    // below is deliberately generous across diagram families.
    out += "skinparam ArrowColor " + strokeColorHex + "\n";
    out += "skinparam ArrowFontColor " + textColorHex + "\n";
    out += "skinparam sequenceArrowColor " + strokeColorHex + "\n";
    out += "skinparam sequenceLifeLineBorderColor " + strokeColorHex + "\n";
    out += "skinparam sequenceGroupBorderColor " + strokeColorHex + "\n";
    out += "skinparam participantBackgroundColor " + fillColorHex + "\n";
    out += "skinparam participantBorderColor " + strokeColorHex + "\n";
    out += "skinparam actorBackgroundColor " + fillColorHex + "\n";
    out += "skinparam actorBorderColor " + strokeColorHex + "\n";
    out += "skinparam classBackgroundColor " + fillColorHex + "\n";
    out += "skinparam classBorderColor " + strokeColorHex + "\n";
    out += "skinparam usecaseBackgroundColor " + fillColorHex + "\n";
    out += "skinparam usecaseBorderColor " + strokeColorHex + "\n";
    out += "skinparam activityBackgroundColor " + fillColorHex + "\n";
    out += "skinparam activityBorderColor " + strokeColorHex + "\n";
    out += "skinparam activityDiamondBackgroundColor " + fillColorHex + "\n";
    out += "skinparam activityDiamondBorderColor " + strokeColorHex + "\n";
    out += "skinparam stateBackgroundColor " + fillColorHex + "\n";
    out += "skinparam stateBorderColor " + strokeColorHex + "\n";
    out += "skinparam objectBackgroundColor " + fillColorHex + "\n";
    out += "skinparam componentBackgroundColor " + fillColorHex + "\n";
    out += "skinparam noteBackgroundColor " + fillColorHex + "\n";
    out += "skinparam noteBorderColor " + strokeColorHex + "\n";
    return out;
}

uint64_t cacheKey(const std::string& source, const std::string& preambleLines,
                  const std::wstring& toolPath, uint64_t toolStamp, int format) {
    uint64_t hash = kFnvOffset;
    fnvText(hash, source);
    fnvText(hash, preambleLines);
    fnvText(hash, toolPath);
    fnvU64(hash, toolStamp);
    fnvU64(hash, static_cast<uint64_t>(static_cast<uint32_t>(format)));
    return hash;
}

uint64_t toolStampFor(const std::wstring& path) {
    if (path.empty()) return 0;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return 0;
    const uint64_t mtime =
        (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
        static_cast<uint64_t>(data.ftLastWriteTime.dwLowDateTime);
    const uint64_t size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) |
                          static_cast<uint64_t>(data.nFileSizeLow);
    uint64_t hash = kFnvOffset;
    fnvU64(hash, mtime);
    fnvU64(hash, size);
    return hash;
}

std::wstring buildCommandLine(const Tool& tool, int format,
                              const std::wstring& outDir,
                              const std::wstring& inputFile) {
    const wchar_t* formatFlag = format == 1 ? L"-tsvg" : L"-tpng";
    std::wstring cmd;
    cmd += L'"';
    cmd += tool.program;
    cmd += L'"';
    if (tool.isJar) {
        cmd += L" -jar \"";
        cmd += tool.jar;
        cmd += L'"';
    }
    cmd += L' ';
    cmd += formatFlag;
    cmd += L" -charset UTF-8 -failfast2 -o \"";
    cmd += outDir;
    cmd += L"\" \"";
    cmd += inputFile;
    cmd += L'"';
    return cmd;
}

bool renderSync(const Tool& tool, const std::string& sourceWithPreamble,
                int format, const std::wstring& workDir, std::wstring& outFile,
                DWORD timeoutMs, std::wstring& error, int* exitCodeOut) {
    outFile.clear();
    error.clear();
    if (exitCodeOut != nullptr) *exitCodeOut = -1;
    const int fmt = format == 1 ? 1 : 0;

    if (!tool.available || tool.program.empty() ||
        (tool.isJar && tool.jar.empty())) {
        error = L"PlantUML tool is not available";
        return false;
    }
    if (timeoutMs == INFINITE) {
        error = L"PlantUML render requires a bounded timeout";
        return false;
    }
    if (workDir.empty()) {
        error = L"PlantUML work directory is empty";
        return false;
    }

    std::error_code ec;
    const std::filesystem::path dir(workDir);
    std::filesystem::create_directories(dir, ec);
    if (ec || !std::filesystem::is_directory(dir, ec) || ec) {
        error = L"Cannot prepare the PlantUML work directory";
        return false;
    }

    const std::filesystem::path inputPath = dir / L"input.puml";
    {
        std::ofstream out(inputPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = L"Cannot write the PlantUML input file";
            return false;
        }
        out.write(sourceWithPreamble.data(),
                  static_cast<std::streamsize>(sourceWithPreamble.size()));
        out.flush();
        if (!out) {
            error = L"Cannot write the PlantUML input file";
            return false;
        }
    }

    std::wstring command =
        buildCommandLine(tool, fmt, workDir, inputPath.wstring());
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi)) {
        const DWORD lastError = GetLastError();
        scrubWorkDir(dir);
        error = L"Cannot start the PlantUML tool (error " +
                std::to_wstring(lastError) + L")";
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    bool timedOut = false;
    DWORD exitCode = 1;
    if (waitResult == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        timedOut = true;
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);  // reap the killed process
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (timedOut) {
        scrubWorkDir(dir);
        error = L"PlantUML timed out after " + std::to_wstring(timeoutMs) + L" ms";
        return false;
    }
    if (exitCode != 0) {
        if (exitCodeOut != nullptr) *exitCodeOut = static_cast<int>(exitCode);
        scrubWorkDir(dir);
        error = L"PlantUML exited with code " + std::to_wstring(exitCode);
        return false;
    }

    const std::filesystem::path artifact =
        resolveArtifact(dir, sourceWithPreamble, fmt, ec);
    if (artifact.empty()) {
        if (exitCodeOut != nullptr) *exitCodeOut = static_cast<int>(exitCode);
        scrubWorkDir(dir);
        error = L"PlantUML produced no usable image";
        return false;
    }
    if (exitCodeOut != nullptr) *exitCodeOut = 0;
    outFile = artifact.wstring();
    return true;
}

}  // namespace plantuml