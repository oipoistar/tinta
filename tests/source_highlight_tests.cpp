// Markdown in the source editor, in colour (#268, source_style.h): the
// fixture's lines as the editor lays them out to draw them, read back for
// their colours, weights and styles; the same after a theme change, with
// the setting off and on through the settings panel, and with word wrap; a
// long document's frames timed with and without highlighting; frames in two
// themes at two widths to look at.
#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "frame_preview.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "overlays.h"
#include "render.h"
#include "settings.h"
#include "source_style.h"
#include "syntax.h"
#include "tabs.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {
namespace fs = std::filesystem;

int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        if (failures < 80) std::cerr << "FAIL source highlight: " << message << '\n';
        ++failures;
    }
}

// What the window draws in the source editor: the pane, the strip, the rail
void drawSource(App& app) {
    app.renderTarget->Clear(app.theme.background);
    renderEditor(app, editorPaneWidth(app));
    renderTabStrip(app);
    app.editRailAnim = 1.0f;
    app.editRailHover = 0;
    renderEditRail(app);
}

void paint(App& app) {
    app.renderTarget->BeginDraw();
    drawSource(app);
    app.renderTarget->EndDraw();
}

void frame(App& app, const fs::path& path) {
    check(saveFramePreview(app, path, [&] { drawSource(app); }), "frame saved: " + path.filename().string());
}

float lineHeight(const App& app) { return app.editorTextFormat->GetFontSize() * 1.5f; }

// The line holding some text, scrolled into view and drawn
size_t show(App& app, const std::wstring& text) {
    size_t pos = app.editorText.find(text);
    check(pos != std::wstring::npos, "the fixture holds: " + toUtf8(text));
    if (pos == std::wstring::npos) return 0;
    auto it = std::upper_bound(app.editorLineStarts.begin(), app.editorLineStarts.end(), pos);
    size_t line = (size_t)(it - app.editorLineStarts.begin()) - 1;
    float wanted = (float)line * lineHeight(app) - 200.0f;
    app.editorScrollY = std::max(0.0f, wanted);
    paint(app);
    return line;
}

// The layout the editor drew a line with
IDWriteTextLayout* drawn(App& app, size_t line) {
    auto it = app.editorLineLayoutCache.find(app.editorLineStarts[line]);
    return it == app.editorLineLayoutCache.end() ? nullptr : it->second.layout;
}

struct Spot {
    IDWriteTextLayout* layout = nullptr;
    UINT32 column = 0;
};

// A character of the fixture as drawn: `within` characters into `text`
Spot spot(App& app, const std::wstring& text, size_t within = 0) {
    size_t line = show(app, text);
    Spot s;
    s.layout = drawn(app, line);
    s.column = (UINT32)(app.editorText.find(text) + within - app.editorLineStarts[line]);
    check(s.layout != nullptr, "the editor drew the line of: " + toUtf8(text));
    return s;
}

// The colour it is drawn in: its brush's, or the text's with none
D2D1_COLOR_F colourOf(App& app, const Spot& s) {
    if (!s.layout) return D2D1::ColorF(-1, -1, -1, -1);
    IUnknown* effect = nullptr;
    DWRITE_TEXT_RANGE range{};
    s.layout->GetDrawingEffect(s.column, &effect, &range);
    if (!effect) return app.theme.text;
    ID2D1SolidColorBrush* brush = nullptr;
    D2D1_COLOR_F colour = D2D1::ColorF(-1, -1, -1, -1);
    if (SUCCEEDED(effect->QueryInterface(__uuidof(ID2D1SolidColorBrush), reinterpret_cast<void**>(&brush))) && brush) {
        colour = brush->GetColor();
        brush->Release();
    }
    effect->Release();
    return colour;
}

bool same(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    return std::fabs(a.r - b.r) < 0.01f && std::fabs(a.g - b.g) < 0.01f && std::fabs(a.b - b.b) < 0.01f &&
           std::fabs(a.a - b.a) < 0.01f;
}

std::string describe(D2D1_COLOR_F c) {
    char text[80];
    snprintf(text, sizeof text, "(%.2f %.2f %.2f %.2f)", c.r, c.g, c.b, c.a);
    return text;
}

D2D1_COLOR_F muted(const App& app) {
    D2D1_COLOR_F c = app.theme.text;
    c.a = 0.45f;
    return c;
}

void colour(App& app, const std::wstring& text, size_t within, D2D1_COLOR_F want, const std::string& what) {
    D2D1_COLOR_F got = colourOf(app, spot(app, text, within));
    check(same(got, want), what + ": " + describe(got) + " for " + describe(want));
}

bool bold(App& app, const std::wstring& text, size_t within = 0) {
    Spot s = spot(app, text, within);
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    if (s.layout) s.layout->GetFontWeight(s.column, &weight);
    return weight >= DWRITE_FONT_WEIGHT_BOLD;
}

bool italic(App& app, const std::wstring& text, size_t within = 0) {
    Spot s = spot(app, text, within);
    DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    if (s.layout) s.layout->GetFontStyle(s.column, &style);
    return style == DWRITE_FONT_STYLE_ITALIC;
}

// The colour the code lexer gives a token, as the preview draws it
D2D1_COLOR_F tokenColour(App& app, const std::wstring& line, int language, size_t column, SyntaxState state = {}) {
    std::wstring text = line;
    for (const SyntaxToken& token : tokenizeLine(text, language, state)) {
        size_t at = (size_t)(token.text.data() - text.data());
        if (column >= at && column < at + token.text.size()) return getTokenColor(app.theme, token.tokenType);
    }
    return app.theme.text;
}

// Whether bold and italic keep the editor font's widths here
bool keepsWidth(App& app) {
    IDWriteTextLayout* l = nullptr;
    app.dwriteFactory->CreateTextLayout(L"Wim0", 4, app.editorTextFormat, 1000.0f, 100.0f, &l);
    if (!l) return false;
    DWRITE_TEXT_METRICS plain{}, heavy{};
    l->GetMetrics(&plain);
    l->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, {0, 4});
    l->GetMetrics(&heavy);
    l->Release();
    return std::fabs(plain.width - heavy.width) < 0.01f;
}

void colours(App& app, const fs::path& out, const std::string& name) {
    const D2DTheme& th = app.theme;
    const bool weights = keepsWidth(app);
    // The properties: their fences faint, their keys as YAML's
    colour(app, L"---\ntitle", 0, muted(app), "the properties' opening fence is faint");
    colour(app, L"title: Highlighting", 0, tokenColour(app, L"title: Highlighting tour", LanguageYaml, 0),
           "a property's key takes YAML's key colour");
    // A heading: its marks faint, its words in the heading colour, bold
    colour(app, L"# Highlighting tour", 0, muted(app), "a heading's # is faint");
    colour(app, L"# Highlighting tour", 2, themeHeadingColor(th, 1), "a heading's words take the first level's colour");
    if (weights) check(bold(app, L"# Highlighting tour", 2), "a heading is bold");
    frame(app, out / (name + "-top.png"));
    // Inline: bold, italic, code, strikethrough, links, notes, pictures, footnotes, escapes, addresses
    colour(app, L"**bold**", 0, muted(app), "a bold's asterisks are faint");
    if (weights) check(bold(app, L"**bold**", 2) && !bold(app, L"A paragraph"), "bold words are bold, plain ones not");
    if (weights) check(italic(app, L"*italic*", 1), "italic words are italic");
    colour(app, L"`inline code`", 0, muted(app), "a code span's backticks are faint");
    colour(app, L"`inline code`", 1, themeInlineCodeColor(th), "a code span takes the inline code colour");
    {
        Spot s = spot(app, L"~~struck~~", 2);
        BOOL struck = FALSE;
        if (s.layout) s.layout->GetStrikethrough(s.column, &struck);
        check(struck == TRUE, "struck words are struck through");
    }
    colour(app, L"[link](https://example.com/tour", 1, th.link, "a link's words take the link colour");
    colour(app, L"[link](https://example.com/tour", 8, muted(app), "a link's address is faint");
    colour(app, L"[[Plans]]", 2, th.link, "a note's name takes the link colour");
    colour(app, L"![Board mock]", 2, th.link, "a picture's words take the link colour");
    if (weights) check(italic(app, L"![Board mock]", 2), "a picture's words are italic");
    colour(app, L"footnote[^1]", 8, th.link, "a footnote's mark takes the link colour");
    colour(app, L"\\*star\\*", 0, muted(app), "an escape's backslash is faint");
    colour(app, L"\\*star\\*", 2, th.text, "escaped stars make nothing italic");
    colour(app, L"https://example.com/bare", 3, th.link, "an address written out takes the link colour");
    colour(app, L"A paragraph", 0, th.text, "plain words keep the text colour");
    // A setext heading and its underline
    colour(app, L"Setext heading", 0, themeHeadingColor(th, 1), "a setext heading's words take the heading colour");
    colour(app, L"==============", 0, muted(app), "its underline is faint");
    colour(app, L"A second setext heading", 0, themeHeadingColor(th, 2),
           "a setext heading over two lines takes the heading colour from its first line");
    colour(app, L"over two lines", 0, themeHeadingColor(th, 2), "and on its second");
    colour(app, L"--------------", 0, muted(app), "a dashed underline is faint");
    // Lists, tasks, quotes and callouts: the marks that start them in the accent
    colour(app, L"- First item", 0, th.accent, "a bullet takes the accent");
    colour(app, L"- First item", 2, th.text, "an item's words keep the text colour");
    colour(app, L"  - Nested item", 2, th.accent, "a nested bullet takes the accent");
    colour(app, L"1. One", 0, th.accent, "a number takes the accent");
    colour(app, L"- [ ] An open task", 2, th.accent, "a task's box takes the accent");
    colour(app, L"- [x] A finished task", 6, muted(app), "a finished task's words go faint");
    colour(app, L"> A quote", 0, th.accent, "a quote's > takes the accent");
    colour(app, L"> ## A heading in a quote", 5, themeHeadingColor(th, 2), "a heading in a quote takes the heading colour");
    colour(app, L"> [!NOTE]", 2, th.accent, "a callout's kind takes the accent");
    // A table: its pipes and delimiter row faint, its header bold, its cells read inline
    colour(app, L"| Name |", 0, muted(app), "a table's pipes are faint");
    colour(app, L"| --- | :---: |", 2, muted(app), "the delimiter row is faint");
    if (weights) check(bold(app, L"| Name |", 2) && !bold(app, L"| Paper |", 2), "the header row is bold, the rows not");
    colour(app, L"`light`", 1, themeInlineCodeColor(th), "code in a cell takes the inline code colour");
    colour(app, L"a \\| pipe", 3, th.text, "an escaped pipe is no cell's edge");
    frame(app, out / (name + "-middle.png"));
    // Fences: their marks faint, their language in the accent, their code in its language's colours
    colour(app, L"```cpp", 0, muted(app), "a fence's marks are faint");
    colour(app, L"```cpp", 3, th.accent, "a fence's language takes the accent");
    colour(app, L"int main() {", 0, tokenColour(app, L"int main() {", detectLanguage(L"cpp"), 0), "C++ code takes C++'s colours");
    colour(app, L"    // a comment", 4, th.syntaxComment, "a C++ comment takes the comment colour");
    colour(app, L"    return 42;", 11, th.syntaxNumber, "a number takes the number colour");
    // A comment over lines is still the comment on its second line: the
    // lexer's state goes on down the fence
    colour(app, L"       over lines */", 7, th.syntaxComment, "a comment over lines keeps its colour on its second line");
    check(!same(tokenColour(app, L"       over lines */", detectLanguage(L"cpp"), 7), th.syntaxComment),
          "alone, that line would not read as a comment");
    colour(app, L"# Not a heading", 2, th.code, "a # line in code is code, not a heading");
    colour(app, L"- not a list", 0, th.code, "a - line in code is code, not a list");
    colour(app, L"plain code in the code colour", 0, th.code, "code in a language Tinta does not know takes the code colour");
    colour(app, L"const inner = true;", 0,
           tokenColour(app, L"const inner = true;", detectLanguage(L"markdown"), 0, [] {
               SyntaxState s;
               s.mode = SyntaxState::Mode::MarkdownFence;
               s.delimiter = L"```";
               return s;
           }()),
           "a fence inside a longer fence stays the longer fence's code");
    // Maths and markup
    colour(app, L"$e^{i", 0, muted(app), "inline maths' $ is faint");
    colour(app, L"$e^{i", 1, themeInlineCodeColor(th), "inline maths takes the inline code colour");
    colour(app, L"$$\n\\int", 0, muted(app), "a maths block's $$ is faint");
    colour(app, L"\\int_0^1", 0, themeInlineCodeColor(th), "a maths block's lines take the inline code colour");
    colour(app, L"<kbd>", 1, muted(app), "an HTML tag is faint");
    colour(app, L"<https://example.com/angle>", 3, th.link, "an address in <> takes the link colour");
    colour(app, L"<!-- comment -->", 5, th.syntaxComment, "an inline comment takes the comment colour");
    colour(app, L"A comment\nover", 0, th.syntaxComment, "a comment over lines takes the comment colour on each");
    colour(app, L"over three lines", 0, th.syntaxComment, "and on its middle lines");
    colour(app, L"***", 0, muted(app), "a divider is faint");
    colour(app, L"[^1]: The footnote", 0, th.link, "a footnote's definition takes the link colour");
    if (weights) check(italic(app, L"*emphasis*.", 1), "a footnote's words read inline");
    colour(app, L"[ref]: https", 1, th.link, "a link's definition takes the link colour");
    colour(app, L"[ref]: https", 8, muted(app), "and its address is faint");
    colour(app, L"The last line.", 0, th.text, "the last line keeps the text colour");
    frame(app, out / (name + "-bottom.png"));

    // A drawn line is as wide as a plain one: the caret and clicks measure plain lines
    {
        Spot s = spot(app, L"# Highlighting tour");
        IDWriteTextLayout* plain = nullptr;
        size_t line = show(app, L"# Highlighting tour");
        size_t length = app.editorText.find(L'\n', app.editorLineStarts[line]) - app.editorLineStarts[line];
        app.dwriteFactory->CreateTextLayout(app.editorText.data() + app.editorLineStarts[line], (UINT32)length, app.editorTextFormat,
                                            1e7f, 1e7f, &plain);
        DWRITE_TEXT_METRICS a{}, b{};
        if (s.layout) s.layout->GetMetrics(&a);
        if (plain) {
            plain->GetMetrics(&b);
            plain->Release();
        }
        check(std::fabs(a.widthIncludingTrailingWhitespace - b.widthIncludingTrailingWhitespace) < 0.01f,
              "a coloured heading is as wide as a plain one");
    }
}

// The setting, through the settings panel's own row
void setting(App& app, const fs::path& out) {
    app.showSettings = true;
    app.settingsSection = 2;
    app.settingsAnimation = 1.0f;
    app.renderTarget->BeginDraw();
    renderSettingsOverlay(app);
    app.renderTarget->EndDraw();
    D2D1_RECT_F toggle{};
    for (const auto& hit : app.settingsHits) {
        if (hit.second == SET_TOGGLE_HIGHLIGHT) toggle = hit.first;
    }
    check(toggle.right > toggle.left, "the editor's settings have the highlighting switch");
    check(saveFramePreview(app, out / "highlight-settings.png", [&] { drawSource(app); renderSettingsOverlay(app); }),
          "the editor's settings are drawn");
    LPARAM point = MAKELPARAM((int)((toggle.left + toggle.right) * 0.5f), (int)((toggle.top + toggle.bottom) * 0.5f));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, point);
    handleMouseUp(app, app.hwnd, 0, point);
    check(!app.editorHighlight, "the switch turns highlighting off");
    check(!loadSettings().editorHighlight, "and the setting is saved");
    app.showSettings = false;
    colour(app, L"# Highlighting tour", 2, app.theme.text, "with it off a heading keeps the text colour");
    colour(app, L"- First item", 0, app.theme.text, "and a bullet too");
    app.editorHighlight = true;
    app.clearEditorLineLayoutCache();
    persistEditorMode(app);
    check(loadSettings().editorHighlight, "on again, and saved");
    colour(app, L"# Highlighting tour", 2, themeHeadingColor(app.theme, 1), "with it on again the heading takes its colour");
}

// A long document: frames at its top and foot, and a key typed at its foot,
// with and without highlighting
void performance(App& app, const fs::path& dir) {
    std::string big;
    for (int i = 0; i < 2000; ++i) {
        big += "## Section " + std::to_string(i) + "\n\nA paragraph with **bold**, *emphasis*, `code` and a [link](https://example.com/" +
               std::to_string(i) + ") that runs on for a while.\n\n- one\n- [ ] two\n\n";
        if (i % 10 == 0) big += "```cpp\nint x = " + std::to_string(i) + "; // a comment\n```\n\n";
    }
    const fs::path path = dir / "Big.md";
    std::ofstream(path, std::ios::binary) << big;
    app.editorDirty = false;
    exitEditMode(app);
    check(openDocumentInViewer(app, path.wstring()), "the long document opens");
    enterEditMode(app);
    check(app.editMode && !editorLiveOn(app), "it opens in the source");
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    auto measure = [&](bool on, double& top, double& foot, double& key) {
        app.editorHighlight = on;
        app.clearEditorLineLayoutCache();
        app.editorScrollY = 0;
        auto t0 = clock::now();
        paint(app);
        top = ms(clock::now() - t0);
        app.editorScrollY = (float)app.editorLineStarts.size() * lineHeight(app);
        app.editorCursorPos = app.editorSelStart = app.editorSelEnd = app.editorText.size();
        app.editorHasSelection = false;
        t0 = clock::now();
        paint(app);
        foot = ms(clock::now() - t0);
        t0 = clock::now();
        typeKeys(app, L"x");
        paint(app);
        key = ms(clock::now() - t0);
        keystroke(app, VK_BACK);
    };
    double offTop = 0, offFoot = 0, offKey = 0, onTop = 0, onFoot = 0, onKey = 0;
    measure(false, offTop, offFoot, offKey);
    measure(true, onTop, onFoot, onKey);
    std::cout << "Source highlighting, " << big.size() / 1024 << " KB, " << app.editorLineStarts.size() << " lines: top " << offTop
              << " / " << onTop << " ms, foot " << offFoot << " / " << onFoot << " ms, a key at the foot " << offKey << " / " << onKey
              << " ms (off / on)\n";
    check(onKey < 40.0, "a key and its frame at a long document's foot take under 40 ms: " + std::to_string(onKey));
    check(onFoot < 60.0, "a frame at a long document's foot takes under 60 ms: " + std::to_string(onFoot));
    // The lines drawn at the foot are coloured: the walk reached them
    show(app, L"## Section 1999");
    colour(app, L"## Section 1999", 3, themeHeadingColor(app.theme, 2), "the last heading of a long document takes its colour");
    app.editorDirty = false;
}

}  // namespace

int runSourceHighlightTests(const char* fixture) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Source highlight tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    const fs::path dir = fs::current_path() / "highlight";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path doc = dir / fs::path(fixture).filename();
    fs::copy_file(fixture, doc, fs::copy_options::overwrite_existing, ec);
    check(!ec, "the fixture copies");
    const fs::path out = fs::current_path() / "frames";
    fs::create_directories(out, ec);

    tabsInit(app);
    applyTheme(app, 0);
    app.editorLive = false;
    check(openDocumentInViewer(app, doc.wstring()), "the fixture opens");
    enterEditMode(app);
    check(app.editMode && !editorLiveOn(app), "the source editor");
    app.editorShowPreview = false;
    app.editorHighlight = true;
    paint(app);
    colours(app, out, "highlight-theme0");
    // Another theme: every line takes the new colours
    applyTheme(app, 5);
    colour(app, L"# Highlighting tour", 2, themeHeadingColor(app.theme, 1), "a new theme gives the heading its own colour");
    colours(app, out, "highlight-theme5");
    // Word wrap: the wrapped editor colours its lines the same
    app.editorWordWrap = true;
    app.editorRowMetricsWidth = -1.0f;
    app.clearEditorLineLayoutCache();
    colour(app, L"# Highlighting tour", 2, themeHeadingColor(app.theme, 1), "wrapped, a heading keeps its colour");
    colour(app, L"    // a comment", 4, app.theme.syntaxComment, "wrapped, a comment keeps its colour");
    setting(app, out);
    // Two themes at two widths, wrapped at the narrow one
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            app.editorWordWrap = width < 1000;
            app.editorRowMetricsWidth = -1.0f;
            app.clearEditorLineLayoutCache();
            app.editorScrollY = 0;
            paint(app);
            frame(app, out / ("highlight-theme" + std::to_string(theme) + "-" + std::to_string(width) + ".png"));
            show(app, L"## Code");
            frame(app, out / ("highlight-code-theme" + std::to_string(theme) + "-" + std::to_string(width) + ".png"));
        }
    }
    app.width = 1050;
    app.editorWordWrap = false;
    applyTheme(app, 0);
    performance(app, dir);

    app.editorDirty = false;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Source highlighting: " << failures << " failures\n";
    return failures ? 1 : 0;
}
