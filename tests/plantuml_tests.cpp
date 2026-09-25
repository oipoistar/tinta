// PlantUML core-module unit suite. Drives the fake CLI double built from
// tests/fake_plantuml.cpp - no real PlantUML and no Java are required.
//
// Paths are injected by CMake: TINTA_FAKE_PLANTUML is the built fake tool
// executable, TINTA_PLANTUML_SOURCE is src/plantuml.cpp (read by the
// no-network assertion). The suite is a plain check(bool, msg) harness in
// the style of tests/mermaid_tests.cpp and prints
// "All PlantUML tests passed" on success.
//
// Passing --expect-failure makes the harness fail on purpose; ctest runs
// that mode as a separate WILL_FAIL test so the checker itself is proven.
//
// The suite must run through the portable driver
// (tests/run_plantuml_tests.cmake), which stages this binary in a temp
// folder beside its own settings.ini and passes --portable-test. The
// settings round-trip writes that file; a direct run refuses instead, so
// the user's %APPDATA%\Tinta is never touched.

#include "plantuml.h"
#include "plantuml_queue.h"
#include "settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#ifndef TINTA_FAKE_PLANTUML
#error "TINTA_FAKE_PLANTUML must point at the fake_plantuml target file"
#endif
#ifndef TINTA_PLANTUML_SOURCE
#error "TINTA_PLANTUML_SOURCE must point at src/plantuml.cpp"
#endif

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    failures++;
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), length);
    return out;
}

std::string toNarrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), out.data(), length,
                        nullptr, nullptr);
    return out;
}

// Saves an environment variable, applies a new value for the scope, then
// restores the previous state (including removing a variable that was unset).
class ScopedEnv {
public:
    ScopedEnv(const wchar_t* name, const wchar_t* value) : name_(name) {
        wchar_t previous[32767] = {};
        const DWORD length = GetEnvironmentVariableW(name, previous, 32767);
        had_ = length > 0 && length < 32767;
        if (had_) previous_.assign(previous, length);
        SetEnvironmentVariableW(name, value);
    }

    ~ScopedEnv() {
        SetEnvironmentVariableW(name_.c_str(), had_ ? previous_.c_str() : nullptr);
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::wstring name_;
    std::wstring previous_;
    bool had_ = false;
};

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

// True only when CTest's portable driver staged this executable: the exe
// sits in the temp TEST_DIR next to the driver's settings.ini, the working
// directory is that folder, and --portable-test was passed. Anything else
// (for example a direct run from build\Release) refuses so the settings
// round-trip can never reach %APPDATA%\Tinta.
bool inIsolatedPortableFolder() {
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return false;
    const std::filesystem::path dir = std::filesystem::path(exe).parent_path();
    const std::string marker = readTextFile(dir / "settings.ini");
    if (marker != "; Isolated plantuml test configuration\n" &&
        marker != "; Isolated plantuml test configuration\r\n") {
        return false;
    }
    std::error_code ec;
    return std::filesystem::equivalent(dir, std::filesystem::current_path(),
                                       ec) &&
           !ec;
}



std::filesystem::path scratchRoot() {
    wchar_t base[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, base);
    return std::filesystem::path(base) /
           (L"tinta-plantuml-tests-" + std::to_wstring(GetCurrentProcessId()));
}

int scratchCounter = 0;

std::filesystem::path freshScratch(const wchar_t* label) {
    const std::filesystem::path dir =
        scratchRoot() / (std::wstring(label) + L"-" +
                         std::to_wstring(++scratchCounter));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return dir;
}

int countImages(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return 0;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        std::error_code typeEc;
        if (!entry.is_regular_file(typeEc)) continue;
        std::wstring ext = entry.path().extension().wstring();
        for (wchar_t& c : ext) c = static_cast<wchar_t>(std::towlower(c));
        if (ext == L".png" || ext == L".svg") ++count;
    }
    return count;
}

// Minimal bounded spawn helper used only to prove a failure scenario really
// produced an artifact before renderSync cleans it up.
bool runProcess(const std::wstring& commandLine, DWORD timeoutMs, DWORD& exitCode) {
    std::vector<wchar_t> cmd(commandLine.begin(), commandLine.end());
    cmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    exitCode = 1;
    if (wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    } else {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return wait == WAIT_OBJECT_0;
}

// ------------------------------------------------------------- queue helpers

std::vector<std::string> readLogLines(const std::filesystem::path& logPath) {
    std::vector<std::string> lines;
    std::ifstream in(logPath, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

// Bounded polling with a clear failure message: the queue tests must never
// hang when an expected render or retry does not arrive.
template <typename Predicate>
bool pollUntil(Predicate predicate, int deadlineMs, const char* failureMessage) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(deadlineMs);
    for (;;) {
        if (predicate()) return true;
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(false, failureMessage);
    return false;
}

const char kQueueSource[] = "@startuml\nAlice -> Bob: queued\n@enduml\n";

const std::wstring kFakeToolPath = toWide(TINTA_FAKE_PLANTUML);

// ---------------------------------------------------------------- language gate

void testLanguageGate() {
    check(plantuml::isFenceLanguage("plantuml"), "plantuml is a fence language");
    check(plantuml::isFenceLanguage("puml"), "puml is a fence language");
    check(plantuml::isFenceLanguage("pu"), "pu is a fence language");
    check(!plantuml::isFenceLanguage("mermaid"), "mermaid is not a plantuml fence");
    check(!plantuml::isFenceLanguage(""), "empty language is not a plantuml fence");
    check(!plantuml::isFenceLanguage("PlantUML"),
          "the gate expects an already-lowercased language");
    check(!plantuml::isFenceLanguage("plantuml2"), "lookalike language is rejected");
}

// ---------------------------------------------------------- preamble injection

void testPreambleInjection() {
    const std::string pre = "skinparam shadowing false\n";

    std::string src = "@startuml\nAlice -> Bob: hi\n@enduml\n";
    check(plantuml::injectPreamble(src, pre), "anchor line is found");
    check(src.compare(0, 9, "@startuml") == 0, "source still starts with the anchor");
    const size_t anchorEnd = src.find('\n') + 1;
    check(src.compare(anchorEnd, pre.size(), pre) == 0,
          "preamble lands immediately after the anchor line");
    check(src.find("skinparam") > src.find("@startuml"),
          "preamble never precedes @startuml");
    check(src.find("Alice -> Bob") > src.find("skinparam shadowing false"),
          "diagram body follows the preamble");

    std::string crlf = "  @StartUml  \r\nBob -> Alice: yo\r\n@enduml\r\n";
    check(plantuml::injectPreamble(crlf, pre), "indented mixed-case anchor is found");
    check(crlf.find("@StartUml  \r\nskinparam shadowing false\n") != std::string::npos,
          "preamble follows a CRLF anchor line");

    std::string named = "@startuml Flow\nA -> B\n";
    check(plantuml::injectPreamble(named, pre), "named block anchor is found");
    check(named.compare(0, 14, "@startuml Flow") == 0 &&
          named.find("skinparam") == 15,
          "preamble follows a named block anchor");

    std::string paren = "@startuml(Flow)\nA -> B\n";
    check(plantuml::injectPreamble(paren, pre),
          "parenthesized block anchor is found");
    check(paren.compare(0, 15, "@startuml(Flow)") == 0 &&
          paren.find("skinparam") == 16,
          "preamble follows a parenthesized block anchor");

    std::string json = "@startjson\n{ \"a\": 1 }\n";
    const std::string jsonBefore = json;
    check(!plantuml::injectPreamble(json, pre),
          "a non-UML start tag is not an anchor");
    check(json == jsonBefore, "a non-UML start tag leaves the source untouched");

    std::string atEof = "@startuml";
    check(plantuml::injectPreamble(atEof, pre), "anchor alone still injects");
    check(atEof == "@startuml\nskinparam shadowing false\n",
          "an anchor at EOF is terminated before the preamble");

    std::string none = "Alice -> Bob\n@enduml\n";
    const std::string before = none;
    check(!plantuml::injectPreamble(none, pre), "no anchor returns false");
    check(none == before, "a failed injection leaves the source untouched");

    std::string lookalike = "@startumlx\nA -> B\n";
    const std::string lookalikeBefore = lookalike;
    check(!plantuml::injectPreamble(lookalike, pre), "@startumlx is not an anchor");
    check(lookalike == lookalikeBefore, "a lookalike token leaves the source untouched");

    std::string multi =
        "@startuml\nA -> B\n@enduml\n@startuml\nC -> D\n@enduml\n";
    check(plantuml::injectPreamble(multi, pre), "multi-diagram anchor is found");
    const size_t first = multi.find("skinparam");
    check(first != std::string::npos &&
          multi.find("skinparam", first + 1) == std::string::npos,
          "only the first block receives the preamble");

    std::string emptyPre = "@startuml\nA -> B\n";
    const std::string emptyPreBefore = emptyPre;
    check(plantuml::injectPreamble(emptyPre, ""),
          "an empty preamble is a no-op that still finds the anchor");
    check(emptyPre == emptyPreBefore, "an empty preamble changes nothing");
}

// ------------------------------------------------------------ preamble content

void testPreambleContent() {
    const std::string lightExpected =
        "skinparam backgroundColor transparent\n"
        "skinparam shadowing false\n"
        "skinparam defaultFontName Segoe UI\n"
        "skinparam defaultFontSize 14\n"
        "skinparam defaultFontColor 334455\n";
    const std::string p = plantuml::preamble("Segoe UI", 14.0f, "334455",
                                             false, "112233", "445566");
    check(p == lightExpected,
          "a light palette returns the documented five-line preamble "
          "byte for byte");
    check(p.find("BackgroundColor") == std::string::npos &&
          p.find("BorderColor") == std::string::npos &&
          p.find("ArrowColor") == std::string::npos,
          "a light palette never themes shapes or strokes");

    const size_t background = p.find("backgroundColor");
    const size_t shadowing = p.find("shadowing");
    const size_t fontName = p.find("defaultFontName");
    const size_t fontSize = p.find("defaultFontSize");
    const size_t fontColor = p.find("defaultFontColor");
    check(background != std::string::npos && shadowing != std::string::npos &&
          fontName != std::string::npos && fontSize != std::string::npos &&
          fontColor != std::string::npos && background < shadowing &&
          shadowing < fontName && fontName < fontSize && fontSize < fontColor,
          "preamble lines keep their documented order");

    const std::string fractional = plantuml::preamble("Cascadia Mono", 13.5f,
                                                      "aabbcc", false, "", "");
    check(fractional.find("skinparam defaultFontSize 13.5\n") != std::string::npos,
          "a fractional font size keeps its fraction");

    const std::string dark = plantuml::preamble("Segoe UI", 14.0f, "334455",
                                                true, "112233", "445566");
    check(dark.find(lightExpected) == 0,
          "a dark palette keeps the light preamble as its byte-identical "
          "prefix");
    const char* darkLines[] = {
        "skinparam ArrowColor 445566\n",
        "skinparam ArrowFontColor 334455\n",
        "skinparam sequenceArrowColor 445566\n",
        "skinparam sequenceLifeLineBorderColor 445566\n",
        "skinparam sequenceGroupBorderColor 445566\n",
        "skinparam participantBackgroundColor 112233\n",
        "skinparam participantBorderColor 445566\n",
        "skinparam actorBackgroundColor 112233\n",
        "skinparam actorBorderColor 445566\n",
        "skinparam classBackgroundColor 112233\n",
        "skinparam classBorderColor 445566\n",
        "skinparam usecaseBackgroundColor 112233\n",
        "skinparam usecaseBorderColor 445566\n",
        "skinparam activityBackgroundColor 112233\n",
        "skinparam activityBorderColor 445566\n",
        "skinparam activityDiamondBackgroundColor 112233\n",
        "skinparam activityDiamondBorderColor 445566\n",
        "skinparam stateBackgroundColor 112233\n",
        "skinparam stateBorderColor 445566\n",
        "skinparam objectBackgroundColor 112233\n",
        "skinparam componentBackgroundColor 112233\n",
        "skinparam noteBackgroundColor 112233\n",
        "skinparam noteBorderColor 445566\n",
    };
    bool darkComplete = true;
    for (const char* line : darkLines) {
        if (dark.find(line) == std::string::npos) darkComplete = false;
    }
    check(darkComplete,
          "a dark palette themes every documented fill, border and stroke");
    check(plantuml::preamble("Segoe UI", 14.0f, "334455", false, "112233",
                             "445566") == lightExpected,
          "a light palette ignores the dark-only color arguments");
    check(plantuml::preamble("Segoe UI", 14.0f, "334455", true, "112233",
                             "445566") !=
              plantuml::preamble("Segoe UI", 14.0f, "334455", true, "aaaaaa",
                                 "445566"),
          "changing a dark fill color changes the dark preamble");
}

// -------------------------------------------------------------------- cache key

void testCacheKey() {
    const std::string source = "@startuml\nA -> B\n@enduml\n";
    const std::string pre =
        plantuml::preamble("Segoe UI", 14.0f, "112233", false, "", "");
    const std::wstring tool = L"C:/tools/plantuml.exe";
    const uint64_t baseline = plantuml::cacheKey(source, pre, tool, 4242, 0);

    check(baseline != 0, "cache key is non-zero");
    check(baseline == plantuml::cacheKey(source, pre, tool, 4242, 0),
          "cache key is stable for equal inputs");
    check(baseline != plantuml::cacheKey(source + "\n", pre, tool, 4242, 0),
          "changing the source changes the key");
    check(baseline != plantuml::cacheKey(source, pre + "extra", tool, 4242, 0),
          "changing the preamble changes the key");
    check(baseline != plantuml::cacheKey(source, pre, L"C:/tools/other.exe", 4242, 0),
          "changing the tool path changes the key");
    check(baseline != plantuml::cacheKey(source, pre, tool, 4243, 0),
          "changing the tool stamp changes the key");
    check(baseline != plantuml::cacheKey(source, pre, tool, 4242, 1),
          "changing the format changes the key");

    const uint64_t stamp = plantuml::toolStampFor(kFakeToolPath);
    check(stamp != 0, "tool stamp reads the fake tool file");
    check(stamp == plantuml::toolStampFor(kFakeToolPath), "tool stamp is stable");
    check(plantuml::toolStampFor(L"C:/definitely/missing/tool.exe") == 0,
          "missing tool files stamp as zero");
}

// -------------------------------------------------------------- command line

void testCommandLine() {
    plantuml::Tool exe;
    exe.available = true;
    exe.program = L"C:\\tools\\plantuml.exe";

    const std::wstring png =
        plantuml::buildCommandLine(exe, 0, L"C:\\out", L"C:\\in\\input.puml");
    check(png == L"\"C:\\tools\\plantuml.exe\" -tpng -charset UTF-8 "
                 L"-failfast2 -o \"C:\\out\" \"C:\\in\\input.puml\"",
          "exe PNG command line is exact");

    const std::wstring svg =
        plantuml::buildCommandLine(exe, 1, L"C:\\out", L"C:\\in\\input.puml");
    check(svg == L"\"C:\\tools\\plantuml.exe\" -tsvg -charset UTF-8 "
                 L"-failfast2 -o \"C:\\out\" \"C:\\in\\input.puml\"",
          "exe SVG command line is exact");

    plantuml::Tool jar;
    jar.available = true;
    jar.isJar = true;
    jar.program = L"C:\\Java\\bin\\java.exe";
    jar.jar = L"C:\\tools\\plantuml.jar";
    const std::wstring jarSvg =
        plantuml::buildCommandLine(jar, 1, L"C:\\out", L"C:\\in\\input.puml");
    check(jarSvg == L"\"C:\\Java\\bin\\java.exe\" -jar \"C:\\tools\\plantuml.jar\" "
                    L"-tsvg -charset UTF-8 -failfast2 -o \"C:\\out\" "
                    L"\"C:\\in\\input.puml\"",
          "jar SVG command line is exact");
    check(jarSvg.find(L"-jar") != std::wstring::npos &&
          jarSvg.find(L"java.exe") != std::wstring::npos,
          "jar command line spawns java with -jar");

    check(exe.describe() == L"C:\\tools\\plantuml.exe", "exe describe is the path");
    check(jar.describe() == L"C:\\Java\\bin\\java.exe -jar C:\\tools\\plantuml.jar",
          "jar describe names java and the jar");
    plantuml::Tool unavailable;
    check(unavailable.describe().empty(), "an unavailable tool describes empty");
}

// --------------------------------------------------------------- render success

void testRenderSuccess() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    check(tool.available && !tool.isJar, "fake tool resolves as an available exe");

    const std::string source = "@startuml\nAlice -> Bob: hello\n@enduml\n";

    for (int format = 0; format <= 1; ++format) {
        const std::filesystem::path dir = freshScratch(format == 0 ? L"png" : L"svg");
        std::wstring out;
        std::wstring error;
        const bool ok = plantuml::renderSync(tool, source, format, dir.wstring(), out,
                                             15000, error);
        check(ok, format == 0 ? "PNG render succeeds" : "SVG render succeeds");
        if (!ok) {
            std::cerr << "  renderSync error: " << toNarrow(error) << '\n';
            continue;
        }
        check(error.empty(), "a successful render clears the error");
        check(!out.empty(), "a successful render sets outFile");
        check(std::filesystem::exists(out), "outFile exists on disk");
        check(std::filesystem::file_size(out) > 0, "outFile is non-empty");
        const std::wstring expectedName = format == 0 ? L"input.png" : L"input.svg";
        check(std::filesystem::path(out).filename().wstring() == expectedName,
              "outFile uses the format's canonical name");

        const std::filesystem::path staged = dir / L"input.puml";
        check(std::filesystem::exists(staged), "the staged source is written");
        std::ifstream stagedIn(staged, std::ios::binary);
        const std::string stagedText((std::istreambuf_iterator<char>(stagedIn)),
                                     std::istreambuf_iterator<char>());
        check(stagedText == source, "the staged source is byte-identical");
        check(stagedText.size() < 3 ||
              !(static_cast<unsigned char>(stagedText[0]) == 0xEF &&
                static_cast<unsigned char>(stagedText[1]) == 0xBB &&
                static_cast<unsigned char>(stagedText[2]) == 0xBF),
              "the staged source has no UTF-8 BOM");

        if (format == 1) {
            std::ifstream svg(out, std::ios::binary);
            const std::string svgText((std::istreambuf_iterator<char>(svg)),
                                      std::istreambuf_iterator<char>());
            check(svgText.find("TINTA-FAKE-PLANTUML") != std::string::npos,
                  "the SVG carries the fake sentinel");
        }
    }
}

// --------------------------------------------------------------- render failure

void testRenderFailureExitCode() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::string source = "@startuml\nA -> B\n@enduml\n";
    const ScopedEnv exitEnv(L"TINTA_FAKE_PLANTUML_EXIT", L"100");

    const std::filesystem::path dir = freshScratch(L"fail-exit");
    std::wstring out;
    std::wstring error;
    const bool ok = plantuml::renderSync(tool, source, 0, dir.wstring(), out, 15000,
                                         error);
    check(!ok, "exit 100 is reported as failure");
    check(!error.empty(), "exit 100 carries an error");
    check(out.empty(), "exit 100 leaves outFile empty");
    check(countImages(dir) == 0, "no image artifact survives a failed render");
    check(!std::filesystem::exists(dir / L"input.puml"),
          "the staged source is removed on failure");
}

void testRenderFailureWithImage() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::string source = "@startuml\nA -> B\n@enduml\n";
    const ScopedEnv imageEnv(L"TINTA_FAKE_PLANTUML_FAIL_WITH_IMAGE", L"1");

    // Control: prove the fake really writes its error image in this
    // environment, so the cleanup assertion below is not vacuous.
    const std::filesystem::path control = freshScratch(L"fail-image-control");
    std::filesystem::create_directories(control);
    {
        std::ofstream controlInput(control / L"input.puml", std::ios::binary);
        controlInput << "@startuml\n@enduml\n";
    }
    const std::wstring controlCmd =
        L"\"" + kFakeToolPath + L"\" -tpng -charset UTF-8 -failfast2 -o \"" +
        control.wstring() + L"\" \"" + (control / L"input.puml").wstring() + L"\"";
    DWORD controlExit = 0;
    const bool controlRan = runProcess(controlCmd, 15000, controlExit);
    check(controlRan, "control run of the fake tool completes");
    check(controlExit == 200, "control run exits 200 as the fail switch documents");
    check(std::filesystem::exists(control / L"fake-error.png"),
          "control proves the tool wrote an error image");

    const std::filesystem::path dir = freshScratch(L"fail-image");
    std::wstring out;
    std::wstring error;
    const bool ok = plantuml::renderSync(tool, source, 0, dir.wstring(), out, 15000,
                                         error);
    check(!ok, "an image plus a non-zero exit is still a failure");
    check(!error.empty(), "fail-with-image carries an error");
    check(out.empty(), "fail-with-image leaves outFile empty");
    check(!std::filesystem::exists(dir / L"input.png"),
          "no input.png survives a failed render");
    check(countImages(dir) == 0,
          "every image artifact is scrubbed from the failed workDir");
}

void testRenderTimeout() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::string source = "@startuml\nA -> B\n@enduml\n";
    const ScopedEnv sleepEnv(L"TINTA_FAKE_PLANTUML_SLEEP_MS", L"5000");

    const std::filesystem::path dir = freshScratch(L"timeout");
    std::wstring out;
    std::wstring error;
    const auto started = std::chrono::steady_clock::now();
    const bool ok =
        plantuml::renderSync(tool, source, 0, dir.wstring(), out, 500, error);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    check(!ok, "a timeout is reported as failure");
    check(!error.empty(), "a timeout carries an error");
    check(elapsed < 2000, "the 500 ms budget is enforced below the 5 s sleep");
    check(countImages(dir) == 0, "no image artifact survives a timeout");
}

// ----------------------------------------------------------------- tool resolve

void testResolveTool() {
    check(!plantuml::resolveTool(L"").available, "an empty path is unavailable");
    check(!plantuml::resolveTool(L"C:/definitely/missing/plantuml.exe").available,
          "a missing exe is unavailable");
    check(!plantuml::resolveTool(L"C:/definitely/missing/plantuml.jar").available,
          "a missing jar is unavailable");

    const plantuml::Tool exe = plantuml::resolveTool(kFakeToolPath);
    check(exe.available, "an existing exe path resolves");
    check(!exe.isJar, "an exe path is not classified as a jar");
    check(exe.program == kFakeToolPath, "the exe program is the configured path");

    // Jar classification: java.exe comes from SearchPathW, so prepend a temp
    // directory holding a copy of the fake renamed java.exe and restore PATH.
    const std::filesystem::path shimDir = freshScratch(L"java-shim");
    std::filesystem::create_directories(shimDir);
    std::filesystem::copy_file(kFakeToolPath, shimDir / L"java.exe",
                               std::filesystem::copy_options::overwrite_existing);
    const std::filesystem::path jarPath = shimDir / L"plantuml.jar";
    {
        std::ofstream jarFile(jarPath, std::ios::binary);
        jarFile << "PK fake jar";
    }

    wchar_t previousPath[32767] = {};
    const DWORD pathLength = GetEnvironmentVariableW(L"PATH", previousPath, 32767);
    const bool hadPath = pathLength > 0 && pathLength < 32767;
    const std::wstring savedPath =
        hadPath ? std::wstring(previousPath, pathLength) : std::wstring();
    const std::wstring shimmedPath = shimDir.wstring() + L";" + savedPath;
    SetEnvironmentVariableW(L"PATH", shimmedPath.c_str());
    const plantuml::Tool jar = plantuml::resolveTool(jarPath.wstring());
    SetEnvironmentVariableW(L"PATH", hadPath ? savedPath.c_str() : nullptr);

    check(jar.available, "a jar with java on PATH resolves");
    check(jar.isJar, "a .jar path is classified as a jar");
    check(!jar.program.empty(), "jar resolution provides a java program");
    check(jar.jar == jarPath.wstring(), "the jar path is preserved");
    check(jar.program == (shimDir / L"java.exe").wstring(),
          "SearchPathW picked the java.exe from PATH");
}

// ------------------------------------------------------- tool path search

void testResolveToolWithPathSearch() {
    // A non-empty path goes straight through resolveTool: an existing exe
    // resolves exactly like the direct call, a missing path stays
    // unavailable (an explicit choice never silently falls back to PATH).
    const plantuml::Tool direct = plantuml::resolveTool(kFakeToolPath);
    const plantuml::Tool explicitTool =
        plantuml::resolveToolWithPathSearch(kFakeToolPath);
    check(explicitTool.available, "an explicit exe path is available");
    check(explicitTool.program == direct.program &&
          explicitTool.jar == direct.jar &&
          explicitTool.isJar == direct.isJar,
          "an explicit path resolves exactly like resolveTool");
    check(!plantuml::resolveToolWithPathSearch(
               L"C:/definitely/missing/plantuml.exe")
               .available,
          "an explicit missing path stays unavailable");

    // PATH branch 1: no plantuml.exe anywhere in the search path. Replacing
    // PATH with an empty temp dir keeps the probe deterministic (the control
    // below proves the rest of the search path carries none either).
    const std::filesystem::path emptyDir = freshScratch(L"path-empty");
    std::filesystem::create_directories(emptyDir);
    {
        const ScopedEnv pathEnv(L"PATH", emptyDir.c_str());
        wchar_t probe[MAX_PATH]{};
        check(SearchPathW(nullptr, L"plantuml.exe", nullptr, MAX_PATH, probe,
                          nullptr) == 0,
              "control: no plantuml.exe in the search path");
        const plantuml::Tool none = plantuml::resolveToolWithPathSearch(L"");
        check(!none.available,
              "an empty path without plantuml.exe is unavailable");
    }

    // PATH branch 2: a temp dir holding a copy of the fake tool renamed
    // plantuml.exe is found through PATH and resolves as an exe tool.
    const std::filesystem::path shimDir = freshScratch(L"path-shim");
    std::filesystem::create_directories(shimDir);
    std::filesystem::copy_file(kFakeToolPath, shimDir / L"plantuml.exe",
                               std::filesystem::copy_options::overwrite_existing);
    {
        const ScopedEnv pathEnv(L"PATH", shimDir.c_str());
        const plantuml::Tool fromPath = plantuml::resolveToolWithPathSearch(L"");
        check(fromPath.available, "plantuml.exe on PATH resolves");
        check(!fromPath.isJar, "a PATH plantuml.exe is an exe tool");
        check(fromPath.program == (shimDir / L"plantuml.exe").wstring(),
              "the PATH plantuml.exe becomes the program");
    }
}

// -------------------------------------------------------- settings round-trip

void testSettingsRoundTrip() {
    // main() guarantees this runs inside CTest's isolated portable folder,
    // so saveSettings/loadSettings write the temp settings.ini staged by
    // tests/run_plantuml_tests.cmake - never %APPDATA%\Tinta.
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path exeDir =
        std::filesystem::path(exe).parent_path();
    check(std::filesystem::path(getSettingsPath()).parent_path() == exeDir,
          "settings resolve inside the isolated portable folder");

    const std::wstring tempPath =
        (scratchRoot() / L"round-trip" / L"PlantUML Tool.exe").wstring();
    Settings settings = loadSettings();
    settings.plantumlPath = toNarrow(tempPath);
    saveSettings(settings);

    Settings reloaded = loadSettings();
    check(reloaded.plantumlPath == toNarrow(tempPath),
          "plantumlPath round-trips through settings.ini");
    const std::string writtenIni =
        readTextFile(std::filesystem::path(getSettingsPath()));
    check(writtenIni.find("plantumlPath=" + toNarrow(tempPath)) !=
              std::string::npos,
          "the saved settings.ini carries the plantumlPath value");

    // An empty value round-trips too (and writes no plantumlPath key).
    settings.plantumlPath.clear();
    saveSettings(settings);
    reloaded = loadSettings();
    check(reloaded.plantumlPath.empty(),
          "an empty plantumlPath round-trips too");
    const std::string clearedIni =
        readTextFile(std::filesystem::path(getSettingsPath()));
    check(clearedIni.find("plantumlPath=") == std::string::npos,
          "an empty plantumlPath leaves no settings.ini key");
}

// --------------------------------------------------------------- no-network gate

void testNoNetworkApis() {
    std::ifstream source(TINTA_PLANTUML_SOURCE, std::ios::binary);
    check(static_cast<bool>(source), "the module source is readable");
    if (!source) return;
    const std::string text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());
    const char* banned[] = {"winhttp", "wininet", "WinHttp", "URLDownload", "curl"};
    for (const char* needle : banned) {
        const std::string message =
            std::string("the module source must not reference ") + needle;
        check(text.find(needle) == std::string::npos, message.c_str());
    }
}

// --------------------------------------------------------------- queue tests

void testRenderExitCodeOut() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::string source = "@startuml\nA -> B\n@enduml\n";

    {
        const std::filesystem::path dir = freshScratch(L"exitcode-ok");
        std::wstring out;
        std::wstring error;
        int exitCode = 12345;
        const bool ok = plantuml::renderSync(tool, source, 0, dir.wstring(), out,
                                             15000, error, &exitCode);
        check(ok, "the exit-code probe renders successfully");
        check(exitCode == 0, "success reports exit code 0");
    }
    {
        const ScopedEnv exitEnv(L"TINTA_FAKE_PLANTUML_EXIT", L"200");
        const std::filesystem::path dir = freshScratch(L"exitcode-200");
        std::wstring out;
        std::wstring error;
        int exitCode = 12345;
        const bool ok = plantuml::renderSync(tool, source, 0, dir.wstring(), out,
                                             15000, error, &exitCode);
        check(!ok, "exit 200 fails the render");
        check(exitCode == 200, "the tool's own exit code is reported");
    }
    {
        const ScopedEnv sleepEnv(L"TINTA_FAKE_PLANTUML_SLEEP_MS", L"5000");
        const std::filesystem::path dir = freshScratch(L"exitcode-timeout");
        std::wstring out;
        std::wstring error;
        int exitCode = 12345;
        const bool ok = plantuml::renderSync(tool, source, 0, dir.wstring(), out,
                                             500, error, &exitCode);
        check(!ok, "the timeout probe fails");
        check(exitCode == -1, "a timeout reports -1");
    }
    {
        const plantuml::Tool unavailable;
        const std::filesystem::path dir = freshScratch(L"exitcode-preflight");
        std::wstring out;
        std::wstring error;
        int exitCode = 12345;
        const bool ok = plantuml::renderSync(unavailable, source, 0, dir.wstring(),
                                             out, 15000, error, &exitCode);
        check(!ok, "an unavailable tool fails the render");
        check(exitCode == -1, "a preflight failure reports -1");
    }
}

void testQueueCoalescing() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::filesystem::path scratch = freshScratch(L"queue-coalesce");
    std::filesystem::create_directories(scratch);
    const std::filesystem::path logPath = scratch / L"spawn.log";
    const ScopedEnv logEnv(L"TINTA_FAKE_PLANTUML_LOG", logPath.c_str());
    const ScopedEnv sleepEnv(L"TINTA_FAKE_PLANTUML_SLEEP_MS", L"250");

    plantuml::PlantumlRenderQueue queue;
    const std::filesystem::path workRoot = scratch / L"work";

    const uint64_t gateKey = 0x9A01ULL;
    queue.request(900, gateKey, kQueueSource, 0, tool, workRoot.wstring());
    if (!pollUntil([&] { return readLogLines(logPath).size() >= 1; }, 5000,
                   "the gate render spawns")) {
        return;
    }

    const uint64_t k1 = 0x1A01ULL;
    const uint64_t k2 = 0x1A02ULL;
    const uint64_t k3 = 0x1A03ULL;
    queue.request(7, k1, kQueueSource, 0, tool, workRoot.wstring());
    queue.request(7, k2, kQueueSource, 0, tool, workRoot.wstring());
    queue.request(7, k3, kQueueSource, 0, tool, workRoot.wstring());
    queue.waitForIdle(5000);

    const std::vector<std::string> lines = readLogLines(logPath);
    check(lines.size() == 2, "coalescing leaves exactly two spawns");
    if (lines.size() >= 2) {
        check(lines[1].find(toNarrow(plantuml::keyHex(k3))) != std::string::npos,
              "the surviving spawn renders the newest key");
        check(lines[1].find(toNarrow(plantuml::keyHex(k1))) == std::string::npos,
              "the superseded key 1 never spawns");
        check(lines[1].find(toNarrow(plantuml::keyHex(k2))) == std::string::npos,
              "the superseded key 2 never spawns");
    }
}

void testQueueCacheHit() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::filesystem::path scratch = freshScratch(L"queue-cache");
    std::filesystem::create_directories(scratch);
    const std::filesystem::path logPath = scratch / L"spawn.log";
    const ScopedEnv logEnv(L"TINTA_FAKE_PLANTUML_LOG", logPath.c_str());

    plantuml::PlantumlRenderQueue queue;
    const std::filesystem::path workRoot = scratch / L"work";
    const uint64_t key = 0x2B01ULL;
    queue.request(1, key, kQueueSource, 0, tool, workRoot.wstring());
    queue.waitForIdle(5000);

    const std::shared_ptr<const plantuml::Cached> hit = queue.lookup(key);
    check(hit != nullptr, "a finished render lands in the cache");
    if (hit) {
        check(hit->ok, "the cached entry is a success");
        check(!hit->filePath.empty() && std::filesystem::exists(hit->filePath),
              "the cached filePath exists on disk");
        check(hit->spawnCount >= 1, "the cached entry counts at least one spawn");
    }
    const size_t before = readLogLines(logPath).size();
    check(before == 1, "the first request spawns the tool exactly once");

    queue.request(1, key, kQueueSource, 0, tool, workRoot.wstring());
    queue.waitForIdle(5000);
    check(readLogLines(logPath).size() == before, "a cached key never respawns");
}

void testQueueLruEviction() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::filesystem::path scratch = freshScratch(L"queue-lru");
    std::filesystem::create_directories(scratch);
    const std::filesystem::path logPath = scratch / L"spawn.log";
    const ScopedEnv logEnv(L"TINTA_FAKE_PLANTUML_LOG", logPath.c_str());

    plantuml::PlantumlRenderQueue queue({}, 4);
    const std::filesystem::path workRoot = scratch / L"work";

    uint64_t keys[6] = {};
    for (int i = 0; i < 6; ++i) {
        keys[i] = 0x3C00ULL + static_cast<uint64_t>(i);
        queue.request(static_cast<size_t>(i), keys[i], kQueueSource, 0, tool,
                      workRoot.wstring());
    }
    queue.waitForIdle(15000);
    check(readLogLines(logPath).size() == 6, "six distinct keys spawn six times");

    for (int i = 0; i < 2; ++i) {
        check(queue.lookup(keys[i]) == nullptr,
              "the two least recently inserted keys are evicted");
        check(!std::filesystem::exists(workRoot / plantuml::keyHex(keys[i])),
              "an evicted key's work directory is deleted");
    }
    for (int i = 2; i < 6; ++i) {
        check(queue.lookup(keys[i]) != nullptr, "the four newest keys stay cached");
    }
}

void testQueueBackoffRetry() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::filesystem::path scratch = freshScratch(L"queue-backoff");
    std::filesystem::create_directories(scratch);
    const std::filesystem::path logPath = scratch / L"spawn.log";
    const ScopedEnv logEnv(L"TINTA_FAKE_PLANTUML_LOG", logPath.c_str());
    const ScopedEnv exitEnv(L"TINTA_FAKE_PLANTUML_EXIT", L"200");

    plantuml::BackoffConfig backoff;
    backoff.delaysMs = {50, 80, 120};
    plantuml::PlantumlRenderQueue queue(backoff);
    const std::filesystem::path workRoot = scratch / L"work";
    const uint64_t key = 0x4D01ULL;

    queue.request(5, key, kQueueSource, 0, tool, workRoot.wstring());
    if (!pollUntil([&] { return readLogLines(logPath).size() >= 1; }, 3000,
                   "the first failing attempt spawns")) {
        return;
    }
    const auto firstSeen = std::chrono::steady_clock::now();

    // A duplicate request inside the window must not reset the backoff.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    queue.request(5, key, kQueueSource, 0, tool, workRoot.wstring());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(readLogLines(logPath).size() == 1,
          "a duplicate request does not spawn inside the first window");

    if (!pollUntil([&] { return readLogLines(logPath).size() >= 2; }, 800,
                   "the first retry spawns after its window")) {
        return;
    }
    const auto secondSeen = std::chrono::steady_clock::now();
    const auto gap1 = std::chrono::duration_cast<std::chrono::milliseconds>(
                          secondSeen - firstSeen)
                          .count();
    check(gap1 >= 40, "the first retry waits out a ~50 ms window");

    if (!pollUntil([&] { return readLogLines(logPath).size() >= 3; }, 800,
                   "the second retry spawns after its window")) {
        return;
    }
    const auto thirdSeen = std::chrono::steady_clock::now();
    const auto gap2 = std::chrono::duration_cast<std::chrono::milliseconds>(
                          thirdSeen - secondSeen)
                          .count();
    check(gap2 >= 60, "the second retry waits out a ~80 ms window");

    queue.shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(readLogLines(logPath).size() == 3,
          "shutdown cancels the pending third retry");
}

void testQueuePermanentFailure() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::filesystem::path scratch = freshScratch(L"queue-permanent");
    std::filesystem::create_directories(scratch);
    const std::filesystem::path logPath = scratch / L"spawn.log";
    const ScopedEnv logEnv(L"TINTA_FAKE_PLANTUML_LOG", logPath.c_str());
    const ScopedEnv exitEnv(L"TINTA_FAKE_PLANTUML_EXIT", L"100");

    plantuml::PlantumlRenderQueue queue;
    const std::filesystem::path workRoot = scratch / L"work";
    const uint64_t keyA = 0x5E01ULL;
    queue.request(1, keyA, kQueueSource, 0, tool, workRoot.wstring());
    queue.waitForIdle(5000);

    check(readLogLines(logPath).size() == 1, "the exit-100 key spawns once");
    check(queue.lookup(keyA) == nullptr,
          "a permanent failure never enters the cache");
    check(!std::filesystem::exists(workRoot / plantuml::keyHex(keyA)),
          "the permanent failure's work directory is removed");

    queue.request(1, keyA, kQueueSource, 0, tool, workRoot.wstring());
    queue.waitForIdle(5000);
    check(readLogLines(logPath).size() == 1, "a permanent key is never retried");

    const uint64_t keyB = 0x5E02ULL;
    queue.request(2, keyB, kQueueSource, 0, tool, workRoot.wstring());
    queue.waitForIdle(5000);
    check(readLogLines(logPath).size() == 2,
          "a new key still renders after a permanent failure");
}

// ---------------------------------------------------- named-block artifacts

void testRenderNamedBlock() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);

    const char* sources[] = {
        "@startuml Flow\nAlice -> Bob: named\n@enduml\n",
        "@startuml(Flow)\nAlice -> Bob: parenthesized\n@enduml\n",
    };
    const char* labels[] = {"named block", "parenthesized block"};
    for (size_t i = 0; i < 2; ++i) {
        const std::filesystem::path dir = freshScratch(L"named-render");
        std::wstring out;
        std::wstring error;
        const bool ok = plantuml::renderSync(tool, sources[i], 0, dir.wstring(),
                                             out, 15000, error);
        check(ok, (std::string(labels[i]) +
                   " renders the named artifact through the fake tool")
                      .c_str());
        if (!ok) {
            std::cerr << "  renderSync error: " << toNarrow(error) << '\n';
            continue;
        }
        check(std::filesystem::path(out).filename().wstring() == L"Flow.png",
              "the named artifact is resolved instead of scrubbed");
        check(std::filesystem::exists(out) &&
                  std::filesystem::file_size(out) > 0,
              "the resolved named artifact exists and is non-empty");
        check(!std::filesystem::exists(dir / L"input.png"),
              "a named block leaves no canonical input.png behind");
    }
}

void testQueueNamedBlockAdoption() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::filesystem::path scratch = freshScratch(L"queue-named");
    std::filesystem::create_directories(scratch);
    const std::filesystem::path logPath = scratch / L"spawn.log";
    const ScopedEnv logEnv(L"TINTA_FAKE_PLANTUML_LOG", logPath.c_str());

    plantuml::PlantumlRenderQueue queue;
    const std::filesystem::path workRoot = scratch / L"work";
    const uint64_t key = 0x7A01ULL;
    queue.request(21, key, "@startuml Flow\nAlice -> Bob: named\n@enduml\n", 0,
                  tool, workRoot.wstring());
    queue.waitForIdle(5000);

    const std::shared_ptr<const plantuml::Cached> hit = queue.lookup(key);
    check(hit != nullptr, "a named-block render lands in the cache");
    if (hit) {
        check(hit->ok, "the named-block cache entry is a success");
        check(std::filesystem::path(hit->filePath).filename().wstring() ==
                  L"Flow.png",
              "the queue adopts the named artifact");
        check(std::filesystem::exists(hit->filePath),
              "the adopted named artifact exists on disk");
    }
    check(readLogLines(logPath).size() == 1,
          "the named block spawns exactly once");
}

void testQueueRetryCap() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::filesystem::path scratch = freshScratch(L"queue-retry-cap");
    std::filesystem::create_directories(scratch);
    const std::filesystem::path logPath = scratch / L"spawn.log";
    const ScopedEnv logEnv(L"TINTA_FAKE_PLANTUML_LOG", logPath.c_str());
    const ScopedEnv exitEnv(L"TINTA_FAKE_PLANTUML_EXIT", L"200");

    plantuml::BackoffConfig backoff;
    backoff.delaysMs = {50, 80, 120};
    plantuml::PlantumlRenderQueue queue(backoff);
    const std::filesystem::path workRoot = scratch / L"work";
    const uint64_t key = 0x8B01ULL;

    queue.request(31, key, kQueueSource, 0, tool, workRoot.wstring());
    const size_t bound = 1 + backoff.delaysMs.size();
    if (!pollUntil([&] { return readLogLines(logPath).size() >= bound; }, 5000,
                   "the finite retry schedule runs to its cap")) {
        return;
    }
    queue.waitForIdle(5000);
    // A repeating schedule would spawn again within the last delay (120 ms);
    // nothing may arrive inside this observation window.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    check(readLogLines(logPath).size() == bound,
          "an exhausted schedule stops respawning");
    check(queue.lookup(key) == nullptr, "a capped key never enters the cache");

    queue.request(31, key, kQueueSource, 0, tool, workRoot.wstring());
    queue.waitForIdle(5000);
    check(readLogLines(logPath).size() == bound,
          "a capped key is never retried after a repeat request");
}

void testQueueKeyDedup() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::filesystem::path scratch = freshScratch(L"queue-key-dedup");
    std::filesystem::create_directories(scratch);
    const std::filesystem::path logPath = scratch / L"spawn.log";
    const ScopedEnv logEnv(L"TINTA_FAKE_PLANTUML_LOG", logPath.c_str());

    plantuml::PlantumlRenderQueue queue;
    const std::filesystem::path workRoot = scratch / L"work";
    const uint64_t key = 0x9C01ULL;

    // Two blocks, one render: the second request must attach to the first
    // job - pending, in flight, or finished but not yet drained - instead of
    // scheduling another spawn.
    queue.request(41, key, kQueueSource, 0, tool, workRoot.wstring());
    queue.request(42, key, kQueueSource, 0, tool, workRoot.wstring());
    queue.waitForIdle(5000);

    check(readLogLines(logPath).size() == 1,
          "two blocks sharing a key spawn the tool exactly once");
    const std::shared_ptr<const plantuml::Cached> hit = queue.lookup(key);
    check(hit != nullptr && hit->ok, "the shared render lands in the cache");
    if (hit) {
        check(hit->spawnCount == 1, "the shared render counts one spawn");
    }

    queue.request(41, key, kQueueSource, 0, tool, workRoot.wstring());
    queue.request(42, key, kQueueSource, 0, tool, workRoot.wstring());
    queue.waitForIdle(5000);
    check(readLogLines(logPath).size() == 1, "cached keys never respawn");
}

void testQueueShutdown() {
    const plantuml::Tool tool = plantuml::resolveTool(kFakeToolPath);
    const std::filesystem::path scratch = freshScratch(L"queue-shutdown");
    std::filesystem::create_directories(scratch);
    const std::filesystem::path logPath = scratch / L"spawn.log";
    const ScopedEnv logEnv(L"TINTA_FAKE_PLANTUML_LOG", logPath.c_str());
    const ScopedEnv sleepEnv(L"TINTA_FAKE_PLANTUML_SLEEP_MS", L"300");

    plantuml::PlantumlRenderQueue queue;
    std::atomic<int> completions{0};
    queue.setCompletion([&completions](uint64_t, bool) {
        completions.fetch_add(1);
    });
    const std::filesystem::path workRoot = scratch / L"work";
    const uint64_t key = 0x6F01ULL;
    queue.request(1, key, kQueueSource, 0, tool, workRoot.wstring());
    if (!pollUntil([&] { return readLogLines(logPath).size() >= 1; }, 3000,
                   "the render to shut down spawns")) {
        return;
    }

    const auto shutdownStart = std::chrono::steady_clock::now();
    queue.shutdown();
    const auto shutdownMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - shutdownStart)
            .count();
    check(shutdownMs < 2000, "shutdown joins the in-flight render promptly");
    check(!std::filesystem::exists(workRoot / plantuml::keyHex(key)),
          "shutdown removes the unadopted work directory");

    const size_t linesAfterShutdown = readLogLines(logPath).size();
    const uint64_t lateKey = 0x6F02ULL;
    queue.request(2, lateKey, kQueueSource, 0, tool, workRoot.wstring());
    const auto idleStart = std::chrono::steady_clock::now();
    queue.waitForIdle(5000);
    const auto idleMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - idleStart)
                            .count();
    check(idleMs < 500, "waitForIdle returns immediately after shutdown");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(readLogLines(logPath).size() == linesAfterShutdown,
          "a request after shutdown never spawns");

    const int settled = completions.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(completions.load() == settled,
          "completion callbacks stop growing after shutdown");
}

void testSweepStaleTempRoots() {
    wchar_t base[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, base);
    const std::filesystem::path temp(base);
    const auto oldTime =
        std::filesystem::file_time_type::clock::now() - std::chrono::hours(48);

    const std::filesystem::path stale = temp / L"tinta-plantuml-999999";
    const std::filesystem::path live =
        temp / (L"tinta-plantuml-" +
                std::to_wstring(static_cast<unsigned long>(GetCurrentProcessId())));
    const std::filesystem::path nonDigits = temp / L"tinta-plantuml-nondigits";

    std::error_code ec;
    std::filesystem::remove_all(stale, ec);
    std::filesystem::remove_all(live, ec);
    std::filesystem::remove_all(nonDigits, ec);
    const std::filesystem::path targets[] = {stale, live, nonDigits};
    for (const std::filesystem::path& dir : targets) {
        std::filesystem::create_directories(dir, ec);
        std::ofstream marker(dir / L"marker.txt", std::ios::binary);
        marker << "x";
        marker.close();
        std::filesystem::last_write_time(dir, oldTime, ec);
        std::filesystem::last_write_time(dir / L"marker.txt", oldTime, ec);
    }

    plantuml::sweepStaleTempRoots();

    check(!std::filesystem::exists(stale),
          "a stale digit-suffixed temp root is swept");
    check(std::filesystem::exists(live),
          "the live process's own temp root survives");
    check(std::filesystem::exists(nonDigits),
          "a non-digit suffix is never swept");

    std::filesystem::remove_all(live, ec);
    std::filesystem::remove_all(nonDigits, ec);
}

}  // namespace

int main(int argc, char** argv) {
    bool portable = false;
    bool expectFailure = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--portable-test") portable = true;
        else if (arg == "--expect-failure") expectFailure = true;
    }

    // Refuse to run outside CTest's isolated portable folder: the settings
    // round-trip writes settings.ini and must never reach the user's
    // %APPDATA%\Tinta. The driver stages the exe beside its own settings.ini.
    if (!portable || !inIsolatedPortableFolder()) {
        std::cerr << "Run this test through CTest's isolated portable wrapper\n";
        return 2;
    }

    if (expectFailure) {
        check(false, "self-check: harness fails as designed");
        std::cout << "Self-check ran; designed failures: " << failures << '\n';
        return failures == 0 ? 0 : 1;
    }

    testLanguageGate();
    testPreambleInjection();
    testPreambleContent();
    testCacheKey();
    testCommandLine();
    testRenderSuccess();
    testRenderFailureExitCode();
    testRenderFailureWithImage();
    testRenderTimeout();
    testResolveTool();
    testResolveToolWithPathSearch();
    testSettingsRoundTrip();
    testRenderExitCodeOut();
    testQueueCoalescing();
    testQueueCacheHit();
    testQueueLruEviction();
    testQueueBackoffRetry();
    testQueuePermanentFailure();
    testRenderNamedBlock();
    testQueueNamedBlockAdoption();
    testQueueRetryCap();
    testQueueKeyDedup();
    testQueueShutdown();
    testSweepStaleTempRoots();
    testNoNetworkApis();

    if (failures == 0) {
        // QA hook: TINTA_PLANTUML_TEST_KEEP_SCRATCH=1 keeps the scratch tree
        // so the per-scenario workDirs can be inspected after the run.
        wchar_t keep[8] = {};
        const bool keepScratch =
            GetEnvironmentVariableW(L"TINTA_PLANTUML_TEST_KEEP_SCRATCH", keep, 8) > 0;
        std::cout << "All PlantUML tests passed\n";
        if (keepScratch) {
            std::cout << "scratch: " << toNarrow(scratchRoot().wstring()) << '\n';
        } else {
            std::error_code ec;
            std::filesystem::remove_all(scratchRoot(), ec);
        }
        return 0;
    }

    std::cerr << failures << " PlantUML check(s) failed; scratch kept at "
              << toNarrow(scratchRoot().wstring()) << '\n';
    return 1;
}