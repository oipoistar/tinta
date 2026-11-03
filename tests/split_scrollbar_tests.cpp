// #280: in the editor beside its preview, the pointer is an arrow over the
// editor's scrollbar and while dragging it, and dragging the preview's
// scrollbar scrolls both panes, as the wheel over the preview does
#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "render.h"
#include "settings.h"
#include "sidepanels.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) { if (failures < 40) std::cerr << "FAIL split scrollbars: " << message << '\n'; ++failures; }
}
bool closeTo(float a, float b) { return std::fabs(a - b) < 0.5f; }

// The pointer's shape after the window handles a move there
HCURSOR cursorAfterMove(App& app, float x, float y) {
    SetCursor(nullptr);
    handleMouseMove(app, app.hwnd, MAKELPARAM((int)x, (int)y));
    return GetCursor();
}

// A frame: the preview lays out, the editor paints (its scrollable height
// comes from that) and the preview follows the editor, as each paint does
void frame(App& app) {
    ensureLayoutComplete(app);
    app.verticalScrollbarVisible = app.contentHeight > app.height;
    app.renderTarget->BeginDraw();
    renderEditor(app, editorPaneWidth(app));
    check(SUCCEEDED(app.renderTarget->EndDraw()), "the editor paints");
    syncPreviewToEditor(app);
}

void scenario(App& app, const char* path, int width, bool wrap) {
    const std::string where = " (" + std::to_string(width) + (wrap ? ", wrap)" : ")");
    const HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    const HCURSOR beam = LoadCursor(nullptr, IDC_IBEAM);
    app.width = width;
    check(openDocumentInViewer(app, toWide(path)), "the fixture opens" + where);
    enterEditMode(app);
    app.editorShowPreview = true;
    app.editorWordWrap = wrap;
    rebuildLineStarts(app);
    ensureLayoutComplete(app);
    frame(app);
    check(app.editorContentHeight > app.height * 2.0f && app.contentHeight > app.height * 2.0f,
          "both panes are several screens long" + where);

    // The editor's scrollbar runs below the tab strip, as the preview's does
    // (#291): at the top its thumb starts there, and is taken just below
    const float editorWidth = editorPaneWidth(app);
    const float barX = editorWidth - dpi(app, 8.0f);
    const ScrollbarGeometry editorBar = editorScrollbarV(app);
    check(closeTo(editorBar.trackStart, chromeTopHeight(app)) &&
              closeTo(editorBar.thumbStart, chromeTopHeight(app)) &&
              closeTo(editorBar.trackStart + editorBar.trackLength, (float)app.height),
          "at the top, the editor's thumb starts below the tab strip" + where);
    const float thumbY = chromeTopHeight(app) + dpi(app, 12.0f);
    check(cursorAfterMove(app, editorWidth * 0.4f, 300.0f) == beam, "a text beam over the editor's text" + where);
    check(cursorAfterMove(app, barX, thumbY) == arrow, "an arrow over the editor's scrollbar" + where);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM((int)barX, (int)thumbY));
    check(app.editorScrollbarDragging, "a press on the editor's scrollbar drags it" + where);
    check(cursorAfterMove(app, barX, thumbY + 150.0f) == arrow, "an arrow while the editor's scrollbar is dragged" + where);
    check(closeTo(editorScrollbarV(app).thumbStart, editorBar.thumbStart + 150.0f),
          "the editor's thumb moves with the pointer" + where);
    check(cursorAfterMove(app, editorWidth * 0.4f, thumbY + 200.0f) == arrow,
          "still an arrow when the drag strays over the text" + where);
    check(app.editorScrollY > 0.0f, "the drag scrolls the editor" + where);
    // To the window's foot and back up to the strip
    cursorAfterMove(app, barX, (float)app.height + 200.0f);
    const ScrollbarGeometry atEnd = editorScrollbarV(app);
    check(closeTo(app.editorScrollY, atEnd.maxScroll) &&
              closeTo(atEnd.thumbStart + atEnd.thumbLength, (float)app.height),
          "dragged to the end, the editor's thumb ends at the window's foot" + where);
    cursorAfterMove(app, barX, -200.0f);
    check(app.editorScrollY <= 0.5f && closeTo(editorScrollbarV(app).thumbStart, chromeTopHeight(app)),
          "dragged back up, the editor's thumb stops below the tab strip" + where);
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM((int)(editorWidth * 0.4f), (int)(thumbY + 200.0f)));
    check(!app.editorScrollbarDragging, "the release ends the drag" + where);
    // A click on the track centres the thumb there
    const float trackClick = (float)app.height * 0.7f;
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM((int)barX, (int)trackClick));
    const ScrollbarGeometry clicked = editorScrollbarV(app);
    check(closeTo(clicked.thumbStart + clicked.thumbLength * 0.5f, trackClick),
          "a click on the editor's track centres its thumb there" + where);
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM((int)barX, (int)trackClick));
    app.editorScrollY = 0.0f;

    // The preview's scrollbar, from the top. Its track runs below the tab
    // strip, as drawn (#287)
    app.editorScrollY = 0.0f;
    frame(app);
    const ScrollbarGeometry bar = documentScrollbarV(app);
    const float maxScroll = std::max(0.0f, app.contentHeight - (float)app.height);
    const float thumbH = bar.thumbLength;
    const float track = bar.trackLength - bar.thumbLength;
    const float previewBarX = documentViewportX(app) + documentViewportWidth(app) - dpi(app, 6.0f);
    const float grabY = chromeTopHeight(app) + dpi(app, 12.0f);
    check(closeTo(bar.trackStart, chromeTopHeight(app)) && grabY < bar.thumbStart + thumbH,
          "the preview's thumb starts below the tab strip" + where);
    check(cursorAfterMove(app, previewBarX, grabY) == arrow, "an arrow over the preview's scrollbar" + where);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM((int)previewBarX, (int)grabY));
    check(app.scrollbarDragging, "a press on the preview's scrollbar drags it" + where);
    const float dragged = 0.35f * track;
    check(cursorAfterMove(app, previewBarX, grabY + dragged) == arrow, "an arrow while the preview's scrollbar is dragged" + where);
    frame(app);
    const float expected = dragged / track * maxScroll;
    // The panes line up by source line, so the page settles within a few
    // lines of where the thumb was taken
    const float tolerance = std::max(90.0f, 0.04f * maxScroll);
    check(std::abs(app.scrollY - expected) <= tolerance,
          "the preview stays where its thumb was dragged, after the panes sync" + where + ": at " +
              std::to_string((int)app.scrollY) + ", dragged to " + std::to_string((int)expected));
    check(app.editorScrollY > (float)app.height, "the editor follows the preview" + where);
    // To the end and back
    cursorAfterMove(app, previewBarX, (float)app.height + 200.0f);
    frame(app);
    check(app.scrollY >= maxScroll - tolerance, "dragged to the end, the preview shows its end" + where);
    cursorAfterMove(app, previewBarX, -200.0f);
    frame(app);
    check(app.scrollY <= 1.0f && app.editorScrollY <= 1.0f, "dragged back up, both panes are at their top" + where);
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM((int)previewBarX, 0));
    check(!app.scrollbarDragging, "the release ends the preview's drag" + where);

    // A click on the track below the thumb jumps there, the editor with it
    const float clickY = (float)app.height * 0.75f;
    cursorAfterMove(app, previewBarX, clickY);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM((int)previewBarX, (int)clickY));
    frame(app);  // the button still held: frames paint before the release
    const float jumped = bar.scrollForThumb(clickY - thumbH * 0.5f);
    check(std::abs(app.scrollY - jumped) <= tolerance && app.editorScrollY > (float)app.height,
          "a click on the preview's track jumps both panes there" + where + ": at " +
              std::to_string((int)app.scrollY) + ", clicked " + std::to_string((int)jumped));
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM((int)previewBarX, (int)clickY));
    exitEditMode(app);
}

// Above the first block (here twelve lines of front matter) the preview
// follows the editor from the page's top to that block. The difference of
// source offsets, unsigned, wrapped there and sent it on to the second
// block (#291)
void aboveFirstBlock(App& app, const char* path, int width, bool wrap) {
    const std::string where = " (" + std::to_string(width) + (wrap ? ", wrap)" : ")");
    app.width = width;
    check(openDocumentInViewer(app, toWide(path)), "the front matter fixture opens" + where);
    enterEditMode(app);
    app.editorShowPreview = true;
    app.editorWordWrap = wrap;
    rebuildLineStarts(app);
    ensureLayoutComplete(app);
    frame(app);
    check(app.scrollAnchors.size() > 2, "the page records its blocks" + where);
    if (app.scrollAnchors.size() > 2) {
        const float firstBlock = app.scrollAnchors[0].renderedY - documentContentTop(app);
        const float lineHeight = app.editorTextFormat->GetFontSize() * 1.5f;
        float last = 0.0f;
        bool between = true, onward = true;
        for (float scroll = 1.0f; scroll < 11.0f * lineHeight; scroll += lineHeight / 3.0f) {
            app.editorScrollY = scroll;
            frame(app);
            between = between && app.scrollY >= 0.0f && app.scrollY <= firstBlock + 0.5f;
            onward = onward && app.scrollY >= last - 0.5f;
            last = app.scrollY;
        }
        check(between, "in the front matter the preview stays between the page's top and the first "
                       "block" + where);
        check(onward && last > 0.0f, "and moves on as the editor does" + where);
        app.editorScrollY = 1.0f;
        frame(app);
        check(app.scrollY < lineHeight, "a pixel down from the top keeps the page at its top" + where);
    }
    exitEditMode(app);
}
}  // namespace

// #314: long unwrapped lines get a bar along the editor's foot. It drags,
// takes track clicks and the horizontal wheel, and goes with wrapping on
void acrossBar(App& app, const char* path, int width) {
    const std::string where = " (" + std::to_string(width) + ")";
    const HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    app.width = width;
    check(openDocumentInViewer(app, toWide(path)), "the long lines open" + where);
    enterEditMode(app);
    app.editorShowPreview = true;
    app.editorWordWrap = false;
    app.editorScrollX = 0.0f;
    rebuildLineStarts(app);
    frame(app);
    ScrollbarGeometry bar = editorScrollbarH(app);
    check(bar.maxScroll > 0 && bar.thumbLength < bar.trackLength && closeTo(bar.thumbStart, bar.trackStart),
          "lines past the pane get a bar along its foot, its thumb at the start" + where);
    check(bar.trackStart + bar.trackLength <= editorPaneWidth(app) - dpi(app, 8.0f),
          "the bar keeps clear of the side bar's corner" + where);
    const float y = (float)app.height - dpi(app, 7.0f), grab = bar.thumbStart + dpi(app, 10.0f);
    check(cursorAfterMove(app, grab, y) == arrow, "an arrow over the bar" + where);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM((int)grab, (int)y));
    check(app.editorScrollbarHDragging, "a press on the thumb drags it" + where);
    check(cursorAfterMove(app, grab + 60.0f, y - 300.0f) == arrow, "still an arrow when the drag strays up" + where);
    check(app.editorScrollX > 0.0f && closeTo(editorScrollbarH(app).thumbStart, bar.thumbStart + 60.0f),
          "the thumb follows the pointer and the lines pan" + where);
    cursorAfterMove(app, (float)app.width + 400.0f, y);
    bar = editorScrollbarH(app);
    check(closeTo(app.editorScrollX, bar.maxScroll) && closeTo(bar.thumbStart + bar.thumbLength, bar.trackStart + bar.trackLength),
          "dragged to the end, the widest line's end is in view" + where);
    cursorAfterMove(app, -400.0f, y);
    check(app.editorScrollX <= 0.5f, "dragged back, the lines' starts are in view" + where);
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM(0, (int)y));
    check(!app.editorScrollbarHDragging, "the release ends the drag" + where);
    // A click on the track centres the thumb there
    const float click = std::floor(bar.trackStart + bar.trackLength * 0.7f);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM((int)click, (int)y));
    bar = editorScrollbarH(app);
    check(closeTo(bar.thumbStart + bar.thumbLength * 0.5f, click), "a click on the track centres the thumb there" + where);
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM((int)click, (int)y));
    // The horizontal wheel over the source pans it, as far as the end
    app.editorScrollX = 0.0f;
    const float reach = editorScrollbarH(app).maxScroll;
    app.mouseX = (int)(editorPaneWidth(app) * 0.5f);
    app.mouseY = 300;
    handleMouseHWheel(app, app.hwnd, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(app.mouseX, app.mouseY));
    check(closeTo(app.editorScrollX, dpi(app, 60.0f)), "the horizontal wheel over the source pans it" + where);
    for (int i = 0; i < 400; ++i) handleMouseHWheel(app, app.hwnd, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(app.mouseX, app.mouseY));
    check(closeTo(app.editorScrollX, reach), "and stops at the widest line's end" + where);
    // Wrapped, nothing runs past the pane
    app.editorWordWrap = true;
    app.editorScrollX = 0.0f;
    rebuildLineStarts(app);
    frame(app);
    check(editorScrollbarH(app).maxScroll == 0.0f, "wrapped lines have no bar along the foot" + where);
    app.editorWordWrap = false;
    exitEditMode(app);
}

int runSplitScrollbarTests(const char* path, const char* frontMatterPath, const char* longLinesPath) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Split scrollbar tests", WS_POPUP, 0, 0, 1050, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            for (bool wrap : {false, true}) {
                scenario(app, path, width, wrap);
                if (frontMatterPath) {
                    scenario(app, frontMatterPath, width, wrap);
                    aboveFirstBlock(app, frontMatterPath, width, wrap);
                }
            }
            if (longLinesPath) acrossBar(app, longLinesPath, width);
        }
    }
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Split scrollbars: " << failures << " failures\n";
    return failures ? 1 : 0;
}
