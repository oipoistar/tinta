#include "d2d_init.h"
#include "utils.h"

#include <objbase.h>

bool initD2D(App& app) {
    auto t0 = Clock::now();

    // Initialize COM (required for WIC image loading)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &app.d2dFactory);
    if (FAILED(hr)) return false;

    app.metrics.d2dInitUs = usElapsed(t0);
    t0 = Clock::now();

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&app.dwriteFactory));
    if (FAILED(hr)) return false;

    app.metrics.dwriteInitUs = usElapsed(t0);

    // Initialize WIC for image loading
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&app.wicFactory));
    // WIC failure is non-fatal (images just won't render)

    return true;
}

void applyTheme(App& app, int themeIndex) {
    if (themeIndex < 0 || themeIndex >= THEME_COUNT) return;

    app.currentThemeIndex = themeIndex;
    app.theme = THEMES[themeIndex];
    app.darkMode = app.theme.isDark;

    // Update fonts for the new theme
    updateTextFormats(app);

    // Force a redraw
    if (app.hwnd) {
        InvalidateRect(app.hwnd, nullptr, FALSE);
    }
}

void updateTextFormats(App& app) {
    // Release existing formats
    if (app.textFormat) { app.textFormat->Release(); app.textFormat = nullptr; }
    if (app.headingFormat) { app.headingFormat->Release(); app.headingFormat = nullptr; }
    if (app.codeFormat) { app.codeFormat->Release(); app.codeFormat = nullptr; }
    if (app.boldFormat) { app.boldFormat->Release(); app.boldFormat = nullptr; }
    if (app.italicFormat) { app.italicFormat->Release(); app.italicFormat = nullptr; }
    for (auto& fmt : app.headingFormats) {
        if (fmt) { fmt->Release(); fmt = nullptr; }
    }

    // Create text formats with current zoom and theme fonts
    float scale = app.contentScale * app.zoomFactor;
    float fontSize = 16.0f * scale;
    float codeSize = 14.0f * scale;

    const wchar_t* fontFamily = app.theme.fontFamily;
    const wchar_t* codeFont = app.theme.codeFontFamily;

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.textFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        28.0f * scale, L"en-us", &app.headingFormat);

    app.dwriteFactory->CreateTextFormat(codeFont, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        codeSize, L"en-us", &app.codeFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.boldFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.italicFormat);

    // Heading formats by level (use Segoe UI to match previous behavior)
    const wchar_t* headingFont = L"Segoe UI";
    float headingSizes[] = {32, 26, 22, 18, 16, 14};
    for (int i = 0; i < 6; i++) {
        app.dwriteFactory->CreateTextFormat(headingFont, nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            headingSizes[i] * scale, L"en-us", &app.headingFormats[i]);
    }

    // Set consistent baseline alignment for all formats
    if (app.textFormat) app.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.headingFormat) app.headingFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.codeFormat) app.codeFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.boldFormat) app.boldFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.italicFormat) app.italicFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    for (auto* fmt : app.headingFormats) {
        if (fmt) fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    // Cache space widths
    if (app.textFormat) app.spaceWidthText = measureText(app, L" ", app.textFormat);
    if (app.boldFormat) app.spaceWidthBold = measureText(app, L" ", app.boldFormat);
    if (app.italicFormat) app.spaceWidthItalic = measureText(app, L" ", app.italicFormat);
    if (app.codeFormat) app.spaceWidthCode = measureText(app, L" ", app.codeFormat);

    // Build font fallback chain for emoji and CJK support
    if (!app.fontFallback) {
        IDWriteFactory2* factory2 = nullptr;
        if (SUCCEEDED(app.dwriteFactory->QueryInterface(__uuidof(IDWriteFactory2),
                reinterpret_cast<void**>(&factory2)))) {
            IDWriteFontFallbackBuilder* builder = nullptr;
            if (SUCCEEDED(factory2->CreateFontFallbackBuilder(&builder))) {
                // CJK fonts for Japanese/Chinese/Korean characters
                const wchar_t* cjkFamilies[] = {
                    L"Yu Gothic UI", L"Meiryo", L"Microsoft YaHei UI", L"Malgun Gothic"
                };
                DWRITE_UNICODE_RANGE cjkRanges[] = {
                    { 0x2E80, 0x9FFF },    // CJK radicals, kana, ideographs
                    { 0xAC00, 0xD7AF },    // Hangul syllables
                    { 0xF900, 0xFAFF },    // CJK compatibility ideographs
                    { 0xFE30, 0xFE4F },    // CJK compatibility forms
                    { 0x20000, 0x2FA1F },  // CJK extensions B-F
                };
                builder->AddMapping(cjkRanges, 5, cjkFamilies, 4);

                // Emoji/symbol fallback for everything else
                const wchar_t* emojiFamilies[] = {
                    L"Segoe UI Emoji", L"Segoe UI Symbol"
                };
                DWRITE_UNICODE_RANGE fullRange = { 0x0000, 0x10FFFF };
                builder->AddMapping(&fullRange, 1, emojiFamilies, 2);

                builder->CreateFontFallback(&app.fontFallback);
                builder->Release();
            }
            factory2->Release();
        }
    }

    updateOverlayFormats(app);
    app.layoutDirty = true;
}

void updateOverlayFormats(App& app) {
    app.releaseOverlayFormats();

    float scale = app.contentScale;

    // Search overlay format
    app.dwriteFactory->CreateTextFormat(app.theme.fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        16.0f * scale, L"en-us", &app.searchTextFormat);

    // Theme chooser formats
    app.dwriteFactory->CreateTextFormat(L"Segoe UI Light", nullptr,
        DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        28.0f * scale, L"en-us", &app.themeTitleFormat);

    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f * scale, L"en-us", &app.themeHeaderFormat);

    app.themePreviewFormats.resize(THEME_COUNT);
    for (int i = 0; i < THEME_COUNT; i++) {
        const D2DTheme& t = THEMES[i];
        app.dwriteFactory->CreateTextFormat(t.fontFamily, nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            14.0f * scale, L"en-us", &app.themePreviewFormats[i].name);
        app.dwriteFactory->CreateTextFormat(t.fontFamily, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            11.0f * scale, L"en-us", &app.themePreviewFormats[i].preview);
        app.dwriteFactory->CreateTextFormat(t.codeFontFamily, nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            10.0f * scale, L"en-us", &app.themePreviewFormats[i].code);
    }

    // Folder browser format
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f * scale, L"en-us", &app.folderBrowserFormat);

    // TOC formats
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f * scale, L"en-us", &app.tocFormatBold);
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f * scale, L"en-us", &app.tocFormat);

    // Editor text format (monospace, same size as body)
    float editorScale = app.contentScale * app.zoomFactor;
    float editorFontSize = 14.0f * editorScale;
    app.dwriteFactory->CreateTextFormat(app.theme.codeFontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        editorFontSize, L"en-us", &app.editorTextFormat);
    if (app.editorTextFormat) {
        app.editorTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        // Measure actual monospace character width
        IDWriteTextLayout* measureLayout = nullptr;
        app.dwriteFactory->CreateTextLayout(L"M", 1, app.editorTextFormat,
            10000.0f, 100.0f, &measureLayout);
        if (measureLayout) {
            DWRITE_TEXT_METRICS metrics{};
            measureLayout->GetMetrics(&metrics);
            app.editorCharWidth = metrics.widthIncludingTrailingWhitespace;
            measureLayout->Release();
        }
    }
}

void createTypography(App& app) {
    // Release existing typography objects
    if (app.bodyTypography) { app.bodyTypography->Release(); app.bodyTypography = nullptr; }
    if (app.codeTypography) { app.codeTypography->Release(); app.codeTypography = nullptr; }

    // Body typography - standard ligatures, kerning, contextual alternates
    app.dwriteFactory->CreateTypography(&app.bodyTypography);
    if (app.bodyTypography) {
        app.bodyTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES, 1});
        app.bodyTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_KERNING, 1});
        app.bodyTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES, 1});
    }

    // Code typography - programming ligatures (for fonts like Cascadia Code, Fira Code)
    app.dwriteFactory->CreateTypography(&app.codeTypography);
    if (app.codeTypography) {
        app.codeTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES, 1});
        app.codeTypography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_DISCRETIONARY_LIGATURES, 1});
    }
}

bool createRenderTarget(App& app) {
    if (app.renderTarget) {
        app.renderTarget->Release();
        app.renderTarget = nullptr;
    }
    if (app.brush) {
        app.brush->Release();
        app.brush = nullptr;
    }

    // D2D bitmaps are tied to the render target, so invalidate cached images
    for (auto& [key, entry] : app.imageCache) {
        if (entry.bitmap) { entry.bitmap->Release(); entry.bitmap = nullptr; }
        entry.failed = false;  // retry on next layout
    }

    RECT rc;
    GetClientRect(app.hwnd, &rc);

    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rtProps.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    rtProps.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    HRESULT hr = app.d2dFactory->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(app.hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &app.renderTarget
    );
    if (FAILED(hr)) return false;

    hr = app.renderTarget->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &app.brush);
    if (FAILED(hr)) return false;

    // Cache device context for color emoji rendering
    if (app.deviceContext) { app.deviceContext->Release(); app.deviceContext = nullptr; }
    app.renderTarget->QueryInterface(__uuidof(ID2D1DeviceContext),
        reinterpret_cast<void**>(&app.deviceContext));

    // Enable high-quality text
    app.renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    // Create custom rendering params for improved text quality
    IDWriteRenderingParams* defaultParams = nullptr;
    IDWriteRenderingParams* customParams = nullptr;

    app.dwriteFactory->CreateRenderingParams(&defaultParams);
    if (defaultParams) {
        app.dwriteFactory->CreateCustomRenderingParams(
            defaultParams->GetGamma(),
            defaultParams->GetEnhancedContrast(),
            1.0f,  // ClearType level
            defaultParams->GetPixelGeometry(),
            DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
            &customParams
        );
        defaultParams->Release();

        if (customParams) {
            app.renderTarget->SetTextRenderingParams(customParams);
            customParams->Release();
        }
    }

    return true;
}
