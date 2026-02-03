#include "d2d_init.h"

bool initD2D(App& app) {
    auto t0 = Clock::now();

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &app.d2dFactory);
    if (FAILED(hr)) return false;

    app.metrics.d2dInitUs = usElapsed(t0);
    t0 = Clock::now();

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&app.dwriteFactory));
    if (FAILED(hr)) return false;

    app.metrics.dwriteInitUs = usElapsed(t0);

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

    // Create text formats with current zoom and theme fonts
    float scale = app.contentScale * app.zoomFactor;
    float fontSize = 16.0f * scale;
    float headingSize = 28.0f * scale;
    float codeSize = 14.0f * scale;

    const wchar_t* fontFamily = app.theme.fontFamily;
    const wchar_t* codeFont = app.theme.codeFontFamily;

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.textFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        headingSize, L"en-us", &app.headingFormat);

    app.dwriteFactory->CreateTextFormat(codeFont, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        codeSize, L"en-us", &app.codeFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.boldFormat);

    app.dwriteFactory->CreateTextFormat(fontFamily, nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &app.italicFormat);

    // Set consistent baseline alignment for all formats
    if (app.textFormat) app.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.headingFormat) app.headingFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.codeFormat) app.codeFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.boldFormat) app.boldFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (app.italicFormat) app.italicFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
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
