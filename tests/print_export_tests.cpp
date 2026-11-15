#include "d2d_init.h"
#include "export.h"
#include "input.h"
#include "overlays.h"
#include "print.h"
#include "render.h"
#include "settings.h"
#include "utils.h"

#include "glyph_fonts.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

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

// The body-sized marker can start above a small heading even when their
// baselines align. A page break must keep both complete runs together.
void listMarkerPagination(App& app) {
    const auto fixture = std::filesystem::path(TINTA_PRINT_EXPORT_FIXTURE).parent_path() / "list-block-order.md";
    const std::string source = read(fixture);
    auto doc = app.parser.parse(source);
    check(!source.empty() && doc.success, "list pagination fixture opens and parses");
    if (!doc.success) return;
    app.root = doc.root;
    const float savedMargin = app.printMarginMm;
    app.printPreviewPaper = 0;
    app.printPreviewLandscape = false;
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float margin : {19.05f, 50.0f}) {
            app.printMarginMm = margin;
            openPrintPreview(app, app.hwnd);
            check(app.showPrintPreview && app.printLayout && app.printPreviewBounds.size() > 1,
                  "list fixture produces native print pages");
            for (const wchar_t* title : {L"Ordered level six heading", L"Unordered level six heading"}) {
                const std::string context = "theme " + std::to_string(theme) + ", margin " + std::to_string(margin) + ", " + utf8(title);
                const size_t offset = app.docText.find(title);
                const App::LayoutTextRun* heading = nullptr;
                for (const auto& run : app.layoutTextRuns)
                    if (offset != std::wstring::npos && run.docLength && run.docStart <= offset && offset < run.docStart + run.docLength)
                        heading = &run;
                // A number is set as text, a bullet drawn (#315)
                const D2D1_RECT_F* marker = nullptr;
                if (heading) for (const auto& run : app.layoutTextRuns)
                    if (!run.docLength && run.pos.x < heading->pos.x &&
                        run.bounds.top < heading->bounds.bottom && run.bounds.bottom > heading->bounds.top)
                        marker = &run.bounds;
                if (heading && !marker) for (const auto& shape : app.layoutShapes)
                    if (shape.rect.right <= heading->pos.x &&
                        shape.rect.top < heading->bounds.bottom && shape.rect.bottom > heading->bounds.top)
                        marker = &shape.rect;
                check(heading && marker, context + ": native heading and marker render");
                if (!heading || !marker) continue;
                int sharedPages = 0;
                for (size_t i = 1; i < app.printPreviewBounds.size(); ++i) {
                    const float top = app.printPreviewBounds[i - 1], bottom = app.printPreviewBounds[i];
                    check(!(marker->top < bottom && bottom < marker->bottom),
                          context + ": no page boundary slices the marker");
                    if (top <= marker->top && marker->bottom <= bottom &&
                        top <= heading->bounds.top && heading->bounds.bottom <= bottom) ++sharedPages;
                }
                check(sharedPages == 1, context + ": marker and first heading line belong wholly to one page");
            }
            closePrintPreview(app, app.hwnd);
        }
    }
    app.printMarginMm = savedMargin;
}

// Pixels of a BGRA image, in a box, with a channel darker than `under`
int darkIn(const std::vector<uint8_t>& pixels, int w, int h, float left, float top, float right, float bottom,
           int under) {
    int n = 0;
    for (int y = std::max(0, (int)std::ceil(top)); y < std::min(h, (int)std::floor(bottom)); ++y) {
        for (int x = std::max(0, (int)std::ceil(left)); x < std::min(w, (int)std::floor(right)); ++x) {
            const uint8_t* p = &pixels[((size_t)y * w + x) * 4];
            if (p[0] < under || p[1] < under || p[2] < under) ++n;
        }
    }
    return n;
}

// A stretch of the print layout replayed at 1:1 as a page replays it, its
// start `offsetY` down a white bitmap
std::vector<uint8_t> replay(App& app, float yStart, float yEnd, float offsetY, UINT w, UINT h) {
    std::vector<uint8_t> pixels;
    ComPtr<ID2D1Device> device;
    app.deviceContext->GetDevice(&device);
    ComPtr<ID2D1DeviceContext> dc;
    if (!device || FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc))) return pixels;
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    ComPtr<ID2D1Bitmap1> target, staging;
    dc->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target);
    dc->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0,
                     D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
                     &staging);
    if (!target || !staging) return pixels;
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f));
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush);
    if (brush) drawLayoutRange(app, dc.Get(), brush.Get(), yStart, yEnd, 0.0f, offsetY);
    D2D1_MAPPED_RECT mapped{};
    if (FAILED(dc->EndDraw()) || FAILED(staging->CopyFromBitmap(nullptr, target.Get(), nullptr)) ||
        FAILED(staging->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return pixels;
    }
    pixels.resize((size_t)w * h * 4);
    for (UINT row = 0; row < h; row++)
        memcpy(pixels.data() + (size_t)row * w * 4, mapped.bits + (size_t)row * mapped.pitch, (size_t)w * 4);
    staging->Unmap();
    return pixels;
}

// A fenced block longer than the page is left runs over the break. The cut
// falls between two rows, so the row above ends exactly where the next page
// starts: it belongs to the page before only.
void codeBlockPageBreak(App& app) {
    const auto fixture = std::filesystem::path(TINTA_PRINT_EXPORT_FIXTURE).parent_path() / "code-block-page-break.md";
    const std::string source = read(fixture);
    auto doc = app.parser.parse(source);
    check(!source.empty() && doc.success, "code block page break fixture opens and parses");
    if (!doc.success) return;
    app.root = doc.root;
    const float savedMargin = app.printMarginMm;
    app.printPreviewPaper = 0;
    app.printPreviewLandscape = false;
    const int ink = 160;  // text, not the card under it
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float marginMm : {19.05f, 25.4f}) {
            const std::string context = "theme " + std::to_string(theme) + ", margin " + std::to_string(marginMm);
            app.printMarginMm = marginMm;
            openPrintPreview(app, app.hwnd);
            const int pages = (int)app.printPreviewBounds.size() - 1;
            check(app.showPrintPreview && app.printLayout && pages >= 3, context + ": the fixture runs to three pages");
            const float pageW = app.printPreviewPageW;
            const auto& bounds = app.printPreviewBounds;

            int rowCuts = 0;
            for (int i = 1; i < pages; ++i) {
                const float cut = bounds[i];
                const std::string at = context + ", pages " + std::to_string(i) + " and " + std::to_string(i + 1);
                bool whole = true;
                for (const auto& run : app.layoutTextRuns)
                    whole = whole && !(run.bounds.top < cut - 0.01f && run.bounds.bottom > cut + 0.01f);
                check(whole, at + ": no row straddles the cut");

                const App::CodeBlockInfo* block = nullptr;
                for (const auto& candidate : app.codeBlocks)
                    if (candidate.bounds.top < cut && cut < candidate.bounds.bottom) block = &candidate;
                if (!block) continue;
                // The rows on either side of the cut, each the union of its runs
                D2D1_RECT_F above{}, below{};
                bool hasAbove = false, hasBelow = false;
                auto unite = [](D2D1_RECT_F& box, bool& has, const D2D1_RECT_F& r) {
                    box = has ? D2D1::RectF(std::min(box.left, r.left), r.top, std::max(box.right, r.right), r.bottom) : r;
                    has = true;
                };
                for (const auto& run : app.layoutTextRuns) {
                    if (!run.layout || run.bounds.right <= run.bounds.left) continue;
                    if (run.bounds.top < block->bounds.top || run.bounds.bottom > block->bounds.bottom) continue;
                    if (std::abs(run.bounds.bottom - cut) < 0.01f) unite(above, hasAbove, run.bounds);
                    if (std::abs(run.bounds.top - cut) < 0.01f) unite(below, hasBelow, run.bounds);
                }
                if (!hasAbove || !hasBelow) continue;  // a blank row beside the cut
                ++rowCuts;

                // Either side of the cut as its page replays it, the cut 80 px down
                const float rowH = above.bottom - above.top, at80 = 80.0f;
                const UINT w = (UINT)pageW, h = 160;
                const std::vector<uint8_t> before = replay(app, bounds[i - 1], cut, at80 - (cut - bounds[i - 1]), w, h);
                const std::vector<uint8_t> after = replay(app, cut, bounds[i + 1], at80, w, h);
                check(!before.empty() && !after.empty(), at + ": both sides of the cut replay");
                if (before.empty() || after.empty()) continue;
                check(darkIn(before, w, h, above.left, at80 - rowH + 1, above.right, at80 - 1, ink) > 0,
                      at + ": the row above the cut closes its page");
                check(darkIn(before, w, h, below.left, at80 + 1, below.right, at80 + rowH - 1, ink) == 0,
                      at + ": the row under the cut is not drawn on the page before");
                check(darkIn(after, w, h, below.left, at80 + 1, below.right, at80 + rowH - 1, ink) > 0,
                      at + ": the row under the cut opens the next page");
                check(darkIn(after, w, h, above.left, at80 - rowH + 1, above.right, at80 - 1, ink) == 0,
                      at + ": the row above the cut is not drawn again over the next page");
            }
            check(rowCuts >= 1, context + ": a page break falls between two rows of a code block");
            closePrintPreview(app, app.hwnd);
        }
    }
    app.printMarginMm = savedMargin;
}

// #256: Word names the faces the screen draws CJK in
void wordFonts(App& app, const std::filesystem::path& dir) {
    const std::wstring bodyHan = facesOf(app, app.textFormat, kHan);
    const std::wstring codeHan = facesOf(app, app.codeFormat, kHan);
    check(exportDocxFile(app, (dir / L"theme-fonts.docx").wstring()), "the Word export succeeds");
    std::string docx = read(dir / L"theme-fonts.docx");
    check(docx.find("<w:rPrDefault><w:rPr>" + rFonts(L"SimSun", bodyHan)) != std::string::npos,
          "Word sets body CJK in the theme's CJK face");
    // Word in Chinese reads CJK from the Normal style, not the defaults
    check(docx.find("w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:rPr>" + rFonts(L"SimSun", bodyHan)) !=
              std::string::npos,
          "the Normal style names the body faces too");
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
    check(docx.find("<w:rPrDefault><w:rPr>" + rFonts(L"Georgia", latinHan)) != std::string::npos &&
              docx.find("w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:rPr>" + rFonts(L"Georgia", latinHan)) !=
                  std::string::npos,
          "a Latin body face names the CJK face the screen falls back to");
    check(docx.find("<w:outlineLvl w:val=\"0\"/></w:pPr><w:rPr>" + rFonts(L"Cambria", headingHan)) !=
              std::string::npos,
          "headingfont= reaches Word's heading styles");

    // #314: settings.ini's cjkFont= comes before Tinta's own CJK faces, on
    // the screen and in Word; one not installed leaves them as they were
    auto preferCjk = [&](const wchar_t* face) {
        app.cjkFont = face;
        if (app.fontFallback) app.fontFallback->Release();
        app.fontFallback = nullptr;
        updateTextFormats(app);
    };
    preferCjk(L"SimSun");
    check(latinHan != L"SimSun" && facesOf(app, app.textFormat, kHan) == L"SimSun",
          "cjkFont= draws CJK beside a Latin body face");
    check(exportDocxFile(app, (dir / L"cjk-font.docx").wstring()), "the cjkFont export succeeds");
    docx = read(dir / L"cjk-font.docx");
    check(docx.find("<w:rPrDefault><w:rPr>" + rFonts(L"Georgia", L"SimSun")) != std::string::npos,
          "Word names cjkFont= for CJK");
    preferCjk(L"No Such CJK Face");
    check(facesOf(app, app.textFormat, kHan) == latinHan, "a cjkFont= not installed leaves the faces as they were");
    preferCjk(L"");
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
    listMarkerPagination(app);
    codeBlockPageBreak(app);
    if (!installed(app, L"SimSun") || !installed(app, L"Courier New") ||
        !installed(app, L"Georgia") || !installed(app, L"Cambria")) {
        std::cout << "Print and export: font checks skipped (fixture font missing), " << failures << " failures\n";
        return failures ? 1 : 0;
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
