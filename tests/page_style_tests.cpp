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
#include "utils.h"

#include <cmath>
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
    for (const auto& rect : app.layoutRects) check(rect.radius == 0 && rect.strokeWidth == 0, "and square boxes");
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
    applyTheme(app, 0);
    check(!inspectorOpen(app, x, y), "without a CSS theme there is nothing to inspect");
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
    DestroyWindow(app.hwnd);
    std::cout << "Page styles: " << failures << " failures\n";
    return failures;
}
