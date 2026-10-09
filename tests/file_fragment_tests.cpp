#include "d2d_init.h"
#include "editor.h"
#include "export.h"
#include "inline_style.h"
#include "i18n.h"
#include "input.h"
#include "link_target.h"
#include "render.h"
#include "settings.h"
#include "signals.h"
#include "tabs.h"
#include "utils.h"
#include <cmath>
#include <filesystem>
#include <fstream>
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

// Based on the PDF regression cases supplied by @msaitov in #237.
void pdfLinks(App& app) {
    namespace fs = std::filesystem;
    const auto root = fs::path(TINTA_FRAGMENT_FIXTURE).parent_path().parent_path() / L"pdf-links";
    const auto document = root / L"docs/index.md";
    const auto pdf = (root / L"pdf/report 2026.pdf").lexically_normal();
    const auto unicodePdf = (root / L"pdf/\u043e\u0442\u0447\u0451\u0442 \u4e2d\u6587 caf\u00e9.pdf").lexically_normal();
    tabOpenPath(app, app.hwnd, toUtf8(document.wstring()), true);
    const auto initialTabs = app.tabs.size();
    for (int theme : {0, 5}) for (int width : {650, 1050}) {
        applyTheme(app, theme); app.width = width; app.layoutDirty = true;
        app.scrollY = app.targetScrollY = 0;
        ensureLayoutComplete(app);
        check(std::count_if(app.root->children.begin(), app.root->children.end(),
              [](const auto& e) { return e->type == qmd::ElementType::Table; }) == 1 && app.codeBlocks.size() == 1,
              "PDF fixture retains mixed table and code content");
        for (const auto& path : {pdf, unicodePdf}) {
            for (const auto& url : {qmd::encodeLinkTarget({toUtf8(path.wstring()), {}, false}),
                                   qmd::encodeLinkTarget({toUtf8(path.lexically_relative(document.parent_path()).generic_wstring()), {}, false})}) {
                auto live = link(app, url);
                check(live.rfind("fileref-ok:", 0) == 0, "existing absolute/relative PDF is a live file reference");
                auto resolved = qmd::splitLinkTarget(live.substr(11)).path;
                check(fs::path(toWide(resolved)) == path && qmd::fileRefIsExternal(resolved),
                      "PDF path resolves against document folder with spaces and Unicode intact");
                app.linkPeekUrl = app.hoveredLink = live;
                handleLinkPeekTimer(app, app.hwnd);
                check(!app.linkPeekActive, "PDF never reaches the text preview renderer");
                app.linkPeekUrl.clear();
            }
        }
        // Exercise hover classification using the actual parsed Markdown hit box.
        auto first = app.linkRects.front();
        app.hoveredLink.clear();
        handleMouseMove(app, app.hwnd, MAKELPARAM(static_cast<int>(first.bounds.left + 3),
                                                static_cast<int>(first.bounds.top + 3)));
        check(app.hoveredLink.rfind("fileref-ok:", 0) == 0 && app.linkPeekUrl.empty(),
              "hovering a parsed PDF link does not arm the peek timer");
        for (bool selectionPath : {false, true}) {
            app.signalChips.clear();
            click(app, "../pdf/missing%20report.PDF", selectionPath);
            check(!app.createRefPending && !fs::exists(root / L"pdf/missing report.PDF"),
                  "missing uppercase PDF is never offered as a new text file");
            check(!app.signalChips.empty() && app.signalChips.back().text == tr(app, "toast.file_missing"),
                  "both click paths report a missing PDF");
        }
        check(link(app, "notes.txt").rfind("fileref-ok:", 0) == 0 &&
              link(app, "https://example.com/a.pdf") == "https://example.com/a.pdf" &&
              link(app, "https://example.com/a.pdf#page=3") == "https://example.com/a.pdf#page=3",
              "text references and remote PDF URLs keep their existing routing");
        check(link(app, "../pdf/report%202026.pdf#page=3") == "../pdf/report%202026.pdf#page=3",
              "PDF page-fragment support is not changed by the file-link fix");
        app.linkPeekUrl = app.hoveredLink = link(app, "notes.md#destination");
        handleLinkPeekTimer(app, app.hwnd);
        check(app.linkPeekActive, "Markdown hover preview still works next to PDF links");
        app.linkPeekActive = false; app.linkPeekUrl.clear();
        click(app, "missing-notes.txt");
        check(app.createRefPending, "missing text links still offer creation");
        app.createRefPending = false;
    }
    // A file can disappear after layout cached it as live. Exercise the real
    // Unicode shell launch failure, suppressing system dialogs and external apps.
    const auto vanished = fs::current_path() / (L"vanished \u4e2d\u6587 " + std::to_wstring(GetCurrentProcessId()) + L".pdf");
    const auto url = qmd::encodeLinkTarget({toUtf8(vanished.wstring()), {}, false});
    { std::ofstream out(vanished, std::ios::binary); out << "%PDF-1.4\n"; }
    check(link(app, url).rfind("fileref-ok:", 0) == 0, "launch-failure fixture is initially live");
    std::error_code ec;
    check(fs::remove(vanished, ec), "launch-failure fixture is removed before clicking");
    app.signalChips.clear();
    click(app, url);
    check(!app.signalChips.empty() && app.signalChips.back().severity == SIG_ERROR &&
          app.signalChips.back().traySub == vanished.wstring(),
          "failed Unicode shell launch reports the target rather than silently doing nothing");
    check(app.tabs.size() == initialTabs && !app.createRefPending,
          "PDF handling never creates an editor tab or empty document");
}

// Top of the rendered line holding text, or -1
float lineTop(App& app, const std::wstring& text) {
    const size_t at = app.docText.find(text);
    if (at == std::wstring::npos) return -1.0f;
    for (const auto& r : app.textRects) {
        if (at >= r.docStart && at < r.docStart + r.docLength) return r.rect.top;
    }
    return -1.0f;
}
float landingFor(App& app, float y) {
    return std::max(0.0f, std::min(y - chromeTopHeight(app) - dpi(app, 14),
                                   app.contentHeight - app.height));
}
void landedOn(App& app, const std::wstring& text, const char* message) {
    const float top = lineTop(app, text);
    check(top >= 0 && std::abs(app.scrollY - landingFor(app, top)) < 1, message);
}

// Raw HTML anchors as link targets (#255): the reporter's bold link in a
// table, and anchors alone above a heading, inside a heading, in a wrapping
// table cell, in an HTML block and with a Unicode id
void htmlAnchors(App& app) {
    namespace fs = std::filesystem;
    const auto fixture = fs::path(TINTA_FRAGMENT_FIXTURE).parent_path().parent_path() /
                         L"anchor-links-255.md";
    tabOpenPath(app, app.hwnd, toUtf8(fixture.wstring()), true);
    for (int theme : {0, 5}) for (int width : {650, 1050}) {
        applyTheme(app, theme); app.width = width; app.layoutDirty = true;
        app.scrollY = app.targetScrollY = 0;
        ensureLayoutComplete(app);
        bool tagsHidden = app.docText.find(L"</a>") == std::wstring::npos;
        for (const wchar_t* tag : {L"<a id=\"a1\"", L"<a name=\"b2\"", L"<a id=\"c3\"",
                                   L"<a id=\"d4\"", L"<a name=\"e5\""}) {
            tagsHidden = tagsHidden && app.docText.find(tag) == std::wstring::npos;
        }
        check(tagsHidden, "anchor tags are not drawn as text");
        check(app.docText.find(L"A1. Table link should navigate here") != std::wstring::npos &&
              app.docText.find(L"Inside an HTML block.") != std::wstring::npos &&
              app.docText.find(L"An <a id> at the start") != std::wstring::npos,
              "the text beside each anchor and tags in code spans still render");
        check(std::count_if(app.root->children.begin(), app.root->children.end(),
              [](const auto& e) { return e->type == qmd::ElementType::Table; }) == 2 &&
              app.codeBlocks.size() == 1, "the fixture keeps both tables and the code block");

        const App::LinkRect* a1 = nullptr;
        for (const auto& r : app.linkRects) if (r.url == "#a1") { a1 = &r; break; }
        check(a1 != nullptr, "the bold table link to #a1 is clickable");
        if (a1) {
            const int x = (int)(a1->bounds.left + 3), y = (int)(a1->bounds.top + 3);
            handleMouseMove(app, app.hwnd, MAKELPARAM(x, y));
            handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM(x, y));
            handleMouseUp(app, app.hwnd, 0, MAKELPARAM(x, y));
            landedOn(app, L"A1. Table link should navigate here",
                     "the table link lands on the line of its <a id>");
        }
        click(app, "#b2");
        const float b2 = lineTop(app, L"B2 heading");
        check(b2 >= 0 && app.scrollY <= landingFor(app, b2) + 0.5f &&
              landingFor(app, b2) - app.scrollY <= dpi(app, 24),
              "an anchor alone above a heading lands just above it");
        click(app, "#c3");
        landed(app, "third-heading");
        const float viaAnchor = app.scrollY;
        click(app, "#third-heading");
        check(std::abs(app.scrollY - viaAnchor) < 1,
              "an anchor inside a heading lands on the heading, which keeps its slug");
        click(app, "#d4");
        landedOn(app, L"Cell target", "an anchor in a table cell lands on its row");
        check(std::count_if(app.htmlAnchors.begin(), app.htmlAnchors.end(),
              [](const auto& a) { return a.first == "d4"; }) == 1,
              "the table's trial measurements leave no stray anchors");
        click(app, "#e5");
        landedOn(app, L"Inside an HTML block.", "an anchor in an HTML block is a target");
        click(app, "#%E4%B8%AD%E6%96%87");
        landedOn(app, L"\u4E2D\u6587\u951A\u70B9\u7684\u76EE\u6807\u6BB5\u843D", "a Unicode anchor id is a target");
        click(app, "#plain-heading");
        landed(app, "plain-heading");
        float before = app.scrollY;
        click(app, "#absent");
        check(app.scrollY == before, "a missing anchor leaves the view alone");
    }

    const fs::path html = fs::current_path() / L"anchor-links-255.html";
    check(exportHtmlFile(app, html.wstring()), "the fixture exports to HTML");
    std::ifstream in(html, std::ios::binary);
    const std::string exported((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(exported.find("<a id=\"a1\"></a>") != std::string::npos &&
          exported.find("<a id=\"e5\"></a>") != std::string::npos &&
          exported.find("&lt;a id=&quot;a1") == std::string::npos &&
          exported.find("&lt;a name=&quot;e5") == std::string::npos,
          "the HTML export keeps the anchors as anchors");

    // Parser and layout details on small documents
    auto layoutOf = [&](const char* markdown) {
        auto doc = app.parser.parse(markdown);
        app.root = doc.root;
        layoutDocument(app);
        return doc.success;
    };
    layoutOf("See <a href=\"https://example.com\">the site</a> here.\n");
    check(app.docText.find(L"<a href=\"https://example.com\">the site</a>") != std::wstring::npos,
          "an inline <a href> stays literal text as before");
    layoutOf("<A NAME=top>Top</A> and <a id='self'/>self closing.\n");
    float y = 0;
    check(app.docText.find(L"Top and self closing.") != std::wstring::npos &&
          documentTargetY(app, "top", y) && documentTargetY(app, "self", y),
          "unquoted, uppercase and self-closing anchors are targets");
    layoutOf("<a id=\"open\">never closed\n\nThis keeps its literal </a>\n");
    check(app.docText.find(L"literal </a>") != std::wstring::npos,
          "an unclosed anchor does not swallow a later paragraph's </a>");
    layoutOf("<a id=\"dup\"></a>First.\n\nSecond.\n\n<a id=\"dup\"></a>Third.\n");
    check(documentTargetY(app, "dup", y) && std::abs(y - lineTop(app, L"First.")) < 1,
          "the first of a repeated id wins, as in a browser");
    layoutOf("Intro.\n\n## Head\n");
    const float plainY = app.headings.empty() ? -1.0f : app.headings[0].y;
    layoutOf("Intro.\n\n<a id=\"lone\"></a>\n## Head\n");
    const float anchoredY = app.headings.empty() ? -2.0f : app.headings[0].y;
    check(std::abs(plainY - anchoredY) < 0.5f && documentTargetY(app, "lone", y),
          "an anchor-only paragraph marks its target and takes no room");
    layoutOf("Intro.\n\n<a name=\"block\"/>\n\n## Head\n");
    const float blockY = app.headings.empty() ? -3.0f : app.headings[0].y;
    check(std::abs(plainY - blockY) < 0.5f && documentTargetY(app, "block", y),
          "an anchor-only HTML block marks its target and takes no room");
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
    exitEditMode(app);
    pdfLinks(app);
    htmlAnchors(app);
    DestroyWindow(app.hwnd); app.hwnd = nullptr; state.reset(); CoUninitialize();
    std::cout << "File fragments: " << failures << " failures\n";
    return failures ? 1 : 0;
}
