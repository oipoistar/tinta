#include "d2d_init.h"
#include "render.h"
#include "selection.h"

#include <cmath>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        if (failures < 30) std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Double-click a token exactly the way handleMouseDown does: map the click to
// an offset, expand to the word run, and compare with the expected text.
void verifyTokenSelects(App& app, const wchar_t* token) {
    const size_t pos = app.docText.find(token);
    check(pos != std::wstring::npos, "cell token is present in the laid-out text");
    if (pos == std::wstring::npos) return;

    // The covering word rect for the token's first character
    const App::TextRect* word = nullptr;
    for (const App::TextRect& tr : app.textRects) {
        if (tr.docStart <= pos && pos < tr.docStart + tr.docLength &&
            tr.docLength == static_cast<size_t>(std::wcslen(token))) {
            word = &tr;
            break;
        }
    }
    // Wrapped CJK rounds produce one rect per ideograph; fall back to any
    // rect covering the token's span so hit-testing stays at the same y.
    if (!word) {
        for (const App::TextRect& tr : app.textRects) {
            if (tr.docStart <= pos && pos < tr.docStart + tr.docLength) {
                word = &tr;
                break;
            }
        }
    }
    check(word != nullptr, "a selectable rect covers the cell token");
    if (!word) return;

    float docX = (word->rect.left + word->rect.right) * 0.5f;
    float docY = (word->rect.top + word->rect.bottom) * 0.5f;
    size_t off = selectionOffsetAtPoint(app, docX, docY);
    size_t ws = 0, we = 0;
    selectionWordRange(app, off, ws, we);
    const std::wstring got = selectionTextForRange(app, ws, we);
    if (got != token) {
        std::wcerr << L"FAIL: double-click on \"" << token << L"\" selected \""
                   << got << L"\"\n";
        ++failures;
    }
}
}  // namespace

int runTableSelectWordTests() {
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!initD2D(app)) return 2;
        std::ifstream file(TINTA_TABLESELECT_FIXTURE);
        check(file.good(), "selectable-table fixture opens");
        std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        auto doc = app.parser.parse(source);
        check(doc.success, "selectable-table fixture parses");
        app.root = doc.root;
        app.height = 900;
        app.readingWidthPct = 100;
        app.width = 1050;
        applyTheme(app, 0);
        updateTextFormats(app);
        layoutDocument(app);

        // Control: a paragraph word never leaks into its neighbours.
        verifyTokenSelects(app, L"Bravo");

        // The tokens of the rendered table must stay word-isolated even
        // though every column of a row shares one visual line.
        for (const wchar_t* t : {L"Alpha", L"Bravo", L"Charlie",
                                 L"Delta", L"Echo", L"Foxtrot",
                                 L"Golf", L"Hotel", L"India"}) {
            verifyTokenSelects(app, t);
        }

        // CJK cells: same isolation, no adjacent-cell run bleed.
        for (const wchar_t* t : {L"\u7532", L"\u4e59", L"\u4e19",
                                 L"\u4e01", L"\u620a", L"\u5df1"}) {
            verifyTokenSelects(app, t);
        }
    }
    CoUninitialize();
    std::cout << "Table word selection: " << failures << " failures\n";
    return failures ? 1 : 0;
}