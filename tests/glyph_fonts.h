#ifndef TINTA_TESTS_GLYPH_FONTS_H
#define TINTA_TESTS_GLYPH_FONTS_H

#include <dwrite_2.h>
#include <wrl/client.h>

#include <string>

// The faces DirectWrite actually draws a layout's glyphs in, after font
// fallback: interface fonts (#254) and the fonts exports name (#256)
namespace glyphfonts {

inline std::wstring familyName(IDWriteFont* font) {
    Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
    Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(font->GetFontFamily(&family)) || FAILED(family->GetFamilyNames(&names)))
        return L"?";
    UINT32 index = 0, length = 0;
    BOOL exists = FALSE;
    names->FindLocaleName(L"en-us", &index, &exists);
    if (!exists) index = 0;
    names->GetStringLength(index, &length);
    std::wstring name(length + 1, L'\0');
    names->GetString(index, name.data(), length + 1);
    name.resize(length);
    return name;
}

// Collects the family of every glyph run a layout draws
class RunFonts : public IDWriteTextRenderer {
public:
    explicit RunFonts(IDWriteFontCollection* collection) : fonts(collection) {}
    std::wstring families;  // "A" or "A|B" in drawing order

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        *disabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
        *transform = {1, 0, 0, 1, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixels) override {
        *pixels = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT, FLOAT, DWRITE_MEASURING_MODE,
            const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION*,
            IUnknown*) override {
        Microsoft::WRL::ComPtr<IDWriteFont> font;
        std::wstring name = L"?";
        if (SUCCEEDED(fonts->GetFontFromFontFace(run->fontFace, &font)))
            name = familyName(font.Get());
        if (families.empty()) families = name;
        else if (families.find(name) == std::wstring::npos) families += L"|" + name;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*,
                                            IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT,
            const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*,
            BOOL, BOOL, IUnknown*) override { return S_OK; }

private:
    IDWriteFontCollection* fonts;
};

}  // namespace glyphfonts

#endif  // TINTA_TESTS_GLYPH_FONTS_H
