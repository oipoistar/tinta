#include "tabs.h"
#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "input.h"
#include "overlays.h"
#include "settings.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d2d1_1.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <wrl/client.h>

// Tab hover card (#246): a pointer resting on a tab shows the tab's whole
// name and folder below the strip, after a dwell, and a click, key, wheel
// turn or the pointer leaving puts it away.

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
}
void paint(App& app) {
    app.renderTarget->BeginDraw();
    renderTabStrip(app);
    renderTabHoverCard(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "tab strip and hover card render");
}
bool cardShown(const App& app) {
    return app.tabHoverCardRect.right > app.tabHoverCardRect.left &&
           app.tabHoverCardRect.bottom > app.tabHoverCardRect.top;
}
const App::TabHit* hitFor(const App& app, int tab) {
    for (const auto& hit : app.tabHits) if (hit.index == tab) return &hit;
    return nullptr;
}
LPARAM pointIn(const D2D1_RECT_F& r, float fx = 0.5f) {
    return MAKELPARAM((int)(r.left + (r.right - r.left) * fx), (int)((r.top + r.bottom) / 2));
}
// Pointer onto a tab and through the dwell: the card is up afterwards
bool hoverUntilCard(App& app, int tab) {
    const App::TabHit* hit = hitFor(app, tab);
    if (!hit) return false;
    handleMouseMove(app, app.hwnd, pointIn(hit->rect, 0.35f));
    handleTabHoverCardTimer(app, app.hwnd);
    paint(app);
    return cardShown(app);
}
}

// The tab strip as drawn: for one frame the window target's device context
// draws into a bitmap, which is copied to the CPU
bool drawStrip(App& app, std::vector<uint32_t>& pixels, UINT& width, UINT& height) {
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc;
    if (FAILED(app.renderTarget->QueryInterface(IID_PPV_ARGS(&dc)))) return false;
    width = (UINT)app.width;
    height = (UINT)std::ceil(chromeTopHeight(app));
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target, readable;
    if (FAILED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format), &target)) ||
        FAILED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                    format), &readable))) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1Image> previous;
    dc->GetTarget(&previous);
    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(app.theme.background);
    renderTabStrip(app);
    const bool drawn = SUCCEEDED(dc->EndDraw());
    dc->SetTarget(previous.Get());
    D2D1_MAPPED_RECT mapped{};
    if (!drawn || FAILED(readable->CopyFromBitmap(nullptr, target.Get(), nullptr)) ||
        FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
        return false;
    }
    pixels.resize((size_t)width * height);
    for (UINT y = 0; y < height; y++)
        memcpy(&pixels[(size_t)y * width], mapped.bits + (size_t)y * mapped.pitch, width * 4);
    readable->Unmap();
    return true;
}

// #271: what marks the active tab shows on both sides of the first and the
// last tab, as on a tab in the middle. The strip's clip ended exactly at the
// row's ends and cut the tab's halo away on their outer side. The halo is an
// edge now (the tab row's own line), and it is the edge that must not be cut.
void endTabsKeepTheirHalo(App& app) {
    const int savedActive = app.activeTab, savedHovered = app.hoveredTab;
    app.hoveredTab = -1;
    // The strip with a middle tab active: no halo near either end
    std::vector<uint32_t> plain;
    UINT plainWidth = 0, plainHeight = 0;
    app.activeTab = 1;
    check(drawStrip(app, plain, plainWidth, plainHeight), "the tab strip draws into a bitmap");
    for (int end : {0, (int)app.tabs.size() - 1}) {
        app.activeTab = end;
        std::vector<uint32_t> pixels;
        UINT width = 0, height = 0;
        check(drawStrip(app, pixels, width, height), "the tab strip draws into a bitmap");
        const App::TabHit* hit = hitFor(app, end);
        check(hit != nullptr, "the end tab is in view");
        if (!hit || pixels.empty() || plain.size() != pixels.size()) continue;
        // Green channel, averaged over the pixels whose centres lie within
        // 1.5 DIPs inside the tab's left or right edge: where its edge is drawn
        const int row = std::clamp((int)(hit->rect.bottom - dpi(app, 4.0f)), 0, (int)height - 1);
        auto side = [&](const std::vector<uint32_t>& image, bool left) {
            const float edge = left ? hit->rect.left : hit->rect.right;
            int sum = 0, count = 0;
            for (int x = (int)edge - (int)dpi(app, 7.0f); x <= (int)edge + (int)dpi(app, 7.0f); x++) {
                const float distance = left ? edge - (x + 0.5f) : (x + 0.5f) - edge;
                if (x < 0 || x >= (int)width || distance < -dpi(app, 1.5f) || distance > 0.0f)
                    continue;
                sum += (int)((image[(size_t)row * width + x] >> 8) & 0xFF);
                count++;
            }
            return count ? (float)sum / count : -1.0f;
        };
        const float outer = side(pixels, end == 0), inner = side(pixels, end != 0);
        const float base = side(plain, end == 0);
        if (std::abs(outer - inner) > 2.0f || std::abs(outer - base) < 1.0f)
            std::cerr << "  tab " << end << ": outer side " << outer << ", inner side " << inner
                      << ", without the edge " << base << '\n';
        check(std::abs(outer - base) >= 1.0f, "the active end tab's edge shows on its outer side");
        check(std::abs(outer - inner) <= 2.0f,
              "the edge is as strong on the outer side as on the inner side");
    }
    app.activeTab = savedActive;
    app.hoveredTab = savedHovered;
}

// #272: the wheel over the tab strip steps through the tabs and wraps
void wheel(App& app, short delta, bool ctrl = false) {
    BYTE original[256], pressed[256];
    GetKeyboardState(original);
    memcpy(pressed, original, sizeof pressed);
    pressed[VK_CONTROL] = ctrl ? 0x80 : 0;
    SetKeyboardState(pressed);
    handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, delta), 0);
    SetKeyboardState(original);
}

void wheelSwitchesTabs(App& app) {
    const int count = (int)app.tabs.size();
    tabActivate(app, app.hwnd, 0);
    paint(app);
    const App::TabHit* hit = hitFor(app, 1);
    check(hit != nullptr, "a tab to rest the pointer on");
    if (!hit) return;
    // Over a tab, then over the strip's empty stretch beside the + button
    for (float x : {(hit->rect.left + hit->rect.right) * 0.5f, captionIslandLeft(app) - dpi(app, 12.0f)}) {
        app.mouseX = (int)x;
        app.mouseY = (int)(chromeTopHeight(app) * 0.5f);
        app.tabWheelLast = {};
        tabActivate(app, app.hwnd, 0);
        bool order = true;
        for (int step = 1; step <= count; step++) {  // down: right, last wraps to first
            wheel(app, -WHEEL_DELTA);
            order = order && app.activeTab == step % count;
        }
        check(order, "each notch down activates the next tab, and the last wraps to the first");
        wheel(app, WHEEL_DELTA);
        check(app.activeTab == count - 1, "a notch up from the first tab wraps to the last");
        wheel(app, WHEEL_DELTA);
        check(app.activeTab == count - 2, "a notch up activates the previous tab");
    }
    // A touchpad's quarter notches add up to one tab
    tabActivate(app, app.hwnd, 0);
    app.tabWheelLast = {};
    for (int i = 0; i < 3; i++) wheel(app, -WHEEL_DELTA / 4);
    check(app.activeTab == 0, "three quarter notches switch nothing yet");
    wheel(app, -WHEEL_DELTA / 4);
    check(app.activeTab == 1, "the fourth quarter notch moves one tab");
    // A pause starts the count afresh, so stray fractions never add up
    wheel(app, -WHEEL_DELTA / 2);
    Sleep(450);
    wheel(app, -WHEEL_DELTA / 2);
    check(app.activeTab == 1, "half notches on either side of a pause switch nothing");
    // Ctrl+wheel keeps zooming
    const float zoom = app.zoomFactor;
    wheel(app, WHEEL_DELTA, true);
    check(app.activeTab == 1 && app.zoomFactor != zoom, "Ctrl+wheel over the strip zooms instead");
    app.zoomFactor = zoom;
    updateTextFormats(app);
    // Below the strip the wheel scrolls the document as before
    app.mouseY = (int)(chromeTopHeight(app) + dpi(app, 200.0f));
    wheel(app, -WHEEL_DELTA);
    check(app.activeTab == 1, "the wheel over the document leaves the tabs alone");
    // A turn that closes the tab menu does only that
    app.mouseY = (int)(chromeTopHeight(app) * 0.5f);
    openTabMenu(app, 1, (float)app.mouseX, (float)app.mouseY);
    wheel(app, -WHEEL_DELTA);
    check(!app.showTabMenu && app.activeTab == 1, "a turn that closes the tab menu switches no tab");
}

// Left edge of a line laid out in a format, as the TOC and this card lay out
// their rows
float textLeft(App& app, IDWriteTextFormat* format, const wchar_t* text) {
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(app.dwriteFactory->CreateTextLayout(text, (UINT32)wcslen(text), format,
                                                  400.0f, 100.0f, &layout))) {
        return -1.0f;
    }
    DWRITE_HIT_TEST_METRICS metrics{};
    float x = 0.0f, y = 0.0f;
    layout->HitTestTextPosition(0, FALSE, &x, &y, &metrics);
    layout->Release();
    return x;
}

// #270: Help centres its title and footer. It did so by centring the shared
// formats, which the TOC, the search panel and this card also draw with, and
// never set them back: after Help, TOC rows and the card's folder path were
// centred. Help now leaves the shared formats as it found them.
void helpLeavesSharedFormats(App& app) {
    IDWriteTextFormat* shared[] = {app.tocFormat, app.tocFormatBold, app.themeTitleFormat,
                                   app.folderBrowserFormat};
    DWRITE_TEXT_ALIGNMENT before[4];
    for (int i = 0; i < 4; i++) before[i] = shared[i]->GetTextAlignment();
    check(textLeft(app, app.tocFormat, L"Heading") < 1.0f, "TOC rows start at the left before Help");
    app.showHelp = true;
    app.helpAnimation = 1.0f;
    app.renderTarget->BeginDraw();
    renderHelpOverlay(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "Help renders");
    app.showHelp = false;
    bool same = true;
    for (int i = 0; i < 4; i++) same = same && shared[i]->GetTextAlignment() == before[i];
    check(same, "Help leaves the shared formats' alignment as it found it");
    check(textLeft(app, app.tocFormat, L"Heading") < 1.0f,
          "after Help, TOC rows and the card's path still start at the left");
}

int runTabHoverCardTests() {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dir = fs::path(exe).parent_path();
    const auto settings = read(dir / "settings.ini");
    if (!fs::equivalent(dir, fs::current_path()) ||
        (settings != "; Isolated tab hover card test configuration\n" &&
         settings != "; Isolated tab hover card test configuration\r\n")) {
        std::cerr << "Run through CTest's isolated tab hover card wrapper\n";
        return 2;
    }
    check(fs::equivalent(tintaConfigDir(), dir), "all persistence stays in the test directory");

    // Two README.md files in different folders, a name no tab can hold
    // and a Chinese name, from the mixed fixture documents
    const fs::path fixtures = TINTA_TAB_HOVER_FIXTURE_DIR;
    const std::wstring longName =
        L"a-very-long-file-name-that-never-fits-inside-a-tab-and-wraps-onto-a-second-line-in-its-hover-card.md";
    const std::wstring cjkName = L"\u4F1A\u8BAE\u8BB0\u5F55 2026.md";
    fs::create_directories(dir / "notes");
    const fs::path files[] = {dir / L"README.md", dir / L"notes" / L"README.md",
                              dir / longName, dir / cjkName};
    const std::string sources[] = {read(fixtures / "README.md"),
                                   read(fixtures / "notes" / "README.md"),
                                   read(fixtures / longName),
                                   read(fixtures / "README.md")};
    std::vector<std::string> paths;
    for (int i = 0; i < 4; i++) {
        check(!sources[i].empty(), "mixed Markdown fixture loads");
        std::ofstream(files[i], std::ios::binary) << sources[i];
        paths.push_back(toUtf8(files[i].wstring()));
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TintaTabHoverCardTest";
    RegisterClassW(&wc);
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, wc.lpszClassName, L"Tab hover card native tests", WS_POPUP,
        -1200, 100, 1050, 900, nullptr, nullptr, wc.hInstance, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) {
        std::cerr << "Cannot initialize native tab hover card test window\n";
        return 1;
    }
    app.currentFile = paths[0];
    tabsSeedSession(app, paths);
    check(app.tabs.size() == 4 && app.activeTab == 0, "four fixture tabs, the first active");

    // The card's text: whole name, folder, nothing to explain yet
    const std::wstring folders[] = {dir.wstring(), (dir / L"notes").wstring(),
                                    dir.wstring(), dir.wstring()};
    const std::wstring names[] = {L"README.md", L"README.md", longName, cjkName};
    for (int i = 0; i < 4; i++) {
        TabHoverCardText text = tabHoverCardText(app, i);
        check(text.name == names[i], "card names the whole file name");
        check(text.folder == folders[i], "card names the folder the file lives in");
        check(text.status.empty(), "a clean tab has no status line");
    }
    check(tabHoverCardText(app, 0).folder != tabHoverCardText(app, 1).folder,
          "the two README.md tabs differ by folder");

    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (float scale : {1.0f, 1.5f}) {
            app.contentScale = scale;
            updateTextFormats(app);
            app.width = (int)(1050 * scale);
            app.height = (int)(900 * scale);
            helpLeavesSharedFormats(app);
            endTabsKeepTheirHalo(app);
            for (int width : {650, 1050}) {
                app.width = (int)(width * scale);
                app.height = (int)(900 * scale);
                tabHoverCardLeave(app, app.hwnd);
                paint(app);
                check(!cardShown(app), "no card without a hovered tab");
                float readmeHeight = 0.0f;
                for (int tab = 0; tab < 4; tab++) {
                    const App::TabHit* hit = hitFor(app, tab);
                    if (!hit) continue;  // scrolled out of a crowded strip
                    const D2D1_RECT_F tabRect = hit->rect;
                    tabHoverCardLeave(app, app.hwnd);
                    handleMouseMove(app, app.hwnd, pointIn(tabRect, 0.35f));
                    check(app.hoveredTab == tab, "pointer over a tab hovers it");
                    check(app.tabHoverCardPending == tab, "resting on a tab arms the dwell");
                    paint(app);
                    check(!cardShown(app), "no card before the dwell");
                    handleTabHoverCardTimer(app, app.hwnd);
                    check(app.tabHoverCardTab == tab, "the dwell brings the card up");
                    paint(app);
                    check(cardShown(app), "the card draws below the hovered tab");
                    const D2D1_RECT_F card = app.tabHoverCardRect;
                    check(card.top >= chromeTopHeight(app), "the card sits below the strip");
                    check(card.left >= 0.0f && card.right <= (float)app.width,
                          "the card stays inside the window");
                    check(card.left <= tabRect.left + 0.5f ||
                              card.right >= (float)app.width - dpi(app, 8.0f) - 0.5f,
                          "the card starts under its tab unless the window edge pushes it left");
                    check(card.right - card.left <= dpi(app, 420.0f + 24.0f) + 1.0f,
                          "the card keeps to its maximum width");
                    if (tab == 0) readmeHeight = card.bottom - card.top;
                    if (tab == 2) {
                        // The strip cuts the long name short; the card wraps it whole
                        check(hit->labelRect.right - hit->labelRect.left <
                                  measureText(app, longName, app.folderBrowserFormat),
                              "the strip cannot hold the long name");
                        check(card.bottom - card.top > readmeHeight + dpi(app, 8.0f),
                              "the long name wraps onto more lines than README.md");
                    }
                    // Moving within the tab keeps the card
                    handleMouseMove(app, app.hwnd, pointIn(tabRect, 0.45f));
                    paint(app);
                    check(cardShown(app) && app.tabHoverCardTab == tab,
                          "moving within the tab keeps its card");
                }
            }
        }
    }

    app.contentScale = 1.0f;
    app.width = 1050;
    app.height = 900;
    applyTheme(app, 0);
    updateTextFormats(app);
    tabHoverCardLeave(app, app.hwnd);
    paint(app);

    // Gliding to a neighbour swaps cards quickly; the old card goes at once
    check(hoverUntilCard(app, 1), "card up on the notes README");
    handleMouseMove(app, app.hwnd, pointIn(hitFor(app, 2)->rect, 0.35f));
    check(app.tabHoverCardTab == -1 && app.tabHoverCardPending == 2,
          "a new tab hides the old card and waits on the new one");
    check(app.tabHoverCardHiddenAt != 0, "a card that just closed speeds up the next");
    paint(app);
    check(!cardShown(app), "no stale card while the next one waits");
    handleTabHoverCardTimer(app, app.hwnd);
    paint(app);
    check(cardShown(app) && app.tabHoverCardTab == 2, "the neighbour's card comes up");

    // The pointer leaving the window (or slipping into the caption between
    // tabs) takes the card and the tab's hover with it
    tabHoverCardLeave(app, app.hwnd);
    check(app.tabHoverCardTab == -1 && app.tabHoverCardPending == -1 && app.hoveredTab == -1,
          "leaving the client area clears the card and the hover");
    paint(app);
    check(!cardShown(app), "no card once the pointer has left");

    // A key press puts the card away until the pointer moves to another tab
    check(hoverUntilCard(app, 1), "card up before a key press");
    handleKeyDown(app, app.hwnd, VK_F24);
    check(app.tabHoverCardTab == -1 && app.tabHoverCardQuiet == 1, "a key press puts the card away");
    check(app.tabHoverCardHiddenAt == 0, "a dismissal does not hurry the next card");
    handleMouseMove(app, app.hwnd, pointIn(hitFor(app, 1)->rect, 0.6f));
    check(app.tabHoverCardPending == -1, "the same tab stays quiet after a key press");
    handleMouseMove(app, app.hwnd, pointIn(hitFor(app, 2)->rect, 0.35f));
    check(app.tabHoverCardPending == 2, "another tab arms the dwell again");

    // So does the wheel
    check(hoverUntilCard(app, 2), "card up before a wheel turn");
    handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, WHEEL_DELTA), 0);
    check(app.tabHoverCardTab == -1 && app.tabHoverCardQuiet == 2, "a wheel turn puts the card away");

    // A click on the tab puts it away, and the pressed tab stays quiet
    handleMouseMove(app, app.hwnd, pointIn(hitFor(app, 0)->rect, 0.35f));
    check(hoverUntilCard(app, 0), "card up on the active tab before a click");
    const LPARAM press = pointIn(hitFor(app, 0)->rect, 0.35f);
    handleMouseDown(app, app.hwnd, MK_LBUTTON, press);
    check(app.tabHoverCardTab == -1 && app.tabHoverCardPending == -1, "a click puts the card away");
    handleMouseUp(app, app.hwnd, 0, press);
    if (GetCapture() == app.hwnd) ReleaseCapture();
    check(app.tabDragIndex < 0, "the click released its drag arm");
    paint(app);
    handleMouseMove(app, app.hwnd, pointIn(hitFor(app, 0)->rect, 0.5f));
    check(app.tabHoverCardPending == -1 && app.tabHoverCardTab == -1,
          "the clicked tab raises no card while the pointer stays on it");

    // A late dwell after the pointer moved on is ignored
    handleMouseMove(app, app.hwnd, pointIn(hitFor(app, 3)->rect, 0.35f));
    check(app.tabHoverCardPending == 3, "the Chinese-named tab arms the dwell");
    app.hoveredTab = -1;
    handleTabHoverCardTimer(app, app.hwnd);
    check(app.tabHoverCardTab == -1, "a dwell for a tab no longer hovered shows nothing");

    // Menus and dialogs keep the card away; so does a pointer off the tab
    check(hoverUntilCard(app, 3), "card up on the Chinese-named tab");
    app.showTabSwitcher = true;
    paint(app);
    check(!cardShown(app), "no card over the open-files list");
    app.showTabSwitcher = false;
    app.mouseY = (int)chromeTopHeight(app) + 40;
    paint(app);
    check(!cardShown(app), "no card once the pointer is off the tab");

    wheelSwitchesTabs(app);
    tabActivate(app, app.hwnd, 0);
    paint(app);

    // Tabs without a file: a card only when it says something
    App::DocTab untitled;
    untitled.id = 900;
    untitled.title = L"Untitled";
    app.tabs.push_back(untitled);
    tabHoverCardLeave(app, app.hwnd);
    paint(app);
    const int note = (int)app.tabs.size() - 1;
    check(hitFor(app, note) != nullptr, "the untitled tab is on the strip");
    if (hitFor(app, note)) {
        handleMouseMove(app, app.hwnd, pointIn(hitFor(app, note)->rect, 0.35f));
        handleTabHoverCardTimer(app, app.hwnd);
        paint(app);
        check(!cardShown(app), "a clean untitled tab whose title fits has no card");
        app.tabs[note].editMode = true;
        app.tabs[note].editorDirty = true;
        TabHoverCardText text = tabHoverCardText(app, note);
        check(text.folder.empty() && text.dirty &&
                  text.status == tr(app, "tabs.card.unsaved"),
              "unsaved changes explain the orange dot");
        paint(app);
        check(cardShown(app), "an untitled tab with unsaved changes has a card");
    }
    app.tabs[1].fileMissing = true;
    TabHoverCardText gone = tabHoverCardText(app, 1);
    check(!gone.dirty && gone.status == tr(app, "tabs.card.missing"),
          "a file gone from disk explains the red-grey dot");

    tabHoverCardLeave(app, app.hwnd);
    DestroyWindow(app.hwnd);
    if (failures) std::cerr << failures << " tab hover card check(s) failed\n";
    else std::cout << "Tab hover card tests passed\n";
    return failures ? 1 : 0;
}
