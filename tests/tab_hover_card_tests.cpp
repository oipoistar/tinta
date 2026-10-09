#include "tabs.h"
#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "input.h"
#include "settings.h"
#include "utils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

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
