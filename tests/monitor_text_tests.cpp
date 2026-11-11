#include "d2d_init.h"
#include "input.h"
#include "print.h"
#include "render.h"

#include <wrl/client.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// Text is smoothed for the monitor the window is on. The render target took
// the primary monitor's gamma, contrast and subpixel order once, for every
// screen: on a second monitor with its subpixels in another order, or tuned
// otherwise in ClearType's tuner, letters had coloured edges.
namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

// A second monitor, stood in for the one this PC may not have: the window
// is "on" whichever handle `on` holds, and the second has values of its own
struct Stand {
    HMONITOR real = nullptr;
    HMONITOR second = (HMONITOR)(INT_PTR)0x5EC0;
    HMONITOR dark = (HMONITOR)(INT_PTR)0xDA2C;  // one whose values cannot be read
    HMONITOR on = nullptr;
    float gamma = 1.2f;
    float contrast = 0.25f;
    DWRITE_PIXEL_GEOMETRY geometry = DWRITE_PIXEL_GEOMETRY_BGR;
    int asked = 0;
} stand;

HMONITOR standMonitor(HWND) { return stand.on; }
HRESULT standParams(IDWriteFactory* factory, HMONITOR monitor, IDWriteRenderingParams** params) {
    ++stand.asked;
    if (monitor == stand.dark) return E_FAIL;
    if (monitor != stand.second) return factory->CreateMonitorRenderingParams(monitor, params);
    // A ClearType level and a mode of the monitor's own, which Tinta does
    // not take: it draws at full level in the natural symmetric mode
    return factory->CreateCustomRenderingParams(stand.gamma, stand.contrast, 0.5f, stand.geometry,
                                                DWRITE_RENDERING_MODE_GDI_CLASSIC, params);
}

struct Values {
    float gamma = -1, contrast = -1, level = -1;
    int geometry = -1, mode = -1;
};
Values of(IDWriteRenderingParams* params) {
    Values v;
    if (!params) return v;
    v.gamma = params->GetGamma();
    v.contrast = params->GetEnhancedContrast();
    v.level = params->GetClearTypeLevel();
    v.geometry = (int)params->GetPixelGeometry();
    v.mode = (int)params->GetRenderingMode();
    return v;
}
Values targetValues(App& app) {
    ComPtr<IDWriteRenderingParams> params;
    app.renderTarget->GetTextRenderingParams(&params);
    return of(params.Get());
}
bool sameScreen(const Values& a, const Values& b) {
    return a.gamma == b.gamma && a.contrast == b.contrast && a.geometry == b.geometry;
}
// What Tinta adds to any monitor's values
bool tintaDraws(const Values& v) {
    return v.level == 1.0f && v.mode == (int)DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
}

// The page as the window's own context draws it, in pixels
std::vector<BYTE> pageOf(App& app, UINT width, UINT height, bool sample = false) {
    std::vector<BYTE> pixels;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Bitmap1> target, readable;
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
    if (FAILED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc))) ||
        FAILED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target)) ||
        FAILED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ |
                                                            D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
                                &readable))) {
        return pixels;
    }
    ComPtr<ID2D1Image> previous;
    dc->GetTarget(&previous);
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    if (sample) {
        // Black letters on white: any tint is the smoothing's own
        static const wchar_t text[] =
            L"Illinois lilt, minimum, HILLTOP, illicit, willfully, 1111 0101 |||| !!!! "
            L"The five boxing wizards jump quickly. Sphinx of black quartz, judge my vow. "
            L"Illinois lilt, minimum, HILLTOP, illicit, willfully, 1111 0101 |||| !!!!";
        dc->Clear(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        app.brush->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f));
        dc->DrawText(text, (UINT32)wcslen(text), app.textFormat,
                     D2D1::RectF(20.0f, 20.0f, (float)width - 20.0f, (float)height), app.brush);
    } else {
        dc->Clear(app.theme.background);
        drawLayoutRange(app, dc.Get(), app.brush, 0.0f, (float)height, 0.0f, 0.0f,
                        D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    const bool drawn = SUCCEEDED(dc->EndDraw());
    dc->SetTarget(previous.Get());
    D2D1_MAPPED_RECT mapped{};
    if (!drawn || FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr)) ||
        FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return pixels;
    }
    for (UINT y = 0; y < height; ++y)
        pixels.insert(pixels.end(), mapped.bits + y * mapped.pitch, mapped.bits + y * mapped.pitch + width * 4);
    readable->Unmap();
    return pixels;
}

// Pixels with a tint: more red than blue, and more blue than red. ClearType
// leaves both kinds at the edges of letters, on the sides the subpixel
// order decides.
struct Tints { size_t red = 0, blue = 0; };
Tints tintsOf(const std::vector<BYTE>& page) {
    Tints t;
    for (size_t i = 0; i + 3 < page.size(); i += 4) {
        const int blue = page[i], red = page[i + 2];
        if (red > blue + 8) ++t.red;
        if (blue > red + 8) ++t.blue;
    }
    return t;
}
// How many pixels of `a` are `b`'s with red and blue changed over
size_t mirrored(const std::vector<BYTE>& a, const std::vector<BYTE>& b) {
    size_t same = 0;
    for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
        if (a[i] == b[i + 2] && a[i + 1] == b[i + 1] && a[i + 2] == b[i]) ++same;
    return same;
}
}  // namespace

int runMonitorTextTests(const char* fixture) {
    namespace fs = std::filesystem;
    auto state = std::make_unique<App>();
    App& app = *state;
    const UINT width = 900, height = 1100;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Monitor text tests", WS_POPUP, 0, 0, (int)width,
                               (int)height, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = (int)width;
    app.height = (int)height;
    updateTextFormats(app);
    createTypography(app);
    applyTheme(app, 0);  // Paper: dark letters on a light page

    // --- This PC's own monitor ------------------------------------------
    stand.real = MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST);
    ComPtr<IDWriteRenderingParams> realParams;
    app.dwriteFactory->CreateMonitorRenderingParams(stand.real, &realParams);
    const Values real = of(realParams.Get());
    check(stand.real && realParams, "the test window is on a monitor whose text settings read");
    check(app.textMonitor == stand.real, "a new window's text is set up for the monitor it is on");
    check(sameScreen(targetValues(app), real) && tintaDraws(targetValues(app)),
          "the target draws with that monitor's gamma, contrast and subpixel order");
    check(app.renderTarget->GetTextAntialiasMode() == D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE,
          "text is drawn with ClearType");
    {
        ComPtr<IDWriteRenderingParams> before, after;
        app.renderTarget->GetTextRenderingParams(&before);
        check(!followMonitorText(app), "a window that stays on its monitor asks for no repaint");
        app.renderTarget->GetTextRenderingParams(&after);
        check(before.Get() == after.Get(), "and its text settings are not made again");
    }

    // The fixture, laid out once: smoothing changes no letter's place
    check(openDocumentInViewer(app, fs::path(fixture).wstring()), "the fixture opens in the viewer");
    layoutDocument(app);
    const size_t runs = app.layoutTextRuns.size();
    const float contentHeight = app.contentHeight;
    check(runs > 40, "the fixture lays out its headings, table, lists, quote and code");
    const std::vector<BYTE> onReal = pageOf(app, width, height);
    check(onReal.size() == (size_t)width * height * 4, "the page draws through the window's context");

    // --- A second monitor, with its subpixels the other way round -------
    MonitorTextSource& source = monitorTextSource();
    const MonitorTextSource own = source;
    source = {standMonitor, standParams};
    stand.on = stand.real;
    check(!followMonitorText(app) && stand.asked == 0, "nothing is read while the monitor is the same");

    stand.on = stand.second;
    check(followMonitorText(app), "a window moved to another monitor wants a repaint");
    Values second;
    second.gamma = stand.gamma;
    second.contrast = stand.contrast;
    second.geometry = (int)stand.geometry;
    check(app.textMonitor == stand.second && sameScreen(targetValues(app), second),
          "there the target draws with the second monitor's values");
    check(tintaDraws(targetValues(app)),
          "at full ClearType level and in the natural symmetric mode, whatever the monitor's own");
    int asked = stand.asked;
    check(!followMonitorText(app) && !followMonitorText(app) && stand.asked == asked,
          "moving about on the second monitor reads nothing more");

    const std::vector<BYTE> onSecond = pageOf(app, width, height);
    check(onSecond.size() == onReal.size() && onSecond != onReal,
          "the page is drawn differently on the second monitor");
    check(app.layoutTextRuns.size() == runs && app.contentHeight == contentHeight,
          "with every run where it was");
    {
        // Subpixels in the order blue, green, red: the tinted edges change
        // sides. Black on white, so that every tint is the smoothing's.
        stand.on = stand.real;
        followMonitorText(app);
        const std::vector<BYTE> sampleReal = pageOf(app, width, 200, true);
        stand.on = stand.second;
        stand.gamma = real.gamma;
        stand.contrast = real.contrast;
        stand.geometry = real.geometry == (int)DWRITE_PIXEL_GEOMETRY_BGR ? DWRITE_PIXEL_GEOMETRY_RGB
                                                                          : DWRITE_PIXEL_GEOMETRY_BGR;
        check(followMonitorText(app), "a second monitor that differs only in its subpixel order wants a repaint");
        const std::vector<BYTE> sampleTurned = pageOf(app, width, 200, true);
        const Tints a = tintsOf(sampleReal), b = tintsOf(sampleTurned);
        check(!sampleReal.empty() && sampleTurned.size() == sampleReal.size() && sampleTurned != sampleReal,
              "the sample is drawn differently under the other subpixel order");
        if (real.geometry == (int)DWRITE_PIXEL_GEOMETRY_FLAT) {
            check(a.red + a.blue < 20, "a monitor with no subpixel order draws letters without tinted edges");
            check(b.red > 300 && b.blue > 300, "and one with an order draws them with");
        } else {
            check(a.red > 300 && a.blue > 300 && b.red > 300 && b.blue > 300,
                  "letters have tinted edges under either subpixel order");
            const size_t pixels = sampleReal.size() / 4;
            check(mirrored(sampleTurned, sampleReal) > pixels * 99 / 100,
                  "with the order turned round, red and blue edges change sides");
        }
    }

    // ClearType's tuner changes the second monitor's values
    stand.gamma = 2.2f;
    stand.contrast = 1.0f;
    asked = stand.asked;
    check(!followMonitorText(app) && stand.asked == asked && targetValues(app).gamma != 2.2f,
          "new values are not looked for on a move within the monitor");
    check(followMonitorText(app, true) && targetValues(app).gamma == 2.2f &&
              targetValues(app).contrast == 1.0f && tintaDraws(targetValues(app)),
          "a settings change takes the monitor's new values");
    asked = stand.asked;
    check(!followMonitorText(app, true) && stand.asked == asked + 1,
          "reading the same values again asks for no repaint");

    // A new target, as after the GPU warm-up, a DPI change or a lost device
    check(createRenderTarget(app), "the render target is made again on the second monitor");
    second.gamma = 2.2f;
    second.contrast = 1.0f;
    second.geometry = (int)stand.geometry;
    check(app.textMonitor == stand.second && sameScreen(targetValues(app), second) &&
              tintaDraws(targetValues(app)) &&
              app.renderTarget->GetTextAntialiasMode() == D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE,
          "a target made again draws for the monitor the window is on, not the primary one");

    // A monitor whose values cannot be read falls back to the primary's
    stand.on = stand.dark;
    followMonitorText(app);
    ComPtr<IDWriteRenderingParams> primaryParams;
    app.dwriteFactory->CreateRenderingParams(&primaryParams);
    check(app.textMonitor == stand.dark && sameScreen(targetValues(app), of(primaryParams.Get())) &&
              tintaDraws(targetValues(app)),
          "a monitor that tells nothing leaves the primary monitor's values");
    stand.on = nullptr;
    followMonitorText(app);
    check(sameScreen(targetValues(app), of(primaryParams.Get())) && tintaDraws(targetValues(app)),
          "and so does a window on no monitor");

    // Back on this PC's monitor the page is drawn as it was at first
    stand.on = stand.real;
    followMonitorText(app);
    check(app.textMonitor == stand.real && sameScreen(targetValues(app), real),
          "back on the first monitor the target has its values again");
    layoutDocument(app);
    check(pageOf(app, width, height) == onReal, "and the page is drawn pixel for pixel as before");

    // Without a target there is nothing to follow
    source = own;
    auto bare = std::make_unique<App>();
    check(!followMonitorText(*bare) && !followMonitorText(*bare, true), "a window without a target is left alone");
    bare.reset();

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Monitor text: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
