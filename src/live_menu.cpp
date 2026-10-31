// The menu a right-click opens on live text (live_text.h, docs/live-editor.md,
// "The menu"): the formats, the clipboard, what the pointer is on, Turn
// into and Insert, with submenus that open beside their rows. Live and
// sheet cards open the same menu on their text.

// Its drawing is DirectWrite's and its rows are few: compiled for size
#pragma optimize("s", on)

#include "live_text.h"
#include "d2d_init.h"
#include "editor.h"
#include "i18n.h"
#include "signals.h"
#include "ui_paint.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace live {

namespace {

using Row = LiveText::MenuRow;
using Panel = LiveText::MenuPanel;
using Menu = LiveText::MenuState;

const int kStrip = FMT_LINK + 1;  // the formats row: bold to link
const int kGridCols = 8, kGridRows = 6;

// --- Rows ---------------------------------------------------------------------------

// A row onto a panel's list; one call for every row keeps the lists small
__declspec(noinline) void add(std::vector<Row>& rows, Row::Kind kind, int id, const wchar_t* label, const wchar_t* keys = L"",
                              bool enabled = true, int badge = -1, bool checked = false) {
    Row r;
    r.kind = kind;
    r.id = id;
    r.label = label;
    r.keys = keys;
    r.enabled = enabled;
    r.badge = badge;
    r.checked = checked;
    rows.push_back(std::move(r));
}

// A line between rows, never two together
__declspec(noinline) void separate(std::vector<Row>& rows) {
    if (!rows.empty() && rows.back().kind != Row::Separator) add(rows, Row::Separator, 0, L"");
}

bool usable(const Row& r) { return r.kind != Row::Separator && r.enabled; }

// A line of the properties at a document's top, between its --- lines
bool inProperties(const std::wstring& t, size_t ls) {
    if (t.compare(0, 4, L"---\n") != 0) return false;
    size_t close = t.find(L"\n---", 3);
    return close != std::wstring::npos && ls <= close + 1;
}

// The menu itself: what the pointer is on decides its middle
std::vector<Row> mainRows(App& app, const LiveText& e) {
    const Menu& M = e.menu;
    const std::wstring& t = e.text;
    const size_t at = std::min(M.at, t.size());
    const size_t ls = lineStart(t, at);
    const bool code = e.multiline && fenceState(t, ls) != 0;
    // Code, the properties and a one-line field have no Markdown to make
    const bool plain = !e.multiline || code || inProperties(t, ls);
    const Row::Kind A = Row::Action;
    std::vector<Row> rows;
    // The formats take words on one line, as the format bar does
    if (!plain) add(rows, Row::Formats, 0, L"", L"", t.find(L'\n', e.selStart()) >= e.selEnd());
    separate(rows);
    bool clip = IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                (M.canPasteMore && (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB)));
    add(rows, A, CMD_CUT, tr(app, "ectx.cut"), L"Ctrl+X", e.hasSelection());
    add(rows, A, CMD_COPY, tr(app, "ectx.copy"), L"Ctrl+C", e.hasSelection());
    add(rows, A, CMD_PASTE, tr(app, "ectx.paste"), L"Ctrl+V", clip);
    separate(rows);
    TableSpot table;
    if (!plain) {
        // A link or a picture under the pointer
        const LinkSpan link = linkAt(t, at);
        const bool wiki = link.kind == LinkSpan::Wiki, image = link.kind == LinkSpan::Image;
        if (image && M.canPickImage) add(rows, A, CMD_REPLACE_IMAGE, tr(app, "live.menu.replace_image"));
        if (link.kind != LinkSpan::None && !image && M.canOpen)
            add(rows, A, CMD_OPEN_LINK, tr(app, wiki ? "live.menu.open_note" : "live.menu.open_link"), L"Ctrl+Click");
        if (link.kind != LinkSpan::None && !wiki) add(rows, A, CMD_COPY_LINK, tr(app, image ? "live.menu.copy_image" : "live.menu.copy_link"));
        if (link.kind == LinkSpan::Inline) add(rows, A, CMD_EDIT_LINK, tr(app, "live.menu.edit_link"));
        if (link.kind == LinkSpan::Inline || wiki) add(rows, A, CMD_REMOVE_LINK, tr(app, "live.menu.remove_link"));
        separate(rows);
        // A task, a list item
        LineMark m = lineMark(t, ls + quoteLength(t, ls));
        if (m.kind == LineMark::Task) add(rows, A, CMD_TOGGLE_TASK, tr(app, m.done ? "live.menu.undone" : "live.menu.done"));
        if (m.kind == LineMark::Bullet || m.kind == LineMark::Number || m.kind == LineMark::Task) {
            add(rows, A, CMD_INDENT, tr(app, "live.menu.indent"), L"Tab");
            add(rows, A, CMD_OUTDENT, tr(app, "live.menu.outdent"), L"Shift+Tab", m.indent > 0);
        }
        separate(rows);
        // A table's rows and columns; its header, its delimiter row and a last column stay
        table = tableAt(t, at);
        if (table.in) {
            static const struct {
                int edit;
                const char* key;
            } edits[] = {
                {TABLE_ROW_ABOVE, "live.menu.row_above"},       {TABLE_ROW_BELOW, "live.menu.row_below"},
                {TABLE_COLUMN_LEFT, "live.menu.column_left"},   {TABLE_COLUMN_RIGHT, "live.menu.column_right"},
                {TABLE_DELETE_ROW, "live.menu.delete_row"},     {TABLE_DELETE_COLUMN, "live.menu.delete_column"},
            };
            for (const auto& edit : edits) {
                bool can = edit.edit == TABLE_DELETE_COLUMN ? table.columns > 1
                           : edit.edit == TABLE_ROW_ABOVE || edit.edit == TABLE_DELETE_ROW ? table.index >= 2
                                                                                            : true;
                add(rows, A, CMD_TABLE_EDIT + edit.edit, tr(app, edit.key), L"", can);
            }
            add(rows, Row::Submenu, SUB_ALIGN, tr(app, "live.menu.align"));
            separate(rows);
        }
    }
    if (code) {
        add(rows, A, CMD_COPY_CODE, tr(app, "live.menu.copy_code"));
        separate(rows);
    }
    if (!plain) {
        // A table's rows stay table rows
        if (!table.in) add(rows, Row::Submenu, SUB_TURN, tr(app, "live.menu.turn_into"));
        add(rows, Row::Submenu, SUB_INSERT, tr(app, "live.menu.insert"));
        separate(rows);
    }
    add(rows, A, CMD_SELECT_ALL, tr(app, "live.menu.select_all"), L"Ctrl+A");
    return rows;
}

std::vector<Row> rowsFor(App& app, const LiveText& e, int sub) {
    if (sub == SUB_MENU) return mainRows(app, e);
    std::vector<Row> rows;
    // Inserts, each with the slash menu's picture; nothing for a line between groups
    auto inserts = [&](std::initializer_list<std::pair<int, const char*>> items) {
        for (const auto& item : items) {
            if (!item.second) separate(rows);
            else add(rows, Row::Action, CMD_INSERT + item.first, tr(app, item.second), L"", true, item.first);
        }
    };
    switch (sub) {
        case SUB_TURN: {
            // What the caret's line is now, ticked
            static const struct {
                int block;
                const char* key;
                int badge;
            } kinds[] = {
                {BLOCK_TEXT, "live.menu.text", kBadgeText},          {BLOCK_H1, "live.insert.h1", INS_H1},
                {BLOCK_H2, "live.insert.h2", INS_H2},                {BLOCK_H3, "live.insert.h3", INS_H3},
                {BLOCK_QUOTE, "live.insert.quote", INS_QUOTE},       {BLOCK_BULLETS, "live.insert.bullets", INS_BULLETS},
                {BLOCK_NUMBERS, "live.insert.numbers", INS_NUMBERS}, {BLOCK_TASKS, "live.insert.tasks", INS_TASKS},
                {BLOCK_CODE, "live.insert.code", INS_CODE},
            };
            int now = blockAt(e.text, e.selStart());
            for (const auto& k : kinds) add(rows, Row::Action, CMD_TURN + k.block, tr(app, k.key), L"", true, k.badge, k.block == now);
            break;
        }
        case SUB_INSERT:
            add(rows, Row::Submenu, SUB_TABLE, tr(app, "live.insert.table"), L"", true, INS_TABLE);
            inserts({{INS_IMAGE, "live.insert.image"}, {INS_LINK, "live.insert.link"}, {INS_FOOTNOTE, "live.insert.footnote"},
                     {INS_DIVIDER, "live.insert.divider"}, {0, nullptr}, {INS_CODE, "live.insert.code"}, {INS_MATH, "live.insert.math"},
                     {INS_INLINE_MATH, "live.insert.inline_math"}, {0, nullptr}});
            add(rows, Row::Submenu, SUB_CALLOUT, tr(app, "live.menu.callout"), L"", true, INS_NOTE);
            add(rows, Row::Submenu, SUB_DIAGRAM, tr(app, "ectx.diagram"), L"", true, INS_FLOWCHART);
            inserts({{0, nullptr}, {INS_PROPERTIES, "live.insert.properties"}});
            break;
        case SUB_TABLE:
            add(rows, Row::Grid, 0, L"");
            break;
        case SUB_CALLOUT:
            inserts({{INS_NOTE, "live.insert.note"}, {INS_TIP, "live.insert.tip"}, {INS_IMPORTANT, "live.insert.important"},
                     {INS_WARNING, "live.insert.warning"}, {INS_CAUTION, "live.insert.caution"}});
            break;
        case SUB_DIAGRAM:
            inserts({{INS_FLOWCHART, "live.insert.flowchart"}, {INS_SEQUENCE, "live.insert.sequence"}, {INS_CLASS, "live.insert.class"},
                     {INS_STATE, "live.insert.state"}, {INS_GANTT, "live.insert.gantt"}, {INS_PIE, "live.insert.pie"},
                     {INS_MINDMAP, "live.insert.mindmap"}, {INS_PLANTUML, "live.insert.plantuml"}});
            break;
        case SUB_ALIGN: {
            // The column's alignment now, ticked
            int align = tableAt(e.text, std::min(e.menu.at, e.text.size())).align;
            add(rows, Row::Action, CMD_TABLE_EDIT + TABLE_ALIGN_LEFT, tr(app, "live.menu.align_left"), L"", true, kBadgeAlignLeft, align == 1);
            add(rows, Row::Action, CMD_TABLE_EDIT + TABLE_ALIGN_CENTER, tr(app, "live.menu.align_center"), L"", true, kBadgeAlignCenter, align == 2);
            add(rows, Row::Action, CMD_TABLE_EDIT + TABLE_ALIGN_RIGHT, tr(app, "live.menu.align_right"), L"", true, kBadgeAlignRight, align == 3);
            break;
        }
        default:
            break;
    }
    return rows;
}

// --- Where things are -------------------------------------------------------------------

float rowHeight(App& app, const Row& r) {
    switch (r.kind) {
        case Row::Separator: return dpi(app, 9.0f);
        case Row::Formats: return dpi(app, 40.0f);
        case Row::Grid: return dpi(app, 16.0f + kGridRows * 18.0f + (kGridRows - 1) * 3.0f + 24.0f);
        default: return dpi(app, 30.0f);
    }
}

// The grid's first cell, its cells' side and the gap between them
void gridCells(App& app, const D2D1_RECT_F& row, float& x0, float& y0, float& cell, float& gap) {
    cell = dpi(app, 18.0f);
    gap = dpi(app, 3.0f);
    x0 = row.left + dpi(app, 8.0f);
    y0 = row.top + dpi(app, 8.0f);
}

// The table size under a point of the grid
bool gridSizeAt(App& app, const D2D1_RECT_F& row, float x, float y, int& cols, int& rows) {
    float x0 = 0, y0 = 0, cell = 0, gap = 0;
    gridCells(app, row, x0, y0, cell, gap);
    const float step = cell + gap;
    if (x < x0 - gap || y < y0 - gap || x > x0 + kGridCols * step || y > y0 + kGridRows * step) return false;
    cols = std::clamp((int)((x - x0) / step) + 1, 1, kGridCols);
    rows = std::clamp((int)((y - y0) / step) + 1, 1, kGridRows);
    return true;
}

float textWidth(App& app, const std::wstring& text, IDWriteTextFormat* format) {
    IDWriteTextLayout* l = paint::makeLayout(app, text, format, 2000.0f, 200.0f);
    float w = paint::metrics(l).width;
    if (l) l->Release();
    return w;
}

void addPanel(App& app, LiveText& e, int sub, int parentRow) {
    Panel P;
    P.sub = sub;
    P.parentRow = parentRow;
    P.rows = rowsFor(app, e, sub);
    if (sub == SUB_TABLE) {
        P.width = dpi(app, 8.0f + 16.0f + kGridCols * 18.0f + (kGridCols - 1) * 3.0f);
    } else {
        // As wide as its widest row: badge, words, and shortcut or arrow
        IDWriteTextFormat* label = paint::textFormat(app, paint::uiFace(), dpi(app, 13.0f), DWRITE_FONT_WEIGHT_NORMAL, false);
        IDWriteTextFormat* keys = paint::textFormat(app, paint::uiFace(), dpi(app, 11.5f), DWRITE_FONT_WEIGHT_NORMAL, false);
        float widest = 0;
        for (const Row& r : P.rows) {
            if (r.kind != Row::Action && r.kind != Row::Submenu) continue;
            float w = dpi(app, 20.0f) + (r.badge >= 0 ? dpi(app, 29.0f) : 0.0f) + textWidth(app, r.label, label) + dpi(app, 30.0f);
            if (!r.keys.empty()) w += textWidth(app, r.keys, keys) + dpi(app, 12.0f);
            widest = std::max(widest, w);
        }
        P.width = std::clamp(widest + dpi(app, 8.0f), dpi(app, sub == SUB_MENU ? 244.0f : 200.0f), dpi(app, 420.0f));
    }
    e.menu.panels.push_back(std::move(P));
}

// Each panel's box and rows: the menu below where it opened (or above it,
// with no room), each submenu beside its row, all inside the bounds
void layout(App& app, LiveText& e) {
    Menu& M = e.menu;
    const float pad = dpi(app, 4.0f), edge = dpi(app, 4.0f), gap = dpi(app, 2.0f);
    const D2D1_RECT_F& b = M.bounds;
    for (size_t k = 0; k < M.panels.size(); ++k) {
        Panel& P = M.panels[k];
        float w = P.width, h = pad * 2;
        for (const Row& r : P.rows) h += rowHeight(app, r);
        float x = 0, y = 0;
        if (k == 0) {
            x = M.x + gap;
            if (x + w > b.right - edge) x = M.x - w - gap;
            y = M.bottom + gap;
            if (y + h > b.bottom - edge) y = M.top - h - gap;
        } else {
            const Panel& parent = M.panels[k - 1];
            x = parent.box.right - gap;
            if (x + w > b.right - edge) x = parent.box.left - w + gap;
            y = parent.rowBoxes[(size_t)P.parentRow].top - pad;
        }
        x = std::max(b.left + edge, std::min(x, b.right - w - edge));
        y = std::max(b.top + edge, std::min(y, b.bottom - h - edge));
        P.box = D2D1::RectF(x, y, x + w, y + h);
        P.rowBoxes.clear();
        P.buttonBoxes.clear();
        float top = y + pad;
        for (const Row& r : P.rows) {
            float rh = rowHeight(app, r);
            D2D1_RECT_F row = D2D1::RectF(x + pad, top, x + w - pad, top + rh);
            P.rowBoxes.push_back(row);
            if (r.kind == Row::Formats) {
                float bw = (row.right - row.left) / kStrip;
                for (int f = 0; f < kStrip; ++f) {
                    P.buttonBoxes.push_back(D2D1::RectF(row.left + bw * f + dpi(app, 1.0f), row.top + dpi(app, 4.0f),
                                                        row.left + bw * (f + 1) - dpi(app, 1.0f), row.bottom - dpi(app, 4.0f)));
                }
            }
            top += rh;
        }
    }
}

bool inside(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

int panelAt(const Menu& M, float x, float y) {
    for (int k = (int)M.panels.size() - 1; k >= 0; --k) {
        if (inside(M.panels[k].box, x, y)) return k;
    }
    return -1;
}

int rowAt(const Panel& P, float x, float y) {
    for (size_t i = 0; i < P.rows.size() && i < P.rowBoxes.size(); ++i) {
        if (P.rows[i].kind != Row::Separator && inside(P.rowBoxes[i], x, y)) return (int)i;
    }
    return -1;
}

int buttonAt(const Panel& P, float x, float y) {
    for (size_t f = 0; f < P.buttonBoxes.size(); ++f) {
        if (inside(P.buttonBoxes[f], x, y)) return (int)f;
    }
    return -1;
}

// The next row the keys can light, going down (or up), round past the ends
int nextRow(const Panel& P, int from, int dir) {
    const int n = (int)P.rows.size();
    int r = from;
    for (int i = 0; i < n; ++i) {
        r = r < 0 ? (dir > 0 ? 0 : n - 1) : (r + dir + n) % n;
        if (usable(P.rows[r])) return r;
    }
    return -1;
}

// Whether the pointer, going from `a` to `b`, heads for a submenu: it keeps
// inside the triangle between where it was and the submenu's near side
bool heading(D2D1_POINT_2F a, D2D1_POINT_2F b, const D2D1_RECT_F& box) {
    if (a.x < 0) return false;
    float nearX = box.left >= a.x ? box.left : box.right;
    if ((nearX - a.x) * (b.x - a.x) <= 0) return false;
    auto side = [](D2D1_POINT_2F p, D2D1_POINT_2F q, D2D1_POINT_2F r) {
        return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
    };
    D2D1_POINT_2F top = D2D1::Point2F(nearX, box.top), bottom = D2D1::Point2F(nearX, box.bottom);
    float d1 = side(a, top, b), d2 = side(top, bottom, b), d3 = side(bottom, a, b);
    bool negative = d1 < 0 || d2 < 0 || d3 < 0, positive = d1 > 0 || d2 > 0 || d3 > 0;
    return !(negative && positive);
}

// --- Commands -----------------------------------------------------------------------

void insertFromMenu(LiveText& e, int insert, const Host& host, int cols, int rows) {
    // Over a selection a link takes it as its words and maths goes around it;
    // a block goes after it
    if (e.hasSelection()) {
        if (insert == INS_LINK) {
            makeLink(e);
            return;
        }
        if (insert == INS_INLINE_MATH) {
            toggleWrap(e, L"$");
            return;
        }
        e.anchor = e.caret = e.selEnd();
    }
    // A block asked for in a table goes under it
    const bool inLine = insert == INS_LINK || insert == INS_IMAGE || insert == INS_INLINE_MATH || insert == INS_FOOTNOTE;
    if (!inLine) {
        TableSpot s = tableAt(e.text, e.caret);
        if (s.in) e.caret = e.anchor = lineEnd(e.text, s.last);
    }
    std::wstring image;
    if (insert == INS_IMAGE && host.pickImage) {
        image = host.pickImage();
        if (image.empty()) return;
    }
    applyInsert(e, insert, image, cols, rows);
}

// --- Drawing ------------------------------------------------------------------------

void drawGrid(App& app, const Menu& M, const D2D1_RECT_F& row, const paint::Surface& s) {
    float x0 = 0, y0 = 0, cell = 0, gap = 0;
    gridCells(app, row, x0, y0, cell, gap);
    for (int r = 0; r < kGridRows; ++r) {
        for (int c = 0; c < kGridCols; ++c) {
            bool in = r < M.rows && c < M.cols;
            D2D1_RECT_F box = D2D1::RectF(x0 + c * (cell + gap), y0 + r * (cell + gap), x0 + c * (cell + gap) + cell,
                                          y0 + r * (cell + gap) + cell);
            paint::fillRound(app, box, dpi(app, 3.0f), in ? paint::alpha(s.accent, 0.30f) : paint::alpha(s.text, 0.05f));
            paint::strokeRound(app, box, dpi(app, 3.0f), in ? paint::alpha(s.accent, 0.65f) : paint::alpha(s.text, 0.14f),
                               dpi(app, 1.0f));
        }
    }
    // Its size: the columns, and the rows under the header
    wchar_t size[64];
    swprintf_s(size, L"%d \x00D7 %d %ls", M.cols, M.rows, tr(app, "ectx.table_word"));
    IDWriteTextLayout* l = paint::makeLayout(app, size, paint::textFormat(app, paint::uiFace(), dpi(app, 12.0f), DWRITE_FONT_WEIGHT_NORMAL, false),
                                             row.right - row.left, dpi(app, 24.0f));
    DWRITE_TEXT_METRICS m = paint::metrics(l);
    paint::drawText(app, l, (row.left + row.right - m.width) * 0.5f, y0 + kGridRows * (cell + gap) + dpi(app, 3.0f), s.text);
    if (l) l->Release();
}

// The lit format button's name and shortcut, just off the menu
void drawTip(App& app, const Menu& M, const paint::Surface& s) {
    const Panel& P = M.panels[0];
    if (P.focus < 0 || P.rows[P.focus].kind != Row::Formats || P.button < 0 || P.button >= (int)P.buttonBoxes.size()) return;
    static const char* names[kStrip] = {"rail.bold", "rail.italic", "rail.strike", "rail.code", "live.menu.highlight", "rail.link"};
    static const wchar_t* shortcuts[kStrip] = {L"Ctrl+B", L"Ctrl+I", L"", L"", L"", L"Ctrl+K"};
    IDWriteTextFormat* format = paint::textFormat(app, paint::uiFace(), dpi(app, 12.0f), DWRITE_FONT_WEIGHT_NORMAL, false);
    IDWriteTextLayout* name = paint::makeLayout(app, tr(app, names[P.button]), format, dpi(app, 300.0f), dpi(app, 30.0f));
    IDWriteTextLayout* keys = *shortcuts[P.button] ? paint::makeLayout(app, shortcuts[P.button], format, dpi(app, 120.0f), dpi(app, 30.0f)) : nullptr;
    DWRITE_TEXT_METRICS nm = paint::metrics(name), km = paint::metrics(keys);
    const float padX = dpi(app, 9.0f), padY = dpi(app, 5.0f), space = keys ? dpi(app, 10.0f) : 0.0f;
    float w = nm.width + space + km.width + padX * 2, h = std::max(nm.height, km.height) + padY * 2;
    const D2D1_RECT_F& button = P.buttonBoxes[(size_t)P.button];
    float x = std::clamp((button.left + button.right - w) * 0.5f, M.bounds.left + dpi(app, 4.0f), std::max(M.bounds.left + dpi(app, 4.0f), M.bounds.right - w - dpi(app, 4.0f)));
    float y = P.box.top - h - dpi(app, 6.0f);
    if (y < M.bounds.top + dpi(app, 2.0f)) y = P.box.bottom + dpi(app, 6.0f);
    D2D1_RECT_F box = D2D1::RectF(x, y, x + w, y + h);
    paint::shadow(app, box, dpi(app, 6.0f), s.shadow, 1.0f);
    paint::fillRound(app, box, dpi(app, 6.0f), s.fill);
    paint::strokeRound(app, box, dpi(app, 6.0f), s.border, dpi(app, 1.0f));
    paint::drawText(app, name, x + padX, y + padY, s.text);
    if (keys) paint::drawText(app, keys, x + padX + nm.width + space, y + padY, paint::alpha(s.text, 0.5f));
    if (name) name->Release();
    if (keys) keys->Release();
}

}  // namespace

// --- The menu -----------------------------------------------------------------------

void openMenu(App& app, LiveText& e, float x, float top, float bottom, size_t at, const D2D1_RECT_F& bounds, const Host& host,
              bool byKeys) {
    closePopup(e);
    Menu& M = e.menu;
    M = LiveText::MenuState{};
    M.open = true;
    M.x = x;
    M.top = top;
    M.bottom = bottom;
    M.bounds = bounds;
    M.at = std::min(at, e.text.size());
    M.keys = byKeys;
    M.canOpen = (bool)host.openLink;
    M.canPickImage = (bool)host.pickImage;
    M.canPasteMore = (bool)host.paste;
    addPanel(app, e, SUB_MENU, -1);
    if (byKeys) {
        // The first row with words lit; the formats are a key up from it
        Panel& P = M.panels[0];
        P.focus = nextRow(P, -1, 1);
        if (P.focus >= 0 && P.rows[P.focus].kind == Row::Formats) P.focus = nextRow(P, P.focus, 1);
    }
}

void openMenuPanel(App& app, LiveText& e, float x, float y, int sub, const D2D1_RECT_F& bounds, const Host& host) {
    openMenu(app, e, x, y, y, e.caret, bounds, host, false);
    e.menu.panels.clear();
    addPanel(app, e, sub, -1);
}

void closeMenu(LiveText& e) {
    e.menu.open = false;
    e.menu.panels.clear();
}

void drawMenu(App& app, LiveText& e) {
    Menu& M = e.menu;
    if (!M.open || M.panels.empty()) return;
    layout(app, e);
    paint::Surface s = paint::surface(app);
    IDWriteTextFormat* labelFormat = paint::textFormat(app, paint::uiFace(), dpi(app, 13.0f), DWRITE_FONT_WEIGHT_NORMAL, false);
    IDWriteTextFormat* keysFormat = paint::textFormat(app, paint::uiFace(), dpi(app, 11.5f), DWRITE_FONT_WEIGHT_NORMAL, false);
    const float radius = dpi(app, 8.0f), u = dpi(app, 1.0f);
    for (size_t k = 0; k < M.panels.size(); ++k) {
        const Panel& P = M.panels[k];
        paint::shadow(app, P.box, radius, s.shadow, 1.5f);
        paint::fillRound(app, P.box, radius, s.fill);
        paint::strokeRound(app, P.box, radius, s.border, u);
        // The row a submenu is open from stays lit
        const int leads = k + 1 < M.panels.size() ? M.panels[k + 1].parentRow : -1;
        for (size_t i = 0; i < P.rows.size(); ++i) {
            const Row& row = P.rows[i];
            const D2D1_RECT_F& r = P.rowBoxes[i];
            if (row.kind == Row::Separator) {
                float y = std::floor((r.top + r.bottom) * 0.5f) + 0.5f;
                paint::line(app, D2D1::Point2F(r.left + dpi(app, 6.0f), y), D2D1::Point2F(r.right - dpi(app, 6.0f), y), s.border, u);
                continue;
            }
            if (row.kind == Row::Formats) {
                for (int f = 0; f < (int)P.buttonBoxes.size(); ++f) {
                    drawFormatButton(app, f, P.buttonBoxes[(size_t)f], formatOn(e, f), (int)i == P.focus && P.button == f, row.enabled);
                }
                continue;
            }
            if (row.kind == Row::Grid) {
                drawGrid(app, M, r, s);
                continue;
            }
            const bool lit = row.enabled && ((int)i == P.focus || (int)i == leads);
            if (lit) paint::fillRound(app, r, dpi(app, 5.0f), paint::alpha(s.accent, (int)i == P.focus ? 0.16f : 0.10f));
            const D2D1_COLOR_F ink = row.enabled ? s.text : paint::alpha(s.text, 0.38f);
            float x = r.left + dpi(app, 10.0f);
            if (row.badge >= 0) {
                const float b = dpi(app, 20.0f);
                D2D1_RECT_F mark = D2D1::RectF(r.left + dpi(app, 6.0f), (r.top + r.bottom - b) * 0.5f, r.left + dpi(app, 6.0f) + b,
                                               (r.top + r.bottom + b) * 0.5f);
                drawBadge(app, row.badge, mark);
                x = mark.right + dpi(app, 9.0f);
            }
            // On the right: a submenu's arrow, a tick, or the shortcut
            float right = r.right - dpi(app, 10.0f);
            const float cy = (r.top + r.bottom) * 0.5f;
            if (row.kind == Row::Submenu) {
                const float cx = right - 2 * u;
                D2D1_COLOR_F arrow = paint::alpha(ink, 0.6f);
                paint::line(app, D2D1::Point2F(cx - 3 * u, cy - 4 * u), D2D1::Point2F(cx + 1 * u, cy), arrow, 1.3f * u, paint::roundStyle(app));
                paint::line(app, D2D1::Point2F(cx + 1 * u, cy), D2D1::Point2F(cx - 3 * u, cy + 4 * u), arrow, 1.3f * u, paint::roundStyle(app));
                right -= dpi(app, 14.0f);
            } else if (row.checked) {
                const float cx = right - 5 * u;
                paint::line(app, D2D1::Point2F(cx - 5 * u, cy), D2D1::Point2F(cx - 1.5f * u, cy + 3.5f * u), s.accent, 1.6f * u, paint::roundStyle(app));
                paint::line(app, D2D1::Point2F(cx - 1.5f * u, cy + 3.5f * u), D2D1::Point2F(cx + 5 * u, cy - 3.5f * u), s.accent, 1.6f * u,
                            paint::roundStyle(app));
                right -= dpi(app, 18.0f);
            } else if (!row.keys.empty()) {
                IDWriteTextLayout* kl = paint::makeLayout(app, row.keys, keysFormat, dpi(app, 200.0f), r.bottom - r.top);
                DWRITE_TEXT_METRICS km = paint::metrics(kl);
                paint::drawText(app, kl, right - km.width, cy - km.height * 0.5f, paint::alpha(s.text, row.enabled ? 0.45f : 0.25f));
                if (kl) kl->Release();
                right -= km.width + dpi(app, 12.0f);
            }
            IDWriteTextLayout* ll = paint::makeLayout(app, row.label, labelFormat, std::max(1.0f, right - x), r.bottom - r.top);
            DWRITE_TEXT_METRICS lm = paint::metrics(ll);
            paint::drawText(app, ll, x, cy - lm.height * 0.5f, ink);
            if (ll) ll->Release();
        }
    }
    drawTip(app, M, s);
}

bool menuHover(App& app, LiveText& e, float x, float y) {
    Menu& M = e.menu;
    if (!M.open || M.panels.empty()) return false;
    layout(app, e);
    const D2D1_POINT_2F was = M.pointer, now = D2D1::Point2F(x, y);
    M.pointer = now;
    if (M.keys) {
        // Lit by the keys: the pointer takes over once it really moves
        if (was.x < 0 || (std::fabs(was.x - x) < 0.5f && std::fabs(was.y - y) < 0.5f)) return false;
        M.keys = false;
    }
    const int k = panelAt(M, x, y);
    if (k < 0) {
        // Off the menu: the last panel's light goes, but not off the row its submenu hangs from
        Panel& last = M.panels.back();
        if (last.focus >= 0 && last.rows[(size_t)last.focus].kind != Row::Submenu) {
            last.focus = last.button = -1;
            return true;
        }
        return false;
    }
    const int r = rowAt(M.panels[(size_t)k], x, y);
    // On the way to the submenu beside this panel, the rows crossed leave it open
    if (k + 1 < (int)M.panels.size() && r != M.panels[(size_t)k + 1].parentRow && heading(was, now, M.panels[(size_t)k + 1].box)) return false;
    bool changed = false;
    const int focus = r >= 0 && usable(M.panels[(size_t)k].rows[(size_t)r]) ? r : -1;
    if (focus != M.panels[(size_t)k].focus) {
        M.panels[(size_t)k].focus = focus;
        changed = true;
    }
    if (focus >= 0) {
        const Row row = M.panels[(size_t)k].rows[(size_t)focus];
        if (row.kind == Row::Submenu) {
            // Its submenu opens; back on its row, the submenu's own light goes
            if (k + 1 < (int)M.panels.size() && M.panels[(size_t)k + 1].parentRow == focus) {
                if (M.panels.size() > (size_t)k + 2) {
                    M.panels.resize((size_t)k + 2);
                    changed = true;
                }
                if (M.panels[(size_t)k + 1].focus >= 0) {
                    M.panels[(size_t)k + 1].focus = M.panels[(size_t)k + 1].button = -1;
                    changed = true;
                }
            } else {
                M.panels.resize((size_t)k + 1);
                addPanel(app, e, row.id, focus);
                layout(app, e);
                changed = true;
            }
        } else {
            if (M.panels.size() > (size_t)k + 1) {
                M.panels.resize((size_t)k + 1);
                changed = true;
            }
            Panel& P = M.panels[(size_t)k];
            if (row.kind == Row::Formats) {
                int button = buttonAt(P, x, y);
                if (button != P.button) {
                    P.button = button;
                    changed = true;
                }
            } else if (row.kind == Row::Grid) {
                int cols = M.cols, rows = M.rows;
                if (gridSizeAt(app, P.rowBoxes[(size_t)focus], x, y, cols, rows) && (cols != M.cols || rows != M.rows)) {
                    M.cols = cols;
                    M.rows = rows;
                    changed = true;
                }
            }
        }
    } else if (r >= 0 && M.panels.size() > (size_t)k + 1) {
        // A row that does nothing: the submenus beside it close
        M.panels.resize((size_t)k + 1);
        changed = true;
    }
    return changed;
}

MenuResult menuPress(App& app, HWND hwnd, LiveText& e, float x, float y, const Host& host) {
    Menu& M = e.menu;
    if (!M.open || M.panels.empty()) return MenuResult::None;
    layout(app, e);
    const int k = panelAt(M, x, y);
    if (k < 0) {
        closeMenu(e);
        return MenuResult::Closed;
    }
    const int r = rowAt(M.panels[(size_t)k], x, y);
    if (r < 0 || !usable(M.panels[(size_t)k].rows[(size_t)r])) return MenuResult::Kept;
    const Row row = M.panels[(size_t)k].rows[(size_t)r];
    M.keys = false;
    switch (row.kind) {
        case Row::Submenu:
            M.panels[(size_t)k].focus = r;
            if (!(k + 1 < (int)M.panels.size() && M.panels[(size_t)k + 1].parentRow == r)) {
                M.panels.resize((size_t)k + 1);
                addPanel(app, e, row.id, r);
            }
            return MenuResult::Kept;
        case Row::Formats: {
            int button = buttonAt(M.panels[(size_t)k], x, y);
            if (button < 0) return MenuResult::Kept;
            menuCommand(app, hwnd, e, CMD_FORMAT + button, host);
            return MenuResult::Ran;
        }
        case Row::Grid: {
            int cols = M.cols, rows = M.rows;
            if (!gridSizeAt(app, M.panels[(size_t)k].rowBoxes[(size_t)r], x, y, cols, rows)) return MenuResult::Kept;
            M.cols = cols;
            M.rows = rows;
            menuCommand(app, hwnd, e, CMD_TABLE, host);
            return MenuResult::Ran;
        }
        default:
            menuCommand(app, hwnd, e, row.id, host);
            return MenuResult::Ran;
    }
}

MenuResult menuKey(App& app, HWND hwnd, LiveText& e, WPARAM key, const Host& host) {
    Menu& M = e.menu;
    if (!M.open || M.panels.empty()) return MenuResult::None;
    layout(app, e);
    M.keys = true;
    const size_t k = M.panels.size() - 1;
    const int focus = M.panels[k].focus;
    const bool grid = M.panels[k].sub == SUB_TABLE;
    const bool onFormats = focus >= 0 && M.panels[k].rows[(size_t)focus].kind == Row::Formats;
    // The last panel goes, or with the menu alone, the menu
    auto back = [&] {
        if (M.panels.size() > 1) {
            M.panels.pop_back();
            return MenuResult::Kept;
        }
        closeMenu(e);
        return MenuResult::Closed;
    };
    // The lit row: a submenu opens with its first row lit, anything else runs
    auto enter = [&](int r) {
        if (r < 0) return MenuResult::Kept;
        const Row row = M.panels[k].rows[(size_t)r];
        switch (row.kind) {
            case Row::Submenu: {
                addPanel(app, e, row.id, r);
                Panel& S = M.panels.back();
                S.focus = nextRow(S, -1, 1);
                if (S.focus >= 0 && S.rows[(size_t)S.focus].kind == Row::Formats) S.button = 0;
                return MenuResult::Kept;
            }
            case Row::Formats:
                if (M.panels[k].button < 0) return MenuResult::Kept;
                menuCommand(app, hwnd, e, CMD_FORMAT + M.panels[k].button, host);
                return MenuResult::Ran;
            case Row::Grid:
                menuCommand(app, hwnd, e, CMD_TABLE, host);
                return MenuResult::Ran;
            default:
                menuCommand(app, hwnd, e, row.id, host);
                return MenuResult::Ran;
        }
    };
    Panel& P = M.panels[k];
    switch (key) {
        case VK_ESCAPE:
            return back();
        case VK_APPS:
        case VK_F10:
            closeMenu(e);
            return MenuResult::Closed;
        case VK_UP:
        case VK_DOWN:
            if (grid) {
                M.rows = std::clamp(M.rows + (key == VK_DOWN ? 1 : -1), 1, kGridRows);
            } else {
                P.focus = nextRow(P, focus, key == VK_DOWN ? 1 : -1);
                if (P.focus >= 0 && P.rows[(size_t)P.focus].kind == Row::Formats && P.button < 0) P.button = 0;
            }
            return MenuResult::Kept;
        case VK_HOME:
        case VK_END:
            if (!grid) P.focus = nextRow(P, -1, key == VK_HOME ? 1 : -1);
            if (P.focus >= 0 && P.rows[(size_t)P.focus].kind == Row::Formats && P.button < 0) P.button = 0;
            return MenuResult::Kept;
        case VK_LEFT:
            if (grid) {
                if (M.cols > 1) {
                    --M.cols;
                    return MenuResult::Kept;
                }
                return back();
            }
            if (onFormats && P.button > 0) {
                --P.button;
                return MenuResult::Kept;
            }
            return M.panels.size() > 1 ? back() : MenuResult::Kept;
        case VK_RIGHT:
            if (grid) {
                M.cols = std::min(M.cols + 1, kGridCols);
                return MenuResult::Kept;
            }
            if (onFormats) {
                P.button = std::min(P.button + 1, kStrip - 1);
                return MenuResult::Kept;
            }
            if (focus >= 0 && P.rows[(size_t)focus].kind == Row::Submenu) return enter(focus);
            return MenuResult::Kept;
        case VK_RETURN:
        case VK_SPACE:
            return enter(grid ? 0 : focus);
        default:
            // A letter lights the next row it starts
            if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) {
                const int n = (int)P.rows.size();
                for (int i = 1; i <= n; ++i) {
                    int r = ((focus < 0 ? n - 1 : focus) + i) % n;
                    const Row& row = P.rows[(size_t)r];
                    if (usable(row) && !row.label.empty() && (wchar_t)towupper(row.label[0]) == (wchar_t)key) {
                        P.focus = r;
                        break;
                    }
                }
            }
            return MenuResult::Kept;
    }
}

void menuCommand(App& app, HWND hwnd, LiveText& e, int command, const Host& host) {
    const size_t at = std::min(e.menu.at, e.text.size());
    const int cols = e.menu.cols, rows = e.menu.rows;
    closeMenu(e);
    if (command >= CMD_TABLE_EDIT) {
        editTable(e, at, command - CMD_TABLE_EDIT);
        return;
    }
    if (command >= CMD_TURN) {
        turnInto(e, command - CMD_TURN);
        return;
    }
    if (command >= CMD_INSERT) {
        insertFromMenu(e, command - CMD_INSERT, host, cols, rows);
        return;
    }
    if (command >= CMD_FORMAT) {
        applyFormat(e, command - CMD_FORMAT);
        return;
    }
    // A copy the reader cannot see happen says so
    auto copy = [&](const std::wstring& text, bool say) {
        bool held = false;
        bool ok = copyToClipboard(hwnd, text, &held);
        if (ok && say) signalPushKey(app, SIG_INFO, SIGI_COPY, "toast.copied");
        else if (!ok && held) signalClipboardBusy(app);
        return ok;
    };
    const std::wstring& t = e.text;
    const LinkSpan link = linkAt(t, at);
    const std::wstring address = link.kind == LinkSpan::None ? std::wstring() : t.substr(link.addressFrom, link.addressTo - link.addressFrom);
    switch (command) {
        case CMD_CUT:
            if (e.hasSelection() && copy(t.substr(e.selStart(), e.selEnd() - e.selStart()), false)) replace(e, L"");
            break;
        case CMD_COPY:
            if (e.hasSelection()) copy(t.substr(e.selStart(), e.selEnd() - e.selStart()), false);
            break;
        case CMD_PASTE:
            if (!(host.paste && host.paste())) paste(e, readClipboardText(hwnd));
            break;
        case CMD_SELECT_ALL:
            e.anchor = 0;
            e.caret = t.size();
            break;
        case CMD_OPEN_LINK:
            if (host.openLink && !address.empty()) host.openLink(link.kind == LinkSpan::Wiki ? "wiki:" + toUtf8(address) : toUtf8(address));
            break;
        case CMD_COPY_LINK:
            if (!address.empty()) copy(address, true);
            break;
        case CMD_EDIT_LINK:
            // The address selected, to type a new one over
            if (link.kind != LinkSpan::None) {
                e.anchor = link.addressFrom;
                e.caret = link.addressTo;
                e.preferredX = -1;
            }
            break;
        case CMD_REMOVE_LINK:
            unlink(e, at);
            break;
        case CMD_REPLACE_IMAGE:
            if (host.pickImage) {
                std::wstring picked = host.pickImage();
                if (!picked.empty()) relink(e, at, picked);
            }
            break;
        case CMD_TOGGLE_TASK: {
            size_t ls = lineStart(t, at);
            toggleTask(e, ls + quoteLength(t, ls));
            break;
        }
        case CMD_INDENT:
        case CMD_OUTDENT:
            indent(e, command == CMD_OUTDENT);
            break;
        case CMD_COPY_CODE: {
            size_t from = 0, to = 0;
            if (fenceAround(t, at, from, to)) copy(codeCopyText(t.substr(from, to - from)), true);
            break;
        }
        case CMD_TABLE:
            insertFromMenu(e, INS_TABLE, host, cols, rows);
            break;
        default:
            break;
    }
}

}  // namespace live
