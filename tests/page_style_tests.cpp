// Per-element styles (docs/specs/theming.md, phase 2): a CSS theme's rules
// for one element at a time, drawn by the real layout. The reading column,
// margins, a first-line indent and centred text; a heading's border; a
// quote's bar, indent and colours; list markers; inline code chips; a code
// block's box; a table's grid, padding and stripes; a rule; and the same
// page under a built-in theme, which draws as it always has; and a sheet's
// own fonts, loaded with @font-face.
#include "d2d_init.h"
#include "document.h"
#include "editor.h"
#include "export.h"
#include "font_faces.h"
#include "frame_preview.h"
#include "input.h"
#include "inspector.h"
#include "live_text.h"
#include "overlays.h"
#include "page_style.h"
#include "print.h"
#include "render.h"
#include "settings.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

bool closeTo(float a, float b, float tolerance = 0.6f) { return std::fabs(a - b) <= tolerance; }
bool sameColor(D2D1_COLOR_F a, uint32_t rgb) {
    const D2D1_COLOR_F b = hexColor(rgb);
    return std::fabs(a.r - b.r) < 0.01f && std::fabs(a.g - b.g) < 0.01f && std::fabs(a.b - b.b) < 0.01f;
}

const char* const kSheet =
    "body { color: #202020; max-width: 600px; }\n"
    "p { margin: 0 0 20px; }\n"
    "p + p { text-indent: 32px; }\n"
    "h1 { text-align: center; }\n"
    "h2 { border-bottom: 3px solid #ff0000; padding-bottom: 5px; }\n"
    "blockquote { border-left: 6px solid #00aa00; padding-left: 30px; color: #555555; }\n"
    "blockquote strong { color: #aa00aa; }\n"
    "a { text-decoration: none; }\n"
    "ul { list-style-type: square; padding-left: 40px; }\n"
    "ol { list-style-type: upper-roman; }\n"
    "li::marker { color: #0000ff; }\n"
    "code { padding: 0 6px; border-radius: 4px; background: #eeeeee; }\n"
    "pre { padding: 20px; border-radius: 8px; border: 1px solid #cccccc; }\n"
    "th, td { padding: 2px 20px; border: 2px solid #123456; }\n"
    "tbody tr:nth-child(even) td { background-color: #fafafa; }\n"
    "hr { border-top: 5px solid #999999; }\n";

const char* const kPage =
    "# Centred title\n\n"
    "First paragraph with a [link](https://example.com) and `code`.\n\n"
    "Second paragraph, its first line indented.\n\n"
    "## A ruled heading\n\n"
    "> A quote with **strong** words.\n\n"
    "- one\n- two\n\n"
    "1. alpha\n2. beta\n\n"
    "| A | B |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\n\n"
    "```js\nlet x = 1;\n```\n\n"
    "---\n\n"
    "The end.\n";

// The text run that draws `text`, or null
const App::LayoutTextRun* runOf(const App& app, const std::wstring& text) {
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && run.docStart + run.docLength <= app.docText.size() &&
            app.docText.compare(run.docStart, run.docLength, text) == 0) {
            return &run;
        }
    }
    return nullptr;
}
const App::LayoutTextRun* runStarting(const App& app, const std::wstring& text) {
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength >= text.size() && run.docStart + text.size() <= app.docText.size() &&
            app.docText.compare(run.docStart, text.size(), text) == 0) {
            return &run;
        }
    }
    return nullptr;
}
bool hasLine(const App& app, uint32_t rgb, float stroke) {
    for (const auto& line : app.layoutLines) {
        if (sameColor(line.color, rgb) && closeTo(line.stroke, stroke, 0.05f)) return true;
    }
    return false;
}

void styled(App& app, const fs::path& dir) {
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    { std::ofstream(themes / "Boxes.css", std::ios::binary) << kSheet; }
    loadCustomThemes();
    int boxes = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"Boxes") boxes = i;
    }
    check(boxes >= 0, "the sheet is listed");
    if (boxes < 0) return;
    applyTheme(app, boxes);
    app.root = app.parser.parse(kPage).root;
    app.scrollY = app.targetScrollY = 0;
    layoutDocument(app);
    const float scale = app.contentScale * app.zoomFactor;
    const float left = app.layoutIndent;

    check(closeTo(app.layoutMaxWidth, 600 * scale), "max-width on body sets the reading column");

    const auto* title = runOf(app, L"Centred title");
    check(title && closeTo((title->bounds.left + title->bounds.right) / 2, left + app.layoutMaxWidth / 2, 1.0f),
          "text-align: center centres the heading");

    const auto* first = runStarting(app, L"First paragraph");
    const auto* second = runStarting(app, L"Second paragraph");
    check(first && closeTo(first->bounds.left, left), "the first paragraph starts at the column's edge");
    check(second && closeTo(second->bounds.left, left + 32 * scale), "p + p indents the second paragraph's first line");
    check(first && second && closeTo(second->bounds.top - first->bounds.bottom, 20 * scale, 1.0f),
          "p's margin-bottom spaces the paragraphs");

    const auto* link = runOf(app, L"link");
    bool underlined = false;
    for (const auto& line : app.layoutLines) {
        if (link && line.p1.y > link->bounds.top && line.p1.y <= link->bounds.bottom + 1 &&
            line.p1.x <= link->bounds.left + 1 && line.p2.x >= link->bounds.right - 1)
            underlined = true;
    }
    check(link && !underlined, "text-decoration: none takes a link's rule away");

    bool chip = false;
    for (const auto& rect : app.layoutRects) chip = chip || (sameColor(rect.color, 0xEEEEEE) && closeTo(rect.radius, 4 * scale));
    check(chip, "inline code gets the sheet's rounded chip");

    check(hasLine(app, 0xFF0000, 3 * scale), "the heading's border-bottom is drawn as its rule");

    bool bar = false;
    for (const auto& rect : app.layoutRects) {
        bar = bar || (sameColor(rect.color, 0x00AA00) && closeTo(rect.rect.right - rect.rect.left, 6 * scale));
    }
    check(bar, "the quote's border-left is its bar");
    const auto* quote = runStarting(app, L"A quote");
    check(quote && closeTo(quote->bounds.left, left + 36 * scale) && sameColor(quote->color, 0x555555),
          "the quote's text sits past its border and padding, in its colour");
    const auto* strong = runOf(app, L"strong");
    check(strong && sameColor(strong->color, 0xAA00AA), "blockquote strong takes its own colour");

    const auto* one = runOf(app, L"one");
    check(one && closeTo(one->bounds.left, left + 40 * scale), "padding-left on ul sets the list's indent");
    int squares = 0;
    for (const auto& shape : app.layoutShapes) {
        if (shape.type == App::LayoutShapeType::Rectangle && sameColor(shape.fill, 0x0000FF)) squares++;
    }
    check(squares == 2, "list-style-type: square, in ::marker's colour");
    // Markers carry no document text: the two blue ones are I. and II.
    std::vector<float> romans;
    for (const auto& run : app.layoutTextRuns) {
        DWRITE_TEXT_METRICS m{};
        if (!run.docLength && sameColor(run.color, 0x0000FF) && run.layout && SUCCEEDED(run.layout->GetMetrics(&m)))
            romans.push_back(m.width);
    }
    check(romans.size() == 2 && romans[1] > romans[0], "upper-roman numbers the ordered list, in ::marker's colour");

    bool card = false;
    for (const auto& rect : app.layoutRects) {
        card = card || (closeTo(rect.radius, 8 * scale) && closeTo(rect.strokeWidth, 1 * scale) && sameColor(rect.stroke, 0xCCCCCC));
    }
    check(card, "the code block gets the sheet's corners and edge");
    bool padded = false;
    for (const auto& rect : app.textRects) {
        if (rect.docStart + 3 <= app.docText.size() && app.docText.compare(rect.docStart, 3, L"let") == 0)
            padded = closeTo(rect.rect.left, left + 20 * scale);
    }
    check(padded, "the code block's padding");

    check(hasLine(app, 0x123456, 2 * scale), "the table's grid in the cells' border");
    int stripes = 0;
    for (const auto& rect : app.layoutRects) stripes += sameColor(rect.color, 0xFAFAFA) ? 1 : 0;
    check(stripes == 1, "tbody tr:nth-child(even) stripes the second body row only");
    const auto* cell = runOf(app, L"1");
    check(cell && closeTo(cell->bounds.left, left + 20 * scale), "the cells' padding");

    check(hasLine(app, 0x999999, 5 * scale), "hr's border-top is the rule");

    check(saveFramePreview(app, dir / "page-styles.png"), "saved page-styles.png");
    fs::remove(themes / "Boxes.css");
    loadCustomThemes();
}

// The same page under Paper draws as before: Tinta's bullets, rules and chips
void unstyled(App& app) {
    applyTheme(app, 0);
    app.root = app.parser.parse(kPage).root;
    layoutDocument(app);
    int discs = 0;
    for (const auto& shape : app.layoutShapes) discs += shape.type == App::LayoutShapeType::Ellipse ? 1 : 0;
    check(discs == 2, "a built-in theme keeps its round bullets");
    const auto* second = runStarting(app, L"Second paragraph");
    check(second && closeTo(second->bounds.left, app.layoutIndent), "and starts every paragraph at the edge");
    // Its boxes are Paper's own: corners of 5 for inline code and of 10 with an edge for a code block
    for (const auto& rect : app.layoutRects) {
        check((rect.radius == 0 && rect.strokeWidth == 0) || (rect.radius == 5 && rect.strokeWidth == 0) ||
                  (rect.radius == 10 && rect.strokeWidth == 1),
              "and Paper's own boxes");
    }
}
// A sheet's own faces (@font-face): a file this PC has, under a family name
// of the sheet's own, draws the body; a missing file is reported on its line
void fontFaces(App& app, const fs::path& dir) {
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    const fs::path georgia = fs::path(windows) / L"Fonts" / L"georgia.ttf";
    if (!fs::exists(georgia)) return;  // a Windows without Georgia: nothing to load
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    fs::copy_file(georgia, themes / "own-face.ttf", fs::copy_options::overwrite_existing);
    {
        std::ofstream(themes / "Faces.css", std::ios::binary)
            << "@font-face { font-family: \"Tinta Own Serif\"; src: url(\"own-face.ttf\"); }\n"
               "@font-face { font-family: \"Tinta Missing\"; src: url(\"fonts/missing.woff2\"); }\n"
               "body { font-family: \"Tinta Own Serif\", sans-serif; }\n"
               "h2 { font-family: \"Tinta Missing\", \"Tinta Own Serif\"; font-style: italic; }\n";
    }
    loadCustomThemes();
    int faces = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"Faces") faces = i;
    }
    check(faces >= 0, "the sheet with faces is listed");
    if (faces < 0) return;
    check(fontfaces::has(L"Tinta Own Serif") && fontfaces::collection(L"Tinta Own Serif") != nullptr,
          "@font-face loads the file into a collection of its own");
    check(std::wstring(fontfaces::name(L"Tinta Own Serif")) == L"Georgia", "known there by the file's own family name");
    check(!fontfaces::has(L"Tinta Missing"), "a face whose file is missing is not loaded");
    bool reported = false;
    for (const auto& line : themeProblems(faces)) reported = reported || line.find(L"missing.woff2") != std::wstring::npos;
    check(reported, "and the missing file is reported");
    applyTheme(app, faces);
    check(app.theme.fontFamily && std::wstring(app.theme.fontFamily) == L"Tinta Own Serif",
          "the body's face is the sheet's own family");
    UINT32 length = app.textFormat ? app.textFormat->GetFontFamilyNameLength() : 0;
    std::wstring family(length + 1, L'\0');
    if (app.textFormat) app.textFormat->GetFontFamilyName(family.data(), length + 1);
    family.resize(length);
    IDWriteFontCollection* collection = nullptr;
    if (app.textFormat) app.textFormat->GetFontCollection(&collection);
    check(family == L"Georgia" && collection == fontfaces::collection(L"Tinta Own Serif"),
          "the body's format draws from the sheet's collection");
    if (collection) collection->Release();
    app.root = app.parser.parse("## An italic heading\n\nBody text.\n").root;
    layoutDocument(app);
    bool italic = false;
    for (const auto& run : app.layoutTextRuns) {
        if (!run.layout || !run.docLength || app.docText.compare(run.docStart, 2, L"An") != 0) continue;
        IDWriteFontCollection* own = nullptr;
        run.layout->GetFontCollection(0, &own);
        DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
        run.layout->GetFontStyle(0, &style);
        italic = style == DWRITE_FONT_STYLE_ITALIC && own == fontfaces::collection(L"Tinta Own Serif");
        if (own) own->Release();
    }
    check(italic, "an element's face falls through a missing face to the sheet's own, in italic");
    check(exportHtmlFile(app, (dir / "faces.html").wstring()), "the HTML export succeeds");
    std::ifstream page(dir / "faces.html", std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(page)), std::istreambuf_iterator<char>());
    check(fs::exists(dir / "faces_fonts" / "own-face.ttf") &&
              html.find("src:url(\"faces_fonts/own-face.ttf\")") != std::string::npos,
          "and carries the sheet's font file beside the page, pointing at it");
    std::error_code ignored;
    fs::remove(themes / "Faces.css", ignored);
    fs::remove(themes / "own-face.ttf", ignored);  // DirectWrite may still hold it
    loadCustomThemes();
    applyTheme(app, 0);
}

// Counters and generated content: headings the sheet numbers, an item's
// own marker, an arrow after outside links; and a class the front matter
// puts on body
void generatedContent(App& app, const fs::path& dir) {
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    {
        std::ofstream(themes / "Numbers.css", std::ios::binary)
            << "body { counter-reset: h2; max-width: 500px }\n"
               "body.wide { max-width: none }\n"
               "h2 { counter-reset: h3 }\n"
               "h2::before { counter-increment: h2; content: counter(h2) \". \"; color: #cc0000 }\n"
               "h3::before { counter-increment: h3; content: counter(h2) \".\" counter(h3, lower-alpha) \" \" }\n"
               "a[href^=\"http\"]::after { content: \" \\2197\" }\n"
               "ul { list-style-type: none }\n"
               "li::before { content: \"\\2192  \" }\n";
    }
    loadCustomThemes();
    int numbers = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"Numbers") numbers = i;
    }
    check(numbers >= 0, "the numbering sheet is listed");
    if (numbers < 0) return;
    applyTheme(app, numbers);
    const std::string page =
        "## First\n\n### Sub\n\n### Sub two\n\n## Second\n\nA [link](https://example.com) and [a note](#first).\n\n"
        "- item\n";
    app.root = parseDocument(app.parser, page, (dir / "numbers.md").wstring()).root;
    layoutDocument(app);
    const auto* one = runOf(app, L"1.");
    check(one && sameColor(one->color, 0xCC0000), "h2::before numbers the first heading, in its own colour");
    check(runOf(app, L"1.b") != nullptr, "counter(h2) and counter(h3, lower-alpha) number the second subheading");
    check(runOf(app, L"2.") != nullptr, "the second h2 counts on");
    check(app.headings.size() >= 4 && app.headings[0].text == L"1. First" && app.headings[2].text == L"1.b Sub two",
          "Contents shows the numbers");
    int arrows = 0;
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && app.docText.compare(run.docStart, run.docLength, L"\x2197") == 0) arrows++;
    }
    check(arrows == 1, "a[href^=http]::after marks the outside link only");
    check(runOf(app, L"\x2192") != nullptr, "li::before gives the item a marker of its own");
    check(app.layoutMaxWidth < 501 * app.contentScale * app.zoomFactor, "body's max-width applies without the class");

    app.root = parseDocument(app.parser, "---\ncssclasses: [wide]\n---\n\n" + page, (dir / "numbers.md").wstring()).root;
    layoutDocument(app);
    check(app.layoutMaxWidth > 600 * app.contentScale * app.zoomFactor, "front matter cssclasses put .wide on body");
    std::error_code ignored;
    fs::remove(themes / "Numbers.css", ignored);
    loadCustomThemes();
    applyTheme(app, 0);
}

// Settings > Appearance > Import: Typora's Night made a CSS theme beside
// the others, listed and applied, what it left out at the top of the sheet
void importing(App& app, const fs::path& dir) {
    const fs::path fixture = fs::path(TINTA_THEME_CSS_FIXTURES) / "import" / "typora-night.css";
    const int index = importThemeFrom(app, fixture.wstring());
    check(index >= 0 && app.currentThemeIndex == index, "the imported theme is listed and applied");
    if (index < 0) return;
    const fs::path sheet = themeSourcePath(index);
    check(sheet.parent_path() == dir / "themes" && sheet.filename() == "typora-night.css",
          "it lands beside the other themes, named as the file was");
    std::ifstream in(sheet, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    check(text.rfind("/* Imported by Tinta from typora-night.css. Left out:", 0) == 0,
          "what could not come along heads the sheet");
    check(app.theme.isDark, "Night draws dark");
    check(themeProblems(index).empty(), "and Tinta reads the imported sheet without a problem");
    const int again = importThemeFrom(app, fixture.wstring());
    check(again >= 0 && fs::path(themeSourcePath(again)).filename() == "typora-night 2.css", "a second import takes the next name");
    std::error_code ignored;
    fs::remove(sheet, ignored);
    if (again >= 0) fs::remove(themeSourcePath(again), ignored);
    loadCustomThemes();
    applyTheme(app, 0);
}

// Settings > Appearance with its Import button, wide and narrow, saved to look at
void settingsPreview(App& app, const fs::path& dir) {
    applyTheme(app, 0);
    app.root = app.parser.parse("# Settings\n").root;
    layoutDocument(app);
    app.showSettings = true;
    app.settingsAnimation = 1.0f;
    app.settingsSection = 1;
    check(saveFramePreview(app, dir / "settings-appearance.png", [&] { renderSettingsOverlay(app); }),
          "saved settings-appearance.png");
    bool import = false;
    for (const auto& hit : app.settingsHits) import = import || hit.second == SET_IMPORT_THEME;
    check(import, "the Themes card has an Import button");
    const int width = app.width;
    app.width = 640;
    check(saveFramePreview(app, dir / "settings-appearance-narrow.png", [&] { renderSettingsOverlay(app); }),
          "saved settings-appearance-narrow.png");
    app.width = width;
    app.showSettings = false;
}

// The CSS inspector: Ctrl+Alt+click on a strong inside a quote shows its
// path, the rule that colours it with its file and line, what that rule
// beat, and what it inherits; the card is saved to look at
void inspecting(App& app, const fs::path& dir) {
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    {
        std::ofstream(themes / "Inspect.css", std::ios::binary)
            << "body { color: #202020; }\n"
               "blockquote { color: #555555; border-left: 6px solid #00aa00; }\n"
               "strong { color: #0000aa; }\n"
               "blockquote strong { color: #aa00aa; }\n";
    }
    loadCustomThemes();
    int inspect = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"Inspect") inspect = i;
    }
    check(inspect >= 0, "the sheet to inspect is listed");
    if (inspect < 0) return;
    applyTheme(app, inspect);
    app.root = app.parser.parse("# Inspected\n\n> A quote with **strong** words.\n").root;
    app.scrollY = app.targetScrollY = 0;
    layoutDocument(app);
    const auto* strong = runOf(app, L"strong");
    check(strong != nullptr, "the strong word is drawn");
    if (!strong) return;
    const float x = (strong->bounds.left + strong->bounds.right) / 2 + documentViewportX(app) - app.scrollX;
    const float y = (strong->bounds.top + strong->bounds.bottom) / 2 - app.scrollY;
    check(inspectorOpen(app, x, y), "Ctrl+Alt+click opens the card on the word");
    const std::wstring card = inspectorText(app);
    check(card.rfind(L"body > blockquote > p > strong\n", 0) == 0, "the card names the word's path from the page");
    check(card.find(L"color: #aa00aa   blockquote strong   Inspect.css:4") != std::wstring::npos,
          "the rule that wins, with its file and line");
    check(card.find(L"~color: #0000aa   strong   Inspect.css:3") != std::wstring::npos, "and the rule it beat, marked");
    check(card.find(L"font-weight: 700   strong") != std::wstring::npos, "Tinta's own bold, from its base sheet");
    check(saveFramePreview(app, dir / "inspector.png", [&] { renderInspector(app); }), "saved inspector.png");
    inspectorClose(app);
    check(inspectorText(app).empty(), "Esc or a click away closes it");
    std::error_code ignored;
    fs::remove(themes / "Inspect.css", ignored);
    loadCustomThemes();
    // A built-in theme is a sheet too: its rules are named by the theme,
    // and are no file to open
    applyTheme(app, 0);
    layoutDocument(app);
    check(inspectorOpen(app, x, y), "a built-in theme's rules can be looked at too");
    const std::wstring own = inspectorText(app);
    // The quote's colour is one of the theme's variables, shown with its value
    check(own.find(L"color: var(--ink-soft)   blockquote   Paper") != std::wstring::npos &&
              own.find(L"--ink-soft: #") != std::wstring::npos && own.find(L".css") == std::wstring::npos,
          "they are named by the theme, with no file");
    inspectorClose(app);
}

// A folder's own tinta.css styles the documents under it, under a built-in
// theme too; the page's chip says so, and a click on it turns the sheet off
// for that document and on again; the HTML export carries it
void folderSheets(App& app, const fs::path& dir) {
    const fs::path project = dir / "project";
    const fs::path notes = project / "notes";
    fs::create_directories(notes);
    { std::ofstream(project / "tinta.css", std::ios::binary) << "h1 { color: #ff00ff; }\nbody { background: #fafaf0; }\n"; }
    const std::string text = "# A note\n\nIts text.\n";
    { std::ofstream(notes / "note.md", std::ios::binary) << text; }
    applyTheme(app, 0);
    const std::string before = app.currentFile;
    app.currentFile = toUtf8((notes / "note.md").wstring());
    app.root = parseDocument(app.parser, text, (notes / "note.md").wstring()).root;
    app.scrollY = app.targetScrollY = 0;
    layoutDocument(app);
    check(app.folderSheetPath == (project / "tinta.css").wstring(), "the tinta.css a folder up styles the note");
    const auto* title = runOf(app, L"A note");
    check(title && sameColor(title->color, 0xFF00FF), "its h1 rule colours the heading under a built-in theme");
    check(app.pageBackground && sameColor(*app.pageBackground, 0xFAFAF0), "and its body rule the page");
    bool chip = false;
    for (const auto& link : app.linkRects) chip = chip || link.url == "tinta:folder-sheet";
    check(chip, "a chip at the top of the page says it does");
    check(saveFramePreview(app, dir / "folder-sheet.png"), "saved folder-sheet.png");
    check(exportHtmlFile(app, (dir / "folder.html").wstring()), "the HTML export succeeds");
    {
        std::ifstream page(dir / "folder.html", std::ios::binary);
        const std::string html((std::istreambuf_iterator<char>(page)), std::istreambuf_iterator<char>());
        check(html.find("#ff00ff") != std::string::npos, "and carries the folder's sheet");
    }
    app.hoveredLink = "tinta:folder-sheet";
    handleLinkClick(app);
    layoutDocument(app);
    title = runOf(app, L"A note");
    check(page::folderSheetOff(app) && title && !sameColor(title->color, 0xFF00FF) && !app.pageBackground,
          "a click on the chip turns the sheet off for the note");
    chip = false;
    for (const auto& link : app.linkRects) chip = chip || link.url == "tinta:folder-sheet";
    check(chip, "and the chip stays, to turn it on again");
    handleLinkClick(app);
    layoutDocument(app);
    title = runOf(app, L"A note");
    check(!page::folderSheetOff(app) && title && sameColor(title->color, 0xFF00FF), "a second click turns it on again");
    app.hoveredLink.clear();
    app.currentFile = before;
    std::error_code ignored;
    fs::remove_all(project, ignored);
}

// Print: a sheet's @media print rules apply on paper, and its colours with
// them; a sheet without print rules prints in Tinta's print palette, with
// its spacing kept
void printing(App& app, const fs::path& dir) {
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    {
        std::ofstream(themes / "Printed.css", std::ios::binary)
            << "p { margin: 0 0 30px; color: #00aa00; }\n@media print { h1 { color: #aa0000; } }\n";
        std::ofstream(themes / "Screen.css", std::ios::binary) << "p { margin: 0 0 30px; color: #00aa00; }\n";
    }
    loadCustomThemes();
    auto indexOf = [](const wchar_t* name) {
        for (int i = 0; i < themeCount(); i++) {
            if (std::wstring(themeAt(i).name) == name) return i;
        }
        return -1;
    };
    const std::string page = "# Title\n\nFirst paragraph.\n\nSecond paragraph.\n";
    auto onPaper = [&](int theme) {
        applyTheme(app, theme);
        const D2DTheme screen = app.theme;
        app.printLayout = true;
        app.theme = printTheme(screen);
        updateTextFormats(app);
        app.root = app.parser.parse(page).root;
        layoutDocument(app);
        const auto* title = runOf(app, L"Title");
        const auto* first = runStarting(app, L"First");
        const auto* second = runStarting(app, L"Second");
        struct Seen { bool redTitle, greenText; float gap; } seen{
            title && sameColor(title->color, 0xAA0000), first && sameColor(first->color, 0x00AA00),
            first && second ? second->bounds.top - first->bounds.bottom : -1};
        app.printLayout = false;
        app.theme = screen;
        updateTextFormats(app);
        return seen;
    };
    const int printed = indexOf(L"Printed"), screen = indexOf(L"Screen");
    check(printed >= 0 && screen >= 0, "the print sheets are listed");
    if (printed < 0 || screen < 0) return;
    const auto withRules = onPaper(printed);
    check(withRules.redTitle && withRules.greenText, "a sheet with @media print prints its colours, print rules included");
    const auto without = onPaper(screen);
    check(!without.greenText, "a sheet without print rules prints in Tinta's print palette");
    check(closeTo(without.gap, 30.0f, 1.0f), "and keeps its spacing on paper");
    std::error_code ignored;
    fs::remove(themes / "Printed.css", ignored);
    fs::remove(themes / "Screen.css", ignored);
    loadCustomThemes();
    applyTheme(app, 0);
}

// Live reads a CSS theme's code padding, list indent and quote bar, as the
// reading view draws them
void liveLook(App& app, const fs::path& dir) {
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    {
        std::ofstream(themes / "Looks.css", std::ios::binary)
            << "pre { padding: 20px; }\nul { padding-left: 40px; }\nblockquote { border-left: 3px solid #123456; }\n";
    }
    loadCustomThemes();
    int looks = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"Looks") looks = i;
    }
    check(looks >= 0, "the Live sheet is listed");
    if (looks < 0) return;
    applyTheme(app, looks);
    app.root = app.parser.parse("Text.\n").root;
    layoutDocument(app);
    const float scale = app.contentScale * app.zoomFactor;
    const live::Look look = live::pageLook(app);
    check(closeTo(look.codeInset, 20 * scale) && closeTo(look.listIndent, 40 * scale) && sameColor(look.quote, 0x123456),
          "Live's code, lists and quotes take the sheet's padding, indent and bar");
    applyTheme(app, 0);
    layoutDocument(app);
    const live::Look plain = live::pageLook(app);
    check(closeTo(plain.codeInset, 12 * scale) && closeTo(plain.listIndent, 24 * scale), "and a built-in theme keeps Live's own");
    std::error_code ignored;
    fs::remove(themes / "Looks.css", ignored);
    loadCustomThemes();
}

// Word from the theme's styles: the type sizes, the body's spacing, a
// heading's border and alignment, the code block's shading, the quote's bar
void wordStyles(App& app, const fs::path& dir) {
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    {
        std::ofstream(themes / "Word.css", std::ios::binary)
            << "body { font-size: 18px; line-height: 1.8; }\n"
               "p { margin: 0 0 20px; }\n"
               "h2 { font-size: 30px; border-bottom: 3px solid #ff0000; text-align: center; margin-top: 40px; }\n"
               "pre { background: #eef4fa; }\n"
               "blockquote { border-left: 6px solid #00aa00; }\n";
    }
    loadCustomThemes();
    int word = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"Word") word = i;
    }
    check(word >= 0, "the Word sheet is listed");
    if (word < 0) return;
    applyTheme(app, word);
    app.root = app.parser.parse("## Ruled\n\nText.\n\n> Quoted.\n\n```\ncode\n```\n").root;
    layoutDocument(app);
    check(exportDocxFile(app, (dir / "styled.docx").wstring()), "the Word export succeeds");
    std::ifstream in(dir / "styled.docx", std::ios::binary);
    const std::string docx((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto has = [&](const char* part) { return docx.find(part) != std::string::npos; };
    check(has("<w:sz w:val=\"27\"/>"), "the body's 18px is 13.5 points");
    check(has("w:after=\"300\" w:line=\"360\""), "p's margin and the body's line height space the paragraphs");
    check(has("<w:bottom w:val=\"single\" w:sz=\"18\" w:space=\"4\" w:color=\"FF0000\"/>"),
          "h2's border is a rule under Word's Heading 2");
    check(has("w:before=\"600\"") && has("<w:jc w:val=\"center\"/>") && has("<w:sz w:val=\"45\"/>"),
          "with its margin, alignment and size");
    check(has("w:fill=\"EEF4FA\""), "pre's background shades the code block");
    check(has("<w:left w:val=\"single\" w:sz=\"36\" w:space=\"12\" w:color=\"00AA00\"/>"), "and the quote has its bar");
    std::error_code ignored;
    fs::remove(themes / "Word.css", ignored);
    loadCustomThemes();
    applyTheme(app, 0);
}

// caret-color colours the editors' caret, box-shadow lays a fading shadow
// under a code block, and opacity dims an image
void caretShadowOpacity(App& app, const fs::path& dir) {
    const fs::path themes = dir / "themes";
    fs::create_directories(themes);
    {
        std::ofstream(themes / "Depth.css", std::ios::binary)
            << ":root { caret-color: #ff8800; }\n"
               "pre { box-shadow: 0 2px 8px rgb(0 0 0 / 0.4); border-radius: 6px; }\n"
               "img { opacity: 0.5; }\n";
    }
    loadCustomThemes();
    int depth = -1;
    for (int i = 0; i < themeCount(); i++) {
        if (std::wstring(themeAt(i).name) == L"Depth") depth = i;
    }
    check(depth >= 0, "the depth sheet is listed");
    if (depth < 0) return;
    applyTheme(app, depth);
    check(app.theme.caret && sameColor(*app.theme.caret, 0xFF8800), "caret-color sets the editors' caret");
    const fs::path image = fs::path(TINTA_THEME_CSS_FIXTURES).parent_path() / "scholar" / "nib.png";
    const std::string before = app.currentFile;
    app.currentFile = toUtf8((image.parent_path() / "page.md").wstring());
    app.root = app.parser.parse("```\ncode\n```\n\n![nib](nib.png)\n").root;
    layoutDocument(app);
    int layers = 0;
    for (const auto& rect : app.layoutRects) {
        if (rect.color.r == 0 && rect.color.g == 0 && rect.color.b == 0 && closeTo(rect.color.a, 0.1f, 0.01f)) layers++;
    }
    check(layers == 4, "box-shadow lays four fading rings under the code block: " + std::to_string(layers));
    bool dimmed = false;
    for (const auto& bitmap : app.layoutBitmaps) dimmed = dimmed || closeTo(bitmap.opacity, 0.5f, 0.001f);
    check(dimmed, "img { opacity } dims the picture: " + std::to_string(app.layoutBitmaps.size()) + " pictures");
    app.currentFile = before;
    std::error_code ignored;
    fs::remove(themes / "Depth.css", ignored);
    loadCustomThemes();
    applyTheme(app, 0);
    check(!app.theme.caret, "a built-in theme keeps the caret in the text's colour");
}
// What a layout draws, a line of text per run, box, line and shape: its
// place to a hundredth of a pixel and its colour
std::string hundredths(float v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", v);
    return text;
}
std::string colourOf(const D2D1_COLOR_F& c) {
    char text[48];
    std::snprintf(text, sizeof(text), "%02X%02X%02X/%.3f", (int)std::lround(c.r * 255), (int)std::lround(c.g * 255),
                  (int)std::lround(c.b * 255), c.a);
    return text;
}
std::string boxOf(const D2D1_RECT_F& r) {
    return hundredths(r.left) + "," + hundredths(r.top) + "," + hundredths(r.right) + "," + hundredths(r.bottom);
}
std::vector<std::string> drawnOf(const App& app, bool look = false) {
    std::vector<std::string> out;
    for (const auto& run : app.layoutTextRuns) {
        std::string words;
        if (run.docLength && run.docStart + run.docLength <= app.docText.size()) {
            words = toUtf8(app.docText.substr(run.docStart, std::min<size_t>(run.docLength, 24)));
        }
        out.push_back("run '" + words + "' at " + hundredths(run.pos.x) + "," + hundredths(run.pos.y) + " in " +
                      boxOf(run.bounds) + " " + colourOf(run.color));
    }
    // A box by its place and hue: its corners, its edge and how strong its
    // ground is are the theme's look, checked on their own
    // (look: with them, for two drawings that have to be the same in all)
    for (const auto& rect : app.layoutRects) {
        out.push_back("box " + boxOf(rect.rect) + " " +
                      (look ? colourOf(rect.color) + " r" + hundredths(rect.radius) + " edge " + hundredths(rect.strokeWidth)
                            : colourOf(rect.color).substr(0, 6)));
    }
    for (const auto& line : app.layoutLines) {
        out.push_back("line " + hundredths(line.p1.x) + "," + hundredths(line.p1.y) + " to " + hundredths(line.p2.x) + "," +
                      hundredths(line.p2.y) + " " + colourOf(line.color) + " w" + hundredths(line.stroke));
    }
    for (const auto& shape : app.layoutShapes) {
        out.push_back("shape " + std::to_string((int)shape.type) + " " + boxOf(shape.rect) + " " + colourOf(shape.fill) + " " +
                      colourOf(shape.stroke));
    }
    return out;
}

// The line that strikes a run: through the middle of a lowercase letter,
// 0.28 of the size above the baseline
bool struckThroughTheMiddle(const App& app, const App::LayoutTextRun& run) {
    UINT32 count = 0;
    run.layout->GetLineMetrics(nullptr, 0, &count);
    std::vector<DWRITE_LINE_METRICS> lines(count);
    if (!count || FAILED(run.layout->GetLineMetrics(lines.data(), count, &count))) return false;
    const float middle = run.pos.y + lines[0].baseline - run.layout->GetFontSize() * 0.28f;
    for (const auto& line : app.layoutLines) {
        if (closeTo(line.p1.y, line.p2.y, 0.01f) && closeTo(line.p1.y, middle, 0.75f) && line.p1.x <= run.bounds.left + 1 &&
            line.p2.x >= run.bounds.right - 1) {
            return true;
        }
    }
    return false;
}

// How well one colour reads on another (WCAG's contrast ratio, 1 to 21)
float readsAt(const D2D1_COLOR_F& ink, const D2D1_COLOR_F& ground) {
    auto light = [](const D2D1_COLOR_F& c) {
        auto part = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * part(c.r) + 0.7152f * part(c.g) + 0.0722f * part(c.b);
    };
    const float a = light(ink), b = light(ground);
    return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f);
}

// The built-in themes are CSS files (resources/themes) and hand the page
// their sheets, so the page draws them as it draws a theme of one's own.
// That the page's code draws a sheet as it drew without one is held by
// iniThemesAsCss below. Here: the ten themes' one design, each in its own
// colours. Their type; hairlines under the two top heading levels, which
// Heading separators takes away; code blocks with round corners and an
// edge; chips for inline code; callouts on a tint of their colour with the
// bar down their side; tables with lines across only and a firmer one
// under the header; links underlined in a tint of their colour; colours
// that read: text, links and code against their grounds. And what a sheet
// once took away: a callout's own colours, the dimming of a link to a
// missing file, a strike through the letters' middle.
void builtInsStyled(App& app, const fs::path& dir) {
    const fs::path fixture = fs::path(TINTA_THEME_FIXTURE).parent_path() / "builtin-themes-styled.md";
    std::ifstream in(fixture, std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    check(source.size() > 1000, "the fixture is read");
    const std::string fileBefore = app.currentFile;
    const int widthBefore = app.width;
    const bool rulesBefore = app.headingRules;
    const fs::path doc = dir / "builtin-themes-styled.md";
    app.currentFile = toUtf8(doc.wstring());
    app.root = parseDocument(app.parser, source, doc.wstring()).root;
    app.scrollY = app.targetScrollY = 0;
    const float sizes[6] = {34, 25, 20, 17, 15, 13};

    for (int theme = 0; theme < THEME_COUNT; ++theme) {
        const D2DTheme& own = THEMES[theme];
        const std::string name = toUtf8(own.name);
        check(themeSheet(theme) != nullptr, name + " hands the page its sheet");

        // The type: one text face and one for headings, the same scale in every theme
        // (Segoe UI Variable as the text face too would lay long documents out 1.7 times slower)
        check(std::wstring(own.fontFamily) == L"Segoe UI" && own.headingFontFamily &&
                  std::wstring(own.headingFontFamily) == L"Segoe UI Variable Display",
              name + ": text in Segoe UI, headings in Segoe UI Variable Display, which gives way to Segoe UI where it is missing");
        bool scale = true;
        for (int level = 0; level < 6; ++level) {
            scale = scale && own.headingSize[level] == sizes[level] && own.headingWeight[level] == (level == 0 ? 0 : 600);
        }
        check(scale, name + ": headings of 34, 25, 20, 17, 15 and 13 px, the first bold and the others semibold");
        // Colours that read
        check(readsAt(own.text, own.background) >= 7.0f, name + ": text reads at 7 to 1 or more on the page");
        check(readsAt(own.heading, own.background) >= 4.5f, name + ": headings read at 4.5 to 1 or more");
        check(readsAt(own.link, own.background) >= 4.5f, name + ": links read at 4.5 to 1 or more");
        check(readsAt(own.code, own.codeBackground) >= 4.5f, name + ": code reads at 4.5 to 1 or more on its ground");
        for (const D2D1_COLOR_F* token : {&own.syntaxKeyword, &own.syntaxString, &own.syntaxNumber, &own.syntaxFunction,
                                           &own.syntaxType, &own.syntaxControlFlow}) {
            check(readsAt(*token, own.codeBackground) >= 4.5f, name + ": every kind of code token reads at 4.5 to 1 or more");
        }
        check(readsAt(own.syntaxComment, own.codeBackground) >= 3.8f, name + ": comments, set back, still read at 3.8 to 1");
        check(own.rule && !own.boldText && !own.italicText && !own.selection, name + ": a hairline colour, and none for emphasis");
        check(own.isDark == (own.highlightBackground && own.highlightText),
              name + ": a dark theme marks words in its own gold, a light one with Tinta's yellow");

        for (int width : {1000, 600}) {
            const std::string where = name + " at " + std::to_string(width);
            app.width = width;
            applyTheme(app, theme);
            app.headingRules = true;
            layoutDocument(app);
            const float tall = app.contentHeight;
            check(!app.pageBackground, where + ": the theme's own background is no second one for the page");

            // Code: a block with corners of 10 and an edge, chips with corners of 5
            int chips = 0, blocks = 0;
            const std::string ground = colourOf(own.codeBackground);
            for (const auto& rect : app.layoutRects) {
                const bool onGround = colourOf(rect.color) == ground;
                if (rect.radius == 5 && rect.strokeWidth == 0 && onGround) chips++;
                if (rect.radius == 10 && rect.strokeWidth == 1 && onGround && rect.stroke.a == 1 && colourOf(rect.stroke) != ground) {
                    blocks++;
                }
            }
            check(chips >= 4, where + ": inline code sits in chips with corners of 5: " + std::to_string(chips));
            check(blocks == 1, where + ": the code block has corners of 10 and an edge: " + std::to_string(blocks));

            // Callouts: a tint of the callout's colour, the bar down its whole side; a plain quote's bar the theme's
            const uint32_t kinds[5] = {own.isDark ? 0x4493F8u : 0x0969DAu, own.isDark ? 0x3FB950u : 0x1A7F37u,
                                       own.isDark ? 0xAB7DF8u : 0x8250DFu, own.isDark ? 0xD29922u : 0x9A6700u,
                                       own.isDark ? 0xF85149u : 0xCF222Eu};
            int boxed = 0;
            bool title = false, quoteBar = false;
            for (const auto& run : app.layoutTextRuns) title = title || sameColor(run.color, kinds[0]);
            for (const auto& tint : app.layoutRects) {
                if (tint.color.a > 0.2f || tint.rect.right - tint.rect.left < 200) continue;
                for (uint32_t kind : kinds) {
                    if (!sameColor(tint.color, kind)) continue;
                    for (const auto& bar : app.layoutRects) {
                        if (sameColor(bar.color, kind) && bar.color.a == 1 && closeTo(bar.rect.right - bar.rect.left, 4, 0.01f) &&
                            closeTo(bar.rect.left, tint.rect.left, 0.01f) && closeTo(bar.rect.top, tint.rect.top, 0.01f) &&
                            closeTo(bar.rect.bottom, tint.rect.bottom, 0.01f)) {
                            boxed++;
                        }
                    }
                }
            }
            for (const auto& rect : app.layoutRects) {
                quoteBar = quoteBar || (colourOf(rect.color) == colourOf(own.blockquoteBorder) && closeTo(rect.rect.right - rect.rect.left, 4, 0.01f));
            }
            check(boxed == 6, where + ": each of the six callouts stands on a tint of its colour, its bar down the whole side: " +
                                  std::to_string(boxed));
            check(title, where + ": a note's title is the note's colour");
            check(quoteBar, where + ": a plain quote's bar is the theme's");
            const auto* quoted = runStarting(app, L"A plain quote");
            check(quoted && colourOf(quoted->color) != colourOf(own.text) && readsAt(quoted->color, own.background) >= 4.5f,
                  where + ": a quote's text is set back, and still reads");

            // The table: lines across only, one under every row, the header's firmer; no grounds of its own
            check(app.tableRects.size() == 1, where + ": the table is laid out");
            if (app.tableRects.size() == 1) {
                const D2D1_RECT_F table = app.tableRects[0].bounds;
                int across = 0, firm = 0, down = 0, grounds = 0;
                for (const auto& line : app.layoutLines) {
                    const bool inside = line.p1.y >= table.top - 0.5f && line.p2.y <= table.bottom + 0.5f &&
                                        line.p1.x >= table.left - 0.5f && line.p2.x <= table.right + 0.5f;
                    if (!inside) continue;
                    if (closeTo(line.p1.x, line.p2.x, 0.01f) && line.p2.y - line.p1.y > 20) down++;
                    if (closeTo(line.p1.y, line.p2.y, 0.01f) && closeTo(line.p1.x, table.left, 0.01f) &&
                        closeTo(line.p2.x, table.right, 0.01f)) {
                        (closeTo(line.stroke, 2, 0.01f) ? firm : across)++;
                        check(line.p1.y > table.top + 1, where + ": no line over the table's header");
                    }
                }
                for (const auto& rect : app.layoutRects) {
                    grounds += closeTo(rect.rect.left, table.left, 0.01f) && closeTo(rect.rect.right, table.right, 0.01f) &&
                                       rect.rect.top >= table.top - 0.5f && rect.rect.bottom <= table.bottom + 0.5f
                                   ? 1 : 0;
                }
                check(down == 0 && firm == 1 && across == 3 && grounds == 0,
                      where + ": the table has lines across only, a firmer one under its header, and no stripes: " +
                          std::to_string(down) + " down, " + std::to_string(firm) + " firm, " + std::to_string(across) +
                          " across, " + std::to_string(grounds) + " grounds");
            }

            // Links: underlined in a tint of their colour; one to a file that is not there is dimmed
            bool tinted = false;
            for (const auto& line : app.layoutLines) {
                tinted = tinted || (colourOf(line.color).substr(0, 6) == colourOf(own.link).substr(0, 6) && closeTo(line.color.a, 0.4f, 0.01f));
            }
            check(tinted, where + ": a link's underline is a tint of its colour");
            const auto* missing = runOf(app, L"missing");
            const auto* found = runOf(app, L"found");
            check(missing && found && closeTo(missing->color.a, found->color.a * 0.45f, 0.01f),
                  where + ": a link to a missing file is dimmed");
            const auto* struck = runOf(app, L"struck words");
            check(struck && struckThroughTheMiddle(app, *struck), where + ": a strike goes through the letters' middle");

            // The hairlines under the two top levels, from side to side, and the setting that takes them away
            auto hairlines = [&] {
                int count = 0;
                for (const auto& line : app.layoutLines) {
                    count += closeTo(line.p1.y, line.p2.y, 0.01f) && closeTo(line.stroke, 1, 0.01f) &&
                                     line.p2.x - line.p1.x > app.layoutMaxWidth - 1 && colourOf(line.color) == colourOf(*own.rule)
                                 ? 1 : 0;
                }
                return count;
            };
            const int with = hairlines();
            app.headingRules = false;
            layoutDocument(app);
            check(with - hairlines() == 3 && app.contentHeight < tall,
                  where + ": Heading separators off takes the hairlines under the three h1 and h2 away, and their room: " +
                      std::to_string(with) + " and " + std::to_string(hairlines()));
            app.headingRules = true;
        }
    }
    // A theme of one's own from a built-in one: its design, one variable changed
    {
        const fs::path themes = dir / "themes";
        std::error_code ignored;
        fs::create_directories(themes, ignored);
        { std::ofstream(themes / "Paper in blue.css", std::ios::binary) << "@import \"Paper\";\n:root { --link: #0b6e99; }\n"; }
        loadCustomThemes();
        int mine = -1;
        for (int i = THEME_COUNT; i < themeCount(); ++i) {
            if (std::wstring(themeAt(i).name) == L"Paper in blue") mine = i;
        }
        check(mine >= 0 && sameColor(themeAt(mine).link, 0x0B6E99) && themeAt(mine).headingSize[0] == 34 &&
                  colourOf(themeAt(mine).background) == colourOf(THEMES[0].background) && themeProblems(mine).empty(),
              "@import of a built-in theme and one variable is a theme: its design, that colour changed");
        fs::remove(themes / "Paper in blue.css", ignored);
        loadCustomThemes();
    }
    app.headingRules = rulesBefore;
    app.currentFile = fileBefore;
    app.width = widthBefore;
    applyTheme(app, 0);
    layoutDocument(app);
}

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
void writeAll(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// A themes.ini section is written as a CSS file (themes.cpp) and the page
// is drawn from that sheet. It has to draw what the section drew before:
// each of the fixture's themes lays two documents out at two widths both
// ways, and every run, box, line and shape is in the same place, colour,
// strength and shape. Then the file's life: it follows its section while
// it stays as written, becomes the theme once changed by hand, comes back
// when taken away, and leaves a sheet of the user's own alone. And Heading
// separators, the setting, still decides the lines under headings.
void iniThemesAsCss(App& app, const fs::path& dir) {
    const fs::path fixtures = fs::path(TINTA_THEME_FIXTURE).parent_path();
    const std::string iniText = readAll(fixtures / "themes-ini-as-css.ini");
    check(iniText.size() > 500, "the fixture's themes are read");
    const fs::path ini = dir / "themes.ini", themes = dir / "themes";
    std::error_code ignored;
    const bool hadIni = fs::exists(ini, ignored);
    const std::string iniBefore = hadIni ? readAll(ini) : std::string();
    const std::string fileBefore = app.currentFile;
    const int widthBefore = app.width;
    const wchar_t* const names[] = {L"Ini plain", L"Ini dark full", L"Ini no box", L"Ini: \"odd\" / name?"};
    const wchar_t* const files[] = {L"Ini plain.css", L"Ini dark full.css", L"Ini no box.css", L"Ini_ _odd_ _ name_.css"};
    for (const wchar_t* file : files) fs::remove(themes / file, ignored);
    fs::remove(themes / "Own sheet.css", ignored);
    writeAll(ini, iniText);

    struct Page { const char* file; std::string source; };
    Page pages[] = {{"themes-ini-as-css.md", {}}, {"builtin-themes-styled.md", {}}};
    for (auto& page : pages) page.source = readAll(fixtures / page.file);
    app.scrollY = app.targetScrollY = 0;

    // Without the files first, as when they cannot be written; then with
    // them, the section still the theme; then with each file changed by its
    // owner, so that the file is the theme, read as any CSS theme is: what
    // every theme will be once themes.ini is read no more
    std::vector<std::vector<std::string>> without;
    std::vector<float> tall;
    for (int pass = 0; pass < 3; ++pass) {
        themesIniBecomesCss() = pass >= 1;
        if (pass == 2) {
            for (const wchar_t* file : files) writeAll(themes / file, readAll(themes / file) + "/* kept by its owner */\n");
        }
        loadCustomThemes();
        applyTheme(app, 0);
        size_t at = 0;
        for (int theme = 0; theme < 4; ++theme) {
            const int index = THEME_COUNT + theme;
            const std::string name = toUtf8(names[theme]);
            check(index < themeCount() && std::wstring(themeAt(index).name) == names[theme],
                  name + " keeps its name and its place in the list");
            if (index >= themeCount()) continue;
            if (pass == 0) {
                check(!themeSheet(index) && !fs::exists(themes / files[theme], ignored), name + ": no file while they are not to be written");
            } else {
                check(themeSheet(index) != nullptr && fs::exists(themes / files[theme], ignored),
                      name + " has its CSS file, and the page a sheet: " + toUtf8(files[theme]));
                if (pass == 1) check(themeSourcePath(index).empty(), name + " is still its section, for the theme editor");
                else check(themeSourcePath(index) == (themes / files[theme]).wstring(), name + " is its file once that was changed");
            }
            for (auto& page : pages) {
                const fs::path doc = dir / page.file;
                app.currentFile = toUtf8(doc.wstring());
                app.root = parseDocument(app.parser, page.source, doc.wstring()).root;
                for (int width : {1000, 600}) {
                    app.width = width;
                    applyTheme(app, index);
                    layoutDocument(app);
                    if (pass == 0) {
                        without.push_back(drawnOf(app, true));
                        tall.push_back(app.contentHeight);
                        continue;
                    }
                    const std::vector<std::string> with = drawnOf(app, true);
                    const std::vector<std::string>& plain = without[at];
                    size_t differing = 0;
                    std::string first;
                    for (size_t i = 0; i < with.size() && i < plain.size(); ++i) {
                        if (with[i] == plain[i]) continue;
                        if (differing++ < 6) first += " ;; " + with[i] + "  |  without: " + plain[i];
                    }
                    check(closeTo(app.contentHeight, tall[at], 0.01f) && with.size() == plain.size() && differing == 0,
                          name + ", " + page.file + " at " + std::to_string(width) +
                              (pass == 1 ? ": drawn from its file as without it (" : ": drawn as before with the file as the theme (") +
                              std::to_string(with.size()) + " against " + std::to_string(plain.size()) + " parts, " +
                              std::to_string(differing) + " differ" + first + ")");
                    ++at;
                }
            }
        }
    }
    // Back to files as Tinta writes them
    for (const wchar_t* file : files) fs::remove(themes / file, ignored);
    loadCustomThemes();

    // The file: named for the theme, the section's colours and faces, a note on top
    const int plain = THEME_COUNT;
    const std::string written = readAll(themes / files[0]);
    check(written.rfind("/* Ini plain\n", 0) == 0 && written.find("tinta-themes-ini: ") != std::string::npos,
          "the file starts with a note that says what it is");
    check(written.find("background-color: #faf7f0") != std::string::npos && written.find("a { color: #0b6e99") != std::string::npos,
          "and holds the section's colours");
    check(written.find("font-family: \"Segoe UI\", \"Arial\", sans-serif") != std::string::npos &&
              written.find("font-family: \"Consolas\", \"Cascadia Mono\", \"Cascadia Code\", \"Courier New\", monospace") != std::string::npos,
          "and its faces, each followed by the ones Tinta falls back to");
    check(written.find("border-bottom") == std::string::npos, "but no line under headings: that is the setting's");

    // Heading separators, the setting, decides the lines under h1 and h2
    {
        const fs::path doc = dir / pages[0].file;
        app.currentFile = toUtf8(doc.wstring());
        app.root = parseDocument(app.parser, pages[0].source, doc.wstring()).root;
        app.width = 1000;
        const bool rules = app.headingRules;
        for (int index : {0, 5, plain, plain + 1}) {
            applyTheme(app, index);
            app.headingRules = true;
            layoutDocument(app);
            const size_t on = app.layoutLines.size();
            const float tallOn = app.contentHeight;
            app.headingRules = false;
            layoutDocument(app);
            check(on == app.layoutLines.size() + 3 && app.contentHeight < tallOn,
                  toUtf8(themeAt(index).name) + ": Heading separators off takes the three lines under h1 and h2 away: " +
                      std::to_string(on) + " and " + std::to_string(app.layoutLines.size()));
        }
        app.headingRules = rules;
    }

    // The section changed by hand: the file follows
    {
        std::string changed = iniText;
        const size_t at = changed.find("link=0B6E99");
        check(at != std::string::npos, "the fixture's link colour is found");
        if (at != std::string::npos) changed.replace(at, 11, "link=AA5500");
        writeAll(ini, changed);
        loadCustomThemes();
        const D2D1_COLOR_F link = themeAt(plain).link;
        check(sameColor(link, 0xAA5500) && readAll(themes / files[0]).find("a { color: #aa5500") != std::string::npos &&
                  themeSourcePath(plain).empty(),
              "a section changed by hand writes its file again");
    }
    // The file changed by hand: it is the theme, under the section's name and in its place
    {
        const std::string own = readAll(themes / files[0]) + "h1 { color: #123456; }\npre { border-radius: 12px; }\n";
        writeAll(themes / files[0], own);
        check(themeReloadIfChanged(plain), "a file changed by hand is noticed");
        check(themeSourcePath(plain) == (themes / files[0]).wstring() && std::wstring(themeAt(plain).name) == names[0],
              "and is the theme from then on, under the section's name and in its place");
        check(themeAt(plain).headingColors[0] && sameColor(*themeAt(plain).headingColors[0], 0x123456) &&
                  sameColor(themeAt(plain).link, 0xAA5500),
              "with what was added to it and what it held");
        writeAll(ini, iniText);  // the section goes back to its first link colour
        loadCustomThemes();
        check(readAll(themes / files[0]) == own && sameColor(themeAt(plain).link, 0xAA5500),
              "its section is read no more: the file stays as its owner left it");
        check(themeCount() == THEME_COUNT + 4, "and it is listed once");
    }
    // Taken away, it is written again from the section
    {
        fs::remove(themes / files[0], ignored);
        loadCustomThemes();
        check(readAll(themes / files[0]).find("a { color: #0b6e99") != std::string::npos && themeSourcePath(plain).empty() &&
                  sameColor(themeAt(plain).link, 0x0B6E99),
              "a file taken away is written again, and the section is the theme");
    }
    // A sheet of the user's own under a section's name is left alone; both are listed
    {
        const std::string mine = "body { background: #010203; }\n";
        writeAll(themes / "Own sheet.css", mine);
        writeAll(ini, iniText + "\n[theme]\nname=Own sheet\nbackground=F0F0F0\n");
        loadCustomThemes();
        const int section = THEME_COUNT + 4;
        check(themeCount() == THEME_COUNT + 6 && std::wstring(themeAt(section).name) == L"Own sheet" && !themeSheet(section) &&
                  sameColor(themeAt(section).background, 0xF0F0F0),
              "a section whose name a sheet of one's own has is drawn as before");
        check(readAll(themes / "Own sheet.css") == mine && sameColor(themeAt(section + 1).background, 0x010203),
              "and that sheet is untouched and listed too");
        fs::remove(themes / "Own sheet.css", ignored);
    }
    // The theme editor's save: the section and its file, the new theme after the sections
    {
        writeAll(ini, iniText);
        loadCustomThemes();
        D2DTheme made = THEMES[2];
        made.link = hexColor(0x00AA77);
        const int index = saveCustomTheme(made, L"Made in Settings", L"Segoe UI", L"Consolas");
        check(index == THEME_COUNT + 4 && std::wstring(themeAt(index).name) == L"Made in Settings" && themeSheet(index) &&
                  readAll(themes / "Made in Settings.css").find("a { color: #00aa77") != std::string::npos,
              "a theme saved from Settings gets its section and its file");
        made.link = hexColor(0x7700AA);
        check(saveCustomTheme(made, L"Made in Settings", L"Segoe UI", L"Consolas") == index &&
                  readAll(themes / "Made in Settings.css").find("a { color: #7700aa") != std::string::npos,
              "and saved again, the file follows");
        fs::remove(themes / "Made in Settings.css", ignored);
    }

    for (const wchar_t* file : files) fs::remove(themes / file, ignored);
    if (hadIni) writeAll(ini, iniBefore);
    else fs::remove(ini, ignored);
    loadCustomThemes();
    app.currentFile = fileBefore;
    app.width = widthBefore;
    applyTheme(app, 0);
}
// The themes in use, for light and for dark, are kept by name beside their
// places in the list: a theme added or taken away moves the others, and a
// place then means another theme. A settings.ini without names, from a
// build before them, is read by place as it always was.
void chosenByName(App& app, const fs::path& dir) {
    const fs::path ini = dir / "themes.ini", themes = dir / "themes", settingsFile = dir / "settings.ini";
    std::error_code ignored;
    const bool hadIni = fs::exists(ini, ignored);
    const std::string iniBefore = hadIni ? readAll(ini) : std::string();
    const std::string settingsBefore = readAll(settingsFile);
    const int current = app.currentThemeIndex, light = app.lightThemeIndex, dark = app.darkThemeIndex;
    fs::create_directories(themes, ignored);
    const std::string sections = "[theme]\nname=Chosen one\nbackground=FAFAFA\n\n[theme]\nname=Chosen two\ndark=1\nbackground=101010\n";
    writeAll(ini, sections);
    writeAll(themes / "Aaa first.css", "body { background: #eeeeee; }\n");
    writeAll(themes / "Zed last.css", "body { background: #111111; color: #dddddd; }\n");
    loadCustomThemes();
    auto placeOf = [](const wchar_t* name) {
        for (int i = 0; i < themeCount(); ++i) {
            if (std::wstring(themeAt(i).name) == name) return i;
        }
        return -1;
    };
    const int zed = placeOf(L"Zed last"), two = placeOf(L"Chosen two"), one = placeOf(L"Chosen one");
    check(one == THEME_COUNT && two == THEME_COUNT + 1 && zed == THEME_COUNT + 3, "the sections stand first, the sheets after them by name");
    app.currentThemeIndex = zed;
    app.lightThemeIndex = one;
    app.darkThemeIndex = zed;
    persistThemeChoice(app);
    Settings kept = loadSettings();
    check(kept.themeIndex == zed && kept.themeName == "Zed last" && kept.lightThemeName == "Chosen one" && kept.darkThemeName == "Zed last",
          "the chosen themes are kept by place and by name");

    // A sheet that sorts before it and a section more: every place after them moves
    writeAll(themes / "Middle.css", "body { background: #abcdef; }\n");
    writeAll(ini, "[theme]\nname=Chosen zero\nbackground=F0F0F0\n\n" + sections);
    loadCustomThemes();
    check(placeOf(L"Zed last") == zed + 2 && placeOf(L"Chosen one") == one + 1, "a theme added moves the ones after it");
    int nowCurrent = -1, nowLight = -1, nowDark = -1;
    settingsGiveThemes(kept, nowCurrent, nowLight, nowDark);
    check(nowCurrent == zed + 2 && nowLight == one + 1 && nowDark == zed + 2, "and the chosen ones are found by name where they stand now");

    // One that is gone gives way to Paper, or to Midnight for dark
    fs::remove(themes / "Zed last.css", ignored);
    loadCustomThemes();
    settingsGiveThemes(kept, nowCurrent, nowLight, nowDark);
    check(nowCurrent == 0 && nowLight == one + 1 && nowDark == 5, "a chosen theme that is gone gives way to Paper, or Midnight for dark");

    // No names, as an older build wrote it: by place
    Settings old = kept;
    old.themeName.clear();
    old.lightThemeName.clear();
    old.darkThemeName.clear();
    old.themeIndex = 3;
    old.lightThemeIndex = 1;
    old.darkThemeIndex = 7;
    settingsGiveThemes(old, nowCurrent, nowLight, nowDark);
    check(nowCurrent == 3 && nowLight == 1 && nowDark == 7, "a settings.ini without names is read by place");
    // A built-in theme is found by its name too, should the built-ins ever change places
    old.themeName = "Dracula";
    old.themeIndex = 2;
    settingsGiveThemes(old, nowCurrent, nowLight, nowDark);
    check(nowCurrent == 6, "a built-in theme is found by its name");

    for (const char* file : {"Aaa first.css", "Zed last.css", "Middle.css", "Chosen zero.css", "Chosen one.css", "Chosen two.css"}) {
        fs::remove(themes / file, ignored);
    }
    if (hadIni) writeAll(ini, iniBefore);
    else fs::remove(ini, ignored);
    writeAll(settingsFile, settingsBefore);
    loadCustomThemes();
    app.currentThemeIndex = current;
    app.lightThemeIndex = light;
    app.darkThemeIndex = dark;
    applyTheme(app, 0);
}
}  // namespace

int testPageStyles(App&, const fs::path& dir) {
    failures = 0;
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Page styles", WS_POPUP, 0, 0, 1000, 1400, nullptr, nullptr, nullptr,
                               nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        check(false, "a window to draw the page in");
        return failures;
    }
    app.width = 1000;
    app.height = 1400;
    createTypography(app);
    styled(app, dir);
    unstyled(app);
    fontFaces(app, dir);
    generatedContent(app, dir);
    importing(app, dir);
    settingsPreview(app, dir);
    inspecting(app, dir);
    folderSheets(app, dir);
    printing(app, dir);
    liveLook(app, dir);
    wordStyles(app, dir);
    caretShadowOpacity(app, dir);
    builtInsStyled(app, dir);
    iniThemesAsCss(app, dir);
    chosenByName(app, dir);
    DestroyWindow(app.hwnd);
    std::cout << "Page styles: " << failures << " failures\n";
    return failures;
}
