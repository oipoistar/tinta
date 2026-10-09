#include "d2d_init.h"
#include "export.h"
#include "input.h"
#include "overlays.h"
#include "print.h"
#include "render.h"
#include "settings.h"
#include "utils.h"

#include "glyph_fonts.h"

#include <cmath>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

using Microsoft::WRL::ComPtr;

// Print, PDF and Word export follow the screen (#256, #257, #258): printed
// pages keep the theme's faces and run between the page margins whatever the
// reading width; Word names the faces the screen draws CJK in.
namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

std::string utf8(const std::wstring& text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0,
                                   nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), size,
                        nullptr, nullptr);
    return out;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The faces a format draws text in through the document fallback
std::wstring facesOf(App& app, IDWriteTextFormat* format, const std::wstring& text) {
    if (!format) return L"(no format)";
    ComPtr<IDWriteFontCollection> fonts;
    app.dwriteFactory->GetSystemFontCollection(&fonts);
    ComPtr<IDWriteTextLayout> layout;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), format, 2000.0f,
                                        200.0f, &layout);
    if (!layout) return L"(no layout)";
    ComPtr<IDWriteTextLayout2> layout2;
    if (app.fontFallback && SUCCEEDED(layout.As(&layout2))) layout2->SetFontFallback(app.fontFallback);
    glyphfonts::RunFonts runs(fonts.Get());
    layout->Draw(nullptr, &runs, 0, 0);
    return runs.families;
}

std::wstring formatFamily(IDWriteTextFormat* format) {
    wchar_t name[128] = {};
    if (format) format->GetFontFamilyName(name, 128);
    return name;
}

// The w:rFonts a Word export writes for a face
std::string rFonts(const std::wstring& face, const std::wstring& eastAsia) {
    return "<w:rFonts w:ascii=\"" + utf8(face) + "\" w:hAnsi=\"" + utf8(face) +
           "\" w:eastAsia=\"" + utf8(eastAsia) + "\" w:cs=\"" + utf8(face) + "\"/>";
}

bool installed(App& app, const wchar_t* family) {
    ComPtr<IDWriteFontCollection> fonts;
    app.dwriteFactory->GetSystemFontCollection(&fonts);
    UINT32 index = 0;
    BOOL exists = FALSE;
    return fonts && SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists;
}

const std::wstring kHan = L"\u4E2D\u6587";

// #257, #258: the preview holds the document in print layout
void printLayout(App& app) {
    const float screenIndent = app.layoutIndent, screenWidth = app.layoutMaxWidth;
    const wchar_t* screenFont = app.theme.fontFamily;
    const std::wstring bodyHan = facesOf(app, app.textFormat, kHan);
    const std::wstring codeHan = facesOf(app, app.codeFormat, kHan);
    check(bodyHan == L"SimSun", "the screen draws the theme's CJK body face");

    app.printPreviewPaper = 0;  // A4, without asking the default printer
    app.printPreviewLandscape = false;
    openPrintPreview(app, app.hwnd);
    check(app.showPrintPreview && app.printLayout, "the preview holds the document in print layout");
    check(std::wstring(app.theme.fontFamily) == L"SimSun" &&
          std::wstring(app.theme.codeFontFamily) == L"Courier New" &&
          formatFamily(app.textFormat) == L"SimSun" &&
          formatFamily(app.codeFormat) == L"Courier New",
          "print keeps the theme's body and code faces");
    check(facesOf(app, app.textFormat, kHan) == bodyHan &&
          facesOf(app, app.codeFormat, kHan) == codeHan &&
          facesOf(app, app.codeFormat, L"code") == L"Courier New",
          "printed CJK and code land in the faces the screen uses");
    check(app.theme.background.r == 1.0f && app.theme.background.g == 1.0f &&
          app.theme.background.b == 1.0f, "print keeps its white paper palette");

    const float contentW = app.printPreviewPageW - 2 * 72.0f;
    check(app.layoutIndent == 0.0f && std::abs(app.layoutMaxWidth - contentW) < 0.5f,
          "printed text spans the page margins despite a 70% reading width");
    check(!app.headings.empty() && app.headings[0].y < 40.0f,
          "the first page starts at its top margin, not below a title strip");
    int shrunk = 0;
    check(app.codeBlocks.size() == 3, "the fixture prints its three code blocks");
    for (const auto& block : app.codeBlocks) {
        // The card is the background rect at the block's top-left; long
        // lines widen it past the column
        const auto& b = block.bounds;
        float right = b.right;
        for (const auto& r : app.layoutRects) {
            if (std::abs(r.rect.top - b.top) < 0.5f && std::abs(r.rect.left - b.left) < 0.5f)
                right = std::max(right, r.rect.right);
        }
        check(std::abs(b.left) < 0.5f, "every code block starts at the left margin");
        if (right <= contentW + 0.5f) {
            check(std::abs(right - contentW) < 0.5f, "a code block ends at the right margin");
            continue;
        }
        ++shrunk;
        bool aligned = false;
        for (const auto& band : app.printShrinkBands) {
            if (b.top >= band.top - 0.5f && b.bottom <= band.bottom + 0.5f)
                aligned = std::abs(right * band.scale - contentW) < 1.0f;
        }
        check(aligned, "a code block wider than the page shrinks to the same margins");
    }
    check(shrunk == 1, "the wide code block is the one that shrinks");

    closePrintPreview(app, app.hwnd);
    check(!app.printLayout && app.theme.fontFamily == screenFont &&
          app.layoutIndent == screenIndent && app.layoutMaxWidth == screenWidth,
          "closing the preview restores the screen's theme and column");

    float height = 300.0f;
    ID2D1Bitmap* peek = renderPeekBitmap(app, toWide(app.currentFile), 400.0f, height);
    check(peek != nullptr && !app.printLayout, "the link peek still renders as the screen does");
    if (peek) peek->Release();
}

// #257: the preview's margin presets re-lay the pages out and are saved
void printMargins(App& app) {
    app.printPreviewPaper = 0;
    app.printPreviewLandscape = false;
    openPrintPreview(app, app.hwnd);
    const float pageW = app.printPreviewPageW;
    for (int i : {0, 2, 1}) {
        app.renderTarget->BeginDraw();
        renderPrintPreview(app);
        check(SUCCEEDED(app.renderTarget->EndDraw()), "the print preview renders");
        const D2D1_RECT_F chip = app.printPreviewMarginBtn[i];
        check(chip.right > chip.left, "the preview shows the margin presets");
        const LPARAM point = MAKELPARAM((int)((chip.left + chip.right) / 2),
                                        (int)((chip.top + chip.bottom) / 2));
        handleMouseDown(app, app.hwnd, MK_LBUTTON, point);
        handleMouseUp(app, app.hwnd, 0, point);
        const float margin = PRINT_MARGINS_MM[i] * 96.0f / 25.4f;
        check(app.showPrintPreview && std::abs(app.printMarginMm - PRINT_MARGINS_MM[i]) < 0.01f &&
              std::abs(app.layoutMaxWidth - (pageW - 2 * margin)) < 0.5f,
              "a margin preset re-lays the preview out between its margins");
        check(std::abs(loadSettings().printMarginMm - PRINT_MARGINS_MM[i]) < 0.01f,
              "the chosen margin is saved");
    }
    closePrintPreview(app, app.hwnd);

    // settings.ini takes any margin from 5 to 50 mm and ignores the rest
    const std::filesystem::path ini = std::filesystem::current_path() / L"settings.ini";
    const std::string saved = read(ini);
    for (const auto& [value, expected] : {std::pair<const char*, float>{"15", 15.0f},
                                         {"3", 19.05f}, {"60", 19.05f}, {"abc", 19.05f}}) {
        std::ofstream(ini, std::ios::binary) << "[Settings]\nprintMarginMm=" << value << "\n";
        check(std::abs(loadSettings().printMarginMm - expected) < 0.01f,
              std::string("settings.ini printMarginMm=") + value + " loads as expected");
    }
    std::ofstream(ini, std::ios::binary) << saved;
}

// #256: Word names the faces the screen draws CJK in
void wordFonts(App& app, const std::filesystem::path& dir) {
    const std::wstring bodyHan = facesOf(app, app.textFormat, kHan);
    const std::wstring codeHan = facesOf(app, app.codeFormat, kHan);
    check(exportDocxFile(app, (dir / L"theme-fonts.docx").wstring()), "the Word export succeeds");
    std::string docx = read(dir / L"theme-fonts.docx");
    check(docx.find("<w:rPrDefault><w:rPr>" + rFonts(L"SimSun", bodyHan)) != std::string::npos,
          "Word sets body CJK in the theme's CJK face");
    const std::string code = rFonts(L"Courier New", codeHan);
    const size_t first = docx.find(code);
    check(first != std::string::npos && docx.find(code, first + 1) != std::string::npos,
          "Word sets code blocks and inline code CJK in the face the screen falls back to");

    const D2DTheme saved = app.theme;
    app.theme.fontFamily = L"Georgia";
    app.theme.headingFontFamily = L"Cambria";
    updateTextFormats(app);
    const std::wstring latinHan = facesOf(app, app.textFormat, kHan);
    const std::wstring headingHan = facesOf(app, app.headingFormats[0], kHan);
    check(latinHan != L"Georgia" && !latinHan.empty(), "a Latin body face falls back for CJK");
    check(exportDocxFile(app, (dir / L"latin-fonts.docx").wstring()), "the Latin-face export succeeds");
    docx = read(dir / L"latin-fonts.docx");
    check(docx.find("<w:rPrDefault><w:rPr>" + rFonts(L"Georgia", latinHan)) != std::string::npos,
          "a Latin body face names the CJK face the screen falls back to");
    check(docx.find("<w:outlineLvl w:val=\"0\"/></w:pPr><w:rPr>" + rFonts(L"Cambria", headingHan)) !=
              std::string::npos,
          "headingfont= reaches Word's heading styles");
    app.theme = saved;
    updateTextFormats(app);
}
}  // namespace

int runPrintExportTests() {
    namespace fs = std::filesystem;
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Print and export tests", WS_POPUP, 0, 0, 1050,
                               900, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    if (!installed(app, L"SimSun") || !installed(app, L"Courier New") ||
        !installed(app, L"Georgia") || !installed(app, L"Cambria")) {
        std::cout << "Print and export: skipped, a fixture font is not installed\n";
        return 0;
    }

    // The reporter's setup: a custom theme with a CJK body face and a
    // Latin-only code face, read from themes.ini like any user theme
    const fs::path dir = fs::current_path();
    fs::copy_file(TINTA_PRINT_EXPORT_THEMES, dir / L"themes.ini",
                  fs::copy_options::overwrite_existing);
    loadCustomThemes();
    int theme = -1;
    for (int i = 0; i < themeCount(); ++i) {
        if (std::wstring(themeAt(i).name) == L"Arctic SimSun") theme = i;
    }
    check(theme >= 0, "the fixture theme loads from themes.ini");
    if (theme < 0) return 1;
    applyTheme(app, theme);
    app.readingWidthPct = 70;
    check(openDocumentInViewer(app, fs::path(TINTA_PRINT_EXPORT_FIXTURE).wstring()),
          "the mixed fixture opens");
    ensureLayoutComplete(app);
    check(app.codeBlocks.size() == 3 && !app.headings.empty(), "the fixture lays out on screen");

    printLayout(app);
    printMargins(app);
    wordFonts(app, dir);

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Print and export: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
