#include "d2d_init.h"
#include "editor.h"
#include "inline_style.h"
#include "input.h"
#include "link_target.h"
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <windowsx.h>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string link(App& app, const std::string& url) {
    auto elem = std::make_shared<qmd::Element>(qmd::ElementType::Link);
    elem->url = url;
    auto text = std::make_shared<qmd::Element>(qmd::ElementType::Text);
    text->text = "test";
    elem->children.push_back(text);
    std::vector<StyledRun> runs;
    flattenInline(app, {elem}, {}, 20, runs);
    check(runs.size() == 1 && elem->url == url, "flatten preserves original export URL");
    return runs.empty() ? std::string{} : runs[0].style.linkUrl;
}
void click(App& app, const std::string& url, bool selectionPath = false) {
    app.hoveredLink = link(app, url);
    app.selecting = selectionPath;
    app.mouseDown = selectionPath;
    app.selectionMode = App::SelectionMode::Normal;
    app.lastClickX = app.lastClickY = 100;
    app.mouseX = app.mouseY = 100;
    app.selShiftExtend = false;
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM(100,100));
}
void landed(App& app, const std::string& id) {
    ensureLayoutComplete(app);
    bool found = false;
    for (const auto& h : app.headings) if (h.id == id) {
        found = true;
        float expected = std::max(0.0f, std::min(h.y - chromeTopHeight(app) - dpi(app,14),
                                                app.contentHeight - app.height));
        check(std::abs(app.scrollY - expected) < 1, "destination heading is aligned in the viewport");
    }
    check(found, "destination heading exists");
    check(app.pendingScrollRestore < 0, "explicit fragment wins over saved reading position");
}
}

int runFileFragmentTests() {
    namespace fs = std::filesystem;
    auto state = std::make_unique<App>(); App& app = *state;
    app.hwnd = CreateWindowExW(0,L"STATIC",L"File fragment tests",WS_POPUP,0,0,1050,900,nullptr,nullptr,nullptr,nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050; app.height = 900; updateTextFormats(app);
    const std::string source = toUtf8(fs::path(TINTA_FRAGMENT_FIXTURE).lexically_normal().wstring());
    const auto folder = fs::path(toWide(source)).parent_path();
    const std::string target = toUtf8((folder / L"target notes.md").wstring());
    check(openDocumentInViewer(app,toWide(source)), "source fixture opens");
    tabsInit(app); ensureLayoutComplete(app);
    check(std::count_if(app.root->children.begin(), app.root->children.end(),
          [](const auto& e) { return e->type == qmd::ElementType::Table; }) == 1 && app.codeBlocks.size() == 1, "source table and code remain intact");
    for (const char* url : {"https://example.com/a.md#b", "mailto:a.md#b", "//host/a.md#b", "notes.txt#b"})
        check(link(app,url) == url, "external and non-Markdown fragment URLs are unchanged");
    check(link(app,"target%20notes.md#destination-heading").rfind("fileref-ok:",0) == 0,
          "fragment is excluded from extension and existence checks");
    auto decoded = qmd::splitLinkTarget("a%23b%2523.md#%E4%B8%AD%23x");
    auto roundtrip = qmd::splitLinkTarget(qmd::encodeLinkTarget(decoded));
    check(decoded.path == "a#b%23.md" && decoded.path == roundtrip.path && decoded.fragment == roundtrip.fragment,
          "encoded path delimiters and UTF-8 fragment survive one decoding per stage");
    check(qmd::decodeLinkComponent("a%zz%00%2") == "a%zz%00%2", "malformed escapes and encoded NUL remain literal");

    // Real hit rectangles and mouse handlers: the ordinary selection route.
    auto first = app.linkRects.front();
    int x = (int)(first.bounds.left + 3), y = (int)(first.bounds.top + 3);
    handleMouseMove(app,app.hwnd,MAKELPARAM(x,y));
    handleMouseDown(app,app.hwnd,MK_LBUTTON,MAKELPARAM(x,y));
    handleMouseUp(app,app.hwnd,0,MAKELPARAM(x,y));
    check(app.currentFile == target && app.tabs.size() == 2, "click opens destination as a tab");
    landed(app,"destination-heading");
    check(std::count_if(app.root->children.begin(), app.root->children.end(),
          [](const auto& e) { return e->type == qmd::ElementType::Table; }) == 1 && app.codeBlocks.size() == 1, "destination table and code remain intact");
    navigateBack(app,app.hwnd);
    check(app.currentFile == source, "Back returns to source");
    navigateForward(app,app.hwnd);
    check(app.currentFile == target, "Forward returns to target");

    for (int theme : {0,5}) for (int width : {650,1050}) {
        applyTheme(app,theme); app.width = width; updateTextFormats(app); app.layoutDirty = true;
        app.pendingScrollRestore = 17.0f;
        click(app,"target%20notes.md#destination-heading"); landed(app,"destination-heading");
        check(app.tabs.size() == 2, "existing tab is reused");
        click(app,"target%20notes.md#%E4%B8%AD%E6%96%87%E6%A0%87%E9%A2%98",true); landed(app,u8"\u4e2d\u6587\u6807\u9898");
        click(app,"target%20notes.md#repeated-1"); landed(app,"repeated-1");
        click(app,"target%20notes.md#"); check(app.scrollY == 0, "empty fragment goes to top");
        click(app,"#%E4%B8%AD%E6%96%87%E6%A0%87%E9%A2%98"); landed(app,u8"\u4e2d\u6587\u6807\u9898");
        float before = app.scrollY;
        click(app,"target%20notes.md#absent");
        check(app.currentFile == target && app.scrollY == before && !app.createRefPending,
              "missing heading keeps existing file open without a create-file prompt");
    }
    click(app,"hash%23notes.md#destination-heading"); landed(app,"destination-heading");
    check(fs::path(toWide(app.currentFile)).filename() == L"hash#notes.md", "encoded hash belongs to filename");
    click(app,"literal%2523.md#destination-heading"); landed(app,"destination-heading");
    check(fs::path(toWide(app.currentFile)).filename() == L"literal%23.md", "literal percent is not decoded twice");
    app.linkPeekUrl = app.hoveredLink = link(app,"target%20notes.md#destination-heading");
    handleLinkPeekTimer(app,app.hwnd);
    check(app.linkPeekActive, "hover preview reads the file without its fragment");
    app.linkPeekActive = false; app.linkPeekUrl.clear();
    click(app,"not-created.md#destination-heading");
    check(app.createRefPending && fs::path(toWide(app.createRefPath)).filename() == L"not-created.md",
          "missing-file prompt receives only the filename");
    app.createRefPending = false;

    click(app,"target%20notes.md#destination-heading");
    enterEditMode(app);
    const auto original = app.editorText;
    app.editorText += L"\n## Unsaved heading\n\nUnsaved content.\n";
    app.editorDirty = true; rebuildLineStarts(app); editorReparse(app,true);
    const auto dirty = app.editorText;
    for (bool preview : {true,false}) {
        app.editorShowPreview = preview;
        tabOpenPath(app,app.hwnd,source,true);
        click(app,"target%20notes.md#unsaved-heading");
        check(app.editMode && app.editorDirty && app.editorText == dirty, "navigation preserves unsaved target buffer");
        landed(app,"unsaved-heading");
        check(app.editorText.substr(app.editorCursorPos,18) == L"## Unsaved heading", "source caret reaches unsaved heading");
        D2D1_POINT_2F point{};
        check(editorCaretPoint(app,point) && point.y >= chromeTopHeight(app) && point.y < app.height,
              "source heading is visible with preview shown or hidden");
    }
    app.editorText = original; app.editorDirty = false;
    DestroyWindow(app.hwnd); app.hwnd = nullptr; state.reset(); CoUninitialize();
    std::cout << "File fragments: " << failures << " failures\n";
    return failures ? 1 : 0;
}
