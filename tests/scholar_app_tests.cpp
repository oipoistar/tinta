// Scholar in the window (docs/specs/scholar.md): the APA paper laid out at
// a wide and a narrow width, its citations links to anchored references,
// a click on one scrolling to its reference, the hover card's text, the
// references set with a hanging indent and GB/T's with a number column.
// Frames are saved beside the test for inspection.
#include "d2d_init.h"
#include "editor.h"
#include "frame_preview.h"
#include "input.h"
#include "render.h"
#include "utils.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

const App::LinkRect* linkTo(const App& app, const std::string& anchor) {
    for (const auto& link : app.linkRects)
        if (link.anchor == anchor) return &link;
    return nullptr;
}

// The text run that starts with `start`, if laid out
const App::LayoutTextRun* runStarting(const App& app, const std::wstring& start) {
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && app.docText.compare(run.docStart, start.size(), start) == 0) return &run;
    }
    return nullptr;
}
}  // namespace

int runScholarAppTests(const char* fixtureDir) {
    const fs::path fixtures = fixtureDir;
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Scholar tests", WS_POPUP, 0, 0, 1050, 900, nullptr, nullptr, nullptr,
                               nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    applyTheme(app, 0);
    updateTextFormats(app);

    check(openDocumentInViewer(app, (fixtures / "paper.md").wstring()), "the APA paper opens");
    for (int width : {1050, 650}) {
        const std::string at = " at " + std::to_string(width) + " px";
        app.width = width;
        app.layoutDirty = true;
        ensureLayoutComplete(app);
        const App::LinkRect* first = linkTo(app, "cite-1");
        check(first && first->url == "#ref-knuth1984", "the first citation links to Knuth's reference" + at);
        check(app.footnoteAnchors.count("ref-knuth1984") && app.footnoteAnchors.count("ref-vanrossum2009"),
              "the references are places to go" + at);
        check(app.footnoteAnchors.count("fig:nib") && app.footnoteAnchors.count("tbl:inks") &&
                  app.footnoteAnchors.count("eq:rise"),
              "so are the figure, the table and the equations" + at);
        auto card = app.referenceCards.find("ref-knuth1984");
        check(card != app.referenceCards.end() && card->second.find(L"Literate Programming") != std::wstring::npos,
              "the hover card has Knuth's reference" + at);
        check(app.citationKeys["cite-1"] == "knuth1984 brooks1995", "and knows both keys of the first citation" + at);
        // Hanging indent: an entry's first line starts left of the lines after it
        const App::LayoutTextRun* abe = runStarting(app, L"Abe, A.");
        const App::LayoutTextRun* next = nullptr;
        for (const auto& run : app.layoutTextRuns) {
            if (abe && run.pos.y > abe->pos.y + 1 && run.pos.y < abe->pos.y + 60 && run.docStart > abe->docStart) {
                next = &run;
                break;
            }
        }
        check(abe && next && next->pos.x > abe->pos.x + 10, "APA's references hang" + at);
        check(saveFramePreview(app, fs::current_path() / ("paper-" + std::to_string(width) + ".png")), "a frame is saved" + at);

        // A click on a citation goes to its reference
        if (first) {
            app.scrollY = app.targetScrollY = 0;
            app.hoveredLink = first->url;
            handleLinkClick(app);
            check(std::fabs(app.targetScrollY - app.footnoteAnchors["ref-knuth1984"]) < 400.0f && app.targetScrollY > 100.0f,
                  "a click on the citation scrolls to its reference" + at);
            app.hoveredLink.clear();
        }
    }

    // GB/T 7714: the numbers in a column, the entries beside it
    app.width = 1050;
    check(openDocumentInViewer(app, (fixtures / "paper-zh.md").wstring()), "the Chinese paper opens");
    app.layoutDirty = true;
    ensureLayoutComplete(app);
    const App::LayoutTextRun* one = runStarting(app, L"[1]");
    const App::LayoutTextRun* entry = nullptr;
    for (const auto& run : app.layoutTextRuns) {
        if (one && std::fabs(run.pos.y - one->pos.y) < 2 && run.pos.x > one->pos.x + 5) entry = &run;
    }
    check(one && entry, "[1] stands in a column beside its entry");
    check(saveFramePreview(app, fs::current_path() / "paper-zh.png"), "the Chinese paper's frame is saved");

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Scholar in the app: " << failures << " failures\n";
    return failures ? 1 : 0;
}
