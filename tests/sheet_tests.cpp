// Sheets in the window: a copy of the fixture sheet opened in a tab, drawn
// at several zooms and themes into frame previews, and worked through the
// real pointer and key handlers (sheet_ui.h)
#include "clipboard_snapshot.h"
#include "d2d_init.h"
#include "editor.h"
#include "frame_preview.h"
#include "i18n.h"
#include "input.h"
#include "keystroke.h"
#include "overlays.h"
#include "settings.h"
#include "sheet_ui.h"
#include "startpage.h"
#include "tabs.h"
#include "touch.h"
#include "utils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <string>

namespace {
namespace fs = std::filesystem;

// The test window hands a lost capture to the sheet as the app's window
// procedure does (main_d2d.cpp). Releasing capture on a mouse-up sends
// WM_CAPTURECHANGED there and then: a press that took its own release for
// a lost capture put moved cards back and dropped new arrows.
App* g_captureApp = nullptr;
WNDPROC g_staticProc = nullptr;
LRESULT CALLBACK captureProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CAPTURECHANGED && g_captureApp && (HWND)lParam != hwnd) sheetCaptureLost(*g_captureApp, hwnd);
    return CallWindowProcW(g_staticProc, hwnd, msg, wParam, lParam);
}
using namespace sheet;
using namespace sheetui;

int failures = 0;
void check(bool ok, const std::string& message) {
    if (!ok) {
        if (failures < 80) std::cerr << "FAIL sheets: " << message << '\n';
        ++failures;
    }
}

std::string readBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void writeBytes(const fs::path& path, const std::string& bytes) { std::ofstream(path, std::ios::binary) << bytes; }

LPARAM at(D2D1_POINT_2F p) { return MAKELPARAM((int)p.x, (int)p.y); }

// A frame as the window paints it before the next press: the sheet's
// buttons and minimap are where the pointer finds them
void paint(App& app) {
    sheetPrepareFrame(app);
    app.renderTarget->BeginDraw();
    renderSheet(app);
    app.renderTarget->EndDraw();
}

void move(App& app, D2D1_POINT_2F p) { handleMouseMove(app, app.hwnd, at(p)); }

void click(App& app, D2D1_POINT_2F p, KeyMods mods = {}) {
    paint(app);
    withKeysHeld(mods, [&] {
        handleMouseMove(app, app.hwnd, at(p));
        handleMouseDown(app, app.hwnd, MK_LBUTTON, at(p));
        handleMouseUp(app, app.hwnd, 0, at(p));
    });
}

void doubleClick(App& app, D2D1_POINT_2F p) {
    click(app, p);
    click(app, p);
}

// A press soon after another on the same spot is a double-click, in the
// sheet as in Windows; a wait keeps the next press a press of its own
void pauseClicks() { Sleep(GetDoubleClickTime() + 30); }

void drag(App& app, D2D1_POINT_2F from, D2D1_POINT_2F to, KeyMods mods = {}) {
    pauseClicks();
    paint(app);
    withKeysHeld(mods, [&] {
        handleMouseMove(app, app.hwnd, at(from));
        handleMouseDown(app, app.hwnd, MK_LBUTTON, at(from));
        check(GetCapture() == app.hwnd, "a press on the sheet holds the mouse");
        for (int i = 1; i <= 8; ++i) {
            D2D1_POINT_2F p = D2D1::Point2F(from.x + (to.x - from.x) * i / 8, from.y + (to.y - from.y) * i / 8);
            handleMouseMove(app, app.hwnd, at(p));
        }
        handleMouseUp(app, app.hwnd, 0, at(to));
    });
}

D2D1_POINT_2F centreOf(App& app, SheetDoc& doc, const std::string& id) {
    int i = nodeIndex(doc.model, id);
    if (i < 0) return D2D1::Point2F(-1, -1);
    const Node& n = doc.model.nodes[i];
    return toScreen(app, doc, n.x + n.width * 0.5f, n.y + n.height * 0.5f);
}

// One frame of the sheet, saved for a look
void frame(App& app, const fs::path& path) {
    sheetPrepareFrame(app);
    // Let the card content finish drawing at this zoom
    for (int pass = 0; pass < 20; ++pass) {
        SheetDoc* doc = activeSheet(app);
        if (!doc) break;
        doc->zoomChangedMs = 0;
        renderCardBitmaps(app, *doc, 400);
    }
    check(saveFramePreview(app, path, [&] { renderSheet(app); renderTabStrip(app); }),
          "frame saved: " + path.filename().string());
}

void settle(App& app) {
    SheetDoc* doc = activeSheet(app);
    if (!doc) return;
    doc->flight.active = false;
    if (doc->flight.durationMs > 0) {
        doc->camX = doc->flight.toX;
        doc->camY = doc->flight.toY;
        doc->zoom = doc->flight.toZoom;
    }
    doc->glide.active = false;
}

std::string noteText(SheetDoc& doc, const std::string& id) {
    int i = nodeIndex(doc.model, id);
    if (i < 0) return {};
    return readBytes(absolutePath(doc, doc.model.nodes[i].file));
}

bool overlap(const Node& a, const Node& b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}

void loading(App& app, SheetDoc& doc, const fs::path& dir) {
    check(doc.model.nodes.size() == 14 && doc.model.edges.size() == 6, "the fixture's 14 nodes and 6 arrows load");
    check(_wcsicmp(doc.cardDir.c_str(), (dir / "Launch plan").wstring().c_str()) == 0, "new notes go to the sheet's folder");
    pollFolder(app, doc, true);
    check(doc.unplaced.size() == 1 && doc.unplaced[0].find(L"Unplaced idea.md") != std::wstring::npos,
          "the note in the folder but not on the sheet waits in the tray");
    const Node& kick = doc.model.nodes[nodeIndex(doc.model, "kickoff")];
    SheetCard& k = card(app, doc, kick);
    check(k.kind == SheetCard::Kind::Note && k.title == "Kickoff" && k.tasksDone == 1 && k.tasksTotal == 3,
          "a note card knows its title and tasks: " + k.title);
    SheetCard& mock = card(app, doc, doc.model.nodes[nodeIndex(doc.model, "mock")]);
    check(mock.kind == SheetCard::Kind::Image, "the PNG is an image card");
    std::set<std::pair<std::string, std::string>> ghosts;
    for (const auto& g : ghostLinks(app, doc)) ghosts.insert(g);
    check(ghosts.count({"kickoff", "budget"}) && ghosts.count({"design", "kickoff"}) && ghosts.count({"ship", "retro"}),
          "links written in the notes show as ghosts");
    check(!ghosts.count({"kickoff", "research"}) && !ghosts.count({"research", "budget"}),
          "a link already drawn as an arrow is no ghost");
    check(ghosts.size() == 3, "three ghosts: " + std::to_string(ghosts.size()));
}

void frames(App& app, SheetDoc& doc, const fs::path& out) {
    for (int theme : {0, 5}) {
        applyTheme(app, theme);
        for (int width : {1050, 650}) {
            app.width = width;
            app.height = width == 1050 ? 800 : 620;
            std::string tag = std::string(theme == 0 ? "paper" : "midnight") + "-" + std::to_string(width);
            doc.selected.clear();
            fitBox(app, doc, bounds(doc.model), false);
            frame(app, out / ("fit-" + tag + ".png"));
            // Close: the notes read in full
            const Node& research = doc.model.nodes[nodeIndex(doc.model, "research")];
            flyTo(doc, research.x + research.width * 0.5f, research.y + research.height * 0.5f, 1.0f, 0);
            doc.selected = {"research"};
            frame(app, out / ("close-" + tag + ".png"));
            // Far: titles and figures instead of unreadable text
            flyTo(doc, 900, 500, 0.18f, 0);
            doc.selected.clear();
            frame(app, out / ("far-" + tag + ".png"));
        }
    }
    applyTheme(app, 5);
    // At 150% the chrome and the cards scale with the screen
    app.width = 1575;
    app.height = 1200;
    app.contentScale = 1.5f;
    updateTextFormats(app);
    fitBox(app, doc, bounds(doc.model), false);
    frame(app, out / "fit-midnight-150pct.png");
    app.contentScale = 1.0f;
    updateTextFormats(app);
    app.width = 1050;
    app.height = 800;
}

void editing(App& app, SheetDoc& doc, const fs::path& out) {
    // A double-click on open ground writes a new note there
    fitBox(app, doc, bounds(doc.model), false);
    flyTo(doc, 3000, 3000, 1.0f, 0);
    size_t before = doc.model.nodes.size();
    D2D1_RECT_F v = viewRect(app);
    D2D1_POINT_2F ground = D2D1::Point2F((v.left + v.right) * 0.5f, (v.top + v.bottom) * 0.5f);
    doubleClick(app, ground);
    settle(app);
    check(doc.model.nodes.size() == before + 1, "a double-click on open ground adds a card");
    check(doc.editing == SheetDoc::Edit::Card, "the new card opens for typing");
    std::string id = doc.editId;
    typeKeys(app, L"# Hello sheet");
    keystroke(app, VK_RETURN);
    typeKeys(app, L"- one");
    keystroke(app, VK_RETURN);
    typeKeys(app, L"two");
    frame(app, out / "editing.png");
    check(doc.edit.text == L"# Hello sheet\n- one\n- two", "Enter continues a list: " + toUtf8(doc.edit.text));
    keystroke(app, VK_ESCAPE);
    check(doc.editing == SheetDoc::Edit::None, "Esc ends the typing");
    int i = nodeIndex(doc.model, id);
    check(i >= 0 && doc.model.nodes[i].file == "Launch plan/Hello sheet.md",
          "the new note names itself after its first line: " + (i >= 0 ? doc.model.nodes[i].file : std::string()));
    check(noteText(doc, id) == "# Hello sheet\n- one\n- two", "what was typed is in the note");
    // Undo takes the note away again, file and all; redo brings it back
    fs::path note = absolutePath(doc, doc.model.nodes[i].file);
    keystroke(app, 'Z', {true, false, false});
    keystroke(app, 'Z', {true, false, false});
    check(nodeIndex(doc.model, id) < 0, "undo removes the new card");
    check(!fs::exists(note), "and the note it made");
    keystroke(app, 'Y', {true, false, false});
    keystroke(app, 'Y', {true, false, false});
    check(nodeIndex(doc.model, id) >= 0 && fs::exists(note) && readBytes(note) == "# Hello sheet\n- one\n- two",
          "redo brings both back");

    // Tab grows a mind map: a child joined by an arrow, typed at once
    doc.selected = {id};
    keystroke(app, VK_ESCAPE);
    doc.selected = {id};
    size_t edges = doc.model.edges.size();
    keystroke(app, VK_TAB);
    settle(app);
    check(doc.editing == SheetDoc::Edit::Card && doc.model.edges.size() == edges + 1, "Tab adds a joined child card");
    std::string child = doc.editId;
    check(!doc.model.edges.empty() && doc.model.edges.back().fromNode == id && doc.model.edges.back().toNode == child,
          "the arrow runs from the card to its child");
    typeKeys(app, L"Child idea");
    keystroke(app, VK_ESCAPE);
    int ci = nodeIndex(doc.model, child), pi = nodeIndex(doc.model, id);
    check(ci >= 0 && pi >= 0 && doc.model.nodes[ci].x > doc.model.nodes[pi].x, "the child sits to the right");
    // Arrow keys walk between cards
    doc.selected = {child};
    keystroke(app, VK_LEFT);
    check(doc.selected.size() == 1 && doc.selected[0] == id, "Left walks back to the parent");
}

void pointer(App& app, SheetDoc& doc, const fs::path& dir) {
    fitBox(app, doc, bounds(doc.model), false);
    const Node& kick = doc.model.nodes[nodeIndex(doc.model, "kickoff")];
    flyTo(doc, kick.x + 600, kick.y + 400, 0.6f, 0);
    // Click selects, Shift adds
    click(app, centreOf(app, doc, "kickoff"));
    check(doc.selected == std::vector<std::string>{"kickoff"}, "a click selects a card");
    click(app, centreOf(app, doc, "research"), {false, true, false});
    check(doc.selected.size() == 2, "Shift+click adds to the selection");
    // A drag moves the card, on the grid
    click(app, centreOf(app, doc, "budget"));
    Node budgetBefore = doc.model.nodes[nodeIndex(doc.model, "budget")];
    D2D1_POINT_2F from = centreOf(app, doc, "budget");
    drag(app, from, D2D1::Point2F(from.x + 37, from.y + 71));
    const Node& budget = doc.model.nodes[nodeIndex(doc.model, "budget")];
    check(budget.x != budgetBefore.x || budget.y != budgetBefore.y, "a drag moves the card");
    check(std::fmod(std::fabs(budget.y), kGrid) < 0.01f || !doc.guidesY.empty(), "the move lands on the grid or a guide");
    keystroke(app, 'Z', {true, false, false});
    const Node& back = doc.model.nodes[nodeIndex(doc.model, "budget")];
    check(back.x == budgetBefore.x && back.y == budgetBefore.y, "undo puts it back");

    // The corner resizes the selected card, on the grid; undo puts it back.
    // The drag above pressed on this same spot.
    {
        pauseClicks();
        click(app, centreOf(app, doc, "budget"));
        Node start = doc.model.nodes[nodeIndex(doc.model, "budget")];
        D2D1_POINT_2F corner = toScreen(app, doc, start.x + start.width, start.y + start.height);
        drag(app, corner, D2D1::Point2F(corner.x + 60, corner.y + 40));
        const Node& grown = doc.model.nodes[nodeIndex(doc.model, "budget")];
        check(grown.width > start.width && grown.height > start.height && grown.x == start.x && grown.y == start.y,
              "a drag on the corner resizes the card and keeps it in place");
        check(std::fmod(grown.x + grown.width, kGrid) < 0.01f && std::fmod(grown.y + grown.height, kGrid) < 0.01f,
              "the dragged corner lands on the grid");
        keystroke(app, 'Z', {true, false, false});
        const Node& undone = doc.model.nodes[nodeIndex(doc.model, "budget")];
        check(undone.width == start.width && undone.height == start.height, "undo restores its size");
    }
    // A box dragged over open ground selects the cards it touches
    {
        const Node& kickoff = doc.model.nodes[nodeIndex(doc.model, "kickoff")];
        const Node& research = doc.model.nodes[nodeIndex(doc.model, "research")];
        doc.selected.clear();
        drag(app, toScreen(app, doc, kickoff.x - 30, kickoff.y - 30),
             toScreen(app, doc, research.x + research.width + 10, research.y + research.height + 10));
        std::set<std::string> picked(doc.selected.begin(), doc.selected.end());
        check(picked == std::set<std::string>({"kickoff", "research"}), "a box selects the cards it touches");
    }
    // Any card of a selection drags all of it, Shift held or not; a Shift
    // click without a drag takes a card out
    {
        auto at = [&](const char* id) { return doc.model.nodes[nodeIndex(doc.model, id)]; };
        auto together = [&](const Node& k0, const Node& r0) {
            Node k1 = at("kickoff"), r1 = at("research");
            return k1.y != k0.y && k1.x - k0.x == r1.x - r0.x && k1.y - k0.y == r1.y - r0.y;
        };
        Node k0 = at("kickoff"), r0 = at("research");
        D2D1_POINT_2F grab = centreOf(app, doc, "kickoff");
        drag(app, grab, D2D1::Point2F(grab.x + 60, grab.y + 90));
        check(together(k0, r0) && doc.selected.size() == 2, "a drag on one selected card moves the selection");
        keystroke(app, 'Z', {true, false, false});
        grab = centreOf(app, doc, "research");
        drag(app, grab, D2D1::Point2F(grab.x + 60, grab.y + 90), {false, true, false});
        check(together(k0, r0) && doc.selected.size() == 2,
              "so does a Shift+drag on a selected card, which stays selected");
        keystroke(app, 'Z', {true, false, false});
        check(at("kickoff").x == k0.x && at("research").y == r0.y, "undo puts both back");
        pauseClicks();
        click(app, centreOf(app, doc, "research"), {false, true, false});
        check(doc.selected == std::vector<std::string>{"kickoff"}, "a Shift+click takes a selected card out");
    }

    // A card dropped into a lane takes the lane's value into its note
    const Node& doing = doc.model.nodes[nodeIndex(doc.model, "g-doing")];
    const Node& ship = doc.model.nodes[nodeIndex(doc.model, "ship")];
    float targetX = doing.x + doing.width * 0.5f, targetY = doing.y + doing.height - ship.height * 0.5f - 30;
    click(app, centreOf(app, doc, "ship"));
    drag(app, centreOf(app, doc, "ship"), toScreen(app, doc, targetX, targetY));
    int shipIndex = nodeIndex(doc.model, "ship");
    check(groupOf(doc.model, shipIndex) == nodeIndex(doc.model, "g-doing"), "the card is in the Doing lane");
    std::string shipText = noteText(doc, "ship");
    check(frontmatterValue(shipText, "status") == "Doing", "its note now says status: Doing: " + shipText.substr(0, 40));
    // Sorting into lanes moves a card whose note names another lane
    std::string buildPath = toUtf8(absolutePath(doc, doc.model.nodes[nodeIndex(doc.model, "build")].file));
    std::string build = readBytes(buildPath);
    setFrontmatterValue(build, "status", "Done");
    writeBytes(buildPath, build);
    pollFolder(app, doc, true);
    runAction(app, app.hwnd, doc, ACT_SORT_LANES);
    check(groupOf(doc.model, nodeIndex(doc.model, "build")) == nodeIndex(doc.model, "g-done"),
          "Sort into lanes moves Build to Done, as its note says");

    // A drag from a side dot to another card draws an arrow
    settle(app);
    size_t edges = doc.model.edges.size();
    doc.selected = {"budget"};
    D2D1_POINT_2F dot = toScreen(app, doc, doc.model.nodes[nodeIndex(doc.model, "budget")].x +
                                               doc.model.nodes[nodeIndex(doc.model, "budget")].width * 0.5f,
                                 doc.model.nodes[nodeIndex(doc.model, "budget")].y);
    move(app, centreOf(app, doc, "budget"));
    drag(app, dot, centreOf(app, doc, "design"));
    check(doc.model.edges.size() == edges + 1 && doc.model.edges.back().fromNode == "budget" &&
              doc.model.edges.back().toNode == "design",
          "a drag from a dot joins two cards");
    check(doc.model.edges.back().fromSide == Side::None, "a new arrow follows the cards' sides by itself");

    // A ghost link becomes an arrow with a click. In the fixture the ghosts
    // run behind cards; one card goes out on its own so a ghost has open
    // ground to be clicked on.
    std::vector<std::pair<std::string, std::string>> ghosts = ghostLinks(app, doc);
    check(!ghosts.empty(), "there are ghosts to promote");
    if (!ghosts.empty()) {
        Box all = bounds(doc.model);
        Node& out = doc.model.nodes[nodeIndex(doc.model, ghosts[0].second)];
        out.x = all.right + 500;
        out.y = all.bottom + 400;
        fitBox(app, doc, bounds(doc.model), false);
        ghosts = {ghosts[0]};
    }
    // A point on a ghost clear of every card and every arrow, in the view
    D2D1_POINT_2F point{};
    bool found = false;
    D2D1_RECT_F view = viewRect(app);
    for (const auto& ghost : ghosts) {
        int a = nodeIndex(doc.model, ghost.first), b = nodeIndex(doc.model, ghost.second);
        Side fs, ts;
        edgeSides(doc.model.nodes[a], doc.model.nodes[b], Side::None, Side::None, fs, ts);
        Curve c = edgeCurve(boxOf(doc.model.nodes[a]), boxOf(doc.model.nodes[b]), fs, ts);
        for (float t = 0.1f; t <= 0.9f && !found; t += 0.02f) {
            D2D1_POINT_2F q = bezier(c, t);
            if (nodeAt(doc, q.x, q.y, false) >= 0) continue;
            bool nearArrow = false;
            for (const Edge& e : doc.model.edges) {
                if (curveDistance(edgeCurveFor(doc, e), q.x, q.y) < 30.0f / scale(app, doc)) nearArrow = true;
            }
            D2D1_POINT_2F screen = toScreen(app, doc, q.x, q.y);
            if (nearArrow || screen.x < view.left + 40 || screen.x > view.right - 260 || screen.y < view.top + 60 ||
                screen.y > view.bottom - 160) {
                continue;
            }
            point = screen;
            found = true;
        }
        if (found) break;
    }
    check(found, "a ghost can be clicked away from cards and arrows");
    if (found) {
        size_t count = doc.model.edges.size();
        move(app, point);
        click(app, point);
        check(doc.model.edges.size() == count + 1, "a click on a ghost makes it an arrow");
    }
    // The menu turns the rest into arrows, ghosts behind cards too
    std::vector<std::pair<std::string, std::string>> rest = ghostLinks(app, doc);
    check(!rest.empty(), "ghosts are left for the menu");
    if (!rest.empty()) {
        doc.selected = {rest[0].first};
        size_t count = doc.model.edges.size();
        runAction(app, app.hwnd, doc, ACT_LINKS_TO_ARROWS);
        check(doc.model.edges.size() > count, "Turn its links into arrows draws them");
        doc.selected.clear();
        runAction(app, app.hwnd, doc, ACT_LINKS_TO_ARROWS);
        check(ghostLinks(app, doc).empty(), "Turn all links into arrows leaves no ghost");
    }

    // Ctrl+G groups the selection and names the group
    doc.selected = {"kickoff", "research"};
    size_t nodes = doc.model.nodes.size();
    keystroke(app, 'G', {true, false, false});
    check(doc.model.nodes.size() == nodes + 1 && doc.editing == SheetDoc::Edit::GroupLabel, "Ctrl+G groups and names");
    keystroke(app, 'A', {true, false, false});
    typeKeys(app, L"First steps");
    keystroke(app, VK_RETURN);
    int g = nodeIndex(doc.model, doc.selected.empty() ? std::string() : doc.selected[0]);
    check(g >= 0 && doc.model.nodes[g].label == "First steps", "the group takes its name");
    check(g >= 0 && insideGroup(doc.model.nodes[g], doc.model.nodes[nodeIndex(doc.model, "kickoff")]),
          "the group holds its cards");
    (void)dir;
}

void commands(App& app, SheetDoc& doc, const fs::path& dir, const fs::path& out) {
    // Find
    keystroke(app, 'F');
    check(doc.finding && doc.editing == SheetDoc::Edit::Find, "F opens the find bar");
    typeKeys(app, L"budget");
    settle(app);
    frame(app, out / "find.png");
    std::set<std::string> found(doc.found.begin(), doc.found.end());
    check(found.count("budget") && found.count("kickoff") && found.count("research"),
          "find matches titles and note text");
    keystroke(app, VK_RETURN);
    check(doc.selected.size() == 1 && doc.foundIndex == 1, "Enter steps to the next match");
    keystroke(app, VK_ESCAPE);
    check(!doc.finding, "Esc closes find");

    // Present along the arrows
    keystroke(app, 'P');
    settle(app);
    frame(app, out / "present.png");
    check(doc.presenting && !doc.slides.empty(), "P presents");
    int first = doc.slide;
    keystroke(app, VK_RIGHT);
    check(doc.slide == first + 1, "Right goes to the next slide");
    keystroke(app, VK_ESCAPE);
    check(!doc.presenting, "Esc ends the presentation");

    // Spotlight
    doc.selected = {"research"};
    keystroke(app, 'S');
    settle(app);
    frame(app, out / "spotlight.png");
    check(doc.spotlight && doc.spotlightId == "research", "S spotlights the card");
    keystroke(app, 'S');
    check(!doc.spotlight, "S again ends it");

    // Copy as Mermaid, and a pasted flowchart becomes cards
    {
        ClipboardSnapshot saved(app.hwnd);
        runAction(app, app.hwnd, doc, ACT_COPY_MERMAID);
        std::wstring text;
        if (OpenClipboard(app.hwnd)) {
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
                    text = p;
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
        }
        check(text.find(L"flowchart LR") != std::wstring::npos && text.find(L"Kickoff") != std::wstring::npos,
              "the sheet copies as a Mermaid flowchart");
        copyToClipboard(app.hwnd, L"flowchart TD\n  A[Plan] --> B[Do]\n  B --> C[Check]\n");
        size_t nodes = doc.model.nodes.size(), edges = doc.model.edges.size();
        keystroke(app, 'V', {true, false, false});
        check(doc.model.nodes.size() == nodes + 3 && doc.model.edges.size() == edges + 2,
              "a pasted flowchart arrives as three cards and two arrows");
        check(fs::exists(dir / "Launch plan" / "Plan.md"), "each pasted card is a note in the folder");
    }

    // Tidy leaves no card on another
    doc.selected.clear();
    keystroke(app, 'L', {true, false, false});
    settle(app);
    bool clean = true;
    for (size_t a = 0; a < doc.model.nodes.size(); ++a) {
        for (size_t b = a + 1; b < doc.model.nodes.size(); ++b) {
            const Node& x = doc.model.nodes[a];
            const Node& y = doc.model.nodes[b];
            if (x.type == NodeType::Group || y.type == NodeType::Group) continue;
            if (overlap(x, y)) clean = false;
        }
    }
    check(clean, "Ctrl+L tidies the cards without overlaps");

    // Compile: one document along the arrows, opened in a tab
    runAction(app, app.hwnd, doc, ACT_COMPILE);
    fs::path compiled = dir / "Launch plan" / "Launch plan.compiled.md";
    check(fs::exists(compiled), "compiling writes a document beside the notes");
    std::string text = readBytes(compiled);
    check(text.rfind("# Launch plan\n", 0) == 0, "it opens with the sheet's title");
    check(text.find("## Discovery") != std::string::npos, "groups are sections");
    check(text.find("### Kickoff") != std::string::npos || text.find("## Kickoff") != std::string::npos,
          "notes keep their headings, one level down");
    check(text.find("![Board mock](mock.png)") != std::string::npos, "images in notes still point at them");
    check(app.currentFile.find("Launch plan.compiled.md") != std::string::npos, "the compiled document opens in a tab");
}

// A finger pans the sheet and a double tap on open ground writes a note
void touch(App& app, SheetDoc& doc) {
    flyTo(doc, 5000, 5000, 1.0f, 0);
    paint(app);
    D2D1_RECT_F v = viewRect(app);
    POINT from{(LONG)((v.left + v.right) * 0.5f), (LONG)((v.top + v.bottom) * 0.5f)};
    PanelList list;
    check(touchTargetAt(app, from, list) == TouchTarget::Sheet, "a finger on the canvas drives the sheet");
    float camX = doc.camX, camY = doc.camY;
    double ms = touchClockMs();
    check(touchDown(app, app.hwnd, 7, from, ms), "the finger is taken");
    for (int i = 1; i <= 10; ++i) touchMove(app, app.hwnd, 7, POINT{from.x + 12 * i, from.y + 6 * i}, ms += 16);
    touchUp(app, app.hwnd, 7, POINT{from.x + 120, from.y + 60}, ms += 200, false);
    touchStopFling(app, app.hwnd);
    float s = scale(app, doc);
    check(std::fabs(doc.camX - (camX - 120 / s)) < 2 / s && std::fabs(doc.camY - (camY - 60 / s)) < 2 / s,
          "dragging a finger right and down moves the view with it");
    size_t nodes = doc.model.nodes.size();
    for (int tap = 0; tap < 2; ++tap) {
        touchDown(app, app.hwnd, 8, from, ms += 80);
        touchUp(app, app.hwnd, 8, from, ms += 40, false);
    }
    check(doc.model.nodes.size() == nodes + 1 && doc.editing == SheetDoc::Edit::Card,
          "a double tap on open ground makes a note to type in");
    keystroke(app, VK_ESCAPE);
}

void saving(App& app, const fs::path& sheetPath) {
    // Back on the sheet: everything done above is in its file
    tabOpenPath(app, app.hwnd, toUtf8(sheetPath.wstring()));
    SheetDoc* doc = activeSheet(app);
    check(doc != nullptr, "the sheet's tab comes back");
    if (!doc) return;
    sheetLeave(app);
    std::string text = readBytes(sheetPath);
    Sheet model;
    SmdInfo info;
    check(parseSmd(text, model, info), "the saved sheet parses");
    check(model.nodes.size() == doc->model.nodes.size() && model.edges.size() == doc->model.edges.size(),
          "the saved sheet holds every card and arrow");
    check(text.find("- [Kickoff](<Launch plan/Kickoff.md>)") != std::string::npos,
          "the outline lists the notes as links");
    check(info.folder == "Launch plan" && info.title == "Launch plan", "the front matter and title stay");
}

void obsidianCanvas(App& app, const fs::path& dir, const fs::path& out) {
    fs::path canvas = dir / "vault" / "Boards" / "Board.canvas";
    tabOpenPath(app, app.hwnd, toUtf8(canvas.wstring()));
    SheetDoc* doc = activeSheet(app);
    check(doc && doc->canvasFile, "an Obsidian canvas opens as a sheet");
    if (!doc) return;
    check(_wcsicmp(doc->base.c_str(), (dir / "vault").wstring().c_str()) == 0, "its paths run from the vault");
    const Node& idea = doc->model.nodes[nodeIndex(doc->model, "i1")];
    SheetCard& c = card(app, *doc, idea);
    check(c.kind == SheetCard::Kind::Note && c.title == "Idea", "a vault note shows on the canvas");
    frame(app, out / "obsidian-canvas.png");
    // A change saves back as plain JSON Canvas
    runAction(app, app.hwnd, *doc, ACT_ZOOM_FIT);
    doc->selected = {"t1"};
    runAction(app, app.hwnd, *doc, ACT_COLOR_BASE + 2);
    sheetLeave(app);
    std::string json = readBytes(canvas);
    Sheet model;
    check(json.rfind("{\n\t\"nodes\":[", 0) == 0 && parseCanvas(json, model) && model.nodes.size() == 3,
          "the canvas saves back as JSON Canvas");
    check(model.nodes[nodeIndex(model, "t1")].color == "2", "with the change in it");
}

void unreadable(App& app, const fs::path& dir) {
    fs::path broken = dir / "Broken.smd";
    std::string text = "# Broken\n\n```json canvas\n{\"nodes\":[{\"id\":\n```\n";
    writeBytes(broken, text);
    tabOpenPath(app, app.hwnd, toUtf8(broken.wstring()));
    SheetDoc* doc = activeSheet(app);
    check(doc && doc->readOnly, "a sheet that does not parse is read-only");
    if (!doc) return;
    keystroke(app, 'N');
    sheetLeave(app);
    check(readBytes(broken) == text, "and is never written over");
}

// A big sheet stays quick: 400 notes, each linking to the next, drawn
// whole and close up. The times are printed; the bounds only catch a
// sheet that has become unusable, not a slower machine.
void performance(App& app, const fs::path& dir) {
    fs::path folder = dir / "Big";
    fs::create_directories(folder);
    for (int i = 0; i < 400; ++i) {
        std::string text = "# Note " + std::to_string(i) + "\n\nSome text with **bold** and a [[Note " +
                           std::to_string((i + 1) % 400) + "]] link.\n\n- [ ] one\n- [x] two\n";
        writeBytes(folder / ("Note " + std::to_string(i) + ".md"), text);
    }
    fs::path sheetPath = dir / "Big.smd";
    check(sheetCreateAt(app, sheetPath.wstring()), "a sheet of 400 notes is made");
    using clock = std::chrono::steady_clock;
    auto ms = [](clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
    auto start = clock::now();
    tabOpenPath(app, app.hwnd, toUtf8(sheetPath.wstring()));
    SheetDoc* doc = activeSheet(app);
    check(doc && doc->model.nodes.size() == 400, "the big sheet opens with its 400 cards");
    if (!doc) return;
    pollFolder(app, *doc, true);
    double open = ms(clock::now() - start);
    start = clock::now();
    size_t ghosts = ghostLinks(app, *doc).size();
    double ghostTime = ms(clock::now() - start);
    check(ghosts == 400, "every note's link shows as a ghost: " + std::to_string(ghosts));
    // The whole sheet: every card as its title
    fitBox(app, *doc, bounds(doc->model), false);
    paint(app);
    start = clock::now();
    for (int i = 0; i < 5; ++i) paint(app);
    double whole = ms(clock::now() - start) / 5;
    // Close up: the notes' content, drawn once and then kept
    const Node& n = doc->model.nodes[200];
    flyTo(*doc, n.x + n.width * 0.5f, n.y + n.height * 0.5f, 1.0f, 0);
    doc->zoomChangedMs = 0;
    start = clock::now();
    for (int i = 0; i < 20; ++i) paint(app);
    double firstClose = ms(clock::now() - start) / 20;
    start = clock::now();
    for (int i = 0; i < 5; ++i) paint(app);
    double close = ms(clock::now() - start) / 5;
    std::cout << "Big sheet: open " << open << " ms, ghosts " << ghostTime << " ms, whole frame " << whole
              << " ms, close frames while drawing notes " << firstClose << " ms, then " << close << " ms\n";
    {
        fitBox(app, *doc, bounds(doc->model), false);
        doc->showGhosts = false;
        paint(app);
        start = clock::now();
        for (int i = 0; i < 5; ++i) paint(app);
        double noGhosts = ms(clock::now() - start) / 5;
        doc->showGhosts = true;
        std::cout << "Big sheet whole frame without ghosts: " << noGhosts << " ms\n";
    }
    // The window draws on the GPU once it is up (useHardwareRT); the same frames there
    app.useHardwareRT = true;
    if (createRenderTarget(app)) {
        fitBox(app, *doc, bounds(doc->model), false);
        paint(app);
        start = clock::now();
        for (int i = 0; i < 10; ++i) paint(app);
        double gpuWhole = ms(clock::now() - start) / 10;
        flyTo(*doc, n.x + n.width * 0.5f, n.y + n.height * 0.5f, 1.0f, 0);
        doc->zoomChangedMs = 0;
        for (int i = 0; i < 20; ++i) paint(app);
        start = clock::now();
        for (int i = 0; i < 10; ++i) paint(app);
        double gpuClose = ms(clock::now() - start) / 10;
        std::cout << "Big sheet on the hardware target: whole frame " << gpuWhole << " ms, close " << gpuClose << " ms\n";
        check(gpuWhole < 250 && gpuClose < 250, "the big sheet draws in time on the hardware target too");
    }
    app.useHardwareRT = false;
    createRenderTarget(app);
    check(open < 3000, "the big sheet opens in seconds");
    check(whole < 250 && close < 250, "a frame of the big sheet draws in under a quarter second");
}

}  // namespace

// A note still called Untitled names itself after its first line once it
// is done, a heading or not, whatever ran while it was typed: here an arrow
// dropped on open ground makes it, and the window's timer saves the typing
// and looks at the folder before a click elsewhere ends it. An Untitled
// left in the sheet's folder by an earlier session names itself too.
void naming(App& app, const fs::path& dir) {
    fs::path path = dir / "Naming.smd";
    fs::create_directories(dir / "Naming");
    writeBytes(dir / "Naming" / "Start.md", "# Start\n");
    writeBytes(dir / "Naming" / "Untitled.md", "Left from before\n");
    check(sheetCreateAt(app, path.wstring()), "a sheet to name notes on is made");
    tabOpenPath(app, app.hwnd, toUtf8(path.wstring()));
    SheetDoc* docPtr = activeSheet(app);
    check(docPtr && docPtr->model.nodes.size() == 2, "it opens with the two notes in its folder");
    if (!docPtr || docPtr->model.nodes.size() != 2) return;
    SheetDoc& doc = *docPtr;
    auto nodeOf = [&](const std::string& file) {
        for (const Node& n : doc.model.nodes) {
            if (n.file == file) return n;
        }
        return Node{};
    };
    const Node start = nodeOf("Naming/Start.md");
    const Node old = nodeOf("Naming/Untitled.md");
    check(!start.id.empty() && !old.id.empty(), "Start and Untitled are on it");
    if (start.id.empty() || old.id.empty()) return;
    flyTo(doc, start.x + start.width * 0.5f, start.y + start.height + 100, 1.0f, 0);
    doc.selected = {start.id};
    D2D1_POINT_2F dot = toScreen(app, doc, start.x + start.width * 0.5f, start.y + start.height);
    move(app, centreOf(app, doc, start.id));
    drag(app, dot, D2D1::Point2F(dot.x, dot.y + 160));
    settle(app);
    check(doc.model.nodes.size() == 3 && doc.editing == SheetDoc::Edit::Card,
          "an arrow dropped on open ground opens a new note");
    std::string id = doc.editId;
    keystroke(app, VK_RETURN);
    typeKeys(app, L"#This is a header");
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    typeKeys(app, L"And here is just some paragraph");
    keystroke(app, VK_RETURN);
    keystroke(app, VK_RETURN);
    typeKeys(app, L"### This is a subheader");
    // The window's timer: the typing reaches the note, the folder is read
    doc.edit.lastTypeMs = 0;
    sheetTimer(app, app.hwnd);
    pollFolder(app, doc, true);
    pauseClicks();
    click(app, toScreen(app, doc, start.x - 200, start.y + start.height * 0.5f));
    check(doc.editing == SheetDoc::Edit::None, "a click on open ground ends the typing");
    int i = nodeIndex(doc.model, id);
    std::string file = i >= 0 ? doc.model.nodes[i].file : std::string();
    check(file == "Naming/This is a header.md", "the note is named after its first line: " + file);
    check(fs::exists(dir / "Naming" / "This is a header.md") && !fs::exists(dir / "Naming" / "Untitled 2.md"),
          "and its file is renamed");

    // The Untitled from before: a new first line typed into its card names it
    flyTo(doc, old.x + old.width * 0.5f, old.y + old.height * 0.5f, 1.0f, 0);
    pauseClicks();
    doubleClick(app, centreOf(app, doc, old.id));
    check(doc.editing == SheetDoc::Edit::Card && doc.editId == old.id, "a double-click opens the old Untitled");
    keystroke(app, VK_HOME, {true, false, false});
    typeKeys(app, L"Fresh name");
    keystroke(app, VK_RETURN);
    keystroke(app, VK_ESCAPE);
    i = nodeIndex(doc.model, old.id);
    file = i >= 0 ? doc.model.nodes[i].file : std::string();
    check(file == "Naming/Fresh name.md" && fs::exists(dir / "Naming" / "Fresh name.md") &&
              readBytes(dir / "Naming" / "Fresh name.md").rfind("Fresh name\nLeft from before", 0) == 0,
          "an Untitled in the sheet's folder names itself too: " + file);
    // A note with a name of its own keeps it
    flyTo(doc, start.x + start.width * 0.5f, start.y + start.height * 0.5f, 1.0f, 0);
    pauseClicks();
    doubleClick(app, centreOf(app, doc, start.id));
    keystroke(app, VK_HOME, {true, false, false});
    typeKeys(app, L"Another title");
    keystroke(app, VK_RETURN);
    keystroke(app, VK_ESCAPE);
    check(doc.model.nodes[nodeIndex(doc.model, start.id)].file == "Naming/Start.md", "a named note keeps its name");
}

// The ten additions, on a sheet of their own (Ten.smd): the hint line, the
// selection bar, folds, arrows written into notes, live writing in a card,
// [[ names, the peek lens, jump labels, exploding a document and lanes
// that stack
void tenMore(App& app, const fs::path& dir, const fs::path& out) {
    setHintsShown(true);  // every hint starts over
    fs::path path = dir / "Ten.smd";
    fs::create_directories(dir / "Ten");
    writeBytes(dir / "Ten" / "Alpha.md", "# Alpha\n\nThe first.\n");
    writeBytes(dir / "Ten" / "Beta.md", "# Beta\n\nThe second.\n");
    writeBytes(dir / "Ten" / "Gamma.md", "# Gamma\n\n- one\n- two\n");
    writeBytes(dir / "Ten" / "Delta.md", "# Delta\n\nLast.\n");
    check(sheetCreateAt(app, path.wstring()), "the ten's sheet is made");
    tabOpenPath(app, app.hwnd, toUtf8(path.wstring()));
    SheetDoc* docPtr = activeSheet(app);
    check(docPtr && docPtr->model.nodes.size() == 4, "it holds the four notes of its folder");
    if (!docPtr || docPtr->model.nodes.size() != 4) return;
    SheetDoc& doc = *docPtr;
    auto idOf = [&](const char* name) {
        for (const Node& n : doc.model.nodes) {
            if (n.file == std::string("Ten/") + name + ".md") return n.id;
        }
        return std::string();
    };
    const std::string alpha = idOf("Alpha"), beta = idOf("Beta"), gamma = idOf("Gamma"), delta = idOf("Delta");
    auto node = [&](const std::string& id) -> Node& { return doc.model.nodes[nodeIndex(doc.model, id)]; };
    auto joined = [&](const std::string& from, const std::string& to) {
        return std::any_of(doc.model.edges.begin(), doc.model.edges.end(),
                           [&](const Edge& e) { return e.fromNode == from && e.toNode == to; });
    };
    // In a row, 440 apart, Alpha → Beta → Gamma
    float x = 0;
    for (const std::string& id : {alpha, beta, gamma, delta}) {
        node(id).x = x;
        node(id).y = 0;
        x += 440;
    }
    doc.model.edges.clear();
    for (auto pair : {std::make_pair(alpha, beta), std::make_pair(beta, gamma)}) {
        Edge e;
        e.id = newId();
        e.fromNode = pair.first;
        e.toNode = pair.second;
        doc.model.edges.push_back(e);
    }
    markChanged(app, doc);
    flyTo(doc, 760, 120, 0.6f, 0);
    auto buttonAt = [&](int id) {
        for (const auto& hit : doc.uiHits) {
            if (hit.second == id) return D2D1::Point2F((hit.first.left + hit.first.right) * 0.5f, (hit.first.top + hit.first.bottom) * 0.5f);
        }
        return D2D1::Point2F(-1, -1);
    };

    // The hint line and the selection bar
    doc.selected = {gamma, delta};
    paint(app);
    check(doc.hintText == tr(app, "sheet.hint.multi"), "two cards selected: the hint line tells how to move them");
    check(doc.selectionBar.right > doc.selectionBar.left, "a bar stands over the selection");
    frame(app, out / "ten-bar.png");
    pauseClicks();
    click(app, buttonAt(ACT_CHAIN));
    check(joined(gamma, delta), "Join in order joins the cards left to right");
    pauseClicks();
    click(app, buttonAt(ACT_COLOR_BASE + 4));
    check(node(gamma).color == "4" && node(delta).color == "4", "a swatch colours every selected card");
    pauseClicks();
    click(app, buttonAt(ACT_ALIGN_MENU));
    check(doc.menuOpen && !doc.menu.empty() && doc.menu[0].action == ACT_ALIGN_LEFT, "Align opens the align commands");
    keystroke(app, VK_ESCAPE);
    for (int k = 0; k < 3; ++k) hintDone(HINT_MULTI);
    paint(app);
    check(doc.hintText.empty(), "a hint done three times retires");

    // Folds: Z folds what follows; the +N badge unfolds; the fold moves with its card
    doc.selected = {alpha};
    keystroke(app, 'Z');
    check(node(alpha).folded && foldedAwayAt(doc, nodeIndex(doc.model, beta)) &&
              foldedAwayAt(doc, nodeIndex(doc.model, delta)) && !foldedAwayAt(doc, nodeIndex(doc.model, alpha)),
          "Z folds away what follows the card");
    paint(app);
    check(doc.foldHits.size() == 1 && foldedCount(doc.model, nodeIndex(doc.model, alpha), doc.hidden) == 3,
          "a +3 badge stands beside it");
    Node b0 = node(beta);
    check(nodeAt(doc, b0.x + 10, b0.y + 10, false) != nodeIndex(doc.model, beta), "a folded-away card is not hit");
    frame(app, out / "ten-folded.png");
    Node a0 = node(alpha);
    D2D1_POINT_2F grab = centreOf(app, doc, alpha);
    drag(app, grab, D2D1::Point2F(grab.x, grab.y + 120));
    check(node(alpha).y != a0.y && node(beta).y - b0.y == node(alpha).y - a0.y, "what it folds away moves along with it");
    keystroke(app, 'Z', {true, false, false});
    // Undo glides the cards back: the badge is where they come to rest
    pauseClicks();
    paint(app);
    D2D1_RECT_F badge = doc.foldHits.empty() ? D2D1_RECT_F{} : doc.foldHits[0].first;
    click(app, D2D1::Point2F((badge.left + badge.right) * 0.5f, (badge.top + badge.bottom) * 0.5f));
    check(!node(alpha).folded && !foldedAwayAt(doc, nodeIndex(doc.model, beta)), "a click on the badge unfolds it");

    // Arrows written into the notes they leave
    runAction(app, app.hwnd, doc, ACT_WRITE_LINKS);
    check(doc.model.writeLinks && noteText(doc, alpha).find("related:\n  - \"[[Beta]]\"") != std::string::npos,
          "writing links writes the arrows already drawn: " + noteText(doc, alpha).substr(0, 60));
    doc.selectedEdge = doc.model.edges.front().id;
    std::string from = doc.model.edges.front().fromNode;
    keystroke(app, VK_DELETE);
    check(noteText(doc, from).find("related") == std::string::npos, "removing the arrow takes its link out");
    keystroke(app, 'Z', {true, false, false});
    check(noteText(doc, from).find("[[") != std::string::npos, "undo brings arrow and link back");
    runAction(app, app.hwnd, doc, ACT_WRITE_LINKS);

    // Live writing: the caret's line shows its Markdown, the others show it drawn
    doc.selected = {alpha};
    startEdit(app, doc, SheetDoc::Edit::Card, alpha);
    std::wstring kept = doc.edit.text;
    doc.edit.text = L"# Title\n- item\n- [ ] task\n**bold** and [[Beta]]\nplain";
    doc.edit.caret = doc.edit.anchor = doc.edit.text.size();
    D2D1_RECT_F rect{};
    float sc = 1;
    editRect(app, doc, rect, sc);
    auto xAt = [&](size_t at) {
        IDWriteTextLayout* layout = editLayout(app, doc, (rect.right - rect.left) / sc);
        float px = 0, py = 0;
        DWRITE_HIT_TEST_METRICS m{};
        if (layout) layout->HitTestTextPosition((UINT32)at, FALSE, &px, &py, &m);
        return px;
    };
    check(std::fabs(xAt(2) - xAt(0)) < 0.5f, "away from the caret a heading's # takes no room");
    frame(app, out / "ten-writing.png");
    doc.edit.caret = doc.edit.anchor = 3;
    check(xAt(2) - xAt(0) > 3.0f, "on the caret's line it shows as typed");
    doc.edit.text = L"#Plan";
    doc.edit.caret = doc.edit.anchor = 5;
    paint(app);
    check(doc.hintText == tr(app, "sheet.hint.heading"), "a line starting #word asks for the space");
    doc.edit.text = kept;
    doc.edit.caret = doc.edit.anchor = kept.size();
    keystroke(app, VK_ESCAPE);

    // [[ names: the sheet's notes under the caret; Ctrl+Enter draws the arrow too
    doc.selected = {delta};
    startEdit(app, doc, SheetDoc::Edit::Card, delta);
    doc.edit.caret = doc.edit.anchor = doc.edit.text.size();
    typeKeys(app, L" See [[Be");
    check(doc.edit.popup == LiveText::Popup::Names && !doc.edit.choices.empty() && doc.edit.choices[0].label == L"Beta",
          "[[ lists the notes, Beta first for Be");
    frame(app, out / "ten-complete.png");
    keystroke(app, VK_RETURN, {true, false, false});
    check(doc.edit.text.size() >= 8 && doc.edit.text.substr(doc.edit.text.size() - 8) == L"[[Beta]]" && joined(delta, beta),
          "Ctrl+Enter writes the link and draws the arrow");
    keystroke(app, VK_ESCAPE);
    check(noteText(doc, delta).find("See [[Beta]]") != std::string::npos, "the note holds the link");

    // The engine cards share with Live: / makes a block, a selection shows the format bar
    doc.selected = {delta};
    startEdit(app, doc, SheetDoc::Edit::Card, delta);
    doc.edit.caret = doc.edit.anchor = doc.edit.text.size();
    keystroke(app, VK_RETURN);
    typeKeys(app, L"/h2");
    check(doc.edit.popup == LiveText::Popup::Slash && !doc.edit.choices.empty() && doc.edit.choices[0].insert == live::INS_H2,
          "/h2 offers Heading 2 first in a card");
    keystroke(app, VK_RETURN);
    typeKeys(app, L"Next");
    check(doc.edit.text.find(L"## Next") != std::wstring::npos, "Enter on it makes the line a heading: " + toUtf8(doc.edit.text));
    doc.edit.anchor = doc.edit.text.size() - 4;
    doc.edit.caret = doc.edit.text.size();
    paint(app);
    check(!doc.edit.barHits.empty(), "a selection in a card shows the format bar");
    frame(app, out / "ten-format-bar.png");
    keystroke(app, 'B', {true, false, false});
    check(doc.edit.text.find(L"## **Next**") != std::wstring::npos, "Ctrl+B in a card makes it bold: " + toUtf8(doc.edit.text));
    keystroke(app, VK_ESCAPE);
    check(noteText(doc, delta).find("## **Next**") != std::string::npos, "the note holds the heading");

    // Peek: far out, a card rested on opens in a lens
    flyTo(doc, 760, 120, 0.2f, 0);
    doc.selected.clear();
    paint(app);
    move(app, centreOf(app, doc, gamma));
    check(doc.peekCandidate == gamma, "resting on a card far out readies a peek");
    doc.peekSinceMs = 0;
    sheetTimer(app, app.hwnd);
    check(doc.peekId == gamma, "after a moment it opens");
    frame(app, out / "ten-peek.png");
    check(doc.peekRect.right > doc.peekRect.left && doc.peekBitmap, "the lens is drawn beside the card");
    pauseClicks();
    click(app, toScreen(app, doc, 760, 1600));
    check(doc.peekId.empty(), "a click elsewhere puts it away");

    // Jump labels: J, then a card's letter
    flyTo(doc, 760, 120, 0.6f, 0);
    doc.selected.clear();
    paint(app);
    keystroke(app, 'J');
    check(doc.jumping && doc.jumpLabels.size() == 4 && doc.jumpLabels[2].second == gamma,
          "J labels the cards in view, row by row");
    frame(app, out / "ten-jump.png");
    keystroke(app, 'D');
    check(!doc.jumping && doc.selected == std::vector<std::string>{gamma}, "a card's letter picks it");
    keystroke(app, 'J', {false, true, false});
    keystroke(app, 'A');
    check(joined(gamma, alpha), "Shift+J joins the selection to the card picked");

    // Explode: a document's sections as cards, chained, their links aimed anew
    size_t nodes = doc.model.nodes.size(), edges = doc.model.edges.size();
    int made = explodeDocumentInto(app, doc, (dir / "Long draft.md").wstring(), 0, 1600);
    check(made == 5 && doc.model.nodes.size() == nodes + 5 && doc.model.edges.size() == edges + 4,
          "the draft explodes into five cards in a chain: " + std::to_string(made));
    std::string pictures = readBytes(dir / "Ten" / "Pictures.md");
    check(fs::exists(dir / "Ten" / "Long draft.md") && fs::exists(dir / "Ten" / "Why now.md") &&
              pictures.find("](../Launch%20plan/mock.png)") != std::string::npos,
          "each section is a note named after it, its image still found: " + pictures.substr(0, 60));
    check(readBytes(dir / "Ten" / "How.md").find("// ## not a heading inside a fence") != std::string::npos,
          "a fence is not cut");
    settle(app);
    frame(app, out / "ten-exploded.png");
    // Split: a note's sections go into notes of their own beside it
    writeBytes(dir / "Ten" / "Gamma.md", "# Gamma\n\nIntro.\n\n## One\n\nFirst.\n\n## Two\n\nSecond.\n");
    pollFolder(app, doc, true);
    nodes = doc.model.nodes.size();
    check(splitCard(app, doc, gamma) && doc.model.nodes.size() == nodes + 2 && readBytes(dir / "Ten" / "Gamma.md") ==
              "# Gamma\n\nIntro.\n\n" && fs::exists(dir / "Ten" / "One.md") && fs::exists(dir / "Ten" / "Two.md"),
          "Split by headings keeps the intro and makes One and Two");

    // Lanes that stack: a card dropped in takes its place in the column; the
    // lane it leaves closes up; over its limit the lane says so
    Node lane;
    lane.id = newId();
    lane.type = NodeType::Group;
    lane.label = "Doing";
    lane.laneKey = "status";
    lane.laneLimit = 1;
    lane.x = 0;
    lane.y = 400;
    lane.width = 440;
    lane.height = 340;
    doc.model.nodes.insert(doc.model.nodes.begin(), lane);
    markChanged(app, doc);
    flyTo(doc, 600, 400, 0.5f, 0);
    auto dropInto = [&](const std::string& id, float sx, float sy) {
        D2D1_POINT_2F a = centreOf(app, doc, id);
        drag(app, a, toScreen(app, doc, sx, sy));
    };
    doc.selected.clear();
    dropInto(alpha, 220, 560);
    const Node& ln = node(lane.id);
    check(node(alpha).x == ln.x + 40 && node(alpha).y == ln.y + 80 && frontmatterValue(noteText(doc, alpha), "status") == "Doing",
          "a card dropped into a lane stands at the top of its column");
    doc.selected.clear();
    dropInto(beta, 220, 700);
    check(node(beta).x == ln.x + 40 && node(beta).y == node(alpha).y + node(alpha).height + 20,
          "the next stacks under it, 20 apart");
    settle(app);
    frame(app, out / "ten-lane.png");
    doc.selected.clear();
    dropInto(alpha, 900, 300);
    check(node(beta).y == ln.y + 80, "a card leaving the lane lets the rest close up");
}

// The start page's four buttons stand two by two, of one width: Open a file
// and Browse over New document and New sheet. Drawn at two widths in a dark
// and a light theme for a look.
void startPage(App& app, const fs::path& out) {
    auto width = [](const D2D1_RECT_F& r) { return r.right - r.left; };
    for (int theme : {5, 0}) {
        applyTheme(app, theme);
        for (int w : {1050, 650}) {
            app.width = w;
            app.height = w == 1050 ? 800 : 620;
            std::string name = std::string("start-") + (theme == 0 ? "paper" : "midnight") + "-" + std::to_string(w);
            check(saveFramePreview(app, out / (name + ".png"), [&] { renderStartPage(app); }), "frame saved: " + name);
            D2D1_RECT_F at[7] = {};
            for (const auto& hit : app.startPageHits) {
                if (hit.second >= 1 && hit.second <= 6) at[hit.second] = hit.first;
            }
            const D2D1_RECT_F &open = at[1], &newDoc = at[2], &browse = at[3], &sheet = at[6];
            check(width(open) > 0 && width(newDoc) > 0 && width(browse) > 0 && width(sheet) > 0,
                  name + ": the start page has its four buttons");
            check(open.top == browse.top && newDoc.top == sheet.top && newDoc.top > open.bottom,
                  name + ": Open and Browse on top, New document and New sheet under them");
            check(open.left == newDoc.left && browse.left == sheet.left && browse.left > open.right,
                  name + ": in two columns");
            check(std::fabs(width(open) - width(browse)) < 0.5f && std::fabs(width(open) - width(newDoc)) < 0.5f &&
                      std::fabs(width(open) - width(sheet)) < 0.5f,
                  name + ": of one width");
        }
    }
    app.width = 1050;
    app.height = 800;
}

// The new tab's empty page offers New sheet beside Open a file, under it
// when the pane is narrow; a sheet made from it takes the empty tab's place
void newTabPage(App& app, const fs::path& dir, const fs::path& out) {
    size_t tabs = app.tabs.size();
    tabOpenQuickNote(app, app.hwnd);
    check(quickNoteEmptyStateActive(app) && app.tabs.size() == tabs + 1, "+ opens an empty note");
    if (!quickNoteEmptyStateActive(app)) return;
    // The narrow one first: the pointer below finds the wide one's buttons
    for (int w : {650, 1050}) {
        app.width = w;
        app.height = w == 1050 ? 800 : 620;
        std::string name = "new-tab-" + std::to_string(w);
        check(saveFramePreview(app, out / (name + ".png"), [&] { renderQuickNoteEmptyState(app); }),
              "frame saved: " + name);
        const D2D1_RECT_F &open = app.quickNoteButtonRect, &sheet = app.quickNoteSheetRect;
        bool beside = sheet.left >= open.right && sheet.top == open.top;
        bool under = sheet.top >= open.bottom && sheet.left == open.left;
        check(sheet.right > sheet.left && (w == 1050 ? beside : beside || under),
              name + ": New sheet stands beside Open a file, or under it in a narrow pane");
    }
    const D2D1_RECT_F& r = app.quickNoteSheetRect;
    move(app, D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f));
    check(app.quickNoteSheetHover && !app.quickNoteButtonHover, "the pointer over New sheet lights it alone");
    fs::path made = dir / "From a new tab.smd";
    check(sheetCreateAndOpen(app, app.hwnd, made.wstring()) && fs::exists(made), "a sheet is made from the new tab");
    check(sheetActive(app) && app.tabs.size() == tabs + 1 && !app.editMode,
          "it opens in the empty tab's place: " + std::to_string(app.tabs.size()) + " tabs");
}

int runSheetTests(const char* fixtureDir) {
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Sheet tests", WS_POPUP, 0, 0, 1050, 800, nullptr, nullptr, nullptr, nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    g_captureApp = &app;
    g_staticProc = (WNDPROC)SetWindowLongPtrW(app.hwnd, GWLP_WNDPROC, (LONG_PTR)captureProc);
    app.width = 1050;
    app.height = 800;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    // The test works on a copy; the fixture stays as it is
    const fs::path dir = fs::current_path() / "sheets";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::copy(fixtureDir, dir, fs::copy_options::recursive, ec);
    check(!ec, "the fixture copies");
    const fs::path out = fs::current_path() / "frames";
    fs::create_directories(out, ec);
    const fs::path sheetPath = dir / "Launch plan.smd";

    startPage(app, out);
    tabsInit(app);
    tabOpenPath(app, app.hwnd, toUtf8(sheetPath.wstring()));
    check(sheetActive(app), "a .smd opens as a sheet");
    SheetDoc* doc = activeSheet(app);
    if (!doc) return 3;
    applyTheme(app, 5);
    loading(app, *doc, dir);
    frames(app, *doc, out);
    editing(app, *doc, out);
    frame(app, out / "after-editing.png");
    pointer(app, *doc, dir);
    frame(app, out / "after-pointer.png");
    // The menu of a card and the help overlay's Sheets section
    doc->selected.clear();
    fitBox(app, *doc, bounds(doc->model), false);
    paint(app);
    {
        D2D1_POINT_2F c = centreOf(app, *doc, "research");
        sheetContextMenu(app, app.hwnd, (int)c.x, (int)c.y);
        check(doc->menuOpen && !doc->menu.empty(), "a right-click opens the card's menu");
        frame(app, out / "menu.png");
        keystroke(app, VK_ESCAPE);
        check(!doc->menuOpen, "Esc closes the menu");
        app.showHelp = true;
        app.helpAnimation = 1.0f;
        app.helpScroll = 100000.0f;
        sheetPrepareFrame(app);
        check(saveFramePreview(app, out / "help.png", [&] { renderSheet(app); renderTabStrip(app); renderHelpOverlay(app); }),
              "the help overlay draws over the sheet");
        app.showHelp = false;
    }
    commands(app, *doc, dir, out);
    tabOpenPath(app, app.hwnd, toUtf8(sheetPath.wstring()));
    if (SheetDoc* back = activeSheet(app)) touch(app, *back);
    saving(app, sheetPath);
    obsidianCanvas(app, dir, out);
    unreadable(app, dir);

    // A new sheet beside a folder of notes places them all
    fs::path fresh = dir / "Fresh.smd";
    fs::create_directories(dir / "Fresh");
    writeBytes(dir / "Fresh" / "One.md", "# One\n");
    writeBytes(dir / "Fresh" / "Two.md", "# Two\n");
    check(sheetCreateAt(app, fresh.wstring()), "a new sheet is made");
    Sheet model;
    SmdInfo info;
    check(parseSmd(readBytes(fresh), model, info) && model.nodes.size() == 2 && info.folder == "Fresh",
          "a new sheet takes the notes already in its folder");
    naming(app, dir);
    tenMore(app, dir, out);
    newTabPage(app, dir, out);
    performance(app, dir);

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Sheets: " << failures << " failures\n";
    return failures ? 1 : 0;
}
