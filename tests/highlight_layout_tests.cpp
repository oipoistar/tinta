#include "d2d_init.h"
#include "render.h"
#include "search.h"
#include "selection.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <vector>
#include <wrl/client.h>

// A ==highlight== split into several runs (scripts, strikethrough, links,
// inline math) must paint one even band: its colour is translucent, so
// overlapping pieces show as darker strips and gaps as pale ones. A
// selection over such runs, or over bold and italic words, must too (#313).

namespace {
int failures = 0;
std::string scenario;
std::map<std::string, int> failuresByCheck;
void check(bool condition, const char* message) {
    if (!condition) {
        if (failures < 30) std::cerr << "FAIL: " << scenario << ": " << message << '\n';
        ++failures;
        ++failuresByCheck[message];
    }
}

bool isHighlight(const App& app, const App::LayoutRect& item) {
    const D2D1_COLOR_F c = themeHighlightBackground(app.theme);
    return item.color.r == c.r && item.color.g == c.g && item.color.b == c.b && item.color.a == c.a;
}

std::vector<D2D1_RECT_F> highlightBands(const App& app) {
    std::vector<D2D1_RECT_F> bands;
    for (const auto& item : app.layoutRects)
        if (isHighlight(app, item)) bands.push_back(item.rect);
    return bands;
}

bool layout(App& app, const std::string& source) {
    const auto doc = app.parser.parse(source);
    check(doc.success, "source parses");
    if (!doc.success) return false;
    app.root = doc.root;
    layoutDocument(app);
    return true;
}

// No two bands paint the same pixels
void checkNoOverlap(const std::vector<D2D1_RECT_F>& bands, const char* message) {
    for (size_t i = 0; i < bands.size(); ++i) {
        for (size_t j = i + 1; j < bands.size(); ++j) {
            const auto& a = bands[i];
            const auto& b = bands[j];
            const float w = std::min(a.right, b.right) - std::max(a.left, b.left);
            const float h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
            if (w > 0.01f && h > 0.01f) {
                std::cerr << "  overlap " << w << " x " << h << " px at x " << b.left << ", y " << b.top << '\n';
                check(false, message);
            }
        }
    }
}

void checkNoOverlap(const App& app) {
    checkNoOverlap(highlightBands(app), "highlight bands do not overlap");
}

// Software raster of one pixel row of the highlight bands alone, so text and
// math rules cannot hide a seam. Layout units are device pixels at 96 DPI,
// as on screen.
class BandRaster {
public:
    bool init(UINT width) {
        width_ = width;
        return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&wic_))) &&
               SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf())) &&
               SUCCEEDED(wic_->CreateBitmap(width, 3, GUID_WICPixelFormat32bppPBGRA,
                                            WICBitmapCacheOnLoad, &bitmap_)) &&
               SUCCEEDED(d2d_->CreateWicBitmapRenderTarget(bitmap_.Get(),
                   D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(), 96, 96),
                   &target_)) &&
               SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_));
    }
    // Pixel row y of the bands drawn over the page background
    bool row(const App& app, const std::vector<D2D1_RECT_F>& bands, int y, D2D1_COLOR_F colour) {
        target_->BeginDraw();
        target_->Clear(app.theme.background);
        target_->SetTransform(D2D1::Matrix3x2F::Translation(0, 1.0f - y));
        brush_->SetColor(colour);
        for (const auto& band : bands) target_->FillRectangle(band, brush_.Get());
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        if (FAILED(target_->EndDraw())) return false;
        WICRect line{0, 1, static_cast<INT>(width_), 1};
        pixels_.assign(width_, 0);
        return SUCCEEDED(bitmap_->CopyPixels(&line, width_ * 4, width_ * 4,
                                             reinterpret_cast<BYTE*>(pixels_.data())));
    }
    bool contains(float right) const { return right < width_; }
    UINT32 at(int x) const { return pixels_[x]; }

private:
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IWICBitmap> bitmap_;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    std::vector<UINT32> pixels_;
    UINT width_ = 0;
};

bool samePixel(UINT32 a, UINT32 b) {
    for (int shift = 0; shift < 32; shift += 8) {
        if (std::abs(static_cast<int>((a >> shift) & 0xff) - static_cast<int>((b >> shift) & 0xff)) > 1) return false;
    }
    return true;
}

// The highlight running from `first` to `last` covers each of its lines as
// one even band: 2 px past its first and last character, with no gap or
// overlap between the pieces in between
void checkContinuous(App& app, BandRaster& raster, const wchar_t* first, const wchar_t* last,
                     size_t expectedLines = 0) {
    const size_t from = app.docText.find(first);
    const size_t to = app.docText.find(last, from == std::wstring::npos ? 0 : from);
    check(from != std::wstring::npos && to != std::wstring::npos, "highlighted words are laid out");
    if (from == std::wstring::npos || to == std::wstring::npos) return;
    const size_t end = to + wcslen(last);

    struct Line { float top, bottom, left, right; };
    std::vector<Line> lines;
    for (const auto& rect : app.textRects) {
        if (rect.docStart < from || rect.docStart >= end) continue;
        auto line = std::find_if(lines.begin(), lines.end(),
                                 [&](const Line& l) { return std::abs(l.top - rect.rect.top) < 0.01f; });
        if (line == lines.end()) {
            lines.push_back({rect.rect.top, rect.rect.bottom, rect.rect.left, rect.rect.right});
        } else {
            line->bottom = std::max(line->bottom, rect.rect.bottom);
            line->left = std::min(line->left, rect.rect.left);
            line->right = std::max(line->right, rect.rect.right);
        }
    }
    check(!lines.empty(), "the highlight has text rects");
    if (expectedLines) check(lines.size() >= expectedLines, "the long highlight wraps");

    const auto bands = highlightBands(app);
    std::vector<D2D1_RECT_F> drawn;
    for (const auto& line : lines) {
        std::vector<D2D1_RECT_F> onLine;
        for (const auto& band : bands)
            if (band.top >= line.top - 0.01f && band.bottom <= line.bottom + 0.01f) onLine.push_back(band);
        check(!onLine.empty(), "every highlighted line has a band");
        if (onLine.empty()) continue;
        std::sort(onLine.begin(), onLine.end(),
                  [](const D2D1_RECT_F& a, const D2D1_RECT_F& b) { return a.left < b.left; });
        check(std::abs(onLine.front().left - (line.left - 2)) < 0.01f, "the band starts 2 px before the line's first character");
        check(std::abs(onLine.back().right - (line.right + 2)) < 0.01f, "the band ends 2 px after the line's last character");
        float rowTop = onLine.front().top, rowBottom = onLine.front().bottom;
        for (size_t i = 0; i < onLine.size(); ++i) {
            if (i && std::abs(onLine[i].left - onLine[i - 1].right) >= 0.01f) {
                std::cerr << "  pieces " << onLine[i - 1].right << " | " << onLine[i].left << " at y " << line.top << '\n';
                check(false, "neighbouring pieces meet exactly");
            }
            if (onLine[i].bottom - onLine[i].top < rowBottom - rowTop) {
                rowTop = onLine[i].top;
                rowBottom = onLine[i].bottom;
            }
        }
        for (const auto& band : onLine)
            check(band.top <= rowTop && band.bottom >= rowBottom, "every piece spans the text band");
        drawn.insert(drawn.end(), onLine.begin(), onLine.end());

        // The raster agrees: one colour along the middle of the band
        if (!raster.contains(onLine.back().right + 2)) continue;
        const int y = static_cast<int>((rowTop + rowBottom) / 2);
        if (!raster.row(app, onLine, y, themeHighlightBackground(app.theme))) {
            check(false, "highlight raster draws");
            continue;
        }
        const int x0 = static_cast<int>(std::ceil(onLine.front().left)) + 1;
        const int x1 = static_cast<int>(std::floor(onLine.back().right)) - 2;
        const UINT32 expected = raster.at(x0);
        int uneven = 0;
        for (int x = x0; x <= x1; ++x) {
            if (!samePixel(raster.at(x), expected)) {
                if (!uneven) std::cerr << "  uneven pixel " << std::hex << raster.at(x) << " vs " << expected
                                       << std::dec << " at x " << x << ", y " << y << '\n';
                ++uneven;
            }
        }
        check(uneven == 0, "the band paints one even colour");
    }
}

void checkSeparate(App& app) {
    // ==first== ==second==: two highlights, whatever their spacing
    const size_t a = app.docText.find(L"first");
    const size_t b = app.docText.find(L"second");
    const App::TextRect* left = nullptr;
    const App::TextRect* right = nullptr;
    for (const auto& rect : app.textRects) {
        if (rect.docStart == a) left = &rect;
        if (rect.docStart == b) right = &rect;
    }
    check(left && right, "both separate highlights are laid out");
    if (!left || !right || std::abs(left->rect.top - right->rect.top) > 0.01f) return;
    const float gapLeft = left->rect.right + 2, gapRight = right->rect.left - 2;
    if (gapRight - gapLeft < 1) return;  // they touch: nothing to keep apart
    const float middle = (gapLeft + gapRight) / 2;
    for (const auto& band : highlightBands(app))
        check(!(band.left < middle && band.right > middle && band.top < left->rect.bottom && band.bottom > left->rect.top),
              "separate highlights keep the space between them");
}

D2D1_COLOR_F selectionColour(const App& app) {
    return themeSelectionColor(app.theme, D2D1::ColorF(0.2f, 0.4f, 0.9f, 0.35f));
}

std::vector<D2D1_RECT_F> selectionBands(const App& app, size_t start, size_t end) {
    std::vector<D2D1_RECT_F> bands;
    selectionHighlightRects(app, start, end, bands);
    return bands;
}

// The text rect that starts with the word
const App::TextRect* wordRect(const App& app, const wchar_t* word) {
    const size_t at = app.docText.find(word);
    for (const auto& rect : app.textRects)
        if (at != std::wstring::npos && rect.docStart == at) return &rect;
    return nullptr;
}

// A selection from `first` to `last` (both whole words) paints each of its
// lines as one even band from the line's first selected character to its
// last. A run ends at its last word, so the spaces between two runs ("sit
// **amet**", "*commodo* consequat", a soft line break) and the padding
// after a code chip belong to none: the band bridges them (#313).
void checkSelection(App& app, BandRaster& raster, const wchar_t* first, const wchar_t* last) {
    const size_t from = app.docText.find(first);
    const size_t to = app.docText.find(last, from == std::wstring::npos ? 0 : from);
    check(from != std::wstring::npos && to != std::wstring::npos, "selected words are laid out");
    if (from == std::wstring::npos || to == std::wstring::npos) return;
    const size_t end = to + wcslen(last);

    struct Line { float top, bottom, left, right; };
    std::vector<Line> lines;
    for (const auto& rect : app.textRects) {
        if (rect.docStart < from || rect.docStart >= end) continue;
        auto line = std::find_if(lines.begin(), lines.end(),
                                 [&](const Line& l) { return std::abs(l.top - rect.rect.top) < 0.01f; });
        if (line == lines.end()) {
            lines.push_back({rect.rect.top, rect.rect.bottom, rect.rect.left, rect.rect.right});
        } else {
            line->bottom = std::max(line->bottom, rect.rect.bottom);
            line->left = std::min(line->left, rect.rect.left);
            line->right = std::max(line->right, rect.rect.right);
        }
    }
    check(!lines.empty(), "the selection has text rects");

    const auto bands = selectionBands(app, from, end);
    checkNoOverlap(bands, "selection pieces do not overlap");
    // On screen, at any scroll, the bands sit on whole pixels and those of
    // lines that meet share their edge: no antialiased seam between lines
    for (float scroll : {0.0f, 0.37f, 0.5f, 0.83f, 120.25f}) {
        for (const auto& band : bands) {
            const D2D1_RECT_F above = selectionBarOnScreen(band, 0.0f, scroll);
            check(above.top == std::round(above.top) && above.bottom == std::round(above.bottom),
                  "selection bands sit on whole pixels");
            for (const auto& next : bands) {
                if (std::abs(band.bottom - next.top) > 0.01f) continue;
                check(above.bottom == selectionBarOnScreen(next, 0.0f, scroll).top,
                      "the lines of a selection meet without a seam");
            }
        }
    }
    for (const auto& line : lines) {
        std::vector<D2D1_RECT_F> onLine;
        for (const auto& band : bands)
            if (band.top < line.bottom - 0.01f && band.bottom > line.top + 0.01f) onLine.push_back(band);
        if (onLine.size() != 1) std::cerr << "  " << onLine.size() << " selection pieces at y " << line.top << '\n';
        check(onLine.size() == 1, "the selection paints each line as one band");
        if (onLine.empty()) continue;
        float left = onLine.front().left, right = onLine.front().right;
        for (const auto& band : onLine) {
            left = std::min(left, band.left);
            right = std::max(right, band.right);
        }
        check(std::abs(left - line.left) < 0.05f, "the selection starts at the line's first selected character");
        check(std::abs(right - line.right) < 0.05f, "the selection ends at the line's last selected character");

        if (!raster.contains(right + 2)) continue;
        const int y = static_cast<int>((line.top + line.bottom) / 2);
        if (!raster.row(app, onLine, y, selectionColour(app))) {
            check(false, "selection raster draws");
            continue;
        }
        const int x0 = static_cast<int>(std::ceil(left)) + 1;
        const int x1 = static_cast<int>(std::floor(right)) - 2;
        const UINT32 expected = raster.at(x0);
        int uneven = 0;
        for (int x = x0; x <= x1; ++x) {
            if (!samePixel(raster.at(x), expected)) {
                if (!uneven) std::cerr << "  uneven selection pixel " << std::hex << raster.at(x) << " vs "
                                       << expected << std::dec << " at x " << x << ", y " << y << '\n';
                ++uneven;
            }
        }
        check(uneven == 0, "the selection paints one even colour");
    }
}

// In "alpha sit **amet** omega" a selection that starts or ends at the
// space beside the bold word covers just as much of it
void checkSelectionEdges(App& app) {
    const App::TextRect* sit = wordRect(app, L"sit");
    const App::TextRect* amet = wordRect(app, L"amet");
    const App::TextRect* omega = wordRect(app, L"omega");
    check(sit && amet && omega && std::abs(sit->rect.top - omega->rect.top) < 0.01f,
          "the words around the bold one share a line");
    if (!sit || !amet || !omega) return;
    const size_t sitEnd = sit->docStart + sit->docLength, ametEnd = amet->docStart + amet->docLength;
    struct Span { size_t start, end; float left, right; const char* message; };
    const Span spans[] = {
        {sitEnd, ametEnd, sit->rect.right, amet->rect.right, "the space before a bold word is selected with it"},
        {sit->docStart, amet->docStart, sit->rect.left, amet->rect.left, "a word is selected with the space after it"},
        {amet->docStart, ametEnd, amet->rect.left, amet->rect.right, "a bold word alone is selected without its spaces"},
        {ametEnd, omega->docStart, amet->rect.right, omega->rect.left, "the space after a bold word is selected alone"},
    };
    for (const auto& span : spans) {
        const auto bands = selectionBands(app, span.start, span.end);
        check(bands.size() == 1 && std::abs(bands[0].left - span.left) < 0.05f &&
                  std::abs(bands[0].right - span.right) < 0.05f,
              span.message);
    }
}

// In "alpha sit  **amet** omega" a selection from the second of the two
// spaces takes the half of the gap that holds it
void checkHalfGap(App& app) {
    const App::TextRect* sit = wordRect(app, L"sit");
    const App::TextRect* amet = wordRect(app, L"amet");
    check(sit && amet && amet->docStart == sit->docStart + sit->docLength + 2, "two spaces part the words");
    if (!sit || !amet) return;
    const auto bands = selectionBands(app, amet->docStart - 1, amet->docStart + amet->docLength);
    check(bands.size() == 1 && std::abs(bands[0].left - (sit->rect.right + amet->rect.left) / 2) < 0.05f &&
              std::abs(bands[0].right - amet->rect.right) < 0.05f,
          "a selection from the second of two spaces takes half the gap");
}

// In "alpha `chip`, omega" the chip selected alone keeps to its text; the
// padding after it joins the band only when the selection runs on past it
void checkChipAlone(App& app) {
    const App::TextRect* chip = wordRect(app, L"chip");
    const App::TextRect* omega = wordRect(app, L"omega");
    check(chip && omega, "the code chip is laid out");
    if (!chip || !omega) return;
    const size_t chipEnd = chip->docStart + chip->docLength;
    auto bands = selectionBands(app, chip->docStart, chipEnd);
    check(bands.size() == 1 && std::abs(bands[0].left - chip->rect.left) < 0.05f &&
              std::abs(bands[0].right - chip->rect.right) < 0.05f,
          "a code chip selected alone paints just its text");
    bands = selectionBands(app, chip->docStart, chipEnd + 1);
    check(bands.size() == 1 && bands[0].right > chip->rect.right + 1.0f,
          "a code chip selected with the comma after it paints its padding too");
}

// Cells keep the gap between them: a selection over a row paints only the
// text of each cell
void checkCellsApart(App& app) {
    const App::TextRect* one = wordRect(app, L"one");
    const App::TextRect* two = wordRect(app, L"two");
    check(one && two && std::abs(one->rect.top - two->rect.top) < 0.01f, "both cells share a row");
    if (!one || !two) return;
    const float middle = (one->rect.right + two->rect.left) / 2;
    for (const auto& band : selectionBands(app, app.docText.find(L"alpha"), app.docText.find(L"omega") + 5))
        check(!(band.left < middle && band.right > middle && band.top < one->rect.bottom &&
                band.bottom > one->rect.top),
              "a selection over a row keeps the gap between its cells");
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}
}

// Every kind of block, over and over, so the document is many windows long
std::string longDocument(int sections) {
    std::string source;
    for (int i = 0; i < sections; ++i) {
        const std::string n = std::to_string(i);
        source += "## Section " + n + " of the long document\n\n";
        source += "The **bold " + n + "** words, the *italic ones*, `code`, [a link](#section-" + n +
                  ") and ==marked text== run over the edge of a narrow window, so the paragraph "
                  "wraps onto several lines of words and each line has runs of its own.\n\n";
        source += "- item one of " + n + "\n- item two with **weight**\n  - a nested item\n\n";
        source += "| Left " + n + " | Right |\n| --- | --- |\n| alpha | beta **bold** |\n| gamma | delta |\n\n";
        source += "```cpp\nint value" + n + " = " + n + "; // the letters of a fence\n```\n\n";
        source += "> A quote with x^2^ and H~2~O in the text of it.\n\n";
    }
    return source;
}

bool reaches(const D2D1_RECT_F& rect, float top, float bottom) {
    return rect.bottom >= top && rect.top <= bottom;
}

// The rects that reach into the lines between two heights, in a fixed order
std::vector<D2D1_RECT_F> between(std::vector<D2D1_RECT_F> rects, float top, float bottom) {
    rects.erase(std::remove_if(rects.begin(), rects.end(),
                               [&](const D2D1_RECT_F& rect) { return !reaches(rect, top, bottom); }),
                rects.end());
    std::sort(rects.begin(), rects.end(), [](const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
        return a.top != b.top ? a.top < b.top : a.left < b.left;
    });
    return rects;
}

bool sameRects(const std::vector<D2D1_RECT_F>& a, const std::vector<D2D1_RECT_F>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].left - b[i].left) > 0.01f || std::abs(a[i].right - b[i].right) > 0.01f ||
            std::abs(a[i].top - b[i].top) > 0.01f || std::abs(a[i].bottom - b[i].bottom) > 0.01f) {
            return false;
        }
    }
    return true;
}

// A frame asks for the selection bars and the search marks of the lines it
// draws. They must be the ones the whole document gives for those lines, and
// what they cost must follow the window, not the document: every one is
// measured against a DirectWrite layout, and a long document selected whole,
// or searched for a common letter, was measured from end to end for every
// frame. The cost is counted, not timed.
void checkViewRanges(App& app) {
    const float margin = 50.0f;  // what render() adds above and below the window
    const float lowest = std::numeric_limits<float>::lowest(), highest = std::numeric_limits<float>::max();
    if (!layout(app, longDocument(150))) return;
    const size_t all = app.docText.size();
    check(app.contentHeight > app.height * 20.0f, "the long document is many windows long");

    app.searchQuery = L"e";
    performSearch(app);
    check(app.searchMatches.size() > 5000, "the letter is found all through the document");
    std::vector<SearchHighlight> everyMark;
    size_t before = selectionMeasureCount();
    searchHighlightRects(app, lowest, highest, everyMark);
    const size_t everyMarkCost = selectionMeasureCount() - before;
    check(everyMark.size() >= app.searchMatches.size(), "every match is marked somewhere in the document");
    std::vector<bool> marked(app.searchMatches.size(), false);
    for (const auto& mark : everyMark) {
        if (mark.matchIndex < marked.size()) marked[mark.matchIndex] = true;
    }
    check(std::find(marked.begin(), marked.end(), false) == marked.end(), "no match goes unmarked");

    before = selectionMeasureCount();
    const std::vector<D2D1_RECT_F> everyBar = selectionBands(app, 0, all);
    const size_t everyBarCost = selectionMeasureCount() - before;

    for (float at : {0.0f, 0.31f, 0.5f, 1.0f}) {
        const float viewTop = at * (app.contentHeight - app.height);
        const float viewBottom = viewTop + app.height;
        const float top = viewTop - margin, bottom = viewBottom + margin;

        size_t rectsInWindow = 0;
        const App::TextRect* middle = nullptr;
        for (const auto& rect : app.textRects) {
            if (rect.docLength == 0 || !reaches(rect.rect, top, bottom)) continue;
            ++rectsInWindow;
            if (!middle && rect.docLength > 3 && rect.rect.top > (viewTop + viewBottom) / 2) middle = &rect;
        }
        check(rectsInWindow > 0 && middle, "the window holds text");
        if (!middle) continue;

        // The whole document selected; a selection that starts in the
        // window; one that ends in it
        const size_t cut = middle->docStart + 2;
        const std::pair<size_t, size_t> ranges[] = {{size_t{0}, all}, {cut, all}, {size_t{0}, cut}};
        for (const auto& range : ranges) {
            std::vector<D2D1_RECT_F> bars;
            before = selectionMeasureCount();
            selectionHighlightRects(app, range.first, range.second, top, bottom, bars);
            const size_t cost = selectionMeasureCount() - before;
            check(!bars.empty(), "the window's lines are selected");
            check(cost <= rectsInWindow, "a frame measures no more of a selection than the lines it draws");
            for (const auto& bar : bars) {
                check(reaches(bar, top - margin, bottom + margin), "a frame gets no bar from outside its lines");
            }
            const std::vector<D2D1_RECT_F> whole =
                range.second - range.first == all ? everyBar : selectionBands(app, range.first, range.second);
            check(sameRects(between(bars, viewTop, viewBottom), between(whole, viewTop, viewBottom)),
                  "the lines in view are selected as the whole document selects them");
            if (range.second - range.first == all) {
                check(cost * 10 < everyBarCost, "the lines in view cost a fraction of the whole selection");
            }
        }

        std::vector<SearchHighlight> marks;
        before = selectionMeasureCount();
        searchHighlightRects(app, top, bottom, marks);
        const size_t cost = selectionMeasureCount() - before;
        check(!marks.empty(), "the window's lines have marks");
        check(cost <= marks.size(), "a frame measures a match once for each mark it draws");
        check(cost * 10 < everyMarkCost, "the marks in view cost a fraction of the whole document's");
        std::vector<D2D1_RECT_F> inView, expected;
        for (const auto& mark : marks) {
            check(reaches(mark.rect, top - margin, bottom + margin), "a frame gets no mark from outside its lines");
            inView.push_back(mark.rect);
        }
        for (const auto& mark : everyMark) expected.push_back(mark.rect);
        check(sameRects(between(inView, viewTop, viewBottom), between(expected, viewTop, viewBottom)),
              "the lines in view are marked as the whole document marks them");
        // Each mark belongs to the match whose letters it covers
        for (const auto& mark : marks) {
            bool found = false;
            for (const auto& other : everyMark) {
                if (other.matchIndex == mark.matchIndex && std::abs(other.rect.left - mark.rect.left) < 0.01f &&
                    std::abs(other.rect.top - mark.rect.top) < 0.01f) {
                    found = true;
                    break;
                }
            }
            check(found, "a mark in view names the match it names in the whole document");
        }
    }

    app.searchQuery.clear();
    app.searchMatches.clear();
}

int runHighlightLayoutTests() {
    namespace fs = std::filesystem;
    {
        auto state = std::make_unique<App>();
        App& app = *state;
        if (!initD2D(app)) return 2;
        BandRaster raster;
        if (!raster.init(2400)) return 2;
        app.readingWidthPct = 100;

        const fs::path fixtures = fs::path(TINTA_FRAGMENT_FIXTURE).parent_path().parent_path();
        std::vector<std::pair<std::string, std::string>> documents;
        for (const char* name : {"highlight-seams.md", "math-mixed-layout.md", "superscript-subscript.md",
                                 "footnotes.md", "markdown-regression-control.md", "math-inline-stress.md",
                                 "struck-and-marked-emphasis.md"}) {
            documents.emplace_back(name, read(fixtures / name));
            check(!documents.back().second.empty(), "fixture loads");
        }

        struct Case { const char* source; size_t lines; };
        const std::string longHighlight =
            "==alpha keeps x^2^, H~2~O, ~~struck text~~ and $E=mc^2$ on its background while it wraps, "
            "then e^iπ^ and $\\sqrt[3]{8}=2$ beside [[Wiki Page]] and CO~2~ at 10^5^ Pa, and again x^2^, "
            "H~2~O and $\\dfrac{1}{1+x^2}$ until the omega==\n";
        // Bold, italic and links inside a highlight are runs of their own
        const std::string longEmphasisHighlight =
            "==alpha keeps **bold words**, *italic words*, ***both at once*** and [a link](#target) on "
            "its background while it wraps, then ~~**struck bold**~~ and *more italic words* beside "
            "**more bold words** over several lines in a narrow window until the omega==\n";
        const Case cases[] = {
            {"==alpha **bold** omega==\n", 0},
            {"==alpha *italic* and [a link](#target) omega==\n", 0},
            {"==**alpha bold** and *italic omega*==\n", 0},
            {"==alpha ~~**struck bold**~~ omega==\n", 0},
            {"- ==alpha **bold** *italic* omega==\n", 0},
            {"| Head |\n| --- |\n| ==alpha **bold** omega== |\n", 0},
            {longEmphasisHighlight.c_str(), 3},
            {"==alpha x^2^ omega==\n", 0},
            {"==alpha H~2~O omega==\n", 0},
            {"==alpha x^2^H~2~O omega==\n", 0},
            {"==alpha $\\sqrt{x}$ omega==\n", 0},
            {"==alpha ($x$) omega==\n", 0},
            {"==alpha ~~struck words~~ omega==\n", 0},
            {"==alpha [[Wiki Page]] omega==\n", 0},
            {"==alpha e^iπ^ beside $\\dfrac{1}{1+x^2}$ and CO~2~ omega==\n", 0},
            {"**==alpha x^2^ omega==**\n", 0},
            {"*==alpha H~2~O $y$ omega==*\n", 0},
            {"[==alpha x^2^ omega==](#target)\n", 0},
            {"# ==alpha x^2^ $y$ omega==\n", 0},
            {"> ==alpha H~2~O $y$ omega==\n", 0},
            {"- ==alpha x^2^ $y$ omega==\n", 0},
            {"| Head |\n| --- |\n| ==alpha x^2^ $y$ omega== |\n", 0},
            {longHighlight.c_str(), 3},
        };

        // Selections over runs of every kind, outside a highlight (#313)
        const std::string selectionFixture = read(fixtures / "selection-over-emphasis-313.md");
        check(!selectionFixture.empty(), "fixture loads");
        const std::string longSelection =
            "alpha keeps **bold words**, *italic words*, ***both at once***, `code` and [a link](#target) "
            "while it wraps, then **more bold** and *more italic* beside `more code`, and $x^2$ over "
            "several lines in a narrow window until the omega\n";
        const char* selections[] = {
            "alpha sit **amet**, consectetur omega\n",
            "alpha nisi *ut aliquip* consequat omega\n",
            "alpha ***both*** and __bold__ and _italic_ omega\n",
            "alpha [a link](#target) and **[bold link](#target)** omega\n",
            "alpha [[Wiki Page]] omega\n",
            "alpha one\ntwo **three**\n*four* omega\n",
            "alpha `code` and `chip`, omega\n",
            "alpha $x$ and $\\sqrt{y}$ omega\n",
            "alpha ~~struck~~ and ==marked== omega\n",
            "alpha x^2^ and H~2~O omega\n",
            "# alpha **bold** *italic* omega\n",
            "> alpha **bold** *italic* omega\n",
            "- alpha **bold** *italic* omega\n",
            "| Head |\n| --- |\n| alpha **bold** *italic* omega |\n",
            longSelection.c_str(),
        };

        // What a frame asks for in a long document, in a narrow and a wide window
        applyTheme(app, 0);
        app.contentScale = 1.0f;
        app.zoomFactor = 1.0f;
        updateTextFormats(app);
        app.height = 900;
        for (int width : {420, 1050}) {
            app.width = width;
            scenario = "long document at width " + std::to_string(width);
            checkViewRanges(app);
        }

        for (int theme : {0, 5}) {  // Paper, Midnight
            applyTheme(app, theme);
            for (float scale : {1.0f, 1.5f, 2.0f}) {
                app.contentScale = scale;
                for (float zoom : {0.8f, 1.0f, 1.5f}) {
                    app.zoomFactor = zoom;
                    updateTextFormats(app);
                    for (int width : {420, 650, 1050}) {
                        app.width = static_cast<int>(width * scale);
                        app.height = static_cast<int>(900 * scale);
                        const std::string where = "theme" + std::to_string(theme) + "-scale" +
                            std::to_string(static_cast<int>(scale * 100)) + "-zoom" +
                            std::to_string(static_cast<int>(zoom * 100)) + "-width" + std::to_string(width);
                        for (const auto& c : cases) {
                            scenario = where + ": " + c.source;
                            scenario.pop_back();
                            if (!layout(app, c.source)) continue;
                            checkNoOverlap(app);
                            checkContinuous(app, raster, L"alpha", L"omega", width == 420 ? c.lines : 0);
                            checkSelection(app, raster, L"alpha", L"omega");
                        }
                        for (const char* source : selections) {
                            scenario = where + ": select " + source;
                            scenario.pop_back();
                            if (layout(app, source)) checkSelection(app, raster, L"alpha", L"omega");
                        }
                        scenario = where + ": select beside a bold word";
                        if (layout(app, "alpha sit **amet** omega\n")) checkSelectionEdges(app);
                        scenario = where + ": select from the second of two spaces";
                        if (layout(app, "alpha sit  **amet** omega\n")) checkHalfGap(app);
                        scenario = where + ": select a code chip";
                        if (layout(app, "alpha `chip`, omega\n")) checkChipAlone(app);
                        scenario = where + ": select over a table row";
                        if (layout(app, "| alpha one | two omega |\n| --- | --- |\n")) checkCellsApart(app);
                        scenario = where + ": selection-over-emphasis-313.md";
                        if (layout(app, selectionFixture)) {
                            checkSelection(app, raster, L"Lorem", L"laborum.");
                            checkSelection(app, raster, L"Bold beside", L"this paragraph.");
                            checkSelection(app, raster, L"metin", L"kleri.");
                            checkSelection(app, raster, L"A heading with", L"italic words");
                            checkSelection(app, raster, L"A quote with", L"inside it.");
                            checkSelection(app, raster, L"A long item", L"the end.");
                            checkSelection(app, raster, L"The last paragraph", L"italic words.");
                            checkNoOverlap(selectionBands(app, 0, app.docText.size()),
                                           "selection pieces do not overlap");
                        }
                        scenario = where + ": separate highlights";
                        if (layout(app, "==first== ==second== and ==third==, ==fourth==\n")) {
                            checkNoOverlap(app);
                            checkSeparate(app);
                        }
                        for (const auto& document : documents) {
                            scenario = where + ": " + document.first;
                            if (!layout(app, document.second)) continue;
                            checkNoOverlap(app);
                            check(!highlightBands(app).empty(), "the fixture has highlights");
                        }
                        scenario = where + ": highlight-seams.md";
                        if (layout(app, documents.front().second)) {
                            check(app.tableRects.size() == 1 && app.codeBlocks.size() == 1,
                                  "the fixture's table and code fence remain");
                            check(app.docText.find(L"All surrounding content") != std::wstring::npos,
                                  "the fixture renders through its final paragraph");
                            check(app.docText.find(L"==marked x^2^== stays literal") != std::wstring::npos,
                                  "the fence keeps its highlight delimiters");
                        }
                    }
                }
            }
        }
        app.shutdown();
    }
    CoUninitialize();
    for (const auto& [message, count] : failuresByCheck) std::cerr << count << " x " << message << '\n';
    std::cout << "Highlight layout: " << failures << " failures\n";
    return failures ? 1 : 0;
}
