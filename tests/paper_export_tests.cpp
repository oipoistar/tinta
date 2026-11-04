// Paper export (docs/specs/scholar.md, phase 3): a paper's front matter read
// as pandoc reads it; the title block across the top of page 1; two columns
// cut where a page would be; page numbers and running heads in the margins;
// the preview's toggles; Word's title block styles and SEQ fields; and a
// document that is no paper printing as it always did. Pages and preview
// frames are saved beside the test for inspection.
#include "d2d_init.h"
#include "export.h"
#include "input.h"
#include "overlays.h"
#include "paper.h"
#include "print.h"
#include "render.h"
#include "utils.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
namespace fs = std::filesystem;
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool within(float a, float b, float tolerance = 0.5f) { return std::fabs(a - b) <= tolerance; }

std::string ascii(const wchar_t* text) {
    std::string out;
    for (; *text; ++text) out += (char)(*text < 128 ? *text : '?');
    return out;
}

// --- The front matter ----------------------------------------------------------

void frontMatter() {
    paper::Front f = paper::read(
        "title: \"Ink: a study\"   # quoted, with a colon\n"
        "author:\n"
        "  - Ana Okafor\n"
        "  - name: Lars Lindqvist\n"
        "    affiliation: Uppsala\n"
        "abstract: |\n"
        "  First line\n"
        "    kept indented\n"
        "\n"
        "  Second paragraph.\n"
        "keywords: [ink, 'paper, sized', \"nib\"]\n"
        "date: 2026-11-04\n");
    check(f.title == "Ink: a study", "a quoted title keeps its colon");
    check(f.authors == std::vector<std::string>{"Ana Okafor", "Lars Lindqvist"},
          "a block list of authors, a mapping giving its name");
    check(f.abstract == "First line\n  kept indented\n\nSecond paragraph.", "a literal block keeps its lines");
    check(f.keywords == std::vector<std::string>{"ink", "paper, sized", "nib"}, "a flow list, quoted items kept whole");
    check(f.date == "2026-11-04" && f.titleBlock(), "a title with authors makes a title block");

    f = paper::read("title: T\nabstract: >-\n  One\n  two.\n\n  Three.\n\nsubtitle: S\n");
    check(f.abstract == "One two.\nThree." && f.subtitle == "S",
          "a folded block joins its lines and keeps a break for an empty line");
    check(f.titleBlock() && f.authors.empty(), "a title with an abstract makes a title block");

    f = paper::read("title: 'It''s ink' # a comment\nauthor: Ana Okafor\nkeywords:\n  - ink\n  - nibs\n"
                    "institute: Lagos\nthanks: \"With \\\"thanks\\\"\"\nshorttitle: Ink\nlang: de-DE\n");
    check(f.title == "It's ink" && f.authors == std::vector<std::string>{"Ana Okafor"},
          "single quotes, a comment and one author as a string");
    check(f.keywords == std::vector<std::string>{"ink", "nibs"} && f.institutes == std::vector<std::string>{"Lagos"},
          "keywords as a block list, an institute as a string");
    check(f.thanks == "With \"thanks\"" && f.shortTitle == "Ink" && f.lang == "de-DE", "escapes and the short title");

    f = paper::read("title: A long\n  title over two lines\nauthor: [O'Brien, {name: Lars Lindqvist, email: l@x.se}]\n");
    check(f.title == "A long title over two lines", "a plain scalar folds over lines");
    check(f.authors == std::vector<std::string>{"O'Brien", "Lars Lindqvist"},
          "an apostrophe inside a flow item, and a flow mapping's name");

    f = paper::read("classoption: [twocolumn, a4paper]\npapersize: A4\nfontsize: 11pt\ngeometry: margin=2cm\n");
    check(f.twoColumn && f.paperSize == "a4" && f.fontSizePt == 11.0f && within(f.marginMm, 20.0f, 0.01f),
          "classoption, papersize, fontsize and geometry as pandoc names them");
    check(!f.titleBlock(), "no title, no title block");
    f = paper::read("classoption: twocolumn,10pt\npapersize: letterpaper\ngeometry: \"top=3cm, margin=1in\"\n"
                    "fontsize: 30pt\n");
    check(f.twoColumn && f.paperSize == "letter" && within(f.marginMm, 25.4f, 0.01f) && f.fontSizePt == 0,
          "options in one string, letterpaper, inches; a type size out of range is left alone");
    f = paper::read("classoption:\n  - onecolumn\npapersize: b5\n");
    check(!f.twoColumn && f.paperSize.empty() && paper::paperIndex(f) == -1, "one column; a paper not offered");
    check(paper::paperIndex(paper::read("papersize: letter\n")) == 1, "Letter is the preview's second paper");
    check(!paper::read("title: Notes\ntags: [a, b]\n").titleBlock(), "a title alone keeps the strip");

    check(paper::joinAuthors({"A"}, "") == "A" && paper::joinAuthors({"A", "B"}, "") == "A and B" &&
              paper::joinAuthors({"A", "B", "C"}, "en-GB") == "A, B and C",
          "authors joined with commas and and");
    check(paper::joinAuthors({"A", "B"}, "de") == "A und B", "and in the document's language");
    check(paper::headAuthors({"Ana Okafor", "Lars Lindqvist"}, "") == "Okafor et al." &&
              paper::headAuthors({"Okafor, Ana"}, "") == "Okafor" && paper::headAuthors({"Ana *Okafor*"}, "") == "Okafor",
          "the running head names the first author's surname");
    check(paper::plainText("On *ink* and `nibs`") == "On ink and nibs", "a running head reads as plain text");

    // In Chinese: authors set apart by the enumeration comma, et al. as
    // "deng", keywords after a full-width colon and between semicolons
    const std::string zhang = "\xE5\xBC\xA0\xE4\xB8\x89", li = "\xE6\x9D\x8E\xE5\x9B\x9B";
    check(paper::joinAuthors({zhang, li}, "zh-CN") == zhang + "\xE3\x80\x81" + li &&
              paper::headAuthors({zhang, li}, "zh-CN") == zhang + "\xE7\xAD\x89",
          "Chinese names joined and headed as Chinese writes them");
    const paper::Words& zh = paper::wordsFor("zh-CN");
    check(std::string(zh.abstract) == "\xE6\x91\x98\xE8\xA6\x81" && std::string(zh.colon) == "\xEF\xBC\x9A" &&
              std::string(zh.between) == "\xEF\xBC\x9B",
          "Chinese words for the abstract and the keywords");
    check(std::string(paper::wordsFor("fr").colon) == "\xC2\xA0: " && std::string(paper::wordsFor("").abstract) == "Abstract",
          "French sets a no-break space before the colon; English is the default");
}

// --- Print -----------------------------------------------------------------------

// The text run laid out for the line that starts with `start`
const App::LayoutTextRun* runStarting(const App& app, const std::wstring& start) {
    for (const auto& run : app.layoutTextRuns) {
        if (run.docLength && app.docText.compare(run.docStart, start.size(), start) == 0) return &run;
    }
    return nullptr;
}

// Pixels of the preview's page that are not paper, in a box of page DIPs
int inkIn(const App& app, float left, float top, float right, float bottom) {
    const float fit = app.printPreviewFit;
    const int w = (int)app.printPreviewPxW, h = (int)app.printPreviewPxH;
    int ink = 0;
    for (int y = std::max(0, (int)(top * fit)); y < std::min(h, (int)(bottom * fit)); ++y) {
        for (int x = std::max(0, (int)(left * fit)); x < std::min(w, (int)(right * fit)); ++x) {
            const uint8_t* p = &app.printPreviewPixels[((size_t)y * w + x) * 4];
            if (p[0] < 200 || p[1] < 200 || p[2] < 200) ++ink;
        }
    }
    return ink;
}

// The preview as the window draws it, saved as a PNG
bool savePreview(App& app, const fs::path& path) {
    ComPtr<ID2D1DeviceContext> dc;
    if (FAILED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc)))) return false;
    const UINT width = (UINT)app.printSaved.width, height = (UINT)app.printSaved.height;
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    ComPtr<ID2D1Bitmap1> target, readable;
    if (FAILED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target)) ||
        FAILED(dc->CreateBitmap(
            D2D1::SizeU(width, height), nullptr, 0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
            &readable))) {
        return false;
    }
    ComPtr<ID2D1Image> previous;
    dc->GetTarget(&previous);
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    renderPrintPreview(app);
    const bool drawn = SUCCEEDED(dc->EndDraw());
    dc->SetTarget(previous.Get());
    D2D1_MAPPED_RECT mapped{};
    if (!drawn || FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr)) ||
        FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return false;
    }
    ComPtr<IWICBitmap> image;
    const bool copied = SUCCEEDED(app.wicFactory->CreateBitmapFromMemory(
        width, height, GUID_WICPixelFormat32bppPBGRA, mapped.pitch, mapped.pitch * height, mapped.bits, &image));
    readable->Unmap();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    return copied && SUCCEEDED(app.wicFactory->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           SUCCEEDED(app.wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
           SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
           SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
           SUCCEEDED(frame->WriteSource(image.Get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
           SUCCEEDED(encoder->Commit());
}

// A preview page drawn as print drew every page before paper export: the
// page's stretch of the layout at the margins, nothing else
std::vector<uint8_t> pageAsBefore(App& app, int page) {
    std::vector<uint8_t> pixels;
    const UINT w = app.printPreviewPxW, h = app.printPreviewPxH;
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
    const float margin = app.printPreviewMarginMm * 96.0f / 25.4f;
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Scale(app.printPreviewFit, app.printPreviewFit));
    dc->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f));
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush);
    drawLayoutRange(app, dc.Get(), brush.Get(), app.printPreviewBounds[page], app.printPreviewBounds[page + 1], margin,
                    margin);
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    D2D1_MAPPED_RECT mapped{};
    if (FAILED(dc->EndDraw()) || FAILED(staging->CopyFromBitmap(nullptr, target.Get(), nullptr)) ||
        FAILED(staging->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return pixels;
    }
    pixels.resize((size_t)w * h * 4);
    for (UINT row = 0; row < h; row++) memcpy(pixels.data() + (size_t)row * w * 4, mapped.bits + (size_t)row * mapped.pitch, (size_t)w * 4);
    staging->Unmap();
    return pixels;
}

// A click on a preview chip, through the window's handlers
void click(App& app, const D2D1_RECT_F& chip) {
    app.renderTarget->BeginDraw();
    renderPrintPreview(app);
    app.renderTarget->EndDraw();
    const LPARAM point = MAKELPARAM((int)((chip.left + chip.right) / 2), (int)((chip.top + chip.bottom) / 2));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, point);
    handleMouseUp(app, app.hwnd, 0, point);
}

int framesOn(const App& app, int page) {
    int n = 0;
    for (const auto& frame : app.printPreviewFrames) n += frame.page == page;
    return n;
}

void twoColumnPaper(App& app, const fs::path& fixtures, const fs::path& out) {
    check(openDocumentInViewer(app, (fixtures / "paper-print.md").wstring()), "the paper opens");
    ensureLayoutComplete(app);
    check(app.paperPrint.titleBottom == 0.0f && !runStarting(app, L"Ink on the move"),
          "on screen the front matter keeps its strip");
    app.printPreviewPaper = 1;  // Letter: the paper's papersize: a4 must win
    app.printPreviewLandscape = false;
    openPrintPreview(app, app.hwnd);
    check(app.showPrintPreview && app.printPreviewPaper == 0, "papersize: a4 picks A4 in the preview");
    check(within(app.zoomFactor, 11.0f / 12.0f, 0.001f), "fontsize: 11pt sets the body at 11 points");
    check(app.printOptionOn[PRINT_TWO_COLUMNS] && app.printOptionOn[PRINT_PAGE_NUMBERS] &&
              app.printOptionOn[PRINT_RUNNING_HEADS],
          "classoption: twocolumn and a title block turn every option on");
    check(app.printHeadLeft == L"Ink on the move" && app.printHeadRight == L"Okafor et al.",
          "the running heads: the short title and the first author et al.");

    const float margin = app.printPreviewMarginMm * 96.0f / 25.4f;
    const float contentW = app.printPreviewPageW - 2 * margin, contentH = app.printPreviewPageH - 2 * margin;
    const float columnW = std::floor((contentW - 24.0f) / 2.0f);
    const float title = app.paperPrint.titleBottom;
    check(within(app.layoutMaxWidth, columnW), "the text is laid out at a column's width");
    check(title > 100.0f && title < contentH * 0.6f, "the title block takes the top of the first page");

    // The title block: centred across both columns, ruled off underneath
    const App::LayoutTextRun* heading = runStarting(app, L"Ink on the move: capillary flow in split nibs");
    check(heading && heading->bounds.top < 1.0f && heading->bounds.bottom < title,
          "the title is the first line of page 1");
    check(heading && within(heading->bounds.left, 0.0f) && within(heading->bounds.right, contentW, 1.0f),
          "the title spans the page, not a column");
    for (const wchar_t* line : {L"A study in two columns", L"Ana Okafor and Lars Lindqvist",
                                L"Department of Writing Instruments", L"4 November 2026", L"Abstract",
                                L"Fountain pens move ink", L"Keywords: capillary flow, fountain pens, paper sizing",
                                L"* We thank the Tinta reading group"}) {
        const App::LayoutTextRun* run = runStarting(app, line);
        check(run && run->bounds.bottom <= title && run->bounds.right > columnW + 24.0f,
              "the title block sets " + ascii(line) + " across the page");
    }
    bool ruled = false;
    for (const auto& rule : app.layoutLines)
        ruled = ruled || (rule.p1.y < title && within(rule.p1.x, 0.0f) && within(rule.p2.x, contentW, 1.0f));
    check(ruled, "a hairline across the page closes the title block");
    const App::LayoutTextRun* abstract = runStarting(app, L"Fountain pens move ink");
    check(abstract && abstract->bounds.left > 10.0f && abstract->bounds.right < contentW - 10.0f,
          "the abstract is set in from both sides");

    // Two columns, the title block above them on page 1
    const int pages = (int)app.printPreviewBounds.size() - 1;
    check(pages >= 2, "the paper runs to more than a page");
    check(framesOn(app, 0) == 3 && framesOn(app, 1) == 2, "page 1: the title block and two columns; then two");
    const float rightX = margin + contentW - columnW;
    for (const auto& frame : app.printPreviewFrames) {
        const bool titleFrame = frame.page == 0 && frame.yEnd <= title + 0.5f;
        if (titleFrame) {
            check(within(frame.x, margin) && within(frame.y, margin) && frame.clip.right <= frame.clip.left,
                  "the title block sits at the top margin, unclipped");
            continue;
        }
        const bool left = within(frame.x, margin);
        check(left || within(frame.x, rightX), "a column starts at the left margin or right of the gutter");
        check(within(frame.y, frame.page == 0 ? margin + title : margin), "columns start under the title block on page 1");
        check(frame.yEnd - frame.yStart <= contentH - (frame.page == 0 ? title : 0.0f) + 1.5f,
              "no column runs past the bottom margin");
        check(left ? frame.clip.right <= margin + columnW + 12.5f : frame.clip.left >= margin + columnW + 11.5f,
              "each column is clipped at the gutter's middle");
        // No line, picture or shape is cut by a column's end
        if (frame.yEnd > app.contentHeight) continue;
        for (const auto& run : app.layoutTextRuns)
            check(!(run.bounds.top < frame.yEnd - 0.5f && run.bounds.bottom > frame.yEnd + 0.5f), "no line straddles a column cut");
        for (const auto& bmp : app.layoutBitmaps)
            check(!(bmp.destRect.top < frame.yEnd - 0.5f && bmp.destRect.bottom > frame.yEnd + 0.5f),
                  "no picture straddles a column cut");
    }
    // Figures, tables, code and equations fit their column
    for (const auto& bmp : app.layoutBitmaps) check(bmp.destRect.right <= columnW + 0.5f, "the figure fits its column");
    for (const auto& run : app.layoutTextRuns) {
        if (run.bounds.top < title || run.bounds.right <= columnW + 0.5f) continue;
        bool shrunk = false;
        for (const auto& band : app.printShrinkBands)
            shrunk = shrunk || (run.bounds.top >= band.top - 0.5f && run.bounds.bottom <= band.bottom + 0.5f &&
                                run.bounds.right * band.scale <= columnW + 1.0f);
        check(shrunk, "text wider than the column prints shrunk into it");
    }
    check(!app.printShrinkBands.empty(), "the code block's long lines shrink into the column");
    for (const auto& band : app.printShrinkBands) check(band.top >= title, "the title block never shrinks");

    // On the pages: the right column holds text, the gutter none; the
    // running head stays off page 1, the number under every page
    const float pageW = app.printPreviewPageW, pageH = app.printPreviewPageH;
    printPreviewSetPage(app, 0);
    check(inkIn(app, 0, 0, pageW, margin - 1) == 0, "page 1 has no running head");
    check(inkIn(app, 0, pageH - margin + 1, pageW, pageH) > 0, "page 1 is numbered");
    check(inkIn(app, rightX + 4, margin + title + 8, pageW - margin, pageH - margin) > 200,
          "page 1's right column carries text");
    check(savePreview(app, out / "preview-page-1.png"), "the preview of page 1 is saved");
    printPreviewSetPage(app, 1);
    check(inkIn(app, margin, 0, pageW / 2, margin - 1) > 0 && inkIn(app, pageW / 2, 0, pageW - margin, margin - 1) > 0,
          "page 2 carries the running heads left and right");
    check(inkIn(app, pageW / 2 - 40, pageH - margin + 1, pageW / 2 + 40, pageH) > 0, "page 2 is numbered, centred");
    check(inkIn(app, margin + columnW + 4, margin, rightX - 4, pageH - margin) == 0, "nothing prints in the gutter");
    check(inkIn(app, rightX + 4, margin, pageW - margin, pageH - margin) > 500, "page 2's right column carries text");
    check(savePreview(app, out / "preview-page-2.png"), "the preview of page 2 is saved");

    // The toggles, clicked: heads off, numbers off, one column, then back
    const float ui = app.printSaved.contentScale;
    for (int i = 0; i < PRINT_OPTION_COUNT; ++i)
        check(app.printPreviewOptionBtn[i].right > app.printPreviewOptionBtn[i].left &&
                  app.printPreviewOptionBtn[i].top >= 52.0f * ui &&
                  app.printPreviewOptionBtn[i].bottom <= (52.0f + PRINT_OPTION_ROW_H) * ui,
              "the paper options sit in their row under the header");
    check(app.printPreviewOptionBtn[0].right < app.printPreviewOptionBtn[1].left &&
              app.printPreviewOptionBtn[1].right < app.printPreviewOptionBtn[2].left,
          "the option chips do not overlap");
    click(app, app.printPreviewOptionBtn[PRINT_RUNNING_HEADS]);
    check(!app.printOptionOn[PRINT_RUNNING_HEADS] && app.showPrintPreview, "a click turns the running heads off");
    printPreviewSetPage(app, 1);
    check(inkIn(app, 0, 0, pageW, margin - 1) == 0, "page 2 without its running heads");
    click(app, app.printPreviewOptionBtn[PRINT_PAGE_NUMBERS]);
    check(!app.printOptionOn[PRINT_PAGE_NUMBERS] && inkIn(app, 0, pageH - margin + 1, pageW, pageH) == 0,
          "a click turns the page numbers off");
    click(app, app.printPreviewOptionBtn[PRINT_TWO_COLUMNS]);
    check(!app.printOptionOn[PRINT_TWO_COLUMNS] && within(app.layoutMaxWidth, contentW, 1.0f),
          "one column runs across the page");
    bool onePerPage = true;
    for (size_t i = 0; i < app.printPreviewFrames.size(); ++i)
        onePerPage = onePerPage && app.printPreviewFrames[i].page == (int)i && within(app.printPreviewFrames[i].x, margin);
    check(onePerPage && app.paperPrint.titleBottom > 0.0f, "one column: a stretch a page, the title block still on top");
    const App::LayoutTextRun* wide = runStarting(app, L"Ink on the move: capillary flow in split nibs");
    check(wide && within(wide->bounds.right, contentW, 1.0f), "the one-column title block spans the text width");
    printPreviewSetPage(app, 0);
    check(savePreview(app, out / "preview-one-column.png"), "the one-column preview is saved");
    for (int option : {PRINT_TWO_COLUMNS, PRINT_PAGE_NUMBERS, PRINT_RUNNING_HEADS}) click(app, app.printPreviewOptionBtn[option]);
    check(app.printOptionOn[0] && app.printOptionOn[1] && app.printOptionOn[2], "and every option back on");
    closePrintPreview(app, app.hwnd);
    check(!app.printLayout && app.paperPrint.titleBottom == 0.0f && !app.paperPrint.titleBlock,
          "closing the preview leaves no title block behind");

    // The choices hold for the document; the pages print as previewed
    app.printChoices = {};
    const int printed = printDebugPages(app, (out / L"pages").wstring());
    check(printed == pages && fs::exists(out / "pages" / "page-1.png"), "the pages print as the preview counts them");
    for (int option : {PRINT_RUNNING_HEADS, PRINT_PAGE_NUMBERS}) printPreviewToggle(app, option);
    check(app.printChoices.file == app.currentFile && app.printChoices.runningHeads == 0 &&
              app.printChoices.pageNumbers == 0 && app.printChoices.columns == -1,
          "a toggle outside the preview is kept for this document");
    app.printChoices = {};

    // A narrow window keeps the chips apart
    const int savedW = app.width;
    app.width = 650;
    openPrintPreview(app, app.hwnd);
    app.renderTarget->BeginDraw();
    renderPrintPreview(app);
    app.renderTarget->EndDraw();
    check(app.printPreviewOptionBtn[0].left > 0 && app.printPreviewOptionBtn[2].right < 650.0f,
          "at 650 px the option row fits the window");
    check(savePreview(app, out / "preview-narrow.png"), "the narrow preview is saved");
    closePrintPreview(app, app.hwnd);
    app.width = savedW;
}

// A document that is no paper prints as it always did: one column, a page
// a stretch at the margins, no title block, nothing added to the margins
void notAPaper(App& app, const fs::path& fixtures) {
    check(openDocumentInViewer(app, (fixtures / "crossref.md").wstring()), "the cross-reference notes open");
    app.printPreviewPaper = 0;
    openPrintPreview(app, app.hwnd);
    check(!app.printOptionOn[0] && !app.printOptionOn[1] && !app.printOptionOn[2], "no paper options for notes");
    check(within(app.zoomFactor, 1.0f, 0.0001f) && within(app.printPreviewMarginMm, app.printMarginMm, 0.001f),
          "the page's type size and margin stay");
    const float margin = app.printPreviewMarginMm * 96.0f / 25.4f;
    const float contentW = app.printPreviewPageW - 2 * margin;
    check(app.paperPrint.titleBottom == 0.0f && within(app.layoutMaxWidth, (float)(int)contentW),
          "laid out across the text width, the strip in place of a title block");
    bool asBefore = !app.printPreviewFrames.empty();
    for (size_t i = 0; i < app.printPreviewFrames.size(); ++i) {
        const auto& frame = app.printPreviewFrames[i];
        asBefore = asBefore && frame.page == (int)i && within(frame.x, margin, 0.001f) && within(frame.y, margin, 0.001f) &&
                   frame.clip.right <= frame.clip.left && frame.yStart == app.printPreviewBounds[i] &&
                   frame.yEnd == app.printPreviewBounds[i + 1];
    }
    check(asBefore, "a stretch a page at the margins, unclipped, as print always did");
    const int pages = (int)app.printPreviewBounds.size() - 1;
    check(pages >= 2, "the notes run to more than a page");
    for (int page = pages - 1; page >= 0; --page) {
        printPreviewSetPage(app, page);
        const std::vector<uint8_t> before = pageAsBefore(app, page);
        check(!before.empty() && before == app.printPreviewPixels,
              "page " + std::to_string(page + 1) + " draws pixel for pixel as print drew it before");
        // A quote cut by the page break still runs into the margin, as
        // it always did; the middle of the margins stays empty
        const float mid = app.printPreviewPageW / 2;
        check(inkIn(app, mid - 60, 0, mid + 60, margin - 1) == 0 &&
                  inkIn(app, mid - 60, app.printPreviewPageH - margin + 1, mid + 60, app.printPreviewPageH) == 0,
              "no number and no head on page " + std::to_string(page + 1));
    }
    // Toggled on, notes get numbers and their first heading as a head
    printPreviewToggle(app, PRINT_PAGE_NUMBERS);
    printPreviewToggle(app, PRINT_RUNNING_HEADS);
    check(app.printOptionOn[PRINT_PAGE_NUMBERS] && app.printHeadLeft == L"Numbered figures, tables and equations" &&
              app.printHeadRight.empty(),
          "notes toggled on take numbers, and their first heading as the running head");
    closePrintPreview(app, app.hwnd);
    app.printChoices = {};
}

// --- Word ------------------------------------------------------------------------

void word(App& app, const fs::path& fixtures, const fs::path& out) {
    check(openDocumentInViewer(app, (fixtures / "paper-print.md").wstring()), "the paper opens for Word");
    check(exportDocxFile(app, (out / L"paper-print.docx").wstring()), "the paper exports to Word");
    const std::string docx = read(out / "paper-print.docx");
    const size_t body = docx.find("<w:body>");
    const size_t heading = docx.find("<w:pStyle w:val=\"Heading1\"/>");
    for (const char* style : {"Title", "Subtitle", "Author", "Affiliation", "Date", "AbstractTitle", "Abstract", "Keywords"}) {
        const size_t at = docx.find(std::string("<w:pPr><w:pStyle w:val=\"") + style + "\"/></w:pPr>");
        check(at != std::string::npos && at > body && at < heading,
              std::string("the title block's ") + style + " paragraph comes before the text");
        check(docx.find(std::string("w:styleId=\"") + style + "\"") != std::string::npos,
              std::string("styles.xml defines ") + style);
    }
    check(docx.find("<w:pStyle w:val=\"Title\"/></w:pPr><w:r><w:t xml:space=\"preserve\">Ink on the move: capillary flow in split nibs</w:t></w:r>") !=
              std::string::npos,
          "the title's words");
    check(docx.find("<w:pStyle w:val=\"AbstractTitle\"/></w:pPr><w:r><w:t xml:space=\"preserve\">Abstract</w:t>") !=
              std::string::npos,
          "an Abstract heading paragraph");
    check(docx.find("Poiseuille&apos;s law</w:t>") != std::string::npos ||
              docx.find("Poiseuille's law</w:t>") != std::string::npos,
          "the abstract keeps its emphasis as a run of its own");
    check(docx.find("w:customMarkFollows=\"1\"") != std::string::npos &&
              docx.find("We thank the Tinta reading group") != std::string::npos,
          "the thanks is a footnote marked * on the title");
    const std::string figureField = "<w:instrText xml:space=\"preserve\"> SEQ Figure \\* ARABIC </w:instrText>";
    const size_t figure = docx.find(figureField);
    check(figure != std::string::npos, "the figure's number is a SEQ Figure field");
    check(docx.find("<w:instrText xml:space=\"preserve\"> SEQ Table \\* ARABIC </w:instrText>") != std::string::npos,
          "the table's number is a SEQ Table field");
    check(docx.find(">Figure </w:t>") != std::string::npos && docx.find(">Table </w:t>") != std::string::npos,
          "the label's words lead the field");
    // begin, the code, separate, the number, end: the code bold, as the number
    const size_t begin = docx.rfind("<w:fldChar w:fldCharType=\"begin\"/>", figure);
    const size_t separate = docx.find("<w:fldChar w:fldCharType=\"separate\"/>", figure);
    const size_t number = docx.find(">1</w:t>", figure);
    const size_t end = docx.find("<w:fldChar w:fldCharType=\"end\"/>", figure);
    check(begin != std::string::npos && figure - begin < 200 && separate < number && number < end && end - figure < 600,
          "the field's parts in Word's order");
    check(docx.compare(docx.rfind("<w:r>", figure), 18, "<w:r><w:rPr><w:b/>") == 0,
          "the field's code is bold, so an update keeps the number bold");
    check(docx.find("<w:pStyle w:val=\"Caption\"/>") != std::string::npos, "captions keep Word's Caption style");
    check(docx.find("<w:pStyle w:val=\"Bibliography\"/>") != std::string::npos, "the references keep their style");

    // Notes without a title block write none, and their captions number all the same
    check(openDocumentInViewer(app, (fixtures / "crossref.md").wstring()), "the cross-reference notes open for Word");
    check(exportDocxFile(app, (out / L"crossref.docx").wstring()), "the notes export to Word");
    const std::string notes = read(out / "crossref.docx");
    check(notes.find("<w:pStyle w:val=\"Title\"/>") == std::string::npos &&
              notes.find("<w:pStyle w:val=\"Abstract\"/>") == std::string::npos,
          "notes get no title block");
    check(notes.find("SEQ Figure") != std::string::npos && notes.find("SEQ Table") != std::string::npos,
          "their numbered captions are fields too");
}
}  // namespace

int runPaperExportTests(const char* fixtureDir) {
    const fs::path fixtures = fixtureDir;
    const fs::path out = fs::current_path();
    frontMatter();

    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Paper export tests", WS_POPUP, 0, 0, 1050, 900, nullptr, nullptr,
                               nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    applyTheme(app, 0);
    updateTextFormats(app);

    twoColumnPaper(app, fixtures, out);
    notAPaper(app, fixtures);
    word(app, fixtures, out);

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Paper export: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
