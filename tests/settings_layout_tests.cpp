#include "d2d_init.h"
#include "i18n.h"
#include "input.h"
#include "overlays.h"
#include "settings.h"
#include "settings_preview.h"
#include "ui_scale.h"

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// Settings is built from the interface's scale (ui_scale.h): its pages are
// groups of rows under small labels, every gap, row and corner a step of the
// scale, the text in the scale's type roles. Before, every setting was a card
// of its own in one flat list, a title and its description were the same
// size, and a page taller than the window ran out of it.
namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool about(float a, float b) { return std::fabs(a - b) < 0.51f; }
float width(const D2D1_RECT_F& r) { return r.right - r.left; }
float height(const D2D1_RECT_F& r) { return r.bottom - r.top; }
bool inside(const D2D1_RECT_F& in, const D2D1_RECT_F& out) {
    return in.left >= out.left - 0.5f && in.right <= out.right + 0.5f && in.top >= out.top - 0.5f &&
           in.bottom <= out.bottom + 0.5f;
}
bool overlap(const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
    return a.left < b.right - 0.5f && b.left < a.right - 0.5f && a.top < b.bottom - 0.5f &&
           b.top < a.bottom - 0.5f;
}
// A length that is a step of the scale's base of 4
bool onGrid(const App& app, float length) {
    const float units = length / app.contentScale / 4.0f;
    return std::fabs(units - std::round(units)) < 0.02f;
}

LPARAM at(const D2D1_RECT_F& r) {
    return MAKELPARAM((int)((r.left + r.right) / 2), (int)((r.top + r.bottom) / 2));
}
void click(App& app, const D2D1_RECT_F& r) {
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(r));
    handleMouseUp(app, app.hwnd, 0, at(r));
}
const D2D1_RECT_F* hitOf(const App& app, int action) {
    for (const auto& hit : app.settingsHits)
        if (hit.second == action) return &hit.first;
    return nullptr;
}

// The dialog as it draws, in pixels (BGRA), through the window's context
struct Frame {
    UINT width = 0, height = 0;
    std::vector<BYTE> pixels;
    D2D1_COLOR_F at(float x, float y) const {
        const UINT px = std::min(width - 1, (UINT)std::max(0.0f, x));
        const UINT py = std::min(height - 1, (UINT)std::max(0.0f, y));
        const BYTE* p = pixels.data() + ((size_t)py * width + px) * 4;
        return D2D1::ColorF(p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f);
    }
};
Frame draw(App& app) {
    Frame frame;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Bitmap1> target, readable;
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
    const D2D1_SIZE_U size = D2D1::SizeU((UINT)app.width, (UINT)app.height);
    if (FAILED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc))) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format),
                                &target)) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ |
                                                            D2D1_BITMAP_OPTIONS_CANNOT_DRAW, format),
                                &readable))) {
        return frame;
    }
    ComPtr<ID2D1Image> previous;
    dc->GetTarget(&previous);
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(app.theme.background);
    renderSettingsOverlay(app);
    const bool drawn = SUCCEEDED(dc->EndDraw());
    dc->SetTarget(previous.Get());
    D2D1_MAPPED_RECT mapped{};
    if (!drawn || FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr)) ||
        FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return frame;
    }
    frame.width = size.width;
    frame.height = size.height;
    for (UINT y = 0; y < size.height; ++y)
        frame.pixels.insert(frame.pixels.end(), mapped.bits + y * mapped.pitch,
                            mapped.bits + y * mapped.pitch + size.width * 4);
    readable->Unmap();
    return frame;
}

float luminance(D2D1_COLOR_F c) {
    auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}
float contrast(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    const float la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

void open(App& app, int section, int w, int h) {
    app.width = w;
    app.height = h;
    app.showSettings = true;
    app.settingsAnimation = 1.0f;
    app.settingsSection = section;
    app.settingsScroll = 0.0f;
    app.settingsLangOpen = false;
    app.settingsKeysOpen = false;
    app.mouseX = app.mouseY = -100;
}

// The language picked the way a user does: the dropdown, then its row
bool pickLanguage(App& app, const char* id) {
    open(app, 0, 1050, 900);
    draw(app);
    const D2D1_RECT_F* box = hitOf(app, SET_LANG_DROPDOWN);
    if (!box) return false;
    click(app, *box);
    draw(app);
    const D2D1_RECT_F* item = hitOf(app, SET_LANG_PICK_BASE + 1 + languageIndexById(id));
    if (!item) return false;
    click(app, *item);
    return app.currentLanguageIndex == languageIndexById(id);
}

// What every page keeps, whatever the language, the width and the theme
void structure(App& app, const std::string& name, const std::vector<int>& groupRows, bool tallEnough) {
    using namespace uiscale;
    const float s = app.contentScale;
    const D2D1_RECT_F panel = settingsPanelRect(app);
    const auto& rows = app.settingsRows;
    const auto& cards = app.settingsCards;
    const auto& labels = app.settingsGroupLabels;
    size_t wanted = 0;
    for (int n : groupRows) wanted += (size_t)n;
    check(cards.size() == groupRows.size() && labels.size() == groupRows.size() && rows.size() == wanted,
          name + ": the page's settings stand in its groups, each under a label");
    if (cards.size() != groupRows.size() || rows.size() != wanted) return;

    bool stacking = true, heights = true, grid = true, apart = true, room = true;
    size_t at = 0;
    for (size_t g = 0; g < cards.size(); g++) {
        // A group's rows fill its card end to end
        float y = cards[g].top;
        for (int r = 0; r < groupRows[g]; r++, at++) {
            const auto& row = rows[at];
            if (row.group != (int)g || !about(row.row.top, y) || !about(row.row.left, cards[g].left) ||
                !about(row.row.right, cards[g].right)) {
                stacking = false;
            }
            y = row.row.bottom;
            const float h = height(row.row) / s;
            if (!about(h, rowControl) && !about(h, rowTwoLine) && !about(h, rowTwoLine + sp24) &&
                !about(h, rowTwoLine + controlBox + sp8)) {
                heights = false;
            }
            if (!onGrid(app, row.row.top - cards[0].top) || !onGrid(app, height(row.row)) ||
                !onGrid(app, row.row.left - panel.left)) {
                grid = false;
            }
            // The label keeps its room, and the control stands clear of it
            const bool hasText = width(row.text) > 0.0f;
            if (hasText && !row.stacked && about(h, rowTwoLine) && row.control.left - row.text.right < sp16 * s - 0.5f)
                apart = false;
            if (hasText && row.stacked && row.control.top < row.text.bottom) apart = false;
            if (hasText && width(row.text) < 120.0f * s - 0.5f) room = false;
            if (!inside(row.control, row.row)) apart = false;
        }
        if (!about(y, cards[g].bottom)) stacking = false;
        // The label sits a step above its card, a group a section's gap under the last
        if (!about(cards[g].top - labels[g].top, (16.0f + sp8) * s)) grid = false;
        if (g && !about(labels[g].top - cards[g - 1].bottom, sp24 * s)) grid = false;
    }
    check(stacking, name + ": a group's rows fill its card one under the other");
    check(heights, name + ": every row is one of the scale's four heights");
    check(grid, name + ": rows, cards and labels stand on the scale's base of 4, groups a section's gap apart");
    check(apart, name + ": a control stands clear of its row's label and inside its row");
    check(room, name + ": a label keeps at least 120 units beside its control");

    // Everything that can be clicked is in the dialog, and no two overlap
    bool reachable = true, distinct = true;
    for (size_t i = 0; i < app.settingsHits.size(); i++) {
        if (!inside(app.settingsHits[i].first, panel)) reachable = false;
        for (size_t j = i + 1; j < app.settingsHits.size(); j++)
            if (overlap(app.settingsHits[i].first, app.settingsHits[j].first)) distinct = false;
    }
    check(reachable, name + ": everything that can be clicked is inside the dialog");
    check(distinct, name + ": no two controls share a place");

    if (tallEnough) {
        check(app.settingsScrollMax == 0.0f && about(cards.back().bottom, app.settingsViewport.bottom) &&
                  about(cards.front().top - labels.front().top, (16.0f + sp8) * s) &&
                  about(panel.bottom - cards.back().bottom, (sp16 + 16.0f + sp24) * s),
              name + ": the dialog is as tall as its page, down to the footer");
    }
}
}  // namespace

int runSettingsLayoutTests(const char* fixture) {
    namespace fs = std::filesystem;
    using namespace uiscale;
    const auto dir = fs::current_path();
    fs::create_directories(dir / "frames");
    loadLanguageOverrides();  // German, French, Italian and Russian
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Settings layout tests", WS_POPUP, 0, 0, 1050, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    applyTheme(app, 0);
    check(openDocumentInViewer(app, fs::path(fixture).wstring()), "the fixture opens behind the dialog");
    const float s = app.contentScale;

    // --- The scale itself -----------------------------------------------
    {
        const Type roles[] = {Type::Micro, Type::Small, Type::Body, Type::Strong, Type::Subtitle, Type::Title};
        const float sizes[] = {11, 12, 13, 13, 16, 20}, lines[] = {14, 16, 18, 18, 22, 28};
        bool made = true, steps = true;
        for (int i = 0; i < 6; i++) {
            IDWriteTextFormat* fmt = font(app, roles[i]);
            if (!fmt || !about(fmt->GetFontSize(), sizes[i] * s) || fontSize(roles[i]) != sizes[i] ||
                lineHeight(roles[i]) != lines[i] ||
                fmt->GetFontWeight() != (semibold(roles[i]) ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL)) {
                made = false;
            }
            if (fontSize(roles[i]) < 11.0f || (i && fontSize(roles[i]) < fontSize(roles[i - 1]))) steps = false;
        }
        check(made, "the six type roles are Segoe UI at 11, 12, 13, 13, 16 and 20 in two weights");
        check(steps && fontSize(Type::Body) == fontSize(Type::Strong) && !semibold(Type::Body) &&
                  semibold(Type::Strong),
              "no role is smaller than 11, and Body and Strong differ in weight alone");
        check(font(app, Type::Body) == font(app, Type::Body) && font(app, Type::Body) != font(app, Type::Strong),
              "a role's format is made once");
        const float spaces[] = {sp2, sp4, sp8, sp12, sp16, sp24, sp32, sp48};
        bool base = true;
        for (float v : spaces)
            if (v != sp2 && std::fmod(v, 4.0f) != 0.0f) base = false;
        check(base && std::fmod(rowList, 4.0f) == 0 && std::fmod(rowMenu, 4.0f) == 0 &&
                  std::fmod(rowControl, 4.0f) == 0 && std::fmod(rowTwoLine, 4.0f) == 0 &&
                  rowTwoLine == sp8 + lineHeight(Type::Body) + sp2 + lineHeight(Type::Small) + sp8,
              "spacings and rows are steps of 4, and a two-line row is its lines and paddings");
    }

    // --- Every page, in every language, at two widths, light and dark ---
    const std::vector<int> pages[3] = {{2, 5, 1}, {4, 2, 2}, {7, 2}};  // #323's row in General, #322's in Editor
    const char* languages[] = {"en", "de", "fr", "it", "ru", "zh", "ja", "ko"};
    int trimmedWide = 0, trimmedNarrow = 0;
    for (const char* language : languages) {
        check(pickLanguage(app, language), std::string("the dropdown picks ") + language);
        for (int theme : {0, 5}) {
            applyTheme(app, theme);
            for (int w : {1050, 650}) {
                for (int section = 0; section < 3; section++) {
                    open(app, section, w, 900);
                    const std::string name = std::string(language) + " page " + std::to_string(section) + " at " +
                                             std::to_string(w) + (theme ? " dark" : " light");
                    check(!draw(app).pixels.empty(), name + ": the dialog draws");
                    structure(app, name, pages[section], true);
                    for (const auto& row : app.settingsRows) (w == 1050 ? trimmedWide : trimmedNarrow) += row.titleTrimmed;
                    if (theme == 0 || std::string(language) == "en") {
                        saveSettingsPreview(app, dir / "frames" /
                            (std::string(language) + "-" + std::to_string(section) + "-" + std::to_string(w) +
                             (theme ? "-dark" : "") + ".png"));
                    }
                }
            }
        }
    }
    check(trimmedWide == 0, "no setting's name is cut short at 1050 in any of the eight languages");
    check(trimmedNarrow == 0, "nor at 650, where the sections go across the top and the rows take the width");
    // The dialog's text takes the interface language's CJK faces, also in
    // formats made before the language was picked (#254)
    {
        auto fallbackOf = [&](Type role) {
            ComPtr<IDWriteTextFormat1> format1;
            ComPtr<IDWriteFontFallback> fallback;
            if (SUCCEEDED(font(app, role)->QueryInterface(IID_PPV_ARGS(&format1)))) format1->GetFontFallback(&fallback);
            return fallback;
        };
        pickLanguage(app, "zh");
        draw(app);
        check(app.uiFontFallback && fallbackOf(Type::Body).Get() == app.uiFontFallback &&
                  fallbackOf(Type::Small).Get() == app.uiFontFallback,
              "in Chinese the dialog's text falls back to the Chinese faces");
        pickLanguage(app, "ja");
        draw(app);
        check(app.uiFontFallback && fallbackOf(Type::Body).Get() == app.uiFontFallback &&
                  fallbackOf(Type::Micro).Get() == app.uiFontFallback,
              "picked Japanese, the same formats fall back to the Japanese ones");
        pickLanguage(app, "de");
        draw(app);
        check(!app.uiFontFallback && !fallbackOf(Type::Body) && !fallbackOf(Type::Title),
              "and in German to the system's again");
    }
    check(pickLanguage(app, "en"), "back to English");
    applyTheme(app, 0);

    // --- Hierarchy: name, description and group label are three levels ---
    {
        open(app, 1, 1050, 900);
        draw(app);
        const D2D1_RECT_F panel = settingsPanelRect(app);
        check(about(width(panel), 760.0f * s) && about(app.settingsRows.front().row.left - panel.left, (sp24 + 140.0f + sp24) * s),
              "at 1050 the sections are a rail at the left, a dialog's padding from the page");
        check(about(app.settingsGroupLabels[0].left, app.settingsRows[0].text.left),
              "group labels and the rows' names share one left edge");
        // The two reading widths, the heading lines and folding are one group; themes are another
        check(app.settingsRows[0].group == 0 && app.settingsRows[3].group == 0 && app.settingsRows[4].group == 1 &&
                  app.settingsRows[5].group == 1 && app.settingsRows[6].group == 2,
              "Follow Windows theme and Themes are neighbours in a group of their own");
        const D2D1_RECT_F* follow = hitOf(app, SET_TOGGLE_FOLLOW);
        const D2D1_RECT_F* newTheme = hitOf(app, SET_NEW_THEME);
        check(follow && newTheme && inside(*follow, app.settingsRows[4].row) && inside(*newTheme, app.settingsRows[5].row),
              "each in its own row");
        check(about(height(app.settingsRows[0].row), (rowTwoLine + sp24) * s) &&
                  about(height(app.settingsRows[2].row), rowTwoLine * s),
              "a slider's row is a two-line row and the slider's line");
        // Before: eight cards of 58 or 84 with 10 between, 718 in all
        check(height(panel) / s <= 718.0f, "the Appearance page is no taller than it was as eight cards");
    }

    // --- Ink: what explains a setting still reads at 4.5 to 1 ------------
    {
        bool reads = true, lifted = true;
        std::string worst;
        for (int theme = 0; theme < 10; theme++) {
            applyTheme(app, theme);
            open(app, 2, 1050, 900);
            const Frame frame = draw(app);
            const auto& row = app.settingsRows.front().row;
            // The card's ground, where nothing is drawn: left of the row's text
            const D2D1_COLOR_F ground = frame.at(row.left + 6.0f * s, row.top + 26.0f * s);
            const D2D1_COLOR_F text = themeUiText(app.theme);
            const D2D1_COLOR_F ink = D2D1::ColorF(ground.r + (text.r - ground.r) * inkSecondary,
                                                  ground.g + (text.g - ground.g) * inkSecondary,
                                                  ground.b + (text.b - ground.b) * inkSecondary);
            if (contrast(ink, ground) < 4.5f) {
                reads = false;
                worst += " " + std::to_string(theme) + ":" + std::to_string(contrast(ink, ground));
            }
            const D2D1_RECT_F panel = settingsPanelRect(app);
            const D2D1_COLOR_F panelGround = frame.at(panel.left + 8.0f * s, panel.bottom - 8.0f * s);
            if (contrast(ground, panelGround) < 1.01f) lifted = false;
        }
        check(reads, "descriptions and group labels read at 4.5 to 1 on a card in all ten built-in themes" + worst);
        check(lifted, "a group's card stands out from the dialog in all ten");
        applyTheme(app, 0);
    }

    // --- A window too short: the page scrolls between header and footer --
    {
        open(app, 1, 1050, 560);
        draw(app);
        const D2D1_RECT_F panel = settingsPanelRect(app);
        const D2D1_RECT_F view = app.settingsViewport;
        check(about(height(panel), 560.0f * s - 64.0f * s) && app.settingsScrollMax > 100.0f * s,
              "in a short window the dialog keeps to the window and its page has more than fits");
        bool within = true;
        for (const auto& hit : app.settingsHits) {
            const bool pageControl = hit.second >= SET_TOGGLE_FOLLOW && hit.second != SET_CLOSE &&
                                     hit.first.left >= view.left - 12.0f * s;
            if (pageControl && (hit.first.top < view.top - 0.5f || hit.first.bottom > view.bottom + 0.5f)) within = false;
        }
        check(within, "nothing can be clicked outside the page's room");
        check(!hitOf(app, SET_TOGGLE_NOTIFICATIONS), "the last setting is out of reach before scrolling");
        check(hitOf(app, SET_CLOSE) && hitOf(app, SET_SECTION_EDITOR), "the header and the sections stay in reach");
        const Frame top = draw(app);

        // The wheel, through the app's own handler
        const LPARAM over = MAKELPARAM((int)(view.left + 40), (int)(view.top + 40));
        handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), over);
        check(about(app.settingsScroll, rowTwoLine * s), "a notch of the wheel moves the page by a row");
        draw(app);
        check(about(app.settingsRows.front().row.top, view.top + (16.0f + sp8) * s - rowTwoLine * s),
              "and the rows move with it");
        for (int i = 0; i < 40; i++) handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), over);
        const Frame bottom = draw(app);
        check(about(app.settingsScroll, app.settingsScrollMax) && about(app.settingsCards.back().bottom, view.bottom),
              "the page stops with its last row at the foot of its room");
        check(top.pixels != bottom.pixels, "the scrolled page is drawn");
        // Header and footer are not scrolled over
        bool headerClean = true;
        for (float x = view.left; x < view.right; x += 7.0f)
            for (float y = panel.top + 2.0f; y < view.top - 1.0f; y += 3.0f)
                if (top.at(x, y).r != bottom.at(x, y).r || top.at(x, y).g != bottom.at(x, y).g) headerClean = false;
        check(headerClean, "the scrolled rows are clipped under the header");
        const D2D1_RECT_F* notifications = hitOf(app, SET_TOGGLE_NOTIFICATIONS);
        const bool before = app.showNotifications;
        check(notifications != nullptr, "scrolled to the end, the last setting is in reach");
        if (notifications) click(app, *notifications);
        check(app.showNotifications != before, "and its switch works");
        if (notifications && app.showNotifications != before) {
            draw(app);
            if (const D2D1_RECT_F* again = hitOf(app, SET_TOGGLE_NOTIFICATIONS)) click(app, *again);
        }
        check(!hitOf(app, SET_SLIDER_READING), "the first setting scrolled out of reach");
        saveSettingsPreview(app, dir / "frames" / "short-scrolled.png");
        for (int i = 0; i < 40; i++) handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)WHEEL_DELTA), over);
        check(app.settingsScroll == 0.0f, "the wheel the other way stops at the top");

        // The window made taller under a scrolled page: the page is back in its room
        for (int i = 0; i < 40; i++) handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), over);
        app.height = 900;
        draw(app);
        check(app.settingsScroll == 0.0f && app.settingsScrollMax == 0.0f &&
                  about(app.settingsRows.front().row.top, app.settingsViewport.top + (16.0f + sp8) * s),
              "a window grown under a scrolled page shows the page from its top");
        app.height = 560;
        draw(app);

        // Another section starts at its top
        handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), over);
        draw(app);
        if (const D2D1_RECT_F* editor = hitOf(app, SET_SECTION_EDITOR)) click(app, *editor);
        check(app.settingsSection == 2 && app.settingsScroll == 0.0f, "another section opens at its top");
        // A page that fits does not scroll
        open(app, 0, 1050, 900);
        draw(app);
        handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), over);
        check(app.settingsScrollMax == 0.0f && app.settingsScroll == 0.0f, "a page that fits does not move");
    }

    // --- Narrow: sections across the top, a wide control under its label -
    {
        open(app, 1, 650, 900);
        draw(app);
        const D2D1_RECT_F panel = settingsPanelRect(app);
        const D2D1_RECT_F* general = hitOf(app, SET_SECTION_GENERAL);
        const D2D1_RECT_F* front = hitOf(app, SET_SECTION_FRONTMATTER);
        check(general && front && about(general->top, front->top) && about(height(*general), rowMenu * s) &&
                  about(width(app.settingsCards.front()), width(panel) - sp24 * 2 * s),
              "at 650 the sections go across the top and the cards take the dialog's width");
        bool none = true;
        for (const auto& row : app.settingsRows) none = none && !row.stacked;
        check(none, "and every control still fits beside its label");

        open(app, 1, 430, 1100);
        draw(app);
        structure(app, "at 430", pages[1], false);
        const auto& themes = app.settingsRows[5];
        check(themes.stacked && about(height(themes.row), (rowTwoLine + controlBox + sp8) * s) &&
                  themes.control.top >= themes.text.bottom && about(themes.control.left, themes.text.left),
              "at 430 the four theme buttons go under their label, on its left edge");
        const D2D1_RECT_F* newTheme = hitOf(app, SET_NEW_THEME);
        check(newTheme && inside(*newTheme, themes.row), "and stay inside their row");
        check(!app.settingsRows[4].stacked && !app.settingsRows[2].stacked, "a switch stays beside its label");
        saveSettingsPreview(app, dir / "frames" / "narrow-430.png");
    }

    // --- The dropdowns' lists are lists of the scale ----------------------
    {
        open(app, 0, 1050, 900);
        draw(app);
        const D2D1_RECT_F panel = settingsPanelRect(app);
        const D2D1_RECT_F* box = hitOf(app, SET_LANG_DROPDOWN);
        check(box && about(height(*box), controlBox * s), "a dropdown's box is a control's height");
        if (box) click(app, *box);
        draw(app);
        int items = 0;
        bool rowsOk = true;
        for (const auto& hit : app.settingsHits) {
            if (hit.second < SET_LANG_PICK_BASE || hit.second >= SET_KEYS_PICK_BASE) continue;
            ++items;
            if (!about(height(hit.first), rowList * s) || !inside(hit.first, panel)) rowsOk = false;
        }
        check(items >= 9 && rowsOk, "the language list has a list's rows, inside the dialog");
        saveSettingsPreview(app, dir / "frames" / "language-list.png");
        // The list lies over the row under it: its own box closes it
        if (const D2D1_RECT_F* same = hitOf(app, SET_LANG_DROPDOWN)) click(app, *same);
        draw(app);
        check(!app.settingsLangOpen && !hitOf(app, SET_LANG_PICK_BASE), "the dropdown's box closes its list again");
        const D2D1_RECT_F* keys = hitOf(app, SET_KEYS_DROPDOWN);
        if (keys) click(app, *keys);
        draw(app);
        check(app.settingsKeysOpen && !app.settingsLangOpen && hitOf(app, SET_KEYS_PICK_BASE + 1),
              "the other dropdown opens its own");
        if (const D2D1_RECT_F* vim = hitOf(app, SET_KEYS_PICK_BASE + 1)) click(app, *vim);
        check(app.keyProfile == "vim" && !app.settingsKeysOpen, "and its row picks the profile");
        draw(app);
        if (const D2D1_RECT_F* again = hitOf(app, SET_KEYS_DROPDOWN)) click(app, *again);
        draw(app);
        if (const D2D1_RECT_F* windows = hitOf(app, SET_KEYS_PICK_BASE)) click(app, *windows);
    }

    // --- The front matter page keeps its own layout in the new frame -----
    {
        open(app, 3, 1050, 900);
        check(!draw(app).pixels.empty() && app.settingsRows.empty() && hitOf(app, SET_CLOSE) &&
                  hitOf(app, SET_SECTION_GENERAL),
              "the front matter page draws in the dialog's frame");
        handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)-WHEEL_DELTA), MAKELPARAM(500, 400));
        check(app.settingsScroll == 0.0f, "and the wheel there is its list's");
        saveSettingsPreview(app, dir / "frames" / "frontmatter.png");
    }

    // How long a frame of the dialog takes (printed, not checked)
    {
        open(app, 1, 1050, 900);
        draw(app);
        LARGE_INTEGER freq, t0, t1;
        QueryPerformanceFrequency(&freq);
        ComPtr<ID2D1DeviceContext> dc;
        app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc));
        ComPtr<ID2D1Bitmap1> target;
        dc->CreateBitmap(D2D1::SizeU(1050, 900), nullptr, 0,
                         D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                                                 D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
                         &target);
        ComPtr<ID2D1Image> previous;
        dc->GetTarget(&previous);
        dc->SetTarget(target.Get());
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < 200; i++) {
            dc->BeginDraw();
            renderSettingsOverlay(app);
            dc->EndDraw();
        }
        QueryPerformanceCounter(&t1);
        dc->SetTarget(previous.Get());
        std::cout << "Settings frame: " << (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart / 200.0
                  << " ms" << std::endl;
        QueryPerformanceCounter(&t0);
        float sum = 0.0f;
        for (int i = 0; i < 200; i++) sum += settingsPanelRect(app).bottom;
        QueryPerformanceCounter(&t1);
        std::cout << "Settings panel rect: " << (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart / 200.0
                  << " ms" << (sum > 0 ? "" : " ") << std::endl;
    }

    app.showSettings = false;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Settings layout: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
