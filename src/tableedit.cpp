#include "tableedit.h"
#include "editor.h"
#include "utils.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

// --- Offset mapping -------------------------------------------------
// Source offsets from the parser are bytes into toUtf8(editorText);
// edits happen in wide indices. One linear walk maps between them.

size_t utf8LenOfRange(const std::wstring& text, size_t wideEnd) {
    size_t bytes = 0;
    for (size_t i = 0; i < wideEnd && i < text.size(); i++) {
        wchar_t c = text[i];
        if (c < 0x80) bytes += 1;
        else if (c < 0x800) bytes += 2;
        else if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size()) {
            bytes += 4;
            i++;
        } else bytes += 3;
    }
    return bytes;
}

size_t wideIndexForUtf8(const std::wstring& text, size_t byteTarget) {
    size_t bytes = 0;
    for (size_t i = 0; i < text.size(); i++) {
        if (bytes >= byteTarget) return i;
        wchar_t c = text[i];
        if (c < 0x80) bytes += 1;
        else if (c < 0x800) bytes += 2;
        else if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size()) {
            bytes += 4;
            i++;
        } else bytes += 3;
    }
    return text.size();
}

// --- Pipe-table source parsing --------------------------------------

struct TableLines {
    // Byte spans [start, end) of each table line, header first; the
    // delimiter row sits at index 1
    std::vector<std::pair<size_t, size_t>> lines;
};

bool lineHasPipe(const std::string& src, size_t ls, size_t le) {
    for (size_t i = ls; i < le; i++) {
        if (src[i] == '|') return true;
        if (src[i] == '\\') i++;
    }
    return false;
}

// Collect the pipe-table's lines around the table's source offset
bool collectTableLines(const std::string& src, size_t tableSrc,
                       TableLines& out) {
    if (tableSrc >= src.size()) return false;
    // Walk back to the start of the header line, then further up while
    // the previous line is still part of the table (the offset points at
    // the first cell text, which lives on the header line)
    size_t ls = src.rfind('\n', tableSrc);
    ls = (ls == std::string::npos) ? 0 : ls + 1;
    out.lines.clear();
    size_t pos = ls;
    while (pos < src.size()) {
        size_t le = src.find('\n', pos);
        if (le == std::string::npos) le = src.size();
        if (!lineHasPipe(src, pos, le)) break;
        out.lines.push_back({pos, le});
        if (le >= src.size()) break;
        pos = le + 1;
    }
    return out.lines.size() >= 2;  // header + delimiter at minimum
}

// Unescaped pipe positions within a line
std::vector<size_t> pipePositions(const std::string& src, size_t ls,
                                  size_t le) {
    std::vector<size_t> pipes;
    for (size_t i = ls; i < le; i++) {
        if (src[i] == '\\') { i++; continue; }
        if (src[i] == '|') pipes.push_back(i);
    }
    return pipes;
}

// Trimmed byte span of cell (row, col); row 0 = header, body rows skip
// the delimiter line. start == end marks an empty cell insertion point.
bool cellByteRange(const std::string& src, const TableLines& t, int row,
                   int col, size_t& start, size_t& end) {
    size_t lineIdx = row == 0 ? 0 : (size_t)row + 1;
    if (lineIdx >= t.lines.size() || lineIdx == 1) return false;
    size_t ls = t.lines[lineIdx].first, le = t.lines[lineIdx].second;
    std::vector<size_t> pipes = pipePositions(src, ls, le);
    if (pipes.empty()) return false;

    // Leading pipe? Cells sit between consecutive pipes; without one,
    // the first cell starts at the line start
    size_t firstContent = ls;
    while (firstContent < le && (src[firstContent] == ' ' ||
                                 src[firstContent] == '\t')) {
        firstContent++;
    }
    bool leadingPipe = firstContent < le && src[firstContent] == '|';

    size_t cellStart, cellEnd;
    if (leadingPipe) {
        if ((size_t)col + 1 > pipes.size()) return false;
        cellStart = pipes[col] + 1;
        cellEnd = ((size_t)col + 1 < pipes.size()) ? pipes[col + 1] : le;
    } else {
        if (col == 0) {
            cellStart = ls;
            cellEnd = pipes[0];
        } else {
            if ((size_t)col > pipes.size()) return false;
            cellStart = pipes[col - 1] + 1;
            cellEnd = ((size_t)col < pipes.size()) ? pipes[col] : le;
        }
    }
    if (cellEnd < cellStart) return false;

    while (cellStart < cellEnd && (src[cellStart] == ' ' ||
                                   src[cellStart] == '\t')) {
        cellStart++;
    }
    while (cellEnd > cellStart && (src[cellEnd - 1] == ' ' ||
                                   src[cellEnd - 1] == '\t')) {
        cellEnd--;
    }
    start = cellStart;
    end = cellEnd;
    return true;
}

int tableColumnCount(const std::string& src, const TableLines& t) {
    // The delimiter line defines the column count
    std::vector<size_t> pipes =
        pipePositions(src, t.lines[1].first, t.lines[1].second);
    if (pipes.empty()) return 0;
    size_t ls = t.lines[1].first;
    while (ls < t.lines[1].second && (src[ls] == ' ' || src[ls] == '\t')) ls++;
    bool leading = ls < t.lines[1].second && src[ls] == '|';
    size_t le = t.lines[1].second;
    size_t trimmedEnd = le;
    while (trimmedEnd > ls && (src[trimmedEnd - 1] == ' ' ||
                               src[trimmedEnd - 1] == '\t')) {
        trimmedEnd--;
    }
    bool trailing = trimmedEnd > ls && src[trimmedEnd - 1] == '|';
    int cells = (int)pipes.size() - 1;
    if (!leading) cells++;
    if (!trailing) cells++;
    return std::max(0, cells);
}

int tableBodyRowCount(const TableLines& t) {
    return (int)t.lines.size() - 2;
}

// Escape pipes and flatten newlines so the cell text stays one cell
std::wstring sanitizeCellText(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t c : text) {
        if (c == L'\n' || c == L'\r') {
            out += L' ';
        } else if (c == L'|') {
            out += L"\\|";
        } else {
            out += c;
        }
    }
    return out;
}

// Fresh source text plus this table's lines; false when the table is gone
bool currentTable(App& app, std::string& src, TableLines& t) {
    src = toUtf8(app.editorText);
    return collectTableLines(src, app.tableEditSrc, t);
}

void closeCellEditor(App& app) {
    app.tableEditActive = false;
    app.tableEditRow = -1;
    app.tableEditCol = -1;
    app.tableEditText.clear();
    app.tableEditCaret = 0;
}

// Load cell (row, col) of the table at tableSrc into the inline editor
bool openCellEditor(App& app, size_t tableSrc, int row, int col) {
    app.tableEditSrc = tableSrc;
    std::string src;
    TableLines t;
    if (!currentTable(app, src, t)) return false;
    size_t s, e;
    if (!cellByteRange(src, t, row, col, s, e)) return false;
    app.tableEditActive = true;
    app.tableEditRow = row;
    app.tableEditCol = col;
    app.tableEditText = toWide(src.substr(s, e - s));
    app.tableEditCaret = app.tableEditText.size();
    resetCursorBlink(app);
    return true;
}

// Replace the open cell's source with the editor text
void commitCellEditor(App& app) {
    if (!app.tableEditActive) return;
    std::string src;
    TableLines t;
    if (currentTable(app, src, t)) {
        size_t s, e;
        if (cellByteRange(src, t, app.tableEditRow, app.tableEditCol, s, e)) {
            std::wstring repl = sanitizeCellText(app.tableEditText);
            size_t ws = wideIndexForUtf8(app.editorText, s);
            size_t we = wideIndexForUtf8(app.editorText, e);
            std::wstring current = app.editorText.substr(ws, we - ws);
            if (current != repl) {
                // An empty cell's insertion point may sit flush against
                // the pipe; pad the replacement for readable source
                if (ws == we && !repl.empty()) repl = L" " + repl + L" ";
                editorReplaceRangeExternal(app, ws, we, repl);
                editorMarkDirtyAndReparse(app);
            }
        }
    }
    closeCellEditor(app);
}

// Append an empty row after the last body row
void insertTableRow(App& app, size_t tableSrc) {
    app.tableEditSrc = tableSrc;
    std::string src;
    TableLines t;
    if (!currentTable(app, src, t)) return;
    int cols = tableColumnCount(src, t);
    if (cols <= 0) return;
    std::wstring row = L"\n|";
    for (int c = 0; c < cols; c++) row += L"   |";
    size_t insertAt =
        wideIndexForUtf8(app.editorText, t.lines.back().second);
    editorReplaceRangeExternal(app, insertAt, insertAt, row);
    editorMarkDirtyAndReparse(app);
}

// Append an empty column to every table line (delimiter included)
void insertTableColumn(App& app, size_t tableSrc) {
    app.tableEditSrc = tableSrc;
    std::string src;
    TableLines t;
    if (!currentTable(app, src, t)) return;
    // Back to front so earlier byte offsets stay valid
    for (size_t i = t.lines.size(); i-- > 0;) {
        size_t ls = t.lines[i].first, le = t.lines[i].second;
        size_t end = le;
        while (end > ls && (src[end - 1] == ' ' || src[end - 1] == '\t' ||
                            src[end - 1] == '\r')) {
            end--;
        }
        bool trailingPipe = end > ls && src[end - 1] == '|';
        std::wstring add;
        if (!trailingPipe) add += L" |";
        add += (i == 1) ? L" --- |" : L"   |";
        size_t at = wideIndexForUtf8(app.editorText, end);
        editorReplaceRangeExternal(app, at, at, add);
    }
    editorMarkDirtyAndReparse(app);
}

// Cell under a document point, using the layout's recorded rects
const App::TableCellRect* cellAt(const App& app, float docX, float docY) {
    for (const auto& c : app.tableCellRects) {
        if (docX >= c.rect.left && docX <= c.rect.right &&
            docY >= c.rect.top && docY <= c.rect.bottom) {
            return &c;
        }
    }
    return nullptr;
}

// The open cell's current document rect, or nullptr after a relayout
// that dropped it
const App::TableCellRect* activeCellRect(const App& app) {
    for (const auto& c : app.tableCellRects) {
        if (c.tableSrc == app.tableEditSrc && c.row == app.tableEditRow &&
            c.col == app.tableEditCol) {
            return &c;
        }
    }
    return nullptr;
}

}  // namespace

bool tableEditMouseDown(App& app, HWND hwnd, float docX, float docY) {
    // + row / + column affordances first: they sit outside cell rects
    if (app.tableAddRowRect.right > app.tableAddRowRect.left &&
        docX >= app.tableAddRowRect.left && docX <= app.tableAddRowRect.right &&
        docY >= app.tableAddRowRect.top && docY <= app.tableAddRowRect.bottom) {
        tableEditCommit(app);
        insertTableRow(app, app.tableAddSrc);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (app.tableAddColRect.right > app.tableAddColRect.left &&
        docX >= app.tableAddColRect.left && docX <= app.tableAddColRect.right &&
        docY >= app.tableAddColRect.top && docY <= app.tableAddColRect.bottom) {
        tableEditCommit(app);
        insertTableColumn(app, app.tableAddSrc);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }

    const App::TableCellRect* cell = cellAt(app, docX, docY);
    if (app.tableEditActive) {
        if (cell && cell->tableSrc == app.tableEditSrc &&
            cell->row == app.tableEditRow && cell->col == app.tableEditCol) {
            return true;  // click inside the open editor keeps focus
        }
        tableEditCommit(app);
        // fall through: the same click may open the next cell
    }
    if (cell) {
        openCellEditor(app, cell->tableSrc, cell->row, cell->col);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    return false;
}

bool tableEditKeyDown(App& app, HWND hwnd, WPARAM key) {
    if (!app.tableEditActive) return false;
    // Ctrl chords pass through (save commits the open cell first)
    if (GetKeyState(VK_CONTROL) & 0x8000) return false;
    switch (key) {
        case VK_ESCAPE:
            tableEditCancel(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        case VK_RETURN:
            tableEditCommit(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        case VK_TAB: {
            bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            size_t tableSrc = app.tableEditSrc;
            int row = app.tableEditRow, col = app.tableEditCol;
            tableEditCommit(app);

            std::string src;
            TableLines t;
            app.tableEditSrc = tableSrc;
            if (!currentTable(app, src, t)) return true;
            int cols = tableColumnCount(src, t);
            int bodyRows = tableBodyRowCount(t);
            if (back) {
                if (--col < 0) {
                    col = cols - 1;
                    row = row == 0 ? 0 : row - 1;
                }
            } else if (++col >= cols) {
                col = 0;
                row++;
                if (row > bodyRows) {
                    // Tab past the last cell grows the table (notion-style)
                    insertTableRow(app, tableSrc);
                }
            }
            openCellEditor(app, tableSrc, row, col);
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        case VK_BACK:
            if (app.tableEditCaret > 0) {
                app.tableEditText.erase(--app.tableEditCaret, 1);
                resetCursorBlink(app);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        case VK_DELETE:
            if (app.tableEditCaret < app.tableEditText.size()) {
                app.tableEditText.erase(app.tableEditCaret, 1);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return true;
        case VK_LEFT:
            if (app.tableEditCaret > 0) app.tableEditCaret--;
            resetCursorBlink(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        case VK_RIGHT:
            if (app.tableEditCaret < app.tableEditText.size())
                app.tableEditCaret++;
            resetCursorBlink(app);
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        case VK_HOME:
            app.tableEditCaret = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        case VK_END:
            app.tableEditCaret = app.tableEditText.size();
            InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        default:
            return true;  // the open cell owns the keyboard
    }
}

bool tableEditChar(App& app, wchar_t ch) {
    if (!app.tableEditActive) return false;
    if (ch < 0x20 || ch == 127) return true;  // controls handled as keys
    app.tableEditText.insert(app.tableEditCaret++, 1, ch);
    resetCursorBlink(app);
    if (app.hwnd) InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
}

void tableEditCommit(App& app) {
    commitCellEditor(app);
}

void tableEditCancel(App& app) {
    closeCellEditor(app);
}

void renderTableEditOverlay(App& app) {
    if (!app.editMode || !editorPreviewVisible(app)) return;
    if (!app.renderTarget || !app.brush || !app.textFormat) return;

    float viewX = documentViewportX(app);
    auto toScreen = [&](const D2D1_RECT_F& r) {
        return D2D1::RectF(r.left + viewX - app.scrollX, r.top - app.scrollY,
                           r.right + viewX - app.scrollX,
                           r.bottom - app.scrollY);
    };

    // Hover affordances: + row under the table, + column at its right.
    // Rects live in document coordinates for the input hit-test.
    app.tableAddRowRect = app.tableAddColRect = D2D1_RECT_F{};
    float scale = app.contentScale * app.zoomFactor;
    float band = 14.0f * scale;
    float docMouseX = (float)app.mouseX - viewX + app.scrollX;
    float docMouseY = (float)app.mouseY + app.scrollY;
    for (const auto& tbl : app.tableRects) {
        if (tbl.sourceOffset == SIZE_MAX) continue;
        bool nearTable = docMouseX >= tbl.bounds.left - band &&
                         docMouseX <= tbl.bounds.right + band * 2 &&
                         docMouseY >= tbl.bounds.top - band &&
                         docMouseY <= tbl.bounds.bottom + band * 2;
        if (!nearTable && !(app.tableEditActive &&
                            tbl.sourceOffset == app.tableEditSrc)) {
            continue;
        }
        app.tableAddSrc = tbl.sourceOffset;
        app.tableAddRowRect =
            D2D1::RectF(tbl.bounds.left, tbl.bounds.bottom,
                        tbl.bounds.right, tbl.bounds.bottom + band);
        app.tableAddColRect =
            D2D1::RectF(tbl.bounds.right, tbl.bounds.top,
                        tbl.bounds.right + band, tbl.bounds.bottom);

        auto drawPlusBand = [&](const D2D1_RECT_F& docRect, bool hovered) {
            D2D1_RECT_F r = toScreen(docRect);
            D2D1_COLOR_F bg = app.theme.accent;
            bg.a = hovered ? 0.18f : 0.07f;
            app.brush->SetColor(bg);
            app.renderTarget->FillRoundedRectangle(
                D2D1::RoundedRect(r, 3.0f * scale, 3.0f * scale), app.brush);
            D2D1_COLOR_F ink = app.theme.accent;
            ink.a = hovered ? 0.95f : 0.55f;
            app.brush->SetColor(ink);
            float cx = (r.left + r.right) * 0.5f;
            float cy = (r.top + r.bottom) * 0.5f;
            float s = 4.0f * scale;
            app.renderTarget->DrawLine(D2D1::Point2F(cx - s, cy),
                                       D2D1::Point2F(cx + s, cy), app.brush,
                                       1.4f);
            app.renderTarget->DrawLine(D2D1::Point2F(cx, cy - s),
                                       D2D1::Point2F(cx, cy + s), app.brush,
                                       1.4f);
        };
        bool rowHover = docMouseX >= app.tableAddRowRect.left &&
                        docMouseX <= app.tableAddRowRect.right &&
                        docMouseY >= app.tableAddRowRect.top &&
                        docMouseY <= app.tableAddRowRect.bottom;
        bool colHover = docMouseX >= app.tableAddColRect.left &&
                        docMouseX <= app.tableAddColRect.right &&
                        docMouseY >= app.tableAddColRect.top &&
                        docMouseY <= app.tableAddColRect.bottom;
        drawPlusBand(app.tableAddRowRect, rowHover);
        drawPlusBand(app.tableAddColRect, colHover);
        break;  // one table's affordances at a time
    }

    // The inline cell input
    if (!app.tableEditActive) return;
    const App::TableCellRect* cell = activeCellRect(app);
    if (!cell) return;  // relayout mid-frame; the next paint finds it

    D2D1_RECT_F r = toScreen(cell->rect);
    float pad = 8.0f * scale;
    D2D1_COLOR_F fill = app.theme.background;
    fill.a = 1.0f;
    app.brush->SetColor(fill);
    app.renderTarget->FillRectangle(r, app.brush);
    D2D1_COLOR_F ring = app.theme.accent;
    ring.a = 0.9f;
    app.brush->SetColor(ring);
    app.renderTarget->DrawRectangle(r, app.brush, 1.5f);

    D2D1_COLOR_F ink = app.theme.text;
    app.brush->SetColor(ink);
    app.renderTarget->DrawText(
        app.tableEditText.c_str(), (UINT32)app.tableEditText.size(),
        app.textFormat,
        D2D1::RectF(r.left + pad, r.top + pad, r.right - pad, r.bottom),
        app.brush);

    if (app.cursorBlinkOn) {
        std::wstring beforeCaret =
            app.tableEditText.substr(0, app.tableEditCaret);
        float cw = beforeCaret.empty()
                       ? 0.0f
                       : measureText(app, beforeCaret, app.textFormat);
        float cx = r.left + pad + cw + 1.0f;
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx, r.top + pad),
            D2D1::Point2F(cx, r.top + pad + app.textFormat->GetFontSize() *
                                                1.4f),
            app.brush, 1.2f);
    }
}
