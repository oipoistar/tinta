#include "d2d_init.h"
#include "i18n.h"
#include "input.h"
#include "overlays.h"
#include "render.h"
#include "settings.h"
#include "startpage.h"
#include "editrail.h"
#include "tabs.h"
#include "ui_frame.h"
#include "ui_scale.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace uiframe;

// The interface's surfaces on its scale (ui_scale.h), one after the other
// as they are moved onto it. Each is drawn through the window's own context
// and checked where its parts went.
namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

namespace fs = std::filesystem;
fs::path g_frames;
void shot(App& app, const Frame& frame, const char* name) {
    save(app, frame, (g_frames / (std::string(name) + ".png")).wstring());
}

void wheel(App& app, int notches, float x, float y) {
    handleMouseWheel(app, app.hwnd, MAKEWPARAM(0, (WORD)(short)(notches * WHEEL_DELTA)),
                     MAKELPARAM((int)x, (int)y));
}
void moveTo(App& app, float x, float y) {
    app.mouseX = (int)x;
    app.mouseY = (int)y;
    handleMouseMove(app, app.hwnd, MAKELPARAM((int)x, (int)y));
}
void click(App& app, const D2D1_RECT_F& r) {
    handleMouseDown(app, app.hwnd, MK_LBUTTON, centre(r));
    handleMouseUp(app, app.hwnd, 0, centre(r));
}

// --- The theme chooser ----------------------------------------------------
//
// Its cards shrank to fit: with 18 light themes in an 800-high window a card
// was 26 high and each name lay over its sample. A card has one size now and
// the columns scroll.

const D2D1_RECT_F* chooserHit(const App& app, int id) {
    for (const auto& hit : app.themeChooserHits)
        if (hit.second == id) return &hit.first;
    return nullptr;
}
Frame chooser(App& app, int w, int h, bool opening = false) {
    app.width = w;
    app.height = h;
    app.showThemeChooser = true;
    app.themeChooserAnimation = opening ? 0.0f : 1.0f;
    Frame frame = draw(app, [&] { renderThemeChooser(app); });
    if (opening) {
        // The opening frames run to their end
        for (int i = 0; i < 12 && app.themeChooserAnimation < 1.0f; i++) frame = draw(app, [&] { renderThemeChooser(app); });
    }
    return frame;
}

// What the chooser keeps, however many themes there are
void chooserStructure(App& app, const std::string& name) {
    using namespace uiscale;
    const float s = app.contentScale;
    const D2D1_RECT_F panel = app.themeChooserPanel, view = app.themeChooserViewport;
    bool sized = true, placed = true, inPanel = true, apart = true;
    std::vector<D2D1_RECT_F> cards;
    for (const auto& hit : app.themeChooserHits) {
        if (!inside(hit.first, panel)) inPanel = false;
        if (hit.second < 0) continue;
        cards.push_back(hit.first);
        // A card is 72 high unless the room's edge cuts it
        const bool cut = about(hit.first.top, view.top) || about(hit.first.bottom, view.bottom);
        if (!about(height(hit.first), 72.0f * s) && !cut) sized = false;
        if (height(hit.first) > 72.0f * s + 0.5f) sized = false;
        if (!inside(hit.first, view)) placed = false;
        const float col = hit.first.left - panel.left;
        if (!about(col, sp24 * s) && !about(col, sp24 * s + width(hit.first) + sp16 * s)) placed = false;
    }
    for (size_t i = 0; i < cards.size(); i++)
        for (size_t j = i + 1; j < cards.size(); j++) {
            if (overlap(cards[i], cards[j])) apart = false;
            // Cards in a column are a card and a gap apart
            if (about(cards[i].left, cards[j].left) && std::fabs(cards[i].top - cards[j].top) < 72.0f * s + 7.5f * s &&
                !about(cards[i].top, view.top) && !about(cards[j].top, view.top)) {
                apart = false;
            }
        }
    check(!cards.empty() && sized, name + ": every card is 72 high, however many themes there are");
    check(placed, name + ": the cards stand in two columns inside their room, a dialog's padding from its edge");
    check(apart, name + ": no card lies over another, and a column's cards are a gap apart");
    check(inPanel && chooserHit(app, -1) && chooserHit(app, -2) && chooserHit(app, -3),
          name + ": the switch, the close cross and the edit entry are in the dialog");
    check(about(view.top - panel.top, (sp24 + 28.0f + sp16 + 16.0f + sp8) * s) &&
              about(panel.bottom - view.bottom, (sp16 + 16.0f + sp24) * s) && onGrid(app, height(panel)),
          name + ": the header, the columns' labels and the footer take the scale's steps");
    check(height(panel) <= app.height - 64.0f * s + 0.5f || height(view) <= 72.0f * 1.5f * s + 0.5f,
          name + ": the dialog keeps to the window");
}

void themeChooser(App& app) {
    using namespace uiscale;
    const float s = app.contentScale;

    // The ten built-in themes: five and five, nothing to scroll
    applyTheme(app, 0);
    Frame builtin = chooser(app, 1050, 800, true);
    check(themeCount() == 10 && themeChooserRows() == 5, "ten built-in themes are five rows of cards");
    chooserStructure(app, "built-in themes");
    check(app.themeChooserScrollMax == 0.0f && app.themeChooserScroll == 0.0f &&
              about(height(app.themeChooserPanel), (sp24 * 2 + 44.0f + 24.0f + 5 * 72.0f + 4 * sp8 + 32.0f) * s),
          "with ten themes the dialog is as tall as its five rows and does not scroll");
    int shown = 0;
    for (const auto& hit : app.themeChooserHits) shown += hit.second >= 0;
    check(shown == 10, "all ten cards are in reach");
    shot(app, builtin, "chooser-builtin");
    wheel(app, -1, 500, 400);
    check(app.themeChooserScroll == 0.0f, "the wheel moves nothing when all cards fit");
    // The same ten in a short window scroll
    chooser(app, 1050, 460);
    chooserStructure(app, "built-in themes in a short window");
    check(app.themeChooserScrollMax > 0.0f, "in a short window the ten cards scroll");

    // Eighteen more themes: twenty light ones in one column
    std::error_code ec;
    fs::copy_file(g_frames.parent_path() / "fixture-themes.ini", g_frames.parent_path() / "themes.ini",
                  fs::copy_options::overwrite_existing, ec);
    loadCustomThemes();
    updateOverlayFormats(app);  // the cards' specimens are made anew
    check(themeCount() == 28 && themeChooserRows() == 20, "the fixture's themes make twenty rows of light cards");
    applyTheme(app, 0);
    Frame many = chooser(app, 1050, 800, true);
    chooserStructure(app, "28 themes");
    const D2D1_RECT_F view = app.themeChooserViewport;
    check(app.themeChooserScrollMax > 500.0f * s && app.themeChooserScroll == 0.0f,
          "with twenty rows the columns have more than fits, and open at the top with the first theme in use");
    check(chooserHit(app, 0) && about(height(*chooserHit(app, 0)), 72.0f * s) && !chooserHit(app, themeCount() - 4),
          "the first card is whole and the last light one out of reach");
    shot(app, many, "chooser-many");

    // The wheel, through the app's handler
    wheel(app, -1, view.left + 40, view.top + 40);
    check(about(app.themeChooserScroll, 80.0f * s), "a notch of the wheel moves the columns by a card and its gap");
    Frame moved = chooser(app, 1050, 800);
    check(!chooserHit(app, 0) && many.differs(moved, view), "the first card scrolled out of reach and the cards moved");
    const D2D1_RECT_F panel = app.themeChooserPanel;
    check(!many.differs(moved, D2D1::RectF(panel.left, panel.top, panel.right, view.top - 6.0f * s)) &&
              !many.differs(moved, D2D1::RectF(panel.left, view.bottom + 6.0f * s, view.right, panel.bottom)),
          "the header, the columns' labels and the footer stay where they are");
    for (int i = 0; i < 60; i++) wheel(app, -1, view.left + 40, view.top + 40);
    Frame end = chooser(app, 1050, 800);
    chooserStructure(app, "28 themes scrolled to the end");
    int lastLight = -1;
    for (int i = 0; i < themeCount(); i++)
        if (!themeAt(i).isDark) lastLight = i;
    const D2D1_RECT_F* last = chooserHit(app, lastLight);
    check(about(app.themeChooserScroll, app.themeChooserScrollMax) && last && about(last->bottom, view.bottom) &&
              about(height(*last), 72.0f * s),
          "the columns stop with the last card whole at the foot of their room");
    const D2D1_RECT_F* lastDark = chooserHit(app, themeCount() - 1);
    check(lastDark && about(lastDark->bottom, view.bottom) && about(height(*lastDark), 72.0f * s),
          "the shorter dark column stopped with its own last card there: no half of the dialog is empty");
    shot(app, end, "chooser-scrolled");

    // Resting on a card shows its theme behind; the wheel takes that back
    if (last) {
        moveTo(app, (last->left + last->right) / 2, (last->top + last->bottom) / 2);
        check(app.themeChooserPreviewBase == 0 && app.currentThemeIndex == lastLight,
              "resting on a card shows its theme behind the dialog");
        wheel(app, 1, (last->left + last->right) / 2, (last->top + last->bottom) / 2);
        check(app.themeChooserPreviewBase == -1 && app.currentThemeIndex == 0,
              "the wheel puts the theme in use back: another card is under the pointer now");
        for (int i = 0; i < 3; i++) wheel(app, -1, view.left + 40, view.top + 40);
        chooser(app, 1050, 800);
        last = chooserHit(app, lastLight);
        if (last) click(app, *last);
        check(last && app.currentThemeIndex == lastLight && !app.showThemeChooser,
              "a click on a card scrolled into reach picks its theme and closes the dialog");
    }

    // Opened with a theme far down in use: it is in sight
    Frame reopened = chooser(app, 1050, 800, true);
    const D2D1_RECT_F* chosen = chooserHit(app, lastLight);
    check(app.themeChooserScroll > 0.0f && chosen && about(height(*chosen), 72.0f * s) &&
              inside(*chosen, app.themeChooserViewport),
          "opened again, the dialog shows the theme in use without scrolling to it");
    shot(app, reopened, "chooser-reopened");
    // Its name stops before the check at the card's corner
    int longName = 0;
    for (int i = 0; i < themeCount(); i++)
        if (std::wstring(themeAt(i).name).size() > 30) longName = i;
    applyTheme(app, longName);
    Frame longFrame = chooser(app, 1050, 800, true);
    const D2D1_RECT_F* longCard = chooserHit(app, longName);
    check(longCard && std::wstring(themeAt(longName).name).size() > 30, "the long-named theme has a card");
    if (longCard) {
        // The bead at the corner is the accent, not letters of the name
        const D2D1_COLOR_F bead = longFrame.at(longCard->right - 20.0f * s - 5.0f * s, longCard->top + 20.0f * s - 5.0f * s);
        const D2D1_COLOR_F accent = themeAt(longName).accent;
        check(std::fabs(bead.r - accent.r) < 0.08f && std::fabs(bead.g - accent.g) < 0.08f &&
                  std::fabs(bead.b - accent.b) < 0.08f,
              "a long name ends before the chosen card's check");
    }
    shot(app, longFrame, "chooser-long-name");

    // The switch and the other header controls
    const bool follow = app.followSystemTheme;
    if (const D2D1_RECT_F* sw = chooserHit(app, -1)) click(app, *sw);
    check(app.followSystemTheme != follow && app.showThemeChooser, "the Follow Windows theme switch flips and the dialog stays");
    app.followSystemTheme = follow;
    chooser(app, 1050, 800);
    if (const D2D1_RECT_F* close = chooserHit(app, -2)) click(app, *close);
    check(!app.showThemeChooser, "the close cross closes it");

    // Narrow, and in a dark theme
    applyTheme(app, 5);
    Frame narrow = chooser(app, 650, 800, true);
    chooserStructure(app, "28 themes at 650");
    check(about(width(app.themeChooserPanel), 650.0f * s - 64.0f * s), "at 650 the dialog takes the window's width");
    shot(app, narrow, "chooser-650-dark");
    app.showThemeChooser = false;
    app.themeChooserPreviewBase = -1;
    applyTheme(app, 0);
}
// --- The menus --------------------------------------------------------------
//
// The reading view's menu, a tab's menu and the two editors' menus were four
// drawings: rows of 30 and of 34, paddings of 4 and 6, corners of 6 and 8,
// shortcuts in the interface's face here and in the code face there. They
// share a row, a padding, a separator, a corner and their type now.

// The heights of a menu's rows, read off its hit test down a column
std::vector<float> rowRuns(const std::function<int(float)>& itemAt, float top, float bottom) {
    std::vector<float> runs;
    int current = -2;
    float start = top;
    for (float y = top; y <= bottom; y += 1.0f) {
        const int item = y < bottom ? itemAt(y) : -3;
        if (item == current) continue;
        if (current >= 0) runs.push_back(y - start);
        current = item;
        start = y;
    }
    return runs;
}

void menus(App& app) {
    using namespace uiscale;
    const float s = app.contentScale;
    applyTheme(app, 0);
    app.width = 1050;
    app.height = 900;

    // The reading view's menu
    openContextMenu(app, 260.0f, 160.0f, false);
    app.contextMenuAnimation = 1.0f;
    app.hoveredContextMenuItem = contextMenuEntries(app)[1].action;
    Frame reading = draw(app, [&] { renderContextMenu(app); });
    const auto& entries = contextMenuEntries(app);
    check(about(contextMenuItemHeight(app), rowMenu * s) && about(contextMenuPadding(app), menuPad * s) &&
              about(contextMenuSeparatorHeight(app), menuSeparator * s),
          "the reading view's menu has the scale's row, padding and separator");
    float wanted = menuPad * 2 * s;
    for (const auto& entry : entries) wanted += rowMenu * s + (entry.separatorAfter ? menuSeparator * s : 0.0f);
    check(about(contextMenuHeight(app), wanted) && onGrid(app, contextMenuHeight(app)) && onGrid(app, contextMenuWidth(app)),
          "and is as tall as its rows, on the scale's base");
    {
        // The second row is lit: the light is the row less the padding, the page's ground shows beside it
        const float top = app.contextMenuY + contextMenuItemTop(app, 1);
        const float mid = top + rowMenu * s * 0.5f;
        const D2D1_COLOR_F lit = reading.at(app.contextMenuX + contextMenuWidth(app) * 0.5f, top + 3.0f * s);
        const D2D1_COLOR_F ground = reading.at(app.contextMenuX + contextMenuWidth(app) * 0.5f,
                                               app.contextMenuY + contextMenuItemTop(app, 0) + 3.0f * s);
        const D2D1_COLOR_F edge = reading.at(app.contextMenuX + 2.0f * s, mid);
        check(contrast(lit, ground) > 1.02f && contrast(edge, ground) < 1.02f,
              "a lit row's light stops a padding short of the menu's edge");
    }
    shot(app, reading, "menu-reading");
    closeContextMenu(app);
    // In a short window its rows give way so that every entry stays in reach
    app.height = 420;
    openContextMenu(app, 260.0f, 60.0f, false);
    check(contextMenuHeight(app) <= 420.0f * s - chromeTopHeight(app) + 0.5f && contextMenuItemHeight(app) < rowMenu * s,
          "in a short window the menu's rows give way and every entry stays in reach");
    closeContextMenu(app);
    app.height = 900;
    // The application's menu is the same rows, wider
    openContextMenu(app, 0.0f, chromeTopHeight(app), true);
    app.contextMenuAnimation = 1.0f;
    Frame application = draw(app, [&] { renderContextMenu(app); });
    check(about(contextMenuItemHeight(app), rowMenu * s) && about(contextMenuWidth(app), 260.0f * s),
          "the application's menu has the same rows");
    shot(app, application, "menu-application");
    closeContextMenu(app);

    // A tab's menu
    app.tabs.resize(3);
    app.tabs[0].title = L"Harbour log.md";
    app.tabs[1].title = L"Tide table.md";
    app.tabs[2].title = L"Crew list.md";
    app.activeTab = 0;
    openTabMenu(app, 1, 240.0f, 40.0f);
    app.tabMenuAnimation = 1.0f;
    app.mouseX = (int)(app.tabMenuX + 40.0f);
    app.mouseY = (int)(app.tabMenuY + menuPad * s + rowMenu * s * 1.5f);
    Frame tab = draw(app, [&] { renderTabMenu(app); });
    {
        const float x = app.tabMenuX + 40.0f * s;
        const std::vector<float> runs = rowRuns([&](float y) { return tabMenuItemAt(app, x, y); }, app.tabMenuY,
                                                app.tabMenuY + 600.0f * s);
        bool rows = runs.size() >= 6;
        for (float run : runs) rows = rows && about(run, rowMenu * s);
        check(rows, "a tab's menu has rows of the same height as the reading view's");
        check(tabMenuItemAt(app, x, app.tabMenuY + menuPad * s * 0.5f) < 0 &&
                  tabMenuItemAt(app, x, app.tabMenuY + menuPad * s + 1.0f) == 0,
              "and the same padding above its first row");
    }
    shot(app, tab, "menu-tab");
    closeTabMenu(app);
    app.tabs.clear();
    app.mouseX = app.mouseY = -100;

    // The Source editor's menu
    app.editMode = true;
    app.editCtxOpen = true;
    app.editCtxRailOnly = false;
    app.editCtxSub = 0;
    app.editCtxX = 300.0f;
    app.editCtxY = 140.0f;
    Frame source = draw(app, [&] { renderEditCtxMenu(app); });
    {
        bool rows = app.editCtxHits.size() >= 7, stacked = true;
        float left = 0.0f, right = 0.0f;
        for (size_t i = 0; i < app.editCtxHits.size(); i++) {
            const D2D1_RECT_F& r = app.editCtxHits[i].first;
            rows = rows && about(height(r), rowMenu * s);
            if (i == 0) { left = r.left; right = r.right; }
            if (!about(r.left, left) || !about(r.right, right)) stacked = false;
        }
        check(rows, "the Source editor's menu has rows of the same height");
        check(stacked && onGrid(app, right - left + menuPad * 2 * s), "its rows share their edges, a padding inside the menu");
    }
    shot(app, source, "menu-source");
    // Its diagram submenu
    app.editCtxSub = 2;
    Frame sub = draw(app, [&] { renderEditCtxMenu(app); });
    {
        bool rows = app.editCtxSubHits.size() >= 8, touching = true;
        for (size_t i = 0; i < app.editCtxSubHits.size(); i++) {
            rows = rows && about(height(app.editCtxSubHits[i].first), rowMenu * s);
            if (i && !about(app.editCtxSubHits[i].first.top, app.editCtxSubHits[i - 1].first.bottom)) touching = false;
        }
        check(rows && touching, "its submenu's rows are the same height and stand one on the other");
    }
    shot(app, sub, "menu-source-diagrams");
    app.editCtxOpen = false;
    app.editCtxSub = 0;
    app.editMode = false;
}

// --- The side panels --------------------------------------------------------
//
// Contents, the folder browser and the search results had rows of 24 and
// 26, section labels and footers at 10 and a bold where the rest of the
// interface is semibold. Their rows are a list's rows now and their small
// text the scale's smallest role.

void sidePanels(App& app) {
    using namespace uiscale;
    const float s = app.contentScale;
    applyTheme(app, 0);
    app.width = 1050;
    app.height = 900;
    updateOverlayFormats(app);

    // The text the panels share is the scale's
    auto is = [&](IDWriteTextFormat* f, float size, DWRITE_FONT_WEIGHT weight) {
        return f && about(f->GetFontSize(), size * s) && f->GetFontWeight() == weight;
    };
    check(is(app.folderBrowserFormat, fontSize(Type::Body), DWRITE_FONT_WEIGHT_NORMAL) &&
              is(app.tocFormat, fontSize(Type::Small), DWRITE_FONT_WEIGHT_NORMAL),
          "the panels' rows are set in Body and Small");
    check(is(app.tocFormatBold, fontSize(Type::Strong), DWRITE_FONT_WEIGHT_SEMI_BOLD),
          "the row in use and a panel's title are semibold, as Strong is, not bold");
    check(is(app.signalSmallFormat, fontSize(Type::Micro), DWRITE_FONT_WEIGHT_SEMI_BOLD) &&
              is(app.themeHeaderFormat, fontSize(Type::Micro), DWRITE_FONT_WEIGHT_SEMI_BOLD),
          "section labels, footers and badges are in Micro: nothing in a panel is under 11");

    // Contents: a row for every heading of the fixture
    layoutDocument(app);
    app.showToc = true;
    app.tocAnimation = 1.0f;
    app.tocFilter.clear();
    Frame contents = draw(app, [&] { renderToc(app); });
    check(app.headings.size() >= 6 && about(tocListContentHeight(app), (float)app.headings.size() * rowList * s),
          "the contents panel has a list's row for every heading");
    shot(app, contents, "panel-contents");
    app.showToc = false;
    app.tocAnimation = 0.0f;

    // The folder browser
    app.showFolderBrowser = true;
    app.folderBrowserAnimation = 1.0f;
    Frame browser = draw(app, [&] { renderFolderBrowser(app); });
    const FolderBrowserMetrics g = folderBrowserMetrics(app);
    check(about(g.itemHeight, rowList * s) && about(g.cardBottom - g.listBottom, rowList * s),
          "the folder browser's rows and its footer are a list's row high");
    shot(app, browser, "panel-browser");
    app.showFolderBrowser = false;
    app.folderBrowserAnimation = 0.0f;
}

// --- The shortcut list ------------------------------------------------------
//
// One column of some sixty rows, 520 wide, whatever the window: most of it
// was out of sight. In a window wide enough its groups stand in two columns.

Frame help(App& app, int w, int h) {
    app.width = w;
    app.height = h;
    app.showHelp = true;
    app.helpAnimation = 1.0f;
    return draw(app, [&] { renderHelpOverlay(app); });
}

void shortcutList(App& app) {
    using namespace uiscale;
    const float s = app.contentScale;
    applyTheme(app, 0);
    app.helpScroll = 0.0f;
    Frame two = help(app, 1050, 900);
    const D2D1_RECT_F panel = helpPanelRect(app);
    std::vector<float> lefts;
    bool rows = app.helpRows.size() > 55, inPanel = true;
    for (const D2D1_RECT_F& r : app.helpRows) {
        rows = rows && about(height(r), 24.0f * s);
        if (r.left < panel.left + sp24 * s - 0.5f || r.right > panel.right - sp24 * s + 0.5f) inPanel = false;
        if (std::none_of(lefts.begin(), lefts.end(), [&](float x) { return about(x, r.left); })) lefts.push_back(r.left);
    }
    check(rows, "every shortcut has a row of 24");
    check(lefts.size() == 2 && inPanel && about(std::fabs(lefts[1] - lefts[0]), (416.0f + sp32) * s),
          "at 1050 the groups stand in two columns, a dialog's padding inside it");
    bool apart = true;
    for (size_t i = 0; i < app.helpRows.size(); i++)
        for (size_t j = i + 1; j < app.helpRows.size(); j++)
            if (overlap(app.helpRows[i], app.helpRows[j])) apart = false;
    check(apart, "no two rows share a place");
    const float twoHeight = app.helpContentHeight;
    check(app.helpContentHeight > app.helpVisibleHeight && about(height(panel), 900.0f * s - 40.0f * s),
          "what does not fit a 900-high window scrolls");
    shot(app, two, "help-two-columns");
    wheel(app, -3, 500, 400);
    Frame scrolled = help(app, 1050, 900);
    check(app.helpScroll > 0.0f && two.differs(scrolled, panel), "the wheel scrolls the list");
    const D2D1_RECT_F head = D2D1::RectF(panel.left, panel.top, panel.right, panel.top + (sp24 + 28.0f) * s);
    check(!two.differs(scrolled, head), "under the title, which stays");
    app.helpScroll = 0.0f;

    // A window tall enough holds both columns whole
    help(app, 1050, 1200);
    check(app.helpContentHeight <= app.helpVisibleHeight + 0.5f &&
              about(height(helpPanelRect(app)), (sp24 * 2 + 28.0f + sp16) * s + app.helpContentHeight),
          "in a window tall enough the dialog is as tall as its columns and nothing scrolls");

    // Narrow: one column, as it was
    applyTheme(app, 5);
    Frame one = help(app, 700, 900);
    lefts.clear();
    for (const D2D1_RECT_F& r : app.helpRows)
        if (std::none_of(lefts.begin(), lefts.end(), [&](float x) { return about(x, r.left); })) lefts.push_back(r.left);
    check(lefts.size() == 1 && app.helpContentHeight > twoHeight * 1.5f,
          "at 700 the groups are one column, more than half again as long");
    shot(app, one, "help-one-column-dark");
    app.showHelp = false;
    app.helpScroll = 0.0f;
    applyTheme(app, 0);
}

// --- The editors' dialogs and the start page ---------------------------------
//
// The shortcut editor and the theme editor had their titles at 28 light and
// rows of 30; the start page had text at 11.5, 12.5 and 13.5.

void dialogsAndStart(App& app) {
    using namespace uiscale;
    const float s = app.contentScale;
    applyTheme(app, 0);
    app.width = 1050;
    app.height = 900;
    updateOverlayFormats(app);
    check(app.themeTitleFormat && about(app.themeTitleFormat->GetFontSize(), fontSize(Type::Title) * s) &&
              app.themeTitleFormat->GetFontWeight() == DWRITE_FONT_WEIGHT_SEMI_BOLD,
          "the editors' titles are in the scale's Title: 20 semibold, where they were 28 light");

    // The shortcut editor
    app.showShortcutEditor = true;
    app.shortcutEditorRow = -1;
    Frame keys = draw(app, [&] { renderShortcutEditor(app); });
    int rows = 0;
    bool rowsOk = true, touching = true;
    float lastBottom = -1.0f;
    for (const auto& hit : app.shortcutHits) {
        if (width(hit.first) < 300.0f * s) continue;  // a row, not a button
        ++rows;
        rowsOk = rowsOk && about(height(hit.first), rowMenu * s - 4.0f * s);
        if (lastBottom >= 0.0f && !about(hit.first.top - lastBottom, 4.0f * s)) touching = false;
        lastBottom = hit.first.bottom;
    }
    check(rows >= 10 && rowsOk && touching, "the shortcut editor's rows are a menu's row apart");
    shot(app, keys, "shortcut-editor");
    app.showShortcutEditor = false;

    // The theme editor, opened as Settings opens it
    app.showSettings = true;
    app.settingsAnimation = 1.0f;
    app.settingsSection = 1;
    draw(app, [&] { renderSettingsOverlay(app); });
    for (const auto& hit : app.settingsHits)
        if (hit.second == SET_NEW_THEME) { click(app, hit.first); break; }
    check(app.showThemeEditor && !app.showSettings, "Settings' New opens the theme editor");
    Frame editor = draw(app, [&] { renderThemeEditor(app); });
    shot(app, editor, "theme-editor");
    app.showThemeEditor = false;
    app.showSettings = false;

    // The start page
    Frame start = draw(app, [&] { renderStartPage(app); });
    check(!start.empty(), "the start page draws");
    shot(app, start, "start-page");
}

}  // namespace

int runUiSurfacesTests(const char* fixtures) {
    const auto dir = fs::current_path();
    g_frames = dir / "frames";
    fs::create_directories(g_frames);
    std::error_code ec;
    fs::copy_file(fs::path(fixtures) / "themes.ini", dir / "fixture-themes.ini", fs::copy_options::overwrite_existing, ec);
    loadLanguageOverrides();
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Interface surface tests", WS_POPUP, 0, 0, 1050, 900,
                               nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    applyTheme(app, 0);
    check(openDocumentInViewer(app, (fs::path(fixtures) / "surfaces.md").wstring()), "the fixture opens behind the surfaces");

    themeChooser(app);
    menus(app);
    sidePanels(app);
    shortcutList(app);
    dialogsAndStart(app);

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    std::cout << "Interface surfaces: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
