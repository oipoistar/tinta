#include "d2d_init.h"
#include "editor.h"
#include "editrail.h"
#include "i18n.h"
#include "input.h"
#include "overlays.h"
#include "startpage.h"
#include "tabs.h"

#include <dwrite_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// Interface text takes the CJK fonts of the interface language (#254).
// With Tinta in Simplified Chinese, menus, settings and tab titles were
// left to the system fallback of an en-us format, which drew them in
// Microsoft JhengHei UI, a Traditional Chinese face.
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

std::wstring familyName(IDWriteFont* font) {
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteLocalizedStrings> names;
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
        ComPtr<IDWriteFont> font;
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

// Wraps the interface fallback and keeps the text every layout sent
// through it, so a surface shows whether its labels took that fallback
class FallbackRecorder : public IDWriteFontFallback {
public:
    explicit FallbackRecorder(IDWriteFontFallback* wrapped) : inner(wrapped) { inner->AddRef(); }
    std::wstring seen;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteFontFallback)) {
            *out = static_cast<IDWriteFontFallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG left = (ULONG)InterlockedDecrement(&refs);
        if (!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE MapCharacters(IDWriteTextAnalysisSource* source,
            UINT32 position, UINT32 length, IDWriteFontCollection* collection,
            const WCHAR* family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
            DWRITE_FONT_STRETCH stretch, UINT32* mappedLength, IDWriteFont** mappedFont,
            FLOAT* scale) override {
        const WCHAR* text = nullptr;
        UINT32 available = 0;
        if (SUCCEEDED(source->GetTextAtPosition(position, &text, &available)) && text) {
            seen.append(text, std::min(available, length));
            seen += L'\n';
        }
        return inner->MapCharacters(source, position, length, collection, family, weight,
                                    style, stretch, mappedLength, mappedFont, scale);
    }

private:
    ~FallbackRecorder() { inner->Release(); }
    IDWriteFontFallback* inner;
    LONG refs = 1;
};

bool installed(IDWriteFontCollection* fonts, const wchar_t* family) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists;
}

// The families a layout of the text draws with: the format's own fallback
// unless another is given
std::wstring fontsOf(App& app, IDWriteFontCollection* fonts, IDWriteTextFormat* format,
                     const std::wstring& text, IDWriteFontFallback* fallback = nullptr) {
    if (!format) return L"(no format)";
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), format,
                                                   2000.0f, 200.0f, &layout)))
        return L"(no layout)";
    if (fallback) {
        ComPtr<IDWriteTextLayout2> layout2;
        if (SUCCEEDED(layout.As(&layout2))) layout2->SetFontFallback(fallback);
    }
    RunFonts runs(fonts);
    layout->Draw(nullptr, &runs, 0, 0);
    return runs.families;
}

// Ideographs, kana and hangul. CJK punctuation is left out: it shares the
// Common script with Latin, so a layout may map it with the Latin before it
bool isCjkLetter(wchar_t c) {
    return (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3040 && c <= 0x30FF) ||
           (c >= 0x3130 && c <= 0x318F) || (c >= 0x3400 && c <= 0x4DBF) ||
           (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) ||
           (c >= 0xF900 && c <= 0xFAFF);
}

// A label reached the interface fallback when every CJK stretch of it did
void expectMapped(const FallbackRecorder& recorder, const std::wstring& label,
                  const std::string& where) {
    std::vector<std::wstring> runs;
    std::wstring run;
    for (wchar_t c : label + L" ") {
        if (isCjkLetter(c)) { run += c; continue; }
        if (!run.empty()) runs.push_back(run);
        run.clear();
    }
    check(!runs.empty(), where + ": the label is translated: " + utf8(label));
    for (const std::wstring& part : runs) {
        check(recorder.seen.find(part) != std::wstring::npos,
              where + " draws " + utf8(label) + " through the interface fallback");
    }
}

// Picks a language the way the Settings dropdown does
void pickLanguage(App& app, int index) {
    app.showSettings = true;
    app.settingsAnimation = 1.0f;
    app.settingsSection = 0;
    app.settingsLangOpen = true;
    const D2D1_RECT_F panel = settingsPanelRect(app);
    app.settingsHits.clear();
    app.settingsHits.push_back({D2D1::RectF(panel.left + 10, panel.top + 10, panel.left + 110,
                                            panel.top + 40),
                                SET_LANG_PICK_BASE + 1 + index});
    const LPARAM point = MAKELPARAM((int)panel.left + 30, (int)panel.top + 20);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, point);
    handleMouseUp(app, app.hwnd, 0, point);
    app.showSettings = false;
}

std::vector<IDWriteTextFormat*> interfaceFormats(App& app) {
    std::vector<IDWriteTextFormat*> formats = {
        app.folderBrowserFormat, app.signalSmallFormat, app.themeTitleFormat,
        app.themeHeaderFormat, app.tocFormat, app.tocFormatBold, app.searchTextFormat,
        app.statsFormat};
    ensureThemePreviewFormats(app);
    for (const auto& preview : app.themePreviewFormats) {
        formats.push_back(preview.name);
        formats.push_back(preview.preview);
    }
    return formats;
}

// Every interface format, and a label laid out in the code font, draws
// the language's ideographs in its own face
void interfaceFont(App& app, IDWriteFontCollection* fonts, const char* id,
                   const std::wstring& text, const std::wstring& expected) {
    const int index = languageIndexById(id);
    check(index >= 0, std::string(id) + " is a known interface language");
    if (index < 0 || !installed(fonts, expected.c_str())) {
        std::cout << "skipped " << id << ": " << utf8(expected) << " is not installed\n";
        return;
    }
    pickLanguage(app, index);
    check(app.currentLanguageIndex == index, std::string("Settings switches to ") + id);
    for (IDWriteTextFormat* format : interfaceFormats(app)) {
        const std::wstring got = fontsOf(app, fonts, format, text);
        check(got == expected, std::string(id) + " interface text uses " + utf8(expected) +
                                   ", not " + utf8(got));
    }
    ComPtr<IDWriteTextLayout> pill;
    app.dwriteFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), app.codeFormat,
                                        2000.0f, 200.0f, &pill);
    useUiFontFallback(app, pill.Get());
    RunFonts runs(fonts);
    if (pill) pill->Draw(nullptr, &runs, 0, 0);
    check(runs.families == expected,
          std::string(id) + " labels in the code font use " + utf8(expected));
}

bool ownFallback(IDWriteTextFormat* format) {
    ComPtr<IDWriteTextFormat1> format1;
    ComPtr<IDWriteFontFallback> fallback;
    return format && SUCCEEDED(format->QueryInterface(IID_PPV_ARGS(&format1))) &&
           SUCCEEDED(format1->GetFontFallback(&fallback)) && fallback;
}

// Other interface languages leave interface text to the system fallback,
// as before
void systemFallback(App& app, const char* id) {
    const int index = languageIndexById(id);
    check(index >= 0, std::string(id) + " is a known interface language");
    if (index < 0) return;
    pickLanguage(app, index);
    check(app.currentLanguageIndex == index, std::string("Settings switches to ") + id);
    check(!app.uiFontFallback, std::string(id) + " builds no interface fallback");
    for (IDWriteTextFormat* format : interfaceFormats(app)) {
        check(!ownFallback(format),
              std::string(id) + " interface formats keep the system fallback");
    }
}

void surfaces(App& app, FallbackRecorder& recorder) {
    auto paint = [&](const char* what, auto&& draw) {
        recorder.seen.clear();
        app.renderTarget->BeginDraw();
        draw();
        check(SUCCEEDED(app.renderTarget->EndDraw()), std::string(what) + " renders");
    };

    for (int section = 0; section < 4; ++section) {
        app.showSettings = true;
        app.settingsAnimation = 1.0f;
        app.settingsSection = section;
        app.settingsLangOpen = false;
        paint("settings", [&] { renderSettingsOverlay(app); });
        expectMapped(recorder, tr(app, "settings.title"), "the settings panel");
        expectMapped(recorder, tr(app, "settings.section.general"), "the settings panel");
    }
    app.showSettings = false;

    openContextMenu(app, 40.0f, 80.0f, true);
    app.contextMenuAnimation = 1.0f;
    paint("context menu", [&] { renderContextMenu(app); });
    int menuLabels = 0;
    for (const ContextMenuEntry& entry : contextMenuEntries(app)) {
        if (!entry.key) continue;
        expectMapped(recorder, tr(app, entry.key), "the context menu");
        ++menuLabels;
    }
    check(menuLabels > 5, "the context menu lists its entries");
    app.showContextMenu = false;

    app.helpAnimation = 1.0f;
    app.showHelp = true;
    paint("help", [&] { renderHelpOverlay(app); });
    expectMapped(recorder, tr(app, "help.title"), "the shortcut list");
    app.showHelp = false;

    paint("start page", [&] { renderStartPage(app); });
    expectMapped(recorder, tr(app, "start.tagline"), "the start page");

    app.tabs.resize(2);
    app.tabs[0].title = L"\u8BFB\u4E66\u7B14\u8BB0.md";  // 读书笔记.md
    app.tabs[1].title = L"\u4F1A\u8BAE\u8BB0\u5F55.md";  // 会议记录.md
    app.activeTab = 0;
    paint("tab strip", [&] { renderTabStrip(app); });
    expectMapped(recorder, app.tabs[0].title, "the tab strip");
    expectMapped(recorder, app.tabs[1].title, "the tab strip");

    app.editMode = true;
    app.editRailAnim = 1.0f;
    app.editRailHover = 1;  // Bold
    app.editCtxOpen = false;
    paint("edit rail", [&] { renderEditRail(app); });
    expectMapped(recorder, tr(app, "rail.bold"), "the edit rail");
    app.editRailHover = 0;

    app.showWordCount = true;
    app.editorText = L"# \u6807\u9898\n\n\u4E2D\u6587 text\n";
    rebuildLineStarts(app);
    app.editorDocCountsStale = true;
    paint("Read button and count", [&] {
        renderEditorReadingButton(app);
        renderEditorWordCount(app);
    });
    expectMapped(recorder, tr(app, "editor.read"), "the Read button");
    expectMapped(recorder, tr(app, "editor.count.words"), "the word count");
    expectMapped(recorder, tr(app, "editor.count.chars"), "the word count");
    app.editMode = false;
}
} // namespace

int runUiFontTests() {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Interface font tests", WS_POPUP, 0, 0, 1050,
                               900, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    app.currentLanguageIndex = languageIndexById("en");
    updateTextFormats(app);
    ComPtr<IDWriteFontCollection> fonts;
    app.dwriteFactory->GetSystemFontCollection(&fonts);

    // Documents keep the Windows order whatever the interface language
    const std::wstring hanzi = L"\u9605\u8BFB\u8BBE\u7F6E\uFF0C";  // 阅读设置，
    IDWriteFontFallback* const documentFallback = app.fontFallback;
    const std::wstring documentFont =
        fontsOf(app, fonts.Get(), app.textFormat, hanzi, app.fontFallback);
    check(!documentFont.empty() && documentFont.find(L'|') == std::wstring::npos,
          "the document draws the sample in one CJK face");

    // The CJK languages take their own face; others keep the system fallback
    systemFallback(app, "en");
    interfaceFont(app, fonts.Get(), "zh", hanzi, L"Microsoft YaHei UI");
    const std::wstring shared = L"\u8A2D\u5B9A";  // 設定, in all four faces
    interfaceFont(app, fonts.Get(), "zh-tw", shared, L"Microsoft JhengHei UI");
    interfaceFont(app, fonts.Get(), "ja", shared, L"Yu Gothic UI");
    interfaceFont(app, fonts.Get(), "ko", shared, L"Malgun Gothic");
    systemFallback(app, "de");
    check(app.fontFallback == documentFallback &&
              fontsOf(app, fonts.Get(), app.textFormat, hanzi, app.fontFallback) == documentFont,
          "switching the interface language leaves documents alone");

    // Only CJK moves: symbols, emoji and other scripts resolve as before
    pickLanguage(app, languageIndexById("zh"));
    ComPtr<IDWriteTextFormat> plain;
    app.dwriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f * app.contentScale,
        L"en-us", &plain);
    for (const wchar_t* sample : {L"Settings", L"\u270E", L"\u25C8", L"\u2318", L"\u2610",
                                  L"\u25A6", L"\u2605", L"\u2713", L"\U0001F600",
                                  L"\u0639\u0631\u0628\u064A", L"\u0E44\u0E17\u0E22",
                                  L"\u05E2\u05D1\u05E8", L"\u0939\u093F\u0928"}) {
        check(fontsOf(app, fonts.Get(), app.folderBrowserFormat, sample) ==
                  fontsOf(app, fonts.Get(), plain.Get(), sample),
              "interface text outside CJK keeps the system font: " + utf8(sample));
    }

    // The surfaces route their labels through the interface fallback
    check(app.uiFontFallback != nullptr, "a Chinese interface builds its fallback");
    if (app.uiFontFallback && installed(fonts.Get(), L"Microsoft YaHei UI")) {
        auto* recorder = new FallbackRecorder(app.uiFontFallback);
        app.uiFontFallback->Release();
        app.uiFontFallback = recorder;  // same language: kept by the rebuild
        recorder->AddRef();
        updateOverlayFormats(app);
        surfaces(app, *recorder);
        pickLanguage(app, languageIndexById("zh"));
        check(app.uiFontFallback == recorder, "picking the same language keeps the fallback");
        recorder->Release();
    }

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Interface fonts: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
