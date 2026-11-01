#include "d2d_init.h"
#include "render.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
#include <wrl/client.h>

// A ==highlight== split into several runs (scripts, strikethrough, links,
// inline math) must paint one even band: its colour is translucent, so
// overlapping pieces show as darker strips and gaps as pale ones.

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

// No two highlight bands paint the same pixels
void checkNoOverlap(const App& app) {
    const auto bands = highlightBands(app);
    for (size_t i = 0; i < bands.size(); ++i) {
        for (size_t j = i + 1; j < bands.size(); ++j) {
            const auto& a = bands[i];
            const auto& b = bands[j];
            const float w = std::min(a.right, b.right) - std::max(a.left, b.left);
            const float h = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
            if (w > 0.01f && h > 0.01f) {
                std::cerr << "  overlap " << w << " x " << h << " px at x " << b.left << ", y " << b.top << '\n';
                check(false, "highlight bands do not overlap");
            }
        }
    }
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
    bool row(const App& app, const std::vector<D2D1_RECT_F>& bands, int y) {
        target_->BeginDraw();
        target_->Clear(app.theme.background);
        target_->SetTransform(D2D1::Matrix3x2F::Translation(0, 1.0f - y));
        brush_->SetColor(themeHighlightBackground(app.theme));
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
        if (!raster.row(app, onLine, y)) { check(false, "highlight raster draws"); continue; }
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

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}
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
