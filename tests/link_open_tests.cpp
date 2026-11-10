// Links a document may have the shell open (input.h, linkOpening). A click
// handed the address to ShellExecute as written: a program by its path or by
// its bare name, a script beside the document, a file on a server the
// document chose, any registered program's own address. Checked here: what
// each kind of address is read as, and through the mouse and key handlers
// what opens at once, what asks first and what the dialog's answer does. The
// shell is not called: the test takes what would go to it.
#include "d2d_init.h"
#include "editor.h"
#include "frame_preview.h"
#include "input.h"
#include "keystroke.h"
#include "overlays.h"
#include "render.h"
#include "settings.h"
#include "tabs.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL link opening: " << message << '\n';
        ++failures;
    }
}

std::wstring normal(const wchar_t* path) { return fs::path(path).lexically_normal().wstring(); }

// What an address is read as, for a document in C:/notes
void reads(const char* address, LinkKind kind, bool ask, const wchar_t* target = nullptr, const wchar_t* folder = L"C:/notes") {
    const LinkOpening opening = linkOpening(address, normal(folder));
    const std::string name = std::string(1, '"') + address + '"';
    check(opening.kind == kind, name + " is read as the kind it is");
    check(opening.ask == ask, name + (ask ? " asks first" : " opens without asking"));
    if (target && kind == LinkKind::Path) {
        check(opening.target == normal(target), name + " is the file " + toUtf8(normal(target)) + ": " + toUtf8(opening.target));
    }
    if (target && kind != LinkKind::Path) check(opening.target == target, name + " goes to the shell as written");
}

void rules() {
    reads("https://example.com/a?b=1#c", LinkKind::Web, false, L"https://example.com/a?b=1#c");
    reads("HTTP://Example.com", LinkKind::Web, false);
    reads("  mailto:someone@example.com ", LinkKind::Web, false, L"mailto:someone@example.com");
    // Any other program's own address
    for (const char* address : {"ms-settings:display", "vscode://file/c:/x", "zotero://select/items/1", "obsidian://open?vault=a",
                                "shell:startup", "javascript:alert(1)"}) {
        reads(address, LinkKind::Address, true);
    }
    // Programs and scripts: by path, beside the document, and by a bare name
    reads("C:/Windows/System32/calc.exe", LinkKind::Path, true, L"C:/Windows/System32/calc.exe");
    reads("notepad.exe", LinkKind::Path, true, L"C:/notes/notepad.exe");
    for (const char* address : {"run.cmd", "tools/install.bat", "a.lnk", "setup.msi", "page.hta", "x.vbs", "x.js", "x.scr", "x.com"}) {
        reads(address, LinkKind::Path, true);
    }
    // Documents open in their own application
    reads("report.pdf", LinkKind::Path, false, L"C:/notes/report.pdf");
    reads("report%20final.pdf#page=3", LinkKind::Path, false, L"C:/notes/report final.pdf");
    reads("../other/data.csv", LinkKind::Path, false, L"C:/other/data.csv");
    reads("D:/shared/deck.pptx", LinkKind::Path, false, L"D:/shared/deck.pptx");
    reads("file:///C:/notes/a%20b.pdf", LinkKind::Path, false, L"C:/notes/a b.pdf");
    reads("file:///C:/Windows/System32/calc.exe", LinkKind::Path, true, L"C:/Windows/System32/calc.exe");
    for (const char* address : {"photo.png", "notes.txt", "sheet.xlsx", "movie.mp4", "folder/inner"}) {
        reads(address, LinkKind::Path, false);
    }
    // A server the document chose asks, whatever the file; its own does not
    reads("//server/share/doc.pdf", LinkKind::Path, true, L"//server/share/doc.pdf");
    reads("file://server/share/doc.pdf", LinkKind::Path, true, L"//server/share/doc.pdf");
    check(linkOpening("//server/share/doc.pdf", normal(L"C:/notes")).remote, "a file on another server is one not to look for");
    reads("//server/share/doc.pdf", LinkKind::Path, false, L"//server/share/doc.pdf", L"//server/share/docs");
    reads("//other/share/doc.pdf", LinkKind::Path, true, nullptr, L"//server/share/docs");
    reads("tool.exe", LinkKind::Path, true, L"//server/share/docs/tool.exe", L"//server/share/docs");
    // Nothing to open
    reads("", LinkKind::None, false);
    reads("   ", LinkKind::None, false);
    check(linkOpening("report.pdf", L"").kind == LinkKind::None, "a name alone with no document folder opens nothing");
}

// A click on the link whose address holds `part`, through the mouse handlers
bool clickLink(App& app, const std::string& part) {
    ensureLayoutComplete(app);
    for (const auto& link : app.linkRects) {
        if (link.url.find(part) == std::string::npos) continue;
        const int x = (int)((link.bounds.left + link.bounds.right) / 2);
        const int y = (int)((link.bounds.top + link.bounds.bottom) / 2 - app.scrollY);
        handleMouseMove(app, app.hwnd, MAKELPARAM(x, y));
        handleMouseDown(app, app.hwnd, MK_LBUTTON, MAKELPARAM(x, y));
        handleMouseUp(app, app.hwnd, 0, MAKELPARAM(x, y));
        return true;
    }
    return false;
}

void clicks(App& app, const char* fixture, const fs::path& out) {
    const fs::path dir = fs::current_path() / "link-opening";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "pics", ec);
    const fs::path doc = dir / "Links.md";
    fs::copy_file(fixture, doc, ec);
    check(!ec, "the fixture copies");
    std::ofstream(dir / "report.pdf", std::ios::binary) << "%PDF-1.4\n";
    std::ofstream(dir / "run.cmd", std::ios::binary) << "@echo off\n";

    std::vector<std::wstring> opened;
    shellOpenHook() = [&](const std::wstring& what) { opened.push_back(what); };
    auto asks = [&](const std::wstring& target) {
        return app.createRefPending && app.createRefOpens && toWide(app.createRefPath) == target && opened.empty();
    };

    tabsInit(app);
    check(openDocumentInViewer(app, doc.wstring()), "the fixture opens");

    // Opened at once
    check(clickLink(app, "example.com") && opened.size() == 1 && opened[0] == L"https://example.com/tinta" && !app.createRefPending,
          "a web address opens at a click");
    opened.clear();
    check(clickLink(app, "report.pdf") && opened.size() == 1 && opened[0] == (dir / "report.pdf").wstring() && !app.createRefPending,
          "a document beside the file opens in its own application, by its full path");
    opened.clear();
    check(clickLink(app, "pics") && opened.size() == 1 && opened[0] == (dir / "pics").wstring() && !app.createRefPending, "a folder opens");
    opened.clear();

    // A script beside the document: asked, and Esc opens nothing
    check(clickLink(app, "run.cmd") && asks((dir / "run.cmd").wstring()), "a script beside the document asks first, with its full path");
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 560}) {
            app.width = width;
            app.layoutDirty = true;
            ensureLayoutComplete(app);
            const fs::path file = out / ("open-link-theme" + std::to_string(theme) + "-" + std::to_string(width) + ".png");
            check(saveFramePreview(app, file, [&] { renderCreateRefDialog(app); }), "frame saved: " + file.filename().string());
        }
    }
    applyTheme(app, 0);
    app.width = 1050;
    app.layoutDirty = true;
    keystroke(app, VK_ESCAPE);
    check(!app.createRefPending && !app.createRefOpens && opened.empty(), "Esc closes the dialog and opens nothing");
    // And Enter opens it (a press soon after another on the same spot is a double-click)
    Sleep(GetDoubleClickTime() + 30);
    check(clickLink(app, "run.cmd") && asks((dir / "run.cmd").wstring()), "the script asks again");
    keystroke(app, VK_RETURN);
    check(!app.createRefPending && opened.size() == 1 && opened[0] == (dir / "run.cmd").wstring(), "Enter opens what was asked about");
    opened.clear();
    check(!fs::exists(dir / "run.cmd.md") && fs::file_size(dir / "run.cmd") == 10, "nothing is created or changed by the dialog");

    // A bare name is a file beside the document, and there is none
    check(clickLink(app, "notepad.exe") && opened.empty() && !app.createRefPending, "a program's bare name opens nothing: no such file is beside the document");
    // A program by its path, another program's address, a file on a server
    check(clickLink(app, "calc.exe") && asks(normal(L"C:/Windows/System32/calc.exe")), "a program asks first");
    keystroke(app, VK_ESCAPE);
    check(clickLink(app, "ms-settings") && asks(L"ms-settings:display"), "another program's address asks first, shown as written");
    keystroke(app, VK_ESCAPE);
    check(clickLink(app, "tinta-test-server") && asks(normal(L"//tinta-test-server.invalid/share/tool.exe")), "a file on a server the document chose asks first");
    keystroke(app, VK_ESCAPE);
    check(opened.empty() && !app.createRefPending, "none of them opened");

    // The dialog's other question is as it was: a missing text file is offered to be created
    app.hoveredLink = "fileref-missing:" + toUtf8((dir / "not-there.md").wstring());
    handleMouseUp(app, app.hwnd, 0, MAKELPARAM(100, 100));
    check(app.createRefPending && !app.createRefOpens, "a missing file's link still asks to create it");
    keystroke(app, VK_ESCAPE);
    check(!app.createRefPending && !fs::exists(dir / "not-there.md"), "and Esc creates nothing");
    shellOpenHook() = nullptr;
}

}  // namespace

int runLinkOpenTests(const char* fixture) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Link opening tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    applyTheme(app, 0);

    const fs::path out = fs::current_path() / "frames";
    std::error_code ec;
    fs::create_directories(out, ec);
    rules();
    clicks(app, fixture, out);

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Link opening: " << failures << " failures\n";
    return failures ? 1 : 0;
}
