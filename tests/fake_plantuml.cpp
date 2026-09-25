// fake_plantuml.cpp - CLI-compatible test double for the PlantUML tool.
//
// Mimics the subset of the real `plantuml` command line that Tinta will
// invoke:  plantuml -tpng|-tsvg -charset <enc> -failfast2 -o <outdir> <input.puml>
// Flags are accepted in any order; the last positional argument is the input
// file. On success the tool copies the matching fixture from
// tests/fixtures/plantuml/ into <outdir>/<name>.png|svg (exit 0), creating
// <outdir> when missing, just like the real CLI. The name follows the real
// CLI's block rule: a name on the first `@startuml` line (` Name` or
// `(Name)` form) is written as `<Name>.<ext>`, an unnamed block as
// `<input-stem>.<ext>`.
//
// Test switches (environment variables):
//   TINTA_FAKE_PLANTUML_EXIT=<code>        exit with that code, write nothing
//   TINTA_FAKE_PLANTUML_FAIL_WITH_IMAGE=1  write fake-error.png into -o, exit 200
//                                          (mirrors the real CLI's error image)
//   TINTA_FAKE_PLANTUML_SLEEP_MS=<n>       sleep n ms before any writing
//   TINTA_FAKE_PLANTUML_LOG=<path>         append one line per invocation with
//                                          the full joined command line (spawn
//                                          counting / command-line assertions)
//
// The fixture directory is located via TINTA_FAKE_PLANTUML_FIXTURE_DIR first,
// then by walking up from the current directory and from the directory of
// argv[0] (the exe lives in build/Release, two levels below the repo root).
//
// Deliberately minimal: standard C++17 only, no project sources, no Windows
// APIs, no network, and no writes outside the -o directory (except the log).

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::string envString(const char* name) {
    const char* raw = std::getenv(name);
    return raw != nullptr ? std::string(raw) : std::string();
}

// Returns `start` followed by each of its parent directories, at most `levels`.
std::vector<std::filesystem::path> ancestryOf(std::filesystem::path start, int levels) {
    std::vector<std::filesystem::path> chain;
    for (int i = 0; i < levels; ++i) {
        chain.push_back(start);
        const std::filesystem::path parent = start.parent_path();
        if (parent == start) {
            break;
        }
        start = parent;
    }
    return chain;
}

// Finds tests/fixtures/plantuml/<name>, or returns an empty path when the
// fixture cannot be located from any known base.
std::filesystem::path locateFixture(const std::string& argv0, const std::string& name) {
    namespace fs = std::filesystem;
    std::error_code ec;

    const std::string overrideDir = envString("TINTA_FAKE_PLANTUML_FIXTURE_DIR");
    if (!overrideDir.empty()) {
        const fs::path candidate = fs::path(overrideDir) / fs::path(name);
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
    }

    std::vector<fs::path> roots;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) {
        for (const fs::path& p : ancestryOf(cwd, 6)) {
            roots.push_back(p);
        }
    }
    if (!argv0.empty()) {
        const fs::path exeDir = fs::path(argv0).parent_path();
        for (const fs::path& p : ancestryOf(exeDir, 6)) {
            roots.push_back(p);
        }
    }
    for (const fs::path& root : roots) {
        const fs::path candidate =
            root / fs::path("tests/fixtures/plantuml") / fs::path(name);
        if (fs::exists(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

bool copyOver(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::error_code ec;
    std::filesystem::copy_file(source, target,
                               std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

void appendLog(const std::string& logPath, const std::vector<std::string>& args) {
    std::ofstream log(logPath, std::ios::app);
    if (!log) {
        return;
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            log << ' ';
        }
        log << args[i];
    }
    log << '\n';
}

// The real CLI names a block's output after the block: when the first
// `@startuml` line carries a name (` Name` or `(Name)` form) the tool writes
// `<Name>.<ext>`. Returns an empty string when that block is unnamed (or the
// input cannot be read), which selects the input-stem name.
std::string sourceBlockName(const std::filesystem::path& input) {
    std::ifstream in(input, std::ios::binary);
    if (!in) return {};
    std::string line;
    while (std::getline(in, line)) {
        size_t begin = 0;
        while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) {
            ++begin;
        }
        if (line.size() - begin < 9) continue;
        std::string head = line.substr(begin, 9);
        for (char& c : head) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (head != "@startuml") continue;
        const size_t after = begin + 9;
        std::string token;
        if (after < line.size()) {
            const char separator = line[after];
            if (separator == ' ' || separator == '\t') {
                size_t first = after;
                while (first < line.size() &&
                       (line[first] == ' ' || line[first] == '\t')) {
                    ++first;
                }
                size_t last = first;
                while (last < line.size() && line[last] != ' ' &&
                       line[last] != '\t' && line[last] != '\r') {
                    ++last;
                }
                token = line.substr(first, last - first);
            } else if (separator == '(') {
                const size_t close = line.find(')', after + 1);
                if (close != std::string::npos) {
                    token = line.substr(after + 1, close - after - 1);
                }
            }
        }
        while (!token.empty() && (token.back() == '\r' || token.back() == ' ' ||
                                  token.back() == '\t')) {
            token.pop_back();
        }
        for (char c : token) {
            if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|') {
                return {};
            }
        }
        return token;
    }
    return {};
}

} // namespace

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;

    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    // Record the spawn before anything else so callers can count invocations
    // even when a switch makes the tool exit early.
    const std::string logPath = envString("TINTA_FAKE_PLANTUML_LOG");
    if (!logPath.empty()) {
        appendLog(logPath, args);
    }

    // Parse the supported flag subset (order is free, last positional wins).
    std::string format = "png"; // the real CLI defaults to PNG output
    std::string outDir;
    std::string input;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-tpng") {
            format = "png";
        } else if (a == "-tsvg") {
            format = "svg";
        } else if (a == "-charset") {
            if (i + 1 >= args.size()) {
                std::cerr << "fake_plantuml: -charset requires an argument\n";
                return 1;
            }
            ++i; // accepted and ignored (encoding only matters to the real CLI)
        } else if (a == "-o") {
            if (i + 1 >= args.size()) {
                std::cerr << "fake_plantuml: -o requires an argument\n";
                return 1;
            }
            outDir = args[++i];
        } else if (a == "-failfast2") {
            // Accepted for fidelity; the fake always behaves the same way.
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "fake_plantuml: unsupported flag " << a << "\n";
            return 1;
        } else {
            input = a;
        }
    }
    if (outDir.empty()) {
        std::cerr << "fake_plantuml: missing -o <outdir>\n";
        return 1;
    }
    if (input.empty()) {
        std::cerr << "fake_plantuml: missing input file path\n";
        return 1;
    }

    // Sleep before writing (lets timeout tests hold the caller).
    const std::string sleepMs = envString("TINTA_FAKE_PLANTUML_SLEEP_MS");
    if (!sleepMs.empty()) {
        const long ms = std::strtol(sleepMs.c_str(), nullptr, 10);
        if (ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }

    // Hard exit switch: produce nothing at all.
    const std::string forcedExit = envString("TINTA_FAKE_PLANTUML_EXIT");
    if (!forcedExit.empty()) {
        return static_cast<int>(std::strtol(forcedExit.c_str(), nullptr, 10));
    }

    const fs::path out(outDir);
    std::error_code ec;
    if (!fs::exists(out, ec)) {
        fs::create_directories(out, ec);
        if (ec) {
            std::cerr << "fake_plantuml: cannot create output directory\n";
            return 1;
        }
    }

    // Error-image mode: mirror the real CLI writing an image, then exiting 200.
    if (envString("TINTA_FAKE_PLANTUML_FAIL_WITH_IMAGE") == "1") {
        const fs::path fixture = locateFixture(argc > 0 ? argv[0] : "", "fake-diagram.png");
        if (fixture.empty()) {
            std::cerr << "fake_plantuml: fixture fake-diagram.png not found\n";
            return 1;
        }
        if (!copyOver(fixture, out / fs::path("fake-error.png"))) {
            std::cerr << "fake_plantuml: cannot write fake-error.png\n";
            return 1;
        }
        return 200;
    }

    // Success: <outdir>/<input-stem>.<format> copied from the fixture.
    const std::string fixtureName = format == "svg" ? "fake-diagram.svg" : "fake-diagram.png";
    const fs::path fixture = locateFixture(argc > 0 ? argv[0] : "", fixtureName);
    if (fixture.empty()) {
        std::cerr << "fake_plantuml: fixture " << fixtureName << " not found\n";
        return 1;
    }
    const std::string stem = fs::path(input).stem().string();
    const std::string blockName = sourceBlockName(fs::path(input));
    const fs::path target =
        out / fs::path((blockName.empty() ? stem : blockName) + "." + format);
    if (!copyOver(fixture, target)) {
        std::cerr << "fake_plantuml: cannot write " << target.string() << "\n";
        return 1;
    }
    return 0;
}
