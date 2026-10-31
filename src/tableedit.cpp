#include "search.h"
#include "tableedit.h"
#include "editor.h"
#include "input.h"
#include "utils.h"

#include <algorithm>
#include <cwctype>
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
            size_t slashes=0;
            for(size_t i=out.size(); i && out[i-1]==L'\\'; --i) ++slashes;
            if (slashes%2==0) out+=L'\\';
            out+=c;
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
    tableEditMouseUp(app);
    app.tableEditActive = false;
    app.tableEditRow = -1;
    app.tableEditCol = -1;
    app.tableEditText.clear();
    app.tableEditCaret = 0;
    app.tableEditAnchor = 0;
    app.tableEditScrollY = 0;
    app.tableEditUndo.clear();
    app.tableEditRedo.clear();
}

// Load cell (row, col) of the table at tableSrc into the inline editor
bool openCellEditor(App& app, size_t tableSrc, int row, int col) {
    app.tableEditSrc = tableSrc;
    std::string src;
    TableLines t;
    if (!currentTable(app, src, t)) return false;
    size_t s, e;
    if (!cellByteRange(src, t, row, col, s, e)) return false;
    releaseSearchInput(app);
    app.tableEditActive = true;
    app.tableEditRow = row;
    app.tableEditCol = col;
    app.tableEditText = toWide(src.substr(s, e - s));
    app.tableEditCaret = app.tableEditText.size();
    app.tableEditAnchor = app.tableEditCaret;
    app.tableEditScrollY = 0;
    app.tableEditUndo.clear();
    app.tableEditRedo.clear();
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

using Microsoft::WRL::ComPtr;

struct CellLayout {
    ComPtr<IDWriteTextLayout> text;
    D2D1_RECT_F box{}; // document coordinates, inside the cell padding
};
CellLayout cellLayout(App& app) {
    CellLayout result;
    const auto* cell = activeCellRect(app);
    if (!cell || !app.dwriteFactory || !app.textFormat) return result;
    float pad = 8.0f * app.contentScale * app.zoomFactor;
    result.box = {cell->rect.left+pad, cell->rect.top+pad,
                  std::max(cell->rect.left+pad+1, cell->rect.right-pad),
                  std::max(cell->rect.top+pad+app.textFormat->GetFontSize()*1.4f, cell->rect.bottom-pad)};
    app.dwriteFactory->CreateTextLayout(app.tableEditText.data(),
        static_cast<UINT32>(app.tableEditText.size()), app.textFormat,
        result.box.right-result.box.left, 1e7f, result.text.GetAddressOf());
    if (result.text) {
        result.text->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        ComPtr<IDWriteTextLayout2> fallback;
        if (app.fontFallback && SUCCEEDED(result.text.As(&fallback))) fallback->SetFontFallback(app.fontFallback);
    }
    return result;
}
DWRITE_HIT_TEST_METRICS caretMetrics(App& app, IDWriteTextLayout* layout, float& x, float& y) {
    DWRITE_HIT_TEST_METRICS hit{};
    if (layout) layout->HitTestTextPosition(static_cast<UINT32>(app.tableEditCaret), FALSE, &x, &y, &hit);
    if (hit.height <= 0 && app.textFormat) hit.height = app.textFormat->GetFontSize()*1.4f;
    return hit;
}
void followCaret(App& app, const CellLayout& layout) {
    float x=0, y=0;
    auto hit=caretMetrics(app, layout.text.Get(), x, y);
    float height=layout.box.bottom-layout.box.top;
    if (y < app.tableEditScrollY) app.tableEditScrollY=y;
    if (y+hit.height > app.tableEditScrollY+height) app.tableEditScrollY=y+hit.height-height;
    app.tableEditScrollY=std::max(0.0f,app.tableEditScrollY);
}
size_t hitCell(App& app, const CellLayout& layout, float x, float y) {
    if (!layout.text) return 0;
    BOOL trailing=FALSE, inside=FALSE;
    DWRITE_HIT_TEST_METRICS hit{};
    layout.text->HitTestPoint(x-layout.box.left, y-layout.box.top+app.tableEditScrollY, &trailing, &inside, &hit);
    return std::min(app.tableEditText.size(), static_cast<size_t>(hit.textPosition)+(trailing ? hit.length : 0));
}
size_t stepCell(App& app, size_t pos, bool forward) {
    auto layout=cellLayout(app);
    if (layout.text) {
        UINT32 count=0;
        layout.text->GetClusterMetrics(nullptr,0,&count);
        std::vector<DWRITE_CLUSTER_METRICS> clusters(count);
        if (count && SUCCEEDED(layout.text->GetClusterMetrics(clusters.data(),count,&count))) {
            size_t start=0;
            for (const auto& cluster:clusters) {
                size_t end=start+cluster.length;
                if ((forward && end>pos) || (!forward && end>=pos)) return forward ? end : start;
                start=end;
            }
        }
    }
    return forward ? std::min(pos+1,app.tableEditText.size()) : (pos ? pos-1 : 0);
}
size_t wordCell(App& app, size_t pos, bool forward) {
    const auto& text=app.tableEditText;
    auto category=[&](size_t at){return iswspace(text[at]) ? 0 : (iswalnum(text[at]) || text[at]==L'_' ? 1 : 2);};
    if (forward) {
        if (pos==text.size()) return pos;
        int kind=category(pos);
        while(pos<text.size() && category(pos)==kind) pos=stepCell(app,pos,true);
        while(pos<text.size() && !category(pos)) pos=stepCell(app,pos,true);
    } else {
        while(pos && !category(pos-1)) pos=stepCell(app,pos,false);
        if (!pos) return 0;
        int kind=category(pos-1);
        while(pos && category(pos-1)==kind) pos=stepCell(app,pos,false);
    }
    return pos;
}
void insertCell(App& app, const std::wstring& text) {
    size_t start=std::min(app.tableEditCaret,app.tableEditAnchor);
    size_t end=std::max(app.tableEditCaret,app.tableEditAnchor);
    if (start==end && text.empty()) return;
    app.tableEditUndo.push_back({app.tableEditText,app.tableEditCaret,app.tableEditAnchor});
    app.tableEditRedo.clear();
    app.tableEditText.replace(start,end-start,text);
    app.tableEditCaret=app.tableEditAnchor=start+text.size();
}
void cellChanged(App& app) {
    auto layout=cellLayout(app);
    followCaret(app,layout);
    resetCursorBlink(app);
    InvalidateRect(app.hwnd,nullptr,FALSE);
}
void placeCellCaret(App& app, float x, float y) {
    auto layout=cellLayout(app);
    app.tableEditCaret=hitCell(app,layout,x,y);
    if (!(GetKeyState(VK_SHIFT)&0x8000)) app.tableEditAnchor=app.tableEditCaret;
    app.tableEditSelecting=true;
    app.hasSelection=app.selecting=false;
    SetCapture(app.hwnd);
    cellChanged(app);
}

}  // namespace

// --- Excel/TSV paste (#181) -----------------------------------------

// Clipboard text as lines, tolerating CRLF; Excel terminates a range
// copy with one newline, so trailing empty lines are dropped
static std::vector<std::wstring> tsvLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find(L'\n', start);
        std::wstring line = (nl == std::wstring::npos)
            ? text.substr(start) : text.substr(start, nl - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

// A paste reads as a spreadsheet grid when it has 2+ rows and every row
// carries the same number of tabs - exactly what Excel (or any TSV
// export) puts on the clipboard for a range copy. Single lines and
// ragged tab counts paste untouched; tabs in code are common.
bool tsvLooksLikeTable(const std::wstring& text) {
    if (text.find(L'\t') == std::wstring::npos) return false;
    std::vector<std::wstring> lines = tsvLines(text);
    if (lines.size() < 2) return false;
    size_t tabs = std::wstring::npos;
    for (const std::wstring& line : lines) {
        size_t count = (size_t)std::count(line.begin(), line.end(), L'\t');
        if (count == 0) return false;
        if (tabs == std::wstring::npos) tabs = count;
        else if (count != tabs) return false;
    }
    return true;
}

// The grid as a markdown table: first row becomes the header, cells are
// trimmed and pipe-escaped. No trailing newline - the caller pads for
// its insertion context.
std::wstring tsvToMarkdownTable(const std::wstring& text) {
    std::vector<std::wstring> lines = tsvLines(text);
    std::wstring out;
    size_t cols = 0;
    for (size_t r = 0; r < lines.size(); r++) {
        const std::wstring& line = lines[r];
        std::vector<std::wstring> cells;
        size_t start = 0;
        while (start <= line.size()) {
            size_t tab = line.find(L'\t', start);
            std::wstring cell = (tab == std::wstring::npos)
                ? line.substr(start) : line.substr(start, tab - start);
            size_t a = cell.find_first_not_of(L' ');
            size_t b = cell.find_last_not_of(L' ');
            cell = (a == std::wstring::npos) ? L"" : cell.substr(a, b - a + 1);
            cells.push_back(sanitizeCellText(cell));
            if (tab == std::wstring::npos) break;
            start = tab + 1;
        }
        if (r == 0) cols = cells.size();
        cells.resize(cols);
        out += L"|";
        for (const std::wstring& cell : cells) {
            out += L" " + cell + L" |";
        }
        if (r == 0) {
            out += L"\n|";
            for (size_t c = 0; c < cols; c++) out += L" --- |";
        }
        if (r + 1 < lines.size()) out += L"\n";
    }
    return out;
}

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
            placeCellCaret(app,docX,docY);
            return true;
        }
        tableEditCommit(app);
        // fall through: the same click may open the next cell
    }
    if (cell) {
        if (openCellEditor(app, cell->tableSrc, cell->row, cell->col)) placeCellCaret(app,docX,docY);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    return false;
}

bool tableEditKeyDown(App& app, HWND hwnd, WPARAM key) {
    if (!app.tableEditActive) return false;
    if (!shortcutModifiersAllowed(static_cast<unsigned>(key))) return false;
    bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
    bool shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
    if (ctrl) {
        size_t start=std::min(app.tableEditCaret,app.tableEditAnchor), end=std::max(app.tableEditCaret,app.tableEditAnchor);
        if (key=='A') { app.tableEditAnchor=0; app.tableEditCaret=app.tableEditText.size(); }
        else if (key=='C' || key=='X') {
            if (start!=end) {
                bool held=false;
                bool copied=copyToClipboard(hwnd,app.tableEditText.substr(start,end-start),&held);
                if (copied && key=='X') insertCell(app,L"");
                if (held) signalClipboardBusy(app);
            }
        } else if (key=='V') {
            // Cell paste is always plain text, never an entire new table.
            auto pasted=clipboardLine(hwnd);
            if (!pasted.empty()) insertCell(app,pasted);
        } else if (key=='Z' || key=='Y') {
            auto& from=key=='Z' ? app.tableEditUndo : app.tableEditRedo;
            auto& to=key=='Z' ? app.tableEditRedo : app.tableEditUndo;
            if (!from.empty()) {
                to.push_back({app.tableEditText,app.tableEditCaret,app.tableEditAnchor});
                auto saved=std::move(from.back()); from.pop_back();
                app.tableEditText=std::move(saved.text); app.tableEditCaret=saved.caret; app.tableEditAnchor=saved.anchor;
            }
        } else if (key!=VK_LEFT && key!=VK_RIGHT && key!=VK_HOME && key!=VK_END && key!=VK_BACK && key!=VK_DELETE) {
            return false; // save, search and other application commands
        } else goto navigation;
        cellChanged(app);
        return true;
    }
navigation:
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
            bool back = shift;
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
        case VK_DELETE:
            if (app.tableEditCaret==app.tableEditAnchor) app.tableEditAnchor=ctrl ? wordCell(app,app.tableEditCaret,key==VK_DELETE) : stepCell(app,app.tableEditCaret,key==VK_DELETE);
            insertCell(app,L"");
            break;
        case VK_LEFT:
        case VK_RIGHT:
            if (!shift && app.tableEditCaret!=app.tableEditAnchor) app.tableEditCaret=key==VK_RIGHT ? std::max(app.tableEditCaret,app.tableEditAnchor) : std::min(app.tableEditCaret,app.tableEditAnchor);
            else app.tableEditCaret=ctrl ? wordCell(app,app.tableEditCaret,key==VK_RIGHT) : stepCell(app,app.tableEditCaret,key==VK_RIGHT);
            if (!shift) app.tableEditAnchor=app.tableEditCaret;
            break;
        case VK_HOME:
        case VK_END:
        case VK_UP:
        case VK_DOWN: {
            auto layout=cellLayout(app);
            float x=0,y=0; auto hit=caretMetrics(app,layout.text.Get(),x,y);
            if (ctrl && (key==VK_HOME || key==VK_END)) app.tableEditCaret=key==VK_HOME ? 0 : app.tableEditText.size();
            else {
                if (key==VK_HOME) x=-1;
                if (key==VK_END) x=layout.box.right-layout.box.left+1;
                y+=hit.height*(key==VK_UP ? -0.5f : key==VK_DOWN ? 1.5f : 0.5f);
                app.tableEditCaret=hitCell(app,layout,layout.box.left+x,layout.box.top+y-app.tableEditScrollY);
            }
            if (!shift) app.tableEditAnchor=app.tableEditCaret;
            break;
        }
        default:
            return true;  // the open cell owns the keyboard
    }
    cellChanged(app);
    return true;
}

bool tableEditChar(App& app, wchar_t ch) {
    if (!app.tableEditActive) return false;
    if (ch < 0x20 || ch == 127) return true;  // controls handled as keys
    insertCell(app,std::wstring(1,ch));
    cellChanged(app);
    return true;
}

void tableEditCommit(App& app) {
    commitCellEditor(app);
}

void tableEditCancel(App& app) {
    closeCellEditor(app);
}

bool tableEditCaretPoint(App& app, D2D1_POINT_2F& point) {
    if (!app.tableEditActive || !app.textFormat) return false;
    auto layout=cellLayout(app);
    if (!layout.text) return false;
    followCaret(app,layout);
    float x=0,y=0; auto hit=caretMetrics(app,layout.text.Get(),x,y);
    point.x=documentViewportX(app)-app.scrollX+layout.box.left+x;
    point.y=layout.box.top-app.scrollY+y-app.tableEditScrollY+hit.height;
    return true;
}

bool tableEditMouseMove(App& app, float screenX, float screenY) {
    if (!app.tableEditSelecting) return false;
    if (GetCapture()!=app.hwnd) { app.tableEditSelecting=false; return false; }
    auto layout=cellLayout(app);
    app.tableEditCaret=hitCell(app,layout,screenX-documentViewportX(app)+app.scrollX,screenY+app.scrollY);
    cellChanged(app);
    return true;
}
void tableEditMouseUp(App& app) {
    if (!app.tableEditSelecting) return;
    app.tableEditSelecting=false;
    app.swallowNextMouseUp=false;
    if (GetCapture()==app.hwnd) ReleaseCapture();
}

void renderTableEditOverlay(App& app) {
    if (app.editorReadingPreview || editorLiveOn(app)) return;
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
    auto layout=cellLayout(app);
    if (!layout.text) return;
    followCaret(app,layout);
    const auto box=toScreen(layout.box);
    auto origin=D2D1::Point2F(box.left,box.top-app.tableEditScrollY);
    app.renderTarget->PushAxisAlignedClip({box.left,box.top,box.right+2,box.bottom},D2D1_ANTIALIAS_MODE_ALIASED);
    size_t start=std::min(app.tableEditCaret,app.tableEditAnchor), end=std::max(app.tableEditCaret,app.tableEditAnchor);
    if (start!=end) {
        UINT32 count=0;
        layout.text->HitTestTextRange(static_cast<UINT32>(start),static_cast<UINT32>(end-start),origin.x,origin.y,nullptr,0,&count);
        std::vector<DWRITE_HIT_TEST_METRICS> rects(count);
        if (count && SUCCEEDED(layout.text->HitTestTextRange(static_cast<UINT32>(start),static_cast<UINT32>(end-start),origin.x,origin.y,rects.data(),count,&count))) {
            auto selected=app.theme.accent; selected.a=0.3f; app.brush->SetColor(selected);
            for(const auto& hit:rects) app.renderTarget->FillRectangle({hit.left,hit.top,hit.left+hit.width,hit.top+hit.height},app.brush);
        }
    }
    app.brush->SetColor(ink);
    app.renderTarget->DrawTextLayout(origin,layout.text.Get(),app.brush);
    if (app.cursorBlinkOn && start==end) {
        float x=0,y=0; auto hit=caretMetrics(app,layout.text.Get(),x,y);
        float cx=origin.x+x+1;
        app.brush->SetColor(app.theme.accent);
        app.renderTarget->DrawLine(
            D2D1::Point2F(cx,origin.y+y), D2D1::Point2F(cx,origin.y+y+hit.height),
            app.brush,std::max(1.5f,dpi(app,1.5f)));
    }
    app.renderTarget->PopAxisAlignedClip();
}
