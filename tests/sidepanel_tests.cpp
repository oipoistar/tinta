#include "sidepanels.h"
#include "d2d_init.h"
#include "file_utils.h"
#include "input.h"
#include "overlays.h"
#include "print.h"
#include "render.h"
#include "settings.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <windowsx.h>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool closeEnough(float a, float b) { return std::abs(a-b) < 0.1f; }
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}
LRESULT CALLBACK testProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app && message == WM_CAPTURECHANGED && reinterpret_cast<HWND>(l) != hwnd)
        sidePanelResizeEnd(*app, hwnd, true);
    return DefWindowProcW(hwnd, message, w, l);
}
void layout(App& app, const std::string& source) {
    const auto result = app.parser.parse(source);
    check(result.success, "panel fixture parses");
    app.root = result.root;
    layoutDocument(app);
}
void renderPanels(App& app) {
    app.renderTarget->BeginDraw();
    if (app.showFolderBrowser) renderFolderBrowser(app);
    if (app.showToc) renderToc(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "native panels render after resize");
}
void headings(App& app, const std::string& source) {
    layout(app, source);
    check(app.headings.size() == 9, "all H1-H6 and repeated headings enter Contents (#210)");
    if (app.headings.size() != 9) return;
    for (int level = 1; level <= 6; ++level)
        check(app.headings[level-1].level == level, "heading order and original levels are retained");
    check(app.headings[6].id == "repeated-title" && app.headings[7].id == "repeated-title-1",
          "duplicate titles across heading levels receive distinct anchors");
    check(scrollToHeadingId(app, "level-four") && scrollToHeadingId(app, "level-five") &&
          scrollToHeadingId(app, "level-six") && scrollToHeadingId(app, "repeated-title-1"),
          "deep headings and duplicate titles support anchor navigation");
    check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1 &&
          app.docText.find(L"Final paragraph") != std::wstring::npos,
          "mixed Markdown table, code, math and final text survive layout");
    app.showToc = true;
    app.tocAnimation = 1;
    app.tocFilter = L"level six";
    app.tocScroll = 0;
    const auto list = tocListRect(app);
    check(tocItemIndexAt(app, list.left + 15, list.top + 10) == 5,
          "filtered Contents hit-testing finds an H6 row");
    app.tocPinned = true;
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM((int)list.left + 15, (int)list.top + 10));
    check(app.tocSpyOverride == 5 && app.showToc, "clicking H6 navigates and preserves a pinned Contents panel");
    check(closeEnough(tocMaxScroll(app), 0), "filtered short Contents has no excess scrolling");
    app.tocFilter.clear();
    app.tocPinned = false;

    // Reflow completion must not skip or register a heading twice.
    app.layoutDirty = true;
    layoutDocumentViewportFirst(app);
    ensureLayoutComplete(app);
    check(app.headings.size() == 9 && app.headings[7].id == "repeated-title-1",
          "incremental layout preserves heading identities");
}
void resizeGesture(App& app, SidePanel panel, bool onLeft, bool both, float scale) {
    app.contentScale = scale;
    app.width = static_cast<int>(1200 * scale);
    app.height = static_cast<int>(900 * scale);
    app.showToc = both || panel == SidePanel::Contents;
    app.showFolderBrowser = both || panel == SidePanel::Browser;
    app.tocOnLeft = onLeft;
    app.tocAnimation = app.folderBrowserAnimation = 1;
    app.tocWidth = 280;
    app.browserWidth = 300;
    app.tocSpyOverride = -1;
    app.swallowNextMouseUp = false;
    const std::string path = app.currentFile;
    const float start = sidePanelResizeEdge(app, panel);
    const float y = dpi(app, 300);
    const float direction = panel == SidePanel::Contents && !onLeft ? -1 : 1;
    const float target = start + direction * dpi(app, 100);
    handleMouseDown(app, app.hwnd, 0, MAKELPARAM((int)start, (int)y));
    check(app.panelResize.panel == panel && GetCapture() == app.hwnd, "panel edge captures the drag");
    handleMouseMove(app, app.hwnd, MAKELPARAM((int)target, (int)y));
    check(closeEnough(sidePanelResizeEdge(app, panel), target), "dragged edge follows the pointer at monitor DPI");
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM((int)target, (int)y));
    check(app.panelResize.panel == SidePanel::None && GetCapture() != app.hwnd,
          "release ends resizing and releases capture");
    check(app.currentFile == path && app.tocSpyOverride == -1 && !app.selecting,
          "resize release cannot open a file, navigate a heading or select document text");
    check(app.showToc == (both || panel == SidePanel::Contents) &&
          app.showFolderBrowser == (both || panel == SidePanel::Browser), "resize does not dismiss either panel");
    const Settings saved = loadSettings();
    check(closeEnough(saved.tocWidth, app.tocWidth) && closeEnough(saved.browserWidth, app.browserWidth),
          "both widths persist after a completed resize");
    const float oldToc = app.tocWidth, oldBrowser = app.browserWidth;
    for (bool escape : {false, true}) {
        const float edge = sidePanelResizeEdge(app, panel);
        check(sidePanelResizeBegin(app, app.hwnd, edge, y), "a second resize can start");
        sidePanelResizeMove(app, edge + direction * dpi(app, 80));
        if (escape) handleKeyDown(app, app.hwnd, VK_ESCAPE);
        else ReleaseCapture();
        check(app.panelResize.panel == SidePanel::None && GetCapture() != app.hwnd &&
              closeEnough(app.tocWidth, oldToc) && closeEnough(app.browserWidth, oldBrowser),
              "Escape or lost capture cancels and restores both widths");
        handleMouseUp(app, app.hwnd, 0, MAKELPARAM((int)edge, (int)y));
        check(app.currentFile == path, "cancelled resize release cannot activate content");
    }
    app.showSettings = true;
    check(sidePanelResizeAt(app, sidePanelResizeEdge(app, panel), y) == SidePanel::None,
          "modal settings block the panel handle");
    app.showSettings = false;
    app.editMode = true;
    check(sidePanelResizeAt(app, sidePanelResizeEdge(app, panel), y) == SidePanel::None,
          "panel resizing cannot steal the editor split handle");
    app.editMode = false;
}
void widthLimits(App& app) {
    app.showToc = app.showFolderBrowser = true;
    for (float scale : {1.0f, 1.5f, 2.0f}) {
        app.contentScale = scale;
        for (int width : {300, 480, 650, 1050, 1600}) {
            app.width = static_cast<int>(width * scale);
            for (float wanted : {180.0f, 280.0f, 600.0f, 4000.0f}) {
                app.tocWidth = wanted;
                app.browserWidth = wanted + 20;
                const auto sizes = sidePanelWidths(app);
                check(sizes.toc >= 0 && sizes.browser >= 0 &&
                      sizes.toc + sizes.browser <= sidePanelBudget(app) + 0.1f &&
                      documentViewportWidth(app) >= std::min(dpi(app, 240), app.width * 0.4f) - 0.1f,
                      "both panels fit and reserve document space on narrow windows");
                app.zoomFactor = 1.5f;
                check(closeEnough(tocPanelWidth(app), sizes.toc) && closeEnough(folderBrowserPanelWidth(app), sizes.browser),
                      "document zoom cannot resize the side panels");
                app.zoomFactor = 1;
                check(closeEnough(app.tocWidth, wanted), "window clamping does not overwrite the preferred width");
                const float fullIndent = tocHeadingIndent(app, 6);
                check(fullIndent >= tocHeadingIndent(app, 3) && fullIndent < sizes.toc * 0.36f,
                      "deep indentation leaves room for heading text");
            }
        }
    }
}

void quietPanelEdges(App& app) {
    app.contentScale = 1;
    app.width = 1200;
    app.height = 900;
    app.showToc = app.showFolderBrowser = true;
    app.tocOnLeft = false;
    app.tocPinned = app.browserPinned = true;
    app.tocAnimation = app.folderBrowserAnimation = 1;
    app.tocWidth = 280;
    app.browserWidth = 300;
    app.verticalScrollbarVisible = true;
    app.contentHeight = 2000;
    app.lastPanelScrollActivity = 0;
    app.panelScrollbarFade = {};
    app.mouseX = 500;
    app.mouseY = 400;
    check(closeEnough(sidePanelDocumentScrollbarOpacity(app, 900), 0) && !sidePanelScrollbarNeedsTicks(app, 900),
          "idle side-panel layout hides scrollbars without a running animation timer");
    app.lastPanelScrollActivity = 1000;
    check(closeEnough(sidePanelDocumentScrollbarOpacity(app, 1000), 0) && sidePanelScrollbarNeedsTicks(app, 1000),
          "new scroll activity starts a fade-in without an opacity jump");
    check(closeEnough(sidePanelDocumentScrollbarOpacity(app, 1060), 0.5f) &&
          closeEnough(sidePanelDocumentScrollbarOpacity(app, 1120), 1) &&
          closeEnough(sidePanelDocumentScrollbarOpacity(app, 1700), 1), "scrollbar gently appears over 120ms then holds");
    check(closeEnough(sidePanelDocumentScrollbarOpacity(app, 1801), 1) &&
          closeEnough(sidePanelDocumentScrollbarOpacity(app, 1951), 0.5f) &&
          closeEnough(sidePanelDocumentScrollbarOpacity(app, 2101), 0) && !sidePanelScrollbarNeedsTicks(app, 2101),
          "scrollbar fades out after inactivity and its timer can stop");
    const float edge = sidePanelResizeEdge(app, SidePanel::Contents);
    handleMouseMove(app, app.hwnd, MAKELPARAM((int)edge - 10, 400));
    check(documentScrollbarEdgeHovered(app) && closeEnough(sidePanelDocumentScrollbarOpacity(app, 3000), 0) &&
          sidePanelScrollbarNeedsTicks(app, 3000) && closeEnough(sidePanelDocumentScrollbarOpacity(app, 3060), 0.5f) &&
          closeEnough(sidePanelDocumentScrollbarOpacity(app, 3120), 1) && !sidePanelScrollbarNeedsTicks(app, 3120),
          "hover alone fades in the scrollbar and stops ticking once fully visible");
    handleMouseDown(app, app.hwnd, 0, MAKELPARAM((int)edge - 10, 400));
    check(app.scrollbarDragging && app.panelResize.panel == SidePanel::None,
          "the document scrollbar remains draggable beside the invisible resize target");
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM((int)edge - 10, 400));
    app.mouseX = (int)edge;
    check(!documentScrollbarEdgeHovered(app) && sidePanelResizeAt(app, edge, 400) == SidePanel::Contents,
          "hover at the shared divider targets resizing only");
    sidePanelDocumentScrollbarOpacity(app, 3200);
    const float fading = sidePanelDocumentScrollbarOpacity(app, 3350);
    app.lastPanelScrollActivity = 3350;
    check(closeEnough(sidePanelDocumentScrollbarOpacity(app, 3350), fading) &&
          closeEnough(sidePanelDocumentScrollbarOpacity(app, 3410), 0.75f) &&
          closeEnough(sidePanelDocumentScrollbarOpacity(app, 3470), 1),
          "scrolling again during fade-out resumes smoothly from the current opacity");
    check(sidePanelResizeBegin(app, app.hwnd, edge, 400) &&
          closeEnough(sidePanelDocumentScrollbarOpacity(app, 3500), 1) &&
          closeEnough(sidePanelDocumentScrollbarOpacity(app, 3800), 0), "resizing fades away the competing scrollbar");
    sidePanelResizeEnd(app, app.hwnd, true);
    app.swallowNextMouseUp = false;
    app.mouseX = app.mouseY = -1;
    check(!documentScrollbarEdgeHovered(app), "leaving the window clears edge hover");
    app.showToc = app.showFolderBrowser = false;
    check(closeEnough(sidePanelDocumentScrollbarOpacity(app, 4000), 1) && !sidePanelScrollbarNeedsTicks(app, 4000),
          "ordinary viewer scrollbar appearance is preserved without an animation timer");
    app.showToc = true;
    check(closeEnough(sidePanelDocumentScrollbarOpacity(app, 5000), 0), "reopening the panel does not reuse stale visible opacity");
    app.tocPinned = app.browserPinned = false;
}

void longContents(App& app) {
    app.contentScale = 1;
    updateTextFormats(app);
    app.width = 1050;
    app.height = 400;
    app.showFolderBrowser = false;
    app.showToc = true;
    app.tocWidth = 180;
    app.tocAnimation = 1;
    app.tocFilter.clear();
    std::string source;
    for (int i = 0; i < 120; ++i)
        source += std::string(i % 6 + 1, '#') + " Section " + std::to_string(i) +
                  "\n\nA paragraph with **bold**, `code` and $x^2$.\n\n";
    app.root = app.parser.parse(source).root;
    app.scrollY = app.targetScrollY = 0;
    app.layoutDirty = true;
    layoutDocumentViewportFirst(app);
    ensureLayoutComplete(app);
    check(app.headings.size() == 120 && app.headings.back().level == 6 &&
          app.headings.back().id == "section-119", "incremental long documents retain every deep heading");
    const auto rect = tocListRect(app);
    app.tocScroll = tocMaxScroll(app) - 1;
    app.mouseX = (int)(rect.left + 20);
    app.mouseY = (int)(rect.top + 20);
    handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), 0);
    check(closeEnough(app.tocScroll, tocMaxScroll(app)) &&
          tocItemIndexAt(app, rect.left + 20, rect.bottom - 13) == 119,
          "wheel scrolling reaches the final H6 row without excess blank space");
    app.tocFilter = L"section 119";
    app.tocScroll = 0;
    check(tocItemIndexAt(app, rect.left + 20, rect.top + 13) == 119 &&
          closeEnough(tocMaxScroll(app), 0), "filtering a long Contents uses the filtered row count");
    app.tocFilter.clear();
    const auto companion = std::filesystem::path(TINTA_PANEL_FIXTURE).parent_path() /
                           "toc-browser-companion-with-a-long-file-name.md";
    layout(app, read(companion));
    check(app.headings.size() == 2 && app.headings[1].level == 6,
          "H6 appears when intermediate heading levels are skipped");
}
}

int runSidePanelTests() {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = fs::path(exe).parent_path();
    const auto marker = read(dir / "settings.ini");
    if (!fs::equivalent(dir, fs::current_path()) ||
        (marker != "; Isolated side-panel test configuration\n" &&
         marker != "; Isolated side-panel test configuration\r\n")) return 2;
    check(fs::equivalent(tintaConfigDir(), dir), "all settings writes stay in the portable test directory");
    auto state = std::make_unique<App>();
    App& app = *state;
    WNDCLASSW wc{};
    wc.lpfnWndProc = testProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TintaSidePanelTest";
    RegisterClassW(&wc);
    app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Panel tests", WS_POPUP,
        0, 0, 1050, 900, nullptr, nullptr, wc.hInstance, nullptr);
    SetWindowLongPtrW(app.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    const auto source = read(TINTA_PANEL_FIXTURE);
    check(!source.empty(), "mixed panel fixture loads");
    app.currentFile = TINTA_PANEL_FIXTURE;
    app.folderBrowserPath = fs::path(TINTA_PANEL_FIXTURE).parent_path().wstring();
    populateFolderItems(app);
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float scale : {1.0f, 1.5f, 2.0f}) {
            app.contentScale = scale;
            updateTextFormats(app);
            for (int width : {650, 1050}) {
                app.width = static_cast<int>(width * scale);
                app.height = static_cast<int>(900 * scale);
                for (bool left : {false, true}) {
                    app.tocOnLeft = left;
                    app.showFolderBrowser = true;
                    app.folderBrowserAnimation = 1;
                    headings(app, source);
                    renderPanels(app);
                }
            }
        }
    }
    for (float scale : {1.0f, 1.5f, 2.0f})
        for (bool left : {false, true})
            for (bool both : {false, true})
                for (SidePanel panel : {SidePanel::Contents, SidePanel::Browser})
                    resizeGesture(app, panel, left, both, scale);
    widthLimits(app);
    quietPanelEdges(app);
    longContents(app);

    app.contentScale = app.zoomFactor = 1;
    app.width = 1050;
    app.height = 900;
    app.showToc = app.showFolderBrowser = false;
    layout(app, source);
    const int plainPages = printDebugPages(app, (dir / "plain-pages").wstring());
    app.showToc = app.showFolderBrowser = true;
    app.tocWidth = 500;
    app.browserWidth = 400;
    const int panelPages = printDebugPages(app, (dir / "panel-pages").wstring());
    check(plainPages > 0 && plainPages == panelPages, "open resized panels do not alter print pagination");
    for (int page = 1; page <= plainPages && page <= panelPages; ++page) {
        const auto name = "page-" + std::to_string(page) + ".png";
        check(read(dir / "plain-pages" / name) == read(dir / "panel-pages" / name),
              "print pixels are identical with resized panels open or closed");
    }
    check(app.showToc && app.showFolderBrowser && closeEnough(app.tocWidth, 500) && closeEnough(app.browserWidth, 400),
          "printing restores panel visibility and preferred widths");
    {
        std::ofstream(dir / "settings.ini") << "tocWidth=bad\nbrowserWidth=nan\n";
        const auto saved = loadSettings();
        check(closeEnough(saved.tocWidth, 280) && closeEnough(saved.browserWidth, 300),
              "invalid saved panel widths safely use defaults");
    }
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Contents and resizable panels: " << failures << " failures\n";
    return failures ? 1 : 0;
}
