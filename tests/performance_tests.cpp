#include "d2d_init.h"
#include "editor.h"
#include "frame_preview.h"
#include "keystroke.h"
#include "render.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
using PerfClock = std::chrono::steady_clock;
double milliseconds(PerfClock::time_point start) {
    return std::chrono::duration<double, std::milli>(PerfClock::now() - start).count();
}

std::string document(const std::string& kind, int count) {
    std::string text;
    if (kind == "table") text = "| Name | Value | Notes |\n|:---:|---:|:---:|\n";
    if (kind == "code") text = "```cpp\n";
    for (int i = 0; i < count; ++i) {
        const std::string n = std::to_string(i);
        if (kind == "table") text += "| Row " + n + " | 1234 | **bold** and `code` |\n";
        else if (kind == "tables") text += "## Section " + n + "\n\n| A | B |\n|:---:|---:|\n| one | two |\n| three | four |\n\n";
        else if (kind == "code") text += "const auto value_" + n + " = function(1234); // a comment\n";
        else if (kind == "paragraph") text += "A long paragraph with **bold** words and *emphasis* to wrap on the page. ";
        else text += "## Section " + n + "\n\nA paragraph with **bold**, *emphasis* and a [link](https://example.com).\n\n- one\n- two\n\n";
    }
    if (kind == "code") text += "```\n";
    return text;
}

// Hash rendered text, hit-test rectangles, and line extents: a baseline
// and an optimized build must agree, not merely report a faster time.
uint64_t layoutHash(const App& app) {
    uint64_t hash = 14695981039346656037ull;
    auto add = [&](uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    auto coord = [&](float value) { add((uint64_t)(int64_t)std::llround(value * 100)); };
    for (wchar_t c : app.docText) add(c);
    coord(app.contentHeight);
    for (const auto& r : app.textRects) {
        add(r.docStart); add(r.docLength);
        coord(r.rect.left); coord(r.rect.top); coord(r.rect.right); coord(r.rect.bottom);
    }
    for (const auto& b : app.lineBuckets) {
        coord(b.top); coord(b.bottom); coord(b.minX); coord(b.maxX);
        for (size_t i : b.textRectIndices) add(i);
    }
    return hash;
}

bool validBuckets(const App& app) {
    for (const auto& b : app.lineBuckets) {
        float left = INFINITY, right = -INFINITY;
        for (size_t i : b.textRectIndices) {
            if (i >= app.textRects.size()) return false;
            left = std::min(left, app.textRects[i].rect.left);
            right = std::max(right, app.textRects[i].rect.right);
        }
        if (std::fabs(left - b.minX) > 0.01f || std::fabs(right - b.maxX) > 0.01f) return false;
    }
    return true;
}

int rowMetricRegressions(App& app) {
    int failures = 0, checks = 0;
    app.editMode = true;
    app.editorLive = false;
    app.editorWordWrap = true;
    app.editorShowPreview = true;
    app.editorText.clear();
    for (int i = 0; i < 80; ++i)
        app.editorText += L"Line " + std::to_wstring(i) +
            L": long words to wrap, a tab\tand Unicode \u4e2d\u6587 \u65e5\u672c\u8a9e e\u0301 \U0001f600. More words for another row.\n";
    auto verify = [&] {
        rebuildLineStarts(app);
        const auto rows = app.editorRowStarts;
        const size_t total = app.editorTotalRows;
        // A fresh measurement of every line is the oracle for the cached
        // result, including edits that shift line numbers and byte offsets.
        auto retained = app.editorRowMetrics;
        app.editorRowMetrics.reset();
        rebuildLineStarts(app);
        if (rows != app.editorRowStarts || total != app.editorTotalRows) ++failures;
        app.editorRowMetrics = std::move(retained);
        ++checks;
    };
    verify();
    for (size_t at : {size_t(0), app.editorText.size() / 2, app.editorText.size()}) {
        app.editorCursorPos = std::min(at, app.editorText.size());
        app.editorHasSelection = false;
        keystroke(app, 'X', {}, L'x');
        verify();
        keystroke(app, VK_RETURN);
        verify();
        keystroke(app, VK_BACK);
        verify();
    }
    // External replacements, like reloads, can change several lines at
    // once; equal lengths alone must never make a cached line reusable.
    std::mt19937 random(20261119);
    const std::wstring pieces[] = {L"", L"\n", L"\n\n", L"a longer replacement with words", L"\u4e2d\u6587\t", L"same"};
    for (int n = 0; n < 120; ++n) {
        const size_t at = random() % (app.editorText.size() + 1);
        const size_t length = std::min<size_t>(random() % 100, app.editorText.size() - at);
        app.editorText.replace(at, length, pieces[random() % 6]);
        verify();
    }
    for (int width : {440, 1300, 700}) {
        app.width = width;
        verify();
    }
    for (float zoom : {0.75f, 1.5f, 1.0f}) {
        app.zoomFactor = zoom;
        updateTextFormats(app);
        verify();
    }
    for (int theme : {5, 0}) {
        applyTheme(app, theme);
        verify();
    }
    app.editorShowPreview = false;
    verify();
    app.editorWordWrap = false;
    verify();
    if (app.editorRowMetrics) ++failures;
    app.editorWordWrap = true;
    verify();
    app.editorLive = true;
    verify();
    if (app.editorRowMetrics) ++failures;
    app.editorLive = false;
    verify();
    for (const auto& text : {L"", L"\n", L"same\nsame\nsame\n", L"different\ntext", L"\r\n\r\n"}) {
        app.editorText = text;
        verify();
    }
    // Two lines of one length that wrap differently: full-width letters
    // take more rows than as many ASCII ones. A cache that matched lines
    // by length would keep the narrow line's count for the wide one.
    std::wstring narrow;
    for (int i = 0; i < 40; ++i) narrow += L"abcd ";
    const std::wstring wide(narrow.size(), wchar_t(0x4E2D));
    app.editorText = narrow + L"\n" + narrow + L"\n" + narrow;
    verify();
    const size_t narrowRows = app.editorTotalRows;
    app.editorText = narrow + L"\n" + wide + L"\n" + narrow;
    verify();
    if (app.editorTotalRows <= narrowRows) ++failures;
    app.editorText.assign(500, L'W');
    auto* factory = app.dwriteFactory;
    app.dwriteFactory = nullptr;
    rebuildLineStarts(app);
    app.dwriteFactory = factory;
    if (app.editorRowMetrics) ++failures;
    verify();
    std::cout << "Wrap cache: " << checks << " cold-layout comparisons, " << failures << " failures\n";
    return failures;
}
} // namespace

int runPerformanceTests(bool benchmarks) {
    if (FAILED(OleInitialize(nullptr))) return 2;
    int failures = 0;
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        app.hwnd = CreateWindowExW(0, L"STATIC", L"Performance tests", WS_POPUP,
                                   0, 0, 1000, 900, nullptr, nullptr, nullptr, nullptr);
        if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
        app.width = 1000;
        app.height = 900;
        app.readingWidthPct = 100;
        updateTextFormats(app);
        applyTheme(app, 0);
        {
            std::ifstream in(std::filesystem::path(TINTA_SOURCE_DIR) / "tests/fixtures/performance-layout.md", std::ios::binary);
            const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (source.empty()) ++failures;
            app.root = app.parser.parse(source).root;
            for (int theme : {0, 5}) {
                applyTheme(app, theme);
                for (int width : {440, 1100}) {
                    app.width = width;
                    layoutDocument(app);
                    if (!validBuckets(app)) ++failures;
                    const std::string name = "performance-" + std::to_string(theme) + "-" + std::to_string(width) + ".png";
                    if (!saveFramePreview(app, std::filesystem::current_path() / name)) ++failures;
                    std::cout << "fixture," << theme << ',' << width << ',' << layoutHash(app) << '\n';
                }
            }
            app.width = 1000;
            applyTheme(app, 0);
        }
        std::cout << "case,count,bytes,parse_ms,layout_ms,hash\n" << std::fixed << std::setprecision(3);
        if (benchmarks) for (const std::string kind : {"mixed", "table", "tables", "code", "paragraph"}) {
            for (int count : {250, 1000, 3000, 10000}) {
                const std::string source = document(kind, count);
                std::vector<double> parseTimes, layoutTimes;
                uint64_t hash = 0;
                for (int run = 0; run < 4; ++run) {
                    auto start = PerfClock::now();
                    auto doc = app.parser.parse(source);
                    const double parse = milliseconds(start);
                    if (!doc.success) { ++failures; break; }
                    app.root = std::move(doc.root);
                    app.layoutDirty = true;
                    start = PerfClock::now();
                    layoutDocument(app);
                    const double layout = milliseconds(start);
                    if (!validBuckets(app)) ++failures;
                    const uint64_t next = layoutHash(app);
                    if (run && hash != next) ++failures;
                    hash = next;
                    if (run) { parseTimes.push_back(parse); layoutTimes.push_back(layout); }
                }
                if (parseTimes.size() != 3) continue;
                std::sort(parseTimes.begin(), parseTimes.end());
                std::sort(layoutTimes.begin(), layoutTimes.end());
                std::cout << kind << ',' << count << ',' << source.size() << ',' << parseTimes[1] << ','
                          << layoutTimes[1] << ',' << hash << std::endl;
            }
        }
        app.editMode = true;
        app.editorWordWrap = true;
        if (benchmarks) for (int count : {250, 1000, 3000, 10000, 30000}) {
            std::wstring source;
            for (int i = 0; i < count; ++i)
                source += L"Line " + std::to_wstring(i) + L": A long line with enough words to need wrapping in the source pane. More words to carry it over to the next visual row.\n";
            app.editorText = source;
            rebuildLineStarts(app);
            app.editorCursorPos = app.editorText.size() / 2;
            app.editorHasSelection = false;
            std::vector<double> keys;
            for (int i = 0; i < 7; ++i) {
                const auto start = PerfClock::now();
                keystroke(app, 'X', {}, L'x');
                keys.push_back(milliseconds(start));
            }
            if (app.editorText.size() != source.size() + 7) ++failures;
            std::sort(keys.begin(), keys.end());
            uint64_t hash = 0;
            for (size_t row : app.editorRowStarts) hash = hash * 31 + row;
            std::cout << "wrapped-edit," << count << ',' << source.size() * sizeof(wchar_t)
                      << ",0," << keys[3] << ',' << hash << std::endl;
        }
        failures += rowMetricRegressions(app);
        KillTimer(app.hwnd, 2);
        app.editorDirty = false;
        DestroyWindow(app.hwnd);
        app.hwnd = nullptr;
    }
    CoUninitialize();
    OleUninitialize();
    if (failures) std::cerr << failures << " performance regression checks failed\n";
    return failures ? 1 : 0;
}
