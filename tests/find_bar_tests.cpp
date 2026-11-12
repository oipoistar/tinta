#include "d2d_init.h"
#include "editor.h"
#include "input.h"
#include "keystroke.h"
#include "live_editor.h"
#include "overlays.h"
#include "render.h"
#include "search.h"
#include "search_panel.h"
#include "settings.h"
#include "ui_frame.h"
#include "utils.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace uiframe;

// The find bar over the editor and the reading view (#319).
//
// What is typed into the bar was drawn from the field's top in whatever face
// had the letters. A Chinese word in a face of the reader's own (cjkFont=)
// whose line has more room over its letters stood low in the bar, beside a
// count that stood right, and its feet were cut off at the field's edge.
//
// And with the bar open no drag selected anything: every move of the mouse
// ended at the bar, wherever the pointer was.
namespace {
int failures = 0, checks = 0;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
fs::path g_frames;

std::string narrow(const std::wstring& s) { return toUtf8(s); }
LPARAM at(D2D1_POINT_2F p) { return MAKELPARAM((int)std::lround(p.x), (int)std::lround(p.y)); }

// --- The word on the bar's line --------------------------------------------

struct Sample {
    const char* name;
    std::wstring word;
};
// Words of the fixture. Each script comes in a face of its own, and several
// of these faces have their baseline elsewhere in their line than Segoe UI
std::vector<Sample> samples() {
    return {
        {"Latin", L"Initial"},
        {"Chinese", L"\x521d\x59cb"},
        {"Japanese", L"\x304b\x306a"},
        {"Korean", L"\xd55c\xad6d\xc5b4"},
        {"Hindi", L"\x0928\x092e\x0938\x094d\x0924\x0947"},
        {"Thai", L"\x0e2a\x0e27\x0e31\x0e2a\x0e14\x0e35"},
        {"Burmese", L"\x1019\x103c\x1014\x103a\x1019\x102c"},
        {"Javanese", L"\xa997\xa9ae"},
        {"Chinese beside Latin", L"x \x521d\x59cb"},
    };
}

// What differs from the ground inside a rectangle
struct Ink {
    int left = INT_MAX, top = INT_MAX, right = -1, bottom = -1;
    bool any() const { return right >= 0; }
    bool operator==(const Ink& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    std::string text() const {
        return any() ? std::to_string(left) + "," + std::to_string(top) + " to " + std::to_string(right) + "," +
                           std::to_string(bottom)
                     : std::string("none");
    }
};
Ink inkIn(const Frame& frame, const D2D1_RECT_F& r, D2D1_COLOR_F ground) {
    Ink ink;
    for (int y = std::max(0, (int)r.top); y < std::min((int)frame.height, (int)r.bottom); y++)
        for (int x = std::max(0, (int)r.left); x < std::min((int)frame.width, (int)r.right); x++) {
            const D2D1_COLOR_F c = frame.at((float)x, (float)y);
            if (std::fabs(c.r - ground.r) + std::fabs(c.g - ground.g) + std::fabs(c.b - ground.b) < 0.06f) continue;
            ink.left = std::min(ink.left, x);
            ink.right = std::max(ink.right, x);
            ink.top = std::min(ink.top, y);
            ink.bottom = std::max(ink.bottom, y);
        }
    return ink;
}

// A word as the field lays it out: the bar's face, the document's fallback
ComPtr<IDWriteTextLayout> laidOut(App& app, const std::wstring& word) {
    ComPtr<IDWriteTextLayout> layout;
    app.dwriteFactory->CreateTextLayout(word.data(), (UINT32)word.size(), app.searchTextFormat, 1e7f,
                                        dpi(app, 40), &layout);
    if (!layout) return layout;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<IDWriteTextLayout2> second;
    if (app.fontFallback && SUCCEEDED(layout.As(&second))) second->SetFontFallback(app.fontFallback);
    return layout;
}
float baselineOf(IDWriteTextLayout* layout) {
    DWRITE_LINE_METRICS line{};
    UINT32 count = 0;
    return layout && SUCCEEDED(layout->GetLineMetrics(&line, 1, &count)) && count ? line.baseline : 0.0f;
}

void typeQuery(App& app, const std::wstring& word) {
    focusSearchInput(app, false);
    // Emptied first. Again if a key pressed on the real keyboard meanwhile
    // took the Ctrl out of Ctrl+A, which leaves all but a letter
    for (int tries = 0; tries < 4 && !app.searchQuery.empty(); tries++) {
        keystroke(app, 'A', {true, false});
        keystroke(app, VK_BACK);
    }
    typeKeys(app, word);
}
Frame barFrame(App& app) {
    app.searchAnimation = 1.0f;
    return draw(app, [&] { renderSearchOverlay(app); });
}
void openBar(App& app) {
    if (!app.showSearch) keystroke(app, 'F', {true, false});
}
void closeBar(App& app) {
    if (app.showSearch) closeSearchInput(app);
}
void preferCjk(App& app, const wchar_t* face) {
    app.cjkFont = face;
    if (app.fontFallback) app.fontFallback->Release();
    app.fontFallback = nullptr;
    updateTextFormats(app);
}

int g_otherBaselines = 0;  // words whose face has its baseline elsewhere than the bar's

// One word in the bar: on the baseline the bar's own face has, whole
void wordOnTheLine(App& app, const Sample& sample, const std::string& where, bool picture) {
    const std::string name = where + ", " + sample.name;
    openBar(app);
    typeQuery(app, sample.word);
    check(app.searchQuery == sample.word, name + ": the word is typed into the bar (" + narrow(app.searchQuery) + ")");
    app.cursorBlinkOn = false;  // the caret is no part of the word
    const Frame bar = barFrame(app);
    const D2D1_RECT_F field = app.searchFields[0].textRect;
    const D2D1_RECT_F box = searchBarDrawRect(app);

    auto own = laidOut(app, L"");
    auto word = laidOut(app, sample.word);
    check(own && word && !bar.empty(), name + ": the bar is drawn");
    if (!own || !word || bar.empty()) return;
    DWRITE_TEXT_METRICS metrics{};
    word->GetMetrics(&metrics);
    const float shift = baselineOf(own.Get()) - baselineOf(word.Get());
    if (std::fabs(shift) > 1.0f) ++g_otherBaselines;

    // The word's place: the columns it takes, the rows of the bar's inside
    const D2D1_RECT_F place = D2D1::RectF(field.left, box.top + 2.0f,
                                          std::min(field.right, field.left + metrics.widthIncludingTrailingWhitespace + 4.0f),
                                          box.bottom - 2.0f);
    check(metrics.widthIncludingTrailingWhitespace + 6.0f < field.right - field.left,
          name + ": the field is wider than the word");
    const D2D1_COLOR_F ground = bar.at(field.right - 2.0f, field.top + 2.0f);
    const Ink drawn = inkIn(bar, place, ground);

    // The same word drawn by the test alone, its baseline put where the
    // bar's own face has it under the field's top
    const D2D1_POINT_2F origin = D2D1::Point2F(field.left, field.top + shift);
    const Frame alone = draw(app, [&] {
        app.renderTarget->Clear(ground);
        D2D1_COLOR_F ink = themeUiText(app.theme);
        ink.a = 1.0f;
        app.brush->SetColor(ink);
        app.renderTarget->DrawTextLayout(origin, word.Get(), app.brush);
    });
    const Ink wanted = inkIn(alone, place, ground);
    if (picture) {
        save(app, bar, (g_frames / (where + "-" + sample.name + ".png")).wstring());
    }
    check(wanted.any() && drawn.any(), name + ": the word leaves ink in the bar");
    check(drawn.top == wanted.top && drawn.bottom == wanted.bottom,
          name + ": the word stands on the bar's own baseline, whole (rows " + drawn.text() + ", wanted " +
              wanted.text() + ", its face's baseline " + std::to_string(shift) + " off)");
    check(drawn.left == wanted.left && drawn.right == wanted.right, name + ": and starts at the field's left");
    check(drawn.top > (int)box.top && drawn.bottom < (int)box.bottom - 1, name + ": inside the bar");

    // The count stands on that line too: a digit's foot is on the baseline,
    // and the word's foot where its own drawing has it under that line
    if (sample.word.find(L'x') != std::wstring::npos) {
        const float line = field.top + baselineOf(own.Get());
        // The x's foot is the lowest row of the word's first columns
        const Ink letter = inkIn(bar, D2D1::RectF(field.left, box.top + 2.0f, field.left + dpi(app, 7), box.bottom - 2.0f), ground);
        check(letter.any() && std::abs(letter.bottom - (int)std::floor(line)) <= 1,
              name + ": a Latin letter beside the word has its foot on the bar's baseline");
    }
}

// The selection in the field is as tall as the field, whatever the face
void selectionInTheField(App& app, const std::string& where) {
    openBar(app);
    typeQuery(app, L" \x521d\x59cb");  // a space first: marked, and without ink
    keystroke(app, 'A', {true, false});
    app.cursorBlinkOn = false;
    const Frame marked = barFrame(app);
    const D2D1_RECT_F field = app.searchFields[0].textRect;
    const D2D1_COLOR_F ground = marked.at(field.right - 2.0f, field.top + 2.0f);
    const float x = field.left + 1.5f;
    auto tinted = [&](float y) {
        const D2D1_COLOR_F c = marked.at(x, y);
        return std::fabs(c.r - ground.r) + std::fabs(c.g - ground.g) + std::fabs(c.b - ground.b) > 0.03f;
    };
    check(tinted(field.top + 1.0f) && tinted(field.bottom - 2.0f) && tinted((field.top + field.bottom) / 2),
          where + ": the marked text is tinted from the field's top to its bottom");
    check(!tinted(field.top - 2.0f) && !tinted(field.bottom + 1.5f), where + ": and no further");
    keystroke(app, VK_END);
}

void wordsInTheBar(App& app) {
    struct Place { int view; int width; float scale; int theme; const wchar_t* cjk; const char* name; };
    const Place places[] = {
        {0, 1050, 1.0f, 0, L"", "reading, Paper"},
        {0, 650, 1.5f, 5, L"", "reading, narrow, Midnight at 150%"},
        {0, 1050, 1.0f, 0, L"SimSun", "reading, cjkFont SimSun"},
        {1, 1050, 1.0f, 0, L"", "editor, Paper"},
        {1, 650, 1.0f, 5, L"", "editor, narrow, Midnight"},
        {1, 1050, 1.5f, 0, L"", "editor at 150%"},
        {1, 1050, 1.0f, 0, L"SimSun", "editor, cjkFont SimSun"},
        {1, 1050, 1.0f, 5, L"Microsoft JhengHei", "editor, cjkFont Microsoft JhengHei, Midnight"},
        {1, 1050, 1.0f, 0, L"Javanese Text", "editor, a cjkFont without Chinese"},
        {2, 1050, 1.0f, 0, L"", "Live, Paper"},
        {2, 650, 1.0f, 5, L"SimSun", "Live, narrow, Midnight, cjkFont SimSun"},
    };
    for (const Place& place : places) {
        closeBar(app);
        const bool editor = place.view != 0;
        if (editor != app.editMode) {
            if (editor) enterEditMode(app);
            else exitEditMode(app);
        }
        if (editor && editorLiveOn(app) != (place.view == 2)) keystroke(app, VK_OEM_2, {true, false, false});
        check(app.editMode == editor && (!editor || editorLiveOn(app) == (place.view == 2)),
              std::string(place.name) + ": the view is the one wanted");
        app.width = place.width;
        app.contentScale = place.scale;
        applyTheme(app, place.theme);
        preferCjk(app, place.cjk);
        ensureLayoutComplete(app);
        bool first = true;
        for (const Sample& sample : samples()) {
            wordOnTheLine(app, sample, place.name, first || sample.name == std::string("Chinese") ||
                                                       sample.name == std::string("Javanese"));
            first = false;
        }
        selectionInTheField(app, place.name);
    }
    closeBar(app);
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, {true, false, false});
    app.width = 1050;
    app.contentScale = 1.0f;
    applyTheme(app, 0);
    preferCjk(app, L"");
    if (g_otherBaselines == 0) {
        std::cout << "note: no face on this PC has its baseline elsewhere than the bar's; the words were only "
                     "checked where they already stood\n";
    } else {
        std::cout << g_otherBaselines << " words came in a face with another baseline than the bar's\n";
    }
}

// --- A drag beside the open bar --------------------------------------------

void drawEditor(App& app) {
    app.renderTarget->Clear(app.theme.background);
    if (editorLiveOn(app)) {
        D2D1_MATRIX_3X2_F saved;
        app.renderTarget->GetTransform(&saved);
        app.renderTarget->SetTransform(D2D1::Matrix3x2F::Translation(documentViewportX(app), 0) * saved);
        renderLivePage(app);
        app.renderTarget->SetTransform(saved);
    } else {
        renderEditor(app, editorPaneWidth(app));
    }
    if (app.showSearch) renderSearchOverlay(app);
}
void paintEditor(App& app) {
    if (app.layoutDirty || !app.layoutComplete) ensureLayoutComplete(app);
    app.searchAnimation = 1.0f;
    app.renderTarget->BeginDraw();
    drawEditor(app);
    check(SUCCEEDED(app.renderTarget->EndDraw()), "the editor and the bar paint");
}

struct Picked {
    bool any = false;
    size_t from = 0, to = 0;
    bool operator==(const Picked& o) const { return any == o.any && from == o.from && to == o.to; }
    std::string text() const {
        return any ? std::to_string(from) + " to " + std::to_string(to) : std::string("nothing");
    }
};
Picked editorPicked(const App& app) {
    Picked p;
    p.any = app.editorHasSelection && app.editorSelStart != app.editorSelEnd;
    p.from = std::min(app.editorSelStart, app.editorSelEnd);
    p.to = std::max(app.editorSelStart, app.editorSelEnd);
    return p;
}
Picked pagePicked(const App& app) {
    Picked p;
    p.any = app.hasSelection && app.selAnchor != app.selFocus;
    p.from = std::min(app.selAnchor, app.selFocus);
    p.to = std::max(app.selAnchor, app.selFocus);
    return p;
}

// A press, the pointer drawn through the given places, a release at the last
void drag(App& app, const std::vector<D2D1_POINT_2F>& way) {
    // A press of its own, not the second of two: Live counts its own clicks
    if (editorLiveOn(app)) Sleep(GetDoubleClickTime() + 30);
    app.lastClickTime = {};
    app.clickCount = 0;
    handleMouseMove(app, app.hwnd, at(way.front()));
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(way.front()));
    for (size_t i = 1; i < way.size(); i++) handleMouseMove(app, app.hwnd, at(way[i]));
    handleMouseUp(app, app.hwnd, 0, at(way.back()));
}

void editorTop(App& app) {
    app.editorScrollY = 0.0f;
    app.scrollY = app.targetScrollY = 0.0f;
    app.editorScrollX = 0.0f;
    paintEditor(app);
}

// Source or Live: the same drag picks the same words with the bar open as
// with it closed
void dragInEditor(App& app, const std::string& mode) {
    closeBar(app);
    const float top = chromeTopHeight(app);
    const float left = editorLiveOn(app) ? documentViewportX(app) : 0.0f;
    const float pane = editorLiveOn(app) ? documentViewportWidth(app) : editorPaneWidth(app);
    const D2D1_POINT_2F from = D2D1::Point2F(left + pane * 0.30f, top + dpi(app, 190));
    const D2D1_POINT_2F half = D2D1::Point2F(left + pane * 0.45f, top + dpi(app, 230));
    const D2D1_POINT_2F to = D2D1::Point2F(left + pane * 0.60f, top + dpi(app, 300));

    // A press away from what is selected: a press on a selection would
    // pick it up to move it
    const D2D1_POINT_2F away = D2D1::Point2F(left + pane * 0.2f, top + dpi(app, 400));

    struct Bar { bool replace; std::wstring query; const char* name; };
    const Bar bars[] = {
        {false, L"\x521d\x59cb", "a word in the bar"},
        {false, L"", "an empty bar"},
        {true, L"Initial", "the replace row open"},
    };
    for (const Bar& bar : bars) {
        const std::string name = mode + ", " + bar.name;
        // What the drag selects with the bar closed, the caret left where
        // the bar's word put it: Live shows the caret's block as it is
        // written, so the same page needs the same caret
        auto prepare = [&](bool open) {
            closeBar(app);
            editorTop(app);
            drag(app, {away});
            keystroke(app, bar.replace ? 'H' : 'F', {true, false});
            typeQuery(app, bar.query);
            if (!open) closeBar(app);
            editorTop(app);
        };
        prepare(false);
        drag(app, {from, half, to});
        const Picked closed = editorPicked(app);
        check(closed.any, name + ": the drag selects text with the bar closed (" + closed.text() + ")");

        prepare(true);
        check(app.showSearch && app.searchActive && app.searchQuery == bar.query, name + ": the bar is open");
        const D2D1_RECT_F box = searchBarDrawRect(app);
        save(app, draw(app, [&] { drawEditor(app); }), (g_frames / (name + ".png")).wstring());
        // The bar over the pane it searches: whole, wide enough for a word
        // and its count, in the middle of the source or of Live's page
        const float paneLeft = editorLiveOn(app) ? documentViewportX(app) : dpi(app, 48);
        const float paneRight = editorLiveOn(app) ? documentViewportX(app) + documentViewportWidth(app)
                                                  : editorPaneWidth(app);
        check(box.right - box.left >= dpi(app, 300) && box.left >= paneLeft && box.right <= paneRight,
              name + ": the bar is whole over its pane (" + std::to_string((int)box.left) + " to " +
                  std::to_string((int)box.right) + " in " + std::to_string((int)paneLeft) + " to " +
                  std::to_string((int)paneRight) + ")");
        check(about((box.left + box.right) / 2, (paneLeft + paneRight) / 2), name + ": in the pane's middle");
        const D2D1_RECT_F field = app.searchFields[0].textRect;
        check(field.right - field.left >= dpi(app, 150) && field.left > box.left && field.right < box.right,
              name + ": with a field to type into");
        check(from.y > box.bottom + dpi(app, bar.replace ? 60.0f : 4.0f), name + ": the drag starts under the bar");

        drag(app, {from, half, to});
        check(editorPicked(app) == closed,
              name + ": the same drag selects the same text (" + editorPicked(app).text() + ", closed " +
                  closed.text() + ")");
        check(app.showSearch && !app.searchActive, name + ": the bar stays, and the text has the keys");
        check(!app.editorSelecting && !liveDragging(app) && GetCapture() != app.hwnd,
              name + ": the release ends the drag");

        // Drawn through the bar and out again: the bar takes nothing of it
        prepare(true);
        const D2D1_POINT_2F onBar = D2D1::Point2F((box.left + box.right) / 2, (box.top + box.bottom) / 2);
        if (editorLiveOn(app)) Sleep(GetDoubleClickTime() + 30);
        app.lastClickTime = {};
        app.clickCount = 0;
        handleMouseMove(app, app.hwnd, at(from));
        handleMouseDown(app, app.hwnd, MK_LBUTTON, at(from));
        const size_t pressed = app.editorCursorPos;
        handleMouseMove(app, app.hwnd, at(onBar));
        check((app.editorSelecting || liveDragging(app)) && app.editorCursorPos < pressed && editorPicked(app).any,
              name + ": over the bar the drag goes on, up the text (pressed at " + std::to_string(pressed) +
                  ", now " + std::to_string(app.editorCursorPos) + ", selecting " +
                  std::to_string(app.editorSelecting) + std::to_string(liveDragging(app)) + ", picked " +
                  editorPicked(app).text() + ")");
        check(app.searchSelectingField < 0 && !app.searchActive, name + ": and the bar's field takes no part");
        handleMouseMove(app, app.hwnd, at(half));
        handleMouseMove(app, app.hwnd, at(to));
        handleMouseUp(app, app.hwnd, 0, at(to));
        check(editorPicked(app) == closed, name + ": back under the bar it ends where the plain drag ended");
    }
    closeBar(app);
}

// The reading view: a drag over the page with the bar open
void dragInReader(App& app) {
    closeBar(app);
    app.scrollY = app.targetScrollY = 0.0f;
    ensureLayoutComplete(app);
    const float top = chromeTopHeight(app);
    const float shiftX = documentViewportX(app) - app.scrollX;
    // A run of text well under the bar, and one a few runs on
    int first = -1;
    for (int i = 0; i < (int)app.textRects.size(); i++) {
        if (app.textRects[i].rect.top - app.scrollY > top + dpi(app, 150)) { first = i; break; }
    }
    check(first >= 0 && first + 8 < (int)app.textRects.size(), "reading: the page has runs of text under the bar");
    if (first < 0 || first + 8 >= (int)app.textRects.size()) return;
    const D2D1_RECT_F a = app.textRects[first].rect, b = app.textRects[first + 8].rect;
    const D2D1_POINT_2F from = D2D1::Point2F(a.left + 1.0f + shiftX, (a.top + a.bottom) / 2 - app.scrollY);
    const D2D1_POINT_2F to = D2D1::Point2F(b.right - 1.0f + shiftX, (b.top + b.bottom) / 2 - app.scrollY);

    drag(app, {from, to});
    const Picked closed = pagePicked(app);
    check(closed.any, "reading: the drag selects text with the bar closed (" + closed.text() + ")");
    drag(app, {D2D1::Point2F(from.x, to.y + dpi(app, 200))});

    for (const std::wstring& query : {std::wstring(L"\x521d\x59cb"), std::wstring()}) {
        const std::string name = query.empty() ? "reading, an empty bar" : "reading, a word in the bar";
        closeBar(app);
        keystroke(app, 'F', {true, false});
        typeQuery(app, query);
        app.scrollY = app.targetScrollY = 0.0f;
        ensureLayoutComplete(app);
        barFrame(app);
        check(app.showSearch && app.searchQuery == query, name + ": the bar is open");
        // The selection follows the pointer while the button is down: it
        // came only with the release, with nothing to see on the way
        app.lastClickTime = {};
        app.clickCount = 0;
        handleMouseMove(app, app.hwnd, at(from));
        handleMouseDown(app, app.hwnd, MK_LBUTTON, at(from));
        handleMouseMove(app, app.hwnd, at(to));
        check(app.selecting && std::min(app.selAnchor, app.selFocus) == closed.from &&
                  std::max(app.selAnchor, app.selFocus) == closed.to,
              name + ": the selection follows the pointer before the release (" + std::to_string(app.selAnchor) +
                  " to " + std::to_string(app.selFocus) + ")");
        handleMouseUp(app, app.hwnd, 0, at(to));
        check(pagePicked(app) == closed, name + ": the same drag selects the same text (" + pagePicked(app).text() +
                                             ", closed " + closed.text() + ")");
        check(!app.selecting && GetCapture() != app.hwnd, name + ": the release ends the drag");
        drag(app, {D2D1::Point2F(from.x, to.y + dpi(app, 200))});
    }
    closeBar(app);
}

// --- The pointer over and beside the bar ------------------------------------

HCURSOR cursorAt(App& app, D2D1_POINT_2F p) {
    SetCursor(nullptr);
    handleMouseMove(app, app.hwnd, at(p));
    return GetCursor();
}
D2D1_POINT_2F middle(const D2D1_RECT_F& r) { return D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2); }

void pointers(App& app) {
    const HCURSOR beam = LoadCursor(nullptr, IDC_IBEAM);
    const HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
    const HCURSOR hand = LoadCursor(nullptr, IDC_HAND);

    // The editor, with the replace row
    check(app.editMode && !editorLiveOn(app), "pointers: the source editor is open");
    closeBar(app);
    app.editorShowPreview = false;
    editorTop(app);
    const float top = chromeTopHeight(app);
    const D2D1_POINT_2F text = D2D1::Point2F(editorPaneWidth(app) * 0.4f, top + dpi(app, 190));
    const HCURSOR plain = cursorAt(app, text);
    check(plain == beam, "pointers: the source's text has the beam");
    keystroke(app, 'H', {true, false});
    typeQuery(app, L"\x521d\x59cb");
    editorTop(app);
    const D2D1_RECT_F box = searchBarDrawRect(app);
    const D2D1_RECT_F row = searchReplaceRowRect(app);
    check(cursorAt(app, text) == beam, "pointers: and keeps it beside the open bar");
    check(cursorAt(app, middle(app.searchFields[0].textRect)) == beam, "pointers: the beam over the find field");
    check(cursorAt(app, middle(app.searchFields[1].textRect)) == beam, "pointers: the beam over the replace field");
    check(cursorAt(app, D2D1::Point2F(box.left + dpi(app, 8), (box.top + box.bottom) / 2)) == arrow,
          "pointers: the arrow over the bar's glass");
    check(cursorAt(app, D2D1::Point2F(row.left + dpi(app, 8), row.top + dpi(app, 22))) == arrow,
          "pointers: the arrow over the replace row's mark");
    int buttons = 0;
    for (const auto& hit : app.searchReplaceHits) {
        if (hit.second < 3) continue;
        ++buttons;
        check(cursorAt(app, middle(hit.first)) == hand, "pointers: a hand over the replace row's button " +
                                                            std::to_string(hit.second));
    }
    check(buttons == 2, "pointers: the row has its two buttons");
    // Over the bar a press still goes to the field
    const D2D1_RECT_F field = app.searchFields[0].textRect;
    handleMouseDown(app, app.hwnd, MK_LBUTTON, at(D2D1::Point2F(field.left + 1.0f, field.top + 3.0f)));
    handleMouseMove(app, app.hwnd, at(D2D1::Point2F(field.right - 2.0f, field.top + 3.0f)));
    handleMouseUp(app, app.hwnd, 0, at(D2D1::Point2F(field.right - 2.0f, field.top + 3.0f)));
    check(app.searchActive && app.searchFields[0].anchor == 0 && app.searchFields[0].caret == app.searchQuery.size() &&
              !app.editorSelecting,
          "pointers: a drag in the find field selects the field's text, not the source's");
    closeBar(app);

    // The reading view
    exitEditMode(app);
    check(!app.editMode, "pointers: the reading view is open");
    app.scrollY = app.targetScrollY = 0.0f;
    ensureLayoutComplete(app);
    keystroke(app, 'F', {true, false});
    typeQuery(app, L"\x521d\x59cb");
    app.scrollY = app.targetScrollY = 0.0f;
    barFrame(app);
    const D2D1_RECT_F readerBar = searchBarDrawRect(app);
    check(cursorAt(app, middle(app.searchFields[0].textRect)) == beam, "pointers, reading: the beam over the find field");
    check(cursorAt(app, D2D1::Point2F(readerBar.left + dpi(app, 8), (readerBar.top + readerBar.bottom) / 2)) == arrow,
          "pointers, reading: the arrow over the bar's glass");
    check(cursorAt(app, middle(searchResultsButtonRect(app))) == hand, "pointers, reading: a hand over the results button");
    check(cursorAt(app, D2D1::Point2F(documentViewportX(app) + dpi(app, 120), readerBar.bottom + dpi(app, 150))) == arrow,
          "pointers, reading: the page keeps the arrow while the bar is open");
    closeBar(app);
}
}  // namespace

int runFindBarTests(const char* fixture) {
    HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 2;
    g_frames = fs::current_path() / "frames";
    fs::create_directories(g_frames);
    auto state = std::make_unique<App>();
    App& app = *state;
    app.hwnd = CreateWindowExW(0, L"STATIC", L"Find bar tests", WS_POPUP, 0, 0, 1050, 900, nullptr, nullptr, nullptr,
                               nullptr);
    if (!app.hwnd || !initD2D(app) || !createRenderTarget(app)) return 2;
    app.width = 1050;
    app.height = 900;
    updateTextFormats(app);
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);
    applyTheme(app, 0);
    check(openDocumentInViewer(app, fs::path(fixture).wstring()), "the fixture opens");
    ensureLayoutComplete(app);

    wordsInTheBar(app);

    if (app.editMode) exitEditMode(app);
    dragInReader(app);

    enterEditMode(app);
    app.editorShowPreview = false;
    if (editorLiveOn(app)) keystroke(app, VK_OEM_2, {true, false, false});
    check(app.editMode && !editorLiveOn(app), "the source editor opens");
    dragInEditor(app, "source");
    app.editorShowPreview = true;
    app.width = 1300;  // the source beside its preview
    dragInEditor(app, "source beside the preview");
    app.width = 1050;
    app.editorShowPreview = false;
    keystroke(app, VK_OEM_2, {true, false, false});
    check(editorLiveOn(app), "Ctrl+/ shows Live");
    dragInEditor(app, "Live");
    keystroke(app, VK_OEM_2, {true, false, false});

    pointers(app);

    app.editorDirty = false;
    DestroyWindow(app.hwnd);
    app.hwnd = nullptr;
    state.reset();
    CoUninitialize();
    OleUninitialize();
    std::cout << "Find bar: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
