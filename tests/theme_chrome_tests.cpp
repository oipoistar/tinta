// The window and the diagrams in a CSS theme (docs/specs/theming.md,
// phase 3): --ui-* colour the tab strip and caption, panels, menus, popups
// and cards and their edges; --mermaid-* colour Mermaid diagrams on the
// page and in the HTML and Word exports. A theme that sets neither, every
// built-in among them, draws as it did. A frame of each chrome surface is
// saved beside the test, for a look and for comparing builds.
//
// The frames use only what Tinta had before phase 3, so the same capture
// runs in a build without it (TINTA_CHROME_BASELINE) and the two sets can
// be compared pixel for pixel.
#include "d2d_init.h"
#include "document.h"
#include "editrail.h"
#include "export.h"
#include "file_utils.h"
#include "frame_preview.h"
#include "input.h"
#include "overlays.h"
#include "print.h"
#include "render.h"
#include "signals.h"
#include "startpage.h"
#include "tabs.h"
#include "utils.h"
#ifndef TINTA_CHROME_BASELINE
#include "themes_css.h"
#include "ui_paint.h"
#endif

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
std::string read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()};
}

// Every chrome surface over the page of the first tab, one frame each,
// then the page at its diagrams
void captureChrome(App& app, const fs::path& out, const std::string& prefix, const fs::path& folder) {
    auto shot = [&](const char* name, const std::function<void()>& overlay) {
        check(saveFramePreview(app, out / (prefix + "-" + name + ".png"), overlay), prefix + ": the " + name + " frame is saved");
    };
    app.mouseX = app.mouseY = -100;
    app.cursorBlinkOn = false;
    app.scrollY = app.targetScrollY = 0;
    layoutDocument(app);
    shot("strip", [] {});

    app.showToc = true;
    app.tocAnimation = 1.0f;
    shot("contents", [&] { renderToc(app); });
    app.showToc = false;

    app.folderBrowserPath = folder.wstring();
    populateFolderItems(app);
    app.showFolderBrowser = true;
    app.folderBrowserAnimation = 1.0f;
    shot("browser", [&] { renderFolderBrowser(app); });
    app.showFolderBrowser = false;

    app.showSearch = true;
    app.searchAnimation = 1.0f;
    shot("find", [&] { renderSearchOverlay(app); });
    app.showSearch = false;

    openContextMenu(app, 260.0f, 160.0f);
    app.contextMenuAnimation = 1.0f;
    app.hoveredContextMenuItem = -1;
    shot("menu", [&] { renderContextMenu(app); });
    app.showContextMenu = false;

    const App::TabHit* second = nullptr;
    for (const auto& hit : app.tabHits) {
        if (hit.index == 1) second = &hit;
    }
    check(second != nullptr, prefix + ": the second tab is in the strip");
    if (second) {
        openTabMenu(app, 1, second->rect.left + 10.0f, second->rect.bottom);
        app.tabMenuAnimation = 1.0f;
        shot("tab-menu", [&] { renderTabMenu(app); });
        closeTabMenu(app);
    }

    app.showTabSwitcher = true;
    app.tabSwitcherHover = -1;
    shot("switcher", [&] { renderTabSwitcher(app); });
    app.showTabSwitcher = false;

    app.showSettings = true;
    app.settingsSection = 1;
    app.settingsAnimation = 1.0f;
    shot("settings", [&] { renderSettingsOverlay(app); });
    app.showSettings = false;

    app.showHelp = true;
    app.helpAnimation = 1.0f;
    app.helpScroll = 0.0f;
    shot("help", [&] { renderHelpOverlay(app); });
    app.showHelp = false;

    signalPush(app, SIG_WARN, SIGI_WARNING, L"The theme has ", L"2 problems", L"", SIGA_NONE, SIGC_NONE, false);
    signalPush(app, SIG_INFO, SIGI_INFO, L"Saved ", L"Harbour log.md", L"", SIGA_NONE, SIGC_NONE, false);
    shot("notices", [&] { renderSignalChips(app); });
    app.signalChips.clear();

    app.confirmExitPending = true;
    shot("prompt", [&] { renderConfirmExitDialog(app); });
    app.confirmExitPending = false;

    app.showThemeChooser = true;
    app.themeChooserAnimation = 1.0f;
    app.themeChooserPreviewBase = -1;
    shot("chooser", [&] { renderThemeChooser(app); });
    app.showThemeChooser = false;

    // The editor's rail and its menu, drawn as edit mode draws them
    app.editMode = true;
    app.editRailAnim = 1.0f;
    app.editCtxOpen = true;
    app.editCtxRailOnly = false;
    app.editCtxSub = 0;
    app.editCtxX = 300.0f;
    app.editCtxY = 140.0f;
    shot("edit-rail", [&] {
        renderEditRail(app);
        renderEditCtxMenu(app);
    });
    app.editCtxOpen = false;
    app.editMode = false;

    // The start page over an empty window
    app.startPageHover = 0;
    shot("start", [&] {
        app.renderTarget->Clear(app.theme.background);
        renderStartPage(app);
        renderTabStrip(app);
    });

    // The page where its diagrams are, drawn whole as the window draws it,
    // shapes and connectors too: the flowchart and the sequence, then the
    // pie
    auto page = [&] {
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc;
        if (FAILED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc)))) return;
        dc->Clear(app.theme.background);
        drawLayoutRange(app, dc.Get(), app.brush, app.scrollY, app.scrollY + (float)app.height,
                        documentViewportX(app) - app.scrollX, 0.0f, D2D1_DRAW_TEXT_OPTIONS_NONE);
        renderTabStrip(app);
    };
    float top = 0.0f;
    for (const auto& connector : app.layoutConnectors) {
        top = connector.bounds.top;
        break;
    }
    app.scrollY = app.targetScrollY = std::max(0.0f, top - 140.0f);
    shot("diagrams", page);
    app.scrollY = app.targetScrollY = app.scrollY + 640.0f;
    shot("diagrams-2", page);
    app.scrollY = app.targetScrollY = 0;
}

#ifndef TINTA_CHROME_BASELINE
const css::ImportSource kNoImports = [](std::string_view, std::string_view, std::string&, std::string&) {
    return false;
};
bool closeTo(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    const float e = 1.5f / 255.0f;
    return std::abs(a.r - b.r) < e && std::abs(a.g - b.g) < e && std::abs(a.b - b.b) < e && std::abs(a.a - b.a) < e;
}
bool sameRgb(D2D1_COLOR_F a, uint32_t hex) {
    D2D1_COLOR_F b = hexColor(hex);
    b.a = a.a;
    return closeTo(a, b);
}
bool is(const std::optional<D2D1_COLOR_F>& value, uint32_t hex) { return value && closeTo(*value, hexColor(hex)); }

// The sheet's variables in the theme Tinta draws with, for each side of
// light-dark(), the narrower Mermaid variable first
void variables() {
    const std::string sheet = read(fs::path(TINTA_THEME_CSS_FIXTURES) / "chrome.css");
    CustomTheme light, dark;
    CssTheme compiled = compileCssTheme(sheet, "chrome.css", false, kNoImports, light);
    compileCssTheme(sheet, "chrome.css", true, kNoImports, dark);
    for (const auto& d : compiled.diagnostics) std::cerr << "chrome.css:" << d.line << ": " << d.message << '\n';
    check(compiled.diagnostics.empty() && compiled.lightDark, "chrome.css reads cleanly and follows Windows");
    const D2DTheme& l = light.theme;
    const D2DTheme& d = dark.theme;
    check(is(l.uiBackground, 0x1D2B38) && is(l.uiText, 0xE6EDF3) && is(l.uiAccent, 0xF59E42) &&
              is(l.uiPanel, 0x243444) && is(l.uiBorder, 0x3B4F63),
          "the five --ui-* colours, light");
    check(is(d.uiBackground, 0x0D141A) && is(d.uiText, 0xD5DEE6) && is(d.uiAccent, 0xF59E42) &&
              is(d.uiPanel, 0x151E27) && is(d.uiBorder, 0x2A3846),
          "light-dark() gives the window its dark side");
    check(!l.isDark && d.isDark && is(std::optional<D2D1_COLOR_F>(l.background), 0xFBFAF6),
          "the page stays light while the window is dark");
    const DiagramColors& m = l.mermaid;
    check(is(m.nodeFill, 0xE3F0F8) && is(m.nodeBorder, 0x0B6E99) && is(m.nodeText, 0x10283A) &&
              is(m.text, 0x10283A),
          "primaryColor, its border and its text colour the nodes and the text");
    check(is(m.line, 0x52677A) && is(m.secondary, 0xFDE8CF) && is(m.edgeLabel, 0xFFF8EF) &&
              is(m.clusterFill, 0xEEF3F6) && is(m.clusterText, 0xB45309),
          "lineColor, secondaryColor, edgeLabelBackground, tertiaryColor and titleColor");
    check(!m.link && !m.clusterBorder && !m.background, "what the sheet leaves out stays the page's");
    check(is(m.series[0], 0x0B6E99) && is(m.series[1], 0xF59E42) && is(m.series[3], 0xC2410C) && !m.series[4],
          "pie1 to pie4 lead the series palette");
    check(is(d.mermaid.nodeFill, 0x1C3346) && is(d.mermaid.text, 0xE3EEF6), "and the diagrams their dark side");

    CustomTheme order;
    compileCssTheme(":root { --mermaid-primary-color: #111111; --mermaid-main-bkg: #222222;\n"
                    "  --mermaid-secondary-color: #333333; --mermaid-tertiary-text-color: #444444;\n"
                    "  --mermaid-text-color: #666666; --mermaid-primary-text-color: #777777 }\n"
                    ".diagram { --mermaid-line-color: #555555 }\nbody { --ui-text: #abcdef }\n",
                    "order.css", false, kNoImports, order);
    const DiagramColors& o = order.theme.mermaid;
    check(is(o.nodeFill, 0x222222), "mainBkg wins over primaryColor");
    check(is(o.edgeLabel, 0x333333) && is(o.secondary, 0x333333), "secondaryColor fills edge labels without edgeLabelBackground");
    check(is(o.clusterText, 0x444444), "tertiaryTextColor titles subgraphs without titleColor");
    check(is(o.text, 0x666666) && is(o.nodeText, 0x777777), "textColor for text, primaryTextColor for the nodes'");
    check(is(o.line, 0x555555), "a rule on .diagram reaches the diagrams");
    check(is(order.theme.uiText, 0xABCDEF) && !order.theme.uiBackground, "--ui-* set on body reach the window");
}

// A built-in or themes.ini theme becomes CSS without the new variables, and
// comes back without any
void builtins() {
    for (int i = 0; i < THEME_COUNT; i++) {
        const std::string name = toUtf8(THEMES[i].name);
        const std::string sheet = themeToCss(THEMES[i]);
        check(sheet.find("--ui-") == std::string::npos && sheet.find("--mermaid-") == std::string::npos,
              name + ": its CSS names no window or diagram variables");
        CustomTheme back;
        compileCssTheme(sheet, name + ".css", THEMES[i].isDark, kNoImports, back);
        const D2DTheme& t = back.theme;
        bool series = false;
        for (const auto& colour : t.mermaid.series) series |= colour.has_value();
        check(!t.uiBackground && !t.uiText && !t.uiAccent && !t.uiPanel && !t.uiBorder && !t.mermaid.nodeFill &&
                  !t.mermaid.nodeBorder && !t.mermaid.nodeText && !t.mermaid.text && !t.mermaid.line &&
                  !t.mermaid.secondary && !t.mermaid.edgeLabel && !t.mermaid.clusterFill && !series,
              name + ": and comes back with none");
    }
}

// Misspelt and unknown names, and values that are no colour, are reported
// on their lines; the rest of the sheet applies
void problems() {
    CustomTheme wrong;
    CssTheme compiled = compileCssTheme(":root {\n"
                                        "  --ui-backgound: #123;\n"
                                        "  --ui-sidebar: #456;\n"
                                        "  --ui-text: 12px;\n"
                                        "  --mermaid-primry-color: #789;\n"
                                        "  --mermaid-actor-bkg: #abc;\n"
                                        "  --ui-accent: var(--missing, #def);\n"
                                        "  --brand: #fff;\n"
                                        "}\n",
                                        "wrong.css", false, kNoImports, wrong);
    auto at = [&](int line, const char* words, css::Diagnostic::Level level) {
        for (const auto& d : compiled.diagnostics) {
            if (d.line == line && d.level == level && d.message.find(words) != std::string::npos) return true;
        }
        return false;
    };
    check(at(2, "did you mean `--ui-background`", css::Diagnostic::Warning), "a misspelt --ui-* name, with the right one");
    check(at(3, "the window takes", css::Diagnostic::Warning), "an unknown --ui-* name, with the ones there are");
    check(at(4, "is not a colour", css::Diagnostic::Warning), "a --ui-* value that is no colour");
    check(at(5, "did you mean `--mermaid-primary-color`", css::Diagnostic::Warning), "a misspelt Mermaid variable");
    check(at(6, "is not drawn by Tinta", css::Diagnostic::Note), "a Mermaid variable Tinta has no part for");
    check(compiled.diagnostics.size() == 5, "and nothing for a var() or a variable of the sheet's own");
    check(!wrong.theme.uiText && is(wrong.theme.uiAccent, 0xDDEEFF), "the rest applies: a var() fallback is a colour");
}

// The functions the chrome draws with: today's derivations for a built-in
// theme, the sheet's colours for the CSS one
void chromeColours(App& app, int sheetIndex) {
    for (int index : {0, 5}) {
        applyTheme(app, index);
        const D2DTheme& t = app.theme;
        const float f = t.isDark ? 0.78f : 0.955f;
        check(closeTo(tabStripBackground(app), D2D1::ColorF(t.background.r * f, t.background.g * f, t.background.b * f, t.background.a)),
              toUtf8(t.name) + ": the strip is the page's background, darkened, as before");
        const float lift = t.isDark ? 0.012f : 0.31f;
        D2D1_COLOR_F panel = t.background;
        panel.r += (1.0f - panel.r) * lift;
        panel.g += (1.0f - panel.g) * lift;
        panel.b += (1.0f - panel.b) * lift;
        panel.a = 0.98f;
        check(closeTo(promptChipSurface(app), panel), toUtf8(t.name) + ": the side panels lift off the page as before");
        check(closeTo(themeUiText(t), t.text) && closeTo(themeUiAccent(t), t.accent) &&
                  closeTo(themeUiPanel(t, hexColor(0x123456, 0.5f)), hexColor(0x123456, 0.5f)) &&
                  closeTo(themeUiBorder(t, hexColor(0x654321, 0.3f), 0.5f), hexColor(0x654321, 0.3f)) &&
                  themeUiDark(t) == t.isDark,
              toUtf8(t.name) + ": every chrome colour is the page's");
    }
    themesSetSystemDark(false);
    applyTheme(app, sheetIndex);
    const D2DTheme& t = app.theme;
    check(sameRgb(tabStripBackground(app), 0x1D2B38), "--ui-background is the tab strip and the caption");
    D2D1_COLOR_F panel = promptChipSurface(app);
    check(sameRgb(panel, 0x243444) && std::abs(panel.a - 0.98f) < 0.01f, "--ui-panel is the side panels");
    check(closeTo(themeUiPanel(t, hexColor(0xFFFFFF, 0.5f)), hexColor(0x243444, 0.5f)),
          "at the surface's own opacity");
    check(closeTo(themeUiBorder(t, hexColor(0xFFFFFF, 0.3f), 0.5f), hexColor(0x3B4F63, 0.5f)), "--ui-border, faded with it");
    check(themeUiDark(t) && !t.isDark, "the window is dark on a light page");
    const paint::Surface s = paint::surface(app);
    check(sameRgb(s.fill, 0x243444) && sameRgb(s.border, 0x3B4F63) && sameRgb(s.text, 0xE6EDF3) &&
              sameRgb(s.accent, 0xF59E42),
          "menus, popups and bars take --ui-panel, --ui-border, --ui-text and --ui-accent");
}

// Diagrams laid out on the page and exported, in the sheet's colours, with
// a node's own style still winning; PlantUML keeps the page's
void diagrams(App& app, int sheetIndex, const fs::path& dir) {
    themesSetSystemDark(false);
    applyTheme(app, sheetIndex);
    const fs::path page = fs::path(TINTA_THEME_CSS_FIXTURES) / "chrome.md";
    app.root = parseDocument(app.parser, read(page), page.wstring()).root;
    layoutDocument(app);
    auto shapes = [&](uint32_t fill, uint32_t stroke) {
        int n = 0;
        for (const auto& shape : app.layoutShapes) n += sameRgb(shape.fill, fill) && sameRgb(shape.stroke, stroke);
        return n;
    };
    auto runIn = [&](const wchar_t* text, uint32_t hex) {
        for (const auto& run : app.layoutTextRuns) {
            if (run.docLength && app.docText.substr(run.docStart, run.docLength) == text) {
                if (!sameRgb(run.color, hex)) {
                    std::wcerr << L"  run '" << text << L"' is " << run.color.r * 255 << L"," << run.color.g * 255 << L","
                               << run.color.b * 255 << L"\n";
                }
                return sameRgb(run.color, hex);
            }
        }
        std::wcerr << L"  no run '" << text << L"'\n";
        return false;
    };
    check(shapes(0xE3F0F8, 0x0B6E99) >= 5, "flowchart nodes and sequence actors in primaryColor and its border");
    check(shapes(0xF9E2AF, 0xC2410C) == 1, "a node's own style wins over the sheet");
    check(shapes(0xFFF8EF, 0x52677A) >= 3, "edge labels on edgeLabelBackground");
    bool lines = !app.layoutConnectors.empty();
    for (const auto& connector : app.layoutConnectors) lines = lines && sameRgb(connector.color, 0x52677A);
    check(lines, "every edge and message in lineColor");
    bool cluster = false;
    for (const auto& rect : app.layoutRects) cluster |= sameRgb(rect.color, 0xEEF3F6);
    check(cluster, "the subgraph on tertiaryColor");
    check(runIn(L"Harbour", 0xB45309), "its title in titleColor");
    check(runIn(L"Open sea", 0x10283A) && runIn(L"cast off", 0x10283A), "node and edge labels in primaryTextColor");
    check(runIn(L"Berth", 0x1F2A33), "and the table beside them in the page's text");
    check(shapes(0xFDE8CF, 0x0B6E99) >= 1, "the note on secondaryColor");
    int slices = 0;
    for (uint32_t hex : {0x0B6E99u, 0xF59E42u, 0x3F9D6Bu, 0xC2410Cu}) {
        for (const auto& shape : app.layoutShapes) {
            if (shape.type == App::LayoutShapeType::Path && sameRgb(shape.fill, hex)) {
                ++slices;
                break;
            }
        }
    }
    check(slices == 4, "the pie's slices in pie1 to pie4");
    mermaidext::Prim prim;
    check(sameRgb(resolveDiagramRole(app, prim, mermaidext::Role::Fill), 0xE3F0F8) &&
              closeTo(plantumlRoleColor(app, mermaidext::Role::Fill), app.theme.codeBackground) &&
              closeTo(plantumlRoleColor(app, mermaidext::Role::Text), app.theme.text),
          "PlantUML keeps the page's colours");

    check(exportHtmlFile(app, (dir / "harbour.html").wstring()), "the HTML export succeeds");
    const std::string html = read(dir / "harbour.html");
    for (const char* colour : {"#e3f0f8", "#0b6e99", "#fff8ef", "#b45309", "#fde8cf", "#f59e42", "#3f9d6b", "#f9e2af"}) {
        check(html.find(std::string("\"") + colour + "\"") != std::string::npos,
              std::string("the exported diagrams carry ") + colour);
    }
    const mermaidext::Built flow = buildDiagramPrims(
        app, "flowchart LR\n  A[Berth] -->|cast off| B{Tide high?}\n", 1.0f);
    bool label = false, chip = false;
    for (const auto& p : flow.prims) {
        label |= p.type == mermaidext::PrimType::Text && p.text == "Berth" &&
                 sameRgb(resolveDiagramRole(app, p, p.fill), 0x10283A);
        chip |= p.type == mermaidext::PrimType::RoundRect && sameRgb(resolveDiagramRole(app, p, p.fill), 0xFFF8EF);
    }
    check(label && chip, "the export's flowchart mirror draws as the page does");
    check(exportDocxFile(app, (dir / "harbour.docx").wstring()), "the Word export succeeds");

    // The same page in a built-in theme keeps today's colours: the page's
    // text for lines (dimmed: 0.6 for edges, 0.62 for messages), the code
    // background and the accent for nodes
    applyTheme(app, 0);
    layoutDocument(app);
    const D2DTheme& paper = app.theme;
    bool before = !app.layoutConnectors.empty();
    for (const auto& connector : app.layoutConnectors) {
        D2D1_COLOR_F line = paper.text;
        line.a = connector.color.a;
        before = before && closeTo(connector.color, line) &&
                 (std::abs(connector.color.a - 0.6f) < 0.001f || std::abs(connector.color.a - 0.62f) < 0.001f);
    }
    int nodes = 0;
    for (const auto& shape : app.layoutShapes) nodes += closeTo(shape.fill, paper.codeBackground) && closeTo(shape.stroke, paper.accent);
    check(before && nodes >= 5, "Paper's diagrams draw as before");
}
#endif

}  // namespace

int testThemeChrome(App&, const fs::path& dir) {
    failures = 0;
#ifndef TINTA_CHROME_BASELINE
    variables();
    builtins();
    problems();
#endif
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Chrome previews", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr,
                               nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        check(false, "a window to draw the chrome in");
        return failures;
    }
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);

    // Three notes in a folder of their own, the harbour log first
    const fs::path folder = dir / "harbour";
    fs::create_directories(folder);
    const fs::path fixtures = TINTA_THEME_CSS_FIXTURES;
    fs::copy_file(fixtures / "chrome.md", folder / "Harbour log.md", fs::copy_options::overwrite_existing);
    { std::ofstream(folder / "Tide table.md", std::ios::binary) << "# Tide table\n\n| Hour | Tide |\n| --- | --- |\n| 6 | high |\n"; }
    { std::ofstream(folder / "Crew list.md", std::ios::binary) << "# Crew list\n\n- Ada, master\n- Bo, mate\n"; }
    check(openDocumentInViewer(app, (folder / "Harbour log.md").wstring()), "the harbour log opens");
    tabsInit(app);
    tabOpenPath(app, app.hwnd, toUtf8((folder / "Tide table.md").wstring()), false);
    tabOpenPath(app, app.hwnd, toUtf8((folder / "Crew list.md").wstring()), false);
    tabActivate(app, app.hwnd, 0);
    check(app.tabs.size() == 3 && app.activeTab == 0, "three tabs, the log active");

    // The built-in themes' window, to compare with a build before phase 3:
    // Paper and Midnight, or all ten with TINTA_CHROME_ALL_THEMES set
    const fs::path out = dir / "chrome-frames";
    fs::create_directories(out);
    const bool all = std::getenv("TINTA_CHROME_ALL_THEMES") != nullptr;
    for (int index = 0; index < THEME_COUNT; index++) {
        if (!all && index != 0 && index != 5) continue;
        applyTheme(app, index);
        captureChrome(app, out, "builtin-" + std::to_string(index), folder);
    }

#ifndef TINTA_CHROME_BASELINE
    // The sheet as an installed theme: its colours in the functions the
    // chrome draws with, its diagrams, and its window light and dark
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    fs::copy_file(fixtures / "chrome.css", themes / "chrome.css", fs::copy_options::overwrite_existing);
    loadCustomThemes();
    int sheet = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"chrome") sheet = i;
    }
    check(sheet >= 0, "chrome.css is listed as a theme");
    if (sheet >= 0) {
        chromeColours(app, sheet);
        diagrams(app, sheet, dir);
        tabActivate(app, app.hwnd, 1);
        tabActivate(app, app.hwnd, 0);
        for (bool dark : {false, true}) {
            themesSetSystemDark(dark);
            applyTheme(app, sheet);
            check(app.theme.isDark == dark, "the page follows Windows");
            captureChrome(app, out, dark ? "chrome-dark" : "chrome-light", folder);
        }
    }
    themesSetSystemDark(false);
    fs::remove(themes / "chrome.css");
    loadCustomThemes();
#endif
    applyTheme(app, 0);
    DestroyWindow(app.hwnd);
    std::cout << "Theme chrome and diagrams: " << failures << " failures\n";
    return failures;
}
