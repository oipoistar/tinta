// Sheets in the window: a copy of the fixture sheet opened in a tab, drawn
// at several zooms and themes into frame previews, and worked through the
// real pointer and key handlers (sheet_ui.h)
#include "clipboard_snapshot.h"
#include "d2d_init.h"
#include "editor.h"
#include "frame_preview.h"
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
    performance(app, dir);

    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    std::cout << "Sheets: " << failures << " failures\n";
    return failures ? 1 : 0;
}
